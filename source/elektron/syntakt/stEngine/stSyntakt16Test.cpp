// syntakt::Syntakt with 16 tracks against the 8-track module, which stSyntaktTest checks against the original
// firmware: two machines side by side, A (tracks 1-8) and B (tracks 9-16), B's sends into A's delay and reverb.
//
//   - with tracks 9-16 idle, the first 14 outputs are the 8-track module's on stSyntaktTest's scenario, sample for
//     sample, and B's are silent;
//   - with tracks 1-8 idle and a second pattern on 9-16 (the scenario 8 tracks up, 777 samples later, 5 semitones
//     higher), each stem of 9-16 is the 8-track module's stem of that pattern, and the returns are its returns: B's
//     sends alone through A's effects are one machine's;
//   - with both patterns, every stem is still its pattern's alone (stems are before the effects), B's buses are as
//     with B alone (they hold B's tracks only), and the returns are the same from run to run and differ from either
//     pattern's alone.
//
// SYNTAKT_SECTION3, SYNTAKT_SECTION7 and SYNTAKT_SAMPLES as stSyntaktTest reads them; exit 77 without them.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "stMainMemory.h"
#include "stSection7.h"
#include "stSyntakt.h"
#include "stSyntaktScenario.h"
#include "stVoice.h"

namespace
{
	using syntakt::Syntakt;
	using namespace syntakt::test;

	// A module's outputs over the Total samples, channel after channel
	struct Rendered
	{
		int channels = 0;
		std::vector<int32_t> samples;

		const int32_t* channel(const int _c) const { return samples.data() + Total * static_cast<size_t>(_c); }
	};

	// The calls through the interface: between calls, renders of uneven lengths, as a host's blocks
	Rendered play(Syntakt& _st, const std::vector<Call>& _calls)
	{
		Rendered out;
		out.channels = _st.outputs();
		out.samples.assign(Total * static_cast<size_t>(out.channels), 0);
		std::vector<int32_t> chunk(128 * static_cast<size_t>(out.channels));
		std::vector<int32_t*> outputs(static_cast<size_t>(out.channels));
		size_t position = 0;
		uint32_t lcg = 1;
		auto renderTo = [&](const size_t _end)
		{
			while(position < _end)
			{
				lcg = lcg * 1664525u + 1013904223u;
				const size_t n = std::min<size_t>(_end - position, 1 + (lcg >> 24) % 100);
				for(int c = 0; c < out.channels; ++c)
					outputs[static_cast<size_t>(c)] = chunk.data() + 128 * c;
				_st.render(outputs.data(), n);
				for(int c = 0; c < out.channels; ++c)
					std::copy_n(outputs[static_cast<size_t>(c)], n, out.samples.data() + Total * c + position);
				position += n;
			}
		};
		for(const auto& c : _calls)
		{
			renderTo(c.at);
			apply(_st, c);
		}
		renderTo(Total);
		return out;
	}

	bool trackCall(const Call& _c)
	{
		return _c.kind != Kind::Tempo && _c.kind != Kind::Fx;
	}

	// The second pattern: each track call of _calls _tracks tracks up, 777 samples later, its notes 5 semitones
	// higher; the tempo and the effects' calls as they are (none when !_shared)
	std::vector<Call> second(const std::vector<Call>& _calls, const int _tracks, const bool _shared)
	{
		std::vector<Call> s;
		for(Call c : _calls)
		{
			if(trackCall(c))
			{
				c.at += 777;
				c.a += _tracks;
				if(c.kind == Kind::On)
					c.b += 5;
			}
			else if(!_shared)
			{
				continue;
			}
			if(c.at < Total)
				s.push_back(c);
		}
		std::stable_sort(s.begin(), s.end(), [](const Call& _x, const Call& _y) { return _x.at < _y.at; });
		return s;
	}

	std::vector<Call> merged(std::vector<Call> _a, const std::vector<Call>& _b)
	{
		_a.insert(_a.end(), _b.begin(), _b.end());
		std::stable_sort(_a.begin(), _a.end(), [](const Call& _x, const Call& _y) { return _x.at < _y.at; });
		return _a;
	}

	double peakDb(const int32_t* _v)
	{
		int32_t peak = 0;
		for(size_t i = 0; i < Total; ++i)
			peak = std::max(peak, _v[i] == INT32_MIN ? INT32_MAX : std::abs(_v[i]));
		return peak ? 20 * std::log10(peak / 2147483648.0) : -999;
	}

	// One channel against another, sample for sample: prints the result and whether it holds; _sound: _got must
	// also be above -90 dBFS
	bool same(const char* _what, const int32_t* _got, const int32_t* _want, const bool _sound = true)
	{
		int differ = 0;
		size_t first = 0;
		for(size_t i = 0; i < Total; ++i)
			if(_got[i] != _want[i] && !differ++)
				first = i;
		const double db = peakDb(_got);
		std::printf("  %-44s %5d differ, peak %7.1f dBFS", _what, differ, db);
		if(differ)
			std::printf(", first at %zu: %08x, expected %08x", first, static_cast<uint32_t>(_got[first]),
				static_cast<uint32_t>(_want[first]));
		std::printf("\n");
		return differ == 0 && (!_sound || db > -90);
	}

