// syntakt::Syntakt end to end against the original firmware: a scenario played through the module's interface alone
// (machines on several tracks, notes with velocities and lengths, knobs, locks, the delay's and the reverb's sends,
// the tempo, a level), and the same calls applied to the original CPU #1's memory by tools/syntakt3.py (outside the
// repository), which runs the original block segment, the original voice CPU and the original mix block after block
// and writes what they output. Every output channel must match sample for sample, and sound.
//
// SYNTAKT_SECTION3, SYNTAKT_SECTION7 and SYNTAKT_SAMPLES name the user's OS 1.41 sections and the sample set SP
// TWINSHOT plays (twinshot_samples.bin); SYNTAKT_SCENARIO the expected outputs syntakt3.py wrote. They hold
// firmware-derived data and stay outside the repository. Exit 77 without them.
//
//     stSyntaktTest --log CALLS.txt    writes the scenario's calls, which syntakt3.py reads, and exits
//     stSyntaktTest --wav OUT.wav      also writes the eight stems mixed with the returns, to listen to
//     stSyntaktTest --raw OUT.bin      also writes the 14 outputs raw (int32, channel after channel), to compare builds
//
// It also measures the time from a noteOn to the first sample of its stem, at several offsets within a block.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "stMainMemory.h"
#include "stSection7.h"
#include "stSyntakt.h"
#include "stVoice.h"

namespace
{
	using syntakt::Syntakt;

	constexpr size_t Total = 24000;		// 750 blocks, half a second
	constexpr int Channels = Syntakt::Outputs;

	const char* const ChannelNames[Channels] = {"stem 1", "stem 2", "stem 3", "stem 4", "stem 5", "stem 6", "stem 7",
		"stem 8", "returns L", "returns R", "direct L", "direct R", "FX bus L", "FX bus R"};

	enum class Kind
	{
		Tempo, Machine, Parameter, Fx, Level, Lock, On, Off
	};

	struct Call
	{
		size_t at;		// the sample the call is made at
		Kind kind;
		int a = 0, b = 0, c = 0;
		double bpm = 0;
	};

	std::vector<Call> scenario()
	{
		std::vector<Call> s;
		auto call = [&](const size_t _at, const Kind _kind, const int _a = 0, const int _b = 0, const int _c = 0)
		{
			s.push_back({_at, _kind, _a, _b, _c});
		};

		// The effects within the half second: a short delay with feedback, a short pre-delay; the reverb's return on
		// the direct bus, the delay's on the FX bus (its default); tracks 1-4 on the direct bus, 5-8 on the FX bus
		s.push_back({0, Kind::Tempo, 0, 0, 0, 132.5});
		call(0, Kind::Fx, Syntakt::DelayTime, 0x0400);
		call(0, Kind::Fx, Syntakt::DelayFeedback, 0x5000);
		call(0, Kind::Fx, Syntakt::ReverbPreDelay, 0x0800);
		call(0, Kind::Fx, Syntakt::ReverbDecay, 0x5800);
		call(0, Kind::Fx, Syntakt::ReverbRouting, 0);
		for(int t = 0; t < 4; ++t)
			call(0, Kind::Parameter, t, Syntakt::Routing, 0);
		call(0, Kind::Parameter, 0, Syntakt::DelaySend, 0x6000);
		call(0, Kind::Parameter, 2, Syntakt::ReverbSend, 0x7000);
		call(5, Kind::Parameter, 4, Syntakt::DelaySend, 0x5000);
		call(5, Kind::Parameter, 4, Syntakt::ReverbSend, 0x5000);
		call(40, Kind::Parameter, 5, Syntakt::ReverbSend, 0x6400);
		call(40, Kind::Parameter, 7, Syntakt::DelaySend, 0x4800);
		call(70, Kind::Parameter, 5, Syntakt::Pan, 0x2000);

		// Hits on every track, at offsets that fall anywhere in a block, each with its velocity and length; some end
		// in the block they start in, some run over the next hit of their track
		const int notes[] = {36, 48, 60, 55, 43, 67, 72, 52, 40, 64, 58, 47};
		const int velocities[] = {100, 64, 127, 90, 30, 110, 75, 120};
		const size_t lengths[] = {10, 200, 900, 2500, 64, 31, 1500, 6000, 400};
		for(int k = 0; k < 64; ++k)
		{
			const int t = (k * 3) % 8;
			const size_t at = 37 + 360 * static_cast<size_t>(k) + static_cast<size_t>((k * 7) % 29);
			call(at, Kind::On, t, notes[k % 12], velocities[(k + k / 8) % 8]);
			call(at + lengths[k % 9], Kind::Off, t);
		}
		call(0, Kind::On, 6, 60, 127);
		call(0, Kind::Off, 6);

		// Locks for track 1's second hit (2944): TUNE and the filter's frequency; its third (5822) goes back
		call(1450, Kind::Lock, 0, Syntakt::Tune, 0x5000);
		call(1450, Kind::Lock, 0, Syntakt::FilterFrequency, 0x3000);

		// Knobs: track 5's filter swept down, track 2's tune, track 3's level, track 8's decay
		for(int k = 0; k < 24; ++k)
			call(4000 + 333 * static_cast<size_t>(k), Kind::Parameter, 4, Syntakt::FilterFrequency, 0x7f00 - 0x280 * k);
		call(4011, Kind::Parameter, 4, Syntakt::FilterResonance, 0x5000);
		call(9000, Kind::Parameter, 1, Syntakt::Tune, 0x3000);
		call(7000, Kind::Level, 2, 0x5000);
		call(8200, Kind::Parameter, 7, Syntakt::Decay, 0x2000);

		// The other four machines, then the tempo
		call(12000, Kind::Machine, 1, 1);		// SD BASIC
		call(12000, Kind::Machine, 3, 3);		// PC CARBON
		call(12013, Kind::Machine, 6, 38);		// SY SWARM
		call(12013, Kind::Machine, 7, 44);		// SP TWINSHOT
		s.push_back({15000, Kind::Tempo, 0, 0, 0, 150});

		// The notes still sounding at the end are not ended
		s.erase(std::remove_if(s.begin(), s.end(), [](const Call& _c) { return _c.at >= Total; }), s.end());
		std::stable_sort(s.begin(), s.end(), [](const Call& _x, const Call& _y) { return _x.at < _y.at; });
		return s;
	}

