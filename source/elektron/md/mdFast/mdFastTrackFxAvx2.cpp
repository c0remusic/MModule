// Dist::process and Filter::render on 4 lanes in AVX doubles, this file alone built for AVX2 without fused
// multiply-adds, called only where the caller found the CPU has it (never off x64, where it does nothing). Each lane
// takes mdFastTrackFx.cpp's operations in their order, so the samples are the same
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

	namespace
	{
		// mdFastTrackFx.cpp's Poles, Zeros and limitedWhole, on 4 lanes
		struct Poles
		{
			D a, y1, y2;

			Poles(const double* _a, const double* _y1, const double* _y2)
				: a(_mm256_loadu_pd(_a)), y1(_mm256_loadu_pd(_y1)), y2(_mm256_loadu_pd(_y2))
			{
			}

			D next(const D _input, const D _c, const D _r)
			{
				const D sum = _mm256_add_pd(_mm256_add_pd(a, _mm256_mul_pd(_r, y2)), _input);
				a = _mm256_add_pd(sum, _mm256_mul_pd(_c, y1));
				y2 = y1;
				y1 = limit(floorWord(a));
				return y1;
			}

			void keep(const D _a, double* _aOut, double* _y1, double* _y2) const
			{
				_mm256_storeu_pd(_aOut, _a);
				_mm256_storeu_pd(_y1, y1);
				_mm256_storeu_pd(_y2, y2);
			}
		};

		struct Zeros
		{
			D x1, x2;

			Zeros(const double* _x1, const double* _x2) : x1(_mm256_loadu_pd(_x1)), x2(_mm256_loadu_pd(_x2)) {}

			D next(const D _x, const D _h, const D _e)
			{
				const D sum = _mm256_add_pd(_mm256_mul_pd(_h, _mm256_add_pd(_x, x2)), _mm256_mul_pd(_e, x1));
				x2 = x1;
				x1 = _x;
				return sum;
			}

			void keep(double* _x1, double* _x2) const
			{
				_mm256_storeu_pd(_x1, x1);
				_mm256_storeu_pd(_x2, x2);
			}
		};

		D limitedWhole(const D _a)
		{
			const D top = _mm256_set1_pd(1.0 - 1.0 / (uint64_t(1) << 47));
			return _mm256_min_pd(_mm256_max_pd(_a, _mm256_set1_pd(-1.0)), top);
		}

		// Filter::render's sections in their order; _Mask 0 reads the words' first sample throughout
		template<int Mask>
		void renderFilter(Block& _buf, const int _l, const double (*_w)[BlockSize][4], double (*_a)[Lanes],
			double (*_y1)[Lanes], double (*_y2)[Lanes], double (*_x1)[Lanes], double (*_x2)[Lanes])
		{
			D x[BlockSize];
			for(int n = 0; n < BlockSize; ++n)
				x[n] = _mm256_cvtps_pd(_mm_loadu_ps(&_buf.s[n][_l]));

			Poles low0(_a[0] + _l, _y1[0] + _l, _y2[0] + _l), low1(_a[1] + _l, _y1[1] + _l, _y2[1] + _l);
			Poles high0(_a[2] + _l, _y1[2] + _l, _y2[2] + _l), high1(_a[3] + _l, _y1[3] + _l, _y2[3] + _l);
			Zeros zeros0(_x1[0] + _l, _x2[0] + _l), zeros1(_x1[1] + _l, _x2[1] + _l);

			enum { LowC, LowR, LowH, HighC, HighR, HighH, HighE };
			for(int n = 0; n < BlockSize; ++n)
			{
				const int i = n & Mask;
				const D lowC = _mm256_load_pd(_w[LowC][i]), lowR = _mm256_load_pd(_w[LowR][i]);
				const D lowH = _mm256_load_pd(_w[LowH][i]);
				const D highC = _mm256_load_pd(_w[HighC][i]), highR = _mm256_load_pd(_w[HighR][i]);
				const D highH = _mm256_load_pd(_w[HighH][i]), highE = _mm256_load_pd(_w[HighE][i]);

				// Each low-pass hands the next section its output 2 samples late
				const D toLow1 = low0.y2, toHigh0 = low1.y2;
				low0.next(_mm256_mul_pd(lowH, x[n]), lowC, lowR);
				low1.next(_mm256_mul_pd(lowH, toLow1), lowC, lowR);
				const D out = high0.next(zeros0.next(toHigh0, highH, highE), highC, highR);
				x[n] = high1.next(zeros1.next(out, highH, highE), highC, highR);
			}

			low0.keep(limitedWhole(low0.a), _a[0] + _l, _y1[0] + _l, _y2[0] + _l);
			low1.keep(limitedWhole(low1.a), _a[1] + _l, _y1[1] + _l, _y2[1] + _l);
			high0.keep(limitedWhole(high0.a), _a[2] + _l, _y1[2] + _l, _y2[2] + _l);
			high1.keep(limitedWhole(high1.a), _a[3] + _l, _y1[3] + _l, _y2[3] + _l);
			zeros0.keep(_x1[0] + _l, _x2[0] + _l);
			zeros1.keep(_x1[1] + _l, _x2[1] + _l);
			for(int n = 0; n < BlockSize; ++n)
				_mm_storeu_ps(&_buf.s[n][_l], _mm256_cvtpd_ps(x[n]));
		}
	}

	void Filter::renderAvx2(Block& _buf, const int _l, const double (*_words)[BlockSize][4], const bool _moving)
	{
		if(_moving)
			renderFilter<BlockSize - 1>(_buf, _l, _words, m_a, m_y1, m_y2, m_x1, m_x2);
		else
			renderFilter<0>(_buf, _l, _words, m_a, m_y1, m_y2, m_x1, m_x2);
	}
}
#else
namespace md::fast
{
	void Dist::processAvx2(Block&, int) {}
	void Filter::renderAvx2(Block&, int, const double (*)[BlockSize][4], bool) {}
}
#endif
