#pragma once

#include <cstdint>

// The voice CPU's SRAM (OS 1.41) where two or more of the port's modules read it. An address one module alone reads
// stays in that module. The addresses are the firmware's: the routines rely on the layout.
namespace syntakt
{
	constexpr uint32_t VoiceOut = 0x80003840;		// 8 x 32 samples, voice v at + 128 v
	constexpr uint32_t TrackOutStride = 0x80;		// one track's 32 samples in the output block
	constexpr uint32_t TrackParams = 0x80003c40;	// the per-track parameter array
	constexpr uint32_t ParamStride = 0x8e;			// its stride
	constexpr uint32_t Sine = 0x80004b70;			// the RAM sine the boot builds

	constexpr uint32_t Block = 0x80008000;			// the parameter block CPU #1 sends every 32 samples
	constexpr uint32_t BlockBytes = 0x1b0;
	constexpr uint32_t TrigSeen = 0x80008550;		// a byte per track: a trig came last block (the filter writes it)
	constexpr uint32_t Histories = 0x80008a60;		// the decimators' histories, 12 longs per voice
	constexpr uint32_t HistoryStride = 0x30;
	constexpr uint32_t ClockStep = 0x8000a07c;		// the block's word at + 0x184, copied there as it arrives

	// Unit k's output buffers, 32 samples each, 0x124 bytes apart: A the scaled output, B the raw one. Engines also
	// use them as scratch for their own stages
	constexpr uint32_t UnitBufferStride = 0x124;
	constexpr uint32_t bufferA(const uint32_t _k) { return 0x8000945c + UnitBufferStride * _k; }
	constexpr uint32_t bufferB(const uint32_t _k) { return 0x80008c60 + UnitBufferStride * _k; }
}
