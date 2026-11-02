// The TRX machines' voice code (DSP2, OS 1.63), ported instruction by instruction as the ROM machines' are
// (mdVoiceRom.cpp): the same integers, 56-bit accumulators, limiting where the DSP stores an accumulator, and the
// voices' shared scratch memory (md::voice::Scratch), which these renders write as the DSP's do.
//
// TRX-BD ($1003DA, $1003E1, $100408), each block: a noise of two words that add as a Fibonacci sequence (Y:$00-$1F),
// scaled by a decaying level; for the eight blocks after the one that follows a Hit, the attack, two sampled
// waveforms weighted by two packet words plus the noise; otherwise two sines from the sine table, the second an
// octave below, both swept down by a decaying pitch envelope and the second scaled by its own decay, then a clipper
// whose two thresholds move by a step each block, the amp envelope (with the noise mixed in), a shaper that bends the
// signal past two thresholds, and the output, twice the shaped signal plus the unshaped one. The block after a Hit
// fades the last block's last sample out (x 0.9 a sample) instead.
//
// TRX-SD ($10074C, $10075B, $100776), each block: two sines swept by a pitch envelope that rises for eleven blocks
// after a Hit and decays after, mixed with a cross term and scaled by the amp envelope; a noise of two words that
// add as a Fibonacci sequence, through two identical two-pole filters whose coefficients a packet word picks in two
// tables, scaled by its own envelope and added; then, for 128 blocks after a Hit, a sampled waveform added.
//
// TRX-XT ($101882, $101883, $101891), each block: a sine swept by a decaying pitch envelope, whose step takes the last
// sine word read as its low word, scaled by the amp envelope, then a clipper whose levels move by a step past its two
// thresholds. The block after a Hit fades the last block's last sample out (x 0.9 a sample), the two after it play a
// sampled waveform each, scaled by a packet word.
//
// TRX-CP ($10194F, $101966, $101972), each block: a noise through two two-pole filters, at a level that decays a
// block at a time; a second noise, its level raised by a burst while each of three counters runs and decaying after,
// through two high-pass filters, then two two-pole filters; their sum, a quarter of it.
//
// TRX-RS ($101AB0, $101AB8, $101ACD), each block: a sine at PTCH / 32, folded back past a threshold either way by a
// level that grows while it stays past, halved; for 24 blocks after a Hit a sampled waveform added; the amp envelope;
// then two high-pass filters in double precision, and twice their output, two samples late.
#include "mdVoiceDsp.h"
#include "mdVoiceInternal.h"

#include <algorithm>
#include <stdexcept>

namespace md::voice
{
	using namespace dsp;

	// The boot's sine (P:$100069-$10008B): s[n+1] = 2c s[n] - s[n-1] in 48-bit double precision, c = cos(2 pi /
	// 32768) = $7FFFFF:$D88586, from 0 and $000648:$7ED46B
	TrxTables::TrxTables()
	{
		constexpr uint32_t cHi = 0x7fffff, cLo = 0xd88586;
		uint32_t h[2] = {0, 0x648};
		uint32_t l[2] = {0, 0x7ed46b};
		sine[0] = 0;
		sine[1] = 0x648;
		int r = 1;
		for(size_t n = 2; n < sine.size(); ++n)
		{
			const uint32_t y1 = h[r], y0 = l[r];
			r ^= 1;
			int64_t a = wrap56(mulUU(cLo, y0));
			a = dmac(a, mulSU(cHi, y0));
			a = wrap56(a + mulSU(y1, cLo));		// MACSU
			a = dmac(a, mul(cHi, y1));
			a = shl(a, 1);
			a = wrap56(a - acc(h[r], l[r]));
			limL(a, h[r], l[r]);
			sine[n] = lim(a);
		}
	}
}

namespace md::voice::trx
{
	namespace
	{
		// ADD X,B on B1:B0 alone, as the phase loops keep them: the 48-bit sum, B2 never read
		uint64_t add48(const uint64_t _b, const int64_t _x)
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
		void envelope(uint32_t* const _block, uint32_t& _level, const uint32_t _rate)
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
		void fadeOut(uint32_t _last, Voice::Block& _out)
		{
			for(auto& s : _out)
			{
				_last = lim(mul(_last, 0x733333));
				s = sx(_last);
			}
		}

