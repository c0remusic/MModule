// Overdrive::process on 4 lanes in AVX doubles, this file alone built with /arch:AVX2, called only where rate::fastFir()
// says the CPU runs it (never off x64, where it does nothing). Each lane takes the SSE2 build's operations
#include "edDrumsMaster.h"

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>

namespace elektron::drums
{
	void Overdrive::processAvx2(Block& _buf, const int _l, const __m128 _written) const
	{
		using D = __m256d;
		const D one = _mm256_set1_pd(1), minusOne = _mm256_set1_pd(-1), two = _mm256_set1_pd(2);
		const D sign = _mm256_set1_pd(-0.0);
		const D gain = _mm256_loadu_pd(m_gain + _l), mix = _mm256_loadu_pd(m_mix + _l);
		for(int n = 0; n < BlockSize; ++n)
		{
			const __m128 in = _mm_loadu_ps(&_buf.s[n][_l]);
			const D x = _mm256_cvtps_pd(in);
			D u = _mm256_min_pd(_mm256_max_pd(_mm256_mul_pd(gain, x), minusOne), one);
			for(int pass = 0; pass < 3; ++pass)
				u = _mm256_mul_pd(u, _mm256_sub_pd(two, _mm256_andnot_pd(sign, u)));
			const __m128 wet = _mm256_cvtpd_ps(_mm256_add_pd(x, _mm256_mul_pd(mix, _mm256_sub_pd(u, x))));
			_mm_storeu_ps(&_buf.s[n][_l], _mm_or_ps(_mm_and_ps(_written, wet), _mm_andnot_ps(_written, in)));
		}
	}
}
#else
namespace elektron::drums
{
	void Overdrive::processAvx2(Block&, int, __m128) const {}
}
#endif
