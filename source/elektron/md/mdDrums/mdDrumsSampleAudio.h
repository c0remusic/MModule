#pragma once

#include "mdDrumsSamples.h"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace mdDrums
{
	// A file's audio as the editor reads it for a Sample (ticket 09 of the Samples map): mono, at the file's rate (the
	// reader takes a rate above MaxRate down to 44.1 kHz), full scale 1.0, and its first loop, if it has one
	struct SampleAudio
	{
		static constexpr double MinRate = 4000.0;
		static constexpr double MaxRate = 48000.0;

		std::vector<float> mono;
		double rate = 44100.0;
		int64_t loopStart = -1;	// the loop's first and last samples, or -1
		int64_t loopEnd = -1;
	};

	// The Sample _audio becomes, as a Machinedrum UW takes a sample (ticket 04): its rate kept, as a fraction of 44.1 kHz;
	// its values to 16 bits as they come, then to the voice DSP's 12-bit codes through the OS's table (RomBank::codes);
	// its loop if it lasts at least 151 samples, the Sample then ending with it. An error that says why for a rate out
	// of 4 to 48 kHz, an empty file, or one longer than _roomWords (Samples::room), with the seconds left
	std::variant<Sample, std::string> makeSample(const SampleAudio& _audio, const std::vector<uint16_t>& _codes,
		size_t _roomWords);
}
