#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "stSection7.h"

// Inlined whatever the compiler's heuristics say: the mix's and the memory's hottest calls, which MSVC leaves as calls
#ifndef ST_FORCE_INLINE
#	ifdef _MSC_VER
#		define ST_FORCE_INLINE __forceinline
#	else
#		define ST_FORCE_INLINE inline __attribute__((always_inline))
#	endif
#endif

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
	// fault, which the first one's address records: reads give 0, writes are dropped.
	//
	// A table of 4 KB pages gives each access its backing bytes in two lookups: a page of SRAM, of Pages, of the data
	// page, of the lines written, or one section 3 fills whole. An access the table has no page for (a fault, a line
	// not written yet, section 3's partial first and last pages, a write to section 3) takes the search through the
	// regions as before, which decides it as it always did
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
			mapPages();
		}

		// A copy's table points at the copy's own bytes
		Memory(const Memory& _other)
			: m_rom(_other.m_rom), m_regions(_other.m_regions), m_lines(_other.m_lines), m_fault(_other.m_fault)
		{
			mapPages();
		}

		Memory& operator=(const Memory& _other)
		{
			if(this == &_other)
				return *this;
			m_rom = _other.m_rom;
			m_regions = _other.m_regions;
			m_lines = _other.m_lines;
			m_fault = _other.m_fault;
			mapPages();
			return *this;
		}

		// Moving keeps the vectors' and the map's storage, so the table stays right
		Memory(Memory&&) = default;
		Memory& operator=(Memory&&) = default;

		// Every page of Lines held from now on, zeros where nothing was written: no page is allocated as a line is
		// first written, which a plug-in's audio thread cannot afford. What any access reads or writes is the same
		void reserveLines()
		{
			for(const auto& r : Lines)
				for(uint32_t a = r.base; a - r.base < r.size; a += PageSize)
					line(a);
		}

		uint8_t byte(const uint32_t _address) const
		{
			if(const uint8_t* b = readAt(_address, 1))
				return *b;
			return searchByte(_address);
		}

		void setByte(const uint32_t _address, const uint8_t _v)
		{
			if(uint8_t* b = writeAt(_address, 1))
				*b = _v;
			else
				searchSetByte(_address, _v);
		}

		int16_t half(const uint32_t _address) const
		{
			if(const uint8_t* b = readAt(_address, 2))
			{
				uint16_t v;
				std::memcpy(&v, b, 2);
				return static_cast<int16_t>(swap16(v));
			}
			return static_cast<int16_t>(byte(_address) << 8 | byte(_address + 1));
		}

		void setHalf(const uint32_t _address, const int32_t _v)
		{
			if(uint8_t* b = writeAt(_address, 2))
			{
				const uint16_t v = swap16(static_cast<uint16_t>(_v));
				std::memcpy(b, &v, 2);
				return;
			}
			setByte(_address, static_cast<uint8_t>(_v >> 8));
			setByte(_address + 1, static_cast<uint8_t>(_v));
		}

		int32_t word(const uint32_t _address) const
		{
			if(const uint8_t* b = readAt(_address, 4))
			{
				uint32_t v;
				std::memcpy(&v, b, 4);
				return static_cast<int32_t>(swap32(v));
			}
			return static_cast<int32_t>(static_cast<uint32_t>(byte(_address)) << 24 | byte(_address + 1) << 16 |
				byte(_address + 2) << 8 | byte(_address + 3));
		}

		void setWord(const uint32_t _address, const int32_t _v)
		{
			const auto v = static_cast<uint32_t>(_v);
			if(uint8_t* b = writeAt(_address, 4))
			{
				const uint32_t w = swap32(v);
				std::memcpy(b, &w, 4);
				return;
			}
			setByte(_address, static_cast<uint8_t>(v >> 24));
			setByte(_address + 1, static_cast<uint8_t>(v >> 16));
			setByte(_address + 2, static_cast<uint8_t>(v >> 8));
			setByte(_address + 3, static_cast<uint8_t>(v));
		}

		// The _n bytes (at most a page) from _address, for a routine that moves a run of them at once, when one page
		// of the table holds them all; null where byte() and setByte() must decide, a byte at a time
		const uint8_t* bytesAt(const uint32_t _address, const uint32_t _n) const { return readAt(_address, _n); }
		uint8_t* writableBytesAt(const uint32_t _address, const uint32_t _n) { return writeAt(_address, _n); }

		// The long at bytes from bytesAt or writableBytesAt, in the CPU's order, and a long written there
		static int32_t wordAt(const uint8_t* _b)
		{
			uint32_t v;
			std::memcpy(&v, _b, 4);
			return static_cast<int32_t>(swap32(v));
		}

		static void setWordAt(uint8_t* _b, const int32_t _v)
		{
			const uint32_t w = swap32(static_cast<uint32_t>(_v));
			std::memcpy(_b, &w, 4);
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
		// A page's bytes for reading and for writing, null where the search decides
		struct Page
		{
			const uint8_t* read = nullptr;
			uint8_t* write = nullptr;
		};

		static constexpr uint32_t ChunkShift = 22;		// the table's first level: 1024 chunks of 4 MB
		static constexpr uint32_t ChunkPages = 1024;
		using Chunk = std::array<Page, ChunkPages>;

		// The _n bytes from _address when one page of the table holds them all, SRAM first, the most accessed; null
		// where the search decides
		ST_FORCE_INLINE const uint8_t* readAt(const uint32_t _address, const uint32_t _n) const
		{
			if(_address - SramBase <= SramSize - _n)
				return m_sram + (_address - SramBase);
			if((_address & (PageSize - 1)) > PageSize - _n)
				return nullptr;
			const uint16_t c = m_chunks[_address >> ChunkShift];
			if(!c)
				return nullptr;
			const uint8_t* p = m_table[c - 1u][(_address >> 12) & (ChunkPages - 1)].read;
			return p ? p + (_address & (PageSize - 1)) : nullptr;
		}

		ST_FORCE_INLINE uint8_t* writeAt(const uint32_t _address, const uint32_t _n) const
		{
			if(_address - SramBase <= SramSize - _n)
				return m_sram + (_address - SramBase);
			if((_address & (PageSize - 1)) > PageSize - _n)
				return nullptr;
			const uint16_t c = m_chunks[_address >> ChunkShift];
			if(!c)
				return nullptr;
			uint8_t* p = m_table[c - 1u][(_address >> 12) & (ChunkPages - 1)].write;
			return p ? p + (_address & (PageSize - 1)) : nullptr;
		}

		// The big-endian CPU's order from the host's, which is little-endian (x86-64, ARM64)
		static uint16_t swap16(const uint16_t _v) { return static_cast<uint16_t>(_v << 8 | _v >> 8); }

		static uint32_t swap32(const uint32_t _v)
		{
#ifdef _MSC_VER
			return _byteswap_ulong(_v);
#else
			return __builtin_bswap32(_v);
#endif
		}

		void mapPage(const uint32_t _base, const uint8_t* _read, uint8_t* _write)
		{
			uint16_t& c = m_chunks[_base >> ChunkShift];
			if(!c)
			{
				m_table.emplace_back();
				c = static_cast<uint16_t>(m_table.size());
			}
			m_table[c - 1u][(_base >> 12) & (ChunkPages - 1)] = {_read, _write};
		}

		// The table from what the memory holds, in the search's order of precedence: section 3's whole pages (read
		// only), then the lines written, then the regions, which section 3's data page lies under
		void mapPages()
		{
			m_chunks.fill(0);
			m_table.clear();
			m_sram = m_regions.front().bytes.data();
			const auto& rom = m_rom->bytes;
			const uint32_t romEnd = Section3::Base + static_cast<uint32_t>(rom.size());
			for(uint32_t a = (Section3::Base + PageSize - 1) & ~(PageSize - 1); a + PageSize <= romEnd; a += PageSize)
				mapPage(a, rom.data() + (a - Section3::Base), nullptr);
			for(auto& [base, page] : m_lines)
				mapPage(base, page.data(), page.data());
			for(auto& r : m_regions)
				for(uint32_t i = 0; i < r.bytes.size(); i += PageSize)
					mapPage(r.base + i, r.bytes.data() + i, r.bytes.data() + i);
		}

		uint8_t searchByte(const uint32_t _address) const
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

		void searchSetByte(const uint32_t _address, const uint8_t _v)
		{
			if(uint8_t* p = find(_address))
				*p = _v;
			else if(inLines(_address))
				line(_address)[_address & (PageSize - 1)] = _v;
			else
				noteFault(_address);
		}

		// A line's page, made of zeros the first time, and mapped
		std::vector<uint8_t>& line(const uint32_t _address)
		{
			const uint32_t base = _address & ~(PageSize - 1);
			auto& page = m_lines[base];
			if(page.empty())
			{
				page.resize(PageSize);
				mapPage(base, page.data(), page.data());
			}
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
		std::array<uint16_t, 1u << (32 - ChunkShift)> m_chunks{};	// a chunk's table + 1, 0: none
		std::vector<Chunk> m_table;
		uint8_t* m_sram = nullptr;		// the first region's bytes
	};
}
