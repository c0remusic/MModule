#include "stDrumsProcessor.h"

#include <algorithm>
#include <cstdlib>

#include "stDrumsEditor.h"

namespace stDrums
{
	namespace
	{
		constexpr int FirstNote = 36;	// notes 36-51: tracks 1-16

		bool synthPage(const int _id)
		{
			return _id >= syntakt::Syntakt::Tune && _id <= syntakt::Syntakt::Overdrive;
		}
	}

	juce::AudioProcessor::BusesProperties Processor::buses()
	{
		auto b = BusesProperties().withOutput("Main", juce::AudioChannelSet::stereo(), true);
		for(int t = 0; t < TrackOuts; ++t)
			b = b.withOutput(juce::String::formatted("Track %02d", t + 1), juce::AudioChannelSet::mono(), false);
		return b;
	}

	Processor::Processor() : AudioProcessor(buses())
	{
		using namespace parameters;
		juce::StringArray machines;
		for(const auto* name : MachineNames)
			machines.add(name);

		for(int t = 0; t < Tracks; ++t)
		{
			const auto id = trackPrefix(t, true);
			const juce::String name = trackPrefix(t, false);
			auto& p = m_tracks[static_cast<size_t>(t)];
			p.machine = new juce::AudioParameterChoice({id + "machine", 1}, name + "Machine", machines, 0);
			addParameter(p.machine);
			p.note = new juce::AudioParameterInt({id + "note", 1}, name + "Note", 0, 127, DefaultNote);
			addParameter(p.note);
			p.level = new juce::AudioParameterInt({id + "level", 1}, name + "Level", 0, 127, 0);
			addParameter(p.level);
			for(size_t k = 0; k < Track.size(); ++k)
			{
				p.values[k] = new juce::AudioParameterInt({valueId(id.c_str(), Track[k].id), 1}, name + Track[k].name,
					0, 127, 0);
				addParameter(p.values[k]);
			}
		}
		for(size_t k = 0; k < Fx.size(); ++k)
		{
			m_fx[k] = new juce::AudioParameterInt({valueId("fx_", Fx[k].id), 1}, Fx[k].name, 0, 127, 0);
			addParameter(m_fx[k]);
		}
		// The host's tempo where its playhead gives one, else this
		m_tempo = new juce::AudioParameterFloat({"tempo", 1}, "Tempo", juce::NormalisableRange<float>(30.0f, 300.0f),
			120.0f);
		addParameter(m_tempo);

		loadFromDataFolder();
		startTimerHz(20);
	}

	Processor::~Processor()
	{
		stopTimer();
		delete m_pending.exchange(nullptr);
		delete m_retired.exchange(nullptr);
		delete m_active;
	}

	juce::File Processor::dataFolder()
	{
		// As the stack's (jucePluginLib's Tools::getPublicDataFolder): GEARMULATOR_DATA_ROOT in place of Documents,
		// which tests set to a folder of their own
		const auto* root = std::getenv("GEARMULATOR_DATA_ROOT");
		const auto base = root && *root ? juce::File(juce::String::fromUTF8(root))
			: juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
		return base.getChildFile("c0remusic").getChildFile("ST Drums");
	}

	juce::File Processor::romsFolder()
	{
		return dataFolder().getChildFile("roms");
	}

	void Processor::loadFromDataFolder()
	{
		auto files = romsFolder().findChildFiles(juce::File::findFiles, false, "*.syx");
		files.sort();
		for(const auto& f : files)
		{
			if(loadFirmware(f, false))
				return;
		}
		m_status = files.isEmpty() ? "No Syntakt OS 1.41 .syx in " + romsFolder().getFullPathName()
			: "No .syx in " + romsFolder().getFullPathName() + " is Syntakt OS 1.41";
	}

	bool Processor::loadFirmware(const juce::File& _file, const bool _keep)
	{
		juce::MemoryBlock data;
		if(!_file.loadFileAsData(data))
		{
			m_status = "Cannot read " + _file.getFullPathName();
			return false;
		}
		const auto* bytes = static_cast<const uint8_t*>(data.getData());
		auto firmware = std::make_shared<Firmware>();
		std::string error;
		if(!load(std::vector<uint8_t>(bytes, bytes + data.getSize()), *firmware, error))
		{
			m_status = _file.getFileName() + ": " + juce::String(error);
			return false;
		}
		auto engine = std::make_unique<Engine>(std::move(firmware));

		if(_keep)
		{
			const auto kept = romsFolder().getChildFile(_file.getFileName());
			if(kept != _file && !(romsFolder().createDirectory() && _file.copyFileTo(kept)))
			{
				m_status = "Cannot copy " + _file.getFileName() + " into " + romsFolder().getFullPathName();
				return false;
			}
		}

		// The firmware's kit, which the parameters take when no state gave them one
		takeKit(*engine);
		if(!m_hasKit)
		{
			for(const auto& [parameter, value] : m_kit)
				setQuietly(*parameter, value);
			m_hasKit = true;
			updateHostDisplay(ChangeDetails().withProgramChanged(true));
		}
		// The engine the audio thread did not take yet is freed here; the one it plays comes back as retired
		delete m_pending.exchange(engine.release(), std::memory_order_acq_rel);
		m_firmwareLoaded = true;
		m_status = "Syntakt OS 1.41 from " + _file.getFileName();
		return true;
	}

