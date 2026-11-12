#pragma once

#include <array>
#include <cstdint>

#include "edDrumsAnalog.h"

// The analog voices' circuits (ticket 28): models of our own, written here from classic circuits, that play what the
// ported control (edDrumsAnalog.h) computes. Nothing of the original's sound is here; its control is.
//
// A Voice runs its control every 32 samples of 48 kHz, as the original's CPU does, and its circuit at the host's rate
// (44.1 kHz on the engine's Machinedrum blocks): a control tick lasts 29 or 30 host samples (147:160), and what the
// control sends per 48 kHz sample (outA, the noise) is read across the tick at its own rate.
namespace elektron::drums::analog
{
	// What a circuit keeps between ticks: each family lays out its own state in these, named in its file
	struct Circuit
	{
		alignas(16) std::array<float, 192> f{};
		std::array<uint32_t, 16> u{};
	};

	// A Hit: what the circuit restarts (phases, filters it clears), before the control's first tick of the Hit. Nothing is
	// cleared for a family: each clears what its circuit restarts, so that what runs free survives the Hit
	void trigCircuit(Machine _m, Circuit& _c);
	// The machine's 20 parameter bytes from its Machine 1-8 (0-127): what the original's control reads there
	void paramsOf(Machine _m, const uint8_t* _controls, std::array<uint8_t, 20>& _params);
	// Whether _m's routine reads Machine _index + 1 (0-7): one it does not is unused, named nothing
	bool usesControl(Machine _m, int _index);
	// SYN1-8 of _m as a machine change gives them: our own defaults, chosen by ear
	std::array<uint8_t, 8> defaultsOf(Machine _m);
	// Whether every envelope of the circuit has reached zero, whatever phase the control is in (a control may park in a
	// phase for good, as the original's does): a family's own judgment of its circuit
	bool endedOf(Machine _m, const VoiceIo& _io, const Circuit& _c);

	// Each family's part of the above, in its file (edDrumsAnalogKickVoice.cpp...): render adds the host's samples of one
	// control tick of _m's circuit, from what the control just wrote into _io: _n samples (1 to 64) at _rate
	namespace circuit
	{
#define ED_ANALOG_FAMILY(_name) \
		void _name##Render(Machine _m, const VoiceIo& _io, Circuit& _c, float* _out, int _n, double _rate); \
		void _name##Trig(Machine _m, Circuit& _c); \
		void _name##Params(Machine _m, const uint8_t* _controls, std::array<uint8_t, 20>& _params); \
		std::array<uint8_t, 8> _name##Defaults(Machine _m); \
		bool _name##Ended(Machine _m, const VoiceIo& _io, const Circuit& _c); \
		void _name##Advance(Machine _m, Circuit& _c, int64_t _samples, double _rate);
		ED_ANALOG_FAMILY(kick)
		ED_ANALOG_FAMILY(snare)
		ED_ANALOG_FAMILY(synth)
		ED_ANALOG_FAMILY(metal)
		ED_ANALOG_FAMILY(utility)
#undef ED_ANALOG_FAMILY
	}
	// One Track's analog voice
	class Voice
	{
	public:
		static constexpr int TickSamples = 32;			// a control tick, in 48 kHz samples
		static constexpr double ControlRate = 48000.0;
		static constexpr int MaxRate = 192000;			// the highest host rate render takes: it plays nothing above

		// _m from its next Hit; the controls as setControls gives them
		void setMachine(Machine _m);
		Machine machine() const { return m_machine; }
		void setControls(const uint8_t* _controls8);
		void setControl(const int _index, const uint8_t _value) { m_controls[static_cast<size_t>(_index & 7)] = _value; }
		// A Hit started the voice and its circuit says an envelope is still above zero. The engine lets a voice go once it is
		// not active and its output has stayed quiet, whatever phase the control is in
		bool active() const { return m_sounding && !endedOf(m_machine, m_io, m_circuit); }
		// semitones, 60 the machine's own pitch
		void setNote(int _note);
		// _now: the engine's host-sample clock at the Hit, so that what runs free (a metal voice's oscillators) moves by the
		// samples a frozen voice was not rendered, in one step; -1 where no clock runs (a voice alone, from its start)
		void trig(int64_t _now = -1);
		// _n host samples at _rate added to _out
		void render(float* _out, int _n, double _rate);

	private:
		void tick();

		Machine m_machine = Machine::KickHard;
		Machine m_pending = Machine::KickHard;
		std::array<uint8_t, 8> m_controls{};
		int m_note = 60;
		VoiceIo m_io;
		Circuit m_circuit;
		bool m_sounding = false;	// a Hit has started the control
		int64_t m_ticks = 0;		// ticks since the Hit: tick k ends at host sample (k + 1) 32 rate / 48000, rounded down
		std::array<float, MaxRate * TickSamples / 48000 + 8> m_buf{};	// the current tick's host samples, at MaxRate
		int m_count = 0, m_pos = 0;
		int64_t m_clock = 0;		// host samples rendered or passed, as trig's _now counts them
		double m_rate = 44100.0;
	};
}
