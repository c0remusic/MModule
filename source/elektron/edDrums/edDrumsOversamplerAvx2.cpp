// edDrumsOversamplerKernel.h's up and down on 8 lanes, this file alone built with /arch:AVX2, called only where
// rate::fastFir() says the CPU runs it (never off x64, where the two do nothing)
#include "edDrumsOversamplerKernel.h"

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>

namespace elektron::drums::oversampling
{
	namespace
	{
		struct Avx
		{
			using V = __m256;
			static V load(const float* _p) { return _mm256_loadu_ps(_p); }
			static void store(float* _p, const V _v) { _mm256_storeu_ps(_p, _v); }
			static V set1(const float _f) { return _mm256_set1_ps(_f); }
			static V add(const V _a, const V _b) { return _mm256_add_ps(_a, _b); }
			static V sub(const V _a, const V _b) { return _mm256_sub_ps(_a, _b); }
			static V mul(const V _a, const V _b) { return _mm256_mul_ps(_a, _b); }
		};
	}

	void upAvx2(const Band _first, const Band _second, const Block& _in, Block* _out, const int _factor, const int _l)
	{
		upGroup<Avx>(_first, _second, _in, _out, _factor, _l);
	}

	void downAvx2(const Band _first, const Band _second, const Block* _in, Block& _out, const int _factor, const int _l)
	{
		downGroup<Avx>(_first, _second, _in, _out, _factor, _l);
	}
}
#else
namespace elektron::drums::oversampling
{
	void upAvx2(Band, Band, const Block&, Block*, int, int) {}
	void downAvx2(Band, Band, const Block*, Block&, int, int) {}
}
#endif
