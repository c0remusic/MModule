#include "edDrumsLibrary.h"

#include <algorithm>

#include "Firmware.h"
#include "edDrumsHouseWaves.h"
#include "mdVoicePiImport.h"

namespace elektron::drums
{
	Library::Library() : os(std::make_unique<md::voice::OsTables>(nullptr))
	{
		const auto mark = [&](const int _from, const int _to, const MachineData _data)
		{
			std::fill(mdData.begin() + _from, mdData.begin() + _to + 1, _data);
		};
		mark(24, 24, MachineData::None);		// TRX-CY: its six waves are all it has
		mark(48, 63, MachineData::None);		// E12
		mark(64, 67, MachineData::None);		// P-I-BD, SD, MT, ML
		mark(69, 69, MachineData::None);		// P-I-RS
		markRoms();
		// TRX-CH, OH: their metal gone
		for(const int id : {22, 23})
			mark(id, id, MachineData::Partial);
		makeHouseWaves(trx);
		for(const int id : {16, 17, 18, 20, 27})		// TRX-BD, SD, XT, RS, XC
			mark(id, id, MachineData::House);
	}

	Library::Library(const std::vector<uint8_t>& _flashImage) : Library()
	{
		const auto flash = md::fw::loadFirmwareFromFlash(_flashImage);
		md::voice::importTrx(flash.firmware, trx);
		pi = md::voice::importPi(flash);
		os = std::make_unique<md::voice::OsTables>(pi.data());
		e12 = md::fw::dspWords(flash.firmware.dspA, md::voice::E12Bank::Addr, md::voice::E12Bank::Words);

		auto uw = md::fw::loadRomBankFromFlash(_flashImage);
		directory.assign(md::voice::RomBank::DirectoryWords, 0);
		for(const auto& e : uw.directory)
			std::copy(std::begin(e.words), std::end(e.words),
				directory.begin() + (e.addr - md::fw::RomBank::DirectoryAddr));
		samples = std::move(uw.data);
		bank = {directory.data(), samples.data(), samples.size()};
		mdData.fill(MachineData::Whole);
		markRoms();
	}

	void Library::markRoms()
	{
		for(const int entry : {0, 48})
			for(int e = entry; e < entry + (entry ? 16 : 32); ++e)
			{
				const bool sample = !directory.empty() && (directory[4 * static_cast<size_t>(e) + 1] & 0xffffff) != 0;
				mdData[static_cast<size_t>(128 + e)] = sample ? MachineData::Whole : MachineData::None;
			}
	}

	std::vector<uint8_t> Library::findFlashImage(const std::filesystem::path& _folder)
	{
		return md::fw::findFlashImage({_folder});
	}
}
