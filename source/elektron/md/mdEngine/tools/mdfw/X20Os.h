// md-drums: MDX X.20's OS file, which the user supplies at run time (never in the repository, a release or a test).
// Decoded as .scratch/user-machines/research/01-decoding-x20.md describes it: section 0 holds a stage-1 stream that
// unpacks to a loader, whose S2MD header lists four payloads, each checked by its CRC-32: the voice DSP program and the
// mixer DSP program (DSU containers, raw LZMA1), the 68k OS (raw LZMA1) and a DSP kernel (a 1.63-style section).
#pragma once
#include "Firmware.h"

#include <cstdint>
#include <vector>

namespace md::fw {

struct X20Os {
    static constexpr uint32_t OsBase = 0x200000;   // where the loader puts the 68k OS
    DspImage voice;                // payload 1, for the voice DSP (DSP2)
    DspImage mixer;                // payload 2, for the mixer DSP (DSP1)
    std::vector<uint8_t> os;       // payload 3, the 68k OS from OsBase
    DspImage kernel;               // payload 4, the loader's DSP kernel
};

// The OS file's bytes as the user gives them (.syx). Throws FirmwareError saying why a file is not an X.20 OS: no
// Machinedrum OS messages, no S2MD header (OS 1.63 and older have none), a CRC that does not match.
X20Os parseX20Os(const std::vector<uint8_t>& syx);

// A DSU container's words (exposed for the tests): its records in order, each record's words byte-planed and summed
// back once (mode 0), not at all (mode 1) or twice (mode 2); other modes are kept as stored. Checks the LZMA output
// against the container's own CRC. _tableAndData, when given, receives the record table and the LZMA output, whose
// size the S2MD header holds.
DspImage parseDsu(const std::vector<uint8_t>& _blob, std::vector<uint8_t>* _tableAndData = nullptr);

uint32_t crc32(const uint8_t* _data, size_t _size);

} // namespace md::fw
