#include "mdVoice.h"
#include "mdVoiceDsp.h"
#include "mdVoiceInternal.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace md::voice
{
	namespace
	{
		using State = Voice::State;
		using Block = Voice::Block;


		// move a,y:... of an accumulator whose value needs its extension: limited to $7FFFFF or $800000
		uint32_t limit(const int64_t _a)
		{
			return static_cast<uint32_t>(std::clamp<int64_t>(_a, -0x800000, 0x7fffff)) & 0xffffff;
		}

		// An init or update that does nothing (RTS, $10008E)
		void none(Context&) {}

		// GND--'s render ($10008F): 32 zeros
		void renderSilence(Context&, Block& _out)
		{
			_out.fill(0);
		}

		// GND-IM's update ($10010A): the packet's four words into the private ones
		void updateGndIm(Context& _c)
		{
			auto& _s = _c.y;
			for(int k = 0; k < 4; ++k)
				_s[5 + k] = _s[1 + k];
		}

		// GND-IM's render ($100113): Y:S+6 for Y:S+5 samples, then Y:S+8 for Y:S+7 samples, then silence. The counts
		// are 56-bit accumulators within the block, limited when stored back.
		void renderGndIm(Context& _c, Block& _out)
		{
			auto& _s = _c.y;
			int64_t a = dsp::sx(_s[5]);
			int64_t b = dsp::sx(_s[7]);
			const int32_t first = dsp::sx(_s[6]);
			const int32_t second = dsp::sx(_s[8]);
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

		// GND-- to GND-IM, by type (machine id + 1); type 0, never assigned, renders silence as the harness's idle voices
		// do
		constexpr std::array<Routines, 5> kGnd = {{
			{none, none, renderSilence},					// 0
			{none, none, renderSilence},					// 1 GND--
			{none, gnd::updateSn, gnd::renderSn},			// 2 GND-SN
			{gnd::initNs, gnd::updateNs, gnd::renderNs},	// 3 GND-NS
			{none, updateGndIm, renderGndIm},				// 4 GND-IM
		}};

		// ROM-01 to -32 (types 129-160) and ROM-33 to -48 (177-192) share one init, update and render
		constexpr Routines kRom = {rom::init, rom::update, rom::render};

		// The type's routines, nullptr when it is not ported
		const Routines* find(const uint32_t _type)
		{
			if(isRom(_type))
				return &kRom;
			if(_type < kGnd.size())
				return &kGnd[_type];
			if(const auto* r = trx::routines(_type))
				return r;
			return efm::routines(_type);
		}

		const Routines& routines(const uint32_t _type)
		{
			if(const auto* r = find(_type))
				return *r;
			throw std::out_of_range("md::voice: type " + std::to_string(_type) + " is not ported");
		}
	}

	bool Voice::ported(const uint32_t _type)
	{
		return find(_type) != nullptr;
	}

	void Voice::setSlot(const uint32_t* _words, const int _count)
	{
		for(int k = 0; k < _count && k < kStateWords; ++k)
			m_state[static_cast<size_t>(k)] = _words[k] & 0xffffff;
	}

	void Voice::render(Block& _out)
	{
		Context c{m_state, m_x, m_active, *m_shared};
		if(const uint32_t pending = m_state[0])
		{
			const auto& next = routines(pending);
			// The type is stored before init (P:$8B), init runs on a type change only, and the trigger is cleared
			// before update (P:$97)
			if(pending != m_active)
			{
				m_active = c.type = pending;
				next.init(c);
			}
			m_state[0] = 0;
			next.update(c);
		}
		routines(m_active).render(c, _out);
	}
}
