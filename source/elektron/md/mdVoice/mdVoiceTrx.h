// The TRX machines' voice code (DSP2, OS 1.63), ported instruction by instruction as the ROM machines' are
// (mdVoiceRom.cpp): the same integers, 56-bit accumulators, limiting where the DSP stores an accumulator, and the
// voices' shared scratch memory (md::voice::Scratch), which these renders write as the DSP's do.
//
// The helpers the TRX machines share, and the routines of each machine, which has a file of its own (mdVoiceTrxBd.cpp
// for TRX-BD and so on); the tables and the dispatch by type are in mdVoiceTrx.cpp.
#pragma once

#include "mdVoiceDsp.h"
#include "mdVoiceInternal.h"

#include <array>
#include <stdexcept>

namespace md::voice::trx
{
	using namespace dsp;

	// ADD X,B on B1:B0 alone, as the phase loops keep them: the 48-bit sum, B2 never read
	inline uint64_t add48(const uint64_t _b, const int64_t _x)
	{
		return (_b + static_cast<uint64_t>(_x)) & 0xffffffffffffull;
	}

	// A word of a table; the port reads 0 outside what it holds, as an index from a packet word could reach
	template<size_t N>
	uint32_t word(const std::array<uint32_t, N>& _w, const uint32_t _index)
	{
		return _index < N ? _w[_index] & 0xffffff : 0;
	}

	// An envelope over a block of 32 words: each scaled by the level, which decays by the rate a sample
	inline void envelope(uint32_t* const _block, uint32_t& _level, const uint32_t _rate)
	{
		uint32_t x1 = _level;
		for(int k = 0; k < 32; ++k)
		{
			const int64_t a = mul(x1, _block[k]);
			const int64_t b = mul(x1, _rate);
			_block[k] = lim(a);
			x1 = lim(b);
		}
		_level = x1;
	}

	// The block after a Hit: the last block's last sample faded out, x 0.9 a sample
	inline void fadeOut(uint32_t _last, Voice::Block& _out)
	{
		for(auto& s : _out)
		{
			_last = lim(mul(_last, 0x733333));
			s = sx(_last);
		}
	}

	inline const TrxTables& tables(const Context& _c)
	{
		if(!_c.shared.trx)
			throw std::logic_error("md::voice: a TRX machine renders without the voice DSP's tables");
		return *_c.shared.trx;
	}

	// A Hit adds its level to the level word, limited
	inline void hitLevel(uint32_t& _level, const testHooks::Probe _limited)
	{
		const int64_t level = acc(0x50c335) + acc(_level);
		if(level > kMax48)
			testHooks::probe(_limited);
		_level = lim(level);
	}

	// A sine over a block: the table's word at the phase (two state words, 48 bits), the phase masked to a period
	// before each step and stored unmasked; _store takes each word
	template<typename Store>
	void sines(const TrxTables& _t, Voice::State& _y, const uint32_t _hiAddr, const uint32_t _loAddr,
		const int64_t _step, Store&& _store)
	{
		uint64_t b = static_cast<uint64_t>(acc(_y[_hiAddr], _y[_loAddr])) & 0xffffffffffffull;
		for(int k = 0; k < 32; ++k)
		{
			b &= 0x007fffffffffull;
			_store(k, _t.sine[b >> 24]);
			b = add48(b, _step);
		}
		_y[_hiAddr] = static_cast<uint32_t>(b >> 24) & 0xffffff;
		_y[_loAddr] = static_cast<uint32_t>(b) & 0xffffff;
	}

	// ... the phase masked to a period after each step instead, and so stored masked; the first word's index is the
	// state's as it stands, which word() reads as 0 past the table
	template<typename Store>
	void sinesMaskedAfter(const TrxTables& _t, Voice::State& _y, const uint32_t _hiAddr, const uint32_t _loAddr,
		const int64_t _step, Store&& _store)
	{
		uint64_t b = static_cast<uint64_t>(acc(_y[_hiAddr], _y[_loAddr])) & 0xffffffffffffull;
		for(int k = 0; k < 32; ++k)
		{
			_store(k, word(_t.sine, static_cast<uint32_t>(b >> 24)));
			b = add48(b, _step) & 0x007fffffffffull;
		}
		_y[_hiAddr] = static_cast<uint32_t>(b >> 24) & 0xffffff;
		_y[_loAddr] = static_cast<uint32_t>(b) & 0xffffff;
	}

	// A noise of 32 words: two words that add as a Fibonacci pair, B1 only
	inline void fibonacci(uint32_t* const _dst, uint32_t& _prev, uint32_t& _cur)
	{
		uint32_t prev = _prev, cur = _cur;
		for(int k = 0; k < 32; ++k)
		{
			const uint32_t next = (prev + cur) & 0xffffff;
			_dst[k] = next;
			prev = cur;
			cur = next;
		}
		_prev = prev;
		_cur = cur;
	}

	// 1/2 - z^-1 + 1/2 z^-2 on three samples, the oldest first, and its taps at Y:$01-$03 as the DSP leaves them
	inline int64_t highPassZeros(const uint32_t _x2, const uint32_t _x1, const uint32_t _x0)
	{
		return mul(_x2, 0x400000) + mul(_x1, 0x800000) + mul(_x0, 0x400000);
	}

	inline void highPassTaps(std::array<uint32_t, 0x100>& _iy)
	{
		_iy[1] = 0x400000;
		_iy[2] = 0x800000;
		_iy[3] = 0x400000;
	}

	// A word of the filters' tables by its X address
	inline uint32_t filterWord(const TrxTables& _t, const uint32_t _addr)
	{
		return word(_t.filters, (_addr - TrxTables::FiltersAddr) & 0xffffff);
	}

