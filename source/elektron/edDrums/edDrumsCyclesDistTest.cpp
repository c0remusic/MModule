// The Cycles dist drive (ticket 17 of .scratch/elektron-drums-v2): the Model:Cycles' VOL+DIST stage as a Track drive.
//   - its laws: dry up to AMOUNT 60, then the Overdrive's curve under the Cycles' gain and wet share;
//   - 16 lanes at their own AMOUNTs give the samples of each lane alone;
//   - a chain at 44.1 kHz gives the samples of one at 48: the stage reads no rate;
//   - Q3 against the stage run in the Cycles' own OS (CYCLES_DIST_VECTORS, the folder vectors_cycles_dist.py wrote: per
//     machine and VOL value the stage's input and its output, 48 kHz, 32 int32 a block): SNR >= 70 dB over the Hit, the
//     worst 10 ms window >= 60 dB; up to VOL 60 the OS's output is its input (so the 108 machine vectors of
//     vectors-cycles, all at the default VOL 60, say nothing of the distortion).
// Exit 77 without the vectors, once the rest has run.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "edDrumsChain.h"
#include "edDrumsMaster.h"
#include "edDrumsRate.h"
#include "stFastMetrics.h"

namespace
{
	using namespace elektron::drums;
	using elektron::Block;
	using elektron::BlockSize;
	using elektron::Lanes;
	namespace bench = syntakt::fast::bench;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	ChainSettings cyclesDrive(const int _amount)
	{
		ChainSettings s;
		s.drive[0] = {DriveType::CyclesDist, true, _amount, 127};
		return s;
	}

	std::vector<float> floats(const std::vector<double>& _x)
	{
		std::vector<float> f(_x.size());
		for(size_t n = 0; n < _x.size(); ++n)
			f[n] = static_cast<float>(_x[n]);
		return f;
	}

	// _in's lane 0 through a chain of _settings at _rate, from silence
	std::vector<float> through(const std::vector<float>& _in, const ChainSettings& _settings, const double _rate,
		const bool _syntakt)
	{
		auto chain = std::make_unique<Chain>(_rate, _syntakt);
		chain->configure(0, _settings);
		auto block = std::make_unique<Block>();
		std::vector<float> y(_in.size());
		for(size_t at = 0; at + BlockSize <= _in.size(); at += BlockSize)
		{
			for(int n = 0; n < BlockSize; ++n)
				for(int l = 0; l < Lanes; ++l)
					block->s[n][l] = _in[at + static_cast<size_t>(n)];
			chain->process(*block, 1);
			for(int n = 0; n < BlockSize; ++n)
				y[at + static_cast<size_t>(n)] = block->s[n][0];
		}
		return y;
	}

	std::vector<float> sine(const double _level, const double _hz, const double _rate, const size_t _samples)
	{
		std::vector<float> x(_samples);
		for(size_t n = 0; n < _samples; ++n)
			x[n] = static_cast<float>(_level * std::sin(2 * bench::Pi * _hz * static_cast<double>(n) / _rate));
		return x;
	}

	// The stage's formula on one sample: the Overdrive's curve under the Cycles' laws
	double formula(const double _x, const int _amount)
	{
		return _x + CyclesDist::mix(_amount) * (saturate(_x, CyclesDist::gain(_amount)) - _x);
	}

	void laws()
	{
		const auto x = sine(0.8, 220, 48000, 4096);
		double worst = 0;
		for(const int amount : {0, 60, 61, 64, 66, 68, 96, 127})
		{
			const auto y = through(x, cyclesDrive(amount), 48000, true);
			for(size_t n = 0; n < y.size(); ++n)
				worst = std::max(worst, std::abs(y[n] - formula(x[n], amount)));
			if(amount <= 60)
				require(y == x, "Cycles dist up to 60 is not the dry Track");
		}
		std::printf("  laws: the curve under the Cycles' gain and share at AMOUNT 0 to 127 within %.1f dB; dry up to 60"
			"\n", 20 * std::log10(std::max(worst, 1e-30)));
		require(worst < 1e-6, "Cycles dist is not its formula");
		require(CyclesDist::mix(60) == 0 && CyclesDist::mix(64) == 0.5 && CyclesDist::mix(68) == 1,
			"the wet share's knots");
		require(std::abs(CyclesDist::gain(64) - 0.1535) < 1e-4 && std::abs(CyclesDist::gain(127) - 0.9737) < 1e-4,
			"the gain's ends");
	}

