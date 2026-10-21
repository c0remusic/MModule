// The port of X.20's six coefficient routines (md::engine::x20::compute) against the routines themselves, run in a
// 68k on the user's own X.20 OS where MD_X20_OS names it (never in this repository): X.20's table builders run first
// (its static initialiser, the tuning tables at TUNING 0), then each routine and the port on the same parameter words,
// the four tuning modes, each parameter swept over its 128 settings and random 14-bit words, every word the routine
// writes compared, those it leaves alone included (ticket 07 of the map of X.20's built-in Machines). Musashi serves
// one CPU class per executable, so this test links no engine: the port, the OS reader and the 68k alone.

#include "X20Builtins.h"
#include "X20Os.h"

#include "mc68k/mc68k.h"
#include "mc68k/cpuState.h"
#include "Musashi/m68k.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace x20port
{
	constexpr uint32_t kMainBase = 0x200000, kMainSize = 0x100000;

	class Cpu final : public mc68k::Mc68k
	{
	public:
		Cpu() : Mc68k(M68K_CPU_TYPE_MCF5206E), m_main(kMainSize, 0) {}
		uint8_t read8(const uint32_t _a) override { if(auto* p = ptr(_a, 1)) return *p; m_bad = true; return 0; }
		uint16_t read16(const uint32_t _a) override
		{
			if(auto* p = ptr(_a, 2)) return static_cast<uint16_t>(p[0] << 8 | p[1]);
			m_bad = true;
			return 0;
		}
		uint16_t readImm16(const uint32_t _a) override { return read16(_a); }
		void write8(const uint32_t _a, const uint8_t _v) override { if(auto* p = ptr(_a, 1)) *p = _v; else m_bad = true; }
		void write16(const uint32_t _a, const uint16_t _v) override
		{
			if(auto* p = ptr(_a, 2)) { p[0] = static_cast<uint8_t>(_v >> 8); p[1] = static_cast<uint8_t>(_v); }
			else m_bad = true;
		}
		uint32_t exec() override { return execInstruction(); }

		uint8_t* ptr(const uint32_t _a, const uint32_t _n)
		{
			return _a >= kMainBase && _a + _n <= kMainBase + kMainSize ? &m_main[_a - kMainBase] : nullptr;
		}
		uint32_t p32(const uint32_t _a) { return static_cast<uint32_t>(read16(_a)) << 16 | read16(_a + 2); }
		void w32(const uint32_t _a, const uint32_t _v)
		{
			write16(_a, static_cast<uint16_t>(_v >> 16));
			write16(_a + 2, static_cast<uint16_t>(_v));
		}

		// From _pc until PC reaches _stop; d0, or throws on a bad access or a runaway
		uint32_t run(const uint32_t _pc, const uint32_t _stop, const uint32_t _sp)
		{
			m68k_set_reg(getCpuState(), M68K_REG_A7, _sp);
			setPC(_pc);
			m_bad = false;
			for(uint64_t n = 0; getPC() != _stop; ++n)
			{
				exec();
				if(m_bad || n > 50'000'000)
					throw std::runtime_error("68k fault near " + std::to_string(getPC()));
			}
			return getDReg(0);
		}
		uint32_t call(const uint32_t _fn, const std::vector<uint32_t>& _args)
		{
			uint32_t sp = 0x2fff00;
			for(auto it = _args.rbegin(); it != _args.rend(); ++it) { sp -= 4; w32(sp, *it); }
			sp -= 4;
			w32(sp, 0x100);	// return to an unmapped address, where run stops
			return run(_fn, 0x100, sp);
		}

		std::vector<uint8_t> m_main;
		bool m_bad = false;
	};
}

#define MC68K_CLASS x20port::Cpu
#include "mc68k/musashiEntry.h"

namespace
{
	namespace x20 = md::engine::x20;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// X.20 B's routines (research/02 of the map), and where this test puts what they read
	struct Routine { uint8_t id; const char* name; uint32_t fn; };
	constexpr Routine kRoutines[] = {{x20::GndSnPro, "GND-SN-PRO", 0x21ada2}, {x20::GndSw, "GND-SW", 0x21acbc},
		{x20::GndPu, "GND-PU", 0x21abd2}, {x20::NfxEv, "NFX-EV", 0x2197d2}, {x20::NfxCo, "NFX-CO", 0x2196c0},
		{x20::NfxUc, "NFX-UC", 0x219402}};
	constexpr uint32_t kDecay = 0x2f0000, kEngine = 0x2f1000, kCtx = 0x2f2000, kWords = 0x2f2200;
	constexpr uint32_t kLeft = 0xdeadbeef;	// a word the routine does not write keeps it
	constexpr uint32_t kTempo24 = 125 * 24;

	struct Checker
	{
		x20port::Cpu cpu;
		x20::DecayTable decay{};
		size_t calls = 0, differ = 0;

