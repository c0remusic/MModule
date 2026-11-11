// The SOPHIE family (source/elektron/sophie as an instrument machine):
//
//   - its 16 factory sounds, Machine 1-20 to the nearest value of each law, against the factory pads;
//   - each sound as a Track plays it in the engine's 32-sample blocks: the voice alone's samples (sophie_render at the
//     same parameters, times Engine::SophieGain and the velocity), from Engine::HitDelay after its note wherever the note
//     falls, jitter 0;
//   - BODY: off, the kick plays Fuse's FM instead of its body; Machine 1-20 set and locked alike;
//   - its kick's peak beside the other families' kicks, the voice let go once quiet, nothing allocated, its cost.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "edDrumsEngine.h"
#include "edDrumsTestSupport.h"
#include "mdDrumsAllocationCounter.h"
#include "sophieMachine.h"
#include "stFastStages.h"

namespace
{
	using namespace elektron::drums;
	using namespace elektron::drums::test;
	using elektron::BlockSize;
	using Clock = std::chrono::steady_clock;

	constexpr Machine SophieMachine{Family::Sophie, 0};
	using Values = std::array<uint8_t, sophie::MachineSlots>;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	const ChainSettings Bare{};		// every slot off

	// The voice alone on pad 1 at _values, _samples of a Hit at velocity 127, as the engine scales them
	std::vector<float> alone(const Values& _values, const size_t _samples, const float _gain)
	{
		// As the engine renders: denormals flushed
		const syntakt::fast::FlushDenormals ftz;
		sophie_t s;
		sophie_init(&s, static_cast<int>(SampleRate), 0);
		for(int i = 0; i < sophie::MachineSlots; ++i)
			sophie::setMachineValue(s, i, _values[static_cast<size_t>(i)]);
		sophie_trigger(&s, 127);
		std::vector<float> out((_samples + BlockSize - 1) / BlockSize * BlockSize);
		for(size_t at = 0; at < out.size(); at += BlockSize)
			sophie_render(&s, out.data() + at, BlockSize);
		out.resize(_samples);
		for(auto& x : out)
			x = x * Engine::SophieGain * _gain;
		return out;
	}

	// Track 0 on _machine, Machine 1-24 at _values (where given), a Hit at _note of _velocity, Main left
	std::vector<float> throughEngine(const Library& _library, const Machine& _machine, const Values* _values,
		const int _note, const int _velocity, const int _samples, const ChainSettings* _chain = &Bare)
	{
		Engine e(_library);
		e.setMachine(0, _machine);
		e.setLevel(0, 127);
		if(_chain)
			e.setChain(0, *_chain);
		for(int i = 0; _values && i < sophie::MachineSlots; ++i)
			e.setParameter(0, i, (*_values)[static_cast<size_t>(i)]);
		Take take(_note + _samples);
		take.render(e, 0, _note);
		e.noteOn(0, _velocity);
		take.render(e, _note, take.samples() - _note);
		return take.channel(0);
	}

	float peakOf(const std::vector<float>& _x)
	{
		float p = 0;
		for(const float v : _x)
			p = std::max(p, std::abs(v));
		return p;
	}

