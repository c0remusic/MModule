#include "stMainBus.h"

#include "stEmac.h"
#include "stSimd.h"

namespace syntakt::main
{
	namespace
	{
		constexpr uint32_t AnalogLevels = 0x401e08e0;	// a long per machine id

		// sumChannels' frames from _first on, from a clear accumulator in fractional mode (each frame's movclr
		// leaves it clear for the next), on the buffers' and the bus side's bytes: a frame's Channels products sum
		// in 64 bits at once (Emac4::sumRun, at most MaxRunTerms). Read and written in the steps' order, the bytes
		// are what Memory::word() and setWord() give and take
		template<int Channels>
		void sumDirect(Emac4& _acc, int32_t* _gains, const int32_t* _steps, const uint8_t* const* _src, uint8_t* _dst,
			const uint32_t _first)
		{
			static_assert(Channels <= MaxRunTerms, "Emac4::sumRun's bound");
			int32_t gains[Channels];
			for(int k = 0; k < Channels; ++k)
				gains[k] = _gains[k];
			for(uint32_t i = _first; i < BlockFrames; ++i)
			{
				RunSum run;
				for(int k = 0; k < Channels; ++k)
					run.add(runTerm(Msac{gains[k], Memory::wordAt(_src[k] + 4 * i)}));
				_acc.sumRun(0, run);
				for(int k = 0; k < Channels; ++k)
					gains[k] = addWrap(gains[k], _steps[k]);
				Memory::setWordAt(_dst + 8 * i, _acc.storeClear(0));
			}
			for(int k = 0; k < Channels; ++k)
				_gains[k] = gains[k];
		}
	}

	void sumChannels(Memory& _mem, Emac4& _acc, const int _channels, const uint32_t _dst, const uint32_t _src,
		const uint32_t _gains, const int32_t* _steps)
	{
		// The EMAC in a local copy, which the compiler keeps in registers, and back at the end: the same state
		Emac4 acc = _acc;
		int32_t gains[10];
		for(int k = 0; k < _channels; ++k)
			gains[k] = _mem.word(_gains + 4 * static_cast<uint32_t>(k));

		// The buffers' and the bus side's bytes, when the memory's table holds each whole
		const uint8_t* src[10];
		bool direct = _channels >= 8 && _channels <= 10;
		for(int k = 0; k < _channels; ++k)
			direct &= (src[k] = _mem.bytesAt(_src + 128 * static_cast<uint32_t>(k), 4 * BlockFrames)) != nullptr;
		uint8_t* dst = _mem.writableBytesAt(_dst, 8 * (BlockFrames - 1) + 4);
		direct &= dst != nullptr;

		// The steps, until the accumulator is clear in fractional mode: the first frame's at most, unless the mode
		// is integer, where every frame takes them
		uint32_t i = 0;
		for(; i < BlockFrames && !(direct && acc.fractional() && acc.clear(0)); ++i)
		{
			for(int k = 0; k < _channels; ++k)
				acc.msac(0, gains[k], _mem.word(_src + 128 * static_cast<uint32_t>(k) + 4 * i));
			for(int k = 0; k < _channels; ++k)
				gains[k] = addWrap(gains[k], _steps[k]);
			_mem.setWord(_dst + 8 * i, acc.storeClear(0));
		}
		if(i < BlockFrames && simd::avx2())
		{
			simd::RampMix m{};
			for(int k = 0; k < _channels; ++k)
			{
				m.src[k] = src[k];
				m.gains[k] = gains[k];
				m.steps[k] = _steps[k];
				gains[k] = simd::ramped(gains[k], _steps[k], BlockFrames - i);
			}
			m.channels = _channels;
			m.srcStride = 4;
			m.dst = dst;
			m.dstStride = 8;
			m.first = i;
			m.negate = true;
			m.saturate = (acc.macsr() & Emac4::Omc) != 0;
			m.bigEndian = true;
			simd::rampMixAvx2(m);
		}
		else if(i < BlockFrames)
		{
			if(_channels == 8)
				sumDirect<8>(acc, gains, _steps, src, dst, i);
			else if(_channels == 9)
				sumDirect<9>(acc, gains, _steps, src, dst, i);
			else
				sumDirect<10>(acc, gains, _steps, src, dst, i);
		}
		for(int k = 0; k < _channels; ++k)
			_mem.setWord(_gains + 4 * static_cast<uint32_t>(k), gains[k]);
		_acc = acc;
	}

	void copySide(Memory& _mem, const uint32_t _dst, const uint32_t _src)
	{
		for(uint32_t i = 0; i < BlockFrames; ++i)
			_mem.setWord(_dst + 4 * i, _mem.word(_src + 8 * i));
	}

