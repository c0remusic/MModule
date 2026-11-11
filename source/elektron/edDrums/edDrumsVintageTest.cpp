// The vintage PCM character (edDrumsVintage.h), as the Sampler's VINTAGE grain and as the chain's DAC drive:
//
//   - the DAC: linear and companded codes exact on known inputs, every code decoded and taken back, the nearest code;
//   - the low-pass: its -3 dB point measured within 5 % of its cutoff;
//   - the hold: pitch +12 doubles the rate at which the grain's held value changes, and the pitch of what it plays;
//   - the DAC drive at its neutral setting next to no drive, its residual in dB; the drives stored before it keep their
//     numbers, an old Sampler (Machine 9-24 at 0) plays the ROM player's grain whatever BITS, COMPANDING and OUTPUT
//     FILTER say;
//   - a Kit's switch of 16 long samples in one block, bounded, nothing copied or allocated; a Choke, a sample given and a
//     grain changed let the voice go; an LFO on PTCH moves the pitch, one on END stays inside the sample;
//   - nothing allocated on the audio path, the cost of a voice and of the drive.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "edDrumsChain.h"
#include "edDrumsEngine.h"
#include "edDrumsTestSupport.h"
#include "edDrumsVintage.h"
#include "mdDrumsAllocationCounter.h"

namespace
{
	using namespace elektron::drums;
	using namespace elektron::drums::test;
	using elektron::Block;
	using elektron::BlockSize;
	using elektron::Lanes;
	using Clock = std::chrono::steady_clock;
	namespace vt = elektron::drums::vintage;

	constexpr double Pi = 3.14159265358979323846;

	static_assert(static_cast<int>(DriveType::MdSrr) == 3 && static_cast<int>(DriveType::Dac) == 4,
		"the drives stored before DAC keep their numbers, DAC after them");

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	double db(const double _x)
	{
		return 20 * std::log10(_x);
	}

	// ---- the DAC ----

	void law()
	{
		// 8 bits companded: M = 4, full scale 31 x 128 - 16 = 3952 units; segment s steps 2^s units from (2^s - 1) 16
		const auto units = [](const double _u) { return static_cast<float>(_u) / 3952.0f; };
		struct Known { double units; int code; double value; };
		const Known known[] = {
			{0, 0, 0}, {1, 1, 1}, {15, 15, 15},				// segment 0: steps of 1
			{16, 16, 16}, {16.99, 16, 16}, {17.01, 17, 18},	// segment 1: steps of 2 from 16
			{46, 31, 46}, {47.01, 32, 48}, {48, 32, 48},	// its top, then nearer the next segment's first
			{3824, 126, 3824}, {3952, 127, 3952}, {5000, 127, 3952},	// segment 7: steps of 128, then full scale
		};
		for(const auto& k : known)
		{
			const int code = vt::compandCode(units(k.units), 8);
			require(code == k.code, "8 bits: " + std::to_string(k.units) + " units gave code " + std::to_string(code));
			require(vt::compandValue(code, 8) == units(k.value), "8 bits: code " + std::to_string(code) + "'s value");
		}
		// In units, where the values are whole: the build's floating point may round a quotient either way
		require(vt::dac(-units(47.01), 8, true) * 3952 == -48 && vt::dac(units(3824), 8, true) * 3952 == 3824,
			"the companded DAC: a sign either side of the segment law");

		// Every width: each code decoded comes back, the values rise, and a magnitude takes the nearest
		int checked = 0;
		for(int bits = vt::MinBits; bits <= vt::MaxBits; ++bits)
		{
			const int codes = 1 << (bits - 1);
			for(int c = 0; c < codes; ++c)
			{
				require(vt::compandCode(vt::compandValue(c, bits), bits) == c, std::to_string(bits) + " bits: code "
					+ std::to_string(c) + " does not come back");
				require(c == 0 || vt::compandValue(c, bits) > vt::compandValue(c - 1, bits), "the values rise");
			}
			for(int i = 0; i <= 20000; ++i)
			{
				const float x = static_cast<float>(i) / 20000;
				const int c = vt::compandCode(x, bits);
				const float d = std::abs(vt::compandValue(c, bits) - x);
				for(const int o : {c - 1, c + 1})
					if(o >= 0 && o < codes)
						require(d <= std::abs(vt::compandValue(o, bits) - x) + 1e-7f, std::to_string(bits)
							+ " bits: not the nearest code at " + std::to_string(x));
				++checked;
			}
			require(vt::compandValue(codes - 1, bits) == 1.0f, "full scale at the top code");
		}

		// Linear: two's complement in steps of 2^(1 - bits)
		require(vt::dac(0.5f, 8, false) == 0.5f && vt::dac(1.0f, 8, false) == 127 / 128.0f && vt::dac(-1.0f, 8, false) == -1
			&& vt::dac(0.3f, 6, false) == 10 / 32.0f && vt::dac(-0.3f, 6, false) == -10 / 32.0f
			&& vt::dac(1 / 4096.0f, 12, false) == 1 / 2048.0f && vt::dac(0.9f / 4096, 12, false) == 0, "linear codes");
		require(vt::bitsOf(0) == 6 && vt::bitsOf(18) == 6 && vt::bitsOf(19) == 7 && vt::bitsOf(127) == 12
			&& vt::bitsOf(40) == 8, "BITS: 6 to 12");
		std::printf("PASS: the DAC's codes exact on %d known inputs, %d magnitudes nearest at 6 to 12 bits, every code back\n",
			static_cast<int>(std::size(known)), checked);
	}

