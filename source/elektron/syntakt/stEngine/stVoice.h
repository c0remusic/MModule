#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "stMemoryMap.h"

namespace syntakt
{
	constexpr int Voices = 8;					// eight voices, eight tracks
	constexpr int BlockSize = 32;				// samples per block, at 48 kHz

	// Byte _address & 3 of a big-endian word, as the CPU sees it. Written with an unsigned shift by (~a & 3) * 8: the
	// first form, with a shift by 8 * (3 - (a & 3)), read the wrong word in MSVC Release builds only, cause not found
	inline uint8_t beByte(const int32_t _word, const uint32_t _address)
	{
		return static_cast<uint8_t>(static_cast<uint32_t>(_word) >> ((~_address & 3u) << 3));
	}

	// The voice CPU's 64 KB of SRAM (0x80000000-0x8000ffff, OS 1.41), as words: the eight voice states, the scratch
	// buffers the voices share as they render one after the other, the decimators' histories and the RAM tables. The
	// port keeps the original's memory map because the routines rely on it: units overlap (0x80 apart, 0xcc long), a
	// voice keeps pointers to buffers, and the voices share the buffers. Addresses are the firmware's.
	struct Sram
	{
		static constexpr uint32_t Base = 0x80000000;
		static constexpr uint32_t Size = 0x10000;

		std::array<int32_t, Size / 4> words{};

		int32_t& at(const uint32_t _address) { return words[(_address - Base) >> 2]; }
		int32_t at(const uint32_t _address) const { return words[(_address - Base) >> 2]; }
		int32_t* ptr(const uint32_t _address) { return &words[(_address - Base) >> 2]; }
		const int32_t* ptr(const uint32_t _address) const { return &words[(_address - Base) >> 2]; }

		// Bytes and halfwords as the big-endian CPU sees them in its words
		uint8_t byte(const uint32_t _address) const { return beByte(words[(_address - Base) >> 2], _address); }

		void setByte(const uint32_t _address, const uint8_t _v)
		{
			const uint32_t shift = (~_address & 3u) << 3;
			const size_t i = (_address - Base) >> 2;
			const uint32_t w = static_cast<uint32_t>(words[i]);
			words[i] = static_cast<int32_t>((w & ~(0xffu << shift)) | static_cast<uint32_t>(_v) << shift);
		}

		int16_t half(const uint32_t _address) const
		{
			return static_cast<int16_t>(byte(_address) << 8 | byte(_address + 1));
		}
	};

	// The samples CPU #1 loads into the voice CPU's DDR, which SP TWINSHOT plays: the directory the firmware keeps at
	// 0x4404f754, 64 entries of (length in samples, pointer to the first), and the signed 16-bit big-endian data the
	// pointers reach, at base. A player reads 0 outside the data and section 7, as the DDR CPU #1 left empty holds
	struct Samples
	{
		struct Entry
		{
			uint32_t length = 0;
			uint32_t pointer = 0;
		};

		std::array<Entry, 64> directory{};
		uint32_t base = 0;
		std::vector<uint8_t> data;
	};

	// Section 7 as words, where the engines read their constant tables: the user's own firmware, never shipped
	struct Rom
	{
		static constexpr uint32_t Base = 0x40000400;

		const int32_t* words = nullptr;
		const Samples* samples = nullptr;	// none: the directory the voice CPU's boot leaves, every length 0

		int32_t at(const uint32_t _address) const { return words[(_address - Base) >> 2]; }
		const int32_t* ptr(const uint32_t _address) const { return &words[(_address - Base) >> 2]; }

		// Entry _i of the table at _table; _i may be negative, as the firmware's signed indices are
		int32_t entry(const uint32_t _table, const int32_t _i) const { return ptr(_table)[_i]; }

		uint8_t byte(const uint32_t _address) const { return beByte(words[(_address - Base) >> 2], _address); }
	};

	// The voice CPU's own globals outside SRAM that the engines read and write
	struct Globals
	{
		std::array<int32_t, 2> rng{};	// 0x4404f954: the generator voices draw their noise seeds from
	};

