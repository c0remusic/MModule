// A User Machine as MDX X.20 installs it: the MDS1 package of jmamma/MDS (docs/MDX_MachineDumpStandard.txt),
// read from its .syx dump (docs/MDX_MachineDumpProtocol.txt). Parsing and checks only; nothing here runs the code.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace md::mds
{
	// How the host turns a 0-127 setting into the word the DSP reads at Y:(R6+1..8) (low four bits of the byte)
	enum class Conversion : uint8_t
	{
		Raw14 = 0,
		UnipolarQ23 = 1,
		SquaredQ23 = 2,
		BipolarQ23 = 3,
		Pitch = 4,
		Boolean = 5,
		Index = 6,
		Decay = 7
	};

	struct Parameter
	{
		std::string name;
		uint8_t defaultValue = 0;
		uint8_t min = 0;
		uint8_t max = 127;
		uint8_t style = 0;	// the byte's upper four bits: continuous, bipolar, percent, ...
		Conversion conversion = Conversion::Raw14;
	};

	struct Import
	{
		uint16_t word = 0;	// program word to patch
		uint8_t kind = 0;
		uint8_t symbol = 0;
	};

	// MDM1 flags
	constexpr uint8_t FlagTonal = 0x01;
	constexpr uint8_t FlagContinuousRetrigger = 0x04;
	constexpr uint8_t FlagNeighbour = 0x08;
	constexpr uint8_t FlagStereoPair = 0x10;

	struct Package
	{
		std::vector<uint8_t> bytes;	// the whole package, header included

		uint16_t xWords = 0;
		uint16_t yWords = 0;
		uint8_t versionMajor = 0;
		uint8_t versionMinor = 0;
		uint16_t initOffset = 0;
		uint16_t trigOffset = 0;
		uint16_t renderOffset = 0;
		uint16_t declaredCycles = 0;

		std::vector<uint32_t> program;	// 24-bit words, as stored (offsets not yet relocated)
		std::vector<bool> relocate;		// one per program word
		std::vector<Import> imports;

		uint8_t flags = 0;
		uint8_t linkMask = 0;
		std::string category;
		std::string code;
		std::string name;
		std::string id;
		std::vector<Parameter> parameters;
		std::array<uint32_t, 32> sprite{};

		std::string displayName() const { return category + "-" + code; }
	};

	// Throws std::runtime_error with what is wrong
	Package parsePackage(const std::vector<uint8_t>& _bytes);
	// The package a $74 dump carries; throws when the dump is incomplete or its checksums fail
	std::vector<uint8_t> packageFromSysex(const std::vector<uint8_t>& _sysex);

	// The word the host writes at Y:(R6+1+i) for a setting, as the documents give the conversions. Pitch and Decay
	// are not specified there (X.20 computes them): these two are guesses until X.20 can be compared
	uint32_t convert(const Parameter& _parameter, int _setting);
	// The same for a setting between two steps, as the smoothing and the LFOs give it: _value is the setting << 7
	// (0 to $3fff), and the documents allow the DSP word to fall between two settings' words
	uint32_t convertFine(const Parameter& _parameter, uint16_t _value);

	// A package from its parts, as an author's tools would make it: for tests, which carry no User Machine of anyone
	// else. The content ID is left zero (parsePackage does not check it); the CRCs are right.
	struct Draft
	{
		std::vector<uint32_t> program;		// 24-bit words, addresses relative to the program's start
		std::vector<uint16_t> relocated;	// words that hold a program address
		std::vector<Import> imports;
		uint16_t initOffset = 0, trigOffset = 0, renderOffset = 0;
		uint16_t xWords = 0, yWords = 9;
		uint16_t declaredCycles = 1000;
		uint8_t flags = 0;
		std::string category, code, name, id;
		std::vector<Parameter> parameters;
	};
	std::vector<uint8_t> buildPackage(const Draft& _draft);
}
