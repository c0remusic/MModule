// The ROM machines' voice code (DSP2, OS 1.63: init $13D, update $15A, render $16C), shared by ROM-01 to -48, ported
// instruction by instruction: the same integers, 56-bit accumulators, limiting where the DSP stores an accumulator, and
// the same internal scratch memory (X:$000-$1FF, Y:$00-$FF), shared by the voices as the DSP's (md::voice::Scratch).
//
// What the render does, each block: from the sample's 12-bit codes (two a word, from the bank) it builds a window of
// 148 pointers into the decoding table (X:$00-$93), the position moving by a step that the pitch and the sample's rate
// give, forward or reversed, looping at the loop start or holding the last sample at the end, and restarting at the
// start when a retrigger falls in the block (RTRG, RTIM); it decodes them in place, interpolates 128 values at four
// times the rate (7 taps of $E0 phases), filters them (a two-pole), decimates twice by 2 (the last a 14-tap symmetric
// half-band), reduces the bits (BRR), then applies the amp envelope (HOLD, DEC) and a second envelope that fades a
// voice that stops or retriggers out; a voice that starts while the last one is near silence crossfades in instead.
#include "mdVoiceDsp.h"
#include "mdVoiceInternal.h"

#include <stdexcept>

namespace md::voice::testHooks
{
	Hooks& hooks()
	{
		static Hooks h;
		return h;
	}
}

namespace md::voice::rom
{
	namespace
	{
		using Block = Voice::Block;
		using namespace dsp;
		using testHooks::probe;

		// The external memory the routines read: the decoding table, the bank's directory and samples
		uint32_t external(const Context& _c, const uint32_t _addr)
		{
			if(_addr - 0x146000 < 0x1000)
				return _c.shared.tables->decode[_addr - 0x146000] & 0xffffff;
			const auto* bank = _c.shared.bank;
			if(bank && _addr - RomBank::DirectoryAddr < RomBank::DirectoryWords)
				return bank->directory[_addr - RomBank::DirectoryAddr] & 0xffffff;
			if(bank && _addr - RomBank::DataAddr < bank->dataWords)
				return bank->data[_addr - RomBank::DataAddr] & 0xffffff;
			const auto& hooks = testHooks::hooks();
			return hooks.stray ? hooks.stray(hooks.strayUser, _addr) & 0xffffff : 0;
		}

		// The registers that live across the routines
		struct Regs
		{
			int64_t a = 0, b = 0;
			uint32_t x0 = 0;
		};

		// $15A, which the render calls as well: B and X0 are what the render goes on with
		void trigger(Context& _c, Regs& _r)
		{
			auto& y = _c.y;
			uint32_t r0 = y[0xd];
			_r.x0 = external(_c, r0++);
			y[0x12] = _r.x0;
			_r.b = acc(1);
			_r.a = acc(external(_c, r0++));
			if(_r.a != 0)
			{
				y[0x1c] = lim(_r.a);
				y[0x26] = lim(_r.b);
				_r.b = 0;
				_r.x0 = y[0x22];
				y[0x29] = _r.x0;
				y[0x18] = 0;
				y[0x22] = 0;
			}
			y[0x2c] = lim(_r.b);
		}
	}

	void init(Context& _c)
	{
		auto& y = _c.y;
		// The directory entry of the type's ROM number
		int64_t a = acc(_c.type) - acc(0x81);
		y[0xf] = lim(a);
		a = shl(a, 2) + acc(RomBank::DirectoryAddr);
		y[0xd] = lim(a);
		y[0x22] = imm8(1);
		y[0x10] = 0;
		y[0x11] = 0;
		y[0x2d] = y[0x2e] = y[0x2f] = y[0x30] = 0;
		for(uint32_t k = 0; k < 0x1b; ++k)
			_c.x[k] = 0;
		y[0x1d] = y[0x1e] = y[0x1f] = y[0x20] = 0;
	}

	void update(Context& _c)
	{
		Regs r;
		trigger(_c, r);
	}

