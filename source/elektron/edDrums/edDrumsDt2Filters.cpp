#include "edDrumsDt2Filters.h"
#include "edDrumsDt2Stages.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <emmintrin.h>

// Built with /fp:precise (CMakeLists.txt): the setters' float operations in their order, as the DSP runs them; under
// /fp:fast EQUALIZER's and LEGACY's sqrt(1 - c^2) at low cutoffs would take the compiler's rounding for the DSP's
namespace elektron::drums
{
	namespace
	{
		using V = __m128;

		constexpr double Pi = 3.14159265358979323846;

		uint32_t bitsOf(const float _x)
		{
			uint32_t b;
			std::memcpy(&b, &_x, 4);
			return b;
		}

		float fromBits(const uint32_t _b)
		{
			float x;
			std::memcpy(&x, &_b, 4);
			return x;
		}

		// A firmware table's word: its formula rounded to _decimals, then to a float (FILTERS.md)
		float rounded(const double _x, const int _decimals = 7)
		{
			const double scale = std::pow(10.0, _decimals);
			return static_cast<float>(std::nearbyint(_x * scale) / scale);
		}

		// The cutoff scale, 84.5 entries an octave from 5 Hz to 22 kHz, which stops at 22 kHz x _rate / 48 kHz
		double cutoffHz(const int _index, const double _rate)
		{
			return std::min(5 * std::pow(4400.0, _index / 1023.0), 22000 * _rate / 48000);
		}

		float clamp(const float _x, const float _lo, const float _hi) { return std::min(std::max(_x, _lo), _hi); }

		// The setters' interpolation at _a, already inside the table: i = trunc(a), its next capped
		template<size_t N>
		float interp(const std::array<float, N>& _table, const float _a)
		{
			const int i = static_cast<int>(_a);
			const int j = std::min(i + 1, static_cast<int>(N) - 1);
			const float f = _a - static_cast<float>(i);
			const float a = _table[static_cast<size_t>(i)];
			return a + f * (_table[static_cast<size_t>(j)] - a);
		}

		V clampV(const V _x, const V _lo, const V _hi) { return _mm_min_ps(_mm_max_ps(_x, _lo), _hi); }

		// The lanes of _lanes among the 4 from _l, as a mask
		V maskOf(const uint16_t _lanes, const int _l)
		{
			const auto on = [&](const int _k) { return (_lanes >> (_l + _k) & 1) ? -1 : 0; };
			return _mm_castsi128_ps(_mm_set_epi32(on(3), on(2), on(1), on(0)));
		}

		V select(const V _mask, const V _a, const V _b)
		{
			return _mm_or_ps(_mm_and_ps(_mask, _a), _mm_andnot_ps(_mask, _b));
		}

		// A group of 4 lanes' words: where the block starts and the step per sample
		template<int Words>
		struct Ramps
		{
			V now[Words], step[Words];

			Ramps(const float (&_now)[Words][Lanes], const float (&_to)[Words][Lanes], const int _l)
			{
				const V inv = _mm_set1_ps(1.0f / BlockSize);
				for(int w = 0; w < Words; ++w)
				{
					now[w] = _mm_load_ps(_now[w] + _l);
					step[w] = _mm_mul_ps(_mm_sub_ps(_mm_load_ps(_to[w] + _l), now[w]), inv);
				}
			}

			// Word _w at sample _n: old + n (new - old) / 32, as the DSP computes it
			V at(const int _w, const V _n) const { return _mm_add_ps(now[_w], _mm_mul_ps(step[_w], _n)); }
		};

		// ---- a 48 kHz filter carried to another rate ----

		// A real pole at the same frequency (0 or Nyquist) and decay per second
		double carriedPole(const double _p, const double _ratio)
		{
			return _p < 0 ? -std::pow(-_p, _ratio) : std::pow(_p, _ratio);
		}

		// The poles of 1 + a1 z^-1 + a2 z^-2 carried; a pair's angle stops at Nyquist
		void carryPoles(const double _a1, const double _a2, const double _ratio, double& _c1, double& _c2)
		{
			const double disc = _a1 * _a1 - 4 * _a2;
			if(disc < 0)
			{
				const double rho = std::sqrt(_a2), theta = std::acos(std::clamp(-_a1 / (2 * rho), -1.0, 1.0));
				const double r = std::pow(rho, _ratio), t = std::min(theta * _ratio, Pi);
				_c1 = -2 * r * std::cos(t);
				_c2 = r * r;
				return;
			}
			const double p = carriedPole((-_a1 + std::sqrt(disc)) / 2, _ratio);
			const double q = carriedPole((-_a1 - std::sqrt(disc)) / 2, _ratio);
			_c1 = -(p + q);
			_c2 = p * q;
		}

		// b0 + b1 z^-1 + b2 z^-2 as its power's three coefficients (dt2::Rerate)
		using Power = std::array<double, 3>;

		Power powerOf(const double _b0, const double _b1, const double _b2)
		{
			return {(_b0 + _b1 + _b2) * (_b0 + _b1 + _b2), (_b0 - _b1 + _b2) * (_b0 - _b1 + _b2), -4 * _b0 * _b2};
		}

		double at(const Power& _p, const std::array<double, 3>& _basis)
		{
			return _p[0] * _basis[0] + _p[1] * _basis[1] + _p[2] * _basis[2];
		}

		// At each of the fit's frequencies, the power a numerator over _below (at the rate) needs to give the 48 kHz
		// filter _above / _over's
		std::array<double, dt2::Rerate::Points> targetOf(const dt2::Rerate& _r, const Power& _above, const Power& _over,
			const Power& _below)
		{
			std::array<double, dt2::Rerate::Points> p;
			for(size_t i = 0; i < dt2::Rerate::Points; ++i)
				p[i] = at(_above, _r.at48[i]) / at(_over, _r.at48[i]) * at(_below, _r.at[i]);
			return p;
		}

