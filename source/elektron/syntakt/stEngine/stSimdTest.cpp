// The AVX2 kernels (stSimdAvx2.cpp) against the scalar code they replace, on random states and edge values:
// INT32_MIN and INT32_MAX samples and gains, steps that wrap, accumulators not clear or overflowed at the start (the
// routines' first frames then step), MACSR with and without OMC, in integer mode too. CPU #1's routines run twice on
// copies of one memory and EMAC, the kernels on and off (simd::setAvx2), and must leave the same SRAM, the same
// accumulators and the same gains; the voice CPU's host-order mix (track::filter's output mix) runs against its own
// scalar loop. Exit 77 on a CPU without AVX2, where only the scalar code runs.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "stEmac.h"
#include "stMainBus.h"
#include "stMainEmac.h"
#include "stMainMemory.h"
#include "stSimd.h"

namespace
{
	using syntakt::main::Emac4;
	using syntakt::main::Memory;

	constexpr int Cases = 3000;
	constexpr uint32_t Frames = 32;

	std::mt19937 rng(0x5717);

	uint32_t below(const uint32_t _n) { return std::uniform_int_distribution<uint32_t>(0, _n - 1)(rng); }

	// A long, often an edge value
	int32_t randomWord()
	{
		static const int32_t edges[] = {INT32_MIN, INT32_MAX, 0, -1, 1, INT32_MIN + 1, INT32_MAX - 1, 0x40000000,
			-0x40000000, 0x7fffff00};
		switch(below(4))
		{
		case 0: return edges[below(sizeof edges / sizeof edges[0])];
		case 1: return static_cast<int32_t>(below(0x20000)) - 0x10000;
		default: return static_cast<int32_t>(rng());
		}
	}

	void fill(Memory& _mem, const uint32_t _address, const uint32_t _longs)
	{
		for(uint32_t i = 0; i < _longs; ++i)
			_mem.setWord(_address + 4 * i, randomWord());
	}

	// MACSR fractional more often than not, with or without OMC, and now and then an accumulator not clear or
	// overflowed (PAV set)
	Emac4 randomEmac()
	{
		static const uint32_t modes[] = {0x20, 0xa0, 0xa0, 0xa0, 0x20, 0x00, 0x80};
		Emac4 acc;
		acc.setMacsr(modes[below(sizeof modes / sizeof modes[0])]);
		for(int n = 0; n < 4; ++n)
		{
			const uint32_t r = below(16);
			if(r < 2)
				acc.load(n, randomWord());
			else if(r == 2)
			{
				const uint32_t macsr = acc.macsr();
				acc.setMacsr(macsr | Emac4::Fractional);
				for(int i = 0; i < 300; ++i)
					acc.mac(n, INT32_MIN, INT32_MIN);
				acc.setMacsr(macsr);
			}
		}
		return acc;
	}

	// What the accumulators hold, as far as the stores show it: bits 0 to 39 and the flags
	std::vector<int64_t> stateOf(const Emac4& _acc)
	{
		std::vector<int64_t> s{_acc.macsr()};
		for(int n = 0; n < 4; ++n)
		{
			Emac4 a = _acc;
			a.setMacsr(Emac4::Fractional);
			s.push_back(a.store(n));
			a.setMacsr(0);
			s.push_back(a.store(n));
			s.push_back(_acc.clear(n));
		}
		return s;
	}

	struct Result
	{
		const char* name;
		int cases = 0;
		int differ = 0;
	};

	// One case of a routine of CPU #1: _setup fills a memory and the EMAC, _run runs the routine on them; the run
	// with the kernels and the run without must agree
	template<typename Setup, typename Run>
	void compare(Result& _r, const syntakt::main::Section3& _rom, const Setup& _setup, const Run& _run)
	{
		Memory mem(_rom);
		Emac4 acc = randomEmac();
		_setup(mem);
		Memory memScalar = mem;
		Emac4 accScalar = acc;
		const std::vector<int32_t> extra = _run(mem, acc, true);
		const std::vector<int32_t> extraScalar = _run(memScalar, accScalar, false);
		++_r.cases;
		const auto& a = mem.regions().front().bytes;
		const auto& b = memScalar.regions().front().bytes;
		if(std::memcmp(a.data(), b.data(), a.size()) || stateOf(acc) != stateOf(accScalar) || extra != extraScalar ||
			mem.fault() != memScalar.fault())
			++_r.differ;
	}

	std::vector<int32_t> randomSteps(const int _n)
	{
		std::vector<int32_t> s(static_cast<size_t>(_n));
		for(auto& v : s)
			v = randomWord();
		return s;
	}
}

