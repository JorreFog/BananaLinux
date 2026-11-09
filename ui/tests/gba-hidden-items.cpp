// The hidden items of a GBA Pokémon ROM, for writing its guide (the "secrets" of companions/gba-pokemon/<game>.json):
// every map's background events of the hidden-item kind, with the map's region map section, how many maps that
// section has (x/y say where only on a section of one map), the tile and the item. Read from the ROM itself (the
// maps as pokemon.cpp's findMapGroups finds them: the five games, Pokémon Unbound, Gaia, Glazed - not the
// pokeemerald-expansion builds), so a ROM hack's guide gets what the decompilations gave the five games'. Tab-separated on stdout:
//   section  section name  maps in it  group.num  x  y  item
//   make tests/gba-hidden-items && tests/gba-hidden-items <rom.gba>
#include "pokemon.h"
#include <chrono>
#include <cstdio>
#include <map>
#include <thread>

using namespace banana;

static uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static long ptr(const uint8_t* rom, size_t n, size_t at, size_t len)
{
	if (at + 4 > n) return -1;
	const uint32_t v = le32(rom + at);
	if (v < 0x08000000u || v - 0x08000000u + len > n) return -1;
	return (long)(v - 0x08000000u);
}

int main(int argc, char** argv)
{
	pkmn::Companion c;
	if (argc < 2 || !c.open(argv[1])) { fprintf(stderr, "gba-hidden-items: %s: not a GBA Pokémon game the companion opens\n", argc > 1 ? argv[1] : "(no ROM)"); return 1; }
	std::shared_ptr<const pkmn::RomTables> t;
	for (int i = 0; i < 1200 && !(t = c.tables()); i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
	if (!t || !t->rom) { fprintf(stderr, "gba-hidden-items: its tables weren't read\n"); return 1; }
	const uint8_t* rom = t->rom->data();
	const size_t n = t->rom->size();
	// the maps as pokemon.cpp's findMapGroups found them, checked against the wild tables (a pokeemerald-expansion
	// build's aren't: their tables by shape alone ran into each other and into the base game's maps left in the ROM,
	// and their events are laid out otherwise - Heart & Soul's - so they are left out, not guessed)
	const std::vector<std::vector<long>>& headers = t->mapHeaders;
	if (headers.empty()) { fprintf(stderr, "gba-hidden-items: its map groups weren't found (a pokeemerald-expansion build: not read)\n"); return 1; }
	auto secOf = [&](long h) { return (int)rom[h + 0x14]; };
	std::map<int, int> perSection;	// (how many maps each section has)
	for (auto& g : headers) for (long h : g) perSection[secOf(h)]++;
	int count = 0;
	for (size_t g = 0; g < headers.size(); g++)
		for (size_t m = 0; m < headers[g].size(); m++)
		{
			const long h = headers[g][m];
			// the map header: +4 its events; MapEvents: object, warp, coord and bg event counts, then their pointers;
			// a bg event is 12 bytes: x, y, elevation, kind (7: a hidden item), 2 bytes, then the item in the low half
			const long ev = ptr(rom, n, (size_t)h + 4, 20);
			if (ev < 0) continue;
			const int bgCount = rom[ev + 3];
			const long bg = bgCount ? ptr(rom, n, (size_t)ev + 16, 12 * (size_t)bgCount) : -1;
			for (int i = 0; bg >= 0 && i < bgCount; i++)
			{
				const uint8_t* e = rom + bg + 12 * i;
				if (e[5] != 7) continue;
				const uint16_t item = le16(e + 8);
				const int sec = secOf(h);
				printf("%d\t%s\t%d\t%zu.%zu\t%d\t%d\t%s\n", sec, t->mapName(sec).c_str(), perSection[sec], g, m, le16(e), le16(e + 2),
					t->itemName(item).c_str());
				count++;
			}
		}
	fprintf(stderr, "gba-hidden-items: %d hidden items\n", count);
	return 0;
}
