// The circuits of the two utility machines (edDrumsAnalogVoice.h): a filtered noise source, and the impulse the
// control renders itself. Our own models; the control (edDrumsAnalogUtility.cpp) gives them their level and settings
// every tick.
#include <algorithm>
#include <cmath>

#include "edDrumsAnalogPort.h"
#include "edDrumsAnalogVoice.h"

namespace elektron::drums::analog::circuit
{
	namespace
	{
		enum Slot : size_t
		{
			Amp,					// the noise's level the last tick played
			HighLow, HighBand,		// the high-pass filter's state
			LowLow, LowBand,		// the low-pass filter's state
			StreamLast				// outA's last sample of the last tick
		};
		enum USlot : size_t { Seed, Sounded };	// Sounded: the control has sent something since the Hit

		constexpr float Q31 = 4.656613e-10f;

		uint32_t nextRandom(uint32_t& _s)
		{
			_s ^= _s << 13;
			_s ^= _s >> 17;
			_s ^= _s << 5;
			return _s;
		}

		// A setting byte 0-127 as a filter's cutoff: 20 Hz to 20 kHz, ten octaves
		float cutoffOf(const uint8_t _b) { return 20.0f * std::exp2(std::min<int>(_b, 127) * (10.0f / 127)); }
		// A state-variable filter as a circuit's two integrators are (zero-delay form): unity gain in its pass band and
		// stable at any cutoff, up to just below half the rate. _k is the damping (1.41: no peak)
		struct Svf
		{
			float a1, a2, a3, k;
			Svf(const float _hz, const float _rate, const float _k) : k(_k)
			{
				const float g = std::tan(3.14159265f * std::min(_hz, _rate * 0.45f) / _rate);
				a1 = 1 / (1 + g * (g + _k));
				a2 = g * a1;
				a3 = g * a2;
			}
			// one sample: _s1, _s2 the two integrators' state; the low-pass out, the high-pass in _high
			float run(const float _in, float& _s1, float& _s2, float& _high) const
			{
				const float v3 = _in - _s2;
				const float v1 = a1 * _s1 + a2 * v3;
				const float v2 = _s2 + a2 * _s1 + a3 * v3;
				_s1 = 2 * v1 - _s1;
				_s2 = 2 * v2 - _s2;
				_high = _in - k * v1 - v2;
				return v2;
			}
		};

		// The output's scales: the level byte at 64 about 0.35 at the output, its top under 1.5
		constexpr float NoiseScale = 1.5f;
		constexpr float ImpulseScale = 1.4f;
	}

	void utilityTrig(const Machine, Circuit& _c)
	{
		// a Hit restarts everything: the filters, the level ramp, the stream, the generator's seed
		_c = Circuit{};
		_c.u[Seed] = 0x2545f491u;
	}

	// Nothing of these circuits runs free: the noise's generator needs no advance (any state of it is as white), the
	// impulse is the control's
	void utilityAdvance(Machine, Circuit&, int64_t, double) {}

