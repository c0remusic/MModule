#pragma once

#include <array>
#include <cstdio>
#include <string>

#include "stSyntakt.h"

// ST Drums' host parameters. Their string IDs are frozen: a host's set keeps them (VST3 hashes them), so a new
// parameter is appended and none is renumbered or renamed in its ID. Per track (the name's prefix T01-T16, the ID's
// t01-t16): Machine (t01_machine), Note (t01_note), Level (t01_level), then each parameter of syntakt::Syntakt's
// TrackParameter table by its internal id (t01_p01 ... t01_p53); then the FX track's delay and reverb (fx_p17 ...
// fx_p34); then Tempo (tempo). Values are 0-127, the word's top byte, as the Syntakt's knobs show them.
namespace stDrums::parameters
{
	using syntakt::Syntakt;

	struct Entry
	{
		int id;
		const char* name;
	};

	constexpr std::array<Entry, 50> Track{{
		{Syntakt::Lfo1Speed, "LFO1 Speed"}, {Syntakt::Lfo1Multiplier, "LFO1 Multiplier"},
		{Syntakt::Lfo1Fade, "LFO1 Fade"}, {Syntakt::Lfo1Destination, "LFO1 Destination"},
		{Syntakt::Lfo1Waveform, "LFO1 Waveform"}, {Syntakt::Lfo1StartPhase, "LFO1 Start Phase"},
		{Syntakt::Lfo1Mode, "LFO1 Mode"}, {Syntakt::Lfo1Depth, "LFO1 Depth"},
		{Syntakt::Lfo2Speed, "LFO2 Speed"}, {Syntakt::Lfo2Multiplier, "LFO2 Multiplier"},
		{Syntakt::Lfo2Fade, "LFO2 Fade"}, {Syntakt::Lfo2Destination, "LFO2 Destination"},
		{Syntakt::Lfo2Waveform, "LFO2 Waveform"}, {Syntakt::Lfo2StartPhase, "LFO2 Start Phase"},
		{Syntakt::Lfo2Mode, "LFO2 Mode"}, {Syntakt::Lfo2Depth, "LFO2 Depth"},
		{Syntakt::Tune, "Tune"}, {Syntakt::Synth1, "Synth 1"}, {Syntakt::Synth2, "Synth 2"},
		{Syntakt::Synth3, "Synth 3"}, {Syntakt::Synth4, "Synth 4"}, {Syntakt::Synth5, "Synth 5"},
		{Syntakt::Decay, "Decay"}, {Syntakt::Overdrive, "Overdrive"},
		{Syntakt::FilterType, "Filter Type"}, {Syntakt::FilterFrequency, "Filter Frequency"},
		{Syntakt::FilterResonance, "Filter Resonance"}, {Syntakt::FilterEnvelopeDepth, "Filter Env Depth"},
		{Syntakt::FilterEnvelopeDelay, "Filter Env Delay"}, {Syntakt::FilterAttack, "Filter Attack"},
		{Syntakt::FilterDecay, "Filter Decay"}, {Syntakt::FilterSustain, "Filter Sustain"},
		{Syntakt::FilterRelease, "Filter Release"}, {Syntakt::FilterEnvelopeReset, "Filter Env Reset"},
		{Syntakt::FilterBase, "Filter Base"}, {Syntakt::FilterWidth, "Filter Width"},
		{Syntakt::AmpAttack, "Amp Attack"}, {Syntakt::AmpHold, "Amp Hold"}, {Syntakt::AmpDecay, "Amp Decay"},
		{Syntakt::AmpSustain, "Amp Sustain"}, {Syntakt::AmpRelease, "Amp Release"},
		{Syntakt::DelaySend, "Delay Send"}, {Syntakt::ReverbSend, "Reverb Send"}, {Syntakt::Pan, "Pan"},
		{Syntakt::Volume, "Volume"}, {Syntakt::Routing, "Routing"}, {Syntakt::AmpMode, "Amp Mode"},
		{Syntakt::AmpEnvelopeReset, "Amp Env Reset"}, {Syntakt::PortamentoTime, "Portamento Time"},
		{Syntakt::Portamento, "Portamento"},
	}};

	constexpr std::array<Entry, 17> Fx{{
		{Syntakt::DelayTime, "Delay Time"}, {Syntakt::DelayPingPong, "Delay Ping Pong"},
		{Syntakt::DelayWidth, "Delay Width"}, {Syntakt::DelayFeedback, "Delay Feedback"},
		{Syntakt::DelayHighPass, "Delay High Pass"}, {Syntakt::DelayLowPass, "Delay Low Pass"},
		{Syntakt::DelayReverbSend, "Delay Reverb Send"}, {Syntakt::DelayVolume, "Delay Volume"},
		{Syntakt::DelayRouting, "Delay Routing"},
		{Syntakt::ReverbPreDelay, "Reverb Pre-Delay"}, {Syntakt::ReverbDecay, "Reverb Decay"},
		{Syntakt::ReverbShelfFrequency, "Reverb Shelf Freq"}, {Syntakt::ReverbShelfGain, "Reverb Shelf Gain"},
		{Syntakt::ReverbHighPass, "Reverb High Pass"}, {Syntakt::ReverbLowPass, "Reverb Low Pass"},
		{Syntakt::ReverbVolume, "Reverb Volume"}, {Syntakt::ReverbRouting, "Reverb Routing"},
	}};

	constexpr std::array<const char*, 12> MachineNames{"BD MODERN", "SD BASIC", "CY ALLOY", "PC CARBON", "SY TONE",
		"SY CHORD", "SD VINTAGE", "CP VINTAGE", "SY TOY", "SY BITS", "SY SWARM", "SP TWINSHOT"};

	// The trig's note when nothing sets it: the firmware's Trig Note default, 60 for every machine
	constexpr int DefaultNote = 60;

	constexpr int PerTrack = 3 + static_cast<int>(Track.size());
	constexpr int Count = 16 * PerTrack + static_cast<int>(Fx.size()) + 1;

	inline std::string trackPrefix(const int _track, const bool _id)
	{
		char text[8];
		std::snprintf(text, sizeof text, _id ? "t%02d_" : "T%02d ", _track + 1);
		return text;
	}

	inline std::string valueId(const char* _prefix, const int _id)
	{
		char text[16];
		std::snprintf(text, sizeof text, "%sp%02d", _prefix, _id);
		return text;
	}
}
