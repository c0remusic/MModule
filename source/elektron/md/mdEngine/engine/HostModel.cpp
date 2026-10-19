#include "HostModel.h"

#include <chrono>

#include <algorithm>
#include <cmath>

namespace md::engine
{
	namespace
	{
		// OS 1.63 addresses (internal SRAM $1000000 = the OS's copy of image $2622f4)
		constexpr uint32_t kRaw = 0x1000ddc;		// smoothing targets: 16 tracks x 24 bytes (0-127)
		constexpr uint32_t kWork = 0x1000a4c;		// smoothed values: 16 x 24 words (value << 7, up to $3fff)
		constexpr uint32_t kVoiceParams = 0x10011cc;	// per-voice arrays (a6): 16 x 24 words, after LFO
		constexpr uint32_t kTempo = 0x100150c;		// BPM x 24
		constexpr uint32_t kSmooth = 0x10002e0, kLfoOsc = 0x1000088, kLfoApply = 0x1000332;
		constexpr uint32_t kLfo = 0x1000f8c, kLfoStride = 0x24;	// 16 LFO structs: bytes 0-4 settings, 5 = trigger flag
		constexpr uint32_t kLfoWave = 0x204c94;		// waveform/restart for one LFO (in the tick routine's trigger path)
		constexpr uint32_t kLfoApplyOne = 0x10001e8;	// apply one LFO to its destination in the voice arrays
		constexpr uint32_t kTrackStride = 0x30;
		constexpr uint32_t kLevel = 0x1000d7c;		// smoothed track levels: 16 words (value << 7); then 32 master FX
		constexpr uint32_t kLevelTarget = 0x1000f5c;	// their targets: 48 bytes (0-127)
		constexpr uint32_t kLevelSmooth = 0x100029e;	// new = (3 old + target << 7) >> 2, all 48
		constexpr uint32_t kMasterTarget = kLevelTarget + 16;	// md-drums: the 32 master bytes, after the 16 levels

		// md-drums: the tick routine computes one master effect's DSP words per tick, inline, from the slewed bytes, into a
		// staging area its DSP2 interrupt then sends ($20aa66). Each body, its exit turned into a return, is called alone.
		struct MasterBody
		{
			uint32_t entry, exit, staging, y;
			int count;
		};
		constexpr MasterBody kMasterBodies[4] = {
			{0x20b4ae, 0x20b5f8, 0x1001af4, 0x150, 9},	// Echo
			{0x20b604, 0x20b742, 0x1001b18, 0x17a, 11},	// Dynamix
			{0x20b74e, 0x20b934, 0x1001b44, 0x170, 10},	// EQ
			{0x20b940, 0x20ba4c, 0x1001b74, 0x185, 8},	// Gate Box
		};
		constexpr uint16_t kRts = 0x4e75;
	}

	template<class TVoices>
	HostModel<TVoices>::HostModel(MachineRunner& _os, TVoices& _voices) : m_os(_os), m_voices(_voices)
	{
		setTempo(125.0);
		m_pendingMachine.fill(-1);
		m_route.fill(6);
		m_link.fill(kOff);
		m_choke.fill(kOff);
		m_cutBy.fill(kOff);
		for(size_t i = 0; i < m_decay.size(); ++i)
			m_decay[i] = m_os.peek32(md::mds::DecayTableAddress + 4 * static_cast<uint32_t>(i));
		// md-drums: the master bodies return to the caller instead of running into the tick's epilogue (which mdEngine
		// never calls)
		for(const auto& body : kMasterBodies)
			m_os.poke16(body.exit, kRts);
		for(int t = 0; t < kTracks; ++t)
		{
			m_os.poke8(kLevelTarget + static_cast<uint32_t>(t), 100);
			m_os.poke16(kLevel + 2 * static_cast<uint32_t>(t), 100 << 7);
			m_machine[t] = 0;
			setMachine(t, 0);
			setLfo(t, t, 0, 0, 0, 0);
		}
	}

