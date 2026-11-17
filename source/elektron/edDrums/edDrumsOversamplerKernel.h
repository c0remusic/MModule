// edDrumsOversampler.cpp's up and down for one group of lanes, written once over the vector they run on: 4 lanes in SSE
// (edDrumsOversampler.cpp), 8 in AVX (edDrumsOversamplerAvx2.cpp, built for it). Each lane takes the same operations in
// either, a multiply then an add, never fused, so that both give the same samples. Everything here has internal
// linkage, each file keeping its own build of it, and calls no inline function of a header other files share: the AVX
// file's copy of one could be the one the linker keeps for every file.
#pragma once

#include "edDrumsOversampler.h"

namespace elektron::drums::oversampling
{
	// One half-band: its coefficients, and its history, x and y by section and lane
	struct Band
	{
		const float* c;
		int count;
		float (*x)[Lanes];
		float (*y)[Lanes];
	};

	// The 8 lanes from _l in AVX, where the caller found the CPU has it: edDrumsOversamplerAvx2.cpp
	void upAvx2(Band _first, Band _second, const Block& _in, Block* _out, int _factor, int _l);
	void downAvx2(Band _first, Band _second, const Block* _in, Block& _out, int _factor, int _l);

	namespace
	{
		// One band of a polyphase pair: a group's samples through its two all-pass chains, the coefficients taken in turn
		template<typename O>
		struct Sections
		{
			using V = typename O::V;
			V x[HalfBand::MaxCoefficients], y[HalfBand::MaxCoefficients], c[HalfBand::MaxCoefficients];
			int count;

			Sections(const Band& _band, const int _l) : count(_band.count)
			{
				for(int i = 0; i < count; ++i)
				{
					x[i] = O::load(_band.x[i] + _l);
					y[i] = O::load(_band.y[i] + _l);
					c[i] = O::set1(_band.c[i]);
				}
			}

			void keep(const Band& _band, const int _l) const
			{
				for(int i = 0; i < count; ++i)
				{
					O::store(_band.x[i] + _l, x[i]);
					O::store(_band.y[i] + _l, y[i]);
				}
			}

			void run(V& _even, V& _odd)
			{
				for(int i = 0; i < count; i += 2)
				{
					const V even = O::add(O::mul(O::sub(_even, y[i]), c[i]), x[i]);
					const V odd = O::add(O::mul(O::sub(_odd, y[i + 1]), c[i + 1]), x[i + 1]);
					x[i] = _even;
					x[i + 1] = _odd;
					y[i] = _even = even;
					y[i + 1] = _odd = odd;
				}
			}
		};

		template<typename O>
		void upGroup(const Band& _first, const Band& _second, const Block& _in, Block* _out, const int _factor, const int _l)
		{
			using V = typename O::V;
			V two[2 * BlockSize];
			Sections<O> first(_first, _l);
			for(int n = 0; n < BlockSize; ++n)
			{
				V even = O::load(&_in.s[n][_l]), odd = even;
				first.run(even, odd);
				two[2 * n] = even;
				two[2 * n + 1] = odd;
			}
			first.keep(_first, _l);
			if(_factor == 2)
			{
				for(int n = 0; n < 2 * BlockSize; ++n)
					O::store(&_out[n / BlockSize].s[n % BlockSize][_l], two[n]);
				return;
			}
			Sections<O> second(_second, _l);
			for(int n = 0; n < 2 * BlockSize; ++n)
			{
				V even = two[n], odd = even;
				second.run(even, odd);
				const int at = 2 * n;
				O::store(&_out[at / BlockSize].s[at % BlockSize][_l], even);
				O::store(&_out[(at + 1) / BlockSize].s[(at + 1) % BlockSize][_l], odd);
			}
			second.keep(_second, _l);
		}

		template<typename O>
		void downGroup(const Band& _first, const Band& _second, const Block* _in, Block& _out, const int _factor,
			const int _l)
		{
			using V = typename O::V;
			const V half = O::set1(0.5f);
			V two[2 * BlockSize];
			if(_factor == 2)
			{
				for(int n = 0; n < 2 * BlockSize; ++n)
					two[n] = O::load(&_in[n / BlockSize].s[n % BlockSize][_l]);
			}
			else
			{
				Sections<O> second(_second, _l);
				for(int n = 0; n < 2 * BlockSize; ++n)
				{
					const int at = 2 * n;
					V even = O::load(&_in[(at + 1) / BlockSize].s[(at + 1) % BlockSize][_l]);
					V odd = O::load(&_in[at / BlockSize].s[at % BlockSize][_l]);
					second.run(even, odd);
					two[n] = O::mul(O::add(even, odd), half);
				}
				second.keep(_second, _l);
			}
			Sections<O> first(_first, _l);
			for(int n = 0; n < BlockSize; ++n)
			{
				V even = two[2 * n + 1], odd = two[2 * n];
				first.run(even, odd);
				O::store(&_out.s[n][_l], O::mul(O::add(even, odd), half));
			}
			first.keep(_first, _l);
		}
	}
}
