#include "carts.h"
#include "cart3d.h"
#include <algorithm>
#include <cmath>

namespace banana
{
	namespace
	{
		// the palette: near-black neutrals, white text in three strengths, the system's colour for accents
		const uint32_t INK = 0xf4f5f7, SOFT = 0xa3a8b0, FAINT = 0x6b7079, PANEL = 0x22242a, BOTTOM_BG = 0x111215;
		const float SHELF_Y = 296;			// the shelf's floor on the bottom screen (layout px)
		const float ROW_H = 120, PICK_H = 152;	// the cartridges' heights: in the row, and the chosen one
		const float HERO_H = 232;			// the top screen's cartridge

		float ease(float t) { t = std::min(1.0f, std::max(0.0f, t)); return 1 - (1 - t) * (1 - t) * (1 - t); }
		uint32_t rgbOf(const rnds::Color& c) { return (uint32_t)(c.r * 255) << 16 | (uint32_t)(c.g * 255) << 8 | (uint32_t)(c.b * 255); }
		uint32_t mix(uint32_t a, uint32_t b, float t)
		{
			auto ch = [&](int sh) { return (uint32_t)(((a >> sh) & 255) * (1 - t) + ((b >> sh) & 255) * t) << sh; };
			return ch(16) | ch(8) | ch(0);
		}
		// the cartridge model for a system (its id, or its icon's file name in the favourites' mixed list)
		std::string modelOf(std::string id)
		{
			size_t sl = id.rfind('/');
			if (sl != std::string::npos) id = id.substr(sl + 1);
			size_t dot = id.rfind('.');
			if (dot != std::string::npos) id = id.substr(0, dot);
			if (id == "md" || id == "genesis") return "megadrive";
			if (id == "ps1" || id == "playstation") return "psx";
			for (const char* k : { "nds", "gba", "gb", "gbc", "nes", "snes", "n64", "megadrive", "psx" }) if (id == k) return id;
			return "other";
		}
		std::string lowerOf(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
		const float TOP_FLOOR = 352;		// the top screen's floor (the large cartridge stands on it)

		// a small hash for the backgrounds' grain (the same every time: the pictures are made once)
		inline float grain(int x, int y)
		{
			uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return ((h ^ (h >> 16)) & 1023) / 1023.0f - 0.5f;
		}
		inline float smooth01(float a, float b, float v) { float t = std::min(1.0f, std::max(0.0f, (v - a) / (b - a))); return t * t * (3 - 2 * t); }

		// a cartridge's reflection in the glossy floor: each column mirrored about its lowest solid pixel (where the
		// cartridge stands, its bottom edge slanted by the angle), fading out over frac of its height and softened; with
		// shadowW (device px) its shadows on the floor baked in under it too (a soft one and a darker one where it
		// touches: one picture to blend instead of three; unit: device px per layout px). top: its first row in the
		// cartridge's picture
		rnds::Bitmap reflectionOf(const rnds::Bitmap& c, float frac, int& top, float shadowW = 0, float unit = 1)
		{
			rnds::Bitmap r;
			if (c.empty() || c.alpha) return r;
			const int W = c.w, H = c.h;
			std::vector<int> foot(W, -1);
			int minFoot = H, maxFoot = 0;
			for (int x = 0; x < W; x++)
				for (int y = H - 1; y >= 0; y--)
					if (c.px[((size_t)y * W + x) * 4 + 3] > 160) { foot[x] = y; minFoot = std::min(minFoot, y); maxFoot = std::max(maxFoot, y); break; }
			if (minFoot >= H) return r;
			const int len = std::max(4, (int)(H * frac));
			// the shadows: centred under the picture, on the floor line
			const float floorRow = H * (1 - CART_MARGIN);
			struct Sh { float cy, rx, ry, a; } sh[2] = { { floorRow + 5 * unit, shadowW / 2, shadowW * 0.08f, 0.32f },
				{ floorRow + 1 * unit, shadowW * 0.4f, shadowW * 0.064f, 0.45f } };
			top = minFoot + 1;
			int bottom = maxFoot + len + 1;
			if (shadowW > 0)
				for (auto& q : sh) { top = std::min(top, (int)std::floor(q.cy - q.ry)); bottom = std::max(bottom, (int)std::ceil(q.cy + q.ry) + 1); }
			top = std::max(0, top);
			const int RH = bottom - top;
			std::vector<float> pm((size_t)W * RH * 4, 0.0f);
			for (int x = 0; x < W; x++)
			{
				if (foot[x] < 0) continue;
				for (int k = 1; k <= len; k++)
				{
					int sy = foot[x] - (k - 1), dy = foot[x] + k - top;
					if (sy < 0 || dy >= RH) break;
					float f = 1 - (float)k / len, fade = 0.30f * f * f;
					const uint8_t* sp = &c.px[((size_t)sy * W + x) * 4];
					float a = sp[3] / 255.0f * fade;
					float* d = &pm[((size_t)dy * W + x) * 4];
					d[0] = sp[0] / 255.0f * a; d[1] = sp[1] / 255.0f * a; d[2] = sp[2] / 255.0f * a; d[3] = a;
				}
			}
			// softened: a box blur across and down (the floor isn't a perfect mirror)
			int rad = std::max(1, H / 120);
			std::vector<float> tmp(pm.size());
			auto blur = [&](const std::vector<float>& src, std::vector<float>& dst, bool down)
			{
				int L = down ? RH : W, M = down ? W : RH;
				for (int m = 0; m < M; m++)
					for (int l = 0; l < L; l++)
					{
						float sum[4] = { 0, 0, 0, 0 };
						for (int o = -rad; o <= rad; o++)
						{
							int q = l + o;
							if (q < 0 || q >= L) continue;
							const float* sp = &src[((size_t)(down ? q * W + m : m * W + q)) * 4];
							for (int k = 0; k < 4; k++) sum[k] += sp[k];
						}
						float* dp = &dst[((size_t)(down ? l * W + m : m * W + l)) * 4];
						for (int k = 0; k < 4; k++) dp[k] = sum[k] / (2 * rad + 1);
					}
			};
			blur(pm, tmp, false);
			blur(tmp, pm, true);
			// the shadows under it (black: only their alpha), the reflection over them
			if (shadowW > 0)
				for (int y = 0; y < RH; y++)
					for (int x = 0; x < W; x++)
					{
						float keep = 1;
						for (auto& q : sh)
						{
							float dx = (x + 0.5f - W / 2.0f) / q.rx, dy = (top + y + 0.5f - q.cy) / q.ry, d = dx * dx + dy * dy;
							if (d < 1) keep *= 1 - q.a * (1 - d) * (1 - d);
						}
						float sa = 1 - keep;
						float* p = &pm[((size_t)y * W + x) * 4];
						p[3] += sa * (1 - p[3]);
					}
			r.w = W; r.h = RH;
			r.px.assign((size_t)W * RH * 4, 0);
			for (size_t i = 0, n = (size_t)W * RH; i < n; i++)
			{
				float a = pm[i * 4 + 3];
				if (a < 1.5f / 255) continue;
				for (int k = 0; k < 3; k++) r.px[i * 4 + k] = (uint8_t)std::min(255.0f, pm[i * 4 + k] / a * 255 + 0.5f);
				r.px[i * 4 + 3] = (uint8_t)std::min(255.0f, a * 255 + 0.5f);
			}
			return r;
		}

		// a screen's background, or the rectangle (x0, y0, w, h) of it (device px; s: device px per layout px; H: the
		// screen's height): a wall in the system's colour fading to near-black with a faint grid of dots, a glossy floor
		// from where the cartridges stand (lighter at its edge), a pool of light on both behind and under the cartridge
		// in front, then the lens: a vignette, and grain (which also keeps the dark gradients from banding). With glow, a
		// soft light of that colour on it too (the chosen art's, behind the top screen's cartridge): an ellipse round
		// (gcx, gcy) with radii grx, gry (device px), at strength ga
		rnds::Bitmap backgroundRect(int screen, uint32_t accent, float s, int x0, int y0, int w, int h, int H,
			uint32_t glow = 0, float gcx = 0, float gcy = 0, float grx = 1, float gry = 1, float ga = 0)
		{
			rnds::Bitmap b;
			b.w = w; b.h = h;
			b.px.resize((size_t)w * h * 4);
			uint32_t top = screen == 0 ? mix(accent, 0x0d0e11, 0.72f) : mix(accent, BOTTOM_BG, 0.93f);
			uint32_t bot = screen == 0 ? 0x0c0d10 : BOTTOM_BG;
			uint32_t light = mix(accent, 0xffffff, 0.35f);
			const float lr = ((light >> 16) & 255) / 255.0f, lg = ((light >> 8) & 255) / 255.0f, lb = (light & 255) / 255.0f;
			const float gr = ((glow >> 16) & 255) / 255.0f, gg = ((glow >> 8) & 255) / 255.0f, gb = (glow & 255) / 255.0f;
			const float floorY = screen == 0 ? TOP_FLOOR : SHELF_Y, lx = screen == 0 ? 176 : 320;
			const float wallK = screen == 0 ? 0.10f : 0.07f, poolK = screen == 0 ? 0.09f : 0.08f;
			int dot = std::max(1, (int)std::lround(1.5f * s)), step = (int)std::lround(16 * s);
			// the pools of light and the glow are Gaussians, so each is a column's factor times a row's: the exponentials
			// once per column here and once per row below, not per pixel (on the main thread, a new colour's two screens)
			std::vector<float> wallX(w), poolX(w), glowX(ga > 0 ? w : 0), vigX(w);
			std::vector<uint8_t> dotX(w);
			for (int xx = 0; xx < w; xx++)
			{
				float u = (x0 + xx + 0.5f) / s;
				float dx = (u - lx) / 250;
				wallX[xx] = wallK * std::exp(-dx * dx * 2.0f);
				dx = (u - lx) / 210;
				poolX[xx] = poolK * std::exp(-dx * dx * 1.4f);
				float nx = u / 320 - 1;
				vigX[xx] = nx * nx * 0.85f;
				dotX[xx] = (x0 + xx) % step < dot;
				if (ga > 0) { float gx = (x0 + xx + 0.5f - gcx) / grx; glowX[xx] = std::exp(-gx * gx * 3.2f); }
			}
			for (int yy = 0; yy < h; yy++)
			{
				int y = y0 + yy;
				float v = (y + 0.5f) / s;
				uint32_t c = mix(top, bot, std::pow((float)y / H, 0.8f));
				float cr = ((c >> 16) & 255) / 255.0f, cg = ((c >> 8) & 255) / 255.0f, cb = (c & 255) / 255.0f;
				const bool wall = v < floorY, dots = wall && v < floorY - 6 && (y % step) < dot;
				const float dotWhite = screen == 0 ? 0.022f : 0.014f;
				float rowWhite = 0, rowK;
				const float* colK;
				if (wall)
				{
					float dy = (v - (floorY - 110)) / 190;
					rowK = std::exp(-dy * dy * 2.0f);
					colK = wallX.data();
				}
				else
				{
					float depth = (v - floorY) / (480 - floorY);
					rowWhite = 0.028f * (1 - depth) * (1 - depth);
					float dy = (v - floorY - 12) / 40;
					rowK = std::exp(-dy * dy * 1.4f);
					colK = poolX.data();
				}
				// the floor's front edge catching the light (on the top screen only under the cartridge: the words
				// to its right stand on the wall)
				float edgeD = std::fabs(v - floorY) * s;
				const bool edge = edgeD < 1.5f;
				float ny = v / 240 - 1, vy = ny * ny;
				float rowGlow = 0;
				if (ga > 0) { float gy = (y + 0.5f - gcy) / gry; rowGlow = std::exp(-gy * gy * 3.2f); }
				for (int xx = 0; xx < w; xx++)
				{
					int x = x0 + xx;
					float r = cr, g = cg, bl = cb, white = rowWhite;
					float add = colK[xx] * rowK;
					if (dots && dotX[xx]) white += dotWhite;
					if (edge)
					{
						float u = (x + 0.5f) / s;
						white += 0.05f * (1 - edgeD / 1.5f) * (screen == 0 ? 1 - smooth01(250, 360, u) : 1 - smooth01(0.35f, 1.0f, std::fabs(u - 320) / 320));
					}
					r += add * lr + white; g += add * lg + white; bl += add * lb + white;
					float q = vigX[xx] + vy;
					if (q > 0.35f * 0.35f)	// (inside that the vignette is 1)
					{
						float vig = 1 - 0.40f * smooth01(0.35f, 1.3f, std::sqrt(q));
						r *= vig; g *= vig; bl *= vig;
					}
					if (ga > 0)
					{
						float a = ga * std::max(0.0f, glowX[xx] * rowGlow - 0.04f) / 0.96f;
						r += (gr - r) * a; g += (gg - g) * a; bl += (gb - bl) * a;
					}
					float n = grain(x, y + screen * 4099) * (2.4f / 255);
					uint8_t* o = &b.px[((size_t)yy * w + xx) * 4];
					o[0] = (uint8_t)std::min(255.0f, std::max(0.0f, (r + n) * 255 + 0.5f));
					o[1] = (uint8_t)std::min(255.0f, std::max(0.0f, (g + n) * 255 + 0.5f));
					o[2] = (uint8_t)std::min(255.0f, std::max(0.0f, (bl + n) * 255 + 0.5f));
					o[3] = 255;
				}
			}
			return b;
		}

		// the colour a picture throws on the wall: its colourful pixels' average, lifted to a bright tone (0: none)
		uint32_t glowOf(const rnds::Bitmap& b)
		{
			if (b.empty() || b.alpha) return 0;
			double r = 0, g = 0, bl = 0, wsum = 0;
			int step = std::max(1, (int)std::sqrt((double)b.w * b.h / 4096));
			for (int y = 0; y < b.h; y += step)
				for (int x = 0; x < b.w; x += step)
				{
					const uint8_t* p = &b.px[((size_t)y * b.w + x) * 4];
					int mx = std::max({ p[0], p[1], p[2] }), mn = std::min({ p[0], p[1], p[2] });
					double sat = mx ? (double)(mx - mn) / mx : 0, w = 0.02 + sat * sat * (mx / 255.0);
					r += p[0] * w; g += p[1] * w; bl += p[2] * w; wsum += w;
				}
			if (wsum <= 0) return 0;
			r /= wsum; g /= wsum; bl /= wsum;
			double mx = std::max({ r, g, bl, 1.0 }), k = 220 / mx;
			return (uint32_t)std::min(255.0, r * k) << 16 | (uint32_t)std::min(255.0, g * k) << 8 | (uint32_t)std::min(255.0, bl * k);
		}
	}

