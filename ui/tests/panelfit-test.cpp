// panelfit's checks: the menu's canvas on panels of another size or way round (src/panelfit.h). The AYN Thor's two
// natively portrait panels (1080x1920 top, 1080x1240 bottom, both turned 90), the RG DS's identity, every rotation:
// where a canvas pixel lands, the black bars, a touch back to the canvas, and a damaged rectangle's blit covering
// exactly what a full blit would change.
#include "panelfit.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace banana;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); fails++; } } while (0)

static std::vector<uint32_t> canvasPattern(int w, int h)
{
	std::vector<uint32_t> c((size_t)w * h);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) c[(size_t)y * w + x] = 0xff000000u | (uint32_t)(x << 12) | (uint32_t)y;
	return c;
}

// the panel's buffer after a blit of the whole canvas
static std::vector<uint32_t> fullBlit(const PanelFit& f, const std::vector<uint32_t>& c, int lw, int lh)
{
	// (the scanout buffers are zeroed when made, kms.cpp: the bars are never written and stay black)
	std::vector<uint32_t> p((size_t)f.panelW() * f.panelH(), 0);
	Damage d; d.x1 = lw; d.y1 = lh;
	f.blit(c.data(), (uint8_t*)p.data(), f.panelW() * 4, d);
	return p;
}

static void geometry(int pw, int ph, int rot, int lw, int lh)
{
	PanelFit f;
	f.set(lw, lh, pw, ph, rot);
	auto c = canvasPattern(lw, lh);
	auto p = fullBlit(f, c, lw, lh);
	// the picture's pixels all written (the pattern is never 0), the bars' none
	size_t written = 0;
	for (uint32_t v : p) written += v != 0;
	CHECK(written == (size_t)f.dw * f.dh, "%dx%d rot %d: %zu panel pixels written, the picture is %dx%d", pw, ph, rot, written, f.dw, f.dh);
	// a touch on the panel back to the canvas reads the pixel the panel shows there
	int bad = 0, in = 0;
	for (int py = 0; py < ph; py += 7)
		for (int px = 0; px < pw; px += 5)
		{
			float x, y;
			if (!f.toCanvas(px + 0.5f, py + 0.5f, x, y))
			{
				if (p[(size_t)py * pw + px] != 0) bad++;
				continue;
			}
			in++;
			uint32_t want = c[(size_t)(int)y * lw + (int)x], got = p[(size_t)py * pw + px];
			// nearest-neighbour rounding: within a canvas pixel
			int dx = (int)((got >> 12) & 0xfff) - (int)((want >> 12) & 0xfff), dy = (int)(got & 0xfff) - (int)(want & 0xfff);
			if (abs(dx) > 1 || abs(dy) > 1) bad++;
		}
	CHECK(bad == 0, "%dx%d rot %d: %d touches read another pixel than the panel shows", pw, ph, rot, bad);
	CHECK(in > 0, "%dx%d rot %d: no touch landed on the picture", pw, ph, rot);
	// a damaged rectangle's blit onto the full picture changes nothing a full blit wouldn't, and covers its part
	std::vector<uint32_t> part(p.size(), 0);
	Damage d; d.x0 = lw / 3; d.y0 = lh / 4; d.x1 = lw / 3 + 37; d.y1 = lh / 4 + 23;
	f.blit(c.data(), (uint8_t*)part.data(), pw * 4, d);
	Damage pd = f.toPanel(d);
	CHECK(!pd.empty(), "%dx%d rot %d: damage maps to nothing", pw, ph, rot);
	int wrong = 0;
	for (int y = pd.y0; y < pd.y1; y++)
		for (int x = pd.x0; x < pd.x1; x++)
			if (part[(size_t)y * pw + x] != p[(size_t)y * pw + x]) wrong++;
	CHECK(wrong == 0, "%dx%d rot %d: %d pixels of a partial blit differ from the full one", pw, ph, rot, wrong);
	// the canvas's damaged pixels all inside the panel rectangle: their centres' panel pixels
	int outside = 0;
	for (int y = d.y0; y < d.y1; y++)
		for (int x = d.x0; x < d.x1; x++)
		{
			uint32_t v = c[(size_t)y * lw + x];
			bool found = false;
			for (int py = pd.y0; py < pd.y1 && !found; py++)
				for (int px = pd.x0; px < pd.x1 && !found; px++) found = part[(size_t)py * pw + px] == v;
			if (!found && f.s >= 1) outside++;
		}
	CHECK(outside == 0, "%dx%d rot %d: %d damaged canvas pixels not in the panel's damage", pw, ph, rot, outside);
}