int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	if(!syntakt::simd::avx2())
	{
		std::printf("SKIP: no AVX2 on this CPU (or SYNTAKT_NO_AVX2 set): only the scalar code runs\n");
		return 77;
	}

	const syntakt::main::Section3 rom{};
	constexpr uint32_t Src = 0x80002000, Dst = 0x80001000, Gains = 0x80003000, Buffer = 0x80004000;
	std::vector<Result> results;

	for(const int channels : {8, 9, 10})
	{
		Result r{channels == 8 ? "sumChannels 8" : channels == 9 ? "sumChannels 9" : "sumChannels 10"};
		for(int c = 0; c < Cases; ++c)
		{
			const auto steps = randomSteps(channels);
			const uint32_t side = 4 * below(2);
			compare(r, rom, [&](Memory& _m)
			{
				fill(_m, Src, 128 * 10 / 4);
				fill(_m, Dst, 2 * Frames);
				fill(_m, Gains, 10);
			}, [&](Memory& _m, Emac4& _a, const bool _avx2)
			{
				syntakt::simd::setAvx2(_avx2);
				syntakt::main::sumChannels(_m, _a, channels, Dst + side, Src, Gains, steps.data());
				return std::vector<int32_t>{};
			});
		}
		results.push_back(r);
	}

	{
		Result r{"rampBuffer"};
		for(int c = 0; c < Cases; ++c)
		{
			const int32_t from = randomWord(), to = randomWord();
			compare(r, rom, [&](Memory& _m) { fill(_m, Buffer, Frames); }, [&](Memory& _m, Emac4& _a, const bool _avx2)
			{
				syntakt::simd::setAvx2(_avx2);
				syntakt::main::rampBuffer(_m, _a, Buffer, from, to);
				return std::vector<int32_t>{};
			});
		}
		results.push_back(r);
	}

	{
		Result r{"scaleBus"};
		for(int c = 0; c < Cases; ++c)
		{
			static const int32_t edges[] = {128, -128, 129, -129, 0, 1, -1, 1000, INT32_MIN, INT32_MAX};
			const int32_t gain = below(3) ? static_cast<int32_t>(below(257)) - 128 : edges[below(10)];
			compare(r, rom, [&](Memory& _m) { fill(_m, Buffer, 2 * Frames); },
				[&](Memory& _m, Emac4& _a, const bool _avx2)
			{
				syntakt::simd::setAvx2(_avx2);
				syntakt::main::scaleBus(_m, _a, Buffer, gain);
				return std::vector<int32_t>{};
			});
		}
		results.push_back(r);
	}

	{
		Result r{"mixBuses"};
		for(int c = 0; c < Cases; ++c)
		{
			const auto gains = randomSteps(2), steps = randomSteps(2);
			// In place, apart, or overlapping (the scalar steps then)
			const uint32_t dst = below(3) == 0 ? Src : below(2) ? Dst : Src + 8;
			compare(r, rom, [&](Memory& _m)
			{
				fill(_m, Src, 4 * Frames);
				fill(_m, Dst, 2 * Frames);
			}, [&](Memory& _m, Emac4& _a, const bool _avx2)
			{
				syntakt::simd::setAvx2(_avx2);
				std::vector<int32_t> g = gains;
				syntakt::main::mixBuses(_m, _a, dst, Src, g.data(), steps.data());
				return g;
			});
		}
		results.push_back(r);
	}

	// The voice CPU's mix of three host-order buffers at ramped gains into the first, saturated, as track::filter's
	// output mix runs it without the kernel
	{
		Result r{"host-order mix"};
		for(int c = 0; c < Cases; ++c)
		{
			int32_t buffers[3][Frames];
			for(auto& b : buffers)
				for(auto& v : b)
					v = randomWord();
			const auto gains = randomSteps(3), steps = randomSteps(3);
			int32_t scalar[3][Frames];
			std::memcpy(scalar, buffers, sizeof buffers);

			syntakt::simd::RampMix m{};
			for(int k = 0; k < 3; ++k)
			{
				m.src[k] = reinterpret_cast<const uint8_t*>(buffers[k]);
				m.gains[k] = gains[static_cast<size_t>(k)];
				m.steps[k] = steps[static_cast<size_t>(k)];
			}
			m.channels = 3;
			m.srcStride = 4;
			m.dst = reinterpret_cast<uint8_t*>(buffers[0]);
			m.dstStride = 4;
			m.first = 0;
			m.negate = false;
			m.saturate = true;
			m.bigEndian = false;
			syntakt::simd::rampMixAvx2(m);

			int32_t px = gains[0], pa = gains[1], pb = gains[2];
			syntakt::Emac acc;
			for(uint32_t n = 0; n < Frames; ++n)
			{
				acc.macs(syntakt::Mac{px, scalar[0][n]}, syntakt::Mac{pa, scalar[1][n]},
					syntakt::Mac{pb, scalar[2][n]});
				px = syntakt::addWrap(px, steps[0]);
				pa = syntakt::addWrap(pa, steps[1]);
				pb = syntakt::addWrap(pb, steps[2]);
				scalar[0][n] = acc.storeClear();
			}
			++r.cases;
			if(std::memcmp(buffers, scalar, sizeof buffers))
				++r.differ;
		}
		results.push_back(r);
	}

	syntakt::simd::setAvx2(true);
	int failed = 0;
	for(const auto& r : results)
	{
		std::printf("%-16s %5d cases, %4d differ\n", r.name, r.cases, r.differ);
		failed += r.differ;
	}
	std::printf(failed ? "FAIL\n" : "PASS\n");
	return failed ? 1 : 0;
}
