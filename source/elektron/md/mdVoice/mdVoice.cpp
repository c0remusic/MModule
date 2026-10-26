#include "mdVoice.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace md::voice
{
	namespace
	{
		using State = Voice::State;
		using Block = Voice::Block;

		int64_t signExtend(const uint32_t _word)
		{
			return static_cast<int64_t>(static_cast<int32_t>(_word << 8) >> 8);
		}

		// move a,y:... of an accumulator whose value needs its extension: limited to $7FFFFF or $800000
		uint32_t limit(const int64_t _a)
		{
			return static_cast<uint32_t>(std::clamp<int64_t>(_a, -0x800000, 0x7fffff)) & 0xffffff;
		}

		// An init or update that does nothing (RTS, $10008E)
		void none(State&) {}

		// GND--'s render ($10008F): 32 zeros
		void renderSilence(State&, Block& _out)
		{
			_out.fill(0);
		}

		// GND-IM's update ($10010A): the packet's four words into the private ones
		void updateGndIm(State& _s)
		{
			for(int k = 0; k < 4; ++k)
				_s[5 + k] = _s[1 + k];
		}

		// GND-IM's render ($100113): Y:S+6 for Y:S+5 samples, then Y:S+8 for Y:S+7 samples, then silence. The counts
		// are 56-bit accumulators within the block, limited when stored back.
		void renderGndIm(State& _s, Block& _out)
		{
			int64_t a = signExtend(_s[5]);
			int64_t b = signExtend(_s[7]);
			const int32_t first = static_cast<int32_t>(signExtend(_s[6]));
			const int32_t second = static_cast<int32_t>(signExtend(_s[8]));
			for(auto& sample : _out)
			{
				if(a != 0)
				{
					--a;
					sample = first;
				}
				else if(b != 0)
				{
					--b;
					sample = second;
				}
				else
				{
					sample = 0;
				}
			}
			_s[5] = limit(a);
			_s[7] = limit(b);
		}

		// A type's three entries, as the dispatch tables at Y:$145AF5, $145BB6 and $145C77 give them
		struct Routines
		{
			void (*init)(State&);
			void (*update)(State&);
			void (*render)(State&, Block&);
		};

		// Indexed by type (machine id + 1); type 0, never assigned, renders silence as the harness's idle voices do
		constexpr std::array<Routines, 5> kPorted = {{
			{none, none, renderSilence},		// 0
			{none, none, renderSilence},		// 1 GND--
			{nullptr, nullptr, nullptr},		// 2 GND-SN
			{nullptr, nullptr, nullptr},		// 3 GND-NS
			{none, updateGndIm, renderGndIm},	// 4 GND-IM
		}};

		const Routines& routines(const uint32_t _type)
		{
			if(!Voice::ported(_type))
				throw std::out_of_range("md::voice: type " + std::to_string(_type) + " is not ported");
			return kPorted[_type];
		}
	}

	bool Voice::ported(const uint32_t _type)
	{
		return _type < kPorted.size() && kPorted[_type].render;
	}

	void Voice::setSlot(const uint32_t* _words, const int _count)
	{
		for(int k = 0; k < _count && k < kStateWords; ++k)
			m_state[static_cast<size_t>(k)] = _words[k] & 0xffffff;
	}

	void Voice::render(Block& _out)
	{
		if(const uint32_t pending = m_state[0])
		{
			const auto& next = routines(pending);
			// The type is stored before init (P:$8B), init runs on a type change only, and the trigger is cleared
			// before update (P:$97)
			if(pending != m_active)
			{
				m_active = pending;
				next.init(m_state);
			}
			m_state[0] = 0;
			next.update(m_state);
		}
		routines(m_active).render(m_state, _out);
	}
}
