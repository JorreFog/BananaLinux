// The GBA Pokémon games' ROM data for the DS companion (src/gba/gen3rom.cpp) on the player's own ROMs: known facts
// every one of the five must show, the timings, and a few sprites as PNGs for a look. Without the ROMs only the
// header checks run. The ROM hacks gba/hacks.h knows and this reads (the three Glazed releases, Gaia): their own facts,
// from a folder of them (searched one level down: hacks/<name>/<file>.gba), skipped when there is none.
//   make tests/gba-rom-test && tests/gba-rom-test [roms-dir [png-dir [hacks-dir]]]   (or GBA_ROMS=<dir>, GBA_HACKS=<dir>)
#include "gba/gen3live.h"
#include "gba/gen3rom.h"
#include "gba/hacks.h"
#include "nds/guide.h"
#include "nds/rom.h"
#include "pokemon.h"
#include <thread>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <fstream>
#include <iterator>
#include <string>
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

static double msSince(std::chrono::steady_clock::time_point t0)
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// the header alone: the five codes, anything else NONE (and a FireRed header with no tables behind it)
static void testHeaders()
{
	std::vector<uint8_t> h(0x200, 0);
	auto with = [&](const char* code, uint8_t rev) { memcpy(h.data() + 0xac, code, 4); h[0xbc] = rev; return gba::detectGame(h.data(), h.size()); };
	// (Ruby and Sapphire have no header tables: the code is enough; the others' hack check needs their tables)
	EQ((int)with("AXVE", 2).id, (int)GameId::RUBY);
	EQ((int)with("AXPE", 2).gen, (int)Gen::III);
	EQ(with("AXPE", 2).dexSize, 386);
	EQ(with("AXVE", 2).title, std::string("Ruby"));
	EQ((int)with("AXVE", 2).revision, 2);
	CHECK(!with("AXVE", 2).romHash.empty());
	EQ((int)with("AXVJ", 0).id, (int)GameId::NONE);		// (Japanese)
	EQ((int)with("BPRE", 1).id, (int)GameId::NONE);		// (no tables behind the header: not taken for FireRed)
	EQ((int)with("CPUE", 0).id, (int)GameId::NONE);
	EQ((int)gba::detectGame(h.data(), 0x10).id, (int)GameId::NONE);
	EQ(gba::itemToGen4(0), (uint16_t)0);
	EQ(gba::itemToGen4(1), (uint16_t)1);			// Master Ball
	EQ(gba::itemToGen4(13), (uint16_t)17);			// Potion
	EQ(gba::itemToGen4(68), (uint16_t)50);			// Rare Candy
	EQ(gba::itemToGen4(186), (uint16_t)220);		// Choice Band
	EQ(gba::itemToGen4(200), (uint16_t)234);		// Leftovers
	EQ(gba::itemToGen4(289), (uint16_t)328);		// TM01
	EQ(gba::itemToGen4(339), (uint16_t)420);		// HM01
	EQ(gba::itemToGen4(259), (uint16_t)(gba::GEN3_ONLY + 259));	// Mach Bike
	EQ(gba::itemToGen4(5000), (uint16_t)(gba::GEN3_ONLY + 5000));
}

static void png(const std::string& dir, const std::string& nm, const Sprite& s, bool shiny, size_t frame)
{
	std::vector<uint8_t> rgba((size_t)s.w * s.h * 4);
	const auto& pal = shiny ? s.shiny : s.normal;
	for (size_t i = 0; i < (size_t)s.w * s.h; i++)
	{
		uint8_t c = s.frames[frame][i];
		uint32_t v = pal[c];
		rgba[i * 4] = (uint8_t)(v >> 16); rgba[i * 4 + 1] = (uint8_t)(v >> 8); rgba[i * 4 + 2] = (uint8_t)v; rgba[i * 4 + 3] = c ? 255 : 0;
	}
	stbi_write_png((dir + "/" + nm + ".png").c_str(), s.w, s.h, 4, rgba.data(), s.w * 4);
}

static const Trainer* trainerNamed(const RomData& r, const char* nm, const char* cls)
{
	for (auto& t : r.trainers) if (t.second.name == nm && t.second.cls == cls && !t.second.team.empty()) return &t.second;
	return nullptr;
}

static const Location* zoneNamed(const RomData& r, const char* nm, EncounterKind k)
{
	for (auto& z : r.zones)
		if (z.second.name == nm)
			for (auto& e : z.second.encounters) if (e.kind == k) return &z.second;
	return nullptr;
}

static bool has(const Location* l, uint16_t species, EncounterKind k)
{
	if (!l) return false;
	for (auto& e : l->encounters) if (e.species == species && e.kind == k && e.percent > 0 && e.minLevel > 0) return true;
	return false;
}

