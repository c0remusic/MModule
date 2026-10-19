#pragma once

#include "mdDrumsSamples.h"

#include "mdProtocol/mdkit.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace md::mds
{
	struct Package;
}

namespace mdDrums
{
	// What md-drums-seq shows of the Kit played (its ADR 0003; ours amended): a device names itself with a hello, four
	// notes the Device takes (Device::FirstHelloNote, Telemetry::takeHello), and reads <id>.kit in <temp>/md-drums-seq:
	// the Kit's name, each Track's Machine and SYN1-8 names, its 24 values. One file per device heard, written whole
	// (baseLib::filesystem::writeFileAtomic) when its device is new or the Kit changed, looked at every StepMs; deleted
	// ForgetMs after its device's last hello, and with this object. On the message thread; the clock is the caller's.
	class KitFiles
	{
	public:
		static constexpr double StepMs = 250.0;
		static constexpr double ForgetMs = 15000.0;

		// _folder: where the files go
		explicit KitFiles(std::filesystem::path _folder = defaultFolder());
		~KitFiles();

		KitFiles(const KitFiles&) = delete;
		KitFiles& operator=(const KitFiles&) = delete;

		// <temp>/md-drums-seq
		static std::filesystem::path defaultFolder();
		// A device's file: its id in six lowercase hexadecimal digits, then ".kit"
		std::filesystem::path file(uint32_t _id) const;
		// The file's text for _kit, version 1. A User Machine's names come from its package as the instance holds it
		// (ticket 17 of the User Machines map): MDM1's category and code, and its parameters' names. A ROM Machine
		// playing a user's Sample is named after the Sample's file, without its extension, as one word (agreed with
		// md-drums-seq on 2026-10-19); one playing the factory's keeps its ROM-NN.
		using UserMachines = std::array<std::shared_ptr<const md::mds::Package>, 16>;
		using SampleNames = std::array<std::string, Samples::RomCount>;	// empty: the factory's
		static std::string text(const md::automation::sysex::MdKit& _kit, const UserMachines& _userMachines = {},
			const SampleNames& _samples = {});

		// A hello from the device _id: a new one has its file at the next step
		void hello(uint32_t _id, double _nowMs);
		// Whether to step at _nowMs: a device heard, and StepMs since the last step or a new device
		bool due(double _nowMs) const { return !m_devices.empty() && _nowMs >= m_nextStep; }
		// Every device's file brought to _text; those of devices silent for ForgetMs deleted
		void step(const std::string& _text, double _nowMs);
		size_t devices() const { return m_devices.size(); }

	private:
		struct Device
		{
			double lastHello = 0.0;
			std::string written;	// what its file holds
		};

		bool write(uint32_t _id, const std::string& _text) const;

		const std::filesystem::path m_folder;
		std::map<uint32_t, Device> m_devices;
		double m_nextStep = 0.0;
	};
}
