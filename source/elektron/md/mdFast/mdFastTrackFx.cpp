#include "mdFastTrackFx.h"

#include "mdFastDsp.h"
#include "mdFastTables.h"

#include <algorithm>
#include <emmintrin.h>

namespace md::fast
{
	namespace
	{
		using dsp::acc;
		using dsp::lim;
		using dsp::mpy;
		using V = __m128;
		using D = __m128d;

		constexpr double LastWord = 8388607.0 / 8388608;	// full scale, the limiter's top
		constexpr double Lsb = 1.0 / 8388608;				// a word's

		// ---- the DSP's division, in its coefficient code ----

		// The shift clb gives: 0 for a normalised word, negative to shift left
		int clb(const int64_t _a)
		{
			if(_a == 0)
				return 0;
			const auto shifted = static_cast<uint64_t>(_a) << 8;
			auto v = (static_cast<int64_t>(shifted) < 0 ? ~shifted : shifted) | 0xff;
			int top = 63;
			while(!(v & (uint64_t(1) << top)))
				--top;
			return top - 54;
		}

		int64_t shifted(const int64_t _a, const int _clb)
		{
			return _clb >= 0 ? _a >> _clb : static_cast<int64_t>(static_cast<uint64_t>(_a) << -_clb);
		}

		// The DSP's 24 steps of non-restoring division, the quotient's word doubled
		int32_t divide(int64_t _d, const int32_t _s)
		{
			bool carry = false;
			for(int i = 0; i < 24; ++i)
			{
				const bool change = (_d < 0) != (_s < 0);
				const auto doubled = static_cast<int64_t>((static_cast<uint64_t>(_d) << 1) | (carry ? 1u : 0u));
				_d = change ? doubled + acc(_s) : doubled - acc(_s);
				carry = _d >= 0;
			}
			return lim(acc(dsp::sx(static_cast<uint32_t>(_d))) * 2);
		}

		// ---- the recursions, the DSP's own, in doubles ----

		// A sample, a coefficient and a word are 24 bits over 2^23, an accumulator 48 bits over 2^47: a double holds
		// their products and sums exactly, and the floor a word takes at the store is the DSP's. So these stages equal
		// TrackFx bit for bit on the DSP's words. The floor is why: the DSP's 24-bit feedback words add noise of their
		// own below 20 Hz, up to -94 dBFS of a stage's output, and an edge at 5 Hz is the machine's default. Float on
		// first differences stays 150 dB from the filter without that noise, 94 from the DSP: Q3 fails wherever the
		// sound is quiet.
		//
		// Exact takes every partial sum under 64, 2^53 of an accumulator's LSB. A block starts it inside the rails, and
		// at a rail the poles' words give c + r = -(what they leave of a DC) <= 0, which pulls it back, while what the
		// zeros add over a block telescopes. So it stays near the rails: the largest sum of the test's rails, jumps and
		// ramps is 10.9. That is measured, not a bound (32 samples of 3 would be 96); past 64 only low bits of an
		// accumulator far past its rail would go. Each vector holds 2 Tracks

		// To the word below: rounding to the grid of 2^-23 that the sum with a huge constant leaves, half a grid lower
		D floorWord(const D _a)
		{
			const D huge = _mm_set1_pd(3.0 * (1 << 28));
			const D half = _mm_set1_pd(1.0 / (1 << 24) - 1.0 / (uint64_t(1) << 48));
			return _mm_sub_pd(_mm_add_pd(_mm_sub_pd(_a, half), huge), huge);
		}

		// The limiter, on a word
		D limit(const D _word) { return _mm_min_pd(_mm_max_pd(_word, _mm_set1_pd(-1.0)), _mm_set1_pd(LastWord)); }

		// What the DSP saves of an accumulator when it stores both its words through the limiter, the filter's
		D limitedWhole(const D _a)
		{
			return _mm_min_pd(_mm_max_pd(_a, _mm_set1_pd(-1.0)), _mm_set1_pd(1.0 - 1.0 / (uint64_t(1) << 47)));
		}

