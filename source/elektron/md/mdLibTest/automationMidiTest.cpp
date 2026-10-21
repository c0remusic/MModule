#include "mdProtocol/mdautomation.h"
#include "mdProtocol/mdmachines.h"
#include "mdProtocol/mdsysexautomation.h"
#include "synthLib/midiBufferParser.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace
{
	void require(const bool _condition, const char* const _message)
	{
		if(_condition)
			return;
		std::cerr << "automationMidiTest: " << _message << '\n';
		std::exit(1);
	}

	void expectMessage(const md::MachineModel _model,
		const md::automation::ParameterChange& _change, const uint8_t _baseChannel,
		const md::automation::ControlChange& _expected)
	{
		const auto encoded = md::automation::encodeParameterChange(_model, _change,
			_baseChannel);
		require(encoded.has_value(), "valid parameter did not encode");
		require(*encoded == _expected, "parameter encoded to the wrong CC");
		const auto decoded = md::automation::decodeParameterChange(_model, *encoded,
			_baseChannel);
		require(decoded.has_value(), "encoded CC did not decode");
		require(*decoded == _change, "CC round trip changed its parameter address");
	}

	void expectRoundTrip(const md::MachineModel _model,
		const md::automation::ParameterChange& _change, const uint8_t _baseChannel)
	{
		const auto encoded = md::automation::encodeParameterChange(_model, _change,
			_baseChannel);
		require(encoded.has_value(), "valid parameter did not encode");
		const auto decoded = md::automation::decodeParameterChange(_model, *encoded,
			_baseChannel);
		require(decoded.has_value(), "encoded CC did not decode");
		require(*decoded == _change, "CC round trip changed its parameter address");
	}

	void testMachinedrum()
	{
		using namespace md::automation;
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Synthesis, 0, 0, 23}, 0, {0xb0, 16, 23});
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Effects, 0, 0, 31}, 0, {0xb0, 24, 31});
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Routing, 3, 7, 127}, 0, {0xb0, 119, 127});
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Synthesis, 4, 0, 64}, 0, {0xb1, 16, 64});
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Level, 15, 0, 99}, 0, {0xb3, 11, 99});
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Mute, 9, 0, 1}, 4, {0xb6, 13, 1});

		for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
		{
			for(uint8_t page = machinedrum::Synthesis;
				page <= machinedrum::Routing; ++page)
			{
				for(uint8_t index = 0; index < 8; ++index)
					expectRoundTrip(md::MachineModel::Machinedrum,
						{page, track, index, static_cast<uint8_t>(track + index)}, 0);
			}

			for(const auto page : {machinedrum::Level, machinedrum::Mute})
			{
				const ParameterChange change{page, track, 0,
					static_cast<uint8_t>(page == machinedrum::Mute ? 1 : track)};
				expectRoundTrip(md::MachineModel::Machinedrum, change, 0);
			}
		}

		require(!encodeParameterChange(md::MachineModel::Machinedrum,
			{machinedrum::Synthesis, 16, 0, 0}, 0), "accepted MD track 17");
		require(!encodeParameterChange(md::MachineModel::Machinedrum,
			{machinedrum::Synthesis, 15, 0, 0}, 13), "accepted overflowing MD channel span");
	}

	void finishDump(md::automation::sysex::Message& _message)
	{
		uint32_t checksum = 0;
		for(size_t i = 9; i < _message.size(); ++i)
			checksum += _message[i];
		checksum &= 0x3fff;
		const auto finalSize = _message.size() + 5;
		const auto length = static_cast<uint16_t>(finalSize - 10);
		_message.push_back(static_cast<uint8_t>(checksum >> 7));
		_message.push_back(static_cast<uint8_t>(checksum & 0x7f));
		_message.push_back(static_cast<uint8_t>(length >> 7));
		_message.push_back(static_cast<uint8_t>(length & 0x7f));
		_message.push_back(0xf7);
	}

	void testSysexRequestsAndStatus()
	{
		using namespace md::automation::sysex;
		require(statusRequest(md::MachineModel::Machinedrum,
			StatusParameter::Global)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x70, 0x01, 0xf7}),
			"wrong MD status request");
		require(globalRequest(md::MachineModel::Monomachine, 7)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x51, 0x07, 0xf7}),
			"wrong MM global request");
		require(kitRequest(md::MachineModel::Machinedrum, 63)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x53, 0x3f, 0xf7}),
			"wrong MD kit request");
		require(kitSave(md::MachineModel::Machinedrum, 63)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x59, 0x3f, 0xf7}),
			"wrong MD kit save");
		require(kitSave(md::MachineModel::Monomachine, 127)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x59, 0x7f, 0xf7}),
			"wrong MM kit save");
		require(kitLoad(md::MachineModel::Machinedrum, 5)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x58, 0x05, 0xf7}),
			"wrong MD kit load");
		require(patternSelect(md::MachineModel::Monomachine, 20)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x71, 0x04, 0x14, 0xf7}),
			"wrong MM pattern selection");
		// Read back as the SET STATUS it is
		const auto selection = parseSetStatus(md::MachineModel::Machinedrum,
			patternSelect(md::MachineModel::Machinedrum, 127));
		require(selection && selection->parameter == StatusParameter::Pattern
			&& selection->value == 127, "pattern selection not read as SET STATUS");

		const Message response{0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00,
			0x72, 0x02, 0x37, 0xf7};
		const auto parsed = parseStatusResponse(md::MachineModel::Monomachine,
			response);
		require(parsed && parsed->parameter == StatusParameter::Kit
			&& parsed->value == 0x37, "could not parse status response");
		require(!parseStatusResponse(md::MachineModel::Machinedrum, response),
			"accepted status response for the wrong product");
		const Message setStatus{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00,
			0x71, 0x02, 0x01, 0xf7};
		const Message strictSetStatus{0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00,
			0x71, 0x02, 0x37, 0xf7};
		const auto parsedSetStatus = parseSetStatus(md::MachineModel::Monomachine,
			strictSetStatus);
		require(parsedSetStatus && parsedSetStatus->parameter == StatusParameter::Kit
			&& parsedSetStatus->value == 0x37, "could not parse SET STATUS");
		for(const auto position : {size_t{1}, size_t{2}, size_t{3}, size_t{4},
			size_t{5}, size_t{6}, size_t{9}})
		{
			auto malformed = strictSetStatus;
			malformed[position] ^= 1;
			require(!parseSetStatus(md::MachineModel::Monomachine, malformed),
				"accepted malformed SET STATUS framing");
		}
		auto malformed = strictSetStatus;
		malformed[8] = 0x80;
		require(!parseSetStatus(md::MachineModel::Monomachine, malformed),
			"accepted non-7-bit SET STATUS value");
		malformed = setStatus;
		malformed[7] = 0x7f;
		require(!parseSetStatus(md::MachineModel::Monomachine, malformed),
			"accepted unknown SET STATUS parameter");
		malformed = setStatus;
		malformed[4] = 0x02;
		malformed[8] = 64;
		require(!parseSetStatus(md::MachineModel::Machinedrum, malformed),
			"accepted out-of-range MD Kit slot");

		for(const auto& request : {
			statusRequest(md::MachineModel::Machinedrum, StatusParameter::Global),
			globalRequest(md::MachineModel::Machinedrum, 3),
			kitRequest(md::MachineModel::Machinedrum, 12),
			statusRequest(md::MachineModel::Monomachine, StatusParameter::Pattern),
			globalRequest(md::MachineModel::Monomachine, 7),
			kitRequest(md::MachineModel::Monomachine, 127)})
		{
			const auto model = request[4] == 0x03
				? md::MachineModel::Monomachine
				: md::MachineModel::Machinedrum;
			require(isReadOnlyRequest(model, request),
				"controller query was not classified as read-only");
		}
		require(!isReadOnlyRequest(md::MachineModel::Machinedrum, setStatus),
			"state-changing status message was classified as read-only");
		require(!isReadOnlyRequest(md::MachineModel::Monomachine,
			statusRequest(md::MachineModel::Machinedrum, StatusParameter::Kit)),
			"wrong-product query was classified as read-only");
		auto invalidRequest = globalRequest(md::MachineModel::Machinedrum, 0);
		invalidRequest[7] = 8;
		require(!isReadOnlyRequest(md::MachineModel::Machinedrum, invalidRequest),
			"out-of-range Global request was classified as read-only");
		invalidRequest = kitRequest(md::MachineModel::Machinedrum, 0);
		invalidRequest[7] = 64;
		require(!isReadOnlyRequest(md::MachineModel::Machinedrum, invalidRequest),
			"out-of-range MD Kit request was classified as read-only");
		invalidRequest = statusRequest(md::MachineModel::Machinedrum,
			StatusParameter::Global);
		invalidRequest.push_back(0xf7);
		require(!isReadOnlyRequest(md::MachineModel::Machinedrum, invalidRequest),
			"request with trailing data was classified as read-only");
	}

	void testMidiRunningStatus()
	{
		synthLib::MidiBufferParser parser(synthLib::MidiEventSource::Device);
		std::vector<synthLib::SMidiEvent> events;
		parser.write(std::vector<uint8_t>{
			0xb2, 16, 1, 17, 2, 18, 3,
			0xf8,
			19, 4,
			0xf1, 0x05,
			20, 6,
			0x92, 60, 100, 61, 101});
		parser.getEvents(events);
		require(events.size() == 8, "wrong MIDI running-status event count");
		require(events[0].a == 0xb2 && events[0].b == 16 && events[0].c == 1,
			"wrong first running-status CC");
		require(events[1].a == 0xb2 && events[1].b == 17 && events[1].c == 2,
			"running-status CC lost retained status");
		require(events[2].a == 0xb2 && events[2].b == 18 && events[2].c == 3,
			"wrong third running-status CC");
		require(events[3].a == 0xf8,
			"realtime byte was not emitted independently");
		require(events[4].a == 0xb2 && events[4].b == 19 && events[4].c == 4,
			"realtime byte incorrectly cancelled running status");
		require(events[5].a == 0xf1 && events[5].b == 0x05,
			"System Common message was parsed incorrectly");
		require(events[6].a == 0x92 && events[6].b == 60 && events[6].c == 100,
			"orphan data after System Common was not discarded");
		require(events[7].a == 0x92 && events[7].b == 61 && events[7].c == 101,
			"note-on running status was parsed incorrectly");

		parser.write(std::vector<uint8_t>{0xb0, 7});
		parser.discardPartialMessage();
		parser.write(std::vector<uint8_t>{100, 101});
		events.clear();
		parser.getEvents(events);
		require(events.empty(), "discontinuity retained unsafe running status");
	}

	void testMachinedrumDumps()
	{
		using namespace md::automation;
		using namespace md::automation::sysex;
		Message global{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00,
			0x50, 0x06, 0x01, 0x05};
		global.resize(0xb0, 0);
		global[0xad] = 11;
		// Track routing: 16 raw bytes at 0x0a, 0 to 5 for A to F, 6 for MAIN
		for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
			global[0x0a + track] = static_cast<uint8_t>(track % 7);
		finishDump(global);
		const auto parsedGlobal = parseGlobalDump(md::MachineModel::Machinedrum, global);
		require(parsedGlobal && parsedGlobal->slot == 5
			&& parsedGlobal->baseChannel == 11,
			"wrong MD base channel");
		require(parsedGlobal->trackOutputs && (*parsedGlobal->trackOutputs)[0] == TrackOutput::A
			&& (*parsedGlobal->trackOutputs)[6] == TrackOutput::Main && (*parsedGlobal->trackOutputs)[9] == TrackOutput::C,
			"wrong MD track routing");
		{
			auto unknown = global;
			unknown.resize(unknown.size() - 5);
			unknown[0x0a + 3] = 7;
			finishDump(unknown);
			const auto parsed = parseGlobalDump(md::MachineModel::Machinedrum, unknown);
			require(parsed && !parsed->trackOutputs, "MD routing accepted an output the machine does not have");
		}
		require(trackRouting(4, TrackOutput::C) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x5c, 4, 2, 0xf7}
			&& trackRouting(15, TrackOutput::Main) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x5c, 15, 6, 0xf7},
			"wrong SET TRACK ROUTING");
		require(!trackRouting(16, TrackOutput::A) && !trackRouting(0, static_cast<TrackOutput>(7)),
			"SET TRACK ROUTING accepted a track or output out of range");
		global[0xad] = 0x7f;
		global.resize(global.size() - 5);
		finishDump(global);
		const auto parsedNone = parseGlobalDump(md::MachineModel::Machinedrum, global);
		require(parsedNone && parsedNone->slot == 5
			&& parsedNone->baseChannel == 0x7f,
			"MD MIDI NONE should be a valid persisted setting");
		global[9] = 8;
		global.resize(global.size() - 5);
		finishDump(global);
		require(!parseGlobalDump(md::MachineModel::Machinedrum, global),
			"accepted out-of-range MD Global slot");

		Message kit{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00,
			0x52, 0x04, 0x01, 0x04};
		kit.resize(1228, 0);
		for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
		{
			for(uint8_t parameter = 0; parameter < 24; ++parameter)
				kit[0x1a + track * 24 + parameter]
					= static_cast<uint8_t>((track * 24 + parameter) & 0x7f);
			kit[0x19a + track] = static_cast<uint8_t>(100 + track);
		}
		// Machine assignments: 16 big-endian 32-bit values in 7-bit groups at 0x1aa.
		// Track 1 is ROM-33 (176, needs the top bit), track 2 TRX-BD with the TONAL
		// flag in its upper bits, the others EFM-BD + track.
		{
			std::vector<uint8_t> raw;
			for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
			{
				const uint32_t value = track == 1 ? 176u : track == 2 ? 0x20000u | 16u : 32u + track;
				raw.push_back(static_cast<uint8_t>(value >> 24));
				raw.push_back(static_cast<uint8_t>(value >> 16));
				raw.push_back(static_cast<uint8_t>(value >> 8));
				raw.push_back(static_cast<uint8_t>(value));
			}
			size_t position = 0x1aa;
			for(size_t group = 0; group < raw.size(); group += 7)
			{
				uint8_t highBits = 0;
				const auto count = std::min<size_t>(7, raw.size() - group);
				for(size_t bit = 0; bit < count; ++bit)
					if(raw[group + bit] & 0x80)
						highBits |= static_cast<uint8_t>(1u << (6u - bit));
				kit[position++] = highBits;
				for(size_t bit = 0; bit < count; ++bit)
					kit[position++] = raw[group + bit] & 0x7f;
			}
		}
		// The name: 16 raw bytes at 0x0a, here padded with spaces and ended by a zero
		const std::string name = "BROKEN DUB  ";
		std::copy(name.begin(), name.end(), kit.begin() + 0x0a);
		// Master effects: 32 raw bytes at 0x487, reverb, echo, EQ, dynamix (mdEditorFirmwareTest)
		for(uint8_t index = 0; index < 32; ++index)
			kit[0x487 + index] = static_cast<uint8_t>(index + 1);
		finishDump(kit);
		const auto parsedKit = parseKitDump(md::MachineModel::Machinedrum, kit);
		require(parsedKit && parsedKit->slot == 4
			&& parsedKit->parameters.size() == 400, "wrong MD Kit parameter count");
		require(parsedKit->parameters[0]
			== ParameterChange{machinedrum::Synthesis, 0, 0, 0},
			"wrong first MD Kit parameter");
		require(parsedKit->parameters[399]
			== ParameterChange{machinedrum::Level, 15, 0, 115},
			"wrong last MD Kit parameter");
		require(parsedKit->machines.size() == machinedrum::TrackCount, "MD Kit machines missing");
		require(parsedKit->machines[0] == 32 && parsedKit->machines[15] == 47, "wrong MD Kit machine ids");
		require(parsedKit->machines[1] == 176, "MD ROM-33 lost the top bit of its 7-bit group");
		require(parsedKit->machines[2] == 16, "MD TONAL flag leaked into the machine id");
		require(parsedKit->name == "BROKEN DUB", ("wrong MD Kit name: \"" + parsedKit->name + "\"").c_str());
		require(parsedKit->masterEffects.has_value(), "MD Kit master effects missing");
		const auto& effects = *parsedKit->masterEffects;
		require(effects[static_cast<uint8_t>(MasterEffect::Reverb)][0] == 1 && effects[static_cast<uint8_t>(MasterEffect::Echo)][0] == 9
			&& effects[static_cast<uint8_t>(MasterEffect::Eq)][7] == 24 && effects[static_cast<uint8_t>(MasterEffect::Dynamix)][7] == 32,
			"MD master effects not read in the dump's reverb, echo, EQ, dynamix order");
		require(masterEffectChange(MasterEffect::Echo, 0, 5) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x5d, 0, 5, 0xf7}
			&& masterEffectChange(MasterEffect::Reverb, 2, 64) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x5e, 2, 64, 0xf7}
			&& masterEffectChange(MasterEffect::Dynamix, 7, 127) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x60, 7, 127, 0xf7},
			"wrong master effect messages");
		require(!masterEffectChange(MasterEffect::Eq, 8, 0) && !masterEffectChange(MasterEffect::Eq, 0, 128),
			"master effect message accepted a parameter or value out of range");
		{
			// A shorter dump (older format) holds no master effects
			auto shortKit = kit;
			shortKit.resize(0x4a7);
			finishDump(shortKit);
			const auto parsed = parseKitDump(md::MachineModel::Machinedrum, shortKit);
			require(parsed && !parsed->masterEffects, "master effects read from a dump of another size");
		}
		kit[100] ^= 1;
		require(!parseKitDump(md::MachineModel::Machinedrum, kit),
			"accepted corrupt MD Kit checksum");
	}

	void testMachineAssignment()
	{
		using md::automation::sysex::Message;
		using md::automation::sysex::assignMachine;
		const auto md = md::MachineModel::Machinedrum;
		const auto mm = md::MachineModel::Monomachine;
		// Same wire form as the firmware tests: track, id, UW flag.
		require(assignMachine(md, 3, 16) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x5b, 3, 16, 0, 0xf7},
			"wrong MD ASSIGN MACHINE for TRX-BD");
		require(assignMachine(md, 0, 128) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x5b, 0, 0, 1, 0xf7},
			"wrong MD ASSIGN MACHINE for ROM-01");
		require(assignMachine(md, 15, 191) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x5b, 15, 63, 1, 0xf7},
			"wrong MD ASSIGN MACHINE for ROM-48");
		require(assignMachine(mm, 5, 32) == Message{0xf0, 0, 0x20, 0x3c, 3, 0, 0x5b, 5, 32, 0, 0xf7},
			"wrong MM ASSIGN MACHINE for DPRO-DDRW");
		require(!assignMachine(md, 16, 16), "MD accepted track 17");
		require(!assignMachine(mm, 6, 3), "MM accepted track 7");
		require(!assignMachine(md, 0, 6), "MD accepted an id that is no machine");
		require(!assignMachine(md, 0, 4), "MD offered GND-SW, which the manual does not list");
		require(!assignMachine(mm, 0, 18), "MM accepted an id that is no machine");

		// Every family has machines, ids are unique, and lookups agree with the list.
		for(const auto model : {md, mm})
		{
			const auto& list = md::machines::machines(model);
			std::vector<uint16_t> ids;
			for(const auto& machine : list)
			{
				ids.push_back(machine.id);
				require(md::machines::find(model, machine.id) == &machine, "machine lookup mismatch");
				require(machine.family < md::machines::families(model).size(), "machine family out of range");
			}
			std::sort(ids.begin(), ids.end());
			require(std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "duplicate machine id");
			for(size_t family = 0; family < md::machines::families(model).size(); ++family)
				require(std::any_of(list.begin(), list.end(), [family](const auto& _m) { return _m.family == family; }),
					"machine family without machines");
		}
		// The Machinedrum's 142, then MDX X.20's 16 User Machines, USR-01 to USR-16
		require(md::machines::machines(md).size() == 142 + 16 && md::machines::machines(mm).size() == 22,
			"unexpected machine count");
		require(md::machines::find(mm, 11)->name == "VO-VO-6" && md::machines::find(md, 176)->name == "ROM-33",
			"wrong machine names");

		// Parameter names from the firmware's machine table: every machine offered has them, as the OS has it
		for(const auto model : {md, mm})
		{
			for(const auto& machine : md::machines::machines(model))
				require(!machine.assignable || md::machines::parameterNames(model, machine.id), "machine offered without parameter names");
		}
		const auto name = [](const md::MachineModel _model, const uint16_t _id, const size_t _index)
		{
			const auto* names = md::machines::parameterNames(_model, _id);
			return names ? std::string((*names)[_index]) : std::string("?");
		};
		require(name(md, 16, 0) == "PTCH" && name(md, 16, 7) == "CLIP" && name(md, 1, 4).empty(), "wrong TRX-BD or GND-SN names");
		require(name(md, 150, 2) == "HOLD" && name(md, 162, 2) == "HOLD" && name(md, 166, 0) == "MLEV" && name(md, 105, 0) == "NOTE",
			"ROM, RAM or MID machines do not share their family's names");
		require(name(mm, 3, 0) == "PW" && name(mm, 4, 3).empty() && name(mm, 18, 0) == "CNTR", "wrong Monomachine names");
		// INP-CA: no table holds its names; GND-SW: MDX X.20's (md-drums: the map of X.20's built-in Machines)
		require(!md::machines::parameterNames(md, 86) && name(md, 4, 6) == "SKEW",
			"names for INP-CA, or GND-SW without X.20's");
	}

	void testBackupFile(const char* const _path, const md::MachineModel _model)
	{
		std::ifstream file(_path, std::ios::binary);
		require(file.good(), "could not open optional SysEx backup");
		const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(file), {});
		size_t globalCount = 0;
		size_t kitCount = 0;
		for(size_t begin = 0; begin < bytes.size();)
		{
			begin = static_cast<size_t>(std::find(bytes.begin() + begin, bytes.end(),
				0xf0) - bytes.begin());
			if(begin == bytes.size())
				break;
			const auto endIterator = std::find(bytes.begin() + begin, bytes.end(), 0xf7);
			if(endIterator == bytes.end())
				break;
			const auto end = static_cast<size_t>(endIterator - bytes.begin());
			const md::automation::sysex::Message message(bytes.begin() + begin,
				bytes.begin() + end + 1);
			if(message.size() > 6 && message[6] == 0x50)
			{
				const auto parsed = md::automation::sysex::parseGlobalDump(
					_model, message);
				require(parsed.has_value(), "could not parse real Global dump");
				require(parsed->slot == message[9],
					"real Global dump reported its format version as its slot");
				++globalCount;
			}
			else if(message.size() > 6 && message[6] == 0x52)
			{
				const auto parsed = md::automation::sysex::parseKitDump(_model, message);
				require(parsed.has_value(), "could not parse real Kit dump");
				for(const auto machine : parsed->machines)
					require(md::machines::find(_model, machine) != nullptr, "real Kit dump holds an unknown machine id");
				require(parsed->slot == message[9],
					"real Kit dump reported its format version as its slot");
				++kitCount;
			}
			else if(message.size() > 6 && message[6] == 0x67 && _model == md::MachineModel::Machinedrum)
			{
				const auto parsed = md::automation::sysex::parseMdPatternDump(message);
				require(parsed.has_value(), "could not parse real MD pattern dump");
				require(parsed->slot == message[9], "real pattern dump reported the wrong slot");
			}
			begin = end + 1;
		}
		require(globalCount > 0, "real backup contained no Global dumps");
		require(kitCount > 0, "real backup contained no Kit dumps");
	}

}

int main(const int _argc, const char* const* _argv)
{
	testMachinedrum();
	testSysexRequestsAndStatus();
	testMidiRunningStatus();
	testMachinedrumDumps();
	testMachineAssignment();
	if(_argc > 1)
		testBackupFile(_argv[1], md::MachineModel::Machinedrum);
	std::cout << "automationMidiTest: PASS\n";
	return 0;
}
