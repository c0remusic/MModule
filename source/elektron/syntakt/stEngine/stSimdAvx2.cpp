// The AVX2 kernels. MSVC compiles this file alone with /arch:AVX2 (CMakeLists.txt), GCC and Clang compile its functions
// for AVX2 by their target attribute: nothing here runs before simd::avx2() says the CPU has it. It includes no header
// whose inline functions other files share, so that no AVX2 copy of one can replace theirs at link time
#include "stSimd.h"

#if defined(_M_X64) || defined(__x86_64__)

#include <immintrin.h>

#if defined(__GNUC__) || defined(__clang__)
#	define ST_AVX2 __attribute__((target("avx2")))
#else
#	define ST_AVX2
#endif

namespace syntakt::simd
{
	namespace
	{
		// Each signed 64-bit lane shifted right by N, as int64_t's >> does: AVX2 shifts 64-bit lanes logically only,
		// so the sign's bits go back in from a compare
		template<int N>
		ST_AVX2 inline __m256i sra64(const __m256i _x)
		{
			const __m256i sign = _mm256_cmpgt_epi64(_mm256_setzero_si256(), _x);
			return _mm256_or_si256(_mm256_srli_epi64(_x, N), _mm256_slli_epi64(sign, 64 - N));
		}

		// Each 64-bit lane saturated to 32 bits
		ST_AVX2 inline __m256i saturate32(__m256i _x)
		{
			const __m256i max = _mm256_set1_epi64x(INT32_MAX), min = _mm256_set1_epi64x(INT32_MIN);
			_x = _mm256_blendv_epi8(_x, max, _mm256_cmpgt_epi64(_x, max));
			return _mm256_blendv_epi8(_x, min, _mm256_cmpgt_epi64(min, _x));
		}

		// The longs' bytes reversed: the big-endian CPU's order and the host's
		ST_AVX2 inline __m256i swapBytes(const __m256i _x)
		{
			const __m256i order = _mm256_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12, 3, 2, 1, 0, 7,
				6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
			return _mm256_shuffle_epi8(_x, order);
		}

		// The low 32 bits of the even frames' and the odd frames' 64-bit lanes, back in frame order
		ST_AVX2 inline __m256i interleave(const __m256i _even, const __m256i _odd)
		{
			return _mm256_blend_epi32(_even, _mm256_slli_epi64(_odd, 32), 0xaa);
		}

		// Eight longs from _at, _stride bytes apart, the lanes _valid sets (the others 0, not read)
		ST_AVX2 inline __m256i load(const uint8_t* _at, const uint32_t _stride, const __m256i _valid)
		{
			const auto* at = reinterpret_cast<const int*>(_at);
			if(_stride == 4)
				return _mm256_maskload_epi32(at, _valid);
			const __m256i index = _mm256_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14);
			return _mm256_mask_i32gather_epi32(_mm256_setzero_si256(), at, index, _valid, 4);
		}

		// Eight longs to _at, _stride bytes apart, the lanes _valid sets (the others not written)
		ST_AVX2 inline void store(uint8_t* _at, const uint32_t _stride, const __m256i _valid, const __m256i _v)
		{
			auto* at = reinterpret_cast<int*>(_at);
			if(_stride == 4)
			{
				_mm256_maskstore_epi32(at, _valid, _v);
				return;
			}
			const __m256i low = _mm256_setr_epi32(0, 0, 1, 1, 2, 2, 3, 3);
			const __m256i high = _mm256_setr_epi32(4, 4, 5, 5, 6, 6, 7, 7);
			const __m256i even = _mm256_setr_epi32(-1, 0, -1, 0, -1, 0, -1, 0);
			const __m256i validLow = _mm256_and_si256(_mm256_permutevar8x32_epi32(_valid, low), even);
			const __m256i validHigh = _mm256_and_si256(_mm256_permutevar8x32_epi32(_valid, high), even);
			_mm256_maskstore_epi32(at, validLow, _mm256_permutevar8x32_epi32(_v, low));
			_mm256_maskstore_epi32(at + 8, validHigh, _mm256_permutevar8x32_epi32(_v, high));
		}

