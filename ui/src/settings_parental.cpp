// banana-ui: System > Parental controls' pages (parental.h): the PIN's keypad and the Allowed games
#include "settings.h"
#include "settings_draw.h"
#include "recorder.h"

#include <algorithm>
#include <ctime>

namespace banana
{
	using rnds::Color;

	// ---- the PIN's keypad ---------------------------------------------------------------------------------------------
	// 1 2 3 / 4 5 6 / 7 8 9 / Delete 0 OK, as a phone's; the d-pad moves, A presses, a tap presses. 4 to 8 digits.
	static const int PIN_MIN = 4, PIN_MAX = 8, PIN_TRIES = 5, PIN_WAIT = 30;
	static const float KX = 83, KY = 140, KW = 150, KH = 58, KG = 12, KGY = 10;
	static int padKey(int cell) { return cell < 9 ? cell + 1 : cell == 9 ? 10 : cell == 10 ? 0 : 11; }	// a cell's key

	void Settings::openPin(PinMode m)
	{
		pinMode = m;
		pinTyped.clear();
		pinNote.clear();
		if (m == PIN_NEW) pinFirst.clear();
		pinSel = 0;
		page = PIN;
	}

	Settings::Result Settings::pinKey(int key)
	{
		if (key == 10) { if (!pinTyped.empty()) pinTyped.pop_back(); return MOVED; }
		if (key < 10)
		{
			if ((int)pinTyped.size() < PIN_MAX) pinTyped += (char)('0' + key);
			pinNote.clear();
			return MOVED;
		}
		// OK
		if ((int)pinTyped.size() < PIN_MIN) { pinNote = "At least 4 digits."; return MOVED; }
		if (pinMode == PIN_UNLOCK)
		{
			if (time(nullptr) < pinWaitUntil) { pinTyped.clear(); return MOVED; }
			if (parental::checkPin(data, pinTyped))
			{
				pinFails = 0;
				pinTyped.clear();
				page = LIST;
				recordEvent("settings", "parental controls: unlocked");
				return MOVED;
			}
			pinTyped.clear();
			if (++pinFails >= PIN_TRIES) { pinFails = 0; pinWaitUntil = time(nullptr) + PIN_WAIT; pinNote.clear(); }
			else pinNote = "That's not the PIN.";
			recordEvent("settings", "parental controls: a wrong PIN");
			return MOVED;
		}
		if (pinMode == PIN_NEW) { pinFirst = pinTyped; pinTyped.clear(); pinMode = PIN_AGAIN; pinNote.clear(); return MOVED; }
		// PIN_AGAIN
		if (pinTyped != pinFirst) { pinMode = PIN_NEW; pinTyped.clear(); pinFirst.clear(); pinNote = "The two didn't match. Choose it again."; return MOVED; }
		bool saved = parental::setPin(data, pinTyped);
		pinTyped.clear(); pinFirst.clear();
		page = LIST;
		if (!saved) { toast("The PIN couldn't be saved", "Is the card full or read-only?", 0xff5a4f, 2, 5000); pinThenOn = false; return MOVED; }
		if (pinThenOn)
		{
			// turned on now that there is a PIN: the row, and the home row read again with only the allowed games
			pinThenOn = false;
			for (auto& it : items)
				if (it.key == "parental") { it.at = 1; save(it); }
			toast("Parental controls on", "Choose the games in Allowed games.", 0x6ad07a, 1, 5000);
			return SHELVES;
		}
		toast("PIN changed", "", 0x6ad07a, 1, 3000);
		return MOVED;
	}

	Settings::Result Settings::pressPin(Key k)
	{
		int r = pinSel / 3, c = pinSel % 3;
		if (k == Key::UP) r = (r + 3) % 4;
		else if (k == Key::DOWN) r = (r + 1) % 4;
		else if (k == Key::LEFT) c = (c + 2) % 3;
		else if (k == Key::RIGHT) c = (c + 1) % 3;
		else if (k == Key::A) return pinKey(padKey(pinSel));
		else if (k == Key::B)
		{
			if (!pinTyped.empty()) { pinTyped.pop_back(); return MOVED; }
			pinThenOn = false;
			// locked: Settings closes (nothing of it is shown without the PIN); else back to the controls' list
			if (pinMode == PIN_UNLOCK) { page = LIST; mOpen = false; return CLOSED; }
			page = LIST;
			return MOVED;
		}
		else if (k == Key::START)
		{
			pinThenOn = false;
			page = LIST; mOpen = false;
			return CLOSED;
		}
		else return NONE;
		pinSel = r * 3 + c;
		return MOVED;
	}

