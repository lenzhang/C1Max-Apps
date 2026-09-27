#pragma once
#include "reports.hpp"
#include <deque>
#include <string>
#include <sys/types.h>
namespace hidpilot {
class Usb {
    pid_t pid_=-1;int fd_=-1;bool waiting_=false,stopping_=false;uint32_t since_=0,sent_=0;
    std::deque<std::vector<uint8_t>> queue_;
public:
    std::string error;
    ~Usb();
    void start(const std::string&binary,const std::string&log);
    void stop();
    void poll(uint32_t now);
    bool ready()const;
    bool active()const{return pid_>0;}
    bool idle()const{return queue_.empty()&&!waiting_;}
    void send(const std::vector<uint8_t>&report);
    void key(uint8_t usage,uint8_t mods=0);
    void click(uint8_t buttons=1);
    void release();
    std::string state()const;
};
}
