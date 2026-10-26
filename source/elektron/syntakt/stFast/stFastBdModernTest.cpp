// syntakt::fast::BdModern, BD MODERN in float on 16 Tracks, measured against the exact integer port (stEngine) and
// against itself at another rate (.scratch/elektron-drums/research/19-qualite-audio.md, section 11):
//	Q2	the linear stages (mix, ramped gains, one-pole filters, the sum) against the integer stages fed the same input:
//		null residual RMS <= -100 dBFS and max <= -90 dBFS
//	Q3	the non-linear stages (the feedback and phase-modulated oscillators, the drive's clip) against the integer
//		stages fed the same input, over 1 s: SNR >= 60 dB, no third octave of the residual (20 Hz to Nyquist) above
//		-60 dB relative to the reference's whole energy
//	Q4	the whole voice, 16 Hits over BD MODERN's parameters (2 s each), against syntakt::bdModern at 48 kHz: SNR >=
//		60 dB, no third octave of the residual above -60 dB, envelope (1 ms RMS) within 0.1 dB where the exact port's is
//		above its peak - 40 dB, pitch (spectral peak) within 1 cent
//	Q15	the same Hits at 44.1 against 48 kHz, from their onsets: envelope within 0.2 dB above -40 dB, spectrum below
//		10 kHz within 0.5 dB per third octave, in the bands above -60 dB from the loudest. Above 10 kHz the deviation is
//		printed, not judged: the machine's aliasing at 48 kHz is its sound, and folds elsewhere at another rate
//	cost: microseconds per 32-sample block of 16 voices playing, fast against exact
// A Hit (or a lane of Q3's feedback sine) is chaotic where the exact port, its phases 1 LSB on, nulls below 60 dB
// against itself (TRANSIENT's feedback): no rewrite can null it either. There the null criteria give way to
// distributions, the mean third-octave spectrum and the 10 ms RMS envelope, which pass within 2 times the exact
// port's own dispersion over 4 perturbations of its phases (1 and 2 LSB either way) plus 0.5 dB; at 44.1 against
// 48 kHz, below 10 kHz, against the same dispersion.
// Q3 and Q4's thresholds are provisional (ticket 05 of elektron-drums-v1 sets them by listening): the null criteria
// are printed and do not fail the test; the chaotic Hits' distributions do, as Q2 and Q15 do.
//
// SYNTAKT_SECTION7 names the user's OS 1.41 section 7. Exit 77 without it.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "stEmac.h"
#include "stEngine.h"
#include "stEngines.h"
#include "stBoot.h"
#include "stFastBdModern.h"
#include "stSection7.h"
#include "stStages.h"

namespace
{
	using namespace syntakt;
	using fast::Lanes;

	constexpr double Pi = 3.14159265358979323846;
	int g_failures = 0;
	std::vector<std::string> g_excused;	// the checks excused at 44.1 kHz, for ticket 05

	void check(const bool _ok, const char* _what)
	{
		std::printf("%s: %s\n", _ok ? "pass" : "FAIL", _what);
		if(!_ok)
			++g_failures;
	}

	void report(const bool _ok, const char* _what)
	{
		std::printf("%s (provisional): %s\n", _ok ? "pass" : "FAIL", _what);
	}

	// A provisional threshold (ticket 05) that fails the test all the same
	void checkProvisional(const bool _ok, const char* _what)
	{
		std::printf("%s (provisional, fatal): %s\n", _ok ? "pass" : "FAIL", _what);
		if(!_ok)
			++g_failures;
	}

	double q31(const int32_t _v) { return _v / 2147483648.0; }
	int32_t toQ31(const double _v)
	{
		return static_cast<int32_t>(std::clamp<long long>(std::llround(_v * 2147483648.0), INT32_MIN, INT32_MAX));
	}
	double db(const double _power) { return 10 * std::log10(std::max(_power, 1e-300)); }

	// ---- metrics ----

	void fft(std::vector<std::complex<double>>& _a)
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
	std::vector<double> powerSpectrum(const std::vector<double>& _x, size_t _min = 0)
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
	std::vector<double> bands(const std::vector<double>& _x, const double _fs, const int _first, const int _last)
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

	double energy(const std::vector<double>& _x)
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
	Null null(const std::vector<double>& _ref, const std::vector<double>& _test, const double _fs)
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

