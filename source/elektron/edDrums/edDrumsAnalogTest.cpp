// The analog family (ticket 28: the Syntakt's analog machines, their control ported, their circuits our own):
//
//   - every machine at its defaults and at both ends of each Machine 1-8 sounds, finite, under 0 dBFS;
//   - each machine as a Track plays it in the engine's 32-sample blocks: the voice alone's samples (analog::Voice at the
//     same values, times the velocity), from Engine::HitDelay after its note wherever the note falls, jitter 0;
//   - set and locked alike, a lock for its Hit alone; a machine change takes effect at the next Hit;
//   - the voice let go once quiet, nothing allocated, the cost of the worst kit at 44.1 and 48 kHz.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "edDrumsAnalogPort.h"
#include "edDrumsAnalogVoice.h"
#include "edDrumsEngine.h"
#include "edDrumsTestSupport.h"
#include "mdDrumsAllocationCounter.h"
#include "stFastStages.h"

namespace
{
	using namespace elektron::drums;
	using namespace elektron::drums::test;
	using elektron::BlockSize;
	using Clock = std::chrono::steady_clock;
	using Values = std::array<uint8_t, 8>;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	Machine machineOf(const int _i) { return {Family::SyntaktAnalog, static_cast<uint8_t>(_i)}; }
	std::string nameOf(const int _i) { return "analog machine " + std::to_string(_i); }

	const ChainSettings Bare{};

	// The voice alone at _values and note 60, _samples of a Hit, times _gain, as the engine renders it
	std::vector<float> alone(const int _machine, const Values& _values, const size_t _samples, const float _gain)
	{
		const syntakt::fast::FlushDenormals ftz;
		analog::Voice v;
		v.setMachine(static_cast<analog::Machine>(_machine));
		v.setControls(_values.data());
		v.setNote(60);
		v.trig();
		std::vector<float> out((_samples + BlockSize - 1) / BlockSize * BlockSize, 0.0f);
		for(size_t at = 0; at < out.size(); at += BlockSize)
			v.render(out.data() + at, BlockSize, SampleRate);
		out.resize(_samples);
		for(auto& x : out)
			x *= _gain;
		return out;
	}

	std::vector<float> throughEngine(const Library& _library, const int _machine, const Values& _values, const int _note,
		const int _velocity, const int _samples, const double _rate = SampleRate)
	{
		Engine e(_library, _rate);
		e.setMachine(0, machineOf(_machine));
		e.setLevel(0, 127);
		e.setChain(0, Bare);
		e.setNote(0, 60);
		for(int i = 0; i < 8; ++i)
			e.setParameter(0, i, _values[static_cast<size_t>(i)]);
		Take take(_note + _samples);
		take.render(e, 0, _note);
		e.noteOn(0, _velocity);
		take.render(e, _note, take.samples() - _note);
		return take.channel(0);
	}

	Values defaults(const int _m)
	{
		const auto d = allDefaults(machineOf(_m));
		Values v{};
		std::copy_n(d.begin(), 8, v.begin());
		return v;
	}

	float peakOf(const std::vector<float>& _x)
	{
		float p = 0;
		for(const float v : _x)
			p = std::max(p, std::abs(v));
		return p;
	}

	// What the port builds at its first use is built with the engine: a Hit of every machine, in a fresh engine, builds nothing
	// on the audio thread
	void tablesBeforeAudio(const Library& _library)
	{
		Engine e(_library);
		const int built = analog::port::tablesBuilt();
		require(built > 0, "the engine built no table");
		std::vector<float> l(BlockSize), r(BlockSize);
		std::array<float*, Channels> outs{};
		outs[0] = l.data();
		outs[1] = r.data();
		for(int m = 0; m < SyntaktAnalogMachines; ++m)
		{
			const int t = m % Tracks;
			e.setMachine(t, machineOf(m));
			for(int i = 0; i < 8; ++i)
				e.setParameter(t, i, static_cast<uint8_t>((m * 37 + i * 53) & 127));
			e.noteOn(t, 127);
			for(int b = 0; b < 64; ++b)
				e.render(outs.data(), BlockSize);
		}
		require(analog::port::tablesBuilt() == built, "a table built during a render");
		std::printf("PASS: the port's %d tables built with the engine, none during the Hits of the %d machines\n", built,
			SyntaktAnalogMachines);
	}

