// Optional, session-scoped FunctionFS HID. Never edits the stock boot scripts.
#include "reports.hpp"
#include <linux/usb/functionfs.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <chrono>
#include <stdexcept>
#include <filesystem>
using namespace hidpilot;
namespace {
const std::string gadget="/sys/kernel/config/usb_gadget/demo",function=gadget+"/functions/ffs.hidpilot",linkpath=gadget+"/configs/c.1/ffs.hidpilot",mountpath="/dev/usb-ffs/hidpilot",runpath="/tmp/c1max-hidpilot",socketpath=runpath+"/control";
volatile sig_atomic_t stopped=0;
void stop(int){stopped=1;}void interrupt_io(int){}
uint64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
void checked(bool ok,const char* what){if(!ok)throw std::runtime_error(std::string(what)+": "+strerror(errno));}
std::string read_text(const std::string&p){std::ifstream f(p);std::string s;std::getline(f,s);return s;}
bool write_text(const std::string&p,const std::string&s){int fd=open(p.c_str(),O_WRONLY|O_TRUNC|O_CLOEXEC);if(fd<0)return false;auto n=write(fd,s.data(),s.size());close(fd);return n==ssize_t(s.size());}
void log(const char*s){fprintf(stderr,"[hidpilot usb] %s\n",s);fflush(stderr);}
void status(const char*s){write_text(runpath+"/status",std::string(s)+"\n");}
void restore(const std::string& udc,int ep0,int ep1){
    // Closing the last FunctionFS fd unregisters the composite gadget on 4.4.
    // Close it while unbound, remove only our function, THEN restore ADB/MTP.
    log("restoring original USB configuration");status("RESTORING");
    write_text(gadget+"/UDC","\n");unlink(linkpath.c_str());
    if(ep1>=0)close(ep1);if(ep0>=0)close(ep0);
    for(int n=0;n<20;n++){if(umount(mountpath.c_str())==0||errno==EINVAL||errno==ENOENT)break;usleep(50000);}
    rmdir(mountpath.c_str());rmdir(function.c_str());
    bool restored=false;
    for(int n=0;n<20;n++){if(read_text(gadget+"/UDC")==udc){restored=true;break;}if(write_text(gadget+"/UDC",udc)){restored=true;break;}usleep(100000);}
    status(restored?"OFF":"RESTORE_FAILED");log(restored?"ADB/MTP restored":"RESTORE FAILED; reconnect/reboot restores original boot configuration");
    unlink(socketpath.c_str());
}
bool report(int fd,const std::vector<uint8_t>& p){alarm(1);auto n=write(fd,p.data(),p.size());alarm(0);return n==ssize_t(p.size());}
void release(int fd){report(fd,keyboard());report(fd,relative(0,0));}
void control(int fd,const usb_ctrlrequest&r){
    const unsigned length=le16toh(r.wLength),value=le16toh(r.wValue);std::vector<uint8_t> data;bool valid=false;
    if(r.bRequestType==0x81&&r.bRequest==6&&(value>>8)==0x22){data=report_descriptor();valid=true;}
    else if(r.bRequestType==0xa1){
        if(r.bRequest==1&&(value>>8)==1){auto id=value&255;if(id==1)data=keyboard();else if(id==2)data=absolute(0,0);else if(id==3)data=relative(0,0);valid=!data.empty();}
        else if(r.bRequest==2){data={0};valid=true;}
        else if(r.bRequest==3){data={1};valid=true;}
    }else if(r.bRequestType==0x21&&(r.bRequest==9||r.bRequest==10||r.bRequest==11)&&length<=64){
        uint8_t buf[64];alarm(1);auto n=read(fd,buf,length);alarm(0);(void)n;return;
    }
    if(valid){data.resize(std::min<size_t>(data.size(),length));alarm(1);auto n=write(fd,data.data(),data.size());alarm(0);(void)n;}
    else{char b; // A reverse-direction operation deliberately stalls ep0.
        if(r.bRequestType&0x80)read(fd,&b,0);else write(fd,&b,0);
    }
}
}
int main(int argc,char**argv){
    bool probe=argc==2&&std::string(argv[1])=="--probe";
    if(argc!=2||(!probe&&std::string(argv[1])!="--serve")){fprintf(stderr,"Usage: c1max-hidpilot-usb --probe|--serve\n");return 2;}
    umask(0077);signal(SIGPIPE,SIG_IGN);
    struct sigaction sa{};sigemptyset(&sa.sa_mask);sa.sa_handler=stop;sigaction(SIGTERM,&sa,nullptr);sigaction(SIGINT,&sa,nullptr);sigaction(SIGHUP,&sa,nullptr);sa.sa_handler=interrupt_io;sigaction(SIGALRM,&sa,nullptr);
    int ep0=-1,ep1=-1,lock=-1,keep=-1,listenfd=-1,client=-1;pid_t guard=-1;std::string udc;bool prepared=false;
    try{
        std::filesystem::create_directories(runpath);chmod(runpath.c_str(),0700);
        lock=open((runpath+"/lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);checked(lock>=0&&flock(lock,LOCK_EX|LOCK_NB)==0,"USB session already running");
        int sf=open((runpath+"/status").c_str(),O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC,0600);checked(sf>=0,"status file");close(sf);status("PREPARING");
        udc=read_text(gadget+"/UDC");checked(udc=="13500000.otg","Unexpected USB controller or USB not bound");
        checked(std::filesystem::is_symlink(gadget+"/configs/c.1/ffs.adb"),"ADB function not present");
        checked(mkdir(function.c_str(),0700)==0,"FunctionFS HID unavailable or stale HID function");prepared=true;
        if(mkdir(mountpath.c_str(),0700)<0&&errno!=EEXIST)checked(false,"mount directory");
        checked(mount("hidpilot",mountpath.c_str(),"functionfs",0,nullptr)==0,"FunctionFS mount");
        ep0=open((mountpath+"/ep0").c_str(),O_RDWR|O_NONBLOCK|O_CLOEXEC);checked(ep0>=0,"open ep0");
        auto d=descriptors(),s=strings();checked(write(ep0,d.data(),d.size())==ssize_t(d.size()),"HID descriptors rejected");checked(write(ep0,s.data(),s.size())==ssize_t(s.size()),"HID strings rejected");
        ep1=open((mountpath+"/ep1").c_str(),O_WRONLY|O_NONBLOCK|O_CLOEXEC);checked(ep1>=0,"open interrupt endpoint");
        // Guardian inherits endpoint fds. Even SIGKILL of this worker cannot
        // close the last fd before it has removed HID and restored ADB/MTP.
        int pipefd[2];checked(pipe2(pipefd,O_CLOEXEC)==0,"guardian pipe");guard=fork();checked(guard>=0,"guardian fork");
        if(!guard){close(pipefd[1]);signal(SIGTERM,SIG_IGN);signal(SIGINT,SIG_IGN);signal(SIGHUP,SIG_IGN);setsid();char b;while(read(pipefd[0],&b,1)<0&&errno==EINTR){}close(pipefd[0]);restore(udc,ep0,ep1);_exit(0);}
        close(pipefd[0]);keep=pipefd[1];
        listenfd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);checked(listenfd>=0,"control socket");
        sockaddr_un addr{};addr.sun_family=AF_UNIX;std::snprintf(addr.sun_path,sizeof addr.sun_path,"%s",socketpath.c_str());unlink(socketpath.c_str());checked(bind(listenfd,(sockaddr*)&addr,sizeof addr)==0&&listen(listenfd,1)==0,"listen control socket");chmod(socketpath.c_str(),0600);
        log("descriptors accepted; re-enumerating ADB + MTP + HID");
        checked(write_text(gadget+"/UDC","\n"),"unbind USB");
        checked(symlink(function.c_str(),linkpath.c_str())==0,"link HID function");
        // Stock adbd closes/reopens FunctionFS on DISCONNECTED. Wait for its
        // descriptors to return; an immediate bind can transiently return ENODEV.
        usleep(300000);bool bound=false;
        for(int n=0;n<20&&!stopped;n++){if(read_text(gadget+"/UDC")==udc||write_text(gadget+"/UDC",udc)){bound=true;break;}usleep(100000);}
        checked(bound,"bind composite USB");status("ENUMERATING");
        auto started=now();bool enabled=false,ever_enabled=false,had_client=false;int result=0;
        while(!stopped){
            if((probe&&now()-started>15000)||(!had_client&&!probe&&now()-started>30000))break;
            if(!ever_enabled&&now()-started>10000){log("host did not enable HID; restoring");result=3;break;}
            pollfd fds[]={{ep0,POLLIN,0},{listenfd,POLLIN,0},{client,POLLIN,0}};
            int n=poll(fds,3,100);if(n<0&&errno==EINTR)continue;checked(n>=0,"poll");
            if(fds[0].revents&POLLIN){usb_functionfs_event ev[4];auto bytes=read(ep0,ev,sizeof ev);for(int i=0;bytes>0&&i<int(bytes/sizeof ev[0]);i++){
                if(ev[i].type==FUNCTIONFS_SETUP)control(ep0,ev[i].u.setup);
                else if(ev[i].type==FUNCTIONFS_ENABLE){enabled=ever_enabled=true;status("READY");log("HID enabled by host");}
                else if(ev[i].type==FUNCTIONFS_DISABLE||ev[i].type==FUNCTIONFS_UNBIND){enabled=false;status("DISCONNECTED");}
            }}
            if(fds[1].revents&POLLIN){int c=accept4(listenfd,nullptr,nullptr,SOCK_CLOEXEC|SOCK_NONBLOCK);if(c>=0){if(client>=0)close(c);else{client=c;had_client=true;}}}
            if(client>=0&&(fds[2].revents&(POLLIN|POLLHUP|POLLERR))){uint8_t packet[64];auto bytes=recv(client,packet,sizeof packet,MSG_TRUNC);
                if(bytes<=0){if(enabled)release(ep1);break;}
                if(enabled&&bytes<=64&&valid_report(packet,bytes)){bool ok=report(ep1,std::vector<uint8_t>(packet,packet+bytes));uint8_t ack=ok?1:0;send(client,&ack,1,MSG_NOSIGNAL);if(!ok){status("IO_ERROR");result=4;break;}}
                else{uint8_t ack=0;send(client,&ack,1,MSG_NOSIGNAL);}
            }
        }
        if(enabled)release(ep1);
        if(client>=0)close(client);close(listenfd);close(ep1);ep1=-1;close(ep0);ep0=-1;close(keep);keep=-1;
        while(waitpid(guard,nullptr,0)<0&&errno==EINTR){}close(lock);return result;
    }catch(const std::exception&e){fprintf(stderr,"[hidpilot usb] %s\n",e.what());
        if(client>=0)close(client);if(listenfd>=0)close(listenfd);
        if(guard>0){if(ep1>=0)close(ep1);if(ep0>=0)close(ep0);if(keep>=0)close(keep);while(waitpid(guard,nullptr,0)<0&&errno==EINTR){}}
        else if(prepared){restore(udc,ep0,ep1);}
        status("FAILED");if(lock>=0)close(lock);return 1;
    }
}
