#include "usb.hpp"
#include "net.hpp"
#include "display.hpp"
#include "idle_reset.h"
#include "lv_tiny_ttf.h"
#include <csignal>
#include <filesystem>
#include <deque>
#include <functional>
#include <unistd.h>
#include <sys/stat.h>
using namespace hidpilot;
namespace {
constexpr uint32_t bg=0x101922,panel=0x1b2a36,ink=0xeef5fa,muted=0xa7bac9,teal=0x5eead4;
Usb usb; uint8_t modifiers=0;
lv_font_t*font=nullptr,*small=nullptr,*title=nullptr;
lv_obj_t*status_label=nullptr,*usb_label=nullptr;
std::string notice="接上电脑后，点右上角连接 USB";
std::deque<std::function<void()>>callbacks;
volatile sig_atomic_t quitting=0;void quit_signal(int){quitting=1;}
void paint();
void message(const std::string&s){notice=s;if(status_label)lv_label_set_text(status_label,s.c_str());}
lv_obj_t*box(lv_obj_t*p,int x,int y,int w,int h,uint32_t color=panel){auto*o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,10,0);return o;}
lv_obj_t*text(lv_obj_t*p,const std::string&s,int x,int y,int w,int h=26,uint32_t color=ink,lv_font_t*f=nullptr){auto*o=lv_label_create(p);lv_label_set_text(o,s.c_str());lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);lv_obj_set_style_text_color(o,lv_color_hex(color),0);if(f)lv_obj_set_style_text_font(o,f,0);return o;}
lv_obj_t*button(const std::string&s,int x,int y,int w,std::function<void()>fn,bool active=false){auto*o=box(lv_screen_active(),x,y,w,44,active?teal:panel);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_style_bg_color(o,lv_color_hex(0x346b72),LV_STATE_PRESSED);callbacks.push_back(std::move(fn));lv_obj_add_event_cb(o,[](lv_event_t*e){auto fn=*static_cast<std::function<void()>*>(lv_event_get_user_data(e));try{fn();}catch(const std::exception&x){message(x.what());}},LV_EVENT_CLICKED,&callbacks.back());auto*l=text(o,s,5,10,w-10,25,active?0x102b31:ink);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);return o;}
void paint(){
    auto*r=lv_screen_active();lv_obj_clean(r);callbacks.clear();status_label=usb_label=nullptr;
    lv_obj_remove_flag(r,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(r,lv_color_hex(bg),0);lv_obj_set_style_bg_opa(r,LV_OPA_COVER,0);lv_obj_set_style_text_font(r,font?font:LV_FONT_DEFAULT,0);
    text(r,"HID 键鼠",14,12,220,34,ink,title);text(r,"USB KEYBOARD / MOUSE",230,19,285,24,muted,small);
    auto*b=button("连接 USB",522,5,264,[]{if(usb.active()){usb.release();usb.stop();modifiers=0;message("正在恢复 ADB / MTP…");}else{usb.start(c1::root()+"/hidpilot/c1max-hidpilot-usb",c1::data()+"/hidpilot/usb.log");message("USB 正在重新识别，ADB 会短暂重连…");}});usb_label=lv_obj_get_child(b,0);
        auto*pad=box(r,12,56,488,246,0x142732);text(pad,"触控板",18,16,440,30,teal,title);text(pad,"滑动移动鼠标 · 轻点左键\n实体键盘输入到电脑",18,66,440,65,muted,font);
        lv_obj_add_flag(pad,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(pad,[](lv_event_t*e){static lv_point_t last{};static int distance=0;lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);auto code=lv_event_get_code(e);
            if(code==LV_EVENT_PRESSED){last=p;distance=0;}else if(code==LV_EVENT_PRESSING&&usb.ready()&&usb.idle()){int dx=p.x-last.x,dy=p.y-last.y;distance+=std::abs(dx)+std::abs(dy);last=p;if(dx||dy)try{usb.send(relative(dx*2,dy*2));}catch(const std::exception&x){message(x.what());}}
            else if(code==LV_EVENT_CLICKED&&distance<8)try{usb.click();}catch(const std::exception&x){message(x.what());}
        },LV_EVENT_ALL,nullptr);
        button("左键",516,57,130,[]{usb.click();});button("右键",654,57,132,[]{usb.click(2);});
        button("向上滚动",516,109,130,[]{usb.send(relative(0,0,0,3));});button("向下滚动",654,109,132,[]{usb.send(relative(0,0,0,-3));});
        const char*mods[]={"Ctrl","Shift","Alt","Cmd"};for(int i=0;i<4;i++)button(mods[i],516+i*69,161,62,[i]{modifiers^=1<<i;paint();},modifiers&(1<<i));
        const char*keys[]={"Esc","Tab","←","→"};const uint8_t codes[]={41,43,80,79};for(int i=0;i<4;i++)button(keys[i],516+i*69,213,62,[i,codes]{usb.key(codes[i],modifiers);});
        button("释放按键",516,265,130,[]{modifiers=0;usb.release();paint();});button("停止 USB",654,265,132,[]{modifiers=0;usb.release();usb.stop();});status_label=text(r,notice,14,312,770,23,muted,small);
}
void key(uint32_t k){
    if(k==screen::KEY_HOME){screen::quit=true;return;}
    if(k==screen::KEY_EXIT){modifiers=0;usb.release();message("已释放全部按键");paint();return;}
    if(k==screen::KEY_MODE){message(screen::caps_lock()?"ABC 大写":"abc 小写 · Shift 符号");return;}
    if(k==LV_KEY_ENTER)usb.key(40,modifiers);else if(k==LV_KEY_BACKSPACE)usb.key(42,modifiers);else if(k>=32&&k<127){auto a=ascii(k);usb.key(a[0],a[1]|modifiers);}
}
}
int main(){
    signal(SIGINT,quit_signal);signal(SIGTERM,quit_signal);signal(SIGPIPE,SIG_IGN);umask(0077);
    std::filesystem::create_directories(c1::data()+"/hidpilot");if(!screen::open())return 1;
    auto fp="A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf";font=lv_tiny_ttf_create_file(fp.c_str(),18);small=lv_tiny_ttf_create_file(fp.c_str(),16);title=lv_tiny_ttf_create_file(fp.c_str(),24);for(auto*f:{font,small,title})if(f)f->fallback=&lv_font_montserrat_18;
    c1_reset_idle();uint32_t idle_at=screen::tick(),state_at=0;bool was_ready=false;paint();
    while(!screen::quit&&!quitting){lv_timer_handler();auto now=screen::tick();usb.poll(now);
        if(usb.active()&&now-idle_at>=5000){c1_reset_idle();idle_at=now;}
        for(uint32_t k;(k=screen::take_key());)try{key(k);}catch(const std::exception&e){message(e.what());}
        if(!usb.error.empty()){message(usb.error);usb.error.clear();}
        if(now-state_at>200){state_at=now;bool ready=usb.ready();if(ready&&!was_ready)message("USB 已连接 · 实体键盘输入到电脑");was_ready=ready;lv_label_set_text(usb_label,ready?"USB 已连接 · ADB 保留":usb.active()?"USB 重新识别中…":"连接 USB");}usleep(5000);
    }
    usb.release();usb.stop();c1_reset_idle();lv_obj_clean(lv_screen_active());lv_obj_set_style_text_font(lv_screen_active(),LV_FONT_DEFAULT,0);for(auto*f:{font,small,title})if(f)lv_tiny_ttf_destroy(f);screen::close();return 0;
}
