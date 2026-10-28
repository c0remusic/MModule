// Harness structure adapted from shnolk/monomodule src/core/dsp/DspEngine.cpp (AGPL-3.0).
#include "VoiceEngine.h"

#include "MdsPackage.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/audio.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"

namespace md::engine
{
	using namespace dsp56k;

	namespace
	{
		// Memory as gearmulator-md-mm models the Machinedrum: external SRAM from $020000 is shared by P, X and Y.
		// P is 2M words, not the DSP's 8M: the MD's map stays below $200000 (samples end at $18fc12), and the interpreter's
		// per-word opcode cache made the full size cost ~300 MB of RAM in MPC. Output is identical.
		// md-drums: X and Y 2M too, as MasterEngine's. Bridged, P is as large as the larger of the two: 8M of X and
		// Y gave P 8M words all the same, which warmUpVoices' saveState then copied whole
		constexpr TWord kSizeP = 0x200000, kSizeXY = 0x200000, kBridge = 0x020000;

		// DSP2 state after the ColdFire's first-stage loader, read from a running gearmulator-md-mm (OS 1.63):
		// the full SPS-1 UW memory map (AAR0-3) and OMR.
		constexpr TWord kAar[4] = {0x100539, 0x140639, 0x180539, 0x1c0639};
		constexpr TWord kOmr = 0x00498d;

		// Program addresses (OS 1.63 voice program, see docs/PROTOCOL.md)
		constexpr TWord kEntry = 0x24;			// after the vector table: init, then bra $64
		constexpr TWord kHostGroupTx = 0x73;	// movep a1,x:HTX every 4 voices (ColdFire pacing): removed
		constexpr TWord kRenderFn = 0xa8;		// r1 = y:(r0+$145c77): render fn address for the voice's machine
		constexpr TWord kAfterRenderFn = 0xaa;	// resumes normal flow after the render-fn lookup
		constexpr TWord kAfterRender = 0xb5;	// voice rendered into Y:(y:$140); original: frame sync + link DMA
		constexpr TWord kNextVoice = 0xd5;		// advance to the next voice
		constexpr TWord kEndOfBlock = 0xe2;		// all 16 voices done; original: RAM-R input position, bra $64
		constexpr TWord kBlockStart = 0x64;
		constexpr TWord kStub = 0x0c00;			// free internal P RAM (the program's internal P ends at $3e2)
		constexpr TWord kSkipStub = 0x0c20;	// silent-voice check, replaces kRenderFn's lookup
		constexpr TWord kSkipNormal = 0x0c30;	// real render fn lookup, resumed
		constexpr TWord kSkipSilent = 0x0c38;	// idle voice: flag 0, straight to the next voice
		// md-drums: a buffer per Track where 1.63 flips two banks, and the descriptor of the MDS Standard's NEIGHBOUR
		// machines at Y:$7F0, a Track's own naming the Tracks it hears (.scratch/user-machines/research/12-neighbour-
		// buffers.md, 14-sources-d-une-nfx.md)
		constexpr TWord kBankFlip = 0x9b;		// 1.63's flip of Y:$140 between $100 and $120, up to the type fetch's end
		constexpr TWord kSourcesStub = 0x0c40;
		constexpr TWord kSourcesCopy = 0x0c80;	// a User Machine's four back, copied into its own buffer
		// Y:$1000 + 32·t, Track t's buffer, in the emulator's Y past what a Machinedrum has (nothing reads or writes it);
		// Y:$7A0 + 4·t, Track t's descriptor (its own buffer, then its first three sources'); Y:$7E0 + t, its fourth
		// source's buffer; Y:$7F0, the pointer to the descriptor of the Track rendering
		constexpr TWord kTrackBuffers = 0x1000, kDescriptors = 0x7a0, kFourBack = 0x7e0, kSourcesPointer = 0x7f0;
		constexpr TWord kBufferWords = 32;
		// md-drums: setQuietSkip's flags, Y:$1200 + 4·t, past the buffers, 1 for a voice that does not render; the stub
		// reads Track t's from its descriptor, $A60 below it
		constexpr TWord kQuietStub = 0x0ca0;
		constexpr TWord kStopFlags = 0x1200, kStopFromDescriptor = kStopFlags - kDescriptors;
		constexpr TWord kActiveTypes = 0x153;	// Y:$153 + t, the type a voice plays (machine id + 1)

		constexpr uint64_t kMaxInstrInit = 200'000'000;
		constexpr uint64_t kMaxInstrBlock = 20'000'000;
		// Cycles a run of runUntilTx may go before it checks its budget; the HOTX write it waits for ends it sooner
		constexpr uint64_t kRunCycles = 1'000'000;

