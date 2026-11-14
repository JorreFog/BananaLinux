// banana-ui: the PC games hub (the AYN Thor's PC games tile opens it in place of a plain shelf). The games on the card
// and the ones of the owner's Steam library in one list, what GameNative's library is on Android: a game that is on
// the card starts, one that is only in the library downloads (and says how far it is), and each has a sheet with what
// one would otherwise edit by hand in its .pc file: how careful the recompiler is, DXVK's overlay, the Direct3D layer,
// which program starts, and its folder taken off the card. All of it is the pc-games and steam tools' doing
// (usr/libexec/banana): this page only asks them and shows what they say.
#include "settings.h"
#include "settings_draw.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <strings.h>
#include <unistd.h>

namespace banana
{
	using rnds::Color;

	static std::string pcQuote(const std::string& x)
	{
		std::string r = "'";
		for (char c : x) { if (c == '\'') r += "'\\''"; else r += c; }
		return r + "'";
	}
	static std::string pcOut(const std::string& cmd)
	{
		std::string out;
		if (FILE* f = popen((cmd + " 2>/dev/null").c_str(), "r")) { char b[512]; while (fgets(b, sizeof b, f)) out += b; pclose(f); }
		return out;
	}
	static const char* const PC_FILTERS[] = { "All games", "On the card", "To download" };
	static const struct { const char* key; const char* label; const char* values[4]; const char* names[4]; } PC_OPTS[] = {
		{ "preset", "Compatibility", { "", "compatible", "fast", nullptr }, { "Balanced", "Careful", "Fast", nullptr } },
		{ "hud", "Overlay", { "", "fps", "full", nullptr }, { "Off", "Frame rate", "Everything", nullptr } },
		{ "d3d", "Direct3D", { "", "wined3d", nullptr, nullptr }, { "DXVK (Vulkan)", "Wine's own", nullptr, nullptr } },
	};

	bool Settings::pcHub() const { return !pcTool.empty(); }

	std::string Settings::pcState() const
	{
		if (page != PCGAMES || !mOpen) return "closed";
		std::string r = "level=" + std::to_string(pcLevel) + " filter=" + std::to_string(pcFilter) + " shown=" + std::to_string(pcShown().size());
		if (pcSel >= 0 && pcSel < (int)pcGames.size()) r += " game=" + pcGames[pcSel].name + " state=" + pcGames[pcSel].state;
		if (pcLevel == 1)
		{
			r += " row=" + std::to_string(pcRow) + " rows=";
			for (const PcRow& x : pcRows) r += x.id + (x.value.empty() ? "" : ":" + x.value) + "|";
		}
		if (!pcDlId.empty()) r += " dl=" + pcDlId + ":" + std::to_string(pcDlPct) + ":" + pcDlState;
		return r;
	}

	void Settings::openPcGames()
	{
		mOpen = true;
		wizard = false;
		inGroups = false;
		armed = -1;
		message.clear();
		page = PCGAMES;
		pcLevel = 0; pcRow = 0; pcArm = false;
		pcLoad();
		pcPollAt = 0;
		pollPc(now);
	}

	void Settings::pcLoad()
	{
		const std::string keep = pcSel >= 0 && pcSel < (int)pcGames.size() ? pcGames[pcSel].file : "";
		pcGames.clear();
		std::istringstream in(pcOut(pcQuote(pcTool) + " list"));
		for (std::string ln; std::getline(in, ln);)
		{
			std::vector<std::string> f;
			for (size_t a = 0;;) { const size_t b = ln.find('\t', a); f.push_back(ln.substr(a, b == std::string::npos ? b : b - a)); if (b == std::string::npos) break; a = b + 1; }
			if (f.size() < 3 || f[0].empty()) continue;
			PcGame g;
			g.file = f[0]; g.name = f[1]; g.state = f[2];
			if (f.size() > 3) g.appid = f[3];
			if (f.size() > 4) g.mb = atol(f[4].c_str());
			if (f.size() > 5) g.cover = f[5];
			pcGames.push_back(g);
		}
		// (what can be played first, then by name)
		std::stable_sort(pcGames.begin(), pcGames.end(), [](const PcGame& a, const PcGame& b)
		{
			const bool ia = a.state != "cloud", ib = b.state != "cloud";
			if (ia != ib) return ia;
			return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
		});
		pcSel = 0;
		for (int i = 0; i < (int)pcGames.size(); i++) if (pcGames[i].file == keep) pcSel = i;
		const auto v = pcShown();
		if (!v.empty() && std::find(v.begin(), v.end(), pcSel) == v.end()) pcSel = v[0];
	}

