// banana-ui: a game's RetroAchievements as a list - the systems' consoles, the server's replies, the card's cache, the
// ROM's hash (rcheevos' rhash) and the Achievements page's loader thread (achievements.h)
#include "achievements.h"
#include "json.h"
#include "settings_curl.h"
#include "zipdir.h"
extern "C" {
#include "rc_hash.h"
}
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace banana::ach
{
	uint32_t consoleFor(const std::string& id)
	{
		// BananaOS' systems (etc/banana/systems.conf) and the ids a card's own systems.conf may add for the cores RetroArch
		// has; rhash hashes each of these. None for the rest: C64 and the ZX Spectrum have no sets, Game & Watch (MAME)
		// and PICO-8 none rhash can make, the apps and Ports aren't games RetroAchievements knows
		static const struct { const char* id; uint32_t console; } MAP[] = {
			{ "nds", RC_CONSOLE_NINTENDO_DS }, { "gba", RC_CONSOLE_GAMEBOY_ADVANCE }, { "gbc", RC_CONSOLE_GAMEBOY_COLOR },
			{ "gb", RC_CONSOLE_GAMEBOY }, { "nes", RC_CONSOLE_NINTENDO }, { "fds", RC_CONSOLE_FAMICOM_DISK_SYSTEM },
			{ "snes", RC_CONSOLE_SUPER_NINTENDO }, { "megadrive", RC_CONSOLE_MEGA_DRIVE }, { "genesis", RC_CONSOLE_MEGA_DRIVE },
			{ "psx", RC_CONSOLE_PLAYSTATION }, { "n64", RC_CONSOLE_NINTENDO_64 }, { "mastersystem", RC_CONSOLE_MASTER_SYSTEM },
			{ "gamegear", RC_CONSOLE_GAME_GEAR }, { "sg-1000", RC_CONSOLE_SG1000 }, { "32x", RC_CONSOLE_SEGA_32X },
			{ "pcengine", RC_CONSOLE_PC_ENGINE }, { "atari2600", RC_CONSOLE_ATARI_2600 }, { "atari7800", RC_CONSOLE_ATARI_7800 },
			{ "atarilynx", RC_CONSOLE_ATARI_LYNX }, { "ngp", RC_CONSOLE_NEOGEO_POCKET }, { "ngpc", RC_CONSOLE_NEOGEO_POCKET },
			{ "wonderswan", RC_CONSOLE_WONDERSWAN }, { "wonderswancolor", RC_CONSOLE_WONDERSWAN }, { "virtualboy", RC_CONSOLE_VIRTUAL_BOY },
			{ "pokemini", RC_CONSOLE_POKEMON_MINI }, { "colecovision", RC_CONSOLE_COLECOVISION }, { "msx", RC_CONSOLE_MSX },
		};
		for (auto& m : MAP) if (id == m.id) return m.console;
		return 0;
	}

	int List::unlocked() const { int n = 0; for (auto& i : items) n += i.unlocked(); return n; }
	unsigned List::points() const { unsigned n = 0; for (auto& i : items) n += i.points; return n; }
	unsigned List::pointsEarned() const { unsigned n = 0; for (auto& i : items) if (i.unlocked()) n += i.points; return n; }

	// ---- the server's replies -----------------------------------------------------------------------------------
	namespace
	{
		bool success(const JPtr& j) { return j && j->isObj() && j->flag("Success"); }
		uint32_t idOf(const JVal* v) { return v && v->isNum() && v->n > 0 && v->n < 4294967296.0 ? (uint32_t)v->n : 0; }
	}

	bool parsePatch(const std::string& json, List& l)
	{
		JPtr j = parseJson(json);
		if (!success(j)) return false;
		const JVal* pd = j->get("PatchData");
		if (!pd || !pd->isObj()) return false;
		List out;
		out.gameId = idOf(pd->get("ID"));
		out.title = pd->str("Title");
		if (const JVal* a = pd->get("Achievements"); a && a->isArr())
			for (auto& o : a->arr)
			{
				if (!o || !o->isObj() || o->num("Flags") != 3) continue;	// core only (5: unofficial)
				Item it;
				it.id = idOf(o->get("ID"));
				if (!it.id || it.id == 101000001) continue;	// (the server's "Warning: Unknown Emulator")
				it.title = o->str("Title");
				it.desc = o->str("Description");
				it.badge = o->str("BadgeName");
				it.points = (unsigned)std::max(0.0, o->num("Points"));
				out.items.push_back(it);
			}
		l = out;
		return true;
	}

	bool parseUnlocks(const std::string& json, bool hardcore, List& l)
	{
		JPtr j = parseJson(json);
		if (!success(j)) return false;
		std::set<uint32_t> ids;
		if (const JVal* u = j->get("UserUnlocks"); u && u->isArr())
			for (auto& v : u->arr)
				if (uint32_t id = idOf(v.get()); id) ids.insert(id);
		for (auto& it : l.items)
		{
			long long& t = hardcore ? it.hard : it.soft;
			if (ids.count(it.id)) { if (!t) t = UNKNOWN; }
			else t = 0;
		}
		return true;
	}

	void keepTimes(List& fresh, const List& old)
	{
		std::map<uint32_t, const Item*> was;
		for (auto& i : old.items) was[i.id] = &i;
		for (auto& i : fresh.items)
		{
			auto w = was.find(i.id);
			if (w == was.end()) continue;
			if (i.soft == UNKNOWN && w->second->soft > UNKNOWN) i.soft = w->second->soft;
			if (i.hard == UNKNOWN && w->second->hard > UNKNOWN) i.hard = w->second->hard;
		}
	}

	// ---- the card's cache ---------------------------------------------------------------------------------------
	namespace
	{
		std::mutex cacheMu;	// (the page's loader and the game panel's thread both write)
		std::string esc(const std::string& s)
		{
			std::string r;
			for (char c : s) r += c == '\\' ? "\\\\" : c == '\t' ? "\\t" : c == '\n' ? "\\n" : c == '\r' ? "\\r" : std::string(1, c);
			return r;
		}
		std::string unesc(const std::string& s)
		{
			std::string r;
			for (size_t i = 0; i < s.size(); i++)
			{
				if (s[i] != '\\' || i + 1 >= s.size()) { r += s[i]; continue; }
				char c = s[++i];
				r += c == 't' ? '\t' : c == 'n' ? '\n' : c == 'r' ? '\r' : c;
			}
			return r;
		}
		std::vector<std::string> fields(const std::string& line)
		{
			std::vector<std::string> f;
			size_t a = 0, b;
			while ((b = line.find('\t', a)) != std::string::npos) { f.push_back(line.substr(a, b - a)); a = b + 1; }
			f.push_back(line.substr(a));
			return f;
		}
		bool mkdirs(const std::string& dir)
		{
			for (size_t at = 1; at <= dir.size(); at++)
				if (at == dir.size() || dir[at] == '/')
					if (mkdir(dir.substr(0, at).c_str(), 0755) != 0 && errno != EEXIST) return false;
			return true;
		}
		// written beside and renamed over, synced: a power cut leaves the old file or the new one
		bool writeFile(const std::string& path, const std::string& body)
		{
			std::string tmp = path + ".part";
			FILE* f = fopen(tmp.c_str(), "wb");
			if (!f) return false;
			bool ok = fwrite(body.data(), 1, body.size(), f) == body.size();
			ok &= fflush(f) == 0;
			ok &= fsync(fileno(f)) == 0;
			ok &= fclose(f) == 0;
			if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
			return true;
		}
		long long num(const std::string& s) { return s.empty() ? -1 : strtoll(s.c_str(), nullptr, 10); }
		const char* const LIST_MAGIC = "banana-achievements 1";
	}

	RomKey romKey(const std::string& path)
	{
		RomKey k;
		k.path = path;
		struct stat st;
		if (stat(path.c_str(), &st) == 0) { k.size = (long long)st.st_size; k.mtime = (long long)st.st_mtime; }
		return k;
	}

	bool readId(const std::string& dir, const RomKey& k, std::string& hash, uint32_t& id)
	{
		std::lock_guard<std::mutex> l(cacheMu);
		std::ifstream f(dir + "/ids.tsv");
		const std::string p = esc(k.path);
		for (std::string line; std::getline(f, line);)
		{
			auto v = fields(line);
			if (v.size() != 5 || v[0] != p) continue;
			if (num(v[1]) != k.size || num(v[2]) != k.mtime) return false;
			hash = v[3];
			id = (uint32_t)strtoul(v[4].c_str(), nullptr, 10);
			return true;
		}
		return false;
	}

	bool writeId(const std::string& dir, const RomKey& k, const std::string& hash, uint32_t id)
	{
		std::lock_guard<std::mutex> l(cacheMu);
		if (!mkdirs(dir)) return false;
		std::string out;
		const std::string p = esc(k.path);
		{
			std::ifstream f(dir + "/ids.tsv");
			for (std::string line; std::getline(f, line);)
			{
				auto v = fields(line);
				if (v.size() == 5 && v[0] != p) out += line + "\n";
			}
		}
		out += p + "\t" + std::to_string(k.size) + "\t" + std::to_string(k.mtime) + "\t" + esc(hash) + "\t" + std::to_string(id) + "\n";
		return writeFile(dir + "/ids.tsv", out);
	}

	bool readList(const std::string& dir, uint32_t gameId, List& out)
	{
		std::lock_guard<std::mutex> l(cacheMu);
		std::ifstream f(dir + "/game-" + std::to_string(gameId) + ".tsv");
		std::string line;
		if (!std::getline(f, line) || line != LIST_MAGIC) return false;
		if (!std::getline(f, line)) return false;
		auto g = fields(line);
		if (g.size() != 4 || g[0] != "game" || num(g[1]) != (long long)gameId) return false;
		List r;
		r.gameId = gameId;
		r.fetched = num(g[2]);
		r.title = unesc(g[3]);
		while (std::getline(f, line))
		{
			auto v = fields(line);
			if (v.size() != 8 || v[0] != "a") return false;
			Item it;
			it.id = (uint32_t)strtoul(v[1].c_str(), nullptr, 10);
			it.points = (unsigned)strtoul(v[2].c_str(), nullptr, 10);
			it.soft = num(v[3]); it.hard = num(v[4]);
			if (!it.id || it.soft < 0 || it.hard < 0) return false;
			it.badge = unesc(v[5]); it.title = unesc(v[6]); it.desc = unesc(v[7]);
			r.items.push_back(it);
		}
		out = r;
		return true;
	}

	bool writeList(const std::string& dir, const List& lst)
	{
		std::lock_guard<std::mutex> l(cacheMu);
		if (!lst.gameId || !mkdirs(dir)) return false;
		std::ostringstream o;
		o << LIST_MAGIC << "\ngame\t" << lst.gameId << "\t" << lst.fetched << "\t" << esc(lst.title) << "\n";
		for (auto& i : lst.items)
			o << "a\t" << i.id << "\t" << i.points << "\t" << i.soft << "\t" << i.hard << "\t" << esc(i.badge) << "\t" << esc(i.title) << "\t" << esc(i.desc) << "\n";
		return writeFile(dir + "/game-" + std::to_string(lst.gameId) + ".tsv", o.str());
	}

	// ---- the ROM's hash -----------------------------------------------------------------------------------------
	std::string hashRom(const std::string& path, uint32_t console, const std::string& tmpDir)
	{
		static std::mutex mu;	// (rhash keeps its readers in globals)
		std::lock_guard<std::mutex> l(mu);
		static bool init = false;
		if (!init) { rc_hash_init_default_cdreader(); init = true; }
		if (!console) return "";
		char hash[33] = {};
		std::string lower = path;
		for (auto& c : lower) c = (char)tolower((unsigned char)c);
		// (a .7z: banana-ui doesn't unpack those; its bytes hashed as the game's would only find "no achievements")
		if (lower.size() >= 3 && lower.compare(lower.size() - 3, 3, ".7z") == 0) return "";
		if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".zip") != 0)
			return rc_hash_generate_from_file(hash, console, path.c_str()) ? std::string(hash) : "";
		// a .zip: its game (the largest member), as RetroArch hashes what it unpacks
		ZipDir z;
		if (!z.open(path)) return "";
		const ZipEntry* best = nullptr;
		for (auto& e : z.entries()) if (!e.dir() && !e.encrypted() && (!best || e.size > best->size)) best = &e;
		if (!best || !best->size) return "";
		if (best->size <= 64u << 20)
		{
			std::vector<uint8_t> buf;
			buf.reserve((size_t)best->size);
			if (!z.read(*best, [&](const uint8_t* p, size_t n) { buf.insert(buf.end(), p, p + n); return true; }) || buf.size() != best->size) return "";
			return rc_hash_generate_from_buffer(hash, console, buf.data(), buf.size()) ? std::string(hash) : "";
		}
		// (too big for memory: a DS game) through a file beside the cache, with the member's extension
		std::string ext = best->name.substr(best->name.find_last_of('.') == std::string::npos ? best->name.size() : best->name.find_last_of('.'));
		if (ext.find('/') != std::string::npos) ext.clear();
		if (!mkdirs(tmpDir)) return "";
		std::string tmp = tmpDir + "/hashing-" + std::to_string(getpid()) + ext;
		FILE* f = fopen(tmp.c_str(), "wb");
		if (!f) return "";
		bool ok = z.read(*best, [&](const uint8_t* p, size_t n) { return fwrite(p, 1, n, f) == n; });
		ok &= fclose(f) == 0;
		std::string r = ok && rc_hash_generate_from_file(hash, console, tmp.c_str()) ? std::string(hash) : "";
		unlink(tmp.c_str());
		return r;
	}

	// ---- the server ---------------------------------------------------------------------------------------------
	namespace
	{
		std::string param(const std::string& fields, const std::string& key)
		{
			size_t at = 0;
			while (at < fields.size())
			{
				size_t e = fields.find('&', at);
				if (e == std::string::npos) e = fields.size();
				if (fields.compare(at, key.size() + 1, key + "=") == 0) return fields.substr(at + key.size() + 1, e - at - key.size() - 1);
				at = e + 1;
			}
			return "";
		}
		std::string slurp(const std::string& path)
		{
			std::ifstream f(path, std::ios::binary);
			if (!f) return "";
			std::ostringstream b;
			b << f.rdbuf();
			return b.str();
		}
		bool get(const std::string& url, std::string& body, long& status)
		{
			Curl cu;
			if (!cu.load()) return false;
			void* h = cu.init();
			if (!h) return false;
			enum { WRITEDATA = 10001, URL = 10002, USERAGENT = 10018, WRITEFUNCTION = 20011, TIMEOUT = 13, NOSIGNAL = 99, RESPONSE_CODE = 0x200002 };
			cu.setopt(h, URL, url.c_str());
			cu.setopt(h, USERAGENT, "BananaOS/0.2 rcheevos");
			cu.setopt(h, NOSIGNAL, 1L);
			cu.setopt(h, TIMEOUT, 20L);
			cu.setopt(h, WRITEFUNCTION, onBody);
			cu.setopt(h, WRITEDATA, &body);
			bool ok = cu.perform(h) == 0;
			if (ok) cu.getinfo(h, RESPONSE_CODE, &status);
			cu.cleanup(h);
			return ok;
		}
	}

	std::string post(const std::string& fields)
	{
		if (const char* fx = getenv("BANANA_RA_FIXTURE"); fx && *fx)
		{
			const std::string r = param(fields, "r"), h = param(fields, "h");
			if (FILE* lg = fopen((std::string(fx) + "/requests.log").c_str(), "a"))
			{
				fprintf(lg, "r=%s g=%s m=%s h=%s\n", r.c_str(), param(fields, "g").c_str(), param(fields, "m").c_str(), h.c_str());
				fclose(lg);
			}
			return slurp(std::string(fx) + "/" + r + (r == "unlocks" ? "-h" + h : "") + ".json");
		}
		Curl cu;
		if (!cu.load()) return "";
		void* h = cu.init();
		if (!h) return "";
		enum { WRITEDATA = 10001, URL = 10002, POSTFIELDS = 10015, USERAGENT = 10018, WRITEFUNCTION = 20011, TIMEOUT = 13, NOSIGNAL = 99 };
		std::string body;
		const char* url = getenv("BANANA_RA_URL");	// (the tests' stand-in for the site)
		cu.setopt(h, URL, url && *url ? url : "https://retroachievements.org/dorequest.php");
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

	bool fetchBadge(const std::string& name, const std::string& dir)
	{
		if (name.empty() || name.find('/') != std::string::npos || name.find("..") != std::string::npos || !mkdirs(dir)) return false;
		const std::string path = dir + "/" + name + ".png";
		std::string body;
		long status = 0;
		if (const char* fx = getenv("BANANA_RA_FIXTURE"); fx && *fx)
		{
			body = slurp(std::string(fx) + "/badges/" + name + ".png");
			status = body.empty() ? 404 : 200;
		}
		else if (!get("https://media.retroachievements.org/Badge/" + name + ".png", body, status)) return false;
		if (status != 200 || body.size() < 8) return false;
		return writeFile(path, body);
	}

	// ---- the loader ---------------------------------------------------------------------------------------------
	const char* stateName(Loader::State s)
	{
		static const char* const N[] = { "loading", "identifying", "ready", "no-account", "no-connection", "no-system", "no-game", "no-file", "login-failed" };
		return (unsigned)s < sizeof N / sizeof *N ? N[s] : "?";
	}

	struct Loader::Shared
	{
		mutable std::mutex mu;
		Job job;
		Snapshot snap;
		bool busy = true;
		std::atomic<bool> stopped{ false };
		std::deque<std::string> want;				// badges to fetch, the page's newest wish first
		std::set<std::string> have, failed;
		bool fetching = false;
		void set(State s, bool offline = false)
		{
			std::lock_guard<std::mutex> l(mu);
			snap.state = s; snap.offline = offline; snap.gen++;
		}
		void done(State s, bool offline = false) { std::lock_guard<std::mutex> l(mu); snap.state = s; snap.offline = offline; snap.gen++; busy = false; }
	};

	namespace
	{
		std::string enc(const std::string& s)
		{
			static const char* const HEX = "0123456789ABCDEF";
			std::string r;
			for (unsigned char c : s)
				if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') r += (char)c;
				else { r += '%'; r += HEX[c >> 4]; r += HEX[c & 15]; }
			return r;
		}
		bool authError(const std::string& reply)
		{
			JPtr j = parseJson(reply);
			if (!j || !j->isObj()) return false;
			const std::string code = j->str("Code"), err = j->str("Error");
			return j->num("Status") == 401 || code == "invalid_credentials" || code == "expired_token" || err.find("nvalid user") != std::string::npos ||
				err.find("assword") != std::string::npos || err.find("oken") != std::string::npos;
		}
	}

	std::shared_ptr<Loader> Loader::start(Job job)
	{
		auto ld = std::make_shared<Loader>();
		ld->sh = std::make_shared<Shared>();
		auto sh = ld->sh;
		if (!job.post) job.post = post;
		sh->job = job;
		const uint32_t console = consoleFor(job.system);
		if (!console && !job.gameId) { sh->done(NO_SYSTEM); return ld; }
		std::thread([sh, console]
		{
			const Job& j = sh->job;
			if (j.account.user.empty() || (j.account.token.empty() && j.account.password.empty())) { sh->done(NO_ACCOUNT); return; }
			const RomKey k = romKey(j.rom);
			uint32_t id = j.gameId;
			std::string hash;
			bool knownUnknown = false;	// identified before as a game the server doesn't know
			if (!id && readId(j.cacheDir, k, hash, id) && !id) knownUnknown = true;
			List cached;
			bool haveCache = id && readList(j.cacheDir, id, cached);
			auto offline = [&] { if (haveCache) sh->done(READY, true); else sh->done(NO_CONNECTION); };
			if (haveCache)
			{
				std::lock_guard<std::mutex> l(sh->mu);
				sh->snap.list = cached; sh->snap.state = READY; sh->snap.offline = true; sh->snap.gen++;
			}
			if (!j.network) { if (knownUnknown) sh->done(NO_GAME); else offline(); return; }
			if (!id)
			{
				if (hash.empty())
				{
					if (!k.size) { sh->done(NO_FILE); return; }
					sh->set(IDENTIFYING);
					hash = hashRom(j.rom, console, j.cacheDir);
					if (hash.empty()) { sh->done(NO_FILE); return; }
				}
				const std::string r = j.post("r=gameid&m=" + hash);
				if (sh->stopped) return;
				if (r.empty()) { if (knownUnknown) sh->done(NO_GAME); else sh->done(NO_CONNECTION); return; }
				JPtr g = parseJson(r);
				if (!success(g)) { sh->done(NO_GAME); return; }
				id = idOf(g->get("GameID"));
				writeId(j.cacheDir, k, hash, id);
				if (!id) { sh->done(NO_GAME); return; }
				haveCache = readList(j.cacheDir, id, cached);	// (the game panel may have kept one under this id)
			}
			std::string token = j.account.token;
			const std::string u = enc(j.account.user);
			if (token.empty())
			{
				// (the account typed in the setup keeps its password, RetroArch's token may not be there yet: a login, which
				// only answers with the token)
				const std::string r = j.post("r=login2&u=" + u + "&p=" + enc(j.account.password));
				if (sh->stopped) return;
				if (r.empty()) { offline(); return; }
				JPtr t = parseJson(r);
				token = success(t) ? t->str("Token") : "";
				if (token.empty()) { sh->done(LOGIN_FAILED); return; }
			}
			const std::string who = "&u=" + u + "&t=" + enc(token) + "&g=" + std::to_string(id);
			const std::string patch = j.post("r=patch" + who);
			if (sh->stopped) return;
			if (patch.empty()) { offline(); return; }
			List fresh;
			if (!parsePatch(patch, fresh)) { sh->done(authError(patch) ? LOGIN_FAILED : NO_GAME); return; }
			fresh.gameId = id;
			if (fresh.items.empty()) { sh->done(NO_GAME); return; }
			const std::string soft = j.post("r=unlocks" + who + "&h=0");
			const std::string hard = soft.empty() ? "" : j.post("r=unlocks" + who + "&h=1");
			if (sh->stopped) return;
			bool unlocksOk = parseUnlocks(soft, false, fresh);
			unlocksOk = parseUnlocks(hard, true, fresh) && unlocksOk;
			if (unlocksOk)
			{
				if (haveCache) keepTimes(fresh, cached);
				fresh.fetched = (long long)time(nullptr);
				writeList(j.cacheDir, fresh);
			}
			else
			{
				// (the list came, the unlocks didn't: what the cache knew of them, and still "as of" it)
				std::map<uint32_t, const Item*> was;
				for (auto& i : cached.items) was[i.id] = &i;
				for (auto& i : fresh.items) { auto w = was.find(i.id); i.soft = w != was.end() ? w->second->soft : 0; i.hard = w != was.end() ? w->second->hard : 0; }
				fresh.fetched = haveCache ? cached.fetched : 0;
			}
			{
				std::lock_guard<std::mutex> l(sh->mu);
				sh->snap.list = fresh;
			}
			sh->done(READY, !unlocksOk);
		}).detach();
		return ld;
	}

	Loader::Snapshot Loader::snapshot() const { std::lock_guard<std::mutex> l(sh->mu); return sh->snap; }
	int Loader::gen() const { std::lock_guard<std::mutex> l(sh->mu); return sh->snap.gen; }
	bool Loader::busy() const { std::lock_guard<std::mutex> l(sh->mu); return sh->busy; }
	bool Loader::fetchingBadges() const { std::lock_guard<std::mutex> l(sh->mu); return sh->fetching; }
	void Loader::stop() { sh->stopped = true; std::lock_guard<std::mutex> l(sh->mu); sh->want.clear(); }

	bool Loader::badge(const std::string& name)
	{
		if (name.empty()) return false;
		std::lock_guard<std::mutex> l(sh->mu);
		if (sh->have.count(name)) return true;
		if (sh->failed.count(name)) return false;
		if (access((sh->job.badgeDir + "/" + name + ".png").c_str(), R_OK) == 0) { sh->have.insert(name); return true; }
		if (!sh->job.network || sh->stopped) return false;
		for (auto i = sh->want.begin(); i != sh->want.end(); ++i) if (*i == name) { sh->want.erase(i); break; }
		sh->want.push_front(name);	// (the rows on the page now first)
		if (!sh->fetching)
		{
			sh->fetching = true;
			auto s = sh;
			std::thread([s]
			{
				for (;;)
				{
					std::string n;
					{
						std::lock_guard<std::mutex> l(s->mu);
						if (s->want.empty() || s->stopped) { s->fetching = false; return; }
						n = s->want.front();
						s->want.pop_front();
					}
					const bool ok = fetchBadge(n, s->job.badgeDir);
					std::lock_guard<std::mutex> l(s->mu);
					(ok ? s->have : s->failed).insert(n);
					if (ok) s->snap.gen++;
				}
			}).detach();
		}
		return false;
	}
}
