#include "stSyntakt.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <utility>

#include "stBoot.h"
#include "stEngine.h"
#include "stMainBlock.h"
#include "stMainControl.h"
#include "stMainFx.h"
#include "stMainModulation.h"
#include "stMainOutput.h"
#include "stMainPitch.h"
#include "stMemoryMap.h"

namespace syntakt
{
	namespace
	{
		using main::Memory;

		constexpr int EngineTracks = 13;		// CPU #1's: 0-7 digital, 8-11 analog, 12 the FX track
		constexpr int FxTrack = 12;

		// Section 3's parameter descriptors (505 of 52 bytes: group, internal id, min, max, default) and the default
		// machine of each user track (a long each, tracks 1-12)
		constexpr uint32_t Descriptors = 0x4022d59c;
		constexpr int DescriptorCount = 505;
		constexpr uint32_t DescriptorBytes = 52;
		constexpr uint32_t DefaultMachines = 0x4022c8d8;
		constexpr int TrackLevelDescriptor = 10;	// "Track Level", the engine copy's word t

		// The descriptor groups a track's parameters belong to, besides its machine's own (whose group is its id):
		// the amp, the filter, the glide, the two LFOs; the FX track's: the reverb, the delay, the external input,
		// the FX filter, the FX amp, the two LFOs
		constexpr int TrackGroups[] = {45, 47, 48, 59, 60};
		constexpr int FxGroups[] = {49, 50, 52, 53, 54, 59, 60};

		// Memory the OS's start leaves as it is, which the module sets for a digital track to sound as on the machine
		constexpr uint32_t KitPointer = 0x800030b8;		// the kit, 0x123 bytes per track; byte 205: velocity curve
		constexpr uint32_t KitBytes = 0x41b9c000;		// a page of the port's memory the pointer can aim at
		constexpr uint32_t LevelToStems = 0x80003145;	// non-zero: LEV scales the stems as it scales the buses
		constexpr uint32_t TrackSettings = 124;

		// Where the module gives the ported routines a trig's locks (lock: a count at +8, (id, value) from +0x14) and
		// a track's kit values (restoreLocks: a word per id from +0x14): free SRAM above everything the OS uses
		constexpr uint32_t LockList = 0x8000fc00;
		constexpr uint32_t KitSound = 0x8000fd00;
		constexpr int MaxLocks = 16;

		constexpr uint32_t engineWord(const int _t, const int _id)
		{
			return main::Engine + 2 * (main::trackWord(static_cast<uint32_t>(_t)) + static_cast<uint32_t>(_id));
		}

		uint32_t bit(const int _t) { return 1u << _t; }
	}

	struct Syntakt::State
	{
		Memory mem;
		Sram sram;
		Globals globals;
		Rom rom;
		std::array<std::array<uint16_t, TrackParameterEnd>, Tracks> kit{};	// what a lock goes back to
		std::array<std::vector<std::pair<int, uint16_t>>, Tracks> locks;		// for each track's next trig
		uint32_t trigs = 0;			// this block's
		uint32_t gateOffs = 0;		// this block's: notes whose gate ends
		uint32_t nextGateOffs = 0;	// the next block's: notes that ended in the block they started in
		uint32_t held = 0;			// notes whose gate is open
		std::array<int32_t, Voices * BlockSize> voices{};	// the voice CPU's last block, which the mix takes next
		std::array<int32_t, Outputs * BlockSize> out{};
		int position = BlockSize;	// the next sample of out to give; BlockSize: none left
		BlockTimes times;

		State(const main::Section3& _section3, const std::vector<int32_t>& _section7)
			: mem(_section3), rom{_section7.data()}
		{
			// Nothing allocates on the audio thread: the delay's and the reverb's lines and the lock lists are held
			// from the start
			mem.reserveLines();
			for(auto& l : locks)
				l.reserve(MaxLocks);
		}

		// The default of internal id _id in descriptor group _group (the first descriptor naming it); false if none
		bool descriptorDefault(const int _group, const int _id, uint16_t& _value) const
		{
			for(int i = 0; i < DescriptorCount; ++i)
			{
				const uint32_t d = Descriptors + DescriptorBytes * static_cast<uint32_t>(i);
				if(mem.word(d) != _group || mem.word(d + 4) != _id)
					continue;
				_value = static_cast<uint16_t>(mem.word(d + 16));
				return true;
			}
			return false;
		}

		uint16_t defaultOf(const int _machine, const int _t, const int _id) const
		{
			uint16_t v = 0;
			if(_t == FxTrack)
			{
				for(const int g : FxGroups)
					if(descriptorDefault(g, _id, v))
						return v;
				return 0;
			}
			if(descriptorDefault(_machine, _id, v))
				return v;
			for(const int g : TrackGroups)
				if(descriptorDefault(g, _id, v))
					return v;
			return 0;
		}

