#include "edDrumsProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <variant>

#include <juce_audio_formats/juce_audio_formats.h>

#include "edDrumsEditor.h"
#include "edDrumsNames.h"
#include "edDrumsRate.h"
#include "edDrumsSampleFiles.h"
#include "mdProtocol/mdmachines.h"
#include "sophieMachine.h"

namespace elektron::drums
{
	namespace
	{
		constexpr int FirstNote = 36;	// notes 36-51: Tracks 1-16
		constexpr int DefaultLevel = 100, DefaultNote = 60, Centre = 64;

		constexpr int StateVersion = 6;

		// The Machinedrum's input, control, MIDI and RAM machines are not the instrument's, and since state version 5 nor
		// are its ROM machines: the factory bank's samples play on the Sampler (ticket 27)
		bool offered(const md::machines::Machine& _m, const bool _roms)
		{
			const auto* family = md::machines::familyOf(md::MachineModel::Machinedrum, _m.id);
			return !family || (family->name != "INP" && family->name != "CTR" && family->name != "MID" && family->name != "RAM"
				&& (_roms || family->name != "ROM"));
		}

		// The lists Machine's choice was stored in: before state version 4 and Kit version 2, the Machinedrum's that
		// played; until state version 5 and Kit version 3, with ROM-01 to -32; the list now
		enum class ListOf { Before4, Before5, Now };

		// Machine's choices: Off, the Syntakt's, the Machinedrum's, the Sampler, OPEN DRUM's, then SOPHIE
		std::vector<Machine> machineList(const ListOf _of = ListOf::Now)
		{
			std::vector<Machine> list{Machine{}};
			for(int id = 0; id < SyntaktMachines; ++id)
				list.push_back({Family::Syntakt, static_cast<uint8_t>(id)});
			for(const auto& m : md::machines::machines(md::MachineModel::Machinedrum))
			{
				const Machine machine{Family::Machinedrum, static_cast<uint8_t>(m.id)};
				if(m.id < 256 && playable(machine) && (_of == ListOf::Before4 || offered(m, _of == ListOf::Before5)))
					list.push_back(machine);
			}
			// Where ROM-33 stood: ROM-33 to -48, the Sampler's entries now, were the list's last 16
			list.push_back(Sampler);
			if(_of == ListOf::Before4)
				return list;
			for(int id = 0; id < OpenDrumMachines; ++id)
				list.push_back({Family::OpenDrum, static_cast<uint8_t>(id)});
			// Last, so that no choice before it moves
			for(int id = 0; id < SophieMachines; ++id)
				list.push_back({Family::Sophie, static_cast<uint8_t>(id)});
			// The analog machines after it, appended for the same reason
			for(int id = 0; id < SyntaktAnalogMachines; ++id)
				list.push_back({Family::SyntaktAnalog, static_cast<uint8_t>(id)});
			return list;
		}

		// The machine a state of _version (a Kit's if not _state) stored as _stored: before version 3 ROM-33 to -48
		// followed the Sampler, which plays them; Off for a choice its list lacks
		Machine storedMachine(const int _stored, const int _version, const bool _state)
		{
			static const auto before4 = machineList(ListOf::Before4), before5 = machineList(ListOf::Before5),
				now = machineList(ListOf::Now);
			const auto& old = (_state ? _version >= 5 : _version >= 3) ? now
				: (_state ? _version >= 4 : _version >= 2) ? before5 : before4;
			const int sampler = static_cast<int>(before4.size()) - 1;
			const int stored = _state && _version < 3 ? std::min(_stored, sampler) : _stored;
			return stored < 0 || stored >= static_cast<int>(old.size()) ? Machine{} : old[static_cast<size_t>(stored)];
		}

		// ROM-n's slot of the factory bank, 1 to 32, for ROM-01 to -32; 0 for any other machine
		int romSlot(const Machine& _machine)
		{
			return _machine.family == Family::Machinedrum && _machine.id >= 128 && _machine.id < 128 + FactorySlots
				? _machine.id - 127 : 0;
		}

		// The choice _list gives the machine a state of _version (a Kit's if not _state) stored as _stored: a ROM machine
		// the Sampler, which plays its sample (Processor::takeRoms); a machine no longer offered Off
		int choiceOf(const std::vector<Machine>& _list, const int _stored, const int _version, const bool _state)
		{
			if(_state ? _version >= StateVersion : _version >= KitBank::Version)
				return _stored;
			const Machine stored = storedMachine(_stored, _version, _state);
			const auto found = std::find(_list.begin(), _list.end(), romSlot(stored) ? Sampler : stored);
			return found == _list.end() ? 0 : static_cast<int>(found - _list.begin());
		}

		// A Track's reference to a factory sample (Sample::factory), where m_sampleRefs keeps a Library file's
		// fingerprint: the slot it names, 0 for none
		constexpr const char* FactoryRef = "factory:";
		// A slot of the factory bank, 1 to FactorySlots; 0, no sample, for any other number
		int factorySlot(const int _slot)
		{
			return _slot >= 1 && _slot <= FactorySlots ? _slot : 0;
		}
		int factorySlotOf(const juce::String& _ref)
		{
			return _ref.startsWith(FactoryRef) ? factorySlot(_ref.substring(8).getIntValue()) : 0;
		}
		juce::String factoryRef(const int _slot)
		{
			return FactoryRef + juce::String(_slot);
		}

		// Ticket 27's house name (edDrumsNames.h); a machine without one, by its id alone, no code of an old machine
		juce::String machineName(const Machine& _machine)
		{
			if(const char* name = names::machine(_machine))
				return name;
			return "MACHINE " + juce::String(_machine.id);
		}

		// Tracks 1-8 start on the Syntakt's machines, which need nothing, Track 9 on TRX-CL for SP TWINSHOT, which
		// needs its samples, the others on the Machinedrum's TRX, which play without an image what they can
		int defaultMachine(const std::vector<Machine>& _list, const int _track)
		{
			const Machine machine = _track == SyntaktTwinshot ? Machine{Family::Machinedrum, 26}
				: _track < SyntaktMachines ? Machine{Family::Syntakt, static_cast<uint8_t>(_track)}
				: Machine{Family::Machinedrum, static_cast<uint8_t>(16 + _track - SyntaktMachines)};
			return static_cast<int>(std::find(_list.begin(), _list.end(), machine) - _list.begin());
		}

		void setQuietly(juce::RangedAudioParameter& _parameter, const float _value)
		{
			static_cast<juce::AudioProcessorParameter&>(_parameter).setValue(_parameter.convertTo0to1(_value));
		}

		// What tells two samples apart, for the audio thread to give the engine a Kit's only where it changes one: 0 for
		// none (FNV-1a of the name, the rate and the values)
		uint64_t sampleHash(const Sample* _sample)
		{
			if(!_sample)
				return 0;
			uint64_t h = 14695981039346656037ull;
			const auto add = [&h](const void* _data, const size_t _size)
			{
				for(size_t i = 0; i < _size; ++i)
					h = (h ^ static_cast<const uint8_t*>(_data)[i]) * 1099511628211ull;
			};
			add(&_sample->factory, sizeof(_sample->factory));	// read where the bank holds it, not from its slot
			add(_sample->name.data(), _sample->name.size());
			add(&_sample->rate, sizeof(_sample->rate));
			add(_sample->pcm.data(), _sample->pcm.size() * sizeof(int16_t));
			return h | 1;
		}

		// ---- the state's patterns: only what differs from a new Pattern's ----

		bool isDefault(const Step& _s)
		{
			const Step d;
			return _s.trig == d.trig && _s.accent == d.accent && _s.slide == d.slide && _s.velocity == d.velocity
				&& _s.note == d.note && _s.gate == d.gate && _s.timing == d.timing && _s.ratchets == d.ratchets && _s.ratchetRate == d.ratchetRate
				&& _s.ratchetVelocity == d.ratchetVelocity && _s.condition.kind == d.condition.kind
				&& _s.condition.a == d.condition.a && _s.condition.b == d.condition.b && _s.lockCount == 0;
		}

		// A float as text that reads back the same float
		juce::String exact(const float _v)
		{
			char text[32];
			std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(_v));
			return text;
		}

		// The pattern as an element, nullptr for one as new
		std::unique_ptr<juce::XmlElement> patternXml(const Pattern& _p, const int _index)
		{
			const Pattern blank;
			auto xml = std::make_unique<juce::XmlElement>("pattern");
			bool used = _p.masterLength != blank.masterLength || _p.swing != blank.swing
				|| _p.trackLength != blank.trackLength || _p.trackScale != blank.trackScale || _p.kit != blank.kit;
			juce::StringArray lengths, scales;
			for(int t = 0; t < SeqTracks; ++t)
			{
				lengths.add(juce::String(_p.trackLength[static_cast<size_t>(t)]));
				scales.add(juce::String(static_cast<int>(_p.trackScale[static_cast<size_t>(t)])));
				for(int i = 0; i < SeqSteps; ++i)
				{
					const Step& s = _p.step(t, i);
					if(isDefault(s))
						continue;
					used = true;
					auto* step = xml->createNewChildElement("step");
					step->setAttribute("track", t);
					step->setAttribute("index", i);
					step->setAttribute("trig", s.trig ? 1 : 0);
					step->setAttribute("accent", s.accent ? 1 : 0);
					step->setAttribute("slide", s.slide ? 1 : 0);
					step->setAttribute("velocity", s.velocity);
					step->setAttribute("note", s.note);
					step->setAttribute("gate", s.gate);
					step->setAttribute("timing", s.timing);
					step->setAttribute("ratchets", s.ratchets);
					step->setAttribute("ratchetRate", s.ratchetRate);
					step->setAttribute("ratchetVelocity", s.ratchetVelocity);
					step->setAttribute("condition", static_cast<int>(s.condition.kind));
					step->setAttribute("conditionA", s.condition.a);
					step->setAttribute("conditionB", s.condition.b);
					// The Locks as "parameter:value" pairs
					juce::StringArray locks;
					const Lock* l = _p.locks(t, i);
					for(int k = 0; k < s.lockCount; ++k)
						locks.add(juce::String(l[k].parameter) + ":" + exact(l[k].value));
					if(!locks.isEmpty())
						step->setAttribute("locks", locks.joinIntoString(" "));
				}
			}
			if(!used)
				return nullptr;
			xml->setAttribute("index", _index);
			xml->setAttribute("length", _p.masterLength);
			xml->setAttribute("swing", _p.swing);
			xml->setAttribute("kit", _p.kit);
			xml->setAttribute("trackLengths", lengths.joinIntoString(" "));
			xml->setAttribute("trackScales", scales.joinIntoString(" "));
			return xml;
		}

		void readPattern(Pattern& _p, const juce::XmlElement& _xml)
		{
			_p.masterLength = std::clamp(_xml.getIntAttribute("length", 16), 1, SeqMaxLength);
			_p.swing = std::clamp(_xml.getIntAttribute("swing", 50), 50, 80);
			_p.kit = KitBank::valid(_xml.getIntAttribute("kit", -1)) ? _xml.getIntAttribute("kit", -1) : -1;
			const auto lengths = juce::StringArray::fromTokens(_xml.getStringAttribute("trackLengths"), false);
			const auto scales = juce::StringArray::fromTokens(_xml.getStringAttribute("trackScales"), false);
			for(int t = 0; t < SeqTracks && t < lengths.size(); ++t)
				_p.trackLength[static_cast<size_t>(t)] = std::clamp(lengths[t].getIntValue(), 1, SeqSteps);
			for(int t = 0; t < SeqTracks && t < scales.size(); ++t)
				_p.trackScale[static_cast<size_t>(t)] = static_cast<StepScale>(std::clamp(scales[t].getIntValue(), 0,
					static_cast<int>(StepScale::Count) - 1));
			for(const auto* step : _xml.getChildWithTagNameIterator("step"))
			{
				const int t = step->getIntAttribute("track", -1), i = step->getIntAttribute("index", -1);
				if(t < 0 || t >= SeqTracks || i < 0 || i >= SeqSteps)
					continue;
				Step s;
				s.trig = step->getIntAttribute("trig") != 0;
				s.accent = step->getIntAttribute("accent") != 0;
				s.slide = step->getIntAttribute("slide") != 0;
				s.velocity = static_cast<uint8_t>(std::clamp(step->getIntAttribute("velocity", 100), 1, 127));
				// A step of a state before notes and gates: the Track's note, no gate
				s.note = static_cast<uint8_t>(std::clamp(step->getIntAttribute("note", NoNote), 0, static_cast<int>(NoNote)));
				s.gate = static_cast<uint16_t>(std::clamp(step->getIntAttribute("gate", 0), 0, 0xffff));
				s.timing = static_cast<int8_t>(std::clamp(step->getIntAttribute("timing"), -(TimingUnits - 1), TimingUnits - 1));
				s.ratchets = static_cast<uint8_t>(std::clamp(step->getIntAttribute("ratchets", 1), 1, 8));
				s.ratchetRate = static_cast<uint8_t>(std::clamp(step->getIntAttribute("ratchetRate"), 0, 255));
				s.ratchetVelocity = static_cast<int8_t>(std::clamp(step->getIntAttribute("ratchetVelocity"), -127, 127));
				s.condition.kind = static_cast<ConditionKind>(std::clamp(step->getIntAttribute("condition"), 0,
					static_cast<int>(ConditionKind::NotFirst)));
				s.condition.a = static_cast<uint8_t>(std::clamp(step->getIntAttribute("conditionA"), 0, 255));
				s.condition.b = static_cast<uint8_t>(std::clamp(step->getIntAttribute("conditionB"), 0, 255));
				_p.step(t, i) = s;
				for(const auto& pair : juce::StringArray::fromTokens(step->getStringAttribute("locks"), false))
				{
					const int colon = pair.indexOfChar(':');
					if(colon <= 0)
						continue;
					const int parameter = pair.substring(0, colon).getIntValue();
					if(parameter >= 0 && parameter <= 0xffff)
						_p.setLock(t, i, static_cast<uint16_t>(parameter),
							std::strtof(pair.substring(colon + 1).toRawUTF8(), nullptr));
				}
			}
		}

		// What the sequencer keeps besides its patterns: mutes and solos a bit per Track, fill, the seed, the chain as
		// "pattern:repeats:fill" triples and whether it plays
		std::unique_ptr<juce::XmlElement> sequencerXml(const Sequencer& _s, const bool _chainOn)
		{
			auto xml = std::make_unique<juce::XmlElement>("sequencer");
			int mutes = 0, solos = 0;
			for(int t = 0; t < SeqTracks; ++t)
			{
				mutes |= _s.mute(t) ? 1 << t : 0;
				solos |= _s.solo(t) ? 1 << t : 0;
			}
			xml->setAttribute("mutes", mutes);
			xml->setAttribute("solos", solos);
			xml->setAttribute("fill", _s.fill() ? 1 : 0);
			xml->setAttribute("seed", juce::String(_s.seed()));
			juce::StringArray chain;
			for(int i = 0; i < _s.chainLength(); ++i)
			{
				const auto& e = _s.chainEntry(i);
				chain.add(juce::String(e.pattern) + ":" + juce::String(e.repeats) + ":" + (e.fill ? "1" : "0"));
			}
			xml->setAttribute("chain", chain.joinIntoString(" "));
			xml->setAttribute("chainOn", _chainOn ? 1 : 0);
			return xml;
		}

