/*
 * VoiceParameters.h
 *
 *  MModule: the voices' cases of MidiParser.c, by front panel parameter number (Parameters.h), on the voice given.
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */

#ifndef VOICEPARAMETERS_H_
#define VOICEPARAMETERS_H_

#include "DrumVoice.h"
#include "Snare.h"
#include "CymbalVoice.h"
#include "HiHat.h"

// A parameter another voice reads changes nothing
void drum_setParameter(DrumVoice* v, uint16_t par, uint8_t itg);
void snare_setParameter(SnareVoice* v, uint16_t par, uint8_t itg);
void cymbal_setParameter(CymbalVoice* v, uint16_t par, uint8_t itg);
void hihat_setParameter(HiHatVoice* v, uint16_t par, uint8_t itg);

#endif /* VOICEPARAMETERS_H_ */
