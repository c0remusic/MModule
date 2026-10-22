#pragma once

#include <cstdint>

#include "stMainMemory.h"

// CPU #1's gain and pan of a track, which the mix 0x4008f1ca applies to each voice's output (cpu1.md, section 4).
// Gains are in the negative representation the mix uses: INT32_MIN full, 0 silent. The mix calls them with MACSR 0x20
// (fractional, no saturation) in its track loop and 0xa0 (saturating) for the meters: Saturate is the mode
namespace syntakt::main
{
	// Two values a routine returns in d0 and d1
	struct Pair
	{
		int32_t d0 = 0;
		int32_t d1 = 0;
	};

	// 0x4008ed48: a track's gain. _level and _volume are parameter words (their low 16 bits, 0x7f00 full), _velocity
	// the track's velocity word (0x80004670: its high byte, bits 8-15, signed, is the velocity), _mode selects one of the four velocity curves of the table 0x401db1dc (its low
	// 2 bits). d0 = curve[velocity] x (level x volume)^2, positive; d1 = -(level x volume)^2
	template<bool Saturate>
	Pair trackGain(const Memory& _mem, uint32_t _level, uint32_t _velocity, uint32_t _volume, uint32_t _mode);

	// 0x400a16bc: 4 x (1 - |x|) x, a parabola for sin(pi x), x in Q31, saturated to Q31
	int32_t parabolicSine(int32_t _x);

	// 0x400a171a: the constant-power pan of a PAN word (0x100 to 0x7f00, clamped): d0 is the gain that is full at
	// PAN 0x100 and below, d1 the one full at 0x7f00 and above, both negative
	Pair pan(int32_t _pan);
}
