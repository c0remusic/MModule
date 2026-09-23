#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

#include "mdtimedhostrx.h"

#include "dsp56kBase/ringbuffer.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kEmu/dspBootCode.h"

namespace mc68k
{
	class Hdi08;
}

namespace md
{
	class Hardware;

	// -------------------------------------------------------------------------
	// md::Dsp - one real dsp56kEmu DSP56303 advanced by Hardware's deterministic
	// interleave scheduler.
	//
	// The Machinedrum has two DSP56303s wired to the ColdFire over an 8-byte HI08
	// host-port window each (DSP1 = mixer/codec @ 0x500000, DSP2 = voice producer
	// @ 0x600000) and to each other over ESSI. This class owns the DSP-side object
	// graph (Memory + Peripherals56303 + DSP + DspBoot) and
	// bridges it to the ColdFire-facing mc68k::Hdi08 register file (owned by the
	// Microcontroller and passed in by reference).
	//
	// The DSP boots synchronously as the ColdFire streams firmware through DspBoot.
	// Once DspBoot signals completion, onDspBootFinished publishes it to the scheduler.
	// -------------------------------------------------------------------------
	class Dsp
	{
	public:
		// A generated block retains one JitBlockRuntimeData object for the lifetime
		// of its executable code. Populate the free pool before emulation
		// so lazy compilation cannot allocate metadata on the emulation/audio owner.
		static constexpr size_t RealtimeJitBlockRuntimeDataReserve = 4096;

		// _index: 0 = DSP1 (mixer/codec, 0x500000), 1 = DSP2 (voice producer, 0x600000).
		Dsp(Hardware& _hw, mc68k::Hdi08& _hdiUc, uint32_t _index);

		dsp56k::HDI08&            hdi08()     { return m_periphX.getHI08(); }	// DSP-side host port

		// Words produced for the UC but not yet consumed by it (DSP-side HOTX ring + UC-facing rx
		// queue). The MM scheduler's flow control (schedStep backpressure) parks a DSP whose
		// backlog is deep - see mdhardware.cpp.
		size_t hostTxBacklog();
		dsp56k::DSP&              dsp()       { return m_dsp; }
		dsp56k::Peripherals56303& getPeriph() { return m_periphX; }

		bool     booted() const { return m_schedRunnable.load(std::memory_order_acquire); }
		void onDspBootFinished();

		// Continuously drain this DSP's HOTX into the UC-facing HI08 receive queue, bounded so the
		// queue never exceeds _maxUcWords. Returns the number of words moved. Safe to call from the
		// scheduler (same context as the other UC-side callers of hdiTransferDSPtoUC). Used by
		// md::Hardware to keep DSP2's receive queue current, which raises the
		// HI08 HREQ line that drives the ColdFire external IRQ4 (see mdhardware.cpp).
		uint32_t pumpHostRx(size_t _maxUcWords);
		void setHostPumpWakeCallback(const std::function<void()>& _callback);
		// Words produced for the UC that are staged but not yet visible: the
		// MM's one-latch TimedHostRx or the MD's dated staging queue.
		bool hasDeferredHostRx() const
		{
			return m_timedHostRx.pending() || !m_hostTxStaging.empty();
		}
		// Host cycle at which the oldest deferred word becomes visible; the UC
		// idle skip must not jump past it (parallel-transport spec §5.7).
		uint64_t nextDeferredHostRxCycle() const;
		// DSP context only: move the HOTX latch into the dated staging queue
		// while there is room. A full queue leaves the latch occupied, so HTDE
		// stays clear and the firmware paces itself, as on silicon.
		void stageHostTx();
		// DSP context only (worker): apply the dated UC->DSP stream up to the
		// DSP's current cycle - data words into HRX when it is free, host
		// commands when none is in flight (parallel-transport spec §5.4).
		void applyHostToDspStream();
		// Diagnostics only (racy cross-thread reads): the state a stalled
		// host-to-DSP stream depends on.
		void traceHostStream(const char* _tag) const;
		// Host words or commands waiting to be applied by the worker.
		bool hasPendingHostToDsp() const { return !m_hostToDsp.empty(); }
		// The DSP cycle up to which the oldest pending host item entitles the
		// DSP to run (its deadline plus the serial inline clamp), or 0 when
		// nothing is pending. Mirrors writeWordToDsp's drain run.
		uint64_t hostToDspHeadAllowance() const;

	private:
		void    onUCRxEmpty(bool _needMoreData);
		void    hdiTransferUCtoDSP(uint32_t _word);
		void    waitForHostCommandIdle();		// wait in emulated time until no host command is in flight
		void    writeWordToDsp(uint32_t _word);	// 1-deep UC->DSP transport: wait for HORX empty, then push
		void    hdiSendIrqToDSP(uint8_t _irq);
		void    dispatchHostCommandInterrupt(uint8_t _vba);
		uint8_t hdiUcReadIsr(uint8_t _isr);
		bool    hdiTransferDSPtoUC();
		void    publishUcRxDepth();			// UC context: mirror m_hdiUC's receive depth for the worker
		// UC context: pop the oldest staged MD word once the host clock has
		// reached its ready cycle.
		bool    takeDueHostRx(uint64_t _now, uint32_t& _word);

		// MD DSP->UC dated staging (parallel-transport spec §5.2): the DSP
		// context stages {word, readyCycle = hostRxReadyCycle(HOTX write
		// cycle)}, the UC context takes due words. Depth = the UC-facing
		// receive capacity (hostReceiveQueueCapacityWords).
		struct StagedHostWord
		{
			uint32_t word = 0;
			uint64_t readyCycle = 0;
		};
		dsp56k::RingBuffer<StagedHostWord, 16, false, true> m_hostTxStaging;
		uint64_t m_lastHostTxCycle = 0;		// DSP context: cycle of the latest HOTX write

		// Dated UC->DSP stream (threaded adapter): the UC context pushes
		// words and host commands stamped with its cycle, the worker applies
		// them in order once its own time has reached the stamp.
		struct HostToDspItem
		{
			enum class Kind : uint8_t { Data, Command };
			Kind kind = Kind::Data;
			uint32_t value = 0;
			uint64_t ucCycle = 0;
		};
		void pushHostToDsp(HostToDspItem::Kind _kind, uint32_t _value);
		// As deep as the DSP-side HDI08 receive ring that absorbed host bursts
		// under the serial adapter, so the UC only waits where the serial
		// path would have stalled too. The UC never drops a host word.
		dsp56k::RingBuffer<HostToDspItem, 8192, false, false> m_hostToDsp;

		Hardware&        m_hardware;
		mc68k::Hdi08&    m_hdiUC;			// ColdFire-facing HI08 register file (owned by the Microcontroller)

		const uint32_t    m_index;

		dsp56k::DefaultMemoryValidator m_validator;
		std::vector<dsp56k::TWord>     m_buffer;
		dsp56k::PeripheralsNop         m_periphNop;
		dsp56k::Peripherals56303       m_periphX;
		dsp56k::Memory                 m_memory;
		dsp56k::DSP                    m_dsp;
		dsp56k::DspBoot                m_boot;

		// Published once boot state is fully initialized; acquired by the scheduler.
		std::atomic<bool> m_schedRunnable{false};

		uint64_t m_mmHostTxCycle = 0;
		TimedHostRx m_timedHostRx;

	};
}
