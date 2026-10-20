#pragma once

#include "mdProtocol/mdkit.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mdDrums
{
	// The Kits' Bank: 64 Slots in one file, shared by every set and every instance of the plug-in. Slot n is a Kit dump
	// ($52, MdKitDumpSize bytes) for slot n at n times that size, so that the file is a .syx that a Machinedrum and
	// Machinemodule read (.scratch/md-drums-editor/research/04-kits.md); an empty Slot is a dump whose name starts with
	// $7f, as OS 1.63 sends one. A missing file starts with the factory Kits from Slot 1 on. The file is read again
	// before each write, and a write touches its own Slots' bytes only, so that two instances keep each other's Slots
	// (ticket 10 of the editor map). Beside it, "Bank.machines" says which User Machine of the Library each Slot's Kit
	// plays at its numbers, written and read the same way, so that a Kit loaded plays the ones it was saved with (ticket
	// 27 of the User Machines map). Without JUCE, for any thread but one at a time.
	class Bank
	{
	public:
		using Kit = md::automation::sysex::MdKit;
		// A User Machine a Kit plays at a number: its package's id (Library::id) and its name, for saying which one the
		// Library lacks; id 0 for none
		struct Machine
		{
			uint64_t id = 0;
			std::string name;
			bool operator==(const Machine& _other) const { return id == _other.id && name == _other.name; }
			bool operator!=(const Machine& _other) const { return !(*this == _other); }
		};
		static constexpr int MachineCount = 16;	// USR-01 to USR-16
		using Machines = std::array<Machine, MachineCount>;
		// A Slot (0-63) and what to write into it, no Kit for an empty Slot, and the User Machines its Kit plays
		struct SlotKit
		{
			int slot = 0;
			std::optional<Kit> kit;
			Machines machines{};
		};
		static constexpr int SlotCount = 64;
		static constexpr size_t SlotSize = md::automation::sysex::MdKitDumpSize;
		static constexpr size_t MachineNameSize = 16;
		static constexpr size_t MachinesSize = MachineCount * (8 + MachineNameSize);	// a Slot's in Bank.machines

		Bank(std::filesystem::path _file, std::vector<Kit> _factory);

		// Reads the file again, writing it first with the factory Kits when it does not exist. False when it can be
		// neither read nor written (the Slots are then all empty).
		bool read();

		// Reads the file again when its write time moved since this Bank last read or wrote it, as when another
		// instance wrote a Slot: true when it did. For whoever compares a Slot with the Kit played (Save, Ctrl+S).
		bool follow();

		// The Kit in a Slot (0-63); empty for an empty Slot and out of range
		const std::optional<Kit>& slot(int _slot) const;
		bool isEmpty(int _slot) const { return !slot(_slot).has_value(); }
		// The User Machines a Slot's Kit plays, none for a Kit that plays none or was imported
		const Machines& machines(int _slot) const;

		// Writes Slots in the list's order, as an import does, after reading the file again once for them all. False
		// when a Slot is out of range or the file cannot be written.
		bool write(const std::vector<SlotKit>& _slots);
		// Writes a Slot, an empty one for no Kit
		bool write(const int _slot, const std::optional<Kit>& _kit, const Machines& _machines = {})
		{
			return write(std::vector<SlotKit>{{_slot, _kit, _machines}});
		}

		const std::filesystem::path& file() const { return m_file; }

		// A Slot's dump: the Kit's for slot _slot, or an empty Slot's
		static std::vector<uint8_t> dump(const std::optional<Kit>& _kit, int _slot);
		// The Kits in a .syx file's bytes, in their order: every Kit dump OS 1.63 takes, the empty Slots' left out
		static std::vector<Kit> parseSyx(const std::vector<uint8_t>& _bytes);

	private:
		std::filesystem::path machinesFile() const;
		void readMachines();
		bool writeMachines(const std::vector<SlotKit>& _slots);

		std::filesystem::path m_file;
		std::vector<Kit> m_factory;
		std::array<std::optional<Kit>, SlotCount> m_slots{};
		std::array<Machines, SlotCount> m_machines{};
		std::filesystem::file_time_type m_seen{};	// the file's write time when this Bank last read or wrote it
	};
}
