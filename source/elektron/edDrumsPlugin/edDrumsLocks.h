#pragma once

// The sequencer's Locks as they play, and as a Seq lane draws them: which host parameter takes a Lock, what a Track
// holds, its slides, and a lane's steps from the same rules. Without JUCE: what playing changes goes through a Sink.

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

#include "edDrumsSequencer.h"

namespace elektron::drums
{
	// What a host parameter is to a Lock of the sequencer's: whose Track, and which of its values
	enum class LockKind : uint8_t { None, Machine, Note, Level, Pan, Chain, Lfo, Send, Accent, Layer };
	struct LockTarget
	{
		int track = -1;
		LockKind kind = LockKind::None;
		int a = 0, b = 0;	// Machine 1-24's, the chain's, the send's or the layer's index; the LFO and its value's
	};

	// _parameter's Lock at _track's step _step in _p, nullptr for none
	const Lock* lockAt(const Pattern& _p, int _track, int _step, int _parameter);

	class Locks
	{
	public:
		static constexpr int Tracks = 16;
		// A slide moves what its step locks in a line, from its Locks to the next trig's, or to the host's value where
		// that trig locks none, over the time to that trig, a step every SlideStep samples from the slide's Hit; the
		// Track's next Hit or Lock, host note or step, ends it
		static constexpr int SlideStep = 32;
		static constexpr int64_t NoSlide = std::numeric_limits<int64_t>::max();

		// What playing a Lock does to the instrument. Called on the audio thread
		class Sink
		{
		public:
			virtual ~Sink() = default;
			// The parameter's host value, plain
			virtual float host(int _parameter) const = 0;
			// A value within the parameter's range, on its steps
			virtual float snap(int _parameter, float _value) const = 0;
			// The parameter to the engine: the Lock's value while held(), else the host's
			virtual void apply(int _parameter) = 0;
			// A Hit's Lock of Machine _machine (Engine::lock), held for that Hit
			virtual void hitLock(int _track, int _machine, uint8_t _value) = 0;
			// A Hit's Lock of the Sampler's sample, by its fingerprint (Engine::lockSample), for that Hit
			virtual void hitSample(int /*_track*/, uint64_t /*_sample*/) {}
		};

		// Before any audio: every host parameter's target, by index. Each Track's Note parameter is its Note target.
		// Allocates what playing needs, so that the audio thread does not
		void setTargets(std::vector<LockTarget> _targets);
		int size() const { return static_cast<int>(m_targets.size()); }
		const LockTarget& target(const int _parameter) const { return m_targets[static_cast<size_t>(_parameter)]; }
		// Whether a Lock of _parameter on _track's steps plays: the editor offers no other
		bool lockable(int _parameter, int _track) const;

		bool held(const int _parameter) const { return m_held[static_cast<size_t>(_parameter)] != 0; }
		float value(const int _parameter) const { return m_value[static_cast<size_t>(_parameter)]; }

		// Audio thread. An event of the sequencer's at sample _at, but its Hit (the caller's noteOn) and its end
		void play(const Event& _event, int64_t _at, Sink& _sink);
		// The Track's next Hit or Lock: what the last step locked takes the host's values again
		void release(int _track, Sink& _sink);
		// No Lock or slide left, nothing given to the engine: it takes every value again by itself
		void forget(int _track);
		void forgetAll();
		// The sample of the next slide step of any Track, NoSlide for none; the steps of every Track due at _at
		int64_t nextSlideStep() const;
		void stepSlides(int64_t _at, Sink& _sink);

		// What a Seq lane of _parameter on _track draws for each of _p's 64 steps, normalised as the host parameter:
		// _base the host's value, _normal a plain value's. _locks, if given, counts the steps that lock it
		struct LaneStep
		{
			float from, to;
			bool locked;
			bool may = false;
			float would = 0;
		};
		std::vector<LaneStep> lane(const Pattern& _p, int _track, int _parameter, float _base,
			const std::function<float(float)>& _normal, int* _locks = nullptr) const;

	private:
		struct Glide
		{
			int parameter = 0;
			float from = 0, to = 0;
		};
		struct Slide
		{
			int64_t start = 0, length = 0;
			int64_t next = NoSlide;			// the sample of its next step
			std::vector<Glide> glides;		// reserved: no allocation on the audio thread
		};

		void hold(int _track, int _parameter);
		void endSlide(int _track);
		void stepSlide(int _track, Sink& _sink);

		std::vector<LockTarget> m_targets;
		std::array<int, Tracks> m_note{};				// each Track's Note parameter, -1 for none
		std::vector<uint8_t> m_held;
		std::vector<float> m_value;
		std::array<std::vector<int>, Tracks> m_heldBy;	// reserved: no allocation on the audio thread
		std::array<Slide, Tracks> m_slides;
	};
}
