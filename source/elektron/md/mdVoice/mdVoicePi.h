// The P-I machines' voice code (DSP2, OS 1.63), ported as mdVoiceTrx.h says. These are physical models: an excitation
// or noise rings through delay lines kept in the external memory (Shared::lines) and, for five of them, a bank of eight
// resonators. Their long feedback stays in integers, 56-bit accumulators and words limited where the DSP stores them.
//
// BD, SD, MT, ML, RS and MA are in mdVoicePi.cpp, CC, RC and HH in mdVoicePiMetal.cpp; here is what the two share. A
// machine's words that are not code are the owner's (PiBank) or come from their formulas (TrxTables).
#pragma once

#include "mdVoiceTrx.h"

namespace md::voice::pi
{
	using namespace trx;

	// r + n, the AGU's way with a modulus of _m + 1, a power of two (dsp56kEmu's updateAddressRegister). A step past the
	// modulus can leave the buffer: P-I-RC's sine does from its pitch knob at 95 up, and reads the UW bank there
	inline uint32_t advance(const uint32_t _r, const uint32_t _n, const uint32_t _m)
	{
		const auto step = static_cast<uint32_t>(sx(_n));
		const uint32_t lower = _r & ~_m, upper = lower + _m;
		uint32_t r = _r + step;
		const uint32_t modulo = step & _m ? _m + 1 : 0;
		if(r < lower)
			r += modulo;
		if(r > upper)
			r -= modulo;
		return r & 0xffffff;
	}

	inline uint32_t next(const uint32_t _r, const uint32_t _m)
	{
		return (_r & ~_m) | ((_r + 1) & _m);
	}

	// The voice's first delay line, $600 words a voice (the DSP's MPYI gives twice its $300)
	inline uint32_t firstLine(const Context& _c)
	{
		return Shared::LinesAddr + lo(mul(0x300, _c.index));
	}

	// A word of the delay lines by its address. The mask keeps a state word that a packet may overwrite inside them
	inline uint32_t& lineWord(const Context& _c, const uint32_t _addr)
	{
		return _c.shared.lines[(_addr - Shared::LinesAddr) & (Shared::LinesWords - 1)];
	}

	// The word _index of a table of X, or what lies there: an index from a packet word may leave the table
	template<size_t N>
	uint32_t tableWord(const Context& _c, const std::array<uint32_t, N>& _table, const uint32_t _addr, const uint32_t _index)
	{
		const uint32_t i = _index & 0xffffff;
		return i < N ? _table[i] : external(_c, (_addr + i) & 0xffffff, testHooks::AreaX);
	}

	// _g (_a + _b) and _g (_a - _b) as MPY and MAC give them, in one multiplication: nothing limits between the two
	inline int64_t mulSum(const uint32_t _g, const uint32_t _a, const uint32_t _b)
	{
		return sx64(_g) * (sx64(_a) + sx64(_b)) * 2;
	}

	inline int64_t mulDiff(const uint32_t _g, const uint32_t _a, const uint32_t _b)
	{
		return sx64(_g) * (sx64(_a) - sx64(_b)) * 2;
	}

	// A one-pole's sample: the word it holds, then _a towards _in by _gain
	inline uint32_t onePole(int64_t& _a, const uint32_t _in, const uint32_t _gain)
	{
		const uint32_t held = lim(_a);
		_a += mulDiff(_gain, _in, held);
		return held;
	}

	// The noise, _pairs pairs of words of the two running at Y:_state, at Y:$00 on. Returns the last but one, Y:_state + 1
	inline uint32_t noise(Context& _c, const uint32_t _state, const uint32_t _pairs)
	{
		auto& y = _c.y;
		auto& iy = _c.shared.scratch.y;
		uint32_t b = y[_state], x1 = y[_state + 1];
		for(uint32_t k = 0; k < _pairs; ++k)
		{
			const uint32_t a = x1;
			b = (b + x1) & 0xffffff;
			iy[2 * k] = x1;
			x1 = b;
			iy[2 * k + 1] = x1;
			x1 = (a + x1) & 0xffffff;
		}
		y[_state] = x1;
		y[_state + 1] = b;
		return b;
	}

	// The routines of the three that share mdVoicePiMetal.cpp; mdVoicePi.cpp holds the others and the dispatch
	namespace cc
	{
		void init(Context& _c);
		void update(Context& _c);
		void render(Context& _c, Voice::Block& _out);
	}

	namespace rc
	{
		void init(Context& _c);
		void update(Context& _c);
		void render(Context& _c, Voice::Block& _out);
	}

	namespace hh
	{
		void init(Context& _c);
		void update(Context& _c);
		void render(Context& _c, Voice::Block& _out);
	}
}
