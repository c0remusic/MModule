// mdDrums::Engine as MD Drums drives it: it finds the flash image, plays a trigger within two 32-sample blocks,
// scales the velocity, plays the UW bank's samples, and renders all 16 tracks playing and the master effects within
// 30 % of real time. It reports the note-to-sound latency and its jitter, sample-accurate (ticket 19) and not, mixes
// and runs the master effects as the engine's own mixer and mixer DSP do, retriggers without a click of its own, and
// plays Links, Chokes, Mute and slides as the Machinedrum does, and User Machines as MDX X.20 does.

#include "mdDrumsEngine.h"
#include "mdDrumsHitPreview.h"
#include "mdDrumsMessages.h"

#include "Firmware.h"
#include "MdEngine.h"
#include "MdsHost.h"
#include "MdsPackage.h"

#include "mdProtocol/mdkitchange.h"

#include "mdDrumsTestMachine.h"
#include "mdDrumsTestSupport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	using mdDrums::test::differences;
	using mdDrums::test::machineId;
	using mdDrums::test::require;

	// Main after a Hit: the master effects' EQ and Dynamix keep a tail of a few LSB, as the Machinedrum's do, so Main is
	// silent below -100 dBFS rather than at 0
	constexpr float g_silence = 1e-5f;

	// Samples from now to the first one at or above -60 dBFS, and the peak, over a second.
	std::pair<int, float> listen(mdDrums::Engine& _engine)
	{
		std::vector<float> left(44100), right(44100);
		_engine.render(left.data(), right.data(), left.size());
		int first = -1;
		float peak = 0;
		for(size_t i = 0; i < left.size(); ++i)
		{
			const auto value = std::max(std::abs(left[i]), std::abs(right[i]));
			if(first < 0 && value >= 0.001f)
				first = static_cast<int>(i);
			peak = std::max(peak, value);
		}
		return {first, peak};
	}

	void settle(mdDrums::Engine& _engine)
	{
		std::vector<float> left(44100 * 2), right(44100 * 2);
		_engine.render(left.data(), right.data(), left.size());
	}

	// Note to sound: 48 notes at pseudo-random samples of the host's blocks (256), each the samples from the note to
	// the first one that is not 0, as the plug-in renders up to a note's offset and triggers there. Returns the jitter.
	int latency(mdDrums::Engine& _engine)
	{
		constexpr size_t hostBlock = 256;
		// The rest of the engine's block (or the constant 31), then TRX-BD's voice, which sounds a block after its
		// trigger, from its 5th sample, then Dynamix's look-ahead on Main
		constexpr int longest = mdDrums::Engine::BlockSize * 2 + 5 + mdDrums::Engine::MasterDelay;
		// From the note on, host blocks up to past that
		constexpr size_t reach = longest + hostBlock;
		std::vector<float> left(reach + hostBlock), right(reach + hostBlock);
		const auto sounds = [&](const size_t _i)
		{
			return std::abs(left[_i]) > g_silence || std::abs(right[_i]) > g_silence;
		};
		uint32_t random = 0x16305eedu;
		int minimum = 1 << 30, maximum = 0;
		double sum = 0;
		for(int note = 0; note < 48; ++note)
		{
			// Two seconds between the notes: a shorter silence leaves the last note's tail just under the threshold,
			// where it moves the next note's first sample
			settle(_engine);
			const auto offset = mdDrums::test::xorshift(random) % hostBlock;
			_engine.render(left.data(), right.data(), offset);
			_engine.trigger(0, 100);
			// Then the host's blocks from the note on, the rest of this one, then whole ones, until it sounds
			int first = -1;
			for(size_t done = 0, count = hostBlock - offset; first < 0 && done < reach;
				done += count, count = hostBlock)
			{
				_engine.render(left.data() + done, right.data() + done, count);
				for(auto i = done; i < done + count && first < 0; ++i)
					if(sounds(i))
						first = static_cast<int>(i);
			}
			require(first >= 0, "a note stayed silent");
			minimum = std::min(minimum, first);
			maximum = std::max(maximum, first);
			sum += first;
		}
		std::printf("note to sound (TRXBD, 48 notes, %s): %d-%d samples, mean %.1f (%.2f ms), jitter %d (%.2f ms)\n",
			_engine.isSampleAccurate() ? "sample-accurate" : "on the engine's blocks", minimum, maximum, sum / 48,
			sum / 48 * 1000.0 / 44100.0, maximum - minimum, (maximum - minimum) * 1000.0 / 44100.0);
		require(maximum <= longest, "a note took more than two blocks to sound");
		return maximum - minimum;
	}

	std::vector<int32_t> toSamples(const std::vector<float>& _values)
	{
		std::vector<int32_t> samples(_values.size());
		for(size_t i = 0; i < _values.size(); ++i)
			samples[i] = static_cast<int32_t>(std::lround(_values[i] * 8388608.0f));
		return samples;
	}

	// The engine's own mixer (md::engine::Engine, as mdEngineFirmwareTest compares it with the firmware) against
	// mdDrums::Engine with every Hit on an engine block and every Track on a machine whose voice is held back as long
	// (the default ones, TRX-SD for those that start later), so that every Track waits as long: the same samples, at
	// once when not sample-accurate, SampleAccurateDelay and the hold later when it is. The master effects run on the engine's buses as
	// they are, and on the same buses SampleAccurateDelay later, each with the OS's words of every tick. The two
	// mdDrums::Engines come from the flash image, their machines and master compiled ahead, against an engine and
	// master effects that were not: the warm-up leaves the samples as they are.
	void equivalence(const std::vector<uint8_t>& _flash, const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		const auto machine = [](const int _track)
		{
			const auto m = mdDrums::Engine::defaultMachine(_track);
			return mdDrums::Engine::voiceStart(m) == mdDrums::Engine::voiceStart(17) ? m : uint8_t{17};	// TRX-SD
		};
		md::engine::Engine raw(_os.firmware, _os.osImage);
		raw.loadRomBank(_bank);
		md::engine::MasterEngine onBlocks(_os.firmware), late(_os.firmware);
		mdDrums::applyEngineDefaults(raw.host());
		mdDrums::Engine accurate(_flash), blocks(_flash);
		blocks.setSampleAccurate(false);
		for(int track = 0; track < mdDrums::Engine::TrackCount; ++track)
		{
			raw.host().setMachine(track, machine(track));
			accurate.setMachine(track, machine(track));
			blocks.setMachine(track, machine(track));
		}
		accurate.setParam(4, 18, 20);	// a panned Track, and one with a send
		blocks.setParam(4, 18, 20);
		raw.host().setParam(4, 18, 20);
		accurate.setParam(7, 19, 90);
		blocks.setParam(7, 19, 90);
		raw.host().setParam(7, 19, 90);

		constexpr int blockCount = 600;
		constexpr size_t block = mdDrums::Engine::BlockSize;
		const auto shift = static_cast<size_t>(mdDrums::Engine::SampleAccurateDelay + mdDrums::Engine::AlignedStart
			- mdDrums::Engine::voiceStart(17));
		std::vector<int32_t> rawLeft, rawRight, lateLeft, lateRight;
		std::vector<float> accLeft(blockCount * block), accRight(blockCount * block), blkLeft(accLeft.size()), blkRight(accLeft.size());
		md::engine::Engine::Output out;
		std::vector<std::array<std::array<int32_t, 2>, 3>> buses(shift);	// the buses' last frames, shift of them, oldest first
		for(int b = 0; b < blockCount; ++b)
		{
			if(b % 3 == 0)
			{
				const auto track = (b / 3) % mdDrums::Engine::TrackCount;
				const auto velocity = 40 + (b * 7) % 88;
				raw.host().trigger(track, velocity);
				accurate.trigger(track, velocity);
				blocks.trigger(track, velocity);
			}
			require(raw.render(out), "the engine faulted");
			md::engine::HostModel<>::MasterWords words;
			if(raw.host().takeMasterWords(words))
			{
				onBlocks.setWords(words.y, words.words.data(), words.count);
				late.setWords(words.y, words.words.data(), words.count);
			}
			md::engine::Mixer::Stereo main{}, lateDry{}, lateRev{}, lateDel{};
			require(onBlocks.process(out.mix.main, out.mix.rev, out.mix.del, main), "the master effects faulted");
			for(size_t i = 0; i < block; ++i)
			{
				buses.push_back({out.mix.main[i], out.mix.rev[i], out.mix.del[i]});
				lateDry[i] = buses.front()[0];
				lateRev[i] = buses.front()[1];
				lateDel[i] = buses.front()[2];
				buses.erase(buses.begin());
			}
			md::engine::Mixer::Stereo lateMain{};
			require(late.process(lateDry, lateRev, lateDel, lateMain), "the master effects faulted");
			for(size_t i = 0; i < block; ++i)
			{
				rawLeft.push_back(main[i][0]);
				rawRight.push_back(main[i][1]);
				lateLeft.push_back(lateMain[i][0]);
				lateRight.push_back(lateMain[i][1]);
			}
			accurate.render(accLeft.data() + b * block, accRight.data() + b * block, block);
			blocks.render(blkLeft.data() + b * block, blkRight.data() + b * block, block);
		}
		const auto blockDiffs = differences(toSamples(blkLeft), rawLeft) + differences(toSamples(blkRight), rawRight);
		const auto accurateDiffs = differences(toSamples(accLeft), lateLeft)
			+ differences(toSamples(accRight), lateRight);
		std::printf("Main against the engine's own mixer and master effects, 200 Hits on its blocks, %zu samples: %zu differ"
			" on the engine's blocks, %zu sample-accurate (buses %zu samples later)\n", rawLeft.size(), blockDiffs,
			accurateDiffs, shift);
		require(blockDiffs == 0 && accurateDiffs == 0, "the delayed mix is not the engine's mix");
	}

	// The tables ASSIGN MACHINE copies (md::automation::sysex::applyKitChange), as OS 1.63 holds them ($2052fe)
	void osTables(const md::fw::FlashOs& _os)
	{
		namespace os = md::automation::sysex::os163;
		constexpr uint32_t base = 0x200000;	// the OS image's first address
		const auto same = [&](const uint32_t _address, const auto& _table, const char* _name)
		{
			require(_address >= base && _address - base + _table.size() <= _os.osImage.size(),
				std::string(_name) + " lies past the OS image");
			const auto* image = _os.osImage.data() + (_address - base);
			require(std::equal(_table.begin(), _table.end(), image), std::string(_name) + " is not the OS's");
		};
		same(os::EffectsAddress, os::Effects, "Effects");
		same(os::EffectsAddress + 8, os::Routing, "Routing");
		same(os::MidEffectsAddress, os::MidEffects, "MidEffects");
		same(os::MidEffectsAddress + 8, os::MidRouting, "MidRouting");
		same(os::CtrAlAddress, os::CtrAl, "CtrAl");
		same(os::Ctr8pAddress, os::Ctr8pEffects, "Ctr8pEffects");
		same(os::Ctr8pAddress + 8, os::Ctr8pRouting, "Ctr8pRouting");
		same(os::Ctr8pAddress + 16, os::Ctr8pTargets, "Ctr8pTargets");
		same(os::CtrBlocksAddress, os::CtrBlocks, "CtrBlocks");
		std::printf("ASSIGN MACHINE's tables are OS 1.63's\n");
	}

	// The ColdFire that runs OS 1.63's routines (ticket 11 of the User Machines map): a byte through -(A7) or (A7)+ moves
	// its stack pointer by one, where the 68000 moves it by two to keep it word aligned. GCC for ColdFire pushes a char
	// argument with a byte push then subq.l #3,sp; MDX X.20's OS crashed on the 68000's rule.
	void coldFireBytePush(const md::fw::FlashOs& _os)
	{
		md::engine::MachineRunner runner(_os.osImage);
		// d1 = sp; move.b d1,-(sp); d2 = sp; d1 -= d2; move.b (sp)+,d0; d0 = sp - d2; d0 = d0 << 8 | d1; rts
		constexpr uint16_t code[] = {0x220f, 0x1f01, 0x240f, 0x9282, 0x101f, 0x200f, 0x9082, 0xe188, 0x8081, 0x4e75};
		constexpr uint32_t at = 0x0100e200;	// the runner's scratch area in internal SRAM, which no OS routine uses
		for(uint32_t i = 0; i < std::size(code); ++i)
			runner.poke16(at + 2 * i, code[i]);
		const auto steps = runner.call(at, {});
		std::printf("ColdFire: a byte pushed moves the stack pointer by %lld, a byte popped by %lld\n",
			static_cast<long long>(steps & 0xff), static_cast<long long>(steps >> 8 & 0xff));
		require(steps == 0x0101, "a byte through the ColdFire's stack pointer does not move it by one");
	}

	// Where the voices start (ticket 36): every machine that sounds, at velocity 127 with the Kit's defaults, on its own
	// output (it does not go through the master effects), its first Hit after it is chosen and a Hit again once it is
	// silent. It prints what each voice takes after the block its Hit lands in, its Hit again's (Engine::voiceStart's
	// table). A Hit again of the machines it holds back sounds HitDelay samples after its trigger: exactly for the
	// voices that start a block late and for ROM, within 4 samples for the others, whose own sound sets their first
	// sample; TRX-XT, TRX-XC and TRX-CB, not held back, sound their own start later.
	void voiceStarts(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		constexpr size_t length = 2048;
		std::vector<float> own(length);
		std::array<float*, mdDrums::Engine::OutputCount> outputs{};
		outputs[2] = own.data();
		// The first sample that is not 0 on Track 1's own output, from a trigger now; -1 when silent
		const auto hit = [&](mdDrums::Engine& _engine)
		{
			_engine.trigger(0, 127);
			_engine.render(outputs.data(), length);
			for(size_t i = 0; i < length; ++i)
				if(own[i] != 0.0f)
					return static_cast<int>(i);
			return -1;
		};
		// Renders until the Track's output has been silent for a while (at most 4 s); false if it never is
		const auto quiet = [&](mdDrums::Engine& _engine)
		{
			size_t silent = 0;
			for(size_t done = 0; done < 44100 * 4; done += length)
			{
				_engine.render(outputs.data(), length);
				for(size_t i = 0; i < length; ++i)
					silent = own[i] == 0.0f ? silent + 1 : 0;
				if(silent >= 8192)
					return true;
			}
			return false;
		};
		// Where a Hit of _machine is expected to sound on its own output, its voice starting _start after its block
		const auto expected = [](const uint8_t _machine, const int _start)
		{
			const auto held = std::max(0, mdDrums::Engine::AlignedStart - mdDrums::Engine::voiceStart(_machine));
			return mdDrums::Engine::SampleAccurateDelay + held + _start + mdDrums::Engine::MasterDelay;
		};

		std::string silentNames, starts, wrong;
		int sounding = 0;
		const mdDrums::Engine machines(_os, _bank, false);
		for(const auto& machine : machines.machines())
		{
			mdDrums::Engine engine(_os, _bank, false);
			engine.setMachine(0, machine.id);
			engine.render(outputs.data(), mdDrums::Engine::BlockSize * 3);
			const auto first = hit(engine);
			if(first < 0)
			{
				silentNames += " " + machine.name;
				continue;
			}
			++sounding;
			const auto again = quiet(engine) ? hit(engine) : -1;
			// What the voice takes after its block: the sound less the 31, the hold and the look-ahead
			const auto start = [&](const int _at) { return _at < 0 ? -1 : _at - expected(machine.id, 0); };
			starts += " " + machine.name + " " + std::to_string(start(first)) + "/" + std::to_string(start(again));

			// The references: a machine of each family, the voices that start a block late, ROM, and the two not held
			const std::string name = machine.name;
			const bool exact = name == "TRXBD" || name == "TRXS2" || name == "ROM01" || name == "ROM16";
			// Not "near": windows.h defines it away (mdDrumsTestSupport.h)
			const bool close = name == "TRXSD" || name == "TRXCH" || name == "EFMBD" || name == "EFMSD"
				|| name == "E12BD" || name == "E12SD" || name == "P-IBD" || name == "GNDSN";
			const bool late = name == "TRXXT" || name == "TRXXC" || name == "TRXCB";
			if(!exact && !close && !late)
				continue;
			const auto want = late ? expected(machine.id, mdDrums::Engine::voiceStart(machine.id))
				: mdDrums::Engine::HitDelay;
			const auto tolerance = exact ? 0 : close ? 4 : 1;
			// A sound that never goes silent (GND-SN, a sine) gives its first Hit
			const auto at = again >= 0 ? again : first;
			if(at < want || at > want + tolerance)
				wrong += " " + name + " (id " + std::to_string(machine.id) + ") at " + std::to_string(at)
					+ ", not " + std::to_string(want) + (tolerance ? " to " + std::to_string(want + tolerance) : "") + ";";
		}
		std::printf("voice starts on their own output, after the block a Hit lands in (first Hit/again):%s\n", starts.c_str());
		std::printf("voice starts: %d machines sound, silent:%s; held back, a Hit again sounds %d samples after its trigger\n",
			sounding, silentNames.c_str(), mdDrums::Engine::HitDelay);
		if(!wrong.empty())
			std::printf("voice starts elsewhere:%s\n", wrong.c_str());
		require(sounding > 60 && wrong.empty(), "a machine's Hit does not sound where its voice's start says");
	}

	// Where a Choke cuts, sample-accurate (ticket 38 of the editor map): its target, a sustained E12-RC on its own output,
	// sounds as it would without the Choke up to where the choking Hit sounds, 31 samples after that Hit's note, and is
	// silent from there, whether it waits more or less than that Hit; through a Link's own Choke too, and after a Choke
	// onto a Track already choked. A lower target goes silent at the engine's next tick instead, as the Machinedrum orders
	// its tick: later, at its own delay, and without a gap before. Each case against the same engine with its Chokes off.
	void chokes(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		struct Case
		{
			const char* name;
			int target;
			std::vector<int> chokers;					// the Tracks whose Choke is the target
			int linkFrom;								// a Track that Links to linkTo, or -1
			int linkTo;
			std::vector<std::pair<size_t, int>> hits;	// a note's host sample and Track; the last one cuts
			size_t from;								// where the comparison starts
			bool lower;									// the target is below the Track that cuts it
		};
		constexpr size_t block = mdDrums::Engine::BlockSize;
		// A note at sample _offset of host block _block: its Hit waits _offset - 1 samples (31 at offset 0)
		const auto at = [](const size_t _block, const size_t _offset) { return _block * block + _offset; };
		const std::vector<Case> cases{
			{"a higher target waiting longer than the Hit", 5, {2}, -1, -1, {{at(138, 30), 5}, {at(300, 3), 2}}, at(138, 30), false},
			{"a higher target waiting less than the Hit", 5, {2}, -1, -1, {{at(138, 3), 5}, {at(300, 30), 2}}, at(138, 3), false},
			{"a lower target", 2, {5}, -1, -1, {{at(138, 30), 2}, {at(300, 3), 5}}, at(138, 30), true},
			{"a Link's Choke", 6, {3}, 1, 3, {{at(138, 30), 6}, {at(300, 3), 1}}, at(138, 30), false},
			{"a Choke after one onto a Track already choked", 5, {2, 3}, 1, 3,
				{{at(138, 30), 5}, {at(300, 30), 2}, {at(400, 30), 2}, {at(500, 30), 5}, {at(800, 3), 1}}, at(600, 0), false},
		};

		const auto run = [&](const Case& _case, const bool _choke)
		{
			mdDrums::Engine engine(_os, _bank, true);
			engine.setMachine(_case.target, machineId(engine.machines(), "E12RC"));
			engine.setParam(_case.target, 1, 127);
			engine.setParam(_case.target, 2, 127);
			engine.setSeparateOutputs(1u << _case.target);
			if(_case.linkFrom >= 0)
				engine.setLink(_case.linkFrom, _case.linkTo);
			if(_choke)
				for(const auto track : _case.chokers)
					engine.setChoke(track, _case.target);
			std::vector<float> own(_case.hits.back().first + 4410);
			size_t rendered = 0;
			const auto renderTo = [&](const size_t _at)
			{
				std::array<float*, mdDrums::Engine::OutputCount> outputs{};
				outputs[2 + _case.target] = own.data() + rendered;
				engine.render(outputs.data(), _at - rendered);
				rendered = _at;
			};
			for(const auto& [note, track] : _case.hits)
			{
				renderTo(note);
				engine.trigger(track, 100);
			}
			renderTo(own.size());
			return own;
		};

		for(const auto& c : cases)
		{
			const auto on = run(c, true), off = run(c, false);
			// The cutting Hit sounds 31 samples after its note, held back as its machine's voice is quicker than the
			// latest, on the target's own output Dynamix's look-ahead later (as on Main)
			const auto last = c.hits.back().second;
			const auto cutter = last == c.linkFrom ? c.linkTo : last;
			const auto cut = c.hits.back().first + mdDrums::Engine::SampleAccurateDelay + mdDrums::Engine::MasterDelay
				+ std::max(0, mdDrums::Engine::AlignedStart
					- mdDrums::Engine::voiceStart(mdDrums::Engine::defaultMachine(cutter)));
			// Where the target goes silent for good, and what differs before
			auto silent = on.size();
			while(silent > c.from && on[silent - 1] == 0.0f)
				--silent;
			size_t differ = 0;
			for(auto i = c.from; i < silent; ++i)
				differ += on[i] != off[i];
			float sounding = 0;
			for(auto i = cut; i < cut + 256; ++i)
				sounding = std::max(sounding, std::abs(off[i]));
			std::printf("Choke, %s: silent %+d samples from where the cutting Hit sounds, %zu samples differ before\n",
				c.name, static_cast<int>(silent) - static_cast<int>(cut), differ);
			require(sounding > 0.001f, "the target was already silent where the Choke cuts it");
			require(differ == 0, "a Choke changed its target before it cut it");
			require(c.lower ? silent > cut : silent == cut, "a Choke did not cut its target where the Hit sounds");
		}
	}

	// Slides (ADR 0003, amended 2026-10-19; .scratch/slide/research-01-os-slide.md), on the engine's host: a Hit's locked
	// parameter glides as OS 1.63's sequencer slides a locked step, its smoothing target stepping at each 24-ppqn clock of
	// the transport by (end - start) x 128 / clocks, truncated, and stopping before the next sum passes the end; a clock
	// in the Hit's own block skips its track; the track's next Hit ends the slide with its own Lock or the Kit's value,
	// at once; without clocks it stands still; a knob sets the target over a Lock or a slide at once, as the OS's does.
	// The research's worked ramp anchors it, which mdSlideFirmwareTest measures on the firmware: one step at 1x (6
	// clocks) from 0 to 127 steps 0, 21, 42, 63, 84, 105.
	void slides(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		static constexpr int fltf = 12, fltw = 13, kitValue = 64;
		using Host = md::engine::HostModel<>;
		// Track 1's FLTF target after each block from the Hit's (block 0), _hit before the first, a clock before every
		// _every-th block (0: none) from the Hit's own, _during before each block
		const auto run = [&](const std::function<void(Host&)>& _hit, const int _blocks, const int _every,
			const std::function<void(Host&, int)>& _during = {})
		{
			md::engine::Engine raw(_os.firmware, _os.osImage);
			raw.loadRomBank(_bank);
			mdDrums::applyEngineDefaults(raw.host());
			auto& host = raw.host();
			host.setParam(0, fltf, kitValue);
			host.setParam(0, fltw, kitValue);
			_hit(host);
			md::engine::Engine::Output out;
			std::vector<int> targets;
			for(int b = 0; b < _blocks; ++b)
			{
				if(_every && b % _every == 0)
					host.clock();
				if(_during)
					_during(host, b);
				require(raw.render(out), "the engine faulted");
				targets.push_back(host.target(0, fltf));
			}
			return targets;
		};
		// The OS's ramp after _clocks clocks of a slide from _start to _end over _over clocks
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
		// Block by block: the clock in the Hit's block skipped, one more at every _every-th block
		const auto expect = [&](const std::vector<int>& _targets, const int _every, const int _start, const int _end,
			const int _over, const char* _name)
		{
			for(size_t b = 0; b < _targets.size(); ++b)
			{
				const auto expected = ramp(_start, _end, _over, static_cast<int>(b) / _every);
				require(_targets[b] == expected, std::string(_name) + ": the target at block " + std::to_string(b)
					+ " is " + std::to_string(_targets[b]) + ", the OS's ramp " + std::to_string(expected));
			}
		};
		const auto slideHit = [](const int _from, const int _clocks, const int _to)
		{
			return [=](Host& _h)
			{
				_h.lock(0, fltf, _from);
				_h.slide(0, _clocks);
				if(_to >= 0)
					_h.slideTarget(0, fltf, _to);
				_h.trigger(0, 100);
			};
		};

		// One step at 1x from 0 to 127, a clock every 28 blocks (896 samples, about 123 BPM); another Track's Hit, and a
		// Hit of its own the mute drops, leave it running; an unlocked parameter does not slide
		const auto rising = run(slideHit(0, 6, 127), 260, 28, [&](Host& _h, const int _b)
		{
			require(_h.target(0, fltw) == kitValue, "an unlocked parameter slid");
			if(_b == 50)
				_h.trigger(1, 100);
			if(_b == 100)
			{
				_h.setMute(0, true);
				_h.trigger(0, 100);
				_h.setMute(0, false);
			}
		});
		const std::vector<int> worked{0, 21, 42, 63, 84, 105, 126};
		for(size_t c = 0; c < worked.size(); ++c)
			require(ramp(0, 127, 6, static_cast<int>(c)) == worked[c], "the reference ramp is not the research's");
		require(rising[27] == 0 && rising[28] == 21, "the clock in the Hit's block was not skipped");
		expect(rising, 28, 0, 127, 6, "0 to 127 over 6 clocks");
		require(rising.back() == 126, "a slide not ended did not hold one increment short of its end");

		// The next Hit ends it, at once: the Kit's value without a Lock, its own Lock with one
		const auto ended = [&](const bool _locked)
		{
			return run(slideHit(0, 6, 127), 200, 28, [&](Host& _h, const int _b)
			{
				if(_b != 120)
					return;
				if(_locked)
					_h.lock(0, fltf, 127);
				_h.trigger(0, 100);
			});
		};
		const auto toKit = ended(false), toLock = ended(true);
		require(toKit[119] == 84 && toKit[120] == kitValue && toKit.back() == kitValue,
			"the next Hit did not play the Kit's value at once");
		require(toLock[120] == 127 && toLock.back() == 127, "the next Hit did not play its Lock at once");

		// A knob or a CC sets the target at once, as the OS's do (mdSlideFirmwareTest): over a Lock until the next Hit,
		// which plays the Kit's new value; over a slide until its next clock, from which the slide goes on. The Kit's
		// value set otherwise (a Kit loaded, ASSIGN MACHINE) leaves the Lock playing until the next Hit.
		const auto overLock = [&](const bool _knob)
		{
			return run([](Host& _h)
			{
				_h.lock(0, fltf, 0);
				_h.trigger(0, 100);
			}, 200, 28, [&](Host& _h, const int _b)
			{
				if(_b == 50)
				{
					if(_knob)
						_h.knob(0, fltf, 100);
					else
						_h.setParam(0, fltf, 100);
				}
				if(_b == 150)
					_h.trigger(0, 100);
			});
		};
		const auto turned = overLock(true), kitSet = overLock(false);
		require(turned[49] == 0 && turned[50] == 100 && turned[149] == 100 && turned.back() == 100,
			"a knob did not set a locked parameter's target at once");
		require(kitSet[50] == 0 && kitSet[149] == 0 && kitSet[150] == 100 && kitSet.back() == 100,
			"the Kit's value set as a Kit loads did not wait for the next Hit over a Lock");
		const auto overSlide = run(slideHit(0, 12, 127), 400, 28, [](Host& _h, const int _b)
		{
			if(_b == 70)
				_h.knob(0, fltf, 100);
		});
		require(overSlide[69] == ramp(0, 127, 12, 2) && overSlide[70] == 100 && overSlide[83] == 100
			&& overSlide[84] == ramp(0, 127, 12, 3) && overSlide.back() == ramp(0, 127, 12, 13),
			"a knob over a slide did not hold until its next clock, the slide going on");

		// Falling, with clocks twice as close; without a target, to the Kit's value
		expect(run(slideHit(100, 3, 20), 80, 14), 14, 100, 20, 3, "100 to 20 over 3 clocks");
		expect(run(slideHit(0, 4, -1), 200, 28), 28, 0, kitValue, 4, "0 to the Kit's value over 4 clocks");

		// Past 127 clocks (note 114): 3 + 127 x 3 = 384 clocks, 64 steps at 1x, a clock a block
		const auto longer = run([](Host& _h)
		{
			_h.lock(0, fltf, 0);
			_h.slide(0, 3);
			_h.extendSlide(0, 3);
			_h.slideTarget(0, fltf, 127);
			_h.trigger(0, 100);
		}, 420, 1);
		expect(longer, 1, 0, 127, 384, "0 to 127 over 384 clocks");
		require(longer[192] == ramp(0, 127, 384, 192) && longer[192] > 60 && longer[192] < 66,
			"a slide of 384 clocks was not halfway after 192");

		// Without clocks, the transport stopped: it stands still
		const auto stopped = run(slideHit(0, 6, 127), 200, 0);
		require(std::all_of(stopped.begin(), stopped.end(), [](const int _t) { return _t == 0; }),
			"a slide moved without clocks");

		// A slide without its Track's Hit in its block is dropped: the Lock of a later Hit holds
		const auto dropped = run([](Host& _h)
		{
			_h.slide(0, 6);
			_h.extendSlide(0, 1);
			_h.slideTarget(0, fltf, 127);
			_h.trigger(1, 100);
		}, 200, 28, [](Host& _h, const int _b)
		{
			if(_b != 1)
				return;
			_h.lock(0, fltf, 0);
			_h.trigger(0, 100);
		});
		require(dropped[0] == kitValue && dropped[1] == 0 && dropped.back() == 0, "a slide without its Hit was kept");
		std::printf("slides: OS 1.63's ramp at the transport's clocks, 0 to 127 over 6 clocks held at %d, over 384 at %d"
			" after 192; the next Hit ends them at once; still without clocks; dropped without their Hit\n",
			rising.back(), longer[192]);
	}

	// A Track that takes a MID or a CTR machine stops sounding at the Hit that brings it: their types play GND--'s null
	// machine on the voice DSP, which the Hit now gives the voice. E12-RC held (HOLD and DEC at 127) rings until then.
	void nonAudioSilence(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		for(const char* name : {"MID01", "CTRAL"})
		{
			mdDrums::Engine engine(_os, _bank, false);
			engine.setMachine(0, machineId(engine.machines(), "E12RC"));
			engine.setParam(0, 1, 127);
			engine.setParam(0, 2, 127);
			engine.setSeparateOutputs(1);
			constexpr size_t half = 22050;
			std::vector<float> own(2 * half);
			std::array<float*, mdDrums::Engine::OutputCount> outputs{};
			outputs[2] = own.data();
			engine.trigger(0, 127);
			engine.render(outputs.data(), half);
			engine.setMachine(0, machineId(engine.machines(), name));
			engine.trigger(0, 127);
			outputs[2] = own.data() + half;
			engine.render(outputs.data(), half);
			float before = 0, after = 0;
			for(size_t i = half / 2; i < half; ++i)
				before = std::max(before, std::abs(own[i]));
			for(size_t i = half + 4410; i < own.size(); ++i)
				after = std::max(after, std::abs(own[i]));
			std::printf("E12-RC held on Track 1, then %s at a Hit: %.4f before, %.7f from 0.1 s after\n", name, before, after);
			require(before > 0.01f && after < 1e-5f, std::string("a Track on ") + name + " still sounds its machine before");
		}
	}

	// A sustained sound (E12-RC, HOLD and DEC at 127) retriggered at each of the 32 places of a block: the delays change
	// at the Hit, which drops or doubles a few samples of the old tail. Threshold set before measuring: within 128
	// samples from the new Hit's trigger, no sample step steeper than 1.5 times the steepest one of the same retrigger
	// on the engine's blocks.
	void retrigger(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		const auto steepest = [&](const bool _accurate, const int _place)
		{
			mdDrums::Engine engine(_os, _bank, true);
			engine.setSampleAccurate(_accurate);
			engine.setMachine(0, machineId(engine.machines(), "E12RC"));
			engine.setParam(0, 1, 127);
			engine.setParam(0, 2, 127);
			std::vector<float> left(44100), right(44100);
			size_t rendered = 0;
			const auto renderTo = [&](const size_t _count)
			{
				engine.render(left.data(), right.data(), _count);
				rendered += _count;
			};
			renderTo(4416);
			renderTo(7);
			engine.trigger(0, 127);
			renderTo(4410 - 7);
			renderTo((_place - rendered % 32 + 64) % 32);
			engine.trigger(0, 127);
			engine.render(left.data(), right.data(), 128);
			float step = 0;
			for(size_t i = 1; i < 128; ++i)
				step = std::max(step, std::abs(left[i] - left[i - 1]));
			return step;
		};
		float worstRatio = 0;
		int worstPlace = 0;
		for(int place = 0; place < 32; ++place)
		{
			const auto ratio = steepest(true, place) / std::max(1e-9f, steepest(false, place));
			if(ratio > worstRatio)
			{
				worstRatio = ratio;
				worstPlace = place;
			}
		}
		std::printf("retrigger of E12-RC at the 32 places of a block: steepest step at most %.3f times the one on the"
			" engine's blocks (place %d)\n", worstRatio, worstPlace);
		require(worstRatio <= 1.5f, "a retrigger clicks with the sample-accurate delays");
	}

	// X.20's conversions (ticket 15 of the User Machines map, research/10): Pitch's 128 settings in the DEFAULT mode as
	// research/10 computed them from X.20's table builder, Decay OS 1.63's table at four entries a setting (nine of its
	// words checked against research/10's), the law fitted to it, and between two settings Unipolar 0 under one setting
	// and $7fffff from 127, Boolean any nonzero word, Index truncated
	void conversions(const md::fw::FlashOs& _os)
	{
		static constexpr uint32_t pitch[128] = {
			2048, 2139, 2234, 2333, 2437, 2545, 2659, 2777, 2900, 3029, 3164, 3304, 3451, 3605, 3765, 3932,
			4096, 4278, 4468, 4666, 4874, 5090, 5318, 5554, 5800, 6058, 6328, 6608, 6902, 7210, 7530, 7864,
			8192, 8556, 8936, 9332, 9748, 10180, 10636, 11108, 11600, 12116, 12656, 13216, 13804, 14420, 15060, 15728,
			16384, 17112, 17872, 18664, 19496, 20360, 21272, 22216, 23200, 24232, 25312, 26432, 27608, 28840, 30120, 31456,
			32768, 34224, 35744, 37328, 38992, 40720, 42544, 44432, 46400, 48464, 50624, 52864, 55216, 57680, 60240, 62912,
			65536, 68448, 71488, 74656, 77984, 81440, 85088, 88864, 92800, 96928, 101248, 105728, 110432, 115360, 120480,
			125824, 131072, 136896, 142976, 149312, 155968, 162880, 170176, 177728, 185600, 193856, 202496, 211456, 220864,
			230720, 240960, 251648, 262144, 273792, 285952, 298624, 311936, 325760, 340352, 355456, 371200, 387712, 404992,
			422912, 441728, 461440, 481920, 503296};
		const auto decay = md::mds::decayTable(_os.osImage);
		md::mds::Parameter p;
		const auto with = [&p](const md::mds::Conversion _c, const uint8_t _min = 0, const uint8_t _max = 127)
		{
			p.conversion = _c;
			p.min = _min;
			p.max = _max;
		};

		with(md::mds::Conversion::Pitch);
		int pitchOff = 0;
		for(int s = 0; s < 128; ++s)
			pitchOff += md::mds::convert(p, s, decay) != pitch[s];
		require(pitchOff == 0, std::to_string(pitchOff) + " Pitch settings are not X.20's");

		with(md::mds::Conversion::Decay);
		const std::pair<int, uint32_t> known[] = {{0, 0x5f29c2}, {16, 0x77d4a9}, {32, 0x7e22ba}, {48, 0x7f9536},
			{64, 0x7fe836}, {80, 0x7ffab5}, {96, 0x7ffed2}, {112, 0x7fffbd}, {127, 0x7ffff0}};
		for(const auto& [s, word] : known)
			require(md::mds::convert(p, s, decay) == word, "Decay at " + std::to_string(s) + " is not OS 1.63's word");
		bool rising = true;
		for(int s = 1; s < 128; ++s)
			rising = rising && md::mds::convert(p, s, decay) >= md::mds::convert(p, s - 1, decay);
		require(rising, "Decay does not rise with its setting");
		require(md::mds::convertFine(p, (10 << 7) + 40, decay) == decay[41], "Decay between two settings is not T[v >> 5]");
		const auto shortest = md::mds::convert(p, 0, decay), longest = md::mds::convert(p, 127, decay);
		const auto fitted = md::mds::approximateDecayTable();
		uint32_t stray = 0;
		for(size_t i = 0; i < decay.size(); ++i)
			stray = std::max(stray, decay[i] > fitted[i] ? decay[i] - fitted[i] : fitted[i] - decay[i]);
		require(stray <= 126, "the fitted Decay law strays " + std::to_string(stray) + " LSB from OS 1.63's table");

		with(md::mds::Conversion::UnipolarQ23);
		require(md::mds::convertFine(p, 0x7f, decay) == 0 && md::mds::convertFine(p, 0x80, decay) == 0x10000
			&& md::mds::convertFine(p, 0x3f7f, decay) == 0x7efe00 && md::mds::convert(p, 127, decay) == 0x7fffff,
			"Unipolar is not X.20's at its ends");
		with(md::mds::Conversion::Boolean);
		require(md::mds::convertFine(p, 0, decay) == 0 && md::mds::convertFine(p, 1, decay) == 1,
			"Boolean is not any nonzero word");
		with(md::mds::Conversion::Index, 2, 9);
		require(md::mds::convertFine(p, (5 << 7) + 127, decay) == 3 && md::mds::convertFine(p, 0, decay) == 0,
			"Index is not truncated from its minimum");
		std::printf("conversions: Pitch's 128 settings X.20's (2048 to 503296), Decay OS 1.63's table (%06x to %06x, the"
			" fitted law within %u LSB), Unipolar, Boolean and Index X.20's between settings\n",
			shortest, longest, stray);
	}

	// The test's own Sample: a sine of _hz at half scale for _seconds at 44.1 kHz, coded to 12 bits through the OS's table
	// in the flash image (flash $7a0000, each 16-bit value read as unsigned), two codes a word, the first in the high half
	mdDrums::Sample testSample(const std::vector<uint8_t>& _flash, const double _hz, const double _seconds)
	{
		const auto code = [&](const int16_t _v)
		{
			const auto at = 0x7a0000 + 2 * static_cast<size_t>(static_cast<uint16_t>(_v));
			return static_cast<uint32_t>((_flash[at] << 8 | _flash[at + 1]) & 0xfff);
		};
		const auto value = [&](const size_t _k)
		{
			return static_cast<int16_t>(std::lround(16383.0 * std::sin(2.0 * 3.14159265358979 * _hz * static_cast<double>(_k) / 44100.0)));
		};
		mdDrums::Sample sample;
		sample.words.resize(static_cast<size_t>(_seconds * 44100.0 / 2.0));
		for(size_t w = 0; w < sample.words.size(); ++w)
			sample.words[w] = code(value(2 * w)) << 12 | code(value(2 * w + 1));
		sample.length = static_cast<uint32_t>(2 * sample.words.size());
		return sample;
	}

	// A user's Sample on the engine (ticket 08 of the Samples map): mdDrums::Engine writes it into the voice DSP a share
	// of words a block (Samples::WordsPerBlock), the directory entry last, and the ROM Machine plays it from then on:
	// ROM-33, silent before, and ROM-05 in place of the factory's, which comes back when it is taken away; ROM-33 is
	// silent again. A Sample that does not fit changes nothing. The Hit preview plays the instance's Samples.
	void userSamples(const std::vector<uint8_t>& _flash, const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		constexpr uint8_t kRom05 = 132, kRom33 = 176;
		constexpr int kBefore = 16;	// blocks before every take's Hit, whatever the writes took
		const auto sine = std::make_shared<const mdDrums::Sample>(testSample(_flash, 441.0, 1.0));
		const int whole[8] = {64, 127, 127, 0, 0, 127, 0, 0};

		// Track 1 on _machine, alone on its Out, its whole Sample once; _change, then kBefore blocks, the Samples written
		// within them; Out 01 from the Hit for 2 s
		const auto take = [&](const uint8_t _machine, const std::function<void(mdDrums::Engine&)>& _change, int* _blocks = nullptr)
		{
			mdDrums::Engine engine(_os, _bank, false);
			engine.setMachine(0, _machine);
			for(int p = 0; p < 8; ++p)
				engine.setParam(0, p, whole[p]);
			engine.setLevel(0, 100);
			engine.setSeparateOutputs(1);
			std::vector<std::vector<float>> buffers(mdDrums::Engine::OutputCount, std::vector<float>(mdDrums::Engine::BlockSize));
			std::array<float*, mdDrums::Engine::OutputCount> outputs{};
			for(int i = 0; i < mdDrums::Engine::OutputCount; ++i)
				outputs[i] = buffers[i].data();
			_change(engine);
			int blocks = -1;
			for(int b = 0; b < kBefore; ++b)
			{
				if(blocks < 0 && engine.samplesWritten())
					blocks = b;
				engine.render(outputs.data(), mdDrums::Engine::BlockSize);
			}
			require(engine.samplesWritten(), "the Samples were not written within the blocks before the Hit");
			if(_blocks)
				*_blocks = blocks;
			engine.trigger(0, 127);
			std::vector<float> own;
			for(int b = 0; b < 2 * 44100 / mdDrums::Engine::BlockSize; ++b)
			{
				engine.render(outputs.data(), mdDrums::Engine::BlockSize);
				own.insert(own.end(), buffers[2].begin(), buffers[2].end());
			}
			return own;
		};
		const auto peakOf = [](const std::vector<float>& _s)
		{
			float p = 0;
			for(const auto v : _s)
				p = std::max(p, std::abs(v));
			return p;
		};
		const auto none = [](mdDrums::Engine&) {};

		int blocks = 0;
		const auto rom33 = take(kRom33, [&](mdDrums::Engine& _e) { require(_e.setSample(32, sine), "ROM-33 refused a Sample"); },
			&blocks);
		const auto empty33 = take(kRom33, none);
		const auto expected = static_cast<int>((sine->words.size() + mdDrums::Samples::WordsPerBlock - 1)
			/ mdDrums::Samples::WordsPerBlock);
		std::printf("user Samples: ROM-33 empty %.4f, with the test's Sample %.4f, written in %d blocks (%zu words, %zu a"
			" block)\n", peakOf(empty33), peakOf(rom33), blocks, sine->words.size(), mdDrums::Samples::WordsPerBlock);
		require(peakOf(empty33) == 0.0f && peakOf(rom33) > 0.01f, "ROM-33 does not play the user's Sample");
		require(blocks >= expected && blocks <= expected + 1, "a Sample took more blocks than its share a block allows");

		// The same Sample again (an equal copy) writes nothing, during which its Machine would play nothing; a Sample that
		// fills the whole room is written by one writeSamples, its budget spent exactly
		{
			mdDrums::Engine engine(_os, _bank, false);
			engine.setSample(32, sine);
			engine.writeSamples();
			require(engine.samplesWritten(), "writeSamples left a Sample unwritten");
			require(engine.setSample(32, std::make_shared<const mdDrums::Sample>(*sine)) && engine.samplesWritten(),
				"an equal Sample was written again");
			auto whole = std::make_shared<mdDrums::Sample>();
			whole->words.assign(mdDrums::Samples::End - mdDrums::Samples::Base, 0x800800);
			whole->length = static_cast<uint32_t>(2 * whole->words.size());
			require(engine.setSample(32, nullptr) && engine.setSample(33, whole), "a Sample of the whole room was refused");
			engine.writeSamples();
			require(engine.samplesWritten(), "a Sample of the whole room was not written by one writeSamples");
		}

		const auto factory05 = take(kRom05, none);
		const auto user05 = take(kRom05, [&](mdDrums::Engine& _e) { _e.setSample(4, sine); });
		const auto back05 = take(kRom05, [&](mdDrums::Engine& _e)
		{
			_e.setSample(4, sine);
			_e.writeSamples();
			_e.setSample(4, nullptr);
		});
		const auto back33 = take(kRom33, [&](mdDrums::Engine& _e)
		{
			_e.setSample(32, sine);
			_e.writeSamples();
			_e.setSample(32, nullptr);
		});
		std::printf("user Samples: ROM-05 factory %.4f, the user's %.4f, the factory's again %.4f; ROM-33 taken away %.4f\n",
			peakOf(factory05), peakOf(user05), peakOf(back05), peakOf(back33));
		require(user05 != factory05, "ROM-05 plays its factory Sample with the user's set");
		require(back05 == factory05, "ROM-05 does not play its factory Sample once the user's is taken away");
		require(peakOf(back33) == 0.0f, "ROM-33 sounds once its Sample is taken away");

		// Too big together: refused, ROM-33 keeps its Sample
		{
			mdDrums::Engine engine(_os, _bank, false);
			require(engine.setSample(32, sine), "ROM-33 refused a Sample");
			auto big = std::make_shared<mdDrums::Sample>();
			big->words.resize(mdDrums::Samples::End - mdDrums::Samples::Base - sine->words.size() + 1);
			big->length = static_cast<uint32_t>(2 * big->words.size());
			require(!engine.setSample(33, big) && !engine.sample(33) && engine.sample(32) == sine,
				"a Sample that does not fit was taken");
		}

		// The Hit preview with the instance's Samples
		{
			mdDrums::HitPreview::Request request;
			request.kit = *mdDrums::messages::factoryKit(_flash);
			request.kit.machines[0] = kRom33;
			for(int p = 0; p < 8; ++p)
				request.kit.parameters[0][static_cast<size_t>(p)] = static_cast<uint8_t>(whole[p]);
			request.velocity = 127;
			const auto preview = [&](const bool _withSample)
			{
				request.samples[32] = _withSample ? sine : nullptr;
				mdDrums::Engine engine(_os, _bank, false);
				mdDrums::Telemetry::Capture capture;
				mdDrums::HitPreview::render(engine, request, capture);
				int p = 0;
				for(int c = 0; c < capture.count; ++c)
					for(const auto v : capture.columns[static_cast<size_t>(c)])
						p = std::max(p, std::abs(static_cast<int>(v)));
				return p;
			};
			const auto with = preview(true), without = preview(false);
			std::printf("user Samples: the Hit preview of ROM-33 peaks %d with the instance's Sample, %d without\n", with,
				without);
			require(with > 300 && without == 0, "the Hit preview does not play the instance's Sample");
		}
	}

	// A Sample written into the voice DSP of an engine that plays (ticket 05 of the Samples map): what it costs, and what a
	// voice does meanwhile. The test's own Sample, a 441 Hz sine at half scale for one second at 44.1 kHz, is coded to
	// 12 bits through the OS's table in the flash image (fw::loadRomBankFromFlash's) and written above the factory bank,
	// then ROM-33's directory entry points at it between two blocks: the next Hit of ROM-33, silent before, plays it,
	// although the voice code was compiled before the write (P data, which the JIT never compiles). The whole free room,
	// $18FC12 to $1FFFFF, is written and timed, at once and a block's share at a time. A voice that plays ROM-01 while its
	// words are overwritten carries on without a fault.
	void sampleWrite(const std::vector<uint8_t>& _flash, const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		constexpr uint32_t kFree = 0x18fc12, kEnd = 0x200000;
		constexpr uint32_t kRom33Entry = md::fw::RomBank::DirectoryAddr + 4 * 48;	// ROM-33 to -48 at entries 48 to 63
		constexpr uint8_t kRom01 = 128, kRom33 = 176;
		constexpr int kBlocks = 200;

		const auto words = testSample(_flash, 441.0, 1.0).words;
		const uint32_t entry[4] = {kFree, static_cast<uint32_t>(2 * words.size()), 0xffffff, 1u << 18};

		// Track 1 on _machine, its whole Sample once (PTCH 64, DEC and HOLD 127, END 127), struck at block 2; its voice's
		// peak from then on, and between two blocks _between(block) runs
		const int whole[8] = {64, 127, 127, 0, 0, 127, 0, 0};
		const auto play = [&](const uint8_t _machine, const std::function<void(md::engine::Engine&, int)>& _between)
		{
			md::engine::Engine raw(_os.firmware, _os.osImage);
			raw.loadRomBank(_bank);
			mdDrums::applyEngineDefaults(raw.host());
			raw.host().setMachine(0, _machine);
			for(int p = 0; p < 8; ++p)
				raw.host().setParam(0, p, whole[p]);
			md::engine::Engine::Output out;
			int32_t peak = 0;
			for(int b = 0; b < kBlocks; ++b)
			{
				if(b == 2)
					raw.host().trigger(0, 127);
				_between(raw, b);
				require(raw.render(out), "the engine faulted");
				for(const auto s : raw.voiceBlock()[0])
					peak = std::max(peak, std::abs(s));
			}
			return peak;
		};
		const auto none = [](md::engine::Engine&, int) {};

		// ROM-33 is silent; with the Sample written and its entry pointed at it before its Hit, it plays it
		const auto empty = play(kRom33, none);
		const auto written = play(kRom33, [&](md::engine::Engine& _e, const int _b)
		{
			if(_b != 1)
				return;
			_e.voices().writeP(kFree, words.data(), words.size());
			_e.voices().writeP(kRom33Entry, entry, 4);
		});
		std::printf("Sample write: ROM-33 empty peaks %d, with the test's Sample written between two blocks %d (of %d)\n",
			empty, written, 1 << 23);
		require(empty == 0, "ROM-33 sounds before its Sample is written");
		require(written > (1 << 20), "ROM-33 does not play the Sample written into the running voice DSP");

		// The whole free room: at once, and in a block's share of words at a time
		std::vector<uint32_t> room(kEnd - kFree, 0x800800);
		double onceMs = 0;
		play(kRom01, [&](md::engine::Engine& _e, const int _b)
		{
			if(_b != 3)
				return;
			const auto start = std::chrono::steady_clock::now();
			_e.voices().writeP(kFree, room.data(), room.size());
			onceMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		});
		constexpr size_t kChunk = 4096;
		double chunkWorstUs = 0;
		play(kRom01, [&](md::engine::Engine& _e, const int _b)
		{
			const auto at = static_cast<size_t>(_b) * kChunk;
			if(at >= room.size())
				return;
			const auto start = std::chrono::steady_clock::now();
			_e.voices().writeP(kFree + static_cast<uint32_t>(at), room.data() + at, std::min(kChunk, room.size() - at));
			chunkWorstUs = std::max(chunkWorstUs,
				std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
		});
		std::printf("Sample write: the free room, %zu words, in %.3f ms at once; %zu words a block, worst %.1f us"
			" (a 32-sample block lasts %.0f us)\n", room.size(), onceMs, kChunk, chunkWorstUs, 32e6 / 44100.0);

		// ROM-01's voice while its own words are overwritten, a block's share at a time from its Hit on: no fault (play
		// requires every block to render), and its sound changes
		const auto romFirst = _bank.directory[0].words[0];
		const auto plain = play(kRom01, none);
		const auto overwritten = play(kRom01, [&](md::engine::Engine& _e, const int _b)
		{
			if(_b < 3 || static_cast<size_t>(_b - 3) * 64 >= words.size())
				return;
			const auto at = static_cast<size_t>(_b - 3) * 64;
			_e.voices().writeP(romFirst + static_cast<uint32_t>(at), words.data() + at, std::min<size_t>(64, words.size() - at));
		});
		std::printf("Sample write: ROM-01 peaks %d undisturbed, %d with its words overwritten while it plays\n", plain,
			overwritten);
		require(plain > 0 && overwritten != plain, "overwriting a playing Sample changed nothing");
	}

	// User Machines (ticket 12 of the User Machines map): the test's sawtooth, packaged as MDX X.20 packages are, at
	// USR-02 on Track 3 of the engine (md::engine::Engine, as mdEngineFirmwareTest compares it): its voice gives the bare
	// host's samples (md::mds::BareHost, mdsPlay's), from its Hit, its code relocated and its parameters converted, and the
	// other Tracks give what they give without it. mdDrums::Engine plays it with a Hit, compiled ahead, and stops when it is
	// removed. A package the voice DSP cannot play is refused.
	// NEIGHBOUR User Machines (ticket 22 of the User Machines map, research/12): the test's copier on a Track renders,
	// word for word, the raw voice block of the Track it reads, as the MDS Standard has it: one, two or three back from
	// the descriptor at Y:$7F0, four back from its own buffer before RENDER; a Track before the first in the block before
	// (Track 1 one back is Track 16 of the last block). The Kit's Tracks all struck, some on GND-- or never struck, which
	// read as zeros
	void neighbours(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		constexpr int blocks = 40;
		using Voices = std::vector<std::array<int32_t, mdDrums::Engine::BlockSize>>;
		const auto voice = [](const Voices& _v, const int _block, const int _track) -> const auto&
		{
			return _v[static_cast<size_t>(_block * mdDrums::Engine::TrackCount + _track)];
		};
		// _source: the Track the copier hears _back back, chosen (ticket 31), else -1 for the Track _back before it
		const auto run = [&](const int _track, const int _back, const int _source = -1)
		{
			const auto package = std::make_shared<const md::mds::Package>(md::mds::parsePackage(
				md::mds::buildPackage(mdDrums::test::neighbourDraft(_back))));
			md::engine::Engine raw(_os.firmware, _os.osImage);
			raw.loadRomBank(_bank);
			mdDrums::applyEngineDefaults(raw.host());
			raw.installUserMachine(0, package);
			raw.host().setMachine(_track, mdDrums::Engine::FirstUserMachine);
			raw.host().setMachine(12, 1);	// GND-SN: a source that renders
			raw.host().setMachine(13, 0);	// GND--: skipped, zeros
			if(_source >= 0)
			{
				auto sources = md::engine::VoiceEngine::defaultSources(_track);
				sources[static_cast<size_t>(_back - 1)] = static_cast<uint8_t>(_source);
				raw.voices().setSources(_track, sources);
			}
			md::engine::Engine::Output out;
			Voices voices(static_cast<size_t>(blocks) * mdDrums::Engine::TrackCount);
			for(int b = 0; b < blocks; ++b)
			{
				if(b == 2)
					for(int t = 0; t < mdDrums::Engine::TrackCount; ++t)
						if(t != 14)	// never struck
							raw.host().trigger(t, 100);
				require(raw.render(out), "the engine faulted");
				for(int t = 0; t < mdDrums::Engine::TrackCount; ++t)
					voices[static_cast<size_t>(b * mdDrums::Engine::TrackCount + t)] = raw.voiceBlock()[static_cast<size_t>(t)];
			}
			// From the block after the copier's Hit, which brings its machine
			size_t differ = 0, sounding = 0;
			for(int b = 3; b < blocks; ++b)
			{
				// A source rendered before the copier gives this block, one after it the block before
				const auto source = _source >= 0 ? _source : _track - _back;
				const auto& expected = source >= 0 && source < _track ? voice(voices, b, source)
					: voice(voices, b - 1, (source + mdDrums::Engine::TrackCount) % mdDrums::Engine::TrackCount);
				const auto& got = voice(voices, b, _track);
				differ += got != expected;
				sounding += std::any_of(got.begin(), got.end(), [](const int32_t _s) { return _s != 0; });
			}
			std::printf("NEIGHBOUR copier on Track %d, %d back%s: %zu of %d blocks differ from its source's, %zu sound\n",
				_track + 1, _back, _source >= 0 ? (" chosen as Track " + std::to_string(_source + 1)).c_str() : "", differ,
				blocks - 3, sounding);
			require(differ == 0, "a NEIGHBOUR machine did not read its source's raw block");
			return sounding;
		};
		// Track 6: all four sources in the block; Track 2: one in the block, the others in the last; Track 1: none
		for(const int back : {1, 2, 3, 4})
			require(run(5, back) > 0, "a NEIGHBOUR copier on Track 6 heard nothing");
		require(run(1, 1) > 0 && run(1, 2) > 0, "a NEIGHBOUR copier on Track 2 heard nothing");
		require(run(0, 1) > 0 && run(0, 4) > 0, "a NEIGHBOUR copier on Track 1 heard nothing");
		// Sources that give zeros: Track 15 never struck, Track 14 on GND-- (skipped); Track 13 on GND-SN renders
		run(15, 1);
		run(14, 1);
		// Chosen sources (ticket 31): Track 12 hearing Track 3 in the same block, one, two and four back; Track 3
		// hearing Track 12, rendered after it, a block later
		for(const int back : {1, 2, 4})
			require(run(11, back, 2) > 0, "a NEIGHBOUR copier on Track 12 did not hear Track 3");
		require(run(2, 1, 11) > 0 && run(2, 4, 11) > 0, "a NEIGHBOUR copier on Track 3 did not hear Track 12");

		// On mdDrums::Engine (ticket 20): the copier one back on Track 6 plays on Track 5's delay, so that each of Track
		// 5's Hits, wherever it falls in a block, starts on Track 6's own output at the sample it starts on Track 5's, and
		// the two line up there (their cross-correlation peaks at lag 0). Not sample for sample: the effects chain, as
		// the mixer DSP's, keeps scratch from one Track to the next, so equal inputs and parameters on two Tracks need
		// not give equal outputs. Both Tracks with the same effects, routing and level, their LFOs at zero depth.
		// Then chosen sources (ticket 31): Track 12 hearing Track 3 lines up the same; Track 3 hearing Track 12, which
		// renders after it, lands a block later, 32 samples, every time
		// _struck: the copier's own Track struck once, as before ticket 32; without, it works from its machine on
		const auto lineUp = [&](const int _source, const int _nfx, const int _lag, const bool _struck = true)
		{
			const auto copier = std::make_shared<const md::mds::Package>(md::mds::parsePackage(
				md::mds::buildPackage(mdDrums::test::neighbourDraft(1))));
			mdDrums::Engine engine(_os, _bank, false);
			engine.installUserMachine(0, copier);
			engine.setMachine(_nfx, mdDrums::Engine::FirstUserMachine);
			if(_source != (_nfx + mdDrums::Engine::TrackCount - 1) % mdDrums::Engine::TrackCount)
			{
				auto sources = engine.sources(_nfx);
				sources[0] = static_cast<uint8_t>(_source);
				engine.setSources(_nfx, sources);
			}
			constexpr std::array<int, 16> same{0, 0, 64, 64, 0, 127, 0, 0, 0, 100, 64, 0, 0, 0, 0, 0};	// AMD to LFOM
			for(const int t : {_source, _nfx})
			{
				engine.setLevel(t, 100);
				for(int p = 0; p < 16; ++p)
					engine.setParam(t, 8 + p, same[static_cast<size_t>(p)]);
			}
			engine.setSeparateOutputs((1u << _source) | (1u << _nfx));
			constexpr size_t settle = 44100, length = settle + 16000;
			std::vector<float> five(length), six(length);
			size_t rendered = 0;
			const auto renderTo = [&](const size_t _at)
			{
				std::array<float*, mdDrums::Engine::OutputCount> outputs{};
				outputs[2 + _source] = five.data() + rendered;
				outputs[2 + _nfx] = six.data() + rendered;
				engine.render(outputs.data(), _at - rendered);
				rendered = _at;
			};
			if(_struck)
				engine.trigger(_nfx, 100);	// the copier's machine, from its Hit
			const std::array<size_t, 4> hits{settle + 1003, settle + 5017, settle + 9030, settle + 12001};
			for(const auto at : hits)
			{
				renderTo(at);
				engine.trigger(_source, 100);
			}
			renderTo(length);
			const auto onset = [](const std::vector<float>& _v, const size_t _from)
			{
				for(auto i = _from; i < _v.size(); ++i)
					if(std::abs(_v[i]) > 1e-4f)
						return static_cast<int64_t>(i);
				return int64_t{-1};
			};
			std::string seen;
			bool aligned = true;
			for(const auto at : hits)
			{
				const auto five0 = onset(five, at), six0 = onset(six, at);
				int bestLag = 0;
				double best = -1;
				for(int lag = -48; lag <= 48; ++lag)
				{
					double c = 0;
					for(size_t i = at + 64; i < at + 2048; ++i)
						c += double(five[i]) * double(six[i + lag]);
					if(c > best)
					{
						best = c;
						bestLag = lag;
					}
				}
				seen += " " + std::to_string(at % mdDrums::Engine::BlockSize) + ":" + std::to_string(six0 - five0) + "/"
					+ std::to_string(bestLag);
				aligned = aligned && five0 >= 0 && six0 >= 0 && bestLag == _lag;
			}
			std::printf("NEIGHBOUR copier on Track %d hearing Track %d%s, Hits at block place: onset difference / lag%s\n",
				_nfx + 1, _source + 1, _struck ? "" : ", never struck", seen.c_str());
			require(aligned, "a NEIGHBOUR Track did not play on its source's delay");
		};
		lineUp(4, 5, 0);
		lineUp(2, 11, 0);
		lineUp(11, 2, mdDrums::Engine::BlockSize);
		// Ticket 32: a NEIGHBOUR works from its machine on, its own Track never struck
		lineUp(4, 5, 0, false);
	}

	// X.20's ANTI-CLICK (ticket 33 of the User Machines map), at 64 and 128 samples: a Hit of TRX-SD, which X.20 fades,
	// waits 2 or 4 blocks, its first one too; struck again while it sounds, the Track's raw voice falls in X.20's straight
	// line over the wait (gain 1 - i/64 or 1 - i/128, truncated), the old voice's words kept, then the new Hit plays; a
	// second Hit during the wait replaces the first and the count goes on; TRX-BD, continuous in X.20, is not held; on
	// mdDrums::Engine every Hit, faded or not, sounds the fade later after its note
	void antiClick(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		constexpr int blocks = 40;
		constexpr int track = 1;
		using Voice = std::vector<std::array<int32_t, mdDrums::Engine::BlockSize>>;
		const auto play = [&](const uint8_t _machine, const int _fadeBlocks, const std::vector<int>& _hits)
		{
			md::engine::Engine raw(_os.firmware, _os.osImage);
			raw.loadRomBank(_bank);
			mdDrums::applyEngineDefaults(raw.host());
			raw.host().setMachine(track, _machine);
			raw.host().setFadeBlocks(_fadeBlocks);
			md::engine::Engine::Output out;
			Voice voice(blocks);
			for(int b = 0; b < blocks; ++b)
			{
				for(const auto at : _hits)
					if(at == b)
						raw.host().trigger(track, 100);
				require(raw.render(out), "the engine faulted");
				voice[static_cast<size_t>(b)] = raw.voiceBlock()[static_cast<size_t>(track)];
			}
			return voice;
		};
		const auto sounds = [](const Voice& _v, const int _block)
		{
			const auto& b = _v[static_cast<size_t>(_block)];
			return std::any_of(b.begin(), b.end(), [](const int32_t _s) { return _s != 0; });
		};
		for(const int w : {2, 4})
		{
			const auto once = play(17, w, {2});			// TRX-SD struck once
			const auto twice = play(17, w, {2, 20});	// and again while it sounds
			const auto merged = play(17, w, {2, 20, 21});	// and once more during the wait
			int first = -1;
			for(int b = 0; b < blocks && first < 0; ++b)
				if(sounds(once, b))
					first = b;
			require(first == 2 + w, "TRX-SD's first Hit did not wait " + std::to_string(w) + " blocks");
			// The wait's blocks: the old voice, as it plays without a second Hit, times X.20's ramp
			const int32_t step = w == 2 ? 0x1ffff : 0xffff;
			size_t wrong = 0;
			for(int b = 20; b < 20 + w; ++b)
			{
				const int64_t start = 0x7fffff - static_cast<int64_t>(32) * step * (b - 20);
				for(size_t i = 0; i < mdDrums::Engine::BlockSize; ++i)
				{
					const auto gain = start - static_cast<int64_t>(i) * step;
					const auto expected = static_cast<int32_t>((static_cast<int64_t>(once[static_cast<size_t>(b)][i]) * gain) >> 23);
					wrong += twice[static_cast<size_t>(b)][i] != expected;
				}
			}
			const auto& last = twice[static_cast<size_t>(19 + w)];
			const auto tail = std::abs(last.back());
			std::printf("ANTI-CLICK %d: TRX-SD's first Hit sounds at block %d; struck again, %zu of %d faded samples differ from"
				" X.20's ramp on the old voice, %d at the wait's end; the new Hit %s at block %d\n", 32 * w, first, wrong,
				32 * w, tail, sounds(twice, 20 + w) && twice[static_cast<size_t>(20 + w)] != once[static_cast<size_t>(20 + w)]
				? "plays" : "does not play", 20 + w);
			require(wrong == 0, "the fade is not X.20's ramp on the old voice");
			require(twice[static_cast<size_t>(20 + w)] != once[static_cast<size_t>(20 + w)] && sounds(twice, 20 + w),
				"the waiting Hit did not fire after the fade");
			require(merged == twice, "a second Hit during the wait did not replace the first, the count going on");
			// TRX-BD, continuous: never held
			require(play(16, w, {2, 20}) == play(16, 0, {2, 20}), "TRX-BD, continuous in X.20, waited for the fade");
		}

		// mdDrums::Engine: TRX-BD on Track 1 and TRX-SD on Track 2 struck at the same sample sound 128 samples later with
		// ANTI-CLICK at 128 than without, each on its own output (where the two outputs line up best: the faded Hit fires
		// at another phase of the engine's ticks, so its first samples need not be the same)
		const auto renderBoth = [&](const int _fade)
		{
			mdDrums::Engine engine(_os, _bank, false);
			engine.setFade(_fade);
			engine.setMachine(0, 16);
			engine.setMachine(1, 17);
			engine.setSeparateOutputs(3);
			constexpr size_t length = 22050;
			std::vector<float> one(length), two(length);
			std::array<float*, mdDrums::Engine::OutputCount> outputs{};
			outputs[2] = one.data();
			outputs[3] = two.data();
			engine.render(outputs.data(), 1000);
			engine.trigger(0, 100);
			engine.trigger(1, 100);
			outputs[2] = one.data() + 1000;
			outputs[3] = two.data() + 1000;
			engine.render(outputs.data(), length - 1000);
			return std::make_pair(one, two);
		};
		const auto lagOf = [](const std::vector<float>& _plain, const std::vector<float>& _faded)
		{
			int best = 0;
			double top = -1;
			for(int lag = 0; lag <= 256; ++lag)
			{
				double c = 0;
				for(size_t i = 1000; i < 9000; ++i)
					c += double(_plain[i]) * double(_faded[i + static_cast<size_t>(lag)]);
				if(c > top)
				{
					top = c;
					best = lag;
				}
			}
			return best;
		};
		const auto plain = renderBoth(0);
		const auto faded = renderBoth(128);
		const auto bd = lagOf(plain.first, faded.first);
		const auto sd = lagOf(plain.second, faded.second);
		std::printf("ANTI-CLICK 128 on mdDrums::Engine: TRX-BD %d and TRX-SD %d samples later (hitDelay %d)\n", bd, sd,
			mdDrums::Engine::HitDelay + 128);
		require(bd == 128 && sd == 128, "ANTI-CLICK did not delay every Hit by the fade");
	}

	// By hand: the user's own NEIGHBOUR User Machines (X.20 B's NFX and SYN-F) where MD_X20_MACHINES names a folder of
	// .syx or .mds files, as the ROM is read, never carried. Each one on Track 6 over Tracks 1 to 5 struck: no voice DSP
	// fault, and what its own output peaks at. Nothing without the variable.
	void userNeighbours(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		const auto* folder = std::getenv("MD_X20_MACHINES");
		if(!folder || !*folder || !std::filesystem::is_directory(folder))
			return;
		for(const auto& entry : std::filesystem::recursive_directory_iterator(folder))
		{
			const auto extension = entry.path().extension().u8string();
			if(!entry.is_regular_file() || (extension != ".syx" && extension != ".mds"))
				continue;
			std::ifstream in(entry.path(), std::ios::binary);
			const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
			std::shared_ptr<const md::mds::Package> package;
			try
			{
				package = std::make_shared<const md::mds::Package>(md::mds::parsePackage(
					!bytes.empty() && bytes.front() == 0xf0 ? md::mds::packageFromSysex(bytes) : bytes));
			}
			catch(const std::exception&)
			{
				continue;
			}
			if(!(package->flags & md::mds::FlagNeighbour))
				continue;
			mdDrums::Engine engine(_os, _bank, false);
			engine.installUserMachine(0, package);
			engine.setMachine(5, mdDrums::Engine::FirstUserMachine);
			engine.setSeparateOutputs(1u << 5);
			std::vector<float> own(88200);
			std::array<float*, mdDrums::Engine::OutputCount> outputs{};
			outputs[2 + 5] = own.data();
			for(int t = 0; t <= 5; ++t)
				engine.trigger(t, 127);
			engine.render(outputs.data(), own.size());
			float peak = 0;
			for(const auto s : own)
				peak = std::max(peak, std::abs(s));
			std::printf("the user's %s (%s) on Track 6 over Tracks 1-5: its own output peaks at %.4f (%.1f dBFS)\n",
				package->displayName().c_str(), entry.path().filename().u8string().c_str(), peak,
				20.0 * std::log10(std::max(peak, 1e-9f)));
		}
	}

	void userMachines(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		const auto package = std::make_shared<const md::mds::Package>(md::mds::parsePackage(md::mds::buildPackage(mdDrums::test::sawDraft())));
		constexpr int user = 1;	// USR-02
		const auto id = static_cast<uint8_t>(mdDrums::Engine::FirstUserMachine + user);
		constexpr int track = 2;
		constexpr int blocks = 96;

		// The bare host, from INIT and TRIG on
		md::mds::BareHost bare(*package, md::mds::decayTable(_os.osImage));
		for(size_t p = 0; p < package->parameters.size(); ++p)
			bare.setParameter(p, package->parameters[p].defaultValue);
		bare.init();
		bare.trig();
		std::vector<int32_t> alone(blocks * mdDrums::Engine::BlockSize);
		for(int b = 0; b < blocks; ++b)
			bare.render(alone.data() + b * mdDrums::Engine::BlockSize);

		// Two engines, Tracks 1, 2 and 4 struck alike; Track 3 on the User Machine in one, on the empty machine in the other
		const auto play = [&](const bool _withUser, std::vector<std::array<int32_t, mdDrums::Engine::BlockSize>>& _voices)
		{
			md::engine::Engine raw(_os.firmware, _os.osImage);
			raw.loadRomBank(_bank);
			mdDrums::applyEngineDefaults(raw.host());
			if(_withUser)
			{
				raw.installUserMachine(user, package);
				raw.host().setMachine(track, id);
			}
			else
			{
				raw.host().setMachine(track, 0);
			}
			md::engine::Engine::Output out;
			_voices.assign(static_cast<size_t>(blocks) * mdDrums::Engine::TrackCount, {});
			for(int b = 0; b < blocks; ++b)
			{
				if(b == 2)
					for(const int t : {0, 1, track, 3})
						raw.host().trigger(t, 100);
				require(raw.render(out), "the engine faulted");
				for(int t = 0; t < mdDrums::Engine::TrackCount; ++t)
					_voices[static_cast<size_t>(b * mdDrums::Engine::TrackCount + t)] = raw.voiceBlock()[static_cast<size_t>(t)];
			}
		};
		std::vector<std::array<int32_t, mdDrums::Engine::BlockSize>> with, without;
		play(true, with);
		play(false, without);

		std::vector<int32_t> voice;
		for(int b = 0; b < blocks; ++b)
		{
			const auto& block = with[static_cast<size_t>(b * mdDrums::Engine::TrackCount + track)];
			voice.insert(voice.end(), block.begin(), block.end());
		}
		const auto first = static_cast<size_t>(std::find_if(voice.begin(), voice.end(), [](int32_t _s) { return _s != 0; })
			- voice.begin());
		require(first < voice.size(), "the User Machine is silent on its Track's voice");
		require(first % mdDrums::Engine::BlockSize == 0, "the User Machine's voice does not start its block");
		const std::vector<int32_t> fromHit(voice.begin() + static_cast<ptrdiff_t>(first), voice.end());
		const std::vector<int32_t> bareSame(alone.begin(), alone.begin() + static_cast<ptrdiff_t>(fromHit.size()));
		const auto diffs = differences(fromHit, bareSame);
		size_t others = 0;
		for(int b = 0; b < blocks; ++b)
			for(int t = 0; t < mdDrums::Engine::TrackCount; ++t)
				if(t != track && with[static_cast<size_t>(b * mdDrums::Engine::TrackCount + t)]
					!= without[static_cast<size_t>(b * mdDrums::Engine::TrackCount + t)])
					++others;
		std::printf("User Machine %s at USR-02 on Track 3: its voice from block %zu, %zu samples, %zu differ from the bare"
			" host's; the other Tracks' voices differ in %zu blocks of %d\n", package->displayName().c_str(),
			first / mdDrums::Engine::BlockSize, fromHit.size(), diffs, others, blocks * (mdDrums::Engine::TrackCount - 1));
		require(diffs == 0, "the User Machine's voice is not the bare host's");
		require(others == 0, "a User Machine changed another Track's voice");

		// Another package at the number the Track plays, as choosing in the Library does when the Track alone plays it
		// (ticket 25): the Track is silent until its next Hit, which INITs the new package, and its voice is then the bare
		// host's for that package. The voice once kept its active type, so the Hit called TRIG alone and the new code ran
		// on the old package's state: the user heard silence, saturation or the wrong pitch until the Track took another
		// machine and came back.
		{
			const auto other = std::make_shared<const md::mds::Package>(md::mds::parsePackage(mdDrums::test::sawPackage(60)));
			md::mds::BareHost fresh(*other, md::mds::decayTable(_os.osImage));
			for(size_t p = 0; p < other->parameters.size(); ++p)
				fresh.setParameter(p, other->parameters[p].defaultValue);
			fresh.init();
			fresh.trig();
			std::vector<int32_t> expected(blocks * mdDrums::Engine::BlockSize);
			for(int b = 0; b < blocks; ++b)
				fresh.render(expected.data() + b * mdDrums::Engine::BlockSize);

			md::engine::Engine raw(_os.firmware, _os.osImage);
			raw.loadRomBank(_bank);
			mdDrums::applyEngineDefaults(raw.host());
			raw.installUserMachine(user, package);
			raw.host().setMachine(track, id);
			md::engine::Engine::Output out;
			std::vector<int32_t> after;
			bool silentBetween = true;
			for(int b = 0; b < 2 * blocks; ++b)
			{
				if(b == 2)
					raw.host().trigger(track, 100);
				if(b == 20)
				{
					raw.installUserMachine(user, other);
					raw.host().setMachine(track, id);
				}
				if(b == 24)
					raw.host().trigger(track, 100);
				require(raw.render(out), "the engine faulted");
				const auto& block = raw.voiceBlock()[static_cast<size_t>(track)];
				if(b >= 20 && b < 24)
					silentBetween &= std::all_of(block.begin(), block.end(), [](int32_t _s) { return _s == 0; });
				if(b >= 24)
					after.insert(after.end(), block.begin(), block.end());
			}
			const auto from = static_cast<size_t>(std::find_if(after.begin(), after.end(), [](int32_t _s) { return _s != 0; })
				- after.begin());
			require(from < after.size(), "the new package is silent after its Hit");
			const auto length = std::min(after.size() - from, expected.size());
			const std::vector<int32_t> heard(after.begin() + static_cast<ptrdiff_t>(from),
				after.begin() + static_cast<ptrdiff_t>(from + length));
			const auto replacedDiffs = differences(heard, std::vector<int32_t>(expected.begin(),
				expected.begin() + static_cast<ptrdiff_t>(heard.size())));
			std::printf("another package at USR-02 while Track 3 plays it: %s until the next Hit, then %zu of %zu samples"
				" differ from the bare host's\n", silentBetween ? "silent" : "not silent", replacedDiffs, heard.size());
			require(silentBetween, "the Track goes on playing after its User Machine was replaced");
			require(replacedDiffs == 0, "a package replaced at the number a Track plays does not start from its INIT");
		}

		// mdDrums::Engine: installed (compiled ahead), struck like any machine on Track 1's own output, then removed
		{
			mdDrums::Engine engine(_os, _bank, false);
			const auto start = std::chrono::steady_clock::now();
			engine.installUserMachine(0, package);
			const auto installMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			engine.setMachine(0, mdDrums::Engine::FirstUserMachine);
			engine.setLevel(0, 100);
			engine.setSeparateOutputs(1);
			std::vector<std::vector<float>> buffers(mdDrums::Engine::OutputCount, std::vector<float>(4096));
			std::array<float*, mdDrums::Engine::OutputCount> outputs{};
			for(int i = 0; i < mdDrums::Engine::OutputCount; ++i)
				outputs[i] = buffers[i].data();
			engine.render(outputs.data(), mdDrums::Engine::BlockSize * 3);
			engine.trigger(0, 127);
			engine.render(outputs.data(), 4096);
			const auto& own = buffers[2];
			const auto sounds = std::find_if(own.begin(), own.end(), [](float _v) { return _v != 0.0f; });
			require(sounds != own.end(), "mdDrums::Engine does not play a User Machine");
			const auto at = static_cast<int>(sounds - own.begin());
			std::printf("User Machine on mdDrums::Engine: installed and compiled in %.1f ms; a Hit at velocity 127 sounds %d"
				" samples after its trigger on its own output (HitDelay %d)\n", installMs, at, mdDrums::Engine::HitDelay);

			engine.removeUserMachine(0);
			for(int i = 0; i < 4; ++i)
				engine.render(outputs.data(), 4096);
			engine.trigger(0, 127);
			engine.render(outputs.data(), 4096);
			require(std::all_of(own.begin(), own.end(), [](float _v) { return std::abs(_v) < 1e-6f; }),
				"a removed User Machine still sounds");

			// Refused: an import the voice DSP does not provide (SAMPLE_READ), and STEREO PAIR (NEIGHBOUR plays: neighbours)
			auto sampleRead = mdDrums::test::sawDraft();
			sampleRead.imports.push_back({0, 5, 10});
			auto pair = mdDrums::test::sawDraft();
			pair.flags = md::mds::FlagNeighbour | md::mds::FlagStereoPair;
			for(const auto& draft : {sampleRead, pair})
			{
				bool refused = false;
				try
				{
					engine.installUserMachine(3, std::make_shared<const md::mds::Package>(
						md::mds::parsePackage(md::mds::buildPackage(draft))));
				}
				catch(const std::exception& _e)
				{
					refused = true;
					std::printf("refused: %s\n", _e.what());
				}
				require(refused, "a User Machine the voice DSP cannot play was installed");
			}
		}
	}
}

