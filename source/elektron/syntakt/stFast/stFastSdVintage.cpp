#include "stFastSdVintage.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "stFastTables.h"

namespace syntakt::fast
{
	using namespace simd;

	namespace
	{
		// The machine's constants by their laws, each against the firmware's word (measured by hand, the word kept out
		// of the source; the tests drive the exact port with it)
		constexpr double Pi = 3.14159265358979323846;
		constexpr double Q31 = 2147483648.0;
		constexpr float UnitAttack = 0.4f;	// the units' attack, a step a block: a round value, the word
		constexpr float HoldRest = 0.3f;	// units 0, 1 and 3's levels at rest with HOLD: a round value, the word
		const auto Gate = static_cast<int32_t>(std::llround(0.33 * Q31));	// the noise's gate: a round value, the word

		// The noise modulator's attack, full in 4 blocks (the firmware's 4.15 blocks: 3.6 % faster); unit 0 in the mix
		// at 0.135, a round value (the firmware's -17.4 dB: 0.077 % louder; -17.5 dB moves the sweep's base sound past
		// Q4's envelope, -32 dB re its peak)
		constexpr float ModAttack = 1 / 4.0f;
		constexpr float Unit0Gain = 0.135f;

		// The pitch sweep rises from half its step, 0.4 a block (a round value), to full, as the machine adds its words
		const int64_t SweepAttackWord = std::llround(0.4 * Q31);
		const double SweepStart = static_cast<double>(SweepAttackWord >> 1) / Q31;
		const double SweepAttack = static_cast<double>(SweepAttackWord) / Q31;
		constexpr double SweepMax = 2147483647.0 / Q31;

		// The drive's gain envelope, one-pole per 48 kHz block: falling by half in 15 ms (22.5 blocks: 1.0e-6 of the
		// pole under the word), recovering with a time constant of 624 ms (2.9e-7 over); its floor 0.25, the clip's
		// pre-gain 4 and the gain after it 0.75 with the drive on (round values, the words)
		const float FallPole = static_cast<float>(std::pow(0.5, 1 / 22.5));
		const float RisePole = static_cast<float>(std::exp(-BlockSize / (MachineRate * 0.624)));
		constexpr float DriveFloor = 0.25f;

		// The noise's high-pass: 2 kHz, Q 0.707 (RBJ), its gain halved as the machine keeps its state at half the
		// output (round values, the words)
		const std::array<float, 5> NoiseHp = []
		{
			const double w = 2 * Pi * 2000 / 48000, alpha = std::sin(w) / (2 * 0.707), a0 = 1 + alpha;
			const double b = (1 + std::cos(w)) / 2 / a0;
			return std::array<float, 5>{static_cast<float>(b / 2), static_cast<float>(-b), static_cast<float>(b / 2),
				static_cast<float>(-2 * std::cos(w) / a0), static_cast<float>((1 - alpha) / a0)};
		}();

		// The noise's fixed low-pass, 4 kHz by the bilinear transform without prewarping, K = pi f / fs (round values,
		// the words); the output's high-pass, a one-pole at 150 Hz, its pole exp(-2 pi f / fs) (6.3e-7 of the pole
		// under the word)
		const OnePole NoiseLp2 = bilinearLowPass(4000);
		const OnePole OutHp = []
		{
			const double p = std::exp(-2 * Pi * 150 / 48000);
			return OnePole{static_cast<float>((1 + p) / 2), static_cast<float>(-(1 + p) / 2), static_cast<float>(p)};
		}();

		// TUNE at 64 plays the note: the machine's offset to the note is -64. Unit 2 plays two octaves up
		constexpr int PitchOffset = 64;
		constexpr int64_t NoteFactor = int64_t(1) << 26;
	}

