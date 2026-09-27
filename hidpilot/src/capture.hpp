#pragma once
#include "../../camera/src/frame.hpp"
#include <linux/videodev2.h>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
namespace hidpilot {
inline uint32_t capture_tick(){return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct Mapped { void* data = nullptr; size_t size = 0; };
class Capture {
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
    ~Capture() { close(); }
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
        streaming_ = true; last_frame_ = capture_tick(); return true;
    }
    int frame(std::string& error) {
        if (fd_ < 0) return -1;
        pollfd p{fd_, POLLIN, 0}; int n = poll(&p, 1, 0);
        if (n < 0 && errno != EINTR) { fail(error, "poll"); return -1; }
        if (n <= 0) {
            if (capture_tick() - last_frame_ > 4000) { error = "Camera frame timeout"; close(); return -1; }
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
            if (capture_tick() - last_frame_ > 4000) { error = "Camera repeatedly returned damaged frames"; close(); return -1; }
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
        last_frame_ = capture_tick(); return 1;
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
