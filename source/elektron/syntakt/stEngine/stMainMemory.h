#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "stSection7.h"

// The Syntakt's main CPU (CPU #1, OS 1.41's section 3) where the port's routines read and write it. The addresses are
// the firmware's: the routines rely on the layout (.scratch/elektron-engines/cpu1.md).
namespace syntakt::main
{
	constexpr uint32_t Engine = 0x800021b0;			// the engine copy: 13 tracks of 142 bytes, then the FX blocks
	constexpr uint32_t EngineWords = 938;			// 0x754 bytes
	constexpr uint32_t TrackBytes = 0x8e;			// a track's parameters, internal id i at word 14 + i
	constexpr uint32_t Settings = 0x80002904;		// 13 tracks' settings, 124 bytes each, right after the engine copy
	constexpr uint32_t SettingsBytes = 0x64c;
	constexpr uint32_t Machines = 0x80002f50;		// a machine id byte per track
	constexpr uint32_t Smoothed = 0x80006348;		// the smoothed copy, same layout as the engine copy
	constexpr uint32_t SettingsCopy = 0x80006a9c;	// the settings, copied unsmoothed after it
	constexpr uint32_t SmoothState = 0x8000e9b0;	// a long per engine word: the smoothing's state, Q31
	constexpr uint32_t LockBits = 0x4550e1cc;		// 3 longs per track, a bit per internal id: the parameter is locked
	constexpr uint32_t LockStride = 12;

	// The engine word of track _t's internal id 0: the routines' (0x8e t + 0x1c) >> 1
	constexpr uint32_t trackWord(const uint32_t _t) { return (TrackBytes * _t + 0x1c) >> 1; }

	constexpr size_t Section3Size = 3438480;			// OS 1.41's
	constexpr uint64_t Section3Fnv = 0xc21a11109890e0d1;	// FNV-1a 64 of OS 1.41's section 3

	// Section 3 as it loads at 0x40000400: the user's own firmware, never shipped. Empty when the file is not OS 1.41's
	struct Section3
	{
		static constexpr uint32_t Base = 0x40000400;

		std::vector<uint8_t> bytes;

		bool holds(const uint32_t _address) const { return _address - Base < bytes.size(); }
	};

	inline Section3 loadSection3(const std::string& _path)
	{
		Section3 s{readFile(_path)};
		uint64_t h = 0xcbf29ce484222325;
		for(const uint8_t b : s.bytes)
			h = (h ^ b) * 0x100000001b3;
		if(s.bytes.size() != Section3Size || h != Section3Fnv)
			s.bytes.clear();
		return s;
	}

	// The memory the ported routines use, kept in the big-endian CPU's order: CPU #1's 64 KB of SRAM (0x80000000), the
	// DDR pages of the state the routines keep (Pages), the DDR the delay and the reverb keep their lines in (Lines),
	// held a 4 KB page at a time as they are written, and section 3, read only but for its data page 0x402d7000
	// (the tempo, the LFOs' random generator), which starts as the file holds it. An access anywhere else is a
	// fault, which the first one's address records: reads give 0, writes are dropped
	class Memory
	{
	public:
		struct Region
		{
			uint32_t base;
			std::vector<uint8_t> bytes;
		};

		struct Range
		{
			uint32_t base;
			uint32_t size;
		};

		static constexpr uint32_t SramBase = 0x80000000;
		static constexpr uint32_t SramSize = 0x10000;
		static constexpr uint32_t DataBase = 0x402d7000;	// section 3's data the routines write
		static constexpr uint32_t PageSize = 0x1000;
		static constexpr uint32_t Pages[] = {
			0x4550e000,		// lock bits, the reverb's sine
			0x444da000,		// LFO state
			0x444db000,		// last pitches
			0x45565000,		// glides
			0x43bdc000,		// filter envelopes, the kit reload, the voice outputs' eDMA
			0x43bdd000,		// the eDMA pending
			0x43bde000,		// the cuts
			0x454e3000,		// the analog voices' state
			0x43bd7000,		// the DAC's output gains, the tracks' Overbridge gains
			0x43bdb000,		// the DAC's other input and its gain [INFER]
			0x4553f000,		// the half of the DAC ring the block writes
			0x41b9c000,		// the kit's per-track bytes, where the pointer 0x800030b8 points (KitBytes)
		};
		static constexpr Range Lines[] = {
			{0x4d570000, 0xa0000},		// the reverb's 9 lines of 64 KB, then its 8 of 4 KB at 0x4d600000
			{0x4fc00000, 0x400000},		// the delay's ring, 2^19 stereo frames
		};

