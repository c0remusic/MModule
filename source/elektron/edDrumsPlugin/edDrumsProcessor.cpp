#include "edDrumsProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <variant>

#include "edDrumsEditor.h"
#include "edDrumsSampleFiles.h"
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
			// Last, where ROM-33 stood: ROM-33 to -48, the Sampler's entries now, were the list's last 16
			list.push_back(Sampler);
			return list;
		}

		juce::String machineName(const Machine& _machine)
		{
			if(_machine.family == Family::Off)
				return "OFF";
			if(_machine.family == Family::Syntakt)
				return syntaktMachineNames[_machine.id];
			if(_machine == Sampler)
				return "SAMPLER";
			return juce::String(std::string(md::machines::find(md::MachineModel::Machinedrum, _machine.id)->name));
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

		// ---- the state's patterns: only what differs from a new Pattern's ----

		bool isDefault(const Step& _s)
		{
			const Step d;
			return _s.trig == d.trig && _s.accent == d.accent && _s.slide == d.slide && _s.velocity == d.velocity
				&& _s.timing == d.timing && _s.ratchets == d.ratchets && _s.ratchetRate == d.ratchetRate
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
				|| _p.trackLength != blank.trackLength || _p.trackScale != blank.trackScale;
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
			xml->setAttribute("trackLengths", lengths.joinIntoString(" "));
			xml->setAttribute("trackScales", scales.joinIntoString(" "));
			return xml;
		}

		void readPattern(Pattern& _p, const juce::XmlElement& _xml)
		{
			_p.masterLength = std::clamp(_xml.getIntAttribute("length", 16), 1, SeqSteps);
			_p.swing = std::clamp(_xml.getIntAttribute("swing", 50), 50, 80);
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
		// Nothing leaves the constructor: without a readable image the Machinedrum's sampled words are the house's, or silence
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
			add(p.machine, Section::Machine, t);
			m_machineSeen[static_cast<size_t>(t)] = machine;
			const auto machineDefaults = defaults(m_machines[static_cast<size_t>(machine)]);
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
			add(p.output = intParameter(id + "output", name + "Output", Outputs - 1, 0), Section::Sends, t);
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
			m_delayFree.parameter = add(new juce::AudioParameterFloat({"delay_free", 1}, "Delay Free Hz", range, 0.0f),
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

		m_edited = std::make_unique<Sequencer>();
		for(auto& s : m_sequencers)
			s = std::make_unique<Sequencer>();
		for(auto& step : m_playingStep)
			step = -1;
		buildLockTargets();

		// A Kit holds every parameter but the Global ones
		const auto& global = ids(Section::Global);
		for(auto* parameter : getParameters())
		{
			const auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter);
			m_kitParameter.push_back(p && !global.contains(p->getParameterID()) ? 1 : 0);
		}
		m_kits = std::make_unique<KitBank>(dataFolder().getChildFile("Kits"));
		startTimerHz(20);
	}

	Processor::~Processor()
	{
		stopTimer();
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
			for(int i = 0; i < 8; ++i)
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
		if(_event.trig)
			m_engine->noteOn(t, _event.velocity, _event.accent);
	}

	void Processor::editSequencer(const std::function<void(Sequencer&)>& _edit)
	{
		jassert(juce::MessageManager::existsAndIsCurrentThread());
		_edit(*m_edited);
		publishSequencer();
	}

	// The triple buffer's writer: the spare takes the edits, then changes places with the middle, marked Fresh
	void Processor::publishSequencer()
	{
		m_sequencers[static_cast<size_t>(m_spare)]->copyEdits(*m_edited);
		m_spare = m_middle.exchange(m_spare | Fresh, std::memory_order_acq_rel) & 3;
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
		publishSample(_track);
		return true;
	}

	juce::String Processor::sampleName(const int _track) const
	{
		const auto& sample = m_samples[static_cast<size_t>(_track)];
		return sample ? juce::String::fromUTF8(sample->name.c_str()) : juce::String();
	}

	void Processor::clearSample(const int _track)
	{
		m_samples[static_cast<size_t>(_track)].reset();
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
		auto* sent = mail.get();
		auto& flight = m_sampleFlight[t];
		flight.push_back(std::move(mail));
		if(const auto* old = m_sampleMail[t].exchange(sent, std::memory_order_acq_rel))
			flight.erase(std::find_if(flight.begin(), flight.end(), [old](const auto& _m) { return _m.get() == old; }));
		pruneSamples();
	}

	void Processor::pruneSamples()
	{
		for(size_t t = 0; t < m_sampleFlight.size(); ++t)
		{
			const uint32_t done = m_sampleDone[t].load(std::memory_order_acquire);
			auto& flight = m_sampleFlight[t];
			flight.erase(std::remove_if(flight.begin(), flight.end(), [done](const auto& _m) { return _m->serial <= done; }),
				flight.end());
		}
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
		pruneSamples();
		if(++m_kitFollow >= 10)
			followKits();
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
				if(const auto& sample = m_samples[static_cast<size_t>(t)])
					m_engine->setSample(t, sample.get());
	}

	void Processor::sync(const bool _hostIsTruth)
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
					for(int i = 0; i < 8; ++i)
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
			for(int i = 0; i < 8; ++i)
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
				m_engine->setSample(static_cast<int>(t), mail->sample.pcm.empty() ? nullptr : &mail->sample);
				m_sampleDone[t].store(mail->serial, std::memory_order_release);
			}
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

		const int count = _buffer.getNumSamples();
		Transport transport;
		transport.playing = m_blockPlaying;
		transport.quarters = m_blockQuarters;
		transport.bpm = m_blockBpm;
		transport.sampleRate = SampleRate;
		transport.length = count;
		sequencer.process(transport, m_events);

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
		const auto eventsUpTo = [&](const int _sample)
		{
			for(; next < m_events.count && static_cast<int>(m_events.events[next].offset) <= _sample; ++next)
			{
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
			upTo(at, done);
			if(status == 0x90 && velocity > 0)
			{
				releaseLocks(track);
				m_engine->noteOn(track, velocity);
			}
			else
			{
				m_engine->noteOff(track);
			}
		}
		eventsUpTo(count);
		upTo(count, done);
		m_now += count;

		// What the editor shows: the peaks fall 20 dB a second, 10^-1 of the gain
		const float fall = static_cast<float>(std::pow(10.0, -count / SampleRate));
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
		for(int t = 0; t < Tracks; ++t)
			m_playingStep[static_cast<size_t>(t)].store(sequencer.stepAt(t, m_blockQuarters), std::memory_order_relaxed);
		m_currentPattern.store(sequencer.currentPattern(), std::memory_order_relaxed);
		m_liveTracks.store(m_engine->liveTracks(), std::memory_order_relaxed);
	}

	// The parameters by ID, all or a Kit's, and the Tracks' samples, in elements of their own
	void Processor::writeSound(juce::XmlElement& _xml, const bool _all) const
	{
		for(auto* parameter : getParameters())
			if(auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter))
				if(_all || isKitParameter(p->getParameterIndex()))
					_xml.setAttribute(p->getParameterID(), p->convertFrom0to1(p->getValue()));
		for(int t = 0; t < Tracks; ++t)
			if(const auto& sample = m_samples[static_cast<size_t>(t)])
			{
				const auto bytes = sampleToBytes(*sample);
				auto* element = _xml.createNewChildElement("sample");
				element->setAttribute("track", t);
				element->addTextElement(juce::MemoryBlock(bytes.data(), bytes.size()).toBase64Encoding());
			}
	}

	void Processor::getStateInformation(juce::MemoryBlock& _data)
	{
		juce::XmlElement xml("ElektronDrums");
		// 2: ticket 18's parameters; the format is the same, every parameter by ID. 3: the Sampler last of Machine's
		// choices, the Tracks' samples and the patterns, in elements of their own. The Kit played's Slot since ticket 19
		xml.setAttribute("version", 3);
		writeSound(xml, true);
		xml.setAttribute("kit", m_kitSlot);
		for(int i = 0; i < SeqPatterns; ++i)
			if(auto pattern = patternXml(m_edited->pattern(i), i))
				xml.addChildElement(pattern.release());
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
		// Before version 3 ROM-33 to -48 were Machine's last 16 choices, past the Sampler's: a Track on one plays the Sampler
		for(auto& p : m_tracks)
			if(_xml.getIntAttribute(p.machine->getParameterID()) > samplerChoice())
				setQuietly(*p.machine, static_cast<float>(samplerChoice()));

		// The samples the state holds, none else
		std::array<const juce::XmlElement*, Tracks> samples{};
		for(const auto* element : _xml.getChildWithTagNameIterator("sample"))
			if(const int t = element->getIntAttribute("track", -1); t >= 0 && t < Tracks)
				samples[static_cast<size_t>(t)] = element;
		for(int t = 0; t < Tracks; ++t)
		{
			auto& sample = m_samples[static_cast<size_t>(t)];
			sample.reset();
			juce::MemoryBlock bytes;
			if(samples[static_cast<size_t>(t)] && bytes.fromBase64Encoding(samples[static_cast<size_t>(t)]->getAllSubText()))
				if(auto read = sampleFromBytes(static_cast<const uint8_t*>(bytes.getData()), bytes.getSize()))
					sample = std::make_unique<Sample>(std::move(*read));
			publishSample(t);
		}
	}

	void Processor::setStateInformation(const void* _data, const int _size)
	{
		jassert(juce::MessageManager::existsAndIsCurrentThread());
		const auto xml = getXmlFromBinary(_data, _size);
		if(!xml || !xml->hasTagName("ElektronDrums"))
			return;
		// No block meanwhile: the audio thread sees the state whole
		suspendProcessing(true);
		readSound(*xml, true);
		for(int i = 0; i < SeqPatterns; ++i)
			m_edited->pattern(i) = Pattern();
		for(const auto* element : xml->getChildWithTagNameIterator("pattern"))
			if(const int i = element->getIntAttribute("index", -1); i >= 0 && i < SeqPatterns)
				readPattern(m_edited->pattern(i), *element);
		publishSequencer();

		m_stateSerial.fetch_add(1, std::memory_order_acq_rel);
		suspendProcessing(false);
		updateHostDisplay(ChangeDetails().withProgramChanged(true));

		// The Kit played, compared from now on with what its Slot holds now
		const int slot = xml->getIntAttribute("kit", -1);
		m_kitSlot = KitBank::valid(slot) ? slot : -1;
		m_kits->follow();
		const auto kit = m_kitSlot < 0 ? nullptr : m_kits->read(m_kitSlot);
		rememberKit(kit.get());
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
		if(!_kit)
			return;
		for(auto* parameter : getParameters())
		{
			const auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter);
			if(!p || !isKitParameter(p->getParameterIndex()))
				continue;
			const auto& id = p->getParameterID();
			m_kitValues[static_cast<size_t>(p->getParameterIndex())] = _kit->hasAttribute(id)
				? p->convertTo0to1(static_cast<float>(_kit->getDoubleAttribute(id))) : p->getDefaultValue();
		}
		for(const auto* element : _kit->getChildWithTagNameIterator("sample"))
		{
			const int t = element->getIntAttribute("track", -1);
			juce::MemoryBlock bytes;
			if(t >= 0 && t < Tracks && bytes.fromBase64Encoding(element->getAllSubText()))
				if(auto read = sampleFromBytes(static_cast<const uint8_t*>(bytes.getData()), bytes.getSize()))
					m_kitSamples[static_cast<size_t>(t)] = std::make_unique<Sample>(std::move(*read));
		}
	}

	void Processor::followKits()
	{
		m_kitFollow = 0;
		const uint64_t changed = m_kits->follow();
		if(m_kitSlot >= 0 && (changed >> m_kitSlot) & 1)
		{
			const auto kit = m_kits->read(m_kitSlot);
			rememberKit(kit.get());
		}
	}

	bool Processor::loadKit(const int _slot)
	{
		jassert(juce::MessageManager::existsAndIsCurrentThread());
		followKits();
		const auto kit = m_kits->read(_slot);
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
		for(size_t t = 0; t < m_samples.size(); ++t)
		{
			const auto* a = m_samples[t].get();
			const auto* b = m_kitSamples[t].get();
			if(!a != !b || (a && (a->name != b->name || a->rate != b->rate || a->pcm != b->pcm)))
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