	template<class TVoices>
	void HostModel<TVoices>::setMachine(const int _track, const uint8_t _machineId)
	{
		if(isUserMachine(_machineId))
		{
			// md-drums: its package's defaults, or none while its number is empty
			const auto& package = m_userMachines[_machineId - kFirstUserMachine];
			m_pendingMachine[_track] = _machineId;
			for(size_t p = 0; p < 8; ++p)
				m_raw[_track][p] = package && p < package->parameters.size() ? package->parameters[p].defaultValue : 0;
			return;
		}
		const auto* m = m_os.machine(_machineId);
		if(!m) return;
		m_pendingMachine[_track] = _machineId;
		for(int p = 0; p < 8; ++p)
			m_raw[_track][p] = m->defaults[p];
	}

	template<class TVoices>
	void HostModel<TVoices>::setUserMachine(const int _index, std::shared_ptr<const md::mds::Package> _package)
	{
		if(_index >= 0 && _index < static_cast<int>(m_userMachines.size()))
			m_userMachines[static_cast<size_t>(_index)] = std::move(_package);
	}

	template<class TVoices>
	void HostModel<TVoices>::setParam(const int _track, const int _param, const int _value)
	{
		const auto value = static_cast<uint8_t>(std::clamp(_value, 0, 127));
		m_raw[_track][_param] = value;
		// md-drums: the OS's knob and CC path writes the smoothing target whatever holds it (mdSlideFirmwareTest): a
		// lock plays the Kit's new value until the track's next Hit, a slide until its next clock, which goes on from
		// where the slide stood
		if((m_locked[_track] >> _param) & 1)
			m_lockValue[_track][_param] = value;
	}

	template<class TVoices>
	void HostModel<TVoices>::lock(const int _track, const int _param, const int _value)
	{
		if(_track < 0 || _track >= kTracks || _param < 0 || _param >= kParams)
			return;
		m_lockPending[_track] |= 1u << _param;
		m_lockPendingValue[_track][_param] = static_cast<uint8_t>(std::clamp(_value, 0, 127));
	}

	template<class TVoices>
	void HostModel<TVoices>::accent(const int _track, const int _amount)
	{
		if(_track < 0 || _track >= kTracks)
			return;
		m_accentPending |= 1u << _track;
		m_accentPendingAmount = static_cast<uint8_t>(std::clamp(_amount, 0, 127));
	}

	template<class TVoices>
	void HostModel<TVoices>::applyAccents()
	{
		// The block's accented Hits, and their Links' (the note handler gives the trig group's track the trigger's
		// accent, $20cf8c), before any voice's mixer words are computed; an accent whose track has no Hit drops
		for(int t = 0; t < kTracks && m_accentPending; ++t)
		{
			if(!((m_accentPending >> t) & 1) || !m_trigger[t])
				continue;
			m_accentAmount = m_accentPendingAmount;
			m_accent[t] = true;
			const auto link = m_link[t];
			if(link < kTracks && link != t && m_trigger[link])
				m_accent[link] = true;
		}
		m_accentPending = 0;
	}

	template<class TVoices>
	uint8_t HostModel<TVoices>::target(const int _track, const int _param) const
	{
		return (m_locked[_track] >> _param) & 1 ? m_lockValue[_track][_param] : m_raw[_track][_param];
	}

	template<class TVoices>
	void HostModel<TVoices>::applyLocks(const int _track)
	{
		// The Hit's locks, and the Kit's value back on what the last Hit locked: straight into the smoothing target, the
		// smoothed array and the voice array, as a pending machine is (no glide)
		const auto changed = m_locked[_track] | m_lockPending[_track];
		m_locked[_track] = m_lockPending[_track];
		m_lockValue[_track] = m_lockPendingValue[_track];
		const auto track = static_cast<uint32_t>(_track);
		for(int p = 0; p < kParams; ++p)
		{
			if(!((changed >> p) & 1))
				continue;
			const auto value = target(_track, p);
			const auto param = static_cast<uint32_t>(p);
			m_os.poke8(kRaw + 24 * track + param, value);
			m_os.poke16(kWork + kTrackStride * track + 2 * param, static_cast<uint16_t>(value << 7));
			m_os.poke16(kVoiceParams + kTrackStride * track + 2 * param, static_cast<uint16_t>(value << 7));
		}
		armSlide(_track);
	}

