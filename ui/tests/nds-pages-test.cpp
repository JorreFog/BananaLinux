// banana-ui tests: the DS Pokémon companion's pages (nds/pages.h) on made-up games (nds-fixture.h): every page and
// state drawn at both panel sizes and in a few themes into PNGs for a look (tests/out/nds-pages/), and the touch
// handling checked: tabs, rows, dragging, the Dex rail, Normal/Shiny, Back to game, notices and the sprites' frames.
//   nds-pages-test <out dir> <rnds assets> <themes dir>
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"
#include "nds-fixture.h"
#include "soft.h"
#include "theme.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sys/stat.h>

using namespace banana;
using namespace banana::nds;

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
static std::vector<uint32_t> grab(const SoftBackend& be, int screen)
{
	return std::vector<uint32_t>(be.pixels(screen), be.pixels(screen) + (size_t)be.width() * be.height());
}
static double ms(std::chrono::steady_clock::time_point a) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count(); }

struct Ctx { std::string out, assets, themes; };

// every page and state of both games in one look at one size
// the icon's place as banana-ui's overlay puts it (overlay.cpp: the top-right corner, 2 px of the look in, a 53x53 picture
// of the look) and BananaDS reports it back: Back to game goes there
static void setTopRight(Pages& pg, int w, int h)
{
	const float k = h / 480.0f;
	const int m = (int)std::lround(2 * k), S = (int)std::lround(44 * k), O = (int)std::lround(9 * k);
	pg.setCorner(w - m - (O + S), m, O + S, S + O);
}

