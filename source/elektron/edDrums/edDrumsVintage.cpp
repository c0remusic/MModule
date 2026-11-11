#include "edDrumsVintage.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace elektron::drums::vintage
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;

		// The segment law's full scale in its units: the top code, segment 7 step 2^M - 1
		int fullScale(const int _m)
		{
			return (((2 << _m) - 1) << 7) - (1 << _m);
		}

		int clampBits(const int _bits)
		{
			return std::clamp(_bits, MinBits, MaxBits);
		}
	}

	int compandCode(const float _magnitude, const int _bits)
	{
		const int m = clampBits(_bits) - 4;
		const double steps = 1 << m;
		// b = a + 2^M is (2^M + q) 2^s at a code's value: its segment is its top bit's, its step the nearest
		const double b = std::clamp(static_cast<double>(_magnitude), 0.0, 1.0) * fullScale(m) + steps;
		int exponent;
		std::frexp(b, &exponent);		// b = f 2^exponent, f in [1/2, 1)
		int s = std::clamp(exponent - 1 - m, 0, 7);
		auto q = static_cast<int>(std::lround(std::ldexp(b, -s))) - (1 << m);
		if(q >= 1 << m)
		{
			// Nearer the next segment's first value, or past the top
			if(s < 7)
			{
				++s;
				q = 0;
			}
			else
			{
				q = (1 << m) - 1;
			}
		}
		return s << m | std::max(q, 0);
	}

	float compandValue(const int _code, const int _bits)
	{
		const int m = clampBits(_bits) - 4;
		const int s = _code >> m, q = _code & ((1 << m) - 1);
		const int units = ((1 << m) + q) * (1 << s) - (1 << m);
		return static_cast<float>(units) / static_cast<float>(fullScale(m));
	}

	float dac(const float _x, const int _bits, const bool _companding)
	{
		const int bits = clampBits(_bits);
		if(_companding)
		{
			const float v = compandValue(compandCode(std::abs(_x), bits), bits);
			return _x < 0 ? -v : v;
		}
		const float scale = static_cast<float>(1 << (bits - 1));
		const float code = std::clamp(std::round(_x * scale), -scale, scale - 1);
		return code / scale;
	}

	void LowPass::set(const double _hz, const double _rate)
	{
		const double g = std::tan(Pi * std::min(_hz, 0.49 * _rate) / _rate), k = std::sqrt(2.0);
		const double a1 = 1 / (1 + g * (g + k));
		m_a1 = static_cast<float>(a1);
		m_a2 = static_cast<float>(g * a1);
		m_a3 = static_cast<float>(g * g * a1);
	}

	double filterHz(const double _value)
	{
		return 100 * std::pow(200.0, std::clamp(_value, 0.0, 127.0) / 127);
	}

	double semitones(const double _ptch)
	{
		return (_ptch - 64) * 3 / 8;
	}

	double holdSeconds(const double _hold)
	{
		return 2 * std::clamp(_hold, 0.0, 127.0) / 127;
	}

	double decaySeconds(const double _dec)
	{
		if(_dec >= 126.5)
			return std::numeric_limits<double>::infinity();
		return 0.02 * std::pow(500.0, std::max(_dec, 0.0) / 126);
	}

	DriveSetting driveSetting(const double _amount, const double _rate)
	{
		const double a = std::clamp(_amount, 0.0, 127.0);
		DriveSetting s;
		s.bits = MaxBits - static_cast<int>(std::lround(6 * a / 127));
		s.companding = a > 0;
		s.clockHz = _rate * std::pow(4000 / _rate, a / 127);
		s.cutoffHz = a > 0 ? std::min(0.45 * s.clockHz, 20000.0) : 0;
		return s;
	}

	// ---- the DAC drive ----

	Dac::Dac(const double _rate) : m_rate(_rate)
	{
		for(int l = 0; l < Lanes; ++l)
		{
			set(l, 0);
			clear(l);
		}
	}

	void Dac::set(const int _lane, const int _amountWord)
	{
		const auto s = driveSetting(_amountWord / 128.0, m_rate);
		m_bits[_lane] = s.bits;
		m_companding[_lane] = s.companding;
		m_step[_lane] = static_cast<float>(std::min(1.0, s.clockHz / m_rate));
		m_filtered[_lane] = s.cutoffHz > 0;
		if(m_filtered[_lane])
			m_filter[_lane].set(s.cutoffHz, m_rate);
	}

	void Dac::clear(const int _lane)
	{
		m_phase[_lane] = 1;		// the next sample is taken
		m_held[_lane] = 0;
		m_filter[_lane].clear();
	}

	void Dac::process(Block& _buf, const uint16_t _lanes)
	{
		for(int l = 0; l < Lanes; ++l)
		{
			if(!(_lanes >> l & 1))
				continue;
			float phase = m_phase[l], held = m_held[l];
			const float step = m_step[l];
			auto& filter = m_filter[l];
			for(int n = 0; n < BlockSize; ++n)
			{
				phase += step;
				if(phase >= 1)
				{
					phase -= 1;
					held = dac(_buf.s[n][l], m_bits[l], m_companding[l]);
				}
				_buf.s[n][l] = m_filtered[l] ? filter.process(held) : held;
			}
			m_phase[l] = phase;
			m_held[l] = held;
		}
	}

	// ---- the VINTAGE grain ----

	void Player::trig(const int16_t* _pcm, const uint32_t _begin, const uint32_t _end, const double _holdSamples)
	{
		m_pcm = _pcm;
		m_index = _begin;
		m_end = _end;
		m_position = 0;
		m_hold = _holdSamples;
		m_level = 1;
		m_playing = _pcm && _begin < _end;
		m_held = m_playing ? dac(_pcm[_begin] / 32768.0f, m_bits, m_companding) : 0;
		m_filter.clear();
	}

	void Player::setFilter(const double _hz, const double _rate)
	{
		if(_hz == m_hz)
			return;
		m_hz = _hz;
		m_filtered = _hz > 0;
		if(m_filtered)
			m_filter.set(_hz, _rate);
	}

	void Player::setDecay(const double _seconds, const double _rate)
	{
		m_fall = std::isfinite(_seconds) ? static_cast<float>(std::pow(10.0, -3 / (_seconds * _rate))) : 1.0f;
	}

	void Player::render(float* _out, const int _count)
	{
		for(int n = 0; n < _count; ++n)
		{
			const float x = m_playing ? m_held : 0.0f;
			_out[n] += (m_filtered ? m_filter.process(x) : x) * m_level;
			if(!m_playing)
				continue;
			// The clock's ticks before the next host sample: the last one's sample is held
			m_position += m_step;
			if(m_position >= 1)
			{
				const auto ticks = static_cast<uint32_t>(m_position);
				m_position -= ticks;
				m_index += ticks;
				if(m_index >= m_end)
					m_playing = false;
				else
					m_held = dac(m_pcm[m_index] / 32768.0f, m_bits, m_companding);
			}
			if(m_hold > 0)
				m_hold -= 1;
			else if((m_level *= m_fall) < Silence)
				m_playing = false;
		}
	}
}
