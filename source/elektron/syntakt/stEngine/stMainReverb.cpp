#include "stMainReverb.h"

#include <type_traits>

#include "stEmac.h"
#include "stMainFx.h"

namespace syntakt::main
{
	namespace
	{
		constexpr uint32_t Frames = 32;
		constexpr uint32_t Poles = 0x4029bffc;
		constexpr uint32_t Shelves = 0x4029b7fc;		// FREQ's coefficient, by its high byte
		constexpr uint32_t Decays = 0x4029b6fc;			// DEC's diffusion gain, by its high byte, 64 of them
		constexpr uint32_t Interpolation = 0x4029b9fc;	// 32 sets of 8 taps
		constexpr uint32_t Matrix = 0x401df8e0;			// per DEC byte 8 longs: 4 per half
		constexpr uint32_t Filters = 0x8000c530;		// the high-pass and low-pass poles
		constexpr uint32_t Diffused = 0x8000c53c;		// 32 samples: the input, then diffused
		constexpr uint32_t Shelf = 0x8000c5fc;			// three coefficients
		constexpr uint32_t PreFilter = 0x8000c608;		// the pre-delay's low-pass and high-pass states
		constexpr uint32_t PreDelay = 0x8000c63c;		// line 0's length, samples
		constexpr uint32_t DiffuserBase = 0x8000d630;	// 8 gains (the SRAM image's)
		constexpr uint32_t DiffuserGains = 0x8000d650;	// 8: those x DEC's
		constexpr uint32_t Outs = 0x8000c310;			// 4 x 32 samples: the half's four filtered lines
		constexpr uint32_t Tap = 0x8000c490;			// 32 samples: the modulated tap, then filtered

		uint32_t index(const uint32_t _table, const int32_t _i, const uint32_t _scale)
		{
			return _table + _scale * static_cast<uint32_t>(_i);
		}

		// 0x40097034 / 0x4009725e: two lines' shelving filter; the first line plus the diffused input
		template<typename Ram, typename Acc>
		void shelve(Memory& _mem, Ram& _ram, Acc& _acc, uint32_t _a, uint32_t _b, const uint32_t _state)
		{
			const int32_t c0 = _ram.word(Shelf), c1 = _ram.word(Shelf + 4), c2 = _ram.word(Shelf + 8);
			int32_t u = _ram.word(_state), v = _ram.word(_state + 4);
			int32_t ya = _ram.word(_state + 8), yb = _ram.word(_state + 12);
			uint32_t x = Diffused, o = Outs;
			for(uint32_t i = 0; i < Frames; ++i)
			{
				_acc.mac(0, c1, u);
				u = _ram.word(_a);
				_a += 4;
				_acc.mac(2, INT32_MAX, u);
				const int32_t d = _ram.word(x);
				x += 4;
				_acc.mac(2, INT32_MAX, d);
				_acc.mac(1, c1, v);
				v = _ram.word(_b);
				_b += 4;
				_acc.mac(1, c0, v);
				_acc.msac(1, c2, yb);
				_acc.msac(0, c2, ya);
				u = _acc.storeClear(2);
				_acc.mac(0, c0, u);
				yb = _acc.storeClear(1);
				_ram.setWord(o + 0x80, yb);
				ya = _acc.storeClear(0);
				_ram.setWord(o, ya);
				o += 4;
			}
			_ram.setWord(_state, u);
			_ram.setWord(_state + 4, v);
			_ram.setWord(_state + 8, ya);
			_ram.setWord(_state + 12, yb);
		}

		// 0x40097068 / 0x40097292: the modulated line read between samples into Tap, 4 taps of two phases of the
		// table, blended by the fraction (positions from modulatedPosition)
		template<typename Ram, typename Acc>
		void readModulated(Memory& _mem, Ram& _ram, Acc& _acc, const uint32_t _buffer)
		{
			const int32_t stepBack = _ram.word(0x8000c51c), phase = _ram.word(0x8000c520);
			const int32_t fraction = _ram.word(0x8000c524);
			uint32_t a = _buffer - static_cast<uint32_t>(asl(stepBack, 2));

			// The 140 bytes it reads from a, which the step moves, through the memory where they leave SRAM
			if constexpr(std::is_same_v<Ram, SramView>)
			{
				if(!SramView::holds(a, 140))
				{
					readModulated(_mem, _mem, _acc, _buffer);
					return;
				}
			}
			const uint32_t t = Interpolation + static_cast<uint32_t>(asl(phase, 5));
			int32_t k[8];
			for(uint32_t j = 0; j < 8; ++j)
				k[j] = _mem.word(t + 4 * j);
			int32_t d7 = _ram.word(a);
			a += 4;
			_acc.msac(2, k[1], d7);
			int32_t d1 = _ram.word(a);
			a += 4;
			_acc.msac(2, k[3], d1);
			d7 = _ram.word(a);
			_acc.msac(2, k[5], d7);
			uint32_t o = Tap;
			for(uint32_t i = 0; i < Frames; ++i)
			{
				_acc.msac(0, k[4], d7);
				d7 = _ram.word(a + 4);
				_acc.msac(2, k[7], d7);
				_acc.msac(0, k[6], d7);
				d7 = _ram.word(a - 4);
				_acc.msac(0, k[2], d7);
				d1 = _ram.word(a - 8);
				_acc.msac(0, k[0], d1);
				d1 = _acc.storeClear(2);
				_acc.mac(0, d1, fraction);
				_acc.msac(2, k[1], d7);
				d1 = _ram.word(a);
				a += 4;
				_acc.msac(2, k[3], d1);
				d7 = _ram.word(a);
				_acc.msac(2, k[5], d7);
				_ram.setWord(o, _acc.storeClear(0));
				o += 4;
			}
			_acc.storeClear(2);
		}

