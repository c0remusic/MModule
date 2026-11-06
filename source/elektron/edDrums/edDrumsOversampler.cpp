#include "edDrumsOversampler.h"

#include <cmath>
#include <initializer_list>

#include <emmintrin.h>

namespace elektron::drums
{
	namespace
	{
		using V = __m128;

		constexpr double Pi = 3.14159265358979323846;

		constexpr int FullBand = 8, UpperBand = 4;		// HIIR's sections at 2x, and for the second octave of 4x
		constexpr double PassBand = 20200;				// Hz, where the first half-band still holds
		constexpr double SecondPass = 22050;

		// One band of a polyphase pair: _in's samples through its two all-pass chains, the coefficients taken in turn
		struct Sections
		{
			V x[HalfBand::MaxCoefficients], y[HalfBand::MaxCoefficients], c[HalfBand::MaxCoefficients];
			int count;

			Sections(const HalfBand& _band, const float (*_x)[Lanes], const float (*_y)[Lanes], const int _l) : count(_band.count())
			{
				for(int i = 0; i < count; ++i)
				{
					x[i] = _mm_load_ps(_x[i] + _l);
					y[i] = _mm_load_ps(_y[i] + _l);
					c[i] = _mm_set1_ps(_band.coefficients()[i]);
				}
			}

			void keep(float (*_x)[Lanes], float (*_y)[Lanes], const int _l) const
			{
				for(int i = 0; i < count; ++i)
				{
					_mm_store_ps(_x[i] + _l, x[i]);
					_mm_store_ps(_y[i] + _l, y[i]);
				}
			}

			void run(V& _even, V& _odd)
			{
				for(int i = 0; i < count; i += 2)
				{
					const V even = _mm_add_ps(_mm_mul_ps(_mm_sub_ps(_even, y[i]), c[i]), x[i]);
					const V odd = _mm_add_ps(_mm_mul_ps(_mm_sub_ps(_odd, y[i + 1]), c[i + 1]), x[i + 1]);
					x[i] = _even;
					x[i + 1] = _odd;
					y[i] = _even = even;
					y[i + 1] = _odd = odd;
				}
			}
		};

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

	Oversampler::Oversampler(const double _rate)
		: m_first(FullBand, 0.25 - PassBand / (2 * _rate)), m_second(UpperBand, 0.25 - SecondPass / (4 * _rate))
	{
	}

	void Oversampler::clear(const int _lane)
	{
		for(auto* stages : {m_up, m_down})
			for(int s = 0; s < 2; ++s)
				for(int i = 0; i < HalfBand::MaxCoefficients; ++i)
					stages[s].x[i][_lane] = stages[s].y[i][_lane] = 0;
	}

	void Oversampler::up(const Block& _in, Block* _out, const int _factor, const uint16_t _lanes)
	{
		for(int l = 0; l < Lanes; l += 4)
		{
			if(!(_lanes >> l & 15))
				continue;
			alignas(16) V two[2 * BlockSize];
			Sections first(m_first, m_up[0].x, m_up[0].y, l);
			for(int n = 0; n < BlockSize; ++n)
			{
				V even = _mm_load_ps(&_in.s[n][l]), odd = even;
				first.run(even, odd);
				two[2 * n] = even;
				two[2 * n + 1] = odd;
			}
			first.keep(m_up[0].x, m_up[0].y, l);
			if(_factor == 2)
			{
				for(int n = 0; n < 2 * BlockSize; ++n)
					_mm_store_ps(&_out[n / BlockSize].s[n % BlockSize][l], two[n]);
				continue;
			}
			Sections second(m_second, m_up[1].x, m_up[1].y, l);
			for(int n = 0; n < 2 * BlockSize; ++n)
			{
				V even = two[n], odd = even;
				second.run(even, odd);
				const int at = 2 * n;
				_mm_store_ps(&_out[at / BlockSize].s[at % BlockSize][l], even);
				_mm_store_ps(&_out[(at + 1) / BlockSize].s[(at + 1) % BlockSize][l], odd);
			}
			second.keep(m_up[1].x, m_up[1].y, l);
		}
	}

	void Oversampler::down(const Block* _in, Block& _out, const int _factor, const uint16_t _lanes)
	{
		const V half = _mm_set1_ps(0.5f);
		for(int l = 0; l < Lanes; l += 4)
		{
			if(!(_lanes >> l & 15))
				continue;
			alignas(16) V two[2 * BlockSize];
			if(_factor == 2)
			{
				for(int n = 0; n < 2 * BlockSize; ++n)
					two[n] = _mm_load_ps(&_in[n / BlockSize].s[n % BlockSize][l]);
			}
			else
			{
				Sections second(m_second, m_down[1].x, m_down[1].y, l);
				for(int n = 0; n < 2 * BlockSize; ++n)
				{
					const int at = 2 * n;
					V even = _mm_load_ps(&_in[(at + 1) / BlockSize].s[(at + 1) % BlockSize][l]);
					V odd = _mm_load_ps(&_in[at / BlockSize].s[at % BlockSize][l]);
					second.run(even, odd);
					two[n] = _mm_mul_ps(_mm_add_ps(even, odd), half);
				}
				second.keep(m_down[1].x, m_down[1].y, l);
			}
			Sections first(m_first, m_down[0].x, m_down[0].y, l);
			for(int n = 0; n < BlockSize; ++n)
			{
				V even = two[2 * n + 1], odd = two[2 * n];
				first.run(even, odd);
				_mm_store_ps(&_out.s[n][l], _mm_mul_ps(_mm_add_ps(even, odd), half));
			}
			first.keep(m_down[0].x, m_down[0].y, l);
		}
	}
}
