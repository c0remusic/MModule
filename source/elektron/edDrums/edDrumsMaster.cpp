#include "edDrumsMaster.h"

#include <algorithm>
#include <cmath>

#include <emmintrin.h>

namespace elektron::drums
{
	namespace
	{
		constexpr double Pi = 3.141592653589793;
		constexpr double Lift = 3.16227766;					// +10 dB
		constexpr double Floor = 1.1920928955078125e-7;		// 2^-23, the detector's
		constexpr double DbToLog = 0.11512925464970229;		// ln 10 / 20
		constexpr double MakeupRange = 24.0823996531;		// 20 log10(16) dB

		double fraction(const uint8_t _value) { return _value / 127.0; }

		// A one-pole's coefficient per sample for a time constant in ms
		double pole(const double _ms, const double _rate) { return std::exp(-1000 / (_rate * _ms)); }

		// The mix's gain of one side, equal steps of dB between knots, nothing at 0
		double mixGain(const int _k)
		{
			static constexpr int K[] = {1, 7, 8, 64, 126, 127};
			static constexpr double Db[] = {-92.25, -45.75, -37.5, -6.0, -0.1875, 0.0};
			if(_k <= 0)
				return 0;
			size_t i = 0;
			while(i + 2 < std::size(K) && _k > K[i + 1])
				++i;
			const double db = Db[i] + (Db[i + 1] - Db[i]) * (_k - K[i]) / (K[i + 1] - K[i]);
			return std::pow(10.0, db / 20);
		}
	}

	double masterThresholdDb(const uint8_t _value) { return 60 * fraction(_value) - 60; }

	double masterRatio(const uint8_t _value)
	{
		static constexpr double Ratios[] = {1.5, 2, 3, 4, 6, 8, 16, 20};
		return Ratios[std::min<int>(_value, 127) * 7 / 127];
	}

	double masterAttackMs(const uint8_t _value)
	{
		const double attack = fraction(_value);
		return 0.1 + 99.9 * attack * attack;
	}

	double masterReleaseMs(const uint8_t _value) { return 10 + 1990 * std::pow(fraction(_value), 2.5); }

	double masterMakeupDb(const uint8_t _value) { return -MakeupRange * (1 - fraction(_value)); }

	double masterFilterHz(const uint8_t _value) { return 20 * std::pow(1000.0, (_value % 64) / 63.0); }

	namespace
	{
		// DRIVE's gain into the curve and its wet share, by its fraction of 127
		double driveGain(const double _drive) { return 0.8465 * _drive * _drive + 0.1534; }
		double driveMix(const double _drive) { return std::min(8 * _drive, 1.0); }

		__m128d clampPair(const __m128d _v) { return _mm_min_pd(_mm_max_pd(_v, _mm_set1_pd(-1)), _mm_set1_pd(1)); }

		// saturate on two doubles at once, its operations in their order: _y x _gain clipped to +-1, then three passes
		__m128d saturatePair(const __m128d _y, const __m128d _gain)
		{
			const __m128d two = _mm_set1_pd(2), sign = _mm_set1_pd(-0.0);
			__m128d u = clampPair(_mm_mul_pd(_gain, _y));
			for(int pass = 0; pass < 3; ++pass)
				u = _mm_mul_pd(u, _mm_sub_pd(two, _mm_andnot_pd(sign, u)));
			return u;
		}
	}

	double masterDriveGain(const uint8_t _value) { return driveGain(fraction(_value)); }

	void Overdrive::set(const int _lane, const int _amountWord)
	{
		const double drive = _amountWord / (127.0 * 128);
		set(_lane, driveGain(drive), driveMix(drive));
	}

	void Overdrive::set(const int _lane, const double _gain, const double _mix)
	{
		m_gain[_lane] = _gain;
		m_mix[_lane] = _mix;
	}

