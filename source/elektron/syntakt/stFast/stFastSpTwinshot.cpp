#include "stFastSpTwinshot.h"

#include <algorithm>

#include "stFastTables.h"

namespace syntakt::fast
{
	namespace
	{
		using namespace twinshotRecord;

		// The machine's constants by their laws

		// TUNE at 64 plays the note: its offset to the note is -64 semitones (Q16); the rate is the exp2 table's word
		// at note 60, an octave a shift
		constexpr int32_t TuneOffset = -(64 << 16), Note60 = 60 << 16, TopPitch = 127 << 16;
		constexpr int32_t Twelfth = 0x0aaaaaab;	// 1/12, rounded up
		constexpr int32_t RateShift = 18;

		// The crossfade's word as a fraction of 127: 64/127, which its doubling makes 1/127 a value
		constexpr int32_t Crossfade = 0x40810204;	// round(2^31 64 / 127)
		constexpr int32_t Half = 0x40000000;

		// The band-pass's word at which both poles are open as far as they go
		constexpr int32_t BandCentre = 0x4000;

		// A slot's length is clamped to 2^19 - 1 samples
		constexpr uint32_t MaxLength = 0x7ffff;

		int32_t addSat(const int32_t _a, const int32_t _b)
		{
			return static_cast<int32_t>(std::clamp<int64_t>(int64_t(_a) + _b, INT32_MIN, INT32_MAX));
		}

		int32_t asl(const int32_t _v, const int _n) { return wrap(static_cast<int64_t>(static_cast<uint32_t>(_v)) << _n); }

		// asr.l by a register: the count mod 64, 32 or more shifting everything out
		int32_t asrReg(const int32_t _v, const int32_t _n)
		{
			const int32_t n = _n & 63;
			return n >= 32 ? (_v < 0 ? -1 : 0) : _v >> n;
		}

		// 0x40006704: a player's rate from the note and its TUNE word, Q12 samples per output sample
		int32_t playerRate(const int32_t* _exp2, const int _note, const int32_t _tune)
		{
			int32_t p = addSat(wrap(int64_t(asl(_tune, 8)) + TuneOffset), _note << 16);
			p = std::clamp(p, 0, TopPitch);
			p = frac(wrap(int64_t(p) - Note60), Twelfth);	// octaves from note 60
			const int32_t fraction = (p & 0xffff) >> 5;
			const int32_t octave = static_cast<int16_t>(static_cast<uint32_t>(p) >> 16);
			return asrReg(_exp2[fraction], RateShift - octave);
		}

		// The slot a player's byte chooses: byte - 64, its complement and reversed when negative, 0 to 63
		int32_t slotOf(const int32_t _byte, bool& _reverse)
		{
			const int32_t t = _byte - 0x40;
			_reverse = static_cast<int16_t>(t) < 0;
			return std::clamp<int32_t>(static_cast<int16_t>(_reverse ? ~t : t), 0, 63);
		}

		int32_t clampedLength(const TwinshotSamples::Slot& _s)
		{
			return asl(static_cast<int32_t>(std::min(_s.length, MaxLength)), 12);
		}

		// Sample _i of the sample at _pointer: 0 outside the data
		int32_t sampleAt(const TwinshotSamples* _samples, const uint32_t _pointer, const int32_t _i)
		{
			if(!_samples)
				return 0;
			const uint32_t k = (_pointer + 2u * static_cast<uint32_t>(_i) - _samples->base) >> 1;
			return k < _samples->data.size() ? _samples->data[k] : 0;
		}

		// Two samples weighted by the position's fraction (_weight1 for _s1, Q31), each product truncated as the
		// machine's accumulator holds it, the sum stored and kept to its 16 bits
		int32_t interpolated(const int32_t _s0, const int32_t _s1, const int32_t _weight1)
		{
			return asl(store(product(_s0, INT32_MAX - _weight1) + product(_s1, _weight1)), 16);
		}
	}

