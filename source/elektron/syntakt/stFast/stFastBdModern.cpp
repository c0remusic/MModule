#include "stFastBdModern.h"

#include <algorithm>
#include <cmath>

#include "stFastTables.h"

namespace syntakt::fast
{
	using namespace simd;

	namespace
	{
		// The firmware's constants at 48 kHz, as Q31 words: poles of the block-rate filters, the click's step
		constexpr int32_t U2DecayWord = static_cast<int32_t>(0x8aa5ca3b);	// negative: the coefficient is -c
		constexpr int32_t ClickStepWord = 0x004468c0;
		constexpr int32_t HpPoleWord = static_cast<int32_t>(0x841f111a);	// -pole
		constexpr int32_t LpPoleWord = static_cast<int32_t>(0xafb69397);
		constexpr int32_t FallPoleWord = static_cast<int32_t>(0x85c7e84d);
		constexpr int32_t RisePoleWord = static_cast<int32_t>(0x809d339f);
	}

	void sum(const Block& _a, const Block& _b, Block& _out)
	{
		for(int n = 0; n < BlockSize; ++n)
			for(int g = 0; g < Lanes; g += 4)
				_mm_store_ps(_out.s[n] + g, clip(_mm_add_ps(_mm_load_ps(_a.s[n] + g), _mm_load_ps(_b.s[n] + g))));
	}

	void pmWave(Block& _out, const Block& _src, uint32_t* _phase, const Increments& _inc, const float _depth,
		const float* _wave)
	{
		const V depth = _mm_set1_ps(_depth);
		for(int g = 0; g < Lanes; g += 4)
		{
			__m128i p = _mm_load_si128(reinterpret_cast<const __m128i*>(_phase + g));
			for(int n = 0; n < BlockSize; ++n)
			{
				const V u = _mm_add_ps(phaseOf(p), _mm_mul_ps(depth, _mm_load_ps(_src.s[n] + g)));
				_mm_store_ps(_out.s[n] + g, lookup(u, _wave));
				p =_mm_add_epi32(p, _mm_load_si128(reinterpret_cast<const __m128i*>(_inc.s[n] + g)));
			}
			_mm_store_si128(reinterpret_cast<__m128i*>(_phase + g), p);
		}
	}

	void click(Block& _out, float* _ramp, const float _dec, const float* _from, const float* _to)
	{
		const V dec = _mm_set1_ps(_dec), zero = _mm_setzero_ps();
		for(int g = 0; g < Lanes; g += 4)
		{
			V r = _mm_load_ps(_ramp + g), v = ramp(_from, g);
			const V s = stepOf(_from, _to, g);
			for(int n = 0; n < BlockSize; ++n)
			{
				_mm_store_ps(_out.s[n] + g, _mm_mul_ps(v, r));
				r = _mm_max_ps(_mm_sub_ps(r, dec), zero);
				v = _mm_add_ps(v, s);
			}
			_mm_store_ps(_ramp + g, r);
		}
	}

	BdModern::BdModern(const float _sampleRate)
	{
		const auto read = [](float* _dst, const auto& _words)
		{
			for(size_t i = 0; i < _words.size(); ++i)
				_dst[i] = q31(_words[i]);
		};
		const auto copy = [](int32_t* _dst, const auto& _words) { std::copy(_words.begin(), _words.end(), _dst); };
		using tables::Parameter;
		copy(m_tables.sweepDepth, tables::parameter(Parameter::SweepDepth));
		copy(m_tables.sweepDecay, tables::parameter(Parameter::SweepDecay));
		read(m_tables.clickLevel, tables::parameter(Parameter::ClickLevel));
		read(m_tables.modDecay, tables::parameter(Parameter::ModDecay));
		read(m_tables.ampDecay, tables::parameter(Parameter::AmpDecay));
		read(m_tables.modTarget, tables::parameter(Parameter::ModTarget));
		read(m_tables.feedback, tables::parameter(Parameter::Feedback));
		read(m_tables.levelU0, tables::parameter(Parameter::LevelU0));
		read(m_tables.levelU2, tables::parameter(Parameter::LevelU2));
		read(m_tables.wave, tables::wave());
		copy(m_tables.exp2, tables::exp2());
		m_tables.wave[257] = m_tables.wave[256];
		for(int i = 0; i < 258; ++i)
			m_tables.sine[i] = static_cast<float>(std::sin(2 * 3.14159265358979323846 * i / 256));

		// The boot's voice: silent, the drive's gain at 1 recovering
		for(int l = 0; l < Lanes; ++l)
		{
			m_driveStage[l] = 5;
			m_driveX[l] = 1;
			m_driveGain.prev[l] = m_driveGain.level[l] = 1;
			m_driveFrom[l] = m_driveTo[l] = 1;
			m_modStage[l] = 1;
		}
		setSampleRate(_sampleRate);
	}

