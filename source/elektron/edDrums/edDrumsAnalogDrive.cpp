#include "edDrumsAnalogDrive.h"

#include <algorithm>
#include <cmath>

namespace elektron::drums
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;

		// tanh's Padé approximant, exact at 0 and at 3 where its slope reaches 0, then held at 1
		float clip(const float _x)
		{
			const float x = std::clamp(_x, -3.0f, 3.0f);
			const float x2 = x * x;
			return x * (27 + x2) / (27 + 9 * x2);
		}

		float onePole(const double _hz, const double _rate)
		{
			return static_cast<float>(std::exp(-2 * Pi * _hz / _rate));
		}
	}

	AnalogDrive::AnalogDrive(const double _rate) : m_rate(_rate)
	{
		setFactor(1);
		for(int l = 0; l < Lanes; ++l)
		{
			m_cutoff[l] = cutoff(0);
			setPoles(l);
			m_gain[l] = m_gainTo[l] = 1;
			clear(l);
		}
	}

	double AnalogDrive::gainDb(const double _value)
	{
		return 30 * std::clamp(_value, 0.0, 127.0) / 127;
	}

	double AnalogDrive::cutoff(const double _value)
	{
		const double v = std::clamp(_value, 0.0, 127.0) / 127;
		return 12 + 290 * v * v * v * v;
	}

	void AnalogDrive::setFactor(const int _factor)
	{
		if(_factor == m_factor && m_dcPole != 0)
			return;
		m_factor = _factor;
		const double rate = m_rate * m_factor;
		m_dcPole = onePole(DcCutoff, rate);
		m_envelopeFall = static_cast<float>(std::pow(1e-3, 1 / (0.1 * rate)));
		for(int l = 0; l < Lanes; ++l)
			setPoles(l);
	}

	void AnalogDrive::setPoles(const int _lane)
	{
		m_pole[_lane] = onePole(m_cutoff[_lane], m_rate * m_factor);
	}

	void AnalogDrive::set(const int _lane, const int _amountWord)
	{
		const double value = _amountWord / 128.0;
		m_cutoff[_lane] = cutoff(value);
		setPoles(_lane);
		m_gainTo[_lane] = static_cast<float>(std::pow(10.0, gainDb(value) / 20));
		// Over the chain's block, which the stage sees as m_factor blocks when the chain raises it
		m_ramp[_lane] = BlockSize * m_factor;
		m_step[_lane] = (m_gainTo[_lane] - m_gain[_lane]) / static_cast<float>(m_ramp[_lane]);
	}

	void AnalogDrive::clear(const int _lane)
	{
		m_gain[_lane] = m_gainTo[_lane];
		m_ramp[_lane] = 0;
		m_hpX[_lane] = m_hpY[_lane] = m_dcX[_lane] = m_dcY[_lane] = m_envelope[_lane] = 0;
		m_noise[_lane] = 0x9e3779b9u * static_cast<uint32_t>(_lane + 1);
	}

	void AnalogDrive::process(Block& _buf, const uint16_t _lanes)
	{
		const float bias = static_cast<float>(Bias), offset = clip(bias);
		const float hiss = static_cast<float>(Hiss / 2147483648.0);
		for(int l = 0; l < _buf.lanes; ++l)
		{
			if(!(_lanes >> l & 1))
				continue;
			const float pole = m_pole[l], dcPole = m_dcPole, fall = m_envelopeFall;
			const float step = m_step[l];
			int ramp = m_ramp[l];
			float gain = m_gain[l], hpX = m_hpX[l], hpY = m_hpY[l], dcX = m_dcX[l], dcY = m_dcY[l];
			float envelope = m_envelope[l];
			uint32_t noise = m_noise[l];
			for(int s = 0; s < BlockSize; ++s)
			{
				const float x = _buf.s[s][l];
				hpY = pole * (hpY + x - hpX);
				hpX = x;
				envelope = std::max(std::abs(x), envelope * fall);
				noise = noise * 1664525u + 1013904223u;
				const float n = static_cast<float>(static_cast<int32_t>(noise)) * hiss * envelope;
				if(ramp > 0)
					gain = --ramp ? gain + step : m_gainTo[l];
				const float y = clip(gain * (hpY + n) + bias) - offset;
				dcY = dcPole * (dcY + y - dcX);
				dcX = y;
				_buf.s[s][l] = dcY;
			}
			m_gain[l] = gain;
			m_ramp[l] = ramp;
			m_hpX[l] = hpX;
			m_hpY[l] = hpY;
			m_dcX[l] = dcX;
			m_dcY[l] = dcY;
			m_envelope[l] = envelope;
			m_noise[l] = noise;
		}
	}
}