	CartMenu::CartMenu(rnds::Source* src_, rnds::Backend* be_, const std::string& fontDir, float scale)
		: src(src_), be(be_), s(scale)
	{
		fReg = rnds::Font::load(fontDir + "/Inter-Regular.ttf");
		fMed = rnds::Font::load(fontDir + "/Inter-Medium.ttf");
		fSemi = rnds::Font::load(fontDir + "/Inter-SemiBold.ttf");
		fBold = rnds::Font::load(fontDir + "/Inter-Bold.ttf");
		if (!fMed) fMed = fReg;
		if (!fSemi) fSemi = fMed;
		if (!fBold) fBold = fSemi;
		rebuild();
	}

	// ---- what is shown ----------------------------------------------------------------------------------------------
	void CartMenu::rebuild()
	{
		items.clear();
		if (mView == rnds::UI::HOME)
		{
			for (int i = 0; i < src->systemCount(); i++)
			{
				rnds::SysInfo si = src->system(i);
				Item it;
				it.system = modelOf(si.id);
				it.art = si.resumeBox;
				it.icon = si.icon;
				it.band = si.name;
				it.title = si.name;
				it.eyebrow = si.maker;
				it.sub = si.app ? "App" : std::to_string(si.games) + (si.games == 1 ? " game" : " games");
				it.stat[0] = std::to_string(si.games); it.statLabel[0] = si.games == 1 ? "Game" : "Games";
				it.stat[1] = std::to_string(si.played); it.statLabel[1] = "Played";
				it.stat[2] = si.time.empty() ? "0m" : si.time; it.statLabel[2] = "Time";
				if (si.hasResume) it.sub = "Last played: " + si.resumeTitle;
				it.fav = si.fav; it.app = si.app;
				it.accent = rgbOf(si.accent);
				items.push_back(it);
			}
			mSys = std::min(std::max(0, mSys), std::max(0, count() - 1));
			return;
		}
		rnds::SysInfo si = src->system(mSys);
		for (int i = 0; i < src->gameCount(); i++)
		{
			rnds::GameInfo g = src->game(i);
			Item it;
			it.system = modelOf(g.icon.empty() ? si.id : g.icon);
			if (it.system == "other") it.system = modelOf(si.id);
			it.art = !g.box.empty() ? g.box : !g.label.empty() ? g.label : g.cart;
			it.icon = g.icon.empty() ? si.icon : g.icon;
			it.band = si.name;
			it.title = g.title;
			it.sub = g.sub;
			it.eyebrow = si.name;
			it.stat[0] = g.plays; it.statLabel[0] = g.plays == "1" ? "Play" : "Plays";
			it.stat[1] = g.last; it.statLabel[1] = "Last played";
			it.stat[2] = g.time; it.statLabel[2] = "Time";
			it.achA = g.achA; it.achB = g.achB;
			it.fav = g.fav;
			it.accent = rgbOf(si.accent);
			items.push_back(it);
		}
		mGame = std::min(std::max(0, mGame), std::max(0, count() - 1));
	}

