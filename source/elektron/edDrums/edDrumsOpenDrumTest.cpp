// The OPEN DRUM family (ticket 26 of .scratch/elektron-drums-v1) against the original engine:
//
//   - each voice alone in its blocks of 64, against the renders of the original firmware's code (OPENDRUM_REFERENCES,
//     recorded out of the repository by opendrumRefs): init kit, Machine 1-8 at two settings, another note, other
//     parameters and a velocity, sample for sample;
//   - each machine as a Track plays it in the engine's 32-sample blocks: the same samples, a Hit's first sample
//     Engine::HitDelay after its note wherever the note falls, its voice no longer rendered once quiet;
//   - what a voice costs per 32-sample block, nothing allocated on the audio path.
//
// Exit 77 without OPENDRUM_REFERENCES.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "edDrumsEngine.h"
#include "edDrumsTestSupport.h"
#include "mdDrumsAllocationCounter.h"
#include "openDrum.h"

namespace
{
	using namespace elektron::drums;
	using namespace elektron::drums::test;
	using elektron::BlockSize;
	using Clock = std::chrono::steady_clock;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	struct Scenario
	{
		int kind = 0;			// 0-3 as OpenDrum's, 4 the hihat open
		int note = 60, velocity = 127;
		std::vector<std::pair<uint16_t, uint8_t>> parameters;
		std::vector<int16_t> samples;
	};

	const char* const KindNames[5]{"OD DRUM", "OD SNARE", "OD CYMBAL", "OD HIHAT", "OD HIHAT open"};

	std::vector<Scenario> readReferences(const char* _path)
	{
		std::ifstream in(_path, std::ios::binary);
		require(in.good(), std::string("cannot read ") + _path);
		const auto read = [&](void* _to, const size_t _bytes)
		{
			in.read(static_cast<char*>(_to), static_cast<std::streamsize>(_bytes));
			require(in.good(), "a truncated reference file");
		};
		char magic[4];
		read(magic, 4);
		require(std::string(magic, 4) == "ODR1", "not a reference file");
		uint32_t count = 0;
		read(&count, 4);
		std::vector<Scenario> scenarios(count);
		for(auto& s : scenarios)
		{
			uint8_t head[4];
			read(head, 4);
			s.kind = head[0];
			s.note = head[1];
			s.velocity = head[2];
			for(int i = 0; i < head[3]; ++i)
			{
				uint16_t p = 0;
				uint8_t v = 0;
				read(&p, 2);
				read(&v, 1);
				s.parameters.emplace_back(p, v);
			}
			uint32_t n = 0;
			read(&n, 4);
			s.samples.resize(n);
			read(s.samples.data(), 2 * static_cast<size_t>(n));
		}
		return scenarios;
	}

	struct VoiceMemory
	{
		VoiceMemory() : bytes(openDrum_size() + alignof(std::max_align_t)) {}
		OpenDrumVoice* voice() { return reinterpret_cast<OpenDrumVoice*>(bytes.data()); }
		std::vector<std::max_align_t> bytes;
	};

	// The port alone, as the reference was recorded
	std::vector<int16_t> renderAlone(const Scenario& _s)
	{
		VoiceMemory memory;
		auto* v = memory.voice();
		openDrum_init(v, std::min(_s.kind, 3));
		for(const auto& [p, value] : _s.parameters)
			openDrum_setParameter(v, p, value);
		openDrum_settle(v);
		openDrum_trigger(v, static_cast<uint8_t>(_s.velocity), static_cast<uint8_t>(_s.note), _s.kind == 4);
		std::vector<int16_t> out(_s.samples.size());
		for(size_t at = 0; at + OpenDrumBlock <= out.size(); at += OpenDrumBlock)
			openDrum_render(v, out.data() + at);
		return out;
	}

