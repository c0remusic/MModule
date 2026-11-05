// The owner's import of the P-I machines' words (spec 11.4): everything PiBank names, from the flash image, in one call.
// Shared::pi points at what it returns, and OsTables takes it too. The flash image is read here and never by a render.
#pragma once

#include "Firmware.h"
#include "mdVoice.h"

#include <cstdint>
#include <vector>

namespace md::voice
{
	inline std::vector<uint32_t> importPi(const fw::FlashOs& _os)
	{
		std::vector<uint32_t> words;
		words.reserve(PiBank::Words);
		for(const auto& range : PiBank::Ranges)
		{
			const auto w = fw::dspWords(_os.firmware.dspA, range.addr, range.words);
			words.insert(words.end(), w.begin(), w.end());
		}
		constexpr uint32_t osImageAddr = 0x200000;
		const uint8_t* table = _os.osImage.data() + (PiBank::OnePoleAddr - osImageAddr);
		for(uint32_t k = 0; k < PiBank::OnePoleWords; ++k, table += 4)
			words.push_back(uint32_t(table[0]) << 24 | uint32_t(table[1]) << 16 | uint32_t(table[2]) << 8 | table[3]);
		return words;
	}
}
