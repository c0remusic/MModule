// TRX-XC's voice code, ported as mdVoiceTrx.h says.
//
// TRX-XC ($102B87, $102B88, $102B96) is TRX-XT's, word for word, but for the second and third blocks after a Hit: a
// silent block, then a sampled waveform scaled by a packet word.
#include "mdVoiceTrx.h"

namespace md::voice::trx::xc
{
	namespace
	{
		using testHooks::probe;
	}

	void render(Context& _c, Voice::Block& _out)
	{
		auto& y = _c.y;

		const uint32_t count = y[0xb];
		if(count != 1 && count != 2)
		{
			xt::render(_c, _out);
			return;
		}
		y[0xb] = count - 1;
		if(count == 2)
		{
			probe(testHooks::XcSilent);
			_out.fill(0);
			return;
		}
		probe(testHooks::XcWave);
		const auto& wave = tables(_c).xcWave;
		for(size_t k = 0; k < 32; ++k)
			_out[k] = sx(lim(mul(wave[k], y[9])));
	}
}