		// md-drums: the dispatch tables (Y, in the memory P, X and Y share), one word per type (machine id + 1): INIT, the
		// update (MDS's TRIG) and RENDER, which the program reads through the bases it holds at P:$8A and P:$95 and the
		// harness through kSkipNormal's. OS 1.63's have 193 types; X.20 copies them to 211, past the end of 1.63's
		// (.scratch/user-machines/research/04-voice-dsp-memory.md), and so does this.
		constexpr TWord kDispatchInit = 0x145af5, kDispatchTrig = 0x145bb6, kDispatchRender = 0x145c77;
		constexpr TWord kDispatchTypes = 193;
		constexpr TWord kExtInit = 0x145d38, kExtTrig = 0x145e0b, kExtRender = 0x145ede;
		constexpr TWord kExtTypes = 211;
		constexpr TWord kInitBase = 0x8a, kTrigBase = 0x95;
		constexpr TWord kEmptyType = 1;	// GND--, machine id 0: silent
		// User Machines: a number's code in the memory 1.63 never writes, $020000-$0FFFFF, split in 16
		constexpr TWord kUserCode = 0x020000, kUserCodeWords = 0xe000;
		// Their imports on 1.63's layout: the sine table its init builds, P-I's external memory (track 0's region,
		// 1,536 words a Track), and the stock renderers' scratch at X:0 and Y:0
		constexpr TWord kImportSine = 0x148000, kImportExternal = 0x135600, kImportScratchX = 0, kImportScratchY = 0;
		constexpr int kMaxDeclaredCycles = 3951;
	}

	VoiceEngine::VoiceEngine(const fw::Firmware& _fw) : m_fw(_fw)
	{
		m_validator = std::make_unique<DefaultMemoryValidator>();
		m_mem = std::make_unique<Memory>(*m_validator, kSizeP, kSizeXY, kBridge);
		// md-drums: no ESSI audio rings (84 MB a DSP): the callbacks below stand in for the link and the ADC
		m_periphX = std::make_unique<Peripherals56303>(false);
		m_periphY = std::make_unique<PeripheralsNop>();
		m_dsp = std::make_unique<DSP>(*m_mem, m_periphX.get(), m_periphY.get());
		if constexpr(g_useJIT)
		{
			// As gearmulator-md-mm configures its DSPs: the program keeps code in the vector area.
			auto cfg = m_dsp->getJit().getConfig();
			cfg.dynamicFastInterrupts = true;
			cfg.aguSupportBitreverse = true;
			cfg.linkJitBlocks = false;
			m_dsp->getJit().setConfig(cfg);
		}
		else
		{
			// No JIT on this target (32-bit ARM, the Force): exec() falls back to the interpreter, whose
			// per-address opcode cache this fork only builds on request (it costs ~48 bytes x P size, moot
			// when the JIT is the normal path). Without this, the cache is empty and exec() dereferences a
			// null instruction-handler pointer on the very first instruction.
#if 0	// md-drums: our dsp56300 has no such switch, and runs the JIT on every target we build
			m_dsp->setInterpreterEnabled(true);
#endif
		}
		m_periphX->getHI08().setRXRateLimit(0);
		m_periphX->getHI08().setTransmitDataAlwaysEmpty(true);
		// runUntilTx runs the DSP under one JIT entry: the HOTX write that brings the words it waits for ends the run at
		// the end of its block, where stepping block by block stopped
		m_periphX->getHI08().setWriteTxCallback([this]
		{
			if(m_txWanted && m_periphX->getHI08().txData().size() >= m_txWanted)
				m_dsp->requestExecExit();
		});

		// The program's init starts both ESSI ports (ESSI0 = link to the mixer DSP, ESSI1 = codec ADC). The
		// harness replaces the link and does not use the ADC: feed silence and discard output, never block.
		for(auto* essi : {&m_periphX->getEssi0(), &m_periphX->getEssi1()})
		{
			essi->setReadRxCallback([](uint64_t& _frameIndex, Audio::RxFrame& _frame)
			{
				_frame.resize(2);
				_frame[0] = Audio::RxSlot{};
				_frame[1] = Audio::RxSlot{};
				++_frameIndex;
			});
			essi->setWriteTxCallback([](uint64_t& _frameIndex, const Audio::TxFrame&) { ++_frameIndex; });
		}
		reset();
	}

	VoiceEngine::~VoiceEngine() = default;

