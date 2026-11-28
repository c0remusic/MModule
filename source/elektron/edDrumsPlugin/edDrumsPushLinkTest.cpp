// The Push companion's link without Lockstep (ticket 22 of .scratch/elektron-drums-v2): the OSC that Max's udpsend and
// udpreceive speak, encoded and decoded as their bytes are (an 'f' rounded, anything else refused), the hello's notes
// for every port, and a link that two ends on 127.0.0.1 exchange messages through. The Processor's side, two
// instances and the sequencer, is edDrumsProcessorTest's.

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>

#include "edDrumsPushLink.h"

namespace
{
	using namespace elektron::drums;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	void codec()
	{
		// "/lockstep/trig" and ",iiii", each padded to four with a NUL at least, then four big-endian words
		const auto bytes = osc::encode({"/lockstep/trig", {1, 2, -3, 127}});
		require(bytes.size() == 16 + 8 + 16, "an encoded message's size, " + std::to_string(bytes.size()));
		require(bytes[14] == 0 && bytes[15] == 0 && bytes[16] == ',' && bytes[21] == 0, "the strings' padding");
		require(bytes[24 + 8] == 0xff && bytes[24 + 11] == 0xfd, "a negative word");
		const auto back = osc::decode(bytes.data(), bytes.size());
		require(back && back->address == "/lockstep/trig" && back->args == std::vector<int>{1, 2, -3, 127}, "decoded");

		// Max sends a float box's value as 'f': rounded; a message with no tags carries nothing
		std::vector<uint8_t> f{'/', 'x', 0, 0, ',', 'f', 0, 0, 0x40, 0x20, 0, 0};	// 2.5f
		const auto rounded = osc::decode(f.data(), f.size());
		require(rounded && rounded->args == std::vector<int>{3}, "an 'f' rounded");
		const std::vector<uint8_t> bare{'/', 'y', 0, 0};
		const auto none = osc::decode(bare.data(), bare.size());
		require(none && none->address == "/y" && none->args.empty(), "a message without tags");

		// Refused: a string argument, a short word, no '/', no NUL, a length not of four, a bundle
		const std::vector<std::vector<uint8_t>> bad{
			{'/', 'x', 0, 0, ',', 's', 0, 0, 'a', 0, 0, 0},
			{'/', 'x', 0, 0, ',', 'i', 0, 0, 0, 0},
			{'/', 'x', 0, 0, ',', 'i', 0, 0},
			{'x', 0, 0, 0},
			{'/', 'x', 'y', 'z'},
			{'#', 'b', 'u', 'n', 'd', 'l', 'e', 0},
		};
		for(const auto& b : bad)
			require(!osc::decode(b.data(), b.size()), "a malformed message decoded");
		std::printf("  OSC: integers both ways, 'f' rounded, strings, short words and bundles refused\n");
	}

	void hello()
	{
		for(const int port : {1, 1024, 49152, 54321, 65535})
		{
			PushHello h;
			const auto v = PushHello::velocities(port);
			for(const int x : v)
				require(x >= 1 && x <= 127, "a hello's velocity out of 1-127");
			int got = 0;
			for(int i = 0; i < 4; ++i)
				got = h.note(PushHello::FirstNote + i, v[static_cast<size_t>(i)]);
			require(got == port, "the hello of port " + std::to_string(port) + " read as " + std::to_string(got));
		}
		// Out of order, without the mark, or broken off by a new mark: nothing, and the next whole hello reads
		PushHello h;
		const auto v = PushHello::velocities(50000);
		require(h.note(125, v[1]) == 0 && h.note(126, v[2]) == 0 && h.note(127, v[3]) == 0, "a hello without its mark");
		require(h.note(124, 75) == 0 && h.note(125, v[1]) == 0 && h.note(126, v[2]) == 0 && h.note(127, v[3]) == 0,
			"a hello with another mark");
		require(h.note(124, v[0]) == 0 && h.note(125, v[1]) == 0 && h.note(124, v[0]) == 0 && h.note(125, v[1]) == 0
			&& h.note(126, v[2]) == 0 && h.note(127, v[3]) == 50000, "a hello begun again");
		require(h.note(124, v[0]) == 0 && h.note(126, v[2]) == 0 && h.note(127, v[3]) == 0, "a hello missing a note");
		std::printf("  hello: every port through velocities 1-127, broken hellos dropped\n");
	}

	void link()
	{
		PushLink a, b;
		require(a.port() > 0 && b.port() > 0 && a.port() != b.port(), "two links, two ports");
		require(!a.send({"/x", {1}}), "a send before the companion is known");
		a.setCompanionPort(b.port());
		b.setCompanionPort(a.port());
		for(int i = 0; i < 100; ++i)
			require(a.send({"/n", {i}}), "a send");
		require(b.send({"/back", {7, 8}}), "a send back");
		std::vector<osc::Message> atB, atA;
		for(int wait = 0; wait < 200 && (atB.size() < 100 || atA.empty()); ++wait)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			for(auto& m : b.take())
				atB.push_back(m);
			for(auto& m : a.take())
				atA.push_back(m);
		}
		require(atB.size() == 100, "the messages received, " + std::to_string(atB.size()));
		for(int i = 0; i < 100; ++i)
			require(atB[static_cast<size_t>(i)].args == std::vector<int>{i}, "a message out of order");
		require(atA.size() == 1 && atA[0].address == "/back", "the answer");
		std::printf("  link: ports %d and %d, 100 messages in order one way, one back\n", a.port(), b.port());
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		codec();
		hello();
		link();
	}
	catch(const std::exception& _e)
	{
		std::printf("FAIL: %s\n", _e.what());
		return 1;
	}
	std::printf("edDrumsPushLinkTest: ok\n");
	return 0;
}
