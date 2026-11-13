#pragma once

#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "edDrumsEngine.h"
#include "mdProtocol/mdkit.h"

// What the LOCKER's walk takes of the original machines' own files (ticket 20 of .scratch/elektron-drums-v1, spec
// section 8). A SysEx file's Kit dumps, which mdProtocol reads as OS 1.63 does (mdkit.h), become Kits of the Bank:
// each Track's machine where Lockstep has it (ROM-01 to -32 the Sampler on that factory slot), SYN1-8, the level and
// pan that give the machine's left and right gains (its LEV, VOL and PAN, whose laws differ from Lockstep's), the two
// sends, the chain the Machinedrum's family brings with EQF, EQG, FLTF, FLTW, FLTQ, SRR and DIST in it (the machine's
// own stages, which take the Kit's values as they are), the Chokes, and the Echo's and the Gate Box's bytes. What a
// file holds that Lockstep does not take is said in a line each, as a user reads it: a machine it lacks, AM, an LFO, a
// Link, the master EQ and DYNAMIX, patterns, songs,
// samples sent by sample dump, and the projects and sounds whose layout nothing here knows.
namespace elektron::drums::machineFiles
{
	struct FoundKit
	{
		juce::String name;		// the Kit's, else its file's with its place in it
		md::automation::sysex::MdKit kit;
	};

	// The most a SysEx file read here holds: a whole backup of Kits, patterns and songs is under a megabyte
	constexpr int64_t MostBytes = 8 << 20;

	// _file, a SysEx file, read on the walk's thread: its Kit dumps added to _kits, a line in _notTaken for each kind of
	// message it holds that Lockstep does not take. False when it gave no Kit
	bool readSysex(const juce::File& _file, std::vector<FoundKit>& _kits, juce::StringArray& _notTaken);

	// The Kit, as KitBank keeps one, that plays _found on Lockstep, _machines Machine's choices. A line in _notTaken for
	// each thing of it that Lockstep lacks; a Track whose machine it lacks is left Off
	std::unique_ptr<juce::XmlElement> kitOf(const FoundKit& _found, const std::vector<Machine>& _machines,
		juce::StringArray& _notTaken);
}