	std::vector<int> Settings::pcShown() const
	{
		std::vector<int> v;
		for (int i = 0; i < (int)pcGames.size(); i++)
			if (pcFilter == 0 || (pcFilter == 1) == (pcGames[i].state != "cloud")) v.push_back(i);
		return v;
	}

	// a download's progress (the steam tool's file: "<app id> <percent> <state...>"), and a library being read again
	bool Settings::pollPc(double t)
	{
		if (page != PCGAMES || !mOpen || t < pcPollAt) return false;
		pcPollAt = t + 1000;
		std::string id, state; int pct = 0;
		{
			std::ifstream f(runDir + "/steam.progress");
			std::string rest;
			if (f >> id >> pct) { std::getline(f >> std::ws, rest); state = rest; }
		}
		bool changed = id != pcDlId || pct != pcDlPct || state != pcDlState;
		const bool ended = changed && pcDlState == "downloading" && state != "downloading";
		pcDlId = id; pcDlPct = pct; pcDlState = state;
		if (pcRefreshing && access((runDir + "/pc-refresh.done").c_str(), F_OK) == 0)
		{
			unlink((runDir + "/pc-refresh.done").c_str());
			pcRefreshing = false;
			pcLoad();
			libChanged = true;
			changed = true;
		}
		else if (ended) { pcLoad(); libChanged = true; if (pcLevel == 1) pcSheet(); }
		return changed;
	}

	// the picked game's sheet: its rows, from what it is and what its .pc says
	void Settings::pcSheet()
	{
		pcRows.clear();
		pcArm = false;
		if (pcSel < 0 || pcSel >= (int)pcGames.size()) { pcLevel = 0; return; }
		const PcGame& g = pcGames[pcSel];
		const bool loading = !g.appid.empty() && g.appid == pcDlId && pcDlState == "downloading";
		if (loading) pcRows.push_back({ "stop", "Stop the download", "", false });
		else if (g.state == "cloud") pcRows.push_back({ "get", "Download", "", false });
		else if (g.state != "missing") pcRows.push_back({ "play", "Play", "", false });
		if (g.state == "installed" || g.state == "missing")
		{
			for (const auto& o : PC_OPTS)
			{
				std::string v = pcOut(pcQuote(pcTool) + " get " + pcQuote(g.file) + " " + o.key);
				while (!v.empty() && (v.back() == '\n' || v.back() == ' ')) v.pop_back();
				std::string name = v;
				for (int i = 0; o.values[i]; i++) if (v == o.values[i]) name = o.names[i];
				if (v == "balanced" || v == "dxvk") name = o.names[0];
				pcRows.push_back({ o.key, o.label, name, true });
			}
			pcExes.clear();
			std::istringstream in(pcOut(pcQuote(pcTool) + " exes " + pcQuote(g.file)));
			for (std::string ln; std::getline(in, ln);) if (!ln.empty()) pcExes.push_back(ln);
			std::string exe = pcOut(pcQuote(pcTool) + " get " + pcQuote(g.file) + " exe");
			while (!exe.empty() && (exe.back() == '\n' || exe.back() == ' ')) exe.pop_back();
			pcExe = exe;
			if (!pcExes.empty()) pcRows.push_back({ "exe", "Program", exe.empty() ? "Not chosen" : exe.substr(exe.rfind('/') + 1), pcExes.size() > 1 });
			if (g.state == "installed" && g.mb > 0) pcRows.push_back({ "remove", "Remove from the card", "", false });
		}
		pcRow = std::max(0, std::min(pcRow, (int)pcRows.size() - 1));
	}

