// banana-ui: the Achievements page - a game's whole RetroAchievements list (X on a game > Achievements), and the list's
// pieces the game panel's own list shares (settings_gamepanel.cpp). Read-only: the loader (achievements.h) identifies the
// game, asks the server for the list and the account's unlocks on its threads, and keeps them on the card; the page
// draws its snapshots. Top panel: the game, its progress, the achievement picked in full (badge, title, how and when it
// was unlocked, its whole description); bottom panel: the list in the settings' rows, five at a time.
#include "settings.h"
#include "settings_draw.h"
#include "lang.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

using rnds::Color;

namespace banana
{
	namespace
	{
		// a translated line with its {name} placeholders filled in
		std::string fill(std::string t, const std::vector<std::pair<std::string, std::string>>& v)
		{
			for (auto& kv : v)
				if (size_t at = t.find(kv.first); at != std::string::npos) t.replace(at, kv.first.size(), kv.second);
			return t;
		}
		const float ACH_Y = 82, ACH_PITCH = 64, ACH_H = 56;	// the page's rows (the settings list's)
		const char* const SEP = "  \xc2\xb7  ";
		// an unlocked one's badge until its picture is there: a star in the accent colour (a locked one: the lock)
		const char* const STAR[9] = { "....#....", "...###...", "...###...", "#########", ".#######.", "..#####..", "..##.##..", ".##...##.", "##.....##" };
	}

	std::string Settings::achPoints(unsigned n)
	{
		return n == 1 ? lang::tr("1 point") : fill(lang::tr("{n} points"), { { "{n}", std::to_string(n) } });
	}

	void Settings::achPlaceholder(const SPal& p, bool got, float x, float y, float size)
	{
		box("bubble", false, x, y, size, size);
		if (!got) { lock(x + size / 2 - 6, y + size / 2 - 9, p.muted); return; }
		const int k = std::max(1, (int)std::lround(size / 16 * s));
		const float w = 9.0f * k / s;
		sprite(STAR, 9, x + (size - w) / 2, y + (size - w) / 2, k, [&](char) { return p.accent; });
	}

	// ---- the pieces ---------------------------------------------------------------------------------------------
	std::string Settings::achDate(long long t)
	{
		if (t <= ach::UNKNOWN) return "";
		time_t tt = (time_t)t;
		struct tm a;
		localtime_r(&tt, &a);
		char b[16];
		strftime(b, sizeof b, "%Y-%m-%d", &a);	// (numbers: the same in every language)
		return b;
	}

	std::string Settings::achCounts(const ach::List& l)
	{
		return fill(lang::tr("{n} of {m} unlocked"), { { "{n}", std::to_string(l.unlocked()) }, { "{m}", std::to_string(l.items.size()) } }) + SEP +
			fill(lang::tr("{n} of {m} points"), { { "{n}", std::to_string(l.pointsEarned()) }, { "{m}", std::to_string(l.points()) } });
	}

	std::string Settings::achStatus(const ach::Item& it)
	{
		if (!it.unlocked()) return lang::tr("Locked");
		const std::string d = achDate(it.when());
		return (d.empty() ? lang::tr("Unlocked") : fill(lang::tr("Unlocked {date}"), { { "{date}", d } })) + SEP + lang::tr(it.hard ? "Hardcore" : "Softcore");
	}

	std::string Settings::achAsOf(const ach::List& l)
	{
		const std::string d = achDate(l.fetched);
		return d.empty() ? lang::tr("Offline: the list saved on the card") : fill(lang::tr("Offline: as of {date}"), { { "{date}", d } });
	}

	std::string Settings::achStateText(ach::Loader::State st)
	{
		switch (st)
		{
		case ach::Loader::LOADING: return lang::tr("Loading the list from RetroAchievements...");
		case ach::Loader::IDENTIFYING: return lang::tr("Identifying the game: reading its file once. A big game takes a while.");
		case ach::Loader::NO_ACCOUNT: return lang::tr("No RetroAchievements account on this handheld. Add one in Settings > System > First-time setup, at its RetroAchievements step.");
		case ach::Loader::NO_CONNECTION: return lang::tr("No connection to RetroAchievements, and no list saved for this game yet. Check Wi-Fi and open the page again.");
		case ach::Loader::NO_SYSTEM: return lang::tr("No achievements for this system.");
		case ach::Loader::NO_GAME: return lang::tr("RetroAchievements has no achievements for this game, or doesn't know this version of it.");
		case ach::Loader::NO_FILE: return lang::tr("This game's file couldn't be read to identify it.");
		case ach::Loader::LOGIN_FAILED: return lang::tr("The RetroAchievements login failed: check the account in Settings > System > First-time setup.");
		default: return "";
		}
	}

