// Fuzz target: the DS Pokémon companion's ROM reader (nds/rom.cpp, nitro.cpp, townmap.cpp): a cartridge image on the
// card, a ROM hack's or a bad dump's as much as a game's own. Random bytes never get past the header and the file
// tables, so the target builds a small game itself (one of the nine releases or an hg-engine HeartGold, picked by the
// input): a NitroFS image with the files the reader looks for (text banks with names enough, personal data, moves,
// items, encounters, trainers, map headers and the TM list in the ARM9), every one of which the input can change: a
// member of an archive replaced or patched, a whole file replaced, files added (the town map's, sprites', overlays').
// Then: the ROM read as the companion reads it (loadRom), its sprites and RAM hints, and the cache it wrote on the card
// patched by the input and read back. The last game number takes the input as the ROM image itself (the file system).
#include "../src/nds/nitro.h"
#include "../src/nds/rom.h"
#include "../src/nds/townmap.h"
#include <fuzzer/FuzzedDataProvider.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace banana::nds;
using nitro::Bytes;

static std::string dir;

static void put16(Bytes& b, size_t o, uint16_t v) { if (b.size() < o + 2) b.resize(o + 2); b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); }
static void put32(Bytes& b, size_t o, uint32_t v) { if (b.size() < o + 4) b.resize(o + 4); for (int i = 0; i < 4; i++) b[o + i] = (uint8_t)(v >> (8 * i)); }
static void add16(Bytes& b, uint16_t v) { put16(b, b.size(), v); }
static void add32(Bytes& b, uint32_t v) { put32(b, b.size(), v); }

static Bytes narc(const std::vector<Bytes>& members)
{
	Bytes img;
	std::vector<std::pair<uint32_t, uint32_t>> fat;
	for (auto& m : members) { while (img.size() % 4) img.push_back(0xff); fat.push_back({ (uint32_t)img.size(), (uint32_t)(img.size() + m.size()) }); img.insert(img.end(), m.begin(), m.end()); }
	Bytes b = { 'N', 'A', 'R', 'C', 0xfe, 0xff, 0x00, 0x01 };
	add32(b, 0); add16(b, 16); add16(b, 3);
	b.insert(b.end(), { 'B', 'T', 'A', 'F' }); add32(b, 12 + 8 * (uint32_t)fat.size()); add32(b, (uint32_t)fat.size());
	for (auto& f : fat) { add32(b, f.first); add32(b, f.second); }
	b.insert(b.end(), { 'B', 'T', 'N', 'F' }); add32(b, 16); add32(b, 4); add16(b, 0); add16(b, 1);
	b.insert(b.end(), { 'G', 'M', 'I', 'F' }); add32(b, 8 + (uint32_t)img.size());
	b.insert(b.end(), img.begin(), img.end());
	put32(b, 8, (uint32_t)b.size());
	return b;
}

// text banks (Gen IV: pokeplatinum's message.c, its character codes for ASCII and "é"; Gen V: UTF-16 XORed from the end)
static Bytes bank4(const std::vector<std::string>& entries)
{
	Bytes b; add16(b, (uint16_t)entries.size()); add16(b, 0);
	b.resize(4 + 8 * entries.size());
	for (size_t i = 0; i < entries.size(); i++)
	{
		std::vector<uint16_t> c;
		for (size_t j = 0; j < entries[i].size(); j++)
		{
			unsigned char ch = (unsigned char)entries[i][j];
			if (ch == 0xc3) { c.push_back(0x188); j++; continue; }	// (é)
			c.push_back(ch >= '0' && ch <= '9' ? 0x121 + (ch - '0') : ch >= 'A' && ch <= 'Z' ? 0x12b + (ch - 'A') :
				ch >= 'a' && ch <= 'z' ? 0x145 + (ch - 'a') : ch == ' ' ? 0x1de : 0x1ac);
		}
		c.push_back(0xffff);
		put32(b, 4 + 8 * i, (uint32_t)b.size()); put32(b, 8 + 8 * i, (uint32_t)c.size());
		uint16_t key = (uint16_t)((i + 1) * 596947);
		for (uint16_t v : c) { add16(b, v ^ key); key = (uint16_t)(key + 18749); }
	}
	return b;
}

