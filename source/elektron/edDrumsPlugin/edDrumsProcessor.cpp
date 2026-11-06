#include "edDrumsProcessor.h"

#include <algorithm>
#include <cstdlib>

#include "mdProtocol/mdmachines.h"

namespace elektron::drums
{
	namespace
	{
		constexpr int FirstNote = 36;	// notes 36-51: Tracks 1-16
		constexpr int DefaultLevel = 100, DefaultNote = 60, Centre = 64;

		std::vector<Machine> machineList()
		{
			std::vector<Machine> list{Machine{}};
			for(int id = 0; id < SyntaktMachines; ++id)
				list.push_back({Family::Syntakt, static_cast<uint8_t>(id)});
			for(const auto& m : md::machines::machines(md::MachineModel::Machinedrum))
			{
				const Machine machine{Family::Machinedrum, static_cast<uint8_t>(m.id)};
				if(m.id < 256 && playable(machine))
					list.push_back(machine);
			}
			return list;
		}

		juce::String machineName(const Machine& _machine)
		{
			if(_machine.family == Family::Off)
				return "OFF";
			if(_machine.family == Family::Syntakt)
				return syntaktMachineNames[_machine.id];
			return juce::String(std::string(md::machines::find(md::MachineModel::Machinedrum, _machine.id)->name));
		}

		// Tracks 1-9 start on the Syntakt's machines, which need nothing, the others on the Machinedrum's TRX
		int defaultMachine(const std::vector<Machine>& _list, const int _track)
		{
			const Machine machine = _track < SyntaktMachines ? Machine{Family::Syntakt, static_cast<uint8_t>(_track)}
				: Machine{Family::Machinedrum, static_cast<uint8_t>(16 + _track - SyntaktMachines)};
			return static_cast<int>(std::find(_list.begin(), _list.end(), machine) - _list.begin());
		}

		void setQuietly(juce::RangedAudioParameter& _parameter, const float _value)
		{
			static_cast<juce::AudioProcessorParameter&>(_parameter).setValue(_parameter.convertTo0to1(_value));
		}

		juce::AudioParameterInt* intParameter(const juce::String& _id, const juce::String& _name, const int _max,
			const int _default, const bool _automatable = true)
		{
			return new juce::AudioParameterInt({_id, 1}, _name, 0, _max, _default,
				juce::AudioParameterIntAttributes().withAutomatable(_automatable));
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
		// Nothing leaves the constructor: without a readable image the Machinedrum's sampled words are silence
		try
		{
			const auto image = Library::findFlashImage(dataFolder().getChildFile("roms").getFullPathName().toStdString());
			m_library = image.empty() ? std::make_unique<Library>() : std::make_unique<Library>(image);
		}
		catch(const std::exception&)
		{
			m_library = std::make_unique<Library>();
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
			addParameter(p.machine);
			m_machineSeen[static_cast<size_t>(t)] = machine;
			const auto machineDefaults = defaults(*m_library, m_machines[static_cast<size_t>(machine)]);
			for(int i = 0; i < 8; ++i)
			{
				p.values[static_cast<size_t>(i)] = intParameter(id + "m" + juce::String(i + 1),
					name + "Machine " + juce::String(i + 1), 127, machineDefaults[static_cast<size_t>(i)]);
				addParameter(p.values[static_cast<size_t>(i)]);
			}
			addParameter(p.note = intParameter(id + "note", name + "Note", 127, DefaultNote));
			addParameter(p.level = intParameter(id + "level", name + "Level", 127, DefaultLevel));
			addParameter(p.pan = intParameter(id + "pan", name + "Pan", 127, Centre));
			addParameter(p.output = intParameter(id + "output", name + "Output", Outputs - 1, 0));
			addParameter(p.chokes = intParameter(id + "chokes", name + "Chokes", 0xffff, 0, false));
		}
		// The host's tempo where its playhead gives one, else this
		m_tempo = new juce::AudioParameterFloat({"tempo", 1}, "Tempo", juce::NormalisableRange<float>(30.0f, 300.0f),
			120.0f);
		addParameter(m_tempo);
		startTimerHz(20);
	}

	Processor::~Processor()
	{
		stopTimer();
	}

	juce::File Processor::dataFolder()
	{
		// As the stack's: GEARMULATOR_DATA_ROOT in place of Documents, which tests set to a folder of their own
		const auto* root = std::getenv("GEARMULATOR_DATA_ROOT");
		const auto base = root && *root ? juce::File(juce::String::fromUTF8(root))
			: juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
		return base.getChildFile("c0remusic").getChildFile(ED_DRUMS_NAME);
	}

