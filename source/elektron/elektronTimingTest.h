#pragma once

// A timing test's process at High priority (HIGH_PRIORITY_CLASS, which needs no privilege), so that other programs
// preempt its threads less: mdDrumsEngineTest under ten busy processes took 31.1 to 33.7 % of a core at High, 31.8
// to 47.9 % at Normal (22 % on a quiet machine). A process does not inherit it from ctest, so each timing test asks.
// The tests that use JUCE ask through juce::Process::setPriority instead
#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall SetPriorityClass(void* _process, unsigned long _priorityClass);
extern "C" __declspec(dllimport) void* __stdcall GetCurrentProcess();
#endif

namespace elektron
{
	inline void raiseTimingTestPriority()
	{
#ifdef _WIN32
		SetPriorityClass(GetCurrentProcess(), 0x00000080);
#endif
	}
}