		const TrxTables& tables(const Context& _c)
		{
			if(!_c.shared.trx)
				throw std::logic_error("md::voice: a TRX machine renders without the voice DSP's tables");
			return *_c.shared.trx;
		}

		// A Hit adds its level to the level word, limited
		void hitLevel(uint32_t& _level, const testHooks::Probe _limited)
		{
			const int64_t level = acc(0x50c335) + acc(_level);
			if(level > kMax48)
				testHooks::probe(_limited);
			_level = lim(level);
		}

		// A sine over a block: the table's word at the phase (two state words, 48 bits), the phase masked to a period
		// before each step; _store takes each word
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

		// A noise of 32 words: two words that add as a Fibonacci pair, B1 only
		void fibonacci(uint32_t* const _dst, uint32_t& _prev, uint32_t& _cur)
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
		int64_t highPassZeros(const uint32_t _x2, const uint32_t _x1, const uint32_t _x0)
		{
			return mul(_x2, 0x400000) + mul(_x1, 0x800000) + mul(_x0, 0x400000);
		}

		void highPassTaps(std::array<uint32_t, 0x100>& _iy)
		{
			_iy[1] = 0x400000;
			_iy[2] = 0x800000;
			_iy[3] = 0x400000;
		}

		// A word of the filters' tables by its X address
		uint32_t filterWord(const TrxTables& _t, const uint32_t _addr)
		{
			return word(_t.filters, (_addr - TrxTables::FiltersAddr) & 0xffffff);
		}

		// A two-pole filter's pole: its radius and its angle's cosine, which two indices pick in the tables
		struct Pole
		{
			uint32_t c, r;
		};

		Pole pole(const TrxTables& _t, const uint32_t _angle, const uint32_t _radius)
		{
			return {filterWord(_t, (0x140900 + _angle) & 0xffffff), filterWord(_t, (0x141900 + _radius) & 0xffffff)};
		}

		// ... by a base word: the angle the base plus X:$140000[offset], the radius the base plus X:$140080[offset]
		Pole offsetPole(const TrxTables& _t, const uint32_t _base, const uint32_t _offset)
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

		Poles poles(const Pole& _p)
		{
			const int64_t a = shl(mul(_p.r, _p.c), 1);
			const uint32_t a2 = lim(-mul(_p.r, _p.r));
			return {lim(a), a2, lim(wrap56(-(a + acc(a2) - acc(0x400000))))};
		}

		// Two two-pole filters in turn, the same coefficients: the first on _in[0-31], its last two outputs _h2 and
		// _h1, the second written over the input, its own last two from _in[-2] and _in[-1]
		void twoPoles(const Poles& _p, uint32_t* const _in, uint32_t& _h2, uint32_t& _h1)
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
	}
}

namespace md::voice::trx::bd
{
	namespace
	{
		using Block = Voice::Block;
		using testHooks::probe;
	}

