#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// FMD CHORD's waves, read from the user's own Model:Cycles OS 1.13 at load: they are the OS's data, which neither the
// repository nor the plug-in ships. The file is the OS's section 3 (the main CPU's code and data, loaded at
// 0x40000400) as elektron-work keeps it, `section_3_MAIN_OS.bin`. CHORD reads its waves through a table of 32
// pointers (0x40118590, SWEP's index 1-32, the 32nd the 31st again): the first into the section itself, the others
// into SRAM, which the boot (0x4000045c) fills from two images in the section, each a source, its end and a
// destination, the rest of its 32 KB cleared.
namespace cycles::fast
{
	struct ChordWaves
	{
		static constexpr int Count = 32;
		static constexpr int Entries = 257;	// 256 and the first again

		// SWEP index 1-32's waves (index 0 is the sine), Q31 words as fractions
		std::array<std::array<float, Entries>, Count> wave{};
		// Where the OS reads each: two indices at one address are one wave (CHORD's crossfade compares them)
		std::array<uint32_t, Count> address{};
	};

	// OS 1.13's section 3: its size and its FNV-1a 64 hash
	constexpr size_t ChordSection3Size = 1744192;
	constexpr uint64_t ChordSection3Fnv = 0xa0cb9bff738a7ebeull;

	// The waves from section 3's bytes; false with _error saying why when they are not OS 1.13's section 3
	bool readChordWaves(const std::vector<uint8_t>& _section3, ChordWaves& _waves, std::string& _error);
	// The same from the file at _path
	bool loadChordWaves(const std::string& _path, ChordWaves& _waves, std::string& _error);
}
