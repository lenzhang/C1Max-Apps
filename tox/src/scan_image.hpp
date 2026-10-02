// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace chat {
struct ScanCrop { unsigned x=0,y=0,w=0,h=0; };
inline ScanCrop scan_crop(unsigned w,unsigned h,unsigned zoom){
    zoom=std::clamp(zoom,100u,200u);unsigned cw=w*100/zoom,ch=h*100/zoom;
    return {(w-cw)/2,(h-ch)/2,cw,ch};
}
// Returns true image samples, never an upscaled preview. Even at 2x the
// 1024x972 source retains a native 512x486 region for the decoder.
inline void crop_gray(const uint8_t*gray,unsigned stride,ScanCrop c,std::vector<uint8_t>&out,unsigned maximum,unsigned&ow,unsigned&oh){
    unsigned longest=std::max(c.w,c.h);ow=c.w;oh=c.h;
    if(longest>maximum){ow=c.w*maximum/longest;oh=c.h*maximum/longest;}
    out.resize(size_t(ow)*oh);
    if(ow==c.w&&oh==c.h){for(unsigned y=0;y<oh;y++)memcpy(out.data()+size_t(y)*ow,gray+size_t(c.y+y)*stride+c.x,ow);return;}
    std::vector<unsigned> columns(ow);for(unsigned x=0;x<ow;x++)columns[x]=x*c.w/ow;
    for(unsigned y=0;y<oh;y++){const uint8_t*row=gray+size_t(c.y+y*c.h/oh)*stride+c.x;for(unsigned x=0;x<ow;x++)out[size_t(y)*ow+x]=row[columns[x]];}
}
inline void scan_preview(const uint8_t*gray,unsigned stride,ScanCrop c,std::vector<uint32_t>&out,unsigned pw,unsigned ph){
    out.assign(size_t(pw)*ph,0xff111b24);unsigned rw=std::min(pw,ph*c.h/c.w),rh=rw*c.w/c.h;
    for(unsigned y=0;y<rh;y++)for(unsigned x=0;x<rw;x++){
        unsigned v=gray[size_t(c.y+c.h-1-x*c.h/rw)*stride+c.x+y*c.w/rh];
        out[size_t(y+(ph-rh)/2)*pw+x+(pw-rw)/2]=0xff000000u|(v<<16)|(v<<8)|v;
    }
}
// Noise floor subtraction reduces the temptation to focus on sensor noise.
// Central edges carry the score; the screen border/background is excluded.
inline double focus_score(const uint8_t*gray,unsigned stride,ScanCrop c){
    if(c.w<16||c.h<16)return 0;
    unsigned left=c.x+c.w/6,top=c.y+c.h/6,right=c.x+c.w*5/6,bottom=c.y+c.h*5/6;
    uint64_t total=0,count=0;
    for(unsigned y=top+2;y+2<bottom;y+=2)for(unsigned x=left+2;x+2<right;x+=2){
        int p=gray[size_t(y)*stride+x];
        int dx=std::abs(int(gray[size_t(y)*stride+x-2])+int(gray[size_t(y)*stride+x+2])-2*p);
        int dy=std::abs(int(gray[size_t(y-2)*stride+x])+int(gray[size_t(y+2)*stride+x])-2*p);
        unsigned edge=std::max(0,dx+dy-12);total+=edge*edge;count++;
    }
    return count?double(total)/count:0;
}
}