	void VoiceEngine::loadImage(const fw::DspImage& _img)
	{
		for(const auto& r : _img.records)
		{
			for(size_t k = 0; k < r.words.size(); ++k)
			{
				const TWord a = r.addr + static_cast<TWord>(k);
				// P space, and anything in the shared external range, must go through memWriteP so the JIT sees it.
				if(r.space == fw::Space::P || a >= kBridge)
					m_dsp->memWriteP(a, r.words[k]);
				else
					m_dsp->memWrite(r.space == fw::Space::X ? MemArea_X : MemArea_Y, a, r.words[k]);
			}
		}
	}

	void VoiceEngine::installHarness()
	{
		Assembler as;
		auto emit = [&](TWord& _pc, const std::string& _text)
		{
			const auto r = as.assemble(_text.c_str());
			if(!r.success())
				throw std::runtime_error("voice harness: cannot assemble '" + _text + "'");
			m_dsp->memWriteP(_pc++, r.word[0]);
			if(r.wordCount > 1)
				m_dsp->memWriteP(_pc++, r.word[1]);
		};
		auto hex = [](TWord _v) { std::ostringstream o; o << "$" << std::hex << _v; return o.str(); };

		// 0) the silent-voice render ($10008f: 32 zeros into Y:(r7)) pads its time with a 32 x 50 nop loop so
		//    the hardware's block timing is even; the output is the same without it
		m_dsp->memWriteP(0x100093, 0x000000);
		m_dsp->memWriteP(0x100094, 0x000000);

		// 1) no per-group handshake word to the host
		m_dsp->memWriteP(kHostGroupTx, 0x000000);

		// 2) after each voice's render: send its 32 samples (Y:(y:$140)) to the host, then continue
		{
			TWord pc = kAfterRender;
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >stub
			m_dsp->memWriteP(pc++, kStub);
		}
		{
			TWord pc = kStub;
			emit(pc, "move y:>$140,r0");
			emit(pc, "move #>$ffffff,m0");
			emit(pc, "do #32," + hex(pc + 3));	// DO, not REP: REP over a peripheral access is unsafe in the JIT
			emit(pc, "movep y:(r0)+,x:<<$ffffc7");
			emit(pc, "nop");
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >next voice
			m_dsp->memWriteP(pc++, kNextVoice);
		}

		// 2b) skip the machine call for idle voices (current machine code 0 = never assigned, or 1 = the
		//     empty machine GND--, both of which render 32 zeros, docs/PROTOCOL.md "per voice, every
		//     tick"): redirect r1 (the render fn about to be jsr'd at $b4) to a fast clear instead of the
		//     real GND-- function. r0 (current machine code) and r7 (buffer pointer, mod 32) are live here.
		{
			TWord pc = kRenderFn;
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >kQuietStub
			m_dsp->memWriteP(pc++, kQuietStub);
		}
		{
			// md-drums: a voice setQuietSkip stopped goes the idle voice's way, whatever its type. R1 is free until the
			// render fn lookup, B until the skip stub's compare
			TWord pc = kQuietStub;
			emit(pc, "move y:>" + hex(kSourcesPointer) + ",r1");
			emit(pc, "nop");
			emit(pc, "move y:(r1+" + hex(kStopFromDescriptor) + "),b");
			emit(pc, "tst b");
			emit(pc, "jne " + hex(kSkipSilent));
			emit(pc, "jmp " + hex(kSkipStub));
		}
		{
			// jeq/jmp, not beq/bra: Bcc_xxxx's operand is a raw PC-relative displacement, not an address
			// (unlike jmp/jclr/jset elsewhere in this file); jeq is the short-absolute conditional form.
			TWord pc = kSkipStub;
			emit(pc, "move r0,a");
			emit(pc, "tst a");
			emit(pc, "jeq " + hex(kSkipSilent));
			emit(pc, "move #>1,b");	// (cmp a,b here never matched code 1: an empty-machine voice kept rendering)
			emit(pc, "sub b,a");
			emit(pc, "tst a");
			emit(pc, "jeq " + hex(kSkipSilent));
			emit(pc, "add b,a");	// a back to the machine code, as the compare left it
			emit(pc, "jmp " + hex(kSkipNormal));
		}
		{
			TWord pc = kSkipNormal;
			emit(pc, "movep #>1,x:<<$ffffc7");	// this voice will render: flag 1, its 32 samples follow
			emit(pc, "move y:(r0+" + hex(kExtRender) + "),r1");	// md-drums: the RENDER table, extended (extendDispatch)
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >kAfterRenderFn
			m_dsp->memWriteP(pc++, kAfterRenderFn);
		}
		{
			// An idle voice sends flag 0 and nothing else - no 32-word transmit; the host fills its zeros. A voice's 32
			// samples are only sent when its flag is 1. md-drums: its buffer takes 32 zeros all the same, as 1.63's null
			// render writes them: a NEIGHBOUR machine reading it would hear the Track's own last block (research/14 §7.1).
			// M7 is linear here; the next voice sets R7 again.
			TWord pc = kSkipSilent;
			emit(pc, "clr a");
			emit(pc, "rep #32");
			emit(pc, "move a,y:(r7)+");
			emit(pc, "movep #>0,x:<<$ffffc7");
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >kNextVoice
			m_dsp->memWriteP(pc++, kNextVoice);
				if(pc > kSourcesStub)
				throw std::runtime_error("voice harness: the silent voice's stub runs into the sources'");
		}

		// 2c) md-drums: a buffer per Track in place of 1.63's bank flip: Track t (R1, 0-15) renders into its own buffer
		//     and Y:$7F0 points at its descriptor (writeSources), whose words 1 to 3 name the buffers of the Tracks it
		//     hears, by default the three before it, as the MDS Standard's NEIGHBOUR machines read them. A User
		//     Machine's own buffer first takes its fourth source's block, which the Standard calls four back and NFX-MX
		//     and NFX-EN read before writing it (research/14 §2.3). Stateless, from the Track number, so it cannot
		//     drift. The type comes into R0 as 1.63's $A3 takes it, and X0 and X1 hold what its flip left there.
		{
			TWord pc = kBankFlip;
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >kSourcesStub
			m_dsp->memWriteP(pc++, kSourcesStub);
			while(pc < kRenderFn)
				m_dsp->memWriteP(pc++, 0x000000);	// nop, never run
		}
		{
			TWord pc = kSourcesStub;
			emit(pc, "move y:(r1+$153),r0");
			emit(pc, "move r1,a");
			emit(pc, "asl a");
			emit(pc, "asl a");
			emit(pc, "add #>" + hex(kDescriptors) + ",a");
			emit(pc, "move a,y:>" + hex(kSourcesPointer));
			emit(pc, "move y:(r1+" + hex(kFourBack) + "),b");	// the fourth source's buffer, while R1 holds the Track
			emit(pc, "move a,r1");
			emit(pc, "nop");
			emit(pc, "move y:(r1),r7");
			emit(pc, "move r7,y:>$140");
			emit(pc, "move r0,a");
			emit(pc, "move #>" + hex(kFirstUserType) + ",x0");
			emit(pc, "cmp x0,a");
			emit(pc, "jge " + hex(kSourcesCopy));
			const TWord rejoin = pc;
			emit(pc, "move #>$100,x1");
			emit(pc, "move #>$20,x0");
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >kRenderFn, the skip stub's jump
			m_dsp->memWriteP(pc++, kRenderFn);
			if(pc > kSourcesCopy)
				throw std::runtime_error("voice harness: the sources' stub runs into its copy");
			pc = kSourcesCopy;
			emit(pc, "move b,r1");
			emit(pc, "nop");
			const TWord loop = pc;
			emit(pc, "do #32," + hex(loop + 4));
			emit(pc, "move y:(r1)+,a");
			emit(pc, "move a,y:(r7)+");
			emit(pc, "move y:>$140,r7");	// M7 is linear here: back to the buffer's start
			emit(pc, "jmp " + hex(rejoin));
			if(pc > kQuietStub)
				throw std::runtime_error("voice harness: the sources' copy runs into the quiet stub");
		}
		writeSources();

		// 3) after all 16 voices: wait for the host's "go" word (the host updates the voice slots meanwhile)
		{
			TWord pc = kEndOfBlock;
			emit(pc, "jclr #0,x:<<$ffffc3," + hex(kEndOfBlock));
			emit(pc, "movep x:<<$ffffc6,a");
			emit(pc, "jmp " + hex(kBlockStart));
		}
	}

