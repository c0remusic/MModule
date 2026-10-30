#pragma once

#include <cstdint>

#include "stEngine.h"
#include "stVoice.h"

// The Syntakt's digital drum engines, ported from OS 1.41's voice CPU, each in its own file: update(pitch, voice,
// P + 0x1c) every block, then render(out, voice). .scratch/elektron-engines describes them.
namespace syntakt
{
	// BD MODERN: a sine at the note, phase-modulated by an enveloped mix of two oscillators an octave up, under a pitch
	// sweep, with a filtered click, a drive stage and the amp envelope
	namespace bdModern
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);

		// 0x40004ee6: the click, a 10 ms ramp, high-passed then low-passed, into _buf
		void click(int32_t* _buf, Voice& _v);

		// 0x400046dc: _out = _b + _a, saturated
		void sum(const int32_t* _a, const int32_t* _b, int32_t* _out);
	}

	namespace sdBasic
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);

		// 0x40004e14: a first-order IIR in place, y = c0 x + c1 x1 - c2 y1, its coefficients at _c[0..2] and its state
		// at _c[6] (x1) and _c[7] (y1), each product truncated alone and the terms added wrapping
		void firstOrder(int32_t* _buf, int32_t* _c);
	}

	namespace cyAlloy
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);

		// 0x4000512c: a biquad in place, y = b0 x + b1 x1 + b2 x2 - 2 a1 y1 - a2 y2, every sample's terms summed before
		// one truncation. _b holds b0-b2 and at +0x18 x1, x2; _a holds a1, a2 and at +0x18 y1, y2
		void biquad(int32_t* _buf, int32_t* _b, int32_t* _a);
	}

	namespace pcCarbon
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);
	}

	namespace sdVintage
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);
	}

	namespace cpVintage
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);
	}

	namespace syTone
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);

		// 0x40003aac: unit _k's sine with feedback from its scaled output, 64 samples at 96 kHz, into its buffers A
		// (scaled by the unit's envelope) and B (raw, halved), each after an 8-word header
		void feedbackOperator(uint32_t _k, Voice& _v, const Rom& _rom);

		// 0x40002c16: unit _k's sine phase-modulated by two buffers, 64 samples at 96 kHz, into its buffers A and B
		void pmOperator(uint32_t _k, Voice& _v, const Rom& _rom);

		// 0x400051c6: a 2:1 polyphase IIR halfband, 96 kHz to 48 kHz, of the 64 samples at _buf + 0x20 into _out, the
		// voice's history (0x56c) read and written
		void decimate(Voice& _v, uint32_t _buf, int32_t* _out);
	}

	namespace syChord
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);
	}

	namespace syToy
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);
	}

	namespace syBits
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);

		// 0x400061a2: unit _k's two waves (0xb0, 0xb4) crossfaded at its 25-bit phase, 32 samples, into its buffers B
		// (raw) and A (under the unit's level); the weights (0xa8, 0xac) ramp a 64th of the way to their targets a
		// sample, then take them
		void crossfadeOscillator(int32_t _k, Voice& _v);

		// 0x40006346: unit _k's wave (0xb0) at its phase plus an offset ramped from 0x74 to 0x70, 32 samples, into its
		// buffers B (raw) and A (under the unit's level)
		void offsetOscillator(int32_t _k, Voice& _v);

		// 0x40005f9a: a bit reducer in place, each sample truncated to a multiple of 2^b and of 2^(b + 1),
		// crossfaded by the fraction; b and the fraction from _c[5], Q16
		void reduceBits(int32_t* _buf, const int32_t* _c);
	}

	namespace sySwarm
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);
	}

	namespace spTwinshot
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);

		// 0x40006764: 32 samples of the player whose record (9 words: pointer, next pointer, position, length, next
		// length, playing, rate, reverse, loop) is _record into _out, forward or reversed, looping on the next pointer
		// and length, or stopping
		void player(int32_t* _out, int32_t* _record, const Rom& _rom);

		// 0x400068ea: two one-poles in series, coefficients _c[0], _c[1], states _c[2], _c[3], each step saturated;
		// _out the first's output minus the second's
		void bandPass(const int32_t* _in, int32_t* _out, int32_t* _c);
	}

	// The engines ported, the one list engine() and the oracle read: the engine's id, its name, the firmware's update
	// and render routines, and the port's
	inline constexpr EngineRoutines PortedEngines[] = {
		{0, "bdModern", 0x40006968, 0x40006c9e, bdModern::update, bdModern::render},
		{1, "sdBasic", 0x40007c68, 0x40007f9e, sdBasic::update, sdBasic::render},
		{2, "cyAlloy", 0x40006d7c, 0x40006ff2, cyAlloy::update, cyAlloy::render},
		{3, "pcCarbon", 0x40007280, 0x400075a8, pcCarbon::update, pcCarbon::render},
		{6, "sdVintage", 0x40008074, 0x4000847a, sdVintage::update, sdVintage::render},
		{7, "cpVintage", 0x40008580, 0x40008988, cpVintage::update, cpVintage::render},
		{4, "syTone", 0x4000709c, 0x40007214, syTone::update, syTone::render},
		{5, "syChord", 0x40007770, 0x40007b38, syChord::update, syChord::render},
		{8, "syToy", 0x40008ae8, 0x40008d58, syToy::update, syToy::render},
		{9, "syBits", 0x40008e0c, 0x400091e0, syBits::update, syBits::render},
		{10, "sySwarm", 0x400092ec, 0x400096e8, sySwarm::update, sySwarm::render},
		{11, "spTwinshot", 0x400097dc, 0x40009a8a, spTwinshot::update, spTwinshot::render},
	};
}
