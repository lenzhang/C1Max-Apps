// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace chat {
class Media {
    struct Impl;std::unique_ptr<Impl>impl_;
public:
    std::vector<uint32_t>pixels;unsigned width=0,height=0,seconds=0;
    std::string file,kind,status;bool ready=false,recording=false,camera=false,playing=false;
    explicit Media(std::string folder);
    ~Media();
    void open_camera();void capture();void record();void finish_record();void play();
    void load(const std::string&name,const std::string&type);
    void poll();void close();void keep();
};
}
