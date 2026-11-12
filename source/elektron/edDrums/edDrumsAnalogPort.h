#pragma once

#include <cassert>
#include <cstdint>

#include "edDrumsAnalog.h"

// What the ported routines share (edDrumsAnalog.h): the CPU's multiply-accumulate unit as the routines run it, the
// voice's memory by name, and the helpers every family calls. Inside edDrums only.
namespace elektron::drums::analog::port
{
	// The voice's frame (VoiceIo::frame), tracks 9-11's voices
	namespace frame
	{
		constexpr int Cv = 0;			// four control words
		constexpr int Envelope = 8;		// the envelope generator's 32 bytes: commands and rates
		constexpr int Level = 40;		// four level words
	}
	// ... and track 12's voice
	namespace metalFrame
	{
		constexpr int Cv = 0;			// five control words
		constexpr int Word6 = 10;		// two words a utility sets on this voice
		constexpr int Word10 = 12;
		constexpr int Topology = 14;	// a byte: how the oscillators and filters are wired
		constexpr int Envelope = 15;	// the envelope generator's 32 bytes
		constexpr int Oscillators = 47;	// twelve longs: the six oscillators' rate words and what follows them
		constexpr int Setup = 95;		// four longs a machine change sets
	}

	// The multiply-accumulate unit: four 48-bit accumulators, the mode word (MACSR). Fractional (0x20): a product
	// (y x) << 1 truncated to bits 71-24, a load or store shifted by 8; integer otherwise. OMC (0x80) saturates an
	// accumulator that overflows, which then holds until loaded or cleared. Rounding and the 16-bit store are not used
	// by these routines
	class Emac
	{
	public:
		static constexpr uint32_t Fractional = 0x20;
		static constexpr uint32_t Omc = 0x80;

		explicit Emac(const bool _saturate) : m_macsr(Fractional | (_saturate ? Omc : 0)) {}

		uint32_t macsr() const { return m_macsr; }
		void setMacsr(const uint32_t _v)
		{
			assert((_v & 0x50) == 0 && "rounding and the unsigned modes are not ported");
			m_macsr = _v & 0xFF;
			for(bool& p : m_pav)
				p = false;
		}

		// mac.l Ry,Rx,ACCn / msac.l; a word form takes the half _upper* says, as the instruction's .u or .l
		void mac(const int _n, const int32_t _y, const int32_t _x) { accumulate(_n, _y, _x, false, true, false, false, 0); }
		void msac(const int _n, const int32_t _y, const int32_t _x) { accumulate(_n, _y, _x, true, true, false, false, 0); }
		void macWord(const int _n, const int32_t _y, const bool _upperY, const int32_t _x, const bool _upperX, const bool _sub = false,
			const int _scale = 0)
		{
			accumulate(_n, _y, _x, _sub, false, _upperY, _upperX, _scale);
		}
		// with a scale factor (<< 1 is 1, >> 1 is 3), which counts in integer mode only
		void macScaled(const int _n, const int32_t _y, const int32_t _x, const int _scale, const bool _sub = false)
		{
			accumulate(_n, _y, _x, _sub, true, false, false, _scale);
		}

