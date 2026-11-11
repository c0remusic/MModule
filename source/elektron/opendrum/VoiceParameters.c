/*
 * VoiceParameters.c
 *
 *  MModule: the cases of MidiParser.c's midiParser_ccHandler that set the drum voice 1, snare, cymbal and hihat
 *  voices, each on the voice it is given, by front panel parameter number (Parameters.h). Values arrive as the front
 *  panel sends them, 0-127 (value.itg), never as a float modulator. MidiParser.c was created on 02.04.2012.
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */

#include "VoiceParameters.h"
#include "Parameters.h"
#include "globals.h"
#include "valueShaper.h"

//-----------------------------------------------------------
static inline float calcPitchModAmount(uint8_t data2)
{
	const float val = data2/127.f;
	return val*val*PITCH_AMOUNT_FACTOR;
}
//-----------------------------------------------------------
static void setCoarse(OscInfo* osc, const uint8_t itg)
{
	//clear upper nibble
	osc->midiFreq &= 0x00ff;
	//set upper nibble
	osc->midiFreq |= itg << 8;
	osc_recalcFreq(osc);
}
//-----------------------------------------------------------
static void setFine(OscInfo* osc, const uint8_t itg)
{
	//clear lower nibble
	osc->midiFreq &= 0xff00;
	//set lower nibble
	osc->midiFreq |= itg;
	osc_recalcFreq(osc);
}
//-----------------------------------------------------------
static void setFilterFreq(ResonantFilter* filter, const float flt)
{
	const float f = flt;
	//exponential full range freq
	SVF_directSetFilterValue(filter,valueShaperF2F(f,FILTER_SHAPER) );
}
//-----------------------------------------------------------
static float transientPitch(const uint8_t itg)
{
	return 1.f + ((itg/33.9f)-0.75f) ;// range about  0.25 to 4 => 1/4 to 1*4
}
//-----------------------------------------------------------
void drum_setParameter(DrumVoice* v, const uint16_t par, const uint8_t itg)
{
	const float flt = itg/127.f;
	switch(par)
	{
	case PAR_OSC_WAVE_DRUM1:		v->osc.waveform = itg; break;
	case PAR_COARSE1:				setCoarse(&v->osc, itg); break;
	case PAR_FINE1:					setFine(&v->osc, itg); break;
	case PAR_MOD_WAVE_DRUM1:		v->modOsc.waveform = itg; break;
	case PAR_FILTER_FREQ_1:			setFilterFreq(&v->filter, flt); break;
	case PAR_RESO_1:				SVF_setReso(&v->filter, flt); break;
	case PAR_VELOA1:				slopeEg2_setAttack(&v->oscVolEg,itg,AMP_EG_SYNC); break;
	case PAR_VELOD1:				slopeEg2_setDecay(&v->oscVolEg,itg,AMP_EG_SYNC); break;
	case PAR_VOL_SLOPE1:			slopeEg2_setSlope(&v->oscVolEg,itg); break;
	case PAR_MOD_EG1:				DecayEg_setDecay(&v->oscPitchEg,itg); break;
	case PAR_MODAMNT1:				v->egPitchModAmount = calcPitchModAmount(itg); break;
	case PAR_PITCH_SLOPE1:			DecayEg_setSlope(&v->oscPitchEg,itg); break;
	case PAR_FMAMNT1:				v->fmModAmount = flt; break;
	case PAR_FM_FREQ1:				setCoarse(&v->modOsc, itg); break;
	case PAR_VOL1:					v->vol = flt; break;
	case PAR_DRIVE1:				setDistortionShape(&v->distortion,flt); break;
	case PAR_FILTER_DRIVE_1:		SVF_setDrive(&v->filter, flt); break;
	case PAR_MIX_MOD_1:				v->mixOscs = itg != 0; break;
	case PAR_VOLUME_MOD_ON_OFF1:	v->volumeMod = itg != 0; break;
	case PAR_FILTER_TYPE_1:			v->filterType = itg+1; break;
	case PAR_TRANS1_VOL:			v->transGen.volume = flt; break;
	case PAR_TRANS1_WAVE:			transient_setWaveform(&v->transGen, itg); break;
	case PAR_TRANS1_FREQ:			v->transGen.pitch = transientPitch(itg); break;
	case PAR_PWM_1:					v->pwm = flt; break;
	default: break;
	}
}
//-----------------------------------------------------------
void snare_setParameter(SnareVoice* v, const uint16_t par, const uint8_t itg)
{
	const float flt = itg/127.f;
	switch(par)
	{
	case PAR_OSC_WAVE_SNARE:		v->osc.waveform = itg; break;
	case PAR_COARSE4:				setCoarse(&v->osc, itg); break;
	case PAR_FINE4:					setFine(&v->osc, itg); break;
	case PAR_NOISE_FREQ1:			v->noiseOsc.freq = flt*22000; break;
	case PAR_MIX1:					v->mix = flt; break;
	case PAR_FILTER_FREQ_4:			setFilterFreq(&v->filter, flt); break;
	case PAR_RESO_4:				SVF_setReso(&v->filter, flt); break;
	case PAR_VELOA4:				slopeEg2_setAttack(&v->oscVolEg,itg,false); break;
	case PAR_VELOD4:				slopeEg2_setDecay(&v->oscVolEg,itg,false); break;
	case PAR_VOL_SLOPE4:			slopeEg2_setSlope(&v->oscVolEg,itg); break;
	case PAR_REPEAT4:				v->oscVolEg.repeat = itg; break;
	case PAR_MOD_EG4:				DecayEg_setDecay(&v->oscPitchEg,itg); break;
	case PAR_MODAMNT4:				v->egPitchModAmount = calcPitchModAmount(itg); break;
	case PAR_PITCH_SLOPE4:			DecayEg_setSlope(&v->oscPitchEg,itg); break;
	case PAR_VOL4:					v->vol = flt; break;
	case PAR_SNARE_DISTORTION:		setDistortionShape(&v->distortion,flt); break;
	case PAR_FILTER_DRIVE_4:		SVF_setDrive(&v->filter, flt); break;
	case PAR_VOLUME_MOD_ON_OFF4:	v->volumeMod = itg != 0; break;
	case PAR_FILTER_TYPE_4:			v->filterType = itg + 1; break; // +1 because 0 is filter off which results in silence
	case PAR_TRANS4_VOL:			v->transGen.volume = flt; break;
	case PAR_TRANS4_WAVE:			transient_setWaveform(&v->transGen, itg); break;
	case PAR_TRANS4_FREQ:			v->transGen.pitch = transientPitch(itg); break;
	case PAR_PWM_4:					v->pwm = flt; break;
	default: break;
	}
}
//-----------------------------------------------------------
void cymbal_setParameter(CymbalVoice* v, const uint16_t par, const uint8_t itg)
{
	const float flt = itg/127.f;
	switch(par)
	{
	case PAR_WAVE1_CYM:				v->osc.waveform = itg; break;
	case PAR_COARSE5:				setCoarse(&v->osc, itg); break;
	case PAR_FINE5:					setFine(&v->osc, itg); break;
	case PAR_WAVE2_CYM:				v->modOsc.waveform = itg; break;
	case PAR_WAVE3_CYM:				v->modOsc2.waveform = itg; break;
	case PAR_MOD_OSC_F1_CYM:		setCoarse(&v->modOsc, itg); break;
	case PAR_MOD_OSC_F2_CYM:		setCoarse(&v->modOsc2, itg); break;
	case PAR_MOD_OSC_GAIN1_CYM:		v->fmModAmount1 = flt; break;
	case PAR_MOD_OSC_GAIN2_CYM:		v->fmModAmount2 = flt; break;
	case PAR_FILTER_FREQ_5:			setFilterFreq(&v->filter, flt); break;
	case PAR_RESO_5:				SVF_setReso(&v->filter, flt); break;
	case PAR_VELOA5:				slopeEg2_setAttack(&v->oscVolEg,itg,false); break;
	case PAR_VELOD5:				slopeEg2_setDecay(&v->oscVolEg,itg,false); break;
	case PAR_VOL_SLOPE5:			slopeEg2_setSlope(&v->oscVolEg,itg); break;
	case PAR_REPEAT5:				v->oscVolEg.repeat = itg; break;
	case PAR_VOL5:					v->vol = flt; break;
	case PAR_CYMBAL_DISTORTION:		setDistortionShape(&v->distortion,flt); break;
	case PAR_FILTER_DRIVE_5:		SVF_setDrive(&v->filter, flt); break;
	case PAR_VOLUME_MOD_ON_OFF5:	v->volumeMod = itg != 0; break;
	case PAR_FILTER_TYPE_5:			v->filterType = itg+1; break;
	case PAR_TRANS5_VOL:			v->transGen.volume = flt; break;
	case PAR_TRANS5_WAVE:			transient_setWaveform(&v->transGen, itg); break;
	case PAR_TRANS5_FREQ:			v->transGen.pitch = transientPitch(itg); break;
	case PAR_PWM_5:					v->pwm = flt; break;
	default: break;
	}
}
//-----------------------------------------------------------
void hihat_setParameter(HiHatVoice* v, const uint16_t par, const uint8_t itg)
{
	const float flt = itg/127.f;
	switch(par)
	{
	case PAR_WAVE1_HH:				v->osc.waveform = itg; break;
	case PAR_COARSE6:				setCoarse(&v->osc, itg); break;
	case PAR_FINE6:					setFine(&v->osc, itg); break;
	case PAR_WAVE2_HH:				v->modOsc.waveform = itg; break;
	case PAR_WAVE3_HH:				v->modOsc2.waveform = itg; break;
	case PAR_MOD_OSC_F1:			setCoarse(&v->modOsc, itg); break;
	case PAR_MOD_OSC_F2:			setCoarse(&v->modOsc2, itg); break;
	case PAR_MOD_OSC_GAIN1:			v->fmModAmount1 = flt; break;
	case PAR_MOD_OSC_GAIN2:			v->fmModAmount2 = flt; break;
	case PAR_FILTER_FREQ_6:			setFilterFreq(&v->filter, flt); break;
	case PAR_RESO_6:				SVF_setReso(&v->filter, flt); break;
	case PAR_VELOA6:				slopeEg2_setAttack(&v->oscVolEg,itg,false); break;
	case PAR_VELOD6_CLOSED:			v->decayClosed = slopeEg2_calcDecay(itg); break;
	case PAR_VELOD6_OPEN:			v->decayOpen = slopeEg2_calcDecay(itg); break;
	case PAR_VOL_SLOPE6:			slopeEg2_setSlope(&v->oscVolEg,itg); break;
	case PAR_VOL6:					v->vol = flt; break;
	case PAR_HAT_DISTORTION:		setDistortionShape(&v->distortion,flt); break;
	case PAR_FILTER_DRIVE_6:		SVF_setDrive(&v->filter, flt); break;
	case PAR_VOLUME_MOD_ON_OFF6:	v->volumeMod = itg != 0; break;
	case PAR_FILTER_TYPE_6:			v->filterType = itg+1; break;
	case PAR_TRANS6_VOL:			v->transGen.volume = flt; break;
	case PAR_TRANS6_WAVE:			transient_setWaveform(&v->transGen, itg); break;
	case PAR_TRANS6_FREQ:			v->transGen.pitch = transientPitch(itg); break;
	case PAR_PWM_6:					v->pwm = flt; break;
	default: break;
	}
}
