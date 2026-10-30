#include "stFastPcCarbon.h"

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
		constexpr float HoldRest = 0.707f;		// units 1 and 3's levels at rest with HOLD: 0.707 rounds to the word
		constexpr float DriveOn = 0.33f;		// drive on: its pre-gain, x 4 = 1.32: rounds to the word
		constexpr float DriveFloor = 0.25f;		// drive on: its gain's floor, 2^-2: the word itself

		// The drive's gain envelope, one-pole per 48 kHz block (2/3 ms) of a round time constant: falling 36 ms
		// (72899 LSB under the word, 3.5e-5 of the pole, 0.19 % of the firmware's 36.07 ms), recovering 555 ms (2237
		// LSB over, 1.0e-6 of the pole, 0.09 % of its 554.5 ms)
		const float FallPole = static_cast<float>(std::exp(-BlockSize / (MachineRate * 0.036)));
		const float RisePole = static_cast<float>(std::exp(-BlockSize / (MachineRate * 0.555)));

		// TUNE at 64 plays the note: the machine's offset to the note is -64
		constexpr int PitchOffset = 64;
	}

	PcCarbon::PcCarbon() : m_drive(FallPole, RisePole, DriveFloor, {4 * DriveOn, 1.0f}, {1.0f, 1.0f})
	{
		using tables::PcCarbon;
		Tables& t = m_tables;
		readQ31(t.decayPosition, tables::pcCarbon(PcCarbon::DecayPosition));
		readQ31(t.decayCurve, tables::pcCarbonDecayCurve());
		readQ31(t.decayU0, tables::pcCarbon(PcCarbon::DecayU0));
		readQ31(t.decayU2, tables::pcCarbon(PcCarbon::DecayU2));
		readQ31(t.depthU1, tables::pcCarbon(PcCarbon::DepthU1));
		readQ31(t.depthU3, tables::pcCarbon(PcCarbon::DepthU3));
		readQ31(t.targetU0, tables::pcCarbon(PcCarbon::TargetU0));
		readQ31(t.targetU2, tables::pcCarbon(PcCarbon::TargetU2));
		copyWords(t.factor[0], tables::pcCarbon(PcCarbon::FactorU0));
		copyWords(t.factor[1], tables::pcCarbon(PcCarbon::FactorU1));
		copyWords(t.factor[2], tables::pcCarbon(PcCarbon::FactorU2));
		copyWords(t.sweepDepth, tables::pcCarbon(PcCarbon::SweepDepth));
		copyWords(t.sweepDecay, tables::pcCarbon(PcCarbon::SweepDecay));
		copyWords(t.exp2, tables::exp2());
		readQ31(t.wave, tables::pcCarbonWave());
		t.wave[257] = t.wave[256];
		sineTable(t.sine);

		// The voice before its first trig: silent, the drive's gain at 1 recovering (Drive)
		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void PcCarbon::setNote(const int _track, const int _note)
	{
		m_controls.notes[_track] = _note;
		convert(_track);
	}

	void PcCarbon::setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
	{
		if(m_controls.setParameter(_track, _index, _value, _fine))
			convert(_track);
	}

	float PcCarbon::decayAt(const double _x) const
	{
		// 0x40007280's curve: entry i = x's top 10 bits, interpolated on the next 21; past 1022 the last, twice
		const double u = std::clamp(_x, 0.0, 1.0) * 1024;
		const int i = std::min(static_cast<int>(u), 1023);
		const float* c = m_tables.decayCurve;
		if(i > 1022)
			return -c[1023];
		const auto f = static_cast<float>(u - i);
		return -((1 - f) * c[i] + f * c[i + 1]);
	}

	void PcCarbon::convert(const int _l)
	{
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		const auto byte = [this, _l](const int _i) { return m_controls.words[_i][_l] >> 8; };
		const Tables& t = m_tables;

		// TUNE: (value - 64 + fine / 256) semitones on the note
		m_pitch[_l] = pitchWord(t.exp2, m_controls.notes[_l], word(0), PitchOffset);

		// The second: units 0-2's frequency factors, unit 3 at the note's
		for(int k = 0; k < 3; ++k)
			m_factor[k][_l] = interpolateWord(t.factor[k], word(1));
		m_factor[3][_l] = 0x04000000;

		// The third: units 1 and 3's modulation (the word D >> 6, 2 D cycles), and unit 1's decay read from the curve
		// at 2 (0.75 - 0.55 w) times unit 3's place on it, w the word in [0, 1)
		m_depthTarget[0][_l] = 2 * interpolate(t.depthU1, word(2));
		m_depthTarget[1][_l] = 2 * interpolate(t.depthU3, word(2));
		const double position = t.decayPosition[byte(7)];
		const double scale = 0.75 - 0.55 * word(2) / 32768.0;
		m_decayOdd[0][_l] = decayAt(2 * scale * position);
		m_decayOdd[1][_l] = decayAt(position);

		// SWEEP: its depth read every block, its decay at a trig (per 48 kHz block)
		m_sweepDepth[_l] = t.sweepDepth[byte(3)];
		m_sweepDecayNext[_l] = -t.sweepDecay[byte(3)] / 2147483648.0;

		// The fifth: units 0 and 2's levels at rest
		m_target[0][_l] = interpolate(t.targetU0, word(4));
		m_target[2][_l] = interpolate(t.targetU2, word(4));

		m_driveNext[_l] = word(5) != 0;
		m_hold[_l] = word(6) != 0;

		// DECAY: units 0 and 2's, latched at a trig
		m_decayNext[0][_l] = -t.decayU0[byte(7)];
		m_decayNext[1][_l] = -t.decayU2[byte(7)];
	}

	void PcCarbon::control()
	{
		for(int l = 0; l < m_lanes; ++l)
		{
			Trigs& trigs = m_controls.trigs;
			const bool now = trigs.now[l];
			bool pending = trigs.pending[l];

			// With HOLD the amp decays as unit 3 did in the block before (0x40007280 reads its decay before it sets it)
			if(m_hold[l])
				m_ampDecay[l] = m_decayU3Prev[l];

			// The update and the envelopes' 48 kHz block 0: what a trig restarts and latches
			if(now)
			{
				// The machine puts a Track on PC CARBON at its trig (0x40003ee0): units 1 and 3's modulation ramps
				// from 0 in the first block; a trig or a gate-off in that block is lost (Trigs::machineChange)
				if(!m_playing[l])
				{
					trigs.machineChange(l);
					pending = false;
					m_playing[l] = true;
					m_depth[0][l] = m_depth[1][l] = 0;
				}
				for(auto& p : m_phase)
					p[l] = 0;
				for(auto& e : m_env)
					e.prev[l] = e.level[l] = 1;
				m_decay[0][l] = m_decayNext[0][l];
				m_decay[2][l] = m_decayNext[1][l];
				m_target[1][l] = m_target[3][l] = m_hold[l] ? HoldRest : 0.0f;
				m_drive.trig(l, m_driveNext[l]);
				m_sweepDecay[l] = m_sweepDecayNext[l];
				m_sweep[l] = 2147483647.0 / 2147483648.0;
				m_clock.restart(l);

				// The units' envelopes start from 1 without a ramp; the amp rises from the trig's block's silence to
				// full over the block (half its step, then its step, saturated), and stays full without HOLD: units
				// 1 and 3 decay instead
				m_amp.trigRise(l, 1);
				if(!m_hold[l])
					m_ampDecay[l] = 1;
			}
			m_decay[1][l] = m_decayOdd[0][l];
			m_decay[3][l] = m_decayOdd[1][l];
			m_decayU3Prev[l] = m_decay[3][l];

			// The units' increments at 48 kHz under the sweep (its level a fraction, from INT32_MAX at the trig), as
			// the machine computes them in integers; the machine steps the sweep before the next block's increments
			const int64_t m = sweepWord(m_sweepDepth[l], m_sweep[l]);
			for(int k = 0; k < Units; ++k)
				setIncrement(m_inc[k], l, m, m_factor[k][l], 0x40000000, m_pitch[l]);
			m_sweep[l] *= m_sweepDecay[l];

			// The envelopes on the block clock; each sample reads their ramps. The amp, full from the trig, has no
			// attack to rise by
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

			// Drive on: unit 3 at 0.5 and unit 1 at 0.25 into the drive (a pre-gain of 1.32); off, 0.25 and 0.125
			m_gainU3[l] = m_drive.on[l] ? 0.5f : 0.25f;
			m_gainU1[l] = m_drive.on[l] ? 0.25f : 0.125f;
		}
	}

	void PcCarbon::setLanes(const int _lanes)
	{
		m_lanes = _lanes;
		fast::setLanes(_lanes, {&m_gEnv[0], &m_gEnv[1], &m_gEnv[2], &m_gEnv[3], &m_b[0], &m_b[1], &m_b[2], &m_b[3],
			&m_gAmp, &m_out});
	}

	const Block& PcCarbon::renderBlock()
	{
		const FlushDenormals ftz;

		m_controls.trigs.start();
		control();

		// The units under their envelopes: unit 0 alone modulating unit 1, unit 2 alone modulating unit 3
		alignas(16) static constexpr float Zero[Lanes] = {};
		const Tables& t = m_tables;
		pmWave(m_b[0], nullptr, Zero, Zero, m_phase[0], m_inc[0], t.wave);
		gain(m_b[0], m_gEnv[0]);
		pmWave(m_b[1], &m_b[0], m_depth[0], m_depthTarget[0], m_phase[1], m_inc[1], t.sine);
		gain(m_b[1], m_gEnv[1]);
		pmWave(m_b[2], nullptr, Zero, Zero, m_phase[2], m_inc[2], t.sine);
		gain(m_b[2], m_gEnv[2]);
		pmWave(m_b[3], &m_b[2], m_depth[1], m_depthTarget[1], m_phase[3], m_inc[3], t.sine);
		gain(m_b[3], m_gEnv[3]);
		for(int i = 0; i < 2; ++i)
			std::copy_n(m_depthTarget[i], m_lanes, m_depth[i]);

		// Units 3 and 1 mixed, into the drive and the amp
		mix(m_b[3], m_b[1], m_out, m_gainU3, m_gainU3, m_gainU1, m_gainU1);
		driveClip(m_out, m_drive.from, m_drive.to, m_drive.amount, m_drive.post);
		gain(m_out, m_gAmp);

		// The drive's ramp's end is the next block's start
		m_drive.advance();

		m_controls.trigs.end();
		return m_out;
	}
}
