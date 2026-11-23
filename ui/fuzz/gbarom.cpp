// Fuzz target: the GBA Pokémon companion's ROM readers (pokemon.cpp findTables and co., gba/gen3rom.cpp,
// gba/expansion.cpp): a GBA ROM on the card, a hack's or a bad dump's as much as a game's own, read as the companion
// reads it once the game starts: the profile by the header's code, the tables found by their shape (or a
// pokeemerald-expansion build's own reader, "RHHEXP" in the header), then the DS pages' data from them. The input is
// the ROM itself; its first byte, when below 10, writes one of the five games' codes (or their Spanish releases') into
// the header and then is the ROM's first byte as any other, so the code-specific readers are reached without the
// fuzzer having to find the four letters at 0xAC.
#include "../src/gba/expansion.h"
#include "../src/gba/gen3rom.h"
#include "../src/pokemon.h"
#include <cstring>
#include <memory>
#include <vector>
#include <zlib.h>

using namespace banana;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	static const char* const CODES[] = { "AXVE", "AXPE", "BPEE", "BPRE", "BPGE", "AXVS", "AXPS", "BPES", "BPRS", "BPGS" };
	// (Companion::open reads the tables only once the header told a game: never from a ROM shorter than one)
	if (size < 0xc0) return 0;
	std::vector<uint8_t> rom(data, data + size);
	if (rom[0] < sizeof CODES / sizeof CODES[0]) memcpy(&rom[0xac], CODES[rom[0]], 4);
	const uint8_t* r = rom.data();
	const size_t n = rom.size();
	// (Companion::open: the profile from the first 0x400 bytes, the tables from the whole ROM)
	const pkmn::GameProfile* profile = pkmn::detectGame(r, std::min<size_t>(n, 0x400));
	auto t = std::make_shared<pkmn::RomTables>();
	std::shared_ptr<const pkmn::GameProfile> own;
	if (!(pkmn::hasOwnProfile(r, std::min<size_t>(n, 0x400)) && gba::expansionProfile(r, n, own, *t))) *t = pkmn::findTables(r, n, profile);
	t->crc = (uint32_t)crc32(0L, r, (uInt)n);
	int major, minor, patch;
	gba::expansionVersion(r, n, major, minor, patch);
	gba::ExpansionLayout L;
	gba::findExpansion(r, n, L);
	std::vector<uint8_t> out;
	for (size_t at : { (size_t)0, (size_t)0xc0, n / 2 }) { gba::decompressPic(r, n, at, out, 0x2000); pkmn::lz77(r, n, at, out, 0x2000); }
	t->rom = std::make_shared<const std::vector<uint8_t>>(std::move(rom));
	nds::Game g;
	bool unsupported = false;
	if (auto d = gba::readGame(*t, &g, &unsupported))
		for (uint16_t s : { 1, 25, 201, 386 }) d->sprite(s, false, 0);
	return 0;
}
