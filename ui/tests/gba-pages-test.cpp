// banana-ui tests: the DS companion's pages on the GBA games (gba/gen3rom + gba/gen3live + nds/pages): each of the five
// ROMs read as the game panel reads it, a made-up save on top (a party from the game's first route and a wild battle
// there), every page drawn at the RG DS Plus's panel size into PNGs for a look. Skips a ROM that isn't there.
//   gba-pages-test <out dir> <rnds assets> <roms dir> [guide dir]
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"
#include "gba/gen3live.h"
#include "gba/gen3rom.h"
#include "gba/hacks.h"
#include "nds/guide.h"
#include "nds/logic.h"
#include "nds/pages.h"
#include "soft.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>

using namespace banana;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static bool savePng(const SoftBackend& be, int screen, const std::string& path)
{
	int w = be.width(), h = be.height();
	std::vector<uint8_t> rgb((size_t)w * h * 3);
	const uint32_t* p = be.pixels(screen);
	for (size_t i = 0; i < (size_t)w * h; i++) { rgb[i * 3] = (p[i] >> 16) & 255; rgb[i * 3 + 1] = (p[i] >> 8) & 255; rgb[i * 3 + 2] = p[i] & 255; }
	return stbi_write_png(path.c_str(), w, h, 3, rgb.data(), w * 3) != 0;
}

