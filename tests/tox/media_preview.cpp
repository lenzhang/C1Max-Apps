#include "media.hpp"
#include "net.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
int main(int argc,char**argv){
    assert(argc==3);std::string dir=argv[1];std::filesystem::create_directories(dir);
    std::string wav="RIFF";auto le32=[&](uint32_t n){for(int i=0;i<4;i++)wav+=char(n>>(i*8));};auto le16=[&](uint16_t n){wav+=char(n);wav+=char(n>>8);};
    le32(32036);wav+="WAVEfmt ";le32(16);le16(1);le16(1);le32(16000);le32(32000);le16(2);le16(16);wav+="data";le32(32000);wav.append(32000,'\0');
    c1::save_private(dir+"/voice.wav",wav);auto voice=chat::preview(dir,"voice.wav","voice");assert(voice.error.empty()&&voice.seconds==1&&voice.pixels.empty());
    c1::save_private(dir+"/bad.wav",wav.substr(0,44));assert(!chat::preview(dir,"bad.wav","voice").error.empty());
    assert(!chat::preview(dir,"../voice.wav","voice").error.empty());
    c1::save_private(dir+"/photo.jpg",c1::read_file(argv[2]));auto photo=chat::preview(dir,"photo.jpg","photo");assert(photo.error.empty()&&!photo.pixels.empty()&&photo.width<=168&&photo.height<=100&&photo.pixels.size()==photo.width*photo.height);
    c1::save_private(dir+"/bad.jpg","broken JPEG");assert(!chat::preview(dir,"bad.jpg","photo").error.empty());
    std::cout<<"PASS bounded inline thumbnail, WAV duration, incomplete media and path rejection\n";
}
