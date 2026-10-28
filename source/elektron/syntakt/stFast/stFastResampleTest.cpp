// syntakt::fast::Resample48To44, the resampler that brings the fast Syntakt's 48 kHz to a 44.1 kHz host (Host), by
// research 19's Q11 on sines (.scratch/elektron-drums/research/19-qualite-audio.md, section 11), fatal:
//	gain within 0.1 dB 20 Hz-18 kHz, THD+N <= -70 dB 1-16 kHz, 22.05-24 kHz (what would alias) rejected by >= 60 dB
//	Host's timing at 44.1 kHz, fatal: a trig at every host block of the 147-block cycle lands from 0.8 host samples
//	early to 28.4 late against its ideal time, past the latency; the latency and the jitter printed
//	cost: microseconds per host block for 16 lanes, printed
// Needs no firmware.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "stFastBdModern.h"
#include "stFastBdModernHits.h"
#include "stFastBench.h"
#include "stFastResample.h"

namespace
{
	using namespace syntakt;
	using fast::Lanes;
	using namespace fast::bench;

	// ---- Q11: the resampler, 48 to 44.1 kHz ----

	// 0.5 sines at _hz (one per lane) for _seconds at 48 kHz through Resample48To44, each lane's output at 44.1 kHz
	// once the filter has filled
	std::vector<std::vector<double>> resampleSines(const double (&_hz)[Lanes], const double _seconds)
	{
		fast::Resample48To44 r;
		std::vector<std::vector<double>> out(Lanes);
		int64_t in = 0;
		const int blocks = static_cast<int>(_seconds * 44100) / fast::BlockSize;
		fast::Block x, y;
		for(int b = 0; b < blocks; ++b)
		{
			while(r.needsInput())
			{
				for(int n = 0; n < fast::BlockSize; ++n, ++in)
					for(int l = 0; l < Lanes; ++l)
					{
						const double t = static_cast<double>(in) / 48000;
						x.s[n][l] = static_cast<float>(0.5 * std::sin(2 * Pi * _hz[l] * t));
					}
				r.push(x);
			}
			r.pull(y);
			if(b < 4)	// 128 samples: past the filter's taps
				continue;
			for(int n = 0; n < fast::BlockSize; ++n)
				for(int l = 0; l < Lanes; ++l)
					out[static_cast<size_t>(l)].push_back(y.s[n][l]);
		}
		return out;
	}

	// The sine at _hz that fits _y best (and a constant): its amplitude and the residual's RMS
	struct Fit
	{
		double amplitude, residual;
	};

	Fit fitSine(const std::vector<double>& _y, const double _hz, const double _fs)
	{
		double a[3][4] = {};	// the normal equations of sin, cos, 1, augmented
		for(size_t i = 0; i < _y.size(); ++i)
		{
			const double w = 2 * Pi * _hz * static_cast<double>(i) / _fs;
			const double b[3] = {std::sin(w), std::cos(w), 1};
			for(int r = 0; r < 3; ++r)
			{
				for(int c = 0; c < 3; ++c)
					a[r][c] += b[r] * b[c];
				a[r][3] += b[r] * _y[i];
			}
		}
		for(int p = 0; p < 3; ++p)	// Gauss-Jordan: the matrix is symmetric positive definite
			for(int r = 0; r < 3; ++r)
			{
				if(r == p)
					continue;
				const double f = a[r][p] / a[p][p];
				for(int c = 0; c < 4; ++c)
					a[r][c] -= f * a[p][c];
			}
		const double s = a[0][3] / a[0][0], co = a[1][3] / a[1][1], dc = a[2][3] / a[2][2];
		double sum = 0;
		for(size_t i = 0; i < _y.size(); ++i)
		{
			const double w = 2 * Pi * _hz * static_cast<double>(i) / _fs;
			const double e = _y[i] - (s * std::sin(w) + co * std::cos(w) + dc);
			sum += e * e;
		}
		return {std::hypot(s, co), std::sqrt(sum / static_cast<double>(_y.size()))};
	}

