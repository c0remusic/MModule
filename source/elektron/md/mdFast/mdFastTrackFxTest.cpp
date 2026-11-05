// md::fast's per-Track stages against mdEngine's TrackFx, which is the mixer DSP's code bit for bit: each stage takes
// the oracle's own input and is compared with the oracle's own output (the EQ at Q2, the filter and DIST at Q3, SRR at
// Q13 through DIST, since the oracle shows SRR's output only through it), then the four in a row. 16 Tracks at once,
// each its setting, on Hits of every family, noise and sines, static and moving between blocks. The stages compute as
// the DSP does, so that a sample that differs at all counts: only DIST's tables, a few words 1 LSB apart, leave any. A
// control proves each measure sees a stage one step wrong, on the setting it passes. Then the tables against the OS's,
// word for word, the denormals with FTZ and DAZ off (Q14), allocation on the audio path, and the cost for 16 Tracks.
// GEARMULATOR_MD_FIRMWARE_BIN names the flash image; exit 77 without it.
// usage: mdFastTrackFxTest

#include "mdFastTables.h"
#include "mdFastTrackFx.h"

#include "mdVoice.h"
#include "mdVoiceCoefficients.h"
#include "mdVoicePiImport.h"

#include "Firmware.h"
#include "TrackFx.h"

#include "stFastMetrics.h"

