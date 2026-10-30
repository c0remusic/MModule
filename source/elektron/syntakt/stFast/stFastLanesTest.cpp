// setLanes on each of the nine machines: an instance rendering lanes 0 to n - 1 (n 4, 8 and 12) gives those lanes
// the samples of one rendering all 16, bit for bit, and silence on the others, from the block it narrows. Both play
// the same random trigs, gate-offs, notes and parameters on Tracks 0 to n - 1; the narrow one also plays Tracks n to
// 15 until it narrows, which must then fall silent at once. No firmware: SP TWINSHOT plays a set of noise made here.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <type_traits>

#include "stFastBdModern.h"
#include "stFastCpVintage.h"
#include "stFastCyAlloy.h"
#include "stFastPcCarbon.h"
#include "stFastSdBasic.h"
#include "stFastSdVintage.h"
#include "stFastSpTwinshot.h"
#include "stFastSyBits.h"
#include "stFastSyToy.h"

namespace
{
	using namespace syntakt::fast;

	constexpr int Blocks = 600;		// 0.4 s at 48 kHz
	constexpr int NarrowAt = 200;	// the block from which the narrow instance renders n lanes

	TwinshotSamples makeSamples()
	{
		constexpr uint32_t Length = 4000;
		TwinshotSamples s;
		s.base = 0x100000;
		s.data.resize(64 * Length);
		std::mt19937 rng(7);
		for(auto& x : s.data)
			x = static_cast<int16_t>(static_cast<int32_t>(rng() % 65536) - 32768);
		for(uint32_t i = 0; i < 64; ++i)
			s.slots[i] = {Length, s.base + 2 * i * Length};
		return s;
	}

	const TwinshotSamples g_samples = makeSamples();

	// One random event on Track _t of both machines (_b alone when _bOnly)
	template<typename M>
	void play(std::mt19937& _rng, M& _a, M& _b, const int _t, const bool _bOnly)
	{
		const auto r = _rng() % 100;
		const auto apply = [&](auto&& _f)
		{
			if(!_bOnly)
				_f(_a);
			_f(_b);
		};
		if(r < 4)
		{
			apply([&](M& _m) { _m.trig(_t); });
		}
		else if(r < 6)
		{
			apply([&](M& _m) { _m.gateOff(_t); });
		}
		else if(r < 7)
		{
			const int note = 24 + static_cast<int>(_rng() % 72);
			apply([&](M& _m) { _m.setNote(_t, note); });
		}
		else if(r < 15)
		{
			const int index = static_cast<int>(_rng() % M::Parameters);
			const auto value = static_cast<uint8_t>(_rng() % 128), fine = static_cast<uint8_t>(_rng() % 256);
			apply([&](M& _m) { _m.setParameter(_t, index, value, fine); });
		}
	}

	// The failures of machine M narrowed to _n lanes
	template<typename M>
	int check(const char* _name, const int _n)
	{
		auto a = std::make_unique<M>(), b = std::make_unique<M>();
		if constexpr(std::is_same_v<M, SpTwinshot>)
		{
			a->setSamples(&g_samples);
			b->setSamples(&g_samples);
		}

		std::mt19937 rng(static_cast<uint32_t>(_n));
		int64_t differing = 0, loud = 0;
		bool sounded[Lanes]{};	// the test's own check: each lane sounded where it is compared
		for(int k = 0; k < Blocks; ++k)
		{
			if(k == NarrowAt)
				b->setLanes(_n);
			for(int t = 0; t < Lanes; ++t)
				if(t < _n || k < NarrowAt)
					play(rng, *a, *b, t, t >= _n);

			const Block& x = a->renderBlock();
			const Block& y = b->renderBlock();
			for(int s = 0; s < BlockSize; ++s)
			{
				differing += std::memcmp(x.s[s], y.s[s], sizeof(float) * static_cast<size_t>(_n)) != 0;
				for(int l = 0; l < Lanes; ++l)
				{
					const bool above = l >= _n;
					loud += above && k >= NarrowAt && y.s[s][l] != 0.0f;
					sounded[l] |= (above ? k < NarrowAt : k >= NarrowAt) && y.s[s][l] != 0.0f;
				}
			}
		}

		// Both checks prove nothing unless the lanes sound: lanes 0 to n - 1 after the narrowing, the others before
		int silent = 0;
		for(const bool s : sounded)
			silent += !s;
		std::printf("%-12s %2d lanes: %lld samples differing, %lld not silent above, %d lanes never sounding\n", _name,
			_n, static_cast<long long>(differing), static_cast<long long>(loud), silent);
		return differing || loud || silent ? 1 : 0;
	}

	template<typename M>
	int checkAll(const char* _name)
	{
		return check<M>(_name, 4) + check<M>(_name, 8) + check<M>(_name, 12);
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);

	int failed = 0;
	failed += checkAll<BdModern>("BD MODERN");
	failed += checkAll<SdBasic>("SD BASIC");
	failed += checkAll<CyAlloy>("CY ALLOY");
	failed += checkAll<PcCarbon>("PC CARBON");
	failed += checkAll<SdVintage>("SD VINTAGE");
	failed += checkAll<CpVintage>("CP VINTAGE");
	failed += checkAll<SyToy>("SY TOY");
	failed += checkAll<SyBits>("SY BITS");
	failed += checkAll<SpTwinshot>("SP TWINSHOT");

	std::printf(failed ? "FAIL: %d\n" : "all pass\n", failed);
	return failed ? 1 : 0;
}
