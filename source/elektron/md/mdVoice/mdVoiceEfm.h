// The EFM machines' voice code (DSP2, OS 1.63), ported as mdVoiceTrx.h says: the helpers they share, on top of the TRX
// machines', and each machine's routines, in a file of its own (mdVoiceEfmBd.cpp for EFM-BD and so on); the dispatch
// by type is in mdVoiceEfm.cpp.
#pragma once

#include "mdVoiceTrx.h"

namespace md::voice::efm
{
	using namespace trx;

	// A sine that hears itself: the feedback, an L word the DSP limits but a byte down from a product never reaches the
	// limit, adds to the phase for the read only, and the word read times _gain, down a byte, is the next feedback. The
	// phase keeps 48 bits, B2 never read
	inline uint32_t sineFedBack(const TrxTables& _t, uint64_t& _phase, int64_t& _feedback, const int64_t _step,
		const uint32_t _gain)
	{
		const uint64_t read = add48(_phase, _feedback) & 0x007fffffffffull;
		const uint32_t w = _t.sine[read >> 24];
		_phase = add48(add48(read, -_feedback), _step);
		_feedback = mul(w, _gain) >> 8;
		return w;
	}

	// A sine read before its step, the phase masked to a period after it
	inline uint32_t sineStep(const TrxTables& _t, uint64_t& _phase, const int64_t _step)
	{
		const uint32_t w = word(_t.sine, static_cast<uint32_t>(_phase >> 24));
		_phase = add48(_phase, _step) & 0x007fffffffffull;
		return w;
	}

	// A carrier's step from its modulator's word: the pitch, bent by the word at _depth
	inline int64_t fmStep(const uint32_t _mod, const uint32_t _pitch, const uint32_t _depth)
	{
		return (shl(mul(hi(round(mul(_mod, _pitch))), _depth), 3) + acc(_pitch)) >> 8;
	}

	// An L word into the state at Y:_n and Y:_n + 1, and into the scratch at L:_n, as the DSP leaves it
	inline void storeL(Context& _c, const uint32_t _n, const int64_t _l)
	{
		_c.y[_n] = _c.shared.scratch.x[_n] = hi(_l);
		_c.y[_n + 1] = _c.shared.scratch.y[_n] = lo(_l);
	}

	// A one-pole filter in place over 32 words, by a coefficient word: (1 + c) / 2 (1 - z^-1) over 1 - c z^-1. Its state
	// from Y:_state: the last input scaled by the gain, which the scratch keeps too, and the last output. The DSP
	// rounds the first _rounded words (MACR), the rest not (MAC): none or all but in EFM-RS, which rounds two
	inline void onePole(Context& _c, uint32_t* const _io, const uint32_t _coef, const uint32_t _state,
		const int _rounded)
	{
		const uint32_t gain = hi((acc(_coef) >> 1) + acc(0x400000));
		uint32_t scaled = _c.y[_state], output = _c.y[_state + 1];
		for(int k = 0; k < 32; ++k)
		{
			const int64_t a = mul(_io[k], gain);
			const int64_t sum = a - acc(scaled) + mul(output, _coef);
			output = lim(k < _rounded ? round(sum) : sum);
			scaled = hi(a);
			_io[k] = output;
		}
		_c.y[_state] = _c.shared.scratch.x[_state] = scaled;
		_c.y[_state + 1] = output;
	}

	namespace bd
	{
		void init(Context& _c);
		void update(Context& _c);
		void render(Context& _c, Voice::Block& _out);
		// The render's two sines into 32 words, as EFM-XT plays them too. Their state, nine words from Y:_state: the
		// first's phase, the second's, the pitch's level, the first's, the amp, and the feedback. The packet's words
		// stay at Y:$01-$06 and Y:$08
		void pair(Context& _c, uint32_t _state, uint32_t* _out);
	}

	namespace sd
	{
		void init(Context& _c);
		void update(Context& _c);
		void render(Context& _c, Voice::Block& _out);
	}

	namespace xt
	{
		void init(Context& _c);
		void update(Context& _c);
		void render(Context& _c, Voice::Block& _out);
	}
}
