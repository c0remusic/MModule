// The Syntakt's track stages rewritten (stFastTrack) against the exact port's (syntakt::track), stage by stage, on
// signals made here, at 48 kHz: Q3 as ticket 05 settled it where the stage bends the signal, SNR 70 dB overall and 60 dB
// in its worst 10 ms window. The filter envelope against section 3's tables, stepped as CPU #1 steps them. Where a law
// was fitted the error is printed, and a bound 1 dB or 10 % over it keeps it there.
//
// SYNTAKT_SECTION7 and SYNTAKT_SECTION3 name the user's firmware; exit 77 without them.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "stBoot.h"
#include "stFastMetrics.h"
#include "stFastTrack.h"
#include "stMainFilter.h"
#include "stMainMemory.h"
#include "stMemoryMap.h"
#include "stSection7.h"
#include "stTrack.h"

namespace
{
	namespace bench = syntakt::fast::bench;
	namespace track = syntakt::track;
	using syntakt::Globals;
	using syntakt::Rom;
	using syntakt::Sram;
	using syntakt::TrackParams;
	using syntakt::VoiceOut;
	using syntakt::fast::BaseWidth;
	using syntakt::fast::Block;
	using syntakt::fast::BlockSize;
	using syntakt::fast::FilterEnvelope;
	using syntakt::fast::Multimode;
	using syntakt::fast::TrackAmp;
	using syntakt::fast::TrackDrive;
	using bench::Pi;

	constexpr int Blocks = 8192;		// 2.7 s

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// A sum of what the stages meet: Hits of noise and of a pitched decay, a low tone under them
	std::vector<double> signal(const uint32_t _seed, const double _level)
	{
		std::mt19937 random(_seed);
		std::uniform_real_distribution<double> uniform(-1, 1);
		std::vector<double> x(Blocks * BlockSize);
		double envelope = 0, phase = 0, hz = 100;
		for(size_t n = 0; n < x.size(); ++n)
		{
			if(n % 9001 == 0)
			{
				envelope = 1;
				hz = 60 + 3000 * (uniform(random) + 1) / 2;
			}
			envelope *= 0.9995;
			phase += 2 * Pi * hz / 48000;
			x[n] = _level * (0.4 * envelope * uniform(random) + 0.4 * envelope * std::sin(phase)
				+ 0.15 * std::sin(2 * Pi * 80.0 * static_cast<double>(n) / 48000));
		}
		return x;
	}

	struct Score
	{
		double overall = 1e9, worst = 1e9;

		void add(const std::vector<double>& _reference, const std::vector<double>& _y)
		{
			double signalPower = 0, noisePower = 0;
			for(size_t n = 0; n < _y.size(); ++n)
			{
				signalPower += _reference[n] * _reference[n];
				noisePower += (_y[n] - _reference[n]) * (_y[n] - _reference[n]);
			}
			overall = std::min(overall, bench::db(signalPower / std::max(noisePower, 1e-30)));
			worst = std::min(worst, bench::localSnr(_reference, _y, 48000));
		}
	};

	// The exact port's stages on track 0, its input and its parameters this test's
	class Exact
	{
	public:
		explicit Exact(const std::vector<int32_t>& _section7) : m_sram(std::make_unique<Sram>()), m_rom{_section7.data()}
		{
			boot(*m_sram, m_globals, m_rom);
		}

		void half(const uint32_t _offset, const int _word)
		{
			m_sram->setByte(TrackParams + _offset, static_cast<uint8_t>(_word >> 8));
			m_sram->setByte(TrackParams + _offset + 1, static_cast<uint8_t>(_word));
		}

		int32_t* out() { return m_sram->ptr(VoiceOut); }
		Sram& sram() { return *m_sram; }
		const Rom& rom() const { return m_rom; }

	private:
		std::unique_ptr<Sram> m_sram;
		Globals m_globals;
		Rom m_rom;
	};

	// A block of each, lane 0
	void capture(const int32_t* _exact, const Block& _fast, std::vector<double>& _reference, std::vector<double>& _y)
	{
		for(int n = 0; n < BlockSize; ++n)
		{
			_reference.push_back(bench::q31(_exact[n]));
			_y.push_back(_fast.s[n][0]);
		}
	}

	void input(const std::vector<double>& _x, const int _block, int32_t* _exact, Block& _fast, const int _lanes)
	{
		for(int n = 0; n < BlockSize; ++n)
		{
			const double x = _x[static_cast<size_t>(_block * BlockSize + n)];
			_exact[n] = bench::toQ31(x);
			for(int l = 0; l < _lanes; ++l)
				_fast.s[n][l] = static_cast<float>(x);
		}
	}

