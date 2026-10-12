#include "RndsUI.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>

namespace rnds
{
	// ---- timing ---------------------------------------------------------------------------------------------------------

	float cubicBezier(float x1, float y1, float x2, float y2, float p)
	{
		if (p <= 0) return 0;
		if (p >= 1) return 1;
		const double cx = 3.0 * x1, bx = 3.0 * (x2 - x1) - cx, ax = 1.0 - cx - bx;
		const double cy = 3.0 * y1, by = 3.0 * (y2 - y1) - cy, ay = 1.0 - cy - by;
		auto sx = [&](double t) { return ((ax * t + bx) * t + cx) * t; };
		auto dx = [&](double t) { return (3.0 * ax * t + 2.0 * bx) * t + cx; };
		double t = p;
		for (int i = 0; i < 8; i++)
		{
			double x = sx(t) - p;
			if (std::fabs(x) < 1e-7) break;
			double d = dx(t);
			if (std::fabs(d) < 1e-6) break;
			t -= x / d;
		}
		if (t < 0 || t > 1 || std::fabs(sx(t) - p) > 1e-5)
		{
			double lo = 0, hi = 1;
			t = p;
			for (int i = 0; i < 40; i++)
			{
				double x = sx(t);
				if (std::fabs(x - p) < 1e-7) break;
				if (x < p) lo = t; else hi = t;
				t = (lo + hi) / 2;
			}
		}
		return (float)(((ay * t + by) * t + cy) * t);
	}

	// ---- mockup palette -------------------------------------------------------------------------------------------------

	// every colour the screens use; dark is the mockup's, light the same pixel look on white
	struct Pal
	{
		Color ink, muted, line, edge, navy, red, shade40, shade45, shade35, blackish;
		Color screenBg, screenFrame, screenBand, ditherA, ditherB;
		Color sheetBase, sheetHi, sheetLo; float sheetMix;
		Color panelBand, panelBody, panelLow;		// hero, slots, the face buttons: top band, body, bottom inset
		Color chipBand, chipBody;
		Color frameBorder, frameBg;					// the box art's frame
		Color meterBg, brandBg, kbdBg, kbdInk, rule;
		Color bubbleBand, bubbleBody, bubbleLow;
		Color trackBg, trackHi, trackShade;
		Color slotOn, railBg, faceMuted;
		Color favBorder, favBand, favBody, favInk;
		Color glint, floor;							// the box art's shine and its shadow on the sheet
		Color slotDark, slotLip;					// the console slot the cartridge goes into
		Color cartLabel;							// the label under a DS ROM's own icon (GameInfo::rom)
	};

	static Pal darkPal()
	{
		Pal p;
		p.ink = Color::hex(0xf1ede2); p.muted = Color::hex(0xa39d8e); p.line = Color::hex(0x8a8374);
		p.edge = Color::hex(0xe8e2d3); p.navy = Color::hex(0xfff6dc); p.red = Color::hex(0xff3b4a);
		p.shade40 = Color(0, 0, 0, 0.4f); p.shade45 = Color(0, 0, 0, 0.45f); p.shade35 = Color(0, 0, 0, 0.35f);
		p.blackish = Color::hex(0x0b0a0d);
		p.screenBg = Color::hex(0x17161c); p.screenFrame = p.blackish; p.screenBand = Color::hex(0x2e2b33);
		p.ditherA = Color(1, 1, 1, 0.14f); p.ditherB = Color(1, 1, 1, 0.07f);
		p.sheetBase = Color::hex(0x15141a); p.sheetMix = 0.34f; p.sheetHi = Color(1, 1, 1, 0.1f); p.sheetLo = p.shade35;
		p.panelBand = Color::hex(0x3a3540); p.panelBody = Color::hex(0x17161c); p.panelLow = p.blackish;
		p.chipBand = Color::hex(0x2e2b33); p.chipBody = Color::hex(0x121117);
		p.frameBorder = Color::hex(0xe8e2d3); p.frameBg = Color::hex(0x17161c);
		p.meterBg = Color::hex(0x15141a); p.brandBg = Color::hex(0x262219); p.kbdBg = Color::hex(0xf1ede2);
		p.kbdInk = Color::hex(0x17161c); p.rule = Color::hex(0x45403d);
		p.bubbleBand = Color::hex(0x38333b); p.bubbleBody = Color::hex(0x191820); p.bubbleLow = Color::hex(0x0f0e13);
		p.trackBg = Color::hex(0x100f14); p.trackHi = Color::hex(0x353039); p.trackShade = p.shade45;
		p.slotOn = Color::hex(0xffffff); p.railBg = Color::hex(0x100f14); p.faceMuted = Color::hex(0x45403d);
		p.favBorder = p.red; p.favBand = Color::hex(0x5a2430); p.favBody = Color::hex(0x2a1016); p.favInk = Color::hex(0xff8a96);
		p.glint = Color(1, 1, 1, 0.28f); p.floor = Color(0, 0, 0, 0.35f);
		p.slotDark = Color::hex(0x070609); p.slotLip = Color::hex(0x45403d);
		p.cartLabel = Color::hex(0xf1ede2);
		return p;
	}

	static Pal lightPal()
	{
		Pal p;
		p.ink = Color::hex(0x2a2620); p.muted = Color::hex(0x7a7262); p.line = Color::hex(0xa69d8c);
		p.edge = Color::hex(0x3a342c); p.navy = Color::hex(0xfffaf0); p.red = Color::hex(0xe23b4a);
		Color sh = Color::hex(0x3a2e14);
		p.shade40 = Color(sh.r, sh.g, sh.b, 0.22f); p.shade45 = Color(sh.r, sh.g, sh.b, 0.25f); p.shade35 = Color(sh.r, sh.g, sh.b, 0.14f);
		p.blackish = Color(sh.r, sh.g, sh.b, 0.3f);
		p.screenBg = Color::hex(0xf3efe6); p.screenFrame = Color::hex(0xd4ccbd); p.screenBand = Color::hex(0xfdfcf9);
		p.ditherA = Color(sh.r, sh.g, sh.b, 0.07f); p.ditherB = Color(sh.r, sh.g, sh.b, 0.035f);
		p.sheetBase = Color::hex(0xfdfbf6); p.sheetMix = 0.3f; p.sheetHi = Color(1, 1, 1, 0.55f); p.sheetLo = Color(sh.r, sh.g, sh.b, 0.12f);
		p.panelBand = Color::hex(0xffffff); p.panelBody = Color::hex(0xf6f2ea); p.panelLow = Color::hex(0xd8d0c0);
		p.chipBand = Color::hex(0xffffff); p.chipBody = Color::hex(0xf6f2ea);
		p.frameBorder = Color::hex(0x3a342c); p.frameBg = Color::hex(0xebe5d9);
		p.meterBg = Color::hex(0xe3dccd); p.brandBg = Color::hex(0x262219); p.kbdBg = Color::hex(0x3a342c);
		p.kbdInk = Color::hex(0xf2f6fa); p.rule = Color::hex(0xd6cfc0);
		p.bubbleBand = Color::hex(0xffffff); p.bubbleBody = Color::hex(0xfaf7f0); p.bubbleLow = Color::hex(0xddd5c6);
		p.trackBg = Color::hex(0xe6dfd0); p.trackHi = Color::hex(0xffffff); p.trackShade = Color(sh.r, sh.g, sh.b, 0.18f);
		p.slotOn = Color::hex(0xf0b429); p.railBg = Color::hex(0xe6dfd0); p.faceMuted = Color::hex(0xd6cfc0);
		p.favBorder = p.red; p.favBand = Color::hex(0xfff2f3); p.favBody = Color::hex(0xffdfe3); p.favInk = Color::hex(0xc8283a);
		p.glint = Color(1, 1, 1, 0.45f); p.floor = Color(sh.r, sh.g, sh.b, 0.2f);
		p.slotDark = Color::hex(0x2a2620); p.slotLip = Color::hex(0xa69d8c);
		p.cartLabel = Color::hex(0xffffff);
		return p;
	}

	// a box in CSS px, the way the mockup's stylesheet declares it
	struct BoxCss
	{
		float radius = 0, border = 0;
		Color borderColor, bg;
		std::vector<Band> bands;
		bool dither = false;
		std::vector<Shadow> shadows;
		std::string key;	// identifies the style in the cache
		bool async = false;	// big: painted on the worker (drawn once it's there)
	};

	static Shadow outer(float dx, float dy, const Color& c) { return Shadow{ dx, dy, 0, c, false }; }
	static Shadow inset(float dx, float dy, float spread, const Color& c) { return Shadow{ dx, dy, spread, c, true }; }

	// ---- implementation -------------------------------------------------------------------------------------------------

	struct SlotState
	{
		bool known = false;
		int d = 0;				// target offset from the centre (items)
		float fromX = 0, toX = 0, fromS = 1, toS = 1;
		double t0 = -1e9;		// transform transition start
		bool trans = false;
		float fromO = 1, toO = 1;
		double o0 = -1e9;
		bool otrans = false;
		// wrapping around the end of the shelf: the item enters from the side it comes in by, and a copy of it
		// leaves from its old place on the other side (with few items, both places are on screen)
		bool ghost = false;
		float gFromX = 0, gToX = 0, gS = 1, gO = 1;
		double g0 = -1e9;
	};

	struct CacheEntry
	{
		Sprite sp;
		uint64_t used = 0;
		size_t bytes = 0;
		int group = 0;
		// images: CSS size they were fitted to (photo frames)
		float cssW = 0, cssH = 0;
	};

	struct Job
	{
		std::string key;
		int group = 0;
		bool smooth = true;
		std::function<Bitmap(float&, float&)> fn;
	};

	struct Done
	{
		std::string key;
		int group = 0;
		bool smooth = true;
		Bitmap bmp;
		float cssW = 0, cssH = 0;	// photos: the fitted CSS size; boxes: the box's origin in the bitmap
	};

	enum Group { G_TEXT = 0, G_BOX, G_ICON, G_CART, G_PHOTO, G_COUNT };
	static const size_t GROUP_LIMIT[G_COUNT] = { 400, 200, 120, 90, 8 };
	// and the cache's bytes (what its textures hold), at 1x (640x480) and times the panel's area (x2.56 at 1.6x). Measured
	// with a 740-game library at 1x (1.6x): one frame of a game list draws 3.8 MB (9.8; the screen box alone is 1.2 MB,
	// the sheet 1.0), and browsing settles at 11 MB (28) under the counts above: 400 lines of text at ~6 KB (14), 90
	// cartridges at 34 KB (87), 8 covers at 175 KB (450) and ~3.6 MB (9.3) of boxes. But every cover size has its own
	// frame (up to 200 KB, 500 KB at 1.6x): a library of scraped art in many sizes filled the box group's 200 entries
	// with frames, 32 MB (81). 20 MB (51) keeps all of normal browsing and five frames' worth, and stops that
	static const size_t BUDGET_1X = 20u << 20;
	// the worker's queue: what scrolled off long ago is dropped (and asked for again if it comes back)
	static const size_t MAX_JOBS = 64;

	struct Face
	{
		std::string kbd, label;
		int kind = 0;		// 0 button, 1 is-on, 2 note
		int action = 0;		// what a tap does
	};

	enum Action { ACT_NONE = 0, ACT_PREV, ACT_NEXT, ACT_OPEN, ACT_BACK, ACT_FAV, ACT_RESUME, ACT_SLOT, ACT_RAIL };

	struct UI::Impl
	{
		UI* ui;
		Source* src;
		Backend* be;
		std::string dir;
		float s;

		std::shared_ptr<Font> R, M;
		std::shared_ptr<const Bitmap> shell;	// game_slot.png (shared with the cartridge jobs, not copied into each)
		Bitmap logo, wifi, batt;	// pre-rendered at this scale
		Bitmap consoleDark, consoleLight;	// Banana: the console the cartridge goes into (tools/make-console.py), per theme
		bool lightTheme = false;

		std::map<std::string, CacheEntry> cache;
		uint64_t frame = 1;
		size_t budget = 0;			// the cache's bytes (BUDGET_1X at this scale)
		bool budgetLogged = false;
		std::function<void(const std::string&)> log;

		// the worker: a key stays in queued from its request until the UI has taken its result (absorb), so a job
		// finished while nothing takes results (ES draws the view under a menu but doesn't update it) is not asked for
		// again every frame
		std::mutex qlock;
		std::deque<Job> jobs;
		std::set<std::string> queued;
		std::vector<Done> done;
		std::function<void()> onQueued, onFinished;
		bool requestedThisFrame = false;