	void sounds()
	{
		for(int m = 0; m < SyntaktAnalogMachines; ++m)
		{
			require(playable(machineOf(m)) && machineParameters(machineOf(m)) == 8, nameOf(m) + ": not 8 parameters");
			const auto d = defaults(m);
			const float peak = peakOf(alone(m, d, 44100, 1.0f));
			require(peak > 0.05f && peak < 1.0f, nameOf(m) + ": its defaults peak at " + std::to_string(peak));
			for(int i = 0; i < 8; ++i)
				for(const int value : {0, 127})
				{
					auto v = d;
					v[static_cast<size_t>(i)] = static_cast<uint8_t>(value);
					const auto x = alone(m, v, 22050, 1.0f);
					for(const float s : x)
						require(std::isfinite(s) && std::abs(s) < 2.0f, nameOf(m) + ": Machine " + std::to_string(i + 1)
							+ " at " + std::to_string(value) + " plays out of bounds");
				}
			std::printf("  %-18s defaults peak %.3f\n", nameOf(m).c_str(), static_cast<double>(peak));
		}
		std::printf("PASS: %d machines sound, finite and bounded at both ends of every control\n", SyntaktAnalogMachines);
	}

	void againstAlone(const Library& _library)
	{
		constexpr int Length = 22050;
		int compared = 0, start = -1;
		for(int m = 0; m < SyntaktAnalogMachines; ++m)
		{
			const auto values = defaults(m);
			for(const int note : {0, 1, 31, 32, 45, 1000})
				for(const int velocity : {127, 64})
				{
					const auto want = alone(m, values, Length, static_cast<float>(velocity) / 127.0f);
					const auto out = throughEngine(_library, m, values, note, velocity, Engine::HitDelay + Length);
					int first = -1;
					for(size_t n = 0; n < out.size(); ++n)
					{
						const auto at = static_cast<int64_t>(n) - note - Engine::HitDelay;
						const float w = at >= 0 && at < Length ? want[static_cast<size_t>(at)] : 0.0f;
						// the engine lets a voice go once it has ended and stayed under Silence: from there it plays 0 where the
						// voice alone still rounds its tail down
						require(out[n] == w || (out[n] == 0 && std::abs(w) < Engine::Silence), nameOf(m) + " through the engine differs at its sample " + std::to_string(at)
							+ ", note at " + std::to_string(note) + ": " + std::to_string(out[n]) + " for " + std::to_string(w));
						if(first < 0 && out[n] != 0)
							first = static_cast<int>(n) - note;
					}
					require(start < 0 || m > 0 || first == start, "a Hit's start moves with its note: jitter");
					if(m == 0)
						start = first;
					++compared;
				}
		}
		std::printf("PASS: %d Hits through the engine's blocks, notes anywhere in a block, velocity 127 and 64: the voice"
			" alone's samples from HitDelay (%d) on, jitter 0\n", compared, Engine::HitDelay);
	}

	std::vector<float> hitWith(const Library& _library, const int _m, const int _index, const int _value, const bool _lock)
	{
		Engine e(_library);
		e.setMachine(0, machineOf(_m));
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
		int heard = 0, total = 0;
		std::string quiet;
		for(int m = 0; m < SyntaktAnalogMachines; ++m)
		{
			const auto plain = hitWith(_library, m, 0, defaults(m)[0], false);
			for(int i = 0; i < 8; ++i)
			{
				if(!analog::usesControl(static_cast<analog::Machine>(m), i))
					continue;
				bool changed = false;
				for(const int value : {0, 127})
				{
					const auto set = hitWith(_library, m, i, value, false);
					require(set == hitWith(_library, m, i, value, true), nameOf(m) + ": set and locked differ");
					changed = changed || set != plain;
				}
				++total;
				heard += changed;
				if(!changed)
					quiet += " " + std::to_string(m) + "/" + std::to_string(i + 1);
			}
		}
		std::printf("  %d of %d controls change their machine's Hit at 0 or 127; not:%s\n", heard, total, quiet.c_str());
		require(heard * 10 >= total * 9, "controls that change nothing");
		std::printf("PASS: Machine 1-8 set and locked alike\n");
	}

