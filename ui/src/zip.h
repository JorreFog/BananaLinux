#pragma once
// banana-ui: reading one member of a .zip in order, for the engine's ROM icons (rnds::setZipReader): the central
// directory for the list, then the chosen member streamed (stored, or inflated with zlib) until the engine has what
// it wants. Archives over 4 GB (zip64) and encrypted members are not read.
#include "../rnds/RndsRaster.h"

namespace banana
{
	bool readZip(const std::string& zip, const std::function<int(const std::vector<rnds::ZipMember>&)>& pick,
		const std::function<bool(const uint8_t*, size_t)>& sink);
}