	template<class TVoices>
	void HostModel<TVoices>::slide(const int _track, const int _clocks)
	{
		if(_track < 0 || _track >= kTracks)
			return;
		m_slidePending[_track] = static_cast<uint8_t>(std::clamp(_clocks, 1, 127));
	}

	template<class TVoices>
	void HostModel<TVoices>::extendSlide(const int _track, const int _times127)
	{
		if(_track < 0 || _track >= kTracks)
			return;
		m_slideExtension[_track] = static_cast<uint8_t>(std::clamp(_times127, 0, 127));
	}

	template<class TVoices>
	void HostModel<TVoices>::slideTarget(const int _track, const int _param, const int _value)
	{
		if(_track < 0 || _track >= kTracks || _param < 0 || _param >= kParams)
			return;
		m_slideTargetPending[_track] |= 1u << _param;
		m_slideTargetValue[_track][_param] = static_cast<uint8_t>(std::clamp(_value, 0, 127));
	}

	template<class TVoices>
	void HostModel<TVoices>::armSlide(const int _track)
	{
		// The trigger path gives every parameter with a lock state the increment waiting for it, 0 unless this Hit slides
		// ($20b078-$20b1d0): the track's last slide ends here. The setup's increment ($23b49c-$23b708): from the Hit's
		// lock to the next Hit's, or the Kit's value, over the clocks between them, divs.l truncating toward zero.
		m_sliding[_track] = 0;
		if(!m_slidePending[_track])
			return;
		const int clocks = m_slidePending[_track] + 127 * m_slideExtension[_track];
		for(int p = 0; p < kParams; ++p)
		{
			if(!((m_locked[_track] >> p) & 1))
				continue;
			const int32_t start = m_lockValue[_track][p] << 7;
			const int32_t end = ((m_slideTargetPending[_track] >> p) & 1 ? m_slideTargetValue[_track][p]
				: m_raw[_track][p]) << 7;
			const int32_t increment = (end - start) / clocks;
			if(!increment)
				continue;
			m_slides[_track][p] = {start, increment, end};
			m_sliding[_track] |= 1u << p;
		}
	}

	template<class TVoices>
	void HostModel<TVoices>::clock()
	{
		// The update ($23ac2c-$23add2), on every clock while the sequencer plays: past its end a slide stops, the target
		// where the last clock left it; else the smoothing target takes the new value >> 7, which the OS's smoothing
		// chases at its ticks. A track whose trig waits for the tick ($2ae8c2) is skipped: here one struck in the block
		// about to render, which its Hit arms or ends.
		for(int t = 0; t < kTracks; ++t)
		{
			if(m_trigger[t])
				continue;
			for(int p = 0; p < kParams && m_sliding[t]; ++p)
			{
				if(!((m_sliding[t] >> p) & 1))
					continue;
				auto& s = m_slides[t][p];
				const int32_t next = s.current + s.increment;
				if(s.increment > 0 ? next > s.end : next < s.end)
				{
					m_sliding[t] &= ~(1u << p);
					continue;
				}
				s.current = next;
				m_lockValue[t][p] = static_cast<uint8_t>(next >> 7);
			}
		}
	}

	template<class TVoices>
	void HostModel<TVoices>::setTempo(const double _bpm)
	{
		m_os.poke32(kTempo, static_cast<uint32_t>(std::lround(std::clamp(_bpm, 30.0, 300.0) * 24.0)));
	}

	template<class TVoices>
	void HostModel<TVoices>::setLfo(const int _track, const int _destTrack, const int _destParam, const int _shape1, const int _shape2, const int _type)
	{
		const uint32_t base = kLfo + kLfoStride * static_cast<uint32_t>(_track);
		m_os.poke8(base + 0, static_cast<uint8_t>(std::clamp(_destTrack, 0, kTracks - 1)));
		m_os.poke8(base + 1, static_cast<uint8_t>(std::clamp(_destParam, 0, kParams - 1)));
		m_os.poke8(base + 2, static_cast<uint8_t>(std::clamp(_shape1, 0, 7)));
		m_os.poke8(base + 3, static_cast<uint8_t>(std::clamp(_shape2, 0, 7)));
		m_os.poke8(base + 4, static_cast<uint8_t>(std::clamp(_type, 0, 3)));
	}

