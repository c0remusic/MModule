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

		// Track _t's 32 samples at m_sram->ptr(VoiceOut + 0x80 t)
		void render()
		{
			int32_t trig = 0, gateOff = 0;
			receiveBlock(*m_sram, m_block.data(), trig, gateOff);
			renderVoices(*m_sram, m_globals, m_rom, VoiceOut, TrackParams, trig, gateOff);
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

	// The Hits' events before block _b at _fs (48 kHz, 44.1 or 96: the retrigger and the gate-off land on a block
	// there), on any engine with BdModern's interface. _noTransient: TRANSIENT at 0, unit 0 out of the mix. _hits:
	// another machine's, whose parameters 3 and 5 the retrigger sets as well
	template<typename Engine>
	void events(Engine& _e, const int _b, const double _fs, const bool _noTransient = false,
		const Hit (&_hits)[Lanes] = Hits)
	{
		const int retrig = static_cast<int>(std::lround(160 * _fs / 48000));
		const int gate = 2 * retrig;
		for(int t = 0; t < Lanes; ++t)
		{
			const Hit& h = _hits[t];
			if(_b == 0)
			{
				for(int i = 0; i < 8; ++i)
				{
					const bool off = _noTransient && i == 2;
					_e.setParameter(t, i, off ? 0 : h.value[i], off ? 0 : h.fine[i]);
				}
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

	// Chaotic: the exact port against itself, its phases 1 LSB on, misses the null in force, SNR >= 70 dB and >= 60 dB
	// in every 10 ms window above -60 dB (localSnr). No rewrite can null where the exact port cannot
	inline bool chaoticNull(const double _snr, const double _local) { return _snr < 70 || _local < 60; }

	// A threshold set by the listening tests of ticket 05 (elektron-drums-v1)
	inline void checkValidated(const bool _ok, const char* _what)
	{
		std::printf("%s (listening-validated, ticket 05): %s\n", _ok ? "pass" : "FAIL", _what);
		if(!_ok)
			++g_failures;
	}

	// ---- a chaotic Hit, judged by its distributions ----

	// Where a voice is chaotic, no rewrite nulls against the exact port, which does not null against itself either: it
	// is judged by its mean third-octave spectrum (bands 20 Hz to 18 kHz, index k + 17 for centre 1000 2^(k/3)) and its
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

	// The third octaves below 10 kHz where a phase-modulated unit's band by Carson's rule (carrier -+ (index + 1)
	// modulator, its index the peak phase deviation in radians) passes 22.05 kHz: the part above lands there, folded
	// at 44.1 kHz (at 22.05 kHz and its multiples)
	struct FoldOver
	{
		bool folded[Band10k + 1]{};

		void add(const double _carrier, const double _index, const double _modulator)
		{
			edge(_carrier + (_index + 1) * _modulator);
		}

		// The band's highest edge _hi
		void edge(const double _hi)
		{
			constexpr double Nyquist = 22050;
			const double hi = _hi;
			if(hi <= Nyquist)
				return;
			// (Nyquist, hi] folds down onto [2 Nyquist - hi, Nyquist), or onto all of [0, Nyquist) once past 2 Nyquist
			const double lo = hi >= 2 * Nyquist ? 0 : 2 * Nyquist - hi;
			for(int k = 0; k <= Band10k; ++k)
			{
				const double centre = 1000 * std::exp2((k - 17) / 3.0);
				if(centre * std::exp2(1 / 6.0) > lo && centre * std::exp2(-1 / 6.0) < Nyquist)
					folded[k] = true;
			}
		}
	};

	// _seconds of the exact port's 16 Hits at 48 kHz, _events(e, block) before each block, from the onset (the block
	// after the trig's). _nudge: every unit's phase _nudge LSB (2^-24 cycle) on after each trig's onset block (the
	// trig at block 0 and the retrigger at 160, whose trig restarts the phases: a nudge before it would be undone),
	// what the exact port nulls to against itself: the floor of a null where the voice is chaotic
	template<typename Events>
	std::vector<std::vector<double>> exactHits(const std::vector<int32_t>& _s7, const double _seconds,
		const int32_t _nudge, Events&& _events)
	{
		Exact16 e(_s7);
		const int samples = static_cast<int>(_seconds * 48000);
		const int blocks = samples / BlockSize + 2;
		std::vector<std::vector<double>> out(Lanes);
		for(int b = 0; b < blocks; ++b)
		{
			_events(e, b);
			e.render();
			if(_nudge && (b == 2 || b == 162))
				for(int t = 0; t < 8; ++t)
					for(uint32_t k = 0; k < 4; ++k)
					{
						e.a.nudge(t, k, _nudge);
						e.b.nudge(t, k, _nudge);
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
	// its range (lane l at entry 8 l + 7 of its _parameter), read from a 256-entry sine as the engines read it. Lanes
	// where the feedback makes the oscillator chaotic are judged by their distributions; _ok and _chaoticOk take the
	// verdicts
	inline void q3FeedbackSine(Stage& _st, const uint32_t _table, const char* _parameter, bool& _ok, bool& _chaoticOk)
	{
		constexpr int Blocks = 1500;
		const Rom& rom = _st.rom;
		const auto inc24 = sweptIncrement;
		std::vector<float> sine(258);
		for(int i = 0; i < 258; ++i)
			sine[static_cast<size_t>(i)] = static_cast<float>(std::sin(2 * Pi * i / 256));
		Runs r;
		alignas(16) uint32_t phase[Lanes] = {};
		fast::Increments inc;
		alignas(16) float fb[Lanes] = {}, target[Lanes], last[Lanes] = {};
		for(int b = 0; b < Blocks; ++b)
		{
			fast::Block out;
			for(int l = 0; l < Lanes; ++l)
			{
				Voice v = _st.voice(l);
				const int32_t f = rom.entry(_table, 8 * l + 7);
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
				target[l] = static_cast<float>(4 * q31(f));
				feedbackOscillator(0, v, rom);
				for(int n = 0; n < BlockSize; ++n)
					r.ref[l].push_back(q31(v.sram.at(bufferB(0) + 4 * n)));
			}
			fast::feedbackSine(out, phase, inc, fb, target, last, sine.data());
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
				v.at(0x2c) = rom.entry(_table, 8 * _l + 7);
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
			const bool isChaotic = chaoticNull(selfSnr, selfLocal);
			std::printf("  Q3 feedback sine, %s %3d (feedback %.4f): SNR %6.1f dB, worst 10 ms %6.1f dB; "
				"the exact port against itself 1 LSB off: %6.1f, %6.1f dB%s\n", _parameter, 8 * l + 7,
				q31(rom.entry(_table, 8 * l + 7)), null(r.ref[l], r.test[l], 48000).snr,
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
	// onsets (_render(nudge)). A Hit is chaotic where the first of those misses the null against the unperturbed one;
	// there, 16 of them give the dispersion its distributions are judged against, with the limit's factor calibrated on
	// all 17, below 18 kHz (Q4) and below 10 kHz (Q15)
	struct Exacts
	{
		std::vector<std::vector<double>> ref;
		bool chaotic[Lanes];
		double selfSnr[Lanes], selfLocal[Lanes], selfEnv[Lanes];
		std::vector<Deviation> spread[Lanes];	// the nudges' deviations, on a chaotic Hit
		double factor = 2, factor10k = 2;

		template<typename Render>
		explicit Exacts(Render&& _render) : ref(_render(0))
		{
			std::vector<std::vector<std::vector<double>>> nudged;
			for(const int32_t p : Nudges)
				nudged.push_back(_render(p));
			std::vector<std::vector<Deviation>> sets;
			for(int l = 0; l < Lanes; ++l)
			{
				selfSnr[l] = null(ref[l], nudged[0][l], 48000).snr;
				selfLocal[l] = localSnr(ref[l], nudged[0][l], 48000);
				selfEnv[l] = envelopeDiff(envelope(ref[l], 48000), envelope(nudged[0][l], 48000));
				chaotic[l] = chaoticNull(selfSnr[l], selfLocal[l]);
				if(!chaotic[l])
					continue;
				const auto r = distribution(ref[l], 48000);
				for(const auto& n : nudged)
					spread[l].push_back(deviation(r, distribution(n[l], 48000)));
				sets.push_back(spread[l]);
			}
			if(sets.empty())
				return;
			factor = calibrate(sets, Band18k, "Q4 chaotic Hits, below 18 kHz");
			factor10k = calibrate(sets, Band10k, "Q15 chaotic Hits, below 10 kHz");
		}

		// The exact port's dispersion on Hit _l: its perturbed renders' deviations from its own
		std::vector<Deviation> dispersion(const int _l) const { return judgedSpread(spread[_l]); }
	};

	// Q4: the fast engine's 16 Hits at 48 kHz (_test) against the exact port's
	inline void q4(const Exacts& _exact, const std::vector<std::vector<double>>& _test)
	{
		const auto& ref = _exact.ref;
		const auto& test = _test;
		double snr = 1e9, local = 1e9, band = -1e9, env = 0, cents = 0;
		bool chaoticOk = true;
		std::string below;
		std::printf("  Q4 Hit  SNR dB  10 ms dB  worst 1/3 oct dB  envelope dB  pitch Hz (exact)  cents  | the exact "
			"port against itself 1 LSB off: SNR dB, 10 ms dB, envelope dB\n");
		for(int l = 0; l < Lanes; ++l)
		{
			const auto n = null(ref[l], test[l], 48000);
			const double w = localSnr(ref[l], test[l], 48000);
			const double e = envelopeDiff(envelope(ref[l], 48000), envelope(test[l], 48000));
			const double f0 = peakHz(ref[l], 48000), f1 = peakHz(test[l], 48000);
			const double c = 1200 * std::log2(f1 / f0);
			std::printf("     %2d  %6.1f  %8.1f  %16.1f  %11.3f  %16.2f  %+5.2f  | %6.1f  %6.1f  %6.3f%s\n", l, n.snr,
				w, n.worstBand, e, f0, c, _exact.selfSnr[l], _exact.selfLocal[l], _exact.selfEnv[l],
				_exact.chaotic[l] ? "  chaotic" : "");
			if(_exact.chaotic[l])
			{
				const auto j = judge(deviation(distribution(ref[l], 48000), distribution(test[l], 48000)),
					_exact.dispersion(l), Band18k, _exact.factor);
				printJudgement("distributions", j);
				chaoticOk &= j.ok;
				continue;
			}
			cents = std::max(cents, std::abs(c));	// a chaotic Hit's pitch is judged by its spectrum
			snr = std::min(snr, n.snr);
			local = std::min(local, w);
			band = std::max(band, n.worstBand);
			env = std::max(env, e);
			if(n.snr < 70 || w < 60)
				below += (below.empty() ? "" : ", ") + std::to_string(l);
		}
		std::printf("  Q4 worst where not chaotic: SNR %.1f dB, worst 10 ms %.1f dB, residual third octave %.1f dB, "
			"envelope %.3f dB; pitch %.2f cents\n", snr, local, band, env, cents);
		std::printf("  Q4 Hits not chaotic below 70 dB of SNR or 60 dB in a 10 ms window: %s\n",
			below.empty() ? "none" : below.c_str());
		report(snr >= 70 && local >= 60 && band <= -60, "Q4: the voice where not chaotic, SNR >= 70 dB, >= 60 dB in "
			"every 10 ms window above -60 dB, no residual third octave above -60 dB");
		checkValidated(env <= 0.1, "Q4: the voice where not chaotic, envelope within 0.1 dB above -40 dB");
		checkValidated(cents <= 1, "Q4: the voice's pitch within 1 cent");
		checkValidated(chaoticOk, "Q4: the chaotic Hits' distributions within the calibrated factor x the exact port's "
			"dispersion + 0.5 dB");
	}

	// ---- Q15: 44.1 against 48 kHz ----

	// The checks excused at 44.1 kHz (a one-sample self-feedback oscillator alone, FM fold-over), for listening
	inline std::vector<std::string> g_excused;

	// Units 2 and 3's Carson bands over a Hit (their highest edges), and the third octaves they fold into
	struct Carson : FoldOver
	{
		float unit2 = 0, unit3 = 0;
	};

	// Q15: the fast engine's Hits at 48 (_a) and 44.1 kHz (_b, its Carson bands _carson), and at 48 and 44.1 kHz
	// without unit 0, the one-sample self-feedback oscillator (_a0, _b0: its feedback or level at 0). Folding is
	// decided where nothing is chaotic, unit 0's feedback table at 0 in both engines: a band where the exact port at 48 kHz
	// (_exact0) differs from the fast engine at 96 kHz (_fast96) by more than 0.5 dB holds content that depends on the
	// rate by nature (what folds at Nyquist): it is printed, tagged folded, and not judged. A Hit with no band below
	// 10 kHz left to judge fails, unless FM fold-over or folding took them all
	inline void q15(const Exacts& _exact, const std::vector<std::vector<double>>& _a,
		const std::vector<std::vector<double>>& _b, const std::vector<std::vector<double>>& _exact0,
		const std::vector<std::vector<double>>& _fast96, const std::vector<std::vector<double>>& _a0,
		const std::vector<std::vector<double>>& _b0, const std::array<Carson, Lanes>& _carson)
	{
		const auto& a = _a;
		const auto& b = _b;
		const auto& a0 = _a0;
		const auto& b0 = _b0;
		const auto& carson = _carson;
		double spec = 0, folded = 0, above = 0;
		bool envOk = true, specOk = true, chaoticOk = true, envProvisional = true, evidence = true;
		std::string nothing;
		std::printf("  Q15 Hit  envelope dB  spectrum < 10 kHz dB, judged (worst band, Hz)  folded: bands, worst dB  "
			"> 10 kHz dB, not judged\n");
		for(int l = 0; l < Lanes; ++l)
		{
			const double e = envelopeDiff(envelope(a[l], 48000), envelope(b[l], 44100));
			auto d = deviation(distribution(a[l], 48000), distribution(b[l], 44100));
			const auto unfolded = deviation(distribution(_exact0[l], 48000), distribution(_fast96[l], 96000));
			double s = 0, at = 0, f = 0, s10 = 0;
			int judged = 0, excluded = 0;
			for(int k = 0; k <= Band18k; ++k)
			{
				auto& v = d.bands[static_cast<size_t>(k)];
				if(v < 0)
					continue;
				if(k > Band10k)
				{
					s10 = std::max(s10, v);
					continue;
				}
				if(unfolded.bands[static_cast<size_t>(k)] > 0.5)
				{
					++excluded;
					f = std::max(f, v);
					v = -1;	// nor judged in the chaotic Hits' distributions
					continue;
				}
				++judged;
				if(v > s)
				{
					s = v;
					at = 1000 * std::exp2((k - 17) / 3.0);
				}
			}
			std::printf("       %2d  %11.3f  %8.3f (%5.0f)  %27d, %6.3f  %22.3f%s\n", l, e, s, at, excluded, f, s10,
				_exact.chaotic[l] ? "  chaotic" : "");
			above = std::max(above, s10);
			folded = std::max(folded, f);
			// Where a unit's Carson band passes 22.05 kHz its sidebands fold at 44.1 kHz and not at 48: the bands they
			// land in are reported, not judged; the others, and the envelope, still are
			const int unit = carson[l].unit3 >= carson[l].unit2 ? 3 : 2;
			const float edge = std::max(carson[l].unit2, carson[l].unit3);
			std::printf("    Carson edges: unit 2 %.0f Hz, unit 3 %.0f Hz\n", carson[l].unit2, carson[l].unit3);
			int fm = 0;
			double fmWorst = 0;
			s = 0;
			for(int k = 0; k <= Band10k; ++k)
			{
				auto& v = d.bands[static_cast<size_t>(k)];
				if(v >= 0 && carson[l].folded[k])
				{
					++fm;
					fmWorst = std::max(fmWorst, v);
					v = -1;
				}
				if(v > s)
				{
					s = v;
					at = 1000 * std::exp2((k - 17) / 3.0);
				}
			}
			if(fm)
			{
				char line[160];
				std::snprintf(line, sizeof(line), "Hit %2d  Q15 spectrum, FM fold-over: unit %d, Carson edge %.0f Hz, "
					"%d bands below 10 kHz, worst %.3f dB, not judged", l, unit, edge, fm, fmWorst);
				if(s > 0)
					std::printf("    %s; the judged bands' worst %.3f dB at %.0f Hz\n", line + 8, s, at);
				else
					std::printf("    %s; no band below 10 kHz left to judge\n", line + 8);
				g_excused.emplace_back(line);
			}
			std::printf("    bands below 10 kHz excluded: %d folded, %d FM fold-over; %d judged\n", excluded, fm,
				judged - fm);
			if(judged == fm && (fm || excluded))
				nothing += (nothing.empty() ? "" : ", ") + std::to_string(l);
			else if(judged == fm)
			{
				std::printf("    no band below 10 kHz to judge, none excluded: fatal\n");
				evidence = false;
			}
			// A check that fails at 44.1 kHz is unit 0's where the same Hit without unit 0 (TRANSIENT 0) passes it: its
			// feedback is a sample late, 1/44100 s here against 1/48000 s on the machine, and near Nyquist it folds
			// elsewhere. Those are excused, as the listening of ticket 05 confirmed; the others stay fatal
			const auto d0 = deviation(distribution(a0[l], 48000), distribution(b0[l], 44100));
			// A check excused by the isolation is at most Cap times its threshold: beyond, it stays fatal
			constexpr double Cap = 3;
			const auto excuse = [&](const char* _check, const double _hz, const double _v, const double _v0)
			{
				// A band below -60 dB from the loudest without unit 0 (_v0 < 0) is unit 0's alone: absent without it
				char what[32], without[32], line[160];
				std::snprintf(what, sizeof(what), _hz > 0 ? "the %.0f Hz band" : "the envelope", _hz);
				std::snprintf(without, sizeof(without), _v0 < 0 ? "absent" : "%.3f dB", _v0);
				std::printf("    %s %.3f dB, without unit 0 %s: unit 0's (cap %.0f x its limit), to judge by ear, "
					"ticket 05\n", what, _v, without, Cap);
				std::snprintf(line, sizeof(line), "Hit %2d  %-28s %-18s %6.3f dB, without unit 0 %s", l, _check, what,
					_v, without);
				g_excused.emplace_back(line);
			};
			const auto bandOf = [](const double _hz)
			{
				return static_cast<size_t>(std::lround(3 * std::log2(_hz / 1000)) + 17);
			};
			if(_exact.chaotic[l])
			{
				const auto& spread = _exact.dispersion(l);
				auto j = judge(d, spread, Band10k, _exact.factor10k);
				printJudgement("distributions, 44.1 against 48 kHz, below 10 kHz, folded bands excluded", j);
				while(j.band > j.limit(j.bandDispersion) && j.band <= Cap * j.limit(j.bandDispersion) &&
					d0.bands[bandOf(j.bandHz)] <= j.limit(j.bandDispersion))
				{
					excuse("Q15 chaotic distributions", j.bandHz, j.band, d0.bands[bandOf(j.bandHz)]);
					d.bands[bandOf(j.bandHz)] = -1;
					j = judge(d, spread, Band10k, _exact.factor10k);
				}
				bool ok = j.band <= j.limit(j.bandDispersion);
				if(j.env > j.limit(j.envDispersion))
				{
					if(d0.env <= j.limit(j.envDispersion) && j.env <= Cap * j.limit(j.envDispersion))
						excuse("Q15 chaotic distributions", 0, j.env, d0.env);
					else
						ok = false;
				}
				if(!ok)
					printJudgement("still beyond its limit without unit 0", j);
				chaoticOk &= ok;
				continue;
			}
			spec = std::max(spec, s);
			for(int k = 0; k <= Band10k; ++k)
			{
				const double v = d.bands[static_cast<size_t>(k)], hz = 1000 * std::exp2((k - 17) / 3.0);
				if(v <= 0.5)
					continue;
				if(d0.bands[static_cast<size_t>(k)] <= 0.5 && v <= Cap * 0.5)
				{
					excuse("Q15 spectrum", hz, v, d0.bands[static_cast<size_t>(k)]);
					continue;
				}
				std::printf("    the %.0f Hz band %.3f dB, %.3f dB without unit 0 (excused up to %.1f dB): fatal\n", hz,
					v, d0.bands[static_cast<size_t>(k)], Cap * 0.5);
				specOk = false;
			}
			if(e <= 0.2)
				continue;
			const double without = envelopeDiff(envelope(a0[l], 48000), envelope(b0[l], 44100));
			if(without <= 0.2 && e <= Cap * 0.2)
			{
				excuse("Q15 envelope", 0, e, without);
				envProvisional = false;
			}
			else
			{
				std::printf("    envelope %.3f dB, %.3f dB without unit 0 (excused up to %.1f dB): fatal\n", e, without,
					Cap * 0.2);
				envOk = false;
			}
		}
		std::printf("  Q15 worst where not chaotic: spectrum below 10 kHz %.3f dB judged; folded bands %.3f dB and "
			"above 10 kHz %.3f dB, not judged\n", spec, folded, above);
		check(envOk, "Q15: 44.1 against 48 kHz where not chaotic, envelope within 0.2 dB above -40 dB, but where unit "
			"0 alone exceeds it");
		report(envProvisional && g_excused.empty(), "Q15: the cases excused (unit 0 alone, FM fold-over), "
			"listening-validated, ticket 05");
		check(specOk, "Q15: 44.1 against 48 kHz where not chaotic, spectrum below 10 kHz within 0.5 dB per third "
			"octave, folded bands excluded, but where unit 0 alone exceeds it");
		std::printf("  Q15 Hits with no band below 10 kHz left to judge (folded or FM fold-over), reported: %s\n",
			nothing.empty() ? "none" : nothing.c_str());
		check(evidence, "Q15: every Hit has a band below 10 kHz left to judge, but where folding or FM fold-over took "
			"them all");
		checkValidated(chaoticOk, "Q15: the chaotic Hits' distributions at 44.1 against 48 kHz within the calibrated "
			"factor x the exact port's dispersion + 0.5 dB, folded bands excluded, but where unit 0 alone exceeds it");
	}

	// ---- the voice without its chaos ----

	// _s7 with the 128-entry table at _table at 0: unit 0's feedback table, for the exact port's voice without it
	inline std::vector<int32_t> withoutTable(std::vector<int32_t> _s7, const uint32_t _table)
	{
		std::fill_n(_s7.begin() + ((_table - 0x40000400) >> 2), 128, 0);
		return _s7;
	}

	// The 16 Hits with unit 0's feedback table at 0 in both engines (_ref the exact port's, _test the fast engine's
	// after testWithoutFeedback): nothing chaotic is left. On every Hit, fatal: envelope within 0.1 dB above -40 dB,
	// pitch within 1 cent; printed, as Q4's: the null, SNR >= 70 dB, >= 60 dB in every 10 ms window above -60 dB
	inline void deterministic(const std::vector<std::vector<double>>& _ref,
		const std::vector<std::vector<double>>& _test)
	{
		bool ok = true, nulls = true;
		std::printf("  feedback 0 Hit  SNR dB  10 ms dB  envelope dB  cents\n");
		for(int l = 0; l < Lanes; ++l)
		{
			const auto n = null(_ref[l], _test[l], 48000);
			const double w = localSnr(_ref[l], _test[l], 48000);
			const double e = envelopeDiff(envelope(_ref[l], 48000), envelope(_test[l], 48000));
			const double c = 1200 * std::log2(peakHz(_test[l], 48000) / peakHz(_ref[l], 48000));
			const bool hit = e <= 0.1 && std::abs(c) <= 1, nulled = n.snr >= 70 && w >= 60;
			std::printf("             %2d  %6.1f  %8.1f  %11.3f  %+5.2f%s%s\n", l, n.snr, w, e, c, hit ? "" : "  FAIL",
				nulled ? "" : "  below the null");
			ok &= hit;
			nulls &= nulled;
		}
		report(nulls, "feedback 0: every Hit nulls, SNR >= 70 dB, >= 60 dB in every 10 ms window above -60 dB");
		check(ok, "feedback 0: every Hit's envelope within 0.1 dB above -40 dB, pitch within 1 cent");
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
}
