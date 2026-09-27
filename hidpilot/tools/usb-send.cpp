// Local on-device diagnostic client. Explicit reports only; no shell execution.
#include "../src/reports.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <iostream>
#include <sstream>
int main(int argc,char**argv){int fd=-1;try{
    if(argc<2)throw std::runtime_error("--type ASCII | --move X Y (0..32767) | --relative DX DY | --click | --release");
    fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);sockaddr_un a{};a.sun_family=AF_UNIX;strcpy(a.sun_path,"/tmp/c1max-hidpilot/control");
    if(fd<0||connect(fd,(sockaddr*)&a,sizeof a))throw std::runtime_error("USB session not ready");
    auto send_report=[&](const std::vector<uint8_t>&r){if(send(fd,r.data(),r.size(),MSG_NOSIGNAL)!=(ssize_t)r.size())throw std::runtime_error("send failed");pollfd p{fd,POLLIN,0};uint8_t ack=0;if(poll(&p,1,2000)!=1||recv(fd,&ack,1,0)!=1||ack!=1)throw std::runtime_error("host did not accept report");usleep(30000);};
    std::string op=argv[1];
    if(op=="--interactive"&&argc==2){std::cout<<"CONNECTED"<<std::endl;std::string line;while(std::getline(std::cin,line)){std::istringstream in(line);std::string cmd;in>>cmd;int x=0,y=0;if(cmd=="move"&&in>>x>>y)send_report(hidpilot::absolute(x,y));else if(cmd=="relative"&&in>>x>>y)send_report(hidpilot::relative(x,y));else if(cmd=="click"){send_report(hidpilot::relative(0,0,1));send_report(hidpilot::relative(0,0));}else if(cmd=="type"){std::string t;std::getline(in,t);if(!t.empty()&&t[0]==' ')t.erase(0,1);for(unsigned char c:t){auto k=hidpilot::ascii(c);if(!k[0])throw std::runtime_error("ASCII only");send_report(hidpilot::keyboard(k[0],k[1]));send_report(hidpilot::keyboard());}}else if(cmd=="quit")break;else throw std::runtime_error("Bad command");std::cout<<"SENT "<<cmd<<std::endl;}}
    else if(op=="--type"&&argc==3){for(unsigned char c:std::string(argv[2]))if(!hidpilot::ascii(c)[0])throw std::runtime_error("ASCII keyboard only");for(unsigned char c:std::string(argv[2])){auto k=hidpilot::ascii(c);send_report(hidpilot::keyboard(k[0],k[1]));send_report(hidpilot::keyboard());}}
    else if(op=="--move"&&argc==4)send_report(hidpilot::absolute(std::stoi(argv[2]),std::stoi(argv[3])));
    else if(op=="--relative"&&argc==4)send_report(hidpilot::relative(std::stoi(argv[2]),std::stoi(argv[3])));
    else if(op=="--click"&&argc==2){send_report(hidpilot::relative(0,0,1));send_report(hidpilot::relative(0,0));}
    else if(op!="--release"||argc!=2)throw std::runtime_error("Invalid arguments");
    send_report(hidpilot::keyboard());send_report(hidpilot::relative(0,0));
    // Keep the shared session alive briefly so enumeration and input can be
    // inspected. This diagnostic owns its connection; closing restores USB.
    close(fd);puts("SENT");return 0;
}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());if(fd>=0)close(fd);return 1;}}
