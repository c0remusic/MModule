#pragma once

#include <cstdint>

#include "stFastStages.h"

namespace syntakt::fast
{
	// PC CARBON on 16 Tracks: two pairs of units, each under its own envelope. Unit 0, a wave of harmonics 1, 5 and 9,
	// modulates unit 1, a sine; unit 2, a sine, modulates unit 3, a sine at the note. Units 3 and 1 are mixed into a
	// drive stage and the amp, which holds full (or, with HOLD, until the gate-off, then decays as unit 3). A pitch
	// sweep on every unit. The same voice as syntakt::pcCarbon, block for block: a trig is seen in its block (the Track
	// fades out) and sounds from the next; a parameter or note takes effect at the next block. It renders the
	// machine's own 48 kHz blocks, its block-rate control stepped once per block (Host: on a host's)
	class PcCarbon
	{
	public:
		static constexpr int Parameters = Controls::Parameters;
		static constexpr int Units = 4;

		// Its tables come from stFastTables' formulas: nothing of the firmware
		PcCarbon();

		// _track 0-15. The note in semitones; parameter _index 0-7 as VoiceCpu's Machine page: TUNE, the units'
		// ratios, the modulation depth with unit 1's decay, the sweep, units 0 and 2's levels at rest, the drive
		// switch, HOLD, DECAY; _value 0-127
		void setNote(int _track, int _note);
		void setParameter(int _track, int _index, uint8_t _value, uint8_t _fine);

		// For the next block rendered only
		void trig(const int _track) { m_controls.trig(_track); }
		void gateOff(const int _track) { m_controls.gateOff(_track); }

		// Only Tracks 0 to _lanes - 1 rendered, from the next block (a multiple of 4; 16 at construction): the
		// others silent, their voices frozen where they are until a wider setLanes renders them again
		void setLanes(int _lanes);

		// For tests, as the other machines': PC CARBON has no feedback, nothing to take out
		void testWithoutChaos() {}

		// One 48 kHz block of the 16 Tracks
		const Block& renderBlock();

	private:
		void convert(int _track);
		void control();

		// The decay coefficient units 1 and 3 read from their curve at _x in [0, 1]
		float decayAt(double _x) const;

		// The tables PC CARBON reads (stFastTables, section 7's words by formula): as floats, and as the words for the
		// pitch (the factors, the sweep's depth, the pitch map's exp2); the 256-entry sine units 1-3 read is computed
		struct Tables
		{
			float decayPosition[128], decayCurve[1024], decayU0[128], decayU2[128];
			float depthU1[128], depthU3[128], targetU0[128], targetU2[128];
			int32_t factor[3][128], sweepDepth[128], sweepDecay[128];
			int32_t exp2[2048];
			float wave[258];	// unit 0's, and a copy of the last
			float sine[258];	// sin(2 pi i / 256)
		};
		Tables m_tables;

		// Each Track's parameters as words, its note and its trigs
		Controls m_controls;
		int m_lanes = Lanes;	// the Tracks rendered (setLanes)

		// What they convert to (convert): read every block, or latched at a trig (*Next)
		int32_t m_pitch[Lanes]{};				// the pitch map's word for the note and TUNE (0x40002596's f)
		int64_t m_factor[Units][Lanes]{};		// the units' frequency factors (0x50, 0x04000000 the note's)
		int32_t m_sweepDepth[Lanes]{};
		alignas(16) float m_depthTarget[2][Lanes]{};	// units 1 and 3's modulation, in cycles per unit
		float m_target[Units][Lanes]{};			// the units' envelopes' levels at rest (1 and 3: latched)
		float m_decayOdd[2][Lanes]{};			// units 1 and 3's decays, from the curve
		bool m_hold[Lanes]{};
		float m_decayNext[2][Lanes]{};			// units 0 and 2's
		double m_sweepDecayNext[Lanes]{};
		bool m_driveNext[Lanes]{};

		// The voices' state
		bool m_playing[Lanes]{};	// since its first trig
		alignas(16) uint32_t m_phase[Units][Lanes]{};
		Increments m_inc[Units]{};
		alignas(16) float m_depth[2][Lanes]{};
		Clock48 m_clock;
		double m_sweepDecay[Lanes]{}, m_sweep[Lanes]{};
		float m_decay[Units][Lanes]{};
		float m_ampDecay[Lanes]{}, m_decayU3Prev[Lanes]{};
		Ramp48 m_env[Units];
		AmpEnvelope m_amp;
		Drive m_drive;

		// The envelopes over the block, sample by sample (the units', the amp's), the amp's at its end, and the mix's
		// gains
		Block m_gEnv[Units], m_gAmp;
		float m_ampEnd[Lanes]{};
		alignas(16) float m_gainU3[Lanes]{}, m_gainU1[Lanes]{};

		Block m_b[Units], m_out;
	};
}
