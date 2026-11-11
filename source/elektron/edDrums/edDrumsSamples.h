#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace elektron::drums
{
	// A user's sample as the Sampler plays it with the Machinedrum's grain: through the ROM machines' player, as a UW
	// takes a sample (ticket 20 of .scratch/elektron-drums-v1). Its 16-bit values, which the state keeps, and from them
	// the player's words: two 12-bit codes each, the first in the high half, each code the one whose A-law value
	// (md::voice::RomTables::decode, a formula, no firmware) is nearest. Its rate kept, as a fraction of 44.1 kHz. No loop.
	struct Sample
	{
		// A Track's room in the engine's bank, a few words of silence after the sample included
		static constexpr uint32_t SlotWords = 0x7000, PadWords = 128;
		static constexpr uint32_t MaxWords = SlotWords - PadWords;
		static constexpr uint32_t MaxLength = 2 * MaxWords;		// 57088 samples: 1.29 s at 44.1 kHz, what the ROM player plays
		// What the Sampler's VINTAGE grain plays (edDrumsVintage.h), from the 16-bit values: 10 s at 48 kHz
		static constexpr uint32_t MaxSourceLength = 480000;
		static constexpr double MinRate = 4000.0, MaxRate = 48000.0;
		static constexpr uint32_t Unity = 1u << 18;				// the rate word of 44.1 kHz

		std::string name;
		double rate = 44100.0;
		std::vector<int16_t> pcm;		// at most MaxSourceLength
		std::vector<uint32_t> words;	// of the first MaxLength values at most: (that + 1) / 2
		// The same values, never changed once made, which every copy of the Sample shares: what the engine's VINTAGE grain
		// reads where it lies (Engine::setSample), no copy on the audio thread
		std::shared_ptr<const std::vector<int16_t>> source;
		uint32_t rateWord = Unity;
	};

	// _mono (full scale 1.0) at _rate, cut to Sample::MaxSourceLength. An error that says why for no audio or a rate out of
	// MinRate to MaxRate
	std::variant<Sample, std::string> makeSample(std::string _name, const std::vector<float>& _mono, double _rate);

	// What the state keeps of a Sample: its name, rate and 16-bit values; and the Sample again, nullopt for bytes that
	// are not one
	std::vector<uint8_t> sampleToBytes(const Sample& _sample);
	std::optional<Sample> sampleFromBytes(const uint8_t* _data, size_t _size);
}
