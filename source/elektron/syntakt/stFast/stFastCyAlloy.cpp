#include "stFastCyAlloy.h"

#include <algorithm>
#include <cmath>

#include "stFastTables.h"

namespace syntakt::fast
{
	using namespace simd;

	namespace
	{
		// The firmware's constants at 48 kHz, as Q31 words
		constexpr int32_t RestWord = 0x60000000;							// units 1 and 3's levels at rest
		constexpr int32_t DriveOnWord = 0x2a3d70a4;							// drive on: its floor and pre-gain (x 4)
		constexpr int32_t PostOffWord = 0x60000000;							// drive off: the gain after it
		constexpr int32_t FallPoleWord = static_cast<int32_t>(0x81415554);	// the drive's gain envelope
		constexpr int32_t RisePoleWord = static_cast<int32_t>(0x8027627b);
		constexpr int32_t B0Word = 0x3a2102de;								// the high-pass: b0 = b2
		constexpr int32_t B1Word = static_cast<int32_t>(0x8bbdfa44);		// -2 b0
		constexpr int32_t A1Word = static_cast<int32_t>(0x92100c93);		// applied twice: a1 = 2 A1
		constexpr int32_t A2Word = 0x75283018;

		// TUNE at 64 plays the note 24 semitones up: the machine's offset to the note is -40
		constexpr int PitchOffset = 40;
	}

	void pmWave(Block& _out, const Block* _src, const float* _from, const float* _to, uint32_t* _phase,
		const Increments& _inc, const float* _wave)
	{
		for(int g = 0; g < Lanes; g += 4)
		{
			__m128i p = _mm_load_si128(reinterpret_cast<const __m128i*>(_phase + g));
			V depth = _mm_load_ps(_from + g);
			const V step = stepOf(_from, _to, g);
			for(int n = 0; n < BlockSize; ++n)
			{
				V u = phaseOf(p);
				if(_src)
					u = _mm_add_ps(u, _mm_mul_ps(depth, _mm_load_ps(_src->s[n] + g)));
				_mm_store_ps(_out.s[n] + g, lookup(u, _wave));
				p = _mm_add_epi32(p, _mm_load_si128(reinterpret_cast<const __m128i*>(_inc.s[n] + g)));
				depth = _mm_add_ps(depth, step);
			}
			_mm_store_si128(reinterpret_cast<__m128i*>(_phase + g), p);
		}
	}

