#pragma once

// The Digitakt II's send reverb (OS 1.16, its routine 0xb80fe0), at 44.1 or 48 kHz (its rings hold the lengths up to
// about 56 kHz, its pre-delay a block down to about 42 kHz): the Reverb slot's third type, Dense Verb.
// Ported from a float32 model that elektron-work's dt2-reverb matched bit for bit against the DT2's own code run in its
// oracle (vectors-dt2\sends rev_*); every table of the DT2 comes here from a fitted formula (20 constants, the user's
// choice on 2026-11-24), its 16 lengths are the firmware's at 48 kHz, scaled to the rate (the nearest prime for the
// static ones).
//
// Per 32-sample block: the two channels' mean into a pre-delay (PRE), a one-pole lowpass and highpass (LPF, HPF), eight
// allpasses in series (their gain from DEC), then eight lines, two of them read on a slow quadrature oscillator through
// a cubic interpolator, the input added to lines 0 and 4, a shelf on each (FREQ, GAIN), two 4x4 Householder mixes
// (their gains from DEC) back into the lines; left and right each the sum of four. The return is the wet signal alone
// (the DT2's mix word is 1). Settings as the DT2's knobs, 0-127, read at each block without smoothing, as the DT2 reads
// them.

#include <array>
#include <cstdint>
#include <vector>

namespace elektron::drums
{
	class Dt2Reverb
	{
	public:
		static constexpr int BlockSize = 32;

		// The rings are allocated here; nothing is in process
		explicit Dt2Reverb(double _rate);

		// 0-127, from the next block
		void set(uint8_t _pre, uint8_t _dec, uint8_t _freq, uint8_t _gain, uint8_t _hpf, uint8_t _lpf);
		// Nothing left in the rings and the filters, the oscillator at its start
		void clear();

		// PRE's pre-delay, the same at every rate
		static double preDelaySeconds(uint8_t _pre);

		void process(const float* _inL, const float* _inR, float* _outL, float* _outR);

	private:
		static constexpr int Stages = 8, PreSize = 16384, AllpassSize = 1024, LineSize = 8192;
		static constexpr float PreMul48 = 16128.4092f;	// PRE^2 into samples at 48 kHz (the DSP's word)

		struct Ring
		{
			std::vector<float> data;
			int write = 0, delay = 0;
		};

		// Of the rate
		std::array<float, Stages> m_allpassK{};				// the allpasses' gains' signs and sizes
		std::array<std::array<float, 128>, Stages> m_lineGains{};	// each line's gain for DEC 0-127
		std::array<float, 64> m_allpassScale{};				// the allpasses' scale for DEC 0-63
		std::array<float, 128> m_shelfPoles{};				// FREQ's
		std::array<float, 1024> m_poles{};					// HPF's and LPF's, 0 from 508 on
		std::array<std::array<float, 4>, 128> m_interp{};	// cubic Lagrange weights of taps i, i-1, i-2, i-3
		std::array<float, Stages> m_lineLengths{};
		float m_preMul = 0, m_preMin = 0, m_lfoStep = 0, m_lfoDepth = 0;

		// Of the settings
		int m_preDelay = 0;
		float m_lowpass = 0, m_lowpassC = 0, m_highpass = 0, m_highpassA0 = 0;
		float m_b0 = 0, m_b1 = 0, m_a1 = 0;
		std::array<float, Stages> m_allpassGains{}, m_gains{};

		// What runs
		Ring m_pre;
		std::array<Ring, Stages> m_allpasses, m_lines;
		float m_s1 = 0, m_y1 = 0;							// the input filter's
		std::array<float, Stages> m_shelfX{}, m_shelfY{};
		float m_sin = 0, m_cos = -1;
	};
}