	void Processor::setQuietly(juce::RangedAudioParameter& _parameter, const float _value)
	{
		// The value without telling the host as an edit: a re-read (updateHostDisplay) tells it
		static_cast<juce::AudioProcessorParameter&>(_parameter).setValue(_parameter.convertTo0to1(_value));
	}

	void Processor::takeKit(const Engine& _engine)
	{
		using namespace parameters;
		const auto& module = _engine.module();
		m_kit.clear();
		for(int t = 0; t < Tracks; ++t)
		{
			auto& p = m_tracks[static_cast<size_t>(t)];
			m_kit.emplace_back(p.machine, static_cast<float>(std::max(machineIndex(module.machine(t)), 0)));
			m_kit.emplace_back(p.level, static_cast<float>(valueOf(module.level(t))));
			for(size_t k = 0; k < Track.size(); ++k)
				m_kit.emplace_back(p.values[k], static_cast<float>(valueOf(module.parameter(t, Track[k].id))));
		}
		for(size_t k = 0; k < Fx.size(); ++k)
			m_kit.emplace_back(m_fx[k], static_cast<float>(valueOf(module.fxParameter(Fx[k].id))));
	}

	void Processor::timerCallback()
	{
		delete m_retired.exchange(nullptr, std::memory_order_acq_rel);
		if(m_reread.exchange(false))
			updateHostDisplay(ChangeDetails().withProgramChanged(true));
	}

	bool Processor::isBusesLayoutSupported(const BusesLayout& _layout) const
	{
		if(!_layout.inputBuses.isEmpty() || _layout.outputBuses.size() != 1 + TrackOuts)
			return false;
		if(_layout.outputBuses[0] != juce::AudioChannelSet::stereo())
			return false;
		for(int b = 1; b < _layout.outputBuses.size(); ++b)
		{
			const auto& set = _layout.outputBuses[b];
			if(set != juce::AudioChannelSet::mono() && !set.isDisabled())
				return false;
		}
		return true;
	}

	void Processor::prepareToPlay(const double _sampleRate, const int _maxBlock)
	{
		m_maxBlock = std::max(_maxBlock, 1);
		// The Tracks whose bus the host takes: the others are neither rendered nor resampled
		std::array<bool, Channels> used{};
		used[0] = used[1] = true;
		for(int b = 1; b <= TrackOuts; ++b)
		{
			const auto* bus = getBus(false, b);
			used[static_cast<size_t>(1 + b)] = bus && bus->isEnabled();
		}
		m_player.prepare(_sampleRate, m_maxBlock, used);
		m_bpm = 0;
		setLatencySamples(m_player.latency());
	}

	void Processor::takeEngine()
	{
		// Only once the message thread freed the last one handed back
		if(m_retired.load(std::memory_order_acquire))
			return;
		Engine* engine = m_pending.exchange(nullptr, std::memory_order_acq_rel);
		if(!engine)
			return;
		m_retired.store(m_active, std::memory_order_release);
		m_active = engine;
		m_player.setEngine(engine);

		// What the new module holds, which the parameters are synced against
		using namespace parameters;
		const auto& module = engine->module();
		for(int t = 0; t < Tracks; ++t)
		{
			auto& a = m_applied[static_cast<size_t>(t)];
			a.machine = std::max(machineIndex(module.machine(t)), 0);
			a.level = valueOf(module.level(t));
			for(size_t k = 0; k < Track.size(); ++k)
				a.values[k] = valueOf(module.parameter(t, Track[k].id));
		}
		for(size_t k = 0; k < Fx.size(); ++k)
			m_fxApplied[k] = valueOf(module.fxParameter(Fx[k].id));
		m_bpm = 0;
		// The parameters as they are, even a machine changed: they hold a state, or the kit taken from it
		syncParameters(true);
	}

	void Processor::syncParameters(const bool _hostIsTruth)
	{
		using namespace parameters;
		auto& module = m_active->module();
		for(int t = 0; t < Tracks; ++t)
		{
			auto& p = m_tracks[static_cast<size_t>(t)];
			auto& a = m_applied[static_cast<size_t>(t)];

			const int machine = p.machine->getIndex();
			if(machine != a.machine)
			{
				// The SYN page moves to the machine's defaults, as on the machine: the host reads them, unless they
				// come from a state, which holds its own
				module.setMachine(t, Machines[static_cast<size_t>(machine)]);
				a.machine = machine;
				for(size_t k = 0; k < Track.size(); ++k)
				{
					if(!synthPage(Track[k].id))
						continue;
					a.values[k] = valueOf(module.parameter(t, Track[k].id));
					if(!_hostIsTruth)
					{
						setQuietly(*p.values[k], static_cast<float>(a.values[k]));
						m_reread = true;
					}
				}
			}

			const int level = p.level->get();
			if(level != a.level)
			{
				module.setLevel(t, static_cast<uint16_t>(level << 8));
				a.level = level;
			}
			for(size_t k = 0; k < Track.size(); ++k)
			{
				const int v = p.values[k]->get();
				if(v == a.values[k])
					continue;
				module.setParameter(t, Track[k].id, m_active->trackWord(a.machine, Track[k].id, v));
				a.values[k] = v;
			}
		}
		for(size_t k = 0; k < Fx.size(); ++k)
		{
			const int v = m_fx[k]->get();
			if(v == m_fxApplied[k])
				continue;
			module.setFxParameter(Fx[k].id, m_active->fxWord(Fx[k].id, v));
			m_fxApplied[k] = v;
		}

		double bpm = m_tempo->get();
		if(auto* playHead = getPlayHead())
		{
			if(const auto position = playHead->getPosition())
			{
				if(const auto hostBpm = position->getBpm())
					bpm = *hostBpm;
			}
		}
		if(bpm != m_bpm)
		{
			module.setTempo(bpm);
			m_bpm = bpm;
		}
	}