		// 0x40097158 / 0x4009738c: the four lines' inputs from the half's four outputs by the sign matrix
		template<typename Ram, typename Acc>
		void feed(Memory& _mem, Ram& _ram, Acc& _acc, const uint32_t _matrix, uint32_t _last)
		{
			int32_t h[4];
			for(uint32_t j = 0; j < 4; ++j)
				h[j] = _mem.word(_matrix + 4 * j);
			uint32_t c = Outs;
			int32_t x = _ram.word(c);
			c += 4;
			for(uint32_t i = 0; i < Frames; ++i)
			{
				// Accumulator n takes +h[n] x[n] and -h[j] x[j] for the others, in j's order (Emac4::macs: at once
				// from a clear accumulator in fractional mode), the same reads in the same order
				int32_t xs[4];
				xs[0] = x;
				for(uint32_t j = 1; j < 4; ++j)
					xs[j] = _ram.word(c + 124 + 128 * (j - 1));
				x = _ram.word(c);
				_acc.macs(0, Mac{h[0], xs[0]}, Msac{h[1], xs[1]}, Msac{h[2], xs[2]}, Msac{h[3], xs[3]});
				_acc.macs(1, Msac{h[0], xs[0]}, Mac{h[1], xs[1]}, Msac{h[2], xs[2]}, Msac{h[3], xs[3]});
				_acc.macs(2, Msac{h[0], xs[0]}, Msac{h[1], xs[1]}, Mac{h[2], xs[2]}, Msac{h[3], xs[3]});
				_acc.macs(3, Msac{h[0], xs[0]}, Msac{h[1], xs[1]}, Msac{h[2], xs[2]}, Mac{h[3], xs[3]});
				c += 4;
				_ram.setWord(_last - 0x210, _acc.storeClear(0));
				_ram.setWord(_last - 0x160, _acc.storeClear(1));
				_ram.setWord(_last - 0xb0, _acc.storeClear(2));
				_ram.setWord(_last, _acc.storeClear(3));
				_last += 4;
			}
		}

