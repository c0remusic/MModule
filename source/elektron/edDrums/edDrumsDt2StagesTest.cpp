// The Digitakt II's BR, SRR and base-width (ticket 10 of .scratch/elektron-drums-v2):
//   - Q3 of each against the stage run in its OS (DT2_VECTORS, vectors-dt2: br_*, srr_*, bw_*, dt2_stage_oracle.py):
//     SNR over the case >= 70 dB, the worst 10 ms window >= 60 dB. BR and SRR are Dt2Br and Dt2Srr; the base-width is
//     the Syntakt's BaseWidth on the DT2's poles (dt2BaseWidthPole) and knobs (dt2BaseWidthKnob);
//   - 16 lanes play each lane's samples alone, bit for bit;
//   - at 44.1 kHz the SRR's mixed hold lasts what it does at 48, within a sample; BR has no rate;
//   - the cost of each on 16 lanes, the worst block.
// Exit 77 without the vectors, once the rest has run.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "edDrumsDt2Stages.h"
#include "elektronTimingTest.h"
#include "stFastMetrics.h"
#include "stFastTrack.h"

namespace
{
	using namespace elektron;
	using namespace elektron::drums;
	namespace bench = syntakt::fast::bench;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	std::string readText(const std::filesystem::path& _path)
	{
		std::ifstream f(_path, std::ios::binary);
		std::stringstream s;
		s << f.rdbuf();
		return s.str();
	}

	// "key": number, the first after _from
	double number(const std::string& _json, const char* _key, const size_t _from = 0,
		const size_t _to = std::string::npos)
	{
		const auto at = _json.find("\"" + std::string(_key) + "\":", _from);
		require(at != std::string::npos && at < _to, std::string("no ") + _key + " in a vector's json");
		return std::atof(_json.c_str() + at + std::strlen(_key) + 3);
	}

	bool has(const std::string& _json, const char* _key, const size_t _from, const size_t _to)
	{
		const auto at = _json.find("\"" + std::string(_key) + "\":", _from);
		return at != std::string::npos && at < _to;
	}

	// A case: the settings of each block (the json's settings, then its changes), its input and the OS's output
	struct Case
	{
		std::string name, stage;
		int blocks = 0;
		std::vector<std::map<std::string, double>> settings;
		std::vector<bool> routeChanged;
		std::vector<float> input, output;
	};

	Case load(const std::filesystem::path& _json)
	{
		Case c;
		const auto json = readText(_json);
		c.name = _json.stem().string();
		c.stage = c.name.substr(0, c.name.find('_'));
		c.blocks = static_cast<int>(number(json, "blocks"));
		const char* keys[]{"br", "srr", "srr_rt", "base", "wdth"};
		std::map<std::string, double> now;
		const auto settings = json.find("\"settings\"");
		for(const char* k : keys)
			now[k] = number(json, k, settings);
		const auto changes = json.find("\"changes\"");
		const auto changesEnd = json.find("\"record\"", changes);
		for(int b = 0; b < c.blocks; ++b)
		{
			bool route = false;
			const auto at = json.find("\"" + std::to_string(b) + "\": {", changes);
			if(at != std::string::npos && at < changesEnd)
			{
				const auto end = json.find('}', at);
				for(const char* k : keys)
					if(has(json, k, at, end))
					{
						now[k] = number(json, k, at, end);
						route = route || std::strcmp(k, "srr_rt") == 0;
					}
			}
			c.settings.push_back(now);
			c.routeChanged.push_back(route);
		}
		std::ifstream f(_json.parent_path() / (c.name + ".f32"), std::ios::binary);
		std::vector<float> frames(static_cast<size_t>(2 * c.blocks * BlockSize));
		f.read(reinterpret_cast<char*>(frames.data()), static_cast<std::streamsize>(frames.size() * sizeof(float)));
		require(f.gcount() == static_cast<std::streamsize>(frames.size() * sizeof(float)), c.name + ".f32 is short");
		for(size_t i = 0; i < frames.size(); i += 2)
		{
			c.input.push_back(frames[i]);
			c.output.push_back(frames[i + 1]);
		}
		return c;
	}

	int word(const double _value) { return static_cast<int>(std::lround(_value * 128)); }

