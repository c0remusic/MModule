#include "stMainFx.h"

#include <cstring>

#include "stEmac.h"
#include "stMainBlock.h"

namespace syntakt::main
{
	namespace
	{
		constexpr uint32_t Poles = 0x4029bffc;			// 512 one-pole coefficients, Q31
		constexpr uint32_t PolesEnd = 0x4029cff8;		// the last one
		constexpr uint32_t DelayTaps = 0x8000d6d0;		// tap A (the new time), 34 frames; tap B 0x110 after it
		constexpr uint32_t DelayTapB = 0x8000d7e0;
		constexpr uint32_t DelayTapBDesc = 0x8000daa0;	// tap B's descriptor, then the reverb's reads
		constexpr uint32_t DelayTime = 0x8000dac0;		// the smoothed time, frames << 12
		constexpr uint32_t DelayLast = 0x8000dac4;		// the last block's time, frames
		constexpr uint32_t DelayWrite = 0x8000dac8;		// the ring's write position, frames
		constexpr uint32_t DiffuserWrite = 0x8000d690;	// 8 longs: write positions, bytes
		constexpr uint32_t DiffuserLength = 0x8000d670;	// 8 longs: lengths, bytes (the SRAM image's)
		constexpr uint32_t DiffuserRead = 0x8000d610;	// 8 longs: the read addresses
		constexpr uint32_t DiffuserReadDesc = 0x8000c8c0;
		constexpr uint32_t DiffuserBuffers = 0x8000d120;	// 0xa0 apart
		constexpr uint32_t LineWrite = 0x8000c660;		// 9 longs: write positions, bytes
		constexpr uint32_t LineLength = 0x8000c63c;		// 9 longs: lengths, samples (0: the pre-delay)
		constexpr uint32_t LineRead = 0x8000c618;		// 9 longs: the read addresses
		constexpr uint32_t LineReadDesc = 0x8000c9c0;
		constexpr uint32_t LineBuffers = 0x8000caf0;	// 0xb0 apart
		constexpr uint32_t Modulated = 0x8000c51c;		// the modulated line's step, interpolation index, fraction
		constexpr uint32_t ModulatedAt = 0x8000c528;	// its two positions (lines 4 and 8)
		constexpr uint32_t Sine = 0x8000c510;			// the oscillator's rate, then its two outputs
		constexpr uint32_t SineOut = 0x4550e1c8;
		constexpr uint32_t ModBase = 0x8000c610;		// lines 4 and 8's own lengths

		uint32_t step(const uint32_t _a, const int32_t _off, const uint32_t _mod)
		{
			if(!_mod)
				return _a + static_cast<uint32_t>(_off);
			const uint32_t m = (uint32_t(1) << _mod) - 1;
			return (_a & ~m) | ((_a + static_cast<uint32_t>(_off)) & m);
		}

		uint32_t transferSize(const uint32_t _code)
		{
			switch(_code)
			{
			case 0: return 1;
			case 1: return 2;
			case 2: return 4;
			case 4: return 16;
			default: return 32;
			}
		}

		// memcpy of one transfer unit, by its size: a constant size each, which the compiler moves inline rather
		// than calling memmove for a size it does not know
		ST_FORCE_INLINE void copyUnit(uint8_t* _to, const uint8_t* _from, const uint32_t _size)
		{
			switch(_size)
			{
			case 1: std::memcpy(_to, _from, 1); break;
			case 2: std::memcpy(_to, _from, 2); break;
			case 4: std::memcpy(_to, _from, 4); break;
			case 16: std::memcpy(_to, _from, 16); break;
			default: std::memcpy(_to, _from, 32); break;
			}
		}

		void zero(Memory& _mem, const uint32_t _address, const uint32_t _bytes)
		{
			for(uint32_t i = 0; i < _bytes; ++i)
				_mem.setByte(_address + i, 0);
		}

		// The read position of a line: its write position less its length, modulo the line, then the descriptor's
		// source (16-byte aligned) and destination (moved back as much, so that the sample lands at the buffer)
		void aimRead(Memory& _mem, const uint32_t _line, const uint32_t _offset, const uint32_t _readAt,
			const uint32_t _desc, const uint32_t _buffer)
		{
			const uint32_t a = _line + _offset;
			_mem.setWord(_readAt, static_cast<int32_t>(a));
			_mem.setWord(_desc, static_cast<int32_t>(a & 0xfffffff0));
			_mem.setWord(_desc + 0x10, static_cast<int32_t>(_buffer - (a & 0xf)));
		}

	}

