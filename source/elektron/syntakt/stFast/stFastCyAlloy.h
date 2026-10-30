#pragma once

#include <cstdint>

#include "stFastStages.h"

namespace syntakt::fast
{
	// CY ALLOY on 16 Tracks: two pairs of units, each under its own envelope. Unit 0, a sine, modulates unit 1, a
	// wave of five harmonics; unit 2, a sine feeding back on itself, modulates unit 3, the same wave. Units 1 and 3
	// are mixed at 0.5 each into a resonant high-pass (3.5 kHz, Q 5), then a drive stage and the amp. The same voice
	// as syntakt::cyAlloy, block for block: a trig is seen in its block (the Track fades out) and sounds from the next;
	// a parameter or note takes effect at the next block. It renders the machine's own 48 kHz blocks, its block-rate
	// control stepped once per block (Host: on a host's)
	class CyAlloy
	{
	public:
		static constexpr int Parameters = Controls::Parameters;
		static constexpr int Units = 4;

		// Its tables come from stFastTables' formulas: nothing of the firmware
		CyAlloy();

		// _track 0-15. The note in semitones; parameter _index 0-7 as VoiceCpu's Machine page: TUNE, unit 2's ratio
		// (and unit 0's over unit 1's), the modulation depth with unit 2's feedback, units 0 and 1's ratio, units 0
		// and 2's levels at rest, the drive switch, HOLD, DECAY; _value 0-127
		void setNote(int _track, int _note);
		void setParameter(int _track, int _index, uint8_t _value, uint8_t _fine);

		// For the next block rendered only
		void trig(const int _track) { m_controls.trig(_track); }
		void gateOff(const int _track) { m_controls.gateOff(_track); }

		// Only Tracks 0 to _lanes - 1 rendered, from the next block (a multiple of 4; 16 at construction): the
		// others silent, their voices frozen where they are until a wider setLanes renders them again
		void setLanes(int _lanes);

		// For tests: the depth table at 0, as the exact port with that table at 0 plays: no feedback, and units 1 and 3
		// unmodulated
		void testWithoutChaos();

		// One 48 kHz block of the 16 Tracks
		const Block& renderBlock();

		// The output's high-pass, a high-pass of 3.5 kHz, Q 5 and gain 0.5: b0, b1, b2, a1, a2 of biquad
		static void highPass(float* _c);

	private:
		void convert(int _track);
		void control();

		// The tables CY ALLOY reads (stFastTables, section 7's words by formula): as floats, and as the words for the
		// pitch (the ratios, the pitch map's exp2); the 256-entry sine units 0 and 2 read is computed
		struct Tables
		{
			float decayOdd[128], decayEven[128], target[128], ampDecay[128];
			int32_t depth[128];	// as words: unit 2's feedback is the machine's integers
			int32_t ratio[128], spread[128];
			int32_t exp2[2048];
			float wave[258];	// units 1 and 3's, and a copy of the last
			float sine[258];	// sin(2 pi i / 256)
		};
		Tables m_tables;

		float m_bq[5]{};	// the output's high-pass, b0, b1, b2, a1, a2: y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2

		// Each Track's parameters as words, its note and its trigs
		Controls m_controls;
		int m_lanes = Lanes;	// the Tracks rendered (setLanes)

		// What they convert to (convert): read every block, or latched at a trig (*Next)
		int32_t m_pitch[Lanes]{};				// the pitch map's word for the note and TUNE (0x40002596's f)
		int64_t m_factor[Units][Lanes]{};		// the units' frequency factors (0x50, 0x04000000 the note's)
		alignas(16) float m_depthTarget[Lanes]{};
		int32_t m_fbWord[Lanes]{};	// unit 2's feedback word, the machine's 0x2c
		float m_target[Units][Lanes]{};			// the units' envelopes' levels at rest
		float m_ampDecay[Lanes]{};
		bool m_hold[Lanes]{};
		float m_decayNext[Units][Lanes]{};
		bool m_driveNext[Lanes]{};

		// The voices' state
		bool m_playing[Lanes]{};	// since its first trig
		alignas(16) uint32_t m_phase[Units][Lanes]{};
		Increments m_inc[Units]{};
		alignas(16) float m_depth[Lanes]{};
		int32_t m_fb[Lanes]{};
		int32_t m_last[Lanes]{};	// unit 2's last sample, a Q31 word (feedbackSine)
		Clock48 m_clock;
		float m_decay[Units][Lanes]{};
		Ramp48 m_env[Units];
		AmpEnvelope m_amp;
		Drive m_drive;

		// The envelopes over the block, sample by sample (the units', the amp's), the amp's at its end
		Block m_gEnv[Units], m_gAmp;
		float m_ampEnd[Lanes]{};

		alignas(16) float m_bqState[4][Lanes]{};	// x1, x2, y1, y2

		Block m_b[Units], m_out;
	};
}
