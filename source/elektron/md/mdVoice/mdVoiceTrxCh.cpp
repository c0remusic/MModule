// TRX-CH's voice code, ported as mdVoiceTrx.h says.
//
// TRX-CH ($10201F, $102034, $10204D), each block: a noise scaled by Y:$05, plus six waves, each read from its own
// modulo buffer (TrxTables::chWaves) at its own gain; through a two-pole filter twice by Y:$03 and Y:$04, then two
// high-pass biquads by Y:$06 and Y:$07, then Y:$11 and Y:$12; then half of it at a level that a Hit raises, held
// while a counter (Y:$09) runs, falling by Y:$01 a sample while a second (Y:$0A) runs, and decaying by Y:$02 after.
#include "mdVoiceTrx.h"

namespace md::voice::trx::ch
{
	namespace
	{
		using Block = Voice::Block;
		using testHooks::probe;

		// A high-pass biquad (the DSP's loop at $10215E and $1021A1), 1/2 - z^-1 + 1/2 z^-2 over the poles _p, its taps
		// and coefficients at Y:$00-$04: the 32 words at X:_in after their last two at X:_in - 2 (from Y:_inPast and
		// Y:_inPast + 1), its outputs from X:_out on after its last two at X:_out - 2 (from Y:_outPast and
		// Y:_outPast + 1)
		void highPassBiquad(Context& _c, const Poles& _p, const uint32_t _in, const uint32_t _out,
			const uint32_t _inPast, const uint32_t _outPast)
		{
			auto& y = _c.y;
			auto& iy = _c.shared.scratch.y;
			auto& ix = _c.shared.scratch.x;
			iy[0] = 0x400000;
			iy[1] = 0x800000;
			iy[2] = 0x400000;
			iy[3] = _p.a2;
			iy[4] = _p.a1;
			ix[_in - 2] = y[_inPast];
			ix[_in - 1] = y[_inPast + 1];
			ix[_out - 2] = y[_outPast];
			ix[_out - 1] = y[_outPast + 1];
			const uint32_t* const in = &ix[_in];
			uint32_t* const out = &ix[_out];
			const uint32_t a1 = _p.a1, a2 = _p.a2;
			uint32_t x2 = y[_inPast], x1 = y[_inPast + 1], o2 = y[_outPast], o1 = y[_outPast + 1];
			for(int k = 0; k < 32; ++k)
			{
				const uint32_t x0 = in[k];
				const uint32_t o = lim(shl(highPassZeros(x2, x1, x0) + mul(a2, o2) + mul(a1, o1), 1));
				out[k] = o;
				x2 = x1;
				x1 = x0;
				o2 = o1;
				o1 = o;
			}
			y[_inPast] = in[30];
			y[_inPast + 1] = in[31];
			y[_outPast] = out[30];
			y[_outPast + 1] = out[31];
		}
	}

	void init(Context& _c)
	{
		auto& y = _c.y;
		y[0x10] = 0;
		y[0x13] = 0;
		y[0x14] = 1;
		for(uint32_t w = 0x21; w <= 0x26; ++w)
			y[w] = 0;
		y[0x11] = 0x30;
		y[0x12] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		y[0xb] = y[0xc] = y[0xd] = y[0xe] = y[0xf] = y[8];
		hitLevel(y[0x10], testHooks::ChLevelLimited, 0x6ccccd);	// more than the other TRX machines' $50C335
		for(uint32_t w = 0x15; w <= 0x20; ++w)
			y[w] = 0;
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& iy = _c.shared.scratch.y;
		auto& ix = _c.shared.scratch.x;
		// The modulo-32 buffer X:$20 (R5): the noise, the waves added, then the filters' output
		uint32_t* const sig = &ix[0x20];

		// The noise, scaled by Y:$05, then the six waves added at their gains, Y:$08 then Y:$0B to Y:$0F
		fibonacci(sig, y[0x13], y[0x14]);
		const uint32_t noiseLevel = y[5];
		for(int k = 0; k < 32; ++k)
			sig[k] = lim(mul(sig[k], noiseLevel));
		addChWaves(t, y, {y[8], y[0xb], y[0xc], y[0xd], y[0xe], y[0xf]}, 0x21, sig);

		// A two-pole filter twice by Y:$03 and Y:$04, the first's last two outputs at X:$00-$01
		{
			const auto p = poles(offsetPole(t, y[3], y[4]));
			iy[0] = p.a2;
			iy[1] = p.g;
			ix[0x1e] = y[0x17];
			ix[0x1f] = y[0x18];
			uint32_t h2 = y[0x15], h1 = y[0x16];
			twoPoles(p, sig, h2, h1);
			ix[0] = h2;
			ix[1] = h1;
			y[0x15] = h2;
			y[0x16] = h1;
			y[0x17] = sig[30];
			y[0x18] = sig[31];
		}

		// The two high-pass biquads: into X:$60, then back into X:$20
		highPassBiquad(_c, poles(offsetPole(t, y[6], y[7])), 0x20, 0x60, 0x19, 0x1b);
		highPassBiquad(_c, poles(offsetPole(t, y[0x11], y[0x12])), 0x60, 0x20, 0x1d, 0x1f);

		// Half of it at the level Y:$10: held while Y:$09 counts down, falling by Y:$01 a sample while Y:$0A does, then
		// decaying by Y:$02
		const auto put = [&](const int _k, const uint32_t _level)
		{
			_out[static_cast<size_t>(_k)] = sx(lim(mul(_level, sig[_k]) >> 1));
		};
		uint32_t level = y[0x10];
		if(countDown(y[9]))
		{
			probe(testHooks::ChHold);
			for(int k = 0; k < 32; ++k)
				put(k, level);
			return;
		}
		if(countDown(y[0xa]))
		{
			// The level kept unlimited, each sample's the limited word
			probe(testHooks::ChRamp);
			const int64_t step = acc(y[1]);
			int64_t b = acc(level);
			for(int k = 0; k < 32; ++k)
			{
				b -= step;
				put(k, level);
				level = lim(b);
			}
			y[0x10] = level;
			return;
		}
		probe(testHooks::ChDecay);
		const uint32_t rate = y[2];
		for(int k = 0; k < 32; ++k)
		{
			put(k, level);
			level = lim(mul(rate, level));
		}
		y[0x10] = level;
	}
}
