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
		// The E12 machines' offsets are the same two, for Q from 0.51 to 14.
		void makeFilters(decltype(TrxTables::filters)& _filters, decltype(TrxTables::e12Offsets)& _e12Offsets)
		{
			const double range = std::log(1e6);
			const auto step = [&](const double _ratio)
			{
				return static_cast<uint32_t>(std::lround(4096.0 / range * std::log(_ratio))) & 0xffffff;
			};
			const auto offsets = [&](uint32_t* const _out, const double _span)
			{
				for(size_t q = 0; q < 0x80; ++q)
				{
					const double sigma = 1.0 / (1.02 + _span * static_cast<double>(q) / 127.0);
					_out[q] = step(std::sqrt(1.0 - sigma * sigma));
					_out[0x80 + q] = step(sigma);
				}
			};
			offsets(_filters.data(), 5.98);
			offsets(_e12Offsets.data(), 26.98);
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

		// X:$142949 and $142B49: the radius that falls by 60 dB in t, from _first to _last seconds over the table at
		// 44.1 kHz, written with 8 decimals before it was made a 24-bit word
		void makeDamping(std::array<uint32_t, 512>& _table, const double _first, const double _last)
		{
			for(size_t i = 0; i < _table.size(); ++i)
			{
				const double t = _first + static_cast<double>(i) * (_last - _first) / 511.0;
				const double decimal = std::round(std::pow(0.001, 1.0 / (44100.0 * t)) * 1e8) / 1e8;
				_table[i] = static_cast<uint32_t>(std::llround(decimal * 8388608.0));
			}
		}

		// X:$145224 and $145424: a second-order Chebyshev II filter by its bilinear transform, of stopband _rs dB and edge
		// _first to _last of Nyquist, a geometric step over 128 rows, as four words: -a2, -(a1 + 1) / 2, b0 / 2, b1 / 2.
		// b2 is b0. _high: a high-pass, else a low-pass
		void makeShelf(std::array<uint32_t, 512>& _table, const double _rs, const double _first, const double _last,
			const bool _high)
		{
			constexpr double pi = 3.14159265358979323846;
			const double mu = std::asinh(std::sqrt(std::pow(10.0, 0.1 * _rs) - 1.0)) / 2.0;
			// The analog prototype, a pole pair s^2 + A s + B over its zeros s^2 + 2, gain B / 2
			const double m = std::cosh(2.0 * mu) / 2.0;
			const double A = std::sqrt(2.0) * std::sinh(mu) / m, B = 1.0 / m;
			for(size_t row = 0; row < 128; ++row)
			{
				const double wn = _first * std::pow(_last / _first, static_cast<double>(row) / 127.0);
				const double T = std::tan(pi * wn / 2.0), T2 = T * T;
				double b0, b1, a0, a1, a2;
				if(_high)
				{
					b0 = T2 + 2.0;
					b1 = 2.0 * T2 - 4.0;
					a0 = T2 + A * T + B;
					a1 = 2.0 * T2 - 2.0 * B;
					a2 = T2 - A * T + B;
				}
				else
				{
					b0 = 1.0 + 2.0 * T2;
					b1 = 4.0 * T2 - 2.0;
					a0 = 1.0 + A * T + B * T2;
					a1 = 2.0 * B * T2 - 2.0;
					a2 = 1.0 - A * T + B * T2;
				}
				const double gain = B / 2.0 / a0;
				const double words[4] = {-a2 / a0, (-a1 / a0 - 1.0) / 2.0, gain * b0 / 2.0, gain * b1 / 2.0};
				for(size_t k = 0; k < 4; ++k)
					_table[4 * row + k] = static_cast<uint32_t>(std::llround(words[k] * 8388608.0)) & 0xffffff;
			}
		}

		// Y:$147200: the cubic B-spline's taps at the positions -1 to 2, for t = k / 256, each row at k + 128 mod 256. Its
		// words are integers summed: k^3 / 6 at full scale $7FFFFF for the two outer taps; for the two inner, $555555
		// (2/3) - t^2 + t^3 / 2 at full scale 2^23, each term rounded. One word does not follow: the DSP's holds 1 less
		// for k = 11, and no form tried (float32, decimals, scales, offsets) gives it.
		void makeInterpolation(decltype(TrxTables::interpolation)& _table)
		{
			const auto outer = [](const int64_t _k)
			{
				const int64_t n = _k * _k * _k * 0x7fffff, d = 6ll << 24;
				return static_cast<uint32_t>((2 * n + d) / (2 * d));
			};
			const auto inner = [](const int64_t _k)
			{
				const int64_t rounded = (_k * _k * _k + 2) / 4;
				return static_cast<uint32_t>(0x555555 - 128 * _k * _k + rounded - (_k == 11 ? 1 : 0));
			};
			for(uint32_t row = 0; row < 256; ++row)
			{
				const int64_t k = (row + 128) & 0xff;
				_table[4 * row] = outer(256 - k);
				_table[4 * row + 1] = inner(k);
				_table[4 * row + 2] = inner(256 - k);
				_table[4 * row + 3] = outer(k);
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
		makeFilters(filters, e12Offsets);
		makeInterpolation(interpolation);
		makeDamping(piDamp, 0.0005, 4.0);
		makeDamping(piRing, 0.0005 / 128.0, 4.0 / 128.0);
		makeShelf(piLowCut, 30.0, 0.01, 0.08, true);
		makeShelf(piHighCut, 15.0, 0.4, 0.85, false);
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
