// ST Drums as a host loads it: the built VST3 through JUCE's VST3 hosting, with a data folder of its own
// (GEARMULATOR_DATA_ROOT), against syntakt::Syntakt driven directly with the same calls:
//
//   - without the firmware: it loads, plays silence, opens its editor;
//   - with the user's OS 1.41 .syx in its roms folder: the class (ST Drums by c0remusic, an instrument), Main and 15
//     mono Track buses, the 866 parameters and their VST3 IDs (the hashes of the frozen string IDs), the only
//     automatable ones with Bypass;
//   - notes 36-51 sound on their Track's output (Track 16 on Main alone);
//   - at 48 kHz every output is the module's, sample for sample after Q31 to float, in blocks of 128 with a parameter
//     automated and in random blocks with note-offs; no latency reported;
//   - at 44.1 kHz every output is the module's resampled by the stack's resampler (MameHq), the latency reported the
//     resampler's, which the Hits' onsets show; Main alone (only Main resampled) as Main with the 15 Tracks;
//   - a machine set by the host gives the track's SYN page the machine's defaults, which the host reads again;
//   - the state through IComponent: another instance gets every parameter back;
//   - no block of 128 over 10 ms on a busy pattern, and nothing allocated by the player's render (in process);
//   - the note-to-sound latency and its jitter at 48 and 44.1 kHz, measured and printed.
//
// SYNTAKT_OS_SYX names the user's Syntakt_OS1.41.syx; exit 77 without it.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "stDrumsFirmware.h"
#include "stDrumsPlayer.h"
#include "stSection7.h"
#include "synthLib/resampler.h"
#include "synthLib/sampleRateTime.h"

// The allocations made on a thread while it counts them: the player's render must make none
namespace
{
	thread_local bool g_counting = false;
	std::atomic<int> g_allocations{0};
}

void* operator new(const size_t _size)
{
	if(g_counting)
		++g_allocations;
	if(void* p = std::malloc(_size ? _size : 1))
		return p;
	throw std::bad_alloc();
}

void operator delete(void* _p) noexcept { std::free(_p); }
void operator delete(void* _p, size_t) noexcept { std::free(_p); }
void* operator new[](const size_t _size) { return operator new(_size); }
void operator delete[](void* _p) noexcept { std::free(_p); }
void operator delete[](void* _p, size_t) noexcept { std::free(_p); }

namespace
{
	using Clock = std::chrono::steady_clock;
	using syntakt::Syntakt;

	constexpr int g_block = 128;
	constexpr int g_tracks = 16;
	constexpr int g_outs = 15;
	constexpr int g_parameters = 866;	// 16 tracks of 53 (Machine, Note, Level, 50 of the table), 17 FX, Tempo
	constexpr int g_channels = 2 + g_outs;
	constexpr int g_note = 60;			// every track's Note, as the plug-in starts
	constexpr double g_stallMs = 10.0;
	constexpr float g_sound = 1e-3f;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	void pump(const int _milliseconds)
	{
		juce::MessageManager::getInstance()->runDispatchLoopUntil(_milliseconds);
	}

	// A data folder of this run's (GEARMULATOR_DATA_ROOT), never the user's
	class DataRoot
	{
	public:
		DataRoot()
			: m_path(std::filesystem::temp_directory_path()
				/ ("STDrumsVst3HostTest_" + std::to_string(Clock::now().time_since_epoch().count())))
		{
			std::filesystem::create_directories(roms());
			set(m_path.string().c_str());
		}
		~DataRoot()
		{
			set("");
			std::error_code error;
			std::filesystem::remove_all(m_path, error);
		}

		std::filesystem::path roms() const { return m_path / "c0remusic" / "ST Drums" / "roms"; }

	private:
		// An empty value counts as unset (Processor::dataFolder)
		static void set(const char* _value)
		{
#if defined(_WIN32)
			_putenv_s("GEARMULATOR_DATA_ROOT", _value);
#else
			setenv("GEARMULATOR_DATA_ROOT", _value, 1);
#endif
		}

		std::filesystem::path m_path;
	};

	// The frozen string IDs in the plug-in's order, written out here: a renumbering in the plug-in fails this test
	std::vector<juce::String> frozenIds()
	{
		static constexpr int TrackIds[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 19, 20, 21, 22, 23,
			25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
			53};
		static constexpr int FxIds[] = {17, 18, 19, 20, 21, 22, 23, 24, 25, 27, 28, 29, 30, 31, 32, 33, 34};
		std::vector<juce::String> ids;
		for(int t = 1; t <= g_tracks; ++t)
		{
			const auto prefix = juce::String::formatted("t%02d_", t);
			ids.push_back(prefix + "machine");
			ids.push_back(prefix + "note");
			ids.push_back(prefix + "level");
			for(const int id : TrackIds)
				ids.push_back(prefix + juce::String::formatted("p%02d", id));
		}
		for(const int id : FxIds)
			ids.push_back(juce::String::formatted("fx_p%02d", id));
		ids.push_back("tempo");
		return ids;
	}