	// Mean square over 1 ms windows from sample 0, at _fs. Sample i stands for [i - 1/2, i + 1/2) / _fs and counts for
	// the part of it a window covers: whole samples would move a window's edges by up to a sample at 44.1 kHz, which
	// near a low note's zero crossing changes its RMS by tenths of a dB
	std::vector<double> envelope(const std::vector<double>& _x, const double _fs, const double _ms = 1)
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
	double envelopeDiff(const std::vector<double>& _ref, const std::vector<double>& _test)
	{
		const double peak = *std::max_element(_ref.begin(), _ref.end());
		double worst = 0;
		for(size_t k = 0; k < std::min(_ref.size(), _test.size()); ++k)
			if(_ref[k] > peak * 1e-4)
				worst = std::max(worst, std::abs(db(_test[k] / _ref[k])));
		return worst;
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

	Distribution distribution(const std::vector<double>& _x, const double _fs)
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

	Deviation deviation(const Distribution& _ref, const Distribution& _test)
	{
		Deviation d{std::vector<double>(_ref.bands.size(), -1), envelopeDiff(_ref.env, _test.env)};
		const double loud = *std::max_element(_ref.bands.begin(), _ref.bands.end());
		for(size_t k = 0; k < d.bands.size(); ++k)
			if(_ref.bands[k] >= loud * 1e-6)
				d.bands[k] = std::abs(db(_test.bands[k] / _ref.bands[k]));
		return d;
	}

	// _test against _ref passes where its deviation is at most 2 times the exact port's worst against its perturbed
	// renders plus 0.5 dB, in every band up to _last and on the envelope. The worst band is the one nearest its limit
	struct Judgement
	{
		bool ok;
		double band, bandDispersion, bandHz, env, envDispersion;
	};

	Judgement judge(const Deviation& _test, const std::vector<Deviation>& _dispersion, const int _last)
	{
		Judgement j{true, 0, 0, 0, _test.env, 0};
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
			const double m = _test.bands[static_cast<size_t>(k)] - (2 * spread + 0.5);
			if(m > margin)
			{
				margin = m;
				j.band = _test.bands[static_cast<size_t>(k)];
				j.bandDispersion = spread;
				j.bandHz = 1000 * std::exp2((k - 17) / 3.0);
			}
		}
		j.ok = margin <= 0 && j.env <= 2 * j.envDispersion + 0.5;
		return j;
	}

	void printJudgement(const char* _what, const Judgement& _j)
	{
		std::printf("    %s: spectrum %.2f dB at %.0f Hz (the exact port's own dispersion %.2f dB, limit %.2f), "
			"envelope %.2f dB (dispersion %.2f, limit %.2f): %s\n", _what, _j.band, _j.bandHz, _j.bandDispersion,
			2 * _j.bandDispersion + 0.5, _j.env, _j.envDispersion, 2 * _j.envDispersion + 0.5, _j.ok ? "pass" : "FAIL");
	}

	// The spectral peak between 20 Hz and 8 kHz, interpolated on the log power around the largest bin
	double peakHz(const std::vector<double>& _x, const double _fs)
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

	// A phase-modulated unit's band by Carson's rule, carrier -+ (index + 1) modulator, its index the peak phase
	// deviation in radians: unit 2 (units 0-2's frequency, modulated by unit 1 at the same frequency, 0.11 cycle per
	// unit) and unit 3 (the note, modulated by the enveloped mix at units 0-2's frequency, 1 cycle per unit). Over a
	// Hit: each unit's highest edge, and the third octaves below 10 kHz that the part of a band above 22.05 kHz lands
	// in, folded at 44.1 kHz (at 22.05 kHz and its multiples)
	struct Carson
	{
		float unit2 = 0, unit3 = 0;
		bool folded[Band10k + 1]{};

