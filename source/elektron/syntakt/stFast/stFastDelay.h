// The Syntakt's send delay in float, what CPU #1 runs at 0x40096890 (stEngine's main::delay and prepareEffects): a
// stereo ring read at two taps, the tempo's time eased over about 256 blocks and last block's, crossfaded over the
// block so that a change of time glides without a click; the sum soft-clipped (y = 2 x - x |x|), a low-pass and a
// high-pass; the output, and in place of it the ring's next input: FDBK of it, L and R crossed with X, plus the send at
// the widths WID sets. stFastSendFxTest compares it with the exact port at 48 kHz, Q2 of research/19-qualite-audio.md
// section 11; over FDBK 64 the loop grows, and that test says what holds there.
//
// The time and the poles are in seconds and Hz, so that the rate given is the delay's own: at 48 kHz the machine's, at
// 44.1 kHz the same delay, without a resampler on its send and its return. Nothing of the firmware is read.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace syntakt::fast
{
	// The machine's smoothed words (the engine copy's +0x6e6): value << 8, its fine part below
	struct DelayWords
	{
		int time = 0, cross = 0, width = 0, feedback = 0, highPass = 0, lowPass = 0;
	};

	class Delay
	{
	public:
		static constexpr int BlockSize = 32;

		// The ring is allocated here; nothing is in process
		explicit Delay(double _rate);

		// Between two blocks
		void setWords(const DelayWords& _words) { m_words = _words; }
		void setTempo(const double _bpm) { m_tempo = static_cast<int32_t>(_bpm * 120); }

		// A time of 1 / _hz seconds in place of TIME's and the tempo's, eased the same, held to a block and the longest
		// time; 0 gives TIME back
		void setFree(double _hz);

		// The eased time at its target at once, for a delay that has not been running: a time left from before would
		// glide from there, and a first echo come early and pitched
		void settle();

		// Nothing left in the ring and the filters, so that nothing of what was sent comes back
		void clear();

		// One block: the send in, the return out. _in and _out may not overlap
		void process(const float* _inL, const float* _inR, float* _outL, float* _outR);

	private:
		uint32_t targetFrames() const;
		uint32_t nextFrames();

		const int m_rate;
		const int m_ease;				// of 65536 of the last time, per block: 256 at 48 kHz
		const uint32_t m_longest;		// the longest time, frames
		const std::array<float, 1024> m_poles;
		DelayWords m_words;
		int32_t m_tempo = 120 * 120;	// BPM x 120, as the machine's
		uint32_t m_free = 0;			// a free time, frames; 0: TIME's

		std::vector<float> m_ring;		// L, R
		uint32_t m_mask;				// of the frames
		uint32_t m_write = 0;
		uint32_t m_time = 32;			// the eased time, frames << 5: a frame at the start, as the exact port's
		uint32_t m_last = 0;			// last block's time, frames
		float m_fade = 0;				// the last block's tap's weight as this block starts
		float m_feedback = 0;
		float m_gains[4] = {};			// left from L, left from R, right from L, right from R
		float m_low[2] = {}, m_high[2] = {};
	};
}
