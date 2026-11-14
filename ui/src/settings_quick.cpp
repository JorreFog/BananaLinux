// banana-ui: the quick menu (the AYN Thor's Home button held for 3 s in a game: main.cpp brings it up over the bottom
// screen as a sheet, the game going on above it). One page on the bottom screen: the two screens' brightness and the
// volume as sliders, the running game's performance mode, and the handheld's fan and stick lights where it has them.
// Left and right (or a tap) change one and it takes effect at once, through the same rows and tools as Settings; B
// closes it (and Home, main.cpp's). In a theme's material look (the Thor's) it is a board of tiles: the time and the
// date, the sliders as wide bars, the choices as tiles, and under them what the handheld is doing right now (the
// processor, the graphics, the memory, the power: the sysstat tool, asked once a second). The top screen isn't drawn.
#include "settings.h"
#include "settings_draw.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace banana
{
	using rnds::Color;

	// a game's performance mode (as its game page's Performance row: per-game-perf.conf, banana-perf's profiles)
	static const char* const QUICK_PERF_VALUES[] = { "", "powersave", "balanced", "steady", "performance" };
	static const char* const QUICK_PERF_NAMES[] = { "As the system's", "Battery saver", "Balanced", "Steady", "Performance" };

	// the rows' place on the bottom panel (CSS px of the 640x480 canvas): big enough for a thumb
	static constexpr float Q_SLIDER_X = 300, Q_SLIDER_W = 236;

	// ---- the board (the material look) -------------------------------------------------------------------------------
	// its measures, the design's px on the 1240x1080 bottom screen: the sliders, the choices three a row, the meters two
	namespace
	{
		constexpr float QB_X = 64, QB_W = 1112, QB_GAP = 24;
		constexpr float QB_SL_Y = 128, QB_SL_H = 80, QB_SL_PITCH = 96;
		constexpr float QB_T_H = 104, QB_T_PITCH = 120, QB_M_H = 128, QB_M_PITCH = 144;
		struct QRect { float x, y, w, h; };
		// how many of the rows are sliders (they come first), and where everything is
		struct QBoard
		{
			int sliders = 0, tiles = 0;
			float tilesY = 0, metersY = 0;
			QRect slider(int i) const { return { QB_X, QB_SL_Y + i * QB_SL_PITCH, QB_W, QB_SL_H }; }
			QRect tile(int i) const { const float w = (QB_W - 2 * QB_GAP) / 3; return { QB_X + (i % 3) * (w + QB_GAP), tilesY + (i / 3) * QB_T_PITCH, w, QB_T_H }; }
			QRect meter(int i) const { const float w = (QB_W - QB_GAP) / 2; return { QB_X + (i % 2) * (w + QB_GAP), metersY + (i / 2) * QB_M_PITCH, w, QB_M_H }; }
			QBoard(int s, int t) : sliders(s), tiles(t)
			{
				tilesY = QB_SL_Y + s * QB_SL_PITCH + 8;
				metersY = tilesY + ((t + 2) / 3) * QB_T_PITCH + 8;
			}
		};
	}

	int Settings::quickSliders() const
	{
		int n = 0;
		for (int q : qRows) { if (q < 0 || (items[q].kind != Item::BRIGHTNESS && items[q].kind != Item::VOLUME)) break; n++; }
		return n;
	}

	// what the handheld is doing (the sysstat tool's lines, name=value): asked again a second after the last answer
	bool Settings::quickPoll(double t)
	{
		if (page != QUICK || !mOpen || !qBoard) return false;
		if (qStatAt > 0 && t - qStatAt < 1000) return false;
		qStatAt = t;
		const char* e = getenv("BANANA_SYSSTAT");
		const std::string tool = e && *e ? e : "/usr/libexec/banana/sysstat";
		std::map<std::string, std::string> got;
		if (FILE* f = popen(("'" + tool + "' 2>/dev/null").c_str(), "r"))
		{
			char ln[160];
			while (fgets(ln, sizeof ln, f))
			{
				std::string l = ln;
				while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
				const size_t eq = l.find('=');
				if (eq != std::string::npos && eq > 0) got[l.substr(0, eq)] = l.substr(eq + 1);
			}
			pclose(f);
		}
		// (the first answer has no processor load yet: the last one kept until there is one)
		if (!got.count("cpu") && qStats.count("cpu")) got["cpu"] = qStats["cpu"];
		const bool changed = got != qStats;
		qStats.swap(got);
		return changed;
	}

	void Settings::openQuick()
	{
		for (auto& it : items) load(it);
		mOpen = true;
		wizard = false;
		inGroups = false;
		armed = -1;
		message.clear();
		page = QUICK;
		qRows.clear();
		for (const char* k : { "top", "bottom" })
			for (int i = 0; i < (int)items.size(); i++) if (items[i].key == k && items[i].kind == Item::BRIGHTNESS) qRows.push_back(i);
		for (int i = 0; i < (int)items.size(); i++) if (items[i].kind == Item::VOLUME) { qRows.push_back(i); break; }
		if (!gRomFile.empty())
		{
			qRows.push_back(-1);
			qPerf = 0;
			const std::string pf = gamePerf(gRomFile);
			for (int i = 1; i < 5; i++) if (pf == QUICK_PERF_VALUES[i]) qPerf = i;
		}
		// the handheld's own (handheld caps: the fan, the stick lights), where it has them
		for (const char* k : { "fan", "leds", "leds_level", "leds_effect" })
			for (int i = 0; i < (int)items.size(); i++) if (items[i].key == k && !items[i].values.empty()) qRows.push_back(i);
		qSel = 0;
		qBoard = rnds::themeFiles.panels == "material";	// (until it is drawn: renderQuick knows)
		qStatAt = 0;
		quickPoll(now);
	}

	std::string Settings::quickState()
	{
		if (page != QUICK || !mOpen) return "closed";
		std::string r = "sel=" + std::to_string(qSel);
		for (int q : qRows)
		{
			if (q < 0) { r += std::string(" perf=") + QUICK_PERF_VALUES[qPerf]; continue; }
			const Item& it = items[q];
			r += " " + (it.key.empty() ? std::string("volume") : it.key) + "=" + (it.at >= 0 && it.at < (int)it.values.size() ? it.values[it.at] : "?");
		}
		return r;
	}

	Settings::Result Settings::quickStep(int row, int dir, int to)
	{
		if (row < 0 || row >= (int)qRows.size()) return NONE;
		const int q = qRows[row];
		if (q < 0)
		{
			// the game's performance mode: saved for it (main.cpp applies it again as the game goes on)
			int at = to >= 0 ? std::min(4, to) : (qPerf + dir + 5) % 5;
			if (at == qPerf) return NONE;
			qPerf = at;
			setPerGame("per-game-perf.conf", gRomFile, QUICK_PERF_VALUES[qPerf]);
			return CHANGED;
		}
		Item& it = items[q];
		const int n = (int)it.values.size();
		if (n == 0) return NONE;
		const bool slider = it.kind == Item::BRIGHTNESS || it.kind == Item::VOLUME;
		int at = to >= 0 ? to : it.at + dir;
		at = slider ? std::min(n - 1, std::max(0, at)) : (at % n + n) % n;
		if (at == it.at) return NONE;
		if (it.kind == Item::VOLUME) volPending = false;	// (the level asked for wins over the script's late answer)
		it.at = at;
		save(it);	// (applied as Settings' row applies it: the backlight, the volume, the handheld tool)
		return CHANGED;
	}

	Settings::Result Settings::pressQuick(Key k)
	{
		const int n = (int)qRows.size();
		if (qBoard && n > 0)
		{
			// the board: the sliders one under the other, then the choices three a row
			const int ns = quickSliders(), nt = n - ns, t = qSel - ns;
			int to = qSel;
			switch (k)
			{
			case Key::UP: to = qSel < ns ? (qSel > 0 ? qSel - 1 : n - 1) : t >= 3 ? qSel - 3 : ns > 0 ? ns - 1 : qSel; break;
			case Key::DOWN: to = qSel < ns - 1 ? qSel + 1 : qSel < ns ? (nt > 0 ? ns : 0) : t + 3 < nt ? qSel + 3 : t / 3 < (nt - 1) / 3 ? n - 1 : 0; break;
			case Key::LEFT: if (qSel < ns) return quickStep(qSel, -1); to = t % 3 > 0 ? qSel - 1 : qSel; break;
			case Key::RIGHT: if (qSel < ns) return quickStep(qSel, 1); to = t % 3 < 2 && t + 1 < nt ? qSel + 1 : qSel; break;
			case Key::L: return quickStep(qSel, -1);
			case Key::R: case Key::A: return quickStep(qSel, 1);
			case Key::B: close(); return CLOSED;
			default: return NONE;
			}
			if (to == qSel) return NONE;
			qSel = to;
			return MOVED;
		}
		switch (k)
		{
		case Key::UP: case Key::DOWN:
			if (n < 2) return NONE;
			qSel = (qSel + (k == Key::UP ? -1 : 1) + n) % n;
			return MOVED;
		case Key::LEFT: case Key::L: return quickStep(qSel, -1);
		case Key::RIGHT: case Key::R: case Key::A: return quickStep(qSel, 1);
		case Key::B: close(); return CLOSED;
		default: return NONE;
		}
	}

	Settings::Result Settings::touchQuick(float cx, float cy)
	{
		(void)cy;
		// B's chip closes it
		if (downY >= qChipY - 4 && downY <= qChipY + 44 && downX < 320) { close(); return CLOSED; }
		if (qBoard)
		{
			// the board: a slider's level where the finger lifted, a choice one on
			const int ns = quickSliders(), nt = (int)qRows.size() - ns;
			const QBoard bd(ns, nt);
			const float u = shift.u1;
			auto in = [&](const QRect& r, float x, float y) { return x >= 18 + px(r.x - QB_X) && x < 18 + px(r.x - QB_X + r.w) && y >= (r.y + 14) * u && y < (r.y + r.h + 14) * u; };
			for (int i = 0; i < ns; i++)
			{
				const QRect r = bd.slider(i);
				if (!in(r, downX, downY)) continue;
				qSel = i;
				const Item& it = items[qRows[i]];
				const int n = (int)it.values.size();
				const float f = std::min(1.0f, std::max(0.0f, (cx - 18) / px(r.w)));
				const int to = it.kind == Item::BRIGHTNESS ? std::max(0, (int)std::lround(f * n) - 1) : (int)std::lround(f * (n - 1));
				const Result res = quickStep(i, 0, to);
				return res == NONE ? MOVED : res;
			}
			for (int i = 0; i < nt; i++)
				if (in(bd.tile(i), downX, downY))
				{
					qSel = ns + i;
					const Result res = quickStep(qSel, 1);
					return res == NONE ? MOVED : res;
				}
			return NONE;
		}
		const int r = (int)std::floor((downY - qY) / qPitch);
		if (downY < qY || r < 0 || r >= (int)qRows.size()) return NONE;
		const bool was = r == qSel;
		qSel = r;
		const int q = qRows[r];
		const bool slider = q >= 0 && (items[q].kind == Item::BRIGHTNESS || items[q].kind == Item::VOLUME);
		if (slider && cx >= Q_SLIDER_X - 12 && cx <= Q_SLIDER_X + Q_SLIDER_W + 12)
		{
			// a slider: the level where the finger lifted
			const int n = (int)items[q].values.size();
			float f = std::min(1.0f, std::max(0.0f, (cx - Q_SLIDER_X) / Q_SLIDER_W));
			// (as drawn: a brightness's ten steps 10% to 100%, a volume's 0% to 100%)
			int to = items[q].kind == Item::BRIGHTNESS ? std::max(0, (int)std::lround(f * n) - 1) : (int)std::lround(f * (n - 1));
			Result res = quickStep(r, 0, to);
			return res == NONE ? MOVED : res;
		}
		if (!slider && downX >= Q_SLIDER_X)
		{
			// a choice: its left half one back, its right half one on
			Result res = quickStep(r, downX < Q_SLIDER_X + (608 - Q_SLIDER_X) / 2 ? -1 : 1);
			return res == NONE ? MOVED : res;
		}
		if (was && !slider) return quickStep(r, 1);
		return MOVED;
	}

	void Settings::renderQuick()
	{
		SPal p = pal(light);
		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		// laid out on the whole screen's height (the AYN Thor's bottom screen is taller than the canvas: 39 units more
		// above and below), the rows as tall as fit between the header and the chips
		float ax, ay, aw, ah;
		rnds::screenArea(be, s, ax, ay, aw, ah);
		float top = ay + 14, chipY = ay + ah - 58;
		qY = top + 62;
		const int n = std::max(1, (int)qRows.size());
		qPitch = std::min(64.0f, std::floor((chipY - 10 - qY) / n));
		float rh = qPitch - 6;
		const bool mat = material();
		if (mat && lay())
		{
			// (a screen layout: the title and the hints where every page has them, the rows the list's, closer when
			// there are more than fit)
			const Measures m = measures();
			top = 14; chipY = 422; qY = m.rowY;
			qPitch = std::min(m.rowPitch, ((968 + 14 - 16) * shift.u1 - qY) / n);
			rh = qPitch - (m.rowPitch - m.rowH);
		}
		qChipY = chipY;
		qBoard = mat && lay();
		if (qBoard)
		{
			renderQuickBoard(chipY);
			be->endScreen();
			return;
		}
		// the header: the page's name, a hairline, the game
		box("bubble", false, 18, top, 604, 54);
		float tw = text("Quick menu", true, 22, 38, top + 14, p.ink);
		if (!gStateName.empty())
		{
			std::string g = fit(gStateName, false, 17, 560 - tw - 40);
			float gw = text(g, false, 17, 0, 0, p.muted, false, 0, true);
			text(g, false, 17, 602 - gw, top + 17, p.muted);
		}
		for (int r = 0; r < (int)qRows.size(); r++)
		{
			const bool on = r == qSel;
			const float y = qY + r * qPitch;
			const float Q_H = rh;
			box(on ? "pick" : "slot", on, 18, y, 604, Q_H);
			const Color ink = on ? p.onPick : p.ink, sub = on ? p.onPick : p.muted;
			const int q = qRows[r];
			std::string label, value;
			bool slider = false;
			float frac = 0;
			if (q < 0) { label = "Performance"; value = QUICK_PERF_NAMES[qPerf]; }
			else
			{
				const Item& it = items[q];
				label = it.key == "top" ? "Top screen" : it.key == "bottom" ? "Bottom screen" : it.label;
				value = it.at >= 0 && it.at < (int)it.names.size() ? it.names[it.at] : "";
				slider = it.kind == Item::BRIGHTNESS || it.kind == Item::VOLUME;
				const int n = (int)it.values.size();
				frac = n > 1 ? (float)std::max(0, it.at) / (n - 1) : 0;
				if (it.kind == Item::BRIGHTNESS) frac = n > 0 ? (float)(std::max(0, it.at) + 1) / n : 0;	// (10% is its lowest)
			}
			if (mat)
			{
				// the list's row: its icon tile and label; a level as a 12 px bar filled with the plate and the
				// percentage after it, a choice as the list's value between chevrons
				Item tmp;
				const Item& it = q < 0 ? tmp : items[q];
				if (q < 0) { tmp.key = "perf"; tmp.kind = Item::ACTION; tmp.names = { value }; tmp.values = { value }; }
				tmp.label = label;
				Item shown = it;
				shown.label = label;
				if (!slider) { listRow(shown, y, Q_H, on, value, true, false); continue; }
				listRow(shown, y, Q_H, on, "", false, false);
				mbar(Q_SLIDER_X, y + Q_H / 2 - px(6), Q_SLIDER_W, frac);
				const float z = px(34);
				float vw = text(value, true, z / 0.92f, 0, 0, ink, false, 0, true);
				text(value, true, z / 0.92f, 622 - px(24) - vw, y + (Q_H - z * 1.2f) / 2, on ? p.ink : mcol("ink2"));
				continue;
			}
			const float ty0 = y + Q_H / 2 - 11;	// (the line's top: the text centred in the row)
			textFit(label, true, 19, 40, ty0, Q_SLIDER_X - 40 - 16, ink, on);
			if (slider)
			{
				// the slider: a track, its filled part, a knob; the level after it
				const float ty = y + Q_H / 2 - 3, th = 6;
				const Color track = on ? Color(0, 0, 0, 0.25f) : p.line, fillc = on ? p.onPick : p.pick;
				be->fill(std::round(X(Q_SLIDER_X)), std::round(X(ty)), std::round(X(Q_SLIDER_W)), std::round(X(th)), track);
				be->fill(std::round(X(Q_SLIDER_X)), std::round(X(ty)), std::round(X(Q_SLIDER_W * frac)), std::round(X(th)), fillc);
				const float kx = Q_SLIDER_X + Q_SLIDER_W * frac;
				be->fill(std::round(X(kx - 4)), std::round(X(y + Q_H / 2 - 10)), std::round(X(8)), std::round(X(20)), on ? p.onPick : p.ink);
				float vw = text(value, true, 18, 0, 0, sub, false, 0, true);
				text(value, true, 18, 604 - vw, ty0, sub);
			}
			else
			{
				float vw = text(fit(value, true, 18, 240), true, 18, 0, 0, sub, false, 0, true);
				arrow(true, 588, ty0 + 3, sub);
				text(fit(value, true, 18, 240), true, 18, 578 - vw, ty0, sub);
				arrow(false, 568 - vw - 2 - std::round(13 / 2.0f + 1), ty0 + 3, sub);
			}
		}
		float bw = faceWidth("B", "Close");
		face("B", "Close", 18, bw, chipY);
		float cw = faceWidth("A", "Change");
		face("A", "Change", 622 - cw, cw, chipY);
		be->endScreen();
	}

	// a meter's ring: a track and the part of it that is used, from the top clockwise (w px across, t px thick)
	static rnds::Bitmap quickRing(int w, float t, float frac, const Color& track, const Color& arc)
	{
		rnds::Bitmap b;
		b.w = b.h = w; b.alpha = false;
		b.px.assign((size_t)w * w * 4, 0);
		const float c = w / 2.0f, ro = c - 0.5f, ri = ro - t, end = std::max(0.0f, std::min(1.0f, frac)) * 6.2831853f;
		for (int y = 0; y < w; y++)
			for (int x = 0; x < w; x++)
			{
				const float dx = x + 0.5f - c, dy = y + 0.5f - c, d = std::hypot(dx, dy);
				const float cov = std::max(0.0f, std::min(1.0f, ro - d + 0.5f)) * std::max(0.0f, std::min(1.0f, d - ri + 0.5f));
				if (cov <= 0) continue;
				float a = std::atan2(dx, -dy);
				if (a < 0) a += 6.2831853f;
				// (the arc's end softened over a pixel's angle)
				const float on = frac >= 1 ? 1 : std::max(0.0f, std::min(1.0f, (end - a) * (ro - t / 2) + 0.5f));
				const Color k(track.r + (arc.r - track.r) * on, track.g + (arc.g - track.g) * on, track.b + (arc.b - track.b) * on, track.a + (arc.a - track.a) * on);
				uint8_t* o = &b.px[((size_t)y * w + x) * 4];
				o[0] = (uint8_t)std::lround(k.r * 255); o[1] = (uint8_t)std::lround(k.g * 255); o[2] = (uint8_t)std::lround(k.b * 255); o[3] = (uint8_t)std::lround(k.a * cov * 255);
			}
		return b;
	}

	void Settings::renderQuickBoard(float chipY)
	{
		SPal p = pal(light);
		const int ns = quickSliders(), nt = (int)qRows.size() - ns;
		const QBoard bd(ns, nt);
		const float u = shift.u1;
		auto bx = [&](float v) { return 18 + px(v - QB_X); };
		auto by = [&](float v) { return (v + 14) * u; };
		const Color ink2 = mcol("ink2"), dim = mcol("dim");
		auto stat = [&](const char* k) { auto it = qStats.find(k); return it == qStats.end() ? std::string() : it->second; };
		auto num = [&](const char* k, double& v) { const std::string s = stat(k); if (s.empty()) return false; v = atof(s.c_str()); return true; };

		// the header: the time and the date; the game on the right
		{
			const char* fixed = getenv("BANANA_NOW");
			time_t tt = fixed && *fixed ? (time_t)atoll(fixed) : time(nullptr);
			struct tm tmv;
			localtime_r(&tt, &tmv);
			bool h12 = false;
			for (const Item& it : items) if (it.key == "clock" && it.at >= 0 && it.at < (int)it.values.size()) h12 = it.values[it.at] == "12h";
			char clock[16], date[32];
			strftime(clock, sizeof clock, h12 ? "%I:%M %p" : "%H:%M", &tmv);
			strftime(date, sizeof date, "%a, %b %e", &tmv);
			std::string d = date;
			for (size_t i; (i = d.find("  ")) != std::string::npos;) d.erase(i, 1);
			const float top = 14;
			const float tw = text(clock[0] == '0' && h12 ? clock + 1 : clock, true, 22, 38, top + 14, p.ink);
			textWith(d, IR, "|ir", px(30), 38 + tw + px(24), top + 14 + px(6), ink2);
			if (!gStateName.empty())
			{
				std::string g = gStateName;
				float gw = textWith(g, IR, "|ir", px(30), 0, 0, ink2, false, 0, true);
				while (gw > px(520) && g.size() > 4) { g.pop_back(); while (!g.empty() && ((unsigned char)g.back() & 0xc0) == 0x80) g.pop_back(); gw = textWith(g + "...", IR, "|ir", px(30), 0, 0, ink2, false, 0, true); }
				if (g != gStateName) g += "...";
				textWith(g, IR, "|ir", px(30), 602 - gw, top + 14 + px(6), ink2);
			}
		}

		// the sliders: a slot, the level as a plate in it with the name on it, the percentage and an icon at its end
		for (int i = 0; i < ns; i++)
		{
			const Item& it = items[qRows[i]];
			const bool on = qSel == i;
			const QRect r = bd.slider(i);
			const int n = (int)it.values.size();
			float frac = n > 1 ? (float)std::max(0, it.at) / (n - 1) : 0;
			if (it.kind == Item::BRIGHTNESS) frac = n > 0 ? (float)(std::max(0, it.at) + 1) / n : 0;	// (10% is its lowest)
			const float x = bx(r.x), y = by(r.y), w = px(r.w), h = r.h * u, in = px(8);
			mpaint(on ? "rowsel" : "row", x, y, w, h);
			const float fw = std::max(frac > 0 ? px(56) : 0.0f, (w - 2 * in) * frac);
			if (fw > 0) mpaint("qfill", x + in, y + in, fw, h - 2 * in);
			const std::string label = it.key == "top" ? "Top screen" : it.key == "bottom" ? "Bottom screen" : it.label;
			const std::string value = it.at >= 0 && it.at < (int)it.names.size() ? it.names[it.at] : "";
			const float z = px(32), ty = y + (h - z * 1.2f) / 2, lx = x + in + px(24);
			// (the name in the slot's ink, and over the plate in the plate's)
			textWith(label, IM, "|im", z, lx, ty, p.ink);
			if (fw > 0)
			{
				be->pushClip(X(x + in), X(y), X(fw), X(h));
				textWith(label, IM, "|im", z, lx, ty, mcol("stateInk"));
				be->popClip();
			}
			const float gx = x + w - in - px(24) - px(32);
			const bool over = x + in + fw > gx - px(8);	// (the plate reaches the icon)
			glyph(it.kind == Item::VOLUME ? "speaker" : "sun", 32, gx, y + (h - px(32)) / 2, over ? mcol("stateInk") : on ? p.ink : ink2);
			const float vw = textWith(value, IR, "|ir", px(28), 0, 0, ink2, false, 0, true);
			const float vx = gx - px(20) - vw;
			textWith(value, IR, "|ir", px(28), vx, y + (h - px(28) * 1.2f) / 2, x + in + fw > vx + vw / 2 ? mcol("stateInk") : ink2);
		}

		// the choices: an icon's tile, the name and what it is set to
		for (int i = 0; i < nt; i++)
		{
			const int q = qRows[ns + i];
			const bool on = qSel == ns + i;
			const QRect r = bd.tile(i);
			const float x = bx(r.x), y = by(r.y), w = px(r.w), h = r.h * u;
			std::string label, value, g = "sliders";
			bool lit = true;	// (set to something: its icon on the plate, as the reference's switched-on tiles)
			if (q < 0) { label = "Game mode"; value = QUICK_PERF_NAMES[qPerf]; g = "rocket"; lit = qPerf != 0; if (!qPerf) value = "System's"; }
			else
			{
				const Item& it = items[q];
				value = it.at >= 0 && it.at < (int)it.names.size() ? it.names[it.at] : "";
				const std::string v = it.at >= 0 && it.at < (int)it.values.size() ? it.values[it.at] : "";
				label = it.key == "leds_level" ? "Light level" : it.label;
				g = it.key == "fan" ? "fan" : it.key == "leds" ? "ring" : it.key == "leds_level" ? "sun" : it.key == "leds_effect" ? "motion" : "sliders";
				lit = v != "off" && v != "none";
				if (it.key == "fan")
				{
					double f;
					if (num("fan_pct", f)) value += " \xc2\xb7 " + std::to_string((int)std::lround(f)) + "%";
				}
			}
			mpaint(on ? "rowsel" : "tile", x, y, w, h);
			const float tile = px(64), tx = x + px(20), tyy = y + (h - tile) / 2;
			mpaint(lit ? "iconOn" : "icon", tx, tyy, tile, tile);
			glyph(g, 32, tx + px(16), tyy + px(16), lit ? mcol("plateSub") : mcol("iconInk"));
			const float lx = tx + tile + px(20), room = x + w - px(16) - lx;
			auto cut = [&](std::string s2, const std::shared_ptr<rnds::Font>& f, const char* tag, float z)
			{
				if (textWith(s2, f, tag, z, 0, 0, ink2, false, 0, true) <= room) return s2;
				while (s2.size() > 3 && textWith(s2 + "...", f, tag, z, 0, 0, ink2, false, 0, true) > room) { s2.pop_back(); while (!s2.empty() && ((unsigned char)s2.back() & 0xc0) == 0x80) s2.pop_back(); }
				return s2 + "...";
			};
			textWith(cut(label, IM, "|im", px(28)), IM, "|im", px(28), lx, y + h / 2 - px(36), p.ink);
			textWith(cut(value, IR, "|ir", px(24)), IR, "|ir", px(24), lx, y + h / 2 + px(4), on ? p.ink : ink2);
		}

		// the meters: a ring of how much, the number, and a line of detail
		struct Meter { const char* name; const char* glyph; std::string value, detail; float frac; bool good; };
		std::vector<Meter> ms;
		{
			double v, a, b;
			auto join = [](std::string x, const std::string& y) { if (!y.empty()) x += (x.empty() ? "" : " \xc2\xb7 ") + y; return x; };
			char buf[64];
			{
				Meter m = { "CPU", "chip", "\xe2\x80\x94", "", 0, false };
				if (num("cpu", v)) { m.value = std::to_string((int)std::lround(v)) + "%"; m.frac = (float)v / 100; }
				if (num("cpu_mhz", a)) { snprintf(buf, sizeof buf, "%.2f GHz", a / 1000); m.detail = buf; }
				if (num("cpu_c", b)) m.detail = join(m.detail, std::to_string((int)std::lround(b)) + " \xc2\xb0""C");
				ms.push_back(m);
			}
			{
				Meter m = { "GPU", "gpu", "\xe2\x80\x94", "", 0, false };
				if (num("gpu", v)) { m.value = std::to_string((int)std::lround(v)) + "%"; m.frac = (float)v / 100; }
				if (num("gpu_mhz", a)) { m.detail = std::to_string((int)std::lround(a)) + " MHz"; if (m.value.size() == 3 && m.frac == 0 && !num("gpu", v)) m.value = m.detail, m.detail.clear(); }
				if (num("gpu_c", b)) m.detail = join(m.detail, std::to_string((int)std::lround(b)) + " \xc2\xb0""C");
				ms.push_back(m);
			}
			{
				Meter m = { "Memory", "memory", "\xe2\x80\x94", "", 0, false };
				if (num("mem_used_mb", v)) { snprintf(buf, sizeof buf, v >= 1000 ? "%.1f GB" : "%.0f MB", v >= 1000 ? v / 1024 : v); m.value = buf; }
				if (num("mem_total_mb", a)) { snprintf(buf, sizeof buf, "of %.0f GB", a / 1024); m.detail = buf; }
				if (num("mem_pct", b)) { m.frac = (float)b / 100; m.detail = join(m.detail, std::to_string((int)std::lround(b)) + "% used"); }
				ms.push_back(m);
			}
			{
				Meter m = { "Power", "bolt", "\xe2\x80\x94", "", 0, false };
				const std::string st = stat("power_state");
				if (num("power_mw", v)) { snprintf(buf, sizeof buf, "%+.1f W", v / 1000); m.value = buf; }
				m.detail = st;
				if (num("battery", b)) { m.frac = (float)b / 100; m.detail = join(m.detail, std::to_string((int)std::lround(b)) + "%"); m.good = st == "Charging" || st == "Full"; }
				ms.push_back(m);
			}
		}
		const float room = 968 - 24;	// (what is left above the hints, design px)
		for (int i = 0; i < (int)ms.size(); i++)
		{
			const QRect r = bd.meter(i);
			if (r.y + r.h > room + 1) break;
			const Meter& m = ms[i];
			const float x = bx(r.x), y = by(r.y), w = px(r.w), h = r.h * u;
			mpaint("tile", x, y, w, h);
			// the ring, made for the screen's own pixels and kept by how full it is
			const float rs = px(88), rx = x + px(20), ry = y + (h - rs) / 2;
			const Color arc = m.good ? mcol("ok") : mcol("stateMid"), track = mcol("raised2");
			const int dw = std::max(8, (int)std::lround(X(rs) * shift.rk())), pc = (int)std::lround(std::max(0.0f, std::min(1.0f, m.frac)) * 100);
			char key[96];
			snprintf(key, sizeof key, "qring:%d:%d:%02x%02x%02x:%02x%02x%02x", dw, pc, (int)(arc.r * 255), (int)(arc.g * 255), (int)(arc.b * 255), (int)(track.r * 255), (int)(track.g * 255), (int)(track.b * 255));
			auto it = cache.find(key);
			if (it == cache.end())
			{
				const rnds::Bitmap b = quickRing(dw, std::max(2.0f, dw * 8.0f / 88), pc / 100.0f, track, arc);
				Cached c;
				c.tex = be->upload(b, true);
				it = put(key, c, b);
			}
			be->draw(keep(it).tex, X(rx), X(ry), X(rs), X(rs), Color(1, 1, 1));
			glyph(m.glyph, 32, rx + (rs - px(32)) / 2, ry + (rs - px(32)) / 2, m.good ? mcol("ok") : ink2);
			const float lx = rx + rs + px(24);
			textWith(m.name, IR, "|ir", px(22), lx, y + px(16), ink2);
			textWith(m.value, IM, "|im", px(40), lx, y + px(40), p.ink);
			textWith(m.detail, IR, "|ir", px(22), lx, y + h - px(16) - px(22) * 1.2f, dim);
		}
		float bw = faceWidth("B", "Close");
		face("B", "Close", 18, bw, chipY);
		float cw = faceWidth("A", "Change");
		face("A", "Change", 622 - cw, cw, chipY);
	}
}
