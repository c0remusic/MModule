#pragma once

#include <cstdint>

#include "stMainMemory.h"

// CPU #1's filter envelopes of the digital tracks 0-7 and the cutoff they move (cpu1.md, sections 3.6 and 3.7)
namespace syntakt::main
{
	// 12 bytes per track: the stage (3 delay, 2 attack, 1 decay to sustain, 0 or any other release), the value
	// (negative full scale: 0 to INT32_MIN), the delay's counter in samples
	constexpr uint32_t FilterEnvelopes = 0x43bdc09c;
	constexpr uint32_t EnvelopeAmounts = 0x4550e1a8;	// a long per track: the envelope times ENV, for the screen

	// 0x4009652c: a block of the filter envelopes over the smoothed copy _p (DEL +0x5a, ATK +0x5c, DEC +0x5e, SUS
	// +0x60, REL +0x62, and +0x64: set, a trig restarts the value from 0). Bit t of _trigs starts track t's (delay, or
	// attack when DEL is 0), bit t of _gateOffs releases it, bit t of _mute's low byte leaves it as it is. MACSR 0x20
	void filterEnvelopes(Memory& _mem, uint32_t _p, int32_t _trigs, int32_t _gateOffs, int32_t _mute);

	// 0x4009664e: track _t's envelope value
	int32_t filterEnvelope(const Memory& _mem, int32_t _t);

	// 0x400956bc: the cutoff word << 16 (0 to 0x7f000000) of the filter parameters at _filter (P + 0x52: FREQ +2, ENV
	// +6): FREQ, plus the key tracking _keyTrack (a signed byte) times the pitch from 48 (Q16), minus the envelope of
	// track _t times ENV, which also goes to EnvelopeAmounts. Runs with MACSR 0xa0 (saturating), which it sets
	int32_t cutoff(Memory& _mem, uint32_t _filter, int32_t _keyTrack, int32_t _pitch, int32_t _t);
}