	rnds::TexPtr Settings::achBadge(const std::string& file, std::map<std::string, rnds::TexPtr>& texs)
	{
		auto it = texs.find(file);
		if (it != texs.end()) return it->second;
		rnds::Bitmap b = rnds::decodeFile(file);
		rnds::TexPtr t = b.empty() ? nullptr : be->upload(b, true);
		if (t) texs[file] = t;
		return t;
	}

	// a row: the badge (the locked one greyed, the server's _lock picture), the title and the description (descLines
	// lines, the last cut with "..."), the points and the status on the right. Drawn as the settings' rows are: "pick"
	// for the one picked, "slot" for the rest; a locked one's words muted
	void Settings::achRow(const SPal& p, const ach::Item& it, float y, float h, int descLines, bool on, const rnds::TexPtr& badge)
	{
		AsNamed named(*this);	// (the server's words as they are; the page's own were translated as they were made)
		box(on ? "pick" : "slot", on, 18, y, 604, h);
		const bool got = it.unlocked();
		const Color ink = on ? p.onPick : got ? p.ink : p.muted, sub = on ? p.onPick : p.muted, mark = on ? p.onPickAccent : p.accent;
		const float bs = std::min(48.0f, h - 12);
		const float by = y + (h - bs) / 2;
		if (badge) be->draw(badge, std::round(X(28)), std::round(X(by)), std::round(X(bs)), std::round(X(bs)), Color(1, 1, 1, got ? 1.0f : 0.85f));
		else achPlaceholder(p, got, 28, by, bs);
		const float tx = 28 + bs + 12;
		// the right column: the points, and under them the date or Locked
		const std::string pts = achPoints(it.points);
		const float pw = text(pts, true, 17, 0, 0, mark, false, 0, true);
		text(pts, true, 17, 606 - pw, y + 7, got ? mark : sub);
		const std::string st = got ? (achDate(it.when()).empty() ? lang::tr(it.hard ? "Hardcore" : "Softcore") : achDate(it.when())) : lang::tr("Locked");
		const float sw = text(st, false, 14, 0, 0, sub, false, 0, true);
		text(st, false, 14, 606 - sw, y + h - 24, sub);
		text(fit(it.title, true, 19, 606 - pw - 14 - tx), true, 19, tx, y + 6, ink);
		const float dw = 606 - sw - 14 - tx;
		if (!it.desc.empty())
		{
			std::string d = it.desc;
			std::replace(d.begin(), d.end(), '\n', ' ');
			std::replace(d.begin(), d.end(), '\t', ' ');
			if (descLines <= 1) text(fit(d, false, 14, dw), false, 14, tx, y + 31, sub);
			else wrapped(d, 13, tx, y + 29, dw, descLines, sub);
		}
	}

	// ---- the page -----------------------------------------------------------------------------------------------
	void Settings::openAchievements(const std::string& title, const std::string& path, const std::string& system)
	{
		std::string rom = path.substr(path.rfind('/') + 1);
		openGame(title, rom, system == "nds", "", "", "", path, "", system);
		openAch();
	}

