// banana-ui: the game panel - the other screen while a RetroArch game plays: the game, its achievements
// (RetroAchievements: RetroArch's log, the server), the tiles (save, load, menu, fast, quit, the keyboard), the save
// states, the on-screen keyboard; with a companion, its pages instead (the GBA Pokémon companion: gba/panel.h; any
// other GBA game with a definition: the game companion, gamecomp.h). Moved out of settings.cpp as it was.
#include "settings.h"
#include "settings_draw.h"
#include "settings_curl.h"
#include <linux/input.h>
#include "recorder.h"
#include "lang.h"
#include "vkbd.h"
#include "json.h"
#include "gba/host.h"
#include "nds/pages.h"
#include "nds/overlay.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using rnds::Color;

namespace banana
{
	// ---- the game panel -----------------------------------------------------------------------------------------
	// RetroArch logs (--verbose, run-retroarch's game panel mode) what its RetroAchievements client does: the hash it
	// made, the game it identified ("Identified game: 264 "EarthBound" (hash)") and each unlock ("Awarding achievement
	// 1234: Title"). The game's list, the account's unlocks and the badges come from the server on a thread, with
	// the account RetroArch keeps in its retroarch.cfg (cheevos_username, cheevos_token).
	namespace
	{
		struct RaAch { uint32_t id = 0; std::string title, desc, badge; unsigned points = 0; };
		// the objects of an array in the server's JSON ("Achievements":[{...},{...}]), as text
		std::vector<std::string> jsonObjects(const std::string& j, const std::string& key)
		{
			std::vector<std::string> out;
			size_t at = j.find("\"" + key + "\"");
			if (at == std::string::npos) return out;
			at = j.find('[', at);
			if (at == std::string::npos) return out;
			int depth = 0;
			bool str = false;
			size_t start = 0;
			for (size_t i = at + 1; i < j.size(); i++)
			{
				char c = j[i];
				if (str) { if (c == '\\') i++; else if (c == '"') str = false; continue; }
				if (c == '"') str = true;
				else if (c == '{') { if (depth++ == 0) start = i; }
				else if (c == '}') { if (--depth == 0) out.push_back(j.substr(start, i - start + 1)); }
				else if (c == ']' && depth == 0) break;
			}
			return out;
		}
		std::string cfgValue(const std::string& path, const std::string& key)
		{
			std::string v = getConfValue(path, key, "");
			return v;
		}
		// a POST to the server's dorequest.php; "" when it can't be reached
		std::string raPost(const std::string& fields)
		{
			Curl cu;
			if (!cu.load()) return "";
			void* h = cu.init();
			if (!h) return "";
			enum { WRITEDATA = 10001, URL = 10002, POSTFIELDS = 10015, USERAGENT = 10018, WRITEFUNCTION = 20011, TIMEOUT = 13, NOSIGNAL = 99 };
			std::string body;
			cu.setopt(h, URL, "https://retroachievements.org/dorequest.php");
			cu.setopt(h, POSTFIELDS, fields.c_str());
			cu.setopt(h, USERAGENT, "BananaOS/0.2 rcheevos");
			cu.setopt(h, NOSIGNAL, 1L);
			cu.setopt(h, TIMEOUT, 20L);
			cu.setopt(h, WRITEFUNCTION, onBody);
			cu.setopt(h, WRITEDATA, &body);
			bool ok = cu.perform(h) == 0;
			cu.cleanup(h);
			return ok ? body : "";
		}
		bool fetchFile(const std::string& url, const std::string& path)
		{
			Curl cu;
			if (!cu.load()) return false;
			void* h = cu.init();
			if (!h) return false;
			enum { WRITEDATA = 10001, URL = 10002, USERAGENT = 10018, WRITEFUNCTION = 20011, TIMEOUT = 13, NOSIGNAL = 99, RESPONSE_CODE = 0x200002 };
			std::string body;
			long status = 0;
			cu.setopt(h, URL, url.c_str());
			cu.setopt(h, USERAGENT, "BananaOS/0.2 rcheevos");
			cu.setopt(h, NOSIGNAL, 1L);
			cu.setopt(h, TIMEOUT, 20L);
			cu.setopt(h, WRITEFUNCTION, onBody);
			cu.setopt(h, WRITEDATA, &body);
			bool ok = cu.perform(h) == 0;
			if (ok) cu.getinfo(h, RESPONSE_CODE, &status);
			cu.cleanup(h);
			if (!ok || status != 200 || body.size() < 8) return false;
			std::string tmp = path + ".part";
			FILE* f = fopen(tmp.c_str(), "wb");
			if (!f) return false;
			bool wrote = fwrite(body.data(), 1, body.size(), f) == body.size();
			wrote &= fflush(f) == 0;
			wrote &= fsync(fileno(f)) == 0;
			wrote &= fclose(f) == 0;
			if (!wrote || rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
			return true;
		}
		const char* const BADGES = "/data/banana/cache/ra-badges";
		const char* const IC_SAVE[12] = { "##########..", "#..#####.##.", "#..#####..##", "#..#####...#", "#..........#", "#..........#",
			"#.########.#", "#.#......#.#", "#.#......#.#", "#.#......#.#", "#.########.#", "############" };
		const char* const IC_LOAD[12] = { "............", ".####.......", "#....#......", "#.....######", "#..........#", "#....##....#",
			"#....##....#", "#..######..#", "#...####...#", "#....##....#", "#..........#", "############" };
		const char* const IC_LIST[12] = { "............", "##.#########", "##.#########", "............", "............", "##.#########",
			"##.#########", "............", "............", "##.#########", "##.#########", "............" };
		const char* const IC_FFWD[12] = { "............", "#.....#.....", "##....##....", "###...###...", "####..####..", "#####.#####.",
			"#####.#####.", "####..####..", "###...###...", "##....##....", "#.....#.....", "............" };
		const char* const IC_EXIT[12] = { "######......", "#....#......", "#....#...#..", "#....#...##.", "#....#######", "#....#######",
			"#....#...##.", "#....#...#..", "#....#......", "#....#......", "######......", "............" };
		const char* const IC_KEY[12] = { "............", "...######...", "..#......#..", ".##########.", ".#........#.",
			".#.#....#.#.", ".#........#.", ".##########.", "............", "............", "............", "............" };
		struct GTile { const char* label; const char* keys; const char* const* icon; };
		const GTile GTILES[6] = { { "Save", "F + R", IC_SAVE }, { "Load", "F + L", IC_LOAD }, { "Menu", "F + X", IC_LIST },
			{ "Fast", "F + R2", IC_FFWD }, { "Quit", "F + START", IC_EXIT }, { "Keys", "Tap", IC_KEY } };

		// ---- the panel's keyboard (gameKeysFrame, gameTouch): a computer's keys with a finger -------------------
		// 640x480 design coordinates (X() scales them): 10 columns, 4 rows, then a bar of pages and the keys a game
		// uses; the whole panel, while the game runs on the other screen
		const float VK_X = 12, VK_Y = 64, VK_CW = 61.6f, VK_CH = 56, VK_RGAP = 6, VK_BAR_Y = 328, VK_BAR_H = 44;
		struct VKey { const char* label; int code; bool shift; };	// code -1: the shift toggle; no label: an empty cell
		const VKey VK_ABC[40] = {
			{ "1", KEY_1 }, { "2", KEY_2 }, { "3", KEY_3 }, { "4", KEY_4 }, { "5", KEY_5 },
			{ "6", KEY_6 }, { "7", KEY_7 }, { "8", KEY_8 }, { "9", KEY_9 }, { "0", KEY_0 },
			{ "Q", KEY_Q }, { "W", KEY_W }, { "E", KEY_E }, { "R", KEY_R }, { "T", KEY_T },
			{ "Y", KEY_Y }, { "U", KEY_U }, { "I", KEY_I }, { "O", KEY_O }, { "P", KEY_P },
			{ "A", KEY_A }, { "S", KEY_S }, { "D", KEY_D }, { "F", KEY_F }, { "G", KEY_G },
			{ "H", KEY_H }, { "J", KEY_J }, { "K", KEY_K }, { "L", KEY_L }, { ";", KEY_SEMICOLON },
			{ "Z", KEY_Z }, { "X", KEY_X }, { "C", KEY_C }, { "V", KEY_V }, { "B", KEY_B },
			{ "N", KEY_N }, { "M", KEY_M }, { ",", KEY_COMMA }, { ".", KEY_DOT }, { "/", KEY_SLASH },
		};
		const VKey VK_SYM[40] = {
			{ "!", KEY_1, true }, { "\"", KEY_APOSTROPHE, true }, { "#", KEY_3, true }, { "$", KEY_4, true }, { "%", KEY_5, true },
			{ "&", KEY_7, true }, { "'", KEY_APOSTROPHE }, { "(", KEY_9, true }, { ")", KEY_0, true }, { "*", KEY_8, true },
			{ "+", KEY_EQUAL, true }, { ",", KEY_COMMA }, { "-", KEY_MINUS }, { ".", KEY_DOT }, { "/", KEY_SLASH },
			{ ":", KEY_SEMICOLON, true }, { ";", KEY_SEMICOLON }, { "<", KEY_COMMA, true }, { "=", KEY_EQUAL }, { ">", KEY_DOT, true },
			{ "?", KEY_SLASH, true }, { "@", KEY_2, true }, { "[", KEY_LEFTBRACE }, { "\\", KEY_BACKSLASH }, { "]", KEY_RIGHTBRACE },
			{ "^", KEY_6, true }, { "_", KEY_MINUS, true }, { "`", KEY_GRAVE }, { "{", KEY_LEFTBRACE, true }, { "}", KEY_RIGHTBRACE, true },
			{ "|", KEY_BACKSLASH, true }, { "~", KEY_GRAVE, true },
			{ nullptr }, { nullptr }, { nullptr }, { nullptr }, { nullptr }, { nullptr }, { nullptr }, { nullptr },
		};
		const VKey VK_KEYS[40] = {
			{ "F1", KEY_F1 }, { "F2", KEY_F2 }, { "F3", KEY_F3 }, { "F4", KEY_F4 }, { "F5", KEY_F5 },
			{ "F6", KEY_F6 }, { "F7", KEY_F7 }, { "F8", KEY_F8 }, { "F9", KEY_F9 }, { "F10", KEY_F10 },
			{ "F11", KEY_F11 }, { "F12", KEY_F12 }, { "RUN", KEY_ESC }, { "CLR", KEY_HOME }, { "RET", KEY_ENTER },
			{ "SPACE", KEY_SPACE }, { "DEL", KEY_BACKSPACE }, { "ESC", KEY_ESC }, { "TAB", KEY_TAB }, { "CAPS", KEY_CAPSLOCK },
			{ "LEFT", KEY_LEFT }, { "RIGHT", KEY_RIGHT }, { "UP", KEY_UP }, { "DOWN", KEY_DOWN }, { "HOME", KEY_HOME },
			{ "END", KEY_END }, { "INS", KEY_INSERT }, { "CTRL", KEY_LEFTCTRL }, { "SHIFT", -1 }, { nullptr },
			{ nullptr }, { nullptr }, { nullptr }, { nullptr }, { nullptr }, { nullptr }, { nullptr }, { nullptr }, { nullptr }, { nullptr },
		};
		const VKey* const VK_PAGES[3] = { VK_ABC, VK_SYM, VK_KEYS };
		const char* const VK_PAGE_NAMES[3] = { "abc", "sym", "keys" };
	}

