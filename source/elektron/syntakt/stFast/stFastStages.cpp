#include "stFastStages.h"

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

	void feedbackSine(Block& _out, uint32_t* _phase, const Increments& _inc, float* _fb, const float* _fbTarget,
		float* _last, const float* _wave)
	{
		for(int g = 0; g < Lanes; g += 4)
		{
			__m128i p = _mm_load_si128(reinterpret_cast<const __m128i*>(_phase + g));
			V fb = _mm_load_ps(_fb + g), s = _mm_load_ps(_last + g);
			const V step = stepOf(_fb, _fbTarget, g);
			for(int n = 0; n < BlockSize; ++n)
			{
				const V u = _mm_add_ps(phaseOf(p), _mm_mul_ps(fb, s));
				s = _wave ? lookup(u, _wave) : sin2pi(u);
				_mm_store_ps(_out.s[n] + g, s);
				p = _mm_add_epi32(p, _mm_load_si128(reinterpret_cast<const __m128i*>(_inc.s[n] + g)));
				fb = _mm_add_ps(fb, step);
			}
			_mm_store_si128(reinterpret_cast<__m128i*>(_phase + g), p);
			_mm_store_ps(_last + g, s);
		}
		std::copy_n(_fbTarget, Lanes, _fb);
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