	template<class TVoices>
	void HostModel<TVoices>::loadLfo(const int _track, const uint8_t* _block)
	{
		const uint32_t base = kLfo + kLfoStride * static_cast<uint32_t>(_track);
		for(uint32_t i = 0; i < static_cast<uint32_t>(kLfoBytes); ++i)
			m_os.poke8(base + i, _block[i]);
	}

	template<class TVoices>
	void HostModel<TVoices>::setLevel(const int _track, const int _level)
	{
		m_os.poke8(kLevelTarget + static_cast<uint32_t>(_track), static_cast<uint8_t>(std::clamp(_level, 0, 127)));
	}

	template<class TVoices>
	void HostModel<TVoices>::setMaster(const int _index, const int _value)
	{
		if(_index >= 0 && _index < kMasterParams)
			m_os.poke8(kMasterTarget + static_cast<uint32_t>(_index), static_cast<uint8_t>(std::clamp(_value, 0, 127)));
	}

	template<class TVoices>
	int HostModel<TVoices>::master(const int _index) const
	{
		return _index >= 0 && _index < kMasterParams ? m_os.peek8(kMasterTarget + static_cast<uint32_t>(_index)) : 0;
	}

	template<class TVoices>
	bool HostModel<TVoices>::takeMasterWords(MasterWords& _out)
	{
		if(!m_masterPending)
			return false;
		_out = m_masterWords;
		m_masterPending = false;
		return true;
	}

	template<class TVoices>
	void HostModel<TVoices>::setLink(const int _track, const int _target)
	{
		m_link[_track] = _target >= 0 && _target < kTracks ? static_cast<uint8_t>(_target) : kOff;
	}

	template<class TVoices>
	void HostModel<TVoices>::setChoke(const int _track, const int _target)
	{
		m_choke[_track] = _target >= 0 && _target < kTracks ? static_cast<uint8_t>(_target) : kOff;
	}

	template<class TVoices>
	uint32_t HostModel<TVoices>::trigger(const int _track, const int _velocity, const bool _accent)
	{
		if(m_mute[_track])
			return 0;
		hit(_track, _velocity, _accent);
		// The note handler gives the trig group's track the same velocity and accent ($20cf8c), and the tick fires it
		// in the same tick from the handler's own flags only, so its own trig group does not fire ($20add8)
		const auto link = m_link[_track];
		if(link >= kTracks || link == _track || m_mute[link])
			return 1u << _track;
		hit(link, _velocity, _accent);
		return (1u << _track) | (1u << link);
	}

	template<class TVoices>
	void HostModel<TVoices>::hit(const int _track, const int _velocity, const bool _accent)
	{
		m_trigger[_track] = true;
		m_velocity[_track] = static_cast<uint8_t>(std::clamp(_velocity, 1, 127));
		m_accent[_track] = _accent;
		// The tick's trigger path snaps the level to the Kit's, smoothed value and target alike ($20b022)
		const auto track = static_cast<uint32_t>(_track);
		m_os.poke16(kLevel + 2 * track, static_cast<uint16_t>(m_os.peek8(kLevelTarget + track) << 7));
		// The OS's track trigger ($20cdf0) flags the track's own LFO; the tick's trigger path acts on it.
		m_os.poke8(kLfo + kLfoStride * static_cast<uint32_t>(_track) + 5, 1);
		// The tick routine's trigger path: a pending machine is applied now, loading the kit values straight into
		// the smoothing targets, the smoothed array and the voice array (no glide).
		if(m_pendingMachine[_track] >= 0)
		{
			m_machine[_track] = static_cast<uint8_t>(m_pendingMachine[_track]);
			m_pendingMachine[_track] = -1;
			for(int p = 0; p < kParams; ++p)
			{
				const auto v = static_cast<uint16_t>(m_raw[_track][p] << 7);
				m_os.poke8(kRaw + 24 * static_cast<uint32_t>(_track) + static_cast<uint32_t>(p), m_raw[_track][p]);
				m_os.poke16(kWork + kTrackStride * static_cast<uint32_t>(_track) + 2 * static_cast<uint32_t>(p), v);
				m_os.poke16(kVoiceParams + kTrackStride * static_cast<uint32_t>(_track) + 2 * static_cast<uint32_t>(p), v);
			}
		}
	}

