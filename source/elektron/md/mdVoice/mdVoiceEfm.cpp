#include "mdVoiceEfm.h"

namespace md::voice::efm
{
	namespace
	{
		constexpr Routines kBd = {bd::init, bd::update, bd::render};
		constexpr Routines kSd = {sd::init, sd::update, sd::render};
		constexpr Routines kXt = {xt::init, xt::update, xt::render};
		constexpr Routines kCp = {cp::init, cp::update, cp::render};
		constexpr Routines kRs = {rs::init, rs::update, rs::render};
		constexpr Routines kCb = {cb::init, cb::update, cb::render};
		constexpr Routines kHh = {hh::init, hh::update, hh::render};
		constexpr Routines kCy = {cy::init, cy::update, cy::render};
	}

	const Routines* routines(const uint32_t _type)
	{
		switch(_type)
		{
			case 33:	return &kBd;
			case 34:	return &kSd;
			case 35:	return &kXt;
			case 36:	return &kCp;
			case 37:	return &kRs;
			case 38:	return &kCb;
			case 39:	return &kHh;
			case 40:	return &kCy;
			default:	return nullptr;
		}
	}
}
