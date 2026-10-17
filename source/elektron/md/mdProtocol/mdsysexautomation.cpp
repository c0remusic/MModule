#include "mdsysexautomation.h"

#include "mdkit.h"
#include "mdkitchange.h"
#include "mdmachines.h"
#include "mdsysexcodec.h"

#include <algorithm>
#include <utility>

namespace md::automation::sysex::codec
{
	uint8_t product(const MachineModel _model)
	{
		return _model == MachineModel::Monomachine ? 0x03 : 0x02;
	}

	bool hasHeader(const MachineModel _model, const MessageView _message,
		const uint8_t _command)
	{
		return _message.size() >= 8 && _message[0] == 0xf0
			&& _message[1] == 0x00 && _message[2] == 0x20
			&& _message[3] == 0x3c && _message[4] == product(_model)
			&& _message[5] == 0x00 && _message[6] == _command
			&& _message.back() == 0xf7;
	}

	Message header(const MachineModel _model, const uint8_t _command, const size_t _size)
	{
		Message result;
		result.reserve(_size);
		result.insert(result.end(), {0xf0, 0x00, 0x20, 0x3c, product(_model), 0x00, _command});
		return result;
	}

	Message message(const MachineModel _model, const uint8_t _command, const std::initializer_list<uint8_t> _body)
	{
		auto result = header(_model, _command, 8 + _body.size());
		result.insert(result.end(), _body);
		result.push_back(0xf7);
		return result;
	}

	bool isSevenBit(const MessageView _message)
	{
		return std::none_of(_message.begin() + 1, _message.end() - 1,
			[](const uint8_t _value) { return _value > 0x7f; });
	}

	bool validDump(const MachineModel _model, const MessageView _message,
		const uint8_t _command)
	{
		if(_message.size() < 13 || !hasHeader(_model, _message, _command))
			return false;
		if(!isSevenBit(_message))
			return false;

		const auto checksumPosition = _message.size() - 5;
		uint32_t sum = 0;
		for(size_t i = 9; i < checksumPosition; ++i)
			sum += _message[i];
		const auto checksum = static_cast<uint16_t>(
			(_message[checksumPosition] << 7) | _message[checksumPosition + 1]);
		if((sum & 0x3fff) != checksum)
			return false;

		const auto length = static_cast<uint16_t>(
			(_message[checksumPosition + 2] << 7) | _message[checksumPosition + 3]);
		return length == _message.size() - 10;
	}

	void append7Bit(std::vector<uint8_t>& _out, const uint8_t* _data, const size_t _count)
	{
		for(size_t position = 0; position < _count; position += 7)
		{
			const auto count = std::min<size_t>(7, _count - position);
			uint8_t highBits = 0;
			for(size_t bit = 0; bit < count; ++bit)
			{
				if(_data[position + bit] & 0x80)
					highBits |= static_cast<uint8_t>(1u << (6u - bit));
			}
			_out.push_back(highBits);
			for(size_t bit = 0; bit < count; ++bit)
				_out.push_back(static_cast<uint8_t>(_data[position + bit] & 0x7f));
		}
	}

	bool read7Bit(const MessageView _message, size_t& _position, const size_t _count, uint8_t* _out)
	{
		for(size_t done = 0; done < _count;)
		{
			if(_position >= _message.size())
				return false;
			const auto highBits = _message[_position++];
			for(uint8_t bit = 0; bit < 7 && done < _count; ++bit, ++done)
			{
				if(_position >= _message.size())
					return false;
				auto value = _message[_position++];
				if(highBits & (1u << (6u - bit)))
					value |= 0x80;
				_out[done] = value;
			}
		}
		return true;
	}

	void finishDump(Message& _message)
	{
		uint32_t sum = 0;
		for(size_t i = 9; i < _message.size(); ++i)
			sum += _message[i];
		const auto length = _message.size() - 5;
		_message.push_back(static_cast<uint8_t>((sum >> 7) & 0x7f));
		_message.push_back(static_cast<uint8_t>(sum & 0x7f));
		_message.push_back(static_cast<uint8_t>((length >> 7) & 0x7f));
		_message.push_back(static_cast<uint8_t>(length & 0x7f));
		_message.push_back(0xf7);
	}

