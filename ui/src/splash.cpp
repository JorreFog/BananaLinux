#include "splash.h"
#include "../rnds/RndsUI.h"
#include <algorithm>
#include <cmath>

namespace banana
{
	// timings (ms)
	static constexpr double WORD_AT = 560, LETTER = 60, LOAD_AT = 600, DOT = 240;
	static constexpr double SHINE_AT = 1000, SHINE = 300;

	// the layout, in CSS px of a 640x480 panel
	static constexpr float BUNCH_Y = 96, WORD_Y = 268, LOAD_Y = 342, DOTS_Y = 368;

	static inline uint32_t mul255(uint32_t p, uint32_t k)
	{
		uint32_t rb = (p & 0x00ff00ffu) * k + 0x00800080u;
		rb = ((rb + ((rb >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
		uint32_t ag = ((p >> 8) & 0x00ff00ffu) * k + 0x00800080u;
		ag = (ag + ((ag >> 8) & 0x00ff00ffu)) & 0xff00ff00u;
		return rb | ag;
	}

	static inline uint32_t over(uint32_t s, uint32_t d)
	{
		uint32_t a = s >> 24;
		if (a == 255) return s;
		if (a == 0) return d;
		return s + mul255(d, 255 - a);
	}

	static bool loadRgba(const std::string& path, std::vector<uint32_t>& px, int& w, int& h)
	{
		rnds::Bitmap b = rnds::decodeFile(path);
		if (b.empty()) return false;
		w = b.w; h = b.h;
		px.resize((size_t)w * h);
		for (size_t i = 0; i < px.size(); i++)
		{
			const uint8_t* s = &b.px[i * 4];
			uint32_t a = s[3];
			auto m = [a](uint32_t v) { uint32_t x = v * a + 128; return (x + (x >> 8)) >> 8; };
			px[i] = rnds::tintAccent((a << 24) | (m(s[0]) << 16) | (m(s[1]) << 8) | m(s[2]));	// the accent's banana
		}
		return true;
	}

	// a word's mask (white on black), with each letter's first column found from the empty columns between them
	static bool loadMask(const std::string& path, int& w, int& h, std::vector<uint8_t>& a, std::vector<int>& letters)
	{
		rnds::Bitmap b = rnds::decodeFile(path);
		if (b.empty()) return false;
		w = b.w; h = b.h;
		a.resize((size_t)w * h);
		for (size_t i = 0; i < a.size(); i++) a[i] = b.px[i * 4] > 127 ? 255 : 0;
		letters.clear();
		bool prev = false;
		for (int x = 0; x < w; x++)
		{
			bool any = false;
			for (int y = 0; y < h; y++) any |= a[(size_t)y * w + x] != 0;
			if (any && !prev) letters.push_back(x);
			prev = any;
		}
		return true;
	}

	bool Splash::load(const std::string& dir, const std::string& banana, const std::string& variant, int w, int h)
	{
		// the composition is laid out in CSS px of a 640x480 panel, so it is the width that fixes its scale (1.9375 on
		// the RPC's 1240x1080); a panel taller than 480*s keeps the background's
		// dots below the console, which is what the artwork is behind the console anyway
		W = w; H = h; s = w / 640.0f;
		oy = std::max(0, (H - (int)std::lround(480 * s)) / 2);
		light = variant == "light";
		K = std::max(1, (int)std::lround(3 * s));
		KW = std::max(1, (int)std::lround(6 * s));
		KL = std::max(1, (int)std::lround(2 * s));
		std::string size = w > 640 ? "1024" : "640", pal = light ? "light" : "dark";
		int bw, bh;
		if (!loadRgba(dir + "/" + pal + "-" + size + "-top.png", bgTop, bw, bh) || !bgTop.size()) return false;
		// a panel the assets weren't made for (the RPC's 1240x1080): the background is stretched to it, dots and all
		if (bw != W || bh != H)
		{
			for (auto* bg : { &bgTop })
			{
				std::vector<uint32_t> o((size_t)W * H);
				for (int y = 0; y < H; y++)
					for (int x = 0; x < W; x++)
						o[(size_t)y * W + x] = (*bg)[(size_t)((int64_t)y * bh / H) * bw + (int64_t)x * bw / W];
				bg->swap(o);
			}
		}
		if (!loadRgba(banana, bunch.px, bunch.w, bunch.h)) return false;
		if (!loadMask(dir + "/word.png", word.w, word.h, word.a, word.letters)) return false;
		if (!loadMask(dir + "/loading.png", loading.w, loading.h, loading.a, loading.letters)) return false;
		ink = light ? 0xff2a2620u : 0xfff1ede2u;
		muted = light ? 0xff7a7262u : 0xffa39d8eu;
		return true;
	}

	int Splash::X(float v) const { return (int)std::lround(v * s); }

	void Splash::fill(uint32_t* d, int x, int y, int w, int h, uint32_t c) const
	{
		int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(W, x + w), y1 = std::min(H, y + h);
		for (int yy = y0; yy < y1; yy++)
			for (int xx = x0; xx < x1; xx++) d[(size_t)yy * W + xx] = over(c, d[(size_t)yy * W + xx]);
	}

	// the bunch at K device px per pixel; shine: the light band's position over the sprite, 0..1 (outside: none)
	void Splash::drawBunch(uint32_t* d, int x, int y, double shine) const
	{
		int band = shine >= 0 && shine <= 1 ? (int)std::lround(shine * (bunch.w + bunch.h + 8)) - 4 : -100;
		for (int sy = 0; sy < bunch.h; sy++)
			for (int sx = 0; sx < bunch.w; sx++)
			{
				uint32_t p = bunch.px[(size_t)sy * bunch.w + sx];
				if (!(p >> 24)) continue;
				// the shine: a diagonal stripe of pale yellow across the fruit (not the outline)
				int diag = sx + (bunch.h - sy);
				if ((p & 0xffffff) != 0 && diag >= band && diag < band + 4) p = rnds::tintAccent(0xfffff0b4u);
				for (int ky = 0; ky < K; ky++)
				{
					int yy = y + sy * K + ky;
					if (yy < 0 || yy >= H) continue;
					uint32_t* row = d + (size_t)yy * W;
					for (int kx = 0; kx < K; kx++)
					{
						int xx = x + sx * K + kx;
						if (xx >= 0 && xx < W) row[xx] = over(p, row[xx]);
					}
				}
			}
	}

	// columns [fromCol, toCol) of a mask at k device px per pixel, dy device px lower
	void Splash::drawMask(uint32_t* d, const Mask& m, int k, int x, int y, uint32_t colour, int fromCol, int toCol, int dy) const
	{
		for (int my = 0; my < m.h; my++)
			for (int mx = fromCol; mx < toCol; mx++)
			{
				if (!m.a[(size_t)my * m.w + mx]) continue;
				fill(d, x + mx * k, y + dy + my * k, k, k, colour);
			}
	}

	// BANANAOS: one letter more every LETTER ms, each popping up from one word-pixel lower; BANANA in two banana
	// tones, OS in white, and a shadow, as the status bar's logo (tools/make-logo.py)
	void Splash::drawWord(uint32_t* d, double t) const
	{
		int n = (int)word.letters.size();
		const int white = 2;		// the last two letters, OS
		int shown = t < WORD_AT ? 0 : std::min(n, 1 + (int)((t - WORD_AT) / LETTER));
		int x = (W - word.w * KW) / 2, y = X(WORD_Y) + oy;
		int sh = std::max(1, KW / 3);
		for (int i = 0; i < shown; i++)
		{
			int c0 = word.letters[i], c1 = i + 1 < n ? word.letters[i + 1] : word.w;
			double age = t - (WORD_AT + i * LETTER);
			int dy = age < 40 ? KW : 0;
			drawMask(d, word, KW, x + sh, y + sh, 0xff000000u, c0, c1, dy);
			// light over the top rows, darker below
			for (int my = 0; my < word.h; my++)
				for (int mx = c0; mx < c1; mx++)
					if (word.a[(size_t)my * word.w + mx])
						fill(d, x + mx * KW, y + dy + my * KW, KW, KW,
						     i >= n - white ? 0xffffffffu : my < (word.h * 11 + 19) / 20 ? rnds::tintAccent(0xffffde52u) : rnds::tintAccent(0xffeea018u));
		}
	}

	void Splash::draw(uint32_t* scr, double t, bool ready)
	{
		// The bunch stays still (no drop: a logo that lands wants a landing sound, and this card cannot play one
		// until well after boot), BANANAOS types itself in, a shine runs over the bananas and LOADING ticks.
		// (`ready` only stops the dots at the hand-over; see nextFrame.)
		(void)ready;
		std::copy(bgTop.begin(), bgTop.end(), scr);
		int bx = (W - bunch.w * K) / 2;
		double shine = t >= SHINE_AT && t < SHINE_AT + SHINE ? (t - SHINE_AT) / SHINE : -1;
		drawBunch(scr, bx, X(BUNCH_Y) + oy, shine);
		drawWord(scr, t);
		if (t >= LOAD_AT)
		{
			int lx = (W - loading.w * KL) / 2;
			drawMask(scr, loading, KL, lx, X(LOAD_Y) + oy, muted, 0, loading.w, 0);
			// three dots, the lit one moving along
			int lit = (int)((t - LOAD_AT) / DOT) % 3, dw = X(10), gap = X(10);
			int dx = (W - 3 * dw - 2 * gap) / 2;
			for (int i = 0; i < 3; i++)
			{
				uint32_t c = i == lit ? ink : mul255(ink, i == (lit + 2) % 3 ? 0x99 : 0x4d);	// the last lit one fading
				fill(scr, dx + i * (dw + gap), X(DOTS_Y) + oy, dw, dw, c);
			}
		}
	}

	int Splash::nextFrame(double t, bool ready) const
	{
		if (t < INTRO_END) return 16;
		if (ready) return 0;
		// waiting on the library: the dots
		double toDot = DOT - std::fmod(t - LOAD_AT, DOT);
		return (int)std::ceil(toDot);
	}
}
