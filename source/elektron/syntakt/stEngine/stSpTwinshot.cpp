#include "stEngines.h"

#include "stEmac.h"
#include "stSection7.h"
#include "stStages.h"

// SP TWINSHOT (engine 11): two sample players, each reading one of 64 samples at the note, forward or reversed, with
// linear interpolation. The first goes through a band-pass (the difference of two one-poles) and the amp envelope;
// the second, which loops while the amp hold is set, goes to the mix as it is. A crossfade sets the two levels.

namespace syntakt::spTwinshot
{
	namespace
	{
		// Scratch buffers in SRAM, as the original lays them out for this engine
		constexpr uint32_t PlayerA = bufferB(0);	// the first sample, band-passed, under the amp envelope
		constexpr uint32_t PlayerB = bufferB(1);	// the second sample

		// The players' records and the band-pass's, in the voice
		constexpr uint32_t RecordA = 0x6b0;
		constexpr uint32_t RecordB = 0x6d4;
		constexpr uint32_t BandPass = 0x6f8;

		// A player's record, 9 words
		enum : uint32_t
		{
			Pointer = 0x00,		// the sample's first halfword
			NextPointer = 0x04,	// where a loop goes on
			Position = 0x08,	// Q12 samples
			Length = 0x0c,		// Q12 samples
			NextLength = 0x10,
			Playing = 0x14,
			Rate = 0x18,		// Q12 samples per output sample
			Reverse = 0x1c,
			Loop = 0x20
		};

		// Tables of section 7
		constexpr uint32_t AmpDecay = 0x4003ec38;	// 128 entries
		constexpr uint32_t Pole = 0x40014980;		// 128 entries: the band-pass's one-pole coefficients
		constexpr uint32_t Exp2 = 0x8000a888;		// the RAM exp2 table, 2048 entries

		// Entry _i of the sample directory at 0x4404f754, outside SRAM and section 7: the caller's samples, or as the
		// voice CPU's boot leaves it without them, every length 0 and entry i pointing at 0x4004f754 + 4 i
		using Sample = Samples::Entry;

		Sample directory(const Rom& _rom, const int32_t _i)
		{
			if(_rom.samples)
				return _rom.samples->directory[static_cast<size_t>(_i)];
			return {0, 0x4004f754u + 4u * static_cast<uint32_t>(_i)};
		}

		// The signed halfword _i of the sample at _pointer. The samples live in the voice CPU's DDR: the port reads
		// section 7 where it lies, the caller's sample data where they lie, and 0 elsewhere. _i may be negative, as a
		// reversed loop leaves the position
		int32_t sampleAt(const Rom& _rom, const uint32_t _pointer, const int32_t _i)
		{
			const uint32_t a = _pointer + 2u * static_cast<uint32_t>(_i);
			if(a >= Rom::Base && a + 1 < Rom::Base + Section7Size)
				return static_cast<int16_t>(_rom.byte(a) << 8 | _rom.byte(a + 1));
			if(!_rom.samples)
				return 0;
			const uint32_t o = a - _rom.samples->base;
			const auto& data = _rom.samples->data;
			if(o >= data.size() || o + 1 >= data.size())
				return 0;
			return static_cast<int16_t>(data[o] << 8 | data[o + 1]);
		}

		// 0x40006704: a player's rate from the note and its tune word: 2^(semitones / 12) as Q12 samples per output
		// sample, from the RAM exp2 table and a shift by the octave
		void playerRate(const int32_t _pitch, const int32_t _tune, int32_t* _record, const Sram& _sram)
		{
			int32_t p = addSat(addWrap(asl(_tune, 8), static_cast<int32_t>(0xffc00000)), _pitch);
			if(p < 0)
				p = 0;
			else if(p > 0x7f0000)
				p = 0x7f0000;
			p = frac(addWrap(p, static_cast<int32_t>(0xffc40000)), 0x0aaaaaab);	// semitones / 12: octaves
			const int32_t fraction = (p & 0xffff) >> 5;
			const int32_t octave = static_cast<int16_t>(static_cast<uint32_t>(p) >> 16);
			_record[Rate / 4] = asrReg(_sram.at(Exp2 + 4 * static_cast<uint32_t>(fraction)), 18 - octave);
		}

		// One output sample: _s0 and _s1 weighted by the position's fraction (_weight1 for _s1), Q31 from 16 bits
		int32_t interpolated(const int32_t _s0, const int32_t _s1, const int32_t _weight1)
		{
			Emac acc;
			acc.mac(_s0, subWrap(INT32_MAX, _weight1));
			acc.mac(_s1, _weight1);
			return asl(acc.storeClear(), 16);
		}
	}