	void twinshotPlayer(Block& _out, const int _l, int32_t* _r, const TwinshotSamples* _samples)
	{
		const int32_t rate = _r[Rate];
		int32_t length = _r[Length];
		const int32_t loop = _r[Loop];
		auto pointer = static_cast<uint32_t>(_r[Pointer]);
		int32_t pos = _r[Position];
		const int32_t nextPointer = _r[NextPointer];
		const int32_t nextLength = _r[NextLength];

		if(!_r[Playing] || length <= 0 || !pointer)
		{
			for(int n = 0; n < BlockSize; ++n)
				_out.s[n][_l] = 0;
			return;
		}

		// The last sample's index, from the length at the block's start: a loop does not change it
		const int32_t last = (length >> 12) - 1;
		int32_t v[BlockSize];
		int n = 0;
		bool stopped = false;
		if(!_r[Reverse])
		{
			for(; n < BlockSize; ++n)
			{
				const int32_t i0 = pos >> 12;
				const int32_t i1 = i0 + 1 <= last ? i0 + 1 : 0;
				const int32_t w = asl(pos & 0xfff, 19);
				pos = wrap(int64_t(pos) + rate);
				v[n] = interpolated(sampleAt(_samples, pointer, i0), sampleAt(_samples, pointer, i1), w);
				if(length > pos)
					continue;
				if(loop && nextLength > 0 && nextPointer)
				{
					// What is past the end, modulo the next length
					pos = wrap(int64_t(pos) - length) % nextLength;
					pointer = static_cast<uint32_t>(nextPointer);
					_r[Pointer] = nextPointer;
					_r[Length] = nextLength;
					length = nextLength;
					continue;
				}
				stopped = true;
				break;
			}
		}
		else
		{
			for(; n < BlockSize; ++n)
			{
				// The fraction goes to the later sample, which is behind
				const int32_t w = asl(pos & 0xfff, 19);
				const int32_t i1 = pos >> 12;
				const int32_t i0 = i1 - 1 >= 0 ? i1 - 1 : last;
				v[n] = interpolated(sampleAt(_samples, pointer, i0), sampleAt(_samples, pointer, i1), w);
				pos = wrap(int64_t(pos) - rate);
				if(pos >= 0)
					continue;
				if(loop)
				{
					// The length plus the negative position, modulo the next length; at a next length of 0 the
					// position stays
					if(nextLength)
						pos = wrap(int64_t(length) + pos) % nextLength;
					pointer = static_cast<uint32_t>(nextPointer);
					_r[Pointer] = nextPointer;
					_r[Length] = nextLength;
					length = nextLength;
					continue;
				}
				stopped = true;
				break;
			}
		}

		if(stopped)
		{
			// The sample written last is cleared with the rest
			_r[Playing] = 0;
			pos = 0;
			for(; n < BlockSize; ++n)
				v[n] = 0;
		}
		_r[Position] = pos;
		for(int k = 0; k < BlockSize; ++k)
			_out.s[k][_l] = q31(v[k]);
	}

