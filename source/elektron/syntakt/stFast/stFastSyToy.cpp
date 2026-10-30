#include "stFastSyToy.h"

#include <algorithm>
#include <cmath>

#include "stFastTables.h"

namespace syntakt::fast
{
	namespace
	{
		// The machine's constants by their laws, each against the firmware's word (measured by hand, the word kept out
		// of the source; the tests drive the exact port with it)

		// The drive's gain envelope, one-pole per 48 kHz block: falling by half in 25 ms (37.5 blocks, SY TONE's: 1.4e-8
		// of the pole over the word), recovering with a time constant of 0.8 ln 2 s (554.5 ms: 1.9e-9 under); its
		// floor 0.25, the clip's pre-gain 1.12 (4 times 0.28) with the drive on, 1 off, the gain after it 1 on, 0.5 off
		// (round values, the words)
		const float FallPole = static_cast<float>(std::pow(0.5, 1 / 37.5));
		const float RisePole = static_cast<float>(std::exp(-BlockSize / (MachineRate * 0.8 * std::log(2.0))));
		constexpr float DriveFloor = 0.25f;

		// Unit 0's level in the mix: -9 dB, and -6 dB in a trig's first block with the drive on (the words, rounded)
		const int32_t Level0 = static_cast<int32_t>(std::llround(std::pow(10.0, -9 / 20.0) * 2147483648.0));
		const int32_t Level0Drive = static_cast<int32_t>(std::llround(std::pow(10.0, -6 / 20.0) * 2147483648.0));

		// TUNE at 64 plays the note: the machine's offset to the note is -64. Unit 0 plays at the note; a unit's ratio
		// is its factor (0x50) times the note's, its scale (0x64) the note's
		constexpr int PitchOffset = 64;
		constexpr int32_t NoteFactor = 1 << 26;
		constexpr int64_t NoteScale = int64_t(1) << 30;
	}

