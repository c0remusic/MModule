#include "stFastStages.h"
#include <array>

namespace syntakt::fast
{
	using namespace simd;

	void mix(const Block& _x, const Block& _y, Block& _out, const float* _gxFrom, const float* _gxTo,
		const float* _gyFrom, const float* _gyTo)
	{
		for(int g = 0; g < Lanes; g += 4)
		{
			V gx = ramp(_gxFrom, g), gy = ramp(_gyFrom, g);
			const V sx = stepOf(_gxFrom, _gxTo, g), sy = stepOf(_gyFrom, _gyTo, g);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V x = _mm_mul_ps(gx, _mm_load_ps(_x.s[n] + g)), y = _mm_mul_ps(gy, _mm_load_ps(_y.s[n] + g));
				_mm_store_ps(_out.s[n] + g, _mm_add_ps(x, y));
				gx = _mm_add_ps(gx, sx);
				gy = _mm_add_ps(gy, sy);
			}
		}
	}

	void rampedGain(Block& _buf, const float* _from, const float* _to)
	{
		for(int g = 0; g < Lanes; g += 4)
		{
			V v = ramp(_from, g);
			const V s = stepOf(_from, _to, g);
			for(int n = 0; n < BlockSize; ++n)
			{
				_mm_store_ps(_buf.s[n] + g, _mm_mul_ps(v, _mm_load_ps(_buf.s[n] + g)));
				v = _mm_add_ps(v, s);
			}
		}
	}

	void gain(Block& _buf, const Block& _g)
	{
		for(int n = 0; n < BlockSize; ++n)
			for(int g = 0; g < Lanes; g += 4)
				_mm_store_ps(_buf.s[n] + g, _mm_mul_ps(_mm_load_ps(_buf.s[n] + g), _mm_load_ps(_g.s[n] + g)));
	}

	void onePole(Block& _buf, const OnePole& _c, float* _x1, float* _y1)
	{
		const V b0 = _mm_set1_ps(_c.b0), b1 = _mm_set1_ps(_c.b1), a1 = _mm_set1_ps(_c.a1);
		for(int g = 0; g < Lanes; g += 4)
		{
			V x1 = _mm_load_ps(_x1 + g), y1 = _mm_load_ps(_y1 + g);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V x = _mm_load_ps(_buf.s[n] + g);
				y1 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(b0, x), _mm_mul_ps(b1, x1)), _mm_mul_ps(a1, y1));
				x1 = x;
				_mm_store_ps(_buf.s[n] + g, y1);
			}
			_mm_store_ps(_x1 + g, x1);
			_mm_store_ps(_y1 + g, y1);
		}
	}

	namespace
	{
		// The RAM sine the machine's oscillators read: sin(2 pi i / 256) in Q31, 257 words
		const std::array<int64_t, 257> QSine = []
		{
			std::array<int64_t, 257> t{};
			for(int i = 0; i < 257; ++i)
				t[i] = std::clamp<int64_t>(std::llround(std::sin(2 * 3.14159265358979323846 * i / 256) * 2147483648.0),
					INT32_MIN, INT32_MAX);
			return t;
		}();
	}

	void feedbackSine(Block& _out, uint32_t* _phase, const Increments& _inc, int32_t* _fb, const int32_t* _fbWord,
		int32_t* _last)
	{
		// Per lane in 64-bit integers, as the machine's accumulator: the target frac(0x04000000, word), the word >> 5;
		// the amount ramped by (target - start) >> 5 a sample; the phase sum (phase << 8) + (fb s >> 23), then >> 8;
		// the lookup (T[i] << 8) + (f T[i + 1] >> 23) - (f T[i] >> 23), then >> 8
		for(int l = 0; l < Lanes; ++l)
		{
			const int32_t target = _fbWord[l] >> 5;
			int64_t phase = _phase[l] >> 8, s = _last[l], fb = _fb[l];
			const int64_t step = (static_cast<int64_t>(target) - fb) >> 5;
			for(int n = 0; n < BlockSize; ++n)
			{
				const int64_t p = (phase * 256 + ((fb * s) >> 23)) >> 8;
				const int64_t i = (p & 0xffffff) >> 16, f = (p & 0xffff) << 15;
				s = (QSine[i] * 256 + ((f * QSine[i + 1]) >> 23) - ((f * QSine[i]) >> 23)) >> 8;
				_out.s[n][l] = static_cast<float>(static_cast<double>(s) / 2147483648.0);
				phase = (phase + (_inc.s[n][l] >> 8)) & 0xffffff;
				fb += step;
			}
			_phase[l] = static_cast<uint32_t>(phase << 8);
			_last[l] = static_cast<int32_t>(s);
			_fb[l] = target;
		}
	}

	void pmSine(Block& _out, const Block* _src, uint32_t* _phase, const Increments& _inc, const float _depth)
	{
		const V depth = _mm_set1_ps(_depth);
		for(int g = 0; g < Lanes; g += 4)
		{
			__m128i p = _mm_load_si128(reinterpret_cast<const __m128i*>(_phase + g));
			for(int n = 0; n < BlockSize; ++n)
			{
				V u = phaseOf(p);
				if(_src)
					u = _mm_add_ps(u, _mm_mul_ps(depth, _mm_load_ps(_src->s[n] + g)));
				_mm_store_ps(_out.s[n] + g, sin2pi(u));
				p = _mm_add_epi32(p, _mm_load_si128(reinterpret_cast<const __m128i*>(_inc.s[n] + g)));
			}
			_mm_store_si128(reinterpret_cast<__m128i*>(_phase + g), p);
		}
	}

	void pmWave(Block& _out, const Block* _src, const float* _from, const float* _to, uint32_t* _phase,
		const Increments& _inc, const float* _wave)
	{
		for(int g = 0; g < Lanes; g += 4)
		{
			__m128i p = _mm_load_si128(reinterpret_cast<const __m128i*>(_phase + g));
			V depth = _mm_load_ps(_from + g);
			const V step = stepOf(_from, _to, g);
			for(int n = 0; n < BlockSize; ++n)
			{
				V u = phaseOf(p);
				if(_src)
					u = _mm_add_ps(u, _mm_mul_ps(depth, _mm_load_ps(_src->s[n] + g)));
				_mm_store_ps(_out.s[n] + g, lookup(u, _wave));
				p = _mm_add_epi32(p, _mm_load_si128(reinterpret_cast<const __m128i*>(_inc.s[n] + g)));
				depth = _mm_add_ps(depth, step);
			}
			_mm_store_si128(reinterpret_cast<__m128i*>(_phase + g), p);
		}
	}

	void driveClip(Block& _buf, const float* _from, const float* _to, const float* _drive, const float* _post)
	{
		for(int g = 0; g < Lanes; g += 4)
		{
			const V post = _mm_load_ps(_post + g), drive = _mm_load_ps(_drive + g);
			V v = _mm_mul_ps(ramp(_from, g), post);
			const V s = _mm_mul_ps(stepOf(_from, _to, g), post);
			for(int n = 0; n < BlockSize; ++n)
			{
				_mm_store_ps(_buf.s[n] + g, _mm_mul_ps(v, clip(_mm_mul_ps(drive, _mm_load_ps(_buf.s[n] + g)))));
				v = _mm_add_ps(v, s);
			}
		}
	}
}
