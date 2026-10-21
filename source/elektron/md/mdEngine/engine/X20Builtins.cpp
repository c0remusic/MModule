#include "X20Builtins.h"

#include <algorithm>
#include <vector>

namespace md::engine::x20
{
	namespace
	{
		int32_t s32(const uint32_t _x) { return static_cast<int32_t>(_x); }
		uint32_t asr(const uint32_t _x, const int _n) { return static_cast<uint32_t>(s32(_x) >> _n); }

		// X.20's tables, computed from the integer recurrences of its builders (research/02 of the map), at TUNING 0
		struct Tables
		{
			std::array<uint16_t, 192> w{};		// $2cdc80
			std::array<uint32_t, 128> l{};		// $2cde00
			std::array<uint32_t, 192> p3{};		// $2ce000
			std::vector<uint32_t> t836;			// $2bd836, 176 longs
			std::vector<uint32_t> t636;			// $2bd636, 128 longs
			std::array<uint16_t, 30> oct{};		// $2b0ad0

			Tables()
			{
				// $21bd6c at TUNING 0: x <- (x * m + 2^23) >> 24 in 64 bits
				const auto q24 = [](const uint64_t _v, const uint64_t _m) { return (_v * _m + 0x800000) >> 24; };
				uint64_t v = (uint64_t(6) << 32) | 0x892121f0;
				for(auto& e : w)
				{
					e = static_cast<uint16_t>((v + 0x800000) >> 24);
					v = q24(v, 0x100ed06);
				}
				v = uint64_t(8) << 32;
				for(auto& e : l)
				{
					e = static_cast<uint32_t>((v + 0x800000) >> 24);
					v = q24(v, 0x1016541);
				}
				uint64_t x = 0x5e7ebde0;
				for(auto& e : p3)
				{
					e = static_cast<uint32_t>((x + 0x200) & 0xffffffff) >> 10;
					x = ((x * 0xff13d513u + 0x80000000u) >> 32) & 0xffffffff;
				}
				// X.20's static initialiser, $200376-$2003dc
				uint32_t d0 = 0xfffd1b90, d1 = 0xff980fb0;
				while(d0)
				{
					t836.push_back((asr(d0, 12) + d1) & 0xffffff);
					d0 += 0x435;
					d1 += 0x972f;
				}
				for(uint32_t d = 0x3fffffff; d != 0x40000f7f; d += 0x1f)
					t636.push_back((d / (d - 0x3fffff7f) + 0x800000) & 0xffffff);
				for(size_t j = 0; j < oct.size(); ++j)
					oct[j] = static_cast<uint16_t>(((192 * (j / 3)) << 4) | (j / 3));
			}
		};

		const Tables& tables()
		{
			static const Tables t;
			return t;
		}

		struct Context
		{
			const Tables& t;
			const DecayTable& decayTable;
			int mode;

