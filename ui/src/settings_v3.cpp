#include "settings.h"
#include "settings_draw.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// banana-ui: Settings in a theme's "material" look ("panels": "material"; Thor OLED's): every surface one dark gradient
// with a lit line along its top edge and a hairline inside it, the selection on the accent's plate (tiles) or in an
// accent ring (rows), the button hints as pills with keys shaped like the handheld's buttons, labels in Inter. The
// pages draw as they always do, in the canvas's units; the look is in the shared pieces (box, text, face), and with
// "settings.layout": "screen" the pages' place on each screen is the stage's (settings.h: StageShift). Measures here are
// the design's pixels on a 1080-line screen (px()).
namespace banana
{
	using rnds::Color;

	// ---- colours ------------------------------------------------------------------------------------------------
	// color-mix(in oklab, a p, b): the design's plate is the accent mixed with white and with black this way
	static void toOklab(const Color& c, float o[3])
	{
		auto lin = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
		float r = lin(c.r), g = lin(c.g), b = lin(c.b);
		float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
		float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
		float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
		o[0] = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
		o[1] = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
		o[2] = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
	}
	static Color fromOklab(const float o[3])
	{
		float l = o[0] + 0.3963377774f * o[1] + 0.2158037573f * o[2];
		float m = o[0] - 0.1055613458f * o[1] - 0.0638541728f * o[2];
		float s = o[0] - 0.0894841775f * o[1] - 1.2914855480f * o[2];
		l = l * l * l; m = m * m * m; s = s * s * s;
		auto gam = [](float v) { v = std::max(0.0f, std::min(1.0f, v)); return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1 / 2.4f) - 0.055f; };
		return Color(gam(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s),
			gam(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s),
			gam(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s));
	}
	static Color oklabMix(const Color& a, float p, const Color& b)
	{
		float x[3], y[3], o[3];
		toOklab(a, x); toOklab(b, y);
		for (int i = 0; i < 3; i++) o[i] = x[i] * p + y[i] * (1 - p);
		return fromOklab(o);
	}

	// the look's colours: a theme's own by these names ("colors"), else Thor OLED's. The plate's are the accent's
	Color Settings::mcol(const char* name) const
	{
		Color c;
		if (rnds::themeColor(name, c)) return c;
		static const struct { const char* n; uint32_t rgb; float a; } D[] = {
			{ "matTop", 0x1f1e1b, 1 }, { "matMid", 0x141311, 1 }, { "matLow", 0x100f0e, 1 }, { "selTop", 0x2a2824, 1 }, { "selLow", 0x1d1c19, 1 },
			{ "lit", 0xfffaf0, 0.07f }, { "litLow", 0x000000, 0 }, { "hair", 0xefe9de, 0.08f }, { "hair2", 0xefe9de, 0.14f }, { "tileEdge", 0xefe9de, 0.10f },
			{ "ink2", 0xaaa398, 1 }, { "dim", 0x8a847a, 1 }, { "surface", 0x0f0e0d, 1 }, { "raised", 0x181715, 1 }, { "raised2", 0x23211e, 1 },
			{ "danger", 0xc93338, 1 }, { "dangerTop", 0xd84a4e, 1 }, { "dangerLow", 0xb12d32, 1 }, { "dangerText", 0xf0767c, 1 }, { "dangerInk", 0xffffff, 1 },
			{ "fav", 0xf27fb2, 1 }, { "favTop", 0xf79ac1, 1 }, { "ok", 0x4cc38a, 1 },
			{ "keyTop", 0xfffaf0, 1 }, { "keyInk", 0x141312, 1 }, { "keyLip", 0x000000, 0.22f }, { "keyLit", 0xffffff, 0 },
			{ "dots", 0xfffaf0, 0.045f }, { "track", 0x0a0a09, 1 },
			{ "plateLit", 0xffffff, 0.4f }, { "plateLip", 0x000000, 0.16f }, { "knobTop", 0x2a2620, 1 }, { "qrGround", 0xefe9de, 1 },
			{ "cardRim", 0xffffff, 0 }, { "iconLit", 0xffffff, 0 }, { "iconEdge", 0xffffff, 0 }, { "iconOnLit", 0xffffff, 0.4f }, { "iconOnLip", 0x000000, 0 },
		};
		for (auto& d : D) if (!strcmp(d.n, name)) return Color::hex(d.rgb, d.a);
		const Color a = Color::hex(rnds::accent.base);
		if (!strcmp(name, "ring") || !strcmp(name, "plateMid")) return a;
		if (!strcmp(name, "iconTop") || !strcmp(name, "iconLow")) return mcol("raised2");
		if (!strcmp(name, "iconInk")) return mcol("ink2");
		const rnds::ThemeFiles& tf = rnds::themeFiles;
		if (!strcmp(name, "plateTop")) return oklabMix(a, 1 - tf.plateMix[0], Color(1, 1, 1));
		if (!strcmp(name, "plateLow")) return oklabMix(a, 1 - tf.plateMix[2], Color(0, 0, 0));
		if (tf.stateMix[0] >= 0)
		{
			// (a state of its own: a glaze of the accent where the plate is something else, Thor White's shell)
			if (!strcmp(name, "stateTop")) return oklabMix(a, 1 - tf.stateMix[0], Color(1, 1, 1));
			if (!strcmp(name, "stateMid")) return a;
			if (!strcmp(name, "stateLow")) return oklabMix(a, 1 - tf.stateMix[2], Color(0, 0, 0));
		}
		if (!strcmp(name, "knobLow")) return mcol("stateInk");
		if (!strcmp(name, "knobOffTop")) return pal(light).ink;
		if (!strcmp(name, "knobOffLow")) return mcol("ink2");
		// (text on the accent: the theme's ink for it, else the accent nearly black; Settings > Text on accent decides
		// over both)
		const Color aInk = rnds::accentTextMode ? Color::hex(rnds::inkOn(rnds::accent.base)) : rnds::accentInk ? Color::hex(rnds::accentInk & 0xffffff) : oklabMix(a, 0.12f, Color(0, 0, 0));
		if (!strcmp(name, "plateInk")) return aInk;
		if (!strcmp(name, "plateSub")) return mcol("plateInk");
		// what says "on" (a switch, a bar's fill, the main hint's key): the plate, unless the theme has another (a glaze)
		if (!strcmp(name, "stateMid")) return mcol("plateMid");
		if (!strcmp(name, "stateTop")) return mcol("plateTop");
		if (!strcmp(name, "stateLow")) return mcol("plateLow");
		if (!strcmp(name, "stateInk")) return aInk;
		if (!strcmp(name, "keyLow")) return pal(light).ink;
		if (!strcmp(name, "ink")) return pal(light).ink;
		return Color(1, 0, 1);
	}

	// ---- surfaces -----------------------------------------------------------------------------------------------
	namespace
	{
		struct Stop { float at; Color c; };
		struct MSpec
		{
			float r[4] = { 0, 0, 0, 0 };	// corners: top-left, top-right, bottom-right, bottom-left (px)
			std::vector<Stop> stops;	// top to bottom
			Color ringC, litC, lowC, lit2C, dotsC, stepC, rimC;
			float ring = 0, lit = 0, low = 0, lit2 = 0, step = 0, dotPitch = 0, dotR = 0, rim = 0;	// px
			const rnds::Bitmap* tex = nullptr;	// tiled over it (RGBA), a texel texQ px
			float texQ = 1;
		};

		// the surface w x h px (and its step under it), straight RGBA
		rnds::Bitmap paintM(const MSpec& m, int w, int h)
		{
			rnds::Bitmap b;
			const int H = h + (int)std::ceil(m.step);
			b.w = w; b.h = H; b.alpha = false;
			b.px.assign((size_t)w * H * 4, 0);
			const float hw = w / 2.0f, hh = h / 2.0f;
			auto sd = [&](float px, float py)
			{
				const float r = px < hw ? (py < hh ? m.r[0] : m.r[3]) : (py < hh ? m.r[1] : m.r[2]);
				const float qx = std::fabs(px - hw) - hw + r, qy = std::fabs(py - hh) - hh + r;
				return std::min(std::max(qx, qy), 0.0f) + std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) - r;
			};
			auto cov = [](float d) { return std::max(0.0f, std::min(1.0f, 0.5f - d)); };
			auto grad = [&](float t)
			{
				if (m.stops.empty()) return Color(0, 0, 0, 0);
				if (t <= m.stops.front().at) return m.stops.front().c;
				for (size_t i = 1; i < m.stops.size(); i++)
					if (t <= m.stops[i].at)
					{
						const Stop& a = m.stops[i - 1], &z = m.stops[i];
						const float f = z.at > a.at ? (t - a.at) / (z.at - a.at) : 1;
						return Color(a.c.r + (z.c.r - a.c.r) * f, a.c.g + (z.c.g - a.c.g) * f, a.c.b + (z.c.b - a.c.b) * f, a.c.a + (z.c.a - a.c.a) * f);
					}
				return m.stops.back().c;
			};
			auto over = [](Color& c, const Color& o, float k) { const float a = o.a * k; c.r += (o.r - c.r) * a; c.g += (o.g - c.g) * a; c.b += (o.b - c.b) * a; };
			for (int y = 0; y < H; y++)
			{
				const float py = y + 0.5f;
				Color row = grad(h > 1 ? (float)y / (h - 1) : 0);
				for (int x = 0; x < w; x++)
				{
					const float px = x + 0.5f;
					const float d = sd(px, py), a = y < h ? cov(d) : 0;
					float outA = a * row.a;
					Color c = row;
					if (a > 0)
					{
						if (m.dotPitch > 0)
						{
							const float fx = std::fmod(px, m.dotPitch) - m.dotPitch / 2, fy = std::fmod(py, m.dotPitch) - m.dotPitch / 2;
							const float k = std::max(0.0f, std::min(1.0f, m.dotR + 0.5f - std::hypot(fx, fy)));
							if (k > 0) over(c, m.dotsC, k);
						}
						if (m.tex && m.tex->w > 0 && m.tex->h > 0)
						{
							const uint8_t* t = &m.tex->px[((size_t)((int)(y / m.texQ) % m.tex->h) * m.tex->w + (int)(x / m.texQ) % m.tex->w) * 4];
							if (t[3]) over(c, Color(t[0] / 255.0f, t[1] / 255.0f, t[2] / 255.0f, 1), t[3] / 255.0f);
						}
						if (m.rim > 0) over(c, m.rimC, std::max(0.0f, a - cov(sd(px, py - m.rim))));
						if (m.lit > 0) over(c, m.litC, std::max(0.0f, a - cov(sd(px, py - m.lit))));
						if (m.lit2 > 0) over(c, m.lit2C, std::max(0.0f, a - cov(sd(px, py + m.lit2))));
						if (m.low > 0) over(c, m.lowC, std::max(0.0f, a - cov(sd(px, py + m.low))));
						if (m.ring > 0) over(c, m.ringC, std::max(0.0f, a - cov(d + m.ring)));
					}
					if (m.step > 0 && a < 1)
					{
						// the step under a key: the shape again, lower, where the key itself isn't
						const float sa = cov(sd(px, py - m.step)) * m.stepC.a * (1 - a);
						if (sa > 0)
						{
							const float t = outA + sa;
							c.r = (c.r * outA + m.stepC.r * sa) / t; c.g = (c.g * outA + m.stepC.g * sa) / t; c.b = (c.b * outA + m.stepC.b * sa) / t;
							outA = t;
						}
					}
					if (outA <= 0) continue;
					uint8_t* o = &b.px[((size_t)y * w + x) * 4];
					o[0] = (uint8_t)std::lround(std::max(0.0f, std::min(1.0f, c.r)) * 255); o[1] = (uint8_t)std::lround(std::max(0.0f, std::min(1.0f, c.g)) * 255);
					o[2] = (uint8_t)std::lround(std::max(0.0f, std::min(1.0f, c.b)) * 255); o[3] = (uint8_t)std::lround(std::min(1.0f, outA) * 255);
				}
			}
			return b;
		}
	}

	// one of the look's surfaces at (x, y) w x h (canvas units), made for the screen's own pixels and kept
	void Settings::mpaint(const std::string& style, float x, float y, float w, float h)
	{
		const float rk = shift.rk(), q = shift.px();	// screen px a device px, screen px a design px
		const int dw = std::max(1, (int)std::lround(X(w) * rk)), dh = std::max(1, (int)std::lround(X(h) * rk));
		std::string key = "m:" + style + ":" + std::to_string(dw) + "x" + std::to_string(dh);
		auto it = cache.find(key);
		if (it == cache.end())
		{
			MSpec m;
			auto radius = [&](float r) { r = std::min(r * q, std::min(dw, dh) / 2.0f); for (float& v : m.r) v = r; };
			auto mat = [&] { m.stops = { { 0, mcol("matTop") }, { 0.7f, mcol("matMid") }, { 1, mcol("matLow") } }; m.lit = 2 * q; m.litC = mcol("lit"); m.lit2 = 2 * q; m.lit2C = mcol("litLow"); };
			const rnds::ThemeFiles& tf = rnds::themeFiles;
			const float pAt = tf.plateMix[1], sAt = tf.stateMix[0] >= 0 ? tf.stateMix[1] : pAt;
			auto plate = [&](const char* top, const char* mid, const char* low, float at)
			{
				m.stops = { { 0, mcol(top) }, { at, mcol(mid) }, { 1, mcol(low) } };
				m.lit = 3 * q; m.litC = mcol("plateLit"); m.low = tf.plateMix[3] * q; m.lowC = mcol("plateLip");
			};
			// (the theme's texture over cards, rows and tiles; without one the cards carry the LCD's dots)
			static std::map<std::string, rnds::Bitmap> textures;
			auto texture = [&]
			{
				if (tf.setTexture.empty()) return false;
				auto t = textures.find(tf.setTexture);
				if (t == textures.end())
				{
					rnds::Bitmap b = rnds::decodeFile(tf.setTexture);
					if (b.alpha) b = rnds::Bitmap();	// (RGBA only)
					t = textures.emplace(tf.setTexture, b).first;
				}
				if (t->second.empty()) return false;
				m.tex = &t->second; m.texQ = std::max(0.25f, q);
				return true;
			};
			auto dots = [&] { if (texture()) return; m.dotPitch = 8 * q; m.dotR = 1.2f * q; m.dotsC = mcol("dots"); };
			const bool big = dh >= 180 * q;
			if (style == "row") { radius(24); mat(); texture(); m.ring = 2 * q; m.ringC = mcol("hair"); }
			else if (style == "rowsel")
			{
				radius(big ? 32 : 24);
				m.stops = { { 0, mcol("selTop") }, { 1, mcol("selLow") } };
				texture();
				m.lit = 2 * q; m.litC = mcol("lit"); m.ring = 4 * q; m.ringC = mcol("ring");
			}
			else if (style == "tile") { radius(32); mat(); texture(); m.ring = 2 * q; m.ringC = mcol("tileEdge"); }
			else if (style == "plate") { radius(32); plate("plateTop", "plateMid", "plateLow", pAt); }
			else if (style == "danger") { radius(32); plate("dangerTop", "danger", "dangerLow", 0.4f); m.litC = Color(1, 1, 1, 0.3f); }
			else if (style == "card") { radius(dh < 110 * q ? 24 : 32); mat(); dots(); m.ring = 2 * q; m.ringC = mcol("hair"); m.rim = 3 * q; m.rimC = mcol("cardRim"); }
			else if (style == "pill") { radius(1000); mat(); m.ring = 2 * q; m.ringC = mcol("hair"); }
			else if (style.compare(0, 3, "key") == 0)
			{
				// a hint's key: keyF a face button (round), keyS a shoulder, keyW a word's (Start, Select, Home);
				// ...1 the main action's (on the plate), ...2 a warning's (pink)
				const char shape = style.size() > 3 ? style[3] : 'F', st = style.size() > 4 ? style[4] : '0';
				if (shape == 'F') radius(1000);
				else if (shape == 'S') { m.r[0] = m.r[1] = 20 * q; m.r[2] = m.r[3] = 8 * q; }
				else radius(12);
				Color mid;
				if (st == '1') m.stops = { { 0, mcol("stateTop") }, { sAt, mcol("stateMid") }, { 1, mcol("stateLow") } };
				else if (st == '2') m.stops = { { 0, mcol("favTop") }, { 1, mcol("fav") } };
				else if (rnds::themeColor("keyMid", mid)) m.stops = { { 0, mcol("keyTop") }, { 0.55f, mid }, { 1, mcol("keyLow") } };
				else m.stops = { { 0, mcol("keyTop") }, { 1, mcol("keyLow") } };
				m.low = 3 * q; m.lowC = mcol("keyLip"); m.lit = 2 * q; m.litC = mcol("keyLit");
				m.step = 2 * q; m.stepC = Color(0, 0, 0, 0.7f);
			}
			else if (style == "icon") { radius(8); m.stops = { { 0, mcol("iconTop") }, { 1, mcol("iconLow") } }; m.lit = 2 * q; m.litC = mcol("iconLit"); m.ring = 2 * q; m.ringC = mcol("iconEdge"); }
			else if (style == "iconOn") { radius(8); m.stops = { { 0, mcol("plateTop") }, { pAt, mcol("plateMid") }, { 1, mcol("plateLow") } }; m.lit = 2 * q; m.litC = mcol("iconOnLit"); m.low = 3 * q; m.lowC = mcol("iconOnLip"); }
			else if (style == "swOff") { radius(1000); m.stops = { { 0, mcol("raised") } }; m.ring = 2 * q; m.ringC = mcol("hair2"); }
			else if (style == "swOn") { radius(1000); m.stops = { { 0, mcol("stateTop") }, { sAt, mcol("stateMid") }, { 1, mcol("stateLow") } }; m.lit = 2 * q; m.litC = Color(1, 1, 1, 0.35f); }
			else if (style == "knobOff") { radius(1000); m.stops = { { 0, mcol("knobOffTop") }, { 1, mcol("knobOffLow") } }; }
			else if (style == "knobOn") { radius(1000); m.stops = { { 0, mcol("knobTop") }, { 1, mcol("knobLow") } }; }
			else if (style == "track") { radius(6); m.stops = { { 0, mcol("track") } }; }
			else if (style == "fill") { radius(6); m.stops = { { 0, mcol("stateTop") }, { sAt, mcol("stateMid") }, { 1, mcol("stateLow") } }; }
			else if (style == "scrub") { radius(4); m.stops = { { 0, mcol("raised2") } }; }
			else if (style == "thumb") { radius(4); m.stops = { { 0, mcol("ring") } }; }
			else if (style == "dot") { radius(1000); m.stops = { { 0, mcol("ring") } }; }
			else if (style == "raised") { radius(12); m.stops = { { 0, mcol("raised2") } }; m.lit = 2 * q; m.litC = mcol("lit"); }
			else if (style == "kbkey") { radius(14); mat(); m.ring = 2 * q; m.ringC = mcol("hair"); }
			else if (style == "kbkeyOn") { radius(14); plate("plateTop", "plateMid", "plateLow", pAt); m.low = 4 * q; }
			else if (style.compare(0, 3, "sw#") == 0 || style.compare(0, 4, "cap#") == 0)
			{
				// a swatch of the palette (flat, a hairline ring) or an accent's cap (round, lit as the plate)
				const bool cap = style[0] == 'c';
				const Color c = Color::hex((uint32_t)strtoul(style.c_str() + (cap ? 4 : 3), nullptr, 16));
				if (cap)
				{
					radius(1000);
					m.stops = { { 0, oklabMix(c, 0.82f, Color(1, 1, 1)) }, { 0.4f, c }, { 1, oklabMix(c, 0.86f, Color(0, 0, 0)) } };
					m.lit = 3 * q; m.litC = Color(1, 1, 1, 0.4f); m.low = 6 * q; m.lowC = Color(0, 0, 0, 0.2f);
				}
				else { radius(32); m.stops = { { 0, c } }; m.ring = 2 * q; m.ringC = mcol("hair2"); }
			}
			else if (style == "capring") { radius(1000); m.stops = { { 0, mcol("ink") } }; }
			else if (style == "caphole") { radius(1000); m.stops = { { 0, pal(light).screenBg } }; }
			else if (style == "qr") { radius(32); m.stops = { { 0, mcol("qrGround") } }; }
			else if (style == "black") { radius(16); m.stops = { { 0, Color(0, 0, 0) } }; m.ring = 2 * q; m.ringC = mcol("hair2"); }
			else { radius(24); mat(); }
			rnds::Bitmap bmp = paintM(m, dw, dh);
			Cached c;
			c.tex = be->upload(bmp, true);
			it = put(key, c, bmp);
		}
		const Cached& c = keep(it);
		be->draw(c.tex, X(x), X(y), c.tex->w / rk, c.tex->h / rk, Color(1, 1, 1, 1));
	}

	// the engine's box kinds in this look. The top screen's card and a page's title box are left out (text straight on
	// the black); a row is the material, the picked one a lighter one in the accent's ring; cards carry the dots
	bool Settings::mbox(const std::string& kind, bool on, float x, float y, float w, float h)
	{
		if (kind == "screen")
		{
			SPal p = pal(light);
			be->fill(X(x), X(y), X(w), X(h), p.screenBg);
			return true;
		}
		if (kind == "hero" && x == 18 && y == 20 && w == 604 && h == 436 && shift.screen == 0) return true;
		if (kind == "bubble" && x == 18 && y == 14 && w == 604 && h == 54) return true;
		const bool big = h >= 100;
		if (kind == "pick") mpaint("rowsel", x, y, w, h);
		else if (kind == "slot") mpaint(on ? "rowsel" : big ? "tile" : "row", x, y, w, h);
		else if (kind == "hero" || kind == "bubble" || kind == "osd") mpaint("card", x, y, w, h);
		else if (kind == "face" || kind == "pill" || kind == "pillA") mpaint("pill", x, y, w, h);
		else if (kind == "kbd") mpaint("kbkey", x, y, w, h);
		else if (kind == "tile") mpaint("icon", x, y, w, h);
		else if (kind == "qrcard") mpaint("qr", x, y, w, h);
		else if (kind == "plate" || kind == "danger" || kind == "card" || kind == "row" || kind == "rowsel" || kind == "tileM" || kind == "kbkey" || kind == "kbkeyOn" || kind == "raised" || kind == "black") mpaint(kind == "tileM" ? "tile" : kind, x, y, w, h);
		else return false;
		return true;
	}

	// ---- text ---------------------------------------------------------------------------------------------------
	// names, titles and numbers stay in the theme's pixel face (the medium one from 21.5 units up); every label, value
	// and hint is Inter, in sentence case with no letter-spacing
	void Settings::role(bool medium, float& size, std::shared_ptr<rnds::Font>& font, bool& upper, float& ls) const
	{
		if (medium && size >= 21.5f) return;
		upper = false; ls = 0;
		if (medium) { font = IM; size *= 0.92f; }
		else { font = IR; size = std::min(size, std::max(17.0f, size * 0.85f)); }
	}

	// ---- line icons ---------------------------------------------------------------------------------------------
	bool Settings::hasGlyph(const std::string& name, int size)
	{
		if (rnds::themeFiles.setGlyphs.empty()) return false;
		std::string key = "glyph:" + name + ":" + std::to_string(size);
		auto it = cache.find(key);
		if (it == cache.end())
		{
			rnds::Bitmap b = rnds::decodeFile(rnds::themeFiles.setGlyphs + "/" + name + "-" + std::to_string(size) + ".png"), a;
			Cached c;
			if (!b.empty())
			{
				a.w = b.w; a.h = b.h; a.alpha = true;
				a.px.resize((size_t)b.w * b.h);
				for (size_t i = 0; i < a.px.size(); i++) a.px[i] = b.px[i * 4 + 3];
				c.tex = be->upload(a, true);
			}
			it = put(key, c, a);
		}
		return (bool)keep(it).tex;
	}

	void Settings::glyph(const std::string& name, int size, float x, float y, const Color& c)
	{
		if (!hasGlyph(name, size)) return;
		const Cached& g = cache.find("glyph:" + name + ":" + std::to_string(size))->second;
		const float w = X(px((float)size));
		be->draw(g.tex, X(x), X(y), w, w * g.tex->h / g.tex->w, c);
	}

	void Settings::glyphAt(const std::string& name, int size, float x, float y, float w, const Color& c)
	{
		if (!hasGlyph(name, size)) return;
		const Cached& g = cache.find("glyph:" + name + ":" + std::to_string(size))->second;
		be->draw(g.tex, X(x), X(y), X(w), X(w) * g.tex->h / g.tex->w, c);
	}

	// ---- the hints' keys ----------------------------------------------------------------------------------------
	static const char* const* keyGlyph(const std::string& k)
	{
		static const char* const L[] = { "#....", "#....", "#....", "#....", "#....", "#....", "#####" };
		static const char* const R[] = { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" };
		static const char* const A[] = { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" };
		static const char* const B[] = { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." };
		static const char* const Xg[] = { "#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#" };
		static const char* const Y[] = { "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.." };
		if (k == "A") return A;
		if (k == "B") return B;
		if (k == "X") return Xg;
		if (k == "Y") return Y;
		if (k == "L") return L;
		if (k == "R") return R;
		return nullptr;
	}
	static std::string keyWord(const std::string& k)
	{
		if (k == "START") return "Start";
		if (k == "SELECT") return "Select";
		if (k == "HOME") return "Home";
		return k;
	}

	float Settings::keycapWidth(const std::string& key)
	{
		if (keyGlyph(key)) return px(key == "L" || key == "R" ? 64 : 48);
		float size = px(24);
		std::shared_ptr<rnds::Font> f = IM;
		return std::max(px(48), textWith(keyWord(key), f, "|k", size, 0, 0, Color(), false, 0, true) + px(24));
	}

	// the key at (x, y: the top of a 48 px line): a face button's round, a shoulder's with its round top, a word's box
	void Settings::keycap(const std::string& key, float x, float y, int state)
	{
		const char* const* g = keyGlyph(key);
		const bool shoulder = key == "L" || key == "R";
		const float w = keycapWidth(key), h = px(g && !shoulder ? 48 : 44), top = y + (px(48) - h) / 2;
		// (a theme's own colour for a face button: the Rainbow shell's four)
		std::string style = std::string("key") + (g ? (shoulder ? "S" : "F") : "W") + (state == 1 ? "1" : state == 2 ? "2" : "0");
		Color ink = state == 1 ? mcol("stateInk") : mcol("keyInk");
		Color own;
		if (g && !shoulder && rnds::themeColor(("key" + key).c_str(), own))
		{
			// (painted in its colour: a lighter top, the colour from the middle down)
			const float rk = shift.rk(), q = shift.px();
			const int d = std::max(1, (int)std::lround(X(w) * rk));
			std::string ck = "m:keyOwn" + key + ":" + std::to_string(d);
			auto it = cache.find(ck);
			if (it == cache.end())
			{
				MSpec m;
				for (float& v : m.r) v = d / 2.0f;
				m.stops = { { 0, oklabMix(own, 0.85f, Color(1, 1, 1)) }, { 0.45f, own }, { 1, own } };
				m.lit = 3 * q; m.litC = Color(1, 1, 1, 0.35f); m.low = 3 * q; m.lowC = Color(0, 0, 0, 0.2f); m.step = 2 * q; m.stepC = Color(0, 0, 0, 0.7f);
				rnds::Bitmap bmp = paintM(m, d, d);
				Cached c;
				c.tex = be->upload(bmp, true);
				it = put(ck, c, bmp);
			}
			const Cached& c = keep(it);
			be->draw(c.tex, X(x), X(top), c.tex->w / rk, c.tex->h / rk, Color(1, 1, 1, 1));
			Color oi;
			ink = rnds::themeColor(("key" + key + "Ink").c_str(), oi) ? oi : Color::hex(rnds::inkOn((uint32_t)(std::lround(own.r * 255) << 16 | std::lround(own.g * 255) << 8 | std::lround(own.b * 255))));
		}
		else mpaint(style, x, top, w, h);
		if (g)
		{
			// the letter: 5 x 7 pixels of 3 px, in the middle
			const float cell = px(3), gx = x + (w - 5 * cell) / 2, gy = top + (h - 7 * cell) / 2;
			for (int r = 0; r < 7; r++)
				for (int c = 0; c < 5;)
				{
					if (g[r][c] != '#') { c++; continue; }
					int e = c;
					while (e < 5 && g[r][e] == '#') e++;
					be->fill(X(gx + c * cell), X(gy + r * cell), X((e - c) * cell), X(cell), ink);
					c = e;
				}
			return;
		}
		float size = px(24);
		std::shared_ptr<rnds::Font> f = IM;
		const std::string word = keyWord(key);
		const float tw = textWith(word, f, "|k", size, 0, 0, ink, false, 0, true);
		textWith(word, f, "|k", size, x + (w - tw) / 2, top + (h - size * 1.22f) / 2, ink);
	}

	// ---- controls -----------------------------------------------------------------------------------------------
	void Settings::mswitch(float x, float y, bool on)
	{
		mpaint(on ? "swOn" : "swOff", x, y, px(88), px(48));
		mpaint(on ? "knobOn" : "knobOff", x + px(on ? 46 : 6), y + px(6), px(36), px(36));
	}

	void Settings::mbar(float x, float y, float w, double fraction)
	{
		fraction = std::max(0.0, std::min(1.0, fraction));
		mpaint("track", x, y, w, px(12));
		if (fraction > 0) mpaint("fill", x, y, std::max(px(12), w * (float)fraction), px(12));
	}

	// ---- the list ---------------------------------------------------------------------------------------------------
	Settings::Measures Settings::measures() const
	{
		Measures m;
		if (rnds::themeFiles.panels != "material" || !shift.lay || shift.u1 <= 0) return m;
		const float u = shift.u1;
		m.rowY = (128 + 14) * u; m.rowH = 96 * u; m.rowPitch = 112 * u;
		m.gridY = (128 + 14) * u; m.gridH = 320 * u; m.gridW = (604 - 2 * 24 * u) / 3; m.gridPX = m.gridW + 24 * u; m.gridPY = m.gridH + 24 * u;
		return m;
	}

	// a row's line icon, by what the row is
	std::string Settings::rowGlyph(const Item& it) const
	{
		static const struct { const char* key; const char* glyph; } K[] = {
			{ "theme", "palette" }, { "accent", "drop" }, { "accent_text", "text" }, { "motion", "motion" }, { "clock", "clock" }, { "clock24", "clock" },
			{ "timezone", "globe" }, { "language", "globe" }, { "wifi", "wifi" }, { "network", "wifi" }, { "about", "info" }, { "volume", "speaker" },
			{ "sounds", "speaker" }, { "steam", "user" }, { "ra_account", "trophy" }, { "share_account", "user" }, { "ds_name", "text" },
			{ "update", "load" }, { "sys_updates", "load" }, { "scrape", "cloud" }, { "sys_boxart", "cloud" }, { "hs_sync", "cloud" },
		};
		for (auto& k : K) if (it.key == k.key) return k.glyph;
		if (it.kind == Item::BRIGHTNESS) return "sun";
		if (it.kind == Item::VOLUME) return "speaker";
		if (it.kind == Item::INFO) return "info";
		if (it.kind == Item::ACTION) return it.key.find("off") != std::string::npos || it.key.find("reboot") != std::string::npos || it.key.find("sleep") != std::string::npos ? "power" : "bolt";
		if (it.kind == Item::PAGE) return "open";
		if (it.values.size() == 2 && ((it.values[0] == "on" && it.values[1] == "off") || (it.values[0] == "off" && it.values[1] == "on"))) return "toggle";
		return "sliders";
	}

	void Settings::listRow(const Item& it, float y, float h, bool on, const std::string& value, bool arrows, bool armedNow)
	{
		SPal p = pal(light);
		const float left = 18, right = 622;
		// the icon's tile: raised, or the plate on the picked row
		const float tile = px(64), tx = left + px(16), ty = y + (h - tile) / 2;
		mpaint(on ? "iconOn" : "icon", tx, ty, tile, tile);
		glyph(rowGlyph(it), 32, tx + px(16), ty + px(16), on ? mcol("plateSub") : mcol("iconInk"));
		const float lx = tx + tile + px(24), z = px(34), tyText = y + (h - z * 1.2f) / 2;
		const Color ink2 = mcol("ink2");
		float vright = right - px(24);
		float vleft = vright;
		const bool toggle = it.kind == Item::CHOICE && it.values.size() == 2 && ((it.values[0] == "on" && it.values[1] == "off") || (it.values[0] == "off" && it.values[1] == "on"));
		if (toggle)
		{
			vleft = vright - px(88);
			mswitch(vleft, y + (h - px(48)) / 2, it.values[it.at] == "on");
		}
		else if (!value.empty())
		{
			const Color vc = armedNow ? mcol("dangerText") : on ? p.ink : ink2;
			const float room = px(480);
			std::string shown = value;
			float vw = textWith(shown, IR, "|ir", z, 0, 0, vc, false, 0, true);
			if (vw > room)
			{
				// (cut to the room: in the regular face's own measure)
				while (shown.size() > 4 && textWith(shown + "...", IR, "|ir", z, 0, 0, vc, false, 0, true) > room) { shown.pop_back(); while (!shown.empty() && ((unsigned char)shown.back() & 0xc0) == 0x80) shown.pop_back(); }
				shown += "...";
				vw = textWith(shown, IR, "|ir", z, 0, 0, vc, false, 0, true);
			}
			const bool chev = arrows && on;
			if (chev) vright -= px(32) + px(16);
			vleft = vright - vw;
			textWith(shown, IR, "|ir", z, vleft, tyText, vc);
			if (it.kind == Item::BRIGHTNESS || it.kind == Item::VOLUME)
			{
				// a meter of five bars before the percentage
				const int n = (int)it.names.size(), lit = n > 1 ? (int)std::lround(5.0 * it.at / (n - 1)) : 0;
				vleft -= px(24) + 5 * px(12) + 4 * px(8);
				for (int b = 0; b < 5; b++) be->fill(X(vleft + b * px(20)), X(y + (h - px(32)) / 2), X(px(12)), X(px(32)), b < lit ? (on ? p.ink : ink2) : mcol("raised"));
			}
			else if (it.key == "accent")
			{
				vleft -= px(16) + px(32);
				mpaint("dot", vleft, y + (h - px(32)) / 2, px(32), px(32));
			}
			if (chev)
			{
				glyph("next", 32, vright + px(16), y + (h - px(32)) / 2, mcol("ring"));
				vleft -= px(16) + px(32);
				glyph("prev", 32, vleft, y + (h - px(32)) / 2, mcol("ring"));
			}
		}
		textFit(it.label, true, z / 0.92f, lx, tyText, vleft - px(24) - lx, p.ink, on);
	}

	// the top screen under the help: the Theme row's palette as six swatches, the Accent colour row's choices as round
	// caps, the one in use ringed
	void Settings::listExtras(const Item& it)
	{
		SPal p = pal(light);
		auto hex = [](const Color& c) { char b[16]; snprintf(b, sizeof b, "%02x%02x%02x", (int)std::lround(c.r * 255), (int)std::lround(c.g * 255), (int)std::lround(c.b * 255)); return std::string(b); };
		const float y = px(720) - shift.bodyDy / s, z = px(28);
		if (it.key == "theme")
		{
			const struct { const char* name; Color c; } S[] = { { "Background", p.screenBg }, { "Surface", mcol("surface") }, { "Raised", mcol("raised") },
				{ "Selected", mcol("raised2") }, { "Text", p.ink }, { "Accent", mcol("ring") } };
			for (int i = 0; i < 6; i++)
			{
				const float x = HELP_X + i * px(192);
				mpaint("sw#" + hex(S[i].c), x, y, px(160), px(160));
				textWith(S[i].name, IR, "|ir", z, x, y + px(176), mcol("dim"));
			}
			// a theme whose face buttons have colours of their own (the Rainbow Thor's): a key to them beside the swatches
			Color own;
			if (rnds::themeColor("keyA", own))
			{
				const struct { const char* key; const char* what; } L[] = { { "A", "Open" }, { "B", "Back" }, { "X", "Options" }, { "Y", "Favourite" } };
				const float lx = HELP_X + px(1176);
				textWith("Button hints", IR, "|ir", z, lx, y - px(4), mcol("dim"));
				float col2 = 0;
				for (int i = 0; i < 4; i += 2) col2 = std::max(col2, px(48 + 12 + 24) + textWith(L[i].what, IM, "|im", z, 0, 0, p.ink, false, 0, true));
				for (int i = 0; i < 4; i++)
				{
					const float kx = lx + (i % 2) * col2, ky = y + px(54) + (i / 2) * px(72);
					keycap(L[i].key, kx, ky, 0);
					textWith(L[i].what, IM, "|im", z, kx + px(60), ky + (px(48) - z * 1.22f) / 2, mcol("ink2"));
				}
			}
			return;
		}
		float x = HELP_X;
		for (int i = 0; i < (int)it.values.size() && i < 8; i++)
		{
			Color c;
			bool known = false;
			for (auto& a : accentChoices) if (a.value == it.values[i]) { c = Color::hex(a.color); known = true; }
			if (!known) continue;
			const bool cur = i == it.at;
			const float nw = textWith(it.names[i], IR, "|ir", z, 0, 0, p.ink, false, 0, true), cell = std::max(px(96), nw);
			if (cur) { mpaint("capring", x + cell / 2 - px(60), y - px(12), px(120), px(120)); mpaint("caphole", x + cell / 2 - px(56), y - px(8), px(112), px(112)); }
			mpaint("cap#" + hex(c), x + cell / 2 - px(48), y, px(96), px(96));
			textWith(it.names[i], IR, "|ir", z, x + cell / 2 - nw / 2, y + px(120), cur ? p.ink : mcol("dim"));
			x += cell + px(32);
		}
	}
}
