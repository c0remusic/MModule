#include "edDrumsNames.h"

#include <algorithm>

#include "edDrumsVintage.h"
#include "sophieMachine.h"

namespace elektron::drums::names
{
	namespace
	{
		using Eight = std::array<const char*, 8>;

		// A machine of the Machinedrum's family by its id: its name and its Machine 1-8, "" for one it does not use
		struct MdNames
		{
			uint8_t id;
			const char* name;
			Eight controls;
		};

		// The sample players but the kick: Machine 4 differs, the rest is theirs alike
		constexpr Eight lofi(const char* _fourth)
		{
			return {"TUNE", "DECAY", "HPF", _fourth, "START", "REPEATS", "REPEAT GAP", "BEND"};
		}
		constexpr Eight Sweep{"TUNE", "DECAY", "SWEEP", "SWEEP TIME", "DAMPING", "DRIVE", "DRIVE TYPE", ""};
		constexpr Eight Hat{"TAIL", "DECAY", "HPF", "LPF", "METAL", "", "", ""};
		constexpr Eight Metal{"TUNE", "DECAY", "HARDNESS", "RING", "BODY", "TONE", "BRIGHTNESS", "LENGTH"};

		const std::array<MdNames, 51> mdNames{{
			// BASIC: the bare generators
			{0, "SILENCE", {"", "", "", "", "", "", "", ""}},
			{1, "SINE", {"TUNE", "DECAY", "SWEEP", "SWEEP TIME", "", "", "", ""}},
			{2, "NOISE", {"DECAY", "", "", "", "", "", "", ""}},
			{3, "IMPULSE", {"CLICK RISE", "CLICK PEAK", "CLICK FALL", "CLICK END", "", "", "", ""}},
			// CIRCUIT: a drum machine's analogue circuits, modelled
			{16, "ROUND KICK", {"TUNE", "DECAY", "SWEEP", "SWEEP TIME", "CLICK", "NOISE", "HARMONICS", "CLIP"}},
			{17, "ANA SNARE", {"TUNE", "DECAY", "PITCH BUMP", "BUMP DECAY", "SNARES", "TONE", "TUNE 2", "CLIP"}},
			{18, "ANA TOM", Sweep},
			{19, "ROOM CLAP", {"CLAPS", "TONE", "HARDNESS", "DENSITY", "CLAP GAP", "ROOM", "ROOM SIZE", "ROOM TUNE"}},
			{20, "ANA RIM", {"TUNE", "DECAY", "DRIVE", "", "", "", "", ""}},
			{21, "COWBELL", {"TUNE", "DECAY", "PRESENCE", "DAMPING", "TONE", "PITCH BUMP", "", ""}},
			{22, "TIGHT HAT", Hat},
			{23, "OPEN HAT", Hat},
			{24, "ANA CYMBAL", {"TUNE", "DECAY", "TOP LEVEL", "TOP TUNE", "SIZE", "RESO", "", ""}},
			{25, "MARACAS", {"ATTACK", "SUSTAIN", "REVERSE", "DAMPING", "RATTLE", "RATTLE TYPE", "TONE", "HARDNESS"}},
			{26, "CLAVES", {"TUNE", "DECAY", "2ND TONE", "ENHANCE", "2ND TUNE", "CLICK", "", ""}},
			{27, "ANA TOM 2", Sweep},
			{28, "DIRTY KICK", {"TUNE", "DECAY", "SWEEP", "HOLD", "CLICK", "NOISE", "GRIT", "DRIVE"}},
			{29, "SINE SNARE", {"TUNE", "DECAY", "NOISE", "NOISE DECAY", "POWER", "BODY TUNE", "NOISE TUNE", "NOISE TYPE"}},
			// FM PERC: two-operator FM
			{32, "FM KICK", {"TUNE", "DECAY", "SWEEP", "SWEEP TIME", "FM AMOUNT", "FM FREQ", "FM DECAY", "FM FDBK"}},
			{33, "FM SNARE", {"TUNE", "DECAY", "NOISE", "NOISE DECAY", "FM AMOUNT", "FM FREQ", "FM DECAY", "HPF"}},
			{34, "FM TOM", {"TUNE", "DECAY", "SWEEP", "SWEEP TIME", "FM AMOUNT", "FM FREQ", "FM DECAY", "CLICK"}},
			{35, "FM CLAP", {"TUNE", "DECAY", "CLAPS", "CLAP DECAY", "FM AMOUNT", "FM FREQ", "FM DECAY", "HPF"}},
			{36, "FM RIM", {"TUNE", "DECAY", "FM AMOUNT", "HPF", "SNARE", "SNARE TUNE", "SNARE DECAY", "SNARE FM"}},
			{37, "FM COWBELL", {"TUNE", "DECAY", "CHOKE", "FM FDBK", "FM AMOUNT", "FM FREQ", "FM DECAY", ""}},
			{38, "FM HAT", {"TUNE", "DECAY", "TREMOLO", "TREM RATE", "FM AMOUNT", "FM FREQ", "FM DECAY", "FM FDBK"}},
			{39, "FM CYMBAL", {"TUNE", "DECAY", "FM FDBK", "HPF", "FM AMOUNT", "FM FREQ", "FM DECAY", ""}},
			// CRUNCH 12: 12-bit samples
			{48, "LOFI KICK", {"TUNE", "DECAY", "CLICK", "CLICK DECAY", "START", "REPEATS", "REPEAT GAP", "BEND"}},
			{49, "LOFI SNARE", lofi("RING")},
			{50, "LOFI HITOM", lofi("HPF RESO")},
			{51, "LOFI LOTOM", lofi("HPF RESO")},
			{52, "LOFI CLAP", lofi("HPF RESO")},
			{53, "LOFI RIM", lofi("RATTLE")},
			{54, "LOFI COW", lofi("HPF RESO")},
			{55, "LOFI HAT", lofi("HPF RESO")},
			{56, "LOFI OPEN", lofi("LENGTH")},
			{57, "LOFI RIDE", lofi("BELL")},
			{58, "LOFI CRASH", lofi("HPF RESO")},
			{59, "LOFI BRUSH", lofi("GAIN")},
			{60, "LOFI TAMB", lofi("HPF RESO")},
			{61, "LOFI TRI", lofi("HPF RESO")},
			{62, "LOFI SHAKE", lofi("SLEW")},
			{63, "LOFI BONGO", lofi("HPF 2")},
			// PHYSICAL: skins, wood and metal struck
			{64, "REAL KICK", {"TUNE", "DECAY", "HARDNESS", "STRIKE", "TENSION", "DAMPING", "", ""}},
			{65, "REAL SNARE", {"TUNE", "DECAY", "HARDNESS", "RING", "TENSION", "RING LEVEL", "RING DECAY", ""}},
			{66, "REAL TOM", {"TUNE", "DECAY", "HARDNESS", "STRIKE", "TUNE 2", "DAMPING", "SIZE", "POSITION"}},
			{67, "MALLET", {"TUNE", "DECAY", "HARDNESS", "TENSION", "", "", "", ""}},
			{68, "SHAKER", {"GRAINS", "DECAY", "GRAIN TIME", "", "SIZE", "HARDNESS", "", ""}},
			{69, "REAL RIM", {"TUNE", "DECAY", "HARDNESS", "RING", "RING LEVEL", "RING DECAY", "", ""}},
			{70, "REAL RIDE", Metal},
			{71, "REAL CRASH", Metal},
			{72, "REAL HAT", {"TUNE", "DECAY", "CLOSED", "RING", "BODY", "TONE", "HPF", "LENGTH"}},
		}};

