#pragma once

// The Digitakt II's send chorus (OS 1.16, its routine 0xb82680), at any rate: the Chorus slot's sixth type. Ported from
// a float32 model that elektron-work's dt2-chorus matched bit for bit against the DT2's own code run in its oracle (15
// vectors of vectors-dt2\sends); its rate-dependent tables come here from their formulas, its delay times from 32
// measured constants (no closed form), the user's choice on 2026-11-24.
//
// Per 32-sample block: a one-pole highpass on the input (HPF), three fixed lowpass biquads (7 kHz, Q 0.8), then per
// sample a 512-sample line per channel read on a triangle LFO (SPD, 0.28 to 3.10 Hz) through a one-pole at 72 Hz, the
// right channel's phase WDTH / 2 past the left, the depth (DPTH) smoothed. No feedback. The return is the wet signal
// alone (the DT2's mix word is 1). DPTH, SPD, HPF and WDTH as the DT2's knobs, 0-127.

#include <array>
#include <cstdint>

namespace elektron::drums
{
	class Dt2Chorus
	{
	public:
		static constexpr int BlockSize = 32;
		static constexpr int Line = 512;

		explicit Dt2Chorus(double _rate);

		// 0-127, from the next block: DPTH and WDTH glide over blocks as the DT2's do
		void set(uint8_t _depth, uint8_t _speed, uint8_t _hpf, uint8_t _width);
		// At the settings at once, the lines and filters empty, the LFO at its start (the smallest delay)
		void settle();

		void process(const float* _inL, const float* _inR, float* _outL, float* _outR);

		// The DT2's float of a knob: its frame word (_value << 8) over 32,512
		static float knob(uint8_t _value);

		// What the DT2 keeps between blocks, public for a test that starts it where the DT2's oracle starts
		struct State
		{
			std::array<std::array<float, Line>, 2> line{};
			int write = 0;
			float delay[2]{}, phase = 0, increment = 0, triangle[2]{}, lfo[2]{}, depth = 0, width = 0;
			float highpass[2][5]{};		// a0 a1 b1 x1 y1
			float lowpass[2][3][4]{};		// x1 x2 y1 y2 of each biquad
			float glide = 0, glideTarget = 0;	// the depth per sample
			float mix[2]{}, mixTarget = 1;		// wet against dry
		};
		State& state() { return m_s; }

	private:
		void blockSettings();
		void lfo();

		// Of the rate
		float m_invRate = 0, m_lfoPole = 0, m_depthMul = 0, m_sampleCoef = 0;
		// 32 times in samples; the DT2's LFO may read one past, its HPF's first pole
		std::array<float, 33> m_delays{};
		std::array<float, 128> m_poles{};		// HPF's
		float m_biquad[5]{};					// b0 b1 b2 a1 a2

		float m_knobs[4]{};						// DPTH SPD HPF WDTH as floats
		State m_s;
	};
}
