#pragma once

#include <cstdint>

#include "stVoice.h"

namespace syntakt
{
	// The stages most of the Syntakt's twelve digital engines share, ported from OS 1.41's voice CPU (section 7) and
	// checked word for word against the original routines (stEngineOracleTest). Each names the routine it ports.

	// 0x40004dc6: a 128-entry table read at a parameter word (value << 8 | fine), linearly interpolated. As in the
	// original, a word above 0x7fff reads past the table: _table must have 256 entries readable for words up to 0xffff
	int32_t interpolate(const int32_t* _table, int32_t _word);

	// 0x40003c0e: the block-rate envelopes of units 0 to _units - 1 (delay, linear attack, one-pole decay)
	void unitEnvelopes(Voice& _v, int32_t _units);

	// 0x40003d42: the amp envelope, one step per block
	void ampEnvelope(Voice& _v);

	// 0x400045a0: the amp envelope applied to the block, ramped linearly from the previous block's level, and the
	// fade to silence in the block a trig arrives in
	void vca(int32_t* _out, Voice& _v);

	// 0x400047ea: drive into a hard clip, under a gain envelope that ducks after a trig
	void driveClip(int32_t* _out, Voice& _v);

	// 0x40004e6c: a first-order IIR in place, y = c0 x + c1 x1 - c2 y1, its coefficients at _c[0..2] and its state at
	// _c[6] (x1) and _c[7] (y1). It reads _buf[32] too, as the original does
	void onePole(int32_t* _buf, int32_t* _c);

	// 0x400049f4: _x and _y mixed into _out with gains ramped over the block. The gains are voice fields: targets at
	// 0x08 + 4 i, current values at 0x18 + 4 i, for _gainX and _gainY
	void mix(const int32_t* _x, const int32_t* _y, int32_t* _out, int32_t _gainX, int32_t _gainY, Voice& _v);

	// 0x40004674: the modulator envelope (0x448) applied to _buf in place, ramped from the previous block's level, and
	// faded to silence in the block a trig arrives in
	void modulatorVca(int32_t* _buf, Voice& _v);

	// 0x40003df2: the modulator envelope, one step per block. Unlike unitEnvelopes, a trig enters the attack stage even
	// when the attack is instant
	void modulatorEnvelope(Voice& _v);

	// 0x40003cda: the pitch sweep's envelope (0x400), one step per block
	void sweepEnvelope(Voice& _v);

	// 0x40002596: the phase increments of units 0 to _units - 1 at 48 kHz, from the note, the tune word, the sweep and
	// each unit's frequency factor. _exp2 is the RAM table 0x8000a888 (2048 entries)
	void phaseIncrements48k(int32_t _pitch, Voice& _v, uint32_t _params, int32_t _units);

	// 0x400035de: unit _k's sine with self-feedback, 32 samples at 48 kHz, into its buffers A (scaled by the unit's
	// envelope) and B (raw)
	void feedbackOscillator(int32_t _k, Voice& _v, const Rom& _rom);

	// 0x4000290e: unit _k's oscillator phase-modulated by two buffers, 32 samples at 48 kHz
	void pmOscillator(int32_t _k, Voice& _v, const Rom& _rom);

	// 0x40003958: unit _k's sine with feedback from its scaled output, 32 samples at 48 kHz, into its buffers A
	// (scaled by the unit's envelope) and B (raw). Unlike feedbackOscillator (0x400035de), the scaled sample feeds
	// back
	void scaledFeedbackOscillator(int32_t _k, Voice& _v, const Rom& _rom);

	// 0x400044e2: 32 samples of noise from a lagged additive generator (_s[0..3]), gated: a sample is kept only
	// when the generator's second sum is above _s[4], signed
	void gatedNoise(int32_t* _out, int32_t* _s);
}
