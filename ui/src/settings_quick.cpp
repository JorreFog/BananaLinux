// banana-ui: the quick menu (the AYN Thor's Home button held for 3 s in a game: main.cpp freezes the game and gives
// the bottom screen to the menu). One page on the bottom screen: the two screens' brightness and the volume as sliders,
// the running game's performance mode, and the handheld's fan and stick lights where it has them. Left and right (or a
// tap) change a row and it takes effect at once, through the same rows and tools as Settings; B closes it (and Home,
// main.cpp's). The top screen isn't drawn: it keeps the frozen game.
#include "settings.h"
#include "settings_draw.h"
#include <algorithm>
#include <cmath>

namespace banana
{
	using rnds::Color;

	// a game's performance mode (as its game page's Performance row: per-game-perf.conf, banana-perf's profiles)
	static const char* const QUICK_PERF_VALUES[] = { "", "powersave", "balanced", "steady", "performance" };
	static const char* const QUICK_PERF_NAMES[] = { "As the system's", "Battery saver", "Balanced", "Steady", "Performance" };

	// the rows' place on the bottom panel (CSS px of the 640x480 canvas): big enough for a thumb
	static constexpr float Q_SLIDER_X = 300, Q_SLIDER_W = 236;

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
}