	void againstReferences(const std::vector<Scenario>& _scenarios)
	{
		int exact = 0;
		for(size_t i = 0; i < _scenarios.size(); ++i)
		{
			const auto& s = _scenarios[i];
			const auto port = renderAlone(s);
			int worst = 0, peak = 0;
			size_t first = port.size();
			for(size_t n = 0; n < port.size(); ++n)
			{
				const int d = std::abs(port[n] - s.samples[n]);
				peak = std::max(peak, std::abs(static_cast<int>(s.samples[n])));
				if(d && first == port.size())
					first = n;
				worst = std::max(worst, d);
			}
			std::printf("  %-14s note %d velocity %3d, %zu parameters: peak %5d, %s\n", KindNames[s.kind], s.note,
				s.velocity, s.parameters.size(), peak, worst ? ("largest difference " + std::to_string(worst)
				+ " from sample " + std::to_string(first)).c_str() : "the same samples");
			exact += worst == 0;
		}
		require(exact == static_cast<int>(_scenarios.size()), "the port differs from the original");
		std::printf("PASS: %d renders of the original, sample for sample, at 64-sample blocks\n", exact);
	}

	const ChainSettings Bare{};		// every slot off

	Machine machineOf(const int _kind)
	{
		return {Family::OpenDrum, static_cast<uint8_t>(std::min(_kind, 3))};
	}

	// Machine 1-8 for a scenario's parameters, false when one is not among them
	bool knobsOf(const Scenario& _s, std::array<uint8_t, MachineParameters>& _knobs)
	{
		_knobs = allDefaults(machineOf(_s.kind));
		const int kind = std::min(_s.kind, 3);
		for(const auto& [p, value] : _s.parameters)
		{
			int index = 0;
			while(index < MachineParameters && openDrum_machineParameter(kind, index) != p)
				++index;
			if(index == MachineParameters)
				return false;
			_knobs[static_cast<size_t>(index)] = value;
		}
		if(_s.kind == 4)
			_knobs[7] = 127;
		return true;
	}

	// Track 0 on the scenario's machine, a Hit at _note of _velocity, Main left
	std::vector<float> throughEngine(const Library& _library, const Scenario& _s, const std::array<uint8_t, MachineParameters>& _knobs,
		const int _note, const int _velocity)
	{
		Engine e(_library);
		e.setMachine(0, machineOf(_s.kind));
		e.setLevel(0, 127);
		e.setChain(0, Bare);
		for(int i = 0; i < MachineParameters; ++i)
			e.setParameter(0, i, _knobs[static_cast<size_t>(i)]);
		Take take(_note + Engine::HitDelay + static_cast<int>(_s.samples.size()));
		take.render(e, 0, _note);
		e.noteOn(0, _velocity);
		take.render(e, _note, take.samples() - _note);
		return take.channel(0);
	}

	void throughEngine(const Library& _library, const std::vector<Scenario>& _scenarios)
	{
		int compared = 0;
		for(const auto& s : _scenarios)
		{
			std::array<uint8_t, MachineParameters> knobs{};
			if(s.note != 60 || s.velocity != 127 || !knobsOf(s, knobs))
				continue;
			for(const int note : {0, 1, 31, 32, 45, 1000})
				for(const int velocity : {127, 64})
				{
					const auto out = throughEngine(_library, s, knobs, note, velocity);
					const float gain = static_cast<float>(velocity) / 127.0f;
					float worst = 0;
					int first = -1;
					int64_t worstAt = 0;
					for(size_t n = 0; n < out.size(); ++n)
					{
						const auto at = static_cast<int64_t>(n) - note - Engine::HitDelay;
						const float want = at >= 0 && at < static_cast<int64_t>(s.samples.size())
							? static_cast<float>(s.samples[static_cast<size_t>(at)]) * (1.0f / 32768.0f) * gain : 0.0f;
						if(std::abs(out[n] - want) > worst)
						{
							worst = std::abs(out[n] - want);
							worstAt = at;
						}
						if(first < 0 && out[n] != 0)
							first = static_cast<int>(n) - note;
					}
					require(worst == 0, std::string(KindNames[s.kind]) + " through the engine differs by "
						+ std::to_string(worst) + " at its sample " + std::to_string(worstAt) + ", note at "
						+ std::to_string(note));
					require(first >= Engine::HitDelay, "a Hit before its delay");
					if(note == 0 && velocity == 127)
						std::printf("  %-14s %zu parameters: the original's samples, its first %d samples after its note"
							" (HitDelay %d)\n", KindNames[s.kind], s.parameters.size(), first, Engine::HitDelay);
					++compared;
				}
		}
		std::printf("PASS: %d Hits through the engine's blocks of 32, notes anywhere in a block, velocity 127 and 64:"
			" the original's samples from HitDelay on, jitter 0\n", compared);
	}