static Bytes bank5(const std::vector<std::string>& entries)
{
	Bytes sec; add32(sec, 0);
	sec.resize(4 + 8 * entries.size());
	for (size_t i = 0; i < entries.size(); i++)
	{
		std::vector<uint16_t> c(entries[i].begin(), entries[i].end());
		c.push_back(0xffff);
		put32(sec, 4 + 8 * i, (uint32_t)sec.size()); put16(sec, 8 + 8 * i, (uint16_t)c.size());
		uint16_t key = 0x1234;
		std::vector<uint16_t> e(c.size());
		for (size_t j = c.size(); j-- > 0;) { e[j] = c[j] ^ key; key = (uint16_t)(key >> 3 | key << 13); }
		for (uint16_t v : e) add16(sec, v);
	}
	put32(sec, 0, (uint32_t)sec.size());
	Bytes b; add16(b, 1); add16(b, (uint16_t)entries.size()); add32(b, (uint32_t)sec.size()); add32(b, 0); add32(b, 16);
	b.insert(b.end(), sec.begin(), sec.end());
	return b;
}

// n entries: these first, then letters
static std::vector<std::string> words(size_t n, std::vector<std::string> first = {})
{
	while (first.size() < n) first.push_back(std::string(1, (char)('A' + first.size() % 26)));
	return first;
}

// a file of the image: an archive's members, or plain bytes
struct File { bool isNarc = false; std::vector<Bytes> members; Bytes raw; };

// every file name the reader asks for, so the input can make any of them (rom.cpp's and townmap.cpp's)
static const char* const PATHS[] = { "a/0/0/2", "a/0/0/4", "a/0/0/9", "a/0/1/1", "a/0/1/2", "a/0/1/6", "a/0/1/7", "a/0/1/8",
	"a/0/1/9", "a/0/2/1", "a/0/2/4", "a/0/2/7", "a/0/3/2", "a/0/3/3", "a/0/3/4", "a/0/3/7", "a/0/4/1", "a/0/5/0", "a/0/5/5",
	"a/0/5/6", "a/0/7/4", "a/0/8/4", "a/0/8/5", "a/0/8/6", "a/0/9/1", "a/0/9/2", "a/0/9/3", "a/1/1/4", "a/1/2/6", "a/1/2/7",
	"a/1/3/6", "a/1/4/4", "a/2/5/2", "a/2/7/3", "application/zukanlist/zkn_data/zukan_data.narc", "data/pokepic_f.narc",
	"data/pokepic_m.narc", "data/tmap_block.dat", "debug/cb_edit/d_test.narc", "extra/new_move_data.narc",
	"fielddata/encountdata/d_enc_data.narc", "fielddata/encountdata/p_enc_data.narc", "fielddata/encountdata/pl_enc_data.narc",
	"fielddata/eventdata/zone_event.narc", "fielddata/eventdata/zone_event_release.narc", "fielddata/mapmatrix/map_matrix.narc",
	"fielddata/maptable/mapname.bin", "graphic/tmap_gra.narc", "itemtool/itemdata/item_data.narc",
	"itemtool/itemdata/pl_item_data.narc", "msgdata/msg.narc", "msgdata/pl_msg.narc", "poketool/personal/evo.narc",
	"poketool/personal/personal.narc", "poketool/personal/pl_personal.narc", "poketool/personal/wotbl.narc",
	"poketool/personal_pearl/personal.narc", "poketool/pokegra/otherpoke.narc", "poketool/pokegra/pl_otherpoke.narc",
	"poketool/pokegra/pl_pokegra.narc", "poketool/pokegra/pokegra.narc", "poketool/trainer/trdata.narc",
	"poketool/trainer/trpoke.narc", "poketool/waza/pl_waza_tbl.narc", "poketool/waza/waza_tbl.narc" };
static const size_t NPATHS = sizeof PATHS / sizeof PATHS[0];
// (and four overlays, the ARM9 and the overlay table: slots after the paths)
enum { OVERLAYS = 4 };