	void CartMenu::setHome(int sys)
	{
		mView = rnds::UI::HOME;
		mSys = sys;
		rebuild();
		pos = (float)mSys;
		heroAt = now;
		launchAt = -1;
	}

	void CartMenu::setLibrary(int sys, int game)
	{
		mSys = sys;
		mView = rnds::UI::LIBRARY;
		mGame = game;
		rebuild();
		pos = (float)mGame;
		heroAt = now;
		launchAt = -1;
	}

	void CartMenu::dataChanged()
	{
		rebuild();
		texts.clear();
	}

	void CartMenu::move(int to)
	{
		if (count() == 0) return;
		to = std::min(std::max(0, to), count() - 1);
		if (to == cursor()) return;
		heroDir = to > cursor() ? 1 : -1;
		if (mView == rnds::UI::HOME) { mSys = to; src->selectSystem(to); }
		else { mGame = to; src->selectGame(to); }
		movedAt = now;
		heroAt = now;
	}

	// ---- input -----------------------------------------------------------------------------------------------------
	bool CartMenu::press(Button b)
	{
		if (launchAt >= 0 || count() == 0) return false;
		int c = cursor();
		auto startLaunch = [&](int game)
		{
			launchGame = game;
			launchAt = now;
			mView = rnds::UI::LAUNCH;
		};
		if (mView == rnds::UI::HOME)
		{
			switch (b)
			{
			case rnds::UI::LEFT: case rnds::UI::UP: case rnds::UI::L: move(c - 1); return true;
			case rnds::UI::RIGHT: case rnds::UI::DOWN: case rnds::UI::R: move(c + 1); return true;
			case rnds::UI::A:
				if (items[c].app) { int g = src->openLibrary(c); mGame = g; startLaunch(g); return true; }
				mGame = src->openLibrary(c);
				mView = rnds::UI::LIBRARY;
				rebuild();
				pos = (float)mGame;
				heroAt = now; heroDir = 1;
				return true;
			case rnds::UI::X:
			{
				int g = src->resumeGame(c);
				if (g < 0) return false;
				mView = rnds::UI::LIBRARY;
				mGame = g;
				rebuild();
				pos = (float)g;
				startLaunch(g);
				return true;
			}
			case rnds::UI::Y: src->toggleSystemFav(c); rebuild(); return true;
			default: return false;
			}
		}
		switch (b)
		{
		case rnds::UI::LEFT: move(c - 1); return true;
		case rnds::UI::RIGHT: move(c + 1); return true;
		case rnds::UI::UP: move(c - 10); return true;
		case rnds::UI::DOWN: move(c + 10); return true;
		case rnds::UI::L: case rnds::UI::R:
		{
			// the next system with games (an app's tile isn't a list)
			int n = src->systemCount(), d = b == rnds::UI::R ? 1 : -1, to = mSys;
			for (int k = 0; k < n; k++)
			{
				to = (to + d + n) % n;
				if (!src->system(to).app) break;
			}
			if (to == mSys) return false;
			src->backToHome();
			mSys = to;
			src->selectSystem(to);
			mGame = src->openLibrary(to);
			rebuild();
			pos = (float)mGame;
			heroAt = now; heroDir = d;
			return true;
		}
		case rnds::UI::A: startLaunch(c); return true;
		case rnds::UI::B:
			src->backToHome();
			mView = rnds::UI::HOME;
			rebuild();
			pos = (float)mSys;
			heroAt = now; heroDir = -1;
			return true;
		case rnds::UI::Y: src->toggleGameFav(c); rebuild(); return true;
		default: return false;
		}
	}

