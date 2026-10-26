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

		// 0x40004dc6 as the machine computes it, for the words of the pitch: both products truncated to Q39, their sum
		// to Q31
		int32_t interpolateWord(const int32_t* _table, const int32_t _word)
		{
			const int32_t i = std::max(_word >> 8, 0);
			const int64_t f = static_cast<int64_t>(static_cast<uint32_t>(_word - (i << 8)) << 23);
			const int64_t sum = ((INT32_MAX - f) * _table[i] >> 23) + (f * _table[std::min(i + 1, 127)] >> 23);
			return static_cast<int32_t>(std::clamp<int64_t>(sum >> 8, INT32_MIN, INT32_MAX));
		}
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

	void gain(Block& _buf, const Block& _g)
	{
		for(int n = 0; n < BlockSize; ++n)
			for(int g = 0; g < Lanes; g += 4)
				_mm_store_ps(_buf.s[n] + g, _mm_mul_ps(_mm_load_ps(_buf.s[n] + g), _mm_load_ps(_g.s[n] + g)));
	}

	SdBasic::SdBasic(const float _sampleRate)
	{
		const auto read = [](float* _dst, const auto& _words)
		{
			for(size_t i = 0; i < _words.size(); ++i)
				_dst[i] = q31(_words[i]);
		};
		const auto copy = [](int32_t* _dst, const auto& _words) { std::copy(_words.begin(), _words.end(), _dst); };
		using tables::SdBasic;
		Tables& t = m_tables;
		read(t.decayU0, tables::sdBasic(SdBasic::DecayU0));
		read(t.decayU1, tables::sdBasic(SdBasic::DecayU1));
		read(t.decayU2, tables::sdBasic(SdBasic::DecayU2));
		read(t.decayU3, tables::sdBasic(SdBasic::DecayU3));
		read(t.ampDecay, tables::sdBasic(SdBasic::AmpDecay));
		read(t.modDecay, tables::sdBasic(SdBasic::ModDecay));
		read(t.ampAttack, tables::sdBasic(SdBasic::AmpAttack));
		copy(t.sweepDepth, tables::sdBasic(SdBasic::SweepDepth));
		copy(t.sweepDecay, tables::sdBasic(SdBasic::SweepDecay));
		read(t.feedback, tables::sdBasic(SdBasic::Feedback));
		read(t.targetU0, tables::sdBasic(SdBasic::TargetU0));
		read(t.targetU1, tables::sdBasic(SdBasic::TargetU1));
		copy(t.scaleU0, tables::sdBasic(SdBasic::ScaleU0));
		copy(t.scaleU1, tables::sdBasic(SdBasic::ScaleU1));
		copy(t.scaleU3, tables::sdBasic(SdBasic::ScaleU3));
		read(t.depthU3, tables::sdBasic(SdBasic::DepthU3));
		read(t.lowPass, tables::sdBasic(SdBasic::LowPass));
		copy(t.exp2, tables::exp2());
		for(int i = 0; i < 258; ++i)
			t.sine[i] = static_cast<float>(std::sin(2 * 3.14159265358979323846 * i / 256));

		// The voice before its first trig: silent, the drive's gain at 1 recovering
		for(int l = 0; l < Lanes; ++l)
		{
			m_driveStage[l] = 5;
			m_driveX[l] = 1;
			m_driveGain.prev[l] = m_driveGain.level[l] = 1;
			m_driveFrom[l] = m_driveTo[l] = 1;
			m_modStage[l] = m_ampStage[l] = 1;
		}
		setSampleRate(_sampleRate);
	}

	void SdBasic::setSampleRate(const float _sampleRate)
	{
		m_rate = _sampleRate;
		// The filters keep their cutoffs; the block-rate envelopes keep the machine's coefficients, stepped on its
		// 48 kHz block clock (control)
		const float hp48 = -q31(HpPoleWord);
		const float hpGain = q31(HpGainWord) / ((1 + hp48) / 2);
		const float hp = bilinearPole(hp48, _sampleRate);
		m_hp = {hpGain * (1 + hp) / 2, -hpGain * (1 + hp) / 2, hp};
		for(int i = 0; i < 2; ++i)
			m_lpPole[i] = bilinearPole(-m_tables.lowPass[126 + i], _sampleRate);

		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void SdBasic::setNote(const int _track, const int _note)
	{
		m_note[_track] = _note;
		convert(_track);
	}

	void SdBasic::setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
	{
		if(_index < 0 || _index >= Parameters)
			return;
		m_word[_index][_track] = std::min<int32_t>(_value, 127) << 8 | _fine;
		convert(_track);
	}

	void SdBasic::trig(const int _track)
	{
		m_trigQueued[_track] = true;
	}

	void SdBasic::gateOff(const int _track)
	{
		m_gateQueued[_track] = true;
	}

	void SdBasic::testWithoutFeedback()
	{
		std::fill_n(m_tables.feedback, 128, 0.0f);
		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void SdBasic::convert(const int _l)
	{
		const auto word = [this, _l](const int _i) { return m_word[_i][_l]; };
		const auto byte = [this, _l](const int _i) { return m_word[_i][_l] >> 8; };
		const Tables& t = m_tables;

		// TUNE: (value - 64 + fine / 256) semitones on the note
		m_pitch[_l] = pitchWord(t.exp2, m_note[_l], word(0), 64);

		// The ratios: units 0, 1 and 3 by the second parameter, unit 2 at the note's
		m_scale[0][_l] = interpolateWord(t.scaleU0, word(1));
		m_scale[1][_l] = interpolateWord(t.scaleU1, word(1));
		m_scale[2][_l] = 0x40000000;
		m_scale[3][_l] = interpolateWord(t.scaleU3, word(1));

		// The third: unit 0's feedback (the word Q31 F adds F / 32 of a 2^-24 cycle per unit sample: 4 F cycles),
		// unit 3's modulation by unit 0 (16 D cycles of the word D / 8) and its low-pass
		m_fbTarget[_l] = 4 * interpolate(t.feedback, word(2));
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

	float SdBasic::driveStep(const int _l)
	{
		return driveGain(m_driveStage[_l], m_driveX[_l], m_driveGain.level[_l], m_drive[_l], -q31(FallPoleWord),
			-q31(RisePoleWord));
	}

	void SdBasic::control()
	{
		const double r = MachineRate / m_rate;	// a host block's length in 48 kHz blocks
		const float modAttack = -q31(ModAttackWord);

		for(int l = 0; l < Lanes; ++l)
		{
			const bool now = m_trigNow[l], pending = m_trigPending[l];

			// The update (0x40007c68) and the envelopes' 48 kHz block 0: what a trig restarts and latches
			if(now)
			{
				// The machine puts a Track on SD BASIC at its trig (0x40003ee0): its feedback and unit 3's modulation
				// by unit 0 ramp from 0 in the first block
				if(!m_playing[l])
				{
					m_playing[l] = true;
					m_fb[l] = m_depth2[l] = 0;
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
				m_drive[l] = m_driveNext[l];
				m_at[l] = 0;
				m_sweepBlock[l] = m_envBlock[l] = 0;
				m_sweep[l] = 2147483647.0 / 2147483648.0;

				// The units' envelopes start from 1 without a ramp; the modulator's from 1 too with the drive on, else
				// it rises from half its step, ramped from the silence of the trig's block; the amp's rises from one
				// and a half of its step, ramped too
				m_modStage[l] = 0;
				m_mod.prev[l] = m_drive[l] ? 1.0f : 0.0f;
				m_mod.level[l] = m_drive[l] ? 1.0f : modAttack / 2;
				m_ampStage[l] = 0;
				m_amp.prev[l] = 0;
				m_amp.level[l] = std::min(m_ampAttack[l] / 2 + m_ampAttack[l], 1.0f);
				m_release[l] = false;
				m_driveStage[l] = 1;
				m_driveX[l] = 1;
				m_driveGain.prev[l] = m_driveGain.level[l] = 1;
				m_driveGain.level[l] = driveStep(l);
				m_driveFrom[l] = 1;
			}

			// The units' increments at 48 kHz under the sweep (its level a fraction, from INT32_MAX at the trig), as
			// the machine computes them: in integers, so that a long Hit keeps the machine's phase
			const auto machine = [this, l](const double _sweep, double* _inc)
			{
				const int64_t m = sweepWord(m_sweepDepth[l], _sweep);
				for(int k = 0; k < Units; ++k)
					_inc[k] = increment48(m, Factor[k], m_scale[k][l], m_pitch[l]);
			};
			const double end = m_at[l] + r;
			Increments* const incs[Units] = {&m_inc[0], &m_inc[1], &m_inc[2], &m_inc[3]};
			splitIncrements<Units>(l, m_at[l], r, m_sweepBlock[l], m_sweep[l], m_sweepDecay[l], machine, incs);
			m_at[l] = end;

			// The envelopes step once per 48 kHz block, as on the machine, and each host sample reads their ramps where
			// it falls on the machine's block clock: at 48 kHz the machine's ramps themselves, at another rate the same
			// shapes, which a ramp over the host block would cut at the machine blocks' edges (the modulator's and the
			// amp's fall by half a block at short DECAYs)
			const auto stepEnvelopes = [&]
			{
				for(int k = 0; k < Units; ++k)
				{
					const float a = m_decay[k][l];
					m_env[k].step(l, a * m_env[k].level[l] + (1 - a) * m_target[k][l]);
				}

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

				const float amp = m_amp.level[l];
				if(m_ampStage[l] == 0)
				{
					if(amp == 1)
						m_ampStage[l] = !m_release[l] && m_hold[l] ? 2 : 1;
					m_amp.step(l, amp == 1 ? amp : std::min(amp + m_ampAttack[l], 1.0f));
				}
				else if(m_ampStage[l] == 2)
				{
					if(m_release[l])
					{
						m_ampStage[l] = 1;
						m_release[l] = false;
					}
					m_amp.step(l, amp);
				}
				else
				{
					m_amp.step(l, amp * m_ampDecay[l]);
				}

				m_driveGain.step(l, driveStep(l));
				++m_envBlock[l];
			};
			const double start = end - r, step = r / BlockSize;
			for(int n = 0; n < BlockSize; ++n)
			{
				const double at = start + n * step;
				while(at >= static_cast<double>(m_envBlock[l] + 1))
					stepEnvelopes();
				const double f = at - static_cast<double>(m_envBlock[l]);
				for(int k = 0; k < Units; ++k)
					m_gEnv[k].s[n][l] = m_env[k].at(l, f);
				m_gMod.s[n][l] = m_mod.at(l, f);
				m_gAmp.s[n][l] = m_amp.at(l, f);
			}
			while(end > static_cast<double>(m_envBlock[l] + 1))
				stepEnvelopes();
			if(m_gateOff[l] && !pending)
				m_release[l] = true;

			// The drive's gain ramps over the host block, slow enough
			const double t = end - static_cast<double>(m_envBlock[l]);
			m_driveTo[l] = m_driveGain.at(l, t);

			// A pending block fades the modulator and the amp out from where the last block left them, and leaves them
			// silent until the trig
			if(pending)
			{
				for(int n = 0; n < BlockSize; ++n)
				{
					const float fade = 1 - static_cast<float>(n) / BlockSize;
					m_gMod.s[n][l] = m_modEnd[l] * fade;
					m_gAmp.s[n][l] = m_ampEnd[l] * fade;
				}
				m_mod.prev[l] = m_mod.level[l] = 0;
				m_amp.prev[l] = m_amp.level[l] = 0;
			}
			m_modEnd[l] = m_mod.at(l, t);
			m_ampEnd[l] = m_amp.at(l, t);

			m_driveAmount[l] = m_drive[l] ? 4.0f : 1.0f;
			m_drivePost[l] = m_drive[l] ? 0.5f : 1.0f;
		}
	}

	float SdBasic::frequency(const int _track, const int _unit) const
	{
		uint32_t top = 0;
		for(const auto& s : m_inc[_unit].s)
			top = std::max(top, s[_track]);
		return static_cast<float>(top / 4294967296.0 * m_rate);
	}

	float SdBasic::peak(const int _track, const int _unit) const
	{
		float p = 0;
		for(const auto& s : m_b[_unit].s)
			p = std::max(p, std::abs(s[_track]));
		return p;
	}

	void SdBasic::render(float* const* _outputs)
	{
		const FlushDenormals ftz;

		for(int l = 0; l < Lanes; ++l)
		{
			m_trigPending[l] = m_trigQueued[l];
			m_gateOff[l] = m_gateQueued[l];
			m_trigQueued[l] = m_gateQueued[l] = false;
		}
		control();

		// The units under their envelopes: unit 0 feeding back on itself, unit 1 alone, unit 2 modulated by unit 1,
		// unit 3 by units 1 and 0
		feedbackSine(m_b[0], m_phase[0], m_inc[0], m_fb, m_fbTarget, m_last, m_tables.sine);
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
		driveClip(m_out, m_driveFrom, m_driveTo, m_driveAmount, m_drivePost);

		// The drive's ramp's end is the next block's start
		std::copy_n(m_driveTo, Lanes, m_driveFrom);

		for(int l = 0; l < Lanes; ++l)
		{
			float* out = _outputs[l];
			for(int n = 0; n < BlockSize; ++n)
				out[n] = m_out.s[n][l];
			m_trigNow[l] = m_trigPending[l];
		}
	}
}
