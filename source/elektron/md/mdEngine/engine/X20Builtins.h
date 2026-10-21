#pragma once

#include <array>
#include <cstdint>

namespace md::engine::x20
{
	// md-drums: the built-in Machines that MDX X.20 adds and that MoDule plays from the user's own X.20 OS (the map of
	// X.20's built-in Machines). Their voice DSP code is X.20's, read from the user's file; their coefficient routines
	// (X.20 B's $21ada2, $21acbc, $21abd2, $2197d2, $2196c0, $219402) are ported here from X.20's arithmetic, checked
	// against the routines themselves run in a 68k on the user's file (mdX20PortTest), as md::mds::convertFine ports
	// $21aeba. INP-CA and INP-CB are out of the map.
	enum Machine : uint8_t
	{
		GndSnPro = 1,	// replaces GND-SN at its number, as under X.20
		GndSw = 4,
		GndPu = 5,
		NfxEv = 7,
		NfxCo = 8,
		NfxUc = 9
	};

	constexpr bool isBuiltin(const uint8_t _id)
	{
		return _id == GndSnPro || _id == GndSw || _id == GndPu || _id == NfxEv || _id == NfxCo || _id == NfxUc;
	}

	// The 8 parameters' names (5 characters at most) and defaults (0-127), from X.20's Machine table ($2b31a6)
	struct Info
	{
		std::array<const char*, 8> names;
		std::array<uint8_t, 8> defaults;
	};
	const Info& info(uint8_t _id);

	using DecayTable = std::array<uint32_t, 512>;	// OS 1.63's ($24bf94), which X.20's routines read through +$90

	// The routine of built-in _id on the voice's parameter words (14 bits, setting << 7, after the smoothing and the
	// LFOs): its words for the voice DSP's slot from word 1 on, into _out. _mode is the context's tuning mode (+$8c, 0
	// DEFAULT), _tempo24 the tempo in BPM times 24, clamped to 20-280 BPM, as NFX-EV reads it ($2cdc6c). The tuning
	// tables are X.20's at TUNING 0. Returns the number of words (9 or 10); a word the routine leaves alone keeps what
	// _out held. 0 for an id that is no built-in of X.20.
	int compute(uint8_t _id, const uint16_t* _params, int _mode, const DecayTable& _decay, uint32_t _tempo24,
		uint32_t* _out);
}
