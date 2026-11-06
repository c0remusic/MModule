#include "edDrumsEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

#include <emmintrin.h>

#include "stFastBdModern.h"
#include "stFastCpVintage.h"
#include "stFastCyAlloy.h"
#include "stFastPcCarbon.h"
#include "stFastResample.h"
#include "stFastSdBasic.h"
#include "stFastSdVintage.h"
#include "stFastSpTwinshot.h"
#include "stFastSyBits.h"
#include "stFastSyToy.h"

namespace elektron::drums
{
	namespace
	{
		using syntakt::fast::Resample48To44;

		constexpr int Up = Resample48To44::Up, Down = Resample48To44::Down;
		constexpr int FirTaps = Resample48To44::Taps;
		constexpr int FirRow = (FirTaps + 3) / 4 * 4;					// a vector's worth of taps at a time
		constexpr int FirDelay = Resample48To44::Latency * Down;		// in 1/147 of a 48 kHz sample
		static_assert(Down % Up == 13 && Down / Up == 1, "a host sample steps the read by 1 sample and 13 phases");

		// The FIR's taps oldest first, the first ones of a row zeros
		struct FirTable
		{
			alignas(16) float rows[Up][FirRow]{};

			FirTable()
			{
				const auto taps = std::make_unique<float[][FirTaps]>(Up);
				Resample48To44::taps(taps.get());
				for(int p = 0; p < Up; ++p)
					for(int k = 0; k < FirTaps; ++k)
						rows[p][FirRow - 1 - k] = taps[p][k];
			}
		};

		const FirTable& firTable()
		{
			static const FirTable table;
			return table;
		}

		// A Syntakt machine's neutral sound until a Kit gives it one (the firmware's defaults are not in the repository):
		// its knobs in the middle, its drive and HOLD off
		constexpr std::array<uint8_t, 8> SyntaktDefaults{64, 64, 64, 64, 64, 0, 0, 64};

		// The latest a Syntakt note can wait, in 1/147 of a 48 kHz sample: the block that sees its trig, at most 142 / 147
		// of a block past 32 samples ahead of the note; the block that sounds it; the FIR's delay
		constexpr int64_t SyntaktWait = BlockSize * 142 + Up * BlockSize + FirDelay;

		// A voice reaches 1 % of its peak in its first samples, all but these, measured by edDrumsEngineTest with the
		// defaults: TRX-BD and TRX-S2 sound a block after their trigger, TRX-XT later still, the ROM machines' filters take 8.
		// TRX-CB, TRX-XC, TRX-MA and P-I-ML are later than a Hit can wait, their own sound; a Syntakt machine starts in
		// its block, up to 7 samples in. As much as a Hit holds its voice back for, in its own samples
		constexpr int LatestStart = 38;

		int voiceStart(const Machine& _machine)
		{
			if(_machine.family != Family::Machinedrum)
				return 0;
			const uint8_t id = _machine.id;
			return id == 16 ? 34 : id == 18 ? LatestStart : id == 29 ? 33 : (id >= 128 && id < 160) || (id >= 176 && id < 192) ? 8 : 0;
		}

		float cutGain(const int64_t _n)
		{
			return std::max(0.0f, 1.0f - static_cast<float>(_n + 1) / static_cast<float>(Engine::CutSamples));
		}

		float gainOf(const int _value)
		{
			const float g = static_cast<float>(_value) / 127.0f;
			return g * g;
		}

		// A sum over the taps of one Track, oldest first
		inline float dot(const float* _x, const float* _h)
		{
			__m128 sum = _mm_setzero_ps();
			for(int k = 0; k < FirRow; k += 4)
				sum = _mm_add_ps(sum, _mm_mul_ps(_mm_loadu_ps(_x + k), _mm_load_ps(_h + k)));
			__m128 hi = _mm_movehl_ps(sum, sum);
			sum = _mm_add_ps(sum, hi);
			hi = _mm_shuffle_ps(sum, sum, 1);
			return _mm_cvtss_f32(_mm_add_ss(sum, hi));
		}
	}

	bool playable(const Machine& _machine)
	{
		switch(_machine.family)
		{
		case Family::Off:			return true;
		case Family::Syntakt:		return _machine.id < SyntaktMachines;
		case Family::Machinedrum:	return md::voice::playable(_machine.id);
		}
		return false;
	}

