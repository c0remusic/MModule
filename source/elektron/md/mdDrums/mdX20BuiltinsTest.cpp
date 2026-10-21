// X.20's built-in Machines on the engine (the map of X.20's built-in Machines, ticket 04: GND-SW's first sound), where
// MD_X20_OS names the user's own X.20 OS file (never in this repository): with GND-SW installed from it, every
// machine of OS 1.63 gives the same samples on every Track as without it; GND-SW on a Track sounds where OS 1.63's id 4
// is silent; and, where MD_X20_WAV names a file, GND-SW's Hits with its eight parameters swept, for the user's ears.

#include "mdDrumsTestSupport.h"

#include "MdEngine.h"
#include "X20Builtins.h"
#include "X20Os.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace
{
	using mdDrums::test::require;

	struct Rig
	{
		md::engine::Engine raw;
		md::engine::Engine::Output out;

		Rig(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank, const std::shared_ptr<const md::fw::DspImage>& _x20)
			: raw(_os.firmware, _os.osImage)
		{
			raw.loadRomBank(_bank);
			mdDrums::applyEngineDefaults(raw.host());
			if(_x20)
				raw.installX20Builtins(_x20, 1u << md::engine::x20::GndSw);
		}
	};

	// Every audio machine of OS 1.63 but RAM-R and RAM-P, sixteen at a time on the sixteen Tracks, struck twice: the
	// voices' samples with X.20's GND-SW installed and without
	void othersUnchanged(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank,
		const std::shared_ptr<const md::fw::DspImage>& _x20)
	{
		Rig probe(_os, _bank, nullptr);
		std::vector<uint8_t> ids;
		for(const auto& m : probe.raw.os().machines())
			if(md::engine::isAudioMachine(m.id) && m.name.rfind("RAM", 0) != 0 && m.id != md::engine::x20::GndSw)
				ids.push_back(m.id);
		size_t differ = 0, blocks = 0;
		for(size_t first = 0; first < ids.size(); first += 16)
		{
			Rig with(_os, _bank, _x20), without(_os, _bank, nullptr);
			for(int t = 0; t < 16; ++t)
			{
				const auto id = ids[(first + static_cast<size_t>(t)) % ids.size()];
				with.raw.host().setMachine(t, id);
				without.raw.host().setMachine(t, id);
			}
			for(int b = 0; b < 60; ++b)
			{
				if(b == 1 || b == 30)
					for(int t = 0; t < 16; ++t)
					{
						with.raw.host().trigger(t, 100);
						without.raw.host().trigger(t, 100);
					}
				require(with.raw.render(with.out) && without.raw.render(without.out), "the engine faulted");
				differ += with.raw.voiceBlock() != without.raw.voiceBlock();
				++blocks;
			}
		}
		std::printf("OS 1.63's %zu machines with X.20's GND-SW installed: %zu of %zu blocks differ\n", ids.size(), differ,
			blocks);
		require(differ == 0, "installing GND-SW changed another machine's samples");
	}

	// Track 1 on id 4, struck: GND-SW installed sounds; without it OS 1.63 has no machine 4 and the Track keeps its own
	double peakOf(Rig& _rig)
	{
		_rig.raw.host().setMachine(0, md::engine::x20::GndSw);
		double peak = 0.0;
		for(int b = 0; b < 200; ++b)
		{
			if(b == 1)
				_rig.raw.host().trigger(0, 127);
			require(_rig.raw.render(_rig.out), "the engine faulted");
			for(const auto s : _rig.raw.voiceBlock()[0])
				peak = std::max(peak, std::abs(s / 8388608.0));
		}
		return peak;
	}

	void writeWav(const char* _path, const std::vector<float>& _samples)
	{
		std::ofstream f(_path, std::ios::binary);
		const auto put32 = [&](const uint32_t _v) { f.write(reinterpret_cast<const char*>(&_v), 4); };
		const auto put16 = [&](const uint16_t _v) { f.write(reinterpret_cast<const char*>(&_v), 2); };
		const auto bytes = static_cast<uint32_t>(_samples.size() * 2);
		f.write("RIFF", 4); put32(36 + bytes); f.write("WAVEfmt ", 8); put32(16); put16(1); put16(1);
		put32(44100); put32(44100 * 2); put16(2); put16(16); f.write("data", 4); put32(bytes);
		for(const auto s : _samples)
			put16(static_cast<uint16_t>(static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f)));
	}

	// GND-SW's Hits for the ears: at its defaults, then each parameter at 0, 64 and 127 in turn, half a second each, the
	// Track's own output after its effects
	void listen(const char* _path, const md::fw::FlashOs& _os, const md::fw::RomBank& _bank,
		const std::shared_ptr<const md::fw::DspImage>& _x20)
	{
		Rig rig(_os, _bank, _x20);
		auto& host = rig.raw.host();
		host.setMachine(0, md::engine::x20::GndSw);
		const auto& info = md::engine::x20::info(md::engine::x20::GndSw);
		std::vector<float> samples;
		const auto hit = [&]
		{
			host.trigger(0, 110);
			for(int b = 0; b < 44100 / 2 / 32; ++b)
			{
				require(rig.raw.render(rig.out), "the engine faulted");
				for(const auto s : rig.out.tracks[0])
					samples.push_back(static_cast<float>(s / 8388608.0));
			}
		};
		hit();
		std::printf("GND-SW for the ears: defaults");
		for(int p = 0; p < 8; ++p)
		{
			for(const int v : {0, 64, 127})
			{
				host.setParam(0, p, v);
				hit();
			}
			host.setParam(0, p, info.defaults[static_cast<size_t>(p)]);
			std::printf(", %s 0 64 127", info.names[static_cast<size_t>(p)]);
		}
		writeWav(_path, samples);
		std::printf(" into %s (%.1f s)\n", _path, samples.size() / 44100.0);
	}
}

int main()
{
	return mdDrums::test::run("mdX20BuiltinsTest", [](const std::vector<uint8_t>& _flash)
	{
		const auto* path = std::getenv("MD_X20_OS");
		if(!path || !*path)
		{
			std::printf("MD_X20_OS unset: X.20's built-in Machines not played\n");
			return;
		}
		const auto x20 = md::fw::parseX20Os(md::fw::readFile(path));
		const auto voice = std::make_shared<const md::fw::DspImage>(x20.voice);
		const auto os = md::fw::loadFirmwareFromFlash(_flash);
		const auto bank = md::fw::loadRomBankFromFlash(_flash);

		Rig with(os, bank, voice), without(os, bank, nullptr);
		const auto sounding = peakOf(with);
		peakOf(without);
		std::printf("GND-SW on Track 1: peak %.3f; without X.20 the Track keeps machine %d\n", sounding,
			without.raw.host().machine(0));
		require(sounding > 0.05 && with.raw.host().machine(0) == md::engine::x20::GndSw
			&& without.raw.host().machine(0) != md::engine::x20::GndSw,
			"GND-SW is silent, or OS 1.63 took a machine 4");

		othersUnchanged(os, bank, voice);

		if(const auto* wav = std::getenv("MD_X20_WAV"); wav && *wav)
			listen(wav, os, bank, voice);
	});
}
