#include "library.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace banana
{
	static std::string trim(const std::string& s)
	{
		size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
		return a == std::string::npos ? "" : s.substr(a, b - a + 1);
	}

	static std::string lower(std::string s)
	{
		for (auto& c : s) c = (char)tolower((unsigned char)c);
		return s;
	}

	static bool exists(const std::string& p)
	{
		struct stat st;
		return !p.empty() && stat(p.c_str(), &st) == 0;
	}

	static std::string readLine(const std::string& path)
	{
		std::ifstream f(path);
		std::string s;
		std::getline(f, s);
		return trim(s);
	}

	// a name as the engine shows it: no file-name leftovers ("(USA, Europe)", "[!]", underscores), as ROCKNIXDS does
	static std::string cleanTitle(std::string t)
	{
		for (auto& c : t) if (c == '_') c = ' ';
		std::string r = trim(t);
		for (;;)
		{
			if (r.size() < 3) break;
			char e = r.back();
			char o = e == ')' ? '(' : (e == ']' ? '[' : 0);
			if (!o) break;
			size_t at = r.rfind(o);
			if (at == std::string::npos || at == 0) break;
			std::string rest = trim(r.substr(0, at));
			if (rest.empty()) break;
			r = rest;
		}
		return r.empty() ? trim(t) : r;
	}

	static std::string fmtTime(long secs)
	{
		long h = secs / 3600, m = (secs % 3600) / 60;
		char b[32];
		if (h > 0) snprintf(b, sizeof b, "%ldh %02ldm", h, m);
		else snprintf(b, sizeof b, "%ldm", m);
		return b;
	}

	static std::string relDay(time_t t)
	{
		if (t <= 0) return "Never";
		time_t now = time(nullptr);
		struct tm a, b;
		localtime_r(&t, &a);
		localtime_r(&now, &b);
		struct tm da = a, db = b;
		da.tm_hour = db.tm_hour = 12; da.tm_min = db.tm_min = 0; da.tm_sec = db.tm_sec = 0;
		long days = (long)std::lround(difftime(mktime(&db), mktime(&da)) / 86400.0);
		static const char* wd[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
		static const char* mo[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
		if (days <= 0) return "Today";
		if (days == 1) return "Yesterday";
		if (days < 7) return wd[a.tm_wday];
		if (days < 14) return "Last week";
		char buf[32];
		if (days < 330) snprintf(buf, sizeof buf, "%s %d", mo[a.tm_mon], a.tm_mday);
		else snprintf(buf, sizeof buf, "%s %d", mo[a.tm_mon], a.tm_year + 1900);
		return buf;
	}

	// ---- gamelist.xml: just the fields the shelf shows -------------------------------------------------------------
	struct GlEntry { std::string name, genre, date, image, thumb, cart, boxback; };

	static std::string xmlText(const std::string& block, const char* tag)
	{
		std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
		size_t a = block.find(open);
		if (a == std::string::npos) return "";
		a += open.size();
		size_t b = block.find(close, a);
		if (b == std::string::npos) return "";
		std::string s = block.substr(a, b - a), r;
		static const std::pair<const char*, char> ents[] = { { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' } };
		for (size_t i = 0; i < s.size();)
		{
			bool hit = false;
			if (s[i] == '&')
				for (auto& e : ents)
					if (!s.compare(i, strlen(e.first), e.first)) { r += e.second; i += strlen(e.first); hit = true; break; }
			if (!hit) r += s[i++];
		}
		return trim(r);
	}

	static std::map<std::string, GlEntry> readGamelist(const std::string& dir)
	{
		std::map<std::string, GlEntry> out;
		std::ifstream f(dir + "/gamelist.xml");
		if (!f) return out;
		std::stringstream ss;
		ss << f.rdbuf();
		std::string x = ss.str();
		size_t at = 0;
		auto rel = [&dir](std::string p)
		{
			if (p.empty()) return p;
			if (p.compare(0, 2, "./") == 0) return dir + "/" + p.substr(2);
			if (p[0] != '/') return dir + "/" + p;
			return p;
		};
		while ((at = x.find("<game", at)) != std::string::npos)
		{
			size_t end = x.find("</game>", at);
			if (end == std::string::npos) break;
			std::string b = x.substr(at, end - at);
			at = end + 7;
			std::string path = xmlText(b, "path");
			if (path.compare(0, 2, "./") == 0) path = path.substr(2);
			if (path.empty()) continue;
			GlEntry e;
			e.name = xmlText(b, "name"); e.genre = xmlText(b, "genre"); e.date = xmlText(b, "releasedate");
			e.image = rel(xmlText(b, "image")); e.thumb = rel(xmlText(b, "thumbnail"));
			e.cart = rel(xmlText(b, "cartridge")); e.boxback = rel(xmlText(b, "boxback"));
			out[path] = e;
		}
		return out;
	}

	// ---- setup ------------------------------------------------------------------------------------------------------
	Library::Library(const std::string& conf, const std::string& roms, const std::string& data, const std::string& a)
		: confPath(conf), romRoot(roms), dataDir(data), assets(a)
	{
		loadDefs();
		loadLooks();
	}

	// systems.conf: [id] sections of key = value (name, maker, folder, ext, run, perf, always)
	void Library::loadDefs()
	{
		defs.clear();
		std::ifstream f(confPath);
		std::string ln;
		SystemDef* cur = nullptr;
		while (std::getline(f, ln))
		{
			ln = trim(ln);
			if (ln.empty() || ln[0] == '#') continue;
			if (ln[0] == '[' && ln.back() == ']')
			{
				defs.push_back(SystemDef());
				cur = &defs.back();
				cur->id = cur->folder = lower(ln.substr(1, ln.size() - 2));
				continue;
			}
			size_t eq = ln.find('=');
			if (!cur || eq == std::string::npos) continue;
			std::string k = trim(ln.substr(0, eq)), v = trim(ln.substr(eq + 1));
			if (k == "name") cur->name = v;
			else if (k == "maker") cur->maker = v;
			else if (k == "folder") cur->folder = v;
			else if (k == "run") cur->run = v;
			else if (k == "perf") cur->perf = v;
			else if (k == "always") cur->always = v == "yes" || v == "1" || v == "true";
			else if (k == "ext")
			{
				std::istringstream is(v);
				std::string e;
				while (is >> e) cur->exts.push_back(lower(e[0] == '.' ? e.substr(1) : e));
			}
		}
	}

	// the theme's systems.cfg, read the way ROCKNIXDS reads it (RndsEs.cpp)
	void Library::loadLooks()
	{
		looks.clear();
		std::ifstream f(assets + "/systems.cfg");
		std::string ln;
		while (std::getline(f, ln))
		{
			if (ln.empty() || ln[0] == '#') continue;
			if (ln[0] == '@')
			{
				size_t hash = ln.rfind('#');
				if (hash == std::string::npos) continue;
				Look mk;
				mk.accent = rnds::Color::hex((uint32_t)strtoul(ln.c_str() + hash + 1, nullptr, 16));
				mk.hasAccent = true;
				looks["@" + lower(trim(ln.substr(1, hash - 1)))] = mk;
				continue;
			}
			std::istringstream is(ln);
			std::string names, icon, accent, maker;
			is >> names >> icon >> accent;
			std::getline(is, maker);
			maker = trim(maker);
			Look look;
			look.icon = icon == "-" ? "" : assets + "/icons/" + icon;
			if (accent.size() == 7 && accent[0] == '#')
			{
				look.accent = rnds::Color::hex((uint32_t)strtoul(accent.c_str() + 1, nullptr, 16));
				look.hasAccent = true;
			}
			if (maker.size() >= 5 && maker.compare(maker.size() - 5, 5, "[app]") == 0)
			{
				look.app = true;
				maker = trim(maker.substr(0, maker.size() - 5));
			}
			look.maker = maker;
			std::stringstream ns(names);
			std::string n;
			while (std::getline(ns, n, ',')) looks[lower(n)] = look;
		}
	}

	Library::Look Library::lookOf(const std::string& id) const
	{
		auto it = looks.find(lower(id));
		if (it == looks.end()) it = looks.find("*");
		return it == looks.end() ? Look() : it->second;
	}

	void Library::loadState()
	{
		favGames.clear(); favSystems.clear(); stats.clear();
		std::string ln;
		{
			std::ifstream f(dataDir + "/favorites.txt");
			while (std::getline(f, ln)) if (!trim(ln).empty()) favGames.insert(trim(ln));
		}
		{
			std::ifstream f(dataDir + "/favorite-systems.txt");
			while (std::getline(f, ln)) if (!trim(ln).empty()) favSystems.insert(trim(ln));
		}
		{
			// <system>/<rel> TAB plays TAB seconds TAB last (epoch)
			std::ifstream f(dataDir + "/playstats.tsv");
			while (std::getline(f, ln))
			{
				std::vector<std::string> c;
				std::stringstream ls(ln);
				std::string part;
				while (std::getline(ls, part, '\t')) c.push_back(part);
				if (c.size() < 4) continue;
				Stat s;
				s.plays = atoi(c[1].c_str()); s.seconds = atol(c[2].c_str()); s.last = (time_t)atoll(c[3].c_str());
				stats[c[0]] = s;
			}
		}
	}

	// written beside and renamed over, so a power cut leaves the old file or the new one
	static void writeAtomic(const std::string& path, const std::string& body)
	{
		std::string tmp = path + ".new";
		FILE* f = fopen(tmp.c_str(), "w");
		if (!f) return;
		fwrite(body.data(), 1, body.size(), f);
		fflush(f);
		fsync(fileno(f));
		fclose(f);
		rename(tmp.c_str(), path.c_str());
	}

	void Library::saveFavs()
	{
		std::string a, b;
		for (auto& g : favGames) a += g + "\n";
		for (auto& s : favSystems) b += s + "\n";
		writeAtomic(dataDir + "/favorites.txt", a);
		writeAtomic(dataDir + "/favorite-systems.txt", b);
	}

	void Library::saveStats()
	{
		std::string out;
		for (auto& kv : stats)
			out += kv.first + "\t" + std::to_string(kv.second.plays) + "\t" + std::to_string(kv.second.seconds) + "\t" + std::to_string((long long)kv.second.last) + "\n";
		writeAtomic(dataDir + "/playstats.tsv", out);
	}

	static void listRoms(const std::string& dir, const std::string& relDir, const std::vector<std::string>& exts, std::vector<std::string>& out, int depth)
	{
		DIR* d = opendir(dir.c_str());
		if (!d) return;
		while (dirent* e = readdir(d))
		{
			if (e->d_name[0] == '.') continue;
			std::string name = e->d_name, full = dir + "/" + name, rel = relDir.empty() ? name : relDir + "/" + name;
			struct stat st;
			if (stat(full.c_str(), &st)) continue;
			if (S_ISDIR(st.st_mode))
			{
				// art and ES's own folders are not games
				std::string ln = lower(name);
				if (depth < 2 && ln != "media" && ln != "images" && ln != "videos" && ln != "manuals" && ln != "downloaded_images")
					listRoms(full, rel, exts, out, depth + 1);
				continue;
			}
			size_t dot = name.rfind('.');
			if (dot == std::string::npos) continue;
			std::string ext = lower(name.substr(dot + 1));
			if (std::find(exts.begin(), exts.end(), ext) != exts.end()) out.push_back(rel);
		}
		closedir(d);
	}

	// media/<kind>/<rom name>.png or .jpg
	static std::string mediaFile(const std::string& sysDir, const char* kind, const std::string& stem)
	{
		for (const char* ext : { ".png", ".jpg" })
		{
			std::string p = sysDir + "/media/" + kind + "/" + stem + ext;
			if (exists(p)) return p;
		}
		return "";
	}

	std::vector<std::string> Library::watchDirs() const
	{
		std::vector<std::string> r = { romRoot };
		for (auto& d : defs)
		{
			std::string dir = romRoot + "/" + d.folder;
			r.push_back(dir);
			r.push_back(dir + "/media");
			for (const char* m : { "box", "cart", "label" }) r.push_back(dir + "/media/" + m);
		}
		return r;
	}

	void Library::scan()
	{
		loadState();
		shelves.clear();
		for (auto& def : defs)
		{
			Shelf sh;
			sh.def = def;
			sh.order = (int)(&def - &defs[0]);
			std::string dir = romRoot + "/" + def.folder;
			std::vector<std::string> rels;
			listRoms(dir, "", def.exts, rels, 0);
			auto gl = readGamelist(dir);
			for (auto& rel : rels)
			{
				Game g;
				g.rel = rel; g.path = dir + "/" + rel;
				std::string base = rel.substr(rel.rfind('/') == std::string::npos ? 0 : rel.rfind('/') + 1);
				std::string stem = base.substr(0, base.rfind('.'));
				g.title = cleanTitle(stem);
				auto it = gl.find(rel);
				if (it != gl.end())
				{
					if (!it->second.name.empty()) g.title = cleanTitle(it->second.name);
					std::string genre = it->second.genre;
					size_t cut = genre.find_first_of(",;/|");
					g.genre = trim(cut == std::string::npos ? genre : genre.substr(0, cut));
					const std::string& d = it->second.date;
					if (d.size() >= 4 && d.compare(0, 4, "0000") && std::all_of(d.begin(), d.begin() + 4, ::isdigit)) g.year = d.substr(0, 4);
					g.box = exists(it->second.thumb) ? it->second.thumb : (exists(it->second.image) ? it->second.image : "");
					if (exists(it->second.cart)) g.cart = it->second.cart;
					if (exists(it->second.boxback)) g.label = it->second.boxback;
				}
				if (g.box.empty()) g.box = mediaFile(dir, "box", stem);
				if (g.cart.empty()) g.cart = mediaFile(dir, "cart", stem);
				if (g.label.empty()) g.label = mediaFile(dir, "label", stem);
				std::string key = def.id + "/" + rel;
				auto st = stats.find(key);
				if (st != stats.end()) { g.plays = st->second.plays; g.seconds = st->second.seconds; g.last = st->second.last; }
				g.fav = favGames.count(key) > 0;
				sh.games.push_back(g);
			}
			std::sort(sh.games.begin(), sh.games.end(), [](const Game& a, const Game& b) { return lower(a.title) < lower(b.title); });
			if (!sh.games.empty() || def.always) shelves.push_back(sh);
		}
		arrange();
		if (lib >= (int)shelves.size()) lib = 0;
	}

	// the shelf's order and the Favorites collection, from the systems' games and the favourites as they are now
	void Library::arrange()
	{
		shelves.erase(std::remove_if(shelves.begin(), shelves.end(), [](const Shelf& s) { return s.favorites; }), shelves.end());
		// the configured order, favourite systems first
		std::stable_sort(shelves.begin(), shelves.end(), [this](const Shelf& a, const Shelf& b)
		{
			int fa = (int)favSystems.count(a.def.id), fb = (int)favSystems.count(b.def.id);
			return fa != fb ? fa > fb : a.order < b.order;
		});
		// Favorites: a collection after the systems (its place among them as a favourite itself), when not empty
		Shelf fav;
		fav.favorites = true;
		fav.order = 1 << 20;
		fav.def.id = "favorites"; fav.def.name = "Favorites"; fav.def.maker = "Collection";
		for (int s = 0; s < (int)shelves.size(); s++)
			for (auto& g : shelves[s].games)
				if (g.fav) { fav.games.push_back(g); fav.sysOf.push_back(s); }
		std::sort(fav.games.begin(), fav.games.end(), [](const Game& a, const Game& b) { return lower(a.title) < lower(b.title); });
		// sysOf follows the sort: find each game's system again by its path
		for (size_t k = 0; k < fav.games.size(); k++)
			for (int s = 0; s < (int)shelves.size(); s++)
				if (fav.games[k].path.compare(0, romRoot.size() + shelves[s].def.folder.size() + 2, romRoot + "/" + shelves[s].def.folder + "/") == 0)
					{ fav.sysOf[k] = s; break; }
		if (!fav.games.empty())
		{
			// after the favourite systems when the collection is starred itself, else at the end
			size_t at = shelves.size();
			if (favSystems.count("favorites"))
				for (at = 0; at < shelves.size() && favSystems.count(shelves[at].def.id); at++) {}
			shelves.insert(shelves.begin() + (long)at, fav);
		}
		if (lib >= (int)shelves.size()) lib = 0;
		changed = false;
	}

	Game* Library::sourceOf(int shelf, int game, int* sys)
	{
		if (shelf < 0 || shelf >= (int)shelves.size()) return nullptr;
		Shelf& sh = shelves[shelf];
		if (game < 0 || game >= (int)sh.games.size()) return nullptr;
		if (!sh.favorites) { if (sys) *sys = shelf; return &sh.games[game]; }
		int s = sh.sysOf[game];
		if (sys) *sys = s;
		for (auto& g : shelves[s].games)
			if (g.path == sh.games[game].path) return &g;
		return nullptr;
	}

	const SystemDef& Library::systemOf(int game) const
	{
		const Shelf& sh = shelves[lib];
		if (sh.favorites && game >= 0 && game < (int)sh.sysOf.size()) return shelves[sh.sysOf[game]].def;
		return sh.def;
	}

	void Library::recordPlay(int game, long seconds)
	{
		int sys = lib;
		Game* g = sourceOf(lib, game, &sys);
		if (!g) return;
		Stat& st = stats[shelves[sys].def.id + "/" + g->rel];
		st.plays++;
		st.seconds += std::max(0L, seconds);
		st.last = time(nullptr);
		saveStats();
		g->plays = st.plays; g->seconds = st.seconds; g->last = st.last;
		if (shelves[lib].favorites) { Game& f = shelves[lib].games[game]; f.plays = st.plays; f.seconds = st.seconds; f.last = st.last; }
	}

	// ---- Source -----------------------------------------------------------------------------------------------------
	int Library::resumeIndex(const Shelf& s) const
	{
		int best = -1;
		for (int i = 0; i < (int)s.games.size(); i++)
			if (s.games[i].last > 0 && (best < 0 || s.games[i].last > s.games[best].last)) best = i;
		return best >= 0 ? best : (s.games.empty() ? -1 : 0);
	}

	rnds::SysInfo Library::system(int i)
	{
		rnds::SysInfo r;
		if (i < 0 || i >= (int)shelves.size()) return r;
		const Shelf& s = shelves[i];
		Look look = lookOf(s.def.id);
		r.id = s.def.id;
		r.name = s.def.name.empty() ? s.def.id : s.def.name;
		r.maker = !s.def.maker.empty() ? s.def.maker : look.maker;
		r.accent = look.hasAccent ? look.accent : rnds::Color::hex(0x3d8ec4);
		if (!look.hasAccent)
		{
			auto mk = looks.find("@" + lower(r.maker));
			if (mk != looks.end()) r.accent = mk->second.accent;
		}
		r.icon = look.icon;
		r.app = look.app;
		r.games = (int)s.games.size();
		long secs = 0;
		for (auto& g : s.games) { if (g.plays > 0) r.played++; secs += g.seconds; }
		r.time = fmtTime(secs);
		r.fav = favSystems.count(s.def.id) > 0;
		int res = resumeIndex(s);
		if (res >= 0)
		{
			const Game& g = s.games[res];
			r.hasResume = true;
			r.resumeEyebrow = g.last > 0 ? "Last played" : "Not played yet";
			r.resumeTitle = g.title;
			r.resumeBox = g.box; r.resumeCart = g.cart; r.resumeLabel = g.label;
		}
		return r;
	}

	int Library::gameCount()
	{
		return lib >= 0 && lib < (int)shelves.size() ? (int)shelves[lib].games.size() : 0;
	}

	rnds::GameInfo Library::game(int i)
	{
		rnds::GameInfo r;
		if (i < 0 || i >= gameCount()) return r;
		const Game& g = shelves[lib].games[i];
		r.title = g.title;
		r.sub = !g.genre.empty() && !g.year.empty() ? g.genre + " · " + g.year : (!g.genre.empty() ? g.genre : (!g.year.empty() ? g.year : systemOf(i).name));
		r.plays = std::to_string(g.plays);
		r.last = relDay(g.last);
		r.time = fmtTime(g.seconds);
		r.box = g.box; r.cart = g.cart; r.label = g.label;
		std::string ext = lower(g.path.substr(g.path.rfind('.') + 1));
		// the DS ROM's own icon when there is no art (from inside a .zip too)
		if (ext == "nds" || (ext == "zip" && systemOf(i).id == "nds")) r.rom = g.path;
		r.icon = lookOf(systemOf(i).id).icon;
		r.achLabel = "No achievements";
		r.fav = g.fav;
		return r;
	}

	std::string Library::clockText()
	{
		time_t now = time(nullptr);
		struct tm t;
		localtime_r(&now, &t);
		char b[16];
		strftime(b, sizeof b, twelveHour ? "%I:%M %p" : "%H:%M", &t);
		return b;
	}

	std::string Library::dateText()
	{
		time_t now = time(nullptr);
		struct tm t;
		localtime_r(&now, &t);
		char b[16];
		strftime(b, sizeof b, "%m/%d", &t);
		return b;
	}

	Library::Power Library::power()
	{
		if (testPower.percent >= 0) return testPower;
		Power r;
		DIR* d = opendir("/sys/class/power_supply");
		if (!d) return r;
		while (dirent* e = readdir(d))
		{
			if (e->d_name[0] == '.') continue;
			std::string base = std::string("/sys/class/power_supply/") + e->d_name;
			if (readLine(base + "/type") != "Battery") continue;
			std::string cap = readLine(base + "/capacity");
			if (cap.empty()) continue;
			r.percent = std::min(100, std::max(0, atoi(cap.c_str())));
			std::string st = readLine(base + "/status");
			r.charging = st == "Charging" || st == "Full";
			break;
		}
		closedir(d);
		return r;
	}

	float Library::battery()
	{
		Power p = power();
		return p.percent < 0 ? -1 : p.percent / 100.0f;
	}

	bool Library::online()
	{
		return readLine("/sys/class/net/wlan0/operstate") == "up";
	}

	int Library::openLibrary(int sys)
	{
		if (sys < 0 || sys >= (int)shelves.size()) return 0;
		lib = sys;
		int r = resumeIndex(shelves[sys]);
		return r < 0 ? 0 : r;
	}

	int Library::resumeGame(int sys)
	{
		if (sys < 0 || sys >= (int)shelves.size()) return -1;
		lib = sys;
		return resumeIndex(shelves[sys]);
	}

	void Library::toggleSystemFav(int sys)
	{
		if (sys < 0 || sys >= (int)shelves.size()) return;
		const std::string& id = shelves[sys].def.id;
		if (!favSystems.erase(id)) favSystems.insert(id);
		saveFavs();
		changed = true;
	}

	void Library::toggleGameFav(int game)
	{
		int sys = lib;
		Game* g = sourceOf(lib, game, &sys);
		if (!g) return;
		std::string key = shelves[sys].def.id + "/" + g->rel;
		g->fav = !g->fav;
		if (g->fav) favGames.insert(key); else favGames.erase(key);
		if (shelves[lib].favorites) shelves[lib].games[game].fav = g->fav;
		saveFavs();
		changed = true;
	}

	// rcS couldn't mount the card's data partition (a tmpfs stands in for it)
	static bool dataFailed() { return access("/run/banana/data-failed", F_OK) == 0; }

	std::string Library::emptyTitle()
	{
		if (dataFailed()) return "Can't read the card's games";
		return lib < (int)shelves.size() && shelves[lib].favorites ? "Nothing here yet" : "No games yet";
	}

	std::string Library::emptyHint()
	{
		if (dataFailed()) return "The BANANA partition needs exFAT";
		if (lib >= (int)shelves.size()) return "";
		const Shelf& s = shelves[lib];
		if (s.favorites) return "Y on a game adds it here";
		std::string ext = s.def.exts.empty() ? "" : "." + s.def.exts[0] + " ";
		return "Copy " + ext + "files to roms/" + s.def.folder;
	}
}
