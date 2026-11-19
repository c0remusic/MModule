#pragma once

#include <cstdint>

#include "elektronBlock.h"

// The Digitakt II's BR and SRR as drive types (ticket 10 of .scratch/elektron-drums-v2), after the laws its OS 1.16
// plays: elektron-work's vectors-dt2/STAGES.md, measured on the DSP's own code (dt2_stage_oracle.py). Its tables are
// formulas here, within their measured error; nothing of the firmware is in the repository.
namespace elektron::drums
{
	// The knob's word as the DT2's frame unpacker gives it, in float as the DSP: the halfword (a Kit value << 8) times
	// 1/32512, one rounding. A Lockstep word is the value << 7
	float dt2Knob(int _amountWord);

	// The base-width (0x1ccbd8, set by 0x1ccc58): the Syntakt's BaseWidth, a one-pole low-pass then a one-pole
	// high-pass on it, with the DT2's table of poles, which is the Syntakt firmware's own: exp(-w g(w)) on an equal
	// scale from 5 Hz (w its frequency in radians at the rate), g a fit within 1.7e-6 of the pole, 0 from index 508.
	// Its knobs as the DSP's setter reads them: trunc(knob x 32512), which a word's float puts one low on some values
	double dt2BaseWidthPole(int _index, double _rate);
	int dt2BaseWidthKnob(int _word);

	// BR (0x1cdd8b): a crossfade of an n-bit and an (n+1)-bit quantiser of (x + 1) / 2, n and the fraction from the
	// bits at BR x 127 (31 at 0, then a cubic in BR, 15.69 bits at 1, 4.19 at 64, 1.90 at 127). BR 0 passes the Track
	// through. No state, the same at every rate
	class Dt2Br
	{
	public:
		// The bits at a value 0-127, fractional between the integers as the DSP interpolates its table
		static double bits(double _value);

		void set(int _lane, int _amountWord);	// a Kit value << 7, an LFO's move in it
		void process(Block& _buf, uint16_t _lanes = 0xffff) const;
		void clear(int) {}

	private:
		int m_bits[Lanes]{};			// n, 0 off
		float m_fraction[Lanes]{};
	};

	// SRR (0x1cdecb): two sample-and-holds, of n + 1 and n + 2 samples at 48 kHz, crossfaded by the fraction of
	// t = 100 (k / 127)^2, k the value's integer part; their counters run on across blocks. At another rate the mixed
	// hold t + 1 keeps its length in time. SRR 0 passes the Track through. A Hit resets the holds where it sounds, and
	// that block passes as the DT2's does, whose trig clears the setting for its block
	class Dt2Srr
	{
	public:
		explicit Dt2Srr(double _rate);

		// The hold t at a value 0-127, in samples at 48 kHz
		static double hold(double _value);

		void set(int _lane, int _amountWord);
		void hit(const int _lane) { m_hit |= static_cast<uint16_t>(1 << _lane); }
		void process(Block& _buf, uint16_t _lanes = 0xffff);
		void clear(int _lane);

	private:
		double m_rate;
		int m_n[Lanes]{};				// the first hold's samples less one, -1 off
		float m_fraction[Lanes]{};
		float m_held[2][Lanes]{};
		int m_count[2][Lanes]{};
		uint16_t m_hit = 0;
	};
}
