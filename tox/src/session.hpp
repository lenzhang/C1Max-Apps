// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "engine.hpp"
namespace chat {
// A UI-only IPC adapter. The service is the sole owner of toxcore and identity.
class Session {
    std::string directory_,nodes_,program_;
    std::atomic<bool>stop_{false};std::thread worker_;std::mutex mutex_;
    Snapshot state_;std::deque<Command>commands_;uint32_t selected_=UINT32_MAX;
    void run();
public:
    Session(std::string directory,std::string nodes,std::string program);
    ~Session();
    bool submit(Command);
    Snapshot snapshot(uint32_t number=UINT32_MAX);
};
bool stop_service(const std::string&directory);
int run_service(const std::string&directory,const std::string&nodes);
}
