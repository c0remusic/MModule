#pragma once

#include <cstdint>

#include "stFastStages.h"

namespace syntakt::fast
{
	// The stages BD MODERN alone uses (the shared ones: stFastStages.h)

	// _out = a + b, clipped to [-1, 1]
	void sum(const Block& _a, const Block& _b, Block& _out);

	// A 256-entry wave (257 values, linearly interpolated) phase-modulated by _src
	void pmWave(Block& _out, const Block& _src, uint32_t* _phase, const Increments& _inc, float _depth,
		const float* _wave);

	// The click: a ramp from 1 down by _dec per sample, held at 0, times a level ramped from _from to _to
	void click(Block& _out, float* _ramp, float _dec, const float* _from, const float* _to);

	// BD MODERN on 16 Tracks: a sine at the note phase-modulated by an enveloped mix of two oscillators an octave up,
	// under a pitch sweep, with a filtered click, a drive stage and the amp envelope (.scratch/elektron-engines/
	// bdmodern.md). The same voice as syntakt::bdModern, block for block: a trig is seen in its block (the Track fades
	// out) and sounds from the next; a parameter or note takes effect at the next block. It renders the machine's own
	// 48 kHz blocks, its block-rate control (the sweep, the envelopes) stepped once per block (Host: on a host's)
	class BdModern
	{
	public:
		static constexpr int Parameters = 8;

		// Its tables come from stFastTables' formulas: nothing of the firmware
		BdModern();

		// _track 0-15. The note in semitones; parameter _index 0-7 as VoiceCpu's Machine page (TUNE, the U2 level,
		// the transient, SWEEP, CONTOUR, the drive switch, HOLD, DECAY), _value 0-127
		void setNote(int _track, int _note);
		void setParameter(int _track, int _index, uint8_t _value, uint8_t _fine);

		// For the next block rendered only
		void trig(int _track);
		void gateOff(int _track);

		// For tests: unit 0's feedback table at 0, the voice without its chaos
		void testWithoutChaos();

		// One 48 kHz block of the 16 Tracks
		const Block& renderBlock();

	private:
		void convert(int _track);
		void control();

		// The tables BD MODERN reads (stFastTables, section 7's words by formula): the 128-entry parameter tables and
		// unit 2's wave as floats, the pitch's as words (the sweep's, the pitch map's exp2), which the machine
		// computes in integers; the 256-entry sine unit 0 reads is computed
		struct Tables
		{
			int32_t sweepDepth[128], sweepDecay[128];
			float clickLevel[128], modDecay[128], ampDecay[128];
			float modTarget[128], feedback[128], levelU0[128], levelU2[128];
			float wave[258];	// 257 and a copy of the last, for a phase that rounds up to 1
			int32_t exp2[2048];
			float sine[258];	// computed: sin(2 pi i / 256)
		};
		Tables m_tables;

		float driveStep(int _l);

		// The click's step per sample and its filters
		float m_clickDec = 0;
		OnePole m_hp{}, m_lp{};

		// Each Track's parameters, as words (value << 8 | fine), and note
		int32_t m_word[Parameters][Lanes]{};
		int m_note[Lanes]{};

		// What they convert to (convert): read every block, or latched at a trig (*Next)
		int32_t m_pitch[Lanes]{};		// the pitch map's word for the note and TUNE (pitchWord)
		int32_t m_sweepDepth[Lanes]{};	// the sweep's depth word
		double m_sweepDecayNext[Lanes]{};
		float m_modDecayNext[Lanes]{};
		bool m_driveNext[Lanes]{};
		alignas(16) float m_g0[Lanes]{}, m_g1[Lanes]{}, m_clickTarget[Lanes]{}, m_fbTarget[Lanes]{};
		float m_modTarget[Lanes]{}, m_ampDecay[Lanes]{};
		bool m_hold[Lanes]{};

		// The voices' state
		bool m_trigQueued[Lanes]{}, m_gateQueued[Lanes]{};
		bool m_trigPending[Lanes]{}, m_trigNow[Lanes]{}, m_gateOff[Lanes]{};
		alignas(16) uint32_t m_phase[4][Lanes]{};
		Increments m_inc3{}, m_inc012{};	// unit 3's, and units 0-2's
		alignas(16) float m_fb[Lanes]{}, m_last[Lanes]{};
		alignas(16) float m_g0Cur[Lanes]{}, m_g1Cur[Lanes]{};
		// The block-rate control runs on the machine's 48 kHz block clock: the blocks since the trig and the envelopes'
		// block
		int64_t m_at[Lanes]{}, m_envBlock[Lanes]{};
		double m_sweepDecay[Lanes]{}, m_sweep[Lanes]{};	// the sweep at its block
		Ramp48 m_u2, m_mod, m_amp, m_driveGain;
		int m_modStage[Lanes]{}, m_ampStage[Lanes]{}, m_driveStage[Lanes]{};
		float m_modDecay[Lanes]{};
		float m_driveX[Lanes]{};
		bool m_drive[Lanes]{}, m_release[Lanes]{};

		// The envelopes at the ends of the previous and this block, which the stages ramp between
		alignas(16) float m_u2Level[Lanes]{}, m_u2Prev[Lanes]{};
		alignas(16) float m_modLevel[Lanes]{}, m_modPrev[Lanes]{};
		alignas(16) float m_ampLevel[Lanes]{}, m_ampPrev[Lanes]{};
		alignas(16) float m_driveFrom[Lanes]{}, m_driveTo[Lanes]{}, m_driveAmount[Lanes]{}, m_drivePost[Lanes]{};

		alignas(16) float m_clickRamp[Lanes]{}, m_clickCur[Lanes]{};
		alignas(16) float m_hpX[Lanes]{}, m_hpY[Lanes]{}, m_lpX[Lanes]{}, m_lpY[Lanes]{};

		Block m_b0, m_b1, m_a2, m_mix, m_b3, m_click;
	};
}
