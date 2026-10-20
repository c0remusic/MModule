// Runs the Machinedrum voice DSP (DSP2) program from the user's OS file on its own, in the dsp56300
// emulator: no ColdFire, no mixer DSP. The hardware's link DMA and frame sync are replaced by a small
// harness stub (the Monomodule approach): each 32-sample block renders all 16 voices, and every voice's
// 32 samples go out over the host port instead of ESSI0. The host fills the 16 voice slots (Y:$800 +
// $40*k: word 0 = trigger/machine code, words 1-12 = machine coefficients) directly in DSP memory
// between blocks. See docs/PROTOCOL.md.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "../tools/mdfw/Firmware.h"

namespace dsp56k { class DSP; class Memory; class Peripherals56303; class PeripheralsNop; class DefaultMemoryValidator; }
namespace md::mds { struct Package; }

namespace md::engine
{
	class VoiceEngine
	{
	public:
		static constexpr int kVoices = 16;
		static constexpr int kBlockFrames = 32;
		static constexpr int kSlotWords = 32;	// machine functions return 0-21 words (the slot is 0x40 wide); 13 truncated EFM-CY and dropped the trigger of 0-word machines
		static constexpr uint32_t kSlotBase = 0x800, kSlotStride = 0x40;

		using Block = std::array<std::array<int32_t, kBlockFrames>, kVoices>;	// [voice][frame], 24-bit signed

		explicit VoiceEngine(const fw::Firmware& _fw);
		~VoiceEngine();

		// Load the program, run its own init, install the harness. Throws on failure.
		void reset();

		// Write a voice slot (up to kSlotWords words) for the next block. Word 0 is the trigger/machine
		// code: non-zero only on the block the voice is (re)triggered; the DSP clears it.
		void setSlot(int _voice, const uint32_t* _words, int _count = kSlotWords);

		// Render one 32-sample block of all 16 voices. Returns false on a fault.
		bool renderBlock(Block& _out);

		// Write words into P memory (e.g. the ROM machines' sample memory, which the MD copies from its sample flash at
		// boot - not part of the OS file). reset() reloads the program's own records, the ROM sample directory among them,
		// so write again after a reset.
		void writeP(uint32_t _addr, const uint32_t* _words, size_t _count);
		uint32_t readP(uint32_t _addr) const;

		// md-drums: User Machines (MDX X.20's, jmamma/MDS packages) at USR-01 to USR-16, _index 0-15, as X.20 runs them:
		// the dispatch tables extended from OS 1.63's 193 types to 211, USR-01 being type 195 (its machine id 194, + 1),
		// each package's code relocated into the memory P, X and Y share ($020000 + $E000 words a number), and its imports
		// on 1.63's layout (the sine table, P-I's 1,536 words of external memory per Track, the X and Y scratch). A type
		// without a User Machine plays the empty machine. A NEIGHBOUR package reads the three Tracks before it through the
		// descriptor at Y:$7F0, on the harness's ring of four buffers (research/12 of the User Machines map). Throws,
		// installing nothing, on a package it cannot play: an import it does not provide (the Sample and RAM services, the
		// rings), STEREO PAIR (its role word is not there), more code than a number's room, a declared cost X.20 refuses.
		// reset() keeps them.
		static constexpr int kUserMachines = 16;
		static constexpr uint32_t kFirstUserType = 195;
		static void checkUserMachine(const md::mds::Package& _package);
		void installUserMachine(int _index, std::shared_ptr<const md::mds::Package> _package);
		void removeUserMachine(int _index);

		// md-drums: the DSP's memory and registers between two blocks, and back (EngineT::warmUpVoices, which runs every
		// machine once so that the JIT compiles its code, then puts the DSP back as it was: P holds the same code after,
		// so what the JIT compiled stays valid)
		struct State;
		std::shared_ptr<State> saveState() const;
		void restoreState(const State& _state);

		const std::string& faultReason() const { return m_fault; }
		dsp56k::DSP& dsp() { return *m_dsp; }

	private:
		void loadImage(const fw::DspImage& _img);
		void installHarness();
		void writeRing();	// the neighbours' ring: its two extra buffers and four descriptors in Y
		void extendDispatch();
		void writeUserMachine(int _index);
		// md-drums: the Tracks whose voice runs User Machine _index's code made silent until their next Hit
		void forgetUserMachine(int _index);
		bool runUntilTx(size_t _words, uint64_t _maxInstructions);
		bool readBlock(Block& _out, uint64_t _maxInstructions);

		const fw::Firmware& m_fw;
		std::unique_ptr<dsp56k::DefaultMemoryValidator> m_validator;
		std::unique_ptr<dsp56k::Memory> m_mem;
		std::unique_ptr<dsp56k::Peripherals56303> m_periphX;
		std::unique_ptr<dsp56k::PeripheralsNop> m_periphY;
		std::unique_ptr<dsp56k::DSP> m_dsp;
		std::string m_fault;
		std::array<std::shared_ptr<const md::mds::Package>, kUserMachines> m_userMachines{};
	};
}
