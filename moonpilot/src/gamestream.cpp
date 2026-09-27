// GameStream pairing protocol adapted from Moonlight Embedded libgamestream,
// Copyright (C) 2015-2017 Iwan Timmer, GPL-3.0-or-later. See licenses/.
#include "gamestream.hpp"
#include "net.hpp"
#include "tinyxml2.h"
#include <mbedtls/aes.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
namespace moonpilot {
namespace {
using Bytes=std::vector<unsigned char>;
using Clock=std::chrono::steady_clock;
void check(int rc,const char*what){if(rc<0)throw std::runtime_error(what);}
std::string hex(const unsigned char*p,size_t n){const char*d="0123456789abcdef";std::string s;for(size_t i=0;i<n;i++){s+=d[p[i]>>4];s+=d[p[i]&15];}return s;}
std::string hex(const Bytes&b){return hex(b.data(),b.size());}
Bytes unhex(const std::string&s){
    if(s.empty()||s.size()%2||s.size()>32768)throw std::runtime_error("主机返回的配对数据无效");
    auto digit=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;throw std::runtime_error("无效的十六进制数据");};
    Bytes b;for(size_t i=0;i<s.size();i+=2)b.push_back(digit(s[i])*16+digit(s[i+1]));return b;
}
Bytes hash(const Bytes&b){Bytes h(32);check(mbedtls_sha256_ret(b.data(),b.size(),h.data(),0),"SHA256 失败");return h;}
void append(Bytes&b,const unsigned char*p,size_t n){b.insert(b.end(),p,p+n);}
Bytes aes(const Bytes&b,const Bytes&key,bool encrypt){
    if(b.empty()||b.size()%16||key.size()<16)throw std::runtime_error("配对数据长度不正确");
    mbedtls_aes_context a;mbedtls_aes_init(&a);Bytes out(b.size());
    int rc=encrypt?mbedtls_aes_setkey_enc(&a,key.data(),128):mbedtls_aes_setkey_dec(&a,key.data(),128);
    for(size_t i=0;rc==0&&i<b.size();i+=16)rc=mbedtls_aes_crypt_ecb(&a,encrypt?MBEDTLS_AES_ENCRYPT:MBEDTLS_AES_DECRYPT,b.data()+i,out.data()+i);
    mbedtls_aes_free(&a);check(rc,"配对加密失败");return out;
}
struct Cert {mbedtls_x509_crt value;Cert(){mbedtls_x509_crt_init(&value);}~Cert(){mbedtls_x509_crt_free(&value);}void parse(const std::string&s){check(mbedtls_x509_crt_parse(&value,(const unsigned char*)s.c_str(),s.size()+1),"证书无效");}};
struct Xml {
    tinyxml2::XMLDocument doc;
    explicit Xml(const std::string&s){if(doc.Parse(s.data(),s.size())!=tinyxml2::XML_SUCCESS||!doc.FirstChildElement("root"))throw std::runtime_error("Sunshine 返回了无效 XML");
        auto*r=doc.FirstChildElement("root");if(r->IntAttribute("status_code",0)!=200)throw std::runtime_error("Sunshine 拒绝请求（"+std::to_string(r->IntAttribute("status_code"))+"）");}
    std::string get(const char*k){auto*e=doc.FirstChildElement("root")->FirstChildElement(k);return e&&e->GetText()?e->GetText():"";}
    int number(const char*k,int fallback=0){auto s=get(k);if(s.empty())return fallback;size_t n=0;long v=std::stol(s,&n);if(n!=s.size()||v<0||v>INT32_MAX)throw std::runtime_error("Sunshine 数字字段无效");return int(v);}
};
}
void validate_host(const Host&h){
    in_addr v4{};in6_addr v6{};
    if(inet_pton(AF_INET,h.address.c_str(),&v4)!=1&&inet_pton(AF_INET6,h.address.c_str(),&v6)!=1)throw std::runtime_error("请输入电脑的 IPv4 或 IPv6 地址");
    if(h.port<1||h.port>65530)throw std::runtime_error("端口应为 1–65530，默认 47989");
    if(!((h.width==512&&h.height==288)||(h.width==640&&h.height==360)||(h.width==800&&h.height==450))||h.fps<5||h.fps>30||h.bitrate<250||h.bitrate>4000)throw std::runtime_error("串流参数超出设备范围");
}
struct GameStream::Impl {
    Host host;std::string dir,id,client_pem,pin_pem;std::atomic<bool>&cancel;
    mbedtls_entropy_context entropy;mbedtls_ctr_drbg_context rng;mbedtls_pk_context key;Cert client;
    int https_port=47984;
    Impl(Host h,std::string data,std::atomic<bool>&c):host(h),cancel(c){
        validate_host(host);mbedtls_entropy_init(&entropy);mbedtls_ctr_drbg_init(&rng);mbedtls_pk_init(&key);
        // Initialization is separate so a throwing operation still runs RAII cleanup.
        auto bytes=Bytes(h.address.begin(),h.address.end());auto port=std::to_string(h.port);append(bytes,(const unsigned char*)port.data(),port.size());
        dir=data+"/hosts/"+hex(hash(bytes)).substr(0,24);
    }
    ~Impl(){mbedtls_pk_free(&key);mbedtls_ctr_drbg_free(&rng);mbedtls_entropy_free(&entropy);}
    Bytes random(size_t n){Bytes b(n);check(mbedtls_ctr_drbg_random(&rng,b.data(),n),"随机数生成失败");return b;}
    void init(){
        check(mbedtls_ctr_drbg_seed(&rng,mbedtls_entropy_func,&entropy,(const unsigned char*)"c1-moonpilot",12),"随机源初始化失败");
        std::filesystem::create_directories(dir);
        auto keyfile=dir+"/client.key",certfile=dir+"/client.pem";
        if(!std::filesystem::exists(keyfile)&&!std::filesystem::exists(certfile)){
            check(mbedtls_pk_setup(&key,mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)),"RSA 初始化失败");
            check(mbedtls_rsa_gen_key(mbedtls_pk_rsa(key),mbedtls_ctr_drbg_random,&rng,2048,65537),"客户端密钥生成失败");
            mbedtls_x509write_cert crt;mbedtls_x509write_crt_init(&crt);mbedtls_mpi serial;mbedtls_mpi_init(&serial);
            auto rnd=random(16);rnd[0]&=0x7f;mbedtls_mpi_read_binary(&serial,rnd.data(),rnd.size());
            mbedtls_x509write_crt_set_md_alg(&crt,MBEDTLS_MD_SHA256);mbedtls_x509write_crt_set_subject_key(&crt,&key);mbedtls_x509write_crt_set_issuer_key(&crt,&key);
            mbedtls_x509write_crt_set_version(&crt,MBEDTLS_X509_CRT_VERSION_3);mbedtls_x509write_crt_set_serial(&crt,&serial);
            mbedtls_x509write_crt_set_subject_name(&crt,"CN=NVIDIA GameStream Client");mbedtls_x509write_crt_set_issuer_name(&crt,"CN=NVIDIA GameStream Client");
            mbedtls_x509write_crt_set_validity(&crt,"20200101000000","20491231235959");
            std::array<unsigned char,8192> pem{};int rc=mbedtls_x509write_crt_pem(&crt,pem.data(),pem.size(),mbedtls_ctr_drbg_random,&rng);mbedtls_x509write_crt_free(&crt);mbedtls_mpi_free(&serial);check(rc,"客户端证书生成失败");
            client_pem=(char*)pem.data();check(mbedtls_pk_write_key_pem(&key,pem.data(),pem.size()),"密钥编码失败");
            c1::save_private(keyfile,(char*)pem.data());c1::save_private(certfile,client_pem);
        }else{
            auto data=c1::read_file(keyfile,8192);check(mbedtls_pk_parse_key(&key,(const unsigned char*)data.c_str(),data.size()+1,nullptr,0),"客户端密钥损坏");client_pem=c1::read_file(certfile,8192);
        }
        client.parse(client_pem);if(std::filesystem::exists(dir+"/id"))id=c1::read_file(dir+"/id",64);if(id.empty()){id=hex(random(8));c1::save_private(dir+"/id",id);}if(id.size()!=16||id.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("客户端 ID 损坏");
        if(std::filesystem::exists(dir+"/server.pem"))pin_pem=c1::read_file(dir+"/server.pem",8192);
    }
    std::string request(const std::string&path,bool tls,int timeout=12){
        if(cancel)throw std::runtime_error("已取消连接");if(tls&&pin_pem.empty())throw std::runtime_error("请先通过 PIN 配对此主机");
        struct Connection {
            mbedtls_net_context net;mbedtls_ssl_context ssl;mbedtls_ssl_config conf;
            Connection(){mbedtls_net_init(&net);mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&conf);}
            ~Connection(){mbedtls_net_free(&net);mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&conf);}
        }conn;
        sockaddr_storage ss{};socklen_t len;int family=host.address.find(':')==std::string::npos?AF_INET:AF_INET6;
        if(family==AF_INET){auto*a=(sockaddr_in*)&ss;a->sin_family=family;a->sin_port=htons(tls?https_port:host.port);inet_pton(family,host.address.c_str(),&a->sin_addr);len=sizeof(*a);}else{auto*a=(sockaddr_in6*)&ss;a->sin6_family=family;a->sin6_port=htons(tls?https_port:host.port);inet_pton(family,host.address.c_str(),&a->sin6_addr);len=sizeof(*a);}
        conn.net.fd=socket(family,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(conn.net.fd<0)throw std::runtime_error("无法创建网络连接");
        auto deadline=Clock::now()+std::chrono::seconds(timeout);
        auto wait=[&](short events){while(true){if(cancel)throw std::runtime_error("已取消连接");if(Clock::now()>deadline)throw std::runtime_error("Sunshine 请求超时");pollfd p{conn.net.fd,events,0};int n=poll(&p,1,100);if(n>0)return;if(n<0&&errno!=EINTR)throw std::runtime_error("网络连接失败");}};
        if(connect(conn.net.fd,(sockaddr*)&ss,len)<0){if(errno!=EINPROGRESS)throw std::runtime_error("无法连接 Sunshine");wait(POLLOUT);int e=0;socklen_t n=sizeof(e);getsockopt(conn.net.fd,SOL_SOCKET,SO_ERROR,&e,&n);if(e)throw std::runtime_error("无法连接 Sunshine，请检查地址和端口");}
        Cert expected;
        if(tls){
            expected.parse(pin_pem);check(mbedtls_ssl_config_defaults(&conn.conf,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT),"TLS 初始化失败");
            mbedtls_ssl_conf_rng(&conn.conf,mbedtls_ctr_drbg_random,&rng);mbedtls_ssl_conf_authmode(&conn.conf,MBEDTLS_SSL_VERIFY_REQUIRED);
            mbedtls_ssl_conf_ca_chain(&conn.conf,&expected.value,nullptr);
            mbedtls_ssl_conf_verify(&conn.conf,[](void*p,mbedtls_x509_crt*crt,int depth,uint32_t*flags){auto*expected=(mbedtls_x509_crt*)p;*flags=depth==0&&(crt->raw.len!=expected->raw.len||memcmp(crt->raw.p,expected->raw.p,crt->raw.len))?MBEDTLS_X509_BADCERT_NOT_TRUSTED:0;return 0;},&expected.value);
            check(mbedtls_ssl_conf_own_cert(&conn.conf,&client.value,&key),"TLS 客户端证书失败");check(mbedtls_ssl_setup(&conn.ssl,&conn.conf),"TLS 设置失败");
            check(mbedtls_ssl_set_hostname(&conn.ssl,host.address.c_str()),"TLS 主机名设置失败");
            mbedtls_ssl_set_bio(&conn.ssl,&conn.net,mbedtls_net_send,mbedtls_net_recv,nullptr);
            for(;;){int rc=mbedtls_ssl_handshake(&conn.ssl);if(!rc)break;if(rc==MBEDTLS_ERR_SSL_WANT_READ)wait(POLLIN);else if(rc==MBEDTLS_ERR_SSL_WANT_WRITE)wait(POLLOUT);else throw std::runtime_error("TLS 校验失败：证书变化或主机拒绝配对（"+std::to_string(rc)+"）");}
        }
        std::string target=path+(path.find('?')==std::string::npos?"?":"&")+"uniqueid="+id+"&uuid="+hex(random(16));
        std::string req="GET "+target+" HTTP/1.0\r\nHost: "+host.address+"\r\nConnection: close\r\n\r\n";
        size_t off=0;while(off<req.size()){int n=tls?mbedtls_ssl_write(&conn.ssl,(const unsigned char*)req.data()+off,req.size()-off):send(conn.net.fd,req.data()+off,req.size()-off,MSG_NOSIGNAL);if(n>0)off+=n;else if(n==MBEDTLS_ERR_SSL_WANT_READ)wait(POLLIN);else if(n==MBEDTLS_ERR_SSL_WANT_WRITE||(!tls&&n<0&&(errno==EAGAIN||errno==EINTR)))wait(POLLOUT);else throw std::runtime_error("请求发送失败");}
        std::string out;unsigned char bytes[4096];while(true){int n=tls?mbedtls_ssl_read(&conn.ssl,bytes,sizeof(bytes)):recv(conn.net.fd,bytes,sizeof(bytes),0);if(n>0){out.append((char*)bytes,n);if(out.size()>1024*1024)throw std::runtime_error("Sunshine 响应过大");}else if(!n||n==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)break;else if(n==MBEDTLS_ERR_SSL_WANT_READ||(!tls&&n<0&&(errno==EAGAIN||errno==EINTR)))wait(POLLIN);else if(n==MBEDTLS_ERR_SSL_WANT_WRITE)wait(POLLOUT);else throw std::runtime_error("读取 Sunshine 失败");}
        auto end=out.find("\r\n\r\n");if(end==std::string::npos||out.size()<12||out.substr(9,3)!="200")throw std::runtime_error("Sunshine HTTP 请求失败");
        // HTTP/1.0 asks for close-delimited responses; reject unexpected transfer
        // encodings instead of attempting to parse an ambiguous XML document.
        auto header=out.substr(0,end);std::transform(header.begin(),header.end(),header.begin(),[](unsigned char c){return std::tolower(c);});
        if(header.find("transfer-encoding:")!=std::string::npos)throw std::runtime_error("不支持的 Sunshine HTTP 传输编码");return out.substr(end+4);
    }
    Server info(){
        Xml initial(request("/serverinfo",false));https_port=initial.number("HttpsPort",host.port-5);if(https_port<1||https_port>65535)throw std::runtime_error("无效的 HTTPS 端口");
        std::unique_ptr<Xml> secure;if(!pin_pem.empty())secure=std::make_unique<Xml>(request("/serverinfo",true));auto&x=secure?*secure:initial;
        Server s;s.name=x.get("hostname");s.version=x.get("appversion");s.gfe=x.get("GfeVersion");s.https_port=https_port;s.paired=secure&&x.get("PairStatus")=="1";
        s.current=x.get("state").find("_SERVER_BUSY")!=std::string::npos?x.number("currentgame"):0;s.codecs=x.number("ServerCodecModeSupport",SCM_H264);
        if(s.version.empty()||std::atoi(s.version.c_str())<7)throw std::runtime_error("只支持现代 Sunshine / GameStream 协议");return s;
    }
};
GameStream::GameStream(Host h,std::string d,std::atomic<bool>&c):p_(std::make_unique<Impl>(h,d,c)){p_->init();}
GameStream::~GameStream()=default;
Server GameStream::inspect(){return p_->info();}
Server GameStream::pair(const std::string&pin){
    if(pin.size()!=4||pin.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("PIN 必须为四位数字");
    auto&s=*p_;auto info=s.info();if(info.paired)return info;
    auto salt=s.random(16),salt_pin=salt;append(salt_pin,(const unsigned char*)pin.data(),4);auto aes_key=hash(salt_pin);
    // Each response is authenticated by the complete PIN challenge before its
    // certificate can become a persistent trust anchor.
    Xml first(s.request("/pair?devicename=MoonPilot&updateState=1&phrase=getservercert&salt="+hex(salt)+"&clientcert="+hex((const unsigned char*)s.client_pem.data(),s.client_pem.size()),false,90));
    if(first.get("paired")!="1")throw std::runtime_error("配对未获主机确认");auto certificate=unhex(first.get("plaincert"));std::string pem(certificate.begin(),certificate.end());Cert peer;peer.parse(pem);
    auto challenge=s.random(16),secret=s.random(16);
    Xml second(s.request("/pair?devicename=MoonPilot&updateState=1&clientchallenge="+hex(aes(challenge,aes_key,true)),false));
    if(second.get("paired")!="1")throw std::runtime_error("PIN 挑战失败");auto response=aes(unhex(second.get("challengeresponse")),aes_key,false);if(response.size()!=48&&response.size()!=64)throw std::runtime_error("主机挑战长度无效");
    Bytes proof(response.begin()+32,response.begin()+48);append(proof,s.client.value.sig.p,s.client.value.sig.len);append(proof,secret.data(),secret.size());
    Xml third(s.request("/pair?devicename=MoonPilot&updateState=1&serverchallengeresp="+hex(aes(hash(proof),aes_key,true)),false));
    if(third.get("paired")!="1")throw std::runtime_error("PIN 不匹配");auto server_secret=unhex(third.get("pairingsecret"));if(server_secret.size()<=16)throw std::runtime_error("主机签名缺失");
    auto h=hash(Bytes(server_secret.begin(),server_secret.begin()+16));check(mbedtls_pk_verify(&peer.value.pk,MBEDTLS_MD_SHA256,h.data(),h.size(),server_secret.data()+16,server_secret.size()-16),"主机配对签名不正确");
    proof=challenge;append(proof,peer.value.sig.p,peer.value.sig.len);append(proof,server_secret.data(),16);auto expected=hash(proof);
    if(!std::equal(expected.begin(),expected.end(),response.begin()))throw std::runtime_error("主机 PIN 校验失败");
    auto digest=hash(secret);Bytes signature(512);size_t siglen=0;check(mbedtls_pk_sign(&s.key,MBEDTLS_MD_SHA256,digest.data(),digest.size(),signature.data(),&siglen,mbedtls_ctr_drbg_random,&s.rng),"客户端签名失败");append(secret,signature.data(),siglen);
    Xml fourth(s.request("/pair?devicename=MoonPilot&updateState=1&clientpairingsecret="+hex(secret),false));if(fourth.get("paired")!="1")throw std::runtime_error("主机拒绝客户端签名");
    s.pin_pem=pem;Xml last(s.request("/pair?devicename=MoonPilot&updateState=1&phrase=pairchallenge",true));if(last.get("paired")!="1")throw std::runtime_error("TLS 配对确认失败");c1::save_private(s.dir+"/server.pem",pem);return s.info();
}
std::vector<Application> GameStream::applications(){
    if(!p_->info().paired)throw std::runtime_error("请先完成主机配对");Xml x(p_->request("/applist",true));std::vector<Application> out;
    for(auto*a=x.doc.FirstChildElement("root")->FirstChildElement("App");a;a=a->NextSiblingElement("App")){auto*i=a->FirstChildElement("ID"),*n=a->FirstChildElement("AppTitle");int id=0;if(i&&n&&i->QueryIntText(&id)==tinyxml2::XML_SUCCESS&&id>0&&n->GetText())out.push_back({id,n->GetText()});if(out.size()>256)throw std::runtime_error("主机应用数量超限");}return out;
}
Server GameStream::launch(int app,STREAM_CONFIGURATION&cfg){
    auto&s=*p_;auto info=s.info();if(!info.paired)throw std::runtime_error("请先完成配对");if(app<1)throw std::runtime_error("请选择要串流的桌面或应用");if(info.current&&info.current!=app)throw std::runtime_error("主机已有其他串流，请先在原客户端结束");
    LiInitializeStreamConfiguration(&cfg);cfg.width=s.host.width;cfg.height=s.host.height;cfg.fps=s.host.fps;cfg.bitrate=s.host.bitrate;cfg.packetSize=1024;cfg.streamingRemotely=STREAM_CFG_AUTO;cfg.audioConfiguration=AUDIO_CONFIGURATION_STEREO;cfg.supportedVideoFormats=VIDEO_FORMAT_H264;cfg.encryptionFlags=ENCFLG_ALL;
    auto key=s.random(16),iv=s.random(4);memcpy(cfg.remoteInputAesKey,key.data(),16);memcpy(cfg.remoteInputAesIv,iv.data(),4);uint32_t id=uint32_t(iv[0])<<24|uint32_t(iv[1])<<16|uint32_t(iv[2])<<8|iv[3];
    auto url=std::string(info.current?"/resume":"/launch")+"?appid="+std::to_string(app)+"&mode="+std::to_string(cfg.width)+"x"+std::to_string(cfg.height)+"x"+std::to_string(cfg.fps)+"&additionalStates=1&sops=0&rikey="+hex(key)+"&rikeyid="+std::to_string(id)+"&localAudioPlayMode=1&surroundAudioInfo="+std::to_string(SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(cfg.audioConfiguration))+"&remoteControllersBitmap=0&gcmap=0"+LiGetLaunchUrlQueryParameters();
    Xml x(s.request(url,true));if(x.get(info.current?"resume":"gamesession").empty()||x.get(info.current?"resume":"gamesession")=="0")throw std::runtime_error("Sunshine 未启动串流");info.session=x.get("sessionUrl0");return info;
}
}