	void drive(const std::vector<int32_t>& _section7)
	{
		Score score;
		const auto x = signal(1, 1.0);
		for(const int word : {0, 0x0400, 0x0800, 0x2000, 0x4000, 0x6000, 0x7f00})
		{
			Exact exact(_section7);
			auto fast = std::make_unique<TrackDrive>();
			auto block = std::make_unique<Block>();
			std::vector<double> reference, y;
			exact.half(0x50, word);
			fast->set(0, word);
			for(int b = 0; b < Blocks; ++b)
			{
				input(x, b, exact.out(), *block, 4);
				track::drive(exact.sram(), VoiceOut, TrackParams);
				fast->process(*block);
				capture(exact.out(), *block, reference, y);
			}
			reference.erase(reference.begin(), reference.begin() + 2 * BlockSize);
			y.erase(y.begin(), y.begin() + 2 * BlockSize);
			score.add(reference, y);
		}
		std::printf("  drive, 7 words: SNR %.1f dB, worst window %.1f dB\n", score.overall, score.worst);
		require(score.overall >= 70 && score.worst >= 60, "Q3 of the drive");
	}

	void baseWidth(const std::vector<int32_t>& _section7)
	{
		Score score, wideOpen;
		const auto x = signal(2, 0.8);
		for(const auto& [base, width] : {std::pair{0, 0x7f00}, {0x2000, 0x3000}, {0x4000, 0x1000}, {0x1000, 0x6000}, {0x6000, 0x1f00}})
		{
			Exact exact(_section7);
			auto fast = std::make_unique<BaseWidth>();
			auto block = std::make_unique<Block>();
			std::vector<double> reference, y;
			exact.half(0x66, base);
			exact.half(0x68, width);
			fast->set(0, base, width);
			for(int b = 0; b < Blocks; ++b)
			{
				input(x, b, exact.out(), *block, 2);
				track::baseWidth(exact.sram(), exact.rom(), VoiceOut, TrackParams);
				fast->process(*block);
				capture(exact.out(), *block, reference, y);
			}
			score.add(reference, y);
			if(width == 0x7f00)
				wideOpen.add(reference, y);
		}
		// The poles' law bends near the top of the scale, which is a fit: the family's chain is the first setting
		std::printf("  BASE/WIDTH, 5 settings: SNR %.1f dB, worst window %.1f dB; wide open, %.1f and %.1f\n", score.overall,
			score.worst, wideOpen.overall, wideOpen.worst);
		require(score.overall >= 56 && score.worst >= 53, "BASE/WIDTH against the exact port");
		require(wideOpen.overall >= 70 && wideOpen.worst >= 60, "Q3 of BASE/WIDTH wide open");
	}

