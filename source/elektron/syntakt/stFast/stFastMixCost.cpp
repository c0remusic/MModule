// A measure, by hand, not a test: what syntakt::fast costs with its 16 Tracks on the nine machines mixed.
//
//	stFastMixCost [RUNS]	RUNS 7 by default; each run 10 s of audio, the median printed
//
// A kit runs one instance per machine it uses, each rendering its 16 lanes (lane t is Track t, silent where Track t is
// on another machine); the instances' blocks are summed lane by lane and, at 44.1 kHz, go through one resampler
// (Host over the summed kit, Mix). For comparison, "separate" runs one Host per machine, nine
// resamplers. It measures:
//	- per machine, with 0, 1 and 16 of its lanes sounding: what an idle lane costs against a sounding one
//	- per machine, the dearest of a few parameter sets (all 0, 64, 127, random), and SP TWINSHOT's worst: both players
//	  at the top rate on 32 distinct slots of 2^19 - 1 samples of near full-scale noise, HOLD on. Retriggered every
//	  10 ms at about 48x, a player reads some 23k samples from its slot's start, never its end: neither the loop's
//	  wrap nor the streaming of the whole slot through memory is measured
//	- the worst case: every machine on one Track at its dearest set, the other 7 Tracks on the machine whose sounding
//	  lane costs most, all 16 retriggered together every 15 blocks (10 ms: the Syntakt's fastest retrig, 1/80 at
//	  300 BPM) with their 8 parameters locked at each trig, DECAY and HOLD at 127
//	- a realistic kit: the nine machines over the 16 Tracks at ordinary sounds (64s, DECAY 40-90, HOLD off), 16th
//	  notes at 120 BPM staggered, SP TWINSHOT on 0.5 s samples at the note
// at 48 and 44.1 kHz, in host blocks of 32, 128 and 512 samples: microseconds per 48 kHz block, percent of a core,
// the worst host block, the per-machine split, the other processes' load (GetSystemTimes minus this process) and the
// margin to the 15 % budget. Nothing of the firmware: the machines' tables are made by formula, the samples here.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <tuple>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "stFastBdModern.h"
#include "stFastCpVintage.h"
#include "stFastCyAlloy.h"
#include "stFastPcCarbon.h"
#include "stFastResample.h"
#include "stFastSdBasic.h"
#include "stFastSdVintage.h"
#include "stFastSpTwinshot.h"
#include "stFastSyBits.h"
#include "stFastSyToy.h"

namespace
{
	using namespace syntakt::fast;
	using Clock = std::chrono::steady_clock;

	constexpr int Machines = 9;
	enum { BD, SD, CY, PC, SV, CP, TY, SB, TW };
	const char* const Names[Machines] = {"BD MODERN", "SD BASIC", "CY ALLOY", "PC CARBON", "SD VINTAGE", "CP VINTAGE",
		"SY TOY", "SY BITS", "SP TWINSHOT"};
	constexpr double Budget = 15;	// % of a core, the map's

	// ---- the samples SP TWINSHOT plays ----

	constexpr uint32_t MaxLength = 0x7ffff;	// the machine's clamp

	// _slots distinct slots of _length samples of noise near full scale (|x| >= 24576), slot i + _slots the same as i
	TwinshotSamples makeSamples(const int _slots, const uint32_t _length)
	{
		TwinshotSamples s;
		s.base = 0x100000;
		s.data.resize(static_cast<size_t>(_slots) * _length);
		std::mt19937 rng(1);
		for(auto& x : s.data)
		{
			const auto r = static_cast<int32_t>(rng() % 8192);
			x = static_cast<int16_t>(rng() & 1 ? 24576 + r : -24576 - r);
		}
		for(int i = 0; i < 64; ++i)
			s.slots[static_cast<size_t>(i)] = {_length, s.base + 2 * static_cast<uint32_t>(i % _slots) * _length};
		return s;
	}

	// ---- a kit: the nine instances ----

	struct Sound
	{
		std::array<uint8_t, 8> value;
		int note;
	};