	// ---- the low-pass ----

	double gainAt(const double _cutoff, const double _hz, const double _rate)
	{
		vt::LowPass f;
		f.set(_cutoff, _rate);
		const int settle = static_cast<int>(_rate / 10), length = static_cast<int>(_rate / 2);
		double in = 0, out = 0;
		for(int n = 0; n < settle + length; ++n)
		{
			const double x = std::sin(2 * Pi * _hz * n / _rate);
			const double y = f.process(static_cast<float>(x));
			if(n >= settle)
			{
				in += x * x;
				out += y * y;
			}
		}
		return std::sqrt(out / in);
	}

	void lowPass()
	{
		for(const double rate : {44100.0, 48000.0})
			for(const double cutoff : {200.0, 1000.0, 4000.0, 10000.0, 16000.0})
			{
				// The -3 dB point, between an octave below and one above
				double lo = cutoff / 2, hi = std::min(cutoff * 2, 0.49 * rate);
				for(int i = 0; i < 40; ++i)
				{
					const double mid = std::sqrt(lo * hi);
					(gainAt(cutoff, mid, rate) > std::sqrt(0.5) ? lo : hi) = mid;
				}
				const double error = std::abs(lo / cutoff - 1);
				require(error < 0.05, "the low-pass at " + std::to_string(cutoff) + " Hz is -3 dB at " + std::to_string(lo));
				std::printf("  %5.0f Hz at %5.0f Hz: -3 dB at %8.1f Hz (%.2f %%)\n", cutoff, rate, lo, 100 * error);
			}
		require(std::abs(vt::filterHz(0) - 100) < 1e-9 && std::abs(vt::filterHz(127) - 20000) < 1e-6, "OUTPUT FILTER's range");
		std::printf("PASS: the low-pass's -3 dB point within 5 %% of its cutoff\n");
	}

	// ---- the hold ----

	// Changes of the grain's held value in a second, its filter off: what the clock ticks, the samples all differ
	int changes(const double _semitones)
	{
		std::vector<int16_t> pcm(48000);
		for(size_t n = 0; n < pcm.size(); ++n)
			pcm[n] = static_cast<int16_t>((n % 2 ? 1 : -1) * (4000 + static_cast<int>(n % 1000) * 16));
		constexpr double Rate = 8000;
		vt::Player p;
		p.setDac(12, false);
		p.setFilter(0, SampleRate);
		p.setDecay(vt::decaySeconds(127), SampleRate);
		p.setStep(Rate / SampleRate * std::pow(2.0, _semitones / 12));
		p.trig(pcm.data(), 0, static_cast<uint32_t>(pcm.size()), 0);
		std::vector<float> out(44100, 0.0f);
		p.render(out.data(), static_cast<int>(out.size()));
		int count = 0;
		for(size_t n = 1; n < out.size(); ++n)
			count += out[n] != out[n - 1];
		return count;
	}