		// move.l Ry,ACCn
		void load(const int _n, const int32_t _v)
		{
			m_acc[_n] = fractional() ? static_cast<int64_t>(_v) * 256 : _v;
			m_pav[_n] = false;
		}
		// move.l ACCn,Rx and movclr.l
		int32_t store(const int _n) const
		{
			const int64_t a = m_acc[_n];
			const int64_t v = fractional() ? a >> 8 : a;
			if(m_macsr & Omc)
			{
				if(v > INT32_MAX)
					return INT32_MAX;
				if(v < INT32_MIN)
					return INT32_MIN;
			}
			return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(v)));
		}
		int32_t storeClear(const int _n)
		{
			const int32_t v = store(_n);
			m_acc[_n] = 0;
			m_pav[_n] = false;
			return v;
		}
		void clear(const int _n)
		{
			m_acc[_n] = 0;
			m_pav[_n] = false;
		}

		bool fractional() const { return (m_macsr & Fractional) != 0; }

	private:
		static int64_t wrap48(const int64_t _v) { return static_cast<int64_t>(static_cast<uint64_t>(_v) << 16) >> 16; }

		void accumulate(int _n, int32_t _y, int32_t _x, bool _sub, bool _long, bool _upperY, bool _upperX, int _scale);

		int64_t m_acc[4] = {};
		bool m_pav[4] = {};
		uint32_t m_macsr;
	};

	// The instructions the compiler of these routines leaves that C++ has no word for
	// Shifts by a register count, taken modulo 64 as the CPU does: 32 or more shifts everything out
	inline int32_t lsl(const int32_t _v, int _n)
	{
		_n &= 63;
		return _n >= 32 ? 0 : static_cast<int32_t>(static_cast<uint32_t>(_v) << _n);
	}
	inline int32_t lsr(const int32_t _v, int _n)
	{
		_n &= 63;
		return _n >= 32 ? 0 : static_cast<int32_t>(static_cast<uint32_t>(_v) >> _n);
	}
	inline int32_t asr(const int32_t _v, int _n)
	{
		_n &= 63;
		return _n >= 32 ? (_v < 0 ? -1 : 0) : _v >> _n;
	}
	inline int32_t add(const int32_t _a, const int32_t _b) { return static_cast<int32_t>(static_cast<uint32_t>(_a) + static_cast<uint32_t>(_b)); }
	inline int32_t sub(const int32_t _a, const int32_t _b) { return static_cast<int32_t>(static_cast<uint32_t>(_a) - static_cast<uint32_t>(_b)); }
	// ff1.l: the bit number of the highest set bit counted from bit 31 (0 for bit 31), 32 for 0
	int ff1(uint32_t _v);
	// sats.l after an add or subtract that overflowed (_overflow): the saturated value of its sign
	inline int32_t sats(const int32_t _v, const bool _overflow) { return _overflow ? (_v < 0 ? INT32_MAX : INT32_MIN) : _v; }
	inline int32_t byteRev(const int32_t _v)
	{
		const auto u = static_cast<uint32_t>(_v);
		return static_cast<int32_t>(u >> 24 | (u >> 8 & 0xff00) | (u << 8 & 0xff0000) | u << 24);
	}
	// 0x7fffffff etc. as what they are: _num / _den of full scale in Q31, truncated
	constexpr int32_t q31(const int64_t _num, const int64_t _den) { return static_cast<int32_t>((_num << 31) / _den); }

	// The shared helpers, each as the CPU computes it (the vectors of edDrumsAnalogControlTest check every one)

	// A pitch word from a tune word and a note word: (tune << 13) and (note << 5) at 2/3 each, offset
	int32_t pitchWord(Emac& _e, int32_t _tune, int32_t _note);
	// The same with the tune at 1/3: a modulator's
	int32_t modPitchWord(Emac& _e, int32_t _tune, int32_t _note);
	// 2^x of a pitch word, bounded to the range a voice plays
	uint32_t expPitch(Emac& _e, int32_t _word);
	// 2^x, its fraction in 24 bits and its octave in the top byte, by an interpolated table of 2^(k/2048)
	uint32_t exp2(Emac& _e, int32_t _x);
	// log2 in Q24 of a positive value; the minimum for 0, 0 below
	int32_t log2(Emac& _e, int32_t _x);
	// How many blocks a decay at envelope rate _rate takes to fall to _level
	int32_t decayTicks(Emac& _e, int32_t _level, int32_t _rate);
	// ... to the level every routine uses
	int32_t decayTicksDefault(Emac& _e, int32_t _rate);
	// (a b 2)^2 of two words: a velocity's square law
	int32_t velocitySquare(Emac& _e, int32_t _a, int32_t _b);
	// A knob word (value << 8 | fine) into a signed fraction; _other its complement, as the CPU's d1 holds it
	int32_t knob(Emac& _e, int32_t _word, int32_t& _other);
	// A knob word into two fractions, one rising and one falling about the centre, each bounded
	void knobPair(Emac& _e, int32_t _word, int32_t& _rising, int32_t& _falling);

	// A table a routine reads that no formula gives exactly: a fit made here, or a sample of our own where the original
	// holds a recording. Each family lists its own (kickTables...), so that a test can check a fit against the original's
	// words and swap the original's in, to prove the logic exact, then put the fits back
	struct Table
	{
		const char* name;		// unique, the test's key
		void* data;				// count values of width bytes, native
		int count;
		int width;				// 2 or 4
		int64_t bound;			// a fit: the most |fit - original| may be, in the value's units; -1: our own sample
	};
	// Each family's tables; all of them in one list
	const Table* kickTables(int& _count);
	const Table* snareTables(int& _count);
	const Table* synthTables(int& _count);
	const Table* metalTables(int& _count);
	const Table* utilityTables(int& _count);

	// How many tables a routine builds on its first use have been built: warmUp builds them all before audio runs, so that
	// none is built on the audio thread (edDrumsAnalogTest counts that none is during a render)
	int& tablesBuilt();
	// Each family's tables built at once, called by warmUp
	void warmUpKick();
	void warmUpSnare();
	void warmUpSynth();
	void warmUpUtility();

	// The machines' routines, each one block of the voice (control() calls them; a family a file)
	void kickHard(VoiceIo& _io);
	void kickClassic(VoiceIo& _io);
	void kickFm(VoiceIo& _io);
	void kickPlastic(VoiceIo& _io);
	void kickSilky(VoiceIo& _io);
	void kickSharp(VoiceIo& _io);
	void kickAcoustic(VoiceIo& _io);
	void snareHard(VoiceIo& _io);
	void snareClassic(VoiceIo& _io);
	void snareFm(VoiceIo& _io);
	void snareNatural(VoiceIo& _io);
	void snareAcoustic(VoiceIo& _io);
	void rimHard(VoiceIo& _io);
	void rimClassic(VoiceIo& _io);
	void synthDual(VoiceIo& _io);
	void synthRaw(VoiceIo& _io);
	void synthChip(VoiceIo& _io);
	void closedClassic(VoiceIo& _io);
	void openClassic(VoiceIo& _io);
	void cymbalClassic(VoiceIo& _io);
	void bellClassic(VoiceIo& _io);
	void closedMetallic(VoiceIo& _io);
	void openMetallic(VoiceIo& _io);
	void cymbalMetallic(VoiceIo& _io);
	void bellMetallic(VoiceIo& _io);
	void ride(VoiceIo& _io);
	void hatBasic(VoiceIo& _io);
	void hatLab(VoiceIo& _io);
	void noise(VoiceIo& _io);
	void impulse(VoiceIo& _io);
	// Track 12's topology at a change of its machine
	void setupMetal(Machine _m, VoiceIo& _io);
}