struct Made
{
	const char* code;
	int gen;	// 4, 5
	int dex;
	const char *text, *personal, *learn, *evo, *moves, *items, *enc, *trdata, *trpoke;
	int bSpecies, bMoves, bLocations, maxBank;
	size_t personalSize, encSize;
};
static const Made GAMES[] = {
	{ "ADAE", 4, 493, "msgdata/msg.narc", "poketool/personal/personal.narc", "poketool/personal/wotbl.narc", "poketool/personal/evo.narc",
		"poketool/waza/waza_tbl.narc", "itemtool/itemdata/item_data.narc", "fielddata/encountdata/d_enc_data.narc",
		"poketool/trainer/trdata.narc", "poketool/trainer/trpoke.narc", 362, 588, 382, 621, 44, 424 },
	{ "APAE", 4, 493, "msgdata/msg.narc", "poketool/personal_pearl/personal.narc", "poketool/personal/wotbl.narc", "poketool/personal/evo.narc",
		"poketool/waza/waza_tbl.narc", "itemtool/itemdata/item_data.narc", "fielddata/encountdata/p_enc_data.narc",
		"poketool/trainer/trdata.narc", "poketool/trainer/trpoke.narc", 362, 588, 382, 621, 44, 424 },
	{ "CPUE", 4, 493, "msgdata/pl_msg.narc", "poketool/personal/pl_personal.narc", "poketool/personal/wotbl.narc", "poketool/personal/evo.narc",
		"poketool/waza/pl_waza_tbl.narc", "itemtool/itemdata/pl_item_data.narc", "fielddata/encountdata/pl_enc_data.narc",
		"poketool/trainer/trdata.narc", "poketool/trainer/trpoke.narc", 412, 647, 433, 711, 44, 424 },
	{ "IPKE", 4, 493, "a/0/2/7", "a/0/0/2", "a/0/3/3", "a/0/3/4", "a/0/1/1", "a/0/1/7", "a/0/3/7", "a/0/5/5", "a/0/5/6", 237, 750, 279, 816, 44, 196 },
	{ "IPGE", 4, 493, "a/0/2/7", "a/0/0/2", "a/0/3/3", "a/0/3/4", "a/0/1/1", "a/0/1/7", "a/1/3/6", "a/0/5/5", "a/0/5/6", 237, 750, 279, 816, 44, 196 },
	{ "IRBO", 5, 649, "a/0/0/2", "a/0/1/6", "a/0/1/8", "a/0/1/9", "a/0/2/1", "a/0/2/4", "a/1/2/6", "a/0/9/2", "a/0/9/3", 70, 203, 89, 260, 60, 232 },
	{ "IRAO", 5, 649, "a/0/0/2", "a/0/1/6", "a/0/1/8", "a/0/1/9", "a/0/2/1", "a/0/2/4", "a/1/2/6", "a/0/9/2", "a/0/9/3", 70, 203, 89, 260, 60, 232 },
	{ "IREO", 5, 649, "a/0/0/2", "a/0/1/6", "a/0/1/8", "a/0/1/9", "a/0/2/1", "a/0/2/4", "a/1/2/7", "a/0/9/1", "a/0/9/2", 90, 403, 109, 464, 76, 232 },
	{ "IRDO", 5, 649, "a/0/0/2", "a/0/1/6", "a/0/1/8", "a/0/1/9", "a/0/2/1", "a/0/2/4", "a/1/2/7", "a/0/9/1", "a/0/9/2", 90, 403, 109, 464, 76, 232 },
	// (an hg-engine HeartGold: another language's code, its own moves and pictures)
	{ "IPKJ", 4, 1025, "a/0/2/7", "a/0/0/2", "a/0/3/3", "a/0/3/4", "a/0/1/1", "a/0/1/7", "a/0/3/7", "a/0/5/5", "a/0/5/6", 237, 750, 279, 816, 52, 196 },
};
static const size_t NGAMES = sizeof GAMES / sizeof GAMES[0];

