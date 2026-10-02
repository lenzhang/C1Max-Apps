// Decode an actual framebuffer crop or camera fixture, without networking.
#include "qr.hpp"
#include <fstream>
#include <iostream>
int main(int argc,char**argv){
    if(argc!=3){std::cerr<<"usage: tox-qr-image grayscale.pgm expected-id.txt\n";return 2;}
    std::ifstream in(argv[1],std::ios::binary),expected(argv[2]);std::string magic,id;unsigned w=0,h=0,max=0;
    in>>magic>>w>>h>>max;char newline=0;in.get(newline);expected>>id;
    if(!in||!expected||magic!="P5"||newline!='\n'||w<16||h<16||w>640||h>640||max!=255)return 2;
    std::vector<uint8_t>gray(size_t(w)*h);in.read(reinterpret_cast<char*>(gray.data()),gray.size());if(!in)return 2;
    chat::QrDecoder decoder;for(auto&p:decoder.decode(gray.data(),w,h,w)){std::string found,error;if(chat::parse_tox_qr(p,found,error)&&found==id){std::cout<<"PASS screenshot QR matches expected Tox ID\n";return 0;}}
    std::cerr<<"FAIL QR missing or mismatched\n";return 1;
}
