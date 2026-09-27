#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <Limelight.h>

namespace moonpilot {
struct Host {
    std::string address;
    int port=47989;
    int width=640,height=360,fps=15,bitrate=1500;
};
void validate_host(const Host&);
struct Application { int id; std::string name; };
struct Server {
    std::string name,version,gfe,session;
    int https_port=47984,current=0,codecs=SCM_H264;
    bool paired=false;
};
// One client per operation/session. TLS is pinned to the certificate verified
// by the PIN challenge, and keys/certificates live outside the public release.
class GameStream {
    struct Impl;
    std::unique_ptr<Impl> p_;
public:
    GameStream(Host host,std::string data,std::atomic<bool>&cancel);
    ~GameStream();
    Server inspect();
    Server pair(const std::string&pin);
    std::vector<Application> applications();
    Server launch(int app,STREAM_CONFIGURATION&config);
};
}