// Nitro's 2D files as the town maps and sprites read them: a palette (NCLR), characters (NCGR: 4 or 8 bits a pixel),
// a screen of w x h tiles (NSCR), a map matrix of w x h zones (Gen IV's and Gen V's)
static Bytes g2d(const char* magic, const char* block, size_t blockHead, size_t dataAt, const Bytes& data)
{
	Bytes b(16, 0);
	memcpy(&b[0], magic, 4);
	put16(b, 14, 1);
	Bytes k(blockHead, 0);
	memcpy(&k[0], block, 4);
	put32(k, 4, (uint32_t)(blockHead + data.size()));
	if (dataAt) put32(k, dataAt, (uint32_t)(blockHead - 8));	// (an NSCR has none: its data right after)
	b.insert(b.end(), k.begin(), k.end());
	b.insert(b.end(), data.begin(), data.end());
	put32(b, 8, (uint32_t)b.size());
	return b;
}
static Bytes nclr(size_t colours)
{
	Bytes d;
	for (size_t i = 0; i < colours; i++) add16(d, (uint16_t)(i * 0x0421 + i * 7));
	Bytes b = g2d("RLCN", "TTLP", 0x18, 20, d);
	put32(b, 16 + 8, 3); put32(b, 16 + 16, (uint32_t)d.size());
	return b;
}
static Bytes ncgr(bool bpp8, size_t tiles, uint16_t hTiles = 0, uint16_t wTiles = 0)
{
	Bytes d(tiles * (bpp8 ? 64 : 32));
	for (size_t i = 0; i < d.size(); i++) d[i] = (uint8_t)(i * 13 + i / 64);
	Bytes b = g2d("RGCN", "RAHC", 0x20, 28, d);
	put16(b, 16 + 8, hTiles); put16(b, 16 + 10, wTiles); put32(b, 16 + 12, bpp8 ? 4 : 3); put32(b, 16 + 24, (uint32_t)d.size());
	return b;
}
static Bytes nscr(int w, int h, size_t tiles)
{
	Bytes d;
	for (int i = 0; i < w * h; i++) add16(d, (uint16_t)((size_t)i % tiles | (i % 3 == 0 ? 0x400 : 0) | (i % 5 == 0 ? 0x1000 : 0)));
	Bytes b = g2d("RCSN", "NRCS", 0x14, 0, d);
	put16(b, 16 + 8, (uint16_t)(w * 8)); put16(b, 16 + 10, (uint16_t)(h * 8)); put32(b, 16 + 16, (uint32_t)d.size());
	return b;
}
static Bytes matrix(int w, int h, bool gen5, size_t zones)
{
	Bytes m;
	if (gen5) { add32(m, 1); add16(m, (uint16_t)w); add16(m, (uint16_t)h); for (int i = 0; i < w * h; i++) add32(m, 0); for (int i = 0; i < w * h; i++) add32(m, i % 4 == 3 ? 0xffffffff : (uint32_t)(i % zones)); }
	else { m = { (uint8_t)w, (uint8_t)h, 1, 0, 1, 'm' }; for (int i = 0; i < w * h; i++) add16(m, (uint16_t)(i % 4 == 3 ? 0 : i % zones)); }
	return m;
}