	// What a Track plays: a machine (-1: nothing) and its sound
	struct Track
	{
		int machine = -1;
		Sound sound{};
	};

	struct Scenario
	{
		std::array<Track, Lanes> track;
		unsigned used = 0;		// the instances run, a bit per machine (those its Tracks name, and any more)
		int period = 0;			// 48 kHz blocks between a Track's trigs
		bool staggered = false;	// Track t trigs at its own phase, else all in the same block
		bool locks = false;		// each trig sets the Track's 8 parameters
		const TwinshotSamples* samples = nullptr;
	};

	template<typename M>
	struct Plain : M
	{
		explicit Plain(float) {}
	};

	// The nine machines, each wrapped (Plain, or Host for "separate")
	template<template<class> class W>
	class KitT
	{
	public:
		// _fs is Host's (Plain ignores it): a Mix, which Host constructs, is the kit at 48 kHz
		explicit KitT(const float _fs = 48000.0f)
		{
			std::apply([&](auto&... _p)
			{
				((_p = std::make_unique<typename std::remove_reference_t<decltype(*_p)>>(_fs)), ...);
			}, m_m);
		}

		template<typename F>
		void each(F&& _f)
		{
			each(_f, std::make_index_sequence<Machines>());
		}

		template<typename F, size_t... I>
		void each(F& _f, std::index_sequence<I...>)
		{
			(_f(static_cast<int>(I), *std::get<I>(m_m)), ...);
		}

		template<typename F>
		void on(const int _machine, F&& _f)
		{
			each([&](const int _i, auto& _m)
			{
				if(_i == _machine)
					_f(_m);
			});
		}

		void setup(const Scenario& _s)
		{
			m_used = _s.used;
			each([&](int, auto& _m)
			{
				if constexpr(std::is_base_of_v<SpTwinshot, std::remove_reference_t<decltype(_m)>>)
					_m.setSamples(_s.samples);
			});
			for(int t = 0; t < Lanes; ++t)
				if(_s.track[t].machine >= 0)
				{
					m_used |= 1u << _s.track[t].machine;
					sound(t, _s.track[t]);
				}
		}

		void sound(const int _t, const Track& _track)
		{
			on(_track.machine, [&](auto& _m)
			{
				for(int i = 0; i < 8; ++i)
					_m.setParameter(_t, i, _track.sound.value[static_cast<size_t>(i)], 0);
				_m.setNote(_t, _track.sound.note);
			});
		}

		void trig(const int _t, const int _machine) { on(_machine, [&](auto& _m) { _m.trig(_t); }); }

		bool used(const int _i) const { return m_used >> _i & 1; }

	protected:
		std::tuple<std::unique_ptr<W<BdModern>>, std::unique_ptr<W<SdBasic>>, std::unique_ptr<W<CyAlloy>>,
			std::unique_ptr<W<PcCarbon>>, std::unique_ptr<W<SdVintage>>, std::unique_ptr<W<CpVintage>>,
			std::unique_ptr<W<SyToy>>, std::unique_ptr<W<SyBits>>, std::unique_ptr<W<SpTwinshot>>> m_m;
		static_assert(std::tuple_size_v<decltype(m_m)> == Machines && TW + 1 == Machines,
			"a machine for each of the enum's and Names'");
		unsigned m_used = 0;
	};

	// The nine instances as one machine for Host: their 48 kHz blocks summed lane by lane
	struct Mix : KitT<Plain>
	{
		std::array<double, Machines> machineUs{};	// each instance's time, summed
		int64_t engineBlocks = 0;

		const Block& renderBlock()
		{
			++engineBlocks;
			m_mix = {};
			each([&](const int _i, auto& _m)
			{
				if(!used(_i))
					return;
				const auto t0 = Clock::now();
				const Block& b = _m.renderBlock();
				for(int n = 0; n < BlockSize; ++n)
					for(int l = 0; l < Lanes; ++l)
						m_mix.s[n][l] += b.s[n][l];
				machineUs[static_cast<size_t>(_i)] +=
					std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
			});
			return m_mix;
		}