	struct Settings::RaWatch
	{
		std::mutex mu;
		// 0 waiting for RetroArch's client, 1 looking the game up, 2 ready, 3 no achievements for this game, 4 no
		// account, 5 no connection
		int state = 0;
		std::string game, hash;
		uint32_t gameId = 0;
		std::vector<RaAch> all;
		std::map<uint32_t, long long> unlocked;		// id -> when (unix time)
		std::map<std::string, bool> badgeReady;		// badge name -> its file is in BADGES
		int gen = 0;
		int awards = 0;		// achievements unlocked while the game runs (each one wakes a dimmed bottom panel)
		std::string user, token;
		std::string rp;		// the game's rich presence ("Lv 23, Viridian Forest"), from the emulator's RA block
		struct Near { uint32_t id; unsigned percent; std::string measured, title; };
		std::vector<Near> nearest;	// the locked achievements closest to done (measured progress)
		uint32_t evSeen = 0;		// the block's events read so far
		bool fromBlock = false;		// the emulator's block is the source (not RetroArch's log)
		time_t cfgSeen = 0;	// retroarch.cfg's mtime when the token was last looked for in it
		long long pos = 0;
		std::string part;
		double since = 0;
		// the badge's file, fetched on a thread when it isn't cached yet
		void wantBadge(const std::shared_ptr<RaWatch>& self, const std::string& name)
		{
			if (name.empty() || badgeReady.count(name)) return;
			std::string path = std::string(BADGES) + "/" + name + ".png";
			if (access(path.c_str(), R_OK) == 0) { badgeReady[name] = true; return; }
			badgeReady[name] = false;
			std::thread([self, name, path]
			{
				if (system((std::string("mkdir -p ") + BADGES).c_str()) != 0) {}
				bool ok = fetchFile("https://media.retroachievements.org/Badge/" + name + ".png", path);
				std::lock_guard<std::mutex> l(self->mu);
				if (ok) { self->badgeReady[name] = true; self->gen++; }
			}).detach();
		}
		static void fetch(std::shared_ptr<RaWatch> w)
		{
			std::string user, token, hash;
			uint32_t id;
			{
				std::lock_guard<std::mutex> l(w->mu);
				user = w->user; token = w->token; hash = w->hash; id = w->gameId;
			}
			Curl cu;
			std::string g = std::to_string(id);
			std::string patch = raPost("r=patch&u=" + user + "&t=" + token + "&g=" + g);
			fprintf(stderr, "game panel: game %s: the server sent %zu bytes (%s)\n", g.c_str(), patch.size(),
				patch.empty() ? "no reply" : jsonField(patch, "Success") == "true" ? "ok" : jsonField(patch, "Error").c_str());
			if (patch.empty() || jsonField(patch, "Success") != "true")
			{
				std::lock_guard<std::mutex> l(w->mu);
				w->state = patch.empty() ? 5 : 3;
				w->gen++;
				return;
			}
			std::vector<RaAch> all;
			for (auto& o : jsonObjects(patch, "Achievements"))
			{
				if (jsonField(o, "Flags") != "3") continue;	// core achievements only (5: unofficial)
				RaAch a;
				a.id = (uint32_t)strtoul(jsonField(o, "ID").c_str(), nullptr, 10);
				if (a.id == 101000001) continue;	// the server's "Warning: Unknown Emulator", not one of the game's
				a.title = jsonField(o, "Title");
				a.desc = jsonField(o, "Description");
				a.badge = jsonField(o, "BadgeName");
				a.points = (unsigned)atoi(jsonField(o, "Points").c_str());
				all.push_back(a);
			}
			// (softcore: the panel only reads; RetroArch keeps its own session, and a hardcore one from a client the
			// server doesn't know gets its warning)
			std::string sess = raPost("r=startsession&u=" + user + "&t=" + token + "&g=" + g + "&h=0" + (hash.empty() ? "" : "&m=" + hash));
			std::map<uint32_t, long long> un;
			for (const char* k : { "Unlocks", "HardcoreUnlocks" })
				for (auto& o : jsonObjects(sess, k))
					un[(uint32_t)strtoul(jsonField(o, "ID").c_str(), nullptr, 10)] = atoll(jsonField(o, "When").c_str());
			std::lock_guard<std::mutex> l(w->mu);
			w->all = all;
			for (auto& u : un) if (!w->unlocked.count(u.first)) w->unlocked[u.first] = u.second;
			w->state = all.empty() ? 3 : 2;
			w->gen++;
		}
		// the emulator's RetroAchievements block (bcomp.h, COMPANION-SPEC section 4): its client (rcheevos) identified the
		// game and unlocks; the list and the badges still come from the server, as for RetroArch's log
		void block(const std::shared_ptr<RaWatch>& self, const bcomp_ra& ra)
		{
			std::lock_guard<std::mutex> l(mu);
			fromBlock = true;
			int st = ra.state == BCOMP_RA_LOGIN || ra.state == BCOMP_RA_LOADING ? 0 : ra.state == BCOMP_RA_NO_SET || ra.state == BCOMP_RA_UNKNOWN_GAME ? 3 :
				ra.state == BCOMP_RA_LOGIN_FAILED ? 4 : -1;
			if (st >= 0 && st != state && gameId == 0) { state = st; gen++; }
			if (ra.game_id && ra.game_id != gameId && (ra.state == BCOMP_RA_ACTIVE || ra.state == BCOMP_RA_OFFLINE))
			{
				gameId = ra.game_id;
				state = 1; gen++;
				fprintf(stderr, "game panel: the emulator identified game %u\n", gameId);
				std::thread(fetch, self).detach();
			}
			std::string r(ra.rp, strnlen(ra.rp, sizeof ra.rp));
			if (r != rp) { rp = r; gen++; }
			std::vector<Near> nn;
			for (auto& n : ra.near_)
				if (n.id) nn.push_back({ n.id, n.percent, std::string(n.measured, strnlen(n.measured, sizeof n.measured)), std::string(n.title, strnlen(n.title, sizeof n.title)) });
			bool same = nn.size() == nearest.size();
			for (size_t i = 0; same && i < nn.size(); i++) same = nn[i].id == nearest[i].id && nn[i].measured == nearest[i].measured;
			if (!same) { nearest = nn; gen++; }
			uint32_t from = ra.ev_head > BCOMP_RA_EVENTS && evSeen < ra.ev_head - BCOMP_RA_EVENTS ? ra.ev_head - BCOMP_RA_EVENTS : evSeen;
			for (uint32_t i = from; i < ra.ev_head; i++)
			{
				const bcomp_ra_event& e = ra.ev[i % BCOMP_RA_EVENTS];
				if (e.type == BCOMP_EV_UNLOCK) { unlocked[e.id] = (long long)time(nullptr); gen++; awards++; }
			}
			evSeen = ra.ev_head;
		}
		// RetroArch's log since the last look
		void poll(const std::shared_ptr<RaWatch>& self, double nowMs)
		{
			FILE* f = fopen("/run/banana/retroarch.log", "r");
			if (!f) return;
			// a new log (shorter than what was read): from its start
			if (fseek(f, 0, SEEK_END) == 0 && ftell(f) < (long)pos) { pos = 0; part.clear(); }
			if (fseek(f, (long)pos, SEEK_SET) != 0) { fclose(f); return; }
			char buf[8192];
			size_t n;
			while ((n = fread(buf, 1, sizeof buf, f)) > 0) { part.append(buf, n); pos += (long long)n; }
			fclose(f);
			size_t nl;
			while ((nl = part.find('\n')) != std::string::npos)
			{
				std::string ln = part.substr(0, nl);
				part.erase(0, nl + 1);
				if (ln.find("[RCHEEVOS]") == std::string::npos) continue;
				std::lock_guard<std::mutex> l(mu);
				size_t at;
				if ((at = ln.find("Generated hash ")) != std::string::npos) hash = ln.substr(at + 15, 32);
				else if ((at = ln.find("Identified game: ")) != std::string::npos)
				{
					gameId = (uint32_t)strtoul(ln.c_str() + at + 17, nullptr, 10);
					size_t q0 = ln.find('"', at), q1 = ln.rfind('"');
					if (q0 != std::string::npos && q1 > q0) game = ln.substr(q0 + 1, q1 - q0 - 1);
					if (!gameId) { state = 3; gen++; continue; }
					fprintf(stderr, "game panel: RetroArch identified game %u (%s)\n", gameId, game.c_str());
					state = 1; gen++;
					std::thread(fetch, self).detach();
				}
				else if (ln.find("Unknown game") != std::string::npos || ln.find("not recognized") != std::string::npos) { state = 3; gen++; }
				else if (ln.find("Login failed") != std::string::npos && state < 2) { state = ln.find("No response") != std::string::npos ? 5 : 4; gen++; }
				else if ((at = ln.find("Awarding achievement ")) != std::string::npos)
				{
					uint32_t id = (uint32_t)strtoul(ln.c_str() + at + 21, nullptr, 10);
					if (id == 101000001) continue;	// (the server's warning)
					unlocked[id] = (long long)time(nullptr);
					gen++; awards++;
				}
			}
			std::lock_guard<std::mutex> l(mu);
			if (state == 0 && since > 0 && nowMs - since > 30000) { state = 5; gen++; fprintf(stderr, "game panel: no game from RetroArch's log in 30 s\n"); }	// RetroArch's client never got the game
		}
	};