		double now = 0;
		double topAt = -1e9, shellAt = -1e9, popAt = -1e9;
		std::string shellKey;
		std::vector<SlotState> slots;
		int prevCenter = -1;
		int lastDir = 0;		// the d-pad's direction of the last step (nudge), for a move that is as short both ways
		float thumbFrom = 0, thumbTo = 0;
		double thumbAt = -1e9;
		bool thumbKnown = false;
		bool launchFromHome = false;
		double launchAt = -1;		// auto-launch time on the ready screen
		double launchT0 = -1e9;		// the insert animation's start
		bool launched = false, cued = false;

		// touch
		bool pointerDown = false;
		float downX = 0, downY = 0;
		int pressedAction = 0;		// face button held (drawn pressed)
		int pressedSide = -1;
		int pressedArrow = -1;		// the game list's arrow held: 0 the previous game, 1 the next
		bool railDrag = false;		// the finger is on the game list's rail: the cursor follows it

		// per-render model
		std::vector<SysInfo> sysCache;
		std::map<int, GameInfo> gameCache;

		Pal P;
		Color INK, MUTED, LINE, EDGE, NAVY, RED, SHADE40, SHADE45, SHADE35, BLACKISH;

		Impl(UI* u) : ui(u) {}

		void setPalette(const std::string& variant)
		{
			lightTheme = variant == "light";
			P = lightTheme ? lightPal() : darkPal();
			INK = P.ink; MUTED = P.muted; LINE = P.line; EDGE = P.edge; NAVY = P.navy; RED = P.red;
			SHADE40 = P.shade40; SHADE45 = P.shade45; SHADE35 = P.shade35; BLACKISH = P.blackish;
		}

		float X(float css) const { return css * s; }

		// ---- cache ----
		CacheEntry* find(const std::string& key)
		{
			auto it = cache.find(key);
			if (it == cache.end()) return nullptr;
			it->second.used = frame;
			return &it->second;
		}

		CacheEntry& put(const std::string& key, int group, const Bitmap& b, bool smooth, int ox, int oy)
		{
			CacheEntry& e = cache[key];
			e.sp.tex = be->upload(b, smooth);
			e.sp.ox = ox; e.sp.oy = oy;
			e.used = frame;
			e.group = group;
			e.bytes = (size_t)b.w * b.h * (b.alpha ? 1 : 4);
			return e;
		}

		// after a frame: the least recently used entries that this frame didn't draw go, oldest first, while a group has
		// more entries than its limit or the cache holds more bytes than its budget
		void evict()
		{
			size_t count[G_COUNT] = { 0 }, bytes[G_COUNT] = { 0 }, total = 0;
			for (auto& kv : cache) { count[kv.second.group]++; bytes[kv.second.group] += kv.second.bytes; total += kv.second.bytes; }
			bool overCount = false;
			for (int g = 0; g < G_COUNT; g++) if (count[g] > GROUP_LIMIT[g]) overCount = true;
			bool overBytes = total > budget;
			if (!overCount && !overBytes)
				return;
			if (overBytes && !budgetLogged && log)
			{
				budgetLogged = true;
				char m[320];
				snprintf(m, sizeof m, "texture cache at its budget: %zu entries, %.1f MB > %.1f MB (scale %.2f; text %zu/%.1f, boxes %zu/%.1f, "
					"icons %zu/%.1f, cartridges %zu/%.1f, photos %zu/%.1f MB); dropping the least recently used",
					cache.size(), total / 1048576.0, budget / 1048576.0, s, count[G_TEXT], bytes[G_TEXT] / 1048576.0,
					count[G_BOX], bytes[G_BOX] / 1048576.0, count[G_ICON], bytes[G_ICON] / 1048576.0,
					count[G_CART], bytes[G_CART] / 1048576.0, count[G_PHOTO], bytes[G_PHOTO] / 1048576.0);
				log(m);
			}
			typedef std::map<std::string, CacheEntry>::iterator It;
			std::vector<It> lru;	// in key order, then (stable) by age: the oldest first, as the per-group scan took them
			for (It it = cache.begin(); it != cache.end(); ++it)
				if (it->second.used < frame) lru.push_back(it);
			std::stable_sort(lru.begin(), lru.end(), [](const It& a, const It& b) { return a->second.used < b->second.used; });
			for (It it : lru)
			{
				int g = it->second.group;
				if (count[g] <= GROUP_LIMIT[g] && total <= budget)
				{
					if (!overCount) break;		// within the budget, and no group over its limit
					continue;
				}
				count[g]--;
				total -= it->second.bytes;
				cache.erase(it);
				overCount = false;
				for (int k = 0; k < G_COUNT; k++) if (count[k] > GROUP_LIMIT[k]) overCount = true;
			}
		}

		void request(const std::string& key, int group, bool smooth, std::function<Bitmap(float&, float&)> fn)
		{
			requestedThisFrame = true;
			{
				std::lock_guard<std::mutex> l(qlock);
				if (queued.count(key)) return;
				queued.insert(key);
				jobs.push_front(Job{ key, group, smooth, std::move(fn) });	// newest first: what's on screen now
				// the oldest jobs are what scrolled off long ago (a held direction through a big list): dropped, and
				// asked for again if they come back
				while (jobs.size() > MAX_JOBS)
				{
					queued.erase(jobs.back().key);
					jobs.pop_back();
				}
			}
			if (onQueued) onQueued();
		}

		// the worker's finished pictures into the cache (from update, and from render: ES keeps drawing a view under its
		// menus without updating it)
		void absorb()
		{
			std::vector<Done> ready;
			{
				std::lock_guard<std::mutex> l(qlock);
				ready.swap(done);
				for (auto& r : ready) queued.erase(r.key);
			}
			for (auto& r : ready)
			{
				if (r.bmp.empty()) { CacheEntry& e = cache[r.key]; e.group = r.group; e.used = frame; continue; }
				bool isBox = r.group == G_BOX;
				CacheEntry& e = put(r.key, r.group, r.bmp, r.smooth, isBox ? (int)r.cssW : 0, isBox ? (int)r.cssH : 0);
				if (!isBox) { e.cssW = r.cssW; e.cssH = r.cssH; }
			}
		}

		// ---- assets ----
		Bitmap loadPng(const std::string& name) { return decodeFile(dir + "/" + name); }

		void loadAssets()
		{
			R = Font::load(dir + "/fonts/PixelifySans-Regular.ttf");
			M = Font::load(dir + "/fonts/PixelifySans-Medium.ttf");
			shell = std::make_shared<const Bitmap>(loadPng("game_slot.png"));
			std::string sfx = s > 1.3f ? "@1.6x.png" : "@1x.png";
			logo = loadPng("logo" + sfx);
			wifi = loadPng("wifi" + sfx);
			batt = loadPng("battery" + sfx);
			consoleDark = loadPng("console-dark" + sfx);
			consoleLight = loadPng("console-light" + sfx);
		}

		// ---- boxes ----
		Sprite box(const BoxCss& css, float w, float h)
		{
			int dw = (int)std::lround(X(w)), dh = (int)std::lround(X(h));
			char k[64];
			snprintf(k, sizeof k, "box:%dx%d:", dw, dh);
			std::string key = std::string(k) + css.key;
			if (auto e = find(key)) return e->sp;
			BoxStyle st;
			st.radius = X(css.radius);
			st.border = css.border <= 0 ? 0 : (s == 1 ? css.border : std::max(1.0f, std::floor(css.border * s)));
			st.borderColor = css.borderColor;
			st.bg = css.bg;
			for (auto b : css.bands) st.bands.push_back(Band{ X(b.y0), b.y1 < 0 ? -1 : X(b.y1), b.c });
			st.dither = css.dither;
			st.ditherScale = s;
			st.ditherA = P.ditherA; st.ditherB = P.ditherB;
			for (auto sh : css.shadows) st.shadows.push_back(Shadow{ X(sh.dx), X(sh.dy), X(sh.spread), sh.c, sh.inset });
			if (css.async)
			{
				request(key, G_BOX, true, [st, dw, dh](float& ox, float& oy) -> Bitmap
				{
					PaintedBox pb = paintBox(st, dw, dh, false);	// drawn 1:1, no need to bleed edges
					ox = (float)pb.originX; oy = (float)pb.originY;
					return pb.bmp;
				});
				return Sprite();
			}
			PaintedBox pb = paintBox(st, dw, dh);
			return put(key, G_BOX, pb.bmp, true, pb.originX, pb.originY).sp;
		}

		// the sheet's shape, drawn tinted with each system's colour (painted once, on the worker)
		Sprite mask(float radius, float w, float h)
		{
			int dw = (int)std::lround(X(w)), dh = (int)std::lround(X(h));
			char k[64];
			snprintf(k, sizeof k, "mask:%dx%d:%.2f", dw, dh, X(radius));
			if (auto e = find(k)) return e->sp;
			float r = X(radius);
			request(k, G_BOX, true, [dw, dh, r](float&, float&) -> Bitmap { return paintMask(dw, dh, r); });
			return Sprite();
		}

		// draws a sprite whose CSS box is at (x, y); xf scales about (oxCss, oyCss) and then translates by (tx, 0) CSS px
		struct Xf { float ox = 0, oy = 0, sc = 1, tx = 0, ty = 0, alpha = 1; };
		void drawSprite(const Sprite& sp, float x, float y, const Xf* xf = nullptr, const Color& tint = Color(1, 1, 1, 1))
		{
			if (!sp.valid()) return;
			float dx = X(x), dy = X(y);
			float a = tint.a * (xf ? xf->alpha : 1);
			if (a <= 0) return;
			Color t(tint.r, tint.g, tint.b, a);
			if (!xf || (xf->sc == 1 && xf->tx == 0 && xf->ty == 0))
			{
				be->draw(sp.tex, std::round(dx) - sp.ox, std::round(dy) - sp.oy, (float)sp.tex->w, (float)sp.tex->h, t);
				return;
			}
			float ox = X(xf->ox), oy = X(xf->oy);
			float bx = std::round(dx) - sp.ox, by = std::round(dy) - sp.oy;
			float nx = ox + (bx - ox) * xf->sc + X(xf->tx), ny = oy + (by - oy) * xf->sc + X(xf->ty);
			if (xf->sc == 1) { nx = std::round(nx); ny = std::round(ny); }
			be->draw(sp.tex, nx, ny, sp.tex->w * xf->sc, sp.tex->h * xf->sc, t);
		}

		void drawBox(const BoxCss& css, float x, float y, float w, float h, const Xf* xf = nullptr)
		{
			drawSprite(box(css, w, h), x, y, xf);
		}

		// ---- text ----
		struct TStyle { Font* f; float size; float ls; bool upper; };
		TStyle ts(bool medium, float size, float ls = 0, bool up = false) { return TStyle{ medium ? M.get() : R.get(), size, ls, up }; }
		TextStyle tstyle(const TStyle& t) { TextStyle st; st.font = t.f == M.get() ? M : R; st.size = t.size; st.letterSpacing = t.ls; st.upper = t.upper; return st; }

		float lineHeight(const TStyle& t) { return t.f->normalLineHeight(t.size); }
		float baseline(const TStyle& t, float lh)
		{
			float asc = t.f->ascent(t.size), desc = t.f->descent(t.size);
			return asc + std::floor((lh - (asc + desc)) / 2);
		}

		TextLine line(const std::string& text, const TStyle& t) { return layoutLine(utf8to32(text), tstyle(t)); }

		// draws a laid-out line whose line box is at (x, top) CSS px
		void drawLine(const TextLine& l, const TStyle& t, float x, float top, float lh, const Color& c, const Xf* xf = nullptr)
		{
			if (l.text.empty()) return;
			float bx = X(x), by = X(top);
			if (xf && xf->sc == 1) { bx += X(xf->tx); by += X(xf->ty); }
			int ix = (int)std::floor(bx), iy = (int)std::floor(by);
			float fx = bx - ix, fy = by - iy;
			int qx = (int)std::lround(fx * 8), qy = (int)std::lround(fy * 8);
			std::string key = "txt:";
			for (auto ch : l.text) { key += std::to_string((unsigned)ch); key += ','; }
			char k[96];
			snprintf(k, sizeof k, "|%p|%.2f|%.3f|%.3f|%d|%d|%.2f", (void*)t.f, t.size, t.ls, lh, qx, qy, s);
			key += k;
			CacheEntry* e = find(key);
			if (!e)
			{
				TextBitmap tb = renderLine(l, tstyle(t), s, baseline(t, lh), qx / 8.0f, qy / 8.0f);
				e = &put(key, G_TEXT, tb.bmp, true, tb.padX, tb.padY);
			}
			Color tint(c.r, c.g, c.b, c.a * (xf ? xf->alpha : 1));
			if (xf && xf->sc != 1)
			{
				float ox = X(xf->ox), oy = X(xf->oy);
				float nx = ox + (ix - e->sp.ox - ox) * xf->sc + X(xf->tx), ny = oy + (iy - e->sp.oy - oy) * xf->sc + X(xf->ty);
				be->draw(e->sp.tex, nx, ny, e->sp.tex->w * xf->sc, e->sp.tex->h * xf->sc, tint);
			}
			else
				be->draw(e->sp.tex, (float)(ix - e->sp.ox), (float)(iy - e->sp.oy), (float)e->sp.tex->w, (float)e->sp.tex->h, tint);
		}