	void multimode(const std::vector<int32_t>& _section7)
	{
		double worstOverall = 1e9, worstWindow = 1e9;
		for(int type = 0; type < 8; ++type)
		{
			Score score;
			const auto x = signal(3 + static_cast<uint32_t>(type), 0.5);
			for(const int reso : {0, 0x2000, 0x4000, 0x6000, 0x7f00})
				for(const int32_t cutoff : {0x20000000, 0x40000000, 0x58000000, 0x70000000})
				{
					Exact exact(_section7);
					auto fast = std::make_unique<Multimode>();
					auto block = std::make_unique<Block>();
					std::vector<double> reference, y;
					exact.half(0x52, type << 8);
					exact.half(0x56, reso);
					exact.half(0x82, 1);
					fast->set(0, type, cutoff, reso << 16, true);
					for(int b = 0; b < Blocks / 4; ++b)
					{
						// A trig now and then, a cutoff that moves between
						const int trig = b % 1000 == 3;
						const int32_t moved = cutoff - (b % 1000 < 200 ? (200 - b % 1000) << 18 : 0);
						input(x, b, exact.out(), *block, 2);
						track::filter(exact.sram(), exact.rom(), TrackParams + 0x52, VoiceOut, moved, 0, trig, 1);
						if(trig)
							fast->trig(0);
						fast->set(0, type, moved, reso << 16, true);
						fast->process(*block);
						capture(exact.out(), *block, reference, y);
					}
					reference.erase(reference.begin(), reference.begin() + 8 * BlockSize);
					y.erase(y.begin(), y.begin() + 8 * BlockSize);
					score.add(reference, y);
				}
			std::printf("  filter type %d, 20 settings: SNR %.1f dB, worst window %.1f dB\n", type, score.overall, score.worst);
			worstOverall = std::min(worstOverall, score.overall);
			worstWindow = std::min(worstWindow, score.worst);
		}

		// Below Q3 only at low cutoffs, the DSP's 32-bit words there being what the doubles do not follow: bounds 1 dB under
		require(worstOverall >= 59 && worstWindow >= 53, "the filter's types");

		// FREQ 127, where a new Sound's filter stands and the cutoff word ends
		for(const int type : {0, 1})
		{
			Score score;
			const auto x = signal(13 + static_cast<uint32_t>(type), 0.5);
			for(const int reso : {0, 0x2000, 0x4000, 0x6000, 0x7f00})
			{
				Exact exact(_section7);
				auto fast = std::make_unique<Multimode>();
				auto block = std::make_unique<Block>();
				std::vector<double> reference, y;
				exact.half(0x52, type << 8);
				exact.half(0x56, reso);
				exact.half(0x82, 1);
				fast->set(0, type, 0x7f000000, reso << 16, true);
				for(int b = 0; b < Blocks / 4; ++b)
				{
					const int trig = b % 1000 == 3;
					input(x, b, exact.out(), *block, 2);
					track::filter(exact.sram(), exact.rom(), TrackParams + 0x52, VoiceOut, 0x7f000000, 0, trig, 1);
					if(trig)
						fast->trig(0);
					fast->process(*block);
					capture(exact.out(), *block, reference, y);
				}
				reference.erase(reference.begin(), reference.begin() + 8 * BlockSize);
				y.erase(y.begin(), y.begin() + 8 * BlockSize);
				score.add(reference, y);
			}
			std::printf("  filter type %d at FREQ 127, 5 resonances: SNR %.1f dB, worst window %.1f dB\n", type, score.overall, score.worst);
			require(score.overall >= 70 && score.worst >= 60, "Q3 of the filter at FREQ 127");
		}

		// A type changed at a block: the machine's fade and snap, sample for sample
		Exact exact(_section7);
		auto fast = std::make_unique<Multimode>();
		auto block = std::make_unique<Block>();
		const auto x = signal(11, 0.5);
		Score score;
		std::vector<double> reference, y;
		exact.half(0x56, 0x2000);
		exact.half(0x82, 0);
		for(int b = 0; b < Blocks / 2; ++b)
		{
			const int type = b / 300 % 8;
			exact.half(0x52, type << 8);
			fast->set(0, type, 0x50000000, 0x2000 << 16, false);
			input(x, b, exact.out(), *block, 2);
			track::filter(exact.sram(), exact.rom(), TrackParams + 0x52, VoiceOut, 0x50000000, 0, 0, 0);
			fast->process(*block);
			capture(exact.out(), *block, reference, y);
		}
		// From the block the two have both left the boot's type
		reference.erase(reference.begin(), reference.begin() + 8 * BlockSize);
		y.erase(y.begin(), y.begin() + 8 * BlockSize);
		score.add(reference, y);
		std::printf("  filter, the types in turn: SNR %.1f dB, worst window %.1f dB\n", score.overall, score.worst);
		require(score.overall >= 70 && score.worst >= 60, "a filter type change");
	}

	// The chain a Syntakt machine is first given, the three stages in turn, as ticket 12 runs them, at a new Sound's values:
	// OVER 0, BASE 0, WIDTH 127, TYPE 0, FREQ 127, RESO 0
	void familyChain(const std::vector<int32_t>& _section7)
	{
		Exact exact(_section7);
		auto drive = std::make_unique<TrackDrive>();
		auto band = std::make_unique<BaseWidth>();
		auto filter = std::make_unique<Multimode>();
		auto block = std::make_unique<Block>();
		const auto x = signal(21, 0.9);
		exact.half(0x52, 0);
		exact.half(0x56, 0);
		exact.half(0x66, 0);
		exact.half(0x68, 0x7f00);
		exact.half(0x82, 1);
		filter->set(0, 0, 0x7f000000, 0, true);
		filter->clear(0);
		std::vector<double> reference, y;
		for(int b = 0; b < Blocks; ++b)
		{
			const int trig = b % 2000 == 100;
			input(x, b, exact.out(), *block, 2);
			track::drive(exact.sram(), VoiceOut, TrackParams);
			track::baseWidth(exact.sram(), exact.rom(), VoiceOut, TrackParams);
			track::filter(exact.sram(), exact.rom(), TrackParams + 0x52, VoiceOut, 0x7f000000, 0, trig, 1);
			if(trig)
				filter->trig(0);
			if(b % 2000 == 101)
				band->clear(0);		// RST, from where the Hit sounds
			drive->process(*block);
			band->process(*block);
			filter->process(*block);
			capture(exact.out(), *block, reference, y);
		}
		// From the first Hit's reset: the drive's boot ramps from 0, and BASE/WIDTH's 5 Hz would keep it
		reference.erase(reference.begin(), reference.begin() + 102 * BlockSize);
		y.erase(y.begin(), y.begin() + 102 * BlockSize);
		Score score;
		score.add(reference, y);
		std::printf("  the family's chain, drive 0, BASE/WIDTH open, type 0 at FREQ 127: SNR %.1f dB, worst window %.1f dB\n", score.overall, score.worst);
		require(score.overall >= 70 && score.worst >= 60, "Q3 of the family's chain");
	}