	void VoiceEngine::writeSources()
	{
		// The sixteen buffers, zeros, then each Track's descriptor and fourth source
		for(TWord a = 0; a < kTracks * kBufferWords; ++a)
			m_dsp->memWrite(MemArea_Y, kTrackBuffers + a, 0);
		for(int t = 0; t < kTracks; ++t)
		{
			writeSources(t);
			m_dsp->memWrite(MemArea_Y, kStopFlags + 4 * static_cast<TWord>(t), 0);
		}
		m_dsp->memWrite(MemArea_Y, kSourcesPointer, kDescriptors);
	}

	void VoiceEngine::writeSources(const int _track)
	{
		const auto buffer = [](const int _t) { return kTrackBuffers + kBufferWords * static_cast<TWord>(_t); };
		const auto& sources = m_sources[static_cast<size_t>(_track)];
		const auto descriptor = kDescriptors + 4 * static_cast<TWord>(_track);
		m_dsp->memWrite(MemArea_Y, descriptor, buffer(_track));
		for(TWord k = 0; k < 3; ++k)
			m_dsp->memWrite(MemArea_Y, descriptor + 1 + k, buffer(sources[k]));
		m_dsp->memWrite(MemArea_Y, kFourBack + static_cast<TWord>(_track), buffer(sources[3]));
	}