		void drawText(const std::string& text, const TStyle& t, float x, float top, const Color& c, float lh = -1)
		{
			drawLine(line(text, t), t, x, top, lh < 0 ? lineHeight(t) : lh, c);
		}

		// ---- images ----
		Sprite icon(const std::string& path)
		{
			if (path.empty()) return Sprite();
			std::string key = "icon:" + path;
			if (auto e = find(key)) return e->sp;
			Bitmap b = decodeFile(path);
			if (b.empty()) return Sprite();
			return put(key, G_ICON, b, false, 0, 0).sp;
		}

		Sprite still(const std::string& name, const Bitmap& b, bool smooth)
		{
			if (b.empty()) return Sprite();
			std::string key = "still:" + name;
			if (auto e = find(key)) return e->sp;
			return put(key, G_ICON, b, smooth, 0, 0).sp;
		}

		// what goes on a cartridge's label: the real card's label art (its scan), else the label art the media tool
		// made from the cover, else the cover without its NINTENDO DS strip
		struct CartArt { std::string scan, label, box, rom, icon; };
		static CartArt artOf(const GameInfo& g) { return CartArt{ g.cart, g.label, g.box, g.rom, g.icon }; }
		static Bitmap labelArt(const CartArt& a)
		{
			if (!a.scan.empty())
			{
				Bitmap b = cardLabelArt(decodeFile(a.scan));
				if (!b.empty()) return b;
			}
			if (!a.label.empty())
			{
				Bitmap b = decodeFile(a.label);
				if (!b.empty()) return b;
			}
			return a.box.empty() ? Bitmap() : coverWithoutStrip(decodeFile(a.box));
		}

		// the mockup's cartridge: the label art filling the label window under the pixel shell, at this CSS size
		Sprite cart(const CartArt& art0, float cw, float ch)
		{
			int dw = (int)std::lround(X(cw)), dh = (int)std::lround(X(ch));
			// or the DS ROM's own icon on it (the host's opt-in): read on the worker; a ROM without one leaves an empty
			// entry, and the label art is drawn instead
			if (!art0.rom.empty())
			{
				char rk[48];
				snprintf(rk, sizeof rk, "romcart:%dx%d:", dw, dh);
				std::string rkey = std::string(rk) + art0.rom;
				if (auto e = find(rkey))
				{
					if (e->sp.valid()) return e->sp;
				}
				else
				{
					request(rkey, G_CART, false, romCart(art0.rom, dw, dh));
					return Sprite();
				}
			}
			char k[48];
			snprintf(k, sizeof k, "cart:%dx%d:", dw, dh);
			std::string key = std::string(k) + art0.scan + "|" + art0.label + "|" + art0.box + "|" + art0.icon;
			if (auto e = find(key)) return e->sp;
			std::shared_ptr<const Bitmap> shellRef = shell;
			float sc = s;
			uint32_t plain = P.cartLabel.rgba8();
			request(key, G_CART, true, [art0, shellRef, sc, dw, dh, plain](float&, float&) -> Bitmap
			{
				const Bitmap& shellCopy = *shellRef;
				// the canvas is 100x104 CSS px; bake it at device resolution, then resample to the drawn size
				int mw = (int)std::lround(100 * sc), mh = (int)std::lround(104 * sc);
				Bitmap master;
				master.w = mw; master.h = mh;
				master.px.assign((size_t)mw * mh * 4, 0);
				Bitmap art = labelArt(art0);
				// no art (a tool or an app without a picture, a game nobody scraped, a picture that doesn't decode): the
				// host's icon for it on a plain label, as the ROM icons are, and never an empty cartridge
				if (art.empty() && !art0.icon.empty())
				{
					Bitmap ic = decodeFile(art0.icon);
					if (!ic.empty())
						return iconCart(ic, shellCopy, sc, dw, dh, plain);
				}
				if (!art.empty())
				{
					// the shell's whole label (x 4..96, y 4..90): the art is all there is on it, no header or margin
					int wx = (int)std::lround(4 * sc), wy = (int)std::lround(4 * sc);
					int ww = (int)std::lround(96 * sc) - wx, wh = (int)std::lround(90 * sc) - wy;
					blit(master, coverTop(art, ww, wh), wx, wy);
				}
				if (!shellCopy.empty())
					blit(master, mw == shellCopy.w && mh == shellCopy.h ? shellCopy : resizeNearest(shellCopy, mw, mh), 0, 0);
				if (dw == mw && dh == mh)
					return master;
				return resize(master, dw, dh);
			});
			return Sprite();
		}

		// a cartridge with a pixel icon instead of label art, dw x dh device px: the label plain in the palette's colour,
		// the icon on it at the largest whole multiple of its size that fits the label window (nearest neighbour: pixel
		// art), the shell over them as the label art's cartridges have it
		static Bitmap iconCart(const Bitmap& icon, const Bitmap& sh, float sc, int dw, int dh, uint32_t label)
		{
			Bitmap out;
			out.w = dw; out.h = dh;
			out.px.assign((size_t)dw * dh * 4, 0);
			// the shell's label window (x 4..96, y 4..90 of its 100x104), at the drawn size
			float fx = dw / 100.0f, fy = dh / 104.0f;
			int wx = (int)std::lround(4 * fx), wy = (int)std::lround(4 * fy);
			int ww = (int)std::lround(96 * fx) - wx, wh = (int)std::lround(90 * fy) - wy;
			for (int y = wy; y < wy + wh; y++)
				for (int x = wx; x < wx + ww; x++)
				{
					uint8_t* p = &out.px[((size_t)y * dw + x) * 4];
					p[0] = (uint8_t)(label >> 24); p[1] = (uint8_t)(label >> 16); p[2] = (uint8_t)(label >> 8); p[3] = 255;
				}
			int side = std::max(1, std::max(icon.w, icon.h));
			int k = std::max(1, std::min(ww, wh) / side);
			int iw = icon.w * k, ih = icon.h * k;
			if (iw > ww || ih > wh)		// an icon larger than the window: fitted into it
			{
				float f = std::min((float)ww / icon.w, (float)wh / icon.h);
				iw = std::max(1, (int)(icon.w * f)); ih = std::max(1, (int)(icon.h * f));
				blit(out, resize(icon, iw, ih), wx + (ww - iw) / 2, wy + (wh - ih) / 2);
			}
			else
				blit(out, resizeNearest(icon, iw, ih), wx + (ww - iw) / 2, wy + (wh - ih) / 2);
			if (!sh.empty())
			{
				int mw = (int)std::lround(100 * sc), mh = (int)std::lround(104 * sc);
				Bitmap m = mw == sh.w && mh == sh.h ? sh : resizeNearest(sh, mw, mh);
				blit(out, dw == mw && dh == mh ? m : resize(m, dw, dh), 0, 0);
			}
			return out;
		}

		// the job for a cartridge with a DS ROM's own icon (uploaded unsmoothed). Empty when the ROM has no icon
		std::function<Bitmap(float&, float&)> romCart(const std::string& rom, int dw, int dh)
		{
			std::shared_ptr<const Bitmap> shellRef = shell;
			float sc = s;
			uint32_t label = P.cartLabel.rgba8();
			return [rom, shellRef, sc, dw, dh, label](float&, float&) -> Bitmap
			{
				Bitmap icon = romIcon(rom);
				if (icon.empty()) return Bitmap();
				return iconCart(icon, *shellRef, sc, dw, dh, label);
			};
		}

		// the box cover fitted into COVER_W x COVER_H CSS px (its fitted CSS size in the cache entry)
		static constexpr float COVER_W = 214, COVER_H = 280;
		CacheEntry* coverArt(const std::string& path)
		{
			if (path.empty()) return nullptr;
			std::string key = "cover:" + path;
			if (auto e = find(key)) return e;
			float sc = s;
			request(key, G_PHOTO, true, [path, sc](float& cssW, float& cssH) -> Bitmap
			{
				Bitmap img = decodeFile(path);
				if (img.empty()) return Bitmap();
				float fw, fh;
				containSize(img.w, img.h, (int)COVER_W, (int)COVER_H, fw, fh);
				cssW = std::max(1.0f, std::round(fw));
				cssH = std::max(1.0f, std::round(fh));
				return resize(img, (int)std::lround(cssW * sc), (int)std::lround(cssH * sc));
			});
			return nullptr;
		}

		// the shine that sweeps over the cover: a slanted band, A8, w x h CSS px plus the slant
		Sprite glint(float h)
		{
			int dh = (int)std::lround(X(h)), band = (int)std::lround(X(22)), slant = (int)std::lround(X(h * 0.45f));
			char k[48];
			snprintf(k, sizeof k, "glint:%d", dh);
			if (auto e = find(k)) return e->sp;
			Bitmap b;
			b.w = band + slant; b.h = dh; b.alpha = true;
			b.px.assign((size_t)b.w * b.h, 0);
			int px = std::max(1, (int)std::lround(s));		// the pixel grid of the look: whole CSS pixels
			for (int y = 0; y < dh; y++)
			{
				int x0 = slant - (int)std::lround((float)(y / px * px) * slant / std::max(1, dh));
				for (int x = x0; x < x0 + band && x < b.w; x++)
					b.px[(size_t)y * b.w + x] = (x - x0) < band / 3 ? 150 : 255;
			}
			return put(k, G_BOX, b, false, 0, 0).sp;
		}

		// ---- model ----
		const SysInfo& sys(int i)
		{
			if (sysCache.empty())
			{
				int n = src->systemCount();
				for (int k = 0; k < n; k++) sysCache.push_back(src->system(k));
			}
			static SysInfo empty;
			return i >= 0 && i < (int)sysCache.size() ? sysCache[i] : empty;
		}
		int sysCount() { sys(0); return (int)sysCache.size(); }
		const GameInfo& game(int i)
		{
			static const GameInfo none;
			if (i < 0 || i >= src->gameCount()) return none;	// an empty library asks the Source for nothing
			auto it = gameCache.find(i);
			if (it == gameCache.end())
				it = gameCache.emplace(i, src->game(i)).first;
			return it->second;
		}

		int count() { return ui->mView == HOME ? sysCount() : src->gameCount(); }
		int center() { return ui->mView == HOME ? ui->mSys : ui->mGame; }

		static int delta(int i, int c, int n)
		{
			int d = (i - c) % n;
			if (d < 0) d += n;
			if (d > n / 2) d -= n;	// JS: d > n / 2 with a float n / 2
			return d;
		}

		// ---- animation state ----
		std::string makeShellKey()
		{
			if (ui->mView == HOME) return "home";
			if (ui->mView == LIBRARY) return "lib-" + std::to_string(ui->mSys);
			return "launch-" + std::to_string(ui->mSys) + "-" + std::to_string(ui->mGame);
		}

		void slotValue(SlotState& st, float& x, float& sc, float& o)
		{
			if (st.trans)
			{
				float p = (float)((now - st.t0) / 420.0);
				if (p >= 1) { st.trans = false; x = st.toX; sc = st.toS; }
				else
				{
					float e = cubicBezier(0.2f, 1.4f, 0.32f, 1, p);
					x = st.fromX + (st.toX - st.fromX) * e;
					sc = st.fromS + (st.toS - st.fromS) * e;
				}
			}
			else { x = st.toX; sc = st.toS; }
			if (st.otrans)
			{
				float p = (float)((now - st.o0) / 240.0);
				if (p >= 1) { st.otrans = false; o = st.toO; }
				else o = st.fromO + (st.toO - st.fromO) * stepsEnd(4, p);
			}
			else o = st.toO;
		}

		bool ghostValue(SlotState& st, float& x, float& sc, float& o)
		{
			if (!st.ghost) return false;
			float p = (float)((now - st.g0) / 420.0);
			if (p >= 1) { st.ghost = false; return false; }
			float e = cubicBezier(0.2f, 1.4f, 0.32f, 1, p);
			x = st.gFromX + (st.gToX - st.gFromX) * e;
			sc = st.gS;
			o = st.gO * (1 - stepsEnd(4, p));
			return o > 0;
		}

