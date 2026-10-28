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

	void onePoleLanes(Block& _buf, const float* _b, const float* _a, float* _x1, float* _y1)
	{
		for(int g = 0; g < Lanes; g += 4)
		{
			const V b = _mm_load_ps(_b + g), a = _mm_load_ps(_a + g);
			V x1 = _mm_load_ps(_x1 + g), y1 = _mm_load_ps(_y1 + g);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V x = _mm_load_ps(_buf.s[n] + g);
				y1 = _mm_add_ps(_mm_mul_ps(b, _mm_add_ps(x, x1)), _mm_mul_ps(a, y1));
				x1 = x;
				_mm_store_ps(_buf.s[n] + g, y1);
			}
			_mm_store_ps(_x1 + g, x1);
			_mm_store_ps(_y1 + g, y1);
		}
	}

	void biquad(Block& _buf, const float* _c, float (*_s)[Lanes])
	{
		const V b0 = _mm_set1_ps(_c[0]), b1 = _mm_set1_ps(_c[1]), b2 = _mm_set1_ps(_c[2]);
		const V a1 = _mm_set1_ps(_c[3]), a2 = _mm_set1_ps(_c[4]);
		for(int g = 0; g < Lanes; g += 4)
		{
			V x1 = _mm_load_ps(_s[0] + g), x2 = _mm_load_ps(_s[1] + g);
			V y1 = _mm_load_ps(_s[2] + g), y2 = _mm_load_ps(_s[3] + g);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V x = _mm_load_ps(_buf.s[n] + g);
				const V f = _mm_add_ps(_mm_add_ps(_mm_mul_ps(b0, x), _mm_mul_ps(b1, x1)), _mm_mul_ps(b2, x2));
				const V y = clip(_mm_sub_ps(f, _mm_add_ps(_mm_mul_ps(a1, y1), _mm_mul_ps(a2, y2))));
				x2 = x1;
				x1 = x;
				y2 = y1;
				y1 = y;
				_mm_store_ps(_buf.s[n] + g, y);
			}
			_mm_store_ps(_s[0] + g, x1);
			_mm_store_ps(_s[1] + g, x2);
			_mm_store_ps(_s[2] + g, y1);
			_mm_store_ps(_s[3] + g, y2);
		}
	}

	void mix4(const Block* const (&_x)[4], Block& _out, const float (*_from)[Lanes], const float (*_to)[Lanes])
	{
		for(int g = 0; g < Lanes; g += 4)
		{
			V gain[4], step[4];
			for(int i = 0; i < 4; ++i)
			{
				gain[i] = ramp(_from[i], g);
				step[i] = stepOf(_from[i], _to[i], g);
			}
			for(int n = 0; n < BlockSize; ++n)
			{
				V acc = _mm_mul_ps(gain[0], _mm_load_ps(_x[0]->s[n] + g));
				for(int i = 1; i < 4; ++i)
					acc = _mm_add_ps(acc, _mm_mul_ps(gain[i], _mm_load_ps(_x[i]->s[n] + g)));
				_mm_store_ps(_out.s[n] + g, acc);
				for(int i = 0; i < 4; ++i)
					gain[i] = _mm_add_ps(gain[i], step[i]);
			}
		}
	}

	void gatedNoise(Block& _out, uint32_t (*_s)[Lanes], const int32_t _gate)
	{
		const __m128i gate = _mm_set1_epi32(_gate);
		const V scale = _mm_set1_ps(1.0f / 2147483648.0f);
		for(int g = 0; g < Lanes; g += 4)
		{
			__m128i s[4];
			for(int k = 0; k < 4; ++k)
				s[k] = _mm_load_si128(reinterpret_cast<const __m128i*>(_s[k] + g));
			for(int n = 0; n < BlockSize; ++n)
			{
				const __m128i d1 = _mm_add_epi32(s[1], s[0]);
				s[1] = _mm_add_epi32(s[1], d1);
				const __m128i a1 = _mm_add_epi32(s[3], s[2]);
				s[0] = d1;
				s[3] = _mm_add_epi32(s[3], a1);
				s[2] = a1;
				const __m128i kept = _mm_and_si128(d1, _mm_cmpgt_epi32(a1, gate));
				_mm_store_ps(_out.s[n] + g, _mm_mul_ps(_mm_cvtepi32_ps(kept), scale));
			}
			for(int k = 0; k < 4; ++k)
				_mm_store_si128(reinterpret_cast<__m128i*>(_s[k] + g), s[k]);
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

	void feedbackWave(Block& _out, Block& _scaled, const Block& _g, float* _last, float* _fb, const float* _fbTo,
		uint32_t* _phase, const Increments& _inc, const float* _wave)
	{
		for(int g = 0; g < Lanes; g += 4)
		{
			__m128i p = _mm_load_si128(reinterpret_cast<const __m128i*>(_phase + g));
			V fb = _mm_load_ps(_fb + g), y = _mm_load_ps(_last + g);
			const V step = stepOf(_fb, _fbTo, g);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V s = lookup(_mm_add_ps(phaseOf(p), _mm_mul_ps(fb, y)), _wave);
				y = _mm_mul_ps(s, _mm_load_ps(_g.s[n] + g));
				_mm_store_ps(_out.s[n] + g, s);
				_mm_store_ps(_scaled.s[n] + g, y);
				p = _mm_add_epi32(p, _mm_load_si128(reinterpret_cast<const __m128i*>(_inc.s[n] + g)));
				fb = _mm_add_ps(fb, step);
			}
			_mm_store_si128(reinterpret_cast<__m128i*>(_phase + g), p);
			_mm_store_ps(_last + g, y);
		}
		std::copy_n(_fbTo, Lanes, _fb);
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