	void BdModern::setSampleRate(const float _sampleRate)
	{
		m_rate = _sampleRate;
		// The click's step per sample scales with the sample's length; the block-rate envelopes keep the machine's
		// coefficients, stepped on its 48 kHz block clock (control)
		m_clickDec = q31(ClickStepWord) * MachineRate / _sampleRate;

		const float hp = bilinearPole(-q31(HpPoleWord), _sampleRate);
		const float lp = bilinearPole(-q31(LpPoleWord), _sampleRate);
		m_hp = {(1 + hp) / 2, -(1 + hp) / 2, hp};
		m_lp = {(1 - lp) / 2, (1 - lp) / 2, lp};

		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void BdModern::setNote(const int _track, const int _note)
	{
		m_note[_track] = _note;
		convert(_track);
	}

	void BdModern::setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
	{
		if(_index < 0 || _index >= Parameters)
			return;
		m_word[_index][_track] = std::min<int32_t>(_value, 127) << 8 | _fine;
		convert(_track);
	}

	void BdModern::trig(const int _track)
	{
		m_trigQueued[_track] = true;
	}

	void BdModern::gateOff(const int _track)
	{
		m_gateQueued[_track] = true;
	}

	void BdModern::testWithoutFeedback()
	{
		std::fill_n(m_tables.feedback, 128, 0.0f);
		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void BdModern::convert(const int _l)
	{
		const auto word =[this, _l](const int _i) { return m_word[_i][_l]; };
		const auto byte = [this, _l](const int _i) { return m_word[_i][_l] >> 8; };
		const Tables& t = m_tables;

		// TUNE: (value - 88 + fine / 256) semitones on the note
		m_pitch[_l] = pitchWord(t.exp2, m_note[_l], word(0), 88);

		m_g0[_l] = interpolate(t.levelU2, word(1));
		m_g1[_l] = interpolate(t.levelU0, word(2));
		m_clickTarget[_l] = interpolate(t.clickLevel, word(2));
		// The feedback word Q31 F adds F / 32 of a 2^-24 cycle per unit sample: 4 F cycles
		m_fbTarget[_l] = 4 * interpolate(t.feedback, word(2));
		m_sweepDepth[_l] = t.sweepDepth[byte(3)];
		m_sweepDecayNext[_l] = -t.sweepDecay[byte(3)] / 2147483648.0;	// per 48 kHz block
		m_modTarget[_l] = interpolate(t.modTarget, word(4));
		m_modDecayNext[_l] = -t.modDecay[byte(4)];
		m_driveNext[_l] = word(5) != 0;
		m_hold[_l] = word(6) != 0;
		m_ampDecay[_l] = -t.ampDecay[byte(7)];
	}

	// The drive's gain envelope, one 48 kHz block, at BD MODERN's poles
	float BdModern::driveStep(const int _l)
	{
		return driveGain(m_driveStage[_l], m_driveX[_l], m_driveGain.level[_l], m_drive[_l], -q31(FallPoleWord),
			-q31(RisePoleWord));
	}

	void BdModern::control()
	{
		const float u2Decay = -q31(U2DecayWord);
		const double r = MachineRate / m_rate;	// a host block's length in 48 kHz blocks

		for(int l = 0; l < Lanes; ++l)
		{
			const bool now = m_trigNow[l], pending = m_trigPending[l];

			// The update (0x40006968): what a trig restarts and latches, and each envelope's 48 kHz block 0
			if(now)
			{
				for(auto& p : m_phase)
					p[l] = 0;
				m_last[l] = 0;
				m_hpX[l] = m_hpY[l] = m_lpX[l] = m_lpY[l] = 0;
				m_g0Cur[l] = m_g0[l];
				m_g1Cur[l] = m_g1[l];
				m_clickCur[l] = m_clickTarget[l];
				m_clickRamp[l] = 1;
				m_sweepDecay[l] = m_sweepDecayNext[l];
				m_modDecay[l] = m_modDecayNext[l];
				m_drive[l] = m_driveNext[l];
				m_at[l] = 0;
				m_sweepBlock[l] = m_envBlock[l] = 0;
				m_sweep[l] = 2147483647.0 / 2147483648.0;

				// Each envelope starts from 1 without a ramp: unit 2's toward 0.75, the modulator's held a block more,
				// the amp's held while HOLD is on, the drive's gain falling from 1
				m_u2.prev[l] = m_u2.level[l] = 1;
				m_mod.prev[l] = m_mod.level[l] = 1;
				m_modStage[l] = 0;
				m_amp.prev[l] = m_amp.level[l] = 1;
				m_ampStage[l] = m_hold[l] ? 2 : 1;
				m_release[l] = false;
				m_driveStage[l] = 1;
				m_driveX[l] = 1;
				m_driveGain.prev[l] = m_driveGain.level[l] = 1;
				m_driveGain.level[l] = driveStep(l);
				m_u2Prev[l] = m_modPrev[l] = m_driveFrom[l] = 1;
				if(!pending)
					m_ampPrev[l] = 1;
			}

			// The units' increments at 48 kHz, as the machine computes them (increment48): unit 3 at the note, units
			// 0-2 an octave up, all under the sweep, which the machine steps once per 48 kHz block (a^k over its block
			// k)
			const auto machine = [this, l](const double _sweep, double* _inc)
			{
				const int64_t m = sweepWord(m_sweepDepth[l], _sweep);
				_inc[0] = increment48(m, 0x04000000, 0x40000000, m_pitch[l]);
				_inc[1] = increment48(m, 0x08000000, 0x40000000, m_pitch[l]);
			};
			const double end = m_at[l] + r;
			Increments* const incs[2] = {&m_inc3, &m_inc012};
			splitIncrements<2>(l, m_at[l], r, m_sweepBlock[l], m_sweep[l], m_sweepDecay[l], machine, incs);
			m_at[l] = end;

			// The envelopes step once per 48 kHz block, as on the machine, up to the block this host block ends in; the
			// host block ramps to where their ramps are at its end. At 48 kHz, the machine's blocks themselves; at
			// another rate, every stage (the trig's block, the modulator's hold, the drive's switch at its floor) lands
			// at the same time in seconds
			while(end > static_cast<double>(m_envBlock[l] + 1))
			{
				m_u2.step(l, u2Decay * m_u2.level[l] + (1 - u2Decay) * 0.75f);

				if(m_modStage[l] == 0)
				{
					m_modStage[l] = 1;
					m_mod.step(l, m_mod.level[l]);
				}
				else
				{
					m_mod.step(l, m_modDecay[l] * m_mod.level[l] + (1 - m_modDecay[l]) * m_modTarget[l]);
				}

				if(m_ampStage[l] == 2)
				{
					if(m_release[l])
					{
						m_ampStage[l] = 1;
						m_release[l] = false;
					}
					m_amp.step(l, m_amp.level[l]);
				}
				else
				{
					m_amp.step(l, m_amp.level[l] * m_ampDecay[l]);
				}

				m_driveGain.step(l, driveStep(l));
				++m_envBlock[l];
			}
			if(m_gateOff[l] && !pending)
				m_release[l] = true;

			const double t = end - static_cast<double>(m_envBlock[l]);
			m_u2Level[l] = m_u2.at(l, t);
			m_modLevel[l] = m_mod.at(l, t);
			m_ampLevel[l] = m_amp.at(l, t);
			m_driveTo[l] = m_driveGain.at(l, t);

			// A pending block fades the modulator and the Track out, and leaves them silent until the trig
			if(pending)
			{
				m_mod.prev[l] = m_mod.level[l] = m_modLevel[l] = 0;
				m_amp.prev[l] = m_amp.level[l] = m_ampLevel[l] = 0;
			}

			m_driveAmount[l] = m_drive[l] ? 2.0f : 1.0f;
			m_drivePost[l] = m_drive[l] ? q31(0x3eb851ec) : q31(0x370a3d71);
		}
	}

	float BdModern::frequency(const int _track, const int _unit) const
	{
		const Increments& inc = _unit == 3 ? m_inc3 : m_inc012;
		uint32_t top = 0;
		for(const auto& s : inc.s)
			top = std::max(top, s[_track]);
		return static_cast<float>(top / 4294967296.0 * m_rate);
	}

	float BdModern::modulatorPeak(const int _track) const
	{
		float peak = 0;
		for(const auto& s : m_mix.s)
			peak = std::max(peak, std::abs(s[_track]));
		return peak;
	}

	void BdModern::render(float* const* _outputs)
	{
		const FlushDenormals ftz;

		for(int l = 0; l < Lanes; ++l)
		{
			m_trigPending[l] = m_trigQueued[l];
			m_gateOff[l] = m_gateQueued[l];
			m_trigQueued[l] = m_gateQueued[l] = false;
		}
		control();

		// Unit 1 (a sine an octave up) modulates unit 2 (the ROM wave), which with unit 0 (a sine an octave up with
		// self-feedback) makes the modulator; the modulator, enveloped, modulates unit 3, the body at the note
		pmSine(m_b1, nullptr, m_phase[1], m_inc012, 0);
		pmWave(m_a2, m_b1, m_phase[2], m_inc012, q31(static_cast<int32_t>(0xffe3d70a)) * 128, m_tables.wave);
		rampedGain(m_a2, m_u2Prev, m_u2Level);
		feedbackSine(m_b0, m_phase[0], m_inc012, m_fb, m_fbTarget, m_last, m_tables.sine);
		mix(m_a2, m_b0, m_mix, m_g0Cur, m_g0, m_g1Cur, m_g1);
		rampedGain(m_mix, m_modPrev, m_modLevel);
		pmSine(m_b3, &m_mix, m_phase[3], m_inc3, q31(0x00ffffff) * 128);
		driveClip(m_b3, m_driveFrom, m_driveTo, m_driveAmount, m_drivePost);
		click(m_click, m_clickRamp, m_clickDec, m_clickCur, m_clickTarget);
		onePole(m_click, m_hp, m_hpX, m_hpY);
		onePole(m_click, m_lp, m_lpX, m_lpY);
		sum(m_click, m_b3, m_b3);
		rampedGain(m_b3, m_ampPrev, m_ampLevel);

		// The ramps' ends are the next block's starts
		std::copy_n(m_u2Level, Lanes, m_u2Prev);
		std::copy_n(m_modLevel, Lanes, m_modPrev);
		std::copy_n(m_ampLevel, Lanes, m_ampPrev);
		std::copy_n(m_driveTo, Lanes, m_driveFrom);
		std::copy_n(m_g0, Lanes, m_g0Cur);
		std::copy_n(m_g1, Lanes, m_g1Cur);
		std::copy_n(m_clickTarget, Lanes, m_clickCur);

		for(int l = 0; l < Lanes; ++l)
		{
			float* out = _outputs[l];
			for(int n = 0; n < BlockSize; ++n)
				out[n] = m_b3.s[n][l];
			m_trigNow[l] = m_trigPending[l];
		}
	}
}
