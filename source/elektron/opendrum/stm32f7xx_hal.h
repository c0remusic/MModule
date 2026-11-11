// SPDX-License-Identifier: GPL-3.0-only
// Part of this port (MModule), linked with the GPL-3.0 voice sources of this folder.
// Stands in for the STM32 HAL that the ported voice sources include: the Cortex-M7 intrinsics they use, as plain C,
// and the GCC attributes and C99 inline they are written with, for MSVC, clang and GCC alike. Part of MModule.
#pragma once

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#include <intrin.h>
#define __attribute__(x)
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline int32_t __SSAT_f(const int32_t _x, const int _bits)
{
	const int32_t max = (1 << (_bits - 1)) - 1;
	return _x > max ? max : _x < -max - 1 ? -max - 1 : _x;
}
#define __SSAT(x, b) __SSAT_f((int32_t)(x), (b))

static inline int16_t __sat16(const int32_t _v)
{
	return (int16_t)(_v > 32767 ? 32767 : _v < -32768 ? -32768 : _v);
}

// Two saturating 16-bit additions or subtractions in one word, as QADD16 and QSUB16
static inline uint32_t __QADD16(const uint32_t _a, const uint32_t _b)
{
	return (uint16_t)__sat16((int16_t)_a + (int16_t)_b)
		| (uint32_t)(uint16_t)__sat16((int16_t)(_a >> 16) + (int16_t)(_b >> 16)) << 16;
}

static inline uint32_t __QSUB16(const uint32_t _a, const uint32_t _b)
{
	return (uint16_t)__sat16((int16_t)_a - (int16_t)_b)
		| (uint32_t)(uint16_t)__sat16((int16_t)(_a >> 16) - (int16_t)(_b >> 16)) << 16;
}

// Leading zeros, 32 for 0, as CLZ
static inline uint8_t __CLZ(const uint32_t _x)
{
	if(!_x)
		return 32;
#ifdef _MSC_VER
	unsigned long index;
	_BitScanReverse(&index, _x);
	return (uint8_t)(31 - index);
#else
	return (uint8_t)__builtin_clz(_x);
#endif
}

#define I2S_AUDIOFREQ_44K 44100u
