// EFM-CP's voice code, ported as mdVoiceTrx.h says.
//
// EFM-CP ($1030EC, $1030F0, $103104): EFM-BD's two sines before their amp, their state three words up, at a level of
// its own. A Hit sets the level and a count of claps (Y:$0A). While claps remain the level decays by Y:$0B, and each
// time it falls under $66666 it jumps back up, one clap less; the last jump leaves it to decay by Y:$02. The result
// goes through a one-pole filter (Y:$09).
#include "mdVoiceEfm.h"

namespace md::voice::efm::cp
{
	namespace
	{
		constexpr uint32_t kClapLevel = 0x50c335;	// a Hit's level, and each clap's
		constexpr int32_t kClapFloor = 0x66666;
	}

	void init(Context& _c)
	{
		_c.y[0x12] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		y[0xc] = y[0xd] = y[0xe] = y[0xf] = y[0x13] = y[0x14] = y[0x17] = y[0x18] = 0;
		y[0x10] = y[7];
		y[0x11] = 0x7fffff;
		y[0x12] = kClapLevel;
		y[0x15] = y[0xa];
	}

	void render(Context& _c, Voice::Block& _out)
	{
		auto& y = _c.y;
		uint32_t out[32];
		bd::sines(_c, 0xc, out);

		uint32_t level = y[0x12], claps = y[0x15];
		for(size_t k = 0; k < 32; ++k)
		{
			out[k] = hi(round(mul(out[k], level)));
			level = lim(mul(level, claps != 0 ? y[0xb] : y[2]));
			if(claps != 0 && sx(level) < kClapFloor)
			{
				testHooks::probe(testHooks::EfmCpClap);
				level = kClapLevel;
				if(--claps == 0)
					testHooks::probe(testHooks::EfmCpLastClap);
			}
		}
		y[0x12] = level;
		y[0x15] = _c.shared.scratch.x[0x15] = claps;

		onePole(_c, out, y[9], 0x17, 0);
		for(size_t k = 0; k < 32; ++k)
			_out[k] = sx(out[k]);
	}
}
