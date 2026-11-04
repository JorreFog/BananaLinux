// The pokeemerald-expansion ROM hacks (src/gba/expansion.cpp) on the player's own ROMs: what each build's tables and
// RAM addresses turned out to be, facts every one must show, the times, and a few sprites as PNGs for a look. A ROM
// that isn't there is skipped (the smol decoder's own check runs always).
//   make tests/gba-exp-test && tests/gba-exp-test [hacks-dir [png-dir]]   (or GBA_HACKS=<dir>; default ~/gba-companion/hacks)
//   DUMP=1 prints every layout field and check.
#include "gba/expansion.h"
#include "gba/gen3rom.h"
#include "gba/hacks.h"
#include "nds/rom.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

using namespace banana;
using namespace banana::nds;

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define EQ(a, b) do { checks++; auto _a = (a); auto _b = (b); if (!(_a == _b)) { fails++; fprintf(stderr, "FAIL %s:%d: %s == %s (%s vs %s)\n", __FILE__, __LINE__, #a, #b, str(_a).c_str(), str(_b).c_str()); } } while (0)
static std::string str(const std::string& s) { return "\"" + s + "\""; }
static std::string str(const char* s) { return str(std::string(s)); }
template <class T> static std::string str(T v) { return std::to_string((long long)v); }
static double msSince(std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }

static std::vector<uint8_t> readFile(const std::string& p)
{
	std::ifstream f(p, std::ios::binary);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static void png(const std::string& path, const Sprite& s, bool shiny)
{
	std::vector<uint8_t> rgba((size_t)s.w * s.h * 4);
	const auto& pal = shiny ? s.shiny : s.normal;
	for (int i = 0; i < s.w * s.h; i++)
	{
		uint8_t c = s.frames[0][(size_t)i];
		uint32_t v = pal[c];
		rgba[4 * i] = (uint8_t)(v >> 16); rgba[4 * i + 1] = (uint8_t)(v >> 8); rgba[4 * i + 2] = (uint8_t)v; rgba[4 * i + 3] = c ? 255 : 0;
	}
	stbi_write_png(path.c_str(), s.w, s.h, 4, rgba.data(), s.w * 4);
}

// what one hack must show
struct Expect
{
	const char* dir; const char* file; const char* key;
	int major, minor;
	const char* fairyName; int fairyIndex;		// a Fairy species by its RomData index
	uint32_t party, partyCount, battleFlags, battleMons, gMain, sb1, sb2;	// the RAM found (by hand from the code, 2026-11-03)
	int monSize, battlerSize;
	const char* firstLeader;					// the first gym leader's name in the trainer table
	bool pikachuEvolves;
};
static const Expect HACKS[] = {
	{ "heart&soul (v2.0.6)", "heart&soul (v2.0.6).gba", "heart-and-soul", 1, 15, "Clefairy", 35,
	  0x02034764, 0x020342a4, 0x020000ac, 0x02000420, 0x03005bd8, 0x030041d8, 0x030041d4, 100, 0x88, "Falkner", true },
	{ "lazarus-v2", "lazarus-v2.gba", "lazarus", 1, 9, "", 0,
	  0x0201b960, 0x0201b95d, 0x020002c0, 0x02000360, 0x030014b8, 0x03003664, 0x03003668, 100, 0x60, "Roxanne", true },
	{ "seaglass v3.0", "seaglass v3.0.gba", "seaglass", 1, 9, "", 0,
	  0x02019c20, 0x02019c1d, 0x020002c0, 0x02000360, 0x030014b4, 0x030051b8, 0x030051bc, 100, 0x60, "Roxanne", true },
	{ "Soulgold (v1.1.4)", "Soulgold (v1.1.4).gba", "soulgold", 1, 15, "Clefairy", 35,
	  0x0203901c, 0x02038dd5, 0x02001ab4, 0x02001ecc, 0x030055c0, 0x030040c4, 0x030040c0, 96, 0x98, "Falkner", true },
};

static void testHack(const std::string& dir, const Expect& x, const std::string& pngDir)
{
	const std::string path = dir + "/" + x.dir + "/" + x.file;
	std::vector<uint8_t> b = readFile(path);
	if (b.empty()) { fprintf(stderr, "skip %s (not found)\n", path.c_str()); return; }
	fprintf(stderr, "== %s\n", x.key);
	auto t0 = std::chrono::steady_clock::now();
	gba::ExpansionLayout L;
	const bool ok = gba::findExpansion(b.data(), b.size(), L);
	const double tFind = msSince(t0);
	CHECK(ok);
	if (!ok) { fprintf(stderr, "  not found: %s\n", L.why.c_str()); return; }
	EQ(L.major, x.major); EQ(L.minor, x.minor);
	if (getenv("DUMP"))
	{
		fprintf(stderr, "  %d.%d.%d moves %d species %d abilities %d items %d\n", L.major, L.minor, L.patch, L.numMoves, L.numSpecies, L.numAbilities, L.numItems);
		fprintf(stderr, "  species %lx stride %x name %x cat %x nat %x h %x desc %x front %x pal %x shiny %x learn %x teach %x egg %x evo %x (%d) slots %d\n",
			L.species, L.spStride, L.spName, L.spCategory, L.spNatDex, L.spHeight, L.spDesc, L.spFront, L.spPal, L.spShiny, L.spLearn, L.spTeach, L.spEgg, L.spEvo, L.evoStride, L.abilitySlots);
		fprintf(stderr, "  moves %lx stride %x | items %lx stride %x namePtr %d pocket byte %x >>%d | abilities %lx stride %x\n",
			L.moves, L.mvStride, L.items, L.itStride, (int)L.itNamePtr, L.itPocketByte, L.itPocketShift, L.abilities, L.abStride);
		fprintf(stderr, "  trainers %lx stride %x n %d mon %x | classes %lx n %d | wild %lx stride %d times %d kinds %d\n",
			L.trainers, L.trStride, L.numTrainers, L.tmonStride, L.classes, L.numClasses, L.wild, L.wildStride, L.wildTimes, L.wildKinds);
		fprintf(stderr, "  save: flags %x dex %x/%u pc %x bag %d %d %d %d %d clear %x badge %x\n", L.flagsOffset, L.dexSeenOffset, L.dexCount,
			L.pcItemsOffset, L.bagCount[0], L.bagCount[1], L.bagCount[2], L.bagCount[3], L.bagCount[4], L.gameClearFlag, L.badgeFlag);
		fprintf(stderr, "  ram: party %x count %x enemy %x size %d | battle mons %x size %x | flags %x | main %x | sb1 %x sb2 %x pos %d | map header %x\n",
			L.party, L.partyCount, L.enemyParty, L.monSize, L.battleMons, L.battlerSize, L.battleFlags, L.gMain, L.sb1, L.sb2, L.sb1Pos, L.mapHeader);
	}
	for (auto& c : L.checks) fprintf(stderr, "  check: %s\n", c.c_str());
	fprintf(stderr, "  times: %s\n", L.times.c_str());
	// the RAM: as found by hand from the code (dis.py on each ROM: HealPlayerParty, Cmd_end, the 0x439 reads...)
	EQ(L.party, x.party); EQ(L.partyCount, x.partyCount); EQ(L.enemyParty, x.party + 6u * (uint32_t)x.monSize);
	EQ(L.monSize, x.monSize); EQ(L.battleFlags, x.battleFlags); EQ(L.battleMons, x.battleMons); EQ(L.battlerSize, x.battlerSize);
	EQ(L.gMain, x.gMain); EQ(L.sb1, x.sb1); EQ(L.sb2, x.sb2);
	CHECK(L.mapHeader >> 24 == 2);

	// the companion's side
	t0 = std::chrono::steady_clock::now();
	std::shared_ptr<const pkmn::GameProfile> prof;
	pkmn::RomTables t;
	CHECK(gba::expansionProfile(b.data(), b.size(), prof, t));
	const double tProfile = msSince(t0);
	if (!prof) return;
	EQ(prof->party, x.party); EQ(prof->mon.size, x.monSize); EQ(prof->mon.battler.size, x.battlerSize);
	CHECK((bool)prof->mon.decode);
	// a party Pokémon as this build keeps it (made here from the layouts in expansion.cpp's notes), and a battler
	{
		std::vector<uint8_t> p((size_t)x.monSize, 0);
		auto put16 = [&](size_t o, uint16_t v) { p[o] = (uint8_t)v; p[o + 1] = (uint8_t)(v >> 8); };
		auto put32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; i++) p[o + i] = (uint8_t)(v >> (8 * i)); };
		const uint32_t pid = 0x12345679u, ot = 0xabcd1234u;		// (pid % 24 = 17: an order other than the first)
		put32(0, pid); put32(4, ot);
		const uint8_t nick[5] = { 0xbb, 0xbc, 0xbd, 0xbe, 0xff };	// "ABCD"
		memcpy(p.data() + 8, nick, 5);
		const int species = 1300, item = 600, lv = 37;
		const uint16_t moves[4] = { 33, 45, 585, 802 };
		if (x.monSize == 100)
		{
			uint8_t part[4][12] = {};
			put16(0x50, 0); p[0x54] = lv; put16(0x56, 77); put16(0x58, 99);
			part[0][0] = (uint8_t)species; part[0][1] = (uint8_t)(species >> 8);
			part[0][2] = (uint8_t)item; part[0][3] = (uint8_t)(item >> 8);
			part[0][4] = 0x10; part[0][5] = 0x27; part[0][9] = 200;	// (experience 10000, friendship)
			for (int i = 0; i < 4; i++) { part[1][2 * i] = (uint8_t)moves[i]; part[1][2 * i + 1] = (uint8_t)(moves[i] >> 8); part[1][8 + i] = (uint8_t)(10 + i); }
			part[2][0] = 252;
			const uint32_t iv = 31 | 30u << 5 | 1u << 25;
			for (int i = 0; i < 4; i++) part[3][4 + i] = (uint8_t)(iv >> (8 * i));
			part[3][11] = 0x40;		// (ability number 2: bits 29-30 of the third word)
			int order[4] = { 0, 1, 2, 3 };
			for (uint32_t k = pid % 24; k > 0; k--) std::next_permutation(order, order + 4);
			uint8_t d[48];
			for (int pos = 0; pos < 4; pos++) memcpy(d + 12 * pos, part[order[pos]], 12);
			uint16_t sum = 0;
			for (int i = 0; i < 48; i += 2) sum = (uint16_t)(sum + (d[i] | d[i + 1] << 8));
			put16(0x1c, sum);
			for (int i = 0; i < 48; i += 4)
			{
				uint32_t v = (uint32_t)(d[i] | d[i + 1] << 8 | d[i + 2] << 16 | (uint32_t)d[i + 3] << 24) ^ pid ^ ot;
				put32(0x20 + (size_t)i, v);
			}
		}
		else
		{
			put16(0x20, species); put16(0x22, item); put32(0x24, 10000); p[0x2b] = 200;
			for (int i = 0; i < 4; i++) { put16(0x2c + 2 * (size_t)i, moves[i]); p[0x34 + (size_t)i] = (uint8_t)(10 + i); }
			p[0x38] = 252; put32(0x48, 31 | 30u << 5 | 1u << 25); p[0x33] |= 2 << 4;
			p[0x50] = lv; put16(0x52, 77); put16(0x54, 99);
		}
		pkmn::Mon m;
		CHECK(pkmn::decodeParty(p.data(), m, prof->mon));
		EQ((int)m.species, species); EQ((int)m.item, item); EQ((int)m.level, lv); EQ((int)m.hp, 77); EQ((int)m.maxHp, 99);
		EQ((int)m.moves[2], 585); EQ((int)m.moves[3], 802); EQ((int)m.pp[3], 13); EQ((int)m.exp, 10000); EQ((int)m.friendship, 200);
		EQ((int)m.ivs[0], 31); EQ((int)m.ivs[1], 30); EQ((int)m.ivs[5], 1); EQ((int)m.evs[0], 252); EQ((int)m.abilitySlot, 2); CHECK(!m.egg);
		EQ(m.nick, std::string("ABCD"));
		if (x.monSize == 100) { p[0x30] ^= 1; CHECK(!pkmn::decodeParty(p.data(), m, prof->mon)); }	// (a bad checksum: not taken)
		std::vector<uint8_t> bt((size_t)x.battlerSize, 0);
		bt[0] = (uint8_t)species; bt[1] = (uint8_t)(species >> 8);
		bt[0x0c] = 33; bt[0x22] = 19; bt[0x23] = 14; bt[0x25] = 35; bt[0x2a] = 50; bt[0x2c] = lv; bt[0x2e] = 60;
		CHECK(pkmn::decodeBattler(bt.data(), m, 2000, prof->mon.battler));
		EQ((int)m.types[0], (int)pkmn::FAIRY); EQ((int)m.types[1], (int)pkmn::ELECTRIC); EQ((int)m.hp, 50); EQ((int)m.maxHp, 60); EQ((int)m.level, lv);
		// the fields after the item where the build keeps them (vanilla 1.9/1.15 layout): the experience, personality,
		// status and nickname
		{
			std::vector<uint8_t> c = bt;
			auto put32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; i++) c[at + i] = (uint8_t)(v >> (8 * i)); };
			const uint8_t nick[] = { 0xbd, 0xdc, 0xd9, 0xe7, 0xe4, 0xdd, 0xe2, 0xff };	// "Chespin"
			memcpy(c.data() + 0x32, nick, sizeof nick);
			for (int i = 0; i < 8; i++) c[0x40 + i] = i ? 0xff : 0xbb;		// (the OT name "A")
			c[0x2c] = 5; put32(0x48, 135); put32(0x4c, 0x5e880f10u); put32(0x50, 0);
			CHECK(pkmn::decodeBattler(c.data(), m, 2000, prof->mon.battler));
			EQ((int)m.exp, 135); EQ(m.pid, 0x5e880f10u); EQ((int)m.status, 0); EQ(m.nick, std::string("Chespin"));
		}
		// SoulGold 1.1.4's 0x98-byte battlers as read from its RAM (0x2001ecc) in Crystal's first battle on the RG DS Plus
		// 2026-11-04: the nickname 2 bytes on, the OT name/experience/personality/status 4 bytes on. Chespin Lv 5 135 exp
		// and Marill Lv 4 51 exp, neither with a status (a misread showed "ë Chespin" and Sleep on both)
		if (x.battlerSize == 0x98)
		{
			static const uint8_t sg[2][0x58] = {
				{ 0x8a,0x02,0x0c,0x00,0x0c,0x00,0x09,0x00,0x0b,0x00,0x09,0x00,0x21,0x00,0x2d,0x00, 0x00,0x00,0x00,0x00,0x46,0x63,0xc6,0x01,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,
				  0x41,0x00,0x0d,0x0d,0x0a,0x23,0x28,0x00,0x00,0x0e,0x14,0x00,0x05,0x32,0x14,0x00, 0x00,0x00,0x1d,0x00,0xbd,0xdc,0xd9,0xe7,0xe4,0xdd,0xe2,0xff,0x3f,0x00,0x00,0x00,
				  0x02,0x00,0xbb,0xd5,0xff,0xff,0xff,0xff,0xff,0xff,0x24,0x08,0x87,0x00,0x00,0x00, 0x6c,0x84,0x6d,0x5d,0x00,0x00,0x00,0x00 },
				{ 0xb7,0x00,0x06,0x00,0x09,0x00,0x08,0x00,0x06,0x00,0x09,0x00,0x01,0x00,0x27,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,
				  0x2f,0x00,0x0c,0x13,0x0a,0x23,0x1e,0x00,0x00,0x0e,0x13,0x00,0x04,0x00,0x13,0x00, 0x00,0x00,0x7a,0x00,0xc7,0xd5,0xe6,0xdd,0xe0,0xe0,0xff,0x00,0x3f,0x00,0x00,0x00,
				  0x02,0x00,0xbb,0xd5,0xff,0xff,0xff,0xff,0xff,0xff,0x24,0x08,0x33,0x00,0x00,0x00, 0xbc,0xc7,0xec,0xf2,0x00,0x00,0x00,0x00 } };
			const char* names[2] = { "Chespin", "Marill" };
			const int lvs[2] = { 5, 4 }, exps[2] = { 135, 51 }, hps[2] = { 20, 19 };
			for (int k = 0; k < 2; k++)
			{
				std::vector<uint8_t> c((size_t)x.battlerSize, 0);
				memcpy(c.data(), sg[k], sizeof sg[k]);
				CHECK(pkmn::decodeBattler(c.data(), m, 2000, prof->mon.battler));
				EQ((int)m.level, lvs[k]); EQ((int)m.hp, hps[k]); EQ((int)m.exp, exps[k]); EQ((int)m.status, 0); EQ(m.nick, std::string(names[k])); EQ((int)m.ppBonuses, 0);
			}
		}
	}
	// the profile's save layout: the bag after the PC's items, the Pokédex in SaveBlock1
	EQ(prof->save.pocket[0], L.pcItemsOffset + 4u * L.pcItemsCount);
	EQ(prof->save.dexSb1, L.dexSeenOffset);
	EQ(prof->mainFlags, x.gMain + 0x439);
	t.rom = std::make_shared<const std::vector<uint8_t>>(b);

	// pkmn::Companion on the file: nothing asked for until the thread has made the build's profile, then this build's
	// addresses (the party at the one found, the battle flags at gMain + 0x439)
	{
		pkmn::Companion c;
		c.wantExt(true);	// (the save's data asked for too, as the pages do)
		CHECK(c.open(path));
		std::string req;
		for (int i = 0; i < 400 && req.empty(); i++) { req = c.requests(1e9 + i * 300.0); if (req.empty()) usleep(20000); }
		char want[64];
		snprintf(want, sizeof want, "READ_CORE_MEMORY %x %d\n", x.party, 6 * x.monSize);
		CHECK(req.find(want) != std::string::npos);
		snprintf(want, sizeof want, "READ_CORE_MEMORY %x 1\n", x.gMain + 0x439);
		CHECK(req.find(want) != std::string::npos);
		CHECK(c.game() && c.game()->party == x.party);
		// (both save block pointers in one read from the lower: Heart & Soul keeps SaveBlock2's before SaveBlock1's)
		const uint32_t lo = std::min(x.sb1, x.sb2), d = x.sb1 > x.sb2 ? x.sb1 - x.sb2 : x.sb2 - x.sb1;
		if (d <= 0x40) { snprintf(want, sizeof want, "READ_CORE_MEMORY %x %u\n", lo, d + 4); CHECK(req.find(want) != std::string::npos); if (req.find(want) == std::string::npos) fprintf(stderr, "  asked: %s", req.c_str()); }
		if (req.find(want) == std::string::npos) fprintf(stderr, "  companion asked: %s\n", req.c_str());
	}

	// the pages' RomData
	t0 = std::chrono::steady_clock::now();
	nds::Game g;
	g.id = GameId::HACK; g.gen = Gen::III; g.code = "BPEE"; g.hack = x.key; g.title = x.key; g.modern = true;
	auto rd = gba::readExpansionRomData(t, g);
	const double tRom = msSince(t0);
	CHECK(rd != nullptr);
	if (!rd) return;
	const RomData& r = *rd;
	fprintf(stderr, "  read: layout %.0f ms, profile+tables %.0f ms, RomData %.0f ms; %zu species (dex %d), %zu moves, %zu items, %zu abilities, %zu zones, %zu trainers, %zu place names\n",
		tFind, tProfile, tRom, r.species.size(), r.game.dexSize, r.moves.size(), r.items.size(), r.abilities.size(), r.zones.size(), r.trainers.size(), r.locationNames.size());
	CHECK(r.game.modern);
	// species: Pikachu is Electric in every one of them (its RomData index is the game's own Pokédex number: 25, but
	// Seaglass's regional dex); its evolution (Lazarus: none - the hack's own)
	CHECK(r.species.size() > 400);
	size_t pika = 0;
	for (size_t i = 1; i < r.species.size() && !pika; i++) if (r.species[i].name == "Pikachu") pika = i;
	CHECK(pika != 0);
	if (pika)
	{
		EQ((int)r.species[pika].types[0], (int)ELECTRIC);
		CHECK(!r.species[pika].learnset.empty());
		CHECK(x.pikachuEvolves == !r.species[pika].evolutions.empty());
		if (!r.species[pika].evolutions.empty()) EQ((int)r.species[pika].evolutions[0].method, (int)evo::USE_ITEM);
		CHECK(!r.species[pika].dexText.empty());
		CHECK(r.species[pika].height > 0);
		fprintf(stderr, "  Pikachu: %s / %s, %d/%d/%d, ability %s, %zu level moves, %zu TMs, evolves: %s; \"%.50s\"\n", r.species[pika].category.c_str(),
			std::to_string((int)r.species[pika].types[0]).c_str(), r.species[pika].base[0], r.species[pika].base[1], r.species[pika].base[5],
			r.species[pika].abilities[0] < r.abilities.size() ? r.abilities[r.species[pika].abilities[0]].c_str() : "?", r.species[pika].learnset.size(),
			r.species[pika].tms.size(), r.species[pika].evolutions.empty() ? "-" : r.species[pika].evolutions[0].how.c_str(), r.species[pika].dexText.c_str());
	}
	// a Fairy
	int fairies = 0;
	for (auto& s : r.species) fairies += s.types[0] == FAIRY || s.types[1] == FAIRY;
	CHECK(fairies > 5);
	if (*x.fairyName && (size_t)x.fairyIndex < r.species.size())
	{
		EQ(r.species[(size_t)x.fairyIndex].name, std::string(x.fairyName));
		EQ((int)r.species[(size_t)x.fairyIndex].types[0], (int)FAIRY);
	}
	// moves and their own categories (the split): Pound physical, Ember special, Growl status, Moonblast Fairy
	if (r.moves.size() > 585)
	{
		EQ(r.moves[1].name, std::string("Pound")); EQ((int)r.moves[1].cat, (int)MoveCategory::PHYSICAL); EQ((int)r.moves[1].power, 40);
		EQ(r.moves[52].name, std::string("Ember")); EQ((int)r.moves[52].cat, (int)MoveCategory::SPECIAL); EQ((int)r.moves[52].type, (int)FIRE);
		EQ(r.moves[45].name, std::string("Growl")); EQ((int)r.moves[45].cat, (int)MoveCategory::STATUS);
		EQ(r.moves[98].name, std::string("Quick Attack")); EQ((int)(int8_t)r.moves[98].priority, 1);
		EQ((int)r.moves[585].type, (int)FAIRY); EQ((int)r.moves[585].cat, (int)MoveCategory::SPECIAL);
		EQ(r.moves[7].name, std::string("Fire Punch")); EQ((int)r.moves[7].cat, (int)MoveCategory::PHYSICAL);	// (Gen III: special by type)
		CHECK(!r.moves[1].description.empty());
	}
	// items in Gen IV's numbering where the names match
	CHECK(r.items.size() > 50 && r.items[4].name == "Poké Ball");
	CHECK(r.items.size() > 17 && r.items[17].name == "Potion");
	CHECK(r.items.size() > 328 && r.items[328].name.compare(0, 4, "TM01") == 0);
	CHECK(r.items.size() > 270 && r.items[270].name == "Life Orb");
	if (r.items.size() > 4) EQ((int)r.items[4].pocket, (int)POCKET_BALLS);
	if (r.items.size() > 328) EQ((int)r.items[328].pocket, (int)POCKET_TMS);
	CHECK(r.tmMoves[0] != 0);
	// abilities
	CHECK(r.abilities.size() > 250);
	if (r.abilities.size() > 2) { EQ(r.abilities[1], std::string("Stench")); CHECK(!r.abilityText[1].empty()); }
	EQ(r.natures.size(), (size_t)25);
	// places and the first wild zone with grass encounters
	int named = 0, withWild = 0;
	const Location* first = nullptr;
	for (auto& z : r.zones) { named += !z.second.name.empty(); if (!z.second.encounters.empty()) { withWild++; if (!first) first = &z.second; } }
	CHECK(named > 20); CHECK(withWild > 10);
	if (first)
	{
		fprintf(stderr, "  first wild zone %04x \"%s\":", first->id, first->name.c_str());
		for (auto& e : first->encounters) if (e.species < r.species.size()) fprintf(stderr, " %s %d-%d %d%%", r.species[e.species].name.c_str(), e.minLevel, e.maxLevel, e.percent);
		fprintf(stderr, "\n");
		for (auto& e : first->encounters) CHECK(e.species && e.species < r.species.size() && !r.species[e.species].name.empty() && e.minLevel >= 1 && e.maxLevel <= 100);
	}
	// the Map page: the game's picture (Lazarus, Seaglass kept Emerald's format) or the squares of its places, a map a
	// region (the Johto builds: Johto, then Kanto), and the starting town's place on its map
	{
		const std::string k = x.key;
		const bool pictured = k == "lazarus" || k == "seaglass";
		const char* home = k == "lazarus" ? "Acrisia City" : k == "seaglass" ? "Littleroot Town" : "New Bark Town";
		CHECK(!r.regionMaps.empty());
		if (!r.regionMaps.empty())
		{
			const RegionMap& m0 = r.regionMaps[0];
			EQ(!m0.rgba.empty(), pictured);
			if (!pictured) EQ(m0.name, std::string("Johto"));
			if (k == "heart-and-soul") { EQ(r.regionMaps.size(), (size_t)2); if (r.regionMaps.size() > 1) EQ(r.regionMaps[1].name, std::string("Kanto")); }
			// (a town over the tower in it: Ecruteak City's cell names the city, not the Burned Tower)
			if (!pictured)
			{
				int city = -1;
				for (size_t i = 0; i < r.locationNames.size(); i++) if (r.locationNames[i] == "Ecruteak City") city = (int)i;
				bool named = false;
				for (uint16_t c : m0.cells) named = named || c == city;
				CHECK(named);
			}
			for (auto& m : r.regionMaps) { CHECK(m.w > 0 && m.h > 0); EQ(m.cells.size(), (size_t)m.gridW * m.gridH); }
		}
		const Location* town = nullptr;
		int onMap = 0;
		for (auto& z : r.zones) { onMap += z.second.spot.map >= 0; if (!town && z.second.name == home && z.second.spot.map >= 0) town = &z.second; }
		CHECK(town != nullptr);
		CHECK(onMap > 150);
		if (town && (size_t)town->spot.map < r.regionMaps.size())
		{
			const RegionMap& m = r.regionMaps[(size_t)town->spot.map];
			const MapSpot& s = town->spot;
			EQ((int)s.map, 0);
			CHECK(s.w >= 8 && s.h >= 8 && s.x >= 0 && s.y >= 0 && s.x + s.w <= m.w && s.y + s.h <= m.h);
			CHECK(s.tilesW > 0 && s.tilesH > 0);
			// (the cell under the spot names the town: a tap there says its name)
			const int cx = (int)((s.x - m.cellX) / m.cell), cy = (int)((s.y - m.cellY) / m.cell);
			CHECK(cx >= 0 && cy >= 0 && cx < m.gridW && cy < m.gridH);
			if (cx >= 0 && cy >= 0 && cx < m.gridW && cy < m.gridH)
			{
				const uint16_t id = m.cells[(size_t)cy * m.gridW + cx];
				CHECK(id < r.locationNames.size() && r.locationNames[id] == home);
			}
			fprintf(stderr, "  map: %zu region map(s), %s on \"%s\" at %.0f,%.0f (%dx%d tiles); %d zones on a map\n", r.regionMaps.size(), home,
				m.name.c_str(), s.x, s.y, s.tilesW, s.tilesH, onMap);
		}
	}
	// trainers: the first gym leader
	const Trainer* leader = nullptr;
	for (auto& tr : r.trainers) if (tr.second.name == x.firstLeader) { leader = &tr.second; break; }
	CHECK(leader != nullptr);
	if (leader)
	{
		fprintf(stderr, "  %s %s (#%d):", leader->cls.c_str(), leader->name.c_str(), leader->id);
		for (auto& m : leader->team) fprintf(stderr, " %s L%d [%s]", m.species < r.species.size() ? r.species[m.species].name.c_str() : "?", m.level,
			m.moves[0] < r.moves.size() ? r.moves[m.moves[0]].name.c_str() : "?");
		fprintf(stderr, "\n");
		CHECK(!leader->team.empty());
		// (every one a species with a name, its level 2-100; but Lazarus's Roxanne leads with a species whose record the
		// hack left empty - species 76: one allowed)
		int unnamed = 0;
		for (auto& m : leader->team)
		{
			CHECK(m.species < r.species.size() && m.level >= 2 && m.level <= 100);
			unnamed += !m.species || (m.species < r.species.size() && r.species[m.species].name.empty());
		}
		CHECK(unnamed <= 1);
	}
	// sprites decode
	t0 = std::chrono::steady_clock::now();
	int pics = 0, tried = 0;
	for (size_t s = 1; s < r.species.size() && s < 1100; s++)
	{
		if (r.species[s].name.empty()) continue;
		tried++;
		auto sp = r.sprite((uint16_t)s);
		if (sp && sp->w == 64 && !sp->frames.empty()) pics++;
	}
	fprintf(stderr, "  sprites: %d of %d decoded in %.0f ms\n", pics, tried, msSince(t0));
	CHECK(pics * 10 >= tried * 9);
	if (!pngDir.empty())
		for (int s : { 1, 4, 25, 35, 150, 152, 252, 387 })
			if ((size_t)s < r.species.size()) if (auto sp = r.sprite((uint16_t)s))
			{
				png(pngDir + "/" + x.key + "-" + std::to_string(s) + ".png", *sp, false);
				png(pngDir + "/" + x.key + "-" + std::to_string(s) + "-shiny.png", *sp, true);
			}
	// the item map Live uses
	auto im = gba::expansionItemMap(rd.get());
	CHECK((bool)im);
	if (im) EQ(im(1), (uint16_t)4);		// (expansion: Poké Ball is item 1)
}