		// And when the limiter sees its high word alone, the low word kept as it is: the EQ's and DIST's
		D limitedHigh(const D _a, const D _word, const D _limited)
		{
			return _mm_add_pd(_limited, _mm_sub_pd(_a, _word));
		}

		// y[n] = limited a[n], a[n] = a[n-1] + input + c y[n-1] + r y[n-2]. The feedback from y[n-1] comes last: it is
		// what the next sample waits for, 17 cycles a sample rather than 26
		struct Poles
		{
			D a, y1, y2, word;

			Poles(const double* _a, const double* _y1, const double* _y2)
				: a(_mm_load_pd(_a)), y1(_mm_load_pd(_y1)), y2(_mm_load_pd(_y2)), word(y1)
			{
			}

			D next(const D _input, const D _c, const D _r)
			{
				a = _mm_add_pd(_mm_add_pd(_mm_add_pd(a, _mm_mul_pd(_r, y2)), _input), _mm_mul_pd(_c, y1));
				y2 = y1;
				word = floorWord(a);
				y1 = limit(word);
				return y1;
			}

			void keep(const D _a, double* _aOut, double* _y1, double* _y2) const
			{
				_mm_store_pd(_aOut, _a);
				_mm_store_pd(_y1, y1);
				_mm_store_pd(_y2, y2);
			}
		};

		// A numerator's two past inputs
		struct Zeros
		{
			D x1, x2;

			Zeros(const double* _x1, const double* _x2) : x1(_mm_load_pd(_x1)), x2(_mm_load_pd(_x2)) {}

			D next(const D _x, const D _h, const D _e, const D _k)
			{
				const D sum = _mm_add_pd(_mm_add_pd(_mm_mul_pd(_h, _x), _mm_mul_pd(_e, x1)), _mm_mul_pd(_k, x2));
				x2 = x1;
				x1 = _x;
				return sum;
			}

			// _h on both ends, one product less
			D next(const D _x, const D _h, const D _e)
			{
				const D sum = _mm_add_pd(_mm_mul_pd(_h, _mm_add_pd(_x, x2)), _mm_mul_pd(_e, x1));
				x2 = x1;
				x1 = _x;
				return sum;
			}

			void keep(double* _x1, double* _x2) const
			{
				_mm_store_pd(_x1, x1);
				_mm_store_pd(_x2, x2);
			}
		};

		// A Block's two Tracks, and back
		void load(const Block& _buf, const int _l, D* _x)
		{
			for(int n = 0; n < BlockSize; ++n)
			{
				const auto two = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(&_buf.s[n][_l]));
				_x[n] = _mm_cvtps_pd(_mm_castsi128_ps(two));
			}
		}

		void store(Block& _buf, const int _l, const D* _x)
		{
			for(int n = 0; n < BlockSize; ++n)
				_mm_storel_epi64(reinterpret_cast<__m128i*>(&_buf.s[n][_l]), _mm_castps_si128(_mm_cvtpd_ps(_x[n])));
		}

		// A sample's share of a ramp, n^2 / 1024: the running sum of X:$648
		struct RampShare
		{
			alignas(16) double s[BlockSize][2];

			RampShare()
			{
				for(int n = 0; n < BlockSize; ++n)
					s[n][0] = s[n][1] = n * n / 1024.0;
			}
		};

		const RampShare Share;

		// A word over a block: floor(from + move share), the move limited, so that from one end of the range to the
		// other the ramp stops half way and the word jumps at the next block. _times: how often the recursion reads it
		void ramp(D* _out, const double* _from, const double* _to, const double _times = 1)
		{
			const D from = _mm_load_pd(_from), move = limit(_mm_sub_pd(_mm_load_pd(_to), from));
			const D times = _mm_set1_pd(_times);
			for(int n = 0; n < BlockSize; ++n)
			{
				const D at = _mm_add_pd(from, _mm_mul_pd(move, _mm_load_pd(Share.s[n])));
				_out[n] = _mm_mul_pd(limit(floorWord(at)), times);
			}
		}

