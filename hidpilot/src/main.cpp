#include "agent.hpp"
#include "voice.hpp"
#include "usb.hpp"
#include "frame_image.hpp"
#include "focus.hpp"
#include "process.hpp"
#include "display.hpp"
#include "idle_reset.h"
#include "lv_tiny_ttf.h"
#include <algorithm>
#include <csignal>
#include <filesystem>
#include <future>
#include <deque>
#include <optional>
#include <unistd.h>
#include <sys/stat.h>
using namespace hidpilot;
namespace {
constexpr uint32_t bg=0x101922,panel=0x1b2a36,ink=0xeef5fa,muted=0xa7bac9,teal=0x5eead4,red=0xfa8e88;
constexpr int preview_w=488,preview_h=250;
enum class Page{Agent,Mouse,Voice,Settings};Page page=Page::Agent;int settings_tab=0;
Settings vision,chat,asr,tts;Capture capture;Focus focus;Usb usb;Process recorder,speaker;
Quad quad{};std::vector<Point>corners;bool calibrated=false,calibrating=false,camera_open=false;
bool usb_was_ready=false;
bool busy=false,cancelled=false,running=false,executing=false,recording=false,transcribe_next=false,spoken=true;
int steps=0,remaining=0;uint8_t modifiers=0;uint32_t next_step=0,preview_at=0,started_recording=0,last_state=0;
std::atomic<int> voice_stage{0};int displayed_stage=0;
std::string heard,goal,notice="先校准屏幕，再输入任务",reply;Json actions_history=Json::array(),chat_history=Json::array();
std::optional<Action>pending;std::future<Json>job;std::function<void(Json)>completion;
std::deque<std::function<void()>>callbacks;
lv_font_t*font=nullptr,*small=nullptr,*title=nullptr;
lv_obj_t*status_label=nullptr,*usb_label=nullptr,*image=nullptr,*task_field=nullptr,*focused=nullptr,*fields[3]{},*record_label=nullptr;
std::vector<uint32_t>preview(preview_w*preview_h,0xff0a1015);lv_image_dsc_t descriptor{};
volatile sig_atomic_t quitting=0;void quit_signal(int){quitting=1;}
std::string path(const char*name){return c1::data()+"/hidpilot/"+name;}
void paint();void stop_task();void request_step();void begin_record();void speak(const std::string&);
void message(const std::string&s){notice=s;if(status_label)lv_label_set_text(status_label,s.c_str());}
lv_obj_t*box(lv_obj_t*p,int x,int y,int w,int h,uint32_t color=panel){auto*o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,10,0);return o;}
lv_obj_t*text(lv_obj_t*p,const std::string&s,int x,int y,int w,int h=26,uint32_t color=ink,lv_font_t*f=nullptr){auto*o=lv_label_create(p);lv_label_set_text(o,s.c_str());lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);lv_obj_set_style_text_color(o,lv_color_hex(color),0);if(f)lv_obj_set_style_text_font(o,f,0);return o;}
lv_obj_t*button(const std::string&s,int x,int y,int w,std::function<void()>fn,bool active=false){auto*o=box(lv_screen_active(),x,y,w,44,active?teal:panel);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_style_bg_color(o,lv_color_hex(0x346b72),LV_STATE_PRESSED);callbacks.push_back(std::move(fn));lv_obj_add_event_cb(o,[](lv_event_t*e){auto fn=*static_cast<std::function<void()>*>(lv_event_get_user_data(e));try{fn();}catch(const std::exception&x){message(x.what());}},LV_EVENT_CLICKED,&callbacks.back());auto*l=text(o,s,5,10,w-10,25,active?0x102b31:ink);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);return o;}
lv_obj_t*field(const std::string&value,int x,int y,int w,bool password=false){auto*o=lv_textarea_create(lv_screen_active());lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,44);lv_textarea_set_one_line(o,true);lv_textarea_set_max_length(o,512);lv_textarea_set_password_mode(o,password);lv_textarea_set_text(o,value.c_str());lv_obj_set_style_bg_color(o,lv_color_hex(panel),0);lv_obj_set_style_text_color(o,lv_color_hex(ink),0);lv_obj_set_style_border_color(o,lv_color_hex(teal),LV_STATE_FOCUSED);lv_obj_set_style_pad_all(o,9,0);lv_obj_add_event_cb(o,[](lv_event_t*e){auto*o=(lv_obj_t*)lv_event_get_target(e);if(focused&&focused!=o)lv_obj_remove_state(focused,LV_STATE_FOCUSED);focused=o;lv_obj_add_state(o,LV_STATE_FOCUSED);},LV_EVENT_CLICKED,nullptr);return o;}
Settings&service(){return settings_tab==0?vision:settings_tab==1?chat:settings_tab==2?asr:tts;}
Json config(){auto encode=[](const Settings&s){return Json{{"endpoint",s.endpoint},{"model",s.model},{"token",s.token}};};return {{"vision",encode(vision)},{"chat",encode(chat)},{"asr",encode(asr)},{"tts",encode(tts)},{"spoken",spoken}};}
void save_settings(){if(page==Page::Settings){auto&s=service();s.endpoint=lv_textarea_get_text(fields[0]);s.model=lv_textarea_get_text(fields[1]);s.token=lv_textarea_get_text(fields[2]);if(!s.endpoint.empty())validate_settings(s);}c1::save_private(path("settings.json"),config().dump(2));message("设置已保存到设备");}
void work(const std::string&label,std::function<Json()>fn,std::function<void(Json)>done){if(busy)throw std::runtime_error("请先等待或停止当前任务");busy=true;cancelled=false;voice_stage=displayed_stage=0;c1::reset_requests(95000);message(label);completion=std::move(done);job=std::async(std::launch::async,[fn]{try{return Json{{"ok",true},{"data",fn()}};}catch(const std::exception&e){return Json{{"ok",false},{"error",e.what()}};}});}
void start_usb(){if(usb.active()){stop_task();usb.stop();message("正在恢复 ADB / MTP…");}else{usb.start(c1::root()+"/hidpilot/c1max-hidpilot-usb",path("usb.log"));message("USB 正在重新识别，ADB 会短暂重连…");}}
void set_page(Page next){stop_task();if(task_field&&page==Page::Agent)goal=lv_textarea_get_text(task_field);if(page==Page::Settings)save_settings();if(next!=Page::Agent&&camera_open){capture.close();camera_open=false;}else if(next==Page::Agent&&!camera_open){std::string e;camera_open=capture.open(e);if(!camera_open)message(e);}page=next;paint();}
void update_preview(){
    if(!image||!capture.has_frame())return;
    std::fill(preview.begin(),preview.end(),0xff0a1015);
    if(calibrated&&!calibrating)preview=rectify(capture,quad,preview_w,preview_h);
    else{
        double scale=std::min(double(preview_w)/capture.width(),double(preview_h)/capture.height());int w=capture.width()*scale,h=capture.height()*scale,x0=(preview_w-w)/2,y0=(preview_h-h)/2;
        for(int y=0;y<h;y++)for(int x=0;x<w;x++)preview[(y+y0)*preview_w+x+x0]=capture.pixel(std::min<unsigned>(x/scale,capture.width()-1),std::min<unsigned>(y/scale,capture.height()-1));
        for(auto p:corners){int cx=x0+p.x*(w-1),cy=y0+p.y*(h-1);for(int dy=-6;dy<=6;dy++)for(int dx=-6;dx<=6;dx++){int x=cx+dx,y=cy+dy;if(x>=0&&y>=0&&x<preview_w&&y<preview_h&&dx*dx+dy*dy<=36)preview[y*preview_w+x]=0xff5eead4;}}
    }
    descriptor.header.magic=LV_IMAGE_HEADER_MAGIC;descriptor.header.cf=LV_COLOR_FORMAT_ARGB8888;descriptor.header.w=preview_w;descriptor.header.h=preview_h;descriptor.header.stride=preview_w*4;descriptor.data_size=preview.size()*4;descriptor.data=(uint8_t*)preview.data();lv_image_set_src(image,&descriptor);lv_obj_invalidate(image);
}
void calibrate_tap(lv_event_t*){
    if(!calibrating||!capture.has_frame())return;lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);p.x-=12;p.y-=56;
    double scale=std::min(double(preview_w)/capture.width(),double(preview_h)/capture.height());int w=capture.width()*scale,h=capture.height()*scale,x0=(preview_w-w)/2,y0=(preview_h-h)/2;
    if(p.x<x0||p.x>=x0+w||p.y<y0||p.y>=y0+h)return;corners.push_back({double(p.x-x0)/(w-1),double(p.y-y0)/(h-1)});
    if(corners.size()==4){Quad next;std::copy(corners.begin(),corners.end(),next.begin());try{Homography check(next);quad=next;calibrated=true;calibrating=false;message("校准完成，请保持词典和屏幕位置不变");paint();}catch(const std::exception&e){corners.clear();message(e.what());}}
    else{static const char*names[]={"左上","右上","右下","左下"};message(std::string("请点电脑屏幕的")+names[corners.size()]+"角");}update_preview();
}
void apply(const Action&a){
    if(!usb.ready())throw std::runtime_error("USB 未就绪，动作没有执行");
    if(a.kind=="click"||a.kind=="double_click"||a.kind=="move"){
        usb.send(absolute(std::lround(a.x*32767),std::lround(a.y*32767)));if(a.kind!="move"){usb.click(a.button);if(a.kind=="double_click")usb.click(a.button);}
    }else if(a.kind=="type"){for(unsigned char c:a.text){auto k=ascii(c);usb.key(k[0],k[1]);}}
    else if(a.kind=="key")usb.key(a.key,a.mods);
    else if(a.kind=="scroll")usb.send(relative(0,0,0,a.amount));
    executing=true;next_step=screen::tick()+(a.kind=="wait"?a.ms:750);steps++;remaining--;
    actions_history.push_back({{"action",a.kind},{"summary",a.summary}});while(actions_history.size()>10)actions_history.erase(actions_history.begin());message("第 "+std::to_string(steps)+" 步 · "+describe(a));pending.reset();
}
void request_step(){
    if(busy||!running)return;
    if(!calibrated||!capture.has_frame()||!usb.ready()){stop_task();message("请校准屏幕并连接 USB HID");return;}
    auto frame=jpeg(rectify(capture,quad,768,432),768,432);auto settings=vision;auto task=goal;auto history=actions_history;
    work("观察屏幕，等待模型…",[settings,task,frame,history]{auto a=decide(settings,task,frame,history);Json out={{"kind",a.kind},{"summary",a.summary},{"text",a.text},{"x",a.x},{"y",a.y},{"amount",a.amount},{"ms",a.ms},{"key",a.key},{"mods",a.mods},{"button",a.button}};return out;},[](Json j){
        Action a;a.kind=j.at("kind");a.summary=j.at("summary");a.text=j.at("text");a.x=j.at("x");a.y=j.at("y");a.amount=j.at("amount");a.ms=j.at("ms");a.key=j.at("key");a.mods=j.at("mods");a.button=j.at("button");
        if(a.kind=="done"||a.kind=="ask"){running=false;message(describe(a));reply=describe(a);if(spoken&&!tts.endpoint.empty())speak(reply);return;}
        apply(a);
    });
}
void start_task(int count){
    if(busy||recording)throw std::runtime_error("请先停止当前对话或录音");
    if(task_field)goal=lv_textarea_get_text(task_field);if(goal.empty())throw std::runtime_error("请输入任务，或按语音按钮说话");validate_settings(vision);
    if(!usb.ready())throw std::runtime_error("请先点右上角连接 USB");if(!calibrated)throw std::runtime_error("请先点校准，再依次选择屏幕四角");
    focused=nullptr;if(task_field)lv_obj_remove_state(task_field,LV_STATE_FOCUSED);steps=0;remaining=count;actions_history=Json::array();running=true;request_step();
}
void stop_task(){
    running=executing=false;pending.reset();if(busy){cancelled=true;c1::cancel_requests();}recorder.stop();speaker.stop();recording=transcribe_next=false;
    usb.release();modifiers=0;unlink(path("record.wav").c_str());message("已停止，所有按键已释放");
}
void text_chat();
void play_wav(){speaker.start({"/usr/bin/mplayer","-noconfig","all","-quiet","-noconsolecontrols","-nolirc","-nojoystick","-nomouseinput","-vo","null","-ao","media",path("reply.wav")},path("audio.log"));}
void speak(const std::string&s){if(busy||s.empty()||tts.endpoint.empty())return;auto settings=tts;work("合成语音…",[settings,s]{auto wav=synthesize(settings,s);c1::save_private(path("reply.wav"),wav);return Json{};},[](Json){play_wav();message(reply);});}
void begin_record(){
    if(recording){recorder.finish_recording();transcribe_next=true;message("结束录音，正在准备识别…");return;}
    if(busy||running)throw std::runtime_error("请先停止当前任务");validate_settings(asr);validate_settings(chat);speaker.stop();unlink(path("record.wav").c_str());
    recorder.start({"/usr/bin/arecord","-q","-D","plughw:0,1","-f","S16_LE","-r","16000","-c","1","-t","wav","-d","10",path("record.wav")},path("record.log"));
    recording=transcribe_next=true;started_recording=screen::tick();message("录音中 · 再按语音结束，最长 10 秒");
}
void voice_done(Json out){heard=out.at("heard");reply=out.at("reply");chat_history.push_back({{"role","user"},{"content",out.at("heard")}});chat_history.push_back({{"role","assistant"},{"content",reply}});while(chat_history.size()>6)chat_history.erase(chat_history.begin());
        if(out.contains("task")&&!out["task"].is_null()){goal=out["task"];if(task_field&&page==Page::Agent)lv_textarea_set_text(task_field,goal.c_str());}
        if(page==Page::Voice)paint();message(out.contains("voice_error")?"回答已显示 · "+out["voice_error"].get<std::string>():"回答已显示");if(out.value("audio",false))play_wav();
}
void text_chat(){
    if(busy||recording||running)throw std::runtime_error("请先停止当前任务");
    auto input=std::string(lv_textarea_get_text(task_field));if(input.empty())throw std::runtime_error("请输入文字");
    auto settings=chat,voice=tts;auto history=chat_history;bool read_aloud=spoken;
    work("正在对话…",[settings,voice,history,input,read_aloud]{voice_stage=2;auto out=converse(settings,input,history);out["heard"]=input;
        if(read_aloud&&!voice.endpoint.empty())try{voice_stage=3;auto audio=synthesize(voice,out.at("reply"));c1::save_private(path("reply.wav"),audio);out["audio"]=true;}catch(const std::exception&e){out["voice_error"]=e.what();}return out;
    },voice_done);
}
void finish_voice(){
    recording=transcribe_next=false;std::string wav=c1::read_file(path("record.wav"),700000);unlink(path("record.wav").c_str());auto recognition=asr,conversation=chat,voice=tts;bool read_aloud=spoken;auto history=chat_history;
    work("正在识别语音…",[recognition,conversation,voice,read_aloud,wav,history]{voice_stage=1;auto result=transcribe(recognition,wav);voice_stage=2;auto out=converse(conversation,result,history);out["heard"]=result;
        if(read_aloud&&!voice.endpoint.empty())try{voice_stage=3;auto audio=synthesize(voice,out.at("reply"));c1::save_private(path("reply.wav"),audio);out["audio"]=true;}catch(const std::exception&e){out["voice_error"]=e.what();}return out;
    },voice_done);
}
void paint(){
    auto*r=lv_screen_active();lv_obj_clean(r);callbacks.clear();focused=task_field=status_label=image=record_label=usb_label=nullptr;for(auto&f:fields)f=nullptr;
    lv_obj_remove_flag(r,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(r,lv_color_hex(bg),0);lv_obj_set_style_bg_opa(r,LV_OPA_COVER,0);lv_obj_set_style_text_font(r,font?font:LV_FONT_DEFAULT,0);lv_obj_set_style_text_color(r,lv_color_hex(ink),0);
    text(r,"HIDPilot",14,12,144,34,ink,title);button("智能体",166,5,86,[]{set_page(Page::Agent);},page==Page::Agent);button("键鼠",258,5,74,[]{set_page(Page::Mouse);},page==Page::Mouse);button("语音",338,5,74,[]{set_page(Page::Voice);},page==Page::Voice);button("设置",418,5,74,[]{set_page(Page::Settings);},page==Page::Settings);
    auto*b=button("连接 USB",522,5,264,[]{start_usb();});usb_label=lv_obj_get_child(b,0);
    if(page==Page::Settings){
        static const char*names[]={"视觉","对话","识别","播报"};for(int i=0;i<4;i++)button(names[i],14+i*119,62,111,[i]{if(busy)return;save_settings();settings_tab=i;paint();},settings_tab==i);
        auto&s=service();text(r,"接口地址",16,126,130,26,muted);fields[0]=field(s.endpoint,152,116,634);text(r,"模型名称",16,186,130,26,muted);fields[1]=field(s.model,152,176,634);text(r,"API 密钥",16,246,130,26,muted);fields[2]=field(s.token,152,236,634,true);
        button("保存",654,62,132,[]{save_settings();});button(spoken?"播报：开":"播报：关",498,62,148,[]{spoken=!spoken;save_settings();paint();},spoken);
        status_label=text(r,notice,16,291,770,39,muted,small);return;
    }
    if(page==Page::Voice){
        auto*conversation=box(r,12,57,488,245);lv_obj_add_flag(conversation,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(conversation,LV_DIR_VER);
        std::string transcript=heard.empty()?"按开始录音说话，或在右侧输入文字。\n\n操作电脑的请求会填入任务栏，点「查看任务」后启动。":("你："+heard+"\n\nHIDPilot："+reply);
        auto*l=text(conversation,transcript,16,14,456,1,ink,font);lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);lv_obj_set_height(l,LV_SIZE_CONTENT);
        text(r,"文字对话",516,58,270,25,muted,small);task_field=field("",516,82,270);lv_textarea_set_placeholder_text(task_field,"也可以用实体键盘提问");
        button("发送",516,137,130,[]{text_chat();});button("查看任务",654,137,132,[]{focused=nullptr;task_field=nullptr;set_page(Page::Agent);});
        b=button("开始录音",516,203,270,[]{begin_record();},true);record_label=lv_obj_get_child(b,0);
        button("再读一遍",516,257,130,[]{speak(reply);});button("停止",654,257,132,[]{stop_task();});
        status_label=text(r,notice,14,312,770,23,muted,small);return;
    }
    if(page==Page::Agent){
        box(r,12,56,preview_w,preview_h,0x0a1015);image=lv_image_create(r);lv_obj_set_pos(image,12,56);lv_obj_set_size(image,preview_w,preview_h);lv_obj_add_flag(image,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(image,calibrate_tap,LV_EVENT_CLICKED,nullptr);
        if(calibrating){
            text(r,"屏幕四角校准",516,60,270,30,ink,title);text(r,"依次点左侧画面里的\n左上 → 右上 → 右下 → 左下\n仅框住要控制的主显示器",516,101,270,86,muted,small);
            button("重新选",516,205,130,[]{corners.clear();message("请点屏幕左上角");});button("取消",654,205,132,[]{calibrating=false;corners.clear();paint();});
            button("焦点 −",516,258,130,[]{message(focus.adjust(-32)?"已调整焦点":"对焦不可用");});button("焦点 ＋",654,258,132,[]{message(focus.adjust(32)?"已调整焦点":"对焦不可用");});
        }else{
            text(r,"任务",516,57,270,25,muted,small);task_field=field(goal,516,82,270);
            status_label=text(r,notice,516,136,270,59,ink,small);
            button("单步",516,203,130,[]{start_task(1);});button("运行 10 步",654,203,132,[]{start_task(10);},true);
            button("校准",516,257,80,[]{if(busy)return;if(task_field)goal=lv_textarea_get_text(task_field);calibrating=true;corners.clear();message("请点屏幕左上角");paint();});
            b=button("语音",604,257,84,[]{set_page(Page::Voice);});record_label=lv_obj_get_child(b,0);button("停止",696,257,90,[]{stop_task();});
        }
        if(!status_label)status_label=text(r,notice,14,312,770,23,muted,small);else text(r,"返回键立即停止 · 电源键返回菜单 · Shift 组合输入",14,312,770,23,muted,small);
        update_preview();
    }else{
        auto*pad=box(r,12,56,488,246,0x142732);text(pad,"触控板",18,16,440,30,teal,title);text(pad,"滑动移动鼠标 · 轻点左键\n实体键盘输入到电脑",18,66,440,65,muted,font);
        lv_obj_add_flag(pad,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(pad,[](lv_event_t*e){static lv_point_t last{};static int distance=0;lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);auto code=lv_event_get_code(e);
            if(code==LV_EVENT_PRESSED){last=p;distance=0;}else if(code==LV_EVENT_PRESSING&&usb.ready()&&usb.idle()){int dx=p.x-last.x,dy=p.y-last.y;distance+=std::abs(dx)+std::abs(dy);last=p;if(dx||dy)try{usb.send(relative(dx*2,dy*2));}catch(const std::exception&x){message(x.what());}}
            else if(code==LV_EVENT_CLICKED&&distance<8)try{usb.click();}catch(const std::exception&x){message(x.what());}
        },LV_EVENT_ALL,nullptr);
        button("左键",516,57,130,[]{usb.click();});button("右键",654,57,132,[]{usb.click(2);});
        button("向上滚动",516,109,130,[]{usb.send(relative(0,0,0,3));});button("向下滚动",654,109,132,[]{usb.send(relative(0,0,0,-3));});
        const char*mods[]={"Ctrl","Shift","Alt","Cmd"};for(int i=0;i<4;i++)button(mods[i],516+i*69,161,62,[i]{modifiers^=1<<i;paint();},modifiers&(1<<i));
        const char*keys[]={"Esc","Tab","←","→"};const uint8_t codes[]={41,43,80,79};for(int i=0;i<4;i++)button(keys[i],516+i*69,213,62,[i,codes]{usb.key(codes[i],modifiers);});
        button("释放按键",516,265,130,[]{modifiers=0;usb.release();paint();});button("停止 USB",654,265,132,[]{stop_task();usb.stop();});status_label=text(r,notice,14,312,770,23,muted,small);
    }
}
void key(uint32_t k){
    if(k==screen::KEY_HOME){screen::quit=true;return;}
    if(k==screen::KEY_EXIT){stop_task();if(calibrating){calibrating=false;corners.clear();paint();return;}if(focused){lv_obj_remove_state(focused,LV_STATE_FOCUSED);focused=nullptr;}else if(page!=Page::Agent){set_page(Page::Agent);}return;}
    if(focused&&!busy){if(k==LV_KEY_ENTER){if(focused==task_field&&page==Page::Voice){text_chat();return;}if(focused==task_field){goal=lv_textarea_get_text(task_field);lv_obj_remove_state(focused,LV_STATE_FOCUSED);focused=nullptr;}else save_settings();}else if(k==LV_KEY_BACKSPACE)lv_textarea_delete_char(focused);else if(k>=32&&k<127){char c[2]={char(k),0};lv_textarea_add_text(focused,c);}return;}
    if(k==screen::KEY_MODE){message(screen::caps_lock()?"ABC 大写":"abc 小写 · Shift 符号");return;}
    if(page==Page::Mouse){if(k==LV_KEY_ENTER)usb.key(40,modifiers);else if(k==LV_KEY_BACKSPACE)usb.key(42,modifiers);else if(k>=32&&k<127){auto a=ascii(k);usb.key(a[0],a[1]|modifiers);}return;}
    if((page==Page::Agent||page==Page::Voice)&&k==screen::KEY_SYMBOL){if(page==Page::Agent)set_page(Page::Voice);begin_record();}
}
}
int main(){
    signal(SIGINT,quit_signal);signal(SIGTERM,quit_signal);signal(SIGPIPE,SIG_IGN);umask(0077);
    std::filesystem::create_directories(c1::data()+"/hidpilot");
    try{auto j=Json::parse(c1::read_file(path("settings.json"),8192));auto decode=[&](const char*k,Settings&s){if(j.contains(k)){auto&a=j[k];s={a.value("endpoint",""),a.value("model",""),a.value("token","")};}};decode("vision",vision);decode("chat",chat);decode("asr",asr);decode("tts",tts);spoken=j.value("spoken",true);}catch(...){}
    if(!screen::open())return 1;
    auto fp="A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf";font=lv_tiny_ttf_create_file(fp.c_str(),18);small=lv_tiny_ttf_create_file(fp.c_str(),16);title=lv_tiny_ttf_create_file(fp.c_str(),24);for(auto*f:{font,small,title})if(f)f->fallback=&lv_font_montserrat_18;
    std::string error;camera_open=capture.open(error);if(!camera_open)notice=error;else focus.open();c1_reset_idle();uint32_t idle_at=screen::tick();paint();
    while(!screen::quit&&!quitting){
        lv_timer_handler();uint32_t now=screen::tick();usb.poll(now);
        if((busy||running||recording||speaker.active()||usb.active()||camera_open)&&now-idle_at>=5000){c1_reset_idle();idle_at=now;}
        for(uint32_t k;(k=screen::take_key());)try{key(k);}catch(const std::exception&e){message(e.what());}
        if(camera_open){int n=capture.frame(error);if(n<0){camera_open=false;stop_task();message(error);}else if(n>0&&now-preview_at>150){preview_at=now;update_preview();}}
        if(recording){if(record_label)lv_label_set_text(record_label,("结束 "+std::to_string((now-started_recording)/1000)).c_str());if(recorder.poll())try{if(transcribe_next)finish_voice();}catch(const std::exception&e){recording=transcribe_next=false;message(e.what());}}
        else if(record_label)lv_label_set_text(record_label,page==Page::Voice?"开始录音":"语音");
        if(busy&&voice_stage.load()!=displayed_stage){displayed_stage=voice_stage.load();message(displayed_stage==1?"正在识别语音…":displayed_stage==2?"正在对话…":displayed_stage==3?"正在合成语音…":notice);}
        speaker.poll();
        if(busy&&job.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){auto out=job.get();busy=false;auto done=std::move(completion);if(cancelled){c1::reset_requests();message("已停止");}else if(out.value("ok",false))try{done(out.at("data"));}catch(const std::exception&e){running=executing=false;usb.release();message(e.what());}else{running=executing=false;usb.release();message(out.value("error","请求失败"));}}
        if(!usb.error.empty()){auto e=usb.error;usb.error.clear();stop_task();message(e);}
        if(executing&&usb.idle()&&int32_t(now-next_step)>=0){executing=false;if(remaining>0&&running){next_step=now+350;}else{running=false;message("本轮已执行 "+std::to_string(steps)+" 步 · 可继续单步或运行");}}
        if(running&&!busy&&!executing&&int32_t(now-next_step)>=0)try{request_step();}catch(const std::exception&e){stop_task();message(e.what());}
        if(now-last_state>200){last_state=now;bool connected=usb.ready();if(connected&&!usb_was_ready&&notice.rfind("USB 正在重新识别",0)==0)message("USB 已连接，键鼠和 ADB 可同时使用");usb_was_ready=connected;if(usb_label)lv_label_set_text(usb_label,usb.ready()?"USB 已连接 · ADB 保留":usb.active()?"USB 重新识别中…":"连接 USB");}
        usleep(5000);
    }
    stop_task();usb.stop();c1::cancel_requests();if(job.valid())job.wait();speaker.stop();recorder.stop();focus.close();capture.close();c1_reset_idle();unlink(path("reply.wav").c_str());unlink(path("record.wav").c_str());
    lv_obj_clean(lv_screen_active());lv_obj_set_style_text_font(lv_screen_active(),LV_FONT_DEFAULT,0);for(auto*f:{font,small,title})if(f)lv_tiny_ttf_destroy(f);screen::close();return 0;
}
