// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace chat {
struct ScanView {
    uint64_t revision=0;
    std::string status="正在打开摄像头…",id;
    bool failed=false,focus_available=false,focusing=false;
    unsigned zoom=100,capture_width=0,capture_height=0;
    std::string focus_status="正在检查对焦…";
    // Upright preview, kept independent of camera mmap buffers.
    static constexpr unsigned width=236,height=236;
    std::vector<uint32_t> pixels;
};
class Scanner {
    std::atomic<bool> stop_{false};
    std::atomic<unsigned> zoom_{100},focus_request_{1};
    std::atomic<int> focus_delta_{0};
    std::mutex mutex_;
    ScanView view_;
    std::thread worker_;
    void run(const std::string& own_id);
public:
    explicit Scanner(const std::string& own_id);
    ~Scanner();
    Scanner(const Scanner&)=delete;
    Scanner& operator=(const Scanner&)=delete;
    bool snapshot(ScanView& result);
    void set_zoom(unsigned zoom){zoom_=zoom==150?150:zoom==200?200:100;++focus_request_;}
    void autofocus(){++focus_request_;}
    void adjust_focus(int direction){focus_delta_.fetch_add(direction>0?32:-32);}
};
}