	void init(Context& _c)
	{
		_c.y[0x1c] = 0;
		_c.y[0x1d] = 1;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		hitLevel(y[0x11], testHooks::BdLevelLimited);
		y[0x12] = 0xa;
		y[0x13] = 0x7ffcb9;
		y[0x16] = y[0x17] = y[0x19] = y[0x1f] = y[0x20] = 0;
		y[0x1a] = y[0x14] = y[6];
		y[0x1b] = y[0x15] = y[7];
		y[0x1e] = y[0xb];
		y[0x21] = y[0xd];
		y[0xf] = 0x7ffcb9;
		y[0x10] = 0x32;
		y[0xe] = 0x7fdbb4;
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& iy = _c.shared.scratch.y;
		auto& ix = _c.shared.scratch.x;
		// The modulo-32 rings: noise Y:$00 (R5), the sines Y:$20 (R1), the signal X:$20 (R4), the shaped X:$40 (R3)
		uint32_t* const noise = &iy[0];
		uint32_t* const low = &iy[0x20];
		uint32_t* const sig = &ix[0x20];
		uint32_t* const shaped = &ix[0x40];

		// The noise: B and X1 swap as a Fibonacci pair, B1 only (B0 stays 0)
		{
			uint32_t b = y[0x1c], x1 = y[0x1d];
			int k = 0;
			for(int i = 0; i < 10; ++i)
			{
				b = (b + x1) & 0xffffff;
				const uint32_t x0 = x1;
				noise[k++] = b;
				noise[k++] = b;
				noise[k++] = b;
				x1 = b;
				b = x0;
			}
			b = (b + x1) & 0xffffff;
			y[0x1c] = x1;
			y[0x1d] = b;
			noise[k++] = b;
			noise[k++] = b;
		}
		{
			const uint32_t x0 = y[0x1e];
			for(int k = 0; k < 32; ++k)
				noise[k] = lim(mul(noise[k], x0));
			y[0x1e] = lim(mul(y[0xc], x0));
		}

		// The block after a Hit, and the eight of the attack
		if(y[0x12] != 0)
		{
			const int64_t a = acc(y[0x12]) - acc(1);
			y[0x12] = lim(a);
			if(a == acc(9))
			{
				probe(testHooks::BdFade);
				fadeOut(y[0x18], _out);
				return;
			}
			probe(testHooks::BdAttack);
			const uint32_t index = y[0x19];
			y[0x19] = lim(acc(0x20) + acc(index));
			const uint32_t x0 = y[0xf], x1 = y[2];
			for(uint32_t k = 0; k < 32; ++k)
			{
				// The attack reads 256 of the 288 words (update restarts the index with the count)
				int64_t acc1 = shl(mul(word(t.bdWaveB, index + k), x1), 2);
				acc1 += mul(word(t.bdWaveA, index + k), x0) + acc(noise[k]);
				_out[k] = sx(lim(wrap56(acc1)));
			}
			return;
		}

		// The two sines, their steps from PTCH and the pitch envelope
		const auto sweep = [&](const int _shift)
		{
			return (acc(y[3]) >> _shift) + (mul(y[0x13], y[4]) >> 1);
		};
		sines(t, y, 0x16, 0x17, sweep(6), [&](const int _k, const uint32_t _w) { sig[_k] = lim(acc(_w) >> 2); });
		const int64_t lowStep = sweep(7);
		y[0x13] = lim(mul(y[0x13], y[5]));
		sines(t, y, 0x1f, 0x20, lowStep, [&](const int _k, const uint32_t _w) { low[_k] = lim(shl(acc(_w), 1)); });
		{
			const uint32_t x0 = y[0xe], x1 = y[0x21];
			for(int k = 0; k < 32; ++k)
				sig[k] = lim(acc(sig[k]) + (mul(low[k], x1) >> 4));
			y[0x21] = lim(mul(x1, x0));
		}

		// The clipper: past its upper threshold the signal holds the lower one's level less a step, past the lower
		// the upper's plus a step, and those levels move (Y:$14, Y:$15); the thresholds move by the step each block
		{
			const uint32_t x0 = y[0x1a], x1 = y[0x1b];
			y[0x1a] = lim(acc(y[0x10]) + acc(x0));
			y[0x1b] = lim(acc(x1) - acc(y[0x10]));
			ix[6] = x0;
			ix[7] = x1;
			uint32_t y0 = y[0x14], y1 = y[0x15];
			const int64_t a = acc(y[0x10]);
			for(int k = 0; k < 32; ++k)
			{
				int64_t b = (-acc(sig[k])) >> 1;
				if(b > acc(x0))
				{
					probe(testHooks::BdClipAbove);
					b = acc(y0) - a;
					y1 = x1;
					y0 = lim(b);
				}
				else if(b < acc(x1))
				{
					probe(testHooks::BdClipBelow);
					b = acc(y1) + a;
					y0 = x0;
					y1 = lim(b);
				}
				sig[k] = lim(shl(b, 1));
			}
			y[0x14] = y0;
			y[0x15] = y1;
		}

		// The amp envelope, the noise mixed in at its level
		{
			const uint32_t x0 = y[1];
			uint32_t x1 = y[0x11];
			for(int k = 0; k < 32; ++k)
			{
				const int64_t a = mul(x1, sig[k]) + mul(noise[k], x1);
				const int64_t b = mul(x1, x0);
				sig[k] = lim(a);
				x1 = lim(b);
			}
			y[0x11] = x1;
		}

		// The shaper: past X0 (above) or X1 (below) the excess is scaled by X0 before the gain Y:$0A
		{
			const uint32_t y1 = y[0xa], x0 = y[8], x1 = y[9];
			for(int k = 0; k < 32; ++k)
			{
				int64_t a = acc(sig[k]);
				if(a >= acc(x0))
				{
					probe(testHooks::BdShapeAbove);
					a = mul(lim(a - acc(x0)), x0) + acc(x0);
				}
				else if(a <= acc(x1))
				{
					probe(testHooks::BdShapeBelow);
					a = mul(lim(a - acc(x1)), x0) + acc(x1);
				}
				a += mul(y1, lim(a));
				shaped[k] = lim(a);
			}
		}

		for(int k = 0; k < 32; ++k)
			_out[static_cast<size_t>(k)] = sx(lim(shl(acc(shaped[k]) + acc(sig[k]), 1)));
		y[0x18] = static_cast<uint32_t>(_out[31]) & 0xffffff;
	}
}

