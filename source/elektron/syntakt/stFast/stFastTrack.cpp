#include "stFastTrack.h"

#include <array>

#include "stFastTables.h"

namespace syntakt::fast
{
	namespace
	{
		using V = __m128;
		using D = __m128d;

		constexpr double Pi = 3.14159265358979323846;
		using simd::load;
		using simd::store;

		D rail(const D _x) { return _mm_max_pd(_mm_min_pd(_x, _mm_set1_pd(1.0)), _mm_set1_pd(-1.0)); }

		// A polynomial of the logarithm of the entry, _c lowest first, fitted to a law no form was found for
		template<size_t N>
		double logCurve(const double (&_c)[N], const int _i)
		{
			const double u = std::log(static_cast<double>(_i + 1)) / std::log(128.0);
			double sum = 0;
			for(size_t k = N; k-- > 0;)
				sum = sum * u + _c[k];
			return std::exp(sum);
		}

		// The drive's gain word over its dry part: small-signal gain 0.15 at 0, as much as a word's square adds to 1
		constexpr float DriveFloor = 0.1535f;

		// Samples of HOLD by its value, at 48 kHz: 2 to 480000. The amp's decay reaches 1/10 in as many (ln 10 time
		// constants, its first entries a sample or two apart)
		constexpr double HoldCurve[] = {0.69310053, 14.869808, -11.569336, 22.023813, -8.9906212, -36.73132, 67.549897,
			-46.7802, 12.01663};
		// A seventh of the hold up to 110, then 1 s to 30 s, the logarithm of its seconds a fit in (value - 110) / 17, within 13 %
		constexpr double AttackSeconds[] = {0.0908654, 1.30812, 6.33092, -4.32922};
		constexpr int AttackLong = 110;

		double attackSeconds(const int _i)
		{
			const double u = (_i - AttackLong) / 17.0;
			return std::exp(AttackSeconds[0] + u * (AttackSeconds[1] + u * (AttackSeconds[2] + u * AttackSeconds[3])));
		}
		// The filter envelope's time constant and attack in blocks by their value
		constexpr double EnvelopeDecay[] = {-5.1204337, 76.482918, -463.84603, 1710.2742, -3821.0166, 5286.2598, -4423.5574,
			2052.8296, -403.52541};
		constexpr double EnvelopeAttack[] = {0, 11.921705, -31.288923, 102.16686, -226.28877, 333.46577, -302.02827, 151.9424,
			-30.274836};
	}

	// ---- drive ----

	TrackDrive::TrackDrive()
	{
		for(int l = 0; l < Lanes; ++l)
		{
			set(l, 0);
			clear(l);
		}
	}

	void TrackDrive::set(const int _lane, const int _word)
	{
		const float u = static_cast<float>(_word) / 32768.0f;
		m_to[_lane] = DriveFloor + (1 - DriveFloor) * u * u;
		const float wet = _word <= 0 ? 0 : std::min(static_cast<float>(_word) / 2048.0f, 1.0f);
		m_dryTo[_lane] = (1 - wet) / 2;
		m_wetTo[_lane] = wet * std::pow(10.0f, (-6 - 4 * static_cast<float>(_word >> 8) / 127) / 20);
	}

