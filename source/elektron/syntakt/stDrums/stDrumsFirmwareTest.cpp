// The .syx reader ST Drums loads its firmware with (stDrumsFirmware.h), on the user's own Syntakt OS 1.41 file:
//
//   - sections 3 and 7 come out with OS 1.41's sizes and the FNV-1a hashes stSyntakt's loaders check (loadSection3,
//     loadSection7), and section 3 is byte for byte SYNTAKT_SECTION3 when that is set (the sections an outside tool
//     extracted, which the other Syntakt tests read);
//   - load gives section 7 as the words loadSection7 gives;
//   - files that are not OS 1.41 are refused, with a reason, not read wrong: no SysEx, a truncated file, a byte changed
//     inside section 3's packed stream.
//
// SYNTAKT_OS_SYX names the user's Syntakt_OS1.41.syx, outside the repository; exit 77 without it.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "stDrumsFirmware.h"
#include "stSection7.h"

namespace
{
	uint64_t fnv(const std::vector<uint8_t>& _bytes)
	{
		uint64_t h = 0xcbf29ce484222325;
		for(const uint8_t b : _bytes)
			h = (h ^ b) * 0x100000001b3;
		return h;
	}

	bool check(const bool _ok, const char* _what)
	{
		std::printf("  %-72s %s\n", _what, _ok ? "ok" : "FAILED");
		return _ok;
	}

	// _syx refused by load, with a reason
	bool refused(const std::vector<uint8_t>& _syx, const char* _what)
	{
		stDrums::Firmware firmware;
		std::string error;
		const bool ok = !stDrums::load(_syx, firmware, error) && !error.empty();
		std::printf("  %-72s %s (%s)\n", _what, ok ? "refused" : "FAILED", error.c_str());
		return ok;
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);

	const char* syxPath = std::getenv("SYNTAKT_OS_SYX");
	if(!syxPath || !*syxPath)
	{
		std::printf("SKIP: set SYNTAKT_OS_SYX to the user's Syntakt_OS1.41.syx\n");
		return 77;
	}
	const auto syx = syntakt::readFile(syxPath);
	if(syx.empty())
	{
		std::printf("FAIL: cannot read %s\n", syxPath);
		return 1;
	}

	bool ok = true;
	const auto start = std::chrono::steady_clock::now();
	stDrums::Sections sections;
	std::string error;
	ok &= check(stDrums::unpack(syx, sections, error), "the .syx unpacks");
	const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	std::printf("  %zu bytes of SysEx: section 3 %zu bytes, section 7 %zu bytes, in %.0f ms %s\n", syx.size(),
		sections.section3.size(), sections.section7.size(), ms, error.c_str());

	ok &= check(sections.section3.size() == syntakt::main::Section3Size
		&& fnv(sections.section3) == syntakt::main::Section3Fnv, "section 3: OS 1.41's size and FNV-1a hash");
	ok &= check(sections.section7.size() == syntakt::Section7Size && fnv(sections.section7) == syntakt::Section7Fnv,
		"section 7: OS 1.41's size and FNV-1a hash (loadSection7's)");

	if(const char* section3Path = std::getenv("SYNTAKT_SECTION3"); section3Path && *section3Path)
		ok &= check(sections.section3 == syntakt::readFile(section3Path), "section 3 byte for byte SYNTAKT_SECTION3");
	else
		std::printf("  (SYNTAKT_SECTION3 unset: section 3 checked by its hash alone)\n");

	stDrums::Firmware firmware;
	ok &= check(stDrums::load(syx, firmware, error), "load takes it");
	if(const char* section7Path = std::getenv("SYNTAKT_SECTION7"); section7Path && *section7Path)
		ok &= check(firmware.section7 == syntakt::loadSection7(section7Path), "section 7's words as loadSection7's");
	ok &= check(firmware.section3.bytes == sections.section3, "section 3 as unpacked");

	ok &= refused({0x01, 0x02, 0x03}, "a file that is not SysEx");
	ok &= refused(std::vector<uint8_t>(syx.begin(), syx.begin() + static_cast<std::ptrdiff_t>(syx.size() / 2)),
		"half of the file");
	// A data byte of the 2000th message, its low bit flipped: about 200 KB into the container, inside section 3's
	// packed stream (0xfd60 to 0x16ff60)
	auto changed = syx;
	size_t message = 0;
	for(int n = 0; n < 2000 && message < syx.size(); ++n)
		while(++message < syx.size() && syx[message] != 0xf0) {}
	changed[message + 1 + 9 + 3] ^= 1;
	ok &= refused(changed, "a byte changed inside section 3");

	std::printf(ok ? "PASS\n" : "FAIL\n");
	return ok ? 0 : 1;
}