	// A Hit of Track 0 on _machine, its Machine 1-24 at _base, Machine _index at _value, set or locked
	std::vector<float> hitWith(const Library& _library, const Machine& _machine, const int _index, const int _value,
		const bool _lock, const int _base = -1)
	{
		Engine e(_library);
		e.setMachine(0, _machine);
		e.setLevel(0, 127);
		for(int i = 0; _base >= 0 && i < machineParameters(_machine); ++i)
			e.setParameter(0, i, static_cast<uint8_t>(_base));
		if(_index >= 0 && !_lock)
			e.setParameter(0, _index, static_cast<uint8_t>(_value));
		Take take(44100);
		take.render(e, 0, 64);
		if(_index >= 0 && _lock)
			e.lock(0, _index, static_cast<uint8_t>(_value));
		e.noteOn(0, 127);
		take.render(e, 64, take.samples() - 64);
		return take.channel(0);
	}

	// Machine 1-24: each of an OPEN DRUM voice's changes its Hit at 0 or 127, every other knob in the middle or at the init kit,
	// set and locked alike; the Machinedrum's 9-24 change nothing
	void pages(const Library& _library)
	{
		for(int kind = 0; kind < 4; ++kind)
		{
			const auto machine = machineOf(kind);
			const int count = machineParameters(machine);
			const auto middle = hitWith(_library, machine, -1, 0, false, 64), kit = hitWith(_library, machine, -1, 0, false);
			std::string names;
			int heard = 0;
			for(int i = 0; i < MachineParameters; ++i)
			{
				const char* name = machineParameterName(machine, i);
				require((name != nullptr) == (i < count), "a name past the voice's parameters, or none before");
				if(!name)
					continue;
				names += std::string(i ? ", " : "") + std::to_string(i + 1) + " " + name;
				bool set = false;
				for(const int base : {64, -1})
					for(const int value : {0, 127})
					{
						const auto& plain = base < 0 ? kit : middle;
						const bool changed = hitWith(_library, machine, i, value, false, base) != plain;
						require(changed == (hitWith(_library, machine, i, value, true, base) != plain),
							std::string(KindNames[kind]) + " " + name + ": set and locked differ");
						set = set || changed;
					}
				heard += set;
				if(!set)
					std::printf("  %-14s %s: no change from the middle or the init kit\n", KindNames[kind], name);
			}
			std::printf("  %-14s %d parameters, %d heard: %s\n", KindNames[kind], count, heard, names.c_str());
			// PWM shapes the PWM wave alone, which neither setting plays
			require(heard >= count - 1, std::string(KindNames[kind]) + ": parameters that change nothing");
		}
		const Machine trxBd{Family::Machinedrum, 16};
		require(machineParameters(trxBd) == 8 && machineParameters(Sampler) == 8
			&& machineParameters({Family::Syntakt, 0}) == 8 && !machineParameterName(trxBd, 0), "8 for the others");
		const auto plain = hitWith(_library, trxBd, -1, 0, false);
		for(const int i : {8, 16, 23})
			require(hitWith(_library, trxBd, i, 0, false) == plain && hitWith(_library, trxBd, i, 0, true) == plain,
				"Machine " + std::to_string(i + 1) + " changes TRX-BD");
		std::printf("PASS: OPEN DRUM's voices on Machine 1-24, set and locked alike; TRX-BD's 9-24 change nothing\n");
	}