	void TrackDrive::process(Block& _buf, const uint16_t _lanes)
	{
		using namespace simd;
		const V sign = _mm_set1_ps(-0.0f);
		for(int l = 0; l < _buf.lanes; l += 4)
		{
			if(!(_lanes >> l & 15))
				continue;
			const V dryStep = stepOf(m_dry, m_dryTo, l), wetStep = stepOf(m_wet, m_wetTo, l), gainStep = stepOf(m_gain, m_to, l);
			const bool clean = _mm_movemask_ps(_mm_cmpeq_ps(_mm_or_ps(ramp(m_wet, l), ramp(m_wetTo, l)), _mm_setzero_ps())) == 15;
			V dry = ramp(m_dry, l), wet = ramp(m_wet, l), gain = ramp(m_gain, l);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V x = _mm_load_ps(&_buf.s[n][l]);
				V y = _mm_mul_ps(x, dry);
				if(!clean)
				{
					V u = _mm_mul_ps(gain, x);
					for(int pass = 0; pass < 3; ++pass)
						u = _mm_sub_ps(_mm_add_ps(u, u), _mm_mul_ps(u, _mm_andnot_ps(sign, u)));
					y = _mm_add_ps(y, _mm_mul_ps(u, wet));
				}
				_mm_store_ps(&_buf.s[n][l], y);
				dry = _mm_add_ps(dry, dryStep);
				wet = _mm_add_ps(wet, wetStep);
				gain = _mm_add_ps(gain, gainStep);
			}
			_mm_store_ps(m_dry + l, ramp(m_dryTo, l));
			_mm_store_ps(m_wet + l, ramp(m_wetTo, l));
			_mm_store_ps(m_gain + l, ramp(m_to, l));
		}
	}

	// ---- BASE/WIDTH ----

	BaseWidth::BaseWidth(const double _rate)
	{
		// exp(-w) at 5 Hz up an equal scale, bent under the top, where it stops at an index of 508. The bend is a fit
		for(int i = 0; i < 1024; ++i)
		{
			const double w = 2 * Pi * 5 * std::exp(0.01769 * i) / _rate;
			m_table[i] = i >= 508 ? 0 : std::exp(-w / std::pow(1 + std::pow(w / 3.44, 2.82), 1 / 2.82));
		}
		for(int l = 0; l < Lanes; ++l)
			set(l, 0, 0x7f00);
	}

	void BaseWidth::set(const int _lane, const int _base, const int _width)
	{
		m_ph[_lane] = m_table[_base >> 6];
		m_pl[_lane] = m_table[(_base + _width) >> 6];
	}

	void BaseWidth::process(Block& _buf, const uint16_t _lanes)
	{
		for(int l = 0; l < _buf.lanes; l += 2)
		{
			if(!(_lanes >> l & 3))
				continue;
			D x[BlockSize];
			load(_buf, l, x);
			const D pl = _mm_load_pd(m_pl + l), ph = _mm_load_pd(m_ph + l);
			const D kl = _mm_sub_pd(_mm_set1_pd(1.0), pl), kh = _mm_mul_pd(_mm_add_pd(_mm_set1_pd(1.0), ph), _mm_set1_pd(0.5));
			D low = _mm_load_pd(m_low + l), high = _mm_load_pd(m_high + l);
			for(int n = 0; n < BlockSize; ++n)
			{
				const D old = low;
				low = rail(_mm_add_pd(_mm_mul_pd(pl, low), _mm_mul_pd(kl, x[n])));
				high = rail(_mm_add_pd(_mm_mul_pd(ph, high), _mm_mul_pd(kh, _mm_sub_pd(low, old))));
				x[n] = high;
			}
			_mm_store_pd(m_low + l, low);
			_mm_store_pd(m_high + l, high);
			store(_buf, l, x);
		}
	}

	// ---- the multimode filter ----

	// The firmware's tables as their formulas. The frequency's scale has its index in 512 steps from 5 Hz to 22 kHz for
	// the resonator's poles and 1023 for the morphs; a pole of damping b sits at the frequency moved up the scale by
	// the logarithm of b (its radius, exp(-w)) or of sqrt(1 - b^2) (its angle), the offsets the DSP adds to its index
	struct Multimode::Tables
	{
		static constexpr int Low = 240;		// the lowest index an offset gives

		std::array<double, 768> cosX, expX, sechX, resonance;	// by index + Low
		std::array<int, 128> angle{};
		std::array<int, 129> damping{};
		std::array<double, 1024> cosW, frequency;
		std::array<double, 129> stage{};
		std::array<double, 128> a0{};
		std::array<double, 64> a1{};
		std::array<double, 5> bandwidth{};
		std::array<int32_t, 1024> log2 = tables::cpVintageLog2();
		std::array<int32_t, 1025> exp2 = tables::cpVintageExp2();

		// sqrt v and 1 / sqrt v as the DSP takes them, a 10-bit mantissa's log2 without interpolation: 1 part in 2000, which
		// is what a sharp resonance hears (0x40001b98)
		struct Root
		{
			double value, inverse;
		};

		Root root(const double _v) const
		{
			const auto v = static_cast<int32_t>(std::min(_v * 2147483648.0, 2147483647.0));
			int z = 0;
			while(z < 31 && !(v >> (30 - z) & 1))
				++z;
			const auto mantissa = static_cast<int32_t>(static_cast<uint32_t>(v) << z);
			const int32_t l = ((-log2[static_cast<size_t>((mantissa >> 20) - 1024)] >> 7) + (z << 24)) >> 1;
			const int32_t inverse = -l;
			const auto entry = [this](const int32_t _l) { return exp2[static_cast<size_t>((0x1000000 - (_l & 0xffffff)) >> 14)]; };
			return {static_cast<double>(entry(l) >> (l >> 24)) / 2147483648.0,
				std::ldexp(static_cast<double>(entry(inverse)) / 2147483648.0, -(inverse >> 24))};
		}

		explicit Tables(const double _rate)
		{
			for(int i = 0; i < static_cast<int>(cosX.size()); ++i)
			{
				const double x = 2 * Pi * 5 * std::pow(4400.0, (i - Low) / 512.0) / _rate;
				cosX[i] = std::cos(x);
				expX[i] = std::exp(-x);
				sechX[i] = 1 / std::cosh(x);
				resonance[i] = 1 - std::pow(std::min(x / Pi, 1.0), 2.5);
			}
			const double steps = 512 / std::log(4400.0);
			for(int r = 0; r < 129; ++r)
			{
				// RESO: a Q of 0.501 to sqrt 500 in equal ratios, b = 1 / 2Q. The offsets round as the firmware's words do
				const double b = 1 / (2 * 0.501 * std::pow(std::sqrt(500.0) / 0.501, std::min(r, 127) / 127.0));
				if(r < 128)
					angle[r] = static_cast<int>(std::floor(steps * 0.5 * std::log(1 - b * b)));
				damping[r] = static_cast<int>(std::lround(steps * std::log(b)));
				stage[r] = std::pow(10.0, -1.2 * (1 - std::min(r, 127) / 127.0));
			}
			for(int r = 0; r < 128; ++r)
			{
				const double u = (r - 63.5) / 63.5;
				a0[r] = 1 / (1 + u * u * u * u);
			}
			for(int r = 0; r < 64; ++r)
				a1[r] = 0.995 * std::pow(10.0, -1.2 * r / 127);
			for(int i = 0; i < 1024; ++i)
			{
				frequency[i] = 5 * std::pow(4400.0, i / 1023.0) / _rate;
				cosW[i] = std::cos(2 * Pi * frequency[i]);
			}
			// The morphs' bandwidth by M, 0x00 to 0x7f, a polynomial of its 1024-step index
			int t = 0;
			for(const int m : {0, 0x17, 0x2b, 0x53, 0x7f})
			{
				const double u = (m << 3) / 1024.0, u2 = u * u;
				bandwidth[t++] = 5 / (4 * std::sqrt(2.0)) / (1 + u * (14.864517 + u * 14.879036) + u2 * u2 * (19.877432 + u2 * 39.832789));
			}
		}
	};

	Multimode::Multimode(const double _rate)
		: m_t([&]() -> const Tables&
		{
			if(_rate != MachineRate && _rate != 44100)
				std::abort();
			static const Tables at48(MachineRate), at44(44100);
			return _rate == MachineRate ? at48 : at44;
		}())
	{
		for(int l = 0; l < Lanes; ++l)
		{
			m_type[l] = m_next[l] = 7;
			m_mix[0][l] = 0.25;
		}
	}

	void Multimode::set(const int _lane, const int _type, const int32_t _cutoff, const int32_t _reso, const bool _reset)
	{
		m_next[_lane] = _type;
		m_cutoff[_lane] = _cutoff;
		m_reso[_lane] = _reso;
		m_reset[_lane] = _reset;
	}

	void Multimode::clear(const int _lane)
	{
		m_s1[_lane] = m_s2[_lane] = m_z[_lane] = 0;
		m_trig[_lane] = m_trigSeen[_lane] = false;
		m_type[_lane] = std::clamp(m_next[_lane], 0, 7);
		mixOf(m_type[_lane], true, _lane);
		for(int i = 0; i < 3; ++i)
			m_mix[i][_lane] = m_mixTo[i][_lane];
		m_stage[_lane] = m_t.stage[std::clamp(m_reso[_lane], 0, 0x7f000000) >> 24];
		m_snap[_lane] = true;
	}

	void Multimode::mixOf(const int _type, const bool _keep, const int _lane)
	{
		m_mixTo[0][_lane] = _keep && (_type == 7 || _type == 1) ? 0.25 : 0;
		m_mixTo[1][_lane] = _keep && (_type == 0 || _type == 1) ? (_type == 0 ? 1 : -1) : 0;
		m_mixTo[2][_lane] = _keep && isMorph(_type) ? 1 : 0;
	}

	void Multimode::pathA(const int _f, const int _r, double* _c) const
	{
		const auto& t = m_t;
		const int r = _r >> 24, index = _f >> 22;
		const int i1 = index + t.angle[r];
		const double d0 = _r * t.resonance[i1 + Tables::Low];
		const int j = static_cast<int>(d0 / (1 << 24));
		const double f = d0 / (1 << 24) - j;
		const int d2 = static_cast<int>(std::floor(2.0 * t.damping[j] + f * (t.damping[j + 1] - t.damping[j])));
		const int i2 = (d2 >> 1) + index;

		const double a = t.cosX[i1 + Tables::Low] / 2, b = t.expX[i2 + Tables::Low];
		const double q = b * b;
		_c[C0] = (q / 4 - (a * b - 0.25)) / 2;
		const double c1 = 2 * a * t.sechX[i2 + Tables::Low];
		const auto c4 = t.root(1 - c1 * c1);
		_c[C1] = c1;
		_c[C4] = c4.value;
		_c[C3] = c1 * c4.inverse * _c[C0];
		_c[C2] = q * c4.value;
		_c[C5] = c1 * q;
	}

	void Multimode::pathB(const int _f, const int _r, const int _m, double* _c) const
	{
		const auto& t = m_t;
		const int r = _r >> 24, i = _f >> 21;
		const double c1 = t.cosW[i];
		const double k1 = _r < 0x40000000 ? t.a1[r] : 0.25;
		const double e = std::min(t.frequency[i] * t.bandwidth[_m] * t.a0[r] * k1, std::sqrt(0.5) / 64);
		const double g = std::tan(Pi / 4 * (1 - e * 65536 / 511.5));
		const double h = t.root(1 - g * g).value;
		const auto c4 = t.root(1 - c1 * c1);
		_c[C1] = c1;
		_c[C4] = c4.value;
		_c[C7] = h / 2;
		_c[C0] = h * c4.value / 512;
		_c[C3] = c1 * c4.inverse * _c[C0];
		_c[C6] = g / 256;
		_c[C2] = c4.value * g;
		_c[C5] = c1 * g;
	}

	void Multimode::prepare(const int _l)
	{
		const int type = m_type[_l];

		// Type 7, staying: the resonator goes unheard, and the type that next takes it snaps first
		m_idle[_l] = type == 7 && m_next[_l] == 7 && !m_snap[_l] && m_mix[0][_l] == 0.25 && m_mix[1][_l] == 0
			&& m_mix[2][_l] == 0;
		if(m_idle[_l])
		{
			m_trig[_l] = m_trigSeen[_l] = false;
			return;
		}

		const bool group = isMorph(type);
		const int r = std::clamp(m_reso[_l], 0, 0x7f000000);

		double c[Coefficients];
		for(int i = 0; i < Coefficients; ++i)
			c[i] = m_to[i][_l];
		if(group)
			pathB(m_cutoff[_l], r, type - 2, c);
		else
			pathA(m_cutoff[_l], r, c);

		const bool snap = (m_trigSeen[_l] && m_reset[_l]) || m_snap[_l];
		for(int i = 0; i < Coefficients; ++i)
		{
			m_to[i][_l] = c[i];
			if(snap)
				m_cur[i][_l] = c[i];
			m_step[i][_l] = (c[i] - m_cur[i][_l]) / BlockSize;
		}
		if(snap)
		{
			m_s1[_l] = m_s2[_l] = 0;
			m_snap[_l] = false;
		}

		const double gain = m_t.stage[r >> 24];
		m_stageStep[_l] = ((group ? gain : m_t.stage[64]) - m_stage[_l]) / BlockSize;
		m_stageNext[_l] = gain;

		const int next = std::clamp(m_next[_l], 0, 7);
		bool keep = true;
		if(next != type && (!group || !isMorph(next)))
		{
			m_snap[_l] = true;
			keep = false;
		}
		if(m_trigSeen[_l])
		{
			m_snap[_l] = m_trigSeen[_l] = false;
			keep = true;
		}
		if(m_trig[_l])
		{
			m_snap[_l] = m_trig[_l] = false;
			m_trigSeen[_l] = true;
			keep = true;
		}
		m_type[_l] = next;

		mixOf(type, keep, _l);
		for(int i = 0; i < 3; ++i)
			m_mixStep[i][_l] = (m_mixTo[i][_l] - m_mix[i][_l]) / BlockSize;
	}

	void Multimode::render(Block& _buf, const int _l)
	{
		D x[BlockSize];
		load(_buf, _l, x);

		D c[Coefficients], step[Coefficients];
		for(int i = 0; i < Coefficients; ++i)
		{
			c[i] = _mm_load_pd(m_cur[i] + _l);
			step[i] = _mm_load_pd(m_step[i] + _l);
		}
		D mix[3], mixStep[3];
		for(int i = 0; i < 3; ++i)
		{
			mix[i] = _mm_load_pd(m_mix[i] + _l);
			mixStep[i] = _mm_load_pd(m_mixStep[i] + _l);
		}
		D s1 = _mm_load_pd(m_s1 + _l), s2 = _mm_load_pd(m_s2 + _l), z = _mm_load_pd(m_z + _l);
		D stage = _mm_load_pd(m_stage + _l);
		const D stageStep = _mm_load_pd(m_stageStep + _l);
		const D four = _mm_set1_pd(4.0);

		for(int n = 0; n < BlockSize; ++n)
		{
			const D input = x[n], fx = _mm_mul_pd(four, input);
			const D a = rail(_mm_sub_pd(_mm_add_pd(_mm_mul_pd(c[C0], fx), _mm_mul_pd(c[C1], s1)), _mm_mul_pd(c[C2], s2)));
			const D b = rail(_mm_add_pd(_mm_sub_pd(_mm_mul_pd(c[C4], s1), _mm_mul_pd(c[C3], fx)), _mm_mul_pd(c[C5], s2)));
			s1 = a;
			s2 = b;

			const D y = _mm_add_pd(_mm_mul_pd(c[C7], z), _mm_mul_pd(c[C6], input));
			z = b;
			const D xs = _mm_mul_pd(input, _mm_set1_pd(1.0 / 256));
			const D v = _mm_add_pd(_mm_mul_pd(stage, _mm_sub_pd(xs, y)), _mm_mul_pd(_mm_add_pd(y, xs), _mm_set1_pd(0.25)));
			const D shaped = rail(_mm_mul_pd(v, _mm_set1_pd(128.0)));

			x[n] = rail(_mm_add_pd(_mm_add_pd(_mm_mul_pd(mix[0], input), _mm_mul_pd(mix[1], _mm_mul_pd(a, _mm_set1_pd(0.5)))),
				_mm_mul_pd(mix[2], shaped)));

			for(int i = 0; i < Coefficients; ++i)
				c[i] = _mm_add_pd(c[i], step[i]);
			for(int i = 0; i < 3; ++i)
				mix[i] = _mm_add_pd(mix[i], mixStep[i]);
			stage = _mm_add_pd(stage, stageStep);
		}

		for(int l = _l; l < _l + 2; ++l)
		{
			for(int i = 0; i < Coefficients; ++i)
				m_cur[i][l] = m_to[i][l];
			m_stage[l] = m_stageNext[l];
			for(int i = 0; i < 3; ++i)
				m_mix[i][l] = m_mixTo[i][l];
		}
		_mm_store_pd(m_s1 + _l, s1);
		_mm_store_pd(m_s2 + _l, s2);
		_mm_store_pd(m_z + _l, z);
		store(_buf, _l, x);
	}

	void Multimode::process(Block& _buf, const uint16_t _lanes)
	{
		for(int l = 0; l < _buf.lanes; l += 2)
		{
			if(!(_lanes >> l & 3))
				continue;
			prepare(l);
			prepare(l + 1);

			if(m_idle[l] && m_idle[l + 1])
			{
				for(int n = 0; n < BlockSize; ++n)
				{
					_buf.s[n][l] *= 0.25f;
					_buf.s[n][l + 1] *= 0.25f;
				}
				continue;
			}
			render(_buf, l);
		}
	}

	// ---- the amp's envelope ----

	TrackAmp::TrackAmp(const double _rate)
	{
		const double scale = _rate / MachineRate;
		for(int i = 0; i < 128; ++i)
		{
			const double hold = logCurve(HoldCurve, i) * scale;
			m_hold[i] = static_cast<int>(std::lround(hold));
			m_decay[i] = i == 127 ? 1.0f : static_cast<float>(std::exp(-std::log(10.0) / hold));
			const double attack = i == 0 ? 1 : i < AttackLong ? hold / 7 : attackSeconds(i) * _rate;
			m_attack[i] = static_cast<float>(1 / attack);
		}
	}

	void TrackAmp::set(const int _lane, const int _attack, const int _hold, const int _decay, const int _sustain,
		const int _release, const bool _adsr)
	{
		m_a[_lane] = _attack;
		m_h[_lane] = _hold;
		m_d[_lane] = _decay;
		m_sustain[_lane] = _adsr ? static_cast<float>(_sustain) / 128 : 0;
		m_r[_lane] = _release;
		m_adsr[_lane] = _adsr;
	}

	void TrackAmp::clear(const int _lane)
	{
		m_stage[_lane] = m_holdCount[_lane] = 0;
		m_level[_lane] = 0;
		m_trig[_lane] = m_gateOff[_lane] = false;
	}

	void TrackAmp::lane(Block& _buf, const int _l)
	{
		int& stage = m_stage[_l];
		float& level = m_level[_l];
		const bool trig = m_trig[_l], gateOff = m_gateOff[_l];
		m_trig[_l] = m_gateOff[_l] = false;

		if(trig)
			stage = Fade;

		const auto segment = [&](int _n, const float _a, const float _target)
		{
			float g = level;
			for(; _n < BlockSize; ++_n)
			{
				_buf.s[_n][_l] *= g;
				g = g * _a + _target * (1 - _a);
			}
			level = g;
		};

		for(int n = 0; n < BlockSize;)
		{
			if(stage == Attack)
			{
				if(level >= 1)
				{
					m_holdCount[_l] = 0;
					stage = m_adsr[_l] ? Decay : Hold;
					continue;
				}
				const float inc = m_attack[m_a[_l]];
				while(n < BlockSize)
				{
					_buf.s[n++][_l] *= level;
					level += inc;
					if(level > 1)
					{
						level = 1;
						break;
					}
				}
			}
			else if(stage == Hold)
			{
				const int length = m_hold[m_h[_l]];
				if(length > m_holdCount[_l])
				{
					const int k = std::min(length - m_holdCount[_l], BlockSize - n);
					m_holdCount[_l] += k;
					n += k;
				}
				else if(m_h[_l] == 127)
				{
					n = BlockSize;
				}
				else
				{
					stage = Decay;
					level = 1;
				}
			}
			else if(stage == Decay)
			{
				segment(n, m_decay[m_d[_l]], m_sustain[_l]);
				n = BlockSize;
			}
			else if(stage == Fade)
			{
				// The machine's linear fade of the old Hit, on its first sample read once, kept
				const float x = _buf.s[n][_l], step = level / BlockSize;
				for(; n < BlockSize; ++n)
				{
					_buf.s[n][_l] = x * level;
					level -= step;
				}
			}
			else
			{
				segment(n, m_decay[m_r[_l]], 0);
				n = BlockSize;
			}
		}

		if(trig)
		{
			level = m_a[_l] == 0 ? 1 : m_attack[m_a[_l]] / 2;
			stage = Attack;
		}
		else if(gateOff && m_adsr[_l])
		{
			stage = Release;
		}
		if(gateOff && !m_adsr[_l] && m_h[_l] == 127)
		{
			if(stage == Hold)
				level = 1;
			stage = Decay;
		}
	}

	void TrackAmp::process(Block& _buf, const uint16_t _lanes)
	{
		for(int l = 0; l < _buf.lanes; ++l)
			if(_lanes >> l & 1)
				lane(_buf, l);
	}

	// ---- the filter's envelope ----

	FilterEnvelope::FilterEnvelope(const double _rate)
	{
		const double scale = _rate / MachineRate;
		for(int i = 0; i < 128; ++i)
		{
			m_decay[i] = i == 0 ? 0 : i == 127 ? 1 : std::exp(-1 / (logCurve(EnvelopeDecay, i) * scale));
			m_attack[i] = 1 / (logCurve(EnvelopeAttack, i) * scale);
			// A block a step, then a cubic, 3 s at 127
			const double d = i;
			const double cubic = d * (d * (d * 0.0021948812 + 0.00050997472) - 0.039009488) + 0.3582401;
			m_delay[i] = (i <= 21 ? d : cubic) * scale;
		}
	}

	double FilterEnvelope::step(const int _l, const Setting& _s, const bool _trig, const bool _gateOff)
	{
		double& value = m_value[_l];
		if(_trig)
		{
			m_count[_l] = 0;
			m_stage[_l] = _s.delay == 0 ? Attack : Delay;
			if(_s.reset)
				value = 0;
			return value;
		}
		if(_gateOff)
			m_stage[_l] = Off;
		switch(m_stage[_l])
		{
		case Delay:
			m_count[_l] += 1;
			if(m_count[_l] >= m_delay[_s.delay])
				m_stage[_l] = Attack;
			break;
		case Attack:
			value += m_attack[_s.attack];
			if(value >= 1)
			{
				value = 1;
				m_stage[_l] = Decay;
			}
			break;
		case Decay:
		{
			const double c = m_decay[_s.decay];
			value = c * value + (1 - c) * _s.sustain / 127.0;
			break;
		}
		default:
			value *= m_decay[_s.release];
			break;
		}
		return value;
	}

	int32_t filterCutoff(const int _freq, const int _depth, const int _keyTrack, const int _note, const double _envelope)
	{
		const double key = 2 * 0.564 * (_note - 48) / 128.0 * _keyTrack / 128.0;
		const double r = _freq / 32768.0 + key + _envelope * (_depth - 0x4000) / 16384.0;
		return static_cast<int32_t>(std::clamp(r, 0.0, 127.0 / 128) * 2147483648.0);
	}
}
