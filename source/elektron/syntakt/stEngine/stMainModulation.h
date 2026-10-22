#pragma once

#include <cstdint>

#include "stMainMemory.h"

// CPU #1's modulation matrix (cpu1.md, section 3.2): the velocity, four MIDI sources and the note move parameters of
// the smoothed copy in place, each through a list of four (depth, internal id) pairs of the track's settings. MACSR
// 0x20, as the audio interrupt sets it
namespace syntakt::main
{
	constexpr uint32_t Velocity = 0x80004670;	// a word per track: the trig's velocity, or MIDI's
	constexpr uint32_t Notes = 0x800046b8;		// a long per track: the note << 16
	constexpr uint32_t PitchBend = 0x80004746;	// a word per track, signed: the MIDI sources, 13 tracks each
	constexpr uint32_t Source472c = 0x8000472c;
	constexpr uint32_t Source4712 = 0x80004712;
	constexpr uint32_t Source46f8 = 0x800046f8;
	constexpr uint32_t SettingsStride = 0x7c;

	// 0x40097aac: the four pairs at _list (a long each: depth in the high word, internal id in the low one) add
	// _source x depth to the words _params[id] (Q15 products, the sum saturated to the 16-bit word's range)
	void modulate(Memory& _mem, uint32_t _params, int32_t _source, uint32_t _list);

	// 0x40097afc: the matrix over the smoothed copy _p: tracks 0-11's velocity through their settings' list +0x00,
	// then for tracks 0-12 the four MIDI sources through +0x10 to +0x40 and the note - 60 (Q8) through +0x50, whose
	// products count 16 times (each doubling saturated)
	void modulationMatrix(Memory& _mem, uint32_t _p);
}