		const MdNames* mdOf(const uint8_t _id)
		{
			const auto found = std::find_if(mdNames.begin(), mdNames.end(), [_id](const MdNames& _n) { return _n.id == _id; });
			return found == mdNames.end() ? nullptr : &*found;
		}

		// MODERN DIGI, in syntaktMachineNames' order
		constexpr std::array<const char*, SyntaktMachines> syntaktNames{"PUNCH KICK", "DIGI SNARE", "SHINY CYM",
			"TONAL PERC", "OLD SNARE", "OLD CLAP", "TOY SYNTH", "BIT SYNTH", "DUO SAMPLE"};
		// Machine 1-8 in the SYN page's order (Engine::syntaktSlotOf); Machine 9 is HOLD, which the amp holds the voice
		// with and no page shows, kept for a set from before
		const std::array<Eight, SyntaktMachines> syntaktControls{{
			{"TUNE", "SWEEP", "PUNCH", "DECAY", "SQUARE MOD", "TRI MOD", "MOD ENV", "DRIVE"},
			{"TUNE", "SWEEP", "PUNCH", "DECAY", "INHARMONIC", "FM FDBK", "FM ENV", "DRIVE"},
			{"TUNE", "SHIMMER", "PUNCH", "DECAY", "RADIO", "FM AMOUNT", "FM ENV", "DRIVE"},
			{"TUNE", "SWEEP", "PUNCH", "DECAY", "INHARMONIC", "FM AMOUNT", "FM ENV", "DRIVE"},
			{"TUNE", "SWEEP", "PUNCH", "DECAY", "INHARMONIC", "COMPLEX", "FM ENV", "DRIVE"},
			{"TUNE", "CLAP GAP", "PUNCH", "DECAY", "BALANCE", "BODY", "BODY DECAY", "DRIVE"},
			{"TUNE", "TUNE 2", "PUNCH", "DECAY", "LPF", "BRIGHTNESS", "PART DECAY", "DRIVE"},
			{"TUNE", "DETUNE", "BALANCE", "DECAY", "WAVE", "DOWNSAMPLE", "BIT CRUSH", "DRIVE"},
			{"TUNE 1", "SAMPLE 1", "BAND 1", "DECAY 1", "TUNE 2", "SAMPLE 2", "BLEND", "DRIVE"},
		}};
		constexpr int SyntaktHold = 8;

