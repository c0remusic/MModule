// A Track of the Machinedrum as the OS's tick drives its voice, without the OS: what mdEngine's HostModel does around the
// OS's own code, here around the port (mdVoiceCoefficients). mdVoiceCoefficientsTest plays this code against HostModel
// on the OS's fixed ticks. Ticket 11 of .scratch/elektron-drums-v1.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "mdVoice.h"
#include "mdVoiceCoefficients.h"

namespace md::voice
{
	// MID and CTR machines ($60-$7B) give the voice DSP nothing to play
	constexpr bool isAudioMachine(const uint8_t _id) { return !(_id >= 0x60 && _id <= 0x7b); }

	// What a Track can play: a ported machine, or a MID or CTR one, whose Hit stops the voice
	inline bool playable(const uint8_t _id) { return coefficientsPorted(_id) || !isAudioMachine(_id); }

	class Track
	{
	public:
		static constexpr size_t kParams = Smoother::kParams;
		using Defaults = std::array<uint8_t, 8>;

		// As on the machine, the voice switches machine at its next Hit, where all 24 parameters take effect at once;
		// SYN1-8 take the machine's _defaults from here on
		void setMachine(uint8_t _id, const Defaults& _defaults);

		// A knob turned: the Kit's value, and the target over a lock too
		void knob(size_t _param, uint8_t _value);

		// A lock for the Hit in the block about to render, which holds until the Track's next Hit; without a Hit in that
		// block it is dropped
		void lock(size_t _param, uint8_t _value);

		// The locks of SYN1-8 gone, of the last Hit and of the block's: what a machine change takes away
		void clearMachineLocks();

		void hit();
		bool triggered() const { return m_trigger; }

		// The block's locks: with a Hit, its locks and the Kit's value back on what the last Hit locked, before any voice's
		// update in the block; without one, dropped
		void takeLocks();

		// The Track's slot as the OS's tick fills it. A machine the voice cannot play has only its Hit: the voice takes
		// GND--'s routines, as the DSP's dispatch gives those types, so that what it played stops
		void update(const OsTables& _tables, uint32_t _tempo, Voice& _voice);

		void tick() { m_smoother.tick(); }

		// This Track's ticks from block _block on. The OS runs every Hit at a tick, for all Tracks; mdEngine, and so the
		// oracle, keeps the ticks fixed and updates a Hit's voice between them. A Hit that calls this brings its Track's
		// ticks along, so that what follows it does not depend on where its note fell
		void alignTicks(const int64_t _block) { m_tickPhase = static_cast<int>(_block % kBlocksPerTick); }
		bool ticks(const int64_t _block) const { return (_block - m_tickPhase) % kBlocksPerTick == 0; }

		const Smoother::Values& values() const { return m_smoother.values(); }
		uint8_t machine() const { return m_machine; }

	private:
		uint8_t target(size_t _param) const;

		Smoother m_smoother;
		std::array<uint8_t, kParams> m_kit{};	// the Kit's values, which a knob sets
		uint32_t m_locked = 0;					// bit n: parameter n plays m_lockValue, the last Hit's
		uint32_t m_lockPending = 0;				// the same for the block's Hit
		std::array<uint8_t, kParams> m_lockValue{}, m_lockPendingValue{};
		uint8_t m_machine = 0;
		int m_pending = 0;	// the machine the next Hit brings, -1: none. HostModel assigns machine 0 at construction
		bool m_trigger = false;
		int m_tickPhase = 0;
	};

	// The parameters and slots of a block, in the order of HostModel::renderBlock: the Hits' locks, then the voices'
	// updates, an OS tick every kBlocksPerTick blocks of each Track. Bit t of _trackMask: Track t plays this family; of
	// _live: its voice sounds, so that its slot follows its parameters. A voice that does not gets its words at its Hit
	void prepareBlock(std::array<Track, 16>& _tracks, std::vector<Voice>& _voices, const OsTables& _tables, uint32_t _tempo,
		int64_t _block, uint32_t _trackMask, uint32_t _live);
}
