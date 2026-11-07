// The OS's side of the Machinedrum's Rhythm Echo and Gate Box, ported to C++: the Kit's eight bytes of each effect,
// their smoothing, and the routines that turn them into the words the mixer DSP's code reads. mdEngine runs the OS's
// own 68k code for these ($20B4AE, $20B940); mdFastSendsTest compares this port with it word for word.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

#include "mdFastEcho.h"
#include "mdFastGateBox.h"
#include "mdVoiceCoefficients.h"

namespace md::fast
{
	// The master bytes of each effect, as a Kit holds them
	enum GateBoxByte { GateDvol, GatePred, GateDec, GateDamp, GateHp, GateLp, GateGate, GateLev, GateBoxBytes };
	enum EchoByte { EchoTime, EchoMod, EchoMfrq, EchoFb, EchoFltf, EchoFltw, EchoMono, EchoLev };

	// An effect's bytes after the smoothing, value << 7 at most $3FFF, as the OS holds them
	using MasterValues = std::array<uint16_t, 8>;

	// The Echo's words. _tempo: BPM x 24, 720 to 7200, as the OS holds it. TIME counts in tempo: the delay in samples
	// is the one number here that depends on 44.1 kHz (ticket 16). onePole comes with the owner's import (spec 11.4):
	// without it the table is zeros, and the filter words $7FFFFF.
	EchoWords echoWords(const voice::OsTables& _tables, const MasterValues& _values, uint32_t _tempo);

	// Words 0 and 1 of the Echo's: its delay, in samples of 44.1 kHz, what echoWords works out from TIME and the tempo
	void echoDelayWords(int32_t _delay, EchoWords& _words);

	GateBoxWords gateBoxWords(const voice::OsTables& _tables, const MasterValues& _values);

	// The OS's control of the two effects on its tick (11 blocks of 32 samples): the bytes slew, as all the master's do
	// (voice::Smoother's step), and one master effect in turn gets new words: the Echo on a tick of four, the Gate Box
	// three ticks later (Dynamix and the EQ on the two between).
	class SendWords
	{
	public:
		enum class Due { None, Echo, GateBox };

		explicit SendWords(const voice::OsTables& _tables) : m_tables(_tables) {}

		void setGateBox(const int _byte, const uint8_t _value) { m_smoother.setTarget(_byte, _value); }
		void setEcho(const int _byte, const uint8_t _value) { m_smoother.setTarget(EchoFirst + _byte, _value); }
		// BPM x 24; held to the OS's 720 to 7200, which echoWords divides by
		void setTempo(const uint32_t _tempo) { m_tempo = std::clamp(_tempo, 720u, 7200u); }

		// One tick; which effect's words are new, echo() or gateBox()
		Due tick();

		const EchoWords& echo() const { return m_echo; }
		const GateBoxWords& gateBox() const { return m_gateBox; }

	private:
		static constexpr int EchoFirst = GateBoxBytes;

		MasterValues values(int _first) const;

		const voice::OsTables& m_tables;
		voice::Smoother m_smoother;
		uint32_t m_tempo = 125 * 24;
		uint32_t m_ticks = 0;
		EchoWords m_echo{};
		GateBoxWords m_gateBox{};
	};
}