	void Processor::processBlock(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midi)
	{
		juce::ScopedNoDenormals noDenormals;
		const int count = _buffer.getNumSamples();
		if(m_maxBlock == 0)
		{
			_buffer.clear();
			return;
		}

		takeEngine();
		if(m_active)
		{
			const uint32_t serial = m_stateSerial.load(std::memory_order_acquire);
			syncParameters(serial != m_stateSeen);
			m_stateSeen = serial;
		}

		// Main, then each Track whose bus the host takes (prepareToPlay told the player which)
		std::array<float*, Channels> outputs{};
		outputs[0] = _buffer.getWritePointer(getChannelIndexInProcessBlockBuffer(false, 0, 0));
		outputs[1] = _buffer.getWritePointer(getChannelIndexInProcessBlockBuffer(false, 0, 1));
		for(int b = 1; b <= TrackOuts; ++b)
		{
			const auto* bus = getBus(false, b);
			if(!bus || !bus->isEnabled())
				continue;
			const int channel = getChannelIndexInProcessBlockBuffer(false, b, 0);
			// A bus taken since prepareToPlay plays silence until the next one
			if(m_player.uses(1 + b))
				outputs[static_cast<size_t>(1 + b)] = _buffer.getWritePointer(channel);
			else
				_buffer.clear(channel, 0, count);
		}

		auto event = _midi.cbegin();
		for(int start = 0; start < count;)
		{
			const int length = std::min(count - start, m_maxBlock);
			const bool last = start + length == count;
			for(; event != _midi.cend() && (last || (*event).samplePosition < start + length); ++event)
			{
				// The bytes as they are: a MidiMessage would allocate for a long SysEx
				const auto& m = *event;
				if(m.numBytes < 3)
					continue;
				const int status = m.data[0] & 0xf0;
				const int track = m.data[1] - FirstNote;
				const int velocity = m.data[2];
				if((status != 0x90 && status != 0x80) || track < 0 || track >= Tracks)
					continue;
				const int offset = std::clamp(m.samplePosition - start, 0, length - 1);
				if(status == 0x90 && velocity > 0)
					m_player.noteOn(offset, track, m_tracks[static_cast<size_t>(track)].note->get(), velocity);
				else
					m_player.noteOff(offset, track);
			}

			std::array<float*, Channels> chunk{};
			for(size_t c = 0; c < Channels; ++c)
				chunk[c] = outputs[c] ? outputs[c] + start : nullptr;
			m_player.render(chunk.data(), length);
			start += length;
		}
	}

	void Processor::getStateInformation(juce::MemoryBlock& _data)
	{
		juce::XmlElement xml("STDrums");
		xml.setAttribute("version", 1);
		xml.setAttribute("kit", m_hasKit);
		for(auto* parameter : getParameters())
		{
			if(auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter))
				xml.setAttribute(p->getParameterID(), p->convertFrom0to1(p->getValue()));
		}
		copyXmlToBinary(xml, _data);
	}

	void Processor::setStateInformation(const void* _data, const int _size)
	{
		const auto xml = getXmlFromBinary(_data, _size);
		if(!xml || !xml->hasTagName("STDrums"))
			return;
		// No block meanwhile: the audio thread sees the state whole, then syncs to it as it is
		suspendProcessing(true);
		for(auto* parameter : getParameters())
		{
			auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter);
			if(p && xml->hasAttribute(p->getParameterID()))
				setQuietly(*p, static_cast<float>(xml->getDoubleAttribute(p->getParameterID())));
		}
		m_hasKit = xml->getBoolAttribute("kit");
		// A state saved before any firmware holds no kit: the firmware's, when one plays
		if(!m_hasKit && !m_kit.empty())
		{
			for(const auto& [parameter, value] : m_kit)
				setQuietly(*parameter, value);
			m_hasKit = true;
		}
		m_stateSerial.fetch_add(1, std::memory_order_acq_rel);
		suspendProcessing(false);
		updateHostDisplay(ChangeDetails().withProgramChanged(true));
	}

	juce::AudioProcessorEditor* Processor::createEditor()
	{
		return new Editor(*this);
	}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new stDrums::Processor();
}