	void Settings::gameOpen(const std::string& title, const std::string& system, int screen, bool touch)
	{
		gTitle = title; gSystem = system; gScreen = screen; gTouch = touch;
		gFlash = gDown = -1; gSeen = -1; awardsSeen = 0; wake = false;
		gKeysOpen = false; gKeyPage = 0; gKeyShift = 0; gKeyTyped.clear();
		gcView = gcLast = gcomp::P_STATUS; gcScreen = 0; gcScreens = 1; gcList = gcRow = -1; gcBattle = false; gcHits.clear();
		if (gameComp) gameComp->want(gcView);
		// the buttons that switch between the menu and the companion: a job of Companion (Settings > Other systems >
		// Buttons > Game Boy Advance, the one system with the row; run-retroarch takes L2/R2/L3/R3 with it off the game,
		// Back is no RetroPad button). Back's by default (the template's)
		gCompKeys.clear();
		if (system == "Game Boy Advance")
		{
			static const struct { const char* b; int code; } BTNS[] = { { "back", BTN_Z }, { "l2", BTN_TL2 }, { "r2", BTN_TR2 }, { "l3", BTN_THUMBL }, { "r3", BTN_THUMBR } };
			std::map<std::string, std::string> conf;
			{
				std::ifstream f(data + "/settings.conf");
				std::string ln;
				while (std::getline(f, ln))
				{
					const size_t eq = ln.find('=');
					if (ln.compare(0, 7, "ra_btn_") || eq == std::string::npos) continue;
					auto trim = [](std::string t) { while (!t.empty() && isspace((unsigned char)t.back())) t.pop_back(); size_t a = 0; while (a < t.size() && isspace((unsigned char)t[a])) a++; return t.substr(a); };
					conf[trim(ln.substr(0, eq))] = trim(ln.substr(eq + 1));	// (the last line counts, as getConfValue)
				}
			}
			for (auto& b : BTNS)
			{
				auto it = conf.find(std::string("ra_btn_") + b.b + ".gba");
				const std::string v = it != conf.end() ? it->second : strcmp(b.b, "back") ? "" : "companion";
				if (v == "companion") gCompKeys.push_back(b.code);
			}
		}
		gIconTex = nullptr;
		gIconBadge = -1;
		raw = std::make_shared<RaWatch>();
		raw->since = now;
		std::string cfg = "/data/retroarch/retroarch.cfg";
		raw->user = cfgValue(cfg, "cheevos_username");
		raw->token = cfgValue(cfg, "cheevos_token");
		// since 0.4.2 ra-secrets moves RetroArch's token out of retroarch.cfg (file sharing shows it) into
		// accounts.conf, for the same account: read from there, or every request got "Invalid user/token combination"
		// and the panel showed no achievements
		if (raw->token.empty() && !raw->user.empty() && account("ra_user") == raw->user) raw->token = account("ra_retroarch_token");
		if (cfgValue(cfg, "cheevos_enable") != "true" || raw->user.empty()) raw->state = 4;
	}

	void Settings::gameRom(const std::string& rom, const std::string& stateDir)
	{
		// RetroArch's (and BananaFront's) state names: the content's file name without its extension
		std::string b = rom.substr(rom.find_last_of('/') + 1);
		size_t dot = b.find_last_of('.');
		if (dot != std::string::npos && dot > 0) b.erase(dot);
		gStateRoot = stateDir;
		gStateName = b;
		gStateBase = stateDir + "/" + b;
	}

	void Settings::gameStatesScan()
	{
		gStatesScan = now;
		// the folder: BananaFront names it in /run/banana/game-id (state_dir); RetroArch keeps a folder a core
		// (/data/states/mGBA, gpSP...): the one holding this game's newest state; else the folder itself
		if (!gStateName.empty())
		{
			std::string dir;
			std::ifstream gid("/run/banana/game-id");
			std::string ln, sdir, base;
			while (std::getline(gid, ln))
			{
				if (ln.compare(0, 10, "state_dir=") == 0) sdir = ln.substr(10);
				else if (ln.compare(0, 9, "rom_base=") == 0) base = ln.substr(9);
			}
			if (!sdir.empty() && base == gStateName) dir = sdir;
			if (dir.empty())
			{
				long long best = -1;
				std::vector<std::string> dirs = { gStateRoot };
				if (DIR* d = opendir(gStateRoot.c_str()))
				{
					while (dirent* e = readdir(d))
						if (e->d_name[0] != '.') dirs.push_back(gStateRoot + "/" + e->d_name);
					closedir(d);
				}
				for (auto& c : dirs)
					for (int i = 0; i < 10; i++)
					{
						struct stat st;
						std::string f = c + "/" + gStateName + ".state" + (i ? std::to_string(i) : "");
						if (stat(f.c_str(), &st) == 0 && S_ISREG(st.st_mode) && (long long)st.st_mtime > best) { best = st.st_mtime; dir = c; }
					}
			}
			if (dir.empty()) dir = gStateRoot;
			if (gStateBase != dir + "/" + gStateName)
			{
				gStateBase = dir + "/" + gStateName;
				for (auto& f : gStateFiles) f = StateFile();
			}
		}
		for (int i = 0; i < 10; i++)
		{
			std::string f = gStateBase + ".state" + (i ? std::to_string(i) : "");
			struct stat st;
			long long m = stat(f.c_str(), &st) == 0 ? (long long)st.st_mtime : 0;
			if (m == gStateFiles[i].mtime) continue;
			gStateFiles[i].mtime = m;
			gStateFiles[i].tex.reset();
			if (!m) continue;
			rnds::Bitmap b = rnds::decodeFile(f + ".png");
			if (!b.empty()) gStateFiles[i].tex = be->upload(b, true);
		}
	}

	// the States view in the achievements box: ten slots, a picture and the time each was saved
	void Settings::gameStatesFrame(const SPal& p)
	{
		if (gStateBase.empty()) return;
		if (now - gStatesScan > 2000) gameStatesScan();
		box("slot", false, 18, 80, 604, 206);
		text("Save states", true, 15, 40, 96, p.muted, true, 1.2f);
		std::string hint = gSlotSel >= 0 ? "Tap it again to load" : "Tap a slot twice to load it";
		float hw = text(hint, false, 14, 0, 0, p.muted, false, 0, true);
		text(hint, false, 14, 600 - hw, 97, gSlotSel >= 0 ? p.accent : p.muted);
		for (int i = 0; i < 10; i++)
		{
			float x = 30 + (i % 5) * 117, y = 120 + (i / 5) * 82;
			bool sel = i == gSlotSel;
			box(sel ? "pick" : "bubble", sel, x, y, 109, 76);
			const StateFile& f = gStateFiles[i];
			Color fg = sel ? p.onPick : p.ink;
			std::string n = i ? std::to_string(i) : "0";
			if (!f.mtime)
			{
				text(n, true, 15, x + 8, y + 6, p.muted);
				text("Empty", false, 14, x + 30, y + 30, p.muted);
				continue;
			}
			if (f.tex) be->draw(f.tex, X(x + 31), X(y + 5), X(72), X(48), Color(1, 1, 1, 1));
			text(n, true, 15, x + 8, y + 6, fg);
			char w[24];
			time_t tt = (time_t)f.mtime, nt = time(nullptr);
			struct tm a, b;
			localtime_r(&tt, &a); localtime_r(&nt, &b);
			strftime(w, sizeof w, a.tm_yday == b.tm_yday && a.tm_year == b.tm_year ? "%H:%M" : "%d %b", &a);
			float ww = text(w, false, 13, 0, 0, fg, false, 0, true);
			text(w, false, 13, x + 55 - ww / 2, y + 56, sel ? p.onPickAccent : p.muted);
		}
	}

	void Settings::gameClose()
	{
		raw.reset();
		gStates = false; gSlotSel = gSlotPick = -1;
		for (auto& f : gStateFiles) f = StateFile();
		gStateBase.clear();
		gStateRoot.clear();
		gStateName.clear();
		gBadges.clear();
		gbaRelease();
	}

	void Settings::gbaAttach(gba::Host* host)
	{
		gba::Panel::Look look;
		look.be = be;
		look.screen = gScreen;
		look.w = (int)std::lround(640 * s);
		look.h = (int)std::lround(480 * s);
		look.assets = assets;
		look.light = light;
		gbaPanel.open(host, companion, look);
	}

	void Settings::gbaRelease() { gbaPanel.close(); }

	std::string Settings::gamePanelView() const
	{
		if (gbaPanel.active() || !gameComp || !gameComp->active()) return gbaPanel.view();
		return gcView == GC_GAME ? "game" : std::string("gc:") + gcomp::pageIdName(gcView);
	}

	// ---- the companion's switch (either companion): the icon on the menu's header in the top-right corner, the pages'
	// Menu in the same place (the DS companion's way: nds/overlay.h); the back button (a job of Companion); F the menu

	bool Settings::compOn() const { return gbaPanel.active() || (gameComp && gameComp->active()); }

	bool Settings::compCorner(float lx, float ly) const { return lx >= 512 && ly < 72; }

	void Settings::compMenu()
	{
		if (gbaPanel.active()) { gbaPanel.showMenu(); return; }
		if (!gameComp || !gameComp->active() || gcView == GC_GAME) return;
		gcLast = gcView;
		const gcomp::Def* d = gameComp->game();
		gcFightAtMenu = d && d->pages[gcomp::P_BATTLE].when.set && gameComp->pageOn(gcomp::P_BATTLE);
		gcView = GC_GAME; gcScreen = 0; gcList = gcRow = -1;
		gameComp->want(-1);
	}

