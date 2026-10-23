#include "stMainDelay.h"

#include "stEmac.h"
#include "stMainFx.h"

namespace syntakt::main
{
	namespace
	{
		constexpr uint32_t Frames = 32;
		constexpr uint32_t Poles = 0x4029bffc;
		constexpr uint32_t Fade = 0x8000d908;			// the new tap's full weight, which fades out next block
		constexpr uint32_t Level = 0x8000d90c;			// the 1/32 the clipped signal is scaled by, ramped
		constexpr uint32_t HighPole = 0x8000d910;
		constexpr uint32_t LowPole = 0x8000d914;
		constexpr uint32_t Feedback = 0x8000d918;		// the last block's feedback gain
		constexpr uint32_t Scratch = 0x8000d91c;		// 32 frames: the taps clipped and scaled
		constexpr uint32_t Filters = 0x8000da1c;		// the low-pass L, R, then the high-pass L, R
		constexpr uint32_t Widths = 0x8000c300;			// the four input gains, ramped
		constexpr uint32_t Buffer = 0x8000d6b0;			// a long: the taps' buffer, the ring's next input
		constexpr uint32_t DelayWrite = 0x8000dac8;

		uint32_t pole(const int32_t _index) { return Poles + 4 * static_cast<uint32_t>(_index); }

		int32_t mul32(const int32_t _a, const int32_t _b)
		{
			return static_cast<int32_t>(static_cast<uint32_t>(static_cast<int64_t>(_a) * _b));
		}