namespace md::voice::trx::sd
{
	namespace
	{
		using Block = Voice::Block;
		using testHooks::probe;
	}

	void init(Context& _c)
	{
		auto& y = _c.y;
		y[0x17] = 0x29a;
		y[0x18] = 1;
		y[0x19] = y[0x1a] = y[0x1b] = y[0x1c] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		hitLevel(y[0xe], testHooks::SdLevelLimited);
		y[0xd] = 0x140;
		y[0x1d] = 0x7eb852;
		// The noise's level grows with the last sample's magnitude
		const int64_t last = acc(y[0x16]);
		y[0x10] = 0;
		y[0xf] = lim(acc(0x4ccccd) + shl(last < 0 ? -last : last, 4));
		y[0x11] = y[0x12] = y[0x13] = y[0x14] = y[0x15] = 0;
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& iy = _c.shared.scratch.y;
		auto& ix = _c.shared.scratch.x;
		// The modulo-32 rings: the tone X:$40 (R0, R3), the second sine then the noise X:$20 (R1)
		uint32_t* const tone = &ix[0x40];
		uint32_t* const noise = &ix[0x20];

		// The two sines: each sample the table's word at the phase, then the step added and the phase masked to a
		// period. The steps from PTCH and the pitch envelope Y:$10, as it was before this block
		const auto step = [&](const uint32_t _ptch) { return ((acc(_ptch) >> 1) + acc(y[0x10])) >> 4; };
		const auto sines = [&](const int64_t _step, const uint32_t _amp, const uint32_t _hiAddr, const uint32_t _loAddr,
			uint32_t* const _dst)
		{
			uint64_t b = static_cast<uint64_t>(acc(y[_hiAddr], y[_loAddr])) & 0xffffffffffffull;
			const int64_t x = acc(hi(_step), lo(_step));
			for(int k = 0; k < 32; ++k)
			{
				_dst[k] = lim(mul(word(t.sine, static_cast<uint32_t>(b >> 24)), _amp));
				b = add48(b, x) & 0x007fffffffffull;
			}
			y[_hiAddr] = static_cast<uint32_t>(b >> 24) & 0xffffff;
			y[_loAddr] = static_cast<uint32_t>(b) & 0xffffff;
		};
		sines(step(y[7]), y[3], 0x12, 0x13, tone);
		const int64_t stepB = step(y[8]);
		if(acc(y[0x11]) > acc(y[0xd]))
		{
			probe(testHooks::SdPitchDecay);
			y[0x10] = lim(mul(y[0xc], y[0x10]));
		}
		else
		{
			probe(testHooks::SdPitchRise);
			y[0x10] = lim(shl(acc(y[0xa]), 1) + acc(y[0x10]));
		}
		sines(stepB, y[4], 0x14, 0x15, noise);

		// Their sum, plus their sum scaled by a decaying cross level, then the amp envelope
		{
			const uint32_t x0 = y[0x1d];
			y[0x1d] = lim(mul(y[9], x0));
			for(int k = 0; k < 32; ++k)
			{
				const int64_t b = shl(mul(tone[k], x0) + mul(x0, noise[k]), 6);
				tone[k] = lim(acc(tone[k]) + acc(noise[k]) + b);
			}
		}
		envelope(tone, y[0xe], y[1]);

		fibonacci(noise, y[0x17], y[0x18]);

		// Two two-pole filters in turn by Y:$0B, its angle 3 and its radius $241 below. The first keeps its last two
		// outputs at X:$02-$03, the second reads its own from X:$1E on, in place over the noise; Y:$00-$01 hold g and
		// a2, as the DSP leaves them
		{
			const uint32_t index = y[0xb];
			const auto p = poles(pole(t, (index - 3) & 0xffffff, (index - 0x241) & 0xffffff));
			iy[0] = p.g;
			iy[1] = p.a2;
			ix[0x1e] = y[0x1b];
			ix[0x1f] = y[0x1c];
			uint32_t h2 = y[0x19], h1 = y[0x1a];
			twoPoles(p, noise, h2, h1);
			ix[3] = h2;
			ix[2] = h1;
			y[0x19] = h2;
			y[0x1a] = h1;
			y[0x1b] = noise[30];
			y[0x1c] = noise[31];
		}

		// The noise's envelope, then the noise added to the tone at its level
		{
			envelope(noise, y[0xf], y[2]);
			const uint32_t level = y[6];
			for(int k = 0; k < 32; ++k)
				tone[k] = lim(acc(tone[k]) + mul(noise[k], level));
		}

		// The sampled waveform, 32 words a block from its start, then the tone alone
		const uint32_t index = y[0x11];
		y[0x11] = lim(acc(index) + acc(0x20));
		if(acc(index) > acc(0xfe0))
		{
			probe(testHooks::SdTail);
			for(int k = 0; k < 32; ++k)
				_out[static_cast<size_t>(k)] = sx(tone[k]);
		}
		else
		{
			probe(testHooks::SdWave);
			for(uint32_t k = 0; k < 32; ++k)
				_out[k] = sx(lim(acc(tone[k]) + mul(word(t.sdWave, (index + k) & 0xffffff), y[5])));
		}
		y[0x16] = static_cast<uint32_t>(_out[31]) & 0xffffff;
	}
}

