#include "MdsHost.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/disasm.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/opcodes.h"
#include "dsp56kEmu/peripherals.h"

namespace md::mds
{
	using namespace dsp56k;

	namespace
	{
		// A memory map of our own: the documents leave the addresses to the host (the code reaches everything through
		// R6, R7, the patched imports and Y:$07F0)
		constexpr TWord kSizeP = 0x100000, kSizeXY = 0x100000, kBridge = 0x020000;
		constexpr TWord kScratchX = 0x0000;		// import 7, 32 words (SAMPLE_READ's X:$00-$1F)
		constexpr TWord kScratchY = 0x0000;		// import 8, 32 words
		constexpr TWord kDescriptor = 0x07e0;	// a Track's four neighbour buffer addresses
		constexpr TWord kDescriptorPtr = 0x07f0;
		constexpr TWord kOutput = 0x0800;		// Y, 32 words, aligned for M7 = $1F
		constexpr TWord kNeighbours = 0x0820;	// Y, three more buffers of 32
		constexpr TWord kTrack = 0x1000;		// R6: the Track's 64 X and 64 Y words
		constexpr TWord kInputRing = 0x4000;	// import 2, 256 words, silent
		constexpr TWord kInputPos = 0x4100;		// import 3
		constexpr TWord kMainRing = 0x4200;		// import 4, 256 words, silent
		constexpr TWord kMainPos = 0x4300;		// import 5: holds the address of the block to read
		constexpr TWord kSine = 0x8000;			// import 1, 32,768 words
		constexpr TWord kExternal = 0x40000;	// import 6: 16 x 1,536 words where X and Y are the same storage
		// The program in external memory, where P, X and Y are the same storage: packages read their tables with x:
		// moves at addresses inside their own code (TX8-BD's lookups)
		constexpr TWord kProgram = 0x30000;
		constexpr TWord kStubInit = 0x0100, kStubTrig = 0x0120, kStubRender = 0x0140, kStubEmpty = 0x0160;
		constexpr TWord kRts = 0x0180;
		constexpr uint64_t kMaxInstructions = 10'000'000;
	}

	BareHost::BareHost(const Package& _package, const DecayTable& _decay) : m_package(_package), m_decay(_decay)
	{
		m_validator = std::make_unique<DefaultMemoryValidator>();
		m_periphX = std::make_unique<PeripheralsNop>();
		m_periphY = std::make_unique<PeripheralsNop>();
		m_mem = std::make_unique<Memory>(*m_validator, kSizeP, kSizeXY, kBridge);
		m_dsp = std::make_unique<DSP>(*m_mem, m_periphX.get(), m_periphY.get());
		m_dsp->resetHW();

		// The program at kProgram: relocated words get the base, imports their resource's address
		std::vector<uint32_t> words = m_package.program;
		for(size_t i = 0; i < words.size(); ++i)
		{
			if(m_package.relocate[i])
				words[i] = (words[i] + kProgram) & 0xffffff;
		}
		for(const auto& im : m_package.imports)
		{
			TWord base = 0;
			switch(im.symbol)
			{
			case 1: base = kSine; break;
			case 2: base = kInputRing; break;
			case 3: base = kInputPos; break;
			case 4: base = kMainRing; break;
			case 5: base = kMainPos; break;
			case 6: base = kExternal; break;
			case 7: base = kScratchX; break;
			case 8: base = kScratchY; break;
			default: throw std::runtime_error("import of symbol " + std::to_string(im.symbol) + " is not provided");
			}
			words[im.word] = (words[im.word] + base) & 0xffffff;
		}
		for(size_t i = 0; i < words.size(); ++i)
			m_dsp->memWriteP(kProgram + static_cast<TWord>(i), words[i]);

		for(TWord i = 0; i < 32768; ++i)
		{
			const auto v = std::lround(std::sin(2.0 * 3.14159265358979323846 * i / 32768.0) * 8388607.0);
			m_dsp->memWrite(MemArea_X, kSine + i, static_cast<TWord>(v) & 0xffffff);
		}
		m_dsp->memWrite(MemArea_X, kMainPos, kMainRing);
		for(TWord k = 0; k < 4; ++k)
			m_dsp->memWrite(MemArea_Y, kDescriptor + k, k == 0 ? kOutput : kNeighbours + 32 * (k - 1));
		m_dsp->memWrite(MemArea_Y, kDescriptorPtr, kDescriptor);

		m_endInit = stub(kStubInit, kProgram + m_package.initOffset);
		m_endTrig = stub(kStubTrig, kProgram + m_package.trigOffset);
		m_endRender = stub(kStubRender, kProgram + m_package.renderOffset);
		m_dsp->memWriteP(kRts, 0x00000c);	// rts
		m_endEmpty = stub(kStubEmpty, kRts);
	}

