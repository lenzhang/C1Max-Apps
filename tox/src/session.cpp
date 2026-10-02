// SPDX-License-Identifier: GPL-3.0-only
#include "session.hpp"
#include "net.hpp"
#include <algorithm>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
namespace chat {
namespace {
volatile sig_atomic_t service_stop=0;
void stop(int){service_stop=1;}
constexpr size_t max_packet=192*1024;
void exact(int fd,void*p,size_t n,bool write){size_t off=0;while(off<n){ssize_t r=write?send(fd,static_cast<char*>(p)+off,n-off,MSG_NOSIGNAL):recv(fd,static_cast<char*>(p)+off,n-off,0);if(r<0&&errno==EINTR)continue;if(r<=0)throw std::runtime_error("后台连接已断开");off+=r;}}
Json receive(int fd){uint32_t n;exact(fd,&n,4,false);if(n>max_packet)throw std::runtime_error("Invalid service packet");std::string b(n,'\0');exact(fd,b.data(),n,false);return Json::parse(b);}
void send_json(int fd,const Json&j){auto s=j.dump();if(s.size()>max_packet)throw std::runtime_error("Service packet too large");uint32_t n=s.size();exact(fd,&n,4,true);exact(fd,s.data(),s.size(),true);}
sockaddr_un address(const std::string&directory){sockaddr_un a{};a.sun_family=AF_UNIX;std::string p=directory+"/service.sock";if(p.size()>=sizeof a.sun_path)throw std::runtime_error("Tox data path too long");std::strcpy(a.sun_path,p.c_str());return a;}
void timeouts(int fd){timeval t{1,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof t);setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof t);}
int connect_service(const std::string&d){auto a=address(d);int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)return -1;timeouts(fd);if(connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof a)){close(fd);return -1;}return fd;}
Json pack(const Snapshot&s){
    Json j={{"revision",s.revision},{"error_count",s.error_count},{"completed",s.completed},{"command_ok",s.command_ok},{"id",s.id},{"name",s.name},{"status",s.status},{"error",s.error},{"dht_key",s.dht_key},{"udp_port",s.udp_port},{"ready",s.ready},{"online",s.online},{"background",s.background},{"friends",Json::array()},{"requests",Json::array()},{"messages",Json::array()},{"transfers",Json::array()}};
    for(auto&f:s.friends)j["friends"].push_back({{"number",f.number},{"key",f.key},{"name",f.name},{"online",f.online},{"unread",f.unread}});
    for(auto&r:s.requests)j["requests"].push_back({{"key",r.key},{"message",r.message}});
    for(auto&m:s.messages)j["messages"].push_back({{"text",m.text},{"state",m.state},{"mine",m.mine},{"receipt",m.receipt},{"file",m.file},{"kind",m.kind},{"size",m.size}});
    for(auto&t:s.transfers)j["transfers"].push_back({{"id",t.id},{"number",t.number},{"file_number",t.file_number},{"size",t.size},{"done",t.done},{"mine",t.mine},{"name",t.name},{"file",t.file},{"kind",t.kind},{"state",t.state}});return j;
}
Snapshot unpack(const Json&j){Snapshot s;s.revision=j.at("revision");s.error_count=j.at("error_count");s.completed=j.at("completed");s.command_ok=j.at("command_ok");s.id=j.at("id");s.name=j.at("name");s.status=j.at("status");s.error=j.at("error");s.dht_key=j.at("dht_key");s.udp_port=j.at("udp_port");s.ready=j.at("ready");s.online=j.at("online");s.background=j.at("background");
    for(auto&f:j.at("friends"))s.friends.push_back({f.at("number"),f.at("key"),f.at("name"),f.at("online"),f.at("unread")});
    for(auto&r:j.at("requests"))s.requests.push_back({r.at("key"),r.at("message")});
    for(auto&m:j.at("messages"))s.messages.push_back({m.at("text"),m.at("state"),m.at("mine"),m.at("receipt"),m.at("file"),m.at("kind"),m.at("size")});
    for(auto&t:j.at("transfers"))s.transfers.push_back({t.at("id"),t.at("number"),t.at("file_number"),t.at("size"),t.at("done"),t.at("mine"),t.at("name"),t.at("file"),t.at("kind"),t.at("state")});return s;
}
void start_service(const std::string&d,const std::string&nodes,const std::string&program){
    std::string logpath=d+"/service.log";long maxfd=std::min<long>(sysconf(_SC_OPEN_MAX),65536);
    pid_t p=fork();if(p<0)throw std::runtime_error("Cannot start Tox service");if(!p){
        if(setsid()<0)_exit(1);pid_t child=fork();if(child<0)_exit(1);if(child)_exit(0);
        // No inherited display, keyboard, camera or launcher lock descriptors.
        for(int f=3;f<maxfd;f++)close(f);
        int in=open("/dev/null",O_RDONLY);if(in>=0){dup2(in,0);if(in>2)close(in);}
        int log=open(logpath.c_str(),O_CREAT|O_WRONLY|O_TRUNC,0600);if(log>=0){dup2(log,1);dup2(log,2);if(log>2)close(log);}
        execl(program.c_str(),program.c_str(),"--service",d.c_str(),nodes.c_str(),nullptr);_exit(127);
    }int status;while(waitpid(p,&status,0)<0&&errno==EINTR){}
}
}
bool stop_service(const std::string&d){int fd=connect_service(d);if(fd<0)return false;try{send_json(fd,{{"stop",true}});auto response=receive(fd);close(fd);return response.value("stopping",false);}catch(...){close(fd);return false;}}
int run_service(const std::string&d,const std::string&nodes){
    mkdir(d.c_str(),0700);int lock=open((d+"/service.lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){if(lock>=0)close(lock);return 1;}
    int server=-1;std::vector<int>clients;std::string path=d+"/service.sock";
    try{
        auto addr=address(d);unlink(path.c_str());server=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(server<0||bind(server,reinterpret_cast<sockaddr*>(&addr),sizeof addr)||chmod(path.c_str(),0600)||listen(server,4))throw std::runtime_error(std::string("Cannot open Tox service socket: ")+strerror(errno));
        signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGPIPE,SIG_IGN);service_stop=0;Engine engine(d,nodes);
        auto idle=std::chrono::steady_clock::now();
        while(!service_stop){
            std::vector<pollfd>polls{{server,POLLIN,0}};for(int c:clients)polls.push_back({c,POLLIN,0});int ready=poll(polls.data(),polls.size(),100);
            if(ready<0&&errno!=EINTR)break;
            for(size_t i=clients.size();i>0;i--)if(polls[i].revents){int fd=clients[i-1];try{
                auto req=receive(fd);if(req.value("stop",false)){service_stop=1;send_json(fd,{{"stopping",true}});continue;}
                auto commands=req.at("commands");if(!commands.is_array()||commands.size()>16)throw std::runtime_error("Invalid commands");
                for(auto&c:commands){int a=c.at("action");if(a<0||a>int(Action::Background))throw std::runtime_error("Invalid action");Command cmd{Action(a),c.at("number"),c.at("text"),c.at("extra"),c.at("port"),c.at("token")};if(!engine.submit(cmd))throw std::runtime_error("Tox command queue unavailable");}
                send_json(fd,pack(engine.snapshot(req.value("friend",UINT32_MAX))));
            }catch(...){close(fd);clients.erase(clients.begin()+i-1);}}
            if(polls[0].revents&POLLIN){int fd=accept4(server,nullptr,nullptr,SOCK_CLOEXEC);if(fd>=0){if(clients.size()>=4)close(fd);else{timeouts(fd);clients.push_back(fd);}}}
            if(!clients.empty())idle=std::chrono::steady_clock::now();
            else if(!engine.snapshot().background&&std::chrono::steady_clock::now()-idle>std::chrono::seconds(3))break;
        }
    }catch(const std::exception&e){fprintf(stderr,"[tox service] %s\n",e.what());}
    for(int fd:clients)close(fd);if(server>=0)close(server);unlink(path.c_str());close(lock);return 0;
}
Session::Session(std::string d,std::string n,std::string p):directory_(std::move(d)),nodes_(std::move(n)),program_(std::move(p)){worker_=std::thread(&Session::run,this);}
Session::~Session(){stop_=true;if(worker_.joinable())worker_.join();}
bool Session::submit(Command c){std::lock_guard<std::mutex>g(mutex_);if(!state_.ready||commands_.size()>=16)return false;commands_.push_back(std::move(c));return true;}
Snapshot Session::snapshot(uint32_t n){std::lock_guard<std::mutex>g(mutex_);if(selected_!=n){selected_=n;state_.messages.clear();state_.transfers.clear();state_.revision++;}return state_;}
void Session::run(){int fd=-1;try{
    mkdir(directory_.c_str(),0700);fd=connect_service(directory_);
    if(fd<0){start_service(directory_,nodes_,program_);for(int i=0;i<50&&!stop_;i++){std::this_thread::sleep_for(std::chrono::milliseconds(100));fd=connect_service(directory_);if(fd>=0)break;}}
    if(fd<0)throw std::runtime_error("无法连接 Tox 后台服务，请重新打开");
    while(!stop_){Json req={{"commands",Json::array()}};uint32_t friend_number;{std::lock_guard<std::mutex>g(mutex_);friend_number=selected_;req["friend"]=friend_number;for(auto&c:commands_)req["commands"].push_back({{"action",int(c.action)},{"number",c.number},{"text",c.text},{"extra",c.extra},{"port",c.port},{"token",c.token}});commands_.clear();}
        send_json(fd,req);auto next=unpack(receive(fd));{std::lock_guard<std::mutex>g(mutex_);if(selected_!=friend_number){next.messages.clear();next.transfers.clear();}state_=std::move(next);}
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}catch(const std::exception&e){std::lock_guard<std::mutex>g(mutex_);state_.ready=false;state_.status="后台已断开";state_.error=e.what();state_.error_count++;state_.revision++;}if(fd>=0)close(fd);}
}