	Settings::Result Settings::pcStep(int dir)
	{
		if (pcRow < 0 || pcRow >= (int)pcRows.size() || pcSel < 0 || pcSel >= (int)pcGames.size()) return NONE;
		const PcGame g = pcGames[pcSel];
		const std::string id = pcRows[pcRow].id;
		const std::string set = pcQuote(pcTool) + " set " + pcQuote(g.file) + " ";
		for (const auto& o : PC_OPTS)
			if (id == o.key)
			{
				int n = 0, at = 0;
				while (o.values[n]) n++;
				for (int i = 0; i < n; i++) if (pcRows[pcRow].value == o.names[i]) at = i;
				at = (at + dir + n) % n;
				if (system((set + o.key + (o.values[at][0] ? " " + pcQuote(o.values[at]) : "") + " >/dev/null 2>&1").c_str()) != 0) {}
				pcRows[pcRow].value = o.names[at];
				return CHANGED;
			}
		if (id == "exe" && pcExes.size() > 1)
		{
			int at = 0;
			for (int i = 0; i < (int)pcExes.size(); i++) if (pcExes[i] == pcExe) at = i;
			at = (at + dir + (int)pcExes.size()) % (int)pcExes.size();
			pcExe = pcExes[at];
			if (system((set + "exe " + pcQuote(pcExe) + " >/dev/null 2>&1").c_str()) != 0) {}
			pcRows[pcRow].value = pcExe.substr(pcExe.rfind('/') + 1);
			return CHANGED;
		}
		return NONE;
	}

	Settings::Result Settings::pressPc(Key k)
	{
		const auto v = pcShown();
		if (pcLevel == 0)
		{
			int at = 0;
			for (int i = 0; i < (int)v.size(); i++) if (v[i] == pcSel) at = i;
			const int n = (int)v.size();
			switch (k)
			{
			case Key::UP: case Key::DOWN: if (n < 2) return NONE; pcSel = v[(at + (k == Key::UP ? -1 : 1) + n) % n]; return MOVED;
			case Key::L: case Key::R: if (n < 2) return NONE; pcSel = v[std::max(0, std::min(n - 1, at + (k == Key::L ? -6 : 6)))]; return MOVED;
			case Key::Y:
				pcFilter = (pcFilter + 1) % 3;
				{ const auto w = pcShown(); if (!w.empty() && std::find(w.begin(), w.end(), pcSel) == w.end()) pcSel = w[0]; }
				return MOVED;
			case Key::X:
				// the Steam library read again (new purchases, box art); without an account: where to sign in
				if (steamTool.empty()) return NONE;
				if (!pcRefreshing)
				{
					pcRefreshing = true;
					unlink((runDir + "/pc-refresh.done").c_str());
					if (system(("(" + pcQuote(steamTool) + " library >/dev/null 2>&1; " + pcQuote(steamTool) + " art >/dev/null 2>&1; : > " + pcQuote(runDir + "/pc-refresh.done") + ") &").c_str()) != 0) pcRefreshing = false;
				}
				return CHANGED;
			case Key::A: if (n == 0) return NONE; pcLevel = 1; pcRow = 0; pcSheet(); return MOVED;
			case Key::B: close(); return CLOSED;
			default: return NONE;
			}
		}
		const int n = (int)pcRows.size();
		if (k != Key::A) pcArm = false;
		switch (k)
		{
		case Key::UP: case Key::DOWN: if (n < 2) return NONE; pcRow = (pcRow + (k == Key::UP ? -1 : 1) + n) % n; return MOVED;
		case Key::LEFT: return pcStep(-1);
		case Key::RIGHT: return pcStep(1);
		case Key::B: pcLevel = 0; return MOVED;
		case Key::A:
		{
			if (pcRow >= n || pcSel >= (int)pcGames.size()) return NONE;
			const PcGame g = pcGames[pcSel];
			const std::string id = pcRows[pcRow].id;
			if (id == "play") { pcLaunch = g.file; close(); return CLOSED; }
			if (id == "get" && !steamTool.empty())
			{
				pcDlId = g.appid; pcDlPct = 0; pcDlState = "downloading";
				{ std::ofstream f(runDir + "/steam.progress"); f << g.appid << " 0 downloading\n"; }
				if (system((pcQuote(steamTool) + " install " + pcQuote(g.file) + " >/dev/null 2>&1 &").c_str()) != 0) {}
				pcSheet();
				return CHANGED;
			}
			if (id == "stop" && !steamTool.empty())
			{
				if (system((pcQuote(steamTool) + " stop " + pcQuote(g.appid) + " >/dev/null 2>&1").c_str()) != 0) {}
				pcPollAt = 0; pollPc(now);
				pcSheet();
				return CHANGED;
			}
			if (id == "remove")
			{
				if (!pcArm) { pcArm = true; return MOVED; }
				if (system((pcQuote(pcTool) + " uninstall " + pcQuote(g.file) + " >/dev/null 2>&1").c_str()) != 0) {}
				pcLoad();
				libChanged = true;
				pcLevel = 0;
				return CHANGED;
			}
			return pcStep(1);
		}
		default: return NONE;
		}
	}

