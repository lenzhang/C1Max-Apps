// SPDX-License-Identifier: GPL-3.0-only
#include "focus.hpp"
#include <linux/videodev2.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
namespace chat {
namespace {
int control(int fd,unsigned long op,void*p){int r;do{r=ioctl(fd,op,p);}while(r<0&&errno==EINTR);return r;}
}
bool FocusMotor::open(){
    if(fd_>=0)return true;
    FILE*f=fopen("/sys/class/video4linux/v4l-subdev0/name","r");char name[80]{};if(!f)return false;
    bool correct=fgets(name,sizeof name,f)&&strncmp(name,"dw9714 ",7)==0;fclose(f);if(!correct)return false;
    int fd=::open("/dev/v4l-subdev0",O_RDWR|O_NONBLOCK|O_CLOEXEC);if(fd<0)return false;
    v4l2_queryctrl q{};q.id=V4L2_CID_FOCUS_ABSOLUTE;v4l2_control c{};c.id=q.id;
    if(control(fd,VIDIOC_QUERYCTRL,&q)<0||q.flags&(V4L2_CTRL_FLAG_DISABLED|V4L2_CTRL_FLAG_READ_ONLY)||control(fd,VIDIOC_G_CTRL,&c)<0||q.minimum<0||q.minimum>=dw9714_max_position||q.maximum<=q.minimum||c.value<q.minimum||c.value>dw9714_max_position){::close(fd);return false;}
    fd_=fd;minimum_=q.minimum;maximum_=std::min(q.maximum,dw9714_max_position);step_=std::max(1,q.step);original_=position_=c.value;
    fprintf(stderr,"[tox focus] driver=%d..%d using=%d..%d original=%d\n",q.minimum,q.maximum,minimum_,maximum_,original_);return true;
}
bool FocusMotor::move(int p){
    if(fd_<0)return false;p=std::clamp(p,minimum_,maximum_);p=minimum_+(p-minimum_)/step_*step_;
    // Small movements avoid slamming across the focus travel in one command.
    while(p!=position_){int next=position_+std::clamp(p-position_,-64,64);v4l2_control c{};c.id=V4L2_CID_FOCUS_ABSOLUTE;c.value=next;
        if(control(fd_,VIDIOC_S_CTRL,&c)<0){fprintf(stderr,"[tox focus] move failed: %s\n",strerror(errno));return false;}
        position_=next;changed_=true;if(p!=position_)usleep(2000);
    }return true;
}
FocusMotor::~FocusMotor(){if(fd_>=0){if(changed_)move(original_);::close(fd_);}}
void FocusSearch::start(int low,int high,int origin){
    low_=low;high_=high;origin_=best_=std::clamp(origin,low,high);radius_=std::max(1,(high-low)/8);
    best_score_=-1;origin_score_=0;fine_=false;confident_=false;active_=true;index_=0;positions_={origin_};
    for(int i=0;i<=8;i++){int p=low+(high-low)*i/8;if(p!=origin_)positions_.push_back(p);}
}
bool FocusSearch::sample(double score){
    if(!active_)return false;
    if(!fine_&&index_==0)origin_score_=score;
    if(score>best_score_){best_score_=score;best_=target();}
    if(++index_<positions_.size())return true;
    if(!fine_){
        int center=best_;positions_.clear();for(int i=-3;i<=3;i++){int p=std::clamp(center+i*std::max(1,radius_/4),low_,high_);if(p!=center&&std::find(positions_.begin(),positions_.end(),p)==positions_.end())positions_.push_back(p);}
        fine_=true;index_=0;if(!positions_.empty())return true;
    }
    active_=false;
    // On blank/dark/textureless scenes restore the starting position.
    // A low-contrast noisy maximum is not presented as a focus lock.
    confident_=best_score_>=80&&(best_==origin_||best_score_>origin_score_*1.08);
    return false;
}
}
