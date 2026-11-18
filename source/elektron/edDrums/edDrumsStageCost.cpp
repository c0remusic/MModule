// A measure, by hand, not a test: what the chain, the sample layer and the master cost alone on fixed input, and a hash
// of every sample they give, so that a change meant to make them cheaper shows both its cost and that it plays the same
// samples (the same hash).
//
//	edDrumsStageCost [RUNS] [SECONDS] [STAGE]
//		RUNS 7, each of SECONDS 2 of audio by default, the least run printed; STAGE 1 to 6 runs that one alone (for a
//		profiler)
//
// The stages, each on 16 Tracks at 44.1 kHz (the Syntakt family's chain at its 48), retriggered every 10 ms, staggered:
//	- the chain at its dearest, the worst kit's of ticket 26 without the amp: DIST before raised 4x, the Syntakt's
//	  drive after raised 4x, the three bands, the family's filter under its envelope, two LFOs a Track moving the
//	  filter's frequency and the low band every block; on both paths
//	- the sample layer playing a 48 kHz sample of noise at 44.1 kHz, plain, then with BR 64 and a decay, then a short
//	  one to its end, reversed and looped
//	- the master, compressing and driven
// and the load of the other processes during the runs. Input is noise from a fixed seed.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "edDrumsCostBench.h"
#include "edDrumsLayer.h"
#include "elektronTimingTest.h"

namespace
{
	using namespace elektron;
	using namespace elektron::drums;

	constexpr int Retrig = 441;		// 10 ms at 44.1 kHz

	struct Hash
	{
		uint64_t h = 14695981039346656037ull;
		void add(const float* _x, const size_t _count)
		{
			for(size_t i = 0; i < _count; ++i)
			{
				uint32_t u;
				std::memcpy(&u, _x + i, 4);
				for(int b = 0; b < 4; ++b)
				{
					h ^= u >> (8 * b) & 255;
					h *= 1099511628211ull;
				}
			}
		}
	};

	struct Noise
	{
		uint32_t s = 12345;
		float next()
		{
			s = s * 1664525u + 1013904223u;
			return static_cast<float>(static_cast<int32_t>(s)) / 2147483648.0f;
		}
	};

	// One run of a stage: the share of a core it took, and the hash of what it gave
	struct Run
	{
		double share;
		uint64_t hash;
	};

	ChainSettings dearest(const bool _syntakt)
	{
		ChainSettings s;
		s.drive[0] = {DriveType::MdDist, false, 80, 127};
		s.drive[1] = {DriveType::Syntakt, true, 80, 127};
		s.band.type = BandType::ThreeBand;
		s.band.values = {40, 80, 70, 50, 100, 70};
		s.filter.type = _syntakt ? FilterType::Syntakt : FilterType::Machinedrum;
		s.filter.mode = 0;
		s.filter.frequency = 90;
		s.filter.resonance = 60;
		s.filter.depth = 100;
		s.filter.envelope.decay = 60;
		return s;
	}

	Run chain(const bool _syntakt, const double _seconds)
	{
		const double rate = _syntakt ? 48000 : SampleRate;
		auto c = std::make_unique<Chain>(rate, _syntakt);
		c->setOversampling(4);
		for(int l = 0; l < Lanes; ++l)
			c->configure(l, dearest(_syntakt));
		Noise noise;
		Hash hash;
		const int blocks = static_cast<int>(_seconds * rate / BlockSize);
		const int retrig = Retrig / BlockSize;
		alignas(16) Block buf;
		std::vector<Block> input(64);
		for(auto& b : input)
			for(auto& s : b.s)
				for(float& x : s)
					x = 0.5f * noise.next();
		double t = 0;
		for(int b = 0; b < blocks; ++b)
		{
			buf = input[static_cast<size_t>(b) % input.size()];
			const auto t0 = bench::Clock::now();
			for(int l = 0; l < Lanes; ++l)
			{
				if((b + l) % retrig == 0)
					c->hit(l, 100);
				const int phase = (b * 7 + l * 13) % 64;
				c->modulate(l, VelocityTarget::Frequency, (phase - 32) * 64);
				c->modulate(l, VelocityTarget::BandA, (32 - phase) * 64);
			}
			c->process(buf, 0xffff);
			t += std::chrono::duration<double>(bench::Clock::now() - t0).count();
			hash.add(&buf.s[0][0], BlockSize * Lanes);
		}
		return {t / (blocks * BlockSize / rate), hash.h};
	}

	enum class Layer { Plain, Shaped, Edges };