		void layout(bool animate)
		{
			int n = count();
			if ((int)slots.size() != n) { slots.assign(n, SlotState()); prevCenter = -1; }
			int c = center();
			// how far the centre moved (in items, the short way round)
			int move = prevCenter >= 0 && prevCenter < n ? delta(c, prevCenter, n) : 0;
			// half way round (two items: either way is one step) the pressed direction says which way it went, so
			// left slides left and right slides right
			if (lastDir && n % 2 == 0 && std::abs(move) == n / 2) move = lastDir > 0 ? n / 2 : -(n / 2);
			lastDir = 0;
			if (std::abs(move) > 3) animate = false;
			prevCenter = c;
			for (int i = 0; i < n; i++)
			{
				SlotState& st = slots[i];
				int d = delta(i, c, n);
				float cx, cs, co;
				slotValue(st, cx, cs, co);
				float tx = d * 122.0f, tsc = d == 0 ? 1.0f : 0.84f, to = std::abs(d) > 3 ? 0.0f : 1.0f;
				bool wrapped = st.known && move != 0 && d != st.d - move;
				if (!animate || !st.known)
				{
					st.trans = false; st.otrans = false; st.ghost = false;
					st.toX = tx; st.toS = tsc; st.toO = to;
				}
				else if (wrapped)
				{
					if (std::abs(st.d) <= 3 && co > 0)
					{
						st.ghost = true; st.g0 = now;
						st.gFromX = cx; st.gToX = (st.d - move) * 122.0f; st.gS = cs; st.gO = co;
					}
					int from = d + move;
					st.fromX = from * 122.0f; st.fromS = from == 0 ? 1.0f : 0.84f;
					st.toX = tx; st.toS = tsc; st.t0 = now; st.trans = true;
					st.fromO = 0; st.toO = to; st.o0 = now; st.otrans = to > 0;
					if (!st.otrans) st.toO = to;
				}
				else
				{
					if (tx != st.toX || tsc != st.toS)
					{
						st.fromX = cx; st.fromS = cs; st.toX = tx; st.toS = tsc;
						st.t0 = now; st.trans = true;
					}
					if (to != st.toO)
					{
						st.fromO = co; st.toO = to; st.o0 = now; st.otrans = true;
					}
				}
				st.d = d;
				st.known = true;
			}
		}

		float thumbLeft(int at, int n, float inner) { return n < 2 ? 0 : at * (inner - 42) / (n - 1); }

		// what render(animate) does in the mockup
		void render(bool animate)
		{
			topAt = now;
			std::string key = makeShellKey();
			if (shellKey != key)
			{
				shellKey = key;
				shellAt = now;
				slots.clear();
				thumbKnown = false;
				layout(false);
			}
			else
				layout(animate);
			popAt = now;
		}

		void dataChanged()
		{
			sysCache.clear();
			gameCache.clear();
		}

		// ---- input ----
		void setSystem(int i)
		{
			int n = sysCount();
			if (n == 0) return;
			ui->mSys = ((i % n) + n) % n;
			src->selectSystem(ui->mSys);
			render(true);
		}

		void setGame(int i)
		{
			int n = src->gameCount();
			if (n == 0) return;
			ui->mGame = ((i % n) + n) % n;
			src->selectGame(ui->mGame);
			gameCache.clear();
			render(true);
		}

		void nudge(int dir)
		{
			lastDir = dir;
			if (ui->mView == HOME) setSystem(ui->mSys + dir);
			else if (ui->mView == LIBRARY) setGame(ui->mGame + dir);
		}

		// up/down on a game list: a page (the five cartridges in view) at a time; it stops at either end of the list,
		// and goes round only from the end itself
		bool page(int dir)
		{
			if (ui->mView != LIBRARY) return false;
			int n = src->gameCount(), g = ui->mGame + dir * 5;
			if (n < 2) return true;
			if (g < 0) g = ui->mGame == 0 ? n - 1 : 0;
			else if (g >= n) g = ui->mGame == n - 1 ? 0 : n - 1;
			setGame(g);
			return true;
		}

		void open()
		{
			if (ui->mView == HOME)
			{
				if (sysCount() == 0) return;
				// an app's tile starts the app: its entry's ready screen, as X (Resume) opens a system's last game. B there
				// is its library, as after Resume
				if (sys(ui->mSys).app)
				{
					resume();
					if (ui->mView == LAUNCH) return;
				}
				int g = src->openLibrary(ui->mSys);
				gameCache.clear();
				ui->mGame = g < 0 ? 0 : g;
				ui->mView = LIBRARY;
				render(false);
			}
			else if (ui->mView == LIBRARY)
			{
				if (src->gameCount() == 0) return;
				ui->mView = LAUNCH;
				launchFromHome = false;
				startInsert();
				render(false);
			}
			else if (ui->mView == LAUNCH && !launched && now - launchT0 < INS_LIFT + INS_SLIDE)
			{
				// A again: straight to the click
				launchT0 = now - (INS_LIFT + INS_SLIDE);
				launchAt = now + (LAUNCH_DELAY - INS_LIFT - INS_SLIDE);
			}
		}

		void startInsert()
		{
			src->cue("ready");
			launchT0 = now;
			launchAt = now + LAUNCH_DELAY;
			launched = false;
			cued = false;
		}

		void back()
		{
			if (ui->mView == LAUNCH)
			{
				launchAt = -1;
				if (launchFromHome)
				{
					int g = src->openLibrary(ui->mSys);
					if (g >= 0) ui->mGame = g;
				}
				ui->mView = LIBRARY;
				gameCache.clear();
				render(false);
			}
			else if (ui->mView == LIBRARY)
			{
				src->backToHome();
				ui->mView = HOME;
				render(false);
			}
		}

		void toggleFav()
		{
			if (ui->mView == LIBRARY && src->gameCount() == 0) return;	// nothing to favourite
			if (ui->mView != LAUNCH) src->cue("fav");
			if (ui->mView == HOME) { src->toggleSystemFav(ui->mSys); sysCache.clear(); }
			else if (ui->mView == LIBRARY) { src->toggleGameFav(ui->mGame); gameCache.clear(); }
			if (ui->mView != LAUNCH) render(false);
		}

		void resume()
		{
			if (ui->mView != HOME) return;
			int g = src->resumeGame(ui->mSys);
			if (g < 0) return;
			gameCache.clear();
			ui->mGame = g;
			ui->mView = LAUNCH;
			launchFromHome = true;
			startInsert();
			render(false);
		}


		// ---- geometry shared by drawing and hit tests ----
		struct Geo
		{
			float bubbleY = 60, bubbleH = 92;
			float reelY = 160, reelH = 226;
			std::vector<TextLine> titleLines, subLines;
			float countW = 0;		// the count's text ("03 / 07")
			float railW = 0;		// the rail's track: what the widest count leaves (one length for the whole list)
			Face left, right;
			float leftW = 0, rightW = 0;
			float startW = 0;
		};

		TStyle tTitle() { return ts(true, 26, 0.52f); }
		TStyle tSub() { return ts(false, 20); }
		TStyle tKbd() { return ts(true, 16); }
		TStyle tFace() { return ts(true, 19); }

		float kbdW(const std::string& k) { return 12 + line(k, tKbd()).width; }
		// the rail's count ("01 / 07"); the rail is the rest of the row
		float countWidth(int at, int n)
		{
			char a[16], b[16];
			snprintf(a, sizeof a, "%02d", n == 0 ? 0 : at + 1);
			snprintf(b, sizeof b, "%02d", n);
			return line(a, ts(true, 18)).width + line(" / ", ts(false, 18)).width + line(b, ts(false, 18)).width;
		}
		// the widest the count gets on a list of n (every digit of the position as wide as the widest digit): the rail
		// keeps one length while the number changes ("98" is narrower than "372"), so a finger on it means the same game
		// when it lifts as during its last move
		float countWidthMax(int n)
		{
			char b[16];
			snprintf(b, sizeof b, "%02d", n);
			float widest = 0;
			for (char d = '0'; d <= '9'; d++) widest = std::max(widest, line(std::string(1, d), ts(true, 18)).width);
			return strlen(b) * widest + line(" / ", ts(false, 18)).width + line(b, ts(false, 18)).width;
		}
		float faceW(const Face& f) { return f.kind == 2 ? 3 + 12 + line(f.label, tFace()).width + 12 + 3 : 3 + 12 + kbdW(f.kbd) + 8 + line(f.label, tFace()).width + 12 + 3; }

		Geo geometry()
		{
			Geo g;
			View v = ui->mView;
			std::string title, sub;
			if (v == HOME) { auto& sy = sys(ui->mSys); title = sy.name; sub = sy.maker; }
			else if (v == LIBRARY && count() == 0) { title = src->emptyTitle(); sub = src->emptyHint(); }
			else
			{
				auto& gm = game(ui->mGame);
				title = gm.title;
				sub = v == LAUNCH ? "Ready" : gm.sub;
			}
			g.titleLines = layoutWrap(utf8to32(title), tstyle(tTitle()), 470, 2);
			g.subLines.clear();
			if (!sub.empty()) g.subLines.push_back(layoutEllipsis(utf8to32(sub), tstyle(tSub()), 470));
			// one height for every name: room for two title lines and the subtitle, so a long name doesn't move the
			// shelf (the text is centred in it)
			g.bubbleH = 2 * 32.5f + 3 + lineHeight(tSub()) + 18 + 6;
			g.bubbleY = v == LAUNCH ? 26 : 60;
			g.reelY = g.bubbleY + g.bubbleH + 8;
			g.reelH = 386 - g.reelY;

			int n = v == LAUNCH ? 1 : count();
			int at = v == LAUNCH ? 0 : center();
			g.countW = countWidth(at, n);
			g.railW = 604 - 12 - countWidthMax(n);

			if (v == HOME)
			{
				g.left = Face{ "Y", "Fav", sys(ui->mSys).fav ? 1 : 0, ACT_FAV };
				g.right = Face{ "X", sys(ui->mSys).app ? "Start" : "Resume", 0, ACT_RESUME };	// an app has nothing to resume
			}
			else if (v == LIBRARY)
			{
				g.left = Face{ "B", "Back", 0, ACT_BACK };
				if (src->gameCount() == 0) g.right = Face{ "", "No games", 2, ACT_NONE };	// nothing to favourite
				else g.right = Face{ "Y", "Fav", game(ui->mGame).fav ? 1 : 0, ACT_FAV };
			}
			else
			{
				g.left = Face{ "B", "Back", 0, ACT_BACK };
				g.right = Face{ "", "Starting", 2, ACT_NONE };
			}
			g.leftW = faceW(g.left);
			g.rightW = faceW(g.right);
			g.startW = line("START", ts(true, 20, 2.4f)).width + 16;
			return g;
		}