	// A voice's state (0x708 bytes at 0x80000000 + 0x708 v): field offsets are the firmware's byte offsets, as
	// .scratch/elektron-engines names them
	struct Voice
	{
		static constexpr uint32_t Size = 0x708;
		static constexpr uint32_t UnitStride = 0x80;	// unit k (an operator, its envelope) is at 0x80 k

		Sram& sram;
		uint32_t address;

		static uint32_t addressOf(const int _v) { return Sram::Base + Size * static_cast<uint32_t>(_v); }

		int32_t& at(const uint32_t _offset) { return sram.at(address + _offset); }
		int32_t at(const uint32_t _offset) const { return sram.at(address + _offset); }
		int32_t& unit(const uint32_t _k, const uint32_t _offset) { return at(UnitStride * _k + _offset); }

		// Voice-level fields
		enum : uint32_t
		{
			Engine = 0x00,
			EngineCopy = 0x04,
			TrigPending = 0x34,		// a trig arrived this block: the voice fades out
			TrigNow = 0x38,			// the previous block's trig: the voice starts
			GateOff = 0x3c,
			DriveOn = 0x48,			// the drive stage's gain envelope runs

			AmpLevel = 0x3d0,		// negative representation: INT32_MIN full, 0 silent
			AmpLevelPrev = 0x3d4,	// at the end of the previous block, where the VCA's ramp starts
			AmpHalfStart = 0x3d8,
			AmpStage = 0x3dc,		// 0 attack, 1 decay, 2 hold
			AmpHoldCount = 0x3e0,
			AmpRelease = 0x3e4,
			AmpAttack = 0x3e8,		// INT32_MIN: instant
			AmpHold = 0x3ec,		// hold until gate-off when not 0
			AmpDecay = 0x3f0,		// decay coefficient, negative

			DriveSetA = 0x414,		// c0, c1, c2 of the gain envelope while it falls
			DriveSetB = 0x420,		// and while it recovers
			DriveSet = 0x42c,		// the firmware's pointer to the active set
			DriveX1 = 0x430,
			DriveY1 = 0x434,		// the gain at the end of the block
			DriveStage = 0x438,		// 1 falling, 5 recovering
			DriveFloor = 0x43c,
			DrivePreGain = 0x440,	// applied 4 times: a hard clip
			DriveGain = 0x444,

			DecimatorHistory = 0x56c	// pointer to the voice's 12 longs, Histories + 0x30 v
		};

		// Unit fields, at 0x80 k
		enum : uint32_t
		{
			EnvAttack = 0x54,		// increment, or start level; INT32_MIN: instant
			EnvDecay = 0x58,		// coefficient, negative
			EnvTarget = 0x5c,
			EnvDelay = 0x60,		// in blocks
			Phase = 0x6c,			// 24 bits
			EnvLevel = 0x7c,		// negative representation
			EnvLevelPrev = 0x80,
			EnvStage = 0x84,		// 0 attack, 1 and 2 decay, 3 delay, 4 hold
			EnvDelayCount = 0x88
		};
	};

	// An engine's update reads the track's machine parameters at P + 0x1c + 0x22 + _k (_params: P + 0x1c): parameter
	// _k as a signed word (value << 8 | fine) or its high byte, signed
	inline int32_t machineWord(const Sram& _sram, const uint32_t _params, const uint32_t _k)
	{
		return _sram.half(_params + 0x22 + _k);
	}

	inline int32_t machineByte(const Sram& _sram, const uint32_t _params, const uint32_t _k)
	{
		return static_cast<int8_t>(_sram.byte(_params + 0x22 + _k));
	}

	// A table a voice points at, in SRAM (the RAM sine) or in section 7 (the ROM waves)
	inline const int32_t* table(const Sram& _sram, const Rom& _rom, const uint32_t _address)
	{
		return _address >= Sram::Base ? _sram.ptr(_address) : _rom.ptr(_address);
	}

	// 0x40003eb4: draws _count values from a generator (_state[0..1]: Globals::rng, or a voice's own) into _dst
	void drawRandom(int32_t* _dst, int32_t _count, int32_t* _state);

	// 0x40003ee0: a voice's state when its engine changes (on a trig)
	void initVoice(Voice& _v, Globals& _g, const Rom& _rom);

	// 0x4000255e: on a trig, the phases of units 0-6 and six fields of the noise and filter stages restart
	void restartPhases(Voice& _v);
}
