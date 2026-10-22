#pragma once

#include <cstdint>

#include "stMainMemory.h"

// CPU #1's parameter smoothing and the locks that bypass it (cpu1.md, sections 3.3 and 3.4). The engine copy holds the
// targets, a word per parameter; the smoothing state SmoothState a long per word; the lock bits LockBits a bit per
// internal id of a track. "Snap" sets a word's state to its target, so that the parameter jumps. Track _t is the
// engine's order: 0-7 the digital tracks, 8-11 the analog ones, 12 the FX track. MACSR 0x20, as the audio interrupt
// sets it
namespace syntakt::main
{
	// 0x40097952: one block of smoothing. Each of the 938 words of _target (the engine copy) moves its state one pole
	// toward it (state = 0x03d7 target + 0x7c29 state, Q15 weights) and the rounded state goes to Smoothed; then the
	// settings after _target are copied to SettingsCopy unsmoothed, and the words that must not glide (LFO1 and LFO2
	// DEST of tracks 0-11, SP TWINSHOT's +0x42 and +0x4a, SY RAW's +0x48 on tracks 8-10) are copied unsmoothed too.
	// Returns Smoothed
	uint32_t smooth(Memory& _mem, uint32_t _target);

	// 0x400978b4: the locks of a trig. _list holds a count at +8 and (id, value) entries of 8 bytes from +0x14: each
	// sets track _t's word of internal id `id` in the engine copy _engine, snaps it and sets its lock bit
	void lock(Memory& _mem, int32_t _t, uint32_t _list, uint32_t _engine);

	// 0x40097802: every locked word of the FX track goes back to its value in _src (a word per internal id, from 0),
	// in _dst (the engine copy) and snapped; its lock bit is cleared
	void restoreFxLocks(Memory& _mem, uint32_t _dst, uint32_t _src);

	// 0x40097760: the same for track _t, from _src + 0x14 (a sound's parameters)
	void restoreLocks(Memory& _mem, int32_t _t, uint32_t _dst, uint32_t _src);

	// 0x40097874: clears track _t's lock bits
	void clearLocks(Memory& _mem, int32_t _t);

	// 0x40097894: engine word _index of _engine is the low word of _value, snapped
	void setParameter(Memory& _mem, int32_t _index, int32_t _value, uint32_t _engine);

	// 0x400976b4: snaps track _t's 71 words to the values at _src
	void snapTrack(Memory& _mem, int32_t _t, uint32_t _src);

	// 0x400976ee: snaps _count words of track _t from internal id _first to the values at _src
	void snapRange(Memory& _mem, int32_t _t, uint32_t _src, int32_t _first, int32_t _count);

	// 0x40097732: snaps track _t's word of internal id _index to the low word of _value
	void snapParameter(Memory& _mem, int32_t _t, int32_t _value, int32_t _index);
}
