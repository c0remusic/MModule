#pragma once

#include <cstdint>

#include "stMainMemory.h"

// CPU #1's block for the voice CPU (cpu1.md, sections 1 and 7): the part of the audio interrupt 0x400a3856 from the end
// of its event loop (0x400a49a6) to the 0x1b0 bytes at Block complete (0x400a4db8), with the routines it calls. MACSR
// 0x20, as the interrupt sets it.
//
// The seam: what the event loop leaves for this part, which a plug-in gives in its place. In registers, five masks of
// tracks (BlockMasks); in memory, what the events wrote before: Notes and Velocity of the tracks trigged, the locks
// (lock() into the engine copy), the MIDI sources, the Mutes, Gates and Held words, Kills, the Reload state and the
// Tempo. The kit is the engine copy, its settings and the machine ids (Engine, Settings, Machines)
namespace syntakt::main
{
	constexpr uint32_t Block = 0x80002000;		// the block the voice CPU reads every 32 samples
	constexpr uint32_t BlockBytes = 0x1b0;
	constexpr uint32_t Tempo = 0x402d7010;		// a long: BPM x 120
	constexpr uint32_t Mutes = 0x80003150;		// a word: bit t, track t is muted; its low byte 0x80003151: 0-7's
	constexpr uint32_t Kills = 0x43bde440;		// a word: bit t, track t is cut this block (amp HOLD full, DEC and
												// REL 0; to the voice CPU, a trig blocked and a gate-off forced)
	constexpr uint32_t Reload = 0x43bdc4c8;		// a long: 0 nothing, 2 a kit reloads (every gate off), 1 after it
	constexpr uint32_t ReloadLevel = 0x43bdc808;	// a word: the FX track's +0x706 at the reload, then fading
	constexpr uint32_t VoiceOutputDma = 0x43bdc824;	// a long: non-zero, the voice CPU's last output comes by eDMA
	constexpr uint32_t VoiceOutputs = 0x80003d50;	// where it comes, 8 x 32 longs; cleared while there is none
	constexpr uint32_t DmaPending = 0x43bdd73c;		// a byte: 1, an eDMA started this block (0x400a2036 waits on it)
	constexpr uint32_t AnalogVoices = 0x454e3c58;	// 0x20 bytes per analog track 8-12: its state, then a long
	constexpr uint32_t CvSource = 0x444db72c;		// a long the CV frame's default word comes from [INFER]
	constexpr uint32_t CvDefault = 0x444db728;		// that word, kept
	constexpr uint32_t CvFrame = 0x800033f8;		// the analog tracks' CV words it fills (read by 0x40090e34)

	// The masks the event loop builds (bit t, track t), in the registers this part starts with
	struct BlockMasks
	{
		int32_t trigs = 0;			// d3: the tracks trigged (an event with flag 0x80)
		int32_t gateOffs = 0;		// d2: the voices' gate-offs: a note's length run out (0x80003050) and no trig, or
									// a track cut
		int32_t lfoTrigs = 0;		// d5: the trigs that restart the LFOs (flag 0x400, LFO trig)
		int32_t filterTrigs = 0;	// a5: the trigs that restart the filter envelope (flag 0x200, filter trig)
		int32_t filterGateOffs = 0;	// d4: the filter envelopes' gate-offs (their length 0x80003084 run out, a cut)
	};

	// 0x4011a2fc: CvDefault = (CvSource - 0x19000000) x 0x361134 (Q31), rounded, >> 9; then 26 longs of _frame
	// (from +0, +0x14, +0x28, +0x38, four each, and eleven more) are that word << 16
	void fillCvFrame(Memory& _mem, uint32_t _frame);

	// 0x400a49a6-0x400a4db8: the interrupt's block from the masks. The muted tracks' trigs go; a trig on a track whose
	// gate is still open and whose settings +0x60 is 3 restarts neither the voice, the filter envelope nor the LFOs
	// (the glide only) [INFER: legato]; then the smoothing, the cut tracks' amp, the modulation matrix, the LFOs, the
	// pitches, a kit reload, each digital track's machine id and hold flag (+0x3e, +0x4c), the analog voices' state,
	// the filter envelopes, and the block: tracks 0-7's machine bytes, filter, amp, pitch, velocity and cutoff, then
	// the trigs, gate-offs and cuts the voice CPU plays and the tempo
	void buildBlock(Memory& _mem, const BlockMasks& _masks);
}
