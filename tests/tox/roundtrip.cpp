#include "engine.hpp"
#include "net.hpp"
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

using namespace chat;
static Snapshot wait_for(Engine&e,const std::function<bool(const Snapshot&)>&pred,uint32_t f=UINT32_MAX,int seconds=30){
    for(int i=0;i<seconds*50;i++){auto s=e.snapshot(f);if(pred(s))return s;std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    auto s=e.snapshot(f);throw std::runtime_error("Timed out: "+s.status+" / "+s.error);
}
static Snapshot command(Engine&e,Command c,uint64_t token,uint32_t f=UINT32_MAX){c.token=token;assert(e.submit(c));return wait_for(e,[token](const Snapshot&s){return s.completed==token;},f);}
int main(int argc,char**argv){
    try{
        assert(argc==2);std::string root=argv[1];mkdir(root.c_str(),0700);
        std::string pattern=root+"/run-XXXXXX";std::vector<char>dir(pattern.begin(),pattern.end());dir.push_back(0);
        auto created=mkdtemp(dir.data());if(!created)throw std::runtime_error("Cannot create test directory");root=created;
        std::cout<<"Test data: "<<root<<'\n';std::string id;
        { // Two identities owned entirely by this test; no public bootstrap or messages to other users.
            Engine a(root+"/a",""),b(root+"/b","");
            auto av=wait_for(a,[](const Snapshot&s){return s.ready;});auto bv=wait_for(b,[](const Snapshot&s){return s.ready;});id=av.id;assert(id.size()==76&&id!=bv.id);
            auto invalid=command(a,{Action::Add,0,"123"},1);assert(!invalid.command_ok&&invalid.friends.empty());
            auto offline=command(a,{Action::Send,123,"offline"},2);assert(!offline.command_ok);
            assert(command(a,{Action::Rename,0,"Alice / 测试"},3).command_ok);
            assert(command(b,{Action::Rename,0,"Bob / 测试"},1).command_ok);
            a.submit({Action::Bootstrap,0,"127.0.0.1",bv.dht_key,bv.udp_port});b.submit({Action::Bootstrap,0,"127.0.0.1",av.dht_key,av.udp_port});
            assert(command(a,{Action::Add,0,bv.id},4).command_ok);
            auto request=wait_for(b,[](const Snapshot&s){return !s.requests.empty();},UINT32_MAX,90);
            assert(request.friends.empty()); // Incoming requests are never auto-accepted.
            assert(command(b,{Action::Accept,0,request.requests.front().key},2).command_ok);
            av=wait_for(a,[](const Snapshot&s){return !s.friends.empty()&&s.friends.front().online;},UINT32_MAX,60);
            bv=wait_for(b,[](const Snapshot&s){return !s.friends.empty()&&s.friends.front().online;},UINT32_MAX,60);
            auto an=av.friends.front().number,bn=bv.friends.front().number;
            const std::string text="Hello from C1 Max. 你好，点对点聊天！";
            assert(command(a,{Action::Send,an,text},5,an).command_ok);
            auto received=wait_for(b,[&](const Snapshot&s){return !s.messages.empty()&&s.messages.back().text==text;},bn);
            assert(!received.messages.back().mine);
            auto delivered=wait_for(a,[](const Snapshot&s){return !s.messages.empty()&&s.messages.back().state=="delivered";},an);
            assert(delivered.messages.back().mine);
            assert(command(b,{Action::Send,bn,"收到。This reply travelled over localhost Tox."},3,bn).command_ok);
            wait_for(a,[](const Snapshot&s){return s.messages.size()==2&&!s.messages.back().mine;},an);
            // Attachments use standard Tox file callbacks. No bytes are accepted
            // until the receiving user explicitly approves the offer.
            std::string wav="RIFF"+std::string(4,'\0')+"WAVE"+std::string(96000,'x');
            c1::save_private(root+"/a/media/test.wav",wav);
            assert(command(a,{Action::SendFile,an,"test.wav"},7,an).command_ok);
            auto offer=wait_for(b,[](const Snapshot&s){return !s.transfers.empty();},bn);
            assert(offer.transfers.front().state=="offered"&&offer.transfers.front().done==0);
            assert(command(b,{Action::AcceptFile,bn,std::to_string(offer.transfers.front().id)},4,bn).command_ok);
            auto complete=wait_for(b,[](const Snapshot&s){return !s.messages.empty()&&s.messages.back().state=="complete";},bn);
            assert(c1::read_file(root+"/b/media/"+complete.messages.back().file)==wav);
            wait_for(a,[](const Snapshot&s){return !s.messages.empty()&&s.messages.back().state=="complete";},an);
            assert(!command(a,{Action::SendFile,an,"../profile.tox"},8,an).command_ok);
            assert(command(a,{Action::SendFile,an,"test.wav"},9,an).command_ok);
            offer=wait_for(b,[](const Snapshot&s){return !s.transfers.empty();},bn);
            assert(command(b,{Action::CancelFile,bn,std::to_string(offer.transfers.front().id)},5,bn).command_ok);
            wait_for(a,[](const Snapshot&s){return s.transfers.empty()&&s.messages.back().state=="cancelled";},an);
            assert(command(a,{Action::Background,0,"1"},10,an).background);
            std::cout<<"PASS: explicit attachment acceptance, chunked byte integrity, rejected paths, cancellation, background preference\n";
            // Oversized text is rejected before entering toxcore's send queue.
            assert(!command(a,{Action::Send,an,std::string(1025,'x')},6,an).command_ok);
            Engine duplicate(root+"/a","");auto blocked=wait_for(duplicate,[](const Snapshot&s){return !s.error.empty();});assert(!blocked.ready);
            std::cout<<"PASS: two-node discovery, manual friend acceptance, UTF-8 send/receive, receipt, bounds, identity lock\n";
        }
        {
            Engine restored(root+"/a","");auto s=wait_for(restored,[](const Snapshot&s){return s.ready;});assert(s.id==id&&s.name=="Alice / 测试"&&s.friends.size()==1);
            auto history=restored.snapshot(s.friends.front().number);assert(history.messages.size()==4&&s.background);assert(history.messages[2].state=="complete"&&history.messages[3].state=="cancelled");
            struct stat st{};assert(!stat((root+"/a/profile.tox").c_str(),&st)&&(st.st_mode&0777)==0600);
            assert(!stat((root+"/a").c_str(),&st)&&(st.st_mode&0777)==0700);
            std::cout<<"PASS: identity, name, friend and history survive restart; private file modes\n";
        }
        mkdir((root+"/bad").c_str(),0700);c1::save_private(root+"/bad/profile.tox","bad profile");
        {Engine bad(root+"/bad","");auto s=wait_for(bad,[](const Snapshot&s){return !s.error.empty();});assert(!s.ready);}
        assert(c1::read_file(root+"/bad/profile.tox")=="bad profile");
        // A previous valid snapshot survives a zero-filled primary file. The
        // application must never silently generate a different identity.
        auto backup=c1::read_file(root+"/a/profile.tox.bak");
        mkdir((root+"/backup").c_str(),0700);c1::save_private(root+"/backup/profile.tox",backup);
        {Engine restored(root+"/backup","");auto s=wait_for(restored,[](const Snapshot&s){return s.ready;});assert(s.id==id&&s.friends.size()==1);}
        auto zeros=std::string(3324,'\0');c1::save_private(root+"/bad/profile.tox",zeros);
        {Engine bad(root+"/bad","");auto s=wait_for(bad,[](const Snapshot&s){return !s.error.empty();});assert(!s.ready);}
        assert(c1::read_file(root+"/bad/profile.tox")==zeros);
        std::cout<<"PASS: last-good snapshot restores the same identity and friends; zero-filled primary remains untouched\n";
        uint8_t data[2];assert(!unhex("XX00",data,2));assert(unhex("a0FF",data,2)&&hex(data,2)=="A0FF");
        assert(clean_text("a\n\tb",10)=="a  b");assert(clean_text("你好",4)=="你");assert(clean_text(std::string("\xc0\xaf",2),8)=="??");
        std::cout<<"PASS: corrupt profile preserved; hex and UTF-8 validation\n";
        return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