		template<bool BigEndian>
		ST_AVX2 void rampMix(const RampMix& _m)
		{
			const __m256i frame = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
			const __m256i zero = _mm256_setzero_si256();

			// Each channel's gains for the first eight frames, and eight steps
			__m256i gains[MaxChannels], steps[MaxChannels];
			for(int k = 0; k < _m.channels; ++k)
			{
				const __m256i step = _mm256_set1_epi32(_m.steps[k]);
				gains[k] = _mm256_add_epi32(_mm256_set1_epi32(_m.gains[k]), _mm256_mullo_epi32(frame, step));
				steps[k] = _mm256_slli_epi32(step, 3);
			}

			for(uint32_t i = _m.first; i < 32; i += 8)
			{
				const __m256i valid = _mm256_cmpgt_epi32(_mm256_set1_epi32(static_cast<int>(32 - i)), frame);

				// The even frames' products in the low halves of four 64-bit lanes, the odd frames' in another four
				__m256i even = zero, odd = zero;
				for(int k = 0; k < _m.channels; ++k)
				{
					__m256i x = load(_m.src[k] + _m.srcStride * i, _m.srcStride, valid);
					if constexpr(BigEndian)
						x = swapBytes(x);
					const __m256i g = gains[k];
					even = _mm256_add_epi64(even, sra64<23>(_mm256_mul_epi32(g, x)));
					odd = _mm256_add_epi64(odd,
						sra64<23>(_mm256_mul_epi32(_mm256_srli_epi64(g, 32), _mm256_srli_epi64(x, 32))));
					gains[k] = _mm256_add_epi32(g, steps[k]);
				}
				if(_m.negate)
				{
					even = _mm256_sub_epi64(zero, even);
					odd = _mm256_sub_epi64(zero, odd);
				}
				even = sra64<8>(even);
				odd = sra64<8>(odd);
				if(_m.saturate)
				{
					even = saturate32(even);
					odd = saturate32(odd);
				}
				__m256i v = interleave(even, odd);
				if constexpr(BigEndian)
					v = swapBytes(v);
				store(_m.dst + _m.dstStride * i, _m.dstStride, valid, v);
			}
			_mm256_zeroupper();
		}

		ST_AVX2 void scale(uint8_t* _bus, const uint32_t _first, const uint32_t _count, const int32_t _gain)
		{
			const __m256i index = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
			const __m256i gain = _mm256_set1_epi32(_gain);
			for(uint32_t i = _first; i < _count; i += 8)
			{
				const __m256i valid = _mm256_cmpgt_epi32(_mm256_set1_epi32(static_cast<int>(_count - i)), index);
				auto* at = reinterpret_cast<int*>(_bus + 4 * i);
				const __m256i x = swapBytes(_mm256_maskload_epi32(at, valid));
				const __m256i even = saturate32(_mm256_mul_epi32(gain, x));
				const __m256i odd = saturate32(_mm256_mul_epi32(gain, _mm256_srli_epi64(x, 32)));
				_mm256_maskstore_epi32(at, valid, swapBytes(interleave(even, odd)));
			}
			_mm256_zeroupper();
		}
	}

	// Plain functions that call the AVX2 ones: GCC and Clang see one version of each, without a target attribute
	void rampMixAvx2(const RampMix& _mix)
	{
		if(_mix.bigEndian)
			rampMix<true>(_mix);
		else
			rampMix<false>(_mix);
	}

	void scaleAvx2(uint8_t* _bus, const uint32_t _first, const uint32_t _count, const int32_t _gain)
	{
		scale(_bus, _first, _count, _gain);
	}
}

#else

// Elsewhere (ARM64) avx2() is false and nothing calls these
namespace syntakt::simd
{
	void rampMixAvx2(const RampMix&) {}
	void scaleAvx2(uint8_t*, uint32_t, uint32_t, int32_t) {}
}

#endif