	void q11()
	{
		double gain = 0, gainAt = 0, thd = -1e9, thdAt = 0, rejection = -1e9, rejectionAt = 0;
		for(int pass = 0; pass < 2; ++pass)
		{
			// 32 frequencies log-spaced over 20 Hz-16 kHz, the lanes of a pass taking every other one
			double hz[Lanes];
			for(int l = 0; l < Lanes; ++l)
				hz[l] = 20 * std::pow(800.0, (2 * l + pass) / 31.0);
			const auto y = resampleSines(hz, 0.5);
			for(int l = 0; l < Lanes; ++l)
			{
				const Fit f = fitSine(y[static_cast<size_t>(l)], hz[l], 44100);
				const double g = 20 * std::log10(f.amplitude / 0.5);
				if(std::abs(g) > std::abs(gain))
				{
					gain = g;
					gainAt = hz[l];
				}
				const double n = 20 * std::log10(f.residual / (f.amplitude / std::sqrt(2.0)));
				if(hz[l] >= 1000 && n > thd)
				{
					thd = n;
					thdAt = hz[l];
				}
			}

			// 32 frequencies over 22.05-24 kHz: what is left of them, against the input's RMS
			for(int l = 0; l < Lanes; ++l)
				hz[l] = 22050 + (23990 - 22050) * (2 * l + pass) / 31.0;
			const auto z = resampleSines(hz, 0.25);
			for(int l = 0; l < Lanes; ++l)
			{
				const double rms = std::sqrt(energy(z[static_cast<size_t>(l)]) / static_cast<double>(z[0].size()));
				const double r = 20 * std::log10(rms / (0.5 / std::sqrt(2.0)));
				if(r > rejection)
				{
					rejection = r;
					rejectionAt = hz[l];
				}
			}
		}
		// 16 frequencies over 16-18 kHz, the top of the band it holds flat
		double hz[Lanes];
		for(int l = 0; l < Lanes; ++l)
			hz[l] = 16000 + 2000.0 * l / (Lanes - 1);
		const auto y = resampleSines(hz, 0.25);
		double top = 0, topAt = 0, at18 = 0;
		for(int l = 0; l < Lanes; ++l)
		{
			const double g = 20 * std::log10(fitSine(y[static_cast<size_t>(l)], hz[l], 44100).amplitude / 0.5);
			if(std::abs(g) > std::abs(top))
			{
				top = g;
				topAt = hz[l];
			}
			at18 = g;
		}
		std::printf("  Q11 resampler, %d taps a phase, %d phases, latency %d samples at 44.1 kHz: gain %+.4f dB at "
			"%.0f Hz (worst, 20 Hz-16 kHz), %+.4f dB at %.0f Hz (worst, 16-18 kHz; %+.4f at 18 kHz), THD+N %.1f dB at "
			"%.0f Hz (worst, 1-16 kHz), 22.05-24 kHz at %.1f dB (worst, %.0f Hz)\n", fast::Resample48To44::Taps,
			fast::Resample48To44::Up, fast::Resample48To44::Latency, gain, gainAt, top, topAt, at18, thd, thdAt,
			rejection, rejectionAt);
		check(std::abs(gain) <= 0.1 && std::abs(top) <= 0.1 && thd <= -70 && rejection <= -60, "Q11: the "
			"resampler's gain within 0.1 dB 20 Hz-18 kHz, THD+N <= -70 dB 1-16 kHz, 22.05-24 kHz rejected by >= 60 dB");
	}


	// ---- Host's timing at 44.1 kHz ----

	// BD MODERN's Hit 0 on every Track of a Host at _fs, Track t triggered before host block _trig[t] (-1: never),
	// _blocks host blocks, each Track's samples
	std::vector<std::vector<double>> hostHits(const float _fs, const int (&_trig)[Lanes], const int _blocks)
	{
		fast::Host<fast::BdModern> e(_fs);
		for(int t = 0; t < Lanes; ++t)
		{
			for(int i = 0; i < 8; ++i)
				e.setParameter(t, i, BdModernHits.hit[0].value[i], BdModernHits.hit[0].fine[i]);
			e.setNote(t, BdModernHits.hit[0].note);
		}
		std::vector<std::vector<double>> out(Lanes);
		std::array<std::array<float, fast::BlockSize>, Lanes> buf{};
		std::array<float*, Lanes> ptr{};
		for(int t = 0; t < Lanes; ++t)
			ptr[t] = buf[t].data();
		for(int b = 0; b < _blocks; ++b)
		{
			for(int t = 0; t < Lanes; ++t)
				if(_trig[t] == b)
					e.trig(t);
			e.render(ptr.data());
			for(int t = 0; t < Lanes; ++t)
				out[static_cast<size_t>(t)].insert(out[static_cast<size_t>(t)].end(), buf[t].begin(), buf[t].end());
		}
		return out;
	}

