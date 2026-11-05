// EFM-CB's voice code, ported as mdVoiceTrx.h says.
//
// EFM-CB ($103272, $103277, $10328C), a sample at a time: two pairs of sines at the pitches Y:$01 and Y:$02, each a
// sine at Y:$03 of its pitch, both at the one level that Y:$05 decays, bending by Y:$04 a second sine that hears itself
// through Y:$06. The output is the mean of the two pairs times the sum of two more levels, Y:$07 and Y:$09 of the words
// that Y:$08 and Y:$0A decay. Neither that sum nor its product is limited.
#include "mdVoiceEfm.h"

namespace md::voice::efm::cb
{
	namespace
	{
		constexpr uint32_t kHitModLevel = 0x7fffff;
		constexpr uint32_t kHitLevel = 0x5a8279;

		// A pair of sines: its state, then the packet's words it plays by
		struct Pair
		{
			uint64_t mod;
			uint64_t car;
			int64_t feedback;
			uint32_t pitch;
			int64_t modStep;
		};

		Pair loadPair(const Context& _c, const uint32_t _i)
		{
			const auto& y = _c.y;
			const uint32_t phases = 0xb + 4 * _i, feedback = 0x16 + 2 * _i;
			return {static_cast<uint64_t>(acc(y[phases], y[phases + 1])),
				static_cast<uint64_t>(acc(y[phases + 2], y[phases + 3])), acc(y[feedback], y[feedback + 1]),
				y[1 + _i], mul(y[1 + _i], y[3]) >> 5};
		}

		void storePair(Context& _c, const Pair& _p, const uint32_t _i)
		{
			const uint32_t phases = 0xb + 4 * _i;
			storeL(_c, phases, static_cast<int64_t>(_p.mod));
			storeL(_c, phases + 2, static_cast<int64_t>(_p.car));
			storeL(_c, 0x16 + 2 * _i, _p.feedback);
		}
	}

	void init(Context& _c)
	{
		_c.y[0x14] = _c.y[0x15] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		y[0xb] = y[0xc] = y[0xd] = y[0xe] = y[0xf] = y[0x10] = y[0x11] = y[0x12] = 0;
		y[0x16] = y[0x17] = y[0x18] = y[0x19] = 0;
		y[0x13] = kHitModLevel;
		y[0x14] = y[0x15] = kHitLevel;
	}

	void render(Context& _c, Voice::Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;

		// The packet's words, which the DSP copies to X:$01-$0A and reads from there
		for(uint32_t w = 1; w <= 0xa; ++w)
			ix[w] = y[w];

		const auto sample = [&t, &y](Pair& _p, const uint32_t _modLevel)
		{
			const uint32_t modOut = hi(round(mul(_modLevel, sineStep(t, _p.mod, _p.modStep))));
			return sineFedBack(t, _p.car, _p.feedback, fmStep(modOut, _p.pitch, y[4]), y[6]);
		};

		Pair first = loadPair(_c, 0), second = loadPair(_c, 1);
		uint32_t modLevel = y[0x13], levelA = y[0x14], levelB = y[0x15];
		uint32_t a = 0;
		uint32_t out[32];
		for(size_t k = 0; k < 32; ++k)
		{
			a = sample(first, modLevel);
			const uint32_t b = sample(second, modLevel);
			modLevel = hi(mul(modLevel, y[5]));

			const uint32_t mean = hi((acc(a) + acc(b)) >> 1);
			const uint32_t sum = hi(round(mul(y[7], levelA)) + round(mul(y[9], levelB)));
			levelA = hi(mul(y[8], levelA));
			levelB = hi(mul(y[0xa], levelB));
			out[k] = hi(round(mul(sum, mean)));
		}
		ix[0x1a] = a;
		storePair(_c, first, 0);
		storePair(_c, second, 1);
		y[0x13] = ix[0x13] = modLevel;
		y[0x14] = ix[0x14] = levelA;
		y[0x15] = ix[0x15] = levelB;

		for(size_t k = 0; k < 32; ++k)
			_out[k] = sx(out[k]);
	}
}
