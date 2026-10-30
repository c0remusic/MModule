// A diagnostic, by hand, not a test: every value of every sound parameter of a fast machine against the exact port
// at 48 kHz, through the bench (stFastBench.h).
//
//	stFastSweep sweep MACHINE OUT.csv	MACHINE bd, sd, cy, pc, sv, cp, sy or sc
//	stFastSweep ab OUTDIR CASES.txt		A/B WAVs and an ABX page for the cases, one a line: MACHINE PARAM VALUE, or
//										MACHINE hitN (BD's bench Hits, PC CARBON's Hit 10)
//
// The sweep sets one parameter (the 8 of the Machine page, then the note) to each value 0-127, fine 0, the others at
// the machine's base sound, its test's Hit of all 64s with the drive and HOLD off, without retrigger or gate-off: 16
// values a render, 2 s from the onset. Per value: the 1 ms envelope's worst difference in dB where the exact port's
// is above -150 dB below its peak (digital silence left out), above -40 dB and above -60 dB, each with its time and
// the level there below the peak; the pitch in cents (bench::peakHz); the null (SNR, worst 10 ms, worst residual third
// octave). A value is chaotic where the exact port, its phases 1 LSB on, fails Q4's measures against itself
// (bench::chaoticSelf); there both engines are measured again with the machine's feedback table at 0 (none in PC
// CARBON and SY CHORD), the row flagged. A machine with noise (SD VINTAGE, CP VINTAGE) starts it from the exact port's
// seeds (fastHits). A value both engines play as exact silence passes, its row flagged "silent" and not measured
// (SY CHORD's TUNE from 118 at its base note: every unit past 2217 Hz from the trig, none ever sounds).
//
// SYNTAKT_SECTION7 names the user's OS 1.41 section 7; what is written is derived from it, so it must go outside the
// repository.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

#include "stFastBdModern.h"
#include "stFastBdModernHits.h"
#include "stFastBench.h"
#include "stFastCpVintage.h"
#include "stFastCyAlloy.h"
#include "stFastPcCarbon.h"
#include "stFastSdBasic.h"
#include "stFastSdVintage.h"
#include "stFastSyChord.h"
#include "stFastSyTone.h"
#include "stSection7.h"

namespace
{
	using namespace syntakt;
	using namespace syntakt::fast::bench;
	using fast::Lanes;

	constexpr double Seconds = 2;
	constexpr int Note = 8;	// the parameter index the note is swept as

	// A machine swept: its name, its id in the exact port, its base sound, what its tests' retrigger sets, its
	// units (which the nudges move), its feedback table and its parameters' names
	struct Machine
	{
		const char* name;
		int id;
		Hit base;
		std::vector<int> retrigger;
		int units;
		uint32_t feedbackTable;	// 0: none
		const char* params[9];
	};

	const Machine Machines[] = {
		{"bd", 0, {{88, 64, 64, 64, 64, 0, 0, 64}, {}, 36, false, false}, {3, 5}, fast::BdModern::Units, 0x4001fc00,
			{"tune", "u2level", "transient", "sweep", "contour", "drive", "hold", "decay", "note"}},
		{"sd", 1, {{64, 64, 64, 64, 64, 0, 0, 64}, {}, 60, false, false}, {3, 5}, fast::SdBasic::Units, 0x40026e38,
			{"tune", "ratios", "feedback", "sweep", "envelopes", "drive", "hold", "decay", "note"}},
		{"cy", 2, {{64, 64, 64, 64, 64, 0, 0, 64}, {}, 48, false, false}, {3, 5}, fast::CyAlloy::Units, 0x40020c00,
			{"tune", "spread", "depth", "ratio", "levels", "drive", "hold", "decay", "note"}},
		{"pc", 3, {{64, 64, 64, 64, 64, 0, 0, 64}, {}, 48, false, false}, {3, 5}, fast::PcCarbon::Units, 0,
			{"tune", "ratios", "depth", "sweep", "levels", "drive", "hold", "decay", "note"}},
		{"sv", 6, {{64, 64, 64, 64, 64, 0, 0, 64}, {}, 48, false, false}, {3, 5}, fast::SdVintage::Units, 0x40039a38,
			{"tune", "ratios", "balance", "sweep", "u3decay", "drive", "hold", "decay", "note"}},
		{"cp", 7, {{64, 64, 64, 64, 64, 0, 0, 64}, {}, 48, false, false}, {3, 5}, fast::CpVintage::Units, 0x4003b438,
			{"tune", "tone", "balance", "period", "tonedecay", "drive", "hold", "decay", "note"}},
		{"sy", 4, {{64, 64, 64, 64, 64, 0, 0, 64}, {}, 48, false, false}, {3, 5}, fast::SyTone::Units, 0x40021404,
			{"tune", "ratio", "index", "feedback", "moddecay", "drive", "hold", "decay", "note"}},
		{"sc", 5, {{64, 64, 64, 64, 64, 0, 0, 64}, {}, 48, false, false}, {3, 5}, fast::SyChord::Units, 0,
			{"tune", "balance", "shape", "wave", "cutoff", "drive", "hold", "decay", "note"}},
	};

