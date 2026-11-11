// SPDX-License-Identifier: GPL-3.0-only
// Part of this port (MModule), linked with the GPL-3.0 voice sources of this folder.
#include "openDrum.h"

#include "InitKit.h"
#include "Parameters.h"
#include "VoiceParameters.h"

struct OpenDrumVoice
{
	int kind;
	uint32_t rng;		// the voice's random generator, which its noise oscillators share
	union
	{
		DrumVoice drum;
		SnareVoice snare;
		CymbalVoice cymbal;
		HiHatVoice hat;
	} v;
};

// Per kind, what Machine 1-24 set: a front panel parameter, the name shown, and the choices of one that is not a knob
// (0: a knob, its 0-127 the firmware's; n: 0-127 in n steps, the firmware's 0 to n - 1)
typedef struct
{
	uint16_t parameter;
	const char* name;
	uint8_t steps;
} MachineParameter;

#define Waves 6			// NUM_OSC_WAVEFORMS: a wave past them plays a user sample, which the voices do not have
#define FilterTypes 7	// FILTER_LP to FILTER_NAIVE_2_POLE, as the parameter's value + 1
#define Transients 14	// the snap, the offset, then NUM_TRANSIENTS samples

static const MachineParameter MachineParameters[OpenDrumKinds][OpenDrumMachineParameters] =
{
	{
		{PAR_COARSE1, "TUNE"}, {PAR_VELOD1, "DECAY"}, {PAR_MOD_EG1, "PITCH DEC"}, {PAR_MODAMNT1, "PITCH AMT"},
		{PAR_FMAMNT1, "FM AMT"}, {PAR_FM_FREQ1, "FM FREQ"}, {PAR_TRANS1_VOL, "CLICK"}, {PAR_DRIVE1, "DRIVE"},
		{PAR_FINE1, "FINE"}, {PAR_OSC_WAVE_DRUM1, "WAVE", Waves}, {PAR_MOD_WAVE_DRUM1, "FM WAVE", Waves},
		{PAR_MIX_MOD_1, "FM/MIX", 2}, {PAR_VELOA1, "ATTACK"}, {PAR_VOL_SLOPE1, "DEC SLOPE"},
		{PAR_PITCH_SLOPE1, "PITCH SLP"}, {PAR_PWM_1, "PWM"},
		{PAR_FILTER_FREQ_1, "FILTER"}, {PAR_RESO_1, "RESO"}, {PAR_FILTER_TYPE_1, "FILT TYPE", FilterTypes},
		{PAR_FILTER_DRIVE_1, "FILT DRIVE"}, {PAR_TRANS1_WAVE, "CLICK WAVE", Transients}, {PAR_TRANS1_FREQ, "CLICK FREQ"},
	},
	{
		{PAR_COARSE4, "TUNE"}, {PAR_VELOD4, "DECAY"}, {PAR_MOD_EG4, "PITCH DEC"}, {PAR_MODAMNT4, "PITCH AMT"},
		{PAR_NOISE_FREQ1, "NOISE"}, {PAR_MIX1, "MIX"}, {PAR_FILTER_FREQ_4, "FILTER"}, {PAR_SNARE_DISTORTION, "DRIVE"},
		{PAR_FINE4, "FINE"}, {PAR_OSC_WAVE_SNARE, "WAVE", Waves}, {PAR_VELOA4, "ATTACK"}, {PAR_VOL_SLOPE4, "DEC SLOPE"},
		{PAR_REPEAT4, "REPEAT"}, {PAR_PITCH_SLOPE4, "PITCH SLP"}, {PAR_PWM_4, "PWM"}, {PAR_RESO_4, "RESO"},
		{PAR_FILTER_TYPE_4, "FILT TYPE", FilterTypes}, {PAR_FILTER_DRIVE_4, "FILT DRIVE"}, {PAR_TRANS4_VOL, "CLICK"},
		{PAR_TRANS4_WAVE, "CLICK WAVE", Transients}, {PAR_TRANS4_FREQ, "CLICK FREQ"},
	},
	{
		{PAR_COARSE5, "TUNE"}, {PAR_VELOD5, "DECAY"}, {PAR_MOD_OSC_F1_CYM, "MOD1 FREQ"}, {PAR_MOD_OSC_F2_CYM, "MOD2 FREQ"},
		{PAR_MOD_OSC_GAIN1_CYM, "MOD1 AMT"}, {PAR_MOD_OSC_GAIN2_CYM, "MOD2 AMT"}, {PAR_FILTER_FREQ_5, "FILTER"},
		{PAR_CYMBAL_DISTORTION, "DRIVE"},
		{PAR_FINE5, "FINE"}, {PAR_WAVE1_CYM, "WAVE", Waves}, {PAR_WAVE2_CYM, "MOD1 WAVE", Waves},
		{PAR_WAVE3_CYM, "MOD2 WAVE", Waves}, {PAR_VELOA5, "ATTACK"}, {PAR_VOL_SLOPE5, "DEC SLOPE"},
		{PAR_REPEAT5, "REPEAT"}, {PAR_PWM_5, "PWM"},
		{PAR_RESO_5, "RESO"}, {PAR_FILTER_TYPE_5, "FILT TYPE", FilterTypes}, {PAR_FILTER_DRIVE_5, "FILT DRIVE"},
		{PAR_TRANS5_VOL, "CLICK"}, {PAR_TRANS5_WAVE, "CLICK WAVE", Transients}, {PAR_TRANS5_FREQ, "CLICK FREQ"},
	},
	{
		{PAR_COARSE6, "TUNE"}, {PAR_VELOD6_CLOSED, "CLOSED"}, {PAR_VELOD6_OPEN, "OPEN DEC"}, {PAR_MOD_OSC_F1, "MOD1 FREQ"},
		{PAR_MOD_OSC_GAIN1, "MOD1 AMT"}, {PAR_FILTER_FREQ_6, "FILTER"}, {PAR_HAT_DISTORTION, "DRIVE"},
		{OpenDrumOpen, "OPEN", 2},
		{PAR_FINE6, "FINE"}, {PAR_WAVE1_HH, "WAVE", Waves}, {PAR_WAVE2_HH, "MOD1 WAVE", Waves},
		{PAR_WAVE3_HH, "MOD2 WAVE", Waves}, {PAR_MOD_OSC_F2, "MOD2 FREQ"}, {PAR_MOD_OSC_GAIN2, "MOD2 AMT"},
		{PAR_VELOA6, "ATTACK"}, {PAR_VOL_SLOPE6, "DEC SLOPE"},
		{PAR_RESO_6, "RESO"}, {PAR_FILTER_TYPE_6, "FILT TYPE", FilterTypes}, {PAR_FILTER_DRIVE_6, "FILT DRIVE"},
		{PAR_TRANS6_VOL, "CLICK"}, {PAR_TRANS6_WAVE, "CLICK WAVE", Transients}, {PAR_TRANS6_FREQ, "CLICK FREQ"},
		{PAR_PWM_6, "PWM"},
	},
};
// The init kit holds the name's 8 bytes, then the parameters from 0
static uint8_t kitValue(const uint16_t _parameter)
{
	return _parameter + 8 < INIT_KIT_SIZE ? initKit[_parameter + 8] : 0;
}