	SyToy::SyToy()
		: m_drive(FallPole, RisePole, DriveFloor, {1.12f, 1.0f}, {1.0f, 0.5f})
	{
		using tables::SyToy;
		Tables& t = m_tables;
		const SyToy ratios[4] = {SyToy::RatioU1, SyToy::RatioU2, SyToy::RatioU3, SyToy::RatioU4};
		const SyToy decays[4] = {SyToy::DecayU1, SyToy::DecayU2, SyToy::DecayU3, SyToy::DecayU4};
		const SyToy gains[3] = {SyToy::Gain1, SyToy::Gain2, SyToy::Gain3};
		for(int k = 0; k < 4; ++k)
		{
			copyWords(t.ratio[k], tables::syToy(ratios[k]));
			copyWords(t.decay[k], tables::syToy(decays[k]));
		}
		for(int k = 0; k < 3; ++k)
			copyWords(t.gain[k], tables::syToy(gains[k]));
		copyWords(t.depth, tables::syToy(SyToy::Depth));
		copyWords(t.feedback, tables::syToy(SyToy::Feedback));
		copyWords(t.target, tables::syToy(SyToy::Target));
		readQ31(t.ampDecay, tables::syToy(SyToy::AmpDecay));
		copyWords(t.exp2, tables::exp2());
		sineTable(t.sine);

		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void SyToy::setNote(const int _track, const int _note)
	{
		m_controls.notes[_track] = _note;
		convert(_track);
	}

	void SyToy::setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
	{
		if(m_controls.setParameter(_track, _index, _value, _fine))
			convert(_track);
	}

	void SyToy::testWithoutChaos()
	{
		std::fill_n(m_tables.feedback, 128, 0);
		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void SyToy::convert(const int _l)
	{
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		const auto byte = [this, _l](const int _i) { return m_controls.words[_i][_l] >> 8; };
		const Tables& t = m_tables;

		// TUNE: (value - 64 + fine / 256) semitones on the note; the second: units 1 to 4's ratios
		const int32_t pitch = pitchWord(t.exp2, m_controls.notes[_l], word(0), PitchOffset);
		for(int k = 0; k < Units; ++k)
		{
			const int32_t factor = k ? interpolateWord(t.ratio[k - 1], word(1)) : NoteFactor;
			setIncrement(m_inc[k], _l, NoteFactor, factor, NoteScale, pitch);
			m_incWord[k][_l] = static_cast<int32_t>(increment48(NoteFactor, factor, NoteScale, pitch));
		}

		// The third: unit 2's modulation by unit 4 (the word D / 2^7 of a 2^-24 cycle per unit: D cycles) and unit 4's
		// feedback
		m_depthWord[_l] = frac(0x01000000, interpolateWord(t.depth, word(2)));
		m_depthTarget[_l] = static_cast<float>(m_depthWord[_l] / 16777216.0);
		m_fbWord[_l] = interpolateWord(t.feedback, word(2));

		// The fourth: units 1 to 3's levels in the mix; the fifth: units 1 to 4's decays and their level at rest
		m_levelNext[0][_l] = Level0;
		for(int k = 1; k < 4; ++k)
			m_levelNext[k][_l] = interpolateWord(t.gain[k - 1], word(3));
		for(int k = 0; k < 4; ++k)
			m_decay[k][_l] = t.decay[k][byte(4)];
		m_target[_l] = interpolateWord(t.target, word(4));

		// DECAY: the amp's, every block
		m_ampDecay[_l] = -t.ampDecay[byte(7)];

		m_driveNext[_l] = word(5) != 0;
		m_hold[_l] = word(6) != 0;
	}

	void SyToy::control()
	{
		for(int l = 0; l < Lanes; ++l)
		{
			Trigs& trigs = m_controls.trigs;
			const bool now = trigs.now[l];
			bool pending = trigs.pending[l];

			for(int k = 0; k < 4; ++k)
				m_level[k][l] = m_levelNext[k][l];
			if(now)
			{
				// The machine puts a Track on SY TOY at its trig (0x40003ee0): unit 4's feedback and last sample, and
				// unit 2's modulation, from 0; a trig or a gate-off in that block is lost (Trigs::machineChange)
				if(!m_playing[l])
				{
					trigs.machineChange(l);
					pending = false;
					m_playing[l] = true;
					m_fb[l] = m_last[l] = 0;
					m_depth[l] = 0;
				}

				// The update (0x40008ae8) at a trig: the phases; the units' envelopes and the amp start full without a
				// ramp, the amp held with HOLD; the levels in the mix at their targets, unit 0's 3 dB up with the
				// drive on
				for(auto& ph : m_phase)
					ph[l] = 0;
				for(int k = 0; k < 4; ++k)
					m_envFrom[k][l] = m_env[k][l] = INT32_MIN;
				m_amp.trigFull(l, m_hold[l]);
				m_drive.trig(l, m_driveNext[l]);
				if(m_drive.on[l])
					m_level[0][l] = Level0Drive;
				for(int k = 0; k < 4; ++k)
					m_gain[k][l] = q31(m_level[k][l]);
			}
			else
			{
				// Units 1 to 4's envelopes (0x40003c0e) in their words: a first-order step toward their level at rest
				for(int k = 0; k < 4; ++k)
				{
					m_envFrom[k][l] = m_env[k][l];
					m_env[k][l] = envelopeDecay(m_env[k][l], m_decay[k][l], m_target[l]);
				}
				m_amp.stepAmp(l, m_ampDecay[l], 0, m_hold[l]);
				m_drive.step(l);
			}
			if(trigs.releases(l))
				m_amp.release(l);

			for(int k = 0; k < 4; ++k)
			{
				m_gFrom[k][l] = -q31(m_envFrom[k][l]);
				m_gTo[k][l] = -q31(m_env[k][l]);
				m_gainTarget[k][l] = q31(m_level[k][l]);
			}

			// The amp's VCA ramps over the block; a pending block fades it out, and leaves it silent until the trig
			for(int n = 0; n < BlockSize; ++n)
				m_gAmp.s[n][l] = m_amp.at(l, n / static_cast<double>(BlockSize));
			m_drive.end(l);
			fadePending(m_gAmp, m_amp, m_ampEnd, l, pending);
		}
	}

	const Block& SyToy::renderBlock()
	{
		const FlushDenormals ftz;

		m_controls.trigs.start();
		control();

		// Unit 4 feeding back on its sample, under its envelope, modulates unit 2; units 1 to 3 under theirs, unit 0
		// alone
		feedbackSine(m_mod, m_phase[4], m_inc[4], m_fb, m_fbWord, m_last);
		rampedGain(m_mod, m_gFrom[3], m_gTo[3]);
		alignas(16) static constexpr float Zero[Lanes] = {};
		pmWave(m_unit[0], nullptr, Zero, Zero, m_phase[0], m_inc[0], m_tables.sine);
		pmWave(m_unit[1], nullptr, Zero, Zero, m_phase[1], m_inc[1], m_tables.sine);
		pmWave(m_unit[2], &m_mod, m_depth, m_depthTarget, m_phase[2], m_inc[2], m_tables.sine);
		pmWave(m_unit[3], nullptr, Zero, Zero, m_phase[3], m_inc[3], m_tables.sine);
		std::copy_n(m_depthTarget, Lanes, m_depth);
		for(int k = 1; k < 4; ++k)
			rampedGain(m_unit[k], m_gFrom[k - 1], m_gTo[k - 1]);

		// The four mixed under their levels, under the amp, into the drive
		const Block* in[4] = {&m_unit[0], &m_unit[1], &m_unit[2], &m_unit[3]};
		mix4(in, m_out, m_gain, m_gainTarget);
		for(int k = 0; k < 4; ++k)
			std::copy_n(m_gainTarget[k], Lanes, m_gain[k]);
		gain(m_out, m_gAmp);
		driveClip(m_out, m_drive.from, m_drive.to, m_drive.amount, m_drive.post);

		// The drive's ramp's end is the next block's start
		m_drive.advance();

		m_controls.trigs.end();
		return m_out;
	}
}
