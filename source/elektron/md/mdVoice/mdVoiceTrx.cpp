// The TRX machines' tables and their routines by machine type; each machine is in a file of its own (mdVoiceTrxBd.cpp
// for TRX-BD and so on), their shared helpers in mdVoiceTrx.h.
#include "mdVoiceTrx.h"

namespace md::voice
{
	using namespace dsp;

	// The boot's sine (P:$100069-$10008B): s[n+1] = 2c s[n] - s[n-1] in 48-bit double precision, c = cos(2 pi /
	// 32768) = $7FFFFF:$D88586, from 0 and $000648:$7ED46B
	TrxTables::TrxTables()
	{
		constexpr uint32_t cHi = 0x7fffff, cLo = 0xd88586;
		uint32_t h[2] = {0, 0x648};
		uint32_t l[2] = {0, 0x7ed46b};
		sine[0] = 0;
		sine[1] = 0x648;
		int r = 1;
		for(size_t n = 2; n < sine.size(); ++n)
		{
			const uint32_t y1 = h[r], y0 = l[r];
			r ^= 1;
			int64_t a = wrap56(mulUU(cLo, y0));
			a = dmac(a, mulSU(cHi, y0));
			a = wrap56(a + mulSU(y1, cLo));		// MACSU
			a = dmac(a, mul(cHi, y1));
			a = shl(a, 1);
			a = wrap56(a - acc(h[r], l[r]));
			limL(a, h[r], l[r]);
			sine[n] = lim(a);
		}
	}
}

namespace md::voice::trx
{
	namespace
	{
		constexpr Routines kBd = {bd::init, bd::update, bd::render};
		constexpr Routines kCb = {cb::init, cb::update, cb::render};
		constexpr Routines kCp = {cp::init, cp::update, cp::render};
		constexpr Routines kRs = {rs::init, rs::update, rs::render};
		constexpr Routines kSd = {sd::init, sd::update, sd::render};
		constexpr Routines kXt = {xt::init, xt::update, xt::render};
	}

	const Routines* routines(const uint32_t _type)
	{
		switch(_type)
		{
			case 17:	return &kBd;
			case 18:	return &kSd;
			case 19:	return &kXt;
			case 20:	return &kCp;
			case 21:	return &kRs;
			case 22:	return &kCb;
			default:	return nullptr;
		}
	}
}