	void utilityRender(const Machine _m, const VoiceIo& _io, Circuit& _c, float* _out, const int _n, const double _rate)
	{
		auto& f = _c.f;
		const float inv = 1.0f / static_cast<float>(_n);
		if(_m == Machine::Impulse)
		{
			// the control's 48 kHz samples, linear across the tick
			const uint8_t* a = _io.outA.data();
			for(int i = 0; i < _n; ++i)
			{
				const float x = static_cast<float>(i) * inv * 32 - 1;
				const int k = static_cast<int>(std::floor(x));
				const float fr = x - static_cast<float>(k);
				const float s0 = k < 0 ? f[StreamLast] : static_cast<float>(static_cast<int32_t>(be32(a + 4 * k)));
				const float s1 = static_cast<float>(static_cast<int32_t>(be32(a + 4 * (k + 1))));
				const float y = (s0 + (s1 - s0) * fr) * Q31;
				_out[i] += std::fabs(y) < 1e-6f ? 0.0f : y * ImpulseScale;
			}
			f[StreamLast] = static_cast<float>(static_cast<int32_t>(be32(a + 4 * 31)));
			if(f[StreamLast] != 0)
				_c.u[Sounded] = 1;
			return;
		}

		// noise: white through a high-pass (the first setting byte) and a resonant low-pass (the third its cutoff, the
		// fourth its resonance), at heads' level, which the control's envelope sets each tick
		const auto rate = static_cast<float>(_rate);
		const float amp = static_cast<float>(static_cast<int32_t>(be32(_io.heads.data()))) * Q31;
		const float low = cutoffOf(_io.noise[2]);
		const float damp = 1.4f - std::min<int>(_io.noise[3], 127) * (1.2f / 127);
		const Svf highPass(cutoffOf(_io.noise[0]), rate, 1.41f), lowPass(low, rate, damp);
		// the filters' loudness kept about even: white noise low-passed loses power as the cutoff falls, and the
		// resonance gains it back (about as 1 / damping at the peak)
		const float even = std::min(8.0f, std::sqrt(12000.0f / low)) * (damp / 1.12f);
		if(amp == 0 && f[Amp] == 0)
		{
			f[HighLow] = f[HighBand] = f[LowLow] = f[LowBand] = 0;
			return;
		}
		for(int i = 0; i < _n; ++i)
		{
			const float white = static_cast<float>(static_cast<int32_t>(nextRandom(_c.u[Seed]))) * Q31;
			float high = 0, unused = 0;
			highPass.run(white, f[HighBand], f[HighLow], high);
			const float y = lowPass.run(high, f[LowBand], f[LowLow], unused);
			const float level = f[Amp] + (amp - f[Amp]) * static_cast<float>(i) * inv;
			_out[i] += y * level * even * NoiseScale;
		}
		f[Amp] = amp;
		if(amp != 0)
			_c.u[Sounded] = 1;
	}

	bool utilityEnded(const Machine _m, const VoiceIo&, const Circuit& _c)
	{
		// the envelope is the control's: the noise's level, the impulse's samples. Ended once it has sounded and come
		// back to zero: under 1e-6 (-120 dB) counts, since the control's integer decays near zero crawl or rest a word
		// or two from it
		if(_c.u[Sounded] == 0)
			return false;
		if(_m == Machine::Impulse)
			return std::fabs(_c.f[StreamLast]) * Q31 < 1e-6f;
		return std::fabs(_c.f[Amp]) < 1e-6f;
	}

	void utilityParams(const Machine _m, const uint8_t* _controls, std::array<uint8_t, 20>& _params)
	{
		int word[10] = {64, 64, 64, 64, 64, 64, 64, 64, 0, 0};
		const auto c = [&](const int _i) { return static_cast<int>(_controls[_i]); };
		if(_m == Machine::Noise)
		{
			// colour, decay, attack, low cut, resonance, sweep time, sweep depth, level
			word[1] = c(0); word[2] = c(1); word[5] = c(2); word[3] = c(3); word[4] = c(4); word[6] = c(5);
			word[7] = c(6); word[0] = c(7);
		}
		else
		{
			// width, decay, polarity (down from 64), level; controls 3-6 have nothing to set (their words stay 64)
			word[1] = c(0); word[2] = c(1); word[7] = c(2) >= 64 ? 1 : 0; word[0] = c(7);
		}
		for(int i = 0; i < 10; ++i)
		{
			_params[static_cast<size_t>(2 * i)] = static_cast<uint8_t>(word[i]);
			_params[static_cast<size_t>(2 * i + 1)] = 0;
		}
	}

	std::array<uint8_t, 8> utilityDefaults(const Machine _m)
	{
		if(_m == Machine::Noise)
			return {70, 30, 0, 20, 30, 40, 90, 64};
		return {2, 64, 0, 64, 64, 64, 64, 64};
	}
}
