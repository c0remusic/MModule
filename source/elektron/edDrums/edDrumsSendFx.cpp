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
		constexpr float DcPole = 0.99919f;	// the DP/4's DC blocker, 5.7 Hz
		constexpr float Smooth = 0.99518f;	// its coefficient's one-pole a sample, 4.7 ms
		constexpr float Edge = 0.9995f;		// short of 1, where a stage would hold DC or Nyquist for ever

		// The DP/4's -99 to 99 from 0-127, 0 at 64
		double signedDp4(const uint8_t _value)
		{
			const int v = _value - 64;
			return v * 99.0 / (v < 0 ? 64 : 63);
		}

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
			return static_cast<uint32_t>(sendFxRateHz(_rate) * ModulatedDelay::BlockSize / Rate * 4294967296.0);
		}

		// 0 to 1 and back in a turn
		float triangle(const uint32_t _phase)
		{
			const uint32_t rising = _phase >> 31 ? ~_phase : _phase;
			return static_cast<float>(rising) / 2147483648.0f;
		}
	}

	double sendFxRateHz(const uint8_t _rate)
	{
		static constexpr Knot Hz[] = {{0, 0.00175}, {4, 0.01}, {8, 0.0197}, {28, 0.0886}, {92, 2.003}, {108, 4.811},
			{120, 13.9}, {124, 28.02}, {127, 50.07}};
		return knots(Hz, _rate);
	}

	double phaserQ(const uint8_t _spread)
	{
		static constexpr Knot Q[] = {{0, 2.83}, {16, 1.92}, {32, 1.35}, {64, 0.8}, {96, 0.5}, {127, 0.29}};
		return knots(Q, _spread);
	}

	double phaserHz(const double _frequency)
	{
		const double v = _frequency;
		return std::pow(2.0, 4.916 + v * (0.07693 - 8.98e-6 * v));
	}

	double dp4RateHz(const uint8_t _rate)
	{
		const double rate = _rate * 99.0 / 127;
		return rate > 0 ? std::exp(-5.684159 + 0.040309 * rate) * std::pow(rate, 0.913459) : 0;
	}

	double dp4DelayMs(const uint8_t _time)
	{
		return 1.6 * std::lround(_time * 1000.0 / 127);
	}

	double dp4Signed(const uint8_t _value)
	{
		return signedDp4(_value);
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

	void ModulatedDelay::set(const uint8_t _delay, const uint8_t _depth, const uint8_t _rate, const uint8_t _feedback,
		const Shape _shape)
	{
		m_shape = _shape;
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

	float ModulatedDelay::lfo()
	{
		const uint32_t before = m_phase;
		m_phase += m_step;
		const float turn = static_cast<float>(m_phase >> 8) / 16777216.0f;
		if(m_phase < before)
		{
			m_held = m_drawn;
			m_random = m_random * 1664525u + 1013904223u;
			m_drawn = static_cast<float>(m_random >> 8) / 16777216.0f;
		}
		switch(m_shape)
		{
		case Shape::Sine:
			return 0.5f - 0.5f * std::cos(turn * 6.2831853f);
		case Shape::Triangle:
			return triangle(m_phase);
		case Shape::Saw:
			return 1 - turn;
		case Shape::Square:
			return turn < 0.5f ? 1.0f : 0.0f;
		case Shape::SampleHold:
			return m_drawn;
		case Shape::SampleGlide:
			return m_held + (m_drawn - m_held) * turn;
		}
		return 0;
	}

	void ModulatedDelay::process(const float* const _inL, const float* const _inR, float* const _outL,
		float* const _outR)
	{
		// The LFO at each sample: a straight line between the block's ends is one, but at its turns. A jump, of the
		// square, the saw or the random shapes, spreads over 8 blocks, 5.8 ms: in 32 samples the time would bend the
		// pitch into a click
		constexpr float MostStep = 0.125f;
		const float to = std::clamp(lfo(), m_top - MostStep, m_top + MostStep);
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
		m_stagesTo = std::clamp(_stages, 1, MaxStages);
		m_frequency = _frequency;
		m_depth = static_cast<float>(127 - _frequency) * _depth / 127;
		m_dampingTo = static_cast<float>(1 / phaserQ(_spread));
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
		const double hz = phaserHz(m_sweep);
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

	Dp4Phaser::Dp4Phaser() : m_line(LineSize, 0.0f)
	{
		set(Settings{});
		settle();
	}

	void Dp4Phaser::set(const Settings& _settings)
	{
		const auto& s = _settings;
		const double center = signedDp4(s.center);
		m_center = static_cast<float>(center >= 0 ? center / 99 : center / 99.79);
		m_width = static_cast<float>(s.width * 99.0 / 127 / 99.8);
		const double hz = dp4RateHz(s.rate);
		m_step = static_cast<uint32_t>(hz * BlockSize / Rate * 4294967296.0);
		m_feedback = static_cast<float>(0.992 * signedDp4(s.feedback) / 99);
		m_notch = static_cast<float>(signedDp4(s.notch) / 99);
		m_outOfPhase = s.outOfPhase;
		m_delayFeedback = static_cast<float>(0.992 * signedDp4(s.delayFeedback) / 99);
		// 0.9938 of the time shown, in samples; the right tap a sample longer, as Phaser-DDL renders it
		const uint8_t times[2] = {s.delayLeft, s.delayRight};
		for(int c = 0; c < 2; ++c)
		{
			const double ms = dp4DelayMs(times[c]);
			m_taps[c] = static_cast<uint32_t>(std::lround(43.8245 * ms + 1.8)) + static_cast<uint32_t>(c);
		}
	}

	void Dp4Phaser::settle()
	{
		const float swing = m_width * std::cos(static_cast<float>(m_phase >> 8) * 3.7450702829239286e-7f);
		m_targetTo[0] = std::clamp(m_center - (m_outOfPhase ? swing : -swing), -Edge, Edge);
		m_targetTo[1] = std::clamp(m_center + swing, -Edge, Edge);
		for(int c = 0; c < 2; ++c)
			m_coefficient[c] = m_target[c] = m_targetTo[c];
		m_tapsFrom[0] = m_taps[0];
		m_tapsFrom[1] = m_taps[1];
	}

	void Dp4Phaser::clear()
	{
		for(auto& channel : m_state)
			std::fill(std::begin(channel), std::end(channel), 0.0f);
		m_last[0] = m_last[1] = m_dcIn[0] = m_dcIn[1] = m_dcOut[0] = m_dcOut[1] = 0;
		m_lowPass[0] = m_lowPass[1] = 0;
		// What the taps can read, not the whole line: a mode change clears this on the audio thread
		const uint32_t reach = std::max({m_taps[0], m_taps[1], m_tapsFrom[0], m_tapsFrom[1]}) + 2;
		for(uint32_t j = 1; j <= reach; ++j)
			m_line[(m_write - j) & LineMask] = 0;
	}

	void Dp4Phaser::process(const float* const _inL, const float* const _inR, float* const _outL, float* const _outR)
	{
		// The sine moves the coefficients, L on the other side of it or not. Its value at the block's end, a line
		// between
		m_phase += m_step;
		const float swing = m_width * std::cos(static_cast<float>(m_phase >> 8) * 3.7450702829239286e-7f);
		const float left = m_outOfPhase ? -swing : swing;
		const float to[2] = {std::clamp(m_center + left, -Edge, Edge), std::clamp(m_center + swing, -Edge, Edge)};
		float step[2];
		for(int c = 0; c < 2; ++c)
		{
			m_target[c] = m_targetTo[c];
			m_targetTo[c] = to[c];
			step[c] = (to[c] - m_target[c]) / BlockSize;
		}
		const float* const in[2] = {_inL, _inR};
		float* const out[2] = {_outL, _outR};
		// A new delay time crossfades over the block, from the old tap to the new
		const bool moved = m_tapsFrom[0] != m_taps[0] || m_tapsFrom[1] != m_taps[1];
		for(int k = 0; k < BlockSize; ++k)
		{
			float v[2], x[2];
			const float t = moved ? static_cast<float>(k + 1) / BlockSize : 1.0f;
			for(int c = 0; c < 2; ++c)
			{
				const float tap = m_line[(m_write - m_taps[c]) & LineMask];
				const float old = m_line[(m_write - m_tapsFrom[c]) & LineMask];
				m_lowPass[c] = 0.5f * m_lowPass[c] + 0.5f * (old + t * (tap - old));
			}
			for(int c = 0; c < 2; ++c)
			{
				// A DC blocker at 5.7 Hz, then the other channel's chain fed back across, R's with its sign turned
				const float sample = in[c][k];
				m_dcOut[c] = sample - m_dcIn[c] + DcPole * m_dcOut[c];
				m_dcIn[c] = sample;
				v[c] = m_dcOut[c] + (c ? -m_feedback : m_feedback) * m_last[1 - c];
				if(c == 0)
					v[c] -= m_delayFeedback * m_lowPass[0];
				m_target[c] += step[c];
				m_coefficient[c] = m_target[c] + Smooth * (m_coefficient[c] - m_target[c]);
				x[c] = v[c];
			}
			for(int s = 0; s < Stages; ++s)
				for(int c = 0; c < 2; ++c)
				{
					const float a = m_coefficient[c];
					const float y = a * x[c] + m_state[s][c];
					m_state[s][c] = x[c] - a * y;
					x[c] = y;
				}
			for(int c = 0; c < 2; ++c)
			{
				m_last[c] = x[c];
				out[c][k] = 0.70711f * (x[c] + m_notch * v[c]);
			}
			out[1][k] += m_delayFeedback * m_lowPass[1];
			m_line[m_write] = x[0];
			m_write = (m_write + 1) & LineMask;
		}
		m_tapsFrom[0] = m_taps[0];
		m_tapsFrom[1] = m_taps[1];
	}
}
