#include "stFastSdBasic.h"

#include <algorithm>
#include <cmath>

#include "stFastTables.h"

namespace syntakt::fast
{
	using namespace simd;

	namespace
	{
		// The firmware's constants at 48 kHz, as Q31 words
		constexpr int32_t ModAttackWord = static_cast<int32_t>(0xc028db9c);	// the modulator's rise, drive off
		constexpr int32_t HpGainWord = 0x4b9047ab;							// unit 2's high-pass: c0, and -pole
		constexpr int32_t HpPoleWord = static_cast<int32_t>(0x841f111a);
		constexpr int32_t LpGainWord = 0x33333333;							// unit 3's low-pass: 0.4 at DC
		constexpr int32_t FallPoleWord = static_cast<int32_t>(0x83e21c3a);	// the drive's gain envelope
		constexpr int32_t RisePoleWord = static_cast<int32_t>(0x803ef8b8);

		// The units' frequency factors (their 0x50, 0x04000000 the note's) and units 2 and 3's modulation depths in
		// cycles per unit of input: unit 2 by unit 1 at frac(0x01000000, 0x1d70a3d7), unit 3 by unit 1 at 0.25
		constexpr int64_t Factor[SdBasic::Units] = {0x08000000, 0x08000000, 0x04000000, 0x0c000000};
		const float DepthU2 = static_cast<float>(std::floor(0x1d70a3d7 / 128.0) * 128 / 2147483648.0);
		constexpr float DepthU3 = 0.25f;

		// Unit 3's low-pass reads its table at (frac(0x7f000000, w) + 0x7f00 - w) >> 8 of the third parameter's word
		// w: entry 127 at 0, 126 above
		int lowPassEntry(const int32_t _word) { return _word == 0 ? 127 : 126; }
	}

