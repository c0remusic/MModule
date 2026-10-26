#pragma once

#include <cstdint>
#include <vector>

// The Syntakt's machines rewritten for speed: 32-bit float, the 16 Tracks side by side (each field an array of 16
// lanes, a vector op on 4 of them), blocks of 32 samples at the host's rate, every rate-dependent constant derived from
// the rate so that times in seconds stay the 48 kHz machine's. The exact integer port (stEngine) is the reference.
namespace syntakt::fast
{
	constexpr int Lanes = 16;		// one per Track
	constexpr int BlockSize = 32;	// samples per block, at the host's rate

	// The 16 Tracks' samples of a block, sample-major: s[n] holds sample n of every Track
	struct alignas(16) Block
	{
		float s[BlockSize][Lanes];
	};

	// The stages, each over the 16 lanes. A ramp goes linearly over the block from _from to _to (per lane), as the
	// firmware's per-sample steps of (to - from) / 32 do

	// _out = gx x + gy y, both gains ramped
	void mix(const Block& _x, const Block& _y, Block& _out, const float* _gxFrom, const float* _gxTo,
		const float* _gyFrom, const float* _gyTo);

	// _buf *= g, g ramped (the modulator's and the amp's VCA)
	void rampedGain(Block& _buf, const float* _from, const float* _to);

	// First-order IIR in place, y = b0 x + b1 x1 + a1 y1, the same coefficients on every lane; _x1, _y1 per lane
	struct OnePole
	{
		float b0, b1, a1;
	};
	void onePole(Block& _buf, const OnePole& _c, float* _x1, float* _y1);

	// _out = a + b, clipped to [-1, 1]
	void sum(const Block& _a, const Block& _b, Block& _out);

	// The oscillators. Phases are 32-bit fractions of a cycle (an integer accumulator, so that a 2 s Hit does not drift
	// from float rounding), increments the same, one per sample: a pitch the machine changes at its 48 kHz blocks'
	// boundaries changes where they fall within a host block. Phase-modulation depths are in cycles per unit of input
	struct alignas(16) Increments
	{
		uint32_t s[BlockSize][Lanes];
	};

	// A sine phase-modulated by its previous sample, the amount ramped from _fb to _fbTarget (left there). _wave: a
	// 256-entry sine (258 values) read as the machine reads its own; nullptr, the sine itself
	void feedbackSine(Block& _out, uint32_t* _phase, const Increments& _inc, float* _fb, const float* _fbTarget,
		float* _last, const float* _wave);

	// A sine phase-modulated by _src (nullptr: none)
	void pmSine(Block& _out, const Block* _src, uint32_t* _phase, const Increments& _inc, float _depth);

	// A 256-entry wave (257 values, linearly interpolated) phase-modulated by _src
	void pmWave(Block& _out, const Block& _src, uint32_t* _phase, const Increments& _inc, float _depth,
		const float* _wave);

	// Drive into a hard clip under a ramped gain: _buf = g post clip(drive x)
	void driveClip(Block& _buf, const float* _from, const float* _to, const float* _drive, const float* _post);

	// The click: a ramp from 1 down by _dec per sample, held at 0, times a level ramped from _from to _to
	void click(Block& _out, float* _ramp, float _dec, const float* _from, const float* _to);

	// BD MODERN on 16 Tracks: a sine at the note phase-modulated by an enveloped mix of two oscillators an octave up,
	// under a pitch sweep, with a filtered click, a drive stage and the amp envelope (.scratch/elektron-engines/
	// bdmodern.md). The same voice as syntakt::bdModern, block for block: a trig is seen in its block (the Track fades
	// out) and sounds from the next; a parameter or note takes effect at the next block. Its block-rate control (the
	// sweep, the envelopes) runs on the machine's own 48 kHz block clock, which the host's blocks and samples read: at
	// any rate each stage lands at the same time in seconds as on the machine.
	class BdModern
	{
	public:
		static constexpr int Parameters = 8;

		// _section7: the words loadSection7 returned, read here only
		BdModern(const std::vector<int32_t>& _section7, float _sampleRate);

		void setSampleRate(float _sampleRate);

