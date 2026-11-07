#include "stFastDelay.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

#include "stFastStages.h"
#include "stFastTables.h"

namespace syntakt::fast
{
	namespace
	{
		constexpr int Frames = Delay::BlockSize;

		// The stores' saturation at full scale
		float rail(const float _x) { return std::min(std::max(_x, -1.0f), 1.0f); }

		// A block of frames from the ring, from frame _at on
		void readFrames(const std::vector<float>& _ring, const uint32_t _mask, const uint32_t _at, float* _to)
		{
			const uint32_t first = _at & _mask, run = std::min<uint32_t>(Frames, _mask + 1 - first);
			std::memcpy(_to, &_ring[2 * size_t(first)], 2 * run * sizeof(float));
			std::memcpy(_to + 2 * run, _ring.data(), 2 * (Frames - run) * sizeof(float));
		}

		void writeFrames(std::vector<float>& _ring, const uint32_t _mask, const uint32_t _at, const float* _from)
		{
			const uint32_t first = _at & _mask, run = std::min<uint32_t>(Frames, _mask + 1 - first);
			std::memcpy(&_ring[2 * size_t(first)], _from, 2 * run * sizeof(float));
			std::memcpy(_ring.data(), _from + 2 * run, 2 * (Frames - run) * sizeof(float));
		}

		uint32_t ringFrames(const uint32_t _longest)
		{
			uint32_t frames = 1;
			while(frames < _longest + 2 * Frames)
				frames <<= 1;
			return frames;
		}
	}

	Delay::Delay(const double _rate)
		: m_rate(static_cast<int>(_rate)), m_ease(static_cast<int>(std::lround(256 * 48000 / _rate))),
		  m_longest(8 * static_cast<uint32_t>(_rate)), m_poles(tables::sendPoles(_rate)),
		  m_ring(2 * size_t(ringFrames(m_longest))), m_mask(ringFrames(m_longest) - 1)
	{
	}

	void Delay::setFree(const double _hz)
	{
		const double frames = _hz > 0 ? std::clamp(m_rate / _hz, double(Frames), double(m_longest)) : 0;
		m_free = static_cast<uint32_t>(std::lround(frames));
	}

	void Delay::settle()
	{
		m_last = targetFrames();
		m_time = m_last << 5;
	}

	void Delay::clear()
	{
		std::fill(m_ring.begin(), m_ring.end(), 0.0f);
		m_low[0] = m_low[1] = m_high[0] = m_high[1] = 0;
	}

	uint32_t Delay::targetFrames() const
	{
		if(m_free)
			return m_free;
		// (TIME + 256) / 1024 of 7.5 s / BPM
		const uint32_t scaled = static_cast<uint32_t>(m_words.time + 0x100) * static_cast<uint32_t>(m_rate);
		return std::min(((scaled >> 10) * 900) / static_cast<uint32_t>(m_tempo), m_longest);
	}

	uint32_t Delay::nextFrames()
	{
		// Eased by 255/256 a block, in the same seconds at any rate: at 48 kHz, the machine's integers, whose rounding
		// keeps the time a few frames off its target, from which side it came
		const uint32_t target = targetFrames();
		const uint64_t half = m_time >> 1;
		const auto ease = static_cast<uint64_t>(m_ease);
		const uint64_t toward = uint64_t((target >> 1) << 5) * ease;
		const auto eased = static_cast<uint32_t>(((half * (65536 - ease)) >> 8) + (toward >> 8));
		m_time = eased >> 7;
		return eased >> 12;
	}

	void Delay::process(const float* const _inL, const float* const _inR, float* const _outL, float* const _outR)
	{
		const FlushDenormals ftz;
		const uint32_t frames = nextFrames();
		float a[2 * Frames], b[2 * Frames];
		readFrames(m_ring, m_mask, m_write - frames, a);
		readFrames(m_ring, m_mask, m_write - m_last, b);
		m_last = frames;

		// The taps crossfaded and clipped: 2 x - x |x|
		const float fadeOut = m_fade;
		m_fade = 1;
		for(int i = 0; i < 2 * Frames; ++i)
		{
			const float t = static_cast<float>(i / 2) / Frames;
			const float x = t * a[i] + fadeOut * (1 - t) * b[i];
			a[i] = x + x - x * std::fabs(x);
		}

		// The low-pass and the high-pass, in the machine's order: each high-pass sees the low-pass's difference
		const auto highPass = static_cast<size_t>(m_words.highPass >> 6);
		const auto lowPass = static_cast<size_t>((m_words.highPass + m_words.lowPass) >> 6);
		const float hp = m_poles[highPass], lp = m_poles[lowPass];
		const float toLow = 1 - lp, toHigh = 0.5f * (1 + hp);
		for(int c = 0; c < 2; ++c)
		{
			float low = m_low[c], high = m_high[c];
			for(int i = 0; i < Frames; ++i)
			{
				const float next = lp * low + toLow * a[2 * i + c];
				high = rail(hp * high + toHigh * (next - low));
				low = next;
				a[2 * i + c] = high;
			}
			m_low[c] = low;
			m_high[c] = high;
		}

		// The output, and what comes back into the ring: FDBK ramped from the last block's, L and R crossed with X
		const double fdbk = 66053.0 * std::min(m_words.feedback, 0x7f00);
		const float feedback = static_cast<float>(std::min(fdbk, 2147483648.0) / 2147483648.0);
		const float step = (feedback - m_feedback) / Frames;
		const int crossed = m_words.cross ? 1 : 0;
		float back[2 * Frames];
		for(int i = 0; i < Frames; ++i)
		{
			const float g = m_feedback + step * static_cast<float>(i);
			_outL[i] = a[2 * i];
			_outR[i] = a[2 * i + 1];
			back[2 * i + crossed] = g * a[2 * i];
			back[2 * i + 1 - crossed] = g * a[2 * i + 1];
		}
		m_feedback = feedback;

		// The send's four gains from WID: with X, L and R each to both sides by (1 - w, w); without, a width
		const float w = static_cast<float>(m_words.width) / 32768;
		float target[4];
		if(crossed)
		{
			target[0] = target[1] = 1 - w;
			target[2] = target[3] = w;
		}
		else if(w < 0.5f)
		{
			target[0] = w + 0.5f;
			target[1] = 0.5f - w;
			target[2] = 0;
			target[3] = 2 * w;
		}
		else
		{
			target[0] = 2 * (1 - w);
			target[1] = 0;
			target[2] = w - 0.5f;
			target[3] = 1.5f - w;
		}
		for(int i = 0; i < Frames; ++i)
		{
			const float t = static_cast<float>(i) / Frames;
			const float g0 = m_gains[0] + (target[0] - m_gains[0]) * t, g1 = m_gains[1] + (target[1] - m_gains[1]) * t;
			const float g2 = m_gains[2] + (target[2] - m_gains[2]) * t, g3 = m_gains[3] + (target[3] - m_gains[3]) * t;
			back[2 * i] = rail(back[2 * i] + g0 * _inL[i] + g1 * _inR[i]);
			back[2 * i + 1] = rail(back[2 * i + 1] + g2 * _inL[i] + g3 * _inR[i]);
		}
		std::copy(target, target + 4, m_gains);

		writeFrames(m_ring, m_mask, m_write, back);
		m_write += Frames;
	}
}