		// The ROM player's eight, then the grain's four (edDrumsVintage.h)
		constexpr std::array<const char*, vintage::SamplerParameters> samplerControls{"TUNE", "DECAY", "HOLD", "BIT CRUSH",
			"START", "END", "REPEATS", "REPEAT GAP", "GRAIN", "BITS", "COMPANDING", "OUTPUT LPF"};

		// VA DRUMS, by id (edDrumsEngine.h, OpenDrumMachines), their controls in openDrum.c's
		constexpr std::array<const char*, OpenDrumMachines> openDrumNames{"VA DRUM", "VA SNARE", "VA CYMBAL", "VA HIHAT"};
		const std::array<std::array<const char*, MachineParameters>, OpenDrumMachines> openDrumControls{{
			{"TUNE", "DECAY", "SWEEP TIME", "SWEEP", "FM AMOUNT", "FM FREQ", "CLICK", "DRIVE", "FINE TUNE", "WAVE",
				"FM WAVE", "MOD MODE", "ATTACK", "DECAY SHAPE", "SWEEP SHAPE", "PULSE WIDTH", "CUTOFF", "RESO",
				"FILTER TYPE", "FILTER DRIVE", "CLICK WAVE", "CLICK TUNE"},
			{"TUNE", "DECAY", "SWEEP TIME", "SWEEP", "NOISE TUNE", "NOISE MIX", "CUTOFF", "DRIVE", "FINE TUNE", "WAVE",
				"ATTACK", "DECAY SHAPE", "REPEATS", "SWEEP SHAPE", "PULSE WIDTH", "RESO", "FILTER TYPE", "FILTER DRIVE",
				"CLICK", "CLICK WAVE", "CLICK TUNE"},
			{"TUNE", "DECAY", "FM1 FREQ", "FM2 FREQ", "FM1 AMOUNT", "FM2 AMOUNT", "CUTOFF", "DRIVE", "FINE TUNE", "WAVE",
				"FM1 WAVE", "FM2 WAVE", "ATTACK", "DECAY SHAPE", "REPEATS", "PULSE WIDTH", "RESO", "FILTER TYPE",
				"FILTER DRIVE", "CLICK", "CLICK WAVE", "CLICK TUNE"},
			{"TUNE", "CLOSED DECAY", "OPEN DECAY", "FM1 FREQ", "FM1 AMOUNT", "CUTOFF", "DRIVE", "OPEN", "FINE TUNE", "WAVE",
				"FM1 WAVE", "FM2 WAVE", "FM2 FREQ", "FM2 AMOUNT", "ATTACK", "DECAY SHAPE", "RESO", "FILTER TYPE",
				"FILTER DRIVE", "CLICK", "CLICK WAVE", "CLICK TUNE", "PULSE WIDTH"},
		}};