		// The numerator whose power meets _target best, its own at DC: B0 that, B1 and (with _second) B2 by least
		// squares, relative (the stop band counts as the pass band, down to -100 dB of the loudest), then factored as a
		// minimum-phase numerator (M. Vicanek, "Matched second order digital filters", 2016). Its phase is the poles'
		// own: a complex fit meets neither where the two rates' phases part, near the top
		std::array<double, 3> numeratorFor(const dt2::Rerate& _r,
			const std::array<double, dt2::Rerate::Points>& _target, const bool _second)
		{
			const double atDc = _target[0], loudest = *std::max_element(_target.begin(), _target.end());
			double a11 = 0, a12 = 0, a22 = 0, r1 = 0, r2 = 0;
			for(size_t i = 1; i < dt2::Rerate::Points; ++i)
			{
				const auto& f = _r.at[i];
				const double p = _target[i], rest = p - atDc * f[0];
				const double weight = 1 / std::max(p * p, 1e-20 * loudest * loudest);
				a11 += weight * f[1] * f[1];
				a12 += weight * f[1] * f[2];
				a22 += weight * f[2] * f[2];
				r1 += weight * f[1] * rest;
				r2 += weight * f[2] * rest;
			}
			double b1 = r1 / a11, b2 = 0;
			if(_second)
			{
				const double d = a11 * a22 - a12 * a12;
				b1 = (r1 * a22 - r2 * a12) / d;
				b2 = (a11 * r2 - a12 * r1) / d;
			}
			b1 = std::max(b1, 0.0);
			const double w = (std::sqrt(atDc) + std::sqrt(b1)) / 2;
			const double n0 = (w + std::sqrt(std::max(w * w + b2, 0.0))) / 2;
			return {n0, (std::sqrt(atDc) - std::sqrt(b1)) / 2, n0 != 0 ? -b2 / (4 * n0) : 0};
		}

		// A 48 kHz biquad, (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2), as Dt2Multimode's words G, GK, A1 and
		// its three weights at another rate: the SVF of the carried poles, its numerator numeratorFor's, as weights of
		// its low-, band- and high-pass
		template<int Words>
		void carry(const double _b0, const double _b1, const double _b2, const double _a1, const double _a2,
			const dt2::Rerate& _r, float (&_to)[Words][Lanes], const int _l)
		{
			double c1, c2;
			carryPoles(_a1, _a2, _r.ratio, c1, c2);
			// The SVF's denominator is S (1 + c1 z^-1 + c2 z^-2): g^2 = D(1) / D(-1)
			const double g2 = (1 + c1 + c2) / (1 - c1 + c2), g = std::sqrt(g2), s = 2 * (1 + g2) / (1 + c2);
			const double k = (s - 1 - g2) / g;
			const auto n = numeratorFor(_r, targetOf(_r, powerOf(_b0, _b1, _b2), powerOf(1, _a1, _a2),
				powerOf(s, s * c1, s * c2)), true);
			// n = wl g^2 (1, 2, 1) + wb g (1, 0, -1) + wh (1, -2, 1)
			_to[0][_l] = static_cast<float>(g);
			_to[1][_l] = static_cast<float>(g + k);
			_to[2][_l] = static_cast<float>(1 / s);
			_to[3][_l] = static_cast<float>((n[0] + n[1] + n[2]) / (4 * g2));
			_to[4][_l] = static_cast<float>((n[0] - n[2]) / (2 * g));
			_to[5][_l] = static_cast<float>((n[0] - n[1] + n[2]) / 4);
		}

		// Dt2Multimode's SVF on _lanes, its words 0-5 G, GK, A1 and the weights; with _mix, words 6 and 7 mix the input
		// and the output as Dt2Legacy's do
		template<int Words>
		void svf(Block& _buf, const uint16_t _lanes, const float (&_now)[Words][Lanes],
			const float (&_to)[Words][Lanes], float* _s1, float* _s2, const bool _mix)
		{
			for(int l = 0; l < _buf.lanes; l += 4)
			{
				if(!(_lanes >> l & 15))
					continue;
				const V mask = maskOf(_lanes, l);
				const Ramps<Words> r(_now, _to, l);
				// The cutoff and resonance at the block's start, unramped
				const V g = _mm_load_ps(_to[0] + l), gk = _mm_load_ps(_to[1] + l), a1 = _mm_load_ps(_to[2] + l);
				V s1 = _mm_load_ps(_s1 + l), s2 = _mm_load_ps(_s2 + l);
				for(int n = 0; n < BlockSize; ++n)
				{
					const V x = _mm_load_ps(&_buf.s[n][l]), nv = _mm_set1_ps(static_cast<float>(n));
					const V hp = _mm_mul_ps(_mm_sub_ps(_mm_sub_ps(x, _mm_mul_ps(gk, s1)), s2), a1);
					const V bp = _mm_add_ps(_mm_mul_ps(g, hp), s1);
					const V lp = _mm_add_ps(_mm_mul_ps(g, bp), s2);
					s1 = _mm_add_ps(_mm_mul_ps(g, hp), bp);
					s2 = _mm_add_ps(_mm_mul_ps(g, bp), lp);
					V y = _mm_add_ps(_mm_add_ps(_mm_mul_ps(r.at(3, nv), lp), _mm_mul_ps(r.at(4, nv), bp)),
						_mm_mul_ps(r.at(5, nv), hp));
					if(_mix)
						y = _mm_add_ps(_mm_mul_ps(r.at(Words - 2, nv), x), _mm_mul_ps(r.at(Words - 1, nv), y));
					_mm_store_ps(&_buf.s[n][l], select(mask, y, x));
				}
				_mm_store_ps(_s1 + l, s1);
				_mm_store_ps(_s2 + l, s2);
			}
		}
	}

