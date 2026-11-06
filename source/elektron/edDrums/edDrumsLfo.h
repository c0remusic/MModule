#pragma once

#include <array>
#include <cstdint>

// A Track's two LFOs (spec 4, ticket 13 of .scratch/elektron-drums-v1), which the engine steps at its Track's family's
// blocks: the Syntakt's shapes and modes, and the speeds of both machines read as a division. What is here describes how an
// LFO behaves, in forms of our own and short fits, not an OS's routine, whose words stay out of it. edDrumsLfoTest measures
// it against the ports that run those routines.
//
// SYNC, a division of the host's transport: in FREE and HOLD the phase is the transport's position over the division's
// length, a function of the position alone, so that the same position gives the same phase whatever came before it. A
// mode that restarts counts its cycles from the trig instead, at the tempo. HZ counts its cycles from where it stands.
namespace elektron::drums
{
	enum class LfoShape : uint8_t
	{
		Triangle,		// 0 up to 1, down to -1, back to 0
		Sine,
		Square,			// 1 then -1
		Saw,			// 1 down to -1
		Exponential,	// 1 down to 0, over twelve octaves
		Ramp,			// 0 up to 1 over the first half of the cycle, then 0
		Random			// a new value each sixteenth of the cycle
	};

	enum class LfoMode : uint8_t
	{
		Free,		// nothing at a Hit
		Trig,		// the phase restarts at a Hit of the LFO's Track
		Hold,		// a Hit samples the output, which stays until the next Hit
		One,		// as Trig, and stops at the cycle's end, at 0
		Half		// ... at half the cycle, at 0, a RMP at its top
	};

	enum class LfoFeel : uint8_t
	{
		Straight,
		Dotted,		// half as long again
		Triplet		// two thirds as long
	};

	// What an LFO moves on the Track aimed at: SYN1-8 of its machine, its level and pan, or a value of its chain (DriveA its
	// first drive, BandA and BandB as VelocityTarget has them). A stage's type is no target: changing one fades over 96
	// samples. A target the Track does not hold moves nothing
	enum class LfoTarget : uint8_t
	{
		None,
		Machine1, Machine2, Machine3, Machine4, Machine5, Machine6, Machine7, Machine8,
		Level,
		Pan,
		DriveA, DriveB, BandA, BandB, Frequency, Resonance, Width, EnvelopeDepth,
		Count
	};

	constexpr int LfoDivisions = 11;		// 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1, 2, 4, 8, 16 bars
	constexpr int LfoQuarterNote = 4;		// the division of a quarter note

	// A cycle in quarter notes. A bar is four, whatever the host's time signature
	double lfoQuarters(int _division, LfoFeel _feel);

	struct LfoRate
	{
		bool sync = true;
		int division = LfoQuarterNote;		// 0 to LfoDivisions - 1
		LfoFeel feel = LfoFeel::Straight;
		double hz = 1;						// HZ
	};

	// The division nearest a cycle of _quarters, as a ratio: what lies past 1/64 triplet or 16 bars dotted takes that end
	LfoRate lfoDivisionOf(double _quarters);

	// The Syntakt's SPD (-64 to 63, its sign dropped) and MULT index: 0-11 a power of 2 at the tempo, a division; 12-23 the
	// same at a fixed 120 BPM, which HZ is. SPD 0, which stands still, takes the slowest division
	LfoRate lfoRateOfSyntakt(int _speed, int _multiplier);

	// The Machinedrum's LFOS (0-127), which runs at the tempo
	LfoRate lfoRateOfMachinedrum(int _speed);

	struct LfoSettings
	{
		LfoRate rate;
		LfoShape shape = LfoShape::Triangle;
		LfoMode mode = LfoMode::Free;
		int depth = 0;			// -127 to 127: 127 moves its target by its whole range each way; 0 leaves it alone
		int track = -1;			// the Track aimed at, -1 its own
		LfoTarget target = LfoTarget::None;
	};

	class Lfo
	{
	public:
		// From here on, from where it stands; _seed tells LFOs' Random apart
		void set(const LfoSettings& _settings, uint64_t _seed);

		// A Hit of its Track at transport position _position, in quarter notes
		void trig(double _position);

		// A block: _position the transport's at its start, _quarters and _seconds its length
		void advance(double _position, double _quarters, double _seconds);

		double value() const;		// the output, -1 to 1
		double phase() const;		// 0 to 1

	private:
		bool onTransport() const;
		double end() const;			// where a cycle stops, 0 where it does not

		LfoRate m_rate;
		LfoShape m_shape = LfoShape::Triangle;
		LfoMode m_mode = LfoMode::Free;
		double m_length = 1;		// a cycle in quarter notes
		uint64_t m_seed = 0;

		double m_cycles = 0;		// from position 0 on the transport, else from the restart
		uint64_t m_restarts = 0;	// so that Random draws other values after each
		double m_held = 0;
		bool m_holding = false;
		bool m_atStop = false;		// a stopped ONE or HALF: its output needs no shape any more
	};

	// The LFOs of 16 Tracks, two each, and what they add up to on each target. A target's sum waits, by its bit, until its Track
	// says it took it. Off, an LFO is not stepped
	class LfoBank
	{
	public:
		static constexpr int Tracks = 16, Targets = static_cast<int>(LfoTarget::Count);

		// LFO _index (0 or 1) of _track from here on. A target it leaves, or depth 0, gets its share back at once
		void set(int _track, int _index, const LfoSettings& _settings);

		// A Hit of _track at _position
		void trig(int _track, double _position);

		// A block of the Tracks of mask _tracks, the LFOs of those Tracks
		void step(uint32_t _tracks, double _position, double _quarters, double _seconds);

		int offset(const int _track, const int _target) const
		{
			return m_sum[static_cast<size_t>(_track)][static_cast<size_t>(_target)];
		}
		uint32_t waiting() const { return m_waiting; }			// the Tracks with a target that waits
		uint32_t waiting(const int _track) const { return m_targets[static_cast<size_t>(_track)]; }		// its targets that wait
		void wait(int _track, int _target);
		void taken(int _track, int _target);

		double phase(int _track, int _index) const;

	private:
		struct Run
		{
			Lfo lfo;
			int track = -1;
			LfoTarget target = LfoTarget::None;
			int depth = 0;
			int offset = 0;			// what its target's sum holds of it, in 1/128 of a value
		};

		bool running(const int _lfo) const { return m_on >> _lfo & 1; }
		void move(Run& _run);
		void leave(Run& _run);

		std::array<Run, 2 * Tracks> m_runs;
		uint32_t m_on = 0;			// a bit each
		std::array<std::array<int, Targets>, Tracks> m_sum{};
		std::array<uint32_t, Tracks> m_targets{};
		uint32_t m_waiting = 0;
	};
}
