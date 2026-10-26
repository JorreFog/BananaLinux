// Fuzz target: a DS ROM's own icon (rnds::romIcon), from a .nds and from a .zip holding one: the header's banner
// offset, the banner, its tiles and palette. romIcon remembers icons per path, so each input gets paths of its own.
#include "../rnds/RndsRaster.h"
#include "../src/zip.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>

static void put(const std::string& path, const uint8_t* data, size_t size)
{
	if (FILE* f = fopen(path.c_str(), "wb")) { fwrite(data, 1, size, f); fclose(f); }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	static unsigned long n = 0;
	static bool once = (rnds::setZipReader(banana::readZip), true);
	(void)once;
	std::string base = "/tmp/banana-fuzz-rom-" + std::to_string(getpid()) + "-" + std::to_string(n++ % 4096);
	put(base + ".nds", data, size);
	put(base + ".zip", data, size);
	rnds::romIcon(base + ".nds");
	rnds::romIcon(base + ".zip");
	unlink((base + ".nds").c_str());
	unlink((base + ".zip").c_str());
	return 0;
}
