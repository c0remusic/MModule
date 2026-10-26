#include "stFastBdModern.h"

#include <algorithm>
#include <cmath>

#include <emmintrin.h>

#include "stVoice.h"

namespace syntakt::fast
{
	namespace
	{
		using V = __m128;

		constexpr float Pi = 3.14159265358979f;
		constexpr float MachineRate = 48000.0f;

		float q31(const int32_t _v) { return static_cast<float>(static_cast<double>(_v) / 2147483648.0); }

		// Section 7's tables (OS 1.41) that BD MODERN reads
		constexpr uint32_t Wave = 0x4001e7fc;			// unit 2's wave, 257 entries
		constexpr uint32_t SweepDepth = 0x4001ec00;
		constexpr uint32_t SweepDecay = 0x4001ee00;
		constexpr uint32_t ClickLevel = 0x4001f400;
		constexpr uint32_t ModDecay = 0x4001f600;
		constexpr uint32_t AmpDecay = 0x4001f800;
		constexpr uint32_t ModTarget = 0x4001fa00;
		constexpr uint32_t Feedback = 0x4001fc00;
		constexpr uint32_t LevelU0 = 0x4001fe00;
		constexpr uint32_t LevelU2 = 0x40020000;
		constexpr uint32_t Exp2 = 0x40059ef8;			// 2048 entries, which the boot copies to RAM 0x8000a888

		// The pitch map's scale (0x40002596): at 48 kHz, a unit's increment in cycles is its frequency factor times
		// PitchScale exp2[index] 2^octave, the products of 0x6e000000 and 0x57619f10 and the shifts the routine makes
		constexpr float PitchScale = static_cast<float>(0x6e000000 / 2147483648.0 * (0x57619f10 / 2147483648.0)) / 32;

		// The firmware's constants at 48 kHz, as Q31 words: poles of the block-rate filters, the click's step
		constexpr int32_t U2DecayWord = static_cast<int32_t>(0x8aa5ca3b);	// negative: the coefficient is -c
		constexpr int32_t ClickStepWord = 0x004468c0;
		constexpr int32_t HpPoleWord = static_cast<int32_t>(0x841f111a);	// -pole
		constexpr int32_t LpPoleWord = static_cast<int32_t>(0xafb69397);
		constexpr int32_t FallPoleWord = static_cast<int32_t>(0x85c7e84d);
		constexpr int32_t RisePoleWord = static_cast<int32_t>(0x809d339f);

		// 0x40004dc6: a 128-entry table read at a parameter word, linearly interpolated
		float interpolate(const float* _table, const int32_t _word)
		{
			const int32_t i = std::max(_word >> 8, 0);
			const float f = static_cast<float>(_word - (i << 8)) / 256.0f;
			return (1 - f) * _table[i] + f * _table[std::min(i + 1, 127)];
		}

		// A first-order bilinear filter's pole at 48 kHz moved to _rate at the same cutoff
		float bilinearPole(const float _pole48, const float _rate)
		{
			const double k48 = (1.0 - _pole48) / (1.0 + _pole48);
			const double k = std::tan(std::atan(k48) * MachineRate / _rate);
			return static_cast<float>((1 - k) / (1 + k));
		}

		V ramp(const float* _v, const int _g) { return _mm_load_ps(_v + _g); }

		V stepOf(const float* _from, const float* _to, const int _g)
		{
			const V d = _mm_sub_ps(_mm_load_ps(_to + _g), _mm_load_ps(_from + _g));
			return _mm_mul_ps(d, _mm_set1_ps(1.0f / BlockSize));
		}

		// A 32-bit phase as a fraction of a cycle in [0, 1), to 24 bits
		V phaseOf(const __m128i _p)
		{
			return _mm_mul_ps(_mm_cvtepi32_ps(_mm_srli_epi32(_p, 8)), _mm_set1_ps(1.0f / 16777216.0f));
		}

		V floorOf(const V _x)
		{
			const V t = _mm_cvtepi32_ps(_mm_cvttps_epi32(_x));
			return _mm_sub_ps(t, _mm_and_ps(_mm_cmplt_ps(_x, t), _mm_set1_ps(1.0f)));
		}