	VoiceEngine::Sources VoiceEngine::defaultSources(const int _track)
	{
		Sources sources{};
		for(int k = 0; k < 4; ++k)
			sources[static_cast<size_t>(k)] = static_cast<uint8_t>((_track + kTracks - (k + 1)) % kTracks);
		return sources;
	}

	void VoiceEngine::setSources(const int _track, const Sources& _sources)
	{
		if(_track < 0 || _track >= kTracks)
			return;
		auto& sources = m_sources[static_cast<size_t>(_track)];
		for(size_t k = 0; k < sources.size(); ++k)
			sources[k] = _sources[k] < kTracks && _sources[k] != _track ? _sources[k] : defaultSources(_track)[k];
		writeSources(_track);
	}

	void VoiceEngine::extendDispatch()
	{
		// The three tables copied past 1.63's, every type past 1.63's playing the empty machine until a User Machine fills it
		constexpr std::array<std::pair<TWord, TWord>, 3> tables{{{kDispatchInit, kExtInit}, {kDispatchTrig, kExtTrig},
			{kDispatchRender, kExtRender}}};
		for(const auto& [from, to] : tables)
			for(TWord t = 0; t < kExtTypes; ++t)
				m_dsp->memWriteP(to + t, readP(from + (t < kDispatchTypes ? t : kEmptyType)));
		// The program's own reads of the INIT and update tables (the harness reads RENDER's itself)
		if(readP(kInitBase) != kDispatchInit || readP(kTrigBase) != kDispatchTrig)
			throw std::runtime_error("voice DSP: the dispatch tables are not where OS 1.63 has them");
		m_dsp->memWriteP(kInitBase, kExtInit);
		m_dsp->memWriteP(kTrigBase, kExtTrig);
	}

	void VoiceEngine::checkUserMachine(const md::mds::Package& _package)
	{
		// NEIGHBOUR plays on the ring (installHarness); STEREO PAIR's role word is not there yet
		if(_package.flags & md::mds::FlagStereoPair)
			throw std::runtime_error(_package.displayName() + " pairs with the next Track (STEREO PAIR), which MoDule does"
				" not play yet");
		for(const auto& im : _package.imports)
		{
			if(im.symbol != 1 && im.symbol != 6 && im.symbol != 7 && im.symbol != 8)
				throw std::runtime_error(_package.displayName() + " needs import " + std::to_string(im.symbol)
					+ ", which MoDule does not provide yet");
		}
		if(_package.program.size() > kUserCodeWords)
			throw std::runtime_error(_package.displayName() + " has more code than a User Machine's room");
		if(_package.declaredCycles == 0 || _package.declaredCycles > kMaxDeclaredCycles)
			throw std::runtime_error(_package.displayName() + " declares a cost X.20 refuses");
	}

	void VoiceEngine::installUserMachine(const int _index, std::shared_ptr<const md::mds::Package> _package)
	{
		if(_index < 0 || _index >= kUserMachines || !_package)
			throw std::runtime_error("no such User Machine number");
		checkUserMachine(*_package);
		m_userMachines[static_cast<size_t>(_index)] = std::move(_package);
		writeUserMachine(_index);
		forgetUserMachine(_index);
	}

	void VoiceEngine::removeUserMachine(const int _index)
	{
		if(_index < 0 || _index >= kUserMachines)
			return;
		m_userMachines[static_cast<size_t>(_index)].reset();
		const auto type = kFirstUserType + static_cast<TWord>(_index);
		m_dsp->memWriteP(kExtInit + type, readP(kDispatchInit + kEmptyType));
		m_dsp->memWriteP(kExtTrig + type, readP(kDispatchTrig + kEmptyType));
		m_dsp->memWriteP(kExtRender + type, readP(kDispatchRender + kEmptyType));
		forgetUserMachine(_index);
	}