	void amp(const std::vector<int32_t>& _section7)
	{
		struct Setting
		{
			int attack, hold, decay, sustain, release, mode;
		};
		double worstDb = 0;
		for(const Setting s : {Setting{0, 0, 40, 0, 0, 0}, {30, 10, 60, 0, 0, 0}, {60, 30, 80, 0, 0, 0}, {20, 0, 50, 64, 40, 1},
			{0, 127, 50, 0, 0, 0}, {115, 0, 40, 0, 0, 0}, {122, 0, 40, 0, 0, 0}, {0, 64, 127, 100, 32, 0}, {0, 64, 127, 100, 32, 1}})
		{
			Exact exact(_section7);
			auto fast = std::make_unique<TrackAmp>();
			auto block = std::make_unique<Block>();
			const uint32_t a = 0x6a;
			exact.half(a + 2, s.attack << 8);
			exact.half(a + 4, s.hold << 8);
			exact.half(a + 6, s.decay << 8);
			exact.half(a + 8, s.sustain << 8);
			exact.half(a + 10, s.release << 8);
			exact.half(a + 22, s.mode << 8);
			exact.half(a + 24, 1 << 8);
			fast->set(0, s.attack, s.hold, s.decay, s.sustain, s.release, s.mode == 1);
			double error = 0;
			for(int b = 0; b < Blocks; ++b)
			{
				const int trig = b % 4000 == 2, gateOff = b % 4000 == 1500;
				for(int n = 0; n < BlockSize; ++n)
				{
					exact.out()[n] = 0x40000000;
					block->s[n][0] = 0.5f;
				}
				track::ampEnvelope(exact.sram(), exact.rom(), VoiceOut, 0x800049e0, TrackParams + a, trig, gateOff, 0);
				if(trig)
					fast->trig(0);
				if(gateOff)
					fast->gateOff(0);
				fast->process(*block, 1);
				for(int n = 0; n < BlockSize; ++n)
				{
					const double reference = 2 * bench::q31(exact.out()[n]), y = 2.0 * block->s[n][0];
					if(reference > 0.01)
						error = std::max(error, std::abs(20 * std::log10(std::max(y, 1e-9) / reference)));
				}
			}
			std::printf("  amp attack %d hold %d decay %d sustain %d release %d mode %d: within %.2f dB down to -40 dB\n", s.attack,
				s.hold, s.decay, s.sustain, s.release, s.mode, error);
			worstDb = std::max(worstDb, error);
		}
		require(worstDb <= 1.5, "the amp's envelope");
	}