		// ---- styles ----
		BoxCss screenBox()
		{
			BoxCss b; b.key = "screen"; b.radius = 2; b.bg = P.screenBg; b.dither = true; b.async = true;
			b.shadows = { inset(0, 0, 4, P.screenFrame), inset(0, 8, 0, P.screenBand) };
			return b;
		}
		// the sheet without its background colour: dither, inset shadows, border and shadow over transparency; the
		// colour (color-mix of the system's accent) is the tinted mask() under it, so one paint serves every system
		BoxCss sheetOverlay()
		{
			BoxCss b; b.key = "sheet"; b.radius = 18; b.border = 3; b.borderColor = EDGE; b.dither = true; b.async = true;
			b.shadows = { inset(0, 6, 0, P.sheetHi), inset(0, -8, 0, P.sheetLo), outer(4, 4, SHADE40) };
			return b;
		}
		void drawSheet(const Color& accent, float x, float y)
		{
			Sprite m = mask(18, 604, 406), o = box(sheetOverlay(), 604, 406);
			if (!m.valid() || !o.valid()) return;
			drawSprite(m, x, y, nullptr, mix(accent, P.sheetMix, P.sheetBase));
			drawSprite(o, x, y);
		}
		BoxCss heroBox()
		{
			BoxCss b; b.key = "hero"; b.radius = 18; b.border = 3; b.borderColor = EDGE; b.bg = P.panelBody;
			b.bands = { Band{ 0, 8, P.panelBand }, Band{ 8, -1, P.panelBody } };
			b.shadows = { inset(0, -6, 0, P.panelLow), outer(4, 4, SHADE45) };
			return b;
		}
		// the home screen's "last played" chip: 3 border + 8 + the 54 px cartridge + 10 + the text + 12 + 3 border; the text
		// is the eyebrow or the title, at most 300 px of it
		float chipWidth(const SysInfo& sy)
		{
			TStyle te = ts(false, 17, 1.36f, true), tt = ts(true, 20);
			float tw = std::max(layoutLine(utf8to32(sy.resumeEyebrow), tstyle(te)).width,
			                    std::min(300.0f, layoutLine(utf8to32(sy.resumeTitle), tstyle(tt)).width));
			return 3 + 8 + 54 + 10 + tw + 12 + 3;
		}
		BoxCss chipBox()
		{
			BoxCss b; b.key = "chip"; b.radius = 12; b.border = 3; b.borderColor = EDGE; b.bg = P.chipBody;
			b.bands = { Band{ 0, 8, P.chipBand }, Band{ 8, -1, P.chipBody } };
			b.shadows = { outer(3, 3, SHADE40) };
			return b;
		}
		// the box art's frame: a pixel border, no fill (the cover covers it)
		BoxCss coverBox()
		{
			BoxCss b; b.key = "cover"; b.radius = 4; b.border = 3; b.borderColor = P.frameBorder; b.bg = P.frameBg;
			b.shadows = { outer(4, 4, SHADE40) };
			return b;
		}
		BoxCss meterBox()
		{
			BoxCss b; b.key = "meter"; b.radius = 2; b.border = 3; b.borderColor = EDGE; b.bg = P.meterBg;
			b.shadows = { inset(2, 2, 0, SHADE45) };
			return b;
		}
		BoxCss brandBox()
		{
			BoxCss b; b.key = "brand"; b.radius = 4; b.bg = P.brandBg;
			b.shadows = { outer(2, 2, BLACKISH) };
			return b;
		}
		BoxCss kbdBox() { BoxCss b; b.key = "kbd"; b.radius = 4; b.bg = P.kbdBg; return b; }
		BoxCss ruleBox() { BoxCss b; b.key = "rule"; b.radius = 2; b.bg = P.rule; return b; }
		BoxCss bubbleBox()
		{
			BoxCss b; b.key = "bubble"; b.radius = 12; b.border = 3; b.borderColor = LINE; b.bg = P.bubbleBody;
			b.bands = { Band{ 0, 10, P.bubbleBand }, Band{ 10, -1, P.bubbleBody } };
			b.shadows = { inset(0, -6, 0, P.bubbleLow), outer(4, 4, BLACKISH) };
			return b;
		}
		// the letter over the rail's thumb while it is dragged: a small bubble in the big one's style
		BoxCss letterBox()
		{
			BoxCss b; b.key = "letter"; b.radius = 8; b.border = 3; b.borderColor = LINE; b.bg = P.bubbleBody;
			b.bands = { Band{ 0, 6, P.bubbleBand }, Band{ 6, -1, P.bubbleBody } };
			b.shadows = { inset(0, -4, 0, P.bubbleLow), outer(3, 3, BLACKISH) };
			return b;
		}
		BoxCss trackBox()
		{
			BoxCss b; b.key = "reeltrack"; b.radius = 18; b.bg = P.trackBg; b.dither = true;
			b.shadows = { inset(0, 4, 0, P.trackShade), inset(0, -4, 0, P.trackHi) };
			return b;
		}
		BoxCss plateBox()
		{
			BoxCss b; b.key = "plate"; b.radius = 18; b.border = 3; b.borderColor = EDGE; b.bg = Color::hex(0xf0b429);
			b.bands = { Band{ 0, 14, Color::hex(0xffe68a) }, Band{ 132, -1, Color::hex(0x8a5a00) } };
			b.shadows = { inset(0, -8, 0, Color(0, 0, 0, 0.35f)), outer(4, 4, SHADE40) };
			return b;
		}
		BoxCss slotBox(bool on)
		{
			BoxCss b; b.key = on ? "slot1" : "slot0"; b.radius = 14; b.border = 3; b.borderColor = on ? P.slotOn : EDGE;
			b.bg = P.panelBody;
			b.bands = { Band{ 0, 8, P.panelBand }, Band{ 8, -1, P.panelBody } };
			b.shadows = { inset(0, -6, 0, P.panelLow), outer(3, 3, SHADE45) };
			return b;
		}
		BoxCss badgeBox() { BoxCss b; b.key = "badge"; b.radius = 2; b.border = 2; b.borderColor = Color::hex(0xffffff); b.bg = RED; return b; }
		BoxCss railBox()
		{
			BoxCss b; b.key = "rail"; b.radius = 2; b.border = 2; b.borderColor = EDGE; b.bg = P.railBg; b.dither = true;
			b.shadows = { inset(2, 2, 0, SHADE45) };
			return b;
		}
		BoxCss thumbBox()
		{
			BoxCss b; b.key = "thumb"; b.radius = 2; b.border = 2; b.borderColor = EDGE; b.bg = Color::hex(0xf0b429);
			b.bands = { Band{ 0, 4, Color::hex(0xffe68a) } };
			return b;
		}
		BoxCss faceBox(int kind, bool pressed)
		{
			BoxCss b; b.radius = 10; b.border = 3;
			if (kind == 1)
			{
				b.key = "face1"; b.borderColor = P.favBorder; b.bg = P.favBody;
				b.bands = { Band{ 0, 8, P.favBand }, Band{ 8, -1, P.favBody } };
			}
			else
			{
				b.key = kind == 2 ? "face2" : "face0"; b.borderColor = kind == 2 ? P.faceMuted : EDGE; b.bg = P.panelBody;
				b.bands = { Band{ 0, 8, P.bubbleBand }, Band{ 8, -1, P.panelBody } };
			}
			if (kind == 2) {}
			else if (pressed) { b.key += "p"; b.shadows = { outer(1, 1, BLACKISH) }; }
			else b.shadows = { inset(0, -4, 0, P.panelLow), outer(3, 3, BLACKISH) };
			return b;
		}
		// the console's cartridge slot on the ready screen: a dark mouth with a lip
		BoxCss slotMouthBox()
		{
			BoxCss b; b.key = "mouth"; b.radius = 6; b.border = 3; b.borderColor = EDGE; b.bg = P.slotDark;
			b.shadows = { inset(0, 6, 0, Color(0, 0, 0, 0.5f)), outer(3, 3, SHADE40) };
			return b;
		}

		// ---- animation values ----
		bool holdStill = false;	// the source's topMotion() off: no rise, no bob (read in update)
		float rise(double at) { double t = now - at; if (holdStill || t < 0 || t >= 280) return 0; return 10 * (1 - stepsEnd(4, (float)(t / 280))); }
		float pop() { double t = now - popAt; if (t < 0 || t >= 320) return 0; return 6 * (1 - stepsEnd(3, (float)(t / 320))); }
		float bob()
		{
			if (holdStill) return 0;
			double t = std::fmod(std::max(0.0, now - topAt), 2400.0) / 2400.0;
			if (t < 0.5) return -4 * stepsEnd(2, (float)(t / 0.5));
			return -4 + 4 * stepsEnd(2, (float)((t - 0.5) / 0.5));
		}
		float march() { double t = std::fmod(std::max(0.0, now - topAt), 800.0) / 800.0; return 16 * stepsEnd(4, (float)t); }
		float thumbValue()
		{
			double t = now - thumbAt;
			if (t >= 350 || t < 0) return thumbTo;
			return thumbFrom + (thumbTo - thumbFrom) * stepsEnd(5, (float)(t / 350));
		}

		// ---- drawing: top screen ----
		void drawStatus(float ry)
		{
			// wifi, battery, brand
			if (!wifi.empty())
				drawSprite(still("wifi", wifi, false), 18, 24 + ry, nullptr, Color(INK.r, INK.g, INK.b, src->online() ? 1.0f : 0.35f));
			std::string bmode = src->batteryMode();
			float lvl = src->battery();
			bool pctText = bmode == "text" && lvl >= 0;
			auto battery = [&](float bx)		// the 26x14 icon, its level 3 px in
			{
				drawSprite(still("batt", batt, false), bx, 25 + ry, nullptr, INK);
				float l = lvl < 0 ? 0.75f : lvl;
				float fw = std::round(16 * std::min(1.0f, std::max(0.0f, l)) * s) / s;
				if (fw > 0)
					be->fill(std::round(X(bx + 3)), std::round(X(28 + ry)), std::round(X(fw)), std::round(X(8)), INK);
			};
			// on the left after the Wi-Fi: "icon only" the battery icon; "icon and text" the percentage and then the icon
			// ("78% [battery]"), the percentage right-aligned in room for "100%" so nothing moves as it changes, and
			// the brand moved right after them (1.5.5)
			float brandX = 78;
			if (pctText)
			{
				TStyle tp = ts(false, 20);
				std::string pct = std::to_string((int)std::round(std::min(1.0f, lvl) * 100)) + "%";
				float slot = line("100%", tp).width, pw = line(pct, tp).width;
				drawText(pct, tp, 44 + slot - pw, 20 + ry, INK);
				float bx = 44 + slot + 6;
				if (!batt.empty())
					battery(bx);
				brandX = std::round(bx + (batt.empty() ? 0 : 26) + 10);
			}
			else if (!batt.empty() && !bmode.empty())
				battery(44);
			drawBox(brandBox(), brandX, 19 + ry, 148.23f, 26);
			if (!logo.empty())
				drawSprite(still("logo", logo, true), brandX + 7, 22 + ry);

			std::string clock = src->clockText(), date = src->dateText();
			float cw = line(clock, ts(true, 20)).width, dw = line(date, ts(false, 20)).width;
			drawText(clock, ts(true, 20), 622 - cw, 20 + ry, INK);
			drawText(date, ts(false, 20), 622 - cw - 8 - dw, 20 + ry, INK);
		}

		// the game list's top screen, left: the box cover in a pixel frame, bobbing like the home screen's icon, its
		// shadow on the sheet and a shine that sweeps over it every few seconds
		void drawCover(const GameInfo& gm, const SysInfo& sy, float ry)
		{
			const float cx = 163, cy = 263;		// the cover's centre at rest
			CacheEntry* ce = coverArt(gm.box);
			bool pending = ce == nullptr && !gm.box.empty();
			if (ce && !ce->sp.valid()) ce = nullptr;
			float w = ce ? ce->cssW : 176, h = ce ? ce->cssH : 176;
			float b = bob();
			float x = std::round(cx - w / 2), y = std::round(cy - h / 2) + ry;
			// the shadow: narrower while the cover is up
			float fw = std::round(w * 0.78f) + (b < 0 ? -10 : 0), fy = std::round(cy + h / 2 + 18) + ry;
			Color fl = P.floor;
			for (int r = 0; r < 3; r++)
			{
				float rw = fw - r * r * 10;
				be->fill(std::round(X(cx - rw / 2)), std::round(X(fy - 1 + r * 2)), std::round(X(rw)), std::round(X(2)), fl);
			}
			if (pending) return;		// the picture is on its way: the frame comes with it
			drawBox(coverBox(), x - 3, y - 3 + b, w + 6, h + 6);
			if (ce)
			{
				drawSprite(ce->sp, x, y + b);
				// the shine: every 5 s, across the cover in 8 steps
				double t = std::fmod(std::max(0.0, now - topAt), GLINT_PERIOD);
				if (t < GLINT_SWEEP)
				{
					Sprite gl = glint(h);
					float k = stepsEnd(8, (float)(t / GLINT_SWEEP));
					float gw = gl.tex ? gl.tex->w / s : 0;
					float gx = x - gw + (w + gw) * k;
					be->pushClip(std::round(X(x)), std::round(X(y + b)), std::round(X(w)), std::round(X(h)));
					if (gl.valid()) be->draw(gl.tex, std::round(X(gx)), std::round(X(y + b)), (float)gl.tex->w, (float)gl.tex->h, P.glint);
					be->popClip();
				}
			}
			else
			{
				// no cover: the system's icon on the frame's background
				Sprite ic = icon(sy.icon);
				if (ic.valid()) be->draw(ic.tex, std::round(X(cx - 48)), std::round(X(cy - 48 + ry + b)), std::round(X(96)), std::round(X(96)), Color(1, 1, 1, 1));
			}
		}

