#include "session.hpp"
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace chat;
Snapshot wait(Session&s,std::function<bool(const Snapshot&)>f){for(int i=0;i<150;i++){auto v=s.snapshot();if(f(v))return v;std::this_thread::sleep_for(std::chrono::milliseconds(100));}throw std::runtime_error("IPC timeout: "+s.snapshot().error);}
int main(int argc,char**argv){assert(argc==3);std::string dir=argv[1],program=argv[2];mkdir(dir.c_str(),0700);std::string id;try{
    {Session s(dir,"",program);id=wait(s,[](auto&v){return v.ready;}).id;assert(s.submit({Action::Background,0,"1","",0,100}));assert(wait(s,[](auto&v){return v.completed==100;}).background);}
    std::this_thread::sleep_for(std::chrono::seconds(4));
    {Session s(dir,"",program);auto v=wait(s,[](auto&v){return v.ready;});assert(v.id==id&&v.background);assert(s.submit({Action::Background,0,"0","",0,101}));assert(!wait(s,[](auto&v){return v.completed==101;}).background);}
    std::this_thread::sleep_for(std::chrono::seconds(4));assert(access((dir+"/service.sock").c_str(),F_OK)!=0);
    {Session s(dir,"",program);auto v=wait(s,[](auto&v){return v.ready;});assert(v.id==id&&!v.background);}
    assert(stop_service(dir));std::cout<<"PASS service IPC, retained identity, optional background, disconnect lifetime and explicit shutdown\n";
}catch(const std::exception&e){stop_service(dir);std::cerr<<e.what()<<'\n';return 1;}}