	// A machine's type, handed to what withMachine calls
	template<typename M>
	struct Type
	{
		using type = M;
	};

	// _f(Type<the fast machine of the exact port's id _id>{}); an id no fast machine has aborts
	template<typename F>
	decltype(auto) withMachine(const int _id, F&& _f)
	{
		switch(_id)
		{
		case 0: return _f(Type<fast::BdModern>{});
		case 1: return _f(Type<fast::SdBasic>{});
		case 2: return _f(Type<fast::CyAlloy>{});
		case 3: return _f(Type<fast::PcCarbon>{});
		case 4: return _f(Type<fast::SyTone>{});
		case 5: return _f(Type<fast::SyChord>{});
		case 6: return _f(Type<fast::SdVintage>{});
		case 7: return _f(Type<fast::CpVintage>{});
		default:
			std::fprintf(stderr, "no fast machine has the id %d\n", _id);
			std::abort();
		}
	}

	const Machine* machine(const std::string& _name)
	{
		for(const auto& m : Machines)
			if(_name == m.name)
				return &m;
		return nullptr;
	}

	Hit withValue(Hit _h, const int _param, const int _value)
	{
		if(_param == Note)
			_h.note = _value;
		else
			_h.value[_param] = static_cast<uint8_t>(_value);
		return _h;
	}

	// _h on the 16 lanes of machine _m
	HitSet lanesOf(const Machine& _m, const Hit& _h)
	{
		HitSet s{{}, _m.retrigger, _m.id};
		std::fill(std::begin(s.hit), std::end(s.hit), _h);
		return s;
	}

	using Renders = std::vector<std::vector<double>>;

	template<typename Fast>
	Renders renderFast(const HitSet& _hits, const bool _noFeedback)
	{
		return fastHits<Fast>(48000, Seconds, _hits, [&](Fast& _e)
		{
			if(_noFeedback)
				_e.testWithoutChaos();
		});
	}

	Renders renderExact(const std::vector<int32_t>& _s7, const HitSet& _hits, const Nudge& _nudge)
	{
		return exactHits(_s7, Seconds, _nudge, _hits);
	}

	struct Worst
	{
		double db = 0, ms = 0, level = 0;
	};

	// The envelopes' largest difference where _ref's is above its peak + _floor dB, where it is and how loud
	Worst envelopeWorst(const std::vector<double>& _ref, const std::vector<double>& _test, const double _floor)
	{
		const double peak = *std::max_element(_ref.begin(), _ref.end());
		Worst w;
		for(size_t k = 0; k < std::min(_ref.size(), _test.size()); ++k)
		{
			if(_ref[k] <= 0 || _ref[k] < peak * std::pow(10.0, _floor / 10))
				continue;
			const double d = std::abs(db(_test[k] / _ref[k]));
			if(d > w.db)
				w = {d, static_cast<double>(k), db(_ref[k] / peak)};
		}
		return w;
	}

	struct Row
	{
		Worst all, at40, at60;
		double cents, snr, local, band, envAbs, cells, selfEnvAbs, selfCells;
		std::vector<PhaseBlind> increments;	// the exact port against its increment nudges, as played
		bool chaotic, silent;
	};