static std::vector<uint8_t> readFile(const std::string& p)
{
	std::ifstream f(p, std::ios::binary);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// a party member of a species at a level, with its level-up moves and plausible stats
static nds::Mon makeMon(const nds::RomData& r, uint16_t sp, int lv, uint32_t pid)
{
	nds::Mon m;
	m.pid = pid; m.species = sp; m.level = (uint8_t)lv; m.otId = 12345; m.otSecret = 54321;
	const nds::Species& s = r.species[sp];
	m.ability = s.abilities[0];
	m.nature = (uint8_t)(pid % 25);
	int k = 0;
	for (auto it = s.learnset.rbegin(); it != s.learnset.rend() && k < 4; ++it)
		if (it->level <= lv) m.moves[k++] = it->move;
	for (int i = 0; i < 4; i++) if (m.moves[i] && m.moves[i] < r.moves.size()) m.pp[i] = r.moves[m.moves[i]].pp;
	for (int i = 0; i < 6; i++) { m.ivs[i] = (uint8_t)((pid >> i) % 32); m.evs[i] = (uint8_t)(i * 7); }
	m.maxHp = (uint16_t)((2 * s.base[0] + m.ivs[0]) * lv / 100 + lv + 10);
	m.hp = m.maxHp * 3 / 4;
	for (int i = 0; i < 5; i++) m.stats[i] = (uint16_t)((2 * s.base[i + 1] + m.ivs[i + 1]) * lv / 100 + 5);
	m.friendship = 120;
	m.nick = s.name;
	return m;
}

int main(int argc, char** argv)
{
	if (argc < 4) { fprintf(stderr, "usage: gba-pages-test <out> <assets> <roms> [guide dir]\n"); return 2; }
	const std::string out = argv[1], assets = argv[2], roms = argv[3];
	if (argc > 4) setenv("BANANA_GBA_GUIDE", argv[4], 1);
	mkdir(out.c_str(), 0755);
	DIR* d = opendir(roms.c_str());
	if (!d) { fprintf(stderr, "gba-pages-test: no ROMs at %s: skipped\n", roms.c_str()); return 0; }
	std::vector<std::string> files;
	while (dirent* e = readdir(d)) { std::string n = e->d_name; if (n.size() > 4 && n.substr(n.size() - 4) == ".gba") files.push_back(roms + "/" + n); }
	closedir(d);
	int games = 0;
	for (const std::string& f : files)
	{
		auto bytes = std::make_shared<const std::vector<uint8_t>>(readFile(f));
		nds::Game g = gba::detectGame(bytes->data(), bytes->size());
		// (a pokeemerald-expansion hack by its CRC-32, until detectGame says HACK for them)
		if (const gba::HackInfo* h = g.id == nds::GameId::NONE ? gba::hackByCrc(gba::romCrc32(bytes->data(), bytes->size())) : nullptr)
			if (h->engine == gba::Engine::EXPANSION) { g.id = nds::GameId::HACK; g.gen = nds::Gen::III; g.code = h->base; g.title = h->title; g.hack = h->key; g.modern = true; }
		if (g.id == nds::GameId::NONE) continue;
		games++;
		auto t0 = std::chrono::steady_clock::now();
		pkmn::RomTables t = pkmn::findTables(bytes->data(), bytes->size());
		t.rom = bytes;
		std::shared_ptr<const nds::RomData> r = gba::readRomData(t, g, "");
		const double readMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		CHECK(r != nullptr, "%s: no RomData", f.c_str());
		if (!r) continue;
		nds::useModernTypeChart(r->game.modern);	// (as the host does: Fairy for a hack that has it)
		fprintf(stderr, "%s (%s): tables + RomData %.0f ms\n", g.title.c_str(), g.code.c_str(), readMs);
		nds::useModernTypeChart(r->game.modern);	// (as the host does: a hack's Fairy and Generation VI's chart)
		const std::string tag = g.hack.empty() ? g.code : g.hack;	// (the PNGs' names: a hack's key, its base game's code shared)
		auto gd = std::make_shared<nds::GuideData>();
		const nds::GuideResolver res = nds::guideResolver(*r);
		std::string why;
		if (!nds::loadGuide(r->game, *gd, &why, &res)) fprintf(stderr, "  no guide: %s\n", why.c_str());
		else fprintf(stderr, "  guide: %zu milestones, %zu steps, %zu secrets%s%s\n", gd->milestones.size(), gd->steps.size(),
			gd->secrets.size(), why.empty() ? "" : "; ", why.c_str());
		for (const auto& u : gd->unresolved) fprintf(stderr, "  unresolved place: %s\n", u.c_str());
		CHECK(gd->unresolved.empty() || gd->milestones.empty(), "%s: %zu places not in the ROM", g.title.c_str(), gd->unresolved.size());
		// the first zone with grass encounters: the player is there, a wild one of it in battle
		auto live = std::make_shared<nds::Live>();
		live->valid = true;
		live->trainerName = "MAY"; live->tid = 12345; live->sid = 54321;
		uint16_t zone = 0, wild = 0;
		for (const auto& z : r->zones)
			for (const auto& e : z.second.encounters)
				if (!wild && e.kind == nds::EncounterKind::GRASS && e.species) { zone = z.first; wild = e.species; }
		CHECK(wild != 0, "%s: no grass encounter in any zone", g.title.c_str());
		live->zone = zone;
		const bool kanto = g.id == nds::GameId::FIRERED || g.id == nds::GameId::LEAFGREEN || g.code == "BPRE";
		const uint16_t starters[3] = { (uint16_t)(kanto ? 6 : 257), 25, wild ? wild : 16 };
		for (int i = 0; i < 3; i++) live->party.push_back(makeMon(*r, starters[i], i == 0 ? 36 : 20, 0x1000 * (i + 3) + 7));
		live->money = 12345; live->playSeconds = 9 * 3600 + 45 * 60; live->badges = 0x0F;
		live->dexCaught.assign(49, 0); live->dexSeen.assign(49, 0);
		for (int n = 1; n <= 80; n++) { live->dexSeen[(n - 1) / 8] |= 1 << ((n - 1) % 8); if (n % 3 == 0) live->dexCaught[(n - 1) / 8] |= 1 << ((n - 1) % 8); }
		live->bag[nds::POCKET_BALLS] = { { 4, 10 }, { 3, 5 }, { 2, 1 } };
		live->bag[nds::POCKET_ITEMS] = { { gba::itemToGen4(13), 3 } };		// (Gen III's Potion)
		live->bag[nds::POCKET_TMS] = { { gba::itemToGen4(289 + 25), 1 } };	// (TM26 Earthquake)
		nds::Live inBattle = *live;
		nds::Battle& b = inBattle.battle;
		b.kind = nds::BattleKind::WILD;
		b.ours[0].mon = live->party[0]; b.ours[0].present = true;
		b.ours[0].types[0] = r->species[live->party[0].species].types[0]; b.ours[0].types[1] = r->species[live->party[0].species].types[1];
		b.foes[0].mon = makeMon(*r, wild, 5, 0xABCDE); b.foes[0].present = true;
		b.foes[0].types[0] = r->species[wild].types[0]; b.foes[0].types[1] = r->species[wild].types[1];
		b.turn = 1;
		const nds::AdviceState st;
		for (int pass = 0; pass < 2; pass++)
		{
			const nds::Live& l = pass ? inBattle : *live;
			nds::PageData pd;
			pd.rom = r; pd.guide = gd;
			pd.live = std::make_shared<nds::Live>(l);
			pd.advice = std::make_shared<nds::Advice>(nds::computeAdvice(*r, l, gd.get(), st));
			pd.journey = std::make_shared<nds::Journey>();
			SoftBackend be(1024, 768);
			nds::Pages pg(&be, 1, 1024, 768, assets, false);
			pg.setData(pd);
			const nds::Page pages[] = { nds::Page::BATTLE, nds::Page::PARTY, nds::Page::TEAM, nds::Page::HERE, nds::Page::MAP, nds::Page::GUIDE, nds::Page::DEX, nds::Page::LOG };
			for (nds::Page p : pages)
			{
				if (pass == 1 && p != nds::Page::BATTLE) continue;
				if (p == nds::Page::DEX) pg.showSpecies(live->party[0].species);
				pg.show(p);
				pg.render();
				pg.update(0);
				pg.render();
				const std::string name = out + "/" + tag + "-" + nds::pageName(p) + (pass ? "-wild" : "") + ".png";
				CHECK(savePng(be, 1, name), "%s", name.c_str());
			}
		}
	}
	if (!games) fprintf(stderr, "gba-pages-test: no GBA Pokémon ROM in %s: skipped\n", roms.c_str());
	if (fails) { fprintf(stderr, "gba-pages-test: %d FAILED\n", fails); return 1; }
	fprintf(stderr, "gba-pages-test: %d games drawn into %s\n", games, out.c_str());
	return 0;
}