	namespace dt2
	{
		Rerate::Rerate(const double _rate) : ratio(48000 / _rate)
		{
			// DC, octaves from 20 Hz to 10 kHz, then the top every 2 kHz to 18 kHz: above, the rate's Nyquist pulls the
			// fit
			std::array<double, Points> hz{0};
			for(int i = 0; i < 10; ++i)
				hz[static_cast<size_t>(i + 1)] = 20 * std::exp2(i);
			for(int i = 0; i < 4; ++i)
				hz[static_cast<size_t>(i + 11)] = 12000 + 2000 * i;
			const auto basis = [](const double _w) -> std::array<double, 3>
			{
				const double c = std::cos(_w);
				return {(1 + c) / 2, (1 - c) / 2, 1 - c * c};
			};
			for(size_t i = 0; i < Points; ++i)
			{
				at48[i] = basis(2 * Pi * hz[i] / 48000);
				at[i] = basis(2 * Pi * hz[i] / _rate);
			}
		}
	}

	namespace dt2
	{
		float div(const float _a, const float _d)
		{
			const float s = fromBits(bitsOf(static_cast<float>(1.0 / static_cast<double>(_d))) & 0xffff8000u);
			float q = s * _a;
			float e = s * _d;
			float t = 2.0f - e;
			e = t * e;
			q = t * q;
			t = 2.0f - e;
			e = t * e;
			q = t * q;
			t = 2.0f - e;
			e = t * e;
			q = t * q;
			t = 2.0f - e;
			e = t - e;
			q = t * q;
			return e * q + q;
		}

		float sqrt(const float _x)
		{
			if(!(_x > 0))
				return 0;
			// (a + b) / 2 rounded once
			const auto average = [](const float _a, const float _b)
			{
				return static_cast<float>((static_cast<double>(_a) + static_cast<double>(_b)) / 2);
			};
			const float m = -_x;
			float y = fromBits(bitsOf(static_cast<float>(1.0 / std::sqrt(static_cast<double>(_x)))) & 0xfff80000u);
			float e = y * m;
			e = y * e;
			y = y * average(3, e);
			e = y * m;
			const float p = y * _x;
			e = y * e;
			return p * average(3, e);
		}

		float frequency(const int _word, const int _keyTrack, const int _note, const bool _comb)
		{
			const float knob = _comb ? knob127(_word) : knob128(_word);
			if(!_keyTrack)
				return knob;
			// Measured at kt 100, notes 36 and 84 (FILTERS.md): one semitone of cutoff per semitone within 0.07 %
			return knob + static_cast<float>(_keyTrack / 100.0 * (_note - 48) * 0.0826 / 12);
		}

		float knob127(const int _word) { return dt2Knob(_word); }
		float knob128(const int _word) { return static_cast<float>(_word) / 16384.0f; }
	}

	// ---- MULTI-MODE ----

	Dt2Multimode::Dt2Multimode(const double _rate) : m_rerate(_rate)
	{
		for(int i = 0; i < 1024; ++i)
			m_cut[static_cast<size_t>(i)] = rounded(std::tan(Pi * cutoffHz(i, 48000) / 48000));
		for(int b = 0; b < 128; ++b)
		{
			const double r = b / 127.0;
			m_reso[static_cast<size_t>(b)] = rounded(1 / (2 * (0.5 + 20 * r * r)));
			// Equal power, which the table exceeds by up to 3e-4: the cubic is fitted (4.5e-5)
			const double u = (b < 64 ? b : 127 - b) / 63.5;
			m_morph[static_cast<size_t>(b)] = static_cast<float>((1 + u * u * (0.00149719 - 0.00124476 * u))
				/ std::sqrt(u * u + (1 - u) * (1 - u)));
		}
	}

	void Dt2Multimode::setter(const int _l)
	{
		const float a = clamp(m_frequency[_l] * 1024.0f, 0, 1023), b = clamp(m_p70[_l] * 128.0f, 0, 127);
		const float g = interp(m_cut, a), k = 2.0f * interp(m_reso, b);
		const float p = m_p68[_l], c = interp(m_morph, clamp(p * 127.0f, 0, 127)), p2 = 2.0f * p;
		const float wh = clamp(p2 - 1.0f, 0, 1), wl = clamp(1.0f - p2, 0, 1), wb = clamp(p2, 0, 1) - wh;
		if(m_rerate.carries())
		{
			// The 48 kHz SVF as a biquad: wl g^2 (1 + z^-1)^2 + wb g (1 - z^-2) + wh (1 - z^-1)^2 over its denominator
			const double gd = g, kd = k, lo = wl * c, band = wb * c, hi = wh * c, d0 = 1 + gd * kd + gd * gd;
			carry((lo * gd * gd + band * gd + hi) / d0, (2 * lo * gd * gd - 2 * hi) / d0,
				(lo * gd * gd - band * gd + hi) / d0, (2 * gd * gd - 2) / d0, (1 - gd * kd + gd * gd) / d0, m_rerate, m_to,
				_l);
			return;
		}
		m_to[G][_l] = g;
		m_to[GK][_l] = g + k;
		m_to[A1][_l] = dt2::div(1.0f, (g * g + g * k) + 1.0f);
		m_to[Wl][_l] = wl * c;
		m_to[Wb][_l] = wb * c;
		m_to[Wh][_l] = wh * c;
	}