	void modulatedPosition(Memory& _mem, Emac4& _acc, const uint32_t _base, const int32_t _sine, const uint32_t _at)
	{
		_acc.mac(0, 0x200000, _sine);
		const int32_t sum = addWrap(_acc.storeClear(0), asl(_mem.word(_base), 18));
		const int32_t whole = sum >> 18;
		const int32_t last = _mem.word(_at);
		_mem.setWord(_at, whole);
		_mem.setWord(Modulated, subWrap(whole, last));
		_mem.setWord(Modulated + 4, (sum >> 13) & 0x1f);
		_mem.setWord(Modulated + 8, asl(sum, 18) & 0x7fffffff);
	}

	Tcd readTcd(const Memory& _mem, const uint32_t _address)
	{
		Tcd t;
		t.saddr = static_cast<uint32_t>(_mem.word(_address));
		t.attr = static_cast<uint16_t>(_mem.half(_address + 4));
		t.soff = _mem.half(_address + 6);
		t.nbytes = static_cast<uint32_t>(_mem.word(_address + 8));
		t.slast = _mem.word(_address + 0xc);
		t.daddr = static_cast<uint32_t>(_mem.word(_address + 0x10));
		t.citer = static_cast<uint16_t>(_mem.half(_address + 0x14));
		t.doff = _mem.half(_address + 0x16);
		t.dlastSga = static_cast<uint32_t>(_mem.word(_address + 0x18));
		t.biter = static_cast<uint16_t>(_mem.half(_address + 0x1c));
		t.csr = static_cast<uint16_t>(_mem.half(_address + 0x1e));
		return t;
	}

	Tcd runChannel(Memory& _mem, Tcd _tcd)
	{
		// The page each side's last unit was found in: a unit inside the same page is found from it without the
		// table, at the same bytes, since a page the table holds keeps its bytes (only a page it lacked can be
		// mapped while the channel runs, a line's first write, and that one is not kept here)
		struct LastPage
		{
			uint32_t page = 0;
			uint8_t* base = nullptr;

			uint8_t* find(const uint32_t _address, const uint32_t _size) const
			{
				const uint32_t offset = _address & (Memory::PageSize - 1);
				return base && _address >> 12 == page && offset <= Memory::PageSize - _size ? base + offset : nullptr;
			}

			void keep(const uint32_t _address, const uint32_t _size, uint8_t* _at)
			{
				const uint32_t offset = _address & (Memory::PageSize - 1);
				if(!_at || offset > Memory::PageSize - _size)
					return;
				page = _address >> 12;
				base = _at - offset;
			}
		};
		LastPage reads, writes;

		uint8_t data[256];
		for(;;)
		{
			const uint32_t smod = _tcd.attr >> 11, dmod = (_tcd.attr >> 3) & 31;
			const uint32_t sb = transferSize((_tcd.attr >> 8) & 7), db = transferSize(_tcd.attr & 7);
			const uint32_t count = (_tcd.citer & 0x8000) ? _tcd.citer & 0x1ff : _tcd.citer & 0x7fff;
			const uint32_t n = _tcd.nbytes < sizeof data ? _tcd.nbytes : sizeof data;
			uint32_t s = _tcd.saddr, d = _tcd.daddr;
			for(uint32_t it = 0; it < count; ++it)
			{
				// A transfer unit a page holds whole moves at once, else a byte at a time, as the same bytes
				for(uint32_t k = 0; k + sb <= n; k += sb)
				{
					const uint8_t* from = reads.find(s, sb);
					if(!from)
					{
						from = _mem.bytesAt(s, sb);
						reads.keep(s, sb, const_cast<uint8_t*>(from));
					}
					if(from)
						copyUnit(data + k, from, sb);
					else
						for(uint32_t b = 0; b < sb; ++b)
							data[k + b] = _mem.byte(s + b);
					s = step(s, _tcd.soff, smod);
				}
				for(uint32_t k = 0; k + db <= n; k += db)
				{
					uint8_t* to = writes.find(d, db);
					if(!to)
					{
						to = _mem.writableBytesAt(d, db);
						writes.keep(d, db, to);
					}
					if(to)
						copyUnit(to, data + k, db);
					else
						for(uint32_t b = 0; b < db; ++b)
							_mem.setByte(d + b, data[k + b]);
					d = step(d, _tcd.doff, dmod);
				}
			}
			if(!(_tcd.csr & 0x10))
				break;
			const Tcd next = readTcd(_mem, _tcd.dlastSga);
			_tcd = next;
			if(!(_tcd.csr & 1))
				break;
		}
		_tcd.csr = static_cast<uint16_t>((_tcd.csr & ~1) | 0x80);
		return _tcd;
	}

