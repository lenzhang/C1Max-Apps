#include "store.hpp"
#include "display.hpp"
#include "lv_tiny_ttf.h"
#include <lvgl.h>
#include <future>
#include <mutex>
#include <csignal>
#include <unistd.h>
namespace {
constexpr uint32_t bg=0x111b24,surface=0x1c2b36,accent=0x9bddbd,ink=0xe6eee9,muted=0x9badae;
std::vector<store::App>apps;std::map<std::string,store::Local>locals;size_t selected=0;bool only_installed=false,detail=false,busy=false;int confirm=0;
lv_font_t*font=nullptr;std::string notice="选择需要的应用，可单独安装或更新";std::future<std::string>job;std::atomic<bool>cancel{false};std::atomic<unsigned>progress{0};std::mutex status_lock;std::string work_status;
volatile sig_atomic_t stopped=0;void stop(int){stopped=1;}
void paint();void action(uint32_t);
void text(const std::string&s,int x,int y,int w,int h,uint32_t color=ink){auto*o=lv_label_create(lv_screen_active());lv_label_set_text(o,s.c_str());lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_label_set_long_mode(o,LV_LABEL_LONG_MODE_WRAP);lv_obj_set_style_text_color(o,lv_color_hex(color),0);}
void button(const std::string&s,int x,int y,int w,uint32_t code,bool active=false){auto*b=lv_button_create(lv_screen_active());lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,38);lv_obj_set_style_bg_color(b,lv_color_hex(active?accent:surface),0);lv_obj_set_style_radius(b,9,0);lv_obj_set_style_shadow_width(b,0,0);auto*l=lv_label_create(b);lv_label_set_text(l,s.c_str());lv_obj_set_width(l,w-16);lv_label_set_long_mode(l,LV_LABEL_LONG_MODE_DOTS);lv_obj_center(l);lv_obj_set_style_text_color(l,lv_color_hex(active?bg:ink),0);lv_obj_add_event_cb(b,[](lv_event_t*e){action(uintptr_t(lv_event_get_user_data(e)));},LV_EVENT_CLICKED,reinterpret_cast<void*>(uintptr_t(code)));}
std::vector<size_t>visible(){std::vector<size_t>v;for(size_t i=0;i<apps.size();i++)if(!only_installed||locals[apps[i].id].installed)v.push_back(i);return v;}
void merge_remote(std::vector<store::App>remote){for(auto&a:remote){auto it=std::find_if(apps.begin(),apps.end(),[&](auto&b){return a.id==b.id;});if(it==apps.end())apps.push_back(a);else *it=a;}}
void start(const std::string&status,std::function<std::string()>fn){if(busy)return;busy=true;cancel=false;progress=0;{std::lock_guard<std::mutex>g(status_lock);work_status=status;}job=std::async(std::launch::async,[fn]{try{return fn();}catch(const std::exception&e){return std::string(e.what());}});paint();}
void refresh(){start("正在读取应用目录…",[]{auto remote=store::fetch_catalog(); // Publish on the UI thread after completion via this separate result.
    // The UI does not inspect apps while busy, but completion owns the final merge.
    Json j=Json::array();for(auto&a:remote)j.push_back({{"id",a.id},{"title",a.title},{"description",a.description},{"version",a.version},{"revision",a.revision},{"url",a.url},{"sha256",a.sha256},{"size",a.size},{"unpacked",a.unpacked}});
    return "CATALOG:"+j.dump();});}
