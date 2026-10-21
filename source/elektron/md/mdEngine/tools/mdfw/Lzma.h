// md-drums: an LZMA1 decoder for raw streams (no .lzma or .xz header), as MDX X.20's OS file packs its payloads.
// Written from the LZMA specification (Igor Pavlov's LzmaSpec.cpp, public domain); nothing of X.20 is in it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace md::fw {

struct LzmaProperties {
    uint32_t lc = 3, lp = 0, pb = 2;   // literal context bits, literal position bits, position bits
};

// Decodes _size bytes of a raw LZMA1 stream into exactly _unpacked bytes; an end marker before that ends the output
// early. Throws FirmwareError on a corrupt stream or properties out of range.
std::vector<uint8_t> lzmaDecode(const uint8_t* _data, size_t _size, size_t _unpacked, LzmaProperties _props);

} // namespace md::fw