	// The gain's law is b + (1 - b) t, t the OS's target of the word squared, 0.969 at 127; the OS's two words sum to 1
	// (the Overdrive's 0.8465 and 0.1534 are a fit of them). The SNR edDrumsCyclesDistTest measures against the OS
	// hangs on b: 137 dB with this one, 63 with 0.1536
	double CyclesDist::gain(const double _value)
	{
		constexpr double b = 0.153487721;
		const double v = std::max(_value - 64, 0.0) / 63;
		return b + (1 - b) * (0.968994140625 * v * v);
	}

	double CyclesDist::mix(const double _value) { return std::clamp((_value - 60) / 8, 0.0, 1.0); }

	void CyclesDist::set(const int _lane, const int _amountWord)
	{
		const double value = _amountWord / 128.0;
		m_drive.set(_lane, gain(value), mix(value));
	}

	// saturate's arithmetic in SSE2 doubles, two lanes at a time, or in AVX four (edDrumsMasterAvx2.cpp) where the caller
	// found the CPU has it: the same operations, so the same samples
	void Overdrive::process(Block& _buf, const uint16_t _lanes) const
	{
		using D = __m128d;
		for(int l = 0; l < Lanes; l += 4)
		{
			// The lanes written: those asked for whose mix lets the drive through
			const auto on = [&](const int _i) { return _lanes >> (l + _i) & 1 && m_mix[l + _i] != 0 ? -1 : 0; };
			const __m128 written = _mm_castsi128_ps(_mm_set_epi32(on(3), on(2), on(1), on(0)));
			if(!_mm_movemask_ps(written))
				continue;
			if(m_avx2)
			{
				processAvx2(_buf, l, written);
				continue;
			}
			const D gain[2] = {_mm_loadu_pd(m_gain + l), _mm_loadu_pd(m_gain + l + 2)};
			const D mix[2] = {_mm_loadu_pd(m_mix + l), _mm_loadu_pd(m_mix + l + 2)};
			for(int n = 0; n < BlockSize; ++n)
			{
				const __m128 in = _mm_loadu_ps(&_buf.s[n][l]);
				D out[2];
				for(int h = 0; h < 2; ++h)
				{
					const D x = _mm_cvtps_pd(h ? _mm_movehl_ps(in, in) : in);
					const D u = saturatePair(x, gain[h]);
					out[h] = _mm_add_pd(x, _mm_mul_pd(mix[h], _mm_sub_pd(u, x)));
				}
				const __m128 wet = _mm_movelh_ps(_mm_cvtpd_ps(out[0]), _mm_cvtpd_ps(out[1]));
				_mm_storeu_ps(&_buf.s[n][l], _mm_or_ps(_mm_and_ps(written, wet), _mm_andnot_ps(written, in)));
			}
		}
	}

	Master::Master(const double _rate, const int _block)
		: m_rate(_rate), m_mixPole(std::pow(0.995, 48000 / _rate)), m_block(_block)
	{
		m_bytes = {127, 0, 0, 64, 0, 127, 64, 0, 127};
		settle();
	}

	void Master::settle()
	{
		// Twice, so that each ramp starts where it ends
		m_changed = true;
		startBlock();
		startBlock();
		m_wet = m_wetTo;
		m_dry = m_dryTo;
		m_x1[0] = m_x1[1] = m_y1[0] = m_y1[1] = 0;
		m_releaseDb = m_attackDb = 0;
		m_reduction.store(0, std::memory_order_relaxed);
	}

