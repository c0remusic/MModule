// edDrumsRate.h's AVX2 kernels, this file alone built with /arch:AVX2, called only where fastFir() says the CPU runs them
#include "edDrumsRate.h"

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>

namespace elektron::drums::rate
{
	namespace
	{
		float dot48(const float* _x, const float* _h)
		{
			__m256 a = _mm256_mul_ps(_mm256_loadu_ps(_x), _mm256_loadu_ps(_h));
			__m256 b = _mm256_mul_ps(_mm256_loadu_ps(_x + 8), _mm256_loadu_ps(_h + 8));
			a = _mm256_fmadd_ps(_mm256_loadu_ps(_x + 16), _mm256_loadu_ps(_h + 16), a);
			b = _mm256_fmadd_ps(_mm256_loadu_ps(_x + 24), _mm256_loadu_ps(_h + 24), b);
			a = _mm256_fmadd_ps(_mm256_loadu_ps(_x + 32), _mm256_loadu_ps(_h + 32), a);
			b = _mm256_fmadd_ps(_mm256_loadu_ps(_x + 40), _mm256_loadu_ps(_h + 40), b);
			a = _mm256_add_ps(a, b);
			__m128 s = _mm_add_ps(_mm256_castps256_ps128(a), _mm256_extractf128_ps(a, 1));
			s = _mm_add_ps(s, _mm_movehl_ps(s, s));
			s = _mm_add_ss(s, _mm_shuffle_ps(s, s, 1));
			return _mm_cvtss_f32(s);
		}
	}

	void readFir(const float* _ring, const int _mask, int64_t _i, int _p, const int _up, const int _down,
		const float* _table, const int8_t* _unit, float* _out, const int _n)
	{
		static_assert(RateFir::Taps == 48, "six vectors a row");
		for(int k = 0; k < _n; ++k)
		{
			const float* window = _ring + ((_i - (RateFir::Taps - 1)) & _mask);
			if(_unit && _unit[_p] >= 0)
				_out[k] += window[_unit[_p]];
			else
				_out[k] += dot48(window, _table + _p * RateFir::Taps);
			_p += _down;
			while(_p >= _up)
			{
				_p -= _up;
				++_i;
			}
		}
	}

	void frames8(const float* _x, const int64_t _first, const int64_t _base, const int _up, const int _down,
		const RateFir& _fir, float* _out)
	{
		const int taps = _fir.taps();
		for(int n = 0; n < BlockSize; ++n)
		{
			const int64_t m = _first + n;
			const float* h = _fir.row(static_cast<int>(m * _down % _up));
			const float* newest = _x + 8 * (m * _down / _up - _base);
			__m256 a = _mm256_setzero_ps(), b = _mm256_setzero_ps();
			int k = 0;
			for(; k + 2 <= taps; k += 2)
			{
				a = _mm256_fmadd_ps(_mm256_set1_ps(h[taps - 1 - k]), _mm256_loadu_ps(newest - 8 * k), a);
				b = _mm256_fmadd_ps(_mm256_set1_ps(h[taps - 2 - k]), _mm256_loadu_ps(newest - 8 * (k + 1)), b);
			}
			for(; k < taps; ++k)
				a = _mm256_fmadd_ps(_mm256_set1_ps(h[taps - 1 - k]), _mm256_loadu_ps(newest - 8 * k), a);
			_mm256_storeu_ps(_out + 8 * n, _mm256_add_ps(a, b));
		}
	}
}
#else
// No AVX2 here: fastFir() is false, nothing calls these
namespace elektron::drums::rate
{
	void readFir(const float*, int, int64_t, int, int, int, const float*, const int8_t*, float*, int) {}
	void frames8(const float*, int64_t, int64_t, int, int, const RateFir&, float*) {}
}
#endif