	private:
		Block m_mix;
	};

	// The arrangement measured: the summed kit through one Host, one resampler at 44.1 kHz
	using Shared = Host<Mix>;

	// For comparison: one Host per machine, each with its own resampler, their outputs summed
	class Separate : public KitT<Host>
	{
	public:
		explicit Separate(const float _fs) : KitT(_fs)
		{
			for(int l = 0; l < Lanes; ++l)
				m_ptr[static_cast<size_t>(l)] = m_tmp[static_cast<size_t>(l)].data();
		}

		void render(float* const* _outputs)
		{
			for(int l = 0; l < Lanes; ++l)
				std::fill_n(_outputs[l], BlockSize, 0.0f);
			each([&](const int _i, auto& _m)
			{
				if(!used(_i))
					return;
				_m.render(m_ptr.data());
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						_outputs[l][n] += m_tmp[static_cast<size_t>(l)][static_cast<size_t>(n)];
			});
		}

		std::array<double, Machines> machineUs{};
		int64_t engineBlocks = 0;

	private:
		std::array<std::array<float, BlockSize>, Lanes> m_tmp{};
		std::array<float*, Lanes> m_ptr{};
	};

	// ---- the load of the other processes ----

	struct Load
	{
#ifdef _WIN32
		static uint64_t ticks(const FILETIME& _f) { return uint64_t(_f.dwHighDateTime) << 32 | _f.dwLowDateTime; }

		Load() { sample(m_idle, m_busy, m_self); }

		// The cores the other processes kept busy since construction
		double otherCores() const
		{
			uint64_t idle, busy, self;
			sample(idle, busy, self);
			const double wall = std::chrono::duration<double>(Clock::now() - m_t0).count() * 1e7;	// 100 ns
			const double other = static_cast<double>((busy - m_busy) - (idle - m_idle)) -
				static_cast<double>(self - m_self);
			return std::max(0.0, other / wall);
		}

	private:
		static void sample(uint64_t& _idle, uint64_t& _busy, uint64_t& _self)
		{
			FILETIME idle, kernel, user, c, e, pk, pu;
			GetSystemTimes(&idle, &kernel, &user);
			GetProcessTimes(GetCurrentProcess(), &c, &e, &pk, &pu);
			_idle = ticks(idle);
			_busy = ticks(kernel) + ticks(user);	// kernel includes idle
			_self = ticks(pk) + ticks(pu);
		}

		uint64_t m_idle = 0, m_busy = 0, m_self = 0;
		Clock::time_point m_t0 = Clock::now();
#else
		double otherCores() const { return -1; }
#endif
	};

	// ---- a run ----

	struct Result
	{
		double pct = 0;			// mean, % of a core
		double us48 = 0;		// per 48 kHz engine block
		double worstUs = 0;		// the dearest host block
		double worstPct = 0;	// of its period
		double p999Us = 0;		// the 99.9th percentile host block
		double trigUs = 0;		// the mean host block holding a trig
		double otherCores = 0;
		std::array<double, Machines> machineUs48{};	// per engine block
	};

