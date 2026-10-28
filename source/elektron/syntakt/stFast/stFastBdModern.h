#pragma once

#include <cstdint>

#include "stFastStages.h"

namespace syntakt::fast
{
	// The stages BD MODERN alone uses (the shared ones: stFastStages.h)

	// _out = a + b, clipped to [-1, 1]
	void sum(const Block& _a, const Block& _b, Block& _out);

	// A 256-entry wave (257 values, linearly interpolated) phase-modulated by _src at a fixed depth (the shared pmWave
	// ramps its depth)
	void pmWaveBd(Block& _out, const Block& _src, uint32_t* _phase, const Increments& _inc, float _depth,
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
		static constexpr int Parameters = Controls::Parameters;
		static constexpr int Units = 4;

		// Its tables come from stFastTables' formulas: nothing of the firmware
		BdModern();

		// _track 0-15. The note in semitones; parameter _index 0-7 as VoiceCpu's Machine page (TUNE, the U2 level,
		// the transient, SWEEP, CONTOUR, the drive switch, HOLD, DECAY), _value 0-127
		void setNote(int _track, int _note);
		void setParameter(int _track, int _index, uint8_t _value, uint8_t _fine);

		// For the next block rendered only
		void trig(const int _track) { m_controls.trig(_track); }
		void gateOff(const int _track) { m_controls.gateOff(_track); }

		// For tests: unit 0's feedback table at 0, the voice without its chaos
		void testWithoutChaos();

		// One 48 kHz block of the 16 Tracks
		const Block& renderBlock();

	private:
		void convert(int _track);
		void control();

		// The tables BD MODERN reads (stFastTables, section 7's words by formula): the 128-entry parameter tables and
		// unit 2's wave as floats, the pitch's as words (the sweep's, the pitch map's exp2), which the machine
		// computes in integers; the sine unit 0 reads is feedbackSine's
		struct Tables
		{
			int32_t sweepDepth[128], sweepDecay[128];
			float clickLevel[128], modDecay[128], ampDecay[128];
			float modTarget[128], levelU0[128], levelU2[128];
			int32_t feedback[128];	// as words: unit 0's feedback is the machine's integers (feedbackSine)
			float wave[258];	// 257 and a copy of the last, for a phase that rounds up to 1
			int32_t exp2[2048];
		};
		Tables m_tables;

		// The click's step per sample and its filters
		float m_clickDec = 0;
		OnePole m_hp{}, m_lp{};

		// Each Track's parameters as words, its note and its trigs
		Controls m_controls;

		// What they convert to (convert): read every block, or latched at a trig (*Next)
		int32_t m_pitch[Lanes]{};		// the pitch map's word for the note and TUNE (pitchWord)
		int32_t m_sweepDepth[Lanes]{};	// the sweep's depth word
		double m_sweepDecayNext[Lanes]{};
		float m_modDecayNext[Lanes]{};
		bool m_driveNext[Lanes]{};
		alignas(16) float m_g0[Lanes]{}, m_g1[Lanes]{}, m_clickTarget[Lanes]{};
		int32_t m_fbWord[Lanes]{};	// unit 0's feedback word, the machine's 0x2c
		float m_modTarget[Lanes]{}, m_ampDecay[Lanes]{};
		bool m_hold[Lanes]{};

		// The voices' state
		alignas(16) uint32_t m_phase[Units][Lanes]{};
		Increments m_inc3{}, m_inc012{};	// unit 3's, and units 0-2's
		int32_t m_fb[Lanes]{};
		int32_t m_last[Lanes]{};	// unit 0's last sample, a Q31 word (feedbackSine)
		alignas(16) float m_g0Cur[Lanes]{}, m_g1Cur[Lanes]{};
		Clock48 m_clock;	// the block-rate control's
		double m_sweepDecay[Lanes]{}, m_sweep[Lanes]{};	// the sweep at its block
		Ramp48 m_u2, m_mod;
		AmpEnvelope m_amp;
		int m_modStage[Lanes]{};
		float m_modDecay[Lanes]{};
		Drive m_drive;

		// The envelopes at the ends of the previous and this block, which the stages ramp between
		alignas(16) float m_u2Level[Lanes]{}, m_u2Prev[Lanes]{};
		alignas(16) float m_modLevel[Lanes]{}, m_modPrev[Lanes]{};
		alignas(16) float m_ampLevel[Lanes]{}, m_ampPrev[Lanes]{};

		alignas(16) float m_clickRamp[Lanes]{}, m_clickCur[Lanes]{};
		alignas(16) float m_hpX[Lanes]{}, m_hpY[Lanes]{}, m_lpX[Lanes]{}, m_lpY[Lanes]{};

		Block m_b0, m_b1, m_a2, m_mix, m_b3, m_click;
	};
}