	std::array<uint8_t, 8> defaults(const Library& _library, const Machine& _machine)
	{
		return _machine.family == Family::Machinedrum ? _library.mdDefaults[_machine.id] : SyntaktDefaults;
	}

	// ---- a Syntakt machine, whichever it is ----

	struct Engine::Source
	{
		virtual ~Source() = default;
		virtual void setLanes(int _lanes) = 0;
		virtual void setNote(int _lane, int _note) = 0;
		virtual void setParameter(int _lane, int _index, uint8_t _value) = 0;
		virtual void trig(int _lane) = 0;
		virtual void gateOff(int _lane) = 0;
		virtual const Block& renderBlock() = 0;

		// The widest group a voice holds: the lowest free lane is the one a Hit takes, so the groups above empty
		void fitWidth()
		{
			int fit = 4;
			for(int l = Lanes - 1; l >= 4; --l)
				if(used >> l & 1)
				{
					fit = (l + 4) / 4 * 4;
					break;
				}
			if(fit == width)
				return;
			width = fit;
			setLanes(fit);
		}

		std::array<Lane, Lanes> lanes{};
		uint32_t used = 0;		// the lanes a voice holds
		int width = Lanes;		// the lanes rendered
	};

	namespace
	{
		template<typename M, typename Base>
		struct SourceOf final : Base
		{
			explicit SourceOf(const syntakt::fast::TwinshotSamples* _samples)
			{
				if constexpr(std::is_same_v<M, syntakt::fast::SpTwinshot>)
					m.setSamples(_samples);
				else
					(void)_samples;
			}

			void setLanes(const int _lanes) override { m.setLanes(_lanes); }
			void setNote(const int _lane, const int _note) override { m.setNote(_lane, _note); }
			void setParameter(const int _lane, const int _index, const uint8_t _value) override
			{
				m.setParameter(_lane, _index, _value, 0);
			}
			void trig(const int _lane) override { m.trig(_lane); }
			void gateOff(const int _lane) override { m.gateOff(_lane); }
			const Block& renderBlock() override { return m.renderBlock(); }

			M m;
		};

		template<typename Base, typename... Machines>
		void makeSources(std::vector<std::unique_ptr<Base>>& _sources, const syntakt::fast::TwinshotSamples* _samples)
		{
			(_sources.push_back(std::make_unique<SourceOf<Machines, Base>>(_samples)), ...);
		}
	}

	// ---- the rings ----

	void Engine::Ring::put(const int64_t _position, const float _value)
	{
		const auto i = static_cast<int>(_position & (Size - 1));
		m_x[i] = _value;
		if(i < Mirror)
			m_x[Size + i] = _value;
	}

	float Engine::Ring::write(const int64_t _at, const float* _x, const float _from, const float _to, const int64_t _fadeAt)
	{
		for(int64_t p = std::max(m_end, _at - Size); p < _at; ++p)
			put(p, 0);
		const float step = (_to - _from) / BlockSize;
		const auto first = static_cast<int>(_at & (Size - 1));

		// A voice on its own, as most blocks are: in one piece of the ring, on nothing, not fading
		if(_at >= m_end && _fadeAt >= _at + BlockSize && first + BlockSize <= Size)
		{
			static_assert(BlockSize % 4 == 0, "whole vectors");
			const __m128 sign = _mm_set1_ps(-0.0f), from = _mm_set1_ps(_from), steps = _mm_set1_ps(step);
			__m128 peak = _mm_setzero_ps(), counts = _mm_set_ps(4, 3, 2, 1);
			for(int n = 0; n < BlockSize; n += 4)
			{
				const __m128 x = _mm_loadu_ps(_x + n);
				peak = _mm_max_ps(peak, _mm_andnot_ps(sign, x));
				_mm_storeu_ps(m_x + first + n, _mm_mul_ps(x, _mm_add_ps(from, _mm_mul_ps(steps, counts))));
				counts = _mm_add_ps(counts, _mm_set1_ps(4));
			}
			if(first < Mirror)
				std::memcpy(m_x + Size + first, m_x + first, sizeof(float) * static_cast<size_t>(std::min(Mirror - first, BlockSize)));
			m_end = _at + BlockSize;
			peak = _mm_max_ps(peak, _mm_movehl_ps(peak, peak));
			return _mm_cvtss_f32(_mm_max_ss(peak, _mm_shuffle_ps(peak, peak, 1)));
		}

		float peak = 0;
		for(int n = 0; n < BlockSize; ++n)
		{
			const float x = _x[n];
			peak = std::max(peak, std::abs(x));
			float g = _from + step * static_cast<float>(n + 1);
			const int64_t p = _at + n;
			if(p >= _fadeAt)
				g *= cutGain(p - _fadeAt);
			put(p, p < m_end ? at(p) + x * g : x * g);
		}
		m_end = std::max(m_end, _at + BlockSize);
		return peak;
	}