	Row measure(const std::vector<double>& _ref, const std::vector<double>& _test)
	{
		Row r{};
		const auto er = envelope(_ref, 48000), et = envelope(_test, 48000);
		r.all = envelopeWorst(er, et, -150);
		r.at40 = envelopeWorst(er, et, -40);
		r.at60 = envelopeWorst(er, et, -60);
		const auto m = measureHit(_ref, _test);
		r.cents = m.cents;
		r.snr = m.snr;
		r.band = m.band;
		r.local = m.local;
		r.envAbs = m.envAbs;
		r.cells = m.cells;
		return r;
	}

	template<typename Fast>
	Row measureCase(const std::vector<int32_t>& _s7, const Machine& _m, const HitSet& _hits, const int _l,
		const Renders& _ref, const Renders& _self, const std::vector<Renders>& _increments, const Renders& _test,
		Renders* _detRef, Renders* _detTest)
	{
		// A value both engines play as exact silence passes, unmeasured: Q4's measures read a peak it has not
		const auto silence = [](const std::vector<double>& _x)
		{
			return std::all_of(_x.begin(), _x.end(), [](const double _v) { return _v == 0; });
		};
		if(silence(_ref[_l]) && silence(_test[_l]))
		{
			Row r{};
			r.silent = true;
			r.increments.assign(static_cast<size_t>(incrementNudgeCount(_m.units)), PhaseBlind{});
			return r;
		}
		const auto self = phaseBlind(_ref[_l], _self[_l]);	// what chaoticSelf reads, without the null's FFTs
		const bool chaotic = chaoticSelf(self);
		Row r;
		if(chaotic && _m.feedbackTable)
		{
			if(_detRef->empty())
			{
				*_detRef = renderExact(withoutTable(_s7, _m.feedbackTable), _hits, Nudge{});
				*_detTest = renderFast<Fast>(_hits, true);
			}
			r = measure((*_detRef)[_l], (*_detTest)[_l]);
		}
		else
		{
			r = measure(_ref[_l], _test[_l]);
		}
		r.selfEnvAbs = self.envAbs;
		r.selfCells = self.cells;
		r.increments.assign(static_cast<size_t>(incrementNudgeCount(_m.units)), PhaseBlind{});
		if(!chaotic)	// a chaotic value is judged with its feedback at 0, not by tolerance
			for(int i = 0; i < incrementNudgeCount(_m.units); ++i)
				r.increments[i] = phaseBlind(_ref[_l], _increments[static_cast<size_t>(i)][_l]);
		r.chaotic = chaotic;
		return r;
	}

	void printRow(std::FILE* _f, const Machine& _m, const int _param, const int _value, const Row& _r)
	{
		std::fprintf(_f, "%s,%s,%d,%s,%.4f,%.0f,%.1f,%.4f,%.0f,%.1f,%.4f,%.0f,%.1f,%.3f,%.1f,%.1f,%.1f,%.2f,%.3f,%.2f,"
			"%.3f", _m.name, _m.params[_param], _value,
			_r.silent ? "silent" : _r.chaotic ? (_m.feedbackTable ? "feedback0" : "chaotic") : "no", _r.all.db, _r.all.ms, _r.all.level,
			_r.at40.db, _r.at40.ms, _r.at40.level, _r.at60.db, _r.at60.ms, _r.at60.level, _r.cents, _r.snr, _r.local,
			_r.band, _r.envAbs, _r.cells, _r.selfEnvAbs, _r.selfCells);
		for(const auto& n : _r.increments)
			std::fprintf(_f, ",%.2f,%.3f", n.envAbs, n.cells);
		std::fprintf(_f, "\n");
	}

