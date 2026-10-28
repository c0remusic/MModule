#pragma once

#include <cstdint>

#include "stFastStages.h"

namespace syntakt::fast
{
	// SD VINTAGE on 16 Tracks: three units, two sines (the note and its ratio) and a wave two octaves up,
	// phase-modulated by a fourth, a wave feeding back on its own output under its envelope; each unit under an
	// envelope that rises over three blocks, then decays. With them, gated noise high-passed, low-passed twice and
	// under a modulator envelope; all four mixed, high-passed, doubled into a drive stage and the amp. A pitch sweep
	// that rises over two blocks. The same voice as syntakt::sdVintage, block for block: a trig is seen in its block
	// (the Track fades out) and sounds from the next; a parameter or note takes effect at the next block. It renders
	// the machine's own 48 kHz blocks, its block-rate control stepped once per block (Host: on a host's)
	class SdVintage
	{
	public:
		static constexpr int Parameters = Controls::Parameters;
		static constexpr int Units = 4;

		// Its tables come from stFastTables' formulas: nothing of the firmware
		SdVintage();

		// _track 0-15. The note in semitones; parameter _index 0-7 as VoiceCpu's Machine page: TUNE, units 1 and 3's
		// ratios, the noise and the units' balance (levels, modulation, unit 3's feedback, the noise's low-pass), the
		// sweep, unit 3's decay, the drive switch, HOLD, DECAY; _value 0-127
		void setNote(int _track, int _note);
		void setParameter(int _track, int _index, uint8_t _value, uint8_t _fine);

		// For the next block rendered only
		void trig(const int _track) { m_controls.trig(_track); }
		void gateOff(const int _track) { m_controls.gateOff(_track); }

		// For tests: unit 3's feedback table at 0
		void testWithoutChaos();

		// For tests, before _track's first trig: its noise from the six words the exact port's machine change draws
		// (0x570-0x584, bench::exactNoiseSeeds), of which the noise reads the last four. The machine's own seeds are a
		// law (NoiseGenerator): its noise is statistically the firmware's but not its sample sequence
		void testSeedNoise(const int _track, const int32_t* _drawn) { m_noise.seed(_track, _drawn + 2); }

		// One 48 kHz block of the 16 Tracks
		const Block& renderBlock();

	private:
		void convert(int _track);
		void control();

		// The tables SD VINTAGE reads (stFastTables, section 7's words by formula): as floats, and as the words for the
		// pitch (the ratios, the sweep's depth, the pitch map's exp2); the 256-entry sine units 0 and 1 read is
		// computed
		struct Tables
		{
			int32_t ratio0[128], ratio1[128], sweepDecay[128], sweepDepth[128];
			float depth[3][128];	// units 0, 1 and 2's modulation by unit 3, in cycles per unit
			float level[3][128];	// units 1 and 2's and the noise's in the mix
			float noiseLp[128], feedback[128];
			float modDecay[128], decay2[128], decay01[128], decay3[128 * 128];
			int32_t exp2[2048];
			float waveU2[258], waveU3[258];	// and a copy of the last
			float sine[258];
		};
		Tables m_tables;

		// Each Track's parameters as words, its note and its trigs
		Controls m_controls;

		// What they convert to (convert): read every block, or latched at a trig (*Next)
		int32_t m_pitch[Lanes]{};				// the pitch map's word for the note and TUNE (0x40002596's f)
		int64_t m_factor[Units][Lanes]{};		// the units' frequency factors (0x50, 0x04000000 the note's)
		alignas(16) float m_depthTarget[3][Lanes]{};
		alignas(16) float m_fbTarget[Lanes]{};	// unit 3's feedback, in cycles per unit
		alignas(16) float m_lpB[Lanes]{}, m_lpA[Lanes]{};	// the noise's low-pass
		alignas(16) float m_gainTarget[4][Lanes]{};	// units 0-2's and the noise's in the mix
		int32_t m_sweepDepthNext[Lanes]{};
		double m_sweepDecayNext[Lanes]{};
		float m_decayNext[Units][Lanes]{}, m_modDecayNext[Lanes]{}, m_attack3Next[Lanes]{};
		bool m_driveNext[Lanes]{}, m_hold[Lanes]{};

		// The voices' state
		bool m_playing[Lanes]{};	// since its first trig
		alignas(16) uint32_t m_phase[Units][Lanes]{};
		Increments m_inc[Units]{};
		alignas(16) float m_depth[3][Lanes]{}, m_fb[Lanes]{}, m_last[Lanes]{};
		alignas(16) float m_gain[4][Lanes]{};
		Clock48 m_clock;
		double m_sweep[Lanes]{}, m_sweepDecay[Lanes]{};
		bool m_sweepRising[Lanes]{};
		int32_t m_sweepDepth[Lanes]{};
		float m_decay[Units][Lanes]{}, m_target[Units][Lanes]{}, m_attack3[Lanes]{}, m_modDecay[Lanes]{};
		float m_ampDecay[Lanes]{};
		RiseEnvelope m_env[Units], m_mod;
		AmpEnvelope m_amp;
		Drive m_drive;
		NoiseGenerator m_noise;

		// The envelopes over the block, sample by sample (the units', the modulator's, the amp's), the latter two at
		// its end
		Block m_gEnv[Units], m_gMod, m_gAmp;
		float m_modEnd[Lanes]{}, m_ampEnd[Lanes]{};

		// The filters' states: the noise's high-pass (x1, x2, y1, y2) and two low-passes, the output's high-pass
		alignas(16) float m_bq[4][Lanes]{};
		alignas(16) float m_lp1X[Lanes]{}, m_lp1Y[Lanes]{}, m_lp2X[Lanes]{}, m_lp2Y[Lanes]{};
		alignas(16) float m_hpX[Lanes]{}, m_hpY[Lanes]{};

		Block m_b[Units], m_scaled3, m_noiseOut, m_out;
	};
}
