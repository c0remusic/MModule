#include "edDrumsDt2Reverb.h"

#include <algorithm>
#include <cmath>

#include "edDrumsDt2Stages.h"

namespace elektron::drums
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;
		constexpr double DtRate = 48000;

		// The firmware's lengths at 48 kHz (tables 0x2c79f0 and 0x2c7a30), lines 3 and 7 read on the oscillator, and
		// the lengths the gain tables were made for, less the real ones (2539, 7001, 6701)
		constexpr int AllpassLengths[8]{53, 73, 113, 163, 251, 367, 491, 809};
		constexpr int LineLengths[8]{2399, 3359, 4721, 7004, 2111, 3109, 4507, 6704};
		constexpr int GainLengthDelta[8]{140, 0, 0, -3, 0, 0, 0, -3};

		// The fitted laws, against the DT2's tables at 48 kHz (dt2-reverb's reverb_formula.py, fit_formulas.py):
		// T60(DEC) in seconds, T0 Q1^d to d = 116, then T1 Q2^(d - 117); 127 freezes (T60 within 0.002 %)
		constexpr double T60T0 = 0.459375, T60Q1 = 1.025916, T60T1 = 9.05349, T60Q2 = 1.197987;
		constexpr int T60Break = 116;
		// The allpasses' scale for DEC 0-63, 0.625 + 0.025 (Q1^d - 1), at most the code's 0.715 (within 1.8e-7)
		constexpr double ScaleBase = 0.625, ScaleSpan = 0.025;
		// |K| = exp(-7.2 L / rate): 1.5e-4 a sample at 48 kHz (within 5e-5)
		constexpr double AllpassDecay = 7.2;
		// FREQ's pole k: an analog frequency exp(c3 k^3 + c2 k^2 + c1 k + c0) Hz, 21.5 kHz down to 4.3 kHz
		constexpr double FreqPoly[4]{-1.7448241e-07, 3.3638095e-05, -1.4026192e-02, 9.9660443e+00};

		// The code's words (its listings' bit patterns, each the float32 of the decimal written)
		constexpr float PreMin48 = 37, PreMax = 16384;
		constexpr double LfoStep48 = 8.50847937e-05 / 32, LfoDepth48 = 8;
		constexpr float AllpassHigh = 0.715f;	// the allpasses' scale from DEC 64 on

		double t60(const int _dec)
		{
			return _dec <= T60Break ? T60T0 * std::pow(T60Q1, _dec) : T60T1 * std::pow(T60Q2, _dec - T60Break - 1);
		}

		bool prime(const int _n)
		{
			if(_n < 2)
				return false;
			for(int q = 2; q * q <= _n; ++q)
				if(_n % q == 0)
					return false;
			return true;
		}

		// The prime nearest _x, the lower one at a tie
		int nearestPrime(const double _x)
		{
			const auto n = static_cast<int>(std::lround(_x));
			for(int step = 0;; ++step)
			{
				if(prime(n - step))
					return n - step;
				if(prime(n + step))
					return n + step;
			}
		}

		float clamped(const float _x, const float _hi)
		{
			return std::min(std::max(_x, 0.0f), _hi);
		}
	}

	Dt2Reverb::Dt2Reverb(const double _rate)
	{
		// The lengths x rate / 48 kHz, the nearest prime for the static ones (as the DT2's are prime), the two read on
		// the oscillator rounded; the same at 48 kHz
		const double k = _rate / DtRate;
		for(int i = 0; i < Stages; ++i)
		{
			const int ap = nearestPrime(AllpassLengths[i] * k);
			const bool modulated = i == 3 || i == 7;
			const double scaled = LineLengths[i] * k;
			const int line = modulated ? static_cast<int>(std::lround(scaled)) : nearestPrime(scaled);
			const auto s = static_cast<size_t>(i);
			m_allpasses[s].delay = ap;
			m_lines[s].delay = line;
			m_lineLengths[s] = static_cast<float>(line);
			m_allpassK[s] = static_cast<float>((i & 1 ? -1 : 1) * std::exp(-AllpassDecay * ap / _rate));
			// The decay in dB/s the same at every rate; DEC 127 holds (0.5, the table's)
			const double length = line + GainLengthDelta[i] * k;
			auto& gains = m_lineGains[s];
			for(int d = 0; d < 127; ++d)
				gains[static_cast<size_t>(d)] =
					static_cast<float>(0.5 * std::pow(10.0, -3 * length / (_rate * t60(d))));
			gains[127] = 0.5f;
		}
		for(int d = 0; d < 64; ++d)
			m_allpassScale[static_cast<size_t>(d)] =
				static_cast<float>(std::min(ScaleBase + ScaleSpan * (std::pow(T60Q1, d) - 1), 0.715));
		// The shelf b0 = p + (1 - p) G, b1 = p - (1 - p) G, a1 = 2p - 1 is the bilinear transform without prewarp of an
		// analog shelf, pole w = 2 x 48 kHz p / (1 - p): the same analog shelf at the rate
		for(int i = 0; i < 128; ++i)
		{
			const double x = i;
			const double hz = std::exp(((FreqPoly[0] * x + FreqPoly[1]) * x + FreqPoly[2]) * x + FreqPoly[3]);
			const double p48 = std::exp(-2 * Pi * hz / DtRate);
			const double u = p48 / (1 - p48) * DtRate / _rate;
			m_shelfPoles[static_cast<size_t>(i)] = static_cast<float>(u / (1 + u));
		}
		// The input filter is the DT2's base-width (0x1ccbd8, its table 0x26cb78): its poles' law, 0 from 508 on
		for(int j = 0; j < 508; ++j)
			m_poles[static_cast<size_t>(j)] = static_cast<float>(dt2BaseWidthPole(j, _rate));
		// Cubic Lagrange at t = i / 127, taps i, i-1, i-2, i-3 at positions 2, 1, 0, -1
		for(int i = 0; i < 128; ++i)
		{
			const double t = i / 127.0;
			m_interp[static_cast<size_t>(i)] = {static_cast<float>((t + 1) * t * (t - 1) / 6),
				static_cast<float>(-(t + 1) * t * (t - 2) / 2), static_cast<float>((t + 1) * (t - 1) * (t - 2) / 2),
				static_cast<float>(-t * (t - 1) * (t - 2) / 6)};
		}
		// The pre-delay in samples x k; the oscillator's period and its depth in whole samples: it rests near
		// (s, c) = (0, -1) for minutes, and a fractional depth would leave line 7 between two samples, under the
		// interpolator's loss of highs at each pass (2.9 dB at 16 kHz at 44.1 kHz)
		m_preMul = static_cast<float>(static_cast<double>(PreMul48) * k);
		m_preMin = static_cast<float>(PreMin48 * k);
		m_lfoStep = static_cast<float>(LfoStep48 / k);
		m_lfoDepth = static_cast<float>(std::round(LfoDepth48 * k));

		m_pre.data.resize(PreSize);
		for(auto& ring : m_allpasses)
			ring.data.resize(AllpassSize);
		for(auto& ring : m_lines)
			ring.data.resize(LineSize);
		// The oracle's knobs
		set(16, 31, 64, 50, 32, 96);
		clear();
	}

	double Dt2Reverb::preDelaySeconds(const uint8_t _pre)
	{
		const float pre = dt2Knob(_pre << 7);
		return std::min(pre * pre * PreMul48 + PreMin48, PreMax) / DtRate;
	}

	void Dt2Reverb::set(const uint8_t _pre, const uint8_t _dec, const uint8_t _freq, const uint8_t _gain,
		const uint8_t _hpf, const uint8_t _lpf)
	{
		// As the DT2's 0xb80e40 makes its words of its knobs' floats, in its order
		const auto knob = [](const uint8_t _v) { return dt2Knob(_v << 7); };
		const float pre = knob(_pre);
		m_preDelay = static_cast<int>(std::min(std::max(pre * pre * m_preMul + m_preMin, m_preMin), PreMax));
		const float p = m_shelfPoles[static_cast<size_t>(clamped(knob(_freq) * 127.0f, 127.0f))];
		const float g = clamped(knob(_gain), 1.0f), omp = 1.0f - p, og = omp * g;
		m_b0 = p + og;
		m_b1 = p - og;
		m_a1 = p - omp;
		// HPF's pole at h / 64, LPF's at (h + l) / 64: open unless HPF + LPF < 127 (a knob is at most 1)
		const int h = dt2BaseWidthKnob(_hpf << 7), l = dt2BaseWidthKnob(_lpf << 7);
		m_highpass = m_poles[static_cast<size_t>(h >> 6)];
		m_lowpass = m_poles[static_cast<size_t>((h + l) >> 6)];
		m_lowpassC = m_lowpass - 1.0f;
		m_highpassA0 = m_highpass * 0.5f + 0.5f;
		const auto d = static_cast<size_t>(clamped(knob(_dec) * 127.0f, 127.0f));
		const float scale = d < 64 ? m_allpassScale[d] : AllpassHigh;
		for(size_t i = 0; i < Stages; ++i)
		{
			m_allpassGains[i] = m_allpassK[i] * scale;
			m_gains[i] = m_lineGains[i][d];
		}
	}

	void Dt2Reverb::clear()
	{
		const auto empty = [](Ring& _ring)
		{
			std::fill(_ring.data.begin(), _ring.data.end(), 0.0f);
			_ring.write = 0;
		};
		empty(m_pre);
		for(auto& ring : m_allpasses)
			empty(ring);
		for(auto& ring : m_lines)
			empty(ring);
		m_s1 = m_y1 = 0;
		m_shelfX.fill(0.0f);
		m_shelfY.fill(0.0f);
		m_sin = 0;
		m_cos = -1;
	}

	void Dt2Reverb::process(const float* const _inL, const float* const _inR, float* const _outL, float* const _outR)
	{
		// The oscillator, once a block (0xb80314), the two read positions from it
		m_sin = m_sin - m_cos * m_lfoStep;
		m_cos = m_cos + m_lfoStep * m_sin;
		struct Read
		{
			int at;
			const std::array<float, 4>* c;
		};
		const auto readOf = [this](const Ring& _ring, const float _delay)
		{
			// 0xb8055d: the position at the block's start, its phase from its fraction
			float p = static_cast<float>(_ring.write) - _delay;
			if(!(p >= 0))
				p = p + static_cast<float>(LineSize);
			const float whole = std::trunc(p);
			const auto phase = static_cast<size_t>(clamped((p - whole) * 127.0f, 127.0f));
			return Read{static_cast<int>(whole), &m_interp[phase]};
		};
		const Read read3 = readOf(m_lines[3], m_lineLengths[3] + m_sin * m_lfoDepth);
		const Read read7 = readOf(m_lines[7], m_lineLengths[7] + m_cos * m_lfoDepth);
		const auto tap = [](const Ring& _ring, const Read& _read, const int _n)
		{
			constexpr int Mask = LineSize - 1;
			const int r = (_read.at + _n) & Mask;
			const auto& c = *_read.c;
			const float* const v = _ring.data.data();
			const float acc = v[(r - 1) & Mask] * c[1] + v[r] * c[0];
			const float acc2 = v[(r - 2) & Mask] * c[2] + acc;
			return acc2 + v[(r - 3) & Mask] * c[3];
		};
		const auto delayed = [](const Ring& _ring, const int _mask, const int _n)
		{
			return _ring.data[static_cast<size_t>((_ring.write - _ring.delay + _n) & _mask)];
		};

		float* const pre = m_pre.data.data();
		for(int n = 0; n < BlockSize; ++n)
		{
			// 0xb80450, the pre-delay: the mean in, read D back (D at least 33 samples: never this block's)
			const float x = (_inL[n] + _inR[n]) * 0.5f;
			const float in = pre[(m_pre.write - m_preDelay + n) & (PreSize - 1)];
			pre[(m_pre.write + n) & (PreSize - 1)] = x;

			// 0x1ccbd8, the input filter: a one-pole lowpass, then a one-pole highpass
			const float s = m_s1 * m_lowpass - m_lowpassC * in;
			const float y = (m_highpassA0 * s - m_highpassA0 * m_s1) + m_y1 * m_highpass;
			m_s1 = s;
			m_y1 = y;

			// 0xb8037f, eight allpasses in series
			float v = y;
			for(size_t k = 0; k < Stages; ++k)
			{
				auto& ring = m_allpasses[k];
				const float g = m_allpassGains[k], g2m1 = g * g - 1.0f;
				const float d = delayed(ring, AllpassSize - 1, n);
				ring.data[static_cast<size_t>((ring.write + n) & (AllpassSize - 1))] = d * g + v;
				v = -g * v - d * g2m1;
			}

			// 0xb8063e, the network
			float t[Stages];
			for(const size_t k : {size_t(0), size_t(1), size_t(2), size_t(4), size_t(5), size_t(6)})
				t[k] = delayed(m_lines[k], LineSize - 1, n);
			t[3] = tap(m_lines[3], read3, n);
			t[7] = tap(m_lines[7], read7, n);
			t[0] = v + t[0];
			t[4] = v + t[4];
			// 0xb8033b, a shelf on each line
			for(size_t k = 0; k < Stages; ++k)
			{
				const float xn = t[k];
				const float yn = (m_shelfX[k] * m_b1 + xn * m_b0) - m_shelfY[k] * m_a1;
				m_shelfX[k] = xn;
				m_shelfY[k] = yn;
				t[k] = yn;
			}
			// 0xb809b4 and 0xb80a2a, the two mixes in the DT2's order: W_i = 2 g_i x_i - sum g_j x_j
			const float* const ga = m_gains.data();
			const float a = t[0], b = t[1], cc = t[2], dd = t[3];
			const float ag = a * ga[0], bg = b * ga[1], ng = a * -ga[0], cg = cc * ga[2], dg = dd * ga[3];
			float w0 = ag - bg, w1 = ng + bg;
			const float nn = ng - bg;
			float w2 = nn + cg, w3 = nn - cg;
			w0 = w0 - cg;
			w1 = w1 - cg;
			const float o0 = a + cc, o1 = b + dd;
			w3 = w3 + dg;
			w2 = w2 - dg;
			w0 = w0 - dg;
			w1 = w1 - dg;
			const float e = t[4], ff = t[5], gg = t[6], hh = t[7];
			const float fh = ff * ga[5], eh = e * ga[4], ne = e * -ga[4];
			float v0 = eh - fh;
			const float gh = gg * ga[6];
			float v1 = ne + fh;
			const float mm = ne - fh, eg = e + gg, hg = hh * ga[7];
			_outR[n] = o1 + eg;
			float v2 = mm + gh, v3 = mm - gh;
			v0 = v0 - gh;
			v1 = v1 - gh;
			const float fhs = ff + hh;
			v2 = v2 - hg;
			v3 = v3 + hg;
			v0 = v0 - hg;
			v1 = v1 - hg;
			_outL[n] = o0 + fhs;
			const float back[Stages]{w0, w1, w2, w3, v0, v1, v2, v3};
			for(size_t k = 0; k < Stages; ++k)
				m_lines[k].data[static_cast<size_t>((m_lines[k].write + n) & (LineSize - 1))] = back[k];
		}
		m_pre.write = (m_pre.write + BlockSize) & (PreSize - 1);
		for(auto& ring : m_allpasses)
			ring.write = (ring.write + BlockSize) & (AllpassSize - 1);
		for(auto& ring : m_lines)
			ring.write = (ring.write + BlockSize) & (LineSize - 1);
	}
}
