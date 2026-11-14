// The instrument as a host loads it: the built VST3 through JUCE's VST3 hosting, with a data folder of its own
// (GEARMULATOR_DATA_ROOT), against the engine driven directly with the same calls:
//
//   - the class, an instrument by c0remusic; Main and 15 stereo Outs; the 1523 parameters (Machine 9-24 last) and their VST3 IDs, Machine and
//     Chokes the ones a host does not automate;
//   - at 44.1 kHz the latency reported is HitDelay; every channel is the engine's, sample for sample, each Track on its
//     Out, in blocks of 128, of 32, of random sizes, offline, Main alone as with all the Outs;
//   - a Hit sounds HitDelay after its note in the plug-in as in the engine;
//   - a machine set by the host gives Machine 1-8 the machine's defaults, which the host reads again;
//   - the state through IComponent: another instance gets every parameter back, and plays the same;
//   - at 48 kHz, which ticket 16 settles, silence and no latency;
//   - the editor opens; no block of 128 over 10 ms on a busy pattern;
//   - the 64 relays for Push (ticket 18): nothing told as the instrument loads; each relay set by the host sets its
//     parameter on the Track shown and nothing is told; the Track relay shows Track 2, the relays reaching the host
//     as its values, edits without a gesture; Track 1 shown again, they read back what they set, which the state
//     gives another instance.
//
// GEARMULATOR_MD_FIRMWARE_BIN names the flash image; exit 77 without it.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "edDrumsEngine.h"
#include "edDrumsParameters.h"

namespace
{
	using namespace elektron::drums;
	using Clock = std::chrono::steady_clock;

	constexpr int g_block = 128;
	constexpr int g_channels = 2 * Outputs;
	constexpr int g_perTrack = 14;	// Machine, Machine 1-8, Note, Level, Pan, Output, Chokes
	// And Tempo, then ticket 18's: 61 more a Track (chain, LFOs, sends, accent), 65 shared, then Pattern
	// (edDrumsParameterIds.txt)
	constexpr int g_pages = Tracks * g_perTrack + 1 + Tracks * 61 + 65 + 1;	// up to Pattern; then each Track's Machine 9-24
	constexpr int g_machine24 = g_pages + Tracks * 16;	// up to T16 Machine 24; then tickets 14-15's sync and S&H
	constexpr int g_parameters = g_machine24 + 7;
	// After the relays: ticket 14's chorus type
	constexpr int g_appended = 1;
	constexpr double g_stallMs = 10.0;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	void pump(const int _milliseconds)
	{
		juce::MessageManager::getInstance()->runDispatchLoopUntil(_milliseconds);
	}

	// A data folder of this run's, never the user's
	class DataRoot
	{
	public:
		DataRoot()
			: m_path(std::filesystem::temp_directory_path()
				/ ("EdDrumsVst3HostTest_" + std::to_string(Clock::now().time_since_epoch().count())))
		{
			std::filesystem::create_directories(m_path);
			_putenv_s("GEARMULATOR_DATA_ROOT", m_path.string().c_str());
		}
		~DataRoot()
		{
			_putenv_s("GEARMULATOR_DATA_ROOT", "");
			std::error_code error;
			std::filesystem::remove_all(m_path, error);
		}

	private:
		std::filesystem::path m_path;
	};

	struct Note
	{
		int64_t sample;
		int track, velocity;
	};

