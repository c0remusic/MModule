// EFM-CY's voice code, ported as mdVoiceTrx.h says.
//
// EFM-CY ($1033F1, $1033F4, $10340F), a stage at a time over the block, in the internal memory that the ROM machines
// also see: four sines read from a table, at the steps Y:$06-$09 and the gain Y:$0A, fall by one level that Y:$0B
// decays. Each of the four, a block in X:$21, Y:$21, X:$41 and Y:$41, then bends the phase of a sine of its own, at the
// pitches Y:$01-$04, that hears itself through Y:$0C. The output is their mean at the amp level, which a Hit raises and
// Y:$05 decays, times twice Y:$0E, an unsigned word, limited, through two one-pole filters (Y:$0D). The DSP's table
// pointers wrap by modulo addressing: the mask here is the same for the OS's steps, all under $8000, and not above.
#include "mdVoiceEfm.h"

namespace md::voice::efm::cy
{
	namespace
	{
		constexpr uint32_t kTableMask = 0x7fff;
		constexpr uint32_t kHitModLevel = 0x7fffff;

		// A block of the table at a gain, from a pointer in the table that wraps round it
		void readTable(const TrxTables& _t, uint32_t& _pointer, const uint32_t _step, const uint32_t _gain,
			uint32_t* const _out)
		{
			uint32_t pointer = _pointer;
			for(size_t k = 0; k < 32; ++k)
			{
				_out[k] = hi(round(mul(_gain, word(_t.sine, pointer - TrxTables::SineAddr))));
				pointer = (pointer & ~kTableMask) | ((pointer + _step) & kTableMask);
			}
			_pointer = pointer;
		}

		// A sine that a block bends, the block taking its words: 24-bit words, as nothing reads the DSP's A0 or A2
		struct BentSine
		{
			uint32_t phase;
			uint32_t feedback;
			uint32_t step;
			uint32_t* io;
		};
	}

	void init(Context& _c)
	{
		_c.y[0x18] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		hitLevel(y[0x18], testHooks::EfmCyLevelLimited);
		y[0xf] = y[0x11] = y[0x13] = y[0x15] = TrxTables::SineAddr;
		y[0x10] = y[0x12] = y[0x14] = y[0x16] = 0;
		y[0x19] = y[0x1a] = y[0x1b] = y[0x1c] = y[0x1d] = y[0x1e] = y[0x1f] = y[0x20] = 0;
		y[0x17] = kHitModLevel;
	}

	void render(Context& _c, Voice::Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;
		auto& iy = _c.shared.scratch.y;
		uint32_t* const sine1 = &ix[0x21];
		uint32_t* const sine2 = &iy[0x21];
		uint32_t* const sine3 = &ix[0x41];
		uint32_t* const sine4 = &iy[0x41];

		ix[0xe] = y[0xe];

		readTable(t, y[0xf], y[6], y[0xa], sine1);
		readTable(t, y[0x11], y[7], y[0xa], sine2);
		readTable(t, y[0x13], y[8], y[0xa], sine3);
		readTable(t, y[0x15], y[9], y[0xa], sine4);

		uint32_t modLevel = y[0x17];
		for(size_t k = 0; k < 32; ++k)
		{
			sine1[k] = hi(round(mul(modLevel, sine1[k])));
			sine2[k] = hi(round(mul(modLevel, sine2[k])));
			sine3[k] = hi(round(mul(modLevel, sine3[k])));
			sine4[k] = hi(round(mul(modLevel, sine4[k])));
			modLevel = hi(mul(y[0xb], modLevel));
		}
		y[0x17] = modLevel;

		// The four in one loop, as each read waits for the last: 1.42 us per voice per block against 1.91 in four loops
		BentSine bent[4] = {{y[0x10], y[0x19], y[1], sine1}, {y[0x12], y[0x1a], y[2], sine2},
			{y[0x14], y[0x1b], y[3], sine3}, {y[0x16], y[0x1c], y[4], sine4}};
		const uint32_t gain = y[0xc];
		for(size_t k = 0; k < 32; ++k)
		{
			for(auto& b : bent)
			{
				b.feedback += b.io[k];
				const uint32_t read = (b.phase + b.feedback) & kTableMask;
				b.phase = (read - b.feedback + b.step) & 0xffffff;
				b.io[k] = t.sine[read];
				b.feedback = hi(round(mul(b.io[k], gain)));
			}
		}
		for(uint32_t i = 0; i < 4; ++i)
		{
			y[0x10 + 2 * i] = bent[i].phase;
			y[0x19 + i] = bent[i].feedback;
		}

		uint32_t amp = y[0x18];
		uint32_t out[32];
		for(size_t k = 0; k < 32; ++k)
		{
			const uint32_t mean = hi((acc(sine1[k]) + acc(sine2[k]) + acc(sine3[k]) + acc(sine4[k])) >> 2);
			out[k] = hi(round(mul(lim(shl(mulSU(amp, y[0xe]), 1)), mean)));
			amp = hi(mul(y[5], amp));
		}
		y[0x18] = amp;

		onePole(_c, out, y[0xd], 0x1d, 32);
		onePole(_c, out, y[0xd], 0x1f, 32);
		for(size_t k = 0; k < 32; ++k)
			_out[k] = sx(out[k]);
	}
}
