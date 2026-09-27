#pragma once
#include "gamestream.hpp"
#include <atomic>
#include <memory>
#include <vector>
namespace moonpilot {
struct Frame {
    std::vector<uint32_t> rgb;
    int width=0,height=0;
    uint64_t sequence=0;
    uint32_t time=0;
};
class Remote {
    struct Impl;std::unique_ptr<Impl> p_;
public:
    Remote();~Remote();
    // start/stop run on an app worker; poll and input run on the GUI thread.
    void start(const Host&,int app,const std::string&data,const std::string&root,std::atomic<bool>&request_cancel);
    void cancel();void stop();void poll();
    bool ready()const;bool active()const;
    std::string status()const;std::string error()const;
    const Frame&frame()const;
    void move(double x,double y);void click(int button=1,bool twice=false);
    void key(uint8_t usage,uint8_t modifiers=0);void type(const std::string&);
    void scroll(int amount);void release();
};
int virtual_key(uint8_t usage);
// GameStream encodes Windows virtual keys with the 0x80 high-byte tag.
inline short wire_key(int vk){return static_cast<short>(0x8000|vk);}
}
