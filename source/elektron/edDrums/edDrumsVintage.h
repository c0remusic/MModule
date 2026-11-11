#pragma once

#include <cstdint>

#include "elektronBlock.h"

// The vintage PCM character (tickets 12 and 20 of .scratch/elektron-drums-v1): what the 8-bit drum machines of the early
// 1980s did to their samples, as their public descriptions and their DACs' datasheets tell it (the Oberheim DMX and the
// LinnDrum: a companding 8-bit DAC of the AM6070 / mu-law kind, pitch by the playback clock with no interpolation, an
// analogue reconstruction low-pass after the DAC). Our own model: no machine's samples, words or tables.
//
// Three pieces, which the Sampler's VINTAGE grain (Player) and the chain's DAC drive (Dac) share: a DAC of 6 to 12 bits,
// linear or companded; a hold, each sample the DAC takes kept until the clock's next tick, so that what lies above half
// the clock folds back down, which is the sound; a two-pole low-pass after it.
namespace elektron::drums::vintage
{
	constexpr int MinBits = 6, MaxBits = 12;

	// The segment law, of the mu-law kind (G.711's shape at any width): a sign, 3 bits of segment and _bits - 4 of step.
	// Segment s holds 2^(_bits - 4) steps of 2^s units; magnitude code k = s 2^M + q (M = _bits - 4) decodes to
	// (2^M + q) 2^s - 2^M units, full scale at the top code. compandCode is the code whose value is nearest _magnitude
	// (0 to 1), compandValue its value
	int compandCode(float _magnitude, int _bits);
	float compandValue(int _code, int _bits);

	// _x (full scale 1) through a DAC of _bits: the nearest of its values, the linear one's two's complement in steps of
	// 2^(1 - _bits), the companded one's the segment law's either side of 0
	float dac(float _x, int _bits, bool _companding);

	// A two-pole low-pass of Q 1/sqrt 2 (a trapezoidal state-variable filter, its frequency prewarped): -3 dB at its cutoff
	class LowPass
	{
	public:
		void set(double _hz, double _rate);
		float process(float _x)
		{
			const float v3 = _x - m_ic2;
			const float v1 = m_a1 * m_ic1 + m_a2 * v3;
			const float v2 = m_ic2 + m_a2 * m_ic1 + m_a3 * v3;
			m_ic1 = 2 * v1 - m_ic1;
			m_ic2 = 2 * v2 - m_ic2;
			return v2;
		}
		void clear() { m_ic1 = m_ic2 = 0; }

	private:
		float m_a1 = 0, m_a2 = 0, m_a3 = 0, m_ic1 = 0, m_ic2 = 0;
	};

	// The Sampler's grain, Machine 9 of the Sampler: under 64 the ROM player's (MD), from 64 VINTAGE. Machine 10 BITS,
	// 11 COMPANDING (on from 64), 12 OUTPUT FILTER
	constexpr int GrainSlot = 8, BitsSlot = 9, CompandingSlot = 10, FilterSlot = 11;
	constexpr int SamplerParameters = 12;
	inline bool isVintage(const int _grain) { return _grain >= 64; }
	inline bool companding(const int _value) { return _value >= 64; }
	// BITS: 6 to 12, about 18 values each
	inline int bitsOf(const int _value) { return MinBits + (_value < 0 ? 0 : _value > 127 ? 127 : _value) * 7 / 128; }
	// OUTPUT FILTER: 100 Hz to 20 kHz in equal ratios
	double filterHz(double _value);

	// The grain's own laws of the ROM player's knobs (PTCH, DEC, HOLD), its knobs' words over 128 (0 to 127, fractions):
	// PTCH +-24 semitones about 64, 3/8 of one a value; HOLD up to 2 s at full level before the decay; DEC a fall of 60 dB
	// in 20 ms to 10 s, none at 127
	double semitones(double _ptch);
	double holdSeconds(double _hold);
	double decaySeconds(double _dec);	// infinite at 127

	// The DAC drive by its amount, 0 to 127 (fractions): 12 bits linear at 0, a bit fewer each 21 values down to 6 and
	// companded above 0; its clock from the chain's rate at 0 down to 4 kHz at 127 in equal ratios; its low-pass at 0.45
	// of the clock, none at 0: at its neutral setting a 12-bit DAC at the chain's rate, nothing else
	struct DriveSetting
	{
		int bits = MaxBits;
		bool companding = false;
		double clockHz = 0;
		double cutoffHz = 0;		// 0: no filter
	};
	DriveSetting driveSetting(double _amount, double _rate);

	// The chain's DAC drive, a Track a lane, as md::fast's stages are: set between blocks, process on the lanes of _lanes
	// alone, clear a lane back to silence
	class Dac
	{
	public:
		explicit Dac(double _rate);
		void set(int _lane, int _amountWord);	// a Kit value << 7, an LFO's move in it
		void process(Block& _buf, uint16_t _lanes = 0xffff);
		void clear(int _lane);

	private:
		double m_rate;
		int m_bits[Lanes];
		bool m_companding[Lanes]{}, m_filtered[Lanes]{};
		float m_step[Lanes];		// the clock's ticks a sample, at most 1
		float m_phase[Lanes], m_held[Lanes]{};
		LowPass m_filter[Lanes];
	};

	// The VINTAGE grain's voice: a Track's 16-bit sample from _begin to _end, read at a clock that its pitch sets with no
	// interpolation (each sample held until the clock's next tick), through the DAC, then the low-pass, then the hold and
	// decay of its level. Renders at the host's 44.1 kHz
	class Player
	{
	public:
		static constexpr float Silence = 1e-5f;		// the level it stops at, -100 dB

		void trig(const int16_t* _pcm, uint32_t _begin, uint32_t _end, double _holdSamples);
		void stop() { m_playing = false; }
		// Source samples a host sample
		void setStep(const double _step) { m_step = _step; }
		void setDac(const int _bits, const bool _companding) { m_bits = _bits; m_companding = _companding; }
		void setFilter(double _hz, double _rate);	// 0: none
		void setDecay(double _seconds, double _rate);	// infinite: none
		// _count samples added to _out
		void render(float* _out, int _count);
		bool playing() const { return m_playing; }

	private:
		const int16_t* m_pcm = nullptr;
		uint32_t m_end = 0;
		uint32_t m_index = 0;		// the sample held
		double m_position = 0;		// past it, in source samples
		double m_step = 1;
		double m_hold = 0;			// host samples left at full level
		double m_hz = -1;			// the filter's cutoff, 0 none
		float m_level = 0, m_fall = 1;
		float m_held = 0;
		int m_bits = 8;
		bool m_companding = true, m_filtered = false, m_playing = false;
		LowPass m_filter;
	};
}
