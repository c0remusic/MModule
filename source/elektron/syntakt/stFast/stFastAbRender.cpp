// The listening test of ticket 05 (elektron-drums-v1): BD MODERN's Hits as WAVs for a blind ABX page, by hand.
//
//	stFastAbRender OUTDIR [--round2]
//
// SYNTAKT_SECTION7 names the user's OS 1.41 section 7; everything written is derived from it, so OUTDIR must be
// outside the repository. Writes into OUTDIR, for a few Hits of stFastBdModernTest (stFastBench.h):
//	the exact port and the fast engine at 48 kHz;
//	the fast render degraded around Q3 and Q4's thresholds as first proposed: white noise at an SNR of 60, 50, 40,
//		30 dB; a gain growing in dB from 0 at the onset to 0.1, 0.3, 1, 3 dB at the end; TUNE's fine raised for
//		about 1, 3, 10 cents;
//	the cases stFastBdModernTest excuses, the fast engine at 44.1 and at 48 kHz;
// mono 24-bit WAVs of 2 s from the onset, each Hit's files under one gain (its loudest at -3 dBFS) and a 20 ms
// fade at the end; pairs.json, every pair with what the test measures of B against A; index.html, the ABX page
// (stFastAbPage.html with the pairs).
// --round2, the second round: the noise group alone at an SNR of 65, 70, 75, 80 dB, a control pair per Hit (B the
// same as A, at a random place among them, to measure the guessing rate), every file after 50 ms of silence.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>

#include "stFastBench.h"
#include "stSection7.h"

namespace
{
	using namespace syntakt;
	using namespace syntakt::fast::bench;
	using fast::Lanes;

	constexpr double Seconds = 2;
	using Hit16 = std::vector<std::vector<double>>;

	// The 16 Hits from their onsets on the fast engine at _fs (48 or 44.1 kHz, Host), TUNE's fine raised by _fine on
	// every Track
	Hit16 renderFast(const double _fs, const int _fine = 0)
	{
		fast::Host<fast::BdModern> e(static_cast<float>(_fs));
		const int samples = static_cast<int>(Seconds * _fs);
		// The onset, 32 samples after the trig at 48 kHz and 29.4 at 44.1: there from the 29th, 0.4 sample early
		const int skip = e.latency() + static_cast<int>(fast::BlockSize * _fs / 48000);
		Hit16 out(Lanes);
		std::array<std::array<float, fast::BlockSize>, Lanes> buf{};
		std::array<float*, Lanes> ptr{};
		for(int t = 0; t < Lanes; ++t)
			ptr[t] = buf[t].data();
		for(int b = 0; b < (samples + skip) / fast::BlockSize + 1; ++b)
		{
			events(e, b, _fs);
			if(b == 0 && _fine)
				for(int t = 0; t < Lanes; ++t)
					e.setParameter(t, 0, Hits[t].value[0], static_cast<uint8_t>(Hits[t].fine[0] + _fine));
			e.render(ptr.data());
			for(int t = 0; t < Lanes; ++t)
				out[t].insert(out[t].end(), buf[t].begin(), buf[t].end());
		}
		for(auto& o : out)
			o = std::vector<double>(o.begin() + skip, o.begin() + skip + samples);
		return out;
	}

	Hit16 renderExact(const std::vector<int32_t>& _s7)
	{
		Exact16 e(_s7);
		const int samples = static_cast<int>(Seconds * 48000);
		Hit16 out(Lanes);
		for(int b = 0; b < samples / BlockSize + 2; ++b)
		{
			events(e, b, 48000);
			e.render();
			for(int t = 0; t < Lanes; ++t)
				for(int n = 0; n < BlockSize; ++n)
					out[t].push_back(q31(e.out(t)[n]));
		}
		for(auto& o : out)
			o = std::vector<double>(o.begin() + BlockSize, o.begin() + BlockSize + samples);
		return out;
	}

	// _x plus white noise at an SNR of _snr dB over the whole Hit
	std::vector<double> withNoise(const std::vector<double>& _x, const double _snr, const unsigned _seed)
	{
		std::mt19937 rng(_seed);
		std::uniform_real_distribution<double> u(-1, 1);
		std::vector<double> n(_x.size());
		for(auto& v : n)
			v = u(rng);
		const double scale = std::sqrt(energy(_x) / energy(n) * std::pow(10.0, -_snr / 10));
		std::vector<double> y(_x);
		for(size_t i = 0; i < y.size(); ++i)
			y[i] += scale * n[i];
		return y;
	}

	// _x under a gain growing in dB linearly from 0 at the onset to _db at the end
	std::vector<double> withGain(const std::vector<double>& _x, const double _db)
	{
		std::vector<double> y(_x);
		for(size_t i = 0; i < y.size(); ++i)
			y[i] *= std::pow(10.0, _db * static_cast<double>(i) / static_cast<double>(y.size()) / 20);
		return y;
	}

