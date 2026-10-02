// SPDX-License-Identifier: GPL-3.0-only
#include "engine.hpp"
#include "net.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace chat {
namespace {
constexpr size_t max_friends=32, max_history=60, max_message=1372;
std::string display_name(Tox*t,uint32_t n){
    std::string s(tox_friend_get_name_size(t,n,nullptr),'\0');
    if(!s.empty())tox_friend_get_name(t,n,reinterpret_cast<uint8_t*>(s.data()),nullptr);
    return clean_text(s,64);
}
}
std::string hex(const uint8_t*d,size_t n){const char*h="0123456789ABCDEF";std::string s;for(size_t i=0;i<n;i++){s+=h[d[i]>>4];s+=h[d[i]&15];}return s;}
bool unhex(const std::string&s,uint8_t*out,size_t n){
    if(s.size()!=n*2)return false;
    auto digit=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;};
    for(size_t i=0;i<n;i++){int a=digit(s[i*2]),b=digit(s[i*2+1]);if(a<0||b<0)return false;out[i]=uint8_t(a*16+b);}return true;
}
std::string clean_text(const std::string&s,size_t cap){
    std::string out;
    for(size_t i=0;i<s.size()&&out.size()<cap;){
        unsigned char c=s[i];size_t n=c<0x80?1:c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
        bool ok=n&&i+n<=s.size();uint32_t cp=n?c&((1u<<(7-n))-1):0;
        if(n==1)cp=c;
        for(size_t k=1;ok&&k<n;k++){unsigned char v=s[i+k];if((v&0xc0)!=0x80)ok=false;else cp=(cp<<6)|(v&63);}
        if(ok&&((n==2&&cp<0x80)||(n==3&&cp<0x800)||(n==4&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)))ok=false;
        if(!ok){out+='?';i++;continue;}
        if(cp>=32&&cp!=127&&!(cp>=0x80&&cp<=0x9f)&&!(cp>=0x202a&&cp<=0x202e)&&!(cp>=0x2066&&cp<=0x2069)){
            if(out.size()+n>cap)break;out.append(s,i,n);
        }else if(cp=='\n'||cp=='\t'){out+=' ';}
        i+=n;
    }return out;
}
Engine::Engine(std::string directory,std::string nodes):directory_(std::move(directory)),nodes_(std::move(nodes)){worker_=std::thread(&Engine::run,this);}
Engine::~Engine(){stop_=true;if(worker_.joinable())worker_.join();}
bool Engine::submit(Command c){std::lock_guard<std::mutex> lock(mutex_);if(!state_.ready||commands_.size()>=16||c.text.size()>4096||c.extra.size()>256)return false;commands_.push_back(std::move(c));return true;}
Snapshot Engine::snapshot(uint32_t n){std::lock_guard<std::mutex> lock(mutex_);auto s=state_;auto it=history_.find(n);if(it!=history_.end())s.messages=it->second;for(auto&[id,t]:transfers_)if(n==UINT32_MAX||t.view.number==n)s.transfers.push_back(t.view);return s;}
void Engine::error(const std::string&e){state_.error=clean_text(e,180);state_.error_count++;state_.revision++;}
void Engine::publish(){
    auto connection=tox_self_get_connection_status(tox_);bool online=connection!=TOX_CONNECTION_NONE;
    std::string status=connection==TOX_CONNECTION_UDP?"网络已连接 · UDP":connection==TOX_CONNECTION_TCP?"网络已连接 · TCP":"正在连接网络…";
    if(online!=state_.online||status!=state_.status){state_.online=online;state_.status=status;state_.revision++;}
    for(auto&f:state_.friends){bool on=tox_friend_get_connection_status(tox_,f.number,nullptr)!=TOX_CONNECTION_NONE;std::string name=display_name(tox_,f.number);if(name.empty())name=f.key.substr(0,12);if(on!=f.online||name!=f.name){f.online=on;f.name=name;state_.revision++;}}
}
void Engine::refresh_friends(){
    std::vector<uint32_t> numbers(tox_self_get_friend_list_size(tox_));tox_self_get_friend_list(tox_,numbers.data());
    std::vector<Friend> next;
    for(auto n:numbers){uint8_t key[TOX_PUBLIC_KEY_SIZE];if(!tox_friend_get_public_key(tox_,n,key,nullptr))continue;
        Friend f;f.number=n;f.key=hex(key,sizeof key);f.name=display_name(tox_,n);if(f.name.empty())f.name=f.key.substr(0,12);
        for(auto&old:state_.friends)if(old.key==f.key)f.unread=old.unread;
        if(!history_.count(n)){
            std::vector<Message> list;
            try{auto j=Json::parse(c1::read_file(directory_+"/chat-"+f.key+".json",256*1024));if(!j.is_array()||j.size()>max_history)throw std::runtime_error("Invalid chat history");for(auto&m:j){Message v;v.text=clean_text(m.at("text").get<std::string>(),max_message);v.mine=m.at("mine").get<bool>();v.state=m.value("state","");v.file=m.value("file",std::string());v.kind=m.value("kind",std::string());v.size=m.value("size",uint64_t(0));if(v.file.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")!=std::string::npos||v.file.find("..")!=std::string::npos){v.file.clear();v.kind.clear();}if(v.state!="delivered"&&v.state!="received"&&v.state!="complete"&&v.state!="cancelled"&&v.state!="failed")v.state="unconfirmed";list.push_back(std::move(v));}}catch(...){}
            history_[n]=std::move(list);
        }next.push_back(std::move(f));
    }state_.friends=std::move(next);state_.revision++;
}
void Engine::save(){
    size_t n=tox_get_savedata_size(tox_);if(n<84||n>4*1024*1024)throw std::runtime_error("Invalid identity snapshot size");
    std::string data(n,'\0');tox_get_savedata(tox_,reinterpret_cast<uint8_t*>(data.data()));
    const unsigned char header[]={0,0,0,0,0x1f,0x1b,0xed,0x15};
    if(std::memcmp(data.data(),header,sizeof header))throw std::runtime_error("Invalid identity snapshot; previous file preserved");
    // Never overwrite the only good copy. Startup has already validated
    // last_saved_ through tox_new; a corrupt disk file is not rotated into it.
    c1::save_private(directory_+"/profile.tox.bak",last_saved_.empty()?data:last_saved_);
    c1::save_private(directory_+"/profile.tox",data);last_saved_=std::move(data);dirty_=false;
}
void Engine::save_requests(){Json j=Json::array();for(auto&r:state_.requests)j.push_back({{"key",r.key},{"message",r.message}});c1::save_private(directory_+"/requests.json",j.dump());}
void Engine::save_history(uint32_t n){
    auto f=std::find_if(state_.friends.begin(),state_.friends.end(),[n](const Friend&f){return f.number==n;});if(f==state_.friends.end())return;
    auto&list=history_[n];if(list.size()>max_history)list.erase(list.begin(),list.end()-max_history);
    Json j=Json::array();for(auto&m:list)j.push_back({{"text",m.text},{"mine",m.mine},{"state",m.state},{"file",m.file},{"kind",m.kind},{"size",m.size}});
    c1::save_private(directory_+"/chat-"+f->key+".json",j.dump());
}
void Engine::receive_request(const uint8_t*k,const uint8_t*m,size_t n){
    std::string key=hex(k,TOX_PUBLIC_KEY_SIZE);
    for(auto&f:state_.friends)if(f.key==key)return;
    for(auto&r:state_.requests)if(r.key==key)return;
    if(state_.requests.size()>=16)return;
    state_.requests.push_back({key,clean_text(std::string(reinterpret_cast<const char*>(m),n),256)});state_.revision++;save_requests();
}
void Engine::receive_message(uint32_t n,const uint8_t*m,size_t len){
    auto f=std::find_if(state_.friends.begin(),state_.friends.end(),[n](const Friend&f){return f.number==n;});if(f==state_.friends.end())return;
    history_[n].push_back({clean_text(std::string(reinterpret_cast<const char*>(m),len),max_message),"received",false,0});f->unread=std::min(999u,f->unread+1);state_.revision++;save_history(n);
}
void Engine::receipt(uint32_t n,uint32_t receipt){for(auto&m:history_[n])if(m.mine&&m.state=="sent"&&m.receipt==receipt){m.state="delivered";state_.revision++;save_history(n);break;}}
// Never unwind a C++ exception through toxcore's C stack.
void Engine::on_request(Tox*,const uint8_t*k,const uint8_t*m,size_t n,void*p){auto&e=*static_cast<Engine*>(p);try{e.receive_request(k,m,n);}catch(const std::exception&x){e.error(x.what());}}
void Engine::on_message(Tox*,uint32_t n,Tox_Message_Type,const uint8_t*m,size_t l,void*p){auto&e=*static_cast<Engine*>(p);try{e.receive_message(n,m,l);}catch(const std::exception&x){e.error(x.what());}}
void Engine::on_receipt(Tox*,uint32_t n,uint32_t r,void*p){auto&e=*static_cast<Engine*>(p);try{e.receipt(n,r);}catch(const std::exception&x){e.error(x.what());}}
void Engine::execute(const Command&c){
    state_.error.clear();state_.revision++;
    if(c.action==Action::Background){c1::save_private(directory_+"/preferences.json",Json{{"background",c.text=="1"}}.dump());state_.background=c.text=="1";return;}
    if(c.action==Action::SendFile||c.action==Action::AcceptFile||c.action==Action::CancelFile){execute_file(c);return;}
    if(c.action==Action::Bootstrap){uint8_t key[TOX_PUBLIC_KEY_SIZE];if(!unhex(c.extra,key,sizeof key)||c.text.empty()||!c.port)throw std::runtime_error("Invalid bootstrap node");if(!tox_bootstrap(tox_,c.text.c_str(),c.port,key,nullptr))throw std::runtime_error("Bootstrap failed");return;}
    if(c.action==Action::Rename){auto name=clean_text(c.text,64);if(name.empty())throw std::runtime_error("Name cannot be empty");if(!tox_self_set_name(tox_,reinterpret_cast<const uint8_t*>(name.data()),name.size(),nullptr))throw std::runtime_error("Cannot set name");state_.name=name;dirty_=true;save();return;}
    if(c.action==Action::Add||c.action==Action::Accept){
        if(state_.friends.size()>=max_friends)throw std::runtime_error("Friend limit is 32");
        Tox_Err_Friend_Add err;uint32_t n;
        if(c.action==Action::Add){uint8_t address[TOX_ADDRESS_SIZE];if(!unhex(c.text,address,sizeof address))throw std::runtime_error("Tox ID must contain 76 hexadecimal characters");const char*hello="Hello from C1 Max";n=tox_friend_add(tox_,address,reinterpret_cast<const uint8_t*>(hello),strlen(hello),&err);}
        else{uint8_t key[TOX_PUBLIC_KEY_SIZE];if(!unhex(c.text,key,sizeof key)||std::none_of(state_.requests.begin(),state_.requests.end(),[&](const Request&r){return r.key==c.text;}))throw std::runtime_error("Request no longer exists");n=tox_friend_add_norequest(tox_,key,&err);}
        if(err!=TOX_ERR_FRIEND_ADD_OK)throw std::runtime_error("Cannot add friend (check ID / duplicate / own ID), code "+std::to_string(err));
        try{save();}catch(...){tox_friend_delete(tox_,n,nullptr);throw;}
        refresh_friends();
    }
    if(c.action==Action::Accept||c.action==Action::Reject){auto&v=state_.requests;v.erase(std::remove_if(v.begin(),v.end(),[&](const Request&r){return r.key==c.text;}),v.end());save_requests();return;}
    if(c.action==Action::Send){
        if(tox_friend_get_connection_status(tox_,c.number,nullptr)==TOX_CONNECTION_NONE)throw std::runtime_error("好友离线，文字仍保留在输入框");
        if(c.text.empty()||c.text.size()>1024||clean_text(c.text,1024)!=c.text)throw std::runtime_error("Invalid message (maximum 1024 UTF-8 bytes)");
        Tox_Err_Friend_Send_Message err;auto receipt=tox_friend_send_message(tox_,c.number,TOX_MESSAGE_TYPE_NORMAL,reinterpret_cast<const uint8_t*>(c.text.data()),c.text.size(),&err);
        if(err!=TOX_ERR_FRIEND_SEND_MESSAGE_OK)throw std::runtime_error("Message was not sent, code "+std::to_string(err));
        history_[c.number].push_back({c.text,"sent",true,receipt});
        try{save_history(c.number);}catch(const std::exception&){error("消息已发出，但本机记录保存失败");}
        return;
    }
    if(c.action==Action::Read){for(auto&f:state_.friends)if(f.number==c.number)f.unread=0;return;}
    if(c.action==Action::Delete){
        std::vector<uint64_t> cancel;for(auto&[id,t]:transfers_)if(t.view.number==c.number)cancel.push_back(id);for(auto id:cancel)finish_file(id,"cancelled");
        auto old=state_.friends;
        if(!tox_friend_delete(tox_,c.number,nullptr))throw std::runtime_error("Friend no longer exists");
        save();history_.erase(c.number);for(auto&f:old)if(f.number==c.number)unlink((directory_+"/chat-"+f.key+".json").c_str());refresh_friends();
    }
}
void Engine::bootstrap(){
    if(nodes_.empty())return;
    auto j=Json::parse(c1::read_file(nodes_,32768));auto&nodes=j.at("nodes");
    if(!nodes.is_array()||nodes.size()>16)throw std::runtime_error("Invalid node list");
    for(auto&n:nodes){uint8_t k[TOX_PUBLIC_KEY_SIZE];auto key=n.at("public_key").get<std::string>();auto ip=n.at("ipv4").get<std::string>();int port=n.at("port").get<int>();
        // Numeric IPv4 entries avoid synchronous DNS stalls in tox_bootstrap.
        if(ip.size()>15||ip.find_first_not_of("0123456789.")!=std::string::npos||port<1||port>65535||!unhex(key,k,sizeof k))throw std::runtime_error("Invalid bootstrap entry");
        tox_bootstrap(tox_,ip.c_str(),uint16_t(port),k,nullptr);
        auto tcp=n.value("tcp_ports",std::vector<int>{});if(tcp.size()>4)throw std::runtime_error("Invalid relay ports");for(int p:tcp)if(p>0&&p<=65535)tox_add_tcp_relay(tox_,ip.c_str(),uint16_t(p),k,nullptr);
    }
}
void Engine::run(){
    int lockfd=-1;
    try{
        if(mkdir(directory_.c_str(),0700)&&errno!=EEXIST)throw std::runtime_error("Cannot create Tox data directory");
        chmod(directory_.c_str(),0700);
        if(mkdir((directory_+"/media").c_str(),0700)&&errno!=EEXIST)throw std::runtime_error("Cannot create media folder");
        try{state_.background=Json::parse(c1::read_file(directory_+"/preferences.json",4096)).value("background",false);}catch(...){}
        lockfd=open((directory_+"/session.lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
        if(lockfd<0||flock(lockfd,LOCK_EX|LOCK_NB))throw std::runtime_error("This Tox identity is already open");
        Tox_Options*options=tox_options_new(nullptr);if(!options)throw std::runtime_error("Cannot allocate Tox options");
        if(getenv("C1_TOX_DEBUG"))tox_options_set_log_callback(options,[](Tox*,Tox_Log_Level,const char*file,uint32_t line,const char*,const char*message,void*){fprintf(stderr,"[tox] %s:%u %s\n",file,line,message);});
        tox_options_set_ipv6_enabled(options,false);tox_options_set_local_discovery_enabled(options,!nodes_.empty());tox_options_set_hole_punching_enabled(options,true);
        std::string saved;auto path=directory_+"/profile.tox";struct stat st{};
        try{if(stat(path.c_str(),&st)==0){saved=c1::read_file(path,4*1024*1024);if(saved.empty())throw std::runtime_error("Empty identity file; restore your profile backup");tox_options_set_savedata_type(options,TOX_SAVEDATA_TYPE_TOX_SAVE);tox_options_set_savedata_data(options,reinterpret_cast<const uint8_t*>(saved.data()),saved.size());}else if(errno!=ENOENT)throw std::runtime_error("Cannot access identity file");}catch(...){tox_options_free(options);throw;}
        Tox_Err_New err;tox_=tox_new(options,&err);tox_options_free(options);
        if(!tox_||err!=TOX_ERR_NEW_OK)throw std::runtime_error("Cannot load Tox identity; existing file was preserved (code "+std::to_string(err)+")");
        if(tox_self_get_friend_list_size(tox_)>max_friends)throw std::runtime_error("Profile exceeds the 32-friend limit");
        last_saved_=saved;
        if(saved.empty()){const char*name="C1 Max";tox_self_set_name(tox_,reinterpret_cast<const uint8_t*>(name),strlen(name),nullptr);save();}
        {
            std::lock_guard<std::mutex> guard(mutex_);
            uint8_t address[TOX_ADDRESS_SIZE],key[TOX_PUBLIC_KEY_SIZE];tox_self_get_address(tox_,address);tox_self_get_dht_id(tox_,key);state_.id=hex(address,sizeof address);state_.dht_key=hex(key,sizeof key);state_.udp_port=tox_self_get_udp_port(tox_,nullptr);
            state_.name.resize(tox_self_get_name_size(tox_));tox_self_get_name(tox_,reinterpret_cast<uint8_t*>(state_.name.data()));
            refresh_friends();
            try{auto j=Json::parse(c1::read_file(directory_+"/requests.json",32768));if(j.is_array()&&j.size()<=16)for(auto&r:j){uint8_t k[TOX_PUBLIC_KEY_SIZE];std::string key=r.at("key");if(unhex(key,k,sizeof k))state_.requests.push_back({hex(k,sizeof k),clean_text(r.at("message"),256)});}}catch(...){}
            tox_callback_friend_request(tox_,on_request);tox_callback_friend_message(tox_,on_message);tox_callback_friend_read_receipt(tox_,on_receipt);
            tox_callback_file_recv(tox_,on_file_offer);tox_callback_file_recv_chunk(tox_,on_file_chunk);tox_callback_file_chunk_request(tox_,on_file_request);tox_callback_file_recv_control(tox_,on_file_control);
            state_.ready=true;state_.revision++;
            c1::save_private(directory_+"/my-id.txt",state_.id+"\n");
            try{bootstrap();}catch(const std::exception&e){error(e.what());}
        }
        auto last_boot=std::chrono::steady_clock::now(),last_save=last_boot;
        while(!stop_){
            {std::lock_guard<std::mutex> guard(mutex_);
                // Core and callbacks are confined to this worker. Snapshots never
                // call toxcore and no LVGL object is touched from this thread.
                while(!commands_.empty()){auto c=std::move(commands_.front());commands_.pop_front();bool ok=true;try{execute(c);}catch(const std::exception&e){error(e.what());ok=false;}if(c.token){state_.completed=c.token;state_.command_ok=ok;}}
                tox_iterate(tox_,this);publish();
                std::vector<uint64_t> offline;for(auto&[id,t]:transfers_)if(tox_friend_get_connection_status(tox_,t.view.number,nullptr)==TOX_CONNECTION_NONE)offline.push_back(id);for(auto id:offline)finish_file(id,"failed");auto now=std::chrono::steady_clock::now();
                if(!state_.online&&now-last_boot>std::chrono::seconds(30)){try{bootstrap();}catch(const std::exception&e){error(e.what());}last_boot=now;}
                if(now-last_save>std::chrono::seconds(60)){try{save();}catch(const std::exception&e){error(e.what());}last_save=now;}
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(50u,tox_iteration_interval(tox_))));
        }
        {std::lock_guard<std::mutex> guard(mutex_);save();}
    }catch(const std::exception&e){std::lock_guard<std::mutex> guard(mutex_);state_.ready=false;state_.status="无法启动";error(e.what());std::fprintf(stderr,"[tox startup] %s\n",e.what());}
    while(!transfers_.empty()){try{finish_file(transfers_.begin()->first,"cancelled");}catch(...){}}
    if(tox_){tox_kill(tox_);tox_=nullptr;}if(lockfd>=0)close(lockfd);
}
}