static void testRom(const std::string& path, const std::string& pngDir)
{
	std::ifstream f(path, std::ios::binary);
	auto rom = std::make_shared<std::vector<uint8_t>>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	auto t0 = std::chrono::steady_clock::now();
	Game g = gba::detectGame(rom->data(), rom->size());
	double detectMs = msSince(t0);
	if (g.id == GameId::NONE || g.id == GameId::HACK) { fprintf(stderr, "gba-rom-test: %s: not one of the five, skipped\n", path.c_str()); return; }
	EQ(gba::detectGame(rom->data(), rom->size()).romHash, g.romHash);
	if (g.id != GameId::RUBY && g.id != GameId::SAPPHIRE)
	{
		// a ROM hack the list doesn't know isn't supported: its species names moved to a longer table (a CFRU FireRed, a
		// patched Emerald), or a type past Dark in the base stats
		std::vector<uint8_t> h(*rom);
		uint32_t sp; memcpy(&sp, h.data() + 0x144, 4); sp += 11 * 100; memcpy(h.data() + 0x144, &sp, 4);
		EQ((int)gba::detectGame(h.data(), h.size()).id, (int)GameId::NONE);
		h = *rom;
		uint32_t bs; memcpy(&bs, h.data() + 0x1bc, 4);
		h[bs - 0x08000000u + 28 * 35 + 7] = 23;	// (Clefairy)
		EQ((int)gba::detectGame(h.data(), h.size()).id, (int)GameId::NONE);
	}
	t0 = std::chrono::steady_clock::now();
	pkmn::RomTables t = pkmn::findTables(rom->data(), rom->size());
	t.rom = rom;
	double tablesMs = msSince(t0);
	t0 = std::chrono::steady_clock::now();
	auto r = gba::readRomData(t, g, "");
	double readMs = msSince(t0);
	CHECK(r != nullptr);
	if (!r) { fprintf(stderr, "FAIL %s: no RomData\n", path.c_str()); fails++; return; }
	const bool frlg = g.id == GameId::FIRERED || g.id == GameId::LEAFGREEN, rs = g.id == GameId::RUBY || g.id == GameId::SAPPHIRE;
	const bool emerald = g.id == GameId::EMERALD;

	EQ((int)r->game.gen, (int)Gen::III);
	EQ(r->game.dexSize, 386);
	EQ(r->species.size(), (size_t)387);
	EQ(r->moves.size(), (size_t)355);
	// species by national number
	EQ(r->species[25].name, std::string("Pikachu"));
	EQ((int)r->species[25].types[0], (int)ELECTRIC);
	EQ(r->species[386].name, std::string("Deoxys"));
	EQ(r->species[252].name, std::string("Treecko"));	// (internal 277: the national order, not the game's)
	EQ((int)r->species[1].types[0], (int)GRASS);
	EQ((int)r->species[1].types[1], (int)POISON);
	EQ((int)r->species[6].types[1], (int)FLYING);
	EQ((int)r->species[197].types[0], (int)DARK);		// (Gen III 17 -> 16)
	EQ((int)r->species[1].base[0], 45); EQ((int)r->species[1].base[3], 45); EQ((int)r->species[1].base[4], 65);
	EQ((int)r->species[150].base[4], 154);				// Mewtwo's Sp. Atk
	EQ((int)r->species[1].catchRate, 45);
	EQ((int)r->species[1].genderRatio, 31);
	EQ((int)r->species[1].baseFriendship, 70);
	EQ((int)r->species[1].growth, 3);
	EQ((int)r->species[1].baseExp, 64);
	EQ((int)r->species[1].evYield[4], 1);				// Bulbasaur: 1 Sp. Atk
	EQ((int)r->species[1].abilities[0], 65);			// Overgrow
	EQ(r->abilities.size() > 65 ? r->abilities[65] : "", std::string("Overgrow"));
	CHECK(r->abilityText.size() > 65 && !r->abilityText[65].empty());
	EQ(r->species[1].category, std::string("Seed"));
	EQ((int)r->species[1].height, 7); EQ((int)r->species[1].weight, 69);
	CHECK(r->species[1].dexText.size() > 40);
	CHECK(r->species[1].dexText.find('\n') == std::string::npos);
	// learnsets: Bulbasaur's Vine Whip at 10 in all five
	bool vine = false;
	for (auto& lm : r->species[1].learnset) if (lm.move == 22 && lm.level == 10) vine = true;
	CHECK(vine);
	// evolutions: Bulbasaur at 16; Eevee by stones (Gen IV item ids) and friendship
	CHECK(!r->species[1].evolutions.empty() && r->species[1].evolutions[0].into == 2 && r->species[1].evolutions[0].method == evo::LEVEL &&
		r->species[1].evolutions[0].param == 16 && r->species[1].evolutions[0].how == "Level 16");
	bool water = false, espeon = false, sil = false, nin = false;
	for (auto& e : r->species[133].evolutions)
	{
		if (e.into == 134 && e.method == evo::USE_ITEM && e.param < r->items.size() && r->items[e.param].name == "Water Stone") water = true;
		if (e.into == 196 && e.method == evo::FRIENDSHIP_DAY) espeon = true;
	}
	for (auto& e : r->species[265].evolutions) if (e.into == 266 && e.method == evo::LEVEL_PID_LOW && e.param == 7) sil = true;
	for (auto& e : r->species[290].evolutions) if (e.into == 292 && e.method == evo::LEVEL_SHEDINJA) nin = true;
	CHECK(water); CHECK(espeon); CHECK(sil); CHECK(nin);
	CHECK(!r->species[66].evolutions.empty() && r->species[67].evolutions.size() == 1 && r->species[67].evolutions[0].method == evo::TRADE);
	// TMs: Gen IV's numbering (HM01 = 93)
	EQ(r->tmMoves.size(), (size_t)100);
	EQ(r->tmMoves[0], (uint16_t)264);	// TM01 Focus Punch
	EQ(r->tmMoves[49], (uint16_t)315);	// TM50 Overheat
	EQ(r->tmMoves[92], (uint16_t)15);	// HM01 Cut
	EQ(r->tmMoves[99], (uint16_t)291);	// HM08 Dive
	auto learnsTm = [&](int sp, int tm) { auto& v = r->species[sp].tms; return std::find(v.begin(), v.end(), (uint16_t)tm) != v.end(); };
	CHECK(learnsTm(1, 6)); CHECK(learnsTm(1, 93)); CHECK(!learnsTm(1, 94));	// Bulbasaur: Toxic, Cut, not Fly
	EQ(r->species[151].tms.size(), (size_t)58);
	CHECK(r->species[129].tms.empty());	// Magikarp
	// moves
	EQ(r->moves[33].name, std::string("Tackle"));
	EQ((int)r->moves[33].power, 35); EQ((int)r->moves[33].type, (int)NORMAL); EQ((int)r->moves[33].cat, (int)MoveCategory::PHYSICAL);
	EQ((int)r->moves[33].accuracy, 95); EQ((int)r->moves[33].pp, 35);
	EQ((int)r->moves[52].cat, (int)MoveCategory::SPECIAL);		// Ember (Fire: special in Gen III)
	EQ((int)r->moves[242].cat, (int)MoveCategory::SPECIAL);	// Crunch (Dark: special in Gen III)
	EQ((int)r->moves[45].cat, (int)MoveCategory::STATUS);		// Growl
	EQ((int)r->moves[98].priority, 1);							// Quick Attack
	EQ((int)(int8_t)r->moves[68].priority, -5);					// Counter
	EQ((int)r->moves[174].type, (int)NO_TYPE);					// Curse (???)
	EQ((int)r->moves[85].effectChance, 10);						// Thunderbolt
	CHECK(!r->moves[1].description.empty() && !r->moves[354].description.empty());
	// items in Gen IV's numbering
	EQ(r->items[1].name, std::string("Master Ball"));
	EQ((int)r->items[1].pocket, (int)POCKET_BALLS);
	EQ(r->items[17].name, std::string("Potion"));
	EQ((int)r->items[17].price, 300);
	EQ((int)r->items[17].pocket, (int)POCKET_MEDICINE);
	CHECK(!r->items[17].description.empty());
	EQ(r->items[328].name, std::string("TM01"));
	EQ((int)r->items[328].pocket, (int)POCKET_TMS);
	EQ(r->items[420].name, std::string("HM01"));
	EQ(r->items[234].name, std::string("Leftovers"));
	EQ(r->natures.size(), (size_t)25);
	if (!r->natures.empty()) { EQ(r->natures[0], std::string("Hardy")); EQ(r->natures[24], std::string("Quirky")); }
	// trainers: the first gym's leader
	const Trainer* leader = trainerNamed(*r, frlg ? "Brock" : "Roxanne", "Leader");
	CHECK(leader != nullptr);
	if (leader)
	{
		const TrainerMon& ace = leader->team.back();
		EQ(ace.species, (uint16_t)(frlg ? 95 : 299));	// Onix / Nosepass
		EQ((int)ace.level, frlg ? 14 : 15);
		CHECK(ace.moves[0] != 0);
		fprintf(stderr, "  %s %s:", leader->cls.c_str(), leader->name.c_str());
		for (auto& m : leader->team) fprintf(stderr, " %s %d [%s %s %s %s]%s", r->species[m.species].name.c_str(), m.level,
			r->moves[m.moves[0]].name.c_str(), r->moves[m.moves[1]].name.c_str(), r->moves[m.moves[2]].name.c_str(), r->moves[m.moves[3]].name.c_str(),
			m.item ? (" @" + r->items[m.item].name).c_str() : "");
		fprintf(stderr, "\n");
	}
	bool doubles = false;
	for (auto& tr : r->trainers) doubles |= tr.second.doubleBattle;
	CHECK(doubles);
	CHECK(r->trainers.size() > 300);
	// places: Route 101 / Route 1 grass
	const Location* route = zoneNamed(*r, frlg ? "Route 1" : "Route 101", EncounterKind::GRASS);
	CHECK(route != nullptr);
	if (frlg) { CHECK(has(route, 16, EncounterKind::GRASS)); CHECK(has(route, 19, EncounterKind::GRASS)); }
	else { CHECK(has(route, 263, EncounterKind::GRASS)); CHECK(has(route, 265, EncounterKind::GRASS)); }
	if (route) { int sum = 0; for (auto& e : route->encounters) if (e.kind == EncounterKind::GRASS) sum += e.percent; EQ(sum, 100); }
	CHECK(route && (route->id >> 8) < 64);
	const Location* rod = zoneNamed(*r, frlg ? "Route 4" : "Route 104", EncounterKind::OLD_ROD);
	CHECK(has(rod, 129, EncounterKind::OLD_ROD));	// Magikarp
	CHECK(zoneNamed(*r, frlg ? "Rock Tunnel" : "Route 111", EncounterKind::ROCK_SMASH) != nullptr);
	EQ(r->locationNames.size() >= 256, true);
	EQ(r->locationNames[frlg ? 0x58 : 0], std::string(frlg ? "Pallet Town" : "Littleroot Town"));
	// sprites
	auto t1 = std::chrono::steady_clock::now();
	auto s = r->sprite(25);
	double spriteMs = msSince(t1);
	CHECK(s != nullptr);
	if (s)
	{
		EQ(s->w, 64); EQ(s->h, 64);
		EQ(s->frames.size(), (size_t)(emerald ? 2 : 1));
		EQ(s->anim.empty(), !emerald);
		CHECK(s->normal != s->shiny);
		int opaque = 0; for (uint8_t c : s->frames[0]) opaque += c != 0;
		CHECK(opaque > 500 && opaque < 4096);
	}
	if (auto dx = r->sprite(386)) { EQ(dx->frames.size(), (size_t)1); CHECK(dx->anim.empty()); }	// (the game's own form, still)
	CHECK(r->sprite(0) == nullptr);
	CHECK(r->sprite(387) == nullptr);
	auto u = r->sprite(201, false, 1), ua = r->sprite(201);
	CHECK(u && ua && u->frames[0] != ua->frames[0]);	// Unown B is not A
	// every species decodes
	t1 = std::chrono::steady_clock::now();
	int bad = 0;
	for (int d = 1; d <= 386; d++) { auto x = r->sprite((uint16_t)d); if (!x || x->frames.empty()) bad++; }
	double allMs = msSince(t1);
	EQ(bad, 0);
	if (!pngDir.empty())
		for (int d : { 1, 6, 25, 150, 201, 252, 384, 386 })
			if (auto x = r->sprite((uint16_t)d))
			{
				std::string base = g.title + "-" + std::to_string(d);
				png(pngDir, base, *x, false, 0); png(pngDir, base + "-shiny", *x, true, 0);
				if (x->frames.size() > 1) png(pngDir, base + "-f2", *x, false, 1);
			}
	(void)rs;
	fprintf(stderr, "gba-rom-test: %s rev %d: detect %.1f ms, pkmn tables %.0f ms, RomData %.0f ms, a sprite %.2f ms, all 386 %.0f ms; "
		"%zu trainers, %zu zones, %zu items\n", g.title.c_str(), g.revision, detectMs, tablesMs, readMs, spriteMs, allMs,
		r->trainers.size(), r->zones.size(), r->items.size());
}