	// A trig before every host block of a 147-block cycle at 44.1 kHz, against the same trig at 48 kHz: BD MODERN's
	// first sample is a jump (its click) to A, D samples after its trig's 48 kHz block; at 44.1 kHz, where the jump's
	// band-limited image crosses A / 2 (interpolated linearly: within about 0.1 sample), against the trig's ideal time
	// plus the same D and the latency. Fatal: from 0.8 host samples early to 28.4 late (Host's bound), within 0.25
	// sample of measure
	void timing()
	{
		constexpr int Native = 4;	// the 48 kHz trig's block
		int trig[Lanes];
		std::fill_n(trig, Lanes, Native);
		const auto x = hostHits(48000, trig, Native + 8)[0];
		size_t n1 = 0;
		while(x[n1] == 0)
			++n1;
		const double d = static_cast<double>(n1) - fast::BlockSize * Native, a = x[n1];

		const int latency = fast::Host<fast::BdModern>(44100).latency();
		double lo = 1e9, hi = -1e9, sum = 0;
		int count = 0;
		for(int run = 0; run * Lanes < 147; ++run)
		{
			for(int t = 0; t < Lanes; ++t)
				trig[t] = run * Lanes + t < 147 ? 147 + run * Lanes + t : -1;
			const auto y = hostHits(44100, trig, 147 + 147 + 8);
			for(int t = 0; t < Lanes; ++t)
			{
				if(trig[t] < 0)
					continue;
				const auto& v = y[static_cast<size_t>(t)];
				size_t i = static_cast<size_t>(fast::BlockSize * trig[t]);
				while(v[i] * a < a * a / 2)
					++i;
				const double c = static_cast<double>(i) - 1 + (a / 2 - v[i - 1]) / (v[i] - v[i - 1]);
				const double ideal = fast::BlockSize * trig[t] + latency + (d - 0.5) * 147 / 160;
				const double e = c - ideal;
				lo = std::min(lo, e);
				hi = std::max(hi, e);
				sum += c - fast::BlockSize * trig[t];
				++count;
			}
		}
		std::printf("  Host at 44.1 kHz, a trig before each of 147 host blocks: latency %d host samples, note to sound "
			"%.2f host samples on average (BD MODERN's own %.0f at 48 kHz, %.2f here, and the latency), the trig %+.2f "
			"to %+.2f host samples from its ideal time (jitter %.2f, %.3f ms)\n", latency, sum / count, d,
			d * 147 / 160, lo, hi, hi - lo, (hi - lo) / 44.1);
		check(count == 147 && lo >= -0.8 - 0.25 && hi <= 28.4 + 0.25, "Host's trig at 44.1 kHz from 0.8 host samples "
			"early to 28.4 late, within 0.25 sample of measure");
	}

	// ---- cost ----

	void cost()
	{
		using Clock = std::chrono::steady_clock;
		fast::Resample48To44 r;
		fast::Block x{}, y;
		double us = 1e9;
		for(int rep = 0; rep < 5; ++rep)
		{
			constexpr int Blocks = 15000;
			const auto t0 = Clock::now();
			for(int b = 0; b < Blocks; ++b)
			{
				while(r.needsInput())
					r.push(x);
				r.pull(y);
			}
			us = std::min(us, std::chrono::duration<double, std::micro>(Clock::now() - t0).count() / Blocks);
		}
		std::printf("  cost, the resampler for 16 lanes per host block at 44.1 kHz: %.2f us (%.2f %% of a core)\n", us,
			100 * us / (32 / 44100.0 * 1e6));
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	q11();
	timing();
	cost();
	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
