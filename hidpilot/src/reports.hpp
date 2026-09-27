#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hidpilot {
// A single interrupt-IN endpoint carries keyboard, absolute and relative mouse.
// LEDs use SET_REPORT on ep0, saving an OUT endpoint alongside ADB + MTP.
inline const std::vector<uint8_t>& report_descriptor() {
    static const std::vector<uint8_t> d = {
        0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,
        0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0x00,0x25,0x01,
        0x75,0x01,0x95,0x08,0x81,0x02,0x75,0x08,0x95,0x01,0x81,0x03,
        0x05,0x08,0x19,0x01,0x29,0x05,0x75,0x01,0x95,0x05,0x91,0x02,
        0x75,0x03,0x95,0x01,0x91,0x03,
        0x05,0x07,0x19,0x00,0x29,0x65,0x15,0x00,0x25,0x65,
        0x75,0x08,0x95,0x06,0x81,0x00,0xc0,
        0x05,0x01,0x09,0x02,0xa1,0x01,0x85,0x02,0x09,0x01,0xa1,0x00,
        0x05,0x09,0x19,0x01,0x29,0x03,0x15,0x00,0x25,0x01,
        0x95,0x03,0x75,0x01,0x81,0x02,0x95,0x01,0x75,0x05,0x81,0x03,
        0x05,0x01,0x09,0x30,0x09,0x31,0x15,0x00,0x26,0xff,0x7f,
        0x75,0x10,0x95,0x02,0x81,0x02,
        0x09,0x38,0x15,0x81,0x25,0x7f,0x75,0x08,0x95,0x01,0x81,0x06,0xc0,0xc0,
        0x05,0x01,0x09,0x02,0xa1,0x01,0x85,0x03,0x09,0x01,0xa1,0x00,
        0x05,0x09,0x19,0x01,0x29,0x03,0x15,0x00,0x25,0x01,
        0x95,0x03,0x75,0x01,0x81,0x02,0x95,0x01,0x75,0x05,0x81,0x03,
        0x05,0x01,0x09,0x30,0x09,0x31,0x09,0x38,0x15,0x81,0x25,0x7f,
        0x75,0x08,0x95,0x03,0x81,0x06,0xc0,0xc0
    }; return d;
}
inline std::vector<uint8_t> keyboard(uint8_t usage=0,uint8_t mods=0) {return {1,mods,0,usage,0,0,0,0,0};}
inline std::vector<uint8_t> absolute(int x,int y,uint8_t buttons=0,int wheel=0) {
    x=std::clamp(x,0,32767);y=std::clamp(y,0,32767);
    return {2,uint8_t(buttons&7),uint8_t(x),uint8_t(x>>8),uint8_t(y),uint8_t(y>>8),uint8_t(std::clamp(wheel,-127,127))};
}
inline std::vector<uint8_t> relative(int dx,int dy,uint8_t buttons=0,int wheel=0) {
    return {3,uint8_t(buttons&7),uint8_t(std::clamp(dx,-127,127)),uint8_t(std::clamp(dy,-127,127)),uint8_t(std::clamp(wheel,-127,127))};
}
inline bool valid_report(const uint8_t* p,size_t n) {
    if(!p||!n)return false;
    if(p[0]==1&&n==9){if(p[2])return false;for(size_t i=3;i<n;++i)if(p[i]>0x65)return false;return true;}
    if(p[0]==2&&n==7)return !(p[1]&~7)&&p[3]<128&&p[5]<128;
    return p[0]==3&&n==5&&!(p[1]&~7);
}
inline std::array<uint8_t,2> ascii(unsigned c) {
    if(c>='a'&&c<='z')return {uint8_t(4+c-'a'),0};
    if(c>='A'&&c<='Z')return {uint8_t(4+c-'A'),2};
    if(c>='1'&&c<='9')return {uint8_t(30+c-'1'),0};
    if(c=='0')return {39,0};
    const std::string plain="\n\x1b\b\t -=[]\\;\x27`,./";
    const uint8_t keys[]={40,41,42,43,44,45,46,47,48,49,51,52,53,54,55,56};
    auto pos=plain.find(char(c));if(pos!=std::string::npos&&c<128)return {keys[pos],0};
    const std::string shifted="!@#$%^&*()_+{}|:\"~<>?";
    const uint8_t skeys[]={30,31,32,33,34,35,36,37,38,39,45,46,47,48,49,51,52,53,54,55,56};
    pos=shifted.find(char(c));if(pos!=std::string::npos&&c<128)return {skeys[pos],2};
    return {0,0};
}
inline void le32(std::vector<uint8_t>&d,uint32_t n){for(int i=0;i<4;i++)d.push_back(uint8_t(n>>(i*8)));}
inline std::vector<uint8_t> descriptors(){
    std::vector<uint8_t>d;le32(d,3);le32(d,70);le32(d,3);le32(d,3);le32(d,3);
    for(int speed=0;speed<2;speed++){
        const uint16_t n=report_descriptor().size();
        std::vector<uint8_t> one={9,4,0,0,1,3,0,0,1, 9,0x21,0x11,0x01,0,1,0x22,uint8_t(n),uint8_t(n>>8), 7,5,0x81,3,64,0,uint8_t(speed?7:8)};
        d.insert(d.end(),one.begin(),one.end());
    }return d;
}
inline std::vector<uint8_t> strings(){
    const std::string name="HIDPilot Keyboard Mouse";std::vector<uint8_t>d;
    le32(d,2);le32(d,18+name.size()+1);le32(d,1);le32(d,1);d.push_back(9);d.push_back(4);
    d.insert(d.end(),name.begin(),name.end());d.push_back(0);return d;
}
}