	BareHost::~BareHost() = default;

	// Registers as the documents give them on entry, then jsr to the routine and stop on a jmp to itself, whose address
	// it returns
	uint32_t BareHost::stub(uint32_t _pc, const uint32_t _entry)
	{
		Assembler as;
		const auto emit = [&](const std::string& _text)
		{
			const auto r = as.assemble(_text.c_str());
			if(!r.success())
				throw std::runtime_error("cannot assemble '" + _text + "'");
			m_dsp->memWriteP(_pc++, r.word[0]);
			if(r.wordCount > 1)
				m_dsp->memWriteP(_pc++, r.word[1]);
		};
		const auto hex = [](const TWord _v) { char s[16]; std::snprintf(s, sizeof(s), "$%x", _v); return std::string(s); };
		emit("move #>" + hex(kTrack) + ",r6");
		emit("move #>0,r1");	// Track 1
		emit("move #>" + hex(kOutput) + ",r7");
		emit("move #>$1f,m7");
		for(int m = 0; m <= 5; ++m)
			emit("move #>$ffffff,m" + std::to_string(m));
		m_dsp->memWriteP(_pc++, 0x0bf080);	// jsr >entry
		m_dsp->memWriteP(_pc++, _entry);
		const auto self = _pc;
		m_dsp->memWriteP(_pc++, 0x0af080);	// jmp >self
		m_dsp->memWriteP(_pc++, self);
		return self;
	}

	uint64_t BareHost::call(const uint32_t _stub, const uint32_t _end)
	{
		m_dsp->setPC(_stub);
		const auto start = m_dsp->getCycles();
		const auto instructions = m_dsp->getInstructionCounter();
		while(m_dsp->getPC().toWord() != _end)
		{
			m_dsp->exec();
			if(m_dsp->getInstructionCounter() - instructions > kMaxInstructions)
				throw std::runtime_error("a routine did not return");
		}
		return m_dsp->getCycles() - start;
	}

	void BareHost::setParameter(const size_t _index, const int _setting)
	{
		m_dsp->memWrite(MemArea_Y, kTrack + 1 + static_cast<TWord>(_index), convert(m_package.parameters[_index], _setting,
			m_decay));
	}

	void BareHost::disassemble() const
	{
		Opcodes opcodes;
		Disassembler disasm(opcodes);
		const auto size = static_cast<TWord>(m_package.program.size());
		for(TWord i = 0; i < size;)
		{
			const auto op = m_mem->get(MemArea_P, kProgram + i);
			const auto opB = i + 1 < size ? m_mem->get(MemArea_P, kProgram + i + 1) : 0;
			std::string text;
			const auto count = std::max<uint32_t>(1, disasm.disassemble(text, op, opB, 0, 0, kProgram + i));
			const char* label = i == m_package.initOffset ? "INIT:" : i == m_package.trigOffset ? "TRIG:" :
				i == m_package.renderOffset ? "RENDER:" : "";
			std::printf("%-8s P:$%04x  %06x %s %s\n", label, kProgram + i, op, count > 1 ? "+1" : "  ", text.c_str());
			i += count;
		}
	}

	uint64_t BareHost::overhead() { return call(kStubEmpty, m_endEmpty); }
	uint64_t BareHost::init() { return call(kStubInit, m_endInit); }
	uint64_t BareHost::trig() { return call(kStubTrig, m_endTrig); }

	uint64_t BareHost::render(int32_t* _out)
	{
		const auto c = call(kStubRender, m_endRender);
		for(TWord i = 0; i < 32; ++i)
			_out[i] = static_cast<int32_t>(m_mem->get(MemArea_Y, kOutput + i) << 8) >> 8;
		return c;
	}
}
