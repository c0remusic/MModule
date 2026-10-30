#include "stFastSyChord.h"

#include <algorithm>
#include <cmath>

#include "stFastTables.h"

namespace syntakt::fast
{
	using namespace simd;

	namespace
	{
		// The machine's constants by their laws, each against the firmware's word (measured by hand, the word kept out
		// of the source; the tests drive the exact port with it)

		// The drive's gain envelope, one-pole per 48 kHz block: falling by half in 50 ms (75 blocks: 6.2e-8 of the
		// pole over the word), recovering with a time constant of 1.2 ln 2 s (831.8 ms: 7.7e-10 under); its floor
		// 0.25, the clip's pre-gain 1, the gain after it 1 with the drive on, 0.5 off (round values, the words)
		const float FallPole = static_cast<float>(std::pow(0.5, 1 / 75.0));
		const float RisePole = static_cast<float>(std::exp(-BlockSize / (MachineRate * 1.2 * std::log(2.0))));
		constexpr float DriveFloor = 0.25f;

		// TUNE at 64 plays the note: the machine's offset to the note is -64; the note at most 96 (C7)
		constexpr int PitchOffset = 64;
		constexpr int MaxNote = 96;

		// A sum the machine saturates (its addSat and subSat)
		int32_t sat(const int64_t _v) { return static_cast<int32_t>(std::clamp<int64_t>(_v, INT32_MIN, INT32_MAX)); }

		// The Q31 word of _v, rounded
		int32_t q31Word(const double _v) { return static_cast<int32_t>(std::llround(_v * 2147483648.0)); }

		// The pitch word is in Hz times 2^16: the filter's top at 22 kHz (the word), a unit's frequency word (the pitch
		// word under its factor, Hz times 2^11) at most 2217 Hz (the word; C#7 is 2217.46 Hz), its 24-bit
		// increment at 48 kHz that word times 2^15 / 48000 in Q31 (the word rounded up, 0.30 LSB), shifted right by 2
		constexpr int32_t Top = 22000 * 65536;
		constexpr int32_t MaxUnitWord = 2217 * 2048;
		const int32_t Rate = static_cast<int32_t>(std::ceil(32768.0 / 48000 * 2147483648.0));

		// The pole table's index from the filter's frequency word: 2^15 / 44000 in Q31 (the word rounded down, 0.40 LSB)
		// doubled, 22 kHz at the table's last entry
		const int32_t PoleScale = static_cast<int32_t>(32768.0 / 44000 * 2147483648.0);

		// A unit's factor at the note's (2^26), its level in the mix (1/4), the crossfade's low gain at its position 0
		// (127/128)
		constexpr int32_t NoteFactor = 1 << 26;
		constexpr int32_t Level = 1 << 29;
		constexpr int32_t CrossfadeFull = 127 << 24;

		// 0x40007630: a chord note's gain from its step _n, in the machine's words: full outside 0 to 9, 1 - n/5 from 0
		// to 5, n/5 - 1 from 6 to 9
		int32_t stepGain(const int _n)
		{
			if(static_cast<uint32_t>(_n) > 9)
				return INT32_MAX;
			const int32_t fifth = q31Word(0.2), x = _n << 27;
			if(_n <= 5)
			{
				const int32_t d = wrap(int64_t(store(-product(fifth, x))) * 8);
				return sat(int64_t(INT32_MAX) + sat(2 * int64_t(d)));
			}
			const int32_t d = sat(int64_t(wrap(int64_t(frac(fifth, x)) * 8)) - (INT32_MAX >> 1));
			return sat(2 * int64_t(d));
		}

