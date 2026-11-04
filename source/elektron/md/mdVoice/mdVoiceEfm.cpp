#include "mdVoiceEfm.h"

namespace md::voice::efm
{
	namespace
	{
		constexpr Routines kBd = {bd::init, bd::update, bd::render};
		constexpr Routines kSd = {sd::init, sd::update, sd::render};
		constexpr Routines kXt = {xt::init, xt::update, xt::render};
		constexpr Routines kCp = {cp::init, cp::update, cp::render};
	}

	const Routines* routines(const uint32_t _type)
	{
		switch(_type)
		{
			case 33:	return &kBd;
			case 34:	return &kSd;
			case 35:	return &kXt;
			case 36:	return &kCp;
			default:	return nullptr;
		}
	}
}
