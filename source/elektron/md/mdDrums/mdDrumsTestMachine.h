// The tests' own User Machine (the User Machines map, .scratch/user-machines/): a sawtooth that decays, written in
// DSP56300 assembly and packaged as MDX X.20 packages are, since no one else's may be carried. For mdDrumsEngineTest and
// mdDrumsProcessorTest.
#pragma once

#include "MdsPackage.h"

#include "dsp56kEmu/assembler.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace mdDrums::test
{
	// A User Machine of the test's own, written here in DSP56300 assembly since no one else's may be carried
	// (.scratch/user-machines/map.md): a sawtooth that decays, PTCH (raw 14-bit) its increment, DEC (decay, OS 1.63's
	// table) its envelope's factor per sample. Its loop's end is an address in its own code, so the package relocates it.
	inline md::mds::Draft sawDraft()
	{
		struct Code
		{
			std::vector<uint32_t> words;
			void op(const std::string& _text)
			{
				dsp56k::Assembler as;
				const auto r = as.assemble(_text.c_str());
				if(!r.success())
					throw std::runtime_error("cannot assemble '" + _text + "'");
				words.push_back(r.word[0]);
				if(r.wordCount > 1)
					words.push_back(r.word[1]);
			}
		};
		const std::vector<std::string> loop{"add x0,b", "move b1,x1", "mpy x1,y0,a", "move a,y:(r7)+", "mpy y1,y0,a",
			"move a,y0"};
		Code body;
		for(const auto& line : loop)
			body.op(line);

		md::mds::Draft draft;
		Code code;
		draft.initOffset = 0;
		code.op("clr a");
		code.op("move a,y:(r6+$9)");	// the phase
		code.op("move a,y:(r6+$a)");	// the envelope
		code.op("rts");
		draft.trigOffset = static_cast<uint16_t>(code.words.size());
		code.op("clr a");
		code.op("move a,y:(r6+$9)");
		code.op("move #>$7fffff,x0");
		code.op("move x0,y:(r6+$a)");
		code.op("rts");
		draft.renderOffset = static_cast<uint16_t>(code.words.size());
		code.op("move y:(r6+$9),b");
		code.op("move y:(r6+$1),x0");	// PTCH
		code.op("move y:(r6+$a),y0");
		code.op("move y:(r6+$2),y1");	// DEC
		const auto end = code.words.size() + 2 + body.words.size();
		char text[32];
		std::snprintf(text, sizeof(text), "do #32,$%zx", end);
		draft.relocated.push_back(static_cast<uint16_t>(code.words.size() + 1));
		code.op(text);
		code.words.insert(code.words.end(), body.words.begin(), body.words.end());
		code.op("move b1,y:(r6+$9)");
		code.op("move y0,y:(r6+$a)");
		code.op("rts");
		draft.program = code.words;
		draft.yWords = 11;
		draft.declaredCycles = 900;
		draft.category = "TST";
		draft.code = "SW";
		draft.name = "TEST SAW";
		draft.id = "org.mmodule.test.saw";
		md::mds::Parameter ptch;
		ptch.name = "PTCH";
		ptch.defaultValue = 100;
		ptch.conversion = md::mds::Conversion::Raw14;
		md::mds::Parameter dec;
		dec.name = "DEC";
		dec.defaultValue = 40;	// about 22 ms to -60 dB
		dec.conversion = md::mds::Conversion::Decay;
		draft.parameters = {ptch, dec};
		return draft;
	}

	// A NEIGHBOUR User Machine of the test's own (ticket 22 of the User Machines map): its RENDER copies, word for word,
	// the raw block of the Track _back places before it (1-3, through the descriptor the MDS Standard puts at Y:$7F0), or
	// with 4 its own buffer as RENDER finds it, which holds the Track four places back. A copy, so that the tests compare
	// it bit for bit with its source.
	inline md::mds::Draft neighbourDraft(const int _back)
	{
		struct Code
		{
			std::vector<uint32_t> words;
			void op(const std::string& _text)
			{
				dsp56k::Assembler as;
				const auto r = as.assemble(_text.c_str());
				if(!r.success())
					throw std::runtime_error("cannot assemble '" + _text + "'");
				words.push_back(r.word[0]);
				if(r.wordCount > 1)
					words.push_back(r.word[1]);
			}
		};
		md::mds::Draft draft;
		Code code;
		draft.initOffset = 0;
		code.op("rts");
		draft.trigOffset = static_cast<uint16_t>(code.words.size());
		code.op("rts");
		draft.renderOffset = static_cast<uint16_t>(code.words.size());
		code.op("move y:>$7f0,r0");	// the descriptor
		code.op("nop");
		char text[32];
		// Word _back of the descriptor; with 4 back, word 0, its own buffer
		if(_back % 4)
		{
			std::snprintf(text, sizeof(text), "move y:(r0+$%x),r0", _back % 4);
			code.op(text);
		}
		else
		{
			code.op("move y:(r0),r0");
		}
		code.op("nop");
		Code body;
		body.op("move y:(r0)+,a");
		body.op("move a,y:(r7)+");
		const auto end = code.words.size() + 2 + body.words.size();
		std::snprintf(text, sizeof(text), "do #32,$%zx", end);
		draft.relocated.push_back(static_cast<uint16_t>(code.words.size() + 1));
		code.op(text);
		code.words.insert(code.words.end(), body.words.begin(), body.words.end());
		code.op("rts");
		draft.program = code.words;
		draft.yWords = 11;
		draft.declaredCycles = 200;
		draft.category = "TST";
		draft.code = "N" + std::to_string(_back);
		draft.name = "TEST NEIGHBOUR " + std::to_string(_back);
		draft.id = "org.mmodule.test.neighbour" + std::to_string(_back);
		draft.flags = md::mds::FlagNeighbour;
		return draft;
	}

	// The package's bytes, with another default for PTCH, so that two packages differ
	inline std::vector<uint8_t> sawPackage(const uint8_t _pitch = 100)
	{
		auto draft = sawDraft();
		draft.parameters[0].defaultValue = _pitch;
		return md::mds::buildPackage(draft);
	}
}