	template<typename Fast>
	int sweep(const std::vector<int32_t>& _s7, const Machine& _m, const std::string& _out)
	{
		std::FILE* f = std::fopen(_out.c_str(), "w");
		if(!f)
			return 2;
		std::fprintf(f, "machine,param,value,chaotic,env_db,env_ms,env_level_db,env40_db,env40_ms,env40_level_db,"
			"env60_db,env60_ms,env60_level_db,cents,snr_db,worst10ms_db,worst_band_db,"
			"env_abs_db,cells30_db,self_env_abs_db,self_cells_db");
		for(int i = 0; i < incrementNudgeCount(_m.units); ++i)
			std::fprintf(f, ",inc%d_env_abs_db,inc%d_cells_db", i, i);
		std::fprintf(f, "\n");
		for(int p = 0; p <= Note; ++p)
			for(int v0 = 0; v0 < 128; v0 += Lanes)
			{
				HitSet hits = lanesOf(_m, _m.base);
				for(int l = 0; l < Lanes; ++l)
					hits.hit[l] = withValue(_m.base, p, v0 + l);
				const auto ref = renderExact(_s7, hits, Nudge{});
				const auto self = renderExact(_s7, hits, phaseNudge(1, _m.units));
				// The increment nudges only where a value is not chaotic: they give its tolerance
				bool anyNot = false;
				for(int l = 0; l < Lanes; ++l)
					anyNot |= !chaoticSelf(phaseBlind(ref[l], self[l]));
				std::vector<Renders> increments;
				for(int i = 0; anyNot && i < incrementNudgeCount(_m.units); ++i)
					increments.push_back(renderExact(_s7, hits, incrementNudge(i, _m.units)));
				const auto test = renderFast<Fast>(hits, false);
				Renders detRef, detTest;
				for(int l = 0; l < Lanes; ++l)
					printRow(f, _m, p, v0 + l,
						measureCase<Fast>(_s7, _m, hits, l, ref, self, increments, test, &detRef, &detTest));
				std::fflush(f);
				std::printf("%s %s %d-%d\n", _m.name, _m.params[p], v0, v0 + Lanes - 1);
			}
		std::fclose(f);
		return 0;
	}

	// ---- A/B ----

	// PC CARBON's Hit 10 of stFastPcCarbonTest
	const Hit PcHit10 = {{64, 70, 50, 50, 70, 0, 0, 96}, {128, 77, 200, 0, 33, 0, 0, 0}, 48, false, false};

	struct Case
	{
		const Machine* m;
		std::string name;
		Hit hit;
	};

	template<typename Fast>
	void renderCase(const std::vector<int32_t>& _s7, const Case& _c, const std::string& _dir, std::ostringstream& _json,
		bool& _first)
	{
		const Machine& m = *_c.m;
		const HitSet hits = lanesOf(m, _c.hit);
		const auto ref = renderExact(_s7, hits, Nudge{});
		const auto self = renderExact(_s7, hits, phaseNudge(1, m.units));
		std::vector<Renders> increments;
		for(int i = 0; i < incrementNudgeCount(m.units); ++i)
			increments.push_back(renderExact(_s7, hits, incrementNudge(i, m.units)));
		const auto test = renderFast<Fast>(hits, false);
		Renders detRef, detTest;
		const Row r = measureCase<Fast>(_s7, m, hits, 0, ref, self, increments, test, &detRef, &detTest);
		std::printf("%-22s chaotic %s; envelope %.3f dB at %.0f ms (%.1f dB below the peak); above -40 dB %.3f dB at "
			"%.0f ms (%.1f dB); above -60 dB %.3f dB at %.0f ms (%.1f dB); %+.3f cents; null %.1f dB, 10 ms %.1f dB, "
			"third octave %.1f dB\n", _c.name.c_str(), r.chaotic ? "yes" : "no", r.all.db, r.all.ms, -r.all.level,
			r.at40.db, r.at40.ms, -r.at40.level, r.at60.db, r.at60.ms, -r.at60.level, r.cents, r.snr, r.local, r.band);
		const auto tolerance = worstOf(r.increments);
		std::printf("%-22s fast %.1f dB re peak, cells %.3f dB; the exact port against its increment nudges at worst "
			"%.1f dB, %.3f dB\n", _c.name.c_str(), r.envAbs, r.cells, tolerance.envAbs, tolerance.cells);
		// The real sound on both sides, under one gain: the louder at -3 dBFS
		double peak = 0;
		for(const auto* x : {&ref[0], &test[0]})
			for(const double v : *x)
				peak = std::max(peak, std::abs(v));
		const double gain = std::pow(10.0, -3 / 20.0) / peak;
		writeWav(_dir + _c.name + "-A.wav", ref[0], 48000, gain, 0.05);	// after 50 ms of silence
		writeWav(_dir + _c.name + "-B.wav", test[0], 48000, gain, 0.05);
		char value[256];
		std::snprintf(value, sizeof(value), "envelope %.3f dB above -60 dB, null %.1f dB", r.at60.db, r.snr);
		_json << (_first ? "" : ",\n") << "  {\"id\": \"" << _c.name << "\", \"group\": \"exact-vs-fast\", \"a\": \""
			<< _c.name << "-A.wav\", \"b\": \"" << _c.name << "-B.wav\", \"value\": \"" << value << "\"}";
		_first = false;
	}

