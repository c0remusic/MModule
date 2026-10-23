#pragma once

#include <cstdint>

// The mixes that ramp gains over a 32-frame block, eight frames at a time in AVX2 (stSimdAvx2.cpp), where the CPU has
// it: the scalar routines call them when avx2() says so and run their own steps otherwise. Every frame's gain is the
// first one plus the frame's number of steps (wrapping, as the steps' addWrap), each product is truncated to Q39 as
// mac.l truncates it, and a frame's products sum in 64 bits from a clear accumulator, as Emac4::sumRun takes them
// (at most 10 products, each at most 2^39 in magnitude): the same bits as the scalar code
namespace syntakt::simd
{
	constexpr int MaxChannels = 10;

	// True where the kernels run: an x86-64 CPU with AVX2 whose OS saves its registers, unless the environment's
	// SYNTAKT_NO_AVX2 is set (to anything but 0), which forces the scalar code. Decided at the first call
	bool avx2();

	// For tests: the kernels on (where the CPU has them) or off
	void setAvx2(bool _on);

	// Frames _first to 31 of a mix: each frame's sum over the channels of gain x sample, negated for msac.l, stored as
	// a fractional-mode store does (>> 8, saturated or wrapped to 32 bits). Samples are longs _srcStride bytes apart
	// (4 or 8), outputs _dstStride apart, in the big-endian CPU's order (Memory) or the host's (the voice CPU's Sram).
	// The gains are frame _first's; a channel's output frame may be its own sample's, read before it is written
	struct RampMix
	{
		const uint8_t* src[MaxChannels];
		int32_t gains[MaxChannels];
		int32_t steps[MaxChannels];
		int channels;
		uint32_t srcStride;
		uint8_t* dst;
		uint32_t dstStride;
		uint32_t first;
		bool negate;
		bool saturate;
		bool bigEndian;
	};

	void rampMixAvx2(const RampMix& _mix);

	// Longs _first to _count - 1 of the big-endian longs at _bus, in place, times the integer _gain (|_gain| <= 128),
	// saturated to 32 bits: the integer-mode mac.l and store with OMC from clear accumulators
	void scaleAvx2(uint8_t* _bus, uint32_t _first, uint32_t _count, int32_t _gain);

	// A gain after _frames steps of _step, as that many addWrap leave it
	inline int32_t ramped(const int32_t _gain, const int32_t _step, const uint32_t _frames)
	{
		return static_cast<int32_t>(static_cast<uint32_t>(_gain) + static_cast<uint32_t>(_step) * _frames);
	}
}