		template<typename Ram, typename Acc>
		void reverbWith(Memory& _mem, Ram& _ram, Acc& _acc, const uint32_t _out, const uint32_t _in,
			const uint32_t _params)
		{
			const int32_t hpf = _mem.half(_params + 8);
			const int32_t freq = static_cast<int8_t>(_mem.byte(_params + 4));
			const int32_t gain = _mem.half(_params + 6);
			const int32_t f = _mem.word(index(Shelves, freq, 4));
			_ram.setWord(Filters, _mem.word(index(Poles, hpf >> 6, 4)));
			const int32_t lpf = _mem.half(_params + 10);
			_ram.setWord(Filters + 4, _mem.word(index(Poles, (hpf + lpf) >> 6, 4)));
			const int32_t dec = static_cast<int8_t>(_mem.byte(_params + 2));
			const int32_t rest = subWrap(INT32_MAX, f);
			const int32_t pre = static_cast<int16_t>(_mem.half(_params));
			int32_t preSq = static_cast<int32_t>(static_cast<uint32_t>(pre * pre));
			preSq = addWrap(preSq, preSq);
			_acc.mac(0, rest, asl(gain, 16));
			_acc.mac(1, preSq, 0x4000);
			const int32_t shelfGain = _acc.storeClear(0);
			int32_t preDelay = addWrap(_acc.storeClear(1), 0x25);
			_ram.setWord(Shelf, addWrap(f, shelfGain));
			_ram.setWord(Shelf + 4, subWrap(f, shelfGain));
			_ram.setWord(Shelf + 8, subWrap(f, rest));
			if(preDelay > 0x4000)
				preDelay = 0x4000;
			_ram.setWord(PreDelay, preDelay);

			const int32_t diffusion = dec > 0x3f ? 0x5b851eb8 : _mem.word(index(Decays, dec, 4));
			for(uint32_t j = 0; j < 8; j += 4)
			{
				for(int n = 0; n < 4; ++n)
					_acc.mac(n, _ram.word(DiffuserBase + 4 * (j + static_cast<uint32_t>(n))), diffusion);
				for(int n = 0; n < 4; ++n)
					_ram.setWord(DiffuserGains + 4 * (j + static_cast<uint32_t>(n)), _acc.storeClear(n));
			}

			// Line 0: the mono input in, its output through the low-pass and the high-pass into Diffused
			{
				const int32_t hp = _ram.word(Filters), lp = _ram.word(Filters + 4);
				const int32_t lpHalf = addWrap(INT32_MIN, lp) >> 1;
				const int32_t hpHalf = addWrap(hp >> 1, 0x40000000);
				int32_t low = _ram.word(PreFilter), high = _ram.word(PreFilter + 4);
				uint32_t line = 0x8000caf0, in = _in, d = Diffused;
				_acc.mac(0, lp, low);
				int32_t delayed = _ram.word(line);
				for(uint32_t i = 0; i < Frames; ++i)
				{
					_acc.msac(0, lpHalf, delayed);
					_acc.msac(2, hpHalf, low);
					_acc.mac(2, hp, high);
					_acc.mac(1, 0x40000000, _ram.word(in));
					_acc.mac(1, 0x40000000, _ram.word(in + 4));
					in += 8;
					low = _acc.storeClear(0);
					_acc.mac(2, hpHalf, low);
					_acc.mac(0, lp, low);
					delayed = _ram.word(line + 4);
					_ram.setWord(line, _acc.storeClear(1));
					line += 4;
					high = _acc.storeClear(2);
					_ram.setWord(d, high);
					d += 4;
				}
				_ram.setWord(PreFilter, low);
				_ram.setWord(PreFilter + 4, high);
				_acc.storeClear(0);
			}

			// The 8 diffusers, each an allpass over its line's buffer, Diffused in place
			uint32_t b = 0x8000d104;
			for(uint32_t k = 0; k < 8; ++k)
			{
				uint32_t x = Diffused;
				const int32_t g = _ram.word(DiffuserGains + 4 * k);
				_acc.load(0, INT32_MIN);
				_acc.mac(0, g, g);
				int32_t d1 = _ram.word(x);
				x += 4;
				b += 0x1c;
				int32_t d2 = _ram.word(b);
				b += 4;
				const int32_t g2 = _acc.storeClear(0);

				// Each accumulator's terms follow a movclr, ACC0's a load (Emac4::loadMacs, Emac4::macs: at once in
				// fractional mode)
				for(uint32_t i = 0; i < Frames; ++i)
				{
					_ram.setWord(b - 8, _acc.storeClear(0));
					_acc.loadMacs(0, d1, Mac{g, d2});
					_ram.setWord(x - 8, _acc.storeClear(1));
					_acc.macs(1, Msac{g, d1}, Msac{g2, d2});
					d1 = _ram.word(x);
					x += 4;
					d2 = _ram.word(b);
					b += 4;
				}
				_ram.setWord(b - 8, _acc.storeClear(0));
				_ram.setWord(x - 8, _acc.storeClear(1));
			}

			// The first half: lines 1 and 2 shelved, line 4 read modulated, lines 3 and the tap shelved and summed
			shelve(_mem, _ram, _acc, 0x8000cba0, 0x8000cc50, 0x8000c5bc);
			readModulated(_mem, _ram, _acc, 0x8000cdb4);
			const int32_t c0 = _ram.word(Shelf), c1 = _ram.word(Shelf + 4), c2 = _ram.word(Shelf + 8);
			{
				int32_t s1 = _ram.word(0x8000c5cc), s2 = _ram.word(0x8000c5d0);
				int32_t y0 = _ram.word(0x8000c5d4), y1 = _ram.word(0x8000c5d8);
				uint32_t pl = 0x8000cd00, pm = 0x8000c410, pc = Outs, po = _out;
				for(uint32_t i = 0; i < Frames; ++i)
				{
					_acc.mac(0, c1, s1);
					s1 = _ram.word(pl);
					pl += 4;
					_acc.mac(0, c0, s1);
					_acc.msac(0, c2, y0);
					_acc.mac(1, c1, s2);
					s2 = _ram.word(pm + 128);
					_acc.mac(1, c0, s2);
					_acc.msac(1, c2, y1);
					const int32_t t0 = _ram.word(pc);
					pc += 4;
					_acc.mac(2, INT32_MAX, t0);
					_acc.mac(2, INT32_MAX, _ram.word(pc + 252));
					_acc.mac(3, INT32_MAX, _ram.word(pc + 124));
					_acc.mac(3, INT32_MAX, _ram.word(pc + 380));
					y0 = _acc.storeClear(0);
					_ram.setWord(pm, y0);
					pm += 4;
					_ram.setWord(po, _acc.storeClear(2));
					_ram.setWord(po + 4, _acc.storeClear(3));
					po += 8;
					y1 = _acc.storeClear(1);
					_ram.setWord(pm + 0x7c, y1);
				}
				_ram.setWord(0x8000c5cc, s1);
				_ram.setWord(0x8000c5d0, s2);
				_ram.setWord(0x8000c5d4, y0);
				_ram.setWord(0x8000c5d8, y1);
			}
			const uint32_t matrix = index(Matrix, dec, 32);
			feed(_mem, _ram, _acc, matrix, 0x8000cdb0);

			// The second half: line 8 at the cosine's position, lines 5 to 8 as lines 1 to 4, added into _out
			modulatedPosition(_mem, _acc, 0x8000c614, _mem.word(0x4550e1c8), 0x8000c52c);
			shelve(_mem, _ram, _acc, 0x8000ce60, 0x8000cf10, 0x8000c5dc);
			readModulated(_mem, _ram, _acc, 0x8000d074);
			{
				int32_t s1 = _ram.word(0x8000c5ec), s2 = _ram.word(0x8000c5f0);
				int32_t y0 = _ram.word(0x8000c5f4), y1 = _ram.word(0x8000c5f8);
				uint32_t pl = 0x8000cfc0, pm = 0x8000c410, pc = Outs, po = _out;
				for(uint32_t i = 0; i < Frames; ++i)
				{
					_acc.mac(0, c1, s1);
					s1 = _ram.word(pl);
					pl += 4;
					_acc.mac(0, c0, s1);
					_acc.msac(0, c2, y0);
					_acc.mac(1, c1, s2);
					s2 = _ram.word(pm + 128);
					_acc.mac(1, c0, s2);
					_acc.msac(1, c2, y1);
					_acc.mac(2, INT32_MAX, _ram.word(po));
					_acc.mac(3, INT32_MAX, _ram.word(po + 4));
					_acc.mac(2, INT32_MAX, _ram.word(pc + 128));
					_acc.mac(2, INT32_MAX, _ram.word(pc + 384));
					const int32_t t0 = _ram.word(pc);
					pc += 4;
					_acc.mac(3, INT32_MAX, t0);
					_acc.mac(3, INT32_MAX, _ram.word(pc + 252));
					y0 = _acc.storeClear(0);
					_ram.setWord(pm, y0);
					pm += 4;
					_ram.setWord(po, _acc.storeClear(2));
					_ram.setWord(po + 4, _acc.storeClear(3));
					po += 8;
					y1 = _acc.storeClear(1);
					_ram.setWord(pm + 0x7c, y1);
				}
				_ram.setWord(0x8000c5ec, s1);
				_ram.setWord(0x8000c5f0, s2);
				_ram.setWord(0x8000c5f4, y0);
				_ram.setWord(0x8000c5f8, y1);
			}
			feed(_mem, _ram, _acc, matrix + 16, 0x8000d070);
		}
	}

