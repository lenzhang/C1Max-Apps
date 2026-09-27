#include "frame_image.hpp"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../camera/src/stb_image_write.h"
namespace moonpilot {
std::vector<uint8_t> jpeg(const std::vector<uint32_t>&rgb,unsigned w,unsigned h){
    if(w<2||h<2||w>1024||h>1024||rgb.size()!=size_t(w)*h)throw std::runtime_error("图像尺寸无效");
    std::vector<uint8_t>bytes(rgb.size()*3),out;for(size_t i=0;i<rgb.size();i++){bytes[i*3]=rgb[i]>>16;bytes[i*3+1]=rgb[i]>>8;bytes[i*3+2]=rgb[i];}
    auto write=[](void*context,void*p,int n){auto&b=*static_cast<std::vector<uint8_t>*>(context);b.insert(b.end(),static_cast<uint8_t*>(p),static_cast<uint8_t*>(p)+n);};
    if(!stbi_write_jpg_to_func(write,&out,w,h,3,bytes.data(),85)||out.size()>512*1024)throw std::runtime_error("图像编码失败");return out;
}
}
