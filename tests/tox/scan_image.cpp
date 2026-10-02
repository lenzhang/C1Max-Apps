// SPDX-License-Identifier: GPL-3.0-only
#include "scan_image.hpp"
#include "focus.hpp"
#include "qr.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>
#include <fstream>
int main(int argc,char**argv){
    assert(argc==2);std::ifstream file(argv[1]);std::string id;file>>id;auto qr=chat::make_tox_qr(id,170);assert(qr.size>100);
    // High-res frame with row padding. QR is small, but all native pixels are
    // retained by the 2x region instead of enlarging a reduced preview image.
    constexpr unsigned w=1024,h=972,stride=1032;
    std::vector<uint8_t>gray(stride*h,200);unsigned x0=(w-qr.size)/2,y0=(h-qr.size)/2;
    for(unsigned y=0;y<qr.size;y++)for(unsigned x=0;x<qr.size;x++)gray[size_t(y+y0)*stride+x+x0]=qr.pixels[size_t(y)*qr.size+x]&255;
    chat::QrDecoder decoder;
    for(unsigned zoom:{100u,150u,200u}){
        auto c=chat::scan_crop(w,h,zoom);assert(c.x+c.w<=w&&c.y+c.h<=h);
        std::vector<uint8_t>region;unsigned rw,rh;chat::crop_gray(gray.data(),stride,c,region,1024,rw,rh);
        auto results=decoder.decode(region.data(),rw,rh,rw);assert(!results.empty());std::string got,error;bool matched=false;
        for(auto&p:results)if(chat::parse_tox_qr(p,got,error)&&got==id)matched=true;assert(matched);
        if(zoom==200){assert(rw==512&&rh==486);assert(region[0]==gray[size_t(c.y)*stride+c.x]);}
        std::vector<uint32_t>preview;chat::scan_preview(gray.data(),stride,c,preview,236,236);assert(preview.size()==236*236);
    }
    auto c=chat::scan_crop(w,h,100);double sharp=chat::focus_score(gray.data(),stride,c);
    auto blurred=gray;for(unsigned y=2;y<h-2;y++)for(unsigned x=2;x<w-2;x++){unsigned sum=0;for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++)sum+=gray[size_t(int(y)+dy)*stride+int(x)+dx];blurred[size_t(y)*stride+x]=sum/25;}
    assert(sharp>chat::focus_score(blurred.data(),stride,c)*1.5);
    // A scene with no detail returns to its original focus. A sharp peak is
    // refined to a nearby motor position, bounded below the register mode bits.
    chat::FocusSearch search;search.start(0,1023,200);unsigned steps=0;
    while(search.active()){assert(search.target()>=0&&search.target()<=1023);search.sample(0);assert(++steps<=18);}
    assert(!search.confident()&&search.result()==200);
    search.start(0,1023,0);steps=0;
    while(search.active()){double distance=search.target()-410.;search.sample(2000./(1.+distance*distance/1600.));assert(++steps<=18);}
    assert(search.confident()&&std::abs(search.result()-410)<=32);
    search.start(0,1023,512);search.cancel();assert(!search.active());
    puts("PASS native-detail QR crops at all zooms, focus sharpness/search bounds, textureless fallback, cancellation");
}