	// A two-pole filter's pole: its radius and its angle's cosine, which two indices pick in the tables
	struct Pole
	{
		uint32_t c, r;
	};

	inline Pole pole(const TrxTables& _t, const uint32_t _angle, const uint32_t _radius)
	{
		return {filterWord(_t, (0x140900 + _angle) & 0xffffff), filterWord(_t, (0x141900 + _radius) & 0xffffff)};
	}

	// ... by a base word: the angle the base plus X:$140000[offset], the radius the base plus X:$140080[offset]
	inline Pole offsetPole(const TrxTables& _t, const uint32_t _base, const uint32_t _offset)
	{
		const uint32_t angle = _base + filterWord(_t, (0x140000 + _offset) & 0xffffff);
		const uint32_t radius = _base + filterWord(_t, (0x140080 + _offset) & 0xffffff);
		return pole(_t, angle & 0xffffff, radius & 0xffffff);
	}

	// A two-pole filter's coefficients: a1 = 2 r c, a2 = -r^2, and g = 1/2 - a1 - a2
	struct Poles
	{
		uint32_t a1, a2, g;
	};

	inline Poles poles(const Pole& _p)
	{
		const int64_t a = shl(mul(_p.r, _p.c), 1);
		const uint32_t a2 = lim(-mul(_p.r, _p.r));
		return {lim(a), a2, lim(wrap56(-(a + acc(a2) - acc(0x400000))))};
	}

	// Two two-pole filters in turn, the same coefficients: the first on _in[0-31], its last two outputs _h2 and
	// _h1, the second written over the input, its own last two from _in[-2] and _in[-1]
	inline void twoPoles(const Poles& _p, uint32_t* const _in, uint32_t& _h2, uint32_t& _h1)
	{
		const uint32_t a1 = _p.a1, a2 = _p.a2, g = _p.g;
		uint32_t h2 = _h2, h1 = _h1, o2 = _in[-2], o1 = _in[-1];
		for(int k = 0; k < 32; ++k)
		{
			const uint32_t s = lim(shl(mul(a2, h2) + mul(a1, h1) + mul(_in[k], g), 1));
			h2 = h1;
			h1 = s;
			const uint32_t o = lim(shl(mul(a2, o2) + mul(a1, o1) + mul(s, g), 1));
			_in[k] = o;
			o2 = o1;
			o1 = o;
		}
		_h2 = h2;
		_h1 = h1;
	}

	// Two high-pass filters in turn, 1/2 - z^-1 + 1/2 z^-2 over the same poles, as TRX-CP and TRX-CB run them (the
	// DSP's loop at $101A3F and $101FA7). The first over the 32 words at X:_in, after its input's last two at X:_in - 2,
	// its own last two left at X:_ring and _ring + 1; the second's outputs from X:_out + 2 on, after its last two at
	// X:_out. Their state, six words from Y:_state: the first's last two outputs, the latest first, then the second's,
	// then the input's. The outputs may overwrite the input: each input word is read before
	inline void highPassStage(Context& _c, const Poles& _p, const uint32_t _in, const uint32_t _ring,
		const uint32_t _out, const uint32_t _state)
	{
		auto& y = _c.y;
		auto& iy = _c.shared.scratch.y;
		auto& ix = _c.shared.scratch.x;
		iy[0] = _p.a2;
		highPassTaps(iy);
		ix[_in - 2] = y[_state + 5];
		ix[_in - 1] = y[_state + 4];
		ix[_out] = y[_state + 3];
		ix[_out + 1] = y[_state + 2];
		const uint32_t* const in = &ix[_in];
		uint32_t* const out = &ix[_out + 2];
		const uint32_t a1 = _p.a1, a2 = _p.a2;
		uint32_t x2 = y[_state + 5], x1 = y[_state + 4];
		uint32_t h2 = y[_state + 1], h1 = y[_state], o2 = y[_state + 3], o1 = y[_state + 2];
		for(int k = 0; k < 32; ++k)
		{
			const uint32_t x0 = in[k];
			const uint32_t s = lim(shl(mul(a2, h2) + mul(a1, h1) + highPassZeros(x2, x1, x0), 1));
			const uint32_t o = lim(shl(mul(a2, o2) + mul(a1, o1) + highPassZeros(h2, h1, s), 1));
			out[k] = o;
			x2 = x1;
			x1 = x0;
			o2 = o1;
			o1 = o;
			h2 = h1;
			h1 = s;
		}
		ix[_ring] = h2;
		ix[_ring + 1] = h1;
		y[_state] = h1;
		y[_state + 1] = h2;
		y[_state + 2] = out[31];
		y[_state + 3] = out[30];
		y[_state + 4] = in[31];
		y[_state + 5] = in[30];
	}

	// Each machine's routines, in its own file
	namespace bd
	{
		void init(Context& _c);
		void update(Context& _c);
		void render(Context& _c, Voice::Block& _out);
	}

	namespace sd
	{
		void init(Context& _c);
		void update(Context& _c);
		void render(Context& _c, Voice::Block& _out);
	}

	namespace xt
	{
		void init(Context& _c);
		void update(Context& _c);
		void render(Context& _c, Voice::Block& _out);
	}

	namespace cp
	{
		void init(Context& _c);
		void update(Context& _c);
		void render(Context& _c, Voice::Block& _out);
	}

	namespace rs
	{
		void init(Context& _c);
		void update(Context& _c);
		void render(Context& _c, Voice::Block& _out);
	}

	namespace cb
	{
		void init(Context& _c);
		void update(Context& _c);
		void render(Context& _c, Voice::Block& _out);
	}
}
