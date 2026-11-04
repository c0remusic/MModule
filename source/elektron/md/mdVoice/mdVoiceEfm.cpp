#include "mdVoiceEfm.h"

namespace md::voice::efm
{
	namespace
	{
		constexpr Routines kBd = {bd::init, bd::update, bd::render};
	}

	const Routines* routines(const uint32_t _type)
	{
		switch(_type)
		{
			case 33:	return &kBd;
			default:	return nullptr;
		}
	}
}