		// 0x4000768c: the levels of units 1 to 3 (_g) for the chords, from the second parameter's byte _b: below 32 a
		// unit after another fading in by tenths, above it the steps of _b mod 32 for each; unit 3 silent for the shapes
		// 3-6, 17, 18, 21 and from 36
		void chordGains(int32_t* _g, const int _b, const int _shape)
		{
			if(_b > 31)
			{
				const int n = _b & 0x1f;
				for(int i = 0; i < 3; ++i)
					_g[i] = stepGain(n - 10 * i) >> 2;
			}
			else
			{
				for(int i = 0; i < 3; ++i)
				{
					const int n = _b - 10 * i;
					int32_t g = INT32_MAX;
					if(n <= 0)
						g = 0;
					else if(n <= 10)
						g = sat(2 * int64_t(wrap(int64_t(frac(q31Word(0.1), n << 27)) * 8)));
					_g[i] = g >> 2;
				}
			}
			if(static_cast<uint16_t>(_shape - 7) > 28 || static_cast<uint16_t>(_shape - 17) <= 1 || _shape == 21)
				_g[2] = 0;
		}

		// 0x40002770: the wave of index _n, 1 to 32 the wavetable's, else the RAM sine (0)
		int32_t waveOf(const int32_t _n) { return static_cast<uint32_t>(_n - 1) <= 31 ? _n : 0; }

		// 0x400032b4's samples for one unit of the 16 Tracks: two waves of 256 entries (258 values) per lane, _w1 and
		// _w2, read at the unit's phase as the machine reads its waves, under gains ramped over the block from _g1From
		// to _g1To and from _g2From to _g2To; _inc the unit's increment per lane (a 32-bit phase step)
		void waveUnit(Block& _out, uint32_t* _phase, const uint32_t* _inc, const float* const* _w1,
			const float* const* _w2, const float* _g1From, const float* _g1To, const float* _g2From, const float* _g2To)
		{
			for(int g = 0; g < Lanes; g += 4)
			{
				__m128i p = _mm_load_si128(reinterpret_cast<const __m128i*>(_phase + g));
				const __m128i inc = _mm_load_si128(reinterpret_cast<const __m128i*>(_inc + g));
				V g1 = ramp(_g1From, g), g2 = ramp(_g2From, g);
				const V s1 = stepOf(_g1From, _g1To, g), s2 = stepOf(_g2From, _g2To, g);
				for(int n = 0; n < BlockSize; ++n)
				{
					// The 24-bit phase's top 8 bits the entry, its 16 below the fraction, as the machine reads its waves
					alignas(16) int32_t i[4];
					alignas(16) float a1[4], b1[4], a2[4], b2[4];
					_mm_store_si128(reinterpret_cast<__m128i*>(i), _mm_srli_epi32(p, 24));
					const V f = _mm_mul_ps(_mm_cvtepi32_ps(_mm_and_si128(_mm_srli_epi32(p, 8), _mm_set1_epi32(0xffff))),
						_mm_set1_ps(1.0f / 65536));
					for(int l = 0; l < 4; ++l)	// a gather: SSE2 has none
					{
						a1[l] = _w1[g + l][i[l]];
						b1[l] = _w1[g + l][i[l] + 1];
						a2[l] = _w2[g + l][i[l]];
						b2[l] = _w2[g + l][i[l] + 1];
					}
					const V va1 = _mm_load_ps(a1), va2 = _mm_load_ps(a2);
					const V x1 = _mm_add_ps(va1, _mm_mul_ps(f, _mm_sub_ps(_mm_load_ps(b1), va1)));
					const V x2 = _mm_add_ps(va2, _mm_mul_ps(f, _mm_sub_ps(_mm_load_ps(b2), va2)));
					_mm_store_ps(_out.s[n] + g, _mm_add_ps(_mm_mul_ps(g1, x1), _mm_mul_ps(g2, x2)));
					p = _mm_add_epi32(p, inc);
					g1 = _mm_add_ps(g1, s1);
					g2 = _mm_add_ps(g2, s2);
				}
				_mm_store_si128(reinterpret_cast<__m128i*>(_phase + g), p);
			}
		}
	}