	// 16 lanes at their own AMOUNTs, asked for in full, in halves and alone, against each lane alone on a stage of its
	// own
	void lanes()
	{
		std::mt19937 random(17);
		CyclesDist all(rate::fastFir());
		std::vector<CyclesDist> alone(Lanes, CyclesDist(rate::fastFir()));
		for(int l = 0; l < Lanes; ++l)
		{
			const int amount = l % 4 == 0 ? 60 : static_cast<int>(random() % 128);
			all.set(l, amount << 7);
			alone[static_cast<size_t>(l)].set(l, amount << 7);
		}
		const uint16_t masks[] = {0xffff, 0x0ff0, 0x00f3, 0x8001, 0xf0f0};
		int64_t compared = 0;
		for(int b = 0; b < 200; ++b)
		{
			Block in;
			for(auto& row : in.s)
				for(auto& v : row)
					v = static_cast<float>(static_cast<int>(random() % 4001) - 2000) / 1000.0f;
			const uint16_t lanes = masks[static_cast<size_t>(b / 8) % std::size(masks)];
			Block out = in;
			all.process(out, lanes);
			for(int l = 0; l < Lanes; ++l)
			{
				Block one = in;
				alone[static_cast<size_t>(l)].process(one, static_cast<uint16_t>(1 << l));
				for(int n = 0; n < BlockSize; ++n)
				{
					const float expected = lanes >> l & 1 ? one.s[n][l] : in.s[n][l];
					require(std::memcmp(&out.s[n][l], &expected, sizeof(float)) == 0,
						"a lane among 16 is not the lane alone");
					++compared;
				}
			}
		}
		std::printf("  lanes: 16 at their own AMOUNTs, %lld samples, each the lane alone's\n",
			static_cast<long long>(compared));
	}

	// The same samples through a Machinedrum chain at 44.1 kHz and a Syntakt one at 48: the stage reads no rate
	void rates()
	{
		const auto x = sine(0.7, 1000, 48000, 8192);
		for(const int amount : {64, 100, 127})
		{
			const auto a = through(x, cyclesDrive(amount), 44100, false);
			const auto b = through(x, cyclesDrive(amount), 48000, true);
			require(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0,
				"44.1 and 48 kHz give different samples");
		}
		std::printf("  rates: 44.1 and 48 kHz, the same samples at AMOUNT 64, 100, 127\n");
	}

	// What the stage costs on 16 lanes, in percent of a core at 44.1 kHz, the least of 5 passes: reported, not bound
	// (the worst kit's bench holds the chain's guide)
	void cost()
	{
		CyclesDist stage(rate::fastFir());
		for(int l = 0; l < Lanes; ++l)
			stage.set(l, (100 + l) << 7);
		Block block;
		for(int n = 0; n < BlockSize; ++n)
			for(int l = 0; l < Lanes; ++l)
				block.s[n][l] = 0.3f * static_cast<float>(std::sin(0.1 * n + l));
		constexpr int Blocks = 20000;
		double best = 1e9;
		for(int pass = 0; pass < 5; ++pass)
		{
			const auto t0 = std::chrono::steady_clock::now();
			for(int b = 0; b < Blocks; ++b)
				stage.process(block, 0xffff);
			const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			best = std::min(best, seconds);
		}
		std::printf("  cost: 16 lanes, %.2f %% of a core at 44.1 kHz (%s)\n",
			100 * best / (Blocks * BlockSize / 44100.0), rate::fastFir() ? "AVX2" : "SSE2");
	}