namespace md::voice::trx::xt
{
	namespace
	{
		using Block = Voice::Block;
		using testHooks::probe;
	}

	void init(Context&) {}

	void update(Context& _c)
	{
		auto& y = _c.y;
		y[0xb] = 3;
		y[0xa] = 0x7ffcb9;
		y[0xe] = y[0xf] = 0;
		y[0xc] = y[4];
		y[0xd] = y[5];
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;
		// The modulo-32 ring X:$20 (R1): the sine, then the signal
		uint32_t* const sig = &ix[0x20];

		// The three blocks after a Hit: the last sample faded out, then the two sampled waveforms
		if(y[0xb] != 0)
		{
			const int64_t a = acc(y[0xb]) - acc(1);
			y[0xb] = lim(a);
			if(a != 0 && a != acc(1))
			{
				probe(testHooks::XtFade);
				fadeOut(y[0x10], _out);
				return;
			}
			probe(a == 0 ? testHooks::XtWaveB : testHooks::XtWaveA);
			const uint32_t* const wave = &t.xtWave[a == 0 ? 32 : 0];
			for(size_t k = 0; k < 32; ++k)
				_out[k] = sx(lim(mul(wave[k], y[9])));
			return;
		}

		// The sine, its step from PTCH and the pitch envelope. Past the first sample the step's low word is the last
		// sine word read: the loop loads it into X0, which the add uses
		{
			const int64_t step = wrap56((acc(y[2]) >> 6) + mul(y[0xa], y[3]));
			y[0xa] = lim(mul(y[0xa], y[7]));
			uint64_t b = static_cast<uint64_t>(acc(y[0xe], y[0xf])) & 0xffffffffffffull;
			const uint32_t x1 = hi(step);
			uint32_t x0 = lo(step);
			for(int k = 0; k < 32; ++k)
			{
				b &= 0x007fffffffffull;
				const uint32_t index = static_cast<uint32_t>(b >> 24);
				b = add48(b, acc(x1, x0));
				x0 = t.sine[index];
				sig[k] = x0;
			}
			y[0xe] = static_cast<uint32_t>(b >> 24) & 0xffffff;
			y[0xf] = static_cast<uint32_t>(b) & 0xffffff;
		}

		envelope(sig, y[8], y[1]);

		// The clipper: from its upper threshold up the inverted signal holds the lower one's level less a step, from
		// the lower down the upper's plus a step, and those levels move (Y:$0C, Y:$0D). Unlike TRX-BD's, the signal is
		// not halved, a threshold reached counts as past, and the thresholds stay
		{
			const uint32_t x0 = y[4], x1 = y[5];
			ix[4] = x0;
			ix[5] = x1;
			uint32_t y0 = y[0xc], y1 = y[0xd];
			const int64_t a = acc(y[6]);
			for(int k = 0; k < 32; ++k)
			{
				int64_t b = -acc(sig[k]);
				if(b >= acc(x0))
				{
					probe(b == acc(x0) ? testHooks::XtClipAt : testHooks::XtClipAbove);
					b = acc(y0) - a;
					y1 = x1;
					y0 = lim(b);
				}
				else if(b <= acc(x1))
				{
					probe(testHooks::XtClipBelow);
					b = acc(y1) + a;
					y0 = x0;
					y1 = lim(b);
				}
				_out[static_cast<size_t>(k)] = sx(lim(b));
			}
			y[0x10] = static_cast<uint32_t>(_out[31]) & 0xffffff;
			y[0xc] = y0;
			y[0xd] = y1;
		}
	}
}