	std::string kitName(const uint8_t* _name, const size_t _size)
	{
		std::string name;
		for(size_t index = 0; index < _size && _name[index] != 0; ++index)
			name.push_back(_name[index] >= 0x20 && _name[index] < 0x7f ? static_cast<char>(_name[index]) : '?');
		while(!name.empty() && name.back() == ' ')
			name.pop_back();
		return name;
	}
}

namespace md::automation::sysex
{
	using namespace codec;

	namespace
	{
		constexpr uint8_t g_globalDump = 0x50;
		constexpr uint8_t g_globalRequest = 0x51;
		constexpr uint8_t g_kitDump = 0x52;
		constexpr uint8_t g_kitRequest = 0x53;
		constexpr uint8_t g_kitLoad = 0x58;
		constexpr uint8_t g_kitSave = 0x59;
		constexpr uint8_t g_patternDump = 0x67;
		constexpr uint8_t g_patternRequest = 0x68;
		constexpr uint8_t g_assignMachine = 0x5b;
		constexpr uint8_t g_trackRouting = 0x5c;
		constexpr uint8_t g_statusRequest = 0x70;
		constexpr uint8_t g_setStatus = 0x71;
		constexpr uint8_t g_statusResponse = 0x72;

		Message request(const MachineModel _model, const uint8_t _command,
			const uint8_t _value)
		{
			return message(_model, _command, {static_cast<uint8_t>(_value & 0x7f)});
		}

		bool validStatusValue(const MachineModel _model,
			const StatusParameter _parameter, const uint8_t _value)
		{
			switch(_parameter)
			{
			case StatusParameter::Global:
				return _value < 8;
			case StatusParameter::Kit:
				return _value < (_model == MachineModel::Monomachine ? 128 : 64);
			case StatusParameter::Pattern:
				return _value < 128;
			}
			return false;
		}

		bool validStatusParameter(const uint8_t _parameter)
		{
			return _parameter == static_cast<uint8_t>(StatusParameter::Global)
				|| _parameter == static_cast<uint8_t>(StatusParameter::Kit)
				|| _parameter == static_cast<uint8_t>(StatusParameter::Pattern);
		}

		std::optional<StatusResponse> parseStatus(const MachineModel _model,
			const MessageView _message, const uint8_t _command)
		{
			if(_message.size() != 10 || !hasHeader(_model, _message, _command) || !isSevenBit(_message))
				return std::nullopt;
			const auto parameter = _message[7];
			if(!validStatusParameter(parameter))
				return std::nullopt;
			const auto typedParameter = static_cast<StatusParameter>(parameter);
			return validStatusValue(_model, typedParameter, _message[8])
				? std::optional<StatusResponse>(StatusResponse{typedParameter, _message[8]})
				: std::nullopt;
		}

		// At most 4096 decoded bytes, more than a Global or a Kit holds
		std::optional<std::vector<uint8_t>> decodeMonomachinePayload(const MessageView _message)
		{
			constexpr size_t maximum = 4096;
			const auto end = _message.size() - 5;
			std::vector<uint8_t> rle;
			rle.reserve(end - 10);
			for(size_t position = 10; position < end;)
			{
				const auto highBits = _message[position++];
				for(uint8_t bit = 0; bit < 7 && position < end; ++bit)
				{
					auto value = _message[position++];
					if(highBits & (1u << (6u - bit)))
						value |= 0x80;
					rle.push_back(value);
				}
			}

			std::vector<uint8_t> decoded;
			for(size_t position = 0; position < rle.size(); ++position)
			{
				const auto value = rle[position];
				if((value & 0x80) == 0)
				{
					decoded.push_back(value);
					continue;
				}

				const auto count = static_cast<size_t>(value & 0x7f);
				if(count == 0 || ++position >= rle.size())
					return std::nullopt;
				if(decoded.size() + count > maximum)
					return std::nullopt;
				decoded.insert(decoded.end(), count, rle[position]);
			}
			return decoded;
		}