	void sounds()
	{
		const auto defaults = allDefaults(SophieMachine);
		const auto kick = sophie::soundValues(0);
		for(int i = 0; i < MachineParameters; ++i)
			require(defaults[static_cast<size_t>(i)] == (i < sophie::MachineSlots ? kick[static_cast<size_t>(i)] : 0),
				"Machine " + std::to_string(i + 1) + ": the defaults are not the Kick sound");
		require(machineParameters(SophieMachine) == 20 && std::string(machineParameterName(SophieMachine, 19)) == "BODY"
			&& !machineParameterName(SophieMachine, 20), "20 named parameters");

		// Each sound's values against its factory pad, Tune counting the pad's base note
		for(int pad = 0; pad < sophie::Sounds; ++pad)
		{
			const auto v = sophie::soundValues(pad);
			sophie_t mapped, factory;
			sophie_init(&mapped, 44100, 0);
			sophie_init(&factory, 44100, pad);
			for(int i = 0; i < sophie::MachineSlots; ++i)
				sophie::setMachineValue(mapped, i, v[static_cast<size_t>(i)]);
			float worst = 0;
			for(int i = 0; i < sophie::MachineParamCount; ++i)
			{
				const auto param = static_cast<sophie_param_t>(i);
				const float want = sophie_get_param(&factory, param) + (i == SOPHIE_TUNE ? static_cast<float>(pad) : 0.0f);
				const int x = v[static_cast<size_t>(i)];
				const float step = std::abs(sophie::machineValue(i, static_cast<uint8_t>(std::min(x + 1, 127)))
					- sophie::machineValue(i, static_cast<uint8_t>(std::max(x - 1, 0))));
				const float d = std::abs(sophie_get_param(&mapped, param) - want);
				require(d <= step, std::string(sophie::SoundNames[static_cast<size_t>(pad)]) + ": parameter "
					+ std::to_string(i + 1) + " off its factory value");
				worst = std::max(worst, step > 0 ? d / step : 0.0f);
			}
			require(mapped.body == (pad == 0), "BODY on for the kick alone");
			std::printf("  %-12s Model %d, Tune %+3.0f st (pad %2d), Decay %.3f s: each value within %.2f of a step of its"
				" factory value\n", sophie::SoundNames[static_cast<size_t>(pad)], mapped.patch.model,
				static_cast<double>(mapped.patch.tune), pad + 1, static_cast<double>(mapped.patch.decay),
				static_cast<double>(worst));
		}
		std::printf("PASS: the 16 factory pads as sounds of pad 1's voice, the Kick the machine's defaults\n");
	}

	void againstAlone(const Library& _library)
	{
		int compared = 0;
		constexpr int Length = 44100;
		for(int pad = 0; pad < sophie::Sounds; ++pad)
		{
			const auto values = sophie::soundValues(pad);
			int start = -1;
			for(const int note : {0, 1, 31, 32, 45, 1000})
				for(const int velocity : {127, 64})
				{
					const float gain = static_cast<float>(velocity) / 127.0f;
					const auto want = alone(values, Length, gain);
					const auto out = throughEngine(_library, SophieMachine, &values, note, velocity,
						Engine::HitDelay - 1 + Length);
					float worst = 0;
					int64_t worstAt = 0;
					int first = -1;
					for(size_t n = 0; n < out.size(); ++n)
					{
						// The voice's first sample, 0 out of its low pass, one before HitDelay
						const auto at = static_cast<int64_t>(n) - note - Engine::HitDelay + 1;
						const float w = at >= 0 && at < Length ? want[static_cast<size_t>(at)] : 0.0f;
						if(std::abs(out[n] - w) > worst)
						{
							worst = std::abs(out[n] - w);
							worstAt = at;
						}
						if(first < 0 && out[n] != 0)
							first = static_cast<int>(n) - note;
					}
					require(worst == 0, std::string(sophie::SoundNames[static_cast<size_t>(pad)])
						+ " through the engine differs by " + std::to_string(worst) + " at its sample "
						+ std::to_string(worstAt) + ", note at " + std::to_string(note));
					require(first == Engine::HitDelay, "a Hit's first sample is not HitDelay after its note");
					require(start < 0 || first == start, "a Hit's start moves with its note: jitter");
					start = first;
					++compared;
				}
			const auto hit = alone(values, Length, 1.0f);
			size_t last = 0;
			const float peak = peakOf(hit);
			for(size_t n = 0; n < hit.size(); ++n)
				if(std::abs(hit[n]) >= peak * 0.01f)
					last = n;
			std::printf("  %-12s first sound %d samples after its note (HitDelay %d), peak %.3f, -40 dB at %.0f ms\n",
				sophie::SoundNames[static_cast<size_t>(pad)], start, Engine::HitDelay, static_cast<double>(peak),
				static_cast<double>(last) * 1000.0 / SampleRate);
		}
		std::printf("PASS: %d Hits through the engine's blocks of 32, notes anywhere in a block, velocity 127 and 64:"
			" the voice alone's samples from HitDelay on, jitter 0\n", compared);
	}

