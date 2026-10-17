// MD Drums' processor as a host drives it, at 44.1 and 48 kHz: 576 host parameters in the order and with the IDs the
// port froze, a note 36-51 sounding its Track and other notes nothing, host automation of a Track's machine reaching
// the Device, SYN1 named after the machine, the Out parameter taking a Track off Main whatever the host does with its
// bus, and the state bringing back the Device's Kit and mixer and the parameters with them; a Track played from the
// editor sounds the machine chosen just before. It reports the note-to-sound latency through the whole plug-in.

#include "mdDrumsProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include "mdDrumsAllocationCounter.h"	// a relay set by the host must not allocate (the audio thread sets it)
#include "mdDrumsController.h"
#include "mdDrumsDevice.h"
#include "mdDrumsHostDriver.h"
#include "mdDrumsSampleFile.h"
#include "mdDrumsTestMachine.h"
#include "mdDrumsTestSupport.h"

#include "synthLib/plugin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace
{
	using mdDrums::hostDriver::g_silence;
	using mdDrums::hostDriver::mainPeak;
	using mdDrums::hostDriver::Note;
	using mdDrums::hostDriver::peak;
	using mdDrums::hostDriver::require;
	using mdDrums::testSupport::hostSets;
	using mdDrums::testSupport::parameter;
	using mdDrums::testSupport::prepare;
	using mdDrums::testSupport::uiSets;

	constexpr int g_block = 512;

	// The relays for Push 2 come after the Tracks' parameters and the master effects
	static_assert(static_cast<size_t>(mdDrums::hostDriver::Host::Relays) == mdDrums::Controller::ParameterCount);

	// The processor as a host in process sees it (mdDrumsHostDriver.h), from its making to its end: what the plug-in
	// tells the host by itself, as JUCE reports it in process. An edit is a parameter's listeners called, which JUCE's
	// VST3 wrapper turns into performEdit on the message thread and Live records while it records; a re-read is a
	// change that makes the host read every value again (restartComponent with kParamValuesChanged); and the gestures
	// it begins and ends. A tick is the Controller's timer, which a block's messages wait for.
	class HostWatch final : public mdDrums::hostDriver::Host, juce::AudioProcessorListener
	{
	public:
		explicit HostWatch(mdDrums::Processor& _processor) : Host(g_block), m_processor(_processor)
		{
			m_processor.addListener(this);
		}
		~HostWatch() override { m_processor.removeListener(this); }
		HostWatch(const HostWatch&) = delete;
		HostWatch& operator=(const HostWatch&) = delete;

		juce::AudioProcessor& plugin() override { return m_processor; }
		int index(const std::string& _name, const int _part) override
		{
			auto* p = m_processor.getController().getParameter(_name, static_cast<uint8_t>(_part));
			require(p != nullptr, "no parameter " + _name);
			return p->getParameterIndex();
		}
		void tick() override { m_processor.getController().processPendingMidiMessages(); }
		std::vector<int> edits() override { return m_edits; }
		int rereads() override { return m_rereads; }
		void clear() override
		{
			m_edits.clear();
			m_rereads = 0;
			begun.clear();
			ended = 0;
		}

		std::vector<int> begun;	// the parameters' indices, in order
		int ended = 0;

	private:
		void audioProcessorParameterChanged(juce::AudioProcessor*, const int _index, float) override
		{
			m_edits.push_back(_index);
		}
		void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& _details) override
		{
			if(_details.programChanged)
				++m_rereads;
		}
		void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, const int _index) override
		{
			begun.push_back(_index);
		}
		void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ++ended; }

		mdDrums::Processor& m_processor;
		std::vector<int> m_edits;
		int m_rereads = 0;
	};

	struct Result
	{
		int first = -1;		// host samples from the note to the first sample of Main above -100 dBFS
		float peak = 0;		// Main
		double energy = 0;
		float outPeak = 0;	// Out 01, when the host enables it (channels 2 on)
		float before = 0;	// Main in the block before the strike
	};

	// A second of silence, then a second (in whole blocks) struck just before its first block: _notes in its MIDI, as a
	// host gives them, and _strike, if any, playing a Track another way; the first sound is counted from sample _offset
	// of that block
	Result play(juce::AudioProcessor& _processor, const double _rate, const std::vector<Note>& _notes,
		const std::function<void()>& _strike, const int _offset = 0)
	{
		using mdDrums::hostDriver::render;
		const auto second = static_cast<int64_t>(_rate / g_block) * g_block;
		const auto silence = render(_processor, {}, second, g_block);
		Result result;
		result.before = mainPeak(silence, second - g_block, g_block);
		const auto take = render(_processor, _notes, second, g_block, 0, nullptr, [&](const int64_t _position)
		{
			if(_position == 0 && _strike)
				_strike();
		});
		for(int s = 0; s < static_cast<int>(second); ++s)
		{
			const auto value = std::max(std::abs(take.getSample(0, s)), std::abs(take.getSample(1, s)));
			if(result.first < 0 && value > g_silence)
				result.first = s - _offset;
			result.energy += double(value) * value;
		}
		result.peak = mainPeak(take, 0, second);
		if(take.getNumChannels() > 2)
			result.outPeak = peak(take, 2, 0, second);
		return result;
	}

	// A second of silence, then _note at sample _offset of a block, and a second from that block on
	Result play(juce::AudioProcessor& _processor, const int _note, const int _offset, const double _rate)
	{
		return play(_processor, _rate, {{_offset, _note, 100}}, {}, _offset);
	}

	// One block, then the Controller's pending messages, as its timer sends them
	void block(mdDrums::Processor& _processor)
	{
		mdDrums::hostDriver::render(_processor, {}, g_block, g_block);
		_processor.getController().processPendingMidiMessages();
	}

	// A new processor given a state, as a host opening a set gives it
	std::unique_ptr<mdDrums::Processor> restoredFrom(const juce::MemoryBlock& _state)
	{
		auto restored = std::make_unique<mdDrums::Processor>(true);
		static_cast<juce::AudioProcessor&>(*restored).setStateInformation(_state.getData(),
			static_cast<int>(_state.getSize()));
		return restored;
	}

	// A new processor given _processor's state
	std::unique_ptr<mdDrums::Processor> restoredFrom(juce::AudioProcessor& _processor)
	{
		juce::MemoryBlock state;
		_processor.getStateInformation(state);
		return restoredFrom(state);
	}

	// As a host automates: the value set on the parameter, then a block for the Controller to send it
	void automate(mdDrums::Processor& _processor, const std::string& _name, const uint8_t _part, const int _value)
	{
		auto& p = parameter(_processor, _name, _part);
		hostSets(p, _value);
		block(_processor);
		// What the editor binds to follows host automation once the Controller's timer has run
		require(static_cast<int>(p.getValueObject().getValue()) == _value, "the editor did not see " + _name + " change");
	}

	const mdDrums::Device& device(mdDrums::Processor& _processor)
	{
		const auto* d = dynamic_cast<const mdDrums::Device*>(_processor.getPlugin().getDevice());
		require(d != nullptr, "the processor has no MD Drums Device");
		return *d;
	}

	// The Kits (ticket 26): a Slot loaded reaches the Device as one dump, the parameters follow it, the mixer stays and it
	// is the Kit played; a parameter changed makes it differ from its Slot; the plug-in's state brings back its Slot; a
	// new name reaches the Device; a Link and a Choke reach the Device and the state (ticket 29); a machine changed gives
	// the 24 parameters ASSIGN MACHINE sets their values. The host reads the values again after a Kit or a machine,
	// with no edit.
	void kits(mdDrums::Processor& _processor)
	{
		auto& controller = _processor.controller();
		auto* bank = _processor.getBank();
		require(bank != nullptr && bank->slot(3).has_value(), "no Bank, or no Kit in its Slot 4");
		const auto kit = *bank->slot(3);
		const auto mixer = device(_processor).mixer();

		{
			HostWatch watch(_processor);
			controller.loadKit(kit, 3);
			block(_processor);
			std::printf("kits: a Kit loaded told the host %zu edits, %d re-reads\n", watch.edits().size(),
				watch.rereads());
			require(watch.edits().empty() && watch.rereads() > 0, "a Kit loaded did not reach the host as one re-read");
		}
		require(device(_processor).kit() == kit, "the Device does not play the Kit loaded");
		require(device(_processor).mixer() == mixer, "loading a Kit changed the mixer");
		require(controller.playedSlot() == 3 && controller.playedKit() == kit, "the Kit played is not the one loaded");
		require(parameter(_processor, "Machine", 0).getUnnormalizedValue() == kit.machine(0)
			&& parameter(_processor, "FLTF", 2).getUnnormalizedValue() == kit.parameters[2][12]
			&& parameter(_processor, "Level", 5).getUnnormalizedValue() == kit.levels[5],
			"the parameters do not follow the Kit loaded");
		automate(_processor, "FLTF", 2, kit.parameters[2][12] == 99 ? 98 : 99);
		require(controller.playedKit() != kit, "a parameter changed does not make the Kit differ from its Slot");

		controller.renameKit("RENAMED");
		block(_processor);
		require(device(_processor).kit().displayName() == "RENAMED" && controller.playedKit().displayName() == "RENAMED",
			"a new name did not reach the Device");

		// A Link and a Choke (ticket 29): the Device plays them from the next block, the Kit played holds them, the state
		// keeps them; a Track aimed at itself is Off
		const auto linkedKit = controller.playedKit();
		controller.setLink(0, 4);
		controller.setChoke(0, 2);
		controller.setLink(1, 1);
		block(_processor);
		require(device(_processor).kit().links[0] == 4 && device(_processor).kit().chokes[0] == 2,
			"a Link or a Choke did not reach the Device");
		require(controller.link(0) == 4 && controller.choke(0) == 2 && controller.link(1) == mdDrums::Controller::Off
			&& device(_processor).kit().links[1] == mdDrums::Controller::Off, "a Track linked to itself is not Off");
		require(controller.playedKit() != linkedKit && controller.playedKit().links[0] == 4,
			"a Link does not make the Kit played differ");
		// The state, once for the parameter changed, the name and the Link and Choke: the Kit played (all of them) and
		// its Slot come back, and the Device plays the Link
		{
			const auto restored = restoredFrom(_processor);
			const auto& c = restored->controller();
			require(c.playedSlot() == 3 && c.playedKit() == controller.playedKit(),
				"the state did not bring back the Kit played and its Slot");
			require(c.link(0) == 4 && c.choke(0) == 2 && device(*restored).kit().links[0] == 4,
				"the state did not keep a Link and a Choke");
		}
		controller.setLink(0, mdDrums::Controller::Off);
		controller.setChoke(0, mdDrums::Controller::Off);
		block(_processor);
		require(device(_processor).kit().links[0] == mdDrums::Controller::Off
			&& device(_processor).kit().chokes[0] == mdDrums::Controller::Off, "a Link or a Choke did not go Off");

		auto& syn1 = parameter(_processor, "SYN1", 0);
		const auto syn1Before = syn1.getUnnormalizedValue();
		HostWatch watch(_processor);
		// The effects and routing pages take OS 1.63's defaults with it (hostDriver::machineChange)
		mdDrums::hostDriver::machineChange(watch, 0, kit.machine(0) == 17 ? 16 : 17);
		require(syn1.getUnnormalizedValue() != syn1Before, "SYN1 did not take the new machine's value");
		std::printf("kits: Slot 04 %s loaded as one dump, parameters and Device following, mixer kept; Slot and Kit back from"
			" the state; renamed; a Link and a Choke to the Device and the state; a machine change: SYN1 %d to %d, %d"
			" re-reads and no edit\n", kit.displayName().c_str(), syn1Before, syn1.getUnnormalizedValue(),
			watch.rereads());
	}

	// Every Track and master parameter, set by the host, reaches the Device: its state reads back what the host set
	// (the Controller numbers its slots from the JSON's rows). Every relay has its target's range, default and text,
	// and sets its target on the Track shown.
	void everyParameter()
	{
		namespace pages = mdDrums::messages::pages;
		mdDrums::Processor processor(true);
		require(prepare(processor, 44100.0, g_block), "no Device");
		auto& host = static_cast<juce::AudioProcessor&>(processor);
		// Another value in the parameter's range; another machine the OS has for Machine
		const auto another = [](const pluginLib::Parameter& _p)
		{
			const auto& d = _p.getDescription();
			const auto current = _p.getUnnormalizedValue();
			if(d.page == pages::Machine)
				return current == 17 ? 16 : 17;
			return current < d.range.getEnd() ? current + 1 : current - 1;
		};
		size_t checked = 0, relays = 0;
		for(auto* hostParameter : host.getParameters())
		{
			auto* p = dynamic_cast<pluginLib::Parameter*>(hostParameter);
			if(!p)
				continue;
			const auto& d = p->getDescription();
			const auto what = d.name + " (part " + std::to_string(p->getPart() + 1) + ")";
			if(d.page == pages::Focus)
			{
				if(d.index == 0)
					continue;
				auto& target = parameter(processor, d.name.substr(std::string("Focus").size()), 0);
				const auto& t = target.getDescription();
				require(t.range == d.range && t.defaultValue == d.defaultValue && t.toText == d.toText,
					"the relay " + d.name + " has not its target's range, default and text");
				const auto value = another(target);
				hostSets(*p, value);
				block(processor);
				require(target.getUnnormalizedValue() == value, "the relay " + d.name + " did not set its target");
				++relays;
				continue;
			}
			const auto value = another(*p);
			hostSets(*p, value);
			block(processor);
			const mdDrums::messages::State state{device(processor).kit(), device(processor).mixer()};
			const auto read = mdDrums::messages::parameterValue(state, static_cast<uint8_t>(d.page), p->getPart(),
				d.index);
			require(read && *read == value, what + ": the host set " + std::to_string(value) + ", the Device holds "
				+ (read ? std::to_string(*read) : std::string("nothing")));
			++checked;
		}
		std::printf("every parameter: %zu set by the host and read back from the Device, %zu relays setting their"
			" targets\n", checked, relays);
		require(checked == mdDrums::Controller::ParameterCount && relays == mdDrums::Controller::RelayCount - 1,
			"not every Track and master parameter, or not every relay");
	}

	// md-drums-seq's hello through the host's MIDI: the file it names holds the Kit played, and goes with the processor
	void kitFile()
	{
		constexpr uint32_t id = 0x1f2e3d;
		const auto file = mdDrums::KitFiles::defaultFolder() / "1f2e3d.kit";
		std::string text;
		{
			mdDrums::Processor processor(true);
			require(prepare(processor, 44100.0, g_block), "no Device");
			std::vector<Note> hello;
			for(int note = 0; note < mdDrums::Device::HelloNotes; ++note)
				hello.push_back({10, mdDrums::Device::FirstHelloNote + note, mdDrums::Device::helloVelocity(id, note)});
			mdDrums::hostDriver::render(processor, hello, g_block, g_block);
			processor.getController().processPendingMidiMessages();
			std::ifstream in(file, std::ios::binary);
			text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}
		std::printf("kit file for md-drums-seq: %s, %zu bytes, %s\n", file.u8string().c_str(), text.size(),
			text.substr(0, text.find('\n', text.find("track 1 "))).c_str());
		require(text.rfind("md-drums-kit 1\nkit ", 0) == 0 && text.find("\ntrack 1 28 TRX-B2 ") != std::string::npos,
			"a hello did not give its device a file with the Kit played");
		require(!std::filesystem::exists(file), "the file stayed after the processor went");
	}

	// The relays for Push 2 (ticket 27): a relay set by the host (as Push or automation sets it) gives its parameter of
	// the Track shown its value, the Device getting it at the next block, the host not told; the relay Track shows
	// another Track and the relays then hold its values, the host taking them as edits without a gesture; a parameter
	// of the Track shown moved elsewhere moves its relay, the host not told (ticket 34); the state keeps the Track
	// shown; "Prepare Push list" moves a page of eight per Track, the Tracks' own parameters (ticket 37), and the
	// relays' entry the 64 of ticket 27's list, each in order and in a gesture
	void relays(mdDrums::Processor& _processor)
	{
		auto& controller = _processor.controller();
		auto& host = static_cast<juce::AudioProcessor&>(_processor);
		auto& relayTrack = *controller.relay(0);
		auto& relayFltf = *controller.relay(20);
		auto& relayMachine = *controller.relay(1);
		require(relayFltf.getDescription().name == "FocusFLTF", "relay 20 is not FLTF");

		controller.setCurrentPart(0);
		block(_processor);
		const auto fltf = parameter(_processor, "FLTF", 0).getUnnormalizedValue();
		const auto allocations = countAllocations([&] { hostSets(relayFltf, fltf == 33 ? 34 : 33); });
		require(allocations == 0, "a relay set by the host allocated " + std::to_string(allocations) + " times");
		{
			HostWatch watch(_processor);
			block(_processor);
			require(watch.edits().empty() && watch.rereads() == 0, "the host was told a relay's parameter: "
				+ std::to_string(watch.edits().size()) + " edits and " + std::to_string(watch.rereads()) + " re-reads");
		}
		const auto set = relayFltf.getUnnormalizedValue();
		require(parameter(_processor, "FLTF", 0).getUnnormalizedValue() == set
			&& device(_processor).kit().parameters[0][12] == set, "a relay did not set its parameter of the Track shown");

		require(countAllocations([&] { hostSets(relayTrack, 4); }) == 0, "the relay Track set by the host allocated");
		block(_processor);
		size_t shownEdits = 0;
		{
			HostWatch watch(_processor);
			mdDrums::hostDriver::relayTrack(watch, 5);
			shownEdits = watch.edits().size();
		}
		require(controller.getCurrentPart() == 5, "the relay Track did not show Track 6");
		require(relayMachine.getUnnormalizedValue() == parameter(_processor, "Machine", 5).getUnnormalizedValue()
			&& relayFltf.getUnnormalizedValue() == parameter(_processor, "FLTF", 5).getUnnormalizedValue(),
			"the relays do not hold the values of the Track shown");

		{
			HostWatch watch(_processor);
			const auto moved = parameter(_processor, "FLTF", 5).getUnnormalizedValue() == 99 ? 98 : 99;
			mdDrums::hostDriver::shownParameter(watch, 5, "FLTF", moved);
			require(relayFltf.getUnnormalizedValue() == moved, "a parameter of the Track shown moved without its relay");
		}

		const auto restored = restoredFrom(host);
		require(restored->getController().getCurrentPart() == 5, "the state did not keep the Track shown");
		{
			HostWatch watch(_processor);
			mdDrums::hostDriver::stateRestored(watch);
		}

		// "Prepare Push list": Level, FLTF, FLTW, FLTQ, DIST, SRR, DEL and REV of Track 1, then of Track 2, and so on, each
		// in its own gesture
		{
			HostWatch watch(_processor);
			controller.preparePushList();
			std::vector<int> expected;
			for(uint8_t track = 0; track < mdDrums::Controller::TrackCount; ++track)
				for(const auto* name : mdDrums::Controller::PushTrackParameters)
					expected.push_back(parameter(_processor, name, track).getParameterIndex());
			std::printf("Push list, a page per Track: %zu parameters moved, from \"%s\" to \"%s\", %d gestures ended\n",
				watch.begun.size(), host.getParameters()[expected.front()]->getName(64).toRawUTF8(),
				host.getParameters()[expected.back()]->getName(64).toRawUTF8(), watch.ended);
			require(watch.begun == expected && watch.ended == static_cast<int>(expected.size())
				&& watch.edits().size() >= expected.size(), "Prepare Push list did not move the Tracks' 128 in order");
		}
		// The relays' entry: the relays then the master effects
		{
			using mdDrums::Controller;
			HostWatch watch(_processor);
			controller.preparePushRelays();
			constexpr auto relays = Controller::RelayCount;
			constexpr auto entry = relays + Controller::MasterParameterCount;
			constexpr auto master = Controller::TrackCount * Controller::TrackParameterCount;
			bool ordered = watch.begun.size() == entry;
			for(size_t i = 0; ordered && i < entry; ++i)
			{
				const auto index = i < relays ? mdDrums::hostDriver::Host::Relays + i : master + (i - relays);
				ordered = watch.begun[i] == static_cast<int>(index);
			}
			std::printf("relays: FLTF set through its relay to %d; Track 6 shown by the relay Track, the relays"
				" following (%zu edits); the relays' Push list moved %zu parameters in order (no allocation setting a"
				" relay), %zu changes, %d gestures ended\n", set, shownEdits, watch.begun.size(), watch.edits().size(),
				watch.ended);
			require(ordered && watch.ended == static_cast<int>(entry) && watch.edits().size() >= entry,
				"Prepare Push list (relays) did not move the 64 in order");
		}
		controller.setCurrentPart(0);
		block(_processor);
	}

	// A flash image chosen in the editor (ticket 35), from where a tester without one stands: the stack's silent stand-in
	// (DummyDevice) in place of the Device, a set opened there. The stand-in keeps the set's Kit, and a set saved then
	// keeps it too. A file that is no flash image is refused, with why. The image makes the Device again, which receives
	// the set's Kit (EFM-BD on Track 1, not factory Kit 1's TRX-B2), the parameters following it (one moved while nothing
	// played comes back); it is then written into the data folder's roms under the name MD Drums looks for, and note 36
	// sounds. The data folder is a temporary one (GEARMULATOR_DATA_ROOT), never the user's.
	void flashLoader(const double _rate)
	{
		const auto flash = mdDrums::Engine::findFlashImage({});
		if(flash.empty())
		{
			std::printf("flash loader: no flash image on this PC, skipped\n");
			return;
		}
		const mdDrums::hostDriver::ScopedDataRoot data("MDDrumsFlash_");
		const auto& root = data.path();
		const auto chosen = root / "chosen.bin";
		std::ofstream(chosen, std::ios::binary).write(reinterpret_cast<const char*>(flash.data()),
			static_cast<std::streamsize>(flash.size()));
		{
			mdDrums::Processor processor(true);
			require(prepare(processor, _rate, g_block), "no Device to start from");
			juce::AudioProcessor& host = processor;
			automate(processor, "Machine", 0, 32);
			juce::MemoryBlock set;
			host.getStateInformation(set);
			processor.setDeviceType(pluginLib::DeviceType::Dummy, true);
			require(!processor.hasDevice(), "the stand-in passes for a Device");

			host.setStateInformation(set.getData(), static_cast<int>(set.getSize()));
			{
				const auto reopened = restoredFrom(host);
				require(device(*reopened).kit().machine(0) == 32, "a set saved while the stand-in played lost its Kit");
			}
			automate(processor, "Machine", 0, 33);

			const auto wrong = root / "not-a-flash.bin";
			std::ofstream(wrong, std::ios::binary) << "Machinedrum";
			const auto* before = processor.getPlugin().getDevice();
			const auto refused = processor.loadFlashImage(wrong);
			require(!refused.empty() && processor.getPlugin().getDevice() == before && !processor.hasDevice(),
				"a file that is no flash image was taken");

			const auto loaded = processor.loadFlashImage(chosen);
			require(loaded.empty(), "the flash image was refused: " + loaded);
			require(processor.hasDevice() && processor.getPlugin().getDevice() != before, "the Device was not made again");
			require(device(processor).kit().machine(0) == 32, "the new Device did not receive the set's Kit");
			require(parameter(processor, "Machine", 0).getUnnormalizedValue() == 32,
				"the parameters did not follow the new Device's Kit");
			const auto written = std::filesystem::u8path(processor.getPublicRomFolder()) / mdDrums::Engine::FlashImageName;
			std::error_code error;
			require(std::filesystem::file_size(written, error) == flash.size(),
				"the flash image was not written into the data folder");
			host.prepareToPlay(_rate, g_block);
			const auto hit = play(processor, 36, 0, _rate);
			std::printf("flash loader: \"%s\" refused (%s); the set's Kit kept by the stand-in and in a set saved there, "
				"then by the Device made from the image (Track 1 EFM-BD), written to %s; note 36 peak %.3f\n",
				wrong.filename().string().c_str(), refused.c_str(), written.parent_path().string().c_str(), hit.peak);
			require(hit.peak > 0.01f, "note 36 is silent after the flash image was loaded");
		}
	}

	// Note to sound: two seconds from one note to the next (Main below -100 dBFS in the block before each). A second
	// is not enough: the last note's tail, under the threshold, moves the next one's first sample at 48 kHz
	void latency(mdDrums::Processor& _processor, const double _rate)
	{
		// USR-16, the Machine parameter's last value (ticket 13 of the User Machines map): an empty User Machine's number,
		// which the Device takes as a host sets it
		automate(_processor, "Machine", 0, 209);
		require(device(_processor).kit().machine(0) == 209, "the Device did not take USR-16 from the Machine parameter");
		automate(_processor, "Machine", 0, 16);
		automate(_processor, "Level", 0, 100);
		uint32_t random = 0x16305eedu;
		int minimum = 1 << 30, maximum = 0;
		double sum = 0;
		for(int note = 0; note < 48; ++note)
		{
			random ^= random << 13; random ^= random >> 17; random ^= random << 5;
			const auto hit = play(_processor, 36, static_cast<int>(random % g_block), _rate);
			require(hit.before < g_silence, "Main was not silent in the block before a note: "
				+ std::to_string(hit.before));
			require(hit.first >= 0, "a note stayed silent");
			minimum = std::min(minimum, hit.first);
			maximum = std::max(maximum, hit.first);
			sum += hit.first;
		}
		std::printf("%.0f Hz: note to sound (TRXBD, 48 notes): %d-%d samples, mean %.1f (%.2f ms), jitter %d (%.2f ms),"
			" reported %d\n", _rate, minimum, maximum, sum / 48, sum / 48 * 1000.0 / _rate, maximum - minimum,
			(maximum - minimum) * 1000.0 / _rate, _processor.getLatencySamples());
	}
}