	int ab(const std::vector<int32_t>& _s7, const std::string& _dir, const std::string& _cases)
	{
		std::vector<Case> cases;
		std::ifstream in(_cases);
		for(std::string line; std::getline(in, line);)
		{
			std::istringstream s(line);
			std::string name, what;
			int value = -1;
			if(!(s >> name >> what))
				continue;
			const Machine* m = machine(name);
			if(!m)
				return 2;
			if(what.rfind("hit", 0) == 0)
			{
				const int h = std::stoi(what.substr(3));
				cases.push_back({m, name + "-" + what, m->id == 3 ? PcHit10 : BdModernHits.hit[h]});
				continue;
			}
			s >> value;
			const auto* at = std::find_if(m->params, m->params + 9, [&](const char* _p) { return what == _p; });
			if(at == m->params + 9)
				return 2;
			const int p = static_cast<int>(at - m->params);
			cases.push_back({m, name + "-" + what + "-" + std::to_string(value), withValue(m->base, p, value)});
		}
		std::ostringstream json;
		json << "[\n";
		bool first = true;
		for(const auto& c : cases)
			withMachine(c.m->id, [&](auto _m) { renderCase<typename decltype(_m)::type>(_s7, c, _dir, json, first); });
		json << "\n]\n";
		std::ofstream(_dir + "pairs.json") << json.str();
		std::ifstream page(ST_FAST_AB_PAGE);
		std::string html((std::istreambuf_iterator<char>(page)), std::istreambuf_iterator<char>());
		const auto at = html.find("__PAIRS__");
		if(at == std::string::npos)
			return 1;
		html.replace(at, 9, json.str());
		std::ofstream(_dir + "index.html") << html;
		return 0;
	}
}