		// _os: X.20's 68k OS; _os163: OS 1.63's, whose decay table ($24bf94) X.20's routines read through the context
		Checker(const std::vector<uint8_t>& _os, const std::vector<uint8_t>& _os163)
		{
			constexpr size_t decayAt = 0x24bf94 - x20port::kMainBase;
			require(_os163.size() >= decayAt + 4 * decay.size(), "OS 1.63's image ends before its decay table");
			for(size_t i = 0; i < decay.size(); ++i)
			{
				const auto* p = &_os163[decayAt + 4 * i];
				decay[i] = static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16
					| static_cast<uint32_t>(p[2]) << 8 | p[3];
			}
			require(_os.size() <= x20port::kMainSize, "X.20's 68k OS larger than the test's memory");
			std::memcpy(cpu.m_main.data(), _os.data(), _os.size());
			m68k_set_reg(cpu.getCpuState(), M68K_REG_SR, 0x2700);
			// X.20's static initialiser's two table loops, then the tuning builder at TUNING 0, as its boot calls it
			cpu.run(0x200376, 0x2003de, 0x2fff00);
			cpu.w32(0x2cdc78, 0);
			cpu.call(0x21bd6c, {0});
			cpu.w32(0x2cdc6c, kTempo24);
			// The decay table where the context points (+$90 -> engine + 4 -> +$24), as X.20's boot points it at 1.63's
			for(size_t i = 0; i < decay.size(); ++i)
				cpu.w32(kDecay + 4 * static_cast<uint32_t>(i), decay[i]);
			cpu.w32(kEngine + 0x28, kDecay);
			cpu.w32(kCtx + 0x90, kEngine + 4);
		}

		void check(const Routine& _r, const int _mode, const uint16_t* _w)
		{
			for(int k = 0; k < 8; ++k)
				cpu.write16(kWords + 2 * static_cast<uint32_t>(k), _w[k]);
			for(uint32_t k = 0; k < 12; ++k)
				cpu.w32(kCtx + 4 * k, kLeft);
			cpu.write16(kCtx + 0x8c, static_cast<uint16_t>(_mode));
			const auto count = static_cast<int>(cpu.call(_r.fn, {kCtx, kWords}));
			uint32_t port[11];
			std::fill(std::begin(port), std::end(port), kLeft);
			const int n = x20::compute(_r.id, _w, _mode, decay, kTempo24, port);
			bool same = n == count;
			for(int k = 0; k < 11; ++k)
				same &= port[k] == cpu.p32(kCtx + 4 * static_cast<uint32_t>(k + 1));
			++calls;
			if(same)
				return;
			if(differ++ < 4)
			{
				std::printf("  %s mode %d words", _r.name, _mode);
				for(int k = 0; k < 8; ++k)
					std::printf(" %04x", _w[k]);
				std::printf(": 68k %d", count);
				for(int k = 0; k < 11; ++k)
					std::printf(" %x", cpu.p32(kCtx + 4 * static_cast<uint32_t>(k + 1)));
				std::printf(" / port %d", n);
				for(const auto w : port)
					std::printf(" %x", w);
				std::printf("\n");
			}
		}
	};
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		const auto* path = std::getenv("MD_X20_OS");
		if(!path || !*path)
		{
			std::printf("mdX20PortTest: SKIP (MD_X20_OS unset)\n");
			return 77;
		}
		const auto* flash = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		if(!flash || !*flash)
		{
			std::printf("mdX20PortTest: SKIP (GEARMULATOR_MD_FIRMWARE_BIN unset: no OS 1.63 decay table)\n");
			return 77;
		}
		const auto os = md::fw::parseX20Os(md::fw::readFile(path));
		const auto os163 = md::fw::loadFirmwareFromFlash(md::fw::readFile(flash)).osImage;
		Checker checker(os.os, os163);
		std::mt19937 rng(20261021);
		for(const auto& r : kRoutines)
		{
			const auto before = checker.differ;
			for(int mode = 0; mode < 4; ++mode)
			{
				for(int p = 0; p < 8; ++p)
					for(int s = 0; s < 128; ++s)
					{
						uint16_t w[8];
						std::fill(std::begin(w), std::end(w), static_cast<uint16_t>(64 << 7));
						w[p] = static_cast<uint16_t>(s << 7);
						checker.check(r, mode, w);
					}
				for(int i = 0; i < 2000; ++i)
				{
					uint16_t w[8];
					for(auto& x : w)
						x = static_cast<uint16_t>(rng() % 0x4000);
					if(i % 4 == 0)
						w[rng() % 8] = static_cast<uint16_t>(i & 1 ? 0x3fff : 0);
					checker.check(r, mode, w);
				}
			}
			std::printf("%-10s: %zu differ\n", r.name, checker.differ - before);
		}
		std::printf("%zu calls, %zu differ\n", checker.calls, checker.differ);
		require(checker.differ == 0, "the port differs from X.20's routines");
	}
	catch(const std::exception& _e)
	{
		std::printf("mdX20PortTest: FAIL: %s\n", _e.what());
		return 1;
	}
	std::printf("mdX20PortTest: PASS\n");
	return 0;
}
