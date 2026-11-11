#include "edDrumsMaster.h"

#include <algorithm>
#include <cmath>

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

	double masterDriveGain(const uint8_t _value)
	{
		const double drive = fraction(_value);
		return 0.8465 * drive * drive + 0.1534;
	}

	Master::Master(const double _rate) : m_rate(_rate), m_mixPole(std::pow(0.995, 48000 / _rate))
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
		m_satMix.next(std::min(8 * drive, 1.0));
		const double volume = fraction(b[MasterVolume]);
		m_volume.next(volume * volume);
	}

	void Master::sample(float& _left, float& _right, const int _n)
	{
		const double x[2] = {_left / Lift, _right / Lift};

		// The detector: the filtered peak of both channels
		const double b0 = m_b0.at(_n), b1 = m_b1.at(_n), a1 = m_a1.at(_n);
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
		const double gain = 16 * std::exp(DbToLog * (m_attackDb + m_makeup.at(_n)));

		const double satGain = m_satGain.at(_n), satMix = m_satMix.at(_n), volume = m_volume.at(_n);
		float* const out[2] = {&_left, &_right};
		for(int c = 0; c < 2; ++c)
		{
			const double wet = std::clamp(gain * x[c], -1.0, 1.0);
			const double y = std::clamp(Lift * (m_wet * wet + m_dry * x[c]), -1.0, 1.0);
			// u (2 - |u|) three times: 1 - (1 - |u|)^8, signed
			double u = std::clamp(satGain * y, -1.0, 1.0);
			for(int pass = 0; pass < 3; ++pass)
				u *= 2 - std::abs(u);
			*out[c] = static_cast<float>(volume * (y + satMix * (u - y)));
		}
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
