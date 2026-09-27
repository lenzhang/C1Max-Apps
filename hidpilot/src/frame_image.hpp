#pragma once
#include "capture.hpp"
#include "geometry.hpp"
#include <memory>
namespace hidpilot {
inline std::vector<uint32_t> rectify(const Capture&camera,const Quad&q,unsigned w,unsigned h){
    if(!camera.has_frame()||!w||!h||w>1024||h>1024)throw std::runtime_error("摄像头尚未准备好");
    Homography map(q);std::vector<uint32_t>rgb(size_t(w)*h);
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
        auto p=map.map((x+0.5)/w,(y+0.5)/h);
        unsigned sx=std::clamp<int>(std::lround(p.x*(camera.width()-1)),0,camera.width()-1),sy=std::clamp<int>(std::lround(p.y*(camera.height()-1)),0,camera.height()-1);
        rgb[y*w+x]=camera.pixel(sx,sy);
    }return rgb;
}
std::vector<uint8_t> jpeg(const std::vector<uint32_t>&rgb,unsigned w,unsigned h);
}
