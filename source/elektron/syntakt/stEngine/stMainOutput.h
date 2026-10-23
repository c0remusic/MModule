#pragma once

#include <cstdint>

#include "stMainEmac.h"
#include "stMainMemory.h"

// CPU #1's mix and outputs, 0x4008f1ca (cpu1.md, section 4): what it does with the voice CPU's eight tracks (one block
// old, at VoiceOutputs) and the eight ADC channels (0-3 the analog tracks, 4-5 the external input, 6-7 the analog
// mix) each block, from the smoothed copy P (0x80006348). Every gain ramps over the block by (target - current) >> 5.
namespace syntakt::main
{
	constexpr uint32_t Adc = 0x80003950;			// 8 x 32 longs
	constexpr uint32_t DirectBus = 0x8000dad0;		// the tracks not routed to the FX track, and the returns not routed
	constexpr uint32_t FxBus = 0x8000dfd0;			// the tracks and returns routed to the FX track
	constexpr uint32_t SendBus = 0x8000dbd0;		// the delay's input, then the reverb's
	constexpr uint32_t DelayReturn = 0x8000dcd0;
	constexpr uint32_t ReverbReturn = 0x8000ddd0;
	constexpr uint32_t AnalogBus = 0x8000ded0;		// the ADC side's sends, then its direct mix plus FxBus
	constexpr uint32_t Overbridge = 0x80004760;		// 32 frames of 20 longs
	constexpr uint32_t DacRing = 0x80000000;		// 2 halves of 32 frames of 8 TDM slots

	// The mix's stack frame (the firmware's -0x22c(a6) to a6): the ramps' steps the sums read, and the send mutes
	struct MixFrame
	{
		int32_t words[0x8c] = {};

		int32_t& at(const int _offset) { return words[(_offset + 0x230) / 4]; }
	};

	// 0x4008f1de-0x4008f80a: the tracks' gains (0x4008ed48), pans (0x400a171a), sends (DEL +0x76, REV +0x78 as
	// -2 v^2) and routing (ROUT +0x7e; the mask 0x80003148, or 0x8000314c while it is negative, removes a track from
	// the FX bus; 0x8000314a, or 0x8000314e, mutes its sends), then the external input's and the FX track's input
	// gains, all as ramps into _frame; then MACSR 0xa0 and the delay's input SendBus: the 8 tracks' and the 8 ADC
	// channels' sends. Returns the FX bus mask
	int32_t trackSends(Memory& _mem, Emac4& _acc, uint32_t _p, MixFrame& _frame);

	// 0x4008f1ca(_dac, _adc, _p): 0x40097414, trackSends, the delay, the reverb's input (the tracks, the delay's
	// return by its REV +0x6f2, the ADC's), the reverb, the lines' moves, the returns' levels (delay VOL +0x6f4,
	// reverb VOL +0x706, muted by bits 12 and 13 of the FX bus mask) and routing (+0x6f6, +0x708) as channels 8 and 9,
	// the direct and FX buses (x 10 and x 8), AnalogBus, the DAC ring's slots, each track's buffer scaled in place by
	// its gain for Overbridge (0x4008febc), and the Overbridge frame. Returns d0, SendBus. The DAC half is
	// 0x4553f6dc's; the routine does not read its first two arguments. _acc as the interrupt has it: its
	// accumulators clear (0x400a38b2 clears them each block, the routines between end each with a movclr), MACSR 0x20
	int32_t mixOutput(Memory& _mem, Emac4& _acc, uint32_t _p);
}
