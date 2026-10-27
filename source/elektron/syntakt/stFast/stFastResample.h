#pragma once

#include <cstdint>
#include <stdexcept>

#include "stFastStages.h"

namespace syntakt::fast
{
	// 48 kHz to 44.1 kHz (147/160) over the 16 lanes, for a machine that renders at its native 48 kHz: a polyphase
	// FIR, one Kaiser-windowed sinc (20 kHz, -6 dB there, beta 7) of 320 Latency + 1 taps at 147 x 48 kHz, read as
	// 147 phases of Taps taps, the fewest a sweep found to hold flat to 18 kHz with 22.05-24 kHz rejected by 60 dB
	// (40 taps and 19 kHz, 18 samples of delay, lost 1.7 dB at 18 kHz). Its delay is Latency host samples, whole:
	// output sample m stands for input time (m - Latency) 160 / 147. Measured by Q11 (stFastResampleTest): gain within
	// 0.1 dB 20 Hz-18 kHz, THD+N <= -70 dB 1-16 kHz, 22.05-24 kHz rejected by 60 dB; above 18 kHz it rolls off to its
	// cutoff. The input waits in a FIFO between the engine's 32-sample blocks and the host's
	class Resample48To44
	{
	public:
		static constexpr int Up = 147, Down = 160;
		static constexpr int Latency = 24;	// host samples
		static constexpr int Taps = 53;		// per phase
		static_assert((320 * Latency + Up) / Up == Taps, "the prototype's taps fill Taps phases");

		Resample48To44();

		// Silence before the first input, the FIFO empty
		void reset();

		// Whether the next host block needs more input than the FIFO holds: push engine blocks until it does not
		bool needsInput() const;
		void push(const Block& _in);

		// The next host block
		void pull(Block& _out);

	private:
		static int64_t inputOf(const int64_t _m) { return _m * Down / Up; }

		float m_h[Up][Taps];	// phase p's taps, m_h[p][k] weighing the input k samples before

		static constexpr int Capacity = Taps + 3 * BlockSize;
		alignas(16) float m_x[Capacity][Lanes];
		int64_t m_base = 0;		// the input sample m_x[0] holds
		int m_count = 0;
		int64_t m_out = 0;		// the next output sample
	};

	// A machine of syntakt::fast, which renders the Syntakt's own 48 kHz blocks (renderBlock), on a host's
	// blocks of 32 samples. At 48 kHz block for block. At 44.1 kHz through Resample48To44: before each host block the
	// FIFO takes the 48 kHz blocks it needs, 1 or 2 (34.8 samples on average), and what was queued before the host
	// block (a trig, a parameter, a note) lands on the first of them, so a trig falls on the machine's own 48 kHz block
	// grid, from 0.8 host samples early to 28.4 late (0.64 ms) against its ideal time, in a pattern that repeats every
	// 147 host blocks: for ticket 11 to settle. 88.2 and 96 kHz are ticket 16's: the machine at its 48 kHz, through
	// Resample48To44 at 88.2, and brought up 2x by a half-band filter (HIIR) at both. Other rates are refused
	template<typename Machine>
	class Host : public Machine
	{
	public:
		explicit Host(const float _sampleRate) : m_resample(supported(_sampleRate) == 44100.0f) {}

		// The host samples the output is delayed by
		int latency() const { return m_resample ? Resample48To44::Latency : 0; }

		// One host block: 32 samples into each of the 16 _outputs
		void render(float* const* _outputs)
		{
			const FlushDenormals ftz;
			const Block* out = &m_out;
			if(m_resample)
			{
				while(m_resampler.needsInput())
					m_resampler.push(Machine::renderBlock());
				m_resampler.pull(m_out);
			}
			else
			{
				out = &Machine::renderBlock();
			}
			for(int l = 0; l < Lanes; ++l)
				for(int n = 0; n < BlockSize; ++n)
					_outputs[l][n] = out->s[n][l];
		}

	private:
		using Machine::renderBlock;	// a Host renders through its FIFO

		static float supported(const float _sampleRate)
		{
			if(_sampleRate != 48000.0f && _sampleRate != 44100.0f)
				throw std::invalid_argument("the fast Syntakt runs at 48 or 44.1 kHz");
			return _sampleRate;
		}

		bool m_resample;
		Resample48To44 m_resampler;
		Block m_out;
	};
}