	template<typename Kit>
	Result runOnce(const Scenario& _s, const float _fs, const int _hostBlock, const double _seconds)
	{
		auto kit = std::make_unique<Kit>(_fs);
		kit->setup(_s);
		std::array<std::array<float, BlockSize>, Lanes> buf{};
		std::array<float*, Lanes> ptr{};
		for(int l = 0; l < Lanes; ++l)
			ptr[static_cast<size_t>(l)] = buf[static_cast<size_t>(l)].data();

		const int subs = _hostBlock / BlockSize;
		const int period = static_cast<int>(std::lround(_s.period * _fs / 48000));
		int64_t sub = 0;
		// One host block; whether it held a trig
		const auto block = [&]
		{
			bool trigged = false;
			for(int k = 0; k < subs; ++k, ++sub)
			{
				for(int t = 0; t < Lanes; ++t)
				{
					const Track& tr = _s.track[static_cast<size_t>(t)];
					if(tr.machine < 0 || (sub + (_s.staggered ? t * period / Lanes : 0)) % period)
						continue;
					if(_s.locks)
						kit->sound(t, tr);
					kit->trig(t, tr.machine);
					trigged = true;
				}
				kit->render(ptr.data());
			}
			return trigged;
		};

		// A second to settle, then the measure
		const int64_t blocks = static_cast<int64_t>(_seconds * _fs / _hostBlock);
		for(int64_t b = 0; b < static_cast<int64_t>(_fs / _hostBlock); ++b)
			block();
		kit->machineUs = {};
		kit->engineBlocks = 0;

		Result r;
		std::vector<double> times;
		times.reserve(static_cast<size_t>(blocks));
		double total = 0, trigTotal = 0;
		int trigBlocks = 0;
		const Load load;
		for(int64_t b = 0; b < blocks; ++b)
		{
			const auto t0 = Clock::now();
			const bool trigged = block();
			const double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
			total += us;
			r.worstUs = std::max(r.worstUs, us);
			times.push_back(us);
			if(trigged)
			{
				trigTotal += us;
				++trigBlocks;
			}
		}
		r.otherCores = load.otherCores();
		const double periodUs = _hostBlock / static_cast<double>(_fs) * 1e6;
		std::sort(times.begin(), times.end());
		r.p999Us = times[static_cast<size_t>(0.999 * static_cast<double>(times.size() - 1))];
		r.trigUs = trigBlocks ? trigTotal / trigBlocks : 0;
		r.pct = 100 * total / (static_cast<double>(blocks) * periodUs);
		r.worstPct = 100 * r.worstUs / periodUs;
		const auto engine = static_cast<double>(std::max<int64_t>(kit->engineBlocks, 1));
		r.us48 = r.pct / 100 * (BlockSize / 48000.0 * 1e6);	// the same share of a 48 kHz block
		for(int i = 0; i < Machines; ++i)
			r.machineUs48[static_cast<size_t>(i)] = kit->machineUs[static_cast<size_t>(i)] / engine;
		return r;
	}

	double median(std::vector<double> _v)
	{
		std::sort(_v.begin(), _v.end());
		const size_t n = _v.size();
		return n % 2 ? _v[n / 2] : (_v[n / 2 - 1] + _v[n / 2]) / 2;
	}

	// The median of _runs runs (the worst block: the worst of all)
	template<typename Kit = Shared>
	Result run(const Scenario& _s, const float _fs, const int _hostBlock, const int _runs, const double _seconds)
	{
		std::vector<Result> rs;
		for(int i = 0; i < _runs; ++i)
			rs.push_back(runOnce<Kit>(_s, _fs, _hostBlock, _seconds));
		const auto med = [&](auto _get)
		{
			std::vector<double> v;
			for(const auto& r : rs)
				v.push_back(_get(r));
			return median(v);
		};
		Result m;
		m.pct = med([](const Result& _r) { return _r.pct; });
		m.us48 = med([](const Result& _r) { return _r.us48; });
		m.otherCores = med([](const Result& _r) { return _r.otherCores; });
		m.p999Us = med([](const Result& _r) { return _r.p999Us; });
		m.trigUs = med([](const Result& _r) { return _r.trigUs; });
		for(const auto& r : rs)
			if(r.worstUs > m.worstUs)
			{
				m.worstUs = r.worstUs;
				m.worstPct = r.worstPct;
			}
		for(size_t i = 0; i < Machines; ++i)
			m.machineUs48[i] = med([i](const Result& _r) { return _r.machineUs48[i]; });
		return m;
	}

	// ---- the scenarios ----

	Sound all(const uint8_t _v, const int _note)
	{
		Sound s{};
		s.value.fill(_v);
		s.note = _note;
		return s;
	}