static std::map<std::string, File> skeleton(const Made& g, Bytes& arm9)
{
	std::map<std::string, File> fs;
	auto narcOf = [](size_t n, size_t each) { File f; f.isNarc = true; f.members.assign(n, Bytes(each, 0)); return f; };
	const size_t ZONES = 4, MOVES = 120;
	File text; text.isNarc = true;
	const bool engine = !strcmp(g.code, "IPKJ");
	for (int i = 0; i <= g.maxBank; i++)
	{
		std::vector<std::string> w = words(2);
		if (i == g.bSpecies) w = words((size_t)g.dex + 2, { "-", "Bulbasaur", "Ivysaur", "Venusaur", "Charmander" });
		else if (i == g.bMoves) w = words(MOVES, { "-", "Pound", "Karate Chop" });
		else if (i == g.bLocations) w = words(ZONES, { "Nowhere", "New Bark Town", "Twinleaf Town" });
		else if (engine)
		{
			// (hg-engine's banks are found by what they say: rom.cpp hgEngineBanks)
			const std::string desc = "A description long enough to be one";
			if (i == g.bMoves + 1) w = words(MOVES, { "-", desc });
			else if (i == 30) w = words(4, { "-", "Stench", "Drizzle" });
			else if (i == 31) w = words(4, { "-", desc });
			else if (i == 40) w = words(8, { "-", "Master Ball", "Ultra Ball" });
			else if (i == 41) w = words(8, { "-", desc });
			else if (i == 50) w = words(25, { "Hardy", "Lonely" });
			else if (i == 69) w = words(4, { "-", "Ann", "Bob" });
			else if (i == 70) w = words(4, { "-", "Trainer", "Youngster", "Lass" });
			else if (i == 80) w = words((size_t)g.dex + 2, { "-", "Seed Pok\xc3\xa9mon", "B", "C", "Lizard Pok\xc3\xa9mon" });
			else if (i == 90) w = words(494, { "-", "A strange seed was planted on its back at birth" });
		}
		text.members.push_back(g.gen == 4 ? bank4(w) : bank5(w));
	}
	fs[g.text] = text;
	fs[g.personal] = narcOf((size_t)g.dex + 2, g.personalSize);
	fs[g.learn] = narcOf(4, 16);
	fs[g.evo] = narcOf(4, 42);
	fs[g.moves] = narcOf(4, 36);
	fs[g.items] = narcOf(4, 36);
	fs[g.enc] = narcOf(ZONES, g.encSize);
	fs[g.trdata] = narcOf(4, 20);
	fs[g.trpoke] = narcOf(4, 32);
	if (!strcmp(g.code, "IPKJ")) { fs["extra/new_move_data.narc"] = narcOf(4, 40); fs["data/pokepic_m.narc"] = narcOf(16, 0); }
	// the ARM9: the TM list (100 or 101 different moves, then 0) and Gen IV's map headers (as many as mapname.bin's names)
	arm9.assign(0x40, 0);
	for (int k = 1; k <= (g.gen == 4 ? 100 : 101); k++) add16(arm9, (uint16_t)k);
	add16(arm9, 0);
	while (arm9.size() % 4) arm9.push_back(0);
	const bool hg = g.code[0] == 'I' && g.code[1] == 'P';
	if (g.gen == 4)
	{
		File mn; mn.raw.assign(16 * ZONES, 0);
		fs["fielddata/maptable/mapname.bin"] = mn;
		for (size_t z = 0; z < ZONES; z++)
		{
			Bytes e(24, 0);
			if (hg) { e[0] = (uint8_t)z; put16(e, 12, 1100); put16(e, 14, 1100); }
			else { put16(e, 14, (uint16_t)z); put16(e, 10, 1100); put16(e, 12, 1100); }
			e[18] = (uint8_t)z;
			arm9.insert(arm9.end(), e.begin(), e.end());
		}
	}
	else
	{
		File zh; zh.isNarc = true; zh.members.assign(1, Bytes(48 * ZONES, 0));
		for (size_t z = 0; z < ZONES; z++) { put16(zh.members[0], 48 * z + 20, (uint16_t)z); zh.members[0][48 * z + 26] = (uint8_t)z; }
		fs["a/0/1/2"] = zh;
	}
	// the town map (townmap.cpp: its graphics, the map matrix, Gen IV's town map blocks and warps, Gen V's places)
	auto members = [](std::map<size_t, Bytes> m, size_t n) { File f; f.isNarc = true; f.members.resize(n); for (auto& e : m) f.members[e.first] = e.second; return f; };
	if (g.gen == 5)
	{
		const bool b2 = g.code[2] == 'E' || g.code[2] == 'D';
		fs[b2 ? "a/0/8/4" : "a/0/8/5"] = members({ { 0, nclr(256) }, { 5, ncgr(false, 64) }, { 7, ncgr(false, 64) }, { 16, nscr(32, 24, 64) }, { 17, nscr(32, 24, 64) } }, 18);
		Bytes places;
		for (int i = 0; i < 24; i++) { Bytes e(54, 0); put16(e, 0, (uint16_t)(i % ZONES)); put16(e, 4, (uint16_t)(20 + 9 * i)); put16(e, 6, (uint16_t)(30 + 5 * i)); put16(e, 22, i % 2); places.insert(places.end(), e.begin(), e.end()); }
		fs[b2 ? "a/0/8/5" : "a/0/8/6"] = members({ { 0, places } }, 1);
		fs["a/0/0/9"] = members({ { 0, matrix(8, 6, true, ZONES) } }, 1);
	}
	else
	{
		Bytes events;
		add32(events, 1); events.resize(events.size() + 20); add32(events, 1); events.resize(events.size() + 32);
		add32(events, 2); for (int i = 0; i < 2; i++) { add16(events, (uint16_t)(3 + i)); add16(events, 5); add16(events, (uint16_t)i); add16(events, 0); add32(events, 0); }
		if (hg)
		{
			fs["a/1/4/4"] = members({ { 20, nclr(256) }, { 10, ncgr(true, 64) }, { 11, nscr(64, 24, 64) } }, 21);
			fs["a/0/4/1"] = members({ { 0, matrix(30, 20, false, ZONES) } }, 1);
			fs["a/0/3/2"] = members({ { 0, events }, { 1, events } }, ZONES);
		}
		else
		{
			fs["graphic/tmap_gra.narc"] = members({ { 0, nclr(256) }, { 19, ncgr(false, 64) }, { 22, nscr(32, 24, 64) }, { 24, nscr(32, 24, 64) } }, 25);
			fs["fielddata/mapmatrix/map_matrix.narc"] = members({ { 0, matrix(30, 20, false, ZONES) } }, 1);
			File bl;
			add32(bl.raw, 6);
			for (int i = 0; i < 6; i++) { Bytes e(24, 0); put16(e, 0, (uint16_t)(i * 3)); put16(e, 2, (uint16_t)(i * 2)); bl.raw.insert(bl.raw.end(), e.begin(), e.end()); }
			fs["data/tmap_block.dat"] = bl;
			fs[!strcmp(g.code, "CPUE") ? "fielddata/eventdata/zone_event.narc" : "fielddata/eventdata/zone_event_release.narc"] = members({ { 0, events } }, ZONES);
		}
		// Gen IV's sprites: per species back (female, male), front (female, male), normal and shiny palette; hg-engine's
		// 8 a species, the front and its palette first (here: species 1's and 3's)
		const char* sprites = engine ? "data/pokepic_m.narc" : hg ? "a/0/0/4" : !strcmp(g.code, "CPUE") ? "poketool/pokegra/pl_pokegra.narc" : "poketool/pokegra/pokegra.narc";
		File sp; sp.isNarc = true;
		const size_t per = engine ? 8 : 6;
		sp.members.resize(per * 4);
		for (size_t s : { (size_t)1, (size_t)3 })
			for (size_t k = 0; k < per; k++)
				sp.members[per * s + k] = engine ? (k == 0 ? ncgr(false, 200, 10, 20) : k == 1 || k == 5 ? nclr(16) : Bytes{})
					: (k == 2 || k == 3 ? ncgr(false, 200, 10, 20) : k >= 4 ? nclr(16) : Bytes{});
		fs[sprites] = sp;
	}
	return fs;
}