	void apply(Syntakt& _st, const Call& _c)
	{
		switch(_c.kind)
		{
		case Kind::Tempo:		_st.setTempo(_c.bpm); break;
		case Kind::Machine:		_st.setMachine(_c.a, _c.b); break;
		case Kind::Parameter:	_st.setParameter(_c.a, _c.b, static_cast<uint16_t>(_c.c)); break;
		case Kind::Fx:			_st.setFxParameter(_c.a, static_cast<uint16_t>(_c.b)); break;
		case Kind::Level:		_st.setLevel(_c.a, static_cast<uint16_t>(_c.b)); break;
		case Kind::Lock:		_st.lock(_c.a, _c.b, static_cast<uint16_t>(_c.c)); break;
		case Kind::On:			_st.noteOn(_c.a, _c.b, _c.c); break;
		case Kind::Off:			_st.noteOff(_c.a); break;
		}
	}

	// One line per call, as syntakt3.py reads them: the sample it is made at, its name, its arguments
	std::string line(const Call& _c)
	{
		static const char* const names[] = {"tempo", "machine", "param", "fx", "level", "lock", "on", "off"};
		char text[96];
		if(_c.kind == Kind::Tempo)
			std::snprintf(text, sizeof text, "%zu tempo %ld", _c.at, std::lround(_c.bpm * 120));
		else
			std::snprintf(text, sizeof text, "%zu %s %d %d %d", _c.at, names[static_cast<int>(_c.kind)], _c.a, _c.b, _c.c);
		return text;
	}

	// The scenario through the interface: between calls, renders of uneven lengths, as a host's blocks
	std::vector<int32_t> play(Syntakt& _st, const std::vector<Call>& _calls)
	{
		std::vector<int32_t> out(Channels * Total);
		std::vector<int32_t> chunk(Channels * 128);
		int32_t* outputs[Channels];
		size_t position = 0, next = 0;
		uint32_t lcg = 1;
		auto renderTo = [&](const size_t _end)
		{
			while(position < _end)
			{
				lcg = lcg * 1664525u + 1013904223u;
				const size_t n = std::min<size_t>(_end - position, 1 + (lcg >> 24) % 100);
				for(int c = 0; c < Channels; ++c)
					outputs[c] = chunk.data() + 128 * c;
				_st.render(outputs, n);
				for(int c = 0; c < Channels; ++c)
					std::copy_n(outputs[c], n, out.data() + Total * c + position);
				position += n;
			}
		};
		for(; next < _calls.size(); ++next)
		{
			renderTo(_calls[next].at);
			apply(_st, _calls[next]);
		}
		renderTo(Total);
		return out;
	}