static void renderAll(const Ctx& c, int w, int h, const std::string& look, bool light, bool all)
{
	SoftBackend be(w, h);
	// (the menu's fallback font for what Pixelify has no glyph for, as banana-ui sets it)
	if (auto f = rnds::Font::load(c.assets + "/fonts/fusion-pixel-12px-proportional-zh_hans.ttf")) rnds::setFallbackFont(f, 12, h / 480.0f);
	Pages pg(&be, 1, w, h, c.assets, light);
	setTopRight(pg, w, h);
	std::string pre = c.out + "/" + std::to_string(w) + "x" + std::to_string(h) + "-" + look + "-";
	auto shot = [&](const std::string& name)
	{
		auto t0 = std::chrono::steady_clock::now();
		pg.render();
		double first = ms(t0);
		t0 = std::chrono::steady_clock::now();
		pg.render();	// (the second frame, as the overlay redraws: cached texts and boxes)
		double again = ms(t0);
		fprintf(stderr, "render %s%s: %.1f ms, again %.1f ms\n", pre.c_str() + c.out.size() + 1, name.c_str(), first, again);
		CHECK(savePng(be, 1, pre + name + ".png"), "%s", name.c_str());
	};
	// a page scrolled by dragging: half way (mid) or to its end
	auto scrolled = [&](const std::string& name, float x, bool mid)
	{
		pg.render();
		for (int i = 0; i < (mid ? 1 : 6); i++)
		{
			pg.touchDown((int)(w * x / 640), h * 400 / 480, 0);
			pg.touchMove((int)(w * x / 640), h * (mid ? 200 : 70) / 480, 0);
			pg.touchUp((int)(w * x / 640), h * (mid ? 200 : 70) / 480, 0);
		}
		shot(name);
	};
	ndsfx::Fixture pt = ndsfx::platinum(), b2 = ndsfx::black2();
	pg.setData(pt.data());
	pg.show(Page::BATTLE); shot("pt-battle");
	// a tap on the foe's card: its type matchups (weak to, resists, immune) where the moves were; again: the moves
	auto tapAt = [&](float x, float y) { pg.touchDown((int)(w * x / 640), (int)(h * y / 480), 0); pg.touchUp((int)(w * x / 640), (int)(h * y / 480), 0); };
	auto same = [&](const std::string& a, const std::string& b)
	{
		std::ifstream fa(pre + a + ".png", std::ios::binary), fb(pre + b + ".png", std::ios::binary);
		std::string da((std::istreambuf_iterator<char>(fa)), {}), db((std::istreambuf_iterator<char>(fb)), {});
		return !da.empty() && da == db;
	};
	tapAt(470, 110); shot("pt-battle-matchups");
	CHECK(!same("pt-battle", "pt-battle-matchups"), "a tap on the foe's card changed nothing");
	tapAt(320, 300); shot("pt-battle-again");
	CHECK(same("pt-battle", "pt-battle-again"), "a tap on the matchups didn't bring the moves back");
	pg.show(Page::PARTY); shot("pt-party");
	pg.show(Page::TEAM); shot("pt-team");
	pg.show(Page::GUIDE); shot("pt-guide");
	pg.showSpecies(391); shot("pt-dex");
	if (!all) return;
	scrolled("pt-dex-mid", 420, true);
	scrolled("pt-dex-end", 420, false);
	pg.show(Page::GUIDE); scrolled("pt-guide-end", 320, false);
	pg.show(Page::TEAM); scrolled("pt-team-end", 320, false);
	pg.show(Page::LOG); shot("pt-log");
	// Here: in Eterna Forest at night (the Platinum fixture is in Eterna City, which has no wild Pokémon)
	{
		ndsfx::Fixture f = ndsfx::platinum();
		auto lv = std::make_shared<Live>(*f.live);
		lv->zone = ndsfx::Z_ETERNA_FOREST; lv->battle = Battle();
		lv->daycare = { lv->party[2], lv->party[3] };
		lv->eggSteps = 180;
		Roamer r; r.species = 481; r.zone = ndsfx::Z_R213; r.level = 50; r.hp = 1; r.active = true;
		lv->roamers = { r };
		f.live = lv;
		pg.setData(f.data());
		pg.show(Page::HERE); shot("pt-here");
		scrolled("pt-here-end", 320, false);
		pg.show(Page::BATTLE); shot("pt-battle-none");
		// the Party page scrolled to its end (the evolution watch, the next moves, the effort points here)
		pg.show(Page::PARTY);
		pg.render();
		pg.touchDown(w * 400 / 640, h * 380 / 480, 0); pg.touchMove(w * 400 / 640, h * 60 / 480, 0); pg.touchUp(w * 400 / 640, h * 60 / 480, 0);
		pg.touchDown(w * 400 / 640, h * 380 / 480, 0); pg.touchMove(w * 400 / 640, h * 60 / 480, 0); pg.touchUp(w * 400 / 640, h * 60 / 480, 0);
		shot("pt-party-scrolled");
	}
	pg.setData(pt.data());
	pg.showSpecies(391);
	pg.render();
	// (Shiny: the Dex's switch)
	pg.touchDown(w * 342 / 640, h * 241 / 480, 0); pg.touchUp(w * 342 / 640, h * 241 / 480, 0);
	shot("pt-dex-shiny");
	pg.touchDown(w * 286 / 640, h * 241 / 480, 0); pg.touchUp(w * 286 / 640, h * 241 / 480, 0);
	pg.showSpecies(493); shot("pt-dex-unseen");
	pg.showSpecies(29); shot("pt-dex-sign");
	// Black 2: a wild battle with the catch helper, challenge mode
	pg.setData(b2.data());
	pg.show(Page::BATTLE); shot("b2-battle-wild");
	if (all) { tapAt(470, 110); shot("b2-battle-wild-matchups"); tapAt(470, 110); }
	pg.show(Page::HERE); shot("b2-here");
	scrolled("b2-here-end", 320, false);
	pg.show(Page::LOG); shot("b2-log");
	pg.showSpecies(447); shot("b2-dex");
	pg.show(Page::PARTY); shot("b2-party");
	pg.show(Page::TEAM); shot("b2-team");
	// a double battle (two foes, compact cards)
	{
		ndsfx::Fixture f = ndsfx::platinum();
		auto lv = std::make_shared<Live>(*f.live);
		Battle& b = lv->battle;
		b.kind = BattleKind::DOUBLE_TRAINER;
		b.ours[1].mon = lv->party[1]; b.ours[1].types[0] = NORMAL; b.ours[1].types[1] = FLYING; b.ours[1].present = true;
		b.foes[1].mon = b.foeParty[0]; b.foes[1].mon.hp = 30; b.foes[1].types[0] = GRASS; b.foes[1].types[1] = GRASS; b.foes[1].present = true;
		f.live = lv;
		pg.setData(f.data());
		pg.show(Page::BATTLE); shot("pt-battle-double");
		tapAt(470, 150); shot("pt-battle-double-matchups");	// (the second foe's)
		CHECK(!same("pt-battle-double", "pt-battle-double-matchups"), "a tap on the second foe's card changed nothing");
		tapAt(470, 150);
	}
	// the states before the pages: reading the game, waiting for a save, no data
	{
		PageData d; d.reading = true; d.readProgress = 0.42f; d.readWhat = "Sprites: 212 of 493";
		pg.setData(d); shot("state-reading");
		PageData d2 = pt.data(); auto lv = std::make_shared<Live>(); d2.live = lv;
		pg.setData(d2); shot("state-waiting");
		PageData d3; pg.setData(d3); shot("state-nodata");
		ndsfx::Fixture f = ndsfx::platinum();
		f.journey = std::make_shared<Journey>();
		pg.setData(f.data()); pg.show(Page::LOG); shot("pt-log-empty");
	}
}