	std::vector<double> readQ31(const std::string& _path)
	{
		std::ifstream f(_path, std::ios::binary);
		require(f.good(), "cannot read " + _path);
		std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		std::vector<double> x(bytes.size() / 4);
		for(size_t n = 0; n < x.size(); ++n)
		{
			int32_t v;
			std::memcpy(&v, bytes.data() + 4 * n, 4);
			x[n] = bench::q31(v);
		}
		return x;
	}

	struct Q3
	{
		double snr, window;
		Q3 worstWith(const Q3& _q) const { return {std::min(snr, _q.snr), std::min(window, _q.window)}; }
	};

	// The chain's output against the OS's: SNR over the whole Hit, and bench::localSnr, the worst 10 ms window of those
	// within 60 dB of the loudest
	Q3 q3Of(const std::vector<double>& _y, const std::vector<double>& _reference)
	{
		std::vector<double> residue(_y.size());
		for(size_t n = 0; n < _y.size(); ++n)
			residue[n] = _y[n] - _reference[n];
		return {bench::db(bench::energy(_reference) / std::max(bench::energy(residue), 1e-30)),
			bench::localSnr(_reference, _y, 48000)};
	}

	// vectors_cycles_dist.py's cases: the six machines at each VOL value
	constexpr const char* Machines[] = {"kick", "snare", "metal", "perc", "tone", "chord"};
	constexpr int Vols[] = {0, 32, 60, 61, 62, 63, 64, 65, 66, 67, 68, 80, 96, 112, 127};

	bool vectors()
	{
		const char* dir = std::getenv("CYCLES_DIST_VECTORS");
		if(!dir || !*dir)
		{
			std::printf("CYCLES_DIST_VECTORS is not set: the stage against its OS skipped\n");
			return false;
		}
		Q3 worst{1e9, 1e9};
		int cases = 0;
		double dryOff = 0;
		for(const int vol : Vols)
		{
			Q3 worstAtVol{1e9, 1e9};
			for(const char* machine : Machines)
			{
				const std::string stem = std::string(dir) + "/" + machine + "_vol" + std::to_string(vol);
				const auto in = readQ31(stem + ".in.i32"), reference = readQ31(stem + ".track.i32");
				require(in.size() == reference.size() && in.size() % BlockSize == 0, stem + ": sizes");
				const auto y = through(floats(in), cyclesDrive(vol), 48000, true);
				worstAtVol = worstAtVol.worstWith(q3Of(std::vector<double>(y.begin(), y.end()), reference));
				if(vol <= 60)
					for(size_t n = 0; n < in.size(); ++n)
						dryOff = std::max(dryOff, std::abs(reference[n] - in[n]));
				++cases;
			}
			std::printf("  Q3, VOL %3d: SNR %.1f dB, worst window %.1f dB\n", vol, worstAtVol.snr, worstAtVol.window);
			worst = worst.worstWith(worstAtVol);
		}
		std::printf("  Q3 at worst over %d cases: SNR %.1f dB, window %.1f dB (thresholds 70, 60); up to VOL 60 the "
			"OS's output is its input within %.0f dB\n", cases, worst.snr, worst.window,
			20 * std::log10(std::max(dryOff, 1e-30)));
		require(worst.snr >= 70 && worst.window >= 60, "Q3 of the Cycles dist against its OS");
		// The OS's dry word is 1 - 2^-31, its product truncated
		require(dryOff < 1e-9, "up to VOL 60 the OS's stage is not a bypass");
		return true;
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		laws();
		lanes();
		rates();
		cost();
		if(!vectors())
			return 77;
	}
	catch(const std::exception& _e)
	{
		std::printf("FAIL: %s\n", _e.what());
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
