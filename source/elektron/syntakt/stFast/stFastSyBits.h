#pragma once

#include <cstdint>

#include "stFastStages.h"

namespace syntakt::fast
{
	// SY BITS's waves (stFastTables::syBitsWave): band-limited saws 0-7 (the note's band, low to high), the triangle 8,
	// the sine 9; 512 entries and the first again (513 floats)
	constexpr int SyBitsWaves = 10, SyBitsWaveSize = 513;

	// SY BITS's own stages, public for its Q3

	// 0x400061a2 on 16 lanes: two waves read at _phase (2^-32 cycles, _inc a sample; left as it is) crossfaded, their
	// weights ramped from _from to _to (the first wave's at [l], the second's at [Lanes + l]); _wave1, _wave2 the
	// lanes' waves
	void crossfadeWaves(Block& _out, const uint32_t* _phase, const uint32_t* _inc, const float* const* _wave1,
		const float* const* _wave2, const float* _from, const float* _to);

	// 0x40006346 on 16 lanes, added to _out saturated: a wave read at _phase (left as it is) plus an offset ramped from
	// _offsetFrom by _offsetStep a sample (2^-32 cycles, wrapping), under a level ramped from _from to _to, negated
	void addOffsetWave(Block& _out, const uint32_t* _phase, const uint32_t* _inc, const float* const* _wave,
		const uint32_t* _offsetFrom, const uint32_t* _offsetStep, const float* _from, const float* _to);

	// 0x40005f9a on 16 lanes: each sample truncated to a multiple of 2^(b - 31) and of 2^(b - 30), crossfaded by the
	// fraction; _word b in Q16 per lane
	void reduceBits(Block& _buf, const int32_t* _word);

	// SY BITS on 16 Tracks: two pairs of oscillators at the note and at the note detuned, each a crossfade of two waves
	// (the sine, the triangle, the note's band-limited saw) plus, from WAVE 43, the saw at a phase offset subtracted;
	// the pairs mixed, gated noise mixed in, a bit reducer, a sample-rate reducer and the amp envelope. The same voice
	// as syntakt::syBits, block for block: a trig is seen in its block (the Track fades out) and sounds from the next; a
	// parameter or note takes effect at the next block. It renders the machine's own 48 kHz blocks, its block-rate
	// control stepped once per block (Host: on a host's)
	class SyBits
	{
	public:
		static constexpr int Parameters = Controls::Parameters;
		static constexpr int Units = 4;

		// Its tables come from stFastTables' formulas: nothing of the firmware
		SyBits();

		// _track 0-15. The note in semitones; parameter _index 0-7 as VoiceCpu's Machine page: TUNE, DETUNE (units 2
		// and 3), the mix of the pairs and the noise, the sample-rate reducer, WAVE, BITS, HOLD, DECAY; _value 0-127
		void setNote(int _track, int _note);
		void setParameter(int _track, int _index, uint8_t _value, uint8_t _fine);

		// For the next block rendered only
		void trig(const int _track) { m_controls.trig(_track); }
		void gateOff(const int _track) { m_controls.gateOff(_track); }

		// SY BITS has no feedback: nothing to take out
		void testWithoutChaos() {}

		// For tests, before _track's first trig: its noise from the six words the exact port's machine change draws
		// (0x570-0x584, bench::exactNoiseSeeds), of which the noise reads the last four
		void testSeedNoise(const int _track, const int32_t* _drawn) { m_noise.seed(_track, _drawn + 2); }

		// For tests: _track's words as the last block left them, as the voice holds them (Fields in its test)
		void testWords(int _track, int32_t* _words) const;

		// One 48 kHz block of the 16 Tracks
		const Block& renderBlock();

	private:
		void convert(int _track);
		void control();

		// The tables SY BITS reads (stFastTables, section 7's words by formula): as words, the amp's decay as floats
		struct Tables
		{
			int32_t phaseOffset[128], bits[128], sampleRate[128], crossfade[128], gain[3][128];
			float ampDecay[128];
			int32_t exp2[2048];
			float wave[SyBitsWaves][SyBitsWaveSize];
		};
		Tables m_tables;

		Controls m_controls;

		// What they convert to (convert): the pairs' increments (0x78, 0x178: units 1 and 3 the same), the waves each
		// unit reads (0xb0, 0xb4 of units 0 and 2, 0xb0 of 1 and 3: SyBitsWaves indices), whether units 1 and 3 take
		// theirs (WAVE from 22), the crossfade (0xa4), the offset (0xf0), units 1 and 3's level (0xfc), the mix's
		// levels (0x08, 0x0c, 0x14), the rate (0x59c) and bits (0x5a0) words, the amp's decay and HOLD
		int32_t m_incWord[2][Lanes]{}, m_waveNext[6][Lanes]{};
		bool m_unitWaves[Lanes]{};
		int32_t m_fade[Lanes]{}, m_offset[Lanes]{}, m_levelWord[Lanes]{}, m_gainWord[3][Lanes]{};
		alignas(16) int32_t m_rate[Lanes]{}, m_bits[Lanes]{};
		float m_ampDecay[Lanes]{};
		bool m_hold[Lanes]{};

		// The voices' state
		bool m_playing[Lanes]{};	// since its first trig
		alignas(16) uint32_t m_phase[2][Lanes]{}, m_inc[2][Lanes]{};
		int32_t m_wave[6][Lanes]{};				// -1: the RAM sine the machine change leaves, at level 0
		int32_t m_weight[2][2][Lanes]{};		// the pairs' crossfade weights (0xa8, 0xac) at their targets
		int32_t m_offsetFrom[Lanes]{}, m_levelFrom[Lanes]{}, m_gainFrom[3][Lanes]{};
		alignas(16) uint32_t m_offsetStart[Lanes]{}, m_offsetStep[Lanes]{};
		alignas(16) float m_wFrom[2][2][Lanes]{}, m_wTo[2][2][Lanes]{}, m_lFrom[Lanes]{}, m_lTo[Lanes]{};
		alignas(16) float m_gFrom[3][Lanes]{}, m_gTo[3][Lanes]{}, m_wavesLevel[Lanes]{};
		alignas(16) float m_held[2][Lanes]{};
		alignas(16) int32_t m_heldCount[2][Lanes]{};

		AmpEnvelope m_amp;
		NoiseGenerator m_noise;

		// The amp's envelope over the block, sample by sample, and at its end
		Block m_gAmp;
		float m_ampEnd[Lanes]{};

		Block m_pair[2], m_noiseOut, m_out;
	};
}