namespace
{
	// setQuietSkip: two patterns on two engines each, the Machinedrum's and one whose quiet voices stop, every Track on
	// its default machine at a short DEC (30), as drum Kits have them: the busy one below (one Hit every 64th of a
	// second) and a beat (BD on the quarters, SD on two and four, CH on the eighths at 120 BPM, the other Tracks never
	// struck, whose voices never rendered). Each window on Main as loud as the Machinedrum's (play), a good share of the
	// voice blocks not rendered, and a knob turned wakes its Track's voice.
	void quietVoices(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		const auto build = [&](const bool _quiet)
		{
			auto engine = std::make_unique<mdDrums::Engine>(_os, _bank, true);
			engine->setQuietSkip(_quiet);
			for(int track = 0; track < 16; ++track)
			{
				const auto id = mdDrums::Engine::defaultMachine(track);
				engine->setMachine(track, id);
				engine->setLevel(track, 100);
				const auto& names = engine->machine(id)->params;
				if(const auto dec = std::find(names.begin(), names.end(), "DEC"); dec != names.end())
					engine->setParam(track, static_cast<int>(dec - names.begin()), 30);
			}
			return engine;
		};
		// A pattern of _period steps: each step's window on Main, on the engine whose quiet voices stop, within half a
		// decibel of the range the Machinedrum's gives that step of the pattern over the run. Not the Machinedrum's
		// own window: a machine whose oscillators run on through a Hit (TRX-CB) sounds from another phase there, as
		// from one Hit to the next of the Machinedrum's (4 dB apart on TRX-CB)
		const auto play = [&](const char* _name, const int _steps, const int _period, const size_t _window,
			const auto& _hits)
		{
			const auto exact = build(false), quiet = build(true);
			std::vector<float> left(_window), right(_window);
			std::array<double, 2> seconds{};
			std::vector<std::array<double, 2>> energies(static_cast<size_t>(_steps));
			for(int step = 0; step < _steps; ++step)
			{
				for(size_t e = 0; e < 2; ++e)
				{
					auto& engine = e == 0 ? *exact : *quiet;
					for(int track = 0; track < 16; ++track)
						if((_hits(step) >> track) & 1)
							engine.trigger(track, 100);
					const auto start = std::chrono::steady_clock::now();
					engine.render(left.data(), right.data(), _window);
					seconds[e] += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
					auto& energy = energies[static_cast<size_t>(step)][e];
					for(size_t i = 0; i < _window; ++i)
						energy += double(left[i]) * left[i] + double(right[i]) * right[i];
				}
			}
			double worst = 0;
			for(int step = 0; step < _steps; ++step)
			{
				double low = 1e30, high = 0;
				for(int other = step % _period; other < _steps; other += _period)
				{
					low = std::min(low, energies[static_cast<size_t>(other)][0]);
					high = std::max(high, energies[static_cast<size_t>(other)][0]);
				}
				const auto energy = energies[static_cast<size_t>(step)][1];
				if(high > 1e-6)
					worst = std::max({worst, 10.0 * std::log10(low / std::max(energy, 1e-12)),
						10.0 * std::log10(energy / high)});
			}
			const double share = double(quiet->quietBlocks()) / (_steps * double(_window) / 32.0 * 16);
			std::printf("quiet voices, %s: %.0f%% of the voice blocks not rendered, %.2f s against %.2f s (%.0f%%), each"
				" window on Main %.2f dB at most out of the Machinedrum's range\n", _name, share * 100.0, seconds[1],
				seconds[0], seconds[1] / seconds[0] * 100.0, worst);
			require(worst < 0.5, "a Hit after a quiet voice stopped sounds other than the Machinedrum's");
			return share;
		};
		const auto busy = play("busy", 512, 16, 689, [](const int _step) { return 1u << (_step % 16); });
		play("beat", 64, 8, 5513, [](const int _step)
		{
			return (_step % 4 == 0 ? 1u : 0u) | (_step % 8 == 4 ? 2u : 0u) | (_step % 2 == 0 ? 0x40u : 0u);
		});
		require(busy > 0.1, "quiet voices keep rendering");

		// Track 1 struck, then quiet: its voice stops; a knob turned on it makes the voice render again
		auto engine = build(true);
		auto& quiet = *engine;
		quiet.trigger(0, 100);
		std::vector<float> rest(44100 * 2), other(44100 * 2);
		quiet.render(rest.data(), other.data(), rest.size());
		const auto before = quiet.quietBlocks();
		quiet.render(rest.data(), other.data(), 32 * 8);
		const auto still = quiet.quietBlocks() - before;
		// The knob reaches the voice at the OS's next tick, every 11 blocks; the voice then renders until it has been quiet
		// for kQuietBlocks again
		quiet.knob(0, 0, 20);
		const auto turned = quiet.quietBlocks();
		quiet.render(rest.data(), other.data(), 32 * 32);
		const auto woken = quiet.quietBlocks() - turned;
		std::printf("quiet voices: Track 1 struck then quiet, %llu of 8 blocks not rendered; a knob turned, %llu of 32\n",
			static_cast<unsigned long long>(still), static_cast<unsigned long long>(woken));
		require(still == 8, "a quiet voice still renders");
		require(woken < 32, "a knob turned does not wake its Track's voice");

		// Track 1 struck every 6 s: the master effects stop once Main has been quiet for 4.5 s, give silence, and the next
		// Hit sounds on Main within half a decibel of the Machinedrum's
		const auto exact = build(false), sparse = build(true);
		std::vector<float> left(44100 * 6), right(44100 * 6);
		double worst = 0;
		bool silent = true;
		for(int hit = 0; hit < 3; ++hit)
		{
			std::array<double, 2> energy{};
			for(size_t e = 0; e < 2; ++e)
			{
				auto& engine = e == 0 ? *exact : *sparse;
				engine.trigger(0, 100);
				engine.render(left.data(), right.data(), left.size());
				for(size_t i = 0; i < 22050; ++i)
					energy[e] += double(left[i]) * left[i] + double(right[i]) * right[i];
				if(e == 1)
					silent = silent && std::all_of(left.end() - 32 * 64, left.end(), [](float _s) { return _s == 0.0f; });
			}
			worst = std::max(worst, std::abs(10.0 * std::log10(energy[1] / energy[0])));
		}
		const auto masterBlocks = sparse->quietMasterBlocks();
		std::printf("quiet master: %llu of %d blocks not run over three Hits 6 s apart, each Hit's half second on Main"
			" %.2f dB from the Machinedrum's\n", static_cast<unsigned long long>(masterBlocks), 3 * 44100 * 6 / 32, worst);
		require(masterBlocks > 1000, "the master effects keep running on silence");
		require(silent, "Main is not silent while the master effects stop");
		require(worst < 0.5, "a Hit after the master effects stopped sounds other than the Machinedrum's");
		// A master parameter changed wakes them when the OS sends its effect's words: one effect a tick, in turn, so
		// within 44 blocks
		sparse->setMaster(7, 0);
		sparse->render(left.data(), right.data(), 32 * 64);
		const auto masterWoken = sparse->quietMasterBlocks() - masterBlocks;
		std::printf("quiet master: a master parameter changed, %llu of 64 blocks not run\n",
			static_cast<unsigned long long>(masterWoken));
		require(masterWoken <= 44, "a master parameter changed does not wake the master effects");
	}
}