int main(int argc, char** argv)
{
	std::string dir = argc > 1 ? argv[1] : getenv("GBA_HACKS") ? getenv("GBA_HACKS") : std::string(getenv("HOME") ? getenv("HOME") : ".") + "/gba-companion/hacks";
	std::string pngDir = argc > 2 ? argv[2] : "";
	// smol: a made-up BASE_ONLY stream (no entropy coding): symbols 1, 2; instructions "1 symbol x3", "2 literal symbols"
	{
		std::vector<uint8_t> s(64, 0);
		// header: mode 1, imageSize (bytes / 4) 8 (32 bytes), symSize 3, initial 0, bitstream 0, loSize 4
		uint32_t w0 = 1 | 8u << 4 | 3u << 18, w1 = 4u << 19;
		memcpy(s.data(), &w0, 4); memcpy(s.data() + 4, &w1, 4);
		const uint16_t syms[3] = { 0x1111, 0x2222, 0x3333 };
		memcpy(s.data() + 8, syms, 6);
		const uint8_t lo[4] = { 13, 1, 0, 2 };		// (length 13, offset 1: the symbol then 13 copies; length 0, offset 2: two symbols)
		memcpy(s.data() + 14, lo, 4);
		std::vector<uint8_t> out;
		CHECK(gba::decompressPic(s.data(), s.size(), 0, out, 0x4000));
		EQ(out.size(), (size_t)32);
		if (out.size() == 32) { EQ((int)out[0], 0x11); EQ((int)out[27], 0x11); EQ((int)out[28], 0x22); EQ((int)out[30], 0x33); }
	}
	for (const Expect& x : HACKS) testHack(dir, x, pngDir);
	fprintf(stderr, "%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