namespace
{
	// A 24-bit mono WAV that writeWav wrote, as samples in [-1, 1)
	std::vector<double> readWav(const std::string& _path)
	{
		std::ifstream f(_path, std::ios::binary);
		const std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		const auto at = raw.find("data");
		std::vector<double> x;
		if(at == std::string::npos)
			return x;
		for(size_t i = at + 8; i + 2 < raw.size(); i += 3)
		{
			int32_t v = static_cast<uint8_t>(raw[i]) | static_cast<uint8_t>(raw[i + 1]) << 8 |
				static_cast<uint8_t>(raw[i + 2]) << 16;
			if(v >= 1 << 23)
				v -= 1 << 24;
			x.push_back(v / 8388608.0);
		}
		return x;
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	// stFastSweep wav DIR NAME...: Q4's envelope and short-time spectrum of NAME-B.wav against NAME-A.wav, no firmware
	if(_argc >= 4 && std::string(_argv[1]) == "wav")
	{
		for(int i = 3; i < _argc; ++i)
		{
			const std::string p = std::string(_argv[2]) + "/" + _argv[i];
			const auto a = readWav(p + "-A.wav"), b = readWav(p + "-B.wav");
			if(a.empty() || b.size() < a.size())
			{
				std::printf("%-14s B shorter than A, or no A: not comparable\n", _argv[i]);
				return 1;
			}
			std::printf("%-14s envelope %.1f dB re its peak, third octave cells %.2f dB\n", _argv[i],
				envelopeAbs(a, b), cellMax(a, b));
		}
		return 0;
	}
	const char* path = std::getenv("SYNTAKT_SECTION7");
	if(_argc < 4 || !path || !*path)
	{
		std::printf("usage: stFastSweep sweep MACHINE OUT.csv | ab OUTDIR CASES.txt, with SYNTAKT_SECTION7 set\n");
		return 2;
	}
	const auto s7 = loadSection7(path);
	if(s7.empty())
	{
		std::printf("%s is not OS 1.41's section 7\n", path);
		return 1;
	}
	g_section7 = s7;	// fastHits seeds a machine with noise from it
	// Firmware-derived output never goes into the repository: OUT.csv (sweep), OUTDIR (ab), OUT.txt (sens)
	const std::string mode = _argv[1], target = mode == "ab" ? _argv[2] : _argv[3];
	std::error_code error;
	const auto out = std::filesystem::weakly_canonical(target, error).generic_string();
	const auto repo = std::filesystem::weakly_canonical(ST_FAST_REPO, error).generic_string();
	if(out.compare(0, repo.size(), repo) == 0 && (out.size() == repo.size() || out[repo.size()] == '/'))
	{
		std::printf("%s is inside the repository: give a place outside it\n", out.c_str());
		return 2;
	}
	if(mode == "ab")
		return ab(s7, std::string(_argv[2]) + "/", _argv[3]);
	const Machine* m = machine(_argv[2]);
	// stFastSweep nudge MACHINE OUT.txt PARAM VALUE...: Q4's measures of the exact port against itself 1 LSB on (the
	// chaos classification's nudge) beside the fast engine's against it, as rendered and with the feedback table at 0
	if(mode == "nudge" && m && _argc >= 6)
	{
		const auto* name = std::find_if(m->params, m->params + 9,
			[&](const char* _p) { return _argv[4] == std::string(_p); });
		std::FILE* f = std::fopen(_argv[3], "w");
		for(int i = 5; i < _argc; ++i)
		{
			const int param = static_cast<int>(name - m->params);
			const HitSet hits = lanesOf(*m, withValue(m->base, param, std::stoi(_argv[i])));
			const auto line = [&](const char* _what, const std::vector<int32_t>& _s7, const bool _noChaos)
			{
				const auto ref = renderExact(_s7, hits, Nudge{});
				const auto self = renderExact(_s7, hits, phaseNudge(1, m->units));
				const Renders test = withMachine(m->id,
					[&](auto _m) { return renderFast<typename decltype(_m)::type>(hits, _noChaos); });
				const auto s = measureHit(ref[0], self[0]), t = measureHit(ref[0], test[0]);
				std::fprintf(f, "%s %s %s, %s: exact 1 LSB on %.1f dB re peak, cells %.3f dB, SNR %.1f, 10 ms %.1f dB; "
					"fast %.1f dB re peak, cells %.3f dB, SNR %.1f dB\n", _argv[2], _argv[4], _argv[i], _what, s.envAbs,
					s.cells, s.snr, s.local, t.envAbs, t.cells, t.snr);
			};
			line("as played", s7, false);
			if(m->feedbackTable)
				line("feedback 0", withoutTable(s7, m->feedbackTable), true);
		}
		std::fclose(f);
		return 0;
	}
	// stFastSweep sens MACHINE OUT.txt TABLE ENTRY PARAM VALUE: how the exact port answers its own table word moved by
	// 1 to 10000 LSB, Q4's measures of the moved render against the unmoved one (is a setting ill-conditioned?)
	if(mode == "sens" && m && _argc >= 8)
	{
		const auto table = static_cast<uint32_t>(std::stoul(_argv[4], nullptr, 16));
		const size_t at = ((table - 0x40000400) >> 2) + std::stoul(_argv[5]);
		const auto* name = std::find_if(m->params, m->params + 9,
			[&](const char* _p) { return _argv[6] == std::string(_p); });
		const HitSet hits = lanesOf(*m, withValue(m->base, static_cast<int>(name - m->params), std::stoi(_argv[7])));
		const auto ref = renderExact(s7, hits, Nudge{});
		std::FILE* f = std::fopen(_argv[3], "w");
		for(const int d : {1, 10, 100, 1000, 2595, 10000})
		{
			auto moved = s7;
			moved[at] += d;
			const auto x = renderExact(moved, hits, Nudge{});
			std::fprintf(f, "%s %s %s, word %+d LSB: envelope %.1f dB re peak, cells %.3f dB, SNR %.1f dB\n", _argv[2],
				_argv[6], _argv[7], d, envelopeAbs(ref[0], x[0]), cellMax(ref[0], x[0]), null(ref[0], x[0], 48000).snr);
		}
		std::fclose(f);
		return 0;
	}
	if(mode != "sweep" || !m)
		return 2;
	return withMachine(m->id, [&](auto _m) { return sweep<typename decltype(_m)::type>(s7, *m, _argv[3]); });
}
