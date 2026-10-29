#include "stFastCpVintage.h"

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
		constexpr double Q31 = 2147483648.0;

		// The tone's envelope and unit 1's rise a step a block, full in 3.25 blocks: 4/13, the word (unit 1's as the
		// machine's negative word)
		constexpr float Attack = 4 / 13.0f;
		const auto AttackWord = static_cast<int32_t>(-std::llround(4 / 13.0 * Q31));
		// The crackle's gate: a round value, the word
		const auto Gate = static_cast<int32_t>(std::llround(0.33 * Q31));

		// The clap's mix: the noise at sqrt(5/32) (-8.06 dB), the crackle 6 dB above (3.0e-8 over the words)
		const float NoiseGain = static_cast<float>(std::sqrt(5 / 32.0));
		const float CrackleGain = static_cast<float>(std::sqrt(5 / 32.0) * std::pow(10.0, 6 / 20.0));

		// The drive's gain envelope, one-pole per 48 kHz block: falling by half in 50 ms (75 blocks: 6.2e-8 of the pole
		// under the word), recovering with a time constant of 208 ms (8.5e-7 over); its floor 0.5, the clip's
		// pre-gain 3 and the gain after it 0.8 with the drive on (round values, the words)
		const float FallPole = static_cast<float>(std::pow(0.5, 1 / 75.0));
		const float RisePole = static_cast<float>(std::exp(-BlockSize / (MachineRate * 0.208)));
		constexpr float DriveFloor = 0.5f;

		// The noise's low-pass at 150 Hz and the clap's last high-pass at 300 Hz, by the bilinear transform without
		// prewarping (round values, the words)
		const OnePole NoiseLp = bilinearLowPass(150), OutHp = bilinearHighPass(300);

		// TUNE at 64 plays the note: the machine's offset to the note is -64. Unit 0 plays an octave up, the resonator
		// two (24 semitones, Q16), below its tables' last eighth of a semitone (127.875 semitones, less 1 LSB)
		constexpr int PitchOffset = 64;
		constexpr int64_t NoteFactor = int64_t(1) << 26;
		constexpr int32_t ResonatorOffset = 24 << 16, ResonatorTop = (1023 << 13) - 1;

		// The bursts' timer counts 2^19 a block; a trig repeats three times, but with HOLD
		constexpr uint32_t TimerStep = 1 << 19;
		constexpr int Repeats = 3;

		// ---- the machine's integers, for the coefficients it computes in them ----

		int32_t frac(const int32_t _y, const int32_t _x) { return store(product(_y, _x)); }
		int32_t neg(const int32_t _v) { return wrap(-static_cast<int64_t>(_v)); }

		int32_t asrReg(const int32_t _v, const int32_t _n)
		{
			const int32_t n = _n & 63;
			return n >= 32 ? (_v < 0 ? -1 : 0) : _v >> n;
		}

		int32_t aslReg(const int32_t _v, const int32_t _n)
		{
			const int32_t n = _n & 63;
			return n >= 32 ? 0 : wrap(static_cast<int64_t>(static_cast<uint32_t>(_v) << n));
		}

		int32_t leadingZeros(const int32_t _v)
		{
			int32_t n = 0;
			for(uint32_t v = static_cast<uint32_t>(_v); v && !(v & 0x80000000u); v <<= 1)
				++n;
			return _v ? n : 32;
		}

		int32_t saturated(const int64_t _v)
		{
			return static_cast<int32_t>(std::clamp<int64_t>(_v, INT32_MIN, INT32_MAX));
		}

		// 0x400054e6: a two-pole resonator and its output stage, the band-pass (1 - A) / 2 of a normalized allpass
		// lattice, its coefficients (CpVintage::resonatorWords, c0, c3 and c6 times 256, c1 and c5 as 1 - c) ramped
		// from _from to _to over the block; its two states times 256, and the output stage's (_z, the second state a
		// sample before, which a trig does not clear). The output clipped, as the machine's doubling saturates (from
		// twice full scale the machine's shift before it wraps: the band-pass's peak gain is 1)
		void resonator(Block& _buf, const float (*_from)[Lanes], const float (*_to)[Lanes], float* _y1, float* _y2,
			float* _z)
		{
			const V half = _mm_set1_ps(0.5f);
			for(int g = 0; g < Lanes; g += 4)
			{
				V c[8], step[8];
				for(int k = 0; k < 8; ++k)
				{
					c[k] = _mm_load_ps(_from[k] + g);
					step[k] = stepOf(_from[k], _to[k], g);
				}
				V y1 = _mm_load_ps(_y1 + g), y2 = _mm_load_ps(_y2 + g), z = _mm_load_ps(_z + g);
				for(int n = 0; n < BlockSize; ++n)
				{
					const V x = _mm_load_ps(_buf.s[n] + g);
					const V a = _mm_sub_ps(_mm_add_ps(_mm_mul_ps(c[0], x), _mm_sub_ps(y1, _mm_mul_ps(c[1], y1))),
						_mm_mul_ps(c[2], y2));
					const V b = _mm_sub_ps(_mm_add_ps(_mm_mul_ps(c[4], y1), _mm_sub_ps(y2, _mm_mul_ps(c[5], y2))),
						_mm_mul_ps(c[3], x));
					const V out = _mm_mul_ps(half, _mm_sub_ps(_mm_sub_ps(x, _mm_mul_ps(c[6], x)), _mm_mul_ps(c[7], z)));
					_mm_store_ps(_buf.s[n] + g, clip(out));
					z = b;
					y1 = a;
					y2 = b;
					for(int k = 0; k < 8; ++k)
						c[k] = _mm_add_ps(c[k], step[k]);
				}
				_mm_store_ps(_y1 + g, y1);
				_mm_store_ps(_y2 + g, y2);
				_mm_store_ps(_z + g, z);
			}
		}

		// 0x40004f74 as CP VINTAGE sets it: a first-order high-pass in place, y = b (x - x1) + a y1, b and a ramped
		// over the block
		void rampedHighPass(Block& _buf, const float* _bFrom, const float* _bTo, const float* _aFrom, const float* _aTo,
			float* _x1, float* _y1)
		{
			for(int g = 0; g < Lanes; g += 4)
			{
				V b = _mm_load_ps(_bFrom + g), a = _mm_load_ps(_aFrom + g);
				const V sb = stepOf(_bFrom, _bTo, g), sa = stepOf(_aFrom, _aTo, g);
				V x1 = _mm_load_ps(_x1 + g), y1 = _mm_load_ps(_y1 + g);
				for(int n = 0; n < BlockSize; ++n)
				{
					const V x = _mm_load_ps(_buf.s[n] + g);
					y1 = _mm_add_ps(_mm_mul_ps(b, _mm_sub_ps(x, x1)), _mm_mul_ps(a, y1));
					x1 = x;
					_mm_store_ps(_buf.s[n] + g, y1);
					b = _mm_add_ps(b, sb);
					a = _mm_add_ps(a, sa);
				}
				_mm_store_ps(_x1 + g, x1);
				_mm_store_ps(_y1 + g, y1);
			}
		}
	}

	CpVintage::CpVintage()
		: m_drive(FallPole, RisePole, DriveFloor, {3.0f, 0.8f}, {1.0f, 1.0f})
	{
		using tables::CpVintage;
		Tables& t = m_tables;
		copyWords(t.modDecay, tables::cpVintage(CpVintage::ModDecay));
		readQ31(t.toneDecay, tables::cpVintage(CpVintage::ToneDecay));
		readQ31(t.repeatDecay, tables::cpVintage(CpVintage::RepeatDecay));
		readQ31(t.clapDecay, tables::cpVintage(CpVintage::ClapDecay));
		readQ31(t.crackleDecay, tables::cpVintage(CpVintage::CrackleDecay));
		readQ31(t.clapLevel, tables::cpVintage(CpVintage::ClapLevel));
		readQ31(t.toneLevel, tables::cpVintage(CpVintage::ToneLevel));
		readQ31(t.toneDepth, tables::cpVintage(CpVintage::ToneDepth));
		copyWords(t.toneFeedback, tables::cpVintage(CpVintage::ToneFeedback));
		copyWords(t.sampleRate, tables::cpVintage(CpVintage::SampleRate));
		copyWords(t.period, tables::cpVintage(CpVintage::Period));
		copyWords(t.toneRatio, tables::cpVintage(CpVintage::ToneRatio));
		copyWords(t.highPass, tables::cpVintage(CpVintage::HighPass));
		copyWords(t.resonatorA, tables::cpVintageResonator(false));
		copyWords(t.resonatorB, tables::cpVintageResonator(true));
		copyWords(t.log2, tables::cpVintageLog2());
		copyWords(t.exp2Half, tables::cpVintageExp2());
		copyWords(t.exp2, tables::exp2());
		sineTable(t.sine);

		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void CpVintage::setNote(const int _track, const int _note)
	{
		m_controls.notes[_track] = _note;
		convert(_track);
	}

	void CpVintage::setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
	{
		if(m_controls.setParameter(_track, _index, _value, _fine))
			convert(_track);
	}

	void CpVintage::testWithoutChaos()
	{
		std::fill_n(m_tables.toneFeedback, 128, 0);
		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void CpVintage::resonatorWords(const int _note, const int32_t _tune, int32_t* _c) const
	{
		const Tables& t = m_tables;

		// The machine's square roots: L = -log2(sqrt(v)) in Q8.24 from v's mantissa, its top 10 bits rounded up
		// (sqrtLog), and 2^-L back as the exp2 table's 2^(1 - L's fraction) / 2, shifted by L's integer part where it
		// is read (exp2Entry); invNorm: L of sqrt(1 - s^2)
		const auto sqrtLog = [&t](const int32_t _v)
		{
			const int32_t z = leadingZeros(_v) - 1;
			const int32_t k = frac(0x800, aslReg(_v, z));
			return wrap((neg(t.log2[k - 1024]) >> 7) + static_cast<int64_t>(aslReg(z, 24))) >> 1;
		};
		const auto exp2Entry = [&t](const int32_t _l) { return t.exp2Half[wrap(0x1000000 - (_l & 0xffffff)) >> 14]; };
		const auto invNorm = [&](const int32_t _s) { return sqrtLog(wrap(int64_t(INT32_MAX) - frac(_s, _s))); };

		// The tables read at an eighth of a semitone, weighted by the fraction of the whole semitone, as the machine
		// reads them
		const int32_t pitch = std::min(pitchQ16(_note, _tune, PitchOffset) + ResonatorOffset, ResonatorTop);
		const int32_t fr = wrap(static_cast<int64_t>(pitch & 0xffff) << 15), w = wrap(int64_t(INT32_MAX) - fr);
		const int32_t i = pitch >> 13;
		_c[1] = neg(store(product(w, t.resonatorA[i]) + product(fr, t.resonatorA[i + 1])));
		const int32_t cosine = store(product(w, t.resonatorB[i]) + product(fr, t.resonatorB[i + 1]));

		const int32_t l0 = invNorm(cosine);
		const int32_t g1 = asrReg(exp2Entry(l0), l0 >> 24);
		const int32_t l1 = invNorm(_c[1]);
		const int32_t e4 = asrReg(exp2Entry(l1), l1 >> 24);
		const int32_t e5 = exp2Entry(neg(l1));
		const int32_t c0 = frac(g1, e4) >> 8;
		_c[0] = c0;
		_c[2] = frac(e4, cosine);
		_c[3] = aslReg(frac(frac(_c[1], e5), c0), neg(neg(l1) >> 24));
		_c[4] = e4;
		_c[5] = frac(_c[1], cosine);
		_c[6] = cosine >> 8;
		_c[7] = g1;
	}

	void CpVintage::convert(const int _l)
	{
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		const auto byte = [this, _l](const int _i) { return m_controls.words[_i][_l] >> 8; };
		const Tables& t = m_tables;

		// TUNE: (value - 64 + fine / 256) semitones on the note, clamped to notes 0-127 (p, Q16)
		const int note = m_controls.notes[_l];
		m_pitch[_l] = pitchWord(t.exp2, note, word(0), PitchOffset);
		const int32_t p = pitchQ16(note, word(0), PitchOffset);

		// The resonator two octaves up, its coefficient words as floats (c0, c3 and c6 times 256, c1 and c5 as 1 - c);
		// the high-pass's pole read at the semitone, the fraction weighting the next entry by 2^-15 of itself at most,
		// as the machine weights it
		int32_t c[8];
		resonatorWords(note, word(0), c);
		for(int k = 0; k < 8; ++k)
			m_res[k][_l] = k == 1 || k == 5 ? static_cast<float>((Q31 - c[k]) / Q31)
				: static_cast<float>(q31(c[k]) * (k == 0 || k == 3 || k == 6 ? 256 : 1));
		const int32_t at = p >> 16, f = p & 0xffff;
		const int32_t pole = store(product(t.highPass[at], INT32_MAX - f) +
			product(t.highPass[std::min(at + 1, 127)], f));
		m_hpB[_l] = q31(static_cast<int32_t>(std::min<int64_t>((int64_t(1) << 30) - (pole >> 1), INT32_MAX)));
		m_hpA[_l] = -q31(pole);	// y = (1/2 - pole/2) (x - x1) - pole y1, the pole's word negative

		// The second: unit 0's modulation by unit 1 (cycles per unit), unit 1's ratio and feedback (the machine's
		// word); unit 0 at an octave up
		m_depthTarget[_l] = interpolate(t.toneDepth, word(1));
		m_fbWord[_l] = interpolateWord(t.toneFeedback, word(1));
		setIncrement(m_inc[0], _l, NoteFactor, 2 * NoteFactor, int64_t(1) << 30, m_pitch[_l]);
		setIncrement(m_inc[1], _l, NoteFactor, interpolateWord(t.toneRatio, word(1)), int64_t(1) << 30, m_pitch[_l]);

		// The third: the tone and the clap in the mix
		m_toneTarget[_l] = interpolate(t.toneLevel, word(2));
		m_clapTarget[_l] = interpolate(t.clapLevel, word(2));

		// The fourth: the rate reducer, the claps' period and their repeats' decay; the fifth: the tone's decays;
		// DECAY: the crackle's and the noise's last
		m_rate[_l] = interpolateWord(t.sampleRate, word(3));
		m_period[_l] = static_cast<uint32_t>(t.period[byte(3)]);
		m_repeatDecay[_l] = -t.repeatDecay[byte(3)];
		m_toneDecay[_l] = -t.toneDecay[byte(4)];
		m_modDecay[_l] = t.modDecay[byte(4)];
		m_lastDecay[0][_l] = -t.crackleDecay[byte(7)];
		m_lastDecay[1][_l] = -t.clapDecay[byte(7)];

		m_driveNext[_l] = word(5) != 0;
		m_hold[_l] = word(6) != 0;
	}

	void CpVintage::control()
	{
		for(int l = 0; l < Lanes; ++l)
		{
			Trigs& trigs = m_controls.trigs;
			const bool now = trigs.now[l];
			bool pending = trigs.pending[l];
			bool restart = false;	// unit 1's envelope, at the noise's repeat

			if(now)
			{
				// The machine puts a Track on CP VINTAGE at its trig (0x40003ee0): unit 1's feedback ramps from 0 in
				// the first block, the noise starts from its seed, the resonator's coefficients ramp from 0 and the
				// rate reducer starts empty; a trig or a gate-off in that block is lost (Trigs::machineChange)
				if(!m_playing[l])
				{
					trigs.machineChange(l);
					pending = false;
					m_playing[l] = true;
					m_fb[l] = m_last[l] = 0;
					m_noise.start(l);
					for(int k = 0; k < 8; ++k)
						m_resFrom[k][l] = k == 1 || k == 5 ? 1.0f : 0.0f;
					m_resZ[l] = 0;
					for(int k = 0; k < 2; ++k)
						m_held[k][l] = 0, m_heldCount[k][l] = 0;
				}

				// The update (0x40008580) at a trig: the phases, the filters' states but the resonator's output
				// stage's, no ramp of the high-pass, the mix's levels or unit 0's modulation; three repeats but with
				// HOLD
				for(auto& ph : m_phase)
					ph[l] = 0;
				m_resY1[l] = m_resY2[l] = 0;
				m_lpX[l] = m_lpY[l] = m_hpX[l] = m_hpY[l] = m_hp2X[l] = m_hp2Y[l] = 0;
				m_hpBFrom[l] = m_hpB[l];
				m_hpAFrom[l] = m_hpA[l];
				m_tone[l] = m_toneTarget[l];
				m_clap[l] = m_clapTarget[l];
				m_depth[l] = m_depthTarget[l];
				m_repeats[l] = m_hold[l] ? 0 : Repeats;

				// The bursts full without a ramp; the tone's envelope from the trig's block's silence to one and a
				// half of its step, unit 1's from its step
				for(int k = 0; k < 2; ++k)
				{
					m_count[k][l] = 0;
					m_timer[k][l] = 0;
					m_burst[k][l] = 1;
					m_fresh[k][l] = false;
				}
				m_toneEnv.trigRise(l, Attack);
				m_envFrom[l] = m_env[l] = AttackWord;
				m_envRising[l] = true;
				m_drive.trig(l, m_driveNext[l]);
			}
			else
			{
				// HOLD or a trig's block: no repeat
				if(m_hold[l] || pending)
					m_repeats[l] = 0;

				// The bursts (0x40005728): a repeat when the period has passed, full a block longer, then the
				// repeats' decay until the last one's
				for(int k = 0; k < 2; ++k)
				{
					if(m_count[k][l] < m_repeats[l] && m_period[l] <= m_timer[k][l])
					{
						++m_count[k][l];
						m_timer[k][l] -= m_period[l];
						m_burst[k][l] = 1;
						m_fresh[k][l] = true;
						restart |= k == 1;
					}
					else if(m_fresh[k][l])
						m_fresh[k][l] = false;
					else
						m_burst[k][l] *= m_repeats[l] > m_count[k][l] ? m_repeatDecay[l] : m_lastDecay[k][l];
				}
				m_toneEnv.stepAmp(l, m_toneDecay[l], Attack, false);

				// Unit 1's envelope (0x40003c0e) in its words: up by its step, saturated, held full a block, then
				// decaying toward 0 (envelopeDecay); rising again from 0 at the noise's repeat, its ramp from where it
				// was
				int32_t& env = m_env[l];
				if(restart)
				{
					env = AttackWord;
					m_envRising[l] = true;
				}
				else if(!m_envRising[l])
					env = envelopeDecay(env, m_modDecay[l], 0);
				else if(env == INT32_MIN)
					m_envRising[l] = false;
				else
					env = saturated(static_cast<int64_t>(env) + AttackWord);
				m_drive.step(l);
			}
			for(auto& timer : m_timer)
				timer[l] += TimerStep;

			// The VCAs (0x400058e8) ramp from the last block's level, the bursts' from full at a trig; a trig's block
			// fades them out, and leaves a trig's sounding block that sees another silent
			for(int k = 0; k < 3; ++k)
				m_vcaFrom[k][l] = k < 2 && now && !pending ? 1.0f : m_vcaTo[k][l];
			for(int k = 0; k < 2; ++k)
				m_vcaTo[k][l] = pending ? 0 : m_burst[k][l];
			m_vcaTo[2][l] = pending ? 0 : m_toneEnv.level[l];
			m_drive.end(l);
		}
	}

	const Block& CpVintage::renderBlock()
	{
		const FlushDenormals ftz;

		m_controls.trigs.start();
		control();

		// The tone: unit 1 feeding back on its own output under its envelope, which modulates unit 0, under the tone's
		// envelope
		const Tables& t = m_tables;
		scaledFeedbackSine(m_scaled, m_phase[1], m_inc[1], m_fb, m_fbWord, m_last, m_envFrom, m_env);
		pmWave(m_toneOut, &m_scaled, m_depth, m_depthTarget, m_phase[0], m_inc[0], t.sine);
		std::copy_n(m_depthTarget, Lanes, m_depth);
		rampedGain(m_toneOut, m_vcaFrom[2], m_vcaTo[2]);

		// The clap: the crackle and the white noise its generator goes on with, low-passed, under their bursts, mixed
		// and clipped
		m_noise.render(m_crackle, Gate);
		whiteNoise(m_white, m_noise.words[2], m_noise.words[3]);
		onePole(m_white, NoiseLp, m_lpX, m_lpY);
		rampedGain(m_crackle, m_vcaFrom[0], m_vcaTo[0]);
		rampedGain(m_white, m_vcaFrom[1], m_vcaTo[1]);
		alignas(16) float noiseGain[Lanes], crackleGain[Lanes];
		std::fill_n(noiseGain, Lanes, NoiseGain);
		std::fill_n(crackleGain, Lanes, CrackleGain);
		mix(m_white, m_crackle, m_out, noiseGain, noiseGain, crackleGain, crackleGain);
		saturate(m_out, 1);

		// Through the resonator, the high-pass, the rate reducer and the second high-pass, eight times into the clip
		resonator(m_out, m_resFrom, m_res, m_resY1, m_resY2, m_resZ);
		for(int k = 0; k < 8; ++k)
			std::copy_n(m_res[k], Lanes, m_resFrom[k]);
		rampedHighPass(m_out, m_hpBFrom, m_hpB, m_hpAFrom, m_hpA, m_hpX, m_hpY);
		std::copy_n(m_hpB, Lanes, m_hpBFrom);
		std::copy_n(m_hpA, Lanes, m_hpAFrom);
		reduceRate(m_out, m_rate, m_held, m_heldCount);
		onePole(m_out, OutHp, m_hp2X, m_hp2Y);
		saturate(m_out, 8);

		// The tone and the clap mixed, clipped, into the drive
		mix(m_toneOut, m_out, m_out, m_tone, m_toneTarget, m_clap, m_clapTarget);
		saturate(m_out, 1);
		std::copy_n(m_toneTarget, Lanes, m_tone);
		std::copy_n(m_clapTarget, Lanes, m_clap);
		driveClip(m_out, m_drive.from, m_drive.to, m_drive.amount, m_drive.post);

		// The drive's ramp's end is the next block's start
		m_drive.advance();

		m_controls.trigs.end();
		return m_out;
	}
}
