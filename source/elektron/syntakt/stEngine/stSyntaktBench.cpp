// syntakt::Syntakt's cost on one core, by hand (not a ctest test): a busy pattern on all eight tracks for 64 s of audio
// at 120 BPM, a trig on every track every 1/16, the twelve ported machines in turn (a track changes machine every two
// bars), both LFOs of every track running, the delay's and the reverb's sends up, a knob moved every block, a lock
// every fourth step. It times every 32-sample block by stage with std::chrono::steady_clock and reports the mean, the
// 99th percentile and the worst as a share of the block's period (667 us at 48 kHz), the worst blocks and what they
// do, the module's memory and the machine's load.
//
// SYNTAKT_SECTION3, SYNTAKT_SECTION7 and SYNTAKT_SAMPLES as stSyntaktTest reads them; exit 77 without them.
//
//     stSyntaktBench [--raw OUT.bin] [--seconds N] [--tracks 8|16] [--busy N]
//
// --raw also writes each block's outputs (14, or 26 with 16 tracks; 32 int32 each). --tracks 16 makes the module
// two machines with one delay and one reverb, and the pattern runs on the 16 tracks, the second eight's kit as the
// first's but their machines; --busy N plays it on the first N tracks alone, the others idle.

#ifdef _WIN32
#	define NOMINMAX
#	include <windows.h>
#	include <psapi.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "stEngine.h"
#include "stMainMemory.h"
#include "stSection7.h"
#include "stSyntakt.h"
#include "stVoice.h"

namespace
{
	using syntakt::Syntakt;
	using Clock = std::chrono::steady_clock;

	constexpr int Block = syntakt::BlockSize;
	constexpr double Period = static_cast<double>(Block) / Syntakt::SampleRate;	// 667 us
	constexpr int StepSamples = Syntakt::SampleRate / 8;							// 1/16 at 120 BPM

	bool readSamples(const std::string& _path, syntakt::Samples& _samples)
	{
		const std::vector<uint8_t> blob = syntakt::readFile(_path);
		constexpr size_t header = 4 + 8 * 64 + 8;
		if(blob.size() < header || std::string(blob.begin(), blob.begin() + 4) != "STS1")
			return false;
		for(size_t i = 0; i < _samples.directory.size(); ++i)
			_samples.directory[i] = {syntakt::be32(&blob[4 + 8 * i]), syntakt::be32(&blob[8 + 8 * i])};
		_samples.base = syntakt::be32(&blob[4 + 8 * 64]);
		const uint32_t size = syntakt::be32(&blob[8 + 8 * 64]);
		if(blob.size() != header + size)
			return false;
		_samples.data.assign(blob.begin() + header, blob.end());
		return true;
	}

	// The process's private bytes
	double privateBytes()
	{
#ifdef _WIN32
		PROCESS_MEMORY_COUNTERS_EX c{};
		c.cb = sizeof c;
		if(K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c), sizeof c))
			return static_cast<double>(c.PrivateUsage);