	void VoiceEngine::forgetUserMachine(const int _index)
	{
		// The dispatcher calls a type's INIT only when a Hit brings a type other than the voice's active one (Y:$153 + t),
		// so a Track that played this number would run the new package's code on the old one's private state at its next
		// Hit: silence, saturation or the wrong pitch, until the Track took another machine and came back. Its active type
		// becomes the empty machine's, silent from now on, and its next Hit INITs what the number holds (the MDS Standard:
		// "A replaced neighbour machine's track stays silent until the new machine is triggered").
		constexpr TWord activeTypes = 0x153;
		const auto type = kFirstUserType + static_cast<TWord>(_index);
		for(TWord t = 0; t < 16; ++t)
			if((m_mem->get(MemArea_Y, activeTypes + t) & 0xffffff) == type)
				m_dsp->memWrite(MemArea_Y, activeTypes + t, kEmptyType);
	}

	void VoiceEngine::writeUserMachine(const int _index)
	{
		const auto& package = *m_userMachines[static_cast<size_t>(_index)];
		const TWord base = kUserCode + kUserCodeWords * static_cast<TWord>(_index);
		auto words = package.program;
		for(size_t i = 0; i < words.size(); ++i)
			if(package.relocate[i])
				words[i] = (words[i] + base) & 0xffffff;
		for(const auto& im : package.imports)
		{
			const TWord resource = im.symbol == 1 ? kImportSine : im.symbol == 6 ? kImportExternal
				: im.symbol == 7 ? kImportScratchX : kImportScratchY;
			words[im.word] = (words[im.word] + resource) & 0xffffff;
		}
		// Through memWriteP, so that the JIT forgets what it compiled of a User Machine this number held before
		for(size_t i = 0; i < words.size(); ++i)
			m_dsp->memWriteP(base + static_cast<TWord>(i), words[i]);
		const auto type = kFirstUserType + static_cast<TWord>(_index);
		m_dsp->memWriteP(kExtInit + type, base + package.initOffset);
		m_dsp->memWriteP(kExtTrig + type, base + package.trigOffset);
		m_dsp->memWriteP(kExtRender + type, base + package.renderOffset);
	}

	void VoiceEngine::installBuiltins(std::shared_ptr<const fw::DspImage> _voice, const uint32_t _ids)
	{
		m_x20Voice = std::move(_voice);
		m_x20Builtins = m_x20Voice ? _ids : 0;
		// A Machine taken back plays OS 1.63's entries again, as extendDispatch copied them
		for(TWord type = 1; type < 32; ++type)
		{
			m_dsp->memWriteP(kExtInit + type, readP(kDispatchInit + type));
			m_dsp->memWriteP(kExtTrig + type, readP(kDispatchTrig + type));
			m_dsp->memWriteP(kExtRender + type, readP(kDispatchRender + type));
		}
		writeBuiltins();
	}

	void VoiceEngine::writeBuiltins()
	{
		if(!m_x20Voice || !m_x20Builtins)
			return;
		// X.20's code where X.20 has it: its built-ins' code and the table they read (research/01 of the map of X.20's
		// built-in Machines), through memWriteP so that the JIT forgets what it compiled there
		constexpr std::array<std::pair<TWord, TWord>, 2> regions{{{0x13b600, 0x13bece}, {0x147600, 0x147c00}}};
		const auto find = [&](const TWord _addr) -> std::optional<TWord>
		{
			for(const auto& r : m_x20Voice->records)
				if(r.space == fw::Space::P && _addr >= r.addr && _addr < r.addr + r.words.size())
					return r.words[_addr - r.addr] & 0xffffff;
			return std::nullopt;
		};
		for(const auto& [first, last] : regions)
			for(TWord a = first; a <= last; ++a)
				if(const auto w = find(a))
					m_dsp->memWriteP(a, *w);
		// Each Machine's three entries from X.20's own copies of OS 1.63's tables, at its type (id + 1)
		for(TWord id = 0; id < 32; ++id)
		{
			if(!((m_x20Builtins >> id) & 1))
				continue;
			const auto type = id + 1;
			const auto init = find(kDispatchInit + type), trig = find(kDispatchTrig + type),
				render = find(kDispatchRender + type);
			if(!init || !trig || !render)
				throw std::runtime_error("voice DSP: X.20's program lacks Machine " + std::to_string(id) + "'s entries");
			m_dsp->memWriteP(kExtInit + type, *init);
			m_dsp->memWriteP(kExtTrig + type, *trig);
			m_dsp->memWriteP(kExtRender + type, *render);
		}
	}