	void mixBuses(Memory& _mem, Emac4& _acc, const uint32_t _dst, const uint32_t _src, int32_t* _gains,
		const int32_t* _steps)
	{
		// From two clear accumulators in fractional mode, the frames left in AVX2 a side at a time, on the buses'
		// bytes, where the table holds them whole and _dst is _src or apart from both: a side's frames are read
		// before they are written, the other side's are neither
		const uint8_t* src = _mem.bytesAt(_src, 8 * BlockFrames);
		const uint8_t* other = _mem.bytesAt(_src + 256, 8 * BlockFrames);
		uint8_t* dst = _mem.writableBytesAt(_dst, 8 * BlockFrames);
		const bool apart = _dst == _src || _dst + 8 * BlockFrames <= _src || _src + 16 * BlockFrames <= _dst;
		const bool vector = simd::avx2() && src && other && dst && apart;
		for(uint32_t i = 0; i < BlockFrames; ++i)
		{
			if(vector && _acc.fractional() && _acc.clear(0) && _acc.clear(1))
			{
				simd::RampMix m{};
				m.gains[0] = _gains[0];
				m.gains[1] = _gains[1];
				m.steps[0] = _steps[0];
				m.steps[1] = _steps[1];
				m.channels = 2;
				m.srcStride = 8;
				m.dstStride = 8;
				m.first = i;
				m.negate = true;
				m.saturate = (_acc.macsr() & Emac4::Omc) != 0;
				m.bigEndian = true;
				for(uint32_t side = 0; side < 8; side += 4)
				{
					m.src[0] = src + side;
					m.src[1] = other + side;
					m.dst = dst + side;
					simd::rampMixAvx2(m);
				}
				_gains[0] = simd::ramped(_gains[0], _steps[0], BlockFrames - i);
				_gains[1] = simd::ramped(_gains[1], _steps[1], BlockFrames - i);
				return;
			}

			// Both sides are read before they are written: _dst may be _src
			const int32_t l = _mem.word(_src + 8 * i), r = _mem.word(_src + 8 * i + 4);
			_acc.msac(0, _gains[0], l);
			_acc.msac(0, _gains[1], _mem.word(_src + 256 + 8 * i));
			_acc.msac(1, _gains[0], r);
			_acc.msac(1, _gains[1], _mem.word(_src + 256 + 8 * i + 4));
			_gains[0] = addWrap(_gains[0], _steps[0]);
			_gains[1] = addWrap(_gains[1], _steps[1]);
			_mem.setWord(_dst + 8 * i, _acc.storeClear(0));
			_mem.setWord(_dst + 8 * i + 4, _acc.storeClear(1));
		}
	}

	void scaleBus(Memory& _mem, Emac4& _acc, const uint32_t _bus, const int32_t _gain)
	{
		Emac4 acc = _acc;
		const uint32_t macsr = acc.macsr();
		acc.setMacsr(Emac4::Omc);

		// A group whose four accumulators are clear (all but perhaps the first) takes each one's integer product
		// at once (Emac4::sumRun): with |_gain| <= 128 a product is at most 2^38 in magnitude, under its own check's
		// 2^39. The bus's bytes, where the table holds them whole, are what word() and setWord() give and take
		uint8_t* bus = _mem.writableBytesAt(_bus, 8 * BlockFrames);
		const bool direct = bus && _gain >= -128 && _gain <= 128;
		for(uint32_t i = 0; i < 2 * BlockFrames; i += 4)
		{
			if(direct && acc.allClear())
			{
				if(simd::avx2())
				{
					simd::scaleAvx2(bus, i, 2 * BlockFrames, _gain);
					break;
				}
				for(int k = 0; k < 4; ++k)
					acc.sumRun(k, int64_t(_gain) * Memory::wordAt(bus + 4 * (i + static_cast<uint32_t>(k))));
				for(int k = 0; k < 4; ++k)
					Memory::setWordAt(bus + 4 * (i + static_cast<uint32_t>(k)), acc.storeClear(k));
				continue;
			}
			for(int k = 0; k < 4; ++k)
				acc.mac(k, _gain, _mem.word(_bus + 4 * (i + static_cast<uint32_t>(k))));
			for(int k = 0; k < 4; ++k)
				_mem.setWord(_bus + 4 * (i + static_cast<uint32_t>(k)), acc.storeClear(k));
		}
		acc.setMacsr(macsr);
		_acc = acc;
	}

	void rampBuffer(Memory& _mem, Emac4& _acc, const uint32_t _buffer, const int32_t _from, const int32_t _to)
	{
		Emac4 acc = _acc;
		const int32_t step = subWrap(_to, _from) >> 5;
		int32_t gain = _from;

		// A group whose four accumulators are clear in fractional mode (all but perhaps the first) takes each one's
		// product at once (Emac4::sumRun). The buffer's bytes, where the table holds them whole, are what word() and
		// setWord() give and take
		uint8_t* buffer = _mem.writableBytesAt(_buffer, 4 * BlockFrames);
		for(uint32_t i = 0; i < BlockFrames; i += 4)
		{
			if(buffer && acc.fractional() && acc.allClear())
			{
				if(simd::avx2())
				{
					simd::RampMix m{};
					m.src[0] = buffer;
					m.gains[0] = gain;
					m.steps[0] = step;
					m.channels = 1;
					m.srcStride = 4;
					m.dst = buffer;
					m.dstStride = 4;
					m.first = i;
					m.negate = true;
					m.saturate = (acc.macsr() & Emac4::Omc) != 0;
					m.bigEndian = true;
					simd::rampMixAvx2(m);
					break;
				}
				for(int k = 0; k < 4; ++k)
				{
					const int32_t v = Memory::wordAt(buffer + 4 * (i + static_cast<uint32_t>(k)));
					acc.sumRun(k, runTerm(Msac{v, gain}));
					gain = addWrap(gain, step);
				}
				for(int k = 0; k < 4; ++k)
					Memory::setWordAt(buffer + 4 * (i + static_cast<uint32_t>(k)), acc.storeClear(k));
				continue;
			}
			for(int k = 0; k < 4; ++k)
			{
				acc.msac(k, _mem.word(_buffer + 4 * (i + static_cast<uint32_t>(k))), gain);
				gain = addWrap(gain, step);
			}
			for(int k = 0; k < 4; ++k)
				_mem.setWord(_buffer + 4 * (i + static_cast<uint32_t>(k)), acc.storeClear(k));
		}
		_acc = acc;
	}

	int32_t analogLevel(const Memory& _mem, Emac4& _acc, const int32_t _machine, const int32_t _word)
	{
		const int32_t level = _mem.word(AnalogLevels + 4 * static_cast<uint32_t>(_machine));
		_acc.msac(0, level, static_cast<int16_t>(_word));
		return _acc.storeClear(0);
	}
}