	Sample sampleOf(const std::vector<float>& _mono, const double _rate)
	{
		auto made = makeSample("test", _mono, _rate);
		require(std::holds_alternative<Sample>(made), "the sample is not made");
		return std::get<Sample>(std::move(made));
	}

	const ChainSettings Bare{};

	// Track 0 on the Sampler with _sample, Machine 1-24 at _values, one Hit at velocity 127; Main left
	std::vector<float> sampler(const Library& _library, const Sample& _sample, const std::array<uint8_t, MachineParameters>& _values,
		const int _samples)
	{
		Engine e(_library);
		e.setMachine(0, Sampler);
		e.setLevel(0, 127);
		e.setChain(0, Bare);
		e.setSample(0, &_sample);
		for(int i = 0; i < MachineParameters; ++i)
			e.setParameter(0, i, _values[static_cast<size_t>(i)]);
		Take take(_samples);
		e.noteOn(0, 127);
		take.render(e, 0, _samples);
		return take.channel(0);
	}

	// Its frequency from its rising zero crossings
	double frequencyOf(const std::vector<float>& _x, const size_t _from, const size_t _to)
	{
		std::vector<double> crossings;
		for(size_t n = _from + 1; n < _to; ++n)
			if(_x[n - 1] < 0 && _x[n] >= 0)
				crossings.push_back(static_cast<double>(n - 1) + _x[n - 1] / (_x[n - 1] - _x[n]));
		require(crossings.size() > 10, "no tone");
		return (static_cast<double>(crossings.size()) - 1) * SampleRate / (crossings.back() - crossings.front());
	}

	std::array<uint8_t, MachineParameters> vintageValues(const int _ptch)
	{
		auto v = allDefaults(Sampler);
		v[0] = static_cast<uint8_t>(_ptch);
		v[vt::GrainSlot] = 127;
		v[vt::FilterSlot] = 72;		// 2 kHz: the hold's images well under the tone
		return v;
	}

	void hold(const Library& _library)
	{
		const int at0 = changes(0), at12 = changes(12);
		const double ratio = static_cast<double>(at12) / at0;
		require(std::abs(at0 - 8000) <= 2 && std::abs(ratio - 2) < 0.001, "the hold's rate: " + std::to_string(at0) + " then "
			+ std::to_string(at12) + " a second");
		require(std::abs(vt::semitones(96) - 12) < 1e-12 && std::abs(vt::semitones(64)) < 1e-12, "PTCH 96 is +12");

		// Through the engine: a 500 Hz sine at 8 kHz, PTCH 64 then 96
		std::vector<float> sine(16000);
		for(size_t n = 0; n < sine.size(); ++n)
			sine[n] = 0.5f * static_cast<float>(std::sin(2 * Pi * 500 * static_cast<double>(n) / 8000));
		const auto s = sampleOf(sine, 8000);
		const auto a = sampler(_library, s, vintageValues(64), 44100), b = sampler(_library, s, vintageValues(96), 44100);
		const double fa = frequencyOf(a, 2000, 40000), fb = frequencyOf(b, 2000, 20000);
		require(std::abs(fa - 500) < 2 && std::abs(fb / fa - 2) < 0.005, "the grain's pitch: " + std::to_string(fa) + " Hz then "
			+ std::to_string(fb));
		// Its first sample HitDelay after the note, wherever the note falls in its block
		const auto dc = sampleOf(std::vector<float>(4000, 0.5f), 44100);
		for(const int note : {0, 1, 17, 31})
		{
			Engine e(_library);
			e.setMachine(0, Sampler);
			e.setChain(0, Bare);
			e.setSample(0, &dc);
			const auto v = vintageValues(64);
			for(int i = 0; i < MachineParameters; ++i)
				e.setParameter(0, i, v[static_cast<size_t>(i)]);
			Take take(note + 400);
			take.render(e, 0, note);
			e.noteOn(0, 127);
			take.render(e, note, take.samples() - note);
			const auto& x = take.channel(0);
			const auto first = std::find_if(x.begin(), x.end(), [](const float _v) { return _v != 0; }) - x.begin();
			require(first == note + Engine::HitDelay, "the grain's first sample at " + std::to_string(first - note)
				+ " after its note");
		}
		std::printf("PASS: the hold changes %d times a second at 8 kHz, %d at +12 (x %.4f); the grain plays %.2f Hz, then %.2f;"
			" its first sample %d after its note\n", at0, at12, ratio, fa, fb, Engine::HitDelay);
	}