		template<typename Ram, typename Acc>
		void delayWith(Memory& _mem, Ram& _ram, Acc& _acc, const uint32_t _out, const uint32_t _in,
			const uint32_t _params)
		{
			const int32_t cross = _mem.half(_params + 2);
			int32_t fdbk = _mem.half(_params + 6);
			if(fdbk > 0x7f00)
				fdbk = 0x7f00;
			const int32_t feedback = subSat(mul32(-66052, fdbk), fdbk);
			const int32_t hpf = _mem.half(_params + 8);
			const int32_t lastFeedback = _ram.word(Feedback);
			int32_t level = _ram.word(Level);
			_ram.setWord(Feedback, feedback);
			_ram.setWord(Level, 0x4000000);
			const int32_t lpf = _mem.half(_params + 10);
			int32_t fadeOut = _ram.word(Fade);
			_ram.setWord(HighPole, _mem.word(pole(hpf >> 6)));
			_ram.setWord(LowPole, _mem.word(pole((hpf + lpf) >> 6)));
			_acc.mac(0, INT32_MAX, INT32_MAX);
			int32_t fadeIn = 0;
			const int32_t levelStep = subWrap(0x4000000, level) >> 5;
			const int32_t full = _acc.storeClear(0);
			_ram.setWord(Fade, full);
			const int32_t inStep = full >> 5, outStep = fadeOut >> 5;

			// The taps: A (the new time) at the buffer, B (the last block's) 0x110 after it, crossfaded, clipped,
			// scaled. Each frame's L product of B is taken before the loop turns: the last one (frame 32) stays in
			// acc 2
			const uint32_t taps = static_cast<uint32_t>(_ram.word(Buffer));
			uint32_t p = taps + 4;
			_acc.mac(2, fadeOut, _ram.word(taps + 0x110));
			int32_t d1 = _ram.word(taps);
			for(uint32_t i = 0; i < Frames; ++i)
			{
				_acc.mac(2, fadeIn, d1);
				d1 = _ram.word(p + 272);
				_acc.mac(3, fadeOut, d1);
				d1 = _ram.word(p);
				p += 4;
				_acc.mac(3, fadeIn, d1);
				fadeIn = addWrap(fadeIn, inStep);
				fadeOut = subWrap(fadeOut, outStep);
				const int32_t l = _acc.store(2), r = _acc.store(3);
				_acc.msac(2, INT32_MIN, l);
				_acc.msac(2, l, l ^ (l >> 31));
				_acc.msac(3, INT32_MIN, r);
				_acc.msac(3, r, r ^ (r >> 31));
				const int32_t clippedL = _acc.storeClear(2);
				_acc.mac(1, level, clippedL);
				_acc.mac(2, fadeOut, _ram.word(p + 272));
				d1 = _ram.word(p);
				p += 4;
				const int32_t clippedR = _acc.storeClear(3);
				_acc.mac(3, level, clippedR);
				level = addWrap(level, levelStep);
				_ram.setWord(Scratch + 8 * i, _acc.storeClear(1));
				_ram.setWord(Scratch + 8 * i + 4, _acc.storeClear(3));
			}
			_acc.storeClear(0);

			// The high-pass and the low-pass, x 16, into the buffer
			const int32_t hp = _ram.word(HighPole), lp = _ram.word(LowPole);
			const int32_t lpLess = addWrap(INT32_MIN, lp);
			const int32_t hpHalf = addWrap(hp >> 1, 0x40000000);
			int32_t lowL = _ram.word(Filters), lowR = _ram.word(Filters + 4);
			int32_t highL = _ram.word(Filters + 8), highR = _ram.word(Filters + 12);
			int32_t xl = asl(_ram.word(Scratch), 4), xr = asl(_ram.word(Scratch + 4), 4);
			for(uint32_t i = 0; i < Frames; ++i)
			{
				_acc.msac(2, hpHalf, lowL);
				_acc.msac(3, hpHalf, lowR);
				_acc.mac(0, lp, lowL);
				_acc.msac(0, lpLess, xl);
				_acc.msac(0, lpLess, xl);
				_acc.mac(1, lp, lowR);
				_acc.msac(1, lpLess, xr);
				_acc.msac(1, lpLess, xr);
				_acc.mac(2, hp, highL);
				_acc.mac(3, hp, highR);
				lowL = _acc.storeClear(0);
				_acc.mac(2, hpHalf, lowL);
				xl = asl(_ram.word(Scratch + 8 * (i + 1)), 4);
				lowR = _acc.storeClear(1);
				_acc.mac(3, hpHalf, lowR);
				xr = asl(_ram.word(Scratch + 8 * (i + 1) + 4), 4);
				highL = _acc.storeClear(2);
				_ram.setWord(taps + 8 * i, highL);
				highR = _acc.storeClear(3);
				_ram.setWord(taps + 8 * i + 4, highR);
			}
			_ram.setWord(Filters, lowL);
			_ram.setWord(Filters + 4, lowR);
			_ram.setWord(Filters + 8, highL);
			_ram.setWord(Filters + 12, highR);

			// The output, then the feedback (ramped from the last block's) in place, L and R crossed when X is set
			const int32_t feedbackStep = subWrap(feedback, lastFeedback) >> 5;
			int32_t g = lastFeedback;
			for(uint32_t i = 0; i < Frames; ++i)
			{
				const int32_t l = _ram.word(taps + 8 * i), r = _ram.word(taps + 8 * i + 4);
				_ram.setWord(_out + 8 * i, l);
				_acc.msac(0, g, l);
				_ram.setWord(_out + 8 * i + 4, r);
				_acc.msac(1, g, r);
				g = addWrap(g, feedbackStep);
				const int32_t fl = _acc.storeClear(0), fr = _acc.storeClear(1);
				_ram.setWord(taps + 8 * i + (cross ? 4 : 0), fl);
				_ram.setWord(taps + 8 * i + (cross ? 0 : 4), fr);
			}

			// The input's four gains from WID: with X, L and R each to both sides by (1 - w, w); without, a width
			_acc.setMacsr(0xa0);
			const int32_t wid = _mem.half(_params + 4);
			const int32_t w = asl(wid, 16);
			int32_t t0, t1, t2, t3;
			if(cross)
			{
				t0 = t1 = subWrap(INT32_MAX, w);
				t2 = t3 = w;
			}
			else if(w <= 0x3fffffff)
			{
				t0 = addWrap(w, 0x40000000);
				t1 = subWrap(0x40000000, w);
				t2 = 0;
				t3 = asl(wid, 17);
			}
			else
			{
				const int32_t rest = subWrap(INT32_MAX, w);
				t0 = addWrap(rest, rest);
				t1 = 0;
				t2 = addWrap(w, static_cast<int32_t>(0xc0000000));
				t3 = addWrap(subWrap(0x40000000, w), INT32_MAX);
			}
			int32_t g0 = _ram.word(Widths), g1 = _ram.word(Widths + 4), g2 = _ram.word(Widths + 8);
			int32_t g3 = _ram.word(Widths + 12);
			const int32_t s0 = subWrap(t0, g0) >> 5, s1 = subWrap(t1, g1) >> 5, s2 = subWrap(t2, g2) >> 5;
			const int32_t s3 = subWrap(t3, g3) >> 5;
			for(uint32_t i = 0; i < Frames; ++i)
			{
				const int32_t l = _ram.word(_in + 8 * i), r = _ram.word(_in + 8 * i + 4);
				_acc.mac(0, g0, l);
				_acc.mac(0, g1, r);
				_acc.mac(1, g2, l);
				_acc.mac(0, INT32_MAX, _ram.word(taps + 8 * i));
				_acc.mac(1, g3, r);
				_acc.mac(1, INT32_MAX, _ram.word(taps + 8 * i + 4));
				g0 = addWrap(g0, s0);
				g1 = addWrap(g1, s1);
				g2 = addWrap(g2, s2);
				g3 = addWrap(g3, s3);
				_ram.setWord(taps + 8 * i, _acc.storeClear(0));
				_ram.setWord(taps + 8 * i + 4, _acc.storeClear(1));
			}
			_ram.setWord(Widths, g0);
			_ram.setWord(Widths + 4, g1);
			_ram.setWord(Widths + 8, g2);
			_ram.setWord(Widths + 12, g3);
		}
	}