	void pmSine2(Block& _out, const Block& _src1, const float _depth1, const Block& _src2, float* _depth2,
		const float* _depth2Target, uint32_t* _phase, const Increments& _inc)
	{
		const V depth1 = _mm_set1_ps(_depth1);
		for(int g = 0; g < Lanes; g += 4)
		{
			__m128i p = _mm_load_si128(reinterpret_cast<const __m128i*>(_phase + g));
			V depth2 = _mm_load_ps(_depth2 + g);
			const V step = stepOf(_depth2, _depth2Target, g);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V m = _mm_add_ps(_mm_mul_ps(depth1, _mm_load_ps(_src1.s[n] + g)),
					_mm_mul_ps(depth2, _mm_load_ps(_src2.s[n] + g)));
				_mm_store_ps(_out.s[n] + g, sin2pi(_mm_add_ps(phaseOf(p), m)));
				p = _mm_add_epi32(p, _mm_load_si128(reinterpret_cast<const __m128i*>(_inc.s[n] + g)));
				depth2 = _mm_add_ps(depth2, step);
			}
			_mm_store_si128(reinterpret_cast<__m128i*>(_phase + g), p);
		}
		std::copy_n(_depth2Target, Lanes, _depth2);
	}

	void onePoleLanes(Block& _buf, const float* _b, const float* _a, float* _x1, float* _y1)
	{
		for(int g = 0; g < Lanes; g += 4)
		{
			const V b = _mm_load_ps(_b + g), a = _mm_load_ps(_a + g);
			V x1 = _mm_load_ps(_x1 + g), y1 = _mm_load_ps(_y1 + g);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V x = _mm_load_ps(_buf.s[n] + g);
				y1 = _mm_add_ps(_mm_mul_ps(b, _mm_add_ps(x, x1)), _mm_mul_ps(a, y1));
				x1 = x;
				_mm_store_ps(_buf.s[n] + g, y1);
			}
			_mm_store_ps(_x1 + g, x1);
			_mm_store_ps(_y1 + g, y1);
		}
	}

	SdBasic::SdBasic() : m_drive(-q31(FallPoleWord), -q31(RisePoleWord), 0.5f, {4.0f, 0.5f}, {1.0f, 1.0f})
	{
		using tables::SdBasic;
		Tables& t = m_tables;
		readQ31(t.decayU0, tables::sdBasic(SdBasic::DecayU0));
		readQ31(t.decayU1, tables::sdBasic(SdBasic::DecayU1));
		readQ31(t.decayU2, tables::sdBasic(SdBasic::DecayU2));
		readQ31(t.decayU3, tables::sdBasic(SdBasic::DecayU3));
		readQ31(t.ampDecay, tables::sdBasic(SdBasic::AmpDecay));
		readQ31(t.modDecay, tables::sdBasic(SdBasic::ModDecay));
		readQ31(t.ampAttack, tables::sdBasic(SdBasic::AmpAttack));
		copyWords(t.sweepDepth, tables::sdBasic(SdBasic::SweepDepth));
		copyWords(t.sweepDecay, tables::sdBasic(SdBasic::SweepDecay));
		copyWords(t.feedback, tables::sdBasic(SdBasic::Feedback));
		readQ31(t.targetU0, tables::sdBasic(SdBasic::TargetU0));
		readQ31(t.targetU1, tables::sdBasic(SdBasic::TargetU1));
		copyWords(t.scaleU0, tables::sdBasic(SdBasic::ScaleU0));
		copyWords(t.scaleU1, tables::sdBasic(SdBasic::ScaleU1));
		copyWords(t.scaleU3, tables::sdBasic(SdBasic::ScaleU3));
		readQ31(t.depthU3, tables::sdBasic(SdBasic::DepthU3));
		readQ31(t.lowPass, tables::sdBasic(SdBasic::LowPass));
		copyWords(t.exp2, tables::exp2());

		// The voice before its first trig: silent, the drive's gain at 1 recovering (Drive)
		for(int l = 0; l < Lanes; ++l)
			m_modStage[l] = 1;

		const float hp = -q31(HpPoleWord);
		const float hpGain = q31(HpGainWord) / ((1 + hp) / 2);
		m_hp = {hpGain * (1 + hp) / 2, -hpGain * (1 + hp) / 2, hp};
		for(int i = 0; i < 2; ++i)
			m_lpPole[i] = -m_tables.lowPass[126 + i];
		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void SdBasic::setNote(const int _track, const int _note)
	{
		m_controls.notes[_track] = _note;
		convert(_track);
	}

	void SdBasic::setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
	{
		if(m_controls.setParameter(_track, _index, _value, _fine))
			convert(_track);
	}

	void SdBasic::testWithoutChaos()
	{
		std::fill_n(m_tables.feedback, 128, 0);
		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void SdBasic::convert(const int _l)
	{
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		const auto byte = [this, _l](const int _i) { return m_controls.words[_i][_l] >> 8; };
		const Tables& t = m_tables;

		// TUNE: (value - 64 + fine / 256) semitones on the note
		m_pitch[_l] = pitchWord(t.exp2, m_controls.notes[_l], word(0), 64);

		// The ratios: units 0, 1 and 3 by the second parameter, unit 2 at the note's
		m_scale[0][_l] = interpolateWord(t.scaleU0, word(1));
		m_scale[1][_l] = interpolateWord(t.scaleU1, word(1));
		m_scale[2][_l] = 0x40000000;
		m_scale[3][_l] = interpolateWord(t.scaleU3, word(1));

		// The third: unit 0's feedback (the word Q31 F adds F / 32 of a 2^-24 cycle per unit sample: 4 F cycles),
		// unit 3's modulation by unit 0 (16 D cycles of the word D / 8) and its low-pass
		m_fbWord[_l] = interpolateWord(t.feedback, word(2));
		m_depth2Target[_l] = 16 * interpolate(t.depthU3, word(2));
		const float lp = m_lpPole[lowPassEntry(word(2)) - 126];
		m_lpA[_l] = lp;
		m_lpB[_l] = q31(LpGainWord) * (1 - lp) / 2;

		m_sweepDepthNext[_l] = t.sweepDepth[byte(3)];
		m_sweepDecayNext[_l] = -t.sweepDecay[byte(3)] / 2147483648.0;	// per 48 kHz block

		// The fifth: units 0 and 1's levels at rest, unit 3's decay; units 2 and 3 rest at 0.5 and 0.25
		m_target[0][_l] = interpolate(t.targetU0, word(4));
		m_target[1][_l] = interpolate(t.targetU1, word(4));
		m_target[2][_l] = 0.5f;
		m_target[3][_l] = 0.25f;
		m_decayNext[3][_l] = -t.decayU3[byte(4)];

		m_driveNext[_l] = word(5) != 0;
		m_hold[_l] = word(6) != 0;

		// DECAY: units 0-2's, the modulator's and the amp's decays, the amp's attack
		m_decayNext[0][_l] = -t.decayU0[byte(7)];
		m_decayNext[1][_l] = -t.decayU1[byte(7)];
		m_decayNext[2][_l] = -t.decayU2[byte(7)];
		m_modDecay[_l] = -t.modDecay[byte(7)];
		m_ampDecay[_l] = -t.ampDecay[byte(7)];
		m_ampAttackNext[_l] = -t.ampAttack[byte(7)];
	}

	void SdBasic::control()
	{
		const float modAttack = -q31(ModAttackWord);

		for(int l = 0; l < Lanes; ++l)
		{
			const Trigs& trigs = m_controls.trigs;
			const bool now = trigs.now[l], pending = trigs.pending[l];

			// The update (0x40007c68) and the envelopes' 48 kHz block 0: what a trig restarts and latches
			if(now)
			{
				// The machine puts a Track on SD BASIC at its trig (0x40003ee0): its feedback and unit 3's modulation
				// by unit 0 ramp from 0 in the first block
				if(!m_playing[l])
				{
					m_playing[l] = true;
					m_fb[l] = 0;
					m_depth2[l] = 0;
				}
				for(auto& p : m_phase)
					p[l] = 0;
				m_last[l] = 0;
				m_hpX[l] = m_hpY[l] = m_lpX[l] = m_lpY[l] = 0;
				for(int k = 0; k < Units; ++k)
				{
					m_decay[k][l] = m_decayNext[k][l];
					m_env[k].prev[l] = m_env[k].level[l] = 1;
				}
				m_sweepDepth[l] = m_sweepDepthNext[l];
				m_sweepDecay[l] = m_sweepDecayNext[l];
				m_ampAttack[l] = m_ampAttackNext[l];
				m_drive.trig(l, m_driveNext[l]);
				m_clock.restart(l);
				m_sweep[l] = 2147483647.0 / 2147483648.0;

				// The units' envelopes start from 1 without a ramp; the modulator's from 1 too with the drive on, else
				// it rises from half its step, ramped from the silence of the trig's block; the amp's rises from one
				// and a half of its step, ramped too; the drive's gain falls from 1
				m_modStage[l] = 0;
				m_mod.prev[l] = m_drive.on[l] ? 1.0f : 0.0f;
				m_mod.level[l] = m_drive.on[l] ? 1.0f : modAttack / 2;
				m_amp.trigRise(l, m_ampAttack[l]);
			}

			// The units' increments at 48 kHz under the sweep (its level a fraction, from INT32_MAX at the trig), as
			// the machine computes them: in integers, so that a long Hit keeps the machine's phase. The machine steps
			// the sweep at the block's end
			const int64_t m = sweepWord(m_sweepDepth[l], m_sweep[l]);
			for(int k = 0; k < Units; ++k)
				setIncrement(m_inc[k], l, m, Factor[k], m_scale[k][l], m_pitch[l]);
			m_sweep[l] *= m_sweepDecay[l];

			// The envelopes on the block clock; each sample reads their ramps
			for(int s = m_clock.advance(l); s > 0; --s)
			{
				for(int k = 0; k < Units; ++k)
					m_env[k].stepToward(l, m_decay[k][l], m_target[k][l]);

				const float mod = m_mod.level[l];
				if(m_modStage[l] != 0)
					m_mod.step(l, m_modDecay[l] * mod);
				else if(mod == 1)
				{
					m_modStage[l] = 1;
					m_mod.step(l, mod);
				}
				else
					m_mod.step(l, std::min(mod + modAttack, 1.0f));

				m_amp.stepAmp(l, m_ampDecay[l], m_ampAttack[l], m_hold[l]);
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

	const Block& SdBasic::renderBlock()
	{
		const FlushDenormals ftz;

		m_controls.trigs.start();
		control();

		// The units under their envelopes: unit 0 feeding back on itself, unit 1 alone, unit 2 modulated by unit 1,
		// unit 3 by units 1 and 0
		feedbackSine(m_b[0], m_phase[0], m_inc[0], m_fb, m_fbWord, m_last);
		gain(m_b[0], m_gEnv[0]);
		pmSine(m_b[1], nullptr, m_phase[1], m_inc[1], 0);
		gain(m_b[1], m_gEnv[1]);
		pmSine(m_b[2], &m_b[1], m_phase[2], m_inc[2], DepthU2);
		gain(m_b[2], m_gEnv[2]);
		pmSine2(m_b[3], m_b[1], DepthU3, m_b[0], m_depth2, m_depth2Target, m_phase[3], m_inc[3]);
		gain(m_b[3], m_gEnv[3]);

		// Unit 2 high-passed under the modulator's envelope, unit 3 low-passed under the amp's, mixed at 1 and 0.5,
		// into the drive
		onePole(m_b[2], m_hp, m_hpX, m_hpY);
		gain(m_b[2], m_gMod);
		onePoleLanes(m_b[3], m_lpB, m_lpA, m_lpX, m_lpY);
		gain(m_b[3], m_gAmp);
		alignas(16) static constexpr float One[Lanes] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
		alignas(16) static constexpr float Half[Lanes] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
			0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
		mix(m_b[2], m_b[3], m_out, One, One, Half, Half);
		driveClip(m_out, m_drive.from, m_drive.to, m_drive.amount, m_drive.post);

		// The drive's ramp's end is the next block's start
		m_drive.advance();

		m_controls.trigs.end();
		return m_out;
	}
}