	// CPU #1's envelope on section 3's words, against FilterEnvelope: blocks to the top of the attack, and to half of
	// the decay
	void envelope(const syntakt::main::Section3& _section3)
	{
		const auto word = [&](const uint32_t _address)
		{
			const auto* p = _section3.bytes.data() + (_address - syntakt::main::Section3::Base);
			return static_cast<int32_t>(uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]);
		};
		auto fast = std::make_unique<FilterEnvelope>();
		double worstAttack = 0, worstDecay = 0, worstDelay = 0;
		for(int v = 2; v < 127; ++v)
		{
			const double attack = -2147483648.0 / word(0x401deec0 + 4 * static_cast<uint32_t>(v));
			const double c = -word(0x401decc0 + 4 * static_cast<uint32_t>(v)) / 2147483648.0;
			const double decay = std::log(0.5) / std::log(c);
			const double delay = word(0x401deac0 + 4 * static_cast<uint32_t>(v)) / 32.0;

			FilterEnvelope::Setting s;
			s.attack = v;
			s.decay = 127;
			fast->clear(0);
			fast->step(0, s, true, false);
			int blocks = 0;
			while(fast->step(0, s, false, false) < 1)
				++blocks;
			worstAttack = std::max(worstAttack, std::abs((blocks + 1) / attack - 1));

			s.attack = 0;
			s.decay = v;
			fast->clear(0);
			fast->step(0, s, true, false);
			fast->step(0, s, false, false);
			fast->step(0, s, false, false);
			blocks = 0;
			while(fast->step(0, s, false, false) > 0.5 && blocks < 1 << 20)
				++blocks;
			if(decay > 30)
				worstDecay = std::max(worstDecay, std::abs((blocks + 1) / decay - 1));

			s.attack = 127;
			s.decay = 0;
			s.delay = v;
			fast->clear(0);
			fast->step(0, s, true, false);
			blocks = 0;
			while(fast->step(0, s, false, false) == 0)
				++blocks;
			worstDelay = std::max(worstDelay, std::abs(blocks / delay - 1));
		}
		std::printf("  filter envelope against section 3's words: attack within %.2f %%, half of its decay %.2f %%, delay %.2f %%\n",
			100 * worstAttack, 100 * worstDecay, 100 * worstDelay);
		// The attack is short of a whole block at its first values
		require(worstAttack < 0.11 && worstDecay < 0.03 && worstDelay < 0.05, "the filter envelope's laws");
	}

	// FENR against CPU #1's envelope: Hits in the attack, the decay and the release, on and off
	void envelopeReset(const syntakt::main::Section3& _section3)
	{
		namespace m = syntakt::main;
		constexpr uint32_t P = m::Memory::SramBase;		// where this test keeps the page the routine reads
		for(const bool reset : {false, true})
		{
			auto mem = std::make_unique<m::Memory>(_section3);
			auto fast = std::make_unique<FilterEnvelope>();
			FilterEnvelope::Setting s;
			s.attack = 50;
			s.decay = 50;
			s.sustain = 40;
			s.release = 50;
			s.reset = reset;
			mem->setHalf(P + 0x5c, s.attack << 8);
			mem->setHalf(P + 0x5e, s.decay << 8);
			mem->setHalf(P + 0x60, s.sustain << 8);
			mem->setHalf(P + 0x62, s.release << 8);
			mem->setHalf(P + 0x64, reset ? 0x100 : 0);
			double worst = 0, atHit = 0;
			int sinceHit = 0;
			for(int b = 0; b < 3000; ++b)
			{
				const bool trig = b == 0 || b == 30 || b == 900 || b == 1800, gateOff = b == 1500;
				m::filterEnvelopes(*mem, P, trig, gateOff, 0);
				const double exact = -static_cast<double>(m::filterEnvelope(*mem, 0)) / 2147483648.0;
				const double y = fast->step(0, s, trig, gateOff);
				sinceHit = trig ? 0 : sinceHit + 1;
				// A Hit's own block and its first steps, before the laws' fit adds up
				if(sinceHit < 8)
					worst = std::max(worst, std::abs(y - exact));
				if(trig && b)
					atHit = std::max(atHit, exact);
			}
			std::printf("  FENR %d: a Hit on a moving envelope within %.4f of CPU #1's, which stands at %.3f at most\n", reset ? 1 : 0, worst, atHit);
			require(worst < 0.02, "FENR against CPU #1's envelope");
			require(reset ? atHit == 0 : atHit > 0.2, "a Hit's envelope");
		}
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const char* section7 = std::getenv("SYNTAKT_SECTION7");
	const char* section3 = std::getenv("SYNTAKT_SECTION3");
	if(!section7 || !*section7 || !section3 || !*section3)
	{
		std::printf("SYNTAKT_SECTION7 or SYNTAKT_SECTION3 not set: skipped\n");
		return 77;
	}
	const auto words = syntakt::loadSection7(section7);
	const auto os = syntakt::main::loadSection3(section3);
	if(words.empty() || os.bytes.empty())
	{
		std::printf("not OS 1.41's sections: skipped\n");
		return 77;
	}
	const syntakt::fast::FlushDenormals ftz;
	try
	{
		drive(words);
		baseWidth(words);
		multimode(words);
		familyChain(words);
		amp(words);
		envelope(os);
		envelopeReset(os);
	}
	catch(const std::exception& _e)
	{
		std::printf("FAIL: %s\n", _e.what());
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
