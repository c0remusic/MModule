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

	// The power spectrum (N / 2 bins) of _n samples of _x, each times _w[i] if given, zero-padded to N
	constexpr size_t SpectrumSize = 8192;
	std::vector<double> powerOf(const float* _x, const size_t _n, const float* _w)
	{
		constexpr size_t N = SpectrumSize;
		std::vector<std::complex<double>> a(N);
		for(size_t i = 0; i < _n && i < N; ++i)
			a[i] = _x[i] * (_w ? _w[i] : 1.0f);
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
		std::vector<double> power(N / 2);
		for(size_t k = 0; k < N / 2; ++k)
			power[k] = std::norm(a[k]);
		return power;
	}

	// A spectrum's 1/3-octave bands from 25 Hz to 16 kHz, in dB
	std::vector<double> bandsOf(const std::vector<double>& _power, const bool _aWeighted)
	{
		std::vector<double> bands;
		for(double f0 = 25; f0 < 16000; f0 *= std::pow(2.0, 1.0 / 3))
		{
			double e = 1e-20;
			const auto lo = static_cast<size_t>(f0 / std::pow(2.0, 1.0 / 6) * SpectrumSize / SampleRate) + 1;
			const auto hi = static_cast<size_t>(f0 * std::pow(2.0, 1.0 / 6) * SpectrumSize / SampleRate);
			for(size_t k = lo; k <= hi && k < SpectrumSize / 2; ++k)
				e += _power[k];
			double db = 10 * std::log10(e);
			if(_aWeighted)
			{
				const double f2 = f0 * f0;
				const double ra = 12194.0 * 12194.0 * f2 * f2 / ((f2 + 20.6 * 20.6) *
					std::sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
				db += 20 * std::log10(ra) + 2.0;
			}
			bands.push_back(db);
		}
		return bands;
	}
	// ... as the ear weighs them (A-weighting), about the loudest band, what lies 30 dB under it counted as silence
	std::vector<double> heardBandsOf(const std::vector<double>& _power)
	{
		auto b = bandsOf(_power, true);
		const double top = *std::max_element(b.begin(), b.end());
		for(double& v : b)
			v = std::max(v - top, -30.0);
		return b;
	}

	// How a machine sounds at its defaults, the voice alone at note 60:
	// - bands and envelope (the full measure): the first 186 ms's spectrum in 1/3-octave bands, in dB about their own
	//   mean (loudness does not count), and its envelope in 5 ms frames, in dB of its peak; both floored at -60 dB;
	// - head and tail (the spectrum alone, the user's listening of 2026-11-13: RING HAT, RING OPEN and RING CRASH, 15 dB
	//   apart in the full measure, sounded like one sound at three lengths, as a decay alone counts there as timbre):
	//   the first 60 ms (its last 10 faded, so that no cut edge leaks a decay's mark into the bands), then from 60 ms on
	//   Hann frames of 2048 samples, 1024 apart, each about its own loudest band (a frame's level, hence the decay, does
	//   not count), averaged over the frames within 30 dB of the sound's loudest frame; a sound with no such frame takes
	//   its head as its tail. Both heard bands (heardBandsOf). A decay alone moves neither: RING HAT at DECAY 10 and 60
	//   are 0.14 dB apart here, 11.5 dB in the full measure
	struct Timbre
	{
		std::vector<double> bands, envelope, head, tail;
	};

	Timbre timbreOf(const int _m)
	{
		constexpr size_t N = SpectrumSize, Length = 65536;
		const auto x = alone(_m, defaults(_m), Length, 1.0f);
		Timbre t;
		t.bands = bandsOf(powerOf(x.data(), N, nullptr), false);
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

		constexpr size_t Head = 2646, Fade = 441, F = 2048, Hop = 1024;	// 60 and 10 ms at 44.1 kHz
		std::vector<float> fade(Head, 1.0f), hann(F);
		for(size_t i = Head - Fade; i < Head; ++i)
			fade[i] = static_cast<float>(0.5 + 0.5 * std::cos(3.14159265358979323846 * static_cast<double>(i - (Head - Fade)) / Fade));
		for(size_t i = 0; i < F; ++i)
			hann[i] = static_cast<float>(0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * static_cast<double>(i) / F));
		t.head = heardBandsOf(powerOf(x.data(), Head, fade.data()));
		const auto energyAt = [&](const size_t _at)
		{
			double e = 0;
			for(size_t i = 0; i < F; ++i)
				e += static_cast<double>(x[_at + i]) * x[_at + i] * hann[i] * hann[i];
			return e;
		};
		double loudest = 0;
		for(size_t at = 0; at + F <= Length; at += Hop)
			loudest = std::max(loudest, energyAt(at));
		t.tail.assign(t.head.size(), 0.0);
		int kept = 0;
		for(size_t at = Head; at + F <= Length; at += Hop)
		{
			if(energyAt(at) < loudest * 1e-3)
				continue;
			const auto frame = heardBandsOf(powerOf(x.data() + at, F, hann.data()));
			for(size_t b = 0; b < t.tail.size(); ++b)
				t.tail[b] += frame[b];
			++kept;
		}
		if(kept == 0)
			t.tail = t.head;
		else
			for(double& b : t.tail)
				b /= kept;
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
	// at the defaults in two measures, the full one (spectrum and envelope, dB) and the spectrum alone (head and tail,
	// dB), and the pairs under the family's fixed line in either: CLOSE; there must be none, but for the spectrum-only
	// lists below
	void distinct()
	{
		using analog::Machine;
		struct Group
		{
			const char* name;
			Machine first, last;
			double before;		// the family's median full distance before its timbre round, dB
			double spectrum;	// its median spectrum-only distance before the round of 2026-11-13, dB
		};
		// The lines are fixed, half each family's median measured before its timbre round (2026-11-12): a line relative to
		// the median as it stands would let a round pass by bringing far pairs closer. Kicks 7.5 dB (line 3.75), snares and
		// rims 25.4 (12.7), metal 21.2 (10.6); the synths had no round, their median then 18.0 (9.0). The medians are
		// printed beside those of before: a family whose machines share one circuit, as the metal's 808 bank, keeps a lower one
		// The spectrum-only lines are half each family's median spectrum-only distance as committed in 346c29eb (measured
		// 2026-11-13 by the same prints, out of tree): kicks 10.85 dB (line 5.43), snares and rims 11.09 (5.55), synths
		// 7.67 (3.84), metal 8.93 (4.47)
		const Group groups[] = {{"kicks", Machine::KickHard, Machine::KickAcoustic, 7.5, 10.85},
			{"snares and rims", Machine::SnareHard, Machine::RimClassic, 25.4, 11.09},
			{"synths", Machine::SynthDual, Machine::SynthChip, 18.0, 7.67},
			{"metal", Machine::ClosedClassic, Machine::HatLab, 21.2, 8.93}};
		// Pairs the spectrum-only line does not judge, each list with its reason:
		// - one circuit: a closed and an open hat on the same circuit share their metal, as on an 808, and may differ in
		//   length alone (the user's decision, 2026-11-13);
		// - pending the user's ear: under the line before the round of 2026-11-13 and not touched by it (HARD THUMP and
		//   SOFT KICK; the 808 hats against BEAT HAT); printed, not failed, until the user has heard them
		struct Pair
		{
			Machine a, b;
		};
		static constexpr Pair oneCircuit[] = {{Machine::ClosedClassic, Machine::OpenClassic}};
		static constexpr Pair pendingEar[] = {{Machine::KickHard, Machine::KickSilky},
			{Machine::ClosedClassic, Machine::HatBasic}, {Machine::OpenClassic, Machine::HatBasic}};
		const auto listed = [](const auto& _list, const int _a, const int _b)
		{
			for(const auto& p : _list)
				if(static_cast<int>(p.a) == _a && static_cast<int>(p.b) == _b)
					return true;
			return false;
		};
		for(const auto& g : groups)
		{
			std::vector<Timbre> t;
			for(int m = static_cast<int>(g.first); m <= static_cast<int>(g.last); ++m)
				t.push_back(timbreOf(m));
			std::vector<double> full, spectrum;
			for(size_t i = 0; i < t.size(); ++i)
				for(size_t j = i + 1; j < t.size(); ++j)
				{
					full.push_back(rmsOf(t[i].bands, t[j].bands) + rmsOf(t[i].envelope, t[j].envelope));
					spectrum.push_back(0.5 * (rmsOf(t[i].head, t[j].head) + rmsOf(t[i].tail, t[j].tail)));
				}
			std::string close;
			const auto check = [&](const char* _measure, const std::vector<double>& _all, const double _before,
				const bool _lists)
			{
				std::vector<double> sorted = _all;
				std::sort(sorted.begin(), sorted.end());
				std::string pairs, same, pending;
				size_t k = 0;
				for(size_t i = 0; i < t.size(); ++i)
					for(size_t j = i + 1; j < t.size(); ++j, ++k)
					{
						if(_all[k] >= _before / 2)
							continue;
						const int a = static_cast<int>(g.first) + static_cast<int>(i);
						const int b = static_cast<int>(g.first) + static_cast<int>(j);
						char text[48];
						std::snprintf(text, sizeof(text), " %d-%d (%.1f dB)", a, b, _all[k]);
						if(_lists && listed(oneCircuit, a, b))
							same += text;
						else if(_lists && listed(pendingEar, a, b))
							pending += text;
						else
							pairs += text;
					}
				std::printf("  %-16s %-13s median %.1f dB (before %.1f), closest %.1f dB, line %.2f; close pairs:%s\n",
					g.name, _measure, sorted[sorted.size() / 2], _before, sorted.front(), _before / 2,
					pairs.empty() ? " none" : pairs.c_str());
				if(!same.empty())
					std::printf("  %-16s %-13s one circuit, closed and open (the user's decision, 2026-11-13):%s\n", g.name,
						_measure, same.c_str());
				if(!pending.empty())
					std::printf("  %-16s %-13s pending the user's ear:%s\n", g.name, _measure, pending.c_str());
				if(!pairs.empty())
					close += std::string(" (") + _measure + ")" + pairs;
			};
			check("full", full, g.before, false);
			check("spectrum only", spectrum, g.spectrum, true);
			require(close.empty(), std::string(g.name) + ": machines that sound alike:" + close);
		}
		std::printf("PASS: no two machines of a family alike at their defaults\n");
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