	// SP TWINSHOT's worst on lane _t: both players at the top rate (TUNE 127 over note 100, the pitch clamped at 127),
	// forward, on slots _t and 16 + _t (distinct data for all 32), the band-pass and crossfade at the middle, HOLD on
	// (the second loops), no decay
	Sound twinshotWorst(const int _t)
	{
		return {{127, static_cast<uint8_t>(64 + _t), 64, 64, 127, static_cast<uint8_t>(80 + _t), 127, 127}, 100};
	}

	// The candidates for a machine's dearest sound on lane _t
	std::vector<Sound> candidates(const int _machine, const int _t)
	{
		std::vector<Sound> c = {all(0, 36), all(64, 60), all(127, 100)};
		std::mt19937 rng(static_cast<uint32_t>(7 + _machine));
		for(int k = 0; k < 3; ++k)
		{
			Sound s{};
			for(auto& v : s.value)
				v = static_cast<uint8_t>(rng() % 128);
			s.note = static_cast<int>(rng() % 101);
			c.push_back(s);
		}
		if(_machine == TW)
			c.push_back(twinshotWorst(_t));
		return c;
	}

	const char* const CandidateNames[] = {"all 0", "all 64", "all 127", "random 1", "random 2", "random 3",
		"TW worst"};

	// One machine on _sounding of its lanes (0: the instance runs, no Track triggered), each with sound _c
	Scenario single(const int _machine, const int _sounding, const int _c, const TwinshotSamples* _samples)
	{
		Scenario s;
		s.used = 1u << _machine;
		s.period = 15;
		s.locks = true;
		s.samples = _samples;
		for(int t = 0; t < _sounding; ++t)
			s.track[static_cast<size_t>(t)] = {_machine, candidates(_machine, t)[static_cast<size_t>(_c)]};
		return s;
	}

	// The realistic kit: kick, snares, clap, hats and cymbal, percussion, two synth drums, two sample Tracks
	Scenario realistic(const TwinshotSamples* _samples)
	{
		const std::array<std::pair<int, int>, Lanes> kit = {{{BD, 36}, {BD, 36}, {SD, 50}, {SV, 50}, {CP, 50},
			{CY, 80}, {CY, 84}, {CY, 72}, {PC, 60}, {PC, 64}, {PC, 67}, {TY, 48}, {SB, 55}, {TW, 60}, {TW, 60},
			{SD, 52}}};
		const uint8_t decay[Lanes] = {70, 60, 50, 55, 45, 25, 30, 90, 40, 40, 45, 60, 50, 64, 64, 40};
		Scenario s;
		s.period = 188;	// a 16th at 120 BPM
		s.staggered = true;
		s.samples = _samples;
		for(int t = 0; t < Lanes; ++t)
		{
			Sound snd = all(64, kit[static_cast<size_t>(t)].second);
			snd.value[5] = 0;	// drive off
			snd.value[6] = 0;	// HOLD off
			snd.value[7] = decay[t];
			if(kit[static_cast<size_t>(t)].first == TW)
			{
				snd.value[1] = static_cast<uint8_t>(64 + t);
				snd.value[5] = static_cast<uint8_t>(80 + t);
			}
			s.track[static_cast<size_t>(t)] = {kit[static_cast<size_t>(t)].first, snd};
		}
		return s;
	}

	void printResult(const char* _what, const float _fs, const int _hostBlock, const Result& _r)
	{
		std::printf("  %-10s %4.1f kHz, blocks of %3d: %6.2f us a 48 kHz block, %5.2f %% of a core (margin %+.2f "
			"points to %.0f %%); a block with trigs %.1f us, 99.9th pct %.1f, worst %.1f us (%.1f %% of its %.0f "
			"us); others' load %.2f cores\n", _what, _fs / 1000, _hostBlock, _r.us48, _r.pct, Budget - _r.pct, Budget,
			_r.trigUs, _r.p999Us, _r.worstUs, _r.worstPct, _hostBlock / static_cast<double>(_fs) * 1e6,
			_r.otherCores);
	}

