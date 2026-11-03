// TRX-CB's voice code, ported as mdVoiceTrx.h says.
//
// TRX-CB ($101EA0, $101EBB, $101EC4), each block: two sines, the first at Y:$03 less a detune, the second at Y:$04
// plus it, the detune decaying by Y:$02 a block; each clipped to a square past its threshold either way, scaled by its
// own level times the amp envelope, and through two high-pass filters, as TRX-CP's clap; a quarter of their sum, two
// samples late. The block after a Hit fades the last block's last sample out (x 0.9 a sample), the two after it are
// silent.
#include "mdVoiceTrx.h"

namespace md::voice::trx::cb
{
	namespace
	{
		using Block = Voice::Block;
		using testHooks::probe;

		// A sine word past the threshold either way clipped to near full scale: $7FFCB9 above it, $800347 below its
		// negative, the second test the last word
		uint32_t clipSquare(const uint32_t _w, const int64_t _threshold)
		{
			const int64_t a = acc(_w);
			uint32_t b = _w;
			if(a > _threshold)
			{
				probe(testHooks::CbClipAbove);
				b = 0x7ffcb9;
			}
			if(-a > _threshold)
			{
				probe(testHooks::CbClipBelow);
				b = 0x800347;
			}
			return b;
		}
	}

	void init(Context& _c)
	{
		auto& y = _c.y;
		y[0x10] = y[0x11] = y[0x12] = y[0x13] = 0;
		y[0xc] = y[0xd] = 0x7f;
		for(uint32_t w = 0x15; w <= 0x20; ++w)
			y[w] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		y[0xe] = 3;
		hitLevel(y[0xf], testHooks::CbLevelLimited);
	}

	void render(Context& _c, Block& _out)
	{
		auto& y = _c.y;

		// The three blocks after a Hit: the last block's last sample faded out, then two silent
		if(y[0xe] != 0)
		{
			const int64_t left = acc(y[0xe]) - acc(1);
			y[0xe] = lim(left);
			if(left != 0 && left != acc(1))
			{
				probe(testHooks::CbFade);
				fadeOut(y[0x14], _out);
				return;
			}
			probe(left == 0 ? testHooks::CbSilentB : testHooks::CbSilentA);
			_out.fill(0);
			return;
		}

		const auto& t = tables(_c);
		auto& ix = _c.shared.scratch.x;
		// The modulo-32 buffers X:$20 (R1), the first sine, and X:$60 (R3), the second
		uint32_t* const a = &ix[0x20];
		uint32_t* const b = &ix[0x60];

		// The sines at half their pitch, each clipped past its threshold (Y:$07, Y:$08), then scaled by its level
		// (Y:$05, Y:$06) times the amp envelope Y:$0F, which decays by Y:$01; the detune Y:$0B decays by Y:$02. A word
		// at a time: the DSP's three passes over each block do nothing across words
		const uint32_t detune = y[0xb], level = y[0xf];
		const uint32_t levelA = lim(mul(level, y[5])), levelB = lim(mul(level, y[6]));
		const int64_t thresholdA = acc(y[7]), thresholdB = acc(y[8]);
		sinesMaskedAfter(t, y, 0x10, 0x11, (acc(y[3]) - acc(detune)) >> 1,
			[&](const int _k, const uint32_t _w) { a[_k] = lim(mul(clipSquare(_w, thresholdA), levelA)); });
		sinesMaskedAfter(t, y, 0x12, 0x13, (acc(y[4]) + acc(detune)) >> 1,
			[&](const int _k, const uint32_t _w) { b[_k] = lim(mul(clipSquare(_w, thresholdB), levelB)); });
		y[0xb] = lim(mul(y[2], detune));
		y[0xf] = lim(mul(level, y[1]));

		// Each through the two high-pass filters, by Y:$09 and Y:$0C, then Y:$0A and Y:$0D: the first sine's outputs
		// from X:$02, the second's from X:$42
		highPassStage(_c, poles(offsetPole(t, y[9], y[0xc])), 0x20, 0x40, 0x00, 0x15);
		highPassStage(_c, poles(offsetPole(t, y[0xa], y[0xd])), 0x60, 0x20, 0x40, 0x1b);

		// A quarter of their sum, two samples late
		uint32_t last = 0;
		for(int k = 0; k < 32; ++k)
		{
			last = lim((acc(ix[k]) + acc(ix[0x40 + k])) >> 2);
			_out[static_cast<size_t>(k)] = sx(last);
		}
		y[0x14] = last;
	}
}