	// the shelf's spacing: wide cartridges stand further apart
	static float spacingOf(const std::string& system) { return std::max(96.0f, ROW_H * cartAspect(system) * 0.9f + 18); }

	bool CartMenu::touch(int screen, float x, float y, bool down)
	{
		if (screen != 1 || launchAt >= 0 || count() == 0) return false;
		float lx = x / s, ly = y / s;
		if (down)
		{
			mDown = true; dragged = false;
			downX = lx; downY = ly; downPos = pos;
			return false;
		}
		if (!mDown) return false;
		mDown = false;
		if (dragged) { move((int)std::lround(pos)); return true; }
		if (ly > SHELF_Y + 20 || ly < 70) return false;
		int hit = -1;
		float best = 1e9f;
		for (int i = std::max(0, cursor() - 6); i <= std::min(count() - 1, cursor() + 6); i++)
		{
			float d = std::fabs(shelfX(i) - lx);
			float half = (i == cursor() ? PICK_H : ROW_H) * cartAspect(items[i].system) * 0.55f;
			if (d < half && d < best) { best = d; hit = i; }
		}
		if (hit < 0) return false;
		if (hit == cursor()) return press(rnds::UI::A);
		move(hit);
		return true;
	}

	bool CartMenu::touchMove(int screen, float x, float y)
	{
		(void)y;
		if (screen != 1 || !mDown) return false;
		float lx = x / s;
		if (!dragged && std::fabs(lx - downX) < 12) return false;
		dragged = true;
		float sp = spacingOf(items[cursor()].system);
		pos = std::min(std::max(0.0f, downPos - (lx - downX) / sp), (float)(count() - 1));
		int c = (int)std::lround(pos);
		if (c != cursor()) { if (mView == rnds::UI::HOME) { mSys = c; src->selectSystem(c); } else { mGame = c; src->selectGame(c); } heroAt = now; }
		return true;
	}

	// where each cartridge stands on the bottom screen (layout px): side by side by their own widths (a shelf of mixed
	// systems), the chosen one further from its neighbours (it is the larger one), scrolled to the eased position
	float CartMenu::shelfX(int i) const
	{
		if (count() == 0) return 320;
		auto widthOf = [&](int k) { return ROW_H * cartAspect(items[k].system) * 1.08f; };
		const float GAP = 16;
		const int c = cursor();
		auto centre = [&](int j) -> float	// the cursor's at 0
		{
			float x = 0;
			if (j > c) for (int k = c; k < j; k++) x += widthOf(k) / 2 + GAP + widthOf(k + 1) / 2;
			else for (int k = c; k > j; k--) x -= widthOf(k) / 2 + GAP + widthOf(k - 1) / 2;
			if (j != c) x += (j > c ? 1 : -1) * (PICK_H - ROW_H) * cartAspect(items[c].system) * 0.55f;
			return x;
		};
		int pf = std::min(std::max(0, (int)std::floor(pos)), count() - 1), pc = std::min(pf + 1, count() - 1);
		float shift = centre(pf) + (centre(pc) - centre(pf)) * (pos - pf);
		return 320 + centre(i) - shift;
	}

	// ---- pictures: the worker renders, the main thread uploads -------------------------------------------------------
	const CartMenu::CartTex* CartMenu::cart(const Item& it, int height, float yaw, float pitch)
	{
		std::string key = it.system + "|" + it.art + "|" + it.icon + "|" + it.band + "|" + std::to_string(height) + "|" + std::to_string((int)yaw);
		auto f = carts.find(key);
		if (f != carts.end())
		{
			auto o = std::find(cartOrder.begin(), cartOrder.end(), key);
			if (o != cartOrder.end()) { cartOrder.erase(o); cartOrder.push_back(key); }
			return &f->second;
		}
		{
			std::lock_guard<std::mutex> l(jm);
			if (!queued.count(key))
			{
				queued.insert(key);
				// the band's text: the system's name in capitals, large (the card scales it to its band)
				auto b = bands.find(it.band);
				if (b == bands.end())
				{
					rnds::Bitmap bm;
					if (!it.band.empty() && fSemi)
					{
						rnds::TextStyle st;
						st.font = fSemi; st.size = 40; st.letterSpacing = 3; st.upper = true;
						rnds::TextLine l = rnds::layoutLine(rnds::utf8to32(it.band), st);
						bm = rnds::renderLine(l, st, 1, fSemi->ascent(40)).bmp;
					}
					b = bands.emplace(it.band, bm).first;
				}
				Job jb;
				jb.key = key; jb.system = it.system; jb.art = it.art; jb.icon = it.icon; jb.band = b->second;
				jb.height = height; jb.yaw = yaw; jb.pitch = pitch;
				jobs.push_back(jb);
				if (mQueued) mQueued();
			}
		}
		return nullptr;
	}

