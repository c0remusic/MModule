// The Syntakt's send reverb in float, what CPU #1 runs at 0x40096e3c (stEngine's main::reverb): the mono send through a
// pre-delay, a low-pass and a high-pass, eight allpasses in series, then two networks of four lines each, which take
// the diffused send on their first line: each line through a shelf, the last read between samples at a position a 0.65
// Hz sine moves, the four fed back by a sign matrix whose gains DEC sets. The stereo return takes lines of each network
// crossed. The delay's return comes into the send by its REV, as on the machine. stFastSendFxTest compares it with the
// exact port at 48 kHz, Q2 of research/19-qualite-audio.md section 11.
//
// The lengths and the poles are in seconds and Hz, so that the rate given is the reverb's own: at 48 kHz the machine's,
// at 44.1 kHz the same reverb, without a resampler on its send and its return. Nothing of the firmware is read: the
// lengths, the diffusers' gains and the sine were measured on responses (stFastReverb.cpp).
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace syntakt::fast
{
	// The machine's smoothed words (the engine copy's +0x6fa): value << 8, its fine part below
	struct ReverbWords
	{
		int preDelay = 0, decay = 0, frequency = 0, gain = 0, highPass = 0, lowPass = 0;
	};

	class Reverb
	{
	public:
		static constexpr int BlockSize = 32;

		// The lines are allocated here; nothing is in process
		explicit Reverb(double _rate);

		// Between two blocks
		void setWords(const ReverbWords& _words) { m_words = _words; }

		// The delay's REV word (the engine copy's +0x6f2): how much of _delayL and _delayR comes in, squared, ramped
		// over the block. 0 for a delay's return in parallel
		void setDelaySend(const int _word)
		{
			const float v = static_cast<float>(_word) / 32768;
			m_sendTarget = v * v;
		}

		// Nothing left in the lines and the filters, so that nothing of what was sent comes back
		void clear();

		// One block, after the delay's: the send and the delay's return in, the return out. _in and _out may not
		// overlap
		void process(const float* _inL, const float* _inR, const float* _delayL, const float* _delayR, float* _outL,
			float* _outR);

	private:
		static constexpr int Diffusers = 8, Lines = 8;

		struct Shelf
		{
			float c0, c1, c2;
		};

		struct HalfState
		{
			float first = 0, second = 0, firstOut = 0, secondOut = 0;	// the two shelves shelve() runs
			float third = 0, tap = 0, thirdOut = 0, tapOut = 0;			// the other two
		};

		void readLine(int _line, float* _to) const;
		void readModulated(int _line, double _delay, float* _to) const;
		void half(const float* _diffused, int _firstLine, const float* _matrix, const Shelf& _shelf, double _delay,
			HalfState& _state, float* _outL, float* _outR, bool _adding);

		const double m_scale;			// of the machine's samples
		const double m_sineRate;
		const std::array<float, 1024> m_poles;
		const std::array<float, 128> m_shelves, m_diffusion;
		const std::array<std::array<float, 8>, 128> m_matrix;
		std::array<int, Diffusers> m_diffuserLength;
		std::array<float, Diffusers> m_diffuserGain;
		std::array<int, Lines> m_lineLength;

		ReverbWords m_words;
		float m_sendTarget = 0, m_send = 0;
		int m_preDelay = 0x25;			// last block's PRE, which this block reads by, in the machine's samples

		std::vector<float> m_preLine, m_diffuserLines, m_lines;		// the last two side by side
		uint32_t m_preMask, m_diffuserMask, m_lineMask;
		uint32_t m_write = 0;			// the same for all: each wraps by its mask

		float m_low = 0, m_high = 0;
		std::array<HalfState, 2> m_half;
		float m_outs[4][BlockSize] = {};	// the half's four filtered lines, the machine's buffer: see half()
		float m_input[Lines][BlockSize] = {};
		double m_sine = 0, m_cosine = -1;
	};
}