#endif
		return 0;
	}

	// The machine's busy time and the process's CPU time, in seconds, and the number of logical processors
	struct Load
	{
		double busy = 0, own = 0;
	};

	Load load()
	{
		Load l;
#ifdef _WIN32
		auto seconds = [](const FILETIME& _t)
		{
			return static_cast<double>(static_cast<uint64_t>(_t.dwHighDateTime) << 32 | _t.dwLowDateTime) * 1e-7;
		};
		FILETIME idle, kernel, user, created, exited, ownKernel, ownUser;
		if(GetSystemTimes(&idle, &kernel, &user))
			l.busy = seconds(kernel) + seconds(user) - seconds(idle);
		if(GetProcessTimes(GetCurrentProcess(), &created, &exited, &ownKernel, &ownUser))
			l.own = seconds(ownKernel) + seconds(ownUser);
#endif
		return l;
	}

	int processors()
	{
#ifdef _WIN32
		SYSTEM_INFO info;
		GetSystemInfo(&info);
		return static_cast<int>(info.dwNumberOfProcessors);
#else
		return 1;
#endif
	}

	// What a block's calls do, for the worst blocks' report
	struct BlockLog
	{
		double control = 0, build = 0, mix = 0, voices = 0, total = 0;
		int trigs = 0;
		int machines = 0;		// tracks that changed machine
		int firstHits = 0;		// tracks whose trig is their machine's first since it changed
		bool delayTime = false;	// the delay's time moved
	};

	struct Stats
	{
		double mean, p99, worst;
	};

	Stats stats(std::vector<double> _v)
	{
		double sum = 0;
		for(const double x : _v)
			sum += x;
		std::sort(_v.begin(), _v.end());
		return {sum / static_cast<double>(_v.size()), _v[_v.size() * 99 / 100], _v.back()};
	}

	void print(const char* _name, const Stats& _s)
	{
		std::printf("%-22s mean %7.1f us %5.1f %%   p99 %7.1f us %5.1f %%   worst %8.1f us %6.1f %%\n", _name,
			_s.mean * 1e6, 100 * _s.mean / Period, _s.p99 * 1e6, 100 * _s.p99 / Period, _s.worst * 1e6,
			100 * _s.worst / Period);
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);

	std::string rawPath;
	double seconds = 64;
	int tracks = Syntakt::Tracks, busy = -1;
	for(int i = 1; i + 1 < _argc; i += 2)
	{
		const std::string option = _argv[i];
		if(option == "--raw")
			rawPath = _argv[i + 1];
		else if(option == "--seconds")
			seconds = std::atof(_argv[i + 1]);
		else if(option == "--tracks")
			tracks = std::atoi(_argv[i + 1]);
		else if(option == "--busy")
			busy = std::atoi(_argv[i + 1]);
	}

	const char* section3Path = std::getenv("SYNTAKT_SECTION3");
	const char* section7Path = std::getenv("SYNTAKT_SECTION7");
	const char* samplesPath = std::getenv("SYNTAKT_SAMPLES");
	if(!section3Path || !*section3Path || !section7Path || !*section7Path || !samplesPath || !*samplesPath)
	{
		std::printf("SKIP: set SYNTAKT_SECTION3, SYNTAKT_SECTION7 and SYNTAKT_SAMPLES\n");
		return 77;
	}
	const auto section3 = syntakt::main::loadSection3(section3Path);
	const auto section7 = syntakt::loadSection7(section7Path);
	syntakt::Samples samples;
	if(section3.bytes.empty() || section7.empty() || !readSamples(samplesPath, samples))
	{
		std::printf("FAIL: cannot read OS 1.41's sections 3 and 7 or the samples\n");
		return 1;
	}

	// The machine of each ported engine: the first machine id the voice CPU's map sends to it
	const syntakt::Rom rom{section7.data()};
	std::vector<int> machines;
	for(uint32_t e = 0; e < 12; ++e)
		for(int m = 0; m <= 44; ++m)
			if(syntakt::engineOf(rom, m) == static_cast<int32_t>(e) && syntakt::engine(e))
			{
				machines.push_back(m);
				break;
			}

	// The machine's load before the run, over one second
	const Load idle0 = load();
	const auto idleStart = Clock::now();
	while(Clock::now() - idleStart < std::chrono::seconds(1))
	{
	}
	const Load idle1 = load();
	const double idleWall = std::chrono::duration<double>(Clock::now() - idleStart).count();
	const int cores = processors();
	const double othersBefore = (idle1.busy - idle0.busy - (idle1.own - idle0.own)) / idleWall;

	// The bench's own buffers before the module's memory is measured
	const int blocks = static_cast<int>(seconds * Syntakt::SampleRate / Block);
	std::vector<BlockLog> logs(static_cast<size_t>(blocks));
	std::vector<std::vector<int32_t>> buffers(Syntakt::Outputs16, std::vector<int32_t>(Block));
	std::ofstream raw;
	if(!rawPath.empty())
		raw.open(rawPath, std::ios::binary);

	const double memBefore = privateBytes();
	Syntakt st(section3, section7, tracks);
	st.setSamples(&samples);
	const double memMade = privateBytes();
	const int channels = st.outputs();
	busy = busy < 1 || busy > st.tracks() ? st.tracks() : busy;
	std::printf("%d tracks, %d busy\n", st.tracks(), busy);
	std::printf("machines:");
	for(const int m : machines)
		std::printf(" %d", m);
	std::printf(" (%zu engines)\n", machines.size());

	// The kit: both LFOs on every track (LFO1 on the filter's frequency, LFO2 on TUNE), the sends up, half the
	// tracks on the FX bus; a delay with feedback, a long reverb. Tracks 9-16 as 1-8 but their machines
	using S = Syntakt;
	for(int t = 0; t < busy; ++t)
	{
		const int k = t % S::Tracks;
		st.setMachine(t, machines[static_cast<size_t>(t) % machines.size()]);
		st.setParameter(t, S::Lfo1Speed, static_cast<uint16_t>(0x5000 + 0x400 * k));
		st.setParameter(t, S::Lfo1Destination, S::FilterFrequency << 8);
		st.setParameter(t, S::Lfo1Waveform, static_cast<uint16_t>((k % 7) << 8));
		st.setParameter(t, S::Lfo1Depth, 0x6000);
		st.setParameter(t, S::Lfo2Speed, static_cast<uint16_t>(0x6800 - 0x300 * k));
		st.setParameter(t, S::Lfo2Multiplier, static_cast<uint16_t>((14 + k) << 8));
		st.setParameter(t, S::Lfo2Destination, S::Tune << 8);
		st.setParameter(t, S::Lfo2Waveform, static_cast<uint16_t>(((k + 3) % 7) << 8));
		st.setParameter(t, S::Lfo2Depth, 0x4800);
		st.setParameter(t, S::DelaySend, 0x5800);
		st.setParameter(t, S::ReverbSend, 0x5800);
		st.setParameter(t, S::Routing, static_cast<uint16_t>(k < 4 ? 0 : 0x100));
	}
	st.setFxParameter(S::DelayTime, 0x2000);
	st.setFxParameter(S::DelayFeedback, 0x6000);
	st.setFxParameter(S::ReverbPreDelay, 0x2000);
	st.setFxParameter(S::ReverbDecay, 0x6800);
	st.setTempo(120);

	int32_t* outputs[Syntakt::Outputs16];
	for(int c = 0; c < channels; ++c)
		outputs[c] = buffers[static_cast<size_t>(c)].data();

	const int notes[] = {36, 48, 60, 55, 43, 67, 72, 52, 40, 64, 58, 47, 62, 50};
	int nextStep = 0;
	std::vector<int> offAt(static_cast<size_t>(busy), -1);		// the block a track's gate ends at
	std::vector<bool> fresh(static_cast<size_t>(busy), false);	// the track's machine changed since its last trig
	uint64_t lastBlocks = 0;

	const Load run0 = load();
	const auto runStart = Clock::now();
	for(int b = 0; b < blocks; ++b)
	{
		auto& log = logs[static_cast<size_t>(b)];
		const int64_t first = static_cast<int64_t>(b) * Block;
		const auto c0 = Clock::now();

		// The calls of the samples in this block, as a host's events before the block
		for(int t = 0; t < busy; ++t)
		{
			if(offAt[static_cast<size_t>(t)] == b)
			{
				st.noteOff(t);
				offAt[static_cast<size_t>(t)] = -1;
			}
		}
		while(static_cast<int64_t>(nextStep) * StepSamples < first + Block)
		{
			const int step = nextStep++;
			// Every two bars each track moves to the next machine
			if(step % 32 == 0 && step)
			{
				for(int t = 0; t < busy; ++t)
				{
					const size_t m = (static_cast<size_t>(t) + static_cast<size_t>(step / 32)) % machines.size();
					st.setMachine(t, machines[m]);
					fresh[static_cast<size_t>(t)] = true;
					++log.machines;
				}
			}
			for(int t = 0; t < busy; ++t)
			{
				if(step % 4 == t % 4)
					st.lock(t, S::Synth1 + (step / 4) % 5, static_cast<uint16_t>((step * 997 + t * 331) % 0x7f00));
				st.noteOn(t, notes[(step + 3 * t) % 14], 40 + (step * 13 + t * 29) % 88);
				offAt[static_cast<size_t>(t)] = b + 1 + (step + t) % 200;
				++log.trigs;
				if(fresh[static_cast<size_t>(t)])
				{
					++log.firstHits;
					fresh[static_cast<size_t>(t)] = false;
				}
			}
		}

		// A knob every block: a track's filter frequency, swept; every 16 blocks a machine parameter; every 750
		// blocks (half a second) the delay's time
		const int knob = b % busy;
		st.setParameter(knob, S::FilterFrequency, static_cast<uint16_t>(0x1000 + (b * 37) % 0x6000));
		if(b % 16 == 0)
			st.setParameter((b / 16) % busy, S::Synth1 + (b / 128) % 5, static_cast<uint16_t>((b * 53) % 0x7f00));
		if(b % 750 == 0 && b)
		{
			st.setFxParameter(S::DelayTime, static_cast<uint16_t>(0x0800 + (b / 750 * 0x0900) % 0x7000));
			log.delayTime = true;
		}
		const auto c1 = Clock::now();

		st.render(outputs, Block);
		const auto c2 = Clock::now();

		const auto& times = st.blockTimes();
		if(times.blocks != lastBlocks + 1)
		{
			std::printf("FAIL: block %d computed %llu blocks\n", b, static_cast<unsigned long long>(times.blocks -
				lastBlocks));
			return 1;
		}
		lastBlocks = times.blocks;
		log.control = std::chrono::duration<double>(c1 - c0).count();
		log.build = times.build;
		log.mix = times.mix;
		log.voices = times.voices;
		log.total = log.control + std::chrono::duration<double>(c2 - c1).count();

		if(raw)
			for(int c = 0; c < channels; ++c)
				raw.write(reinterpret_cast<const char*>(outputs[c]), 4 * Block);
	}
	const double runWall = std::chrono::duration<double>(Clock::now() - runStart).count();
	const Load run1 = load();
	const double memRun = privateBytes();

	std::vector<double> control, build, mix, voices, total, controlBuild;
	for(const auto& l : logs)
	{
		control.push_back(l.control);
		build.push_back(l.build);
		mix.push_back(l.mix);
		voices.push_back(l.voices);
		total.push_back(l.total);
		controlBuild.push_back(l.control + l.build);
	}

	std::printf("%d blocks, %.1f s of audio in %.2f s; one block = %.1f us\n", blocks, blocks * Period, runWall,
		Period * 1e6);
	print("CPU #1 calls", stats(control));
	print("CPU #1 buildBlock", stats(build));
	print("CPU #1 calls + build", stats(controlBuild));
	print("CPU #2 voices + stages", stats(voices));
	print("CPU #1 mix, FX", stats(mix));
	print("block total", stats(total));

	std::vector<size_t> order(logs.size());
	for(size_t i = 0; i < order.size(); ++i)
		order[i] = i;
	std::partial_sort(order.begin(), order.begin() + 8, order.end(),
		[&](const size_t _a, const size_t _b) { return logs[_a].total > logs[_b].total; });
	std::printf("worst blocks (us: calls, build, voices, mix; this block's calls, the previous block's):\n");
	for(size_t k = 0; k < 8; ++k)
	{
		const size_t i = order[k];
		const auto& l = logs[i];
		const BlockLog none;
		const auto& p = i ? logs[i - 1] : none;
		std::printf("  block %6zu (%7.3f s) %8.1f us: %6.1f %6.1f %6.1f %6.1f; trigs %d machines %d first hits %d%s;"
			" previous: trigs %d first hits %d%s\n", i, static_cast<double>(i) * Period, l.total * 1e6, l.control * 1e6,
			l.build * 1e6, l.voices * 1e6, l.mix * 1e6, l.trigs, l.machines, l.firstHits,
			l.delayTime ? " delay time" : "", p.trigs, p.firstHits, p.delayTime ? " delay time" : "");
	}

	std::printf("memory (private bytes): module %.2f MB as made, %.2f MB after the run (+%.2f MB on the audio path)\n",
		(memMade - memBefore) / 1048576, (memRun - memBefore) / 1048576, (memRun - memMade) / 1048576);
	const double othersRun = (run1.busy - run0.busy - (run1.own - run0.own)) / runWall;
	std::printf("machine load (other processes, cores busy of %d): %.2f before, %.2f during the run\n", cores,
		othersBefore, othersRun);
	if(raw)
		std::printf("wrote %d blocks x %d channels x %d int32 -> %s\n", blocks, channels, Block, rawPath.c_str());
	return 0;
}
