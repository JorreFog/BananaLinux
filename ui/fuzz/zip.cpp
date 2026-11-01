// Fuzz target: a .zip on the card (zip.cpp), read as the ROM icons read it: the central directory's list, each member
// picked in turn, its bytes streamed (stored or inflated) to the end. readZip takes a path, so each input is written to
// a file first.
#include "../src/zip.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>

using namespace banana;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	static const std::string path = "/tmp/banana-fuzz-zip-" + std::to_string(getpid()) + ".zip";
	FILE* f = fopen(path.c_str(), "wb");
	if (!f) return 0;
	fwrite(data, 1, size, f);
	fclose(f);
	size_t members = 0;
	readZip(path, [&](const std::vector<rnds::ZipMember>& ms) { members = ms.size(); return -1; }, [](const uint8_t*, size_t) { return true; });
	for (size_t k = 0; k < members && k < 8; k++)
	{
		size_t got = 0;
		readZip(path, [k](const std::vector<rnds::ZipMember>&) { return (int)k; },
			[&got](const uint8_t* p, size_t n) { for (size_t i = 0; i < n; i++) got += p[i]; return got < (64u << 20); });
	}
	return 0;
}
