// The Machinedrum's Rhythm Echo as a send effect, the mixer DSP's code at P:$342-$43B: two lines of 97,020 words read
// without interpolation at a pointer that glides towards its distance and a triangle moves, the left line's return and
// the right's scaled by LEV, and written back through FB (doubled, and limited as the DSP's stores limit it), MONO's
// cross-feed, a high-pass and a low-pass.
//
// The send and the return are float; the rest stays in the DSP's 24-bit words, sample for sample its own: FB above 64
// has a loop gain over 1, where any difference from the DSP's arithmetic doubles at each pass in what is not limited,
// so that a float loop drifts away from the oracle (measured at FB 127: worst 10 ms 50 dB, ticket 10), and these
// integers cost about what floats did.
//
// TIME is in tempo, and every length here in samples of 44.1 kHz: another rate is ticket 16's.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace md::fast
{
	using EchoWords = std::array<uint32_t, 9>;		// Y:$150-$158, 24 bits each

	class Echo
	{
	public:
		static constexpr int BlockSize = 32;

		// The lines are allocated here; nothing is in process
		Echo();

		// The words of the OS's tick, between two blocks
		void setWords(const EchoWords& _words);

		// Nothing left in the lines and the filters, so that nothing of what was sent comes back
		void clear();

		// The pointers at their target at once, for a return that was not running: they glide for seconds otherwise
		void settle()
		{
			m_distance = m_targetDistance;
			m_rate = m_targetRate;
		}

		// One block: the delay send in, the return out. _in and _out may not overlap
		void process(const float* _inL, const float* _inR, float* _outL, float* _outR);

		size_t lineBytes() const { return m_lines.size() * sizeof(int32_t); }

	private:
		void advancePointers();

		// The pointers, in the DSP's words
		int32_t m_targetDistance = 0x9c40;	// Y:$150
		int32_t m_targetRate = 0x4001;		// Y:$151
		int32_t m_lfoDepth = 0;				// Y:$152
		int32_t m_lfoStep = 0;				// Y:$153
		int32_t m_write = 0;				// Y:$159, from the line's first word
		int32_t m_distance = 0x9c40;		// Y:$15A
		int32_t m_rate = 0x4001;			// Y:$15B
		int32_t m_rateStep = 0;				// Y:$15D
		int32_t m_lfoPhase = 0;				// Y:$165
		std::array<int32_t, BlockSize> m_readAt{}, m_writeAt{};

		// The levels and the loop's filters, Y:$154-$158, and their state
		int32_t m_feedback = 0, m_highCut = 0, m_lowCut = 0, m_straight = 0, m_cross = 0, m_level = 0;
		int32_t m_highState[2]{}, m_highLast[2]{}, m_lowState[2]{};

		std::vector<int32_t> m_lines;
	};
}
