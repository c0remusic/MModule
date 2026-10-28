#pragma once

// What the fast machines' tests measure with and play, shared with stFastAbRender (the listening test's renders): the
// metrics, the exact port's eight tracks as the voice CPU renders them, BD MODERN's 16 Hits with their events, and the
// criteria every machine is judged by (Q2-Q4, the chaotic Hits' distributions, the cost)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "stBoot.h"
#include "stEngine.h"
#include "stFastBdModern.h"
#include "stFastResample.h"
#include "stStages.h"

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

	// The spectral peak between 20 Hz and 8 kHz, interpolated on the log power around the largest bin
	inline double peakHz(const std::vector<double>& _x, const double _fs)
	{
		const auto p = powerSpectrum(_x, size_t(1) << 20);
		const double n = 2.0 * static_cast<double>(p.size());
		const auto lo = static_cast<size_t>(20 * n / _fs), hi = static_cast<size_t>(8000 * n / _fs);
		size_t k = lo;
		for(size_t i = lo; i < hi; ++i)
			if(p[i] > p[k])
				k = i;
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

	// Q4's fatal thresholds on those measures, and the null's (printed in Q4; the chaos classification's)
	constexpr double EnvelopeAbsDb = -39, CellMaxDb = 1;
	constexpr double NullSnrDb = 70, NullLocalDb = 60, NullBandDb = -60;

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

	// ---- the exact port ----

	// Eight tracks of syntakt::bdModern as the voice CPU renders them, before the track stages: CPU #1's block in,
	// renderVoices' output out (the block's layout as VoiceCpu writes it)
	class Exact
	{
	public:
		explicit Exact(const std::vector<int32_t>& _section7)
			: m_sram(std::make_unique<Sram>()), m_rom{_section7.data()}
		{
			boot(*m_sram, m_globals, m_rom);
		}

		void setParameter(const int _t, const int _i, const uint8_t _value, const uint8_t _fine)
		{
			m_block[20 * _t + 2 + 2 * _i] = _value;
			m_block[20 * _t + 3 + 2 * _i] = _fine;
		}

		void setNote(const int _t, const int _note)
		{
			const uint32_t pitch = static_cast<uint32_t>(_note) << 16;
			for(int i = 0; i < 4; ++i)
				m_block[0x150 + 4 * _t + i] = static_cast<uint8_t>(pitch >> (24 - 8 * i));
		}

		// The machine the track plays from its next trig (0 BD MODERN, 1 SD BASIC ...)
		void setMachine(const int _t, const int _machine) { m_block[20 * _t] = static_cast<uint8_t>(_machine); }

		void trig(const int _t) { m_block[0x180] |= static_cast<uint8_t>(1 << _t); }
		void gateOff(const int _t) { m_block[0x181] |= static_cast<uint8_t>(1 << _t); }

		// LSB added to unit k's increment word (0x78) after each update, before its render reads it, on every voice
		// (_inc[7], kept by the caller), null for none. Set after the constructor: the boot never runs under it
		void setIncrementNudge(const int8_t* _inc) { m_inc = _inc; }

		// Track _t's 32 samples at m_sram->ptr(VoiceOut + 0x80 t)
		void render()
		{
			int32_t trig = 0, gateOff = 0;
			receiveBlock(*m_sram, m_block.data(), trig, gateOff);
			const AfterUpdate nudge = [](Voice& _v, int32_t, void* _ctx)
			{
				const auto* inc = static_cast<const int8_t*>(_ctx);
				for(uint32_t k = 0; k < 7; ++k)
					_v.unit(k, 0x78) += inc[k];
			};
			renderVoices(*m_sram, m_globals, m_rom, VoiceOut, TrackParams, trig, gateOff, m_inc ? nudge : nullptr,
				const_cast<int8_t*>(m_inc));
			m_block[0x180] = 0;
			m_block[0x181] = 0;
		}

		const int32_t* out(const int _t) const
		{
			return m_sram->ptr(VoiceOut + TrackOutStride * static_cast<uint32_t>(_t));
		}
		void nudge(const int _t, const uint32_t _k, const int32_t _lsb)
		{
			m_sram->at(Voice::addressOf(_t) + 0x80 * _k + 0x6c) += _lsb;
		}

	private:
		std::unique_ptr<Sram> m_sram;
		Globals m_globals;
		Rom m_rom;
		std::array<uint8_t, BlockBytes> m_block{};
		const int8_t* m_inc = nullptr;
	};

	// The 16 Tracks of the exact port: two voice CPUs
	struct Exact16
	{
		Exact a, b;
		explicit Exact16(const std::vector<int32_t>& _s7) : a(_s7), b(_s7) {}
		Exact& cpu(const int _t) { return _t < 8 ? a : b; }
		void setParameter(const int _t, const int _i, const uint8_t _v, const uint8_t _f)
		{
			cpu(_t).setParameter(_t & 7, _i, _v, _f);
		}
		void setNote(const int _t, const int _n) { cpu(_t).setNote(_t & 7, _n); }
		void setMachine(const int _t, const int _m) { cpu(_t).setMachine(_t & 7, _m); }
		void trig(const int _t) { cpu(_t).trig(_t & 7); }
		void gateOff(const int _t) { cpu(_t).gateOff(_t & 7); }
		void render() { a.render(); b.render(); }
		const int32_t* out(const int _t) { return cpu(_t).out(_t & 7); }
	};

	// ---- the Hits ----

	struct Hit
	{
		uint8_t value[8];	// TUNE, U2 level, transient, SWEEP, CONTOUR, drive, HOLD, DECAY
		uint8_t fine[8];
		int note;
		bool retrig;		// a second trig at 106.7 ms (block 160 at 48 kHz, 147 at 44.1): SWEEP, drive 127, note + 2
		bool gate;			// a gate-off at 213.3 ms (block 320, 294)
	};

	const Hit Hits[Lanes] = {
		{{88, 64, 64, 64, 64, 0, 0, 64}, {}, 36, false, false},
		{{88, 0, 0, 0, 0, 0, 0, 40}, {}, 36, false, false},
		{{88, 127, 127, 127, 127, 1, 0, 90}, {}, 36, false, false},
		{{100, 32, 96, 20, 100, 0, 0, 110}, {}, 40, false, false},
		{{76, 96, 32, 110, 10, 1, 0, 70}, {}, 30, false, false},
		{{88, 64, 127, 64, 64, 1, 0, 127}, {}, 36, false, false},
		{{88, 127, 0, 127, 0, 0, 0, 100}, {}, 48, false, false},
		{{64, 80, 50, 30, 120, 0, 0, 80}, {}, 60, false, false},
		{{88, 64, 64, 64, 64, 0, 1, 90}, {}, 36, false, true},
		{{110, 20, 110, 90, 40, 1, 0, 60}, {}, 24, false, false},
		{{88, 50, 70, 50, 70, 0, 0, 96}, {128, 77, 200, 0, 33, 0, 0, 0}, 36, false, false},
		{{88, 64, 64, 64, 64, 1, 0, 100}, {}, 36, true, false},
		{{127, 40, 40, 40, 40, 0, 0, 80}, {}, 50, false, false},
		{{0, 90, 90, 90, 90, 1, 0, 85}, {}, 100, false, false},
		{{88, 110, 20, 80, 60, 1, 1, 120}, {}, 33, false, true},
		{{88, 64, 100, 0, 64, 0, 0, 100}, {}, 36, true, false},
	};

	// The Hits' events before block _b at _fs (48 or 44.1 kHz: the retrigger and the gate-off land on a block there),
	// on any engine with BdModern's interface. _hits: another machine's, whose parameters 3 and 5 the retrigger sets
	// as well
	template<typename Engine>
	void events(Engine& _e, const int _b, const double _fs, const Hit (&_hits)[Lanes] = Hits)
	{
		const int retrig = static_cast<int>(std::lround(160 * _fs / 48000));
		const int gate = 2 * retrig;
		for(int t = 0; t < Lanes; ++t)
		{
			const Hit& h = _hits[t];
			if(_b == 0)
			{
				for(int i = 0; i < 8; ++i)
					_e.setParameter(t, i, h.value[i], h.fine[i]);
				_e.setNote(t, h.note);
				_e.trig(t);
			}
			if(h.retrig && _b == retrig)
			{
				_e.setParameter(t, 3, 127, 0);
				_e.setParameter(t, 5, 127, 0);
				_e.setNote(t, h.note + 2);
				_e.trig(t);
			}
			if(h.gate && _b == gate)
				_e.gateOff(t);
		}
	}

	// ---- the criteria (research 19, section 11), each machine's test judging its voice by them ----

	inline int g_failures = 0;

	inline void check(const bool _ok, const char* _what)
	{
		std::printf("%s: %s\n", _ok ? "pass" : "FAIL", _what);
		if(!_ok)
			++g_failures;
	}

	inline void report(const bool _ok, const char* _what)
	{
		std::printf("%s (not fatal): %s\n", _ok ? "pass" : "FAIL", _what);
	}

	// Chaotic for a null (Q3's stages): the exact port against itself, its phases 1 LSB on, misses the null, SNR
	// >= 70 dB and >= 60 dB in every 10 ms window above -60 dB (localSnr). No rewrite can null where it cannot
	inline bool chaoticForNull(const double _snr, const double _local)
	{
		return _snr < NullSnrDb || _local < NullLocalDb;
	}

	// Chaotic for Q4 (a Hit, a sweep's value): the exact port against itself, its phases 1 LSB on (_self), fails Q4's
	// own measures, the Hilbert envelope or the short-time third octaves. A voice whose nudge only drifts its phase
	// sounds the same and is judged as it plays. _self: HitMeasures or PhaseBlind
	template<typename Measures>
	bool chaoticSelf(const Measures& _self)
	{
		return !(_self.envAbs <= EnvelopeAbsDb && _self.cells <= CellMaxDb);
	}

	// ---- Q4's tolerance: the exact port against its own increments ----

	// Q4's two measures alone (no null, no pitch): what the increment nudges are measured by
	struct PhaseBlind
	{
		double envAbs, cells;
	};

	inline PhaseBlind phaseBlind(const std::vector<double>& _ref, const std::vector<double>& _test)
	{
		return {envelopeAbs(_ref, _test), cellMax(_ref, _test)};
	}

	// Where one LSB of a frequency word moves the exact port past Q4, a rewrite that is not bit-exact moves as far: a
	// voice not chaotic passes within max(-39 dB, the worst of the exact port against its increment nudges + 20 log10
	// _k) and max(1 dB, _k times their worst cells), at most -20 dB and 4 dB: a Hit whose nudges decorrelate it would
	// otherwise pass anything. The cap judges the fast engine only: the calibration of k measures the exact port's
	// own nudges, uncapped (capped, CY ALLOY's sweep, whose nudges move many values past -20 dB, would need k past 4)
	constexpr double EnvelopeAbsCapDb = -20, CellsCapDb = 4;

	inline PhaseBlind worstOf(const std::vector<PhaseBlind>& _nudges)
	{
		PhaseBlind w{-1e9, 0};
		for(const auto& n : _nudges)
			w = {worseOf(w.envAbs, n.envAbs), worseOf(w.cells, n.cells)};
		return w;
	}

	inline double envelopeLimit(const PhaseBlind& _tolerance, const double _k, const bool _cap = true)
	{
		const double limit = std::max(EnvelopeAbsDb, _tolerance.envAbs + 20 * std::log10(_k));
		return _cap ? std::min(EnvelopeAbsCapDb, limit) : limit;
	}

	inline double cellsLimit(const PhaseBlind& _tolerance, const double _k, const bool _cap = true)
	{
		const double limit = std::max(CellMaxDb, _k * _tolerance.cells);
		return _cap ? std::min(CellsCapDb, limit) : limit;
	}

	// Whether either limit is held at its cap
	inline bool atCap(const PhaseBlind& _tolerance, const double _k)
	{
		return envelopeLimit(_tolerance, _k, false) > EnvelopeAbsCapDb || cellsLimit(_tolerance, _k, false) > CellsCapDb;
	}

	inline bool withinTolerance(const PhaseBlind& _m, const PhaseBlind& _tolerance, const double _k,
		const bool _cap = true)
	{
		return _m.envAbs <= envelopeLimit(_tolerance, _k, _cap) && _m.cells <= cellsLimit(_tolerance, _k, _cap);
	}

	// The factor k that lets each held-out increment nudge pass against the tolerance of the others at least 95 % of
	// the time, from 1 up in steps of 0.25, at most 4 (not reached there: fatal, and 4 is used). Only the held-out
	// nudges past Q4's fixed limits are counted, the others passing by them whatever k: none, and k is 1. _sets: per
	// Hit not chaotic, its nudges' measures against the exact port
	inline double calibrateTolerance(const std::vector<std::vector<PhaseBlind>>& _sets, const char* _what)
	{
		constexpr double MaxK = 4;
		double k = 1, rate = 1;
		int total = 0;
		for(;; k += 0.25)
		{
			int pass = 0;
			total = 0;
			for(const auto& set : _sets)
				for(size_t i = 0; i < set.size(); ++i)
				{
					if(set[i].envAbs <= EnvelopeAbsDb && set[i].cells <= CellMaxDb)
						continue;
					std::vector<PhaseBlind> others(set);
					others.erase(others.begin() + static_cast<std::ptrdiff_t>(i));
					pass += withinTolerance(set[i], worstOf(others), k, false);
					++total;
				}
			rate = total ? static_cast<double>(pass) / total : 1;
			if(rate >= 0.95 || k >= MaxK)
				break;
		}
		std::printf("  %s: tolerance factor k %.2f, the exact port's held-out increment nudges passing %.1f %% (%d "
			"past Q4's fixed limits, over %zu Hits)\n", _what, k, 100 * rate, total, _sets.size());
		check(rate >= 0.95, "tolerance: the exact port's held-out increment nudges pass at least 95 % within k = 4");
		return k;
	}

	// A threshold set by the listening tests of ticket 05 (elektron-drums-v1)
	inline void checkValidated(const bool _ok, const char* _what)
	{
		std::printf("%s (listening-validated, ticket 05): %s\n", _ok ? "pass" : "FAIL", _what);
		if(!_ok)
			++g_failures;
	}

	// ---- a chaotic Hit, judged by its distributions ----

	// Where a voice is chaotic, no rewrite matches the exact port, which does not match itself either: it is judged by
	// its mean third-octave spectrum (bands 20 Hz to 18 kHz, index k + 17 for centre 1000 2^(k/3)) and its
	// 10 ms RMS envelope, against the exact port's own dispersion under 1-LSB perturbations of its phases
	constexpr int Band10k = 9 + 17, Band18k = 12 + 17;	// the last band below 10 kHz, below 18 kHz

	struct Distribution
	{
		std::vector<double> bands, env;
	};

	inline Distribution distribution(const std::vector<double>& _x, const double _fs)
	{
		return {bands(_x, _fs, -17, 12), envelope(_x, _fs, 10)};
	}

	// How far _test's distribution is from _ref's: per band in dB where _ref's is above its loudest - 60 dB (-1
	// elsewhere), and the envelope's largest difference where _ref's is above its peak - 40 dB
	struct Deviation
	{
		std::vector<double> bands;
		double env;
	};

	inline Deviation deviation(const Distribution& _ref, const Distribution& _test)
	{
		Deviation d{std::vector<double>(_ref.bands.size(), -1), envelopeDiff(_ref.env, _test.env)};
		const double loud = *std::max_element(_ref.bands.begin(), _ref.bands.end());
		for(size_t k = 0; k < d.bands.size(); ++k)
			if(_ref.bands[k] >= loud * 1e-6)
				d.bands[k] = std::abs(db(_test.bands[k] / _ref.bands[k]));
		return d;
	}

	// _test against _ref passes where its deviation is at most _factor times the exact port's worst against its
	// perturbed renders plus 0.5 dB, in every band up to _last and on the envelope. The worst band is the one nearest
	// its limit
	struct Judgement
	{
		bool ok;
		double band, bandDispersion, bandHz, env, envDispersion, factor;

		double limit(const double _dispersion) const { return factor * _dispersion + 0.5; }
	};

	inline Judgement judge(const Deviation& _test, const std::vector<Deviation>& _dispersion, const int _last,
		const double _factor)
	{
		Judgement j{true, 0, 0, 0, _test.env, 0, _factor};
		for(const auto& d : _dispersion)
			j.envDispersion = std::max(j.envDispersion, d.env);
		double margin = -1e9;
		for(int k = 0; k <= _last; ++k)
		{
			if(_test.bands[static_cast<size_t>(k)] < 0)
				continue;
			double spread = 0;
			for(const auto& d : _dispersion)
				spread = std::max(spread, d.bands[static_cast<size_t>(k)]);
			const double m = _test.bands[static_cast<size_t>(k)] - j.limit(spread);
			if(m > margin)
			{
				margin = m;
				j.band = _test.bands[static_cast<size_t>(k)];
				j.bandDispersion = spread;
				j.bandHz = 1000 * std::exp2((k - 17) / 3.0);
			}
		}
		j.ok = margin <= 0 && j.env <= j.limit(j.envDispersion);
		return j;
	}

	inline void printJudgement(const char* _what, const Judgement& _j)
	{
		std::printf("    %s: spectrum %.2f dB at %.0f Hz (the exact port's own dispersion %.2f dB, limit %.2f), "
			"envelope %.2f dB (dispersion %.2f, limit %.2f): %s\n", _what, _j.band, _j.bandHz, _j.bandDispersion,
			_j.limit(_j.bandDispersion), _j.env, _j.envDispersion, _j.limit(_j.envDispersion), _j.ok ? "pass" : "FAIL");
	}

	// The nudges of the exact port a chaotic voice's dispersion comes from: its phases 1 to 8 LSB either way, and 9
	// more. The calibration holds each of the 17 out against the other 16; the engine is judged against the first 16,
	// as many as a held-out nudge
	constexpr int32_t Nudges[] = {1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6, -6, 7, -7, 8, -8, 9};
	constexpr size_t Judged = 16;

	// The first Judged of _set: the dispersion an engine is judged against
	inline std::vector<Deviation> judgedSpread(const std::vector<Deviation>& _set)
	{
		return {_set.begin(), _set.begin() + static_cast<std::ptrdiff_t>(std::min(Judged, _set.size()))};
	}

	// The factor of the limit that lets the exact port's own nudges pass at least 95 % of the time: each of _sets'
	// deviations (a chaotic Hit's or lane's nudges against its unperturbed render) judged against the others'
	// dispersion, the factor from 2 up in steps of 0.25 until they do, at most 4: not reached there, the judgement
	// itself fails (fatal) and 4 is used. Prints it and the pass rate
	inline double calibrate(const std::vector<std::vector<Deviation>>& _sets, const int _last, const char* _what)
	{
		constexpr double MaxFactor = 4;
		double factor = 2, rate = 1;
		for(;; factor += 0.25)
		{
			int pass = 0, total = 0;
			for(const auto& set : _sets)
				for(size_t i = 0; i < set.size(); ++i)
				{
					std::vector<Deviation> others(set);
					others.erase(others.begin() + static_cast<std::ptrdiff_t>(i));
					pass += judge(set[i], others, _last, factor).ok;
					++total;
				}
			rate = total ? static_cast<double>(pass) / total : 1;
			if(rate >= 0.95 || factor >= MaxFactor)
				break;
		}
		std::printf("  %s: limit factor %.2f, the exact port's held-out nudges passing %.1f %% (%zu sets of %zu)\n",
			_what, factor, 100 * rate, _sets.size(), _sets.empty() ? size_t(0) : _sets.front().size());
		check(rate >= 0.95, "calibration: the exact port's held-out nudges pass at least 95 % within a factor of 4");
		return factor;
	}

	// _seconds of a fast machine's 16 Hits (_hits) at _fs, 48 or 44.1 kHz (Host), past the resampler's latency: from
	// the onset (the block after the trig's), or from the trig (_fromTrig), the instant both rates share, the onset
	// coming 32 samples later at 48 kHz and 29.4 at 44.1. _setup(machine) before the first block (a test hook)
	template<typename Machine, typename Setup>
	std::vector<std::vector<double>> fastHits(const double _fs, const double _seconds, const Hit (&_hits)[Lanes],
		Setup&& _setup, const bool _fromTrig = false)
	{
		Host<Machine> e(static_cast<float>(_fs));
		_setup(e);
		const int samples = static_cast<int>(_seconds * _fs);
		// The onset at 44.1 kHz is 29.4 samples after the trig: from the 29th, 0.4 sample before it
		const int skip = e.latency() + (_fromTrig ? 0 : static_cast<int>(std::lround(BlockSize * _fs / 48000)));
		const int blocks = (samples + skip) / BlockSize + 1;
		std::vector<std::vector<double>> out(Lanes);
		std::array<std::array<float, BlockSize>, Lanes> buf{};
		std::array<float*, Lanes> ptr{};
		for(int t = 0; t < Lanes; ++t)
			ptr[t] = buf[t].data();
		for(int b = 0; b < blocks; ++b)
		{
			events(e, b, _fs, _hits);
			e.render(ptr.data());
			for(int t = 0; t < Lanes; ++t)
				out[static_cast<size_t>(t)].insert(out[static_cast<size_t>(t)].end(), buf[t].begin(), buf[t].end());
		}
		for(auto& o : out)
			o = std::vector<double>(o.begin() + skip, o.begin() + skip + samples);
		return out;
	}

	// _seconds of the exact port's 16 Hits at 48 kHz, _events(e, block) before each block, from the onset (the block
	// after the trig's). _nudge: every unit's phase _nudge LSB (2^-24 cycle) on after each trig's onset block (the
	// trig at block 0 and the retrigger at 160, whose trig restarts the phases: a nudge before it would be undone),
	// what the exact port nulls to against itself: the floor of a null where the voice is chaotic. Its inc: LSB added
	// to each unit's increment word throughout, after each update (Exact::setIncrementNudge): the frequency word a
	// table or the tune moves when it is a little off
	struct Nudge
	{
		int32_t phase = 0;
		int8_t inc[7] = {};
	};

	inline Nudge phaseNudge(const int32_t _lsb)
	{
		Nudge n;
		n.phase = _lsb;
		return n;
	}

	//
	// The increment nudges, each up (even index) and down (odd): each of units 0-3 1 LSB alone, all four, then each
	// pair of them. A unit alone is what one table's entry moves (a ratio), all four what the pitch moves (TUNE, the
	// note), a pair what one entry read by two units moves (CY ALLOY's Spread: unit 2 and, through a product, unit 0)
	constexpr int Units4 = 4;
	constexpr int IncrementNudgeCount = 2 * (Units4 + 1 + Units4 * (Units4 - 1) / 2);	// 22

	// Whether nudge _i (of IncrementNudgeCount) moves unit _k
	constexpr bool incrementNudgeMoves(const int _i, const int _k)
	{
		const int set = _i / 2;	// 0-3 a unit alone, 4 all four, 5-10 the pairs (0 1) (0 2) (0 3) (1 2) (1 3) (2 3)
		if(_k >= Units4)
			return false;
		if(set < Units4)
			return _k == set;
		if(set == Units4)
			return true;
		int pair = set - Units4 - 1;
		for(int a = 0; a < Units4; ++a)
			for(int b = a + 1; b < Units4; ++b)
				if(pair-- == 0)
					return _k == a || _k == b;
		return false;
	}

	inline Nudge incrementNudge(const int _i)
	{
		Nudge n;
		for(int k = 0; k < 7; ++k)
			n.inc[k] = static_cast<int8_t>(incrementNudgeMoves(_i, k) ? (_i % 2 ? -1 : 1) : 0);
		return n;
	}

	template<typename Events>
	std::vector<std::vector<double>> exactHits(const std::vector<int32_t>& _s7, const double _seconds,
		const Nudge& _n, Events&& _events)
	{
		const int32_t nudge = _n.phase;
		Exact16 e(_s7);
		e.a.setIncrementNudge(_n.inc);
		e.b.setIncrementNudge(_n.inc);
		const int samples = static_cast<int>(_seconds * 48000);
		const int blocks = samples / BlockSize + 2;
		std::vector<std::vector<double>> out(Lanes);
		for(int b = 0; b < blocks; ++b)
		{
			_events(e, b);
			e.render();
			if(nudge && (b == 2 || b == 162))
				for(int t = 0; t < 8; ++t)
					for(uint32_t k = 0; k < 4; ++k)
					{
						e.a.nudge(t, k, nudge);
						e.b.nudge(t, k, nudge);
					}
			for(int t = 0; t < Lanes; ++t)
				for(int n = 0; n < BlockSize; ++n)
					out[t].push_back(q31(e.out(t)[n]));
		}
		for(auto& o : out)
			o = std::vector<double>(o.begin() + BlockSize, o.begin() + BlockSize + samples);
		return out;
	}

	// ---- Q2 and Q3: a stage against its integer original ----

	// A booted voice CPU's SRAM for the integer stages, 16 voices' fields on two of them
	struct Stage
	{
		std::unique_ptr<Sram> sram[2];
		Globals g;
		Rom rom;

		explicit Stage(const std::vector<int32_t>& _s7) : rom{_s7.data()}
		{
			for(auto& s : sram)
			{
				s = std::make_unique<Sram>();
				boot(*s, g, rom);
			}
		}

		Voice voice(const int _l) { return Voice{*sram[_l / 8], Voice::addressOf(_l & 7)}; }
	};

	struct Residual
	{
		double sum = 0, max = 0;
		size_t n = 0;

		void add(const double _ref, const double _test)
		{
			const double d = _test - _ref;
			sum += d * d;
			max = std::max(max, std::abs(d));
			++n;
		}

		bool print(const char* _stage)
		{
			const double rms = db(sum / static_cast<double>(n)), peak = 20 * std::log10(std::max(max, 1e-300));
			std::printf("  Q2 %-22s residual RMS %7.1f dBFS, max %7.1f dBFS\n", _stage, rms, peak);
			return rms <= -100 && peak <= -90;
		}
	};

	// A stage's output over a Hit on each lane, integer and float, compared lane by lane
	struct Runs
	{
		std::vector<std::vector<double>> ref = std::vector<std::vector<double>>(Lanes);
		std::vector<std::vector<double>> test = std::vector<std::vector<double>>(Lanes);

		bool print(const char* _stage)
		{
			double snr = 1e9, local = 1e9, band = -1e9;
			for(int l = 0; l < Lanes; ++l)
			{
				if(ref[l].empty())	// a lane judged otherwise
					continue;
				const auto n = null(ref[l], test[l], 48000);
				snr = std::min(snr, n.snr);
				local = std::min(local, localSnr(ref[l], test[l], 48000));
				band = std::max(band, n.worstBand);
			}
			std::printf("  Q3 %-28s SNR min %6.1f dB, worst 10 ms %6.1f dB, worst residual third octave %7.1f dB\n",
				_stage, snr, local, band);
			return snr >= 70 && local >= 60 && band <= -60;
		}
	};

	// The increments of a swept Hit, in 24-bit phase units: lane _l's note 24 + 3 l, times _octave
	inline int32_t sweptIncrement(const int _l, const int _b, const double _octave)
	{
		const double sweep = 1 + (2 + _l) * std::pow(0.97, _b);
		const double hz = 440 * std::exp2((24 + 3 * _l - 69) / 12.0) * _octave * sweep;
		return static_cast<int32_t>(hz / 48000 * 16777216.0);
	}

	// Q3 of unit 0's sine with self-feedback (0x400035de) over 1 s, feedback from a machine's table at _table across
	// its range (lane l at entry 8 l + 7 of its _parameter, shifted right by _shift as the machine's update does), read
	// from the 256-entry sine the engines read. Lanes where the feedback makes the oscillator chaotic are judged by
	// their distributions; _ok and _chaoticOk take the verdicts
	inline void q3FeedbackSine(Stage& _st, const uint32_t _table, const char* _parameter, bool& _ok, bool& _chaoticOk,
		const int _shift = 0)
	{
		constexpr int Blocks = 1500;
		const Rom& rom = _st.rom;
		const auto inc24 = sweptIncrement;
		Runs r;
		alignas(16) uint32_t phase[Lanes] = {};
		fast::Increments inc;
		int32_t fb[Lanes] = {}, target[Lanes];
		int32_t last[Lanes] = {};
		for(int b = 0; b < Blocks; ++b)
		{
			fast::Block out;
			for(int l = 0; l < Lanes; ++l)
			{
				Voice v = _st.voice(l);
				const int32_t f = rom.entry(_table, 8 * l + 7) >> _shift;
				if(b == 0)
				{
					v.at(0x28) = 0;
					v.at(0x30) = 0;
					v.unit(0, Voice::Phase) = 0;
				}
				v.at(0x2c) = f;
				v.unit(0, 0x78) = inc24(l, b, 2);
				for(auto& s : inc.s)
					s[l] = static_cast<uint32_t>(v.unit(0, 0x78)) << 8;
				target[l] = f;	// the machine's feedback word, its 0x2c
				feedbackOscillator(0, v, rom);
				for(int n = 0; n < BlockSize; ++n)
					r.ref[l].push_back(q31(v.sram.at(bufferB(0) + 4 * n)));
			}
			fast::feedbackSine(out, phase, inc, fb, target, last);
			for(int l = 0; l < Lanes; ++l)
				for(int n = 0; n < BlockSize; ++n)
					r.test[l].push_back(out.s[n][l]);
		}
		// Where the feedback is strong the oscillator is chaotic: the exact port, its first phase 1 LSB (2^-24 cycle)
		// on, misses the null against itself. Those lanes are judged by their distributions against the exact port's
		// dispersion under 16 such perturbations (Nudges), the limit's factor calibrated on 17 (calibrate)
		const auto exact = [&](const int _l, const int32_t _phase)
		{
			std::vector<double> out;
			Voice v = _st.voice(_l);
			v.at(0x28) = 0;
			v.at(0x30) = 0;
			v.unit(0, Voice::Phase) = _phase & 0xffffff;
			for(int b = 0; b < Blocks; ++b)
			{
				v.at(0x2c) = rom.entry(_table, 8 * _l + 7) >> _shift;
				v.unit(0, 0x78) = inc24(_l, b, 2);
				feedbackOscillator(0, v, rom);
				for(int n = 0; n < BlockSize; ++n)
					out.push_back(q31(v.sram.at(bufferB(0) + 4 * n)));
			}
			return out;
		};
		Runs clean;
		std::vector<int> chaotic;
		std::vector<std::vector<Deviation>> spreads;
		for(int l = 0; l < Lanes; ++l)
		{
			const auto self = exact(l, 1);
			const double selfSnr = null(r.ref[l], self, 48000).snr, selfLocal = localSnr(r.ref[l], self, 48000);
			const bool isChaotic = chaoticForNull(selfSnr, selfLocal);
			std::printf("  Q3 feedback sine, %s %3d (feedback %.4f): SNR %6.1f dB, worst 10 ms %6.1f dB; "
				"the exact port against itself 1 LSB off: %6.1f, %6.1f dB%s\n", _parameter, 8 * l + 7,
				q31(rom.entry(_table, 8 * l + 7) >> _shift), null(r.ref[l], r.test[l], 48000).snr,
				localSnr(r.ref[l], r.test[l], 48000), selfSnr, selfLocal, isChaotic ? ", chaotic" : "");
			if(!isChaotic)
			{
				clean.ref[static_cast<size_t>(l)] = r.ref[l];
				clean.test[static_cast<size_t>(l)] = r.test[l];
				continue;
			}
			const auto ref = distribution(r.ref[l], 48000);
			std::vector<Deviation> spread;
			for(const int32_t p : Nudges)
				spread.push_back(deviation(ref, distribution(p == 1 ? self : exact(l, p), 48000)));
			chaotic.push_back(l);
			spreads.push_back(spread);
		}
		if(!chaotic.empty())
		{
			const double factor = calibrate(spreads, Band18k, "Q3 chaotic feedback sine");
			for(size_t i = 0; i < chaotic.size(); ++i)
			{
				const int l = chaotic[i];
				const auto j = judge(deviation(distribution(r.ref[l], 48000), distribution(r.test[l], 48000)),
					judgedSpread(spreads[i]), Band18k, factor);
				std::printf("  Q3 feedback sine, %s %3d,", _parameter, 8 * l + 7);
				printJudgement("distributions", j);
				_chaoticOk &= j.ok;
			}
		}
		_ok &= clean.print("feedback sine (unit 0), not chaotic");
	}

	// ---- Q4: the voice against the exact port ----

	// The exact port's 16 Hits at 48 kHz, and the same with every unit's phase nudged as Nudges says after the
	// onsets (_render(nudge)), and with its increments moved by the increment nudges. A Hit is chaotic where the first
	// phase nudge fails Q4's own measures against the unperturbed render (chaoticSelf); there, 16 phase nudges give
	// the dispersion its distributions are judged against, with the limit's factor calibrated on all 17, below 18 kHz.
	// Elsewhere the increment nudges give its tolerance, with k calibrated on them (calibrateTolerance) (Q4)
	struct Exacts
	{
		std::vector<std::vector<double>> ref;
		bool chaotic[Lanes];
		double selfSnr[Lanes], selfLocal[Lanes], selfEnv[Lanes], selfEnvAbs[Lanes], selfCells[Lanes];
		PhaseBlind tolerance[Lanes]{};		// the worst of the exact port against its increment nudges
		std::vector<Deviation> spread[Lanes];	// the nudges' deviations, on a chaotic Hit
		double factor = 2, k = 1;

		template<typename Render>
		explicit Exacts(Render&& _render) : ref(_render(Nudge{}))
		{
			// The first phase nudge classifies; the other 16 are rendered only for a chaotic Hit's dispersion, the
			// increment nudges only for the tolerance of a Hit that is not
			std::vector<std::vector<std::vector<double>>> nudged{_render(phaseNudge(Nudges[0]))}, increments;
			bool anyChaotic = false, anyNot = false;
			for(int l = 0; l < Lanes; ++l)
			{
				const auto self = measureHit(ref[l], nudged[0][l]);
				selfSnr[l] = self.snr;
				selfLocal[l] = self.local;
				selfEnv[l] = self.env;
				selfEnvAbs[l] = self.envAbs;
				selfCells[l] = self.cells;
				chaotic[l] = chaoticSelf(self);
				(chaotic[l] ? anyChaotic : anyNot) = true;
			}
			for(size_t i = 1; anyChaotic && i < std::size(Nudges); ++i)
				nudged.push_back(_render(phaseNudge(Nudges[i])));
			for(int i = 0; anyNot && i < IncrementNudgeCount; ++i)
				increments.push_back(_render(incrementNudge(i)));
			std::vector<std::vector<Deviation>> sets;
			std::vector<std::vector<PhaseBlind>> tolerances;
			for(int l = 0; l < Lanes; ++l)
			{
				if(!chaotic[l])
				{
					std::vector<PhaseBlind> set;
					for(const auto& n : increments)
						set.push_back(phaseBlind(ref[l], n[l]));
					tolerance[l] = worstOf(set);
					tolerances.push_back(set);
					continue;
				}
				const auto r = distribution(ref[l], 48000);
				for(const auto& n : nudged)
					spread[l].push_back(deviation(r, distribution(n[l], 48000)));
				sets.push_back(spread[l]);
			}
			if(!tolerances.empty())
				k = calibrateTolerance(tolerances, "Q4 Hits not chaotic");
			if(!sets.empty())
				factor = calibrate(sets, Band18k, "Q4 chaotic Hits, below 18 kHz");
		}

		// The exact port's dispersion on Hit _l: its perturbed renders' deviations from its own
		std::vector<Deviation> dispersion(const int _l) const { return judgedSpread(spread[_l]); }
	};

	// Q4: the fast engine's 16 Hits at 48 kHz (_test) against the exact port's
	inline void q4(const Exacts& _exact, const std::vector<std::vector<double>>& _test)
	{
		const auto& ref = _exact.ref;
		const auto& test = _test;
		// The worst over the Hits not chaotic; the smaller the worse for the null's SNRs, as negated maxima
		double snr = -1e9, local = -1e9, band = -1e9, env = 0, cents = 0, envAbs = -1e9, cells = 0;
		int judged = 0;
		bool chaoticOk = true, envOk = true, cellsOk = true;
		std::string below, byTolerance, capped;
		std::printf("  Q4 Hit  SNR dB  10 ms dB  worst 1/3 oct dB  envelope dB  pitch Hz (exact)  cents  envelope re "
			"peak dB  1/3 oct cells dB  | the exact port against itself 1 LSB off: SNR dB, 10 ms dB, envelope dB, "
			"envelope re peak dB, 1/3 oct cells dB; the limits from its increment nudges (k %.2f): envelope dB, cells "
			"dB\n", _exact.k);
		for(int l = 0; l < Lanes; ++l)
		{
			const auto m = measureHit(ref[l], test[l]);
			const PhaseBlind& tol = _exact.tolerance[l];	// read only where not chaotic
			const bool clean = !_exact.chaotic[l];
			const double envLimit = clean ? envelopeLimit(tol, _exact.k) : 0;
			const double cellLimit = clean ? cellsLimit(tol, _exact.k) : 0;
			std::printf("     %2d  %6.1f  %8.1f  %16.1f  %11.3f  %16.2f  %+5.2f  %17.1f  %16.2f  | %6.1f  %6.1f  "
				"%6.3f  %6.1f  %6.2f; %6.1f  %6.2f%s\n", l, m.snr, m.local, m.band, m.env, m.refHz, m.cents, m.envAbs,
				m.cells, _exact.selfSnr[l], _exact.selfLocal[l], _exact.selfEnv[l], _exact.selfEnvAbs[l],
				_exact.selfCells[l], envLimit, cellLimit, _exact.chaotic[l] ? "  chaotic" : "");
			if(_exact.chaotic[l])
			{
				const auto j = judge(deviation(distribution(ref[l], 48000), distribution(test[l], 48000)),
					_exact.dispersion(l), Band18k, _exact.factor);
				printJudgement("distributions", j);
				chaoticOk &= j.ok;
				continue;
			}
			++judged;
			cents = worseOf(cents, std::abs(m.cents));	// a chaotic Hit's pitch is judged by its spectrum
			snr = worseOf(snr, -m.snr);
			local = worseOf(local, -m.local);
			band = worseOf(band, m.band);
			env = worseOf(env, m.env);
			envAbs = worseOf(envAbs, m.envAbs);
			cells = worseOf(cells, m.cells);
			envOk &= m.envAbs <= envLimit;
			cellsOk &= m.cells <= cellLimit;
			const bool fixed = m.envAbs <= EnvelopeAbsDb && m.cells <= CellMaxDb;
			if(!fixed && withinTolerance({m.envAbs, m.cells}, tol, _exact.k))
				byTolerance += (byTolerance.empty() ? "" : ", ") + std::to_string(l);
			if(atCap(tol, _exact.k))
				capped += (capped.empty() ? "" : ", ") + std::to_string(l);
			if(!(m.snr >= NullSnrDb && m.local >= NullLocalDb))
				below += (below.empty() ? "" : ", ") + std::to_string(l);
		}
		snr = -snr;
		local = -local;
		if(judged)
			std::printf("  Q4 worst where not chaotic: SNR %.1f dB, worst 10 ms %.1f dB, residual third octave %.1f "
				"dB, envelope %.3f dB; envelope %.1f dB re its peak, third octave cells %.2f dB; pitch %.2f cents\n",
				snr, local, band, env, envAbs, cells, cents);
		else
			std::printf("  Q4: every Hit chaotic, none judged on the null, the envelopes, the third octave cells or "
				"the pitch\n");
		std::printf("  Q4 Hits not chaotic below 70 dB of SNR or 60 dB in a 10 ms window: %s\n",
			below.empty() ? "none" : below.c_str());
		std::printf("  Q4 Hits past -39 dB or 1 dB that pass within the exact port's own tolerance: %s\n",
			byTolerance.empty() ? "none" : byTolerance.c_str());
		std::printf("  Q4 Hits whose limits are held at the cap (-20 dB, 4 dB), judged there: %s\n",
			capped.empty() ? "none" : capped.c_str());
		report(snr >= NullSnrDb && local >= NullLocalDb && band <= NullBandDb, "Q4: the voice where not chaotic, SNR "
			">= 70 dB, >= 60 dB in every 10 ms window above -60 dB, no residual third octave above -60 dB");
		report(env <= 0.1, "Q4: the voice where not chaotic, 1 ms envelope within 0.1 dB above -40 dB");
		checkValidated(envOk, "Q4: the voice where not chaotic, Hilbert envelope (5 ms) within -39 dB of the exact "
			"port's peak, or within the exact port's against its increment nudges + 20 log10 k");
		checkValidated(cellsOk, "Q4: the voice where not chaotic, third octaves in 1024-sample frames within 1 dB "
			"where the exact port's are within 30 dB of its loudest, or within k times the exact port's against its "
			"increment nudges");
		checkValidated(cents <= 1, "Q4: the voice's pitch within 1 cent");
		checkValidated(chaoticOk, "Q4: the chaotic Hits' distributions within the calibrated factor x the exact port's "
			"dispersion + 0.5 dB");
	}

	// ---- Q15: 44.1 against 48 kHz, its envelopes read on one sample grid ----

	// A Kaiser window (_beta) at _r in [-1, 1]
	inline double kaiser(const double _beta, const double _r)
	{
		const auto i0 = [](const double _v)
		{
			double sum = 1, term = 1;
			for(int k = 1; k < 60; ++k)
			{
				term *= (_v / (2 * k)) * (_v / (2 * k));
				sum += term;
			}
			return sum;
		};
		return i0(_beta * std::sqrt(std::max(0.0, 1 - _r * _r))) / i0(_beta);
	}

	// _x at 44.1 kHz brought to 48 kHz for measuring, never for listening: band-limited interpolation by a sinc at
	// 21.5 kHz under a Kaiser window (beta 10) of 1024 taps at 44.1 kHz (11.6 ms), in 160 exact phases, the input 0
	// outside. An RMS envelope read on two sample grids is not one measure: on a narrow-band sound near 10-18 kHz,
	// 1 ms windows of 44.1 and 48 samples differ by up to 0.28 dB where the sound is the same (stFastCyAlloyTest).
	// Output sample n stands for 44.1 kHz time n 147 / 160, as the 48 kHz render's sample n does
	inline std::vector<double> to48k(const std::vector<double>& _x)
	{
		constexpr int Half = 512, Phases = 160, Up = 147;
		static const std::vector<double> h = []
		{
			constexpr double Cutoff = 21500 / 44100.0, Beta = 10;
			std::vector<double> t(static_cast<size_t>(Phases * 2 * Half));
			for(int p = 0; p < Phases; ++p)
				for(int k = -Half + 1; k <= Half; ++k)
				{
					const double x = k - p / static_cast<double>(Phases);	// taps at the input's samples i0 + k
					const double sinc = x == 0 ? 2 * Cutoff : std::sin(2 * Pi * Cutoff * x) / (Pi * x);
					t[static_cast<size_t>(p * 2 * Half + k + Half - 1)] = sinc * kaiser(Beta, x / Half);
				}
			return t;
		}();
		const auto n = static_cast<int64_t>(_x.size());
		std::vector<double> y(static_cast<size_t>(n * Phases / Up));
		for(int64_t m = 0; m < static_cast<int64_t>(y.size()); ++m)
		{
			const int64_t at = m * Up / Phases;
			const auto p = static_cast<int>(m * Up % Phases);
			const double* c = &h[static_cast<size_t>(p * 2 * Half)];
			double s = 0;
			for(int k = -Half + 1; k <= Half; ++k)
			{
				const int64_t i = at + k;
				if(i >= 0 && i < n)
					s += c[k + Half - 1] * _x[static_cast<size_t>(i)];
			}
			y[static_cast<size_t>(m)] = s;
		}
		return y;
	}

	// to48k's own error, fatal: a known band-limited sound (six decaying partials, 100 Hz to 20.5 kHz, a 2 ms
	// raised-cosine attack) sampled at 44.1 kHz and brought to 48, against the same sound sampled at 48 kHz: its 1 ms
	// and 10 ms envelopes within 0.002 dB above -40 dB
	inline void checkTo48k()
	{
		const auto sound = [](const double _fs)
		{
			constexpr double Hz[6] = {100, 1000, 5000, 12000, 18000, 20500};
			constexpr double Tau[6] = {0.8, 0.5, 0.3, 0.2, 0.15, 0.1};
			std::vector<double> x(static_cast<size_t>(_fs));	// 1 s
			for(size_t i = 0; i < x.size(); ++i)
			{
				const double t = static_cast<double>(i) / _fs;
				const double attack = t < 0.002 ? 0.5 - 0.5 * std::cos(Pi * t / 0.002) : 1;
				double v = 0;
				for(int k = 0; k < 6; ++k)
					v += 0.15 * std::exp(-t / Tau[k]) * std::sin(2 * Pi * Hz[k] * t + k);
				x[i] = attack * v;
			}
			return x;
		};
		// The first 0.9 s: the last ms of both feel the sound cut off at 1 s, each in its own way
		const auto a = sound(48000), b = to48k(sound(44100));
		const std::vector<double> a9(a.begin(), a.begin() + 43200), b9(b.begin(), b.begin() + 43200);
		const double e1 = envelopeDiff(envelope(a9, 48000), envelope(b9, 48000));
		const double e10 = envelopeDiff(envelope(a9, 48000, 10), envelope(b9, 48000, 10));
		std::printf("  Q15's to48k on a known sound sampled at 44.1 kHz against it sampled at 48: 1 ms envelope "
			"%.5f dB, 10 ms %.5f dB\n", e1, e10);
		check(e1 <= 0.002 && e10 <= 0.002, "Q15's to48k: its own envelope error within 0.002 dB");
	}

	// _x low-passed at 18 kHz, the band a 44.1 kHz signal holds, the same at any rate _fs: a zero-phase FIR, a sinc
	// at 18 kHz (-6 dB there) under a Kaiser window (beta 8) 2 ms long, flat within 0.01 dB to about 16.5 kHz and
	// -80 dB from about 19.5 kHz; the signal taken as 0 outside
	inline std::vector<double> lowPass18k(const std::vector<double>& _x, const double _fs)
	{
		constexpr double Cutoff = 18000, Beta = 8, Seconds = 0.002;
		const int half = static_cast<int>(std::lround(Seconds * _fs / 2));
		std::vector<double> h(static_cast<size_t>(2 * half + 1));
		for(int j = -half; j <= half; ++j)
		{
			const double fc = Cutoff / _fs;
			const double sinc = j == 0 ? 2 * fc : std::sin(2 * Pi * fc * j) / (Pi * j);
			h[static_cast<size_t>(j + half)] = sinc * kaiser(Beta, static_cast<double>(j) / half);
		}
		std::vector<double> y(_x.size());
		const auto n = static_cast<int>(_x.size());
		for(int i = 0; i < n; ++i)
		{
			double s = 0;
			for(int j = std::max(-half, i - n + 1); j <= std::min(half, i); ++j)
				s += h[static_cast<size_t>(j + half)] * _x[static_cast<size_t>(i - j)];
			y[static_cast<size_t>(i)] = s;
		}
		return y;
	}

	// Q15: a machine renders at its native 48 kHz and reaches 44.1 kHz through Host (Resample48To44). Its Hits at
	// 44.1 kHz (_b, aligned for the resampler's latency) against its Hits at 48 kHz (_a), both from the trig and
	// rendered 50 ms past _seconds, each low-passed at 18 kHz (lowPass18k), then cut to _seconds: what lies above
	// 18 kHz, which 44.1 kHz cannot hold, is printed, not judged; the cut leaves out the filters' zero-padded ends.
	// Both renders are the same 48 kHz engine output, the one resampled: nothing chaotic separates them, so every Hit
	// is judged alike and nothing is excused: the envelope within 0.2 dB above -40 dB, read on the 48 kHz grid (to48k,
	// its own error checked first), every third octave below 10 kHz within 0.5 dB
	inline void q15(const std::vector<std::vector<double>>& _a, const std::vector<std::vector<double>>& _b,
		const double _seconds)
	{
		checkTo48k();
		const auto cut = [](std::vector<double> _x, const double _fs, const double _s)
		{
			_x.resize(static_cast<size_t>(_s * _fs));
			return _x;
		};
		bool envOk = true, specOk = true;
		double env = 0, spec = 0, above = 0, full = 0;
		std::printf("  Q15 Hit  envelope dB, < 18 kHz  spectrum < 10 kHz dB (worst band, Hz)  > 10 kHz dB, not judged  "
			"envelope dB, full band, not judged\n");
		for(int l = 0; l < Lanes; ++l)
		{
			const auto a = cut(lowPass18k(_a[l], 48000), 48000, _seconds);
			const auto b = lowPass18k(_b[l], 44100);
			const double e = envelopeDiff(envelope(a, 48000), envelope(cut(to48k(b), 48000, _seconds), 48000));
			const double ef = envelopeDiff(envelope(cut(_a[l], 48000, _seconds), 48000),
				envelope(cut(to48k(_b[l]), 48000, _seconds), 48000));
			full = std::max(full, ef);

			// The third octaves where _a's is above its loudest - 60 dB
			const auto ra = bands(a, 48000, -17, 12), rb = bands(cut(b, 44100, _seconds), 44100, -17, 12);
			const double loud = *std::max_element(ra.begin(), ra.end());
			double s = 0, at = 0, s10 = 0;
			for(int k = 0; k <= Band18k; ++k)
			{
				const auto i = static_cast<size_t>(k);
				if(ra[i] < loud * 1e-6)
					continue;
				const double v = std::abs(db(rb[i] / ra[i]));
				if(k > Band10k)
					s10 = std::max(s10, v);
				else if(v > s)
				{
					s = v;
					at = 1000 * std::exp2((k - 17) / 3.0);
				}
			}
			std::printf("       %2d  %21.3f  %8.3f (%5.0f)  %37.3f  %33.3f\n", l, e, s, at, s10, ef);
			above = std::max(above, s10);
			env = std::max(env, e);
			spec = std::max(spec, s);
			envOk &= e <= 0.2;
			specOk &= s <= 0.5;
		}
		std::printf("  Q15 worst: envelope below 18 kHz %.3f dB, spectrum below 10 kHz %.3f dB; above 10 kHz %.3f dB "
			"and the full band's envelope %.3f dB, not judged\n", env, spec, above, full);
		check(envOk, "Q15: 44.1 against 48 kHz, every Hit's envelope below 18 kHz within 0.2 dB above -40 dB");
		check(specOk, "Q15: 44.1 against 48 kHz, every Hit's third octaves below 10 kHz within 0.5 dB");
	}

	// ---- the voice without its chaos ----

	// _s7 with the 128-entry table at _table at 0: unit 0's feedback table, for the exact port's voice without it
	inline std::vector<int32_t> withoutTable(std::vector<int32_t> _s7, const uint32_t _table)
	{
		std::fill_n(_s7.begin() + ((_table - 0x40000400) >> 2), 128, 0);
		return _s7;
	}

	// The 16 Hits with unit 0's feedback table at 0 in both engines (_ref the exact port's, _test the fast engine's
	// after testWithoutChaos): nothing chaotic is left. On every Hit, fatal, Q4's measures: the Hilbert envelope within
	// -39 dB of the peak, the short-time third octaves within 1 dB, pitch within 1 cent; printed, not fatal: the null
	// (SNR >= 70 dB, >= 60 dB in every 10 ms window above -60 dB) and the 1 ms envelope within 0.1 dB above -40 dB
	inline void deterministic(const std::vector<std::vector<double>>& _ref,
		const std::vector<std::vector<double>>& _test)
	{
		bool ok = true, nulls = true, envs = true;
		std::printf("  feedback 0 Hit  SNR dB  10 ms dB  envelope dB  cents  envelope re peak dB  1/3 oct cells dB\n");
		for(int l = 0; l < Lanes; ++l)
		{
			const auto m = measureHit(_ref[l], _test[l]);
			const bool hit = m.envAbs <= EnvelopeAbsDb && m.cells <= CellMaxDb && std::abs(m.cents) <= 1;
			const bool nulled = m.snr >= NullSnrDb && m.local >= NullLocalDb;
			std::printf("             %2d  %6.1f  %8.1f  %11.3f  %+5.2f  %19.1f  %16.2f%s%s\n", l, m.snr, m.local,
				m.env, m.cents, m.envAbs, m.cells, hit ? "" : "  FAIL", nulled ? "" : "  below the null");
			ok &= hit;
			nulls &= nulled;
			envs &= m.env <= 0.1;
		}
		report(nulls, "feedback 0: every Hit nulls, SNR >= 70 dB, >= 60 dB in every 10 ms window above -60 dB");
		report(envs, "feedback 0: every Hit's 1 ms envelope within 0.1 dB above -40 dB");
		check(ok, "feedback 0: every Hit's Hilbert envelope within -39 dB of the peak, third octave cells within 1 dB, "
			"pitch within 1 cent");
	}

	// ---- cost ----

	// 16 Tracks playing _hits: each retriggers every 300 + 20 t blocks with its Hit's parameters
	template<typename Engine, typename Render>
	double costPerBlock(Engine& _e, const Hit (&_hits)[Lanes], Render&& _render)
	{
		using Clock = std::chrono::steady_clock;
		constexpr int Blocks = 15000;	// 10 s at 48 kHz
		for(int t = 0; t < Lanes; ++t)
		{
			for(int i = 0; i < 8; ++i)
				_e.setParameter(t, i, _hits[t].value[i], _hits[t].fine[i]);
			_e.setNote(t, _hits[t].note);
		}
		double best = 1e9;
		for(int rep = 0; rep < 5; ++rep)
		{
			const auto t0 = Clock::now();
			for(int b = 0; b < Blocks; ++b)
			{
				for(int t = 0; t < Lanes; ++t)
					if(b % (300 + 20 * t) == 0)
						_e.trig(t);
				_render();
			}
			best = std::min(best, std::chrono::duration<double, std::micro>(Clock::now() - t0).count() / Blocks);
		}
		return best;
	}

	// 16 Machine voices playing _hits (costPerBlock): per block at 48 kHz, and per host block at 44.1 kHz with the
	// resampler, printed against _exactUs, the exact port's per 48 kHz block
	template<typename Machine>
	void printCost(const char* _name, const Hit (&_hits)[Lanes], const double _exactUs)
	{
		std::array<std::array<float, BlockSize>, Lanes> buf{};
		std::array<float*, Lanes> ptr{};
		for(int t = 0; t < Lanes; ++t)
			ptr[t] = buf[t].data();
		Host<Machine> f48(48000), f44(44100);
		const double us48 = costPerBlock(f48, _hits, [&] { f48.render(ptr.data()); });
		const double us44 = costPerBlock(f44, _hits, [&] { f44.render(ptr.data()); });
		const double block48 = 32 / 48000.0 * 1e6, block44 = 32 / 44100.0 * 1e6;
		std::printf("  cost, 16 %s voices per 32-sample block: fast %.2f us (%.2f %% of a core at 48 kHz), exact %.2f "
			"us (%.2f %%), %.1fx; at 44.1 kHz with the resampler %.2f us a host block (%.2f %%)\n", _name, us48,
			100 * us48 / block48, _exactUs, 100 * _exactUs / block48, _exactUs / us48, us44, 100 * us44 / block44);
	}
}