	void Engine::Ring::fade(const int64_t _at)
	{
		for(int64_t p = _at; p < m_end; ++p)
			put(p, at(p) * cutGain(p - _at));
	}

	void Engine::Ring::close()
	{
		// A run of samples reads past the end it started before
		for(int n = 0; n < Mirror + 2 * BlockSize; ++n)
			put(m_end + n, 0);
	}

	void Engine::Run::start(const int64_t _at, const float _velocity, const float _level)
	{
		at = _at;
		velocity = _velocity;
		gain = _level * _velocity;
		quiet = 0;
	}

	void Engine::Run::write(Ring& _ring, const float* _x, const float _level, const int64_t _fadeAt)
	{
		const float to = _level * velocity;
		const float peak = _ring.write(at, _x, gain, to, _fadeAt);
		gain = to;
		at += BlockSize;
		quiet = peak < Silence ? quiet + 1 : 0;
	}

	// ---- the engine ----

	Engine::Engine(const Library& _library) : m_library(_library), m_shared(std::make_unique<md::voice::Shared>())
	{
		static_assert(HitDelay >= BlockSize - 1 + LatestStart, "a Machinedrum Hit waits its block and the latest voice");
		static_assert(HitDelay * Down >= SyntaktWait, "a Syntakt Hit waits its blocks and the FIR");
		static_assert(2 * HitDelay + 2 * BlockSize + CutSamples + Ring::Mirror < Ring::Size, "the rings hold it all");
		static_assert(FirRow == Ring::Mirror, "a window of the ring is a row of the FIR");

		m_shared->tables = &m_library.rom;
		m_shared->bank = m_library.bank.directory ? &m_library.bank : nullptr;
		m_shared->trx = &m_library.trx;
		m_shared->e12 = m_library.e12.empty() ? nullptr : m_library.e12.data();
		m_shared->pi = m_library.pi.empty() ? nullptr : m_library.pi.data();
		m_voices.reserve(Tracks);
		for(uint32_t t = 0; t < Tracks; ++t)
			m_voices.emplace_back(*m_shared, t);

		using namespace syntakt::fast;
		makeSources<Source, BdModern, SdBasic, CyAlloy, PcCarbon, SdVintage, CpVintage, SyToy, SyBits, SpTwinshot>(m_sources,
			m_library.twinshot);
		for(auto& s : m_sources)
			s->fitWidth();

		firTable();		// made here, not on the audio thread
	}

	Engine::~Engine() = default;

	Engine::Source& Engine::sourceOf(const Track& _track)
	{
		return *m_sources[_track.machine.id];
	}

