// P-I-CC, -RC and -HH's voice code, as mdVoicePi.h says. They share one instrument: noise, gated into an impulse train,
// through two shelves ($145224, $145424), a stick's sine, a level and a network of four delay lines of $100 words at
// $135600 + $600 t, whose taps follow the pitch, eight samples at a time. The block is what leaves the lines plus a
// click through a third shelf. P-I-HH's program also halves its pitch word when Y:$37 is 2 ($1459CC), which no state
// reaches: the word it stores there is $020000.
#include "mdVoicePi.h"

#include <cstdlib>

namespace md::voice::pi
{
	namespace
	{
		using Block = Voice::Block;

		constexpr uint32_t kLineMask = 0xff, kSineMask = 0x7fff;

		// The four lines at Y:$14, $17, $16, $15, one after the other, and the words every machine starts with
		void initLines(Context& _c)
		{
			auto& y = _c.y;
			for(uint32_t k = 0; k < 0x14; ++k)
				y[0x1c + k] = 0;
			y[2] = 0x7fdf3b;
			y[0x14] = firstLine(_c);
			y[0x17] = firstLine(_c) + 0x100;
			y[0x16] = firstLine(_c) + 0x200;
			y[0x15] = firstLine(_c) + 0x300;
			y[0x30] = 0xa;
			y[0x31] = 1;
			y[0x10] = 0x51eb8;
			y[0x11] = 0x79999a;
			y[0x12] = 0xb33333;
		}

		// What every update does
		void strike(Context& _c)
		{
			auto& y = _c.y;
			y[0x13] = 0;
			y[0x39] = y[0x37] = 0x7fffff;
			y[0x36] = 0xc8;
			y[0x35] = 0x14a000;
		}

		// $14505A: X:$08-$27, a train: _x0 adds to Y:$34 at each noise word above _gate, _x1 decays it. Then each word by
		// the noise word before it, the first by Y:$20. Returns the last word of the noise read
		uint32_t impulses(Context& _c, const uint32_t _x0, const uint32_t _x1, const uint32_t _gate)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			auto& iy = _c.shared.scratch.y;
			int64_t a = acc(y[0x34]);
			for(uint32_t k = 0; k < 32; ++k)
			{
				if(acc(iy[k]) > acc(_gate))
					a += acc(_x0);
				const uint32_t held = lim(a);
				ix[8 + k] = held;
				a = mul(held, _x1);
			}
			y[0x34] = lim(a);
			uint32_t y0 = iy[32];
			for(uint32_t k = 0; k < 32; ++k)
			{
				ix[8 + k] = lim(mul(y0, ix[8 + k]));
				y0 = iy[k];
			}
			return y0;
		}

		// A row of a shelf's coefficients
		struct Row
		{
			uint32_t c, d, e, f;
		};

		// The row at _addr of a table
		Row row(const Context& _c, const std::array<uint32_t, 512>& _table, const uint32_t _base, const uint32_t _addr)
		{
			const uint32_t i = _addr - _base;
			return {tableWord(_c, _table, _base, i), tableWord(_c, _table, _base, i + 1),
				tableWord(_c, _table, _base, i + 2), tableWord(_c, _table, _base, i + 3)};
		}

		Row lowRow(const Context& _c, const uint32_t _addr)
		{
			return row(_c, tables(_c).piLowCut, TrxTables::PiLowCutAddr, _addr);
		}

		// In place, over X:_first: the last two outputs at _first and _first + 1, then the input from _first + 2 and the
		// outputs written from there on. y[n + 1] = y[n] + 2 d y[n] + c y[n - 1] + e x[n] + f x[n - 1] + e x[n - 2], the
		// accumulator running from sample to sample. The row in Y:$28-$2C for the DSP's ring, the outputs' low words at
		// Y:_first + 2 on. Its state, five words from Y:_state: the last output's low word and upper word, the one before,
		// then the input's last two
		void shelf(Context& _c, const Row& _r, const uint32_t _first, const uint32_t _state)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			auto& iy = _c.shared.scratch.y;
			iy[0x28] = _r.c;
			iy[0x29] = _r.d;
			iy[0x2a] = iy[0x2c] = _r.e;
			iy[0x2b] = _r.f;
			ix[_first] = y[_state + 2];
			ix[_first + 1] = y[_state + 1];
			ix[_first + 2] = y[_state + 4];
			ix[_first + 3] = y[_state + 3];

