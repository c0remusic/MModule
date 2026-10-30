#include "stFastSyBits.h"

#include <algorithm>
#include <cmath>

#include "stFastTables.h"

namespace syntakt::fast
{
	namespace
	{
		// The machine's constants by their laws, each against the firmware's word (measured by hand, the word kept out
		// of the source; the tests drive the exact port with it)

		// The noise's gate: 0.44 of full scale (0.12 LSB over the word)
		const int32_t Gate = static_cast<int32_t>(0.44 * 2147483648.0);

		// The pairs' sum in the mix with the noise: -12 dB (0.27 LSB under the word)
		const float WavesLevel = static_cast<float>(std::pow(10.0, -12 / 20.0));

		// The crossfade's two weights sum to 127/128; an offset of a cycle is 2^25 in the machine's phase
		constexpr int32_t FadeTop = 127 << 24;
		constexpr int32_t Cycle25 = 1 << 25;

		// TUNE at 64 plays the note: the machine's offset to the note is -64. Every unit's factor is the note's
		constexpr int PitchOffset = 64;
		constexpr int32_t NoteFactor = 1 << 26;
		constexpr int64_t NoteScale = int64_t(1) << 30;

		// DETUNE's words at which units 2 and 3 take unit 0's increment an octave down, as it is, an octave up
		constexpr int32_t OctaveDown = 52 << 8, Unison = 64 << 8, OctaveUp = 76 << 8;

		// The waves (SyBitsWaves' indices): the band-limited saw for a note, the triangle and the sine
		constexpr int Triangle = 8, Sine = 9;

		// 0x40006588's band: at or below 35, then ranges of 12 notes from 36 to 107, then from 108
		int band(const int _note)
		{
			return _note <= 35 ? 0 : std::min((_note - 24) / 12, 7);
		}

		// The machine's saturated difference, which its ramps step by
		int32_t subSat(const int32_t _a, const int32_t _b)
		{
			return static_cast<int32_t>(std::clamp<int64_t>(int64_t(_a) - _b, INT32_MIN, INT32_MAX));
		}

		// A wave of SyBitsWaveSize floats at a 32-bit phase: its top 9 bits the entry, the next 23 the fraction
		float read(const float* _w, const uint32_t _p)
		{
			const uint32_t i = _p >> 23;
			const float f = static_cast<float>(_p & 0x7fffff) * (1.0f / 8388608.0f);
			return _w[i] + f * (_w[i + 1] - _w[i]);
		}
	}

	void crossfadeWaves(Block& _out, const uint32_t* _phase, const uint32_t* _inc, const float* const* _wave1,
		const float* const* _wave2, const float* _from, const float* _to)
	{
		for(int l = 0; l < _out.lanes; ++l)
		{
			uint32_t p = _phase[l];
			float g1 = _from[l], g2 = _from[Lanes + l];
			const float s1 = (_to[l] - g1) / BlockSize, s2 = (_to[Lanes + l] - g2) / BlockSize;
			for(int n = 0; n < BlockSize; ++n)
			{
				_out.s[n][l] = g1 * read(_wave1[l], p) + g2 * read(_wave2[l], p);
				g1 += s1;
				g2 += s2;
				p += _inc[l];
			}
		}
	}

	void addOffsetWave(Block& _out, const uint32_t* _phase, const uint32_t* _inc, const float* const* _wave,
		const uint32_t* _offsetFrom, const uint32_t* _offsetStep, const float* _from, const float* _to)
	{
		for(int l = 0; l < _out.lanes; ++l)
		{
			uint32_t p = _phase[l], o = _offsetFrom[l];
			float g = _from[l];
			const float s = (_to[l] - g) / BlockSize;
			for(int n = 0; n < BlockSize; ++n)
			{
				_out.s[n][l] = std::clamp(_out.s[n][l] - g * read(_wave[l], p + o), -1.0f, 1.0f);
				g += s;
				o += _offsetStep[l];
				p += _inc[l];
			}
		}
	}

