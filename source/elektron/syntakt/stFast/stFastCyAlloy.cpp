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

	CyAlloy::CyAlloy()
		: m_drive(-q31(FallPoleWord), -q31(RisePoleWord), q31(DriveOnWord), {4 * q31(DriveOnWord), q31(INT32_MAX)},
			{1.0f, q31(PostOffWord)})
	{
		using tables::CyAlloy;
		Tables& t = m_tables;
		readQ31(t.decayOdd, tables::cyAlloy(CyAlloy::DecayOdd));
		readQ31(t.decayEven, tables::cyAlloy(CyAlloy::DecayEven));
		readQ31(t.target, tables::cyAlloy(CyAlloy::Target));
		copyWords(t.depth, tables::cyAlloy(CyAlloy::Depth));
		readQ31(t.ampDecay, tables::cyAlloy(CyAlloy::AmpDecay));
		copyWords(t.ratio, tables::cyAlloy(CyAlloy::Ratio));
		copyWords(t.spread, tables::cyAlloy(CyAlloy::Spread));
		copyWords(t.exp2, tables::exp2());
		readQ31(t.wave, tables::cyAlloyWave());
		t.wave[257] = t.wave[256];
		sineTable(t.sine);

		// The voice before its first trig: silent, the drive's gain at 1 recovering (Drive)
		for(int l = 0; l < Lanes; ++l)
			convert(l);
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
		m_controls.notes[_track] = _note;
		convert(_track);
	}

	void CyAlloy::setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
	{
		if(m_controls.setParameter(_track, _index, _value, _fine))
			convert(_track);
	}

	void CyAlloy::testWithoutChaos()
	{
		std::fill_n(m_tables.depth, 128, 0);
		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void CyAlloy::convert(const int _l)
	{
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		const auto byte = [this, _l](const int _i) { return m_controls.words[_i][_l] >> 8; };
		const Tables& t = m_tables;

		// TUNE: (value - 64 + fine / 256) semitones on the note, 24 up
		m_pitch[_l] = pitchWord(t.exp2, m_controls.notes[_l], word(0), PitchOffset);

		// The frequency factors: unit 1 at Ratio, unit 2 at Spread, unit 0 at their product, unit 3 at the note's,
		// each product truncated as the machine's
		const int64_t ratio = interpolateWord(t.ratio, word(3)), spread = interpolateWord(t.spread, word(1));
		m_factor[0][_l] = static_cast<int32_t>(static_cast<uint32_t>(ratio * spread >> 31) << 5);
		m_factor[1][_l] = ratio;
		m_factor[2][_l] = spread;
		m_factor[3][_l] = 0x04000000;

		// The third: units 1 and 3's modulation (the word D >> 6, 2 D cycles) and unit 2's feedback (the word D >> 3,
		// D / 2 cycles a unit, as SdBasic's 4 F)
		const int32_t d = interpolateWord(t.depth, word(2));
		m_depthTarget[_l] = 2 * q31(d);
		m_fbWord[_l] = d >> 3;

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

	void CyAlloy::control()
	{
		for(int l = 0; l < m_lanes; ++l)
		{
			Trigs& trigs = m_controls.trigs;
			const bool now = trigs.now[l];
			bool pending = trigs.pending[l];

			// The update (0x40006d7c) and the envelopes' 48 kHz block 0: what a trig restarts and latches
			if(now)
			{
				// The machine puts a Track on CY ALLOY at its trig (0x40003ee0): unit 2's feedback and units 1 and
				// 3's modulation ramp from 0 in the first block; a trig or a gate-off in that block is lost
				// (Trigs::machineChange)
				if(!m_playing[l])
				{
					trigs.machineChange(l);
					pending = false;
					m_playing[l] = true;
					m_fb[l] = 0;
					m_depth[l] = 0;
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
				m_drive.trig(l, m_driveNext[l]);
				m_clock.restart(l);

				// The units' envelopes and the amp's start from 1 without a ramp; the amp holds or decays from the
				// next block
				m_amp.trigFull(l, m_hold[l]);
			}

			// No sweep: each unit's increment is the machine's at 48 kHz, as it computes it in integers
			for(int k = 0; k < Units; ++k)
				setIncrement(m_inc[k], l, 0x04000000, m_factor[k][l], 0x40000000, m_pitch[l]);

			// The envelopes on the block clock; each sample reads their ramps
			for(int s = m_clock.advance(l); s > 0; --s)
			{
				for(int k = 0; k < Units; ++k)
					m_env[k].stepToward(l, m_decay[k][l], m_target[k][l]);
				m_amp.stepAmp(l, m_ampDecay[l], 0, m_hold[l]);
				m_drive.step(l);
			}
			for(int n = 0; n < BlockSize; ++n)
			{
				const double f = n / static_cast<double>(BlockSize);
				for(int k = 0; k < Units; ++k)
					m_gEnv[k].s[n][l] = m_env[k].at(l, f);
				m_gAmp.s[n][l] = m_amp.at(l, f);
			}
			if(trigs.releases(l))
				m_amp.release(l);

			// The drive's gain ramps over the block, slow enough
			m_drive.end(l);

			// A pending block fades the amp out, and leaves it silent until the trig
			fadePending(m_gAmp, m_amp, m_ampEnd, l, pending);
		}
	}

	void CyAlloy::setLanes(const int _lanes)
	{
		m_lanes = _lanes;
		fast::setLanes(_lanes, {&m_gEnv[0], &m_gEnv[1], &m_gEnv[2], &m_gEnv[3], &m_gAmp, &m_b[0], &m_b[1], &m_b[2],
			&m_b[3], &m_out});
	}

	const Block& CyAlloy::renderBlock()
	{
		const FlushDenormals ftz;

		m_controls.trigs.start();
		control();

		// The units under their envelopes: unit 0 alone modulating unit 1, unit 2 feeding back on itself modulating
		// unit 3, both modulations at the same depth
		const Tables& t = m_tables;
		pmWave(m_b[0], nullptr, m_depth, m_depthTarget, m_phase[0], m_inc[0], t.sine);
		gain(m_b[0], m_gEnv[0]);
		pmWave(m_b[1], &m_b[0], m_depth, m_depthTarget, m_phase[1], m_inc[1], t.wave);
		gain(m_b[1], m_gEnv[1]);
		feedbackSine(m_b[2], m_phase[2], m_inc[2], m_fb, m_fbWord, m_last);
		gain(m_b[2], m_gEnv[2]);
		pmWave(m_b[3], &m_b[2], m_depth, m_depthTarget, m_phase[3], m_inc[3], t.wave);
		gain(m_b[3], m_gEnv[3]);
		std::copy_n(m_depthTarget, m_lanes, m_depth);

		// Units 1 and 3 at 0.5 each, high-passed, into the drive and the amp
		alignas(16) static constexpr float Half[Lanes] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
			0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
		mix(m_b[3], m_b[1], m_out, Half, Half, Half, Half);
		biquad(m_out, m_bq, m_bqState);
		driveClip(m_out, m_drive.from, m_drive.to, m_drive.amount, m_drive.post);
		gain(m_out, m_gAmp);

		// The drive's ramp's end is the next block's start
		m_drive.advance();

		m_controls.trigs.end();
		return m_out;
	}
}
