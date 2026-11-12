#pragma once

#include <array>
#include <cstdint>

// The control of the Syntakt's analog voices (tracks 9-12, ticket 28 of .scratch/elektron-drums-v1), ported from what
// its main CPU computes for them every block of 32 samples at 48 kHz: the machine's parameters into control values,
// switches, envelope commands and the short digital signals it sends the voice. Nothing of the circuits is here: a
// voice model of our own (edDrumsAnalogVoice.h) reads what this computes.
//
// The port works on the voice's own memory, VoiceIo, as the CPU's routines do on theirs, byte for byte, so that a vector
// recorded from the original routine (edDrumsAnalogControlTest, vectors kept outside the repository) compares every byte
// the routine wrote. Its layout is the port's, voice-relative: the vectors' recorder maps the firmware's memory onto it.
// The unit is an untrimmed one: the calibration a real voice reads from its flash is zero here, as in the vectors.
namespace elektron::drums::analog
{
	// The machines, in the port's order: tracks 9-11's (kicks, snares, rims, synths), then track 12's (metal), then the
	// two utilities, which play on any of the four
	enum class Machine : uint8_t
	{
		KickHard, KickClassic, KickFm, KickPlastic, KickSilky, KickSharp, KickAcoustic,
		SnareHard, SnareClassic, SnareFm, SnareNatural, SnareAcoustic,
		RimHard, RimClassic,
		SynthDual, SynthRaw, SynthChip,
		ClosedClassic, OpenClassic, CymbalClassic, BellClassic, ClosedMetallic, OpenMetallic, CymbalMetallic,
		BellMetallic, Ride, HatBasic, HatLab,
		Noise, Impulse,
		Count
	};
	constexpr int Machines = static_cast<int>(Machine::Count);
	constexpr int FirstMetal = static_cast<int>(Machine::ClosedClassic);
	constexpr int FirstUtility = static_cast<int>(Machine::Noise);

	// Track 12's voice is voice 3, made for metal and noise; voices 0-2 are tracks 9-11's
	constexpr int MetalVoice = 3;
	inline bool isMetal(const Machine _m) { return static_cast<int>(_m) >= FirstMetal && static_cast<int>(_m) < FirstUtility; }
	inline bool isUtility(const Machine _m) { return static_cast<int>(_m) >= FirstUtility; }
	// Whether voice _voice can play _m
	inline bool plays(const int _voice, const Machine _m)
	{
		if(isUtility(_m))
			return _voice >= 0 && _voice <= MetalVoice;
		return isMetal(_m) ? _voice == MetalVoice : _voice >= 0 && _voice < MetalVoice;
	}

	// One voice's memory. Multi-byte values are big-endian, as the CPU holds them (be16, be32 below)
	struct VoiceIo
	{
		int voice = 0;					// 0-3
		bool saturate = false;			// the multiplier saturates on overflow (MACSR's OMC), as where a routine before set it
		int32_t pitch = 0;				// the note after glide and bend, semitones in Q16
		int32_t tempo = 14400;			// what a tempo-synced step adds a block: the CPU's tempo word, 14400 as the vectors hold it

		std::array<uint8_t, 20> params{};	// the machine's 10 words: SYN1-8 and two more, value << 8 | fine
		std::array<uint8_t, 32> state{};	// the envelope's state machine
		std::array<uint8_t, 44> smooth{};	// the control signals' smoothing filter
		std::array<uint8_t, 132> outA{};	// a block of 32 longs the voice sends, and a long one routine reads past it
		std::array<uint8_t, 8> heads{};		// two longs
		std::array<uint8_t, 128> outB{};	// a second block of 32 longs
		std::array<uint8_t, 112> frame{};	// the voice's words of the control frame sent to the analog board
		uint32_t switches = 0;				// tracks 9-11: the voice's ten switches, bit k the k-th
		std::array<uint8_t, 4> noise{};		// the digital noise the voice is sent, where the block makes it: its settings (its
											// samples are the voice model's own)
	};

	// The regions of VoiceIo by number, as the vectors name them
	enum class Region : uint8_t { Params, State, Smooth, OutA, Heads, OutB, Frame, Switch, Noise, Count };
	uint8_t* regionOf(VoiceIo& _io, Region _region, size_t& _size);

	// Every table the port builds at its first use built now: the engine calls it before audio runs
	void warmUp();

	// A trig, as the note handler gives one: the state machine's first two longs cleared
	void trig(VoiceIo& _io);
	// One block of _m on _io: the routine the CPU calls for the voice at every block
	void control(Machine _m, VoiceIo& _io);
	// What follows the four voices' calls in a block: the state's countdown
	void endBlock(VoiceIo& _io);
	// Track 12's voice at a change of its machine to _m: its topology, which a metal machine sets and a utility leaves
	void setup(Machine _m, VoiceIo& _io);

	inline uint16_t be16(const uint8_t* _p) { return static_cast<uint16_t>(_p[0] << 8 | _p[1]); }
	inline uint32_t be32(const uint8_t* _p)
	{
		return static_cast<uint32_t>(_p[0]) << 24 | static_cast<uint32_t>(_p[1]) << 16 | static_cast<uint32_t>(_p[2]) << 8 | _p[3];
	}
	inline void setBe16(uint8_t* _p, const uint32_t _v)
	{
		_p[0] = static_cast<uint8_t>(_v >> 8);
		_p[1] = static_cast<uint8_t>(_v);
	}
	inline void setBe32(uint8_t* _p, const uint32_t _v)
	{
		_p[0] = static_cast<uint8_t>(_v >> 24);
		_p[1] = static_cast<uint8_t>(_v >> 16);
		_p[2] = static_cast<uint8_t>(_v >> 8);
		_p[3] = static_cast<uint8_t>(_v);
	}
}
