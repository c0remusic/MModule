#pragma once

#include <cstdint>

#include "stMainMemory.h"

// CPU #1's pitch of tracks 0-11 (cpu1.md, section 3.5): the note glides from the last pitch (portamento), then the
// pitch bend times the track's bend range is added. Pitches are semitones in Q16. MACSR 0x20, as the audio interrupt
// sets it
namespace syntakt::main
{
	constexpr uint32_t Glide = 0x45565328;		// 8 bytes per track: the glide's progress (from 0 down to INT32_MIN,
												// where it is done), then the distance from the target it starts at
	constexpr uint32_t LastPitch = 0x444db6f8;	// a long per track: the pitch of the last block
	constexpr uint32_t Pitches = 0x80004688;	// a long per track: the pitch the block builder sends (0x150 + 4 t)
	constexpr uint32_t Held = 0x80003154;		// a word: bit t, track t's note is held [INFER]
	constexpr uint32_t Gates = 0x80003152;		// a word: bit t, track t's gate is open [INFER]

	// 0x40119fe8: track _t's pitch for the note _target (Q16). On a trig (_trig's low byte), the glide restarts from
	// the last pitch of track _source as the legato setting (settings +0x68) allows, scaled by settings +0x64 (a
	// percentage); PORT (+0x86) at 0x3fff or below ends it at once. Settings +0x6c rounds the distance and the pitch
	// to semitones; settings +0x70 set, the glide moves at PTIM's rate (table 0x40245c50), else in a time that the
	// distance in semitones sets (table 0x40245a50); settings +0x74 set, it moves only while the gate is open or on
	// the trig
	int32_t portamento(Memory& _mem, int32_t _t, int32_t _source, int32_t _trig, int32_t _target, uint32_t _p);

	// The interrupt's pitch loop 0x400a4ade-0x400a4b3e: for tracks 0-11, Pitches[t] = portamento(t, t, bit t of
	// _trigs, Notes[t], _p) + bend range (settings +0x7a, a signed byte, semitones) x pitch bend (Q15)
	void pitches(Memory& _mem, uint32_t _p, int32_t _trigs);
}