	// ---- the DAC drive ----

	std::vector<float> throughChain(const ChainSettings& _settings, const std::vector<float>& _x)
	{
		Chain chain(SampleRate, false);
		chain.configure(0, _settings);
		chain.hit(0, 127);
		std::vector<float> out(_x.size());
		Block b{};
		for(size_t at = 0; at + BlockSize <= _x.size(); at += BlockSize)
		{
			for(int n = 0; n < BlockSize; ++n)
				b.s[n][0] = _x[at + static_cast<size_t>(n)];
			chain.process(b, 1);
			for(int n = 0; n < BlockSize; ++n)
				out[at + static_cast<size_t>(n)] = b.s[n][0];
		}
		return out;
	}

	void drive()
	{
		std::vector<float> sine(44100);
		for(size_t n = 0; n < sine.size(); ++n)
			sine[n] = 0.5f * static_cast<float>(std::sin(2 * Pi * 997 * static_cast<double>(n) / SampleRate));
		ChainSettings dac;
		dac.drive[0] = {DriveType::Dac, false, 0, 127};
		const auto dry = throughChain(Bare, sine), wet = throughChain(dac, sine);
		double signal = 0, residual = 0;
		for(size_t n = 0; n < sine.size(); ++n)
		{
			signal += dry[n] * dry[n];
			residual += (wet[n] - dry[n]) * (wet[n] - dry[n]);
		}
		const double neutral = db(std::sqrt(residual / signal));
		require(neutral < -65, "the DAC drive at 0 is not transparent: " + std::to_string(neutral) + " dB");
		const auto s0 = vt::driveSetting(0, SampleRate), s127 = vt::driveSetting(127, SampleRate);
		require(s0.bits == 12 && !s0.companding && s0.clockHz == SampleRate && s0.cutoffHz == 0, "the neutral setting");
		require(s127.bits == 6 && s127.companding && std::abs(s127.clockHz - 4000) < 1e-6 && std::abs(s127.cutoffHz - 1800) < 1e-6,
			"the top setting");

		// At 64: 9 bits companded, its clock at 13.3 kHz, held: the samples change at that rate
		const auto s64 = vt::driveSetting(64, SampleRate);
		dac.drive[0].amount = 64;
		dac.drive[0].post = true;
		const auto held = throughChain(dac, sine);
		std::printf("  at 64: %d bits, companded, clock %.0f Hz, low-pass %.0f Hz; output peak %.3f\n", s64.bits, s64.clockHz,
			s64.cutoffHz, *std::max_element(held.begin(), held.end()));
		require(s64.bits == 9 && s64.companding, "AMOUNT 64");
		std::printf("PASS: the DAC drive at 0 (12 bits, linear, every sample, no filter) %.1f dB from no drive on a -6 dBFS"
			" tone\n", neutral);
	}

	// ---- old states, allocations, cost ----

