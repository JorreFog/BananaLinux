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
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace bsync
{
	using banana::JVal;
	using banana::JPtr;

	extern const char* VERSION;
	extern std::string ROOT, ROMS, CONF, SETTINGS, SYSTEMS, STATUS, RUN, CA;	// CA: homesync_ca, a PEM file of CA certificates (or "")
	extern bool dryRun, verbose;
	extern std::string saveSlot;	// RomM 5.4's slot every game's save syncs with (settings: homesync_slot, homesync_slot_name)
	extern bool tlsVerify;	// homesync_tls = off: an https server's certificate taken unchecked
	extern bool logWanted;	// the next line said opens the report on the card, banana/logs/sync.log
	// what of romsync.json this run may write: the games and the pairing while it holds sync.lock (holdRoms), the
	// saves while it holds save.lock (holdSaves); the rest is written back as another run left it
	extern bool holdRoms, holdSaves;
	extern long connectTimeout, callTimeout;	// each request's, in seconds (shorter for the sync before a game starts)

	// ---- the status line the menu reads ---------------------------------------------------------------------------
	void status(const std::string& s);
	void catchKill(bool pidFile = true);	// pidFile: this run's sync.pid is removed too
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
	bool writeAtomic(const std::string& path, const std::string& data, mode_t mode = 0666);	// (mode: less the umask)
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
		long code = 0;			// 0: no answer (err says why, curl is libcurl's code)
		int curl = 0;
		long long certFrom = 0, certTo = 0;	// Req's certDates: the server's certificate's own dates (unix time; 0 not read)
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
		long timeout = callTimeout;				// whole request (0 = none: the speed floor below instead)
		bool follow = true;						// redirects followed; off, a 3xx is the answer (its location in hdr)
		bool certDates = false;					// the server's certificate taken unchecked, for its dates (Resp's certFrom, certTo)
	};
	Resp http(const Req& q);

	// ---- the card's sync state: /data/banana/romsync.json --------------------------------------------------------
	// The same file romsync.py (Romhost's reference client) keeps: server, token, device, name, state{files,gamelist,
	// etag}, saves{path: {version, sha1}}, so either can carry on from the other. backend says which server it is.
	struct SaveSt { double version = 0; std::string hash, server; };	// romm: version = the save's id, hash its MD5, server its updated_at
	// RomM's save slots (stateVersion 2), keyed "<rom id>/<slot>": the version both sides last held (the agreed one: its
	// MD5, the server's id for it, and the card's save it was for; wire: the server's MD5 for it when the card holds it
	// in another form, a DraStic .dsv taken as its raw save), what this handheld holds back for its owner, a conflict
	// (both kept, the server's version in the vault) or a save it won't take or send by itself, and the saves waiting
	// to be sent
	// (lost: the save went missing or blank here after this version was agreed. What is at its path next is a new save, not
	// that version played on, whatever happens to it, until a version is agreed again)
	struct SlotSt { std::string md5, rel; long long saveId = 0; std::string wire; bool lost = false; };
	// (rom: the game's path under the ROMs folder, which a save's name alone doesn't say: two systems can share one)
	struct SlotNote { std::string rel, why, localMd5, serverMd5, device, updated, vault; long long saveId = 0; std::string rom = ""; };
	// a held note's why when it is DraStic's and BananaDS's saves that differ, not the card's and the server's: one of
	// the two to choose, or DraStic's unreadable (its footer doesn't add up, or a size no DS save has), nothing to choose,
	// or the two not brought together this time (a read or a write that failed), tried again at the next sync
	constexpr const char* NDS_DIVERGED = "DraStic's and BananaDS's saves both changed";
	constexpr const char* NDS_UNREAD = "DraStic's save can't be read";
	constexpr const char* NDS_NOT_BROUGHT = "DraStic's and BananaDS's saves couldn't be brought together";
	// a held note's why when the save is gone from the card or blank since it was agreed
	constexpr const char* NOTE_MISSING = "missing here since the last sync";
	constexpr const char* NOTE_BLANK = "blank here since the last sync";
	inline bool lostNote(const SlotNote& n) { return n.why == NOTE_MISSING || n.why == NOTE_BLANK; }
	// nds-saves' exit codes, as its header lists them
	constexpr int NDS_BOTH_CHANGED = 3, NDS_UNREADABLE = 5, NDS_FAILED = 6;
	inline const char* ndsWhyLog(const std::string& why)
	{
		return why == NDS_UNREAD ? "DraStic's save can't be read (left for its owner)"
			: why == NDS_NOT_BROUGHT ? "DraStic's and BananaDS's saves couldn't be brought together (a read or a write failed; tried again next time)"
			: "DraStic's and BananaDS's saves both changed since they last agreed";
	}
	// a save that couldn't be sent (no answer, the server's error): its bytes in the vault, sent by the next sync
	// a save waiting to be sent, under its slot's key; or, for a game that has none yet (never synced from this card),
	// under "rom:<its roms-relative path>" with rel, its save's path, until a sync that reaches the server finds its slot
	// (sd2: its game was on the second SD card when it was left waiting, so with that card out the game isn't one the
	// card can be seen not to have)
	struct QueueSt { std::string md5, vault; double since = 0; std::string rel; bool sd2 = false; };
	struct Conf
	{
		std::string backend = "romhost", server, token, device, name, serverName, serverVersion, etag, last;	// serverVersion: RomM's, as its heartbeat says
		bool deviceNum = false;	// Romhost's device id is a number
		std::map<std::string, std::string> files;					// roms-relative path -> the hash it had when written
		std::map<std::string, std::string> where;					// a file of files' on the second SD card -> "sd2" (absent: this card)
		std::map<std::string, std::vector<std::string>> gamelist;	// system folder -> entry paths written into gamelist.xml
																	// (the second SD card's: "@sd2/<folder>")
		std::map<std::string, SaveSt> saves;						// data-relative path -> as of the last sync
		std::map<std::string, SaveSt> states;						// RomM's states (Romhost keeps them with the saves)
		int stateVersion = 1;										// 2: RomM's saves by slot (romsync.v1.json: the file before)
		std::map<std::string, SlotSt> slots;
		std::map<std::string, SlotNote> conflicts, held;
		std::map<std::string, QueueSt> queue;
		double lastTime = 0;
		bool paired() const { return !server.empty() && !token.empty(); }
	};
	Conf loadConf();
	// written back under romsync.lock, only this run's parts (holdRoms, holdSaves) over the file as it is then. False,
	// nothing written, when the file was forgotten or paired again meanwhile: this run's state is the old pairing's.
	bool saveConf(const Conf& c);
	// a new pairing written whole; the same server's saves part and files taken from the file as it is then
	bool savePairing(const Conf& c);
	// romsync.lock taken (its fd, closed to let go; -1 when it can't be opened): what every write of the file holds
	int lockConf();
	// the handheld's name (and the device it registered), alone, as rename sets them, with romsync.lock held already.
	// False, nothing written, when the pairing isn't the one c was read from
	bool saveNameLocked(const Conf& c);
	void takeSaves(Conf& to, const Conf& from);	// the saves' part of the state (saves, states, slots, conflicts, held, queue)
	// save.lock (one run at a time over the card's saves), waited for up to secs; kept until the run ends
	bool lockSaves(int secs);
	void unlockSaves();	// (the saves done: another run, the sync after a game, may have them while this one copies games)

	// ---- what a server offers: one list of files whichever server it is ----------------------------------------------
	struct MFile
	{
		std::string path;		// relative to the roms folder
		long long size = -1;	// -1: not known (a cover)
		std::string sha1, md5;	// either may be empty
		std::string url;		// absolute
		std::string content;	// made here (a PS1 set's .m3u): no download
		bool generated = false, media = false;
		// compressed (zip, 7z, rar, tar, gz, bz2): RomM's hashes are of what is inside, never of these bytes, so they
		// can't check a download; a zip's members (size, CRC32 in hex), from its listing, are checked against its own
		// central directory instead, and a 7z or rar has only its size checked
		bool packed = false;
		std::vector<std::pair<long long, std::string>> members;
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
		std::set<std::string> ownCovers;	// system folders that make their own covers (systems.conf: covers = own)
		// RomM: the games whose saves stay on this handheld ("<system folder>/<ROM file name>", banana/homesync-local.txt,
		// the game page's Save sync), neither sent nor taken
		std::set<std::string> local;
		std::string settleKey;		// resolve: the conflict its owner settled for the card's save, sent or kept as the run decides it
		std::string onlyRom;		// sync --rom: only that game's save (roms-relative); RomM 5.4 and newer, others every save
		bool gameStarted = false;
		bool oneGame = false;	// this run moves one game's save and no other's (--rom alone doesn't say: see saves())
		bool onlyQueued = false;	// only the saves waiting to be sent (their games'), the same way
		bool beforeGame = false;	// the sync before a game starts: that game's save or none, never every save
		bool keptUnsent = false;	// one game's save, the server out of reach, left waiting in the vault for the next sync
		// the game started again after Power off after sleeping, not started yet (roms-relative, "" for none): its resume
		// state belongs with the save on the card, so a sync of other games leaves that one as it is
		std::string resumeRom;
		// a game started meanwhile, with this run holding save.lock: its save may be in use and any of the rest may be it,
		// so a run of more than one game stops where it is (said once). True: leave the rest for the next sync
		bool stopForGame(std::vector<std::string>& errors);
		// the sync after a game with the server out of reach: what was played is on the card alone until the next sync,
		// and a session or a power cut can write over it first, so a copy goes to the vault now, as a failed upload's does.
		// True when there was a real save to keep; ndsHeld: DraStic's and BananaDS's weren't brought together, so which of
		// them is the save is its owner's choice
		struct Played { std::string rel, md5, vault; bool ndsHeld = false; };
		bool keepPlayed(Played& g, std::vector<std::string>& errors);
		explicit Backend(Conf& conf) : c(conf) {}
		virtual ~Backend() {}
		virtual std::vector<std::string> auth() const { return { "Authorization: Bearer " + c.token }; }
		// the same for a URL the server named itself (a cover's): the token goes to the paired server and nowhere else,
		// a file kept on another host, or the same host on another port or scheme, asked for without it
		std::vector<std::string> authFor(const std::string& url) const
		{
			return url.compare(0, c.server.size() + 1, c.server + "/") == 0 ? auth() : std::vector<std::string>();
		}
		Resp call(const std::string& method, const std::string& path, const std::string* body = nullptr, std::vector<std::string> h = {}) const
		{
			Req q; q.method = method; q.url = c.server + path; q.headers = auth();
			for (auto& x : h) q.headers.push_back(x);
			if (body) q.headers.push_back("Content-Type: application/json");
			q.body = body;
			return http(q);
		}
		virtual bool manifest(Manifest& m, const std::set<std::string>& skip, std::string& err) = 0;
		virtual bool haveList() const { return false; }	// the server's games listed by this run already (manifest() then asks nothing)
		virtual void report(const Manifest&, int, int, const std::vector<std::string>&, const std::vector<std::string>&) {}
		virtual bool saves(bool statesToo, int& up, int& down, int& conflicts, std::vector<std::string>& errors, std::string& note) = 0;
	};

	// ---- this card as a RomM device: banana/sync-id (made once, on the card: the image's machine-id is every card's)
	// as its hostname bananaos-<id>, by which RomM finds the same device again ---------------------------------------
	std::string cardId();
	std::string osVersion();	// /etc/banana/version
	std::string deviceFields(const Conf& c);	// name, platform, client, client_version, hostname, sync_mode (JSON members)
	// the card registered: the device held updated (PUT), else one registered (POST, allow_existing); c.device set.
	// "" when done, else why not
	std::string registerDevice(Conf& c);
	// a version such as "5.4.1" (or "v5.4.0-beta.2") at least major.minor.patch; one that isn't a number ("development",
	// a build from RomM's own branch) counts as new
	inline bool versionAtLeast(const std::string& v, int major, int minor, int patch)
	{
		size_t i = v.size() && (v[0] == 'v' || v[0] == 'V') ? 1 : 0;
		if (i >= v.size() || !isdigit((unsigned char)v[i])) return true;
		int got[3] = { 0, 0, 0 };
		for (int k = 0; k < 3 && i < v.size() && isdigit((unsigned char)v[i]); k++)
		{
			while (i < v.size() && isdigit((unsigned char)v[i])) got[k] = got[k] * 10 + (v[i++] - '0');
			if (i < v.size() && v[i] == '.') i++; else break;
		}
		int want[3] = { major, minor, patch };
		for (int k = 0; k < 3; k++) if (got[k] != want[k]) return got[k] > want[k];
		return true;
	}

	// ---- the saves on the card ----------------------------------------------------------------------------------------
	// a file's name as a write in progress leaves it (.bak .new .part .tmp): never a save, nor a copy in the vault
	bool workFile(const std::string& name);
	std::string saveKind(const std::string& rel);
	std::map<std::string, std::string> localSaves(bool states);
	// saves-flat (system/rootfs/usr/libexec/banana), as the launchers run it (until a run of this boot has moved
	// everything, which leaves its marker): a save still in a RetroArch core's folder from before 0.5.3 moved to the
	// flat saves/ (saves/gba for the GBA's cores) the emulators read now, so a card that hasn't started a game since
	// the update syncs the save its next game will read. Run under save.lock, before anything is decided;
	// BANANA_SAVES_FLAT names another script (the tests')
	void savesFlatFirst();
	void ndsSavesFirst();
	// nds-saves' exit code: 0 once the two agree (or with no nds-saves to run, off the card); NDS_BOTH_CHANGED,
	// NDS_UNREADABLE (DraStic's), NDS_FAILED (a read or a write failed)
	int ndsSaves(const std::string& romPath);
	// why the DS save at rel (saves/nds/<stem>.sav) isn't synced, as the last nds-saves run for its game found: the
	// two both changed since they last agreed, DraStic's can't be read, or they couldn't be brought together; the
	// note's why the Saves page lists. "" when the two are in step
	std::string ndsDivergedWhy(const std::string& rel);
	bool gameRunning();
	// SIGTERM and the like wait while this lives: what must not be left half done by a stop (ended early by end())
	struct KillHold
	{
		KillHold();
		~KillHold() { end(); }
		void end();
	private:
		sigset_t before;
		bool held = true;
	};
	// a download to a name of this run's own given up: what fetchTo keeps for a retry is never retried under it
	void dropDownload(const std::string& dl);
	bool fetchTo(const std::string& url, const std::vector<std::string>& headers, const std::string& dst, long long size,
		const std::string& algo, const std::string& want, std::string& err, std::string* got = nullptr,
		const std::function<std::string(const std::string&)>& check = nullptr);
}
