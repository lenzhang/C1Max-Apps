#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace c1input {
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
}
