// SPDX-License-Identifier: GPL-3.0-only
#include "qr.hpp"
#include "engine.hpp"
#include <toxcore/tox.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
// Independently generated toxcore address, not a hand-constructed checksum.
int main(int argc,char**argv){
    Tox_Options*options=tox_options_new(nullptr);assert(options);tox_options_set_udp_enabled(options,false);
    Tox*t=tox_new(options,nullptr);assert(t);uint8_t bytes[TOX_ADDRESS_SIZE];tox_self_get_address(t,bytes);tox_kill(t);tox_options_free(options);
    std::string id=chat::hex(bytes,sizeof bytes),decoded,error;
    assert(chat::parse_tox_qr(id,decoded,error)&&decoded==id);
    std::string lower=id;std::transform(lower.begin(),lower.end(),lower.begin(),[](char c){return c>='A'&&c<='F'?c+32:c;});
    assert(chat::parse_tox_qr(" \nToX:"+lower+"\r\t",decoded,error)&&decoded==id);
    assert(!chat::parse_tox_qr(id,decoded,error,id));
    std::string bad=id;bad.back()=bad.back()=='0'?'1':'0';assert(!chat::parse_tox_qr(bad,decoded,error));
    for(auto&s:{"https://example.org/"+id,"tox://"+id,id.substr(0,64),"tox:"+id+"?message=hello",std::string(1024,'a'),"tox:"+id+std::string(1,'\0')})assert(!chat::parse_tox_qr(s,decoded,error));
    auto image=chat::make_tox_qr(id);assert(image.size>180&&image.size<=234);
    std::vector<uint8_t> gray(image.pixels.size());for(size_t i=0;i<gray.size();i++)gray[i]=image.pixels[i]&255;
    chat::QrDecoder reader;
    auto check=[&](const std::vector<uint8_t>& frame,unsigned w,unsigned h,unsigned stride){
        auto payloads=reader.decode(frame.data(),w,h,stride);assert(!payloads.empty());bool matched=false;
        for(auto&p:payloads)if(chat::parse_tox_qr(p,decoded,error)&&decoded==id)matched=true;assert(matched);
    };
    auto start=std::chrono::steady_clock::now();check(gray,image.size,image.size,image.size);
    // Rotation, camera-like luminance range, padded stride and asymmetric margins.
    for(unsigned rotation=0;rotation<4;rotation++){
        std::vector<uint8_t> next(gray.size());for(unsigned y=0;y<image.size;y++)for(unsigned x=0;x<image.size;x++)next[size_t(x)*image.size+image.size-1-y]=gray[size_t(y)*image.size+x];gray=std::move(next);
        std::vector<uint8_t> camera(520*486,207);for(unsigned y=0;y<image.size;y++)for(unsigned x=0;x<image.size;x++)camera[size_t(y+110)*520+x+98]=gray[size_t(y)*image.size+x]?210:35;
        check(camera,512,486,520);
    }
    // Reflection occurs with some front-camera clients.
    std::vector<uint8_t> mirrored(gray.size());for(unsigned y=0;y<image.size;y++)for(unsigned x=0;x<image.size;x++)mirrored[size_t(y)*image.size+x]=gray[size_t(y)*image.size+image.size-1-x];check(mirrored,image.size,image.size,image.size);
    std::vector<uint8_t> blank(512*486,127);assert(reader.decode(blank.data(),512,486,512).empty());
    bool rejected=false;try{reader.decode(nullptr,512,486,512);}catch(...){rejected=true;}assert(rejected);
    if(argc==2){std::ofstream out(std::string(argv[1])+".pgm",std::ios::binary);out<<"P5\n"<<image.size<<" "<<image.size<<"\n255\n";out.write(reinterpret_cast<const char*>(gray.data()),gray.size());std::ofstream(std::string(argv[1])+".id")<<id<<"\n";}
    std::cout<<"PASS Tox URI/checksum/self-ID validation; QR decode, rotations, padded camera frames, mirror, blank. "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<" ms\n";
}
