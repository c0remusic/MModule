// The two send effects of our own, in float, marked "+" in the instrument: a chorus, and a phaser or flanger, at
// 44.1 kHz. Their laws, 0-127 to samples and Hz, are measured on what OsTIrus renders: clean, nothing in the wet path
// but the delay or the allpasses. Blocks of 32, return only.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace elektron::drums
{
	// A return's gain for a mix byte, 0-127
	float sendFxLevel(uint8_t _mix);

	// The laws below by byte, 0-127, for those who show them: the chorus's, the phaser's and the flanger's LFO in Hz;
	// the phaser's Q by Spread and its allpasses' frequency in Hz on the Frequency byte's scale (a byte, or between
	// two as the sweep moves); the DP/4's LFO in Hz, a delay's time in ms, and its -99 to 99 of a signed byte
	double sendFxRateHz(uint8_t _rate);
	double phaserQ(uint8_t _spread);
	double phaserHz(double _frequency);
	double dp4RateHz(uint8_t _rate);
	double dp4DelayMs(uint8_t _time);
	double dp4Signed(uint8_t _value);
	// The DP/4's S&H, 0-127 for its 0 to 100: three times its LFO's law at the same number, 0 none
	double dp4SampleHoldHz(uint8_t _rate);

	// A stereo line an LFO shortens, fed back, each channel on the other's side of the LFO. A flanger is this at a
	// short delay
	class ModulatedDelay
	{
	public:
		static constexpr int BlockSize = 32;
		// Samples a step of the delay byte: up to 23 ms, up to 0.75 ms
		static constexpr double ChorusStep = 8, FlangerStep = 32.0 / 127;

		explicit ModulatedDelay(double _samplesPerStep);

		enum class Shape : uint8_t { Sine, Triangle, Saw, Square, SampleHold, SampleGlide };

		// 0-127. The LFO takes _depth / 128 of the delay away at its top. A change of delay glides over blocks. A saw
		// falls; the random shapes draw a value a turn, held or glided to
		void set(uint8_t _delay, uint8_t _depth, uint8_t _rate, uint8_t _feedback, Shape _shape = Shape::Triangle);
		// The LFO at _hz in place of the rate byte's, until the next set
		void setHz(double _hz);

		// At the settings at once
		void settle();

		void process(const float* _inL, const float* _inR, float* _outL, float* _outR);
		void clear();

	private:
		static constexpr uint32_t Size = 2048, Mask = Size - 1;		// the longest delay is 1,017 samples

		float lfo();

		const double m_samplesPerStep;
		Shape m_shape = Shape::Triangle;
		uint32_t m_random = 0x2545f491;
		float m_held = 0, m_drawn = 0;
		std::array<std::vector<float>, 2> m_lines;
		uint32_t m_write = 0, m_phase = 0, m_step = 0;
		float m_delay = 1, m_swing = 0, m_delayTo = 1, m_swingTo = 0, m_feedback = 0, m_feedbackTo = 0, m_top = 0;
	};

	// A line read from no delay up, through an allpass between two samples, flat to the top: OsTIrus's chorus types past
	// Classic read so (a straight line would dull the highs), its coefficient 1 - t of the fraction t, not the exact
	// (1 - t) / (1 + t): 22 to 30 dB closer to the Virus on noise
	class SweptLine
	{
	public:
		SweptLine();

		void write(float _in)
		{
			m_write = (m_write + 1) & Mask;
			m_line[m_write] = _in;
		}
		// _delay samples, 0 to 1,023, before the sample written last; _state is the allpass's, one for each read
		float read(float _delay, float& _state) const;
		void clear();

	private:
		static constexpr uint32_t Size = 1024, Mask = Size - 1;

		std::vector<float> m_line;
		uint32_t m_write = 0;
	};

	// A vibrato: a line per channel, both read at the same delay, a sine moving it between none and the depth's top;
	// nothing of the dry signal, no feedback. Measured on OsTIrus's Vibrato chorus: the LFO at 2^(rate / 32) Hz, the
	// depth as a bend of pitch, the same at any rate (the delay's top shrinks as the LFO speeds up), at most 512 samples
	class Vibrato
	{
	public:
		static constexpr int BlockSize = 32;

		// 0-127. A change glides over blocks
		void set(uint8_t _depth, uint8_t _rate);
		// The LFO at _hz in place of the rate byte's, until the next set; the depth's bend stays
		void setHz(double _hz);
		// The delay's top, in samples, at a depth byte and an LFO in Hz
		static double top(uint8_t _depth, double _hz);

		// At the settings at once
		void settle();

		void process(const float* _inL, const float* _inR, float* _outL, float* _outR);
		void clear();

	private:
		SweptLine m_line[2];
		uint8_t m_depth = 0;
		uint32_t m_phase = 0, m_step = 0;
		float m_top = 0, m_topTo = 0, m_delay = 0, m_state[2] = {};
	};

	// Air: a line per channel, the right read where the left is not, between none and the depth's top; nothing of the
	// dry signal, no feedback. Measured on OsTIrus's Air chorus: the sweep half a triangle and half a raised cosine;
	// Depth alone sets it, its top growing to 512 samples at 77, past which the sweep speeds up instead, from 0.0999 Hz
	// to 0.61 Hz at 127
	class Air
	{
	public:
		static constexpr int BlockSize = 32;

		// 0-127. A change glides over blocks
		void set(uint8_t _depth);
		// The depth's top in samples and its sweep in Hz
		static double top(uint8_t _depth);
		static double hz(uint8_t _depth);

		void settle();

		void process(const float* _inL, const float* _inR, float* _outL, float* _outR);
		void clear();

	private:
		SweptLine m_line[2];
		uint32_t m_phase = 0, m_step = 0;
		float m_top = 0, m_topTo = 0, m_delay[2] = {}, m_state[2] = {};
	};

	// A chorus after a bucket brigade's: below a first-order crossover the input passes as it is; above it, a direct
	// part and one voice, a line read straight between samples and low-passed by a second order near 4.2 kHz, a
	// triangle sweeping its delay, the right channel half a turn on. Measured on OsTIrus's Vintage chorus: the triangle
	// at 0.25 x 2^(rate / 24) Hz, Depth widening the sweep about 132 samples, X-Over moving the crossover from 19.7 Hz
	// up a semitone a step or near; Amount, Feedback and Shape do nothing there
	class Vintage
	{
	public:
		static constexpr int BlockSize = 32;

		Vintage();

		// 0-127. A change of depth glides over blocks
		void set(uint8_t _depth, uint8_t _rate, uint8_t _xover = 0);
		// The triangle at _hz in place of the rate byte's, until the next set
		void setHz(double _hz);
		static double hz(uint8_t _rate);
		// The read's shortest and longest delay at a depth, in samples
		static double shortest(uint8_t _depth);
		static double longest(uint8_t _depth);
		// The crossover's frequency at an X-Over byte, in Hz
		static double crossover(uint8_t _xover);

		void settle();
		// The triangle at _turn: for a test that meets another's
		void restart(double _turn);

		void process(const float* _inL, const float* _inR, float* _outL, float* _outR);
		void clear();

	private:
		static constexpr uint32_t Size = 512, Mask = Size - 1;			// the longest delay is 247 samples

		std::vector<float> m_line[2];
		uint32_t m_write = 0, m_phase = 0, m_step = 0;
		float m_k = 0, m_pole = 0;										// the crossover's one-pole
		float m_low[2] = {}, m_lastIn[2] = {};
		float m_lo = 0, m_hi = 0, m_loTo = 0, m_hiTo = 0, m_delay[2] = {};
		float m_b0 = 0, m_b1 = 0, m_a1 = 0, m_a2 = 0;					// the low-pass; its b2 is b0
		float m_z1[2] = {}, m_z2[2] = {};
	};

	// One to six second-order allpasses in a row, the same on a channel, the right ones a little higher, a triangle
	// sweeping them up together; the last fed back to the first
	class Phaser
	{
	public:
		static constexpr int BlockSize = 32;

		// _stages 1-6; the rest 0-127. _spread narrows and widens the notches. A change of _stages fades out and in over
		// two blocks, the rest glides
		void set(int _stages, uint8_t _frequency, uint8_t _spread, uint8_t _depth, uint8_t _rate, uint8_t _feedback);
		// The sweep at _hz in place of the rate byte's, until the next set
		void setHz(double _hz);

		// At the settings at once
		void settle();

		void process(const float* _inL, const float* _inR, float* _outL, float* _outR);
		void clear();

	private:
		static constexpr int MaxStages = 6;

		// Two integrators: a direct form's state ran away, to 1e28, with a low frequency swept at 28 Hz and feedback
		struct Stage
		{
			float low = 0, band = 0;
		};

		int m_stages = 1, m_stagesTo = 1;
		bool m_entering = false;
		float m_frequency = 0, m_depth = 0, m_sweep = 0;
		float m_damping = 1, m_dampingTo = 1, m_feedback = 0, m_feedbackTo = 0;
		uint32_t m_phase = 0, m_step = 0;
		Stage m_state[MaxStages][2];
		float m_last[2] = {};
	};

	// Twelve first-order allpasses, one coefficient a sine moves, the channels together or on either side of it, each
	// chain fed back into the other's; then the input mixed back for the notches. A delay line takes the left chain's
	// output: its left tap, low-passed, goes back into the left chain, its right one into the right return. Its laws
	// are measured on what Phaser-DDL renders, an emulation of the Ensoniq DP/4's phaser, at the host's rate
	class Dp4Phaser
	{
	public:
		static constexpr int BlockSize = 32;

		// 0-127. Centre, feedback, notch and delay feedback are signed about 64: a notch under it turns the input mixed
		// back over. A delay time is 0 to 1.6 s on the DP/4's steps of 1.6 ms
		struct Settings
		{
			uint8_t center = 64, width = 0, rate = 0, feedback = 64, notch = 127;
			bool outOfPhase = true;
			uint8_t delayLeft = 0, delayRight = 0, delayFeedback = 64;
			uint8_t sampleHold = 0;		// the S&H's rate; 0 the sine as it moves
		};

		Dp4Phaser();

		void set(const Settings& _settings);
		// The sine at _hz in place of the rate byte's, until the next set
		void setHz(double _hz);
		// At the settings at once
		void settle();

		void process(const float* _inL, const float* _inR, float* _outL, float* _outR);
		void clear();

	private:
		static constexpr int Stages = 12;
		static constexpr uint32_t LineSize = 1 << 17, LineMask = LineSize - 1;	// over 70,123 samples, 1.6 s

		float m_center = 0, m_width = 0, m_feedback = 0, m_notch = 0, m_delayFeedback = 0;
		bool m_outOfPhase = true;
		std::vector<float> m_line;
		uint32_t m_write = 0, m_taps[2] = {2, 2}, m_tapsFrom[2] = {2, 2};
		float m_lowPass[2] = {};
		float swing();

		uint32_t m_phase = 0, m_step = 0;
		// The S&H: its phase, its step a block, 0 for none; the sine's value it took at its last turn
		uint32_t m_holdPhase = 0, m_holdStep = 0;
		float m_held = 0;
		float m_target[2] = {}, m_targetTo[2] = {}, m_coefficient[2] = {};
		float m_state[Stages][2] = {};
		float m_last[2] = {}, m_dcIn[2] = {}, m_dcOut[2] = {};
	};
}