		// A state without the element: no mute, no solo, fill off, seed 1, no chain. Returns whether the chain played
		bool readSequencer(Sequencer& _s, const juce::XmlElement* _xml)
		{
			const int mutes = _xml ? _xml->getIntAttribute("mutes") : 0, solos = _xml ? _xml->getIntAttribute("solos") : 0;
			for(int t = 0; t < SeqTracks; ++t)
			{
				_s.setMute(t, (mutes >> t) & 1);
				_s.setSolo(t, (solos >> t) & 1);
			}
			_s.setFill(_xml && _xml->getIntAttribute("fill") != 0);
			_s.setSeed(_xml ? static_cast<uint32_t>(_xml->getStringAttribute("seed", "1").getLargeIntValue()) : 1);
			std::vector<ChainEntry> chain;
			if(_xml)
				for(const auto& triple : juce::StringArray::fromTokens(_xml->getStringAttribute("chain"), false))
				{
					const auto parts = juce::StringArray::fromTokens(triple, ":", "");
					if(parts.size() != 3)
						continue;
					chain.push_back({static_cast<uint8_t>(std::clamp(parts[0].getIntValue(), 0, SeqPatterns - 1)),
						static_cast<uint8_t>(std::clamp(parts[1].getIntValue(), 1, 64)), parts[2].getIntValue() != 0});
				}
			_s.setChain(chain);
			return _xml && _xml->getIntAttribute("chainOn") != 0 && !chain.empty();
		}

		juce::AudioParameterInt* intParameter(const juce::String& _id, const juce::String& _name, const int _max,
			const int _default, const bool _automatable = true,
			const juce::AudioParameterIntAttributes& _attributes = juce::AudioParameterIntAttributes())
		{
			return new juce::AudioParameterInt({_id, 1}, _name, 0, _max, _default, _attributes.withAutomatable(_automatable));
		}