	void Settings::openAch()
	{
		ach::Loader::Job j;
		j.rom = pgPath;
		j.system = pgSystem;
		j.cacheDir = achCacheDir();
		j.badgeDir = achBadgeDir();
		// the account as the game panel and the emulators have it: accounts.conf's (the setup's), RetroArch's token for it
		// (ra-secrets keeps it there), else retroarch.cfg's own; the password when there is no token yet (a login, read-only)
		j.account.user = account("ra_user");
		j.account.token = account("ra_retroarch_token");
		j.account.password = account("ra_password");
		const char* rc = getenv("BANANA_RA_CFG");	// (tests: a retroarch.cfg of their own; none here)
		const std::string cfg = rc ? rc : "/data/retroarch/retroarch.cfg";
		if (j.account.user.empty()) j.account.user = getConfValue(cfg, "cheevos_username", "");
		if (j.account.token.empty() && getConfValue(cfg, "cheevos_username", "") == j.account.user) j.account.token = getConfValue(cfg, "cheevos_token", "");
		auto unq = [](std::string v) { if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2); return v; };
		j.account.user = unq(j.account.user); j.account.token = unq(j.account.token);
		j.network = wifiOn();
		if (achLoader) achLoader->stop();
		achLoader = ach::Loader::start(j);
		achSel = achTop = 0;
		achSeen = -1;
		achTex.clear();
		message.clear();
		page = ACHIEVEMENTS;
		if (log) log("achievements: " + pgSystem + " " + pgPath);
	}

	bool Settings::achWorking() const { return achLoader && (achLoader->busy() || achLoader->fetchingBadges()); }

	std::string Settings::achState() const
	{
		if (!mOpen || page != ACHIEVEMENTS || !achLoader) return "closed";
		auto sn = achLoader->snapshot();
		if (sn.state != ach::Loader::READY) return ach::stateName(sn.state);
		char b[64];
		snprintf(b, sizeof b, "ready %zu %d %s", sn.list.items.size(), sn.list.unlocked(), sn.offline ? "offline" : "online");
		return b;
	}

	Settings::Result Settings::pressAch(Key k)
	{
		const int n = achLoader ? (int)achLoader->snapshot().list.items.size() : 0;
		auto keep = [&] { if (achSel < achTop) achTop = achSel; if (achSel >= achTop + ROWS) achTop = achSel - ROWS + 1; };
		switch (k)
		{
		case Key::UP: case Key::DOWN:
			if (n < 2) return NONE;
			achSel = (achSel + (k == Key::UP ? n - 1 : 1)) % n;
			keep();
			return MOVED;
		case Key::L: case Key::R: case Key::LEFT: case Key::RIGHT:
		{
			if (n < 2) return NONE;
			const int d = k == Key::L || k == Key::LEFT ? -ROWS : ROWS;
			const int to = std::max(0, std::min(n - 1, achSel + d));
			if (to == achSel) return NONE;
			achSel = to;
			achTop = std::max(0, std::min(std::max(0, n - ROWS), achTop + d));
			keep();
			return MOVED;
		}
		case Key::B: case Key::A: case Key::START:
			if (k == Key::A) return NONE;	// (nothing to open: the top panel has the whole achievement)
			if (achLoader) achLoader->stop();
			achLoader.reset();
			achTex.clear();
			page = GAME;
			// the game page's row again: its count from what the page just kept
			if (ach::consoleFor(pgSystem))
			{
				std::string h; uint32_t id = 0; ach::List l;
				if (ach::readId(achCacheDir(), ach::romKey(pgPath), h, id) && id && ach::readList(achCacheDir(), id, l) && !l.items.empty())
					pgAchValue = std::to_string(l.unlocked()) + " / " + std::to_string(l.items.size());
			}
			return MOVED;
		default: return NONE;
		}
	}

	std::vector<Settings::Chip> Settings::achChips(int n)
	{
		std::vector<Chip> cs = { { Key::B, "B", lang::tr("Back") } };
		if (n > ROWS) cs.push_back({ Key::R, "L / R", lang::tr("Page"), true });
		return cs;
	}

	Settings::Result Settings::touchAch(float cx, float cy)
	{
		const int n = achLoader ? (int)achLoader->snapshot().list.items.size() : 0;
		if (downY >= 418 && downY <= 466)
		{
			// (the chips: Back, and the page on: the list's next five, from the last back to the first)
			const Key k = chipAt(achChips(n), downX);
			if (k != Key::R) return pressAch(Key::B);
			if (achSel >= n - 1) { achSel = achTop = 0; return MOVED; }
			return pressAch(Key::R);
		}
		if (downY < ACH_Y || downY >= ACH_Y + ROWS * ACH_PITCH || !n) return NONE;
		// a drag scrolls (a row a 64 px); a tap picks the row
		const float dy = downY - cy;
		if (std::fabs(dy) > 32)
		{
			const int rows = (int)std::lround(dy / ACH_PITCH);
			achTop = std::max(0, std::min(std::max(0, n - ROWS), achTop + rows));
			achSel = std::max(achTop, std::min(achTop + ROWS - 1, std::min(n - 1, achSel)));
			return MOVED;
		}
		const int r = achTop + (int)((downY - ACH_Y) / ACH_PITCH);
		if (r >= n || downY - ACH_Y - (r - achTop) * ACH_PITCH > ACH_H || r == achSel) return NONE;
		achSel = r;
		return MOVED;
	}

	void Settings::renderAch()
	{
		SPal p = pal(light);
		ach::Loader::Snapshot sn;
		if (achLoader) { sn = achLoader->snapshot(); achSeen = sn.gen; }
		const ach::List& l = sn.list;
		const int n = (int)l.items.size();
		const bool ready = sn.state == ach::Loader::READY && n;
		if (achSel >= n) achSel = std::max(0, n - 1);
		achTop = std::max(0, std::min(achTop, std::max(0, n - ROWS)));
		AsNamed named(*this);	// (each line below was translated as it was made, or is the server's / the game's)

		// ---- top: the game, its progress, the achievement picked in full
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon(pgNds ? "nds" : "gamepad", 50, HEAD_ICON_Y, 3);
		std::string head = lang::tr("Achievements");
		if (ready && sn.offline) head += SEP + achAsOf(l);
		textFit(head, true, 17, 170, HEAD_SECTION_Y, 420, p.muted, false);
		textFit(pgTitle, true, 30, 170, HEAD_LABEL_Y, 420, p.ink, true);
		if (!ready)
		{
			const bool waiting = sn.state == ach::Loader::LOADING || sn.state == ach::Loader::IDENTIFYING;
			textFit(waiting ? lang::tr("Loading...") : lang::tr("No list"), true, 22, 170, HEAD_VALUE_Y, 420, p.accent, false);
			helpText(achStateText(sn.state), HELP_TOP, HELP_BOTTOM, p.muted);
		}
		else
		{
			textFit(achCounts(l), true, 20, 170, HEAD_VALUE_Y, 420, p.accent, false);
			// the progress bar (the game panel's)
			const float bx = 50, bw = 540, by = 148;
			be->fill(X(bx), X(by), X(bw), X(10), p.line);
			if (l.points()) be->fill(X(bx + 2), X(by + 2), X((bw - 4) * l.pointsEarned() / l.points()), X(6), p.accent);
			else be->fill(X(bx + 2), X(by + 2), X((bw - 4) * l.unlocked() / n), X(6), p.accent);
			const ach::Item& it = l.items[achSel];
			const bool got = it.unlocked();
			const std::string bname = got ? it.badge : it.badge + "_lock";
			rnds::TexPtr tex = achLoader->badge(bname) ? achBadge(achBadgeDir() + "/" + bname + ".png", achTex) : nullptr;
			const float y0 = 176;
			if (tex) be->draw(tex, std::round(X(50)), std::round(X(y0)), std::round(X(64)), std::round(X(64)), Color(1, 1, 1, 1));
			else achPlaceholder(p, got, 50, y0, 64);
			textFit(it.title, true, 22, 132, y0 + 2, 458, p.ink, true);
			textFit(achStatus(it), true, 17, 132, y0 + 34, 458, got ? p.accent : p.muted, false);
			const std::string pts = achPoints(it.points);
			text(pts, false, 15, 132, y0 + 58, p.muted);
			std::string d = it.desc.empty() ? lang::tr("No description.") : it.desc;
			helpText(d, y0 + 92, HELP_BOTTOM, p.ink);
		}
		be->endScreen();

		// ---- bottom: the list
		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("bubble", false, 18, 14, 604, 54);
		text(lang::tr("Achievements"), true, 22, 38, 28, p.ink);
		if (ready)
		{
			const std::string pos = std::to_string(achSel + 1) + " / " + std::to_string(n);
			const float w = text(pos, true, 17, 0, 0, p.muted, false, 0, true);
			text(pos, true, 17, 602 - w, 31, p.muted);
			for (int r = achTop; r < n && r < achTop + ROWS; r++)
			{
				const ach::Item& it = l.items[r];
				const std::string bname = it.unlocked() ? it.badge : it.badge + "_lock";
				rnds::TexPtr tex = achLoader->badge(bname) ? achBadge(achBadgeDir() + "/" + bname + ".png", achTex) : nullptr;
				achRow(p, it, ACH_Y + (r - achTop) * ACH_PITCH, ACH_H, 1, r == achSel, tex);
			}
			// where the rows are in the list: a thin bar at the right edge, when there are more than fit
			if (n > ROWS)
			{
				const float ty = ACH_Y, th = ROWS * ACH_PITCH - (ACH_PITCH - ACH_H);
				const float kh = std::max(16.0f, th * ROWS / n), ky = ty + (th - kh) * achTop / std::max(1, n - ROWS);
				be->fill(X(628), X(ty), X(4), X(th), p.line);
				be->fill(X(628), X(ky), X(4), X(kh), p.accent);
			}
		}
		else
		{
			box("slot", false, 18, ACH_Y, 604, 200);
			wrapped(achStateText(sn.state), 18, 40, ACH_Y + 22, 560, 6, p.muted);
		}
		drawChips(achChips(ready ? n : 0));
		be->endScreen();
	}
}
