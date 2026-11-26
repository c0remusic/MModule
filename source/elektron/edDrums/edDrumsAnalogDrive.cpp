#include "edDrumsAnalogDrive.h"

#include <algorithm>
#include <cmath>

#include <emmintrin.h>

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

	// tick's arithmetic in SSE2, 4 lanes at a time, in its order: the same samples as tick lane by lane
	void AnalogDrive::process(Block& _buf, const uint16_t _lanes)
	{
		using V = __m128;
		using I = __m128i;
		// The low 32 bits of each lane's product, which SSE2 lacks (_mm_mullo_epi32 is SSE4.1's)
		const auto mul = [](const I _a, const I _b)
		{
			const I even = _mm_mul_epu32(_a, _b);
			const I odd = _mm_mul_epu32(_mm_srli_epi64(_a, 32), _mm_srli_epi64(_b, 32));
			return _mm_unpacklo_epi32(_mm_shuffle_epi32(even, _MM_SHUFFLE(0, 0, 2, 0)),
				_mm_shuffle_epi32(odd, _MM_SHUFFLE(0, 0, 2, 0)));
		};
		const V sign = _mm_set1_ps(-0.0f), dcPole = _mm_set1_ps(m_dcPole), fall = _mm_set1_ps(m_envelopeFall);
		const V hiss = _mm_set1_ps(HissScale), bias = _mm_set1_ps(BiasF), offset = _mm_set1_ps(Offset);
		const V low = _mm_set1_ps(-3.0f), high = _mm_set1_ps(3.0f), c27 = _mm_set1_ps(27.0f), c9 = _mm_set1_ps(9.0f);
		const I lcgMul = _mm_set1_epi32(1664525), lcgAdd = _mm_set1_epi32(1013904223);
		for(int l = 0; l < _buf.lanes; l += 4)
		{
			const int mask = _lanes >> l & 0xf;
			if(!mask)
				continue;
			Lane* const lane = m_lanes + l;
			const auto load = [&](float Lane::* _field)
			{
				return _mm_set_ps(lane[3].*_field, lane[2].*_field, lane[1].*_field, lane[0].*_field);
			};
			const auto bit = [&](const int _i) { return mask >> _i & 1 ? -1 : 0; };
			const V written = _mm_castsi128_ps(_mm_set_epi32(bit(3), bit(2), bit(1), bit(0)));
			const V pole = load(&Lane::pole), gainTo = load(&Lane::gainTo), step = load(&Lane::step);
			V gain = load(&Lane::gain), hpX = load(&Lane::hpX), hpY = load(&Lane::hpY), dcX = load(&Lane::dcX);
			V dcY = load(&Lane::dcY), envelope = load(&Lane::envelope);
			I ramp = _mm_set_epi32(lane[3].ramp, lane[2].ramp, lane[1].ramp, lane[0].ramp);
			I noise = _mm_set_epi32(static_cast<int>(lane[3].noise), static_cast<int>(lane[2].noise),
				static_cast<int>(lane[1].noise), static_cast<int>(lane[0].noise));
			const bool ramping = _mm_movemask_epi8(_mm_cmpgt_epi32(ramp, _mm_setzero_si128())) != 0;
			for(int s = 0; s < BlockSize; ++s)
			{
				const V in = _mm_loadu_ps(&_buf.s[s][l]);
				hpY = _mm_mul_ps(pole, _mm_sub_ps(_mm_add_ps(hpY, in), hpX));
				hpX = in;
				envelope = _mm_max_ps(_mm_andnot_ps(sign, in), _mm_mul_ps(envelope, fall));
				noise = _mm_add_epi32(mul(noise, lcgMul), lcgAdd);
				const V n = _mm_mul_ps(_mm_mul_ps(_mm_cvtepi32_ps(noise), hiss), envelope);
				if(ramping)
				{
					// As tick: a lane still ramping counts down, and lands on gainTo where its count ends
					const I active = _mm_cmpgt_epi32(ramp, _mm_setzero_si128());
					ramp = _mm_add_epi32(ramp, active);
					const V on = _mm_castsi128_ps(active);
					const V done = _mm_and_ps(on, _mm_castsi128_ps(_mm_cmpeq_epi32(ramp, _mm_setzero_si128())));
					const V next = _mm_or_ps(_mm_and_ps(done, gainTo), _mm_andnot_ps(done, _mm_add_ps(gain, step)));
					gain = _mm_or_ps(_mm_and_ps(on, next), _mm_andnot_ps(on, gain));
				}
				const V x = _mm_min_ps(_mm_max_ps(_mm_add_ps(_mm_mul_ps(gain, _mm_add_ps(hpY, n)), bias), low), high);
				const V x2 = _mm_mul_ps(x, x);
				const V y = _mm_sub_ps(_mm_div_ps(_mm_mul_ps(x, _mm_add_ps(c27, x2)),
					_mm_add_ps(c27, _mm_mul_ps(c9, x2))), offset);
				dcY = _mm_mul_ps(dcPole, _mm_sub_ps(_mm_add_ps(dcY, y), dcX));
				dcX = y;
				_mm_storeu_ps(&_buf.s[s][l], _mm_or_ps(_mm_and_ps(written, dcY), _mm_andnot_ps(written, in)));
			}
			// Back to the lanes written alone
			alignas(16) float f[6][4];
			alignas(16) int32_t r[4];
			alignas(16) uint32_t u[4];
			const V fields[] = {gain, hpX, hpY, dcX, dcY, envelope};
			for(int k = 0; k < 6; ++k)
				_mm_store_ps(f[k], fields[k]);
			_mm_store_si128(reinterpret_cast<I*>(r), ramp);
			_mm_store_si128(reinterpret_cast<I*>(u), noise);
			for(int i = 0; i < 4; ++i)
			{
				if(!(mask >> i & 1))
					continue;
				Lane& one = lane[i];
				one.gain = f[0][i], one.hpX = f[1][i], one.hpY = f[2][i], one.dcX = f[3][i], one.dcY = f[4][i];
				one.envelope = f[5][i], one.ramp = r[i], one.noise = u[i];
			}
		}
	}

	float AnalogDrive::sample(const int _lane, const float _x)
	{
		return tick(m_lanes[_lane], _x);
	}
}
