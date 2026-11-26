#include "edDrumsAnalogDrive.h"

#include <algorithm>
#include <cmath>

namespace elektron::drums
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;
		constexpr float BiasF = static_cast<float>(AnalogDrive::Bias);
		constexpr float HissScale = static_cast<float>(AnalogDrive::Hiss / 2147483648.0);	// of a signed 32-bit word

		// tanh's Padé approximant, exact at 0 and at 3 where its slope reaches 0, then held at 1
		constexpr float clip(const float _x)
		{
			const float x = std::clamp(_x, -3.0f, 3.0f);
			const float x2 = x * x;
			return x * (27 + x2) / (27 + 9 * x2);
		}

		constexpr float Offset = clip(BiasF);		// the clip's output at rest, taken away

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
			m_lanes[l].cutoff = cutoff(0);
			setPole(m_lanes[l]);
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
		for(auto& lane : m_lanes)
			setPole(lane);
	}

	void AnalogDrive::setPole(Lane& _lane) const
	{
		_lane.pole = onePole(_lane.cutoff, m_rate * m_factor);
	}

	void AnalogDrive::set(const int _lane, const int _amountWord)
	{
		auto& lane = m_lanes[_lane];
		const double value = _amountWord / 128.0;
		lane.cutoff = cutoff(value);
		setPole(lane);
		lane.gainTo = static_cast<float>(std::pow(10.0, gainDb(value) / 20));
		// Over the chain's block, which the stage sees as m_factor blocks when the chain raises it
		lane.ramp = BlockSize * m_factor;
		lane.step = (lane.gainTo - lane.gain) / static_cast<float>(lane.ramp);
	}

	void AnalogDrive::clear(const int _lane)
	{
		clear(_lane, 0x9e3779b9u * static_cast<uint32_t>(_lane + 1));
	}

	void AnalogDrive::clear(const int _lane, const uint32_t _seed)
	{
		auto& lane = m_lanes[_lane];
		lane.gain = lane.gainTo;
		lane.ramp = 0;
		lane.hpX = lane.hpY = lane.dcX = lane.dcY = lane.envelope = 0;
		lane.noise = _seed;
	}

	inline float AnalogDrive::tick(Lane& _lane, const float _x) const
	{
		_lane.hpY = _lane.pole * (_lane.hpY + _x - _lane.hpX);
		_lane.hpX = _x;
		_lane.envelope = std::max(std::abs(_x), _lane.envelope * m_envelopeFall);
		_lane.noise = _lane.noise * 1664525u + 1013904223u;
		const float n = static_cast<float>(static_cast<int32_t>(_lane.noise)) * HissScale * _lane.envelope;
		if(_lane.ramp > 0)
			_lane.gain = --_lane.ramp ? _lane.gain + _lane.step : _lane.gainTo;
		const float y = clip(_lane.gain * (_lane.hpY + n) + BiasF) - Offset;
		_lane.dcY = m_dcPole * (_lane.dcY + y - _lane.dcX);
		_lane.dcX = y;
		return _lane.dcY;
	}

	void AnalogDrive::process(Block& _buf, const uint16_t _lanes)
	{
		for(int l = 0; l < _buf.lanes; ++l)
		{
			if(!(_lanes >> l & 1))
				continue;
			// A copy the loop keeps in registers
			Lane lane = m_lanes[l];
			for(int s = 0; s < BlockSize; ++s)
				_buf.s[s][l] = tick(lane, _buf.s[s][l]);
			m_lanes[l] = lane;
		}
	}

	float AnalogDrive::sample(const int _lane, const float _x)
	{
		return tick(m_lanes[_lane], _x);
	}
}
