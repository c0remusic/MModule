#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "elektronBlock.h"

// The Digitakt II's six track filters as FILTER types (ticket 09 of .scratch/elektron-drums-v2), after the laws its
// OS 1.16 plays: elektron-work's vectors-dt2/FILTERS.md, measured on the DSP's own code (dt2_filter_oracle.py). Its
// tables are formulas here, rounded as the firmware's are (to 7 decimals, LEGACY's cosine to 5), and the fits of
// FILTERS.md; nothing of the firmware is in the repository.
//
// Each filter takes its settings as the DT2's setter reads its record: the cutoff word (knob / 128 plus the key
// tracking, the combs' knob / 127), p68 and p70 (knob / 127 or / 128, by type). Every block it runs the setter on
// them, as the DT2 does, and its coefficient words ramp over the block from the last block's. A Hit resets the state
// where it sounds, and the first block after starts every ramp at its own value.
//
// The laws are at 48 kHz. At another rate MULTI-MODE, LOWPASS 4 and LEGACY compute their 48 kHz words, then carry the
// filter they make to the rate (dt2::Rerate), LOWPASS 4's smoothing on the clock of the DT2's blocks; EQUALIZER and the
// combs keep their frequencies in hertz: the centre and the width, the combs' pitch and the time their low-pass delays
// the loop, a frequency above 22 kHz x rate / 48 kHz stopping there.
namespace elektron::drums
{
	namespace dt2
	{
		// a / _d and sqrt(_x) as the setters' routines compute them on the emulator the vectors come from: a seed (the
		// true value with its low 15 or 19 mantissa bits cleared), then Newton steps, every operation in float. One ulp
		// off the true value now and then, which EQUALIZER and LEGACY amplify at low cutoffs (FILTERS.md)
		float div(float _a, float _d);
		float sqrt(float _x);

		// The setter's cutoff word from a Kit's FREQ as a word (value << 7, an LFO's or the envelope's move in it), the
		// key tracking (a value, 100 one semitone of cutoff per semitone) and the note: FREQ / 128 + kt (note - 48)
		// 0.0826 / 12; the combs' FREQ / 127 as the chain hands it to them
		float frequency(int _word, int _keyTrack, int _note, bool _comb);

		// p68 and p70 from a Kit's value as a word: value / 127 as the DSP's frame unpacker makes it (dt2Knob),
		// or / 128
		float knob127(int _word);
		float knob128(int _word);

		// A 48 kHz filter carried to another rate: its poles where the filter's are, its zeros those whose magnitude
		// meets its response best from DC to 18 kHz, at the frequencies here. A quadratic's power there is
		// B0 cos^2(w/2) + B1 sin^2(w/2) + B2 sin^2 w, B0 = (b0 + b1 + b2)^2, B1 = (b0 - b1 + b2)^2, B2 = -4 b0 b2: the
		// three are kept for each frequency, at 48 kHz and at the rate. At 48 kHz nothing is carried
		struct Rerate
		{
			static constexpr int Points = 15;
			using Basis = std::array<std::array<double, 3>, Points>;

			explicit Rerate(double _rate);
			bool carries() const { return ratio != 1; }

			double ratio;					// 48 kHz over the rate
			Basis at48, at;
		};
	}

	// What every type keeps per lane: the setter's three settings, the words of the last block and this one's, the
	// lanes a Hit resets in the next block and those whose ramps start at their own value
	template<int Words>
	class Dt2FilterBase
	{
	public:
		void set(const int _lane, const float _frequency, const float _p68, const float _p70)
		{
			if(m_frequency[_lane] == _frequency && m_p68[_lane] == _p68 && m_p70[_lane] == _p70)
				return;
			m_frequency[_lane] = _frequency;
			m_p68[_lane] = _p68;
			m_p70[_lane] = _p70;
			m_changed |= static_cast<uint16_t>(1 << _lane);
		}
		void hit(const int _lane) { m_hit |= static_cast<uint16_t>(1 << _lane); }

	protected:
		// Before a block, on the lanes of _lanes below _count: a Hit's reset, then the setter, where the settings
		// changed or a reset or a clear starts the ramps at their own words, or _always; those lanes
		template<typename Reset, typename Setter>
		uint16_t prepare(const uint16_t _lanes, const int _count, Reset&& _reset, Setter&& _setter,
			const bool _always = false)
		{
			uint16_t fresh = 0;
			for(int l = 0; l < _count; ++l)
			{
				const auto bit = static_cast<uint16_t>(1 << l);
				if(!(_lanes & bit))
					continue;
				if(m_hit & bit)
				{
					_reset(l);
					m_fresh |= bit;
				}
				// Unchanged, the setter gives the words it gave: they stand where the last block left them
				if(_always || (m_changed | m_fresh) & bit)
					_setter(l);
				m_changed &= static_cast<uint16_t>(~bit);
				if(!(m_fresh & bit))
					continue;
				fresh |= bit;
				for(int w = 0; w < Words; ++w)
					m_now[w][l] = m_to[w][l];
			}
			m_hit &= static_cast<uint16_t>(~_lanes);
			m_fresh &= static_cast<uint16_t>(~_lanes);
			return fresh;
		}

		// After it: the words where it ended
		void settle(const uint16_t _lanes)
		{
			for(int w = 0; w < Words; ++w)
				for(int l = 0; l < Lanes; ++l)
					if(_lanes >> l & 1)
						m_now[w][l] = m_to[w][l];
		}

		void cleared(const int _lane)
		{
			m_fresh |= static_cast<uint16_t>(1 << _lane);
			m_hit &= static_cast<uint16_t>(~(1 << _lane));
		}