#include "mdDrumsAllocationCounter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using md::engine::TrackFx;
	using md::fast::Block;
	using md::fast::BlockSize;
	using md::fast::Lanes;
	using syntakt::fast::bench::db;
	using syntakt::fast::bench::energy;
	using syntakt::fast::bench::localSnr;

	using V = __m128;

	constexpr double kRate = 44100, kFull = 8388608.0;
	constexpr double kQ2Rms = -100, kQ2Max = -90, kQ3Global = 70, kQ3Local = 60;
	constexpr double kBlockBudgetUs = 1e6 * BlockSize / kRate;
	// Under this the oracle's output is its own 24-bit rounding, and an SNR measures nothing
	constexpr double kSilence = -110;
	constexpr int kBlocks = 1024;

	int g_failures = 0;

	void check(const bool _ok, const std::string& _what)
	{
		std::printf("%s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++g_failures;
	}

	std::string fmt(const char* _format, const double _a, const double _b = 0, const double _c = 0)
	{
		char s[160];
		std::snprintf(s, sizeof(s), _format, _a, _b, _c);
		return s;
	}

	// The nine parameter words of TrackFx::setParams
	struct Params
	{
		int eqf = 0x2000, eqg = 0x1f80, fltf = 0, fltw = 0x3fff, fltq = 0, srr = 0, dist = 0;

		std::array<uint32_t, 9> words() const
		{
			return {0, 0x2000, static_cast<uint32_t>(eqf), static_cast<uint32_t>(eqg), static_cast<uint32_t>(fltf),
				static_cast<uint32_t>(fltw), static_cast<uint32_t>(fltq), static_cast<uint32_t>(srr),
				static_cast<uint32_t>(dist)};
		}
	};

	std::string text(const Params& _p)
	{
		char s[120];
		std::snprintf(s, sizeof(s), "EQF %d EQG %d FLTF %d FLTW %d FLTQ %d SRR %d DIST %d", _p.eqf >> 7, _p.eqg >> 7,
			_p.fltf >> 7, _p.fltw >> 7, _p.fltq >> 7, _p.srr >> 7, _p.dist >> 7);
		return s;
	}

	using Signal = std::vector<int32_t>;
	using Setting = std::function<Params(int _block, int _lane)>;

	// The value furthest one way, and where
	struct Mark
	{
		double value;
		std::string at;

		void above(const double _v, const std::string& _where)
		{
			if(_v > value)
			{
				value = _v;
				at = _where;
			}
		}

		void below(const double _v, const std::string& _where)
		{
			if(_v < value)
			{
				value = _v;
				at = _where;
			}
		}
	};

	// The worst of a stage over the runs
	struct Worst
	{
		Mark rms{-999, {}}, peak{-999, {}}, global{999, {}}, local{999, {}};
		int silent = 0, lanes = 0;
		size_t differ = 0;

		void add(const std::vector<double>& _ref, const std::vector<double>& _test, const std::string& _where)
		{
			++lanes;
			std::vector<double> res(_ref.size());
			double top = 0;
			for(size_t i = 0; i < res.size(); ++i)
			{
				res[i] = _test[i] - _ref[i];
				top = std::max(top, std::abs(res[i]));
				differ += res[i] != 0;
			}
			const double r = db(energy(res) / res.size()), sig = db(energy(_ref) / _ref.size());
			rms.above(r, _where);
			peak.above(db(top * top), _where);
			if(sig < kSilence)
			{
				++silent;
				return;
			}
			global.below(sig - r, _where);
			local.below(localSnr(_ref, _test, kRate), _where);
		}

		bool q2() const { return rms.value <= kQ2Rms && peak.value <= kQ2Max; }
		bool q3() const { return global.value >= kQ3Global && local.value >= kQ3Local; }

		void print(const char* _name) const
		{
			std::printf("%s: %d lanes, %d below %.0f dBFS, %zu samples differ\n", _name, lanes, silent, kSilence,
				differ);
			std::printf("  null rms %.1f dBFS at %s\n", rms.value, rms.at.c_str());
			std::printf("  null max %.1f dBFS at %s\n", peak.value, peak.at.c_str());
			std::printf("  SNR %.1f dB at %s\n", global.value, global.at.c_str());
			std::printf("  worst 10 ms %.1f dB at %s\n", local.value, local.at.c_str());
		}
	};

	struct Stages
	{
		Worst eq, filter, srrDist, chain;
	};

	// A stage's setting one step wrong, for the control
	struct Wrong
	{
		int eqf = 0, fltq = 0, srr = 0, dist = 0;
	};

	struct Chain
	{
		md::fast::Eq eq;
		md::fast::Filter filter;
		md::fast::Srr srr;
		md::fast::Dist dist;

		void set(const int _l, const Params& _p, const Wrong& _w = {})
		{
			eq.set(_l, std::min(_p.eqf + _w.eqf, 0x3fff), _p.eqg);
			filter.set(_l, _p.fltf, _p.fltw, std::min(_p.fltq + _w.fltq, 0x3fff));
			srr.set(_l, std::min(_p.srr + _w.srr, 0x3fff));
			dist.set(_l, std::min(_p.dist + _w.dist, 0x3fff));
		}

		void process(Block& _buf)
		{
			eq.process(_buf);
			filter.process(_buf);
			srr.process(_buf);
			dist.process(_buf);
		}
	};

	using Inputs = std::array<Signal, Lanes>;

	void run(const TrackFx::Tables& _tables, const char* _name, const Setting& _setting, const Inputs& _in,
		Stages& _worst, const Wrong& _wrong = {})
	{
		auto fx = std::make_unique<TrackFx>(_tables);
		auto states = std::make_unique<std::array<TrackFx::State, Lanes>>();
		for(auto& s : *states)
			TrackFx::init(s);
		auto stages = std::make_unique<Chain>(), chain = std::make_unique<Chain>();

		using Trace = std::array<std::vector<double>, Lanes>;
		Trace eqRef, eqTest, filterRef, filterTest, outRef, outTest, chainTest;
		const size_t size = static_cast<size_t>(kBlocks) * BlockSize;
		for(auto* t : {&eqRef, &eqTest, &filterRef, &filterTest, &outRef, &outTest, &chainTest})
			for(auto& v : *t)
				v.resize(size);

		auto eqIn = std::make_unique<Block>(), filterIn = std::make_unique<Block>(), srrIn = std::make_unique<Block>(),
			chainIn = std::make_unique<Block>();
		int32_t out[BlockSize];
		for(int b = 0; b < kBlocks; ++b)
		{
			const size_t at = static_cast<size_t>(b) * BlockSize;
			for(int l = 0; l < Lanes; ++l)
			{
				const Params p = _setting(b, l);
				const int32_t* in = _in[l].data() + at;
				auto& state = (*states)[l];
				TrackFx::setParams(state, p.words().data());

				// What the oracle stops at: the next stage's input and this one's reference
				const auto tap = [&](const TrackFx::Stop _stop, const int _first, Block& _next, Trace& _ref)
				{
					auto copy = state;
					fx->stopAfter = _stop;
					fx->process(copy, in, out);
					for(int n = 0; n < BlockSize; ++n)
					{
						const int32_t w = fx->scratch()[_first + n];
						_next.s[n][l] = static_cast<float>(w / kFull);
						_ref[l][at + n] = w / kFull;
					}
				};
				tap(TrackFx::Stop::AfterEq, 0xa, *filterIn, eqRef);
				tap(TrackFx::Stop::AfterFilter2, 2, *srrIn, filterRef);
				fx->stopAfter = TrackFx::Stop::None;
				fx->process(state, in, out);
				for(int n = 0; n < BlockSize; ++n)
				{
					outRef[l][at + n] = out[n] / kFull;
					eqIn->s[n][l] = chainIn->s[n][l] = static_cast<float>(in[n] / kFull);
				}
				stages->set(l, p, _wrong);
				chain->set(l, p);
			}
			stages->eq.process(*eqIn);
			stages->filter.process(*filterIn);
			stages->srr.process(*srrIn);
			stages->dist.process(*srrIn);
			chain->process(*chainIn);
			for(int l = 0; l < Lanes; ++l)
				for(int n = 0; n < BlockSize; ++n)
				{
					eqTest[l][at + n] = eqIn->s[n][l];
					filterTest[l][at + n] = filterIn->s[n][l];
					outTest[l][at + n] = srrIn->s[n][l];
					chainTest[l][at + n] = chainIn->s[n][l];
				}
		}
		for(int l = 0; l < Lanes; ++l)
		{
			const std::string where = std::string(_name) + ", " + text(_setting(kBlocks - 1, l));
			_worst.eq.add(eqRef[l], eqTest[l], where);
			_worst.filter.add(filterRef[l], filterTest[l], where);
			_worst.srrDist.add(outRef[l], outTest[l], where);
			_worst.chain.add(outRef[l], chainTest[l], where);
		}
	}

	// ---- the inputs ----

	// The families' voices, each with what its owner imports from the flash image
	struct Voices
	{
		const std::vector<uint32_t> pi;
		const std::vector<uint32_t> e12;
		const md::fw::RomBank uw;
		std::vector<uint32_t> directory = std::vector<uint32_t>(md::voice::RomBank::DirectoryWords, 0);
		md::voice::RomBank bank;
		md::voice::RomTables rom;
		md::voice::TrxTables trx;
		md::voice::OsTables os;
		std::unique_ptr<md::voice::Shared> shared = std::make_unique<md::voice::Shared>();

		Voices(const md::fw::FlashOs& _os, const std::vector<uint8_t>& _flash) : pi(md::voice::importPi(_os)),
			e12(md::fw::dspWords(_os.firmware.dspA, md::voice::E12Bank::Addr, md::voice::E12Bank::Words)),
			uw(md::fw::loadRomBankFromFlash(_flash)), os(pi.data())
		{
			md::voice::importTrx(_os.firmware, trx);
			for(const auto& e : uw.directory)
				std::copy(std::begin(e.words), std::end(e.words),
					directory.begin() + (e.addr - md::fw::RomBank::DirectoryAddr));
			bank = {directory.data(), uw.data.data(), uw.data.size()};
			shared->trx = &trx;
			shared->tables = &rom;
			shared->bank = &bank;
			shared->e12 = e12.data();
			shared->pi = pi.data();
		}

		// A Hit every 256 blocks of machine _machine (its id), its knobs at 64 but the level's
		Signal hits(const uint8_t _machine, const unsigned _seed)
		{
			std::mt19937 random(_seed);
			md::voice::Voice voice(*shared, 0);
			std::array<uint16_t, 8> params{};
			Signal out;
			out.reserve(static_cast<size_t>(kBlocks) * BlockSize);
			for(int b = 0; b < kBlocks; ++b)
			{
				if(b % md::voice::kBlocksPerTick == 0)
				{
					const bool trigger = b % 253 == 0;
					if(trigger)
						for(auto& p : params)
							p = static_cast<uint16_t>((20 + random() % 88) << 7);
					md::voice::Packet packet;
					const int n = md::voice::coefficients(os, _machine, params.data(), trigger, 3000, packet);
					voice.setSlot(packet.data(), n);
				}
				md::voice::Voice::Block frames{};
				voice.render(frames);
				out.insert(out.end(), frames.begin(), frames.end());
			}
			return out;
		}
	};

	Signal noise(const double _level, const unsigned _seed)
	{
		std::mt19937 random(_seed);
		std::uniform_real_distribution<double> u(-_level, _level);
		Signal out(static_cast<size_t>(kBlocks) * BlockSize);
		for(auto& s : out)
			s = static_cast<int32_t>(u(random) * kFull);
		return out;
	}

	Signal sine(const double _hz, const double _level)
	{
		Signal out(static_cast<size_t>(kBlocks) * BlockSize);
		for(size_t n = 0; n < out.size(); ++n)
			out[n] = static_cast<int32_t>(_level * kFull * std::sin(2 * syntakt::fast::bench::Pi * _hz * n / kRate));
		return out;
	}

	constexpr int word(const int _kit) { return _kit << 7; }

	// ---- the sweeps ----

	// The Kit values a lane sweeps
	constexpr int spread(const int _l) { return _l == 15 ? 127 : _l * 8; }

	Stages sweeps(const TrackFx::Tables& _t, const Inputs& _hits, const Inputs& _loud, const Inputs& _quiet,
		const Inputs& _low)
	{
		Stages w;
		const std::array<const Inputs*, 4> inputs = {&_hits, &_loud, &_quiet, &_low};
		const char* const names[4] = {"Hits", "noise 0.9", "noise 0.1", "sines"};
		for(size_t i = 0; i < inputs.size(); ++i)
		{
			const auto& in = *inputs[i];
			const std::string name = names[i];
			// The EQ: every 8th frequency by 8 gains, the extremes on both sides of the flat word
			for(const int eqg : {0, 1, 30, 62, 63, 64, 100, 127})
				run(_t, (name + " EQ").c_str(), [eqg](int, const int _l)
				{
					Params p;
					p.eqf = word(spread(_l));
					p.eqg = word(eqg);
					return p;
				}, in, w);

			// The filter: edges by resonances, FLTF and FLTW crossing the top
			for(const int fltq : {0, 1, 32, 64, 100, 127})
				for(const int fltw : {0, 8, 40, 127})
					run(_t, (name + " filter").c_str(), [fltq, fltw](int, const int _l)
					{
						Params p;
						p.fltf = word(spread(_l));
						p.fltw = word(fltw);
						p.fltq = word(fltq);
						return p;
					}, in, w);

			// SRR and DIST, each alone and together
			run(_t, (name + " SRR").c_str(), [](int, const int _l)
			{
				Params p;
				p.srr = word(spread(_l));
				return p;
			}, in, w);
			run(_t, (name + " DIST").c_str(), [](int, const int _l)
			{
				Params p;
				p.dist = word(spread(_l));
				return p;
			}, in, w);
			run(_t, (name + " SRR off and on").c_str(), [](const int _b, const int _l)
			{
				Params p;
				p.srr = (_b / 8 + _l / 4) & 1 ? word(127) : 0;
				return p;
			}, in, w);
			run(_t, (name + " SRR and DIST at the top").c_str(), [](int, const int _l)
			{
				Params p;
				p.srr = word(127 - _l % 2 * 20);
				p.dist = word(127 - _l * 2);
				return p;
			}, in, w);

			// Hot: boost and resonance push the DSP's stores and DIST's shift past their limits
			run(_t, (name + " hot").c_str(), [](int, const int _l)
			{
				Params p;
				p.eqf = word(40 + _l * 4);
				p.eqg = word(127);
				p.fltf = word(30 + _l * 4);
				p.fltw = word(10);
				p.fltq = word(127);
				p.dist = word(127 - _l);
				return p;
			}, in, w);

			// Moves between blocks: sweeps up and down over a few blocks, random jumps, every Track its own
			run(_t, (name + " swept").c_str(), [](const int _b, const int _l)
			{
				const int period = 16 << (_l % 4);
				const int phase = (_b + _l * 7) % (2 * period);
				const int ramp = (phase < period ? phase : 2 * period - phase) * 0x3fff / period;
				Params p;
				p.eqf = ramp;
				p.eqg = 0x3fff - ramp;
				p.fltf = ramp;
				p.fltw = (_l & 1) ? ramp : 0x3fff - ramp;
				p.fltq = word(_l * 8);
				return p;
			}, in, w);
			run(_t, (name + " jumps").c_str(), [](const int _b, const int _l)
			{
				std::mt19937 r(static_cast<unsigned>(_b / 8 * 16 + _l));
				Params p;
				p.eqf = static_cast<int>(r() % 0x4000);
				p.eqg = static_cast<int>(r() % 0x4000);
				p.fltf = static_cast<int>(r() % 0x4000);
				p.fltw = static_cast<int>(r() % 0x4000);
				p.fltq = static_cast<int>(r() % 0x4000);
				p.srr = static_cast<int>(r() % 0x4000);
				p.dist = static_cast<int>(r() % 0x4000);
				return p;
			}, in, w);
		}
		return w;
	}

	// ---- the controls: one step wrong must fail ----

	void controls(const TrackFx::Tables& _t, const Inputs& _noise, const Inputs& _quiet)
	{
		const auto with = [&](const Setting& _setting, const Inputs& _in, const Wrong& _wrong = {})
		{
			Stages s;
			run(_t, "control", _setting, _in, s, _wrong);
			return s;
		};
		const auto both = [](const char* _name, const bool _right, const bool _wrongPasses, const double _value)
		{
			check(_right, std::string(_name) + " passes right");
			check(!_wrongPasses, fmt((std::string(_name) + " fails one step wrong, at %.1f").c_str(), _value));
		};

		const Setting setting = [](int, const int _l)
		{
			Params p;
			p.eqf = word(20 + _l * 6);
			p.eqg = word(100);
			p.fltf = word(10 + _l * 6);
			p.fltw = word(30);
			p.fltq = word(64);
			p.srr = word(127);
			p.dist = word(64);
			return p;
		};
		const Stages right = with(setting, _quiet);

		Wrong step;
		step.eqf = 8;			// one word of the scale
		auto wrong = with(setting, _quiet, step);
		both("the EQ, EQF one word off", right.eq.q2(), wrong.eq.q2(), wrong.eq.rms.value);

		step = {};
		step.fltq = 128;		// one Kit value
		wrong = with(setting, _quiet, step);
		both("the filter, FLTQ one Kit value off", right.filter.q3(), wrong.filter.q3(), wrong.filter.global.value);

		const Setting top = [&setting](const int _b, const int _l)
		{
			Params p = setting(_b, _l);
			p.srr = word(_l);
			return p;
		};
		step = {};
		step.srr = 128;
		const Stages topRight = with(top, _noise);
		wrong = with(top, _noise, step);
		both("SRR, one Kit value off", topRight.srrDist.q2(), wrong.srrDist.q2(), wrong.srrDist.rms.value);

		const Setting linear = [](int, const int _l)
		{
			Params p;
			p.dist = word(_l * 5);
			return p;
		};
		step = {};
		step.dist = 64;			// one word of its table, half a Kit value
		const Stages linearRight = with(linear, _quiet);
		wrong = with(linear, _quiet, step);
		both("DIST, half a Kit value off", linearRight.srrDist.q3(), wrong.srrDist.q3(), wrong.srrDist.global.value);
	}

	// ---- the tables ----

	struct Difference
	{
		int count, worst;
	};

	template<typename Table>
	Difference differences(const char* _name, const Table& _ours, const TrackFx::Tables& _os, const uint32_t _addr)
	{
		Difference d{0, 0};
		for(size_t i = 0; i < _ours.size(); ++i)
		{
			const int e = std::abs(static_cast<int>(_ours[i]) - _os[_addr + static_cast<uint32_t>(i)]);
			d.count += e != 0;
			d.worst = std::max(d.worst, e);
		}
		std::printf("table %-12s %4d words, %d differ, by %d at most\n", _name, static_cast<int>(_ours.size()), d.count,
			d.worst);
		return d;
	}

	void tablesAgainstOs(const TrackFx::Tables& _os)
	{
		const auto& t = md::fast::tables::tables();
		int exact = 0;
		exact += differences("cutoffCos", t.cutoffCos, _os, 0x140000).count;
		exact += differences("cutoffExp", t.cutoffExp, _os, 0x141554).count;
		exact += differences("eqCos", t.eqCos, _os, 0x142000).count;
		exact += differences("eqExp", t.eqExp, _os, 0x142a1d).count;
		exact += differences("resonanceCos", t.resonanceCos, _os, 0x141f00).count;
		exact += differences("resonanceExp", t.resonanceExp, _os, 0x141f80).count;
		exact += differences("eqPoleCos", t.eqPoleCos, _os, 0x143476).count;
		exact += differences("eqZeroCos", t.eqZeroCos, _os, 0x1434f6).count;
		exact += differences("eqPoleExp", t.eqPoleExp, _os, 0x143576).count;
		exact += differences("eqZeroExp", t.eqZeroExp, _os, 0x1435f6).count;
		check(exact == 0, "the filter's and the EQ's tables equal the OS's, word for word");
		const Difference gain = differences("distGain", t.distGain, _os, 0x143777);
		check(gain.count <= 15 && gain.worst <= 1, "distGain's words stay 1 LSB apart, few of them");
		const Difference filter = differences("distFilter", t.distFilter, _os, 0x143878);
		check(filter.count <= 11 && filter.worst <= 1, "distFilter's words stay 1 LSB apart, few of them");
	}

	// SRR's frequency stays where the rate moves: its hold takes as long at twice the rate, twice the samples
	void srrRate()
	{
		const auto holds = [](const double _rate)
		{
			auto srr = std::make_unique<md::fast::Srr>(_rate);
			auto block = std::make_unique<Block>();
			for(int l = 0; l < Lanes; ++l)
				srr->set(l, word(127));
			int changes = 0, counted = 0;
			float last = 0;
			for(int b = 0; b < 2000; ++b)
			{
				for(int n = 0; n < BlockSize; ++n)
				{
					++counted;
					std::fill(block->s[n], block->s[n] + Lanes, static_cast<float>(counted) / 8388608);
				}
				srr->process(*block);
				for(int n = 0; n < BlockSize; ++n)
				{
					changes += block->s[n][0] != last;
					last = block->s[n][0];
				}
			}
			return static_cast<double>(counted) / changes;
		};
		const double machine = holds(kRate), twice = holds(2 * kRate);
		std::printf("SRR at the top holds %.2f samples at 44.1 kHz, %.2f at 88.2\n", machine, twice);
		check(std::abs(twice - 2 * machine) < 0.1, "SRR keeps its frequency at twice the rate");
	}

	// ---- cost ----

	void noiseBlock(Block& _b, const float _level)
	{
		std::mt19937 random(7);
		std::uniform_real_distribution<float> u(-_level, _level);
		for(auto& s : _b.s)
			for(auto& l : s)
				l = u(random);
	}

	// The fastest of 7 runs of _blocks calls
	template<typename Function>
	double usPerBlock(const int _blocks, Function&& _block)
	{
		using Clock = std::chrono::steady_clock;
		double best = 1e9;
		for(int run = 0; run < 7; ++run)
		{
			const auto start = Clock::now();
			for(int b = 0; b < _blocks; ++b)
				_block();
			best = std::min(best, std::chrono::duration<double, std::micro>(Clock::now() - start).count() / _blocks);
		}
		return best;
	}

	Params costSetting(const int _l)
	{
		Params p;
		p.eqf = word(30 + _l);
		p.eqg = word(100);
		p.fltf = word(20 + _l);
		p.fltw = word(60);
		p.fltq = word(90);
		p.srr = p.dist = word(40);
		return p;
	}

	void costs(const TrackFx::Tables& _t)
	{
		constexpr int kPerRun = 3000;
		auto chain = std::make_unique<Chain>();
		auto input = std::make_unique<Block>(), buf = std::make_unique<Block>();
		noiseBlock(*input, 0.1f);
		for(int l = 0; l < Lanes; ++l)
			chain->set(l, costSetting(l));

		const double copy = usPerBlock(kPerRun, [&] { *buf = *input; });
		const auto line = [](const char* _name, const double _us)
		{
			std::printf("cost of 16 Tracks: %-30s %7.2f us a block, %5.2f %% of a core\n", _name, _us,
				100 * _us / kBlockBudgetUs);
		};
		const auto stage = [&](auto&& _process)
		{
			return usPerBlock(kPerRun, [&]
			{
				*buf = *input;
				_process();
			}) - copy;
		};
		int flip = 0;
		const auto move = [&]
		{
			flip ^= 1;
			for(int l = 0; l < Lanes; ++l)
				chain->filter.set(l, word(20 + l + flip), word(60), word(90));
		};
		const double eq = stage([&] { chain->eq.process(*buf); });
		const double still = stage([&] { chain->filter.process(*buf); });
		const double moving = stage([&]
		{
			move();
			chain->filter.process(*buf);
		});
		const double srr = stage([&] { chain->srr.process(*buf); });
		const double dist = stage([&] { chain->dist.process(*buf); });
		line("EQ", eq);
		line("filter, still", still);
		line("filter, moving", moving);
		line("SRR", srr);
		line("DIST", dist);
		line("the stages' sum, still", eq + still + srr + dist);
		line("the stages' sum, moving", eq + moving + srr + dist);
		line("chain, still", stage([&] { chain->process(*buf); }));
		line("chain, moving", stage([&]
		{
			move();
			chain->process(*buf);
		}));

		for(int l = 0; l < Lanes; ++l)
			chain->srr.set(l, 0);
		line("SRR at 0", stage([&] { chain->srr.process(*buf); }));

		auto fx = std::make_unique<TrackFx>(_t);
		auto states = std::make_unique<std::array<TrackFx::State, Lanes>>();
		for(int l = 0; l < Lanes; ++l)
		{
			TrackFx::init((*states)[l]);
			TrackFx::setParams((*states)[l], costSetting(0).words().data());
		}
		int32_t in[BlockSize], out[BlockSize];
		for(int n = 0; n < BlockSize; ++n)
			in[n] = static_cast<int32_t>(input->s[n][0] * kFull);
		line("TrackFx (AMD included)", usPerBlock(300, [&]
		{
			for(auto& s : *states)
				fx->process(s, in, out);
		}));
	}

	// The host may leave denormals on: FTZ and DAZ off while _body runs
	template<typename Function>
	auto withDenormals(Function&& _body)
	{
		struct Restore
		{
			unsigned int csr = _mm_getcsr();
			Restore() { _mm_setcsr(csr & ~0x8040u); }
			~Restore() { _mm_setcsr(csr); }
		} restore;
		return _body();
	}

	// Whether _body met a denormal, by the processor's own sticky flags, DE and UE: the time it costs says little here,
	// a Ryzen 3700X pays 25 % for a block of them
	template<typename Function>
	bool meetsDenormals(Function&& _body)
	{
		_mm_setcsr(_mm_getcsr() & ~0x3fu);
		_body();
		return (_mm_getcsr() & 0x12u) != 0;
	}

	// Q14: a block after 10 s of silence that follows a loud sound, against an active block, with denormals on; and no
	// denormal met on the way, which a float product that makes one proves the flags would show
	double denormals()
	{
		return withDenormals([]
		{
			const V tiny = _mm_set1_ps(1e-20f);
			volatile float keep = 0;
			check(meetsDenormals([&] { keep = _mm_cvtss_f32(_mm_mul_ps(tiny, tiny)); }), "the flags see a denormal");

			auto chain = std::make_unique<Chain>();
			auto input = std::make_unique<Block>(), zero = std::make_unique<Block>(), buf = std::make_unique<Block>();
			noiseBlock(*input, 0.5f);
			std::fill(&zero->s[0][0], &zero->s[0][0] + BlockSize * Lanes, 0.0f);
			for(int l = 0; l < Lanes; ++l)
			{
				Params p;
				p.eqf = word(10 + l * 7);
				p.eqg = word(120);
				p.fltf = word(5 + l);
				p.fltw = word(80);
				p.fltq = word(120);
				p.srr = word(10);
				p.dist = word(20);
				chain->set(l, p);
			}
			const auto render = [&](const Block& _in)
			{
				*buf = _in;
				chain->process(*buf);
			};
			double active = 0, silent = 0;
			const bool met = meetsDenormals([&]
			{
				active = usPerBlock(2000, [&] { render(*input); });
				for(int b = 0; b < static_cast<int>(10 * kRate / BlockSize); ++b)
					render(*zero);
				silent = usPerBlock(2000, [&] { render(*zero); });
			});
			check(!met, "no denormal in the stages, loud or over 10 s of silence");
			std::printf("Q14: an active block %.2f us, one after 10 s of silence %.2f us\n", active, silent);
			return silent / active;
		});
	}

	void allocations()
	{
		auto chain = std::make_unique<Chain>();
		auto input = std::make_unique<Block>(), buf = std::make_unique<Block>();
		noiseBlock(*input, 0.5f);
		const size_t count = countAllocations([&]
		{
			for(int b = 0; b < 100; ++b)
			{
				*buf = *input;
				for(int l = 0; l < Lanes; ++l)
					chain->filter.set(l, word(b), word(60), word(90));
				chain->process(*buf);
			}
		});
		check(count == 0, "no allocation in the stages");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const char* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	if(!path || !*path)
	{
		std::printf("SKIP: set GEARMULATOR_MD_FIRMWARE_BIN to the Machinedrum UW flash image\n");
		return 77;
	}
	try
	{
		const auto flash = md::fw::readFile(path);
		const auto os = md::fw::loadFirmwareFromFlash(flash);
		const auto tables = std::make_unique<TrackFx::Tables>(os.firmware);

		tablesAgainstOs(*tables);

		// TRX-BD to -CY, EFM-BD and -SD, E12-BD and -SD, P-I-BD, ROM-01
		constexpr std::array<uint8_t, Lanes> kMachines = {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 32, 33, 48, 49, 64,
			128};
		auto voices = std::make_unique<Voices>(os, flash);
		Inputs hits, loud, quiet, low;
		double peak = 0;
		for(int l = 0; l < Lanes; ++l)
		{
			hits[l] = voices->hits(kMachines[l], 100u + l);
			double top = 0;
			for(const int32_t s : hits[l])
				top = std::max(top, std::abs(s) / kFull);
			check(top > 0, "machine " + std::to_string(kMachines[l]) + " sounds");
			peak = std::max(peak, top);
			loud[l] = noise(0.9, 200u + l);
			quiet[l] = noise(0.1, 300u + l);
			low[l] = sine(30 * std::pow(1.5, l), 0.5);
		}
		std::printf("the Hits peak at %.3f of full scale\n", peak);

		const Stages w = sweeps(*tables, hits, loud, quiet, low);
		w.eq.print("EQ");
		check(w.eq.q2(), fmt("EQ Q2: %.1f dBFS rms, %.1f max", w.eq.rms.value, w.eq.peak.value));
		w.filter.print("filter");
		check(w.filter.q3(), fmt("filter Q3: %.1f dB, worst 10 ms %.1f", w.filter.global.value,
			w.filter.local.value));
		w.srrDist.print("SRR through DIST");
		check(w.srrDist.q3(), fmt("DIST Q3: %.1f dB, worst 10 ms %.1f", w.srrDist.global.value,
			w.srrDist.local.value));
		check(w.srrDist.q2(), fmt("SRR Q13: %.1f dBFS rms, %.1f max", w.srrDist.rms.value, w.srrDist.peak.value));
		w.chain.print("the whole chain");
		check(w.chain.q2(), fmt("the chain Q2: %.1f dBFS rms, %.1f max", w.chain.rms.value, w.chain.peak.value));
		check(w.eq.differ + w.filter.differ == 0, "every sample of the EQ and the filter equals the DSP's");

		controls(*tables, loud, quiet);

		srrRate();
		allocations();
		check(denormals() <= 1.2, "Q14");

		costs(*tables);
	}
	catch(const std::exception& _e)
	{
		std::printf("FAIL: %s\n", _e.what());
		return 1;
	}
	std::printf(g_failures ? "%d FAILED\n" : "OK\n", g_failures);
	return g_failures ? 1 : 0;
}
