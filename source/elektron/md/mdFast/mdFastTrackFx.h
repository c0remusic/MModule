#pragma once

#include "stFastStages.h"

// The Machinedrum's per-Track chain (mdEngine's TrackFx, the mixer DSP's own code) in SIMD across the 16 Tracks: a
// class per stage, so that the slot chain of ticket 12 can place each alone. Samples are the DSP's words over 2^23.
// Parameters are its words too, a Kit value << 7 (0 to $3FFF), set between blocks; each stage starts where the DSP's
// boot does. A stage keeps the DSP's gain, so that the firmware's order sounds as the firmware does: the EQ halves, the
// filter quarters (and delays by 4 samples), DIST multiplies by 8 and more before it clips.
//
// The stages compute as the DSP does, in doubles: the same words, the same floors (the .cpp says why). On the DSP's
// words they equal TrackFx bit for bit. Nothing they hold can be a denormal. No stage allocates or locks in process.
namespace md::fast
{
	using syntakt::fast::Block;
	using syntakt::fast::BlockSize;
	using syntakt::fast::Lanes;

	// P:$d2-$133: a peaking biquad, EQF its frequency, EQG its gain: Kit value 63 is flat, and the boot's 64 already
	// 0.24 dB up. The coefficients jump between blocks, as on the DSP
	class Eq
	{
	public:
		Eq();
		void set(int _lane, int _eqf, int _eqg);
		void process(Block& _buf);

	private:
		alignas(16) double m_c[Lanes], m_r[Lanes], m_h[Lanes], m_e[Lanes], m_k[Lanes];
		alignas(16) double m_a[Lanes]{}, m_y1[Lanes]{}, m_y2[Lanes]{}, m_x1[Lanes]{}, m_x2[Lanes]{};
		int m_eqf[Lanes], m_eqg[Lanes];
	};

	// P:$134-$21e: two two-pole low-passes at FLTF + FLTW, then two two-pole high-passes at FLTF, 2 samples apart, with
	// the resonance FLTQ. The coefficients ramp over each block, n^2 / 1024 of the way at sample n, whichever way they
	// move: the OS's second, falling ramp (X:$668) is never taken, the word its test reads stays 0
	class Filter
	{
	public:
		Filter();
		void set(int _lane, int _fltf, int _fltw, int _fltq);
		void process(Block& _buf);

	private:
		// The DSP's words, over 2^23: the sections' 2 radius cos - 1 (the high-pass's half of it, radius cos - 1/2),
		// -radius^2 and numerator, and the high-pass's -2 h
		enum { LowC, LowR, LowH, HighC, HighR, HighH, HighE, WordCount };

		template<typename Words> void render(Block& _buf, int _l, const Words& _w);

		alignas(16) double m_word[WordCount][Lanes]{};		// as a block reads them, at the start of a move
		alignas(16) double m_to[WordCount][Lanes]{};		// where a move heads, before the limiter
		// The two low-passes, then the two high-passes
		alignas(16) double m_a[4][Lanes]{}, m_y1[4][Lanes]{}, m_y2[4][Lanes]{};
		alignas(16) double m_x1[2][Lanes]{}, m_x2[2][Lanes]{};		// the high-passes' inputs
		bool m_moving[Lanes / 2]{};
		int m_fltf[Lanes], m_fltw[Lanes], m_fltq[Lanes];
	};

	// P:$21f-$233: a sample-and-hold on an accumulator. _rate scales its step, so that a setting keeps its frequency:
	// 44.1 kHz at 0, 1.78 kHz at the top, whatever the rate. At another rate than the machine's, 0 is then no bypass
	class Srr
	{
	public:
		explicit Srr(double _rate = 44100);
		void set(int _lane, int _srr);
		void process(Block& _buf);

	private:
		alignas(16) float m_period[Lanes], m_phase[Lanes]{}, m_held[Lanes]{};
		float m_step;
	};

	// P:$234-$25d: a gain, the DSP's left shift of 9 and its limiter, then a DC blocker and an output gain. The shift
	// wraps in the DSP's 56-bit accumulator: a sample at half of full scale or more after the gain folds over to the
	// other rail
	class Dist
	{
	public:
		Dist();
		void set(int _lane, int _dist);
		void process(Block& _buf);

	private:
		alignas(16) double m_gain[Lanes], m_out[Lanes], m_outOld[Lanes]{};
		alignas(16) double m_a[Lanes]{}, m_y1[Lanes]{}, m_c1[Lanes]{};
	};
}
