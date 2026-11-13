#include "edDrumsMachineFiles.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "edDrumsKits.h"
#include "edDrumsParameters.h"
#include "mdProtocol/mdmachines.h"

namespace elektron::drums::machineFiles
{
	namespace
	{
		using md::automation::sysex::MdKit;
		using md::automation::sysex::MasterEffect;

		// A Kit's 24 values of a Track, as OS 1.63 orders them
		enum MdValue { Amd = 8, Eqf = 10, Eqg, Fltf, Fltw, Fltq, Srr, Dist, Vol, Pan, Del, Rev, Lfod = 22 };

		constexpr uint8_t KitDump = 0x52, PatternDump = 0x67, SongDump = 0x69, GlobalDump = 0x50, SampleName = 0x73;

		juce::String trackName(const int _track)
		{
			return juce::String::formatted("T%02d", _track + 1);
		}

		juce::String mdName(const int _id)
		{
			if(const auto* m = md::machines::find(md::MachineModel::Machinedrum, static_cast<uint16_t>(_id)))
				return juce::String(std::string(m->name));
			return "machine " + juce::String(_id);
		}

		// The Tracks of _tracks, a bit each, as "T02, T05"
		juce::String tracksOf(const uint32_t _tracks)
		{
			juce::StringArray names;
			for(int t = 0; t < Tracks; ++t)
				if(_tracks & (1u << t))
					names.add(trackName(t));
			return names.joinIntoString(", ");
		}

		juce::String counted(const int _count, const char* _one, const char* _many)
		{
			return juce::String(_count) + " " + (_count == 1 ? _one : _many);
		}

		// The machine's mix gives a Track's left and right G (cos, sin) of the pan's angle, G = (LEV / 127)^2 x
		// (VOL / 127)^2 (HostModel::mixer: LEV^2 x VOL^2; Mixer::gains: the sine's first quarter at PAN x 64, PAN 127
		// at its end); its sends REV^2 and DEL^2 after those gains. Lockstep's gives (level / 127)^2 and its pan's
		// min(1, (127 - pan) / 63), min(1, pan / 64) (Engine's gainOf and panOf), its sends the same square after them.
		// The pan that keeps the machine's balance, and the level whose louder side is the machine's: the sends then
		// follow as they are
		struct Mix
		{
			int level, pan;
		};

		Mix mixOf(const int _level, const int _vol, const int _pan)
		{
			const int index = _pan >= 127 ? 8191 : _pan * 64;
			const double angle = juce::MathConstants<double>::halfPi * index / 8192;
			const double left = std::cos(angle), right = std::sin(angle);
			const double balance = right / left;	// left stays above 0: index 8191 at most
			const double pan = balance <= 1 ? 64 * balance : 127 - 63 / balance;
			const double level = _level * _vol / 127.0 * std::sqrt(std::max(left, right));
			return {juce::jlimit(0, 127, juce::roundToInt(level)), juce::jlimit(0, 127, juce::roundToInt(pan))};
		}
	}

	bool readSysex(const juce::File& _file, std::vector<FoundKit>& _kits, juce::StringArray& _notTaken)
	{
		const auto name = _file.getFileName();
		juce::MemoryBlock data;
		if(_file.getSize() > MostBytes || !_file.loadFileAsData(data))
		{
			_notTaken.add(name + ": not read, larger than " + juce::String(MostBytes >> 20) + " MB or unreadable");
			return false;
		}
		const auto* bytes = static_cast<const uint8_t*>(data.getData());
		const size_t size = data.getSize();

		// Each message between its F0 and its F7, counted by what it is
		std::map<juce::String, int> left;
		int kits = 0, refused = 0;
		for(size_t at = 0; at < size; ++at)
		{
			if(bytes[at] != 0xf0)
				continue;
			const auto* end = std::find(bytes + at + 1, bytes + size, uint8_t{0xf7});
			if(end == bytes + size)
			{
				++left["a message cut short"];
				break;
			}
			const md::automation::sysex::MessageView message(bytes + at, static_cast<size_t>(end - bytes) + 1 - at);
			at = static_cast<size_t>(end - bytes);

			const bool elektron = message.size() >= 8 && message[1] == 0x00 && message[2] == 0x20 && message[3] == 0x3c;
			if(elektron && message[4] == 0x02 && message[6] == KitDump)
			{
				auto kit = md::automation::sysex::parseMdKit(message);
				if(!kit)
					++refused;
				else if(!kit->isEmptySlot())
				{
					++kits;
					auto kitName = juce::String(kit->displayName()).trim();
					if(kitName.isEmpty())
						kitName = _file.getFileNameWithoutExtension() + " " + juce::String(kits);
					_kits.push_back({kitName, *kit});
				}
				continue;
			}
			if(elektron && message[4] == 0x02)
			{
				const auto command = message[6];
				++left[command == PatternDump ? "patterns: Lockstep's patterns are its own, none is read from a file of the machine"
					: command == SongDump ? "songs: Lockstep has none"
					: command == GlobalDump ? "global settings: Lockstep has none of the machine's"
					: command == SampleName ? "sample names, of samples not read here"
					: "other messages of the machine"];
				continue;
			}
			if(message.size() >= 5 && message[1] == 0x7e && (message[3] == 1 || message[3] == 2))
			{
				++left["sample dumps: load the samples as WAV, AIFF or FLAC files instead"];
				continue;
			}
			++left[elektron ? "messages of another machine, which Lockstep does not read"
				: "messages of another maker, which Lockstep does not read"];
		}
		if(refused > 0)
			_notTaken.add(name + ": " + counted(refused, "kit", "kits") + " not read, damaged or of another version");
		for(const auto& [what, count] : left)
			_notTaken.add(name + ": " + juce::String(count) + " x " + what);
		if(kits == 0 && refused == 0 && left.empty())
			_notTaken.add(name + ": holds nothing Lockstep reads");
		return kits > 0;
	}