// ---- the ROM hacks ----------------------------------------------------------------------------------------------
static const Species* named(const RomData& r, const char* nm, uint16_t* at = nullptr)
{
	for (size_t i = 1; i < r.species.size(); i++) if (r.species[i].name == nm) { if (at) *at = (uint16_t)i; return &r.species[i]; }
	return nullptr;
}
static bool types(const Species* s, Type a, Type b) { return s && s->types[0] == a && s->types[1] == b; }
static const Location* placeNamed(const RomData& r, const char* nm)
{
	for (auto& z : r.zones) if (z.second.name == nm && !z.second.encounters.empty()) return &z.second;
	return nullptr;
}
static bool meets(const RomData& r, const Location* l, const char* species, EncounterKind k)
{
	if (!l) return false;
	for (auto& e : l->encounters) if (e.kind == k && e.species < r.species.size() && r.species[e.species].name == species && e.percent && e.minLevel) return true;
	return false;
}
// the first trainer of that class and name whose team is these species at these levels
static bool team(const RomData& r, const char* cls, const char* nm, std::initializer_list<std::pair<const char*, int>> want)
{
	for (auto& t : r.trainers)
	{
		if (t.second.cls != cls || t.second.name != nm || t.second.team.size() != want.size()) continue;
		size_t i = 0; bool ok = true;
		for (auto& w : want)
		{
			const TrainerMon& m = t.second.team[i++];
			ok = ok && m.species < r.species.size() && r.species[m.species].name == w.first && m.level == w.second;
		}
		if (ok) return true;
	}
	return false;
}