	void Settings::compToggle()
	{
		gDown = gFlash = -1;
		gStates = false;
		if (gbaPanel.active()) { gbaPanel.toggle(now); return; }
		if (!gameComp || !gameComp->active()) return;
		if (gcView != GC_GAME) { compMenu(); return; }
		// (back on the page the menu left - Status for a Battle page whose fight is over -, Battle when a fight started
		// meanwhile)
		gcView = gcLast;
		const gcomp::Def* d = gameComp->game();
		const bool fight = d && d->pages[gcomp::P_BATTLE].when.set && gameComp->pageOn(gcomp::P_BATTLE);
		if (gcView == gcomp::P_BATTLE && d && d->pages[gcomp::P_BATTLE].when.set && !fight) gcView = gcomp::P_STATUS;
		if (fight && !gcFightAtMenu) gcView = gcomp::P_BATTLE;
		gcScreen = 0; gcList = gcRow = -1;
		gameComp->want(gcView);
	}

	bool Settings::gameButton(int code, double nowMs)
	{
		if (!compOn() || gKeysOpen) return false;
		now = nowMs;	// (a press can come long after the last update: the pages' notices and animations from now)
		const bool onComp = gbaPanel.active() ? gbaPanel.pagesShown() : gcView != GC_GAME;
		if (code == BTN_MODE) { if (!onComp) return false; compMenu(); return true; }	// (F: the menu, where its combos are)
		if (std::find(gCompKeys.begin(), gCompKeys.end(), code) == gCompKeys.end()) return false;
		compToggle();
		return true;
	}

	void Settings::compIcon()
	{
		// the DS companion's icon (iconPicture), its badge the GBA pages' new notices; drawn again when the count changes
		const int badge = gbaPanel.active() ? gbaPanel.badge() : 0;
		if (!gIconTex || badge != gIconBadge)
		{
			nds::setAssets(assets);
			rnds::Bitmap b = nds::iconPicture(s, badge, BOVL_CORNER_TR);
			gIconTex = b.empty() ? nullptr : be->upload(b, true);
			gIconBadge = badge;
		}
		if (gIconTex) be->draw(gIconTex, std::round(X(638)) - gIconTex->w, std::round(X(2)), (float)gIconTex->w, (float)gIconTex->h, Color(1, 1, 1, 1));
	}

	void Settings::gameTouchMove(float x, float y) { gbaPanel.touchMove((int)x, (int)y, now); }

	bool Settings::gameUpdate(double t)
	{
		now = t;
		bool changed = false;
		if (raw)
		{
			if (raw->since == 0) raw->since = t;
			bcomp_ra ra;
			if (link && link->attached() && link->ra(ra)) raw->block(raw, ra);
			else if (!raw->fromBlock) raw->poll(raw, t);
			std::lock_guard<std::mutex> l(raw->mu);
			// a token RetroArch wrote while the game started (a first login): looked for when the file changes, not on
			// every pass (the whole retroarch.cfg ten times a second, beside the game, for as long as it had none)
			struct stat st;
			if (raw->token.empty() && raw->state != 4 && stat("/data/retroarch/retroarch.cfg", &st) == 0 && st.st_mtime != raw->cfgSeen)
			{
				raw->cfgSeen = st.st_mtime;
				raw->token = cfgValue("/data/retroarch/retroarch.cfg", "cheevos_token");
			}
			if (raw->gen != gSeen) changed = true;
			if (raw->awards != awardsSeen) { awardsSeen = raw->awards; wake = true; }
		}
		if (gbaPanel.update(t)) changed = true;	// (the GBA companion's pages: the host's newest, a battle's start and end)
		if (companion && companion->takeChanged()) changed = true;	// (the tiles' Battle tab)
		if (gameComp && gameComp->takeChanged())
		{
			changed = true;
			// a fight starting shows the Battle page, its end Status (unless another tab was picked meanwhile); a page
			// with no "when" for Battle is just a page
			const gcomp::Def* d = gameComp->game();
			if (d && d->pages[gcomp::P_BATTLE].when.set)
			{
				bool b = gameComp->pageOn(gcomp::P_BATTLE);
				if (b && !gcBattle && gcView != GC_GAME) { gcView = gcomp::P_BATTLE; gcScreen = 0; gcList = gcRow = -1; }
				if (!b && gcBattle && gcView == gcomp::P_BATTLE) { gcView = gcomp::P_STATUS; gcScreen = 0; gcList = gcRow = -1; }
				gcBattle = b;
				gameComp->want(gcView == GC_GAME ? -1 : gcView, gcList);
			}
		}
		if (gFlash >= 0 && t >= gFlashUntil && gDown < 0) { gFlash = -1; changed = true; }
		return changed;
	}

	bool Settings::gameProgress(int& unlocked, int& total)
	{
		if (!raw) return false;
		std::lock_guard<std::mutex> l(raw->mu);
		if (raw->state != 2 || raw->all.empty()) return false;
		total = (int)raw->all.size();
		unlocked = 0;
		for (auto& a : raw->all) if (raw->unlocked.count(a.id)) unlocked++;
		return true;
	}

	Settings::GameAction Settings::gameTouch(float x, float y, bool down)
	{
		float lx = x / s, ly = y / s;
		int hit = -1;
		// the on-screen keyboard (the Keys tile): a key types on the press, the bar's pages and Done on the release
		if (gKeysOpen)
		{
			if (down)
			{
				if (ly >= VK_BAR_Y && ly < VK_BAR_Y + VK_BAR_H)
				{
					const int i = (int)((lx - 12) / 77);
					if (lx >= 12 && i >= 0 && i < 8 && lx < 12 + i * 77 + 73)
						switch (i)
						{
						case 0: case 1: case 2: gKeyPage = i; break;
						case 3: gKeyShift ^= 1; break;
						case 4: gameKeyTap(KEY_SPACE, false, nullptr); break;
						case 5: gameKeyTap(KEY_ENTER, false, nullptr); break;
						case 6: gameKeyTap(KEY_BACKSPACE, false, nullptr); break;
						case 7: gKeysOpen = false; break;
						}
					return G_NONE;
				}
				if (ly >= VK_Y && ly < VK_Y + 4 * (VK_CH + VK_RGAP))
				{
					const int c = (int)((lx - VK_X) / VK_CW), r = (int)((ly - VK_Y) / (VK_CH + VK_RGAP));
					const float ry = ly - VK_Y - r * (VK_CH + VK_RGAP);
					if (c >= 0 && c < 10 && r >= 0 && r < 4 && ry < VK_CH)
					{
						const VKey& k = VK_PAGES[gKeyPage < 0 || gKeyPage > 2 ? 0 : gKeyPage][r * 10 + c];
						if (k.label) gameKeyTap(k.code, k.shift, k.label);
					}
				}
			}
			return G_NONE;
		}
		if (gbaPanel.pagesShown())
		{
			// the GBA companion's pages: theirs, all of the panel; their Back to game shows the tiles
			gbaPanel.touch(down, (int)x, (int)y, now);
			return G_NONE;
		}
		bool gamePage = !(gameComp && gameComp->active() && gcView != GC_GAME);
		// the corner: the icon on the menu, Menu on the game companion's pages - the switch (on the release)
		if (compOn() && compCorner(lx, ly) && !(gamePage && gStates))
		{
			if (!down) compToggle();
			gDown = gFlash = -1;
			return G_NONE;
		}
		// the achievements box: a tap opens the save states there; a slot tapped twice loads it, a tap above closes it
		if (gamePage && !gStateBase.empty() && !down && lx >= 18 && lx < 622)
		{
			if (!gStates && ly >= 80 && ly < 286) { gStates = true; gSlotSel = -1; gameStatesScan(); return G_NONE; }
			if (gStates && ly < 80) { gStates = false; return G_NONE; }
			if (gStates && ly >= 120 && ly < 284)
			{
				int c = (int)((lx - 30) / 117), r = (int)((ly - 120) / 82);
				if (c < 0 || c > 4 || r < 0 || r > 1) return G_NONE;
				int i = r * 5 + c;
				if (!gStateFiles[i].mtime) { gSlotSel = -1; return G_NONE; }
				if (i != gSlotSel) { gSlotSel = i; return G_NONE; }
				gSlotPick = i; gSlotSel = -1; gStates = false;
				return G_LOADSLOT;
			}
		}
		const int ntiles = vkbd && gTouch ? 6 : 5;
		const float tstep = ntiles == 6 ? 102 : 122, tw = ntiles == 6 ? 96 : 114;
		for (int i = 0; i < ntiles; i++)
		{
			float tx = 18 + i * tstep;
			if (lx >= tx && lx < tx + tw && ly >= 300 && ly < 420) hit = i;
		}
		// the game companion's pages: the tab bar, a page's screens (the pager), a list's row opened or closed (the menu
		// has the tiles alone, below)
		if (gameComp && gameComp->active() && gcView != GC_GAME)
		{
			if (ly >= 424)
			{
				if (!down)
					for (int i = 0; i < gcomp::NUM_PAGES; i++)
						if (gcTabW[i] > 0 && lx >= gcTabX[i] && lx < gcTabX[i] + gcTabW[i] && i != gcView) { gcView = i; gcScreen = 0; gcList = gcRow = -1; }
				gameComp->want(gcView, gcList);
				gDown = gFlash = -1;
				return G_NONE;
			}
			{
				if (!down)
				{
					if (gcRow >= 0) { gcList = gcRow = -1; }	// (a row's view: a tap goes back to the list)
					else if (gcScreens > 1 && ly >= 372 && ly < 418)	// (the pager: its buttons, its page count)
						gcScreen = std::min(gcScreens - 1, std::max(0, gcScreen + pagerStep(lx, ly, PAGER)));
					else
						for (auto& h : gcHits)
							if (ly >= h.y0 && ly < h.y1) { gcList = h.list; gcRow = h.row; break; }
					gameComp->want(gcView, gcList);
				}
				return G_NONE;
			}
		}
		if (down) { gDown = gFlash = hit; gFlashUntil = now + 10000; return G_NONE; }
		int was = gDown;
		gDown = -1;
		gFlashUntil = now + 220;
		if (hit < 0 || hit != was) { gFlash = -1; return G_NONE; }
		static const GameAction acts[6] = { G_SAVE, G_LOAD, G_MENU, G_FFWD, G_QUIT, G_NONE };
		if (hit == 5)	// the panel's keyboard
		{
			gKeysOpen = true; gKeyPage = 0; gKeyTyped.clear();
			fprintf(stderr, "game panel: keyboard open (%s)\n", vkbd && vkbd->ready() ? "the virtual keyboard is there" : "no virtual keyboard");
			return G_NONE;
		}
		return acts[hit];
	}