			int64_t a = acc(y[_state + 1], y[_state]) + mul(_r.c, ix[_first]) + 2 * mul(_r.d, ix[_first + 1]);
			int64_t b = 0;
			for(uint32_t k = 0; k < 32; ++k)
			{
				a += mulSum(_r.e, ix[_first + 2 + k], ix[_first + 4 + k]) + mul(_r.f, ix[_first + 3 + k]);
				b = a;
				a += mul(_r.c, ix[_first + 1 + k]) + 2 * mul(_r.d, lim(b));
				limL(b, ix[_first + 2 + k], iy[_first + 2 + k]);
			}
			y[_state + 3] = ix[_first + 0x23];
			y[_state + 4] = ix[_first + 0x22];
			y[_state + 1] = ix[_first + 0x21];
			y[_state] = lo(b);
			y[_state + 2] = ix[_first + 0x20];
		}

		// The accumulator that picks the row of $145224 by Y:$08. P-I-CC and P-I-RC move it limited, P-I-HH its A1 as it
		// stands ($145901, $145934)
		int64_t lowRowWord(const Context& _c)
		{
			return shl(acc(_c.y[8]), 2) + acc(TrxTables::PiLowCutAddr);
		}

		// The first shelf by the row at _first of $145224, then the second by the row at _second of $145424
		void shelves(Context& _c, const uint32_t _first, const uint32_t _second)
		{
			shelf(_c, lowRow(_c, _first), 4, 0x21);
			shelf(_c, row(_c, tables(_c).piHighCut, TrxTables::PiHighCutAddr, _second), 0, 0x26);
		}

		// The accumulator that picks the row of $145424 by Y:$07 of Y:$3A
		int64_t followingRow(const Context& _c)
		{
			const auto& y = _c.y;
			int64_t a = shl(mul(y[0x3a], y[7]), 2);
			a &= ~(3ll << 24);
			return a + acc(TrxTables::PiHighCutAddr);
		}

		// How many samples of the sine this block, while Y:$13 stays below _length. False when it has ended
		bool toneWords(Context& _c, const uint32_t _length, uint32_t& _pairs)
		{
			auto& y = _c.y;
			const int64_t b = acc(y[0x13]);
			int64_t a = b + acc(0x20);
			if(b >= acc(_length))
				return false;
			testHooks::probe(testHooks::PiStick);
			y[0x13] = lim(a);
			a = -(a - acc(_length));
			if(a > acc(0x20))
			{
				testHooks::probe(testHooks::PiStickLong);
				a = acc(0x20);
			}
			else
			{
				testHooks::probe(testHooks::PiStickShort);
			}
			_pairs = hi(std::llabs(a) >> 1);
			return true;
		}

		// The sine from Y:$35, a step of Y:$05 a word, added to _words, in pairs of words: P-I-CC's and -HH's to X, by _gain,
		// after the shelves, P-I-RC's to Y, before them
		template<size_t N>
		void tone(Context& _c, std::array<uint32_t, N>& _words, const uint32_t _first, const uint32_t _pairs,
			const uint32_t _gain, const testHooks::Area _area)
		{
			auto& y = _c.y;
			uint32_t r = y[0x35];
			const auto sine = [&]
			{
				if(r - TrxTables::SineAddr >= 0x8000)
					testHooks::probe(testHooks::PiStickOutside);
				const uint32_t w = external(_c, r, _area);
				r = advance(r, y[5], kSineMask);
				return w;
			};
			int64_t b = acc(_words[_first]) + mul(sine(), _gain);
			uint32_t from = _first + 1, to = _first;
			for(uint32_t p = 0; p < _pairs; ++p)
			{
				int64_t a = acc(_words[from++]);
				const uint32_t s0 = sine();
				_words[to++] = lim(b);
				a += mul(s0, _gain);
				b = acc(_words[from++]);
				const uint32_t s1 = sine();
				_words[to++] = lim(a);
				b += mul(s1, _gain);
			}
			y[0x35] = advance(r, (0 - y[5]) & 0xffffff, kSineMask);
		}

		// $145107: X:$00-$1F by the amp level Y:$32, which rises by Y:$33 until full scale, then falls by _fall
		void amp(Context& _c, const uint32_t _fall)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			int64_t a = acc(y[0x32]);
			if(const uint32_t rise = y[0x33]; rise != 0)
			{
				testHooks::probe(testHooks::PiAmpRising);
				// The level steps a sample late: the second word hears the first's
				uint32_t held = lim(a);
				for(uint32_t k = 0; k < 32; ++k)
				{
					ix[k] = lim(mul(held, ix[k]));
					held = lim(a);
					a += acc(rise);
				}
				y[0x32] = lim(a);
				if(a >= acc(0x7fffff))
				{
					testHooks::probe(testHooks::PiAmpTop);
					y[0x33] = 0;
				}
				return;
			}
			testHooks::probe(testHooks::PiAmpFalling);
			// The level steps every other sample: Y0 lags A by two
			uint32_t y0 = lim(a);
			int64_t b = mul(ix[0], y0);
			uint32_t next0 = lim(a);
			a = mul(y0, _fall);
			y0 = next0;
			for(uint32_t k = 1; k < 32; ++k)
			{
				ix[k - 1] = lim(b);
				b = mul(ix[k], y0);
				next0 = lim(a);
				a = mul(y0, _fall);
				y0 = next0;
			}
			ix[31] = lim(b);
			y[0x32] = hi(a);
		}

		using Words = std::array<uint32_t, 32>;

		// The network: _g0 and _g1 how the lines and the input reach the output, _h the input to the lines
		struct Network
		{
			uint32_t g0, g1, h;
		};

		// Y:$00, Y:$02-$05 and Y:$07 set: four rounds of eight samples through the lines at Y:$14-$17, kept at Y:$1C-$1F
		// after their one-poles, at X:$80-$87, $89-$90, $C0-$C7 and $C9-$D0, into _out
		void network(Context& _c, const Network& _n, Words& _out)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			auto& iy = _c.shared.scratch.y;

			const uint32_t gain = iy[7];
			for(uint32_t round = 0; round < 4; ++round)
			{
				// All the reads come before all the writes: a tap can lie less than eight words behind its line's pointer
				uint32_t r0 = advance(y[0x14], iy[2], kLineMask), r1 = advance(y[0x17], iy[5], kLineMask);
				uint32_t r2 = advance(y[0x16], iy[4], kLineMask), r3 = advance(y[0x15], iy[3], kLineMask);
				int64_t a0 = acc(y[0x1c]), a1 = acc(y[0x1f]), a2 = acc(y[0x1e]), a3 = acc(y[0x1d]);
				for(uint32_t j = 0; j < 8; ++j)
				{
					ix[0x89 + j] = onePole(a0, lineWord(_c, r0), gain);
					ix[0xc9 + j] = onePole(a1, lineWord(_c, r1), gain);
					ix[0xc0 + j] = onePole(a2, lineWord(_c, r2), gain);
					ix[0x80 + j] = onePole(a3, lineWord(_c, r3), gain);
					r0 = next(r0, kLineMask);
					r1 = next(r1, kLineMask);
					r2 = next(r2, kLineMask);
					r3 = next(r3, kLineMask);
				}
				y[0x1c] = lim(a0);
				y[0x1f] = lim(a1);
				y[0x1e] = lim(a2);
				y[0x1d] = lim(a3);

				const uint32_t r = iy[6], g = iy[0];
				uint32_t r4 = y[0x14], r5 = y[0x17], r6 = y[0x15], r7 = y[0x16];
				for(uint32_t j = 0; j < 8; ++j)
				{
					const uint32_t in = ix[r + j];
					_out[8 * round + j] = lim(mulSum(_n.g0, ix[0xc9 + j], ix[0xc0 + j]) + mul(in, _n.g1));
					const int64_t heard = mul(_n.h, in);
					lineWord(_c, r5) = lim(heard - mulSum(g, ix[0x80 + j], ix[0xc0 + j]));
					lineWord(_c, r4) = lim(heard - mulDiff(g, ix[0x80 + j], ix[0xc0 + j]));
					lineWord(_c, r7) = lim(heard + mulDiff(g, ix[0x89 + j], ix[0xc9 + j]));
					lineWord(_c, r6) = lim(heard + mulSum(g, ix[0x89 + j], ix[0xc9 + j]));
					r4 = next(r4, kLineMask);
					r5 = next(r5, kLineMask);
					r6 = next(r6, kLineMask);
					r7 = next(r7, kLineMask);
				}
				y[0x14] = r4;
				y[0x17] = r5;
				y[0x15] = r6;
				y[0x16] = r7;
				iy[6] = (r + 8) & 0xff;
			}
		}

		// The tuning before it: the lines' taps from Y:$01 by negative factors, and their damping
		void tune(Context& _c, const std::array<uint32_t, 4>& _taps)
		{
			auto& y = _c.y;
			auto& iy = _c.shared.scratch.y;
			iy[7] = lim(acc(0x733333) + mul(0xccccd, y[6]));
			for(uint32_t k = 0; k < 4; ++k)
				iy[2 + k] = lim(mul(_taps[k], y[1]));
			iy[6] = 0;
		}

		// The third shelf, at Y:$2B-$2F, over X:$04-$23, whose input the machines have just set; its output at X:$02-$21,
		// the block twice the network plus X:$04-$23
		void finish(Context& _c, const Row& _r, const Words& _network, Block& _out)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;

			shelf(_c, _r, 0, 0x2b);
			for(uint32_t k = 0; k < 32; ++k)
				_out[k] = sx(lim(shl(acc(_network[k]) + acc(ix[4 + k]), 1)));
			y[0x37] = 0;
		}

		// X:$04-$23: _words of _level, then zeros
		void click(const Context& _c, const uint32_t _words, const uint32_t _level)
		{
			auto& ix = _c.shared.scratch.x;
			for(uint32_t k = 0; k < 32; ++k)
				ix[4 + k] = k < _words ? _level : 0;
		}

		// Row $145224 + 4 Y:$08 with its numerator by _x0
		Row clickRow(const Context& _c, const uint32_t _addr, const uint32_t _x0)
		{
			const Row r = lowRow(_c, _addr);
			return {r.c, r.d, lim(mul(_x0, r.e)), lim(mul(_x0, r.f))};
		}

		// Y:$00: the lines' feedback
		void feedbackWord(const Context& _c, const uint32_t _x0, const uint32_t _scale)
		{
			auto& iy = _c.shared.scratch.y;
			const uint32_t r = tableWord(_c, tables(_c).piRing, TrxTables::PiRingAddr, lim(mul(_scale, _x0)));
			iy[0] = lim(mul(r, 0x5a827a));
		}

		// Y:$39 under its end: the level's short fall
		bool ended(const Context& _c)
		{
			if(acc(_c.y[0x39]) >= acc(0x54))
				return false;
			testHooks::probe(testHooks::PiEnded);
			return true;
		}
	}

	namespace cc
	{
		// $144FEC, $145023
		void init(Context& _c)
		{
			_c.y[0x3a] = _c.y[0x32] = 0;
			initLines(_c);
		}

		void update(Context& _c)
		{
			auto& y = _c.y;
			y[0x3b] = y[3];
			y[0x3c] = lim(acc(0x7fffff) - acc(y[3]));
			y[0x33] = 0;
			strike(_c);
		}

		// $145037
		void render(Context& _c, Block& _out)
		{
			auto& y = _c.y;

			y[0x39] = lim(mul(y[9], y[0x39]));
			const uint32_t bend = y[0x3b];
			y[0x3b] = lim(mul(bend, y[0x3c]));
			y[0x32] = lim(acc(y[0x32]) + acc(bend));
			if(y[0x37] != 0)
				y[0x3a] = y[0x32];

			noise(_c, 0x30, 16);
			impulses(_c, y[0x10], y[0x11], y[0x12]);
			shelves(_c, lim(lowRowWord(_c)), lim(followingRow(_c)));

			const uint32_t pitch = y[0x37] != 0 ? 0x733333 : y[1];
			uint32_t pairs = 0;
			if(toneWords(_c, lim(mul(0x49, pitch)), pairs))
			{
				uint32_t gain = 0;
				if(acc(y[3]) >= acc(0x19999a))
				{
					testHooks::probe(testHooks::PiStickGain);
					gain = lim(mul(y[3], 0x19999a));
				}
				tone(_c, _c.shared.scratch.x, 0, pairs, gain, testHooks::AreaY);
			}
			amp(_c, ended(_c) ? 0x666666 : y[2]);

			feedbackWord(_c, ended(_c) ? 0xccccd : y[4], 0x1f4);
			tune(_c, {0xffff07, 0xffff7b, 0xffff8b, 0xffffb7});
			Words lines{};
			network(_c, {0x7eb852, 0x0ccccd, 0x666666}, lines);

			click(_c, 6, y[0x37] != 0 ? 0x333333 : 0);
			uint32_t x0 = 0;
			if(acc(y[3]) > acc(0x19999a))
			{
				testHooks::probe(testHooks::PiClickGain);
				x0 = lim(acc(y[3]) - acc(0x19999a));
			}
			finish(_c, clickRow(_c, lim(lowRowWord(_c)), x0), lines, _out);
		}
	}

	namespace rc
	{
		// $145624, $14565B
		void init(Context& _c)
		{
			initLines(_c);
			_c.y[0x38] = 0x266666;
		}

		void update(Context& _c)
		{
			auto& y = _c.y;
			y[0x33] = y[0x38];
			y[0x32] = 0;
			y[0x34] = 0x7fffff;
			strike(_c);
		}

		// $14566C
		void render(Context& _c, Block& _out)
		{
			auto& y = _c.y;

			y[0x39] = lim(mul(y[9], y[0x39]));
			noise(_c, 0x30, 16);

			uint32_t pairs = 0;
			if(toneWords(_c, lim(mul(0x41, y[1]) >> 2), pairs))
				tone(_c, _c.shared.scratch.y, 0x20, pairs, 0x733333, testHooks::AreaX);

			const uint32_t rest = lim(acc(0x7fffff) - acc(y[3]));
			impulses(_c, lim(mul(y[0x10], rest)), y[0x11], y[3]);
			shelves(_c, lim(lowRowWord(_c)), lim(shl(acc(y[7]), 2) + acc(TrxTables::PiHighCutAddr)));

			amp(_c, ended(_c) ? 0x666666 : y[2]);

			const uint32_t damp = ended(_c) ? 0xccccd : lim(mul(y[3], 0x7fdf3b) + acc(y[4]));
			feedbackWord(_c, damp, 0x1ff);
			tune(_c, {0xffff83, 0xffff95, 0xffffa9, 0xffffbf});
			Words lines{};
			network(_c, {0x733333, 0x0ccccd, 0x59999a}, lines);

			click(_c, 16, y[0x37] != 0 ? 0x666666 : 0);
			finish(_c, clickRow(_c, lim(lowRowWord(_c)), lim(acc(0x7fffff) - acc(y[3]))), lines, _out);
		}
	}

	namespace hh
	{
		// $14584F, $145887
		void init(Context& _c)
		{
			_c.y[0x3a] = 0;
			initLines(_c);
			_c.y[0x38] = 0xccccd;
		}

		void update(Context& _c)
		{
			auto& y = _c.y;
			y[0x10] = 0x51eb8;
			y[0x11] = 0x79999a;
			y[0x12] = 0xb33333;
			y[0x3c] = y[0x33] = 0;
			y[0x3b] = 0x7fffff;
			strike(_c);
		}

		// $1458A0
		void render(Context& _c, Block& _out)
		{
			auto& y = _c.y;

			const uint32_t was = y[0x39];
			const int64_t a = mul(y[9], was);
			if(a > acc(0x54))
			{
				y[0x12] = lim(acc(y[3]) - acc(0x4ccccd));
			}
			else if(acc(was) >= acc(0x54))
			{
				testHooks::probe(testHooks::PiHhClosing);
				y[0x37] = imm8(2);
				y[0x10] = 0xccccd;
				y[0x11] = 0x75c28f;
				y[0x12] = 0xccccd;
				y[0x3b] = 0xccccd;
				y[0x3c] = 0x733333;
			}
			y[0x39] = lim(a);

			const uint32_t bend = y[0x3b];
			y[0x3b] = lim(mul(bend, y[0x3c]));
			y[0x32] = lim(acc(y[0x32]) + acc(bend));
			if(y[0x37] != 0)
				y[0x3a] = y[0x32];

			noise(_c, 0x30, 17);
			const uint32_t last = impulses(_c, y[0x10], y[0x11], y[0x12]);
			if(acc(last) >= acc(0x79999a))
			{
				testHooks::probe(testHooks::PiHhAccent);
				y[0x34] = lim(acc(y[0x34]) + acc(0x333333));
			}
			shelves(_c, hi(lowRowWord(_c)), hi(followingRow(_c)));

			const uint32_t pitch = y[0x37] != 0 ? 0x733333 : y[1];
			uint32_t pairs = 0;
			if(toneWords(_c, lim(mul(0x49, pitch)), pairs))
				tone(_c, _c.shared.scratch.x, 0, pairs, 0x19999a, testHooks::AreaY);
			amp(_c, ended(_c) ? 0x7f8d50 : y[2]);

			feedbackWord(_c, y[4], 0x1f4);
			tune(_c, {0xffff07, 0xffff7b, 0xffff8b, 0xffffb7});
			Words lines{};
			network(_c, {0x7eb852, 0x400000, 0x666666}, lines);

			click(_c, 8, y[0x37] == 0x7fffff ? 0x666666 : 0);
			finish(_c, clickRow(_c, TrxTables::PiLowCutAddr + 4 * 127, 0x666666), lines, _out);
		}
	}
}
