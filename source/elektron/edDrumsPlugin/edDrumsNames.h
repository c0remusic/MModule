#pragma once

#include "edDrumsEngine.h"

// What the interface calls the machines, their families and their controls (ticket 27 of .scratch/elektron-drums-v1,
// the table approved in noms-proposition.md): house names in English that say what a machine sounds like or how it
// makes its sound, and what a control does to it, never a brand's or a machine's of origin. The engine and the files
// keep their own ids: a state, a Kit or a Track sound stores a machine's index and its values, never these names.
namespace elektron::drums::names
{
	// _machine's name, 10 characters at most, unique in the list; nullptr for a machine the list does not offer
	const char* machine(const Machine& _machine);

	// The family _machine belongs to, 12 characters at most; nullptr for Off
	const char* family(const Machine& _machine);

	// What the editor names _machine's Machine _index + 1 (_index 0-23), 12 characters at most; nullptr for one it does
	// not use. The host keeps "T01 Machine 1" to "T01 Machine 24", which cannot follow the machine chosen
	const char* control(const Machine& _machine, int _index);
}