	void twinshotBandPass(Block& _buf, const float* _c0, const float* _c1, float* _s1, float* _s2)
	{
		using namespace simd;
		for(int g = 0; g < _buf.lanes; g += 4)
		{
			const V c0 = _mm_load_ps(_c0 + g), c1 = _mm_load_ps(_c1 + g);
			V s1 = _mm_load_ps(_s1 + g), s2 = _mm_load_ps(_s2 + g);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V x = _mm_load_ps(_buf.s[n] + g);
				const V d1 = _mm_mul_ps(clip(_mm_sub_ps(x, s1)), c0);
				const V l1 = clip(_mm_add_ps(d1, s1));
				const V d2 = _mm_mul_ps(clip(_mm_sub_ps(l1, s2)), c1);
				const V l2 = clip(_mm_add_ps(d2, s2));
				_mm_store_ps(_buf.s[n] + g, clip(_mm_sub_ps(l1, l2)));
				s1 = clip(_mm_add_ps(l1, d1));
				s2 = clip(_mm_add_ps(l2, d2));
			}
			_mm_store_ps(_s1 + g, s1);
			_mm_store_ps(_s2 + g, s2);
		}
	}

	SpTwinshot::SpTwinshot() : m_exp2(tables::exp2()), m_pole(tables::spTwinshotPole())
	{
		readQ31(m_ampDecayTable, tables::syBits(tables::SyBits::AmpDecay));
		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void SpTwinshot::setNote(const int _track, const int _note)
	{
		m_controls.notes[_track] = _note;
		convert(_track);
	}

	void SpTwinshot::setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
	{
		if(m_controls.setParameter(_track, _index, _value, _fine))
			convert(_track);
	}

	void SpTwinshot::convert(const int _l)
	{
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		const auto byte = [this, _l](const int _i) { return m_controls.words[_i][_l] >> 8; };
		const int note = m_controls.notes[_l];

		// TUNE: (value - 64 + fine / 256) semitones on the note, each player
		m_rateWord[0][_l] = playerRate(m_exp2.data(), note, word(0));
		m_rateWord[1][_l] = playerRate(m_exp2.data(), note, word(4));

		// The crossfade x: the first player at 1 - x^2, the second at 1 - (1 - x)^2, both halved
		const int32_t f = frac(Crossfade, asl(word(3), 16));
		const int32_t x = addSat(f, f);
		const int32_t a = x >> 1;
		const int32_t b = (INT32_MAX - x) >> 1;
		m_levelWord[0][_l] = frac(asl(frac(b, INT32_MAX - b), 2), Half);
		m_levelWord[1][_l] = frac(asl(frac(a, INT32_MAX - a), 2), Half);

		// The band-pass: at its centre both poles open as far as they go, below it the low-pass closes, above it the
		// high-pass opens
		const int32_t w = word(2);
		const int last = static_cast<int>(m_pole.size()) - 1;
		const int low = w < BandCentre ? std::max(w >> 7, 0) : last;
		const int high = w > BandCentre ? std::clamp(((w - BandCentre) >> 7) + 1, 0, last) : 0;
		m_poleWord[0][_l] = m_pole[static_cast<size_t>(low)];
		m_poleWord[1][_l] = m_pole[static_cast<size_t>(high)];

		// The samples, DECAY (127: none)
		m_slot[0][_l] = slotOf(byte(1), m_reverse[0][_l]);
		m_slot[1][_l] = slotOf(byte(5), m_reverse[1][_l]);
		m_ampDecay[_l] = byte(7) == 127 ? 1.0f : -m_ampDecayTable[byte(7)];
	}

	void SpTwinshot::control()
	{
		const TwinshotSamples::Slot none;
		for(int l = 0; l < m_lanes; ++l)
		{
			Trigs& trigs = m_controls.trigs;
			const bool now = trigs.now[l];
			bool pending = trigs.pending[l];

			// The machine puts a Track on SP TWINSHOT at its trig (0x40003ee0): a trig or a gate-off in that block is
			// lost
			if(now && !m_playing[l])
			{
				trigs.machineChange(l);
				pending = false;
				m_playing[l] = true;
			}

			// The update (0x400097dc) every block: the rates, the levels' targets, the band-pass, the second player's
			// loop from HOLD, the slots each player goes on at and their directions
			const TwinshotSamples::Slot* slot[2];
			int32_t length[2];
			for(int p = 0; p < 2; ++p)
			{
				int32_t* r = m_record[p][l];
				r[Rate] = m_rateWord[p][l];
				slot[p] = m_samples ? &m_samples->slots[static_cast<size_t>(m_slot[p][l])] : &none;
				length[p] = clampedLength(*slot[p]);
				if(length[p])
				{
					r[NextPointer] = static_cast<int32_t>(slot[p]->address);
					r[NextLength] = length[p];
				}
				r[Reverse] = m_reverse[p][l];
				m_target[p][l] = pending ? 0 : m_levelWord[p][l];
			}
			m_record[1][l][Loop] = static_cast<int16_t>(m_controls.words[6][l]);
			m_c0[l] = q31(m_poleWord[0][l]);
			m_c1[l] = q31(m_poleWord[1][l]);

			// A trig: the levels without a ramp, the band-pass from silence, both players from their start (the
			// reversed one from its last sample, of its length before the clamp); the amp full, never held
			if(now)
			{
				m_s1[l] = m_s2[l] = 0;
				for(int p = 0; p < 2; ++p)
				{
					m_from[p][l] = m_target[p][l];
					int32_t* r = m_record[p][l];
					r[Pointer] = r[NextPointer] = static_cast<int32_t>(slot[p]->address);
					r[Length] = r[NextLength] = length[p];
					r[Position] = 0;
					r[Playing] = length[p] != 0;
					if(length[p] && m_reverse[p][l])
						r[Position] = asl(static_cast<int32_t>(slot[p]->length - 1), 12);
				}
				m_amp.trigFull(l, false);
			}
			else
			{
				m_amp.stepAmp(l, m_ampDecay[l], 0, false);
			}
			if(trigs.releases(l))
				m_amp.release(l);

			// The mix's levels ramp over the block from the last
			for(int p = 0; p < 2; ++p)
			{
				m_gFrom[p][l] = q31(m_from[p][l]);
				m_gTo[p][l] = q31(m_target[p][l]);
				m_from[p][l] = m_target[p][l];
			}

			// The amp's VCA ramps over the block; a pending block fades it out, and leaves it silent until the trig
			for(int n = 0; n < BlockSize; ++n)
				m_gAmp.s[n][l] = m_amp.at(l, n / static_cast<double>(BlockSize));
			fadePending(m_gAmp, m_amp, m_ampEnd, l, pending);
		}
	}

	void SpTwinshot::setLanes(const int _lanes)
	{
		m_lanes = _lanes;
		fast::setLanes(_lanes, {&m_gAmp, &m_player[0], &m_player[1], &m_out});
	}

	const Block& SpTwinshot::renderBlock()
	{
		const FlushDenormals ftz;

		m_controls.trigs.start();
		control();

		// The players; the first band-passed under the amp, then the two mixed
		for(int p = 0; p < 2; ++p)
			for(int l = 0; l < m_lanes; ++l)
				twinshotPlayer(m_player[p], l, m_record[p][l], m_samples);
		twinshotBandPass(m_player[0], m_c0, m_c1, m_s1, m_s2);
		gain(m_player[0], m_gAmp);
		mix(m_player[0], m_player[1], m_out, m_gFrom[0], m_gTo[0], m_gFrom[1], m_gTo[1]);

		m_controls.trigs.end();
		return m_out;
	}

	void SpTwinshot::testWords(const int _track, int32_t* _words) const
	{
		const int l = _track;	// the lane
		int i = 0;
		for(int p = 0; p < 2; ++p)
			for(int k = 0; k < Words; ++k)
				_words[i++] = m_record[p][l][k];
		_words[i++] = m_poleWord[0][l];
		_words[i++] = m_poleWord[1][l];
		for(int p = 0; p < 2; ++p)
			_words[i++] = m_target[p][l];
		for(int p = 0; p < 2; ++p)
			_words[i++] = m_from[p][l];
		_words[i++] = m_amp.stage[l];
		_words[i++] = m_amp.released[l];
	}
}
