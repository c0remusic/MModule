// The EFM machines' voice code (DSP2, OS 1.63), ported as mdVoiceTrx.h says: each machine's routines, in a file of its
// own (mdVoiceEfmBd.cpp for EFM-BD and so on), on the TRX machines' helpers; the dispatch by type is in mdVoiceEfm.cpp.
#pragma once

#include "mdVoiceTrx.h"

namespace md::voice::efm
{
	using namespace trx;

	namespace bd
	{
		void init(Context& _c);
		void update(Context& _c);
		void render(Context& _c, Voice::Block& _out);
	}
}