		// Inverse of decodeMonomachinePayload: run-length pass, then 7-bit groups.
		std::vector<uint8_t> encodeMonomachinePayload(const std::vector<uint8_t>& _decoded)
		{
			// A byte with bit 7 set is the repeat count of the byte after it. Single
			// bytes stay literal unless their own bit 7 would read as a count.
			std::vector<uint8_t> rle;
			rle.reserve(_decoded.size() + 16);
			for(size_t position = 0; position < _decoded.size();)
			{
				const auto value = _decoded[position];
				size_t count = 1;
				while(count < 0x7f && position + count < _decoded.size()
					&& _decoded[position + count] == value)
					++count;
				if(count == 1 && (value & 0x80) == 0)
					rle.push_back(value);
				else
				{
					rle.push_back(static_cast<uint8_t>(0x80 | count));
					rle.push_back(value);
				}
				position += count;
			}

			std::vector<uint8_t> packed;
			packed.reserve(rle.size() + rle.size() / 7 + 1);
			append7Bit(packed, rle.data(), rle.size());
			return packed;
		}

		// Machinedrum Global: raw bytes follow the 7-bit packed key map. In the
		// sync byte (OS 1.63, Global version 6), bit 0 selects the EXTERNAL tempo
		// source and bit 4 turns CTRL IN (start, stop, song position) off; the
		// factory Global has both clear. Bits 5 and 6 are the outputs.
		constexpr size_t g_mdGlobalSize = 0xc5;
		constexpr size_t g_mdSyncPosition = 0xb2;
		constexpr uint8_t g_mdClockIn = 0x01;
		constexpr uint8_t g_mdTransportInOff = 0x10;
		// Before the key map: one raw byte per track, 0 to 5 for outputs A to F, 6 for MAIN.
		constexpr size_t g_mdRoutingPosition = 0x0a;

		// Monomachine Global, decoded payload: five MIDI channels, then a byte
		// whose bit 0 is CLOCK IN (its bits 4 to 6 are CTRL IN, CLOCK OUT and
		// CTRL OUT), then TRANSPORT IN as a byte of its own.
		constexpr size_t g_mmSyncIndex = 5;
		constexpr size_t g_mmTransportInIndex = 6;
		constexpr uint8_t g_mmClockIn = 0x01;
	}

	Message statusRequest(const MachineModel _model,
		const StatusParameter _parameter)
	{
		return request(_model, g_statusRequest, static_cast<uint8_t>(_parameter));
	}

	Message globalRequest(const MachineModel _model, const uint8_t _slot)
	{
		return request(_model, g_globalRequest, _slot);
	}

	Message kitRequest(const MachineModel _model, const uint8_t _slot)
	{
		return request(_model, g_kitRequest, _slot);
	}

	namespace
	{
		// Pattern dump sections after the 6 plain bytes: the lock rows, then accent,
		// slide and swing edit flags and per-track patterns. The 64-step form adds
		// one 7-bit run: trigs, accent/slide/swing, lock rows and per-track patterns
		// of steps 33 to 64 (MCL's MDPattern).
		constexpr size_t g_patternShortSize = 0xacb;
		constexpr size_t g_patternLongSize = 0x1522;
		constexpr size_t g_patternTailSize = 204;
		// In the tail, after the three EDIT ALL words: the tracks' accent, slide and swing masks
		constexpr size_t g_patternTrackFlags = 12;
		constexpr size_t g_patternExtensionSize = 64 + 12 + 64 * 32 + 192;
		// In the extension: steps 33 to 64 of every track's flags, of the lock rows, of the tracks' flags
		constexpr size_t g_patternExtensionFlags = 64;
		constexpr size_t g_patternExtensionLocks = 64 + 12;
		constexpr size_t g_patternExtensionTrackFlags = 64 + 12 + 64 * 32;
		constexpr size_t g_patternRows = 64;
		constexpr uint8_t g_noLock = 0xff;
	}