	void Master::startBlock()
	{
		const auto& b = m_bytes;
		m_reduction.store(static_cast<float>(m_attackDb), std::memory_order_relaxed);
		if(!m_changed)
		{
			// Each ramp holds where it ended
			for(Ramp* const r : {&m_makeup, &m_b0, &m_b1, &m_a1, &m_satGain, &m_satMix, &m_volume})
				r->from = r->to;
			return;
		}
		m_changed = false;
		m_threshold = masterThresholdDb(b[MasterThreshold]);
		m_thresholdPeak = std::exp(DbToLog * m_threshold);
		m_ratio = 1 / masterRatio(b[MasterRatio]);
		m_attack = pole(masterAttackMs(b[MasterAttack]), m_rate);
		m_release = pole(masterReleaseMs(b[MasterRelease]), m_rate);
		m_makeup.next(masterMakeupDb(b[MasterMakeup]));
		const int mix = b[MasterMix];
		m_wetTo = mixGain(mix);
		m_dryTo = mixGain(127 - mix);

		// Bilinear one-pole, not prewarped
		const int filter = b[MasterFilter];
		const double w = Pi * 20 * std::pow(1000.0, (filter % 64) / 63.0) / m_rate;		// masterFilterHz's
		if(filter < 64)
		{
			const double c = w / (1 + w);
			m_b0.next(c);
			m_b1.next(c);
			m_a1.next(2 * c - 1);
		}
		else
		{
			const double c = (w - 1) / (w + 1);
			m_b0.next((1 - c) / 2);
			m_b1.next(-(1 - c) / 2);
			m_a1.next(c);
		}

		const double drive = fraction(b[MasterDrive]);
		m_satGain.next(masterDriveGain(b[MasterDrive]));
		m_satMix.next(driveMix(drive));
		const double volume = fraction(b[MasterVolume]);
		m_volume.next(volume * volume);
	}

	void Master::sample(float& _left, float& _right, const int _n)
	{
		const double x[2] = {_left / Lift, _right / Lift};

		// The detector: the filtered peak of both channels
		const double b0 = m_b0.at(_n, m_block), b1 = m_b1.at(_n, m_block), a1 = m_a1.at(_n, m_block);
		double peak = Floor;
		for(int c = 0; c < 2; ++c)
		{
			const double y = b0 * x[c] + b1 * m_x1[c] - a1 * m_y1[c];
			m_x1[c] = x[c];
			m_y1[c] = y;
			peak = std::max(peak, std::abs(y));
		}
		// The logarithm only over the threshold, where it counts
		const double reduction = peak > m_thresholdPeak ? (20 * std::log10(peak) - m_threshold) * (m_ratio - 1) : 0;
		// More reduction at once, less as a one-pole; then the attack's one-pole
		m_releaseDb = std::min(reduction, (1 - m_release) * reduction + m_release * m_releaseDb);
		m_attackDb = m_attack * m_attackDb + (1 - m_attack) * m_releaseDb;
		const double gain = 16 * std::exp(DbToLog * (m_attackDb + m_makeup.at(_n, m_block)));

		const double satGain = m_satGain.at(_n, m_block), satMix = m_satMix.at(_n, m_block), volume = m_volume.at(_n, m_block);
		// Both channels at once, each through saturate's and the clamps' operations in their order
		using D = __m128d;
		const D in = _mm_set_pd(x[1], x[0]);
		const D wet = clampPair(_mm_mul_pd(_mm_set1_pd(gain), in));
		const D y = clampPair(_mm_mul_pd(_mm_set1_pd(Lift), _mm_add_pd(_mm_mul_pd(_mm_set1_pd(m_wet), wet),
			_mm_mul_pd(_mm_set1_pd(m_dry), in))));
		const D u = saturatePair(y, _mm_set1_pd(satGain));
		const D out = _mm_mul_pd(_mm_set1_pd(volume), _mm_add_pd(y, _mm_mul_pd(_mm_set1_pd(satMix), _mm_sub_pd(u, y))));
		const __m128 out32 = _mm_cvtpd_ps(out);
		_left = _mm_cvtss_f32(out32);
		_right = _mm_cvtss_f32(_mm_shuffle_ps(out32, out32, 1));
		m_wet = m_wetTo + m_mixPole * (m_wet - m_wetTo);
		m_dry = m_dryTo + m_mixPole * (m_dry - m_dryTo);
	}

	void Master::block()
	{
		startBlock();
		m_n = 0;
	}

	void Master::process(float* const _left, float* const _right, const int _count)
	{
		for(int i = 0; i < _count; ++i)
			sample(_left[i], _right[i], m_n++);
	}
}