	std::unique_ptr<juce::XmlElement> kitOf(const FoundKit& _found, const std::vector<Machine>& _machines,
		juce::StringArray& _notTaken)
	{
		const auto& kit = _found.kit;
		const auto choice = [&](const Machine& _m) -> int
		{
			const auto at = std::find(_machines.begin(), _machines.end(), _m);
			return at == _machines.end() ? -1 : static_cast<int>(at - _machines.begin());
		};
		const auto said = [&](const juce::String& _what) { _notTaken.add(_found.name + ": " + _what); };

		auto xml = std::make_unique<juce::XmlElement>(KitBank::Tag);
		xml->setAttribute("version", KitBank::Version);
		xml->setAttribute("name", _found.name);

		uint32_t am = 0, lfo = 0;
		for(int t = 0; t < Tracks; ++t)
		{
			const auto prefix = juce::String::formatted("t%02d_", t + 1);
			const auto& v = kit.parameters[static_cast<size_t>(t)];
			const int id = kit.machine(static_cast<uint8_t>(t));
			// ROM-01 to -32: the Sampler on that slot of the factory bank, the same player with the same SYN1-8
			const bool rom = id >= 128 && id < 128 + 32;
			const int machine = choice(rom ? Sampler : Machine{Family::Machinedrum, static_cast<uint8_t>(id)});
			if(machine < 0)
			{
				said(trackName(t) + " plays " + mdName(id) + ", which Lockstep does not have: the Track is off");
				xml->setAttribute(prefix + "machine", 0);
				continue;
			}
			xml->setAttribute(prefix + "machine", machine);
			if(rom)
			{
				auto* sample = xml->createNewChildElement("sample");
				sample->setAttribute("track", t);
				sample->setAttribute("factory", id - 127);
			}
			for(int i = 0; i < 8; ++i)
				xml->setAttribute(prefix + "m" + juce::String(i + 1), v[static_cast<size_t>(i)]);
			const auto mix = mixOf(kit.levels[static_cast<size_t>(t)], v[Vol], v[Pan]);
			xml->setAttribute(prefix + "level", mix.level);
			xml->setAttribute(prefix + "pan", mix.pan);
			xml->setAttribute(prefix + parameters::sendSpecs[0].id, v[Del]);
			xml->setAttribute(prefix + parameters::sendSpecs[1].id, v[Rev]);

			// The family's chain, whose stages are the machine's own: EQ, filter, SRR then DIST after it
			auto chain = machinedrumChain();
			chain.band.values[0] = v[Eqf];
			chain.band.values[1] = v[Eqg];
			chain.filter.frequency = v[Fltf];
			chain.filter.width = v[Fltw];
			chain.filter.resonance = v[Fltq];
			chain.drive[0].amount = v[Srr];
			chain.drive[1].amount = v[Dist];
			const auto values = parameters::valuesOf(chain);
			for(size_t i = 0; i < values.size(); ++i)
				xml->setAttribute(prefix + parameters::chainSpecs[i].id, values[i]);

			const auto target = kit.chokes[static_cast<size_t>(t)];
			xml->setAttribute(prefix + "chokes", target < Tracks ? 1 << target : 0);
			if(kit.links[static_cast<size_t>(t)] < Tracks)
				said(trackName(t) + "'s Hit also hits " + trackName(kit.links[static_cast<size_t>(t)])
					+ ": Lockstep has no such link");
			if(v[Amd] != 0)
				am |= 1u << t;
			if(v[Lfod] != 0)
				lfo |= 1u << t;
		}
		if(am)
			said("AM on " + tracksOf(am) + " not taken: Lockstep has no AM");
		if(lfo)
			said("the LFO of " + tracksOf(lfo) + " not taken: Lockstep's LFOs are its own");

		// The Echo and the Gate Box are the machine's own, their bytes in its order
		const auto master = kit.masterEffectValues();
		xml->setAttribute(parameters::sourceSpecs[0].id, 0);
		xml->setAttribute(parameters::sourceSpecs[1].id, 0);
		for(size_t i = 0; i < parameters::echoSpecs.size(); ++i)
			xml->setAttribute(parameters::echoSpecs[i].id, master[static_cast<size_t>(MasterEffect::Echo)][i]);
		for(size_t i = 0; i < parameters::gateBoxSpecs.size(); ++i)
			xml->setAttribute(parameters::gateBoxSpecs[i].id, master[static_cast<size_t>(MasterEffect::Reverb)][i]);
		said("the master EQ and DYNAMIX not taken: Lockstep's master is its own");
		return xml;
	}
}