	void reduceBits(Block& _buf, const int32_t* _word)
	{
		for(int g = 0; g < _buf.lanes; g += 4)
		{
			alignas(16) float k0[4], k1[4], w1[4];
			for(int l = 0; l < 4; ++l)
			{
				const int32_t b = _word[g + l] >> 16;
				k0[l] = std::ldexp(1.0f, 31 - b);
				k1[l] = std::ldexp(1.0f, 30 - b);
				w1[l] = static_cast<float>(_word[g + l] & 0xffff) / 65536;
			}
			const simd::V up0 = _mm_load_ps(k0), up1 = _mm_load_ps(k1), wb = _mm_load_ps(w1);
			const simd::V down0 = _mm_div_ps(_mm_set1_ps(1), up0), down1 = _mm_div_ps(_mm_set1_ps(1), up1);
			for(int n = 0; n < BlockSize; ++n)
			{
				const simd::V x = _mm_load_ps(_buf.s[n] + g);
				const simd::V a = _mm_mul_ps(simd::floorOf(_mm_mul_ps(x, up0)), down0);
				const simd::V b = _mm_mul_ps(simd::floorOf(_mm_mul_ps(x, up1)), down1);
				_mm_store_ps(_buf.s[n] + g, _mm_add_ps(a, _mm_mul_ps(wb, _mm_sub_ps(b, a))));
			}
		}
	}

