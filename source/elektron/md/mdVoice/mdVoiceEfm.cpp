#include "mdVoiceEfm.h"

namespace md::voice::efm
{
	namespace
	{
		constexpr Routines kBd = {bd::init, bd::update, bd::render};
		constexpr Routines kSd = {sd::init, sd::update, sd::render};
	}

	const Routines* routines(const uint32_t _type)
	{
		switch(_type)
		{
			case 33:	return &kBd;
			case 34:	return &kSd;
			default:	return nullptr;
		}
	}
}