	void Dt2Multimode::clear(const int _lane)
	{
		m_s1[_lane] = m_s2[_lane] = 0;
		cleared(_lane);
	}

	void Dt2Multimode::process(Block& _buf, const uint16_t _lanes)
	{
		prepare(_lanes, _buf.lanes, [&](const int _l) { clear(_l); }, [&](const int _l) { setter(_l); });
		svf(_buf, _lanes, m_now, m_to, m_s1, m_s2, false);
		settle(_lanes);
	}

	// ---- EQUALIZER ----

	namespace
	{
		// log W, the width table, as a Chebyshev series in 2 i / 1023 - 1 (fitted to the DT2's, within 8.3e-6
		// relative: vectors-dt2/FILTERS.md), lowest degree first
		constexpr double WidthSeries[]{-2.6422605509969634, -2.058380691285399, 0.17991208041812068,
			-0.15653881208643447, 0.07042410940517776, -0.025858731504151772, 0.013102029167178444,
			-0.007129869905941949, 0.003564320382516134, -0.0018147802449787814, 0.0009698982337683527,
			-0.0005202365657703003, 0.0002785074837131142, -0.00015055864656011354, 8.213726068110766e-05,
			-4.4730084736686174e-05, 2.4638098013975923e-05, -1.316147169336329e-05, 7.3590431048169e-06,
			-3.533112574275795e-06, 2.013414380562534e-06};

		double chebyshev(const double _t)
		{
			// Clenshaw, as numpy's chebval
			constexpr int N = static_cast<int>(std::size(WidthSeries));
			double c0 = WidthSeries[N - 2], c1 = WidthSeries[N - 1];
			for(int i = 3; i <= N; ++i)
			{
				const double t = c0;
				c0 = WidthSeries[N - i] - c1;
				c1 = t + c1 * 2 * _t;
			}
			return c0 + c1 * _t;
		}
	}

	Dt2Equalizer::Dt2Equalizer(const double _rate)
	{
		for(int i = 0; i < 1024; ++i)
		{
			const auto k = static_cast<size_t>(i);
			const double f = cutoffHz(i, _rate) / _rate;
			m_f[k] = rounded(f);
			m_cos[k] = rounded(std::cos(2 * Pi * f));
			m_width[k] = rounded(std::exp(chebyshev(2.0 * i / 1023 - 1)));
			m_radius[k] = rounded(std::tan(Pi / 4 - Pi * i / 2046));
		}
		for(int i = 0; i < 128; ++i)
		{
			const auto k = static_cast<size_t>(i);
			const double u = std::abs(i - 63.5) / 63.5;
			m_gain[k] = rounded(std::pow(10.0, (24.0 * i / 127 - 24) / 20));
			m_narrow[k] = rounded(1 / (1 + u * u * u * u));
			m_cut[k] = i < 64 ? 4.0f * rounded(0.25 * std::pow(10.0, (12 - 24.0 * i / 127) / 20)) : 1.0f;
		}
	}

	void Dt2Equalizer::setter(const int _l)
	{
		const auto f = static_cast<size_t>(clamp(m_frequency[_l] * 1024.0f, 0, 1023));
		const auto w = static_cast<size_t>(clamp(m_p68[_l] * 1023.0f, 0, 1023));
		const auto g = static_cast<size_t>(clamp(m_p70[_l] * 128.0f, 0, 127));
		const float x = m_f[f] * m_width[w] * m_narrow[g] * m_cut[g];
		// r = tan(pi/4 - pi j / 2046) at j = 65536 x, on 15 fractional bits, j at most 724.08
		const int phase =
			static_cast<int>(std::min(static_cast<double>(x * 2147483648.0f), static_cast<double>(0x16a09e6)));
		const auto j = static_cast<size_t>(phase >> 15);
		const float fr = static_cast<float>(phase & 0x7fff) * (1.0f / 32768);
		const float r = m_radius[j] * (1.0f - fr) + fr * m_radius[j + 1];
		const float cs = m_cos[f];
		const float sn = dt2::sqrt(1.0f - cs * cs), sq = dt2::sqrt(1.0f - r * r);
		const float c1 = sq * sn * 0.25f;
		const float words[]{c1, cs, r * sn, dt2::div(cs * c1, sn), sn, r * cs, r, sq, m_gain[g]};
		for(int k = 0; k < 9; ++k)
			m_to[k][_l] = words[k];
	}

	void Dt2Equalizer::clear(const int _lane)
	{
		m_sa[_lane] = m_sb[_lane] = 0;
		cleared(_lane);
	}