	void oldSampler(const Library& _library)
	{
		require(machineParameters(Sampler) == vt::SamplerParameters, "the Sampler uses Machine 1-12");
		const auto d = allDefaults(Sampler);
		require(!vt::isVintage(d[vt::GrainSlot]), "a new Sampler plays the ROM player's grain");
		for(int i = vt::SamplerParameters; i < MachineParameters; ++i)
			require(d[static_cast<size_t>(i)] == 0, "Machine 13-24 stay 0");

		std::vector<float> tone(20000);
		for(size_t n = 0; n < tone.size(); ++n)
			tone[n] = 0.6f * static_cast<float>(std::sin(2 * Pi * 220 * static_cast<double>(n) / 44100)) * std::exp(-static_cast<float>(n) / 6000);
		const auto s = sampleOf(tone, 44100);
		// A Track saved before the grains: Machine 9-24 at 0
		std::array<uint8_t, MachineParameters> old{};
		const auto first = defaults(Sampler);
		std::copy(first.begin(), first.end(), old.begin());
		auto moved = old;
		moved[vt::BitsSlot] = 127;
		moved[vt::CompandingSlot] = 127;
		moved[vt::FilterSlot] = 3;
		const auto a = sampler(_library, s, old, 30000), b = sampler(_library, s, moved, 30000);
		require(a == b, "BITS, COMPANDING or OUTPUT FILTER change the ROM player's grain");
		float peak = 0;
		for(const float x : a)
			peak = std::max(peak, std::abs(x));
		require(peak > 0.05f, "the old Sampler is silent");
		std::printf("PASS: an old Sampler (Machine 9-24 at 0) plays the ROM player's grain, peak %.3f, the same whatever BITS,"
			" COMPANDING and OUTPUT FILTER\n", peak);
	}

	void allocations(const Library& _library)
	{
		std::vector<float> tone(96000);
		for(size_t n = 0; n < tone.size(); ++n)
			tone[n] = 0.5f * static_cast<float>(std::sin(static_cast<double>(n) * 0.05));
		const auto s = sampleOf(tone, 48000);
		Engine e(_library);
		std::vector<float> l(BlockSize), r(BlockSize);
		std::array<float*, Channels> outs{};
		outs[0] = l.data();
		outs[1] = r.data();
		ChainSettings dac = machinedrumChain();
		dac.drive[1] = {DriveType::Dac, false, 90, 100};
		const auto allocated = countAllocations([&]
		{
			for(int t = 0; t < Tracks; ++t)
			{
				e.setMachine(t, Sampler);
				e.setSample(t, &s);
				e.setParameter(t, vt::GrainSlot, 127);
				e.setParameter(t, vt::BitsSlot, 30);
				e.setParameter(t, vt::FilterSlot, 90);
				e.setChain(t, dac);
				e.lock(t, 0, 80);
				e.noteOn(t, 100);
				for(int b = 0; b < 8; ++b)
					e.render(outs.data(), BlockSize);
				e.setParameter(t, vt::GrainSlot, 0);
				e.noteOn(t, 100);
				e.render(outs.data(), BlockSize);
			}
		});
		require(allocated == 0, std::to_string(allocated) + " allocations on the audio path");
		std::printf("PASS: samples, grains, drives, locks, Hits and blocks allocate nothing\n");
	}

	double microseconds(const Clock::duration _d)
	{
		return std::chrono::duration<double, std::micro>(_d).count();
	}

	// Track _track on VINTAGE with _sample, its chain bare
	void vintageTrack(Engine& _e, const int _track, const Sample& _sample, const int _ptch = 64)
	{
		_e.setMachine(_track, Sampler);
		_e.setLevel(_track, 127);
		_e.setChain(_track, Bare);
		_e.setSample(_track, &_sample);
		const auto v = vintageValues(_ptch);
		for(int i = 0; i < MachineParameters; ++i)
			_e.setParameter(_track, i, v[static_cast<size_t>(i)]);
	}

