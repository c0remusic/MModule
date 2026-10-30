#pragma once

#include <algorithm>
#include <cstdint>

#include "stFastStages.h"

namespace syntakt::fast
{
	// SY CHORD on 16 Tracks: four units, each a crossfade of two neighbouring waves of a wavetable, tuned as a chord
	// from one of 38 shapes, mixed with ramped gains under the units' envelope, through the drive stage, then two
	// one-pole low-passes whose cutoff follows the note, a sweep and a parameter, and the amp envelope. The same voice
	// as syntakt::syChord, block for block: a trig is seen in its block (the Track fades out) and sounds from the next;
	// a parameter or note takes effect at the next block. It renders the machine's own 48 kHz blocks, its block-rate
	// control stepped once per block (Host: on a host's)
	class SyChord
	{
	public:
		static constexpr int Parameters = Controls::Parameters;
		static constexpr int Units = 4;

		// Its tables come from stFastTables' formulas: nothing of the firmware
		SyChord();

		// Not copied or moved: each lane's waves point into its own tables
		SyChord(const SyChord&) = delete;
		SyChord& operator=(const SyChord&) = delete;

		// _track 0-15. The note in semitones; parameter _index 0-7 as VoiceCpu's Machine page: TUNE, the detune (shapes
		// 0-2) or the units' balance and octaves (the chords), the shape, the wave, the cutoff, the drive switch, HOLD,
		// DECAY; _value 0-127
		void setNote(int _track, int _note);
		void setParameter(int _track, int _index, uint8_t _value, uint8_t _fine);

		// For the next block rendered only
		void trig(const int _track) { m_controls.trig(_track); }
		void gateOff(const int _track) { m_controls.gateOff(_track); }

		// For tests: no feedback, nothing chaotic to take away
		void testWithoutChaos() {}

		// For tests: _track's words as the last block left them, as the voice holds them: the units' increments (the
		// voice's 0x78 + 0x80 k), the filter's cutoff and sweep (0x3f4, 0x400), its pole (0x4a4), and the crossfade's
		// low and high gains and wave index (0xa8, 0xac, 0xc0), the units' levels in the mix (0x08 + 4 k)
		void testWords(const int _track, int32_t* _words) const
		{
			const int32_t w[14] = {m_incWord[0][_track], m_incWord[1][_track], m_incWord[2][_track],
				m_incWord[3][_track], m_cut[_track], m_sweep[_track], m_poleWord[_track], m_lowGain[_track],
				m_highGain[_track], m_index[_track], m_level[0][_track], m_level[1][_track], m_level[2][_track],
				m_level[3][_track]};
			std::copy_n(w, 14, _words);
		}

		// One 48 kHz block of the 16 Tracks
		const Block& renderBlock();

	private:
		void convert(int _track);
		void control();
		void crossfade(int _track);

		// The tables SY CHORD reads (stFastTables, section 7's words by formula): as words where the machine computes in
		// integers (the units' factors and increments, the filter, the sweep), the amp's decay as floats; the waves as
		// floats, 0 the RAM sine and 1-32 the wavetable's
		struct Tables
		{
			int32_t ratio[4][128], cutoff[128], pole[128], sweepDecay[128], attack[40], shapes[140];
			float ampDecay[128];
			int32_t exp2[2048];
			float waves[33][258];
		};
		Tables m_tables;

		// Each Track's parameters as words, its note and its trigs
		Controls m_controls;

		// What they convert to (convert): read every block, or latched at a trig (*Next). The pitch word; the units'
		// factors the shape sets (where m_ratioSet) and their levels in the mix before the increments' limit, as the
		// machine's words
		int32_t m_pitch[Lanes]{}, m_ratioNext[Units][Lanes]{}, m_levelNext[Units][Lanes]{};
		bool m_ratioSet[Units][Lanes]{};

		// The update's, every block: the units' factors and increments as the machine's words (its 0x50 and 0x78 +
		// 0x80 k), which a unit keeps where the update leaves them, and their levels in the mix
		int32_t m_ratio[Units][Lanes]{}, m_incWord[Units][Lanes]{}, m_level[Units][Lanes]{};
		alignas(16) uint32_t m_inc[Units][Lanes]{};
		alignas(16) float m_gainTarget[Units][Lanes]{};
		int32_t m_cut[Lanes]{};							// the filter's cutoff under the note (its 0x3f4)
		int32_t m_position[Lanes]{}, m_indexNext[Lanes]{};	// the crossfade's position (0xa4) and wave index
		float m_ampDecay[Lanes]{}, m_attackNext[Lanes]{};
		int32_t m_sweepDecayNext[Lanes]{};
		bool m_driveNext[Lanes]{}, m_hold[Lanes]{};

		// The voices' state
		bool m_playing[Lanes]{};	// since its first trig
		alignas(16) uint32_t m_phase[Units][Lanes]{};
		alignas(16) float m_gain[Units][Lanes]{};	// the mix's levels ramped from

		// The crossfade (0x400032b4) in the machine's words: the low and high waves' gains (0xa8, 0xac), the wave index
		// (0xc0) and the last block's index and waves (0xb8, 0xbc: 0 the sine, 1-32 the wavetable's); this block's
		// two waves per lane and their gains' ramps
		int32_t m_lowGain[Lanes]{}, m_highGain[Lanes]{}, m_index[Lanes]{}, m_lastLow[Lanes]{}, m_lastHigh[Lanes]{};
		const float* m_wave1[Lanes]{};
		const float* m_wave2[Lanes]{};
		alignas(16) float m_g1From[Lanes]{}, m_g1To[Lanes]{}, m_g2From[Lanes]{}, m_g2To[Lanes]{};

		// The units' envelope, the same for the four, its attack latched at the trig, and its ramp over the block
		RiseEnvelope m_env;
		float m_attack[Lanes]{};
		alignas(16) float m_envFrom[Lanes]{}, m_envTo[Lanes]{};

		// The sweep (0x400) and its decay in the machine's words; the low-passes' pole word (0x4a4), their coefficients
		// and their states
		int32_t m_sweep[Lanes]{}, m_sweepDecay[Lanes]{}, m_poleWord[Lanes]{};
		alignas(16) float m_lpB[Lanes]{}, m_lpA[Lanes]{}, m_lpX[2][Lanes]{}, m_lpY[2][Lanes]{};

		AmpEnvelope m_amp;
		Drive m_drive;

		// The amp's envelope over the block, sample by sample, and at its end
		Block m_gAmp;
		float m_ampEnd[Lanes]{};

		// The units' samples and the voice's
		Block m_unit[Units], m_out;
	};
}
