#include "engine.hpp"
#include "session.hpp"
#include "net.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
using namespace chat;
namespace fs=std::filesystem;
template<class Peer,class Predicate> Snapshot wait(Peer&p,Predicate pred,uint32_t f=UINT32_MAX,int seconds=90){
    for(int i=0;i<seconds*50;i++){auto s=p.snapshot(f);if(pred(s))return s;std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    throw std::runtime_error("outbox timeout: "+p.snapshot(f).error);
}
template<class Peer> Snapshot command(Peer&p,Command c,uint32_t f=UINT32_MAX){
    static uint64_t token=1000;c.token=++token;assert(p.submit(c));
    return wait(p,[&](auto&s){return s.completed==c.token;},f,10);
}
int main(int argc,char**argv){
    assert(argc==3);std::string root=argv[1],program=fs::absolute(argv[2]);fs::create_directories(root);
    std::string pattern=root+"/outbox-XXXXXX";std::vector<char>temp(pattern.begin(),pattern.end());temp.push_back(0);auto dir=mkdtemp(temp.data());assert(dir);root=dir;
    std::string aid,bid,chatpath;uint32_t an=0,bn=0;uint64_t uncertain=0;std::string wav="RIFF"+std::string(4,'\0')+"WAVE"+std::string(96000,'a'),jpg="\xff\xd8\xff"+std::string(65536,'b');
    try{
        {
            Engine a(root+"/a",""),b(root+"/b","");aid=wait(a,[](auto&s){return s.ready;}).id;bid=wait(b,[](auto&s){return s.ready;}).id;
            assert(command(a,{Action::Add,0,bid}).command_ok);assert(command(b,{Action::Add,0,aid}).command_ok);
            an=a.snapshot().friends[0].number;bn=b.snapshot().friends[0].number;
        }
        chatpath=root+"/a/chat-"+bid.substr(0,64)+".json";
        Json history=Json::array();for(int i=0;i<45;i++)history.push_back({{"text","old history"},{"mine",false},{"state","received"}});c1::save_private(chatpath,history.dump());
        {
            Engine a(root+"/a","");wait(a,[](auto&s){return s.ready;});assert(!a.snapshot().friends[0].online);
            assert(command(a,{Action::Background,0,"1"}).command_ok);
            for(int i=0;i<30;i++)assert(command(a,{Action::Send,an,"offline-"+std::to_string(i)},an).command_ok);
            c1::save_private(root+"/a/media/test.wav",wav);c1::save_private(root+"/a/media/test.jpg",jpg);
            assert(command(a,{Action::SendFile,an,"test.wav"},an).command_ok);assert(command(a,{Action::SendFile,an,"test.jpg"},an).command_ok);
            auto full=a.snapshot(an);assert(full.messages.size()==60&&full.queued==32);
            assert(!command(a,{Action::Send,an,"over limit"},an).command_ok);
            auto m=std::find_if(full.messages.begin(),full.messages.end(),[](auto&m){return m.text=="offline-0";});assert(m!=full.messages.end());
            assert(command(a,{Action::CancelQueued,an,std::to_string(m->id)},an).command_ok);assert(a.snapshot(an).queued==31);
            // Failure to persist must not clear the draft/acknowledge admission.
            fs::rename(chatpath,chatpath+".backup-test");fs::create_directory(chatpath);
            assert(!command(a,{Action::Send,an,"must not enter queue"},an).command_ok);assert(a.snapshot(an).queued==31);
            fs::remove(chatpath);fs::rename(chatpath+".backup-test",chatpath);
        }
        // Simulate a process dying after send intent / network acceptance.
        // Neither ambiguous record may be automatically replayed after restart.
        history=Json::parse(c1::read_file(chatpath));
        for(auto&m:history){if(m["text"]=="offline-1"){m["state"]="sending";uncertain=m["id"];}if(m["text"]=="offline-2")m["state"]="sent";}
        c1::save_private(chatpath,history.dump());
        {
            Engine a(root+"/a","");wait(a,[](auto&s){return s.ready;});auto s=a.snapshot(an);
            assert(s.queued==29&&std::count_if(s.messages.begin(),s.messages.end(),[](auto&m){return m.state=="unconfirmed";})==2);
        }
        // A corrupt history is preserved and blocked, never silently replaced.
        fs::create_directories(root+"/bad");fs::copy_file(root+"/a/profile.tox",root+"/bad/profile.tox");auto bad=root+"/bad/chat-"+bid.substr(0,64)+".json";c1::save_private(bad,"broken");
        {Engine a(root+"/bad","");wait(a,[](auto&s){return s.ready;});assert(!command(a,{Action::Send,an,"blocked"},an).command_ok);assert(c1::read_file(bad)=="broken");}
        // Deleting/re-adding the friend must not resurrect the former queue.
        fs::create_directories(root+"/deleted");fs::copy_file(root+"/a/profile.tox",root+"/deleted/profile.tox");auto old=root+"/deleted/chat-"+bid.substr(0,64)+".json";fs::copy_file(chatpath,old);
        {Engine a(root+"/deleted","");wait(a,[](auto&s){return s.ready;});assert(command(a,{Action::Delete,an}).command_ok);assert(!fs::exists(old));assert(command(a,{Action::Add,0,bid}).command_ok);assert(a.snapshot().queued==0);}
        std::cout<<"PASS offline durable text/photo/voice admission, cap, cancellation, trimming, write failure, restart, ambiguity and corrupt history protection\n";
        {
            Engine b(root+"/b","");auto bs=wait(b,[](auto&s){return s.ready;});
            {
                Session a(root+"/a","",program);auto as=wait(a,[](auto&s){return s.ready;});assert(as.id==aid&&as.background&&as.queued==29);
                assert(command(a,{Action::Bootstrap,0,"127.0.0.1",bs.dht_key,bs.udp_port}).command_ok);
                assert(command(b,{Action::Bootstrap,0,"127.0.0.1",as.dht_key,as.udp_port}).command_ok);
            } // No GUI/IPC client is attached while the recipient comes online.
            wait(b,[](auto&s){return !s.friends.empty()&&s.friends[0].online;});
            auto received=wait(b,[](auto&s){return s.messages.size()==27&&s.transfers.size()==2;},bn);
            for(int i=0;i<27;i++){assert(received.messages[i].text=="offline-"+std::to_string(i+3));assert(!received.messages[i].mine);}
            for(auto&t:received.transfers){assert(t.state=="offered"&&t.done==0);assert(command(b,{Action::AcceptFile,bn,std::to_string(t.id)},bn).command_ok);}
            auto done=wait(b,[](auto&s){return s.messages.size()==29&&s.transfers.empty();},bn);
            for(auto&m:done.messages)if(!m.file.empty()){assert(m.state=="complete");assert(c1::read_file(root+"/b/media/"+m.file)==(m.kind=="voice"?wav:jpg));}
            {
                Session a(root+"/a","",program);wait(a,[](auto&s){return s.ready;});
                auto sent=wait(a,[](auto&s){return s.queued==0&&s.transfers.empty()&&std::count_if(s.messages.begin(),s.messages.end(),[](auto&m){return m.state=="delivered";})==27;},an);
                assert(command(a,{Action::RetryQueued,an,std::to_string(uncertain)},an).command_ok);
                wait(b,[](auto&s){return s.messages.size()==30&&s.messages.back().text=="offline-1";},bn);
                wait(a,[&](auto&s){return std::any_of(s.messages.begin(),s.messages.end(),[&](auto&m){return m.id==uncertain&&m.state=="delivered";});},an);
            }
            assert(stop_service(root+"/a"));for(int i=0;i<50&&fs::exists(root+"/a/service.sock");i++)usleep(100000);
            {
                Session a(root+"/a","",program);auto as=wait(a,[](auto&s){return s.ready;});
                assert(as.queued==0);assert(command(a,{Action::Bootstrap,0,"127.0.0.1",bs.dht_key,bs.udp_port}).command_ok);
                assert(command(b,{Action::Bootstrap,0,"127.0.0.1",as.dht_key,as.udp_port}).command_ok);
                wait(a,[](auto&s){return s.friends[0].online;});
                std::this_thread::sleep_for(std::chrono::seconds(2));assert(b.snapshot(bn).messages.size()==30);
            }
        }
        stop_service(root+"/a");std::cout<<"PASS background service FIFO replay, receipt updates, explicit attachment acceptance, byte integrity, explicit resend and no replay after restart\n";
        std::cout<<"Private test data: "<<root<<'\n';return 0;
    }catch(const std::exception&e){stop_service(root+"/a");std::cerr<<e.what()<<'\n';return 1;}
}