	// A voice stops costing once quiet: liveTracks lets it go, and the engine idles again
	void quiet(const Library& _library)
	{
		for(int kind = 0; kind < 5; ++kind)
		{
			Engine e(_library);
			e.setMachine(0, machineOf(kind));
			if(kind == 4)
				e.setParameter(0, 7, 127);
			Take take(BlockSize);
			e.noteOn(0, 127);
			int blocks = 0;
			while(e.liveTracks() & 1)
			{
				take.render(e, 0, BlockSize);
				require(++blocks < 20 * 44100 / BlockSize, "a voice that never stops");
			}
			std::printf("  %-14s rendered %d blocks, %.0f ms, then let go\n", KindNames[kind], blocks,
				blocks * BlockSize * 1000.0 / SampleRate);
		}
		std::printf("PASS: every voice let go once quiet for %d blocks\n", Engine::OpenDrumQuietBlocks);
	}

	void allocations(const Library& _library)
	{
		Engine e(_library);
		std::vector<float> l(BlockSize), r(BlockSize);
		std::array<float*, Channels> outs{};
		outs[0] = l.data();
		outs[1] = r.data();
		const auto allocated = countAllocations([&]
		{
			for(int kind = 0; kind < 4; ++kind)
				for(int t = 0; t < Tracks; ++t)
				{
					e.setMachine(t, machineOf(kind));
					e.setParameter(t, 1, 100);
					e.lock(t, 0, 40);
					e.noteOn(t, 100);
					for(int b = 0; b < 8; ++b)
						e.render(outs.data(), BlockSize);
				}
		});
		require(allocated == 0, std::to_string(allocated) + " allocations on the audio path");
		std::printf("PASS: machine changes, parameters, locks, Hits and blocks allocate nothing\n");
	}

	double microseconds(const Clock::duration _d)
	{
		return std::chrono::duration<double, std::micro>(_d).count();
	}

	// µs per 32 samples: a voice alone, retriggered every 96 blocks of 64; then 16 Tracks through the engine
	void cost(const Library& _library)
	{
		constexpr int Renders = 20000;
		for(int kind = 0; kind < 4; ++kind)
		{
			VoiceMemory memory;
			openDrum_init(memory.voice(), kind);
			int16_t block[OpenDrumBlock];
			const auto start = Clock::now();
			for(int i = 0; i < Renders; ++i)
			{
				if(i % 96 == 0)
					openDrum_trigger(memory.voice(), 127, 60, 0);
				openDrum_render(memory.voice(), block);
			}
			std::printf("  %-14s alone: %.3f us per 32 samples\n", KindNames[kind],
				microseconds(Clock::now() - start) / Renders / 2);
		}
		std::vector<float> l(BlockSize), r(BlockSize);
		std::array<float*, Channels> outs{};
		outs[0] = l.data();
		outs[1] = r.data();
		const auto run = [&](Engine& _e, const bool _hits)
		{
			constexpr int Blocks = 20000;
			const auto start = Clock::now();
			for(int b = 0; b < Blocks; ++b)
			{
				if(_hits && b % 192 == 0)
					for(int t = 0; t < Tracks; ++t)
						_e.noteOn(t, 127);
				_e.render(outs.data(), BlockSize);
			}
			return microseconds(Clock::now() - start) / Blocks;
		};
		Engine idle(_library);
		for(int t = 0; t < Tracks; ++t)
			idle.setMachine(t, machineOf(t % 4));
		const double rest = run(idle, false);
		Engine busy(_library);
		for(int t = 0; t < Tracks; ++t)
			busy.setMachine(t, machineOf(t % 4));
		const double sounding = run(busy, true);
		std::printf("  16 Tracks, 4 of each, through their chains: %.2f us per 32-sample block sounding, %.2f quiet"
			" (budget %.0f)\n", sounding, rest, BlockSize * 1e6 / SampleRate);
		std::printf("PASS: cost measured\n");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		const char* path = std::getenv("OPENDRUM_REFERENCES");
		if(!path || !*path)
		{
			std::printf("SKIP: OPENDRUM_REFERENCES not set\n");
			return 77;
		}
		const auto scenarios = readReferences(path);
		againstReferences(scenarios);
		const Library library;
		throughEngine(library, scenarios);
		pages(library);
		quiet(library);
		allocations(library);
		cost(library);
		return 0;
	}
	catch(const std::exception& _e)
	{
		std::printf("FAIL: %s\n", _e.what());
		return 1;
	}
}
