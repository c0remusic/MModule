// The Digitakt II's six track filters (ticket 09 of .scratch/elektron-drums-v2):
//   - Q3 of each against the filter run in its OS (DT2_VECTORS, vectors-dt2: flt_*, dt2_filter_oracle.py): SNR over
//     the case >= 70 dB, the worst 10 ms window >= 60 dB; the cutoff word the chain hands the setter (key tracking
//     included) against the one the OS handed it; type 0 passes the Track through, bit for bit;
//   - 16 lanes play each lane's samples alone, bit for bit, and leave the lanes not asked for;
//   - Q15: a Hit through each at 44.1 kHz against 48 kHz, 16 settings each: 10 ms envelopes within 0.2 dB, third
//     octaves to 16 kHz within 0.5 dB, or the measured bounds the user took for MULTI-MODE, LOWPASS 4 and the combs;
//   - the cost of each on 16 lanes at 44.1 kHz, the worst block.
// Exit 77 without the vectors, once the rest has run.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "edDrumsDt2Filters.h"
#include "elektronTimingTest.h"
#include "stFastMetrics.h"

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

	// The DT2's type index, 1 to 6, as one object of Lockstep's, by its sample rate
	struct Filters
	{
		explicit Filters(const double _rate)
			: multimode(_rate), equalizer(_rate), lowpass4(_rate), combMinus(_rate, false), combPlus(_rate, true),
			  legacy(_rate)
		{
		}

		// _function(the type's filter), for a type 1 to 6
		template<typename Function>
		void with(const int _type, Function&& _function)
		{
			switch(_type)
			{
			case 1: _function(multimode); break;
			case 2: _function(equalizer); break;
			case 3: _function(lowpass4); break;
			case 4: _function(combMinus); break;
			case 5: _function(legacy); break;
			case 6: _function(combPlus); break;
			default: break;
			}
		}

		Dt2Multimode multimode;
		Dt2Equalizer equalizer;
		Dt2Lowpass4 lowpass4;
		Dt2Comb combMinus, combPlus;
		Dt2Legacy legacy;
	};

	bool isComb(const int _type) { return _type == 4 || _type == 6; }

	// A Kit's settings as the DT2's record holds them: FREQ, p68, p70, key tracking and note
	struct Setting
	{
		int type = 0, freq = 64, p68 = 64, p70 = 0, kt = 0, note = 48;
	};

	// The type's filter set as the chain sets it (Chain::setFilter, no envelope, no LFO)
	void apply(Filters& _f, const int _lane, const Setting& _s)
	{
		const bool comb = isComb(_s.type);
		const float freq = dt2::frequency(_s.freq << 7, _s.kt, _s.note, comb);
		const float p68 = dt2::knob127(_s.p68 << 7), p70 = comb ? dt2::knob127(_s.p70 << 7) : dt2::knob128(_s.p70 << 7);
		_f.with(_s.type, [&](auto& _filter) { _filter.set(_lane, freq, p68, p70); });
	}

	void process(Filters& _f, const int _type, Block& _block, const uint16_t _lanes)
	{
		_f.with(_type, [&](auto& _filter) { _filter.process(_block, _lanes); });
	}

	// ---- the vectors ----

	std::string readText(const std::filesystem::path& _path)
	{
		std::ifstream f(_path, std::ios::binary);
		std::stringstream s;
		s << f.rdbuf();
		return s.str();
	}

	// "key": number, the first after _from and before _to
	bool find(const std::string& _json, const char* _key, double& _value, const size_t _from = 0,
		const size_t _to = std::string::npos)
	{
		const auto at = _json.find("\"" + std::string(_key) + "\":", _from);
		if(at == std::string::npos || at >= _to)
			return false;
		_value = std::atof(_json.c_str() + at + std::strlen(_key) + 3);
		return true;
	}

	double number(const std::string& _json, const char* _key, const size_t _from = 0)
	{
		double v = 0;
		require(find(_json, _key, v, _from), std::string("no ") + _key + " in a vector's json");
		return v;
	}

	struct Case
	{
		std::string name, group;
		int blocks = 0;
		std::vector<Setting> settings;
		float r8 = 0;					// the cutoff word the OS's chain handed the setter in block 0
		bool hasR8 = false;
		std::vector<float> input, output;
	};

	Case load(const std::filesystem::path& _json)
	{
		Case c;
		const auto json = readText(_json);
		c.name = _json.stem().string();
		const auto second = c.name.find('_', 4);
		c.group = c.name.substr(4, second - 4);
		if(c.group == "combm" || c.group == "combp")
			c.group = "comb";
		c.blocks = static_cast<int>(number(json, "blocks"));
		std::map<std::string, double> now;
		const char* keys[]{"type", "freq", "p68", "p70", "kt", "note"};
		const auto settings = json.find("\"settings_block0\"");
		for(const char* k : keys)
			now[k] = number(json, k, settings);
		const auto changes = json.find("\"changes\"");
		const auto changesEnd = json.find("\"type_name\"", changes);
		for(int b = 0; b < c.blocks; ++b)
		{
			const auto at = json.find("\"" + std::to_string(b) + "\": {", changes);
			if(at != std::string::npos && at < changesEnd)
			{
				const auto end = json.find('}', at);
				for(const char* k : keys)
				{
					double v;
					if(find(json, k, v, at, end))
						now[k] = v;
				}
			}
			Setting s;
			s.type = static_cast<int>(now["type"]);
			s.freq = static_cast<int>(now["freq"]);
			s.p68 = static_cast<int>(now["p68"]);
			s.p70 = static_cast<int>(now["p70"]);
			s.kt = static_cast<int>(now["kt"]);
			s.note = static_cast<int>(now["note"]);
			c.settings.push_back(s);
		}
		double r8;
		if(find(json, "R8", r8, json.find("\"setter_args_per_block\"")))
		{
			c.r8 = static_cast<float>(r8);
			c.hasR8 = true;
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

	// The case on lane 0: the trig before block 0 resets the type it starts on; a change of type places the new one
	// anew, which clears it (Chain::place)
	std::vector<float> play(const Case& _c)
	{
		Filters f(48000);
		std::vector<float> y;
		int type = -1;
		for(int b = 0; b < _c.blocks; ++b)
		{
			const auto& s = _c.settings[static_cast<size_t>(b)];
			if(s.type != type)
			{
				f.with(s.type, [&](auto& _filter) { b == 0 ? _filter.hit(0) : _filter.clear(0); });
				type = s.type;
			}
			apply(f, 0, s);
			Block block{};
			for(int n = 0; n < BlockSize; ++n)
				block.s[n][0] = _c.input[static_cast<size_t>(b * BlockSize + n)];
			process(f, s.type, block, 1);
			for(int n = 0; n < BlockSize; ++n)
				y.push_back(block.s[n][0]);
		}
		return y;
	}

	bool vectors()
	{
		const char* dir = std::getenv("DT2_VECTORS");
		if(!dir || !*dir || !std::filesystem::exists(std::filesystem::path(dir) / "FILTERS.md"))
		{
			std::printf("DT2_VECTORS is not set or holds no filter vectors: the filters against their OS skipped\n");
			return false;
		}
		std::map<std::string, double> worstSnr, worstLocal;
		std::map<std::string, std::string> worstCase, worstWindowCase;
		int cases = 0, words = 0;
		for(const auto& entry : std::filesystem::directory_iterator(dir))
		{
			const auto name = entry.path().filename().string();
			if(entry.path().extension() != ".json" || name.rfind("flt_", 0) != 0)
				continue;
			const auto c = load(entry.path());
			const auto y = play(c);
			++cases;
			if(c.group == "none")
			{
				require(y == c.input && c.output == c.input, c.name + ": type 0 does not pass the Track through");
				continue;
			}
			// The cutoff word, key tracking included, as the OS's chain gave it (to its float's rounding)
			if(c.hasR8)
			{
				const auto& s = c.settings[0];
				const float word = dt2::frequency(s.freq << 7, s.kt, s.note, isComb(s.type));
				require(std::abs(word - c.r8) <= 1e-6f, c.name + ": the cutoff word " + std::to_string(word)
					+ " where the OS gave " + std::to_string(c.r8));
				++words;
			}
			const std::vector<double> ref(c.output.begin(), c.output.end()), test(y.begin(), y.end());
			double residual = 0, signal = 0;
			for(size_t i = 0; i < ref.size(); ++i)
			{
				residual += (test[i] - ref[i]) * (test[i] - ref[i]);
				signal += ref[i] * ref[i];
			}
			const double snr = residual == 0 ? 300 : 10 * std::log10(signal / residual);
			const double local = residual == 0 ? 300 : bench::localSnr(ref, test, 48000);
			if(snr < 70 || local < 60)
				std::printf("    %s: SNR %.1f dB, worst window %.1f dB\n", c.name.c_str(), snr, local);
			if(!worstSnr.count(c.group) || snr < worstSnr[c.group])
			{
				worstSnr[c.group] = snr;
				worstCase[c.group] = c.name;
			}
			if(!worstLocal.count(c.group) || local < worstLocal[c.group])
			{
				worstLocal[c.group] = local;
				worstWindowCase[c.group] = c.name;
			}
		}
		require(cases >= 150, "only " + std::to_string(cases) + " filter vectors found");
		require(words >= 140, "only " + std::to_string(words) + " cutoff words checked");
		bool pass = true;
		for(const auto& [group, snr] : worstSnr)
		{
			std::printf("  Q3, %s: SNR %.1f dB at worst (%s), worst 10 ms window %.1f dB (%s)\n", group.c_str(), snr,
				worstCase[group].c_str(), worstLocal[group], worstWindowCase[group].c_str());
			pass = pass && snr >= 70 && worstLocal[group] >= 60;
		}
		require(pass, "Q3 against the OS");
		std::printf("  %d cases, %d cutoff words as the OS's chain gave them, type 0 the Track itself\n", cases, words);
		return true;
	}

	// ---- 16 lanes ----

	Setting laneSetting(const int _type, const int _lane)
	{
		Setting s;
		s.type = _type;
		s.freq = 20 + 6 * _lane;
		s.p68 = _type == 5 ? _lane % 3 : 8 * _lane;
		s.p70 = 127 - 7 * _lane;
		s.kt = _lane % 2 ? 100 : 0;
		s.note = 36 + 3 * _lane;
		return s;
	}

	void lanes()
	{
		for(int type = 1; type <= 6; ++type)
		{
			Filters together(48000);
			std::vector<std::unique_ptr<Filters>> alone;
			for(int l = 0; l < Lanes; ++l)
			{
				alone.push_back(std::make_unique<Filters>(48000));
				apply(together, l, laneSetting(type, l));
				apply(*alone.back(), l, laneSetting(type, l));
			}
			for(int b = 0; b < 40; ++b)
			{
				// Halfway, the odd lanes placed anew (a change of type) and the even ones hit: each lane's own news
				if(b == 20)
					for(int l = 0; l < Lanes; ++l)
						for(Filters* f : {&together, alone[static_cast<size_t>(l)].get()})
							f->with(type, [&](auto& _filter)
							{
								if(l & 1)
									_filter.clear(l);
								else
									_filter.hit(l);
							});
				Block in;
				for(int n = 0; n < BlockSize; ++n)
					for(int l = 0; l < Lanes; ++l)
						in.s[n][l] = static_cast<float>(0.6 * std::sin(0.013 * (b * BlockSize + n) * (1 + l))
							+ (b * BlockSize + n == 3 * l ? 0.4 : 0));
				Block all = in;
				process(together, type, all, 0xffff);
				for(int l = 0; l < Lanes; ++l)
				{
					Block one = in;
					process(*alone[static_cast<size_t>(l)], type, one, static_cast<uint16_t>(1 << l));
					for(int n = 0; n < BlockSize; ++n)
						for(int k = 0; k < Lanes; ++k)
							require(one.s[n][k] == (k == l ? all.s[n][k] : in.s[n][k]), "type " + std::to_string(type)
								+ ": lane " + std::to_string(l) + " alone differs from the 16 together, or writes lane "
								+ std::to_string(k));
				}
			}
		}
		std::printf("  16 lanes play each lane's samples alone, bit for bit, and leave the others, every type, through "
			"Hits and changes of type\n");
	}

	// ---- Q15 ----

	// A Hit as a function of time: 48 partials from 40 Hz to 15 kHz at fixed phases, a 2 ms attack and a 40 ms decay
	std::vector<float> hit(const double _rate, const double _seconds)
	{
		std::vector<float> x(static_cast<size_t>(_seconds * _rate));
		for(size_t i = 0; i < x.size(); ++i)
		{
			const double t = static_cast<double>(i) / _rate;
			double v = 0;
			for(int k = 0; k < 48; ++k)
				v += std::sin(2 * bench::Pi * 40 * std::pow(375.0, k / 47.0) * t + 2.4 * k * k) / std::sqrt(48.0);
			x[i] = static_cast<float>(0.3 * v * std::min(t / 0.002, 1.0) * std::exp(-t / 0.04));
		}
		return x;
	}

	std::vector<double> render(const int _type, const int _lane, const double _rate, const double _seconds)
	{
		Filters f(_rate);
		const auto x = hit(_rate, _seconds);
		f.with(_type, [&](auto& _filter) { _filter.hit(_lane); });
		apply(f, _lane, laneSetting(_type, _lane));
		std::vector<double> y;
		for(size_t at = 0; at < x.size(); at += BlockSize)
		{
			Block block{};
			for(int n = 0; n < BlockSize && at + static_cast<size_t>(n) < x.size(); ++n)
				block.s[n][_lane] = x[at + static_cast<size_t>(n)];
			process(f, _type, block, static_cast<uint16_t>(1 << _lane));
			for(int n = 0; n < BlockSize; ++n)
				y.push_back(block.s[n][_lane]);
		}
		// As long in seconds at both rates, its end tapered over 10 ms: a filter still ringing there would leak into
		// every band
		y.resize(x.size());
		const auto taper = static_cast<size_t>(0.01 * _rate);
		for(size_t i = 0; i < taper; ++i)
			y[y.size() - 1 - i] *=
				0.5 - 0.5 * std::cos(bench::Pi * static_cast<double>(i) / static_cast<double>(taper));
		return y;
	}

	void rates()
	{
		constexpr double Seconds = 0.5;
		const char* names[]{"", "MULTI-MODE", "EQUALIZER", "LOWPASS 4", "COMB-", "LEGACY", "COMB+"};
		// Q15's 0.2 and 0.5 dB, but where the user took the measured bounds (ticket 09): MULTI-MODE at the top of its
		// cutoff (22 kHz), LOWPASS 4 resonant or near 16 kHz, the combs' loop interpolated at another fraction
		const double envBound[]{0, 0.55, 0.2, 0.3, 0.35, 0.2, 0.3};
		const double bandBound[]{0, 0.7, 0.5, 0.7, 1.15, 0.5, 1.6};
		bool pass = true;
		for(int type = 1; type <= 6; ++type)
		{
			double worstEnv = 0, worstBand = 0, worstAt = 0;
			int worstLane = 0;
			for(int l = 0; l < Lanes; ++l)
			{
				const auto a = render(type, l, 48000, Seconds), b = render(type, l, 44100, Seconds);
				const double env = bench::envelopeDiff(bench::envelope(a, 48000, 10), bench::envelope(b, 44100, 10));
				worstEnv = std::max(worstEnv, env);
				const auto ra = bench::bands(a, 48000, -17, 12), rb = bench::bands(b, 44100, -17, 12);
				const double loud = *std::max_element(ra.begin(), ra.end());
				double band = 0, at = 0;
				for(size_t k = 0; k < ra.size(); ++k)
					if(ra[k] >= loud * 1e-6 && std::abs(bench::db(rb[k] / ra[k])) > band)
					{
						band = std::abs(bench::db(rb[k] / ra[k]));
						at = 1000 * std::exp2((static_cast<int>(k) - 17) / 3.0);
					}
				if(env > envBound[type] || band > bandBound[type])
				{
					const auto s = laneSetting(type, l);
					std::printf("    %s lane %d (FREQ %d, p68 %d, p70 %d, kt %d, note %d): envelope %.3f dB, %.3f dB "
						"at %.0f Hz\n", names[type], l, s.freq, s.p68, s.p70, s.kt, s.note, env, band, at);
				}
				if(band > worstBand)
				{
					worstBand = band;
					worstAt = at;
					worstLane = l;
				}
			}
			std::printf("  Q15, %s: 44.1 against 48 kHz, 16 settings: 10 ms envelope within %.3f dB (bound %.2f), "
				"third octaves to 16 kHz within %.3f dB (bound %.2f; lane %d, %.0f Hz)\n", names[type], worstEnv,
				envBound[type], worstBand, bandBound[type], worstLane, worstAt);
			pass = pass && worstEnv <= envBound[type] && worstBand <= bandBound[type];
		}
		require(pass, "Q15: a filter at 44.1 kHz is off its 48 kHz sound");
	}

	// ---- the cost ----

	void cost()
	{
		const char* names[]{"", "MULTI-MODE", "EQUALIZER", "LOWPASS 4", "COMB-", "LEGACY", "COMB+"};
		// Held, and with FREQ moving every block as an LFO moves it, which runs every setter every block
		for(int type = 1; type <= 6; ++type)
			for(const bool moving : {false, true})
			{
				Filters f(44100);
				for(int l = 0; l < Lanes; ++l)
					apply(f, l, laneSetting(type, l));
				Block block{};
				double worst = 0, total = 0;
				constexpr int Rounds = 4000;
				for(int r = 0; r < Rounds; ++r)
				{
					for(int n = 0; n < BlockSize; ++n)
						for(int l = 0; l < Lanes; ++l)
							block.s[n][l] = static_cast<float>(0.5 * std::sin(0.01 * (r * BlockSize + n) + l));
					const auto t0 = std::chrono::steady_clock::now();
					if(moving)
						for(int l = 0; l < Lanes; ++l)
						{
							auto s = laneSetting(type, l);
							s.freq = 20 + (s.freq + r) % 100;
							apply(f, l, s);
						}
					process(f, type, block, 0xffff);
					const double us =
						std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
					worst = std::max(worst, us);
					total += us;
				}
				const double share = total / Rounds / (BlockSize / 44100.0 * 1e6) * 100;
				std::printf("  %s on 16 lanes at 44.1 kHz, %s: %.3f %% of a core, worst block %.1f us\n", names[type],
					moving ? "FREQ moving" : "held", share, worst);
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
		const bool found = vectors();
		cost();
		rates();
		if(!found)
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
