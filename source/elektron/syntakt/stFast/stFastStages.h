#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <emmintrin.h>

// What the Syntakt's machines rewritten for speed share: 32-bit float, the 16 Tracks side by side (each field an array
// of 16 lanes, a vector op on 4 of them), blocks of 32 samples at the host's rate, every rate-dependent constant
// derived from the rate so that times in seconds stay the 48 kHz machine's. The exact integer port (stEngine) is the
// reference.
namespace syntakt::fast
{
	constexpr int Lanes = 16;		// one per Track
	constexpr int BlockSize = 32;	// samples per block, at the host's rate
	constexpr float MachineRate = 48000.0f;

	// The 16 Tracks' samples of a block, sample-major: s[n] holds sample n of every Track
	struct alignas(16) Block
	{
		float s[BlockSize][Lanes];
	};

	// The stages, each over the 16 lanes. A ramp goes linearly over the block from _from to _to (per lane), as the
	// firmware's per-sample steps of (to - from) / 32 do

	// _out = gx x + gy y, both gains ramped
	void mix(const Block& _x, const Block& _y, Block& _out, const float* _gxFrom, const float* _gxTo,
		const float* _gyFrom, const float* _gyTo);

	// _buf *= g, g ramped (the modulator's and the amp's VCA)
	void rampedGain(Block& _buf, const float* _from, const float* _to);

	// First-order IIR in place, y = b0 x + b1 x1 + a1 y1, the same coefficients on every lane; _x1, _y1 per lane
	struct OnePole
	{
		float b0, b1, a1;
	};
	void onePole(Block& _buf, const OnePole& _c, float* _x1, float* _y1);

	// The oscillators. Phases are 32-bit fractions of a cycle (an integer accumulator, so that a 2 s Hit does not drift
	// from float rounding), increments the same, one per sample: a pitch the machine changes at its 48 kHz blocks'
	// boundaries changes where they fall within a host block. Phase-modulation depths are in cycles per unit of input
	struct alignas(16) Increments
	{
		uint32_t s[BlockSize][Lanes];
	};

	// A sine phase-modulated by its previous sample, the amount ramped from _fb to _fbTarget (left there). _wave: a
	// 256-entry sine (258 values) read as the machine reads its own; nullptr, the sine itself
	void feedbackSine(Block& _out, uint32_t* _phase, const Increments& _inc, float* _fb, const float* _fbTarget,
		float* _last, const float* _wave);

	// A sine phase-modulated by _src (nullptr: none)
	void pmSine(Block& _out, const Block* _src, uint32_t* _phase, const Increments& _inc, float _depth);

	// Drive into a hard clip under a ramped gain: _buf = g post clip(drive x)
	void driveClip(Block& _buf, const float* _from, const float* _to, const float* _drive, const float* _post);

	// ---- the control path ----

	inline float q31(const int32_t _v) { return static_cast<float>(static_cast<double>(_v) / 2147483648.0); }

	// 0x40004dc6: a 128-entry table read at a parameter word, linearly interpolated
	inline float interpolate(const float* _table, const int32_t _word)
	{
		const int32_t i = std::max(_word >> 8, 0);
		const float f = static_cast<float>(_word - (i << 8)) / 256.0f;
		return (1 - f) * _table[i] + f * _table[std::min(i + 1, 127)];
	}

	// A first-order bilinear filter's pole at 48 kHz moved to _rate at the same cutoff, its warped angle held below
	// pi/2 (a cutoff past the rate's Nyquist), where the pole would leave the unit circle
	inline float bilinearPole(const float _pole48, const float _rate)
	{
		const double k48 = (1.0 - _pole48) / (1.0 + _pole48);
		const double k = std::tan(std::min(std::atan(k48) * MachineRate / _rate, 0.49 * 3.14159265358979323846));
		return static_cast<float>((1 - k) / (1 + k));
	}

	// ---- the pitch, as the machine computes it (0x40002596): in integers, so that a long Hit keeps its phase ----

	// The pitch map's word for the note, the TUNE word (value << 8 | fine) and the machine's offset to the note in
	// semitones (its 0x40): the pitch clamped to notes 0-127 (Q16), octaves from A4 by its 1/12 (0x0aaaaaab), their
	// fraction's top 11 bits indexing its exp2 table (_exp2: 2048 words)
	inline int32_t pitchWord(const int32_t* _exp2, const int _note, const int32_t _tune, const int _offset)
	{
		const int64_t p = std::clamp<int64_t>((_note - _offset) * int64_t(65536) + 256 * _tune, 0, 127 * 65536);
		const int64_t q = ((p - 69 * 65536) * 0x0aaaaaab) >> 31;
		const int octave = static_cast<int16_t>(q >> 16);
		const int64_t f = (int64_t(0x6e000000) * _exp2[(q & 0xffff) >> 5]) >> 31;
		const int shift = 5 - octave;
		return shift >= 32 ? 0 : static_cast<int32_t>(f >> shift);
	}

