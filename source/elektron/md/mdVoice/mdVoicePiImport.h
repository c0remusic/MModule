// The owner's import of the P-I machines' words (spec 11.4): everything PiBank names, from the flash image, in one call.
// Shared::pi points at what it returns, and OsTables takes it too. The flash image is read here and never by a render.
#pragma once

#include "Firmware.h"
#include "mdVoice.h"

#include <algorithm>
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

	// The sampled waveforms TrxTables holds, from the voice program (the same owner's import, spec 11.4)
	inline void importTrx(const fw::Firmware& _fw, TrxTables& _tables)
	{
		// The whole table, or its words from _first on (the firmware loads nothing between TRX-CH's waves)
		const auto copy = [&](auto& _table, const uint32_t _addr, const uint32_t _first = 0, uint32_t _count = 0)
		{
			if(_count == 0)
				_count = static_cast<uint32_t>(_table.size());
			const auto w = fw::dspWords(_fw.dspA, _addr + _first, _count);
			std::copy(w.begin(), w.end(), _table.begin() + _first);
		};
		using T = TrxTables;
		copy(_tables.bdWaveA, T::BdWaveAAddr);
		copy(_tables.bdWaveB, T::BdWaveBAddr);
		copy(_tables.sdWave, T::SdWaveAddr);
		copy(_tables.xtWave, T::XtWaveAddr);
		copy(_tables.xcWave, T::XcWaveAddr);
		copy(_tables.rsWave, T::RsWaveAddr);
		for(const auto& w : T::ChWaves)
			copy(_tables.chWaves, T::ChWavesAddr, w.offset, w.size);
	}
}