// the cartridge image: header, ARM9, overlay table, FNT, FAT, then the files (named ones by directory, then the
// overlays')
static Bytes image(const char* code, const Bytes& arm9, const Bytes& ovt, std::map<std::string, File>& fs, const std::vector<Bytes>& overlays, bool ovtGiven)
{
	Bytes rom(0x200, 0);
	memcpy(&rom[0], "POKEMON FUZZ", 12);
	memcpy(&rom[12], code, 4);
	put32(rom, 0x20, 0x200); put32(rom, 0x28, 0x02000000); put32(rom, 0x2c, (uint32_t)arm9.size());
	rom.insert(rom.end(), arm9.begin(), arm9.end());
	// directories: each one's files, then its subdirectories
	struct Dir { std::vector<std::string> files; std::vector<std::pair<std::string, size_t>> dirs; };
	std::vector<Dir> dirs(1);
	std::map<std::string, size_t> dirId = { { "", 0 } };
	for (auto& f : fs)
	{
		size_t parent = 0, at = 0;
		const std::string& p = f.first;
		for (size_t s; (s = p.find('/', at)) != std::string::npos; at = s + 1)
		{
			std::string d = p.substr(0, s);
			auto it = dirId.find(d);
			if (it == dirId.end())
			{
				it = dirId.emplace(d, dirs.size()).first;
				dirs.push_back({});
				dirs[parent].dirs.push_back({ p.substr(at, s - at), it->second });
			}
			parent = it->second;
		}
		dirs[parent].files.push_back(p);
	}
	std::vector<const Bytes*> content;
	std::vector<Bytes> built;
	built.reserve(fs.size());
	Bytes fnt(8 * dirs.size(), 0), sub;
	std::vector<uint16_t> first(dirs.size());
	for (size_t d = 0; d < dirs.size(); d++)
	{
		first[d] = (uint16_t)content.size();
		for (auto& p : dirs[d].files)
		{
			File& f = fs[p];
			if (f.isNarc) { built.push_back(narc(f.members)); content.push_back(&built.back()); }
			else content.push_back(&f.raw);
		}
	}
	for (size_t d = 0; d < dirs.size(); d++)
	{
		put32(fnt, 8 * d, (uint32_t)(8 * dirs.size() + sub.size()));
		put16(fnt, 8 * d + 4, first[d]);
		put16(fnt, 8 * d + 6, d == 0 ? (uint16_t)dirs.size() : 0xf000);
		for (auto& p : dirs[d].files)
		{
			std::string n = p.substr(p.rfind('/') == std::string::npos ? 0 : p.rfind('/') + 1);
			sub.push_back((uint8_t)n.size()); sub.insert(sub.end(), n.begin(), n.end());
		}
		for (auto& s : dirs[d].dirs)
		{
			sub.push_back((uint8_t)(0x80 | s.first.size())); sub.insert(sub.end(), s.first.begin(), s.first.end());
			sub.push_back((uint8_t)s.second); sub.push_back(0xf0 | (uint8_t)(s.second >> 8));
		}
		sub.push_back(0);
	}
	fnt.insert(fnt.end(), sub.begin(), sub.end());
	// the overlays: unnamed files after the named ones, and the table that lists them (unless the input gave one)
	const uint16_t ovFirst = (uint16_t)content.size();
	for (auto& o : overlays) content.push_back(&o);
	Bytes table = ovt;
	if (!ovtGiven)
		for (size_t k = 0; k < overlays.size(); k++)
		{
			Bytes e(32, 0);
			put32(e, 0, (uint32_t)k); put32(e, 4, 0x02200000 + 0x10000 * (uint32_t)k); put32(e, 8, (uint32_t)overlays[k].size());
			put32(e, 24, ovFirst + (uint32_t)k);
			table.insert(table.end(), e.begin(), e.end());
		}
	put32(rom, 0x50, (uint32_t)rom.size()); put32(rom, 0x54, (uint32_t)table.size());
	rom.insert(rom.end(), table.begin(), table.end());
	put32(rom, 0x40, (uint32_t)rom.size()); put32(rom, 0x44, (uint32_t)fnt.size());
	rom.insert(rom.end(), fnt.begin(), fnt.end());
	const size_t fatAt = rom.size();
	put32(rom, 0x48, (uint32_t)fatAt); put32(rom, 0x4c, (uint32_t)(8 * content.size()));
	rom.resize(fatAt + 8 * content.size());
	for (size_t i = 0; i < content.size(); i++)
	{
		while (rom.size() % 4) rom.push_back(0xff);
		put32(rom, fatAt + 8 * i, (uint32_t)rom.size());
		rom.insert(rom.end(), content[i]->begin(), content[i]->end());
		put32(rom, fatAt + 8 * i + 4, (uint32_t)rom.size());
	}
	return rom;
}