static void testHack(const std::string& path, const std::string& pngDir)
{
	std::ifstream f(path, std::ios::binary);
	auto rom = std::make_shared<std::vector<uint8_t>>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	auto t0 = std::chrono::steady_clock::now();
	const uint32_t crc = gba::romCrc32(rom->data(), rom->size());
	const double crcMs = msSince(t0);
	const gba::HackInfo* hi = gba::hackByCrc(crc);
	// (a hack the owner plays that the pages must read - 2026-11-04: Pokémon Unbound had only the old views)
	if (!hi) { CHECK(path.find("Unbound") == std::string::npos); if (path.find("Unbound") != std::string::npos) fprintf(stderr, "FAIL %s: not a hack the companion knows\n", path.c_str()); return; }
	const std::string key = hi->key;
	const bool glazed = key == "glazed", golden = key == "golden-glazed", unoff = key == "glazed-unofficial", gaia = key == "gaia";
	const bool unbound = key == "unbound";
	if (!glazed && !golden && !unoff && !gaia && !unbound) { fprintf(stderr, "gba-rom-test: %s: %s is read elsewhere (its engine), skipped\n", path.c_str(), hi->title); return; }
	t0 = std::chrono::steady_clock::now();
	Game g = gba::detectGame(rom->data(), rom->size(), crc);
	const double detectMs = msSince(t0);
	EQ((int)g.id, (int)GameId::HACK);
	EQ(g.hack, key);
	EQ(g.title, std::string(hi->title));
	EQ((int)g.gen, (int)Gen::III);
	CHECK(g.modern);
	if (!unbound) EQ(g.dexSize, gaia ? 721 : 386);
	else CHECK(g.dexSize > 721);	// (Unbound: Pokémon up to Generation VIII)
	EQ(gba::detectGame(rom->data(), rom->size()).hack, key);	// (the CRC-32 worked out there)
	t0 = std::chrono::steady_clock::now();
	pkmn::RomTables t = pkmn::findTables(rom->data(), rom->size(), pkmn::detectGame(rom->data(), rom->size()));
	t.rom = rom; t.crc = crc;
	const double tablesMs = msSince(t0);
	EQ(t.has.fairyType, gaia || unbound ? (int)pkmn::FAIRY : (int)pkmn::MYSTERY);
	t0 = std::chrono::steady_clock::now();
	auto r = gba::readRomData(t, g, "");
	const double readMs = msSince(t0);
	CHECK(r != nullptr);
	if (!r) return;
	EQ(r->game.dexSize, g.dexSize);
	// species: by the hack's own national numbers, names in the DS games' case, Fairy, the newer generations
	EQ(r->species[1].name, std::string("Bulbasaur"));
	CHECK(types(&r->species[1], GRASS, POISON));
	CHECK(types(named(*r, "Deino"), DARK, DRAGON));		// (Generation V)
	CHECK(r->species[1].dexText.size() > 40);
	EQ(r->species[1].category, std::string("Seed"));
	CHECK(r->species[1].learnset.size() > 5 && r->species[1].learnset[0].level == 1);
	CHECK(!r->species[1].tms.empty());
	CHECK(!r->species[1].evolutions.empty() && r->species[1].evolutions[0].into == 2);
	EQ(r->natures.size(), (size_t)25);
	if (!r->natures.empty()) EQ(r->natures[0], std::string("Hardy"));
	uint16_t at = 0;
	if (unbound)
	{
		// Pokémon Unbound (CFRU, the Borrius region): Fairy, the newer generations, the split, its first town and gym
		CHECK(types(named(*r, "Clefairy"), FAIRY, FAIRY));
		CHECK(types(named(*r, "Sylveon"), FAIRY, FAIRY));
		CHECK(types(named(*r, "Deino"), DARK, DRAGON));
		CHECK(named(*r, "Grookey") != nullptr);			// (Generation VIII)
		CHECK(r->moves.size() > 355);
		EQ((int)r->moves[242].cat, (int)MoveCategory::PHYSICAL);	// Crunch
		EQ((int)r->moves[52].cat, (int)MoveCategory::SPECIAL);		// Ember
		bool dresco = false;
		for (auto& z : r->zones) dresco |= z.second.name == "Dresco Town";
		CHECK(dresco);
		bool mirskle = false;
		for (auto& tr : r->trainers) mirskle |= tr.second.name == "Mirskle" && !tr.second.team.empty();
		CHECK(mirskle);
		uint16_t nat = 0; uint8_t form = 0;
		CHECK(gba::speciesOf(*r, 1, nat, form) && nat == 1 && form == 0);
	}
	else if (gaia)
	{
		EQ(r->species[387].name, std::string("Turtwig"));
		CHECK(types(&r->species[387], GRASS, GRASS));
		EQ(r->species[700].name, std::string("Sylveon"));
		CHECK(types(&r->species[700], FAIRY, FAIRY));
		CHECK(types(&r->species[35], FAIRY, FAIRY));		// Clefairy: Fairy since Generation VI
		EQ(r->species[721].name, std::string("Volcanion"));
		EQ(r->species[6].formCount, (uint8_t)3);			// Charizard and its two Mega Evolutions
		EQ(r->species[493].formCount, (uint8_t)18);		// Arceus's types
		bool shiny = false;
		for (auto& e : r->species[133].evolutions)
			shiny |= e.into == 700 && e.method == evo::USE_ITEM && e.param < r->items.size() && r->items[e.param].name == "Shiny Stone";
		CHECK(shiny);
		// moves past the 354, with their own categories
		EQ(r->moves.size(), (size_t)511);
		EQ(r->moves[356].name, std::string("Inferno"));
		EQ((int)r->moves[356].type, (int)FIRE);
		EQ((int)r->moves[356].cat, (int)MoveCategory::SPECIAL);
		CHECK(!r->moves[356].description.empty());
		EQ((int)r->moves[355].cat, (int)MoveCategory::STATUS);		// Roost
		EQ((int)r->moves[242].cat, (int)MoveCategory::PHYSICAL);	// Crunch: physical since Generation IV
		CHECK(r->abilities.size() > 150);
		// items: Gen IV's ids where they are Gen IV items (Dusk Ball 13), the rest past 1000 (the Mega Stones)
		EQ(r->items[13].name, std::string("Dusk Ball"));
		EQ(gba::itemId(*r, 89), (uint16_t)13);
		CHECK(gba::itemId(*r, 230) >= gba::GEN3_ONLY);				// Venusaurite
		// the first route and gym
		CHECK(meets(*r, placeNamed(*r, "Celanto Path"), "Bidoof", EncounterKind::GRASS));
		CHECK(meets(*r, placeNamed(*r, "Celanto Path"), "Zigzagoon", EncounterKind::GRASS));
		CHECK(team(*r, "Leader", "Fernando", { { "Clefairy", 11 }, { "Ralts", 11 }, { "Jigglypuff", 13 } }));
		// the running game's species: the game's own index to the national number and form
		uint16_t nat = 0; uint8_t form = 0;
		CHECK(gba::speciesOf(*r, 440, nat, form) && nat == 387 && form == 0);	// (Turtwig's slot)
	}
	else
	{
		if (!golden) CHECK(types(named(*r, "Clefairy"), FAIRY, FAIRY));	// (Golden Glazed has Cutiefly in its place)
		if (!golden) CHECK(types(named(*r, "Jigglypuff"), NORMAL, FAIRY));
		if (golden) CHECK(types(named(*r, "Cutiefly"), BUG, FAIRY));	// (Generation VII)
		EQ(r->moves.size(), (size_t)355);
		EQ((int)r->moves[52].cat, (int)MoveCategory::SPECIAL);		// Ember
		EQ((int)r->moves[33].cat, (int)MoveCategory::PHYSICAL);	// Tackle
		EQ((int)r->moves[45].cat, (int)MoveCategory::STATUS);		// Growl
		if (golden) { EQ(r->moves[2].name, std::string("Earth Power")); EQ((int)r->moves[2].cat, (int)MoveCategory::SPECIAL); }
		// a Mega Evolution: Venusaur's form, named so
		const Species* v = named(*r, "Venusaur", &at);
		CHECK(v && v->formCount == 2 && v->formsAt > 386 && v->formsAt < r->species.size() && r->species[v->formsAt].name == "Mega Venusaur");
		CHECK(named(*r, "Timburr") || golden);		// (a species past the Pokédex: after it)
		// items: Park Ball for Safari Ball's slot, a donut for a berry's: not Gen IV's Liechi Berry
		EQ(r->items[gba::itemId(*r, 5)].name, std::string("Park Ball"));
		CHECK(gba::itemId(*r, 168) >= gba::GEN3_ONLY);
		EQ(gba::itemId(*r, 13), (uint16_t)17);			// Potion
		// the first route and gym
		CHECK(meets(*r, placeNamed(*r, "Forest Pass"), "Taillow", EncounterKind::GRASS));
		CHECK(team(*r, "Leader", "Sparky", { { "Shinx", glazed ? 14 : 13 }, { "Flaaffy", glazed ? 14 : 13 }, { "Jolteon", glazed ? 17 : 15 } }));
		uint16_t nat = 0; uint8_t form = 0;
		CHECK(gba::speciesOf(*r, 269, nat, form) && nat == at && form == 1);	// (Mega Venusaur's slot)
		CHECK(!t.mapImages.empty());		// the region map's picture
		// a battler of type 9: the Fairy type here
		pkmn::Snapshot s;
		s.seen = true; s.inBattle = true;
		pkmn::Mon m; m.species = 35; m.level = 5; m.hp = 10; m.maxHp = 10; m.types[0] = m.types[1] = 9;
		s.party.push_back(m); s.lead = m; s.foes.push_back(m);
		auto l = gba::toLive(s, &t, r.get(), [&](uint16_t i) { return gba::itemId(*r, i); },
			[&](uint16_t sp, uint16_t& n, uint8_t& fm) { return gba::speciesOf(*r, sp, n, fm); });
		EQ((int)l->battle.foes[0].types[0], (int)FAIRY);
		EQ(r->species[l->party[0].species].name, std::string(golden ? "Cutiefly" : "Clefairy"));
	}
	// sprites: every species and form decodes, normal and shiny
	int bad = 0;
	for (size_t d = 1; d < r->species.size(); d++)
		if (!r->species[d].name.empty())
		{
			auto x = r->sprite((uint16_t)d);
			// (a form past the Pokédex may share its palettes: Glazed's second Pikachu)
			// (Minior's Meteor Form is the same shiny: its core differs, which the games show only out of the shell)
			if (!x || x->frames.empty() || (x->normal == x->shiny && d <= (size_t)r->game.dexSize && r->species[d].name != "Minior")) { bad++; fprintf(stderr, "  %s: %s's picture: %s\n", hi->title, r->species[d].name.c_str(), x ? "its shiny palette is its normal one" : "none"); }
		}
	EQ(bad, 0);
	auto deino = named(*r, "Deino", &at) ? r->sprite(at) : nullptr;
	CHECK(deino != nullptr);
	if (!pngDir.empty())
	{
		std::vector<uint16_t> pics = { 1, 25 };
		for (const char* nm : { "Deino", "Sylveon", "Turtwig", "Mega Venusaur", "Cutiefly", "Timburr" }) if (named(*r, nm, &at)) pics.push_back(at);
		for (uint16_t d : pics)
			if (auto x = r->sprite(d)) { std::string b = key + "-" + std::to_string(d); png(pngDir, b, *x, false, 0); png(pngDir, b + "-shiny", *x, true, 0); }
		if (!t.mapImages.empty())
		{
			auto& im = t.mapImages[0];
			stbi_write_png((pngDir + "/" + key + "-map.png").c_str(), im.w, im.h, 4, im.rgba.data(), im.w * 4);
		}
	}
	fprintf(stderr, "gba-rom-test: %s: CRC-32 %.1f ms, detect %.1f ms, pkmn tables %.0f ms, RomData %.0f ms; %zu species (%d in its Pokédex), "
		"%zu moves, %zu trainers, %zu zones, %zu items\n", hi->title, crcMs, detectMs, tablesMs, readMs, r->species.size() - 1, r->game.dexSize,
		r->moves.size() - 1, r->trainers.size(), r->zones.size(), r->items.size());
}

