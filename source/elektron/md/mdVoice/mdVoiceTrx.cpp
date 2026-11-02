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
#include "mdVoiceDsp.h"
#include "mdVoiceInternal.h"

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
			int64_t a = wrap56(static_cast<int64_t>(cLo) * y0 * 2);					// MPYUU
			a = wrap56((a >> 24) + static_cast<int64_t>(sx(cHi)) * y0 * 2);			// DMAC (signed x unsigned)
			a = wrap56(a + static_cast<int64_t>(sx(y1)) * cLo * 2);					// MACSU
			a = wrap56((a >> 24) + mul(cHi, y1));									// DMAC
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

		const TrxTables& tables(const Context& _c)
		{
			if(!_c.shared.trx)
				throw std::logic_error("md::voice: a TRX machine renders without the voice DSP's tables");
			return *_c.shared.trx;
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
		const int64_t level = acc(0x50c335) + acc(y[0x11]);
		if(level > kMax48)
			probe(testHooks::BdLevelLimited);
		y[0x11] = lim(level);
		y[0x12] = 0xa;
		y[0x13] = 0x7ffcb9;
		y[0x16] = y[0x17] = y[0x19] = 0;
		y[0x1a] = y[0x14] = y[6];
		y[0x1b] = y[0x15] = y[7];
		y[0x1e] = y[0xb];
		y[0x21] = y[0xd];
		y[0x1f] = y[0x16];
		y[0x20] = y[0x17];
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
				uint32_t x1 = y[0x18];
				for(auto& s : _out)
				{
					x1 = lim(mul(x1, 0x733333));
					s = sx(x1);
				}
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
		const auto sines = [&](const int64_t _step, const uint32_t _hiAddr, const uint32_t _loAddr, auto&& _store)
		{
			uint64_t b = static_cast<uint64_t>(acc(y[_hiAddr], y[_loAddr])) & 0xffffffffffffull;
			const int64_t x = acc(hi(_step), lo(_step));
			for(int k = 0; k < 32; ++k)
			{
				b &= 0x007fffffffffull;
				_store(k, acc(t.sine[(b >> 24) & 0x7fff]));
				b = add48(b, x);
			}
			y[_hiAddr] = static_cast<uint32_t>(b >> 24) & 0xffffff;
			y[_loAddr] = static_cast<uint32_t>(b) & 0xffffff;
		};
		sines(sweep(6), 0x16, 0x17, [&](const int _k, const int64_t _a) { sig[_k] = lim(_a >> 2); });
		const int64_t lowStep = sweep(7);
		y[0x13] = lim(mul(y[0x13], y[5]));
		sines(lowStep, 0x1f, 0x20, [&](const int _k, const int64_t _a) { low[_k] = lim(shl(_a, 1)); });
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
					y1 = ix[7];
					y0 = lim(b);
				}
				else if(b < acc(x1))
				{
					probe(testHooks::BdClipBelow);
					b = acc(y1) + a;
					y0 = ix[6];
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
		const int64_t level = acc(0x50c335) + acc(y[0xe]);
		if(level > kMax48)
			probe(testHooks::SdLevelLimited);
		y[0xd] = 0x140;
		y[0x1d] = 0x7eb852;
		y[0xe] = lim(level);
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
		{
			const uint32_t x0 = y[1];
			uint32_t x1 = y[0xe];
			for(int k = 0; k < 32; ++k)
			{
				const int64_t a = mul(x1, tone[k]);
				const int64_t b = mul(x1, x0);
				tone[k] = lim(a);
				x1 = lim(b);
			}
			y[0xe] = x1;
		}

		// The noise: two words that add as a Fibonacci pair, B1 only
		{
			uint32_t prev = y[0x17], cur = y[0x18];
			for(int k = 0; k < 32; ++k)
			{
				const uint32_t next = (prev + cur) & 0xffffff;
				noise[k] = next;
				prev = cur;
				cur = next;
			}
			y[0x17] = prev;
			y[0x18] = cur;
		}

		// Two two-pole filters in turn, the same coefficients: g, the input's gain, a1 and a2 = -r^2 from the tables by
		// Y:$0B. The first keeps its last two outputs at X:$02-$03, the second reads its own from X:$1E on, in place
		// over the noise; Y:$00-$01 hold g and a2, as the DSP leaves them
		{
			const uint32_t index = y[0xb];
			const uint32_t r = word(t.filterB, (index - 0x241 + 0x800) & 0xffffff);
			const uint32_t c = word(t.filterA, (index - 3 + 0x800) & 0xffffff);
			int64_t a = shl(mul(r, c), 1);
			const uint32_t a2 = lim(-mul(r, r));
			const uint32_t a1 = lim(a);
			a = wrap56(-(a + acc(a2) - acc(0x400000)));
			const uint32_t g = lim(a);
			iy[0] = g;
			iy[1] = a2;
			ix[0x1e] = y[0x1b];
			ix[0x1f] = y[0x1c];
			uint32_t h2 = y[0x19], h1 = y[0x1a];
			uint32_t* const second = &ix[0x1e];
			for(int k = 0; k < 32; ++k)
			{
				const uint32_t s = lim(shl(mul(a2, h2) + mul(a1, h1) + mul(noise[k], g), 1));
				h2 = h1;
				h1 = s;
				second[k + 2] = lim(shl(mul(a2, second[k]) + mul(a1, second[k + 1]) + mul(s, g), 1));
			}
			ix[3] = h2;
			ix[2] = h1;
			y[0x19] = h2;
			y[0x1a] = h1;
			y[0x1b] = second[32];
			y[0x1c] = second[33];
		}

		// The noise's envelope, then the noise added to the tone at its level
		{
			const uint32_t x0 = y[2];
			uint32_t x1 = y[0xf];
			for(int k = 0; k < 32; ++k)
			{
				const int64_t a = mul(x1, noise[k]);
				const int64_t b = mul(x1, x0);
				noise[k] = lim(a);
				x1 = lim(b);
			}
			y[0xf] = x1;
			for(int k = 0; k < 32; ++k)
				tone[k] = lim(acc(tone[k]) + mul(noise[k], y[6]));
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

namespace md::voice::trx
{
	namespace
	{
		constexpr Routines kBd = {bd::init, bd::update, bd::render};
		constexpr Routines kSd = {sd::init, sd::update, sd::render};
	}

	const Routines* routines(const uint32_t _type)
	{
		switch(_type)
		{
			case 17:	return &kBd;
			case 18:	return &kSd;
			default:	return nullptr;
		}
	}
}
