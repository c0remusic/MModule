// The two utility machines, noise and impulse, which play on any of the four voices (edDrumsAnalog.h): a block of
// each into the voice's memory, as the main CPU computes it
#include <algorithm>
#include <array>
#include <cmath>

#include "edDrumsAnalogPort.h"

namespace elektron::drums::analog::port
{
	namespace
	{
		int16_t word(const VoiceIo& _io, const int _k)
		{
			return static_cast<int16_t>(be16(_io.params.data() + 2 * _k));
		}
		// A word's top byte, the value of its knob, as the CPU reads it: signed
		int knobValue(const VoiceIo& _io, const int _k)
		{
			return static_cast<int8_t>(_io.params[static_cast<size_t>(2 * _k)]);
		}

		// add.l x,x then sats.l: twice _v, saturated where the add overflowed
		int32_t twiceSat(const int32_t _v)
		{
			const int32_t r = add(_v, _v);
			return sats(r, ((_v ^ r) & 0x80000000) != 0);
		}

		// The envelope's state machine: its step in the first long, the countdown endBlock runs in the second
		int32_t stateLong(const VoiceIo& _io, const int _k)
		{
			return static_cast<int32_t>(be32(_io.state.data() + 4 * _k));
		}
		void setStateLong(VoiceIo& _io, const int _k, const int32_t _v)
		{
			setBe32(_io.state.data() + 4 * _k, static_cast<uint32_t>(_v));
		}

		// An envelope command byte: its top nibble the command, its bottom nibble kept
		void setCommand(uint8_t* _p, const uint8_t _command) { *_p = static_cast<uint8_t>((*_p & 0x0f) | _command); }

		// What a machine does to the voice it starts on: its control words and the envelope generator's levels to rest,
		// so that nothing the machine before left plays on
		void voiceOff(VoiceIo& _io)
		{
			uint8_t* f = _io.frame.data();
			if(_io.voice == MetalVoice)
			{
				setBe16(f + metalFrame::Cv + 8, 0);
				setBe16(f + metalFrame::Word6, 0);
				setBe16(f + metalFrame::Word10, 0);
				setBe16(f + metalFrame::Cv, 0);
				// the envelope generator's twelve longs to -1/2 of full scale, their rest
				for(int i = 0; i < 12; ++i)
					setBe32(f + metalFrame::Oscillators + 4 * i, 0xc0000000u);
				return;
			}
			for(const int at : {0, 2, 4, 6})
				setBe16(f + frame::Cv + at, 0);
			// each of the envelope's two halves: its level's low 20 bits cleared, the byte before cleared, command 0xb
			uint8_t* env = f + frame::Envelope;
			for(const int at : {4, 12})
			{
				setBe32(env + at, be32(env + at) & 0xfff00000u);
				env[at - 1] = 0;
				setCommand(env + at, 0xb0);
			}
		}

		// Decay coefficients: per tick of rate _fs, the factor a level falls by to fall 60 dB in _seconds *
		// (_base^(k/127) - 1) / (_base - 1), k a knob value 0-127; with the time in whole microseconds where _micro. 0
		// for k = 0, truncated
		struct DecayTable
		{
			std::array<int32_t, 128> t{};
			DecayTable(const double _seconds, const double _base, const double _fs, const bool _micro)
			{
				++tablesBuilt();
				for(int k = 1; k < 128; ++k)
				{
					double time = _seconds * (std::pow(_base, k / 127.0) - 1) / (_base - 1);
					if(_micro)
						time = std::round(time * 1e6) / 1e6;
					const double c = std::exp(-std::log(1000.0) / (time * _fs));
					t[static_cast<size_t>(k)] = static_cast<int32_t>(std::min(2147483647.0, std::ldexp(c, 31)));
				}
			}
		};

		constexpr double BlockRate = 48000.0 / 32;

		// The CPU's decays per block, 25, 20, 15, 10, 5 and 1 s at the top, one table after the other (those these
		// machines read). A negative knob value reads the table before, as the CPU does
		const DecayTable& blockDecay(const int _seconds)
		{
			static const DecayTable t25(25, 33, BlockRate, false), t15(15, 33, BlockRate, false),
				t10(10, 33, BlockRate, false), t1(1, 33, BlockRate, false);
			switch(_seconds)
			{
			case 25: return t25;
			case 15: return t15;
			case 10: return t10;
			default: return t1;
			}
		}
		int32_t blockDecayAt(const int _seconds, const int _k)
		{
			return blockDecay(_seconds).t[static_cast<size_t>(_k)];
		}

