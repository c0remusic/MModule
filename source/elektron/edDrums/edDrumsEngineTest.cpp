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
//   - the OS's machine table against mdProtocol's names.
//
// GEARMULATOR_MD_FIRMWARE_BIN names the flash image; exit 77 without it. SP TWINSHOT plays noise made here.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "Firmware.h"
#include "edDrumsEngine.h"
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
	using elektron::BlockSize;
	using syntakt::fast::bench::fft;
	using syntakt::fast::bench::Pi;
	using Clock = std::chrono::steady_clock;

	constexpr int Channels = 2 * Outputs;
	constexpr int HitSamples = 1500;	// a Hit compared from its onset

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	std::string nameOf(const Machine& _m)
	{
		return (_m.family == Family::Syntakt ? syntaktMachineNames[_m.id] : "MD machine " + std::to_string(_m.id));
	}

	// Every machine that can sound, both families
	std::vector<Machine> machines()
	{
		std::vector<Machine> m;
		for(int id = 0; id < SyntaktMachines; ++id)
			m.push_back({Family::Syntakt, static_cast<uint8_t>(id)});
		for(int id = 1; id < 256; ++id)
			if(md::voice::coefficientsPorted(static_cast<uint8_t>(id)))
				m.push_back({Family::Machinedrum, static_cast<uint8_t>(id)});
		return m;
	}

	class Take
	{
	public:
		explicit Take(const int _samples) : m_samples(_samples), m_buffers(Channels)
		{
			for(int c = 0; c < Channels; ++c)
				m_buffers[static_cast<size_t>(c)].assign(static_cast<size_t>(_samples), 0.0f);
		}

		// Renders _count samples at _at
		void render(Engine& _e, const int _at, const int _count)
		{
			std::array<float*, Channels> p{};
			for(int c = 0; c < Channels; ++c)
				p[static_cast<size_t>(c)] = m_buffers[static_cast<size_t>(c)].data() + _at;
			_e.render(p.data(), _count);
		}

		const std::vector<float>& channel(const int _c) const { return m_buffers[static_cast<size_t>(_c)]; }
		int samples() const { return m_samples; }

	private:
		int m_samples;
		std::vector<std::vector<float>> m_buffers;
	};

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

	// ... hit at _note
	Take hitAt(const Library& _library, const Machine& _machine, const int _note, const int _samples)
	{
		auto e = engineWith(_library, _machine);
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
			// ROM-33 to -48 are empty in the factory bank
			if(machine.family == Family::Machinedrum && machine.id >= 176 && machine.id < 192)
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
		for(const Machine machine : {Machine{Family::Syntakt, 0}, Machine{Family::Machinedrum, 3}})
		{
			auto e = engineWith(_library, machine);
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
			std::printf("  %s on a running engine, %d Hits over the 48 kHz cycle: half its first peak %.3f to %.3f samples "
				"after the note (jitter %.3f samples, %.5f ms)\n", nameOf(machine).c_str(), Cycle, lo, hi, hi - lo,
				(hi - lo) / 44.1);
			require(hi - lo < 0.02, "a Hit moved on a running engine");
		}
	}

	// The Track's read against the family's resampler it replaces (Q11's, stFastResampleTest): BD MODERN's Hit through
	// fast::Host, whose phase falls with its block, one of the 147 blocks of the cycle giving the engine's samples
	void againstHost(const Library& _library)
	{
		const Machine machine{Family::Syntakt, 0};
		const auto reference = hitAt(_library, machine, 0, 4000);
		const auto& y = reference.channel(0);
		const auto sound = defaults(_library, machine);
		constexpr int Compared = 2000, First = Engine::HitDelay - 24;
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

	// research/19's HF: energy above 2 kHz over all of it, in 4096 samples around _cut under a Blackman-Harris window
	double hf(const std::vector<float>& _y, const int _cut)
	{
		constexpr int N = 4096;
		std::vector<std::complex<double>> a(N);
		for(int i = 0; i < N; ++i)
		{
			const double w = 0.35875 - 0.48829 * std::cos(2 * Pi * i / (N - 1)) + 0.14128 * std::cos(4 * Pi * i / (N - 1))
				- 0.01168 * std::cos(6 * Pi * i / (N - 1));
			a[static_cast<size_t>(i)] = static_cast<double>(_y[static_cast<size_t>(_cut - N / 2 + i)]) * w;
		}
		fft(a);
		double total = 0, high = 0;
		for(int i = 1; i < N / 2; ++i)
		{
			const double p = std::norm(a[static_cast<size_t>(i)]);
			total += p;
			if(i * SampleRate / N > 2000)
				high += p;
		}
		return 10 * std::log10(high / total);
	}

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
			{"MD sine", {Family::Machinedrum, 1}, {35, 127, 0, 0, 0, 0, 0, 0}, 0, -25.5, -23.3},
			{"Syntakt held", {Family::Syntakt, 0}, {64, 0, 0, 0, 0, 0, 127, 127}, 79, -39.0, -40.4},
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
			const double base = hf(u, crest);
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
							require(e->parameter(0, i) == _library.mdDefaults[cut.to.id][static_cast<size_t>(i)],
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
				const double v = hf(y, cutAt);
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
				const double v = hf(take.channel(0), crest);
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
				const double v = hf(take.channel(0), crest + offset);
				lo = std::min(lo, v);
				hi = std::max(hi, v);
			}
			std::printf("    the voice struck again at the crest, %s's own retrigger: HF %.1f to %.1f dB\n", tone.name, lo, hi);
			require(hi <= tone.retriggerBound, std::string(tone.name) + ": a retrigger's HF " + std::to_string(hi));
		}
	}

	// Every lane of a machine held, a Track choked and struck again at once: its Hit, the quietest, takes the lane the cut
	// voice is still fading on
	void fullMachine(const Library& _library)
	{
		constexpr int Start = 200, Around = 12000, Samples = 16384;
		const Tone tone{"Syntakt held", {Family::Syntakt, 0}, {64, 0, 0, 0, 0, 0, 127, 127}, 79, 0, 0};
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
		const double v = hf(take.channel(0), crest);
		std::printf("  all 16 lanes of a machine held, a Track choked and struck at once: HF %.1f dB\n", v);
		// Measured, 1 dB of margin: the machine's own fade of the block that sees the trig ends the ramp, 32 samples, not
		// Q12's 64. Without it, the lane freed at once, -26 dB
		require(v <= -42, "a lane taken from a cut voice clicks");
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
				// One Track reads a ring at one phase, a Hit's: under a Syntakt Hit the Syntakt tail moves by a fraction of a
				// sample. The rest adds up exactly
				const int from = from_.family == Family::Syntakt && to.family == Family::Syntakt
					? At + Engine::HitDelay + Engine::CutSamples + 60 : 0;
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
				require(worst / peak < 1e-3, "a Hit after a machine change is not a fresh one");
			}
	}

	// ---- what switching a silent voice off changes ----

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
	// a wrong stride would move. Defaults have no such second source here: within range, and 0 on a parameter with no name
	void machineTable(const std::vector<uint8_t>& _flash, const Library& _library)
	{
		const auto table = md::fw::readMachineTable(md::fw::loadFirmwareFromFlash(_flash).osImage);
		using md::MachineModel;
		int named = 0;
		for(int id = 0; id < 256; ++id)
			named += md::machines::parameterNames(MachineModel::Machinedrum, static_cast<uint16_t>(id)) != nullptr;
		int unusedNonZero = 0;
		for(const auto& m : table)
		{
			const auto* names = md::machines::parameterNames(MachineModel::Machinedrum, m.id);
			require(names != nullptr, "machine " + std::to_string(m.id) + " is not mdProtocol's");
			for(size_t p = 0; p < 8; ++p)
			{
				require((*names)[p] == m.params[p], "machine " + std::to_string(m.id) + ": parameter " + std::to_string(p));
				require(m.defaults[p] <= 127, "machine " + std::to_string(m.id) + ": a default out of range");
				require(_library.mdDefaults[m.id][p] == m.defaults[p], "the Library's defaults are not the table's");
				unusedNonZero += m.params[p].empty() && m.defaults[p] != 0;
			}
		}
		std::printf("  the OS's machine table: %zu machines of mdProtocol's %d, every parameter's name the same; %d defaults on "
			"an unnamed parameter not 0\n", table.size(), named, unusedNonZero);
		require(unusedNonZero == 0, "a default where no parameter is: its bytes are not the defaults");
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
		machineTable(flash, library);
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