	void CartMenu::drawCart(const CartTex& c, float cx, float floorY, float lift, float tint, float alpha, bool reflect)
	{
		if (!c.tex) return;
		float H = (float)c.tex->h, W = (float)c.tex->w;
		float x0 = (float)std::lround(X(cx) - W / 2), y0 = (float)std::lround(X(floorY - lift) - H * (1 - CART_MARGIN));
		// the reflection: under the floor, as far below it as the cartridge is above
		if (reflect && c.refl)
			be->draw(c.refl, x0, y0 + c.reflTop + std::lround(X(2 * lift)), (float)c.refl->w, (float)c.refl->h, rnds::Color(tint, tint, tint, alpha));
		be->draw(c.tex, x0, y0, W, H, rnds::Color(tint, tint, tint, alpha));
	}

	bool CartMenu::workOnce()
	{
		Job j;
		{
			std::lock_guard<std::mutex> l(jm);
			if (jobs.empty()) return false;
			j = jobs.back();	// the newest first: what the cursor is on now
			jobs.pop_back();
		}
		if (j.back >= 0)
		{
			// a screen's background in a colour not seen yet (tens of milliseconds: not on the main thread)
			CartBmp cb;
			int H = (int)std::lround(480 * s), W = (int)std::lround(640 * s);
			cb.card = backgroundRect(j.back, j.accent, s, 0, 0, W, H, H);
			{
				std::lock_guard<std::mutex> l(jm);
				doneBmp.push_back({ j.key, std::move(cb) });
			}
			if (mFinished) mFinished();
			return true;
		}
		if (j.wall)
		{
			// a piece of the top screen's wall with the chosen art's light baked in (drawn as a plain copy)
			CartBmp cb;
			int H = (int)std::lround(480 * s), W = (int)std::lround(640 * s);
			float gcx = 176 * s, gcy = (TOP_FLOOR - j.heroH * 0.52f) * s, grx = 230 * s, gry = 190 * s;
			int x0 = std::max(0, (int)std::floor(gcx - grx)), y0 = std::max(0, (int)std::floor(gcy - gry));
			int x1 = std::min(W, (int)std::ceil(gcx + grx)), y1 = std::min(H, (int)std::ceil(gcy + gry));
			cb.card = backgroundRect(0, j.accent, s, x0, y0, x1 - x0, y1 - y0, H, j.glow, gcx, gcy, grx, gry, 0.17f);
			cb.reflTop = x0; cb.glow = (uint32_t)y0;	// (where it goes)
			{
				std::lock_guard<std::mutex> l(jm);
				doneBmp.push_back({ j.key, std::move(cb) });
			}
			if (mFinished) mFinished();
			return true;
		}
		rnds::Bitmap art = j.art.empty() ? rnds::Bitmap() : rnds::decodeFile(j.art);
		// a handheld's box art: its side band (the console's name down the left edge) isn't on the cartridge's label
		if (!art.empty() && !art.alpha && j.art.find("/media/box/") != std::string::npos &&
			(j.system == "gba" || j.system == "nds" || j.system == "gb" || j.system == "gbc"))
		{
			int cut = art.w * 14 / 100;
			rnds::Bitmap c;
			c.w = art.w - cut; c.h = art.h;
			c.px.resize((size_t)c.w * c.h * 4);
			for (int y = 0; y < c.h; y++) std::copy(&art.px[((size_t)y * art.w + cut) * 4], &art.px[((size_t)y * art.w + art.w) * 4], &c.px[(size_t)y * c.w * 4]);
			art = c;
		}
		rnds::Bitmap icon = j.icon.empty() ? rnds::Bitmap() : rnds::decodeFile(j.icon);
		CartPose p;
		p.height = j.height; p.yaw = j.yaw; p.pitch = j.pitch;
		CartBmp cb;
		cb.card = renderCart(j.system, art, p, icon, j.band);
		// (the shelf's row: its shadows baked into its reflection; the lifted one's and the top screen's move, drawn apart)
		float unit = j.height / (float)ROW_H;
		cb.refl = reflectionOf(cb.card, 0.2f, cb.reflTop, j.height == (int)std::lround(X(ROW_H)) ? ROW_H * cartAspect(j.system) * 0.95f * unit : 0, unit);
		cb.glow = glowOf(art);
		{
			std::lock_guard<std::mutex> l(jm);
			doneBmp.push_back({ j.key, std::move(cb) });
		}
		if (mFinished) mFinished();
		return true;
	}

	void CartMenu::update(double t)
	{
		double dt = last > 0 ? std::min(50.0, t - last) : 16;
		last = t;
		now = t;
		{
			std::lock_guard<std::mutex> l(jm);
			for (auto& d : doneBmp)
			{
				queued.erase(d.first);
				if (d.first.compare(0, 5, "back|") == 0)
				{
					if (backs.size() > 12) backs.clear();
					backs[d.first.substr(5)] = be->upload(d.second.card, false);
					continue;
				}
				if (d.first.compare(0, 5, "wall|") == 0)
				{
					Wall w;
					w.tex = be->upload(d.second.card, false);
					w.x = d.second.reflTop; w.y = (int)d.second.glow;
					walls[d.first] = w;
					wallOrder.push_back(d.first);
					while (walls.size() > 6 && !wallOrder.empty()) { walls.erase(wallOrder.front()); wallOrder.pop_front(); }
					continue;
				}
				CartTex ct;
				ct.tex = be->upload(d.second.card, true);
				if (!d.second.refl.empty()) ct.refl = be->upload(d.second.refl, true);
				ct.reflTop = d.second.reflTop;
				ct.glow = d.second.glow;
				carts[d.first] = ct;
				cartOrder.push_back(d.first);
			}
			doneBmp.clear();
			// only the newest wishes wait: the cursor ran past the rest
			while (jobs.size() > 24) { queued.erase(jobs.front().key); jobs.pop_front(); }
		}
		while (carts.size() > 64 && !cartOrder.empty()) { carts.erase(cartOrder.front()); cartOrder.pop_front(); }
		if (!mDown)
		{
			float k = 1 - (float)std::exp(-dt / 70.0);
			float to = (float)cursor();
			pos += (to - pos) * k;
			if (std::fabs(to - pos) < 0.002f) pos = to;
		}
		if (launchAt >= 0 && t - launchAt >= 900)
		{
			launchAt = -1;
			src->launch(launchGame);	// the main loop starts it (the library's pending launch)
		}
		if (launchAt >= 0 && t - launchAt >= 560 && !cued) { cued = true; src->cue("insert"); }	// the click into the slot
		if (launchAt < 0) cued = false;
	}

