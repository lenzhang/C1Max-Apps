#include "y4m.hpp"
#include <cassert>
#include <iostream>
#include <string>
int main(){
    std::string header="YUV4MPEG2 W640 H360 F15:1 Ip A1:1 C420jpeg\nFRAME\n";
    std::string pixels(640*360*3/2,char(128));auto packet=header+pixels;
    Y4mReader old;assert(!old.feed((const uint8_t*)packet.data(),packet.size(),[](auto,int,int,int,int){}));
    Y4mReader desktop(800,450);int count=0;
    for(size_t i=0;i<packet.size();i+=137)assert(desktop.feed((const uint8_t*)packet.data()+i,std::min(size_t(137),packet.size()-i),[&](auto,int w,int h,int,int){assert(w==640&&h==360);count++;}));
    assert(count==1&&desktop.frames()==1);
    auto bad=std::string("YUV4MPEG2 W1920 H1080 F15:1 Ip A1:1 C420jpeg\n");assert(!desktop.feed((const uint8_t*)bad.data(),bad.size(),[](auto,int,int,int,int){}));
    std::cout<<"PASS: fragmented desktop frames, existing player limits, oversized-frame rejection\n";
}