		// sin(2 pi _u): _u reduced to [-1/2, 1/2], folded to [-1/4, 1/4], then the odd Taylor series to x^11 (error
		// below 1e-7 on [-pi/2, pi/2]). The firmware reads a 256-entry sine linearly interpolated (error ~8e-5)
		V sin2pi(const V _u)
		{
			const V sign = _mm_set1_ps(-0.0f);
			V r = _mm_sub_ps(_u, _mm_cvtepi32_ps(_mm_cvtps_epi32(_u)));
			const V folded = _mm_sub_ps(_mm_or_ps(_mm_set1_ps(0.5f), _mm_and_ps(r, sign)), r);
			const V big = _mm_cmpgt_ps(_mm_andnot_ps(sign, r), _mm_set1_ps(0.25f));
			r = _mm_or_ps(_mm_and_ps(big, folded), _mm_andnot_ps(big, r));
			const V x = _mm_mul_ps(r, _mm_set1_ps(2 * Pi));
			const V x2 = _mm_mul_ps(x, x);
			V p = _mm_set1_ps(-1.0f / 39916800.0f);
			p = _mm_add_ps(_mm_mul_ps(p, x2), _mm_set1_ps(1.0f / 362880.0f));
			p = _mm_add_ps(_mm_mul_ps(p, x2), _mm_set1_ps(-1.0f / 5040.0f));
			p = _mm_add_ps(_mm_mul_ps(p, x2), _mm_set1_ps(1.0f / 120.0f));
			p = _mm_add_ps(_mm_mul_ps(p, x2), _mm_set1_ps(-1.0f / 6.0f));
			p = _mm_add_ps(_mm_mul_ps(p, x2), _mm_set1_ps(1.0f));
			return _mm_mul_ps(p, x);
		}

		// A 256-entry wave at _u cycles (258 values: 257 and a copy of the last), linearly interpolated as the machine
		// reads its waves
		V lookup(const V _u, const float* _wave)
		{
			const V u = _mm_mul_ps(_mm_sub_ps(_u, floorOf(_u)), _mm_set1_ps(256.0f));
			const __m128i k = _mm_cvttps_epi32(u);
			const V f = _mm_sub_ps(u, _mm_cvtepi32_ps(k));
			alignas(16) int32_t i[4];
			alignas(16) float a[4], b[4];
			_mm_store_si128(reinterpret_cast<__m128i*>(i), k);
			for(int l = 0; l < 4; ++l)	// a gather: SSE2 has none
			{
				a[l] = _wave[i[l]];
				b[l] = _wave[i[l] + 1];
			}
			const V va = _mm_load_ps(a);
			return _mm_add_ps(va, _mm_mul_ps(f, _mm_sub_ps(_mm_load_ps(b), va)));
		}

		V clip(const V _x)
		{
			return _mm_max_ps(_mm_min_ps(_x, _mm_set1_ps(1.0f)), _mm_set1_ps(-1.0f));
		}

		// RAII: denormals flushed to zero, in and out, while a block renders
		struct FlushDenormals
		{
			unsigned int csr = _mm_getcsr();
			FlushDenormals() { _mm_setcsr(csr | 0x8040); }
			~FlushDenormals() { _mm_setcsr(csr); }
		};
	}

