#pragma once

#include <cstdint>

#include "stMainMemory.h"

// CPU #1's LFOs (cpu1.md, section 3.1): two per track for the 13 tracks, which add their output to a parameter of
// their own track in the smoothed copy every block. LFO2 runs before LFO1, so that it can modulate LFO1's
// parameters. MACSR 0x20, as the audio interrupt sets it
namespace syntakt::main
{
	// The state of track t's LFO2 is at LfoState + 0x50 t, LFO1's 0x28 below; fields from -0x24 to +1 (a = that
	// address): a-0x24 phase, a-0x20 output, a-0x1c the random value, a-0x18 the last output, a-0x14 the fade,
	// a-0x10 the phase of the last random value, a-0xc DEST, a-8 the modulation added, a-4 the phase or 0 while
	// stopped, byte a+0 stopped (or holding), byte a+1 restarted in the block before
	constexpr uint32_t LfoState = 0x444da778;
	constexpr uint32_t LfoTrackStride = 0x50;
	constexpr uint32_t LfoStride = 0x28;
	constexpr uint32_t RandomState = 0x402d701c;	// two longs: the generator's last two values
	constexpr int32_t LfoPeriod = 0x5265c000;		// a cycle of the phase

	// 0x40115ec8: 2^x of a Q24 exponent (x >> 24 the integer part, at most 0), Q31
	int32_t exp2(int32_t _x);

	// 0x40118c74: the fade step of a block for a FADE word: 0x40000000 at 0x4000 (no fade: full at once), else an
	// exponential of the distance either way, negative above (fading out), times 0x1c200000
	int32_t lfoFadeStep(int32_t _fade);

	// 0x40118cf0: the fade at a restart: full (0x3fffffff) when the LFO fades out (FADE above 0x3fff), else 0
	int32_t lfoFadeStart(int32_t _fade);

	// The waveforms of the table 0x40245838 over a phase in Q32 (0 to 2^32 a cycle): 0 TRI 0x40118d8e,
	// 1 SIN 0x40118da4, 2 SQR 0x40118d70, 3 SAW 0x40118d82, 4 EXP 0x40118dfe, 5 RMP 0x40118dee. RND (6) takes
	// another path
	int32_t lfoWave(int32_t _wave, int32_t _phase);

	// 0x40118ef8: the next value of the additive (Fibonacci) generator at RandomState
	int32_t lfoRandom(Memory& _mem);

	// 0x40119266: the 26 LFOs over the smoothed copy _p. _tempo is the tempo long 0x402d7010 (BPM x 120), bit t of
	// _trigs a trig of track t this block. MULT 0-11 runs at the tempo, 12-23 at 120 BPM's. MODE 0 FREE (a trig
	// restarts the fade), 1 TRIG (the phase at SPH and the fade), 2 HOLD (a trig samples the waveform, which holds),
	// 3 ONE (stops at the cycle's end), 4 HALF (at half a cycle). RND draws a value each sixteenth of a cycle and on
	// a trig (not in FREE), mixed with the last output by the coefficient of SPH
	void lfos(Memory& _mem, uint32_t _p, int32_t _tempo, int32_t _trigs);
}