static void write(const std::string& p, const Bytes& b)
{
	if (FILE* f = fopen(p.c_str(), "wb")) { fwrite(b.data(), 1, b.size(), f); fclose(f); }
}

static Bytes readAll(const std::string& p)
{
	Bytes b;
	if (FILE* f = fopen(p.c_str(), "rb"))
	{
		uint8_t buf[65536];
		size_t k;
		while ((k = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + k);
		fclose(f);
	}
	return b;
}

static void patch(Bytes& b, size_t at, const std::string& s)
{
	at = b.empty() ? 0 : at % (b.size() + 1);
	if (b.size() < at + s.size()) b.resize(at + s.size());
	if (!s.empty()) memcpy(b.data() + at, s.data(), s.size());
}

static void use(const std::shared_ptr<RomData>& r)
{
	if (!r) return;
	int got = 0;
	for (uint16_t sp : { 1, 3, 25, 201, 386, 487 })
		for (uint8_t form : { 0, 1, 2 }) { got += r->sprite(sp, false, form) != nullptr; got += r->sprite(sp, true, form) != nullptr; }
	if (getenv("BANANA_FUZZ_DEBUG")) fprintf(stderr, "  %d sprites\n", got);
	ramHints(*r);
	for (auto& s : r->species) speciesIndex(*r, (uint16_t)(&s - &r->species[0]), 1);
}

// the input as the cartridge image itself: the file system and every file in it
static void raw(const uint8_t* data, size_t size)
{
	nitro::Rom rom;
	if (!rom.openMemory(Bytes(data, data + size))) return;
	Bytes b;
	for (size_t i = 0; i < rom.fileCount() && i < 64; i++)
	{
		if (rom.read((int)i, b))
		{
			nitro::Narc n;
			n.parse(b);
			Bytes o;
			nitro::lzDecompress(b.data(), b.size(), o);
			nitro::blzDecompress(b, o);
			nitro::gen4Text(b.data(), b.size());
			nitro::gen5Text(b.data(), b.size());
		}
	}
	for (auto& p : rom.paths())
	{
		nitro::NarcFile nf;
		if (nf.open(rom, p.first)) for (size_t i = 0; i < nf.size() && i < 64; i++) nf[i];
	}
	rom.arm9(b);
	for (int i = 0; i < 8; i++) rom.overlay9(i, b);
	rom.overlays9();
}

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
	struct stat st;
	dir = (stat("/dev/shm", &st) == 0 ? "/dev/shm" : "/tmp") + std::string("/banana-fuzz-nds-") + std::to_string(getpid());
	mkdir(dir.c_str(), 0755);
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	if (size < 2) return 0;
	const size_t gi = data[0] % (NGAMES + 1);
	const bool cache = data[1] & 1;
	if (gi == NGAMES) { raw(data + 2, size - 2); return 0; }
	const Made& g = GAMES[gi];
	FuzzedDataProvider fd(data + 2, size - 2);
	Bytes arm9, ovt;
	auto fs = skeleton(g, arm9);
	std::vector<Bytes> overlays;
	bool ovtGiven = false;
	// the input's changes: a slot (a path, an overlay, the ARM9, the overlay table), how, where, and the bytes
	const unsigned edits = fd.ConsumeIntegralInRange<unsigned>(0, 24);
	for (unsigned k = 0; k < edits && fd.remaining_bytes(); k++)
	{
		const size_t slot = fd.ConsumeIntegralInRange<size_t>(0, NPATHS + OVERLAYS + 1);
		const uint8_t how = fd.ConsumeIntegral<uint8_t>() % 3;	// 0 replace a member, 1 patch a member, 2 the whole file
		const uint16_t which = fd.ConsumeIntegral<uint16_t>(), at = fd.ConsumeIntegral<uint16_t>();
		const std::string s = fd.ConsumeRandomLengthString(2048);
		if (slot == NPATHS + OVERLAYS) { if (how == 2) arm9.assign(s.begin(), s.end()); else patch(arm9, at, s); continue; }
		if (slot == NPATHS + OVERLAYS + 1) { ovtGiven = true; if (how == 2) ovt.assign(s.begin(), s.end()); else patch(ovt, at, s); continue; }
		if (slot >= NPATHS)
		{
			const size_t o = slot - NPATHS;
			if (overlays.size() <= o) overlays.resize(o + 1);
			if (how == 2) overlays[o].assign(s.begin(), s.end()); else patch(overlays[o], at, s);
			continue;
		}
		File& f = fs[PATHS[slot]];
		if (how == 2 || (!f.isNarc && !f.raw.empty())) { f.isNarc = false; if (how == 2) f.raw.assign(s.begin(), s.end()); else patch(f.raw, at, s); continue; }
		f.isNarc = true;
		const size_t m = which % (f.members.size() + 1);
		if (m == f.members.size()) f.members.push_back({});
		if (how == 0) f.members[m].assign(s.begin(), s.end());
		else patch(f.members[m], at, s);
	}
	const std::string path = dir + "/game.nds", cacheDir = dir + "/cache";
	write(path, image(g.code, arm9, ovt, fs, overlays, ovtGiven));
	RomLoadInfo info;
	std::string err;
	auto r = loadRom(path, cache ? cacheDir : "", &err, &info);
	if (getenv("BANANA_FUZZ_DEBUG")) fprintf(stderr, "%s: %s (%zu zones, %zu trainers, %zu maps)\n", g.code, r ? "read" : err.c_str(),
		r ? r->zones.size() : 0, r ? r->trainers.size() : 0, r ? r->regionMaps.size() : 0);
	if (getenv("BANANA_FUZZ_DEBUG") && r && r->regionMaps.empty())
	{
		// (why the town map wasn't read: the harness's own files checked)
		struct Copy : RomData { std::shared_ptr<const Sprite> sprite(uint16_t, bool, uint8_t) const override { return nullptr; } } c;
		static_cast<RomData&>(c) = *r;
		nitro::Rom rom;
		Bytes a9, img = readAll(path);
		rom.openMemory(img);
		rom.arm9(a9);
		Gen4Headers h;
		gen4MapHeaders(r->game.id, a9, 4, {}, r->locationNames.size(), 4, h);
		std::string why;
		townmap::read(rom, a9, h.at, c, &why);
		fprintf(stderr, "  town map: %s\n", why.c_str());
	}
	use(r);
	if (cache && r && !info.cacheFile.empty())
	{
		// the cache on the card, changed (a write cut short, another version's, a card gone bad) and read again
		Bytes c = readAll(info.cacheFile);
		while (fd.remaining_bytes() && !c.empty())
		{
			const uint32_t at = fd.ConsumeIntegral<uint32_t>();
			const uint8_t how = fd.ConsumeIntegral<uint8_t>();
			if (how < 16) c.resize(at % c.size() + 1);
			else c[at % c.size()] ^= (uint8_t)(how * 37);
		}
		write(info.cacheFile, c);
		use(loadRom(path, cacheDir, nullptr, &info));
		unlink(info.cacheFile.c_str());
	}
	return 0;
}
