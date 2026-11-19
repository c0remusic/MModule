// A Lock of the Sampler's sample (ticket 07 of .scratch/elektron-drums-v2, Engine::lockSample), without the flash
// image:
//   - on each grain (12-BIT, VINTAGE, CLEAN), four Hits each locked to its own sample and a fifth without a Lock play
//     the samples a host setting the Track's sample before each Hit plays, sample for sample: each its sample, from its
//     Hit; the fifth the Track's own again; each locked Hit differs from the same Hit on the Track's own sample;
//     the next Hit after a Lock on its own sample again: the host sets it back;
//   - nothing allocated while they play;
//   - the cost of 16 Tracks' Hits in one block, each on a sample of the 12-BIT grain's whole length, the worst block.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "elektronTimingTest.h"
#include "edDrumsEngine.h"
#include "edDrumsTestSupport.h"
#include "edDrumsVintage.h"
#include "mdDrumsAllocationCounter.h"
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

	Sample sine(const double _hz, const int _length)
	{
		std::vector<float> x(static_cast<size_t>(_length));
		for(int n = 0; n < _length; ++n)
			x[static_cast<size_t>(n)] = static_cast<float>(0.5 * std::sin(2 * bench::Pi * _hz * n / 44100.0)
				* (1.0 - static_cast<double>(n) / _length));
		auto m = makeSample("sine", x, 44100.0);
		require(std::holds_alternative<Sample>(m), "makeSample refuses the test's sample");
		return std::get<Sample>(std::move(m));
	}

	constexpr int Length = 30000;
	const std::vector<int> Notes{1000, 6017, 11500, 17003, 23000};
	const char* GrainNames[]{"12-BIT", "VINTAGE", "CLEAN"};

	enum class Way { Lock, Host, Own };

	// Track 0 on the Sampler on _grain, its own sample _own; Hit k at Notes[k] on _locked[k] (nullptr: none), as _way
	// gives it: a Lock, the host setting the Track's sample before the Hit, or nothing (the Track's own throughout)
	std::vector<float> play(const Library& _library, const int _grain, const Sample* _own,
		const std::vector<const Sample*>& _locked, const Way _way, size_t* _allocations = nullptr)
	{
		auto e = std::make_unique<Engine>(_library);
		e->setMachine(0, Sampler);
		e->setLevel(0, 127);
		e->setChain(0, ChainSettings{});
		e->setParameter(0, vintage::GrainSlot, vintage::GrainValues[static_cast<size_t>(_grain)]);
		e->setSample(0, _own);
		Take take(Length);
		int at = 0;
		const auto run = [&]
		{
			for(size_t k = 0; k < Notes.size(); ++k)
			{
				take.render(*e, at, Notes[k] - at);
				at = Notes[k];
				const Sample* s = _locked[k];
				if(_way == Way::Lock && s)
					e->lockSample(0, s);
				else if(_way == Way::Host)
					e->setSample(0, s ? s : _own);
				e->noteOn(0, 127);
			}
			take.render(*e, at, Length - at);
		};
		if(_allocations)
			*_allocations = countAllocations(run);
		else
			run();
		return take.channel(0);
	}

	void locks(const Library& _library)
	{
		const Sample own = sine(220, 4000);
		const std::vector<Sample> samples{sine(440, 4000), sine(660, 3000), sine(880, 5000), sine(1320, 4500)};
		const std::vector<const Sample*> locked{&samples[0], &samples[1], &samples[2], &samples[3], nullptr};
		// A Track with a sample of its own, and one without (silence between the Locks)
		for(const Sample* ownSample : {&own, static_cast<const Sample*>(nullptr)})
		for(int grain = 0; grain < 3; ++grain)
		{
			size_t allocations = 0;
			const auto lock = play(_library, grain, ownSample, locked, Way::Lock, &allocations);
			const auto host = play(_library, grain, ownSample, locked, Way::Host);
			const auto plain = play(_library, grain, ownSample, locked, Way::Own);
			require(lock == host, std::string(GrainNames[grain]) + ": a Lock plays other samples than the host's");
			// Each locked Hit's stretch differs from the same Hit on the Track's own sample (the fifth, unlocked, is
			// the host's own sample above)
			for(size_t k = 0; k < Notes.size(); ++k)
			{
				if(!locked[k])
					continue;
				const auto from = static_cast<std::ptrdiff_t>(Notes[k]);
				const auto to = static_cast<std::ptrdiff_t>(Notes[k + 1]);
				require(!std::equal(lock.begin() + from, lock.begin() + to, plain.begin() + from),
					std::string(GrainNames[grain]) + ": Hit " + std::to_string(k) + " plays the Track's own sample");
			}
			require(allocations == 0, std::string(GrainNames[grain]) + ": a Lock allocates");
			std::printf("  %s: four Locks play the host's samples sample for sample, the fifth Hit the Track's own, "
				"nothing allocated\n", GrainNames[grain]);
		}
	}

	void cost(const Library& _library)
	{
		// 16 Tracks on the 12-BIT grain, a Hit each in one block, each on its own sample of the grain's whole length;
		// the next round of Hits on other samples, so that every Hit copies its sample's words
		std::vector<Sample> samples;
		for(int i = 0; i < 2 * Tracks; ++i)
			samples.push_back(sine(200 + 37 * i, static_cast<int>(Sample::MaxLength)));
		auto e = std::make_unique<Engine>(_library);
		for(int t = 0; t < Tracks; ++t)
		{
			e->setMachine(t, Sampler);
			e->setChain(t, ChainSettings{});
		}
		Take take(BlockSize);
		const syntakt::fast::FlushDenormals ftz;
		double worst = 0;
		for(int round = 0; round < 40; ++round)
		{
			const auto t0 = std::chrono::steady_clock::now();
			for(int t = 0; t < Tracks; ++t)
			{
				e->lockSample(t, &samples[static_cast<size_t>(t + Tracks * (round % 2))]);
				e->noteOn(t, 100);
			}
			take.render(*e, 0, BlockSize);
			worst = std::max(worst, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0)
				.count());
			for(int b = 0; b < 4; ++b)
				take.render(*e, 0, BlockSize);
		}
		std::printf("  16 locked Hits of %u-sample samples in one block: %.1f us at worst (a block of 32 lasts %.0f "
			"us)\n", Sample::MaxLength, worst, BlockSize / SampleRate * 1e6);
		require(worst < 725, "16 locked Hits take longer than a block of 32");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	elektron::raiseTimingTestPriority();
	try
	{
		const Library none;
		locks(none);
		cost(none);
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