	Settings::Result Settings::touchPin(float cx, float cy)
	{
		if (downY >= 418 && downY <= 466 && downX < 320) return pressPin(Key::B);
		for (int i = 0; i < 12; i++)
		{
			float x = KX + (i % 3) * (KW + KG), y = KY + (i / 3) * (KH + KGY);
			if (cx >= x && cx < x + KW && cy >= y && cy < y + KH && downX >= x && downX < x + KW && downY >= y && downY < y + KH)
			{
				pinSel = i;
				return pinKey(padKey(i));
			}
		}
		return NONE;
	}

	void Settings::renderPin()
	{
		SPal p = pal(light);
		long wait = (long)(pinWaitUntil - time(nullptr));
		std::string prompt = pinMode == PIN_UNLOCK ? "Enter the PIN" : pinMode == PIN_NEW ? "Choose a PIN" : "The PIN again";
		std::string help = pinMode == PIN_UNLOCK
			? "Settings are locked by Parental controls. Enter the PIN to change them; closing Settings locks them again."
			: pinMode == PIN_NEW ? "4 to 8 digits. It unlocks Settings while Parental controls are on. Keep it where the children won't find it."
			: "Once more, to be sure it's the one you meant.";
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon("gear", 50, 56, 3);
		text("Parental controls", true, 17, 170, 62, p.muted, true, 1.4f);
		text(prompt, true, 30, 170, 88, p.ink);
		if (pinMode == PIN_UNLOCK && wait > 0)
			text("Too many tries: wait " + std::to_string(wait) + " s", true, 22, 170, 130, p.accent);
		else if (!pinNote.empty()) text(fit(pinNote, true, 22, 420), true, 22, 170, 130, p.accent);
		wrapped(help, 20, 50, 196, 540, 6, p.muted);
		be->endScreen();

		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("bubble", false, 18, 14, 604, 54);
		text(prompt, true, 22, 38, 28, p.ink);
		// the digits typed, as dots, centred
		std::string dots;
		for (size_t i = 0; i < pinTyped.size(); i++) dots += i ? " *" : "*";
		if (dots.empty()) dots = "-";
		float dw = text(dots, true, 30, 0, 0, p.ink, false, 0, true);
		text(dots, true, 30, 320 - dw / 2, 86, pinTyped.empty() ? p.muted : p.ink);
		for (int i = 0; i < 12; i++)
		{
			float x = KX + (i % 3) * (KW + KG), y = KY + (i / 3) * (KH + KGY);
			bool on = i == pinSel;
			box(on ? "pick" : "slot", on, x, y, KW, KH);
			int key = padKey(i);
			std::string l = key < 10 ? std::to_string(key) : key == 10 ? "Delete" : "OK";
			float sz = key < 10 ? 28 : 20;
			float lw = text(l, true, sz, 0, 0, p.ink, false, 0, true);
			text(l, true, sz, x + (KW - lw) / 2, y + (KH - sz) / 2 - 2, on ? p.onPick : key == 11 ? p.accent : p.ink);
		}
		float bw = faceWidth("B", "Back");
		face("B", "Back", 18, bw);
		float aw = faceWidth("A", "Press");
		face("A", "Press", 622 - aw, aw);
		be->endScreen();
	}

	// ---- Allowed games --------------------------------------------------------------------------------------------------
	void Settings::openKidGames()
	{
		kidGames.clear(); kidSys.clear();
		std::set<std::string> allowed = parental::games(data);
		if (allGames)
			for (auto& g : allGames())
			{
				kidGames.push_back({ g.system, g.key, g.title, allowed.count(g.key) > 0 });
				if (std::find(kidSys.begin(), kidSys.end(), g.system) == kidSys.end()) kidSys.push_back(g.system);
			}
		kidLevel = 0; kidSysSel = 0; kidRow = 0; kidSysRow = 0; kidTop = 0;
		kidDirty = false;
		page = KIDGAMES;
	}

	std::vector<int> Settings::kidList() const
	{
		std::vector<int> v;
		if (kidSysSel < 0 || kidSysSel >= (int)kidSys.size()) return v;
		for (size_t i = 0; i < kidGames.size(); i++) if (kidGames[i].sys == kidSys[kidSysSel]) v.push_back((int)i);
		return v;
	}

