#pragma once
#include <linux/videodev2.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
namespace hidpilot {
class Focus {
    int fd_=-1,original_=0,position_=0,lo_=0,hi_=1023;
public:
    ~Focus(){close();}
    void close(){if(fd_>=0){move(original_);::close(fd_);fd_=-1;}}
    bool open(){close();std::error_code e;for(auto&entry:std::filesystem::directory_iterator("/sys/class/video4linux",e)){
        std::ifstream f(entry.path()/"name");std::string name;std::getline(f,name);if(name.rfind("dw9714 ",0)!=0)continue;
        auto path="/dev/"+entry.path().filename().string();int fd=::open(path.c_str(),O_RDWR|O_CLOEXEC);if(fd<0)continue;
        v4l2_queryctrl q{};q.id=V4L2_CID_FOCUS_ABSOLUTE;v4l2_control c{};c.id=q.id;
        if(ioctl(fd,VIDIOC_QUERYCTRL,&q)<0||ioctl(fd,VIDIOC_G_CTRL,&c)<0||q.minimum<0||q.minimum>=1023||q.maximum<=q.minimum||c.value<0||c.value>1023){::close(fd);continue;}
        fd_=fd;lo_=q.minimum;hi_=std::min(q.maximum,1023);original_=position_=c.value;return true;
    }return false;}
    bool move(int target){if(fd_<0)return false;target=std::clamp(target,lo_,hi_);while(target!=position_){v4l2_control c{};c.id=V4L2_CID_FOCUS_ABSOLUTE;c.value=position_+std::clamp(target-position_,-64,64);if(ioctl(fd_,VIDIOC_S_CTRL,&c)<0)return false;position_=c.value;usleep(5000);}return true;}
    bool adjust(int d){return move(position_+d);}
};
}