	void Engine::setMachine(const int _track, const Machine& _machine)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		if(t.machine == _machine || !playable(_machine))
			return;
		cut(_track);
		t.machine = _machine;
		t.pendingLocks = t.lockedNow = 0;
		t.params = defaults(m_library, _machine);
		const uint32_t bit = 1u << _track;
		m_mdMask &= ~bit;
		if(_machine.family == Family::Machinedrum)
		{
			auto& md = m_mdTracks[static_cast<size_t>(_track)];
			md.setMachine(_machine.id, t.params);
			md.clearMachineLocks();
			m_mdMask |= bit;
		}
	}

	void Engine::setParameter(const int _track, const int _index, const uint8_t _value)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		t.params[static_cast<size_t>(_index)] = _value;
		if(t.machine.family == Family::Machinedrum)
			m_mdTracks[static_cast<size_t>(_track)].knob(static_cast<size_t>(_index), _value);
		else if(t.lane >= 0)
			sourceOf(t).setParameter(t.lane, _index, _value);
	}

	void Engine::setNote(const int _track, const int _note)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		if(t.note == _note)
			return;
		t.note = _note;
		if(t.lane >= 0)
			sourceOf(t).setNote(t.lane, _note);
	}

	void Engine::setLevel(const int _track, const int _level)
	{
		m_tracks[static_cast<size_t>(_track)].level = gainOf(_level);
	}

	void Engine::setPan(const int _track, const int _pan)
	{
		const auto pan = static_cast<float>(_pan);
		m_tracks[static_cast<size_t>(_track)].pan = {std::min(1.0f, (127.0f - pan) / 63.0f), std::min(1.0f, pan / 64.0f)};
	}

	void Engine::setOutput(const int _track, const int _output)
	{
		m_tracks[static_cast<size_t>(_track)].output = std::clamp(_output, 0, Outputs - 1);
	}

	void Engine::setTempo(const double _bpm)
	{
		m_tempo = static_cast<uint32_t>(std::lround(std::clamp(_bpm, 30.0, 300.0) * 24.0));
	}

	void Engine::setChokes(const int _track, const uint32_t _targets)
	{
		m_tracks[static_cast<size_t>(_track)].chokes = _targets & ~(1u << _track);
	}

	void Engine::lock(const int _track, const int _index, const uint8_t _value)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		if(t.machine.family == Family::Machinedrum)
		{
			m_mdTracks[static_cast<size_t>(_track)].lock(static_cast<size_t>(_index), _value);
			return;
		}
		t.pendingLocks |= 1u << _index;
		t.lockValue[static_cast<size_t>(_index)] = _value;
	}

	void Engine::noteOn(const int _track, const int _velocity)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		for(uint32_t bits = t.chokes, target = 0; bits; bits >>= 1, ++target)
			if(bits & 1)
				cut(static_cast<int>(target));
		if(t.machine.family == Family::Machinedrum)
			hitMachinedrum(_track, _velocity);
		else if(t.machine.family == Family::Syntakt)
			hitSyntakt(_track, _velocity);
	}

	void Engine::noteOff(const int _track)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		if(t.lane >= 0)
			sourceOf(t).gateOff(t.lane);
	}

	void Engine::hitMachinedrum(const int _track, const int _velocity)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		m_mdTracks[static_cast<size_t>(_track)].hit();
		m_mdTracks[static_cast<size_t>(_track)].alignTicks(m_mdBlock);
		// The voice's first sample lands HitDelay after the note, wherever its block falls
		t.md.start(m_now + HitDelay - voiceStart(t.machine), static_cast<float>(_velocity) / 127.0f, t.level);
		m_mdLive |= 1u << _track;
	}

	void Engine::takeLane(const int _track)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		auto& source = sourceOf(t);
		int lane = 0;
		while(lane < Lanes && source.used >> lane & 1)
			++lane;
		if(lane == Lanes)
		{
			// All held: one is a cut voice, on its way out, or the Track would hold one. The Hit takes it at the block that
			// sees its trig, in which the machine fades what the lane played
			lane = 0;
			while(source.lanes[static_cast<size_t>(lane)].fadeAt == Never || source.lanes[static_cast<size_t>(lane)].heir >= 0)
				++lane;
			source.lanes[static_cast<size_t>(lane)].heir = _track;
		}
		else
		{
			source.used |= 1u << lane;
			source.fitWidth();
			source.lanes[static_cast<size_t>(lane)] = {};
			source.lanes[static_cast<size_t>(lane)].track = _track;
			source.lanes[static_cast<size_t>(lane)].skip = true;
		}
		t.lane = lane;
		for(int i = 0; i < 8; ++i)
			source.setParameter(lane, i, t.params[static_cast<size_t>(i)]);
		source.setNote(lane, t.note);
	}

	void Engine::freeLane(const int _source, const int _lane)
	{
		auto& source = *m_sources[static_cast<size_t>(_source)];
		auto& lane = source.lanes[static_cast<size_t>(_lane)];
		auto& t = m_tracks[static_cast<size_t>(lane.track)];
		if(lane.fadeAt == Never)
			t.lane = -1;
		if(t.lane < 0)
			t.stRing.close();
		lane.track = -1;
		source.used &= ~(1u << _lane);
		source.fitWidth();
	}

	void Engine::hitSyntakt(const int _track, const int _velocity)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		if(t.lane < 0)
			takeLane(_track);
		auto& source = sourceOf(t);
		auto& lane = source.lanes[static_cast<size_t>(t.lane)];

		// The Hit's locks, and the Kit's value back on what the last Hit locked
		const uint32_t changed = t.lockedNow | t.pendingLocks;
		for(int i = 0; i < 8; ++i)
			if(changed >> i & 1)
				source.setParameter(t.lane, i, t.pendingLocks >> i & 1 ? t.lockValue[static_cast<size_t>(i)]
					: t.params[static_cast<size_t>(i)]);
		t.lockedNow = t.pendingLocks;
		t.pendingLocks = 0;
		source.trig(t.lane);

		// The block that sees the trig, the next that sounds it: the voice's first sample must be what the FIR's centre
		// reads at output sample m_now + HitDelay, read - FirDelay = 147 (position of that sample in the ring)
		const int64_t first = BlockSize * (m_stBlock + 1);
		const int64_t rest = Down * (m_now + HitDelay) - FirDelay - Up * first;
		lane.next = first + rest / Up;
		lane.nextVelocity = static_cast<float>(_velocity) / 127.0f;
		t.nextPhase = static_cast<int>(rest % Up);
		// The first output whose newest tap reaches that sample, whatever the phase before it
		t.phaseAt = m_now + HitDelay - Resample48To44::Latency - 1;
	}

	void Engine::cut(const int _track)
	{
		const int64_t at = m_now + HitDelay;
		auto& t = m_tracks[static_cast<size_t>(_track)];
		const uint32_t bit = 1u << _track;
		if(m_mdLive & bit)
		{
			auto& md = m_mdTracks[static_cast<size_t>(_track)];
			// A Hit that waits for its block starts now; the voice's tail, ramped, goes ahead of its blocks, so that the
			// voice is free at once for the Track's next Hit
			if(md.triggered())
			{
				md.takeLocks();
				md.update(*m_library.os, m_tempo, m_voices[static_cast<size_t>(_track)]);
			}
			t.mdRing.fade(at);
			while(t.md.at < at + CutSamples)
				renderVoice(_track, at);
			m_mdLive &= ~bit;
		}
		if(t.lane >= 0)
		{
			auto& lane = sourceOf(t).lanes[static_cast<size_t>(t.lane)];
			if(lane.heir == _track)
			{
				// Its Hit had not begun: the lane goes on as the cut voice it was
				lane.heir = -1;
				lane.next = -1;
			}
			else
			{
				lane.fadeAt = (Down * at - t.phase - FirDelay) / Up;
				t.stRing.fade(lane.fadeAt);
			}
			t.lane = -1;
		}
	}

	void Engine::renderVoice(const int _track, const int64_t _fadeAt)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		md::voice::Voice::Block voice;
		m_voices[static_cast<size_t>(_track)].render(voice);
		float x[BlockSize];
		for(int n = 0; n < BlockSize; ++n)
			x[n] = static_cast<float>(voice[static_cast<size_t>(n)]) * (1.0f / 8388608.0f);
		t.md.write(t.mdRing, x, t.level, _fadeAt);
	}

	void Engine::renderMachinedrum()
	{
		md::voice::prepareBlock(m_mdTracks, m_voices, *m_library.os, m_tempo, m_mdBlock, m_mdMask, m_mdLive);
		for(uint32_t bits = m_mdLive, t = 0; bits; bits >>= 1, ++t)
		{
			if(!(bits & 1))
				continue;
			renderVoice(static_cast<int>(t), Never);
			if(m_tracks[t].md.quiet >= MachinedrumQuietBlocks)
				m_mdLive &= ~(1u << t);
		}
		++m_mdBlock;
	}

	void Engine::renderSyntakt()
	{
		for(size_t s = 0; s < m_sources.size(); ++s)
		{
			auto& source = *m_sources[s];
			if(!source.used)
				continue;
			const Block& block = source.renderBlock();
			for(int l = 0; l < source.width; ++l)
			{
				auto& lane = source.lanes[static_cast<size_t>(l)];
				if(lane.track < 0)
					continue;
				if(lane.skip)
				{
					lane.skip = false;
				}
				else
				{
					float x[BlockSize];
					for(int n = 0; n < BlockSize; ++n)
						x[n] = block.s[n][l];
					auto& t = m_tracks[static_cast<size_t>(lane.track)];
					lane.run.write(t.stRing, x, t.level, lane.fadeAt);
				}
				if(lane.heir >= 0)
				{
					if(m_tracks[static_cast<size_t>(lane.track)].lane < 0)
						m_tracks[static_cast<size_t>(lane.track)].stRing.close();
					lane.track = lane.heir;
					lane.heir = -1;
					lane.fadeAt = Never;
				}
				if(lane.next >= 0)
				{
					lane.run.start(lane.next, lane.nextVelocity, m_tracks[static_cast<size_t>(lane.track)].level);
					lane.next = -1;
				}
				if(lane.run.at >= lane.fadeAt + CutSamples || lane.run.quiet >= SyntaktQuietBlocks)
					freeLane(static_cast<int>(s), l);
			}
		}
		for(auto& t : m_tracks)
			t.pendingLocks = 0;
		++m_stBlock;
		m_stNext = (Up * m_stBlock * BlockSize + Down - 1) / Down;
	}

	void Engine::read(const int _track, float* _out, const int _count)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		if(m_now < t.mdRing.end())
		{
			const int n = static_cast<int>(std::min<int64_t>(_count, t.mdRing.end() - m_now));
			const float* x = t.mdRing.window(m_now);
			for(int i = 0; i < n; ++i)
				_out[i] += x[i];
		}
		const auto& fir = firTable();
		for(int done = 0; done < _count;)
		{
			const int64_t m = m_now + done;
			if(m >= t.phaseAt)
			{
				t.phase = t.nextPhase;
				t.phaseAt = Never;
			}
			const int64_t q = Down * m - t.phase;
			int64_t i = q >= 0 ? q / Up : -1;
			int p = static_cast<int>(q - i * Up);
			if(i - FirTaps >= t.stRing.end())
				return;
			const int n = static_cast<int>(std::min<int64_t>(_count - done, t.phaseAt - m));
			for(int k = 0; k < n; ++k)
			{
				_out[done + k] += dot(t.stRing.window(i - (FirRow - 1)), fir.rows[p]);
				++i;
				p += Down - Up;
				if(p >= Up)
				{
					p -= Up;
					++i;
				}
			}
			done += n;
		}
	}

	void Engine::mix(float* const* _outputs, const int _offset, const int _count)
	{
		alignas(16) float sum[BlockSize];
		for(int track = 0; track < Tracks; ++track)
		{
			auto& t = m_tracks[static_cast<size_t>(track)];
			if(m_now >= t.mdRing.end() && (Down * m_now - t.phase) / Up - FirTaps >= t.stRing.end()
				&& t.phaseAt > m_now + _count)
				continue;
			std::fill_n(sum, _count, 0.0f);
			read(track, sum, _count);
			for(size_t c = 0; c < 2; ++c)
			{
				float* out = _outputs[2 * static_cast<size_t>(t.output) + c];
				if(out)
					out += _offset;
				// The pan moves a full swing in CutSamples, sample by sample whatever the runs
				float g = t.panNow[c];
				const float to = t.pan[c];
				int i = 0;
				for(; i < _count && g != to; ++i)
				{
					if(out)
						out[i] += sum[i] * g;
					const float move = to - g;
					g = std::abs(move) <= 1.0f / CutSamples ? to : g + (move > 0 ? 1.0f : -1.0f) / CutSamples;
				}
				t.panNow[c] = g;
				if(out)
					for(; i < _count; ++i)
						out[i] += sum[i] * g;
			}
		}
	}

	void Engine::render(float* const* _outputs, const int _count)
	{
		const syntakt::fast::FlushDenormals ftz;
		for(int b = 0; b < 2 * Outputs; ++b)
			if(_outputs[b])
				std::fill_n(_outputs[b], _count, 0.0f);
		for(int done = 0; done < _count;)
		{
			if(m_now == m_mdBlock * BlockSize)
				renderMachinedrum();
			if(m_now == m_stNext)
				renderSyntakt();
			const auto run = static_cast<int>(std::min({static_cast<int64_t>(_count - done),
				m_mdBlock * BlockSize - m_now, m_stNext - m_now}));
			mix(_outputs, done, run);
			m_now += run;
			done += run;
		}
	}

	uint32_t Engine::liveTracks() const
	{
		uint32_t live = m_mdLive;
		for(const auto& s : m_sources)
			for(int l = 0; l < Lanes; ++l)
				if(s->used >> l & 1)
					live |= 1u << s->lanes[static_cast<size_t>(l)].track;
		return live;
	}

	int Engine::lanesRendered(const int _machine) const
	{
		const auto& s = *m_sources[static_cast<size_t>(_machine)];
		return s.used ? s.width : 0;
	}
}