	// 0x40006764: 32 samples of a player into _out, forward or reversed. At the end a looping player goes on at
	// its next pointer and length, otherwise it stops and the rest of the block is silent
	void player(int32_t* _out, int32_t* _r, const Rom& _rom)
	{
		const int32_t rate = _r[Rate / 4];
		int32_t length = _r[Length / 4];
		const int32_t loop = _r[Loop / 4];
		uint32_t pointer = static_cast<uint32_t>(_r[Pointer / 4]);
		int32_t pos = _r[Position / 4];
		const int32_t nextPointer = _r[NextPointer / 4];
		const int32_t nextLength = _r[NextLength / 4];

		if(!_r[Playing / 4] || length <= 0 || !pointer)
		{
			for(int n = 0; n < BlockSize; ++n)
				_out[n] = 0;
			_r[Position / 4] = pos;
			return;
		}

		// The last sample's index, from the length at the block's start: a loop does not change it
		const int32_t last = (length >> 12) - 1;
		int n = 0;
		bool stopped = false;
		if(!_r[Reverse / 4])
		{
			for(; n < BlockSize; ++n)
			{
				const int32_t i0 = pos >> 12;
				const int32_t i1 = i0 + 1 <= last ? i0 + 1 : 0;
				const int32_t w = asl(pos & 0xfff, 19);
				pos = addWrap(pos, rate);
				_out[n] = interpolated(sampleAt(_rom, pointer, i0), sampleAt(_rom, pointer, i1), w);
				if(length > pos)
					continue;
				if(loop && nextLength > 0 && nextPointer)
				{
					// rems.l (0x4000681a, which Capstone shows as divs.l): what is past the end, modulo the
					// next length
					pos = subWrap(pos, length) % nextLength;
					pointer = static_cast<uint32_t>(nextPointer);
					_r[Pointer / 4] = nextPointer;
					_r[Length / 4] = nextLength;
					length = nextLength;
					continue;
				}
				stopped = true;
				break;
			}
		}
		else
		{
			for(; n < BlockSize; ++n)
			{
				// As the original weighs them: the fraction goes to the later sample, which is behind
				const int32_t w = asl(pos & 0xfff, 19);
				const int32_t i1 = pos >> 12;
				const int32_t i0 = i1 - 1 >= 0 ? i1 - 1 : last;
				_out[n] = interpolated(sampleAt(_rom, pointer, i0), sampleAt(_rom, pointer, i1), w);
				pos = subWrap(pos, rate);
				if(pos >= 0)
					continue;
				if(loop)
				{
					// rems.l, as forward: the length plus the negative position, modulo the next length. The
					// original does not check the next length, which the update never leaves 0 while a player
					// plays; at 0 it would trap, and the port keeps the position
					if(nextLength)
						pos = addWrap(length, pos) % nextLength;
					pointer = static_cast<uint32_t>(nextPointer);
					_r[Pointer / 4] = nextPointer;
					_r[Length / 4] = nextLength;
					length = nextLength;
					continue;
				}
				stopped = true;
				break;
			}
		}

		if(stopped)
		{
			// The sample written last is cleared with the rest
			_r[Playing / 4] = 0;
			pos = 0;
			for(; n < BlockSize; ++n)
				_out[n] = 0;
		}
		_r[Position / 4] = pos;
	}

	// 0x400068ea: two one-poles in series, coefficients _c[0] and _c[1], states _c[2] and _c[3], each step
	// saturated; the output is the first's minus the second's, a band-pass
	void bandPass(const int32_t* _in, int32_t* _out, int32_t* _c)
	{
		const int32_t c0 = _c[0], c1 = _c[1];
		int32_t s1 = _c[2], s2 = _c[3];
		for(int n = 0; n < BlockSize; ++n)
		{
			const int32_t d1 = frac(subSat(_in[n], s1), c0);
			const int32_t l1 = addSat(d1, s1);
			const int32_t d2 = frac(subSat(l1, s2), c1);
			const int32_t l2 = addSat(d2, s2);
			_out[n] = subSat(l1, l2);
			s1 = addSat(l1, d1);
			s2 = addSat(l2, d2);
		}
		_c[2] = s1;
		_c[3] = s2;
	}

	namespace
	{
		// The sample a player's byte chooses: byte - 64, its complement and reversed when negative, 0 to 63
		int32_t sampleIndex(const int32_t _byte, bool& _reverse)
		{
			const int32_t t = _byte - 0x40;
			_reverse = static_cast<int16_t>(t) < 0;
			int32_t i = static_cast<int16_t>(_reverse ? ~t : t);
			if(i < 0)
				i = 0;
			else if(i > 63)
				i = 63;
			return i;
		}

		int32_t clampedLength(const Sample& _s)
		{
			return asl(static_cast<int32_t>(_s.length > 0x7ffff ? 0x7ffff : _s.length), 12);
		}
	}