	void VoiceEngine::writeP(const uint32_t _addr, const uint32_t* _words, const size_t _count)
	{
		for(size_t i = 0; i < _count && _addr + i < m_mem->sizeP(); ++i)
			m_mem->set(MemArea_P, static_cast<TWord>(_addr + i), _words[i] & 0xffffff);
	}

	uint32_t VoiceEngine::readP(const uint32_t _addr) const
	{
		return _addr < m_mem->sizeP() ? m_mem->get(MemArea_P, _addr) & 0xffffff : 0;
	}

	void VoiceEngine::reset()
	{
		m_fault.clear();
		m_dsp->resetHW();
		loadImage(m_fw.dspA);	// section 1 = voice program (docs/PROTOCOL.md)
		installHarness();
		extendDispatch();
		for(int i = 0; i < kUserMachines; ++i)
			if(m_userMachines[static_cast<size_t>(i)])
				writeUserMachine(i);
		writeBuiltins();
		for(int i = 0; i < 4; ++i)
			m_periphX->write(0xfffff9 - static_cast<TWord>(i), kAar[i]);
		m_dsp->regs().omr.var = kOmr;
		auto& hi = m_periphX->getHI08();
		hi.clearRX();
		while(hi.hasTX())
			hi.readTX();
		m_dsp->setPC(kEntry);

		// Init runs into the block loop; the first (silent) block comes out and the DSP then waits for "go".
		Block first;
		if(!readBlock(first, kMaxInstrInit))
			throw std::runtime_error("voice DSP init did not complete: " + m_fault);
		while(hi.hasTX())
			hi.readTX();
		// The ring's words again, whatever the program's init did to that memory, and every voice rendering
		writeSources();
		m_slots = {};
		m_quietRun = {};
		m_sounded.fill(true);
		m_stopped = {};
	}

	void VoiceEngine::setSlot(const int _voice, const uint32_t* _words, const int _count)
	{
		const TWord base = kSlotBase + kSlotStride * static_cast<TWord>(_voice);
		for(int k = 0; k < _count; ++k)
			m_dsp->memWrite(MemArea_Y, base + static_cast<TWord>(k), _words[k] & 0xffffff);
		if(m_quietTypes.none())
			return;
		// Word 0 is a Hit; the others change with a knob, a lock or an LFO
		auto& slot = m_slots[static_cast<size_t>(_voice)];
		const auto count = static_cast<size_t>(std::min(_count, kSlotWords));
		const bool hit = _count > 0 && (_words[0] & 0xffffff) != 0;
		const bool changed = count > 1 && !std::equal(_words + 1, _words + count, slot.begin() + 1);
		std::copy(_words, _words + count, slot.begin());
		if(hit)
			m_sounded[static_cast<size_t>(_voice)] = false;
		if(hit || changed)
			wake(_voice);
	}

	void VoiceEngine::setQuietSkip(const std::bitset<256>& _types)
	{
		m_quietTypes = _types;
		for(int v = 0; v < kVoices; ++v)
			wake(v);
	}

	void VoiceEngine::wake(const int _voice)
	{
		const auto v = static_cast<size_t>(_voice);
		m_quietRun[v] = 0;
		if(!m_stopped[v])
			return;
		m_stopped[v] = false;
		m_dsp->memWrite(MemArea_Y, kStopFlags + 4 * static_cast<TWord>(_voice), 0);
	}

	void VoiceEngine::stopQuietVoices(const Block& _out)
	{
		for(size_t v = 0; v < _out.size(); ++v)
		{
			if(m_stopped[v])
			{
				++m_skipped;
				continue;
			}
			const bool quiet = std::all_of(_out[v].begin(), _out[v].end(),
				[](const int32_t _s) { return _s >= -kQuiet && _s <= kQuiet; });
			if(!quiet)
			{
				m_sounded[v] = true;
				m_quietRun[v] = 0;
				continue;
			}
			if(++m_quietRun[v] < (m_sounded[v] ? kQuietBlocks : kSilentBlocks))
				continue;
			const auto type = m_mem->get(MemArea_Y, kActiveTypes + static_cast<TWord>(v)) & 0xffffff;
			const bool x20 = type >= 1 && type <= 32 && ((m_x20Builtins >> (type - 1)) & 1);
			if(type >= m_quietTypes.size() || !m_quietTypes[type] || x20)
				continue;
			m_stopped[v] = true;
			m_dsp->memWrite(MemArea_Y, kStopFlags + 4 * static_cast<TWord>(v), 1);
		}
	}