	struct Told final : juce::AudioProcessorListener
	{
		std::atomic<int> rereads{0}, edits{0}, gestures{0};
		void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { ++edits; }
		void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& _details) override
		{
			if(_details.programChanged)
				++rereads;
		}
		void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++gestures; }
		void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override {}
	};

	struct Instance
	{
		Told told;
		std::unique_ptr<juce::AudioPluginInstance> plugin;

		~Instance()
		{
			if(plugin)
				plugin->removeListener(&told);
		}

		juce::AudioProcessorParameter& parameter(const int _track, const int _index) const
		{
			return *plugin->getParameters()[_track * g_perTrack + _index];
		}

		void setInt(const int _track, const int _index, const int _value, const int _max) const
		{
			parameter(_track, _index).setValueNotifyingHost(static_cast<float>(_value) / static_cast<float>(_max));
		}

		void prepare(const double _rate, const bool _outs) const
		{
			plugin->releaseResources();
			auto layout = plugin->getBusesLayout();
			for(int bus = 1; bus < layout.outputBuses.size(); ++bus)
				layout.outputBuses.getReference(bus) = _outs ? juce::AudioChannelSet::stereo()
					: juce::AudioChannelSet::disabled();
			require(plugin->setBusesLayout(layout), "the host cannot take the buses it asks for");
			plugin->prepareToPlay(_rate, g_block);
		}
	};

	std::unique_ptr<Instance> load(juce::AudioPluginFormatManager& _formats, const juce::PluginDescription& _description,
		const double _rate)
	{
		auto instance = std::make_unique<Instance>();
		juce::String error;
		instance->plugin = _formats.createPluginInstance(_description, _rate, g_block, error);
		require(instance->plugin != nullptr, "the host cannot create the instrument: " + error.toStdString());
		instance->plugin->addListener(&instance->told);
		instance->prepare(_rate, true);
		pump(100);
		return instance;
	}

	// As a host's audio callback, in blocks of _size or, with a _random seed, of random sizes up to g_block
	juce::AudioBuffer<float> render(juce::AudioProcessor& _plugin, const std::vector<Note>& _notes, const int64_t _samples,
		const int _size, uint32_t _random = 0, std::vector<double>* _blockMs = nullptr)
	{
		const auto channels = _plugin.getTotalNumOutputChannels();
		juce::AudioBuffer<float> take(channels, static_cast<int>(_samples));
		juce::AudioBuffer<float> buffer(channels, g_block);
		juce::MidiBuffer midi;
		size_t next = 0;
		for(int64_t position = 0; position < _samples;)
		{
			int size = _size;
			if(_random)
			{
				_random ^= _random << 13;
				_random ^= _random >> 17;
				_random ^= _random << 5;
				size = 1 + static_cast<int>(_random % g_block);
			}
			size = static_cast<int>(std::min<int64_t>(size, _samples - position));
			buffer.setSize(channels, size, false, false, true);
			buffer.clear();
			midi.clear();
			for(; next < _notes.size() && _notes[next].sample < position + size; ++next)
				midi.addEvent(juce::MidiMessage::noteOn(1 + _notes[next].track % 4, 36 + _notes[next].track,
					static_cast<juce::uint8>(_notes[next].velocity)), static_cast<int>(_notes[next].sample - position));
			const auto start = Clock::now();
			_plugin.processBlock(buffer, midi);
			if(_blockMs)
				_blockMs->push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
			for(int c = 0; c < channels; ++c)
				take.copyFrom(c, static_cast<int>(position), buffer, c, 0, size);
			position += size;
		}
		return take;
	}

	std::vector<Note> pattern(const int64_t _samples, const uint32_t _seed)
	{
		std::mt19937 random(_seed);
		std::vector<Note> notes;
		for(int64_t at = 100; at < _samples - 3000; at += 1 + random() % 1500)
			notes.push_back({at, static_cast<int>(random() % Tracks), 1 + static_cast<int>(random() % 127)});
		return notes;
	}

	// Not the defaults: a sound of its own on each Track, as the host sets it and as the engine takes it
	struct Sound
	{
		int level, pan, note, chokes;
		std::array<uint8_t, 8> values;
	};

	Sound soundOf(const int _track)
	{
		Sound s{30 + 6 * _track, _track * 8, 40 + 2 * _track, _track == 2 ? 1 << 3 : 0, {}};
		for(size_t i = 0; i < s.values.size(); ++i)
			s.values[i] = static_cast<uint8_t>((13 * _track + 7 * i + 20) % 128);
		s.values[6] = 0;	// a Syntakt machine's HOLD off
		return s;
	}

	std::vector<std::vector<float>> direct(const Library& _library, const std::vector<Note>& _notes, const int _samples,
		const std::vector<Machine>& _kit, const bool _sounds)
	{
		auto e = std::make_unique<Engine>(_library);
		std::vector<std::vector<float>> out(g_channels, std::vector<float>(static_cast<size_t>(_samples)));
		std::array<float*, g_channels> p{};
		for(int c = 0; c < g_channels; ++c)
			p[static_cast<size_t>(c)] = out[static_cast<size_t>(c)].data();
		for(int t = 0; t < Tracks; ++t)
		{
			const auto sound = soundOf(t);
			e->setMachine(t, _kit[static_cast<size_t>(t)]);
			e->setOutput(t, t % Outputs);
			e->setLevel(t, _sounds ? sound.level : 100);
			e->setPan(t, _sounds ? sound.pan : 64);
			if(!_sounds)
				continue;
			e->setNote(t, sound.note);
			e->setChokes(t, static_cast<uint32_t>(sound.chokes));
			for(int i = 0; i < 8; ++i)
				e->setParameter(t, i, sound.values[static_cast<size_t>(i)]);
		}
		int done = 0;
		const auto upTo = [&](const int _sample)
		{
			if(_sample <= done)
				return;
			e->render(p.data(), _sample - done);
			for(auto*& q : p)
				q += _sample - done;
			done = _sample;
		};
		for(const auto& n : _notes)
		{
			upTo(static_cast<int>(n.sample));
			e->noteOn(n.track, n.velocity);
		}
		upTo(_samples);
		return out;
	}

	// The plug-in against the engine as this program links it: /fp:fast and link-time code generation round a product
	// differently in the two, a few units in the last place of a float
	void compare(const char* _what, const juce::AudioBuffer<float>& _take, const std::vector<std::vector<float>>& _expected,
		const float _tolerance)
	{
		float worst = 0;
		for(int c = 0; c < _take.getNumChannels(); ++c)
			for(int n = 0; n < _take.getNumSamples(); ++n)
			{
				const float difference = std::abs(_take.getSample(c, n) - _expected[static_cast<size_t>(c)][static_cast<size_t>(n)]);
				worst = std::max(worst, difference);
				if(difference > _tolerance)
					throw std::runtime_error(std::string(_what) + ": channel " + std::to_string(c) + " differs at sample "
						+ std::to_string(n) + " by " + std::to_string(difference));
			}
		std::printf("%s: %d channels of %d samples, worst difference %.2g\n", _what, _take.getNumChannels(),
			_take.getNumSamples(), worst);
	}

	std::vector<std::vector<float>> copy(const juce::AudioBuffer<float>& _take)
	{
		std::vector<std::vector<float>> out;
		for(int c = 0; c < _take.getNumChannels(); ++c)
			out.emplace_back(_take.getReadPointer(c), _take.getReadPointer(c) + _take.getNumSamples());
		return out;
	}

	juce::File bundleOf(const juce::File& _file)
	{
		for(auto file = _file; file.exists() && file != file.getParentDirectory(); file = file.getParentDirectory())
			if(file.isDirectory() && file.hasFileExtension("vst3"))
				return file;
		return _file;
	}

	void openEditor(juce::AudioPluginInstance& _plugin)
	{
		std::unique_ptr<juce::AudioProcessorEditor> editor(_plugin.createEditorIfNeeded());
		require(editor != nullptr, "the host cannot open the editor");
		juce::Component window;
		window.addAndMakeVisible(*editor);
		window.setSize(editor->getWidth(), editor->getHeight());
		window.addToDesktop(juce::ComponentPeer::windowIsTemporary);
		window.setVisible(true);
		pump(300);
		require(editor->getWidth() > 0 && editor->getHeight() > 0, "the editor has no size");
		window.removeChildComponent(editor.get());
		editor.reset();
		pump(100);
	}

	// The relays for Push as a host sees them, on a fresh instance. A Track parameter's VST3 value the host caches is
	// stale once a relay sets it silently: the host reads it back through the relays, Track 1 shown again, and through
	// the state, which another instance gets
	void relays(juce::AudioPluginFormatManager& _formats, const juce::PluginDescription& _description)
	{
		std::ifstream file(ED_DRUMS_PARAMETER_IDS);
		require(file.good(), std::string("no ") + ED_DRUMS_PARAMETER_IDS);
		std::vector<std::string> ids;
		for(std::string line; std::getline(file, line);)
			if(!line.empty())
				ids.push_back(juce::String(line).trim().toStdString());
		const auto indexOf = [&ids](const std::string& _id)
		{
			const auto at = std::find(ids.begin(), ids.end(), _id);
			require(at != ids.end(), "no parameter " + _id);
			return static_cast<int>(at - ids.begin());
		};

		auto instance = load(_formats, _description, SampleRate);
		auto& plugin = *instance->plugin;
		const auto& hosted = plugin.getParameters();
		render(plugin, {}, g_block, g_block);
		pump(300);
		require(instance->told.edits == 0 && instance->told.gestures == 0, "the host told of edits as the instrument loads");

		struct Aim
		{
			juce::AudioProcessorParameter *relay, *own, *other;	// the relay, its target on Track 1 and on Track 2
			int own_, other_;
			float set;
		};
		std::vector<Aim> aims;
		juce::AudioProcessorParameter* track = nullptr;
		for(const auto& spec : elektron::drums::parameters::relaySpecs)
		{
			auto* relay = hosted[indexOf(std::string("push_") + (spec.target ? spec.target : "track"))];
			require(relay->getName(64) == spec.name && relay->isAutomatable(), std::string(spec.name) + ": not the relay");
			if(!spec.target)
			{
				track = relay;
				continue;
			}
			const int own = indexOf(std::string("t01_") + spec.target), other = indexOf(std::string("t02_") + spec.target);
			require(std::abs(relay->getValue() - hosted[own]->getValue()) <= 1e-6f,
				std::string(spec.name) + " at load is not Track 1's");
			// An end of its range away from Track 2's value
			aims.push_back({relay, hosted[own], hosted[other], own, other,
				hosted[other]->getValue() < 0.5f ? 1.0f : 0.0f});
		}

		// Each relay set as the host sets it: Track 1's parameter at the next block, nothing told
		for(const auto& a : aims)
			a.relay->setValueNotifyingHost(a.set);
		instance->told.edits = 0;
		render(plugin, {}, g_block, g_block);
		pump(300);
		require(instance->told.edits == 0, "a relay's parameter told to the host");

		// The Track relay on Track 2: the relays reach the host with its values, edits without a gesture
		const auto show = [&](const int _track)
		{
			track->setValueNotifyingHost(static_cast<float>(_track) / (Tracks - 1));
			instance->told.edits = 0;
			instance->told.gestures = 0;
			render(plugin, {}, g_block, g_block);
			pump(300);
		};
		show(1);
		for(const auto& a : aims)
			require(a.relay->getValue() == a.other->getValue(), a.relay->getName(64).toStdString() + " on Track 2");
		std::printf("the relays: nothing told at load; Track 2 shown, %d edits and %d gestures told\n",
			instance->told.edits.load(), instance->told.gestures.load());
		require(instance->told.edits == static_cast<int>(aims.size()) && instance->told.gestures == 0,
			"Track 2 shown: not the relays as edits without a gesture");

		// Track 1 again: each relay reads back what it set
		show(0);
		for(const auto& a : aims)
			require(a.relay->getValue() == a.set, a.relay->getName(64).toStdString() + ": Track 1's parameter not set");
		require(instance->told.edits == static_cast<int>(aims.size()), "Track 1 shown: not the relays as edits");

		// The state: another instance holds what the relays set on Track 1, Track 2 as it was
		juce::MemoryBlock state;
		plugin.getStateInformation(state);
		auto other = load(_formats, _description, SampleRate);
		other->plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
		pump(100);
		const auto& restored = other->plugin->getParameters();
		for(const auto& a : aims)
			require(std::abs(restored[a.own_]->getValue() - a.set) < 1e-6f
				&& std::abs(restored[a.other_]->getValue() - a.other->getValue()) < 1e-6f,
				a.relay->getName(64).toStdString() + ": its parameter not in the state");
		std::printf("the %d relays set their Track 1 parameters, read back through the relays and the state\n",
			static_cast<int>(aims.size()) + 1);
	}

	void run(const juce::File& _bundle, const Library& _library)
	{
		juce::AudioPluginFormatManager formats;
		formats.addFormat(new juce::VST3PluginFormat());
		juce::OwnedArray<juce::PluginDescription> found;
		formats.getFormat(0)->findAllTypesForFile(found, _bundle.getFullPathName());
		require(found.size() == 1, "the VST3 does not hold one plug-in");
		const auto& description = *found[0];
		std::printf("%s by %s %s, %s, %s, class hash %u\n", description.name.toRawUTF8(),
			description.manufacturerName.toRawUTF8(), description.version.toRawUTF8(), description.category.toRawUTF8(),
			description.isInstrument ? "instrument" : "effect", static_cast<uint32_t>(description.uniqueId));
		require(description.name == ED_DRUMS_NAME && description.manufacturerName == "c0remusic"
			&& description.isInstrument, "the VST3 is not the instrument");
		// Ticket 27: named Lockstep, the class of before, whose ID JUCE makes from the manufacturer's and the plug-in's
		// codes alone (JUCE_VST3_CAN_REPLACE_VST2=0): the sets saved as Elektron Drums Dev find it. The hash of the class
		// ID that JUCE's VST3 hosting gives, read on 2026-11-11 from the bundle built as Elektron Drums Dev
		constexpr uint32_t ClassHash = 3065139503u;
		require(description.name == "Lockstep" && static_cast<uint32_t>(description.uniqueId) == ClassHash,
			"not Lockstep of the same class: " + description.name.toStdString() + ", "
			+ std::to_string(static_cast<uint32_t>(description.uniqueId)));

		constexpr int Samples = 150000;
		const auto notes = pattern(Samples, 1);

		std::vector<Machine> kit;
		for(int t = 0; t < SyntaktMachines; ++t)
			kit.push_back({Family::Syntakt, static_cast<uint8_t>(t)});
		kit[SyntaktTwinshot] = {Family::Machinedrum, 26};
		for(int t = SyntaktMachines; t < Tracks; ++t)
			kit.push_back({Family::Machinedrum, static_cast<uint8_t>(16 + t - SyntaktMachines)});

		auto instance = load(formats, description, SampleRate);
		auto& plugin = *instance->plugin;
		require(plugin.getBusCount(true) == 0 && plugin.getBusCount(false) == Outputs, "not Main and 15 Outs");
		require(plugin.getBus(false, 0)->getName() == "Main", "no Main");
		for(int bus = 1; bus < Outputs; ++bus)
			require(plugin.getBus(false, bus)->getName() == juce::String::formatted("Out %02d", bus)
				&& plugin.getChannelCountOfBus(false, bus) == 2, "Out " + std::to_string(bus) + " is not stereo");
		require(plugin.getTotalNumOutputChannels() == g_channels, "not 32 channels");
		require(plugin.getLatencySamples() == Engine::HitDelay, "the latency reported is not HitDelay");

		// JUCE's VST3 hosting lists the host's MIDI CC parameters too, after ours; none is automatable
		const auto& parameters = plugin.getParameters();
		int automatable = 0;
		for(int p = 0; p < parameters.size(); ++p)
			automatable += parameters[p]->isAutomatable();
		for(int t = 0; t < Tracks; ++t)
		{
			const auto prefix = juce::String::formatted("T%02d ", t + 1);
			require(instance->parameter(t, 0).getName(64) == prefix + "Machine"
				&& !instance->parameter(t, 0).isAutomatable(), "Machine's place");
			require(instance->parameter(t, 8).getName(64) == prefix + "Machine 8", "Machine 8's place");
			require(instance->parameter(t, 13).getName(64) == prefix + "Chokes"
				&& !instance->parameter(t, 13).isAutomatable(), "Chokes' place");
		}
		require(parameters[Tracks * g_perTrack]->getName(64) == "Tempo", "Tempo's place");
		require(parameters[Tracks * g_perTrack + 1]->getName(64) == "T01 Drive 1 Type", "ticket 18's first place");
		require(parameters[g_pages - 2]->getName(64) == "Oversampling", "ticket 18's last place");
		require(parameters[g_pages - 1]->getName(64) == "Pattern", "Pattern's place");
		require(parameters[g_pages]->getName(64) == "T01 Machine 9" && parameters[g_machine24 - 1]->getName(64) == "T16 Machine 24"
			&& parameters[g_parameters - 1]->getName(64) == "Rack S&H Rate",
			"Machine 9-24's places");
		const int chorusType = g_parameters + elektron::drums::parameters::RelayCount;
		require(parameters[chorusType]->getName(64) == "Chorus Type" && parameters[chorusType]->isAutomatable()
			&& parameters[chorusType]->getText(0.0f, 64) == "Classic"
			&& parameters[chorusType]->getText(1.0f, 64) == "Bucket Brigade",
			"Chorus Type's place after the relays, or its choices");
		std::printf("%d parameters of ours, %d relays and %d after them, %d automatable with Bypass; latency %d samples\n",
			g_parameters, elektron::drums::parameters::RelayCount, g_appended, automatable, plugin.getLatencySamples());
		require(automatable == g_parameters + elektron::drums::parameters::RelayCount + g_appended - 2 * Tracks + 1,
			"Machine and Chokes are not for the host to automate");

		// Each Track on an Out of its own
		for(int t = 0; t < Tracks; ++t)
			instance->setInt(t, 12, t, Outputs - 1);
		const auto expected = direct(_library, notes, Samples, kit, false);
		std::vector<double> ms;
		const auto take = render(plugin, notes, Samples, g_block, 0, &ms);
		compare("the plug-in against the engine", take, expected, 1e-6f);
		const auto whole = copy(take);
		instance->prepare(SampleRate, true);
		compare("blocks of 32", render(plugin, notes, Samples, 32), whole, 0);
		instance->prepare(SampleRate, true);
		compare("random blocks", render(plugin, notes, Samples, 0, 0x12345), whole, 0);
		// Offline, as a host renders a set to a file (spec 12, criterion 10)
		plugin.setNonRealtime(true);
		instance->prepare(SampleRate, true);
		compare("offline, random blocks", render(plugin, notes, Samples, 0, 0x6789), whole, 0);
		plugin.setNonRealtime(false);
		instance->prepare(SampleRate, false);
		{
			auto main = render(plugin, notes, Samples, g_block);
			main.setSize(2, Samples, true);
			compare("Main alone", main, whole, 0);
		}
		// One Out among disabled ones: its channels follow Main's in the buffer
		{
			constexpr int Bus = 3;
			plugin.releaseResources();
			auto layout = plugin.getBusesLayout();
			layout.outputBuses.getReference(Bus) = juce::AudioChannelSet::stereo();
			require(plugin.setBusesLayout(layout), "the host cannot take one Out");
			plugin.prepareToPlay(SampleRate, g_block);
			const auto take = render(plugin, notes, Samples, g_block);
			require(take.getNumChannels() == 4, "Main and one Out are four channels");
			for(int c = 0; c < 2; ++c)
				for(int n = 0; n < Samples; ++n)
					require(take.getSample(2 + c, n) == whole[static_cast<size_t>(2 * Bus + c)][static_cast<size_t>(n)],
						"Out 03 among disabled buses");
			std::printf("Out %02d alone with Main: its channels the same\n", Bus);
		}
		std::sort(ms.begin(), ms.end());
		std::printf("a block of 128: median %.3f ms, worst %.3f ms\n", ms[ms.size() / 2], ms.back());
		require(ms.back() < g_stallMs, "a block stalled the audio thread");

		// Each Track on its Out
		instance->prepare(SampleRate, true);
		{
			std::vector<Note> each;
			for(int t = 0; t < Tracks; ++t)
				each.push_back({2000 + 3000 * t, t, 127});
			const auto take = render(plugin, each, 2000 + 3000 * 16 + 3000, g_block);
			for(int o = 0; o < Outputs; ++o)
				require(take.getMagnitude(2 * o, 0, take.getNumSamples()) > 1e-4f, "Out " + std::to_string(o) + " is silent");
		}

		// A sound that is not the defaults, against the engine, then through the state into another instance
		{
			for(int t = 0; t < Tracks; ++t)
			{
				const auto sound = soundOf(t);
				for(int i = 0; i < 8; ++i)
					instance->setInt(t, 1 + i, sound.values[static_cast<size_t>(i)], 127);
				instance->setInt(t, 9, sound.note, 127);
				instance->setInt(t, 10, sound.level, 127);
				instance->setInt(t, 11, sound.pan, 127);
				instance->setInt(t, 13, sound.chokes, 0xffff);
			}
			instance->prepare(SampleRate, true);
			const auto sounds = render(plugin, notes, Samples, g_block);
			compare("a sound of its own on each Track", sounds, direct(_library, notes, Samples, kit, true), 1e-6f);

			// The chorus's type, after the relays, kept too: no Track sends to the chorus here, so the sound stays
			const int chorusType = g_parameters + elektron::drums::parameters::RelayCount;
			parameters[chorusType]->setValueNotifyingHost(1.0f);
			juce::MemoryBlock state;
			plugin.getStateInformation(state);
			auto other = load(formats, description, SampleRate);
			other->plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			pump(100);
			for(int p = 0; p < g_parameters; ++p)
				require(std::abs(other->plugin->getParameters()[p]->getValue() - parameters[p]->getValue()) < 1e-6f,
					"parameter " + std::to_string(p) + " of the state");
			require(other->plugin->getParameters()[chorusType]->getValue() == 1.0f, "the chorus's type of the state");
			compare("the state in another instance", render(*other->plugin, notes, Samples, g_block), copy(sounds), 0);
		}

		// A Hit HitDelay after its note, GND-IM's impulse on Track 16
		{
			auto choice = dynamic_cast<juce::HostedAudioProcessorParameter*>(&instance->parameter(15, 0));
			require(choice != nullptr, "Machine");
			const int steps = choice->getNumSteps();
			std::printf("Machine has %d choices\n", steps);
			// Off, nine Syntakt machines, GND---, GND-SN, GND-NS, GND-IM
			instance->parameter(15, 0).setValueNotifyingHost(13.0f / static_cast<float>(steps - 1));
			pump(100);
			const auto rereads = instance->told.rereads.load();
			for(const int note : {3000, 3013, 3031})
			{
				instance->prepare(SampleRate, true);
				const auto take = render(plugin, {{note, 15, 127}}, 8000, 64);
				int first = 0;
				const auto* main = take.getReadPointer(2 * 15);
				while(first < 8000 && main[first] == 0)
					++first;
				require(first == note + Engine::HitDelay, "a Hit at " + std::to_string(first - note) + " samples");
			}
			pump(200);
			std::printf("GND-IM on Track 16: its Hit HitDelay after its note wherever it falls; the host told %d times\n",
				instance->told.rereads.load() - rereads);
			require(instance->told.rereads.load() > 0, "the host was not told of the machine's defaults");
		}

		openEditor(plugin);
		relays(formats, description);

		// Ticket 16's rates: sound, the latency hitDelay's; a rate it does not play: silence, no latency
		{
			auto other = load(formats, description, 48000.0);
			const auto take = render(*other->plugin, notes, 20000, g_block);
			require(take.getMagnitude(0, take.getNumSamples()) > 0
				&& other->plugin->getLatencySamples() == Engine::hitDelay(48000.0), "at 48 kHz");
			auto unplayable = load(formats, description, 22050.0);
			const auto none = render(*unplayable->plugin, notes, 20000, g_block);
			require(none.getMagnitude(0, none.getNumSamples()) == 0 && unplayable->plugin->getLatencySamples() == 0,
				"at 22.05 kHz");
			std::printf("at 48 kHz: sound, latency %d samples; at 22.05 kHz: silence, latency 0\n",
				other->plugin->getLatencySamples());
		}
	}
}

int main(const int _argc, char* _argv[])
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	if(_argc < 2)
	{
		std::cerr << "usage: edDrumsVst3HostTest <the VST3>\n";
		return 2;
	}
	const auto image = Library::findFlashImage({});
	if(image.empty())
	{
		std::cout << "edDrumsVst3HostTest: SKIP (GEARMULATOR_MD_FIRMWARE_BIN names no flash image)\n";
		return 77;
	}
	const Library library(image);

	const DataRoot root;
	int result = 0;
	{
		juce::ScopedJuceInitialiser_GUI juce;
		const auto bundle = bundleOf(juce::File(juce::String(_argv[1])));
		try
		{
			require(bundle.isDirectory(), "no VST3 bundle");
			run(bundle, library);
			std::cout << "edDrumsVst3HostTest: OK\n";
		}
		catch(const std::exception& _error)
		{
			std::cerr << "edDrumsVst3HostTest: " << _error.what() << '\n';
			result = 1;
		}
	}
	return result;
}
