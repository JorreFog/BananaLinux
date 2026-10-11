#pragma once
// banana-ui: the Pixel engine's Backend in software. The engine already rasterizes everything (boxes, text, fitted
// pictures) into bitmaps at device size, so drawing is mostly copying them into place: textures are kept as
// premultiplied XRGB words (or A8 coverage for text), and a draw at its own size is a straight blend with no
// sampling. Each screen is composed in ordinary cached memory and only the rows that changed are copied to the
// panel's scanout buffer, which is uncached (reading it back to blend would be many times slower).
#include "../rnds/RndsUI.h"
#include <cstdint>
#include <cstring>
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
	};

	class SoftBackend : public rnds::Backend
	{
	public:
		SoftBackend(int w, int h);
		int width() const { return W; }
		int height() const { return H; }
		// the composed screen, 0xXXRRGGBB rows of W words
		const uint32_t* pixels(int screen) const { return scr[screen].data(); }
		// rows touched since the last call (y0 >= y1: none); clears the mark
		void takeDamage(int screen, int& y0, int& y1);
		// everything again (new scanout buffers): the next frame is drawn in full
		void invalidate() { for (int s = 0; s < 2; s++) { dmg0[s] = 0; dmg1[s] = H; last[s].clear(); } }
		// a screen's pixels to draw on directly (the boot animation): all of it counts as changed, and the engine's
		// next frame is drawn over all of it
		uint32_t* canvas(int screen) { dmg0[screen] = 0; dmg1[screen] = H; last[screen].clear(); return scr[screen].data(); }

		rnds::TexPtr upload(const rnds::Bitmap& b, bool smooth) override;
		void draw(const rnds::TexPtr& t, float x, float y, float w, float h, const rnds::Color& tint) override;
		void fill(float x, float y, float w, float h, const rnds::Color& c) override;
		void pushClip(float x, float y, float w, float h) override;
		void popClip() override;
		void beginScreen(int screen) override;
		void endScreen() override;

		// screens drawn this frame whose draw list matched the previous frame's (left as they were)
		int skipped = 0;

	private:
		// a screen's draws are recorded and only run at endScreen, when they differ from the last frame's: the same
		// list over the same (immutable) textures draws the same pixels
		struct Cmd
		{
			enum Op { DRAW, FILL, PUSH, POP } op;
			rnds::TexPtr tex;	// held, so its address can't be reused by a new texture while it is compared
			float x, y, w, h;
			float c[4];
			bool operator==(const Cmd& o) const
			{
				return op == o.op && tex == o.tex && x == o.x && y == o.y && w == o.w && h == o.h && !memcmp(c, o.c, sizeof c);
			}
		};
		std::vector<Cmd> rec[2], last[2];
		void run(const Cmd& c);
		void doDraw(const rnds::TexPtr& t, float x, float y, float w, float h, const rnds::Color& tint);
		void doFill(float x, float y, float w, float h, const rnds::Color& c);
		void doPushClip(float x, float y, float w, float h);
		struct Clip { int x0, y0, x1, y1; };
		int W, H, cur = 0;
		std::vector<uint32_t> scr[2];
		int dmg0[2], dmg1[2];
		std::vector<Clip> clips;
		void touch(int y0, int y1);
	};
}
