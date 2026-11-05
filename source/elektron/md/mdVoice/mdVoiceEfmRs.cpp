// EFM-RS's voice code, ported as mdVoiceTrx.h says.
//
// EFM-RS ($1031A3, $1031A8, $1031BB), a sample at a time: two pairs of sines, each a sine that hears itself bending a
// second one. The first pair plays at Y:$01, Y:$02 its ratio, Y:$03 its depth and Y:$04 its feedback, the second at
// Y:$05 to Y:$08. Each second sine has a level that a Hit sets and Y:$09 or Y:$0A decays. The output is the second
// pair's plus the first's at Y:$0B, through a one-pole filter (Y:$0C).
#include "mdVoiceEfm.h"

namespace md::voice::efm::rs
{
	namespace
	{
		constexpr uint32_t kHitPhase = 0x2000;	// every sine's word at a Hit
		constexpr uint32_t kHitLevel = 0x50c335;

		// A pair of sines: its state, then the packet's words it plays by
		struct Pair
		{
			uint64_t mod;
			uint64_t car;
			int64_t feedback;
			uint32_t level;
			uint32_t pitch;
			uint32_t depth;
			uint32_t gain;
			uint32_t decay;
			int64_t modStep;
		};

		Pair loadPair(const Context& _c, const uint32_t _i)
		{
			const auto& y = _c.y;
			const uint32_t phases = 0xd + 4 * _i, feedback = 0x17 + 2 * _i, words = 1 + 4 * _i;
			return {static_cast<uint64_t>(acc(y[phases], y[phases + 1])),
				static_cast<uint64_t>(acc(y[phases + 2], y[phases + 3])),
				acc(y[feedback], y[feedback + 1]), y[0x15 + _i], y[words], y[words + 2], y[words + 3], y[9 + _i],
				mul(y[words], y[words + 1]) >> 5};
		}

		void storePair(Context& _c, const Pair& _p, const uint32_t _i)
		{
			const uint32_t phases = 0xd + 4 * _i;
			storeL(_c, phases, static_cast<int64_t>(_p.mod));
			storeL(_c, phases + 2, static_cast<int64_t>(_p.car));
			storeL(_c, 0x17 + 2 * _i, _p.feedback);
			_c.y[0x15 + _i] = _c.shared.scratch.x[0x15 + _i] = _p.level;
		}
	}

	void init(Context& _c)
	{
		_c.y[0x15] = _c.y[0x16] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		y[0xd] = y[0xf] = y[0x11] = y[0x13] = kHitPhase;
		y[0xe] = y[0x10] = y[0x12] = y[0x14] = y[0x17] = y[0x18] = y[0x19] = y[0x1a] = 0;
		y[0x15] = y[0x16] = kHitLevel;
	}

	void render(Context& _c, Voice::Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;

		// The packet's words, which the DSP copies to X:$01-$0C and reads from there
		for(uint32_t w = 1; w <= 0xc; ++w)
			ix[w] = y[w];

		// A lambda, which MSVC inlines where it called a function: with X:$1D written after the loop, 0.55 us per voice
		// per block against 0.70
		const auto step = [&t](Pair& _p)
		{
			const uint32_t modSine = sineFedBack(t, _p.mod, _p.feedback, _p.modStep, _p.gain);
			const uint32_t carSine = sineStep(t, _p.car, fmStep(modSine, _p.pitch, _p.depth));
			// The level never grows from a Hit's, so a word holds the product
			const uint32_t out = hi(round(mul(_p.level, carSine)));
			_p.level = hi(mul(_p.level, _p.decay));
			return out;
		};

		Pair first = loadPair(_c, 0), second = loadPair(_c, 1);
		uint32_t a = 0;
		uint32_t out[32];
		for(size_t k = 0; k < 32; ++k)
		{
			a = step(first);
			const uint32_t b = step(second);
			out[k] = lim(round(mul(y[0xb], a)) + acc(b));
		}
		ix[0x1d] = a;
		storePair(_c, first, 0);
		storePair(_c, second, 1);

		onePole(_c, out, y[0xc], 0x1b, 2);
		for(size_t k = 0; k < 32; ++k)
			_out[k] = sx(out[k]);
	}
}
