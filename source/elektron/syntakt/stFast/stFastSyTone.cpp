#include "stFastSyTone.h"

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

		// The drive's gain envelope, one-pole per 48 kHz block: falling by half in 25 ms (37.5 blocks: 1.4e-8 of the
		// pole over the word), recovering with a time constant of ln 2 / 2 s (346.6 ms: 5.1e-9 under); its floor 0.25,
		// the clip's pre-gain 3 with the drive on, 1 off, the gain after it 0.25 (round values, the words)
		const float FallPole = static_cast<float>(std::pow(0.5, 1 / 37.5));
		const float RisePole = static_cast<float>(std::exp(-BlockSize / (MachineRate * std::log(2.0) / 2)));
		constexpr float DriveFloor = 0.25f;

		// The decimator's allpass, 1 + C0 z^-1 + C1 z^-2 + C2 z^-3 + C3 z^-4 over the odd samples (z^-1 two of 96 kHz):
		// round values of five digits (the words 5425, 195, 443 and 1047 LSB off, 2.5e-6, 9.1e-8, 2.1e-7 and 4.9e-7 of
		// full scale; its stopband from 30 kHz -52.14 dB, the firmware's -52.13, its passband to 18 kHz the same within
		// 1e-5 dB)
		constexpr float C0 = 0.48101f, C1 = -0.098467f, C2 = 0.033187f, C3 = -0.011871f;

		// TUNE at 64 plays the note: the machine's offset to the note is -64. Unit 1 plays at the note
		constexpr int PitchOffset = 64;
		constexpr int64_t NoteFactor = int64_t(1) << 26;

		// A unit's increment at 96 kHz (0x40002686) on lane _l: 0x40002596's at 48 kHz halved, rounded down as the
		// machine's rate constant, halved, rounds it; as a 32-bit phase step, wrapping
		void setIncrement96(Increments& _inc, const int _l, const int64_t _factor, const int64_t _pitch)
		{
			const double inc = std::floor(increment48(NoteFactor, _factor, int64_t(1) << 30, _pitch) / 2);
			for(auto& s : _inc.s)
				s[_l] = static_cast<uint32_t>(static_cast<uint64_t>(inc) * 256);
		}
	}

	namespace syTone
	{
		void feedbackUnit(Block (&_scaled)[2], uint32_t* _phase, const Increments& _inc, int32_t* _fb,
			const int32_t* _fbWord, int32_t* _last, int32_t* _from, const int32_t* _to)
		{
			for(int l = 0; l < Lanes; ++l)
			{
				const int32_t target = _fbWord[l] >> 4;
				int64_t phase = _phase[l] >> 8, y = _last[l], fb = _fb[l], amount = _from[l];
				const int64_t inc = _inc.s[0][l] >> 8;
				const int64_t step = (static_cast<int64_t>(target) - fb) >> 6;
				const int64_t amountStep = std::clamp<int64_t>(int64_t(_to[l]) - amount, INT32_MIN, INT32_MAX) / 64;
				for(int n = 0; n < 2 * BlockSize; ++n)
				{
					const int64_t s = feedbackSample(phase, fb, y) >> 1;
					y = -((amount * s) >> 23) >> 8;
					_scaled[n / BlockSize].s[n % BlockSize][l] = q31(static_cast<int32_t>(y));
					phase = (phase + inc) & 0xffffff;
					fb += step;
					amount += amountStep;
				}
				_phase[l] = static_cast<uint32_t>(phase << 8);
				_last[l] = static_cast<int32_t>(y);
				_fb[l] = target;
				_from[l] = _to[l];
			}
		}

		void decimate(const Block (&_in)[2], Block& _out, float (*_x)[Lanes], float (*_y)[Lanes])
		{
			const V c0 = _mm_set1_ps(C0), c1 = _mm_set1_ps(C1), c2 = _mm_set1_ps(C2), c3 = _mm_set1_ps(C3);
			const V half = _mm_set1_ps(0.5f);
			for(int g = 0; g < Lanes; g += 4)
			{
				V x[8 + 2 * BlockSize], y[4 + BlockSize];
				for(int k = 0; k < 8; ++k)
					x[k] = _mm_load_ps(_x[k] + g);
				for(int k = 0; k < 2 * BlockSize; ++k)
					x[8 + k] = _mm_mul_ps(half, _mm_load_ps(_in[k / BlockSize].s[k % BlockSize] + g));
				for(int k = 0; k < 4; ++k)
					y[k] = _mm_load_ps(_y[k] + g);
				for(int j = 0; j < BlockSize; ++j)
				{
					const V* xj = x + 2 * j;
					const V f = _mm_add_ps(_mm_add_ps(xj[1], _mm_mul_ps(c0, xj[3])),
						_mm_add_ps(_mm_mul_ps(c1, xj[5]), _mm_add_ps(_mm_mul_ps(c2, xj[7]), _mm_mul_ps(c3, xj[9]))));
					const V b = _mm_add_ps(_mm_add_ps(_mm_mul_ps(c3, y[j]), _mm_mul_ps(c2, y[j + 1])),
						_mm_add_ps(_mm_mul_ps(c1, y[j + 2]), _mm_mul_ps(c0, y[j + 3])));
					y[j + 4] = _mm_sub_ps(f, b);
					_mm_store_ps(_out.s[j] + g, clip(_mm_add_ps(xj[2], y[j + 4])));
				}
				for(int k = 0; k < 8; ++k)
					_mm_store_ps(_x[k] + g, x[2 * BlockSize + k]);
				for(int k = 0; k < 4; ++k)
					_mm_store_ps(_y[k] + g, y[BlockSize + k]);
			}
		}
	}

	SyTone::SyTone()
		: m_drive(FallPole, RisePole, DriveFloor, {3.0f, 0.25f}, {1.0f, 0.25f})
	{
		using tables::SyTone;
		Tables& t = m_tables;
		copyWords(t.feedback, tables::syTone(SyTone::Feedback));
		copyWords(t.ratio, tables::syTone(SyTone::Ratio));
		copyWords(t.index, tables::syTone(SyTone::Index));
		copyWords(t.modTarget, tables::syTone(SyTone::ModTarget));
		copyWords(t.modDecay, tables::syTone(SyTone::ModDecay));
		readQ31(t.ampDecay, tables::syTone(SyTone::AmpDecay));
		copyWords(t.exp2, tables::exp2());
		sineTable(t.sine);

		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void SyTone::setNote(const int _track, const int _note)
	{
		m_controls.notes[_track] = _note;
		convert(_track);
	}

	void SyTone::setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
	{
		if(m_controls.setParameter(_track, _index, _value, _fine))
			convert(_track);
	}

	void SyTone::testWithoutChaos()
	{
		std::fill_n(m_tables.feedback, 128, 0);
		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void SyTone::convert(const int _l)
	{
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		const auto byte = [this, _l](const int _i) { return m_controls.words[_i][_l] >> 8; };
		const Tables& t = m_tables;

		// TUNE: (value - 64 + fine / 256) semitones on the note; the second: unit 0's frequency factor
		const int32_t pitch = pitchWord(t.exp2, m_controls.notes[_l], word(0), PitchOffset);
		setIncrement96(m_inc[0], _l, interpolateWord(t.ratio, word(1)), pitch);
		setIncrement96(m_inc[1], _l, NoteFactor, pitch);

		// The third: unit 1's modulation, twice the table's word >> 6 (2^-24 cycle per unit); the fourth: unit 0's
		// feedback; the fifth: unit 0's level at rest, and its decay at a trig
		m_depthTarget[_l] = static_cast<float>(2 * (interpolateWord(t.index, word(2)) >> 6) / 16777216.0);
		m_fbWord[_l] = interpolateWord(t.feedback, word(3));
		m_target[_l] = interpolateWord(t.modTarget, word(4));
		m_decayNext[_l] = t.modDecay[byte(4)];

		// DECAY: the amp's, every block
		m_ampDecay[_l] = -t.ampDecay[byte(7)];

		m_driveNext[_l] = word(5) != 0;
		m_hold[_l] = word(6) != 0;
	}

	void SyTone::control()
	{
		for(int l = 0; l < Lanes; ++l)
		{
			Trigs& trigs = m_controls.trigs;
			const bool now = trigs.now[l];
			bool pending = trigs.pending[l];

			if(now)
			{
				// The machine puts a Track on SY TONE at its trig (0x40003ee0): unit 0's feedback and unit 1's
				// modulation ramp from 0 in the first block; a trig or a gate-off in that block is lost
				// (Trigs::machineChange)
				if(!m_playing[l])
				{
					trigs.machineChange(l);
					pending = false;
					m_playing[l] = true;
					m_fb[l] = 0;
					m_depth[l] = 0;
				}

				// The update (0x4000709c) at a trig: the phases, unit 0's last output, the decimator's history, unit
				// 0's decay latched. Unit 0's envelope and the amp start full without a ramp, the amp held with HOLD
				for(auto& ph : m_phase)
					ph[l] = 0;
				m_last[l] = 0;
				for(auto& x : m_decX)
					x[l] = 0;
				for(auto& y : m_decY)
					y[l] = 0;
				m_decay[l] = m_decayNext[l];
				m_envFrom[l] = m_env[l] = INT32_MIN;
				m_amp.trigFull(l, m_hold[l]);
				m_drive.trig(l, m_driveNext[l]);
			}
			else
			{
				// Unit 0's envelope (0x40003c0e) in its words: a first-order step toward its level at rest
				m_env[l] = envelopeDecay(m_env[l], m_decay[l], m_target[l]);
				m_amp.stepAmp(l, m_ampDecay[l], 0, m_hold[l]);
				m_drive.step(l);
			}
			if(trigs.releases(l))
				m_amp.release(l);

			// The amp's VCA ramps over the block; a pending block fades it out, and leaves it silent until the trig
			for(int n = 0; n < BlockSize; ++n)
				m_gAmp.s[n][l] = m_amp.at(l, n / static_cast<double>(BlockSize));
			m_drive.end(l);
			fadePending(m_gAmp, m_amp, m_ampEnd, l, pending);
		}
	}

	const Block& SyTone::renderBlock()
	{
		const FlushDenormals ftz;

		m_controls.trigs.start();
		control();

		// Unit 0 feeding back on its own output under its envelope, which modulates unit 1, both at 96 kHz; unit 1's
		// modulation ramped over the 64 samples, half of it in each block
		syTone::feedbackUnit(m_mod, m_phase[0], m_inc[0], m_fb, m_fbWord, m_last, m_envFrom, m_env);
		for(int l = 0; l < Lanes; ++l)
			m_depthMid[l] = 0.5f * (m_depth[l] + m_depthTarget[l]);
		pmWave(m_car[0], &m_mod[0], m_depth, m_depthMid, m_phase[1], m_inc[1], m_tables.sine);
		pmWave(m_car[1], &m_mod[1], m_depthMid, m_depthTarget, m_phase[1], m_inc[1], m_tables.sine);
		std::copy_n(m_depthTarget, Lanes, m_depth);

		// Unit 1 decimated to 48 kHz, under the amp, into the drive
		syTone::decimate(m_car, m_out, m_decX, m_decY);
		gain(m_out, m_gAmp);
		driveClip(m_out, m_drive.from, m_drive.to, m_drive.amount, m_drive.post);

		// The drive's ramp's end is the next block's start
		m_drive.advance();

		m_controls.trigs.end();
		return m_out;
	}
}