		void add(const double _carrier, const double _index, const double _modulator)
		{
			constexpr double Nyquist = 22050;
			const double hi = _carrier + (_index + 1) * _modulator;
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

	// _seconds of the 16 Hits from the fast engine at _fs, from the onset (the block after the trig's)
	std::vector<std::vector<double>> renderFast(const std::vector<int32_t>& _s7, const double _fs,
		const double _seconds, const bool _noTransient = false, std::array<Carson, Lanes>* _carson = nullptr)
	{
		fast::BdModern e(_s7, static_cast<float>(_fs));
		const int samples = static_cast<int>(_seconds * _fs);
		const int blocks = samples / fast::BlockSize + 2;
		std::vector<std::vector<double>> out(Lanes);
		std::array<std::array<float, fast::BlockSize>, Lanes> buf{};
		std::array<float*, Lanes> ptr{};
		for(int t = 0; t < Lanes; ++t)
			ptr[t] = buf[t].data();
		for(int b = 0; b < blocks; ++b)
		{
			events(e, b, _fs, _noTransient);
			e.render(ptr.data());
			for(int t = 0; t < Lanes; ++t)
			{
				out[t].insert(out[t].end(), buf[t].begin(), buf[t].end());
				if(!_carson)
					continue;
				const double f012 = e.frequency(t, 0), f3 = e.frequency(t, 3);
				const double index2 = 2 * Pi * 0.11, index3 = 2 * Pi * e.modulatorPeak(t);
				auto& c = (*_carson)[t];
				c.unit2 = std::max(c.unit2, static_cast<float>(f012 + (index2 + 1) * f012));
				c.unit3 = std::max(c.unit3, static_cast<float>(f3 + (index3 + 1) * f012));
				c.add(f012, index2, f012);
				c.add(f3, index3, f012);
			}
		}
		for(auto& o : out)
			o = std::vector<double>(o.begin() + fast::BlockSize, o.begin() + fast::BlockSize + samples);
		return out;
	}

	// The same from the exact port at 48 kHz. _nudge: every unit's phase _nudge LSB (2^-24 cycle) on after the onset
	// block, what the exact port nulls to against itself: the floor of a null where the voice is chaotic
	std::vector<std::vector<double>> renderExact(const std::vector<int32_t>& _s7, const double _seconds,
		const int32_t _nudge = 0)
	{
		Exact16 e(_s7);
		const int samples = static_cast<int>(_seconds * 48000);
		const int blocks = samples / BlockSize + 2;
		std::vector<std::vector<double>> out(Lanes);
		for(int b = 0; b < blocks; ++b)
		{
			events(e, b, 48000);
			e.render();
			if(_nudge && b == 2)
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

	void q2(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		std::mt19937 rng(1234);
		std::uniform_real_distribution<double> u(-1, 1);
		constexpr int Blocks = 500;
		const auto noise = [&](const double _a) { return toQ31(_a * u(rng)); };
		bool ok = true;

		// mix (0x400049f4): two inputs, two gains ramped from the previous block's to this one's
		{
			Residual r;
			alignas(16) float gx0[Lanes], gx1[Lanes], gy0[Lanes], gy1[Lanes];
			int32_t cur[Lanes][2] = {};
			fast::Block x, y, out;
			constexpr uint32_t X = 0x800096a4, Y = 0x80008c60, Out = 0x80009c58;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					const int32_t t0 = noise(0.5), t1 = noise(0.5);
					v.at(0x08) = t0;
					v.at(0x0c) = t1;
					v.at(0x18) = cur[l][0];
					v.at(0x1c) = cur[l][1];
					gx0[l] = static_cast<float>(q31(cur[l][0]));
					gy0[l] = static_cast<float>(q31(cur[l][1]));
					gx1[l] = static_cast<float>(q31(t0));
					gy1[l] = static_cast<float>(q31(t1));
					for(int n = 0; n <= BlockSize; ++n)
					{
						v.sram.at(X + 4 * n) = noise(0.95);
						v.sram.at(Y + 4 * n) = noise(0.95);
						if(n < BlockSize)
						{
							x.s[n][l] = static_cast<float>(q31(v.sram.at(X + 4 * n)));
							y.s[n][l] = static_cast<float>(q31(v.sram.at(Y + 4 * n)));
						}
					}
					mix(v.sram.ptr(X), v.sram.ptr(Y), v.sram.ptr(Out), 0, 1, v);
					std::copy_n(v.sram.ptr(Out), BlockSize, ref[l]);
					cur[l][0] = t0;
					cur[l][1] = t1;
				}
				fast::mix(x, y, out, gx0, gx1, gy0, gy1);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), out.s[n][l]);
			}
			ok &= r.print("mix");
		}

