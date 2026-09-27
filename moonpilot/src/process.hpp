#pragma once
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <cerrno>
namespace moonpilot {
class Process {
    pid_t pid_=-1;
public:
    int result=-1;
    ~Process(){stop();}
    bool active()const{return pid_>0;}
    void start(std::vector<std::string> args,const std::string&log){
        stop();result=-1;int fd=open(log.c_str(),O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC,0600);if(fd<0)throw std::runtime_error("无法准备音频日志");
        std::vector<char*>av;for(auto&a:args)av.push_back(a.data());av.push_back(nullptr);auto parent=getpid();pid_=fork();
        if(pid_==0){setpgid(0,0);prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(1);dup2(fd,1);dup2(fd,2);int in=open("/dev/null",O_RDONLY);dup2(in,0);execv(av[0],av.data());_exit(127);}close(fd);if(pid_<0)throw std::runtime_error("无法启动音频进程");setpgid(pid_,pid_);
    }
    bool poll(){if(pid_<=0)return false;int s=0;auto r=waitpid(pid_,&s,WNOHANG);if(r!=pid_)return false;pid_=-1;result=WIFEXITED(s)?WEXITSTATUS(s):-1;return true;}
    void finish_recording(){if(pid_>0)kill(pid_,SIGINT);}
    void stop(){if(pid_<=0)return;kill(-pid_,SIGTERM);for(int i=0;i<20;i++){if(poll())return;usleep(10000);}kill(-pid_,SIGKILL);while(waitpid(pid_,nullptr,0)<0&&errno==EINTR){}pid_=-1;}
};
}
