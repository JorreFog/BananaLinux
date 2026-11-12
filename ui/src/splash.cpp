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
	// the shutdown's (drawOutro): the insert backwards, then the CRT switching off
	static constexpr double DOTS_OUT = 90, LOAD_OUT = 3 * DOTS_OUT, LIGHT_OUT = 120, EJ_PRESS = 60, EJ_SLIDE = 400, EJ_RISE = 220;
	static constexpr double UNTYPE_AT = 200, UNTYPE = 50, BYE_SHINE = 300, HOP_AT = 600, HOP = 100, FALL = 260;
	static constexpr double OFF_AT = 960, SQUEEZE = 150, LINE = 110, DOT_FADE = 80;

	// the layout, in CSS px of a 640x480 panel
	static constexpr float BUNCH_Y = 96, WORD_Y = 268, LOAD_Y = 342, DOTS_Y = 368;
	static constexpr float CART_X = 270, CART_Y0 = 66, CLIP_Y = 325, LIP_X = 257;
	static constexpr float CART_END = CLIP_Y - 47;	// the cartridge's top once it is in: 47 of its 104 px still show

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

	bool Splash::load(const std::string& dir, const std::string& banana, const std::string& variant, int w, int h, const std::string& font)
	{
		W = w; H = h; s = h / 480.0f;
		light = variant == "light";
		// the pictures are made for the RG DS's two sizes; any other canvas (the AYN Thor's 1440x1080) has the OLED boot
		oled = !((w == 640 && h == 480) || (w == 1024 && h == 768));
		if (oled) return loadOled(dir, banana, font);
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

	// BANANAOS's first shown letters, the last of them lastDy device px lower (popping in, or about to go); BANANA in
	// two banana tones, OS in white, and a shadow, as the status bar's logo (tools/make-logo.py)
	void Splash::drawWord(uint32_t* d, int shown, int lastDy) const
	{
		int n = (int)word.letters.size();
		const int white = 2;		// the last two letters, OS
		shown = std::min(shown, n);
		int x = (W - word.w * KW) / 2, y = X(WORD_Y);
		int sh = std::max(1, KW / 3);
		for (int i = 0; i < shown; i++)
		{
			int c0 = word.letters[i], c1 = i + 1 < n ? word.letters[i + 1] : word.w;
			int dy = i == shown - 1 ? lastDy : 0;
			drawMask(d, word, KW, x + sh, y + sh, 0xff000000u, c0, c1, dy);
			// light over the top rows, darker below
			for (int my = 0; my < word.h; my++)
				for (int mx = c0; mx < c1; mx++)
					if (word.a[(size_t)my * word.w + mx])
						fill(d, x + mx * KW, y + dy + my * KW, KW, KW,
						     i >= n - white ? 0xffffffffu : my < (word.h * 11 + 19) / 20 ? rnds::tintAccent(0xffffde52u) : rnds::tintAccent(0xffeea018u));
		}
	}

	// LOADING and its three dots, the lit one moving along; dotsOn: how many of them are there (the shutdown's go out)
	void Splash::drawLoading(uint32_t* d, double t, int dotsOn) const
	{
		int lx = (W - loading.w * KL) / 2;
		drawMask(d, loading, KL, lx, X(LOAD_Y), muted, 0, loading.w, 0);
		int lit = (int)((t - LOAD_AT) / DOT) % 3, dw = X(10), gap = X(10);
		int dx = (W - 3 * dw - 2 * gap) / 2;
		for (int i = 0; i < dotsOn; i++)
		{
			uint32_t c = i == lit ? ink : mul255(ink, i == (lit + 2) % 3 ? 0x99 : 0x4d);	// the last lit one fading
			fill(d, dx + i * (dw + gap), X(DOTS_Y), dw, dw, c);
		}
	}

	// sparks off the slot's corners, as the menu's insert; ct: ms since the click (gone after 240)
	void Splash::drawSparks(uint32_t* bot, double ct) const
	{
		if (ct < 0 || ct >= 240) return;
		float k = (float)(ct / 240);
		float dd = 8 + 18 * k;
		uint32_t sp = mul255(light ? 0xff2a2620u : 0xffffffffu, (uint32_t)std::lround(255 * (1 - k * 0.8f)));
		for (float x : { 262.0f, 378.0f })
		{
			float dir = x < 320 ? -1.0f : 1.0f;
			fill(bot, X(x + dir * dd), X(CLIP_Y - 6 - dd * 0.6f), X(4), X(4), sp);
			fill(bot, X(x + dir * (dd + 6)), X(CLIP_Y - 1), X(4), X(4), sp);
			fill(bot, X(x + dir * dd * 0.5f), X(CLIP_Y - 12 - dd), X(4), X(4), sp);
		}
	}

	void Splash::draw(uint32_t* top, uint32_t* bot, double t, bool ready)
	{
		if (oled) { drawOled(top, bot, t); return; }
		// ---- top: the bunch, the word, loading ----
		std::copy(bgTop.begin(), bgTop.end(), top);
		int bx = (W - bunch.w * K) / 2;
		float by = BUNCH_Y;
		if (t < DROP)
		{
			// falling, faster as it goes (smooth: steps() looked like a low frame rate on the handheld)
			float p = (float)(t / DROP);
			by = -bunch.h * K / s + (BUNCH_Y + bunch.h * K / s) * p * p;
		}
		else if (t < DROP + BOUNCE_UP) { float q = (float)((t - DROP) / BOUNCE_UP); by = BUNCH_Y - 10 * (1 - (1 - q) * (1 - q)); }	// up, slowing
		else if (t < DROP + BOUNCE_UP + BOUNCE_DOWN) { float q = (float)((t - DROP - BOUNCE_UP) / BOUNCE_DOWN); by = BUNCH_Y - 10 + 10 * q * q; }	// down, faster
		else if (t >= SHINE_AT + SHINE && !ready)
			by = BUNCH_Y - 3 * (((int)((t - SHINE_AT - SHINE) / BOB)) % 2);	// still loading: the menu's bob
		double shine = t >= SHINE_AT && t < SHINE_AT + SHINE ? (t - SHINE_AT) / SHINE : -1;
		drawBunch(top, bx, X(by), shine);
		// one letter more every LETTER ms, each popping up from one word-pixel lower
		int shown = t < WORD_AT ? 0 : std::min((int)word.letters.size(), 1 + (int)((t - WORD_AT) / LETTER));
		drawWord(top, shown, t - (WORD_AT + (shown - 1) * LETTER) < 40 ? KW : 0);
		if (t >= LOAD_AT) drawLoading(top, t, 3);

		// ---- bottom: the cartridge goes in ----
		std::copy(bgBot.begin(), bgBot.end(), bot);
		float cy;
		if (t < CART_IN)
		{
			float p = (float)(t / CART_IN);
			float e = 1 - (1 - p) * (1 - p);
			cy = -104 + (CART_Y0 + 104) * e;
		}
		else if (t < INSERT_AT) cy = CART_Y0;
		else if (t < INSERT_AT + INS_LIFT) { float q = (float)((t - INSERT_AT) / INS_LIFT); cy = CART_Y0 - 6 * (1 - (1 - q) * (1 - q)); }
		else if (t < CLICK_AT)
		{
			float e = rnds::cubicBezier(0.55f, 0, 0.9f, 0.45f, (float)((t - INSERT_AT - INS_LIFT) / INS_SLIDE));
			cy = (CART_Y0 - 6) + (CART_END + 3 - (CART_Y0 - 6)) * e;
		}
		else if (t < CLICK_AT + INS_CLICK) cy = CART_END + 3;
		else if (t < CLICK_AT + INS_CLICK + INS_SETTLE) { float q = (float)((t - CLICK_AT - INS_CLICK) / INS_SETTLE); cy = CART_END + 3 - 3 * (1 - (1 - q) * (1 - q)); }
		else cy = CART_END;
		blit(bot, cart, X(CART_X), X(cy), X(CLIP_Y));
		blit(bot, lip, X(LIP_X), X(CLIP_Y), H);
		if (t >= CLICK_AT)
		{
			fill(bot, X(411), X(338), X(10), X(6), 0xff3ce07au);	// the power light, green
			if (!cued) { cued = true; if (onCue) onCue("insert"); }
			drawSparks(bot, t - CLICK_AT);
		}
	}

	void Splash::drawOutro(uint32_t* top, uint32_t* bot, double t)
	{
		if (oled && t < OUTRO_END) { drawOledOutro(top, bot, t); return; }
		if (t >= OUTRO_END)
		{
			std::fill(top, top + (size_t)W * H, 0xff000000u);
			std::fill(bot, bot + (size_t)W * H, 0xff000000u);
			return;
		}
		// ---- top: the dots and LOADING go, BANANAOS types itself out from its end, the bunch shines, hops and falls ----
		std::copy(bgTop.begin(), bgTop.end(), top);
		const double loadT = INTRO_END + t;	// the dots carry on from where the intro left them
		if (t < LOAD_OUT) drawLoading(top, loadT, 3 - (int)(t / DOTS_OUT));
		int n = (int)word.letters.size();
		int gone = t < UNTYPE_AT ? 0 : 1 + (int)((t - UNTYPE_AT) / UNTYPE);	// each letter dips a word-pixel, then goes
		bool dip = t >= UNTYPE_AT && t - (UNTYPE_AT + (gone - 1) * UNTYPE) < UNTYPE / 2;
		if (dip) gone--;
		drawWord(top, n - gone, dip ? KW : 0);
		int bx = (W - bunch.w * K) / 2;
		float by = BUNCH_Y;
		if (t >= HOP_AT && t < HOP_AT + HOP) { float q = (float)((t - HOP_AT) / HOP); by = BUNCH_Y - 10 * (1 - (1 - q) * (1 - q)); }	// up, slowing
		else if (t >= HOP_AT + HOP)
		{
			// down and off the bottom of the panel, faster as it goes
			float p = (float)std::min(1.0, (t - HOP_AT - HOP) / FALL);
			by = BUNCH_Y - 10 + (480 - BUNCH_Y + 10) * p * p;
		}
		double shine = t >= HOP_AT - BYE_SHINE && t < HOP_AT ? (t - HOP_AT + BYE_SHINE) / BYE_SHINE : -1;
		drawBunch(top, bx, X(by), shine);

		// ---- bottom: the light goes out, the cartridge is pressed in, pops out and is lifted away ----
		std::copy(bgBot.begin(), bgBot.end(), bot);
		const double ej = LIGHT_OUT, slideAt = ej + EJ_PRESS, riseAt = slideAt + EJ_SLIDE;
		float cy;
		if (t < ej) cy = CART_END;
		else if (t < slideAt) cy = CART_END + 3;		// pressed in: the click
		else if (t < riseAt)
		{
			// out of the slot: the insert's slide backwards (fast off the spring, slowing)
			float e = rnds::cubicBezier(0.1f, 0.55f, 0.45f, 1, (float)((t - slideAt) / EJ_SLIDE));
			cy = (CART_END + 3) + ((CART_Y0 - 6) - (CART_END + 3)) * e;
		}
		else
		{
			float p = (float)std::min(1.0, (t - riseAt) / EJ_RISE);
			cy = (CART_Y0 - 6) + (-110 - (CART_Y0 - 6)) * p * p;	// lifted up and away
		}
		blit(bot, cart, X(CART_X), X(cy), X(CLIP_Y));
		blit(bot, lip, X(LIP_X), X(CLIP_Y), H);
		if (t < LIGHT_OUT) fill(bot, X(411), X(338), X(10), X(6), 0xff3ce07au);	// the power light, green until the press
		if (t >= slideAt && !ejected) { ejected = true; if (onCue) onCue("eject"); }
		drawSparks(bot, t - slideAt);

		if (t >= OFF_AT) { crtOff(top, t - OFF_AT); crtOff(bot, t - OFF_AT); }
	}

	// a panel switching off like a CRT, t ms in: the picture squeezed to a bright line through the middle, the line
	// pulled in to a dot, the dot fading; all in whole steps of the splash's pixels, on black
	void Splash::crtOff(uint32_t* d, double t)
	{
		const int block = std::max(1, (int)std::lround(2 * s));
		const size_t n = (size_t)W * H;
		if (t < SQUEEZE)
		{
			float p = (float)(t / SQUEEZE), e = p * p;
			int h = std::max(block, (int)(H * (1 - e)) / block * block);
			int y0 = (H - h) / 2;
			uint32_t g = (uint32_t)std::lround(220 * e), glow = g << 24 | g * 0x010101u;	// whiter as it thins
			tmp.assign(d, d + n);
			std::fill(d, d + n, 0xff000000u);
			for (int y = 0; y < h; y++)
			{
				const uint32_t* src = &tmp[(size_t)((long)y * H / h) * W];
				uint32_t* row = d + (size_t)(y0 + y) * W;
				for (int x = 0; x < W; x++) row[x] = over(glow, src[x]);
			}
			return;
		}
		std::fill(d, d + n, 0xff000000u);
		t -= SQUEEZE;
		if (t < LINE)
		{
			float q = (float)(t / LINE);
			int w = std::max(2 * block, (int)(W * (1 - q) * (1 - q)) / block * block);
			fill(d, (W - w) / 2, (H - block) / 2, w, block, 0xfff8f6efu);
			return;
		}
		t -= LINE;
		if (t < DOT_FADE)
		{
			uint32_t a = (uint32_t)std::lround(255 * (1 - t / DOT_FADE));
			fill(d, W / 2 - block, (H - block) / 2, 2 * block, block, mul255(0xfff8f6efu, a));
		}
	}

	// ---- the OLED boot ------------------------------------------------------------------------------------------------
	// Laid out in px of a 1080-line screen (k: the canvas's px per those), centred: the bunch in a 200 px box, 40 px,
	// the word 64 px high, 40 px, the line 420 x 6. Timings (ms): the bunch pops in over 800, a ring of light leaves it
	// every 600 from 300, the letters rise 50 apart from 350, the line fills from 300 to 1350
	static constexpr double O_POP = 800, O_RING_AT = 300, O_RING = 1600, O_LETTER_AT = 350, O_LETTER = 50, O_RISE = 400;
	static constexpr double O_BAR_AT = 300, O_BAR = 1050, O_SUB_AT = 500, O_SUB = 600;
	static constexpr float O_LOGO_Y = 458, O_WORD_Y = 615, O_BAR_Y = 716, O_BAR_W = 420, O_BAR_H = 6;
	static double easeOut(double p) { p = std::min(1.0, std::max(0.0, p)); return 1 - (1 - p) * (1 - p) * (1 - p); }
	static double easeOutBack(double p) { p = std::min(1.0, std::max(0.0, p)); const double c = 1.70158; return 1 + (c + 1) * std::pow(p - 1, 3) + c * std::pow(p - 1, 2); }
	static uint32_t withAlpha(uint32_t rgb, double a) { return mul255(0xff000000u | (rgb & 0xffffff), (uint32_t)std::lround(255 * std::min(1.0, std::max(0.0, a)))); }

	bool Splash::loadOled(const std::string& dir, const std::string& banana, const std::string& font)
	{
		if (!loadRgba(banana, bunch.px, bunch.w, bunch.h)) return false;
		if (!loadMask(dir + "/word.png", word.w, word.h, word.a, word.letters)) return false;
		ink = 0xfff1ede2u;
		muted = 0xff55514au;
		device = Mask();
		// "AYN THOR" in Pixelify Sans, 34 px of the bottom screen's 1240 across (the canvas is drawn on it narrower), spaced
		std::shared_ptr<rnds::Font> f = font.empty() ? nullptr : rnds::Font::load(font);
		if (f)
		{
			rnds::TextStyle st;
			st.font = f;
			st.size = 34.0f * W / 1240;
			st.letterSpacing = st.size * 0.2f;
			rnds::TextLine l = rnds::layoutLine(U"AYN THOR", st);
			rnds::TextBitmap tb = rnds::renderLine(l, st, 1, f->ascent(st.size));
			const rnds::Bitmap& b = tb.bmp;
			if (!b.empty())
			{
				device.w = b.w; device.h = b.h;
				device.a.resize((size_t)b.w * b.h);
				for (size_t i = 0; i < device.a.size(); i++) device.a[i] = b.alpha ? b.px[i] : b.px[i * 4 + 3];
			}
		}
		return true;
	}

	// the bunch centred at cx, cy at k device px per sprite pixel (nearest: it stays pixel art as it grows)
	void Splash::drawBunchAt(uint32_t* d, float cx, float cy, float k, double alpha) const
	{
		if (k <= 0 || alpha <= 0) return;
		uint32_t a = (uint32_t)std::lround(255 * std::min(1.0, alpha));
		int w = (int)std::lround(bunch.w * k), h = (int)std::lround(bunch.h * k);
		int x0 = (int)std::lround(cx - w / 2.0f), y0 = (int)std::lround(cy - h / 2.0f);
		for (int y = std::max(0, y0); y < std::min(H, y0 + h); y++)
		{
			int sy = std::min(bunch.h - 1, (int)((y - y0) / k));
			uint32_t* row = d + (size_t)y * W;
			for (int x = std::max(0, x0); x < std::min(W, x0 + w); x++)
			{
				uint32_t p = bunch.px[(size_t)sy * bunch.w + std::min(bunch.w - 1, (int)((x - x0) / k))];
				if (p >> 24) row[x] = over(a == 255 ? p : mul255(p, a), row[x]);
			}
		}
	}

	// a soft round light, strongest in the middle (the CSS drop-shadow glow round the bunch)
	void Splash::glow(uint32_t* d, float cx, float cy, float r, uint32_t colour, float strength) const
	{
		if (strength <= 0 || r <= 0) return;
		for (int y = std::max(0, (int)(cy - r)); y < std::min(H, (int)(cy + r) + 1); y++)
			for (int x = std::max(0, (int)(cx - r)); x < std::min(W, (int)(cx + r) + 1); x++)
			{
				float dd = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) / r;
				if (dd >= 1) continue;
				float q = 1 - dd;
				d[(size_t)y * W + x] = over(withAlpha(colour, strength * q * q), d[(size_t)y * W + x]);
			}
	}

	// a circle's outline, anti-aliased
	void Splash::ring(uint32_t* d, float cx, float cy, float r, float th, uint32_t colour, float alpha) const
	{
		if (alpha <= 0) return;
		float ro = r + th / 2 + 1;
		for (int y = std::max(0, (int)(cy - ro)); y < std::min(H, (int)(cy + ro) + 1); y++)
			for (int x = std::max(0, (int)(cx - ro)); x < std::min(W, (int)(cx + ro) + 1); x++)
			{
				float dist = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
				float cov = std::min(1.0f, std::max(0.0f, th / 2 + 0.5f - std::fabs(dist - r)));
				if (cov > 0) d[(size_t)y * W + x] = over(withAlpha(colour, alpha * cov), d[(size_t)y * W + x]);
			}
	}

	// the top screen: the bunch (its scale and opacity), the first letters of BANANA OS (the last one letterT ms into
	// its rise; letterOut: all of them that far into their fall, the shutdown's), the line filled to bar (0..1)
	void Splash::oledTop(uint32_t* top, double logoScale, double logoAlpha, int letters, double letterT, double letterOut, double bar, double t, bool rings)
	{
		const float k = H / 1080.0f, cx = W / 2.0f;
		const uint32_t acc = rnds::accent.base & 0xffffff;
		std::fill(top, top + (size_t)W * H, 0xff000000u);
		// the glow, then the rings of light (two, 600 ms apart, each growing from 0.7 to 2.2 of the 200 px box, fading)
		glow(top, cx, O_LOGO_Y * k, 170 * k * (float)logoScale, acc, 0.5f * (float)logoAlpha);
		if (rings)
			for (double start : { O_RING_AT, O_RING_AT + 600 })
			{
				if (t < start) continue;
				double p = std::fmod(t - start, O_RING) / O_RING;
				ring(top, cx, O_LOGO_Y * k, 100 * k * (float)(0.7 + 1.5 * p), std::max(1.0f, 3 * k), acc, 0.7f * (float)(1 - p) * (float)logoAlpha);
			}
		drawBunchAt(top, cx, O_LOGO_Y * k, (float)(150.0 / 48) * k * (float)logoScale, logoAlpha);
		// BANANA OS: the word's pixel letters 6 px a pixel at 1080 lines, 2 more pixels between letters (CSS letter-spacing
		// .18em) and a space before OS, each rising 26 px as it fades in
		int kw = std::max(1, (int)std::lround(6 * k)), gap = 2 * kw, n = (int)word.letters.size();
		int total = word.w * kw + (n - 1) * gap + 3 * kw;
		int x = (W - total) / 2, y = (int)std::lround(O_WORD_Y * k);
		for (int i = 0; i < n; i++)
		{
			int c0 = word.letters[i], c1 = i + 1 < n ? word.letters[i + 1] : word.w;
			int lx = x + c0 * kw + i * gap + (i >= n - 2 ? 3 * kw : 0);
			if (i >= letters) continue;
			double lt = i == letters - 1 ? letterT : 1e9;
			double q = std::min(easeOut(lt / O_RISE), 1 - easeOut(letterOut / O_RISE));
			if (q <= 0) continue;
			int dy = (int)std::lround(26 * k * (1 - q));
			for (int my = 0; my < word.h; my++)
				for (int mx = c0; mx < c1; mx++)
					if (word.a[(size_t)my * word.w + mx]) fill(top, lx + (mx - c0) * kw, y + dy + my * kw, kw, kw, withAlpha(acc, q));
		}
		// the line: its track, the accent filling it, a faint light round the filled part
		int bw = (int)std::lround(O_BAR_W * k), bh = std::max(2, (int)std::lround(O_BAR_H * k)), bx = (W - bw) / 2, by = (int)std::lround(O_BAR_Y * k);
		fill(top, bx, by, bw, bh, 0xff1a1a1du);
		int fw = (int)std::lround(bw * std::min(1.0, std::max(0.0, bar)));
		if (fw > 0)
		{
			for (int g = 1; g <= 4; g++) fill(top, bx - g, by - g, fw + 2 * g, bh + 2 * g, withAlpha(acc, 0.09));
			fill(top, bx, by, fw, bh, 0xff000000u | acc);
		}
	}

	void Splash::drawOled(uint32_t* top, uint32_t* bot, double t)
	{
		int n = (int)word.letters.size();
		int shown = t < O_LETTER_AT ? 0 : std::min(n, 1 + (int)((t - O_LETTER_AT) / O_LETTER));
		double lastT = shown ? t - (O_LETTER_AT + (shown - 1) * O_LETTER) : 0;
		// (every letter but the last has risen O_LETTER more than the one after it: the last one's rise is enough to
		// tell the others' apart, as they rise together)
		oledTop(top, 0.4 + 0.6 * easeOutBack(t / O_POP), std::min(1.0, t / 250), shown, lastT, 0, easeOut((t - O_BAR_AT) / O_BAR), t, true);
		std::fill(bot, bot + (size_t)W * H, 0xff000000u);
		if (!device.a.empty() && t >= O_SUB_AT)
		{
			double q = easeOut((t - O_SUB_AT) / O_SUB);
			int x = (W - device.w) / 2, y = (H - device.h) / 2 + (int)std::lround(26 * H / 1080.0f * (1 - q));
			for (int my = 0; my < device.h; my++)
				for (int mx = 0; mx < device.w; mx++)
					if (uint8_t a = device.a[(size_t)my * device.w + mx]) fill(bot, x + mx, y + my, 1, 1, withAlpha(muted, q * a / 255.0));
		}
	}

	// the shutdown: the line empties, the letters fall away from the end, the bunch shrinks into the dark, then both
	// screens switch off like the RG DS's
	void Splash::drawOledOutro(uint32_t* top, uint32_t* bot, double t)
	{
		int n = (int)word.letters.size();
		double bar = 1 - easeOut(t / 400);
		double out = t < 200 ? 0 : (t - 200);
		double logo = t < 600 ? 1 : 1 - easeOut((t - 600) / 300);
		oledTop(top, 0.4 + 0.6 * logo, logo, n, 1e9, out, bar, INTRO_END, false);
		std::fill(bot, bot + (size_t)W * H, 0xff000000u);
		if (!device.a.empty() && t < 400)
		{
			double q = 1 - t / 400;
			int x = (W - device.w) / 2, y = (H - device.h) / 2;
			for (int my = 0; my < device.h; my++)
				for (int mx = 0; mx < device.w; mx++)
					if (uint8_t a = device.a[(size_t)my * device.w + mx]) fill(bot, x + mx, y + my, 1, 1, withAlpha(muted, q * a / 255.0));
		}
		if (t >= OFF_AT) { crtOff(top, t - OFF_AT); crtOff(bot, t - OFF_AT); }
	}

	int Splash::nextFrame(double t, bool ready) const
	{
		if (t < INTRO_END) return 16;
		if (oled) return ready ? 0 : 33;	// waiting on the library: the rings go on
		if (ready) return 0;
		// waiting on the library: the dots and the bob
		double toDot = DOT - std::fmod(t - LOAD_AT, DOT), toBob = BOB - std::fmod(t - SHINE_AT - SHINE, BOB);
		return (int)std::ceil(std::min(toDot, toBob));
	}
}
