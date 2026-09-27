// Explicit on-device, silent decoder probe; not part of the application package.
// Input must be Annex-B H.264 with AUDs, 640x360 at 15 fps, no B frames.
#include "y4m.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <thread>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static uint64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int main(int argc,char**argv){
    if(argc!=4){fprintf(stderr,"usage: decoder-test INPUT.h264 YUV-TAP.so QA-DIRECTORY\n");return 2;}
    signal(SIGPIPE,SIG_IGN);umask(0077);
    std::ifstream source(argv[1],std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(source)),{});
    std::vector<size_t> offsets;
    for(size_t i=0;i+5<bytes.size();i++)if(bytes[i]==0&&bytes[i+1]==0&&bytes[i+2]==0&&bytes[i+3]==1&&(bytes[i+4]&31)==9)offsets.push_back(i);
    if(offsets.size()<60||offsets.front()!=0){fprintf(stderr,"Need >=60 AUD-separated frames\n");return 2;}offsets.push_back(bytes.size());
    std::string fifo=std::string(argv[3])+"/live-frames",log=std::string(argv[3])+"/live-decoder.log";
    if(mkfifo(fifo.c_str(),0600)){perror("mkfifo");return 2;}
    int video=open(fifo.c_str(),O_RDWR|O_NONBLOCK|O_CLOEXEC),fds[2];
    if(video<0||pipe2(fds,O_CLOEXEC)){unlink(fifo.c_str());return 2;}
    int logfd=open(log.c_str(),O_CREAT|O_TRUNC|O_WRONLY|O_CLOEXEC,0600);
    pid_t pid=fork();
    if(pid==0){setenv("LD_PRELOAD",argv[2],1);setenv("C1_YUV_FIFO",fifo.c_str(),1);setenv("C1_YUV_DESKTOP","1",1);unsetenv("C1_YUV_SCALE");
        dup2(fds[0],0);dup2(logfd,1);dup2(logfd,2);close(fds[0]);close(fds[1]);close(logfd);
        execl("/usr/bin/mplayer","mplayer","-noconfig","all","-quiet","-noconsolecontrols","-nolirc","-nojoystick","-nomouseinput","-nosound","-nosub","-noautosub","-vo","null","-nocache","-benchmark","-demuxer","h264es","-fps","15","-",nullptr);_exit(127);}
    close(fds[0]);close(logfd);fcntl(fds[1],F_SETFL,O_NONBLOCK);
    std::atomic<bool>stop{false};uint64_t began=now();
    std::thread writer([&]{for(size_t i=0;i+1<offsets.size()&&!stop;i++){
        size_t off=offsets[i];while(off<offsets[i+1]&&!stop){auto n=write(fds[1],bytes.data()+off,offsets[i+1]-off);if(n>0)off+=n;else if(errno==EAGAIN||errno==EINTR){pollfd p{fds[1],POLLOUT,0};poll(&p,1,20);}else{stop=true;break;}}
        auto due=began+(i+1)*1000/15;while(now()<due&&!stop)usleep(1000);
    }
        // Keep input open after the last frame: first-frame success must not
        // depend on EOF releasing MPlayer's stdin probe buffer.
        auto until=now()+2000;while(now()<until&&!stop)usleep(1000);close(fds[1]);
    });
    Y4mReader reader(800,450);int frames=0;uint64_t first=0;bool valid=true;uint8_t chunk[8192];
    while(now()-began<9000&&!stop){auto n=read(video,chunk,sizeof(chunk));if(n>0){if(!reader.feed(chunk,n,[&](const uint32_t*,int w,int h,int,int){if(w!=640||h!=360)valid=false;if(!frames)first=now()-began;frames++;})){valid=false;break;}}else usleep(1000);}
    stop=true;writer.join();kill(pid,SIGKILL);waitpid(pid,nullptr,0);close(video);unlink(fifo.c_str());
    printf("first_frame_ms=%llu decoded_frames=%d input_frames=%zu valid=%d\n",(unsigned long long)first,frames,offsets.size()-1,int(valid));
    return valid&&frames>=60&&first>0&&first<2000?0:1;
}
