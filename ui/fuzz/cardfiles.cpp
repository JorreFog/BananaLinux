// Fuzz target: Banana's own files on the card, which a hand edit, an older or newer Banana, or a write cut short by
// the battery can leave in any state. One input is split into systems.conf, settings.conf, favorites.txt,
// favorite-systems.txt, playstats.tsv and bananad.conf; then the menu reads them as it does at boot: the settings (and
// the card's copy made whole), a bananad.conf value, the library scanned over a few games, shelves arranged, a search,
// and a play recorded (the stats written back and read again).
#include "../src/library.h"
#include "../src/settings.h"
#include "../src/settings_defaults.h"
#include <fuzzer/FuzzedDataProvider.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace banana;

static std::string root;

static void put(const std::string& path, const std::string& s)
{
	if (FILE* f = fopen(path.c_str(), "wb")) { fwrite(s.data(), 1, s.size(), f); fclose(f); }
}

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
	root = "/tmp/banana-fuzz-card-" + std::to_string(getpid());
	for (const char* d : { "", "/roms", "/roms/nds", "/roms/gba", "/roms/odd", "/data" }) mkdir((root + d).c_str(), 0755);
	for (const char* g : { "/roms/nds/Peel Quest.nds", "/roms/nds/Yellow [!].zip", "/roms/gba/Orchard.gba", "/roms/odd/x.y" }) put(root + g, "x");
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	FuzzedDataProvider fd(data, size);
	const std::string conf = root + "/systems.conf", dataDir = root + "/data";
	put(conf, fd.ConsumeRandomLengthString(4096));
	put(dataDir + "/settings.conf", fd.ConsumeRandomLengthString(4096));
	put(dataDir + "/favorites.txt", fd.ConsumeRandomLengthString(1024));
	put(dataDir + "/favorite-systems.txt", fd.ConsumeRandomLengthString(256));
	put(dataDir + "/playstats.tsv", fd.ConsumeRandomLengthString(4096));
	put(dataDir + "/bananad.conf", fd.ConsumeRemainingBytesAsString());

	auto settings = readSettings(dataDir);
	for (const auto& kv : settings) settingValid(kv.first, kv.second);
	normalizeSettings(dataDir);
	(void)atoi(getConfValue(dataDir + "/bananad.conf", "battery_shutdown_percent", "3").c_str());

	const char* assets = getenv("BANANA_FUZZ_ASSETS");
	Library lib(conf, root + "/roms", dataDir, assets ? assets : "ui/assets/rnds");
	lib.scan();
	lib.arrange();
	lib.setSearch("pe");
	lib.setSearch("");
	if (lib.systemCount() > 0 && lib.shelf(0).games.size() > 0)
	{
		lib.openLibrary(0);
		lib.recordPlay(0, 61, 3, 10);
		lib.scan();
	}
	return 0;
}