	void body(const Library& _library)
	{
		auto on = sophie::soundValues(0), off = on;
		off[sophie::Body] = 0;
		constexpr int Length = 22050;
		const auto withBody = throughEngine(_library, SophieMachine, &on, 0, 127, Engine::HitDelay + Length);
		const auto without = throughEngine(_library, SophieMachine, &off, 0, 127, Engine::HitDelay + Length);
		const auto want = alone(off, Length, 1.0f);
		for(int n = 0; n < Length; ++n)
			require(without[static_cast<size_t>(n + Engine::HitDelay - 1)] == want[static_cast<size_t>(n)],
				"BODY off is not the voice without its body");
		// The body is a sine with the FM a quiet layer, off Fuse's FM: what changes, and above 1 kHz (a one-pole high pass)
		const auto energy = [](const std::vector<float>& _x, const bool _high)
		{
			const float a = std::exp(-2.0f * 3.14159265f * 1000.0f / static_cast<float>(SampleRate));
			double e = 0;
			float last = 0, y = 0;
			for(const float v : _x)
			{
				y = _high ? a * (y + v - last) : v;
				last = v;
				e += static_cast<double>(y) * y;
			}
			return e;
		};
		std::vector<float> change(withBody.size());
		for(size_t n = 0; n < change.size(); ++n)
			change[n] = withBody[n] - without[n];
		const double changed = 10 * std::log10(energy(change, false) / energy(without, false));
		const double high = 10 * std::log10(energy(without, true) / energy(withBody, true));
		std::printf("  Kick: peak %.3f with its body, %.3f without; the change %.1f dB of the hit, %.1f dB more above"
			" 1 kHz without it\n", static_cast<double>(peakOf(withBody)), static_cast<double>(peakOf(without)), changed,
			high);
		require(changed > -10, "BODY off leaves the kick's body");

		// A Track on Rim (Shard) with BODY on plays the body: the user's choice, any Track
		auto rim = sophie::soundValues(1);
		rim[0] = 0;		// Fuse: the body plays for models 0-3
		auto rimBody = rim;
		rimBody[sophie::Body] = 127;
		require(throughEngine(_library, SophieMachine, &rim, 0, 127, 4096)
			!= throughEngine(_library, SophieMachine, &rimBody, 0, 127, 4096), "BODY does nothing on another sound");
		std::printf("PASS: BODY off takes the kick's body away, on gives it to any sound of models 0-3\n");
	}

	// A Hit of Track 0 on SOPHIE, Machine _index at _value, set or locked
	std::vector<float> hitWith(const Library& _library, const int _index, const int _value, const bool _lock)
	{
		Engine e(_library);
		e.setMachine(0, SophieMachine);
		e.setLevel(0, 127);
		if(!_lock)
			e.setParameter(0, _index, static_cast<uint8_t>(_value));
		Take take(22050);
		take.render(e, 0, 64);
		if(_lock)
			e.lock(0, _index, static_cast<uint8_t>(_value));
		e.noteOn(0, 127);
		take.render(e, 64, take.samples() - 64);
		return take.channel(0);
	}

	void pages(const Library& _library)
	{
		const auto plain = hitWith(_library, 0, allDefaults(SophieMachine)[0], false);
		std::string quiet;
		int heard = 0;
		for(int i = 0; i < sophie::MachineSlots; ++i)
		{
			bool changed = false;
			for(const int value : {0, 127})
			{
				const auto set = hitWith(_library, i, value, false);
				require(set == hitWith(_library, i, value, true), std::string(sophie::MachineNames[static_cast<size_t>(i)])
					+ ": set and locked differ");
				changed = changed || set != plain;
			}
			heard += changed;
			if(!changed)
				quiet += std::string(quiet.empty() ? "" : ", ") + sophie::MachineNames[static_cast<size_t>(i)];
		}
		// Ring Time, Feedback and Tone with the Kick's Ring Mix at 0, Tail Mode with nothing edited as the Hit rings
		std::printf("  %d of 20 change the Kick's Hit at 0 or 127; not: %s\n", heard, quiet.c_str());
		require(heard >= 16, "Machine parameters that change nothing");

		// A lock holds for its Hit alone
		Engine e(_library);
		e.setMachine(0, SophieMachine);
		e.setLevel(0, 127);
		Take take(3 * 8192);
		e.lock(0, 1, 100);
		e.noteOn(0, 127);
		take.render(e, 0, 8192);
		e.noteOn(0, 127);
		take.render(e, 8192, 2 * 8192);
		const auto& out = take.channel(0);
		const std::vector<float> first(out.begin(), out.begin() + 8192), second(out.begin() + 8192, out.begin() + 16384);
		require(first != second, "a lock outlives its Hit, or the next Hit plays it");
		std::printf("PASS: Machine 1-20 set and locked alike, a lock for its Hit alone\n");
	}

