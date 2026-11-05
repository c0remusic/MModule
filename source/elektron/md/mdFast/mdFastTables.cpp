#include "mdFastTables.h"

#include <algorithm>
#include <cmath>

namespace md::fast::tables
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846, Rate = 44100;
		constexpr int32_t FullScale = 0x7fffff;

		// The cosines round to the nearest word, away from zero at a half. eqCos alone takes a nudge: at 3 of its 2589
		// words (the scale's -55, 945, 1911) the cosine lies 0.00015 under a half and the OS has it over, which _half
		// 0.5002 gives (0.50015 to 0.5003 do). Why the OS rounds so is not known. The radii take their 10 decimals
		// first
		int32_t cosine(const double _w, const double _half)
		{
			const double c = FullScale * std::cos(_w);
			const auto magnitude = static_cast<int32_t>(std::floor(std::abs(c) + _half));
			return c < 0 ? -magnitude : magnitude;
		}

		int32_t decay(const double _w)
		{
			const double decimal = std::floor(std::exp(-_w) * 1e10 + 0.5) / 1e10;
			return std::min(static_cast<int32_t>(std::llround(8388608.0 * decimal)), FullScale);
		}

		double cutoff(const int _i) { return 2 * Pi * 5 * std::pow(4000.0, _i / 2048.0) / Rate; }
		double eqFrequency(const int _i) { return 2 * Pi * 20 * std::pow(1000.0, _i / 2048.0) / Rate; }

		// A damping as the two offsets on a scale of _k steps per e-fold of frequency
		void offsets(const double _b, const double _k, int16_t& _cos, int16_t& _exp)
		{
			_cos = static_cast<int16_t>(std::lround(_k * 0.5 * std::log(1 - _b * _b)));
			_exp = static_cast<int16_t>(std::lround(_k * std::log(_b)));
		}

		// ln b as a polynomial, cubic but for the boost's zero, of x = (EQG - 63.5) / 64, fitted: EQG 0 to 62 cuts, 64
		// to 127 boost, about 0.235 dB a step, and the damping halves through 63, which is flat at b = 0.4. Each fit
		// lies well inside the interval that the offsets' rounding leaves it, and no form was found for them
		double quartic(const double _x, const double (&_c)[5])
		{
			return _c[0] + _x * (_c[1] + _x * (_c[2] + _x * (_c[3] + _x * _c[4])));
		}

		constexpr double CutPole[5] = {-0.717147, -0.4368612, 0.0238397, 0.0644923, 0};
		constexpr double CutZero[5] = {-0.7055149, 1.2844071, 0.0026649, 0.0527128, 0};
		constexpr double BoostPole[5] = {-1.4086557, -0.4474621, 0.0868283, 0.0218466, 0};
		constexpr double BoostZero[5] = {-1.3965915, 1.3015957, 0.0309666, 0.0967734, -0.0340695};
	}

	Tables::Tables()
	{
		for(int i = 0; i < static_cast<int>(cutoffCos.size()); ++i)
			cutoffCos[i] = cosine(cutoff(i - CutoffCosBase), 0.5);
		for(int i = 0; i < static_cast<int>(cutoffExp.size()); ++i)
			cutoffExp[i] = decay(cutoff(i - CutoffExpBase));
		for(int i = 0; i < static_cast<int>(eqCos.size()); ++i)
			eqCos[i] = cosine(eqFrequency(i - EqCosBase), 0.5002);
		for(int i = 0; i < static_cast<int>(eqExp.size()); ++i)
			eqExp[i] = decay(eqFrequency(i - EqExpBase));

		// FLTQ: a Q of 0.501 to 2 sqrt 2 in equal ratios, b = 1 / 2Q
		const double cutoffSteps = 2048 / std::log(4000.0), eqSteps = 2048 / std::log(1000.0);
		for(int i = 0; i < 128; ++i)
		{
			const double q = 0.501 * std::pow(2 * std::sqrt(2.0) / 0.501, i / 127.0);
			offsets(1 / (2 * q), cutoffSteps, resonanceCos[i], resonanceExp[i]);

			const double x = (i - 63.5) / 64;
			const bool flat = i == 63;
			const double pole = flat ? 0.4 : std::exp(quartic(x, i < 63 ? CutPole : BoostPole));
			const double zero = flat ? 0.4 : std::exp(quartic(x, i < 63 ? CutZero : BoostZero));
			offsets(pole, eqSteps, eqPoleCos[i], eqPoleExp[i]);
			offsets(zero, eqSteps, eqZeroCos[i], eqZeroExp[i]);
		}

		// 6 octaves over 255 steps
		for(int i = 0; i < 257; ++i)
		{
			const auto word = [](const double _x) { return static_cast<int32_t>(std::llround(FullScale * _x)); };
			distGain[i] = std::min(word(std::pow(64.0, (i - 255) / 255.0)), FullScale);
			distFilter[i] = std::max(word(std::pow(64.0, -i / 255.0)), 0x80000);
		}
	}

	const Tables& tables()
	{
		static const Tables t;
		return t;
	}
}
