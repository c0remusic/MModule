// The Machinedrum OS's side of a voice ported to C++, without a 68k emulator: the coefficient functions that turn a
// machine's eight parameters into its packet for the voice DSP, and the smoothing of the parameters. mdEngine runs the
// OS's own code for both (MachineRunner, Musashi); mdVoiceCoefficientsTest compares this port with it over the OS's
// parameter range. Ticket 08 of .scratch/elektron-drums-v1.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace md::voice
{
	// The tables the coefficient functions read, as OS 1.63's image holds them, from their formulas: nothing here comes
	// from the OS
	struct OsTables
	{
		std::array<uint32_t, 128> pitch{};		// $24BD94, one octave of a sine's step
		std::array<uint32_t, 512> decay{};		// $24BF94, an envelope's factor per step, towards 1
		std::array<uint32_t, 512> rise{};		// $24C794, 0 then a rise towards 1
		std::array<uint32_t, 128> octaves{};	// $24D794, a log: 8833 is an octave of the pitch table

		OsTables();
	};

	// The words of a slot, its type or 0 then the machine's coefficients: at most TRX-S2's 21
	using Packet = std::array<uint32_t, 21>;

	// The machines (ids) whose coefficient function is ported: the GND ones, the TRX ones, the EFM ones, ROM-01 to -48;
	// mdVoiceCoefficientsTest holds them to Voice::ported
	bool coefficientsPorted(uint8_t _machine);

	// The OS's tick runs every 11 blocks of 32 samples (125.3 Hz), as mdEngine's HostModel has it
	constexpr int kBlocksPerTick = 11;

	// As the OS's tick fills a voice's slot: word 0 the machine's type (id + 1) on a trigger, else 0, then the
	// coefficient function's words on _params, the machine's eight parameters after smoothing and modulation (value <<
	// 7, which must be at most $3FFF, as the OS holds them: above it a table's index stays inside but the word is not
	// the OS's), the words it does not write 0. Tempo: BPM x 24, 720 to 7200 as the OS holds it at $100150C.
	// Returns the words to give Voice::setSlot, at least 1: word 0 alone for a machine that computes on a trigger only.
	// A machine not ported (coefficientsPorted) throws std::out_of_range; a ported one allocates nothing.
	int coefficients(const OsTables& _tables, uint8_t _machine, const uint16_t* _params, bool _trigger, uint32_t _tempo,
		Packet& _out);

	// The OS's smoothing of a Track's 24 parameters ($10002E0), one step per tick: value = (3 value + target << 7) >>
	// 2. A tick's packet reads the values from before that tick's step. With no LFO the values are what coefficients()
	// reads; an LFO, on the OS, modulates them first.
	class Smoother
	{
	public:
		static constexpr size_t kParams = 24;
		using Values = std::array<uint16_t, kParams>;

		// 0 to 127
		void setTarget(const size_t _param, const uint8_t _value) { m_target[_param] = _value; }

		// Target and value at once, as a Hit that brings a machine or a Lock sets them
		void snap(const size_t _param, const uint8_t _value)
		{
			m_target[_param] = _value;
			m_value[_param] = static_cast<uint16_t>(_value << 7);
		}

		void tick()
		{
			for(size_t p = 0; p < kParams; ++p)
				m_value[p] = static_cast<uint16_t>((3u * m_value[p] + (static_cast<uint32_t>(m_target[p]) << 7)) >> 2);
		}

		const Values& values() const { return m_value; }

	private:
		std::array<uint8_t, kParams> m_target{};
		Values m_value{};
	};
}