	void biquad(Block& _buf, const float* _c, float (*_s)[Lanes])
	{
		const V b0 = _mm_set1_ps(_c[0]), b1 = _mm_set1_ps(_c[1]), b2 = _mm_set1_ps(_c[2]);
		const V a1 = _mm_set1_ps(_c[3]), a2 = _mm_set1_ps(_c[4]);
		for(int g = 0; g < Lanes; g += 4)
		{
			V x1 = _mm_load_ps(_s[0] + g), x2 = _mm_load_ps(_s[1] + g);
			V y1 = _mm_load_ps(_s[2] + g), y2 = _mm_load_ps(_s[3] + g);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V x = _mm_load_ps(_buf.s[n] + g);
				const V f = _mm_add_ps(_mm_add_ps(_mm_mul_ps(b0, x), _mm_mul_ps(b1, x1)), _mm_mul_ps(b2, x2));
				const V y = clip(_mm_sub_ps(f, _mm_add_ps(_mm_mul_ps(a1, y1), _mm_mul_ps(a2, y2))));
				x2 = x1;
				x1 = x;
				y2 = y1;
				y1 = y;
				_mm_store_ps(_buf.s[n] + g, y);
			}
			_mm_store_ps(_s[0] + g, x1);
			_mm_store_ps(_s[1] + g, x2);
			_mm_store_ps(_s[2] + g, y1);
			_mm_store_ps(_s[3] + g, y2);
		}
	}

	CyAlloy::CyAlloy()
	{
		const auto read = [](float* _dst, const auto& _words)
		{
			for(size_t i = 0; i < _words.size(); ++i)
				_dst[i] = q31(_words[i]);
		};
		const auto copy = [](int32_t* _dst, const auto& _words) { std::copy(_words.begin(), _words.end(), _dst); };
		using tables::CyAlloy;
		Tables& t = m_tables;
		read(t.decayOdd, tables::cyAlloy(CyAlloy::DecayOdd));
		read(t.decayEven, tables::cyAlloy(CyAlloy::DecayEven));
		read(t.target, tables::cyAlloy(CyAlloy::Target));
		read(t.depth, tables::cyAlloy(CyAlloy::Depth));
		read(t.ampDecay, tables::cyAlloy(CyAlloy::AmpDecay));
		copy(t.ratio, tables::cyAlloy(CyAlloy::Ratio));
		copy(t.spread, tables::cyAlloy(CyAlloy::Spread));
		copy(t.exp2, tables::exp2());
		read(t.wave, tables::cyAlloyWave());
		t.wave[257] = t.wave[256];
		for(int i = 0; i < 258; ++i)
			t.sine[i] = static_cast<float>(std::sin(2 * 3.14159265358979323846 * i / 256));

		// The voice before its first trig: silent, the drive's gain at 1 recovering
		for(int l = 0; l < Lanes; ++l)
		{
			m_driveStage[l] = 5;
			m_driveX[l] = 1;
			m_driveGain.prev[l] = m_driveGain.level[l] = 1;
			m_driveFrom[l] = m_driveTo[l] = 1;
			m_ampStage[l] = 1;
			convert(l);
		}
		highPass(m_bq);
	}

	void CyAlloy::highPass(float* _c)
	{
		_c[0] = _c[2] = q31(B0Word);
		_c[1] = q31(B1Word);
		_c[3] = 2 * q31(A1Word);
		_c[4] = q31(A2Word);
	}

	void CyAlloy::setNote(const int _track, const int _note)
	{
		m_note[_track] = _note;
		convert(_track);
	}

	void CyAlloy::setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
	{
		if(_index < 0 || _index >= Parameters)
			return;
		m_word[_index][_track] = std::min<int32_t>(_value, 127) << 8 | _fine;
		convert(_track);
	}

	void CyAlloy::trig(const int _track)
	{
		m_trigQueued[_track] = true;
	}

	void CyAlloy::gateOff(const int _track)
	{
		m_gateQueued[_track] = true;
	}

	void CyAlloy::testWithoutDepth()
	{
		std::fill_n(m_tables.depth, 128, 0.0f);
		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void CyAlloy::convert(const int _l)
	{
		const auto word = [this, _l](const int _i) { return m_word[_i][_l]; };
		const auto byte = [this, _l](const int _i) { return m_word[_i][_l] >> 8; };
		const Tables& t = m_tables;

		// TUNE: (value - 64 + fine / 256) semitones on the note, 24 up
		m_pitch[_l] = pitchWord(t.exp2, m_note[_l], word(0), PitchOffset);

		// The frequency factors: unit 1 at Ratio, unit 2 at Spread, unit 0 at their product, unit 3 at the note's,
		// each product truncated as the machine's
		const int64_t ratio = interpolateWord(t.ratio, word(3)), spread = interpolateWord(t.spread, word(1));
		m_factor[0][_l] = static_cast<int32_t>(static_cast<uint32_t>(ratio * spread >> 31) << 5);
		m_factor[1][_l] = ratio;
		m_factor[2][_l] = spread;
		m_factor[3][_l] = 0x04000000;

		// The third: units 1 and 3's modulation (the word D >> 6, 2 D cycles) and unit 2's feedback (the word D >> 3,
		// D / 2 cycles a unit, as SdBasic's 4 F)
		const float d = interpolate(t.depth, word(2));
		m_depthTarget[_l] = 2 * d;
		m_fbTarget[_l] = d / 2;

		// The fifth: units 0 and 2's levels at rest; units 1 and 3 rest at 0.75
		m_target[0][_l] = m_target[2][_l] = interpolate(t.target, word(4));
		m_target[1][_l] = m_target[3][_l] = q31(RestWord);

		m_driveNext[_l] = word(5) != 0;
		m_hold[_l] = word(6) != 0;

		// DECAY: the units' decays, units 0 and 2 twice as fast as 1 and 3, and the amp's
		m_decayNext[0][_l] = m_decayNext[2][_l] = -t.decayEven[byte(7)];
		m_decayNext[1][_l] = m_decayNext[3][_l] = -t.decayOdd[byte(7)];
		m_ampDecay[_l] = -t.ampDecay[byte(7)];
	}

	float CyAlloy::driveStep(const int _l)
	{
		return driveGain(m_driveStage[_l], m_driveX[_l], m_driveGain.level[_l], m_drive[_l], -q31(FallPoleWord),
			-q31(RisePoleWord), q31(DriveOnWord));
	}

	void CyAlloy::control()
	{
		for(int l = 0; l < Lanes; ++l)
		{
			const bool now = m_trigNow[l], pending = m_trigPending[l];

			// The update (0x40006d7c) and the envelopes' 48 kHz block 0: what a trig restarts and latches
			if(now)
			{
				// The machine puts a Track on CY ALLOY at its trig (0x40003ee0): unit 2's feedback and units 1 and
				// 3's modulation ramp from 0 in the first block
				if(!m_playing[l])
				{
					m_playing[l] = true;
					m_fb[l] = m_depth[l] = 0;
				}
				for(auto& p : m_phase)
					p[l] = 0;
				m_last[l] = 0;
				for(auto& s : m_bqState)
					s[l] = 0;
				for(int k = 0; k < Units; ++k)
				{
					m_decay[k][l] = m_decayNext[k][l];
					m_env[k].prev[l] = m_env[k].level[l] = 1;
				}
				m_drive[l] = m_driveNext[l];
				m_at[l] = 0;
				m_envBlock[l] = 0;

				// The units' envelopes and the amp's start from 1 without a ramp; the amp holds or decays from the
				// next block
				m_ampStage[l] = m_hold[l] ? 2 : 1;
				m_amp.prev[l] = m_amp.level[l] = 1;
				m_release[l] = false;
				m_driveStage[l] = 1;
				m_driveX[l] = 1;
				m_driveGain.prev[l] = m_driveGain.level[l] = 1;
				m_driveGain.level[l] = driveStep(l);
				m_driveFrom[l] = 1;
			}

			// No sweep: each unit's increment is the machine's at 48 kHz, as it computes it in integers
			for(int k = 0; k < Units; ++k)
			{
				const double inc48 = increment48(0x04000000, m_factor[k][l], 0x40000000, m_pitch[l]);
				const auto inc = static_cast<uint32_t>(std::llround(inc48 * 256));
				for(auto& s : m_inc[k].s)
					s[l] = inc;
			}

			// The envelopes step once per 48 kHz block, as on the machine, but in the trig's block; each sample reads
			// their ramps (SdBasic::control)
			const auto stepEnvelopes = [&]
			{
				for(int k = 0; k < Units; ++k)
				{
					const float a = m_decay[k][l];
					m_env[k].step(l, a * m_env[k].level[l] + (1 - a) * m_target[k][l]);
				}

				const float amp = m_amp.level[l];
				if(m_ampStage[l] == 2)
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
			const int64_t start = m_at[l]++;	// the block's start, in 48 kHz blocks since the trig
			while(start >= m_envBlock[l] + 1)
				stepEnvelopes();
			for(int n = 0; n < BlockSize; ++n)
			{
				const double f = n / static_cast<double>(BlockSize);
				for(int k = 0; k < Units; ++k)
					m_gEnv[k].s[n][l] = m_env[k].at(l, f);
				m_gAmp.s[n][l] = m_amp.at(l, f);
			}
			if(m_gateOff[l] && !pending)
				m_release[l] = true;

			// The drive's gain ramps over the block, slow enough
			constexpr double t = 1;	// the ramps at the block's end
			m_driveTo[l] = m_driveGain.at(l, t);

			// A pending block fades the amp out from where the last block left it, and leaves it silent until the trig
			if(pending)
			{
				for(int n = 0; n < BlockSize; ++n)
					m_gAmp.s[n][l] = m_ampEnd[l] * (1 - static_cast<float>(n) / BlockSize);
				m_amp.prev[l] = m_amp.level[l] = 0;
			}
			m_ampEnd[l] = m_amp.at(l, t);

			m_driveAmount[l] = m_drive[l] ? 4 * q31(DriveOnWord) : 1.0f;
			m_drivePost[l] = m_drive[l] ? q31(INT32_MAX) : q31(PostOffWord);
		}
	}

	const Block& CyAlloy::renderBlock()
	{
		const FlushDenormals ftz;

		for(int l = 0; l < Lanes; ++l)
		{
			m_trigPending[l] = m_trigQueued[l];
			m_gateOff[l] = m_gateQueued[l];
			m_trigQueued[l] = m_gateQueued[l] = false;
		}
		control();

		// The units under their envelopes: unit 0 alone modulating unit 1, unit 2 feeding back on itself modulating
		// unit 3, both modulations at the same depth
		const Tables& t = m_tables;
		pmWave(m_b[0], nullptr, m_depth, m_depthTarget, m_phase[0], m_inc[0], t.sine);
		gain(m_b[0], m_gEnv[0]);
		pmWave(m_b[1], &m_b[0], m_depth, m_depthTarget, m_phase[1], m_inc[1], t.wave);
		gain(m_b[1], m_gEnv[1]);
		feedbackSine(m_b[2], m_phase[2], m_inc[2], m_fb, m_fbTarget, m_last, t.sine);
		gain(m_b[2], m_gEnv[2]);
		pmWave(m_b[3], &m_b[2], m_depth, m_depthTarget, m_phase[3], m_inc[3], t.wave);
		gain(m_b[3], m_gEnv[3]);
		std::copy_n(m_depthTarget, Lanes, m_depth);

		// Units 1 and 3 at 0.5 each, high-passed, into the drive and the amp
		alignas(16) static constexpr float Half[Lanes] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
			0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
		mix(m_b[3], m_b[1], m_out, Half, Half, Half, Half);
		biquad(m_out, m_bq, m_bqState);
		driveClip(m_out, m_driveFrom, m_driveTo, m_driveAmount, m_drivePost);
		gain(m_out, m_gAmp);

		// The drive's ramp's end is the next block's start
		std::copy_n(m_driveTo, Lanes, m_driveFrom);

		for(int l = 0; l < Lanes; ++l)
			m_trigNow[l] = m_trigPending[l];
		return m_out;
	}
}
