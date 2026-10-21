#pragma once

#include <cstdint>

#include "stVoice.h"

// BD MODERN, the Syntakt's engine 0, ported from OS 1.41's voice CPU: a sine at the note, phase-modulated by an
// enveloped mix of two oscillators an octave up, under a pitch sweep, with a filtered click, a drive stage and the amp
// envelope. .scratch/elektron-engines describes it.
namespace syntakt::bdModern
{
	// 0x40006968: the voice's fields from the track's parameters (_params: P + 0x1c), every block
	void update(int32_t _pitch, Voice& _v, uint32_t _params, Sram& _sram, Globals& _g, const Rom& _rom);

	// 0x40006c9e: one block of the voice into _out
	void render(int32_t* _out, Voice& _v, Sram& _sram, Globals& _g, const Rom& _rom);

	// 0x40004ee6: the click, a 10 ms ramp, high-passed then low-passed, into _buf
	void click(int32_t* _buf, Voice& _v);

	// 0x400046dc: _out = _b + _a, saturated
	void sum(const int32_t* _a, const int32_t* _b, int32_t* _out);
}
