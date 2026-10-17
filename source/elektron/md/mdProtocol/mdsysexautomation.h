#pragma once

#include "mdautomation.h"
#include "mdlfosettings.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace md::automation::sysex
{
	using Message = std::vector<uint8_t>;
	class MessageView
	{
	public:
		template<typename Allocator>
		MessageView(const std::vector<uint8_t, Allocator>& _message)
			: m_data(_message.data()), m_size(_message.size()) {}
		MessageView(const uint8_t* _data, const size_t _size) : m_data(_data), m_size(_size) {}

		const uint8_t* begin() const { return m_data; }
		const uint8_t* end() const { return m_data + m_size; }
		const uint8_t& operator[](size_t _index) const { return m_data[_index]; }
		const uint8_t& back() const { return m_data[m_size - 1]; }
		size_t size() const { return m_size; }

	private:
		const uint8_t* m_data;
		size_t m_size;
	};

	enum class StatusParameter : uint8_t
	{
		Global = 0x01,
		Kit = 0x02,
		Pattern = 0x04
	};

	struct StatusResponse
	{
		StatusParameter parameter;
		uint8_t value;
	};

	// Where a Machinedrum track plays: one of the six individual outputs, or the
	// main pair (stereo, pan and master effects).
	enum class TrackOutput : uint8_t
	{
		A, B, C, D, E, F,
		Main
	};
	using TrackOutputs = std::array<TrackOutput, machinedrum::TrackCount>;

	struct GlobalDump
	{
		uint8_t slot;
		uint8_t baseChannel;
		// Machinedrum only: the output of each track.
		std::optional<TrackOutputs> trackOutputs;
	};

	// The Machinedrum Kit's four master effects, in the order of their SysEx ids
	// ($5D to $60), 8 parameters each.
	enum class MasterEffect : uint8_t
	{
		Echo,       // RHYTHM ECHO
		Reverb,     // GATE BOX REVERB
		Eq,
		Dynamix
	};
	constexpr uint8_t MasterEffectCount = 4;
	constexpr uint8_t MasterEffectParameters = 8;
	using MasterEffects = std::array<std::array<uint8_t, MasterEffectParameters>, MasterEffectCount>;

	// The block of 8 bytes where a Kit keeps each master effect, by MasterEffect: the Kit holds reverb first, then
	// echo, EQ, dynamix (mdEditorFirmwareTest checks the order)
	constexpr std::array<uint8_t, MasterEffectCount> MdKitMasterEffectBlock{1, 0, 2, 3};
	// The index of parameter _parameter (0..7) of master effect _effect (MasterEffect) among a Kit's 32 bytes of
	// master effects
	constexpr uint8_t mdKitMasterIndex(const uint8_t _effect, const uint8_t _parameter)
	{
		return static_cast<uint8_t>(MdKitMasterEffectBlock[_effect] * MasterEffectParameters + _parameter);
	}
	// The same for a master effect's parameter in MasterEffect order (effect * 8 + parameter, 0..31)
	constexpr uint8_t mdKitMasterIndex(const uint8_t _parameter)
	{
		return mdKitMasterIndex(static_cast<uint8_t>(_parameter / MasterEffectParameters),
			static_cast<uint8_t>(_parameter % MasterEffectParameters));
	}

	struct KitDump
	{
		uint8_t slot;
		std::vector<ParameterChange> parameters;
		// Machine id per track (see mdmachines.h), empty when the dump is too short to hold them.
		std::vector<uint16_t> machines;
		// As the machine shows it, trailing spaces removed: up to 16 characters on
		// the Machinedrum, 11 on the Monomachine.
		std::string name;
		// Machinedrum only, indexed by MasterEffect.
		std::optional<MasterEffects> masterEffects;
		// Machinedrum only: each track's LFO.
		std::optional<std::array<LfoSettings, machinedrum::TrackCount>> lfos;
	};

	// The Machinedrum's master effects from the 32 bytes of a Kit that hold them, in the dump's order
	// (reverb, echo, EQ, dynamix): the dump at $487, the live Kit (LiveKit::masterEffects).
	MasterEffects masterEffectsFromKit(const uint8_t* _bytes);

	// A Machinedrum pattern's trigs and locks, as far as the editor shows them. The
	// dump holds 32 steps, or 64 in its long form (a length over 32).
	// What a step does besides its trig: accent (Machinedrum), slide and swing
	enum class StepFlag : uint8_t
	{
		Accent,
		Slide,
		Swing
	};
	constexpr uint8_t StepFlagCount = 3;

	struct PatternDump
	{
		uint8_t slot = 0;      // 0..127, A01..H16
		uint8_t length = 16;   // steps, 1..64
		uint8_t kit = 0;       // 0..63, the Kit the pattern loads
		uint8_t steps = 32;    // steps the dump holds: 32, or 64 in the long form
		std::array<uint64_t, 16> trigs{};      // bit n: a trig on step n + 1
		std::array<uint32_t, 16> lockMasks{};  // bit p: parameter p (0..23) has a lock row
		// The 64 lock rows: row k belongs to the k-th set bit of the masks, in
		// track then parameter order; 128 and above means no lock on that step
		// (always past the steps the dump holds).
		std::vector<std::array<uint8_t, 64>> lockRows;
		// Accent, slide and swing (by StepFlag): a mask of steps every track follows,
		// or, when the pattern sets that kind per track (the machine's EDIT ALL off),
		// a mask per track. Bit n: step n + 1.
		std::array<uint64_t, StepFlagCount> flags{};
		std::array<bool, StepFlagCount> flagPerTrack{};
		std::array<std::array<uint64_t, 16>, StepFlagCount> trackFlags{};
		uint8_t accentAmount = 0;
		uint32_t swingAmount = 0;		// how far a swung step is delayed, in 16384ths of the step
	};

	// The receiving half of a Global's MIDI SYNC page.
	struct GlobalSync
	{
		bool clockIn = false;
		bool transportIn = false;

		bool operator==(const GlobalSync& _other) const
		{
			return clockIn == _other.clockIn && transportIn == _other.transportIn;
		}
		bool operator!=(const GlobalSync& _other) const { return !(*this == _other); }
	};

	Message statusRequest(MachineModel _model, StatusParameter _parameter);
	Message globalRequest(MachineModel _model, uint8_t _slot);
	Message kitRequest(MachineModel _model, uint8_t _slot);
	// These requests only inspect firmware state. They may be sent while the UW
	// factory image is being learned without making that image user-modified.
	bool isReadOnlyRequest(MachineModel _model, MessageView _message);
	Message kitSave(MachineModel _model, uint8_t _slot);
	// LOAD KIT ($58, MCL's MD::loadKit): the stored Kit becomes the live one, as stored; what the live Kit held
	// and was not saved is gone. SET STATUS with the Kit does the same on both machines (mdEditorFirmwareTest).
	Message kitLoad(MachineModel _model, uint8_t _slot);
	// SET STATUS with the pattern ($71 $04): the machine's pattern at once when stopped, at the end of the one
	// playing otherwise (patternChainFirmwareTest), its Kit loaded with it (mdEditorFirmwareTest)
	Message patternSelect(MachineModel _model, uint8_t _slot);
	// ASSIGN MACHINE ($5B) for a track of the live Kit. Machinedrum ids 128 and up
	// go out as id - 128 with the UW flag; the Monomachine form asks for no page
	// initialisation. Empty for a track or machine the model does not have.
	std::optional<Message> assignMachine(MachineModel _model, uint8_t _track, uint16_t _machine);
	// Machinedrum: one parameter (0..7) of a master effect of the live Kit, $5D to $60.
	// Empty for a parameter or value out of range.
	std::optional<Message> masterEffectChange(MasterEffect _effect, uint8_t _parameter, uint8_t _value);
	// Machinedrum: one field of a track's LFO (SET LFO PARAM, $62, after MCL's MD::setLFOParam), 0 the
	// destination track, 1 its parameter, 2 and 3 the shapes, 4 the update (LfoSettings). OS 1.63 ignores a
	// value out of range, keeping the field's ($205852 on; mdPlayheadProbe --lfo sends rising values, the last
	// in range stays); empty for one here, or a track or field out of range. Fields 5-7, which the OS also
	// takes (LFOS, LFOD, LFOM), are the CCs' here.
	std::optional<Message> lfoChange(uint8_t _track, uint8_t _field, uint8_t _value);
	// Machinedrum: the output of a track (SET TRACK ROUTING, $5C), kept in the Global.
	// Empty for a track or output out of range.
	std::optional<Message> trackRouting(uint8_t _track, TrackOutput _output);

	std::optional<StatusResponse> parseStatusResponse(MachineModel _model,
		MessageView _message);
	std::optional<StatusResponse> parseSetStatus(MachineModel _model,
		MessageView _message);
	std::optional<GlobalDump> parseGlobalDump(MachineModel _model,
		MessageView _message);
	std::optional<KitDump> parseKitDump(
		MachineModel _model, MessageView _message);
	// Machinedrum pattern dump ($67), 32- or 64-step form; only the first 32 steps are kept.
	std::optional<PatternDump> parseMdPatternDump(MessageView _message);

	// SET STATUS for the Global slot: the firmware reloads that slot, which is how
	// a Global dump written to the active slot takes effect.
	Message globalReload(MachineModel _model, uint8_t _slot);

	// CLOCK IN and TRANSPORT IN of a Global dump.
	std::optional<GlobalSync> parseGlobalSync(MachineModel _model,
		MessageView _message);
	// The same Global dump with CLOCK IN and TRANSPORT IN replaced, everything
	// else kept, and its checksum and length recomputed.
	std::optional<Message> withGlobalSync(MachineModel _model,
		MessageView _message, GlobalSync _sync);
}
