// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct quirc;
namespace chat {
// Only raw Tox addresses and standard tox: URIs; never opens URLs.
bool parse_tox_qr(const std::string& payload, std::string& id, std::string& error,
                  const std::string& own_id = {});
struct QrImage { unsigned size=0; std::vector<uint32_t> pixels; };
QrImage make_tox_qr(const std::string& id, unsigned maximum=234);
class QrDecoder {
    quirc* decoder_=nullptr;
    unsigned width_=0,height_=0;
public:
    QrDecoder();
    ~QrDecoder();
    QrDecoder(const QrDecoder&)=delete;
    QrDecoder& operator=(const QrDecoder&)=delete;
    // Bounded grayscale frame. Throws on allocation failure/invalid dimensions.
    std::vector<std::string> decode(const uint8_t* gray,unsigned w,unsigned h,unsigned stride);
};
}
