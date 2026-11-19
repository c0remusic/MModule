#include "edDrumsDt2Stages.h"

#include <algorithm>
#include <cmath>

namespace elektron::drums
{
	float dt2Knob(const int _amountWord)
	{
		// The DSP's constant, 1/32512 rounded to a float (0x38010204)
		constexpr float inverse = 1.0f / 32512.0f;
		return static_cast<float>(2 * _amountWord) * inverse;
	}

	double dt2BaseWidthPole(const int _index, const double _rate)
	{
		if(_index >= 508)
			return 0;
		// The equal scale and g's polynomial in w, highest power first: measured on the table (vectors-dt2/STAGES.md)
		constexpr double g[]{3.3128420454245209e-06, -6.5148969059154624e-05, 0.0004696516083057947,
			-0.0014624574590131915, 0.0020902601050789582, -0.0024831698360756045, -0.014174308708846674,
			-0.00023940218740858325, 1.0000368090313294};
		const double w = 2 * 3.14159265358979323846 * 5.00003751 * std::exp(0.01769123042 * _index) / _rate;
		double r = 0;
		for(const double c : g)
			r = r * w + c;
		return std::exp(-w * r);
	}

	int dt2BaseWidthKnob(const int _word)
	{
		// Stored, so that the compiler cannot fold the two products into one and lose the float's rounding
		volatile float knob = dt2Knob(_word);
		return static_cast<int>(knob * 32512.0f);
	}

	namespace
	{
		// The DSP's table at its integers: 31 at 0, then the cubic it fits within 7.9e-6 bits
		double brTable(const int _k)
		{
			if(_k <= 0)
				return 31;
			const double k = _k;
			return 16 - 0.30852417 * k + 0.0023236076 * k * k - 6.0510611e-6 * k * k * k;
		}

		// The value as the DSP reads it from its knob word: p x 127 in float, inside 0-127
		float valueOf(const int _amountWord)
		{
			// Stored first, as dt2BaseWidthKnob: the DSP reads some values one integer low through this rounding
			volatile float knob = dt2Knob(_amountWord);
			return std::clamp(knob * 127.0f, 0.0f, 127.0f);
		}
	}

	double Dt2Br::bits(const double _value)
	{
		const double v = std::clamp(_value, 0.0, 127.0);
		const int i = static_cast<int>(v);
		const double f = v - i;
		return (1 - f) * brTable(i) + f * brTable(std::min(i + 1, 127));
	}

	void Dt2Br::set(const int _lane, const int _amountWord)
	{
		if(_amountWord <= 0)
		{
			m_bits[_lane] = 0;
			return;
		}
		const double b = bits(valueOf(_amountWord));
		m_bits[_lane] = static_cast<int>(b);
		m_fraction[_lane] = static_cast<float>(b - m_bits[_lane]);
	}

	void Dt2Br::process(Block& _buf, const uint16_t _lanes) const
	{
		for(int l = 0; l < _buf.lanes; ++l)
		{
			const int n = m_bits[l];
			if(!(_lanes >> l & 1) || !n)
				continue;
			// q_m(u) = trunc(u (2^m - 1) + 1/2) 2^-m: its top step is 1 - 2^-m, which leaves the DSP's small offset
			const float g = m_fraction[l], scaleA = std::ldexp(1.0f, n) - 1, scaleB = std::ldexp(1.0f, n + 1) - 1;
			const float stepA = std::ldexp(1.0f, -n), stepB = std::ldexp(1.0f, -(n + 1));
			for(int s = 0; s < BlockSize; ++s)
			{
				const float u = std::clamp((_buf.s[s][l] + 1.0f) * 0.5f, 0.0f, 1.0f);
				const float a = std::trunc(u * scaleA + 0.5f) * stepA, b = std::trunc(u * scaleB + 0.5f) * stepB;
				const float mix = a * (1 - g) + b * g;
				_buf.s[s][l] = mix + mix - 1;
			}
		}
	}

	Dt2Srr::Dt2Srr(const double _rate) : m_rate(_rate)
	{
		std::fill(std::begin(m_n), std::end(m_n), -1);
	}

	double Dt2Srr::hold(const double _value)
	{
		const double k = std::floor(std::clamp(_value, 0.0, 127.0));
		return 100 * (k / 127) * (k / 127);
	}

	void Dt2Srr::set(const int _lane, const int _amountWord)
	{
		if(_amountWord <= 0)
		{
			m_n[_lane] = -1;
			return;
		}
		// The mixed hold t + 1 samples at 48 kHz, as long in time at this rate; one sample at least
		const double t = std::max((hold(valueOf(_amountWord)) + 1) * m_rate / 48000 - 1, 0.0);
		// A hair over before the truncation: under /fp:fast the division is a product by its reciprocal, which puts a
		// whole hold (100 at 127) one ulp under its integer, a sample short with a fraction of 1
		m_n[_lane] = static_cast<int>(t + 1e-9);
		m_fraction[_lane] = static_cast<float>(std::max(t - m_n[_lane], 0.0));
	}

	void Dt2Srr::clear(const int _lane)
	{
		for(int h = 0; h < 2; ++h)
		{
			m_held[h][_lane] = 0;
			m_count[h][_lane] = 0;
		}
	}

	void Dt2Srr::process(Block& _buf, const uint16_t _lanes)
	{
		for(int l = 0; l < _buf.lanes; ++l)
		{
			if(!(_lanes >> l & 1))
				continue;
			if(m_hit >> l & 1)
			{
				clear(l);
				continue;
			}
			const int n = m_n[l];
			if(n < 0)
				continue;
			const float g = m_fraction[l];
			const int lengths[2]{n + 1, n + 2};
			for(int s = 0; s < BlockSize; ++s)
			{
				for(int h = 0; h < 2; ++h)
					if(m_count[h][l] == 0)
						m_held[h][l] = _buf.s[s][l];
				_buf.s[s][l] = m_held[0][l] * (1 - g) + m_held[1][l] * g;
				for(int h = 0; h < 2; ++h)
					if(++m_count[h][l] >= lengths[h])
						m_count[h][l] = 0;
			}
		}
		m_hit &= static_cast<uint16_t>(~_lanes);
	}
}