	// the lists' rows: "Select all" and "Deselect all" first (every game on the systems' list, the system's on a system's),
	// then the systems or the games. kidRow is the row picked on either list (kidSysRow keeps the systems' place)
	static const int KID_VIS = 8, KID_FIRST = 2;

	Settings::Result Settings::kidSet(bool on)
	{
		std::vector<int> v;
		if (kidLevel == 0) for (size_t i = 0; i < kidGames.size(); i++) v.push_back((int)i);
		else v = kidList();
		for (int i : v) kidGames[i].on = on;
		kidDirty = kidDirty || !v.empty();
		return MOVED;
	}

	Settings::Result Settings::pressKidGames(Key k)
	{
		auto leave = [&](bool close) -> Result
		{
			bool was = kidDirty;
			if (kidDirty)
			{
				// what was allowed and isn't on the card now (its game deleted, the second card out) stays allowed
				std::set<std::string> keep = parental::games(data);
				for (auto& g : kidGames) { if (g.on) keep.insert(g.key); else keep.erase(g.key); }
				if (!parental::saveGames(data, keep)) toast("The games couldn't be saved", "Is the card full or read-only?", 0xff5a4f, 2, 5000);
				kidDirty = false;
			}
			page = LIST;
			if (close) mOpen = false;
			if (was && parental::on(data)) return SHELVES;	// (the home row with the new choice; it closes Settings too when asked)
			return close ? CLOSED : MOVED;
		};
		if (k == Key::START) return leave(true);
		if (k == Key::B)
		{
			if (kidLevel == 1) { kidLevel = 0; kidRow = kidSysRow; kidTop = std::max(0, kidRow - KID_VIS + 1); return MOVED; }
			return leave(false);
		}
		if (kidGames.empty()) return NONE;
		int n = KID_FIRST + (kidLevel == 0 ? (int)kidSys.size() : (int)kidList().size());
		int to = kidRow;
		if (k == Key::UP) to = std::max(0, kidRow - 1);
		else if (k == Key::DOWN) to = std::min(n - 1, kidRow + 1);
		else if (k == Key::L || k == Key::LEFT) to = std::max(0, kidRow - (KID_VIS - 1));
		else if (k == Key::R || k == Key::RIGHT) to = std::min(n - 1, kidRow + (KID_VIS - 1));
		else if (k == Key::A)
		{
			if (kidRow < KID_FIRST) return kidSet(kidRow == 0);
			if (kidLevel == 0) { kidSysRow = kidRow; kidSysSel = kidRow - KID_FIRST; kidLevel = 1; kidRow = 0; kidTop = 0; return MOVED; }
			auto v = kidList();
			int g = kidRow - KID_FIRST;
			if (g < (int)v.size()) { kidGames[v[g]].on = !kidGames[v[g]].on; kidDirty = true; }
			return MOVED;
		}
		else if (k == Key::X && kidLevel == 0 && kidRow >= KID_FIRST)
		{
			// a whole system: all of its games, or (all allowed already) none
			kidSysSel = kidRow - KID_FIRST;
			kidLevel = 1;
			auto v = kidList();
			kidLevel = 0;
			bool all = true;
			for (int i : v) all = all && kidGames[i].on;
			for (int i : v) kidGames[i].on = !all;
			kidDirty = kidDirty || !v.empty();
			return MOVED;
		}
		else return NONE;
		if (to == kidRow) return NONE;
		kidRow = to;
		if (kidRow < kidTop) kidTop = kidRow;
		if (kidRow >= kidTop + KID_VIS) kidTop = kidRow - KID_VIS + 1;
		return MOVED;
	}

	Settings::Result Settings::touchKidGames(float cx, float cy)
	{
		if (downY >= 418 && downY <= 466 && downX < 320) return pressKidGames(Key::B);
		// a row: picked, or (picked already) pressed, as A
		int r = (int)((cy - 80) / 41), rd = (int)((downY - 80) / 41);
		if (cy < 80 || r != rd || r < 0 || r >= KID_VIS) return NONE;
		int n = KID_FIRST + (kidLevel == 0 ? (int)kidSys.size() : (int)kidList().size());
		int row = kidTop + r;
		if (row >= n) return NONE;
		if (row != kidRow) { kidRow = row; return MOVED; }
		return pressKidGames(Key::A);
	}

