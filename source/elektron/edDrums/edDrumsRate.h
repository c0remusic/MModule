// From a family's rate to the host's, and the sends' 44.1 kHz to it and back (ticket 16 of .scratch/elektron-drums-v1).
// Every family keeps its own rate: the Machinedrum's 44.1 kHz, the Syntakt's 48. At 44.1 kHz the engine is what it was:
// the Machinedrum's rings read as they are, the Syntakt's through Resample48To44. At any other host rate a ring is read
// through a RateFir, as the Syntakt's is read at 44.1 kHz: a polyphase FIR, each Track at a phase of its own which a
// Hit sets, so that a Hit sounds the same samples wherever its note falls. Nothing here allocates after its constructor.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "edDrumsSends.h"
#include "elektronBlock.h"

namespace elektron::drums
{
	// The host rates the engine plays at: 44.1, 48, 88.2, 96 and 192 kHz
	bool playableRate(double _rate);

	// _from to _to, two whole rates in Hz: host sample m stands for source time m down() / up(). A sinc at _cutoff (-6 dB
	// there) under a Kaiser window (_beta), linear phase, each phase's gain at 0 Hz 1. By default 20.5 kHz and beta 8:
	// flat within 0.002 dB to 18 kHz, what lies past 23.6 kHz down by 80 dB (edDrumsRateTest measures it). taps() source
	// samples a phase, the newest the one a host sample reads up to
	class RateFir
	{
	public:
		static constexpr int Taps = 48;		// a ring's read: a window of the ring is a row (Ring::Mirror)

		static constexpr double Cutoff = 20500, Beta = 8;

		RateFir(double _from, double _to, int _taps = Taps, double _cutoff = Cutoff, double _beta = Beta);

		int up() const { return m_up; }
		int down() const { return m_down; }
		int taps() const { return m_taps; }
		// The FIR's delay in 1/up() of a source sample, half its length: the output at q / up() stands for source time
		// (q - delay()) / up()
		int delay() const { return m_up * m_taps / 2; }

		// Phase _p's taps, oldest input first: row(p)[taps() - 1 - k] weighs the input k samples before the newest
		const float* row(const int _p) const { return m_table.data() + static_cast<size_t>(_p) * static_cast<size_t>(m_taps); }
		// Per phase, the one tap of a row that is the source's sample itself (1, the others 0), -1 for a row of a sum
		const int8_t* units() const { return m_unit.data(); }

	private:
		int m_up, m_down, m_taps;
		std::vector<float> m_table;		// rows of a multiple of 4 taps, 16-byte aligned as the heap's blocks are
		std::vector<int8_t> m_unit;
	};

	// The FIRs' kernels in AVX2 and FMA (edDrumsRateAvx2.cpp), away from 44.1 kHz, whose SSE they would not match bit for bit
	namespace rate
	{
		// Whether the CPU runs them and its OS saves their registers; decided once
		bool fastFir();
		// _n host samples of a ring of _mask + 1 samples (and a mirror of a window past it) read through _table's rows
		// (_unit's where a half-band's phase is a sample alone) from its sample _i at phase _p, added to _out
		void readFir(const float* _ring, int _mask, int64_t _i, int _p, int _up, int _down, const float* _table,
			const int8_t* _unit, float* _out, int _n);
		// 32 frames of 8 channels from _first on through _fir, _x's frame 0 holding input _base: RateStream::pull's
		void frames8(const float* _x, int64_t _first, int64_t _base, int _up, int _down, const RateFir& _fir, float* _out);
	}

	// Eight channels through a RateFir in blocks of 32 each way, as Resample48To44 streams the Syntakt's lanes: the input
	// waits in a FIFO until an output block can be read. _preroll source samples of silence come before the first input
	class RateStream
	{
	public:
		static constexpr int Channels = 8;
		using Frame = std::array<float, Channels>;

		RateStream(double _from, double _to, int _taps, int _preroll, double _cutoff = RateFir::Cutoff,
			double _beta = RateFir::Beta);

		// Whether the next output block needs input the FIFO does not hold yet
		bool needsInput() const;
		void push(const Frame* _in);		// 32 frames
		void pull(Frame* _out);				// 32 frames
		const RateFir& fir() const { return m_fir; }

	private:
		int64_t inputOf(const int64_t _m) const { return _m * m_fir.down() / m_fir.up(); }

		RateFir m_fir;
		std::vector<Frame> m_x;
		int64_t m_base;		// the input m_x[0] holds
		int m_count;
		int64_t m_out = 0;	// the next output
	};

	// The sends at their 44.1 kHz on a host at another rate: what the Tracks send at the host's rate goes down to 44.1
	// kHz, through Sends' blocks of 32, and its return comes back up, returnDelay() host samples after it went in. The
	// calls of Sends that the engine mixes through, at the host's blocks, of _host samples. Above 48 kHz the way down counts
	// on what is sent being the rings' FIRs' band, under 20.5 kHz (edDrumsRate.cpp, downOf)
	class SendsAtRate
	{
	public:
		static constexpr int MaxHost = 4 * BlockSize;

		// _sends outlives this. _host a multiple of 32, MaxHost at most
		SendsAtRate(Sends& _sends, double _rate, int _host = BlockSize);

		float* input(const Send _send, const int _channel, const int _at)
		{
			m_fed = true;
			return &m_in[static_cast<size_t>(_send)][static_cast<size_t>(_channel)][static_cast<size_t>(_at)];
		}
		const float* output(const Send _send, const int _channel, const int _at) const
		{
			return &m_out[static_cast<size_t>(_send)][static_cast<size_t>(_channel)][static_cast<size_t>(_at)];
		}
		int output(const Send _send) const { return m_sends.output(_send); }
		bool sounding() const { return m_awake; }

		// The host block just mixed goes in, the next one's return comes out
		void block();

		const RateFir& downFir() const { return m_down.fir(); }

		// From the host's input to its return, in host samples: the FIRs', the sends' block of 32 at 44.1 kHz, the host's
		// block that the return waits for. 44.1 kHz's is 32, the sends' block alone
		double returnDelay() const;

	private:
		using Block = std::array<std::array<std::array<float, MaxHost>, 2>, SendCount>;

		// Host blocks of nothing in and nothing out of the sends before the streams stop: what they held has gone through
		static constexpr int DrainBlocks = 64;

		// 32 samples of the host block from _from down to the sends, and every sends' block that makes up
		void goDown(int _from);

		Sends& m_sends;
		const int64_t m_rate;
		const int m_host;
		const int m_preroll;		// up's silence ahead, in 44.1 kHz samples
		RateStream m_down, m_up;
		Block m_in{}, m_out{};
		int m_quiet = DrainBlocks;
		int64_t m_sleep = 0;	// the sends' time asleep past their last block, in 1/rate of a 44.1 kHz sample
		bool m_fed = false, m_awake = false;
		bool m_feeding = false;		// a host block was fed since the sends' last block
	};
}
