#include "../src/agent.hpp"
#include "../src/geometry.hpp"
#include "../src/voice.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <map>
#include <thread>
#include <chrono>
using namespace hidpilot;
template<class F> void rejects(F fn) { bool failed=false; try {fn();} catch(...) {failed=true;} assert(failed); }
int main(int argc,char**argv) {
    // Decode the HID descriptor independently and compare its wire report sizes.
    std::map<int,int> input,output; int size=0,count=0,id=0;
    const auto&d=report_descriptor();
    for(size_t i=0;i<d.size();) {
        int tag=d[i++],n=tag&3;if(n==3)n=4;assert(i+n<=d.size());
        unsigned value=0;for(int j=0;j<n;j++)value|=unsigned(d[i++])<<(8*j);
        switch(tag&0xfc) {case 0x74:size=value;break;case 0x94:count=value;break;case 0x84:id=value;break;case 0x80:input[id]+=size*count;break;case 0x90:output[id]+=size*count;break;}
    }
    assert(input[1]==64&&input[2]==48&&input[3]==32&&output[1]==8);
    assert(descriptors().size()==70);
    for(auto report:{keyboard(4,2),keyboard(),absolute(-1,40000),relative(500,-500)})assert(valid_report(report.data(),report.size()));
    auto abs=absolute(-1,40000);assert(abs[2]==0&&abs[3]==0&&abs[4]==255&&abs[5]==127);
    auto bad=keyboard();bad[2]=1;assert(!valid_report(bad.data(),bad.size()));bad=keyboard(102);assert(!valid_report(bad.data(),bad.size()));
    bad=absolute(0,0);bad[3]=128;assert(!valid_report(bad.data(),bad.size()));assert(!valid_report(nullptr,0));
    for(unsigned c=32;c<127;c++) {auto k=ascii(c);assert(k[0]&&k[0]<=0x65);}
    assert(ascii('A')[0]==4&&ascii('A')[1]==2&&ascii('z')[0]==29);
    assert(ascii('/')[0]==56&&ascii('?')[0]==56&&ascii('?')[1]==2);
    assert(ascii('\n')[0]==40&&ascii('\b')[0]==42&&ascii(0xE4)[0]==0);
    Quad unit={Point{0,0},{1,0},{1,1},{0,1}};Homography identity(unit);
    auto p=identity.map(.23,.71);assert(std::abs(p.x-.23)<1e-8&&std::abs(p.y-.71)<1e-8);
    Quad skew={Point{.12,.19},{.83,.08},{.95,.91},{.06,.81}};Homography warp(skew);
    for(int i=0;i<4;i++){p=warp.map(unit[i].x,unit[i].y);assert(std::abs(p.x-skew[i].x)<1e-8&&std::abs(p.y-skew[i].y)<1e-8);}
    auto invalid=unit;std::swap(invalid[1],invalid[2]);rejects([&]{Homography h(invalid);});
    invalid=unit;invalid[0].x=std::numeric_limits<double>::quiet_NaN();assert(!valid_quad(invalid));
    assert(base64({0,255,10})=="AP8K"&&base64({'f'})=="Zg=="&&base64({'f','o'})=="Zm8=");
    assert(parse_action({{"action","click"},{"x",.5},{"y",.25}}).button==1);
    assert(parse_action({{"action","key"},{"key","A"},{"modifiers",{"SUPER"}}}).mods==10);
    for(auto j:{Json{{"action","shell"},{"command","ls"}},Json{{"action","click"},{"x",2},{"y",.5}},Json{{"action","type"},{"text","中文"}},Json{{"action","scroll"},{"amount",.5}},Json{{"action","wait"},{"ms",5000}},Json{{"action","key"},{"key","INVALID"}},Json{{"action","type"},{"text",std::string(161,'a')}}})rejects([&]{parse_action(j);});
    rejects([]{validate_settings({"file:///etc/passwd","model",""});});
    rejects([]{validate_settings({"http://localhost/v1","model","bad\r\nheader"});});
    rejects([]{transcribe({"http://localhost/asr","model",""},std::string(100,'x'));});
    if(argc==2) {
        std::string base=argv[1];c1::reset_requests(5000);
        auto a=decide({base+"/vision","test",""},"click the test button",{0xff,0xd8,0xff,0xd9},Json::array());assert(a.kind=="click"&&a.x==.5);
        auto wav=synthesize({base+"/tts","test",""},"Hello");assert(wav.substr(0,4)=="RIFF");
        assert(transcribe({base+"/asr","test",""},wav)=="你好，请打开计算器。");
        auto out=converse({base+"/chat","test",""},"请打开计算器",Json::array());assert(out["task"]=="打开计算器");
        rejects([&]{decide({base+"/reasoning","test",""},"test",{1},Json::array());});
        rejects([&]{synthesize({base+"/bad-audio","test",""},"Hello");});
        std::thread cancel([]{std::this_thread::sleep_for(std::chrono::milliseconds(200));c1::cancel_requests();});
        auto started=std::chrono::steady_clock::now();rejects([&]{c1::http("GET",base+"/slow",{},"",nullptr,90);});cancel.join();
        assert(std::chrono::steady_clock::now()-started<std::chrono::seconds(2));c1::reset_requests();
    }
    std::cout<<"HID wire reports, ASCII map, calibration, action validation, voice API and cancellation passed\n";
}