int main()
{
	// the RG DS: the canvas is the panels, the fit the identity
	{
		int lw, lh;
		canvasFor(640, 480, 0, 640, 480, 0, lw, lh);
		CHECK(lw == 640 && lh == 480, "RG DS canvas %dx%d", lw, lh);
		PanelFit f;
		f.set(640, 480, 640, 480, 0);
		CHECK(f.identity(), "RG DS fit isn't the identity");
	}
	// the AYN Thor: 1080x1920 top and 1080x1240 bottom, both turned 90
	{
		int lw, lh;
		canvasFor(1080, 1920, 90, 1080, 1240, 90, lw, lh);
		CHECK(lw == 1440 && lh == 1080, "Thor canvas %dx%d, not 1440x1080", lw, lh);
		PanelFit top, bottom;
		top.set(lw, lh, 1080, 1920, 90);
		bottom.set(lw, lh, 1080, 1240, 90);
		CHECK(!top.identity() && !bottom.identity(), "Thor fits are the identity");
		CHECK(top.dw == 1440 && top.dh == 1080 && top.ox == 240 && top.oy == 0, "Thor top fit %dx%d+%d+%d", top.dw, top.dh, top.ox, top.oy);
		CHECK(bottom.dw == 1240 && bottom.dh == 930 && bottom.ox == 0 && bottom.oy == 75, "Thor bottom fit %dx%d+%d+%d", bottom.dw, bottom.dh, bottom.ox, bottom.oy);
		// turned 90 clockwise: the canvas's top-left corner at the panel's top-right, past the bars
		auto c = canvasPattern(lw, lh);
		auto p = fullBlit(top, c, lw, lh);
		CHECK(p[(size_t)240 * 1080 + 1079] == c[0], "Thor top: the canvas's corner isn't at the panel's top right");
		CHECK(p[0] == 0, "Thor top: the bar isn't black");
	}
	// touches: a touchscreen reporting the portrait panel's pixels, and one the device tree turned already (upright
	// range), land on the same canvas point
	{
		PanelFit f;
		f.set(1440, 1080, 1080, 1920, 90);
		float ax, ay, bx, by;
		// the canvas's centre: upright (960, 540), on the portrait panel (1079 - 540, 960)
		bool a = f.touchToCanvas(539.5f / 1080, 960.5f / 1920, false, ax, ay);
		bool b = f.touchToCanvas(960.5f / 1920, 540.5f / 1080, true, bx, by);
		CHECK(a && b, "Thor touch at the centre fell in the bars (%d %d)", a, b);
		CHECK(std::fabs(ax - 720) < 2 && std::fabs(ay - 540) < 2, "panel-space touch at %.1f,%.1f, not 720,540", ax, ay);
		CHECK(std::fabs(bx - 720) < 2 && std::fabs(by - 540) < 2, "upright-space touch at %.1f,%.1f, not 720,540", bx, by);
		CHECK(!f.touchToCanvas(100.0f / 1920, 0.5f, true, bx, by), "a touch in the left bar landed on the canvas");
	}
	for (int rot : { 0, 90, 180, 270 })
	{
		geometry(1080, 1920, rot, 1440, 1080);
		geometry(1080, 1240, rot, 1440, 1080);
		geometry(1024, 768, rot, 640, 480);
	}
	geometry(640, 480, 0, 640, 480);
	if (fails) { fprintf(stderr, "panelfit-test: %d failures\n", fails); return 1; }
	fprintf(stderr, "panelfit-test: ok\n");
	return 0;
}
