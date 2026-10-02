// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <toxcore/tox.h>

namespace chat {
struct Message { std::string text, state; bool mine=false; uint32_t receipt=0; };
struct Friend { uint32_t number=0; std::string key,name; bool online=false; unsigned unread=0; };
struct Request { std::string key,message; };
struct Snapshot {
    uint64_t revision=0;
    uint64_t error_count=0;
    uint64_t completed=0;
    bool command_ok=false;
    std::string id,name="C1 Max",status="正在连接网络…",error,dht_key;
    uint16_t udp_port=0;
    bool ready=false, online=false;
    std::vector<Friend> friends;
    std::vector<Request> requests;
    std::vector<Message> messages;
};
enum class Action { Add, Accept, Reject, Send, Rename, Delete, Read, Bootstrap };
struct Command { Action action; uint32_t number=0; std::string text,extra; uint16_t port=0; uint64_t token=0; };
std::string hex(const uint8_t *data,size_t size);
bool unhex(const std::string &text,uint8_t *out,size_t size);
std::string clean_text(const std::string &text,size_t max_bytes);
class Engine {
public:
    // An empty nodes path keeps QA instances off the public network.
    Engine(std::string directory,std::string nodes_path);
    ~Engine();
    Engine(const Engine&)=delete;
    bool submit(Command command);
    Snapshot snapshot(uint32_t friend_number=UINT32_MAX);
private:
    std::string directory_,nodes_;
    std::atomic<bool> stop_{false};
    std::thread worker_;
    std::mutex mutex_;
    Snapshot state_;
    std::deque<Command> commands_;
    std::map<uint32_t,std::vector<Message>> history_;
    Tox *tox_=nullptr;
    std::string last_saved_; // Last snapshot successfully loaded/saved by this instance.
    bool dirty_=false;
    void run();
    void publish();
    void save();
    void save_requests();
    void save_history(uint32_t number);
    void refresh_friends();
    void execute(const Command&);
    void bootstrap();
    void error(const std::string&);
    void receive_request(const uint8_t*,const uint8_t*,size_t);
    void receive_message(uint32_t,const uint8_t*,size_t);
    void receipt(uint32_t,uint32_t);
    static void on_request(Tox*,const uint8_t*,const uint8_t*,size_t,void*);
    static void on_message(Tox*,uint32_t,Tox_Message_Type,const uint8_t*,size_t,void*);
    static void on_receipt(Tox*,uint32_t,uint32_t,void*);
};
}
