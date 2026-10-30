#pragma once

#include <cstdint>

#include "stFastStages.h"

namespace syntakt::fast
{
	// SY TOY on 16 Tracks: five sine units. Unit 4 feeds back on its own sample and, under its envelope, phase-modulates
	// unit 2; unit 0 at the note and units 1 to 3, each at its ratio under its envelope, are mixed with ramped gains,
	// then go through the amp envelope and the drive stage. The same voice as syntakt::syToy, block for block: a trig is
	// seen in its block (the Track fades out) and sounds from the next; a parameter or note takes effect at the next
	// block. It renders the machine's own 48 kHz blocks, its block-rate control stepped once per block (Host: on a
	// host's)
	class SyToy
	{
	public:
		static constexpr int Parameters = Controls::Parameters;
		static constexpr int Units = 5;

		// Its tables come from stFastTables' formulas: nothing of the firmware
		SyToy();

		// _track 0-15. The note in semitones; parameter _index 0-7 as VoiceCpu's Machine page: TUNE, the units' ratios,
		// unit 2's modulation by unit 4 and unit 4's feedback, the units' levels in the mix, their decays and level at
		// rest, the drive switch, HOLD, DECAY; _value 0-127
		void setNote(int _track, int _note);
		void setParameter(int _track, int _index, uint8_t _value, uint8_t _fine);

		// For the next block rendered only
		void trig(const int _track) { m_controls.trig(_track); }
		void gateOff(const int _track) { m_controls.gateOff(_track); }

		// Only Tracks 0 to _lanes - 1 rendered, from the next block (a multiple of 4; 16 at construction): the
		// others silent, their voices frozen where they are until a wider setLanes renders them again
		void setLanes(int _lanes);

		// For tests: unit 4's feedback table at 0
		void testWithoutChaos();

		// For tests: _track's words as the last block left them, as the voice holds them: the units' increments and
		// phases (0x78 and 0x6c + 0x80 k), units 1 to 4's envelope levels (0x7c + 0x80 k), unit 4's last sample, its
		// feedback ramped and its feedback word (0x28, 0x30, 0x2c), unit 2's modulation (0x18c), the units' levels in
		// the mix (0x08 + 4 k), the amp's stage and its release latched (0x3dc, 0x3e4)
		void testWords(const int _track, int32_t* _words) const
		{
			for(int k = 0; k < Units; ++k)
			{
				_words[k] = m_incWord[k][_track];
				_words[Units + k] = static_cast<int32_t>(m_phase[k][_track] >> 8);
			}
			for(int k = 0; k < 4; ++k)
			{
				_words[2 * Units + k] = m_env[k][_track];
				_words[2 * Units + 8 + k] = m_level[k][_track];
			}
			_words[2 * Units + 4] = m_last[_track];
			_words[2 * Units + 5] = m_fb[_track];
			_words[2 * Units + 6] = m_fbWord[_track];
			_words[2 * Units + 7] = m_depthWord[_track];
			_words[2 * Units + 12] = m_amp.stage[_track];
			_words[2 * Units + 13] = m_amp.released[_track];
		}

		// One 48 kHz block of the 16 Tracks
		const Block& renderBlock();

	private:
		void convert(int _track);
		void control();

		// The tables SY TOY reads (stFastTables, section 7's words by formula): as words where the machine computes in
		// integers (the increments, unit 4's feedback, the units' envelopes and levels, unit 2's modulation), the
		// amp's decay as floats
		struct Tables
		{
			int32_t ratio[4][128], depth[128], feedback[128], target[128], gain[3][128], decay[4][128];
			float ampDecay[128];
			int32_t exp2[2048];
			float sine[258];
		};
		Tables m_tables;

		// Each Track's parameters as words, its note and its trigs
		Controls m_controls;
		int m_lanes = Lanes;	// the Tracks rendered (setLanes)

		// What they convert to (convert), every block: the units' increments, as the machine's words too (0x78); unit
		// 4's feedback word (0x2c); unit 2's modulation (0x18c) and in cycles per unit; units 1 to 4's decays (their
		// 0x58) and level at rest (0x5c); the units' levels in the mix (0x08 + 4 k), unit 0's that of the drive off
		Increments m_inc[Units]{};
		int32_t m_incWord[Units][Lanes]{}, m_fbWord[Lanes]{}, m_depthWord[Lanes]{}, m_decay[4][Lanes]{};
		int32_t m_target[Lanes]{}, m_levelNext[4][Lanes]{};
		alignas(16) float m_depthTarget[Lanes]{};
		float m_ampDecay[Lanes]{};
		bool m_driveNext[Lanes]{}, m_hold[Lanes]{};

		// The voices' state
		bool m_playing[Lanes]{};	// since its first trig
		alignas(16) uint32_t m_phase[Units][Lanes]{};
		int32_t m_fb[Lanes]{}, m_last[Lanes]{};	// unit 4's feedback ramped from (its 0x30), its last sample (0x28)
		alignas(16) float m_depth[Lanes]{};		// unit 2's modulation ramped from

		// Units 1 to 4's envelopes in the machine's words (0x40003c0e: negative, INT32_MIN full), at the end of the last
		// block and of this one, and as the ramps of their outputs
		int32_t m_envFrom[4][Lanes]{}, m_env[4][Lanes]{};
		alignas(16) float m_gFrom[4][Lanes]{}, m_gTo[4][Lanes]{};

		// The units' levels in the mix this block as the machine's words, and their ramps
		int32_t m_level[4][Lanes]{};
		alignas(16) float m_gain[4][Lanes]{}, m_gainTarget[4][Lanes]{};

		AmpEnvelope m_amp;
		Drive m_drive;

		// The amp's envelope over the block, sample by sample, and at its end
		Block m_gAmp;
		float m_ampEnd[Lanes]{};

		// Units 0 to 3's samples into the mix, unit 4's under its envelope, the voice's
		Block m_unit[4], m_mod, m_out;
	};
}