static void touchTests(const Ctx& c, int w, int h)
{
	SoftBackend be(w, h);
	Pages pg(&be, 1, w, h, c.assets, false);
	setTopRight(pg, w, h);
	float s = w / 640.0f;
	auto tap = [&](float x, float y) { int X = (int)(x * s), Y = (int)(y * s); pg.touchDown(X, Y, 0); return pg.touchUp(X, Y, 0); };
	auto drag = [&](float x, float y0, float y1)
	{
		pg.touchDown((int)(x * s), (int)(y0 * s), 0);
		for (int i = 1; i <= 8; i++) pg.touchMove((int)(x * s), (int)((y0 + (y1 - y0) * i / 8) * s), 0);
		return pg.touchUp((int)(x * s), (int)(y1 * s), 0);
	};
	ndsfx::Fixture pt = ndsfx::platinum();
	pg.setData(pt.data());
	CHECK(pg.unseen() == 2, "two notices unseen (%d)", pg.unseen());
	// opened in a battle: the Battle page, whatever the notices are for (HeartGold on the RG DS landed on Guide in a
	// wild battle), and those stay unseen
	pg.opened(0);
	CHECK(pg.page() == Page::BATTLE, "opened in a battle on page %d, not Battle", (int)pg.page());
	CHECK(pg.takeSeen().empty() && pg.unseen() == 2, "notices still unseen after a battle opening");
	// out of the battle: lands on the page an unseen notice is for (the Team page's first), and that notice is reported
	// seen
	{
		auto calm = std::make_shared<Live>(*pt.live);
		calm->battle = Battle{};
		PageData d = pt.data();
		d.live = calm;
		pg.setData(d);
	}
	pg.opened(0);
	CHECK(pg.page() == Page::TEAM, "opened on page %d, not Team", (int)pg.page());
	auto seen = pg.takeSeen();
	CHECK(seen.size() == 1 && seen[0] == "pt-catch-zubat", "seen %zu", seen.size());
	CHECK(pg.unseen() == 1, "one left after the Team page (%d)", pg.unseen());
	pg.render();
	// draw(): the same picture inside a frame the caller began (the overlay's)
	{
		auto a = grab(be, 1);
		be.beginScreen(1); pg.draw(); be.endScreen();
		CHECK(grab(be, 1) == a, "draw() in the overlay's frame");
		// the pages warmed (each drawn on the other screen): the shown one's picture and its touches are as they were
		int warmed = 0;
		while (pg.warmNext()) warmed++;
		CHECK(warmed == (int)Page::COUNT, "%d pages warmed", warmed);
		pg.render();
		CHECK(grab(be, 1) == a, "the shown page unchanged by the warming");
	}
	// the tabs: each one's middle (the whole width: Back to game is up in the icon's corner, the top-right one)
	float cell = (628.0f - 12 - 12) / (int)Page::COUNT;
	for (int i = 0; i < (int)Page::COUNT; i++)
	{
		CHECK(tap(18 + i * cell + cell / 2, 448) == Pages::Action::REDRAW, "tab %d", i);
		CHECK(pg.page() == (Page)i, "tab %d gave page %d", i, (int)pg.page());
		pg.render();
	}
	// the Here notice: seen once its page was shown
	seen = pg.takeSeen();
	CHECK(seen.size() == 1 && seen[0] == "pt-hidden-ef", "Here's notice seen (%zu)", seen.size());
	// Back to game: its plate in the header's top-right corner, and the icon's corner itself; the bottom-right corner is
	// the Log tab now
	CHECK(tap(530, 30) == Pages::Action::CLOSE, "Back to game");
	CHECK(tap(638, 2) == Pages::Action::CLOSE, "the very corner");
	CHECK(tap(600, 450) == Pages::Action::REDRAW && pg.page() == Page::LOG, "the bottom-right corner is a tab");
	CHECK(tap(470, 30) != Pages::Action::CLOSE, "left of Back to game: the header");
	CHECK(tap(600, 75) != Pages::Action::CLOSE, "under Back to game: the page");
	pg.setCorner((int)(560 * s), 0, (int)(80 * s), (int)(56 * s));
	pg.render();
	CHECK(tap(565, 5) == Pages::Action::CLOSE, "a wider corner");
	setTopRight(pg, w, h);
	// Party: a row picks a member
	pg.show(Page::PARTY); pg.render();
	{
		// the row under the finger is lit while it's down, and not once it lifts
		auto before = grab(be, 1);
		CHECK(pg.touchDown((int)(100 * s), (int)(205 * s), 0) == Pages::Action::REDRAW, "a press is shown");
		pg.render();
		CHECK(grab(be, 1) != before, "the pressed row lit");
		CHECK(pg.touchMove((int)(100 * s), (int)(260 * s), 0) == Pages::Action::REDRAW, "a drag unlights it");
		pg.touchUp((int)(100 * s), (int)(260 * s), 0);
		pg.render();
		CHECK(grab(be, 1) == before && pg.partySel() == 0, "nothing picked by a drag");
	}
	// (a tap between a warming and the next drawing goes to the shown page's rows: the warming drew another page)
	pg.setLook(false);
	pg.render();
	CHECK(pg.warmNext(), "a page warmed");
	CHECK(tap(100, 58 + 2 * 60 + 27) == Pages::Action::REDRAW && pg.partySel() == 2, "party row 3 (%d)", pg.partySel());
	pg.render();
	// a drag scrolls the member's page; a drag isn't a tap
	CHECK(pg.scrollMax(Page::PARTY) > 0, "the party page scrolls (max %.0f)", pg.scrollMax(Page::PARTY));
	drag(420, 380, 200);
	CHECK(pg.scrollOf(Page::PARTY) > 150 && pg.scrollOf(Page::PARTY) < 200, "dragged 180: scroll %.0f", pg.scrollOf(Page::PARTY));
	CHECK(pg.partySel() == 2, "a drag over the rows isn't a tap");
	drag(420, 400, 0); drag(420, 400, 0); drag(420, 400, 0);
	pg.render();
	CHECK(pg.scrollOf(Page::PARTY) == pg.scrollMax(Page::PARTY), "clamped at the end: %.0f of %.0f", pg.scrollOf(Page::PARTY), pg.scrollMax(Page::PARTY));
	drag(420, 70, 400); drag(420, 70, 400); drag(420, 70, 400);
	CHECK(pg.scrollOf(Page::PARTY) == 0, "back at the top: %.0f", pg.scrollOf(Page::PARTY));
	// Team: a Catch now row opens its Dex entry
	pg.show(Page::TEAM); pg.render();
	CHECK(tap(400, 56 + 2 + 232 + 26 + 20) == Pages::Action::REDRAW && pg.page() == Page::DEX && pg.dexSpecies() == 41,
		"catch-now row -> Dex %d (page %d)", pg.dexSpecies(), (int)pg.page());
	// Dex: Normal/Shiny, the picture changes
	pg.showSpecies(391); pg.render();
	auto normal = grab(be, 1);
	CHECK(!pg.shiny(), "starts Normal");
	CHECK(tap(342, 241) == Pages::Action::REDRAW && pg.shiny(), "Shiny");
	pg.render();
	CHECK(grab(be, 1) != normal, "the shiny picture differs");
	CHECK(tap(286, 241) == Pages::Action::REDRAW && !pg.shiny(), "Normal again");
	// the Pokédex bits as the RAM reader keeps them (species n at bit n - 1, ram.h): a species seen only through its own
	// bit shows its picture, the bit of the species after it doesn't (found on HeartGold on the Plus: Caterpie seen
	// showed as Blastoise)
	{
		ndsfx::Fixture f = ndsfx::platinum();
		auto dexOnly = [&](int bitIndex)
		{
			auto l = std::make_shared<Live>(*f.live);
			l->dexSeen.assign(64, 0); l->dexCaught.assign(64, 0);
			if (bitIndex >= 0) l->dexSeen[bitIndex >> 3] |= 1 << (bitIndex & 7);
			PageData d = f.data(); d.live = l; pg.setData(d);
			pg.showSpecies(41); pg.render();
			std::vector<uint32_t> detail;	// (the entry's panel only: the list beside it names the rows it has seen)
			const uint32_t* px = be.pixels(1);
			for (int y = (int)(60 * s); y < (int)(410 * s); y++)
				for (int x = (int)(224 * s); x < (int)(624 * s); x++) detail.push_back(px[(size_t)y * w + x]);
			return detail;
		};
		auto none = dexOnly(-1), own = dexOnly(40), next = dexOnly(41);
		CHECK(own != none, "species 41 seen through bit 40 shows it");
		CHECK(next == none, "bit 41 (species 42) leaves 41 unseen");
		pg.setData(pt.data());
	}
	// stat stages in Battler's order (HP, Atk, Def, Speed, SpA, SpD, Acc, Eva): the HP slot is never drawn, Speed is
	// drawn as Speed (found on HeartGold on the Plus: String Shot's Speed -1 showed as "SpA -1")
	{
		ndsfx::Fixture f = ndsfx::platinum();
		auto withStage = [&](int i, int v)
		{
			auto l = std::make_shared<Live>(*f.live);
			for (auto& st : l->battle.ours[0].stages) st = 0;
			if (i >= 0) l->battle.ours[0].stages[i] = (int8_t)v;
			PageData d = f.data(); d.live = l; pg.setData(d);
			pg.show(Page::BATTLE); pg.render();
			return grab(be, 1);
		};
		auto plain = withStage(-1, 0), hp = withStage(0, 1), spe = withStage(3, -1), spa = withStage(4, -1);
		CHECK(hp == plain, "the HP stage slot draws nothing");
		CHECK(spe != plain && spe != spa, "Speed -1 and SpA -1 are drawn differently");
		pg.setData(pt.data());
		pg.showSpecies(391);	// (back where the rail test below starts)
	}
	// the Guide's "along the way": a step of a later stage stays away even when it names this area (HeartGold's
	// post-game Embedded Tower step showed on Route 30 at the start)
	{
		ndsfx::Fixture f = ndsfx::platinum();
		auto guideWith = [&](bool extra)
		{
			auto g = std::make_shared<GuideData>(*f.guide);
			if (extra && !g->milestones.empty())
			{
				GuideStep later; later.id = "later-step"; later.title = "A post-game errand"; later.text = "Not yet.";
				later.stage = g->milestones.back().id; later.zones = { f.live->zone };
				g->steps.insert(g->steps.begin(), later);
			}
			PageData d = f.data(); d.guide = g; pg.setData(d);
			pg.show(Page::GUIDE); pg.render();
			return grab(be, 1);
		};
		CHECK(f.advice->next && f.guide->milestones.size() > 1, "the fixture has a next goal");
		CHECK(guideWith(true) == guideWith(false), "a later stage's step isn't shown");
		pg.setData(pt.data());
		pg.showSpecies(391);
	}
	// the notice that raised the badge stays on the page it opened after the logic has dropped it as seen (the session
	// sends Advice without seen notices a tick later: on Platinum "Shinx lives here" never showed on Team)
	{
		ndsfx::Fixture f = ndsfx::platinum();
		f.live->battle = Battle{};	// (out of the battle: a battle opens on Battle)
		PageData d = f.data();
		auto bare = std::make_shared<Advice>(*f.advice);
		bare->notices.clear();
		PageData without = d; without.advice = bare;
		pg.setData(without); pg.show(Page::TEAM); pg.render();
		auto never = grab(be, 1);
		pg.setData(d); pg.show(Page::GUIDE); pg.opened(0);
		CHECK(pg.page() == Page::TEAM, "opened on Team (%d)", (int)pg.page());
		auto taken = pg.takeSeen();
		without.seenNotices = taken;
		pg.setData(without); pg.render();
		CHECK(!taken.empty() && grab(be, 1) != never, "the notice is still drawn after Advice dropped it");
		pg.setData(pt.data());
		pg.showSpecies(391);
	}
	// Here lists a secret of this place that holds the zone among its zones (a town's houses), as the badge counts it
	// (White 2: "1 secret here" in the player's house in Aspertia, Here showed none)
	{
		ndsfx::Fixture f = ndsfx::platinum();
		auto hereWith = [&](bool extra)
		{
			auto g = std::make_shared<GuideData>(*f.guide);
			g->secrets.clear();
			if (extra)
			{
				Secret s1; s1.id = "town"; s1.text = "A gift in this town."; s1.zone = (uint16_t)(f.live->zone + 1); s1.zones = { f.live->zone };
				g->secrets.push_back(s1);
			}
			PageData d = f.data(); d.guide = g; pg.setData(d);
			pg.show(Page::HERE); pg.render();
			return grab(be, 1);
		};
		hereWith(false);	// (the first opening shows the fixture's notice card: past it, the page alone)
		auto a0 = hereWith(false), b = hereWith(true), a1 = hereWith(false);
		CHECK(a0 == a1 && b != a0, "a secret found through its zones is listed");
		pg.setData(pt.data());
		pg.showSpecies(391);
	}
	// the list's rail: its bottom is the end of the Pokédex
	pg.render();
	pg.touchDown((int)(199 * s), (int)(408 * s), 0); pg.touchUp((int)(199 * s), (int)(408 * s), 0);
	pg.render();
	pg.touchDown((int)(100 * s), (int)(395 * s), 0); pg.touchUp((int)(100 * s), (int)(395 * s), 0);
	CHECK(pg.dexSpecies() >= 480, "rail to the end, a row near 493 picked (%d)", pg.dexSpecies());
	// the evolution family's members are taps too
	pg.showSpecies(391); pg.render();
	// a tap that moved off its row picks nothing
	uint16_t before = pg.dexSpecies();
	pg.touchDown((int)(100 * s), (int)(100 * s), 0); pg.touchUp((int)(100 * s), (int)(103 * s), 0);
	(void)before;

	// the sprites' frames: Black 2's animate (the Sprite steps) and wake the overlay when they change
	ndsfx::Fixture b2 = ndsfx::black2();
	pg.setData(b2.data());
	pg.showSpecies(447);
	pg.update(1000); pg.render();
	auto f0 = grab(be, 1);
	int wake = pg.nextWake(1000);
	CHECK(wake > 0 && wake <= 160, "next frame in %d ms", wake);
	CHECK(!pg.update(1000 + wake - 1) && pg.update(1000 + wake), "update says when");
	pg.render();
	CHECK(grab(be, 1) != f0, "the next frame is drawn");
	// nothing animated: no wake
	PageData none; none.reading = true; pg.setData(none); pg.render();
	CHECK(pg.nextWake(0) == -1, "no animation, no wake (%d)", pg.nextWake(0));
	// momentum: a quick drag on the Dex list flings it on after the finger lifts, slowing down and stopping (the RG DS
	// validation: a 320 px drag moved 9 rows of 493, no fling); a slow drag doesn't; a tap while it flings fast only
	// stops it on the list, a tap anywhere else (a tab) is taken (B2: a tab right after a fast drag needed a second tap)
	{
		pg.setData(pt.data());
		pg.showSpecies(1); pg.render();
		auto list = [&] { return pg.scrollOf(Page::DEX, 0); };
		auto flick = [&](float y0, float y1, double t0, double ms)
		{
			pg.touchDown((int)(100 * s), (int)(y0 * s), t0);
			for (int i = 1; i <= 6; i++) pg.touchMove((int)(100 * s), (int)((y0 + (y1 - y0) * i / 6) * s), t0 + ms * i / 6);
			pg.touchUp((int)(100 * s), (int)(y1 * s), t0 + ms);
		};
		flick(380, 200, 1000, 90);	// 180 px in 90 ms: 2 px a ms
		const float atUp = list();
		CHECK(atUp > 150 && atUp < 200, "the drag itself: %.0f", atUp);
		double t = 1100;
		CHECK(pg.update(t) && pg.nextWake(t) >= 0 && pg.nextWake(t) <= 20, "flinging: frames wanted (%d)", pg.nextWake(t));
		int frames = 0;
		while (pg.update(t += 16) && frames < 1000) { pg.render(); frames++; }
		const float flung = list();
		CHECK(flung > atUp + 300, "flung on: %.0f after %.0f", flung, atUp);
		CHECK(frames > 20 && frames < 400, "and stopped (%d frames)", frames);
		CHECK(pg.nextWake(t) == -1 || pg.nextWake(t) > 100, "no frames wanted once it stopped (%d)", pg.nextWake(t));
		// a slow drag (the finger stops before it lifts): no fling
		pg.touchDown((int)(100 * s), (int)(380 * s), 5000);
		pg.touchMove((int)(100 * s), (int)(300 * s), 5100);
		pg.touchMove((int)(100 * s), (int)(300 * s), 5400);
		pg.touchUp((int)(100 * s), (int)(300 * s), 5450);
		const float slow = list();
		CHECK(!pg.update(5500) && !pg.update(5600) && list() == slow, "a slow drag stays where it was let go");
		// a tap on the list while it flings fast stops it and picks nothing
		const uint16_t sel = pg.dexSpecies();
		flick(380, 200, 6000, 90);
		pg.update(6100); pg.update(6116);
		pg.touchDown((int)(100 * s), (int)(200 * s), 6120); pg.touchUp((int)(100 * s), (int)(200 * s), 6180);
		const float stopped = list();
		CHECK(pg.dexSpecies() == sel, "the stopping tap picked nothing (%d)", pg.dexSpecies());
		CHECK(!pg.update(6300) && list() == stopped, "stopped by the tap");
		pg.render();
		CHECK(tap(100, 200) == Pages::Action::REDRAW && pg.dexSpecies() != sel, "the next tap picks a row");
		// a tab tapped while it flings: taken at once
		pg.render();
		flick(380, 200, 7000, 90);
		pg.update(7100); pg.update(7116); pg.render();
		pg.touchDown((int)((18 + 1 * cell + cell / 2) * s), (int)(448 * s), 7120);
		CHECK(pg.touchUp((int)((18 + 1 * cell + cell / 2) * s), (int)(448 * s), 7170) == Pages::Action::REDRAW && pg.page() == Page::PARTY,
			"a tab right after a fast drag (page %d)", (int)pg.page());
		pg.render();
	}
	// no data at all: no crash, the tabs do nothing, Back to game still closes
	PageData empty; pg.setData(empty); pg.render();
	CHECK(tap(18 + cell / 2, 448) == Pages::Action::NONE, "no tabs without a game");
	CHECK(tap(530, 30) == Pages::Action::CLOSE, "Back to game without a game");
}

