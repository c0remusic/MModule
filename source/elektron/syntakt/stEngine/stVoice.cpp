#include "stVoice.h"

#include "stEmac.h"

namespace syntakt
{
	namespace
	{
		void clear(Voice& _v, const std::initializer_list<uint32_t> _offsets)
		{
			for(const auto o : _offsets)
				_v.at(o) = 0;
		}
	}

	void drawRandom(int32_t* _dst, const int32_t _count, int32_t* _state)
	{
		for(int32_t i = 0; i < _count; ++i)
		{
			const int32_t a = addWrap(_state[0], _state[1]);
			_state[0] = a;
			_state[1] = addWrap(_state[1], a);
			_dst[i] = a;
		}
	}

	void initVoice(Voice& _v, Globals& _g, const Rom& _rom)
	{
		// The order of the writes is the original's: units overlap, and a later write wins
		_v.at(0x40) = static_cast<int32_t>(0xfff40000);
		for(uint32_t o = 0; o <= 0x3c; o += 4)
			_v.at(o) = 0;
		_v.at(0x44) = 0x1fffcb;
		_v.at(0x48) = 0;
		_v.at(0x4c) = 0;

		for(uint32_t k = 0; k < 7; ++k)
		{
			const uint32_t u = Voice::UnitStride * k;
			_v.at(u + 0x50) = 0x04000000;
			_v.at(u + 0x54) = INT32_MIN;
			_v.at(u + 0x64) = 0x40000000;
			_v.at(u + 0x9c) = static_cast<int32_t>(bufferA(0));
			_v.at(u + 0xa0) = static_cast<int32_t>(bufferA(0));
			for(uint32_t o = 0xb0; o <= 0xbc; o += 4)
				_v.at(u + o) = static_cast<int32_t>(Sine);
			_v.at(u + 0xc8) = static_cast<int32_t>(bufferA(k));
			_v.at(u + 0xcc) = static_cast<int32_t>(bufferB(k));
			for(const uint32_t o : {0x58u, 0x5cu, 0x60u, 0x68u, 0x6cu, 0x70u, 0x74u, 0x78u, 0x7cu, 0x80u, 0x84u, 0x88u,
				0x8cu, 0x90u, 0x94u, 0x98u, 0xa4u, 0xa8u, 0xacu, 0xc0u, 0xc4u})
				_v.at(u + o) = 0;
			const uint32_t m = 0x640 + 0x10 * k;
			_v.at(m) = 0;
			_v.at(m + 4) = 0x68db9;
			_v.at(m + 8) = 0;
			_v.at(m + 12) = 0x7fffffb6;
		}

		_v.at(0x414) = INT32_MAX;
		_v.at(0x420) = INT32_MAX;
		_v.at(0x3e8) = INT32_MIN;
		_v.at(0x40c) = INT32_MIN;
		_v.at(0x42c) = static_cast<int32_t>(_v.address + 0x420);
		_v.at(0x438) = 5;
		_v.at(0x3d8) = 1;
		_v.at(0x3dc) = 1;
		_v.at(0x3fc) = 1;
		_v.at(0x408) = 1;
		_v.at(0x430) = INT32_MAX;
		_v.at(0x434) = INT32_MAX;
		_v.at(0x440) = 0x20000000;
		_v.at(0x43c) = 0x66666666;
		_v.at(0x444) = INT32_MAX;
		_v.at(0x450) = 1;
		clear(_v, {0x3d0, 0x3d4, 0x3e0, 0x3e4, 0x3ec, 0x3f0, 0x3f4, 0x400, 0x404, 0x410, 0x418, 0x41c, 0x424, 0x428,
			0x448, 0x44c, 0x454});
		_v.at(0x458) = INT32_MIN;
		_v.at(0x468) = static_cast<int32_t>(_v.address + 0x47c);
		_v.at(0x46c) = static_cast<int32_t>(_v.address + 0x49c);
		for(const uint32_t o : {0x47cu, 0x49cu, 0x4bcu, 0x4fcu, 0x4dcu})
			_v.at(o) = INT32_MAX;
		clear(_v, {0x45c, 0x460, 0x464, 0x470, 0x474, 0x478, 0x480, 0x484, 0x494, 0x498, 0x4a0, 0x4a4, 0x4b4, 0x4b8,
			0x4c0, 0x4c4, 0x4d4, 0x4d8, 0x500, 0x504, 0x508, 0x50c, 0x510, 0x514, 0x518, 0x51c, 0x4e0, 0x4e4, 0x4f4,
			0x4f8});
		for(uint32_t i = 0; i < 3; ++i)
		{
			const uint32_t o = 4 * i;
			_v.at(0x488 + o) = _v.at(0x47c + o);
			_v.at(0x4a8 + o) = _v.at(0x49c + o);
			_v.at(0x4c8 + o) = _v.at(0x4bc + o);
			_v.at(0x4e8 + o) = _v.at(0x4dc + o);
		}

		const uint32_t history = static_cast<uint32_t>(_v.at(Voice::DecimatorHistory));
		for(uint32_t i = 0; i < 12; ++i)
			_v.sram.at(history + 4 * i) = 0;
		for(uint32_t i = 0; i < 8; ++i)
		{
			_v.at(0x520 + 4 * i) = 0;
			_v.at(0x540 + 4 * i) = 0;
		}
		clear(_v, {0x560, 0x564, 0x568});

		for(uint32_t o = 0x570; o <= 0x584; o += 4)
			drawRandom(&_v.at(o), 1, _g.rng.data());

		clear(_v, {0x59c, 0x5a0, 0x594});
		_v.at(0x588) = INT32_MAX;
		clear(_v, {0x598, 0x58c, 0x590});
		for(uint32_t j = 0; j < 3; ++j)
		{
			const uint32_t o = 0x34 * j;
			_v.at(o + 0x5ac) = 1;
			_v.at(o + 0x5b0) = 1;
			clear(_v, {o + 0x5a4, o + 0x5a8, o + 0x5b4, o + 0x5b8});
			_v.at(o + 0x5bc) = INT32_MIN;
			for(uint32_t f = 0x5c0; f <= 0x5d4; f += 4)
				_v.at(o + f) = 0;
		}

		_v.at(0x6f8) = _rom.at(0x40014b7c);
		for(uint32_t o = 0x6b0; o <= 0x6f4; o += 4)
			_v.at(o) = 0;
		clear(_v, {0x700, 0x704});
		_v.at(0x6fc) = _rom.at(0x40014980);
	}

	void restartPhases(Voice& _v)
	{
		if(!_v.at(Voice::TrigNow))
			return;
		clear(_v, {0x4f4, 0x4f8, 0x494, 0x498, 0x4b4, 0x4b8});
		for(uint32_t k = 0; k < 7; ++k)
			_v.unit(k, Voice::Phase) = 0;
	}
}
