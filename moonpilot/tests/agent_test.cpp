#include "../src/agent.hpp"
#include "../src/voice.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
using namespace moonpilot;
template<class F> void rejects(F fn){bool failed=false;try{fn();}catch(...){failed=true;}assert(failed);}
int main(int argc,char**argv){
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
    std::cout<<"Agent validation, voice API and cancellation passed\n";
}