	void loadSramImage(Memory& _mem)
	{
		// 0x40339000-0x40340330 to 0x80000000, 0x40340330-0x40347b90 to 0x80008000; the rest is cleared
		auto copy = [&](const uint32_t _from, const uint32_t _to, const uint32_t _bytes)
		{
			for(uint32_t i = 0; i < _bytes; ++i)
				_mem.setByte(_to + i, _mem.romHolds(_from + i) ? _mem.byte(_from + i) : 0);
		};
		zero(_mem, Memory::SramBase, Memory::SramSize);
		copy(0x40339000, 0x80000000, 0x7330);
		copy(0x40340330, 0x80008000, 0x7860);
	}

	void delayInit(Memory& _mem)
	{
		zero(_mem, 0x8000da1c, 0x10);
		zero(_mem, 0x8000d8f0, 0x18);
		_mem.setWord(0x8000d918, 0);
		_mem.setWord(0x8000d908, 0);
		_mem.setWord(0x8000d910, _mem.word(Poles));
		_mem.setWord(0x8000d914, _mem.word(PolesEnd));
		_mem.setWord(0x8000d90c, _mem.word(Poles - 4));
		zero(_mem, 0x8000d91c, 0x100);
		zero(_mem, DelayRing, 0x400000);
		_mem.setWord(0x8000da8c, 0);
		_mem.setWord(0x8000daac, 0);
		_mem.setWord(0x8000da4c, 0);
		// The descriptors' fixed fields: channel 30's own (0x8000da80, a copy) and tap B's, channel 42's copy
		const uint32_t fields[][2] = {
			{0x8000da84, 0xb4020010}, {0x8000da94, 0xbc110004}, {0x8000da9c, 0xbc110011},
			{0x8000dab4, 0xbc110004}, {0x8000dabc, 0xbc110011}, {0x8000da54, 0xd4100010},
			{0x8000da5c, 0xd4100011}, {0x8000da88, 0x10}, {0x8000da98, DelayTapBDesc}, {0x8000daa4, 0xb4020010},
			{0x8000daa8, 0x10}, {0x8000dab8, DiffuserReadDesc}, {0x8000da44, 0x04b40010}, {0x8000da48, 0x10},
			{0x8000da58, 0x8000c6a0},
		};
		for(const auto& f : fields)
			_mem.setWord(f[0], static_cast<int32_t>(f[1]));
	}

	void reverbInit(Memory& _mem)
	{
		zero(_mem, 0x8000c608, 8);
		zero(_mem, 0x8000c5bc, 0x40);
		_mem.setWord(0x8000c530, _mem.word(Poles));
		_mem.setWord(0x8000c534, _mem.word(PolesEnd));
		zero(_mem, 0x8000c310, 0x200);
		const int32_t line8 = _mem.word(LineLength + 32), line4 = _mem.word(LineLength + 16);
		_mem.setWord(ModBase + 4, line8);
		_mem.setWord(ModBase, line4);
		_mem.setWord(ModulatedAt, line4);
		_mem.setWord(ModulatedAt + 4, subWrap(line8, 8));
		zero(_mem, ReverbDiffusers, 0x8000);
		zero(_mem, ReverbLines, 0x90000);
		auto descriptor = [&](const uint32_t _at, const uint32_t _attr, const uint32_t _iter, const uint32_t _next,
			const uint32_t _csr)
		{
			_mem.setWord(_at + 4, static_cast<int32_t>(_attr));
			_mem.setWord(_at + 8, 0x10);
			_mem.setWord(_at + 0xc, 0);
			_mem.setWord(_at + 0x14, static_cast<int32_t>(_iter));
			_mem.setWord(_at + 0x18, static_cast<int32_t>(_next));
			_mem.setWord(_at + 0x1c, static_cast<int32_t>(_csr));
		};
		// Channel 30's reads after tap B: the 8 diffusers (9 x 16 bytes), then the 9 lines (lines 4 and 8: 10 x 16)
		for(uint32_t k = 0; k < 8; ++k)
			descriptor(DiffuserReadDesc + 0x20 * k, 0x64020010, 0xbc090004, DiffuserReadDesc + 0x20 * (k + 1),
				0xbc090011);
		for(uint32_t k = 0; k < 9; ++k)
		{
			const bool longer = k == 4 || k == 8;
			descriptor(LineReadDesc + 0x20 * k, 0x84020010, longer ? 0xbc0a0004 : 0xbc090004,
				k == 8 ? 0 : LineReadDesc + 0x20 * (k + 1), k == 8 ? 0xbc0a0001 : longer ? 0xbc0a0011 : 0xbc090011);
		}
		// Channel 42's writes after the delay's: the 8 diffusers', then the 9 lines', 8 x 16 bytes each
		for(uint32_t k = 0; k < 8; ++k)
		{
			_mem.setWord(0x8000c6a0 + 0x20 * k, static_cast<int32_t>(DiffuserBuffers + 0xa0 * k));
			descriptor(0x8000c6a0 + 0x20 * k, 0x04640010, 0xd4080010, 0x8000c6a0 + 0x20 * (k + 1), 0xd4080011);
		}
		for(uint32_t k = 0; k < 9; ++k)
		{
			_mem.setWord(0x8000c7a0 + 0x20 * k, static_cast<int32_t>(LineBuffers + 0xb0 * k));
			descriptor(0x8000c7a0 + 0x20 * k, 0x04840010, 0xd4080010, k == 8 ? 0 : 0x8000c7a0 + 0x20 * (k + 1),
				k == 8 ? 0xd4080001 : 0xd4080011);
		}
	}