	bool CartMenu::animating() const
	{
		return std::fabs(pos - (float)cursor()) > 0.002f || now - movedAt < 700 || now - heroAt < 320 || launchAt >= 0 || mDown;
	}

	int CartMenu::nextWake() const
	{
		if (animating()) return 0;
		std::lock_guard<std::mutex> l(const_cast<std::mutex&>(jm));
		return doneBmp.empty() ? -1 : 0;
	}

	// ---- drawing helpers ---------------------------------------------------------------------------------------------
	float CartMenu::text(const std::string& t, const std::shared_ptr<rnds::Font>& f, float size, float x, float y, uint32_t rgb,
		float alpha, float maxW, int align, float spacing, bool upper, bool measure)
	{
		if (t.empty() || !f) return 0;
		std::string key = std::to_string((uintptr_t)f.get()) + "|" + std::to_string(size) + "|" + std::to_string(maxW) + "|" +
			std::to_string(spacing) + (upper ? "U" : "") + "|" + t;
		auto it = texts.find(key);
		if (it == texts.end())
		{
			rnds::TextStyle st;
			st.font = f; st.size = size; st.letterSpacing = spacing; st.upper = upper;
			std::u32string u = rnds::utf8to32(t);
			rnds::TextLine l = maxW > 0 ? rnds::layoutEllipsis(u, st, (int)maxW) : rnds::layoutLine(u, st);
			float asc = f->ascent(size);
			rnds::TextBitmap tb = rnds::renderLine(l, st, s, asc);
			TextTex tt;
			if (!tb.bmp.empty()) tt.tex = be->upload(tb.bmp, true);
			tt.padX = tb.padX; tt.padY = tb.padY; tt.w = l.width;
			if (texts.size() > 600) texts.clear();
			it = texts.emplace(key, tt).first;
		}
		const TextTex& tt = it->second;
		if (!measure && tt.tex)
		{
			float x0 = align == 1 ? x - tt.w / 2 : align == 2 ? x - tt.w : x;
			be->draw(tt.tex, std::floor(X(x0)) - tt.padX, std::floor(X(y)) - tt.padY, (float)tt.tex->w, (float)tt.tex->h, rnds::Color::hex(rgb, alpha));
		}
		return tt.w;
	}

	// a rounded rectangle: its coverage made once per size and radius, drawn tinted
	void CartMenu::round(float x, float y, float w, float h, float r, uint32_t rgb, float alpha)
	{
		int W = std::max(1, (int)std::lround(X(w))), H = std::max(1, (int)std::lround(X(h)));
		float R = std::min(X(r), std::min(W, H) / 2.0f);
		std::string key = std::to_string(W) + "x" + std::to_string(H) + "r" + std::to_string((int)(R * 4));
		auto it = shapes.find(key);
		if (it == shapes.end())
		{
			rnds::Bitmap b;
			b.w = W; b.h = H; b.alpha = true;
			b.px.assign((size_t)W * H, 0);
			for (int yy = 0; yy < H; yy++)
				for (int xx = 0; xx < W; xx++)
				{
					float dx = std::max({ R - (xx + 0.5f), 0.0f, (xx + 0.5f) - (W - R) }), dy = std::max({ R - (yy + 0.5f), 0.0f, (yy + 0.5f) - (H - R) });
					float d = std::sqrt(dx * dx + dy * dy) - R;
					b.px[(size_t)yy * W + xx] = (uint8_t)(std::min(1.0f, std::max(0.0f, 0.5f - d)) * 255);
				}
			it = shapes.emplace(key, be->upload(b, false)).first;
		}
		be->draw(it->second, std::lround(X(x)), std::lround(X(y)), (float)W, (float)H, rnds::Color::hex(rgb, alpha));
	}

	// a soft shadow under a cartridge: a blurred ellipse
	void CartMenu::shadow(float cx, float y, float w, float alpha)
	{
		int W = std::max(2, (int)std::lround(X(w))), H = std::max(2, (int)std::lround(X(w * 0.16f)));
		std::string key = "shadow" + std::to_string(W) + "x" + std::to_string(H);
		auto it = shapes.find(key);
		if (it == shapes.end())
		{
			rnds::Bitmap b;
			b.w = W; b.h = H; b.alpha = true;
			b.px.assign((size_t)W * H, 0);
			for (int yy = 0; yy < H; yy++)
				for (int xx = 0; xx < W; xx++)
				{
					float dx = (xx + 0.5f) / W * 2 - 1, dy = (yy + 0.5f) / H * 2 - 1, d = dx * dx + dy * dy;
					b.px[(size_t)yy * W + xx] = (uint8_t)(std::max(0.0f, 1 - d) * std::max(0.0f, 1 - d) * 255);
				}
			it = shapes.emplace(key, be->upload(b, false)).first;
		}
		be->draw(it->second, std::lround(X(cx) - W / 2.0f), std::lround(X(y) - H / 2.0f), (float)W, (float)H, rnds::Color::hex(0, alpha));
	}

	bool CartMenu::background(int screen, uint32_t accent)
	{
		int W = (int)std::lround(X(640)), H = (int)std::lround(X(480));
		std::string key = std::to_string(screen) + ":" + std::to_string(accent);
		auto it = backs.find(key);
		if (it == backs.end())
		{
			// the first one is made here; a new colour's on the worker, the last one shown until it is ready
			if (lastBack[screen])
			{
				std::lock_guard<std::mutex> l(jm);
				std::string bk = "back|" + key;
				if (!queued.count(bk))
				{
					queued.insert(bk);
					Job jb;
					jb.key = bk; jb.back = screen; jb.accent = accent;
					jobs.push_back(jb);
					if (mQueued) mQueued();
				}
			}
			else
			{
				if (backs.size() > 12) backs.clear();
				it = backs.emplace(key, be->upload(backgroundRect(screen, accent, s, 0, 0, W, H, H), false)).first;
			}
		}
		const rnds::TexPtr& t = it != backs.end() ? it->second : lastBack[screen];
		be->draw(t, 0, 0, (float)W, (float)H, rnds::Color(1, 1, 1, 1));
		lastBack[screen] = t;
		return it != backs.end();
	}

