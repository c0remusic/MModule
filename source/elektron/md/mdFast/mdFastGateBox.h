// The Machinedrum's Gate Box reverb as a send effect, the mixer DSP's code at P:$43C-$66B in float: a stereo predelay,
// the mono sum through five Schroeder allpasses, a network of six delay lines of prime lengths (four through a
// one-pole, two read with a linear interpolation a very slow sine moves, the same one-pole after it) whose return
// passes a gate that an envelope of the diffused input opens, then a high-pass, a low-pass and the level. The Echo's
// return feeds the send with DVOL, as on the machine; on a return in parallel, _echoL and _echoR at zero or DVOL at 0
// leaves it out.
//
// Its two tables have formulas: a row of the decay's gains is a reverberation time, 0.4 to 4 s in equal ratios, and a
// row of the damping's a pole. Every length is in samples of 44.1 kHz: another rate is ticket 16's.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace md::fast
{
	using GateBoxWords = std::array<uint32_t, 8>;	// Y:$185-$18C

	class GateBox
	{
	public:
		static constexpr int BlockSize = 32;
		static constexpr int Lines = 6;
		static constexpr int Rows = 64;

		// Y:$1439C0 and Y:$143B40, computed: 24-bit words, a row of six per line
		using Table = std::array<std::array<uint32_t, Lines>, Rows>;
		struct Tables
		{
			Table decay;	// an echo's gain over each line for a reverberation time, of 1 / sqrt 2
			Table damping;	// the line's one-pole
		};
		static const Tables& tables();

		// The delay lines are allocated here; nothing is in process. _sine: the DSP's 32,768 words of a period, the
		// caller's (md::voice::TrxTables::sine holds the same words)
		explicit GateBox(const uint32_t* _sine);

		// The words of the OS's tick, between two blocks
		void setWords(const GateBoxWords& _words);

		// One block: the reverb send and the Echo's return in, the return out. _in and _out may not overlap
		void process(const float* _inL, const float* _inR, const float* _echoL, const float* _echoR, float* _outL,
			float* _outR);

		size_t lineBytes() const { return m_memory.size() * sizeof(float); }

	private:
		using Samples = std::array<float, BlockSize>;

		struct Line
		{
			float* words = nullptr;
			int size = 0, read = 0, write = 0;
		};

		void predelay(const float* _inL, const float* _inR, const float* _echoL, const float* _echoR, Samples& _mono);
		void diffuse(Samples& _mono);
		void readLines(std::array<Samples, Lines>& _reads);
		void writeLines(const std::array<Samples, Lines>& _reads, const Samples& _mono);
		void gate(const Samples& _mono, Samples& _gains);

		// The words as fractions, and the rows they name
		float m_highCut = 0, m_lowCut = 0, m_level = 0, m_echoSend = 0;
		std::array<float, 5> m_allpassGain{};
		std::array<float, Lines> m_decay{}, m_damping{};
		int32_t m_gateHold = 0;				// Y:$18A
		int m_predelay = 0;					// Y:$18C / 2, in stereo frames

		const uint32_t* m_sine;

		// Y:$143979 and the ring from $134000
		int m_ringWrite = 0;
		float* m_ring = nullptr;

		// The five allpasses from $1319C0, on the buffers and the state their loop keeps
		std::array<Line, 5> m_allpass;
		std::array<float, 5> m_allpassState{};

		std::array<Line, Lines> m_lines;
		std::array<float, Lines> m_lowState{};	// Y:$1439B4-$1439B9
		std::array<float, 4> m_readHeld{};		// Y:$1439BA-$1439BD: the sample each line read last
		uint32_t m_sinePhase = 0;				// Y:$1439B3

		int32_t m_envelope = 0, m_peak = 0;		// Y:$143987, Y:$143988
		int32_t m_gateCount = 0;				// Y:$143989
		int32_t m_gateGain = 0;					// Y:$14398A

		std::array<float, 2> m_highState{}, m_highLast{}, m_outState{};

		std::vector<float> m_memory;
	};
}