	// The case through Lockstep's stage on _lanes lanes, each the case's input; lane 0's output
	std::vector<float> play(const Case& _c, const double _rate, const uint16_t _lanes = 1)
	{
		Dt2Br br;
		Dt2Srr srr(_rate);
		syntakt::fast::BaseWidth bw(_rate, dt2BaseWidthPole);
		std::vector<float> y;
		for(int b = 0; b < _c.blocks; ++b)
		{
			const auto& s = _c.settings[static_cast<size_t>(b)];
			Block block;
			for(int n = 0; n < BlockSize; ++n)
				for(int l = 0; l < Lanes; ++l)
					block.s[n][l] = _c.input[static_cast<size_t>(b * BlockSize + n)];
			for(int l = 0; l < Lanes; ++l)
			{
				if(!(_lanes >> l & 1))
					continue;
				br.set(l, word(s.at("br")));
				srr.set(l, word(s.at("srr")));
				bw.set(l, dt2BaseWidthKnob(word(s.at("base"))), dt2BaseWidthKnob(word(s.at("wdth"))));
				// The trig before block 0, and SR.RT moved: the routing places the stage anew, which clears it
				if(_c.stage == "srr" && b == 0)
					srr.hit(l);
				if(_c.routeChanged[static_cast<size_t>(b)])
					srr.clear(l);
			}
			if(_c.stage == "br")
				br.process(block, _lanes);
			else if(_c.stage == "srr")
				srr.process(block, _lanes);
			else
				bw.process(block, _lanes);
			for(int n = 0; n < BlockSize; ++n)
				y.push_back(block.s[n][0]);
		}
		return y;
	}

	std::vector<double> doubles(const std::vector<float>& _x) { return {_x.begin(), _x.end()}; }

	bool vectors()
	{
		const char* dir = std::getenv("DT2_VECTORS");
		if(!dir || !*dir || !std::filesystem::exists(std::filesystem::path(dir) / "STAGES.md"))
		{
			std::printf("DT2_VECTORS is not set or holds no stage vectors: the stages against their OS skipped\n");
			return false;
		}
		std::map<std::string, double> worstSnr{{"br", 1e9}, {"srr", 1e9}, {"bw", 1e9}}, worstLocal = worstSnr;
		std::map<std::string, std::string> worstCase;
		int cases = 0;
		for(const auto& entry : std::filesystem::directory_iterator(dir))
		{
			const auto name = entry.path().filename().string();
			if(entry.path().extension() != ".json" || !(name.rfind("br_", 0) == 0 || name.rfind("srr_", 0) == 0
				|| name.rfind("bw_", 0) == 0))
				continue;
			const auto c = load(entry.path());
			const auto y = play(c, 48000);
			const auto ref = doubles(c.output), test = doubles(y);
			double residual = 0, signal = 0;
			for(size_t i = 0; i < ref.size(); ++i)
			{
				residual += (test[i] - ref[i]) * (test[i] - ref[i]);
				signal += ref[i] * ref[i];
			}
			// A silent case (BR on silence) or one played exactly: a null residual
			const double snr = residual == 0 ? 300 : signal == 0 ? -300 : 10 * std::log10(signal / residual);
			// Shorter than a 10 ms window: its SNR alone
			const double local = residual == 0 ? 300 : ref.size() < 480 ? snr : bench::localSnr(ref, test, 48000);
			if(snr < 70 || local < 60)
				std::printf("    %s: SNR %.1f dB, worst window %.1f dB\n", c.name.c_str(), snr, local);
			if(snr < worstSnr[c.stage])
				worstCase[c.stage] = c.name;
			worstSnr[c.stage] = std::min(worstSnr[c.stage], snr);
			worstLocal[c.stage] = std::min(worstLocal[c.stage], local);
			++cases;
		}
		require(cases >= 90, "only " + std::to_string(cases) + " stage vectors found");
		for(const char* s : {"br", "srr", "bw"})
		{
			std::printf("  Q3, %s: SNR %.1f dB at worst (%s), worst 10 ms window %.1f dB\n", s, worstSnr[s],
				worstCase[s].c_str(), worstLocal[s]);
			require(worstSnr[s] >= 70 && worstLocal[s] >= 60, std::string("Q3 of ") + s + " against its OS");
		}
		std::printf("  %d cases\n", cases);
		return true;
	}

