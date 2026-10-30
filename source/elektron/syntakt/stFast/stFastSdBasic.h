#pragma once

#include <cstdint>

#include "stFastStages.h"

namespace syntakt::fast
{
	// SD BASIC on 16 Tracks: four sines, each under its own envelope. Unit 0 (2x the note) feeds back on itself,
	// unit 1 (2x) modulates unit 2 (the note) and, with unit 0, unit 3 (3x); unit 2, high-passed, plays under the
	// modulator's envelope and unit 3, low-passed, under the amp's, both into a drive stage. Ratios and sweep on the
	// machine's parameters. The same voice as syntakt::sdBasic, block for block: a trig is seen in its block (the
	// Track fades out) and sounds from the next; a parameter or note takes effect at the next block. It renders the
	// machine's own 48 kHz blocks, its block-rate control stepped once per block, as BdModern's (Host: on a host's)
	class SdBasic
	{
	public:
		static constexpr int Parameters = Controls::Parameters;
		static constexpr int Units = 4;

		// Its tables come from stFastTables' formulas: nothing of the firmware
		SdBasic();

		// _track 0-15. The note in semitones; parameter _index 0-7 as VoiceCpu's Machine page: TUNE, the units'
		// ratios, unit 0's feedback with unit 3's modulation by it, the sweep, the units' envelopes, the drive switch,
		// HOLD, DECAY; _value 0-127
		void setNote(int _track, int _note);
		void setParameter(int _track, int _index, uint8_t _value, uint8_t _fine);

		// For the next block rendered only
		void trig(const int _track) { m_controls.trig(_track); }
		void gateOff(const int _track) { m_controls.gateOff(_track); }

		// Only Tracks 0 to _lanes - 1 rendered, from the next block (a multiple of 4; 16 at construction): the
		// others silent, their voices frozen where they are until a wider setLanes renders them again
		void setLanes(int _lanes);

		// For tests: unit 0's feedback table at 0, the voice without its chaos
		void testWithoutChaos();

		// One 48 kHz block of the 16 Tracks
		const Block& renderBlock();

	private:
		void convert(int _track);
		void control();

		// The tables SD BASIC reads (stFastTables, section 7's words by formula): as floats, and as the words for the
		// pitch, which the machine computes in integers as a long Hit's phase needs it (the ratios, the sweep's depth,
		// the pitch map's exp2); the sine unit 0 reads is feedbackSine's
		struct Tables
		{
			float decayU0[128], decayU1[128], decayU2[128], decayU3[128], ampDecay[128], modDecay[128];
			float ampAttack[128], targetU0[128], targetU1[128];
			int32_t feedback[128];	// as words: unit 0's feedback is the machine's integers (feedbackSine)
			float depthU3[128], lowPass[128];
			int32_t scaleU0[128], scaleU1[128], scaleU3[128], sweepDepth[128], sweepDecay[128];
			int32_t exp2[2048];
		};
		Tables m_tables;

		// Unit 2's high-pass, unit 3's low-pass poles (MOD at 0 and above)
		OnePole m_hp{};
		float m_lpPole[2]{};

		// Each Track's parameters as words, its note and its trigs
		Controls m_controls;
		int m_lanes = Lanes;	// the Tracks rendered (setLanes)

		// What they convert to (convert): read every block, or latched at a trig (*Next)
		int32_t m_pitch[Lanes]{};			// the pitch map's word for the note and TUNE (0x40002596's f)
		int32_t m_scale[Units][Lanes]{};	// the units' ratio words (Q31, 0.5 the note's)
		alignas(16) float m_depth2Target[Lanes]{};
		int32_t m_fbWord[Lanes]{};	// unit 0's feedback word, the machine's 0x2c
		float m_target[Units][Lanes]{};		// the units' envelopes' levels at rest
		float m_modDecay[Lanes]{}, m_ampDecay[Lanes]{};
		bool m_hold[Lanes]{};
		alignas(16) float m_lpB[Lanes]{}, m_lpA[Lanes]{};
		float m_decayNext[Units][Lanes]{};
		double m_sweepDecayNext[Lanes]{};
		int32_t m_sweepDepthNext[Lanes]{};
		float m_ampAttackNext[Lanes]{};
		bool m_driveNext[Lanes]{};

		// The voices' state
		bool m_playing[Lanes]{};	// since its first trig
		alignas(16) uint32_t m_phase[Units][Lanes]{};
		Increments m_inc[Units]{};
		alignas(16) float m_depth2[Lanes]{};
		int32_t m_fb[Lanes]{};
		int32_t m_last[Lanes]{};	// unit 0's last sample, a Q31 word (feedbackSine)
		Clock48 m_clock;
		double m_sweepDecay[Lanes]{}, m_sweep[Lanes]{};
		int32_t m_sweepDepth[Lanes]{};
		float m_decay[Units][Lanes]{}, m_ampAttack[Lanes]{};
		Ramp48 m_env[Units];
		RiseEnvelope m_mod;
		AmpEnvelope m_amp;
		Drive m_drive;

		// The envelopes over the block, sample by sample (the units', the modulator's, the amp's), the latter two at
		// its end
		Block m_gEnv[Units], m_gMod, m_gAmp;
		float m_modEnd[Lanes]{}, m_ampEnd[Lanes]{};

		alignas(16) float m_hpX[Lanes]{}, m_hpY[Lanes]{}, m_lpX[Lanes]{}, m_lpY[Lanes]{};

		Block m_b[Units], m_out;
	};

	// The stages SD BASIC alone uses

	// A sine phase-modulated by _src1 at _depth1 and by _src2 at a depth ramped from _depth2 to _depth2Target (left
	// there)
	void pmSine2(Block& _out, const Block& _src1, float _depth1, const Block& _src2, float* _depth2,
		const float* _depth2Target, uint32_t* _phase, const Increments& _inc);
}