	bool silent(const char* _what, const int32_t* _got)
	{
		const std::vector<int32_t> zero(Total, 0);
		return same(_what, _got, zero.data(), false);
	}

	// The samples where _a and _b differ
	size_t differences(const int32_t* _a, const int32_t* _b)
	{
		size_t n = 0;
		for(size_t i = 0; i < Total; ++i)
			n += _a[i] != _b[i];
		return n;
	}

	std::string name(const char* _format, const int _n)
	{
		char text[64];
		std::snprintf(text, sizeof text, _format, _n);
		return text;
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);

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

	auto run = [&](const int _tracks, const std::vector<Call>& _calls)
	{
		Syntakt st(section3, section7, _tracks);
		st.setSamples(&samples);
		return play(st, _calls);
	};

	const auto first = scenario();
	const auto secondAlone = second(first, 0, true);		// on an 8-track module's tracks 1-8
	const auto secondOnB = second(first, 8, true);			// on tracks 9-16
	const Rendered a8 = run(8, first), b8 = run(8, secondAlone);
	bool ok = a8.channels == Syntakt::Outputs;

	std::printf("tracks 9-16 idle, against the 8-track module on stSyntaktTest's scenario:\n");
	const Rendered idle = run(16, first);
	ok = ok && idle.channels == Syntakt::Outputs16;
	const char* const names[Syntakt::Outputs] = {"stem 1", "stem 2", "stem 3", "stem 4", "stem 5", "stem 6", "stem 7",
		"stem 8", "returns L", "returns R", "direct L", "direct R", "FX bus L", "FX bus R"};
	for(int c = 0; c < Syntakt::Outputs; ++c)
		ok &= same(names[c], idle.channel(c), a8.channel(c));
	for(int c = Syntakt::Outputs; c < Syntakt::Outputs16; ++c)
		ok &= silent(name("output %d (B, idle)", c).c_str(), idle.channel(c));

	std::printf("tracks 1-8 idle, the second pattern on 9-16, against the 8-track module playing it:\n");
	const Rendered onB = run(16, secondOnB);
	for(int t = 0; t < Syntakt::Tracks; ++t)
		ok &= same(name("stem %d", 9 + t).c_str(), onB.channel(Syntakt::Stem9 + t), b8.channel(Syntakt::Stem1 + t));
	ok &= same("returns L (B's sends through A's effects)", onB.channel(Syntakt::ReturnsLeft),
		b8.channel(Syntakt::ReturnsLeft));
	ok &= same("returns R", onB.channel(Syntakt::ReturnsRight), b8.channel(Syntakt::ReturnsRight));
	for(int t = 0; t < Syntakt::Tracks; ++t)
		ok &= silent(name("stem %d (idle)", 1 + t).c_str(), onB.channel(Syntakt::Stem1 + t));

	std::printf("both patterns, A's on 1-8 and the second on 9-16:\n");
	const auto both = merged(first, second(first, 8, false));
	const Rendered mixed = run(16, both), again = run(16, both);
	for(int t = 0; t < Syntakt::Tracks; ++t)
		ok &= same(name("stem %d (as the first pattern alone)", 1 + t).c_str(), mixed.channel(Syntakt::Stem1 + t),
			a8.channel(Syntakt::Stem1 + t));
	for(int t = 0; t < Syntakt::Tracks; ++t)
		ok &= same(name("stem %d (as the second pattern alone)", 9 + t).c_str(), mixed.channel(Syntakt::Stem9 + t),
			b8.channel(Syntakt::Stem1 + t));
	for(int c = Syntakt::DirectLeftB; c <= Syntakt::FxRightB; ++c)
		ok &= same(name("B's bus %d (as with B alone)", c - Syntakt::DirectLeftB).c_str(), mixed.channel(c),
			onB.channel(c), false);
	for(int c = 0; c < Syntakt::Outputs16; ++c)
		ok &= same(name("output %d, a second run", c).c_str(), again.channel(c), mixed.channel(c), false);
	for(const int c : {Syntakt::ReturnsLeft, Syntakt::ReturnsRight})
	{
		const size_t fromA = differences(mixed.channel(c), a8.channel(c));
		const size_t fromB = differences(mixed.channel(c), b8.channel(c));
		std::printf("  returns %c: %zu samples differ from the first pattern's alone, %zu from the second's, peak "
			"%.1f dBFS\n", c == Syntakt::ReturnsLeft ? 'L' : 'R', fromA, fromB, peakDb(mixed.channel(c)));
		ok = ok && fromA > 0 && fromB > 0 && peakDb(mixed.channel(c)) > -90;
	}

	std::printf(ok ? "PASS\n" : "FAIL\n");
	return ok ? 0 : 1;
}