// every game the companion supports (the five, the hacks gba/hacks.h lists) is read by the pages; another release of a
// listed hack isn't supported (the panel says so, nothing more: 2026-11-06, the owner)
static void testPagesRead(const std::string& path)
{
	pkmn::Companion c;
	if (!c.open(path)) return;
	std::shared_ptr<const pkmn::RomTables> t;
	for (int i = 0; i < 600 && !(t = c.tables()); i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
	CHECK(t && t->rom);
	if (!t || !t->rom) return;
	Game g;
	bool unsupported = true;
	auto r = gba::readGame(*t, &g, &unsupported);
	CHECK(g.id != GameId::NONE);
	CHECK(!unsupported);
	CHECK(!g.title.empty());
	CHECK(r != nullptr);
	if (!r) { fprintf(stderr, "FAIL %s: the companion opens it (%s), the pages don't read it\n", path.c_str(), c.game()->name); return; }
	CHECK(r->species.size() > 386);
	if (g.id == GameId::HACK)
	{
		// (another release of it - a CRC-32 not in the list: not supported)
		pkmn::RomTables other = *t;
		other.crc ^= 1;
		Game none;
		bool no = false;
		CHECK(gba::readGame(other, &none, &no) == nullptr);
		CHECK(no);
		EQ((int)none.id, (int)GameId::NONE);
	}
	fprintf(stderr, "gba-rom-test: %s: the pages read it as %s (%s, %zu species)\n", path.c_str(), g.title.c_str(), g.hack.c_str(), r->species.size());
	// its guide (the repository's, BANANA_GBA_GUIDE): every place a zone of this ROM, every milestone's foe a trainer
	// in it (the save's trainer flags tick a milestone off by them)
	if (!getenv("BANANA_GBA_GUIDE")) return;
	GuideData gd;
	const GuideResolver res = guideResolver(*r);
	std::string why;
	if (!loadGuide(r->game, gd, &why, &res))
	{
		fprintf(stderr, "FAIL %s: no guide (%s)\n", g.title.c_str(), why.c_str());
		CHECK(!"every supported game has a guide");
		return;
	}
	int foes = 0, found = 0;
	std::string missing;
	for (auto& m : gd.milestones)
	{
		if (m.foeName.empty()) continue;
		foes++;
		if (!m.trainerIds.empty()) found++;
		else missing += " " + m.id;
	}
	fprintf(stderr, "gba-rom-test: %s guide: %zu milestones, %d foes found in the ROM of %d,%s %zu steps, %zu secrets, %zu notes, %zu unresolved places%s\n",
		g.title.c_str(), gd.milestones.size(), found, foes, missing.empty() ? "" : (" not:" + missing + ",").c_str(), gd.steps.size(), gd.secrets.size(),
		gd.notes.size(), gd.unresolved.size(), why.empty() ? "" : (" (" + why + ")").c_str());
	CHECK(found == foes);
	CHECK(gd.unresolved.empty());
}

int main(int argc, char** argv)
{
	testHeaders();
	std::string dir = argc > 1 ? argv[1] : getenv("GBA_ROMS") ? getenv("GBA_ROMS") : "";
	std::string pngDir = argc > 2 ? argv[2] : "";
	int roms = 0;
	if (!dir.empty())
		if (DIR* d = opendir(dir.c_str()))
		{
			std::vector<std::string> files;
			while (dirent* e = readdir(d))
			{
				std::string nm = e->d_name;
				if (nm.size() > 4 && nm.compare(nm.size() - 4, 4, ".gba") == 0) files.push_back(dir + "/" + nm);
			}
			closedir(d);
			std::sort(files.begin(), files.end());
			for (auto& fl : files) { testRom(fl, pngDir); testPagesRead(fl); roms++; }
		}
	if (!roms) fprintf(stderr, "gba-rom-test: no ROMs (GBA_ROMS or the first argument: a folder of .gba files): header checks only\n");
	// the ROM hacks: a folder of them, each .gba in it or one folder down
	std::string hacks = argc > 3 ? argv[3] : getenv("GBA_HACKS") ? getenv("GBA_HACKS") : "";
	int hackRoms = 0;
	std::vector<std::string> hf;
	std::function<void(const std::string&, int)> scan = [&](const std::string& p, int depth)
	{
		DIR* d = opendir(p.c_str());
		if (!d) return;
		while (dirent* e = readdir(d))
		{
			std::string nm = e->d_name;
			if (nm == "." || nm == "..") continue;
			const bool dir = e->d_type == DT_DIR;	// (a folder may be named like a ROM: "pokemon_gaia_v3.2.gba")
			if (!dir && nm.size() > 4 && nm.compare(nm.size() - 4, 4, ".gba") == 0) hf.push_back(p + "/" + nm);
			else if (dir && depth > 0) scan(p + "/" + nm, depth - 1);
		}
		closedir(d);
	};
	if (!hacks.empty()) scan(hacks, 1);
	std::sort(hf.begin(), hf.end());
	for (auto& fl : hf) { const int before = checks; testHack(fl, pngDir); testPagesRead(fl); hackRoms += checks > before; }
	if (!hackRoms) fprintf(stderr, "gba-rom-test: no ROM hacks (GBA_HACKS or the third argument): skipped\n");
	fprintf(stderr, "gba-rom-test: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