		float m_frequency[Lanes]{}, m_p68[Lanes]{}, m_p70[Lanes]{};
		alignas(16) float m_now[Words][Lanes]{}, m_to[Words][Lanes]{};
		uint16_t m_hit = 0, m_fresh = 0xffff, m_changed = 0;
	};

	// MULTI-MODE (index 1): a TPT state-variable filter, LP to BP at p68 64, BP to HP at 127, the three weights
	// ramping; cutoff and resonance (Q 0.5 to 20.5) change at the block's start. At another rate the SVF and its three
	// weights are the 48 kHz filter carried
	class Dt2Multimode : public Dt2FilterBase<6>
	{
	public:
		explicit Dt2Multimode(double _rate);
		void process(Block& _buf, uint16_t _lanes = 0xffff);
		void clear(int _lane);

	private:
		enum { G, GK, A1, Wl, Wb, Wh };
		void setter(int _lane);
		dt2::Rerate m_rerate;
		std::array<float, 1024> m_cut;
		std::array<float, 128> m_reso, m_morph;
		alignas(16) float m_s1[Lanes]{}, m_s2[Lanes]{};
	};

	// EQUALIZER (index 2): a peaking EQ around a coupled-form allpass, FREQ its centre (not interpolated), p70 its gain
	// (-11.96 to +12.04 dB), p68 its width
	class Dt2Equalizer : public Dt2FilterBase<9>
	{
	public:
		explicit Dt2Equalizer(double _rate);
		void process(Block& _buf, uint16_t _lanes = 0xffff);
		void clear(int _lane);

	private:
		void setter(int _lane);
		std::array<float, 1024> m_f, m_cos, m_width, m_radius;
		std::array<float, 128> m_gain, m_narrow, m_cut;		// by the gain's index: g, Gq, the cut's widening
		alignas(16) float m_sa[Lanes]{}, m_sb[Lanes]{};
	};

	// LOWPASS 4 (index 3): four bilinear one-poles in a zero-delay feedback with a tanh, its knobs smoothed per block.
	// At another rate each one-pole is the 48 kHz one carried (a pole and a zero), and the words follow the DT2's 48
	// kHz blocks in time: where a block ends, its words are the DT2's ramps there
	class Dt2Lowpass4 : public Dt2FilterBase<7>
	{
	public:
		explicit Dt2Lowpass4(double _rate);
		void process(Block& _buf, uint16_t _lanes = 0xffff);
		void clear(int _lane);

	private:
		enum { B0, B1, A, C4, G4, Comp, Fb8, WordCount };
		void step48(int _lane);			// one DT2 block: the knobs' smoothing, then its words
		void setter(int _lane);
		dt2::Rerate m_rerate;
		double m_blocks48;				// the DT2's blocks in one of this rate's
		std::array<float, 1024> m_g, m_a, m_tanh;
		float m_smoothF[Lanes]{}, m_smoothR[Lanes]{};
		float m_history[4][WordCount][Lanes]{};		// the DT2's last blocks' words, by their number since the Hit & 3
		double m_clock[Lanes]{};		// the DT2's blocks since the Hit, where the next block starts
		int m_steps[Lanes]{};			// the DT2's blocks computed since the Hit
		int m_settled[Lanes]{};			// of the last, those whose words were the one before's, up to 4
		uint16_t m_standing = 0;		// the lanes whose words per sample are their settled ones
		alignas(16) float m_perSample[WordCount][BlockSize][Lanes]{};	// at another rate, the words of each sample
		alignas(16) float m_s[5][Lanes]{};
	};

	// COMB- and COMB+ (indices 4 and 6): a feedback comb on an 8192-word ring per lane, a one-pole low-pass (p68) in
	// its loop, p70 its feedback; COMB- sounds an octave below. Its read moves from the last block's delay to the new
	// one
	class Dt2Comb : public Dt2FilterBase<2>
	{
	public:
		static constexpr int Ring = 8192;

		Dt2Comb(double _rate, bool _positive);
		void process(Block& _buf, uint16_t _lanes = 0xffff);
		void clear(int _lane);

	private:
		enum { Delay, Feedback };
		void setter(int _lane);
		double m_rate;
		float m_sign;
		std::array<float, 128> m_feedback;
		float m_gain[Lanes]{};			// the loop low-pass's, which changes at the block's start
		float m_s[Lanes]{};
		int m_write[Lanes]{};
		uint16_t m_filling = 0xffff;	// the write index has not wrapped since the Hit: words past it are silence
		std::vector<float> m_ring;		// Lanes x Ring
	};

	// LEGACY LP/HP (index 5): a two-pole all-pole low-pass in a coupled form, the high-pass x - LP; p68 x 128 picks
	// low-pass (0), high-pass (1) or dry (2 and above), the mix ramping. A lane placed anew ramps its mix from nothing,
	// as the DT2's does after a change of type; after a Hit from its own. At another rate the low-pass is the 48 kHz
	// one its words make, carried and played as Dt2Multimode's SVF: its words then G, GK, A1 and the three weights, the
	// coupled form's float32 C, within 6e-6 of 1 at low cutoffs, left behind
	class Dt2Legacy : public Dt2FilterBase<8>
	{
	public:
		explicit Dt2Legacy(double _rate);
		void process(Block& _buf, uint16_t _lanes = 0xffff);
		void clear(int _lane);

	private:
		enum { G, C, D, E, S, K, MixX, MixY };
		void setter(int _lane);
		dt2::Rerate m_rerate;
		std::array<float, 1024> m_w0, m_cos, m_limit;
		std::array<float, 128> m_q, m_t;
		uint16_t m_placed = 0;			// cleared since its last Hit: the mix ramps from 0
		alignas(16) float m_sa[Lanes]{}, m_sb[Lanes]{};
	};
}
