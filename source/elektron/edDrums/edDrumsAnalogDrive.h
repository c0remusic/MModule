#pragma once

#include <cstdint>

#include "elektronBlock.h"

// ANALOG ≈ (ticket 02 of .scratch/elektron-drums-v2): the Syntakt's Analog FX DRIVE approached, without a machine to
// measure it (.scratch/lockstep-post-v1/issues/02-modele-overdrive-master.md). No circuit is public; the model gives the
// behaviours the reviews describe:
//
//     coupling high-pass (its cutoff rising with DRIVE) > gain > asymmetric soft clip > output DC blocker
//
// - the high-pass keeps the low end at mid travel and loses it at the top (12 Hz at 0, 31 Hz at 64, 302 Hz at 127);
// - the gain runs from 0 to +30 dB, which the clip, a rational tanh with its output capped at 1, compresses: the
//   output's level rises with DRIVE and with the input's, ever less, up to its ceiling;
// - the clip's bias makes the curve asymmetric, so that its distortion holds even harmonics, as a single transistor's;
// - a hiss at the stage's input, white, 2^-16 of the input's envelope at its peak before the gain, rises with it (under
//   a held -6 dBFS: -107 dBFS at DRIVE 0, -92 at 64, -77 at 127). It follows the input's envelope, so that a Track's
//   tail still falls to silence and its voice ends.
//
// Mono per lane; it reads its rate and the chain's oversampling factor (setFactor), its cutoffs kept in hertz at either.
namespace elektron::drums
{
	class AnalogDrive
	{
	public:
		explicit AnalogDrive(double _rate);

		// The laws, at a value 0-127 that may be fractional (an LFO's move)
		static double gainDb(double _value);
		static double cutoff(double _value);		// the coupling high-pass's, in Hz

		static constexpr double Bias = 0.2;				// the clip's, in units of its input
		static constexpr double Hiss = 1.0 / 65536;		// at the input, before the gain, of the envelope's level
		static constexpr double DcCutoff = 10;			// the output blocker's, in Hz

		// The rate the stage runs at is the chain's times _factor (1, 2 or 4), from the next block
		void setFactor(int _factor);

		void set(int _lane, int _amountWord);	// a Kit value << 7, an LFO's move in it
		void process(Block& _buf, uint16_t _lanes = 0xffff);
		void clear(int _lane);

	private:
		void setPoles(int _lane);

		double m_rate;
		int m_factor = 1;
		float m_dcPole = 0;
		float m_envelopeFall = 0;					// a sample's fall of the hiss's envelope, 100 ms to -60 dB
		double m_cutoff[Lanes]{};
		float m_pole[Lanes]{};
		float m_gain[Lanes]{}, m_gainTo[Lanes]{};	// at the start of the next block, and where it ramps to
		float m_step[Lanes]{};
		int m_ramp[Lanes]{};						// the ramp's samples left, over the chain's block at its raised rate
		float m_hpX[Lanes]{}, m_hpY[Lanes]{}, m_dcX[Lanes]{}, m_dcY[Lanes]{}, m_envelope[Lanes]{};
		uint32_t m_noise[Lanes]{};
	};
}
