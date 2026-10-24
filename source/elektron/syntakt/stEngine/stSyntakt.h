#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace syntakt
{
	struct Samples;

	namespace main
	{
		struct Section3;
	}

	// The Syntakt's digital path as a plug-in drives it: OS 1.41's main CPU (CPU #1: the parameters' smoothing, the
	// locks, the modulation, LFOs, glides, filter envelopes, the block for the voice CPU, then the mix, the delay and
	// the reverb) and its voice CPU (CPU #2: the eight digital tracks' machines and stages), both ported bit for bit,
	// one 32-sample block at a time at 48 kHz. No analog track, no analog mixer, no Main: what leaves is the eight
	// tracks' stems, the effects' returns and the two digital buses the machine sends to its analog mixer.
	//
	// It starts without the OS's boot, from a defined state: CPU #1's SRAM as its boot copies it and the delay's and
	// reverb's inits, a default kit made of the parameter descriptors' defaults (each track's default machine), 120
	// BPM. What the OS's sequencer and MIDI would leave between two blocks comes from the calls below: a call takes
	// effect in the next block the module computes, so at the block that holds the sample it is made at, rounded up
	// to the block's start.
	//
	// Made with 16 tracks, it is two such machines side by side, A (tracks 1-8) and B (tracks 9-16), each a whole CPU
	// #1 and voice CPU, with one delay and one reverb: B's sends join A's at the effects' inputs (each bus added
	// saturated after A's own, as the firmware adds its ADC side's), B's effects do not run, the returns are A's.
	// Every stem is still the machine's; the returns are exact for one machine's sends and an extension for both
	class Syntakt
	{
	public:
		static constexpr int Tracks = 8;		// a machine's, and an 8-track module's
		static constexpr int MaxTracks = 16;
		static constexpr int SampleRate = 48000;

		// What render writes, each channel Q31 (int32_t), as CPU #1 computes it: the stems are Overbridge's channels
		// 2-9 (each track's voice after its velocity, VOL and LEV gain, before pan and sends), the returns its channels
		// 16-17 (delay return plus reverb return, saturated), the direct bus the tracks and returns not routed to the FX
		// track (x 10, as the DACs take it), the FX bus the ones routed (x 8). An 8-track module writes the first
		// Outputs; a 16-track one also tracks 9-16's stems and B's two buses, which hold B's tracks alone (the returns
		// are on A's), each as B's machine scales and saturates it: they reach the analog mixer apart, which sums them
		// without a Q31 ceiling, so the plug-in sums them in its own format
		enum Output
		{
			Stem1, Stem2, Stem3, Stem4, Stem5, Stem6, Stem7, Stem8,
			ReturnsLeft, ReturnsRight,
			DirectLeft, DirectRight,
			FxLeft, FxRight,
			Outputs,
			Stem9 = Outputs, Stem10, Stem11, Stem12, Stem13, Stem14, Stem15, Stem16,
			DirectLeftB, DirectRightB,
			FxLeftB, FxRightB,
			Outputs16
		};

		// A digital track's parameters, by the firmware's internal id (the descriptors at 0x4022d59c of section 3, as
		// SyntHack's docs/ui-parameters.md lists them): id i is the word at offset 28 + 2 i of the track's 142 bytes.
		// A value is a word, value << 8 | fine, 0 to 0x7f00 (0x4000 the middle of a bipolar one) unless noted
		enum TrackParameter : int
		{
			Lfo1Speed = 1, Lfo1Multiplier, Lfo1Fade, Lfo1Destination, Lfo1Waveform, Lfo1StartPhase, Lfo1Mode, Lfo1Depth,
			Lfo2Speed, Lfo2Multiplier, Lfo2Fade, Lfo2Destination, Lfo2Waveform, Lfo2StartPhase, Lfo2Mode, Lfo2Depth,
			// The SYN page: TUNE, five of the machine's own (named by machine in the descriptors), DEC and OVER (the
			// drive stage's)
			Tune = 18, Synth1, Synth2, Synth3, Synth4, Synth5, Decay = 25, Overdrive,
			FilterType = 27, FilterFrequency, FilterResonance, FilterEnvelopeDepth, FilterEnvelopeDelay, FilterAttack,
			FilterDecay, FilterSustain, FilterRelease, FilterEnvelopeReset, FilterBase, FilterWidth,
			AmpAttack = 40, AmpHold, AmpDecay, AmpSustain, AmpRelease,
			DelaySend, ReverbSend, Pan, Volume,
			Routing,			// ROUT: 0 the direct bus, else the FX bus (the analog FX track)
			AmpMode, AmpEnvelopeReset,
			PortamentoTime, Portamento,
			TrackParameterEnd	// ids from 0 to below this one are the track's own
		};

		// The FX track's parameters the digital path reads: the delay and the reverb, by internal id
		enum FxParameter : int
		{
			DelayTime = 17, DelayPingPong, DelayWidth, DelayFeedback, DelayHighPass, DelayLowPass, DelayReverbSend,
			DelayVolume,
			DelayRouting,		// 0 the return to the direct bus, else to the FX bus
			ReverbPreDelay = 27, ReverbDecay, ReverbShelfFrequency, ReverbShelfGain, ReverbHighPass, ReverbLowPass,
			ReverbVolume,
			ReverbRouting,		// as DelayRouting
			FxParameterEnd = 72
		};

		// _section3, _section7: the user's OS 1.41 sections (main::loadSection3, loadSection7), which must outlive
		// the module. _tracks: 8 or 16 (any other count makes 8)
		Syntakt(const main::Section3& _section3, const std::vector<int32_t>& _section7, int _tracks = Tracks);
		~Syntakt();

		// 8 or 16, and the outputs render writes: Outputs or Outputs16
		int tracks() const;
		int outputs() const;

		// The samples SP TWINSHOT plays, as CPU #1 would load them; none as it is made. _samples must outlive the
		// module or the next call, and not change while a block renders
		void setSamples(const Samples* _samples);

		// True when _machine (0-44) is one of the twelve the digital tracks play
		bool isPorted(int _machine) const;

		// Track _track (0 to tracks() - 1) plays _machine, a ported one (else nothing changes and it returns false);
		// its SYN parameters (ids 18-26) move to the machine's defaults, as setParameter moves them
		bool setMachine(int _track, int _machine);

		// The kit's value of a parameter, as the OS writes it when a knob turns: the parameter moves to it through
		// CPU #1's smoothing (about 22 ms). _id below TrackParameterEnd, below FxParameterEnd
		void setParameter(int _track, int _id, uint16_t _value);
		void setFxParameter(int _id, uint16_t _value);

		// Track _track's level (LEV, the kit's, 0 to 0x7f00): the buses and the stems take it squared
		void setLevel(int _track, uint16_t _value);

		// The kit as the module holds it: the defaults it starts with, then what the calls above set (a lock leaves
		// it as it is). 0 outside the module's tracks or ids
		int machine(int _track) const;
		uint16_t parameter(int _track, int _id) const;
		uint16_t fxParameter(int _id) const;
		uint16_t level(int _track) const;

		// The range section 3's descriptors give a value: a track's parameter _id when the track plays _machine (the
		// machine's own descriptors for the SYN page), an FX parameter; first the minimum, then the maximum, 0 to
		// 0x7f00 where no descriptor names the id
		struct Range
		{
			uint16_t min = 0;
			uint16_t max = 0x7f00;
		};

		Range parameterRange(int _machine, int _id) const;
		Range fxParameterRange(int _id) const;

		// The tempo, which the LFOs synced to it and the delay's time follow
		void setTempo(double _bpm);

		// A parameter lock for track _track's next noteOn: the parameter jumps to _value with that trig, without the
		// smoothing, and goes back to the kit's value at the track's trig after (the OS's locks of a trig)
		void lock(int _track, int _id, uint16_t _value);

		// A trig of track _track: the note (0-127, semitones), the velocity (1-127), and the trig's amp, filter
		// envelope and LFO restarts, as the trig's defaults have them on. noteOff ends the gate (the amp in ADSR
		// MODE, the filter envelope) as the trig's length running out does: in the next block, or in the block after
		// when the note started in this one; a noteOn of the track in the same block takes its place
		void noteOn(int _track, int _note, int _velocity);
		void noteOff(int _track);

		// _count samples of each output into _outputs[0] to _outputs[outputs() - 1]
		void render(int32_t* const* _outputs, size_t _count);

		// The time the last block computed took by stage, in seconds, as std::chrono::steady_clock measures it: CPU
		// #1's block for the voice CPU (buildBlock), CPU #1's mix with the delay and the reverb, the voice CPU (its
		// voices and the tracks' stages), each summed over the two machines of a 16-track module; blocks counts the
		// blocks computed
		struct BlockTimes
		{
			double build = 0;
			double mix = 0;
			double voices = 0;
			uint64_t blocks = 0;
		};

		const BlockTimes& blockTimes() const;

	private:
		struct State;

		std::unique_ptr<State> m_state;
	};
}