	void Settings::gameKeyTap(int code, bool shift, const char* label)
	{
		if (code == -1) { gKeyShift ^= 1; return; }	// the shift toggle (sticky: shift then the letters)
		if (getenv("BANANA_LOG_KEYS")) fprintf(stderr, "game panel: key %d%s\n", code, shift ? " (shift)" : "");
		if (vkbd && vkbd->ready()) vkbd->tap(code, shift || (gKeyShift != 0 && gKeyPage == 0));
		// what the key typed, in the line at the panel's top (the game's own screen shows it as it arrives)
		if (code == KEY_BACKSPACE) { if (!gKeyTyped.empty()) gKeyTyped.pop_back(); }
		else if (code == KEY_ENTER) gKeyTyped.clear();
		else if (label && label[0] && !label[1] && label[0] > ' ' && label[0] < 127)
		{
			gKeyTyped += label;
			if (gKeyTyped.size() > 46) gKeyTyped.erase(0, gKeyTyped.size() - 46);
		}
	}

	void Settings::gameKeysFrame(const SPal& p)
	{
		const VKey* pg = VK_PAGES[gKeyPage < 0 || gKeyPage > 2 ? 0 : gKeyPage];
		text("Keyboard", true, 24, 16, 6, p.ink);
		const std::string typed = gKeyTyped.empty() ? "Tap a key: the game hears it on the other screen" : gKeyTyped;
		text(fit(typed, false, 18, 440), false, 18, 170, 12, gKeyTyped.empty() ? p.muted : p.accent);
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 10; c++)
			{
				const VKey& k = pg[r * 10 + c];
				if (!k.label) continue;
				const float x = VK_X + c * VK_CW, y = VK_Y + r * (VK_CH + VK_RGAP), w = VK_CW - 4;
				const bool shiftOn = k.code == -1 && gKeyShift;
				box(shiftOn ? "pick" : "slot", shiftOn, x, y, w, VK_CH);
				const Color& fg = shiftOn ? p.onPick : p.ink;
				const float tw = text(k.label, true, 20, 0, 0, fg, false, 0, true);
				text(k.label, true, 20, x + (w - tw) / 2, y + VK_CH / 2 - 11, fg);
			}
		// the bar: the pages, shift, and the keys typed the most
		const char* bar[8] = { VK_PAGE_NAMES[0], VK_PAGE_NAMES[1], VK_PAGE_NAMES[2], "SHIFT", "SPACE", "RET", "DEL", "DONE" };
		for (int i = 0; i < 8; i++)
		{
			const float x = 12 + i * 77, w = 73;
			const bool on = (i < 3 && gKeyPage == i) || (i == 3 && gKeyShift);
			box(on ? "pick" : "slot", on, x, VK_BAR_Y, w, VK_BAR_H);
			const Color& fg = on ? p.onPick : p.ink;
			const float tw = text(bar[i], true, 18, 0, 0, fg, false, 0, true);
			text(bar[i], true, 18, x + (w - tw) / 2, VK_BAR_Y + 13, fg);
		}
		text("The game keeps running on the other screen. Done brings the tiles back.", false, 15, 12, 384, p.muted);
	}

	void Settings::gameRender()
	{
		frame++;	// (a frame of the cache's, as render()'s: evict() after it)
		gameFrame();
		evict();
	}

