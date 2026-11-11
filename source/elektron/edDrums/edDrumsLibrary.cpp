#include "edDrumsLibrary.h"

#include <algorithm>
#include <stdexcept>

#include "Firmware.h"
#include "edDrumsHouseWaves.h"
#include "mdVoicePiImport.h"

namespace elektron::drums
{
	Library::Library()
	{
		const auto mark = [&](const int _from, const int _to, const MachineData _data)
		{
			std::fill(mdData.begin() + _from, mdData.begin() + _to + 1, _data);
		};
		mark(48, 63, MachineData::None);		// E12
		markRoms();
		makeHouseWaves(trx);
		pi = makeHousePi();
		os = std::make_unique<md::voice::OsTables>(pi.data());
		// TRX-BD, SD, XT, RS, XC, P-I-BD, SD, MT, ML, RS
		for(const int id : {16, 17, 18, 20, 27, 64, 65, 66, 67, 69})
			mark(id, id, MachineData::House);
	}

	namespace
	{
		// TRX's sampled waves in ImageWords::trx's order
		template<typename F> void forTrxWaves(md::voice::TrxTables& _t, F _f)
		{
			_f(_t.bdWaveA);
			_f(_t.bdWaveB);
			_f(_t.sdWave);
			_f(_t.xtWave);
			_f(_t.xcWave);
			_f(_t.rsWave);
			_f(_t.chWaves);
		}
	}

	Library::Library(const std::vector<uint8_t>& _flashImage) : Library(importImage(_flashImage))
	{
	}

	ImageWords Library::importImage(const std::vector<uint8_t>& _flashImage)
	{
		ImageWords w;
		const auto flash = md::fw::loadFirmwareFromFlash(_flashImage);
		{
			md::voice::TrxTables t;
			md::voice::importTrx(flash.firmware, t);
			forTrxWaves(t, [&](const auto& _wave) { w.trx.insert(w.trx.end(), _wave.begin(), _wave.end()); });
		}
		w.pi = md::voice::importPi(flash);
		w.e12 = md::fw::dspWords(flash.firmware.dspA, md::voice::E12Bank::Addr, md::voice::E12Bank::Words);

		auto uw = md::fw::loadRomBankFromFlash(_flashImage);
		w.directory.assign(md::voice::RomBank::DirectoryWords, 0);
		for(const auto& e : uw.directory)
			std::copy(std::begin(e.words), std::end(e.words),
				w.directory.begin() + (e.addr - md::fw::RomBank::DirectoryAddr));
		w.samples = std::move(uw.data);
		return w;
	}

	Library::Library(const ImageWords& _words) : Library()
	{
		size_t trxWords = 0;
		forTrxWaves(trx, [&](const auto& _wave) { trxWords += _wave.size(); });
		if(_words.trx.size() != trxWords || _words.pi.size() != md::voice::PiBank::Words
			|| _words.e12.size() != md::voice::E12Bank::Words || _words.directory.size() != md::voice::RomBank::DirectoryWords)
			throw std::runtime_error("the image's words are not a Machinedrum OS 1.63's");
		auto from = _words.trx.begin();
		forTrxWaves(trx, [&](auto& _wave)
		{
			std::copy(from, from + static_cast<std::ptrdiff_t>(_wave.size()), _wave.begin());
			from += static_cast<std::ptrdiff_t>(_wave.size());
		});
		pi = _words.pi;
		os = std::make_unique<md::voice::OsTables>(pi.data());
		e12 = _words.e12;
		directory = _words.directory;
		samples = _words.samples;
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