	std::optional<PatternDump> parseMdPatternDump(const MessageView _message)
	{
		// 32-step form 0xacb bytes; the 64-step form appends the second half.
		if(!validDump(MachineModel::Machinedrum, _message, g_patternDump)
			|| (_message.size() != g_patternShortSize && _message.size() != g_patternLongSize))
			return std::nullopt;
		PatternDump result;
		result.slot = _message[9];
		if(result.slot >= 128)
			return std::nullopt;
		result.steps = _message.size() == g_patternLongSize ? 64 : 32;

		// Trig and lock-row masks: 16 big-endian 32-bit values each, in their own 7-bit runs.
		size_t position = 0x0a;
		std::array<uint8_t, 64> raw{};
		if(!read7Bit(_message, position, raw.size(), raw.data()))
			return std::nullopt;
		for(size_t track = 0; track < 16; ++track)
			result.trigs[track] = readBigEndian32(&raw[track * 4]);
		if(!read7Bit(_message, position, raw.size(), raw.data()))
			return std::nullopt;
		for(size_t track = 0; track < 16; ++track)
			result.lockMasks[track] = readBigEndian32(&raw[track * 4]);
		// Accent, slide and swing patterns (steps 1 to 32) and the swing amount, then six plain
		// bytes: accent amount, length, double tempo, scale, kit, locked rows.
		std::array<uint8_t, 16> flags{};
		if(!read7Bit(_message, position, flags.size(), flags.data()))
			return std::nullopt;
		for(uint8_t flag = 0; flag < StepFlagCount; ++flag)
			result.flags[flag] = readBigEndian32(&flags[flag * 4]);
		result.swingAmount = readBigEndian32(&flags[12]);
		// The row count byte is not needed: rows follow the masks, as on the machine.
		result.accentAmount = _message[position];
		result.length = _message[position + 1];
		result.kit = _message[position + 4];
		position += 6;
		if(result.length == 0 || result.length > 64)
			return std::nullopt;

		std::vector<uint8_t> locks(g_patternRows * 32);
		if(!read7Bit(_message, position, locks.size(), locks.data()))
			return std::nullopt;
		result.lockRows.resize(g_patternRows);
		for(size_t row = 0; row < g_patternRows; ++row)
		{
			result.lockRows[row].fill(g_noLock);
			std::copy_n(locks.begin() + row * 32, 32, result.lockRows[row].begin());
		}
		// The tail: per kind, whether every track follows one mask (MCL: EDIT ALL), then the tracks'
		// own masks, steps 1 to 32
		std::vector<uint8_t> tail(g_patternTailSize);
		if(!read7Bit(_message, position, tail.size(), tail.data()))
			return std::nullopt;
		for(uint8_t flag = 0; flag < StepFlagCount; ++flag)
		{
			result.flagPerTrack[flag] = readBigEndian32(&tail[flag * 4]) == 0;
			for(size_t track = 0; track < 16; ++track)
				result.trackFlags[flag][track] = readBigEndian32(&tail[g_patternTrackFlags + (flag * 16 + track) * 4]);
		}
		// The 64-step form: steps 33 to 64 of the trigs, the flags and each lock row, after the tail
		if(result.steps == 64)
		{
			std::vector<uint8_t> extension(g_patternExtensionSize);
			if(!read7Bit(_message, position, extension.size(), extension.data()))
				return std::nullopt;
			for(size_t track = 0; track < 16; ++track)
				result.trigs[track] |= static_cast<uint64_t>(readBigEndian32(&extension[track * 4])) << 32;
			for(uint8_t flag = 0; flag < StepFlagCount; ++flag)
			{
				const auto everyTrack = readBigEndian32(&extension[g_patternExtensionFlags + flag * 4]);
				result.flags[flag] |= static_cast<uint64_t>(everyTrack) << 32;
				for(size_t track = 0; track < 16; ++track)
				{
					result.trackFlags[flag][track] |= static_cast<uint64_t>(readBigEndian32(
						&extension[g_patternExtensionTrackFlags + (flag * 16 + track) * 4])) << 32;
				}
			}
			for(size_t row = 0; row < g_patternRows; ++row)
				std::copy_n(&extension[g_patternExtensionLocks + row * 32], 32, result.lockRows[row].begin() + 32);
		}
		// Parameters 24 and up belong to lock rows the classic format does not have.
		for(auto& mask : result.lockMasks)
			mask &= 0x00ffffffu;
		return result;
	}

