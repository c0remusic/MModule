#pragma once

#include <array>
#include <functional>
#include <string>

#include "edDrumsEngine.h"

// The host parameters added by ticket 18 of .scratch/elektron-drums-v1, as tables the processor builds them from, and
// what turns their values into the engine's settings. Each Track's: its chain, its two LFOs, its four sends, its
// accent; then the effects the Tracks share, the master and the oversampling. Their IDs are frozen
// (edDrumsParameterIds.txt): a new one is appended, none renamed.
namespace elektron::drums::parameters
{
	// A parameter: an integer from min to max, a toggle, or a choice among choices ("|" between them)
	struct Spec
	{
		const char* id;
		const char* name;
		int min = 0, max = 127;
		const char* choices = nullptr;
		bool toggle = false;
	};

	// A Track's chain, in ChainSettings' order
	enum ChainValue
	{
		Drive1Type, Drive1Post, Drive1Amount, Drive1Mix, Drive2Type, Drive2Post, Drive2Amount, Drive2Mix,
		BandKind, BandAfter, Band1, Band2, Band3, Band4, Band5, Band6,
		FilterKind, FilterMode, FilterFrequency, FilterResonance, FilterWidth,
		EnvelopeDelay, EnvelopeAttack, EnvelopeDecay, EnvelopeSustain, EnvelopeRelease, EnvelopeReset, EnvelopeDepth,
		KeyTrack,
		AmpOn, AmpAttack, AmpHold, AmpDecay, AmpSustain, AmpRelease, AmpAdsr,
		VelTarget, VelocityDepth,
		ChainValues
	};

	// An LFO's; its Hz is a float parameter of its own, built apart
	enum LfoValue
	{
		LfoSync, LfoSpeed, LfoFeelValue, LfoHz, LfoShapeValue, LfoModeValue, LfoDepth, LfoTrack, LfoTargetValue,
		LfoValues
	};

	using ChainArray = std::array<int, ChainValues>;
	using LfoArray = std::array<double, LfoValues>;

	extern const std::array<Spec, ChainValues> chainSpecs;
	extern const std::array<Spec, LfoValues> lfoSpecs;		// ids and names after "lfo1_", "LFO1 "
	extern const std::array<Spec, SendCount> sendSpecs;
	extern const Spec accentSpec;

	// The effects the Tracks share, each byte in its setter's order
	extern const std::array<Spec, 2> sourceSpecs;			// the delay's, the reverb's
	extern const std::array<Spec, SendCount> sendOutputSpecs;
	extern const std::array<Spec, 8> echoSpecs;				// md::fast::EchoByte
	extern const std::array<Spec, 8> gateBoxSpecs;			// md::fast::GateBoxByte
	extern const std::array<Spec, StDelayBytes> syntaktDelaySpecs;
	extern const std::array<Spec, StReverbBytes> syntaktReverbSpecs;
	extern const Spec syntaktDelaySendSpec;
	extern const std::array<Spec, ChorusBytes> chorusSpecs;
	extern const std::array<Spec, PhaserBytes> phaserSpecs;
	extern const Spec masterOnSpec;
	extern const std::array<Spec, MasterBytes> masterSpecs;
	extern const Spec oversamplingSpec;						// 1x, 2x, 4x

	// Appended by ticket 14: either delay's time as a note of the host's tempo while Sync is on, else the free Hz; the
	// chorus's and the phaser's LFO a turn per note value while their Sync is on; the DP/4's S&H rate
	extern const Spec delaySyncSpec, delayNoteSpec;
	extern const std::array<Spec, 2> lfoSyncSpecs, lfoNoteSpecs;	// the chorus's, the phaser's
	extern const Spec dp4SampleHoldSpec;
	// The chorus's type (Sends' ChorusType), appended after the relays
	extern const Spec chorusTypeSpec;
	constexpr int DefaultDelayNote = 9, DefaultLfoNote = 6;		// 1/4, 1 Bar
	// Beats of delayNoteSpec's and lfoNoteSpecs' choices
	double delayNoteQuarters(int _choice);
	double lfoNoteQuarters(int _choice);

