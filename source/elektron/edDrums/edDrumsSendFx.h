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

	// A stereo line a triangle shortens, fed back, each channel on the other's side of the triangle. A flanger is this
	// at a short delay
	class ModulatedDelay
	{
	public:
		static constexpr int BlockSize = 32;
		// Samples a step of the delay byte: up to 23 ms, up to 0.75 ms
		static constexpr double ChorusStep = 8, FlangerStep = 32.0 / 127;

		explicit ModulatedDelay(double _samplesPerStep);

		// 0-127. The triangle takes _depth / 128 of the delay away at its top. A change of delay glides over blocks
		void set(uint8_t _delay, uint8_t _depth, uint8_t _rate, uint8_t _feedback);

		// At the settings at once
		void settle();

		void process(const float* _inL, const float* _inR, float* _outL, float* _outR);
		void clear();

	private:
		static constexpr uint32_t Size = 2048, Mask = Size - 1;		// the longest delay is 1,017 samples

		const double m_samplesPerStep;
		std::array<std::vector<float>, 2> m_lines;
		uint32_t m_write = 0, m_phase = 0, m_step = 0;
		float m_delay = 1, m_swing = 0, m_delayTo = 1, m_swingTo = 0, m_feedback = 0, m_feedbackTo = 0, m_top = 0;
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
}
