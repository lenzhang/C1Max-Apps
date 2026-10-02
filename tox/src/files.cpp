// SPDX-License-Identifier: GPL-3.0-only
#include "engine.hpp"
#include "net.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
namespace chat {
namespace {
constexpr uint64_t limit=4*1024*1024,quota=64*1024*1024;
std::string kind_of(std::string name){std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::tolower(c);});auto dot=name.rfind('.');auto ext=dot==std::string::npos?"":name.substr(dot);return ext==".jpg"||ext==".jpeg"?"photo":ext==".wav"?"voice":"";}
bool leaf(const std::string&s){return !s.empty()&&s.size()<128&&s.find("..") == std::string::npos&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")==std::string::npos;}
void room(const std::string&folder,uint64_t size){
    uint64_t used=0;DIR*d=opendir(folder.c_str());if(!d)throw std::runtime_error("附件目录无法读取");
    while(auto*e=readdir(d)){struct stat s{};if(!lstat((folder+"/"+e->d_name).c_str(),&s)&&S_ISREG(s.st_mode))used+=s.st_size;}closedir(d);
    struct statvfs fs{};if(used+size>quota||statvfs(folder.c_str(),&fs)||uint64_t(fs.f_bavail)*fs.f_frsize<size+8*1024*1024)throw std::runtime_error("附件空间不足（上限 64 MB），请先清理附件");
}
void sniff(int fd,const std::string&kind){unsigned char b[12]{};if(pread(fd,b,sizeof b,0)!=sizeof b)throw std::runtime_error("附件内容不完整");if(kind=="photo"?(b[0]!=255||b[1]!=216||b[2]!=255):(std::memcmp(b,"RIFF",4)||std::memcmp(b+8,"WAVE",4)))throw std::runtime_error("附件不是 JPEG / WAV");}
std::string fresh(const std::string&folder,const std::string&kind,int&fd){std::string path=folder+"/media-XXXXXX";std::vector<char>name(path.begin(),path.end());name.push_back(0);fd=mkstemp(name.data());if(fd<0)throw std::runtime_error("无法创建附件");fcntl(fd,F_SETFD,FD_CLOEXEC);std::string old=name.data(),final=old+(kind=="photo"?".jpg":".wav");if(rename(old.c_str(),(final+".part").c_str())){close(fd);fd=-1;unlink(old.c_str());throw std::runtime_error("无法创建附件");}return final.substr(folder.size()+1);}
}
uint64_t Engine::find_file(uint32_t n,uint32_t f){for(auto&[id,t]:transfers_)if(t.view.number==n&&t.view.file_number==f)return id;return 0;}
void Engine::finish_file(uint64_t id,const std::string&state){
    auto it=transfers_.find(id);if(it==transfers_.end())return;auto t=it->second;transfers_.erase(it);
    if(t.fd>=0)close(t.fd);if(!t.view.mine&&!t.view.file.empty())unlink((directory_+"/media/"+t.view.file+".part").c_str());
    if(state!="complete")tox_file_control(tox_,t.view.number,t.view.file_number,TOX_FILE_CONTROL_CANCEL,nullptr);
    for(auto&m:history_[t.view.number])if(m.file==t.view.file&&!m.file.empty()&&(m.state=="waiting"||m.state=="receiving"))m.state=state;
    state_.revision++;save_history(t.view.number);
}
void Engine::execute_file(const Command&c){
    if(c.action==Action::SendFile){
        if(transfers_.size()>=8)throw std::runtime_error("最多同时传输八个附件");
        if(tox_friend_get_connection_status(tox_,c.number,nullptr)==TOX_CONNECTION_NONE)throw std::runtime_error("好友离线，附件未发送");
        if(!leaf(c.text))throw std::runtime_error("无效的附件文件名");auto kind=kind_of(c.text);if(kind.empty())throw std::runtime_error("只支持 JPEG 照片和 WAV 语音");
        auto path=directory_+"/media/"+c.text;int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)throw std::runtime_error("附件不存在");struct stat st{};
        try{if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<12||uint64_t(st.st_size)>limit)throw std::runtime_error("附件必须小于 4 MB");sniff(fd,kind);}catch(...){close(fd);throw;}
        Tox_Err_File_Send err;uint32_t f=tox_file_send(tox_,c.number,TOX_FILE_KIND_DATA,st.st_size,nullptr,reinterpret_cast<const uint8_t*>(c.text.data()),c.text.size(),&err);
        if(err!=TOX_ERR_FILE_SEND_OK){close(fd);throw std::runtime_error("无法发送附件请求");}
        Transfer t{++next_transfer_,c.number,f,uint64_t(st.st_size),0,true,c.text,c.text,kind,"waiting"};transfers_[t.id]={t,fd};
        history_[c.number].push_back({kind=="photo"?"照片":"语音","waiting",true,0,c.text,kind,uint64_t(st.st_size)});save_history(c.number);return;
    }
    uint64_t id=0;try{id=std::stoull(c.text);}catch(...){throw std::runtime_error("附件请求不存在");}
    auto it=transfers_.find(id);if(it==transfers_.end()||it->second.view.number!=c.number)throw std::runtime_error("附件请求已结束");
    if(c.action==Action::CancelFile){finish_file(id,"cancelled");return;}
    auto&t=it->second;if(t.view.mine||t.view.state!="offered")throw std::runtime_error("附件已处理");
    uint64_t reserved=0;for(auto&[k,v]:transfers_)if(!v.view.mine&&v.view.state!="offered")reserved+=v.view.size-v.view.done;
    room(directory_+"/media",reserved+t.view.size);t.view.file=fresh(directory_+"/media",t.view.kind,t.fd);t.view.state="receiving";
    history_[c.number].push_back({t.view.kind=="photo"?"照片":"语音","receiving",false,0,t.view.file,t.view.kind,t.view.size});save_history(c.number);
    if(!tox_file_control(tox_,c.number,t.view.file_number,TOX_FILE_CONTROL_RESUME,nullptr)){finish_file(id,"failed");throw std::runtime_error("无法接受附件");}
}
void Engine::file_offer(uint32_t n,uint32_t f,uint32_t kind,uint64_t size,const uint8_t*name,size_t len){
    auto media=kind_of(std::string(reinterpret_cast<const char*>(name),len));
    if(kind!=TOX_FILE_KIND_DATA||size<12||size>limit||media.empty()||transfers_.size()>=8){tox_file_control(tox_,n,f,TOX_FILE_CONTROL_CANCEL,nullptr);return;}
    auto id=++next_transfer_;Transfer v{id,n,f,size,0,false,clean_text(std::string(reinterpret_cast<const char*>(name),len),80),"",media,"offered"};transfers_[id]={v,-1};
    for(auto&fr:state_.friends)if(fr.number==n)fr.unread=std::min(999u,fr.unread+1);state_.revision++;
}
void Engine::file_chunk(uint32_t n,uint32_t f,uint64_t pos,const uint8_t*data,size_t len){
    auto id=find_file(n,f);if(!id)return;auto&t=transfers_.at(id);if(t.view.mine||t.fd<0)return;
    try{
        if(pos!=t.view.done||pos>t.view.size||len>t.view.size-pos)throw std::runtime_error("附件分块顺序错误");
        if(!len){
            if(pos!=t.view.size)throw std::runtime_error("附件不完整");sniff(t.fd,t.view.kind);if(fsync(t.fd))throw std::runtime_error("附件保存失败");
            auto path=directory_+"/media/"+t.view.file;if(rename((path+".part").c_str(),path.c_str()))throw std::runtime_error("附件保存失败");
            int d=open((directory_+"/media").c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(d>=0){fsync(d);close(d);}finish_file(id,"complete");return;
        }
        size_t off=0;while(off<len){ssize_t w=pwrite(t.fd,data+off,len-off,pos+off);if(w<0&&errno==EINTR)continue;if(w<=0)throw std::runtime_error("附件写入失败");off+=w;}
        t.view.done+=len;state_.revision++;
    }catch(...){finish_file(id,"failed");throw;}
}
void Engine::file_request(uint32_t n,uint32_t f,uint64_t pos,size_t len){
    auto id=find_file(n,f);if(!id)return;auto&t=transfers_.at(id);if(!t.view.mine)return;
    if(!len){finish_file(id,"complete");return;}
    if(pos>t.view.size||len>t.view.size-pos||len>65536){finish_file(id,"failed");return;}
    std::vector<uint8_t> bytes(len);ssize_t got;do{got=pread(t.fd,bytes.data(),len,pos);}while(got<0&&errno==EINTR);
    if(got!=ssize_t(len)){finish_file(id,"failed");return;}
    Tox_Err_File_Send_Chunk err;bool sent=tox_file_send_chunk(tox_,n,f,pos,bytes.data(),len,&err);
    if(!sent&&err!=TOX_ERR_FILE_SEND_CHUNK_SENDQ){finish_file(id,"failed");return;}
    if(sent){t.view.done=std::max(t.view.done,pos+len);t.view.state="sending";state_.revision++;}
}
void Engine::file_control(uint32_t n,uint32_t f,Tox_File_Control control){auto id=find_file(n,f);if(!id)return;if(control==TOX_FILE_CONTROL_CANCEL)finish_file(id,"cancelled");else{transfers_.at(id).view.state=control==TOX_FILE_CONTROL_PAUSE?"paused":"sending";state_.revision++;}}
#define SAFE_FILE(body) auto&e=*static_cast<Engine*>(p);try{body;}catch(const std::exception&x){e.error(x.what());}
void Engine::on_file_offer(Tox*,uint32_t n,uint32_t f,uint32_t k,uint64_t s,const uint8_t*b,size_t l,void*p){SAFE_FILE(e.file_offer(n,f,k,s,b,l))}
void Engine::on_file_chunk(Tox*,uint32_t n,uint32_t f,uint64_t o,const uint8_t*b,size_t l,void*p){SAFE_FILE(e.file_chunk(n,f,o,b,l))}
void Engine::on_file_request(Tox*,uint32_t n,uint32_t f,uint64_t o,size_t l,void*p){SAFE_FILE(e.file_request(n,f,o,l))}
void Engine::on_file_control(Tox*,uint32_t n,uint32_t f,Tox_File_Control c,void*p){SAFE_FILE(e.file_control(n,f,c))}
}
