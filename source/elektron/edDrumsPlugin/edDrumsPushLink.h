#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// The Push companion's link (ticket 22 of .scratch/elektron-drums-v2): a Max for Live device in front of Lockstep on
// its track says hello by notes 124-127, giving the UDP port it listens on; Lockstep answers from a port of its own, which
// the system chooses, so that two instances and their companions never read each other's. Both speak OSC on 127.0.0.1,
// what Max's udpsend and udpreceive speak, with integer arguments only.
//
// Companion to Lockstep:
//   /lockstep/trig pattern track step velocity    the step's trig at velocity 1-127, or removed at 0 (all from 0)
//   /lockstep/track track                         the Track shown, as the editor shows one (ticket 23)
//   /lockstep/mute track 0|1                      the Track muted or not, kept in the state (ticket 23)
//   /lockstep/hold pattern track step 0|1         a step held or let go: while one is held, a relay the host turns
//                                                 locks the steps held of its Track, the Kit untouched (ticket 24)
// Lockstep to companion:
//   /lockstep/hello port                          Lockstep's port, at each hello
//   /lockstep/view pattern track length v1..v16   the pattern playing, the Track shown, its length and its first 16
//                                                 steps' velocities (0 for none), when one of them changes
//   /lockstep/play step                           the step playing among the 16, -1 stopped or past them
//   /lockstep/tracks s1..s16                      each Track: 2 muted, 1 with trigs in the pattern playing, else 0
//   /lockstep/locked pattern track step           a step held given a Lock
namespace elektron::drums
{
	namespace osc
	{
		struct Message
		{
			std::string address;
			std::vector<int> args;		// 'i', and 'f' rounded; a message with another type is refused
		};

		std::vector<uint8_t> encode(const Message& _message);
		std::optional<Message> decode(const uint8_t* _data, size_t _size);
	}

	// Notes 124-127 at velocities 1-127: 124 at HelloMark opens a hello, 125-127 give the port six bits each, the
	// highest first, as velocity 1 + bits (a velocity 0 would read as a note-off)
	class PushHello
	{
	public:
		static constexpr int FirstNote = 124, HelloMark = 76;
		static std::array<int, 4> velocities(int _port);
		// Audio thread: a note-on of 124-127; the port once 127 completes a hello, else 0
		int note(int _note, int _velocity);

	private:
		int m_port = 0;
		int m_have = 0;		// notes of the hello received, in order
	};

	class PushLink final : juce::Thread
	{
	public:
		PushLink();
		~PushLink() override;

		// The port Lockstep listens on, 0 when no port could be had
		int port() const { return m_port; }
		// Where the companion listens
		void setCompanionPort(int _port) { m_companion = _port; }
		int companionPort() const { return m_companion; }
		bool send(const osc::Message& _message);
		// The messages received since the last call, in order
		std::vector<osc::Message> take();

	private:
		void run() override;

		juce::DatagramSocket m_socket;
		int m_port = 0;
		int m_companion = 0;
		std::mutex m_mutex;
		std::vector<osc::Message> m_received;
	};
}
