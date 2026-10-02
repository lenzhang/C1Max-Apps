// SPDX-License-Identifier: GPL-3.0-only
#include "engine.hpp"
#include "qr.hpp"
#include "scanner.hpp"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "display.hpp"
#include "net.hpp"
#include "lv_tiny_ttf.h"
#include <lvgl.h>
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr uint32_t bg=0x111b24, surface=0x1b2a35, raised=0x263945, ink=0xe6eee9, muted=0x9badae, accent=0x9bddbd, amber=0xf0c685;
constexpr uint32_t nav_profile=0x20001,nav_add=0x20002,nav_requests=0x20003,nav_scan=0x20004,nav_zoom1=0x20010,nav_zoom15=0x20011,nav_zoom2=0x20012;
lv_font_t*font=nullptr;
std::unique_ptr<chat::Engine>engine;
chat::Snapshot view;
enum class Page { Friends, Chat, Add, Profile, Rename, Requests, Delete, Scan };
Page page=Page::Friends,scan_origin=Page::Friends;
std::unique_ptr<chat::Scanner>scanner;
chat::ScanView scan_view;
chat::QrImage own_qr;
std::string qr_id;
lv_image_dsc_t qr_descriptor{},scan_descriptor{};
lv_obj_t *scan_image=nullptr,*scan_status=nullptr,*scan_focus=nullptr;
bool scanned=false;

