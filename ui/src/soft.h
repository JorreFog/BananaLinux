#pragma once
// banana-ui: the Pixel engine's Backend in software. The engine already rasterizes everything (boxes, text, fitted
// pictures) into bitmaps at device size, so drawing is mostly copying them into place: textures are kept as
// premultiplied XRGB words (or A8 coverage for text), and a draw at its own size is a straight blend with no
// sampling. Each screen is composed in ordinary cached memory and only the part that changed is copied to the
// panel's scanout buffer, which is uncached (reading it back to blend would be many times slower).
#include "../rnds/RndsUI.h"
#include "kms.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace banana
{
	struct SoftTex : rnds::Tex
	{
		std::vector<uint32_t> px;	// premultiplied 0xAARRGGBB (RGBA textures)
		std::vector<uint8_t> a8;	// coverage (alpha textures)
		bool opaque = false;		// every pixel has alpha 255: plain copies
		bool smooth = false;
		// each row as runs of one kind: clear (skipped), solid (copied, or filled for alpha textures), mixed (blended).
		// A run is (kind << 30) | length; row y's runs are runs[rowAt[y] .. rowAt[y + 1])
		enum { CLEAR = 0, SOLID = 1, MIXED = 2 };
		std::vector<uint32_t> runs;
		std::vector<uint32_t> rowAt;
		// drawn at another size: what a scaled draw samples depends only on its size and where its first pixel's
		// centre falls in the texture (the sub-pixel phase), so a draw seen again at a size and phase (a tile at rest,
		// or moving by whole pixels) keeps its samples and is copied from then on. The last few, most recent first;
		// an entry with no samples was seen once
		struct Scaled
		{
			double phX, phY; float w, h; int bw, bh;
			std::shared_ptr<SoftTex> tex;
			bool same(const Scaled& o) const { return phX == o.phX && phY == o.phY && w == o.w && h == o.h && bw == o.bw && bh == o.bh; }
		};
		std::vector<Scaled> scaled;
		// a smooth RGBA texture's rows blended down to a draw's rows (each texel column's two rows by the row's
		// fraction, channels in 32-bit lanes: red and blue, then alpha and green), for the last height and vertical
		// phase it was drawn at (the last two: the shelf draws its tiles at rest and the selected one, growing): a draw
		// moving sideways only blends across columns
		struct Rows { double phY = -1; float h = -1; int rows = 0; std::vector<uint64_t> lanes; };
		Rows vRows[2];
	};

	class SoftBackend : public rnds::Backend
	{
	public:
		SoftBackend(int w, int h);
		int width() const { return W; }
		int height() const { return H; }
		// the composed screen, 0xXXRRGGBB rows of W words
		const uint32_t* pixels(int screen) const { return scr[screen].data(); }
		// the part touched since the last call (empty: none); clears the mark
		Damage takeDamage(int screen) { Damage d = dmg[screen]; dmg[screen] = Damage(); return d; }
		// everything again (new scanout buffers): the next frame is drawn in full
		void invalidate() { for (int s = 0; s < 2; s++) { dmg[s] = { 0, 0, W, H }; last[s].clear(); full[s] = true; } }
		// a screen's pixels to draw on directly (the boot animation): all of it counts as changed, and the engine's
		// next frame is drawn over all of it
		uint32_t* canvas(int screen) { dmg[screen] = { 0, 0, W, H }; last[screen].clear(); full[screen] = true; return scr[screen].data(); }

		// (touches nothing of the backend's but the new texture: callable from the picture workers)
		rnds::TexPtr upload(const rnds::Bitmap& b, bool smooth) override;
		void draw(const rnds::TexPtr& t, float x, float y, float w, float h, const rnds::Color& tint) override;
		void fill(float x, float y, float w, float h, const rnds::Color& c) override;
		void pushClip(float x, float y, float w, float h) override;
		void popClip() override;
		void beginScreen(int screen) override;
		void endScreen() override;

		// drawn over every screen just before it ends (the menu's notices), so it is part of the screen's draw list
		std::function<void(int screen)> overlay;

		// screens drawn this frame whose draw list matched the previous frame's (left as they were)
		int skipped = 0;
		// pixels redrawn (the changed rectangles' area), for the benchmark
		uint64_t redrawn = 0;

	private:
		// a screen's draws are recorded and only run at endScreen. The list is compared with the last frame's, draw by
		// draw (each with the rectangle it can touch, its clip resolved): the same draws over the same (immutable)
		// textures in the same order draw the same pixels, so only the rectangles of the draws that came, went or
		// changed are cleared and drawn again, with every draw that touches them clipped to them
		struct Cmd
		{
			enum Op { DRAW, FILL, PUSH, POP } op;
			rnds::TexPtr tex;	// held, so its address can't be reused by a new texture while it is compared
			float x, y, w, h;
			float c[4];
		};
		struct Clip { int x0, y0, x1, y1; };
		struct Item
		{
			Cmd c;
			Clip box;	// the pixels it can touch: its rectangle in its clip
			bool operator==(const Item& o) const
			{
				return c.op == o.c.op && c.tex == o.c.tex && c.x == o.c.x && c.y == o.c.y && c.w == o.c.w && c.h == o.c.h &&
					!memcmp(c.c, o.c.c, sizeof c.c) && box.x0 == o.box.x0 && box.y0 == o.box.y0 && box.x1 == o.box.x1 && box.y1 == o.box.y1;
			}
		};
		std::vector<Cmd> rec[2];
		std::vector<Item> items, last[2];
		std::vector<Clip> dirty;
		bool full[2] = { true, true };
		void resolve();
		Clip bounds(const Cmd& c, const Clip& clip) const;
		void addDirty(const Clip& r);
		void redraw(const Clip& r);
		const uint64_t* vertical(SoftTex* t, float y, float h, int64_t stepY, int by0, int by1);
		void columns(const SoftTex* t, float x, int64_t stepX, int bx0, int x0, int n);
		void sample(const SoftTex* t, float x, float y, float w, float h, int x0, int y0, int x1, int y1, uint32_t* px, uint8_t* a8, int stride);
		void blitRuns(const SoftTex* t, int bx0, int by0, int x0, int y0, int x1, int y1, uint32_t tr, uint32_t tg, uint32_t tb, uint32_t ta, uint32_t tintWord);
		void doDraw(SoftTex* t, float x, float y, float w, float h, const rnds::Color& tint, const Clip& cl);
		void doFill(float x, float y, float w, float h, const rnds::Color& c, const Clip& cl);
		int W, H, cur = 0;
		std::vector<uint32_t> scr[2];
		Damage dmg[2];
		std::vector<Clip> clips;
		std::vector<int> colA, colB;	// a scaled draw's source columns (and their blend) for each pixel of a row
		std::vector<uint32_t> colF;
		void touch(const Clip& r) { dmg[cur].add({ r.x0, r.y0, r.x1, r.y1 }); }
	};
}
