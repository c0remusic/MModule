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
// It also compares the module's kit with the firmware's parameter descriptors, read from section 3, and measures the
// time from a noteOn to the first sample of its stem, at several offsets within a block.

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
#include "stSyntaktScenario.h"
#include "stVoice.h"

namespace
{
	using syntakt::Syntakt;
	using namespace syntakt::test;

	constexpr int Channels = Syntakt::Outputs;

	const char* const ChannelNames[Channels] = {"stem 1", "stem 2", "stem 3", "stem 4", "stem 5", "stem 6", "stem 7",
		"stem 8", "returns L", "returns R", "direct L", "direct R", "FX bus L", "FX bus R"};

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

	// The firmware's choice of parameter descriptors, read from section 3 by its own rules, apart from the module's
	// lookup: 0x400a5e4c walks the 505 and each one it stores replaces the one before, in a table per kind of track
	// (keyed by internal id) and a table of machine parameters (keyed by group and id); 0x400a63ec looks in them
	class Firmware
	{
	public:
		struct Descriptor
		{
			int32_t group, id, min, max, value;
		};

		explicit Firmware(const syntakt::main::Section3& _section3) : m_section3(_section3)
		{
			constexpr uint32_t Descriptors = 0x4022d59c;
			for(uint32_t i = 0; i < 505; ++i)
			{
				const uint32_t d = Descriptors + 52 * i;
				m_descriptors.push_back({longAt(d), longAt(d + 4), longAt(d + 8), longAt(d + 12), longAt(d + 16)});
			}
		}

		const Descriptor& operator[](const int _index) const { return m_descriptors[static_cast<size_t>(_index)]; }

		// 0x400a63ec: the descriptor of internal id _id on machine _machine, 0 for none. Ids 17-26 are the machine's
		// own; the others its kind of track's table, one of two (0x400a63c4 says which) that 0x400a5c8e and 0x400a5d04
		// keep descriptors out of
		int ofTrack(const int _machine, const int _id) const
		{
			if(_id >= 17 && _id <= 26)
				return last([&](const int, const Descriptor& _d) { return _d.group == _machine && _d.id == _id; });
			const bool second = _machine == 35 || halfAt(0x4022c93a + 4 * static_cast<uint32_t>(_machine)) == 0xff0;
			return last([&](const int _i, const Descriptor& _d)
			{
				if(_d.id != _id || _i == 95 || _i == 105 || (second ? _i == 61 : _i == 62 || _i == 70 || _i == 71))
					return false;
				return (_d.group >= 45 && _d.group <= 48) || _d.group == 59 || _d.group == 60 || _d.group == 62;
			});
		}

		// 0x400b3c38, a Sound for machine _machine: ofTrack's descriptor but for AMP MODE on four machines
		int ofSound(const int _machine, const int _id) const
		{
			const int d = ofTrack(_machine, _id);
			const bool held = _machine == 4 || _machine == 5 || _machine == 20 || _machine == 37;
			return d == 84 && held ? 83 : d;
		}

		// The FX track's table (0x400a6300), which 0x400a5dd6 keeps the same two descriptors out of
		int ofFx(const int _id) const
		{
			return last([&](const int _i, const Descriptor& _d)
			{
				if(_d.id != _id || _i == 95 || _i == 105)
					return false;
				return (_d.group >= 49 && _d.group <= 54) || _d.group == 59 || _d.group == 60 || _d.group == 62;
			});
		}

		// A descriptor's default, as 0x400b3c38 writes it: 0 for none
		uint16_t value(const int _descriptor) const
		{
			return _descriptor ? static_cast<uint16_t>((*this)[_descriptor].value) : 0;
		}

	private:
		template<typename Stored> int last(const Stored& _stored) const
		{
			int found = 0;
			for(int i = 0; i < static_cast<int>(m_descriptors.size()); ++i)
				if(_stored(i, m_descriptors[static_cast<size_t>(i)]))
					found = i;
			return found;
		}

