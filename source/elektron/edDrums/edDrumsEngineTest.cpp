// The instrument's engine on its criteria (ticket 11 of .scratch/elektron-drums-v1):
//
//   - every machine of both families: a Hit gives the same samples wherever its note falls, a fixed delay after it;
//     note-to-sound measured on each, and on a running engine at every position of the 147-block cycle of 48 kHz;
//   - the same samples whatever the host's blocks, each Track on its output, its velocity and level, its Locks;
//   - lanes: a machine's Hits on its first lanes, the groups above let go, a machine with no voice not rendered;
//   - an off Track and an absent family cost nothing, nothing allocated on the audio path, no denormal;
//   - Q12: a Choke and a machine change on a sounding voice, without a Hit, MID and CTR machines too;
//   - what switching a silent voice off changes against rendering all 16 (md::voice's shared scratch, its free-running
//     state), and what a Hit bringing its Track's ticks changes, measured;
//   - the OS's machine table against mdProtocol's names;
//   - ticket 12: a Track in its family's chain against TrackFx, word for word, and against its voice alone; Q5 on a voice
//     with every slot off; a heavy chain's Hits at their position, what it does to latency; velocity's target, accent.
//
// GEARMULATOR_MD_FIRMWARE_BIN names the flash image; exit 77 without it. SP TWINSHOT plays noise made here.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "Firmware.h"
#include "TrackFx.h"
#include "edDrumsEngine.h"
#include "edDrumsTestSupport.h"
#include "mdDrumsAllocationCounter.h"
#include "mdProtocol/mdmachines.h"
#include "stFastBdModern.h"
#include "stFastCpVintage.h"
#include "stFastCyAlloy.h"
#include "stFastMetrics.h"
#include "stFastPcCarbon.h"
#include "stFastResample.h"
#include "stFastSdBasic.h"
#include "stFastSdVintage.h"
#include "stFastSpTwinshot.h"
#include "stFastSyBits.h"
#include "stFastSyToy.h"

namespace
{
	using namespace elektron::drums;
	using namespace elektron::drums::test;
	using elektron::BlockSize;
	using syntakt::fast::bench::highShare;
	using syntakt::fast::bench::Pi;
	using Clock = std::chrono::steady_clock;

	constexpr int HitSamples = 1500;	// a Hit compared from its onset

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	float peakOf(const std::vector<float>& _x)
	{
		float peak = 0;
		for(const float v : _x)
			peak = std::max(peak, std::abs(v));
		return peak;
	}

	// A fresh engine, _machine on Track 0 at full level
	std::unique_ptr<Engine> engineWith(const Library& _library, const Machine& _machine)
	{
		auto e = std::make_unique<Engine>(_library);
		e->setMachine(0, _machine);
		e->setLevel(0, 127);
		return e;
	}

	const ChainSettings Bare{};		// every slot off

	// ... hit at _note, in _chain or its family's, its drives at _factor, SYN1-8 at _knobs or its defaults
	Take hitAt(const Library& _library, const Machine& _machine, const int _note, const int _samples,
		const ChainSettings* _chain = nullptr, const int _factor = Engine::DefaultOversampling,
		const std::array<uint8_t, 8>* _knobs = nullptr)
	{
		auto e = engineWith(_library, _machine);
		e->setOversampling(_factor);
		if(_chain)
			e->setChain(0, *_chain);
		for(int i = 0; _knobs && i < 8; ++i)
			e->setParameter(0, i, (*_knobs)[static_cast<size_t>(i)]);
		Take take(_samples);
		take.render(*e, 0, _note);
		e->noteOn(0, 127);
		take.render(*e, _note, _samples - _note);
		return take;
	}

	// Every machine, at positions of the block of 32 and of the 147 blocks of 32 of the 48 kHz cycle
	void hits(const Library& _library)
	{
		const std::array<int, 12> positions{0, 1, 2, 15, 30, 31, 32, 33, 1021, 2340, 4703, 4704 + 77};
		constexpr int Delay = Engine::HitDelay;
		std::map<int, int> onsets;
		std::string late;
		for(const auto& machine : machines())
		{
			const auto reference = hitAt(_library, machine, 0, Delay + HitSamples);
			const auto& r = reference.channel(0);
			const float peak = peakOf(r);
			if(dataOf(_library, machine) == MachineData::None)
			{
				require(peak == 0, nameOf(machine) + " sounds");
				continue;
			}
			require(peak > 0, nameOf(machine) + " is silent");
			int onset = 0;
			while(std::abs(r[static_cast<size_t>(onset)]) < peak * 0.01f)
				++onset;
			++onsets[onset - Delay];
			if(onset - Delay > 3 || onset < Delay)
				late += " " + nameOf(machine) + " " + std::to_string(onset - Delay) + ",";
			for(const int position : positions)
			{
				const auto take = hitAt(_library, machine, position, position + Delay + HitSamples);
				const auto& y = take.channel(0);
				for(int n = 0; n < Delay + HitSamples; ++n)
					if(y[static_cast<size_t>(position + n)] != r[static_cast<size_t>(n)])
						throw std::runtime_error(nameOf(machine) + " at note " + std::to_string(position) + " differs "
							+ std::to_string(n) + " samples after it");
			}
		}
		std::printf("  a Hit on each of %zu machines at %zu positions: the same samples, jitter 0; 1 %% of its peak, after "
			"HitDelay (%d samples, %.2f ms):", machines().size(), positions.size(), Delay, Delay / 44.1);
		for(const auto& [offset, count] : onsets)
			std::printf(" %+d (%d)", offset, count);
		std::printf("\n    earlier, or past +3:%s\n", late.c_str());
	}

	// BD MODERN and GND-IM, whose Hits do not depend on the last, on a running engine, a Hit at each position of the 48 kHz
	// cycle, in random host blocks: where it crosses half its first peak, from the note
	void running(const Library& _library)
	{
		constexpr int Cycle = 147 * 32, Spacing = 2053;
		for(const bool bare : {true, false})
		for(const Machine machine : {Machine{Family::Syntakt, 0}, Machine{Family::Machinedrum, 3}})
		{
			auto e = engineWith(_library, machine);
			if(bare)
				e->setChain(0, ChainSettings{});
			// BD MODERN's jump: Hit 0 of its comparison, no sweep
			if(machine.family == Family::Syntakt)
				for(const int i : {3, 4, 5})
					e->setParameter(0, i, 0);
			Take take(Spacing * (Cycle + 2));
			std::mt19937 random(5);
			int at = 0;
			double lo = 1e9, hi = -1e9;
			const auto advance = [&](const int _to)
			{
				while(at < _to)
				{
					const int n = std::min<int>(_to - at, 1 + static_cast<int>(random() % 700));
					take.render(*e, at, n);
					at += n;
				}
			};
			std::vector<int> notes;
			for(int k = 0; k < Cycle; ++k)
			{
				const int note = (k + 1) * Spacing + (k * 37 + 11) % 32;
				advance(note);
				e->noteOn(0, 127);
				notes.push_back(note);
			}
			advance(take.samples());
			const auto& y = take.channel(0);
			const auto crossing = [&](const int _note)
			{
				// The first peak of the Hit, then where the line before it crosses its half
				const int from = _note + Engine::HitDelay;
				int top = from;
				for(int n = from; n < from + 60; ++n)
					if(std::abs(y[static_cast<size_t>(n)]) > std::abs(y[static_cast<size_t>(top)]))
						top = n;
				// Through its filter GND-IM's impulse has no slope to cross: its peak, a whole sample
				if(!bare && machine.family == Family::Machinedrum)
					return static_cast<double>(top - _note);
				const float half = y[static_cast<size_t>(top)] / 2;
				int i = top;
				while(i > from - 20 && std::abs(y[static_cast<size_t>(i - 1)]) > std::abs(half))
					--i;
				const double a = y[static_cast<size_t>(i - 1)], b = y[static_cast<size_t>(i)];
				return i - 1 + (half - a) / (b - a) - _note;
			};
			for(const int note : notes)
			{
				const double c = crossing(note);
				lo = std::min(lo, c);
				hi = std::max(hi, c);
			}
			std::printf("  %s %s on a running engine, %d Hits over the 48 kHz cycle: half its first peak %.3f to %.3f samples "
				"after the note (jitter %.3f samples, %.5f ms)\n", nameOf(machine).c_str(), bare ? "alone" : "in its chain", Cycle, lo,
				hi, hi - lo, (hi - lo) / 44.1);
			// In its chain BASE/WIDTH's high-pass leaves the last Hit's tail off its zero, and the ring adds what the two Hits
			// overlap: the half-peak moves, the Hit does not, and only at this retrigger, 46 ms
			require(hi - lo < (bare ? 0.02 : 0.25), "a Hit moved on a running engine");
		}
	}

	// The Track's read against the family's resampler it replaces (Q11's, stFastResampleTest): BD MODERN's Hit through
	// fast::Host, whose phase falls with its block, one of the 147 blocks of the cycle giving the engine's samples
	void againstHost(const Library& _library)
	{
		const Machine machine{Family::Syntakt, 0};
		const auto reference = hitAt(_library, machine, 0, 4000, &Bare);
		const auto& y = reference.channel(0);
		const auto sound = defaults(machine);
		constexpr int Compared = 2000, First = Engine::HitDelay - syntakt::fast::Resample48To44::Latency;
		double best = 1e9;
		for(int block = 0; block < 147; ++block)
		{
			auto host = std::make_unique<syntakt::fast::Host<syntakt::fast::BdModern>>(44100.0f);
			for(int i = 0; i < 8; ++i)
				host->setParameter(0, i, sound[static_cast<size_t>(i)], 0);
			host->setNote(0, 60);
			std::array<std::array<float, BlockSize>, Tracks> buffers;
			std::array<float*, Tracks> p{};
			for(size_t t = 0; t < p.size(); ++t)
				p[t] = buffers[t].data();
			std::vector<float> x;
			for(int b = 0; b < block + 2 + (Compared + 200) / BlockSize; ++b)
			{
				if(b == block)
					host->trig(0);
				host->render(p.data());
				x.insert(x.end(), buffers[0].begin(), buffers[0].end());
			}
			// Their first sound lined up, within a sample
			size_t onset = 0;
			while(onset < x.size() && x[onset] == 0)
				++onset;
			for(size_t at = onset > 1 ? onset - 1 : 0; at < onset + 2; ++at)
			{
				double worst = 0;
				for(int n = 0; n < Compared; ++n)
					worst = std::max<double>(worst, std::abs(x[at + static_cast<size_t>(n)]
						- y[static_cast<size_t>(First + n)]));
				best = std::min(best, worst);
			}
		}
		std::printf("  BD MODERN's Hit against fast::Host's resampler, the best of the 147 blocks: within %.1f dB\n",
			20 * std::log10(std::max(best, 1e-12)));
		require(best < 1e-5, "the Track's read is not the family's resampler");
	}

	struct Note
	{
		int at, track, velocity;
	};

	std::vector<Note> pattern(const int _samples, const uint32_t _seed)
	{
		std::mt19937 random(_seed);
		std::vector<Note> notes;
		for(int at = 200; at < _samples - 2000; at += 1 + static_cast<int>(random() % 900))
			notes.push_back({at, static_cast<int>(random() % Tracks), 1 + static_cast<int>(random() % 127)});
		return notes;
	}

	// Every kind of stage at once, on the side the firmware does not have them, velocity moving the filter
	ChainSettings heavyChain()
	{
		ChainSettings s;
		s.drive[0] = {DriveType::Syntakt, true, 60, 127};
		s.drive[1] = {DriveType::MdDist, false, 50, 80};
		s.band.type = BandType::ThreeBand;
		s.band.after = true;
		s.band.values = {40, 80, 70, 50, 100, 70};
		s.filter.type = FilterType::Syntakt;
		s.filter.mode = 0;
		s.filter.frequency = 100;
		s.filter.resonance = 40;
		s.filter.depth = 90;
		s.filter.envelope.decay = 40;
		s.amp.on = true;
		s.amp.decay = 70;
		s.amp.hold = 20;
		s.velocityTarget = VelocityTarget::Frequency;
		s.velocityDepth = -40;
		return s;
	}

	// A mixed kit, Track t on Output t % 5, in blocks that _block gives
	template<typename Blocks>
	Take play(const Library& _library, const std::vector<Note>& _notes, const int _samples, Blocks&& _block)
	{
		auto e = std::make_unique<Engine>(_library);
		const auto list = machines();
		for(int t = 0; t < Tracks; ++t)
		{
			e->setMachine(t, list[static_cast<size_t>((t * 7) % 40)]);
			e->setOutput(t, t % 5);
			e->setPan(t, t * 8);
			if(t % 4 == 1)
				e->setChain(t, heavyChain());
		}
		e->setChokes(2, 1u << 3);
		Take take(_samples);
		int at = 0;
		size_t next = 0;
		while(at < _samples)
		{
			const int end = std::min(_samples, at + _block());
			for(int from = at; from < end;)
			{
				const int to = next < _notes.size() ? std::min(end, _notes[next].at) : end;
				take.render(*e, from, to - from);
				from = to;
				for(; next < _notes.size() && _notes[next].at == from; ++next)
				{
					e->lock(_notes[next].track, 2, static_cast<uint8_t>(_notes[next].velocity));
					e->noteOn(_notes[next].track, _notes[next].velocity);
				}
			}
			at = end;
		}
		return take;
	}

	void blocks(const Library& _library)
	{
		constexpr int Samples = 200000;
		const auto notes = pattern(Samples, 3);
		const auto whole = play(_library, notes, Samples, [] { return 64; });
		std::mt19937 random(9);
		const auto same = [&](const Take& _take, const char* _what)
		{
			for(int c = 0; c < Channels; ++c)
				if(whole.channel(c) != _take.channel(c))
					throw std::runtime_error("channel " + std::to_string(c) + " differs in " + _what);
		};
		same(play(_library, notes, Samples, [] { return 32; }), "blocks of 32");
		same(play(_library, notes, Samples, [&] { return 1 + static_cast<int>(random() % 97); }), "blocks of 1 to 97");
		int sounding = 0;
		for(int c = 0; c < Channels; ++c)
			sounding += *std::max_element(whole.channel(c).begin(), whole.channel(c).end()) > 0;
		require(sounding == 10, "the five outputs in use sound, the others do not");
		std::printf("  %zu notes on 16 Tracks of both families, a Choke and Locks: every channel the same in blocks of 64, "
			"32 and 1 to 97\n", notes.size());
	}

