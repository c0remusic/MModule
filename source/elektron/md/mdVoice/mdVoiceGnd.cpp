// GND-SN's and GND-NS's voice code, ported as mdVoiceTrx.h says. GND-- and GND-IM are in mdVoice.cpp.
#include "mdVoiceTrx.h"

namespace md::voice::gnd
{
	using namespace trx;

	namespace
	{
		// A 48-bit level by a word, as the DSP's MPYSU and DMAC multiply one
		int64_t scaled(const uint32_t _s, const int64_t _level)
		{
			return dmac(mulSU(_s, lo(_level)), mul(_s, hi(_level)));
		}
	}

	// GND-SN ($1000A3, $1000A4, $1000AD): a sine, a quarter of full scale, at the pitch Y:$01 plus an envelope, Y:$03 of
	// a level that Y:$04 decays once a block, at a 48-bit amp level that Y:$02 decays. The DSP keeps the sine in
	// X:$00-$1F.
	void updateSn(Context& _c)
	{
		auto& y = _c.y;
		y[0xa] = y[6] = 0x7ffff8;
		y[7] = y[8] = y[9] = 0;
	}

	void renderSn(Context& _c, Voice::Block& _out)
	{
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;

		const int64_t step = (acc(y[1]) + mul(y[6], y[3])) >> 6;
		y[6] = lim(mul(y[6], y[4]));

		sines(tables(_c), y, 7, 8, step, [&](const int _k, const uint32_t _w) { ix[_k] = lim(acc(_w) >> 2); });

		int64_t level = acc(y[0xa], y[9]);
		for(size_t k = 0; k < 32; ++k)
		{
			_out[k] = sx(lim(scaled(ix[k], level)));
			level = scaled(y[2], level);
		}
		y[0xa] = hi(level);
		y[9] = lo(level);
	}

	// GND-NS ($1000DD, $1000E4, $1000EA): the 24-bit Fibonacci noise, in X:$00-$1F, at a 48-bit amp level that Y:$01
	// decays and of which it hears the upper word alone
	void initNs(Context& _c)
	{
		_c.y[3] = 0;
		_c.y[4] = 1;
	}

	void updateNs(Context& _c)
	{
		_c.y[2] = 0x7ffcb9;
		_c.y[5] = 0;
	}

	void renderNs(Context& _c, Voice::Block& _out)
	{
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;

		fibonacci(&ix[0], y[3], y[4]);

		int64_t level = acc(y[2], y[5]);
		for(size_t k = 0; k < 32; ++k)
		{
			_out[k] = sx(lim(mul(hi(level), ix[k])));
			level = scaled(y[1], level);
		}
		y[2] = hi(level);
		y[5] = lo(level);
	}
}