			// $2189c6: the table the context's +$90 points at, 0 past its end
			uint32_t decay(const uint32_t _i) const
			{
				return s32(_i) > 0x1ff ? 0 : decayTable[_i & 0x1ff];
			}
			uint32_t lpitch(uint32_t _x) const
			{
				_x = std::min<uint32_t>(_x, 0x3fff);
				return t.l[(_x >> 4) & 0x7f] << (_x >> 11);
			}
			// $2189b0, $218184(v, mode, $c0)
			uint32_t pitch(const uint32_t _v) const
			{
				if(mode == 0)
					return lpitch(_v);
				uint32_t d0 = _v > 0x3ffe ? 0x400 : _v >> 4;
				d0 += 0xc0;
				const uint32_t w = t.oct[d0 >> 6];
				const int shift = static_cast<int>(w & 0xf) + (mode == 2 ? -1 : mode == 3 ? 1 : 0);
				const int32_t word = static_cast<int16_t>(t.w[d0 - (w >> 4)]);
				return shift == -1 ? static_cast<uint32_t>(word >> 1) : static_cast<uint32_t>(word) << shift;
			}
			// $21822a(a, b): the DEFAULT pitch of a + b / 8 minus that of a, >> 5
			uint32_t unis(const uint32_t _a, const uint32_t _b) const
			{
				return asr(lpitch(_a + asr(_b, 3)) - lpitch(_a), 5);
			}
			// Unipolar: 0 to $7f give 0, above $3f7f $7fffff, else v << 9
			static uint32_t uni(const uint32_t _v) { return _v > 0x3f7f ? 0x7fffff : _v <= 0x7f ? 0 : _v << 9; }
			// $218624(v, mode): a time from the P3 table, octave by octave
			uint32_t tim(const uint32_t _v) const
			{
				uint32_t d0;
				int sh;
				if(_v > 0x3f7f) { d0 = 0x3ff - 0x2a0; sh = 3; }
				else
				{
					d0 = _v >> 4;
					if(_v <= 0x19ff)
					{
						if(d0 > 0xdf) { d0 += 0x60; sh = -1; }
						else if(d0 > 0x1f) { d0 += 0x120; sh = -2; }
						else { d0 += 0x1e0; sh = -3; }
					}
					else if(d0 > 0x31f)
					{
						if(d0 > 0x3df) { d0 -= 0x2a0; sh = 3; }
						else { d0 -= 0x1e0; sh = 2; }
					}
					else if(d0 > 0x25f) { d0 -= 0x120; sh = 1; }
					else { d0 -= 0x60; sh = 0; }
				}
				uint32_t r = t.p3[d0 - 0x140];
				r = sh < 0 ? r << -sh : asr(r, sh);
				if(mode == 2)
					r = s32(r) > 0x3fffff ? 0x7fffff : r * 2;
				else if(mode == 3)
					r = asr(r, 1);
				return r;
			}
			// NFX-UC's FF and FB ($2194de-$21956e): a bipolar curve, a polynomial near the centre, the decay table beyond
			uint32_t curve(const uint32_t _w) const
			{
				const uint32_t d3 = _w;
				uint32_t d1;
				uint32_t d4 = 0;
				bool poly;
				if(d3 <= 0x1fff)
				{
					if(d3 == 0) { d1 = 0x1fff; d4 = 0x185a; poly = false; }
					else
					{
						d1 = 0x2000 - d3;
						poly = d3 > 0x7ff;
						d4 = 0x185a;
					}
				}
				else
				{
					const uint32_t d4w = (_w - 0x2000) & 0xffff;
					if(d4w > 0x1f7f) { d1 = 0x1fff; d4 = 0x1859; poly = false; }
					else
					{
						d1 = (d4w * 0x2083) >> 19;
						d1 += d4w;
						poly = d1 <= 0x1800;
						d4 = 0x1859;
					}
				}
				uint32_t d0;
				if(poly)
				{
					d0 = ((0x45c29u * d1) >> 20) + d1;	// the 68k's muls.l, its low 32 bits
					d0 <<= 10;
				}
				else
				{
					const int32_t lo = static_cast<int16_t>((d1 - 0x1800) & 0xffff);
					const uint32_t idx = static_cast<uint32_t>((lo * 0xb2) >> 11) + 0x4e;
					d0 = decay(idx) + d4;
				}
				if(d3 <= 0x1fff)
					d0 = 0u - d0;
				return d0;
			}
		};

		// GND-SN-PRO, GND-SW, GND-PU ($21ada2, $21acbc, $21abd2): 9 words
		void gnd(const Context& _c, const uint8_t _id, const uint16_t* _w, uint32_t* _out)
		{
			const bool pro = _id == GndSnPro;
			_out[0] = _c.pitch(_w[0]);
			const uint32_t rel[3] = {_w[4], _w[5], _w[6]};
			const int relCount = pro ? 3 : 2;
			for(int k = 0; k < relCount; ++k)
			{
				const uint32_t d0 = rel[k] + _w[0] - 0x2000;
				_out[1 + k] = d0 > 0x3f80 || rel[k] == 0 ? 0 : _c.pitch(d0);
			}
			const int o = 1 + relCount;
			_out[o] = _c.decay(_w[1] >> 5);
			_out[o + 1] = (uint32_t(_w[2]) * _w[2]) >> 7;
			_out[o + 2] = _c.decay(_w[3] >> 5);
			if(_id == GndSw)
				_out[6] = _w[6];
			else if(_id == GndPu)
				_out[6] = uint32_t(_w[6]) * 2;
			_out[7] = _c.unis(_w[0], pro ? std::min<uint32_t>(_w[7], 0x3f80) : _w[7]);
		}