	template<class TVoices>
	const uint16_t* HostModel<TVoices>::voiceParams(const int _track) const
	{
		auto* self = const_cast<HostModel*>(this);
		for(int p = 0; p < kParams; ++p)
			self->m_scratch[p] = m_os.peek16(kVoiceParams + kTrackStride * static_cast<uint32_t>(_track) + 2 * static_cast<uint32_t>(p));
		return m_scratch.data();
	}

	template<class TVoices>
	void HostModel<TVoices>::updateVoice(const int _track)
	{
		// As the tick routine's voice loop: the machine function on the voice's current array; word 0 = trigger
		// flag, which the slot pump turns into machine id + 1. MID/CTR machines have no audio and are not sent.
		// The tick routine's trigger path, before the machine function: the LFO's restart/hold for this
		// trigger, then this track's LFO applied to its destination straight away.
		if(m_trigger[_track])
		{
			m_os.call(kLfoWave, {static_cast<uint32_t>(_track)});
			m_os.call(kLfoApplyOne, {static_cast<uint32_t>(_track)});
		}
		const auto id = m_machine[_track];
		if(isUserMachine(id))
		{
			// md-drums: a User Machine's words are its eight parameters, converted as its package says, from the voice's
			// array after the smoothing and the LFOs (a setting between two steps falls between their words, as MDS allows);
			// word 0 the trigger flag, the type its number's (id + 1), as the built-in Machines'
			uint32_t out[9] = {};
			if(const auto& package = m_userMachines[id - kFirstUserMachine])
			{
				const auto* params = voiceParams(_track);
				for(size_t p = 0; p < package->parameters.size() && p < 8; ++p)
					out[1 + p] = md::mds::convertFine(package->parameters[p], params[p], m_decay);
			}
			out[0] = m_trigger[_track] ? static_cast<uint32_t>(id) + 1 : 0;
			m_voices.setSlot(_track, out, 9);
		}
		else if(isAudioMachine(id))
		{
			uint32_t out[32];
			const int n = m_os.compute(id, voiceParams(_track), out, 32, m_trigger[_track]);
			if(n >= 0)	// n == 0 (TRX CP/MA/CL: no parameter words) still carries the trigger word
			{
				out[0] = m_trigger[_track] ? static_cast<uint32_t>(id) + 1 : 0;
				m_voices.setSlot(_track, out, std::clamp(n, 1, static_cast<int>(TVoices::kSlotWords)));
			}
		}
		updateMixer(_track);
		if(m_trigger[_track])
			applyChoke(_track);
		m_trigger[_track] = false;
	}

	template<class TVoices>
	void HostModel<TVoices>::applyChoke(const int _track)
	{
		// The tick's mute-group step ($20b3a2), after the track's own words: those took its flag as it was, so a track
		// struck while choked stays silent until the next tick gives it words again
		const auto target = m_choke[_track];
		if(target < kTracks && target != _track)
		{
			m_choked[target] = true;
			// A higher track's words come later in the same tick, a lower one's at the next tick. Between ticks only
			// the struck tracks are updated: a higher target's words are given at once.
			if(target > _track)
			{
				m_cutBy[target] = static_cast<uint8_t>(_track);
				if(!m_trigger[target])
					updateMixer(target);
			}
		}
		// With a self-reference, the flag is set then cleared: nothing
		m_choked[_track] = false;
	}

