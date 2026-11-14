#pragma once
// banana-sync's RomM backend: rommapp/romm's REST API (5.x) behind the Backend that sync_core.h describes.
#include "sync_core.h"
#include <memory>

namespace bsync
{
	std::unique_ptr<Backend> newRomm(Conf& c);
}
