// EFM-XT's voice code, ported as mdVoiceTrx.h says.
//
// EFM-XT ($10304E, $103052, $103065): EFM-BD's two sines word for word, their state two words up, through a one-pole
// filter (Y:$09) with every word rounded. A Hit starts both phases at Y:$0A.
#include "mdVoiceEfm.h"

namespace md::voice::efm::xt
{
	void init(Context& _c)
	{
		_c.y[0x11] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		hitLevel(y[0x11], testHooks::EfmXtLevelLimited, 0x400000);	// less than the other machines' $50C335
		y[0xb] = y[0xd] = y[0xa];
		y[0xc] = y[0xe] = y[0x12] = y[0x13] = y[0x14] = y[0x15] = 0;
		y[0xf] = y[7];
		y[0x10] = 0x7fffff;
	}

	void render(Context& _c, Voice::Block& _out)
	{
		uint32_t out[32];
		bd::pair(_c, 0xb, out);
		onePole(_c, out, _c.y[9], 0x14, 32);
		for(size_t k = 0; k < 32; ++k)
			_out[k] = sx(out[k]);
	}
}