	// A Kit's switch: 16 Tracks playing 10 s samples given others in one block, the samples read where they lie
	void kitSwitch(const Library& _library)
	{
		std::vector<float> a(Sample::MaxSourceLength), b(Sample::MaxSourceLength);
		for(size_t n = 0; n < a.size(); ++n)
		{
			a[n] = 0.5f * static_cast<float>(std::sin(static_cast<double>(n) * 0.03));
			b[n] = 0.4f * static_cast<float>(std::sin(static_cast<double>(n) * 0.05));
		}
		const auto sa = sampleOf(a, 48000), sb = sampleOf(b, 48000);
		Engine e(_library);
		std::vector<float> l(BlockSize), r(BlockSize);
		std::array<float*, Channels> outs{};
		outs[0] = l.data();
		outs[1] = r.data();
		for(int t = 0; t < Tracks; ++t)
		{
			vintageTrack(e, t, sa);
			e.noteOn(t, 127);
		}
		double worst = 0;
		size_t allocated = 0;
		for(int k = 0; k < 40; ++k)
		{
			for(int i = 0; i < 16; ++i)
				e.render(outs.data(), BlockSize);
			for(int t = 0; t < Tracks; ++t)
				e.noteOn(t, 127);
			const auto start = Clock::now();
			allocated += countAllocations([&]
			{
				for(int t = 0; t < Tracks; ++t)
					e.setSample(t, k % 2 ? &sa : &sb);
				e.render(outs.data(), BlockSize);
			});
			worst = std::max(worst, microseconds(Clock::now() - start));
		}
		require(allocated == 0, "a Kit switch allocates");
		require(worst < 500, "a Kit switch of 16 long samples took " + std::to_string(worst) + " us in one block");
		std::printf("PASS: 16 Tracks given 10 s samples under their voices in one block: at worst %.1f us (bound 500), nothing"
			" allocated\n", worst);
	}

	// A Choke, a sample given and a grain changed end the voice with their ramp: the Track lets it go
	void cuts(const Library& _library)
	{
		std::vector<float> long10(Sample::MaxSourceLength, 0.3f);
		const auto s = sampleOf(long10, 48000);
		std::vector<float> l(BlockSize), r(BlockSize);
		std::array<float*, Channels> outs{};
		outs[0] = l.data();
		outs[1] = r.data();
		const char* names[]{"a Choke", "a sample given", "the grain changed"};
		for(int how = 0; how < 3; ++how)
		{
			Engine e(_library);
			vintageTrack(e, 0, s);
			e.setOutput(0, 1);		// its own on Out 01
			e.setMachine(1, Machine{Family::Machinedrum, 16});
			e.setChokes(1, 1);
			e.noteOn(0, 127);
			for(int b = 0; b < 100; ++b)
				e.render(outs.data(), BlockSize);
			require(e.liveTracks() & 1, "the voice plays");
			if(how == 0)
				e.noteOn(1, 127);
			else if(how == 1)
				e.setSample(0, &s);
			else
				e.setParameter(0, vt::GrainSlot, 0);
			int blocks = 0;
			while(e.liveTracks() & 1)
			{
				e.render(outs.data(), BlockSize);
				require(++blocks < Engine::OpenDrumQuietBlocks + 16, std::string(names[how])
					+ ": the Track still renders " + std::to_string(blocks) + " blocks on");
			}
			// Past the ramp, its Out is silent
			Take take(4096);
			take.render(e, 0, take.samples());
			float after = 0;
			for(size_t n = 2 * Engine::HitDelay + Engine::CutSamples; n < 4096; ++n)
				after = std::max({after, std::abs(take.channel(2)[n]), std::abs(take.channel(3)[n])});
			require(after < Engine::Silence, std::string(names[how]) + ": the Track sounds on past its ramp");
			std::printf("  %s: the Track let go %d blocks on, silent past its ramp\n", names[how], blocks);
		}
		std::printf("PASS: a Choke, a sample given, a grain changed: the VINTAGE voice ends with its ramp\n");
	}