		// The value's text in its unit, parameters::valueText's, for the parameter _prefix + _id, the others it depends on
		// read from the same Track's or the shared ones
		juce::AudioParameterIntAttributes unitsOf(const Processor* _processor, const juce::String& _prefix,
			const std::string& _id, const int _min, const int _max)
		{
			const parameters::Other other = [_processor, _prefix](const char* _other)
			{
				// Not a parameter: the rate the Track's chain runs at, which the DAC drive's clock starts from
				if(std::strcmp(_other, "chain_rate") == 0)
					return _prefix.isEmpty() ? 0 : juce::roundToInt(_processor->chainRate(_prefix.substring(1, 3).getIntValue() - 1));
				const auto* p = _processor->parameter(_prefix + _other);
				return p ? juce::roundToInt(p->convertFrom0to1(p->getValue())) : 0;
			};
			return juce::AudioParameterIntAttributes()
				.withStringFromValueFunction([other, _id](const int _value, int)
				{
					return juce::String(parameters::valueText(_id, _value, other));
				})
				.withValueFromStringFunction([other, _id, _min, _max](const juce::String& _text)
				{
					return parameters::valueOf(_id, _text.toStdString(), _min, _max, other);
				});
		}
	}

	juce::AudioProcessor::BusesProperties Processor::buses()
	{
		auto b = BusesProperties().withOutput("Main", juce::AudioChannelSet::stereo(), true);
		for(int o = 1; o < Outputs; ++o)
			b = b.withOutput(juce::String::formatted("Out %02d", o), juce::AudioChannelSet::stereo(), false);
		return b;
	}

	Processor::Processor() : AudioProcessor(buses()), m_machines(machineList())
	{
		// The host makes the instrument on its message thread: the old data folder's files come over before anything
		// reads the new one
		migrateData(legacyDataFolder(), dataFolder());
		// Nothing leaves the constructor: the words the Locker keeps (ticket 20), else an image found, else the
		// sampled words are the house's, or silence
		try
		{
			ImageWords words;
			LockerImport::takeOldWords(dataFolder());
			if(LockerImport::readWords(LockerImport::wordsFile(dataFolder()), words))
			{
				m_library = std::make_unique<Library>(words);
				m_libraryFromWords = true;
			}
		}
		catch(const std::exception&)
		{
		}
		try
		{
			if(!m_library)
			{
				const auto image = Library::findFlashImage(dataFolder().getChildFile("roms").getFullPathName().toStdString());
				m_library = image.empty() ? std::make_unique<Library>() : std::make_unique<Library>(image);
			}
		}
		catch(const std::exception&)
		{
			m_library = std::make_unique<Library>();
		}
		// The Locker's FACTORY: the factory bank's slots that hold a sample, by what their directory entries say
		for(int slot = 1; slot <= FactorySlots && !m_library->directory.empty(); ++slot)
		{
			const auto* entry = m_library->directory.data() + 4 * static_cast<size_t>(slot - 1);
			if((entry[1] & 0xffffff) == 0)
				continue;
			SampleLibrary::Entry e;
			e.name = "FACTORY " + juce::String(slot).paddedLeft('0', 2);
			e.folder = SampleLibrary::FactoryFolder;
			e.length = static_cast<int64_t>(entry[1] & 0xffffff);
			e.rate = std::clamp(static_cast<double>(entry[3] & 0xffffff) * 44100.0 / Sample::Unity, Sample::MinRate,
				Sample::MaxRate);
			e.channels = 1;
			e.fingerprint = factoryRef(slot);
			m_factorySamples.push_back(e);
		}

		juce::StringArray names;
		for(const auto& m : m_machines)
			names.add(machineName(m));
		for(int t = 0; t < Tracks; ++t)
		{
			const auto id = juce::String::formatted("t%02d_", t + 1);
			const auto name = juce::String::formatted("T%02d ", t + 1);
			auto& p = m_tracks[static_cast<size_t>(t)];
			const int machine = defaultMachine(m_machines, t);
			p.machine = new juce::AudioParameterChoice({id + "machine", 1}, name + "Machine", names, machine,
				juce::AudioParameterChoiceAttributes().withAutomatable(false));
			add(p.machine, Section::Machine, t);
			m_machineSeen[static_cast<size_t>(t)] = machine;
			const auto machineDefaults = allDefaults(m_machines[static_cast<size_t>(machine)]);
			for(int i = 0; i < 8; ++i)
			{
				p.values[static_cast<size_t>(i)] = intParameter(id + "m" + juce::String(i + 1),
					name + "Machine " + juce::String(i + 1), 127, machineDefaults[static_cast<size_t>(i)]);
				add(p.values[static_cast<size_t>(i)], Section::Machine, t);
			}
			add(p.note = intParameter(id + "note", name + "Note", 127, DefaultNote), Section::Machine, t);
			add(p.level = intParameter(id + "level", name + "Level", 127, DefaultLevel, true,
				unitsOf(this, id, "level", 0, 127)), Section::Amp, t);
			add(p.pan = intParameter(id + "pan", name + "Pan", 127, Centre, true, unitsOf(this, id, "pan", 0, 127)),
				Section::Amp, t);
			add(p.output = intParameter(id + "output", name + "Out", Outputs - 1, 0), Section::Sends, t);
			add(p.chokes = intParameter(id + "chokes", name + "Chokes", 0xffff, 0, false), Section::Machine, t);
		}
		// The host's tempo where its playhead gives one, else this
		m_tempo = new juce::AudioParameterFloat({"tempo", 1}, "Tempo", juce::NormalisableRange<float>(30.0f, 300.0f),
			120.0f);
		add(m_tempo, Section::Global);

		// Ticket 18's, appended. A Track's chain starts as its default machine's family brings it, as the engine does
		using namespace parameters;
		for(int t = 0; t < Tracks; ++t)
		{
			const auto id = juce::String::formatted("t%02d_", t + 1);
			const auto name = juce::String::formatted("T%02d ", t + 1);
			auto& p = m_tracks[static_cast<size_t>(t)];
			const auto& machine = m_machines[static_cast<size_t>(defaultMachine(m_machines, t))];
			m_chainFamily[static_cast<size_t>(t)] = machine.family;
			const auto chain = valuesOf(familyChain(machine));
			for(size_t i = 0; i < ChainValues; ++i)
			{
				const auto section = i < BandKind ? Section::Drive : i < FilterKind ? Section::Band
					: i < AmpOn ? Section::Filter : Section::Amp;
				const auto& spec = chainSpecs[i];
				watch(p.chain[i], spec, id + spec.id, name + spec.name, chain[i], section, t);
			}
			const auto lfo = valuesOf(LfoSettings{});
			for(size_t l = 0; l < 2; ++l)
			{
				const auto lfoId = id + "lfo" + juce::String(l + 1) + "_";
				const auto lfoName = name + "LFO" + juce::String(l + 1) + " ";
				const auto section = l == 0 ? Section::Lfo1 : Section::Lfo2;
				for(size_t i = 0; i < LfoValues; ++i)
				{
					if(i != LfoHz)
					{
						watch(p.lfo[l][i], lfoSpecs[i], lfoId + lfoSpecs[i].id, lfoName + lfoSpecs[i].name,
							juce::roundToInt(lfo[i]), section, t);
						continue;
					}
					// HZ, an LFO free of the tempo: the Syntakt's go from 1/256 Hz to 504 Hz
					juce::NormalisableRange<float> range(0.01f, 500.0f);
					range.setSkewForCentre(2.0f);
					p.lfo[l][i].parameter = add(new juce::AudioParameterFloat({lfoId + lfoSpecs[i].id, 1},
						lfoName + lfoSpecs[i].name, range, static_cast<float>(lfo[i])), section, t);
					m_watched.push_back(&p.lfo[l][i]);
				}
			}
			for(size_t s = 0; s < SendCount; ++s)
				watch(p.sends[s], sendSpecs[s], id + sendSpecs[s].id, name + sendSpecs[s].name, 0, Section::Sends, t);
			watch(p.accent, accentSpec, id + accentSpec.id, name + accentSpec.name, 0, Section::Amp, t);
		}
		const auto shared = [this](Watched& _watched, const Spec& _spec, const int _default, const Section _section)
		{
			watch(_watched, _spec, _spec.id, _spec.name, _default, _section);
		};
		for(size_t i = 0; i < m_sources.size(); ++i)
			shared(m_sources[i], sourceSpecs[i], static_cast<int>(SendSource::Machinedrum), Section::Effects);
		for(size_t i = 0; i < SendCount; ++i)
			shared(m_sendOutputs[i], sendOutputSpecs[i], 0, Section::Effects);
		for(size_t i = 0; i < m_echo.size(); ++i)
			shared(m_echo[i], echoSpecs[i], echoDefaults[i], Section::Effects);
		for(size_t i = 0; i < m_gateBox.size(); ++i)
			shared(m_gateBox[i], gateBoxSpecs[i], gateBoxDefaults[i], Section::Effects);
		for(size_t i = 0; i < m_syntaktDelay.size(); ++i)
			shared(m_syntaktDelay[i], syntaktDelaySpecs[i], syntaktDelayDefaults[i], Section::Effects);
		for(size_t i = 0; i < m_syntaktReverb.size(); ++i)
			shared(m_syntaktReverb[i], syntaktReverbSpecs[i], syntaktReverbDefaults[i], Section::Effects);
		shared(m_syntaktDelaySend, syntaktDelaySendSpec, 0, Section::Effects);
		{
			// A delay time of 1 / Hz in place of TIME's, 0 TIME's (Sends::setDelayFree)
			juce::NormalisableRange<float> range(0.0f, 1378.125f);
			range.setSkewForCentre(4.0f);
			m_delayFree.parameter = add(new juce::AudioParameterFloat({"delay_free", 1}, "Delay Free Rate", range, 0.0f),
				Section::Effects);
			m_watched.push_back(&m_delayFree);
		}
		for(size_t i = 0; i < m_chorus.size(); ++i)
			shared(m_chorus[i], chorusSpecs[i], chorusDefaults[i], Section::Effects);
		for(size_t i = 0; i < m_phaser.size(); ++i)
			shared(m_phaser[i], phaserSpecs[i], phaserDefaults[i], Section::Effects);
		shared(m_masterOn, masterOnSpec, 0, Section::Master);
		for(size_t i = 0; i < m_master.size(); ++i)
			shared(m_master[i], masterSpecs[i], masterDefaults[i], Section::Master);
		shared(m_oversampling, oversamplingSpec, Engine::DefaultOversampling / 2, Section::Global);

		// Ticket 17's: the pattern the sequencer plays, from the end of the one playing
		m_pattern = add(new juce::AudioParameterInt({"pattern", 1}, "Pattern", 1, SeqPatterns, 1), Section::Global);

		// Ticket 26's: Machine 9-24, for the machines that use more than 8
		for(int t = 0; t < Tracks; ++t)
		{
			const auto id = juce::String::formatted("t%02d_", t + 1);
			const auto name = juce::String::formatted("T%02d ", t + 1);
			auto& p = m_tracks[static_cast<size_t>(t)];
			const auto machineDefaults = allDefaults(m_machines[static_cast<size_t>(defaultMachine(m_machines, t))]);
			for(int i = 8; i < MachineParameters; ++i)
			{
				// On the Sampler, its grain's values in their units
				const auto text = juce::AudioParameterIntAttributes().withStringFromValueFunction([this, t, i](const int _v, int)
				{
					const auto* machine = m_tracks[static_cast<size_t>(t)].machine;
					const bool sampler = machine && m_machines[static_cast<size_t>(machine->getIndex())] == Sampler;
					return sampler ? juce::String(parameters::samplerText(i, _v)) : juce::String(_v);
				});
				p.values[static_cast<size_t>(i)] = intParameter(id + "m" + juce::String(i + 1),
					name + "Machine " + juce::String(i + 1), 127, machineDefaults[static_cast<size_t>(i)], true, text);
				add(p.values[static_cast<size_t>(i)], Section::Machine, t);
			}
		}

		// Ticket 14's: the effects' tempo sync and the DP/4's S&H
		shared(m_delaySync, delaySyncSpec, 1, Section::Effects);
		shared(m_delayNote, delayNoteSpec, DefaultDelayNote, Section::Effects);
		for(size_t i = 0; i < m_lfoSync.size(); ++i)
		{
			shared(m_lfoSync[i], lfoSyncSpecs[i], 0, Section::Effects);
			shared(m_lfoNote[i], lfoNoteSpecs[i], DefaultLfoNote, Section::Effects);
		}
		shared(m_dp4SampleHold, dp4SampleHoldSpec, 0, Section::Effects);

		m_edited = std::make_unique<Sequencer>();
		for(auto& s : m_sequencers)
			s = std::make_unique<Sequencer>();
		for(auto& step : m_playingStep)
			step = -1;
		for(auto& step : m_playingRowStep)
			step = -1;
		buildLockTargets();
		// LIVE hears the gestures on the parameters
		m_gestures.assign(static_cast<size_t>(getParameters().size()), 0);
		m_liveLocks.reserve(LiveNotes);
		addListener(&m_liveListener);

		// A Kit holds every parameter but the Global ones
		const auto& global = ids(Section::Global);
		for(auto* parameter : getParameters())
		{
			const auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter);
			m_kitParameter.push_back(p && !global.contains(p->getParameterID()) ? 1 : 0);
		}
		m_kits = std::make_unique<KitBank>(dataFolder().getChildFile("Kits"));
		m_trackSounds = std::make_unique<TrackSounds>(dataFolder().getChildFile("TrackSounds"));
		m_sampleLibrary = std::make_unique<SampleLibrary>(dataFolder());
		m_sampleLibrary->adoptImported();
		m_import = std::make_unique<LockerImport>(dataFolder());
		startTimerHz(20);
	}

	Processor::~Processor()
	{
		stopTimer();
		removeListener(&m_liveListener);
	}

	template<typename P> P* Processor::add(P* _parameter, const Section _section, const int _track)
	{
		addParameter(_parameter);
		const auto& id = _parameter->getParameterID();
		jassert(m_byId.count(id) == 0);
		m_byId[id] = _parameter;
		if(_track < 0)
		{
			m_sharedSections[static_cast<size_t>(_section) - TrackSections].add(id);
			return _parameter;
		}
		m_trackSections[static_cast<size_t>(_track)][static_cast<size_t>(_section)].add(id);
		m_trackIds[static_cast<size_t>(_track)].add(id);
		return _parameter;
	}

	void Processor::watch(Watched& _watched, const parameters::Spec& _spec, const juce::String& _id,
		const juce::String& _name, const int _default, const Section _section, const int _track)
	{
		juce::RangedAudioParameter* p;
		if(_spec.toggle)
			p = new juce::AudioParameterBool({_id, 1}, _name, _default != 0);
		else if(_spec.choices)
			p = new juce::AudioParameterChoice({_id, 1}, _name, juce::StringArray::fromTokens(_spec.choices, "|", ""),
				_default);
		else
		{
			// A Track's ID after its "tNN_"
			const auto prefix = _track >= 0 ? _id.substring(0, 4) : juce::String();
			p = new juce::AudioParameterInt({_id, 1}, _name, _spec.min, _spec.max, _default,
				unitsOf(this, prefix, _id.substring(prefix.length()).toStdString(), _spec.min, _spec.max));
		}
		_watched.parameter = add(p, _section, _track);
		m_watched.push_back(&_watched);
	}

	juce::RangedAudioParameter* Processor::parameter(const juce::String& _id) const
	{
		const auto found = m_byId.find(_id);
		return found == m_byId.end() ? nullptr : found->second;
	}

	const juce::StringArray& Processor::ids(const Section _section, const int _track) const
	{
		const auto s = static_cast<size_t>(_section);
		if(s < TrackSections)
			return m_trackSections[static_cast<size_t>(_track)][s];
		return m_sharedSections[s - TrackSections];
	}

	void Processor::buildLockTargets()
	{
		m_lockTargets.assign(static_cast<size_t>(getParameters().size()), {});
		m_locked.assign(m_lockTargets.size(), 0);
		m_lockValue.assign(m_lockTargets.size(), 0.0f);
		const auto target = [this](const juce::AudioProcessorParameter* _p, const int _track, const LockKind _kind,
			const int _a = 0, const int _b = 0)
		{
			m_lockTargets[static_cast<size_t>(_p->getParameterIndex())] = {_track, _kind, _a, _b};
		};
		for(int t = 0; t < Tracks; ++t)
		{
			const auto& p = m_tracks[static_cast<size_t>(t)];
			for(int i = 0; i < MachineParameters; ++i)
				target(p.values[static_cast<size_t>(i)], t, LockKind::Machine, i);
			target(p.note, t, LockKind::Note);
			target(p.level, t, LockKind::Level);
			target(p.pan, t, LockKind::Pan);
			for(size_t i = 0; i < p.chain.size(); ++i)
				target(p.chain[i].parameter, t, LockKind::Chain, static_cast<int>(i));
			for(size_t l = 0; l < p.lfo.size(); ++l)
				for(size_t i = 0; i < p.lfo[l].size(); ++i)
					target(p.lfo[l][i].parameter, t, LockKind::Lfo, static_cast<int>(l), static_cast<int>(i));
			for(size_t i = 0; i < p.sends.size(); ++i)
				target(p.sends[i].parameter, t, LockKind::Send, static_cast<int>(i));
			target(p.accent.parameter, t, LockKind::Accent);
			// Each of a Track's parameters once at most: the list never grows past this on the audio thread
			m_lockedBy[static_cast<size_t>(t)].reserve(static_cast<size_t>(trackIds(t).size()));
			m_slides[static_cast<size_t>(t)].glides.reserve(static_cast<size_t>(trackIds(t).size()));
		}
	}

	float Processor::lockOr(const Watched& _watched) const
	{
		return held(_watched.parameter)
			? m_lockValue[static_cast<size_t>(_watched.parameter->getParameterIndex())] : _watched.value();
	}

	parameters::ChainArray Processor::chainValues(const int _track) const
	{
		parameters::ChainArray values{};
		const auto& p = m_tracks[static_cast<size_t>(_track)];
		for(size_t i = 0; i < values.size(); ++i)
			values[i] = juce::roundToInt(lockOr(p.chain[i]));
		return values;
	}

	parameters::LfoArray Processor::lfoValues(const int _track, const int _lfo) const
	{
		parameters::LfoArray values{};
		const auto& lfo = m_tracks[static_cast<size_t>(_track)].lfo[static_cast<size_t>(_lfo)];
		for(size_t i = 0; i < values.size(); ++i)
			values[i] = lockOr(lfo[i]);
		return values;
	}

	// The chain whole, when it is not the one the engine holds: given again, the same chain is not quite a no-op (ulps
	// of the plug-in's build against the engine's)
	void Processor::setChainHeld(const int _track)
	{
		const auto values = chainValues(_track);
		if(auto& chain = m_chainHeld[static_cast<size_t>(_track)]; values != chain)
		{
			m_engine->setChain(_track, parameters::chainOf(values));
			chain = values;
		}
	}

	// One parameter of a Track to the engine: the Lock's value while it holds one, else the host's
	void Processor::applyTarget(const int _parameter)
	{
		const auto i = static_cast<size_t>(_parameter);
		const auto& target = m_lockTargets[i];
		const int t = target.track;
		auto& e = *m_engine;
		const auto* parameter = static_cast<const juce::RangedAudioParameter*>(getParameters()[_parameter]);
		const int v = juce::roundToInt(m_locked[i] ? m_lockValue[i] : parameter->convertFrom0to1(parameter->getValue()));
		switch(target.kind)
		{
		case LockKind::None:	break;
		case LockKind::Machine:
			if(v != e.parameter(t, target.a))
				e.setParameter(t, target.a, static_cast<uint8_t>(v));
			break;
		case LockKind::Note:	e.setNote(t, v); break;
		case LockKind::Level:	e.setLevel(t, v); break;
		case LockKind::Pan:		e.setPan(t, v); break;
		case LockKind::Chain:	setChainHeld(t); break;
		case LockKind::Lfo:		e.setLfo(t, target.a, parameters::lfoOf(lfoValues(t, target.a))); break;
		case LockKind::Send:	e.setSend(t, static_cast<Send>(target.a), v); break;
		case LockKind::Accent:	e.setAccent(t, v); break;
		}
	}

	// The Track's next Hit or Lock: what the last step locked takes the host's values again
	void Processor::releaseLocks(const int _track)
	{
		endSlide(_track);
		auto& list = m_lockedBy[static_cast<size_t>(_track)];
		for(const int i : list)
			m_locked[static_cast<size_t>(i)] = 0;
		for(const int i : list)
			applyTarget(i);
		list.clear();
	}

	void Processor::endSlide(const int _track)
	{
		auto& slide = m_slides[static_cast<size_t>(_track)];
		slide.next = NoSlide;
		slide.glides.clear();
	}

	// A slide's step: each value where the line stands at this sample, the last on the next trig's value
	void Processor::stepSlide(const int _track)
	{
		auto& slide = m_slides[static_cast<size_t>(_track)];
		const int64_t elapsed = std::min(slide.next - slide.start, slide.length);
		const float f = static_cast<float>(elapsed) / static_cast<float>(slide.length);
		for(const auto& g : slide.glides)
		{
			m_lockValue[static_cast<size_t>(g.parameter)] = g.from + (g.to - g.from) * f;
			applyTarget(g.parameter);
		}
		slide.next = elapsed >= slide.length ? NoSlide : slide.next + SlideStep;
	}

	// _at: the event's sample since prepareToPlay
	void Processor::applyEvent(const Event& _event, const int64_t _at)
	{
		const int t = _event.track;
		// The end of a Hit's gate: what it locked holds until the Track's next Hit or Lock
		if(_event.off)
		{
			m_engine->noteOff(t);
			return;
		}
		// A ratchet's later Hit keeps what its step locked, and locks Machine 1-8 again, which Engine::lock holds a Hit; it
		// leaves its step's slide going
		if(!_event.retrig)
			releaseLocks(t);
		const bool slides = _event.slide && !_event.retrig && _event.slideSamples > 0;
		auto& slide = m_slides[static_cast<size_t>(t)];
		for(int k = 0; k < _event.lockCount; ++k)
		{
			const auto& lock = _event.locks[k];
			if(lock.parameter >= m_lockTargets.size())
				continue;
			const auto& target = m_lockTargets[lock.parameter];
			if(target.track != t || target.kind == LockKind::None)
				continue;
			if(_event.retrig && target.kind != LockKind::Machine)
				continue;
			// Within the parameter's range, on its steps
			const auto* parameter = static_cast<const juce::RangedAudioParameter*>(getParameters()[lock.parameter]);
			const float value = parameter->convertFrom0to1(parameter->convertTo0to1(lock.value));
			const bool hitLock = _event.trig && target.kind == LockKind::Machine;
			// A ratchet's later Hit locks where its step's slide stands, not back at the slide's start
			const float held = _event.retrig && m_locked[lock.parameter] ? m_lockValue[lock.parameter] : value;
			if(hitLock)
				m_engine->lock(t, target.a, static_cast<uint8_t>(juce::roundToInt(held)));
			if(hitLock && !slides)
				continue;
			// A Machine 1-8 the Hit locks is held while it slides, and takes the host's value again where the slide ends
			m_lockValue[lock.parameter] = value;
			if(!m_locked[lock.parameter])
			{
				m_locked[lock.parameter] = 1;
				m_lockedBy[static_cast<size_t>(t)].push_back(lock.parameter);
			}
			if(!hitLock)
				applyTarget(lock.parameter);
			if(!slides)
				continue;
			float to = parameter->convertFrom0to1(parameter->getValue());
			for(int n = 0; n < _event.nextLockCount; ++n)
				if(_event.nextLocks[n].parameter == lock.parameter)
					to = parameter->convertFrom0to1(parameter->convertTo0to1(_event.nextLocks[n].value));
			// A step may lock a parameter twice: one glide each, so that the list stays within what it reserved
			const auto same = std::find_if(slide.glides.begin(), slide.glides.end(),
				[&](const Glide& _g) { return _g.parameter == lock.parameter; });
			if(same != slide.glides.end())
				*same = {lock.parameter, value, to};
			else
				slide.glides.push_back({lock.parameter, value, to});
		}
		if(slides && !slide.glides.empty())
		{
			slide.start = _at;
			slide.length = _event.slideSamples;
			slide.next = _at + SlideStep;
		}
		// The step's note is a Lock of the Track's Note, over a Lock of Note the step may hold
		if(_event.trig && _event.note != NoNote)
		{
			const auto i = static_cast<size_t>(m_tracks[static_cast<size_t>(t)].note->getParameterIndex());
			m_lockValue[i] = _event.note;
			if(!m_locked[i])
			{
				m_locked[i] = 1;
				m_lockedBy[static_cast<size_t>(t)].push_back(static_cast<int>(i));
			}
			applyTarget(static_cast<int>(i));
		}
		if(_event.trig)
			m_engine->noteOn(t, _event.velocity, _event.accent);
	}

	void Processor::editSequencer(const std::function<void(Sequencer&)>& _edit)
	{
		jassert(juce::MessageManager::existsAndIsCurrentThread());
		_edit(*m_edited);
		// A pattern pasted or edited may name a Kit no pattern named: read it ahead, else it would not load
		prepareKits(0);
		publishSequencer();
	}

	// The triple buffer's writer: the spare takes the edits, then changes places with the middle, marked Fresh
	void Processor::publishSequencer()
	{
		m_sequencers[static_cast<size_t>(m_spare)]->copyEdits(*m_edited);
		m_spare = m_middle.exchange(m_spare | Fresh, std::memory_order_acq_rel) & 3;
	}

	bool Processor::exportPatternMidi(const int _pattern, const juce::File& _file) const
	{
		const auto bytes = Sequencer::patternMidi(m_edited->pattern(_pattern), m_edited->seed());
		return _file.replaceWithData(bytes.data(), bytes.size());
	}

	// LIVE's Locks: the gestures counted on the message thread, the host's own automation having none
	void Processor::liveGesture(const int _index, const bool _begin)
	{
		if(!juce::MessageManager::existsAndIsCurrentThread() || _index < 0 || _index >= static_cast<int>(m_gestures.size()))
			return;
		auto& n = m_gestures[static_cast<size_t>(_index)];
		n = _begin ? n + 1 : std::max(0, n - 1);
	}

	void Processor::liveParameter(const int _index, const float _value)
	{
		if(!m_live.load() || !juce::MessageManager::existsAndIsCurrentThread() || _index < 0
			|| _index >= static_cast<int>(m_gestures.size()) || !m_gestures[static_cast<size_t>(_index)])
			return;
		const auto& target = m_lockTargets[static_cast<size_t>(_index)];
		const int step = target.track < 0 ? -1 : playingStep(target.track);
		if(target.kind == LockKind::None || step < 0)
			return;
		const auto* p = static_cast<const juce::RangedAudioParameter*>(getParameters()[_index]);
		const LiveLock lock{currentPattern(), target.track, step, _index, p->convertFrom0to1(_value)};
		for(auto& l : m_liveLocks)
			if(l.pattern == lock.pattern && l.track == lock.track && l.step == lock.step && l.parameter == lock.parameter)
			{
				l.value = lock.value;
				return;
			}
		if(m_liveLocks.size() < LiveNotes)
			m_liveLocks.push_back(lock);
	}

	// What LIVE recorded since the timer last ran, into the patterns
	void Processor::takeLive()
	{
		std::array<LiveNote, LiveNotes> notes;
		int count = 0;
		{
			const juce::AbstractFifo::ScopedRead read(m_liveFifo, LiveNotes);
			for(int i = 0; i < read.blockSize1; ++i)
				notes[static_cast<size_t>(count++)] = m_liveNotes[static_cast<size_t>(read.startIndex1 + i)];
			for(int i = 0; i < read.blockSize2; ++i)
				notes[static_cast<size_t>(count++)] = m_liveNotes[static_cast<size_t>(read.startIndex2 + i)];
		}
		if(!count && m_liveLocks.empty())
			return;
		editSequencer([&](Sequencer& _s)
		{
			for(int i = 0; i < count; ++i)
			{
				const auto& n = notes[static_cast<size_t>(i)];
				auto& p = _s.pattern(n.pattern);
				p.setTrig(n.track, n.step, true, n.velocity);
				p.step(n.track, n.step).timing = n.timing;
			}
			for(const auto& l : m_liveLocks)
				_s.pattern(l.pattern).setLock(l.track, l.step, static_cast<uint16_t>(l.parameter), l.value);
		});
		m_liveLocks.clear();
	}

	bool Processor::loadSample(const int _track, const juce::File& _file, juce::String& _error)
	{
		auto result = readSampleFile(_file);
		if(const auto* error = std::get_if<std::string>(&result))
		{
			_error = juce::String::fromUTF8(error->c_str());
			return false;
		}
		m_samples[static_cast<size_t>(_track)] = std::make_unique<Sample>(std::move(std::get<Sample>(result)));
		m_sampleRefs[static_cast<size_t>(_track)] = SampleLibrary::fingerprintOf(_file);
		publishSample(_track);
		return true;
	}

	namespace
	{
		// SOPHIE's sounds, searched once: the editor asks which one a Track holds at every frame
		const std::array<std::array<uint8_t, sophie::MachineSlots>, sophie::Sounds>& sophieSounds()
		{
			static const auto sounds = []
			{
				std::array<std::array<uint8_t, sophie::MachineSlots>, sophie::Sounds> s{};
				for(int i = 0; i < sophie::Sounds; ++i)
					s[static_cast<size_t>(i)] = sophie::soundValues(i);
				return s;
			}();
			return sounds;
		}
	}

	void Processor::setSophieSound(const int _track, const int _sound)
	{
		const auto& values = sophieSounds()[static_cast<size_t>(std::clamp(_sound, 0, sophie::Sounds - 1))];
		auto& p = m_tracks[static_cast<size_t>(_track)];
		for(size_t i = 0; i < values.size(); ++i)
		{
			auto* value = p.values[i];
			value->beginChangeGesture();
			*value = values[i];
			value->endChangeGesture();
		}
	}

	int Processor::sophieSound(const int _track) const
	{
		const auto& p = m_tracks[static_cast<size_t>(_track)];
		for(int s = 0; s < sophie::Sounds; ++s)
		{
			const auto& values = sophieSounds()[static_cast<size_t>(s)];
			bool same = true;
			for(size_t i = 0; i < values.size() && same; ++i)
				same = p.values[i]->get() == values[i];
			if(same)
				return s;
		}
		return -1;
	}

	bool Processor::loadLibrarySample(const int _track, const juce::String& _fingerprint, juce::String& _error)
	{
		if(const int slot = factorySlotOf(_fingerprint))
		{
			auto sample = factorySample(*m_library, slot);
			if(!sample)
			{
				_error = "FACTORY BANK NOT IN THE LOCKER";
				return false;
			}
			m_samples[static_cast<size_t>(_track)] = std::make_unique<Sample>(std::move(*sample));
			m_sampleRefs[static_cast<size_t>(_track)] = factoryRef(slot);
			publishSample(_track);
			return true;
		}
		if(const auto* entry = m_sampleLibrary->find(_fingerprint))
			return loadSample(_track, entry->file, _error);
		// IN THIS SET: another Track's copy
		if(const int from = trackHolding(_fingerprint); from >= 0)
		{
			if(from != _track)
			{
				m_samples[static_cast<size_t>(_track)] = std::make_unique<Sample>(*m_samples[static_cast<size_t>(from)]);
				m_sampleRefs[static_cast<size_t>(_track)] = _fingerprint;
				publishSample(_track);
			}
			return true;
		}
		_error = "No sample of the Locker has fingerprint " + _fingerprint;
		return false;
	}

	int Processor::trackHolding(const juce::String& _fingerprint) const
	{
		for(size_t t = 0; t < m_samples.size(); ++t)
			if(m_samples[t] && _fingerprint.isNotEmpty() && m_sampleRefs[t] == _fingerprint)
				return static_cast<int>(t);
		return -1;
	}

	bool Processor::auditionSample(const juce::File& _file, juce::String& _error)
	{
		auto result = readSampleFile(_file);
		if(const auto* error = std::get_if<std::string>(&result))
		{
			_error = juce::String::fromUTF8(error->c_str());
			return false;
		}
		m_auditioned = std::make_unique<Sample>(std::move(std::get<Sample>(result)));
		sendAudition();
		return true;
	}

	bool Processor::auditionFactorySample(const int _slot, juce::String& _error)
	{
		auto sample = factorySample(*m_library, _slot);
		if(!sample)
		{
			_error = "FACTORY BANK NOT IN THE LOCKER";
			return false;
		}
		m_auditioned = std::make_unique<Sample>(std::move(*sample));
		sendAudition();
		return true;
	}

	const std::vector<SampleLibrary::Entry>& Processor::factorySamples()
	{
		if(m_factoryGeneration != m_sampleLibrary->generation())
		{
			m_factoryGeneration = m_sampleLibrary->generation();
			m_factoryDressed = m_factorySamples;
			for(auto& e : m_factoryDressed)
				m_sampleLibrary->dress(e);
		}
		return m_factoryDressed;
	}

	bool Processor::removeFactory()
	{
		if(!m_libraryFromWords || !LockerImport::wordsFile(dataFolder()).deleteFile())
			return false;
		m_factorySamples.clear();
		m_factoryDressed.clear();
		return true;
	}

	int64_t Processor::factoryBytes() const
	{
		const auto words = LockerImport::wordsFile(dataFolder());
		return m_libraryFromWords && words.existsAsFile() ? words.getSize() : 0;
	}

	const std::vector<SampleLibrary::Entry>& Processor::setSamples()
	{
		// Rebuilt only when what it is made of changes, so that the editor's references stay
		m_sampleLibrary->scanOnce();
		const bool known = m_sampleLibrary->known();
		juce::String key(known ? static_cast<int>(m_sampleLibrary->generation()) : -1);
		for(const auto& ref : m_sampleRefs)
			key << "/" << ref;
		if(key == m_setKey)
			return m_setSamples;
		m_setKey = key;
		m_setSamples.clear();
		for(size_t t = 0; known && t < m_samples.size(); ++t)
		{
			const auto& sample = m_samples[t];
			const auto& ref = m_sampleRefs[t];
			const auto listed = [&](const SampleLibrary::Entry& _e) { return _e.fingerprint == ref; };
			if(!sample || ref.isEmpty() || factorySlotOf(ref) || m_sampleLibrary->find(ref)
				|| std::any_of(m_setSamples.begin(), m_setSamples.end(), listed))
				continue;
			SampleLibrary::Entry e;
			e.name = juce::String::fromUTF8(sample->name.c_str());
			e.folder = SampleLibrary::SetFolder;
			e.length = static_cast<int64_t>(sample->pcm.size());
			e.rate = sample->rate;
			e.channels = 1;
			e.bytes = 2 * e.length;
			e.fingerprint = ref;
			e.type = SampleLibrary::guessType(e.name);
			m_setSamples.push_back(e);
		}
		return m_setSamples;
	}

	bool Processor::auditionSetSample(const juce::String& _fingerprint, juce::String& _error)
	{
		const int t = trackHolding(_fingerprint);
		if(t < 0)
		{
			_error = "No Track holds that sample any more";
			return false;
		}
		m_auditioned = std::make_unique<Sample>(*m_samples[static_cast<size_t>(t)]);
		sendAudition();
		return true;
	}

	bool Processor::keepInLocker(const juce::String& _fingerprint, juce::String& _error)
	{
		const int t = trackHolding(_fingerprint);
		if(t < 0)
		{
			_error = "No Track holds that sample any more";
			return false;
		}
		const auto& sample = *m_samples[static_cast<size_t>(t)];
		// Its 16-bit values at its rate, as the state keeps them: a WAV of the Locker's own
		const auto folder = SampleLibrary::importedFolder(dataFolder()).getChildFile("From sets");
		auto name = juce::File::createLegalFileName(juce::String::fromUTF8(sample.name.c_str())).upToLastOccurrenceOf(".",
			false, false);
		if(name.isEmpty())
			name = "Sample";
		const auto file = folder.getNonexistentChildFile(name, ".wav", false);
		{
			juce::AudioBuffer<float> buffer(1, static_cast<int>(sample.pcm.size()));
			for(size_t n = 0; n < sample.pcm.size(); ++n)
				buffer.setSample(0, static_cast<int>(n), static_cast<float>(sample.pcm[n]) / 32768.0f);
			juce::WavAudioFormat wav;
			std::unique_ptr<juce::AudioFormatWriter> writer;
			if(folder.createDirectory().wasOk())
			{
				// The writer owns the stream once made; the stream is ours until then
				auto stream = std::make_unique<juce::FileOutputStream>(file);
				if(stream->openedOk())
					writer.reset(wav.createWriterFor(stream.get(), sample.rate, 1, 16, {}, 0));
				if(writer)
					stream.release();
			}
			const bool written = writer && writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
			writer.reset();
			if(!written)
			{
				file.deleteFile();
				_error = "The sample could not be written to " + folder.getFullPathName();
				return false;
			}
		}
		const auto fingerprint = SampleLibrary::fingerprintOf(file);
		if(!m_sampleLibrary->addSource(SampleLibrary::Kind::Imported, folder, "FROM SETS"))
		{
			_error = "The Locker's list could not be written";
			return false;
		}
		for(size_t i = 0; i < m_sampleRefs.size(); ++i)
			if(m_sampleRefs[i] == _fingerprint)
				m_sampleRefs[i] = fingerprint;
		// The set's state names the Tracks' samples: the host saves it again
		updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
		return true;
	}

	// m_auditioned to the audio thread, which plays it from its next block
	void Processor::sendAudition()
	{
		auto mail = std::make_unique<SampleMail>();
		mail->sample = *m_auditioned;
		mail->serial = ++m_sampleSerial;
		auto* sent = mail.get();
		m_auditionFlight.push_back(std::move(mail));
		if(const auto* old = m_auditionMail.exchange(sent, std::memory_order_acq_rel))
			m_auditionFlight.erase(std::find_if(m_auditionFlight.begin(), m_auditionFlight.end(),
				[old](const auto& _m) { return _m.get() == old; }));
		pruneSamples();
	}

	juce::String Processor::sampleName(const int _track) const
	{
		const auto& sample = m_samples[static_cast<size_t>(_track)];
		if(sample)
			return juce::String::fromUTF8(sample->name.c_str());
		// A factory sample whose bank this instrument's Locker lacks: the Track is silent, and says why
		if(const int slot = factorySlotOf(m_sampleRefs[static_cast<size_t>(_track)]))
			return "Factory " + juce::String(slot).paddedLeft('0', 2) + ", not in the Locker";
		return {};
	}

	void Processor::clearSample(const int _track)
	{
		m_samples[static_cast<size_t>(_track)].reset();
		m_sampleRefs[static_cast<size_t>(_track)].clear();
		publishSample(_track);
	}

	// A copy of the Track's sample, an empty one for none, in its mailbox. One the audio thread had not taken yet goes at
	// once; one it took, once it has given the engine that one or a later one
	void Processor::publishSample(const int _track)
	{
		const auto t = static_cast<size_t>(_track);
		auto mail = std::make_unique<SampleMail>();
		if(m_samples[t])
			mail->sample = *m_samples[t];
		mail->serial = ++m_sampleSerial;
		mail->hash = sampleHash(m_samples[t].get());
		holdSource(&mail->sample);
		auto* sent = mail.get();
		auto& flight = m_sampleFlight[t];
		flight.push_back(std::move(mail));
		if(const auto* old = m_sampleMail[t].exchange(sent, std::memory_order_acq_rel))
			flight.erase(std::find_if(flight.begin(), flight.end(), [old](const auto& _m) { return _m.get() == old; }));
		pruneSamples();
	}

	void Processor::holdSource(const Sample* _sample)
	{
		if(_sample && _sample->source
			&& std::find(m_heldSources.begin(), m_heldSources.end(), _sample->source) == m_heldSources.end())
			m_heldSources.push_back(_sample->source);
	}

	// Audio thread (or prepareToPlay): the engine reads _sample's source until the Track's next one, which this holds; the
	// one it replaces the message thread holds too, so that letting it go here frees nothing
	void Processor::giveSample(const int _track, const Sample* _sample)
	{
		m_engine->setSample(_track, _sample);
		m_engineSources[static_cast<size_t>(_track)] = _sample ? _sample->source : nullptr;
	}

	void Processor::pruneSamples()
	{
		// A source only this list holds: no Sample, mail, Kit or engine has it
		m_heldSources.erase(std::remove_if(m_heldSources.begin(), m_heldSources.end(),
			[](const auto& _s) { return _s.use_count() == 1; }), m_heldSources.end());
		for(size_t t = 0; t < m_sampleFlight.size(); ++t)
		{
			const uint32_t done = m_sampleDone[t].load(std::memory_order_acquire);
			auto& flight = m_sampleFlight[t];
			flight.erase(std::remove_if(flight.begin(), flight.end(), [done](const auto& _m) { return _m->serial <= done; }),
				flight.end());
		}
		// The auditions before the one playing: the audio thread took them all in order, and holds the last alone
		const uint32_t held = m_auditionHeld.load(std::memory_order_acquire);
		m_auditionFlight.erase(std::remove_if(m_auditionFlight.begin(), m_auditionFlight.end(),
			[held](const auto& _m) { return _m->serial < held; }), m_auditionFlight.end());
	}

	juce::File Processor::dataFolder()
	{
		// As the stack's: GEARMULATOR_DATA_ROOT in place of Documents, which tests set to a folder of their own
		const auto* root = std::getenv("GEARMULATOR_DATA_ROOT");
		const auto base = root && *root ? juce::File(juce::String::fromUTF8(root))
			: juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
		return base.getChildFile("c0remusic").getChildFile(ED_DRUMS_NAME);
	}

	juce::File Processor::legacyDataFolder()
	{
		return dataFolder().getSiblingFile("Elektron Drums Dev");
	}

	bool Processor::migrateData(const juce::File& _from, const juce::File& _to)
	{
		const auto mark = _to.getChildFile(MigratedMark);
		if(!_from.isDirectory())
			return true;
		// The mark's first line names _from, each other one a file that could not be copied, which a later load tries
		// alone: a mark of one line, nothing more
		juce::StringArray paths;
		if(mark.existsAsFile())
		{
			mark.readLines(paths);
			paths.remove(0);
			paths.removeEmptyStrings();
			if(paths.isEmpty())
				return true;
		}
		else
			for(const auto& file : _from.findChildFiles(juce::File::findFiles, true, "*", juce::File::FollowSymlinks::no))
				paths.add(file.getRelativePathFrom(_from));
		juce::StringArray failed;
		for(const auto& path : paths)
		{
			const auto file = _from.getChildFile(path), target = _to.getChildFile(path);
			if(!file.existsAsFile() || target.exists())
				continue;
			if(!target.getParentDirectory().createDirectory().wasOk() || !file.copyFileTo(target))
				failed.add(path);
		}
		failed.insert(0, _from.getFullPathName());
		return _to.createDirectory().wasOk() && mark.replaceWithText(failed.joinIntoString("\n") + "\n")
			&& failed.size() == 1;
	}

	bool Processor::addToLocker(const juce::File& _folder, const bool _copy)
	{
		std::set<juce::String> known;
		for(const auto& e : m_sampleLibrary->entries())
			known.insert(e.fingerprint);
		// A copy in a folder of its own under Imported, named after the folder imported
		const auto copy = _copy ? SampleLibrary::importedFolder(dataFolder()).getNonexistentChildFile(
			juce::File::createLegalFileName(_folder.getFileName()), {}, true) : juce::File();
		return m_import->start(_folder, std::move(known), copy);
	}

	bool Processor::takeLockerImport()
	{
		if(!m_import->take(m_lastImport))
			return false;
		const auto& r = m_lastImport;
		if(r.copy != juce::File() && r.copied > 0)
			m_sampleLibrary->addSource(SampleLibrary::Kind::Imported, r.copy, r.folder.getFileName());
		else if(r.copy != juce::File())
			r.copy.deleteRecursively();	// the folder addToLocker made, which nothing was copied into
		else if(r.copy == juce::File() && r.audio > r.known)
			m_sampleLibrary->addSource(SampleLibrary::Kind::Linked, r.folder);
		++m_imports;
		return true;
	}

	void Processor::timerCallback()
	{
		if(m_reread.exchange(false))
			updateHostDisplay(ChangeDetails().withProgramChanged(true));
		pruneSamples();
		takeLive();
		takeKitSwitch();
		m_sampleLibrary->update();
		takeLockerImport();
		if(++m_kitFollow >= 10)
		{
			followKits();
			m_sampleLibrary->follow();
			m_trackSounds->follow();
		}
	}

	bool Processor::isBusesLayoutSupported(const BusesLayout& _layout) const
	{
		if(!_layout.inputBuses.isEmpty() || _layout.outputBuses.size() != Outputs)
			return false;
		if(_layout.outputBuses[0] != juce::AudioChannelSet::stereo())
			return false;
		for(int b = 1; b < _layout.outputBuses.size(); ++b)
		{
			const auto& set = _layout.outputBuses[b];
			if(set != juce::AudioChannelSet::stereo() && !set.isDisabled())
				return false;
		}
		return true;
	}

	void Processor::prepareToPlay(const double _sampleRate, const int)
	{
		m_engine = playableRate(_sampleRate) ? std::make_unique<Engine>(*m_library, _sampleRate) : nullptr;
		m_rate = m_engine ? _sampleRate : SampleRate;
		setLatencySamples(m_engine ? m_engine->hitDelay() : 0);
		// A new engine knows nothing yet, and holds no family's chain
		for(auto* w : m_watched)
			w->due = true;
		m_engineFamily.fill(Family::Off);
		// ... no Lock, and the Tracks' samples, which a mail still on its way gives it again
		std::fill(m_locked.begin(), m_locked.end(), uint8_t{0});
		for(auto& list : m_lockedBy)
			list.clear();
		for(int t = 0; t < Tracks; ++t)
			endSlide(t);
		m_now = 0;
		if(m_engine)
			for(int t = 0; t < Tracks; ++t)
			{
				const auto* sample = m_samples[static_cast<size_t>(t)].get();
				holdSource(sample);
				giveSample(t, sample);
			}
		else
			m_engineSources.fill(nullptr);
		for(int t = 0; t < Tracks; ++t)
			m_engineSampleHash[static_cast<size_t>(t)] = sampleHash(m_samples[static_cast<size_t>(t)].get());
	}

	// The Tracks' and the shared effects' parameters to the engine: at each block, and where a pattern's Kit loads
	void Processor::syncSound(const bool _hostIsTruth)
	{
		auto& e = *m_engine;
		for(int t = 0; t < Tracks; ++t)
		{
			auto& p = m_tracks[static_cast<size_t>(t)];

			const int choice = p.machine->getIndex();
			const auto& machine = m_machines[static_cast<size_t>(choice)];
			e.setMachine(t, machine);
			// The engine has just loaded the family's chain (Engine::setMachine): the parameters' is compared with it
			auto& engineFamily = m_engineFamily[static_cast<size_t>(t)];
			if(machine.family != Family::Off && machine.family != engineFamily)
			{
				engineFamily = machine.family;
				m_chainHeld[static_cast<size_t>(t)] = parameters::valuesOf(parameters::familyChain(machine));
				p.chain[0].due = true;
			}
			// Machine 1-8 move to a new machine's defaults, which the host reads, unless a state gave them; a machine of
			// another family brings its family's chain, which the chain's parameters take in the same way
			if(choice != m_machineSeen[static_cast<size_t>(t)])
			{
				m_machineSeen[static_cast<size_t>(t)] = choice;
				auto& family = m_chainFamily[static_cast<size_t>(t)];
				const bool newChain = machine.family != Family::Off && machine.family != family;
				if(newChain)
					family = machine.family;
				if(!_hostIsTruth)
				{
					for(int i = 0; i < MachineParameters; ++i)
						setQuietly(*p.values[static_cast<size_t>(i)], static_cast<float>(e.parameter(t, i)));
					if(newChain)
					{
						const auto chain = parameters::valuesOf(parameters::familyChain(machine));
						for(size_t i = 0; i < chain.size(); ++i)
							setQuietly(*p.chain[i].parameter, static_cast<float>(chain[i]));
					}
					m_reread = true;
				}
			}
			// What a Lock holds stays, until the Track's next Hit gives the host's value back
			for(int i = 0; i < MachineParameters; ++i)
			{
				const int v = p.values[static_cast<size_t>(i)]->get();
				if(v != e.parameter(t, i) && !held(p.values[static_cast<size_t>(i)]))
					e.setParameter(t, i, static_cast<uint8_t>(v));
			}
			if(!held(p.note))
				e.setNote(t, p.note->get());
			if(!held(p.level))
				e.setLevel(t, p.level->get());
			if(!held(p.pan))
				e.setPan(t, p.pan->get());
			e.setOutput(t, p.output->get());
			e.setChokes(t, static_cast<uint32_t>(p.chokes->get()));

			// The chain whole, when any of its values moved
			bool chainMoved = false;
			for(auto& w : p.chain)
				chainMoved |= w.changed();
			if(chainMoved)
				setChainHeld(t);
			for(size_t l = 0; l < p.lfo.size(); ++l)
			{
				bool lfoMoved = false;
				for(auto& w : p.lfo[l])
					lfoMoved |= w.changed();
				if(lfoMoved)
					e.setLfo(t, static_cast<int>(l), parameters::lfoOf(lfoValues(t, static_cast<int>(l))));
			}
			for(size_t s = 0; s < p.sends.size(); ++s)
				if(p.sends[s].changed() && !held(p.sends[s].parameter))
					e.setSend(t, static_cast<Send>(s), p.sends[s].integer());
			if(p.accent.changed() && !held(p.accent.parameter))
				e.setAccent(t, p.accent.integer());
		}
		syncShared();
	}

	void Processor::sync(const bool _hostIsTruth)
	{
		auto& e = *m_engine;
		syncSound(_hostIsTruth);

		double bpm = m_tempo->get();
		juce::Optional<double> quarters;
		if(auto* playHead = getPlayHead())
			if(const auto position = playHead->getPosition())
			{
				if(const auto hostBpm = position->getBpm(); hostBpm && std::isfinite(*hostBpm))
					bpm = *hostBpm;
				if(position->getIsPlaying())
					quarters = position->getPpqPosition();
			}
		e.setTempo(bpm);
		// A transport that stands still leaves the LFOs running on at the tempo, as the machines' run without their sequencer
		m_blockBpm = bpm;
		m_blockPlaying = quarters && std::isfinite(*quarters);
		m_blockQuarters = m_blockPlaying ? *quarters : 0.0;
		if(m_blockPlaying)
			e.setPosition(*quarters);
	}

	void Processor::syncShared()
	{
		auto& e = *m_engine;
		auto& fx = e.sends();
		const auto byte = [](const Watched& _w) { return static_cast<uint8_t>(_w.integer()); };
		for(size_t i = 0; i < m_sources.size(); ++i)
			if(m_sources[i].changed())
				fx.setSource(i == 0 ? Send::Delay : Send::Reverb, static_cast<SendSource>(m_sources[i].integer()));
		for(size_t i = 0; i < m_sendOutputs.size(); ++i)
			if(m_sendOutputs[i].changed())
				fx.setOutput(static_cast<Send>(i), m_sendOutputs[i].integer());
		for(size_t i = 0; i < m_echo.size(); ++i)
			if(m_echo[i].changed())
				fx.setEcho(static_cast<int>(i), byte(m_echo[i]));
		for(size_t i = 0; i < m_gateBox.size(); ++i)
			if(m_gateBox[i].changed())
				fx.setGateBox(static_cast<int>(i), byte(m_gateBox[i]));
		for(size_t i = 0; i < m_syntaktDelay.size(); ++i)
			if(m_syntaktDelay[i].changed())
				fx.setSyntaktDelay(static_cast<int>(i), byte(m_syntaktDelay[i]));
		for(size_t i = 0; i < m_syntaktReverb.size(); ++i)
			if(m_syntaktReverb[i].changed())
				fx.setSyntaktReverb(static_cast<int>(i), byte(m_syntaktReverb[i]));
		if(m_syntaktDelaySend.changed())
			fx.setSyntaktDelaySend(byte(m_syntaktDelaySend));
		if(m_delayFree.changed())
			fx.setDelayFree(m_delayFree.value());
		for(size_t i = 0; i < m_chorus.size(); ++i)
			if(m_chorus[i].changed())
				fx.setChorus(static_cast<int>(i), byte(m_chorus[i]));
		for(size_t i = 0; i < m_phaser.size(); ++i)
			if(m_phaser[i].changed())
				fx.setPhaser(static_cast<int>(i), byte(m_phaser[i]));
		if(m_dp4SampleHold.changed())
			fx.setPhaser(PhaserDp4SampleHold, byte(m_dp4SampleHold));
		if(m_delayNote.changed())
			fx.setDelayNote(parameters::delayNoteQuarters(m_delayNote.integer()));
		if(m_delaySync.changed())
			fx.setDelaySync(m_delaySync.integer() != 0);
		for(size_t i = 0; i < m_lfoSync.size(); ++i)
		{
			const Send send = i == 0 ? Send::Chorus : Send::Phaser;
			if(m_lfoNote[i].changed())
				fx.setLfoNote(send, parameters::lfoNoteQuarters(m_lfoNote[i].integer()));
			if(m_lfoSync[i].changed())
				fx.setLfoSync(send, m_lfoSync[i].integer() != 0);
		}
		// The bytes before On: a master that comes on settles at them
		for(size_t i = 0; i < m_master.size(); ++i)
			if(m_master[i].changed())
				e.master().set(static_cast<int>(i), byte(m_master[i]));
		if(m_masterOn.changed())
			e.setMasterOn(m_masterOn.integer() != 0);
		if(m_oversampling.changed())
			e.setOversampling(1 << m_oversampling.integer());
	}

	void Processor::processBlock(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midi)
	{
		juce::ScopedNoDenormals noDenormals;
		if(!m_engine)
		{
			_buffer.clear();
			m_blocksDone.fetch_add(1, std::memory_order_acq_rel);
			return;
		}
		const uint32_t serial = m_stateSerial.load(std::memory_order_acquire);
		sync(serial != m_stateSeen);
		m_stateSeen = serial;

		// The sequencer published last, which carries on from where the one it replaces stands: the triple buffer's reader
		if(m_middle.load(std::memory_order_acquire) & Fresh)
		{
			const int old = m_playingIndex;
			m_playingIndex = m_middle.exchange(old, std::memory_order_acq_rel) & 3;
			m_sequencers[static_cast<size_t>(m_playingIndex)]->takePlayback(*m_sequencers[static_cast<size_t>(old)]);
		}
		auto& sequencer = *m_sequencers[static_cast<size_t>(m_playingIndex)];

		// The samples published since the last block
		for(size_t t = 0; t < m_sampleMail.size(); ++t)
		{
			if(!m_sampleMail[t].load(std::memory_order_acquire))
				continue;
			if(auto* mail = m_sampleMail[t].exchange(nullptr, std::memory_order_acq_rel))
			{
				giveSample(static_cast<int>(t), mail->sample.pcm.empty() ? nullptr : &mail->sample);
				m_engineSampleHash[t] = mail->hash;
				m_sampleDone[t].store(mail->serial, std::memory_order_release);
			}
		}
		if(m_auditionMail.load(std::memory_order_acquire))
			if(auto* mail = m_auditionMail.exchange(nullptr, std::memory_order_acq_rel))
			{
				m_auditionPlaying = mail->sample.pcm.empty() ? nullptr : &mail->sample;
				m_auditionAt = 0;
				m_auditionHeld.store(mail->serial, std::memory_order_release);
			}

		// A pattern asked for by the parameter or a program change
		if(const int asked = m_pattern->get() - 1; asked != m_patternAsked)
		{
			m_patternAsked = asked;
			sequencer.requestPattern(asked);
		}
		for(const auto metadata : _midi)
			if(metadata.numBytes >= 2 && (metadata.data[0] & 0xf0) == 0xc0)
				sequencer.requestPattern(metadata.data[1]);
		if(m_chainAsked.exchange(false))
			sequencer.requestChain();

		const int count = _buffer.getNumSamples();
		Transport transport;
		transport.playing = m_blockPlaying;
		transport.quarters = m_blockQuarters;
		transport.bpm = m_blockBpm;
		transport.sampleRate = m_rate;
		transport.length = count;
		sequencer.process(transport, m_events);
		// A pattern that began in the block (the transport starting on one included) loads its Kit there, read ahead
		// (prepareKits)
		const PreparedKit* kitAhead = nullptr;
		int kitAt = m_events.patternStart;
		if(const int slot = sequencer.pattern(sequencer.currentPattern()).kit; kitAt >= 0 && KitBank::valid(slot))
			kitAhead = m_prepared[static_cast<size_t>(slot)].load(std::memory_order_acquire);
		if(!kitAhead)
			kitAt = -1;

		// A bus the host took has two channels, in the buses' order
		int channel = 0;
		for(int b = 0; b < Outputs; ++b)
		{
			const auto* bus = getBus(false, b);
			const bool on = bus && bus->isEnabled();
			for(size_t c = 0; c < 2; ++c)
				m_outputs[2 * static_cast<size_t>(b) + c] = on ? _buffer.getWritePointer(channel++) : nullptr;
		}
		const auto outputs = m_outputs;

		const auto renderTo = [&](const int _sample, int& _done)
		{
			if(_sample <= _done)
				return;
			m_engine->render(m_outputs.data(), _sample - _done);
			for(size_t c = 0; c < m_outputs.size(); ++c)
				if(m_outputs[c])
					m_outputs[c] += _sample - _done;
			_done = _sample;
		};
		// The slides' steps on the way, each at its sample, before an event at the same one
		const auto upTo = [&](const int _sample, int& _done)
		{
			for(;;)
			{
				int64_t step = NoSlide;
				for(const auto& slide : m_slides)
					step = std::min(step, slide.next);
				if(step > m_now + _sample)
					break;
				renderTo(static_cast<int>(step - m_now), _done);
				for(int t = 0; t < Tracks; ++t)
					if(m_slides[static_cast<size_t>(t)].next == step)
						stepSlide(t);
			}
			renderTo(_sample, _done);
		};

		int done = 0;
		int next = 0;	// the sequencer's next event, before a host's note at the same sample
		// The pattern's Kit, before the events at its sample
		const auto kitUpTo = [&](const int _sample)
		{
			if(kitAt < 0 || kitAt > _sample)
				return;
			upTo(kitAt, done);
			switchKit(*kitAhead);
			kitAt = -1;
		};
		const auto eventsUpTo = [&](const int _sample)
		{
			for(; next < m_events.count && static_cast<int>(m_events.events[next].offset) <= _sample; ++next)
			{
				kitUpTo(static_cast<int>(m_events.events[next].offset));
				upTo(static_cast<int>(m_events.events[next].offset), done);
				applyEvent(m_events.events[next], m_now + m_events.events[next].offset);
			}
		};
		for(const auto metadata : _midi)
		{
			// The bytes as they are: a MidiMessage would allocate for a long SysEx
			if(metadata.numBytes < 3)
				continue;
			const int status = metadata.data[0] & 0xf0;
			const int track = metadata.data[1] - FirstNote;
			const int velocity = metadata.data[2];
			if((status != 0x90 && status != 0x80) || track < 0 || track >= Tracks)
				continue;
			const int at = std::clamp(metadata.samplePosition, 0, count);
			eventsUpTo(at);
			kitUpTo(at);
			upTo(at, done);
			if(status == 0x90 && velocity > 0)
			{
				releaseLocks(track);
				m_engine->noteOn(track, velocity);
				// LIVE: the note to the timer, as a trig of the step nearest; a full queue drops it
				int step = 0, timing = 0;
				if(m_live.load(std::memory_order_relaxed) && sequencer.nearestStep(track,
					m_blockQuarters + at * m_blockBpm / (60.0 * m_rate), step, timing))
				{
					const juce::AbstractFifo::ScopedWrite write(m_liveFifo, 1);
					if(write.blockSize1 > 0)
						m_liveNotes[static_cast<size_t>(write.startIndex1)] = {static_cast<uint8_t>(sequencer.currentPattern()),
							static_cast<uint8_t>(track), static_cast<uint8_t>(step), static_cast<uint8_t>(velocity),
							static_cast<int8_t>(timing)};
				}
			}
			else
			{
				m_engine->noteOff(track);
			}
		}
		eventsUpTo(count);
		kitUpTo(count);
		upTo(count, done);
		m_now += count;

		// The audition on Main, its values as they are, at its rate by a line between two
		if(m_auditionPlaying && outputs[0] && outputs[1])
		{
			const auto& pcm = m_auditionPlaying->pcm;
			const double step = m_auditionPlaying->rate / m_rate;
			constexpr float Gain = 0.7f / 32768.0f;
			for(int n = 0; n < count; ++n, m_auditionAt += step)
			{
				const auto i = static_cast<size_t>(m_auditionAt);
				if(i + 1 >= pcm.size())
				{
					m_auditionPlaying = nullptr;
					break;
				}
				const float frac = static_cast<float>(m_auditionAt - static_cast<double>(i));
				const float x = (static_cast<float>(pcm[i]) + frac * static_cast<float>(pcm[i + 1] - pcm[i])) * Gain;
				outputs[0][n] += x;
				outputs[1][n] += x;
			}
		}

		// What the editor shows: the peaks fall 20 dB a second, 10^-1 of the gain
		const float fall = static_cast<float>(std::pow(10.0, -count / m_rate));
		for(size_t b = 0; b < m_outputPeak.size(); ++b)
		{
			float peak = 0;
			for(size_t c = 2 * b; c < 2 * b + 2; ++c)
				if(const float* x = outputs[c])
					for(int n = 0; n < count; ++n)
						peak = std::max(peak, std::abs(x[n]));
			m_outputPeak[b].store(std::max(peak, m_outputPeak[b].load(std::memory_order_relaxed) * fall),
				std::memory_order_relaxed);
		}
		for(int s = 0; s < SendCount; ++s)
			m_returnPeak[static_cast<size_t>(s)].store(m_engine->sends().peak(static_cast<Send>(s)),
				std::memory_order_relaxed);
		m_reduction.store(m_masterOn.integer() ? m_engine->master().reduction() : 0.0f, std::memory_order_relaxed);
		for(int t = 0; t < Tracks; ++t)
			m_playingStep[static_cast<size_t>(t)].store(sequencer.stepAt(t, m_blockQuarters), std::memory_order_relaxed);
		m_currentPattern.store(sequencer.currentPattern(), std::memory_order_relaxed);
		for(int t = 0; t < Tracks; ++t)
			m_playingRowStep[static_cast<size_t>(t)].store(sequencer.stepSinceStart(t, m_blockQuarters),
				std::memory_order_relaxed);
		m_chainSlot.store(sequencer.chainSlot(), std::memory_order_relaxed);
		m_chainPass.store(static_cast<int>(sequencer.pass()), std::memory_order_relaxed);
		m_fillPlaying.store(sequencer.fillPlaying(), std::memory_order_relaxed);
		m_liveTracks.store(m_engine->liveTracks(), std::memory_order_relaxed);
		// After the Kit read ahead it may have loaded: the message thread frees a replaced one once it sees this
		m_blocksDone.fetch_add(1, std::memory_order_acq_rel);
	}

	// The parameters by ID, all or a Kit's, and the Tracks' samples, in elements of their own
	void Processor::writeSound(juce::XmlElement& _xml, const bool _all) const
	{
		for(auto* parameter : getParameters())
			if(auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter))
				if(_all || isKitParameter(p->getParameterIndex()))
					_xml.setAttribute(p->getParameterID(), p->convertFrom0to1(p->getValue()));
		for(int t = 0; t < Tracks; ++t)
			if(auto* element = writeSample(_xml, t))
				element->setAttribute("track", t);
	}

	// The Track's sample as a child of _xml: its copy, which file of the Library it is beside it; a factory sample only
	// its slot, so that no factory audio is written, and kept without the image that plays it. nullptr for none
	juce::XmlElement* Processor::writeSample(juce::XmlElement& _xml, const int _track) const
	{
		const auto t = static_cast<size_t>(_track);
		const auto& sample = m_samples[t];
		if(const int slot = factorySlotOf(m_sampleRefs[t]))
		{
			auto* element = _xml.createNewChildElement("sample");
			element->setAttribute("factory", slot);
			return element;
		}
		if(!sample)
			return nullptr;
		const auto bytes = sampleToBytes(*sample);
		auto* element = _xml.createNewChildElement("sample");
		element->setAttribute("name", juce::String::fromUTF8(sample->name.c_str()));
		if(m_sampleRefs[t].isNotEmpty())
			element->setAttribute("fingerprint", m_sampleRefs[t]);
		element->addTextElement(juce::MemoryBlock(bytes.data(), bytes.size()).toBase64Encoding());
		return element;
	}

	// What writeSample wrote: the sample, nullptr for none or a factory one without the image; its reference in _ref
	std::unique_ptr<Sample> Processor::readSample(const juce::XmlElement& _element, juce::String& _ref) const
	{
		_ref.clear();
		if(_element.hasAttribute("factory"))
		{
			const int slot = factorySlot(_element.getIntAttribute("factory"));
			if(!slot)
				return nullptr;
			_ref = factoryRef(slot);
			auto sample = factorySample(*m_library, slot);
			return sample ? std::make_unique<Sample>(std::move(*sample)) : nullptr;
		}
		juce::MemoryBlock bytes;
		if(!bytes.fromBase64Encoding(_element.getAllSubText()))
			return nullptr;
		auto read = sampleFromBytes(static_cast<const uint8_t*>(bytes.getData()), bytes.getSize());
		if(!read)
			return nullptr;
		_ref = _element.getStringAttribute("fingerprint");
		return std::make_unique<Sample>(std::move(*read));
	}

	// A state of a version before 5 or a Kit before 3 (_state false) whose Track played ROM-n plays the Sampler, which
	// choiceOf gives it, on factory slot n: Machine 1-8 are the same knobs of the same ROM player; Machine 9-12 the
	// Sampler's defaults (its 12-bit grain), which ROM-n had none of; the Track's sample element, a reference to the slot
	void Processor::takeRoms(juce::XmlElement& _xml, const bool _state) const
	{
		const int version = _xml.getIntAttribute("version");
		if(_state ? version >= 5 : version >= 3)
			return;
		const auto defaults = allDefaults(Sampler);
		for(int t = 0; t < Tracks; ++t)
		{
			const auto prefix = juce::String::formatted("t%02d_", t + 1);
			if(!_xml.hasAttribute(prefix + "machine"))
				continue;
			const int slot = romSlot(storedMachine(_xml.getIntAttribute(prefix + "machine"), version, _state));
			if(!slot)
				continue;
			for(int i = 8; i < vintage::SamplerParameters; ++i)
				_xml.setAttribute(prefix + "m" + juce::String(i + 1), defaults[static_cast<size_t>(i)]);
			for(auto* element = _xml.getChildByName("sample"); element; )
			{
				auto* next = element->getNextElementWithTagName("sample");
				if(element->getIntAttribute("track", -1) == t)
				{
					// A Library file's: the log and the editor say which, to load it again from the Library
					if(const auto fingerprint = element->getStringAttribute("fingerprint"); fingerprint.isNotEmpty())
						notice("Track " + juce::String(t + 1) + ": sample " + element->getStringAttribute("name")
							+ " replaced by Factory " + juce::String(slot).paddedLeft('0', 2), "fingerprint " + fingerprint);
					_xml.removeChildElement(element, true);
				}
				element = next;
			}
			auto* element = _xml.createNewChildElement("sample");
			element->setAttribute("track", t);
			element->setAttribute("factory", slot);
		}
	}

	void Processor::notice(const juce::String& _text, const juce::String& _detail) const
	{
		if(m_noticesGiven.contains(_text))
			return;
		m_noticesGiven.add(_text);
		m_notices.add(_text);
		juce::Logger::writeToLog(_text + " (" + _detail + ")");
	}

	juce::StringArray Processor::takeNotices()
	{
		return std::exchange(m_notices, {});
	}

	// A state of a version before 6 or a Kit before 4 (_state false) played a Syntakt Track's Machine k at its voice's
	// index k - 1, which Machine k no longer is (syntaktSlotOf): each value goes where it plays now, the voice's HOLD into
	// Machine 9, OVERDRIVE, which it did not reach, off; an LFO aimed at Machine 1-8 of a Syntakt Track follows its value,
	// one on HOLD at Machine 9; so do a state's Locks (takePageOrderLocks). The voice gets the words it got. A Track
	// sound's Kit (_sound its Track) holds that Track alone: an LFO aimed at another follows the machine the set plays there
	void Processor::takePageOrder(juce::XmlElement& _xml, const bool _state, const int _sound) const
	{
		const int version = _xml.getIntAttribute("version");
		if(_state ? version >= StateVersion : version >= KitBank::Version)
			return;
		std::array<bool, Tracks> syntakt{};
		for(int t = 0; t < Tracks; ++t)
		{
			const auto machine = _sound >= 0 && t != _sound
				? m_machines[static_cast<size_t>(m_tracks[static_cast<size_t>(t)].machine->getIndex())]
				: machineIn(_xml, t, _state);
			syntakt[static_cast<size_t>(t)] = machine.family == Family::Syntakt;
		}
		for(int t = 0; t < Tracks; ++t)
		{
			if(!syntakt[static_cast<size_t>(t)])
				continue;
			const auto prefix = juce::String::formatted("t%02d_m", t + 1);
			std::array<juce::String, 8> old;
			for(int i = 0; i < 8; ++i)
			{
				old[static_cast<size_t>(i)] = _xml.getStringAttribute(prefix + juce::String(i + 1));
				_xml.removeAttribute(prefix + juce::String(i + 1));
			}
			_xml.setAttribute(prefix + "9", 0);
			for(int i = 0; i < 8; ++i)
				if(old[static_cast<size_t>(i)].isNotEmpty())
					_xml.setAttribute(prefix + juce::String(syntaktSlotOf(i) + 1), old[static_cast<size_t>(i)]);
			_xml.setAttribute(prefix + "8", 0);
		}
		for(int t = 0; t < Tracks; ++t)
			for(int l = 1; l <= 2; ++l)
			{
				const auto id = juce::String::formatted("t%02d_lfo%d_", t + 1, l);
				const int aimed = juce::roundToInt(_xml.getDoubleAttribute(id + "track"));
				const int target = juce::roundToInt(_xml.getDoubleAttribute(id + "target"));
				const int track = aimed > 0 && aimed <= Tracks ? aimed - 1 : t;
				if(!syntakt[static_cast<size_t>(track)] || target < static_cast<int>(LfoTarget::Machine1)
					|| target > static_cast<int>(LfoTarget::Machine8))
					continue;
				const int slot = syntaktSlotOf(target - static_cast<int>(LfoTarget::Machine1));
				_xml.setAttribute(id + "target", static_cast<int>(machineTarget(slot)));
			}
		if(!_state)
			return;
		for(auto* pattern : _xml.getChildWithTagNameIterator("pattern"))
			takePageOrderLocks(*pattern, [&](const int _track) { return syntakt[static_cast<size_t>(_track)]; });
	}

	// The machine _xml (a state's if _state, else a Kit's) puts Track _track on, as readSound reads it: one it lacks the
	// parameter's default
	Machine Processor::machineIn(const juce::XmlElement& _xml, const int _track, const bool _state) const
	{
		const auto id = juce::String::formatted("t%02d_machine", _track + 1);
		if(!_xml.hasAttribute(id))
			return m_machines[static_cast<size_t>(defaultMachine(m_machines, _track))];
		return storedMachine(_xml.getIntAttribute(id), _xml.getIntAttribute("version"), _state);
	}

	// A pattern of before page order (takePageOrder), its Locks on Machine 1-8 of a Track _syntakt says played a Syntakt
	// machine there: each on the Machine that plays its value now. A pattern that loads a Kit at its start, the Kit's
	// machines
	void Processor::takePageOrderLocks(juce::XmlElement& _pattern, const std::function<bool(int)>& _syntakt) const
	{
		std::unique_ptr<juce::XmlElement> kit;
		if(const int slot = _pattern.getIntAttribute("kit", -1); KitBank::valid(slot))
			kit = readKit(slot);
		const auto syntakt = [&](const int _track)
		{
			return kit ? machineIn(*kit, _track, false).family == Family::Syntakt : _syntakt(_track);
		};
		for(auto* step : _pattern.getChildWithTagNameIterator("step"))
		{
			juce::StringArray locks;
			for(const auto& pair : juce::StringArray::fromTokens(step->getStringAttribute("locks"), false))
			{
				const int colon = pair.indexOfChar(':');
				const int parameter = colon > 0 ? pair.substring(0, colon).getIntValue() : -1;
				if(parameter < 0 || parameter >= static_cast<int>(m_lockTargets.size()))
				{
					locks.add(pair);
					continue;
				}
				const auto& target = m_lockTargets[static_cast<size_t>(parameter)];
				if(target.kind != LockKind::Machine || target.a >= 8 || !syntakt(target.track))
				{
					locks.add(pair);
					continue;
				}
				const auto* moved = m_tracks[static_cast<size_t>(target.track)].values[static_cast<size_t>(syntaktSlotOf(target.a))];
				locks.add(juce::String(moved->getParameterIndex()) + pair.substring(colon));
			}
			if(!locks.isEmpty())
				step->setAttribute("locks", locks.joinIntoString(" "));
		}
	}

	void Processor::getStateInformation(juce::MemoryBlock& _data)
	{
		juce::XmlElement xml("ElektronDrums");
		// 2: ticket 18's parameters; the format is the same, every parameter by ID. 3: the Sampler last of Machine's
		// choices, the Tracks' samples and the patterns, in elements of their own. The Kit played's Slot since ticket 19.
		// 4: Machine's choices without the Machinedrum's INP, CTR, MID and RAM machines, OPEN DRUM's after the Sampler.
		// 5: without the ROM machines, a factory sample a Track's reference to it (writeSample). 6: a Syntakt machine's
		// Machine 1-8 in its SYN page's order (takePageOrder)
		xml.setAttribute("version", StateVersion);
		writeSound(xml, true);
		xml.setAttribute("kit", m_kitSlot);
		for(int i = 0; i < SeqPatterns; ++i)
			if(auto pattern = patternXml(m_edited->pattern(i), i))
				xml.addChildElement(pattern.release());
		xml.addChildElement(sequencerXml(*m_edited, chainSlot() >= 0 || m_chainAsked.load()).release());
		copyXmlToBinary(xml, _data);
	}

	// What a state or a Kit gives: the parameters, all or a Kit's, and the Tracks' samples. Processing suspended
	void Processor::readSound(const juce::XmlElement& _xml, const bool _all)
	{
		// No Lock outlives the state: the engine takes every value again at the next block
		std::fill(m_locked.begin(), m_locked.end(), uint8_t{0});
		for(auto& list : m_lockedBy)
			list.clear();
		for(int t = 0; t < Tracks; ++t)
			endSlide(t);
		for(auto& p : m_tracks)
		{
			p.chain[0].due = true;
			for(auto& lfo : p.lfo)
				lfo[0].due = true;
			for(auto& send : p.sends)
				send.due = true;
			p.accent.due = true;
		}
		// A parameter the state lacks takes its default; one it holds that is not ours is not read
		for(auto* parameter : getParameters())
		{
			auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter);
			if(!p || !(_all || isKitParameter(p->getParameterIndex())))
				continue;
			if(_xml.hasAttribute(p->getParameterID()))
				setQuietly(*p, static_cast<float>(_xml.getDoubleAttribute(p->getParameterID())));
			else
				static_cast<juce::AudioProcessorParameter&>(*p).setValue(p->getDefaultValue());
		}
		// A state or Kit from before Delay Sync played its TIME: it keeps it, a new one starts synced
		if(auto* sync = m_delaySync.parameter; !_xml.hasAttribute(sync->getParameterID())
			&& (_all || isKitParameter(sync->getParameterIndex())))
			setQuietly(*sync, 0.0f);
		// Machine's choice in the list the state or Kit was written with
		const int version = _xml.getIntAttribute("version");
		for(auto& p : m_tracks)
			if(_xml.hasAttribute(p.machine->getParameterID()))
				setQuietly(*p.machine, static_cast<float>(choiceOf(m_machines,
					_xml.getIntAttribute(p.machine->getParameterID()), version, _all)));
		// Machine 9-24, which a state or Kit from before them lacks: the machine's own defaults
		for(auto& p : m_tracks)
		{
			const auto all = allDefaults(m_machines[static_cast<size_t>(p.machine->getIndex())]);
			for(size_t i = 8; i < p.values.size(); ++i)
				if(!_xml.hasAttribute(p.values[i]->getParameterID()))
					setQuietly(*p.values[i], static_cast<float>(all[i]));
		}
		// ... but a chain the state lacks is the one its machine's family brings, as the engine loads it
		for(auto& p : m_tracks)
		{
			const auto& machine = m_machines[static_cast<size_t>(p.machine->getIndex())];
			if(machine.family == Family::Off)
				continue;
			const auto chain = parameters::valuesOf(parameters::familyChain(machine));
			for(size_t i = 0; i < chain.size(); ++i)
				if(!_xml.hasAttribute(p.chain[i].parameter->getParameterID()))
					setQuietly(*p.chain[i].parameter, static_cast<float>(chain[i]));
		}

		// The samples the state holds, none else
		std::array<const juce::XmlElement*, Tracks> samples{};
		for(const auto* element : _xml.getChildWithTagNameIterator("sample"))
			if(const int t = element->getIntAttribute("track", -1); t >= 0 && t < Tracks)
				samples[static_cast<size_t>(t)] = element;
		for(int t = 0; t < Tracks; ++t)
		{
			auto& sample = m_samples[static_cast<size_t>(t)];
			sample.reset();
			m_sampleRefs[static_cast<size_t>(t)].clear();
			if(samples[static_cast<size_t>(t)])
				sample = readSample(*samples[static_cast<size_t>(t)], m_sampleRefs[static_cast<size_t>(t)]);
			publishSample(t);
		}
	}

	void Processor::setStateInformation(const void* _data, const int _size)
	{
		jassert(juce::MessageManager::existsAndIsCurrentThread());
		const auto xml = getXmlFromBinary(_data, _size);
		if(!xml || !xml->hasTagName("ElektronDrums"))
			return;
		takeRoms(*xml, true);
		takePageOrder(*xml, true);
		// No block meanwhile: the audio thread sees the state whole
		suspendProcessing(true);
		readSound(*xml, true);
		for(int i = 0; i < SeqPatterns; ++i)
			m_edited->pattern(i) = Pattern();
		for(const auto* element : xml->getChildWithTagNameIterator("pattern"))
			if(const int i = element->getIntAttribute("index", -1); i >= 0 && i < SeqPatterns)
				readPattern(m_edited->pattern(i), *element);
		if(readSequencer(*m_edited, xml->getChildByName("sequencer")))
			requestChain();
		publishSequencer();

		m_stateSerial.fetch_add(1, std::memory_order_acq_rel);
		suspendProcessing(false);
		updateHostDisplay(ChangeDetails().withProgramChanged(true));

		// The Kit played, compared from now on with what its Slot holds now
		const int slot = xml->getIntAttribute("kit", -1);
		m_kitSlot = KitBank::valid(slot) ? slot : -1;
		m_kits->follow();
		const auto kit = m_kitSlot < 0 ? nullptr : readKit(m_kitSlot);
		rememberKit(kit.get());
		prepareKits(~uint64_t{0});
	}

	// ---- the Kits ----

	// What the Slot played holds, to compare the sound with: _kit's values as readSound gives them (a missing one, its
	// default), nullptr for an empty Slot
	void Processor::rememberKit(const juce::XmlElement* _kit)
	{
		m_kitHeld = _kit != nullptr;
		m_kitValues.assign(m_kitParameter.size(), 0.0f);
		for(auto& s : m_kitSamples)
			s.reset();
		for(auto& r : m_kitRefs)
			r.clear();
		if(!_kit)
			return;
		m_kitValues = kitValuesOf(*_kit);
		for(const auto* element : _kit->getChildWithTagNameIterator("sample"))
		{
			const int t = element->getIntAttribute("track", -1);
			if(t >= 0 && t < Tracks)
				m_kitSamples[static_cast<size_t>(t)] = readSample(*element, m_kitRefs[static_cast<size_t>(t)]);
		}
	}

	// A Slot's Kit as it plays: a Kit of a version before 3 on a ROM machine plays the Sampler (takeRoms)
	std::unique_ptr<juce::XmlElement> Processor::readKit(const int _slot) const
	{
		auto kit = m_kits->read(_slot);
		if(kit)
		{
			takeRoms(*kit, false);
			takePageOrder(*kit, false);
		}
		return kit;
	}

	// _kit's values as readSound gives them, normalised by parameter index: a missing one, its default; Machine 9-24
	// that it lacks, its machine's defaults; a chain it lacks, its machine's family's
	std::vector<float> Processor::kitValuesOf(const juce::XmlElement& _kit) const
	{
		std::vector<float> values(m_kitParameter.size(), 0.0f);
		const int version = _kit.getIntAttribute("version");
		for(auto* parameter : getParameters())
		{
			const auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter);
			if(!p || !isKitParameter(p->getParameterIndex()))
				continue;
			const auto& id = p->getParameterID();
			const float value = id.endsWith("_machine")
				? static_cast<float>(choiceOf(m_machines, _kit.getIntAttribute(id), version, false))
				: static_cast<float>(_kit.getDoubleAttribute(id));
			values[static_cast<size_t>(p->getParameterIndex())] = _kit.hasAttribute(id)
				? p->convertTo0to1(value) : p->getDefaultValue();
		}
		// As readSound: a Kit from before Delay Sync keeps its TIME
		if(const auto* sync = m_delaySync.parameter; isKitParameter(sync->getParameterIndex())
			&& !_kit.hasAttribute(sync->getParameterID()))
			values[static_cast<size_t>(sync->getParameterIndex())] = 0.0f;
		for(const auto& p : m_tracks)
		{
			const auto& id = p.machine->getParameterID();
			const int choice = std::clamp(_kit.hasAttribute(id) ? choiceOf(m_machines, _kit.getIntAttribute(id), version, false)
				: defaultMachine(m_machines, static_cast<int>(&p - m_tracks.data())), 0, static_cast<int>(m_machines.size()) - 1);
			const auto& machine = m_machines[static_cast<size_t>(choice)];
			const auto all = allDefaults(machine);
			for(size_t i = 8; i < p.values.size(); ++i)
				if(!_kit.hasAttribute(p.values[i]->getParameterID()))
					values[static_cast<size_t>(p.values[i]->getParameterIndex())]
						= p.values[i]->convertTo0to1(static_cast<float>(all[i]));
			if(machine.family == Family::Off)
				continue;
			const auto chain = parameters::valuesOf(parameters::familyChain(machine));
			for(size_t i = 0; i < chain.size(); ++i)
				if(!_kit.hasAttribute(p.chain[i].parameter->getParameterID()))
					values[static_cast<size_t>(p.chain[i].parameter->getParameterIndex())]
						= p.chain[i].parameter->convertTo0to1(static_cast<float>(chain[i]));
		}
		return values;
	}

	void Processor::followKits()
	{
		m_kitFollow = 0;
		takeKitSwitch();
		const uint64_t changed = m_kits->follow();
		if(m_kitSlot >= 0 && (changed >> m_kitSlot) & 1)
		{
			const auto kit = readKit(m_kitSlot);
			rememberKit(kit.get());
		}
		if(changed)
			prepareKits(changed);
	}

	// The Slots the patterns load at their start, read ahead where they are not yet or their file changed (_changed, a
	// bit per Slot); the ones no pattern loads any more, given up. The replaced ones wait for the block that may read
	// them to end
	void Processor::prepareKits(const uint64_t _changed)
	{
		uint64_t used = 0;
		for(int i = 0; i < SeqPatterns; ++i)
			if(const int slot = m_edited->pattern(i).kit; KitBank::valid(slot))
				used |= uint64_t{1} << slot;
		for(int s = 0; s < KitSlots; ++s)
		{
			auto& owned = m_preparedKits[static_cast<size_t>(s)];
			const bool wanted = (used >> s) & 1;
			if(wanted && owned && !((_changed >> s) & 1))
				continue;
			if(!wanted && !owned)
				continue;
			std::unique_ptr<PreparedKit> kit;
			if(wanted)
				if(auto xml = readKit(s))
				{
					kit = std::make_unique<PreparedKit>();
					kit->slot = s;
					kit->values = kitValuesOf(*xml);
					for(const auto* element : xml->getChildWithTagNameIterator("sample"))
					{
						const auto t = static_cast<size_t>(element->getIntAttribute("track", -1));
						if(t >= Tracks)
							continue;
						kit->samples[t] = readSample(*element, kit->refs[t]);
						kit->sampleHash[t] = sampleHash(kit->samples[t].get());
						holdSource(kit->samples[t].get());
					}
					kit->xml = std::move(xml);
				}
			m_prepared[static_cast<size_t>(s)].store(kit.get(), std::memory_order_release);
			// The blocks done counted after the store: a block that still reads the old one ends past this count
			if(owned)
				m_preparedRetired.emplace_back(std::move(owned), m_blocksDone.load(std::memory_order_acquire));
			owned = std::move(kit);
		}
		// A block that read a replaced one has ended once one more is done than when it was replaced; the one it may have
		// loaded taken first
		const uint64_t done = m_blocksDone.load(std::memory_order_acquire);
		takeKitSwitch();
		m_preparedRetired.erase(std::remove_if(m_preparedRetired.begin(), m_preparedRetired.end(),
			[done](const auto& _r) { return done > _r.second; }), m_preparedRetired.end());
	}

	// The audio thread loaded a pattern's Kit: it is the Kit played, its samples the Tracks'
	void Processor::takeKitSwitch()
	{
		const auto* kit = m_kitSwitched.exchange(nullptr, std::memory_order_acq_rel);
		if(!kit)
			return;
		for(size_t t = 0; t < Tracks; ++t)
		{
			m_samples[t] = kit->samples[t] ? std::make_unique<Sample>(*kit->samples[t]) : nullptr;
			m_sampleRefs[t] = kit->refs[t];
		}
		m_kitSlot = kit->slot;
		rememberKit(kit->xml.get());
	}

	// Audio thread: the Kit's values where they differ, its samples where they do, as a state loads but within the
	// block, with nothing allocated: what the Tracks lock gives the Kit's values back, the host re-reads them
	void Processor::switchKit(const PreparedKit& _kit)
	{
		const auto& all = getParameters();
		for(size_t i = 0; i < _kit.values.size(); ++i)
			if(m_kitParameter[i] && all[static_cast<int>(i)]->getValue() != _kit.values[i])
				all[static_cast<int>(i)]->setValue(_kit.values[i]);
		syncSound(true);
		for(int t = 0; t < Tracks; ++t)
		{
			releaseLocks(t);
			auto& hash = m_engineSampleHash[static_cast<size_t>(t)];
			if(hash == _kit.sampleHash[static_cast<size_t>(t)])
				continue;
			giveSample(t, _kit.samples[static_cast<size_t>(t)].get());
			hash = _kit.sampleHash[static_cast<size_t>(t)];
		}
		m_kitSwitched.store(const_cast<PreparedKit*>(&_kit), std::memory_order_release);
		m_reread = true;
	}

	void Processor::setPatternKit(const int _pattern, const int _slot)
	{
		editSequencer([&](Sequencer& _s) { _s.pattern(_pattern).kit = KitBank::valid(_slot) ? _slot : -1; });
		prepareKits(0);
	}

	bool Processor::exportKit(const int _slot, const juce::File& _file)
	{
		followKits();
		const auto kit = m_kits->read(_slot);
		return kit && KitBank::writeFile(_file, *kit);
	}

	bool Processor::importKit(const int _slot, const juce::File& _file)
	{
		const auto kit = KitBank::readFile(_file, KitBank::Tag);
		if(!kit || !m_kits->write(_slot, *kit))
			return false;
		followKits();
		return true;
	}

	namespace
	{
		constexpr const char* PatternTag = "ElektronDrumsPattern";
	}

	bool Processor::exportPattern(const int _pattern, const juce::File& _file) const
	{
		juce::XmlElement file(PatternTag);
		file.setAttribute("version", 2);	// 2: its Locks on a Syntakt machine's Machine 1-8 in page order
		if(auto pattern = patternXml(m_edited->pattern(_pattern), _pattern))
			file.addChildElement(pattern.release());
		return KitBank::writeFile(_file, file);
	}

	bool Processor::importPattern(const int _pattern, const juce::File& _file)
	{
		const auto file = KitBank::readFile(_file, PatternTag);
		if(!file)
			return false;
		auto* xml = file->getChildByName("pattern");
		// A pattern of before page order: its Locks where the Tracks' machines play them now
		if(xml && file->getIntAttribute("version") < 2)
			takePageOrderLocks(*xml, [this](const int _track)
			{
				return m_machines[static_cast<size_t>(m_tracks[static_cast<size_t>(_track)].machine->getIndex())].family
					== Family::Syntakt;
			});
		editSequencer([&](Sequencer& _s)
		{
			_s.pattern(_pattern) = Pattern();
			if(xml)
				readPattern(_s.pattern(_pattern), *xml);
		});
		prepareKits(0);
		return true;
	}

	// ---- the Track sounds ----

	namespace
	{
		// A Track's parameters that are its sound: not its Output nor its Chokes, the Kit's routing
		bool soundSuffix(const juce::String& _suffix)
		{
			return _suffix != "output" && _suffix != "chokes";
		}
	}

	std::unique_ptr<juce::XmlElement> Processor::trackSoundOf(const int _track) const
	{
		auto xml = std::make_unique<juce::XmlElement>(TrackSounds::Tag);
		xml->setAttribute("version", KitBank::Version);
		xml->setAttribute("machineName", m_tracks[static_cast<size_t>(_track)].machine->getCurrentChoiceName());
		for(const auto& id : trackIds(_track))
		{
			const auto suffix = id.substring(4);
			if(!soundSuffix(suffix))
				continue;
			const auto* p = parameter(id);
			xml->setAttribute(suffix, p->convertFrom0to1(p->getValue()));
		}
		writeSample(*xml, _track);
		return xml;
	}

	// _sound as _track's Kit would hold it: its IDs given the Track's prefix, its sample the Track's, then a ROM machine
	// taken as a Kit's is (takeRoms)
	std::unique_ptr<juce::XmlElement> Processor::soundAsKit(const juce::XmlElement& _sound, const int _track) const
	{
		auto kit = std::make_unique<juce::XmlElement>(KitBank::Tag);
		kit->setAttribute("version", _sound.getIntAttribute("version"));
		const auto prefix = juce::String::formatted("t%02d_", _track + 1);
		for(int i = 0; i < _sound.getNumAttributes(); ++i)
			if(const auto name = _sound.getAttributeName(i); parameter(prefix + name) && soundSuffix(name))
				kit->setAttribute(prefix + name, _sound.getAttributeValue(i));
		if(const auto* sample = _sound.getChildByName("sample"))
		{
			auto* element = new juce::XmlElement(*sample);
			element->setAttribute("track", _track);
			kit->addChildElement(element);
		}
		takeRoms(*kit, false);
		takePageOrder(*kit, false, _track);
		return kit;
	}

	juce::String Processor::trackSoundMachine(const TrackSounds::Entry& _sound) const
	{
		const int choice = _sound.machineChoice < 0 ? -1 : choiceOf(m_machines, _sound.machineChoice, _sound.version, false);
		if(choice < 0 || choice >= static_cast<int>(m_machines.size()))
			return _sound.machine;
		return machineName(m_machines[static_cast<size_t>(choice)]);
	}

	juce::File Processor::saveTrackSound(const int _track, const juce::String& _name)
	{
		auto xml = trackSoundOf(_track);
		xml->setAttribute("name", _name.trim().isEmpty() ? juce::String("Sound") : _name.trim());
		return m_trackSounds->write(*xml);
	}

	bool Processor::loadTrackSound(const int _track, const juce::File& _file)
	{
		jassert(juce::MessageManager::existsAndIsCurrentThread());
		const auto sound = m_trackSounds->read(_file);
		if(!sound || _track < 0 || _track >= Tracks)
			return false;
		// The sound as this Track's Kit would hold it, then read as a Kit is
		const auto kit = soundAsKit(*sound, _track);
		const auto values = kitValuesOf(*kit);

		// As a Kit loads: no block meanwhile, the host reads the parameters again, none told as an edit
		// ... and no Lock of the Track outlives it: the engine takes its values again at the next block
		suspendProcessing(true);
		auto& locked = m_lockedBy[static_cast<size_t>(_track)];
		for(const int i : locked)
			m_locked[static_cast<size_t>(i)] = 0;
		locked.clear();
		endSlide(_track);
		auto& p = m_tracks[static_cast<size_t>(_track)];
		p.chain[0].due = true;
		for(auto& lfo : p.lfo)
			lfo[0].due = true;
		for(auto& send : p.sends)
			send.due = true;
		p.accent.due = true;
		for(const auto& id : trackIds(_track))
			if(soundSuffix(id.substring(4)))
			{
				auto* q = parameter(id);
				static_cast<juce::AudioProcessorParameter*>(q)->setValue(values[static_cast<size_t>(q->getParameterIndex())]);
			}
		auto& sample = m_samples[static_cast<size_t>(_track)];
		sample.reset();
		m_sampleRefs[static_cast<size_t>(_track)].clear();
		if(const auto* element = kit->getChildByName("sample"))
			sample = readSample(*element, m_sampleRefs[static_cast<size_t>(_track)]);
		publishSample(_track);
		m_stateSerial.fetch_add(1, std::memory_order_acq_rel);
		suspendProcessing(false);
		updateHostDisplay(ChangeDetails().withProgramChanged(true));
		return true;
	}

	bool Processor::auditionTrackSound(const juce::File& _file)
	{
		const auto sound = m_trackSounds->read(_file);
		if(!sound)
			return false;
		// On Track 1 of an engine of its own at its defaults, Main alone, one Hit at velocity 100: the Sampler's length
		const auto kit = soundAsKit(*sound, 0);
		const auto values = kitValuesOf(*kit);
		const auto plain = [&](const juce::RangedAudioParameter* _p)
		{
			return juce::roundToInt(_p->convertFrom0to1(values[static_cast<size_t>(_p->getParameterIndex())]));
		};
		const auto& p = m_tracks[0];
		Engine e(*m_library);
		e.setTempo(m_tempo->get());
		e.setMachine(0, m_machines[static_cast<size_t>(plain(p.machine))]);
		for(int i = 0; i < MachineParameters; ++i)
			if(const int v = plain(p.values[static_cast<size_t>(i)]); v != e.parameter(0, i))
				e.setParameter(0, i, static_cast<uint8_t>(v));
		e.setNote(0, plain(p.note));
		e.setLevel(0, plain(p.level));
		e.setPan(0, plain(p.pan));
		parameters::ChainArray chain{};
		for(size_t i = 0; i < chain.size(); ++i)
			chain[i] = plain(p.chain[i].parameter);
		e.setChain(0, parameters::chainOf(chain));
		for(size_t l = 0; l < p.lfo.size(); ++l)
		{
			parameters::LfoArray lfo{};
			for(size_t i = 0; i < lfo.size(); ++i)
				lfo[i] = p.lfo[l][i].parameter->convertFrom0to1(values[static_cast<size_t>(p.lfo[l][i].parameter->getParameterIndex())]);
			e.setLfo(0, static_cast<int>(l), parameters::lfoOf(lfo));
		}
		for(size_t s = 0; s < p.sends.size(); ++s)
			e.setSend(0, static_cast<Send>(s), plain(p.sends[s].parameter));
		e.setAccent(0, plain(p.accent.parameter));
		std::unique_ptr<Sample> sample;
		juce::String ref;
		if(const auto* element = kit->getChildByName("sample"))
			sample = readSample(*element, ref);
		e.setSample(0, sample.get());
		e.noteOn(0, 100);
		// The ROM player's 1.29 s; on VINTAGE the sample at its pitch and a little tail, at most what a Sample holds
		size_t length = Sample::MaxLength;
		if(sample && m_machines[static_cast<size_t>(plain(p.machine))] == Sampler
			&& vintage::isVintage(plain(p.values[vintage::GrainSlot])))
		{
			const double played = static_cast<double>(sample->pcm.size()) * SampleRate / sample->rate
				/ std::exp2(vintage::semitones(plain(p.values[0])) / 12);
			length = static_cast<size_t>(std::clamp(played + 4096.0, 1.0, static_cast<double>(Sample::MaxSourceLength)));
		}
		std::vector<float> left(length), right(length), mono(length);
		std::array<float*, 2 * Outputs> out{};
		out[0] = left.data();
		out[1] = right.data();
		e.render(out.data(), static_cast<int>(length));
		for(size_t n = 0; n < mono.size(); ++n)
			mono[n] = 0.5f * (left[n] + right[n]);
		auto made = makeSample(sound->getStringAttribute("name").toStdString(), mono, SampleRate);
		if(!std::holds_alternative<Sample>(made))
			return false;
		m_auditioned = std::make_unique<Sample>(std::move(std::get<Sample>(made)));
		auto mail = std::make_unique<SampleMail>();
		mail->sample = *m_auditioned;
		mail->serial = ++m_sampleSerial;
		auto* sent = mail.get();
		m_auditionFlight.push_back(std::move(mail));
		if(const auto* old = m_auditionMail.exchange(sent, std::memory_order_acq_rel))
			m_auditionFlight.erase(std::find_if(m_auditionFlight.begin(), m_auditionFlight.end(),
				[old](const auto& _m) { return _m.get() == old; }));
		pruneSamples();
		return true;
	}

	bool Processor::loadKit(const int _slot)
	{
		jassert(juce::MessageManager::existsAndIsCurrentThread());
		followKits();
		const auto kit = readKit(_slot);
		if(!kit)
			return false;
		// As a state loads: no block meanwhile, the host reads the parameters again, none told as an edit
		suspendProcessing(true);
		readSound(*kit, false);
		m_stateSerial.fetch_add(1, std::memory_order_acq_rel);
		suspendProcessing(false);
		updateHostDisplay(ChangeDetails().withProgramChanged(true));
		m_kitSlot = _slot;
		rememberKit(kit.get());
		return true;
	}

	bool Processor::saveKit(const int _slot)
	{
		followKits();
		if(!KitBank::valid(_slot))
			return false;
		const auto& slot = m_kits->slot(_slot);
		juce::XmlElement kit(KitBank::Tag);
		kit.setAttribute("version", KitBank::Version);
		kit.setAttribute("name", slot.empty() ? "Kit " + juce::String(_slot + 1).paddedLeft('0', 2) : slot.name);
		writeSound(kit, false);
		if(!m_kits->write(_slot, kit))
			return false;
		m_kitSlot = _slot;
		rememberKit(&kit);
		m_kits->follow();
		prepareKits(uint64_t{1} << _slot);
		return true;
	}

	bool Processor::renameKit(const int _slot, const juce::String& _name)
	{
		followKits();
		auto kit = m_kits->read(_slot);
		if(!kit)
			return false;
		kit->setAttribute("name", _name);
		const bool written = m_kits->write(_slot, *kit);
		followKits();
		return written;
	}

	bool Processor::clearKit(const int _slot)
	{
		followKits();
		if(!KitBank::valid(_slot) || m_kits->slot(_slot).empty())
			return false;
		const bool cleared = m_kits->clear(_slot);
		followKits();
		return cleared;
	}

	bool Processor::kitDiffers() const
	{
		if(m_kitSlot < 0)
			return false;
		if(!m_kitHeld)
			return true;
		// A float parameter's value comes back from its plain value within a rounding
		for(size_t i = 0; i < m_kitValues.size(); ++i)
			if(m_kitParameter[i] && std::abs(getParameters()[static_cast<int>(i)]->getValue() - m_kitValues[i]) > 1e-5f)
				return true;
		// ... and the samples, a factory one by its slot alone: without the image it plays nothing, its reference stays
		for(size_t t = 0; t < m_samples.size(); ++t)
		{
			const auto* a = m_samples[t].get();
			const auto* b = m_kitSamples[t].get();
			if(m_sampleRefs[t] != m_kitRefs[t] || !a != !b || (a && (a->name != b->name || a->rate != b->rate || a->pcm != b->pcm)))
				return true;
		}
		return false;
	}

	juce::AudioProcessorEditor* Processor::createEditor()
	{
		return new Editor(*this);
	}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new elektron::drums::Processor();
}