		void nfxEv(const Context& _c, const uint16_t* _w, const uint32_t _tempo24, uint32_t* _out)
		{
			const auto d1 = static_cast<uint32_t>(0x10266000 / std::clamp<uint32_t>(_tempo24, 0x1e0, 0x1a40));
			const uint32_t spb = asr(((d1 << 4) - d1) << 2, 12) & 0xffff;
			const auto ev = [&](const uint32_t _x) { return _c.decay(asr(_x * 0x50, 12) + 0x78); };
			_out[0] = (spb * _w[0]) >> 7;
			_out[1] = ev(_w[1]);
			_out[2] = ev(_w[2]);
			_out[3] = Context::uni(_w[3]);
			_out[4] = (spb * _w[4]) >> 8;
			_out[5] = ev(_w[5]);
			_out[6] = Context::uni(_w[6]);
			_out[7] = Context::uni(_w[7]);
		}

		void nfxCo(const Context& _c, const uint16_t* _w, uint32_t* _out)
		{
			const auto co = [&](const uint32_t _x) { return (0x7fffff - _c.decay(asr(_x * 0x38, 12) + 0x90)); };
			const uint32_t d1 = asr(uint32_t(_w[6]) * _w[6], 5);
			_out[0] = co(_w[0]);
			_out[1] = co(_w[1]);
			_out[2] = _c.t.t836[_w[2] >> 7];
			_out[3] = _c.t.t636[_w[3] >> 7];
			_out[4] = _c.t.t836[(_w[4] >> 7) + 0x30];
			_out[6] = Context::uni(_w[5]);
			_out[7] = Context::uni(_w[7]);
			_out[8] = asr((d1 << 4) - d1, 4) + 0x7ffff;
		}

		void nfxUc(const Context& _c, const uint16_t* _w, uint32_t* _out)
		{
			_out[0] = _c.mode ? _c.tim(_w[0]) : Context::uni(_w[0]);
			_out[1] = _c.mode ? _c.tim(_w[1]) : Context::uni(_w[1]);
			_out[2] = 0x7fffff - _c.decay(asr(uint32_t(_w[2]) * 0x38, 12) + 0x90);
			_out[3] = _w[3] <= 0x80 ? 0 : _c.decay(asr(uint32_t(_w[3]) * 0x50, 12) + 0x78);
			_out[4] = _c.curve(_w[4]);
			_out[5] = _c.curve(_w[5]);
			_out[6] = Context::uni(_w[6]);
			_out[7] = Context::uni(_w[7]);
			_out[8] = 0;
		}
	}

	const Info& info(const uint8_t _id)
	{
		static const Info gndSnPro{{"PTC1", "DEC", "RAMP", "RDEC", "PTC2", "PTC3", "PTC4", "UNIS"}, {73, 96, 0, 0, 0, 0, 0, 0}};
		static const Info gndSw{{"PTC1", "DEC", "RAMP", "RDEC", "PTC2", "PTC3", "SKEW", "UNIS"}, {73, 96, 0, 0, 0, 0, 127, 0}};
		static const Info gndPu{{"PTC1", "DEC", "RAMP", "RDEC", "PTC2", "PTC3", "WIDT", "UNIS"}, {73, 96, 0, 0, 0, 0, 127, 0}};
		static const Info nfxEvInfo{{"DEL", "ATK", "DEC", "SUS", "HOLD", "REL", "RING", "NBAL"}, {0, 32, 16, 100, 64, 32, 0, 127}};
		static const Info nfxCoInfo{{"ATK", "REL", "THRE", "RTIO", "KNEE", "SIDE", "MKUP", "NBAL"}, {64, 16, 96, 32, 127, 127, 0, 127}};
		static const Info nfxUcInfo{{"TIM1", "TIM2", "TD", "TI", "FF", "FB", "DMIX", "NBAL"}, {16, 0, 64, 0, 80, 64, 0, 127}};
		switch(_id)
		{
		case GndSnPro:	return gndSnPro;
		case GndPu:		return gndPu;
		case NfxEv:		return nfxEvInfo;
		case NfxCo:		return nfxCoInfo;
		case NfxUc:		return nfxUcInfo;
		default:		return gndSw;
		}
	}

	int compute(const uint8_t _id, const uint16_t* _params, const int _mode, const DecayTable& _decay,
		const uint32_t _tempo24, uint32_t* _out)
	{
		const Context c{tables(), _decay, _mode};
		switch(_id)
		{
		case GndSnPro:
		case GndSw:
		case GndPu:	gnd(c, _id, _params, _out);		return 9;
		case NfxEv:	nfxEv(c, _params, _tempo24, _out);	return 9;
		case NfxCo:	nfxCo(c, _params, _out);			return 10;
		case NfxUc:	nfxUc(c, _params, _out);			return 10;
		default:	return 0;
		}
	}
}
