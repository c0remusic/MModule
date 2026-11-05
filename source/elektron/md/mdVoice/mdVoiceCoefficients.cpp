// OS 1.63's coefficient functions (ColdFire, the machine descriptors' function pointers), one by machine, from the 68k
// listing: each is count = f(out, params), its word n at out + 4n. The arithmetic is the listing's, the compiler's
// divisions by a constant included (x * m >> s), so that a word can be checked against it line by line.
#include "mdVoiceCoefficients.h"
#include "mdVoiceInternal.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace md::voice
{
	namespace
	{
		struct In
		{
			const OsTables& t;
			std::array<uint32_t, 8> p;
			bool trigger;
			uint32_t tempo;
		};

		constexpr uint32_t k1 = 0x7fffff;	// full scale
		constexpr uint32_t kHoldFilter = 0x7a1200;

		// A 7-bit-mantissa pitch: _x's low 7 bits in the octave's table, its upper bits and _octaves its shift
		uint32_t pitched(const In& _in, const uint32_t _x, const int _octaves)
		{
			return _in.t.pitch[_x & 0x7f] << ((_x >> 7) + _octaves);
		}
		constexpr uint32_t level(const uint32_t _p) { return (_p << 9) & 0x1fffe00; }
		
		int gnd(const In&, uint32_t*)
		{
			return 2;
		}

		uint32_t gndImCount(const uint32_t _p)
		{
			if(_p <= 0x1fff)
				return _p >> 7;
			const uint32_t d = _p - 0x2000;
			return (d * d >> 14) + 0x40;
		}

		int gndIm(const In& _in, uint32_t* _o)
		{
			const auto& p = _in.p;
			_o[1] = gndImCount(p[0]);
			_o[2] = p[1] * p[1] >> 5;
			_o[3] = gndImCount(p[2]);
			_o[4] = p[3] * p[3] >> 5;
			return 5;
		}

		int trxBd(const In& _in, uint32_t* _o)
		{
			const auto& p = _in.p;
			const auto& decay = _in.t.decay;
			_o[3] = pitched(_in, (p[0] >> 6) + 0x40, 0) >> 1;
			_o[1] = decay[120 + (p[1] * 0x50 >> 12)];
			_o[4] = ((p[2] * p[2] >> 18) * p[2] >> 14) + 3;
			_o[5] = decay[(p[3] * 0x46 >> 12)];
			_o[2] = level(p[4]);
			if(p[5] <= 0x1fff)
			{
				_o[11] = p[5] << 8;
				_o[12] = decay[0];
			}
			else
			{
				_o[11] = 0x200000;
				_o[12] = decay[(p[5] * 0x28 - 0x50000) >> 12];
			}
			_o[13] = level(p[6]);
			const uint32_t d = p[7] * 125;
			_o[8] = 0x1fffff - d;
			_o[9] = d - 0x200000;
			_o[10] = (p[7] * p[7] >> 14) * 511;
			if(p[7] <= 0x1fff)
			{
				_o[6] = k1;
				_o[7] = 0xff800000;
			}
			else
			{
				_o[6] = 0x1c7fff - p[7] * 100;
				_o[7] = p[7] * 100 - 0x1c8000;
			}
			return 14;
		}

		int trxSd(const In& _in, uint32_t* _o)
		{
			const auto& p = _in.p;
			const auto& decay = _in.t.decay;
			const uint32_t step = p[0] >> 2;
			_o[7] = step + 0x1000;
			_o[11] = (step >> 3) + 0x400;
			const uint32_t d = p[1] >> 7;
			_o[1] = decay[220 + d];
			_o[2] = decay[232 + (d >> 1)];
			_o[5] = p[4] << 9;
			if(p[4] > 0x1fff)
			{
				const uint32_t e = p[4] - 0x2000;
				_o[6] = (e * e >> 4) & 0xfffffc00;
			}
			_o[3] = k1 - (p[5] << 8);
			_o[4] = p[5] << 8;
			_o[8] = (_o[7] >> 6) * ((p[6] >> 9) + 0xb9) >> 1;
			_o[10] = p[2] * p[2] >> 18;
			_o[12] = decay[15 + (p[3] * 5 >> 10)];
			_o[9] = _in.t.rise[120 + (p[7] * 0x62b3 >> 21)];
			return 13;
		}

		// TRX-XT and TRX-XC, which differ in their pitch's octave and their decay's scale
		int trxXt(const In& _in, uint32_t* _o, const uint32_t _pitch, const int _octaves, const uint32_t _decayScale)
		{
			const auto& p = _in.p;
			_o[2] = pitched(_in, _pitch, _octaves);
			_o[1] = _in.t.decay[200 + (p[1] * _decayScale >> 12)];
			_o[3] = (p[2] + 3) >> 6;
			_o[7] = p[3] * 15 + 0x7c2035;
			_o[8] = k1 - (p[4] << 8);
			_o[9] = (p[4] << 8) + 0x400000;
			const uint32_t d = p[5] * 0x180;
			_o[4] = k1 - d;
			_o[5] = d + 0xff800001;
			_o[6] = (p[6] * p[6] >> 13) & 0xfffffffe;
			return _in.trigger ? 10 : 8;
		}

		// x / n for the divisions the compiler gives a rounding step: (x - h) / 2 + h, h = x * m >> 16
		constexpr uint32_t roundedHigh(const uint32_t _x, const uint32_t _m)
		{
			const uint32_t h = _x * _m >> 16;
			return (((_x - h) & 0xffff) >> 1) + h;
		}

		int trxCp(const In& _in, uint32_t* _o)
		{
			if(!_in.trigger)
				return 0;
			const auto& p = _in.p;
			const uint32_t d = p[0] * 0x119;
			_o[1] = d + 0x399999;
			_o[2] = (d >> 1) + 0x333333;
			_o[7] = (p[1] >> 8) + 0x6a0;
			_o[5] = (p[1] >> 5) + 0x2c0;
			_o[6] = _o[8] = (p[2] + 0x30) >> 8;
			_o[11] = level(p[5]);
			_o[3] = _in.t.decay[144 + (p[3] >> 7)];
			_o[4] = _in.t.decay[85 + (p[6] >> 8)];
			_o[9] = (p[7] >> 5) + 0x400;
			_o[10] = 0x40;
			_o[12] = (roundedHigh(p[4], 0xc01d) >> 9) + 1;
			_o[13] = (p[4] * 0x41ab >> 23) + 1;
			_o[14] = (roundedHigh(p[4], 0x7fd1) >> 9) + 1;
			return 15;
		}

		int trxRs(const In& _in, uint32_t* _o)
		{
			const auto& p = _in.p;
			_o[2] = (p[0] >> 2) + 0x1000;
			_o[1] = _in.t.decay[220 + (p[1] * 0x28 >> 12)];
			_o[3] = k1 - (p[2] << 5);
			_o[4] = p[2] << 4;
			return 5;
		}

		int trxCb(const In& _in, uint32_t* _o)
		{
			const auto& p = _in.p;
			_o[3] = (roundedHigh(p[0], 0x51d1) >> 6) + 0x2c7;
			_o[4] = ((p[0] >> 1) * 0xc7cf >> 21) + 0x3d7;
			_o[1] = _in.t.rise[220 + (p[1] * 0x2d >> 12)];
			_o[9] = _o[10] = 0x4e2 - (p[2] >> 7);
			_o[5] = 0x5fffff - (p[4] << 8);
			_o[6] = (p[4] << 8) + 0x200000;
			_o[7] = _o[8] = p[3] * 0x180 + 0x1e0000;
			_o[2] = _in.t.rise[100 + (p[5] * 0x24 >> 12)];
			_o[11] = p[5] * 0xaaab >> 17;
			return _in.trigger ? 12 : 11;
		}

		// TRX-CH and TRX-OH, which differ in words 2 and 10
		int trxCh(const In& _in, uint32_t* _o, const uint32_t _decay, const uint32_t _word10)
		{
			const auto& p = _in.p;
			_o[2] = _in.t.decay[227 + _decay];
			_o[10] = _word10;
			_o[3] = (p[3] * 0xac77 >> 22) + 0x680;
			_o[6] = (p[2] * 0x2d83 >> 20) + 0x600;
			_o[4] = _o[7] = 0x46;
			_o[5] = 0x80000 - p[4] * 25;
			_o[8] = p[4] * 14 + 0x10000;
			_o[9] = 1;
			_o[1] = 0x126;
			return _in.trigger ? 11 : 9;
		}

		int trxCy(const In& _in, uint32_t* _o)
		{
			const auto& p = _in.p;
			const uint32_t tone = p[0];
			// Six partials, each a line of the tone with its own kinks, as the listing's branches
			const uint32_t by28 = (tone >> 2) * 0x4925 >> 17;
			const uint32_t by32 = tone >> 5;
			const uint32_t a = tone <= 0x1fff ? by32 : 0x200 - by32;
			uint32_t b = by28 - 0x7b;
			uint32_t c = by28 - 0xd7;
			if(tone > 0x2a2f)
			{
				b = 0x200 - ((tone >> 1) * 0xc30d >> 20);
				if(tone > 0x3461)
					c = 0x1b9 - (tone * 0xe071 >> 22);
			}
			const uint32_t d = by32 - 0x100;
			const uint32_t e = tone <= 0x8bf ? 0xffffff1a - (tone * 0xbc53 >> 22) : (tone * 0x469f >> 19) - 0x14d;
			const uint32_t f = tone <= 0x12f1 ? 0xffffff66 - (roundedHigh(tone, 0x5c99) >> 5) : by28 - 0x1ac;
			const uint32_t scale = 0x800 - (tone >> 3);
			_o[2] = a * scale + 0x63fbc;
			_o[3] = b * scale + 0x63fbc;
			_o[4] = c * scale + 0x63fbc;
			_o[5] = d * scale + 0x63fbc;
			_o[6] = e * scale + 0x63fbc;
			_o[7] = f * scale + 0x63fbc;
			_o[1] = p[1] * 2 + 0x7f67fd;
			_o[13] = p[1] * 2 + 0x7f263b;
			_o[11] = p[2] * 0x180 + 0x200000;
			_o[8] = (p[3] >> 6) + 0x600;
			_o[9] = (p[4] >> 6) + 0x400;
			_o[10] = (p[4] >> 6) + 0x600;
			_o[12] = (p[5] * 0xaaab >> 24) + 0x20;
			return 14;
		}

		int trxMa(const In& _in, uint32_t* _o)
		{
			if(!_in.trigger)
				return 0;
			const auto& p = _in.p;
			_o[1] = (p[0] >> 6) + 0xa;
			_o[2] = 0x800000 / ((p[0] >> 1) + 0x2a);
			_o[3] = (p[1] >> 6) + 1;
			_o[4] = (p[2] >> 6) + 0xa;
			_o[5] = 0x800000 / ((p[2] >> 3) + 0x10);
			_o[6] = 0x783a3c - (p[3] << 7);
			_o[11] = (p[4] << 6) / (_o[1] + _o[3] + _o[4]);
			_o[10] = (p[5] >> 4) + 0x400;
			_o[7] = (p[6] >> 6) + 0x600;
			_o[9] = (p[6] >> 6) + 0x5e0;
			_o[8] = p[7] >> 8;
			return 12;
		}

		int trxCl(const In& _in, uint32_t* _o)
		{
			if(!_in.trigger)
				return 0;
			const auto& p = _in.p;
			_o[3] = (p[0] * 0xcccd >> 18) + 0x578;
			_o[5] = (roundedHigh(p[0], 0x8f9d) >> 5) + 0x458;
			_o[1] = _in.t.decay[200 + (p[1] * 0x28 >> 12)];
			_o[2] = (p[2] + 0x1f4) >> 5;
			_o[6] = (p[3] >> 8) + 0x40;
			_o[4] = (p[4] * 0xcccd >> 18) + 0x578;
			_o[7] = (p[4] << 8) + 0x100000;
			_o[8] = 0 - (p[5] << 9);
			return 9;
		}

		// A sweep's decay from _base, in the upper half of _p, and its rise, in the lower half
		void s2Sweep(const In& _in, const uint32_t _p, const uint32_t _base, uint32_t& _decay, uint32_t& _rise)
		{
			if(_p <= 0x1fff)
			{
				_decay = _in.t.decay[_base];
				_rise = _p >> 6;
			}
			else
			{
				_decay = _in.t.decay[_base + ((_p - 0x2000) >> 6)];
				_rise = 0x80;
			}
		}

		int trxS2(const In& _in, uint32_t* _o)
		{
			const auto& p = _in.p;
			_o[7] = (p[0] >> 2) + 0x7d0;
			s2Sweep(_in, p[1], 130, _o[2], _o[5]);
			_o[12] = (p[2] << 6) & 0x3fffc0;
			s2Sweep(_in, p[3], 45, _o[13], _o[20]);
			_o[6] = p[4] << 7;
			_o[18] = 0x7270e0 - (p[7] << 7);
			_o[9] = p[7] <= 0x1fff ? p[7] << 7 : (p[7] << 9) + 0xffd00000;
			_o[3] = (k1 - (p[5] << 7)) >> 1;
			_o[4] = p[5] << 6;
			_o[8] = ((p[0] * 3 >> 4) * 1495 + 0x2eb800) >> 10;
			_o[10] = p[4] * p[4] >> 23;
			_o[11] = p[4] * p[4] >> 22;
			_o[17] = 0x76c;
			_o[1] = (p[6] >> 4) + 0x400;
			return 21;
		}

		int trxB2(const In& _in, uint32_t* _o)
		{
			const auto& p = _in.p;
			const auto& t = _in.t;
			_o[1] = (t.pitch[p[0] >> 7] - 0x800) * 9 + 0x76e;
			_o[2] = p[2] * 76087 >> 14;
			_o[3] = t.decay[100 + (p[1] * 3 >> 7)];
			_o[4] = t.rise[200 + (p[3] >> 6)];
			const uint32_t d = p[7] * p[7] >> 12;
			_o[5] = 0xffff8000 - d * 60;
			_o[6] = d * 70 + 0x8000;
			_o[7] = 0xffff8000 - d * 50;
			_o[8] = d * 40 + 0x8000;
			_o[9] = t.octaves[p[6] >> 7] * 24 / 0x26d0b;
			_o[10] = (1u << _o[9]) >> 1;
			_o[11] = level(p[5]);
			_o[12] = (p[4] << 8) & 0xffff00;
			return 13;
		}

		uint32_t efmDecay(const In& _in, const uint32_t _p, const uint32_t _m)
		{
			return _in.t.decay[147 + (_p * _m >> 12)];
		}

		// An FM index: at least $80 at its scale
		constexpr uint32_t efmIndex(const uint32_t _p, const int _shift)
		{
			return std::max(_p << _shift, 1u << (_shift + 7));
		}

		// Words 2-5, which four EFM machines share: the decay of p1, the index p5, the level of p4, the decay of p6
		void efmHead(const In& _in, uint32_t* _o, const int _indexShift)
		{
			const auto& p = _in.p;
			_o[2] = efmDecay(_in, p[1], 0x46);
			_o[3] = efmIndex(p[5], _indexShift);
			_o[4] = level(p[4]);
			_o[5] = efmDecay(_in, p[6], 0x52);
		}

		int efmBd(const In& _in, uint32_t* _o)
		{
			const auto& p = _in.p;
			_o[1] = pitched(_in, (p[0] >> 5) + 0x20, 1);
			efmHead(_in, _o, 9);
			_o[6] = level(p[7]);
			_o[7] = (p[2] << 9) + 0xfff00000;
			_o[8] = efmDecay(_in, p[3], 0x4b);
			return 9;
		}

		int efmSd(const In& _in, uint32_t* _o)
		{
			const auto& p = _in.p;
			_o[1] = pitched(_in, (p[0] * 5 >> 8) + 0x40, 3);
			efmHead(_in, _o, 9);
			_o[6] = kHoldFilter;
			_o[7] = (p[2] << 8) & 0xffff00;
			_o[8] = efmDecay(_in, p[3], 0x32);
			_o[9] = 0x44aa20;
			_o[10] = k1 - (p[7] << 8);
			return 11;
		}

		int efmXt(const In& _in, uint32_t* _o)
		{
			const auto& p = _in.p;
			_o[1] = pitched(_in, p[0] >> 6, 2);
			efmHead(_in, _o, 9);
			_o[6] = kHoldFilter;
			_o[7] = (p[2] << 9) + 0xfff00000;
			_o[8] = efmDecay(_in, p[3], 0x4b);
			_o[9] = 0x7de290;
			_o[10] = p[7] >> 1;
			return 11;
		}

		int efmCp(const In& _in, uint32_t* _o)
		{
			const auto& p = _in.p;
			_o[1] = pitched(_in, (p[0] * 0xb >> 8) + 0x40, 3);
			efmHead(_in, _o, 7);
			_o[6] = kHoldFilter;
			_o[9] = k1 - (p[7] << 8);
			_o[10] = p[2] >> 10;
			_o[11] = _in.t.decay[70 + (p[3] * 0x28 >> 12)];
			return 12;
		}

		int efmRs(const In& _in, uint32_t* _o)
		{
			const auto& p = _in.p;
			_o[1] = pitched(_in, (p[5] * 5 >> 8) + 0x40, 3);
			_o[2] = 0x600000;
			_o[3] = efmIndex(p[7], 9);
			_o[4] = kHoldFilter;
			_o[5] = pitched(_in, (p[0] >> 5) + 0x40, 4);
			_o[6] = 0x600000;
			_o[7] = (p[2] << 7) & 0x7fff80;
			_o[8] = kHoldFilter;
			_o[9] = efmDecay(_in, p[6], 0x46);
			_o[10] = efmDecay(_in, p[1], 0x46);
			_o[11] = level(p[4]);
			_o[12] = k1 - (p[3] << 8);
			return 13;
		}

		int efmCb(const In& _in, uint32_t* _o)
		{
			const auto& p = _in.p;
			_o[1] = pitched(_in, p[0] * 5 >> 8, 4);
			_o[2] = _o[1] * 379 >> 8;
			_o[3] = efmIndex(p[5], 9);
			_o[4] = (p[4] << 8) & 0xffff00;
			_o[5] = efmDecay(_in, p[6], 0x52);
			_o[6] = p[3] * 0xc0;
			_o[7] = 0x7ffe00 - p[2] * 0x1a4;
			_o[9] = p[2] * 0x1a4;
			_o[8] = efmDecay(_in, p[1], 0x46);
			_o[10] = _in.t.decay[100 + (p[1] * 19 >> 11)];
			return 11;
		}

		// 60 (270950400 / tempo) >> 12, what a count of samples by the tempo is scaled by
		uint32_t tempoStep(const uint32_t _tempo)
		{
			return (0x10266000 / _tempo) * 60 >> 12;
		}

		// ROM-01 to -24 (First) and ROM-25 to -48 differ in words 3, 4 and 5 only
		template<bool First>
		int rom(const In& _in, uint32_t* _o)
		{
			const auto& p = _in.p;
			const auto& pitch = _in.t.pitch;
			const uint32_t tone = p[0];
			if(tone <= 0xdff)
				_o[1] = pitch[(tone / 28) & 0x7f] << 8;
			else if(tone <= 0x31ff)
				_o[1] = pitch[((tone << 7) - 0x70000) / 0x1200 & 0x7f] << ((tone - 0xe00) / 0x1200 + 9);
			else
				_o[1] = pitch[((tone << 7) - 0x190000) / 0xe00 & 0x7f] << ((tone - 0x3200) / 0xe00 + 11);

			_o[2] = p[1] > 0x3f40 ? 0x800000 : 0 - _in.t.decay[100 + (((p[1] << 2) / 3) >> 6)];

			const uint32_t step = tempoStep(_in.tempo);
			_o[4] = First ? p[2] * step >> 8 : p[2] * step >> 7;
			_o[3] = First ? p[4] * p[4] >> 5 : level(p[4]);
			if(p[5] > 0x3f40)
				_o[5] = 0x400000;
			else
				_o[5] = First ? p[5] * p[5] >> 6 : p[5] << 8;

			const uint32_t octaves = _in.t.octaves[p[3] >> 7];
			const uint32_t rate = pitch[(octaves << 7) / 0x2281 & 0x7f] << (octaves / 0x2281) >> 6;
			_o[7] = rate;
			_o[6] = 0x4000000 / rate;

			if(p[6] > 0x3f40)
				_o[8] = 0x7f0000;
			else if(p[6] <= 0x1fff)
				_o[8] = (p[6] + 0x40) >> 7;
			else
				_o[8] = (p[6] * p[6] >> 21) + 0x20;

			uint32_t retrig;
			if(p[7] <= 0x1fff)
				retrig = p[7] * step >> 11;
			else
			{
				const uint32_t x = ((p[7] << 7) / 0x7f) >> 5;
				retrig = ((pitch[x & 0x7f] >> (5 - (x >> 7))) * 1058400 / _in.tempo) * 60 >> 10;
			}
			_o[9] = std::max(retrig, 0x44u);
			return 10;
		}

		int trxXtMachine(const In& _in, uint32_t* _o) { return trxXt(_in, _o, (_in.p[0] >> 6) + 0x60, 0, 0x37); }
		int trxXc(const In& _in, uint32_t* _o) { return trxXt(_in, _o, _in.p[0] >> 6, 1, 0x28); }
		int trxChMachine(const In& _in, uint32_t* _o) { return trxCh(_in, _o, _in.p[0] >> 8, _in.p[1] >> 6); }
		int trxOh(const In& _in, uint32_t* _o)
		{
			return trxCh(_in, _o, _in.p[0] >> 7, (_in.p[1] * 0x8889 >> 19) + 0x25);
		}

		using Function = int (*)(const In&, uint32_t*);

		// nullptr for a machine not ported
		Function function(const uint8_t _machine)
		{
			if(_machine >= 128 && _machine <= 151)
				return rom<true>;
			if(isRom(_machine + 1u))
				return rom<false>;
			switch(_machine)
			{
			case 0:		return gnd;
			case 3:		return gndIm;
			case 16:	return trxBd;
			case 17:	return trxSd;
			case 18:	return trxXtMachine;
			case 19:	return trxCp;
			case 20:	return trxRs;
			case 21:	return trxCb;
			case 22:	return trxChMachine;
			case 23:	return trxOh;
			case 24:	return trxCy;
			case 25:	return trxMa;
			case 26:	return trxCl;
			case 27:	return trxXc;
			case 28:	return trxB2;
			case 29:	return trxS2;
			case 32:	return efmBd;
			case 33:	return efmSd;
			case 34:	return efmXt;
			case 35:	return efmCp;
			case 36:	return efmRs;
			case 37:	return efmCb;
			default:	return nullptr;
			}
		}
	}

	OsTables::OsTables()
	{
		// Both decays are the same curve: 2^23 e^-(a e^(-n i / 511)). The constants hold every word of the OS's tables.
		for(size_t i = 0; i < pitch.size(); ++i)
			pitch[i] = static_cast<uint32_t>(std::llround(2048.0 * std::pow(2.0, static_cast<double>(i) / 127.5)));
		for(size_t i = 0; i < decay.size(); ++i)
		{
			const double x = static_cast<double>(i) / 511.0;
			decay[i] = static_cast<uint32_t>(std::llround(8388608.0 * std::exp(-0.29643787 * std::exp(-12.0 * x))));
			rise[i] = static_cast<uint32_t>(std::llround(8388607.0 * std::exp(-517.918697 * std::exp(-16.0 * x))));
		}
		for(size_t i = 0; i < octaves.size(); ++i)
			octaves[i] = static_cast<uint32_t>(std::llround(32767.0 * std::log(1.0 + static_cast<double>(i))));
	}

	bool coefficientsPorted(const uint8_t _machine)
	{
		return function(_machine) != nullptr;
	}

	int coefficients(const OsTables& _tables, const uint8_t _machine, const uint16_t* _params, const bool _trigger,
		const uint32_t _tempo, Packet& _out)
	{
		const auto f = function(_machine);
		if(!f)
			throw std::out_of_range("no coefficient function for this machine");
		In in{_tables, {}, _trigger, _tempo};
		for(size_t k = 0; k < in.p.size(); ++k)
			in.p[k] = _params[k] & 0x3fffu;
		_out.fill(0);
		const int count = f(in, _out.data());
		_out[0] = _trigger ? static_cast<uint32_t>(_machine) + 1 : 0;
		return std::max(count, 1);
	}
}
