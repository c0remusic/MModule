#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "stMainMemory.h"

// The user's Syntakt OS 1.41 update file (.syx) read as the plug-in needs it: sections 3 (CPU #1's OS) and 7 (the
// voice CPU's), nothing else, and nothing of them shipped. The file's layers, outermost first (the format
// mischa85/elektron-firmware-tool reads, MIT): SysEx messages of 126 bytes between F0 and F7, each 116 bytes of 8-in-7
// data after a 9-byte header; an ELE3 container in the decoded stream, its section table of 16-byte entries (id,
// offset, length, load address) at +0x20, their count at +0x1c; each section either packed, an 8-byte header
// [length, byte sum] before an aPLib-style LZ stream, or stored as it is.
namespace stDrums
{
	struct Sections
	{
		std::vector<uint8_t> section3;
		std::vector<uint8_t> section7;
	};

	// Sections 3 and 7 of the .syx _syx, decoded; false with _error saying why when the file is not one
	bool unpack(const std::vector<uint8_t>& _syx, Sections& _sections, std::string& _error);

	// What syntakt::Syntakt is made from
	struct Firmware
	{
		syntakt::main::Section3 section3;
		std::vector<int32_t> section7;
	};

	// Sections 3 and 7 of _syx, which must be OS 1.41's (the FNV-1a hashes stSyntakt's loaders check)
	bool load(const std::vector<uint8_t>& _syx, Firmware& _firmware, std::string& _error);
}