	void delay(Memory& _mem, Emac4& _acc, const uint32_t _out, const uint32_t _in, const uint32_t _params)
	{
		// Every accumulator the delay uses is cleared again within 4 steps, and MACSR stays fractional: from clear
		// accumulators in fractional mode none can overflow (ProvedEmac4). Its state is in SRAM, its taps' buffer
		// (0x214 bytes from the long at Buffer), input and output where they are given: then on SRAM's bytes
		// (SramView)
		const uint32_t taps = static_cast<uint32_t>(_mem.word(Buffer));
		if(!ProvedEmac4::proves(_acc) || !SramView::holds(taps, 0x214) || !SramView::holds(_in, 8 * Frames) ||
			!SramView::holds(_out, 8 * Frames))
		{
			delayWith(_mem, _mem, _acc, _out, _in, _params);
			return;
		}
		SramView ram(_mem);
		ProvedEmac4 acc(_acc);
		delayWith(_mem, ram, acc, _out, _in, _params);
		acc.done();
	}

	void writeLines(Memory& _mem)
	{
		const uint32_t write = static_cast<uint32_t>(_mem.word(DelayWrite));
		_mem.setWord(DelayWrite, static_cast<int32_t>((write + 0x20) & 0x7ffff));
		const uint32_t src = static_cast<uint32_t>(_mem.word(Buffer));
		const uint32_t dst = (write << 3) + DelayRing;
		_mem.setWord(0x8000da40, static_cast<int32_t>(src));
		_mem.setWord(0x8000da50, static_cast<int32_t>(dst));
		Tcd t;
		t.saddr = src;
		t.attr = 0x04b4;
		t.soff = 0x10;
		t.nbytes = 0x10;
		t.daddr = dst;
		t.citer = t.biter = 0xd410;
		t.doff = 0x10;
		t.dlastSga = 0x8000c6a0;
		t.csr = 0x11;
		runChannel(_mem, t);
	}
}