	void printSplit(const Result& _r, const Scenario& _s)
	{
		std::printf("    per machine, us a 48 kHz block (render + its sum):");
		double sum = 0;
		for(int i = 0; i < Machines; ++i)
		{
			int tracks = 0;
			for(const auto& t : _s.track)
				tracks += t.machine == i;
			std::printf(" %s (%d) %.2f%s", Names[i], tracks, _r.machineUs48[static_cast<size_t>(i)],
				i + 1 < Machines ? "," : "");
			sum += _r.machineUs48[static_cast<size_t>(i)];
		}
		std::printf("; machines %.2f, the rest (resampler, outputs, events) %.2f\n", sum, _r.us48 - sum);
	}

	void scenario(const char* _name, const Scenario& _s, const int _runs)
	{
		std::printf("\n%s\n", _name);
		for(const float fs : {48000.0f, 44100.0f})
			for(const int hb : {32, 128, 512})
			{
				const Result r = run(_s, fs, hb, _runs, 10);
				printResult("shared", fs, hb, r);
				if(hb == 32)
					printSplit(r, _s);
			}
		printResult("separate", 44100.0f, 128, run<Separate>(_s, 44100.0f, 128, _runs, 10));
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const int runs = _argc > 1 ? std::max(1, std::atoi(_argv[1])) : 7;
	std::printf("stFastMixCost: median of %d runs of 10 s; 16 Tracks, one instance per machine used, summed, one "
		"resampler at 44.1 kHz (\"separate\": a Host per machine)\n", runs);

	const TwinshotSamples worstSamples = makeSamples(32, MaxLength);
	const TwinshotSamples shortSamples = makeSamples(32, 24000);

	// Each machine's dearest sound with its 16 lanes sounding, and what its idle lanes cost
	std::printf("\nper machine, 48 kHz, blocks of 32, every lane retriggered every 15 blocks with locks "
		"(us a block):\n");
	std::array<int, Machines> dearest{};
	std::array<double, Machines> lane{};
	for(int m = 0; m < Machines; ++m)
	{
		const size_t n = candidates(m, 0).size();
		std::printf("  %-11s", Names[m]);
		double best = -1;
		for(size_t c = 0; c < n; ++c)
		{
			const double us = run(single(m, 16, static_cast<int>(c), &worstSamples), 48000, 32, 3, 2).us48;
			std::printf(" %s %.2f,", CandidateNames[c], us);
			if(us > best)
			{
				best = us;
				dearest[static_cast<size_t>(m)] = static_cast<int>(c);
			}
		}
		const int d = dearest[static_cast<size_t>(m)];
		const double none = run(single(m, 0, d, &worstSamples), 48000, 32, 3, 2).us48;
		const double one = run(single(m, 1, d, &worstSamples), 48000, 32, 3, 2).us48;
		lane[static_cast<size_t>(m)] = (best - none) / 16;	// a sounding lane over an idle one
		std::printf(" dearest %s; lanes sounding 0: %.2f, 1: %.2f, 16: %.2f\n", CandidateNames[d], none, one, best);
	}

	// The worst case: every machine once at its dearest, the other 7 Tracks on the dearest sounding lane
	const int dearLane = static_cast<int>(std::max_element(lane.begin(), lane.end()) - lane.begin());
	Scenario worst;
	worst.period = 15;
	worst.locks = true;
	worst.samples = &worstSamples;
	for(int t = 0; t < Lanes; ++t)
	{
		const int m = t < Machines ? t : dearLane;
		const auto c = static_cast<size_t>(dearest[static_cast<size_t>(m)]);
		worst.track[static_cast<size_t>(t)] = {m, candidates(m, t)[c]};
	}
	std::printf("\nthe 7 Tracks past the nine on %s (%.3f us a sounding lane over an idle one)\n", Names[dearLane],
		lane[static_cast<size_t>(dearLane)]);
	scenario("worst case: 16 Tracks retriggered together every 15 blocks, 8 locks a trig, the dearest sounds", worst,
		runs);
	scenario("realistic: a kit of the nine machines, ordinary decays, 16ths at 120 BPM staggered",
		realistic(&shortSamples), runs);
	return 0;
}