		// the game list's top screen, right: title, genre and year, the play stats and the achievements
		void drawGameInfo(const GameInfo& gm, const SysInfo& sy, float ry)
		{
			const float L = 300, W = 296;
			TStyle tt = ts(true, 26, 0.52f), tsb = ts(false, 20);
			std::vector<TextLine> tl = layoutWrap(utf8to32(gm.title), tstyle(tt), (int)W, 2);
			// no word left alone on the second line ("Pokemon Black Version / 2"): narrow the first line until they balance
			for (int w = (int)W - 16; tl.size() == 2 && tl[1].width < tl[0].width * 0.35f && w > W * 0.55f; w -= 16)
			{
				std::vector<TextLine> t2 = layoutWrap(utf8to32(gm.title), tstyle(tt), w, 2);
				if (t2.size() != 2 || t2[1].text.size() < tl[1].text.size()) break;
				bool cut = false;
				for (auto& l : t2) if (!l.text.empty() && l.text.back() == U'\u2026') cut = true;
				if (cut) break;
				tl = t2;
			}
			bool launch = ui->mView == LAUNCH;
			std::string subText = launch ? "Starting..." : gm.sub;
			TextLine sl = layoutEllipsis(utf8to32(subText), tstyle(tsb), (int)(W - (launch ? 16 : 0)));

			// the stats: values over labels, the columns as wide as their text
			std::string vals[3] = { gm.plays, gm.last, gm.time };
			std::string labs[3] = { "Plays", "Last played", "Time" };
			float vsize = 24;
			TStyle tv = ts(true, vsize), tlab = ts(false, 15, 1.2f, true);
			TextLine lv[3], ll[3];
			float colW[3], total = 0;
			for (int pass = 0; pass < 2; pass++)
			{
				total = 0;
				for (int i = 0; i < 3; i++)
				{
					lv[i] = line(vals[i], tv);
					ll[i] = layoutLine(utf8to32(labs[i]), tstyle(tlab));
					colW[i] = std::max(lv[i].width, ll[i].width);
					total += colW[i];
				}
				if (total + 2 * 16 <= W || pass == 1) break;
				tv = ts(true, vsize = 20);
			}
			float gap = std::min(28.0f, (W - total) / 2);

			float titleH = tl.size() * 32.5f, block = titleH + 4 + 24 + 20 + 48 + 22 + 22 + 6 + 14;
			float top = std::round(263 - block / 2) + ry;
			float y = top;
			for (size_t i = 0; i < tl.size(); i++)
				drawLine(tl[i], tt, L, y + 32.5f * i, 32.5f, INK);
			y += titleH + 4;
			float sx = L;
			if (launch)
			{
				// the live dot
				float base = y + baseline(tsb, 24), dy = base - 1 - 8;
				be->fill(std::round(X(sx - 2)), std::round(X(dy - 2)), std::round(X(12)), std::round(X(12)), EDGE);
				be->fill(std::round(X(sx)), std::round(X(dy)), std::round(X(8)), std::round(X(8)), RED);
				sx += 16;
			}
			drawLine(sl, tsb, sx, y, 24, launch ? INK : MUTED);
			y += 24 + 20;
			be->fill(std::round(X(L)), std::round(X(y - 10)), std::round(X(W)), std::round(X(2)), Color(LINE.r, LINE.g, LINE.b, 0.5f));
			float x = L;
			for (int i = 0; i < 3; i++)
			{
				drawLine(lv[i], tv, x, y, 28, INK);
				drawLine(ll[i], tlab, x, y + 28, 20, MUTED);
				x += colW[i] + gap;
			}
			y += 48 + 22;

			// achievements
			TStyle ta = ts(false, 16, 1.0f, true);
			TextLine la = layoutEllipsis(utf8to32(gm.achLabel), tstyle(ta), (int)W);
			drawLine(la, ta, L, y, 22, MUTED);
			y += 22 + 6;
			drawBox(meterBox(), L, y, W, 14);
			float pct = gm.achB > 0 ? std::round(gm.achA * 100.0f / gm.achB) : 0;
			float fillW = (W - 6) * pct / 100;
			if (fillW > 0)
			{
				float l = std::round(X(L + 3)), t = std::round(X(y + 3)), r = std::round(X(L + 3 + fillW)), bt = std::round(X(y + 11));
				be->fill(l, t, r - l, bt - t, sy.accent);
				be->pushClip(l, t, r - l, bt - t);
				float bp = march();
				Color w45(1, 1, 1, 0.45f);
				for (float mx = L + 3 + bp + 6 - 16; mx < L + 3 + fillW; mx += 8)
				{
					float a0 = std::round(X(mx)), a1 = std::round(X(mx + 2));
					if (a1 > l && a0 < r) be->fill(a0, t, a1 - a0, bt - t, w45);
				}
				be->popClip();
			}
		}

		// the game list's top screen with no games: the title and where the games go, in the info column (the frame on
		// the left holds the system's icon, as drawCover draws it without a cover)
		void drawEmpty(float ry)
		{
			const float L = 300, W = 296;
			TStyle tt = ts(true, 26, 0.52f), th = ts(false, 20);
			std::vector<TextLine> tl = layoutWrap(utf8to32(src->emptyTitle()), tstyle(tt), (int)W, 2), hl;
			std::string hint = src->emptyHint();
			if (!hint.empty()) hl = layoutWrap(utf8to32(hint), tstyle(th), (int)W, 3);
			float block = tl.size() * 32.5f + (hl.empty() ? 0 : 8 + hl.size() * 24);
			float y = std::round(263 - block / 2) + ry;
			for (size_t i = 0; i < tl.size(); i++) drawLine(tl[i], tt, L, y + 32.5f * i, 32.5f, INK);
			y += tl.size() * 32.5f + 8;
			for (size_t i = 0; i < hl.size(); i++) drawLine(hl[i], th, L, y + 24 * i, 24, MUTED);
		}

		static constexpr double GLINT_PERIOD = 5000, GLINT_SWEEP = 560;

		void drawTop()
		{
			be->beginScreen(0);
			drawBox(screenBox(), 0, 0, 640, 480);
			float ry = rise(topAt);
			drawStatus(ry);
			const SysInfo& sy = sys(ui->mSys);
			drawSheet(sy.accent, 18, 60 + ry);

			if (ui->mView == HOME)
			{
				bool chip = sy.hasResume;
				float total = 96 + 10 + 52 + (chip ? 10 + 74 : 0);
				float y0 = 81 + (366 - total) / 2 + ry;
				float b = bob();
				drawBox(heroBox(), 272, y0 + b, 96, 96);
				Sprite ic = icon(sy.icon);
				if (ic.valid()) be->draw(ic.tex, std::round(X(280)), std::round(X(y0 + 8 + b)), std::round(X(80)), std::round(X(80)), Color(1, 1, 1, 1));

				// stats; an app's tile has its time alone (one entry, started or not, is nothing to count)
				std::string vals[3] = { std::to_string(sy.games), std::to_string(sy.played), sy.time };
				std::string labs[3] = { sy.games == 1 ? "Game" : "Games", "Played", "Time" };
				TStyle tb = ts(true, 28), tl = ts(false, 17, 1.36f, true);
				int c0 = sy.app ? 2 : 0;
				float colW[3], total3 = 18.0f * (2 - c0);
				TextLine lb[3], ll[3];
				for (int i = c0; i < 3; i++)
				{
					lb[i] = line(vals[i], tb);
					ll[i] = layoutLine(utf8to32(labs[i]), tstyle(tl));
					colW[i] = std::max(84.0f, std::max(lb[i].width, ll[i].width));
					total3 += colW[i];
				}
				float x = 320 - total3 / 2, sy0 = y0 + 106;
				for (int i = c0; i < 3; i++)
				{
					drawLine(lb[i], tb, x + (colW[i] - lb[i].width) / 2, sy0, 28, INK);
					drawLine(ll[i], tl, x + (colW[i] - ll[i].width) / 2, sy0 + 31, lineHeight(tl), MUTED);
					x += colW[i] + 18;
				}

				if (chip)
				{
					TStyle te = ts(false, 17, 1.36f, true), tt = ts(true, 20);
					TextLine le = layoutLine(utf8to32(sy.resumeEyebrow), tstyle(te));
					TextLine lt = layoutEllipsis(utf8to32(sy.resumeTitle), tstyle(tt), 300);
					float cw = chipWidth(sy);
					float cx = 320 - cw / 2, cy = y0 + 168;
					drawBox(chipBox(), cx, cy, cw, 74);
					Sprite c = cart(CartArt{ sy.resumeCart, sy.resumeLabel, sy.resumeBox, "", sy.icon }, 54, 56);
					drawSprite(c, cx + 11, cy + 9);
					float tx = cx + 11 + 54 + 10, ty = cy + 13.5f;
					drawLine(le, te, tx, ty, lineHeight(te), MUTED);
					drawLine(lt, tt, tx, ty + 23, lineHeight(tt), INK);
				}
				// the systems either side: their chip's box and cartridge made on the worker now, so stepping to one
				// draws it at once. Made when first shown, the box (12-15 ms, sized to the title) cost a frame and the
				// cartridge came a few frames after the chip: a flicker on every new system (issue 27)
				int n = sysCount();
				for (int k = -1; n > 1 && k <= 1; k += 2)
				{
					const SysInfo& ny = sys((ui->mSys + k + n) % n);
					if (!ny.hasResume) continue;
					BoxCss b = chipBox(); b.async = true;
					box(b, chipWidth(ny), 74);
					cart(CartArt{ ny.resumeCart, ny.resumeLabel, ny.resumeBox, "", ny.icon }, 54, 56);
				}
			}
			else if (ui->mView == LIBRARY && count() == 0)
			{
				drawCover(GameInfo(), sy, ry);	// the frame with the system's icon
				drawEmpty(ry);
			}
			else
			{
				const GameInfo& gm = game(ui->mGame);
				drawCover(gm, sy, ry);
				drawGameInfo(gm, sy, ry);
			}
			be->endScreen();
		}

		// ---- drawing: bottom screen ----
		void drawFace(const Face& f, float x, float w, bool pressed)
		{
			float dx = pressed ? 2 : 0;
			drawBox(faceBox(f.kind, pressed), x + dx, 422 + dx, w, 40);
			Color lc = f.kind == 1 ? P.favInk : (f.kind == 2 ? MUTED : INK);
			float lx = x + 15 + dx;
			if (f.kind != 2)
			{
				float kw = kbdW(f.kbd);
				drawBox(kbdBox(), lx, 430.5f + dx, kw, 23);
				drawText(f.kbd, tKbd(), lx + 6, 432.5f + dx, P.kbdInk);
				lx += kw + 8;
			}
			drawText(f.label, tFace(), lx, 431 + dx, lc);
		}

		// the game list's ◀ ▶ (its shoulders are their tap zones): a stepped triangle on the look's pixel grid, whole CSS
		// px at 1x and 2 device px at 1.6x, as tall as the names' capitals (13 px at 1x, 22 at 1.6x) and about half as
		// wide (an A8 sprite in the text's colour, the thumb's blue while held)
		struct ArrowDims { int u, rows, w, h; };
		ArrowDims arrowDims()
		{
			int u = std::max(1, (int)std::lround(s));
			int rows = std::max(3, (int)std::lround(13 * s / u)) | 1;
			return ArrowDims{ u, rows, (rows / 2 + 1) * u, rows * u };
		}
		Sprite arrow(bool right)
		{
			ArrowDims a = arrowDims();
			char k[48];
			snprintf(k, sizeof k, "arrow:%d:%d:%d", right ? 1 : 0, a.rows, a.u);
			if (auto e = find(k)) return e->sp;
			Bitmap b;
			b.w = a.w; b.h = a.h; b.alpha = true;
			b.px.assign((size_t)b.w * b.h, 0);
			int half = a.rows / 2;
			for (int r = 0; r < a.rows; r++)
			{
				int span = (half + 1 - std::abs(r - half)) * a.u, x0 = right ? 0 : b.w - span;	// the tip's row is the widest
				for (int y = r * a.u; y < (r + 1) * a.u; y++)
					std::fill(b.px.begin() + (size_t)y * b.w + x0, b.px.begin() + (size_t)y * b.w + x0 + span, (uint8_t)255);
			}
			return put(k, G_ICON, b, false, 0, 0).sp;
		}
		// x: the ◀'s left edge, the ▶'s right edge; cy: the row's middle (CSS px)
		void drawArrow(bool right, float x, float cy, bool held)
		{
			Sprite a = arrow(right);
			if (!a.valid()) return;
			float dx = std::round(X(x)) - (right ? a.tex->w : 0), dy = std::round(X(cy) - a.tex->h / 2.0f);
			be->draw(a.tex, dx, dy, (float)a.tex->w, (float)a.tex->h, held ? Color::hex(0xf0b429) : INK);
		}

		// a triangle painted once about its own apex and drawn at (cx, top) CSS px (the letter bubble's pointer moves)
		void drawPointer(const char* name, float cx, float top, float halfW, float h, const Color& c)
		{
			char k[64];
			snprintf(k, sizeof k, "ptr:%s", name);
			CacheEntry* e = find(k);
			if (!e)
			{
				int bx, byy;
				Bitmap b = paintTriangle(-X(halfW), 0, X(halfW), 0, 0, X(h), c, bx, byy);
				e = &put(k, G_BOX, b, true, -bx, -byy);
			}
			be->draw(e->sp.tex, std::round(X(cx)) - e->sp.ox, std::floor(X(top)) - e->sp.oy, (float)e->sp.tex->w, (float)e->sp.tex->h, Color(1, 1, 1, 1));
		}