	// The parameter index of a frozen ID
	int indexOf(const juce::String& _id)
	{
		static const auto ids = frozenIds();
		const auto it = std::find(ids.begin(), ids.end(), _id);
		require(it != ids.end(), "no parameter " + _id.toStdString());
		return static_cast<int>(it - ids.begin());
	}

	// JUCE's wrapper: a parameter's VST3 ID is its string ID's hash, its top bit cleared
	uint32_t vst3Id(const juce::String& _id)
	{
		return static_cast<uint32_t>(_id.hashCode()) & 0x7fffffffu;
	}

	struct Note
	{
		int64_t sample;
		int number;			// 36-51
		int velocity;
		bool off = false;
	};

	// What the plug-in tells the host by itself: re-reads (restartComponent) and parameters
	struct Told final : juce::AudioProcessorListener
	{
		std::atomic<int> rereads{0};
		std::atomic<int> gestures{0};
		void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
		void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& _details) override
		{
			if(_details.programChanged)
				++rereads;
		}
		void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++gestures; }
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

		juce::AudioProcessorParameter& parameter(const int _index) const { return *plugin->getParameters()[_index]; }

		// Main and, with _tracks, the 15 Track buses at _rate
		void prepare(const double _rate, const bool _tracks) const
		{
			plugin->releaseResources();
			auto layout = plugin->getBusesLayout();
			for(int bus = 1; bus < layout.outputBuses.size(); ++bus)
				layout.outputBuses.getReference(bus) = _tracks ? juce::AudioChannelSet::mono()
					: juce::AudioChannelSet::disabled();
			require(plugin->setBusesLayout(layout), "the host cannot take the Track buses it asks for");
			plugin->prepareToPlay(_rate, g_block);
		}
	};

	std::unique_ptr<Instance> load(juce::AudioPluginFormatManager& _formats, const juce::PluginDescription& _description,
		const double _rate, const bool _tracks = true)
	{
		auto instance = std::make_unique<Instance>();
		juce::String error;
		instance->plugin = _formats.createPluginInstance(_description, _rate, g_block, error);
		require(instance->plugin != nullptr, "the host cannot create ST Drums: " + error.toStdString());
		instance->plugin->addListener(&instance->told);
		instance->prepare(_rate, _tracks);
		pump(100);
		return instance;
	}

	// The plug-in played as a host's audio callback plays it, in blocks of g_block or, with a seed, of random sizes up
	// to it; _beforeBlock runs before each block with its first sample, _blockMs takes each block's time
	juce::AudioBuffer<float> render(juce::AudioProcessor& _plugin, const std::vector<Note>& _notes, const int64_t _samples,
		uint32_t _random = 0, std::vector<double>* _blockMs = nullptr, const std::function<void(int64_t)>& _beforeBlock = {})
	{
		const auto channels = _plugin.getTotalNumOutputChannels();
		juce::AudioBuffer<float> take(channels, static_cast<int>(_samples));
		take.clear();
		juce::AudioBuffer<float> buffer(channels, g_block);
		juce::MidiBuffer midi;
		size_t next = 0;
		for(int64_t position = 0; position < _samples;)
		{
			int size = g_block;
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
			{
				const auto& n = _notes[next];
				midi.addEvent(n.off ? juce::MidiMessage::noteOff(1 + n.number % 4, n.number)
					: juce::MidiMessage::noteOn(1 + n.number % 4, n.number, static_cast<juce::uint8>(n.velocity)),
					static_cast<int>(n.sample - position));
			}
			if(_beforeBlock)
				_beforeBlock(position);
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

	// A host's call at a sample: a parameter of a track set to a value (0-127), as the plug-in gives it the module
	struct Automation
	{
		int64_t sample;
		int track;
		int id;
		int value;
	};

	// syntakt::Syntakt with 16 tracks driven directly: each note a noteOn (Note 60, its velocity) or noteOff of its
	// track at its sample (the source's, 48 kHz), each automation a setParameter; the plug-in's channels
	std::vector<std::vector<float>> reference(const stDrums::Firmware& _firmware, const std::vector<Note>& _notes,
		const int64_t _samples, const std::vector<Automation>& _automations = {})
	{
		stDrums::Engine engine(std::make_shared<stDrums::Firmware>(_firmware));
		auto& st = engine.module();
		std::vector<std::vector<int32_t>> q31(Syntakt::Outputs16, std::vector<int32_t>(static_cast<size_t>(_samples)));
		std::vector<int32_t*> pointers(Syntakt::Outputs16);
		int64_t position = 0;
		const auto renderTo = [&](const int64_t _end)
		{
			if(_end <= position)
				return;
			for(int c = 0; c < Syntakt::Outputs16; ++c)
				pointers[static_cast<size_t>(c)] = q31[static_cast<size_t>(c)].data() + position;
			st.render(pointers.data(), static_cast<size_t>(_end - position));
			position = _end;
		};
		size_t a = 0;
		for(const auto& n : _notes)
		{
			for(; a < _automations.size() && _automations[a].sample <= n.sample; ++a)
			{
				renderTo(_automations[a].sample);
				const auto& x = _automations[a];
				st.setParameter(x.track, x.id, engine.trackWord(std::max(stDrums::machineIndex(st.machine(x.track)), 0),
					x.id, x.value));
			}
			renderTo(std::min(n.sample, _samples));
			if(n.off)
				st.noteOff(n.number - 36);
			else
				st.noteOn(n.number - 36, g_note, n.velocity);
		}
		for(; a < _automations.size(); ++a)
		{
			renderTo(_automations[a].sample);
			const auto& x = _automations[a];
			st.setParameter(x.track, x.id, engine.trackWord(std::max(stDrums::machineIndex(st.machine(x.track)), 0),
				x.id, x.value));
		}
		renderTo(_samples);

		std::vector<std::vector<float>> out(g_channels, std::vector<float>(static_cast<size_t>(_samples)));
		std::vector<const int32_t*> q(Syntakt::Outputs16);
		for(int c = 0; c < Syntakt::Outputs16; ++c)
			q[static_cast<size_t>(c)] = q31[static_cast<size_t>(c)].data();
		for(size_t s = 0; s < static_cast<size_t>(_samples); ++s)
		{
			out[0][s] = stDrums::mainSample(q.data(), s, 0);
			out[1][s] = stDrums::mainSample(q.data(), s, 1);
			for(int t = 0; t < g_outs; ++t)
				out[static_cast<size_t>(2 + t)][s] = stDrums::stemSample(q.data(), s, t);
		}
		return out;
	}

	// The reference at _rate: its notes on the source samples the host's fall on (rounded up), then every channel
	// resampled by the stack's resampler in blocks of g_block
	std::vector<std::vector<float>> resampledReference(const stDrums::Firmware& _firmware, std::vector<Note> _notes,
		const int64_t _samples, const double _rate)
	{
		for(auto& n : _notes)
			n.sample = static_cast<int64_t>(synthLib::rescaleSamplesCeil(static_cast<uint64_t>(n.sample),
				static_cast<float>(_rate), 48000.0f));
		const auto sourceSamples = static_cast<int64_t>(std::ceil(static_cast<double>(_samples) * 48000.0 / _rate)) + 4096;
		const auto source = reference(_firmware, _notes, sourceSamples);

		synthLib::Resampler resampler(48000.0f, static_cast<float>(_rate), synthLib::Resampler::Mode::MameHq);
		resampler.prepare(g_channels, g_block);
		std::vector<std::vector<float>> out(g_channels, std::vector<float>(static_cast<size_t>(_samples)));
		size_t read = 0;
		for(int64_t position = 0; position < _samples; position += g_block)
		{
			const auto n = static_cast<uint32_t>(std::min<int64_t>(g_block, _samples - position));
			synthLib::TAudioOutputs outputs{};
			for(int c = 0; c < g_channels; ++c)
				outputs[static_cast<size_t>(c)] = out[static_cast<size_t>(c)].data() + position;
			resampler.process(outputs, g_channels, n, false, [&](synthLib::TAudioOutputs& _o, const uint32_t _n)
			{
				require(read + _n <= source[0].size(), "the reference's source is too short");
				for(int c = 0; c < g_channels; ++c)
					std::copy_n(source[static_cast<size_t>(c)].data() + read, _n, _o[static_cast<size_t>(c)]);
				read += _n;
			});
		}
		return out;
	}

	// Every channel of _take (Main, then the Tracks the host took) against _expected: the samples that differ and the
	// largest difference
	void compare(const char* _what, const juce::AudioBuffer<float>& _take, const std::vector<std::vector<float>>& _expected,
		const float _tolerance)
	{
		int64_t differ = 0;
		float largest = 0;
		int firstChannel = -1;
		int64_t firstSample = -1;
		float peak = 0;
		const int channels = _take.getNumChannels();
		for(int c = 0; c < channels; ++c)
		{
			for(int s = 0; s < _take.getNumSamples(); ++s)
			{
				const float got = _take.getSample(c, s), want = _expected[static_cast<size_t>(c)][static_cast<size_t>(s)];
				peak = std::max(peak, std::abs(want));
				const float d = std::abs(got - want);
				if(got == want)
					continue;
				++differ;
				largest = std::max(largest, d);
				if(firstSample < 0)
				{
					firstChannel = c;
					firstSample = s;
				}
			}
		}
		std::printf("%s: %lld samples of %d channels differ (largest %.3g), the reference's peak %.3f\n", _what,
			static_cast<long long>(differ), channels, static_cast<double>(largest), static_cast<double>(peak));
		require(peak > g_sound, std::string(_what) + ": the reference is silent");
		require(largest <= _tolerance, std::string(_what) + ": first difference on channel "
			+ std::to_string(firstChannel) + " at sample " + std::to_string(firstSample));
	}

	// Notes 36-51 one after the other
	std::vector<Note> eachTrack(const int64_t _spacing)
	{
		std::vector<Note> notes;
		for(int t = 0; t < g_tracks; ++t)
			notes.push_back({_spacing * (t + 1), 36 + t, 100});
		return notes;
	}

	// Two hits a sixteenth at 120 BPM over the 16 tracks, each released a 32nd later, from 0.25 s
	std::vector<Note> pattern(const double _seconds, const double _rate, const bool _offs)
	{
		std::vector<Note> notes;
		for(int step = 0; 0.25 + step * 0.125 < _seconds - 0.25; ++step)
		{
			const auto at = std::llround((0.25 + step * 0.125) * _rate) + step * 3;
			const int a = 36 + step * 5 % 16, b = 36 + (step * 7 + 3) % 16;
			notes.push_back({at, a, 30 + step * 37 % 97});
			if(b != a)
				notes.push_back({at, b, 100});
			if(_offs)
			{
				const auto off = at + std::llround(0.0625 * _rate);
				notes.push_back({off, a, 0, true});
			}
		}
		std::stable_sort(notes.begin(), notes.end(), [](const Note& _x, const Note& _y) { return _x.sample < _y.sample; });
		return notes;
	}

	// Track 1's note (BD MODERN) at _count times whose offsets in the 32-sample block and the host's block vary: the
	// samples from each note to where Track 01 first reaches a quarter of the hit's peak
	struct Onsets
	{
		std::vector<double> samples;
		double mean() const
		{
			double s = 0;
			for(const auto v : samples)
				s += v;
			return s / static_cast<double>(samples.size());
		}
		double min() const { return *std::min_element(samples.begin(), samples.end()); }
		double max() const { return *std::max_element(samples.begin(), samples.end()); }
	};

	Onsets onsets(juce::AudioProcessor& _plugin, const double _rate, const int _count)
	{
		// A short decay and release, each note released 50 ms on, so that each hit starts from silence
		_plugin.getParameters()[indexOf("t01_p42")]->setValue(20.0f / 127.0f);
		_plugin.getParameters()[indexOf("t01_p44")]->setValue(10.0f / 127.0f);
		render(_plugin, {}, g_block * 4);
		const auto spacing = std::llround(0.25 * _rate);
		std::vector<Note> notes, played;
		for(int i = 0; i < _count; ++i)
		{
			const auto at = spacing * (i + 1) + i * 37 % 61;
			notes.push_back({at, 36, 100});
			played.push_back({at, 36, 100});
			played.push_back({at + std::llround(0.05 * _rate), 36, 0, true});
		}
		const auto take = render(_plugin, played, spacing * (_count + 2));
		Onsets result;
		for(const auto& n : notes)
		{
			const auto from = static_cast<int>(n.sample);
			const auto length = static_cast<int>(spacing);
			const float peak = take.getMagnitude(2, from, length);
			require(peak > g_sound, "Track 1's hit is silent on Track 01");
			require(std::abs(take.getSample(2, from)) < 0.01f * peak, "Track 1's hit does not start from silence");
			for(int s = from; s < from + length; ++s)
			{
				if(std::abs(take.getSample(2, s)) >= 0.25f * peak)
				{
					result.samples.push_back(static_cast<double>(s - n.sample));
					break;
				}
			}
		}
		return result;
	}

	std::string blockStats(std::vector<double> _ms, const double _rate)
	{
		std::sort(_ms.begin(), _ms.end());
		const auto deadline = 1000.0 * g_block / _rate;
		char text[200];
		std::snprintf(text, sizeof text, "median %.3f ms (%.1f %% of its %.2f ms), 99.9 %% %.3f ms, worst %.3f ms",
			_ms[_ms.size() / 2], 100.0 * _ms[_ms.size() / 2] / deadline, deadline,
			_ms[std::min(_ms.size() - 1, _ms.size() * 999 / 1000)], _ms.back());
		return text;
	}

	juce::File bundleOf(const juce::File& _file)
	{
		for(auto file = _file; file.exists() && file != file.getParentDirectory(); file = file.getParentDirectory())
		{
			if(file.isDirectory() && file.hasFileExtension("vst3"))
				return file;
		}
		return _file;
	}

	// The editor opened in a window of the host's and drawn
	void openEditor(juce::AudioPluginInstance& _plugin, const char* _what)
	{
		require(_plugin.hasEditor(), "ST Drums has no editor");
		std::unique_ptr<juce::AudioProcessorEditor> editor(_plugin.createEditorIfNeeded());
		require(editor != nullptr, std::string("the host cannot open the editor ") + _what);
		juce::Component window;
		window.addAndMakeVisible(*editor);
		window.setSize(editor->getWidth(), editor->getHeight());
		window.setTopLeftPosition(48, 48);
		window.addToDesktop(juce::ComponentPeer::windowIsTemporary);
		window.setVisible(true);
		pump(800);
		std::printf("Editor %s: %dx%d\n", _what, editor->getWidth(), editor->getHeight());
		require(editor->getWidth() > 0 && editor->getHeight() > 0, "the editor has no size");
		window.removeChildComponent(editor.get());
		editor.reset();
		pump(100);
	}

	void run(const juce::File& _bundle, const std::vector<uint8_t>& _syx, const std::filesystem::path& _syxPath,
		const DataRoot& _root)
	{
		stDrums::Firmware firmware;
		std::string error;
		require(stDrums::load(_syx, firmware, error), "the .syx does not load: " + error);

		juce::AudioPluginFormatManager formats;
		formats.addFormat(new juce::VST3PluginFormat());
		juce::OwnedArray<juce::PluginDescription> found;
		formats.getFormat(0)->findAllTypesForFile(found, _bundle.getFullPathName());
		require(found.size() == 1, "the VST3 does not hold one plug-in");
		const auto& description = *found[0];
		std::printf("%s by %s %s, %s, %s, class hash %08x\n", description.name.toRawUTF8(),
			description.manufacturerName.toRawUTF8(), description.version.toRawUTF8(), description.category.toRawUTF8(),
			description.isInstrument ? "instrument" : "effect", static_cast<unsigned>(description.uniqueId));
		require(description.name == "ST Drums" && description.manufacturerName == "c0remusic" && description.isInstrument,
			"the VST3 is not ST Drums, an instrument by c0remusic");

		// Without the firmware: silence, and the editor (its button chooses the file)
		{
			auto instance = load(formats, description, 48000.0);
			const auto take = render(*instance->plugin, eachTrack(4800), 4800 * 18);
			std::printf("Without the firmware: Main's peak %.6f\n", take.getMagnitude(0, 0, take.getNumSamples()));
			require(take.getMagnitude(0, take.getNumSamples()) == 0.0f, "ST Drums sounds without its firmware");
			openEditor(*instance->plugin, "without the firmware");
		}

		std::filesystem::copy_file(_syxPath, _root.roms() / _syxPath.filename());

		// The class, the buses, the parameters
		{
			const auto start = Clock::now();
			auto instance = load(formats, description, 48000.0);
			auto& plugin = *instance->plugin;
			std::printf("Loaded with the firmware and prepared in %.0f ms; %d re-reads, %d gestures told\n",
				std::chrono::duration<double, std::milli>(Clock::now() - start).count(), instance->told.rereads.load(),
				instance->told.gestures.load());
			require(instance->told.gestures == 0, "ST Drums began a gesture at load");

			require(plugin.getBusCount(true) == 0 && plugin.getBusCount(false) == 1 + g_outs, "not Main and 15 Tracks");
			require(plugin.getBus(false, 0)->getName() == "Main" && plugin.getChannelCountOfBus(false, 0) == 2,
				"Main is not a stereo bus");
			for(int bus = 1; bus <= g_outs; ++bus)
			{
				require(plugin.getBus(false, bus)->getName() == juce::String::formatted("Track %02d", bus)
					&& plugin.getBus(false, bus)->getDefaultLayout() == juce::AudioChannelSet::mono(),
					"Track " + std::to_string(bus) + " is not a mono bus");
			}
			require(plugin.getTotalNumOutputChannels() == g_channels, "Main and 15 Tracks are not 17 channels");

			const auto& parameters = plugin.getParameters();
			const auto ids = frozenIds();
			require(static_cast<int>(ids.size()) == g_parameters, "the test's list is not 866 IDs");
			int automatable = 0;
			juce::StringArray others;
			std::unordered_set<uint32_t> unique;
			for(int i = 0; i < parameters.size(); ++i)
			{
				const auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*>(parameters[i]);
				require(hosted != nullptr, "a parameter without a VST3 ID");
				const auto id = static_cast<uint32_t>(hosted->getParameterID().getLargeIntValue());
				require(unique.insert(id).second, "two parameters share a VST3 ID");
				if(i < g_parameters && id != vst3Id(ids[static_cast<size_t>(i)]))
				{
					throw std::runtime_error("parameter " + std::to_string(i) + " \"" + parameters[i]->getName(64).toStdString()
						+ "\" has VST3 ID " + std::to_string(id) + ", not the hash of " + ids[static_cast<size_t>(i)].toStdString());
				}
				if(!parameters[i]->isAutomatable())
					continue;
				++automatable;
				if(i >= g_parameters)
					others.add(parameters[i]->getName(64));
			}
			std::printf("%d parameters through VST3, %d automatable: the 866 and %s; T01's first: %s, %s, %s, %s\n",
				parameters.size(), automatable, others.joinIntoString(", ").toRawUTF8(),
				parameters[0]->getName(64).toRawUTF8(), parameters[1]->getName(64).toRawUTF8(),
				parameters[2]->getName(64).toRawUTF8(), parameters[3]->getName(64).toRawUTF8());
			require(automatable == g_parameters + 1 && others == juce::StringArray("Bypass"),
				"the host lists other parameters than ST Drums' 866 and Bypass");
			require(plugin.getLatencySamples() == 0, "a latency reported at 48 kHz");

			// The kit the firmware gives: track 1 BD MODERN, track 2 SD VINTAGE (the OS's default machines)
			require(parameters[indexOf("t01_machine")]->getCurrentValueAsText() == "BD MODERN"
				&& parameters[indexOf("t02_machine")]->getCurrentValueAsText() == "SD VINTAGE",
				"the parameters do not hold the firmware's default kit");

			// Each note on its Track's output; Track 16 on Main alone
			const auto spacing = int64_t{24000};
			const auto take = render(plugin, eachTrack(spacing), spacing * 18);
			std::printf("Notes 36-51, Main and the track's output:");
			for(int t = 0; t < g_tracks; ++t)
			{
				const auto from = spacing * (t + 1);
				const float main = std::max(take.getMagnitude(0, static_cast<int>(from), static_cast<int>(spacing)),
					take.getMagnitude(1, static_cast<int>(from), static_cast<int>(spacing)));
				const float own = t < g_outs ? take.getMagnitude(2 + t, static_cast<int>(from), static_cast<int>(spacing)) : 0;
				std::printf(" %d %.3f/%.3f", t + 1, main, own);
				require(main > g_sound, "Track " + std::to_string(t + 1) + "'s note is silent on Main");
				if(t < g_outs)
					require(own > g_sound, "Track " + std::to_string(t + 1) + "'s note is silent on Track "
						+ std::to_string(t + 1));
				for(int later = t + 1; later < g_outs; ++later)
					require(take.getMagnitude(2 + later, static_cast<int>(from), static_cast<int>(spacing)) == 0.0f,
						"Track " + std::to_string(t + 1) + " sounds on Track " + std::to_string(later + 1));
			}
			std::printf("\n");

			// A machine set by the host: track 2 to SY TOY, its SYN page at SY TOY's defaults, read again by the host
			{
				stDrums::Engine expected(std::make_shared<stDrums::Firmware>(firmware));
				expected.module().setMachine(1, 36);
				const int rereads = instance->told.rereads;
				instance->parameter(indexOf("t02_machine")).setValue(8.0f / 11.0f);
				render(plugin, {}, g_block * 4);
				pump(300);
				int wrong = 0;
				for(const int id : {18, 19, 20, 21, 22, 23, 25, 26})
				{
					const int want = stDrums::valueOf(expected.module().parameter(1, id));
					const int got = static_cast<int>(std::lround(instance->parameter(indexOf(juce::String::formatted("t02_p%02d",
						id))).getValue() * 127.0f));
					wrong += want != got;
				}
				std::printf("Track 2 set to SY TOY by the host: %d re-reads, %d of its 8 SYN values not SY TOY's\n",
					instance->told.rereads - rereads, wrong);
				require(instance->told.rereads > rereads && wrong == 0, "a machine's SYN defaults did not reach the host");
				instance->parameter(indexOf("t02_p18")).setValue(70.0f / 127.0f);
			}

			// The state: another instance gets every parameter back, and saves the same state
			juce::MemoryBlock saved;
			plugin.getStateInformation(saved);
			auto restored = load(formats, description, 48000.0);
			restored->plugin->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
			pump(200);
			for(int i = 0; i < g_parameters; ++i)
			{
				require(std::abs(instance->parameter(i).getValue() - restored->parameter(i).getValue()) < 1e-6f,
					"the restored " + ids[static_cast<size_t>(i)].toStdString() + " differs");
			}
			juce::MemoryBlock again;
			restored->plugin->getStateInformation(again);
			std::printf("State of %zu bytes: every parameter restored, %s\n", saved.getSize(),
				again == saved ? "the same state saved again" : "ANOTHER state saved");
			require(again == saved, "the restored instance saves another state");
			const auto sound = render(*restored->plugin, {{4800, 37, 100}}, 24000);
			require(sound.getMagnitude(3, 0, 24000) > g_sound, "the restored track 2 is silent");

			openEditor(plugin, "with the firmware");
		}

		// 48 kHz: the module's samples, in blocks of 128 with Volume automated, and in random blocks with note-offs
		{
			const auto samples = int64_t{48000 * 3};
			const auto notes = pattern(3.0, 48000.0, false);
			std::vector<Automation> automations{{128 * 300, 0, Syntakt::Volume, 40}, {128 * 600, 5, Syntakt::Volume, 90},
				{128 * 700, 3, Syntakt::FilterFrequency, 30}};
			auto instance = load(formats, description, 48000.0);
			const auto take = render(*instance->plugin, notes, samples, 0, nullptr, [&](const int64_t _position)
			{
				for(const auto& a : automations)
				{
					if(a.sample == _position)
						instance->parameter(indexOf(juce::String::formatted("t%02d_p%02d", a.track + 1, a.id)))
							.setValue(static_cast<float>(a.value) / 127.0f);
				}
			});
			compare("48 kHz, blocks of 128, 3 parameters automated", take, reference(firmware, notes, samples, automations),
				0.0f);

			const auto withOffs = pattern(3.0, 48000.0, true);
			auto random = load(formats, description, 48000.0);
			compare("48 kHz, random blocks of 1-128, note-offs", render(*random->plugin, withOffs, samples, 0x9e3779b9u),
				reference(firmware, withOffs, samples), 0.0f);
		}

		// 44.1 kHz: the module's samples resampled, the latency the resampler's
		int latency44 = 0;
		{
			const double rate = 44100.0;
			const auto samples = int64_t{44100 * 3};
			const auto notes = pattern(3.0, rate, true);
			auto instance = load(formats, description, rate);
			latency44 = instance->plugin->getLatencySamples();
			std::printf("44.1 kHz: latency reported %d samples (%.2f ms)\n", latency44, 1000.0 * latency44 / rate);
			require(latency44 > 0, "no latency reported at 44.1 kHz");
			const auto expected = resampledReference(firmware, notes, samples, rate);
			compare("44.1 kHz, blocks of 128", render(*instance->plugin, notes, samples), expected, 0.0f);
			auto random = load(formats, description, rate);
			compare("44.1 kHz, random blocks of 1-128", render(*random->plugin, notes, samples, 0x2545f491u), expected,
				0.0f);
			// Main alone, as Live takes it until a Track's output is routed: only Main resampled
			auto main = load(formats, description, rate, false);
			require(main->plugin->getTotalNumOutputChannels() == 2, "Main alone is not 2 channels");
			compare("44.1 kHz, Main alone", render(*main->plugin, notes, samples), expected, 0.0f);
		}

		// The note-to-sound latency and its jitter on Track 1 (BD MODERN), 64 notes at varied offsets
		{
			auto at48 = load(formats, description, 48000.0);
			const auto o48 = onsets(*at48->plugin, 48000.0, 64);
			auto at44 = load(formats, description, 44100.0);
			const auto o44 = onsets(*at44->plugin, 44100.0, 64);
			std::printf("Note to sound, Track 1 (BD MODERN), a quarter of the hit's peak on Track 01, 64 notes:\n"
				"  48 kHz:   mean %.1f samples (%.3f ms), %.0f to %.0f: jitter %.0f samples (%.3f ms)\n"
				"  44.1 kHz: mean %.1f samples (%.3f ms), %.0f to %.0f: jitter %.0f samples (%.3f ms)\n",
				o48.mean(), 1000 * o48.mean() / 48000, o48.min(), o48.max(), o48.max() - o48.min(),
				1000 * (o48.max() - o48.min()) / 48000, o44.mean(), 1000 * o44.mean() / 44100, o44.min(), o44.max(),
				o44.max() - o44.min(), 1000 * (o44.max() - o44.min()) / 44100);
			// The resampler's delay is the difference, in the host's samples
			const double added = o44.mean() - o48.mean() * 44100.0 / 48000.0;
			std::printf("  the 44.1 kHz onsets come %.1f samples after the 48 kHz ones: the reported %d\n", added,
				latency44);
			require(std::abs(added - latency44) <= 3.0, "the latency reported is not the onsets'");
			require(o48.max() - o48.min() <= 33, "more jitter at 48 kHz than the 32-sample block");
		}

		// A busy pattern, the audio thread's time per block of 128: at 48 kHz, at 44.1 kHz with the 15 Tracks and with
		// Main alone
		for(const auto& [rate, tracks] : {std::pair{48000.0, true}, std::pair{44100.0, true}, std::pair{44100.0, false}})
		{
			auto instance = load(formats, description, rate, tracks);
			std::vector<double> blockMs;
			blockMs.reserve(16384);
			render(*instance->plugin, pattern(20.0, rate, true), std::llround(20.0 * rate), 0, &blockMs);
			std::printf("A block of 128 at %.1f kHz through VST3, %s, 20 s of a busy pattern: %s\n", rate / 1000,
				tracks ? "Main and 15 Tracks" : "Main alone", blockStats(blockMs, rate).c_str());
			require(*std::max_element(blockMs.begin(), blockMs.end()) < g_stallMs, "a block stalled the audio thread");
		}

		// The player's render allocates nothing, at 48 and 44.1 kHz, notes and note-offs included (in process)
		for(const double rate : {48000.0, 44100.0})
		{
			stDrums::Engine engine(std::make_shared<stDrums::Firmware>(firmware));
			stDrums::Player player;
			player.prepare(rate, g_block);
			player.setEngine(&engine);
			std::vector<std::vector<float>> out(g_channels, std::vector<float>(g_block));
			std::vector<float*> pointers(g_channels);
			for(int c = 0; c < g_channels; ++c)
				pointers[static_cast<size_t>(c)] = out[static_cast<size_t>(c)].data();
			const auto notes = pattern(5.0, rate, true);
			size_t next = 0;
			g_allocations = 0;
			for(int64_t position = 0; position < std::llround(5.0 * rate); position += g_block)
			{
				g_counting = true;
				for(; next < notes.size() && notes[next].sample < position + g_block; ++next)
				{
					const auto& n = notes[next];
					const auto offset = static_cast<int>(n.sample - position);
					if(n.off)
						player.noteOff(offset, n.number - 36);
					else
						player.noteOn(offset, n.number - 36, g_note, n.velocity);
				}
				player.render(pointers.data(), g_block);
				g_counting = false;
			}
			std::printf("The player's render at %.1f kHz, 5 s with notes: %d allocations\n", rate / 1000,
				g_allocations.load());
			require(g_allocations == 0, "the player's render allocates");
		}
	}
}

int main(const int _argc, char* _argv[])
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	if(_argc < 2)
	{
		std::cerr << "usage: stDrumsVst3HostTest <ST Drums.vst3>\n";
		return 2;
	}
	const char* syxPath = std::getenv("SYNTAKT_OS_SYX");
	if(!syxPath || !*syxPath || !std::filesystem::exists(syxPath))
	{
		std::cout << "stDrumsVst3HostTest: SKIP (SYNTAKT_OS_SYX names no Syntakt OS 1.41 .syx)\n";
		return 77;
	}
	const auto syx = syntakt::readFile(syxPath);

	// Before the plug-in loads: its data folder is this run's
	const DataRoot root;
	int result = 0;
	{
		juce::ScopedJuceInitialiser_GUI juce;
		const auto bundle = bundleOf(juce::File(juce::String(_argv[1])));
		if(!bundle.isDirectory())
		{
			std::cerr << "stDrumsVst3HostTest: no VST3 bundle at " << _argv[1] << '\n';
			result = 1;
		}
		else
		{
			try
			{
				run(bundle, syx, std::filesystem::path(syxPath), root);
				std::cout << "stDrumsVst3HostTest: OK\n";
			}
			catch(const std::exception& _error)
			{
				std::cerr << "stDrumsVst3HostTest: " << _error.what() << '\n';
				result = 1;
			}
		}
	}
	return result;
}