size_t openDrum_size(void)
{
	return sizeof(OpenDrumVoice);
}

void openDrum_init(OpenDrumVoice* _voice, const int _kind)
{
	// As the firmware's zeroed globals
	memset(_voice, 0, sizeof(*_voice));
	_voice->kind = _kind;
	_voice->rng = OpenDrumSeed;
	switch(_kind)
	{
	case OpenDrumDrum:
		initDrumVoice(&_voice->v.drum, 0);
		_voice->v.drum.osc.rng = _voice->v.drum.modOsc.rng = &_voice->rng;
		break;
	case OpenDrumSnare:
		Snare_init(&_voice->v.snare);
		_voice->v.snare.osc.rng = _voice->v.snare.noiseOsc.rng = &_voice->rng;
		break;
	case OpenDrumCymbal:
		Cymbal_init(&_voice->v.cymbal);
		_voice->v.cymbal.osc.rng = _voice->v.cymbal.modOsc.rng = _voice->v.cymbal.modOsc2.rng = &_voice->rng;
		break;
	default:
		HiHat_init(&_voice->v.hat);
		_voice->v.hat.osc.rng = _voice->v.hat.modOsc.rng = _voice->v.hat.modOsc2.rng = &_voice->rng;
		break;
	}
	// The kit in the order the firmware's start sends it
	for(uint16_t p = 0; p < END_OF_SOUND_PARAMETERS; ++p)
		openDrum_setParameter(_voice, p, kitValue(p));
	openDrum_settle(_voice);
}

void openDrum_setParameter(OpenDrumVoice* _voice, const uint16_t _parameter, const uint8_t _value)
{
	switch(_voice->kind)
	{
	case OpenDrumDrum:		drum_setParameter(&_voice->v.drum, _parameter, _value); break;
	case OpenDrumSnare:		snare_setParameter(&_voice->v.snare, _parameter, _value); break;
	case OpenDrumCymbal:	cymbal_setParameter(&_voice->v.cymbal, _parameter, _value); break;
	default:				hihat_setParameter(&_voice->v.hat, _parameter, _value); break;
	}
}

