// Host-only adapter: renders the real LVGL UI to PPM. No simulated framebuffer
// is ever packaged for the device, and these images are labelled host previews.
#include "display.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
namespace screen {
bool quit=false,playing=false,tap=false;
static uint32_t pixels[800*340],draw[800*40];
static auto start=std::chrono::steady_clock::now();
uint32_t tick(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();}
static void flush(lv_display_t*d,const lv_area_t*a,uint8_t*p){int width=a->x2-a->x1+1;for(int y=a->y1;y<=a->y2;y++)memcpy(pixels+y*800+a->x1,p+(y-a->y1)*width*4,width*4);lv_display_flush_ready(d);}
bool open(){lv_init();lv_tick_set_cb(tick);auto*d=lv_display_create(800,340);lv_display_set_color_format(d,LV_COLOR_FORMAT_ARGB8888);lv_display_set_buffers(d,draw,nullptr,sizeof draw,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);return true;}
bool caps_lock(){return false;}
uint32_t take_key(){
    static bool initialized=false;static std::vector<int>keys;static size_t index=0;
    if(!initialized){initialized=true;if(auto*s=getenv("C1_TOX_QA_KEYS")){std::string v=s;size_t p;do{p=v.find(',');keys.push_back(std::stoi(v.substr(0,p)));if(p!=std::string::npos)v.erase(0,p+1);}while(p!=std::string::npos);}}
    if(index<keys.size()&&tick()>1500+index*250)return keys[index++];
    if(tick()>2500+keys.size()*250)quit=true;return 0;
}
void close(){auto*path=getenv("C1_TOX_QA_OUTPUT");if(!path)return;FILE*f=fopen(path,"wb");if(!f)return;fprintf(f,"P6\n800 340\n255\n");for(auto p:pixels){unsigned char rgb[]={static_cast<unsigned char>(p>>16),static_cast<unsigned char>(p>>8),static_cast<unsigned char>(p)};fwrite(rgb,1,3,f);}fclose(f);lv_deinit();}
}
