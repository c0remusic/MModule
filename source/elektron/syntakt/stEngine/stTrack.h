#pragma once

#include <cstdint>

#include "stVoice.h"

// The per-track stages the voice CPU runs after the voices, in place on each track's 32 samples at 0x80003840 +
// 0x80 t, before the block goes out to CPU #1: drive, BASE/WIDTH, the multimode filter, the amp envelope (the audio
// interrupt 0x40000934 after 0x40004324). .scratch/elektron-engines/trackstages.md describes them.
namespace syntakt::track
{
	// 0x4000112c: the eight tracks' drive, a soft clip three times over, crossfaded with the dry signal (MACSR 0x20)
	void drive(Sram& _sram, uint32_t _out, uint32_t _params);

	// 0x40002320: the eight tracks' BASE/WIDTH band, a one-pole low-pass then a one-pole high-pass
	void baseWidth(Sram& _sram, const Rom& _rom, uint32_t _out, uint32_t _params);

	// 0x400019d6: track _t's multimode filter. _pf is the track's P + 0x52, _cutoff the modulated cutoff CPU #1
	// sends (0x80008188 + 4 t), _rst the track's RST word
	void filter(Sram& _sram, const Rom& _rom, uint32_t _pf, uint32_t _out, int32_t _cutoff, int32_t _t, int32_t _trig,
		int32_t _rst);

	// 0x40001428: one segment of the amp envelope, a one-pole toward -_target, applied to _n samples (MACSR 0x20)
	uint32_t ampSegment(Sram& _sram, uint32_t _buf, uint32_t _level, int32_t _coef, int32_t _target, int32_t _n);

	// 0x40001476: track's amp envelope (AHD or ADSR) applied to its block. _env: stage, hold count, level at
	// 0x800049e0 + 12 t; _params: P + 0x6a
	void ampEnvelope(Sram& _sram, const Rom& _rom, uint32_t _out, uint32_t _env, uint32_t _params, int32_t _trig,
		int32_t _gateOff, int32_t _kill);

	// 0x4000168e: the eight tracks' amp envelopes; _kill is byte 0x1a8 of CPU #1's block
	void ampEnvelopes(Sram& _sram, const Rom& _rom, uint32_t _out, uint32_t _params, int32_t _trig, int32_t _gateOff,
		int32_t _kill);
}