	// ---- the screens --------------------------------------------------------------------------------------------------
	void CartMenu::render()
	{
		drawTop();
		drawBottom();
	}

	void CartMenu::drawTop()
	{
		be->beginScreen(0);
		if (count() == 0)
		{
			background(0, 0x4a8af0);
			text("No games yet", fBold, 30, 320, 200, INK, 1, 0, 1);
			text(src->emptyHint(), fReg, 16, 320, 244, SOFT, 1, 560, 1);
			be->endScreen();
			return;
		}
		const Item& it = items[cursor()];
		bool wallReady = background(0, it.accent);
		// the status line: date, time, battery
		text(src->dateText(), fMed, 14, 22, 16, SOFT);
		std::string right = src->clockText();
		float bat = src->battery();
		if (bat >= 0) right += "   " + std::to_string((int)std::lround(bat * 100)) + "%";
		text(right, fMed, 14, 618, 16, SOFT, 1, 0, 2);
		// the cartridge, sliding in from the side it came from; it drops away when the game starts
		float e = ease((float)((now - heroAt) / 280.0));
		float dx = (1 - e) * 36 * heroDir, dy = 0, alpha = 0.35f + 0.65f * e;
		if (mView == rnds::UI::LAUNCH && launchAt >= 0)
		{
			float t = (float)((now - launchAt) / 560.0);
			dy = t * t * 520;
		}
		float cx = 176 + dx;
		// as tall as HERO_H, or narrower for a wide cartridge (an N64's, a SNES's): it stays left of the words
		float heroH = std::min(HERO_H, 285 / (cartAspect(it.system) * 1.12f));
		const CartTex* hero = cart(it, (int)std::lround(X(heroH)), -28, 9);
		if (!hero) hero = cart(it, (int)std::lround(X(PICK_H)), -12, 6);	// (the shelf's while the large one is made)
		// the light its art throws on the wall behind it: a piece of the wall made with it (once the cartridge is, as
		// its colour comes from the art; not over the last colour's wall)
		if (hero && wallReady)
		{
			uint32_t glow = hero->glow ? hero->glow : mix(it.accent, 0xffffff, 0.3f);
			std::string wk = "wall|" + std::to_string(it.accent) + "|" + std::to_string(glow) + "|" + std::to_string((int)std::lround(heroH * 10));
			auto w = walls.find(wk);
			if (w != walls.end())
			{
				auto o = std::find(wallOrder.begin(), wallOrder.end(), wk);
				if (o != wallOrder.end()) { wallOrder.erase(o); wallOrder.push_back(wk); }
				be->draw(w->second.tex, (float)w->second.x, (float)w->second.y, (float)w->second.tex->w, (float)w->second.tex->h, rnds::Color(1, 1, 1, 1));
			}
			else
			{
				std::lock_guard<std::mutex> l(jm);
				if (!queued.count(wk))
				{
					queued.insert(wk);
					Job jb;
					jb.key = wk; jb.wall = true; jb.accent = it.accent; jb.glow = glow; jb.heroH = heroH;
					jobs.push_back(jb);
					if (mQueued) mQueued();
				}
			}
		}
		float footW = std::min(300.0f, heroH * cartAspect(it.system) * 1.1f);
		bool falling = mView == rnds::UI::LAUNCH && launchAt >= 0;
		shadow(cx, TOP_FLOOR + 8 + dy * 0.2f, footW, 0.40f * alpha * (falling ? std::max(0.0f, 1 - dy / 120) : 1));
		shadow(cx, TOP_FLOOR + 2, footW * 0.78f, 0.55f * alpha * (falling ? std::max(0.0f, 1 - dy / 40) : 1));	// (where it touches)
		if (hero) drawCart(*hero, cx, TOP_FLOOR + dy, 0, 1, alpha, !falling);
		// the words, right of it
		float x = 336, w = 282, y = 98;
		text(it.eyebrow, fSemi, 13, x, y, mix(it.accent, 0xffffff, 0.45f), 1, w, 0, 1.4f, true);
		y += 26;
		if (fBold)	// (no fonts: no words, rather than a crash)
		{
			rnds::TextStyle st;
			st.font = fBold; st.size = 30;
			std::vector<rnds::TextLine> lines = rnds::layoutWrap(rnds::utf8to32(it.title), st, (int)w, 3);
			for (auto& l : lines)
			{
				std::string ln;
				for (char32_t c : l.text)
				{
					if (c < 0x80) ln += (char)c;
					else if (c < 0x800) { ln += (char)(0xc0 | (c >> 6)); ln += (char)(0x80 | (c & 63)); }
					else if (c < 0x10000) { ln += (char)(0xe0 | (c >> 12)); ln += (char)(0x80 | ((c >> 6) & 63)); ln += (char)(0x80 | (c & 63)); }
					else { ln += (char)(0xf0 | (c >> 18)); ln += (char)(0x80 | ((c >> 12) & 63)); ln += (char)(0x80 | ((c >> 6) & 63)); ln += (char)(0x80 | (c & 63)); }
				}
				text(ln, fBold, 30, x, y + 2, 0x000000, 0.45f);
				text(ln, fBold, 30, x, y, INK);
				y += 36;
			}
		}
		y += 4;
		text(it.sub, fReg, 16, x, y, SOFT, 1, w);
		y += 36;
		round(x, y, w, 1, 0, 0xffffff, 0.12f);
		y += 18;
		for (int i = 0; i < 3; i++)
		{
			float cxs = x + i * (w / 3);
			text(it.stat[i], fSemi, 19, cxs, y, INK, 1, w / 3 - 8);
			text(it.statLabel[i], fMed, 11, cxs, y + 27, FAINT, 1, w / 3 - 8, 0, 0.8f, true);
		}
		y += 60;
		if (it.achB > 0)
		{
			text(std::to_string(it.achA) + " / " + std::to_string(it.achB) + " achievements", fMed, 13, x, y, SOFT, 1, w);
			round(x, y + 24, w, 6, 3, 0xffffff, 0.12f);
			float fw = std::max(6.0f, w * it.achA / it.achB);
			if (it.achA > 0) round(x, y + 24, fw, 6, 3, mix(it.accent, 0xffffff, 0.25f), 1);
		}
		be->endScreen();
	}

