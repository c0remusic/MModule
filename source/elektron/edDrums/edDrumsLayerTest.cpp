// The sample layer (ticket 06 of .scratch/elektron-drums-v2, edDrumsLayer.h), without the flash image or vectors:
//   - off, a Hit sounds nothing of it and the Track's samples are those without a layer;
//   - its Hit at HitDelay on both paths (the Sampler without a sample, silent, on its 12-BIT grain and on CLEAN), its
//     offset 100 ms after, its pitch and length, the Track's note followed or not; at HitDelay on a machine of each
//     family too, the Track with its layer less the Track without;
//   - a Hit over a sounding layer crossfades, no step larger than the sine's own;
//   - BR: the bits a ramp keeps within 0.5 bit of the Analog Rytm's law; BR 0 the ramp whole;
//   - the envelope: the attack's half-way point and the decay's -60 dB where their laws put them;
//   - Q15: the same Hits at 44.1 and 48 kHz, 10 ms envelopes within 0.2 dB, third octaves within 0.5 dB;
//   - the cost of 16 Tracks' layers playing at 44.1 kHz, 1.5 % of a core at most (spec v2, §3).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "elektronTimingTest.h"
#include "edDrumsEngine.h"
#include "edDrumsLayer.h"
#include "edDrumsTestSupport.h"
#include "stFastMetrics.h"

namespace
{
	using namespace elektron::drums;
	using namespace elektron::drums::test;
	using elektron::BlockSize;
	namespace bench = syntakt::fast::bench;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	Sample made(const std::vector<float>& _x, const double _rate)
	{
		auto m = makeSample("layer", _x, _rate);
		require(std::holds_alternative<Sample>(m), "makeSample refuses the test's sample");
		return std::get<Sample>(std::move(m));
	}

	Sample sine(const double _hz, const double _rate, const int _length)
	{
		std::vector<float> x(static_cast<size_t>(_length) + 1, 0.0f);
		for(int n = 0; n < _length; ++n)
			x[static_cast<size_t>(n)] = static_cast<float>(0.5 * std::sin(2 * bench::Pi * _hz * n / _rate));
		return made(x, _rate);
	}

	struct Measure
	{
		int onset = -1, end = -1;
		double hz = 0;
	};