int main(int argc, char** argv)
{
	Ctx c;
	c.out = std::string(argc > 1 ? argv[1] : "tests/out") + "/nds-pages";
	c.assets = argc > 2 ? argv[2] : "assets/rnds";
	c.themes = argc > 3 ? argv[3] : "../system/rootfs/usr/share/banana/themes";
	mkdir(c.out.c_str(), 0755);
	// Gen V's season: the game's own isn't read (not found in RAM), so Here shows which season its odds are for (the
	// handheld's month to begin with) and a tap picks the one the game shows (Black 2 loaded in summer on 2 November)
	for (int w : { 640, 1024 })
	{
		const int h = w * 3 / 4;
		const float sc = w / 640.0f;
		SoftBackend be(w, h);
		Pages pg(&be, 1, w, h, c.assets, false);
		setTopRight(pg, w, h);
		ndsfx::Fixture b2 = ndsfx::black2();
		pg.setData(b2.data());
		pg.show(Page::HERE); pg.render();
		CHECK(pg.season() == 2 && pg.seasonPick() < 0, "Here's odds for the handheld's season (%d)", pg.season());
		auto autumn = grab(be, 1);
		CHECK(savePng(be, 1, c.out + "/" + std::to_string(w) + "x" + std::to_string(h) + "-dark-b2-here-season.png"), "season png");
		// the four seasons in a row at the top of the list (the first row: no notice here), Summer second
		const float cw = 4 * 78, x0 = 628 - cw;
		pg.touchDown((int)((x0 + 78 + 39) * sc), (int)(78 * sc), 0); pg.touchUp((int)((x0 + 78 + 39) * sc), (int)(78 * sc), 0);
		pg.render();
		CHECK(pg.season() == 1 && pg.seasonPick() == 1, "Summer picked (%d, %d)", pg.season(), pg.seasonPick());
		CHECK(grab(be, 1) != autumn, "the odds drawn again for summer");
		CHECK(savePng(be, 1, c.out + "/" + std::to_string(w) + "x" + std::to_string(h) + "-dark-b2-here-summer.png"), "summer png");
		// a game without seasons (Platinum): no switch
		ndsfx::Fixture ptf = ndsfx::platinum();
		pg.setData(ptf.data()); pg.show(Page::HERE); pg.render();
		CHECK(pg.season() < 0, "no season in Gen IV (%d)", pg.season());
	}
	{	// a goal's name with its place: the place once (Platinum's Team page said "Rival Barry: Route 203, Route 203")
		Milestone m; m.name = "Rival Barry: Route 203"; m.place = "Route 203";
		CHECK(goalWithPlace(m) == "Rival Barry: Route 203", "%s", goalWithPlace(m).c_str());
		m.name = "Gardenia"; m.place = "Eterna City";
		CHECK(goalWithPlace(m) == "Gardenia, Eterna City", "%s", goalWithPlace(m).c_str());
		m.name = "Rival: Cheren at the gate"; m.place = "";
		CHECK(goalWithPlace(m) == "Rival: Cheren at the gate", "%s", goalWithPlace(m).c_str());
		m.name = "Team Plasma in the Dreamyard"; m.place = "dreamyard";
		CHECK(goalWithPlace(m) == "Team Plasma in the Dreamyard", "%s", goalWithPlace(m).c_str());
		// (a town's name without its "City": Black 2's Team page said "Plasma Sage Rood in Driftveil, Driftveil City")
		m.name = "Plasma Sage Rood in Driftveil"; m.place = "Driftveil City";
		CHECK(goalWithPlace(m) == "Plasma Sage Rood in Driftveil", "%s", goalWithPlace(m).c_str());
		m.name = "Lake Verity's legend"; m.place = "Lake Valor";
		CHECK(goalWithPlace(m) == "Lake Verity's legend, Lake Valor", "%s", goalWithPlace(m).c_str());
	}
	// the growth rates at level 100 (and medium fast at 10)
	CHECK(expForLevel(0, 10) == 1000 && expForLevel(0, 100) == 1000000, "medium fast");
	CHECK(expForLevel(1, 100) == 600000 && expForLevel(2, 100) == 1640000, "erratic, fluctuating");
	CHECK(expForLevel(3, 100) == 1059860 && expForLevel(4, 100) == 800000 && expForLevel(5, 100) == 1250000, "medium slow, fast, slow");
	CHECK(expForLevel(3, 1) == 0 && expForLevel(3, 2) == 9, "medium slow's start (%u)", expForLevel(3, 2));

	applyTheme("dark", "yellow");
	touchTests(c, 640, 480);
	touchTests(c, 1024, 768);
	renderAll(c, 640, 480, "dark", false, true);
	renderAll(c, 1024, 768, "dark", false, true);
	// the theme's palette and accent: light, a user theme (Ocean), another accent
	bool l = applyTheme("light", "yellow") == "light";
	renderAll(c, 1024, 768, "light", l, false);
	renderAll(c, 640, 480, "light", l, false);
	std::string v = applyTheme("file:" + c.themes + "/ocean.json", "yellow");
	renderAll(c, 1024, 768, "ocean", v == "light", false);
	v = applyTheme("dark", "pink");
	renderAll(c, 640, 480, "pink", false, false);
	fprintf(stderr, "%s: %d failed\n", fails ? "FAIL" : "ok", fails);
	return fails ? 1 : 0;
}
