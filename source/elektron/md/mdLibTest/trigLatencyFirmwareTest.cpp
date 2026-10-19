// Note-to-sound latency of the Machinedrum: a host note reaches the firmware's MIDI input, the firmware
// triggers a track, and this measures the samples from the note's position in the host block to the first
// audible sample at the plug-in's output, at each plug-in latency setting. It also reports which notes
// sound, what the firmware sends back, and whether the velocity changes the level.

#include "firmwareBench.h"

#include "mdProtocol/mdkit.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace mdFirmwareBench;

	// The 128x64 LCD as text, two pixel rows per line.
	void printLcd(md::Hardware& _hardware)
	{
		md::FrontPanel panel;
		if(!_hardware.tryGetFrontPanelSnapshot(panel))
			return;
		const auto pixel = [&](const uint32_t _x, const uint32_t _y)
		{
			return (panel.getLcdVram(_x / 64, _y / 8, _x % 64) >> (_y % 8)) & 1;
		};
		for(uint32_t y = 0; y < 64; y += 2)
		{
			std::string line("      |");
			for(uint32_t x = 0; x < 128; ++x)
			{
				const auto top = pixel(x, y), bottom = pixel(x, y + 1);
				line += top && bottom ? '8' : top ? '\'' : bottom ? '.' : ' ';
			}
			std::printf("%s|\n", line.c_str());
		}
	}

	// From boot: when the firmware takes MIDI, when the factory flash is ready to cache, and when a note
	// first sounds, probing with one every 32 blocks (186 ms) if _probe.
	void startupTimeline(Bench& _bench, const bool _probe)
	{
		const auto seconds = [&] { return _bench.frame() / g_rate; };
		double midiReady = -1, cacheReady = -1, firstNote = -1;
		const auto wallStart = std::chrono::steady_clock::now();
		for(uint32_t block = 0; block < 172 * 60; ++block)
		{
			auto& hardware = _bench.hardware();
			if(midiReady < 0 && hardware.isFirmwareMidiReady())
				midiReady = seconds();
			if(cacheReady < 0 && hardware.isFactoryFlashCacheReady())
				cacheReady = seconds();
			if(block % 172 == 0)
				std::printf("    %.1f s: flash dirty %d, MIDI bytes taken %llu, queued %zu, LCD %016llx\n",
					seconds(), hardware.flashDirty(), static_cast<unsigned long long>(hardware.midiRxConsumedCount()),
					hardware.queuedMidiRxBytes(), static_cast<unsigned long long>(hardware.lcdPagesDigest(0, 7)));
			if(_probe && block % 344 == 0 && firstNote < 0 && std::getenv("MD_TRIG_LCD"))
				printLcd(hardware);
			if(_probe && midiReady >= 0 && firstNote < 0 && block % 32 == 0)
				_bench.note(0x90, 36, 100, 0);
			std::optional<uint32_t> first;
			_bench.process(1e-4f, first);
			if(first && firstNote < 0)
				firstNote = seconds();
			if(_probe && block % 32 == 16)
				_bench.note(0x80, 36, 0, 0);
			if((!_probe || firstNote >= 0) && cacheReady >= 0 && midiReady >= 0)
				break;
		}
		std::printf("  MIDI ready %.2f s, factory flash cacheable %.2f s, first note %.2f s (-1: never within 60 s)\n",
			midiReady, cacheReady, firstNote);
		const auto wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
		std::printf("  %.2f s of audio in %.2f s of wall clock (%.2fx real time)\n", seconds(), wall, seconds() / wall);
	}

	void startup(const char* _path, const std::string& _homePath)
	{
		std::printf("fresh flash, no notes:\n");
		{
			Bench bench(_path, 0);
			startupTimeline(bench, false);
			const auto cache = bench.hardware().copyFactoryFlashCache();
			require(!cache.empty(), "no factory flash cache to write");
			const auto nvram = _homePath + "/nvram/";
			baseLib::filesystem::createDirectory(nvram);
			require(baseLib::filesystem::writeFile(nvram + "md-uw-1.63-factory-v2.cache", cache),
				"cannot write the factory flash cache");
		}
		std::printf("fresh flash, a note every 186 ms:\n");
		{
			Bench bench(_path, 0);
			startupTimeline(bench, true);
		}
		std::printf("factory flash from the cache, a note every 186 ms:\n");
		{
			Bench bench(_path, 0, _homePath);
			startupTimeline(bench, true);
		}
	}

	struct Stats
	{
		uint64_t minimum = ~0ull, maximum = 0;
		double sum = 0;
		uint32_t count = 0;
		void add(const uint64_t _value)
		{
			minimum = std::min(minimum, _value);
			maximum = std::max(maximum, _value);
			sum += static_cast<double>(_value);
			++count;
		}
	};

	// One hit of each machine, one after the other on one booted machine, the n-th on track n + 2 so that no voice
	// has played before (see recordHit; the boot probes track 1), each silenced once recorded: writes, for each, its
	// id, its track (0-15), its sample count and a second of output A from the note on (three 32-bit unsigned, then
	// 32-bit floats; little-endian), the reference mdEngineFirmwareTest compares the engines with.
	void hits(const char* _path, const std::string& _file, const std::vector<uint16_t>& _machines)
	{
		Bench bench(_path, 0);
		const auto threshold = std::max(1e-4f, bootUntilNotesPlay(bench) * 8.0f);
		std::vector<uint8_t> bytes;
		const auto append = [&](const void* _data, const size_t _size)
		{
			const auto* data = static_cast<const uint8_t*>(_data);
			bytes.insert(bytes.end(), data, data + _size);
		};
		require(_machines.size() < 16, "more machines than fresh tracks");
		for(size_t i = 0; i < _machines.size(); ++i)
		{
			const auto track = static_cast<uint8_t>(i + 1);
			const auto channels = recordHit(bench, threshold, _machines[i], track);
			silenceHit(bench, track);
			const uint32_t id = _machines[i], trackIndex = track, count = static_cast<uint32_t>(channels[0].size());
			append(&id, sizeof(id));
			append(&trackIndex, sizeof(trackIndex));
			append(&count, sizeof(count));
			append(channels[0].data(), channels[0].size() * sizeof(float));
			for(size_t channel = 0; channel < channels.size(); ++channel)
			{
				float peak = 0;
				for(const auto value : channels[channel])
					peak = std::max(peak, std::abs(value));
				std::printf("machine %u output %zu peak %.4f\n", id, channel, peak);
			}
		}
		require(baseLib::filesystem::writeFile(_file, bytes), "cannot write the hits");
	}

	// The voice DSP's (DSP2's) P memory from _from to _to (exclusive) once the OS has booted and plays notes: what the
	// boot leaves there besides the program, among it the UW sample bank it copies from the flash (a directory at
	// $147e00, the sample data from $150000). Little-endian 32-bit words: the reference for the engine's own copy.
	void dsp2(const char* _path, const std::string& _file, const uint32_t _from, const uint32_t _to)
	{
		require(_from < _to, "empty DSP2 range");
		Bench bench(_path, 0);
		bootUntilNotesPlay(bench);
		const auto& memory = bench.hardware().getDspProducer().dsp().memory();
		std::vector<uint8_t> bytes;
		bytes.reserve(static_cast<size_t>(_to - _from) * 4);
		for(uint32_t address = _from; address < _to; ++address)
		{
			const uint32_t word = memory.get(dsp56k::MemArea_P, address);
			for(int shift = 0; shift < 32; shift += 8)
				bytes.push_back(static_cast<uint8_t>(word >> shift));
		}
		require(baseLib::filesystem::writeFile(_file, bytes), "cannot write the DSP2 dump");
		std::printf("DSP2 P $%06x-$%06x: %u words\n", _from, _to, _to - _from);
	}

	// Links and Chokes (trig and mute groups) as the firmware plays them, for comparison with another engine: on one
	// booted machine, the tracks of g_groupTracks take their machines and hitParameters, then each scenario of
	// g_groupScenarios plays its notes and records a second of outputs A to F from its first note on. Writes, per
	// scenario, its index, its sample count and the six outputs (32-bit unsigned, then 32-bit floats; little-endian).
	// The routing of each scenario sends the tracks it plays to their own outputs, out of the master effects.
	void groups(const char* _path, const std::string& _file)
	{
		using namespace md::automation;
		Bench bench(_path, 0);
		const auto threshold = std::max(1e-4f, bootUntilNotesPlay(bench) * 8.0f);
		std::vector<uint8_t> bytes;
		const auto append32 = [&](const uint32_t _value)
		{
			for(int shift = 0; shift < 32; shift += 8)
				bytes.push_back(static_cast<uint8_t>(_value >> shift));
		};
		const auto sendSysex = [&](const std::optional<sysex::Message>& _message)
		{
			require(_message.has_value(), "no SysEx for a group scenario");
			bench.sysex(std::vector<uint8_t>(_message->begin(), _message->end()));
		};
		const auto sendCc = [&](const ParameterChange& _change)
		{
			const auto cc = encodeParameterChange(md::MachineModel::Machinedrum, _change, 0);
			require(cc.has_value(), "no CC for a group scenario");
			bench.note((*cc)[0], (*cc)[1], (*cc)[2], 0);
		};
		const auto settle = [&](const uint32_t _blocks)
		{
			for(uint32_t block = 0; block < _blocks; ++block)
				bench.process();
		};

		// No LFO of Kit 1 reaches the tracks played: every LFO at zero depth (LFOD)
		for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
			sendCc({machinedrum::Routing, track, 6, 0});
		for(const auto& track : g_groupTracks)
		{
			sendSysex(sysex::assignMachine(md::MachineModel::Machinedrum, track.track, track.machine));
			settle(4);
			const auto parameters = hitParameters(track.machine);
			for(uint8_t parameter = 0; parameter < parameters.size(); ++parameter)
				sendCc({static_cast<uint8_t>(parameter / 8), track.track, static_cast<uint8_t>(parameter % 8),
					parameters[parameter]});
			sendCc({machinedrum::Level, track.track, 0, g_hitLevel});
		}
		settle(172 * 2);

		for(uint32_t index = 0; index < g_groupScenarios.size(); ++index)
		{
			const auto& scenario = g_groupScenarios[index];
			for(uint8_t output = 0; output < scenario.outputs.size(); ++output)
				if(scenario.outputs[output] != g_noTrack)
					sendSysex(sysex::trackRouting(scenario.outputs[output], static_cast<sysex::TrackOutput>(output)));
			for(const auto& group : scenario.links)
				sendSysex(sysex::linkChange(group[0], group[1]));
			for(const auto& group : scenario.chokes)
				sendSysex(sysex::chokeChange(group[0], group[1]));
			for(const auto track : scenario.muted)
				sendCc({machinedrum::Mute, track, 0, 1});
			uint32_t quiet = 0;
			for(uint32_t block = 0; block < 172 || quiet < 16; ++block)
			{
				require(block < 172 * 10, "the machine never fell silent");
				quiet = bench.process() < threshold ? quiet + 1 : 0;
			}

			std::vector<std::vector<float>> channels(6);
			const auto start = bench.frame();
			for(uint32_t block = 0; block < 172; ++block)
			{
				const auto frame = bench.frame() - start;
				for(const auto& note : scenario.notes)
					if(note.frame >= frame && note.frame < frame + g_block)
						bench.note(0x90, g_trackNotes[note.track], note.velocity, static_cast<uint32_t>(note.frame - frame));
				bench.process();
				for(size_t channel = 0; channel < channels.size(); ++channel)
					channels[channel].insert(channels[channel].end(), bench.outputs()[channel].begin(),
						bench.outputs()[channel].end());
			}
			for(const auto& note : scenario.notes)
				bench.note(0x80, g_trackNotes[note.track], 0, 0);
			// Back to the main outputs, so that what still rings stays out of the next scenario's outputs
			for(const auto track : scenario.outputs)
				if(track != g_noTrack)
					sendSysex(sysex::trackRouting(track, sysex::TrackOutput::Main));
			for(const auto track : scenario.muted)
				sendCc({machinedrum::Mute, track, 0, 0});
			for(const auto& group : scenario.links)
				sendSysex(sysex::linkChange(group[0], sysex::MdKit::Off));
			for(const auto& group : scenario.chokes)
				sendSysex(sysex::chokeChange(group[0], sysex::MdKit::Off));

			append32(index);
			append32(static_cast<uint32_t>(channels[0].size()));
			for(const auto& channel : channels)
			{
				const auto* data = reinterpret_cast<const uint8_t*>(channel.data());
				bytes.insert(bytes.end(), data, data + channel.size() * sizeof(float));
			}
			std::printf("scenario %u %s:", index, scenario.name);
			for(size_t channel = 0; channel < channels.size(); ++channel)
			{
				float peak = 0;
				for(const auto value : channels[channel])
					peak = std::max(peak, std::abs(value));
				std::printf(" %c %.4f", static_cast<char>('A' + channel), peak);
			}
			std::printf("\n");
		}
		require(baseLib::filesystem::writeFile(_file, bytes), "cannot write the group scenarios");
	}

	// The master effects as the firmware plays them (g_masterScenarios): on one booted machine, every track's LFO at zero
	// depth and its sends at 0, then for each scenario the 32 master bytes, a fresh track routed to Main with its machine,
	// hitParameters, the scenario's sends and g_hitLevel, silence, then its Hit and two seconds of Main. Writes, per
	// scenario: its index; the mixer DSP's memory of g_masterState before the Hit; and outputs A and B (32-bit unsigned
	// counts and words, then 32-bit floats; little-endian).
	void master(const char* _path, const std::string& _file)
	{
		using namespace md::automation;
		Bench bench(_path, 0);
		bootUntilNotesPlay(bench);
		std::vector<uint8_t> bytes;
		const auto append32 = [&](const uint32_t _value)
		{
			for(int shift = 0; shift < 32; shift += 8)
				bytes.push_back(static_cast<uint8_t>(_value >> shift));
		};
		const auto sendSysex = [&](const std::optional<sysex::Message>& _message)
		{
			require(_message.has_value(), "no SysEx for a master scenario");
			bench.sysex(std::vector<uint8_t>(_message->begin(), _message->end()));
		};
		const auto sendCc = [&](const ParameterChange& _change)
		{
			const auto cc = encodeParameterChange(md::MachineModel::Machinedrum, _change, 0);
			require(cc.has_value(), "no CC for a master scenario");
			bench.note((*cc)[0], (*cc)[1], (*cc)[2], 0);
		};
		auto& mixer = bench.hardware().getDspMixer().dsp().memory();

		for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
		{
			sendCc({machinedrum::Routing, track, 3, 0});	// DEL
			sendCc({machinedrum::Routing, track, 4, 0});	// REV
			sendCc({machinedrum::Routing, track, 6, 0});	// LFOD
		}
		// The Kit's blocks of 8 are Gate Box, Echo, EQ, Dynamix; the SysEx ids go Echo, Gate Box, EQ, Dynamix
		constexpr sysex::MasterEffect effects[4] = {sysex::MasterEffect::Reverb, sysex::MasterEffect::Echo,
			sysex::MasterEffect::Eq, sysex::MasterEffect::Dynamix};
		for(uint32_t index = 0; index < g_masterScenarios.size(); ++index)
		{
			const auto& scenario = g_masterScenarios[index];
			for(uint8_t k = 0; k < 32; ++k)
				sendSysex(sysex::masterEffectChange(effects[k / 8], k % 8, scenario.master[k]));
			sendSysex(sysex::trackRouting(scenario.track, sysex::TrackOutput::Main));
			sendSysex(sysex::assignMachine(md::MachineModel::Machinedrum, scenario.track, scenario.machine));
			for(uint32_t block = 0; block < 4; ++block)
				bench.process();
			auto parameters = hitParameters(scenario.machine);
			parameters[19] = scenario.del;
			parameters[20] = scenario.rev;
			for(uint8_t parameter = 0; parameter < parameters.size(); ++parameter)
				sendCc({static_cast<uint8_t>(parameter / 8), scenario.track, static_cast<uint8_t>(parameter % 8),
					parameters[parameter]});
			sendCc({machinedrum::Level, scenario.track, 0, g_hitLevel});
			// The OS's slew settles and the previous scenario's tails fall below -80 dB (the state taken below holds what
			// remains, and the Echo's glide)
			uint32_t quiet = 0;
			for(uint32_t block = 0; (block < 172 * 3 || quiet < 16) && block < 172 * 10; ++block)
				quiet = bench.process() < 1e-4f ? quiet + 1 : 0;
			std::printf("scenario %u %s: %s before the Hit\n", index, scenario.name, quiet >= 16 ? "quiet" : "NOT quiet");

			// The mixer DSP's state between two blocks: its thread waits for the output to drain wherever the codec's
			// frame falls, mid-block too, so only a state taken in its wait for the next block (P:$3c-$43) is whole, and
			// the same before and after a pause
			auto& mixerDsp = bench.hardware().getDspMixer().dsp();
			const auto waiting = [&]
			{
				const auto pc = mixerDsp.getPC().toWord();
				return pc >= 0x3c && pc <= 0x43;
			};
			const auto snapshot = [&]
			{
				std::vector<uint32_t> words;
				words.reserve(g_masterStateWords);
				for(const auto& range : g_masterState)
					for(auto address = range.from; address < range.to; ++address)
						words.push_back(mixer.get(range.y ? dsp56k::MemArea_Y : dsp56k::MemArea_X, address) & 0xffffff);
				return words;
			};
			std::vector<uint32_t> state;
			for(uint32_t attempt = 0; ; ++attempt)
			{
				require(attempt < 400, "the mixer DSP never held still between two blocks");
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
				if(waiting())
				{
					state = snapshot();
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
					if(waiting() && snapshot() == state)
					{
						std::printf("  mixer DSP state taken between two blocks, attempt %u\n", attempt);
						break;
					}
				}
				bench.process();
			}

			append32(index);
			append32(static_cast<uint32_t>(state.size()));
			for(const auto word : state)
				append32(word);

			bench.note(0x90, g_trackNotes[scenario.track], 100, 0);
			std::array<std::vector<float>, 2> channels;
			for(uint32_t block = 0; block < 172 * 2; ++block)
			{
				bench.process();
				for(size_t channel = 0; channel < channels.size(); ++channel)
					channels[channel].insert(channels[channel].end(), bench.outputs()[channel].begin(),
						bench.outputs()[channel].end());
			}
			bench.note(0x80, g_trackNotes[scenario.track], 0, 0);
			sendCc({machinedrum::Level, scenario.track, 0, 0});
			append32(static_cast<uint32_t>(channels[0].size()));
			for(const auto& channel : channels)
			{
				const auto* data = reinterpret_cast<const uint8_t*>(channel.data());
				bytes.insert(bytes.end(), data, data + channel.size() * sizeof(float));
			}
			float peak = 0;
			for(const auto& channel : channels)
				for(const auto value : channel)
					peak = std::max(peak, std::abs(value));
			std::printf("  Main peak %.4f\n", peak);
		}
		require(baseLib::filesystem::writeFile(_file, bytes), "cannot write the master scenarios");
	}

	void run(const char* _path)
	{
		// Which notes on the base channel (channel 1) trigger a track, and what the firmware sends back.
		{
			Bench bench(_path, 0);
			const auto floor = bootUntilNotesPlay(bench);
			const auto threshold = std::max(1e-4f, floor * 8.0f);
			std::printf("idle floor %g, threshold %g\n", floor, threshold);
			for(uint8_t note = 34; note < 64; ++note)
			{
				const auto hit = play(bench, threshold, 0x90, note, 100, 0);
				std::printf("note %3u: %s", note, hit.sounded ? "sound" : "silent");
				if(hit.sounded)
					std::printf(" peak %.4f latency %llu", hit.peak, static_cast<unsigned long long>(hit.latency));
				for(const auto& event : bench.midiSeen())
					if(event.sysex.empty())
						std::printf(" out[%02x %u %u]", event.a, event.b, event.c);
				bench.midiSeen().clear();
				std::printf("\n");
			}
		}

		// Latency and jitter at each latency setting, notes at varied positions in the block.
		for(const uint32_t blocks : {0u, 1u, 2u, 4u})
		{
			Bench bench(_path, blocks);
			const auto floor = bootUntilNotesPlay(bench);
			const auto threshold = std::max(1e-4f, floor * 8.0f);
			Stats stats;
			uint32_t silent = 0;
			uint32_t random = 0x16305eedu;
			for(uint32_t trial = 0; trial < 48; ++trial)
			{
				random ^= random << 13; random ^= random >> 17; random ^= random << 5;
				const auto offset = random % g_block;
				const auto hit = play(bench, threshold, 0x90, 36, 100, offset);
				if(hit.sounded)
					stats.add(hit.latency);
				else
					++silent;
			}
			const auto reported = bench.reportedLatency();
			std::printf("latencyBlocks %u (reported %u): %u hits, %u silent, latency min %llu max %llu mean %.1f "
				"samples, beyond reported min %lld (%.2f ms), jitter %llu (%.2f ms)\n",
				blocks, reported, stats.count, silent,
				static_cast<unsigned long long>(stats.minimum), static_cast<unsigned long long>(stats.maximum),
				stats.count ? stats.sum / stats.count : 0.0,
				static_cast<long long>(stats.minimum) - reported,
				(static_cast<double>(stats.minimum) - reported) * 1000.0 / g_rate,
				static_cast<unsigned long long>(stats.maximum - stats.minimum),
				static_cast<double>(stats.maximum - stats.minimum) * 1000.0 / g_rate);
		}

		// Velocity.
		{
			Bench bench(_path, 0);
			const auto floor = bootUntilNotesPlay(bench);
			const auto threshold = std::max(1e-4f, floor * 8.0f);
			for(const uint8_t velocity : {127, 100, 64, 32, 8})
			{
				const auto hit = play(bench, threshold, 0x90, 36, velocity, 0);
				std::printf("velocity %3u: peak %.4f\n", velocity, hit.peak);
			}
		}
	}

	// Slides as OS 1.63's own sequencer plays them (.scratch/slide/research-01-os-slide.md; MD Drums ports them, ADR
	// 0003 amended 2026-10-19): on a booted machine in EXTENDED mode at its 125 BPM, Pattern A01 rewritten in RAM for
	// each scenario to hold track 1's trigs, its Locks on FLTF and the Slide row, then played from the front panel
	// twice through (a sequencer that starts sets up its first step's Locks and slides at none). Track 1's FLTF
	// smoothing target ($1000ddc + 12), read after every block, takes the stairs the research derived: the slide step's
	// Lock, then (end - start) x 128 / clocks, truncated toward zero, added at each 24-ppqn clock, each sum >> 7, a
	// clock (882 samples) apart; then the next trig's value, its Lock or the Kit's. The trig's own clock steps the
	// slide or skips it as the control tick's phase falls (MD Drums always skips it, HostModel::clock). Stopped
	// mid-slide, it stands still. mdDrumsEngineTest checks MD Drums' port against the same ramp.
	void slides(const char* _path)
	{
		using namespace md::automation;
		const auto check = [](const bool _condition, const std::string& _message)
		{
			require(_condition, _message.c_str());
		};
		constexpr uint32_t target = 0x01000ddc + 12;	// track 1's FLTF in the smoothing targets
		constexpr uint32_t tempoWord = 0x0100150c;		// BPM x 24
		constexpr uint8_t kitValue = 64;
		Bench bench(_path, 0);
		bootUntilNotesPlay(bench);
		auto& hardware = bench.hardware();
		auto& uc = hardware.getUC();
		const auto settle = [&](const uint32_t _blocks)
		{
			for(uint32_t block = 0; block < _blocks; ++block)
				bench.process();
		};
		const auto tap = [&](const md::PanelControl _control)
		{
			const auto packet = md::panelPacket(md::MachineModel::Machinedrum, _control);
			require(packet.has_value(), "unknown panel control");
			require(hardware.trySendPanelEvent(packet->row, packet->mask), "panel press rejected");
			settle(8);
			require(hardware.trySendPanelEvent(packet->row, 0), "panel release rejected");
			settle(25);
		};
		const auto cc = encodeParameterChange(md::MachineModel::Machinedrum, {machinedrum::Effects, 0, 4, kitValue}, 0);
		require(cc.has_value(), "no CC for FLTF");
		bench.note((*cc)[0], (*cc)[1], (*cc)[2], 0);
		settle(8);
		const auto extended = [&]
		{
			return hardware.getFrontPanelSnapshot().getModeLed(md::FrontPanel::ModeLed::Extended);
		};
		if(!extended())
			tap(md::PanelControl::ClassicExtended);
		require(extended(), "the machine is not in EXTENDED mode");
		const uint32_t tempo = static_cast<uint32_t>(uc.read8(tempoWord)) << 24 | uc.read8(tempoWord + 1) << 16
			| uc.read8(tempoWord + 2) << 8 | uc.read8(tempoWord + 3);
		require(tempo == 125 * 24, "the machine is not at 125 BPM");
		require(uc.read8(target) == kitValue, "the Kit's FLTF did not reach the smoothing target");

		const auto layout = hardware.livePatternLayout();
		require(layout.has_value(), "no pattern layout for this OS");
		const auto main = layout->mainOf(0), tail = layout->tailOf(0);
		require(uc.read8(main + 146) == 0, "Pattern A01 does not play at 1x");

		struct Scenario
		{
			const char* name;
			std::vector<uint8_t> trigs;		// track 1's steps (0-31)
			std::vector<std::pair<uint8_t, uint8_t>> locks;	// step, FLTF
			uint8_t slideStep;
			int start, end, clocks;			// the ramp the research derives
			bool stop;						// STOP after three stairs
		};
		const std::vector<Scenario> scenarios{
			{"one step from 0 to 127", {0, 1}, {{0, 0}, {1, 127}}, 0, 0, 127, 6, false},
			{"to the Kit's value", {0, 1}, {{0, 0}}, 0, 0, kitValue, 6, false},
			{"two steps from 0 to 127", {0, 2}, {{0, 0}, {2, 127}}, 0, 0, 127, 12, false},
			{"one step from 100 to 20", {0, 1}, {{0, 100}, {1, 20}}, 0, 100, 20, 6, false},
			{"stopped", {0, 2}, {{0, 0}, {2, 127}}, 0, 0, 127, 12, true},
		};
		const auto ramp = [](const int _start, const int _end, const int _over, const int _clocks)
		{
			const int increment = (_end - _start) * 128 / _over;
			int current = _start * 128;
			for(int c = 0; c < _clocks && increment; ++c)
			{
				const int next = current + increment;
				if(increment > 0 ? next > _end * 128 : next < _end * 128)
					break;
				current = next;
			}
			return current >> 7;
		};

		for(const auto& scenario : scenarios)
		{
			// Track 1 alone: its trigs, one lock row (FLTF), the Slide row for every track (EDIT ALL); no other trig,
			// lock, accent or swing
			std::vector<md::RamWrite> writes;
			const auto put32 = [&](const uint32_t _address, const uint32_t _value)
			{
				for(uint32_t i = 0; i < 4; ++i)
					writes.push_back({_address + i, static_cast<uint8_t>(_value >> (24 - 8 * i))});
			};
			uint32_t trigs = 0;
			for(const auto step : scenario.trigs)
				trigs |= 1u << step;
			for(uint32_t track = 0; track < 16; ++track)
			{
				put32(main + 4 * track, track ? 0 : trigs);
				put32(main + 64 + 4 * track, track ? 0 : 1u << 12);
			}
			put32(main + 128, 0);
			put32(main + 132, 1u << scenario.slideStep);
			put32(main + 136, 0);
			writes.push_back({main + 149, 1});
			for(uint32_t step = 0; step < 32; ++step)
				writes.push_back({main + 150 + step, 0xff});
			for(const auto& [step, value] : scenario.locks)
				writes.push_back({main + 150 + step, value});
			put32(tail + 0, 0);
			put32(tail + 4, 1);
			put32(tail + 8, 0);
			require(hardware.writeRamIfUnchanged(writes), "the pattern's bytes were refused");
			settle(16);

			// Played from step 1, twice through: a sequencer that starts sets up its first step's Locks and slides at
			// none, so the second time through plays them. Every change of the target there, its value and its frame.
			const auto play = md::panelPacket(md::MachineModel::Machinedrum, md::PanelControl::Play);
			require(play.has_value() && hardware.trySendPanelEvent(play->row, play->mask), "PLAY rejected");
			const auto loopClocks = 6 * static_cast<int>(uc.read8(main + 145));
			std::vector<std::pair<uint64_t, int>> changes;
			int last = uc.read8(target);
			const auto blocks = static_cast<uint32_t>((loopClocks + scenario.clocks + 8) * 882 / g_block + 8);
			size_t from = SIZE_MAX;
			const auto startFrame = bench.frame();
			bool held = false;
			for(uint32_t block = 0; block < blocks; ++block)
			{
				if(block == 8)
					require(hardware.trySendPanelEvent(play->row, 0), "PLAY's release rejected");
				bench.process();
				const int value = uc.read8(target);
				if(value != last)
				{
					changes.emplace_back(bench.frame(), value);
					// The second time through starts at the slide step's first change
					const auto secondTime = startFrame + static_cast<uint64_t>((loopClocks - 3) * 882);
					if(from == SIZE_MAX && bench.frame() > secondTime)
						from = changes.size() - 1;
				}
				last = value;
				if(scenario.stop && from != SIZE_MAX && changes.size() == from + 3)
				{
					tap(md::PanelControl::Stop);
					const auto stoppedAt = uc.read8(target);
					settle(172);
					check(uc.read8(target) == stoppedAt,
						std::string(scenario.name) + ": the slide moved while stopped");
					std::printf("slide %s: held at %d for a second after STOP\n", scenario.name, stoppedAt);
					held = true;
					break;
				}
			}
			if(!scenario.stop)
				tap(md::PanelControl::Stop);
			settle(64);

			std::string seen;
			for(size_t i = 0; i < changes.size(); ++i)
				seen += (i == from ? " |" : " ") + std::to_string(changes[i].second);
			std::printf("slide %s:%s\n", scenario.name, seen.c_str());
			check(from != SIZE_MAX, std::string(scenario.name) + ": nothing changed the second time through");
			if(scenario.stop)
			{
				check(held, std::string(scenario.name) + ": never stopped mid-slide");
				continue;
			}
			// The Lock, the clocks' stairs, then the next trig's value. The trig's own clock steps the slide too when
			// the control tick arms it between the clock handler's trigs and its update (the update skips a track whose
			// trig waits for the tick, $2ae8c2), which the tick's phase decides: the Lock and its first stair then
			// change the target within one block, and the slide takes a stair more before the next trig
			const auto stairs = [&](const int _first)
			{
				std::vector<int> values;
				for(int c = _first; c < scenario.clocks + _first; ++c)
				{
					const auto value = ramp(scenario.start, scenario.end, scenario.clocks, c);
					if(values.empty() || value != values.back())
						values.push_back(value);
				}
				if(values.back() != scenario.end)
					values.push_back(scenario.end);
				return values;
			};
			std::vector<int> observed;
			for(auto i = from; i < changes.size(); ++i)
			{
				observed.push_back(changes[i].second);
				if(changes[i].second == scenario.end)
					break;
			}
			const auto skipped = stairs(0), stepped = stairs(1);
			const bool tickBefore = observed == stepped;
			check(observed == skipped || tickBefore,
				std::string(scenario.name) + ": the stairs are not the research's ramp");
			// A clock apart, within the bench's blocks, but the last: the next trig's value comes at the control tick
			// after the trig
			std::string gaps;
			for(size_t i = 1; i < observed.size(); ++i)
			{
				const auto gap = static_cast<int64_t>(changes[from + i].first - changes[from + i - 1].first);
				gaps += " " + std::to_string(gap);
				check(i + 1 == observed.size() || std::abs(gap - 882) <= static_cast<int64_t>(g_block),
					std::string(scenario.name) + ": stair " + std::to_string(i) + " came " + std::to_string(gap)
						+ " samples after the last");
			}
			std::printf("slide %s: the trig's own clock %s; samples between stairs%s\n", scenario.name,
				tickBefore ? "stepped it" : "skipped it", gaps.c_str());
		}
	}
}

