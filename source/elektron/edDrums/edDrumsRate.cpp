#include "edDrumsRate.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <utility>

#include <emmintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace elektron::drums
{
	namespace
	{
		constexpr double Pi = 3.141592653589793;

		double besselI0(const double _x)
		{
			double sum = 1, term = 1;
			for(int k = 1; k < 60; ++k)
			{
				term *= (_x / (2 * k)) * (_x / (2 * k));
				sum += term;
			}
			return sum;
		}

		int whole(const double _rate)
		{
			const auto r = static_cast<int>(std::lround(_rate));
			if(r <= 0 || std::abs(_rate - r) > 1e-6)
				throw std::invalid_argument("a rate in whole Hz");
			return r;
		}
	}

	bool rate::fastFir()
	{
		static const bool on = []
		{
#if !defined(_MSC_VER) || !defined(_M_X64)
			return false;
#else
			// SYNTAKT_NO_AVX2 forces the SSE code here too, which a test runs that way
			if(const char* v = std::getenv("SYNTAKT_NO_AVX2"); v && *v && !(v[0] == '0' && !v[1]))
				return false;
			// AVX2 and FMA on the CPU, and the YMM registers saved by the OS (OSXSAVE, then XCR0's SSE and AVX state), as
			// syntakt::simd asks
			int r[4];
			__cpuid(r, 0);
			if(r[0] < 7)
				return false;
			__cpuid(r, 1);
			const bool fma = (r[2] >> 12) & 1, osxsave = (r[2] >> 27) & 1, avx = (r[2] >> 28) & 1;
			if(!fma || !osxsave || !avx || (_xgetbv(0) & 6) != 6)
				return false;
			__cpuidex(r, 7, 0);
			return ((r[1] >> 5) & 1) != 0;
#endif
		}();
		return on;
	}

	bool playableRate(const double _rate)
	{
		for(const double r : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
			if(_rate == r)
				return true;
		return false;
	}

	// ---- RateFir ----

	RateFir::RateFir(const double _from, const double _to, const int _taps, const double _cutoff, const double _beta)
	{
		const int from = whole(_from), to = whole(_to);
		const int g = std::gcd(from, to);
		m_up = to / g;
		m_down = from / g;
		m_taps = _taps;
		if(m_taps % 4)
			throw std::invalid_argument("whole vectors of taps");

		// The prototype at up() times the source's rate, centred on delay(): tap d weighs the input d / up() source
		// samples before the output
		const int length = m_up * m_taps;
		const double centre = delay();
		const double fc = _cutoff / (static_cast<double>(m_up) * from);
		std::vector<double> h(static_cast<size_t>(length));
		for(int d = 0; d < length; ++d)
		{
			const double x = d - centre;
			const double sinc = x == 0 ? 2 * fc : std::sin(2 * Pi * fc * x) / (Pi * x);
			const double r = x / centre;
			h[static_cast<size_t>(d)] = sinc * besselI0(_beta * std::sqrt(std::max(0.0, 1 - r * r))) / besselI0(_beta);
		}
		m_table.assign(static_cast<size_t>(length), 0.0f);
		for(int p = 0; p < m_up; ++p)
		{
			// Phase p reads the input k samples before the newest at d = p + up() k; its sum is 1, so that each phase
			// passes 0 Hz alike
			double sum = 0;
			for(int k = 0; k < m_taps; ++k)
				sum += h[static_cast<size_t>(p + m_up * k)];
			float* row = m_table.data() + static_cast<size_t>(p) * static_cast<size_t>(m_taps);
			for(int k = 0; k < m_taps; ++k)
				row[m_taps - 1 - k] = static_cast<float>(h[static_cast<size_t>(p + m_up * k)] / sum);
			// A half-band's phase on the source's own samples (a cutoff at half the source's rate, up() a whole multiple):
			// its sinc's zeros fall on every tap but the centre's, which a read takes alone
			int nonzero = 0, at = -1;
			for(int k = 0; k < m_taps; ++k)
				if(std::abs(row[k]) > 1e-7f)
				{
					++nonzero;
					at = k;
				}
			m_unit.push_back(static_cast<int8_t>(nonzero == 1 ? at : -1));
			if(nonzero == 1)
			{
				std::fill_n(row, m_taps, 0.0f);
				row[at] = 1;
			}
		}
		if(reinterpret_cast<uintptr_t>(m_table.data()) % 16)
			throw std::runtime_error("RateFir: the heap gives no 16-byte block");
	}

	// ---- RateStream ----

	RateStream::RateStream(const double _from, const double _to, const int _taps, const int _preroll, const double _cutoff,
		const double _beta)
		: m_fir(_from, _to, _taps, _cutoff, _beta)
	{
		const auto most = static_cast<size_t>(_taps + _preroll + BlockSize * m_fir.down() / m_fir.up() + 4 * BlockSize);
		m_x.assign(most, Frame{});
		m_base = -(_taps - 1);
		m_count = _taps - 1 + _preroll;
	}

	bool RateStream::needsInput() const
	{
		return inputOf(m_out + BlockSize - 1) >= m_base + m_count;
	}

	void RateStream::push(const Frame* _in)
	{
		std::copy_n(_in, BlockSize, m_x.begin() + m_count);
		m_count += BlockSize;
	}

	void RateStream::pull(Frame* _out)
	{
		static_assert(Channels == 8, "two vectors a frame");
		const int taps = m_fir.taps();
		if(rate::fastFir())
			rate::frames8(m_x.data()->data(), m_out, m_base, m_fir.up(), m_fir.down(), m_fir, _out->data());
		for(int n = 0; !rate::fastFir() && n < BlockSize; ++n)
		{
			const int64_t m = m_out + n;
			const float* h = m_fir.row(static_cast<int>(m * m_fir.down() % m_fir.up()));
			const auto i = static_cast<int>(inputOf(m) - m_base);
			__m128 a = _mm_setzero_ps(), b = _mm_setzero_ps();
			for(int k = 0; k < taps; ++k)
			{
				const __m128 c = _mm_set1_ps(h[taps - 1 - k]);
				const float* x = m_x[static_cast<size_t>(i - k)].data();
				a = _mm_add_ps(a, _mm_mul_ps(c, _mm_loadu_ps(x)));
				b = _mm_add_ps(b, _mm_mul_ps(c, _mm_loadu_ps(x + 4)));
			}
			_mm_storeu_ps(_out[n].data(), a);
			_mm_storeu_ps(_out[n].data() + 4, b);
		}
		m_out += BlockSize;

		// Keep what the next block's first output reads
		const auto drop = static_cast<int>(inputOf(m_out) - (taps - 1) - m_base);
		m_count -= drop;
		std::memmove(m_x.data(), m_x.data() + drop, sizeof(Frame) * static_cast<size_t>(m_count));
		m_base += drop;
	}

	// ---- SendsAtRate ----

	namespace
	{
		// The way down. At 48 kHz what the Tracks send holds the Syntakt's band to 24 kHz, read as it is: a filter as sharp
		// as a ring's, its taps as many at 48 kHz as RateFir::Taps at 44.1. Above, every ring is read through a RateFir,
		// so that nothing is sent past 20.5 kHz but 84 dB down, and nothing can fold under it: the filter need only be flat
		// to 20.5 kHz, a sinc at 0.45 of the host's rate under a Kaiser window of beta 6, 16 taps, within 0.008 dB to 18 kHz
		// at every phase. A seventh of the taps at 96 kHz, which made the sends the dearest part of the engine there
		struct Shape
		{
			int taps;
			double cutoff, beta;
		};

		Shape downOf(const double _rate)
		{
			if(_rate <= 48000)		// the Syntakt's own
				return {static_cast<int>(std::ceil(RateFir::Taps * _rate / 44100 / 4)) * 4, RateFir::Cutoff, RateFir::Beta};
			return {16, 0.45 * _rate, 6};
		}

		// The silence the way up must start with so that it never waits for the sends: the most, over a cycle of the host's
		// blocks, that a host block's return reads past what the sends have given by then
		int prerollOf(const double _rate, const int _host)
		{
			const auto rate = static_cast<int64_t>(std::lround(_rate)), sends = static_cast<int64_t>(Sends::Rate);
			const int64_t g = std::gcd(rate, sends), hostPart = rate / g, sendsPart = sends / g;
			int64_t pushed = 0, given = 0, worst = 0;
			int64_t downOut = 0;
			for(int64_t block = 0; block < 147 * 640; ++block)		// whole cycles of every rate's blocks
			{
				pushed += _host;
				// The down stream's needsInput, its input counted from 0: its output m reads host sample m rate / 44.1
				while((downOut + BlockSize - 1) * hostPart / sendsPart < pushed)
				{
					downOut += BlockSize;
					given += BlockSize;
				}
				const int64_t read = (block * _host + _host - 1) * sendsPart / hostPart;
				worst = std::max(worst, read + 1 - given);
			}
			return static_cast<int>(worst);
		}
	}

	SendsAtRate::SendsAtRate(Sends& _sends, const double _rate, const int _host)
		: m_sends(_sends), m_rate(whole(_rate)), m_host(_host), m_preroll(prerollOf(_rate, _host)),
		m_down(_rate, Sends::Rate, downOf(_rate).taps, 0, downOf(_rate).cutoff, downOf(_rate).beta),
		m_up(Sends::Rate, _rate, RateFir::Taps, m_preroll)
	{
		if(_host % BlockSize || _host > MaxHost)
			throw std::invalid_argument("SendsAtRate: a host block of 32, 64, 96 or 128");
	}

	double SendsAtRate::returnDelay() const
	{
		// A host sample at h is down's output at (h + its delay) 44.1 / rate, which the sends return as the same sample of
		// their output; up's output n stands for its input n 44.1 / rate - its delay - the preroll; and up's output block
		// plays in the host block after the one that went in
		const auto rate = static_cast<double>(m_rate);
		return m_down.fir().taps() / 2.0 + (m_up.fir().taps() / 2.0 + m_preroll) * rate / Sends::Rate + m_host;
	}

	void SendsAtRate::block()
	{
		const bool fed = m_fed;
		m_fed = false;
		m_feeding = m_feeding || fed;
		if(fed || m_sends.sounding())
		{
			m_quiet = 0;
			m_awake = true;
		}
		else if(m_awake && ++m_quiet >= DrainBlocks)
		{
			m_awake = false;
			for(auto& send : m_out)
				for(auto& channel : send)
					channel.fill(0.0f);
		}
		if(!m_awake)
		{
			// The sends sleep on at their own blocks, as they would at 44.1 kHz: their words still take their ticks
			for(m_sleep += m_host * static_cast<int64_t>(Sends::Rate); m_sleep >= BlockSize * m_rate; m_sleep -= BlockSize * m_rate)
				m_sends.block();
			return;
		}

		// The host's block down to the sends, 32 samples at a time, and what they return up
		for(int from = 0; from < m_host; from += BlockSize)
			goDown(from);
		for(auto& send : m_in)
			for(auto& channel : send)
				channel.fill(0.0f);
		std::array<RateStream::Frame, BlockSize> frames;
		for(int from = 0; from < m_host; from += BlockSize)
		{
			m_up.pull(frames.data());
			for(int n = 0; n < BlockSize; ++n)
				for(int s = 0; s < SendCount; ++s)
					for(int c = 0; c < 2; ++c)
						m_out[static_cast<size_t>(s)][static_cast<size_t>(c)][static_cast<size_t>(from + n)]
							= frames[static_cast<size_t>(n)][static_cast<size_t>(2 * s + c)];
		}
	}

	void SendsAtRate::goDown(const int _from)
	{
		std::array<RateStream::Frame, BlockSize> frames;
		for(int n = 0; n < BlockSize; ++n)
			for(int s = 0; s < SendCount; ++s)
				for(int c = 0; c < 2; ++c)
					frames[static_cast<size_t>(n)][static_cast<size_t>(2 * s + c)]
						= m_in[static_cast<size_t>(s)][static_cast<size_t>(c)][static_cast<size_t>(_from + n)];
		m_down.push(frames.data());
		while(!m_down.needsInput())
		{
			m_down.pull(frames.data());
			// A Track that sends, even nothing yet, wakes the sends, as at 44.1 kHz: the first block after it went in
			bool any = std::exchange(m_feeding, false);
			for(const auto& f : frames)
				for(const float v : f)
					any = any || v != 0;
			if(any)
				for(int s = 0; s < SendCount; ++s)
					for(int c = 0; c < 2; ++c)
					{
						float* in = m_sends.input(static_cast<Send>(s), c, 0);
						for(int n = 0; n < BlockSize; ++n)
							in[n] = frames[static_cast<size_t>(n)][static_cast<size_t>(2 * s + c)];
					}
			m_sends.block();
			for(int s = 0; s < SendCount; ++s)
				for(int c = 0; c < 2; ++c)
				{
					const float* out = m_sends.output(static_cast<Send>(s), c, 0);
					for(int n = 0; n < BlockSize; ++n)
						frames[static_cast<size_t>(n)][static_cast<size_t>(2 * s + c)] = m_sends.sounding() ? out[n] : 0.0f;
				}
			m_up.push(frames.data());
		}
	}
}
