// The TRX machines' tables and their routines by machine type; each machine is in a file of its own (mdVoiceTrxBd.cpp
// for TRX-BD and so on), their shared helpers in mdVoiceTrx.h.
#include "mdVoiceTrx.h"

#include <cmath>

namespace md::voice
{
	using namespace dsp;

	namespace
	{
		// X:$140000-$1420FF, the two-pole filters', from what the words mean. A filter has a cutoff and a Q; its poles are
		// e^(-sigma +- j t), sigma and t each a frequency on a scale of 4096 steps from 0.02 Hz to 20 kHz at 44.1 kHz.
		// For Q from 0.51 to 3.5 over 128 steps, the first table holds how far t lies below the cutoff on that scale,
		// sqrt(1 - 1 / (4 Q^2)), the second sigma's, 1 / (2 Q). The pole tables hold cos(w) and e^-w over sqrt 2,
		// written with 9 decimals before they were made 24-bit words, as every word of the DSP's shows.
		void makeFilters(decltype(TrxTables::filters)& _filters)
		{
			const double range = std::log(1e6);
			const auto step = [&](const double _ratio)
			{
				return static_cast<uint32_t>(std::lround(4096.0 / range * std::log(_ratio))) & 0xffffff;
			};
			for(size_t q = 0; q < 0x80; ++q)
			{
				const double sigma = 1.0 / (1.02 + 5.98 * static_cast<double>(q) / 127.0);
				_filters[q] = step(std::sqrt(1.0 - sigma * sigma));
				_filters[0x80 + q] = step(sigma);
			}
			const auto word = [](const double _x)
			{
				const double decimal = std::round(_x * std::sqrt(0.5) * 1e9) / 1e9;
				return static_cast<uint32_t>(std::lround(decimal * 8388608.0)) & 0xffffff;
			};
			constexpr double pi = 3.14159265358979323846;
			for(size_t i = 0; i < 0x1000; ++i)
			{
				const double w = 2.0 * pi * 0.02 * std::pow(1e6, static_cast<double>(i) / 4096.0) / 44100.0;
				_filters[0x100 + i] = word(std::cos(w));
				_filters[0x1100 + i] = word(std::exp(-w));
			}
		}
	}

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
		makeFilters(filters);
	}
}

namespace md::voice::trx
{
	namespace
	{
		constexpr Routines kB2 = {b2::init, b2::update, b2::render};
		constexpr Routines kBd = {bd::init, bd::update, bd::render};
		constexpr Routines kCb = {cb::init, cb::update, cb::render};
		constexpr Routines kCh = {ch::init, ch::update, ch::render};
		constexpr Routines kCl = {cl::init, cl::update, cl::render};
		constexpr Routines kCp = {cp::init, cp::update, cp::render};
		constexpr Routines kCy = {cy::init, cy::update, cy::render};
		constexpr Routines kMa = {ma::init, ma::update, ma::render};
		constexpr Routines kRs = {rs::init, rs::update, rs::render};
		constexpr Routines kS2 = {s2::init, s2::update, s2::render};
		constexpr Routines kSd = {sd::init, sd::update, sd::render};
		constexpr Routines kXc = {xt::init, xt::update, xc::render};
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
			case 23:	return &kCh;
			// TRX-OH ($1021F4, $102209, $102222) is TRX-CH's code word for word but for its DO loops' end
			// addresses, the same waves at X:$102600 included: only the OS's packets tell them apart. Their types
			// differ, so a voice that goes from one to the other runs init, as on the DSP
			case 24:	return &kCh;
			case 25:	return &kCy;
			case 26:	return &kMa;
			case 27:	return &kCl;
			case 28:	return &kXc;
			case 29:	return &kB2;
			case 30:	return &kS2;
			default:	return nullptr;
		}
	}
}
