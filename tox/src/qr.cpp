// SPDX-License-Identifier: GPL-3.0-only
#include "qr.hpp"
#include "quirc.h"
#include "src/libs/qrcode/qrcodegen.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace chat {
namespace {
int nibble(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
bool space(char c){return c==' '||c=='\r'||c=='\n'||c=='\t';}
}
bool parse_tox_qr(const std::string& payload,std::string& id,std::string& error,const std::string& own){
    id.clear();error.clear();
    if(payload.size()>256){error="二维码不是 Tox ID";return false;}
    size_t a=0,b=payload.size();while(a<b&&space(payload[a]))a++;while(b>a&&space(payload[b-1]))b--;
    std::string value=payload.substr(a,b-a);
    if(value.size()>=4&&(value[0]=='t'||value[0]=='T')&&(value[1]=='o'||value[1]=='O')&&(value[2]=='x'||value[2]=='X')&&value[3]==':')value.erase(0,4);
    if(value.size()!=76){error="请扫描包含完整 Tox ID 的二维码";return false;}
    uint8_t bytes[38],check[2]={0,0};
    for(size_t i=0;i<38;i++){int hi=nibble(value[i*2]),lo=nibble(value[i*2+1]);if(hi<0||lo<0){error="二维码包含无效 Tox ID";return false;}bytes[i]=uint8_t(hi*16+lo);if(i<36)check[i%2]^=bytes[i];}
    if(check[0]!=bytes[36]||check[1]!=bytes[37]){error="Tox ID 校验失败，请重新扫描";return false;}
    for(char&c:value)if(c>='a'&&c<='f')c-=32;
    if(own.size()>=64){bool same=true;for(size_t i=0;i<64;i++)if(nibble(value[i])!=nibble(own[i]))same=false;if(same){error="这是自己的 ID，请扫描对方的二维码";return false;}}
    id=std::move(value);return true;
}
QrImage make_tox_qr(const std::string& input,unsigned maximum){
    std::string id,error;if(!parse_tox_qr(input,id,error)||maximum<90||maximum>512)return {};
    uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(8)],code[sizeof tmp];
    if(!qrcodegen_encodeText(("tox:"+id).c_str(),tmp,code,qrcodegen_Ecc_MEDIUM,1,8,qrcodegen_Mask_AUTO,true))return {};
    unsigned modules=qrcodegen_getSize(code),scale=maximum/(modules+8);
    if(scale<2)return {};
    QrImage image;image.size=(modules+8)*scale;image.pixels.assign(size_t(image.size)*image.size,0xffffffff);
    for(unsigned y=0;y<modules;y++)for(unsigned x=0;x<modules;x++)if(qrcodegen_getModule(code,x,y))
        for(unsigned dy=0;dy<scale;dy++)for(unsigned dx=0;dx<scale;dx++)image.pixels[size_t((y+4)*scale+dy)*image.size+(x+4)*scale+dx]=0xff000000;
    return image;
}
QrDecoder::QrDecoder():decoder_(quirc_new()){if(!decoder_)throw std::bad_alloc();}
QrDecoder::~QrDecoder(){quirc_destroy(decoder_);}
std::vector<std::string> QrDecoder::decode(const uint8_t*gray,unsigned w,unsigned h,unsigned stride){
    if(!gray||w<16||h<16||w>1024||h>1024||stride<w)throw std::runtime_error("Unsupported QR frame");
    if(width_!=w||height_!=h){if(quirc_resize(decoder_,w,h)<0)throw std::bad_alloc();width_=w;height_=h;}
    uint8_t*dst=quirc_begin(decoder_,nullptr,nullptr);
    for(unsigned y=0;y<h;y++)memcpy(dst+size_t(y)*w,gray+size_t(y)*stride,w);
    quirc_end(decoder_);std::vector<std::string> result;
    for(int i=0;i<std::min(quirc_count(decoder_),8);i++){
        quirc_code code;quirc_data data;quirc_extract(decoder_,i,&code);auto err=quirc_decode(&code,&data);
        if(err==QUIRC_ERROR_DATA_ECC){quirc_flip(&code);err=quirc_decode(&code,&data);}
        if(!err&&data.payload_len>0&&data.payload_len<=256)result.emplace_back(reinterpret_cast<char*>(data.payload),data.payload_len);
    }return result;
}
}
