#include "stMainMix.h"

#include "stEmac.h"

namespace syntakt::main
{
	namespace
	{
		constexpr uint32_t VelocityCurves = 0x401db1dc;	// 4 curves of 128 longs
		constexpr int32_t Unit = 0x40810204;			// 64/127: a word of 0x7f00 << 16, times 2, is full scale
	}

	template<bool Saturate>
	Pair trackGain(const Memory& _mem, const uint32_t _level, const uint32_t _velocity, const uint32_t _volume,
		const uint32_t _mode)
	{
		EmacT<Saturate> acc;
		const int32_t index = static_cast<int32_t>((_mode & 3) << 7) + static_cast<int8_t>(_velocity >> 8);
		const int32_t curve = _mem.word(VelocityCurves + 4 * static_cast<uint32_t>(index));

		acc.msac(curve, 0x7f000000);
		int32_t d0 = acc.storeClear();
		acc.msac(Unit, asl(static_cast<int32_t>(_level), 16));
		int32_t level = acc.storeClear();
		level = addSat(level, level);
		acc.msac(Unit, asl(static_cast<int32_t>(_volume), 16));
		int32_t d1 = acc.storeClear();
		d1 = addSat(d1, d1);
		acc.msac(d1, level);
		d1 = acc.storeClear();
		acc.msac(Unit, d0);
		d0 = acc.storeClear();
		acc.msac(d1, d1);
		d1 = acc.storeClear();
		d0 = addSat(d0, d0);
		acc.msac(d0, d1);
		d0 = acc.storeClear();
		return {d0, d1};
	}

	template Pair trackGain<false>(const Memory&, uint32_t, uint32_t, uint32_t, uint32_t);
	template Pair trackGain<true>(const Memory&, uint32_t, uint32_t, uint32_t, uint32_t);

	int32_t parabolicSine(const int32_t _x)
	{
		// The mix runs it with MACSR 0x20; the product never overflows, so the mode does not show
		EmacWrap acc;
		const int32_t rest = addWrap(_x, INT32_MIN);
		if(_x < 0)
			acc.mac(_x, rest);
		else
			acc.msac(_x, rest);
		const int32_t d0 = acc.storeClear();
		const int32_t twice = addWrap(d0, d0);
		return addSat(twice, twice);	// add.l then sats.l: the second add's overflow alone saturates
	}

	Pair pan(const int32_t _pan)
	{
		int32_t p = subWrap(_pan, 0x100);
		if(p < 0)
			p = 0;
		if(p > 0x7e00)
			p = 0x7e00;
		EmacWrap acc;
		acc.mac(asl(p, 16), 0x41041041);	// 0x7e00 << 16 becomes 0.5
		const int32_t angle = acc.storeClear();
		const int32_t d1 = parabolicSine(neg(angle));
		const int32_t d0 = parabolicSine(addWrap(angle, static_cast<int32_t>(0xc0000000)));
		return {d0, d1};
	}
}