		// _track 0-15. The note in semitones; parameter _index 0-7 as VoiceCpu's Machine page (TUNE, the U2 level,
		// the transient, SWEEP, CONTOUR, the drive switch, HOLD, DECAY), _value 0-127
		void setNote(int _track, int _note);
		void setParameter(int _track, int _index, uint8_t _value, uint8_t _fine);

		// For the next block rendered only
		void trig(int _track);
		void gateOff(int _track);

		// One block: 32 samples into each of the 16 _outputs
		void render(float* const* _outputs);

		// Read-only, of the last block on Track _track: the highest frequency in Hz of units 0-2 (_unit 0) or unit 3
		// (_unit 3), and the peak of unit 3's modulator, the enveloped mix, which shifts its phase by as many cycles
		float frequency(int _track, int _unit) const;
		float modulatorPeak(int _track) const;

	private:
		void convert(int _track);
		void control();

		// Section 7's tables BD MODERN reads, as floats: the 128-entry parameter tables, unit 2's wave and the pitch
		// map's exp2; the 256-entry sine unit 0 reads is computed. Ticket 07 of elektron-drums-v1 makes them by formula
		struct Tables
		{
			float sweepDepth[128], sweepDecay[128], clickLevel[128], modDecay[128], ampDecay[128];
			float modTarget[128], feedback[128], levelU0[128], levelU2[128];
			float wave[258];	// 257 and a copy of the last, for a phase that rounds up to 1
			float exp2[2048];
			float sine[258];	// computed: sin(2 pi i / 256)
		};
		Tables m_tables;

		// A block-rate envelope as the machine steps it, once per 48 kHz block: its levels at the ends of 48 kHz blocks
		// k - 1 and k (m_envBlock), between which its ramp runs over block k
		struct Ramp48
		{
			float prev[Lanes]{}, level[Lanes]{};

			void step(const int _l, const float _v)
			{
				prev[_l] = level[_l];
				level[_l] = _v;
			}

			// The ramp _t of the way through block k
			float at(const int _l, const double _t) const
			{
				return prev[_l] + static_cast<float>(_t) * (level[_l] - prev[_l]);
			}
		};

		float driveStep(int _l);

		// The rate's constants
		float m_rate = 48000;
		float m_clickDec = 0;		// per sample
		OnePole m_hp{}, m_lp{};		// the click's filters

		// Each Track's parameters, as words (value << 8 | fine), and note
		int32_t m_word[Parameters][Lanes]{};
		int m_note[Lanes]{};

		// What they convert to (convert): read every block, or latched at a trig (*Next)
		float m_baseInc[Lanes]{};		// cycles per 48 kHz sample at the note and TUNE
		float m_sweepDepth[Lanes]{};	// the units' frequency factor at the trig
		float m_sweepDecayNext[Lanes]{}, m_modDecayNext[Lanes]{};
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
		// The block-rate control runs on the machine's 48 kHz block clock: the time since the trig in 48 kHz blocks,
		// the sweep's block and the envelopes' block
		double m_at[Lanes]{};
		int64_t m_sweepBlock[Lanes]{}, m_envBlock[Lanes]{};
		double m_sweepDecay[Lanes]{}, m_sweep[Lanes]{};	// the sweep at its block
		Ramp48 m_u2, m_mod, m_amp, m_driveGain;
		int m_modStage[Lanes]{}, m_ampStage[Lanes]{}, m_driveStage[Lanes]{};
		float m_modDecay[Lanes]{};
		float m_driveX[Lanes]{};
		bool m_drive[Lanes]{}, m_release[Lanes]{};

		// The envelopes at the ends of the previous and this host block, which the stages ramp between
		alignas(16) float m_u2Level[Lanes]{}, m_u2Prev[Lanes]{};
		alignas(16) float m_modLevel[Lanes]{}, m_modPrev[Lanes]{};
		alignas(16) float m_ampLevel[Lanes]{}, m_ampPrev[Lanes]{};
		alignas(16) float m_driveFrom[Lanes]{}, m_driveTo[Lanes]{}, m_driveAmount[Lanes]{}, m_drivePost[Lanes]{};

		alignas(16) float m_clickRamp[Lanes]{}, m_clickCur[Lanes]{};
		alignas(16) float m_hpX[Lanes]{}, m_hpY[Lanes]{}, m_lpX[Lanes]{}, m_lpY[Lanes]{};

		Block m_b0, m_b1, m_a2, m_mix, m_b3, m_click;
	};
}