	void Settings::renderKidGames()
	{
		SPal p = pal(light);
		int chosen = 0;
		for (auto& g : kidGames) chosen += g.on;
		const bool sysList = kidLevel == 0;
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon("gear", 50, 56, 3);
		text("Parental controls", true, 17, 170, 62, p.muted, true, 1.4f);
		text("Allowed games", true, 30, 170, 88, p.ink);
		text(fit(std::to_string(chosen) + (chosen == 1 ? " game" : " games") + " allowed", true, 22, 420), true, 22, 170, 130, p.accent);
		std::string help = kidGames.empty() ? "No games on the card yet."
			: kidRow == 0 ? (sysList ? "Allows every game on the card." : "Allows every game of this system.")
			: kidRow == 1 ? (sysList ? "Hides every game: nothing to play until some are allowed again." : "Hides every game of this system.")
			: sysList ? "The games shown while Parental controls are on. A opens a system to pick its games one by one; X allows all of them, or none when all are. B saves."
			: "A allows a game or hides it. L and R jump a page. B goes back to the systems.";
		wrapped(help, 20, 50, 196, 540, 6, p.muted);
		if (!sysList && kidRow >= KID_FIRST)
		{
			auto v = kidList();
			int g = kidRow - KID_FIRST;
			if (g < (int)v.size())
			{
				const KidGame& kg = kidGames[v[g]];
				text(fit(kg.title, true, 24, 540), true, 24, 50, 340, p.ink);
				text(kg.on ? "Allowed" : "Hidden", false, 20, 50, 378, kg.on ? p.accent : p.muted);
			}
		}
		be->endScreen();

		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("bubble", false, 18, 14, 604, 54);
		std::string head = sysList || kidSys.empty() ? std::string("Systems") : kidSys[kidSysSel];
		text(fit(head, true, 22, 420), true, 22, 38, 28, p.ink);
		auto v = kidList();
		int n = KID_FIRST + (sysList ? (int)kidSys.size() : (int)v.size());
		if (kidGames.empty()) n = 0;
		for (int r = 0; r < KID_VIS && kidTop + r < n; r++)
		{
			int row = kidTop + r;
			bool on = row == kidRow;
			float y = 80 + r * 41;
			box(on ? "pick" : "slot", on, 18, y, 604, 37);
			Color c = on ? p.onPick : p.ink, acc = on ? p.onPickAccent : p.accent;
			if (row < KID_FIRST)
			{
				text(row == 0 ? "Select all" : "Deselect all", true, 18, 74, y + 9, row == 0 ? acc : c);
				continue;
			}
			// the box: [x] all allowed, [-] some, [ ] none
			int all = 0, ch = 0;
			std::string name;
			if (sysList)
			{
				name = kidSys[row - KID_FIRST];
				for (auto& g : kidGames) if (g.sys == name) { all++; ch += g.on; }
			}
			else { const KidGame& g = kidGames[v[row - KID_FIRST]]; name = g.title; all = 1; ch = g.on; }
			text(ch == 0 ? "[ ]" : ch == all ? "[x]" : "[-]", true, 18, 34, y + 9, ch ? acc : c);
			float tw = 0;
			if (sysList)
			{
				std::string tag = std::to_string(ch) + " of " + std::to_string(all);
				tw = text(tag, true, 17, 0, 0, c, false, 0, true);
				text(tag, true, 17, 606 - tw, y + 10, on ? p.onPick : p.muted);
			}
			text(fit(name, true, 18, 520 - tw - (tw > 0 ? 24 : 0)), true, 18, 74, y + 9, c);
		}
		if (!sysList)
		{
			std::string count = std::to_string(kidRow < KID_FIRST ? 0 : kidRow - KID_FIRST + 1) + " / " + std::to_string(v.size());
			float cw = text(count, true, 17, 0, 0, p.muted, false, 0, true);
			text(count, true, 17, 602 - cw, 31, p.muted);
		}
		float bw = faceWidth("B", "Back");
		face("B", "Back", 18, bw);
		if (!kidGames.empty())
		{
			std::string aa = kidRow < KID_FIRST ? (kidRow == 0 ? "Select all" : "Deselect all") : sysList ? "Open" : "Allow / hide";
			float aw = faceWidth("A", aa);
			if (sysList && kidRow >= KID_FIRST)
			{
				const std::string xa = "All / none";
				float xw = faceWidth("X", xa);
				face("X", xa, 320 - xw / 2, xw);
			}
			face("A", aa, 622 - aw, aw);
		}
		be->endScreen();
	}
}