		explicit Memory(const Section3& _rom) : m_rom(&_rom)
		{
			m_regions.push_back({SramBase, std::vector<uint8_t>(SramSize)});
			for(const uint32_t page : Pages)
				m_regions.push_back({page, std::vector<uint8_t>(PageSize)});
			Region data{DataBase, std::vector<uint8_t>(PageSize)};
			for(uint32_t i = 0; i < PageSize; ++i)
				data.bytes[i] = _rom.holds(DataBase + i) ? _rom.bytes[DataBase + i - Section3::Base] : 0;
			m_regions.push_back(std::move(data));
		}

		uint8_t byte(const uint32_t _address) const
		{
			if(const uint8_t* p = find(_address))
				return *p;
			if(inLines(_address))
			{
				const auto it = m_lines.find(_address & ~(PageSize - 1));
				return it == m_lines.end() ? 0 : it->second[_address & (PageSize - 1)];
			}
			if(m_rom->holds(_address))
				return m_rom->bytes[_address - Section3::Base];
			noteFault(_address);
			return 0;
		}

		void setByte(const uint32_t _address, const uint8_t _v)
		{
			if(uint8_t* p = find(_address))
				*p = _v;
			else if(inLines(_address))
				line(_address)[_address & (PageSize - 1)] = _v;
			else
				noteFault(_address);
		}

		int16_t half(const uint32_t _address) const
		{
			return static_cast<int16_t>(byte(_address) << 8 | byte(_address + 1));
		}

		void setHalf(const uint32_t _address, const int32_t _v)
		{
			setByte(_address, static_cast<uint8_t>(_v >> 8));
			setByte(_address + 1, static_cast<uint8_t>(_v));
		}

		int32_t word(const uint32_t _address) const
		{
			return static_cast<int32_t>(static_cast<uint32_t>(byte(_address)) << 24 | byte(_address + 1) << 16 |
				byte(_address + 2) << 8 | byte(_address + 3));
		}

		void setWord(const uint32_t _address, const int32_t _v)
		{
			const auto v = static_cast<uint32_t>(_v);
			setByte(_address, static_cast<uint8_t>(v >> 24));
			setByte(_address + 1, static_cast<uint8_t>(v >> 16));
			setByte(_address + 2, static_cast<uint8_t>(v >> 8));
			setByte(_address + 3, static_cast<uint8_t>(v));
		}

		// The first address accessed outside the memory, 0 while there is none
		uint32_t fault() const { return m_fault; }

		bool romHolds(const uint32_t _address) const { return m_rom->holds(_address); }

		const std::vector<Region>& regions() const { return m_regions; }

		// The pages of Lines written so far, by base: a page not here holds zeros
		const std::map<uint32_t, std::vector<uint8_t>>& lines() const { return m_lines; }

		static bool inLines(const uint32_t _address)
		{
			for(const auto& r : Lines)
				if(_address - r.base < r.size)
					return true;
			return false;
		}

	private:
		std::vector<uint8_t>& line(const uint32_t _address)
		{
			auto& page = m_lines[_address & ~(PageSize - 1)];
			if(page.empty())
				page.resize(PageSize);
			return page;
		}

		uint8_t* find(const uint32_t _address)
		{
			for(auto& r : m_regions)
				if(_address - r.base < r.bytes.size())
					return &r.bytes[_address - r.base];
			return nullptr;
		}

		const uint8_t* find(const uint32_t _address) const { return const_cast<Memory*>(this)->find(_address); }

		void noteFault(const uint32_t _address) const
		{
			if(!m_fault)
				m_fault = _address;
		}

		const Section3* m_rom;
		std::vector<Region> m_regions;
		std::map<uint32_t, std::vector<uint8_t>> m_lines;
		mutable uint32_t m_fault = 0;
	};
}