	// syntakt3.py's "ST1E": u32 blocks, u32 channels, then per block each channel's 32 samples, little-endian
	bool readExpected(const std::string& _path, std::vector<int32_t>& _out)
	{
		const std::vector<uint8_t> blob = syntakt::readFile(_path);
		if(blob.size() < 12 || std::string(blob.begin(), blob.begin() + 4) != "ST1E")
			return false;
		auto u32 = [&](const size_t _at)
		{
			return uint32_t(blob[_at]) | uint32_t(blob[_at + 1]) << 8 | uint32_t(blob[_at + 2]) << 16 |
				uint32_t(blob[_at + 3]) << 24;
		};
		const uint32_t blocks = u32(4), channels = u32(8);
		if(channels != Channels || blocks * syntakt::BlockSize != Total || blob.size() != 12 + 4ull * Channels * Total)
			return false;
		_out.assign(Channels * Total, 0);
		for(uint32_t b = 0; b < blocks; ++b)
			for(uint32_t c = 0; c < Channels; ++c)
				for(uint32_t s = 0; s < syntakt::BlockSize; ++s)
					_out[Total * c + syntakt::BlockSize * b + s] = static_cast<int32_t>(
						u32(12 + 4 * ((static_cast<size_t>(b) * Channels + c) * syntakt::BlockSize + s)));
		return true;
	}

	// As stMainOracleTest reads it: "STS1", 64 directory entries (u32 length, u32 pointer), u32 base, u32 size, data
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