	void Settings::gameFrame()
	{
		SPal p = pal(light);
		int state;
		std::string game;
		unsigned total = 0, got = 0;
		struct Row { std::string title, desc, badge; long long when; unsigned points; };
		std::vector<Row> rows;
		std::string rpText;
		std::vector<RaWatch::Near> nearList;
		{
			std::lock_guard<std::mutex> l(raw->mu);
			gSeen = raw->gen;
			state = raw->state;
			game = raw->game;
			total = (unsigned)raw->all.size();
			for (auto& a : raw->all)
			{
				auto u = raw->unlocked.find(a.id);
				if (u == raw->unlocked.end()) continue;
				got++;
				rows.push_back({ a.title, a.desc, a.badge, u->second, a.points });
			}
			std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.when > b.when; });
			rpText = raw->rp;
			nearList = raw->nearest;
			// with achievements close to done (the emulator's measured progress): the latest unlock, then the closest
			if (rows.size() > (nearList.empty() ? 2u : 1u)) rows.resize(nearList.empty() ? 2 : 1);
			for (auto& r : rows) raw->wantBadge(raw, r.badge);
		}
		be->beginScreen(gScreen);
		box("screen", false, 0, 0, 640, 480);
		if (gKeysOpen) { gameKeysFrame(p); be->endScreen(); return; }	// (the panel is the keyboard while it is open)
		bool pk = companion && companion->active();
		if (gbaPanel.pagesShown())
		{
			gbaPanel.draw();
			be->endScreen();
			return;
		}
		bool gc = !pk && gameComp && gameComp->active();
		if (gc && gcView != GC_GAME)
		{
			gcRender(p);
			gcTabs(p);
			be->endScreen();
			return;
		}
		// ---- the header: the game, the system
		box("bubble", false, 18, 14, 604, 54);
		std::string sys = fit(gSystem, true, 17, 200);
		float sw = text(sys, true, 17, 0, 0, p.muted, false, 0, true);
		const float sysEnd = compOn() ? 574 : 602;	// (the companion's icon at the header's right end)
		if (rpText.empty())
		{
			text(sys, true, 17, sysEnd - sw, 31, p.muted);
			text(fit(gTitle, true, 22, sysEnd - 62 - sw), true, 22, 38, 28, p.ink);
		}
		else
		{
			// the game's rich presence under its name (RetroAchievements, any game with a script: "Lv 23, Viridian Forest")
			text(sys, true, 15, sysEnd - sw * 15 / 17, 20, p.muted);
			text(fit(gTitle, true, 19, sysEnd - 62 - sw), true, 19, 38, 18, p.ink);
			text(fit(rpText, false, 15, 564), false, 15, 38, 44, p.accent);
		}
		// ---- the achievements (or, after a tap on them, the save states)
		if (gStates) gameStatesFrame(p);
		else {
		box("slot", false, 18, 80, 604, 206);
		if (!gStateBase.empty() && gTouch)
		{
			float hw = text("Tap: save states", false, 13, 0, 0, p.muted, false, 0, true);
			text("Tap: save states", false, 13, 600 - hw, 264, p.muted);
		}
		text("Latest achievements", true, 15, 40, 96, p.muted, true, 1.2f);
		if (state == 2 && total)
		{
			std::string v = std::to_string(got) + " / " + std::to_string(total);
			float vw = text(v, true, 15, 0, 0, p.muted, false, 0, true);
			text(v, true, 15, 600 - vw, 96, p.accent);
			float bx = 40, bw = 560, by = 122;
			be->fill(X(bx), X(by), X(bw), X(10), p.line);
			be->fill(X(bx + 2), X(by + 2), X((bw - 4) * got / total), X(6), p.accent);
			if (rows.empty() && nearList.empty()) text("None unlocked yet. Good luck!", false, 18, 40, 150, p.muted);
			for (size_t i = 0; i < rows.size(); i++)
			{
				float y = 146 + i * 66;
				auto& r = rows[i];
				// the badge: its picture when cached, else a tile with a star
				rnds::TexPtr tex;
				auto bi = gBadges.find(r.badge);
				if (bi != gBadges.end()) tex = bi->second;
				else
				{
					bool ready = false;
					{ std::lock_guard<std::mutex> l(raw->mu); auto b = raw->badgeReady.find(r.badge); ready = b != raw->badgeReady.end() && b->second; }
					if (ready)
					{
						rnds::Bitmap b = rnds::decodeFile(std::string(BADGES) + "/" + r.badge + ".png");
						if (!b.empty()) { tex = be->upload(b, true); gBadges[r.badge] = tex; }
					}
				}
				box("bubble", false, 40, y, 56, 56);
				if (tex) be->draw(tex, X(43), X(y + 3), X(50), X(50), Color(1, 1, 1, 1));
				else text("*", true, 30, 58, y + 10, p.accent);
				// when: "just now", "12 min ago", today's time, or the date
				char w[40];
				long long age = (long long)time(nullptr) - r.when;
				if (age < 60) snprintf(w, sizeof w, "just now");
				else if (age < 3600) snprintf(w, sizeof w, "%lld min ago", age / 60);
				else
				{
					time_t tt = (time_t)r.when, nt = time(nullptr);
					struct tm a, b;
					localtime_r(&tt, &a); localtime_r(&nt, &b);
					if (a.tm_yday == b.tm_yday && a.tm_year == b.tm_year) strftime(w, sizeof w, "today %H:%M", &a);
					else strftime(w, sizeof w, "%d %b %Y", &a);
				}
				float ww = text(w, false, 15, 0, 0, p.muted, false, 0, true);
				text(w, false, 15, 600 - ww, y + 6, p.muted);
				text(fit(r.title, true, 19, 480 - ww), true, 19, 110, y + 4, p.ink);
				text(fit(r.desc, false, 15, 490), false, 15, 110, y + 32, p.muted);
			}
		}
		// the achievements closest to done, with their progress ("34/50"), under the latest unlock
		if ((state == 2 && total) || !nearList.empty())
		{
			float y0 = state == 2 && total ? 146 + rows.size() * 66 : 112;
			for (size_t i = 0; i < nearList.size() && y0 + 56 <= 286; i++, y0 += 58)
			{
				auto& n = nearList[i];
				text(lang::tr("Second screen (RetroArch games)", "Next"), true, 15, 40, y0 + 4, p.muted);
				float mw = text(n.measured, true, 17, 0, 0, p.accent, false, 0, true);
				text(n.measured, true, 17, 600 - mw, y0 + 2, p.accent);
				text(fit(n.title, true, 19, 470 - mw), true, 19, 110, y0 + 2, p.ink);
				be->fill(X(110), X(y0 + 32), X(490), X(8), p.line);
				be->fill(X(111), X(y0 + 33), X(488.0f * std::min(100u, n.percent) / 100), X(6), p.accent);
			}
		}
		if (!(state == 2 && total) && nearList.empty())
		{
			const char* msg = state == 0 ? (raw->fromBlock ? "Waiting for the emulator..." : "Waiting for RetroArch...") : state == 1 ? "Looking the game up..." :
				state == 3 ? "This game has no achievements." :
				state == 4 ? "RetroAchievements is off, or its login failed: check the account in Settings > System > First-time setup." :
				"No connection to RetroAchievements: check Wi-Fi.";
			wrapped(msg, 18, 40, 130, 560, 3, p.muted);
		}
		}
		// ---- the tiles: RetroArch's commands by touch, and their buttons
		const int ntiles = vkbd && gTouch ? 6 : 5;
		const float tstep = ntiles == 6 ? 102 : 122, tw = ntiles == 6 ? 96 : 114;
		for (int i = 0; i < ntiles; i++)
		{
			float x = 18 + i * tstep, y = 300;
			bool on = gFlash == i;
			box(on ? "pick" : "slot", on, x, y, tw, 120);
			Color fg = on ? p.onPick : (i == 4 ? Color::hex(0xff8a96) : p.ink);
			int k = std::max(1, (int)std::lround(3 * s));
			float ix = std::round(X(x + tw / 2) - 6 * k), iy = std::round(X(y + 16));
			for (int r = 0; r < 12; r++)
				for (int c = 0; c < 12; c++)
					if (GTILES[i].icon[r][c] == '#') be->fill(ix + c * k, iy + r * k, (float)k, (float)k, fg);
			// (the document's own translation for this panel: its "Fast" is fast-forward, the settings' is speed)
			std::string lb = lang::tr("Second screen (RetroArch games)", GTILES[i].label);
			float lw = text(lb, true, 19, 0, 0, fg, false, 0, true);
			text(lb, true, 19, x + tw / 2 - lw / 2, y + 60, fg);
			std::string kk = GTILES[i].keys;
			float kw = text(kk, false, 14, 0, 0, p.muted, false, 0, true);
			text(kk, false, 14, x + tw / 2 - kw / 2, y + 90, on ? p.onPickAccent : p.muted);
		}
		std::string hint = gTouch ? "Tap a tile, or hold F with the button under it" : "Hold F with the button under each tile";
		float hw = text(hint, false, 15, 0, 0, p.muted, false, 0, true);
		text(hint, false, 15, 320 - hw / 2, 438, p.muted);
		if (compOn()) compIcon();
		be->endScreen();
	}

	// ---- the game panel's drawing bits (the game companion's pages): numbers, chips, bars, the pager. Banana's look:
	// the game panel's boxes, Pixelify Sans, the theme's colours
	namespace
	{
		const uint32_t HP_GREEN = 0x58d068, HP_YELLOW = 0xf8c030, HP_RED = 0xf05848;
	}

	// numbers a character at a time (HP and PP change all the time: one cached texture for each digit, not each value)
	float Settings::num(const std::string& t, float size, float x, float top, const Color& c, bool measureOnly)
	{
		float w = 0;
		for (char ch : t)
		{
			std::string one(1, ch);
			if (ch == ' ') { w += size * 0.3f; continue; }
			w += text(one, true, size, x + w, top, c, false, 0, measureOnly);
		}
		return w;
	}

	void Settings::sprite(const char* const* rows, int h, float x, float y, int k, const std::function<Color(char)>& colour)
	{
		float x0 = std::round(X(x)), y0 = std::round(X(y));
		for (int r = 0; r < h; r++)
			for (int c = 0; rows[r][c]; c++)
				if (rows[r][c] != '.') be->fill(x0 + c * k, y0 + r * k, (float)k, (float)k, colour(rows[r][c]));
	}

	float Settings::chip(const std::string& label, uint32_t rgb, float x, float y, bool measureOnly)
	{
		float w = text(label, true, 12, 0, 0, Color(1, 1, 1, 1), true, 0.5f, true) + 14;
		if (measureOnly) return w;
		be->fill(std::round(X(x)), std::round(X(y)), std::round(X(w)), std::round(X(20)), Color::hex(rgb));
		be->fill(std::round(X(x)), std::round(X(y + 18)), std::round(X(w)), std::round(X(2)), Color(0, 0, 0, 0.25f));	// (its pixel shadow)
		text(label, true, 12, x + 7, y + 4, Color(1, 1, 1, 1), true, 0.5f);
		return w;
	}

	void Settings::hpBar(int hp, int maxHp, float x, float y, float w, uint32_t rgb)
	{
		SPal p = pal(light);
		be->fill(std::round(X(x)), std::round(X(y)), std::round(X(w)), std::round(X(10)), p.line);
		if (maxHp <= 0 || hp <= 0) return;
		float f = std::min(1.0f, (float)hp / maxHp);
		if (!rgb) rgb = f > 0.5f ? HP_GREEN : f > 0.2f ? HP_YELLOW : HP_RED;
		be->fill(std::round(X(x + 2)), std::round(X(y + 2)), std::max(1.0f, std::round(X((w - 4) * f))), std::round(X(6)), Color::hex(rgb));
	}

	// ---- the pager (the game companion's long pages)

	namespace
	{
		const char* const PX_PREV[9] = { "....#", "...##", "..###", ".####", "#####", ".####", "..###", "...##", "....#" };
		const char* const PX_NEXT[9] = { "#....", "##...", "###..", "####.", "#####", "####.", "###..", "##...", "#...." };
	}

	void Settings::pager(const SPal& p, int page, int pages, const std::string& left)
	{
		// the left text and the page in a box; back and on as two buttons at its right, each its whole box to tap
		// (pagerStep), greyed at the first and the last page
		bool paged = pages > 1;
		box("slot", false, 18, 372, paged ? 392 : 604, 40);
		if (!left.empty()) text(fit(left, false, 15, paged ? 280 : 560), false, 15, 34, 384, p.muted);
		if (!paged) return;
		std::string pg = std::to_string(page + 1) + " / " + std::to_string(pages);
		num(pg, 16, 394 - num(pg, 16, 0, 0, p.ink, true), 382, p.ink);
		pagerButtons(p, page, pages, PAGER);
	}

	const Settings::PagerButtons Settings::PAGER = { 418, 372, 98, 40, 8 };

	void Settings::pagerButtons(const SPal& p, int page, int pages, const PagerButtons& b)
	{
		int k = std::max(1, (int)std::lround(2 * s));
		for (int i = 0; i < 2; i++)
		{
			bool on = i ? page < pages - 1 : page > 0;
			float bx = b.x + i * (b.w + b.gap);
			box("slot", false, bx, b.y, b.w, b.h);
			Color c = on ? p.accent : p.line;
			sprite(i ? PX_NEXT : PX_PREV, 9, bx + b.w / 2 - 2.5f * k / s, b.y + b.h / 2 - 4.5f * k / s, k, [&](char) { return c; });
		}
	}

	int Settings::pagerStep(float lx, float ly, const PagerButtons& b) const
	{
		if (ly < b.y - 4 || ly >= b.y + b.h + 6 || lx < b.x - 4) return 0;
		return lx < b.x + b.w + b.gap / 2 ? -1 : 1;
	}

	// ---- the game companion (gamecomp.h): the Pokémon companion's look for any GBA game with a definition. The whole
	// panel: the page's header (its title, "Reference" on a page of notes), its widgets in a box (paged when they don't
	// fit), the tab bar along the bottom (Battle only while the game is in a fight, when its definition can tell; a page
	// whose "when" is false greyed). The game's own colour marks what the theme's accent marks elsewhere. The companion
	// idea is PokeDaisy's (Lidor Itzhari).
	namespace
	{
		const float GC_X = 36, GC_W = 568, GC_TOP = 88;
		std::string gcLower(std::string t) { for (auto& c : t) c = (char)tolower((unsigned char)c); return t; }
		bool gcHpLike(const std::string& l)
		{
			std::string t = gcLower(l);
			return t == "hp" || t.find("health") != std::string::npos || t.find("life") != std::string::npos || t.compare(0, 3, "hp ") == 0;
		}
		std::string gcNum(double v)
		{
			char b[48];
			if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) snprintf(b, sizeof b, "%lld", (long long)std::llround(v));
			else snprintf(b, sizeof b, "%.1f", v);
			return b;
		}
		// the game's colour where it marks text on the panel's dark boxes: lightened when it is too dark to read there
		uint32_t gcInk(uint32_t rgb, bool lightTheme)
		{
			float r = (rgb >> 16 & 255) / 255.0f, g = (rgb >> 8 & 255) / 255.0f, b = (rgb & 255) / 255.0f;
			float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
			float k = lightTheme ? (l > 0.6f ? -0.45f : 0) : (l < 0.45f ? 0.45f - l : 0);
			auto ch = [&](float c) { float v = k >= 0 ? c + (1 - c) * k : c * (1 + k); return (uint32_t)std::lround(std::min(1.0f, std::max(0.0f, v)) * 255); };
			return ch(r) << 16 | ch(g) << 8 | ch(b);
		}
		std::vector<std::string> gcParas(const std::string& t)
		{
			std::vector<std::string> r;
			size_t a = 0, b;
			while ((b = t.find('\n', a)) != std::string::npos) { if (b > a) r.push_back(t.substr(a, b - a)); a = b + 1; }
			if (a < t.size()) r.push_back(t.substr(a));
			return r;
		}
	}

	int Settings::wrapLines(const std::string& t, float size, float width)
	{
		if (!R || t.empty()) return 0;
		rnds::TextStyle st;
		st.font = R; st.size = size;
		return std::max(1, (int)rnds::layoutWrap(rnds::utf8to32(t), st, (int)width).size());
	}

	std::string Settings::gcValue(const gcomp::Val& v, const std::string& enumName, const std::string& format) const
	{
		if (!v.ok) return "-";
		if (v.v.isStr) return v.v.s;
		double n = v.v.n;
		long long i = std::llround(n);
		const gcomp::Def* d = gameComp ? gameComp->game() : nullptr;
		if (!enumName.empty() && d) return d->enumLabel(enumName, (long)i);
		char b[64];
		if (format == "time60" || format == "secs")
		{
			long long sec = format == "time60" ? i / 60 : i;
			if (sec >= 3600) snprintf(b, sizeof b, "%lld:%02lld:%02lld", sec / 3600, sec / 60 % 60, sec % 60);
			else snprintf(b, sizeof b, "%lld:%02lld", sec / 60, sec % 60);
			return b;
		}
		if (format == "hex") { snprintf(b, sizeof b, "0x%llX", (unsigned long long)i); return b; }
		if (format == "pad2" || format == "pad3" || format == "pad6") { snprintf(b, sizeof b, "%0*lld", format[3] - '0', i); return b; }
		std::string t = gcNum(n);
		if (format == "thousands")
		{
			std::string r;
			bool neg = !t.empty() && t[0] == '-';
			std::string digits = neg ? t.substr(1) : t;
			for (size_t k = 0; k < digits.size(); k++) { if (k && (digits.size() - k) % 3 == 0) r += ','; r += digits[k]; }
			return (neg ? "-" : "") + r;
		}
		size_t at = format.find("{}");
		if (at != std::string::npos) return format.substr(0, at) + t + format.substr(at + 2);
		return t;
	}

	std::vector<Settings::GcBlock> Settings::gcBlocks(const gcomp::Page& pg, float width)
	{
		std::vector<GcBlock> out;
		const gcomp::Snapshot& sn = gameComp->snapshot();
		for (auto& w : pg.widgets)
		{
			if (w.when.set)
			{
				gcomp::Val on = gameComp->eval(w.when);
				if (!on.ok || !on.v.truthy()) continue;
			}
			switch (w.type)
			{
			case gcomp::Widget::NUMBER: case gcomp::Widget::TEXT_W: out.push_back({ &w, 0, 32 }); break;
			case gcomp::Widget::BAR: case gcomp::Widget::COUNT: out.push_back({ &w, 0, 48 }); break;
			case gcomp::Widget::FLAGS:
			{
				// chips in rows across the width
				float cx = 0, h = 26;
				for (auto& it : w.items)
				{
					if (it.empty()) continue;
					float cw = chip(it, 0, 0, 0, true);
					if (cx > 0 && cx + cw > width) { cx = 0; h += 26; }
					cx += cw + 6;
				}
				out.push_back({ &w, 0, h + (w.label.empty() ? 6 : 28) });
				break;
			}
			case gcomp::Widget::GRID:
			{
				int n = 0;
				for (auto& it : w.items) n += !it.empty();
				out.push_back({ &w, 0, (float)((n + 3) / 4) * 26 + (w.label.empty() ? 6 : 28) });
				break;
			}
			case gcomp::Widget::STATIC:
			{
				auto paras = gcParas(w.text);
				for (size_t i = 0; i < paras.size(); i++)
					out.push_back({ &w, (int)i, wrapLines(paras[i], 17, width) * 24.0f + 6 + (i == 0 && !w.label.empty() ? 24 : 0) });
				break;
			}
			case gcomp::Widget::KV:
				for (size_t i = 0; i < w.rows.size(); i++)
					out.push_back({ &w, (int)i, std::max(1, wrapLines(w.rows[i].second, 16, width * 0.6f)) * 22.0f + 8 + (i == 0 && !w.label.empty() ? 24 : 0) });
				break;
			case gcomp::Widget::LIST:
			{
				const auto& rows = w.listId < (int)sn.lists.size() ? sn.lists[w.listId] : std::vector<std::vector<gcomp::Val>>();
				out.push_back({ &w, -1, w.label.empty() ? (rows.empty() ? 30.0f : 4.0f) : (rows.empty() ? 54.0f : 26.0f) });
				for (size_t i = 0; i < rows.size(); i++) out.push_back({ &w, (int)i, 34 });
				break;
			}
			}
		}
		return out;
	}

	void Settings::gcBlock(const SPal& p, const GcBlock& b, float x, float y, float w)
	{
		const gcomp::Widget& wd = *b.w;
		const gcomp::Def& d = *gameComp->game();
		const gcomp::Snapshot& sn = gameComp->snapshot();
		Color acc = Color::hex(gcInk(d.accent, light));
		auto heading = [&](const std::string& l) { text(fit(l, true, 14, w), true, 14, x, y + 2, p.muted, true, 1.0f); };
		switch (wd.type)
		{
		case gcomp::Widget::NUMBER: case gcomp::Widget::TEXT_W:
		{
			gcomp::Val v = gameComp->eval(wd.value);
			std::string t = gcValue(v, wd.enumName, wd.format);
			bool digits = v.ok && !v.v.isStr && wd.enumName.empty();
			float vw = digits ? num(t, 19, 0, 0, p.ink, true) : text(fit(t, true, 19, w * 0.6f), true, 19, 0, 0, p.ink, false, 0, true);
			if (digits) num(t, 19, x + w - vw, y + 4, v.ok ? p.ink : p.muted);
			else text(fit(t, true, 19, w * 0.6f), true, 19, x + w - vw, y + 4, v.ok ? p.ink : p.muted);
			text(fit(wd.label, false, 17, w - vw - 16), false, 17, x, y + 6, p.muted);
			be->fill(std::round(X(x)), std::round(X(y + 30)), std::round(X(w)), std::max(1.0f, std::round(X(1))), p.line);
			break;
		}
		case gcomp::Widget::BAR: case gcomp::Widget::COUNT:
		{
			gcomp::Val v = gameComp->eval(wd.value);
			gcomp::Val m = gameComp->eval(wd.type == gcomp::Widget::BAR ? wd.max : wd.total);
			double a = v.ok ? v.v.num() : 0, z = m.ok ? m.v.num() : 0;
			if (wd.type == gcomp::Widget::COUNT && wd.bits && v.ok) a = __builtin_popcount((uint32_t)(int64_t)a);
			std::string t = (v.ok ? gcNum(a) : "-") + " / " + (m.ok ? gcNum(z) : "-");
			float vw = num(t, 16, 0, 0, p.ink, true);
			num(t, 16, x + w - vw, y + 2, v.ok ? p.ink : p.muted);
			text(fit(wd.label, false, 17, w - vw - 16), false, 17, x, y + 2, p.muted);
			uint32_t rgb = wd.color ? wd.color : wd.type == gcomp::Widget::BAR && gcHpLike(wd.label) ? 0 : d.accent;
			if (wd.type == gcomp::Widget::COUNT) rgb = wd.color ? wd.color : d.accent;
			hpBar(v.ok ? (int)std::lround(std::min(a, 1e9)) : 0, m.ok ? (int)std::lround(std::min(z, 1e9)) : 0, x, y + 28, w, rgb);
			break;
		}
		case gcomp::Widget::FLAGS: case gcomp::Widget::GRID:
		{
			float yy = y;
			if (!wd.label.empty()) { heading(wd.label); yy += 24; }
			gcomp::Val v = gameComp->eval(wd.value);
			uint32_t bits = v.ok ? (uint32_t)(int64_t)v.v.num() : 0;
			if (wd.invert) bits = ~bits;
			float cx = 0;
			int k = 0;
			int sq = std::max(1, (int)std::lround(12 * s));
			for (size_t i = 0; i < wd.items.size() && i < 32; i++)
			{
				const std::string& it = wd.items[i];
				if (it.empty()) continue;
				bool on = v.ok && (bits >> i & 1);
				if (wd.type == gcomp::Widget::GRID)
				{
					float gx = x + (k % 4) * (w / 4), gy = yy + (k / 4) * 26;
					be->fill(std::round(X(gx)), std::round(X(gy + 4)), (float)sq, (float)sq, on ? acc : p.line);
					text(fit(it, false, 15, w / 4 - 24), false, 15, gx + 18, gy + 2, on ? p.ink : p.muted);
					k++;
					continue;
				}
				float cw = chip(it, 0, 0, 0, true);
				if (cx > 0 && cx + cw > w) { cx = 0; yy += 26; }
				chip(it, on ? d.accent : 0x4a4a52, x + cx, yy);
				cx += cw + 6;
			}
			break;
		}
		case gcomp::Widget::STATIC:
		{
			auto paras = gcParas(wd.text);
			float yy = y;
			if (b.part == 0 && !wd.label.empty()) { heading(wd.label); yy += 24; }
			if (b.part >= 0 && b.part < (int)paras.size()) wrapped(paras[b.part], 17, x, yy, w, 0, p.ink);
			break;
		}
		case gcomp::Widget::KV:
		{
			float yy = y;
			if (b.part == 0 && !wd.label.empty()) { heading(wd.label); yy += 24; }
			if (b.part >= 0 && b.part < (int)wd.rows.size())
			{
				text(fit(wd.rows[b.part].first, true, 16, w * 0.38f), true, 16, x, yy + 2, acc);
				wrapped(wd.rows[b.part].second, 16, x + w * 0.4f, yy, w * 0.6f, 0, p.ink);
			}
			break;
		}
		case gcomp::Widget::LIST:
		{
			const auto& rows = wd.listId < (int)sn.lists.size() ? sn.lists[wd.listId] : std::vector<std::vector<gcomp::Val>>();
			if (b.part < 0)
			{
				float yy = y;
				if (!wd.label.empty()) { heading(wd.label); yy += 24; }
				if (rows.empty()) text(sn.seen ? "None right now." : "-", false, 16, x, yy + 4, p.muted);
				break;
			}
			if (b.part >= (int)rows.size()) break;
			const auto& row = rows[b.part];
			be->fill(std::round(X(x)), std::round(X(y + 5)), std::round(X(3)), std::round(X(20)), acc);	// (a row to open)
			be->fill(std::round(X(x)), std::round(X(y + 31)), std::round(X(w)), std::max(1.0f, std::round(X(1))), p.line);
			x += 12; w -= 12;
			// the name (the first field), then up to three others at the right, each "label value"
			float right = x + w - 4;
			for (int f = std::min((int)wd.fields.size(), 4) - 1; f >= 1; f--)
			{
				const gcomp::Field& fd = wd.fields[f];
				std::string v = gcValue(row[f], fd.enumName, fd.format);
				bool digits = row[f].ok && !row[f].v.isStr && fd.enumName.empty();
				float vw = digits ? num(v, 16, 0, 0, p.ink, true) : text(fit(v, true, 16, 120), true, 16, 0, 0, p.ink, false, 0, true);
				if (digits) num(v, 16, right - vw, y + 6, p.ink); else text(fit(v, true, 16, 120), true, 16, right - vw, y + 6, p.ink);
				std::string l = fit(fd.label, false, 14, 90);
				float lw = text(l, false, 14, 0, 0, p.muted, false, 0, true);
				text(l, false, 14, right - vw - 6 - lw, y + 8, p.muted);
				right -= vw + lw + 22;
			}
			std::string nm = gcValue(row[0], wd.fields[0].enumName, wd.fields[0].format);
			text(fit(nm, true, 17, std::max(40.0f, right - x - 8)), true, 17, x + 2, y + 6, p.ink);
			gcHits.push_back({ y, y + 32, wd.listId, b.part });
			break;
		}
		}
	}

	void Settings::gcRowView(const SPal& p)
	{
		// a list's row in full: every field, one a line
		const gcomp::Def& d = *gameComp->game();
		const gcomp::Snapshot& sn = gameComp->snapshot();
		const gcomp::Widget* lw = nullptr;
		for (auto& w : d.pages[gcView].widgets) if (w.type == gcomp::Widget::LIST && w.listId == gcList) lw = &w;
		if (!lw || gcList >= (int)sn.lists.size() || gcRow >= (int)sn.lists[gcList].size()) { gcList = gcRow = -1; return; }
		const auto& row = sn.lists[gcList][gcRow];
		box("bubble", false, 18, 14, 604, 54);
		std::string nm = gcValue(row[0], lw->fields[0].enumName, lw->fields[0].format);
		text(fit(nm, true, 22, 420), true, 22, 36, 28, p.ink);
		std::string back = "Tap: back";
		float bw = text(back, false, 15, 0, 0, p.muted, false, 0, true);
		text(back, false, 15, 504 - bw, 32, p.muted);
		gcMenuButton(p);
		box("bubble", false, 18, 76, 604, 336);
		float y = GC_TOP;
		for (size_t f = lw->fields.size() > 1 ? 1 : 0; f < lw->fields.size() && y + 32 <= 404; f++)
		{
			const gcomp::Field& fd = lw->fields[f];
			std::string v = gcValue(row[f], fd.enumName, fd.format);
			bool digits = row[f].ok && !row[f].v.isStr && fd.enumName.empty();
			float vw = digits ? num(v, 19, 0, 0, p.ink, true) : text(fit(v, true, 19, GC_W * 0.6f), true, 19, 0, 0, p.ink, false, 0, true);
			if (digits) num(v, 19, GC_X + GC_W - vw, y + 4, p.ink); else text(fit(v, true, 19, GC_W * 0.6f), true, 19, GC_X + GC_W - vw, y + 4, p.ink);
			text(fit(fd.label, false, 17, GC_W - vw - 16), false, 17, GC_X, y + 6, p.muted);
			be->fill(std::round(X(GC_X)), std::round(X(y + 30)), std::round(X(GC_W)), std::max(1.0f, std::round(X(1))), p.line);
			y += 34;
		}
	}

	void Settings::gcRender(const SPal& p)
	{
		const gcomp::Def& d = *gameComp->game();
		const gcomp::Snapshot& sn = gameComp->snapshot();
		gcHits.clear();
		if (gcView < 0 || gcView >= gcomp::NUM_PAGES) gcView = gcomp::P_STATUS;
		if (gcView == gcomp::P_BATTLE && d.pages[gcView].when.set && !gameComp->pageOn(gcView)) { gcView = gcomp::P_STATUS; gcList = gcRow = -1; }
		gameComp->want(gcView, gcList);
		const gcomp::Page& pg = d.pages[gcView];
		if (gcRow >= 0) { gcRowView(p); if (gcRow >= 0) return; }
		// ---- the header: the page, the game; "Reference" on a page of notes
		box("bubble", false, 18, 14, 604, 54);
		float rw = 0;
		if (pg.reference)
		{
			rw = chip("Reference", 0x6a6a74, 0, 0, true);
			chip("Reference", 0x6a6a74, 504 - rw, 30);
			rw += 12;
		}
		else
		{
			std::string g = fit(d.title, true, 15, 260);
			rw = text(g, true, 15, 0, 0, p.muted, false, 0, true);
			text(g, true, 15, 504 - rw, 32, Color::hex(gcInk(d.accent, light)));
			rw += 12;
		}
		text(fit(pg.title, true, 22, 468 - rw), true, 22, 36, 28, p.ink);
		gcMenuButton(p);
		// ---- the page
		bool live = !pg.reference;
		if (live && !sn.seen)
		{
			box("bubble", false, 18, 76, 604, 336);
			std::string why = !sn.error.empty() ? "RetroArch can't read the game's memory with this emulator: " + sn.error : "Waiting for the game...";
			wrapped(why, 18, 40, 96, 560, 4, p.muted);
			return;
		}
		if (pg.when.set && !gameComp->pageOn(gcView))
		{
			box("bubble", false, 18, 76, 604, 336);
			wrapped("Nothing here right now: this page fills in when the game gets to it.", 18, 40, 96, 560, 3, p.muted);
			return;
		}
		std::vector<GcBlock> blocks = gcBlocks(pg, GC_W);
		// in screens: all of it when it fits the box, else as much as fits above the pager each time
		const float full = 404 - GC_TOP, paged = 360 - GC_TOP;
		float total = 0;
		for (auto& b : blocks) total += b.h + 6;
		std::vector<int> starts = { 0 };
		if (total > full)
		{
			float h = 0;
			for (size_t i = 0; i < blocks.size(); i++)
			{
				if (h > 0 && h + blocks[i].h > paged) { starts.push_back((int)i); h = 0; }
				h += blocks[i].h + 6;
			}
		}
		gcScreens = (int)starts.size();
		gcScreen = std::max(0, std::min(gcScreen, gcScreens - 1));
		box("bubble", false, 18, 76, 604, gcScreens > 1 ? 290 : 336);
		size_t from = starts[gcScreen], to = gcScreen + 1 < gcScreens ? starts[gcScreen + 1] : blocks.size();
		float y = GC_TOP, bottom = (gcScreens > 1 ? 360 : 404);
		for (size_t i = from; i < to; i++)
		{
			if (y + std::min(blocks[i].h, 24.0f) > bottom) break;
			gcBlock(p, blocks[i], GC_X, y, GC_W);
			y += blocks[i].h + 6;
		}
		if (gcScreens > 1) pager(p, gcScreen, gcScreens, "");
	}

	int Settings::gcAudit(const std::function<void(const std::string&)>& log)
	{
		if (!gameComp || !gameComp->active()) return 0;
		const gcomp::Def& d = *gameComp->game();
		std::string f = d.file.substr(d.file.find_last_of('/') + 1);
		int issues = 0;
		SPal p = pal(light);
		for (int i = 0; i < gcomp::NUM_PAGES; i++)
		{
			gcView = i; gcScreen = 0; gcList = gcRow = -1;
			be->beginScreen(gScreen);
			gcRender(p);
			gcTabs(p);
			be->endScreen();
			const gcomp::Page& pg = d.pages[i];
			char b[256];
			if (gcScreens > 2) { snprintf(b, sizeof b, "%s %s: %d screens", f.c_str(), gcomp::pageIdName(i), gcScreens); log(b); issues++; }
			if (pg.tab.empty() && text(pg.title, true, 13, 0, 0, Color(1, 1, 1, 1), true, 0.5f, true) > 604.0f / 8 - 6)
			{ snprintf(b, sizeof b, "%s %s: the title \"%s\" is too long for its tab (give \"tab\")", f.c_str(), gcomp::pageIdName(i), pg.title.c_str()); log(b); issues++; }
			size_t chars = 0;
			for (auto& w : pg.widgets) { chars += w.text.size(); for (auto& r : w.rows) chars += r.second.size(); for (auto& it : w.items) chars += it.size(); }
			if (pg.reference && chars < 60) { snprintf(b, sizeof b, "%s %s: a reference page with %zu characters", f.c_str(), gcomp::pageIdName(i), chars); log(b); issues++; }
		}
		gcView = gcomp::P_STATUS;
		return issues;
	}

	void Settings::gcMenuButton(const SPal& p)
	{
		// Menu at the header's right end, where the menu's icon is (the same spot switches back)
		box("pick", true, 516, 20, 100, 42);
		const float mw = text("Menu", true, 17, 0, 0, p.onPick, false, 0, true);
		text("Menu", true, 17, 566 - mw / 2, 31, p.onPick);
	}

	void Settings::gcTabs(const SPal& p)
	{
		// the tab bar: the seven pages (Battle only in a fight when the game can tell); the menu is the corner's Menu
		const gcomp::Def& d = *gameComp->game();
		box("bubble", false, 18, 424, 604, 46);
		bool show[GC_TABS];
		int n = 0;
		for (int i = 0; i < GC_TABS; i++)
		{
			show[i] = i < gcomp::NUM_PAGES && d.pages[i].present && (i != gcomp::P_BATTLE || !d.pages[i].when.set || gameComp->pageOn(i));
			n += show[i];
		}
		float cell = 604.0f / n, cx = 18;
		Color acc = Color::hex(gcInk(d.accent, light));
		static const char* const GENERIC[gcomp::NUM_PAGES] = { "Status", "Detail", "Battle", "Map", "Bag", "Collect", "Progress" };
		for (int i = 0; i < GC_TABS; i++)
		{
			gcTabW[i] = 0;
			if (!show[i]) continue;
			bool on = gcView == i;
			bool grey = i < gcomp::NUM_PAGES && d.pages[i].when.set && !gameComp->pageOn(i);
			float size = n > 7 ? 13 : n > 6 ? 14 : 16;
			// the tab's own name, else the page's title when it fits, else what the page is
			std::string l = !d.pages[i].tab.empty() ? d.pages[i].tab : d.pages[i].title;
			if (i < gcomp::NUM_PAGES && d.pages[i].tab.empty() && text(l, true, size, 0, 0, p.ink, true, 0.5f, true) > cell - 6) l = GENERIC[i];
			l = fit(l, true, size, cell - 6);
			float w = text(l, true, size, 0, 0, p.ink, true, 0.5f, true);
			float tx = std::round(cx + cell / 2 - w / 2);
			text(l, true, size, tx, 438, on ? acc : grey ? p.line : p.muted, true, 0.5f);
			if (on) be->fill(std::round(X(tx)), std::round(X(458)), std::round(X(w)), std::round(X(3)), acc);
			gcTabX[i] = cx; gcTabW[i] = cell;
			cx += cell;
		}
	}
}
