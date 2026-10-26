#pragma once

// What stFastBdModernTest measures with and plays, shared with stFastAbRender (the listening test's renders): the
// metrics, the exact port's eight tracks as the voice CPU renders them, and the 16 Hits with their events

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

#include "stBoot.h"
#include "stEngine.h"
#include "stFastBdModern.h"

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
	// there), on any engine with BdModern's interface. _noTransient: TRANSIENT at 0, unit 0 out of the mix
	template<typename Engine>
	void events(Engine& _e, const int _b, const double _fs, const bool _noTransient = false)
	{
		const int retrig = static_cast<int>(std::lround(160 * _fs / 48000));
		const int gate = 2 * retrig;
		for(int t = 0; t < Lanes; ++t)
		{
			const Hit& h = Hits[t];
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

}
