#pragma once

#include <cstdint>

#include "elektronBlock.h"

// A drive's rate raised: HIIR's half-bands (Laurent de Soras), recomputed by its formula, in SSE over 16 lanes
namespace elektron::drums
{
	// _coefficients sections of a polyphase pair of all-passes (Valenzuela-Constantinides), of the band left below
	// _rate / 4 - _transition _rate
	class HalfBand
	{
	public:
		static constexpr int MaxCoefficients = 8;

		HalfBand(int _coefficients, double _transition);

		const float* coefficients() const { return m_c; }
		int count() const { return m_count; }

	private:
		alignas(16) float m_c[MaxCoefficients];
		int m_count;
	};

	// 2x or 4x, up and down by the same filters, 16 lanes in groups of 4
	class Oversampler
	{
	public:
		explicit Oversampler(double _rate);

		void clear(int _lane);

		// _factor 2 or 4: _in's 32 samples as _factor Blocks of 32; the groups of _lanes
		void up(const Block& _in, Block* _out, int _factor, uint16_t _lanes);
		void down(const Block* _in, Block& _out, int _factor, uint16_t _lanes);

		// The delay of a low frequency through up and down, in samples of the Block's rate, measured by Q7
		static constexpr int delay(const int _factor) { return _factor == 1 ? 0 : _factor == 2 ? 3 : 4; }

	private:
		struct Stage
		{
			alignas(16) float x[HalfBand::MaxCoefficients][Lanes]{}, y[HalfBand::MaxCoefficients][Lanes]{};
		};

		HalfBand m_first, m_second;
		Stage m_up[2], m_down[2];
	};
}