	void routing(const Library& _library)
	{
		for(const int output : {0, 3, 15})
		{
			auto e = std::make_unique<Engine>(_library);
			e->setMachine(0, {Family::Machinedrum, 17});
			e->setOutput(0, output);
			e->setPan(0, 127);
			e->noteOn(0, 127);
			Take take(3000);
			take.render(*e, 0, 3000);
			for(int c = 0; c < Channels; ++c)
			{
				const float peak = *std::max_element(take.channel(c).begin(), take.channel(c).end());
				if((peak != 0) != (c == 2 * output + 1))
					throw std::runtime_error("output " + std::to_string(output) + ": channel " + std::to_string(c));
			}
		}
		// A bus the host does not take: its Track goes nowhere, nothing else moves
		auto e = std::make_unique<Engine>(_library);
		e->setMachine(0, {Family::Syntakt, 0});
		e->setOutput(0, 7);
		e->noteOn(0, 100);
		std::array<float, 256> left{}, right{};
		std::array<float*, Channels> p{};
		p[0] = left.data();
		p[1] = right.data();
		e->render(p.data(), 256);
		require(left[255] == 0 && right[255] == 0, "a Track on a bus nobody takes");

		// Velocity and level scale the Hit
		const auto loudest = [&](const int _level, const int _velocity)
		{
			auto engine = std::make_unique<Engine>(_library);
			engine->setMachine(0, {Family::Syntakt, 1});
			engine->setLevel(0, _level);
			engine->noteOn(0, _velocity);
			Take take(2000);
			take.render(*engine, 0, 2000);
			return peakOf(take.channel(0));
		};
		const float full = loudest(127, 127);
		require(std::abs(loudest(127, 64) / full - 64.0f / 127) < 1e-3f, "velocity on the level");
		require(std::abs(loudest(64, 127) / full - 64.0f * 64 / (127 * 127)) < 1e-3f, "level squared");
		std::printf("  outputs: a Track on its bus alone, panned, velocity and level as gains\n");
	}

	int renderedSources(const Engine& _e)
	{
		int n = 0;
		for(int m = 0; m < SyntaktMachines; ++m)
			n += _e.lanesRendered(m) > 0;
		return n;
	}

	void lanes(const Library& _library)
	{
		auto e = std::make_unique<Engine>(_library);
		const Machine bd{Family::Syntakt, 0}, sd{Family::Syntakt, 1};
		for(int t = 0; t < 6; ++t)
			e->setMachine(t, bd);
		e->setMachine(6, sd);
		require(renderedSources(*e) == 0 && e->liveTracks() == 0, "no voice before a Hit");
		for(int t = 0; t < 4; ++t)
			e->noteOn(t, 100);
		require(e->lanesRendered(0) == 4, "4 Tracks on 4 lanes");
		e->noteOn(4, 100);
		e->noteOn(6, 100);
		require(e->lanesRendered(0) == 8 && e->lanesRendered(1) == 4 && renderedSources(*e) == 2, "a fifth on a second group");
		e->setMachine(4, sd);	// its voice let go after its ramp
		Take take(4096);
		take.render(*e, 0, 4096);
		require(e->lanesRendered(0) == 4, "the second group freed");
		e->noteOn(4, 100);
		require(e->lanesRendered(1) == 4, "the Track on its new machine's lanes");
		std::printf("  lanes: Tracks on a machine's first lanes, a group freed by a machine change, setLanes follows\n");
	}

	// ---- what costs nothing ----

	double blockUs(Engine& _e, const int _blocks)
	{
		Take take(128);
		double best = 1e9;
		for(int run = 0; run < 5; ++run)
		{
			const auto t0 = Clock::now();
			for(int b = 0; b < _blocks; ++b)
				take.render(_e, 0, 128);
			best = std::min(best, std::chrono::duration<double, std::micro>(Clock::now() - t0).count() / _blocks);
		}
		return best;
	}

	void off(const Library& _library)
	{
		constexpr double PerCent = 100.0 / (128 / 44100.0 * 1e6);
		auto none = std::make_unique<Engine>(_library);
		const double nothing = blockUs(*none, 2000);

		// Eight Tracks of each family, hit, then their voices off
		auto e = std::make_unique<Engine>(_library);
		for(int t = 0; t < Tracks; ++t)
		{
			const Machine m = t & 1 ? Machine{Family::Machinedrum, 17} : Machine{Family::Syntakt, 1};
			e->setMachine(t, m);
			e->noteOn(t, 100);
		}
		require(e->liveTracks() == 0xffff, "16 voices live");
		Take take(128);
		int blocksToOff = 0;
		const auto sounding = blockUs(*e, 2);
		while(e->liveTracks() != 0)
		{
			take.render(*e, 0, 128);
			require(++blocksToOff < 4 * 44100 * 10 / 128, "a voice never went off");
		}
		require(renderedSources(*e) == 0, "a Syntakt machine rendered with no voice");
		const double idle = blockUs(*e, 2000);
		std::printf("  a block of 128: engine with nothing %.3f us (%.4f %% of a core), 16 Tracks of both families off "
			"%.3f us (%.4f %%), both after their voices ended (%.2f s), sounding %.1f us\n", nothing, nothing * PerCent, idle,
			idle * PerCent, blocksToOff * 128 / 44100.0, sounding);
		require(idle * PerCent < 0.05 && nothing * PerCent < 0.05, "an off Track or an absent family costs");
		// Q14: a block after a loud Hit's silence, against a block before it
		require(idle <= 1.2 * sounding, "Q14: denormals in the silence after a Hit");
	}

	void allocations(const Library& _library)
	{
		auto e = std::make_unique<Engine>(_library);
		const auto list = machines();
		Take take(128);
		std::mt19937 random(4);
		const auto allocated = countAllocations([&]
		{
			for(int b = 0; b < 3000; ++b)
			{
				const int t = static_cast<int>(random() % Tracks);
				if(b % 7 == 0)
					e->setMachine(t, list[random() % list.size()]);
				if(b % 11 == 0)
					e->setChain(t, b % 2 ? heavyChain() : ChainSettings{});
				e->setChokes(t, random() & 0xffff);
				e->lock(t, 3, 100);
				e->noteOn(t, 100);
				take.render(*e, 0, 128);
			}
		});
		require(allocated == 0, std::to_string(allocated) + " allocations on the audio path");
		std::printf("  3000 blocks of Hits, Chokes, Locks and machine changes: nothing allocated\n");
	}

	// ---- Q12 ----

	// What holds near a 200 Hz sine: GND-SN, or BD MODERN held, on Track 0
	struct Tone
	{
		const char* name;
		Machine machine;
		std::array<uint8_t, 8> params;
		int note;
		// HF bounds, measured with 1 dB of margin. They guard what a struck voice adds to a sounding one: its restart at
		// its block and the splice of its tail, which are the machine's own, not a cut's Q12
		double retriggerBound, chokedHitBound;
		double softerBound, raisedBound;					// struck again, 127 to 64; through a raised drive
		double cutSofterBound, cutSoftestBound, cutLouderBound;	// cut and struck, 127 to 64, 127 to 1, 1 to 127
	};

	std::unique_ptr<Engine> engineWith(const Library& _library, const Tone& _tone)
	{
		auto e = engineWith(_library, _tone.machine);
		for(int i = 0; i < 8; ++i)
			e->setParameter(0, i, _tone.params[static_cast<size_t>(i)]);
		e->setNote(0, _tone.note);
		return e;
	}

	void q12(const Library& _library)
	{
		constexpr int Start = 200, Around = 12000, Samples = 16384;
		constexpr int Delay = Engine::HitDelay;
		const std::array<Tone, 2> tones{{
			{"MD sine", {Family::Machinedrum, 1}, {35, 127, 0, 0, 0, 0, 0, 0}, 0, -25.0, -25.0, -23.4, -20.2, -21.9, -22.6, -36.5},
			{"Syntakt held", {Family::Syntakt, 0}, {64, 0, 0, 0, 0, 0, 127, 127}, 79, -39.0, -39.7, -39.6, -37.7, -22.1, -23.0, -40.2},
		}};
		struct Cut
		{
			const char* name;
			int kind;
			Machine to;
		};
		const std::array<Cut, 7> cuts{{
			{"Choked by an MD Hit", 0, {}},
			{"Choked by a Syntakt Hit", 1, {}},
			{"machine change to MD", 2, {Family::Machinedrum, 17}},
			{"machine change to Syntakt", 2, {Family::Syntakt, 1}},
			{"machine change to MID", 2, {Family::Machinedrum, 0x60}},
			{"machine change to CTR", 2, {Family::Machinedrum, 0x70}},
			{"Track off", 2, {Family::Off, 0}},
		}};
		for(const auto& tone : tones)
		{
			auto engine = engineWith(_library, tone);
			Take uncut(Samples);
			uncut.render(*engine, 0, Start);
			engine->noteOn(0, 127);
			uncut.render(*engine, Start, Samples - Start);
			const auto& u = uncut.channel(0);
			// A crest, and how far the tone is from a pure one there
			int crest = Around;
			while(!(u[static_cast<size_t>(crest)] >= u[static_cast<size_t>(crest - 1)]
				&& u[static_cast<size_t>(crest)] >= u[static_cast<size_t>(crest + 1)] && u[static_cast<size_t>(crest)] > 0))
				++crest;
			int period = crest + 1;
			while(!(u[static_cast<size_t>(period)] >= u[static_cast<size_t>(period - 1)]
				&& u[static_cast<size_t>(period)] >= u[static_cast<size_t>(period + 1)] && u[static_cast<size_t>(period)] > 0))
				++period;
			const double base = highShare(u, crest, SampleRate);
			std::printf("  Q12, %s at %.0f Hz, crest %.3f, its own HF %.1f dB:", tone.name, SampleRate / (period - crest),
				u[static_cast<size_t>(crest)], base);
			require(u[static_cast<size_t>(crest)] > 0.05f, "a tone to cut");
			for(const auto& cut : cuts)
			{
				auto e = engineWith(_library, tone);
				e->setMachine(1, cut.kind == 1 ? Machine{Family::Syntakt, 0} : Machine{Family::Machinedrum, 0});
				e->setOutput(1, 15);
				e->setChokes(1, 1);
				Take take(Samples);
				take.render(*e, 0, Start);
				e->noteOn(0, 127);
				take.render(*e, Start, crest - Delay - Start);
				if(cut.kind < 2)
				{
					e->noteOn(1, 127);
				}
				else
				{
					e->setMachine(0, cut.to);
					for(int i = 0; i < 8; ++i)
						if(cut.to.family == Family::Machinedrum)
							require(e->parameter(0, i) == defaults(cut.to)[static_cast<size_t>(i)],
								"the new machine's defaults");
				}
				take.render(*e, crest - Delay, Samples - (crest - Delay));
				const auto& y = take.channel(0);
				// Until the cut as it was, nothing after the ramp
				const int cutAt = crest;
				for(int n = Start; n < cutAt - 40; ++n)
					if(y[static_cast<size_t>(n)] != u[static_cast<size_t>(n)])
						throw std::runtime_error(std::string(cut.name) + ": the tone changed before its cut");
				float rest = 0;
				for(int n = cutAt + Engine::CutSamples + 40; n < Samples; ++n)
					rest = std::max(rest, std::abs(y[static_cast<size_t>(n)]));
				const double v = highShare(y, cutAt, SampleRate);
				std::printf(" %s %.1f,", cut.name, v);
				require(v <= -50, std::string(tone.name) + ", " + cut.name + ": Q12, HF " + std::to_string(v) + " dB");
				require(rest == 0, std::string(cut.name) + ": sound past the ramp");
				require(!(e->liveTracks() & 1) || cut.kind == 2, std::string(cut.name) + ": the voice stayed");
			}
			std::printf(" dB\n");

			// The same voice struck again there, with a choking Hit at the same sample: what the note, the voice in its block
			// not yet begun, makes of the cut
			{
				auto e = engineWith(_library, tone);
				e->setMachine(1, {Family::Machinedrum, 0});
				e->setOutput(1, 15);
				e->setChokes(1, 1);
				Take take(Samples);
				take.render(*e, 0, Start);
				e->noteOn(0, 127);
				take.render(*e, Start, crest - Delay - Start);
				e->noteOn(0, 127);
				e->noteOn(1, 127);
				take.render(*e, crest - Delay, Samples - (crest - Delay));
				float rest = 0;
				for(int n = crest + Engine::CutSamples + 40; n < Samples; ++n)
					rest = std::max(rest, std::abs(take.channel(0)[static_cast<size_t>(n)]));
				const double v = highShare(take.channel(0), crest, SampleRate);
				std::printf("    a Hit and the Hit that chokes it at one sample, %s: HF %.1f dB, %g past the ramp\n", tone.name, v,
					rest);
				require(rest == 0, "the choked Hit sounds past the ramp");
				require(v <= tone.chokedHitBound, std::string(tone.name) + ": a choked Hit's HF " + std::to_string(v));
			}

			// The same voice struck again there: the voice's restart and the splice of its tail. A Syntakt Hit also moves the
			// phase its Track is read at, which is not what this measures: frozen, the HF moves by 0.1 dB at most
			double lo = 0, hi = -200;
			for(int offset = 0; offset < 40; offset += 3)
			{
				auto e = engineWith(_library, tone);
				Take take(Samples);
				take.render(*e, 0, Start);
				e->noteOn(0, 127);
				take.render(*e, Start, crest - Delay - Start + offset);
				e->noteOn(0, 127);
				take.render(*e, crest - Delay + offset, Samples - (crest - Delay + offset));
				const double v = highShare(take.channel(0), crest + offset, SampleRate);
				lo = std::min(lo, v);
				hi = std::max(hi, v);
			}
			std::printf("    the voice struck again at the crest, %s's own retrigger: HF %.1f to %.1f dB\n", tone.name, lo, hi);
			require(hi <= tone.retriggerBound, std::string(tone.name) + ": a retrigger's HF " + std::to_string(hi));

			// A second Hit at another velocity, the old voice cut at its sample or not: the worst of 14 places around the crest
			const auto struck = [&](const int _first, const int _second, const bool _cut, const ChainSettings* _chain)
			{
				double worst = -200;
				for(int offset = 0; offset < 40; offset += 3)
				{
					auto e = engineWith(_library, tone);
					if(_chain)
						e->setChain(0, *_chain);
					e->setMachine(1, {Family::Machinedrum, 0});
					e->setOutput(1, 15);
					e->setChokes(1, _cut ? 1 : 0);
					Take take(Samples);
					take.render(*e, 0, Start);
					e->noteOn(0, _first);
					take.render(*e, Start, crest - Delay - Start + offset);
					if(_cut)
						e->noteOn(1, 127);
					e->noteOn(0, _second);
					take.render(*e, crest - Delay + offset, Samples - (crest - Delay + offset));
					worst = std::max(worst, highShare(take.channel(0), crest + offset, SampleRate));
				}
				return worst;
			};
			for(const bool cut : {false, true})
			{
				const double softer = struck(127, 64, cut, nullptr), softest = struck(127, 1, cut, nullptr);
				const double louder = struck(1, 127, cut, nullptr);
				std::printf("    %s, %s: 127 to 127 %.1f, 127 to 64 %.1f, 127 to 1 %.1f, 1 to 127 %.1f dB\n", tone.name,
					cut ? "cut then struck" : "struck again", struck(127, 127, cut, nullptr), softer, softest, louder);
				require(softer <= (cut ? tone.cutSofterBound : tone.softerBound), std::string(tone.name) + ": 127 to 64");
				require(!cut || (softest <= tone.cutSoftestBound && louder <= tone.cutLouderBound), std::string(tone.name) + ": a cut under another velocity");
			}

			// A raised drive's delay is not a pure one: its Hit keeps its first samples, the last voice's stay in them
			ChainSettings raised;
			raised.drive[0] = {tone.machine.family == Family::Syntakt ? DriveType::MdDist : DriveType::Syntakt, false, 1, 127};
			const double throughRaised = struck(127, 127, false, &raised);
			std::printf("    %s through a raised drive, struck again: HF %.1f dB\n", tone.name, throughRaised);
			require(throughRaised <= tone.raisedBound, std::string(tone.name) + ": a raised drive's retrigger");
		}
	}

