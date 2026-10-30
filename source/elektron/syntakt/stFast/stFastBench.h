#pragma once

// What the fast machines' tests play and judge with, shared with stFastAbRender (the listening test's renders) and
// stFastSweep: the exact port's eight tracks as the voice CPU renders them, a machine's 16 Hits with their events,
// and the criteria every machine is judged by (Q2-Q4, Q15, the chaotic Hits' distributions, the cost). The measures
// they read are stFastMetrics.h's

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "stBoot.h"
#include "stEngine.h"
#include "stFastMetrics.h"
#include "stFastResample.h"
#include "stFastSpTwinshot.h"
#include "stSection7.h"
#include "stStages.h"
#include "stSyntaktScenario.h"

namespace syntakt::fast::bench
{
	// Q4's fatal thresholds on the measures, and the null's (printed in Q4; the chaos classification's)
	constexpr double EnvelopeAbsDb = -39, CellMaxDb = 1;
	constexpr double NullSnrDb = 70, NullLocalDb = 60, NullBandDb = -60;

	// ---- the exact port ----

	// The sample set SP TWINSHOT plays (loadSamplesOrSkip): as the exact port reads it, and as the fast machine does.
	// None (null, empty) for the other machines
	inline const Samples* g_samples = nullptr;
	inline TwinshotSamples g_twinshotSamples;

	// Eight tracks of the exact port's machines as the voice CPU renders them, before the track stages: CPU #1's block
	// in, renderVoices' output out (the block's layout as VoiceCpu writes it). A track plays BD MODERN until
	// setMachine says otherwise
	class Exact
	{
	public:
		explicit Exact(const std::vector<int32_t>& _section7)
			: m_sram(std::make_unique<Sram>()), m_rom{_section7.data(), g_samples}
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

		// The machine the track plays from its next trig: the machine's number, which the firmware maps to its engine
		// (0 BD MODERN, 1 SD BASIC ... as their engines, but 36 SY TOY, engine 8)
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

		// Track _t's voice field at _offset, as the last render left it
		int32_t field(const int _t, const uint32_t _offset) const { return m_sram->at(Voice::addressOf(_t) + _offset); }

		// The voice CPU's own globals: the generator voices draw their noise seeds from
		const Globals& globals() const { return m_globals; }

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
		int32_t field(const int _t, const uint32_t _offset) { return cpu(_t).field(_t & 7, _offset); }
	};

	// The six words (0x570-0x584) each Track of the exact port draws when its first trig puts it on another machine,
	// the 16 changing machine in one block: on each voice CPU, in the voices' order, a voice draws six from the CPU's
	// generator as the boot leaves it (0x40003ee0). A machine's noise reads some of them (SD VINTAGE's the last four):
	// what a fast machine with noise takes to play the exact port's, its testSeedNoise picking the words it reads
	inline std::array<std::array<int32_t, 6>, Lanes> exactNoiseSeeds(const std::vector<int32_t>& _s7)
	{
		Globals g = Exact(_s7).globals();
		std::array<std::array<int32_t, 6>, Lanes> seeds{};
		for(size_t v = 0; v < 8; ++v)
		{
			drawRandom(seeds[v].data(), 6, g.rng.data());
			seeds[v + 8] = seeds[v];
		}
		return seeds;
	}

	// The section 7 the test loaded (loadSection7OrSkip), from which fastHits seeds a machine with noise as the exact
	// port's
	inline std::vector<int32_t> g_section7;

	// Whether a fast machine takes the exact port's noise seeds (its testSeedNoise)
	template<typename Machine, typename = void>
	struct SeedsNoise : std::false_type {};
	template<typename Machine>
	struct SeedsNoise<Machine, std::void_t<decltype(std::declval<Machine&>().testSeedNoise(0,
		static_cast<const int32_t*>(nullptr)))>> : std::true_type {};