		void setKit(const int _t, const int _id, const uint16_t _value)
		{
			mem.setHalf(engineWord(_t, _id), _value);
			if(_t < Tracks)
				kit[static_cast<size_t>(_t)][static_cast<size_t>(_id)] = _value;
		}

		void initialize()
		{
			main::loadSramImage(mem);
			main::delayInit(mem);
			main::reverbInit(mem);

			// The default kit: each track's default machine and every parameter at its descriptor's default; the
			// settings (modulation lists, glide, key track, bend range) all 0; each track's level LEV's default
			for(int t = 0; t < EngineTracks; ++t)
			{
				const int machine = t == FxTrack ? 0 : mem.word(DefaultMachines + 4 * static_cast<uint32_t>(t));
				mem.setByte(main::Machines + static_cast<uint32_t>(t), static_cast<uint8_t>(machine));
				const int ids = t == FxTrack ? FxParameterEnd : TrackParameterEnd;
				for(int id = 0; id < ids; ++id)
					setKit(t, id, defaultOf(machine, t, id));
				mem.setHalf(main::Engine + 2 * static_cast<uint32_t>(t), trackLevel());
			}
			// The smoothing starts at its targets
			for(uint32_t i = 0; i < main::EngineWords; ++i)
				mem.setWord(main::SmoothState + 4 * i, mem.half(main::Engine + 2 * i) * 65536);

			mem.setWord(KitPointer, static_cast<int32_t>(KitBytes));
			mem.setByte(LevelToStems, 1);
			mem.setWord(main::VoiceOutputDma, 1);
			mem.setWord(main::Tempo, 120 * 120);
		}

		uint16_t trackLevel() const
		{
			const uint32_t d = Descriptors + DescriptorBytes * TrackLevelDescriptor;
			return static_cast<uint16_t>(mem.word(d + 16));
		}

		// The masks of the block from the calls since the last one, the gates the event loop leaves, then the block,
		// the mix of the voice CPU's last one, and the voice CPU's turn
		void block()
		{
			using Clock = std::chrono::steady_clock;
			const auto start = Clock::now();
			main::BlockMasks masks;
			masks.trigs = masks.lfoTrigs = masks.filterTrigs = static_cast<int32_t>(trigs);
			masks.gateOffs = masks.filterGateOffs = static_cast<int32_t>(gateOffs & ~trigs);
			const uint32_t open = held & ~gateOffs;
			mem.setHalf(main::Gates, static_cast<int32_t>(open));
			mem.setHalf(main::Held, static_cast<int32_t>(open));
			held = open | trigs;
			trigs = 0;
			gateOffs = nextGateOffs;
			nextGateOffs = 0;

			for(uint32_t i = 0; i < voices.size(); ++i)
				mem.setWord(main::VoiceOutputs + 4 * i, voices[i]);
			main::buildBlock(mem, masks);
			const auto built = Clock::now();
			main::Emac4 acc;
			main::mixOutput(mem, acc, main::Smoothed);

			for(uint32_t s = 0; s < BlockSize; ++s)
			{
				const uint32_t frame = main::Overbridge + 0x50 * s;
				for(uint32_t t = 0; t < Tracks; ++t)
					out[BlockSize * (Stem1 + t) + s] = mem.word(frame + 8 + 4 * t);
				out[BlockSize * ReturnsLeft + s] = mem.word(frame + 0x40);
				out[BlockSize * ReturnsRight + s] = mem.word(frame + 0x44);
				out[BlockSize * DirectLeft + s] = mem.word(main::DirectBus + 8 * s);
				out[BlockSize * DirectRight + s] = mem.word(main::DirectBus + 8 * s + 4);
				out[BlockSize * FxLeft + s] = mem.word(main::FxBus + 8 * s);
				out[BlockSize * FxRight + s] = mem.word(main::FxBus + 8 * s + 4);
			}
			const auto mixed = Clock::now();

			uint8_t bytes[main::BlockBytes];
			for(uint32_t i = 0; i < main::BlockBytes; ++i)
				bytes[i] = mem.byte(main::Block + i);
			runBlock(sram, globals, rom, bytes);
			std::copy_n(sram.ptr(VoiceOut), voices.size(), voices.data());
			position = 0;

			const auto end = Clock::now();
			times.build = std::chrono::duration<double>(built - start).count();
			times.mix = std::chrono::duration<double>(mixed - built).count();
			times.voices = std::chrono::duration<double>(end - mixed).count();
			++times.blocks;
		}
	};

	Syntakt::Syntakt(const main::Section3& _section3, const std::vector<int32_t>& _section7)
		: m_state(std::make_unique<State>(_section3, _section7))
	{
		boot(m_state->sram, m_state->globals, m_state->rom);
		m_state->initialize();
	}

	Syntakt::~Syntakt() = default;

	void Syntakt::setSamples(const Samples* _samples)
	{
		m_state->rom.samples = _samples;
	}