	void Settings::renderPc()
	{
		SPal p = pal(light);
		const auto v = pcShown();
		const PcGame* g = pcSel >= 0 && pcSel < (int)pcGames.size() && !v.empty() ? &pcGames[pcSel] : nullptr;
		auto size = [](long mb) { char t[32]; if (mb >= 1024) snprintf(t, sizeof t, "%.1f GB", mb / 1024.0); else snprintf(t, sizeof t, "%ld MB", mb); return std::string(t); };
		auto loading = [&](const PcGame& x) { return !x.appid.empty() && x.appid == pcDlId && pcDlState == "downloading"; };
		auto tag = [&](const PcGame& x) -> std::string
		{
			if (loading(x)) return "Downloading " + std::to_string(pcDlPct) + "%";
			if (x.state == "cloud") return "Download";
			if (x.state == "missing") return "No program";
			if (x.state == "tool") return "App";
			return x.mb > 0 ? size(x.mb) : "Ready";
		};

		// ---- the top screen: the game, what state it is in, its cover ----
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon("gamepad", 50, 56, 3);
		text("PC games", true, 17, 170, 62, p.muted, true, 1.4f);
		text(g ? fit(g->name, true, 30, 420) : std::string("No games yet"), true, 30, 170, 88, p.ink);
		std::string status, help;
		if (!g) help = steamTool.empty() ? "Copy a game's folder into roms/pc on the card and it shows here."
			: "Copy a game's folder into roms/pc on the card, or sign in to Steam (Settings > System > Accounts) and press X: your library shows here, ready to download.";
		else if (loading(*g)) { status = "Downloading " + std::to_string(pcDlPct) + "%"; help = "It goes on while you play something else. Stopping keeps what is on the card: Download carries on from there."; }
		else if (g->state == "cloud") { status = "In your Steam library"; help = "Not on the card yet. Download fetches its Windows build straight to the card; it starts from here when it is done."; }
		else if (g->state == "missing") { status = "No program chosen"; help = "Its folder is on the card but no program to start is set. Pick one under Program."; }
		else if (g->state == "tool") { status = "App"; help = "Steam's own client, for games that need it running (their copy protection, their multiplayer)."; }
		else { status = "On the card" + (g->mb > 0 ? " \xc2\xb7 " + size(g->mb) : std::string()); help = "A starts it. Its sheet has what to try when a game misbehaves: Careful for crashes, Fast for speed, Wine's own Direct3D when the picture is wrong."; }
		if (pcRefreshing) status = "Reading your Steam library...";
		if (!status.empty()) text(fit(status, true, 22, 420), true, 22, 170, 130, p.accent);
		const bool cover = g && !g->cover.empty();
		const bool wide = material() && lay();	// (the screen's own shape: room beside the text for the cover)
		wrapped(help, 20, 50, wide ? HELP_TOP : 196, cover ? (wide ? 480 : 360) : 540, 7, p.muted);
		if (cover)
		{
			const std::string key = "pccover:" + g->cover;
			auto it = cache.find(key);
			if (it == cache.end())
			{
				rnds::Bitmap b = rnds::decodeFile(g->cover);
				Cached c;
				if (!b.empty() && !b.alpha) { c.tex = be->upload(b, true); c.w = (float)b.w / std::max(1, b.h); }
				it = put(key, c, b);
			}
			Cached& c = keep(it);
			if (c.tex)
			{
				// (as tall as the card allows, its own shape kept: Steam's are 2:3)
				const float h = wide ? 360 : 300, w = std::min(wide ? 260.0f : 230.0f, h * c.w), x = wide ? 560 : 600 - w, y = wide ? 60 : 96;
				be->draw(c.tex, X(x), X(y), X(w), X(w / c.w), Color(1, 1, 1));
			}
		}
		be->endScreen();

		// ---- the bottom screen: the list, or the picked game's sheet ----
		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("bubble", false, 18, 14, 604, 54);
		const bool mat = material() && lay();
		const Measures m = measures();
		const float rowY = mat ? m.rowY : 80, pitch = mat ? m.rowPitch : 41, rowH = mat ? m.rowH : 37;
		const int VIS = mat ? 7 : 8;
		const std::string head = pcLevel == 1 && g ? g->name : std::string(PC_FILTERS[pcFilter]);
		const std::string count = pcLevel == 1 ? std::string() : std::to_string(v.size()) + (v.size() == 1 ? " game" : " games");
		float cw = count.empty() ? 0 : text(count, false, 17, 0, 0, p.muted, false, 0, true);
		text(fit(head, true, 22, 540 - cw), true, 22, 38, 28, p.ink);
		if (!count.empty()) text(count, false, 17, 602 - cw, 31, p.muted);
		auto row = [&](int r, bool on, const std::string& label, const std::string& value, bool arrows, bool danger, const char* gl)
		{
			const float y = rowY + r * pitch;
			if (mat)
			{
				box(on ? "pick" : "slot", on, 18, y, 604, rowH);
				Item it;
				it.label = label; it.kind = Item::ACTION; it.key = gl;
				listRow(it, y, rowH, on, value, arrows, danger);
				return;
			}
			box(on ? "pick" : "slot", on, 18, y, 604, rowH);
			const Color c = on ? p.onPick : p.ink;
			float tw = value.empty() ? 0 : text(value, true, 17, 0, 0, c, false, 0, true);
			text(fit(label, true, 18, 560 - tw - 24), true, 18, 34, y + 9, c);
			if (!value.empty()) text(value, true, 17, 606 - tw, y + 10, on ? p.onPick : p.muted);
		};
		if (pcLevel == 0)
		{
			int at = 0;
			for (int i = 0; i < (int)v.size(); i++) if (v[i] == pcSel) at = i;
			pcTop = std::max(0, std::min(std::max(pcTop, at - VIS + 1), std::min(at, std::max(0, (int)v.size() - VIS))));
			for (int r = 0; r < VIS && pcTop + r < (int)v.size(); r++)
			{
				const PcGame& x = pcGames[v[pcTop + r]];
				row(r, v[pcTop + r] == pcSel, x.name, tag(x), false, false, x.state == "cloud" ? "update" : "pcplay");
			}
			if (v.empty()) text(pcFilter == 2 ? "Nothing left to download." : "Nothing here yet.", true, 20, 38, rowY + 20, p.muted);
			float bw = faceWidth("B", "Back");
			face("B", "Back", 18, bw, 422);
			float aw = faceWidth("A", "Open");
			face("A", "Open", 622 - aw, aw, 422);
			float yw = faceWidth("Y", "Filter");
			face("Y", "Filter", 622 - aw - 10 - yw, yw, 422);
			if (!steamTool.empty())
			{
				float xw = faceWidth("X", "Steam library");
				face("X", "Steam library", 18 + bw + 10, xw, 422);
			}
		}
		else
		{
			for (int r = 0; r < (int)pcRows.size() && r < VIS; r++)
			{
				const PcRow& x = pcRows[r];
				const bool on = r == pcRow, danger = x.id == "remove" && pcArm && on;
				std::string value = x.value;
				if (x.id == "stop") value = std::to_string(pcDlPct) + "%";
				if (danger) value = "A again to remove";
				row(r, on, x.label, value, x.arrows, danger, x.id == "play" ? "pcplay" : x.id == "get" ? "update" : x.id == "remove" || x.id == "stop" ? "poweroff" : "sliders");
			}
			float bw = faceWidth("B", "Back");
			face("B", "Back", 18, bw, 422);
			const bool arrows = pcRow < (int)pcRows.size() && pcRows[pcRow].arrows;
			const char* al = arrows ? "Change" : "OK";
			float aw = faceWidth("A", al);
			face("A", al, 622 - aw, aw, 422);
		}
		be->endScreen();
	}
}