	struct File
	{
		std::string name;
		std::vector<double> x;
		double fs;
		int hit;
	};

	struct Pair
	{
		std::string id, group, a, b, value;
		double size;	// how large the difference is, to put the largest first
	};

	void putLe(std::ofstream& _f, const uint32_t _v, const int _bytes)
	{
		for(int i = 0; i < _bytes; ++i)
			_f.put(static_cast<char>(_v >> (8 * i)));
	}

	// 24-bit PCM, mono, after _pre seconds of silence, _gain and a 20 ms fade at the end
	void writeWav(const std::string& _path, const std::vector<double>& _hit, const double _fs, const double _gain,
		const double _pre)
	{
		std::vector<double> x(static_cast<size_t>(_pre * _fs), 0.0);
		x.insert(x.end(), _hit.begin(), _hit.end());
		std::ofstream f(_path, std::ios::binary);
		const uint32_t data = static_cast<uint32_t>(x.size()) * 3, rate = static_cast<uint32_t>(_fs);
		f.write("RIFF", 4);
		putLe(f, 36 + data, 4);
		f.write("WAVEfmt ", 8);
		putLe(f, 16, 4);
		putLe(f, 1, 2);
		putLe(f, 1, 2);
		putLe(f, rate, 4);
		putLe(f, rate * 3, 4);
		putLe(f, 3, 2);
		putLe(f, 24, 2);
		f.write("data", 4);
		putLe(f, data, 4);
		const size_t fade = static_cast<size_t>(0.02 * _fs);
		for(size_t i = 0; i < x.size(); ++i)
		{
			const double left = static_cast<double>(x.size() - i);
			const double g = _gain * (left < fade ? left / static_cast<double>(fade) : 1);
			const auto v = static_cast<int32_t>(std::lround(std::clamp(x[i] * g, -1.0, 1.0) * 8388607));
			putLe(f, static_cast<uint32_t>(v), 3);
		}
	}

