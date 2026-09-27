// Explicit diagnostics only; never packaged with the application.
#include "gamestream.hpp"
#include "remote.hpp"
#include "net.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <sys/stat.h>
int main(int argc,char**argv){
    umask(0077);
    try{
        if(argc<5)throw std::runtime_error("usage: probe inspect|pair|apps|launch|cancel IP PORT PRIVATE_DIRECTORY [PIN]");
        moonpilot::Host host;host.address=argv[2];host.port=std::stoi(argv[3]);std::atomic<bool>cancel{false};moonpilot::GameStream gs(host,argv[4],cancel);
        std::string mode=argv[1];
        if(mode=="cancel"){
            std::thread timer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(150));cancel=true;});bool rejected=false;
            auto begin=std::chrono::steady_clock::now();try{gs.inspect();}catch(...){rejected=true;}timer.join();
            if(!rejected||std::chrono::steady_clock::now()-begin>std::chrono::seconds(2))throw std::runtime_error("cancellation failed");std::cout<<"CANCELLED\n";return 0;
        }
        auto info=mode=="pair"?gs.pair(argc>5?argv[5]:""):gs.inspect();
        Json out={{"name",info.name},{"paired",info.paired}};
        if(mode=="apps"||mode=="pair"){out["apps"]=Json::array();for(auto&a:gs.applications())out["apps"].push_back({{"id",a.id},{"name",a.name}});}
        if(mode=="launch"){STREAM_CONFIGURATION c{};auto s=gs.launch(1,c);out["session"]=s.session;out["width"]=c.width;out["fps"]=c.fps;}
        assert(moonpilot::virtual_key(4)=='A');assert(moonpilot::virtual_key(40)==13);assert(moonpilot::virtual_key(42)==8);assert(moonpilot::virtual_key(82)==38);
        assert(uint16_t(moonpilot::wire_key(moonpilot::virtual_key(4)))==0x8041);assert(uint16_t(moonpilot::wire_key(0xa2))==0x80a2);
        std::cout<<out.dump()<<"\n";return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
}