	bool isReadOnlyRequest(const MachineModel _model, const MessageView _message)
	{
		if(_message.size() != 9 || !isSevenBit(_message) || !hasHeader(_model, _message, _message[6]))
			return false;
		switch(_message[6])
		{
		case g_globalRequest:
			return validStatusValue(_model, StatusParameter::Global, _message[7]);
		case g_kitRequest:
			return validStatusValue(_model, StatusParameter::Kit, _message[7]);
		case g_patternRequest:
			return validStatusValue(_model, StatusParameter::Pattern, _message[7]);
		case g_statusRequest:
			return validStatusParameter(_message[7]);
		default:
			return false;
		}
	}

	Message kitSave(const MachineModel _model, const uint8_t _slot)
	{
		return request(_model, g_kitSave, _slot);
	}

	Message kitLoad(const MachineModel _model, const uint8_t _slot)
	{
		return request(_model, g_kitLoad, _slot);
	}

	Message patternSelect(const MachineModel _model, const uint8_t _slot)
	{
		return message(_model, g_setStatus,
			{static_cast<uint8_t>(StatusParameter::Pattern), static_cast<uint8_t>(_slot & 0x7f)});
	}

	std::optional<StatusResponse> parseStatusResponse(const MachineModel _model,
		const MessageView _message)
	{
		return parseStatus(_model, _message, g_statusResponse);
	}

	std::optional<StatusResponse> parseSetStatus(const MachineModel _model,
		const MessageView _message)
	{
		return parseStatus(_model, _message, g_setStatus);
	}

	std::optional<GlobalDump> parseGlobalDump(const MachineModel _model,
		const MessageView _message)
	{
		if(!validDump(_model, _message, g_globalDump))
			return std::nullopt;
		// Dump headers are command, format version, revision, original position.
		// The request correlation identity is the original position, not the format
		// version (which happens to be a small in-range value on both machines).
		const auto slot = _message[9];
		if(!validStatusValue(_model, StatusParameter::Global, slot))
			return std::nullopt;

		uint8_t channel = 0;
		std::optional<TrackOutputs> outputs;
		if(_model == MachineModel::Machinedrum)
		{
			constexpr size_t baseChannelPosition = 0xad;
			if(_message.size() <= baseChannelPosition)
				return std::nullopt;
			channel = _message[baseChannelPosition];
			TrackOutputs routing{};
			bool valid = true;
			for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
			{
				const auto output = _message[g_mdRoutingPosition + track];
				valid &= output <= static_cast<uint8_t>(TrackOutput::Main);
				routing[track] = static_cast<TrackOutput>(output);
			}
			if(valid)
				outputs = routing;
		}
		else
		{
			const auto decoded = decodeMonomachinePayload(_message);
			if(!decoded || decoded->size() < 2)
				return std::nullopt;
			channel = (*decoded)[1];
		}

		return channel < 16 || channel == 0x7f
			? std::optional<GlobalDump>(GlobalDump{slot, channel, outputs}) : std::nullopt;
	}

	MasterEffects masterEffectsFromKit(const uint8_t* _bytes)
	{
		MasterEffects values{};
		for(uint8_t effect = 0; effect < MasterEffectCount; ++effect)
			std::copy_n(_bytes + mdKitMasterIndex(effect, 0), MasterEffectParameters, values[effect].begin());
		return values;
	}