		// HYPER FM, in sophie::MachineNames' order
		constexpr std::array<const char*, sophie::MachineSlots> sophieControls{"MODEL", "TUNE", "COLOR", "METAL",
			"FM FDBK", "SWEEP", "DECAY", "BIT CRUSH", "DRIVE", "CUTOFF", "RESO", "FILTER TYPE", "COMB TIME", "COMB FDBK",
			"COMB MIX", "COMB TONE", "METAL DECAY", "FDBK MODE", "TAIL MODE", "KICK BODY"};

		const char* named(const char* _name)
		{
			return _name && *_name ? _name : nullptr;
		}
	}

	const char* machine(const Machine& _machine)
	{
		if(!playable(_machine))
			return nullptr;
		switch(_machine.family)
		{
		case Family::Off:			return "OFF";
		case Family::Syntakt:		return syntaktNames[_machine.id];
		case Family::OpenDrum:		return openDrumNames[_machine.id];
		case Family::Sophie:		return "HYPER FM";
		case Family::Machinedrum:
			if(_machine == Sampler)
				return "SAMPLER";
			if(const auto* n = mdOf(_machine.id))
				return n->name;
			return nullptr;
		}
		return nullptr;
	}

	const char* family(const Machine& _machine)
	{
		switch(_machine.family)
		{
		case Family::Off:			return nullptr;
		case Family::Syntakt:		return "MODERN DIGI";
		case Family::OpenDrum:		return "VA DRUMS";
		case Family::Sophie:		return "HYPER FM";
		case Family::Machinedrum:
			if(_machine == Sampler)
				return "SAMPLER";
			if(_machine.id < 16)
				return "BASIC";
			if(_machine.id < 32)
				return "CIRCUIT";
			if(_machine.id < 48)
				return "FM PERC";
			if(_machine.id < 64)
				return "CRUNCH 12";
			if(_machine.id < 80)
				return "PHYSICAL";
			return nullptr;
		}
		return nullptr;
	}

	const char* control(const Machine& _machine, const int _index)
	{
		if(!playable(_machine) || _index < 0 || _index >= MachineParameters)
			return nullptr;
		const auto i = static_cast<size_t>(_index);
		switch(_machine.family)
		{
		case Family::Off:			return nullptr;
		case Family::Syntakt:		return _index < 8 ? syntaktControls[_machine.id][i] : _index == SyntaktHold ? "HOLD" : nullptr;
		case Family::OpenDrum:		return _index < machineParameters(_machine) ? named(openDrumControls[_machine.id][i]) : nullptr;
		case Family::Sophie:		return _index < sophie::MachineSlots ? sophieControls[i] : nullptr;
		case Family::Machinedrum:
			if(_machine == Sampler)
				return _index < vintage::SamplerParameters ? samplerControls[i] : nullptr;
			if(const auto* n = mdOf(_machine.id); n && _index < 8)
				return named(n->controls[i]);
			return nullptr;
		}
		return nullptr;
	}
}
