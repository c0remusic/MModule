#pragma once

#include <array>
#include <cstdint>

// The control of the Rytm MKII's analog voices that the Syntakt has none of (ticket 28, stage 2): the clap, the big tom
// and the toms, ported from what the Rytm's main CPU computes for them every block of 32 samples at 48 kHz: the
// machine's parameters into control values, the envelopes' state, the short digital signals it sends the voice. The
// Rytm's other analog machines are the Syntakt's (edDrumsAnalog.h), whose control they share. Nothing of the circuits is
// here: a voice model of our own (edDrumsRytmVoice.h) reads what this computes.
//
// As edDrumsAnalog.h, the port works on the voice's own memory, byte for byte, so that a vector recorded from the original
// routine (edDrumsRytmControlTest, vectors kept outside the repository) compares every byte the routine wrote. The layout
// is the port's own. The unit's calibration is an input (trim): the voice model gives the nominal one.
namespace elektron::drums::rytm
{
	enum class Machine : uint8_t
	{
		Clap, BigTom, Tom,
		Count
	};
	constexpr int Machines = static_cast<int>(Machine::Count);

	// The voice each machine plays on, as the Rytm numbers its voices (the tom has a second one, 5)
	constexpr int ClapVoice = 7;
	constexpr int BigTomVoice = 1;
	constexpr int TomVoice = 3;

	// One voice's memory. Multi-byte values are big-endian, as the CPU holds them
	struct VoiceIo
	{
		int voice = 0;
		bool saturate = false;			// the multiplier saturates on overflow, as where a routine before set it
		int32_t pitch = 0;				// the note after glide and bend, semitones in Q16

		std::array<uint8_t, 20> params{};	// the machine's 10 words: SYN1-8 and two more, value << 8 | fine
		std::array<uint8_t, 32> state{};	// the envelopes' state
		std::array<uint8_t, 132> outA{};	// the clap's block of 32 longs: its noise in, scaled by its envelope out (and the
											// long past it, which the scaling loop reads); the big tom clears it
		std::array<uint8_t, 8> heads{};		// two longs: a level and its target
		std::array<uint8_t, 128> stream{};	// the toms' 32 longs of the stream to the voice: their click's envelope
		std::array<uint8_t, 8> frame{};		// the voice's words of the control frame sent to the analog board
		std::array<uint8_t, 4> noise{};		// the digital noise the voice is sent: its settings (its samples are the model's)
		std::array<uint8_t, 16> trim{};		// the voice's calibration: two pairs (scale, offset)
		std::array<uint8_t, 8> random{};	// the clap's random state
	};

	enum class Region : uint8_t
	{
		Params = 0, State = 1, OutA = 3, Heads = 4, Stream = 5, Frame = 6, Noise = 8, Trim = 9, Random = 10, Count = 11
	};
	// nullptr for a number no region has
	uint8_t* regionOf(VoiceIo& _io, Region _region, size_t& _size);

	// Every table the port builds at its first use built now: the engine calls it before audio runs
	void warmUp();
	int& tablesBuilt();

	// A trig, as the note handler gives one: the state's first two longs cleared
	void trig(VoiceIo& _io);
	// One block of _m on _io: the routine the CPU calls for the voice at every block
	void control(Machine _m, VoiceIo& _io);
	// What follows the voices' calls in a block: the state's countdown
	void endBlock(VoiceIo& _io);

	// The decay coefficients the routines read, by formula: a fall of 40 dB over n blocks, n = _scale (_base^(k/127) - 1)
	int32_t decayCoefficient(double _scale, double _base, int _k);
}