void paint(){
    auto*r=lv_screen_active();lv_obj_clean(r);lv_obj_remove_flag(r,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(r,lv_color_hex(bg),0);lv_obj_set_style_text_font(r,font?font:LV_FONT_DEFAULT,0);
    text("应用商店",20,12,210,32,accent);
    if(busy){std::string s;{std::lock_guard<std::mutex>g(status_lock);s=work_status;}text(s,42,104,720,64);auto*bar=lv_bar_create(r);lv_obj_set_pos(bar,42,187);lv_obj_set_size(bar,716,14);lv_bar_set_value(bar,progress,LV_ANIM_OFF);button("取消",609,240,150,'x');text("只下载当前选中的应用；完成校验后才切换版本",24,307,750,25,muted);return;}
    auto list=visible();if(selected>=list.size())selected=0;
    button(only_installed?"已安装":"全部应用",480,8,142,'t',only_installed);button("刷新目录",638,8,142,'r');
    if(detail&&!list.empty()){
        auto&a=apps[list[selected]];auto l=locals[a.id];text(a.title,25,64,720,32,accent);text(a.description,25,103,720,48);text(store::compare(l,a)+(l.installed?"  本机 "+l.version:"")+(a.version.empty()?"":"  远端 "+a.version),25,155,750,29,muted);
        text(a.url.empty()?"刷新目录后可下载安装":("下载 "+std::to_string(a.size/1024)+" KB · 安装后 "+std::to_string(a.unpacked/1024)+" KB"),25,188,735,26,muted);
        if(confirm){text(confirm==1?"确认安装此构建？同版本构建不能仅凭哈希判断新旧。":"确认卸载？首页入口移除，个人数据和回退版本保留。",25,226,735,35);button("确认",430,265,162,'y',true);button("取消",610,265,162,'n');}
        else{button(!l.installed?"安装":store::compare(l,a)=="可更新"?"更新此应用":"安装此构建",25,246,216,'i',true);if(l.installed&&a.id!="appstore"){button(l.visible?"首页隐藏":"显示到首页",257,246,218,'h');button("卸载应用",491,246,150,'u');}button("返回",657,246,116,screen::KEY_EXIT);}
    }else{
        size_t first=selected/4*4;int y=62;
        for(size_t i=first;i<list.size()&&i<first+4;i++){auto&a=apps[list[i]];auto&l=locals[a.id];std::string state=store::compare(l,a);if(l.installed&&!l.visible)state+=" · 已隐藏";button(a.title+"   "+state,20,y,760,0x22000+i,i==selected);y+=50;}
        if(list.empty())text("没有已安装的可选应用",30,114,720,60,muted);
        button("撤销上次变更",20,265,208,'z');text(list.empty()?"":std::to_string(selected+1)+" / "+std::to_string(list.size()),692,270,84,28,muted);
    }
    text(notice,20,310,762,24,muted);
}
void action(uint32_t k){
    if(k==screen::KEY_HOME){cancel=true;screen::quit=true;return;}
    if(busy){if(k=='x'||k==screen::KEY_EXIT){cancel=true;c1::cancel_requests();}return;}
    if(k==screen::KEY_MODE)return;
    auto list=visible();if(selected>=list.size())selected=0;
    if(k==screen::KEY_EXIT){if(confirm)confirm=0;else detail=false;paint();return;}
    if(k>=0x22000&&k<0x22100){selected=k-0x22000;detail=true;confirm=0;paint();return;}
    if(confirm){if(k=='n')confirm=0;else if(k=='y'||k==LV_KEY_ENTER){auto a=apps[list[selected]];int c=confirm;confirm=0;start(c==1?"准备安装…":"正在卸载…",[a,c]{if(c==1)store::install(a,cancel,[](unsigned p,const std::string&s){progress=p;std::lock_guard<std::mutex>g(status_lock);work_status=s;});else store::uninstall(a.id);return c==1?"安装完成，返回首页即可使用":"已卸载；个人数据保留";});return;}paint();return;}
    if(k=='r'||k=='R'){c1::reset_requests();refresh();return;}if(k=='t'||k=='T'){only_installed=!only_installed;selected=0;detail=false;}
    else if(!detail){if(!list.empty()){if(k=='w'||k==LV_KEY_UP)selected=(selected+list.size()-1)%list.size();else if(k=='s'||k==LV_KEY_DOWN)selected=(selected+1)%list.size();else if(k==LV_KEY_ENTER)detail=true;}if(k=='z')start("撤销上次安装或首页变更…",[]{store::rollback();return "已恢复上次状态";});}
    else if(!list.empty()){auto a=apps[list[selected]];auto l=locals[a.id];if(k=='i'||k==LV_KEY_ENTER){if(a.url.empty())notice="请先刷新目录，获取已发布的安装包";else if(store::compare(l,a)=="本地版本较新")notice="本地版本较新，不降级";else confirm=1;}else if(k=='u'&&l.installed&&a.id!="appstore")confirm=2;else if(k=='h'&&l.installed&&a.id!="appstore"){start("更新首页…",[a,l]{store::set_visible(a.id,!l.visible);return l.visible?"已从首页隐藏；不会自动下载更新":"已显示到首页";});return;}}
    paint();
}
}
int main(){signal(SIGTERM,stop);signal(SIGINT,stop);if(!screen::open())return 1;font=lv_tiny_ttf_create_file(("A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf").c_str(),18);apps=store::builtin();try{locals=store::load_state();}catch(const std::exception&e){notice=e.what();}paint();uint32_t last=0;
    while(!screen::quit&&!stopped){lv_timer_handler();for(uint32_t k;(k=screen::take_key());)action(k);if(busy&&job.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){notice=job.get();busy=false;if(notice.rfind("CATALOG:",0)==0){auto j=Json::parse(notice.substr(8));std::vector<store::App>a;for(auto&e:j)a.push_back({e["id"],e["title"],e["description"],e["version"],e["revision"],e["url"],e["sha256"],e["size"],e["unpacked"]});merge_remote(std::move(a));notice="目录已更新；只下载你选择的应用";}try{locals=store::load_state();}catch(const std::exception&e){notice=e.what();}paint();}else if(busy&&screen::tick()-last>300){last=screen::tick();paint();}usleep(10000);}
    cancel=true;c1::cancel_requests();if(busy)job.wait();lv_obj_clean(lv_screen_active());lv_obj_set_style_text_font(lv_screen_active(),LV_FONT_DEFAULT,0);if(font)lv_tiny_ttf_destroy(font);screen::close();return 0;
}
