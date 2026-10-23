#include "stSimd.h"

#include <cstdlib>

#if defined(_M_X64) || defined(__x86_64__)
#	ifdef _MSC_VER
#		include <intrin.h>
#	endif
#endif

namespace syntakt::simd
{
	namespace
	{
		// AVX2 on the CPU, and the YMM registers saved by the OS (OSXSAVE, then XCR0's SSE and AVX state)
		bool cpuHasAvx2()
		{
#if defined(_M_X64) || defined(__x86_64__)
#	ifdef _MSC_VER
			int r[4];
			__cpuid(r, 0);
			if(r[0] < 7)
				return false;
			__cpuid(r, 1);
			const bool osxsave = (r[2] >> 27) & 1, avx = (r[2] >> 28) & 1;
			if(!osxsave || !avx || (_xgetbv(0) & 6) != 6)
				return false;
			__cpuidex(r, 7, 0);
			return (r[1] >> 5) & 1;
#	else
			return __builtin_cpu_supports("avx2");
#	endif
#else
			return false;
#endif
		}

		bool forcedOff()
		{
			const char* v = std::getenv("SYNTAKT_NO_AVX2");
			return v && *v && !(v[0] == '0' && !v[1]);
		}

		bool& enabled()
		{
			static bool on = cpuHasAvx2() && !forcedOff();
			return on;
		}
	}

	bool avx2()
	{
		return enabled();
	}

	void setAvx2(const bool _on)
	{
		enabled() = _on && cpuHasAvx2();
	}
}