	void peaks(const Library& _library)
	{
		const auto sophieKick = peakOf(throughEngine(_library, SophieMachine, nullptr, 0, 127, 22050));
		const auto odDrum = peakOf(throughEngine(_library, {Family::OpenDrum, 0}, nullptr, 0, 127, 22050));
		const auto bdModern = peakOf(throughEngine(_library, {Family::Syntakt, 0}, nullptr, 0, 127, 22050));
		const double others = std::sqrt(static_cast<double>(odDrum) * bdModern);
		const double db = 20 * std::log10(sophieKick / others);
		std::printf("  peaks at level 127, velocity 127, no chain: SOPHIE Kick %.3f, OD DRUM %.3f, BD MODERN %.3f;"
			" SOPHIE %+.1f dB from the others' mean (gain %.2f)\n", static_cast<double>(sophieKick),
			static_cast<double>(odDrum), static_cast<double>(bdModern), db, static_cast<double>(Engine::SophieGain));
		require(std::abs(db) < 3, "SOPHIE's kick peaks more than 3 dB away from the other families'");
		std::printf("PASS: SOPHIE's kick peaks as the others do\n");
	}

	void quiet(const Library& _library)
	{
		for(int pad = 0; pad < sophie::Sounds; ++pad)
		{
			const auto values = sophie::soundValues(pad);
			Engine e(_library);
			e.setMachine(0, SophieMachine);
			for(int i = 0; i < sophie::MachineSlots; ++i)
				e.setParameter(0, i, values[static_cast<size_t>(i)]);
			Take take(BlockSize);
			e.noteOn(0, 127);
			int blocks = 0;
			while(e.liveTracks() & 1)
			{
				take.render(e, 0, BlockSize);
				require(++blocks < 20 * 44100 / BlockSize, "a voice that never stops");
			}
			std::printf("  %-12s rendered %5d blocks, %4.0f ms, then let go\n", sophie::SoundNames[static_cast<size_t>(pad)],
				blocks, blocks * BlockSize * 1000.0 / SampleRate);
		}
		std::printf("PASS: every sound let go once its voice stopped and its chain was quiet for %d blocks\n",
			Engine::OpenDrumQuietBlocks);
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
			for(int t = 0; t < Tracks; ++t)
			{
				e.setMachine(t, SophieMachine);
				e.setParameter(t, 1, 100);
				e.setParameter(t, sophie::Body, 0);
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

	void cost(const Library& _library)
	{
		constexpr int Renders = 20000;
		for(const int pad : {0, 6, 13})
		{
			sophie_t s;
			sophie_init(&s, 44100, 0);
			const auto values = sophie::soundValues(pad);
			for(int i = 0; i < sophie::MachineSlots; ++i)
				sophie::setMachineValue(s, i, values[static_cast<size_t>(i)]);
			float block[BlockSize];
			const auto start = Clock::now();
			for(int i = 0; i < Renders; ++i)
			{
				if(i % 192 == 0)
					sophie_trigger(&s, 127);
				sophie_render(&s, block, BlockSize);
			}
			std::printf("  %-12s alone: %.3f us per 32 samples\n", sophie::SoundNames[static_cast<size_t>(pad)],
				microseconds(Clock::now() - start) / Renders);
		}
		std::vector<float> l(BlockSize), r(BlockSize);
		std::array<float*, Channels> outs{};
		outs[0] = l.data();
		outs[1] = r.data();
		Engine e(_library);
		for(int t = 0; t < Tracks; ++t)
		{
			e.setMachine(t, SophieMachine);
			const auto values = sophie::soundValues(t);
			for(int i = 0; i < sophie::MachineSlots; ++i)
				e.setParameter(t, i, values[static_cast<size_t>(i)]);
		}
		constexpr int Blocks = 20000;
		const auto start = Clock::now();
		for(int b = 0; b < Blocks; ++b)
		{
			if(b % 192 == 0)
				for(int t = 0; t < Tracks; ++t)
					e.noteOn(t, 127);
			e.render(outs.data(), BlockSize);
		}
		std::printf("  16 Tracks, the 16 sounds, through their chains: %.2f us per 32-sample block (budget %.0f)\n",
			microseconds(Clock::now() - start) / Blocks, BlockSize * 1e6 / SampleRate);
		std::printf("PASS: cost measured\n");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		const Library library;
		sounds();
		againstAlone(library);
		body(library);
		pages(library);
		peaks(library);
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