	void quiet(const Library& _library)
	{
		for(int m = 0; m < SyntaktAnalogMachines; ++m)
		{
			Engine e(_library);
			e.setMachine(0, machineOf(m));
			Take take(BlockSize);
			e.noteOn(0, 127);
			int blocks = 0;
			while(e.liveTracks() & 1)
			{
				take.render(e, 0, BlockSize);
				require(++blocks < 20 * 44100 / BlockSize, nameOf(m) + ": a voice that never stops");
			}
			std::printf("  %-18s let go after %4.0f ms\n", nameOf(m).c_str(), blocks * BlockSize * 1000.0 / SampleRate);
		}
		std::printf("PASS: every machine let go once its envelope ended and its chain was quiet\n");
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
				e.setMachine(t, machineOf(t % SyntaktAnalogMachines));
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

	// The dearest kit: four Tracks each of the dearest machines voiceCheck measured, the modulated snare with every control
	// at its top, the hard rim, the ride and the beat hat; a Hit on every Track every 192 blocks, at _rate: a core's share
	double kitCost(const Library& _library, const double _rate)
	{
		using analog::Machine;
		const int dearest[] = {static_cast<int>(Machine::SnareFm), static_cast<int>(Machine::RimHard),
			static_cast<int>(Machine::Ride), static_cast<int>(Machine::HatBasic)};
		Engine e(_library, _rate);
		for(int t = 0; t < Tracks; ++t)
		{
			const int m = dearest[t % 4];
			e.setMachine(t, machineOf(m));
			for(int i = 0; m == static_cast<int>(Machine::SnareFm) && i < 8; ++i)
				e.setParameter(t, i, 127);
		}
		const int block = Engine::hostBlockOf(_rate);
		std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
		std::array<float*, Channels> outs{};
		outs[0] = l.data();
		outs[1] = r.data();
		constexpr int Blocks = 6000;
		const auto start = Clock::now();
		for(int b = 0; b < Blocks; ++b)
		{
			if(b % 192 == 0)
				for(int t = 0; t < Tracks; ++t)
					e.noteOn(t, 127);
			e.render(outs.data(), block);
		}
		const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
		return seconds / (Blocks * block / _rate);
	}

	// How a machine sounds at its defaults, the first 186 ms of the voice alone at note 60: its spectrum in 1/3-octave
	// bands from 25 Hz to 16 kHz, in dB about its own mean (loudness does not count), and its envelope in 5 ms frames, in
	// dB of its peak; both floored at -60 dB
	struct Timbre
	{
		std::vector<double> bands, envelope;
	};

	Timbre timbreOf(const int _m)
	{
		constexpr size_t N = 8192;
		const auto x = alone(_m, defaults(_m), N, 1.0f);
		// the spectrum by a plain DFT of the bands' bins, once per machine
		std::vector<double> power(N / 2);
		{
			std::vector<std::complex<double>> a(x.begin(), x.end());
			for(size_t i = 1, j = 0; i < N; ++i)
			{
				size_t bit = N >> 1;
				for(; j & bit; bit >>= 1)
					j ^= bit;
				j ^= bit;
				if(i < j)
					std::swap(a[i], a[j]);
			}
			for(size_t len = 2; len <= N; len <<= 1)
			{
				const std::complex<double> w = std::polar(1.0, -2 * 3.14159265358979323846 / static_cast<double>(len));
				for(size_t i = 0; i < N; i += len)
				{
					std::complex<double> wn(1);
					for(size_t k = 0; k < len / 2; ++k, wn *= w)
					{
						const auto u = a[i + k], v = a[i + k + len / 2] * wn;
						a[i + k] = u + v;
						a[i + k + len / 2] = u - v;
					}
				}
			}
			for(size_t k = 0; k < N / 2; ++k)
				power[k] = std::norm(a[k]);
		}
		Timbre t;
		for(double f0 = 25; f0 < 16000; f0 *= std::pow(2.0, 1.0 / 3))
		{
			double e = 1e-20;
			const auto lo = static_cast<size_t>(f0 / std::pow(2.0, 1.0 / 6) * N / SampleRate) + 1;
			const auto hi = static_cast<size_t>(f0 * std::pow(2.0, 1.0 / 6) * N / SampleRate);
			for(size_t k = lo; k <= hi && k < N / 2; ++k)
				e += power[k];
			t.bands.push_back(10 * std::log10(e));
		}
		double mean = 0;
		for(const double b : t.bands)
			mean += b / static_cast<double>(t.bands.size());
		for(double& b : t.bands)
			b = std::max(b - mean, -60.0);
		constexpr size_t Frame = 220;
		double top = 1e-20;
		for(size_t f = 0; f + Frame <= N; f += Frame)
		{
			double s = 0;
			for(size_t i = f; i < f + Frame; ++i)
				s += static_cast<double>(x[i]) * x[i];
			t.envelope.push_back(s / Frame);
			top = std::max(top, s / Frame);
		}
		for(double& e : t.envelope)
			e = std::max(10 * std::log10(e / top + 1e-20), -60.0);
		return t;
	}

	double rmsOf(const std::vector<double>& _a, const std::vector<double>& _b)
	{
		double s = 0;
		for(size_t i = 0; i < _a.size(); ++i)
			s += (_a[i] - _b[i]) * (_a[i] - _b[i]);
		return std::sqrt(s / static_cast<double>(_a.size()));
	}

	// The user's listening, 2026-11-12: some machines of a family sounded alike. Within each family, every pair's distance
	// at the defaults (spectrum and envelope, dB), and the pairs under half the family's median: CLOSE. The families made
	// distinct so far must have none; the others are printed until their round
	void distinct()
	{
		using analog::Machine;
		struct Group
		{
			const char* name;
			Machine first, last;
			bool required;
		};
		const Group groups[] = {{"kicks", Machine::KickHard, Machine::KickAcoustic, true},
			{"snares and rims", Machine::SnareHard, Machine::RimClassic, false},
			{"synths", Machine::SynthDual, Machine::SynthChip, false},
			{"metal", Machine::ClosedClassic, Machine::HatLab, false}};
		for(const auto& g : groups)
		{
			std::vector<Timbre> t;
			for(int m = static_cast<int>(g.first); m <= static_cast<int>(g.last); ++m)
				t.push_back(timbreOf(m));
			std::vector<double> all;
			for(size_t i = 0; i < t.size(); ++i)
				for(size_t j = i + 1; j < t.size(); ++j)
					all.push_back(rmsOf(t[i].bands, t[j].bands) + rmsOf(t[i].envelope, t[j].envelope));
			std::vector<double> sorted = all;
			std::sort(sorted.begin(), sorted.end());
			const double median = sorted[sorted.size() / 2];
			std::string close;
			size_t k = 0;
			for(size_t i = 0; i < t.size(); ++i)
				for(size_t j = i + 1; j < t.size(); ++j, ++k)
					if(all[k] < median / 2)
						close += " " + std::to_string(static_cast<int>(g.first) + static_cast<int>(i)) + "-"
							+ std::to_string(static_cast<int>(g.first) + static_cast<int>(j));
			std::printf("  %-16s median distance %.1f dB, closest %.1f dB; close pairs:%s\n", g.name, median, sorted.front(),
				close.empty() ? " none" : close.c_str());
			require(!g.required || close.empty(), std::string(g.name) + ": machines that sound alike:" + close);
		}
		std::printf("PASS: no two kicks alike at their defaults\n");
	}

	void cost(const Library& _library)
	{
		const double at44 = kitCost(_library, SampleRate), at48 = kitCost(_library, 48000),
			at88 = kitCost(_library, 88200), at96 = kitCost(_library, 96000);
		std::printf("  the dearest kit, 16 Tracks: %.1f %% of a core at 44.1 kHz, %.1f %% at 48, %.1f %% at 88.2, %.1f %% at 96\n",
			at44 * 100, at48 * 100, at88 * 100, at96 * 100);
		require(at44 < 0.15 && at48 < 0.15 && at88 < 0.15 && at96 < 0.15, "the dearest kit over 15 % of a core");
		std::printf("PASS: cost within the budget\n");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		const Library library;
		tablesBeforeAudio(library);
		sounds();
		againstAlone(library);
		pages(library);
		quiet(library);
		allocations(library);
		distinct();
		cost(library);
		return 0;
	}
	catch(const std::exception& _e)
	{
		std::printf("FAIL: %s\n", _e.what());
		return 1;
	}
}
