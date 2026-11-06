#include "edDrumsLibrary.h"

#include <algorithm>

#include "Firmware.h"
#include "mdVoicePiImport.h"

namespace elektron::drums
{
	Library::Library() : os(std::make_unique<md::voice::OsTables>(nullptr))
	{
		for(auto& d : mdDefaults)
			d.fill(64);
	}

	Library::Library(const std::vector<uint8_t>& _flashImage) : Library()
	{
		const auto flash = md::fw::loadFirmwareFromFlash(_flashImage);
		md::voice::importTrx(flash.firmware, trx);
		pi = md::voice::importPi(flash);
		os = std::make_unique<md::voice::OsTables>(pi.data());
		e12 = md::fw::dspWords(flash.firmware.dspA, md::voice::E12Bank::Addr, md::voice::E12Bank::Words);
		for(const auto& machine : md::fw::readMachineTable(flash.osImage))
			mdDefaults[machine.id] = machine.defaults;

		auto uw = md::fw::loadRomBankFromFlash(_flashImage);
		directory.assign(md::voice::RomBank::DirectoryWords, 0);
		for(const auto& e : uw.directory)
			std::copy(std::begin(e.words), std::end(e.words),
				directory.begin() + (e.addr - md::fw::RomBank::DirectoryAddr));
		samples = std::move(uw.data);
		bank = {directory.data(), samples.data(), samples.size()};
	}

	std::vector<uint8_t> Library::findFlashImage(const std::filesystem::path& _folder)
	{
		return md::fw::findFlashImage({_folder});
	}
}