		// while the rail is dragged: the game's first letter ("#" for a digit or a sign) in a bubble over the thumb, in
		// the band between the reel's plate and the rail, so it covers nothing
		void drawLetter(float thumbX)
		{
			std::u32string t = upper(utf8to32(game(ui->mGame).title));
			char32_t c = t.empty() ? U'#' : t[0];
			bool letter = (c >= U'A' && c <= U'Z') || (c >= 0xc0 && c <= 0x17f && c != 0xd7 && c != 0xf7);
			if (!letter || !M->hasGlyph(c)) c = U'#';
			const float w = 36, h = 30, ptrH = 8;
			float top = 391 - ptrH - h + 3;				// the pointer's tip 3 px over the thumb
			float mid = thumbX + 21;					// the thumb's middle
			float x = std::round(std::min(std::max(mid - w / 2, 12.0f), 628 - w));
			drawBox(letterBox(), x, top, w, h);
			float pm = std::min(std::max(mid, x + 12), x + w - 12), pb = top + h - 3;
			drawPointer("letterO", pm, pb, 8, ptrH, LINE);
			drawPointer("letterI", pm, pb, 5, ptrH - 3, P.bubbleBody);
			TStyle tl = ts(true, 20);
			TextLine l = layoutLine(std::u32string(1, c), tstyle(tl));
			float lh = lineHeight(tl);
			drawLine(l, tl, x + (w - l.width) / 2, top + 3 + (h - 6 - lh) / 2, lh, INK);
		}

		// the game list's rail under a finger at x (CSS px): the game whose thumb has its middle nearest to it
		void railTo(float x)
		{
			int n = src->gameCount();
			if (ui->mView != LIBRARY || n == 0) return;
			float inner = 604 - 12 - countWidthMax(n) - 4;
			int at = n < 2 ? 0 : (int)std::lround((x - 20 - 21) * (n - 1) / std::max(1.0f, inner - 42));
			at = std::min(n - 1, std::max(0, at));
			if (at != ui->mGame) setGame(at);
		}

		void drawBottom()
		{
			be->beginScreen(1);
			drawBox(screenBox(), 0, 0, 640, 480);
			float rb = rise(shellAt);
			Geo g = geometry();
			View v = ui->mView;
			int n = count();

			if (v != LAUNCH)
			{
				// shoulders: the names either side. A shelf or a list of one has no other side (both said its own name):
				// the rule alone
				bool sides = n > 1;
				float kl = kbdW("L"), kr = kbdW("R");
				std::string prev, next;
				if (sides && v == HOME) { prev = sys((ui->mSys - 1 + n) % n).name; next = sys((ui->mSys + 1) % n).name; }
				else if (sides) { prev = game((ui->mGame - 1 + n) % n).title; next = game((ui->mGame + 1) % n).title; }
				TStyle tsh = ts(false, 20);
				TextLine lp = layoutEllipsis(utf8to32(prev), tstyle(tsh), 180), ln = layoutEllipsis(utf8to32(next), tstyle(tsh), 180);
				// a game list's shoulders are its touch arrows too: ◀ and ▶ between the button and the name, a flex item
				// more (8 px apart), so a name of the full 180 px still ends short of the rule
				bool arrows = v == LIBRARY && sides;
				float aw = arrows ? arrowDims().w / s + 8 : 0;
				drawBox(ruleBox(), 284, 31.5f + rb, 72, 3);
				if (sides || n == 0)
				{
					drawBox(kbdBox(), 18, 21.5f + rb, kl, 23);
					drawText("L", tKbd(), 24, 23.5f + rb, P.kbdInk);
					drawLine(lp, tsh, 18 + kl + 8 + aw, 21 + rb, 24, INK);
					drawBox(kbdBox(), 622 - kr, 21.5f + rb, kr, 23);
					drawText("R", tKbd(), 622 - kr + 6, 23.5f + rb, P.kbdInk);
					drawLine(ln, tsh, 622 - kr - 8 - aw - ln.width, 21 + rb, 24, INK);
				}
				if (arrows)
				{
					drawArrow(false, 18 + kl + 8, 33 + rb, pressedArrow == 0);
					drawArrow(true, 622 - kr - 8, 33 + rb, pressedArrow == 1);
				}
			}

			// bubble
			float by = g.bubbleY + rb;
			drawBox(bubbleBox(), 66, by, 508, g.bubbleH);
			float contentH = g.titleLines.size() * 32.5f + 3 + g.subLines.size() * lineHeight(tSub());
			float cy = by + 3 + 8 + ((g.bubbleH - 24) - contentH) / 2;
			float p = pop();
			for (size_t i = 0; i < g.titleLines.size(); i++)
				drawLine(g.titleLines[i], tTitle(), 320 - g.titleLines[i].width / 2, cy + p + 32.5f * i, 32.5f, INK);
			float ey = cy + g.titleLines.size() * 32.5f + 3;
			for (size_t i = 0; i < g.subLines.size(); i++)
				drawLine(g.subLines[i], tSub(), 320 - g.subLines[i].width / 2, ey + p + lineHeight(tSub()) * i, lineHeight(tSub()), MUTED);
			// pointer: the border-coloured triangle and the inner one that opens the border into it
			float pb = by + g.bubbleH - 3;
			drawTriangle("ptrO", 306, pb, 334, pb, 320, pb + 16, LINE);
			drawTriangle("ptrI", 309, pb, 331, pb, 320, pb + 13, P.bubbleBody);

			// reel
			float ry = g.reelY + rb;
			be->pushClip(std::round(X(10)), std::round(X(ry)), std::round(X(620)), std::round(X(g.reelH)));
			if (v != LAUNCH && n > 0)
				drawBox(trackBox(), 26, ry + 34, 588, 80);
			if (v != LAUNCH && n > 0)	// no plate (and no START) over an empty library
				drawBox(plateBox(), 258, ry + 4, 124, 162);

			if (v == LAUNCH)
				drawInsert(ry);
			else
			{
				// slots by z-index: |d| >= 2, |d| == 1, the centre; DOM order within a level
				std::vector<std::pair<int, int>> order;
				for (int i = 0; i < n && i < (int)slots.size(); i++)
				{
					int d = slots[i].d;
					int z = d == 0 ? 4 : 3 - std::min(std::abs(d), 2);
					order.push_back({ z, i });
				}
				std::stable_sort(order.begin(), order.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.first < b.first; });
				// the copies leaving round the end go under everything
				std::vector<std::pair<int, int>> all;
				for (auto& o : order) if (slots[o.second].ghost) all.push_back({ -1, o.second });
				all.insert(all.end(), order.begin(), order.end());
				for (auto& o : all)
				{
					int i = o.second;
					float tx, sc, op;
					if (o.first < 0) { if (!ghostValue(slots[i], tx, sc, op)) continue; }
					else slotValue(slots[i], tx, sc, op);
					if (op <= 0) continue;
					Xf xf; xf.ox = 320; xf.oy = ry + 16; xf.sc = sc; xf.tx = tx; xf.alpha = op;
					if (v == HOME)
					{
						const SysInfo& si = sys(i);
						drawBox(slotBox(o.first >= 0 && slots[i].d == 0), 272, ry + 16, 96, 96, &xf);
						Sprite ic = icon(si.icon);
						if (ic.valid())
						{
							float ix = 320 + (280 - 320) * sc + tx, iy = ry + 16 + 8 * sc;
							be->draw(ic.tex, X(ix), X(iy), X(80 * sc), X(80 * sc), Color(1, 1, 1, op));
						}
						if (si.fav) drawBox(badgeBox(), 272 + 77, ry + 16 + 9, 10, 10, &xf);
					}
					else
					{
						Sprite c = cart(artOf(game(i)), 92.3077f, 96);
						drawSprite(c, 273.846f, ry + 16, &xf);
					}
				}
				if (n > 0)
				{
					TStyle tsr = ts(true, 20, 2.4f);
					TextLine lsr = line("START", tsr);
					drawLine(lsr, tsr, 320 - lsr.width / 2, ry + 124, 24, NAVY);
				}
			}
			be->popClip();

			// rail
			int at = v == LAUNCH ? 0 : center(), total = v == LAUNCH ? 1 : n;
			float trackW = g.railW;
			drawBox(railBox(), 18, 396, trackW, 10);
			float target = thumbLeft(at, total, trackW - 4);
			if (!thumbKnown || railDrag) { thumbKnown = true; thumbFrom = thumbTo = target; thumbAt = -1e9; }	// dragged: under the finger
			else if (target != thumbTo)
			{
				// a glide cut short starts the next one from where it was going: started from where it was, a held
				// direction (a new target every few frames, before the first of the 5 steps at 70 ms) kept the thumb
				// standing still until the button was let go (issue 27)
				thumbFrom = now - thumbAt < 350 ? thumbTo : thumbValue();
				thumbTo = target; thumbAt = now;
			}
			drawBox(thumbBox(), 20 + thumbValue(), 394, 42, 14);
			char a[16], b[16];
			snprintf(a, sizeof a, "%02d", total == 0 ? 0 : at + 1);
			snprintf(b, sizeof b, "%02d", total);
			float cx = 622 - g.countW;
			TextLine la = line(a, ts(true, 18)), lsl = line(" / ", ts(false, 18)), lb = line(b, ts(false, 18));
			drawLine(la, ts(true, 18), cx, 390, 22, INK);
			drawLine(lsl, ts(false, 18), cx + la.width, 390, 22, MUTED);
			drawLine(lb, ts(false, 18), cx + la.width + lsl.width, 390, 22, MUTED);

			// face buttons
			drawFace(g.left, 18, g.leftW, pressedAction && pressedSide == 0);
			drawFace(g.right, 622 - g.rightW, g.rightW, pressedAction && pressedSide == 1 && g.right.kind != 2);
			if (railDrag && v == LIBRARY && n > 0)
				drawLetter(20 + thumbValue());
			be->endScreen();
		}

		// ---- the ready screen: the cartridge goes into the console ----
		static constexpr double INS_LIFT = 120, INS_SLIDE = 400, INS_CLICK = 90, INS_SETTLE = 110;
		// then the game starts, once the click has been heard (ES closes its audio as it launches)
		static constexpr double LAUNCH_DELAY = INS_LIFT + INS_SLIDE + INS_CLICK + INS_SETTLE + 200;

		void drawInsert(float ry)
		{
			double t = now - launchT0;
			// the console under it: a face with the slot's mouth and a power light
			float mouthY = ry + 170, clipY = mouthY + 9;
			// Banana: the back of the console as pixel art, 560 wide, its slot where the box's mouth is (tools/make-console.py)
			const Bitmap& console = lightTheme ? consoleLight : consoleDark;
			if (!console.empty()) drawSprite(still(lightTheme ? "console-light" : "console-dark", console, false), 40, mouthY + 6);
			else drawBox(heroBox(), 150, ry + 154, 340, 110);
			drawBox(slotMouthBox(), 260, mouthY, 120, 28);	// Banana: a card-sized slot (was 180 wide at 230)
			bool clicked = t >= INS_LIFT + INS_SLIDE;
			Color led = clicked ? Color::hex(0x3ce07a) : Color::hex(0x7a2a30);
			be->fill(std::round(X(409)), std::round(X(mouthY + 20)), std::round(X(14)), std::round(X(10)), EDGE);	// Banana: right of the slot, on the slab
			be->fill(std::round(X(411)), std::round(X(mouthY + 22)), std::round(X(10)), std::round(X(6)), led);

			// where the cartridge's top is: lift, then down into the slot with an accelerating fall, a click past the
			// end, and back up onto it
			float y0 = ry + 14, yEnd = clipY - 47;
			float y;
			if (t < INS_LIFT) y = y0 - 6 * stepsEnd(2, (float)(t / INS_LIFT));
			else if (t < INS_LIFT + INS_SLIDE)
			{
				float e = cubicBezier(0.55f, 0, 0.9f, 0.45f, (float)((t - INS_LIFT) / INS_SLIDE));
				y = (y0 - 6) + (yEnd + 3 - (y0 - 6)) * e;
			}
			else if (t < INS_LIFT + INS_SLIDE + INS_CLICK) y = yEnd + 3;
			else if (t < INS_LIFT + INS_SLIDE + INS_CLICK + INS_SETTLE)
				y = yEnd + 3 - 3 * stepsEnd(3, (float)((t - INS_LIFT - INS_SLIDE - INS_CLICK) / INS_SETTLE));
			else y = yEnd;
			y = std::round(y);

			// the cartridge, cut off where it enters the slot; the mouth's front edge over it
			be->pushClip(std::round(X(10)), std::round(X(ry)), std::round(X(620)), std::round(X(clipY)) - std::round(X(ry)));
			Sprite c = cart(artOf(game(ui->mGame)), 100, 104);
			drawSprite(c, 270, y);
			be->popClip();
			be->fill(std::round(X(263)), std::round(X(clipY - 1)), std::round(X(114)), std::round(X(3)), P.slotLip);

			// the click: sparks off the slot's corners for a moment
			double ct = t - (INS_LIFT + INS_SLIDE);
			if (ct >= 0 && ct < 240)
			{
				float k = stepsEnd(3, (float)(ct / 240));
				float d = 8 + 18 * k;
				Color sp(1, 1, 1, 1 - k * 0.8f);
				const float sx[2] = { 262, 378 };
				for (float x : sx)
				{
					float dir = x < 320 ? -1.0f : 1.0f;
					be->fill(std::round(X(x + dir * d)), std::round(X(clipY - 6 - d * 0.6f)), std::round(X(4)), std::round(X(4)), sp);
					be->fill(std::round(X(x + dir * (d + 6))), std::round(X(clipY - 1)), std::round(X(4)), std::round(X(4)), sp);
					be->fill(std::round(X(x + dir * d * 0.5f)), std::round(X(clipY - 12 - d)), std::round(X(4)), std::round(X(4)), sp);
				}
			}
			if (t >= INS_LIFT + INS_SLIDE - 90 && !cued) { cued = true; src->cue("insert"); }
		}

