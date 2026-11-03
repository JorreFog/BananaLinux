// The GBA Pokémon games' ROM data for the DS companion (src/gba/gen3rom.cpp) on the player's own ROMs: known facts
// every one of the five must show, the timings, and a few sprites as PNGs for a look. Without the ROMs only the
// header checks run.
//   make tests/gba-rom-test && tests/gba-rom-test [roms-dir [png-dir]]   (or GBA_ROMS=<dir>)
#include "gba/gen3rom.h"
#include "nds/rom.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
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

// the header alone: the five codes, anything else NONE
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
	if (g.id == GameId::NONE) { fprintf(stderr, "gba-rom-test: %s: not one of the five, skipped\n", path.c_str()); return; }
	EQ(gba::detectGame(rom->data(), rom->size()).romHash, g.romHash);
	if (g.id != GameId::RUBY && g.id != GameId::SAPPHIRE)
	{
		// a CFRU-style hack: its species names moved to a longer table, or the Fairy type in the base stats
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
			for (auto& fl : files) { testRom(fl, pngDir); roms++; }
		}
	if (!roms) fprintf(stderr, "gba-rom-test: no ROMs (GBA_ROMS or the first argument: a folder of .gba files): header checks only\n");
	fprintf(stderr, "gba-rom-test: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