namespace md::voice::trx::cp
{
	namespace
	{
		using Block = Voice::Block;
		using testHooks::probe;
	}

	void init(Context& _c)
	{
		auto& y = _c.y;
		y[0x15] = 0x29a;
		y[0x16] = 1;
		for(uint32_t w = 0x17; w <= 0x24; ++w)
			y[w] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		y[0x14] = 0;
		y[0xf] = y[2];
		y[0x10] = 0x333333;
		y[0x11] = y[0x12] = y[0x13] = y[3];
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& iy = _c.shared.scratch.y;
		auto& ix = _c.shared.scratch.x;
		// The modulo-32 ring X:$60 (R5): a noise, then the clap's noise; Y:$60 the first noise filtered
		uint32_t* const ring = &ix[0x60];

		// A noise through two two-pole filters by Y:$09 and Y:$0A, kept at Y:$60
		fibonacci(ring, y[0x15], y[0x16]);
		{
			const auto p = poles(offsetPole(t, y[9], y[0xa]));
			iy[0] = p.a2;
			iy[1] = p.g;
			ix[0x5e] = y[0x24];
			ix[0x5f] = y[0x23];
			uint32_t h2 = y[0x21], h1 = y[0x22];
			twoPoles(p, ring, h2, h1);
			ix[0] = h2;
			ix[1] = h1;
			y[0x21] = h2;
			y[0x22] = h1;
			y[0x23] = ring[31];
			y[0x24] = ring[30];
		}
		std::copy(ring, ring + 32, &iy[0x60]);

		// The clap's noise, its level raised by a burst each time a counter (Y:$0C, then $0D, then $0E) runs, by a word
		// the burst then clears, and decaying at the burst's rate; past the three the last word and rate
		fibonacci(ring, y[0x15], y[0x16]);
		{
			int64_t b = acc(y[0x14]);
			uint32_t rate;
			const auto burst = [&](const uint32_t _counter, const uint32_t _add, const uint32_t _rate)
			{
				y[_counter] = lim(acc(y[_counter]) - acc(1));
				b += acc(y[_add]);
				y[_add] = 0;
				rate = y[_rate];
			};
			if(y[0xc] != 0)
			{
				probe(testHooks::CpBurstA);
				burst(0xc, 1, 3);
			}
			else if(y[0xd] != 0)
			{
				probe(testHooks::CpBurstB);
				burst(0xd, 2, 0x11);
			}
			else if(y[0xe] != 0)
			{
				probe(testHooks::CpBurstC);
				burst(0xe, 0xf, 0x12);
			}
			else
			{
				probe(testHooks::CpDecay);
				b += acc(y[0x10]);
				y[0x10] = 0;
				rate = y[0x13];
			}
			if(b > kMax48 || b < kMin48)
				probe(testHooks::CpLevelLimited);
			uint32_t level = lim(b);
			envelope(ring, level, rate);
			y[0x14] = level;
		}

		// The clap's noise through two high-pass filters in turn, 1/2 - z^-1 + 1/2 z^-2 over the poles by Y:$05 and
		// Y:$06, the first's last two outputs at X:$00-$01, the second's written from X:$22 on after its last two
		{
			const auto p = poles(offsetPole(t, y[5], y[6]));
			iy[0] = p.a2;
			highPassTaps(iy);
			ix[0x5e] = y[0x1c];
			ix[0x5f] = y[0x1b];
			ix[0x20] = y[0x1a];
			ix[0x21] = y[0x19];
			uint32_t* const out = &ix[0x22];
			uint32_t h2 = y[0x18], h1 = y[0x17], o2 = y[0x1a], o1 = y[0x19];
			for(int k = 0; k < 32; ++k)
			{
				const int64_t in = highPassZeros(ring[k - 2], ring[k - 1], ring[k]);
				const uint32_t s = lim(shl(mul(p.a2, h2) + mul(p.a1, h1) + in, 1));
				const uint32_t o = lim(shl(mul(p.a2, o2) + mul(p.a1, o1) + highPassZeros(h2, h1, s), 1));
				out[k] = o;
				o2 = o1;
				o1 = o;
				h2 = h1;
				h1 = s;
			}
			ix[0] = h2;
			ix[1] = h1;
			y[0x17] = h1;
			y[0x18] = h2;
			y[0x19] = out[31];
			y[0x1a] = out[30];
			y[0x1b] = ring[31];
			y[0x1c] = ring[30];
		}

		// Two two-pole filters by Y:$07 and Y:$08 over X:$20-$3F: the high-passed noise two samples late
		{
			const auto p = poles(offsetPole(t, y[7], y[8]));
			iy[0] = p.a2;
			iy[1] = p.g;
			uint32_t* const in = &ix[0x20];
			ix[0x1e] = y[0x20];
			ix[0x1f] = y[0x1f];
			uint32_t h2 = y[0x1d], h1 = y[0x1e];
			twoPoles(p, in, h2, h1);
			ix[0] = h2;
			ix[1] = h1;
			y[0x1d] = h2;
			y[0x1e] = h1;
			y[0x1f] = in[31];
			y[0x20] = in[30];
		}

		// The output: the clap plus the filtered noise at a level that decays by Y:$04 a block, a quarter of the sum
		const uint32_t level = y[0xb];
		y[0xb] = lim(mul(level, y[4]));
		for(int k = 0; k < 32; ++k)
			_out[static_cast<size_t>(k)] = sx(lim((acc(ix[0x20 + k]) + mul(level, iy[0x60 + k])) >> 2));
	}
}

