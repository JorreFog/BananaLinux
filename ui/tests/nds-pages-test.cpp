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
#include <cstdio>
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
static void renderAll(const Ctx& c, int w, int h, const std::string& look, bool light, bool all)
{
	SoftBackend be(w, h);
	// (the menu's fallback font for what Pixelify has no glyph for, as banana-ui sets it)
	if (auto f = rnds::Font::load(c.assets + "/fonts/fusion-pixel-12px-proportional-zh_hans.ttf")) rnds::setFallbackFont(f, 12, h / 480.0f);
	Pages pg(&be, 1, w, h, c.assets, light);
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
	// opened: lands on the page an unseen notice is for (the Team page's first), and that notice is reported seen
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
	}
	// the tabs: each one's middle
	float cell = (512.0f - 8 - 12 - 12) / 7;
	for (int i = 0; i < 7; i++)
	{
		CHECK(tap(18 + i * cell + cell / 2, 448) == Pages::Action::REDRAW, "tab %d", i);
		CHECK(pg.page() == (Page)i, "tab %d gave page %d", i, (int)pg.page());
		pg.render();
	}
	// the Here notice: seen once its page was shown
	seen = pg.takeSeen();
	CHECK(seen.size() == 1 && seen[0] == "pt-hidden-ef", "Here's notice seen (%zu)", seen.size());
	// Back to game: its plate, and the icon's corner itself
	CHECK(tap(570, 450) == Pages::Action::CLOSE, "Back to game");
	CHECK(tap(636, 476) == Pages::Action::CLOSE, "the very corner");
	pg.setCorner((int)(560 * s), (int)(400 * s), (int)(80 * s), (int)(80 * s));
	pg.render();
	CHECK(tap(565, 405) == Pages::Action::CLOSE, "a bigger corner");
	pg.setCorner((int)(592 * s), (int)(432 * s), (int)(48 * s), (int)(48 * s));
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
	// no data at all: no crash, the tabs do nothing, Back to game still closes
	PageData empty; pg.setData(empty); pg.render();
	CHECK(tap(18 + cell / 2, 448) == Pages::Action::NONE, "no tabs without a game");
	CHECK(tap(570, 450) == Pages::Action::CLOSE, "Back to game without a game");
}

int main(int argc, char** argv)
{
	Ctx c;
	c.out = std::string(argc > 1 ? argv[1] : "tests/out") + "/nds-pages";
	c.assets = argc > 2 ? argv[2] : "assets/rnds";
	c.themes = argc > 3 ? argv[3] : "../system/rootfs/usr/share/banana/themes";
	mkdir(c.out.c_str(), 0755);
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
