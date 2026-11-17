// Dist::process on 4 lanes in AVX doubles, this file alone built for AVX2 without fused multiply-adds, called only where
// the caller found the CPU has it (never off x64, where it does nothing). Each lane takes mdFastTrackFx.cpp's operations
// in their order, so the samples are the same
#include "mdFastTrackFx.h"

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>

#include "mdFastDsp.h"

namespace md::fast
{
	namespace
	{
		using D = __m256d;

		constexpr double LastWord = 8388607.0 / 8388608, Lsb = 1.0 / 8388608;

		D floorWord(const D _a)
		{
			const D huge = _mm256_set1_pd(3.0 * (1 << 28));
			const D half = _mm256_set1_pd(1.0 / (1 << 24) - 1.0 / (uint64_t(1) << 48));
			return _mm256_sub_pd(_mm256_add_pd(_mm256_sub_pd(_a, half), huge), huge);
		}

		D limit(const D _word) { return _mm256_min_pd(_mm256_max_pd(_word, _mm256_set1_pd(-1.0)), _mm256_set1_pd(LastWord)); }
	}

	void Dist::processAvx2(Block& _buf, const int _l)
	{
		const D gain = _mm256_loadu_pd(m_gain + _l), out = _mm256_loadu_pd(m_out + _l);
		D y1 = _mm256_loadu_pd(m_y1 + _l), c1 = _mm256_loadu_pd(m_c1 + _l);

		// The first sample takes back the last one's clipped word at the last block's gain
		const D old = _mm256_mul_pd(_mm256_sub_pd(out, _mm256_loadu_pd(m_outOld + _l)), c1);
		D a = _mm256_add_pd(_mm256_loadu_pd(m_a + _l), old), word = y1;
		constexpr double Feedback = -dsp::wordOf(0.002) * Lsb;
		const D feedback = _mm256_set1_pd(Feedback);
		for(int n = 0; n < BlockSize; ++n)
		{
			// The shift in the 56-bit accumulator
			const D x = _mm256_cvtps_pd(_mm_loadu_ps(&_buf.s[n][_l]));
			const D p = _mm256_mul_pd(_mm256_mul_pd(gain, x), _mm256_set1_pd(512.0));
			const D wraps = _mm256_or_pd(_mm256_cmp_pd(p, _mm256_set1_pd(256.0), _CMP_GE_OS),
				_mm256_cmp_pd(p, _mm256_set1_pd(-256.0), _CMP_LT_OS));
			const D turn = _mm256_or_pd(_mm256_and_pd(p, _mm256_set1_pd(-0.0)), _mm256_set1_pd(512.0));
			const D c = limit(floorWord(_mm256_sub_pd(p, _mm256_and_pd(wraps, turn))));
			const D moved = _mm256_add_pd(a, _mm256_mul_pd(out, _mm256_sub_pd(c, c1)));
			a = _mm256_add_pd(moved, _mm256_mul_pd(feedback, y1));
			c1 = c;
			word = floorWord(a);
			y1 = limit(word);
			_mm_storeu_ps(&_buf.s[n][_l], _mm256_cvtpd_ps(y1));
		}
		_mm256_storeu_pd(m_a + _l, _mm256_add_pd(y1, _mm256_sub_pd(a, word)));
		_mm256_storeu_pd(m_y1 + _l, y1);
		_mm256_storeu_pd(m_c1 + _l, c1);
		_mm256_storeu_pd(m_outOld + _l, out);
	}
}
#else
namespace md::fast
{
	void Dist::processAvx2(Block&, int) {}
}
#endif