	bool VoiceEngine::runUntilTx(const size_t _words, const uint64_t _maxInstructions)
	{
		auto& hi = m_periphX->getHI08();
		const auto start = m_dsp->getInstructionCounter();
		static const bool debug = std::getenv("MDV_DEBUG") != nullptr;
		uint64_t nextReport = start + 5'000'000;
		m_txWanted = _words;
		while(hi.txData().size() < _words)
		{
			m_dsp->execUntilCycles(m_dsp->getCycles() + kRunCycles);
			if(debug && m_dsp->getInstructionCounter() > nextReport)
			{
				std::fprintf(stderr, "  PC=$%06x instr=%llu tx=%zu\n", m_dsp->getPC().toWord(),
					static_cast<unsigned long long>(m_dsp->getInstructionCounter() - start), hi.txData().size());
				nextReport += 5'000'000;
			}
			if(m_dsp->getInstructionCounter() - start > _maxInstructions)
			{
				std::ostringstream o;
				o << "instruction budget exceeded at PC=$" << std::hex << m_dsp->getPC().toWord() << std::dec
				  << " with " << hi.txData().size() << "/" << _words << " words";
				m_fault = o.str();
				m_txWanted = 0;
				return false;
			}
		}
		m_txWanted = 0;
		return true;
	}

	// One block from the DSP, voice by voice: a flag word (1 = it rendered, its 32 samples follow; 0 = idle, all zeros).
	// Every voice's slot is read before its flag goes out, so once the 16th flag is in the DSP has consumed every slot.
	bool VoiceEngine::readBlock(Block& _out, const uint64_t _maxInstructions)
	{
		auto& hi = m_periphX->getHI08();
		for(auto& voice : _out)
		{
			if(!runUntilTx(1, _maxInstructions))
				return false;
			const TWord flag = hi.readTX() & 0xffffff;
			if(flag != 1)
			{
				voice.fill(0);
				continue;
			}
			if(!runUntilTx(voice.size(), _maxInstructions))
				return false;
			std::array<TWord, kBlockFrames> words;
			hi.readTX(words.data(), words.size());
			for(size_t i = 0; i < voice.size(); ++i)
				voice[i] = static_cast<int32_t>((words[i] & 0xffffff) << 8) >> 8;
		}
		return true;
	}

	struct VoiceEngine::State
	{
		std::vector<TWord> p, x, y;
		DspRegs regs;
		std::array<std::array<uint32_t, kSlotWords>, kVoices> slots;
		std::array<int, kVoices> quietRun;
		std::array<bool, kVoices> sounded, stopped;
	};

	std::shared_ptr<VoiceEngine::State> VoiceEngine::saveState() const
	{
		// P as large as the external memory X and Y share with it; X and Y up to where they join it
		const auto sizeP = Memory::calcPMemSize(kSizeP, kSizeXY, kBridge);
		const auto sizeXY = Memory::calcXYMemSize(kSizeXY, kBridge);
		auto state = std::make_shared<State>();
		const auto* p = m_mem->getMemAreaPtr(MemArea_P);
		const auto* x = m_mem->getMemAreaPtr(MemArea_X);
		const auto* y = m_mem->getMemAreaPtr(MemArea_Y);
		state->p.assign(p, p + sizeP);
		state->x.assign(x, x + sizeXY);
		state->y.assign(y, y + sizeXY);
		state->regs = m_dsp->regs();
		state->slots = m_slots;
		state->quietRun = m_quietRun;
		state->sounded = m_sounded;
		state->stopped = m_stopped;
		return state;
	}

	void VoiceEngine::restoreState(const State& _state)
	{
		std::copy(_state.p.begin(), _state.p.end(), m_mem->getMemAreaPtr(MemArea_P));
		std::copy(_state.x.begin(), _state.x.end(), m_mem->getMemAreaPtr(MemArea_X));
		std::copy(_state.y.begin(), _state.y.end(), m_mem->getMemAreaPtr(MemArea_Y));
		m_dsp->regs() = _state.regs;
		m_slots = _state.slots;
		m_quietRun = _state.quietRun;
		m_sounded = _state.sounded;
		m_stopped = _state.stopped;
		m_fault.clear();
	}

	bool VoiceEngine::renderBlock(Block& _out)
	{
		auto& hi = m_periphX->getHI08();
		const TWord go = 1;
		hi.writeRX(&go, 1);
		if(!readBlock(_out, kMaxInstrBlock))
			return false;
		if(m_quietTypes.any())
			stopQuietVoices(_out);
		return true;
	}
}
