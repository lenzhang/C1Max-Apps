#include "usb.hpp"
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <signal.h>
#include <unistd.h>
namespace hidpilot {
static uint32_t tick(){return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
std::string Usb::state()const{std::ifstream f("/tmp/c1max-hidpilot/status");std::string s;std::getline(f,s);return s;}
bool Usb::ready()const{return pid_>0&&fd_>=0&&!stopping_&&state()=="READY";}
void Usb::start(const std::string&binary,const std::string&log){
    if(active())return;error.clear();stopping_=false;since_=tick();
    int out=open(log.c_str(),O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC,0600);if(out<0)throw std::runtime_error("无法写入 USB 会话日志");pid_t parent=getpid();pid_=fork();
    if(pid_==0){setpgid(0,0);prctl(PR_SET_PDEATHSIG,SIGTERM);if(getppid()!=parent)_exit(1);dup2(out,1);dup2(out,2);int in=open("/dev/null",O_RDONLY);dup2(in,0);execl(binary.c_str(),binary.c_str(),"--serve",nullptr);_exit(127);}close(out);
    if(pid_<0)throw std::runtime_error("无法启动 USB HID 会话");
}
void Usb::stop(){
    queue_.clear();waiting_=false;stopping_=true;
    if(fd_>=0){close(fd_);fd_=-1;}if(pid_>0)kill(pid_,SIGTERM);since_=tick();
}
Usb::~Usb(){stop();for(int i=0;pid_>0&&i<40;i++){poll(tick());usleep(50000);}if(pid_>0){kill(pid_,SIGKILL);while(waitpid(pid_,nullptr,0)<0&&errno==EINTR){}}}
void Usb::poll(uint32_t now){
    if(pid_<=0)return;int status;pid_t r=waitpid(pid_,&status,WNOHANG);
    if(r==pid_){pid_=-1;if(fd_>=0)close(fd_);fd_=-1;queue_.clear();waiting_=false;if(!stopping_&&(!WIFEXITED(status)||WEXITSTATUS(status)))error="USB 启动失败，已尝试恢复 ADB；检查 USB 日志";stopping_=false;return;}
    if(stopping_)return;
    if(fd_<0){
        if(now-since_>12000){error="USB 连接超时，恢复原配置";stop();return;}
        if(state()!="READY")return;
        int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);sockaddr_un a{};a.sun_family=AF_UNIX;strcpy(a.sun_path,"/tmp/c1max-hidpilot/control");
        if(fd>=0&&connect(fd,(sockaddr*)&a,sizeof a)==0)fd_=fd;else if(fd>=0)close(fd);return;
    }
    if(state()!="READY"){error="USB 已断开，已停止动作";stop();return;}
    if(waiting_){uint8_t ack=0;auto n=recv(fd_,&ack,1,MSG_DONTWAIT);if(n==1&&ack==1){waiting_=false;queue_.pop_front();}
        else if(n==0||(n==1&&!ack)||(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)||now-sent_>1500){error="电脑未接收 USB 动作，已停止";stop();return;}}
    if(!waiting_&&!queue_.empty()&&now-sent_>=12){auto&p=queue_.front();if(::send(fd_,p.data(),p.size(),MSG_NOSIGNAL)!=(ssize_t)p.size()){error="USB 动作发送失败";stop();return;}waiting_=true;sent_=now;}
}
void Usb::send(const std::vector<uint8_t>&r){if(!ready())throw std::runtime_error("请先连接 USB HID");if(!valid_report(r.data(),r.size())||queue_.size()>=400)throw std::runtime_error("USB 动作队列已满或报告无效");queue_.push_back(r);}
void Usb::key(uint8_t usage,uint8_t mods){send(keyboard(usage,mods));send(keyboard());}
void Usb::click(uint8_t b){send(relative(0,0,b));send(relative(0,0));}
void Usb::release(){if(waiting_&&!queue_.empty()){auto pending=queue_.front();queue_.clear();queue_.push_back(std::move(pending));}else queue_.clear();if(ready()){send(keyboard());send(relative(0,0));}}
}
