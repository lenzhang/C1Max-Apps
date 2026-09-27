#pragma once
#include <cstdint>
#include <vector>
#include <stdexcept>
namespace moonpilot {
std::vector<uint8_t> jpeg(const std::vector<uint32_t>&rgb,unsigned w,unsigned h);
}