	// Whether a fast machine plays samples (its setSamples), which the bench gives it from g_twinshotSamples
	template<typename Machine, typename = void>
	struct TakesSamples : std::false_type {};
	template<typename Machine>
	struct TakesSamples<Machine, std::void_t<decltype(std::declval<Machine&>().setSamples(
		static_cast<const TwinshotSamples*>(nullptr)))>> : std::true_type {};

	template<typename Machine>
	void giveSamples(Machine& _m)
	{
		if constexpr(TakesSamples<Machine>::value)
			_m.setSamples(&g_twinshotSamples);
	}

	// ---- the Hits ----

	// The retrigger's 48 kHz block (106.7 ms), the gate-off's twice it
	constexpr int RetrigBlock48 = 160;

	struct Hit
	{
		uint8_t value[8];	// the Machine page's eight parameters
		uint8_t fine[8];
		int note;
		bool retrig;		// a second trig at RetrigBlock48 (147 at 44.1 kHz): HitSet's retrigger
		bool gate;			// a gate-off at 213.3 ms (block 320, 294)
	};

	// A machine's 16 Hits, the parameters its retrigger sets to 127 before its trig, the note 2 up (none: a Hit set
	// that retriggers fails), and the machine's id in the exact port (Exact::setMachine)
	struct HitSet
	{
		Hit hit[Lanes];
		std::vector<int> retrigger;
		int machine;
	};

