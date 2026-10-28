#pragma once

#include <cstdint>

#include "stFastStages.h"

namespace syntakt::fast
{
	// CP VINTAGE on 16 Tracks: a hand clap and a tone. The clap: sparse crackle (gated noise) and white noise
	// low-passed, each under a burst envelope that strikes again three times a period apart, mixed, through a band-pass
	// resonator two octaves above the note, a high-pass that follows the note, a sample-rate reducer and a second
	// high-pass, then eight times into a clip. The tone: a sine an octave up, phase-modulated by a second sine feeding
	// back on its own output under its envelope, under an envelope of its own. Both mixed into a drive stage. The same
	// voice as syntakt::cpVintage, block for block: a trig is seen in its block (the Track's envelopes fade out) and
	// sounds from the next; a parameter or note takes effect at the next block. It renders the machine's own 48 kHz
	// blocks, its block-rate control stepped once per block (Host: on a host's). HOLD plays one strike: on the machine,
	// HOLD's strikes repeat on its clock step (0x8000a07c, the tempo's), which MoDule leaves at 0 as the bench's exact
	// port does; a gate-off then changes nothing
	class CpVintage
	{
	public:
		static constexpr int Parameters = Controls::Parameters;
		static constexpr int Units = 2;

		// Its tables come from stFastTables' formulas: nothing of the firmware
		CpVintage();

		// _track 0-15. The note in semitones; parameter _index 0-7 as VoiceCpu's Machine page: TUNE, the tone (unit 0's
		// modulation, unit 1's ratio and feedback), the balance of clap and tone, the claps' period and the sample-rate
		// reducer, the tone's decay, the drive switch, HOLD, DECAY (the clap's last); _value 0-127
		void setNote(int _track, int _note);
		void setParameter(int _track, int _index, uint8_t _value, uint8_t _fine);

		// For the next block rendered only
		void trig(const int _track) { m_controls.trig(_track); }
		void gateOff(const int _track) { m_controls.gateOff(_track); }

		// For tests: unit 1's feedback table at 0
		void testWithoutChaos();

		// For tests: unit 1's envelope word on _track as the last block left it (0x40003c0e's level, the voice's 0xfc)
		int32_t testEnvelope(const int _track) const { return m_env[_track]; }

		// For tests, before _track's first trig: its noise from the six words the exact port's machine change draws
		// (0x570-0x584, bench::exactNoiseSeeds), of which the crackle reads the last four and the white noise goes on
		// with the last two. The machine's own seeds are a law (NoiseGenerator)
		void testSeedNoise(const int _track, const int32_t* _drawn) { m_noise.seed(_track, _drawn + 2); }

		// One 48 kHz block of the 16 Tracks
		const Block& renderBlock();

		// The resonator's eight coefficient words for _note and the TUNE word _tune, two octaves above their pitch, as
		// the machine computes them (0x400052f8), which the voice runs and its test checks: c0 and c3 the input's, c1,
		// c2, c4 and c5 the two states', c6 and c7 the output's
		void resonatorWords(int _note, int32_t _tune, int32_t* _c) const;

	private:
		void convert(int _track);
		void control();

		// The tables CP VINTAGE reads (stFastTables, section 7's words by formula): as floats, and as words where the
		// machine computes in integers (the claps' period on its timer, the rate reducer's, the pitch, unit 1, the
		// high-pass's and the resonator's coefficients)
		struct Tables
		{
			float toneDecay[128], repeatDecay[128], clapDecay[128], crackleDecay[128];
			float clapLevel[128], toneLevel[128], toneDepth[128];
			int32_t modDecay[128], toneFeedback[128];
			int32_t sampleRate[128], period[128], toneRatio[128], highPass[128];
			int32_t resonatorA[1024], resonatorB[1024], log2[1024], exp2Half[1025];
			int32_t exp2[2048];
			float sine[258];
		};
		Tables m_tables;

		// Each Track's parameters as words, its note and its trigs
		Controls m_controls;

		// What they convert to (convert), read every block; the drive latched at a trig
		int32_t m_pitch[Lanes]{};				// the pitch map's word for the note and TUNE (0x40002596's f)
		alignas(16) float m_res[8][Lanes]{};	// the resonator's coefficients (resonatorWords), c1 and c5 as 1 - c
		alignas(16) float m_hpB[Lanes]{}, m_hpA[Lanes]{};	// the clap's high-pass, y = b (x - x1) + a y1
		alignas(16) int32_t m_rate[Lanes]{};	// the rate reducer's period less one, Q16
		alignas(16) float m_depthTarget[Lanes]{};	// unit 0's modulation, cycles per unit
		int32_t m_fbWord[Lanes]{};				// unit 1's feedback, the machine's word (its 0x2c)
		alignas(16) float m_toneTarget[Lanes]{}, m_clapTarget[Lanes]{};	// the tone and the clap in the mix
		uint32_t m_period[Lanes]{};
		float m_repeatDecay[Lanes]{}, m_lastDecay[2][Lanes]{}, m_toneDecay[Lanes]{};
		int32_t m_modDecay[Lanes]{};
		bool m_driveNext[Lanes]{}, m_hold[Lanes]{};

		// The voices' state
		bool m_playing[Lanes]{};	// since its first trig
		alignas(16) uint32_t m_phase[Units][Lanes]{};
		Increments m_inc[Units]{};
		alignas(16) float m_depth[Lanes]{};
		int32_t m_fb[Lanes]{}, m_last[Lanes]{};	// unit 1's feedback ramped from (its 0x30), its last output (0x28)
		alignas(16) float m_tone[Lanes]{}, m_clap[Lanes]{};	// the mix's levels, ramped from

		// Unit 1's envelope in the machine's words (0x40003c0e: negative, INT32_MIN full), at the end of the last
		// block and of this one, and whether it rises
		int32_t m_envFrom[Lanes]{}, m_env[Lanes]{};
		bool m_envRising[Lanes]{};

		AmpEnvelope m_toneEnv;
		Drive m_drive;
		NoiseGenerator m_noise;

		// The bursts of the crackle (0) and the noise (1): full at a trig and at each of their three repeats, a block
		// longer there, then decaying, the repeats' decay until the last. Their timer counts 2^19 a block
		float m_burst[2][Lanes]{};
		bool m_fresh[2][Lanes]{};
		int m_count[2][Lanes]{}, m_repeats[Lanes]{};
		uint32_t m_timer[2][Lanes]{};

		// The VCAs' ramps over the block, the crackle's, the noise's and the tone's
		alignas(16) float m_vcaFrom[3][Lanes]{}, m_vcaTo[3][Lanes]{};

		// The filters' states: the noise's low-pass, the resonator (its two states, x 256, and its output stage's),
		// the clap's high-pass (its coefficients ramped from), the rate reducer, the second high-pass
		alignas(16) float m_lpX[Lanes]{}, m_lpY[Lanes]{};
		alignas(16) float m_resFrom[8][Lanes]{}, m_resY1[Lanes]{}, m_resY2[Lanes]{}, m_resZ[Lanes]{};
		alignas(16) float m_hpBFrom[Lanes]{}, m_hpAFrom[Lanes]{}, m_hpX[Lanes]{}, m_hpY[Lanes]{};
		alignas(16) float m_held[2][Lanes]{};
		alignas(16) int32_t m_heldCount[2][Lanes]{};
		alignas(16) float m_hp2X[Lanes]{}, m_hp2Y[Lanes]{};

		Block m_scaled, m_toneOut, m_crackle, m_white, m_out;
	};
}