	void mix(const Block& _x, const Block& _y, Block& _out, const float* _gxFrom, const float* _gxTo,
		const float* _gyFrom, const float* _gyTo)
	{
		for(int g = 0; g < Lanes; g += 4)
		{
			V gx = ramp(_gxFrom, g), gy = ramp(_gyFrom, g);
			const V sx = stepOf(_gxFrom, _gxTo, g), sy = stepOf(_gyFrom, _gyTo, g);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V x = _mm_mul_ps(gx, _mm_load_ps(_x.s[n] + g)), y = _mm_mul_ps(gy, _mm_load_ps(_y.s[n] + g));
				_mm_store_ps(_out.s[n] + g, _mm_add_ps(x, y));
				gx = _mm_add_ps(gx, sx);
				gy = _mm_add_ps(gy, sy);
			}
		}
	}

	void rampedGain(Block& _buf, const float* _from, const float* _to)
	{
		for(int g = 0; g < Lanes; g += 4)
		{
			V v = ramp(_from, g);
			const V s = stepOf(_from, _to, g);
			for(int n = 0; n < BlockSize; ++n)
			{
				_mm_store_ps(_buf.s[n] + g, _mm_mul_ps(v, _mm_load_ps(_buf.s[n] + g)));
				v = _mm_add_ps(v, s);
			}
		}
	}

	void onePole(Block& _buf, const OnePole& _c, float* _x1, float* _y1)
	{
		const V b0 = _mm_set1_ps(_c.b0), b1 = _mm_set1_ps(_c.b1), a1 = _mm_set1_ps(_c.a1);
		for(int g = 0; g < Lanes; g += 4)
		{
			V x1 = _mm_load_ps(_x1 + g), y1 = _mm_load_ps(_y1 + g);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V x = _mm_load_ps(_buf.s[n] + g);
				y1 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(b0, x), _mm_mul_ps(b1, x1)), _mm_mul_ps(a1, y1));
				x1 = x;
				_mm_store_ps(_buf.s[n] + g, y1);
			}
			_mm_store_ps(_x1 + g, x1);
			_mm_store_ps(_y1 + g, y1);
		}
	}

	void sum(const Block& _a, const Block& _b, Block& _out)
	{
		for(int n = 0; n < BlockSize; ++n)
			for(int g = 0; g < Lanes; g += 4)
				_mm_store_ps(_out.s[n] + g, clip(_mm_add_ps(_mm_load_ps(_a.s[n] + g), _mm_load_ps(_b.s[n] + g))));
	}

	void feedbackSine(Block& _out, uint32_t* _phase, const Increments& _inc, float* _fb, const float* _fbTarget,
		float* _last, const float* _wave)
	{
		for(int g = 0; g < Lanes; g += 4)
		{
			__m128i p = _mm_load_si128(reinterpret_cast<const __m128i*>(_phase + g));
			V fb = _mm_load_ps(_fb + g), s = _mm_load_ps(_last + g);
			const V step = stepOf(_fb, _fbTarget, g);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V u = _mm_add_ps(phaseOf(p), _mm_mul_ps(fb, s));
				s = _wave ? lookup(u, _wave) : sin2pi(u);
				_mm_store_ps(_out.s[n] + g, s);
				p = _mm_add_epi32(p, _mm_load_si128(reinterpret_cast<const __m128i*>(_inc.s[n] + g)));
				fb = _mm_add_ps(fb, step);
			}
			_mm_store_si128(reinterpret_cast<__m128i*>(_phase + g), p);
			_mm_store_ps(_last + g, s);
		}
		std::copy_n(_fbTarget, Lanes, _fb);
	}

	void pmSine(Block& _out, const Block* _src, uint32_t* _phase, const Increments& _inc, const float _depth)
	{
		const V depth = _mm_set1_ps(_depth);
		for(int g = 0; g < Lanes; g += 4)
		{
			__m128i p = _mm_load_si128(reinterpret_cast<const __m128i*>(_phase + g));
			for(int n = 0; n < BlockSize; ++n)
			{
				V u = phaseOf(p);
				if(_src)
					u = _mm_add_ps(u, _mm_mul_ps(depth, _mm_load_ps(_src->s[n] + g)));
				_mm_store_ps(_out.s[n] + g, sin2pi(u));
				p = _mm_add_epi32(p, _mm_load_si128(reinterpret_cast<const __m128i*>(_inc.s[n] + g)));
			}
			_mm_store_si128(reinterpret_cast<__m128i*>(_phase + g), p);
		}
	}

	void pmWave(Block& _out, const Block& _src, uint32_t* _phase, const Increments& _inc, const float _depth,
		const float* _wave)
	{
		const V depth = _mm_set1_ps(_depth);
		for(int g = 0; g < Lanes; g += 4)
		{
			__m128i p = _mm_load_si128(reinterpret_cast<const __m128i*>(_phase + g));
			for(int n = 0; n < BlockSize; ++n)
			{
				const V u = _mm_add_ps(phaseOf(p), _mm_mul_ps(depth, _mm_load_ps(_src.s[n] + g)));
				_mm_store_ps(_out.s[n] + g, lookup(u, _wave));
				p =_mm_add_epi32(p, _mm_load_si128(reinterpret_cast<const __m128i*>(_inc.s[n] + g)));
			}
			_mm_store_si128(reinterpret_cast<__m128i*>(_phase + g), p);
		}
	}

	void driveClip(Block& _buf, const float* _from, const float* _to, const float* _drive, const float* _post)
	{
		for(int g = 0; g < Lanes; g += 4)
		{
			const V post = _mm_load_ps(_post + g), drive = _mm_load_ps(_drive + g);
			V v = _mm_mul_ps(ramp(_from, g), post);
			const V s = _mm_mul_ps(stepOf(_from, _to, g), post);
			for(int n = 0; n < BlockSize; ++n)
			{
				_mm_store_ps(_buf.s[n] + g, _mm_mul_ps(v, clip(_mm_mul_ps(drive, _mm_load_ps(_buf.s[n] + g)))));
				v = _mm_add_ps(v, s);
			}
		}
	}

	void click(Block& _out, float* _ramp, const float _dec, const float* _from, const float* _to)
	{
		const V dec = _mm_set1_ps(_dec), zero = _mm_setzero_ps();
		for(int g = 0; g < Lanes; g += 4)
		{
			V r = _mm_load_ps(_ramp + g), v = ramp(_from, g);
			const V s = stepOf(_from, _to, g);
			for(int n = 0; n < BlockSize; ++n)
			{
				_mm_store_ps(_out.s[n] + g, _mm_mul_ps(v, r));
				r = _mm_max_ps(_mm_sub_ps(r, dec), zero);
				v = _mm_add_ps(v, s);
			}
			_mm_store_ps(_ramp + g, r);
		}
	}

	BdModern::BdModern(const std::vector<int32_t>& _section7, const float _sampleRate)
	{
		const Rom rom{_section7.data()};
		const auto read = [&rom](float* _dst, const uint32_t _table, const int _n)
		{
			for(int i = 0; i < _n; ++i)
				_dst[i] = q31(rom.entry(_table, i));
		};
		read(m_tables.sweepDepth, SweepDepth, 128);
		read(m_tables.sweepDecay, SweepDecay, 128);
		read(m_tables.clickLevel, ClickLevel, 128);
		read(m_tables.modDecay, ModDecay, 128);
		read(m_tables.ampDecay, AmpDecay, 128);
		read(m_tables.modTarget, ModTarget, 128);
		read(m_tables.feedback, Feedback, 128);
		read(m_tables.levelU0, LevelU0, 128);
		read(m_tables.levelU2, LevelU2, 128);
		read(m_tables.wave, Wave, 257);
		read(m_tables.exp2, Exp2, 2048);
		m_tables.wave[257] = m_tables.wave[256];
		for(int i = 0; i < 258; ++i)
			m_tables.sine[i] = static_cast<float>(std::sin(2 * 3.14159265358979323846 * i / 256));

		// The boot's voice: silent, the drive's gain at 1 recovering
		for(int l = 0; l < Lanes; ++l)
		{
			m_driveStage[l] = 5;
			m_driveX[l] = 1;
			m_driveGain.prev[l] = m_driveGain.level[l] = 1;
			m_driveFrom[l] = m_driveTo[l] = 1;
			m_modStage[l] = 1;
		}
		setSampleRate(_sampleRate);
	}

	void BdModern::setSampleRate(const float _sampleRate)
	{
		m_rate = _sampleRate;
		// The click's step per sample scales with the sample's length; the block-rate envelopes keep the machine's
		// coefficients, stepped on its 48 kHz block clock (control)
		m_clickDec = q31(ClickStepWord) * MachineRate / _sampleRate;

		const float hp = bilinearPole(-q31(HpPoleWord), _sampleRate);
		const float lp = bilinearPole(-q31(LpPoleWord), _sampleRate);
		m_hp = {(1 + hp) / 2, -(1 + hp) / 2, hp};
		m_lp = {(1 - lp) / 2, (1 - lp) / 2, lp};

		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void BdModern::setNote(const int _track, const int _note)
	{
		m_note[_track] = _note;
		convert(_track);
	}

	void BdModern::setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
	{
		if(_index < 0 || _index >= Parameters)
			return;
		m_word[_index][_track] = std::min<int32_t>(_value, 127) << 8 | _fine;
		convert(_track);
	}

	void BdModern::trig(const int _track)
	{
		m_trigQueued[_track] = true;
	}

	void BdModern::gateOff(const int _track)
	{
		m_gateQueued[_track] = true;
	}

	void BdModern::convert(const int _l)
	{
		const auto word =[this, _l](const int _i) { return m_word[_i][_l]; };
		const auto byte = [this, _l](const int _i) { return m_word[_i][_l] >> 8; };
		const Tables& t = m_tables;

		// TUNE: (value - 88 + fine / 256) semitones on the note, the pitch clamped to notes 0-127 (Q16). The machine's
		// pitch map (0x40002596) resolves 1/2048 octave from A4: octaves in Q16 by its own 1/12 (0x0aaaaaab), their
		// fraction's top 11 bits indexing its exp2 table (whose entries are not quite 2^(i / 2048) / 2: 2^((i + 1/4) /
		// 2048) / 2 near the middle). 2^(i / 2048) put the voice 0.1 cent off the machine's, enough to drift a long Hit
		const int64_t p = std::clamp<int64_t>((m_note[_l] - 88) * int64_t(65536) + 256 * word(0), 0, 127 * 65536);
		const int64_t q = ((p - 69 * 65536) * 0x0aaaaaab) >> 31;
		const auto octave = static_cast<int>(q >> 16);
		const float exp2 = t.exp2[(q & 0xffff) >> 5];
		m_baseInc[_l] = PitchScale * std::ldexp(exp2, octave);

		m_g0[_l] = interpolate(t.levelU2, word(1));
		m_g1[_l] = interpolate(t.levelU0, word(2));
		m_clickTarget[_l] = interpolate(t.clickLevel, word(2));
		// The feedback word Q31 F adds F / 32 of a 2^-24 cycle per unit sample: 4 F cycles
		m_fbTarget[_l] = 4 * interpolate(t.feedback, word(2));
		m_sweepDepth[_l] = 32 * t.sweepDepth[byte(3)];
		m_sweepDecayNext[_l] = -t.sweepDecay[byte(3)];	// coefficients per 48 kHz block
		m_modTarget[_l] = interpolate(t.modTarget, word(4));
		m_modDecayNext[_l] = -t.modDecay[byte(4)];
		m_driveNext[_l] = word(5) != 0;
		m_hold[_l] = word(6) != 0;
		m_ampDecay[_l] = -t.ampDecay[byte(7)];
	}

	// 0x400047ea's gain envelope, one 48 kHz block: a first-order filter on a step, falling from 1 toward 0 until 0.5,
	// then recovering toward 1
	float BdModern::driveStep(const int _l)
	{
		const bool falling = m_driveStage[_l] == 1;
		const float pole = -q31(falling ? FallPoleWord : RisePoleWord);
		const float x = falling ? 0.0f : 1.0f;
		float y = (1 - pole) / 2 * (x + m_driveX[_l]) + pole * m_driveGain.level[_l];
		if(y <= 0.5f)
		{
			m_driveStage[_l] = 5;
			y = 0.5f;
		}
		if(!m_drive[_l])
			y = 1;
		m_driveX[_l] = x;
		return y;
	}

	void BdModern::control()
	{
		const float u2Decay = -q31(U2DecayWord);
		const double r = MachineRate / m_rate;	// a host block's length in 48 kHz blocks

		for(int l = 0; l < Lanes; ++l)
		{
			const bool now = m_trigNow[l], pending = m_trigPending[l];

			// The update (0x40006968): what a trig restarts and latches, and each envelope's 48 kHz block 0
			if(now)
			{
				for(auto& p : m_phase)
					p[l] = 0;
				m_last[l] = 0;
				m_hpX[l] = m_hpY[l] = m_lpX[l] = m_lpY[l] = 0;
				m_g0Cur[l] = m_g0[l];
				m_g1Cur[l] = m_g1[l];
				m_clickCur[l] = m_clickTarget[l];
				m_clickRamp[l] = 1;
				m_sweepDecay[l] = m_sweepDecayNext[l];
				m_modDecay[l] = m_modDecayNext[l];
				m_drive[l] = m_driveNext[l];
				m_at[l] = 0;
				m_sweepBlock[l] = m_envBlock[l] = 0;
				m_sweep[l] = 1;

				// Each envelope starts from 1 without a ramp: unit 2's toward 0.75, the modulator's held a block more,
				// the amp's held while HOLD is on, the drive's gain falling from 1
				m_u2.prev[l] = m_u2.level[l] = 1;
				m_mod.prev[l] = m_mod.level[l] = 1;
				m_modStage[l] = 0;
				m_amp.prev[l] = m_amp.level[l] = 1;
				m_ampStage[l] = m_hold[l] ? 2 : 1;
				m_release[l] = false;
				m_driveStage[l] = 1;
				m_driveX[l] = 1;
				m_driveGain.prev[l] = m_driveGain.level[l] = 1;
				m_driveGain.level[l] = driveStep(l);
				m_u2Prev[l] = m_modPrev[l] = m_driveFrom[l] = 1;
				if(!pending)
					m_ampPrev[l] = 1;
			}

			// The units' frequencies: unit 3 at the note, units 0-2 an octave up but clamped at 32 times the note, all
			// under the sweep, which the machine steps once per 48 kHz block (a^k over its block k). Its frequencies
			// are its increments at 48 kHz: the factor keeps 19 fractional bits of the note's 1/32, the increment 24
			// bits of a cycle, both rounded down (finer, a long Hit drifts out of phase with the machine's)
			const auto machine = [this, l](double& _inc3, double& _inc012)
			{
				const double s = 1 + (m_sweepDepth[l] - 1) * m_sweep[l];
				const double base = m_baseInc[l] * 16777216.0;
				_inc3 = std::floor(base * std::floor(s * 16384.0) / 16384.0);
				_inc012 = std::floor(base * std::floor(std::min(2 * s, 32.0) * 16384.0) / 16384.0);
			};

			// Each host sample adds the phase the machine adds over the same time, in 48 kHz samples: its block's
			// increment, or two blocks' weighted where the sample straddles their boundary. At 48 kHz, the machine's
			const double end = m_at[l] + r, step = r / BlockSize;
			const auto host = [r](const double _inc48)
			{
				return static_cast<uint32_t>(std::llround(_inc48 * 256 * r));
			};
			double inc3, inc012;
			machine(inc3, inc012);
			uint32_t whole3 = host(inc3), whole012 = host(inc012);	// a sample within one machine block
			double edge = static_cast<double>(m_sweepBlock[l] + 1);
			for(int n = 0; n < BlockSize; ++n)
			{
				// The samples wholly inside this machine block, those that end before its edge, take its increment;
				// the one that straddles the edge, and the block's last (which ends exactly at end), are weighted
				const int before = static_cast<int>(std::ceil((edge - m_at[l]) / step)) - 1;
				const int whole = std::clamp(before, n, BlockSize - 1);
				for(; n < whole; ++n)
				{
					m_inc3.s[n][l] = whole3;
					m_inc012.s[n][l] = whole012;
				}
				const double from = m_at[l] + n * step, to = n == BlockSize - 1 ? end : m_at[l] + (n + 1) * step;
				double p3 = 0, p012 = 0;	// in 2^-24 cycles
				for(double at = from;;)
				{
					const double until = std::min(to, edge);
					p3 += inc3 * (until - at) * BlockSize;
					p012 += inc012 * (until - at) * BlockSize;
					if(to < edge)
						break;
					++m_sweepBlock[l];
					m_sweep[l] *= m_sweepDecay[l];
					machine(inc3, inc012);
					at = edge;
					edge += 1;
					if(to == at)
						break;
				}
				whole3 = host(inc3);
				whole012 = host(inc012);
				m_inc3.s[n][l] = static_cast<uint32_t>(std::llround(p3 * 256.0));
				m_inc012.s[n][l] = static_cast<uint32_t>(std::llround(p012 * 256.0));
			}
			m_at[l] = end;

			// The envelopes step once per 48 kHz block, as on the machine, up to the block this host block ends in; the
			// host block ramps to where their ramps are at its end. At 48 kHz, the machine's blocks themselves; at
			// another rate, every stage (the trig's block, the modulator's hold, the drive's switch at its floor) lands
			// at the same time in seconds
			while(end > static_cast<double>(m_envBlock[l] + 1))
			{
				m_u2.step(l, u2Decay * m_u2.level[l] + (1 - u2Decay) * 0.75f);

				if(m_modStage[l] == 0)
				{
					m_modStage[l] = 1;
					m_mod.step(l, m_mod.level[l]);
				}
				else
				{
					m_mod.step(l, m_modDecay[l] * m_mod.level[l] + (1 - m_modDecay[l]) * m_modTarget[l]);
				}

				if(m_ampStage[l] == 2)
				{
					if(m_release[l])
					{
						m_ampStage[l] = 1;
						m_release[l] = false;
					}
					m_amp.step(l, m_amp.level[l]);
				}
				else
				{
					m_amp.step(l, m_amp.level[l] * m_ampDecay[l]);
				}

				m_driveGain.step(l, driveStep(l));
				++m_envBlock[l];
			}
			if(m_gateOff[l] && !pending)
				m_release[l] = true;

			const double t = end - static_cast<double>(m_envBlock[l]);
			m_u2Level[l] = m_u2.at(l, t);
			m_modLevel[l] = m_mod.at(l, t);
			m_ampLevel[l] = m_amp.at(l, t);
			m_driveTo[l] = m_driveGain.at(l, t);

			// A pending block fades the modulator and the Track out, and leaves them silent until the trig
			if(pending)
			{
				m_mod.prev[l] = m_mod.level[l] = m_modLevel[l] = 0;
				m_amp.prev[l] = m_amp.level[l] = m_ampLevel[l] = 0;
			}

			m_driveAmount[l] = m_drive[l] ? 2.0f : 1.0f;
			m_drivePost[l] = m_drive[l] ? q31(0x3eb851ec) : q31(0x370a3d71);
		}
	}

	float BdModern::frequency(const int _track, const int _unit) const
	{
		const Increments& inc = _unit == 3 ? m_inc3 : m_inc012;
		uint32_t top = 0;
		for(const auto& s : inc.s)
			top = std::max(top, s[_track]);
		return static_cast<float>(top / 4294967296.0 * m_rate);
	}

	float BdModern::modulatorPeak(const int _track) const
	{
		float peak = 0;
		for(const auto& s : m_mix.s)
			peak = std::max(peak, std::abs(s[_track]));
		return peak;
	}

	void BdModern::render(float* const* _outputs)
	{
		const FlushDenormals ftz;

		for(int l = 0; l < Lanes; ++l)
		{
			m_trigPending[l] = m_trigQueued[l];
			m_gateOff[l] = m_gateQueued[l];
			m_trigQueued[l] = m_gateQueued[l] = false;
		}
		control();

		// Unit 1 (a sine an octave up) modulates unit 2 (the ROM wave), which with unit 0 (a sine an octave up with
		// self-feedback) makes the modulator; the modulator, enveloped, modulates unit 3, the body at the note
		pmSine(m_b1, nullptr, m_phase[1], m_inc012, 0);
		pmWave(m_a2, m_b1, m_phase[2], m_inc012, q31(static_cast<int32_t>(0xffe3d70a)) * 128, m_tables.wave);
		rampedGain(m_a2, m_u2Prev, m_u2Level);
		feedbackSine(m_b0, m_phase[0], m_inc012, m_fb, m_fbTarget, m_last, m_tables.sine);
		mix(m_a2, m_b0, m_mix, m_g0Cur, m_g0, m_g1Cur, m_g1);
		rampedGain(m_mix, m_modPrev, m_modLevel);
		pmSine(m_b3, &m_mix, m_phase[3], m_inc3, q31(0x00ffffff) * 128);
		driveClip(m_b3, m_driveFrom, m_driveTo, m_driveAmount, m_drivePost);
		click(m_click, m_clickRamp, m_clickDec, m_clickCur, m_clickTarget);
		onePole(m_click, m_hp, m_hpX, m_hpY);
		onePole(m_click, m_lp, m_lpX, m_lpY);
		sum(m_click, m_b3, m_b3);
		rampedGain(m_b3, m_ampPrev, m_ampLevel);

		// The ramps' ends are the next block's starts
		std::copy_n(m_u2Level, Lanes, m_u2Prev);
		std::copy_n(m_modLevel, Lanes, m_modPrev);
		std::copy_n(m_ampLevel, Lanes, m_ampPrev);
		std::copy_n(m_driveTo, Lanes, m_driveFrom);
		std::copy_n(m_g0, Lanes, m_g0Cur);
		std::copy_n(m_g1, Lanes, m_g1Cur);
		std::copy_n(m_clickTarget, Lanes, m_clickCur);

		for(int l = 0; l < Lanes; ++l)
		{
			float* out = _outputs[l];
			for(int n = 0; n < BlockSize; ++n)
				out[n] = m_b3.s[n][l];
			m_trigNow[l] = m_trigPending[l];
		}
	}
}