	// Every lane of a machine held, a Track choked and struck again at once: its Hit, the quietest, takes the lane the cut
	// voice is still fading on
	void fullMachine(const Library& _library)
	{
		constexpr int Start = 200, Around = 12000, Samples = 16384;
		const Tone tone{"Syntakt held", {Family::Syntakt, 0}, {64, 0, 0, 0, 0, 0, 127, 127}, 79, 0, 0, 0, 0, 0, 0, 0};
		const auto play = [&](const int _at, const bool _cut)
		{
			auto e = std::make_unique<Engine>(_library);
			for(int t = 0; t < Tracks; ++t)
			{
				e->setMachine(t, tone.machine);
				e->setLevel(t, 127);
				e->setOutput(t, t ? 15 : 0);
				e->setNote(t, tone.note);
				for(int i = 0; i < 8; ++i)
					e->setParameter(t, i, tone.params[static_cast<size_t>(i)]);
			}
			Take take(Samples);
			take.render(*e, 0, Start);
			for(int t = 0; t < Tracks; ++t)
				e->noteOn(t, 127);
			take.render(*e, Start, _at - Start);
			if(_cut)
			{
				e->setChokes(1, 1);
				e->noteOn(1, 127);
				e->noteOn(0, 1);
				require(e->lanesRendered(0) == 16, "the lanes all held");
			}
			take.render(*e, _at, Samples - _at);
			return take;
		};
		const auto uncut = play(Start + 1, false);
		const auto& u = uncut.channel(0);
		int crest = Around;
		while(!(u[static_cast<size_t>(crest)] >= u[static_cast<size_t>(crest - 1)]
			&& u[static_cast<size_t>(crest)] >= u[static_cast<size_t>(crest + 1)] && u[static_cast<size_t>(crest)] > 0))
			++crest;
		const auto take = play(crest - Engine::HitDelay, true);
		const double v = highShare(take.channel(0), crest, SampleRate);
		std::printf("  all 16 lanes of a machine held, a Track choked and struck at once: HF %.1f dB\n", v);
		// Measured, 1 dB of margin: the machine's own fade of the block that sees the trig ends the ramp, 32 samples, not
		// Q12's 64. Without it, the lane freed at once, -26 dB
		require(v <= -40.7, "a lane taken from a cut voice clicks");
	}

	// A machine change, its Hit at once: the old voice's ramp and a Hit as a fresh Track's, whichever follows which
	void changeAndHit(const Library& _library)
	{
		constexpr int Samples = 6000, At = 2000;
		for(const Machine& from_ : {Machine{Family::Machinedrum, 17}, Machine{Family::Syntakt, 1}})
			for(const Machine& to : {Machine{Family::Machinedrum, 33}, Machine{Family::Syntakt, 3}})
			{
				const auto run = [&](const bool _old, const bool _new)
				{
					auto e = std::make_unique<Engine>(_library);
					e->setMachine(0, from_);
					Take take(Samples);
					take.render(*e, 0, 100);
					if(_old)
						e->noteOn(0, 100);
					take.render(*e, 100, At - 100);
					e->setMachine(0, to);
					// A lock of the old machine's goes with it
					e->lock(0, 2, 3);
					e->setMachine(0, from_);
					e->setMachine(0, to);
					if(_new)
						e->noteOn(0, 100);
					take.render(*e, At, Samples - At);
					return take;
				};
				const auto both = run(true, true), tail = run(true, false), fresh = run(false, true);
				// One chain has one place in its ring, the Hit's from the block it starts: under the Hit the old voice's ramp,
				// on the same family, moves with it, up to a block. Past the ramp, a fresh Hit
				const bool sameChain = from_.family == to.family;
				const int from = sameChain ? At + Engine::HitDelay + Engine::CutSamples : 0;
				double worst = 0;
				float peak = 0;
				for(int n = from; n < Samples; ++n)
				{
					const auto i = static_cast<size_t>(n);
					worst = std::max<double>(worst, std::abs(both.channel(0)[i] - tail.channel(0)[i] - fresh.channel(0)[i]));
					peak = std::max(peak, std::abs(both.channel(0)[i]));
				}
				std::printf("  %s to %s, a Hit at once: the tail plus a fresh Hit, within %.1f dB of the peak\n",
					nameOf(from_).c_str(), nameOf(to).c_str(), 20 * std::log10(std::max(worst, 1e-12) / peak));
				// What is left past the ramp: the chain's own memory of the last voice, its high-pass at 5 Hz, as on the machine
				// -45.6 dB measured
				require(worst / peak < (sameChain ? 6e-3 : 1e-3), "a Hit after a machine change is not a fresh one");
			}
	}

	struct VoiceSet
	{
		std::unique_ptr<md::voice::Shared> shared = std::make_unique<md::voice::Shared>();
		std::vector<md::voice::Voice> voices;
		std::array<md::voice::Track, Tracks> tracks{};
		std::array<int, Tracks> quiet{};
		uint32_t live = 0;

		explicit VoiceSet(const Library& _l)
		{
			shared->tables = &_l.rom;
			shared->bank = &_l.bank;
			shared->trx = &_l.trx;
			shared->e12 = _l.e12.data();
			shared->pi = _l.pi.data();
			for(uint32_t t = 0; t < Tracks; ++t)
				voices.emplace_back(*shared, t);
		}
	};

	// ---- the chain on the engine ----

	// Q1: a Machinedrum Track in its family's chain against TrackFx on its voice alone, word for word; a Syntakt one, its filter
	// off, against its voice alone, a high-pass at 5 Hz apart. In its family's own, against the exact port: edDrumsChainTest
	void familyChains(const std::vector<uint8_t>& _flash, const Library& _library)
	{
		const auto os = md::fw::loadFirmwareFromFlash(_flash);
		using md::engine::TrackFx;
		const auto tables = std::make_unique<TrackFx::Tables>(os.firmware);
		constexpr int Samples = 32 * 400;
		std::array<uint32_t, 9> params{};
		params[2] = 64 << 7;
		params[3] = 63 << 7;
		params[5] = 127 << 7;
		size_t compared = 0;
		for(const uint8_t id : {uint8_t(3), uint8_t(16), uint8_t(17), uint8_t(19), uint8_t(25), uint8_t(32), uint8_t(33), uint8_t(48), uint8_t(64), uint8_t(128)})
		{
			const Machine machine{Family::Machinedrum, id};
			const auto bare = hitAt(_library, machine, 0, Samples, &Bare);
			const auto chained = hitAt(_library, machine, 0, Samples);
			auto fx = std::make_unique<TrackFx>(*tables);
			auto state = std::make_unique<TrackFx::State>();
			TrackFx::init(*state);
			TrackFx::setParams(*state, params.data());
			std::array<int32_t, 32> in{}, out{};
			for(int b = 0; b < 400; ++b)
				fx->process(*state, in.data(), out.data());
			std::vector<int32_t> reference;
			for(int b = 0; b < Samples / 32; ++b)
			{
				for(int n = 0; n < 32; ++n)
					in[static_cast<size_t>(n)] = static_cast<int32_t>(std::lround(bare.channel(0)[static_cast<size_t>(b * 32 + n)] * 8388608.0));
				fx->process(*state, in.data(), out.data());
				reference.insert(reference.end(), out.begin(), out.end());
			}
			// TrackFx's 4 samples of delay are the chain's, and every chain's Hit takes them off its wait
			for(int n = 0; n + Chain::MachinedrumFilterDelay < Samples; ++n)
			{
				const double y = chained.channel(0)[static_cast<size_t>(n)] * 2097152.0;
				if(y != reference[static_cast<size_t>(n + Chain::MachinedrumFilterDelay)])
					throw std::runtime_error(nameOf(machine) + ": not TrackFx's word " + std::to_string(n) + ": "
						+ std::to_string(y) + " against " + std::to_string(reference[static_cast<size_t>(n + Chain::MachinedrumFilterDelay)]));
				++compared;
			}
		}
		std::printf("  Q1: 10 Machinedrum machines in their chain, %zu samples: TrackFx's words x 4, 0 LSB apart\n", compared);

		double worst = -300;
		auto filterOff = syntaktChain();
		filterOff.filter.mode = 7;
		for(int id = 0; id < SyntaktMachines; ++id)
		{
			const Machine machine{Family::Syntakt, static_cast<uint8_t>(id)};
			const auto bare = hitAt(_library, machine, 0, 8000, &Bare), chained = hitAt(_library, machine, 0, 8000, &filterOff);
			double power = 0, error = 0;
			for(size_t n = 0; n < 8000; ++n)
			{
				const double a = bare.channel(0)[n], d = chained.channel(0)[n] - a;
				power += a * a;
				error += d * d;
			}
			worst = std::max(worst, 10 * std::log10(error / power));
		}
		std::printf("  the nine Syntakt machines in their chain, the filter off, against their voice alone: %.1f dB of the signal at "
			"worst (BASE/WIDTH's 5 Hz)\n", worst);
		require(worst < -26, "a Syntakt machine's chain is not its voice");
	}

	// Q5: with every slot off, the voice's own samples through the rings
	void bareVoice(const Library& _library)
	{
		constexpr int Blocks = 400;
		const uint8_t id = 17;
		VoiceSet set(_library);
		set.tracks[0].setMachine(id, defaults({Family::Machinedrum, id}));
		set.tracks[0].hit();
		set.tracks[0].alignTicks(0);
		const auto take = hitAt(_library, {Family::Machinedrum, id}, 0, Blocks * BlockSize, &Bare);
		const auto& y = take.channel(0);
		int first = 0;
		while(y[static_cast<size_t>(first)] == 0)
			++first;
		std::vector<double> voice;
		for(int b = 0; b < Blocks; ++b)
		{
			md::voice::prepareBlock(set.tracks, set.voices, *_library.os, 120 * 24, b, 1, 1);
			md::voice::Voice::Block block;
			set.voices[0].render(block);
			for(const int32_t x : block)
				voice.push_back(x / 8388608.0);
		}
		int firstVoice = 0;
		while(voice[static_cast<size_t>(firstVoice)] == 0)
			++firstVoice;
		double error = 0;
		for(int n = 0; first + n < static_cast<int>(y.size()); ++n)
			error = std::max(error, std::abs(y[static_cast<size_t>(first + n)] - voice[static_cast<size_t>(firstVoice + n)]));
		std::printf("  Q5: TRX-SD with every slot off against its voice: %.1f dBFS at worst (threshold -120); BD MODERN against its "
			"machine above\n", 20 * std::log10(std::max(error, 1e-30)));
		require(error < 1e-6, "Q5: a Track with every slot off is not its voice");
	}