	void Processor::timerCallback()
	{
		if(m_reread.exchange(false))
			updateHostDisplay(ChangeDetails().withProgramChanged(true));
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
		m_engine = _sampleRate == SampleRate ? std::make_unique<Engine>(*m_library) : nullptr;
		setLatencySamples(m_engine ? Engine::HitDelay : 0);
	}

	void Processor::sync(const bool _hostIsTruth)
	{
		auto& e = *m_engine;
		for(int t = 0; t < Tracks; ++t)
		{
			const auto& p = m_tracks[static_cast<size_t>(t)];

			const int choice = p.machine->getIndex();
			e.setMachine(t, m_machines[static_cast<size_t>(choice)]);
			// Machine 1-8 move to a new machine's defaults, which the host reads, unless a state gave them
			if(choice != m_machineSeen[static_cast<size_t>(t)])
			{
				m_machineSeen[static_cast<size_t>(t)] = choice;
				if(!_hostIsTruth)
				{
					for(int i = 0; i < 8; ++i)
						setQuietly(*p.values[static_cast<size_t>(i)], static_cast<float>(e.parameter(t, i)));
					m_reread = true;
				}
			}
			for(int i = 0; i < 8; ++i)
			{
				const int v = p.values[static_cast<size_t>(i)]->get();
				if(v != e.parameter(t, i))
					e.setParameter(t, i, static_cast<uint8_t>(v));
			}
			e.setNote(t, p.note->get());
			e.setLevel(t, p.level->get());
			e.setPan(t, p.pan->get());
			e.setOutput(t, p.output->get());
			e.setChokes(t, static_cast<uint32_t>(p.chokes->get()));
		}

		double bpm = m_tempo->get();
		if(auto* playHead = getPlayHead())
			if(const auto position = playHead->getPosition())
				if(const auto hostBpm = position->getBpm())
					bpm = *hostBpm;
		e.setTempo(bpm);
	}

	void Processor::processBlock(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midi)
	{
		juce::ScopedNoDenormals noDenormals;
		if(!m_engine)
		{
			_buffer.clear();
			return;
		}
		const uint32_t serial = m_stateSerial.load(std::memory_order_acquire);
		sync(serial != m_stateSeen);
		m_stateSeen = serial;

		// A bus the host took has two channels, in the buses' order
		int channel = 0;
		for(int b = 0; b < Outputs; ++b)
		{
			const auto* bus = getBus(false, b);
			const bool on = bus && bus->isEnabled();
			for(size_t c = 0; c < 2; ++c)
				m_outputs[2 * static_cast<size_t>(b) + c] = on ? _buffer.getWritePointer(channel++) : nullptr;
		}

		const auto upTo = [&](const int _sample, int& _done)
		{
			if(_sample <= _done)
				return;
			m_engine->render(m_outputs.data(), _sample - _done);
			for(size_t c = 0; c < m_outputs.size(); ++c)
				if(m_outputs[c])
					m_outputs[c] += _sample - _done;
			_done = _sample;
		};

		const int count = _buffer.getNumSamples();
		int done = 0;
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
			upTo(std::clamp(metadata.samplePosition, 0, count), done);
			if(status == 0x90 && velocity > 0)
				m_engine->noteOn(track, velocity);
			else
				m_engine->noteOff(track);
		}
		upTo(count, done);
	}

	void Processor::getStateInformation(juce::MemoryBlock& _data)
	{
		juce::XmlElement xml("ElektronDrums");
		xml.setAttribute("version", 1);
		for(auto* parameter : getParameters())
			if(auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter))
				xml.setAttribute(p->getParameterID(), p->convertFrom0to1(p->getValue()));
		copyXmlToBinary(xml, _data);
	}

	void Processor::setStateInformation(const void* _data, const int _size)
	{
		const auto xml = getXmlFromBinary(_data, _size);
		if(!xml || !xml->hasTagName("ElektronDrums"))
			return;
		// No block meanwhile: the audio thread sees the state whole
		suspendProcessing(true);
		for(auto* parameter : getParameters())
		{
			auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter);
			if(p && xml->hasAttribute(p->getParameterID()))
				setQuietly(*p, static_cast<float>(xml->getDoubleAttribute(p->getParameterID())));
		}
		m_stateSerial.fetch_add(1, std::memory_order_acq_rel);
		suspendProcessing(false);
		updateHostDisplay(ChangeDetails().withProgramChanged(true));
	}

	juce::AudioProcessorEditor* Processor::createEditor()
	{
		return new juce::GenericAudioProcessorEditor(*this);
	}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new elektron::drums::Processor();
}