	// The relays for Push (ticket 18, as MoDule's: docs/md-drums-push2.md): 64 host parameters in Push's banks of 8, each
	// the parameter of the Track the editor shows whose ID ends in its target ("t05_" + target for Track 5), the first
	// the Track shown itself (target nullptr). Their IDs are "push_" + target ("push_track" for the first), appended to
	// edDrumsParameterIds.txt; their names fit Push's ten characters. What a player reaches for: Machine 1-8; the
	// Track's level, pan, note and sends; the filter and its envelope; the two drives; the EQ; the amp and the accent;
	// each LFO. Left to the Track's own parameters: Machine 9-24, the velocity, the filter envelope's delay, sustain,
	// release and retrig, key tracking, an LFO's Track, the Out, the machine and the Chokes
	struct Relay
	{
		const char* target;
		const char* name;
	};
	constexpr int RelayCount = 64;
	inline constexpr std::array<Relay, RelayCount> relaySpecs{{
		{"m1", "Machine 1"}, {"m2", "Machine 2"}, {"m3", "Machine 3"}, {"m4", "Machine 4"},
		{"m5", "Machine 5"}, {"m6", "Machine 6"}, {"m7", "Machine 7"}, {"m8", "Machine 8"},
		{nullptr, "Track"}, {"level", "Level"}, {"pan", "Pan"}, {"note", "Note"},
		{"send_delay", "Snd Delay"}, {"send_reverb", "Snd Reverb"}, {"send_chorus", "Snd Chorus"},
		{"send_phaser", "Snd Phaser"},
		{"flt_type", "Flt Type"}, {"flt_mode", "Flt Mode"}, {"flt_freq", "Cutoff"}, {"flt_res", "Resonance"},
		{"flt_width", "Flt Width"}, {"fenv_attack", "Env Attack"}, {"fenv_decay", "Env Decay"},
		{"fenv_depth", "Env Depth"},
		{"drv1_type", "Drv1 Type"}, {"drv1_amount", "Drv1 Amt"}, {"drv1_mix", "Drv1 Mix"}, {"drv1_post", "Drv1 Place"},
		{"drv2_type", "Drv2 Type"}, {"drv2_amount", "Drv2 Amt"}, {"drv2_mix", "Drv2 Mix"}, {"drv2_post", "Drv2 Place"},
		{"band_type", "EQ Type"}, {"band_after", "EQ Place"}, {"band_1", "EQ 1"}, {"band_2", "EQ 2"},
		{"band_3", "EQ 3"}, {"band_4", "EQ 4"}, {"band_5", "EQ 5"}, {"band_6", "EQ 6"},
		{"amp_attack", "Attack"}, {"amp_hold", "Hold"}, {"amp_decay", "Decay"}, {"amp_sustain", "Sustain"},
		{"amp_release", "Release"}, {"amp_mode", "Amp Mode"}, {"amp_on", "Amp Env"}, {"accent", "Accent"},
		{"lfo1_target", "L1 Target"}, {"lfo1_depth", "L1 Depth"}, {"lfo1_shape", "L1 Wave"}, {"lfo1_mode", "L1 Trig"},
		{"lfo1_sync", "L1 Sync"}, {"lfo1_speed", "L1 Note"}, {"lfo1_hz", "L1 Rate"}, {"lfo1_feel", "L1 Feel"},
		{"lfo2_target", "L2 Target"}, {"lfo2_depth", "L2 Depth"}, {"lfo2_shape", "L2 Wave"}, {"lfo2_mode", "L2 Trig"},
		{"lfo2_sync", "L2 Sync"}, {"lfo2_speed", "L2 Note"}, {"lfo2_hz", "L2 Rate"}, {"lfo2_feel", "L2 Feel"},
	}};
	constexpr int TrackRelay = 8;	// relaySpecs' index of the Track shown

	// The defaults of the shared effects' bytes: an effect that a Track's send reaches sounds, with no feedback
	extern const std::array<int, 8> echoDefaults, gateBoxDefaults;
	extern const std::array<int, StDelayBytes> syntaktDelayDefaults;
	extern const std::array<int, StReverbBytes> syntaktReverbDefaults;
	extern const std::array<int, ChorusBytes> chorusDefaults;
	extern const std::array<int, PhaserBytes> phaserDefaults;
	extern const std::array<int, MasterBytes> masterDefaults;	// Master's own, which leave Main as it is

	// The chain a machine's family brings, as Engine::setMachine loads it; Off brings none, value-initialised
	ChainSettings familyChain(const Machine& _machine);

	ChainArray valuesOf(const ChainSettings& _settings);
	ChainSettings chainOf(const ChainArray& _values);
	LfoArray valuesOf(const LfoSettings& _settings);
	LfoSettings lfoOf(const LfoArray& _values);

	// The value of another parameter by its ID, a Track's without its "tNN_": the same Track's, or a shared one
	using Other = std::function<int(const char* _id)>;

	// What the host and the editor show of an integer parameter's _value: Hz, dB, seconds, notes, % or Q where the engine's
	// law gives the unit (some depend on another parameter: a filter's or drive's type, the band's, the phaser's mode),
	// else the number. _id as in Other
	std::string valueText(const std::string& _id, int _value, const Other& _other);

	// What the Sampler's Machine _index (0-23) shows of _value: GRAIN, BITS, COMPANDING and OUTPUT FILTER in their units
	// (edDrumsVintage.h), the number for the others
	std::string samplerText(int _index, int _value);

	// What _machine's Machine _index + 1 (_index 0-23) shows of _value, in the unit its engine's law gives: TUNE in
	// semitones from 64 on the Syntakt's machines and the ANALOG kicks, the note and cents of VA DRUMS' TUNE and FINE,
	// every HYPER FM control in sophie::machineValue's units, the Sampler's as samplerText; else the number, as the
	// machines' own pages show it (the Machinedrum's are 0-127, their laws per machine in the voice DSP's code)
	std::string machineText(const Machine& _machine, int _index, int _value);

	// A MIDI note's name, C4 for 60 as the editor's NOTE lists name it
	std::string noteName(int _note);

	// A free rate's text (an LFO's, the delay's): Hz to three figures, OFF at 0; and back, kHz read as 1000
	std::string rateText(double _hz);
	double rateOf(const std::string& _text);

	// The value from _min to _max whose text is _text, the nearest to the parameter's own value among those that show it;
	// else the one whose text's number is nearest to the number _text starts with
	int valueOf(const std::string& _id, const std::string& _text, int _min, int _max, const Other& _other);
	// The same for _machine's Machine _index + 1, its own value _own
	int machineValueOf(const Machine& _machine, int _index, const std::string& _text, int _own);
}