	// Heavy chains: a Hit's samples wherever its note falls, on each family's Tracks, and where it lands
	void chainsAtHits(const Library& _library)
	{
		constexpr int Delay = Engine::HitDelay, Samples = Delay + 1500;
		const auto onset = [](const std::vector<float>& _y)
		{
			const float peak = peakOf(_y);
			int n = 0;
			while(std::abs(_y[static_cast<size_t>(n)]) < peak * 0.01f)
				++n;
			return n;
		};
		// The Hit at _position, against the one at 0 _position samples on
		const auto same = [&](const Machine& _machine, const std::vector<float>& _reference, const int _position,
			const ChainSettings& _chain, const int _factor)
		{
			auto y = _reference;
			y.insert(y.begin(), static_cast<size_t>(_position), 0.0f);
			return hitAt(_library, _machine, _position, _position + Samples, &_chain, _factor).channel(0) == y;
		};
		for(const Machine machine : {Machine{Family::Syntakt, 1}, Machine{Family::Syntakt, 2}, Machine{Family::Machinedrum, 17}, Machine{Family::Machinedrum, 34}})
			for(const int factor : {1, 2, 4})
			{
				const auto chain = heavyChain();
				const auto reference = hitAt(_library, machine, 0, Samples, &chain, factor);
				for(const int position : {1, 15, 31, 33, 1021, 4703})
					if(!same(machine, reference.channel(0), position, chain, factor))
						throw std::runtime_error(nameOf(machine) + ": a Hit in a chain moved with its note");
			}

		// What a raised drive adds: the voice's onset through a light drive, against the voice alone
		for(const Machine machine : {Machine{Family::Syntakt, 1}, Machine{Family::Machinedrum, 17}})
		{
			ChainSettings raised;
			raised.drive[0] = {machine.family == Family::Syntakt ? DriveType::MdDist : DriveType::Syntakt, false, 1, 127};
			const int alone = onset(hitAt(_library, machine, 0, Samples, &Bare).channel(0));
			std::printf("  %s, onset %d samples after the note alone;", nameOf(machine).c_str(), alone);
			for(const int factor : {1, 2, 4})
			{
				const int shift = onset(hitAt(_library, machine, 0, Samples, &raised, factor).channel(0)) - alone;
				std::printf(" a raised drive at %dx: %+d", factor, shift);
				// A Hit waits less by its chain's delay, a low frequency's: what is left is where the onset's edge falls
				const int expected = factor == 1 ? 0 : machine.family == Family::Syntakt ? 1 : -2;
				require(shift == expected, "a raised drive's Hit, off its stated place");
			}
			std::printf("\n");
		}

		// The longest wait HitDelay is made for: the latest voice, and on the Syntakt the longest chain, wherever its note falls
		for(const Machine machine : {Machine{Family::Syntakt, 0}, Machine{Family::Machinedrum, 18}})
		{
			const DriveType type = machine.family == Family::Syntakt ? DriveType::MdDist : DriveType::Syntakt;
			ChainSettings longest;
			longest.drive[0] = {type, false, 1, 127};
			longest.drive[1] = {type, false, 1, 127};
			longest.filter.type = FilterType::Machinedrum;
			const auto reference = hitAt(_library, machine, 0, Samples, &longest, 4);
			for(int position = 1; position < 2 * BlockSize; ++position)
				if(!same(machine, reference.channel(0), position, longest, 4))
					throw std::runtime_error(nameOf(machine) + ": the longest chain moved its Hit, note at " + std::to_string(position));
		}
		std::printf("  heavy chains on both families, off, 2x and 4x: a Hit's samples the same at 6 positions, the longest chain at 63, jitter 0\n");
	}

	void velocityAndAccent(const Library& _library)
	{
		const Machine machine{Family::Syntakt, 3};
		const auto play = [&](const ChainSettings& _chain, const int _velocity, const bool _accent)
		{
			auto e = engineWith(_library, machine);
			e->setChain(0, _chain);
			e->setAccent(0, 20);
			e->noteOn(0, _velocity, _accent);
			Take take(6000);
			take.render(*e, 0, 6000);
			return take;
		};
		// The filter's frequency follows velocity: the Hit's first differences, against its level
		ChainSettings chain = syntaktChain();
		chain.filter.mode = 0;
		chain.filter.frequency = 40;
		chain.velocityTarget = VelocityTarget::Frequency;
		chain.velocityDepth = 80;
		const auto brightness = [](const Take& _take)
		{
			double slope = 0, level = 0;
			const auto& y = _take.channel(0);
			for(size_t n = 1; n < y.size(); ++n)
			{
				slope += (y[n] - y[n - 1]) * (y[n] - y[n - 1]);
				level += y[n] * y[n];
			}
			return slope / level;
		};
		const double soft = brightness(play(chain, 10, false)), loud = brightness(play(chain, 127, false));
		require(loud > 4 * soft, "velocity does not reach its target");
		chain.velocityTarget = VelocityTarget::None;
		const double plain = brightness(play(chain, 127, false));
		require(std::abs(plain / brightness(play(chain, 10, false)) - 1) < 1e-3, "velocity changes more than the level");

		const float normal = peakOf(play(ChainSettings{}, 127, false).channel(0)), accented = peakOf(play(ChainSettings{}, 127, true).channel(0));
		std::printf("  velocity on the filter's frequency: %.1f times the brightness from velocity 10 to 127; accent 20: %.3f times the "
			"peak, %.3f expected\n", loud / soft, accented / normal, 168.0 / 127);
		require(std::abs(accented / normal - 168.0f / 127) < 1e-3f, "accent");
	}

	// A closed hat choking an open hat, the two in one group
	void chokedNeighbour(const Library& _library)
	{
		const Machine machine{Family::Machinedrum, 22};
		const auto play = [&](const bool _before, const bool _bare)
		{
			auto e = std::make_unique<Engine>(_library);
			for(const int t : {2, 3})
			{
				e->setMachine(t, machine);
				e->setLevel(t, 127);
				if(_bare)
					e->setChain(t, Bare);
			}
			e->setOutput(2, 1);
			e->setChokes(2, 1u << 3);
			Take take(40000);
			take.render(*e, 0, 1000);
			if(_before)
				e->noteOn(3, 127);
			take.render(*e, 1000, 4000);
			e->noteOn(2, 127);
			take.render(*e, 5000, 5000);
			e->noteOn(3, 127);
			take.render(*e, 10000, 30000);
			return take;
		};
		const auto apart = [&](const bool _bare)
		{
			const auto dirty = play(true, _bare), fresh = play(false, _bare);
			double worst = 0;
			for(size_t n = 10000; n < 40000; ++n)
				worst = std::max<double>(worst, std::abs(dirty.channel(0)[n] - fresh.channel(0)[n]));
			return 20 * std::log10(std::max(worst, 1e-12));
		};
		// The voice itself keeps something of its last Hit: the chain must add nothing to that
		const double chained = apart(false), voice = apart(true);
		std::printf("  an open hat struck again after its Choke, against a fresh one: apart by %.1f dB in its chain, %.1f dB alone\n", chained, voice);
		require(chained <= voice + 1, "a choked Track came back with its group's rows");
	}

	// A chain set while the Track plays nothing
	void chainWhileOff(const Library& _library)
	{
		const Machine machine{Family::Syntakt, 1};
		ChainSettings s = syntaktChain();
		s.filter.mode = 0;
		s.filter.frequency = 50;
		auto e = engineWith(_library, machine);
		e->setMachine(0, {Family::Off, 0});
		e->setChain(0, s);
		e->setMachine(0, machine);
		e->noteOn(0, 127);
		Take take(4000);
		take.render(*e, 0, 4000);
		auto d = engineWith(_library, machine);
		d->setChain(0, s);
		d->noteOn(0, 127);
		Take direct(4000);
		direct.render(*d, 0, 4000);
		const bool same = take.channel(0) == direct.channel(0);
		std::printf("  a chain set on an Off Track plays when its machine comes back: %s\n", same ? "yes" : "no");
		require(same, "the chain set while Off was lost");
	}

	// setChain during a Hit leaves what velocity set
	void midHit(const Library& _library)
	{
		const Machine machine{Family::Syntakt, 3};
		ChainSettings s = syntaktChain();
		s.filter.mode = 0;
		s.filter.frequency = 40;
		s.filter.keyTrack = 60;
		s.velocityTarget = VelocityTarget::Frequency;
		s.velocityDepth = 70;
		const auto play = [&](const bool _again)
		{
			auto e = engineWith(_library, machine);
			e->setChain(0, s);
			e->noteOn(0, 127);
			Take take(6000);
			take.render(*e, 0, 1000);
			if(_again)
				e->setChain(0, s);
			take.render(*e, 1000, 5000);
			return take;
		};
		const auto plain = play(false), rechained = play(true);
		double worst = 0;
		for(size_t n = 0; n < 6000; ++n)
			worst = std::max<double>(worst, std::abs(plain.channel(0)[n] - rechained.channel(0)[n]));
		std::printf("  setChain during a Hit, the same chain: apart by %.1f dB\n", 20 * std::log10(std::max(worst, 1e-12)));
		require(worst == 0, "setChain lost the Hit's velocity");
	}

	// A raised drive's Hit keeps the samples it has before its onset
	void earlySamples(const Library& _library)
	{
		const Machine machine{Family::Machinedrum, 17};
		ChainSettings raised;
		raised.drive[0] = {DriveType::Syntakt, false, 1, 127};
		auto e = engineWith(_library, machine);
		e->setChain(0, raised);
		e->noteOn(0, 127);
		Take take(2000);
		take.render(*e, 0, 2000);
		int first = 0;
		while(take.channel(0)[static_cast<size_t>(first)] == 0)
			++first;
		const auto bare = hitAt(_library, machine, 0, 2000, &Bare);
		int alone = 0;
		while(bare.channel(0)[static_cast<size_t>(alone)] == 0)
			++alone;
		std::printf("  first sample of a raised Hit %d, of the voice alone %d\n", first, alone);
		require(first == alone - Oversampler::delay(2), "a raised Hit lost its first samples");
	}

	// A noteOff on a Track at rest
	void staleGateOff(const Library& _library)
	{
		const Machine machine{Family::Syntakt, 0};
		ChainSettings s = syntaktChain();
		s.amp.on = true;
		s.amp.decay = 60;
		auto e = engineWith(_library, machine);
		e->setChain(0, s);
		const auto hit = [&]
		{
			e->noteOn(0, 127);
			Take take(3000);
			take.render(*e, 0, 3000);
			return take;
		};
		const auto first = hit();
		Take rest(4096);
		int guard = 0;
		while(e->lanesRendered(0) && ++guard < 2000)
			rest.render(*e, 0, 4096);
		e->noteOff(0);
		const auto second = hit();
		double worst = 0;
		for(size_t n = 0; n < 3000; ++n)
			worst = std::max<double>(worst, std::abs(first.channel(0)[n] - second.channel(0)[n]));
		std::printf("  a Hit after a noteOff at rest against the first: apart by %.1f dB\n", 20 * std::log10(std::max(worst, 1e-12)));
		require(worst < 1e-3, "a stale gate-off");
	}

	// A Choke and a Hit at one sample on a machine that starts late: the tail no louder than it was
	void tailGain(const Library& _library)
	{
		const Machine machine{Family::Machinedrum, 16};
		double worst = 0;
		for(int at = 400; at < 700; at += 3)
		{
			const auto render = [&](const bool _cut)
			{
				auto e = engineWith(_library, machine);
				e->setChain(0, ChainSettings{});
				e->setMachine(1, {Family::Machinedrum, 0});
				e->setOutput(1, 15);
				e->setChokes(1, 1);
				Take take(4000);
				take.render(*e, 0, 100);
				e->noteOn(0, 127);
				take.render(*e, 100, at - 100);
				if(_cut)
				{
					e->noteOn(1, 127);
					e->noteOn(0, 1);
				}
				take.render(*e, at, 4000 - at);
				return take;
			};
			const auto uncut = render(false), cut = render(true);
			// What a Hit leaves of the cut voice before its ramp: at most the voice, but what the ring holds where the Hit
			// moved its place, which adds
			float top = 0, tail = 0;
			for(size_t n = static_cast<size_t>(at + Engine::HitDelay - 34); n < static_cast<size_t>(at + Engine::HitDelay); ++n)
			{
				top = std::max(top, std::abs(uncut.channel(0)[n]));
				tail = std::max(tail, std::abs(cut.channel(0)[n]));
			}
			worst = std::max<double>(worst, tail / std::max(top, 1e-6f));
		}
		std::printf("  under a Hit a cut tail at worst %.3f of the uncut voice before its ramp\n", worst);
		require(worst <= 1.014, "the ramp above 1");		// 1.012 measured; a ramp above 1 gives 1.016
	}

	// Two Hits of a Track in one 48 kHz block, the first on the later machine's source
	void twoHitsInABlock(const Library& _library)
	{
		const Machine first{Family::Syntakt, 1}, second{Family::Syntakt, 0};
		double worst = 0;
		for(int at = 1000; at < 1033; ++at)
		{
			const auto play = [&](const bool _both)
			{
				auto e = engineWith(_library, first);
				Take take(6000);
				take.render(*e, 0, at);
				if(_both)
					e->noteOn(0, 127);
				take.render(*e, at, 1);
				e->setMachine(0, second);
				e->noteOn(0, 60);
				take.render(*e, at + 1, 6000 - at - 1);
				return take;
			};
			const auto both = play(true), alone = play(false);
			for(size_t n = 1400; n < 6000; ++n)
				worst = std::max<double>(worst, std::abs(both.channel(0)[n] - alone.channel(0)[n]));
		}
		std::printf("  two Hits in one block, the later's: apart by %.1f dB at worst of 33 positions\n", 20 * std::log10(std::max(worst, 1e-12)));
		require(worst < 1e-3, "the earlier Hit won");
	}

	// A second Hit of a Track before the first's phase has come, HitDelay - Latency - 1 samples after its note: the first plays
	// on the phase before it, up to a sample of 48 kHz off, until the second's. The second's samples are its own, and past
	// the window the first is not touched before the block that starts the second's ramp, which moves with the Hit
	void hitInPhaseWindow(const Library& _library)
	{
		const Machine machine{Family::Syntakt, 0};
		constexpr int First = 1000, Samples = 6000;
		constexpr int Early = syntakt::fast::Resample48To44::Latency, Window = Engine::HitDelay - Early - 1;
		const double peak = peakOf(hitAt(_library, machine, First, Samples, &Bare).channel(0));
		double second = 0, inWindow = 0, pastWindow = 0;
		for(const int older : {77, 84, 91, 98, 105})		// an older Hit, all but silent, leaves the Track on a phase of its own
		{
			const auto play = [&](const std::vector<int>& _notes)
			{
				auto e = engineWith(_library, machine);
				e->setChain(0, Bare);
				Take take(Samples);
				take.render(*e, 0, older);
				e->noteOn(0, 1);
				int at = older;
				for(const int note : _notes)
				{
					take.render(*e, at, note - at);
					e->noteOn(0, 127);
					at = note;
				}
				take.render(*e, at, Samples - at);
				return take;
			};
			const auto alone = play({First});
			for(int d = 1; d <= Window + 8; ++d)
			{
				const int note = First + d;
				const auto both = play({First, note}), later = play({note});
				for(size_t n = static_cast<size_t>(note + Engine::HitDelay + Engine::CutSamples); n < Samples; ++n)
					second = std::max<double>(second, std::abs(both.channel(0)[n] - later.channel(0)[n]));
				if(d < BlockSize)		// the first may not have reached a block: the later Hit takes its place
					continue;
				double& first = d <= Window ? inWindow : pastWindow;
				for(int n = First; n < note + Engine::HitDelay - Early - BlockSize; ++n)
					first = std::max<double>(first, std::abs(both.channel(0)[static_cast<size_t>(n)] - alone.channel(0)[static_cast<size_t>(n)]));
			}
		}
		const auto relative = [&](const double _x) { return 20 * std::log10(std::max(_x / peak, 1e-12)); };
		std::printf("  a second Hit 1 to %d samples after the first (the phase's window %d): its own samples within %.1f dB of the peak past "
			"its ramp; the first, before the second's ramp, within %.1f dB in the window, %.1f dB past it\n", Window + 8, Window,
			relative(second), relative(inWindow), relative(pastWindow));
		require(second / peak < 1e-4, "a second Hit is not its own");		// -107.8 dB measured
		require(inWindow / peak < 0.14, "a first Hit further off than a sample");		// -18.1 dB measured
		require(pastWindow / peak < 1e-3, "a second Hit past the window touched the first");
	}