namespace
{
	void migration()
	{
		const auto root = std::filesystem::temp_directory_path()
			/ ("MoDuleMigration_" + juce::Uuid().toString().toStdString());
		const auto from = root / "MD Drums", to = root / "MoDule";
		const auto write = [](const std::filesystem::path& _file, const std::string& _text)
		{
			std::filesystem::create_directories(_file.parent_path());
			std::ofstream(_file, std::ios::binary) << _text;
		};
		const auto read = [](const std::filesystem::path& _file)
		{
			std::ifstream in(_file, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		};
		const auto flash = std::filesystem::path("roms") / mdDrums::Engine::FlashImageName;
		write(from / "Bank.syx", "the old Bank");
		write(from / flash, "the old flash image");
		require(mdDrums::Processor::migrateData(from, to), "the migration failed");
		require(read(to / "Bank.syx") == "the old Bank" && read(to / flash) == "the old flash image",
			"the Bank and the flash image did not come along");
		write(to / "Bank.syx", "the new Bank");
		require(mdDrums::Processor::migrateData(from, to) && read(to / "Bank.syx") == "the new Bank",
			"the migration overwrote the new Bank");
		std::error_code error;
		std::filesystem::remove_all(root, error);
		std::puts("migration: the Bank and the flash image come along once, nothing overwritten");
	}
}

namespace
{
	// A test file written by JUCE's _format: _seconds of a 441 Hz sine at half scale on _channels channels at _rate, 16 bits
	juce::File writeTone(juce::AudioFormat& _format, const juce::File& _file, const double _rate, const int _channels,
		const double _seconds, const juce::StringPairArray& _metadata = {})
	{
		_file.deleteFile();
		auto out = _file.createOutputStream();
		require(out != nullptr, "cannot write " + _file.getFullPathName().toStdString());
		std::unique_ptr<juce::AudioFormatWriter> writer(_format.createWriterFor(out.get(), _rate,
			static_cast<unsigned int>(_channels), 16, _metadata, 0));
		require(writer != nullptr, "no " + _format.getFormatName().toStdString() + " writer");
		out.release();	// the writer owns it
		const auto n = static_cast<int>(_rate * _seconds);
		juce::AudioBuffer<float> buffer(_channels, n);
		for(int c = 0; c < _channels; ++c)
			for(int i = 0; i < n; ++i)
				buffer.setSample(c, i, 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 441.0 * i / _rate)));
		require(writer->writeFromAudioSampleBuffer(buffer, 0, n), "the test file was not written");
		return _file;
	}

	// A file becomes a Sample (ticket 09 of the Samples map, as ticket 04 decided): WAV, AIFF and FLAC read the same, a
	// stereo file is the mean of its channels, a rate above 48 kHz comes down to 44.1, a WAV's smpl loop of 151 samples
	// or more ends the Sample, a shorter one is dropped; too slow, too long or no audio file refused with why. One
	// loaded into a Processor plays on ROM-33.
	void sampleFiles(const double _rate)
	{
		const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MoDuleSampleFilesTest");
		folder.deleteRecursively();
		require(folder.createDirectory().wasOk(), "no folder for the test's files");
		juce::WavAudioFormat wav;
		juce::AiffAudioFormat aiff;
		juce::FlacAudioFormat flac;
		const auto read = [](const juce::File& _file)
		{
			auto result = mdDrums::readSampleFile(_file);
			const auto* error = std::get_if<std::string>(&result);
			require(!error, _file.getFileName().toStdString() + " was not read: " + (error ? *error : std::string()));
			return std::get<mdDrums::SampleAudio>(result);
		};
		const auto peakOf = [](const std::vector<float>& _s)
		{
			float p = 0;
			for(const auto v : _s)
				p = std::max(p, std::abs(v));
			return p;
		};

		const auto mono = read(writeTone(wav, folder.getChildFile("mono.wav"), 44100.0, 1, 1.0));
		require(mono.rate == 44100.0 && mono.mono.size() == 44100 && mono.loopStart < 0, "a mono WAV did not read as it is");
		const auto stereo = read(writeTone(wav, folder.getChildFile("stereo.wav"), 22050.0, 2, 1.0));
		require(stereo.rate == 22050.0 && stereo.mono.size() == 22050 && std::abs(peakOf(stereo.mono) - 0.5f) < 0.01f,
			"a stereo WAV is not the mean of its channels at its own rate");
		const auto fast = read(writeTone(wav, folder.getChildFile("fast.wav"), 96000.0, 1, 1.0));
		require(fast.rate == 44100.0 && std::abs(static_cast<int>(fast.mono.size()) - 44100) <= 2
			&& std::abs(peakOf(fast.mono) - 0.5f) < 0.02f, "a 96 kHz WAV did not come down to 44.1 kHz");
		const auto fromAiff = read(writeTone(aiff, folder.getChildFile("mono.aif"), 44100.0, 1, 1.0));
		const auto fromFlac = read(writeTone(flac, folder.getChildFile("mono.flac"), 44100.0, 1, 1.0));
		float aiffOff = 0, flacOff = 0;
		for(size_t i = 0; i < mono.mono.size(); ++i)
		{
			aiffOff = std::max(aiffOff, std::abs(fromAiff.mono[i] - mono.mono[i]));
			flacOff = std::max(flacOff, std::abs(fromFlac.mono[i] - mono.mono[i]));
		}
		require(fromAiff.mono.size() == mono.mono.size() && aiffOff < 1e-4f, "an AIFF does not read as the same WAV");
		require(fromFlac.mono.size() == mono.mono.size() && flacOff < 1e-4f, "a FLAC does not read as the same WAV");

		juce::StringPairArray loop;
		loop.set("NumSampleLoops", "1");
		loop.set("Loop0Identifier", "0");
		loop.set("Loop0Type", "0");
		loop.set("Loop0Start", "1000");
		loop.set("Loop0End", "5000");
		const auto looped = read(writeTone(wav, folder.getChildFile("loop.wav"), 44100.0, 1, 1.0, loop));
		loop.set("Loop0End", "1100");
		const auto shortLoop = read(writeTone(wav, folder.getChildFile("short.wav"), 44100.0, 1, 1.0, loop));
		require(looped.loopStart == 1000 && looped.loopEnd == 5000, "a WAV's smpl loop was not read");

		// To Samples, with the factory bank's 12-bit table
		mdDrums::Processor processor(true);
		require(prepare(processor, _rate, g_block), "no Device for the Samples");
		const auto& codes = device(processor).sampleCodes();
		const auto room = device(processor).sampleRoom();
		const auto made = [&](const mdDrums::SampleAudio& _audio, const size_t _room)
		{
			return mdDrums::makeSample(_audio, codes, _room);
		};
		const auto sample = std::get<mdDrums::Sample>(made(mono, room));
		require(sample.length == 44100 && sample.words.size() == 22050 && sample.rate == mdDrums::Sample::Unity
			&& sample.loop == mdDrums::Sample::NoLoop, "a 44.1 kHz WAV is not a Sample of its samples at unity rate");
		const auto half = std::get<mdDrums::Sample>(made(stereo, room));
		require(half.rate == mdDrums::Sample::Unity / 2, "a 22.05 kHz Sample does not play at half the rate");
		const auto withLoop = std::get<mdDrums::Sample>(made(looped, room));
		const auto withoutLoop = std::get<mdDrums::Sample>(made(shortLoop, room));
		require(withLoop.loop == 1000 && withLoop.length == 5001, "a loop of 4,001 samples does not end the Sample");
		require(withoutLoop.loop == mdDrums::Sample::NoLoop && withoutLoop.length == 44100,
			"a loop of 101 samples was kept");
		const auto slow = made(read(writeTone(wav, folder.getChildFile("slow.wav"), 3000.0, 1, 1.0)), room);
		const auto* slowError = std::get_if<std::string>(&slow);
		const auto tooLong = made(mono, sample.words.size() - 1);
		const auto* longError = std::get_if<std::string>(&tooLong);
		std::printf("Sample files: a 3 kHz file \"%s\"; a 1 s file with 0.99 s of room \"%s\"\n",
			slowError ? slowError->c_str() : "", longError ? longError->c_str() : "");
		require(slowError && slowError->find("4 to 48 kHz") != std::string::npos, "a 3 kHz file was not refused");
		require(longError && longError->find("left") != std::string::npos, "a file longer than the room was not refused");
		const auto text = folder.getChildFile("notes.txt");
		require(text.replaceWithText("not audio"), "the test's text file was not written");
		const auto notAudio = mdDrums::readSampleFile(text);
		require(std::holds_alternative<std::string>(notAudio), "a text file read as audio");

		// One loaded into the Processor plays on ROM-33, silent before
		automate(processor, "Machine", 0, 176);
		automate(processor, "Level", 0, 100);
		const auto silent = play(processor, 36, 0, _rate);
		const auto error = processor.loadSampleFile(32, folder.getChildFile("mono.wav").getFullPathName().toStdString());
		require(error.empty(), "the Processor did not load a WAV onto ROM-33: " + error);
		const auto sounds = play(processor, 36, 0, _rate);
		std::printf("Sample files: WAV, AIFF and FLAC alike, stereo by mean, 96 kHz to 44.1, smpl loop of 4,001 kept and"
			" of 101 dropped; ROM-33 peaks %.4f before the WAV, %.4f after\n", silent.peak, sounds.peak);
		require(silent.peak < g_silence && sounds.peak > 0.01f, "ROM-33 does not play the WAV loaded into the Processor");
		folder.deleteRecursively();
	}

	// The user's Samples kept (ticket 10 of the Samples map): a WAV loaded into a Processor goes into its sample library
	// and plays on ROM-33, and the set carries it; a set opened where the library is empty plays its own copy and leaves
	// the library empty; a Kit of the Bank plays the library's Sample, which differs; taking it away plays the factory's
	// silence again; two instances hold two Samples on ROM-33; a set whose Kit plays no user's Sample carries none
	void keptSamples(const double _rate)
	{
		constexpr int rom = 32;	// ROM-33
		const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MoDuleKeptSamplesTest");
		folder.deleteRecursively();
		require(folder.createDirectory().wasOk(), "no folder for the test's files");
		juce::WavAudioFormat wav;
		const auto low = writeTone(wav, folder.getChildFile("low.wav"), 44100.0, 1, 1.0).getFullPathName().toStdString();
		const auto high = writeTone(wav, folder.getChildFile("high.wav"), 22050.0, 1, 1.0).getFullPathName().toStdString();
		const auto holds = [&](mdDrums::Processor& _processor, const char* _name)
		{
			const auto sample = device(_processor).sample(rom);
			return _name ? sample && sample->name == _name : !sample;
		};
		const auto onRom33 = [&](mdDrums::Processor& _processor)
		{
			automate(_processor, "Machine", 0, 176);
			automate(_processor, "Level", 0, 100);
			automate(_processor, "DEL", 0, 0);
			automate(_processor, "REV", 0, 0);
		};

		juce::MemoryBlock set;
		float played = 0;
		{
			mdDrums::Processor a(true);
			require(prepare(a, _rate, g_block), "no Device for the kept Samples");
			require(a.loadSampleFile(rom, low).empty(), "the WAV was not loaded onto ROM-33");
			require(a.getSampleLibrary()->get(rom) && holds(a, "low.wav"), "the library or the Device lacks ROM-33's Sample");
			onRom33(a);
			played = play(a, 36, 0, _rate).peak;
			require(played > 0.01f, "ROM-33 is silent with its Sample");
			static_cast<juce::AudioProcessor&>(a).getStateInformation(set);
		}
		{
			// A set opened where the library is empty: its own copy plays, the library stays empty
			const auto b = restoredFrom(set);
			require(prepare(*b, _rate, g_block), "no Device to open the set");
			require(!b->getSampleLibrary()->get(rom) && holds(*b, "low.wav"), "the set did not bring its Sample");
			require(play(*b, 36, 0, _rate).peak > 0.01f, "the set's Sample is silent where it is not kept");

			// A Kit of the Bank plays the library's Sample on ROM-33, which differs
			const auto audio = mdDrums::readSampleFile(juce::File(high));
			auto made = mdDrums::makeSample(std::get<mdDrums::SampleAudio>(audio), device(*b).sampleCodes(),
				device(*b).sampleRoom());
			auto other = std::get<mdDrums::Sample>(made);
			other.name = "high.wav";
			require(b->getSampleLibrary()->put(rom, std::make_shared<const mdDrums::Sample>(std::move(other))),
				"the library did not keep the other Sample");
			require(holds(*b, "low.wav"), "keeping a Sample in the library changed what the instance plays");
			b->controller().loadKit(device(*b).kit(), 0);
			block(*b);
			require(holds(*b, "high.wav"), "a Kit of the Bank did not take the library's Sample");

			// Taken away: the factory's silence on ROM-33, in the library too
			b->removeSample(rom);
			require(holds(*b, nullptr) && !b->getSampleLibrary()->get(rom), "the Sample was not taken away");
			const auto gone = play(*b, 36, 0, _rate).peak;
			require(gone < played / 100.0f, "ROM-33 still sounds once its Sample is taken away");
		}
		{
			// Two instances, two Samples on ROM-33: each its own
			mdDrums::Processor c(true), d(true);
			require(prepare(c, _rate, g_block) && prepare(d, _rate, g_block), "no Devices for two instances");
			require(c.loadSampleFile(rom, low).empty() && d.loadSampleFile(rom, high).empty(), "two Samples were not loaded");
			require(holds(c, "low.wav") && holds(d, "high.wav"), "two instances do not each hold their own ROM-33");

			// A set whose Kit plays no user's Sample carries none
			juce::MemoryBlock plain;
			static_cast<juce::AudioProcessor&>(c).getStateInformation(plain);
			const auto e = restoredFrom(plain);
			require(prepare(*e, _rate, g_block), "no Device to open the plain set");
			require(holds(*e, nullptr), "a set whose Kit plays no user's Sample carried one");
		}
		std::printf("kept Samples: a set brings its own (ROM-33 peaks %.4f), a Kit of the Bank takes the library's, two"
			" instances hold their own, taken away it is silent again\n", played);
		folder.deleteRecursively();
	}

	// User Machines (tickets 07 and 14 of the User Machines map): installed into a Processor's Library and its Device,
	// played on a Track; a set opened where none is installed plays its own copy; two instances hold two Machines at the
	// same number; a Kit of the Bank takes the Library's; a Track on a number the Device lacks takes the Library's
	void userMachines(const double _rate)
	{
		const auto saw = mdDrums::test::sawPackage();
		const auto other = mdDrums::test::sawPackage(60);
		constexpr int index = 4;	// USR-05
		const auto id = mdDrums::Engine::FirstUserMachine + index;
		const auto holds = [&](mdDrums::Processor& _processor, const std::vector<uint8_t>& _package)
		{
			const auto package = device(_processor).userMachine(index);
			return package && package->bytes == _package;
		};

		juce::MemoryBlock set;
		{
			mdDrums::Processor a(true);
			require(prepare(a, _rate, g_block), "no Device for the User Machines");
			require(a.installUserMachine(index, {0x01, 0x02}).size() > 0, "a file that is no User Machine was installed");
			require(a.installUserMachine(index, saw).empty(), "the test's User Machine was not installed");
			require(a.getLibrary()->get(index) && holds(a, saw), "the Library or the Device lacks USR-05");
			automate(a, "Machine", 0, id);
			automate(a, "Level", 0, 100);
			automate(a, "DEL", 0, 0);
			automate(a, "REV", 0, 0);
			const auto hit = play(a, 36, 0, _rate);
			require(hit.peak > 0.01f, "USR-05 is silent on Track 1");
			static_cast<juce::AudioProcessor&>(a).getStateInformation(set);
			std::printf("User Machine: USR-05 installed and played on Track 1, peak %.4f; the set holds %zu bytes\n",
				hit.peak, set.getSize());
		}
		{
			// A set opened where USR-05 is not installed: its own copy plays, the Library stays empty
			const auto b = restoredFrom(set);
			require(prepare(*b, _rate, g_block), "no Device to open the set");
			require(!b->getLibrary()->get(index) && holds(*b, saw), "the set did not bring its User Machine");
			require(device(*b).kit().machine(0) == id, "the set's Kit lost USR-05");
			require(play(*b, 36, 0, _rate).peak > 0.01f, "the set's User Machine is silent where it is not installed");

			// A Kit of the Bank plays the Library's USR-05, which differs
			require(b->getLibrary()->install(index, other).empty(), "the other User Machine was not installed");
			require(holds(*b, saw), "installing into the Library changed what the instance plays");
			b->controller().loadKit(device(*b).kit(), 0);
			block(*b);
			require(holds(*b, other), "a Kit of the Bank did not take the Library's User Machine");
		}
		{
			// Two instances, two Machines at USR-05: each its own
			mdDrums::Processor c(true), d(true);
			require(prepare(c, _rate, g_block) && prepare(d, _rate, g_block), "no Devices for two instances");
			require(c.installUserMachine(index, saw).empty() && d.installUserMachine(index, other).empty(),
				"two User Machines were not installed");
			require(holds(c, saw) && holds(d, other), "two instances do not each hold their own USR-05");

			// A Track chosen on a number the Device lacks takes the Library's
			require(d.getLibrary()->install(5, saw).empty(), "USR-06 was not installed into the Library");
			require(!device(d).userMachine(5), "the Device took USR-06 before a Track chose it");
			d.takeUserMachineIfAbsent(5);
			require(device(d).userMachine(5) && device(d).userMachine(5)->bytes == saw,
				"a Track on USR-06 did not take the Library's");
		}
		std::puts("User Machines: a set brings its own, two instances hold their own, a Kit of the Bank and a Track take"
			" the Library's");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	juce::ScopedJuceInitialiser_GUI juce;
	try
	{
		migration();
		for(const double rate : {44100.0, 48000.0})
		{
			mdDrums::Processor processor(true);
			if(!prepare(processor, rate, g_block))
			{
				std::cout << "mdDrumsProcessorTest: SKIP (no Machinedrum UW OS 1.63 flash image found)\n";
				return 77;
			}
			if(rate == 44100.0)
			{
				userMachines(rate);
				sampleFiles(rate);
				keptSamples(rate);
			}
			juce::AudioProcessor& host = processor;

			// The parameters, frozen from the port's first push: per Track Machine, 24, 5 LFO, Level, Mute, Solo, Out
			const auto& parameters = host.getParameters();
			const auto id = [&](const int _index)
			{
				return dynamic_cast<juce::AudioProcessorParameterWithID*>(parameters[_index])->paramID;
			};
			std::printf("%.0f Hz: %d parameters, first %s \"%s\", last %s \"%s\"\n", rate, parameters.size(),
				id(0).toRawUTF8(), parameters[0]->getName(64).toRawUTF8(), id(parameters.size() - 1).toRawUTF8(),
				parameters.getLast()->getName(64).toRawUTF8());
			require(parameters.size() == 608, "not 608 parameters");
			require(id(0) == "5_0_0" && parameters[0]->getName(64) == "T01 Machine",
				"track 1's machine is not the first parameter, named for Push");
			require(parameters[15 * 34 + 33]->getName(64) == "T16 Out", "Track 16's last parameter is not \"T16 Out\"");
			require(id(33) == "7_0_1" && id(34) == "5_1_0", "a Track does not hold 34 parameters ending with Out");
			require(id(544) == "8_0_0" && parameters[544]->getName(64) == "Echo TIME",
				"the master effects do not follow the Tracks");
			require(id(576) == "9_0_0" && parameters[576]->getName(64) == "Track"
				&& parameters[607]->getName(64) == "LFOM", "the relays for Push 2 do not follow the master effects");

			// The parameters start as the Device boots: factory Kit 1, TRX-B2 on track 1
			require(parameter(processor, "Machine", 0).getUnnormalizedValue() == 28
				&& parameter(processor, "Level", 0).getUnnormalizedValue() == device(processor).kit().levels[0],
				"the parameters do not start as the Device");
			// Track 1 without sends: no Echo or Gate Box tail on Main from one note to the next
			automate(processor, "DEL", 0, 0);
			automate(processor, "REV", 0, 0);

			const auto kick = play(processor, 36, 200, rate);
			const auto none = play(processor, 60, 200, rate);
			std::printf("%.0f Hz: note 36 at sample 200: first sound %d samples later, peak %.4f; note 60: peak %.4f\n",
				rate, kick.first, kick.peak, none.peak);
			require(kick.first >= 0 && kick.peak > 0.01f, "note 36 is silent");
			require(none.peak < g_silence, "a note outside 36-51 sounded");

			// The host automates track 1's machine: TRX-B2 to EFM-BD (id 32)
			const auto before = play(processor, 36, 0, rate);
			automate(processor, "Machine", 0, 32);
			const auto after = play(processor, 36, 0, rate);
			const auto syn1 = parameter(processor, "SYN1", 0).getCurrentValueAsText();
			std::printf("%.0f Hz: track 1 TRX-B2 energy %.3f, EFM-BD energy %.3f, SYN1 reads \"%s\"\n", rate,
				before.energy, after.energy, syn1.toRawUTF8());
			require(device(processor).kit().machine(0) == 32, "the machine did not reach the Device");
			require(std::abs(after.energy - before.energy) > before.energy * 0.05, "the machine parameter did nothing");
			require(syn1.startsWith("PTCH "), "SYN1 does not show its name on EFM-BD");
			require(parameter(processor, "SYN1", 0).getUnnormalizedValue() == device(processor).kit().parameters[0][0],
				"SYN1 does not follow the machine's default");

			// The editor plays a Track (Listen while choosing): a machine chosen, the Track played at once, sounds the
			// machine chosen, its note reaching the Device after the machine in the same block
			{
				const auto energy = play(processor, rate, {}, [&]
				{
					uiSets(parameter(processor, "Machine", 0), 28);
					processor.controller().audition(0, 100);
				}).energy;
				// The same machine at its defaults, struck by the host
				const auto host36 = play(processor, 36, 0, rate);
				std::printf("%.0f Hz: TRX-B2 chosen and played from the editor: energy %.3f; struck by the host %.3f, EFM-BD %.3f\n",
					rate, energy, host36.energy, after.energy);
				require(device(processor).kit().machine(0) == 28, "the machine chosen did not reach the Device");
				require(std::abs(energy - host36.energy) < host36.energy * 0.02, "the editor's note did not sound the machine chosen");
				automate(processor, "Machine", 0, 32);
			}

			// Out: track 1 leaves Main whether or not the host takes its bus
			automate(processor, "Out", 0, 1);
			const auto offMain = play(processor, 36, 0, rate);
			auto layout = host.getBusesLayout();
			layout.outputBuses.getReference(1) = juce::AudioChannelSet::mono();
			require(host.setBusesLayout(layout), "the host cannot enable Out 01");
			host.prepareToPlay(rate, g_block);
			const auto onOut = play(processor, 36, 0, rate);
			std::printf("%.0f Hz: track 1 on its Out: Main %.4f without the bus, Main %.4f and Out 01 %.4f with it\n", rate,
				offMain.peak, onOut.peak, onOut.outPeak);
			require(offMain.peak < g_silence && onOut.peak < g_silence && onOut.outPeak > 0.01f,
				"the Out parameter did not move track 1 off Main");
			layout.outputBuses.getReference(1) = juce::AudioChannelSet::disabled();
			require(host.setBusesLayout(layout), "the host cannot disable Out 01");
			host.prepareToPlay(rate, g_block);

			// The state: the Device's Kit and mixer, and the parameters following them
			automate(processor, "LfoShape1", 3, 5);
			automate(processor, "EqGAIN", 0, 99);
			juce::MemoryBlock state;
			host.getStateInformation(state);
			const auto restored = restoredFrom(state);
			require(device(*restored).kit() == device(processor).kit()
				&& device(*restored).mixer() == device(processor).mixer(),
				"the state did not bring back the Device's Kit and mixer");
			require(parameter(*restored, "Machine", 0).getUnnormalizedValue() == 32
				&& parameter(*restored, "Out", 0).getUnnormalizedValue() == 1
				&& parameter(*restored, "LfoShape1", 3).getUnnormalizedValue() == 5
				&& parameter(*restored, "EqGAIN", 0).getUnnormalizedValue() == 99,
				"the restored parameters do not follow the state");
			std::printf("%.0f Hz: state of %zu bytes restored, parameters following\n", rate, state.getSize());

			automate(processor, "Out", 0, 0);
			if(rate == 44100.0)
			{
				kits(processor);
				relays(processor);
			}
			latency(processor, rate);
		}
		flashLoader(44100.0);
		everyParameter();
		kitFile();
		std::cout << "mdDrumsProcessorTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdDrumsProcessorTest: " << _error.what() << '\n';
		return 1;
	}
}
