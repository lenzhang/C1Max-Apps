// SPDX-License-Identifier: GPL-3.0-only
#include "engine.hpp"
#include <algorithm>
#include <stdexcept>

namespace chat {
bool pending_message(const Message&m){
    return m.state=="queued"||m.state=="sending"||m.state=="sent"||
        m.state=="waiting"||m.state=="receiving"||(m.mine&&m.state=="unconfirmed");
}
void Engine::check_queue_room(uint32_t n){
    if(blocked_history_.count(n))throw std::runtime_error("此好友的记录损坏，已保留原文件；暂不能发送");
    if(std::none_of(state_.friends.begin(),state_.friends.end(),[n](auto&f){return f.number==n;}))throw std::runtime_error("好友不存在");
    size_t own=0,total=0;
    for(auto&[number,list]:history_)for(auto&m:list)if(m.mine&&pending_message(m)){total++;if(number==n)own++;}
    if(own>=32||total>=128)throw std::runtime_error("待发队列已满（每好友 32 条，共 128 条），请先处理队列");
}
void Engine::queue_message(uint32_t n,Message m){
    check_queue_room(n);auto old=history_[n];m.id=++next_message_;m.state="queued";m.mine=true;
    history_[n].push_back(std::move(m));
    // A successful command means durable admission, not network delivery.
    try{save_history(n);}catch(...){history_[n]=std::move(old);throw;}
    retry_at_.erase(n);retry_delay_.erase(n);state_.revision++;
}
void Engine::execute_outbox(const Command&c){
    uint64_t id=0;try{size_t used=0;id=std::stoull(c.text,&used);if(used!=c.text.size())id=0;}catch(...){}
    auto&list=history_[c.number];auto it=std::find_if(list.begin(),list.end(),[id](auto&m){return id&&m.id==id&&m.mine;});
    if(it==list.end())throw std::runtime_error("待发消息不存在");
    if(it->state!="queued"&&it->state!="unconfirmed"&&it->state!="failed")throw std::runtime_error("消息已开始发送，不能修改待发队列");
    if(c.action==Action::RetryQueued){
        if(it->state=="queued")return;
        if(it->state=="failed")check_queue_room(c.number);
    }
    auto old=*it;it->state=c.action==Action::CancelQueued?"cancelled":"queued";
    try{save_history(c.number);}catch(...){*it=std::move(old);throw;}
    retry_at_.erase(c.number);retry_delay_.erase(c.number);state_.revision++;
}
void Engine::process_outbox(){
    const auto now=std::chrono::steady_clock::now();
    for(auto&f:state_.friends){
        if(!f.online||blocked_history_.count(f.number)||retry_at_[f.number]>now)continue;
        auto&list=history_[f.number];auto it=std::find_if(list.begin(),list.end(),[](auto&m){return m.mine&&m.state=="queued";});
        if(it==list.end())continue;auto&m=*it;
        // Persist the intent before the network side effect. A crash between
        // this write and the receipt is ambiguous: restart shows unconfirmed
        // and never blindly replays it to clients without deduplication IDs.
        m.state="sending";
        try{save_history(f.number);}catch(const std::exception&){
            m.state="queued";retry_at_[f.number]=now+std::chrono::seconds(30);
            error("待发消息落盘失败，暂缓发送；请检查存储空间");continue;
        }
        bool retry=false;
        try{
            if(!m.file.empty())retry=!offer_file(f.number,m);
            else{
                Tox_Err_Friend_Send_Message err;
                auto receipt=tox_friend_send_message(tox_,f.number,TOX_MESSAGE_TYPE_NORMAL,reinterpret_cast<const uint8_t*>(m.text.data()),m.text.size(),&err);
                if(err==TOX_ERR_FRIEND_SEND_MESSAGE_OK){m.state="sent";m.receipt=receipt;}
                else if(err==TOX_ERR_FRIEND_SEND_MESSAGE_FRIEND_NOT_CONNECTED||err==TOX_ERR_FRIEND_SEND_MESSAGE_SENDQ)retry=true;
                else throw std::runtime_error("待发文字无法发送，错误 "+std::to_string(err));
            }
        }catch(const std::exception&e){m.state="failed";error(e.what());}
        if(retry){m.state="queued";auto&delay=retry_delay_[f.number];delay=delay?std::min(30u,delay*2):1;retry_at_[f.number]=now+std::chrono::seconds(delay);}
        else{retry_delay_.erase(f.number);retry_at_[f.number]=now+std::chrono::milliseconds(250);}
        state_.revision++;
        try{save_history(f.number);}catch(const std::exception&){error("发送状态保存失败；重启后请核对未确认消息");}
    }
}
}