	std::string format(const char* _fmt, const double _v)
	{
		char s[64];
		std::snprintf(s, sizeof(s), _fmt, _v);
		return s;
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const char* path = std::getenv("SYNTAKT_SECTION7");
	if(_argc < 2 || !path || !*path)
	{
		std::printf("usage: stFastAbRender OUTDIR, with SYNTAKT_SECTION7 set\n");
		return 2;
	}
	const auto s7 = loadSection7(path);
	if(s7.empty())
	{
		std::printf("%s is not OS 1.41's section 7\n", path);
		return 1;
	}
	// Firmware-derived output never goes into the repository
	std::error_code error;
	const auto out = std::filesystem::weakly_canonical(_argv[1], error).generic_string();
	const auto repo = std::filesystem::weakly_canonical(ST_FAST_REPO, error).generic_string();
	if(out.compare(0, repo.size(), repo) == 0 && (out.size() == repo.size() || out[repo.size()] == '/'))
	{
		std::printf("%s is inside the repository: give a folder outside it\n", out.c_str());
		return 2;
	}
	const std::string dir = std::string(_argv[1]) + "/";
	const bool round2 = _argc > 2 && std::string(_argv[2]) == "--round2";

	// The second round plays the fast engine at 48 kHz alone
	const auto exact = round2 ? Hit16() : renderExact(s7);
	const auto fast48 = renderFast(48000), fast44 = round2 ? Hit16() : renderFast(44100);
	const int fines[] = {3, 8, 26};	// 1/256 semitone each: about 1, 3 and 10 cents
	std::map<int, Hit16> tuned;
	for(const int f : fines)
		if(!round2)
			tuned[f] = renderFast(48000, f);

	std::vector<File> files;
	std::vector<Pair> exactPairs, noise, envelopes, pitch, rate, controls;
	const auto add = [&](const std::string& _name, const std::vector<double>& _x, const double _fs, const int _hit)
	{
		files.push_back({_name, _x, _fs, _hit});
		return _name + ".wav";
	};

	// A clean low kick, a punchy one with drive and sweep, a chaotic one (TRANSIENT 127), the FM one
	for(const int h : {0, 4, 2, 12})
	{
		const std::string p = "hit" + std::to_string(h) + "_";
		const auto& ref = fast48[h];
		const std::string a = add(p + "fast48", ref, 48000, h);
		const auto snrs = round2 ? std::vector<double>{65, 70, 75, 80} : std::vector<double>{30, 40, 50, 60};
		for(const double snr : snrs)
		{
			const auto y = withNoise(ref, snr, 1000u + static_cast<unsigned>(h));
			const std::string b = add(p + "noise" + std::to_string(static_cast<int>(snr)), y, 48000, h);
			noise.push_back({"h" + std::to_string(h) + "-noise" + std::to_string(static_cast<int>(snr)), "noise", a, b,
				format("SNR %.1f dB", null(ref, y, 48000).snr), -snr});
		}
		if(round2)
		{
			const std::string b = add(p + "control", ref, 48000, h);
			controls.push_back({"h" + std::to_string(h) + "-control", "control", a, b, "B = A", 0});
			continue;
		}
		const std::string x = add(p + "exact48", exact[h], 48000, h);
		const double snr = null(exact[h], ref, 48000).snr;
		exactPairs.push_back({"h" + std::to_string(h) + "-exact", "exact-vs-fast", x, a, format("SNR %.1f dB", snr) +
			format(", enveloppe %.3f dB", envelopeDiff(envelope(exact[h], 48000), envelope(ref, 48000))), -snr});
		for(const double g : {3.0, 1.0, 0.3, 0.1})
		{
			const auto y = withGain(ref, g);
			const std::string b = add(p + "env" + format("%.1f", g), y, 48000, h);
			envelopes.push_back({"h" + std::to_string(h) + "-env" + format("%.1f", g), "envelope", a, b,
				format("enveloppe %.3f dB", envelopeDiff(envelope(ref, 48000), envelope(y, 48000))), g});
		}
		for(auto f = std::rbegin(fines); f != std::rend(fines); ++f)
		{
			const auto& y = tuned[*f][static_cast<size_t>(h)];
			const std::string b = add(p + "fine" + std::to_string(*f), y, 48000, h);
			pitch.push_back({"h" + std::to_string(h) + "-fine" + std::to_string(*f), "pitch", a, b,
				format("%+.2f cents", 1200 * std::log2(peakHz(y, 48000) / peakHz(ref, 48000))), *f + 0.0});
		}
	}

	// The cases stFastBdModernTest excuses at 44.1 kHz
	for(const int h : {9, 11, 12, 15})
	{
		if(round2)
			break;
		const std::string p = "hit" + std::to_string(h) + "_";
		const std::string a = h == 12 ? p + "fast48.wav" : add(p + "fast48", fast48[h], 48000, h);
		const std::string b = add(p + "fast44", fast44[h], 44100, h);
		const double e = envelopeDiff(envelope(fast48[h], 48000), envelope(fast44[h], 44100));
		rate.push_back({"h" + std::to_string(h) + "-rate", "rate", a, b, format("enveloppe %.3f dB", e), e});
	}

	// One gain per Hit, its loudest file at -3 dBFS
	std::map<int, double> peak;
	for(const auto& f : files)
		for(const double v : f.x)
			peak[f.hit] = std::max(peak[f.hit], std::abs(v));
	for(const auto& f : files)
		writeWav(dir + f.name + ".wav", f.x, f.fs, std::pow(10.0, -3 / 20.0) / peak[f.hit], round2 ? 0.05 : 0);

	// Within each group from the largest difference to the smallest
	std::ostringstream json;
	json << "[\n";
	bool first = true;
	for(const auto* group : {&noise, &envelopes, &pitch, &exactPairs, &rate})
	{
		auto pairs = *group;
		std::stable_sort(pairs.begin(), pairs.end(), [](const Pair& _l, const Pair& _r) { return _l.size > _r.size; });
		if(group == &noise)	// the controls at random places among the noise pairs (a fixed seed: the same each run)
		{
			std::mt19937 rng(5);
			for(const auto& c : controls)
				pairs.insert(pairs.begin() + static_cast<std::ptrdiff_t>(rng() % (pairs.size() + 1)), c);
		}
		for(const auto& p : pairs)
		{
			json << (first ? "" : ",\n") << "  {\"id\": \"" << p.id << "\", \"group\": \"" << p.group << "\", \"a\": \""
				<< p.a << "\", \"b\": \"" << p.b << "\", \"value\": \"" << p.value << "\"}";
			first = false;
			std::printf("%-14s %-14s %-22s %-22s %s\n", p.id.c_str(), p.group.c_str(), p.a.c_str(), p.b.c_str(),
				p.value.c_str());
		}
	}
	json << "\n]\n";
	std::ofstream(dir + "pairs.json") << json.str();

	std::ifstream page(ST_FAST_AB_PAGE);
	std::string html((std::istreambuf_iterator<char>(page)), std::istreambuf_iterator<char>());
	const auto at = html.find("__PAIRS__");
	if(at == std::string::npos)
	{
		std::printf("%s holds no __PAIRS__\n", ST_FAST_AB_PAGE);
		return 1;
	}
	html.replace(at, 9, json.str());
	std::ofstream(dir + "index.html") << html;
	std::printf("%zu files, %s\n", files.size(), (dir + "index.html").c_str());
	return 0;
}