int main()
{
	return mdDrums::test::run("mdDrumsEngineTest", [](const std::vector<uint8_t>& _flash)
	{
		mdDrums::Engine engine(_flash);
		require(engine.machines().size() > 100, "the OS's machine table is incomplete");

		// The scenarios on engines of their own build them from the OS and the UW bank read once, without the
		// machines and the master compiled ahead, which leaves the samples as they are (mdDrumsFirstHitTest)
		const auto os = md::fw::loadFirmwareFromFlash(_flash);
		const auto bank = md::fw::loadRomBankFromFlash(_flash);
		osTables(os);
		// First: the checks below take where the Hits sound from it
		voiceStarts(os, bank);

		engine.setMachine(0, machineId(engine.machines(), "TRXBD"));
		engine.setLevel(0, 100);
		settle(engine);
		require(listen(engine).second == 0.0f, "a track sounds without a trigger");

		// A trigger sounds HitDelay samples later on Main, as every machine's (TRX-BD's voice sounds a block after its
		// trigger, the latest), and reaches -60 dBFS within a block
		engine.trigger(0, 127);
		const auto [first, loud] = listen(engine);
		std::printf("TRXBD velocity 127: first audible sample %d, peak %.4f\n", first, loud);
		require(first >= mdDrums::Engine::HitDelay && first < mdDrums::Engine::HitDelay + mdDrums::Engine::BlockSize,
			"the trigger does not sound HitDelay samples later on Main");
		require(loud > 0.01f, "the trigger is too quiet");

		settle(engine);
		engine.trigger(0, 40);
		const auto soft = listen(engine).second;
		std::printf("TRXBD velocity 40: peak %.4f\n", soft);
		require(soft < loud * 0.8f && soft > 0.0f, "the velocity does not scale the level");

		// One of X.20's built-ins without X.20's OS plays the empty machine, not the machine the Track had (ticket 08 of
		// the map of X.20's built-in Machines)
		settle(engine);
		engine.setMachine(0, md::engine::x20::GndSw);
		engine.trigger(0, 127);
		const auto missing = listen(engine).second;
		std::printf("GND-SW without X.20 after TRXBD: peak %.4f\n", missing);
		require(missing < 1e-4f, "GND-SW without X.20 plays the machine the Track had");
		engine.setMachine(0, machineId(engine.machines(), "TRXBD"));

		// On engines of their own: 48 Hits leave their track's voice in another state than the tests below expect
		for(const bool accurate : {false, true})
		{
			mdDrums::Engine fresh(os, bank, true);
			fresh.setSampleAccurate(accurate);
			fresh.setMachine(0, machineId(fresh.machines(), "TRXBD"));
			fresh.setLevel(0, 100);
			const auto jitter = latency(fresh);
			require(!accurate || jitter == 0, "sample-accurate Hits jitter");
		}
		equivalence(_flash, os, bank);
		retrigger(os, bank);
		nonAudioSilence(os, bank);
		chokes(os, bank);
		slides(os, bank);
		coldFireBytePush(os);
		conversions(os);
		userMachines(os, bank);
		neighbours(os, bank);
		antiClick(os, bank);
		userNeighbours(os, bank);
		sampleWrite(_flash, os, bank);
		userSamples(_flash, os, bank);
		quietVoices(os, bank);

		// Track 1 on its own output: it leaves the main mix and sounds there alone.
		{
			settle(engine);
			engine.setSeparateOutputs(1);
			std::vector<std::vector<float>> buffers(mdDrums::Engine::OutputCount, std::vector<float>(44100));
			std::array<float*, mdDrums::Engine::OutputCount> outputs{};
			for(int i = 0; i < mdDrums::Engine::OutputCount; ++i)
				outputs[i] = buffers[i].data();
			engine.trigger(0, 127);
			engine.render(outputs.data(), 44100);
			const auto peak = [&](const int _output)
			{
				float result = 0;
				for(const auto value : buffers[_output])
					result = std::max(result, std::abs(value));
				return result;
			};
			std::printf("track 1 separated: main peak %.4f, own output %.4f, track 2 output %.4f\n",
				std::max(peak(0), peak(1)), peak(2), peak(3));
			require(peak(0) < g_silence && peak(1) < g_silence, "a separated track still plays in the main mix");
			require(peak(2) > 0.01f, "a separated track is silent on its own output");
			require(peak(3) == 0.0f, "another track's output carries track 1");
			engine.setSeparateOutputs(0);
		}

		// Links, Chokes and Mute, on tracks 2 to 6, each on its own output (mdEngineFirmwareTest compares them with
		// the firmware's)
		{
			for(int track = 1; track < 6; ++track)
			{
				engine.setMachine(track, machineId(engine.machines(), "EFMCB"));
				engine.setLevel(track, 100);
			}
			engine.setSeparateOutputs(0x3e);
			settle(engine);
			std::vector<std::vector<float>> buffers(mdDrums::Engine::OutputCount, std::vector<float>(4410));
			std::array<float*, mdDrums::Engine::OutputCount> outputs{};
			for(int i = 0; i < mdDrums::Engine::OutputCount; ++i)
				outputs[i] = buffers[i].data();
			const auto peak = [&](const int _track, const size_t _from = 0)
			{
				float result = 0;
				for(size_t i = _from; i < buffers[2 + _track].size(); ++i)
					result = std::max(result, std::abs(buffers[2 + _track][i]));
				return result;
			};

			// A Link: track 2 also hits track 3, whose own Link to track 4 does not fire
			engine.setLink(1, 2);
			engine.setLink(2, 3);
			engine.trigger(1, 100);
			engine.render(outputs.data(), 4410);
			std::printf("Link 2 to 3: track 2 %.4f, track 3 %.4f, track 4 %.4f\n", peak(1), peak(2), peak(3));
			require(peak(1) > 0.01f && std::abs(peak(2) - peak(1)) < 1e-6f, "a Link did not hit its target as hard");
			require(peak(3) == 0.0f, "a Link went two steps");
			engine.setLink(1, mdDrums::Engine::NoTrack);
			engine.setLink(2, mdDrums::Engine::NoTrack);

			// A Choke: track 4 sounding, track 3's Hit silences it where it sounds (HitDelay after its trigger) and until
			// its own next Hit
			settle(engine);
			engine.setChoke(2, 3);
			engine.trigger(3, 100);
			engine.render(outputs.data(), 4410);
			const auto before = peak(3);
			engine.trigger(2, 100);
			engine.render(outputs.data(), 4410);
			std::printf("Choke 3 to 4: track 4 %.4f before, %.4f after where the Hit sounds\n", before,
				peak(3, mdDrums::Engine::HitDelay));
			require(before > 0.01f && peak(3, mdDrums::Engine::HitDelay) == 0.0f, "a Choke did not silence its target");
			engine.trigger(3, 100);
			engine.render(outputs.data(), 4410);
			require(peak(3) > 0.01f, "a choked track stayed silent after its own Hit");
			engine.setChoke(2, mdDrums::Engine::NoTrack);

			// Mute: track 6 drops its Hits, firing neither its Link nor its Choke
			settle(engine);
			engine.trigger(1, 100);
			engine.render(outputs.data(), 441);
			engine.setMute(5, true);
			engine.setLink(5, 4);
			engine.setChoke(5, 1);
			engine.trigger(5, 100);
			engine.render(outputs.data(), 4410);
			std::printf("muted track 6 struck: track 6 %.4f, its Link %.4f, its Choke %.4f\n", peak(5), peak(4), peak(1));
			require(peak(5) == 0.0f && peak(4) == 0.0f, "a muted track's Hit sounded");
			require(peak(1) > 0.01f, "a muted track's Choke fired");
			engine.setMute(5, false);
			engine.setLink(5, mdDrums::Engine::NoTrack);
			engine.setChoke(5, mdDrums::Engine::NoTrack);
			for(int track = 1; track < 6; ++track)
				engine.setLevel(track, 0);
			engine.setSeparateOutputs(0);
		}

		// The ROM machines play the UW bank, which fills ROM-01 to -32: the whole sample once (PTCH 64, DEC and HOLD
		// 127, BRR 0, STRT 0, END 127, no retrig). ROM-33 holds no sample and stays silent.
		{
			const int whole[8] = {64, 127, 127, 0, 0, 127, 0, 0};
			for(const char* name : {"ROM01", "ROM32", "ROM33"})
			{
				engine.setMachine(0, machineId(engine.machines(), name));
				for(int param = 0; param < 8; ++param)
					engine.setParam(0, param, whole[param]);
				settle(engine);
				engine.trigger(0, 127);
				const auto peak = listen(engine).second;
				std::printf("%s velocity 127: peak %.4f\n", name, peak);
				if(std::string(name) == "ROM33")
					require(peak < g_silence, "an empty ROM slot sounds");
				else
					require(peak > 0.01f, "a ROM machine of the factory bank is silent");
			}
		}

		// All 16 tracks on the machines they start with (TRX, then EFM-BD and -SD), triggered every 64th of a second
		// for 8 seconds.
		for(int track = 0; track < 16; ++track)
		{
			engine.setMachine(track, mdDrums::Engine::defaultMachine(track));
			engine.setLevel(track, 100);
		}
		std::vector<float> left(689), right(689);
		for(const bool accurate : {false, true})
		{
			engine.setSampleAccurate(accurate);
			const auto start = std::chrono::steady_clock::now();
			for(int step = 0; step < 512; ++step)
			{
				engine.trigger(step % 16, 100);
				engine.render(left.data(), right.data(), left.size());
			}
			const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			const double audio = 512.0 * 689.0 / 44100.0;
			std::printf("16 tracks and the master effects%s: %.2f s of audio in %.2f s (%.1f%% of one core)\n",
				accurate ? ", sample-accurate" : "", audio, seconds, seconds / audio * 100.0);
			// The audio thread's budget (ticket 09): what MD Drums renders, at most 30 % of real time on the reference PC
			require(seconds < audio * 0.3, "the engine takes more than 30 % of real time for 16 tracks and the master");
		}
	});
}