	void prepareEffects(Memory& _mem, Emac4& _acc, const uint32_t _p)
	{
		_acc.setMacsr(0xa0);
		for(uint32_t k = 0; k < 8; ++k)
		{
			const uint32_t off = static_cast<uint32_t>(
				subWrap(_mem.word(DiffuserWrite + 4 * k), _mem.word(DiffuserLength + 4 * k))) & 0xfff;
			aimRead(_mem, ReverbDiffusers + 0x1000 * k, off, DiffuserRead + 4 * k, DiffuserReadDesc + 0x20 * k,
				DiffuserBuffers + 0xa0 * k);
		}
		_mem.setWord(LineLength + 16, _mem.word(ModulatedAt));
		_mem.setWord(LineLength + 32, _mem.word(ModulatedAt + 4));

		// The sine: a rotation by the rate at 0x8000c510
		const int32_t rate = _mem.word(Sine);
		int32_t s = _mem.word(Sine + 4), c = _mem.word(Sine + 8);
		_acc.mac(0, INT32_MAX, s);
		_acc.mac(1, INT32_MAX, c);
		_acc.msac(0, rate, c);
		s = _acc.storeClear(0);
		_acc.mac(1, rate, s);
		c = _acc.storeClear(1);
		_mem.setWord(SineOut, c);
		modulatedPosition(_mem, _acc, ModBase, s, ModulatedAt);
		_mem.setWord(Sine + 4, s);
		_mem.setWord(Sine + 8, c);

		for(uint32_t k = 0; k < 9; ++k)
		{
			const int32_t length = static_cast<int32_t>(static_cast<uint32_t>(_mem.word(LineLength + 4 * k)) * 4);
			const uint32_t off = static_cast<uint32_t>(subWrap(_mem.word(LineWrite + 4 * k), length)) & 0xffff;
			aimRead(_mem, ReverbLines + 0x10000 * k, off, LineRead + 4 * k, LineReadDesc + 0x20 * k,
				LineBuffers + 0xb0 * k);
		}

		// The delay's time: (TIME + 256) x 48000 >> 10 x 900 / tempo frames, at most 0x5dc00, smoothed by 255/256
		int32_t time = static_cast<int32_t>(static_cast<uint32_t>(_mem.half(_p + 0x6e6) + 0x100) * 48000u);
		time = static_cast<int32_t>(static_cast<uint32_t>(time >> 10) * 900u);
		time /= _mem.word(Tempo);	// divs.l: the tempo is BPM x 120, never 0
		uint32_t frames = static_cast<uint32_t>(time) > 0x5dc00 ? 0x5dc00 : static_cast<uint32_t>(time);
		const uint32_t half = static_cast<uint32_t>(_mem.word(DelayTime) >> 1);
		const uint32_t smoothed = (half << 8) - half + ((frames >> 1) << 5);
		_mem.setWord(DelayTime, static_cast<int32_t>(smoothed >> 7));
		frames = smoothed >> 12;
		const uint32_t write = static_cast<uint32_t>(_mem.word(DelayWrite));
		const uint32_t a = (((write - frames) & 0x7ffff) << 3) + DelayRing;
		const uint32_t b = (((write - static_cast<uint32_t>(_mem.word(DelayLast))) & 0x7ffff) << 3) + DelayRing;
		_mem.setWord(DelayLast, static_cast<int32_t>(frames));
		_mem.setWord(DelayTapBDesc, static_cast<int32_t>(b & 0xfffffff0));
		_mem.setWord(DelayTapBDesc + 0x10, static_cast<int32_t>(DelayTapB - (b & 0xf)));

		// Channel 30: tap A, then by scatter-gather tap B and the reverb's reads
		Tcd t;
		t.saddr = a & 0xfffffff0;
		t.attr = 0xb402;
		t.soff = 0x10;
		t.nbytes = 0x10;
		t.daddr = DelayTaps - (a & 0xf);
		t.citer = t.biter = 0xbc11;
		t.doff = 4;
		t.dlastSga = DelayTapBDesc;
		t.csr = 0x11;
		runChannel(_mem, t);
		_acc.setMacsr(0x20);
	}
}