	std::optional<KitDump> parseKitDump(
		const MachineModel _model, const MessageView _message)
	{
		if(!validDump(_model, _message, g_kitDump))
			return std::nullopt;
		const auto slot = _message[9];
		if(!validStatusValue(_model, StatusParameter::Kit, slot))
			return std::nullopt;

		std::vector<ParameterChange> result;
		if(_model == MachineModel::Machinedrum)
		{
			if(_message.size() <= mdKitDumpLayout::Levels + machinedrum::TrackCount)
				return std::nullopt;
			result.reserve(machinedrum::TrackCount * 25);
			for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
			{
				for(uint8_t page = machinedrum::Synthesis;
					page <= machinedrum::Routing; ++page)
				{
					for(uint8_t index = 0; index < 8; ++index)
					{
						const auto offset = mdKitDumpLayout::Parameters + track * 24 + page * 8 + index;
						result.push_back({page, track, index, _message[offset]});
					}
				}
				result.push_back({machinedrum::Level, track, 0,
					_message[mdKitDumpLayout::Levels + track]});
			}

			// A packed field is read when the dump holds it whole up to _end, then its checksum, length and F7
			const auto holds = [&](const size_t _end) { return _message.size() >= _end + 5; };

			// Machine assignments follow the levels: 16 big-endian 32-bit values. The id
			// is the low byte; the upper bits carry flags such as TONAL.
			std::vector<uint16_t> machines;
			std::array<uint8_t, machinedrum::TrackCount * 4> words{};
			size_t position = mdKitDumpLayout::Machines;
			if(holds(mdKitDumpLayout::Lfos) && read7Bit(_message, position, words.size(), words.data()))
			{
				for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
					machines.push_back(words[track * 4 + 3]);
			}
			// Then the LFOs, 36 bytes a track: the five fields $62 sets, then the LFO's state
			std::optional<std::array<LfoSettings, machinedrum::TrackCount>> lfos;
			std::array<uint8_t, machinedrum::TrackCount * MdKit::LfoSize> lfoBytes{};
			position = mdKitDumpLayout::Lfos;
			if(holds(mdKitDumpLayout::MasterEffects) && read7Bit(_message, position, lfoBytes.size(), lfoBytes.data()))
			{
				lfos.emplace();
				for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
				{
					const auto* lfo = &lfoBytes[track * MdKit::LfoSize];
					(*lfos)[track] = {lfo[0], lfo[1], lfo[2], lfo[3], lfo[4]};
				}
			}
			std::optional<MasterEffects> effects;
			if(_message.size() == MdKitDumpSize)
				effects = masterEffectsFromKit(&_message[mdKitDumpLayout::MasterEffects]);
			return KitDump{slot, std::move(result), std::move(machines),
				kitName(&_message[mdKitDumpLayout::Name], MdKit::NameSize), effects, lfos};
		}

		const auto decoded = decodeMonomachinePayload(_message);
		constexpr size_t levelPosition = 0x0b;
		constexpr size_t parameterPosition = 0x11;
		constexpr size_t parameterStride = 72;
		if(!decoded || decoded->size() < parameterPosition
			+ monomachine::TrackCount * parameterStride)
			return std::nullopt;
		result.reserve(monomachine::TrackCount * 57);
		for(uint8_t track = 0; track < monomachine::TrackCount; ++track)
		{
			for(uint8_t page = monomachine::Synthesis;
				page <= monomachine::Lfo3; ++page)
			{
				for(uint8_t index = 0; index < 8; ++index)
				{
					const auto offset = parameterPosition + track * parameterStride
						+ page * 8 + index;
					result.push_back({page, track, index, (*decoded)[offset]});
				}
			}
			result.push_back({monomachine::Level, track, 0,
				(*decoded)[levelPosition + track]});
		}

		// One machine id byte per track right after the parameters.
		constexpr size_t machinePosition = parameterPosition + monomachine::TrackCount * parameterStride;
		std::vector<uint16_t> machines;
		if(decoded->size() >= machinePosition + monomachine::TrackCount)
		{
			for(uint8_t track = 0; track < monomachine::TrackCount; ++track)
				machines.push_back((*decoded)[machinePosition + track]);
		}
		// The first 11 decoded bytes, before the levels
		return KitDump{slot, std::move(result), std::move(machines), kitName(decoded->data(), levelPosition)};
	}