		// The VCA (0x400045a0, and 0x40004674 alike): a gain ramped from the previous block's level
		{
			Residual r;
			alignas(16) float g0[Lanes], g1[Lanes];
			int32_t prev[Lanes] = {};
			fast::Block x;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					const int32_t level = toQ31(-std::abs(u(rng)));	// negative representation
					v.at(Voice::TrigPending) = 0;
					v.at(Voice::TrigNow) = 0;
					v.at(Voice::AmpLevel) = level;
					v.at(Voice::AmpLevelPrev) = prev[l];
					g0[l] = static_cast<float>(-q31(prev[l]));
					g1[l] = static_cast<float>(-q31(level));
					for(int n = 0; n < BlockSize; ++n)
					{
						ref[l][n] = noise(0.95);
						x.s[n][l] = static_cast<float>(q31(ref[l][n]));
					}
					vca(ref[l], v);
					prev[l] = level;
				}
				fast::rampedGain(x, g0, g1);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), x.s[n][l]);
			}
			ok &= r.print("ramped gain (VCA)");
		}

		// The click's two first-order filters (0x40004e6c), with BD MODERN's coefficients
		using Coefficients = std::array<int32_t, 3>;
		const auto word = [](const uint32_t _w) { return static_cast<int32_t>(_w); };
		for(const auto& c : {Coefficients{0x7df07773, word(0x820f888d), word(0x841f111a)},
				Coefficients{0x17db49cb, 0x17db49cb, word(0xafb69397)}})
		{
			Residual r;
			int32_t state[Lanes][8] = {};
			alignas(16) float x1[Lanes] = {}, y1[Lanes] = {};
			const auto q = [](const int32_t _v) { return static_cast<float>(q31(_v)); };
			const fast::OnePole f{q(c[0]), q(c[1]), -q(c[2])};
			fast::Block x;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ref[Lanes][BlockSize + 1];
				for(int l = 0; l < Lanes; ++l)
				{
					for(int n = 0; n <= BlockSize; ++n)
						ref[l][n] = noise(0.45);
					for(int n = 0; n < BlockSize; ++n)
						x.s[n][l] = static_cast<float>(q31(ref[l][n]));
					std::copy_n(c.data(), 3, state[l]);
					onePole(ref[l], state[l]);
				}
				fast::onePole(x, f, x1, y1);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), x.s[n][l]);
			}
			ok &= r.print(c[0] == 0x7df07773 ? "one-pole high-pass" : "one-pole low-pass");
		}

		// The sum (0x400046dc), saturated
		{
			Residual r;
			fast::Block a, bb, out;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ra[Lanes][BlockSize], rb[Lanes][BlockSize], ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					for(int n = 0; n < BlockSize; ++n)
					{
						ra[l][n] = noise(0.9);
						rb[l][n] = noise(0.9);
						a.s[n][l] = static_cast<float>(q31(ra[l][n]));
						bb.s[n][l] = static_cast<float>(q31(rb[l][n]));
					}
					bdModern::sum(ra[l], rb[l], ref[l]);
				}
				fast::sum(a, bb, out);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), out.s[n][l]);
			}
			ok &= r.print("sum");
		}
		check(ok, "Q2: the linear stages null to <= -100 dBFS RMS, <= -90 dBFS max");
	}

	// A stage's output over a Hit on each lane, integer and float, compared lane by lane
	struct Runs
	{
		std::vector<std::vector<double>> ref = std::vector<std::vector<double>>(Lanes);
		std::vector<std::vector<double>> test = std::vector<std::vector<double>>(Lanes);

		bool print(const char* _stage)
		{
			double snr = 1e9, band = -1e9;
			for(int l = 0; l < Lanes; ++l)
			{
				if(ref[l].empty())	// a lane judged otherwise
					continue;
				const auto n = null(ref[l], test[l], 48000);
				snr = std::min(snr, n.snr);
				band = std::max(band, n.worstBand);
			}
			std::printf("  Q3 %-28s SNR min %6.1f dB, worst residual third octave %7.1f dB\n", _stage, snr, band);
			return snr >= 60 && band <= -60;
		}
	};

	void q3(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		const Rom& rom = st.rom;
		constexpr int Blocks = 1500;	// 1 s
		bool ok = true, chaoticOk = true;

		// The increments of a swept Hit, in 24-bit phase units: lane l's note 24 + 3 l, an octave up for units 0-2
		const auto inc24 = [](const int _l, const int _b, const double _octave)
		{
			const double sweep = 1 + (2 + _l) * std::pow(0.97, _b);
			const double hz = 440 * std::exp2((24 + 3 * _l - 69) / 12.0) * _octave * sweep;
			return static_cast<int32_t>(hz / 48000 * 16777216.0);
		};

		// Unit 0's sine with self-feedback (0x400035de), feedback from BD MODERN's table across its range, read from a
		// 256-entry sine as the engine reads it
		std::vector<float> sine(258);
		for(int i = 0; i < 258; ++i)
			sine[static_cast<size_t>(i)] = static_cast<float>(std::sin(2 * Pi * i / 256));
		{
			Runs r;
			alignas(16) uint32_t phase[Lanes] = {};
			fast::Increments inc;
			alignas(16) float fb[Lanes] = {}, target[Lanes], last[Lanes] = {};
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block out;
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					const int32_t f = rom.entry(0x4001fc00, 8 * l + 7);
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
			// Lane l's feedback is TRANSIENT 8 l + 7's. Where the feedback is strong the oscillator is chaotic: the
			// exact port, its first phase 1 LSB (2^-24 cycle) on, nulls below 60 dB against itself. Those lanes are
			// judged by their distributions against the exact port's dispersion under 4 such perturbations
			const auto exact = [&](const int _l, const int32_t _phase)
			{
				std::vector<double> out;
				Voice v = st.voice(_l);
				v.at(0x28) = 0;
				v.at(0x30) = 0;
				v.unit(0, Voice::Phase) = _phase & 0xffffff;
				for(int b = 0; b < Blocks; ++b)
				{
					v.at(0x2c) = rom.entry(0x4001fc00, 8 * _l + 7);
					v.unit(0, 0x78) = inc24(_l, b, 2);
					feedbackOscillator(0, v, rom);
					for(int n = 0; n < BlockSize; ++n)
						out.push_back(q31(v.sram.at(bufferB(0) + 4 * n)));
				}
				return out;
			};
			Runs clean;
			for(int l = 0; l < Lanes; ++l)
			{
				const auto self = exact(l, 1);
				const double selfSnr = null(r.ref[l], self, 48000).snr;
				std::printf("  Q3 feedback sine, TRANSIENT %3d (feedback %.4f): SNR %6.1f dB; the exact port against "
					"itself 1 LSB off: %6.1f dB%s\n", 8 * l + 7, q31(rom.entry(0x4001fc00, 8 * l + 7)),
					null(r.ref[l], r.test[l], 48000).snr, selfSnr, selfSnr < 60 ? ", chaotic" : "");
				if(selfSnr >= 60)
				{
					clean.ref[static_cast<size_t>(l)] = r.ref[l];
					clean.test[static_cast<size_t>(l)] = r.test[l];
					continue;
				}
				const auto ref = distribution(r.ref[l], 48000);
				std::vector<Deviation> spread;
				for(const int32_t p : {1, -1, 2, -2})
					spread.push_back(deviation(ref, distribution(p == 1 ? self : exact(l, p), 48000)));
				const auto j = judge(deviation(ref, distribution(r.test[l], 48000)), spread, Band18k);
				printJudgement("distributions", j);
				chaoticOk &= j.ok;
			}
			ok &= clean.print("feedback sine (unit 0), not chaotic");
		}

		// Units 2 and 3 (0x4000290e): the ROM wave modulated by a sine at -0.11 cycle, the sine modulated by a
		// decaying modulator at 1 cycle
		std::vector<float> wave(258);
		for(int i = 0; i < 257; ++i)
			wave[i] = static_cast<float>(q31(rom.entry(0x4001e7fc, i)));
		wave[257] = wave[256];
		for(const int k : {2, 3})
		{
			Runs r;
			alignas(16) uint32_t phase[Lanes] = {};
			fast::Increments inc;
			const int32_t depth = k == 2 ? static_cast<int32_t>(0xffe3d70a) : 0x00ffffff;
			const uint32_t src = k == 2 ? bufferB(1) : 0x80009c58;
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block in, out;
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					const auto u = static_cast<uint32_t>(k);
					if(b == 0)
					{
						v.unit(u, Voice::Phase) = 0;
						v.unit(u, 0x9c) = static_cast<int32_t>(src);
						v.unit(u, 0x8c) = v.unit(u, 0x94) = depth;
						v.unit(u, 0x90) = v.unit(u, 0x98) = 0;
						if(k == 2)
							v.unit(u, 0xb0) = 0x4001e7fc;
					}
					v.unit(u, 0x78) = inc24(l, b, k == 2 ? 2 : 1);
					for(auto& s : inc.s)
						s[l] = static_cast<uint32_t>(v.unit(u, 0x78)) << 8;
					for(int n = 0; n <= BlockSize; ++n)
					{
						const double t = (b * BlockSize + n) / 48000.0;
						const double m = 0.95 * std::exp(-t / (0.02 + 0.02 * l)) * std::sin(2 * Pi * (60 + 25 * l) * t);
						v.sram.at(src + 4 * n) = toQ31(m);
						if(n < BlockSize)
							in.s[n][l] = static_cast<float>(q31(v.sram.at(src + 4 * n)));
					}
					pmOscillator(k, v, rom);
					for(int n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(v.sram.at(bufferB(u) + 4 * n)));
				}
				if(k == 2)
					fast::pmWave(out, in, phase, inc, static_cast<float>(q31(depth) * 128), wave.data());
				else
					fast::pmSine(out, &in, phase, inc, static_cast<float>(q31(depth) * 128));
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(out.s[n][l]);
			}
			ok &= r.print(k == 2 ? "PM wave (unit 2)" : "PM sine (unit 3)");
		}

		// The drive (0x400047ea) on lanes 0-7, clean on 8-15, fed a full-scale decaying sine from a trig, the float
		// clip fed the integer gain envelope's levels
		{
			Runs r;
			alignas(16) float from[Lanes], to[Lanes], drive[Lanes], post[Lanes];
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block x;
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					const bool on = l < 8;
					v.at(Voice::TrigNow) = b == 0;
					if(b == 0)
					{
						v.at(0x414) = v.at(0x418) = on ? 0x02e3f426 : INT32_MAX;
						v.at(0x41c) = on ? static_cast<int32_t>(0x85c7e84d) : 0;
						v.at(0x420) = v.at(0x424) = on ? 0x004e99d0 : INT32_MAX;
						v.at(0x428) = on ? static_cast<int32_t>(0x809d339f) : 0;
						v.at(Voice::DriveFloor) = on ? 0x40000000 : 0x66666666;
						v.at(Voice::DrivePreGain) = on ? 0x40000000 : 0x20000000;
						v.at(Voice::DriveGain) = on ? 0x3eb851ec : 0x370a3d71;
						v.at(Voice::DriveOn) = on;
						v.at(Voice::DriveX1) = v.at(Voice::DriveY1) = INT32_MAX;
					}
					from[l] = static_cast<float>(on ? q31(v.at(Voice::DriveY1)) : q31(INT32_MAX));
					drive[l] = on ? 2.0f : 1.0f;
					post[l] = static_cast<float>(q31(v.at(Voice::DriveGain)));
					for(int n = 0; n < BlockSize; ++n)
					{
						const double t = (b * BlockSize + n) / 48000.0;
						const double decay = std::exp(-t / (0.05 + 0.05 * (l & 7)));
						ref[l][n] = toQ31(decay * std::sin(2 * Pi * (40 + 10 * l) * t));
						x.s[n][l] = static_cast<float>(q31(ref[l][n]));
					}
					driveClip(ref[l], v);
					to[l] = static_cast<float>(q31(v.at(Voice::DriveY1)));
					for(int n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(ref[l][n]));
				}
				fast::driveClip(x, from, to, drive, post);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(x.s[n][l]);
			}
			ok &= r.print("drive clip (on, off)");
		}
		checkProvisional(ok, "Q3: the non-linear stages where not chaotic, SNR >= 60 dB, no residual third octave "
			"above -60 dB");
		checkProvisional(chaoticOk, "Q3: the chaotic feedback sine's distributions within 2 x the exact port's "
			"dispersion + 0.5 dB");
	}

	// ---- Q4: the voice against the exact port ----

	// The exact port's 16 Hits at 48 kHz, and the same with every unit's phase nudged 1 or 2 LSB either way after the
	// onset block. A Hit is chaotic where the first of those nulls below 60 dB against the unperturbed one; there, the
	// four give the dispersion its distributions are judged against
	struct Exacts
	{
		std::vector<std::vector<double>> ref;
		std::vector<std::vector<std::vector<double>>> nudged;	// +1, -1, +2, -2 LSB
		bool chaotic[Lanes];
		double selfSnr[Lanes], selfEnv[Lanes];

		Exacts(const std::vector<int32_t>& _s7, const double _seconds) : ref(renderExact(_s7, _seconds))
		{
			for(const int32_t p : {1, -1, 2, -2})
				nudged.push_back(renderExact(_s7, _seconds, p));
			for(int l = 0; l < Lanes; ++l)
			{
				selfSnr[l] = null(ref[l], nudged[0][l], 48000).snr;
				selfEnv[l] = envelopeDiff(envelope(ref[l], 48000), envelope(nudged[0][l], 48000));
				chaotic[l] = selfSnr[l] < 60;
			}
		}

		// The exact port's dispersion on Hit _l: its perturbed renders' deviations from its own
		std::vector<Deviation> dispersion(const int _l) const
		{
			const auto r = distribution(ref[_l], 48000);
			std::vector<Deviation> d;
			for(const auto& n : nudged)
				d.push_back(deviation(r, distribution(n[_l], 48000)));
			return d;
		}
	};

	constexpr double Seconds = 2;

	void q4(const std::vector<int32_t>& _s7, const Exacts& _exact)
	{
		const auto& ref = _exact.ref;
		const auto test = renderFast(_s7, 48000, Seconds);
		double snr = 1e9, band = -1e9, env = 0, cents = 0;
		bool chaoticOk = true;
		std::printf("  Q4 Hit  SNR dB  worst 1/3 oct dB  envelope dB  pitch Hz (exact)  cents  | the exact port "
			"against itself 1 LSB off: SNR dB, envelope dB\n");
		for(int l = 0; l < Lanes; ++l)
		{
			const auto n = null(ref[l], test[l], 48000);
			const double e = envelopeDiff(envelope(ref[l], 48000), envelope(test[l], 48000));
			const double f0 = peakHz(ref[l], 48000), f1 = peakHz(test[l], 48000);
			const double c = 1200 * std::log2(f1 / f0);
			std::printf("     %2d  %6.1f  %16.1f  %11.3f  %16.2f  %+5.2f  | %6.1f  %6.3f%s\n", l, n.snr, n.worstBand, e,
				f0, c, _exact.selfSnr[l], _exact.selfEnv[l], _exact.chaotic[l] ? "  chaotic" : "");
			cents = std::max(cents, std::abs(c));
			if(_exact.chaotic[l])
			{
				const auto j = judge(deviation(distribution(ref[l], 48000), distribution(test[l], 48000)),
					_exact.dispersion(l), Band18k);
				printJudgement("distributions", j);
				chaoticOk &= j.ok;
				continue;
			}
			snr = std::min(snr, n.snr);
			band = std::max(band, n.worstBand);
			env = std::max(env, e);
		}
		std::printf("  Q4 worst where not chaotic: SNR %.1f dB, residual third octave %.1f dB, envelope %.3f dB; pitch "
			"%.2f cents\n", snr, band, env, cents);
		report(snr >= 60 && band <= -60, "Q4: the voice where not chaotic, SNR >= 60 dB, no residual third octave "
			"above -60 dB");
		checkProvisional(env <= 0.1, "Q4: the voice where not chaotic, envelope within 0.1 dB above -40 dB");
		checkProvisional(cents <= 1, "Q4: the voice's pitch within 1 cent");
		checkProvisional(chaoticOk, "Q4: the chaotic Hits' distributions within 2 x the exact port's dispersion + "
			"0.5 dB");
	}

	// ---- Q15: 44.1 against 48 kHz ----

	void q15(const std::vector<int32_t>& _s7, const Exacts& _exact)
	{
		const auto a = renderFast(_s7, 48000, Seconds);
		std::array<Carson, Lanes> carson{};
		const auto b = renderFast(_s7, 44100, Seconds, false, &carson);

		// The unfolded reference: the fast engine at 96 kHz. A band where the exact port at 48 kHz differs from it by
		// more than 0.5 dB holds content that depends on the rate by nature (what folds at Nyquist, unit 0's
		// one-sample feedback): it is printed, tagged folded, and not judged
		const auto c = renderFast(_s7, 96000, Seconds);

		// The same Hits without unit 0 in the mix (TRANSIENT 0), to attribute an envelope deviation to its feedback
		const auto a0 = renderFast(_s7, 48000, Seconds, true);
		const auto b0 = renderFast(_s7, 44100, Seconds, true);

		double spec = 0, folded = 0, above = 0;
		bool envOk = true, specOk = true, chaoticOk = true, evidence = true, envProvisional = true;
		std::printf("  Q15 Hit  envelope dB  spectrum < 10 kHz dB, judged (worst band, Hz)  folded: bands, worst dB  "
			"> 10 kHz dB, not judged\n");
		for(int l = 0; l < Lanes; ++l)
		{
			const double e = envelopeDiff(envelope(a[l], 48000), envelope(b[l], 44100));
			auto d = deviation(distribution(a[l], 48000), distribution(b[l], 44100));
			const auto unfolded = deviation(distribution(_exact.ref[l], 48000), distribution(c[l], 96000));
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
			else if(!judged)
			{
				std::printf("    every band below 10 kHz is folded: nothing judged\n");
				evidence = false;
			}
			// A check that fails at 44.1 kHz is unit 0's where the same Hit without unit 0 (TRANSIENT 0) passes it: its
			// feedback is a sample late, 1/44100 s here against 1/48000 s on the machine, and near Nyquist it folds
			// elsewhere. Those are provisional, to judge by ear (ticket 05); the others stay fatal
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
				const auto spread = _exact.dispersion(l);
				auto j = judge(d, spread, Band10k);
				printJudgement("distributions, 44.1 against 48 kHz, below 10 kHz, folded bands excluded", j);
				while(j.band > 2 * j.bandDispersion + 0.5 && j.band <= Cap * (2 * j.bandDispersion + 0.5) &&
					d0.bands[bandOf(j.bandHz)] <= 2 * j.bandDispersion + 0.5)
				{
					excuse("Q15 chaotic distributions", j.bandHz, j.band, d0.bands[bandOf(j.bandHz)]);
					d.bands[bandOf(j.bandHz)] = -1;
					j = judge(d, spread, Band10k);
				}
				bool ok = j.band <= 2 * j.bandDispersion + 0.5;
				if(j.env > 2 * j.envDispersion + 0.5)
				{
					if(d0.env <= 2 * j.envDispersion + 0.5 && j.env <= Cap * (2 * j.envDispersion + 0.5))
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
		report(envProvisional && g_excused.empty(), "Q15: what unit 0 alone puts beyond its limit (ticket 05)");
		check(specOk, "Q15: 44.1 against 48 kHz where not chaotic, spectrum below 10 kHz within 0.5 dB per third "
			"octave, folded bands excluded, but where unit 0 alone exceeds it");
		check(evidence, "Q15: every Hit has a band below 10 kHz left to judge");
		checkProvisional(chaoticOk, "Q15: the chaotic Hits' distributions at 44.1 against 48 kHz within 2 x the exact "
			"port's dispersion + 0.5 dB, folded bands excluded, but where unit 0 alone exceeds it");
	}

	// ---- cost ----

	// 16 Tracks playing: each retriggers every 300 + 20 t blocks with its Hit's parameters
	template<typename Engine, typename Render>
	double costPerBlock(Engine& _e, Render&& _render)
	{
		using Clock = std::chrono::steady_clock;
		constexpr int Blocks = 15000;	// 10 s at 48 kHz
		for(int t = 0; t < Lanes; ++t)
		{
			for(int i = 0; i < 8; ++i)
				_e.setParameter(t, i, Hits[t].value[i], Hits[t].fine[i]);
			_e.setNote(t, Hits[t].note);
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

	void cost(const std::vector<int32_t>& _s7)
	{
		constexpr double BlockUs = 32 / 48000.0 * 1e6;
		fast::BdModern f(_s7, 48000);
		std::array<std::array<float, fast::BlockSize>, Lanes> buf{};
		std::array<float*, Lanes> ptr{};
		for(int t = 0; t < Lanes; ++t)
			ptr[t] = buf[t].data();
		const double fastUs = costPerBlock(f, [&] { f.render(ptr.data()); });
		Exact16 e(_s7);
		const double exactUs = costPerBlock(e, [&] { e.render(); });
		std::printf("  cost, 16 BD MODERN voices per 32-sample block: fast %.2f us (%.2f %% of a core at 48 kHz), "
			"exact %.2f us (%.2f %%), %.1fx; the whole Syntakt voice section for 16 Tracks today: 7.0 %%\n",
			fastUs, 100 * fastUs / BlockUs, exactUs, 100 * exactUs / BlockUs, exactUs / fastUs);
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const char* path = std::getenv("SYNTAKT_SECTION7");
	if(!path || !*path)
	{
		std::printf("SYNTAKT_SECTION7 not set: skipped\n");
		return 77;
	}
	const auto s7 = loadSection7(path);
	if(s7.empty())
	{
		std::printf("%s is not OS 1.41's section 7: skipped\n", path);
		return 77;
	}

	q2(s7);
	q3(s7);
	const Exacts exact(s7, Seconds);
	q4(s7, exact);
	q15(s7, exact);
	cost(s7);

	std::printf("Checks excused at 44.1 kHz (unit 0 alone, FM fold-over), to judge by ear (ticket 05): %zu\n",
		g_excused.size());
	for(const auto& e : g_excused)
		std::printf("  %s\n", e.c_str());
	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
