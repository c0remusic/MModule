#include "edDrumsSendFx.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace elektron::drums
{
	namespace
	{
		constexpr double Rate = 44100, Pi = 3.141592653589793;
		constexpr float Glide = 0.5f;		// what a block leaves of the way to a new setting

		struct Knot
		{
			uint8_t at;
			double value;
		};

		// Equal ratios between two knots
		template<size_t N> double knots(const Knot (&_knots)[N], const uint8_t _value)
		{
			size_t i = 0;
			while(i + 2 < N && _value > _knots[i + 1].at)
				++i;
			const Knot& a = _knots[i];
			const Knot& b = _knots[i + 1];
			const double t = static_cast<double>(_value - a.at) / (b.at - a.at);
			return a.value * std::pow(b.value / a.value, t);
		}

		// A triangle's phase for a block, whole turns in 32 bits
		uint32_t phaseStep(const uint8_t _rate)
		{
			static constexpr Knot Hz[] = {{0, 0.00175}, {4, 0.01}, {8, 0.0197}, {28, 0.0886}, {92, 2.003}, {108, 4.811},
				{120, 13.9}, {124, 28.02}, {127, 50.07}};
			return static_cast<uint32_t>(knots(Hz, _rate) * ModulatedDelay::BlockSize / Rate * 4294967296.0);
		}

		// 0 to 1 and back in a turn
		float triangle(const uint32_t _phase)
		{
			const uint32_t rising = _phase >> 31 ? ~_phase : _phase;
			return static_cast<float>(rising) / 2147483648.0f;
		}
	}

	float sendFxLevel(const uint8_t _mix)
	{
		// sqrt(1/2) in the middle
		constexpr float Middle = 0.7071f;
		if(_mix <= 64)
			return Middle * static_cast<float>(_mix) / 64;
		return 1 - (1 - Middle) * static_cast<float>(127 - _mix) / 63;
	}

	ModulatedDelay::ModulatedDelay(const double _samplesPerStep) : m_samplesPerStep(_samplesPerStep)
	{
		for(auto& line : m_lines)
			line.assign(Size, 0.0f);
	}

	void ModulatedDelay::set(const uint8_t _delay, const uint8_t _depth, const uint8_t _rate, const uint8_t _feedback)
	{
		m_delayTo = static_cast<float>(m_samplesPerStep * _delay + 1);
		m_swingTo = static_cast<float>(m_samplesPerStep * _delay * _depth / 128);
		m_step = phaseStep(_rate);
		m_feedbackTo = (static_cast<float>(_feedback) - 64) / 65.6f;
	}

	void ModulatedDelay::settle()
	{
		m_delay = m_delayTo;
		m_swing = m_swingTo;
		m_feedback = m_feedbackTo;
	}

	void ModulatedDelay::clear()
	{
		for(auto& line : m_lines)
			std::fill(line.begin(), line.end(), 0.0f);
	}

	void ModulatedDelay::process(const float* const _inL, const float* const _inR, float* const _outL,
		float* const _outR)
	{
		// The triangle at each sample: a straight line between the block's ends is one, but at its turns
		m_phase += m_step;
		const float to = triangle(m_phase);
		const float step = (to - m_top) / BlockSize;
		m_feedback = m_feedbackTo + Glide * (m_feedback - m_feedbackTo);
		// Delay and swing glide together, so that the time stays over a sample
		const float delayEnd = m_delayTo + Glide * (m_delay - m_delayTo);
		const float swingEnd = m_swingTo + Glide * (m_swing - m_swingTo);
		const float delayStep = (delayEnd - m_delay) / BlockSize, swingStep = (swingEnd - m_swing) / BlockSize;
		float top = m_top, delayNow = m_delay, swingNow = m_swing;
		const float* const in[2] = {_inL, _inR};
		float* const out[2] = {_outL, _outR};
		for(int k = 0; k < BlockSize; ++k)
		{
			top += step;
			delayNow += delayStep;
			swingNow += swingStep;
			const float tops[2] = {top, 1 - top};
			for(size_t c = 0; c < 2; ++c)
			{
				auto& line = m_lines[c];
				// Never under a sample, so what is read is written. Straight between samples: the highs dip on the way
				const float delay = delayNow - swingNow * tops[c];
				const auto whole = static_cast<uint32_t>(delay);
				const float t = delay - static_cast<float>(whole);
				const float a = line[(m_write - whole) & Mask], b = line[(m_write - whole - 1) & Mask];
				const float y = a + t * (b - a);
				line[m_write] = in[c][k] + m_feedback * y;
				out[c][k] = y;
			}
			m_write = (m_write + 1) & Mask;
		}
		m_top = to;
		m_delay = delayEnd;
		m_swing = swingEnd;
	}

	void Phaser::set(const int _stages, const uint8_t _frequency, const uint8_t _spread, const uint8_t _depth,
		const uint8_t _rate, const uint8_t _feedback)
	{
		static constexpr Knot Q[] = {{0, 2.83}, {16, 1.92}, {32, 1.35}, {64, 0.8}, {96, 0.5}, {127, 0.29}};
		m_stagesTo = std::clamp(_stages, 1, MaxStages);
		m_frequency = _frequency;
		m_depth = static_cast<float>(127 - _frequency) * _depth / 127;
		m_dampingTo = static_cast<float>(1 / knots(Q, _spread));
		m_step = phaseStep(_rate);
		m_feedbackTo = 0.9375f * (static_cast<float>(_feedback) - 64) / 64;
	}

	void Phaser::settle()
	{
		m_sweep = m_frequency;
		m_damping = m_dampingTo;
		m_feedback = m_feedbackTo;
		m_stages = m_stagesTo;
		m_entering = false;
	}

	void Phaser::clear()
	{
		for(auto& stage : m_state)
			stage[0] = stage[1] = Stage{};
		m_last[0] = m_last[1] = 0;
	}

	void Phaser::process(const float* const _inL, const float* const _inR, float* const _outL, float* const _outR)
	{
		// The triangle runs on the byte's scale, nearly equal ratios of Hz; new coefficients each block
		m_phase += m_step;
		m_sweep += (1 - Glide) * (m_frequency + m_depth * triangle(m_phase) - m_sweep);
		const double v = m_sweep;
		const double hz = std::pow(2.0, 4.916 + v * (0.07693 - 8.98e-6 * v));
		m_damping = m_dampingTo + Glide * (m_damping - m_dampingTo);
		m_feedback = m_feedbackTo + Glide * (m_feedback - m_feedbackTo);
		float a1[2], a2[2], a3[2];
		for(int c = 0; c < 2; ++c)
		{
			const auto g = static_cast<float>(std::tan(Pi * std::min(c ? 1.1108 * hz : hz, 0.47 * Rate) / Rate));
			a1[c] = 1 / (1 + g * (g + m_damping));
			a2[c] = g * a1[c];
			a3[c] = g * a2[c];
		}
		// Both channels a stage at a time: twice as fast as one channel's whole chain after the other
		for(int k = 0; k < BlockSize; ++k)
		{
			float x[2] = {_inL[k] + m_feedback * m_last[0], _inR[k] + m_feedback * m_last[1]};
			for(int s = 0; s < m_stages; ++s)
				for(int c = 0; c < 2; ++c)
				{
					Stage& stage = m_state[s][c];
					const float v3 = x[c] - stage.low;
					const float band = a1[c] * stage.band + a2[c] * v3;
					const float low = stage.low + a2[c] * stage.band + a3[c] * v3;
					stage.band = 2 * band - stage.band;
					stage.low = 2 * low - stage.low;
					x[c] -= 2 * m_damping * band;
				}
			m_last[0] = x[0];
			m_last[1] = x[1];
			_outL[k] = x[0];
			_outR[k] = x[1];
		}

		const bool changing = m_stagesTo != m_stages;
		if(!changing && !m_entering)
			return;
		for(float* const channel : {_outL, _outR})
			for(int k = 0; k < BlockSize; ++k)
				channel[k] *= static_cast<float>(changing ? BlockSize - 1 - k : k) / BlockSize;
		m_entering = changing;
		if(changing)
		{
			clear();
			m_stages = m_stagesTo;
		}
	}
}
