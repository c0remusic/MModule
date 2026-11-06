#include "stFastResample.h"

#include <cstring>

namespace syntakt::fast
{
	alignas(16) const float Resample48To44::Table[Up][Taps] = {
#include "stFastResampleTaps.inc"
	};

	Resample48To44::Resample48To44()
	{
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
			const float* h = Table[m * Down % Up];
			const auto i = static_cast<int>(at - m_base);
			// The taps outer, a sum per group of four lanes: each lane still sums k = 0 to Taps - 1 in order
			static_assert(Lanes == 16, "four groups of four lanes");
			__m128 acc[4] = {_mm_setzero_ps(), _mm_setzero_ps(), _mm_setzero_ps(), _mm_setzero_ps()};
			for(int k = 0; k < Taps; ++k)
			{
				const __m128 c = _mm_set1_ps(h[Taps - 1 - k]);
				const float* x = m_x[i - k];
				for(int g = 0; g < 4; ++g)
					acc[g] = _mm_add_ps(acc[g], _mm_mul_ps(c, _mm_load_ps(x + 4 * g)));
			}
			for(int g = 0; g < 4; ++g)
				_mm_store_ps(_out.s[n] + 4 * g, acc[g]);
		}
		m_out += BlockSize;

		// Keep what the next block's first sample reads
		const auto drop = static_cast<int>(inputOf(m_out) - (Taps - 1) - m_base);
		m_count -= drop;
		std::memmove(m_x[0], m_x[drop], sizeof(m_x[0]) * static_cast<size_t>(m_count));
		m_base += drop;
	}
}