	// Its first and last samples above 1 % of its peak, its frequency from its rising zero crossings in between
	Measure measure(const std::vector<float>& _y)
	{
		Measure m;
		float peak = 0;
		for(const float v : _y)
			peak = std::max(peak, std::abs(v));
		for(int n = 0; n < static_cast<int>(_y.size()); ++n)
			if(std::abs(_y[static_cast<size_t>(n)]) >= 0.01f * peak && peak > 0)
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

	enum class Path { Machinedrum, Syntakt };

	// Track 0 on the Sampler without a sample (silence), on the 12-BIT grain or CLEAN, its layer _layer playing _sample;
	// Hits at _notes, Main rendered
	std::vector<float> layerHits(const Library& _library, const Path _path, const LayerSettings& _layer,
		const Sample* _sample, const std::vector<int>& _notes, const int _samples, const int _note = 60)
	{
		auto e = std::make_unique<Engine>(_library);
		e->setMachine(0, Sampler);
		e->setLevel(0, 127);
		e->setChain(0, ChainSettings{});
		if(_path == Path::Syntakt)
			e->setParameter(0, vintage::GrainSlot, vintage::GrainValues[2]);
		e->setNote(0, _note);
		e->setLayerSample(0, _sample);
		e->setLayer(0, _layer);
		Take take(_samples);
		int at = 0;
		for(const int note : _notes)
		{
			take.render(*e, at, note - at);
			e->noteOn(0, 127);
			at = note;
		}
		take.render(*e, at, _samples - at);
		return take.channel(0);
	}

	const char* nameOf(const Path _path) { return _path == Path::Machinedrum ? "12-BIT grain" : "CLEAN grain"; }

	void offIsSilent(const Library& _library)
	{
		SampleLayers layers(SampleRate);
		const auto a = sine(1000, 44100, 4410);
		layers.setSample(0, {a.source->data(), static_cast<uint32_t>(a.source->size()), a.rate});
		layers.hit(0, 0);
		require(!layers.sounding(0), "a layer off sounds");

		// A Track with a sample of its own on the Sampler: its layer's sample given, the layer off, the same samples
		const auto own = sine(440, 44100, 8000);
		for(const Path path : {Path::Machinedrum, Path::Syntakt})
		{
			const auto render = [&](const Sample* _layer)
			{
				auto e = std::make_unique<Engine>(_library);
				e->setMachine(0, Sampler);
				e->setLevel(0, 127);
				if(path == Path::Syntakt)
					e->setParameter(0, vintage::GrainSlot, vintage::GrainValues[2]);
				e->setSample(0, &own);
				e->setLayerSample(0, _layer);
				Take take(12000);
				take.render(*e, 0, 1000);
				e->noteOn(0, 127);
				take.render(*e, 1000, 11000);
				return take.channel(0);
			};
			require(render(nullptr) == render(&a), std::string("a layer off changes the Track's samples on the ") + nameOf(path));
		}
		std::printf("  off: no Hit, and the Track's samples unchanged on both paths\n");
	}

	void hits(const Library& _library)
	{
		constexpr int Note = 1000, Length = 22050;
		const auto a = sine(1000, 44100, Length);
		LayerSettings on;
		on.on = true;
		on.level = 127;
		for(const Path path : {Path::Machinedrum, Path::Syntakt})
		{
			const auto y = layerHits(_library, path, on, &a, {Note}, Note + Engine::HitDelay + Length + 6000);
			const auto m = measure(y);
			std::printf("  %s: onset %d after its note, %d samples long, %.2f Hz for 1000\n", nameOf(path), m.onset - Note,
				m.end - m.onset + 1, m.hz);
			require(m.onset - Note >= Engine::HitDelay && m.onset - Note <= Engine::HitDelay + 8,
				std::string("the layer's Hit is not at HitDelay on the ") + nameOf(path));
			require(std::abs(m.end - m.onset + 1 - Length) <= 96, std::string("the layer plays another length on the ") + nameOf(path));
			require(std::abs(m.hz - 1000) < 1, std::string("the layer plays another pitch on the ") + nameOf(path));

			// At every place of the note in a block: the same onset, no jitter
			const auto shortSine = sine(1000, 44100, 2000);
			int earliest = 1 << 30, latest = -1;
			for(int k = 0; k < BlockSize; ++k)
			{
				const int onset = measure(layerHits(_library, path, on, &shortSine, {Note + k},
					Note + k + Engine::HitDelay + 4000)).onset - (Note + k);
				earliest = std::min(earliest, onset);
				latest = std::max(latest, onset);
			}
			std::printf("    the note at each of a block's 32 samples: onset %d to %d\n", earliest, latest);
			require(earliest == latest, std::string("the layer's onset jitters on the ") + nameOf(path));

			// 100 ms of offset
			LayerSettings late = on;
			late.offset = 127;
			const auto z = measure(layerHits(_library, path, late, &a, {Note}, Note + Engine::HitDelay + Length + 12000));
			std::printf("    offset 127: %d samples later\n", z.onset - m.onset);
			require(std::abs(z.onset - m.onset - 4410) <= 1, std::string("the offset is not 100 ms on the ") + nameOf(path));

			// The Track's note an octave up, followed or not
			const auto up = measure(layerHits(_library, path, on, &a, {Note}, Note + Engine::HitDelay + Length + 6000, 72));
			LayerSettings fixed = on;
			fixed.noteTrack = false;
			const auto still = measure(layerHits(_library, path, fixed, &a, {Note}, Note + Engine::HitDelay + Length + 6000, 72));
			std::printf("    note 72: %.2f Hz followed, %.2f Hz not\n", up.hz, still.hz);
			require(std::abs(up.hz - 2000) < 2 && std::abs(still.hz - 1000) < 1,
				std::string("the layer follows the note otherwise on the ") + nameOf(path));

			// A Hit over the sounding layer, at another phase and at every place of its note in a block: the old voice fades
			// over a block as the new one starts, no step larger than a Hit's own onset (the player's half-band) plus the
			// old voice's own there (the sine's 2 pi 1000 / 44100 and the fade's 1 / BlockSize of the peak); a cut, or the
			// old voice going on 24 samples off its place, steps by 1 to 1.6 of the peak
			const auto largestStep = [](const std::vector<float>& _y)
			{
				float step = 0;
				for(size_t n = 1; n < _y.size(); ++n)
					step = std::max(step, std::abs(_y[n] - _y[n - 1]));
				return step;
			};
			float peak = 0;
			for(const float v : y)
				peak = std::max(peak, std::abs(v));
			const float own = largestStep(y);
			float worst = 0;
			for(int gap = 2000; gap < 2000 + BlockSize; ++gap)
				worst = std::max(worst, largestStep(layerHits(_library, path, on, &a, {Note, Note + gap},
					Note + gap + Engine::HitDelay + Length + 2000)));
			const auto bound = own + static_cast<float>(2 * bench::Pi * 1000 / 44100 + 1.0 / BlockSize) * peak;
			std::printf("    retrig: largest step %.3f of the peak (a Hit's own %.3f, bound %.3f)\n", worst / peak, own / peak,
				bound / peak);
			require(worst <= bound, std::string("a retrig clicks on the ") + nameOf(path));
		}
	}

	// One layer of a SampleLayers on its own, _blocks rendered after a Hit at once
	std::vector<double> alone(const double _rate, const Sample& _sample, const LayerSettings& _settings, const int _blocks)
	{
		SampleLayers layers(_rate);
		layers.setSample(0, {_sample.source->data(), static_cast<uint32_t>(_sample.source->size()), _sample.rate});
		layers.setSettings(0, _settings);
		layers.hit(0, 0);
		const syntakt::fast::FlushDenormals ftz;
		std::vector<double> y;
		for(int b = 0; b < _blocks; ++b)
		{
			float x[BlockSize]{};
			layers.render(0, x);
			y.insert(y.end(), x, x + BlockSize);
		}
		return y;
	}

	// On a machine of each family: the Track with its layer less the Track without, through a bare chain, is the layer,
	// which sounds at HitDelay as on the Sampler
	void families(const Library& _library)
	{
		constexpr int Note = 1000, Samples = 8000;
		const auto a = sine(1000, 44100, 4000);
		const std::vector<std::pair<const char*, Machine>> machines{{"Syntakt machine 0", {Family::Syntakt, 0}},
			{"Machinedrum TRX-BD", {Family::Machinedrum, 16}}, {"OPEN DRUM", {Family::OpenDrum, 1}},
			{"SOPHIE", {Family::Sophie, 0}}, {"Syntakt analog", {Family::SyntaktAnalog, 0}},
			{"Rytm analog", {Family::RytmAnalog, 0}}};
		for(const auto& [name, machine] : machines)
		{
			const auto render = [&](const bool _layer)
			{
				auto e = std::make_unique<Engine>(_library);
				e->setMachine(0, machine);
				e->setLevel(0, 127);
				e->setChain(0, ChainSettings{});
				e->setLayerSample(0, &a);
				LayerSettings on;
				on.on = _layer;
				on.level = 127;
				e->setLayer(0, on);
				Take take(Samples);
				take.render(*e, 0, Note);
				e->noteOn(0, 127);
				take.render(*e, Note, Samples - Note);
				return take.channel(0);
			};
			const auto with = render(true), without = render(false);
			std::vector<float> layer(with.size());
			for(size_t n = 0; n < with.size(); ++n)
				layer[n] = with[n] - without[n];
			const auto m = measure(layer);
			std::printf("  %s: the layer's onset %d after its note\n", name, m.onset - Note);
			require(m.onset - Note >= Engine::HitDelay && m.onset - Note <= Engine::HitDelay + 8,
				std::string("the layer's Hit is not at HitDelay on ") + name);
		}
	}

	// A voice placed anywhere in its block ends with its sample, its delay line given out: the Track is let go
	void ends()
	{
		const auto a = sine(1000, 44100, 300);
		LayerSettings on;
		on.on = true;
		for(int startIn = 0; startIn < 2 * BlockSize; ++startIn)
		{
			SampleLayers layers(SampleRate);
			layers.setSample(0, {a.source->data(), static_cast<uint32_t>(a.source->size()), a.rate});
			layers.setSettings(0, on);
			layers.hit(0, startIn);
			float x[BlockSize];
			for(int b = 0; b < 20 && layers.sounding(0); ++b)
				layers.render(0, x);
			require(!layers.sounding(0), "a layer started " + std::to_string(startIn) + " samples in never ends");
		}
		std::printf("  a 300-sample layer ends within 20 blocks wherever it starts\n");
	}

	void bitReduction()
	{
		// A ramp over the whole 16-bit range, a value a sample: the steps BR leaves, from the levels it keeps
		std::vector<float> ramp(65536);
		for(size_t i = 0; i < ramp.size(); ++i)
			ramp[i] = (static_cast<float>(i) - 32768.0f) / 32768.0f;
		const auto s = made(ramp, 44100);
		LayerSettings on;
		on.on = true;
		on.level = 127;
		const auto whole = alone(44100, s, on, 2048);
		for(const int br : {1, 16, 32, 64, 100})
		{
			LayerSettings r = on;
			r.bitReduction = br;
			const auto y = alone(44100, s, r, 2048);
			std::set<double> levels;
			double lo = 1, hi = -1;
			for(size_t n = 200; n < 65000; ++n)
			{
				levels.insert(y[n]);
				lo = std::min(lo, y[n]);
				hi = std::max(hi, y[n]);
			}
			const double step = (hi - lo) / static_cast<double>(levels.size() - 1) * 32768;
			const double bits = 16 - std::log2(step), law = 16 - (6.06 + 0.0729 * br);
			std::printf("  BR %3d: %.2f bits kept (the law's %.2f)\n", br, bits, law);
			require(std::abs(bits - law) <= 0.5, "BR keeps another number of bits");
		}
		// BR 0: the ramp whole, a level a sample
		std::set<double> levels(whole.begin() + 200, whole.begin() + 65000);
		std::printf("  BR   0: %zu levels over 64800 samples\n", levels.size());
		require(levels.size() > 60000, "BR 0 reduces the ramp");
	}

	void envelope()
	{
		// A constant at half scale for 3 s: the attack's half-way point at half its time, the decay's -60 dB at its time
		const auto dc = made(std::vector<float>(132300, 0.5f), 44100);
		LayerSettings a;
		a.on = true;
		a.level = 127;
		a.attack = 64;
		const auto y = alone(44100, dc, a, 3000);
		const double attack = SampleLayers::attackSeconds(64);
		size_t half = 0;
		while(half < y.size() && y[half] < 0.25)
			++half;
		std::printf("  attack 64: half-way at %.1f ms (%.1f ms of attack)\n", 1000 * half / 44100.0, 1000 * attack);
		require(std::abs(half / 44100.0 - attack / 2) < 0.002, "the attack reaches half-way elsewhere");

		LayerSettings d;
		d.on = true;
		d.level = 127;
		d.hold = 32;
		d.decay = 40;
		const auto z = alone(44100, dc, d, 3000);
		const double hold = vintage::holdSeconds(32), decay = vintage::decaySeconds(40);
		const auto at = static_cast<size_t>((hold + decay) * 44100);
		const double level = 20 * std::log10(std::abs(z[at]) / 0.5);
		std::printf("  hold 32, decay 40: %.1f dB at %.0f ms\n", level, 1000 * (hold + decay));
		require(std::abs(z[static_cast<size_t>(hold * 44100) - 10] - 0.5) < 0.01, "the hold does not hold");
		require(std::abs(level + 60) < 1, "the decay reaches -60 dB elsewhere");
	}

	void q15()
	{
		// A Hit: a decaying mix of a falling sine and noise, at 44.1 kHz, through each path's layer
		constexpr int Length = 8820;
		std::vector<float> x(Length);
		uint32_t seed = 1;
		double phase = 0;
		for(int n = 0; n < Length; ++n)
		{
			seed = seed * 1664525u + 1013904223u;
			const double noise = (static_cast<double>(seed >> 8) / (1 << 24) - 0.5);
			phase += 2 * bench::Pi * (60 + 200 * std::exp(-n / 400.0)) / 44100;
			x[static_cast<size_t>(n)] = static_cast<float>(std::exp(-n / 2000.0) * (0.6 * std::sin(phase) + 0.3 * noise));
		}
		const auto s = made(x, 44100);
		double worstEnv = 0, worstBand = 0;
		for(const int tune : {56, 64, 71})
		{
			LayerSettings on;
			on.on = true;
			on.level = 127;
			on.tune = tune;
			auto a = alone(44100, s, on, 160), b = alone(48000, s, on, 160);
			a.erase(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(bench::onsetOf(a)));
			b.erase(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(bench::onsetOf(b)));
			constexpr double Seconds = 0.08;
			a.resize(static_cast<size_t>(Seconds * 44100));
			b.resize(static_cast<size_t>(Seconds * 48000));
			worstEnv = std::max(worstEnv, bench::envelopeDiff(bench::envelope(b, 48000, 10), bench::envelope(a, 44100, 10)));
			const auto ra = bench::bands(b, 48000, -17, 12), rb = bench::bands(a, 44100, -17, 12);
			const double loud = *std::max_element(ra.begin(), ra.end());
			for(size_t k = 0; k < ra.size(); ++k)
				if(ra[k] >= loud * 1e-6)
					worstBand = std::max(worstBand, std::abs(bench::db(rb[k] / ra[k])));
		}
		std::printf("  Q15: 44.1 against 48 kHz: 10 ms envelope within %.3f dB, third octaves within %.3f dB\n", worstEnv,
			worstBand);
		require(worstEnv <= 0.2, "Q15: the envelope at 44.1 kHz is off by more than 0.2 dB");
		require(worstBand <= 0.5, "Q15: a third octave at 44.1 kHz is off by more than 0.5 dB");
	}

	void cost()
	{
		std::vector<float> x(48000);
		for(size_t i = 0; i < x.size(); ++i)
			x[i] = static_cast<float>(0.6 * std::sin(i * 0.05) * std::sin(i * 0.0011));
		const auto s = made(x, 44100);
		auto layers = std::make_unique<SampleLayers>(SampleRate);
		for(int t = 0; t < Tracks; ++t)
		{
			LayerSettings on;
			on.on = true;
			on.play = Dt2Player::ForwardLoop * 32;
			on.tune = 60 + t / 2;
			on.bitReduction = t;
			on.decay = 127;
			layers->setSample(t, {s.source->data(), static_cast<uint32_t>(s.source->size()), s.rate});
			layers->setSettings(t, on);
			layers->hit(t, t);
		}
		const syntakt::fast::FlushDenormals ftz;
		float out[BlockSize]{};
		constexpr int Blocks = 3000;
		double best = 1e9;
		for(int run = 0; run < 5; ++run)
		{
			const auto t0 = std::chrono::steady_clock::now();
			for(int b = 0; b < Blocks; ++b)
				for(int t = 0; t < Tracks; ++t)
					layers->render(t, out);
			best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
		}
		require(layers->soundingTracks() == 0xffff, "a looping layer stopped");
		const double share = 100 * best / (Blocks * BlockSize / SampleRate);
		std::printf("  cost: 16 layers playing, %.3f %% of a core at 44.1 kHz (%.2f us a block)\n", share, 1e6 * best / Blocks);
		require(share <= 1.5, "16 layers cost more than 1.5 % of a core");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	elektron::raiseTimingTestPriority();
	try
	{
		const Library none;
		offIsSilent(none);
		hits(none);
		families(none);
		ends();
		bitReduction();
		envelope();
		q15();
		cost();
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
