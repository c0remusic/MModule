#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "stDrumsFirmware.h"
#include "stSyntakt.h"

#include "synthLib/resampler.h"

// ST Drums' sound without JUCE: syntakt::Syntakt with 16 tracks, a trig per note, its outputs as the plug-in's
// channels, at the host's rate.
namespace stDrums
{
	constexpr int Tracks = syntakt::Syntakt::MaxTracks;
	constexpr int TrackOuts = 15;				// Track 01-15; Track 16 only in Main: Live opens 16 buses
	constexpr int Channels = 2 + TrackOuts;		// Main L and R, then Track 01-15
	constexpr int Rate = syntakt::Syntakt::SampleRate;

	// The twelve machines the digital tracks play, by the firmware's machine id
	constexpr std::array<int, 12> Machines{0, 1, 2, 3, 4, 5, 6, 7, 36, 37, 38, 44};

	// The index of machine id _machine in Machines; -1 for another
	int machineIndex(int _machine);

	// The plug-in's channels from the module's outputs at sample _s: Main the sum of A's and B's direct and FX
	// buses (the returns are on A's, as the routing parameters send them), each Track its stem; Q31 to float
	float mainSample(const int32_t* const* _outputs, size_t _s, int _side);
	inline float stemSample(const int32_t* const* _outputs, const size_t _s, const int _track)
	{
		const int c = _track < syntakt::Syntakt::Tracks ? syntakt::Syntakt::Stem1 + _track
			: syntakt::Syntakt::Stem9 + _track - syntakt::Syntakt::Tracks;
		return static_cast<float>(static_cast<double>(_outputs[c][_s]) * (1.0 / 2147483648.0));
	}

	// The module and the firmware it reads, made on the message thread, then played on the audio thread alone
	class Engine
	{
	public:
		explicit Engine(std::shared_ptr<const Firmware> _firmware);

		syntakt::Syntakt& module() { return *m_module; }
		const syntakt::Syntakt& module() const { return *m_module; }

		// A value of the plug-in (0-127, the word's top byte) into the module, held to its descriptor's range
		uint16_t trackWord(int _machineIndex, int _id, int _value) const;
		uint16_t fxWord(int _id, int _value) const;

	private:
		std::shared_ptr<const Firmware> m_firmware;
		std::unique_ptr<syntakt::Syntakt> m_module;
		using Ranges = std::array<syntakt::Syntakt::Range, syntakt::Syntakt::TrackParameterEnd>;
		std::array<Ranges, Machines.size()> m_ranges{};
		std::array<syntakt::Syntakt::Range, syntakt::Syntakt::FxParameterEnd> m_fxRanges{};
	};

	// The value _word holds as the plug-in shows it (its top byte, rounded)
	int valueOf(uint16_t _word);

	// Plays an Engine at the host's rate: notes as trigs at the source sample they fall on, the module's outputs as
	// Channels, resampled from 48 kHz by the stack's resampler (MameHq, fixed latency) when the host runs at another
	// rate. Nothing allocates, locks or reads a file in noteOn, noteOff or render.
	class Player
	{
	public:
		static constexpr int MaxEvents = 1024;

		// Every buffer for blocks of up to _maxBlock samples at _hostRate; not while render runs. _used: the Channels
		// render writes (Main always); the others it leaves as they are, and resamples nothing for them
		void prepare(double _hostRate, int _maxBlock, const std::array<bool, Channels>& _used = allChannels());

		bool uses(const int _channel) const
		{
			for(int k = 0; k < m_usedCount; ++k)
				if(m_used[static_cast<size_t>(k)] == _channel)
					return true;
			return false;
		}

		static std::array<bool, Channels> allChannels()
		{
			std::array<bool, Channels> all{};
			all.fill(true);
			return all;
		}

		// The host samples from a note to its sound that resampling adds (0 at 48 kHz)
		int latency() const { return m_latency; }

		// The engine render plays from now; none plays silence
		void setEngine(Engine* _engine) { m_engine = _engine; }
		Engine* engine() const { return m_engine; }

		// A trig of track _track (0-15) at sample _offset of the next render, with note _note and _velocity (1-127);
		// noteOff ends its gate
		void noteOn(int _offset, int _track, int _note, int _velocity);
		void noteOff(int _offset, int _track);

		// _count host samples of each Channel used into _outputs[0] to _outputs[Channels - 1] (null for the others);
		// _count up to prepare's
		void render(float* const* _outputs, int _count);

		// The source samples (48 kHz) rendered so far
		uint64_t sourcePosition() const { return m_source; }

	private:
		struct Event
		{
			uint64_t at;	// the source sample
			int track;
			int note;		// -1: a note off
			int velocity;
		};

		void addEvent(int _offset, int _track, int _note, int _velocity);
		void renderSource(synthLib::TAudioOutputs& _outputs, uint32_t _count);
		void applyEvents(uint64_t _until);

		std::unique_ptr<synthLib::Resampler> m_resampler;
		std::array<int, Channels> m_used{};		// the Channels render writes, in order
		int m_usedCount = 0;
		double m_hostRate = Rate;
		int m_latency = 0;
		Engine* m_engine = nullptr;
		uint64_t m_host = 0;		// host samples rendered
		uint64_t m_source = 0;		// source samples rendered
		std::array<Event, MaxEvents> m_events{};
		int m_eventCount = 0;
		int m_eventNext = 0;

		static constexpr int Chunk = 256;
		std::vector<int32_t> m_q31;		// the module's outputs for a chunk, Outputs16 channels of Chunk
		std::array<int32_t*, syntakt::Syntakt::Outputs16> m_q31Channels{};
	};
}
