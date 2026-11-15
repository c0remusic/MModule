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

		// An LFO's phase for a block, whole turns in 32 bits
		uint32_t stepOf(const double _hz)
		{
			return static_cast<uint32_t>(_hz * ModulatedDelay::BlockSize / Rate * 4294967296.0);
		}

		uint32_t phaseStep(const uint8_t _rate)
		{
			return stepOf(sendFxRateHz(_rate));
		}

		// 0 to 1 and back in a turn
		float triangle(const uint32_t _phase)
		{
			const uint32_t rising = _phase >> 31 ? ~_phase : _phase;
			return static_cast<float>(rising) / 2147483648.0f;
		}

		// Straight between two knots
		template<size_t N> double straight(const Knot (&_knots)[N], const uint8_t _value)
		{
			size_t i = 0;
			while(i + 2 < N && _value > _knots[i + 1].at)
				++i;
			const Knot& a = _knots[i];
			const Knot& b = _knots[i + 1];
			const double t = std::clamp(static_cast<double>(_value - a.at) / (b.at - a.at), 0.0, 1.0);
			return a.value + (b.value - a.value) * t;
		}

		// Air's and Hyper's sweep, 0 to 1 and back: half a triangle, half a raised cosine
		float airSweep(const uint32_t _phase)
		{
			const float turn = static_cast<float>(_phase >> 8) / 16777216.0f;
			return 0.5f * triangle(_phase) + 0.25f - 0.25f * std::cos(turn * 6.2831853f);
		}

		// Air's top and sweep at a depth, which Hyper takes 21 higher: 0 to 148
		double sweptTop(const int _depth)
		{
			// Fitted on noise: straight to 32, 4/3 every 8 past it, 512 from 77
			static constexpr Knot Top[] = {{8, 25.75}, {16, 51.41}, {24, 77.09}, {32, 103.84}, {40, 138.44},
				{48, 184.6}, {56, 246.15}, {64, 328.22}, {72, 437.65}, {76, 505.38}};
			if(_depth < 8)
				return Top[0].value * _depth / 8;
			return _depth > 76 ? 512 : knots(Top, static_cast<uint8_t>(_depth));
		}

		// Past 76 the sweep speeds up in eighths of its slowest, fitted one depth at a time, Air's to 127 and Hyper's
		// past (a speed between two of them missed the Virus by 10 dB more on noise). Where both were fitted they
		// agree, but at Air's 123, Hyper's 102: 42 and 43
		int sweptEighths(const int _depth)
		{
			static constexpr uint8_t Eighths[] = {8, 8, 8, 9, 9, 9, 10, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15,
				16, 16, 17, 18, 18, 19, 20, 20, 21, 22, 23, 24, 24, 25, 26, 27, 28, 29, 31, 32, 33, 34, 35, 37, 38, 39,
				41, 42, 44, 46, 47, 49, 51, 53, 55, 57, 59, 61, 63, 66, 68, 71, 73, 76, 79, 82, 85, 88, 91, 94, 98, 102,
				105};
			static_assert(std::size(Eighths) == 149 - 77);
			return _depth <= 76 ? 8 : Eighths[_depth - 77];
		}

		double sweptHz(const int _depth)
		{
			constexpr double Slowest = 0.09989;
			return Slowest * sweptEighths(_depth) / 8;
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

	double dp4SampleHoldHz(const uint8_t _rate)
	{
		// Measured on Phaser-DDL: 0.74, 1.49, 2.75, 4.80, 8.4 and 14.1 Hz at 30 to 80, 3.0 times the LFO's rate at the
		// same number within the steps' 2 % of jitter
		const double rate = _rate * 100.0 / 127;
		return rate > 0 ? 3 * std::exp(-5.684159 + 0.040309 * rate) * std::pow(rate, 0.913459) : 0;
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

	void ModulatedDelay::setHz(const double _hz)
	{
		m_step = stepOf(_hz);
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

	SweptLine::SweptLine() : m_line(Size, 0.0f)
	{
	}

	float SweptLine::read(const float _delay, float& _state) const
	{
		const auto whole = static_cast<uint32_t>(_delay);
		const float eta = 1 - (_delay - static_cast<float>(whole));
		const float a = m_line[(m_write - whole) & Mask], b = m_line[(m_write - whole - 1) & Mask];
		_state = eta * a + b - eta * _state;
		return _state;
	}

	void SweptLine::clear()
	{
		std::fill(m_line.begin(), m_line.end(), 0.0f);
	}

	double Vibrato::top(const uint8_t _depth, const double _hz)
	{
		// The top at 4 Hz, measured every 16 of depth; it scales as 1 / Hz, a bend of pitch the rate keeps
		static constexpr Knot At4Hz[] = {{16, 2.72}, {32, 5.11}, {48, 9.14}, {64, 16.42}, {80, 29.08}, {96, 51.9},
			{112, 92.1}, {127, 158.44}};
		const double at4 = _depth < 16 ? At4Hz[0].value * _depth / 16 : knots(At4Hz, _depth);
		constexpr double Longest = 512;
		return _hz > 0 ? std::min(Longest, at4 * 4 / _hz) : Longest;
	}

	void Vibrato::set(const uint8_t _depth, const uint8_t _rate)
	{
		m_depth = _depth;
		setHz(std::exp2(_rate / 32.0));
	}

	void Vibrato::setHz(const double _hz)
	{
		m_step = stepOf(_hz);
		m_topTo = static_cast<float>(top(m_depth, _hz));
	}

	void Vibrato::settle()
	{
		m_top = m_topTo;
	}

	void Vibrato::clear()
	{
		for(auto& line : m_line)
			line.clear();
		m_state[0] = m_state[1] = 0;
	}

	void Vibrato::process(const float* const _inL, const float* const _inR, float* const _outL, float* const _outR)
	{
		// The sine at the block's end, straight between
		m_phase += m_step;
		m_top = m_topTo + Glide * (m_top - m_topTo);
		const float turn = static_cast<float>(m_phase >> 8) / 16777216.0f;
		const float to = m_top * (0.5f - 0.5f * std::cos(turn * 6.2831853f));
		const float step = (to - m_delay) / BlockSize;
		const float* const in[2] = {_inL, _inR};
		float* const out[2] = {_outL, _outR};
		float delay = m_delay;
		for(int k = 0; k < BlockSize; ++k)
		{
			delay += step;
			// Written first: no delay reads the sample in
			for(size_t c = 0; c < 2; ++c)
			{
				m_line[c].write(in[c][k]);
				out[c][k] = m_line[c].read(delay, m_state[c]);
			}
		}
		m_delay = to;
	}

	double Air::top(const uint8_t _depth)
	{
		return sweptTop(_depth);
	}

	double Air::hz(const uint8_t _depth)
	{
		return sweptHz(_depth);
	}

	void Air::set(const uint8_t _depth)
	{
		m_topTo = static_cast<float>(top(_depth));
		m_step = stepOf(hz(_depth));
	}

	void Air::settle()
	{
		m_top = m_topTo;
	}

	void Air::clear()
	{
		for(auto& line : m_line)
			line.clear();
		m_state[0] = m_state[1] = 0;
	}

	void Air::process(const float* const _inL, const float* const _inR, float* const _outL, float* const _outR)
	{
		m_phase += m_step;
		m_top = m_topTo + Glide * (m_top - m_topTo);
		const float sweep = airSweep(m_phase);
		// The right where the left is not, each straight from where the last block left it
		const float to[2] = {m_top * sweep, m_top * (1 - sweep)};
		const float step[2] = {(to[0] - m_delay[0]) / BlockSize, (to[1] - m_delay[1]) / BlockSize};
		const float* const in[2] = {_inL, _inR};
		float* const out[2] = {_outL, _outR};
		for(size_t c = 0; c < 2; ++c)
		{
			float delay = m_delay[c];
			for(int k = 0; k < BlockSize; ++k)
			{
				delay += step[c];
				m_line[c].write(in[c][k]);
				out[c][k] = m_line[c].read(delay, m_state[c]);
			}
			m_delay[c] = to[c];
		}
	}

	namespace
	{
		// Vintage, fitted on noise and on impulses. Its direct part's gain; the voice's low-pass and gain
		constexpr double VintageDirect = 0.7101;
		constexpr double VintageCutoff = 4198.8, VintageQ = 0.715, VintageWet = 0.982;
		// The read's ends against Depth, straight between, measured every 16: the impulses' peaks less the low-pass's
		// 2.19 samples
		constexpr Knot VintageShortest[] = {{0, 131.99}, {64, 74.9}, {127, 68.05}};
		constexpr Knot VintageLongest[] = {{0, 131.99}, {64, 178.9}, {127, 246.78}};
	}

	Vintage::Vintage()
	{
		for(auto& line : m_line)
			line.assign(Size, 0.0f);
		const double k = std::tan(Pi * VintageCutoff / Rate);
		const double d = 1 + k / VintageQ + k * k;
		m_b0 = static_cast<float>(VintageWet * k * k / d);
		m_b1 = 2 * m_b0;
		m_a1 = static_cast<float>(2 * (k * k - 1) / d);
		m_a2 = static_cast<float>((1 - k / VintageQ + k * k) / d);
		set(0, 64, 0);
		settle();
	}

	double Vintage::hz(const uint8_t _rate)
	{
		return 0.25 * std::exp2(_rate / 24.0);
	}

	double Vintage::shortest(const uint8_t _depth)
	{
		return straight(VintageShortest, _depth);
	}

	double Vintage::longest(const uint8_t _depth)
	{
		return straight(VintageLongest, _depth);
	}

	double Vintage::crossover(const uint8_t _xover)
	{
		// Fitted on noise every 8: 19.7 Hz at 0, 13.3 kHz at 112; held under Nyquist, where the low part takes all
		return std::min(19.74 * std::exp2(_xover / 11.92), 0.495 * Rate);
	}

	void Vintage::set(const uint8_t _depth, const uint8_t _rate, const uint8_t _xover)
	{
		m_loTo = static_cast<float>(shortest(_depth));
		m_hiTo = static_cast<float>(longest(_depth));
		m_step = stepOf(hz(_rate));
		const double k = std::tan(Pi * crossover(_xover) / Rate);
		m_k = static_cast<float>(k / (1 + k));
		m_pole = static_cast<float>((1 - k) / (1 + k));
	}

	void Vintage::setHz(const double _hz)
	{
		m_step = stepOf(_hz);
	}

	void Vintage::settle()
	{
		m_lo = m_loTo;
		m_hi = m_hiTo;
		m_delay[0] = m_lo + (m_hi - m_lo) * triangle(m_phase);
		m_delay[1] = m_lo + (m_hi - m_lo) * triangle(m_phase + 0x80000000u);
	}

	void Vintage::restart(const double _turn)
	{
		m_phase = static_cast<uint32_t>(std::fmod(_turn, 1.0) * 4294967296.0);
		settle();
	}

	void Vintage::clear()
	{
		for(auto& line : m_line)
			std::fill(line.begin(), line.end(), 0.0f);
		for(int c = 0; c < 2; ++c)
			m_low[c] = m_lastIn[c] = m_z1[c] = m_z2[c] = 0;
	}

	void Vintage::process(const float* const _inL, const float* const _inR, float* const _outL, float* const _outR)
	{
		m_phase += m_step;
		m_lo = m_loTo + Glide * (m_lo - m_loTo);
		m_hi = m_hiTo + Glide * (m_hi - m_hiTo);
		// The right half a turn on, each straight from where the last block left it
		const float to[2] = {m_lo + (m_hi - m_lo) * triangle(m_phase),
			m_lo + (m_hi - m_lo) * triangle(m_phase + 0x80000000u)};
		const float* const in[2] = {_inL, _inR};
		float* const out[2] = {_outL, _outR};
		const auto direct = static_cast<float>(VintageDirect);
		for(int c = 0; c < 2; ++c)
		{
			const float step = (to[c] - m_delay[c]) / BlockSize;
			float delay = m_delay[c];
			auto& line = m_line[c];
			uint32_t write = m_write;
			for(int k = 0; k < BlockSize; ++k)
			{
				const float x = in[c][k];
				m_low[c] = m_k * (x + m_lastIn[c]) + m_pole * m_low[c];
				m_lastIn[c] = x;
				const float high = x - m_low[c];
				write = (write + 1) & Mask;
				line[write] = high;
				delay += step;
				const auto whole = static_cast<uint32_t>(delay);
				const float t = delay - static_cast<float>(whole);
				const float a = line[(write - whole) & Mask], b = line[(write - whole - 1) & Mask];
				const float read = a + t * (b - a);
				// The low-pass, transposed direct form II
				const float wet = m_b0 * read + m_z1[c];
				m_z1[c] = m_b1 * read - m_a1 * wet + m_z2[c];
				m_z2[c] = m_b0 * read - m_a2 * wet;
				out[c][k] = m_low[c] + direct * high + wet;
			}
			m_delay[c] = to[c];
		}
		m_write = (m_write + BlockSize) & Mask;
	}

	namespace
	{
		// Hyper's crossover, fitted on noise: first order, the low part straight to the output
		constexpr double HyperCrossover = 307.5;
		// Its sweeps' lowest delay, a share of the top (impulses, every amount and depth to 100; on noise a thousandth
		// more costs 13 dB where a voice turns at the bottom); its top's most
		constexpr double HyperLo = 0.0589, HyperTopMost = 510.04;
		// Its top is 2.1243 times Air's at the same depth, fitted every depth from 40 to 55 and on noise at 32 (a
		// tenth of a sample off costs dB on noise); its speed-ups, fitted every depth from 56, are Air's 21 deeper
		constexpr double HyperTopOfAir = 2.1243;
		constexpr int HyperDeeper = 21;
		// The dry high part's gain against Amount, straight from 0 to 64 and from 64 to 127, fitted on noise every 16;
		// each voice's 0.98 of it, the second's coming in from 0 to 64, the third's from 64 to 128
		constexpr Knot HyperDry[] = {{0, 0.7102}, {64, 0.5812}, {127, 0.5053}};
		constexpr double HyperVoice = 0.98;
		// Each voice's sweep advances a whole number of quarters of 44100 / 2^26 Hz: the floor of a share of the
		// depth's base, 19 quarters per eighth of Air's slowest at the depth 21 higher (152 to 1,995). The share runs
		// straight from Amount 0 to 64 and from 64 to 128; the second's at 0 and the third's to 64, where they are
		// silent, carried on. Every voice's rate fitted on impulses (depth 32, 64, 100 and 127, every 16 of Amount at
		// 127) is its quarter to 0.01 of one
		constexpr double HyperShare[Hyper::Voices][3] = {{1, 0.84, 0.7}, {1.4, 1.16, 1}, {1.6, 1.6, 1.4}};
		constexpr int HyperQuartersPerEighth = 19;
		constexpr double HyperQuarter = 44100 / 67108864.0;

		uint32_t hyperQuarters(const uint8_t _depth, const uint8_t _amount, const int _voice)
		{
			const double* const k = HyperShare[_voice];
			const double share = _amount <= 64 ? k[0] + (k[1] - k[0]) * _amount / 64
				: k[1] + (k[2] - k[1]) * (_amount - 64) / 64;
			const int base = HyperQuartersPerEighth * sweptEighths(_depth + HyperDeeper);
			// 1e-9 above, so that a share that lands on a whole number, 1.5 x 152, is not floored below it
			return static_cast<uint32_t>(std::floor(base * share + 1e-9));
		}
	}

	Hyper::Hyper()
	{
		const double k = std::tan(Pi * HyperCrossover / Rate);
		m_k = static_cast<float>(k / (1 + k));
		m_pole = static_cast<float>((1 - k) / (1 + k));
		const double turns[Voices] = {0, 1.0 / 3, 2.0 / 3};
		restart(turns);
	}

	double Hyper::top(const uint8_t _depth)
	{
		return std::min(HyperTopMost, HyperTopOfAir * sweptTop(_depth));
	}

	double Hyper::hz(const uint8_t _depth, const uint8_t _amount, const int _voice)
	{
		return hyperQuarters(_depth, _amount, _voice) * HyperQuarter;
	}

	double Hyper::gain(const uint8_t _amount, const int _part)
	{
		const double dry = straight(HyperDry, _amount);
		switch(_part)
		{
		case 0:
			return dry;
		case 1:
			return HyperVoice * dry;
		case 2:
			return HyperVoice * dry * std::min(1.0, _amount / 64.0);
		default:
			return HyperVoice * dry * std::max(0.0, (_amount - 64) / 64.0);
		}
	}

	void Hyper::set(const uint8_t _depth, const uint8_t _amount)
	{
		m_topTo = static_cast<float>(top(_depth));
		// At depth 0 the Virus's Hyper gives its input back, on noise to the last bit
		m_throughTo = _depth == 0 ? 1.0f : 0.0f;
		for(int v = 0; v < Voices; ++v)
			m_step[v] = hyperQuarters(_depth, _amount, v) << 11;	// a quarter is 2^6 of our 2^32, 32 samples a block
		for(int g = 0; g <= Voices; ++g)
			m_gainTo[g] = static_cast<float>(gain(_amount, g));
	}

	void Hyper::settle()
	{
		m_top = m_topTo;
		m_through = m_throughTo;
		std::copy(std::begin(m_gainTo), std::end(m_gainTo), std::begin(m_gain));
		// The voices where their sweeps are, not gliding from where the last top left them
		const auto lo = static_cast<float>(HyperLo);
		for(int v = 0; v < Voices; ++v)
			for(int c = 0; c < 2; ++c)
				m_delay[v][c] = m_top * (lo + (1 - lo) * airSweep(m_phase[v] + (c ? 0x40000000u : 0)));
	}

	void Hyper::restart(const double (&_turns)[Voices])
	{
		for(int v = 0; v < Voices; ++v)
			m_phase[v] = static_cast<uint32_t>(std::fmod(_turns[v], 1.0) * 4294967296.0);
		settle();
	}

	void Hyper::clear()
	{
		for(auto& line : m_line)
			line.clear();
		for(auto& state : m_state)
			state[0] = state[1] = 0;
		m_low[0] = m_low[1] = m_lastIn[0] = m_lastIn[1] = 0;
	}

	void Hyper::process(const float* const _inL, const float* const _inR, float* const _outL, float* const _outR)
	{
		m_top = m_topTo + Glide * (m_top - m_topTo);
		float gainFrom[Voices + 1], gainStep[Voices + 1];
		for(int g = 0; g <= Voices; ++g)
		{
			gainFrom[g] = m_gain[g];
			m_gain[g] = m_gainTo[g] + Glide * (m_gain[g] - m_gainTo[g]);
			gainStep[g] = (m_gain[g] - gainFrom[g]) / BlockSize;
		}
		float to[Voices][2], step[Voices][2];
		const float lo = static_cast<float>(HyperLo) * m_top;
		for(int v = 0; v < Voices; ++v)
		{
			m_phase[v] += m_step[v];
			for(int c = 0; c < 2; ++c)
			{
				// The right a quarter turn past the left
				to[v][c] = lo + (m_top - lo) * airSweep(m_phase[v] + (c ? 0x40000000u : 0));
				step[v][c] = (to[v][c] - m_delay[v][c]) / BlockSize;
			}
		}
		const float* const in[2] = {_inL, _inR};
		float* const out[2] = {_outL, _outR};
		for(int c = 0; c < 2; ++c)
		{
			float gains[Voices + 1];
			std::copy(std::begin(gainFrom), std::end(gainFrom), gains);
			float delays[Voices];
			for(int v = 0; v < Voices; ++v)
				delays[v] = m_delay[v][c];
			auto& line = m_line[c];
			for(int k = 0; k < BlockSize; ++k)
			{
				const float x = in[c][k];
				m_low[c] = m_k * (x + m_lastIn[c]) + m_pole * m_low[c];
				m_lastIn[c] = x;
				const float high = x - m_low[c];
				line.write(high);
				for(int g = 0; g <= Voices; ++g)
					gains[g] += gainStep[g];
				float y = m_low[c] + gains[0] * high;
				for(int v = 0; v < Voices; ++v)
				{
					delays[v] += step[v][c];
					// A voice faded out is still read, so that it comes back where its sweep is
					y += gains[v + 1] * line.read(delays[v], m_state[v][c]);
				}
				out[c][k] = y;
			}
		}
		for(int v = 0; v < Voices; ++v)
			for(int c = 0; c < 2; ++c)
				m_delay[v][c] = to[v][c];
		// To and from depth 0, the voices and the input crossfaded, straight over the blocks of a glide
		const float from = m_through;
		m_through = m_throughTo + Glide * (m_through - m_throughTo);
		if(from == 0 && m_through < 1e-6f)
		{
			m_through = 0;
			return;
		}
		if(m_through > 1 - 1e-6f)
			m_through = 1;
		const float throughStep = (m_through - from) / BlockSize;
		for(int c = 0; c < 2; ++c)
		{
			float through = from;
			for(int k = 0; k < BlockSize; ++k)
			{
				through += throughStep;
				out[c][k] += through * (in[c][k] - out[c][k]);
			}
		}
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

	void Phaser::setHz(const double _hz)
	{
		m_step = stepOf(_hz);
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
		m_step = stepOf(dp4RateHz(s.rate));
		// An S&H that comes on takes the sine at once and again half a turn later, then at each turn, as Phaser-DDL
		// does from its start
		const uint32_t holdStep = stepOf(dp4SampleHoldHz(s.sampleHold));
		if(holdStep && !m_holdStep)
		{
			m_holdPhase = 1u << 31;
			m_held = std::cos(static_cast<float>(m_phase >> 8) * 3.7450702829239286e-7f);
		}
		m_holdStep = holdStep;
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

	void Dp4Phaser::setHz(const double _hz)
	{
		m_step = stepOf(_hz);
	}

	float Dp4Phaser::swing()
	{
		const float sine = std::cos(static_cast<float>(m_phase >> 8) * 3.7450702829239286e-7f);
		if(!m_holdStep)
			return m_width * sine;
		const uint32_t before = m_holdPhase;
		m_holdPhase += m_holdStep;
		if(m_holdPhase < before)
			m_held = sine;
		return m_width * m_held;
	}

	void Dp4Phaser::settle()
	{
		m_held = std::cos(static_cast<float>(m_phase >> 8) * 3.7450702829239286e-7f);
		const float swing = m_width * m_held;
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
		const float swing = this->swing();
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