	// The Hits' events before block _b at _fs (48 or 44.1 kHz: the retrigger and the gate-off land on a block there),
	// on any engine with a machine's interface
	template<typename Engine>
	void events(Engine& _e, const int _b, const double _fs, const HitSet& _hits)
	{
		const int retrig = static_cast<int>(std::lround(RetrigBlock48 * _fs / 48000));
		const int gate = 2 * retrig;
		for(int t = 0; t < Lanes; ++t)
		{
			const Hit& h = _hits.hit[t];
			if(_b == 0)
			{
				for(int i = 0; i < 8; ++i)
					_e.setParameter(t, i, h.value[i], h.fine[i]);
				_e.setNote(t, h.note);
				_e.trig(t);
			}
			if(h.retrig && _b == retrig)
			{
				if(_hits.retrigger.empty())
					throw std::logic_error("a Hit set that retriggers names what its retrigger sets");
				for(const int i : _hits.retrigger)
					_e.setParameter(t, i, 127, 0);
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
	std::vector<std::vector<double>> fastHits(const double _fs, const double _seconds, const HitSet& _hits,
		Setup&& _setup, const bool _fromTrig = false)
	{
		Host<Machine> e(static_cast<float>(_fs));
		if constexpr(SeedsNoise<Machine>::value)
		{
			if(g_section7.empty())
				throw std::logic_error("a machine with noise needs the section 7 loaded (loadSection7OrSkip)");
			static const auto seeds = exactNoiseSeeds(g_section7);
			for(int t = 0; t < Lanes; ++t)
				e.testSeedNoise(t, seeds[static_cast<size_t>(t)].data());
		}
		giveSamples(e);
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

	// The units a nudge may move: a voice's 7, at least 1
	constexpr int checkedUnits(const int _units)
	{
		if(_units < 1 || _units > 7)
			throw std::invalid_argument("a machine has 1 to 7 units");
		return _units;
	}

	// What the exact port's Hits are rendered under (exactHits). Its phase: the phase of each of a machine's first
	// units (units, which phaseNudge sets with it) _phase LSB (2^-24 cycle) on after each trig's onset block (the trig
	// at block 0 and the retrigger at RetrigBlock48, whose trig restarts the phases: a nudge before it would be
	// undone), what the exact port nulls to against itself: the floor of a null where the voice is chaotic. Its inc:
	// LSB added to each unit's increment word throughout, after each update (Exact::setIncrementNudge): the frequency
	// word a table or the tune moves when it is a little off. Nudge{}: none
	struct Nudge
	{
		int32_t phase = 0;
		int units = 0;	// the units the phase nudge moves
		int8_t inc[7] = {};
	};

	// The phase nudge of a machine's _units units
	inline Nudge phaseNudge(const int32_t _lsb, const int _units)
	{
		Nudge n;
		n.phase = _lsb;
		n.units = checkedUnits(_units);
		return n;
	}

	// The increment nudges of a machine's _units units (at most 7), each up (even index) and down (odd): each unit 1
	// LSB alone, all of them, then each pair of them. A unit alone is what one table's entry moves (a ratio), all what
	// the pitch moves (TUNE, the note), a pair what one entry read by two units moves (CY ALLOY's Spread: unit 2 and,
	// through a product, unit 0). 22 for four units
	constexpr int incrementNudgeCount(const int _units)
	{
		const int u = checkedUnits(_units);
		return 2 * (u + 1 + u * (u - 1) / 2);
	}

	// Whether nudge _i (of incrementNudgeCount(_units)) moves unit _k
	constexpr bool incrementNudgeMoves(const int _i, const int _k, const int _units)
	{
		const int set = _i / 2;	// a unit alone below _units, all at _units, then the pairs (0 1) (0 2) ... in order
		if(_k >= _units)
			return false;
		if(set < _units)
			return _k == set;
		if(set == _units)
			return true;
		int pair = set - _units - 1;
		for(int a = 0; a < _units; ++a)
			for(int b = a + 1; b < _units; ++b)
				if(pair-- == 0)
					return _k == a || _k == b;
		return false;
	}

	inline Nudge incrementNudge(const int _i, const int _units)
	{
		Nudge n;
		checkedUnits(_units);
		for(int k = 0; k < 7; ++k)
			n.inc[k] = static_cast<int8_t>(incrementNudgeMoves(_i, k, _units) ? (_i % 2 ? -1 : 1) : 0);
		return n;
	}

	// _seconds of the exact port's 16 Hits (_hits, every Track on its machine) at 48 kHz under _n, from the onset (the
	// block after the trig's)
	inline std::vector<std::vector<double>> exactHits(const std::vector<int32_t>& _s7, const double _seconds,
		const Nudge& _n, const HitSet& _hits)
	{
		const int32_t nudge = _n.phase;
		if(nudge)
			checkedUnits(_n.units);
		Exact16 e(_s7);
		e.a.setIncrementNudge(_n.inc);
		e.b.setIncrementNudge(_n.inc);
		const int samples = static_cast<int>(_seconds * 48000);
		const int blocks = samples / BlockSize + 2;
		std::vector<std::vector<double>> out(Lanes);
		for(int b = 0; b < blocks; ++b)
		{
			if(b == 0)
				for(int t = 0; t < Lanes; ++t)
					e.setMachine(t, _hits.machine);
			events(e, b, 48000, _hits);
			e.render();
			if(nudge && (b == 2 || b == RetrigBlock48 + 2))
				for(int t = 0; t < 8; ++t)
					for(uint32_t k = 0; k < static_cast<uint32_t>(_n.units); ++k)
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

	// A wave as a fast engine reads it: its table's 257 words (stFastTables) as floats, and a copy of the last
	template<typename Words>
	std::vector<float> fastWave(const Words& _w)
	{
		std::vector<float> f(258);
		for(size_t i = 0; i < 257; ++i)
			f[i] = static_cast<float>(q31(_w[i]));
		f[257] = f[256];
		return f;
	}

	// A booted voice CPU's SRAM for the integer stages, 16 voices' fields on two of them
	struct Stage
	{
		std::unique_ptr<Sram> sram[2];
		Globals g;
		Rom rom;

		explicit Stage(const std::vector<int32_t>& _s7) : rom{_s7.data(), g_samples}
		{
			for(auto& s : sram)
			{
				s = std::make_unique<Sram>();
				boot(*s, g, rom);
			}
		}

		Voice voice(const int _l) { return Voice{*sram[_l / 8], Voice::addressOf(_l & 7)}; }
	};

	// Voice _l of _st after an exact port's machine update (_update, a syntakt::xxx::update) at a trig (TrigNow), its
	// Machine page's bytes _bytes (parameter i's value at 2 + 2 i, its fine after, the others 0), at _pitch: the words
	// the machine runs its stages with, which a test reads rather than writes
	template<typename Update>
	Voice updatedVoice(Stage& _st, const int _l, const int32_t _pitch,
		const std::vector<std::pair<int, uint8_t>>& _bytes, Update&& _update)
	{
		Voice v = _st.voice(_l);
		const uint32_t params = TrackParams + ParamStride * static_cast<uint32_t>(_l & 7);
		for(uint32_t k = 2; k < 18; ++k)
			v.sram.setByte(params + 0x3e + k, 0);
		for(const auto& [k, b] : _bytes)
			v.sram.setByte(params + 0x3e + static_cast<uint32_t>(k), b);
		v.at(Voice::TrigNow) = 1;
		v.at(Voice::TrigPending) = 0;
		_update(_pitch, v, params + 0x1c, _st.rom);
		return v;
	}

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

	// Q2 of the gated noise (0x400044e2) at a machine's gate word _gate, over _blocks: the generator in integers as the
	// machine's, lane l from the exact port's seeds, the noise reading the last four of the six words its machine
	// change draws. Printed, its verdict returned
	inline bool q2GatedNoise(const std::vector<int32_t>& _s7, const int32_t _gate, const int _blocks)
	{
		Residual r;
		const auto seeds = exactNoiseSeeds(_s7);
		int32_t s[Lanes][5];
		alignas(16) uint32_t w[4][Lanes];
		for(int l = 0; l < Lanes; ++l)
		{
			const int32_t* drawn = seeds[static_cast<size_t>(l)].data();
			for(int k = 0; k < 4; ++k)
				w[k][l] = static_cast<uint32_t>(s[l][k] = drawn[2 + k]);
			s[l][4] = _gate;
		}
		fast::Block out;
		for(int b = 0; b < _blocks; ++b)
		{
			int32_t ref[Lanes][BlockSize];
			for(int l = 0; l < Lanes; ++l)
				syntakt::gatedNoise(ref[l], s[l]);
			fast::gatedNoise(out, w, _gate);
			for(int l = 0; l < Lanes; ++l)
				for(int n = 0; n < BlockSize; ++n)
					r.add(q31(ref[l][n]), out.s[n][l]);
		}
		return r.print("gated noise");
	}

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

	// A machine's drive (0x400047ea) as its voice holds it, with the drive on and off: the gain envelope's fall
	// (0x414-0x41c: its start, twice, and pole) and rise (0x420-0x428), its floor, pre-gain and gain after the clip
	struct DriveWords
	{
		struct Side
		{
			int32_t fallStart, fallPole, riseStart, risePole, floor, preGain, gain;
		};
		Side on, off;
	};

	// Q3 of the drive over 1 s with a machine's words (_words), on on lanes 0-7, off on 8-15, fed a full-scale
	// decaying sine from a trig, the float clip fed the integer gain envelope's levels; printed as _stage, true where
	// it passes
	inline bool q3DriveClip(Stage& _st, const DriveWords& _words, const char* _stage)
	{
		constexpr int Blocks = 1500;
		Runs r;
		alignas(16) float from[Lanes], to[Lanes], drive[Lanes], post[Lanes];
		for(int b = 0; b < Blocks; ++b)
		{
			fast::Block x;
			int32_t ref[Lanes][BlockSize];
			for(int l = 0; l < Lanes; ++l)
			{
				Voice v = _st.voice(l);
				const bool on = l < 8;
				const DriveWords::Side& w = on ? _words.on : _words.off;
				v.at(Voice::TrigNow) = b == 0;
				if(b == 0)
				{
					v.at(0x414) = v.at(0x418) = w.fallStart;
					v.at(0x41c) = w.fallPole;
					v.at(0x420) = v.at(0x424) = w.riseStart;
					v.at(0x428) = w.risePole;
					v.at(Voice::DriveFloor) = w.floor;
					v.at(Voice::DrivePreGain) = w.preGain;
					v.at(Voice::DriveGain) = w.gain;
					v.at(Voice::DriveOn) = on;
					v.at(Voice::DriveX1) = v.at(Voice::DriveY1) = INT32_MAX;
				}
				from[l] = static_cast<float>(on ? q31(v.at(Voice::DriveY1)) : q31(INT32_MAX));
				drive[l] = static_cast<float>(4 * q31(v.at(Voice::DrivePreGain)));
				post[l] = static_cast<float>(q31(v.at(Voice::DriveGain)));
				for(int n = 0; n < BlockSize; ++n)
				{
					const double t = (b * BlockSize + n) / 48000.0;
					const double decay = std::exp(-t / (0.05 + 0.05 * (l & 7)));
					ref[l][n] = toQ31(decay * std::sin(2 * Pi * (40 + 10 * l) * t));
					x.s[n][l] = static_cast<float>(q31(ref[l][n]));
				}
				syntakt::driveClip(ref[l], v);
				to[l] = static_cast<float>(q31(v.at(Voice::DriveY1)));
				for(int n = 0; n < BlockSize; ++n)
					r.ref[l].push_back(q31(ref[l][n]));
			}
			fast::driveClip(x, from, to, drive, post);
			for(int l = 0; l < Lanes; ++l)
				for(int n = 0; n < BlockSize; ++n)
					r.test[l].push_back(x.s[n][l]);
		}
		return r.print(_stage);
	}

	// ---- Q4: the voice against the exact port ----

	// The exact port's 16 Hits at 48 kHz, and the same with every unit's phase nudged as Nudges says after the
	// onsets (_render(nudge)), and with its increments moved by the increment nudges. A Hit is chaotic where the first
	// phase nudge fails Q4's own measures against the unperturbed render (chaoticSelf); there, 16 phase nudges give
	// the dispersion its distributions are judged against, with the limit's factor calibrated on all 17, below 18 kHz.
	// Elsewhere the increment nudges give its tolerance, with k calibrated on them (calibrateTolerance) (Q4). _units:
	// the machine's units, which the nudges move
	struct Exacts
	{
		std::vector<std::vector<double>> ref;
		bool chaotic[Lanes];
		double selfSnr[Lanes], selfLocal[Lanes], selfEnv[Lanes], selfEnvAbs[Lanes], selfCells[Lanes];
		PhaseBlind tolerance[Lanes]{};		// the worst of the exact port against its increment nudges
		std::vector<Deviation> spread[Lanes];	// the nudges' deviations, on a chaotic Hit
		double factor = 2, k = 1;

		template<typename Render>
		Exacts(Render&& _render, const int _units) : ref(_render(Nudge{}))
		{
			// The first phase nudge classifies; the other 16 are rendered only for a chaotic Hit's dispersion, the
			// increment nudges only for the tolerance of a Hit that is not
			std::vector<std::vector<std::vector<double>>> nudged{_render(phaseNudge(Nudges[0], _units))}, increments;
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
				nudged.push_back(_render(phaseNudge(Nudges[i], _units)));
			for(int i = 0; anyNot && i < incrementNudgeCount(_units); ++i)
				increments.push_back(_render(incrementNudge(i, _units)));
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

	// ---- the block-rate control, block by block ----

	// _hits at 48 kHz in the exact port, every Track on their machine, and in Machine, then again with trigs in
	// adjacent blocks: a second in the block after the first (block 1, the machine change's block, which loses it and
	// the gate-off sent with it as the exact port's 0x40003ee0 clears the voice's words 0x00-0x3c), two more at blocks
	// 80 and 81, and a second after the retrigger (161): blocks that see a trig while their own sounds (TrigNow and
	// TrigPending at once). Per block from the first trig's, the words _words(machine, track, words) gives equal to the
	// exact port's voice fields _fields, in their order, fatal (none for an empty _fields). Every Hit by Q4's fixed
	// measures, fatal: Hilbert envelope within -39 dB of the peak, third octave cells within 1 dB. A machine with noise
	// starts it from the exact port's seeds (exactNoiseSeeds); _setup(machine) before the first block; _label names
	// the runs
	template<typename Machine, typename Setup, typename Words>
	void blockByBlock(const std::vector<int32_t>& _s7, const HitSet& _hits, const char* _label, Setup&& _setup,
		const std::vector<uint32_t>& _fields, Words&& _words)
	{
		for(const bool adjacent : {false, true})
		{
			Exact16 exact(_s7);
			auto machine = std::make_unique<Machine>();
			if constexpr(SeedsNoise<Machine>::value)
			{
				const auto seeds = exactNoiseSeeds(_s7);
				for(int t = 0; t < Lanes; ++t)
					machine->testSeedNoise(t, seeds[static_cast<size_t>(t)].data());
			}
			giveSamples(*machine);
			_setup(*machine);
			for(int t = 0; t < Lanes; ++t)
				exact.setMachine(t, _hits.machine);
			std::vector<std::vector<double>> ref(Lanes), test(Lanes);
			int64_t words = 0, differ = 0;
			std::vector<int32_t> w(_fields.size());
			for(int b = 0; b <= 2 * 48000 / BlockSize; ++b)	// 2 s
			{
				const auto both = [&](auto&& _f) { _f(exact); _f(*machine); };
				both([&](auto& _e) { events(_e, b, 48000, _hits); });
				for(int t = 0; adjacent && t < Lanes; ++t)
				{
					if(b == 1 || b == 80 || b == 81 || (_hits.hit[t].retrig && b == RetrigBlock48 + 1))
						both([&](auto& _e) { _e.trig(t); });
					if(b == 1)
						both([&](auto& _e) { _e.gateOff(t); });
				}
				exact.render();
				const Block& out = machine->renderBlock();
				for(int t = 0; t < Lanes; ++t)
				{
					if(b >= 1 && !_fields.empty())
					{
						_words(*machine, t, w.data());
						for(size_t k = 0; k < _fields.size(); ++k, ++words)
							differ += w[k] != exact.field(t, _fields[k]);
					}
					for(int n = 0; n < BlockSize; ++n)
					{
						ref[t].push_back(q31(exact.out(t)[n]));
						test[t].push_back(out.s[n][t]);
					}
				}
			}
			bool ok = true;
			const std::string what = std::string(_label) + (adjacent ? ", trigs in adjacent blocks" : ", as played");
			for(int t = 0; t < Lanes; ++t)
			{
				// From the onset, as exactHits
				const std::vector<double> r(ref[t].begin() + BlockSize, ref[t].end());
				const std::vector<double> x(test[t].begin() + BlockSize, test[t].end());
				const auto m = measureHit(r, x);
				ok &= m.envAbs <= EnvelopeAbsDb && m.cells <= CellMaxDb;
				std::printf("  %s, Hit %2d: envelope %.1f dB re peak, cells %.3f dB, SNR %.1f dB\n", what.c_str(), t,
					m.envAbs, m.cells, m.snr);
			}
			if(!_fields.empty())
			{
				std::printf("  %s: %lld words, %lld differ\n", what.c_str(), static_cast<long long>(words),
					static_cast<long long>(differ));
				check(differ == 0, "block by block: the machine's words are the exact port's every block");
			}
			check(ok, "block by block: every Hit's Hilbert envelope within -39 dB of the peak, third octave cells "
				"within 1 dB");
		}
	}

	// blockByBlock without words to compare
	template<typename Machine, typename Setup>
	void blockByBlock(const std::vector<int32_t>& _s7, const HitSet& _hits, const char* _label, Setup&& _setup)
	{
		blockByBlock<Machine>(_s7, _hits, _label, _setup, {}, [](const Machine&, int, int32_t*) {});
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
	double costPerBlock(Engine& _e, const HitSet& _hits, Render&& _render)
	{
		using Clock = std::chrono::steady_clock;
		constexpr int Blocks = 15000;	// 10 s at 48 kHz
		for(int t = 0; t < Lanes; ++t)
		{
			for(int i = 0; i < 8; ++i)
				_e.setParameter(t, i, _hits.hit[t].value[i], _hits.hit[t].fine[i]);
			_e.setNote(t, _hits.hit[t].note);
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
	// resampler, printed against the exact port's per 48 kHz block, every Track on _hits' machine, measured first
	template<typename Machine>
	void printCost(const char* _name, const std::vector<int32_t>& _s7, const HitSet& _hits)
	{
		Exact16 e(_s7);
		for(int t = 0; t < Lanes; ++t)
			e.setMachine(t, _hits.machine);
		const double exactUs = costPerBlock(e, _hits, [&] { e.render(); });

		std::array<std::array<float, BlockSize>, Lanes> buf{};
		std::array<float*, Lanes> ptr{};
		for(int t = 0; t < Lanes; ++t)
			ptr[t] = buf[t].data();
		Host<Machine> f48(48000), f44(44100);
		giveSamples(f48);
		giveSamples(f44);
		const double us48 = costPerBlock(f48, _hits, [&] { f48.render(ptr.data()); });
		const double us44 = costPerBlock(f44, _hits, [&] { f44.render(ptr.data()); });
		const double block48 = 32 / 48000.0 * 1e6, block44 = 32 / 44100.0 * 1e6;
		std::printf("  cost, 16 %s voices per 32-sample block: fast %.2f us (%.2f %% of a core at 48 kHz), exact %.2f "
			"us (%.2f %%), %.1fx; at 44.1 kHz with the resampler %.2f us a host block (%.2f %%)\n", _name, us48,
			100 * us48 / block48, exactUs, 100 * exactUs / block48, exactUs / us48, us44, 100 * us44 / block44);
	}

	// ---- the tests' start ----

	// SP TWINSHOT's sample set from SYNTAKT_SAMPLES (twinshot_samples.py's file) into g_samples and g_twinshotSamples,
	// each slot with a length checked to lie where the fast machine reads it as the exact port (even from the base,
	// outside section 7, which it does not read); false, printing why, when there is none: a test then exits 77
	inline bool loadSamplesOrSkip()
	{
		static Samples exact;
		const char* path = std::getenv("SYNTAKT_SAMPLES");
		if(!path || !*path)
		{
			std::printf("SYNTAKT_SAMPLES not set: skipped\n");
			return false;
		}
		if(!syntakt::test::readSamples(path, exact))
		{
			std::printf("%s is not a sample set: skipped\n", path);
			return false;
		}
		TwinshotSamples& fast = g_twinshotSamples;
		fast.base = exact.base;
		fast.data.resize(exact.data.size() / 2);
		for(size_t k = 0; k < fast.data.size(); ++k)
			fast.data[k] = static_cast<int16_t>(exact.data[2 * k] << 8 | exact.data[2 * k + 1]);
		for(size_t i = 0; i < exact.directory.size(); ++i)
		{
			const auto& e = exact.directory[i];
			fast.slots[i] = {e.length, e.pointer};
			const bool inSection7 = e.pointer >= Rom::Base && e.pointer < Rom::Base + Section7Size;
			if(e.length && (((e.pointer - exact.base) & 1) || inSection7))
				throw std::logic_error("a sample set whose slots the fast SP TWINSHOT cannot read as the exact port");
		}
		g_samples = &exact;
		return true;
	}

	// OS 1.41's section 7 from SYNTAKT_SECTION7, or, printing why, empty: a test then exits 77, skipped
	inline std::vector<int32_t> loadSection7OrSkip()
	{
		const char* path = std::getenv("SYNTAKT_SECTION7");
		if(!path || !*path)
		{
			std::printf("SYNTAKT_SECTION7 not set: skipped\n");
			return {};
		}
		auto s7 = loadSection7(path);
		if(s7.empty())
			std::printf("%s is not OS 1.41's section 7: skipped\n", path);
		g_section7 = s7;
		return s7;
	}
}