	void render(Context& _c, Block& _out)
	{
		if(!_c.shared.tables)
			throw std::logic_error("md::voice: a ROM machine renders without the voice DSP's tables");
		const auto& t = *_c.shared.tables;
		auto& y = _c.y;
		auto& xs = _c.x;

		// The internal memory the render uses as scratch (masked indices keep a pointer out of the window in it; a
		// peripheral or other address it names reads 0)
		auto& ix = _c.shared.scratch.x;
		auto& iy = _c.shared.scratch.y;
		const auto yRead = [&](const uint32_t _addr) -> uint32_t
		{
			if(_addr < 0x100)
				return iy[_addr];
			if(_addr - 0x163 < t.interpolator.size())
				return t.interpolator[_addr - 0x163];
			return external(_c, _addr);
		};
		const auto xAt = [&](const uint32_t _addr) -> uint32_t& { return ix[_addr & 0x1ff]; };

		// The dispatcher leaves $20 in X0 (its bank flip); the render calls update itself while its last found
		// the slot empty
		Regs r;
		r.x0 = 0x20;
		r.b = 0;
		if(y[0x2c] != 0)
			trigger(_c, r);
		if(r.b != 0)
		{
			_out.fill(0);
			return;
		}
		int64_t a = r.a, b = r.b;
		uint32_t x0 = r.x0, x1 = 0, y0 = 0, y1 = 0;

		// $173: the length again, the sample may have changed under the voice
		if(y[0x22] == 0)
		{
			uint32_t r0 = y[0xd] + 1;
			a = acc(external(_c, r0++));
			b = acc(y[0x1c]);
			y[0x13] = lim(a);
			if(a >= b)
				b = a;
			y[0x1c] = lim(b);
			if(b != a)
			{
				b = acc(y[0x10], y[0x11]);
				if(b - a < 0)
				{
					y[0x22] = imm8(1);
					y[0x12] = 0;
					_out.fill(0);
					return;
				}
			}
			// $18A: a Hit starts the sample
			if(y[0x26] != 0)
			{
				r0 = y[0xd];
				y[0x27] = y[9];
				y[0x1b] = 0;
				y[0x1a] = 0;
				y[0x26] = 0;
				y[0x28] = x0;
				y[0x17] = imm8(0x80);
				y[0x2b] = imm8(0x80);
				y[0x15] = 0x7fffff;
				y[0x2a] = 0x7fffff;
				y[0x25] = y[5];
				r0 += 2;
				y[0x14] = external(_c, r0++);
				y[0x16] = external(_c, r0++);
				a = mul(y[3], y[0x1c]);
				y[0xe] = y[8];
				y[0x10] = lim(a);
			}
		}

		// $1AB
		if(y[0x12] == 0)
		{
			_out.fill(0);
			return;
		}
		x0 = y[0x1c];
		y0 = y[3];
		a = mul(y0, x0);
		y1 = y[0x25];
		b = shl(mul(x0, y1), 1);
		y[0x24] = lim(a);
		y[0x19] = lim(b);
		b -= a;
		a = acc(y[5]);
		y1 = lim(b);
		a = shl(a, 1);
		a -= acc(y0);
		b = acc(y[0x19]);
		y[0x21] = lim(a);
		if(a < 0)
		{
			a = acc(y1);
			if(a >= 0)
				b = 0;
		}
		else
		{
			a = acc(x0) - acc(1);
			x0 = lim(a);
			a = acc(y1);
			if(a < 0)
				b = acc(x0);
		}
		// $1C7
		y[0x19] = lim(b);
		x0 = y[0x1c];
		xAt(0xfb) = x0;
		b = acc(y[0x18]) + acc(0x20);
		if(b >= 0)
		{
			y[0x18] = lim(b);
			y0 = y[4];
			if(b - acc(y0) >= 0)
			{
				x0 = y[2];
				y[0x17] = x0;
			}
		}

		// $1D4: the step, its 128 steps, the position
		x0 = y[1];
		x1 = y[0x16];
		a = mul(x1, x0);
		b = a >> 18;
		a = acc(y[0x10], y[0x11]);
		limL(b, xAt(0xfd), iy[0xfd]);
		b = shl(b, 7);
		limL(b, xAt(0xf9), iy[0xf9]);
		limL(a, xAt(0xff), iy[0xff]);
		xAt(0xfc) = 0xffffff;
		x0 = 0x80;
		y1 = 0x93;
		a = acc(y[0x1a]);
		xAt(0xf8) = y1;
		a += acc(x0);
		x1 = y[0x27];
		y0 = lim(a);
		a -= acc(x1);

		// The retrigger counter; on to $222 when the retriggers are done
		bool retrigger = true;
		if(a > 0)
		{
			b = acc(y[0x1b]);
			y[0x1a] = lim(a);
			b += acc(1);
			x1 = y[0xe];
			y0 = lim(a);
			if(b - acc(x1) >= 0)
			{
				retrigger = false;
			}
			else
			{
				y[0x1b] = hi(b);
				y[0x25] = y[5];
				y[0x2b] = imm8(0x80);
				y[0x2a] = 0x7fffff;
			}
		}
		else
		{
			y[0x1a] = y0;
			b = acc(y[0x1b]) + acc(1);
			x1 = y[0xe];
			y0 = lim(a);
			retrigger = b - acc(x1) < 0;
		}
		if(retrigger)
		{
			// $204: where in the window the retrigger falls
			a = acc(x0) - acc(y0);
			if(a < 0)
				a = 0;
			y0 = y[1];
			x1 = y[0x16];
			const uint32_t a1 = hi(a);
			a = mul(x1, y0);
			y0 = a1;
			x1 = lo(a);
			b = static_cast<int64_t>(sx(y0)) * static_cast<int64_t>(x1) * 2;	// MPYSU
			x1 = hi(a);
			b = (b >> 24) + mul(y0, x1);	// DMAC
			b = shl(b, 5);
			x1 = hi(b);
			bool done = false;
			if(b - acc(y1) < 0)
			{
				xAt(0xf8) = x1;
				xAt(0xfc) = x1;
				a = acc(xAt(0xf9), iy[0xf9]);
				if(a - acc(x1) < 0)
				{
					done = true;
				}
				else
				{
					// ($21B negates A for a reversed voice, which nothing reads)
					b = acc(y[0x24]) - acc(x1);
					y[0x10] = hi(b);
					y[0x11] = lo(b);
				}
			}
			if(!done)
				y[0x27] = y[9];
		}

		// $222: the position after the block, held at the end or looped
		b = acc(xAt(0xf9), iy[0xf9]);
		if(sx(y[0x21]) < 0)
			b = -b;
		a = acc(y[0x10], y[0x11]);
		a += b;
		bool stop = false;
		if(b < 0)
		{
			b = acc(y[0x19]);
			if(a - b <= 0)
			{
				if(a < 0)
					a = 0;
				stop = true;
				probe(testHooks::BackwardStop);
			}
		}
		else
		{
			b = acc(y[0x19]);
			if(a - b >= 0)
			{
				// Past the end: on from the loop start when the end is the sample's and it loops
				x1 = xAt(0xfb);
				bool loops = false;
				if(b - acc(x1) >= 0)
				{
					b = acc(y[0x14]);
					loops = b >= 0;
				}
				if(loops)
				{
					a += b;
					a -= acc(x1);
				}
				else
				{
					if(a - acc(x1) >= 0)
						a = acc(x1) - acc(1);
					stop = true;
				}
			}
		}
		if(stop)
		{
			x0 = 0x810625;
			y[0x2b] = x0;
			y[0x22] = x0;
		}
		// $247
		y[0x10] = hi(a);
		y[0x11] = lo(a);
		x0 = 0;
		xAt(0xfa) = 0;

		// A fill count past $7FFFFF, a position past the length (a loop shorter than the block's advance, RomBank): the
		// DSP's DO counts it as 2^24 passes and stalls. The port renders the block silent and returns there, its state
		// as the DSP had stored it before that loop (the position past the length, which stays past it until a Hit sets
		// it again), the scratch as far as the fills got: the simplest state that is the DSP's up to the stall
		const auto stalls = [&](const uint32_t _count)
		{
			if(_count <= 0x7fffff)
				return false;
			probe(testHooks::Stall);
			_out.fill(0);
			return true;
		};

		// $3BC and $3CF: n5 pairs of pointers from the sample word at r4, forward or backward, the first sample alone
		// when y0's parity says the position falls in a word's second
		const auto fillForward = [&](uint32_t& _r0, uint32_t _r4, const uint32_t _n5)
		{
			a = acc(0x146000);
			b = a;
			x0 = 0x01800c;
			x1 = 0x00c018;
			if(y0 & 1)
			{
				probe(testHooks::ForwardOdd);
				y0 = external(_c, _r4++);
				b = insert(b, x1, y0);
				xAt(_r0++) = lim(b);
			}
			y0 = external(_c, _r4++);
			for(uint32_t k = 0; k < _n5; ++k)
			{
				a = insert(a, x0, y0);
				b = insert(b, x1, y0);
				xAt(_r0++) = hi(a);
				xAt(_r0++) = lim(b);
				y0 = external(_c, _r4++);
			}
		};
		const auto fillReverse = [&](uint32_t& _r0, uint32_t _r4, const uint32_t _n5)
		{
			a = acc(0x146000);
			b = a;
			x0 = 0x01800c;
			x1 = 0x00c018;
			if(!(y0 & 1))
			{
				probe(testHooks::ReverseEven);
				y0 = external(_c, _r4--);
				b = insert(b, x0, y0);
				xAt(_r0++) = lim(b);
			}
			y0 = external(_c, _r4--);
			for(uint32_t k = 0; k < _n5; ++k)
			{
				a = insert(a, x1, y0);
				b = insert(b, x0, y0);
				xAt(_r0++) = lim(a);
				y0 = external(_c, _r4--);
				xAt(_r0++) = hi(b);
			}
		};

		if(sx(y[0x21]) >= 0)
		{
			// $24F: forward to the end, then from the loop start or the last sample held; again from the start for
			// a retrigger in the block
			for(;;)
			{
				b = acc(xAt(0xf8));
				x0 = xAt(0xfa);
				uint32_t r0 = x0;
				b -= acc(x0);
				y0 = xAt(0xff);
				a = acc(xAt(0xfb));
				x0 = lim(b);
				a -= acc(y0);
				b = acc(y0);
				if(a - acc(x0) > 0)
					a = acc(x0);
				y1 = lim(a);
				a >>= 1;
				a += acc(1);
				b >>= 1;
				x1 = y[0x12];
				uint32_t n5 = lim(a);
				b += acc(x1);
				if(stalls(n5))
					return;
				fillForward(r0, hi(b), n5);
				a = acc(xAt(0xfa)) + acc(y1);
				b = acc(xAt(0xf8));
				r0 = lim(a);
				b -= a;
				b = (b >> 1) + acc(1);
				y1 = y[0x12];
				n5 = hi(b);
				if(stalls(n5))
					return;
				b = acc(y[0x14]);
				y0 = hi(b);
				b >>= 1;
				if(b < 0)
				{
					probe(testHooks::HeldLastSample);
					x0 = xAt(r0 - 1);
					for(uint32_t k = 0; k < n5; ++k)
					{
						xAt(r0++) = x0;
						xAt(r0++) = x0;
					}
				}
				else
				{
					b += acc(y1);
					fillForward(r0, hi(b), n5);
				}
				b = acc(xAt(0xfc));
				if(b < 0)
					break;
				xAt(0xf8) = 0x94;
				xAt(0xfa) = lim(b);
				xAt(0xff) = y[0x24];
				xAt(0xfc) = 0xffffff;
			}
		}
		else
		{
			// $28A: backward to the start, then the silent code; again from the start for a retrigger
			for(;;)
			{
				b = acc(xAt(0xf8));
				x0 = xAt(0xfa);
				a = acc(xAt(0xff), iy[0xff]);
				uint32_t r0 = x0;
				b -= acc(x0);
				y1 = 0;
				if(b - a > 0)
					b = a;
				y0 = 0xffffff;
				a += acc(y1, y0);
				y1 = hi(b);
				b >>= 1;
				b += acc(1);
				y0 = hi(a);
				a >>= 1;
				const uint32_t n5 = hi(b);
				if(stalls(n5))
					return;
				x0 = y[0x12];
				a += acc(x0);
				fillReverse(r0, hi(a), n5);
				b = acc(xAt(0xf8));
				a = acc(xAt(0xfa));
				a += acc(y1);
				b -= a;
				r0 = lim(a);
				// Before a retrigger in the block, an odd count leaves the window's last pointer there unwritten
				if((hi(b) & 1) && sx(xAt(0xfc)) >= 0)
					probe(testHooks::ReverseOddRetrigger);
				b >>= 1;
				if(stalls(hi(b)))
					return;
				x0 = 0x146800;
				for(uint32_t k = 0, n = hi(b); k < n; ++k)
				{
					xAt(r0++) = x0;
					xAt(r0++) = x0;
				}
				a = acc(xAt(0xfc));
				if(a < 0)
					break;
				xAt(0xfa) = hi(a);
				xAt(0xff) = y[0x24];
				xAt(0xfc) = 0xffffff;
				xAt(0xf8) = 0x94;
			}
			a = -acc(xAt(0xff), iy[0xff]);
			iy[0xff] = lo(a);
		}

		// $2C2: the 148 pointers decoded in place, a ring X:$00-$93 written two words behind its reads
		{
			uint32_t r0 = 0, r1 = 0x92;
			const auto put = [&](const uint32_t _v)
			{
				xAt(r1) = _v;
				r1 = r1 == 0x93 ? 0 : r1 + 1;
			};
			for(int k = 0; k < 37; ++k)
			{
				const uint32_t p4 = xAt(r0++), p5 = xAt(r0++), p6 = xAt(r0++), p7 = xAt(r0++);
				put(lim(a));
				a = acc(yRead(p4));
				put(lim(b));
				b = acc(yRead(p5));
				put(lim(a));
				a = acc(yRead(p6));
				put(lim(b));
				b = acc(yRead(p7));
			}
			put(lim(a));
			put(lim(b));
		}

		// $2D5: 128 values at four times the rate into Y:$00-$7F, two a step of the loop, 7 taps each
		{
			// A phase's row is $1D3 plus $70 times a signed fraction, $163-$242, and six rows of $E0 follow: always
			// in the table
			const auto coef = [&](const uint32_t _addr) { return t.interpolator[_addr - 0x163]; };
			a = iy[0xff];
			const uint32_t r2 = 0xa0;
			const uint32_t r1 = 0xfd;
			x1 = 0x70;
			x0 = 0x1d3;
			y0 = lo(a);
			xAt(r2) = x0;
			b = mul(x1, y0);
			uint32_t r5 = 0;
			for(int k = 0; k < 64; ++k)
			{
				y1 = xAt(r1);
				y0 = iy[r1];
				b += acc(x0);
				uint32_t r3 = hi(a);
				const int64_t step = acc(y1, y0);
				a += step;
				uint32_t r4 = hi(b);
				b = acc(x0);
				x0 = lo(a);
				uint32_t r0 = hi(a);
				b += mul(x1, x0);
				a += step;
				x0 = xAt(r3++);
				y1 = coef(r4);
				r4 += 0xe0;
				uint32_t r7 = hi(b);
				b = mul(x0, y1);
				x0 = xAt(r3++);
				y1 = coef(r4);
				r4 += 0xe0;
				for(int j = 0; j < 4; ++j)
				{
					b += mul(x0, y1);
					x0 = xAt(r3++);
					y1 = coef(r4);
					r4 += 0xe0;
				}
				b += mul(x0, y1);
				x0 = xAt(r3++);
				y1 = coef(r4);
				b += mul(x0, y1);
				x0 = xAt(r0++);
				y1 = coef(r7);
				r7 += 0xe0;
				y0 = coef(r7);
				r7 += 0xe0;
				iy[r5++ & 0xff] = lim(b);
				b = mul(x0, y1);
				x0 = xAt(r0++);
				b += mul(y0, x0);
				x0 = xAt(r0++);
				y1 = coef(r7);
				r7 += 0xe0;
				for(int j = 0; j < 3; ++j)
				{
					b += mul(x0, y1);
					x0 = xAt(r0++);
					y1 = coef(r7);
					r7 += 0xe0;
				}
				b += mul(x0, y1);
				x0 = xAt(r0++);
				y1 = coef(r7);
				b += mul(x0, y1);
				x0 = lo(a);
				iy[r5++ & 0xff] = lim(b);
				b = mul(x1, x0);
				x0 = xAt(r2);
			}
		}

		// $2FE: a two-pole over the 128, its input two behind on a ring through Y:$80-$81 (the last block's), into
		// X:$00-$81
		{
			iy[0x80] = y[0x1d];
			iy[0x81] = y[0x1e];
			uint32_t r4 = 0x80, r5 = 0, r0 = 0;
			const auto next4 = [&]
			{
				const uint32_t v = iy[r4];
				r4 = r4 == 0x81 ? 0 : r4 + 1;
				return v;
			};
			x0 = 0x4ad49b;
			y1 = 0x11aa63;
			x1 = y[0x1f];
			b = acc(y[0x20]);
			y0 = iy[r5++];
			a = acc(next4());
			for(int k = 0; k < 64; ++k)
			{
				xAt(r0++) = x1;
				a += mul(y0, x0);
				y0 = iy[r5++];
				{
					const uint32_t nx1 = lim(b);
					const int64_t nb = acc(next4());
					a -= mul(x1, x0);
					x1 = nx1;
					b = nb;
				}
				xAt(r0++) = x1;
				b += mul(y1, y0);
				y0 = iy[r5++ & 0xff];
				{
					const uint32_t nx1 = lim(a);
					const int64_t na = acc(next4());
					b -= mul(y1, x1);
					x1 = nx1;
					a = na;
				}
			}
			xAt(r0++) = x1;
			xAt(r0++) = lim(b);
			y[0x1f] = x1;
			y[0x20] = lim(b);
			y[0x1d] = lim(a);
			x0 = next4();
			y[0x1e] = x0;
		}

		// $31E: by 2, into Y:$0D-$2C and Y:$3A-$5A
		{
			uint32_t r0 = 2, r4 = 0xd, r5 = 0x3a;
			y0 = imm8(0x40);
			x0 = xAt(r0++);
			for(int k = 0; k < 32; ++k)
			{
				a = mul(y0, x0);
				x0 = xAt(r0++);
				iy[r5++] = lim(b);
				a += mul(y0, x0);
				x0 = xAt(r0++);
				b = mul(y0, x0);
				x0 = xAt(r0++);
				iy[r4++] = lim(a);
				b += mul(y0, x0);
				x0 = xAt(r0++);
			}
			iy[r5] = lim(b);
		}

		// $32A: the decimator's history, 13 and 8 words, swapped with X:S+$00-$14
		{
			uint32_t r0 = 0, r1 = 0, r4 = 0, r5 = 0x20;
			a = acc(xs[r0++]);
			b = acc(iy[r5++]);
			for(int k = 0; k < 13; ++k)
			{
				iy[r4++] = lim(a);
				a = acc(xs[r0++]);
				xs[r1++] = lim(b);
				b = acc(iy[r5++]);
			}
			r5 = 0x53;
			r4 = 0x33;
			b = acc(iy[r5++]);
			for(int k = 0; k < 8; ++k)
			{
				iy[r4++] = lim(a);
				a = acc(xs[r0++]);
				xs[r1++] = lim(b);
				b = acc(iy[r5++]);
			}
		}

		// $33A: by 2 again, 14 symmetric taps and the centre one, into X:$01-$20 (X:$00 takes B, unused)
		uint32_t r1 = 0;
		{
			const auto& c = t.decimator;
			uint32_t r0 = 0, r4 = 0, r5 = 0x34;
			x1 = 0x27eccb;
			x0 = c[r0++];
			y0 = iy[r4++];
			for(int k = 0; k < 16; ++k)
			{
				for(int half = 0; half < 2; ++half)
				{
					int64_t& d = half ? b : a;
					if(half)
					{
						b = mul(y0, x0);
						x0 = c[r0++];
						y0 = iy[r4++];
					}
					else
					{
						a = mul(y0, x0);
						x0 = c[r0++];
						y0 = iy[r4++];
					}
					for(int j = 0; j < 4; ++j)
					{
						d += mul(y0, x0);
						x0 = c[r0++];
						y0 = iy[r4++];
					}
					d += mul(y0, x0);
					x0 = c[r0--];
					y0 = iy[r4++];
					xAt(r1++) = lim(half ? a : b);
					d += mul(y0, x0);
					y0 = iy[r4++];
					for(int j = 0; j < 5; ++j)
					{
						d += mul(y0, x0);
						x0 = c[r0--];
						y0 = iy[r4++];
					}
					d += mul(y0, x0);
					x0 = c[r0];
					y0 = iy[r4];
					r4 -= 12;
					d += mul(y0, x0);
					x0 = c[r0++];
					y0 = iy[r5++];
					d += mul(x1, y0);
					y0 = iy[r4++];
				}
			}
		}

		// $364: BRR, two stages behind its reads, over X:$01-$22
		uint32_t r0 = 1;
		xAt(r1++) = lim(b);
		x1 = y[6];
		y1 = y[7];
		for(int k = 0; k < 34; ++k)
		{
			const uint32_t ny0 = lim(a);
			a = round(mul(x1, x0));
			x0 = xAt(r0);
			y0 = ny0;
			xAt(r0++) = lim(b);
			b = mul(y1, y0);
			b = shl(b, 20);
		}

		// $36D: the amp envelope over X:$04-$23, 8 samples at the last block's decay, then 24 at this one's
		const auto envelope = [&](uint32_t _first, const uint32_t _envelope, const uint32_t _floor,
			const uint32_t _decay, const auto& _read, const auto& _write)
		{
			y1 = y[_envelope];
			y0 = y[_first];
			b = -mul(y0, y0);
			a = -mul(y1, y0);
			x0 = _read();
			y0 = lim(b);
			b = mul(x0, y1);
			for(int k = 0; k < 8; ++k)
			{
				const uint32_t ny1 = lim(a);
				a = -mul(y1, y0);
				x0 = _read();
				y1 = ny1;
				_write(lim(b));
				b = mul(x0, y1);
			}
			b = acc(y[_floor]);
			if(a >= b)
				b = a;
			y0 = y[_decay];
			y1 = lim(b);
			b = -mul(y0, y0);
			a = -mul(y1, y0);
			y[_first] = y0;
			y0 = lim(b);
			b = mul(x0, y1);
			for(int k = 0; k < 24; ++k)
			{
				const uint32_t ny1 = lim(a);
				a = -mul(y1, y0);
				x0 = _read();
				y1 = ny1;
				_write(lim(b));
				b = mul(x0, y1);
			}
			y[_envelope] = lim(a);
		};
		{
			xAt(r0++) = lim(b);
			// The first read advances, the others write where they read
			uint32_t rr = 4;
			bool first = true;
			// MPY -Y0,Y0,B with its parallel store came first above
			envelope(0x2e, 0x2d, 0x15, 0x17,
				[&]
				{
					if(first)
					{
						first = false;
						return xAt(rr++);
					}
					return xAt(rr);
				},
				[&](const uint32_t _v) { xAt(rr++) = _v; });
		}
		a = 0;
		y[0x15] = 0;

		// $385: the fade a stop or a retrigger runs, into the output from X:$05; a Hit after near silence
		// crossfades in instead
		a = acc(y[0x28]);
		const bool started = (hi(a) & 1) != 0;
		a |= 0x1000000;
		size_t o = 0;
		const auto fade = [&]
		{
			uint32_t rr = 5;
			envelope(0x30, 0x2f, 0x2a, 0x2b, [&] { return xAt(rr++); }, [&](const uint32_t _v) { _out[o++] = sx(_v); });
			y[0x2a] = 0;
		};
		if(started)
		{
			fade();
			return;
		}
		b = acc(y[0x2d]);
		y[0x28] = hi(a);
		if(b - acc(0x8000) > 0 || y[0x29] != 0)
		{
			fade();
			return;
		}
		// $3AA
		r0 = 5;
		for(int k = 0; k < 6; ++k)
			_out[o++] = sx(xAt(r0++));
		x0 = 0x055555;
		y0 = xAt(r0++);
		b = acc(x0);
		y1 = x0;
		b += acc(x0);
		x1 = xAt(r0++);
		a = acc(y0);
		for(int k = 0; k < 25; ++k)
		{
			_out[o++] = sx(lim(a));
			a = acc(y0);
			a += mul(y1, x1);
			a -= mul(y1, y0);
			const uint32_t ny1 = lim(b);
			x1 = xAt(r0++);
			b += acc(x0);
			y1 = ny1;
		}
		_out[o++] = sx(lim(a));
	}
}