	void CartMenu::drawBottom()
	{
		be->beginScreen(1);
		uint32_t accent = count() ? items[cursor()].accent : 0x4a8af0;
		background(1, accent);
		// the header: where this is, and how far along
		bool home = mView == rnds::UI::HOME;
		if (home) text("Library", fSemi, 20, 24, 20, INK);
		else
		{
			round(22, 20, 26, 22, 6, PANEL, 1);
			text("L", fSemi, 13, 35, 24, SOFT, 1, 0, 1);
			float nw = text(src->system(mSys).name, fSemi, 20, 58, 20, INK, 1, 380);
			round(66 + nw, 20, 26, 22, 6, PANEL, 1);
			text("R", fSemi, 13, 79 + nw, 24, SOFT, 1, 0, 1);
		}
		if (count())
		{
			char n[32];
			snprintf(n, sizeof n, "%d / %d", cursor() + 1, count());
			text(n, fMed, 14, 616, 24, SOFT, 1, 0, 2);
		}
		round(24, 56, 592, 1, 0, 0xffffff, 0.08f);
		if (count() == 0)
		{
			text(src->emptyTitle(), fSemi, 20, 320, 200, INK, 1, 560, 1);
			be->endScreen();
			return;
		}
		// the shelf: far cartridges first, the chosen one last, lifted out with a little spring
		const Item& sel = items[cursor()];

		bool launching = mView == rnds::UI::LAUNCH && launchAt >= 0;
		float lt = launching ? (float)((now - launchAt) / 1000.0) : 0;
		int first = std::max(0, (int)std::floor(pos) - 6), lastI = std::min(count() - 1, (int)std::ceil(pos) + 6);
		std::vector<int> order;
		for (int i = first; i <= lastI; i++) order.push_back(i);
		std::sort(order.begin(), order.end(), [&](int a, int b) { return std::fabs(a - pos) > std::fabs(b - pos); });
		for (int i : order)
		{
			float d = i - pos;
			bool chosen = i == cursor();
			const Item& it = items[i];
			float x = shelfX(i);
			if (x < -200 || x > 840) continue;
			float h = chosen ? PICK_H : ROW_H;
			float lift = 0;
			if (chosen)
			{
				float t = (float)((now - movedAt) / 1000.0);
				lift = 14 + (t < 0.7f ? std::exp(-t * 9) * std::sin(t * 34) * 7 : 0);
			}
			float fade = launching && !chosen ? std::max(0.0f, 1 - lt * 3) : 1;
			float a = (chosen ? 1.0f : std::max(0.25f, 0.9f - 0.16f * std::fabs(d))) * fade;
			if (launching && chosen) continue;	// (drawn below, going into the slot)
			// the lifted one's shadow (the row's are baked into their reflections)
			if (chosen) shadow(x, SHELF_Y + 5 + lift * 0.3f, h * cartAspect(it.system) * 0.95f * (1 + lift / 120), 0.45f * fade);
			const CartTex* tex = cart(it, (int)std::lround(X(h)), -12, 6);
			if (tex) drawCart(*tex, x, SHELF_Y, lift, a, fade, true);
			else
			{
				float w = h * cartAspect(it.system);
				round(x - w / 2, SHELF_Y - lift - h, w, h, 8, mix(it.accent, 0x2a2c31, 0.7f), a);
			}
			if (it.fav) round(x - 4, SHELF_Y - lift - h - 14, 8, 8, 4, 0xffd24a, a);
		}
		if (launching)
		{
			// the slot, and the cartridge going into it
			float slotY = 250;
			round(170, slotY - 6, 300, 30, 12, 0x08090b, 1);
			round(190, slotY + 4, 260, 8, 4, 0x000000, 1);
			float t = std::min(1.0f, lt / 0.62f);
			float e2 = t * t * (3 - 2 * t);
			float h = PICK_H;
			float bottom = -10 + e2 * (slotY + 8 + h * 0.62f);
			const CartTex* tex = cart(sel, (int)std::lround(X(h)), -12, 6);
			be->pushClip(0, 0, X(640), X(slotY + 8));
			if (tex) drawCart(*tex, 320, bottom, 0, 1, 1, false);
			be->popClip();
			text(sel.title, fSemi, 19, 320, 326, INK, 1, 560, 1);
			text("Starting", fReg, 14, 320, 352, SOFT, 1, 0, 1);
			be->endScreen();
			return;
		}
		// the chosen one's name under the shelf
		text(sel.title, fSemi, 19, 320, 350, 0x000000, 0.5f, 580, 1);
		text(sel.title, fSemi, 19, 320, 348, INK, 1, 580, 1);
		text(sel.sub, fReg, 14, 320, 374, SOFT, 1, 580, 1);
		// the buttons, as chips
		struct Chip { const char* key; std::string label; };
		std::vector<Chip> chips;
		if (home) chips = { { "A", sel.app ? "Start" : "Open" }, { "X", "Resume" }, { "Y", "Favourite" } };
		else chips = { { "A", "Play" }, { "B", "Back" }, { "Y", sel.fav ? "Unfavourite" : "Favourite" }, { "L R", "Systems" } };
		float total = 0;
		std::vector<float> widths;
		for (auto& c : chips)
		{
			float kw = std::max(22.0f, text(c.key, fSemi, 12, 0, 0, INK, 1, 0, 0, 0, false, true) + 12);
			float w = kw + 8 + text(c.label, fMed, 13, 0, 0, INK, 1, 0, 0, 0, false, true) + 14;
			widths.push_back(w);
			total += w + 10;
		}
		float cxp = 320 - (total - 10) / 2;
		for (size_t i = 0; i < chips.size(); i++)
		{
			// raised a little: a shadow under it, light on its top edge
			round(cxp, 428, widths[i], 32, 16, 0x000000, 0.35f);
			round(cxp, 425, widths[i], 32, 16, 0xffffff, 0.09f);
			round(cxp, 426, widths[i], 32, 16, PANEL, 1);
			float kw = std::max(22.0f, text(chips[i].key, fSemi, 12, 0, 0, INK, 1, 0, 0, 0, false, true) + 12);
			round(cxp + 5, 431, kw, 22, 11, mix(accent, 0xffffff, 0.15f), 1);
			text(chips[i].key, fSemi, 12, cxp + 5 + kw / 2, 435, 0x111214, 1, 0, 1);
			text(chips[i].label, fMed, 13, cxp + 5 + kw + 8, 434, INK);
			cxp += widths[i] + 10;
		}
		be->endScreen();
	}
}