	SdVintage::SdVintage()
		: m_drive(FallPole, RisePole, DriveFloor, {4.0f, 0.75f}, {1.0f, 1.0f})
	{
		using tables::SdVintage;
		Tables& t = m_tables;
		copyWords(t.ratio0, tables::sdVintage(SdVintage::Ratio0));
		copyWords(t.ratio1, tables::sdVintage(SdVintage::Ratio1));
		copyWords(t.sweepDecay, tables::sdVintage(SdVintage::SweepDecay));
		copyWords(t.sweepDepth, tables::sdVintage(SdVintage::SweepDepth));
		readQ31(t.depth[0], tables::sdVintage(SdVintage::Depth0));
		readQ31(t.depth[1], tables::sdVintage(SdVintage::Depth1));
		readQ31(t.depth[2], tables::sdVintage(SdVintage::Depth2));
		readQ31(t.level[0], tables::sdVintage(SdVintage::Level1));
		readQ31(t.level[1], tables::sdVintage(SdVintage::Level2));
		readQ31(t.level[2], tables::sdVintage(SdVintage::Level3));
		readQ31(t.noiseLp, tables::sdVintage(SdVintage::NoiseLp));
		readQ31(t.feedback, tables::sdVintage(SdVintage::Feedback));
		readQ31(t.modDecay, tables::sdVintage(SdVintage::ModDecay));
		readQ31(t.decay2, tables::sdVintage(SdVintage::Decay2));
		readQ31(t.decay01, tables::sdVintage(SdVintage::Decay01));
		readQ31(t.decay3, tables::sdVintageDecay3());
		copyWords(t.exp2, tables::exp2());
		readQ31(t.waveU2, tables::sdVintageWaveU2());
		readQ31(t.waveU3, tables::sdVintageWaveU3());
		t.waveU2[257] = t.waveU2[256];
		t.waveU3[257] = t.waveU3[256];
		sineTable(t.sine);

		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void SdVintage::setNote(const int _track, const int _note)
	{
		m_controls.notes[_track] = _note;
		convert(_track);
	}

	void SdVintage::setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
	{
		if(m_controls.setParameter(_track, _index, _value, _fine))
			convert(_track);
	}

	void SdVintage::testWithoutChaos()
	{
		std::fill_n(m_tables.feedback, 128, 0.0f);
		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void SdVintage::convert(const int _l)
	{
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		const auto byte = [this, _l](const int _i) { return m_controls.words[_i][_l] >> 8; };
		const Tables& t = m_tables;

		// TUNE: (value - 64 + fine / 256) semitones on the note
		m_pitch[_l] = pitchWord(t.exp2, m_controls.notes[_l], word(0), PitchOffset);

		// The second: units 1 and 3's frequency factors; unit 0 at the note's, unit 2 two octaves up
		m_factor[0][_l] = NoteFactor;
		m_factor[1][_l] = interpolateWord(t.ratio1, word(1));
		m_factor[2][_l] = 4 * NoteFactor;
		m_factor[3][_l] = interpolateWord(t.ratio0, word(1));

		// The third: units 0-2's modulation by unit 3, unit 3's feedback, the noise's low-pass (y = (1 + c) / 2
		// (x + x1) - c y1) and the levels in the mix
		for(int k = 0; k < 3; ++k)
			m_depthTarget[k][_l] = interpolate(t.depth[k], word(2));
		m_fbTarget[_l] = interpolate(t.feedback, word(2));
		const float lp = interpolate(t.noiseLp, word(2));
		m_lpB[_l] = (1 + lp) / 2;
		m_lpA[_l] = -lp;
		m_gainTarget[0][_l] = Unit0Gain;
		for(int k = 0; k < 3; ++k)
			m_gainTarget[k + 1][_l] = interpolate(t.level[k], word(2));

		// SWEEP: its depth and decay (per 48 kHz block) at a trig
		m_sweepDepthNext[_l] = t.sweepDepth[byte(3)];
		m_sweepDecayNext[_l] = -t.sweepDecay[byte(3)] / Q31;

		// The fifth with DECAY: unit 3's attack (none at 127, which leaves it silent) and decay; DECAY: the units',
		// the modulator's, at a trig
		const int b10 = byte(4), b16 = byte(7);
		m_attack3Next[_l] = b10 <= 126 ? UnitAttack : 0.0f;
		m_decayNext[0][_l] = m_decayNext[1][_l] = -t.decay01[b16];
		m_decayNext[2][_l] = -t.decay2[b16];
		m_decayNext[3][_l] = -t.decay3[b16 << 7 | b10];
		m_modDecayNext[_l] = -t.modDecay[b16];

		m_driveNext[_l] = word(5) != 0;
		m_hold[_l] = word(6) != 0;
	}

	void SdVintage::control()
	{
		for(int l = 0; l < m_lanes; ++l)
		{
			Trigs& trigs = m_controls.trigs;
			const bool now = trigs.now[l];
			bool pending = trigs.pending[l];

			// The update (0x40008074) and the envelopes' 48 kHz block 0: what a trig restarts and latches
			if(now)
			{
				// The machine puts a Track on SD VINTAGE at its trig (0x40003ee0): unit 3's feedback and units 0-2's
				// modulation ramp from 0 in the first block, the noise starts from its seed; a trig or a gate-off in
				// that block is lost (Trigs::machineChange)
				if(!m_playing[l])
				{
					trigs.machineChange(l);
					pending = false;
					m_playing[l] = true;
					m_fb[l] = 0;
					for(auto& d : m_depth)
						d[l] = 0;
					m_noise.start(l);
				}
				for(auto& p : m_phase)
					p[l] = 0;
				m_last[l] = 0;
				for(auto& s : m_bq)
					s[l] = 0;
				m_lp1X[l] = m_lp1Y[l] = m_lp2X[l] = m_lp2Y[l] = m_hpX[l] = m_hpY[l] = 0;
				for(int k = 0; k < Units; ++k)
				{
					m_decay[k][l] = m_decayNext[k][l];
					m_target[k][l] = k != 2 && m_hold[l] ? HoldRest : 0.0f;
				}
				m_attack3[l] = m_attack3Next[l];
				m_modDecay[l] = m_modDecayNext[l];
				m_sweepDepth[l] = m_sweepDepthNext[l];
				m_sweepDecay[l] = m_sweepDecayNext[l];
				m_sweep[l] = SweepStart;
				m_sweepRising[l] = true;
				m_drive.trig(l, m_driveNext[l]);
				m_clock.restart(l);

				// The units' envelopes start from their attack without a ramp, the modulator's ramps from the trig's
				// block's silence to half its step, the amp starts full: held with HOLD, which units 0 and 1's decay
				// then ends, else held full
				for(int k = 0; k < 3; ++k)
					m_env[k].trig(l, UnitAttack, UnitAttack);
				m_env[3].trig(l, m_attack3[l], m_attack3[l]);
				m_mod.trig(l, 0, ModAttack / 2);
				m_amp.trigFull(l, m_hold[l]);
				m_ampDecay[l] = m_hold[l] ? m_decay[0][l] : 1.0f;

				// No ramp of the mix's levels at the trig
				for(int i = 0; i < 4; ++i)
					m_gain[i][l] = m_gainTarget[i][l];
			}

			// The sweep (0x40003cda) before the increments: up by its attack to full, held a block, then its decay
			if(!m_sweepRising[l])
				m_sweep[l] *= m_sweepDecay[l];
			else if(m_sweep[l] == SweepMax)
				m_sweepRising[l] = false;
			else
				m_sweep[l] = std::min(m_sweep[l] + SweepAttack, SweepMax);

			// The units' increments at 48 kHz under the sweep, as the machine computes them in integers
			const int64_t m = sweepWord(m_sweepDepth[l], m_sweep[l]);
			for(int k = 0; k < Units; ++k)
				setIncrement(m_inc[k], l, m, m_factor[k][l], int64_t(1) << 30, m_pitch[l]);

			// The envelopes on the block clock; each sample reads their ramps
			for(int s = m_clock.advance(l); s > 0; --s)
			{
				for(int k = 0; k < Units; ++k)
					m_env[k].stepRise(l, k == 3 ? m_attack3[l] : UnitAttack, m_decay[k][l], m_target[k][l]);
				m_mod.stepRise(l, ModAttack, m_modDecay[l], 0);
				m_amp.stepAmp(l, m_ampDecay[l], 0, m_hold[l]);
				m_drive.step(l);
			}
			for(int n = 0; n < BlockSize; ++n)
			{
				const double f = n / static_cast<double>(BlockSize);
				for(int k = 0; k < Units; ++k)
					m_gEnv[k].s[n][l] = m_env[k].at(l, f);
				m_gMod.s[n][l] = m_mod.at(l, f);
				m_gAmp.s[n][l] = m_amp.at(l, f);
			}
			if(trigs.releases(l))
				m_amp.release(l);

			// The drive's gain ramps over the block, slow enough
			m_drive.end(l);

			// A pending block fades the modulator and the amp out, and leaves them silent until the trig
			fadePending(m_gMod, m_mod, m_modEnd, l, pending);
			fadePending(m_gAmp, m_amp, m_ampEnd, l, pending);
		}
	}

	void SdVintage::setLanes(const int _lanes)
	{
		m_lanes = _lanes;
		fast::setLanes(_lanes, {&m_gEnv[0], &m_gEnv[1], &m_gEnv[2], &m_gEnv[3], &m_b[0], &m_b[1], &m_b[2], &m_b[3],
			&m_gMod, &m_gAmp, &m_scaled3, &m_noiseOut, &m_out});
	}

	const Block& SdVintage::renderBlock()
	{
		const FlushDenormals ftz;

		m_controls.trigs.start();
		control();

		// Unit 3 feeding back on its own output under its envelope, which modulates units 0-2, each under its own
		const Tables& t = m_tables;
		feedbackWave(m_b[3], m_scaled3, m_gEnv[3], m_last, m_fb, m_fbTarget, m_phase[3], m_inc[3], t.waveU3);
		const float* waves[3] = {t.sine, t.sine, t.waveU2};
		for(int k = 0; k < 3; ++k)
		{
			pmWave(m_b[k], &m_scaled3, m_depth[k], m_depthTarget[k], m_phase[k], m_inc[k], waves[k]);
			gain(m_b[k], m_gEnv[k]);
			std::copy_n(m_depthTarget[k], m_lanes, m_depth[k]);
		}

		// The noise, gated, high-passed into its clip and doubled, low-passed twice, under the modulator's envelope
		m_noise.render(m_noiseOut, Gate);
		biquad(m_noiseOut, NoiseHp.data(), m_bq);
		saturate(m_noiseOut, 2);
		onePoleLanes(m_noiseOut, m_lpB, m_lpA, m_lp1X, m_lp1Y);
		onePole(m_noiseOut, NoiseLp2, m_lp2X, m_lp2Y);
		gain(m_noiseOut, m_gMod);

		// Mixed with units 0-2, high-passed, doubled into the drive and the amp
		const Block* in[4] = {&m_b[0], &m_b[1], &m_b[2], &m_noiseOut};
		mix4(in, m_out, m_gain, m_gainTarget);
		for(int i = 0; i < 4; ++i)
			std::copy_n(m_gainTarget[i], m_lanes, m_gain[i]);
		onePole(m_out, OutHp, m_hpX, m_hpY);
		saturate(m_out, 2);
		driveClip(m_out, m_drive.from, m_drive.to, m_drive.amount, m_drive.post);
		gain(m_out, m_gAmp);

		// The drive's ramp's end is the next block's start
		m_drive.advance();

		m_controls.trigs.end();
		return m_out;
	}
}
