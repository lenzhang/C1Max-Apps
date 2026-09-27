#include "remote.hpp"
#include "net.hpp"
#include "y4m.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
namespace moonpilot {
static uint32_t tick(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int virtual_key(uint8_t usage){
    if(usage>=4&&usage<=29)return 'A'+usage-4;
    if(usage>=30&&usage<=38)return '1'+usage-30;
    if(usage==39)return '0';
    if(usage>=58&&usage<=69)return 0x70+usage-58;
    switch(usage){case 40:return 0x0d;case 41:return 0x1b;case 42:return 8;case 43:return 9;case 44:return 32;
    case 45:return 0xbd;case 46:return 0xbb;case 47:return 0xdb;case 48:return 0xdd;case 49:return 0xdc;
    case 51:return 0xba;case 52:return 0xde;case 53:return 0xc0;case 54:return 0xbc;case 55:return 0xbe;case 56:return 0xbf;
    case 73:return 0x2d;case 74:return 0x24;case 75:return 0x21;case 76:return 0x2e;case 77:return 0x23;case 78:return 0x22;case 79:return 0x27;case 80:return 0x25;case 81:return 0x28;case 82:return 0x26;default:return 0;}
}
struct Remote::Impl {
    static Impl*current;
    mutable std::mutex lock;
    std::string state="尚未连接",failure,dir,fifo;
    std::atomic<bool> cancelled{false},connected{false},working{false};
    bool started=false,need_idr=true;bool held[256]{};std::deque<std::vector<uint8_t>> queue;size_t queued=0;
    pid_t decoder=-1;int input=-1,video=-1;std::thread writer;
    Frame frame;Y4mReader reader{800,450};uint32_t began=0;
    void set(const std::string&s){std::lock_guard<std::mutex>g(lock);state=s;}
    void fail(const std::string&s){std::lock_guard<std::mutex>g(lock);failure=s;connected=false;}
    static void log(const char*,...){/* upstream messages can contain session details */}
    static void stage(int n){if(current){if(current->cancelled)LiInterruptConnection();current->set(std::string("连接中 · ")+LiGetStageName(n));}}
    static void stage_failed(int,int error){if(current)current->fail("串流连接失败（"+std::to_string(error)+"）");}
    static void connected_cb(){if(current){current->connected=true;current->set("已连接 · 等待桌面画面");}}
    static void terminated(int error){if(current)current->fail("串流已断开（"+std::to_string(error)+"）");}
    static int setup(int format,int w,int h,int,void*,int){return format==VIDEO_FORMAT_H264&&w<=800&&h<=450?0:-1;}
    static int audio_setup(int,const POPUS_MULTISTREAM_CONFIGURATION,void*,int){return 0;}
    static void audio(char*,int){/* first version retains audio playback on host */}
    static int submit(PDECODE_UNIT du){
        auto*p=current;if(!p||p->cancelled||du->fullLength<1||du->fullLength>512*1024)return DR_NEED_IDR;
        std::lock_guard<std::mutex>g(p->lock);
        if(p->need_idr&&du->frameType!=FRAME_TYPE_IDR)return DR_NEED_IDR;
        if(p->queued+du->fullLength>512*1024||p->queue.size()>=4){p->queue.clear();p->queued=0;p->need_idr=true;return DR_NEED_IDR;}
        std::vector<uint8_t>b;b.reserve(du->fullLength);
        for(auto*entry=du->bufferList;entry;entry=entry->next){if(entry->length<0||b.size()+entry->length>size_t(du->fullLength))return DR_NEED_IDR;b.insert(b.end(),entry->data,entry->data+entry->length);}
        if(b.size()!=size_t(du->fullLength))return DR_NEED_IDR;p->need_idr=false;p->queued+=b.size();p->queue.push_back(std::move(b));return DR_OK;
    }
    void decode(const Host&h,const std::string&root){
        fifo=dir+"/frames-"+std::to_string(getpid())+".y4m";if(mkfifo(fifo.c_str(),0600))throw std::runtime_error("无法创建桌面帧管道");
        video=open(fifo.c_str(),O_RDWR|O_NONBLOCK|O_CLOEXEC);if(video<0)throw std::runtime_error("无法打开桌面帧管道");
        int fds[2];if(pipe2(fds,O_CLOEXEC))throw std::runtime_error("无法建立解码输入");
        int logfd=open((dir+"/decoder.log").c_str(),O_CREAT|O_TRUNC|O_WRONLY|O_CLOEXEC,0600);if(logfd<0){close(fds[0]);close(fds[1]);throw std::runtime_error("无法创建解码日志");}
        std::string rate=std::to_string(h.fps),preload=root+"/streamplayer/c1max-yuv-pipe.so";pid_t parent=getpid();
        decoder=fork();if(decoder==0){setpgid(0,0);prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(1);
            setenv("LD_PRELOAD",preload.c_str(),1);setenv("C1_YUV_FIFO",fifo.c_str(),1);setenv("C1_YUV_DESKTOP","1",1);unsetenv("C1_YUV_SCALE");
            dup2(fds[0],0);dup2(logfd,1);dup2(logfd,2);close(fds[0]);close(fds[1]);close(logfd);
            execl("/usr/bin/mplayer","mplayer","-noconfig","all","-quiet","-noconsolecontrols","-nolirc","-nojoystick","-nomouseinput","-nosound","-nosub","-noautosub","-vo","null","-nocache","-benchmark","-demuxer","h264es","-fps",rate.c_str(),"-",nullptr);_exit(127);}
        close(fds[0]);close(logfd);input=fds[1];if(decoder<0)throw std::runtime_error("无法启动桌面解码器");fcntl(input,F_SETFL,O_NONBLOCK);
        writer=std::thread([this]{while(!cancelled){std::vector<uint8_t> bytes;{std::lock_guard<std::mutex>g(lock);if(!queue.empty()){bytes=std::move(queue.front());queue.pop_front();queued-=bytes.size();}}
            if(bytes.empty()){usleep(4000);continue;}size_t off=0;auto began=tick();
            while(off<bytes.size()&&!cancelled){ssize_t n=write(input,bytes.data()+off,bytes.size()-off);if(n>0)off+=n;else if(n<0&&(errno==EINTR||errno==EAGAIN)){pollfd fd{input,POLLOUT,0};::poll(&fd,1,50);}else{fail("桌面解码器已退出");cancelled=true;}
                if(tick()-began>2500){fail("解码跟不上串流，请降低画质");cancelled=true;}}
        }});
    }
};
Remote::Impl*Remote::Impl::current=nullptr;
Remote::Remote():p_(std::make_unique<Impl>()){}
Remote::~Remote(){stop();}
void Remote::start(const Host&h,int app,const std::string&data,const std::string&root,std::atomic<bool>&request_cancel){
    stop();auto&p=*p_;p.cancelled=false;p.working=true;p.dir=data;p.frame={};p.reader=Y4mReader(800,450);p.failure.clear();p.began=tick();
    try{if(request_cancel)throw std::runtime_error("已取消连接");GameStream gs(h,data,request_cancel);STREAM_CONFIGURATION cfg{};p.set("启动 Sunshine 桌面…");auto host=gs.launch(app,cfg);if(p.cancelled||request_cancel)throw std::runtime_error("已取消连接");
        p.decode(h,root);Impl::current=&p;SERVER_INFORMATION info{};LiInitializeServerInformation(&info);info.address=h.address.c_str();info.serverInfoAppVersion=host.version.c_str();info.serverInfoGfeVersion=host.gfe.c_str();info.rtspSessionUrl=host.session.empty()?nullptr:host.session.c_str();info.serverCodecModeSupport=host.codecs;
        DECODER_RENDERER_CALLBACKS video{};video.setup=Impl::setup;video.submitDecodeUnit=Impl::submit;video.capabilities=CAPABILITY_DIRECT_SUBMIT;
        AUDIO_RENDERER_CALLBACKS audio{};audio.init=Impl::audio_setup;audio.decodeAndPlaySample=Impl::audio;
        CONNECTION_LISTENER_CALLBACKS events{};events.stageStarting=Impl::stage;events.stageFailed=Impl::stage_failed;events.connectionStarted=Impl::connected_cb;events.connectionTerminated=Impl::terminated;events.logMessage=Impl::log;
        // common-c serializes each session. Stop is called only after this
        // function returns; cancel uses its documented asynchronous interrupt.
        int rc=LiStartConnection(&info,&cfg,&events,&video,&audio,nullptr,0,nullptr,0);if(rc)throw std::runtime_error("Moonlight 连接失败（"+std::to_string(rc)+"）");p.started=true;p.began=tick();if(p.cancelled||request_cancel)throw std::runtime_error("连接已取消");
    }catch(...){stop();throw;}
}
void Remote::cancel(){p_->cancelled=true;if(p_->working)LiInterruptConnection();}
void Remote::stop(){
    if(!p_)return;auto&p=*p_;p.cancelled=true;if(p.started){release();LiStopConnection();p.started=false;}p.connected=false;p.working=false;if(Impl::current==&p)Impl::current=nullptr;
    if(p.writer.joinable())p.writer.join();if(p.input>=0){close(p.input);p.input=-1;}if(p.decoder>0){kill(p.decoder,SIGTERM);bool done=false;for(int i=0;i<25;i++){if(waitpid(p.decoder,nullptr,WNOHANG)==p.decoder){done=true;break;}usleep(10000);}if(!done){kill(p.decoder,SIGKILL);while(waitpid(p.decoder,nullptr,0)<0&&errno==EINTR){}}p.decoder=-1;}
    if(p.video>=0){close(p.video);p.video=-1;}if(!p.fifo.empty()){unlink(p.fifo.c_str());p.fifo.clear();}std::lock_guard<std::mutex>g(p.lock);p.queue.clear();p.queued=0;p.need_idr=true;p.state="未连接";p.frame={};
}
void Remote::poll(){
    auto&p=*p_;if(!p.working||p.video<0)return;uint8_t bytes[8192];size_t budget=1600000;ssize_t n;
    while(budget&&(n=read(p.video,bytes,std::min(sizeof(bytes),budget)))>0){budget-=n;if(!p.reader.feed(bytes,n,[&](const uint32_t*rgb,int w,int h,int,int){p.frame.rgb.assign(rgb,rgb+size_t(w)*h);p.frame.width=w;p.frame.height=h;p.frame.time=tick();p.frame.sequence++;})){p.fail(p.reader.error());break;}}
    int status;if(p.decoder>0&&waitpid(p.decoder,&status,WNOHANG)==p.decoder){p.decoder=-1;p.fail("桌面解码器异常退出");}
    if(p.connected&&tick()-(p.frame.sequence?p.frame.time:p.began)>(p.frame.sequence?8000u:25000u))p.fail("没有收到新桌面画面，请重连或降低画质");
}
bool Remote::ready()const{return p_->connected&&!p_->cancelled;}
bool Remote::active()const{return p_->working;}
std::string Remote::status()const{std::lock_guard<std::mutex>g(p_->lock);return p_->state;}
std::string Remote::error()const{std::lock_guard<std::mutex>g(p_->lock);return p_->failure;}
const Frame&Remote::frame()const{return p_->frame;}
static void sent(int rc){if(rc)throw std::runtime_error("远程输入发送失败");}
void Remote::move(double x,double y){if(!ready()||!std::isfinite(x)||!std::isfinite(y)||x<0||x>1||y<0||y>1)throw std::runtime_error("远程桌面未就绪或坐标无效");sent(LiSendMousePositionEvent(std::lround(x*32766),std::lround(y*32766),32767,32767));}
void Remote::click(int b,bool twice){if(!ready()||(b!=1&&b!=2))throw std::runtime_error("远程鼠标未就绪");int button=b==1?BUTTON_LEFT:BUTTON_RIGHT;try{for(int i=0;i<(twice?2:1);i++){sent(LiSendMouseButtonEvent(BUTTON_ACTION_PRESS,button));sent(LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE,button));}}catch(...){release();throw;}}
void Remote::key(uint8_t usage,uint8_t modifiers){if(!ready())throw std::runtime_error("远程键盘未就绪");int vk=virtual_key(usage);if(!vk)throw std::runtime_error("不支持的按键");char mods=(modifiers&1?MODIFIER_CTRL:0)|(modifiers&2?MODIFIER_SHIFT:0)|(modifiers&4?MODIFIER_ALT:0)|(modifiers&8?MODIFIER_META:0);
    // Explicit modifier transitions also support hosts which don't synthesize
    // them from the modifier mask (particularly Command/Meta shortcuts).
    auto down=[&](int key){p_->held[key]=true;sent(LiSendKeyboardEvent(wire_key(key),KEY_ACTION_DOWN,mods));};
    auto up=[&](int key,char mask){sent(LiSendKeyboardEvent(wire_key(key),KEY_ACTION_UP,mask));p_->held[key]=false;};
    const int keys[]={0xa2,0xa0,0xa4,0x5b};try{for(int i=0;i<4;i++)if(modifiers&(1<<i))down(keys[i]);
        down(vk);up(vk,mods);for(int i=3;i>=0;i--)if(modifiers&(1<<i))up(keys[i],0);
    }catch(...){release();throw;}
}
void Remote::type(const std::string&s){if(!ready()||s.empty()||s.size()>640)throw std::runtime_error("输入文字无效");sent(LiSendUtf8TextEvent(s.data(),s.size()));}
void Remote::scroll(int n){if(!ready()||n<-8||n>8)throw std::runtime_error("滚动参数无效");sent(LiSendScrollEvent(n));}
void Remote::release(){if(!p_->started)return;for(int b:{BUTTON_LEFT,BUTTON_RIGHT,BUTTON_MIDDLE})LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE,b);for(int key=0;key<256;key++)if(p_->held[key]){LiSendKeyboardEvent(wire_key(key),KEY_ACTION_UP,0);p_->held[key]=false;}}
}
