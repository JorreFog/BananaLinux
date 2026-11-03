// banana-ui tests: the software backend's background copy (soft.h: a screen's first draws kept composed when they
// stay the same) must never stand in for another screen's draws: after invalidate() (a game gave the panel back) the
// next frame is the new draws only. Found 2026-11-03 on the RG DS Plus: the GBA companion's pages lingered on the
// bottom panel after the game, the library drawn over them in pieces.
#include "soft.h"
#include <cstdio>

using namespace banana;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void frame(SoftBackend& be, const rnds::Color& back, const rnds::Color& top, float x)
{
	be.beginScreen(0);
	be.fill(0, 0, 160, 120, back);			// two full-screen layers: a background worth keeping composed
	be.fill(0, 0, 160, 120, back);
	be.fill(x, 10, 8, 8, top);				// something small that moves
	be.endScreen();
}

int main()
{
	SoftBackend be(160, 120);
	const rnds::Color green(0, 1, 0, 1), red(1, 0, 0, 1), blue(0, 0, 1, 1), white(1, 1, 1, 1);
	// a page that stays the same long enough for its background to be kept (the companion's, during a game)
	for (int i = 0; i < 200; i++) frame(be, green, red, (float)(i % 2));
	CHECK((be.pixels(0)[60 * 160 + 80] & 0xffffff) == 0x00ff00);
	// the game gives the panel back: everything again, another screen's draws (the library)
	be.invalidate();
	frame(be, blue, white, 100);
	CHECK((be.pixels(0)[60 * 160 + 80] & 0xffffff) == 0x0000ff);
	CHECK((be.pixels(0)[119 * 160 + 159] & 0xffffff) == 0x0000ff);
	// and the frames after it (drawn by what changed) stay the new screen's
	for (int i = 0; i < 5; i++) frame(be, blue, white, 100 + (float)i);
	CHECK((be.pixels(0)[60 * 160 + 80] & 0xffffff) == 0x0000ff);
	// a new screen without invalidate (the same backend drawn by another view): its own background, never the old one
	for (int i = 0; i < 200; i++) frame(be, green, red, (float)(i % 2));
	frame(be, blue, white, 50);
	CHECK((be.pixels(0)[60 * 160 + 80] & 0xffffff) == 0x0000ff);
	if (fails) { fprintf(stderr, "soft-base: %d FAILED\n", fails); return 1; }
	fprintf(stderr, "soft-base: all passed\n");
	return 0;
}