uint32_t selected=UINT32_MAX;
size_t request_index=0;
unsigned history_offset=0;
std::map<uint32_t,std::string>drafts;
std::string edit,notice;
uint64_t token=0,pending=0;
uint64_t dismissed_error=0;
chat::Action pending_action=chat::Action::Read;
volatile sig_atomic_t stopped=0;
void stop_signal(int){stopped=1;}
void paint();
void key(uint32_t);
void label(lv_obj_t*p,const std::string&t,int x,int y,int w,int h,uint32_t color=ink){
    auto*o=lv_label_create(p);lv_label_set_text(o,t.c_str());lv_label_set_long_mode(o,LV_LABEL_LONG_MODE_WRAP);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_obj_set_style_text_font(o,font?font:LV_FONT_DEFAULT,0);
}
lv_obj_t*box(int x,int y,int w,int h,uint32_t color=surface){auto*o=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,12,0);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;}
void button(const char*t,int x,int y,int w,uint32_t k,bool active=false){
    auto*b=box(x,y,w,34,active?accent:raised);lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);label(b,t,10,6,w-18,23,active?bg:ink);
    lv_obj_add_event_cb(b,[](lv_event_t*e){key(uint32_t(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));},LV_EVENT_CLICKED,reinterpret_cast<void*>(uintptr_t(k)));
}
const chat::Friend*friend_now(){for(auto&f:view.friends)if(f.number==selected)return &f;return nullptr;}
std::string tail(const std::string&s,size_t bytes){if(s.size()<=bytes)return s;size_t at=s.size()-bytes;while(at<s.size()&&(static_cast<unsigned char>(s[at])&0xc0)==0x80)at++;return "…"+s.substr(at);}
void erase_utf8(std::string&s){if(s.empty())return;size_t n=s.size()-1;while(n>0&&(static_cast<unsigned char>(s[n])&0xc0)==0x80)n--;s.erase(n);}
void submit(chat::Command c){if(pending)return;c.token=++token;pending=c.token;pending_action=c.action;if(!engine->submit(c)){pending=0;notice="操作队列繁忙，请稍后再试";}}
void open_chat(uint32_t n){if(pending)return;selected=n;page=Page::Chat;history_offset=0;engine->submit({chat::Action::Read,n});view=engine->snapshot(selected);paint();}
void image_source(lv_obj_t*o,lv_image_dsc_t&d,const std::vector<uint32_t>&pixels,unsigned w,unsigned h){
    lv_image_cache_drop(&d);d={};d.header.magic=LV_IMAGE_HEADER_MAGIC;d.header.cf=LV_COLOR_FORMAT_ARGB8888;d.header.w=w;d.header.h=h;d.header.stride=w*4;d.data_size=pixels.size()*4;d.data=reinterpret_cast<const uint8_t*>(pixels.data());lv_image_set_src(o,&d);lv_obj_invalidate(o);
}
void stop_scan(){scanner.reset();scan_image=nullptr;scan_status=nullptr;scan_focus=nullptr;}
void start_scan(){
    if(!view.ready||view.id.empty()){notice="身份正在加载，请稍后再试";paint();return;}
    if(page==Page::Scan)return;
    scan_origin=page;scan_view={};scan_view.pixels.assign(chat::ScanView::width*chat::ScanView::height,0xff111b24);
    scanner=std::make_unique<chat::Scanner>(view.id);page=Page::Scan;paint();
}
void sidebar(){
    box(12,48,210,252);
    label(lv_screen_active(),"好友  "+std::to_string(view.friends.size())+" / 32",26,59,183,24,muted);
    size_t index=0;for(size_t i=0;i<view.friends.size();i++)if(view.friends[i].number==selected)index=i;
    size_t start=index>=4?index-3:0;
    for(size_t i=start;i<view.friends.size()&&i<start+4;i++){
        auto&f=view.friends[i];int y=91+int(i-start)*41;auto*b=box(20,y,194,38,f.number==selected?raised:surface);lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);
        label(b,(f.online?"● ":"○ ")+f.name,8,7,170,24,f.online?accent:muted);
        if(f.unread)label(b,"+"+std::to_string(f.unread),145,7,46,24,amber);
        lv_obj_add_event_cb(b,[](lv_event_t*e){open_chat(uint32_t(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));},LV_EVENT_CLICKED,reinterpret_cast<void*>(uintptr_t(f.number)));
    }
    if(view.friends.empty())label(lv_screen_active(),"还没有好友\n添加对方的 Tox ID\n即可发送好友请求",28,103,176,88,muted);
    button("A 添加",24,257,84,nav_add);button("R 请求",116,257,94,nav_requests);
}
void paint(){
    auto*r=lv_screen_active();lv_obj_clean(r);scan_image=nullptr;scan_status=nullptr;scan_focus=nullptr;lv_obj_remove_flag(r,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(r,lv_color_hex(bg),0);lv_obj_set_style_text_font(r,font?font:LV_FONT_DEFAULT,0);
    label(r,"TOX",20,9,66,28,accent);label(r,view.name,90,10,243,24);
    label(r,view.status,350,10,250,24,view.online?accent:muted);
    button("我的 ID",668,4,118,nav_profile);
    std::string footer;
    if(page==Page::Friends||page==Page::Chat){
        sidebar();box(234,48,554,252);
        auto*f=friend_now();
        if(page==Page::Friends||!f){
            label(r,"与好友直接对话",256,70,490,28,accent);
            label(r,"添加 Tox ID，或接受收到的好友请求。\n使用实体键盘输入，回车发送文字。\n双方在线时才能投递消息。",256,117,490,95);
            label(r,"好友请求  "+std::to_string(view.requests.size()),256,220,240,25,muted);button("扫码添加",594,243,172,nav_scan);
            footer="W/S 选好友  ·  回车聊天  ·  I 我的 ID  ·  电源返回菜单";
        }else{
            label(r,f->name,250,59,330,25,accent);label(r,f->online?"在线":"离线",703,59,70,25,f->online?accent:muted);
            button("↑",594,52,44,LV_KEY_UP);button("↓",645,52,44,LV_KEY_DOWN);
            // A bounded, scrollable LVGL history avoids allocating a widget for
            // every stored message. At most six messages are rendered at once.
            auto*history=box(245,88,532,154,surface);lv_obj_add_flag(history,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(history,LV_DIR_VER);
            size_t end=view.messages.size()>history_offset?view.messages.size()-history_offset:0;
            size_t begin=end>6?end-6:0;int y=4;
            if(!end)label(history,"还没有消息，输入第一句话吧。",10,40,506,50,muted);
            for(size_t i=begin;i<end;i++){
                auto&m=view.messages[i];std::string prefix=m.mine?"我":"好友";
                if(m.mine)prefix+=m.state=="delivered"?" · 已送达":m.state=="sent"?" · 已发出":" · 未确认";
                auto*o=lv_label_create(history);lv_label_set_text(o,(prefix+"\n"+m.text).c_str());lv_label_set_long_mode(o,LV_LABEL_LONG_MODE_WRAP);lv_obj_set_pos(o,10,y);lv_obj_set_width(o,505);lv_obj_set_style_text_font(o,font?font:LV_FONT_DEFAULT,0);lv_obj_set_style_text_color(o,lv_color_hex(m.mine?accent:ink),0);lv_obj_update_layout(o);y+=lv_obj_get_height(o)+14;
            }
            lv_obj_update_layout(history);if(!history_offset)lv_obj_scroll_to_y(history,lv_obj_get_scroll_bottom(history),LV_ANIM_OFF);
            box(245,251,428,39,raised);label(r,drafts[selected].empty()?"输入文字…":tail(drafts[selected],92)+"_",257,259,401,26,drafts[selected].empty()?muted:ink);button(pending?"发送中":"发送",684,252,92,LV_KEY_ENTER,true);
            footer="回车发送  ·  滑动查看历史  ·  返回键选好友  ·  电源返回菜单";
        }
    }else if(page==Page::Profile){
        box(12,48,776,252);
        if(qr_id!=view.id){lv_image_cache_drop(&qr_descriptor);own_qr=chat::make_tox_qr(view.id);qr_id=view.id;}
        if(own_qr.size){auto*o=lv_image_create(r);lv_obj_set_pos(o,22+(234-own_qr.size)/2,57+(234-own_qr.size)/2);image_source(o,qr_descriptor,own_qr.pixels,own_qr.size,own_qr.size);}
        else label(r,"正在生成二维码…",35,144,216,55,muted);
        label(r,"我的 Tox ID",278,61,472,27,accent);
        std::string id;for(size_t i=0;i<view.id.size();i+=26)id+=view.id.substr(i,26)+"\n";
        label(r,id,278,103,485,82);
        label(r,"让好友扫描左侧二维码添加你。\n仅包含公开 ID，不包含身份私钥。",278,195,485,49,muted);
        button("N 修改昵称",278,255,148,'n');button("扫码添加",440,255,148,nav_scan);button("返回",659,255,113,screen::KEY_EXIT);
        footer="I 我的 ID  ·  实体拍摄键扫描对方  ·  返回键回到好友";
    }else if(page==Page::Scan){
        box(12,48,776,252);scan_image=lv_image_create(r);lv_obj_set_pos(scan_image,20,56);image_source(scan_image,scan_descriptor,scan_view.pixels,chat::ScanView::width,chat::ScanView::height);
        label(r,"扫描好友二维码",279,62,341,28,accent);button("取消",660,56,111,screen::KEY_EXIT);
        button("1×",279,103,85,nav_zoom1,scan_view.zoom==100);button("1.5×",375,103,85,nav_zoom15,scan_view.zoom==150);button("2×",471,103,85,nav_zoom2,scan_view.zoom==200);
        label(r,scan_view.capture_width>=1024?"高清识别":"",578,109,185,25,muted);
        button("F 自动对焦",279,151,166,'f');button("W 调焦 −",457,151,151,'w');button("S 调焦 ＋",619,151,151,'s');
        scan_focus=lv_label_create(r);lv_obj_set_pos(scan_focus,279,198);lv_obj_set_size(scan_focus,490,49);lv_obj_set_style_text_color(scan_focus,lv_color_hex(muted),0);lv_label_set_text(scan_focus,scan_view.focus_status.c_str());
        scan_status=lv_label_create(r);lv_obj_set_pos(scan_status,279,249);lv_obj_set_size(scan_status,490,48);lv_obj_set_style_text_color(scan_status,lv_color_hex(scan_view.failed?amber:accent),0);lv_label_set_text(scan_status,scan_view.status.c_str());
        footer="Z 缩放  ·  F / 拍摄键对焦  ·  W/S 微调  ·  返回取消";
    }else if(page==Page::Add||page==Page::Rename){
        box(12,48,776,252);label(r,page==Page::Add?(scanned?"确认扫描到的好友":"添加好友"):"修改昵称",30,63,680,26,accent);
        label(r,page==Page::Add?(scanned?"请核对对方 ID；按回车才发送好友请求。":"输入完整 76 位 Tox ID，或按实体拍摄键扫描。"):"输入昵称，确认后保存。",30,106,730,28,muted);
        box(26,151,746,84,raised);std::string shown=edit;
        if(page==Page::Add&&shown.size()>38)shown.insert(38,"\n");label(r,shown+"_",40,164,718,60);
        button(pending?"处理中":"回车确认",30,254,150,LV_KEY_ENTER,true);button("取消",661,254,111,screen::KEY_EXIT);if(page==Page::Add)button("扫码添加",194,254,149,nav_scan);
        footer=page==Page::Add?std::to_string(edit.size())+" / 76  ·  Shift + Q–P 输入数字  ·  退格删除":"双击 Shift 切换大写  ·  退格删除  ·  返回取消";
    }else if(page==Page::Requests){
        box(12,48,776,252);label(r,"好友请求  "+std::to_string(view.requests.size()),30,63,700,26,accent);
        if(view.requests.empty())label(r,"没有待处理请求。\n新请求会出现在这里，接受后才能聊天。",30,119,730,85,muted);
        else{request_index=std::min(request_index,view.requests.size()-1);auto&q=view.requests[request_index];label(r,q.key.substr(0,32)+"\n"+q.key.substr(32),30,102,730,57);label(r,q.message,30,170,730,62);button("回车接受",30,251,147,LV_KEY_ENTER,true);button("退格拒绝",189,251,147,LV_KEY_BACKSPACE);label(r,std::to_string(request_index+1)+" / "+std::to_string(view.requests.size()),664,257,96,28,muted);}
        footer="W/S 切换请求  ·  接受后加入好友  ·  返回键回到好友";
    }else{
        box(12,48,776,252);label(r,"删除好友？",30,68,730,30,amber);auto*f=friend_now();label(r,(f?f->name:"")+"\n将同时删除本机保存的对话记录。",30,124,724,91);button("确认删除",30,249,147,LV_KEY_ENTER);button("取消",191,249,113,screen::KEY_EXIT);footer="回车确认  ·  返回键取消";
    }
    bool show_error=!view.error.empty()&&(!view.ready||view.error_count!=dismissed_error);
    if(!notice.empty())footer=notice;else if(show_error)footer=view.error;
    label(r,footer,18,310,770,24,(!notice.empty()||show_error)?amber:muted);
}
void key(uint32_t k){
    dismissed_error=view.error_count;
    if(k==screen::KEY_HOME){screen::quit=true;return;}
    if(k==screen::KEY_MODE)return;
    if(k==screen::KEY_EXIT){if(pending)return;if(page==Page::Scan){stop_scan();page=scan_origin;}else page=Page::Friends;notice.clear();paint();return;}
    if(pending)return;
    notice.clear();
    if(k==nav_scan||(k==screen::KEY_SYMBOL&&(page==Page::Friends||page==Page::Add||page==Page::Profile))){start_scan();return;}
    if(page==Page::Scan&&scanner){
        if(k==nav_zoom1||k==nav_zoom15||k==nav_zoom2||k=='z'||k=='Z'){
            unsigned zoom=k==nav_zoom1?100:k==nav_zoom15?150:k==nav_zoom2?200:scan_view.zoom==100?150:scan_view.zoom==150?200:100;
            scanner->set_zoom(zoom);scan_view.zoom=zoom;
        }else if(k=='f'||k=='F'||k==screen::KEY_SYMBOL)scanner->autofocus();
        else if(k=='w'||k=='W')scanner->adjust_focus(-1);
        else if(k=='s'||k=='S')scanner->adjust_focus(1);
        else if(k!=nav_profile&&k!=nav_add&&k!=nav_requests)return;
        if(k!=nav_profile&&k!=nav_add&&k!=nav_requests){paint();return;}
    }
    if(k==screen::KEY_SYMBOL)return;
    if(k==nav_profile||k==nav_add||k==nav_requests ){stop_scan();page=k==nav_profile?Page::Profile:k==nav_add?Page::Add:Page::Requests;edit.clear();scanned=false;paint();return;}
    if(page==Page::Scan)return;
    if(page==Page::Add||page==Page::Rename){
        if(k==LV_KEY_BACKSPACE){erase_utf8(edit);scanned=false;}
        else if(k==LV_KEY_ENTER){
            if(page==Page::Add){std::string id,error;if(chat::parse_tox_qr(edit,id,error,view.id)){edit=id;submit({chat::Action::Add,0,edit});}else notice=error;}
            else submit({chat::Action::Rename,0,edit});
        }
        else if(k>=32&&k<127){if(page==Page::Rename&&edit.size()<64)edit+=char(k);else if(page==Page::Add&&edit.size()<76&&((k>='0'&&k<='9')||(k>='a'&&k<='f')||(k>='A'&&k<='F')))edit+=char(k);}
    }else if(page==Page::Chat){
        if(k==LV_KEY_BACKSPACE)erase_utf8(drafts[selected]);
        else if(k==LV_KEY_ENTER){if(!drafts[selected].empty())submit({chat::Action::Send,selected,drafts[selected]});}
        else if(k==LV_KEY_UP)history_offset=std::min<unsigned>(view.messages.size()>0?view.messages.size()-1:0,history_offset+3);
        else if(k==LV_KEY_DOWN)history_offset=history_offset>3?history_offset-3:0;
        else if(k>=32&&k<127&&drafts[selected].size()<1024)drafts[selected]+=char(k);
    }else if(page==Page::Delete){if(k==LV_KEY_ENTER)submit({chat::Action::Delete,selected});}
    else if(page==Page::Profile){if(k=='n'||k=='N'){page=Page::Rename;edit=view.name;}}
    else if(page==Page::Requests){
        if(!view.requests.empty()){
            if(k=='w'||k=='W'||k==LV_KEY_UP)request_index=(request_index+view.requests.size()-1)%view.requests.size();
            else if(k=='s'||k=='S'||k==LV_KEY_DOWN)request_index=(request_index+1)%view.requests.size();
            else if(k==LV_KEY_ENTER||k==LV_KEY_BACKSPACE)submit({k==LV_KEY_ENTER?chat::Action::Accept:chat::Action::Reject,0,view.requests[request_index].key});
        }
    }else{
        if(k=='a'||k=='A'){page=Page::Add;edit.clear();scanned=false;}
        else if(k=='i'||k=='I')page=Page::Profile;
        else if(k=='r'||k=='R')page=Page::Requests;
        else if(!view.friends.empty()){
            auto it=std::find_if(view.friends.begin(),view.friends.end(),[](const chat::Friend&f){return f.number==selected;});size_t i=it==view.friends.end()?0:it-view.friends.begin();
            if(k=='w'||k=='W'||k==LV_KEY_UP)i=(i+view.friends.size()-1)%view.friends.size();
            if(k=='s'||k=='S'||k==LV_KEY_DOWN)i=(i+1)%view.friends.size();selected=view.friends[i].number;
            if(k==LV_KEY_ENTER){open_chat(selected);return;}
            if(k==LV_KEY_BACKSPACE)page=Page::Delete;
        }
    }paint();
}
}
int main(){
    signal(SIGTERM,stop_signal);signal(SIGINT,stop_signal);
    if(!screen::open())return 1;
    font=lv_tiny_ttf_create_file(("A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf").c_str(),18);
    std::string nodes=c1::root()+"/tox/bootstrap.json";if(access((c1::data()+"/tox/bootstrap.json").c_str(),R_OK)==0)nodes=c1::data()+"/tox/bootstrap.json";
    engine=std::make_unique<chat::Engine>(c1::data()+"/tox",nodes);paint();uint32_t last=0;
    while(!stopped&&!screen::quit){
        lv_timer_handler();for(uint32_t k;(k=screen::take_key());)key(k);
        if(screen::tick()-last>=100){last=screen::tick();auto next=engine->snapshot(selected);bool dirty=next.revision!=view.revision;view=std::move(next);
            if(selected==UINT32_MAX&&!view.friends.empty())selected=view.friends.front().number;
            if(pending&&view.completed==pending){pending=0;dirty=true;if(view.command_ok){
                if(pending_action==chat::Action::Send){drafts[selected].clear();history_offset=0;}
                else if(pending_action==chat::Action::Rename)page=Page::Profile;
                else if(pending_action==chat::Action::Add){page=Page::Friends;notice="好友请求已提交，等待对方接受";}
                else if(pending_action==chat::Action::Delete){drafts.erase(selected);selected=UINT32_MAX;page=Page::Friends;}
            }}
            if(page==Page::Scan&&scanner){
                chat::ScanView next_scan;next_scan.revision=scan_view.revision;
                if(scanner->snapshot(next_scan)){
                    // Invalidate before replacing backing pixels; all LVGL access stays here.
                    if(next_scan.zoom!=scan_view.zoom||next_scan.capture_width!=scan_view.capture_width)dirty=true;
                    lv_image_cache_drop(&scan_descriptor);if(next_scan.pixels.empty())next_scan.pixels=std::move(scan_view.pixels);scan_view=std::move(next_scan);
                    if(!scan_view.id.empty()){
                        std::string id=scan_view.id;stop_scan();
                        bool exists=false;for(auto&f:view.friends)if(f.key==id.substr(0,64))exists=true;
                        page=Page::Add;edit=id;scanned=true;notice=exists?"对方已在好友列表中":"已识别，请核对 ID 后按回车确认";dirty=true;
                    }else if(scan_image&&!scan_view.pixels.empty()){
                        image_source(scan_image,scan_descriptor,scan_view.pixels,chat::ScanView::width,chat::ScanView::height);
                        lv_label_set_text(scan_focus,scan_view.focus_status.c_str());
                        lv_label_set_text(scan_status,scan_view.status.c_str());lv_obj_set_style_text_color(scan_status,lv_color_hex(scan_view.failed?amber:accent),0);
                    }else dirty=true;
                }
            }
            if(dirty)paint();
        }usleep(10000);
    }
    stop_scan();engine.reset();lv_image_cache_drop(&qr_descriptor);lv_image_cache_drop(&scan_descriptor);lv_obj_clean(lv_screen_active());lv_obj_set_style_text_font(lv_screen_active(),LV_FONT_DEFAULT,0);if(font)lv_tiny_ttf_destroy(font);screen::close();return 0;
}
