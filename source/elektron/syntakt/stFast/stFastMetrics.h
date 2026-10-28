#pragma once

// The pure measures the fast machines' tests and tools take of a render, against the exact port's or alone: spectra,
// the null, the envelopes, the pitch, Q4's phase-blind measures, and a WAV writer. No engine, no criterion: those are
// stFastBench.h's

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace syntakt::fast::bench
{
	constexpr double Pi = 3.14159265358979323846;

	// ---- Q31 and dB ----

	inline double q31(const int32_t _v) { return _v / 2147483648.0; }
	inline int32_t toQ31(const double _v)
	{
		return static_cast<int32_t>(std::clamp<long long>(std::llround(_v * 2147483648.0), INT32_MIN, INT32_MAX));
	}
	inline double db(const double _power) { return 10 * std::log10(std::max(_power, 1e-300)); }

	// ---- metrics ----

	inline void fft(std::vector<std::complex<double>>& _a)
	{
		const size_t n = _a.size();
		for(size_t i = 1, j = 0; i < n; ++i)
		{
			size_t bit = n >> 1;
			for(; j & bit; bit >>= 1)
				j ^= bit;
			j ^= bit;
			if(i < j)
				std::swap(_a[i], _a[j]);
		}
		for(size_t len = 2; len <= n; len <<= 1)
		{
			const double ang = -2 * Pi / static_cast<double>(len);
			const std::complex<double> wl(std::cos(ang), std::sin(ang));
			for(size_t i = 0; i < n; i += len)
			{
				std::complex<double> w(1);
				for(size_t k = 0; k < len / 2; ++k)
				{
					const auto u = _a[i + k], v = _a[i + k + len / 2] * w;
					_a[i + k] = u + v;
					_a[i + k + len / 2] = u - v;
					w *= wl;
				}
			}
		}
	}

	// |X_k|^2 of _x zero-padded to a power of two of at least _min points
	inline std::vector<double> powerSpectrum(const std::vector<double>& _x, size_t _min = 0)
	{
		size_t n = 1;
		while(n < std::max(_x.size(), _min))
			n <<= 1;
		std::vector<std::complex<double>> a(n);
		for(size_t i = 0; i < _x.size(); ++i)
			a[i] = _x[i];
		fft(a);
		std::vector<double> p(n / 2);
		for(size_t k = 0; k < n / 2; ++k)
			p[k] = std::norm(a[k]);
		return p;
	}
	// Energy per third octave (centres 1000 2^(k/3), k = _first .. _last), in seconds-weighted units comparable across
	// rates: sum |X|^2 / (N fs), one-sided
	inline std::vector<double> bands(const std::vector<double>& _x, const double _fs, const int _first, const int _last)
	{
		const auto p = powerSpectrum(_x, size_t(1) << 20);	// fine bins: a band's edges cut few of them
		const double n = 2.0 * static_cast<double>(p.size());
		std::vector<double> e(static_cast<size_t>(_last - _first + 1), 0.0);
		for(size_t k = 1; k < p.size(); ++k)
		{
			const double f = static_cast<double>(k) * _fs / n;
			const double b = std::floor(3 * std::log2(f / 1000) + 0.5);
			if(b >= _first && b <= _last)
				e[static_cast<size_t>(b - _first)] += 2 * p[k] / (n * _fs);
		}
		return e;
	}

	inline double energy(const std::vector<double>& _x)
	{
		double e = 0;
		for(const double v : _x)
			e += v * v;
		return e;
	}

	struct Null
	{
		double snr, worstBand;
	};

	// SNR of _test against _ref, and the residual's loudest third octave (20 Hz to Nyquist at 48 kHz) relative to
	// _ref's energy
	inline Null null(const std::vector<double>& _ref, const std::vector<double>& _test, const double _fs)
	{
		std::vector<double> res(_ref.size());
		for(size_t i = 0; i < res.size(); ++i)
			res[i] = _test[i] - _ref[i];
		const auto rb = bands(_ref, _fs, -17, 14), eb = bands(res, _fs, -17, 14);	// 20 Hz to Nyquist
		double total = 0, worst = 0;
		for(const double v : rb)
			total += v;
		for(const double v : eb)
			worst = std::max(worst, v);
		return {db(energy(_ref) / energy(res)), db(worst / total)};
	}

	// The worst SNR of _test against _ref over 10 ms rectangular windows, one after the other, among those where _ref
	// is above its loudest window - 60 dB: the measure that set Q3's local threshold by listening (ticket 05)
	inline double localSnr(const std::vector<double>& _ref, const std::vector<double>& _test, const double _fs)
	{
		const auto w = static_cast<size_t>(std::lround(0.01 * _fs));
		std::vector<double> sig, res;
		for(size_t a = 0; a + w <= _ref.size(); a += w)
		{
			double s = 0, r = 0;
			for(size_t i = a; i < a + w; ++i)
			{
				s += _ref[i] * _ref[i];
				r += (_test[i] - _ref[i]) * (_test[i] - _ref[i]);
			}
			sig.push_back(s);
			res.push_back(r);
		}
		const double loud = *std::max_element(sig.begin(), sig.end());
		double worst = 1e9;
		for(size_t k = 0; k < sig.size(); ++k)
			if(sig[k] >= loud * 1e-6)
				worst = std::min(worst, db(sig[k] / res[k]));
		return worst;
	}

	// Mean square over 1 ms windows from sample 0, at _fs. Sample i stands for [i - 1/2, i + 1/2) / _fs and counts for
	// the part of it a window covers: whole samples would move a window's edges by up to a sample at 44.1 kHz, which
	// near a low note's zero crossing changes its RMS by tenths of a dB
	inline std::vector<double> envelope(const std::vector<double>& _x, const double _fs, const double _ms = 1)
	{
		std::vector<double> e;
		for(int k = 0;; ++k)
		{
			const double a = k * _ms * _fs / 1000 + 0.5, b = (k + 1) * _ms * _fs / 1000 + 0.5;
			if(b + 1 > static_cast<double>(_x.size()))
				break;
			double s = 0;
			for(auto i = static_cast<size_t>(a); i <= static_cast<size_t>(b); ++i)
			{
				const double w = std::min(b, i + 1.0) - std::max(a, static_cast<double>(i));
				if(w > 0)
					s += w * _x[i] * _x[i];
			}
			e.push_back(s / (b - a));
		}
		return e;
	}

	// The largest difference in dB between two envelopes where _ref is above its peak - 40 dB
	inline double envelopeDiff(const std::vector<double>& _ref, const std::vector<double>& _test)
	{
		const double peak = *std::max_element(_ref.begin(), _ref.end());
		double worst = 0;
		for(size_t k = 0; k < std::min(_ref.size(), _test.size()); ++k)
			if(_ref[k] > peak * 1e-4)
				worst = std::max(worst, std::abs(db(_test[k] / _ref[k])));
		return worst;
	}

	// The spectral peak between 20 Hz and 8 kHz, both bins included, interpolated on the log power around the largest
	// local maximum: not the range's first bin where it lies on the falling skirt of a fundamental below 20 Hz, whose
	// interpolation divides by nearly 0 (SD VINTAGE at notes 6-12). NaN where the range holds no maximum (a flat or
	// silent spectrum), which fails a pitch check
	inline double peakHz(const std::vector<double>& _x, const double _fs)
	{
		const auto p = powerSpectrum(_x, size_t(1) << 20);
		const double n = 2.0 * static_cast<double>(p.size());
		const auto lo = static_cast<size_t>(20 * n / _fs), hi = static_cast<size_t>(8000 * n / _fs);
		size_t k = 0;
		for(size_t i = lo; i <= hi; ++i)
			if(p[i] > p[i - 1] && p[i] >= p[i + 1] && (!k || p[i] > p[k]))
				k = i;
		if(!k)
			return std::nan("");
		const double a = std::log(p[k - 1]), b = std::log(p[k]), c = std::log(p[k + 1]);
		return (static_cast<double>(k) + 0.5 * (a - c) / (a - 2 * b + c)) * _fs / n;
	}

	// ---- Q4's phase-blind measures (set by the ABX of ticket 05 on 06-sweep's pairs) ----

	// Both renders from the exact port's onset, its first sample above its peak - 60 dB: a fast render whose sweep or
	// pitch drifts its phase a little nulls badly yet sounds the same, so Q4 compares what the ear follows
	inline size_t onsetOf(const std::vector<double>& _ref)
	{
		double peak = 0;
		for(const double v : _ref)
			peak = std::max(peak, std::abs(v));
		size_t i = 0;
		while(i < _ref.size() && std::abs(_ref[i]) <= peak * 1e-3)
			++i;
		return i < _ref.size() ? i : 0;
	}

	// The magnitude of _x's analytic signal (zero-padded to a power of two), smoothed by a 5 ms boxcar at 48 kHz
	inline std::vector<double> hilbertEnvelope(const std::vector<double>& _x)
	{
		size_t n = 1;
		while(n < _x.size())
			n <<= 1;
		std::vector<std::complex<double>> a(n);
		for(size_t i = 0; i < _x.size(); ++i)
			a[i] = _x[i];
		fft(a);
		for(size_t k = 1; k < n / 2; ++k)
			a[k] *= 2;
		for(size_t k = n / 2 + 1; k < n; ++k)
			a[k] = 0;
		for(auto& v : a)	// the inverse transform, by conjugation
			v = std::conj(v);
		fft(a);
		std::vector<double> e(_x.size());
		for(size_t i = 0; i < e.size(); ++i)
			e[i] = std::abs(a[i]) / static_cast<double>(n);
		constexpr int Box = 240, Before = Box / 2;	// samples i - 120 to i + 119
		std::vector<double> sum(e.size() + 1, 0.0), y(e.size());
		for(size_t i = 0; i < e.size(); ++i)
			sum[i + 1] = sum[i] + e[i];
		const auto n0 = static_cast<int64_t>(e.size());
		for(int64_t i = 0; i < n0; ++i)
		{
			const int64_t lo = std::max<int64_t>(0, i - Before), hi = std::min<int64_t>(n0, i - Before + Box);
			y[static_cast<size_t>(i)] = (sum[static_cast<size_t>(hi)] - sum[static_cast<size_t>(lo)]) / Box;
		}
		return y;
	}

	// Whether two renders can be compared sample for sample: _test at least as long as _ref, both finite. Q4's
	// measures fail (+inf) otherwise
	inline bool comparable(const std::vector<double>& _ref, const std::vector<double>& _test)
	{
		const auto finite = [](const std::vector<double>& _x)
		{
			return std::all_of(_x.begin(), _x.end(), [](const double _v) { return std::isfinite(_v); });
		};
		return _test.size() >= _ref.size() && finite(_ref) && finite(_test);
	}

	// The larger of two measures, a NaN in either kept: std::max would drop it
	inline double worseOf(const double _a, const double _b)
	{
		return std::isnan(_a) || std::isnan(_b) ? std::nan("") : std::max(_a, _b);
	}

	// The envelopes' largest difference relative to the exact port's peak, in dB: max |B - A| / max A; +inf where the
	// renders cannot be compared or the exact port is silent
	inline double envelopeAbs(const std::vector<double>& _ref, const std::vector<double>& _test)
	{
		if(!comparable(_ref, _test))
			return HUGE_VAL;
		const auto on = static_cast<std::ptrdiff_t>(onsetOf(_ref));
		const auto end = static_cast<std::ptrdiff_t>(_ref.size());
		const auto a = hilbertEnvelope({_ref.begin() + on, _ref.end()});
		const auto b = hilbertEnvelope({_test.begin() + on, _test.begin() + end});
		double peak = 0, worst = 0;
		for(size_t i = 0; i < a.size(); ++i)
		{
			peak = std::max(peak, a[i]);
			worst = worseOf(worst, std::abs(b[i] - a[i]));
		}
		if(!(peak > 0))
			return HUGE_VAL;
		return 20 * std::log10(std::max(worst, 1e-300) / peak);
	}

	// Power per third octave (centres 1000 2^(k/3), k -14 to 13, edges at +-1/6 octave, bands without a bin left out)
	// in 1024-sample Hann frames, hop 256, at 48 kHz: [frame][band]
	inline std::vector<std::vector<double>> shortTimeBands(const std::vector<double>& _x)
	{
		constexpr int N = 1024, Hop = 256;
		std::vector<std::pair<int, int>> bins;	// each band's bins, [first, last)
		for(int k = -14; k <= 13; ++k)
		{
			const double c = 1000 * std::exp2(k / 3.0);
			int first = N / 2 + 1, last = 0;
			for(int b = 0; b <= N / 2; ++b)
			{
				const double f = b * 48000.0 / N;
				if(f >= c * std::exp2(-1 / 6.0) && f < c * std::exp2(1 / 6.0))
				{
					first = std::min(first, b);
					last = b + 1;
				}
			}
			if(last > first)
				bins.emplace_back(first, last);
		}
		std::vector<std::vector<double>> s;
		std::vector<std::complex<double>> a(N);
		for(size_t at = 0; at + N <= _x.size(); at += Hop)
		{
			for(int i = 0; i < N; ++i)	// numpy's hanning: symmetric
				a[i] = _x[at + i] * (0.5 - 0.5 * std::cos(2 * Pi * i / (N - 1)));
			fft(a);
			std::vector<double> row;
			for(const auto& [first, last] : bins)
			{
				double p = 0;
				for(int b = first; b < last; ++b)
					p += std::norm(a[b]);
				row.push_back(p);
			}
			s.push_back(row);
		}
		return s;
	}

	// The short-time third octaves' largest difference in dB, over the cells where the exact port's is within _range
	// dB of its loudest cell; +inf where the renders cannot be compared or the exact port is silent
	inline double cellMax(const std::vector<double>& _ref, const std::vector<double>& _test, const double _range = 30)
	{
		if(!comparable(_ref, _test))
			return HUGE_VAL;
		const auto on = static_cast<std::ptrdiff_t>(onsetOf(_ref));
		const auto end = static_cast<std::ptrdiff_t>(_ref.size());
		const auto a = shortTimeBands({_ref.begin() + on, _ref.end()});
		const auto b = shortTimeBands({_test.begin() + on, _test.begin() + end});
		double top = 0, worst = 0;
		for(const auto& row : a)
			for(const double v : row)
				top = std::max(top, v);
		if(!(top > 0))
			return HUGE_VAL;
		for(size_t t = 0; t < a.size(); ++t)
			for(size_t k = 0; k < a[t].size(); ++k)
				if(a[t][k] > top * std::pow(10.0, -_range / 10))
					worst = worseOf(worst, std::abs(db(std::max(b[t][k], 1e-30) / a[t][k])));
		return worst;
	}

	// What Q4, the deterministic check and stFastSweep measure of a fast render (_test) against the exact port's
	struct HitMeasures
	{
		double snr, local, band;	// the null: global, worst 10 ms, worst residual third octave
		double env;					// the 1 ms envelope's largest difference above -40 dB
		double refHz, cents;		// the exact port's spectral peak, the fast render's against it
		double envAbs, cells;		// the Hilbert envelope (dB re peak), the short-time third octaves
	};

	inline HitMeasures measureHit(const std::vector<double>& _ref, const std::vector<double>& _test)
	{
		HitMeasures m{};
		const auto n = null(_ref, _test, 48000);
		m.snr = n.snr;
		m.band = n.worstBand;
		m.local = localSnr(_ref, _test, 48000);
		m.env = envelopeDiff(envelope(_ref, 48000), envelope(_test, 48000));
		m.refHz = peakHz(_ref, 48000);
		m.cents = 1200 * std::log2(peakHz(_test, 48000) / m.refHz);
		m.envAbs = envelopeAbs(_ref, _test);
		m.cells = cellMax(_ref, _test);
		return m;
	}

	// ---- WAV ----

	inline void putLe(std::ofstream& _f, const uint32_t _v, const int _bytes)
	{
		for(int i = 0; i < _bytes; ++i)
			_f.put(static_cast<char>(_v >> (8 * i)));
	}

	// 24-bit PCM, mono, at _fs, after _pre seconds of silence, _gain and a 20 ms fade at the end
	inline void writeWav(const std::string& _path, const std::vector<double>& _hit, const double _fs,
		const double _gain, const double _pre)
	{
		std::vector<double> x(static_cast<size_t>(_pre * _fs), 0.0);
		x.insert(x.end(), _hit.begin(), _hit.end());
		std::ofstream f(_path, std::ios::binary);
		const uint32_t data = static_cast<uint32_t>(x.size()) * 3, rate = static_cast<uint32_t>(_fs);
		f.write("RIFF", 4);
		putLe(f, 36 + data, 4);
		f.write("WAVEfmt ", 8);
		putLe(f, 16, 4);
		putLe(f, 1, 2);
		putLe(f, 1, 2);
		putLe(f, rate, 4);
		putLe(f, rate * 3, 4);
		putLe(f, 3, 2);
		putLe(f, 24, 2);
		f.write("data", 4);
		putLe(f, data, 4);
		const size_t fade = static_cast<size_t>(0.02 * _fs);
		for(size_t i = 0; i < x.size(); ++i)
		{
			const double left = static_cast<double>(x.size() - i);
			const double g = _gain * (left < fade ? left / static_cast<double>(fade) : 1);
			const auto v = static_cast<int32_t>(std::lround(std::clamp(x[i] * g, -1.0, 1.0) * 8388607));
			putLe(f, static_cast<uint32_t>(v), 3);
		}
	}
}
