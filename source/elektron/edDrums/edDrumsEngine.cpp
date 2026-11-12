#include "edDrumsEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <type_traits>

#include <emmintrin.h>

#include "edDrumsRate.h"
#include "openDrum.h"
#include "sophieMachine.h"
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
		constexpr int FirDelay = Resample48To44::Latency * Down;		// Latency, in 1/147 of a 48 kHz sample
		static_assert(Down % Up == 13 && Down / Up == 1, "a host sample steps the read by 1 sample and 13 phases");

		// A machine's neutral sound until a Kit gives it one, with or without a flash image, so that a set sounds the same:
		// no default is a firmware's. Its knobs in the middle, a Syntakt machine's PUNCH and OVERDRIVE off (and its HOLD, in
		// Machine 9)
		constexpr std::array<uint8_t, 8> SyntaktDefaults{64, 64, 0, 64, 64, 64, 64, 0};
		constexpr std::array<uint8_t, 8> MachinedrumDefaults{64, 64, 64, 64, 64, 64, 64, 64};
		constexpr int VoiceHold = 6;		// a Syntakt voice's index of HOLD: on, every machine's voice waits for its gate-off

		// The voice's index each of a Syntakt machine's Machine 1-9 plays (syntaktSlotOf), internal id 18 + index: the SYN
		// page's TUNE 18, SWEEP or SHIMMER 21, PUNCH 23, DECAY 25, the machine's two 19 and 20, MOD ENV 22, in the order of
		// section 3's descriptors as syntakt::Syntakt names the ids; OVERDRIVE, 26, none (the stage after the voice); HOLD 24
		constexpr int OverSlot = 7, HoldSlot = 8;
		constexpr std::array<int, 9> SyntaktIndex{0, 3, 5, 7, 1, 2, 4, -1, VoiceHold};

		// Whether Machine _index + 1 changes what _machine plays: the slots it uses, the Sampler's first 8 here (its 9-12
		// read where they play), a Syntakt machine's Machine 9, its HOLD
		bool playsSlot(const Machine& _machine, const int _index)
		{
			if(_machine.family == Family::Syntakt && _index == HoldSlot)
				return true;
			return _index < machineParameters(_machine) && !(_machine == Sampler && _index >= 8);
		}
		// The Sampler plays its whole sample at its pitch: PTCH in the middle, no decay, STRT 0, END 127, no retrigger
		constexpr std::array<uint8_t, 8> SamplerDefaults{64, 127, 0, 0, 0, 127, 0, 64};

		// Where Track t's sample lies in the ROM player's bank, past the factory's, and the entry of ROM-33 + t
		constexpr uint32_t SamplerAddr = 0x190000;
		constexpr uint32_t SamplerEntry = 48;
		constexpr uint8_t Rom33 = 176;
		static_assert(SamplerAddr + Tracks * Sample::SlotWords <= 0x200000, "the voice DSP's 2M words hold the slots");

		// A new Sound of these starts in ADSR, its voice held: SY BITS, as SY TONE, SY CHORD and SY DUAL VCO will
		bool startsSustained(const Machine& _machine)
		{
			return _machine.id == 7;
		}

		// The latest a Syntakt note can wait, in 1/147 of a 48 kHz sample: the block that sees its trig, at most 142 / 147
		// of a block past 32 samples ahead of the note; the block that sounds it; the longest chain; the FIR's Latency
		constexpr int64_t SyntaktWait = BlockSize * 142 + Up * BlockSize + Up * Chain::MaxDelay + FirDelay;

		// A voice reaches 1 % of its peak in its first samples, all but these, measured by edDrumsEngineTest with the
		// OS's defaults: TRX-BD and TRX-S2 sound a block after their trigger, TRX-XT later still, the ROM machines' filters take 8.
		// TRX-CB, TRX-XC, TRX-MA and P-I-ML are later than a Hit can wait, their own sound; a Syntakt machine starts in
		// its block, up to 7 samples in. As much as a Hit holds its voice back for, in its own samples
		constexpr int LatestStart = 38;

		int voiceStart(const Machine& _machine)
		{
			// SOPHIE's filter comes last: as a low pass, every factory sound's, its first sample is 0
			if(_machine.family == Family::Sophie)
				return 1;
			if(_machine.family != Family::Machinedrum)
				return 0;
			const uint8_t id = _machine == Sampler ? Rom33 : _machine.id;
			return id == 16 ? 34 : id == 18 ? LatestStart : id == 29 ? 33 : (id >= 128 && id < 160) || (id >= 176 && id < 192) ? 8 : 0;
		}

		// A ring's gain is the last Hit's, and a cut voice goes on under it before the chain: what keeps the tail as loud as it
		// was. Never above 1: a louder tail under a soft Hit (2 here) adds 1.9 dB of HF to a plain 127 to 64, and takes back
		// none of 127 to 1
		float tailGain(const float _old, const float _new)
		{
			return std::min(_old / _new, 1.0f);
		}

		float cutGain(const int64_t _n)
		{
			return std::clamp(1.0f - static_cast<float>(_n + 1) / static_cast<float>(Engine::CutSamples), 0.0f, 1.0f);
		}

		float gainOf(const float _value)
		{
			const float g = _value / 127.0f;
			return g * g;
		}

		std::array<float, 2> panOf(const float _pan)
		{
			return {std::min(1.0f, (127.0f - _pan) / 63.0f), std::min(1.0f, _pan / 64.0f)};
		}

		float moved(const int _value, const int _offset)
		{
			return static_cast<float>(wordOf(_value, _offset)) / 128.0f;
		}

		// _y[i] += _x[i] _a _b for i under _n, four at a time: the mix's steady gains away from 44.1 kHz, which run for every
		// Track, output and send at every host sample. Not at 44.1 kHz: the compiler's loop there may fuse a multiply and
		// an add, which this does not, and 44.1 kHz renders what it did before
		inline void addScaled(float* _y, const float* _x, const float _a, const float _b, const int _n)
		{
			const __m128 a = _mm_set1_ps(_a), b = _mm_set1_ps(_b);
			int i = 0;
			for(; i + 4 <= _n; i += 4)
				_mm_storeu_ps(_y + i, _mm_add_ps(_mm_loadu_ps(_y + i), _mm_mul_ps(_mm_mul_ps(_mm_loadu_ps(_x + i), a), b)));
			for(; i < _n; ++i)
				_y[i] += _x[i] * _a * _b;
		}

		inline void addScaled(float* _y, const float* _x, const float _a, const int _n)
		{
			const __m128 a = _mm_set1_ps(_a);
			int i = 0;
			for(; i + 4 <= _n; i += 4)
				_mm_storeu_ps(_y + i, _mm_add_ps(_mm_loadu_ps(_y + i), _mm_mul_ps(_mm_loadu_ps(_x + i), a)));
			for(; i < _n; ++i)
				_y[i] += _x[i] * _a;
		}

		// A sum over the taps of one Track, oldest first
		inline float dot(const float* _x, const float* _h)
		{
			static_assert(FirTaps % 4 == 0, "whole vectors");
			__m128 sum = _mm_setzero_ps();
			for(int k = 0; k < FirTaps; k += 4)
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
		case Family::OpenDrum:		return _machine.id < OpenDrumMachines;
		case Family::Sophie:		return _machine.id < SophieMachines;
		case Family::SyntaktAnalog:	return _machine.id < SyntaktAnalogMachines;
		case Family::Machinedrum:
			// ROM-33 to -48's entries are the Sampler's
			return _machine == Sampler || (md::voice::playable(_machine.id) && (_machine.id < Rom33 || _machine.id >= Rom33 + 16));
		}
		return false;
	}

	MachineData dataOf(const Library& _library, const Machine& _machine)
	{
		if(_machine == Sampler)
			return MachineData::Whole;
		if(!playable(_machine))
			return MachineData::None;
		if(_machine.family == Family::Machinedrum)
			return _library.mdData[_machine.id];
		const bool twinshot = _machine.family == Family::Syntakt && _machine.id == SyntaktTwinshot;
		return twinshot && !_library.twinshot ? MachineData::None : MachineData::Whole;
	}

	int machineParameters(const Machine& _machine)
	{
		if(_machine.family == Family::Off || !playable(_machine))
			return 0;
		if(_machine.family == Family::Sophie)
			return sophie::MachineSlots;
		if(_machine.family == Family::SyntaktAnalog)
			return 8;
		if(_machine == Sampler)
			return vintage::SamplerParameters;
		return _machine.family == Family::OpenDrum ? openDrum_machineParameters(_machine.id) : 8;
	}

	int syntaktSlotOf(const int _index)
	{
		return static_cast<int>(std::find(SyntaktIndex.begin(), SyntaktIndex.end(), _index) - SyntaktIndex.begin());
	}

	const char* machineParameterName(const Machine& _machine, const int _index)
	{
		if(!playable(_machine) || _index < 0 || _index >= MachineParameters)
			return nullptr;
		if(_machine.family == Family::Sophie)
			return _index < sophie::MachineSlots ? sophie::MachineNames[static_cast<size_t>(_index)] : nullptr;
		if(_machine.family != Family::OpenDrum)
			return nullptr;
		return openDrum_machineParameterName(_machine.id, _index);
	}

	std::array<uint8_t, MachineParameters> allDefaults(const Machine& _machine)
	{
		std::array<uint8_t, MachineParameters> d{};
		if(_machine.family == Family::SyntaktAnalog)
		{
			// our own, by ear (edDrumsAnalogVoice.h)
			const auto a = analog::defaultsOf(static_cast<analog::Machine>(_machine.id));
			std::copy(a.begin(), a.end(), d.begin());
			return d;
		}
		if(_machine.family == Family::Sophie)
		{
			// Factory pad 1, the kick, its body on: soundValues(0), which the test checks, without its search
			std::copy(sophie::MachineDefaults.begin(), sophie::MachineDefaults.end(), d.begin());
			d[sophie::Body] = 127;
			return d;
		}
		if(_machine.family == Family::OpenDrum)
		{
			// The voice's init kit
			for(int i = 0; i < MachineParameters; ++i)
				d[static_cast<size_t>(i)] = openDrum_machineDefault(_machine.id, i);
			return d;
		}
		const auto& first = _machine == Sampler ? SamplerDefaults
			: _machine.family == Family::Machinedrum ? MachinedrumDefaults : SyntaktDefaults;
		std::copy(first.begin(), first.end(), d.begin());
		if(_machine == Sampler)
		{
			// The ROM player's grain; VINTAGE's at 8 bits, companded, its low-pass at 9.9 kHz
			d[vintage::BitsSlot] = 40;
			d[vintage::CompandingSlot] = 127;
			d[vintage::FilterSlot] = 110;
		}
		return d;
	}

	std::array<uint8_t, 8> defaults(const Machine& _machine)
	{
		const auto all = allDefaults(_machine);
		std::array<uint8_t, 8> d{};
		std::copy_n(all.begin(), d.size(), d.begin());
		return d;
	}

	// ---- a Syntakt machine, whichever it is ----

	struct Engine::Source
	{
		virtual ~Source() = default;
		virtual void setLanes(int _lanes) = 0;
		virtual void setNote(int _lane, int _note) = 0;
		virtual void setParameter(int _lane, int _index, uint8_t _value, uint8_t _fine) = 0;
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
			void setParameter(const int _lane, const int _index, const uint8_t _value, const uint8_t _fine) override
			{
				m.setParameter(_lane, _index, _value, _fine);
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
		quiet = peak < Silence && !voiced ? quiet + 1 : 0;
		voiced = false;
	}

	// ---- the engine ----

	struct Engine::SophieVoices
	{
		std::array<sophie_t, Tracks> v;
	};

	struct Engine::AnalogVoices
	{
		std::array<analog::Voice, Tracks> v;
	};

	namespace
	{
		// Engine::hitDelay away from 44.1 kHz, at a whole rate _rate. The Machinedrum: a note waits for its block, which
		// starts up to a block after its time and one sample more, the latest voice and the longest chain, and the FIR's half
		// length; its position rounded down to a whole sample. The Syntakt: the block that sees its trig, up to a block after
		// its time, the block that sounds it, the longest chain, the FIR's half length where it has one
		constexpr int hitDelayAt(const int64_t _rate)
		{
			const auto ceilDiv = [](const int64_t _a, const int64_t _b) { return static_cast<int>((_a + _b - 1) / _b); };
			const int64_t md = std::gcd(_rate, int64_t{44100}), mdUp = _rate / md, mdDown = 44100 / md;
			const int mdWait = ceilDiv(mdUp * (BlockSize + 1 + LatestStart + Chain::MaxDelay + RateFir::Taps / 2) - 1, mdDown);
			const int64_t st = std::gcd(_rate, int64_t{48000}), stUp = _rate / st, stDown = 48000 / st;
			const int64_t stDelay = stUp == stDown ? 0 : stUp * RateFir::Taps / 2;
			const int stWait = ceilDiv(stDelay + stUp * (2 * BlockSize + Chain::MaxDelay), stDown);
			return mdWait > stWait ? mdWait : stWait;
		}

		// The rings hold it all at every rate: twice the lead a Hit is written at, in each family's samples, its blocks, a
		// cut's ramp and a window of the FIR, as 44.1 kHz's static_assert in the constructor has it
		constexpr bool ringsHold(const int64_t _rate)
		{
			const int64_t delay = _rate == 44100 ? Engine::HitDelay : hitDelayAt(_rate);
			const int64_t md = (delay * 44100 + _rate - 1) / _rate, st = (delay * 48000 + _rate - 1) / _rate;
			return 2 * (md > st ? md : st) + 2 * BlockSize + Engine::CutSamples + RateFir::Taps < 512;
		}
		static_assert(ringsHold(44100) && ringsHold(48000) && ringsHold(88200) && ringsHold(96000) && ringsHold(192000),
			"the rings hold a Hit's lead at every rate");
		static_assert(Engine::hostBlockOf(192000) <= SendsAtRate::MaxHost, "a run fits the mix's sum, a host block the sends'");

		// phaseFrom gives 44.1 kHz's Syntakt the phase as before, its Latency and chainDelay as 48 kHz samples, while
		// ceil(147 c / 160) is c: for every chain delay c up to 12
		static_assert(13 * Chain::MaxDelay <= Down - 1, "phaseFrom's 44.1 kHz phase as before");
	}

	int Engine::hitDelay(const double _rate)
	{
		if(!playableRate(_rate))
			throw std::invalid_argument("the engine plays at 44.1, 48, 88.2, 96 or 192 kHz");
		return _rate == SampleRate ? HitDelay : hitDelayAt(std::lround(_rate));
	}

	Engine::Engine(const Library& _library, const double _rate)
		: m_library(_library), m_rate(_rate), m_hitDelay(hitDelay(_rate)), m_hostBlock(hostBlockOf(_rate)), m_shared(std::make_unique<md::voice::Shared>()),
		m_mdChain(std::make_unique<Chain>(SampleRate, false)), m_mdIn(std::make_unique<Block>()),
		m_stChain(std::make_unique<Chain>(syntakt::fast::MachineRate, true)), m_stIn(std::make_unique<Block>())
	{
		static_assert(HitDelay == BlockSize - 1 + LatestStart + Chain::MaxDelay,
			"a Machinedrum Hit waits its block, the latest voice and its chain");
		static_assert(HitDelay * Down >= SyntaktWait, "a Syntakt Hit waits its blocks and the FIR");
		static_assert(2 * HitDelay + 2 * BlockSize + CutSamples + Ring::Mirror < Ring::Size, "the rings hold it all");
		static_assert(Ring::Size == 512 && Ring::Mirror == RateFir::Taps, "ringsHold's ring, at every rate");
		static_assert(FirTaps == Ring::Mirror, "a window of the ring is a row of the FIR");

		m_shared->tables = &m_library.rom;
		// The bank is the engine's, the Library's words copied, so that each Track's sample has its slot
		using md::voice::RomBank;
		m_directory.assign(RomBank::DirectoryWords, 0);
		std::copy(m_library.directory.begin(), m_library.directory.end(), m_directory.begin());
		m_bankData.assign(SamplerAddr - RomBank::DataAddr + Tracks * Sample::SlotWords, 0);
		std::copy_n(m_library.samples.begin(), std::min<size_t>(m_library.samples.size(), SamplerAddr - RomBank::DataAddr),
			m_bankData.begin());
		for(int t = 0; t < Tracks; ++t)		// ROM-33 to -48's entries are the Tracks' samples, none yet
			setSample(t, nullptr);
		m_bank = {m_directory.data(), m_bankData.data(), m_bankData.size()};
		m_shared->bank = &m_bank;
		m_shared->trx = &m_library.trx;
		m_shared->e12 = m_library.e12.empty() ? nullptr : m_library.e12.data();
		m_shared->pi = m_library.pi.data();
		m_sends = std::make_unique<Sends>(*m_library.os, m_library.trx.sine.data());
		m_master = std::make_unique<Master>(m_rate, m_hostBlock);

		// What the host's rate changes: at 44.1 kHz nothing, the Syntakt read through Resample48To44 as before
		static_assert(RateFir::Taps == FirTaps, "a RateFir's row is a window of the ring");
		m_stRead = {Up, Down, FirDelay, &Resample48To44::Table[0][0], 1.0 / Up};
		if(m_rate != SampleRate)
		{
			m_mdFir = std::make_unique<RateFir>(SampleRate, m_rate);
			m_mdRead = {m_mdFir->up(), m_mdFir->down(), m_mdFir->delay(), m_mdFir->row(0), 1.0 / m_mdFir->up()};
			m_stRead = {};
			if(m_rate != syntakt::fast::MachineRate)
			{
				// At a whole multiple of 48 kHz a half-band, a cutoff at 24 kHz, whose first phase is the Syntakt's own
				// sample: what lies past 20.5 kHz is the Syntakt's at 48 kHz, its images from 27.5 kHz on down by 80 dB
				const bool halfBand = std::lround(m_rate) % 48000 == 0;
				m_stFir = std::make_unique<RateFir>(syntakt::fast::MachineRate, m_rate, RateFir::Taps,
					halfBand ? syntakt::fast::MachineRate / 2 : RateFir::Cutoff);
				m_stRead = {m_stFir->up(), m_stFir->down(), m_stFir->delay(), m_stFir->row(0), 1.0 / m_stFir->up(),
					halfBand ? m_stFir->units() : nullptr};
			}
			m_bridge = std::make_unique<SendsAtRate>(*m_sends, m_rate, m_hostBlock);
			m_mdRead.fast = m_stRead.fast = rate::fastFir();
			m_ramp = static_cast<float>(SampleRate / (CutSamples * m_rate));
			m_cutHost = static_cast<int>(std::lround(CutSamples * m_rate / SampleRate));
			m_quartersPerSample = 120.0 / 60 / m_rate;
		}
		m_sends->setTempo(m_tempo);
		m_voices.reserve(Tracks);
		for(uint32_t t = 0; t < Tracks; ++t)
			m_voices.emplace_back(*m_shared, t);
		static_assert(OpenDrumBlock == 2 * BlockSize, "an OPEN DRUM block is two of the engine's");
		const size_t odWords = (openDrum_size() + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t);
		m_odMemory.resize(odWords * Tracks);
		for(size_t t = 0; t < Tracks; ++t)
			m_tracks[t].od = reinterpret_cast<OpenDrumVoice*>(m_odMemory.data() + t * odWords);
		m_sophie = std::make_unique<SophieVoices>();
		m_analog = std::make_unique<AnalogVoices>();
		analog::warmUp();	// every table the analog port builds, here rather than on the audio thread at a first Hit

		using namespace syntakt::fast;
		makeSources<Source, BdModern, SdBasic, CyAlloy, PcCarbon, SdVintage, CpVintage, SyToy, SyBits, SpTwinshot>(m_sources,
			m_library.twinshot);
		for(auto& s : m_sources)
			s->fitWidth();
		m_stOver = std::make_unique<TrackDrive>();

		setOversampling(DefaultOversampling);
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
		t.params = allDefaults(_machine);
		const uint32_t bit = 1u << _track;
		if(_machine.family != Family::Off && _machine.family != t.chainFamily)
		{
			// The LFOs' moves go with the Track: the new chain takes them from what the old one leaves
			for(int target = static_cast<int>(LfoTarget::DriveA); target <= static_cast<int>(LfoTarget::EnvelopeDepth); ++target)
			{
				chainOf(t).modulate(_track, chainTarget(static_cast<LfoTarget>(target)), 0);
				if(t.taken[static_cast<size_t>(target)] || m_lfos.offset(_track, target))
					m_lfos.wait(_track, target);
			}
			t.chainFamily = _machine.family;
			chainOf(t).setNote(_track, t.note);
			chainOf(t).configure(_track, _machine.family == Family::Syntakt ? syntaktChain(startsSustained(_machine)) : machinedrumChain());
			t.held = chainOf(t).holdsVoice(_track);
		}
		m_stMask = _machine.family == Family::Syntakt ? m_stMask | bit : m_stMask & ~bit;
		// OVERDRIVE at the new machine's, off but on a Syntakt one, ramping as any word of the stage does
		setOver(_track, 0);
		if(_machine.family == Family::Syntakt)
			playSyntakt(_track, OverSlot);
		m_mdMask &= ~bit;
		if(_machine.family == Family::Machinedrum)
		{
			auto& md = m_mdTracks[static_cast<size_t>(_track)];
			md.setMachine(_machine == Sampler ? static_cast<uint8_t>(Rom33 + _track) : _machine.id, defaults(_machine));
			md.clearMachineLocks();
			t.vt.stop();
			t.vtTrig = false;
			// What it took while on no Machinedrum machine went nowhere
			for(size_t i = 0; i < 8; ++i)
				md.modulate(i, t.taken[static_cast<size_t>(LfoTarget::Machine1) + i]);
			m_mdMask |= bit;
		}
		m_odMask &= ~bit;
		if(_machine.family == Family::OpenDrum)
		{
			openDrum_init(t.od, _machine.id);
			t.odPending = t.odTrig = t.odOpen = false;
			for(int i = 0; i < MachineParameters; ++i)
				playOpenDrum(_track, i);
			m_odMask |= bit;
		}
		m_spMask &= ~bit;
		if(_machine.family == Family::Sophie)
		{
			sophie_init(&m_sophie->v[static_cast<size_t>(_track)], static_cast<int>(SampleRate), 0);
			t.spTrig = false;
			for(int i = 0; i < sophie::MachineSlots; ++i)
				playSophie(_track, i);
			m_spMask |= bit;
		}
		m_anMask &= ~bit;
		if(_machine.family == Family::SyntaktAnalog)
		{
			m_analog->v[static_cast<size_t>(_track)].setMachine(static_cast<analog::Machine>(_machine.id));
			t.anTrig = false;
			for(int i = 0; i < 8; ++i)
				playAnalog(_track, i);
			m_anMask |= bit;
		}
	}

	void Engine::setSample(const int _track, const Sample* _sample)
	{
		if(m_tracks[static_cast<size_t>(_track)].machine == Sampler)
			cut(_track);	// renders what the old voice has left ahead: its words can go
		const uint32_t entry = 4 * (SamplerEntry + static_cast<uint32_t>(_track));
		const uint32_t addr = SamplerAddr + static_cast<uint32_t>(_track) * Sample::SlotWords;
		if(_sample && _sample->factory >= 1 && _sample->factory <= FactorySlots)
		{
			// A factory sample plays where the bank holds it, the ROM machine's own entry: the same words, loop and rate
			const auto from = 4 * static_cast<size_t>(_sample->factory - 1);
			std::copy_n(m_directory.begin() + static_cast<std::ptrdiff_t>(from), 4, m_directory.begin() + entry);
		}
		else
		{
			auto* slot = m_bankData.data() + (addr - md::voice::RomBank::DataAddr);
			const size_t words = _sample ? std::min<size_t>(_sample->words.size(), Sample::MaxWords) : 0;
			if(words)
				std::copy_n(_sample->words.begin(), words, slot);
			std::fill_n(slot + words, Sample::PadWords, 0u);
			m_directory[entry] = addr;
			m_directory[entry + 1] = words ? static_cast<uint32_t>(std::min<size_t>(_sample->pcm.size(), 2 * words)) : 0;
			m_directory[entry + 2] = 0xffffff;		// no loop
			m_directory[entry + 3] = words ? _sample->rateWord : 0;
		}

		// The VINTAGE grain's, its values whole where they lie: the cut voice's tail is rendered, nothing reads the old ones
		const auto t = static_cast<size_t>(_track);
		const auto* source = _sample ? _sample->source.get() : nullptr;
		m_vtSource[t] = source && !source->empty() ? source->data() : nullptr;
		m_vtLength[t] = m_vtSource[t] ? static_cast<uint32_t>(std::min<size_t>(source->size(), Sample::MaxSourceLength)) : 0;
		m_vtRate[t] = _sample ? _sample->rate : SampleRate;
	}

	void Engine::setParameter(const int _track, const int _index, const uint8_t _value)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		// Another grain: what the old one plays ramps out
		if(t.machine == Sampler && _index == vintage::GrainSlot
			&& vintage::isVintage(_value) != vintage::isVintage(t.params[vintage::GrainSlot]))
			cut(_track);
		t.params[static_cast<size_t>(_index)] = _value;
		t.lockValue[static_cast<size_t>(_index)] = _value;
		// Machine 9-24 of a machine of 8 are kept, and change nothing; the Sampler's 9-12 are read where they play
		if(!playsSlot(t.machine, _index))
			return;
		if(t.machine.family == Family::Machinedrum)
			m_mdTracks[static_cast<size_t>(_track)].knob(static_cast<size_t>(_index), _value);
		else if(t.machine.family == Family::OpenDrum)
			playOpenDrum(_track, _index);
		else if(t.machine.family == Family::Sophie)
			playSophie(_track, _index);
		else if(t.machine.family == Family::SyntaktAnalog)
			playAnalog(_track, _index);
		else
			playSyntakt(_track, _index);
	}

	void Engine::setNote(const int _track, const int _note)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		if(t.note == _note)
			return;
		t.note = _note;
		if(t.lane >= 0)
			sourceOf(t).setNote(t.lane, _note);
		chainOf(t).setNote(_track, _note);
	}

	void Engine::setLevel(const int _track, const int _level)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		t.levelValue = _level;
		t.level = gainOf(moved(_level, t.taken[static_cast<size_t>(LfoTarget::Level)]));
	}

	void Engine::setPan(const int _track, const int _pan)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		t.panValue = _pan;
		t.pan = panOf(moved(_pan, t.taken[static_cast<size_t>(LfoTarget::Pan)]));
	}

	void Engine::setOutput(const int _track, const int _output)
	{
		m_tracks[static_cast<size_t>(_track)].output = std::clamp(_output, 0, Outputs - 1);
	}

	void Engine::setSend(const int _track, const Send _send, const int _level)
	{
		const float g = static_cast<float>(std::clamp(_level, 0, 127)) / 127.0f;
		m_tracks[static_cast<size_t>(_track)].send[static_cast<size_t>(_send)] = g * g;
	}

	void Engine::setTempo(const double _bpm)
	{
		m_tempo = static_cast<uint32_t>(std::lround(std::clamp(_bpm, 30.0, 300.0) * 24.0));
		m_sends->setTempo(m_tempo);
		m_sends->setBpm(std::clamp(_bpm, 1.0, 1000.0));
		const double perSample = std::clamp(_bpm, 1.0, 1000.0) / 60 / m_rate;
		if(perSample == m_quartersPerSample)
			return;
		m_quarters = position();
		m_quartersAt = m_now;
		m_quartersPerSample = perSample;
	}

	double Engine::position() const
	{
		return m_quarters + static_cast<double>(m_now - m_quartersAt) * m_quartersPerSample;
	}

	void Engine::setPosition(const double _quarters)
	{
		if(std::abs(_quarters - position()) <= PositionTolerance)
			return;
		m_quarters = _quarters;
		m_quartersAt = m_now;
	}

	void Engine::setLfo(const int _track, const int _index, const LfoSettings& _settings)
	{
		m_lfos.set(_track, _index, _settings);
	}

	double Engine::lfoPhase(const int _track, const int _index) const
	{
		return m_lfos.phase(_track, _index);
	}

	void Engine::playSyntakt(const int _track, const int _slot)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		// What the LFOs aimed at it add
		const int offset = t.taken[static_cast<size_t>(machineTarget(_slot))];
		const int value = playedValue(t, _slot);
		if(_slot == OverSlot)
		{
			setOver(_track, wordOf(value, offset) << 1);
			return;
		}
		if(t.lane < 0)
			return;
		const int index = SyntaktIndex[static_cast<size_t>(_slot)];
		const int word = index == VoiceHold && t.held ? 127 << 7 : wordOf(value, offset);
		sourceOf(t).setParameter(t.lane, index, static_cast<uint8_t>(word >> 7), static_cast<uint8_t>((word & 127) << 1));
	}

	void Engine::setOver(const int _track, const int _word)
	{
		// The same word again moves nothing: the stage need not run for it
		auto& word = m_overWord[static_cast<size_t>(_track)];
		if(_word == word)
			return;
		word = _word;
		m_stOver->set(_track, _word);
		const uint32_t bit = 1u << _track;
		m_overOn = _word > 0 ? m_overOn | bit : m_overOn & ~bit;
		m_overRun |= bit;
	}

	void Engine::takeMoves(const int _track, const bool _settle)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		for(uint64_t bits = m_lfos.waiting(_track), p = 0; bits; bits >>= 1, ++p)
		{
			if(!(bits & 1))
				continue;
			const auto target = static_cast<LfoTarget>(p);
			const int sum = m_lfos.offset(_track, static_cast<int>(p));
			const int limit = isChainTarget(target) ? chainOf(t).moveLimit(_track, chainTarget(target)) : LfoMove;
			const int offset = t.taken[p] = _settle ? sum : t.taken[p] + std::clamp(sum - t.taken[p], -limit, limit);
			if(offset == sum)
				m_lfos.taken(_track, static_cast<int>(p));
			if(const int index = machineOf(target); index >= 0)
			{
				// A Machine its machine does not read moves nothing; the Sampler's VINTAGE grain reads its 9-12 where they
				// play (renderVintage)
				if(!playsSlot(t.machine, index))
					continue;
				if(t.machine.family == Family::Machinedrum)
					m_mdTracks[static_cast<size_t>(_track)].modulate(static_cast<size_t>(index), offset);
				else if(t.machine.family == Family::OpenDrum)
					playOpenDrum(_track, index);
				else if(t.machine.family == Family::Sophie)
					playSophie(_track, index);
				else if(t.machine.family == Family::SyntaktAnalog)
					playAnalog(_track, index);
				else if(t.machine.family == Family::Syntakt)
					playSyntakt(_track, index);
			}
			else if(target == LfoTarget::Level)
			{
				setLevel(_track, t.levelValue);
			}
			else if(target == LfoTarget::Pan)
			{
				setPan(_track, t.panValue);
			}
			else
			{
				chainOf(t).modulate(_track, chainTarget(target), offset);
			}
		}
	}

	void Engine::modulate(const uint32_t _tracks, const double _seconds)
	{
		m_lfos.step(_tracks, position(), _seconds * m_rate * m_quartersPerSample, _seconds);
		for(uint32_t bits = m_lfos.waiting() & _tracks, t = 0; bits; bits >>= 1, ++t)
			if(bits & 1)
				takeMoves(static_cast<int>(t));
	}

	void Engine::setChain(const int _track, const ChainSettings& _settings)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		chainOf(t).configure(_track, _settings);
		t.held = chainOf(t).holdsVoice(_track);
		if(t.machine.family == Family::Syntakt)
			playSyntakt(_track, HoldSlot);
	}

	void Engine::setOversampling(const int _factor)
	{
		m_mdChain->setOversampling(_factor);
		m_stChain->setOversampling(_factor);
	}

	void Engine::setAccent(const int _track, const int _amount)
	{
		m_tracks[static_cast<size_t>(_track)].accent = _amount;
	}

	void Engine::setChokes(const int _track, const uint32_t _targets)
	{
		m_tracks[static_cast<size_t>(_track)].chokes = _targets & ~(1u << _track);
	}

	void Engine::lock(const int _track, const int _index, const uint8_t _value)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		if(!playsSlot(t.machine, _index))
			return;
		if(t.machine.family == Family::Machinedrum)
		{
			m_mdTracks[static_cast<size_t>(_track)].lock(static_cast<size_t>(_index), _value);
			return;
		}
		t.pendingLocks |= 1u << _index;
		t.lockPendingValue[static_cast<size_t>(_index)] = _value;
	}

	uint8_t Engine::played(const int _track, const int _index) const
	{
		const auto& t = m_tracks[static_cast<size_t>(_track)];
		// A Machinedrum machine's locks are its voice's Track's
		if(t.machine.family == Family::Machinedrum && playsSlot(t.machine, _index))
			return m_mdTracks[static_cast<size_t>(_track)].target(static_cast<size_t>(_index));
		return static_cast<uint8_t>(playedValue(t, _index));
	}

	void Engine::noteOn(const int _track, const int _velocity, const bool _accent)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		for(uint32_t bits = t.chokes, target = 0; bits; bits >>= 1, ++target)
			if(bits & 1)
				cut(static_cast<int>(target));
		m_lfos.trig(_track, position());
		// What is aimed at the Track plays from the Hit where it stands, not where its ramp has got to
		if(m_lfos.waiting(_track))
			takeMoves(_track, true);
		const float gain = static_cast<float>(_accent ? 128 + 2 * t.accent : _velocity) / 127.0f;
		if(t.machine.family == Family::Machinedrum || t.machine.family == Family::OpenDrum
			|| t.machine.family == Family::Sophie || t.machine.family == Family::SyntaktAnalog)
			hitMachinedrum(_track, _velocity, gain);
		else if(t.machine.family == Family::Syntakt)
			hitSyntakt(_track, _velocity, gain);
	}

	void Engine::noteOff(const int _track)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		// A note that ends in the block its Hit waits for ends in the block after, as the port's length counter has it: that
		// block the machine and the amp would both drop its gate-off
		if(t.stNext >= 0)
		{
			t.stGateOff = true;
			return;
		}
		if(t.lane >= 0)
			sourceOf(t).gateOff(t.lane);
		chainOf(t).gateOff(_track);
	}

	void Engine::hitMachinedrum(const int _track, const int _velocity, const float _gain)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		if(t.machine.family == Family::OpenDrum || t.machine.family == Family::Sophie || t.machine.family == Family::SyntaktAnalog)
		{
			// The Hit's locks, and the Kit's value back on what the last Hit locked; the voice starts at the next block
			const bool sophie = t.machine.family == Family::Sophie;
			const bool analogFamily = t.machine.family == Family::SyntaktAnalog;
			const uint32_t changed = t.lockedNow | t.pendingLocks;
			t.lockedNow = t.pendingLocks;
			t.pendingLocks = 0;
			t.lockValue = t.lockPendingValue;
			for(int i = 0; i < MachineParameters; ++i)
				if(changed >> i & 1)
				{
					if(analogFamily)
						playAnalog(_track, i);
					else if(sophie)
						playSophie(_track, i);
					else
						playOpenDrum(_track, i);
				}
			if(analogFamily)
			{
				t.anTrig = true;
			}
			else if(sophie)
			{
				t.spTrig = true;
			}
			else
			{
				t.odTrig = true;
				t.odSettle = !(m_mdLive >> _track & 1);
			}
		}
		else
		{
			m_mdTracks[static_cast<size_t>(_track)].hit();
			m_mdTracks[static_cast<size_t>(_track)].alignTicks(m_mdBlock);
			t.vtTrig = vintageGrain(t);
		}
		m_mdChain->hit(_track, _velocity);
		// The voice's first sample lands HitDelay after the note, wherever its block falls. The VINTAGE grain's sounds from
		// its block's first sample
		const int start = vintageGrain(t) ? 0 : voiceStart(t.machine);
		const int64_t sounds = m_now + m_hitDelay;
		int64_t at = sounds - start - m_mdChain->delay(_track);
		if(m_mdRead.table)
		{
			// Away from 44.1 kHz the voice's first sample out of its chain is what the FIR places at that host sample, at
			// the phase that puts it there, as a Syntakt Hit is placed (hitSyntakt); the old phase reads on up to the first
			// output whose newest tap reaches this voice's first block
			const int ahead = start + m_mdChain->delay(_track);
			const int64_t q = m_mdRead.down * sounds - m_mdRead.delay;
			at = q / m_mdRead.up - ahead;
			t.mdNextPhase = static_cast<int>(q % m_mdRead.up);
			t.mdPhaseAt = phaseFrom(m_mdRead, sounds, ahead);
			t.mdFade = phaseFade(t.mdPhaseAt);
		}
		t.mdHitSkip = m_mdChain->pureDelay(_track);
		const float tail = tailGain(t.md.velocity, _gain);
		t.md.start(at, _gain, t.level);

		// A cut voice goes on under this one: what remains of its ramp, before the chain from here
		if(t.mdFadeAt != Never)
		{
			for(int b = 0; b < t.mdTailBlocks; ++b)
				for(int n = 0; n < BlockSize; ++n)
					t.mdTail[b][n] *= tail * cutGain(at + b * BlockSize + n - t.mdFadeAt);
			t.mdFadeAt = Never;
		}
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
			++t.stVoices;
		}
		t.lane = lane;
		for(int i = 0; i <= HoldSlot; ++i)
			playSyntakt(_track, i);
		source.setNote(lane, t.note);
	}

	void Engine::freeLane(const int _source, const int _lane)
	{
		auto& source = *m_sources[static_cast<size_t>(_source)];
		auto& lane = source.lanes[static_cast<size_t>(_lane)];
		auto& t = m_tracks[static_cast<size_t>(lane.track)];
		if(lane.fadeAt == Never)
			t.lane = -1;
		releaseVoice(lane.track);
		lane.track = -1;
		source.used &= ~(1u << _lane);
		source.fitWidth();
	}

	void Engine::releaseVoice(const int _track)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		if(--t.stVoices > 0)
			return;
		t.stRing.close();
		m_stChain->clear(_track);
		m_stOver->clear(_track);
		t.stSounding = false;
		t.stFadeAt = Never;
	}

	void Engine::hitSyntakt(const int _track, const int _velocity, const float _gain)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		if(t.lane < 0)
			takeLane(_track);
		auto& source = sourceOf(t);

		// The Hit's locks, and the Kit's value back on what the last Hit locked
		const uint32_t changed = t.lockedNow | t.pendingLocks;
		t.lockedNow = t.pendingLocks;
		t.pendingLocks = 0;
		t.lockValue = t.lockPendingValue;
		for(int i = 0; i <= HoldSlot; ++i)
			if(changed >> i & 1)
				playSyntakt(_track, i);
		source.trig(t.lane);

		// The block that sees the trig, the next that sounds it: the voice's first sample out of its chain must be what the
		// FIR places at output sample m_now + HitDelay, read - FirDelay = 147 (position of that sample in the ring)
		const int chainDelay = m_stChain->delay(_track);
		const int64_t first = BlockSize * (m_stBlock + 1);
		const auto& r = m_stRead;
		const int64_t rest = r.down * (m_now + m_hitDelay) - r.delay - r.up * (first + chainDelay);
		t.stNext = first + rest / r.up;
		t.stNextGain = _gain;
		t.stNextVelocity = _velocity;
		t.stGateOff = false;
		t.nextPhase = static_cast<int>(rest % r.up);
		// The first output whose newest tap reaches that sample, whatever the phase before it: at 44.1 kHz the FIR's Latency
		// and chainDelay as it is in 48 kHz samples, which comes a sample early at a chain of 12, never late
		t.phaseAt = phaseFrom(r, m_now + m_hitDelay, chainDelay);
		t.stFade = phaseFade(t.phaseAt);
	}

	void Engine::cut(const int _track)
	{
		const int64_t at = m_now + m_hitDelay;
		auto& t = m_tracks[static_cast<size_t>(_track)];
		const uint32_t bit = 1u << _track;
		if(m_mdLive & bit)
		{
			// Where in the ring the ramp starts: what sounds at that host sample
			const int64_t fadeAt = m_mdRead.table ? soundsAt(m_mdRead, at, t.mdPhase) : at;
			auto& md = m_mdTracks[static_cast<size_t>(_track)];
			// A Hit that waits for its block starts now; the voice's tail goes ahead of its blocks, so that the voice is free
			// at once for the Track's next Hit
			if(t.machine.family == Family::Machinedrum && md.triggered())
			{
				md.takeLocks();
				md.update(*m_library.os, m_tempo, m_voices[static_cast<size_t>(_track)]);
			}
			t.mdRing.fade(fadeAt);
			t.mdFadeAt = fadeAt;
			int blocks = 0;
			for(int64_t p = t.md.at; p < fadeAt + CutSamples && blocks < TailBlocks; p += BlockSize)
				renderVoice(_track, t.mdTail[blocks++]);
			t.mdTailBlocks = std::max(t.mdTailBlocks, blocks);
			// The rest of an OPEN DRUM block lies past the ramp: the voice's next Hit starts a block of its own
			t.odPending = false;
			// The VINTAGE grain's voice ends with its ramp, which its tail holds: nothing of it reads its sample after this
			t.vt.stop();
			if(t.mdTailBlocks)
			{
				m_mdTails |= bit;
			}
			else
			{
				m_mdChain->clear(_track);
				t.mdFadeAt = Never;
				if(m_mdRead.table)
					t.mdRing.close();	// a FIR reads past the end
			}
			m_mdLive &= ~bit;
		}
		if(t.lane >= 0)
		{
			auto& lane = sourceOf(t).lanes[static_cast<size_t>(t.lane)];
			if(lane.heir == _track)
			{
				// Its Hit had not begun: the lane goes on as the cut voice it was
				lane.heir = -1;
				t.stNext = -1;
			}
			else
			{
				lane.fadeAt = soundsAt(m_stRead, at, t.phase);
				t.stRing.fade(lane.fadeAt);
				t.stFadeAt = lane.fadeAt;
			}
			t.lane = -1;
		}
	}

	void Engine::playOpenDrum(const int _track, const int _index)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		const uint16_t parameter = openDrum_machineParameter(t.machine.id, _index);
		if(parameter == OpenDrumNone)
			return;
		const int offset = t.taken[static_cast<size_t>(machineTarget(_index))];
		const auto value = openDrum_machineValue(t.machine.id, _index,
			static_cast<uint8_t>(wordOf(playedValue(t, _index), offset) >> 7));
		if(parameter == OpenDrumOpen)
			t.odOpen = value != 0;
		else
			openDrum_setParameter(t.od, parameter, value);
	}

	void Engine::renderOpenDrum(Track& _track, float* _x)
	{
		if(_track.odTrig)
		{
			// The firmware's voice at full velocity, the Track's coming after
			if(_track.odSettle)
				openDrum_settle(_track.od);
			openDrum_trigger(_track.od, 127, 60, _track.odOpen);
			_track.odTrig = false;
			_track.odPending = false;
		}
		int16_t block[OpenDrumBlock];
		const int16_t* from = _track.odHalf;
		if(_track.odPending)
		{
			_track.odPending = false;
		}
		else
		{
			openDrum_render(_track.od, block);
			std::copy_n(block + BlockSize, BlockSize, _track.odHalf);
			_track.odPending = true;
			from = block;
		}
		for(int n = 0; n < BlockSize; ++n)
			_x[n] += static_cast<float>(from[n]) * (1.0f / 32768.0f);
	}

	void Engine::playSophie(const int _track, const int _index)
	{
		if(_index >= sophie::MachineSlots)
			return;
		auto& t = m_tracks[static_cast<size_t>(_track)];
		const int offset = t.taken[static_cast<size_t>(machineTarget(_index))];
		sophie::setMachineValue(m_sophie->v[static_cast<size_t>(_track)], _index,
			static_cast<uint8_t>(wordOf(playedValue(t, _index), offset) >> 7));
	}

	void Engine::renderSophie(const int _track, float* _x)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		auto& s = m_sophie->v[static_cast<size_t>(_track)];
		if(t.spTrig)
		{
			// At full velocity, the Track's coming after
			sophie_trigger(&s, 127);
			t.spTrig = false;
		}
		float block[BlockSize];
		sophie_render(&s, block, BlockSize);
		for(int n = 0; n < BlockSize; ++n)
			_x[n] += block[n] * SophieGain;
	}

	void Engine::playAnalog(const int _track, const int _index)
	{
		if(_index >= 8)
			return;
		auto& t = m_tracks[static_cast<size_t>(_track)];
		const int offset = t.taken[static_cast<size_t>(machineTarget(_index))];
		m_analog->v[static_cast<size_t>(_track)].setControl(_index,
			static_cast<uint8_t>(wordOf(playedValue(t, _index), offset) >> 7));
	}

	void Engine::renderAnalog(const int _track, float* _x)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		auto& v = m_analog->v[static_cast<size_t>(_track)];
		if(t.anTrig)
		{
			// the voice at full velocity, the Track's coming after; the Machinedrum's blocks the clock of what runs free
			v.setNote(t.note);
			v.trig(m_mdBlock * BlockSize);
			t.anTrig = false;
		}
		v.render(_x, BlockSize, SampleRate);
	}

	bool Engine::vintageGrain(const Track& _track)
	{
		return _track.machine == Sampler && vintage::isVintage(_track.params[vintage::GrainSlot]);
	}

	void Engine::renderVintage(const int _track, float* _x)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		const auto i = static_cast<size_t>(_track);
		// PTCH, DEC, HOLD, STRT and END as the ROM player reads them, smoothed and locked, the LFOs' moves added and the sum
		// held inside a Kit value's range first (the moves reach $3FFF: END past 127 would read past the sample). The
		// smoothing's floor stops up to 3 of 1/128 under its target, which the +2 and the rounding take back
		const auto& words = m_mdTracks[i].values();
		const auto word = [&](const int _p)
		{
			return std::clamp(static_cast<int>(words[static_cast<size_t>(_p)])
				+ t.taken[static_cast<size_t>(LfoTarget::Machine1) + static_cast<size_t>(_p)], 0, 127 << 7);
		};
		const auto smooth = [&](const int _p) { return std::min(127.0, (word(_p) + 2) / 128.0); };
		// The grain's Machine 10-12 with the LFOs' moves
		const auto grain = [&](const int _p)
		{
			return wordOf(t.params[static_cast<size_t>(_p)], t.taken[static_cast<size_t>(machineTarget(_p))]) >> 7;
		};
		const auto value = [&](const int _p) { return static_cast<int>(std::lround(word(_p) / 128.0)); };
		if(t.vtTrig)
		{
			t.vtTrig = false;
			const uint32_t length = m_vtLength[i];
			const auto begin = static_cast<uint32_t>(static_cast<uint64_t>(length) * static_cast<uint32_t>(value(4)) / 128);
			const auto end = static_cast<uint32_t>(static_cast<uint64_t>(length) * static_cast<uint32_t>(value(5)) / 127);
			t.vt.setDac(vintage::bitsOf(grain(vintage::BitsSlot)), vintage::companding(grain(vintage::CompandingSlot)));
			t.vt.trig(m_vtSource[i], begin, end, vintage::holdSeconds(value(2)) * SampleRate);
		}
		t.vt.setStep(m_vtRate[i] / SampleRate * std::exp2(vintage::semitones(smooth(0)) / 12));
		t.vt.setDecay(vintage::decaySeconds(smooth(1)), SampleRate);
		t.vt.setDac(vintage::bitsOf(grain(vintage::BitsSlot)), vintage::companding(grain(vintage::CompandingSlot)));
		t.vt.setFilter(vintage::filterHz(grain(vintage::FilterSlot)), SampleRate);
		t.vt.render(_x, BlockSize);
	}

	void Engine::renderVoice(const int _track, float* _x)
	{
		if(vintageGrain(m_tracks[static_cast<size_t>(_track)]))
		{
			renderVintage(_track, _x);
			return;
		}
		if(m_tracks[static_cast<size_t>(_track)].machine.family == Family::OpenDrum)
		{
			renderOpenDrum(m_tracks[static_cast<size_t>(_track)], _x);
			return;
		}
		if(m_tracks[static_cast<size_t>(_track)].machine.family == Family::Sophie)
		{
			renderSophie(_track, _x);
			return;
		}
		if(m_tracks[static_cast<size_t>(_track)].machine.family == Family::SyntaktAnalog)
		{
			renderAnalog(_track, _x);
			return;
		}
		md::voice::Voice::Block voice;
		m_voices[static_cast<size_t>(_track)].render(voice);
		for(int n = 0; n < BlockSize; ++n)
			_x[n] += static_cast<float>(voice[static_cast<size_t>(n)]) * (1.0f / 8388608.0f);
	}

	void Engine::renderMachinedrum()
	{
		modulate(~m_stMask, BlockSize / SampleRate);
		md::voice::prepareBlock(m_mdTracks, m_voices, *m_library.os, m_tempo, m_mdBlock, m_mdMask, m_mdLive);
		const uint32_t live = m_mdLive | m_mdTails;
		auto& in = *m_mdIn;
		for(uint32_t bits = live, track = 0; bits; bits >>= 1, ++track)
		{
			if(!(bits & 1))
				continue;
			auto& t = m_tracks[track];
			float x[BlockSize]{};
			if(m_mdLive >> track & 1)
				renderVoice(static_cast<int>(track), x);
			if(t.mdTailBlocks)
			{
				for(int n = 0; n < BlockSize; ++n)
					x[n] += t.mdTail[0][n];
				--t.mdTailBlocks;
				std::memmove(t.mdTail[0], t.mdTail[1], sizeof(t.mdTail) - sizeof(t.mdTail[0]));
				std::fill_n(t.mdTail[TailBlocks - 1], BlockSize, 0.0f);
			}
			for(int n = 0; n < BlockSize; ++n)
				in.s[n][track] = x[n];
			if(t.md.quiet)
				for(const float v : x)
					t.md.voiced = t.md.voiced || std::abs(v) >= Silence;
		}
		if(live)
			m_mdChain->process(in, static_cast<uint16_t>(live));
		for(uint32_t bits = live, track = 0; bits; bits >>= 1, ++track)
		{
			if(!(bits & 1))
				continue;
			auto& t = m_tracks[track];
			float x[BlockSize];
			for(int n = 0; n < BlockSize; ++n)
				x[n] = in.s[n][track];
			// The ring already holds the last voice's samples there, at its own place
			std::fill_n(x, std::exchange(t.mdHitSkip, 0), 0.0f);
			t.md.write(t.mdRing, x, t.level, t.mdFadeAt);
			const uint32_t bit = 1u << track;
			// An OPEN DRUM voice rounds a quiet tail to silence for longer than that: it is over once its envelope is too
			// and a SOPHIE voice once it has stopped
			if(t.machine.family == Family::OpenDrum ? t.md.quiet >= OpenDrumQuietBlocks && !openDrum_sounding(t.od)
				: t.machine.family == Family::Sophie ? t.md.quiet >= OpenDrumQuietBlocks && !t.spTrig
					&& !sophie_active(&m_sophie->v[track])
				: t.machine.family == Family::SyntaktAnalog ? t.md.quiet >= OpenDrumQuietBlocks && !t.anTrig
					&& !m_analog->v[track].active()
				: vintageGrain(t) ? t.md.quiet >= OpenDrumQuietBlocks && !t.vtTrig && !t.vt.playing()
				: t.md.quiet >= MachinedrumQuietBlocks)
				m_mdLive &= ~bit;
			if(!t.mdTailBlocks)
				m_mdTails &= ~bit;
			if(!((m_mdLive | m_mdTails) & bit))
			{
				m_mdChain->clear(static_cast<int>(track));
				t.mdFadeAt = Never;
				if(m_mdRead.table)
					t.mdRing.close();	// a FIR reads past the end
			}
		}
		// An OPEN DRUM or SOPHIE lock without a Hit in its block goes
		for(uint32_t bits = m_odMask | m_spMask | m_anMask, track = 0; bits; bits >>= 1, ++track)
			if(bits & 1)
				m_tracks[track].pendingLocks = 0;
		++m_mdBlock;
		if(m_mdRead.table)
			m_mdNext = (m_mdRead.up * m_mdBlock * BlockSize + m_mdRead.down - 1) / m_mdRead.down;
	}

	void Engine::renderSyntakt()
	{
		modulate(m_stMask, BlockSize / syntakt::fast::MachineRate);
		auto& in = *m_stIn;
		uint32_t live = 0;
		const auto compute = [&](const size_t _track)
		{
			if(live >> _track & 1)
				return;
			live |= 1u << _track;
			for(auto& sample : in.s)
				sample[_track] = 0;
		};
		for(auto& s : m_sources)
		{
			auto& source = *s;
			if(!source.used)
				continue;
			const Block& block = source.renderBlock();
			for(int l = 0; l < source.width; ++l)
			{
				auto& lane = source.lanes[static_cast<size_t>(l)];
				if(lane.track < 0)
					continue;
				const auto track = static_cast<size_t>(lane.track);
				auto& t = m_tracks[track];
				compute(track);
				if(lane.skip)
				{
					lane.skip = false;
				}
				else if(lane.fadeAt != Never && t.stFadeAt == Never)
				{
					// A Hit of the Track sounds: the cut voice ramps under it
					for(int n = 0; n < BlockSize; ++n)
						in.s[n][track] += block.s[n][l] * t.stTailGain * cutGain(t.st.at + n - lane.fadeAt);
				}
				else
				{
					for(int n = 0; n < BlockSize; ++n)
						in.s[n][track] += block.s[n][l];
				}
				if(lane.heir >= 0)
				{
					if(lane.heir != lane.track)
					{
						releaseVoice(lane.track);
						++m_tracks[static_cast<size_t>(lane.heir)].stVoices;
					}
					lane.track = lane.heir;
					lane.heir = -1;
					lane.fadeAt = Never;
				}
			}
		}
		for(size_t track = 0; track < Tracks; ++track)
			if(m_tracks[track].stNext >= 0)
			{
				compute(track);
				m_stChain->hit(static_cast<int>(track), m_tracks[track].stNextVelocity);
			}
		// What an amp has closed is over, whatever goes on under it
		for(uint32_t bits = live, track = 0; bits; bits >>= 1, ++track)
			if(bits & 1 && m_tracks[track].st.quiet && m_stChain->ampLevel(static_cast<int>(track)) >= Silence)
				for(auto& sample : in.s)
					m_tracks[track].st.voiced = m_tracks[track].st.voiced || std::abs(sample[track]) >= Silence;
		// Nothing at all while no Track's OVERDRIVE is on or ramps
		if(m_overRun)
			overdrive(in, live);
		if(live)
			m_stChain->process(in, static_cast<uint16_t>(live));
		for(size_t track = 0; track < Tracks; ++track)
		{
			auto& t = m_tracks[track];
			if(live >> track & 1 && t.stSounding)
			{
				float x[BlockSize];
				for(int n = 0; n < BlockSize; ++n)
					x[n] = in.s[n][track];
				t.st.write(t.stRing, x, t.level, t.stFadeAt);
			}
			if(t.stNext >= 0)
			{
				t.stTailGain = tailGain(t.st.velocity, t.stNextGain);
				t.st.start(t.stNext, t.stNextGain, t.level);
				t.stNext = -1;
				t.stSounding = true;
				t.stFadeAt = Never;
			}
			if(std::exchange(t.stGateOff, false))
				noteOff(static_cast<int>(track));
		}
		for(size_t s = 0; s < m_sources.size(); ++s)
		{
			auto& source = *m_sources[s];
			for(int l = 0; l < source.width; ++l)
			{
				const auto& lane = source.lanes[static_cast<size_t>(l)];
				if(lane.track < 0)
					continue;
				const auto& t = m_tracks[static_cast<size_t>(lane.track)];
				if(lane.fadeAt != Never ? t.st.at >= lane.fadeAt + CutSamples : t.st.quiet >= SyntaktQuietBlocks)
					freeLane(static_cast<int>(s), l);
			}
		}
		for(auto& t : m_tracks)
			t.pendingLocks = 0;
		++m_stBlock;
		m_stNext = m_bridge ? (m_stRead.up * m_stBlock * BlockSize + m_stRead.down - 1) / m_stRead.down
			: (Up * m_stBlock * BlockSize + Down - 1) / Down;	// 44.1 kHz's by its constants
	}

	// OVERDRIVE, the stage the Syntakt has after the voice, on the _live Tracks where it is on or ramps: it gives its input
	// at half (stFastTrack.h), which its groups take back to 1, at 0 exactly
	void Engine::overdrive(Block& _in, const uint32_t _live)
	{
		if(const uint32_t over = _live & m_overRun)
		{
			m_stOver->process(_in, static_cast<uint16_t>(over));
			for(int l = 0; l < Lanes; ++l)
				if(over >> (l & ~3) & 15)
					for(auto& sample : _in.s)
						sample[l] *= 2;
		}
		m_overRun = (m_overRun & ~_live) | m_overOn;
	}

	bool Engine::readOver(const Ring& _ring, const Reading& _r, const int _phase, const int64_t _phaseAt, const int _fade,
		const int _count) const
	{
		if(!_r.table)
			return m_now >= _ring.end();
		// (down m - phase) / up - FirTaps >= end, without a division: it runs for every Track at every run
		return _r.down * m_now - _phase >= _r.up * (_ring.end() + FirTaps) && _phaseAt - _fade > m_now + _count;
	}

	void Engine::readThrough(const Ring& _ring, const Reading& _r, int& _phase, const int _nextPhase, int64_t& _phaseAt,
		const int _fade, float* _out, const int _count) const
	{
		if(!_r.table)
		{
			// The ring at the host's rate: as it is
			if(m_now < _ring.end())
			{
				const int n = static_cast<int>(std::min<int64_t>(_count, _ring.end() - m_now));
				const float* x = _ring.window(m_now);
				for(int i = 0; i < n; ++i)
					_out[i] += x[i];
			}
			return;
		}
		for(int done = 0; done < _count;)
		{
			const int64_t m = m_now + done;
			if(m >= _phaseAt)
			{
				_phase = _nextPhase;
				_phaseAt = Never;
			}
			const int64_t fadeFrom = _phaseAt == Never ? Never : _phaseAt - _fade;
			if(m >= fadeFrom)
			{
				// The old phase's reading into the new one's, sample by sample, ending where the new can reach the Hit's
				// first block: what the ring holds from before slides by under a sample, without a step
				const auto at = [&](const int _ph)
				{
					const int64_t q = _r.down * m - _ph;
					const int64_t i = q >= 0 ? q / _r.up : -1;
					const auto p = static_cast<int>(q - i * _r.up);
					return i - FirTaps >= _ring.end() ? 0.0f : dot(_ring.window(i - (FirTaps - 1)), _r.table + p * FirTaps);
				};
				const float w = static_cast<float>(m - fadeFrom + 1) / static_cast<float>(_fade + 1);
				_out[done] += (1 - w) * at(_phase) + w * at(_nextPhase);
				++done;
				continue;
			}
			const int64_t q = _r.down * m - _phase;
			int64_t i = q >= 0 ? _r.below(q) : -1;
			int p = static_cast<int>(q - i * _r.up);
			// Past what the ring holds: nothing to read, at either phase, until a block is written, which no run sees
			if(i - FirTaps >= _ring.end())
				return;
			const int n = static_cast<int>(std::min<int64_t>(_count - done, fadeFrom - m));
			if(_r.fast)
			{
				rate::readFir(_ring.data(), Ring::Size - 1, i, p, _r.up, _r.down, _r.table, _r.unit, _out + done, n);
				done += n;
				continue;
			}
			for(int k = 0; k < n; ++k)
			{
				const float* window = _ring.window(i - (FirTaps - 1));
				if(_r.unit && _r.unit[p] >= 0)
					_out[done + k] += window[_r.unit[p]];
				else
					_out[done + k] += dot(window, _r.table + p * FirTaps);
				// A host sample on: down / up of the ring's
				p += _r.down;
				while(p >= _r.up)
				{
					p -= _r.up;
					++i;
				}
			}
			done += n;
		}
	}

	void Engine::read(const int _track, float* _out, const int _count)
	{
		auto& t = m_tracks[static_cast<size_t>(_track)];
		readThrough(t.mdRing, m_mdRead, t.mdPhase, t.mdNextPhase, t.mdPhaseAt, t.mdFade, _out, _count);
		readThrough(t.stRing, m_stRead, t.phase, t.nextPhase, t.phaseAt, t.stFade, _out, _count);
	}

	void Engine::mix(float* const* _outputs, const int _offset, const int _count)
	{
		alignas(16) float sum[BlockSize];
		const int at = static_cast<int>(m_now & (BlockSize - 1));
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
			// Each send moves a full swing in CutSamples too
			for(int s = 0; s < SendCount; ++s)
			{
				const auto index = static_cast<size_t>(s);
				float g = t.sendNow[index];
				const float to = t.send[index];
				if(g == 0 && to == 0)
					continue;
				float* const left = m_sends->input(static_cast<Send>(s), 0, at);
				float* const right = m_sends->input(static_cast<Send>(s), 1, at);
				for(int i = 0; i < _count; ++i)
				{
					left[i] += sum[i] * g * t.panNow[0];
					right[i] += sum[i] * g * t.panNow[1];
					const float move = to - g;
					g = std::abs(move) <= 1.0f / CutSamples ? to : g + (move > 0 ? 1.0f : -1.0f) / CutSamples;
				}
				t.sendNow[index] = g;
			}
		}
		if(!m_sends->sounding())
			return;
		for(int s = 0; s < SendCount; ++s)
			for(int c = 0; c < 2; ++c)
				if(float* const out = _outputs[2 * m_sends->output(static_cast<Send>(s)) + c])
				{
					const float* const back = m_sends->output(static_cast<Send>(s), c, at);
					for(int i = 0; i < _count; ++i)
						out[_offset + i] += back[i];
				}
	}

	// mix away from 44.1 kHz: the sends through SendsAtRate in host blocks, the ramps at the host's rate, the steady gains
	// four at a time. 44.1 kHz keeps mix as it was, which runs for every Track at every run, off or not
	void Engine::mixAtRate(float* const* _outputs, const int _offset, const int _count)
	{
		alignas(16) float sum[SendsAtRate::MaxHost];		// a run: a host block at most
		const int at = static_cast<int>(m_now % m_hostBlock);	// in the sends' block
		for(int track = 0; track < Tracks; ++track)
		{
			auto& t = m_tracks[static_cast<size_t>(track)];
			if(readOver(t.mdRing, m_mdRead, t.mdPhase, t.mdPhaseAt, t.mdFade, _count)
				&& readOver(t.stRing, m_stRead, t.phase, t.phaseAt, t.stFade, _count))
			{
				continue;
			}
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
					g = std::abs(move) <= m_ramp ? to : g + (move > 0 ? m_ramp : -m_ramp);
				}
				t.panNow[c] = g;
				if(out)
					addScaled(out + i, sum + i, g, _count - i);
			}
			// Each send moves a full swing in CutSamples too
			for(int s = 0; s < SendCount; ++s)
			{
				const auto index = static_cast<size_t>(s);
				float g = t.sendNow[index];
				const float to = t.send[index];
				if(g == 0 && to == 0)
					continue;
				const auto send = static_cast<Send>(s);
				float* const left = m_bridge->input(send, 0, at);
				float* const right = m_bridge->input(send, 1, at);
				int i = 0;
				for(; i < _count && g != to; ++i)
				{
					left[i] += sum[i] * g * t.panNow[0];
					right[i] += sum[i] * g * t.panNow[1];
					const float move = to - g;
					g = std::abs(move) <= m_ramp ? to : g + (move > 0 ? m_ramp : -m_ramp);
				}
				// The rest at its level, as the pan's: twice the samples a run at 88.2 and 96 kHz
				addScaled(left + i, sum + i, g, t.panNow[0], _count - i);
				addScaled(right + i, sum + i, g, t.panNow[1], _count - i);
				t.sendNow[index] = g;
			}
		}
		if(!m_bridge->sounding())
			return;
		for(int s = 0; s < SendCount; ++s)
			for(int c = 0; c < 2; ++c)
				if(float* const out = _outputs[2 * m_sends->output(static_cast<Send>(s)) + c])
				{
					const auto send = static_cast<Send>(s);
					const float* const back = m_bridge->output(send, c, at);
					for(int i = 0; i < _count; ++i)
						out[_offset + i] += back[i];
				}
	}

	void Engine::render(float* const* _outputs, const int _count)
	{
		const syntakt::fast::FlushDenormals ftz;
		for(int b = 0; b < 2 * Outputs; ++b)
			if(_outputs[b])
				std::fill_n(_outputs[b], _count, 0.0f);
		if(!m_bridge)
		{
			// 44.1 kHz as before: the Machinedrum's blocks are the host's, the sends' and the master's
			for(int done = 0; done < _count;)
			{
				if(m_now == m_mdBlock * BlockSize)
				{
					if(m_now)
						m_sends->block();
					m_master->block();
					renderMachinedrum();
				}
				if(m_now == m_stNext)
					renderSyntakt();
				const auto run = static_cast<int>(std::min({static_cast<int64_t>(_count - done),
					m_mdBlock * BlockSize - m_now, m_stNext - m_now}));
				mix(_outputs, done, run);
				if(m_masterOn && _outputs[0] && _outputs[1])
					m_master->process(_outputs[0] + done, _outputs[1] + done, run);
				m_now += run;
				done += run;
			}
			return;
		}
		for(int done = 0; done < _count;)
		{
			// The host's blocks, the sends' and the master's: about 0.7 ms, a run each but where a family's block falls
			if(m_now % m_hostBlock == 0)
			{
				if(m_now)
					m_bridge->block();
				m_master->block();
			}
			if(m_now == m_mdNext)
				renderMachinedrum();
			if(m_now == m_stNext)
				renderSyntakt();
			const auto run = static_cast<int>(std::min({static_cast<int64_t>(_count - done),
				(m_now / m_hostBlock + 1) * m_hostBlock - m_now, m_mdNext - m_now, m_stNext - m_now}));
			mixAtRate(_outputs, done, run);
			if(m_masterOn && _outputs[0] && _outputs[1])
				m_master->process(_outputs[0] + done, _outputs[1] + done, run);
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
