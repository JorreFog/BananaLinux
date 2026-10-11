#include "splash.h"
#include "../rnds/RndsUI.h"
#include <algorithm>
#include <cmath>

namespace banana
{
	// timings (ms). The insert is the menu's ready screen's (RndsUI's INS_LIFT, INS_SLIDE, INS_CLICK, INS_SETTLE).
	static constexpr double DROP = 380, BOUNCE_UP = 120, BOUNCE_DOWN = 100;
	static constexpr double WORD_AT = 560, LETTER = 60, LOAD_AT = 600, DOT = 240;
	static constexpr double SHINE_AT = 1000, SHINE = 300, BOB = 600;
	static constexpr double CART_IN = 400, INSERT_AT = 520, INS_LIFT = 120, INS_SLIDE = 400, INS_CLICK = 90, INS_SETTLE = 110;
	static constexpr double CLICK_AT = INSERT_AT + INS_LIFT + INS_SLIDE;

	// the layout, in CSS px of a 640x480 panel
	static constexpr float BUNCH_Y = 96, WORD_Y = 268, LOAD_Y = 342, DOTS_Y = 368;
	static constexpr float CART_X = 220, CART_Y0 = 66, CLIP_Y = 325, LIP_X = 227;
	static constexpr float CART_END = CLIP_Y - 94;	// the cartridge's top once it is in: 94 of its 208 px still show

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
			px[i] = (a << 24) | (m(s[0]) << 16) | (m(s[1]) << 8) | m(s[2]);
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
		W = w; H = h; s = h / 480.0f;
		light = variant == "light";
		K = std::max(1, (int)std::lround(3 * s));
		KW = std::max(1, (int)std::lround(6 * s));
		KL = std::max(1, (int)std::lround(2 * s));
		std::string size = w > 640 ? "1024" : "640", pal = light ? "light" : "dark";
		int bw, bh;
		if (!loadRgba(dir + "/" + pal + "-" + size + "-top.png", bgTop, bw, bh) || bw != W || bh != H) return false;
		if (!loadRgba(dir + "/" + pal + "-" + size + "-bot.png", bgBot, bw, bh) || bw != W || bh != H) return false;
		if (!loadRgba(dir + "/" + pal + "-" + size + "-lip.png", lip.px, lip.w, lip.h)) return false;
		if (!loadRgba(dir + "/cart-" + size + ".png", cart.px, cart.w, cart.h)) return false;
		if (!loadRgba(banana, bunch.px, bunch.w, bunch.h)) return false;
		if (!loadMask(dir + "/word.png", word.w, word.h, word.a, word.letters)) return false;
		if (!loadMask(dir + "/loading.png", loading.w, loading.h, loading.a, loading.letters)) return false;
		ink = light ? 0xff1d2733u : 0xffe7eef6u;
		muted = light ? 0xff66758au : 0xff93a0b0u;
		return true;
	}

	int Splash::X(float v) const { return (int)std::lround(v * s); }

	void Splash::fill(uint32_t* d, int x, int y, int w, int h, uint32_t c) const
	{
		int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(W, x + w), y1 = std::min(H, y + h);
		for (int yy = y0; yy < y1; yy++)
			for (int xx = x0; xx < x1; xx++) d[(size_t)yy * W + xx] = over(c, d[(size_t)yy * W + xx]);
	}

	void Splash::blit(uint32_t* d, const Rgba& r, int x, int y, int clipY1) const
	{
		int y1 = std::min({ H, y + r.h, clipY1 });
		for (int yy = std::max(0, y); yy < y1; yy++)
		{
			const uint32_t* s = &r.px[(size_t)(yy - y) * r.w];
			uint32_t* row = d + (size_t)yy * W;
			for (int xx = std::max(0, x); xx < std::min(W, x + r.w); xx++) row[xx] = over(s[xx - x], row[xx]);
		}
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
				if ((p & 0xffffff) != 0 && diag >= band && diag < band + 4) p = 0xfffff0b4u;
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

	// BANANA: one letter more every LETTER ms, each popping up from one word-pixel lower; two tones and a shadow,
	// as the status bar's logo
	void Splash::drawWord(uint32_t* d, double t) const
	{
		int n = (int)word.letters.size();
		int shown = t < WORD_AT ? 0 : std::min(n, 1 + (int)((t - WORD_AT) / LETTER));
		int x = (W - word.w * KW) / 2, y = X(WORD_Y);
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
						fill(d, x + mx * KW, y + dy + my * KW, KW, KW, my < (word.h * 11 + 19) / 20 ? 0xffffde52u : 0xffeea018u);
		}
	}

	void Splash::draw(uint32_t* top, uint32_t* bot, double t, bool ready)
	{
		// ---- top: the bunch, the word, loading ----
		std::copy(bgTop.begin(), bgTop.end(), top);
		int bx = (W - bunch.w * K) / 2;
		float by = BUNCH_Y;
		if (t < DROP)
		{
			// falling, faster as it goes, in 8 steps
			float p = rnds::stepsEnd(8, (float)(t / DROP));
			by = -bunch.h * K / s + (BUNCH_Y + bunch.h * K / s) * p * p;
		}
		else if (t < DROP + BOUNCE_UP) by = BUNCH_Y - 10 * rnds::stepsEnd(3, (float)((t - DROP) / BOUNCE_UP));
		else if (t < DROP + BOUNCE_UP + BOUNCE_DOWN) by = BUNCH_Y - 10 + 10 * rnds::stepsEnd(2, (float)((t - DROP - BOUNCE_UP) / BOUNCE_DOWN));
		else if (t >= SHINE_AT + SHINE && !ready)
			by = BUNCH_Y - 3 * (((int)((t - SHINE_AT - SHINE) / BOB)) % 2);	// still loading: the menu's bob
		double shine = t >= SHINE_AT && t < SHINE_AT + SHINE ? rnds::stepsEnd(8, (float)((t - SHINE_AT) / SHINE)) : -1;
		drawBunch(top, bx, X(by), shine);
		drawWord(top, t);
		if (t >= LOAD_AT)
		{
			int lx = (W - loading.w * KL) / 2;
			drawMask(top, loading, KL, lx, X(LOAD_Y), muted, 0, loading.w, 0);
			// three dots, the lit one moving along
			int lit = (int)((t - LOAD_AT) / DOT) % 3, dw = X(10), gap = X(10);
			int dx = (W - 3 * dw - 2 * gap) / 2;
			for (int i = 0; i < 3; i++)
			{
				uint32_t c = i == lit ? ink : mul255(ink, i == (lit + 2) % 3 ? 0x99 : 0x4d);	// the last lit one fading
				fill(top, dx + i * (dw + gap), X(DOTS_Y), dw, dw, c);
			}
		}

		// ---- bottom: the cartridge goes in ----
		std::copy(bgBot.begin(), bgBot.end(), bot);
		float cy;
		if (t < CART_IN)
		{
			float p = rnds::stepsEnd(6, (float)(t / CART_IN));
			float e = 1 - (1 - p) * (1 - p);
			cy = -208 + (CART_Y0 + 208) * e;
		}
		else if (t < INSERT_AT) cy = CART_Y0;
		else if (t < INSERT_AT + INS_LIFT) cy = CART_Y0 - 6 * rnds::stepsEnd(2, (float)((t - INSERT_AT) / INS_LIFT));
		else if (t < CLICK_AT)
		{
			float e = rnds::cubicBezier(0.55f, 0, 0.9f, 0.45f, (float)((t - INSERT_AT - INS_LIFT) / INS_SLIDE));
			cy = (CART_Y0 - 6) + (CART_END + 3 - (CART_Y0 - 6)) * e;
		}
		else if (t < CLICK_AT + INS_CLICK) cy = CART_END + 3;
		else if (t < CLICK_AT + INS_CLICK + INS_SETTLE) cy = CART_END + 3 - 3 * rnds::stepsEnd(3, (float)((t - CLICK_AT - INS_CLICK) / INS_SETTLE));
		else cy = CART_END;
		blit(bot, cart, X(CART_X), X(std::round(cy)), X(CLIP_Y));
		blit(bot, lip, X(LIP_X), X(CLIP_Y), H);
		if (t >= CLICK_AT)
		{
			fill(bot, X(448), X(326), X(10), X(6), 0xff3ce07au);	// the power light, green
			if (!cued) { cued = true; if (onCue) onCue("insert"); }
			double ct = t - CLICK_AT;
			if (ct < 240)
			{
				// sparks off the slot's corners, as the menu's insert
				float k = rnds::stepsEnd(3, (float)(ct / 240));
				float dd = 8 + 18 * k;
				uint32_t sp = mul255(light ? 0xff1d2733u : 0xffffffffu, (uint32_t)std::lround(255 * (1 - k * 0.8f)));
				for (float x : { 232.0f, 404.0f })
				{
					float dir = x < 320 ? -1.0f : 1.0f;
					fill(bot, X(x + dir * dd), X(CLIP_Y - 6 - dd * 0.6f), X(4), X(4), sp);
					fill(bot, X(x + dir * (dd + 6)), X(CLIP_Y - 1), X(4), X(4), sp);
					fill(bot, X(x + dir * dd * 0.5f), X(CLIP_Y - 12 - dd), X(4), X(4), sp);
				}
			}
		}
	}

	int Splash::nextFrame(double t, bool ready) const
	{
		if (t < INTRO_END) return 16;
		if (ready) return 0;
		// waiting on the library: the dots and the bob
		double toDot = DOT - std::fmod(t - LOAD_AT, DOT), toBob = BOB - std::fmod(t - SHINE_AT - SHINE, BOB);
		return (int)std::ceil(std::min(toDot, toBob));
	}
}