	Run layer(const Layer _kind, const double _seconds)
	{
		// A second of noise at 48 kHz, read at 44.1 kHz: every phase of the kernel. Edges plays its first 300 samples,
		// which end before the next Hit, forward, reversed and looped both ways, a Track each in turn
		static const std::vector<int16_t> pcm = []
		{
			Noise noise;
			std::vector<int16_t> p(48000);
			for(auto& x : p)
				x = static_cast<int16_t>(noise.next() * 30000);
			return p;
		}();
		auto layers = std::make_unique<SampleLayers>(SampleRate);
		LayerSettings s;
		s.on = true;
		if(_kind == Layer::Shaped)
		{
			s.bitReduction = 64;
			s.hold = 20;
			s.decay = 60;
		}
		const bool edges = _kind == Layer::Edges;
		for(int t = 0; t < Lanes; ++t)
		{
			if(edges)
			{
				s.play = 32 * (t % 4);	// Forward, Reverse, ForwardLoop, ReverseLoop
				s.loop = 40;
			}
			layers->setSample(t, {pcm.data(), static_cast<uint32_t>(edges ? 300 : pcm.size()), 48000.0});
			layers->setSettings(t, s);
			layers->setNote(t, 60 + t % 5);
		}
		Hash hash;
		const int blocks = static_cast<int>(_seconds * SampleRate / BlockSize);
		alignas(16) float x[Lanes][BlockSize];
		double t = 0;
		for(int b = 0; b < blocks; ++b)
		{
			std::memset(x, 0, sizeof(x));
			const auto t0 = bench::Clock::now();
			for(int l = 0; l < Lanes; ++l)
			{
				const int at = (b * BlockSize + l * 23) % Retrig;
				if(at < BlockSize)
					layers->hit(l, at);
				layers->render(l, x[l]);
			}
			t += std::chrono::duration<double>(bench::Clock::now() - t0).count();
			hash.add(&x[0][0], BlockSize * Lanes);
		}
		return {t / (blocks * BlockSize / SampleRate), hash.h};
	}

	Run master(const double _seconds)
	{
		auto m = std::make_unique<Master>(SampleRate);
		m->set(MasterThreshold, 0);
		m->set(MasterRatio, 100);
		m->set(MasterMix, 127);
		m->set(MasterDrive, 127);
		m->settle();
		Noise noise;
		Hash hash;
		const int blocks = static_cast<int>(_seconds * SampleRate / BlockSize);
		std::vector<float> input(64 * BlockSize * 2);
		for(float& x : input)
			x = 0.8f * noise.next();
		alignas(16) float left[BlockSize], right[BlockSize];
		double t = 0;
		for(int b = 0; b < blocks; ++b)
		{
			const float* in = input.data() + static_cast<size_t>(b % 64) * 2 * BlockSize;
			std::copy_n(in, BlockSize, left);
			std::copy_n(in + BlockSize, BlockSize, right);
			// A setting moved now and then, so that the ramps run
			if(b % 50 == 0)
				m->set(MasterVolume, static_cast<uint8_t>(100 + b / 50 % 27));
			const auto t0 = bench::Clock::now();
			m->block();
			m->process(left, right, BlockSize);
			t += std::chrono::duration<double>(bench::Clock::now() - t0).count();
			hash.add(left, BlockSize);
			hash.add(right, BlockSize);
		}
		return {t / (blocks * BlockSize / SampleRate), hash.h};
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	elektron::raiseTimingTestPriority();
	const syntakt::fast::FlushDenormals ftz;
	const int runs = _argc > 1 ? std::atoi(_argv[1]) : 7;
	const double seconds = _argc > 2 ? std::atof(_argv[2]) : 2;
	const int only = _argc > 3 ? std::atoi(_argv[3]) : 0;

	struct Stage
	{
		const char* name;
		Run (*run)(double);
	};
	const Stage stages[] = {
		{"chain, Machinedrum path (44.1 kHz)", [](const double _s) { return chain(false, _s); }},
		{"chain, Syntakt path (48 kHz)", [](const double _s) { return chain(true, _s); }},
		{"sample layer, plain", [](const double _s) { return layer(Layer::Plain, _s); }},
		{"sample layer, BR and decay", [](const double _s) { return layer(Layer::Shaped, _s); }},
		{"sample layer, ends, reversed, looped", [](const double _s) { return layer(Layer::Edges, _s); }},
		{"master", [](const double _s) { return master(_s); }},
	};
	const bench::Load load;
	bool same = true;
	for(int k = 0; k < static_cast<int>(std::size(stages)); ++k)
	{
		const auto& stage = stages[k];
		if(only && only != k + 1)
			continue;
		double least = 1e9;
		uint64_t hash = 0;
		for(int r = 0; r < runs; ++r)
		{
			const Run run = stage.run(seconds);
			least = std::min(least, run.share);
			if(r && run.hash != hash)
				same = false;
			hash = run.hash;
		}
		std::printf("%-36s %6.3f %% of a core   hash %016llx\n", stage.name, 100 * least,
			static_cast<unsigned long long>(hash));
	}
	std::printf("other processes: %.2f cores busy on average\n", load.otherCores());
	if(!same)
	{
		std::printf("a stage gave different samples from one run to the next\n");
		return 1;
	}
	return 0;
}
