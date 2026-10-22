#pragma once

#include <cstdint>
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

	// The memory the ported routines use: CPU #1's 64 KB of SRAM (0x80000000), the DDR page of the lock bits
	// (0x4550e000) and section 3, read only. Bytes are kept in the big-endian CPU's order. An access anywhere else is
	// a fault, which the first one's address records: reads give 0, writes are dropped
	class Memory
	{
	public:
		static constexpr uint32_t SramBase = 0x80000000;
		static constexpr uint32_t SramSize = 0x10000;
		static constexpr uint32_t DdrBase = 0x4550e000;
		static constexpr uint32_t DdrSize = 0x1000;

		explicit Memory(const Section3& _rom) : m_rom(&_rom), m_sram(SramSize), m_ddr(DdrSize) {}

		uint8_t byte(const uint32_t _address) const
		{
			if(const uint8_t* p = find(_address))
				return *p;
			if(m_rom->holds(_address))
				return m_rom->bytes[_address - Section3::Base];
			noteFault(_address);
			return 0;
		}

		void setByte(const uint32_t _address, const uint8_t _v)
		{
			if(uint8_t* p = find(_address))
				*p = _v;
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

		const std::vector<uint8_t>& sram() const { return m_sram; }
		const std::vector<uint8_t>& ddr() const { return m_ddr; }

	private:
		uint8_t* find(const uint32_t _address)
		{
			if(_address - SramBase < SramSize)
				return &m_sram[_address - SramBase];
			if(_address - DdrBase < DdrSize)
				return &m_ddr[_address - DdrBase];
			return nullptr;
		}

		const uint8_t* find(const uint32_t _address) const { return const_cast<Memory*>(this)->find(_address); }

		void noteFault(const uint32_t _address) const
		{
			if(!m_fault)
				m_fault = _address;
		}

		const Section3* m_rom;
		std::vector<uint8_t> m_sram;
		std::vector<uint8_t> m_ddr;
		mutable uint32_t m_fault = 0;
	};
}
