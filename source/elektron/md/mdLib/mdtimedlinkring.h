#pragma once

#include <array>
#include <cstdint>

#include "dsp56kEmu/audio.h"
#include "dsp56kBase/ringbuffer.h"

namespace md
{
	// Dated inter-DSP link word (parallel-transport spec, link transport).
	// The MD/MM ESSI0 link carries one meaningful word per slot, so the entry
	// stores the words compactly instead of a full 32-slot RxFrame; the pop
	// site rebuilds the RxFrame exactly as the txToRx conversion used to.
	// stamp/epoch/fresh are captured in the producer's context at TX so the
	// dating pop of the next migration step can evaluate its gates without
	// re-reading peer state. Until then they ride along inert.
	struct TimedLinkEntry
	{
		static constexpr uint32_t MaxSlots = 8;

		std::array<dsp56k::TWord, MaxSlots> words{};
		uint64_t producerCycles = 0;	// producer DSP getCycles() at TX
		uint64_t epoch = 0;				// flush (MD) / strobe (MM) epoch at TX
		uint8_t slotCount = 0;
		bool fresh = false;				// producer TX wrote this slot

		void fromTx(const dsp56k::Audio::TxFrame& _tx)
		{
			const auto count = std::min<uint32_t>(_tx.size(), MaxSlots);
			slotCount = static_cast<uint8_t>(count);
			for(uint32_t i = 0; i < count; ++i)
				words[i] = _tx[i][0];	// the link carries one word per slot
		}

		void toRx(dsp56k::Audio::RxFrame& _rx) const
		{
			_rx.resize(slotCount);
			for(uint32_t i = 0; i < slotCount; ++i)
				_rx[i] = dsp56k::Audio::RxSlot{words[i]};
		}
	};

	// Same depth as the ESSI ring it replaces. Non-blocking (Lock=false):
	// every push is guarded by full() (drop-on-full) and every pop by
	// empty(), exactly like the serial scheduler used the ESSI ring. SPSC
	// (one producing DSP context, one consuming DSP context) from the
	// threaded migration steps on.
	using TimedLinkRing = dsp56k::RingBuffer<TimedLinkEntry, 32768, false, false>;
}
