// SPDX-License-Identifier: GPL-3.0-only
#include "scanner.hpp"
#include "qr.hpp"
#include "focus.hpp"
#include "scan_image.hpp"
#include "../../camera/src/frame.hpp"
#include <linux/videodev2.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
namespace chat {
namespace {
using Clock=std::chrono::steady_clock;
int ctl(int fd,unsigned long req,void*arg){int n;do{n=ioctl(fd,req,arg);}while(n<0&&errno==EINTR);return n;}
void check(int n,const char*what){if(n<0)throw std::runtime_error(std::string(what)+": "+strerror(errno));}
// This path uses the same ispvideo NV12 stride correction as the photo app,
// but keeps only luminance and never writes a photo or plays a shutter sound.
class Capture {
    struct Buffer{void*data=nullptr;size_t size=0;}buffers_[3];
    int fd_=-1;bool streaming_=false;
    unsigned count_=0,w_=0,h_=0,stride_=0;
    Clock::time_point last_=Clock::now();
public:
    ~Capture(){
        if(fd_<0)return;
        if(streaming_){v4l2_buf_type type=V4L2_BUF_TYPE_VIDEO_CAPTURE;ctl(fd_,VIDIOC_STREAMOFF,&type);}
        for(auto&b:buffers_)if(b.data)munmap(b.data,b.size);
        v4l2_requestbuffers req{};req.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;req.memory=V4L2_MEMORY_MMAP;ctl(fd_,VIDIOC_REQBUFS,&req);close(fd_);
    }
    void open(){
        fd_=::open("/dev/video4",O_RDWR|O_NONBLOCK|O_CLOEXEC);check(fd_,"Open camera");
        v4l2_capability cap{};check(ctl(fd_,VIDIOC_QUERYCAP,&cap),"QUERYCAP");
        uint32_t caps=(cap.capabilities&V4L2_CAP_DEVICE_CAPS)?cap.device_caps:cap.capabilities;
        if(!(caps&V4L2_CAP_VIDEO_CAPTURE)||!(caps&V4L2_CAP_STREAMING))throw std::runtime_error("Camera cannot stream");
        v4l2_format fmt{};fmt.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;auto&pix=fmt.fmt.pix;
        // Half of the ISP's active mode; retain native detail for cropped QR scans.
        pix.width=1024;pix.height=972;pix.pixelformat=V4L2_PIX_FMT_NV12;pix.field=V4L2_FIELD_ANY;
        check(ctl(fd_,VIDIOC_S_FMT,&fmt),"S_FMT");w_=pix.width;h_=pix.height;
        if(w_<160||h_<120||w_>2048||h_>1944||(w_&1)||(h_&1)||(pix.pixelformat!=V4L2_PIX_FMT_NV12&&pix.pixelformat!=V4L2_PIX_FMT_NV21))throw std::runtime_error("Unsupported camera mode");
        stride_=camera::nv12_stride(w_,h_,pix.bytesperline,pix.sizeimage,strncmp((char*)cap.driver,"ispvideo",sizeof cap.driver)==0);
        if(stride_>8192||pix.sizeimage<camera::nv12_size(stride_,h_))throw std::runtime_error("Unsupported camera layout");
        fprintf(stderr,"[tox scan] %ux%u stride=%u\n",w_,h_,stride_);
        v4l2_requestbuffers req{};req.count=3;req.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;req.memory=V4L2_MEMORY_MMAP;
        check(ctl(fd_,VIDIOC_REQBUFS,&req),"REQBUFS");if(req.count<2)throw std::runtime_error("Too few camera buffers");count_=std::min(req.count,3u);
        for(unsigned i=0;i<count_;i++){
            v4l2_buffer b{};b.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;b.memory=V4L2_MEMORY_MMAP;b.index=i;check(ctl(fd_,VIDIOC_QUERYBUF,&b),"QUERYBUF");
            if(b.length<camera::nv12_size(stride_,h_))throw std::runtime_error("Camera buffer too small");
            void*p=mmap(nullptr,b.length,PROT_READ|PROT_WRITE,MAP_SHARED,fd_,b.m.offset);if(p==MAP_FAILED)check(-1,"mmap");buffers_[i]={p,b.length};check(ctl(fd_,VIDIOC_QBUF,&b),"QBUF");
        }
        v4l2_buf_type type=V4L2_BUF_TYPE_VIDEO_CAPTURE;check(ctl(fd_,VIDIOC_STREAMON,&type),"STREAMON");streaming_=true;last_=Clock::now();
    }
    bool frame(std::vector<uint8_t>&gray,unsigned&w,unsigned&h){
        pollfd p{fd_,POLLIN,0};int n=poll(&p,1,50);
        if(n<0&&errno!=EINTR)check(n,"poll");
        if(n>0&&(p.revents&(POLLERR|POLLHUP|POLLNVAL)))throw std::runtime_error("Camera stream stopped");
        if(Clock::now()-last_>std::chrono::seconds(4))throw std::runtime_error("Camera frame timeout");
        if(n<=0)return false;
        v4l2_buffer b{};b.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;b.memory=V4L2_MEMORY_MMAP;
        if(ctl(fd_,VIDIOC_DQBUF,&b)<0){if(errno==EAGAIN)return false;check(-1,"DQBUF");}
        if(b.index>=count_||b.bytesused<camera::nv12_size(stride_,h_))throw std::runtime_error("Incomplete camera frame");
        bool valid=!(b.flags&V4L2_BUF_FLAG_ERROR);
        if(valid){
            w=w_;h=h_;gray.resize(size_t(w)*h);
            auto*src=static_cast<const uint8_t*>(buffers_[b.index].data);
            for(unsigned y=0;y<h;y++)memcpy(gray.data()+size_t(y)*w,src+size_t(y)*stride_,w);
            last_=Clock::now();
        }
        check(ctl(fd_,VIDIOC_QBUF,&b),"QBUF");return valid;
    }
};
}
Scanner::Scanner(const std::string& own){worker_=std::thread([this,own]{run(own);});}
Scanner::~Scanner(){stop_=true;if(worker_.joinable())worker_.join();}
bool Scanner::snapshot(ScanView&result){std::lock_guard<std::mutex>lock(mutex_);if(result.revision==view_.revision)return false;result=view_;return true;}
void Scanner::run(const std::string& own){
    try{
        Capture camera;camera.open();FocusMotor motor;bool focus_ok=motor.open();FocusSearch search;
        QrDecoder decoder,detail_decoder;std::vector<uint8_t> gray,region;unsigned w=0,h=0,seen_focus=0;
        auto started=Clock::now(),last_preview=Clock::time_point{},last_decode=Clock::time_point{},focus_ready=started;
        int samples=0;double sample_sum=0;std::string focus_note=focus_ok?"准备自动对焦…":"对焦不可用，仍可扫码";
        std::string last_error;auto hint_until=Clock::time_point{};
        auto move=[&](int p){
            if(!motor.move(p)){focus_ok=false;search.cancel();focus_note="对焦控制失败，仍可扫码";return false;}
            focus_ready=Clock::now()+std::chrono::milliseconds(250);samples=0;sample_sum=0;return true;
        };
        while(!stop_){
            if(!camera.frame(gray,w,h))continue;
            auto now=Clock::now();unsigned zoom=zoom_.load();auto crop=scan_crop(w,h,zoom);
            unsigned request=focus_request_.load();int delta=focus_delta_.exchange(0);
            if(delta){
                seen_focus=request;search.cancel();
                if(focus_ok&&move(motor.position()+std::clamp(delta,-256,256)))focus_note="手动对焦";
            }else if(request!=seen_focus&&now-started>=std::chrono::milliseconds(500)){
                seen_focus=request;if(focus_ok){search.start(motor.minimum(),motor.maximum(),motor.position());if(move(search.target()))focus_note="自动对焦中…";}
            }
            bool stable=now>=focus_ready;
            if(search.active()&&stable){
                sample_sum+=focus_score(gray.data(),w,crop);
                if(++samples>=2){
                    if(search.sample(sample_sum/samples))move(search.target());
                    else{bool confident=search.confident();move(search.result());if(focus_ok)focus_note=confident?"已调焦，可 W/S 微调":"纹理不足，请对准二维码后按 F";}
                }
            }
            std::string found;
            if(stable&&now-last_decode>=std::chrono::milliseconds(300)){
                last_decode=now;unsigned rw=0,rh=0;
                crop_gray(gray.data(),w,crop,region,512,rw,rh);
                auto payloads=decoder.decode(region.data(),rw,rh,rw);
                // Decode actual high-resolution samples on failure. At 2x the
                // crop itself is 512x486, so the fast path already has all detail.
                if(payloads.empty()&&std::max(crop.w,crop.h)>512&&!stop_){
                    crop_gray(gray.data(),w,crop,region,1024,rw,rh);payloads=detail_decoder.decode(region.data(),rw,rh,rw);
                }
                for(const auto&payload:payloads){std::string id,error;
                    if(parse_tox_qr(payload,id,error,own)){found=std::move(id);break;}
                    last_error=std::move(error);hint_until=now+std::chrono::seconds(3);
                }
            }
            if(found.empty()&&now-last_preview<std::chrono::milliseconds(100))continue;last_preview=now;
            ScanView next;scan_preview(gray.data(),w,crop,next.pixels,ScanView::width,ScanView::height);
            next.id=std::move(found);next.zoom=zoom;next.capture_width=w;next.capture_height=h;next.focus_available=focus_ok;next.focusing=search.active();
            next.focus_status=focus_note;if(focus_ok)next.focus_status+=" · 位置 "+std::to_string(motor.percent())+"%";
            next.status=next.id.empty()?(now<hint_until?last_error:"对准中央；识别后还需回车确认"):"已识别，请确认好友 ID";
            bool done=!next.id.empty();{std::lock_guard<std::mutex>lock(mutex_);next.revision=view_.revision+1;view_=std::move(next);}
            if(done)return; // Restore focus and close camera before confirmation.
        }
    }catch(const std::exception&e){
        fprintf(stderr,"[tox scan] %s\n",e.what());std::lock_guard<std::mutex>lock(mutex_);view_.status="摄像头不可用，请返回后重试";view_.focus_status="";view_.failed=true;view_.revision++;
    }
}
}
