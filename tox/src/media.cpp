// SPDX-License-Identifier: GPL-3.0-only
#include "media.hpp"
#include "net.hpp"
#include "../../camera/src/frame.hpp"
#include "../../camera/src/album.hpp"
#include "../../moonpilot/src/process.hpp"
#include <linux/videodev2.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../camera/src/stb_image_write.h"
namespace chat { namespace {
uint32_t media_tick(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
struct Mapped { void* data = nullptr; size_t size = 0; };
class V4L2Camera {
    int fd_ = -1;
    Mapped buffers_[3];
    unsigned count_ = 0, w_ = 640, h_ = 480, stride_ = 640;
    bool streaming_ = false;
    uint32_t format_ = V4L2_PIX_FMT_NV12, last_frame_ = 0;
    std::vector<uint8_t> latest_;
    static int ctl(int fd, unsigned long req, void* arg) {
        int r; do { r = ioctl(fd, req, arg); } while (r < 0 && errno == EINTR); return r;
    }
    bool fail(std::string& error, const char* stage) {
        error = std::string(stage) + ": " + strerror(errno);
        fprintf(stderr, "[camera] %s\n", error.c_str()); close(); return false;
    }
public:
    ~V4L2Camera() { close(); }
    void close() {
        std::vector<uint8_t>().swap(latest_);
        if (fd_ < 0) return;
        if (streaming_) { v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE; ctl(fd_, VIDIOC_STREAMOFF, &t); }
        streaming_ = false;
        for (auto& b : buffers_) { if (b.data) munmap(b.data, b.size); b = {}; }
        count_ = 0;
        v4l2_requestbuffers req{}; req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; req.memory = V4L2_MEMORY_MMAP;
        ctl(fd_, VIDIOC_REQBUFS, &req); ::close(fd_); fd_ = -1;
    }
    bool open(std::string& error) {
        close(); fd_ = ::open("/dev/video4", O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) return fail(error, "Open /dev/video4");
        v4l2_capability cap{};
        if (ctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) return fail(error, "QUERYCAP");
        uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
        if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
            error = "Camera does not support streaming capture"; close(); return false;
        }
        v4l2_format fmt{}; fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        // Half of the ISP's 2048x1944 active mode, preserving its geometry.
        // Convert only display samples in the live view, full RGB at shutter.
        fmt.fmt.pix.width = 1024; fmt.fmt.pix.height = 972;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12; fmt.fmt.pix.field = V4L2_FIELD_ANY;
        if (ctl(fd_, VIDIOC_S_FMT, &fmt) < 0) return fail(error, "S_FMT");
        auto& pix = fmt.fmt.pix; w_ = pix.width; h_ = pix.height; format_ = pix.pixelformat;
        if (w_ < 160 || h_ < 120 || w_ > 2048 || h_ > 1944 || (w_ & 1) || (h_ & 1) ||
            (format_ != V4L2_PIX_FMT_NV12 && format_ != V4L2_PIX_FMT_NV21)) {
            error = "Unsupported camera mode"; close(); return false;
        }
        stride_ = camera::nv12_stride(w_, h_, pix.bytesperline, pix.sizeimage,
                                     strncmp((const char*)cap.driver, "ispvideo", sizeof cap.driver) == 0);
        fprintf(stderr, "[camera] %.16s %ux%u bpl=%u stride=%u size=%u\n",
                cap.driver, w_, h_, pix.bytesperline, stride_, pix.sizeimage);
        if (stride_ > 8192 || pix.sizeimage < camera::nv12_size(stride_, h_)) {
            error = "Unsupported NV12 plane layout"; close(); return false;
        }
        v4l2_requestbuffers req{}; req.count = 3; req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; req.memory = V4L2_MEMORY_MMAP;
        if (ctl(fd_, VIDIOC_REQBUFS, &req) < 0) return fail(error, "REQBUFS");
        if (req.count < 2) { error = "Not enough camera buffers"; close(); return false; }
        count_ = std::min<unsigned>(req.count, 3);
        for (unsigned i = 0; i < count_; ++i) {
            v4l2_buffer b{}; b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory = V4L2_MEMORY_MMAP; b.index = i;
            if (ctl(fd_, VIDIOC_QUERYBUF, &b) < 0) return fail(error, "QUERYBUF");
            if (b.length < camera::nv12_size(stride_, h_)) { error = "Camera buffer is too small"; close(); return false; }
            buffers_[i].size = b.length;
            buffers_[i].data = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, b.m.offset);
            if (buffers_[i].data == MAP_FAILED) { buffers_[i].data = nullptr; return fail(error, "mmap"); }
            if (ctl(fd_, VIDIOC_QBUF, &b) < 0) return fail(error, "QBUF");
        }
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ctl(fd_, VIDIOC_STREAMON, &type) < 0) return fail(error, "STREAMON");
        streaming_ = true; last_frame_ = media_tick(); return true;
    }
    int frame(std::string& error) {
        if (fd_ < 0) return -1;
        pollfd p{fd_, POLLIN, 0}; int n = poll(&p, 1, 0);
        if (n < 0 && errno != EINTR) { fail(error, "poll"); return -1; }
        if (n <= 0) {
            if (media_tick() - last_frame_ > 4000) { error = "Camera frame timeout"; close(); return -1; }
            return 0;
        }
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) { error = "Camera stream stopped"; close(); return -1; }
        v4l2_buffer b{}; b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory = V4L2_MEMORY_MMAP;
        if (ctl(fd_, VIDIOC_DQBUF, &b) < 0) {
            if (errno == EAGAIN) return 0;
            fail(error, "DQBUF"); return -1;
        }
        if (b.index >= count_) { error = "Invalid camera buffer index"; close(); return -1; }
        if (b.flags & V4L2_BUF_FLAG_ERROR) {
            if (ctl(fd_, VIDIOC_QBUF, &b) < 0) { fail(error, "QBUF"); return -1; }
            if (media_tick() - last_frame_ > 4000) { error = "Camera repeatedly returned damaged frames"; close(); return -1; }
            return 0;
        }
        const auto& buffer = buffers_[b.index];
        const size_t needed = camera::nv12_size(stride_, h_);
        if (b.bytesused < needed || buffer.size < needed) {
            fprintf(stderr, "[camera] short frame used=%u mapped=%zu stride=%u\n", b.bytesused, buffer.size, stride_);
            error = "Camera produced an incomplete frame"; close(); return -1;
        }
        // Retain an immutable latest frame after QBUF; reading a queued mmap
        // while ISP writes it would tear the preview and saved photo.
        latest_.assign((const uint8_t*)buffer.data, (const uint8_t*)buffer.data + needed);
        if (ctl(fd_, VIDIOC_QBUF, &b) < 0) { fail(error, "QBUF"); return -1; }
        last_frame_ = media_tick(); return 1;
    }
    // Dimensions of the clockwise-rotated RGB frame, not the raw NV12 sensor.
    unsigned width() const { return h_; }
    unsigned height() const { return w_; }
    bool has_frame() const { return !latest_.empty(); }
    uint32_t pixel(unsigned x, unsigned y) const {
        return camera::nv12_pixel(latest_.data(), stride_, h_, y, h_ - 1 - x, format_ == V4L2_PIX_FMT_NV21);
    }
    bool decode(std::vector<uint32_t>& pixels) const {
        return camera::decode_nv12(latest_.data(), latest_.size(), latest_.size(),
                                    w_, h_, stride_, format_ == V4L2_PIX_FMT_NV21, pixels, true);
    }
};

}
struct Media::Impl {std::string folder;V4L2Camera camera;moonpilot::Process recorder,player;uint32_t started=0,last_frame=0;bool temporary=false;explicit Impl(std::string f):folder(std::move(f)){};};
namespace {
std::string fresh_media(const std::string&folder,const char*ext){
    uint64_t size=0;DIR*d=opendir(folder.c_str());if(!d)throw std::runtime_error("附件目录不可用");while(auto*e=readdir(d)){struct stat s{};if(!lstat((folder+"/"+e->d_name).c_str(),&s)&&S_ISREG(s.st_mode))size+=s.st_size;}closedir(d);if(size>60*1024*1024)throw std::runtime_error("附件已占用 60 MB，请先清理");
    std::string p=folder+"/capture-XXXXXX";std::vector<char>b(p.begin(),p.end());b.push_back(0);int fd=mkstemp(b.data());if(fd<0)throw std::runtime_error("无法创建附件");close(fd);p=b.data();std::string final=p+ext;if(rename(p.c_str(),final.c_str())){unlink(p.c_str());throw std::runtime_error("无法创建附件");}return final.substr(folder.size()+1);
}
void check_voice(const std::string&path){
    auto data=c1::read_file(path,4*1024*1024);if(data.size()<44||data.compare(0,4,"RIFF")||data.compare(8,4,"WAVE"))throw std::runtime_error("录音不是 WAV 音频");
    auto le16=[&](size_t p){return uint16_t(uint8_t(data[p])|(uint16_t(uint8_t(data[p+1]))<<8));};auto le32=[&](size_t p){return uint32_t(le16(p))|(uint32_t(le16(p+2))<<16);};
    bool fmt=false,pcm=false;uint32_t byte_rate=0;
    for(size_t p=12;p+8<=data.size();){uint32_t n=le32(p+4);if(n>data.size()-p-8)throw std::runtime_error("录音数据不完整");
        if(data.compare(p,4,"fmt ")==0){if(n<16||le16(p+8)!=1||le16(p+10)<1||le16(p+10)>2||le32(p+12)<8000||le32(p+12)>48000||le16(p+22)!=16)throw std::runtime_error("仅支持 8–48 kHz、16 位 PCM WAV");byte_rate=le32(p+16);if(byte_rate!=le32(p+12)*le16(p+10)*2)throw std::runtime_error("无效 WAV 采样格式");fmt=true;}
        if(data.compare(p,4,"data")==0){if(!fmt||n<1600||n>byte_rate*120)throw std::runtime_error("语音过短或超过两分钟");pcm=true;}p+=8+n+(n&1);
    }if(!pcm)throw std::runtime_error("录音没有有效声音数据");
}
}
Media::Media(std::string folder):impl_(new Impl(std::move(folder))){mkdir(impl_->folder.c_str(),0700);}
Media::~Media(){close();}
void Media::close(){impl_->camera.close();impl_->recorder.stop();impl_->player.stop();if(impl_->temporary&&!file.empty())unlink((impl_->folder+"/"+file).c_str());impl_->temporary=false;file.clear();pixels.clear();ready=recording=camera=playing=false;}
void Media::keep(){impl_->temporary=false;}
void Media::open_camera(){close();std::string error;if(!impl_->camera.open(error))throw std::runtime_error(error);camera=true;kind="photo";status="对准画面，按拍摄键或空格拍照";}
void Media::capture(){
    if(!camera||!impl_->camera.has_frame())throw std::runtime_error("等待相机画面");
    const unsigned w=640,h=480;std::vector<uint8_t>rgb(w*h*3);auto&c=impl_->camera;
    unsigned cw=std::min(c.width(),c.height()*4/3),ch=cw*3/4,x0=(c.width()-cw)/2,y0=(c.height()-ch)/2;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){auto p=c.pixel(x0+x*cw/w,y0+y*ch/h);auto i=(size_t(y)*w+x)*3;rgb[i]=p>>16;rgb[i+1]=p>>8;rgb[i+2]=p;}
    file=fresh_media(impl_->folder,".jpg");impl_->temporary=true;std::string encoded;auto writer=[](void*p,void*d,int n){static_cast<std::string*>(p)->append(static_cast<char*>(d),n);};
    if(!stbi_write_jpg_to_func(writer,&encoded,w,h,3,rgb.data(),85))throw std::runtime_error("照片编码失败");c1::save_private(impl_->folder+"/"+file,encoded);
    impl_->camera.close();camera=false;std::string error;if(!camera::load_photo(impl_->folder+"/"+file,460,225,pixels,width,height,error))throw std::runtime_error(error);ready=true;status="照片已拍好，确认后发送";
}
void Media::record(){close();file=fresh_media(impl_->folder,".wav");impl_->temporary=true;kind="voice";impl_->recorder.start({"/usr/bin/arecord","-q","-D","plughw:0,1","-f","S16_LE","-r","16000","-c","1","-t","wav","-d","30",impl_->folder+"/"+file},impl_->folder+"/record.log");recording=true;seconds=0;impl_->started=media_tick();status="正在录音，再按回车结束（最长 30 秒）";}
void Media::finish_record(){if(recording){impl_->recorder.finish_recording();status="正在保存录音…";}}
void Media::play(){if(playing){impl_->player.stop();playing=false;return;}if(!ready||kind!="voice")return;check_voice(impl_->folder+"/"+file);impl_->player.start({"/usr/bin/mplayer","-noconfig","all","-quiet","-noconsolecontrols","-nolirc","-nojoystick","-nomouseinput","-vo","null","-ao","media",impl_->folder+"/"+file},impl_->folder+"/play.log");playing=true;}
void Media::load(const std::string&name,const std::string&type){close();if(name.empty()||name.find("..")!=std::string::npos||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")!=std::string::npos)throw std::runtime_error("无效的附件");file=name;kind=type;std::string error;if(type=="photo"){if(!camera::load_photo(impl_->folder+"/"+file,460,225,pixels,width,height,error))throw std::runtime_error(error);}else check_voice(impl_->folder+"/"+file);ready=true;status="已接收的附件";}
void Media::poll(){
    if(camera&&media_tick()-impl_->last_frame>90){impl_->last_frame=media_tick();std::string error;int r=impl_->camera.frame(error);if(r<0){camera=false;throw std::runtime_error(error);}if(r>0){width=300;height=225;pixels.resize(width*height);auto&c=impl_->camera;unsigned cw=std::min(c.width(),c.height()*4/3),ch=cw*3/4,x0=(c.width()-cw)/2,y0=(c.height()-ch)/2;for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++)pixels[size_t(y)*width+x]=c.pixel(x0+x*cw/width,y0+y*ch/height);}}
    if(recording){seconds=(media_tick()-impl_->started)/1000;if(impl_->recorder.poll()){recording=false;check_voice(impl_->folder+"/"+file);ready=true;status="录音已保存，可以试听后发送";}}
    if(playing&&impl_->player.poll()){playing=false;if(impl_->player.result)throw std::runtime_error("音频播放失败，请重试");}
}
}