		// What the CPU reads below the noise's decay: another table of its own (rate codes), which a real knob value
		// never reaches: 0 here (registered in utilityTables)
		std::array<int32_t, 128> noiseDecayBelow{};
		Table utilityTableList[] = {{"noiseDecayBelow", noiseDecayBelow.data(), 128, 4, 904848}};

		// The noise's decay, per block: 25 s at the top
		int32_t noiseDecay(const int _k)
		{
			return _k >= 0 ? blockDecayAt(25, _k) : noiseDecayBelow[static_cast<size_t>(128 + _k)];
		}
		// Its filter's sweep, per block: 10 s at the top
		int32_t sweepDecay(const int _k)
		{
			return _k >= 0 ? blockDecayAt(10, _k) : blockDecayAt(15, 128 + _k);
		}
		// The impulse's decay, per sample: 0.5 s at the top, in whole microseconds. Below it, two zero longs, then the
		// 1 s decay per block
		int32_t impulseDecay(const int _k)
		{
			static const DecayTable t(0.5, 1025, 48000, true);
			if(_k >= 0)
				return t.t[static_cast<size_t>(_k)];
			return _k >= -2 ? 0 : blockDecayAt(1, 130 + _k);
		}
		// The noise's attack: full scale over 1 + the blocks of 5 s * (33^(k/127) - 1) / 32, a step a block. Below it,
		// the table of 1 to 128
		int32_t noiseAttack(const int _k)
		{
			static const std::array<int32_t, 128> t = []
			{
				++tablesBuilt();
				std::array<int32_t, 128> r{};
				for(int k = 0; k < 128; ++k)
				{
					const double blocks = 1 + 5 * BlockRate * (std::pow(33.0, k / 127.0) - 1) / 32;
					r[static_cast<size_t>(k)] = static_cast<int32_t>(std::min(2147483647.0, 2147483648.0 / blocks));
				}
				return r;
			}();
			return _k >= 0 ? t[static_cast<size_t>(_k)] : _k + 129;
		}

		// The floor of the noise's decay: the 1 s decay at knob value 40, about -0.64 dB a block
		int32_t noiseDecayFloor() { return blockDecayAt(1, 40); }

		// The digital noise the voice is sent: its four settings, where the CPU hands them to the generator
		void sendNoise(VoiceIo& _io, const int32_t _w0, const int32_t _w1, const int32_t _w2, const int32_t _w3)
		{
			_io.noise[0] = static_cast<uint8_t>(_w0 >> 8);
			_io.noise[1] = static_cast<uint8_t>(_w1 >> 8);
			_io.noise[2] = static_cast<uint8_t>(_w2 >> 8);
			_io.noise[3] = static_cast<uint8_t>(_w3 >> 8);
		}
	}