	bool Syntakt::isPorted(const int _machine) const
	{
		// As VoiceCpu::isPorted: the voice CPU's map sends every analog machine to engine 0, BD MODERN's
		if(_machine < 0 || _machine > 44)
			return false;
		const int32_t id = engineOf(m_state->rom, _machine);
		if(id == 0 && _machine != 0)
			return false;
		return engine(static_cast<uint32_t>(id)) != nullptr;
	}

	bool Syntakt::setMachine(const int _track, const int _machine)
	{
		if(_track < 0 || _track >= Tracks || !isPorted(_machine))
			return false;
		auto& s = *m_state;
		s.mem.setByte(main::Machines + static_cast<uint32_t>(_track), static_cast<uint8_t>(_machine));
		for(int id = Tune; id <= Overdrive; ++id)
			s.setKit(_track, id, s.defaultOf(_machine, _track, id));
		return true;
	}

	void Syntakt::setParameter(const int _track, const int _id, const uint16_t _value)
	{
		if(_track < 0 || _track >= Tracks || _id < 0 || _id >= TrackParameterEnd)
			return;
		m_state->setKit(_track, _id, _value);
	}

	void Syntakt::setFxParameter(const int _id, const uint16_t _value)
	{
		if(_id < 0 || _id >= FxParameterEnd)
			return;
		m_state->setKit(FxTrack, _id, _value);
	}

	void Syntakt::setLevel(const int _track, const uint16_t _value)
	{
		if(_track < 0 || _track >= Tracks)
			return;
		m_state->mem.setHalf(main::Engine + 2 * static_cast<uint32_t>(_track), _value);
	}

	void Syntakt::setTempo(const double _bpm)
	{
		// The firmware's tempo is BPM x 120; 30 to 300 BPM, as the machine's
		const double bpm = std::clamp(_bpm, 30.0, 300.0);
		m_state->mem.setWord(main::Tempo, static_cast<int32_t>(std::lround(bpm * 120)));
	}

	void Syntakt::lock(const int _track, const int _id, const uint16_t _value)
	{
		if(_track < 0 || _track >= Tracks || _id < 0 || _id >= TrackParameterEnd)
			return;
		auto& locks = m_state->locks[static_cast<size_t>(_track)];
		if(locks.size() < MaxLocks)
			locks.emplace_back(_id, _value);
	}

	void Syntakt::noteOn(const int _track, const int _note, const int _velocity)
	{
		if(_track < 0 || _track >= Tracks)
			return;
		auto& s = *m_state;
		auto& mem = s.mem;
		const auto t = static_cast<uint32_t>(_track);

		// What the event loop writes on a trig: the note, the velocity, the last trig's locks back to the kit's values
		// (0x40097760 from the track's sound), then this trig's (0x400978b4)
		mem.setWord(main::Notes + 4 * t, std::clamp(_note, 0, 127) << 16);
		mem.setHalf(main::Velocity + 2 * t, std::clamp(_velocity, 1, 127) << 8);
		for(int id = 0; id < TrackParameterEnd; ++id)
			mem.setHalf(KitSound + 0x14 + 2 * static_cast<uint32_t>(id), s.kit[t][static_cast<size_t>(id)]);
		main::restoreLocks(mem, _track, main::Engine, KitSound);
		auto& locks = s.locks[t];
		if(!locks.empty())
		{
			mem.setWord(LockList + 8, static_cast<int32_t>(locks.size()));
			for(size_t i = 0; i < locks.size(); ++i)
			{
				mem.setHalf(LockList + 0x14 + 8 * static_cast<uint32_t>(i), locks[i].first);
				mem.setHalf(LockList + 0x16 + 8 * static_cast<uint32_t>(i), locks[i].second);
			}
			main::lock(mem, _track, LockList, main::Engine);
			locks.clear();
		}

		s.trigs |= bit(_track);
		s.nextGateOffs &= ~bit(_track);
	}

	void Syntakt::noteOff(const int _track)
	{
		if(_track < 0 || _track >= Tracks)
			return;
		auto& s = *m_state;
		// The length counter (0x80003050) counts down at the start of each block, before the trigs set it: a note
		// trigged in this block ends in the next one at the earliest
		if(s.trigs & bit(_track))
			s.nextGateOffs |= bit(_track);
		else
			s.gateOffs |= bit(_track);
	}

	const Syntakt::BlockTimes& Syntakt::blockTimes() const
	{
		return m_state->times;
	}

	void Syntakt::render(int32_t* const* _outputs, const size_t _count)
	{
		auto& s = *m_state;
		for(size_t done = 0; done < _count;)
		{
			if(s.position == BlockSize)
				s.block();
			const size_t n = std::min(_count - done, static_cast<size_t>(BlockSize - s.position));
			for(int c = 0; c < Outputs; ++c)
				std::copy_n(s.out.data() + BlockSize * c + s.position, n, _outputs[c] + done);
			s.position += static_cast<int>(n);
			done += n;
		}
	}
}
