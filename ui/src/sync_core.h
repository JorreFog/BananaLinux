#pragma once
// banana-sync's shared layer: what both servers (Romhost in sync.cpp, RomM in sync_romm.cpp) and the ROM sync use. The
// card's paths and the run's flags, the status line the menu reads, files written atomically, libcurl (dlopen()ed) and
// one HTTP request, the sync state in romsync.json, the one list of files a server offers, the Backend each server is,
// and the saves on the card as both servers see them.
#include "json.h"
#include "sync_util.h"
#include <cstdio>
#include <ctime>
#include <dlfcn.h>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace bsync
{
	using banana::JVal;
	using banana::JPtr;

	extern const char* VERSION;
	extern std::string ROOT, ROMS, CONF, SETTINGS, SYSTEMS, STATUS, RUN;
	extern bool dryRun, verbose;
	extern bool tlsVerify;	// homesync_tls = off: a Home server's certificate taken unchecked
	extern bool logWanted;	// the next line said opens the report on the card, banana/logs/sync.log

	// ---- the status line the menu reads ---------------------------------------------------------------------------
	void status(const std::string& s);
	void catchKill();
	void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

	// ---- files --------------------------------------------------------------------------------------------------------
	inline bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }
	inline long long fileSize(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 ? (long long)st.st_size : -1; }
	double fileTime(const std::string& p);
	std::string readFile(const std::string& p, bool* ok = nullptr);
	void mkdirs(const std::string& dir);
	inline std::string dirOf(const std::string& p) { size_t s = p.rfind('/'); return s == std::string::npos ? "." : p.substr(0, s); }
	inline std::string baseOf(const std::string& p) { size_t s = p.rfind('/'); return s == std::string::npos ? p : p.substr(s + 1); }
	inline std::string stemOf(const std::string& name) { size_t d = name.rfind('.'); return d == std::string::npos || d == 0 ? name : name.substr(0, d); }
	inline std::string extOf(const std::string& name) { size_t d = name.rfind('.'); return d == std::string::npos ? "" : lower(name.substr(d + 1)); }
	bool syncDir(const std::string& dir);
	bool writeAtomic(const std::string& path, const std::string& data);
	long long freeBytes(const std::string& p);
	void pruneDirs(std::string d, const std::string& top);
	std::string sd2Roms();
	// a ROM's path made roms-relative from either card's roms folder (the path as it is when on neither); and a
	// roms-relative path's file, on whichever card has it (the main card's when both or neither do)
	std::string romsRelative(const std::string& path);
	std::string romPath(const std::string& rel);
	void sd2MakeRoms();
	inline std::string fmtNum(double d) { char b[32]; snprintf(b, sizeof b, "%.0f", d); return b; }
	inline std::string isoNow() { char b[32]; time_t t = time(nullptr); strftime(b, sizeof b, "%Y%m%d-%H%M%S", localtime(&t)); return b; }
	std::map<std::string, std::string> readConf(const std::string& path);
	std::set<std::string> ownCoversOf(const std::string& path);

	// ---- libcurl, dlopen()ed as banana-ui and banana-scrape do it --------------------------------------------------
	struct Curl
	{
		void* (*init)(void) = nullptr;
		void (*reset)(void*) = nullptr;
		int (*setopt)(void*, int, ...) = nullptr;
		int (*perform)(void*) = nullptr;
		int (*getinfo)(void*, int, ...) = nullptr;
		void (*cleanup)(void*) = nullptr;
		char* (*escape)(void*, const char*, int) = nullptr;
		void (*freep)(void*) = nullptr;
		void* (*slistAppend)(void*, const char*) = nullptr;
		void (*slistFree)(void*) = nullptr;
		const char* (*strerror)(int) = nullptr;
		void* (*mimeInit)(void*) = nullptr;
		void* (*mimeAddpart)(void*) = nullptr;
		int (*mimeName)(void*, const char*) = nullptr;
		int (*mimeFiledata)(void*, const char*) = nullptr;
		int (*mimeFilename)(void*, const char*) = nullptr;
		void (*mimeFree)(void*) = nullptr;
		bool load()
		{
			void* l = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
			if (!l) return false;
			init = (void* (*)(void))dlsym(l, "curl_easy_init");
			reset = (void (*)(void*))dlsym(l, "curl_easy_reset");
			setopt = (int (*)(void*, int, ...))dlsym(l, "curl_easy_setopt");
			perform = (int (*)(void*))dlsym(l, "curl_easy_perform");
			getinfo = (int (*)(void*, int, ...))dlsym(l, "curl_easy_getinfo");
			cleanup = (void (*)(void*))dlsym(l, "curl_easy_cleanup");
			escape = (char* (*)(void*, const char*, int))dlsym(l, "curl_easy_escape");
			freep = (void (*)(void*))dlsym(l, "curl_free");
			slistAppend = (void* (*)(void*, const char*))dlsym(l, "curl_slist_append");
			slistFree = (void (*)(void*))dlsym(l, "curl_slist_free_all");
			strerror = (const char* (*)(int))dlsym(l, "curl_easy_strerror");
			mimeInit = (void* (*)(void*))dlsym(l, "curl_mime_init");
			mimeAddpart = (void* (*)(void*))dlsym(l, "curl_mime_addpart");
			mimeName = (int (*)(void*, const char*))dlsym(l, "curl_mime_name");
			mimeFiledata = (int (*)(void*, const char*))dlsym(l, "curl_mime_filedata");
			mimeFilename = (int (*)(void*, const char*))dlsym(l, "curl_mime_filename");
			mimeFree = (void (*)(void*))dlsym(l, "curl_mime_free");
			return init && reset && setopt && perform && getinfo && cleanup && escape && freep && slistAppend && slistFree && strerror
				&& mimeInit && mimeAddpart && mimeName && mimeFiledata && mimeFilename && mimeFree;
		}
	};
	extern Curl cu;
	std::string urlEsc(const std::string& s);
	std::string urlFix(const std::string& u);
	std::string pathEsc(const std::string& p);

	struct Resp
	{
		long code = 0;			// 0: no answer (err says why)
		std::string body, err;
		std::map<std::string, std::string> hdr;	// lower-case names
		JPtr json() const { return banana::parseJson(body); }
		std::string error() const	// the server's {"error": ...} (Romhost) or {"detail": ...} (RomM/FastAPI), or why
		{
			if (code == 0) return err.empty() ? "no answer" : err;
			JPtr j = json();
			if (j && j->isObj())
			{
				if (j->get("error") && j->get("error")->isStr()) return j->str("error");
				if (j->get("detail") && j->get("detail")->isStr()) return j->str("detail");
			}
			return "HTTP " + std::to_string(code);
		}
	};
	extern std::function<void(long long, long long)> progress;

	struct Req
	{
		std::string method = "GET", url;
		std::vector<std::string> headers;
		const std::string* body = nullptr;		// POST/PUT bytes
		FILE* out = nullptr;					// the body into a file instead of Resp::body
		std::vector<std::pair<std::string, std::string>> fileParts;	// multipart: (field name, file path)
		std::string partName;					// the multipart file's name as sent
		long timeout = 60;						// whole request (0 = none: the speed floor below instead)
	};
	Resp http(const Req& q);

	// ---- the card's sync state: /data/banana/romsync.json --------------------------------------------------------
	// The same file romsync.py (Romhost's reference client) keeps: server, token, device, name, state{files,gamelist,
	// etag}, saves{path: {version, sha1}}, so either can carry on from the other. backend says which server it is.
	struct SaveSt { double version = 0; std::string hash, server; };	// romm: version = the save's id, hash its MD5, server its updated_at
	struct Conf
	{
		std::string backend = "romhost", server, token, device, name, serverName, etag, last;
		bool deviceNum = false;	// Romhost's device id is a number
		std::map<std::string, std::string> files;					// roms-relative path -> the hash it had when written
		std::map<std::string, std::string> where;					// a file of files' on the second SD card -> "sd2" (absent: this card)
		std::map<std::string, std::vector<std::string>> gamelist;	// system folder -> entry paths written into gamelist.xml
																	// (the second SD card's: "@sd2/<folder>")
		std::map<std::string, SaveSt> saves;						// data-relative path -> as of the last sync
		std::map<std::string, SaveSt> states;						// RomM's states (Romhost keeps them with the saves)
		double lastTime = 0;
		bool paired() const { return !server.empty() && !token.empty(); }
	};
	Conf loadConf();
	bool saveConf(const Conf& c);

	// ---- what a server offers: one list of files whichever server it is ----------------------------------------------
	struct MFile
	{
		std::string path;		// relative to the roms folder
		long long size = -1;	// -1: not known (a cover)
		std::string sha1, md5;	// either may be empty
		std::string url;		// absolute
		std::string content;	// made here (a PS1 set's .m3u): no download
		bool generated = false, media = false;
		std::string key() const	// what the state keeps for it: its hash, or for a cover the URL (its ?ts= changes with it)
		{
			if (generated) return "sha1:" + hashStr(content, "sha1");
			if (!sha1.empty()) return sha1;
			if (!md5.empty()) return "md5:" + md5;
			return "url:" + url + (size >= 0 ? "#" + std::to_string(size) : "");
		}
	};
	struct MGame { std::string title; std::vector<MFile> files; std::vector<std::pair<std::string, std::string>> gl; };
	struct MSystem { std::string folder; std::vector<MGame> games; };
	struct Manifest { long long generation = 0; std::vector<MSystem> systems; std::string etag; bool unchanged = false; int skipped = 0; };
	std::string localKey(const MFile& f, const std::string& path);
	bool unchangedOnCard(const std::string& key, const std::string& path);

	struct Backend
	{
		Conf& c;
		std::set<std::string> skip;	// system folders this handheld skips (settings: homesync.<system> = off)
		const std::set<std::string>* chosen = nullptr;	// games chosen for syncing (null: every game)
		std::set<std::string> ownCovers;	// system folders that make their own covers (systems.conf: covers = own)
		explicit Backend(Conf& conf) : c(conf) {}
		virtual ~Backend() {}
		virtual std::vector<std::string> auth() const { return { "Authorization: Bearer " + c.token }; }
		Resp call(const std::string& method, const std::string& path, const std::string* body = nullptr, std::vector<std::string> h = {}) const
		{
			Req q; q.method = method; q.url = c.server + path; q.headers = auth();
			for (auto& x : h) q.headers.push_back(x);
			if (body) q.headers.push_back("Content-Type: application/json");
			q.body = body;
			return http(q);
		}
		virtual bool manifest(Manifest& m, const std::set<std::string>& skip, std::string& err) = 0;
		virtual void report(const Manifest&, int, int, const std::vector<std::string>&, const std::vector<std::string>&) {}
		virtual bool saves(bool statesToo, int& up, int& down, int& conflicts, std::vector<std::string>& errors, std::string& note) = 0;
	};

	// ---- the saves on the card ----------------------------------------------------------------------------------------
	std::string saveKind(const std::string& rel);
	std::map<std::string, std::string> localSaves(bool states);
	// saves-flat (system/rootfs/usr/libexec/banana), once a boot as the launchers run it: a save still in a RetroArch
	// core's folder from before 0.5.3 moved to the flat saves/ (saves/gba for the GBA's cores) the emulators read now,
	// so a card that hasn't started a game since the update syncs the save its next game will read. Run under
	// save.lock, before anything is decided; BANANA_SAVES_FLAT names another script (the tests')
	void savesFlatFirst();
	void ndsSavesFirst();
	void ndsSaves(const std::string& romPath);
	bool gameRunning();
	bool fetchTo(const std::string& url, const std::vector<std::string>& headers, const std::string& dst, long long size,
		const std::string& algo, const std::string& want, std::string& err, std::string* got = nullptr);
}