		// The words of the filter's sections, over a block or the one they keep, which each pair of sections shares
		template<int N>
		struct SectionWords
		{
			static constexpr int Mask = N - 1;
			D lowC[N], lowR[N], lowH[N], highC[N], highR[N], highH[N], highE[N];
		};

		// The EQ of 2 Tracks
		struct Biquad
		{
			Poles poles;
			Zeros zeros;
			D c, r, h, e, k;

			D next(const D _x) { return poles.next(zeros.next(_x, h, e, k), c, r); }
		};

		// DIST of 2 Tracks
		struct Clipper
		{
			D gain, out;	// the gain, the output's
			D a, y1, c1, word;

			D next(const D _x)
			{
				// The shift in the 56-bit accumulator
				const D p = _mm_mul_pd(_mm_mul_pd(gain, _x), _mm_set1_pd(512.0));
				const D wraps = _mm_or_pd(_mm_cmpge_pd(p, _mm_set1_pd(256.0)), _mm_cmplt_pd(p, _mm_set1_pd(-256.0)));
				const D turn = _mm_or_pd(_mm_and_pd(p, _mm_set1_pd(-0.0)), _mm_set1_pd(512.0));
				const D c = limit(floorWord(_mm_sub_pd(p, _mm_and_pd(wraps, turn))));
				const D moved = _mm_add_pd(a, _mm_mul_pd(out, _mm_sub_pd(c, c1)));
				a = _mm_add_pd(moved, _mm_mul_pd(_mm_set1_pd(-0x4189 * Lsb), y1));
				c1 = c;
				word = floorWord(a);
				y1 = limit(word);
				return y1;
			}
		};
	}

	// ---- EQ ----

	Eq::Eq()
	{
		for(int l = 0; l < Lanes; ++l)
		{
			m_eqf[l] = m_eqg[l] = -1;
			set(l, 0x2000, 0x2000);
		}
	}

	void Eq::set(const int _lane, const int _eqf, const int _eqg)
	{
		if(_eqf == m_eqf[_lane] && _eqg == m_eqg[_lane])
			return;
		m_eqf[_lane] = _eqf;
		m_eqg[_lane] = _eqg;

		using namespace tables;
		const auto& t = tables::tables();
		const int f = _eqf >> 3, g = _eqg >> 7;

		// The poles' radius and cosine, the zeros' the same; c0 = -r^2, c1 = rc - 1/2
		const int32_t cosP = t.eqCos[f + t.eqPoleCos[g] + EqCosBase], radP = t.eqExp[f + t.eqPoleExp[g] + EqExpBase];
		const int32_t cosZ = t.eqCos[f + t.eqZeroCos[g] + EqCosBase], radZ = t.eqExp[f + t.eqZeroExp[g] + EqExpBase];
		const int64_t rc = mpy(radP, cosP) - acc(0x400000), rr = -mpy(radP, radP);
		const int32_t numerator = lim(-((rr >> 1) + rc));

		const int64_t rz = -mpy(radZ, cosZ), zz = mpy(radZ, radZ) >> 1;
		const int64_t denominator = zz + acc(0x400000) + rz;
		const int shift = clb(denominator);
		const int32_t q = divide(shifted(acc(numerator), shift) >> 3, lim(shifted(denominator, shift)));

		// a[n] = a[n-1] + 2 c1 y[n-1] + c0 y[n-2] + the zeros'
		m_c[_lane] = 2 * lim(rc) * Lsb;
		m_r[_lane] = lim(rr) * Lsb;
		m_h[_lane] = lim(mpy(q, 0x400000)) * Lsb;
		m_e[_lane] = lim(mpy(q, lim(rz))) * Lsb;
		m_k[_lane] = lim(mpy(q, lim(zz))) * Lsb;
	}

	void Eq::process(Block& _buf)
	{
		const auto biquad = [this](const int _l)
		{
			return Biquad{{m_a + _l, m_y1 + _l, m_y2 + _l}, {m_x1 + _l, m_x2 + _l}, _mm_load_pd(m_c + _l),
				_mm_load_pd(m_r + _l), _mm_load_pd(m_h + _l), _mm_load_pd(m_e + _l), _mm_load_pd(m_k + _l)};
		};
		const auto keep = [this](const Biquad& _b, const int _l)
		{
			_b.poles.keep(limitedHigh(_b.poles.a, _b.poles.word, _b.poles.y1), m_a + _l, m_y1 + _l, m_y2 + _l);
			_b.zeros.keep(m_x1 + _l, m_x2 + _l);
		};
		for(int l = 0; l < _buf.lanes; l += 4)
		{
			D x[BlockSize], y[BlockSize];
			load(_buf, l, x);
			load(_buf, l + 2, y);
			Biquad first = biquad(l), second = biquad(l + 2);
			for(int n = 0; n < BlockSize; ++n)
			{
				x[n] = first.next(x[n]);
				y[n] = second.next(y[n]);
			}
			keep(first, l);
			keep(second, l + 2);
			store(_buf, l, x);
			store(_buf, l + 2, y);
		}
	}

	// ---- filter ----

	Filter::Filter()
	{
		for(int l = 0; l < Lanes; ++l)
		{
			m_fltf[l] = m_fltw[l] = m_fltq[l] = -1;
			set(l, 0, 0x3fff, 0);
		}
	}

	void Filter::set(const int _lane, const int _fltf, const int _fltw, const int _fltq)
	{
		if(_fltf == m_fltf[_lane] && _fltw == m_fltw[_lane] && _fltq == m_fltq[_lane])
			return;
		m_fltf[_lane] = _fltf;
		m_fltw[_lane] = _fltw;
		m_fltq[_lane] = _fltq;

		using namespace tables;
		const auto& t = tables::tables();

		// P:$a4-$ba: a low edge at 0 has no resonance, nor a high edge at the top of FLTW
		const int q = _fltq >> 7;
		const int highEdge = _fltf >> 3, highQ = _fltf ? q : 0;
		const int lowEdge = std::min((_fltf + _fltw) >> 3, 0x7ff), lowQ = (_fltw >> 3) >= 0x7ef ? 0 : q;

		const auto cosine = [&](const int _edge, const int _q)
		{
			return t.cutoffCos[_edge + t.resonanceCos[_q] + CutoffCosBase];
		};
		const auto radius = [&](const int _edge, const int _q)
		{
			return t.cutoffExp[_edge + t.resonanceExp[_q] + CutoffExpBase];
		};

		// The words ramp to these; the low-pass's h and the high-pass's c before the limiter
		{
			const int32_t c = cosine(lowEdge, lowQ), r = radius(lowEdge, lowQ);
			const int64_t rc = mpy(r, c) * 2 + acc(-0x800000);
			const int32_t rr = lim(-mpy(r, r));
			m_to[LowC][_lane] = lim(rc) * Lsb;
			m_to[LowR][_lane] = rr * Lsb;
			m_to[LowH][_lane] = static_cast<double>((-(rc + acc(rr))) >> 24) * Lsb;
		}
		{
			const int32_t c = cosine(highEdge, highQ), r = radius(highEdge, highQ);
			const int64_t rc = mpy(r, c), rr = -mpy(r, r);
			m_to[HighC][_lane] = (lim(rc) - 0x400000) * Lsb;
			m_to[HighR][_lane] = lim(rr) * Lsb;
			m_to[HighH][_lane] = lim((rc * 2 - rr - acc(-0x800000)) >> 3) * Lsb;
		}
		m_moving[_lane / 2] = true;
	}

	template<typename Words>
	void Filter::render(Block& _buf, const int _l, const Words& _w)
	{
		D x[BlockSize];
		load(_buf, _l, x);

		Poles low0(m_a[0] + _l, m_y1[0] + _l, m_y2[0] + _l), low1(m_a[1] + _l, m_y1[1] + _l, m_y2[1] + _l);
		Poles high0(m_a[2] + _l, m_y1[2] + _l, m_y2[2] + _l), high1(m_a[3] + _l, m_y1[3] + _l, m_y2[3] + _l);
		Zeros zeros0(m_x1[0] + _l, m_x2[0] + _l), zeros1(m_x1[1] + _l, m_x2[1] + _l);

		for(int n = 0; n < BlockSize; ++n)
		{
			const int i = n & Words::Mask;
			const D lowC = _w.lowC[i], lowR = _w.lowR[i], lowH = _w.lowH[i];
			const D highC = _w.highC[i], highR = _w.highR[i], highH = _w.highH[i], highE = _w.highE[i];

			// Each low-pass hands the next section its output 2 samples late
			const D toLow1 = low0.y2, toHigh0 = low1.y2;
			low0.next(_mm_mul_pd(lowH, x[n]), lowC, lowR);
			low1.next(_mm_mul_pd(lowH, toLow1), lowC, lowR);
			const D out = high0.next(zeros0.next(toHigh0, highH, highE), highC, highR);
			x[n] = high1.next(zeros1.next(out, highH, highE), highC, highR);
		}

		low0.keep(limitedWhole(low0.a), m_a[0] + _l, m_y1[0] + _l, m_y2[0] + _l);
		low1.keep(limitedWhole(low1.a), m_a[1] + _l, m_y1[1] + _l, m_y2[1] + _l);
		high0.keep(limitedWhole(high0.a), m_a[2] + _l, m_y1[2] + _l, m_y2[2] + _l);
		high1.keep(limitedWhole(high1.a), m_a[3] + _l, m_y1[3] + _l, m_y2[3] + _l);
		zeros0.keep(m_x1[0] + _l, m_x2[0] + _l);
		zeros1.keep(m_x1[1] + _l, m_x2[1] + _l);
		store(_buf, _l, x);
	}

	void Filter::process(Block& _buf)
	{
		const auto word = [](const double _word) { return std::clamp(_word, -1.0, LastWord); };
		for(int l = 0; l < _buf.lanes; l += 2)
		{
			if(!m_moving[l / 2])
			{
				SectionWords<1> w;
				const auto kept = [&](const int _word) { return _mm_load_pd(m_word[_word] + l); };
				w.lowC[0] = kept(LowC);
				w.lowR[0] = kept(LowR);
				w.lowH[0] = kept(LowH);
				w.highC[0] = _mm_add_pd(kept(HighC), kept(HighC));
				w.highR[0] = kept(HighR);
				w.highH[0] = kept(HighH);
				w.highE[0] = kept(HighE);
				render(_buf, l, w);
				continue;
			}

			// -2 h ramps by its own move, so rounds on its own
			for(int i = l; i < l + 2; ++i)
				m_to[HighE][i] = m_word[HighE][i] + word(-2 * (m_to[HighH][i] - m_word[HighH][i]));

			SectionWords<BlockSize> w;
			ramp(w.lowC, m_word[LowC] + l, m_to[LowC] + l);
			ramp(w.lowR, m_word[LowR] + l, m_to[LowR] + l);
			ramp(w.lowH, m_word[LowH] + l, m_to[LowH] + l);
			ramp(w.highC, m_word[HighC] + l, m_to[HighC] + l, 2);
			ramp(w.highR, m_word[HighR] + l, m_to[HighR] + l);
			ramp(w.highH, m_word[HighH] + l, m_to[HighH] + l);
			ramp(w.highE, m_word[HighE] + l, m_to[HighE] + l);
			render(_buf, l, w);

			m_moving[l / 2] = false;
			for(int i = l; i < l + 2; ++i)
			{
				for(int k = LowC; k <= HighH; ++k)
					m_word[k][i] = word(m_to[k][i]);
				m_word[HighE][i] = word(-2 * m_word[HighH][i]);
			}
		}
	}

	// ---- SRR ----

	Srr::Srr(const double _rate) : m_step(static_cast<float>(16 * 44100 / _rate))
	{
		for(int l = 0; l < Lanes; ++l)
			set(l, 0);
	}

	void Srr::set(const int _lane, const int _srr)
	{
		const int32_t x = _srr + 0x4000;
		m_period[_lane] = static_cast<float>(lim((mpy(x, x) - acc(0x20)) * 4 + acc(0x10)));
	}

	void Srr::process(Block& _buf)
	{
		const V step = _mm_set1_ps(m_step);
		for(int v = 0; v < _buf.lanes; v += 4)
		{
			const V period = _mm_load_ps(m_period + v);

			// A period of one step takes every sample, its phase put back each time: the output is the input
			if(_mm_movemask_ps(_mm_cmpeq_ps(period, step)) == 15)
			{
				_mm_store_ps(m_held + v, _mm_load_ps(&_buf.s[BlockSize - 1][v]));
				continue;
			}

			V phase = _mm_load_ps(m_phase + v), held = _mm_load_ps(m_held + v);
			for(int n = 0; n < BlockSize; ++n)
			{
				phase = _mm_add_ps(phase, step);
				const V take = _mm_cmpge_ps(phase, period);
				const V x = _mm_load_ps(&_buf.s[n][v]);
				held = _mm_or_ps(_mm_and_ps(take, x), _mm_andnot_ps(take, held));
				phase = _mm_sub_ps(phase, _mm_and_ps(take, period));
				_mm_store_ps(&_buf.s[n][v], held);
			}
			_mm_store_ps(m_phase + v, phase);
			_mm_store_ps(m_held + v, held);
		}
	}

	// ---- DIST ----

	Dist::Dist()
	{
		for(int l = 0; l < Lanes; ++l)
			set(l, 0);
	}

	void Dist::set(const int _lane, const int _dist)
	{
		const auto& t = tables::tables();
		const int i = _dist >> 6;
		m_gain[_lane] = t.distGain[i] * Lsb;
		m_out[_lane] = lim(mpy(t.distFilter[i], 0x7fdf3b)) * Lsb;
	}

	void Dist::process(Block& _buf)
	{
		const auto clipper = [this](const int _l)
		{
			const D out = _mm_load_pd(m_out + _l), y1 = _mm_load_pd(m_y1 + _l), c1 = _mm_load_pd(m_c1 + _l);

			// The first sample takes back the last one's clipped word at the last block's gain
			const D old = _mm_mul_pd(_mm_sub_pd(out, _mm_load_pd(m_outOld + _l)), c1);
			return Clipper{_mm_load_pd(m_gain + _l), out, _mm_add_pd(_mm_load_pd(m_a + _l), old), y1, c1, y1};
		};
		const auto keep = [this](const Clipper& _c, const int _l)
		{
			_mm_store_pd(m_a + _l, limitedHigh(_c.a, _c.word, _c.y1));
			_mm_store_pd(m_y1 + _l, _c.y1);
			_mm_store_pd(m_c1 + _l, _c.c1);
			_mm_store_pd(m_outOld + _l, _c.out);
		};
		for(int l = 0; l < _buf.lanes; l += 4)
		{
			D x[BlockSize], y[BlockSize];
			load(_buf, l, x);
			load(_buf, l + 2, y);
			Clipper first = clipper(l), second = clipper(l + 2);
			for(int n = 0; n < BlockSize; ++n)
			{
				x[n] = first.next(x[n]);
				y[n] = second.next(y[n]);
			}
			keep(first, l);
			keep(second, l + 2);
			store(_buf, l, x);
			store(_buf, l + 2, y);
		}
	}
}