	// An LFO on PTCH moves the pitch; one on END past 127 never reads past the sample
	void lfos(const Library& _library)
	{
		std::vector<float> sine(80000);
		for(size_t n = 0; n < sine.size(); ++n)
			sine[n] = 0.5f * static_cast<float>(std::sin(2 * Pi * 500 * static_cast<double>(n) / 8000));
		const auto s = sampleOf(sine, 8000);
		LfoSettings lfo;
		lfo.rate.sync = false;
		lfo.rate.hz = 2;
		lfo.shape = LfoShape::Square;
		lfo.depth = 64;
		lfo.target = LfoTarget::Machine1;
		Engine e(_library);
		vintageTrack(e, 0, s);
		e.setLfo(0, 0, lfo);
		Take take(44100);
		e.noteOn(0, 127);
		take.render(e, 0, take.samples());
		double lo = 1e9, hi = 0;
		for(size_t at = 2000; at + 4410 <= 44100; at += 4410)
		{
			const double f = frequencyOf(take.channel(0), at, at + 4410);
			lo = std::min(lo, f);
			hi = std::max(hi, f);
		}
		require(hi / lo > 2, "an LFO on PTCH: " + std::to_string(lo) + " to " + std::to_string(hi) + " Hz");

		// END at 127 and an LFO on it at full depth, on Track 16: the voice ends with its sample
		constexpr int Length = 4410;
		const auto dc = sampleOf(std::vector<float>(Length, 0.5f), 44100);
		Engine f(_library);
		vintageTrack(f, 15, dc);
		f.setParameter(15, vt::FilterSlot, 127);
		f.setOutput(15, 0);
		lfo.target = LfoTarget::Machine6;
		lfo.depth = 127;
		lfo.rate.hz = 50;
		f.setLfo(15, 0, lfo);
		Take end(Length + 2000);
		f.noteOn(15, 127);
		end.render(f, 0, end.samples());
		const auto& x = end.channel(0);
		float after = 0;
		for(size_t n = Length + Engine::HitDelay + 64; n < x.size(); ++n)
			after = std::max(after, std::abs(x[n]));
		require(std::abs(x[Length + Engine::HitDelay - 10]) > 0.1f && after < 1e-3f, "END past 127: the voice plays on past its"
			" sample (" + std::to_string(after) + ")");
		std::printf("PASS: an LFO on PTCH moves the tone from %.0f to %.0f Hz; END at 127 with an LFO on it ends with the sample\n",
			lo, hi);
	}

	void cost(const Library& _library)
	{
		std::vector<float> tone(96000);
		for(size_t n = 0; n < tone.size(); ++n)
			tone[n] = 0.5f * static_cast<float>(std::sin(static_cast<double>(n) * 0.05));
		const auto s = sampleOf(tone, 48000);
		std::vector<float> l(BlockSize), r(BlockSize);
		std::array<float*, Channels> outs{};
		outs[0] = l.data();
		outs[1] = r.data();
		constexpr int Blocks = 20000;
		const auto run = [&](const bool _vintage, const bool _drive)
		{
			Engine e(_library);
			ChainSettings chain = Bare;
			if(_drive)
				chain.drive[0] = {DriveType::Dac, true, 64, 127};
			for(int t = 0; t < Tracks; ++t)
			{
				e.setMachine(t, Sampler);
				e.setSample(t, &s);
				e.setChain(t, chain);
				e.setParameter(t, vt::GrainSlot, _vintage ? 127 : 0);
			}
			const auto start = Clock::now();
			for(int b = 0; b < Blocks; ++b)
			{
				if(b % 1024 == 0)
					for(int t = 0; t < Tracks; ++t)
						e.noteOn(t, 127);
				e.render(outs.data(), BlockSize);
			}
			return microseconds(Clock::now() - start) / Blocks;
		};
		const double rom = run(false, false), vintage = run(true, false), driven = run(true, true);
		std::printf("  16 Tracks on the Sampler, no chain, per 32-sample block (budget %.0f us): ROM player %.2f us, VINTAGE %.2f us,"
			" VINTAGE and a DAC drive %.2f us\n", BlockSize * 1e6 / SampleRate, rom, vintage, driven);
		std::printf("  a voice: VINTAGE %.3f us a block, the DAC drive %.3f us a Track a block\n", vintage / Tracks,
			(driven - vintage) / Tracks);
		std::printf("PASS: cost measured\n");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		const Library library;
		law();
		lowPass();
		hold(library);
		drive();
		oldSampler(library);
		allocations(library);
		kitSwitch(library);
		cuts(library);
		lfos(library);
		cost(library);
		return 0;
	}
	catch(const std::exception& _e)
	{
		std::printf("FAIL: %s\n", _e.what());
		return 1;
	}
}