	// 16 lanes, each its own setting, against each alone
	void lanes()
	{
		for(const bool isBr : {true, false})
		{
			// All 16 lanes at once, each its own setting and input, against one stage per lane running that lane alone
			Dt2Br br;
			Dt2Srr srr(48000);
			std::vector<Dt2Br> brs(Lanes);
			std::vector<Dt2Srr> srrs(Lanes, Dt2Srr(48000));
			for(int l = 0; l < Lanes; ++l)
			{
				br.set(l, word(5 + 7.5 * l));
				brs[static_cast<size_t>(l)].set(l, word(5 + 7.5 * l));
				srr.set(l, word(3 + 8 * l));
				srrs[static_cast<size_t>(l)].set(l, word(3 + 8 * l));
			}
			for(int b = 0; b < 40; ++b)
			{
				Block in;
				for(int n = 0; n < BlockSize; ++n)
					for(int l = 0; l < Lanes; ++l)
						in.s[n][l] = static_cast<float>(0.8 * std::sin(0.013 * (b * BlockSize + n) * (1 + l)));
				Block together = in;
				isBr ? br.process(together) : srr.process(together);
				for(int l = 0; l < Lanes; ++l)
				{
					Block one = in;
					const auto mask = static_cast<uint16_t>(1 << l);
					if(isBr)
						brs[static_cast<size_t>(l)].process(one, mask);
					else
						srrs[static_cast<size_t>(l)].process(one, mask);
					for(int n = 0; n < BlockSize; ++n)
						for(int k = 0; k < Lanes; ++k)
							require(one.s[n][k] == (k == l ? together.s[n][k] : in.s[n][k]),
								std::string(isBr ? "BR" : "SRR") + ": lane " + std::to_string(l)
								+ " alone differs from the 16 together, or writes lane " + std::to_string(k));
				}
			}
		}
		std::printf("  BR and SRR: 16 lanes play each lane's samples alone, bit for bit, and leave the others\n");
	}

	void rates()
	{
		// The mixed hold, in seconds, at 48 and at 44.1 kHz
		for(int v = 1; v <= 127; ++v)
		{
			const double at48 = (Dt2Srr::hold(v) + 1) / 48000;
			const double t = std::max((Dt2Srr::hold(v) + 1) * 44100 / 48000 - 1, 0.0);
			const double at44 = (t + 1) / 44100;
			require(std::abs(at48 - at44) <= 1 / 44100.0, "SRR " + std::to_string(v) + "'s hold at 44.1 kHz");
		}
		std::printf("  SRR's mixed hold at 44.1 kHz lasts what it does at 48 within a sample, values 1-127\n");
	}

	void cost()
	{
		Dt2Br br;
		Dt2Srr srr(44100);
		Block block{};
		for(int l = 0; l < Lanes; ++l)
		{
			br.set(l, word(100));
			srr.set(l, word(64));
		}
		for(const char* stage : {"BR", "SRR"})
		{
			double worst = 0, total = 0;
			constexpr int Rounds = 4000;
			for(int r = 0; r < Rounds; ++r)
			{
				for(int n = 0; n < BlockSize; ++n)
					for(int l = 0; l < Lanes; ++l)
						block.s[n][l] = static_cast<float>(std::sin(0.01 * (r * BlockSize + n) + l));
				const auto t0 = std::chrono::steady_clock::now();
				std::strcmp(stage, "BR") == 0 ? br.process(block) : srr.process(block);
				const auto spent = std::chrono::steady_clock::now() - t0;
				const double us = std::chrono::duration<double, std::micro>(spent).count();
				worst = std::max(worst, us);
				total += us;
			}
			const double share = total / Rounds / (BlockSize / 44100.0 * 1e6) * 100;
			std::printf("  %s on 16 lanes at 44.1 kHz: %.3f %% of a core, worst block %.1f us\n", stage, share, worst);
		}
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	elektron::raiseTimingTestPriority();
	try
	{
		lanes();
		rates();
		cost();
		if(!vectors())
		{
			std::printf("PASS (no vectors)\n");
			return 77;
		}
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
