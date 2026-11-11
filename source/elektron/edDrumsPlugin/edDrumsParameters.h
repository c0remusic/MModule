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
	constexpr int DefaultDelayNote = 9, DefaultLfoNote = 6;		// 1/4, 1 Bar
	// Beats of delayNoteSpec's and lfoNoteSpecs' choices
	double delayNoteQuarters(int _choice);
	double lfoNoteQuarters(int _choice);

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

	// The value from _min to _max whose text is _text, the nearest to the parameter's own value among those that show it;
	// else the one whose text's number is nearest to the number _text starts with
	int valueOf(const std::string& _id, const std::string& _text, int _min, int _max, const Other& _other);
}
