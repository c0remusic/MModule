#include "edDrumsDt2Chorus.h"

#include <algorithm>
#include <cmath>

namespace elektron::drums
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;
		constexpr double DtRate = 48000;

		// The DT2's words (its listings' bit patterns, each the float32 of the decimal written)
		constexpr float SpeedMul = 2.81999993f, SpeedAdd = 0.280000001f;	// Hz = SPD x 2.82 + 0.28
		constexpr float SmoothK = -0.233700797f;		// a block's glide k = (1 + 32 x SmoothK / rate)^256
		constexpr float DepthMul48 = 0.00469020009f;	// the depth into the LFO's one-pole at 48 kHz
		constexpr float TriangleMul = 30.9990005f;		// the triangle, 0-1, into the delay table's 0-31

		// The delay table's 32 times in microseconds, measured on the DT2 (its words are these x 0.048, float-exact):
		// no closed form, a parabola holding 0-29 within 0.19 samples and breaking at 30 and 31
		constexpr int DelayUs[32]{5403, 5490, 5576, 5660, 5744, 5826, 5907, 5988, 6068, 6147, 6222, 6299, 6375, 6451,
			6525, 6598, 6670, 6743, 6814, 6884, 6954, 7022, 7090, 7158, 7224, 7290, 7356, 7420, 7484, 7548, 7661, 7674};

		// The DT2's filters come from the bilinear transform without prewarp at 48 kHz: K = pi f / 48 kHz. At another
		// rate, the K that gives the same digital response: tan of the frequency the 48 kHz one warped to (at 48 kHz,
		// the DT2's K exactly). Without it the three 7 kHz biquads are 3 dB off at 12.7 kHz at 44.1 kHz
		double warpedK(const double _hz, const double _rate)
		{
			const double k48 = Pi * _hz / DtRate;
			return _rate == DtRate ? k48 : std::tan(std::atan(k48) * DtRate / _rate);
		}

		// A one-pole by the bilinear transform, as the DT2's tables are: (2 - w) / (2 + w), w = 2K
		double bilinearPole(const double _hz, const double _rate)
		{
			const double w = 2 * warpedK(_hz, _rate);
			return (2 - w) / (2 + w);
		}

		float triangle(const float _x)
		{
			return 0.5f > _x ? _x + _x : -(_x + _x) + 2.0f;
		}

		float fraction(const float _x)
		{
			return _x - std::trunc(_x);
		}
	}

	Dt2Chorus::Dt2Chorus(const double _rate)
	{
		m_invRate = static_cast<float>(1 / _rate);
		// HPF: 40 Hz + 960 Hz x (k / 127)^2, its pole rounded to 6 decimals as the DT2's table is
		for(size_t k = 0; k < m_poles.size(); ++k)
		{
			const double hz = 40 + 960 * std::pow(static_cast<double>(k) / 127, 2);
			m_poles[k] = static_cast<float>(std::round(bilinearPole(hz, _rate) * 1e6) / 1e6);
		}
		for(size_t i = 0; i < 32; ++i)
			m_delays[i] = static_cast<float>(DelayUs[i] * _rate / 1e6);
		m_delays[32] = m_poles[0];
		// Three lowpass biquads at 7 kHz, Q 0.8, bilinear without prewarp at 48 kHz
		const double k = warpedK(7000, _rate), q = 0.8, n = 1 / (1 + k / q + k * k);
		m_biquad[0] = static_cast<float>(k * k * n);
		m_biquad[1] = static_cast<float>(2 * k * k * n);
		m_biquad[2] = static_cast<float>(k * k * n);
		m_biquad[3] = static_cast<float>(2 * (k * k - 1) * n);
		m_biquad[4] = static_cast<float>((1 - k / q + k * k) * n);
		// The LFO's one-pole at 72 Hz; its depth word, the DT2's at 48 kHz, as the pole moves with the rate
		const double pole = bilinearPole(72, _rate);
		m_lfoPole = static_cast<float>(pole);
		m_depthMul = static_cast<float>(DepthMul48 * (1 - pole) / (1 - bilinearPole(72, DtRate)));
		// The per-sample glides, 0.995 of the gap left at 48 kHz
		m_sampleCoef = static_cast<float>(std::pow(0.995, DtRate / _rate));
		set(127, 32, 0, 127);
		settle();
	}

	float Dt2Chorus::knob(const uint8_t _value)
	{
		return static_cast<float>(_value * 256) * (1.0f / 32512);
	}

	void Dt2Chorus::set(const uint8_t _depth, const uint8_t _speed, const uint8_t _hpf, const uint8_t _width)
	{
		m_knobs[0] = knob(_depth);
		m_knobs[1] = knob(_speed);
		m_knobs[2] = knob(_hpf);
		m_knobs[3] = knob(_width);
	}

	void Dt2Chorus::settle()
	{
		m_s = State{};
		m_s.depth = m_knobs[0];
		m_s.width = m_knobs[3];
		m_s.mix[0] = m_s.mix[1] = 1;
		blockSettings();
		// The block's glide in float may leave DPTH and WDTH an ulp off their knobs: at them exactly
		m_s.depth = m_knobs[0];
		m_s.width = m_knobs[3];
		m_s.glideTarget = m_s.depth * m_depthMul;
		m_s.glide = m_s.glideTarget;
		m_s.delay[0] = m_s.delay[1] = m_delays[0];
	}

	// 0xb82543: the block's HPF, LFO rate, glides of DPTH and WDTH
	void Dt2Chorus::blockSettings()
	{
		const float h = std::min(std::max(m_knobs[2] * 127.0f, 0.0f), 127.0f);
		const float p = m_poles[static_cast<size_t>(h)];
		m_s.increment = m_invRate * (m_knobs[1] * SpeedMul + SpeedAdd);
		// The mix's target, the driver's word, 1: the wet signal alone
		m_s.mixTarget = 1;
		const float a0 = p * 0.5f + 0.5f;
		for(auto& f : m_s.highpass)
		{
			f[0] = a0;
			f[1] = -a0;
			f[2] = -p;
		}
		float k = static_cast<float>(BlockSize) * m_invRate * SmoothK + 1.0f;
		for(int i = 0; i < 8; ++i)
			k = k * k;
		const float rest = 1.0f - k;
		m_s.depth = m_s.depth * k + rest * m_knobs[0];
		m_s.width = m_s.width * k + rest * m_knobs[3];
		m_s.glideTarget = m_s.depth * m_depthMul;
	}

	// 0xb8235f: the two channels' delays from the triangle, the right one WDTH / 2 of a turn past
	void Dt2Chorus::lfo()
	{
		auto& s = m_s;
		const float d = s.glide;
		const float phases[2]{s.phase, fraction(s.phase + s.width * 0.5f)};
		float y[2];
		for(int c = 0; c < 2; ++c)
		{
			const float t = triangle(phases[c]) * TriangleMul;
			y[c] = s.lfo[c] * m_lfoPole + (s.triangle[c] * d + t * d);
			s.triangle[c] = t;
			s.lfo[c] = y[c];
		}
		s.glide = s.glideTarget + (d - s.glideTarget) * m_sampleCoef;
		for(int c = 0; c < 2; ++c)
		{
			const auto i = static_cast<size_t>(std::trunc(y[c]));
			const float f = fraction(y[c]);
			s.delay[c] = (1.0f - f) * m_delays[i] + f * m_delays[i + 1];
		}
		s.phase = fraction(s.phase + s.increment);
	}

	void Dt2Chorus::process(const float* _inL, const float* _inR, float* _outL, float* _outR)
	{
		auto& s = m_s;
		blockSettings();
		const float* in[2]{_inL, _inR};
		float wet[2][BlockSize];
		for(int c = 0; c < 2; ++c)
		{
			auto& h = s.highpass[c];
			for(int n = 0; n < BlockSize; ++n)
			{
				const float x = in[c][n];
				const float y = (h[3] * h[1] + x * h[0]) - h[4] * h[2];
				h[3] = x;
				h[4] = y;
				wet[c][n] = y;
			}
			const float* b = m_biquad;
			for(auto& z : s.lowpass[c])
			{
				for(int n = 0; n < BlockSize; ++n)
				{
					const float x = wet[c][n];
					const float y = ((z[1] * b[2] + (z[0] * b[1] + x * b[0])) - z[2] * b[3]) - z[3] * b[4];
					z[3] = z[2];
					z[1] = z[0];
					z[0] = x;
					z[2] = y;
					wet[c][n] = y;
				}
			}
		}
		for(int n = 0; n < BlockSize; ++n)
		{
			lfo();
			const float x[2]{wet[0][n], wet[1][n]};
			for(int c = 0; c < 2; ++c)
			{
				const auto i = static_cast<int>(std::trunc(s.delay[c]));
				const float f = fraction(s.delay[c]);
				const auto& line = s.line[static_cast<size_t>(c)];
				const float old = line[static_cast<size_t>((s.write - i) & (Line - 1))];
				const float now = line[static_cast<size_t>((s.write - i + 1) & (Line - 1))];
				wet[c][n] = (now - old) * (1.0f - f) + old;
			}
			s.line[0][static_cast<size_t>(s.write)] = x[0];
			s.line[1][static_cast<size_t>(s.write)] = x[1];
			s.write = (s.write + 1) & (Line - 1);
		}
		float* out[2]{_outL, _outR};
		for(int c = 0; c < 2; ++c)
		{
			float m = s.mix[c];
			for(int n = 0; n < BlockSize; ++n)
			{
				out[c][n] = m * wet[c][n] + (1.0f - m) * in[c][n];
				m = s.mixTarget + (m - s.mixTarget) * m_sampleCoef;
			}
			s.mix[c] = m;
		}
	}
}
