// X.20's built-in Machines on the engine (the map of X.20's built-in Machines, tickets 04 and 06), where MD_X20_OS names
// the user's own X.20 OS file (never in this repository): with the six installed from it, every machine of OS 1.63
// but GND-SN (whose number GND-SN-PRO takes, as under X.20) gives the same samples on every Track as without them;
// each of the six sounds, the three NFX hearing GND-SW on the Track before; mdDrums::Engine takes the NFX for effects
// on their sources and gives the six X.20's defaults; and, where MD_X20_WAV names a file prefix, each one's Hits with its
// eight parameters swept, for the user's ears.

#include "mdDrumsTestSupport.h"

#include "MdEngine.h"
#include "X20Builtins.h"
#include "X20Os.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace
{
	using mdDrums::test::require;
	namespace x20 = md::engine::x20;

	constexpr uint8_t kSix[] = {x20::GndSnPro, x20::GndSw, x20::GndPu, x20::NfxEv, x20::NfxCo, x20::NfxUc};
	const char* name(const uint8_t _id)
	{
		switch(_id)
		{
		case x20::GndSnPro:	return "GND-SN-PRO";
		case x20::GndSw:	return "GND-SW";
		case x20::GndPu:	return "GND-PU";
		case x20::NfxEv:	return "NFX-EV";
		case x20::NfxCo:	return "NFX-CO";
		default:			return "NFX-UC";
		}
	}

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
				raw.installX20Builtins(_x20, x20::kAll);
		}
	};

	// Every audio machine of OS 1.63 but RAM-R, RAM-P and the numbers the six take, sixteen at a time on the sixteen
	// Tracks, struck twice: the voices' samples with the six installed and without
	void othersUnchanged(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank,
		const std::shared_ptr<const md::fw::DspImage>& _x20)
	{
		Rig probe(_os, _bank, nullptr);
		std::vector<uint8_t> ids;
		for(const auto& m : probe.raw.os().machines())
			if(md::engine::isAudioMachine(m.id) && m.name.rfind("RAM", 0) != 0 && !x20::isBuiltin(m.id))
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
		std::printf("OS 1.63's %zu other machines with X.20's six installed: %zu of %zu blocks differ\n", ids.size(), differ,
			blocks);
		require(differ == 0, "installing X.20's built-ins changed another machine's samples");
	}

	// _id on Track 2 (an NFX hearing GND-SW on Track 1, struck with it), or on Track 1 alone: its voice's peak over 300
	// blocks after one Hit at _velocity; _params, when given, its eight settings
	double peak(Rig& _rig, const uint8_t _id)
	{
		auto& host = _rig.raw.host();
		const bool nfx = x20::hearsTracks(_id);
		const int track = nfx ? 1 : 0;
		if(nfx)
			host.setMachine(0, x20::GndSw);
		host.setMachine(track, _id);
		double p = 0.0;
		for(int b = 0; b < 300; ++b)
		{
			if(b == 1)
			{
				host.trigger(0, 127);
				if(nfx)
					host.trigger(track, 127);
			}
			require(_rig.raw.render(_rig.out), "the engine faulted");
			for(const auto s : _rig.raw.voiceBlock()[static_cast<size_t>(track)])
				p = std::max(p, std::abs(s / 8388608.0));
		}
		return p;
	}

	void writeWav(const std::string& _path, const std::vector<float>& _samples)
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

	// A built-in's Hits for the ears: at its defaults, then each parameter at 0, 64 and 127 in turn, half a second each,
	// its Track's own output after its effects (an NFX on Track 2 hearing GND-SW on Track 1, struck together)
	void listen(const std::string& _prefix, const uint8_t _id, const md::fw::FlashOs& _os, const md::fw::RomBank& _bank,
		const std::shared_ptr<const md::fw::DspImage>& _x20)
	{
		Rig rig(_os, _bank, _x20);
		auto& host = rig.raw.host();
		const bool nfx = x20::hearsTracks(_id);
		const int track = nfx ? 1 : 0;
		if(nfx)
			host.setMachine(0, x20::GndSw);
		host.setMachine(track, _id);
		const auto& info = x20::info(_id);
		std::vector<float> samples;
		const auto hit = [&]
		{
			host.trigger(0, 110);
			if(nfx)
				host.trigger(track, 110);
			for(int b = 0; b < 44100 / 2 / 32; ++b)
			{
				require(rig.raw.render(rig.out), "the engine faulted");
				for(const auto s : rig.out.tracks[static_cast<size_t>(track)])
					samples.push_back(static_cast<float>(s / 8388608.0));
			}
		};
		hit();
		for(int p = 0; p < 8; ++p)
		{
			for(const int v : {0, 64, 127})
			{
				host.setParam(track, p, v);
				hit();
			}
			host.setParam(track, p, info.defaults[static_cast<size_t>(p)]);
		}
		const auto path = _prefix + name(_id) + ".wav";
		writeWav(path, samples);
		std::printf("%s for the ears: defaults, then each of", name(_id));
		for(const auto* n : info.names)
			std::printf(" %s", n);
		std::printf(" at 0, 64, 127, into %s (%.1f s)\n", path.c_str(), samples.size() / 44100.0);
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
		const auto x20os = md::fw::parseX20Os(md::fw::readFile(path));
		const auto voice = std::make_shared<const md::fw::DspImage>(x20os.voice);
		const auto os = md::fw::loadFirmwareFromFlash(_flash);
		const auto bank = md::fw::loadRomBankFromFlash(_flash);

		// Each of the six sounds; without X.20, OS 1.63 has no machine 4 and the Track keeps its own
		for(const auto id : kSix)
		{
			Rig rig(os, bank, voice);
			const auto p = peak(rig, id);
			std::printf("%s on Track %d: peak %.3f\n", name(id), x20::hearsTracks(id) ? 2 : 1, p);
			require(p > 0.01 && rig.raw.host().machine(x20::hearsTracks(id) ? 1 : 0) == id, std::string(name(id)) + " is silent");
		}
		{
			Rig without(os, bank, nullptr);
			without.raw.host().setMachine(0, x20::GndSw);
			without.raw.host().trigger(0, 100);
			require(without.raw.render(without.out) && without.raw.host().machine(0) != x20::GndSw,
				"OS 1.63 took a machine 4");
		}

		// mdDrums::Engine: the NFX are effects on their sources, the six take X.20's defaults, GND-SN-PRO GND-SN's number
		{
			mdDrums::Engine engine(_flash);
			require(!engine.playsX20(x20::NfxCo) && !engine.isNeighbour(x20::NfxCo), "an X.20 built-in without X.20");
			engine.installX20(voice);
			for(const auto id : kSix)
			{
				const auto defaults = engine.machineDefaults(id);
				require(engine.playsX20(id) && engine.isNeighbour(id) == x20::hearsTracks(id) && defaults
					&& *defaults == x20::info(id).defaults, std::string(name(id)) + " is not offered as X.20 has it");
			}
			std::printf("mdDrums::Engine: the six installed, the three NFX effects on their sources, X.20's defaults\n");
		}

		othersUnchanged(os, bank, voice);

		if(const auto* wav = std::getenv("MD_X20_WAV"); wav && *wav)
			for(const auto id : kSix)
				listen(wav, id, os, bank, voice);
	});
}