	void noise(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		const int32_t attack = noiseAttack(knobValue(_io, 5));
		const int decayKnob = knobValue(_io, 2);
		const int32_t decay = decayKnob == 127 ? INT32_MIN : -noiseDecay(decayKnob);

		// the level: the first word squared
		e.macWord(0, word(_io, 0), false, word(_io, 0), false);
		const int32_t level = e.storeClear(0);

		// the main part's factor a block: the decay's and a faster floor's, weighted by the larger of four times the
		// attack word and twice the decay word (each 0x4081 / 2^15 a step, 1/127 of half scale), saturated
		e.macWord(0, 0x4081, false, word(_io, 5), false);
		e.macWord(1, 0x4081, false, word(_io, 2), false);
		const int32_t attackWeight = twiceSat(twiceSat(e.storeClear(0)));
		const int32_t decayWeight = twiceSat(e.storeClear(1));
		const int32_t weight = std::max(attackWeight, decayWeight);
		const int32_t floor = std::max(decay, -noiseDecayFloor());
		e.mac(0, weight, decay);
		e.mac(0, sub(INT32_MAX, weight), floor);
		const int32_t fall = e.storeClear(0);

		int32_t env = stateLong(_io, 2);		// the amplitude's main part
		int32_t tail = stateLong(_io, 3);		// its second part, which falls at the decay alone
		int32_t sweep = stateLong(_io, 4);		// the filter's sweep
		int32_t amp;
		switch(stateLong(_io, 0))
		{
		case 0:
			voiceOff(_io);
			setStateLong(_io, 0, 1);
			amp = 0;
			tail = 0;
			env = 0;
			break;
		case 1:
		case 2:
			if(stateLong(_io, 0) == 1)
			{
				setStateLong(_io, 0, 2);
				sweep = INT32_MAX;
			}
			// the attack: a step a block to full scale
			if(sub(INT32_MAX, attack) > env)
			{
				env = add(env, attack);
				amp = env;
			}
			else
			{
				setStateLong(_io, 1, 0);
				setStateLong(_io, 0, 3);
				amp = env = INT32_MAX;
			}
			break;
		case 3:
			// a hold of a block or so at full scale, until the countdown passes 15
			if(stateLong(_io, 1) <= 15)
				setStateLong(_io, 0, 4);
			amp = tail = env = INT32_MAX;
			break;
		default:
		{
			// the decay: the two parts mixed 4:1
			e.msac(0, fall, env);
			env = e.storeClear(0);
			e.msac(0, decay, tail);
			tail = e.storeClear(0);
			e.mac(0, q31(4, 5), env);
			e.mac(0, q31(1, 5), tail);
			amp = e.storeClear(0);
			break;
		}
		}

		// the noise's colour: the second word, swept by the eighth's depth along the sweep
		const int32_t depth = add(word(_io, 7) - 0x4000, word(_io, 7) - 0x4000);
		e.mac(0, depth, sweep);
		const int32_t colour = std::clamp(add(word(_io, 1), e.storeClear(0)), 0, 0x7fff);
		e.mac(0, amp, level);
		const int32_t out = e.storeClear(0);
		setStateLong(_io, 2, env);
		setStateLong(_io, 3, tail);
		e.mac(0, sweepDecay(knobValue(_io, 6)), sweep);
		setStateLong(_io, 4, e.storeClear(0));
		setBe32(_io.heads.data(), static_cast<uint32_t>(out));
		sendNoise(_io, word(_io, 3), word(_io, 3), colour, word(_io, 4));
		std::fill_n(_io.outA.begin(), 128, uint8_t(0));
	}

	void impulse(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		const int32_t level = word(_io, 0);
		const int32_t width = word(_io, 1);
		const int decayKnob = knobValue(_io, 2);
		const int polarity = knobValue(_io, 7);

		e.macWord(0, level, false, level, false);
		const int32_t peak = e.storeClear(0);

		int32_t done = stateLong(_io, 2);		// the impulse has been sent
		int32_t value = stateLong(_io, 3);		// where it stands
		uint8_t* out = _io.outA.data();
		int n = 32;								// the samples of the block left to write
		if(stateLong(_io, 0) == 0)
		{
			voiceOff(_io);
			done = 0;
			setStateLong(_io, 0, 1);
			n = 0;
		}
		else if(done == 0)
		{
			// a ramp to the peak over the width's samples, down from it where the polarity is set, then the decay
			// a width byte of 255 divides by zero, which traps on the CPU: here the impulse is then a single sample
			const uint16_t samples = static_cast<uint16_t>(asr(width, 8) + 1);
			const auto step = static_cast<int32_t>(static_cast<uint32_t>(peak) / (samples ? samples : 1u));
			int at = 0;
			bool decaying = false;
			if(polarity != 0)
			{
				const int32_t end = sub(step, peak);
				for(; at < 32; ++at)
				{
					setBe32(out + 4 * at, static_cast<uint32_t>(value));
					if(end >= value)
					{
						value = -peak;
						decaying = true;
						break;
					}
					value = sub(value, step);
				}
			}
			else
			{
				const int32_t end = sub(peak, step);
				for(; at < 32; ++at)
				{
					setBe32(out + 4 * at, static_cast<uint32_t>(value));
					if(end <= value)
					{
						value = peak;
						decaying = true;
						break;
					}
					value = add(value, step);
				}
			}
			if(decaying)
			{
				done = 1;
				n = 31 - at;
				out += 4 * (at + 1);
			}
			else
				n = 0;
		}
		if(n)
		{
			// the decay, a factor a sample
			const int32_t c = impulseDecay(decayKnob);
			for(; n > 0; --n, out += 4)
			{
				setBe32(out, static_cast<uint32_t>(value));
				e.mac(0, c, value);
				value = e.storeClear(0);
			}
		}
		setStateLong(_io, 2, done);
		setStateLong(_io, 3, value);
		setBe32(_io.heads.data(), 0);
		sendNoise(_io, 0, 0, 0, 0);
	}

	const Table* utilityTables(int& _count)
	{
		_count = static_cast<int>(std::size(utilityTableList));
		return utilityTableList;
	}
}

namespace elektron::drums::analog::port
{
	void warmUpUtility()
	{
		blockDecay(25);
		impulseDecay(64);
		noiseAttack(64);
	}
}
