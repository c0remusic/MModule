#include "stFastResample.h"

#include <cmath>
#include <cstring>

namespace syntakt::fast
{
	namespace
	{
		// The modified Bessel function I0, by its series
		double besselI0(const double _x)
		{
			double sum = 1, term = 1;
			for(int k = 1; k < 50; ++k)
			{
				term *= (_x / (2 * k)) * (_x / (2 * k));
				sum += term;
			}
			return sum;
		}
	}

	Resample48To44::Resample48To44()
	{
		constexpr double Cutoff = 20000, Beta = 7, Pi = 3.14159265358979323846;
		constexpr int N = 320 * Latency + 1;			// centred on 160 Latency: Latency host samples
		const double c = (N - 1) / 2.0, fc = Cutoff / (Up * MachineRate);	// in cycles per upsampled sample
		for(int p = 0; p < Up; ++p)
			for(int k = 0; k < Taps; ++k)
			{
				const int j = p + Up * k;
				if(j >= N)
				{
					m_h[p][k] = 0;
					continue;
				}
				const double t = j - c, r = t / c;
				const double sinc = t == 0 ? 2 * fc : std::sin(2 * Pi * fc * t) / (Pi * t);
				m_h[p][k] = static_cast<float>(Up * sinc * besselI0(Beta * std::sqrt(1 - r * r)) / besselI0(Beta));
			}
		reset();
	}

	void Resample48To44::reset()
	{
		std::memset(m_x, 0, sizeof(m_x));
		m_base = -(Taps - 1);
		m_count = Taps - 1;
		m_out = 0;
	}

	bool Resample48To44::needsInput() const
	{
		return inputOf(m_out + BlockSize - 1) >= m_base + m_count;
	}

	void Resample48To44::push(const Block& _in)
	{
		std::memcpy(m_x[m_count], _in.s, sizeof(_in.s));
		m_count += BlockSize;
	}

	void Resample48To44::pull(Block& _out)
	{
		for(int n = 0; n < BlockSize; ++n)
		{
			const int64_t m = m_out + n;
			const int64_t at = inputOf(m);
			const float* h = m_h[m * Down % Up];
			const auto i = static_cast<int>(at - m_base);
			for(int g = 0; g < Lanes; g += 4)
			{
				__m128 acc = _mm_setzero_ps();
				for(int k = 0; k < Taps; ++k)
					acc = _mm_add_ps(acc, _mm_mul_ps(_mm_set1_ps(h[k]), _mm_load_ps(m_x[i - k] + g)));
				_mm_store_ps(_out.s[n] + g, acc);
			}
		}
		m_out += BlockSize;

		// Keep what the next block's first sample reads
		const auto drop = static_cast<int>(inputOf(m_out) - (Taps - 1) - m_base);
		m_count -= drop;
		std::memmove(m_x[0], m_x[drop], sizeof(m_x[0]) * static_cast<size_t>(m_count));
		m_base += drop;
	}
}