	SyChord::SyChord()
		: m_drive(FallPole, RisePole, DriveFloor, {1.0f, 1.0f}, {1.0f, 0.5f})
	{
		using tables::SyChord;
		Tables& t = m_tables;
		const SyChord ratios[4] = {SyChord::RatioA, SyChord::RatioB, SyChord::RatioC, SyChord::RatioD};
		for(int i = 0; i < 4; ++i)
			copyWords(t.ratio[i], tables::syChord(ratios[i]));
		copyWords(t.cutoff, tables::syChord(SyChord::Cutoff));
		copyWords(t.pole, tables::syChord(SyChord::Pole));
		copyWords(t.sweepDecay, tables::syChord(SyChord::SweepDecay));
		copyWords(t.attack, tables::syChordAttack());
		copyWords(t.shapes, tables::syChordShapes());
		readQ31(t.ampDecay, tables::syChord(SyChord::AmpDecay));
		copyWords(t.exp2, tables::exp2());
		sineTable(t.waves[0]);
		for(int n = 1; n <= 32; ++n)
		{
			readQ31(t.waves[n], tables::syChordWave(n));
			t.waves[n][257] = t.waves[n][256];
		}

		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void SyChord::setNote(const int _track, const int _note)
	{
		m_controls.notes[_track] = _note;
		convert(_track);
	}

	void SyChord::setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
	{
		if(m_controls.setParameter(_track, _index, _value, _fine))
			convert(_track);
	}

	void SyChord::convert(const int _l)
	{
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		const auto byte = [this, _l](const int _i) { return m_controls.words[_i][_l] >> 8; };
		const Tables& t = m_tables;

		// TUNE: (value - 64 + fine / 256) semitones on the note, the pitch word in Hz times 2^16
		const int32_t f = pitchWord(t.exp2, std::clamp(m_controls.notes[_l], 0, MaxNote), word(0), PitchOffset);

		// The shape (third parameter): 0 to 2 two, three or four units detuned by the second parameter's word, the
		// others a chord of four whose units' levels and octaves the second's byte sets. A unit a shape leaves keeps
		// its factor
		const int shape = byte(2), b = byte(1);
		m_pitch[_l] = f;
		int32_t* r[4] = {&m_ratioNext[0][_l], &m_ratioNext[1][_l], &m_ratioNext[2][_l], &m_ratioNext[3][_l]};
		const auto detune = [&](const int _table) { return interpolateWord(t.ratio[_table], word(1)); };
		int32_t level[4] = {Level, Level, shape >= 1 ? Level : 0, shape == 2 ? Level : 0};
		for(int k = 0; k < Units; ++k)
			m_ratioSet[k][_l] = shape >= k - 1;
		if(shape == 0)
		{
			*r[0] = detune(0);
			*r[1] = detune(1);
		}
		else if(shape == 1)
		{
			*r[0] = NoteFactor;
			*r[1] = detune(0);
			*r[2] = detune(1);
		}
		else if(shape == 2)
		{
			*r[0] = detune(2);
			*r[1] = detune(0);
			*r[2] = detune(1);
			*r[3] = detune(3);
		}
		else
		{
			chordGains(level + 1, b, shape);
			*r[0] = NoteFactor;
			const int32_t* row = t.shapes + 4 * (std::min(shape, 37) - 3);
			for(int k = 1; k < 4; ++k)
			{
				// An octave down where the byte is within 32 above 27 + 10 k, up above 90 + 10 k
				const int32_t v = row[k];
				*r[k] = static_cast<uint32_t>(b - 27 - 10 * k) <= 31 ? v >> 1 : b > 90 + 10 * k ? wrap(2 * int64_t(v)) : v;
			}
		}
		for(int k = 0; k < Units; ++k)
			m_levelNext[k][_l] = level[k];

		// The fourth: the wave, its top 6 bits the index and the next 10 the crossfade's position into the next; the
		// fifth: how far the filter's cutoff follows the note from 22 kHz down
		m_indexNext[_l] = word(3) >> 10;
		m_position[_l] = static_cast<int32_t>(static_cast<uint32_t>(word(3) & 0x3ff) << 21);
		m_cut[_l] = frac(wrap(int64_t(Top) - f), interpolateWord(t.cutoff, word(4)));

		// DECAY: the amp's, every block; the units' attack and the sweep's decay at a trig
		const int d = byte(7);
		m_ampDecay[_l] = -t.ampDecay[d];
		m_attackNext[_l] = -q31(t.attack[std::min(d, 39)]);
		m_sweepDecayNext[_l] = t.sweepDecay[d];

		m_driveNext[_l] = word(5) != 0;
		m_hold[_l] = word(6) != 0;
	}

	void SyChord::crossfade(const int _l)
	{
		// 0x400032b4's waves and gains, once a block, the same for the four units: from the last block's gains to the
		// low wave's 127/128 - position and the high wave's position; where the index moved by one, the wave both
		// blocks share carries on and the other fades out, by more, both last waves fade out and the new start next
		// block
		const int32_t index = m_indexNext[_l], last = m_index[_l], position = m_position[_l];
		const int32_t low = waveOf(index), high = waveOf(index + 1);
		const int32_t lowTarget = wrap(int64_t(CrossfadeFull) - position);
		int32_t w1 = low, w2 = high, g1 = m_lowGain[_l], g2 = m_highGain[_l], to1 = lowTarget, to2 = position;
		if(last + 1 == index)
		{
			g1 = m_highGain[_l];
			g2 = m_lowGain[_l];
			to2 = 0;
			w2 = m_lastLow[_l];
			m_lowGain[_l] = lowTarget;
			m_highGain[_l] = 0;
		}
		else if(last - 1 == index)
		{
			g1 = m_highGain[_l];
			g2 = m_lowGain[_l];
			to1 = 0;
			w1 = m_lastHigh[_l];
			m_lowGain[_l] = 0;
			m_highGain[_l] = position;
		}
		else if(low != m_lastLow[_l])
		{
			to1 = to2 = 0;
			w1 = m_lastLow[_l];
			w2 = m_lastHigh[_l];
			m_lowGain[_l] = m_highGain[_l] = 0;
		}
		else
		{
			m_lowGain[_l] = lowTarget;
			m_highGain[_l] = position;
		}
		m_lastLow[_l] = low;
		m_lastHigh[_l] = high;
		m_index[_l] = index;
		m_wave1[_l] = m_tables.waves[w1];
		m_wave2[_l] = m_tables.waves[w2];
		m_g1From[_l] = q31(g1);
		m_g1To[_l] = q31(to1);
		m_g2From[_l] = q31(g2);
		m_g2To[_l] = q31(to2);
	}

	void SyChord::control()
	{
		for(int l = 0; l < Lanes; ++l)
		{
			Trigs& trigs = m_controls.trigs;
			const bool now = trigs.now[l];
			bool pending = trigs.pending[l];

			if(now)
			{
				// The machine puts a Track on SY CHORD at its trig (0x40003ee0): the units' factors at the note's,
				// their increments and levels in the mix at 0, the crossfade at the sine; a trig or a gate-off in that
				// block is lost (Trigs::machineChange)
				if(!m_playing[l])
				{
					trigs.machineChange(l);
					pending = false;
					m_playing[l] = true;
					for(int k = 0; k < Units; ++k)
					{
						m_ratio[k][l] = NoteFactor;
						m_incWord[k][l] = 0;
						m_inc[k][l] = 0;
						m_gain[k][l] = 0;
					}
					m_lowGain[l] = m_highGain[l] = m_index[l] = m_lastLow[l] = m_lastHigh[l] = 0;
				}

				// The update at a trig: the phases, the filters' states, unit 0's level without a ramp, the attack and
				// the sweep's decay latched; the units' envelope starts at its attack, the sweep full, the amp full
				// without a ramp, held with HOLD
				for(auto& ph : m_phase)
					ph[l] = 0;
				for(int i = 0; i < 2; ++i)
					m_lpX[i][l] = m_lpY[i][l] = 0;
				m_gain[0][l] = q31(Level);
				m_attack[l] = m_attackNext[l];
				m_env.trig(l, m_attack[l], m_attack[l]);
				m_sweepDecay[l] = m_sweepDecayNext[l];
				m_sweep[l] = INT32_MAX;
				m_amp.trigFull(l, m_hold[l]);
				m_drive.trig(l, m_driveNext[l]);
			}
			else
			{
				// The units' envelope (0x40003c0e): up by its attack to full, then held; the sweep (0x40003cda) a
				// first-order fall in its words
				m_env.stepRise(l, m_attack[l], 0, 1);
				m_sweep[l] = frac(m_sweep[l], wrap(-int64_t(m_sweepDecay[l])));
				m_amp.stepAmp(l, m_ampDecay[l], 0, m_hold[l]);
				m_drive.step(l);
			}
			if(trigs.releases(l))
				m_amp.release(l);

			// The update's factors, which a shape that leaves a unit lets it keep, and the units' increments at 48 kHz
			// from the pitch word under them, every block: a unit above 2217 Hz keeps its increment, and units 1 to 3
			// leave the mix
			for(int k = 0; k < Units; ++k)
			{
				if(m_ratioSet[k][l])
					m_ratio[k][l] = m_ratioNext[k][l];
				const int32_t u = frac(m_ratio[k][l], m_pitch[l]);
				int32_t level = m_levelNext[k][l];
				if(u <= MaxUnitWord)
				{
					m_incWord[k][l] = frac(u, Rate) >> 2;
					m_inc[k][l] = static_cast<uint32_t>(m_incWord[k][l]) << 8;
				}
				else if(k)
				{
					level = 0;
				}
				m_level[k][l] = level;
				m_gainTarget[k][l] = q31(level);
			}

			crossfade(l);
			m_envFrom[l] = m_env.prev[l];
			m_envTo[l] = m_env.level[l];

			// The low-passes' pole: the cutoff under the sweep, up to 22 kHz, read from the pole table
			int64_t c = int64_t(frac(m_cut[l], wrap(int64_t(m_sweep[l]) - INT32_MAX))) + Top;
			c = std::clamp<int64_t>(wrap(c), 0, Top);
			const auto at = static_cast<int16_t>(static_cast<uint32_t>(store(2 * product(PoleScale,
				static_cast<int32_t>(c)))) >> 16);
			const int32_t c2 = interpolateWord(m_tables.pole, at);
			m_poleWord[l] = c2;
			m_lpB[l] = q31(wrap(int64_t(c2 >> 1) + 0x40000000));
			m_lpA[l] = -q31(c2);

			// The amp's VCA ramps over the block; a pending block fades it out, and leaves it silent until the trig
			for(int n = 0; n < BlockSize; ++n)
				m_gAmp.s[n][l] = m_amp.at(l, n / static_cast<double>(BlockSize));
			m_drive.end(l);
			fadePending(m_gAmp, m_amp, m_ampEnd, l, pending);
		}
	}

	const Block& SyChord::renderBlock()
	{
		const FlushDenormals ftz;

		m_controls.trigs.start();
		control();

		// The four units' waves crossfaded, mixed under their levels and their envelope
		for(int k = 0; k < Units; ++k)
			waveUnit(m_unit[k], m_phase[k], m_inc[k], m_wave1, m_wave2, m_g1From, m_g1To, m_g2From, m_g2To);
		const Block* in[4] = {&m_unit[0], &m_unit[1], &m_unit[2], &m_unit[3]};
		mix4(in, m_out, m_gain, m_gainTarget);
		for(int k = 0; k < Units; ++k)
			std::copy_n(m_gainTarget[k], Lanes, m_gain[k]);
		rampedGain(m_out, m_envFrom, m_envTo);

		// Into the drive, the two low-passes and the amp
		driveClip(m_out, m_drive.from, m_drive.to, m_drive.amount, m_drive.post);
		for(int i = 0; i < 2; ++i)
			onePoleLanes(m_out, m_lpB, m_lpA, m_lpX[i], m_lpY[i]);
		gain(m_out, m_gAmp);

		// The drive's ramp's end is the next block's start
		m_drive.advance();

		m_controls.trigs.end();
		return m_out;
	}
}