	SyBits::SyBits()
	{
		using tables::SyBits;
		Tables& t = m_tables;
		copyWords(t.phaseOffset, tables::syBits(SyBits::PhaseOffset));
		copyWords(t.bits, tables::syBits(SyBits::Bits));
		copyWords(t.sampleRate, tables::syBits(SyBits::SampleRate));
		copyWords(t.crossfade, tables::syBits(SyBits::Crossfade));
		copyWords(t.gain[0], tables::syBits(SyBits::GainA));
		copyWords(t.gain[1], tables::syBits(SyBits::GainB));
		copyWords(t.gain[2], tables::syBits(SyBits::GainNoise));
		readQ31(t.ampDecay, tables::syBits(SyBits::AmpDecay));
		copyWords(t.exp2, tables::exp2());
		for(int w = 0; w < SyBitsWaves; ++w)
			readQ31(t.wave[w], tables::syBitsWave(w));
		std::fill_n(m_wavesLevel, Lanes, WavesLevel);
		for(auto& w : m_wave)
			std::fill_n(w, Lanes, -1);

		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void SyBits::setNote(const int _track, const int _note)
	{
		m_controls.notes[_track] = _note;
		convert(_track);
	}

	void SyBits::setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
	{
		if(m_controls.setParameter(_track, _index, _value, _fine))
			convert(_track);
	}

	void SyBits::convert(const int _l)
	{
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		const auto byte = [this, _l](const int _i) { return m_controls.words[_i][_l] >> 8; };
		const Tables& t = m_tables;
		const int note = m_controls.notes[_l];

		// TUNE: (value - 64 + fine / 256) semitones on the note, units 0 and 1; DETUNE: units 2 and 3 at TUNE + DETUNE
		// - 64, the two words' sum wrapping as the machine's 16 bits
		const int32_t tuneB = static_cast<int16_t>(word(0) + word(1) - Unison);
		const int32_t pitchA = pitchWord(t.exp2, note, word(0), PitchOffset);
		const int64_t incA = increment24(NoteFactor, NoteFactor, NoteScale, pitchA);
		int64_t incB;
		if(word(1) == OctaveDown)
			incB = incA >> 1;
		else if(word(1) == Unison)
			incB = incA;
		else if(word(1) == OctaveUp)
			incB = std::min<int64_t>(2 * incA, INT32_MAX);
		else
			incB = increment24(NoteFactor, NoteFactor, NoteScale, pitchWord(t.exp2, note, tuneB, PitchOffset));
		m_incWord[0][_l] = static_cast<int32_t>(incA);
		m_incWord[1][_l] = static_cast<int32_t>(incB);

		// WAVE: the crossfade, units 1 and 3's offset and, from 43, their level; the waves by its region
		const int32_t w4 = word(4), b4 = byte(4);
		m_fade[_l] = interpolateWord(t.crossfade, w4);
		m_offset[_l] = frac(Cycle25, interpolateWord(t.phaseOffset, w4));
		m_levelWord[_l] = b4 > 42 ? m_fade[_l] : 0;
		const int a = pitchQ16(note, word(0), PitchOffset) >> 16, b = pitchQ16(note, tuneB, PitchOffset) >> 16;
		const int sawA = band(a), sawB = band(b);
		const int32_t waves[3][6] = {
			{Sine, Triangle, 0, Sine, Triangle, 0},
			{sawA, Triangle, sawA, sawB, Triangle, sawB},
			{sawA, sawA, sawA, sawB, sawB, sawB}};
		const int region = b4 < 22 ? 0 : b4 <= 42 ? 1 : 2;
		for(int k = 0; k < 6; ++k)
			m_waveNext[k][_l] = waves[region][k];
		m_unitWaves[_l] = region != 0;

		// The mix, the sample-rate reducer, BITS, DECAY (127: none) and HOLD
		for(int k = 0; k < 3; ++k)
			m_gainWord[k][_l] = interpolateWord(t.gain[k], word(2));
		m_rate[_l] = interpolateWord(t.sampleRate, word(3));
		m_bits[_l] = interpolateWord(t.bits, word(5));
		m_hold[_l] = word(6) != 0;
		m_ampDecay[_l] = byte(7) == 127 ? 1.0f : -t.ampDecay[byte(7)];
	}

	void SyBits::control()
	{
		for(int l = 0; l < m_lanes; ++l)
		{
			Trigs& trigs = m_controls.trigs;
			const bool now = trigs.now[l];
			bool pending = trigs.pending[l];

			if(now)
			{
				// The machine puts a Track on SY BITS at its trig (0x40003ee0): the crossfade's weights, the rate
				// reducer and the noise from their start, units 1 and 3 on the RAM sine (silent: their level is 0
				// until the update gives them a wave); a trig or a gate-off in that block is lost
				if(!m_playing[l])
				{
					trigs.machineChange(l);
					pending = false;
					m_playing[l] = true;
					for(auto& pair : m_weight)
						pair[0][l] = pair[1][l] = 0;
					m_wave[2][l] = m_wave[5][l] = -1;
					m_held[0][l] = m_held[1][l] = 0;
					m_heldCount[0][l] = m_heldCount[1][l] = 0;
					m_noise.start(l);
				}

				// The update (0x40008e0c) at a trig: the phases; no ramp of the mix's levels, units 1 and 3's level
				// or their offset; the amp full without a ramp, held with HOLD
				m_phase[0][l] = m_phase[1][l] = 0;
				for(int k = 0; k < 3; ++k)
					m_gainFrom[k][l] = m_gainWord[k][l];
				m_levelFrom[l] = m_levelWord[l];
				m_offsetFrom[l] = m_offset[l];
				m_amp.trigFull(l, m_hold[l]);
			}
			else
			{
				m_amp.stepAmp(l, m_ampDecay[l], 0, m_hold[l]);
			}
			if(trigs.releases(l))
				m_amp.release(l);

			// The update every block: the waves, units 1 and 3's from WAVE 22; the increments
			for(int k = 0; k < 6; ++k)
				if(m_unitWaves[l] || (k != 2 && k != 5))
					m_wave[k][l] = m_waveNext[k][l];
			for(int p = 0; p < 2; ++p)
				m_inc[p][l] = static_cast<uint32_t>(m_incWord[p][l]) << 7;

			// The crossfade's weights ramp by a 64th of the way a sample, half of it over the block, from their last
			// targets, which then take the new ones
			const int32_t target[2] = {wrap(int64_t(FadeTop) - m_fade[l]), m_fade[l]};
			for(int p = 0; p < 2; ++p)
				for(int w = 0; w < 2; ++w)
				{
					int32_t& v = m_weight[p][w][l];
					const int32_t step = wrap(int64_t(target[w]) - v) >> 6;
					m_wFrom[p][w][l] = q31(v);
					m_wTo[p][w][l] = q31(wrap(v + int64_t(BlockSize) * step));
					v = target[w];
				}

			// Units 1 and 3's offset and level, the mix's levels: ramped over the block from the last
			const int32_t offsetStep = subSat(m_offset[l], m_offsetFrom[l]) >> 5;
			m_offsetStart[l] = static_cast<uint32_t>(m_offsetFrom[l]) << 7;
			m_offsetStep[l] = static_cast<uint32_t>(offsetStep) << 7;
			m_offsetFrom[l] = m_offset[l];
			const int32_t levelStep = subSat(m_levelWord[l], m_levelFrom[l]) >> 5;
			m_lFrom[l] = q31(m_levelFrom[l]);
			m_lTo[l] = q31(wrap(m_levelFrom[l] + int64_t(BlockSize) * levelStep));
			m_levelFrom[l] = m_levelWord[l];
			for(int k = 0; k < 3; ++k)
			{
				m_gFrom[k][l] = q31(m_gainFrom[k][l]);
				m_gTo[k][l] = q31(m_gainWord[k][l]);
				m_gainFrom[k][l] = m_gainWord[k][l];
			}

			// The amp's VCA ramps over the block; a pending block fades it out, and leaves it silent until the trig
			for(int n = 0; n < BlockSize; ++n)
				m_gAmp.s[n][l] = m_amp.at(l, n / static_cast<double>(BlockSize));
			fadePending(m_gAmp, m_amp, m_ampEnd, l, pending);
		}
	}

	void SyBits::setLanes(const int _lanes)
	{
		m_lanes = _lanes;
		fast::setLanes(_lanes, {&m_gAmp, &m_pair[0], &m_pair[1], &m_noiseOut, &m_out});
	}

	const Block& SyBits::renderBlock()
	{
		const FlushDenormals ftz;

		m_controls.trigs.start();
		control();

		// Each pair: units 0 and 2 crossfading their two waves, units 1 and 3, at the same phase, their wave at the
		// offset under their level subtracted
		for(int p = 0; p < 2; ++p)
		{
			const float* w1[Lanes];
			const float* w2[Lanes];
			const float* w3[Lanes];
			for(int l = 0; l < m_lanes; ++l)
			{
				w1[l] = m_tables.wave[m_wave[3 * p][l]];
				w2[l] = m_tables.wave[m_wave[3 * p + 1][l]];
				w3[l] = m_wave[3 * p + 2][l] < 0 ? m_tables.wave[Sine] : m_tables.wave[m_wave[3 * p + 2][l]];
			}
			crossfadeWaves(m_pair[p], m_phase[p], m_inc[p], w1, w2, m_wFrom[p][0], m_wTo[p][0]);
			addOffsetWave(m_pair[p], m_phase[p], m_inc[p], w3, m_offsetStart, m_offsetStep, m_lFrom, m_lTo);
			for(int l = 0; l < m_lanes; ++l)
				m_phase[p][l] += BlockSize * m_inc[p][l];
		}

		// The pairs mixed, then the noise; the bits, the rate and the amp
		mix(m_pair[0], m_pair[1], m_out, m_gFrom[0], m_gTo[0], m_gFrom[1], m_gTo[1]);
		saturate(m_out, 1);	// the two -6 dB levels' sum passes full scale near MIX 127: the store saturates
		m_noise.render(m_noiseOut, Gate);
		mix(m_out, m_noiseOut, m_out, m_wavesLevel, m_wavesLevel, m_gFrom[2], m_gTo[2]);
		reduceBits(m_out, m_bits);
		reduceRate(m_out, m_rate, m_held, m_heldCount);
		gain(m_out, m_gAmp);

		m_controls.trigs.end();
		return m_out;
	}

	void SyBits::testWords(const int _track, int32_t* _words) const
	{
		const int l = _track;
		int i = 0;
		for(int p = 0; p < 2; ++p)
			_words[i++] = m_incWord[p][l], _words[i++] = m_incWord[p][l];
		for(int p = 0; p < 2; ++p)
		{
			_words[i++] = static_cast<int32_t>(m_phase[p][l] >> 7);	// units 0 and 1 (2 and 3) share their phase
			_words[i++] = static_cast<int32_t>(m_phase[p][l] >> 7);
		}
		_words[i++] = m_fade[l];
		_words[i++] = m_fade[l];
		for(int p = 0; p < 2; ++p)
			_words[i++] = m_weight[p][0][l], _words[i++] = m_weight[p][1][l];
		_words[i++] = m_offset[l];
		_words[i++] = m_offset[l];
		_words[i++] = m_levelWord[l];
		_words[i++] = m_levelWord[l];
		for(int k = 0; k < 3; ++k)
			_words[i++] = m_gainWord[k][l];
		_words[i++] = m_rate[l];
		_words[i++] = m_bits[l];
		for(int k = 0; k < 4; ++k)
			_words[i++] = static_cast<int32_t>(m_noise.words[k][l]);
		_words[i++] = m_heldCount[0][l];
		_words[i++] = m_heldCount[1][l];
		_words[i++] = m_amp.stage[l];
		_words[i++] = m_amp.released[l];
		for(int k = 0; k < 6; ++k)
			_words[i++] = m_wave[k][l];
	}
}