	// Track B takes the lane of Track A's cut voice, all lanes held: B's chain sees its Hit
	void heirChain(const Library& _library)
	{
		const Machine machine{Family::Syntakt, 0};
		const std::array<uint8_t, 8> held{64, 0, 0, 0, 0, 0, 127, 127};
		const auto play = [&](const int _depth)
		{
			auto e = std::make_unique<Engine>(_library);
			constexpr int B = Tracks - 1;
			for(int t = 0; t < B; ++t)
			{
				e->setMachine(t, machine);
				e->setLevel(t, 127);
				e->setOutput(t, 15);
				for(int i = 0; i < 8; ++i)
					e->setParameter(t, i, held[static_cast<size_t>(i)]);
			}
			e->setMachine(B, {Family::Syntakt, 1});
			e->setLevel(B, 127);
			ChainSettings s = syntaktChain();
			s.filter.mode = 0;
			s.filter.frequency = 20;
			s.filter.depth = _depth;
			s.filter.envelope.decay = 50;
			e->setChain(B, s);
			Take take(16384);
			take.render(*e, 0, 200);
			for(int t = 0; t < B; ++t)
				e->noteOn(t, 127);
			take.render(*e, 200, 2000);

			// Track 0's voice cut and struck again fills the 16 lanes; B, with no lane, takes the one Track 0 is leaving
			e->setChokes(1, 1);
			e->noteOn(1, 127);
			e->noteOn(0, 127);
			require(e->lanesRendered(0) == 16, "the lanes all held");
			e->setMachine(B, machine);
			for(int i = 0; i < 8; ++i)
				e->setParameter(B, i, held[static_cast<size_t>(i)]);
			e->noteOn(B, 127);
			take.render(*e, 2200, 14184);
			return take;
		};
		const auto moved = play(120), still = play(64);
		double power = 0;
		for(size_t n = 2400; n < 6000; ++n)
			power += std::pow(moved.channel(0)[n] - still.channel(0)[n], 2);
		std::printf("  Track B on Track A's cut lane, its filter's envelope: %.2e of difference\n", power);
		require(power > 1e-3, "the heir's chain never saw its Hit");
	}

	// ---- what switching a silent voice off changes ----

	// The 16 voices rendered every block, the product's Tracks driving them (mdVoiceCoefficientsTest holds that to the
	// oracle), against the same Hits and random sounds: with the off ones skipped; rendered, but their scratch left as it
	// was, what comes of the shared scratch alone; with each Hit bringing its Track's ticks, as the engine does. Per
	// machine family. _chance: 1 in that many, a Track's Hit in a block; _tempo: BPM x 24
	// _bounds: the error each family stays under, in dB of its signal: what was measured with 1 dB of margin, for this seed,
	// so that a change that makes skipping a voice cost more of its sound fails. GND's error is 0 but for rounding
	void skip(const Library& _library, const uint32_t _tempo, const uint32_t _chance,
		const std::map<std::string, double>& _bounds)
	{
		constexpr int Blocks = 600000;
		constexpr int Quiet = Engine::MachinedrumQuietBlocks;
		constexpr int SilentWord = 84;		// Engine::Silence
		std::vector<uint8_t> list;
		for(int id = 1; id < 256; ++id)
			if(md::voice::coefficientsPorted(static_cast<uint8_t>(id)))
				list.push_back(static_cast<uint8_t>(id));
		VoiceSet all(_library), skipped(_library), kept(_library), aligned(_library);
		std::mt19937 random(11);
		struct Stat
		{
			int64_t hits = 0, hitsDiffering = 0;
			double signal = 0, error = 0, scratch = 0, ticks = 0;
		};
		std::map<std::string, Stat> stats;
		auto saved = std::make_unique<md::voice::Scratch>();
		const auto family = [](const uint8_t _id)
		{
			return _id < 16 ? "GND" : _id < 32 ? "TRX" : _id < 48 ? "EFM" : _id < 64 ? "E12" : _id < 128 ? "P-I" : "ROM";
		};
		std::array<bool, Tracks> differed{};
		float lost = 0;
		int longest = 0, longestMachine = 0;
		std::array<int, Tracks> gap{};
		for(int b = 0; b < Blocks; ++b)
		{
			for(int t = 0; t < Tracks; ++t)
			{
				const auto i = static_cast<size_t>(t);
				const auto event = random() % _chance;
				if(event == 0)
				{
					const auto id = list[random() % list.size()];
					md::voice::Track::Defaults sound;
					for(auto& v : sound)
						v = static_cast<uint8_t>(random() % 128);
					for(auto* set : {&all, &kept, &skipped, &aligned})
						set->tracks[i].setMachine(id, sound);
				}
				else if(event == 1)
				{
					for(auto* set : {&all, &kept, &skipped, &aligned})
						set->tracks[i].hit();
					aligned.tracks[i].alignTicks(b);
					skipped.live |= 1u << t;
					skipped.quiet[i] = 0;
					auto& stat = stats[family(skipped.tracks[i].machine())];
					++stat.hits;
					stat.hitsDiffering += differed[i];
					differed[i] = false;
					gap[i] = 0;
				}
			}
			md::voice::prepareBlock(all.tracks, all.voices, *_library.os, _tempo, b, 0xffffu, 0xffffu);
			md::voice::prepareBlock(kept.tracks, kept.voices, *_library.os, _tempo, b, 0xffffu, 0xffffu);
			md::voice::prepareBlock(aligned.tracks, aligned.voices, *_library.os, _tempo, b, 0xffffu, 0xffffu);
			md::voice::prepareBlock(skipped.tracks, skipped.voices, *_library.os, _tempo, b, 0xffffu, skipped.live);
			for(int t = 0; t < Tracks; ++t)
			{
				const auto i = static_cast<size_t>(t);
				md::voice::Voice::Block x{}, s{}, k{}, l{};
				all.voices[i].render(x);
				aligned.voices[i].render(l);
				const bool on = skipped.live >> t & 1;
				if(on)
				{
					skipped.voices[i].render(s);
					kept.voices[i].render(k);
				}
				else
				{
					*saved = kept.shared->scratch;
					kept.voices[i].render(k);
					kept.shared->scratch = *saved;
				}
				auto& stat = stats[family(skipped.tracks[i].machine())];
				int peak = 0;
				for(size_t n = 0; n < x.size(); ++n)
				{
					peak = std::max(peak, std::abs(x[n]));
					const auto square = [](const double _d) { return _d / 8388608.0 * (_d / 8388608.0); };
					stat.signal += square(x[n]);
					stat.error += square(x[n] - s[n]);
					stat.scratch += square(x[n] - k[n]);
					stat.ticks += square(x[n] - l[n]);
					differed[i] = differed[i] || x[n] != s[n];
					if(!on)
						lost = std::max(lost, std::abs(x[n]) / 8388608.0f);
				}
				if(peak >= SilentWord)
				{
					if(gap[i] > longest)
					{
						longest = gap[i];
						longestMachine = skipped.tracks[i].machine();
					}
					gap[i] = 0;
				}
				else
				{
					++gap[i];
				}
				if(on)
				{
					skipped.quiet[i] = peak < SilentWord ? skipped.quiet[i] + 1 : 0;
					if(skipped.quiet[i] >= Quiet)
						skipped.live &= ~(1u << t);
				}
			}
		}
		std::printf("  off voices at %.0f BPM, %d blocks of 16 random Tracks, a Hit each %u blocks, off after %d quiet blocks: what "
			"they never render peaks at %.1f dB, longest silence inside a Hit %d blocks (machine %d); against 16 voices "
			"rendered:\n", _tempo / 24.0, Blocks, _chance, Quiet, 20 * std::log10(std::max(lost, 1e-9f)), longest, longestMachine);
		const auto db = [](const double _a, const double _b) { return 10 * std::log10(std::max(_a, 1e-30) / std::max(_b, 1e-30)); };
		for(const auto& [name, s] : stats)
			std::printf("    %s: %lld Hits, %lld with a sample changed, error %.1f dB of the signal; %.1f dB from the shared "
				"scratch alone; %.1f dB from a Hit bringing its ticks\n", name.c_str(), static_cast<long long>(s.hits),
				static_cast<long long>(s.hitsDiffering), db(s.error, s.signal), db(s.scratch, s.signal), db(s.ticks, s.signal));
		require(lost <= 2 * Engine::Silence, "a voice went off that was to sound again");
		for(const auto& [name, s] : stats)
			require(db(s.error, s.signal) <= _bounds.at(name), name + ": what skipping a voice changes");
	}

	// The longest rest inside a Hit of machine M, in its blocks under Engine::Silence, over random sounds
	template<typename M>
	int longestRest(const syntakt::fast::TwinshotSamples* _samples, std::mt19937& _random)
	{
		constexpr int Blocks = 1500;	// a second
		int longest = 0;
		for(int round = 0; round < 60; ++round)
		{
			auto m = std::make_unique<M>();
			if constexpr(std::is_same_v<M, syntakt::fast::SpTwinshot>)
				m->setSamples(_samples);
			m->setLanes(4);
			for(int i = 0; i < 8; ++i)
				m->setParameter(0, i, static_cast<uint8_t>(_random() % 128), 0);
			m->setNote(0, static_cast<int>(_random() % 100));
			m->trig(0);
			int rest = -1;	// not until it has sounded
			for(int b = 0; b < Blocks; ++b)
			{
				const auto& block = m->renderBlock();
				float peak = 0;
				for(int n = 0; n < BlockSize; ++n)
					peak = std::max(peak, std::abs(block.s[n][0]));
				if(peak >= Engine::Silence)
				{
					longest = std::max(longest, rest);
					rest = 0;
				}
				else if(rest >= 0)
				{
					++rest;
				}
			}
		}
		return longest;
	}

	// The same for the Syntakt, which an off voice skips in its own blocks of 48 kHz: SP TWINSHOT plays a sample with a gap
	void syntaktGaps(const syntakt::fast::TwinshotSamples* _samples)
	{
		using namespace syntakt::fast;
		std::mt19937 random(2);
		const std::array<int, SyntaktMachines> rests{longestRest<BdModern>(_samples, random),
			longestRest<SdBasic>(_samples, random), longestRest<CyAlloy>(_samples, random),
			longestRest<PcCarbon>(_samples, random), longestRest<SdVintage>(_samples, random),
			longestRest<CpVintage>(_samples, random), longestRest<SyToy>(_samples, random),
			longestRest<SyBits>(_samples, random), longestRest<SpTwinshot>(_samples, random)};
		std::printf("  off voices, 60 random sounds of a second, the longest rest inside a Hit, in blocks of 48 kHz, off after %d:",
			Engine::SyntaktQuietBlocks);
		for(int m = 0; m < SyntaktMachines; ++m)
			std::printf(" %s %d,", syntaktMachineNames[static_cast<size_t>(m)], rests[static_cast<size_t>(m)]);
		std::printf("\n");
		require(*std::max_element(rests.begin(), rests.end()) < Engine::SyntaktQuietBlocks,
			"a Syntakt voice rests longer than it stays on");
	}

	// The OS's machine table as md::fw reads it, against the names mdProtocol keeps by hand: every machine, in every field
	// a wrong stride would move
	void machineTable(const std::vector<uint8_t>& _flash)
	{
		const auto table = md::fw::readMachineTable(md::fw::loadFirmwareFromFlash(_flash).osImage);
		using md::MachineModel;
		int named = 0;
		for(int id = 0; id < 256; ++id)
			named += md::machines::parameterNames(MachineModel::Machinedrum, static_cast<uint16_t>(id)) != nullptr;
		for(const auto& m : table)
		{
			const auto* names = md::machines::parameterNames(MachineModel::Machinedrum, m.id);
			require(names != nullptr, "machine " + std::to_string(m.id) + " is not mdProtocol's");
			for(size_t p = 0; p < 8; ++p)
			{
				require((*names)[p] == m.params[p], "machine " + std::to_string(m.id) + ": parameter " + std::to_string(p));
			}
		}
		std::printf("  the OS's machine table: %zu machines of mdProtocol's %d, every parameter's name the same\n",
			table.size(), named);
		// mdProtocol also names GND-SW, GND-PU and NFX-EV, -CO, -UC, which OS 1.63's table lacks
		std::array<bool, 256> inTable{};
		for(const auto& m : table)
			inTable[m.id] = true;
		for(int id = 0; id < 256; ++id)
		{
			const bool otherOs = id == 4 || id == 5 || (id >= 7 && id <= 9);
			const bool hasNames = md::machines::parameterNames(MachineModel::Machinedrum, static_cast<uint16_t>(id)) != nullptr;
			require(inTable[static_cast<size_t>(id)] == (hasNames && !otherOs), "machine " + std::to_string(id) + " in the table");
		}
	}

	// A machine that reads nothing of the image plays the same samples with and without it, at its defaults, with each
	// knob at 0, 100 and 127, the others in the middle, and with all of them at random
	// The sends: nothing at 0, a return on the output chosen and nowhere else, the same samples whatever the host's blocks
	double energy(const std::vector<float>& _x)
	{
		double sum = 0;
		for(const float v : _x)
			sum += v * v;
		return sum;
	}

