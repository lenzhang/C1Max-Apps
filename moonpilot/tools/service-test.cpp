// Explicit service diagnostic. Never opens camera/microphone or emits HID.
#include "../src/agent.hpp"
#include "../src/voice.hpp"
#include <iostream>
int main(int argc,char**argv){
    if(argc!=4){std::cerr<<"Usage: service-test PRIVATE_SETTINGS asr|vision|chat|tts FIXTURE_PATH_OR_TEXT\n";return 2;}
    try{
        auto config=Json::parse(c1::read_file(argv[1],8192));std::string mode=argv[2];auto j=config.at(mode);
        moonpilot::Settings s{j.at("endpoint"),j.at("model"),j.value("token","")};c1::reset_requests(95000);
        if(mode=="asr")std::cout<<Json{{"text",moonpilot::transcribe(s,c1::read_file(argv[3],700000))}}.dump()<<"\n";
        else if(mode=="chat")std::cout<<moonpilot::converse(s,argv[3],Json::array()).dump()<<"\n";
        else if(mode=="tts"){auto audio=moonpilot::synthesize(s,"你好，这是语音播报测试。");c1::save_private(argv[3],audio);std::cout<<"WAV bytes="<<audio.size()<<"\n";}
        else if(mode=="vision"){auto file=c1::read_file(argv[3],512*1024);auto a=moonpilot::decide(s,"Click the center of the large green CLICKED button in this test window. Do not click anything else.",{file.begin(),file.end()},Json::array());std::cout<<Json{{"action",a.kind},{"x",a.x},{"y",a.y},{"summary",a.summary}}.dump()<<"\n";}
        else return 2;
    }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}return 0;
}