	// The eight stems summed with the returns, stereo 16-bit at 48 kHz, scaled to a peak of -1 dBFS
	bool writeWav(const std::string& _path, const std::vector<int32_t>& _out)
	{
		std::vector<double> mix(2 * Total);
		double peak = 1e-9;
		for(size_t i = 0; i < Total; ++i)
		{
			double stems = 0;
			for(int t = 0; t < 8; ++t)
				stems += _out[Total * t + i];
			mix[2 * i] = stems + _out[Total * Syntakt::ReturnsLeft + i];
			mix[2 * i + 1] = stems + _out[Total * Syntakt::ReturnsRight + i];
			peak = std::max({peak, std::abs(mix[2 * i]), std::abs(mix[2 * i + 1])});
		}
		std::ofstream f(_path, std::ios::binary);
		auto u32 = [&](const uint32_t _v) { f.write(reinterpret_cast<const char*>(&_v), 4); };
		auto u16 = [&](const uint16_t _v) { f.write(reinterpret_cast<const char*>(&_v), 2); };
		const uint32_t bytes = static_cast<uint32_t>(4 * Total);
		f.write("RIFF", 4);
		u32(36 + bytes);
		f.write("WAVEfmt ", 8);
		u32(16);
		u16(1);
		u16(2);
		u32(Syntakt::SampleRate);
		u32(Syntakt::SampleRate * 4);
		u16(4);
		u16(16);
		f.write("data", 4);
		u32(bytes);
		const double gain = 0.891 * 32767 / peak;
		for(const double v : mix)
			u16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(v * gain))));
		return static_cast<bool>(f);
	}

	// The first sample of _count a stem is not 0, or -1
	long firstSound(const std::vector<int32_t>& _stem)
	{
		for(size_t i = 0; i < _stem.size(); ++i)
			if(_stem[i])
				return static_cast<long>(i);
		return -1;
	}

	// The samples from a noteOn made _offset samples into a block to its stem's first sample, on a fresh module
	long latency(const syntakt::main::Section3& _s3, const std::vector<int32_t>& _s7, const int _track,
		const size_t _offset)
	{
		Syntakt st(_s3, _s7);
		std::vector<std::vector<int32_t>> buffers(Channels, std::vector<int32_t>(512));
		int32_t* outputs[Channels];
		for(int c = 0; c < Channels; ++c)
			outputs[c] = buffers[c].data();
		st.render(outputs, _offset);
		st.noteOn(_track, 60, 100);
		st.render(outputs, 512);
		return firstSound(buffers[static_cast<size_t>(_track)]);
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);

	std::string logPath, wavPath, rawPath;
	for(int i = 1; i + 1 < _argc; i += 2)
	{
		const std::string option = _argv[i];
		if(option == "--log")
			logPath = _argv[i + 1];
		else if(option == "--wav")
			wavPath = _argv[i + 1];
		else if(option == "--raw")
			rawPath = _argv[i + 1];
	}

	const auto calls = scenario();
	if(!logPath.empty())
	{
		std::ofstream f(logPath);
		for(const auto& c : calls)
			f << line(c) << '\n';
		f << Total << " end\n";
		std::printf("%zu calls, %zu samples -> %s\n", calls.size(), Total, logPath.c_str());
		return f ? 0 : 1;
	}

	const char* section3Path = std::getenv("SYNTAKT_SECTION3");
	const char* section7Path = std::getenv("SYNTAKT_SECTION7");
	const char* samplesPath = std::getenv("SYNTAKT_SAMPLES");
	const char* expectedPath = std::getenv("SYNTAKT_SCENARIO");
	if(!section3Path || !*section3Path || !section7Path || !*section7Path || !samplesPath || !*samplesPath ||
		!expectedPath || !*expectedPath)
	{
		std::printf("SKIP: set SYNTAKT_SECTION3, SYNTAKT_SECTION7, SYNTAKT_SAMPLES and SYNTAKT_SCENARIO\n");
		return 77;
	}
	const auto section3 = syntakt::main::loadSection3(section3Path);
	const auto section7 = syntakt::loadSection7(section7Path);
	syntakt::Samples samples;
	std::vector<int32_t> expected;
	if(section3.bytes.empty() || section7.empty() || !readSamples(samplesPath, samples) ||
		!readExpected(expectedPath, expected))
	{
		std::printf("FAIL: cannot read OS 1.41's sections 3 and 7, the samples or the expected outputs\n");
		return 1;
	}

	Syntakt st(section3, section7);
	st.setSamples(&samples);
	const auto out = play(st, calls);
	if(!rawPath.empty())
	{
		std::ofstream f(rawPath, std::ios::binary);
		f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(4 * out.size()));
		std::printf("%s the outputs, channel after channel, raw Q31 -> %s\n", f ? "wrote" : "FAIL: cannot write",
			rawPath.c_str());
	}

	bool ok = true;
	for(int c = 0; c < Channels; ++c)
	{
		int differ = 0;
		size_t first = 0;
		int32_t peak = 0;
		for(size_t i = 0; i < Total; ++i)
		{
			const int32_t got = out[Total * c + i], want = expected[Total * c + i];
			peak = std::max(peak, got == INT32_MIN ? INT32_MAX : std::abs(got));
			if(got != want && !differ++)
				first = i;
		}
		const double db = peak ? 20 * std::log10(peak / 2147483648.0) : -999;
		std::printf("%-10s %6zu samples, %5d differ, peak %7.1f dBFS", ChannelNames[c], Total, differ, db);
		if(differ)
			std::printf(", first at %zu: %08x, original %08x", first, static_cast<uint32_t>(out[Total * c + first]),
				static_cast<uint32_t>(expected[Total * c + first]));
		std::printf("\n");
		ok = ok && differ == 0 && db > -90;
	}

	if(!wavPath.empty() && !writeWav(wavPath, out))
	{
		std::printf("FAIL: cannot write %s\n", wavPath.c_str());
		ok = false;
	}

	// From a noteOn to its stem: the block it waits for, the voice CPU's block (a trig starts its voice in the block
	// after), the mix's (one block behind the voice CPU), then the machine's own start
	std::printf("latency, track 1 (BD MODERN) by offset in the block:");
	long lo = 1 << 30, hi = -1;
	for(const size_t offset : {0, 1, 8, 16, 24, 31})
	{
		const long l = latency(section3, section7, 0, offset);
		std::printf(" %zu: %ld", offset, l);
		if(l >= 0)
		{
			lo = std::min(lo, l);
			hi = std::max(hi, l);
		}
	}
	std::printf("; %ld to %ld samples, jitter %ld\n", lo, hi, hi - lo);
	std::printf("latency at offset 0 by track (default machines):");
	for(int t = 0; t < 8; ++t)
		std::printf(" %ld", latency(section3, section7, t, 0));
	std::printf("\n");

	std::printf(ok ? "PASS\n" : "FAIL\n");
	return ok ? 0 : 1;
}