	void sends(const Library& _library)
	{
		constexpr int Samples = 44100, Warm = 2000, Out = 3;
		const auto play = [&](const int _delay, const int _reverb, const int _output, const int _block,
			const SendSource _source = SendSource::Machinedrum)
		{
			auto e = std::make_unique<Engine>(_library);
			e->setMachine(0, {Family::Machinedrum, 16});
			e->setLevel(0, 127);
			e->setChain(0, ChainSettings{});
			auto& fx = e->sends();
			for(const auto& [byte, value] : {std::pair<int, uint8_t>{md::fast::EchoTime, 24}, {md::fast::EchoFb, 32},
				{md::fast::EchoFltw, 127}, {md::fast::EchoLev, 127}})
				fx.setEcho(byte, value);
			for(const auto& [byte, value] : {std::pair<int, uint8_t>{md::fast::GateDec, 64}, {md::fast::GateDamp, 64},
				{md::fast::GateLp, 127}, {md::fast::GateGate, 127}, {md::fast::GateLev, 127}})
				fx.setGateBox(byte, value);
			for(const auto& [byte, value] : {std::pair<int, uint8_t>{StDelayTime, 24}, {StDelayFdbk, 32},
				{StDelayLp, 127}, {StDelayWidth, 64}})
				fx.setSyntaktDelay(byte, value);
			for(const auto& [byte, value] : {std::pair<int, uint8_t>{StReverbDec, 64}, {StReverbFreq, 64},
				{StReverbLp, 127}})
				fx.setSyntaktReverb(byte, value);
			fx.setSource(Send::Delay, _source);
			fx.setSource(Send::Reverb, _source);
			fx.setOutput(Send::Delay, _output);
			fx.setOutput(Send::Reverb, _output);
			e->setSend(0, Send::Delay, _delay);
			e->setSend(0, Send::Reverb, _reverb);
			Take take(Samples);
			take.render(*e, 0, Warm);
			e->noteOn(0, 127);
			while(take.at() < Samples)
				take.render(*e, std::min(_block, Samples - take.at()));
			return take;
		};
		const auto dry = play(0, 0, 0, 32);
		require(energy(dry.channel(2 * Out)) == 0, "no send, an Out with nothing");
		for(const int channel : {0, 1})
			require(dry.channel(channel) == play(0, 0, Out, 32).channel(channel), "a send at 0 changes Main");
		const auto sent = play(127, 127, Out, 32);
		for(const int channel : {0, 1})
			require(dry.channel(channel) == sent.channel(channel), "a return on an Out reaches Main");
		require(energy(sent.channel(2 * Out)) > 1e-3, "no return on the Out chosen");
		require(energy(play(127, 0, Out, 32).channel(2 * Out)) > 1e-3, "no delay");
		require(energy(play(0, 127, Out, 32).channel(2 * Out)) > 1e-3, "no reverb");
		require(energy(play(127, 127, 0, 32).channel(0)) > energy(dry.channel(0)), "no return on Main");
		const auto other = play(127, 127, Out, 57);
		require(sent.channel(2 * Out) == other.channel(2 * Out), "a return moves with the host's blocks");
		const auto ours = [&](const Send _send, const int _mode, const int _block, const int _rate = 64,
			const int _frequency = 64, const int _feedback = 64)
		{
			auto e = std::make_unique<Engine>(_library);
			e->setMachine(0, {Family::Machinedrum, 16});
			e->setLevel(0, 127);
			e->setChain(0, ChainSettings{});
			auto& fx = e->sends();
			for(int byte = 0; byte < ChorusBytes; ++byte)
				fx.setChorus(byte, 64);
			fx.setChorus(ChorusLevel, 127);
			fx.setChorus(ChorusShape, 1);
			for(int byte = 0; byte < PhaserBytes; ++byte)
				fx.setPhaser(byte, 64);
			fx.setPhaser(PhaserMode, static_cast<uint8_t>(_mode));
			fx.setPhaser(PhaserLevel, 127);
			fx.setPhaser(PhaserRate, static_cast<uint8_t>(_rate));
			fx.setPhaser(PhaserFrequency, static_cast<uint8_t>(_frequency));
			fx.setPhaser(PhaserFeedback, static_cast<uint8_t>(_feedback));
			fx.setOutput(_send, Out);
			e->setSend(0, _send, 127);
			Take take(Samples);
			take.render(*e, 0, Warm);
			e->noteOn(0, 127);
			while(take.at() < Samples)
				take.render(*e, std::min(_block, Samples - take.at()));
			return take;
		};
		for(const auto& [send, mode] :
			{std::pair<Send, int>{Send::Chorus, 0}, {Send::Phaser, 0}, {Send::Phaser, 1}, {Send::Phaser, 2}})
		{
			const auto take = ours(send, mode, 32);
			require(energy(take.channel(2 * Out)) > 1e-3, "no chorus, phaser or flanger");
			std::vector<float> free;
			for(const bool synced : {false, true})
			{
				// The return's peak for the meters, and the LFO at a note of the tempo, which moves the return
				auto e = std::make_unique<Engine>(_library);
				e->setMachine(0, {Family::Machinedrum, 16});
				e->setLevel(0, 127);
				e->setChain(0, ChainSettings{});
				auto& fx = e->sends();
				fx.setChorus(ChorusLevel, 127);
				fx.setPhaser(PhaserMode, static_cast<uint8_t>(mode));
				fx.setPhaser(PhaserLevel, 127);
				fx.setPhaser(PhaserDepth, 127);
				fx.setChorus(ChorusDepth, 127);
				fx.setChorus(ChorusDelay, 64);
				fx.setPhaser(PhaserFrequency, 64);
				fx.setOutput(send, Out);
				if(synced)
				{
					fx.setLfoSync(send, true);
					fx.setLfoNote(send, 0.25);
				}
				e->setSend(0, send, 127);
				Take t(Samples);
				t.render(*e, 0, Warm);
				e->noteOn(0, 127);
				t.render(*e, 0, Samples - Warm);
				require(fx.peak(send) > 0.01f && fx.peak(Send::Delay) == 0, "a return's peak is not its own");
				if(synced)
					require(t.channel(2 * Out) != free, "the LFO's note changes nothing");
				free = t.channel(2 * Out);
			}
			require(take.channel(2 * Out) == ours(send, mode, 57).channel(2 * Out), "ours move with the host's blocks");
			for(const float x : take.channel(2 * Out))
				require(std::isfinite(x) && std::abs(x) < 4, "ours run away");
		}
		// The sweep at its fastest, a low frequency, all the feedback: a direct form's state ran away here
		for(const int rate : {124, 127})
			for(const int feedback : {0, 127})
				for(const int mode : {0, 2})
					for(const float x : ours(Send::Phaser, mode, 32, rate, 0, feedback).channel(2 * Out))
						require(std::isfinite(x) && std::abs(x) < 64, "the phaser runs away");
		const auto syntakt = play(127, 127, Out, 32, SendSource::Syntakt);
		for(const int channel : {0, 1})
			require(dry.channel(channel) == syntakt.channel(channel), "the Syntakt's return on an Out reaches Main");
		require(energy(play(127, 0, Out, 32, SendSource::Syntakt).channel(2 * Out)) > 1e-3, "no Syntakt delay");
		require(energy(play(0, 127, Out, 32, SendSource::Syntakt).channel(2 * Out)) > 1e-3, "no Syntakt reverb");
		require(syntakt.channel(2 * Out) == play(127, 127, Out, 57, SendSource::Syntakt).channel(2 * Out),
			"the Syntakt's return moves with the host's blocks");
		std::printf("PASS: the sends, none at 0, the returns where they are sent, the same whatever the blocks\n");
	}

	// ---- the master on Main ----

	void master(const Library& _library)
	{
		constexpr int Samples = 44100;
		const auto play = [&](const bool _on, const std::vector<std::pair<int, uint8_t>>& _bytes, const int _block)
		{
			auto e = std::make_unique<Engine>(_library);
			e->setMachine(0, {Family::Machinedrum, 16});
			e->setLevel(0, 90);
			e->setChain(0, ChainSettings{});
			e->setMachine(1, {Family::Machinedrum, 16});
			e->setOutput(1, 1);
			e->setMasterOn(_on);
			for(const auto& [byte, value] : _bytes)
				e->master().set(byte, value);
			Take take(Samples);
			e->noteOn(0, 127);
			e->noteOn(1, 127);
			while(take.at() < Samples)
				take.render(*e, std::min(_block, Samples - take.at()));
			return take;
		};
		const auto off = play(false, {}, 32);
		const auto on = play(true, {}, 32);
		float peak = 0, apart = 0;
		for(const int c : {0, 1})
			for(size_t i = 0; i < static_cast<size_t>(Samples); ++i)
			{
				peak = std::max(peak, std::abs(off.channel(c)[i]));
				apart = std::max(apart, std::abs(on.channel(c)[i] - off.channel(c)[i]));
			}
		std::printf("  master: Main peak %.3f, apart %.2e\n", peak, apart);
		require(peak > 0.1f && peak < 1, "the Hit fills Main under full scale");
		require(apart < 1e-6f, "the master at its defaults changes Main");
		const std::vector<std::pair<int, uint8_t>> squeezed = {{MasterThreshold, 40}, {MasterRatio, 127},
			{MasterDrive, 64}, {MasterMakeup, 20}};
		const auto pressed = play(true, squeezed, 32);
		require(energy(pressed.channel(0)) < 0.5 * energy(off.channel(0)), "a low threshold does not compress");
		require(pressed.channel(2) == off.channel(2), "the master reaches an Out");
		require(pressed.channel(0) == play(true, squeezed, 57).channel(0), "the master moves with the host's blocks");
		for(const float x : pressed.channel(0))
			require(std::abs(x) <= 1, "Main past full scale");
		{
			// The reduction the editor shows, while the Hit is loud and once it has gone
			auto e = std::make_unique<Engine>(_library);
			e->setMachine(0, {Family::Machinedrum, 16});
			e->setLevel(0, 90);
			e->setMasterOn(true);
			for(const auto& [byte, value] : squeezed)
				e->master().set(byte, value);
			Take take(Samples);
			e->noteOn(0, 127);
			take.render(*e, 0, 2048);
			const float loud = e->master().reduction();
			take.render(*e, 0, Samples - 2048);
			const float after = e->master().reduction();
			std::printf("  gain reduction: %.1f dB in the Hit, %.2f dB a second on\n", loud, after);
			require(loud < -3 && after > loud / 4, "the gain reduction does not follow the Hit");
		}
		std::printf("PASS: the master, nothing at its defaults, Main only, the same whatever the blocks\n");
	}

	// ---- what the sends and the master cost: one Track hit every quarter second, in blocks of 128, the best of three
	// runs, against the same Track with neither ----

	void effectsCost(const Library& _library)
	{
		constexpr int Seconds = 4, Blocks = Seconds * 44100 / 128;
		const auto run = [&](const std::function<void(Engine&)>& _set)
		{
			double best = 1e9;
			for(int r = 0; r < 3; ++r)
			{
				auto e = std::make_unique<Engine>(_library);
				e->setMachine(0, {Family::Machinedrum, 16});
				e->setLevel(0, 100);
				_set(*e);
				Take take(128);
				const auto t0 = Clock::now();
				for(int b = 0; b < Blocks; ++b)
				{
					if(b % 86 == 0)
						e->noteOn(0, 100);
					take.render(*e, 0, 128);
				}
				best = std::min(best, std::chrono::duration<double>(Clock::now() - t0).count());
			}
			return 100 * best / Seconds;
		};
		const auto allSends = [](Engine& _e, const SendSource _source, const int _phaserMode)
		{
			auto& fx = _e.sends();
			fx.setSource(Send::Delay, _source);
			fx.setSource(Send::Reverb, _source);
			fx.setEcho(md::fast::EchoLev, 127);
			fx.setGateBox(md::fast::GateLev, 127);
			fx.setChorus(ChorusLevel, 127);
			fx.setPhaser(PhaserStages, 6);
			fx.setPhaser(PhaserLevel, 127);
			fx.setPhaser(PhaserMode, static_cast<uint8_t>(_phaserMode));
			for(int s = 0; s < SendCount; ++s)
				_e.setSend(0, static_cast<Send>(s), 127);
		};
		const double bare = run([](Engine&) {});
		const double md = run([&](Engine& _e) { allSends(_e, SendSource::Machinedrum, 0); });
		const double st = run([&](Engine& _e) { allSends(_e, SendSource::Syntakt, 0); });
		const double dp4 = run([&](Engine& _e) { allSends(_e, SendSource::Syntakt, 2); });
		const double master = run([](Engine& _e)
		{
			_e.setMasterOn(true);
			_e.master().set(MasterThreshold, 40);
			_e.master().set(MasterDrive, 64);
		});
		std::printf("  cost at 44.1 kHz, %% of a core: a Track alone %.3f; the four sends on top, Echo and Gate Box %.3f, "
			"the Syntakt's %.3f, the Syntakt's with the DP/4 phaser %.3f; the master on top %.3f\n", bare, md - bare,
			st - bare, dp4 - bare, master - bare);
		require(md - bare < 3 && st - bare < 3 && dp4 - bare < 3 && master - bare < 1, "the sends or the master cost too much");
		std::printf("PASS: the cost of the sends and the master\n");
	}

	// ---- the sends on their own: a block of Sends here, with its input before it, is 32 samples of the engine's ----

	constexpr int Frames = Sends::BlockSize;
	constexpr double SendRate = Sends::Rate;
	constexpr int TimeFrames = 22739;	// TIME 32 at 120 BPM at 44.1 kHz, of the Syntakt's delay

