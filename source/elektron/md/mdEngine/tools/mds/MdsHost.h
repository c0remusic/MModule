// A User Machine played on a bare DSP56300 of its own, as jmamma/MDS's docs/MDX_MachineDumpStandard.txt describes its
// host: the package relocated into memory that P, X and Y share, its imports patched, its eight parameters in
// Y:(R6+1..8), INIT, then TRIG on each Hit and RENDER every block of 32 samples, on one Track. mdsPlay plays packages
// with it, and the engine's tests compare the voice DSP with it.
#pragma once

#include <cstdint>
#include <memory>

#include "MdsPackage.h"

namespace dsp56k { class DSP; class Memory; class PeripheralsNop; class DefaultMemoryValidator; }

namespace md::mds
{
	class BareHost
	{
	public:
		// Throws on an import it does not provide (the services). _decay: the table Decay parameters read (decayTable)
		BareHost(const Package& _package, const DecayTable& _decay);
		~BareHost();

		// The word at Y:(R6+1+_index), as the conversion of the parameter gives it
		void setParameter(size_t _index, int _setting);

		// Each call's cycles, the call's own few included (overhead() is that alone)
		uint64_t overhead();
		uint64_t init();
		uint64_t trig();
		uint64_t render(int32_t* _out);	// 32 samples, 24-bit signed

		void disassemble() const;	// the program as loaded, its routines marked, to stdout

	private:
		uint32_t stub(uint32_t _pc, uint32_t _entry);
		uint64_t call(uint32_t _stub, uint32_t _end);

		const Package& m_package;
		const DecayTable m_decay;
		std::unique_ptr<dsp56k::DefaultMemoryValidator> m_validator;
		std::unique_ptr<dsp56k::PeripheralsNop> m_periphX, m_periphY;
		std::unique_ptr<dsp56k::Memory> m_mem;
		std::unique_ptr<dsp56k::DSP> m_dsp;
		uint32_t m_endInit = 0, m_endTrig = 0, m_endRender = 0, m_endEmpty = 0;
	};
}
