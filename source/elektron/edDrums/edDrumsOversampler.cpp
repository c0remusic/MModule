#include "edDrumsOversampler.h"
#include "edDrumsOversamplerKernel.h"

#include <cmath>
#include <initializer_list>

#include <emmintrin.h>

namespace elektron::drums
{
	using namespace oversampling;

	namespace
	{
		constexpr double Pi = 3.14159265358979323846;

		constexpr int FullBand = 8, UpperBand = 4;		// HIIR's sections at 2x, and for the second octave of 4x
		constexpr double PassBand = 20200;				// Hz, where the first half-band still holds
		constexpr double SecondPass = 22050;

		// HIIR's PolyphaseIir2Designer
		double numerator(const double _q, const int _order, const int _c)
		{
			double sum = 0, term = 0;
			int i = 0, sign = 1;
			do
			{
				term = std::pow(_q, i * (i + 1)) * std::sin((i * 2 + 1) * _c * Pi / _order) * sign;
				sum += term;
				sign = -sign;
				++i;
			} while(std::abs(term) > 1e-100 && i < 64);
			return sum;
		}

		double denominator(const double _q, const int _order, const int _c)
		{
			double sum = 0, term = 0;
			int i = 1, sign = -1;
			do
			{
				term = std::pow(_q, i * i) * std::cos(i * 2 * _c * Pi / _order) * sign;
				sum += term;
				sign = -sign;
				++i;
			} while(std::abs(term) > 1e-100 && i < 64);
			return sum;
		}
	}

	HalfBand::HalfBand(const int _coefficients, const double _transition) : m_c{}, m_count(_coefficients)
	{
		const double k0 = std::tan((1 - _transition * 2) * Pi / 4);
		const double k = k0 * k0;
		const double root = std::pow(1 - k * k, 0.25);
		const double e = 0.5 * (1 - root) / (1 + root);
		const double e2 = e * e, e4 = e2 * e2;
		const double q = e * (1 + e4 * (2 + e4 * (15 + 150 * e4)));
		const int order = _coefficients * 2 + 1;
		for(int i = 0; i < _coefficients; ++i)
		{
			const int c = i + 1;
			const double ww0 = numerator(q, order, c) * std::pow(q, 0.25) / (denominator(q, order, c) + 0.5);
			const double ww = ww0 * ww0;
			const double x = std::sqrt((1 - ww * k) * (1 - ww / k)) / (1 + ww);
			m_c[i] = static_cast<float>((1 - x) / (1 + x));
		}
	}

	Oversampler::Oversampler(const double _rate, const bool _avx2)
		: m_first(FullBand, 0.25 - PassBand / (2 * _rate)), m_second(UpperBand, 0.25 - SecondPass / (4 * _rate)),
		  m_avx2(_avx2)
	{
	}

	void Oversampler::clear(const int _lane)
	{
		for(auto* stages : {m_up, m_down})
			for(int s = 0; s < 2; ++s)
				for(int i = 0; i < HalfBand::MaxCoefficients; ++i)
					stages[s].x[i][_lane] = stages[s].y[i][_lane] = 0;
	}

	namespace
	{
		struct Sse
		{
			using V = __m128;
			static V load(const float* _p) { return _mm_load_ps(_p); }
			static void store(float* _p, const V _v) { _mm_store_ps(_p, _v); }
			static V set1(const float _f) { return _mm_set1_ps(_f); }
			static V add(const V _a, const V _b) { return _mm_add_ps(_a, _b); }
			static V sub(const V _a, const V _b) { return _mm_sub_ps(_a, _b); }
			static V mul(const V _a, const V _b) { return _mm_mul_ps(_a, _b); }
		};

		// In AVX, where the caller found the CPU has it, the groups of 8 lanes whose two halves both run: a half left out
		// keeps its history
		bool both(const uint16_t _lanes, const int _l) { return (_lanes >> _l & 15) && (_lanes >> (_l + 4) & 15); }
	}

	void Oversampler::up(const Block& _in, Block* _out, const int _factor, const uint16_t _lanes)
	{
		const Band first{m_first.coefficients(), m_first.count(), m_up[0].x, m_up[0].y};
		const Band second{m_second.coefficients(), m_second.count(), m_up[1].x, m_up[1].y};
		for(int l = 0; l < Lanes; l += 8)
		{
			if(m_avx2 && both(_lanes, l))
			{
				upAvx2(first, second, _in, _out, _factor, l);
				continue;
			}
			for(const int g : {l, l + 4})
				if(_lanes >> g & 15)
					upGroup<Sse>(first, second, _in, _out, _factor, g);
		}
	}

	void Oversampler::down(const Block* _in, Block& _out, const int _factor, const uint16_t _lanes)
	{
		const Band first{m_first.coefficients(), m_first.count(), m_down[0].x, m_down[0].y};
		const Band second{m_second.coefficients(), m_second.count(), m_down[1].x, m_down[1].y};
		for(int l = 0; l < Lanes; l += 8)
		{
			if(m_avx2 && both(_lanes, l))
			{
				downAvx2(first, second, _in, _out, _factor, l);
				continue;
			}
			for(const int g : {l, l + 4})
				if(_lanes >> g & 15)
					downGroup<Sse>(first, second, _in, _out, _factor, g);
		}
	}
}
