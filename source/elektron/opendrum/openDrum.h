// SPDX-License-Identifier: GPL-3.0-only
// Part of this port (MModule), linked with the GPL-3.0 voice sources of this folder.
#pragma once

// OPEN DRUM: one voice of the ported drum engine (README.md) on its own, its state in the caller's memory, so that
// every Track holds one. The firmware's blocks of OpenDrumBlock samples, int16 out, at the engine's REAL_FS. Nothing
// here allocates or locks: any thread, one voice at a time.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	OpenDrumDrum,		// the drum voice 1: FM oscillators, pitch envelope, transient, filter, drive
	OpenDrumSnare,
	OpenDrumCymbal,
	OpenDrumHiHat,
	OpenDrumKinds
};

#define OpenDrumBlock 64	// the firmware's OUTPUT_DMA_SIZE: its envelopes step once a block
#define OpenDrumMachineParameters 24	// a Track's Machine 1-24
#define OpenDrumOpen 0xffff	// the hihat's eighth Machine parameter: a Hit at 64 or more plays it open
#define OpenDrumNone 0xfffe	// a Machine parameter the voice does not use
#define OpenDrumSeed 0x2545f491u

typedef struct OpenDrumVoice OpenDrumVoice;

size_t openDrum_size(void);

// A voice of _kind as the firmware starts it, with the init kit's parameters, its smoothers settled
void openDrum_init(OpenDrumVoice* _voice, int _kind);

// A parameter by its front panel number (the firmware's Parameters.h), 0-127; one the voice does not read changes
// nothing
void openDrum_setParameter(OpenDrumVoice* _voice, uint16_t _parameter, uint8_t _value);

// What the mixer's smoothers of the fader, the level and the drive hold once settled: their targets
void openDrum_settle(OpenDrumVoice* _voice);

// _open: the hihat's open decay, else its closed one
void openDrum_trigger(OpenDrumVoice* _voice, uint8_t _velocity, uint8_t _note, int _open);

// The next OpenDrumBlock samples, the voice alone as the firmware's mixer computes it, before its pan
void openDrum_render(OpenDrumVoice* _voice, int16_t* _out);

// Whether the voice's amp envelope still opens it: once it does not, every block it renders is silence. A voice under
// it can be silent for a while, its samples rounded to 0
int openDrum_sounding(const OpenDrumVoice* _voice);

// Machine parameter _index 0-23 of _kind: its front panel number, OpenDrumOpen or OpenDrumNone; its name, NULL for
// none; the voice's parameters, Machine 1 to that count
uint16_t openDrum_machineParameter(int _kind, int _index);
const char* openDrum_machineParameterName(int _kind, int _index);
int openDrum_machineParameters(int _kind);
// A Machine parameter's 0-127 as the firmware's value: a choice of a few in as many steps of 0-127
uint8_t openDrum_machineValue(int _kind, int _index, uint8_t _value);
// The Machine value of the init kit's
uint8_t openDrum_machineDefault(int _kind, int _index);

#ifdef __cplusplus
}
#endif