	// Both effects of both machines audible, with no feedback
	std::unique_ptr<Sends> sendsOf(const Library& _library)
	{
		auto fx = std::make_unique<Sends>(*_library.os, _library.trx.sine.data());
		fx->setTempo(120 * 24);
		fx->setEcho(md::fast::EchoTime, 32);
		fx->setEcho(md::fast::EchoFltw, 127);
		fx->setEcho(md::fast::EchoLev, 127);
		for(const auto& [byte, value] : {std::pair<int, uint8_t>{md::fast::GateDec, 64}, {md::fast::GateDamp, 64},
			{md::fast::GateLp, 127}, {md::fast::GateGate, 127}, {md::fast::GateLev, 127}})
			fx->setGateBox(byte, value);
		fx->setSyntaktDelay(StDelayTime, 32);
		fx->setSyntaktDelay(StDelayWidth, 64);
		fx->setSyntaktDelay(StDelayLp, 127);
		fx->setSyntaktReverb(StReverbDec, 64);
		fx->setSyntaktReverb(StReverbFreq, 64);
		fx->setSyntaktReverb(StReverbLp, 127);
		return fx;
	}

	void idle(Sends& _fx, const int _blocks, const bool _fed)
	{
		for(int b = 0; b < _blocks; ++b)
		{
			if(_fed)
				_fx.input(Send::Delay, 0, 0);
			_fx.block();
		}
	}

	// Samples from a click sent to the delay now to the first of its return, within _most
	int firstEcho(Sends& _fx, const int _most)
	{
		constexpr int At = 16;		// past where a send's gains rise from nothing in a delay's first block
		_fx.input(Send::Delay, 0, At)[0] = 0.25f;
		for(int b = 0; b * Frames < _most; ++b)
		{
			_fx.input(Send::Delay, 0, 0);
			_fx.block();
			const float* const left = _fx.output(Send::Delay, 0, 0);
			const float* const right = _fx.output(Send::Delay, 1, 0);	// where the Echo's MONO at 0 gives it
			for(int i = 0; i < Frames; ++i)
				if(std::max(std::abs(left[i]), std::abs(right[i])) > 0.02f)
					return b * Frames + i - At;
		}
		return -1;
	}

	void requireNear(const int _measured, const int _target, const int _within, const std::string& _what)
	{
		std::printf("  %s: first echo after %d samples, target %d\n", _what.c_str(), _measured, _target);
		require(std::abs(_measured - _target) <= _within, _what + ": the first echo is not at its time");
	}

	// The Syntakt's delay, which does not run while Sends sleeps or the Echo plays, starts at its time; the tempo held
	// to the OS's range; a free time on both delays
	void sendTimes(const Library& _library)
	{
		auto fx = sendsOf(_library);
		fx->setSource(Send::Delay, SendSource::Syntakt);
		idle(*fx, 2, false);
		requireNear(firstEcho(*fx, 2 * TimeFrames), TimeFrames, 3, "after boot");

		idle(*fx, 15000, false);
		require(!fx->sounding(), "the sends do not sleep");
		fx->setSyntaktDelay(StDelayTime, 16);
		idle(*fx, 100, false);
		requireNear(firstEcho(*fx, 2 * TimeFrames), 11714, 3, "after 10 s of silence, TIME moved");
		fx->setSyntaktDelay(StDelayTime, 32);
		idle(*fx, 15000, false);

		fx->setTempo(0);
		idle(*fx, 15000, false);
		requireNear(firstEcho(*fx, 200000), 90956, 3, "a tempo of 0, held to 30 BPM");
		idle(*fx, 15000, false);
		fx->setTempo(1000000);
		requireNear(firstEcho(*fx, 200000), 9095, 3, "a tempo too high, held to 300 BPM");
		idle(*fx, 15000, false);

		fx->setDelayFree(4);
		fx->setTempo(60 * 24);
		requireNear(firstEcho(*fx, 40000), 11025, 3, "the Syntakt's, 4 Hz, whatever the tempo");
		idle(*fx, 15000, false);
		fx->setDelayFree(1378.125);
		requireNear(firstEcho(*fx, 40000), 32, 3, "the Syntakt's, 1378 Hz, a block");
		idle(*fx, 15000, false);
		fx->setDelayFree(0.125);
		requireNear(firstEcho(*fx, 400000), 352800, 3, "the Syntakt's, 0.125 Hz");
		idle(*fx, 15000, false);
		fx->setDelayFree(0);
		fx->setTempo(120 * 24);
		requireNear(firstEcho(*fx, 2 * TimeFrames), TimeFrames, 3, "TIME again");

		// A note at the host's tempo, over the free time and TIME's; a triplet in whole samples, the tempo followed
		idle(*fx, 15000, false);
		fx->setDelayFree(4);
		fx->setBpm(120);
		fx->setDelaySync(true);
		fx->setDelayNote(1);
		requireNear(firstEcho(*fx, 40000), 22050, 3, "the Syntakt's, a quarter at 120 BPM");
		idle(*fx, 15000, false);
		fx->setBpm(100);
		fx->setDelayNote(1.0 / 3);
		requireNear(firstEcho(*fx, 40000), 8820, 3, "the Syntakt's, an eighth triplet at 100 BPM");
		idle(*fx, 15000, false);
		fx->setBpm(137.5);
		requireNear(firstEcho(*fx, 40000), 6415, 3, "the Syntakt's, an eighth triplet at 137.5 BPM");
		idle(*fx, 15000, false);
		fx->setDelaySync(false);
		fx->setDelayFree(0);
		requireNear(firstEcho(*fx, 2 * TimeFrames), TimeFrames, 3, "TIME after the note");

		const auto echoAt = [&](const double _hz, const double _seconds)
		{
			auto echo = sendsOf(_library);
			echo->setDelayFree(_hz);
			echo->setTempo(60 * 24);
			idle(*echo, static_cast<int>(_seconds * SendRate / Frames), true);
			return firstEcho(*echo, 800000);
		};
		requireNear(echoAt(4, 8), 11025, 3, "the Echo's, 4 Hz, whatever the tempo");
		requireNear(echoAt(100, 8), 441, 3, "the Echo's, 100 Hz");
		requireNear(echoAt(0.5, 12), 88200, 3, "the Echo's, 0.5 Hz");
		{
			auto echo = sendsOf(_library);
			echo->setBpm(90);
			echo->setDelaySync(true);
			echo->setDelayNote(0.75);
			idle(*echo, static_cast<int>(8 * SendRate / Frames), true);
			requireNear(firstEcho(*echo, 800000), 22050, 3, "the Echo's, a dotted eighth at 90 BPM");
		}
		for(const double hz : {1000.0, 0.4536, 0.4535, 0.25, 0.125})
		{
			const int at = echoAt(hz, 12);
			std::printf("  the Echo asked for %.4f Hz: first echo after %d samples, %.4f Hz\n", hz, at, SendRate / at);
		}
		std::printf("PASS: the delays' first echoes at their times, after boot, a sleep, on a free time\n");
	}

	// A change of source fades the return out and in, and the effect left keeps nothing
	void sendSwitch(const Library& _library)
	{
		constexpr float Level = 0.2f;
		constexpr double Hz = 110;
		const auto other = [](const SendSource _s)
		{
			return _s == SendSource::Syntakt ? SendSource::Machinedrum : SendSource::Syntakt;
		};
		const auto steps = [&](const Send _send, const SendSource _from)
		{
			auto fx = sendsOf(_library);
			fx->setSource(_send, _from);
			double phase = 0;
			float last = 0, peak = 0;
			const auto run = [&](const int _blocks)
			{
				float step = 0;
				for(int b = 0; b < _blocks; ++b)
				{
					float* const left = fx->input(_send, 0, 0);
					float* const right = fx->input(_send, 1, 0);
					for(int i = 0; i < Frames; ++i)
					{
						left[i] = right[i] = Level * static_cast<float>(std::sin(phase));
						phase += 2 * Pi * Hz / SendRate;
					}
					fx->block();
					const float* const out = fx->output(_send, 0, 0);
					for(int i = 0; i < Frames; ++i)
					{
						step = std::max(step, std::abs(out[i] - last));
						last = out[i];
						peak = std::max(peak, std::abs(out[i]));
					}
				}
				return step;
			};
			run(static_cast<int>(2 * SendRate / Frames));
			peak = 0;
			const float steady = run(1000);
			fx->setSource(_send, other(_from));
			const float through = run(3);
			const float bound = steady + 1.5f * peak / Frames;
			std::printf("  %s from %s: return %.4f, its biggest step %.5f, through the change %.5f\n",
				_send == Send::Delay ? "the delay" : "the reverb",
				_from == SendSource::Syntakt ? "the Syntakt" : "the MD", static_cast<double>(peak),
				static_cast<double>(steady), static_cast<double>(through));
			require(peak > 0.05f, "no return to change");
			require(through <= bound, "a change of source steps the return");
		};
		const auto tail = [&](const Send _send, const SendSource _from)
		{
			auto fx = sendsOf(_library);
			fx->setEcho(md::fast::EchoFb, 100);
			fx->setGateBox(md::fast::GateDec, 120);
			fx->setSyntaktDelay(StDelayFdbk, 50);
			fx->setSyntaktReverb(StReverbDec, 120);
			fx->setSource(_send, _from);
			idle(*fx, 100, false);
			for(int b = 0; b < 100; ++b)
			{
				fx->input(_send, 0, 0)[b % Frames] = 0.5f;
				fx->block();
			}
			fx->setSource(_send, other(_from));
			idle(*fx, 4, false);
			fx->setSource(_send, _from);
			idle(*fx, 4, false);
			float peak = 0;
			for(int b = 0; b < static_cast<int>(3 * SendRate / Frames); ++b)
			{
				fx->block();
				for(int i = 0; i < Frames; ++i)
					peak = std::max(peak, std::abs(fx->output(_send, 0, 0)[i]));
			}
			require(peak == 0, "the tail of an effect came back with it");
		};
		for(const Send send : {Send::Delay, Send::Reverb})
			for(const SendSource from : {SendSource::Machinedrum, SendSource::Syntakt})
			{
				steps(send, from);
				tail(send, from);
			}

		// What the change costs the audio thread: both sends at once, the cache full of something else
		std::vector<uint8_t> elsewhere(64 << 20);
		double worstBlock = 0;
		auto fx = sendsOf(_library);
		idle(*fx, 100, true);
		for(int run = 0; run < 16; ++run)
		{
			const auto to = run % 2 ? SendSource::Machinedrum : SendSource::Syntakt;
			idle(*fx, 4, true);
			for(auto& byte : elsewhere)
				++byte;
			fx->setSource(Send::Delay, to);
			fx->setSource(Send::Reverb, to);
			const auto t0 = Clock::now();
			fx->block();
			worstBlock = std::max(worstBlock, std::chrono::duration<double, std::micro>(Clock::now() - t0).count());
		}
		std::printf("  the block of a change of both sends: worst %.0f us of %.0f\n", worstBlock,
			1e6 * Frames / SendRate);
		std::printf("PASS: a change of source without a step, and no tail of the effect left\n");
	}

	// The Syntakt's bytes slew as the OS's step says: the effects as the words give them, against those words by hand.
	// HPF's jump no longer clicks; PRE's still moves a read by whole samples at each block, printed and not bound
	void sendSlew(const Library& _library, const bool _pre)
	{
		using syntakt::fast::Delay;
		using syntakt::fast::DelayWords;
		using syntakt::fast::Reverb;
		using syntakt::fast::ReverbWords;
		constexpr int Word = 127 << 8, Middle = 64 << 8;
		auto fx = sendsOf(_library);
		fx->setSource(Send::Delay, SendSource::Syntakt);
		fx->setSource(Send::Reverb, SendSource::Syntakt);
		DelayWords delayWords{32 << 8, 0, Middle, 0, 0, Word};
		ReverbWords reverbWords{0, Middle, Middle, 0, 0, Word};
		auto delay = std::make_unique<Delay>(SendRate);
		auto reverb = std::make_unique<Reverb>(SendRate);
		auto jumpedDelay = std::make_unique<Delay>(SendRate);
		auto jumped = std::make_unique<Reverb>(SendRate);
		const auto toRaw = [&]
		{
			delay->setWords(delayWords);
			reverb->setWords(reverbWords);
			delay->setTempo(120);
		};
		toRaw();
		jumpedDelay->setWords(delayWords);
		jumpedDelay->setTempo(120);
		jumped->setWords(reverbWords);
		delay->settle();
		jumpedDelay->settle();
		std::array<float, Frames> in{}, returned{}, right{}, l{}, r{}, jumpedOut{}, jumpedRight{}, jumpedL{}, jumpedR{};
		const std::array<float, Frames> none{};
		fx->block();
		for(int b = 1; b < 100; ++b)
		{
			fx->block();
			delay->process(in.data(), in.data(), returned.data(), right.data());
			reverb->process(in.data(), in.data(), returned.data(), right.data(), l.data(), r.data());
			jumpedDelay->process(in.data(), in.data(), jumpedOut.data(), jumpedRight.data());
			jumped->process(in.data(), in.data(), none.data(), none.data(), jumpedL.data(), jumpedR.data());
		}

		// The delay's and the reverb's last sample, slewed and jumped, and their biggest steps
		std::array<std::array<float, 2>, 2> before{}, steady{}, step{};
		double phase = 0;
		const auto slew = [](int& _word, const int _target) { _word = _target + 3 * (_word - _target) / 4; };
		const int preTo = _pre ? 100 << 8 : 0, highPassTo = _pre ? 0 : 80 << 8, delayHighPassTo = _pre ? 0 : 100 << 8;
		for(int b = 0; b < 2000; ++b)
		{
			if(b == 1400)
			{
				fx->setSyntaktReverb(StReverbPre, _pre ? 100 : 0);
				fx->setSyntaktReverb(StReverbHp, _pre ? 0 : 80);
				fx->setSyntaktDelay(StDelayHp, _pre ? 0 : 100);
				ReverbWords now = reverbWords;
				now.preDelay = preTo;
				now.highPass = highPassTo;
				jumped->setWords(now);
				DelayWords nowDelay = delayWords;
				nowDelay.highPass = delayHighPassTo;
				jumpedDelay->setWords(nowDelay);
			}
			if(b >= 1400)
			{
				slew(delayWords.highPass, delayHighPassTo);
				reverbWords.preDelay = preTo;
				slew(reverbWords.highPass, highPassTo);
				toRaw();
			}
			float* const toDelayL = fx->input(Send::Delay, 0, 0);
			float* const toDelayR = fx->input(Send::Delay, 1, 0);
			float* const toReverbL = fx->input(Send::Reverb, 0, 0);
			float* const toReverbR = fx->input(Send::Reverb, 1, 0);
			for(int i = 0; i < Frames; ++i)
			{
				const auto x = 0.2f * static_cast<float>(std::sin(phase));
				phase += 2 * Pi * 110 / SendRate;
				in[static_cast<size_t>(i)] = toDelayL[i] = toDelayR[i] = toReverbL[i] = toReverbR[i] = x;
			}
			fx->block();
			delay->process(in.data(), in.data(), returned.data(), right.data());
			reverb->process(in.data(), in.data(), returned.data(), right.data(), l.data(), r.data());
			jumpedDelay->process(in.data(), in.data(), jumpedOut.data(), jumpedRight.data());
			jumped->process(in.data(), in.data(), none.data(), none.data(), jumpedL.data(), jumpedR.data());
			const float* const outDelay = fx->output(Send::Delay, 0, 0);
			const float* const outReverb = fx->output(Send::Reverb, 0, 0);
			for(int i = 0; i < Frames; ++i)
			{
				const auto n = static_cast<size_t>(i);
				require(outDelay[i] == returned[n], "the Syntakt's delay does not slew by the step");
				require(outReverb[i] == l[n], "the Syntakt's reverb does not slew by the step");
				const float now[2][2] = {{returned[n], jumpedOut[n]}, {l[n], jumpedL[n]}};
				for(size_t e = 0; e < 2; ++e)
					for(size_t k = 0; k < 2; ++k)
					{
						auto& biggest = b < 1400 ? steady[e][k] : step[e][k];
						if(b >= 1000 && b < 1500)
							biggest = std::max(biggest, std::abs(now[e][k] - before[e][k]));
						before[e][k] = now[e][k];
					}
			}
		}
		std::printf("  a jump of %s, the reverb's biggest step: %.5f playing, %.5f slewed, %.5f jumped\n",
			_pre ? "PRE" : "HPF", double(steady[1][0]), double(step[1][0]), double(step[1][1]));
		if(_pre)
			return;
		std::printf("  a jump of HPF, the delay's biggest step: %.5f playing, %.5f slewed, %.5f jumped\n",
			double(steady[0][0]), double(step[0][0]), double(step[0][1]));
		require(step[0][0] < 1.5f * steady[0][0], "a jump of the delay's HPF clicks");
		std::printf("PASS: the Syntakt's bytes slew by the OS's step\n");
	}