	std::optional<Message> assignMachine(const MachineModel _model, const uint8_t _track,
		const uint16_t _machine)
	{
		const auto trackCount = _model == MachineModel::Monomachine
			? monomachine::TrackCount : machinedrum::TrackCount;
		const auto* machine = machines::find(_model, _machine);
		if(_track >= trackCount || !machine || !machine->assignable)
			return std::nullopt;
		if(_model == MachineModel::Monomachine)
			return message(_model, g_assignMachine, {_track, static_cast<uint8_t>(_machine), 0x00});
		return kitChangeMessage({KitChange::Kind::Machine, _track, KitChange::AssignScope,
			static_cast<uint8_t>(_machine)});
	}

	std::optional<Message> masterEffectChange(const MasterEffect _effect, const uint8_t _parameter, const uint8_t _value)
	{
		const auto effect = static_cast<uint8_t>(_effect);
		if(effect >= MasterEffectCount || _parameter >= MasterEffectParameters)
			return std::nullopt;
		const auto index = static_cast<uint8_t>(effect * MasterEffectParameters + _parameter);
		return kitChangeMessage({KitChange::Kind::Master, 0, index, _value});
	}

	std::optional<Message> lfoChange(const uint8_t _track, const uint8_t _field, const uint8_t _value)
	{
		if(_field >= LfoSettings::FieldMaximum.size())
			return std::nullopt;
		return kitChangeMessage({KitChange::Kind::Lfo, _track, _field, _value});
	}

	std::optional<Message> trackRouting(const uint8_t _track, const TrackOutput _output)
	{
		if(_track >= machinedrum::TrackCount || _output > TrackOutput::Main)
			return std::nullopt;
		return message(MachineModel::Machinedrum, g_trackRouting, {_track, static_cast<uint8_t>(_output)});
	}

	Message globalReload(const MachineModel _model, const uint8_t _slot)
	{
		return message(_model, g_setStatus,
			{static_cast<uint8_t>(StatusParameter::Global), static_cast<uint8_t>(_slot & 0x07)});
	}

	std::optional<GlobalSync> parseGlobalSync(const MachineModel _model,
		const MessageView _message)
	{
		if(!parseGlobalDump(_model, _message))
			return std::nullopt;
		if(_model == MachineModel::Machinedrum)
		{
			if(_message.size() != g_mdGlobalSize)
				return std::nullopt;
			const auto sync = _message[g_mdSyncPosition];
			return GlobalSync{(sync & g_mdClockIn) != 0, (sync & g_mdTransportInOff) == 0};
		}
		const auto decoded = decodeMonomachinePayload(_message);
		if(!decoded || decoded->size() <= g_mmTransportInIndex)
			return std::nullopt;
		return GlobalSync{((*decoded)[g_mmSyncIndex] & g_mmClockIn) != 0,
			(*decoded)[g_mmTransportInIndex] != 0};
	}

	std::optional<Message> withGlobalSync(const MachineModel _model,
		const MessageView _message, const GlobalSync _sync)
	{
		if(!parseGlobalSync(_model, _message))
			return std::nullopt;
		if(_model == MachineModel::Machinedrum)
		{
			Message result(_message.begin(), _message.end() - 5);
			auto& sync = result[g_mdSyncPosition];
			sync = static_cast<uint8_t>((sync & ~(g_mdClockIn | g_mdTransportInOff))
				| (_sync.clockIn ? g_mdClockIn : 0) | (_sync.transportIn ? 0 : g_mdTransportInOff));
			finishDump(result);
			return result;
		}
		auto decoded = *decodeMonomachinePayload(_message);
		auto& sync = decoded[g_mmSyncIndex];
		sync = static_cast<uint8_t>((sync & ~g_mmClockIn) | (_sync.clockIn ? g_mmClockIn : 0));
		decoded[g_mmTransportInIndex] = _sync.transportIn ? 1 : 0;
		Message result(_message.begin(), _message.begin() + 10);
		const auto packed = encodeMonomachinePayload(decoded);
		result.insert(result.end(), packed.begin(), packed.end());
		finishDump(result);
		return result;
	}
}