int main(int argc, char** argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto* const path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	if(!path || !*path)
	{
		std::cout << "mdTrigLatencyFirmwareTest: SKIP (GEARMULATOR_MD_FIRMWARE_BIN not set)\n";
		return 77;
	}
	try
	{
		// --startup <folder>: boot timeline only, with a factory flash cache written to that folder.
		if(argc > 2 && std::string(argv[1]) == "--startup")
			startup(path, argv[2]);
		// --hits <file> <machine id>...: one hit of each machine on track 1, for comparison with another engine.
		else if(argc > 3 && std::string(argv[1]) == "--hits")
		{
			std::vector<uint16_t> machines;
			for(int i = 3; i < argc; ++i)
				machines.push_back(static_cast<uint16_t>(std::atoi(argv[i])));
			hits(path, argv[2], machines);
		}
		// --groups <file>: the Link and Choke scenarios of g_groupScenarios, for comparison with another engine.
		else if(argc > 2 && std::string(argv[1]) == "--groups")
			groups(path, argv[2]);
		// --master <file>: the master effect scenarios of g_masterScenarios, for comparison with another engine.
		else if(argc > 2 && std::string(argv[1]) == "--master")
			master(path, argv[2]);
		// --dsp2 <file> <from> <to>: DSP2's P memory after the boot, addresses in hexadecimal.
		else if(argc > 4 && std::string(argv[1]) == "--dsp2")
			dsp2(path, argv[2], static_cast<uint32_t>(std::stoul(argv[3], nullptr, 16)),
				static_cast<uint32_t>(std::stoul(argv[4], nullptr, 16)));
		// --slides: OS 1.63's own sequencer sliding Locks, against the ramp MD Drums ports.
		else if(argc > 1 && std::string(argv[1]) == "--slides")
			slides(path);
		else
			run(path);
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdTrigLatencyFirmwareTest: " << _error.what() << '\n';
		return 1;
	}
}