	void withoutTheImage(const std::vector<uint8_t>& _flash, const Library& _library)
	{
		const Library none;
		std::mt19937 random(1);
		int same = 0;
		const Machine rom01{Family::Machinedrum, 128}, trxCy{Family::Machinedrum, 24}, rom33{Family::Machinedrum, 176};
		require(dataOf(_library, rom01) == MachineData::Whole && dataOf(_library, trxCy) == MachineData::Whole,
			"the image gives its machines their data");
		require(dataOf(_library, rom33) == MachineData::None, "ROM-33: empty in the factory bank");
		for(const Machine machine : machines())
		{
			if(dataOf(none, machine) != MachineData::Whole)
				continue;
			const auto check = [&](const std::array<uint8_t, 8>& _knobs)
			{
				const auto play = [&](const Library& _from)
				{
					return hitAt(_from, machine, 0, 8000, &Bare, Engine::DefaultOversampling, &_knobs);
				};
				if(play(_library).channel(0) != play(none).channel(0))
				{
					std::string values;
					for(const uint8_t k : _knobs)
						values += " " + std::to_string(k);
					throw std::runtime_error(nameOf(machine) + ": another sound with the image, at" + values);
				}
			};
			check(defaults(machine));
			for(size_t k = 0; k < 8; ++k)
				for(const uint8_t value : {0, 100, 127})
				{
					auto knobs = defaults(machine);
					knobs[k] = value;
					check(knobs);
				}
			for(int n = 0; n < 40; ++n)
			{
				std::array<uint8_t, 8> knobs{};
				for(auto& k : knobs)
					k = static_cast<uint8_t>(random() % 128);
				check(knobs);
			}
			++same;
		}
		std::printf("  %d machines that read nothing of the image: the same samples with and without it, each knob at "
			"0, 100 and 127, 40 at random\n", same);

		// P-I-RC's sine steps out of its table into the UW bank from PTCH 95 up, and what it reads there reaches no
		// output
		Library loud(_flash);
		std::fill(loud.samples.begin(), loud.samples.end(), 0x7fffffu);
		const std::array<uint8_t, 8> high{127, 64, 64, 64, 64, 64, 64, 64};
		const Machine rc{Family::Machinedrum, 70};
		const auto rcFrom = [&](const Library& _from)
		{
			return hitAt(_from, rc, 0, 8000, &Bare, Engine::DefaultOversampling, &high).channel(0);
		};
		require(rcFrom(loud) == rcFrom(none), "P-I-RC hears the UW bank");
	}

	// ---- the Sampler (ticket 20) ----

	Sample sine(const double _hz, const double _rate, const int _length, const char* _name)
	{
		// A last sample of 0: the player holds the last one, and a sample that ends off 0 fades from there in some 300
		std::vector<float> x(static_cast<size_t>(_length) + 1, 0.0f);
		for(int n = 0; n < _length; ++n)
			x[static_cast<size_t>(n)] = static_cast<float>(0.5 * std::sin(2 * Pi * _hz * n / _rate));
		auto made = makeSample(_name, x, _rate);
		require(std::holds_alternative<Sample>(made), "makeSample refuses a sine");
		return std::get<Sample>(std::move(made));
	}

	// Its first and last samples above 1 % of its peak, and its frequency from its rising zero crossings in between
	struct Measure
	{
		int onset = -1, end = -1;
		double hz = 0;
	};

	Measure measure(const std::vector<float>& _y)
	{
		Measure m;
		const float peak = peakOf(_y);
		for(int n = 0; n < static_cast<int>(_y.size()); ++n)
			if(std::abs(_y[static_cast<size_t>(n)]) >= 0.01f * peak)
			{
				m.end = n;
				if(m.onset < 0)
					m.onset = n;
			}
		double first = -1, last = -1;
		int crossings = 0;
		for(int n = m.onset + 200; n < m.end - 200; ++n)
		{
			const float a = _y[static_cast<size_t>(n - 1)], b = _y[static_cast<size_t>(n)];
			if(a < 0 && b >= 0)
			{
				last = n - 1 + a / (a - b);
				if(first < 0)
					first = last;
				++crossings;
			}
		}
		m.hz = crossings > 1 ? (crossings - 1) * SampleRate / (last - first) : 0;
		return m;
	}

	// Track 0 on the Sampler, its sample _sample, hit at _note, Main and Out 01 rendered
	Take samplerHit(const Library& _library, const Sample& _sample, const int _note, const int _samples)
	{
		auto e = engineWith(_library, Sampler);
		e->setChain(0, Bare);
		e->setSample(0, &_sample);
		Take take(_samples);
		take.render(*e, 0, _note);
		e->noteOn(0, 127);
		take.render(*e, _note, _samples - _note);
		return take;
	}

	void sampler(const Library& _library)
	{
		const Library none;
		constexpr int Note = 1000, Length = 22050;
		const auto a = sine(1000, 44100, Length, "sine 1000");
		const auto lowRate = sine(1000, 22050, Length, "sine 1000 at 22 kHz");

		// The state's copy gives the same words back; bytes that are not one give none
		const auto bytes = sampleToBytes(a);
		const auto back = sampleFromBytes(bytes.data(), bytes.size());
		require(back && back->words == a.words && back->name == a.name && back->rate == a.rate
			&& back->rateWord == a.rateWord, "a Sample through its bytes differs");
		require(!sampleFromBytes(bytes.data(), bytes.size() - 1), "cut bytes make a Sample");
		require(std::holds_alternative<std::string>(makeSample("x", {0.5f}, 96000)), "96 kHz taken");

		for(const Library* library : {&_library, &none})
		{
			const char* which = library == &none ? "without the image" : "with the image";
			const auto take = samplerHit(*library, a, Note, Note + Engine::HitDelay + Length + 4000);
			const auto m = measure(take.channel(0));
			std::printf("  Sampler %s: onset %d after its note, %d samples long, %.2f Hz for 1000\n", which,
				m.onset - Note, m.end - m.onset + 1, m.hz);
			require(m.onset - Note >= Engine::HitDelay && m.onset - Note <= Engine::HitDelay + 3,
				"the Sampler's Hit is not at HitDelay");
			require(std::abs(m.end - m.onset + 1 - Length) <= 32, "the Sampler plays another length");
			require(std::abs(m.hz - 1000) < 1, "the Sampler plays another pitch");
			if(library == &none)
				require(take.channel(0) == samplerHit(_library, a, Note, take.samples()).channel(0),
					"the Sampler sounds otherwise without the image");

			const auto slow = measure(samplerHit(*library, lowRate, Note, Note + 200 + 2 * Length + 4000).channel(0));
			std::printf("  ... a sample at 22.05 kHz: %d samples long, %.2f Hz for 1000\n", slow.end - slow.onset + 1, slow.hz);
			require(std::abs(slow.end - slow.onset + 1 - 2 * Length) <= 64 && std::abs(slow.hz - 1000) < 1,
				"a sample at its own rate plays otherwise");
		}

		// Two Tracks, each its own sample on its own output; then Track 0's changed while Track 1 sounds, and while it
		// sounds itself: Track 1's samples do not move, Track 0's ramp, nothing allocated
		const auto b = sine(1500, 44100, Length, "sine 1500");
		const auto two = [&](const bool _change, size_t* _allocated)
		{
			auto e = engineWith(_library, Sampler);
			e->setMachine(1, Sampler);
			e->setLevel(1, 127);
			e->setOutput(1, 1);
			for(int t = 0; t < 2; ++t)
				e->setChain(t, Bare);
			e->setSample(0, &a);
			e->setSample(1, &b);
			Take take(Note + Length + 4000);
			take.render(*e, 0, Note);
			e->noteOn(0, 127);
			e->noteOn(1, 127);
			const auto played = countAllocations([&]
			{
				take.render(*e, Note, 6000);
				if(_change)
					e->setSample(0, &lowRate);
				take.render(*e, Note + 6000, 2000);
				if(_change)
					e->setSample(0, &b);
				take.render(*e, take.samples() - take.at());
			});
			if(_allocated)
				*_allocated = played;
			return take;
		};
		const auto kept = two(false, nullptr);
		const auto m0 = measure(kept.channel(0)), m1 = measure(kept.channel(2));
		std::printf("  two Tracks: %.2f and %.2f Hz for 1000 and 1500\n", m0.hz, m1.hz);
		require(std::abs(m0.hz - 1000) < 1 && std::abs(m1.hz - 1500) < 1.5, "two Tracks do not play their own samples");
		size_t allocated = 0;
		const auto changed = two(true, &allocated);
		require(changed.channel(2) == kept.channel(2), "Track 1 moves as Track 0's sample changes");
		const auto& y = changed.channel(0);
		float step = 0, stepBefore = 0;
		for(size_t n = 1; n < y.size(); ++n)
		{
			const float d = std::abs(y[n] - y[n - 1]);
			step = std::max(step, d);
			if(n < Note + 6000)
				stepBefore = std::max(stepBefore, d);
		}
		const int changedAt = Note + 6000 + Engine::HitDelay;
		require(peakOf(std::vector<float>(y.begin() + changedAt + Engine::CutSamples, y.begin() + changedAt + 1900)) == 0,
			"Track 0 sounds on after its sample changed");
		std::printf("  a sample changed under its voice: largest step %.4f against %.4f before, %zu allocations\n", step,
			stepBefore, allocated);
		require(step <= stepBefore * 1.05f, "a sample changed under its voice clicks");
		require(allocated == 0, "changing a sample allocates");
	}

	std::unique_ptr<syntakt::fast::TwinshotSamples> noise()
	{
		auto s = std::make_unique<syntakt::fast::TwinshotSamples>();
		constexpr uint32_t Length = 24000;
		s->base = 0x100000;
		s->data.resize(8 * Length);
		std::mt19937 rng(1);
		for(auto& x : s->data)
			x = static_cast<int16_t>(static_cast<int32_t>(rng() % 40000) - 20000);
		// A gap in each, 63 ms played at its rate
		for(uint32_t i = 0; i < 8; ++i)
			std::fill_n(s->data.begin() + i * Length + Length / 2, 3000, static_cast<int16_t>(0));
		for(uint32_t i = 0; i < 64; ++i)
			s->slots[i] = {Length, s->base + 2 * (i % 8) * Length};
		return s;
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto flash = Library::findFlashImage({});
	if(flash.empty())
	{
		std::printf("SKIP: set GEARMULATOR_MD_FIRMWARE_BIN to the Machinedrum UW flash image\n");
		return 77;
	}
	try
	{
		Library library(flash);
		const auto samples = noise();
		library.twinshot = samples.get();
		sampler(library);
		withoutTheImage(flash, library);
		machineTable(flash);
		familyChains(flash, library);
		sends(library);
		master(library);
		effectsCost(library);
		sendTimes(library);
		sendSwitch(library);
		sendSlew(library, false);
		sendSlew(library, true);
		chokedNeighbour(library);
		chainWhileOff(library);
		midHit(library);
		earlySamples(library);
		staleGateOff(library);
		tailGain(library);
		twoHitsInABlock(library);
		hitInPhaseWindow(library);
		heirChain(library);
		bareVoice(library);
		chainsAtHits(library);
		velocityAndAccent(library);
		hits(library);
		running(library);
		againstHost(library);
		blocks(library);
		routing(library);
		lanes(library);
		off(library);
		allocations(library);
		q12(library);
		fullMachine(library);
		changeAndHit(library);
		syntaktGaps(samples.get());
		skip(library, 3000, 6000, {{"E12", -128}, {"EFM", -99}, {"GND", -300}, {"P-I", -21}, {"ROM", -48}, {"TRX", -16.5}});
		skip(library, 720, 24000, {{"E12", -118}, {"EFM", -17}, {"GND", -300}, {"P-I", -4.8}, {"ROM", -34}, {"TRX", -23}});
		std::printf("PASS: 16 Tracks of both families, each Hit at its note's sample\n");
		return 0;
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
}