	// The sweep's factor word: 0x04000000 plus the depth word's excess under the sweep's level _sweep (a fraction, from
	// INT32_MAX / 2^31 at the trig), rounded down
	inline int64_t sweepWord(const double _depth, const double _sweep)
	{
		return static_cast<int64_t>(std::floor((_depth - 67108864.0) * _sweep)) + 67108864;
	}

	// A unit's increment in 2^-24 cycles per 48 kHz sample: its factor (0x50) under the sweep's word _m, its ratio
	// (0x64, 0x40000000 the note's), at the pitch word, each product truncated as the machine's, at most 32 times the
	// note's
	inline double increment48(const int64_t _m, const int64_t _factor, const int64_t _scale, const int64_t _pitch)
	{
		const int64_t t2 = (((_factor >> 6) * _m >> 31) * _scale) >> 31;
		const auto t3 = static_cast<int32_t>(static_cast<uint32_t>(t2) << 11);
		const int64_t t4 = std::clamp<int64_t>(2 * int64_t(t3), INT32_MIN, INT32_MAX);
		return static_cast<double>(((t4 * _pitch >> 31) * 0x57619f10 >> 31) >> 2);
	}

	// A block-rate envelope as the machine steps it, once per 48 kHz block: its levels at the ends of 48 kHz blocks
	// k - 1 and k, between which its ramp runs over block k
	struct Ramp48
	{
		float prev[Lanes]{}, level[Lanes]{};

		void step(const int _l, const float _v)
		{
			prev[_l] = level[_l];
			level[_l] = _v;
		}

		// The ramp _t of the way through block k
		float at(const int _l, const double _t) const
		{
			return prev[_l] + static_cast<float>(_t) * (level[_l] - prev[_l]);
		}
	};

	// 0x400047ea's gain envelope, one 48 kHz block: a first-order filter on a step, falling from 1 toward 0 until 0.5
	// (_stage 1, pole _fall), then recovering toward 1 (_stage 5, pole _rise); 1 where the drive is off. _x: its input
	// at the previous block, _level its output
	inline float driveGain(int& _stage, float& _x, const float _level, const bool _on, const float _fall,
		const float _rise)
	{
		const bool falling = _stage == 1;
		const float pole = falling ? _fall : _rise;
		const float x = falling ? 0.0f : 1.0f;
		float y = (1 - pole) / 2 * (x + _x) + pole * _level;
		if(y <= 0.5f)
		{
			_stage = 5;
			y = 0.5f;
		}
		if(!_on)
			y = 1;
		_x = x;
		return y;
	}

	// The units' increments over a host block, on the machine's 48 kHz block clock. The host block covers _at to _at
	// + _r in 48 kHz blocks since the trig; the machine steps its sweep (_sweep *= _decay) at each 48 kHz block's edge
	// (_sweepBlock, the block it is in) and _machine(_sweep, double* _inc48) then gives the _Units' increments there,
	// in 2^-24 cycles per 48 kHz sample. Each host sample adds the phase the machine adds over the same time: its
	// block's increment, or two blocks' weighted where the sample straddles their boundary. At 48 kHz, the machine's
	template<int Units, typename Machine>
	void splitIncrements(const int _l, const double _at, const double _r, int64_t& _sweepBlock, double& _sweep,
		const double _decay, Machine&& _machine, Increments* const* _out)
	{
		const double end = _at + _r, step = _r / BlockSize;
		const auto host = [_r](const double _inc48)
		{
			return static_cast<uint32_t>(std::llround(_inc48 * 256 * _r));
		};
		double inc[Units];
		uint32_t whole[Units];
		_machine(_sweep, inc);
		for(int u = 0; u < Units; ++u)
			whole[u] = host(inc[u]);	// a sample within one machine block
		double edge = static_cast<double>(_sweepBlock + 1);
		for(int n = 0; n < BlockSize; ++n)
		{
			// The samples wholly inside this machine block, those that end before its edge, take its increment; the
			// one that straddles the edge, and the block's last (which ends exactly at end), are weighted
			const int before = static_cast<int>(std::ceil((edge - _at) / step)) - 1;
			const int last = std::clamp(before, n, BlockSize - 1);
			for(; n < last; ++n)
				for(int u = 0; u < Units; ++u)
					_out[u]->s[n][_l] = whole[u];
			const double from = _at + n * step, to = n == BlockSize - 1 ? end : _at + (n + 1) * step;
			double p[Units]{};	// in 2^-24 cycles
			for(double at = from;;)
			{
				const double until = std::min(to, edge);
				for(int u = 0; u < Units; ++u)
					p[u] += inc[u] * (until - at) * BlockSize;
				if(to < edge)
					break;
				++_sweepBlock;
				_sweep *= _decay;
				_machine(_sweep, inc);
				at = edge;
				edge += 1;
				if(to == at)
					break;
			}
			for(int u = 0; u < Units; ++u)
			{
				whole[u] = host(inc[u]);
				_out[u]->s[n][_l] = static_cast<uint32_t>(std::llround(p[u] * 256.0));
			}
		}
	}