	void update(const int32_t _pitch, Voice& _v, const uint32_t _params, const Rom& _rom)
	{
		Sram& sram = _v.sram;
		int32_t* recordA = sram.ptr(_v.address + RecordA);
		int32_t* recordB = sram.ptr(_v.address + RecordB);
		playerRate(_pitch, machineWord(sram, _params, 2), recordA, sram);
		playerRate(_pitch, machineWord(sram, _params, 10), recordB, sram);

		_v.at(Voice::AmpAttack) = INT32_MIN;
		const int32_t b16 = machineByte(sram, _params, 16);
		_v.at(Voice::AmpDecay) = b16 == 0x7f ? INT32_MIN : _rom.entry(AmpDecay, b16);

		// The crossfade from word 8: x and 1 - x, halved, then 4 h (1 - h) of each half h
		const int32_t f = frac(0x40810204, asl(machineWord(sram, _params, 8), 16));
		const int32_t x = addSat(f, f);
		const int32_t a = x >> 1;
		const int32_t b = subWrap(INT32_MAX, x) >> 1;
		const int32_t levelB = asl(frac(a, subWrap(INT32_MAX, a)), 2);
		const int32_t levelA = asl(frac(b, subWrap(INT32_MAX, b)), 2);

		// The band-pass's poles from word 6: at 0x4000 both open as far as they go, below it the low-pass closes,
		// above it the high-pass opens
		const int32_t w6 = machineWord(sram, _params, 6);
		if(w6 == 0x4000)
		{
			_v.at(BandPass) = _rom.entry(Pole, 127);
			_v.at(BandPass + 4) = _rom.entry(Pole, 0);
		}
		else if(w6 > 0x4000)
		{
			int32_t i = ((w6 - 0x4000) >> 7) + 1;
			if(i > 127)
				i = 127;
			_v.at(BandPass) = _rom.entry(Pole, 127);
			_v.at(BandPass + 4) = _rom.entry(Pole, i < 0 ? 0 : i);
		}
		else
		{
			int32_t i = w6 >> 7;
			if(i < 0)
				i = 0;
			_v.at(BandPass) = _rom.entry(Pole, i);
			_v.at(BandPass + 4) = _rom.entry(Pole, 0);
		}

		// The amp hold makes the second player loop, and the amp envelope never holds
		_v.at(RecordB + Loop) = _v.at(Voice::AmpHold);
		_v.at(Voice::AmpHold) = 0;
		_v.at(0x08) = frac(levelA, 0x40000000);
		_v.at(0x0c) = frac(levelB, 0x40000000);

		bool reverseA, reverseB;
		const int32_t iA = sampleIndex(machineByte(sram, _params, 4), reverseA);
		const int32_t iB = sampleIndex(machineByte(sram, _params, 12), reverseB);
		const Sample sA = directory(_rom, iA);
		const Sample sB = directory(_rom, iB);
		const int32_t lengthA = clampedLength(sA);
		const int32_t lengthB = clampedLength(sB);
		if(lengthA)
		{
			recordA[NextPointer / 4] = static_cast<int32_t>(sA.pointer);
			recordA[NextLength / 4] = lengthA;
		}
		if(lengthB)
		{
			recordB[NextPointer / 4] = static_cast<int32_t>(sB.pointer);
			recordB[NextLength / 4] = lengthB;
		}
		recordA[Reverse / 4] = reverseA;
		recordB[Reverse / 4] = reverseB;

		if(_v.at(Voice::TrigPending))
		{
			_v.at(0x08) = 0;
			_v.at(0x0c) = 0;
		}

		if(!_v.at(Voice::TrigNow))
			return;

		_v.at(0x18) = _v.at(0x08);
		_v.at(0x1c) = _v.at(0x0c);
		_v.at(BandPass + 8) = 0;
		_v.at(BandPass + 12) = 0;
		const auto start = [](int32_t* _r, const Sample& _s, const int32_t _length, const bool _reverse)
		{
			_r[Pointer / 4] = static_cast<int32_t>(_s.pointer);
			_r[NextPointer / 4] = static_cast<int32_t>(_s.pointer);
			_r[Length / 4] = _length;
			_r[NextLength / 4] = _length;
			_r[Position / 4] = 0;
			_r[Playing / 4] = 0;
			if(!_length)
				return;
			_r[Playing / 4] = 1;
			if(_reverse)
				_r[Position / 4] = asl(static_cast<int32_t>(_s.length - 1), 12);	// from the unclamped length
		};
		start(recordA, sA, lengthA, reverseA);
		start(recordB, sB, lengthB, reverseB);
	}

	void render(int32_t* _out, Voice& _v, const Rom& _rom)
	{
		Sram& sram = _v.sram;
		player(sram.ptr(PlayerA), sram.ptr(_v.address + RecordA), _rom);
		player(sram.ptr(PlayerB), sram.ptr(_v.address + RecordB), _rom);
		bandPass(sram.ptr(PlayerA), sram.ptr(PlayerA), sram.ptr(_v.address + BandPass));
		ampEnvelope(_v);
		vca(sram.ptr(PlayerA), _v);
		mix(sram.ptr(PlayerA), sram.ptr(PlayerB), _out, 0, 1, _v);
	}
}