	void reverb(Memory& _mem, Emac4& _acc, const uint32_t _out, const uint32_t _in, const uint32_t _params)
	{
		// Every accumulator the reverb uses is cleared or loaded again within 8 steps: from clear accumulators in
		// fractional mode none can overflow (ProvedEmac4). Its buffers, state and lines are in SRAM, its input and
		// output where they are given: then on SRAM's bytes (SramView)
		if(!ProvedEmac4::proves(_acc) || !SramView::holds(_in, 8 * Frames) || !SramView::holds(_out, 8 * Frames))
		{
			reverbWith(_mem, _mem, _acc, _out, _in, _params);
			return;
		}
		SramView ram(_mem);
		ProvedEmac4 acc(_acc);
		reverbWith(_mem, ram, acc, _out, _in, _params);
		acc.done();
	}

	void advanceReverbLines(Memory& _mem)
	{
		for(uint32_t k = 0; k < 8; ++k)
		{
			const uint32_t w = static_cast<uint32_t>(_mem.word(0x8000d690 + 4 * k));
			_mem.setWord(0x8000d690 + 4 * k, static_cast<int32_t>((w + 0x80) & 0xfff));
			_mem.setWord(0x8000c6b0 + 0x20 * k, static_cast<int32_t>(w + ReverbDiffusers + 0x1000 * k));
		}
		for(uint32_t k = 0; k < 9; ++k)
		{
			const uint32_t w = static_cast<uint32_t>(_mem.word(0x8000c660 + 4 * k));
			_mem.setWord(0x8000c660 + 4 * k, static_cast<int32_t>((w + 0x80) & 0xffff));
			_mem.setWord(0x8000c7b0 + 0x20 * k, static_cast<int32_t>(w + ReverbLines + 0x10000 * k));
		}
	}
}