	void Dt2Equalizer::process(Block& _buf, const uint16_t _lanes)
	{
		enum { C1, Cos, RSin, C4, Sin, RCos, R, Sq, Gain };
		prepare(_lanes, _buf.lanes, [&](const int _l) { clear(_l); }, [&](const int _l) { setter(_l); });
		const V half = _mm_set1_ps(0.5f), eighth = _mm_set1_ps(0.125f), four = _mm_set1_ps(4.0f);
		for(int l = 0; l < _buf.lanes; l += 4)
		{
			if(!(_lanes >> l & 15))
				continue;
			const V mask = maskOf(_lanes, l);
			const Ramps<9> r(m_now, m_to, l);
			V sa = _mm_load_ps(m_sa + l), sb = _mm_load_ps(m_sb + l);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V x = _mm_load_ps(&_buf.s[n][l]), nv = _mm_set1_ps(static_cast<float>(n));
				const V sq = r.at(Sq, nv), rr = r.at(R, nv), cs = r.at(Cos, nv), sn = r.at(Sin, nv);
				// P, the allpass; (x + P) / 2 a notch and 4 g (x - P) / 2 a band-pass about it
				const V p = _mm_add_ps(_mm_mul_ps(sq, sa), _mm_mul_ps(rr, x));
				const V y = _mm_mul_ps(four, _mm_add_ps(_mm_mul_ps(_mm_mul_ps(r.at(Gain, nv), half), _mm_sub_ps(x, p)),
					_mm_mul_ps(_mm_add_ps(x, p), eighth)));
				const V b = _mm_sub_ps(_mm_add_ps(_mm_mul_ps(cs, sb), _mm_mul_ps(x, _mm_mul_ps(four, r.at(C1, nv)))),
					_mm_mul_ps(sa, r.at(RSin, nv)));
				sa = _mm_add_ps(_mm_mul_ps(r.at(RCos, nv), sa),
					_mm_sub_ps(_mm_mul_ps(sn, sb), _mm_mul_ps(x, _mm_mul_ps(four, r.at(C4, nv)))));
				sb = b;
				_mm_store_ps(&_buf.s[n][l], select(mask, y, x));
			}
			_mm_store_ps(m_sa + l, sa);
			_mm_store_ps(m_sb + l, sb);
		}
		settle(_lanes);
	}

	// ---- LOWPASS 4 ----

	Dt2Lowpass4::Dt2Lowpass4(const double _rate) : m_rerate(_rate), m_blocks48(48000 / _rate)
	{
		for(int i = 0; i < 1024; ++i)
		{
			const auto k = static_cast<size_t>(i);
			const double g = std::tan(Pi * cutoffHz(i, 48000) / 48000);
			m_g[k] = rounded(g / (1 + g));
			m_a[k] = rounded((g - 1) / (g + 1));
			m_tanh[k] = rounded(std::tanh(4 * (2.0 * i / 1023 - 1)));
		}
	}

	void Dt2Lowpass4::step48(const int _l)
	{
		// The DSP's words (0x3e86f634, 0x3f3c84e6)
		static const float old = fromBits(0x3e86f634), next = fromBits(0x3f3c84e6);
		float f = m_frequency[_l];
		const float r = m_p70[_l];
		// Its cutoff held under the top while the resonance is high
		if(f > 0.984375f && r > 0.78125f)
			f = 0.984375f;
		const float smoothF = m_smoothF[_l] * old + f * next, smoothR = m_smoothR[_l] * old + r * next;
		auto& history = m_history[static_cast<size_t>(m_steps[_l] & 3)];
		// Settled, the words of the last block again
		if(m_steps[_l] && smoothF == m_smoothF[_l] && smoothR == m_smoothR[_l])
		{
			const auto& last = m_history[static_cast<size_t>((m_steps[_l] - 1) & 3)];
			for(int k = 0; k < WordCount; ++k)
				history[k][_l] = last[k][_l];
			++m_steps[_l];
			m_settled[_l] = std::min(m_settled[_l] + 1, 4);
			return;
		}
		m_smoothF[_l] = smoothF;
		m_smoothR[_l] = smoothR;
		m_settled[_l] = 0;
		const float a = clamp(m_smoothF[_l] * 1024.0f, 0, 1023);
		float g = interp(m_g, a), b1 = g, pole = interp(m_a, a);
		if(m_rerate.carries())
		{
			// The one-pole G (1 + z^-1) / (1 + A z^-1) carried: its pole the bilinear one's at the same cutoff, which
			// keeps the loop's phase where the resonance sits (a pole of the same decay does not), then the zero whose
			// magnitude meets its response best (numeratorFor, its gain at DC 1)
			const double g48 = g, a48 = pole;
			const double g44 = std::tan(std::atan(g48 / (1 - g48)) * m_rerate.ratio), a44 = (g44 - 1) / (g44 + 1);
			const auto n = numeratorFor(m_rerate, targetOf(m_rerate, powerOf(g48, g48, 0), powerOf(1, a48, 0),
				powerOf(1, a44, 0)), false);
			g = static_cast<float>(n[0]);
			b1 = static_cast<float>(n[1]);
			pole = static_cast<float>(a44);
		}
		const float kf = clamp(3.5f * m_smoothR[_l] * m_smoothR[_l], 0, 4), g4 = g * g * g * g;
		const float words[]{g, b1, pole, (kf + 1.0f) * 0.25f, g4, dt2::div(1.0f, 1.0f + kf * g4), -kf * 0.125f};
		for(int k = 0; k < WordCount; ++k)
			history[k][_l] = words[k];
		++m_steps[_l];
	}

	void Dt2Lowpass4::setter(const int _l)
	{
		if(!m_rerate.carries())
		{
			step48(_l);
			for(int k = 0; k < WordCount; ++k)
				m_to[k][_l] = m_history[static_cast<size_t>((m_steps[_l] - 1) & 3)][k][_l];
			return;
		}
		// Each sample on the DT2's clock, in its block m, whose words ramp from block m - 1's to its own (the first
		// block's stand still): the DT2's ramps where this rate's samples fall
		const double perSample = m_blocks48 / BlockSize, start = m_clock[_l];
		while(m_steps[_l] <= static_cast<int>(start + (BlockSize - 1) * perSample))
			step48(_l);
		m_clock[_l] = start + m_blocks48;
		// The four blocks kept all the same and written so: they stand
		const auto bit = static_cast<uint16_t>(1 << _l);
		if(m_settled[_l] >= 4 && m_standing & bit)
			return;
		m_standing = static_cast<uint16_t>(m_settled[_l] >= 4 ? m_standing | bit : m_standing & ~bit);
		for(int n = 0; n < BlockSize; ++n)
		{
			const double t = start + n * perSample;
			const int m = static_cast<int>(t);
			const auto& from = m_history[static_cast<size_t>(std::max(m - 1, 0) & 3)];
			const auto& to = m_history[static_cast<size_t>(m & 3)];
			const auto fraction = static_cast<float>(t - m);
			for(int k = 0; k < WordCount; ++k)
				m_perSample[k][n][_l] = from[k][_l] + fraction * (to[k][_l] - from[k][_l]);
		}
	}

	void Dt2Lowpass4::clear(const int _lane)
	{
		for(auto& s : m_s)
			s[_lane] = 0;
		m_smoothF[_lane] = 1;
		m_smoothR[_lane] = 0;
		m_clock[_lane] = 0;
		m_steps[_lane] = 0;
		m_settled[_lane] = 0;
		m_standing &= static_cast<uint16_t>(~(1 << _lane));
		cleared(_lane);
	}

	void Dt2Lowpass4::process(Block& _buf, const uint16_t _lanes)
	{
		// Its knobs' smoothing moves on every block
		prepare(_lanes, _buf.lanes, [&](const int _l) { clear(_l); }, [&](const int _l) { setter(_l); }, true);
		const bool perSample = m_rerate.carries();
		const V one = _mm_set1_ps(1.0f), minusOne = _mm_set1_ps(-1.0f), half = _mm_set1_ps(0.5f);
		const V four = _mm_set1_ps(4.0f), top = _mm_set1_ps(1023.0f);
		for(int l = 0; l < _buf.lanes; l += 4)
		{
			if(!(_lanes >> l & 15))
				continue;
			const V mask = maskOf(_lanes, l);
			const Ramps<WordCount> r(m_now, m_to, l);
			V s1 = _mm_load_ps(m_s[0] + l), s2 = _mm_load_ps(m_s[1] + l), s3 = _mm_load_ps(m_s[2] + l);
			V s4 = _mm_load_ps(m_s[3] + l), s5 = _mm_load_ps(m_s[4] + l);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V x = _mm_load_ps(&_buf.s[n][l]), nv = _mm_set1_ps(static_cast<float>(n));
				// The block's ramps, or at another rate the DT2's at each sample
				const auto word = [&](const int _w)
				{
					return perSample ? _mm_load_ps(&m_perSample[_w][n][l]) : r.at(_w, nv);
				};
				const V b0 = word(B0), b1 = word(B1), a = word(A), c4 = word(C4);
				// One-pole k: b1 x[-1] + b0 x - a y[-1] (at 48 kHz b0 = b1 = G), the four from their states
				const auto stage = [&](const V _state, const V _in, const V _last)
				{
					return _mm_sub_ps(_mm_add_ps(_mm_mul_ps(b1, _state), _mm_mul_ps(b0, _in)), _mm_mul_ps(a, _last));
				};
				// p: the four stages' output with no input; v the feedback's linear solution, out 4 v
				const V p1 = _mm_sub_ps(_mm_mul_ps(b1, s1), _mm_mul_ps(a, s2));
				const V p2 = stage(s2, p1, s3), p3 = stage(s3, p2, s4), p4 = stage(s4, p3, s5);
				const V cx = _mm_mul_ps(c4, x);
				const V v = clampV(_mm_mul_ps(word(Comp), _mm_add_ps(p4, _mm_mul_ps(word(G4), cx))), minusOne, one);
				_mm_store_ps(&_buf.s[n][l], select(mask, _mm_mul_ps(four, v), x));
				// The tanh's table at (0.5 - kf v / 8) 1023, interpolated
				const V j = _mm_mul_ps(_mm_add_ps(half, _mm_mul_ps(word(Fb8), v)), top);
				alignas(16) int index[4];
				_mm_store_si128(reinterpret_cast<__m128i*>(index), _mm_cvttps_epi32(j));
				alignas(16) float lo[4], hi[4];
				for(int k = 0; k < 4; ++k)
				{
					lo[k] = m_tanh[static_cast<size_t>(index[k])];
					hi[k] = m_tanh[static_cast<size_t>(std::min(index[k] + 1, 1023))];
				}
				const V vlo = _mm_load_ps(lo);
				const V fr = _mm_sub_ps(j, _mm_cvtepi32_ps(_mm_load_si128(reinterpret_cast<const __m128i*>(index))));
				const V shaped = _mm_add_ps(vlo, _mm_mul_ps(fr, _mm_sub_ps(_mm_load_ps(hi), vlo)));
				const V u = clampV(_mm_add_ps(cx, shaped), minusOne, one);
				const V y1 = stage(s1, u, s2), y2 = stage(s2, y1, s3), y3 = stage(s3, y2, s4), y4 = stage(s4, y3, s5);
				s1 = u;
				s2 = y1;
				s3 = y2;
				s4 = y3;
				s5 = y4;
			}
			V* states[]{&s1, &s2, &s3, &s4, &s5};
			for(int k = 0; k < 5; ++k)
				_mm_store_ps(m_s[k] + l, *states[k]);
		}
		settle(_lanes);
	}

	// ---- COMB- and COMB+ ----

	Dt2Comb::Dt2Comb(const double _rate, const bool _positive)
		: m_rate(_rate), m_sign(_positive ? 1.0f : -1.0f), m_ring(static_cast<size_t>(Lanes * Ring), 0.0f)
	{
		// FEEDBACK: a minimax quartic in k / 127, highest power first, rounded to 1/8192 and capped at 1: every word of
		// the DT2's table (FILTERS.md)
		constexpr double poly[]{0.000657897464, 0.965786581, -2.92305424, 2.9566176, 1.69725684e-05};
		for(int k = 0; k < 128; ++k)
		{
			double p = 0;
			for(const double c : poly)
				p = p * (k / 127.0) + c;
			m_feedback[static_cast<size_t>(k)] = static_cast<float>(std::nearbyint(8192 * std::min(p, 1.0)) / 8192);
		}
	}

	void Dt2Comb::setter(const int _l)
	{
		const float w = m_frequency[_l], lpf = m_p68[_l];
		// The pitch: MIDI note 127 w - 12 above 0.26, capped at 8372 Hz; under it a square law down to 48000 / 8191 Hz
		float hz;
		if(w > 0.259842515f)
			hz = std::min(static_cast<float>(220.0 * std::pow(2.0, (static_cast<double>(w * 127.0f) - 69.0) / 12.0)),
				8372.01562f);
		else
		{
			const float low = 48000.0f * 0.000122085214f, x = w * 3.84848499f;
			hz = low + x * x * (27.5f - low);
		}
		// Less the samples the loop's low-pass delays it, as long in seconds at every rate
		const float scale = static_cast<float>(m_rate / 48000);
		const float dark = (1.0f - lpf) * (1.0f - lpf) * 6.0f * (1.0f - w * w * 0.9f) * scale;
		m_to[Delay][_l] = clamp(dt2::div(static_cast<float>(m_rate), hz) - dark, 1, Ring - 1);
		const float fc = std::min(clamp(lpf * lpf + w * w * 0.2f, 0, 1) * 22000.0f + 20.0f, 22020.0f * scale);
		const auto t = static_cast<float>(std::tan(Pi * static_cast<double>(fc) / m_rate));
		m_gain[_l] = dt2::div(t, t + 1.0f);
		m_to[Feedback][_l] = interp(m_feedback, clamp(m_p70[_l] * 127.0f, 0, 127)) * m_sign;
	}

	void Dt2Comb::clear(const int _lane)
	{
		// The ring keeps its words: those the write index has not passed since read as silence
		m_s[_lane] = 0;
		m_write[_lane] = 0;
		m_filling |= static_cast<uint16_t>(1 << _lane);
		cleared(_lane);
	}

	void Dt2Comb::process(Block& _buf, const uint16_t _lanes)
	{
		const uint16_t fresh = prepare(_lanes, _buf.lanes, [&](const int _l) { clear(_l); },
			[&](const int _l) { setter(_l); });
		for(int l = 0; l < _buf.lanes; ++l)
		{
			if(!(_lanes >> l & 1))
				continue;
			float* ring = m_ring.data() + static_cast<size_t>(l) * Ring;
			const int write = m_write[l];
			const bool filling = m_filling >> l & 1;
			// The reads' positions, once a block: the write index less the delay, wrapped
			int at[2];
			float fraction[2];
			for(int k = 0; k < 2; ++k)
			{
				float p = static_cast<float>(write) - (k == 0 ? m_to[Delay][l] : m_now[Delay][l]);
				if(p < 0)
					p += static_cast<float>(Ring);
				at[k] = static_cast<int>(p);
				fraction[k] = p - static_cast<float>(at[k]);
			}
			// After a Hit the old read sits at 0, the word about to be written: the new one fades in over the block
			if(fresh >> l & 1)
			{
				at[1] = 0;
				fraction[1] = 0;
			}
			const float g = m_gain[l], fb = m_now[Feedback][l], step = (m_to[Feedback][l] - fb) * (1.0f / BlockSize);
			float s = m_s[l];
			for(int n = 0; n < BlockSize; ++n)
			{
				// A word the write index has not passed since the Hit is silence, as in a cleared ring
				const auto word = [&](const int _i)
				{
					const int i = _i & (Ring - 1);
					return filling && i >= write + n ? 0.0f : ring[i];
				};
				const auto read = [&](const int _k)
				{
					return word(at[_k] + n) * (1.0f - fraction[_k]) + word(at[_k] + n + 1) * fraction[_k];
				};
				const float a = static_cast<float>(n) * (1.0f / BlockSize);
				float rd = a * read(0) + (1.0f - a) * read(1);
				// Until the write index first wraps, no read while the new one's first word is unwritten
				if(filling && !(write + n > ((at[0] + n) & (Ring - 1))))
					rd = 0;
				const float e = g * (rd - s), lp = s + e;
				s = e + lp;
				const float v = clamp(_buf.s[n][l] + (fb + step * static_cast<float>(n)) * lp, -1, 1);
				ring[(write + n) & (Ring - 1)] = v;
				_buf.s[n][l] = v;
			}
			m_s[l] = s;
			if(write + BlockSize >= Ring)
				m_filling &= static_cast<uint16_t>(~(1 << l));
			m_write[l] = (write + BlockSize) & (Ring - 1);
		}
		settle(_lanes);
	}

	// ---- LEGACY LP/HP ----

	Dt2Legacy::Dt2Legacy(const double _rate) : m_rerate(_rate)
	{
		for(int i = 0; i < 1024; ++i)
		{
			const auto k = static_cast<size_t>(i);
			m_w0[k] = rounded(2 * Pi * cutoffHz(i, 48000) / 48000);
			m_cos[k] = rounded(std::cos(2 * Pi * i / 1023), 5);
			// The resonance's limit at high cutoffs (fitted, 7.6e-6)
			m_limit[k] = rounded(1 - std::pow(2.0, (i - 686.311) / 16.9211));
		}
		for(int k = 0; k < 128; ++k)
		{
			// Qp - 1 by the resonance's index (fitted, 1.8e-6), and the limit's offset per resonance knob (fitted: 105
			// of 127 exact, the others one off; at 0 it never acts)
			m_q[static_cast<size_t>(k)] = rounded(1.00199997 * std::pow(1.03036085, k) - 0.99999993);
			m_t[static_cast<size_t>(k)] = k == 0 ? -169.0f
				: static_cast<float>(std::min(-std::nearbyint(397.446 / (k + 3.45434) - 5.4929), -1.0));
		}
	}

	void Dt2Legacy::setter(const int _l)
	{
		const float one = 1.0f, freq = m_frequency[_l], reso = m_p70[_l];
		// The operations in the setter's order: with S = sqrt(1 - C^2) one ulp of C moves the pole at low cutoffs
		const float m = clamp(reso * 128.0f, 0, 127);
		auto i = static_cast<size_t>(m);
		float fr = m - static_cast<float>(i);
		const float t = (one - fr) * m_t[i] + fr * m_t[std::min<size_t>(i + 1, 127)];
		const int j = std::clamp(static_cast<int>(t + 512.0f * freq) + 169, 0, 680);
		const float rr = clamp(reso * m_limit[static_cast<size_t>(j)], 0, one);
		const float m2 = clamp(rr * 128.0f, 0, 127);
		i = static_cast<size_t>(m2);
		fr = m2 - static_cast<float>(i);
		const float qp = (fr * m_q[std::min<size_t>(i + 1, 127)] + one) + (one - fr) * m_q[i];
		const float a = clamp(freq * 1024.0f, 0, 1023);
		i = static_cast<size_t>(a);
		fr = a - static_cast<float>(i);
		const float w0 = (one - fr) * m_w0[i] + fr * m_w0[std::min<size_t>(i + 1, 1023)];
		const float th = w0 * dt2::sqrt(one - dt2::div(one, qp * qp));
		const float ci = clamp(th * 162.815506f, 0, 1023);
		i = static_cast<size_t>(ci);
		fr = ci - static_cast<float>(i);
		const float c0 = m_cos[i];
		const float cth = (c0 - fr * c0) + fr * m_cos[std::min<size_t>(i + 1, 1023)];
		// r = (1 - w0 / (256 Qp))^256, about exp(-w0 / Qp)
		float r = dt2::div(-w0, qp) * (1.0f / 256) + one;
		for(int k = 0; k < 8; ++k)
			r = r * r;
		const float rr2 = r * r, nC = dt2::div(-(cth * (r + r)), rr2 + one);
		const float s = dt2::sqrt(one - nC * nC), g = (rr2 * (nC + one) + one) + nC;
		// p68 x 128: 0 the low-pass, 1 the high-pass x - LP, the rest dry
		const int mode = static_cast<int>(m_p68[_l] * 128.0f);
		const float words[]{g, -nC, rr2 * s, dt2::div(-(g * nC), s), s, rr2 * -nC,
			mode == 0 ? 0.0f : 1.0f, mode == 0 ? 1.0f : mode == 1 ? -1.0f : 0.0f};
		for(int k = 0; k < 8; ++k)
			m_to[k][_l] = words[k];
		if(!m_rerate.carries())
			return;
		// The coupled form its words make, in double: (G + (D E - G K) z^-1) / (1 - (C + K) z^-1 + (C K + D S) z^-2)
		const double gd = words[G], cd = words[C], dd = words[D], ed = words[E], sd = words[S], kd = words[K];
		carry(gd, dd * ed - gd * kd, 0, -(cd + kd), cd * kd + dd * sd, m_rerate, m_to, _l);
	}

	void Dt2Legacy::clear(const int _lane)
	{
		m_sa[_lane] = m_sb[_lane] = 0;
		m_placed |= static_cast<uint16_t>(1 << _lane);
		cleared(_lane);
	}

	void Dt2Legacy::process(Block& _buf, const uint16_t _lanes)
	{
		const uint16_t fresh = prepare(_lanes, _buf.lanes,
			[&](const int _l)
			{
				clear(_l);
				m_placed &= static_cast<uint16_t>(~(1 << _l));
			},
			[&](const int _l) { setter(_l); });
		for(int l = 0; l < Lanes; ++l)
			if((fresh & m_placed) >> l & 1)
				m_now[MixX][l] = m_now[MixY][l] = 0;
		m_placed &= static_cast<uint16_t>(~fresh);
		if(m_rerate.carries())
		{
			static_assert(MixX == 6 && MixY == 7, "svf mixes with the last two words");
			svf(_buf, _lanes, m_now, m_to, m_sa, m_sb, true);
			settle(_lanes);
			return;
		}

		for(int l = 0; l < _buf.lanes; l += 4)
		{
			if(!(_lanes >> l & 15))
				continue;
			const V mask = maskOf(_lanes, l);
			const Ramps<8> r(m_now, m_to, l);
			V sa = _mm_load_ps(m_sa + l), sb = _mm_load_ps(m_sb + l);
			const V zero = _mm_setzero_ps();
			for(int n = 0; n < BlockSize; ++n)
			{
				const V x = _mm_load_ps(&_buf.s[n][l]), nv = _mm_set1_ps(static_cast<float>(n));
				const V y = _mm_sub_ps(_mm_add_ps(_mm_mul_ps(r.at(G, nv), x), _mm_mul_ps(r.at(C, nv), sa)),
					_mm_mul_ps(r.at(D, nv), sb));
				sb = _mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_sub_ps(zero, r.at(E, nv)), x), _mm_mul_ps(r.at(S, nv), sa)),
					_mm_mul_ps(r.at(K, nv), sb));
				sa = y;
				const V out = _mm_add_ps(_mm_mul_ps(r.at(MixX, nv), x), _mm_mul_ps(r.at(MixY, nv), y));
				_mm_store_ps(&_buf.s[n][l], select(mask, out, x));
			}
			_mm_store_ps(m_sa + l, sa);
			_mm_store_ps(m_sb + l, sb);
		}
		settle(_lanes);
	}
}