void openDrum_settle(OpenDrumVoice* _voice)
{
	switch(_voice->kind)
	{
	case OpenDrumDrum:
		_voice->v.drum.FaderVolumeFilter.b = 1.0;
		_voice->v.drum.MixerPageFilter[0].b = _voice->v.drum.vol;
		_voice->v.drum.distortion.DriveParamFilter.b = _voice->v.drum.distortion.shape;
		break;
	case OpenDrumSnare:		_voice->v.snare.FaderVolumeFilter.b = 1.0; break;
	case OpenDrumCymbal:	_voice->v.cymbal.FaderVolumeFilter.b = 1.0; break;
	default:				_voice->v.hat.FaderVolumeFilter.b = 1.0; break;
	}
}

void openDrum_trigger(OpenDrumVoice* _voice, const uint8_t _velocity, const uint8_t _note, const int _open)
{
	switch(_voice->kind)
	{
	case OpenDrumDrum:		Drum_trigger(&_voice->v.drum, _velocity, _note); break;
	case OpenDrumSnare:		Snare_trigger(&_voice->v.snare, _velocity, _note); break;
	case OpenDrumCymbal:	Cymbal_trigger(&_voice->v.cymbal, _velocity, _note); break;
	default:				HiHat_trigger(&_voice->v.hat, _velocity, _open ? 1 : 0, _note); break;
	}
}

void openDrum_render(OpenDrumVoice* _voice, int16_t* _out)
{
	// As mixer_calcNextSampleBlock: the filter's coefficient, the envelopes, then the block
	switch(_voice->kind)
	{
	case OpenDrumDrum:
		SVF_recalcFreq(&_voice->v.drum.filter);
		calcDrumVoiceAsync(&_voice->v.drum);
		calcDrumVoiceSyncBlock(&_voice->v.drum, _out, OpenDrumBlock);
		break;
	case OpenDrumSnare:
		SVF_recalcFreq(&_voice->v.snare.filter);
		Snare_calcAsync(&_voice->v.snare);
		Snare_calcSyncBlock(&_voice->v.snare, _out, OpenDrumBlock);
		break;
	case OpenDrumCymbal:
		SVF_recalcFreq(&_voice->v.cymbal.filter);
		Cymbal_calcAsync(&_voice->v.cymbal);
		Cymbal_calcSyncBlock(&_voice->v.cymbal, _out, OpenDrumBlock);
		break;
	default:
		SVF_recalcFreq(&_voice->v.hat.filter);
		HiHat_calcAsync(&_voice->v.hat);
		HiHat_calcSyncBlock(&_voice->v.hat, _out, OpenDrumBlock);
		break;
	}
}

static int envelopeOpen(const SlopeEg2* _eg)
{
	return _eg->state != EG_STOPPED && !(_eg->state == EG_D && _eg->value <= 0);
}

int openDrum_sounding(const OpenDrumVoice* _voice)
{
	switch(_voice->kind)
	{
	case OpenDrumDrum:
		// The block after the envelope's end still ramps from its last gain
		return envelopeOpen(&_voice->v.drum.oscVolEg) || _voice->v.drum.lastGain > 0;
	case OpenDrumSnare:		return envelopeOpen(&_voice->v.snare.oscVolEg);
	case OpenDrumCymbal:	return envelopeOpen(&_voice->v.cymbal.oscVolEg);
	default:				return envelopeOpen(&_voice->v.hat.oscVolEg);
	}
}

uint16_t openDrum_machineParameter(const int _kind, const int _index)
{
	const MachineParameter* p = &MachineParameters[_kind][_index];
	return p->name ? p->parameter : OpenDrumNone;
}

const char* openDrum_machineParameterName(const int _kind, const int _index)
{
	return MachineParameters[_kind][_index].name;
}

int openDrum_machineParameters(const int _kind)
{
	int count = 0;
	while(count < OpenDrumMachineParameters && MachineParameters[_kind][count].name)
		++count;
	return count;
}

uint8_t openDrum_machineValue(const int _kind, const int _index, const uint8_t _value)
{
	const uint8_t steps = MachineParameters[_kind][_index].steps;
	return steps ? (uint8_t)(_value * steps / 128) : _value;
}

uint8_t openDrum_machineDefault(const int _kind, const int _index)
{
	const MachineParameter* p = &MachineParameters[_kind][_index];
	if(!p->name || p->parameter == OpenDrumOpen)
		return 0;
	const uint8_t value = kitValue(p->parameter);
	// The lowest knob value of the kit's step
	return p->steps ? (uint8_t)((value * 128 + p->steps - 1) / p->steps) : value;
}