	template<class TVoices>
	void HostModel<TVoices>::updateMixer(const int _track)
	{
		// The tick routine's DSP1 block ($20b1e2-$20b302). MID/CTR machines send nothing.
		auto& m = m_mixer[_track];
		if(!isAudioMachine(m_machine[_track]))
			return;
		const uint16_t* a6 = voiceParams(_track);
		for(int k = 0; k < 9; ++k)
			m.fx[k] = a6[8 + k];	// sent unchanged by $1000702
		m.mix[0] = m_route[_track];
		if(m_mute[_track] || m_choked[_track])
		{
			m.mix[1] = m.mix[2] = m.mix[3] = m.mix[4] = 0;
			return;
		}
		// VOL gain = ((LEV^2 >> 8) x VEL >> 17) x (VOL^2 >> 17); VEL = the trigger's velocity, or 128 + 2 x accent
		const int32_t lev = m_os.peek16(kLevel + 2 * static_cast<uint32_t>(_track));
		const int32_t vel = m_accent[_track] ? 128 + 2 * m_accentAmount : m_velocity[_track];
		const auto sq = [](const uint16_t _v) { return static_cast<uint32_t>(_v) * _v; };
		const int32_t gain = static_cast<int32_t>((((lev * lev) >> 8) * vel) >> 17) * static_cast<int32_t>(sq(a6[17]) >> 17);
		m.mix[1] = static_cast<uint32_t>(gain) & 0xffffff;
		m.mix[2] = (static_cast<uint32_t>(a6[18]) << 9) & 0x1fffe00;
		m.mix[3] = sq(a6[20]) >> 5;	// REV
		m.mix[4] = sq(a6[19]) >> 5;	// DEL
	}

	template<class TVoices>
	void HostModel<TVoices>::tick()
	{
		for(int t = 0; t < kTracks; ++t)
			for(int p = 0; p < kParams; ++p)
				m_os.poke8(kRaw + 24 * static_cast<uint32_t>(t) + static_cast<uint32_t>(p), target(t, p));

		for(int t = 0; t < kTracks; ++t)
			updateVoice(t);

		// After the voice loop the OS smooths the parameters, runs the LFOs and builds the next tick's arrays.
		m_os.call(kSmooth, {});
		m_os.call(kLfoOsc, {});
		m_os.call(kLfoApply, {});
		m_os.call(kLevelSmooth, {});
		// md-drums: then one master effect's words, in turn, as the tick routine does after the level slew ($20b4a2)
		{
			const auto& body = kMasterBodies[m_tickCount & 3];
			m_os.call(body.entry, {});
			m_masterWords.y = body.y;
			m_masterWords.count = body.count;
			for(int k = 0; k < body.count; ++k)
				m_masterWords.words[static_cast<size_t>(k)] = m_os.peek32(body.staging + 4 * static_cast<uint32_t>(k)) & 0xffffff;
			m_masterPending = true;
		}
		++m_tickCount;
	}

	template<class TVoices>
	bool HostModel<TVoices>::renderBlock(typename TVoices::Block& _out)
	{
		// Ticks on a fixed block schedule; a trigger between ticks updates just its voice, so it starts on this
		// block rather than waiting for the next tick.
		const auto t0 = timingOn ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
		// md-drums: the block's Hits take their locks and slides before any voice is updated, whichever came first; the
		// rest drop
		for(int t = 0; t < kTracks; ++t)
			if(m_trigger[t])
				applyLocks(t);
		m_lockPending.fill(0);
		m_slidePending.fill(0);
		m_slideExtension.fill(0);
		m_slideTargetPending.fill(0);
		applyAccents();
		m_cutBy.fill(kOff);
		if(m_blockCount++ % kBlocksPerTick == 0)
			tick();
		else
			for(int t = 0; t < kTracks; ++t)
				if(m_trigger[t])
					updateVoice(t);
		const auto t1 = timingOn ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
		const bool ok = m_voices.renderBlock(_out);
		if(timingOn)
		{
			const auto t2 = std::chrono::steady_clock::now();
			tickUs += std::chrono::duration<double, std::micro>(t1 - t0).count();
			dspUs += std::chrono::duration<double, std::micro>(t2 - t1).count();
		}
		return ok;
	}

	template class HostModel<VoiceEngine>;
}