namespace md::voice::trx::rs
{
	namespace
	{
		using Block = Voice::Block;
		using testHooks::probe;
	}

	void init(Context& _c)
	{
		auto& y = _c.y;
		y[9] = 0;
		y[0xb] = 0x2000;
		y[0xc] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		hitLevel(y[9], testHooks::RsLevelLimited);
		y[5] = 0x400000;
		y[6] = 0x200;
		// The filters' low words, Y:$13 and Y:$14, stay from the last Hit, as the DSP leaves them
		y[7] = y[8] = y[0xa] = y[0xd] = y[0xe] = y[0xf] = y[0x10] = y[0x11] = y[0x12] = 0;
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& iy = _c.shared.scratch.y;
		auto& ix = _c.shared.scratch.x;
		// The modulo-32 rings: the signal X:$00 (R1), then X:$60 (R4)
		uint32_t* const sig = &ix[0];
		uint32_t* const ring = &ix[0x60];

		// The sine, its step PTCH / 32, the phase masked to a period before each step
		sines(t, y, 0xb, 0xc, acc(y[2]) >> 5, [&](const int _k, const uint32_t _w) { sig[_k] = _w; });

		// The fold: past the threshold Y:$03 either way, the sine's peak less a level that grows by Y:$04 a sample
		// while it stays past (Y:$0A), and back to 0 once inside; then scaled by Y:$05
		{
			int64_t b = acc(y[0xa]);
			const int64_t threshold = acc(y[3]), grow = acc(y[4]);
			const uint32_t scale = y[5];
			for(int k = 0; k < 32; ++k)
			{
				int64_t a = acc(sig[k]);
				if(a >= threshold)
				{
					probe(testHooks::RsFoldAbove);
					a = wrap56(acc(0x7eb852) - b);
					b = wrap56(b + grow);
				}
				else if(-a >= threshold)
				{
					probe(testHooks::RsFoldBelow);
					a = wrap56(acc(0x8147ae) + b);
					b = wrap56(b + grow);
				}
				else
				{
					b = 0;
				}
				sig[k] = lim(mul(lim(a), scale));
			}
			y[0xa] = lim(b);
		}

		// The sampled waveform added, 32 words a block from its start, then the amp envelope, into X:$60
		const uint32_t index = y[8];
		if(acc(index) >= acc(0x300))
		{
			probe(testHooks::RsTail);
			std::copy(sig, sig + 32, ring);
		}
		else
		{
			probe(testHooks::RsWave);
			y[8] = lim(acc(index) + acc(0x20));
			for(uint32_t k = 0; k < 32; ++k)
				ring[k] = lim(acc(sig[k]) + acc(word(t.rsWave, (index + k) & 0xffffff)));
		}
		envelope(ring, y[9], y[1]);

		// Two high-pass filters in turn, 1/2 - z^-1 + 1/2 z^-2 over the poles by Y:$06 and Y:$07, in double precision:
		// each keeps its accumulator from one sample to the next (the last output, its low word at Y:$14 and Y:$13),
		// adds -2 r^2 times the output before and twice 2 r c - 1/2 times the last, and stores each output as a long
		// word. That is TRX-CP's high-pass, 2 (2 r c h1 - r^2 h2), its accumulator kept to 48 bits. The first's last
		// two at X:$42-$43 (and Y), the second's from X:$22 on after its last two
		{
			const auto pl = offsetPole(t, y[6], y[7]);
			const uint32_t a2 = lim(shl(-mul(pl.r, pl.r), 1));
			const uint32_t a1 = lim(wrap56(shl(mul(pl.r, pl.c), 1) + acc(0xc00000)));
			iy[0] = a2;
			highPassTaps(iy);
			const auto feedback = [&](const uint32_t _h2, const uint32_t _h1)
			{
				return mul(a2, _h2) + mul(_h1, a1) + mul(_h1, a1);
			};
			ix[0x5e] = y[0x12];
			ix[0x5f] = y[0x11];
			uint32_t* const out = &ix[0x20];
			out[0] = y[0x10];
			out[1] = y[0xf];
			uint32_t h2 = y[0xe], h1 = y[0xd], h2lo = 0, h1lo = y[0x14];
			uint32_t o2 = y[0x10], o1 = y[0xf], o1lo = y[0x13];
			int64_t a = wrap56(acc(h1, h1lo) + feedback(h2, h1));
			int64_t b = acc(o1, o1lo);
			for(int k = 0; k < 32; ++k)
			{
				a = wrap56(a + highPassZeros(ring[k - 2], ring[k - 1], ring[k]));
				b = wrap56(b + feedback(o2, o1) + highPassZeros(h2, h1, hi(a)));
				uint32_t s, sLo;
				limL(a, s, sLo);
				a = wrap56(a + feedback(h1, s));
				h2 = h1;
				h1 = s;
				h2lo = h1lo;
				h1lo = sLo;
				uint32_t o;
				limL(b, o, o1lo);
				out[k + 2] = o;
				iy[0x22 + k] = o1lo;
				o2 = o1;
				o1 = o;
			}
			// The first's long words alternate between X:$42 and X:$43, the last at $43
			ix[0x42] = h2;
			iy[0x42] = h2lo;
			ix[0x43] = h1;
			iy[0x43] = h1lo;
			y[0xd] = h1;
			y[0x14] = h1lo;
			y[0xe] = h2;
			y[0xf] = o1;
			y[0x13] = o1lo;
			y[0x10] = o2;
			y[0x11] = ring[31];
			y[0x12] = ring[30];
		}

		// Twice the second filter's output, two samples late
		for(int k = 0; k < 32; ++k)
			_out[static_cast<size_t>(k)] = sx(lim(shl(acc(ix[0x20 + k]), 1)));
	}
}

namespace md::voice::trx
{
	namespace
	{
		constexpr Routines kBd = {bd::init, bd::update, bd::render};
		constexpr Routines kCp = {cp::init, cp::update, cp::render};
		constexpr Routines kRs = {rs::init, rs::update, rs::render};
		constexpr Routines kSd = {sd::init, sd::update, sd::render};
		constexpr Routines kXt = {xt::init, xt::update, xt::render};
	}

	const Routines* routines(const uint32_t _type)
	{
		switch(_type)
		{
			case 17:	return &kBd;
			case 18:	return &kSd;
			case 19:	return &kXt;
			case 20:	return &kCp;
			case 21:	return &kRs;
			default:	return nullptr;
		}
	}
}
