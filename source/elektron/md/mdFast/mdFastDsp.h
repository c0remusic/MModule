#pragma once

#include <cstdint>

// The DSP56300's arithmetic on signed 24-bit words, in an accumulator's units (a word << 24): what the mixer DSP's code
// does where md::fast keeps its words. mdVoiceDsp.h holds the voice DSP's, on masked unsigned words.
namespace md::fast::dsp
{
	constexpr int64_t acc(const int32_t _word) { return static_cast<int64_t>(_word) * (int64_t(1) << 24); }

	// The fractional product
	constexpr int64_t mpy(const int32_t _a, const int32_t _b) { return static_cast<int64_t>(_a) * _b * 2; }

	// A store of the accumulator to a word: truncated, limited
	constexpr int32_t lim(const int64_t _acc)
	{
		if(_acc >= (int64_t(1) << 47))
			return 0x7fffff;
		if(_acc < -(int64_t(1) << 47))
			return -0x800000;
		return static_cast<int32_t>(_acc >> 24);
	}

	// The sign of a 24-bit word
	constexpr int32_t sx(const uint32_t _word) { return static_cast<int32_t>(_word << 8) >> 8; }
}