		void drawTriangle(const char* name, float x0, float y0, float x1, float y1, float x2, float y2, const Color& c)
		{
			// cached by shape at the origin; y offsets reuse it
			char k[64];
			snprintf(k, sizeof k, "tri:%s", name);
			float oy = std::floor(X(y0));
			CacheEntry* e = find(k);
			if (!e)
			{
				int bx, byy;
				Bitmap b = paintTriangle(X(x0), X(y0) - oy, X(x1), X(y1) - oy, X(x2), X(y2) - oy, c, bx, byy);
				e = &put(k, G_BOX, b, true, -bx, -byy);
			}
			be->draw(e->sp.tex, (float)(-e->sp.ox), oy - e->sp.oy, (float)e->sp.tex->w, (float)e->sp.tex->h, Color(1, 1, 1, 1));
		}

		// ---- touch hit tests (CSS px of the bottom screen) ----
		int hit(float x, float y, int& slotIndex, int& side)
		{
			Geo g = geometry();
			View v = ui->mView;
			slotIndex = -1; side = -1;
			auto in = [&](float rx, float ry, float rw, float rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; };
			if (in(18, 422, g.leftW, 40)) { side = 0; return g.left.action; }
			if (in(622 - g.rightW, 422, g.rightW, 40)) { side = 1; return g.right.action; }
			if (v == LAUNCH) return ACT_NONE;
			// the shoulders (with a game list's arrows): each half of the row, from the screen's edge and as tall as the
			// gap over the bubble, for a finger
			if (in(0, 10, 276, 46)) return ACT_PREV;
			if (in(364, 10, 276, 46)) return ACT_NEXT;
			// a game list's rail, from the reel's foot to the face buttons: a tap jumps there, a drag scrubs the list
			if (v == LIBRARY && count() > 0 && in(12, 384, g.railW + 12, 36)) return ACT_RAIL;
			if (in(320 - g.startW / 2, g.reelY + 120, g.startW, 32)) return ACT_OPEN;
			if (!in(10, g.reelY, 620, g.reelH)) return ACT_NONE;
			// topmost slot first
			int best = -1, bestZ = -1;
			for (int i = 0; i < (int)slots.size(); i++)
			{
				float tx, sc, op;
				slotValue(slots[i], tx, sc, op);
				if (op <= 0) continue;
				float sx = 320 - 48 * sc + tx, sy = g.reelY + 16, sw = 96 * sc;
				if (!in(sx, sy, sw, sw)) continue;
				int d = slots[i].d, z = d == 0 ? 4 : 3 - std::min(std::abs(d), 2);
				if (z >= bestZ) { bestZ = z; best = i; }
			}
			if (best >= 0) { slotIndex = best; return ACT_SLOT; }
			return ACT_NONE;
		}

		void act(int a, int slot)
		{
			switch (a)
			{
			case ACT_PREV: nudge(-1); break;
			case ACT_NEXT: nudge(1); break;
			case ACT_OPEN: open(); break;
			case ACT_BACK: back(); break;
			case ACT_FAV: toggleFav(); break;
			case ACT_RESUME: resume(); break;
			case ACT_SLOT:
				if (ui->mView == HOME) { if (slot == ui->mSys) open(); else setSystem(slot); }
				else if (ui->mView == LIBRARY) { if (slot == ui->mGame) open(); else setGame(slot); }
				break;
			}
		}
	};

	// ---- UI -----------------------------------------------------------------------------------------------------------

	UI::UI(Source* src, Backend* be, const std::string& assetDir, float scale, const std::string& variant) : d(new Impl(this))
	{
		d->setPalette(variant);
		d->src = src;
		d->be = be;
		d->dir = assetDir;
		d->s = scale;
		d->budget = (size_t)(BUDGET_1X * (double)scale * scale);
		d->loadAssets();
	}

	UI::~UI() {}

	void UI::setHome(int sys)
	{
		bool same = mView == HOME && mSys == sys && !d->shellKey.empty();
		mView = HOME;
		mSys = sys;
		d->launchAt = -1;
		if (!same) d->render(false);
	}

	void UI::setLibrary(int sys, int game)
	{
		bool same = mView == LIBRARY && mSys == sys && mGame == game && !d->shellKey.empty();
		mView = LIBRARY;
		mSys = sys;
		mGame = game;
		d->launchAt = -1;
		d->gameCache.clear();
		if (!same) d->render(false);
	}

	void UI::setLaunch(int sys, int game, bool fromHome)
	{
		mView = LAUNCH;
		mSys = sys;
		mGame = game;
		d->launchFromHome = fromHome;
		d->startInsert();
		d->gameCache.clear();
		d->render(false);
	}

	bool UI::settled() const
	{
		std::lock_guard<std::mutex> l(d->qlock);
		return !d->requestedThisFrame && d->jobs.empty() && d->done.empty();
	}

	bool UI::launchPending() const { return mView == LAUNCH && !d->launched; }

	void UI::dropTextures()
	{
		d->cache.clear();
	}

	void UI::dataChanged()
	{
		d->dataChanged();
		int n = d->count();
		if ((int)d->slots.size() != n)
			d->layout(false);
	}

	bool UI::press(Button b)
	{
		switch (b)
		{
		case LEFT: case L: d->nudge(-1); return true;
		case RIGHT: case R: d->nudge(1); return true;
		case UP: return d->page(-1);
		case DOWN: return d->page(1);
		case A: d->open(); return true;
		case B: if (mView == HOME) return false; d->back(); return true;
		case X: if (mView != HOME) return false; d->resume(); return true;
		case Y: if (mView == LAUNCH) return false; d->toggleFav(); return true;
		default: return false;
		}
	}

	void UI::jumpTo(int game)
	{
		if (mView == LIBRARY && game != mGame) d->setGame(game);
	}

	bool UI::touch(int screen, float x, float y, bool down)
	{
		if (screen != 1) return false;
		float cx = x / d->s, cy = y / d->s;
		if (down)
		{
			d->pointerDown = true;
			d->downX = cx; d->downY = cy;
			d->pressedAction = 0; d->pressedSide = -1; d->pressedArrow = -1; d->railDrag = false;	// a lift that never came
			int slot, side;
			int a = d->hit(cx, cy, slot, side);
			if ((a == ACT_FAV || a == ACT_RESUME || a == ACT_BACK) && side >= 0) { d->pressedAction = a; d->pressedSide = side; }
			if ((a == ACT_PREV || a == ACT_NEXT) && mView == LIBRARY) d->pressedArrow = a == ACT_PREV ? 0 : 1;
			// the rail: the cursor jumps under the finger, and follows it until it lifts (touchMove)
			if (a == ACT_RAIL) { d->railDrag = true; d->railTo(cx); }
			return true;
		}
		if (!d->pointerDown) return false;
		d->pointerDown = false;
		d->pressedAction = 0; d->pressedSide = -1; d->pressedArrow = -1;
		if (d->railDrag)
		{
			d->railDrag = false;
			if (x >= 0 && y >= 0) d->railTo(cx);	// (-1, -1): lifted off the panel, where the last move left it
			return true;
		}
		float dx = cx - d->downX, dy = cy - d->downY;
		if (std::fabs(dx) > 56 && std::fabs(dx) > std::fabs(dy) * 1.2f)
		{
			d->nudge(dx < 0 ? 1 : -1);
			return true;
		}
		int slot, side;
		int a = d->hit(d->downX, d->downY, slot, side);
		if (a != ACT_NONE) d->act(a, slot);
		return true;
	}

	void UI::touchCancel()
	{
		d->pointerDown = false;
		d->railDrag = false;
		d->pressedAction = 0; d->pressedSide = -1; d->pressedArrow = -1;
	}

	bool UI::touchMove(int screen, float x, float)
	{
		if (screen != 1 || !d->pointerDown || !d->railDrag) return false;	// taps and swipes act when the finger lifts
		d->railTo(x / d->s);
		return true;
	}

	void UI::update(double nowMs)
	{
		d->now = nowMs;
		d->holdStill = !d->src->topMotion();
		d->absorb();
		if (mView == LAUNCH && d->launchAt >= 0 && nowMs >= d->launchAt && !d->launched)
		{
			d->launched = true;
			d->launchAt = -1;
			d->src->launch(mGame);
		}
	}

	void UI::render()
	{
		d->absorb();	// ES draws the view under its menus without updating it: what the worker finished goes in here
		d->frame++;
		d->requestedThisFrame = false;
		d->drawTop();
		d->drawBottom();
		d->evict();
	}

	bool UI::animating() const
	{
		double now = d->now;
		if (now - d->topAt < 280 || now - d->shellAt < 280 || now - d->popAt < 320 || now - d->thumbAt < 350) return true;
		for (auto& st : d->slots)
			if ((st.trans && now - st.t0 < 420) || (st.otrans && now - st.o0 < 240) || (st.ghost && now - st.g0 < 420)) return true;
		if (d->launchAt >= 0) return true;
		std::lock_guard<std::mutex> l(d->qlock);
		return !d->jobs.empty() || !d->done.empty();
	}

	int UI::nextWake() const
	{
		if (animating()) return 0;
		double now = d->now;
		// the looping animations step: the icon or cover bob every 600 ms; on a game list also the meter stripes every
		// 200 ms and, every 5 s, the cover's shine in 8 steps
		double t = now - d->topAt;
		auto nextOf = [t](double period) { return (std::floor(t / period) + 1) * period - t; };
		double next = d->holdStill ? 1e9 : nextOf(600);	// held still: no bob to step
		if (mView != HOME)
		{
			next = std::min(next, nextOf(200));
			double g = std::fmod(t, Impl::GLINT_PERIOD), step = Impl::GLINT_SWEEP / 8;
			next = std::min(next, g < Impl::GLINT_SWEEP ? (std::floor(g / step) + 1) * step - g : Impl::GLINT_PERIOD - g);
		}
		return next >= 1e9 ? -1 : (int)std::ceil(next);
	}

	bool UI::workOnce()
	{
		Job job;
		{
			std::lock_guard<std::mutex> l(d->qlock);
			if (d->jobs.empty()) return false;
			job = std::move(d->jobs.front());
			d->jobs.pop_front();
		}
		Done r;
		r.key = job.key; r.group = job.group; r.smooth = job.smooth;
		r.bmp = job.fn(r.cssW, r.cssH);
		{
			std::lock_guard<std::mutex> l(d->qlock);
			d->done.push_back(std::move(r));	// its key stays queued until absorb() takes it
		}
		if (d->onFinished) d->onFinished();
		return true;
	}

	UI::CacheStats UI::cacheStats() const
	{
		CacheStats st;
		st.budget = d->budget;
		for (auto& kv : d->cache)
		{
			const CacheEntry& e = kv.second;
			st.entries++; st.bytes += e.bytes;
			st.groupEntries[e.group]++; st.groupBytes[e.group] += e.bytes;
			if (e.used == d->frame) { st.frameEntries++; st.frameBytes += e.bytes; }
		}
		std::lock_guard<std::mutex> l(d->qlock);
		st.jobs = d->jobs.size();
		st.done = d->done.size();
		for (auto& r : d->done) st.doneBytes += r.bmp.px.size();
		st.queued = d->queued.size();
		return st;
	}

	void UI::setLog(std::function<void(const std::string&)> log) { d->log = log; }

	void UI::setNotifiers(std::function<void()> queued, std::function<void()> finished)
	{
		d->onQueued = queued;
		d->onFinished = finished;
	}

	void UI::finishWork() { while (workOnce()) {} }
}
