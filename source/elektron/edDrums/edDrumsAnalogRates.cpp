#include "edDrumsAnalogRates.h"

#include <array>
#include <cmath>

#include "edDrumsAnalogPort.h"

namespace elektron::drums::analog::port
{
	namespace
	{
		// A rate code for a per-tick decrement _d: _d = (1 - m / 2^17) / 2^s, s in bits 16-19, m below
		uint32_t encode(const double _d)
		{
			int s = static_cast<int>(std::floor(-std::log2(_d)));
			auto m = static_cast<long>(std::lround((1 - std::ldexp(_d, s)) * 131072));
			if(m >= 0x10000)
			{
				++s;
				m = std::lround((1 - std::ldexp(_d, s)) * 131072);
			}
			return static_cast<uint32_t>(s) << 16 | static_cast<uint32_t>(m);
		}

		// A table of codes from a Chebyshev series of the 60 dB time in ticks, x = (k - 64) / 63 for k 1-127, fitted
		// to the time's relative error. k = 0 is the code of a decrement of almost all of the level, the shortest the
		// generator has
		template<size_t N> std::array<uint32_t, 128> fromSeries(const double (&_c)[N])
		{
			std::array<uint32_t, 128> t{};
			t[0] = 0x80;
			for(int k = 1; k < 128; ++k)
			{
				const double x = (k - 64) / 63.0;
				double b0 = 0, b1 = 0;
				for(size_t i = N; i-- > 1;)
				{
					const double b = 2 * x * b0 - b1 + _c[i];
					b1 = b0;
					b0 = b;
				}
				const double ticks = x * b0 - b1 + _c[0];
				t[static_cast<size_t>(k)] = encode(1 - std::exp(-std::log(1000.0) / ticks));
			}
			return t;
		}

		// The best fits of the CPU's tables, each code within 1 of the original's (the time within 1.6e-5).
		// synthEnvelope: 69 of the 127 codes exact; chipEnvelope: 75
		constexpr double SynthSeries[] = {115310.29353423875, 141292.82311080984, 25776.799920594451,
			-2506.6043428633384, -1891.6087803039045, -151.40481383140494, 90.032371785467461, 23.05765869129598,
			-2.0796627802822862, -1.9008518905284966, -0.30966623957383543, -0.062598790505994317,
			-0.12157305143864444, -0.17181805605153647, -0.096046444405194512};
		constexpr double ChipSeries[] = {92361.351677152299, 134280.58335747285, 50027.213473182223,
			6299.5602047153534, -3165.9510468425769, -1798.7739009445577, -254.48485067004543, 117.49395040220674,
			68.256414083567392, 9.5160586001766063, -4.6859529021456137, -2.7860396362412954, -0.47109419834742933,
			0.13233238234011893, 0.075328458374151655};

		// Built at static initialisation, so that a test sees them built
		std::array<uint32_t, 128> synthRates = fromSeries(SynthSeries);
		std::array<uint32_t, 128> chipRates = fromSeries(ChipSeries);
		// What the CPU reads below two tables: other tables of its own (rate codes, switch numbers), which a real knob
		// value never reaches: 0 here
		std::array<int32_t, 128> rawLevelBelow{};
		std::array<uint32_t, 128> synthRateBelow{};

		Table synthTableList[] = {
			{"synthEnvelopeRate", synthRates.data(), 128, 4, 1},
			{"chipEnvelopeRate", chipRates.data(), 128, 4, 1},
			{"rawLevelBelow", rawLevelBelow.data(), 128, 4, 2147483648},
			{"synthEnvelopeRateBelow", synthRateBelow.data(), 128, 4, 2147483648},
		};
	}

	namespace rates
	{
		uint32_t synthEnvelope(const int _k) { return synthRates[static_cast<size_t>(_k)]; }
		uint32_t chipEnvelope(const int _k) { return chipRates[static_cast<size_t>(_k)]; }
		int32_t belowRawLevel(const int _k) { return rawLevelBelow[static_cast<size_t>(128 + _k)]; }
		uint32_t belowSynthEnvelope(const int _k) { return synthRateBelow[static_cast<size_t>(128 + _k)]; }
	}

	const Table* synthTables(int& _count)
	{
		_count = static_cast<int>(std::size(synthTableList));
		return synthTableList;
	}
}