	// RAII: denormals flushed to zero, in and out, while a block renders
	struct FlushDenormals
	{
		unsigned int csr = _mm_getcsr();
		FlushDenormals() { _mm_setcsr(csr | 0x8040); }
		~FlushDenormals() { _mm_setcsr(csr); }
	};

	// ---- the vector helpers the stages are made of ----
	namespace simd
	{
		using V = __m128;

		constexpr float Pi = 3.14159265358979f;

		inline V ramp(const float* _v, const int _g) { return _mm_load_ps(_v + _g); }

		inline V stepOf(const float* _from, const float* _to, const int _g)
		{
			const V d = _mm_sub_ps(_mm_load_ps(_to + _g), _mm_load_ps(_from + _g));
			return _mm_mul_ps(d, _mm_set1_ps(1.0f / BlockSize));
		}

		// A 32-bit phase as a fraction of a cycle in [0, 1), to 24 bits
		inline V phaseOf(const __m128i _p)
		{
			return _mm_mul_ps(_mm_cvtepi32_ps(_mm_srli_epi32(_p, 8)), _mm_set1_ps(1.0f / 16777216.0f));
		}

		inline V floorOf(const V _x)
		{
			const V t = _mm_cvtepi32_ps(_mm_cvttps_epi32(_x));
			return _mm_sub_ps(t, _mm_and_ps(_mm_cmplt_ps(_x, t), _mm_set1_ps(1.0f)));
		}

		// sin(2 pi _u): _u reduced to [-1/2, 1/2], folded to [-1/4, 1/4], then the odd Taylor series to x^11 (error
		// below 1e-7 on [-pi/2, pi/2]). The firmware reads a 256-entry sine linearly interpolated (error ~8e-5)
		inline V sin2pi(const V _u)
		{
			const V sign = _mm_set1_ps(-0.0f);
			V r = _mm_sub_ps(_u, _mm_cvtepi32_ps(_mm_cvtps_epi32(_u)));
			const V folded = _mm_sub_ps(_mm_or_ps(_mm_set1_ps(0.5f), _mm_and_ps(r, sign)), r);
			const V big = _mm_cmpgt_ps(_mm_andnot_ps(sign, r), _mm_set1_ps(0.25f));
			r = _mm_or_ps(_mm_and_ps(big, folded), _mm_andnot_ps(big, r));
			const V x = _mm_mul_ps(r, _mm_set1_ps(2 * Pi));
			const V x2 = _mm_mul_ps(x, x);
			V p = _mm_set1_ps(-1.0f / 39916800.0f);
			p = _mm_add_ps(_mm_mul_ps(p, x2), _mm_set1_ps(1.0f / 362880.0f));
			p = _mm_add_ps(_mm_mul_ps(p, x2), _mm_set1_ps(-1.0f / 5040.0f));
			p = _mm_add_ps(_mm_mul_ps(p, x2), _mm_set1_ps(1.0f / 120.0f));
			p = _mm_add_ps(_mm_mul_ps(p, x2), _mm_set1_ps(-1.0f / 6.0f));
			p = _mm_add_ps(_mm_mul_ps(p, x2), _mm_set1_ps(1.0f));
			return _mm_mul_ps(p, x);
		}

		// A 256-entry wave at _u cycles (258 values: 257 and a copy of the last), linearly interpolated as the machine
		// reads its waves
		inline V lookup(const V _u, const float* _wave)
		{
			const V u = _mm_mul_ps(_mm_sub_ps(_u, floorOf(_u)), _mm_set1_ps(256.0f));
			const __m128i k = _mm_cvttps_epi32(u);
			const V f = _mm_sub_ps(u, _mm_cvtepi32_ps(k));
			alignas(16) int32_t i[4];
			alignas(16) float a[4], b[4];
			_mm_store_si128(reinterpret_cast<__m128i*>(i), k);
			for(int l = 0; l < 4; ++l)	// a gather: SSE2 has none
			{
				a[l] = _wave[i[l]];
				b[l] = _wave[i[l] + 1];
			}
			const V va = _mm_load_ps(a);
			return _mm_add_ps(va, _mm_mul_ps(f, _mm_sub_ps(_mm_load_ps(b), va)));
		}

		inline V clip(const V _x)
		{
			return _mm_max_ps(_mm_min_ps(_x, _mm_set1_ps(1.0f)), _mm_set1_ps(-1.0f));
		}
	}
}