		uint32_t halfAt(const uint32_t _address) const
		{
			const uint8_t* b = m_section3.bytes.data() + (_address - m_section3.Base);
			return uint32_t(b[0]) << 8 | b[1];
		}

		int32_t longAt(const uint32_t _address) const
		{
			return static_cast<int32_t>(halfAt(_address) << 16 | halfAt(_address + 2));
		}

		const syntakt::main::Section3& m_section3;
		std::vector<Descriptor> m_descriptors;
	};

	// The module's kit against the firmware's descriptors, every word: the default kit (a Sound of each track's default
	// machine, the FX track's words), then a machine set, which writes ids 17-26 where it has a descriptor and nothing
	// else (0x40024db2), and every parameter's range on each machine
	bool defaultsMatch(const syntakt::main::Section3& _section3, const std::vector<int32_t>& _section7)
	{
		const Firmware firmware(_section3);
		Syntakt st(_section3, _section7);
		int words = 0, differ = 0;
		const auto check = [&](const char* _what, const int _track, const int _id, const int _got, const int _want)
		{
			++words;
			if(_got != _want && ++differ <= 12)
				std::printf("  %s, track %d, id %d: %04x, the firmware's %04x\n", _what, _track, _id,
					static_cast<unsigned>(_got), static_cast<unsigned>(_want));
		};

		std::vector<std::vector<int>> kit(Syntakt::Tracks);
		for(int t = 0; t < Syntakt::Tracks; ++t)
		{
			for(int id = 0; id < Syntakt::TrackParameterEnd; ++id)
			{
				kit[static_cast<size_t>(t)].push_back(st.parameter(t, id));
				check("default kit", t, id, st.parameter(t, id), firmware.value(firmware.ofSound(st.machine(t), id)));
			}
		}
		for(int id = 0; id < Syntakt::FxParameterEnd; ++id)
			check("default kit, FX track", 12, id, st.fxParameter(id), firmware.value(firmware.ofFx(id)));

		for(int machine = 0; machine <= 44; ++machine)
		{
			if(!st.isPorted(machine))
				continue;
			for(int t = 0; t < Syntakt::Tracks; ++t)
			{
				st.setMachine(t, machine);
				for(int id = 0; id < Syntakt::TrackParameterEnd; ++id)
				{
					const int d = id >= 17 && id <= 26 ? firmware.ofTrack(machine, id) : 0;
					check("machine set", t, id, st.parameter(t, id),
						d ? firmware.value(d) : kit[static_cast<size_t>(t)][static_cast<size_t>(id)]);
				}
			}
			for(int id = 1; id < Syntakt::TrackParameterEnd; ++id)
			{
				if(const int d = firmware.ofTrack(machine, id))
				{
					const auto range = st.parameterRange(machine, id);
					check("minimum", machine, id, range.min, static_cast<uint16_t>(firmware[d].min));
					check("maximum", machine, id, range.max, static_cast<uint16_t>(firmware[d].max));
				}
			}
		}
		for(int id = 1; id < Syntakt::FxParameterEnd; ++id)
		{
			if(const int d = firmware.ofFx(id))
			{
				const auto range = st.fxParameterRange(id);
				check("minimum, FX track", 12, id, range.min, static_cast<uint16_t>(firmware[d].min));
				check("maximum, FX track", 12, id, range.max, static_cast<uint16_t>(firmware[d].max));
			}
		}
		std::printf("defaults: %d words and ranges against section 3's descriptors, %d differ\n", words, differ);
		return differ == 0;
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
	const auto out = play(st, calls).samples;
	if(!rawPath.empty())
	{
		std::ofstream f(rawPath, std::ios::binary);
		f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(4 * out.size()));
		std::printf("%s the outputs, channel after channel, raw Q31 -> %s\n", f ? "wrote" : "FAIL: cannot write",
			rawPath.c_str());
	}

	bool ok = defaultsMatch(section3, section7);
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
