// banana-sync: the card kept in step with a home server: its games (with their box art and gamelist entries) copied
// down, and the saves synced both ways. Two kinds of server, behind one Backend:
//   Romhost  the ROM library on Homeport (romhost/PROTOCOL.md: UDP discovery, a 6-digit pairing code, a manifest, a
//            save plan the server decides)
//   RomM     the popular self-hosted ROM manager (rommapp/romm, API of 5.x: a device pairing code approved in its web
//            UI, platforms + roms listed, ROMs and covers downloaded, saves and states per ROM)
// Usage (the menu runs it; it says how far it got in /run/banana/sync.status, one line):
//   banana-sync discover                       Romhost servers on the LAN: "ROMHOST\t<url>\t<name>" lines
//   banana-sync probe <address>                what answers there: "romhost <url> <name>" / "romm <url> <version>" / "none"
//   banana-sync pair <address> [code]          Romhost: the code from Homeport. RomM: no code = the device flow (the
//                                              status shows the code to type in RomM: "pair <code> <url>"); a code =
//                                              a client token's pairing code; rmm_... = a token pasted
//   banana-sync forget                         the pairing (and the sync's state) dropped; the files stay
//   banana-sync sync [--saves-only] [--auto quit|wifi] [--dry-run]
//   banana-sync status                         the pairing and the last sync, for a person
// The card: --root /data (saves/, states/, drastic/), --roms <root>/roms, --conf <root>/banana/romsync.json,
// --settings <root>/banana/settings.conf (homesync_roms, homesync_states, homesync_auto, homesync.<system>).
// Rules kept (PROTOCOL.md "What a device sync must do"): only files the sync put there are ever changed or deleted
// (the state lists them with their hashes; a file of the user's at the same path is left alone and reported);
// downloads go to <file>.part, are checked, then renamed; gamelist.xml is merged; saves never sync while a game runs.
#include "json.h"
#include "sync_util.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstdarg>
#include <ctime>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <memory>
#include <signal.h>
#include <sstream>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>

using namespace bsync;
using banana::JVal;
using banana::JPtr;

namespace
{
	const char* VERSION = "banana-sync 1.0";
	std::string ROOT = "/data", ROMS, CONF, SETTINGS, STATUS = "/run/banana/sync.status", RUN = "/run/banana";
	bool dryRun = false, verbose = false;

	// ---- the status line the menu reads ---------------------------------------------------------------------------
	std::string statusLine;
	void status(const std::string& s)
	{
		if (s == statusLine) return;
		statusLine = s;
		if (verbose) fprintf(stderr, "status: %s\n", s.c_str());
		std::string tmp = STATUS + ".new";
		if (FILE* f = fopen(tmp.c_str(), "w")) { fprintf(f, "%s\n", s.c_str()); fclose(f); rename(tmp.c_str(), STATUS.c_str()); }
	}
	// killed (a quit, the menu's Stop, a shutdown): the status says so at once ("error stopped", what the menu shows for
	// a run that is gone) instead of "running" until the menu's 120 s check notices. Async-signal-safe: open, write,
	// rename and _exit only, on paths set before the handler is installed.
	char sigStatus[512], sigStatusTmp[520], sigPid[512];
	void onKill(int sig)
	{
		static const char msg[] = "error stopped\n";
		int fd = open(sigStatusTmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (fd >= 0) { ssize_t w = write(fd, msg, sizeof msg - 1); (void)w; close(fd); rename(sigStatusTmp, sigStatus); }
		unlink(sigPid);
		_exit(128 + sig);
	}
	void catchKill()
	{
		snprintf(sigStatus, sizeof sigStatus, "%s", STATUS.c_str());
		snprintf(sigStatusTmp, sizeof sigStatusTmp, "%s.new", STATUS.c_str());
		snprintf(sigPid, sizeof sigPid, "%s/sync.pid", RUN.c_str());
		struct sigaction sa = {};
		sa.sa_handler = onKill;
		sigemptyset(&sa.sa_mask);
		for (int sig : { SIGTERM, SIGINT, SIGHUP }) sigaction(sig, &sa, nullptr);
	}
	void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
	void say(const char* fmt, ...)
	{
		va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); printf("\n"); fflush(stdout);
	}

	// ---- files --------------------------------------------------------------------------------------------------------
	bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }
	long long fileSize(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 ? (long long)st.st_size : -1; }
	double fileTime(const std::string& p)
	{
		struct stat st;
		return stat(p.c_str(), &st) == 0 ? st.st_mtim.tv_sec + st.st_mtim.tv_nsec / 1e9 : 0;
	}
	std::string readFile(const std::string& p, bool* ok = nullptr)
	{
		std::ifstream f(p, std::ios::binary);
		if (ok) *ok = (bool)f;
		std::stringstream ss; ss << f.rdbuf(); return ss.str();
	}
	void mkdirs(const std::string& dir)
	{
		for (size_t i = 1; i <= dir.size(); i++)
			if (i == dir.size() || dir[i] == '/') mkdir(dir.substr(0, i).c_str(), 0755);
	}
	std::string dirOf(const std::string& p) { size_t s = p.rfind('/'); return s == std::string::npos ? "." : p.substr(0, s); }
	std::string baseOf(const std::string& p) { size_t s = p.rfind('/'); return s == std::string::npos ? p : p.substr(s + 1); }
	std::string stemOf(const std::string& name) { size_t d = name.rfind('.'); return d == std::string::npos || d == 0 ? name : name.substr(0, d); }
	std::string extOf(const std::string& name) { size_t d = name.rfind('.'); return d == std::string::npos ? "" : lower(name.substr(d + 1)); }
	// written beside, synced, renamed over
	bool writeAtomic(const std::string& path, const std::string& data)
	{
		mkdirs(dirOf(path));
		std::string tmp = path + ".new";
		FILE* f = fopen(tmp.c_str(), "wb");
		if (!f) return false;
		bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
		ok &= fflush(f) == 0 && fsync(fileno(f)) == 0;
		ok &= fclose(f) == 0;
		if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
		return true;
	}
	long long freeBytes(const std::string& p)
	{
		struct statvfs v;
		std::string d = p;
		while (!exists(d) && d.size() > 1) d = dirOf(d);
		return statvfs(d.c_str(), &v) == 0 ? (long long)v.f_bavail * v.f_frsize : -1;
	}
	// empty folders left behind by a deleted file, up to (not including) top
	void pruneDirs(std::string d, const std::string& top)
	{
		while (d.size() > top.size() && d.compare(0, top.size(), top) == 0 && rmdir(d.c_str()) == 0) d = dirOf(d);
	}
	std::string fmtNum(double d) { char b[32]; snprintf(b, sizeof b, "%.0f", d); return b; }
	std::string isoNow() { char b[32]; time_t t = time(nullptr); strftime(b, sizeof b, "%Y%m%d-%H%M%S", localtime(&t)); return b; }

	// settings.conf ("key = value" lines)
	std::map<std::string, std::string> readConf(const std::string& path)
	{
		std::map<std::string, std::string> m;
		std::ifstream f(path);
		for (std::string ln; std::getline(f, ln);)
		{
			ln = trim(ln);
			if (ln.empty() || ln[0] == '#') continue;
			size_t e = ln.find('=');
			if (e == std::string::npos) continue;
			std::string v = trim(ln.substr(e + 1));
			if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
			m[trim(ln.substr(0, e))] = v;
		}
		return m;
	}

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
	} cu;
	enum
	{
		O_WRITEDATA = 10001, O_URL = 10002, O_POSTFIELDS = 10015, O_USERAGENT = 10018, O_HTTPHEADER = 10023,
		O_HEADERDATA = 10029, O_CUSTOMREQUEST = 10036, O_MIMEPOST = 10269, O_WRITEFUNCTION = 20011, O_HEADERFUNCTION = 20079,
		O_XFERINFOFUNCTION = 20219, O_XFERINFODATA = 10057, O_NOPROGRESS = 43, O_TIMEOUT = 13, O_LOW_SPEED_LIMIT = 19,
		O_LOW_SPEED_TIME = 20, O_CONNECTTIMEOUT = 78, O_NOSIGNAL = 99, O_FOLLOWLOCATION = 52, O_POSTFIELDSIZE_LARGE = 30120,
		O_HTTPGET = 80, I_RESPONSE_CODE = 0x200002
	};
	std::string urlEsc(const std::string& s)
	{
		std::string r;
		for (unsigned char c : s)
		{
			if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') r += (char)c;
			else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); r += b; }
		}
		return r;
	}
	// a path's parts escaped, the slashes kept
	std::string pathEsc(const std::string& p)
	{
		std::string r;
		size_t a = 0;
		while (true)
		{
			size_t b = p.find('/', a);
			r += urlEsc(p.substr(a, b == std::string::npos ? std::string::npos : b - a));
			if (b == std::string::npos) break;
			r += '/';
			a = b + 1;
		}
		return r;
	}

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
	size_t onBody(char* p, size_t sz, size_t n, void* u) { ((std::string*)u)->append(p, sz * n); return sz * n; }
	size_t onFile(char* p, size_t sz, size_t n, void* u) { return fwrite(p, sz, n, (FILE*)u) * sz; }
	size_t onHeader(char* p, size_t sz, size_t n, void* u)
	{
		std::string l(p, sz * n);
		size_t c = l.find(':');
		if (c != std::string::npos) (*(std::map<std::string, std::string>*)u)[lower(trim(l.substr(0, c)))] = trim(l.substr(c + 1));
		return sz * n;
	}
	std::function<void(long long, long long)> progress;	// (bytes so far, of) for the download running
	int onProgress(void*, long long dltotal, long long dlnow, long long, long long)
	{
		if (progress) progress(dlnow, dltotal);
		return 0;
	}

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
	Resp http(const Req& q)
	{
		Resp r;
		static void* h = nullptr;
		if (!h) h = cu.init();
		else cu.reset(h);
		if (!h) { r.err = "libcurl"; return r; }
		cu.setopt(h, O_URL, q.url.c_str());
		cu.setopt(h, O_USERAGENT, "BananaOS banana-sync/1.0 (+https://github.com/JorreFog/BananaLinux)");
		cu.setopt(h, O_NOSIGNAL, 1L);
		cu.setopt(h, O_CONNECTTIMEOUT, 10L);
		if (q.timeout) cu.setopt(h, O_TIMEOUT, q.timeout);
		else { cu.setopt(h, O_LOW_SPEED_LIMIT, 1024L); cu.setopt(h, O_LOW_SPEED_TIME, 60L); }
		cu.setopt(h, O_FOLLOWLOCATION, 1L);
		cu.setopt(h, O_HEADERFUNCTION, onHeader);
		cu.setopt(h, O_HEADERDATA, &r.hdr);
		if (q.out) { cu.setopt(h, O_WRITEFUNCTION, onFile); cu.setopt(h, O_WRITEDATA, q.out); }
		else { cu.setopt(h, O_WRITEFUNCTION, onBody); cu.setopt(h, O_WRITEDATA, &r.body); }
		if (progress) { cu.setopt(h, O_NOPROGRESS, 0L); cu.setopt(h, O_XFERINFOFUNCTION, onProgress); cu.setopt(h, O_XFERINFODATA, nullptr); }
		void* hl = nullptr;
		for (auto& x : q.headers) hl = cu.slistAppend(hl, x.c_str());
		void* mime = nullptr;
		if (!q.fileParts.empty())
		{
			mime = cu.mimeInit(h);
			for (auto& fp : q.fileParts)
			{
				void* part = cu.mimeAddpart(mime);
				cu.mimeName(part, fp.first.c_str());
				cu.mimeFiledata(part, fp.second.c_str());
				cu.mimeFilename(part, (q.partName.empty() ? baseOf(fp.second) : q.partName).c_str());
			}
			cu.setopt(h, O_MIMEPOST, mime);
			if (q.method != "POST") cu.setopt(h, O_CUSTOMREQUEST, q.method.c_str());
		}
		else if (q.body)
		{
			hl = cu.slistAppend(hl, "Expect:");
			cu.setopt(h, O_POSTFIELDSIZE_LARGE, (long long)q.body->size());
			cu.setopt(h, O_POSTFIELDS, q.body->data());
			if (q.method != "POST") cu.setopt(h, O_CUSTOMREQUEST, q.method.c_str());
		}
		else if (q.method != "GET") cu.setopt(h, O_CUSTOMREQUEST, q.method.c_str());
		cu.setopt(h, O_HTTPHEADER, hl);
		int rc = cu.perform(h);
		if (hl) cu.slistFree(hl);
		if (mime) cu.mimeFree(mime);
		if (rc != 0) { r.err = cu.strerror(rc); cu.cleanup(h); h = nullptr; return r; }
		cu.getinfo(h, I_RESPONSE_CODE, &r.code);
		return r;
	}

	// ---- the card's sync state: /data/banana/romsync.json --------------------------------------------------------
	// The same file romsync.py (Romhost's reference client) keeps: server, token, device, name, state{files,gamelist,
	// etag}, saves{path: {version, sha1}}, so either can carry on from the other. backend says which server it is.
	struct SaveSt { double version = 0; std::string hash, server; };	// romm: version = the save's id, hash its MD5, server its updated_at
	struct Conf
	{
		std::string backend = "romhost", server, token, device, name, serverName, etag, last;
		bool deviceNum = false;	// Romhost's device id is a number
		std::map<std::string, std::string> files;					// roms-relative path -> the hash it had when written
		std::map<std::string, std::vector<std::string>> gamelist;	// system folder -> entry paths written into gamelist.xml
		std::map<std::string, SaveSt> saves;						// data-relative path -> as of the last sync
		std::map<std::string, SaveSt> states;						// RomM's states (Romhost keeps them with the saves)
		double lastTime = 0;
		bool paired() const { return !server.empty() && !token.empty(); }
	};
	void readSaves(const JVal* j, std::map<std::string, SaveSt>& out)
	{
		if (!j || !j->isObj()) return;
		for (auto& kv : j->obj)
		{
			SaveSt s;
			s.version = kv.second->num("version");
			s.hash = kv.second->get("md5") ? kv.second->str("md5") : kv.second->str("sha1");
			s.server = kv.second->str("server");
			out[kv.first] = s;
		}
	}
	Conf loadConf()
	{
		Conf c;
		JPtr j = banana::readJsonFile(CONF);
		if (!j || !j->isObj()) return c;
		c.backend = j->str("backend", "romhost");
		c.server = j->str("server"); c.token = j->str("token"); c.name = j->str("name"); c.serverName = j->str("serverName");
		c.last = j->str("last"); c.lastTime = j->num("lastTime");
		if (const JVal* d = j->get("device")) { c.deviceNum = d->isNum(); c.device = d->isNum() ? fmtNum(d->n) : d->s; }
		if (const JVal* st = j->get("state"))
		{
			c.etag = st->str("etag");
			if (const JVal* f = st->get("files")) if (f->isObj()) for (auto& kv : f->obj) c.files[kv.first] = kv.second->s;
			if (const JVal* g = st->get("gamelist")) if (g->isObj())
				for (auto& kv : g->obj) for (auto& p : kv.second->arr) c.gamelist[kv.first].push_back(p->s);
		}
		readSaves(j->get("saves"), c.saves);
		readSaves(j->get("states"), c.states);
		return c;
	}
	std::string savesJson(const std::map<std::string, SaveSt>& m, bool md5)
	{
		std::string o = "{";
		bool first = true;
		for (auto& kv : m)
		{
			o += std::string(first ? "\n" : ",\n") + "  " + jsonStr(kv.first) + ": {\"version\": " + fmtNum(kv.second.version) + ", \"" + (md5 ? "md5" : "sha1") + "\": " + jsonStr(kv.second.hash);
			if (!kv.second.server.empty()) o += ", \"server\": " + jsonStr(kv.second.server);
			o += "}";
			first = false;
		}
		return o + (first ? "}" : "\n }");
	}
	bool saveConf(const Conf& c)
	{
		if (dryRun) return true;
		std::string o = "{\n \"backend\": " + jsonStr(c.backend) + ",\n \"server\": " + jsonStr(c.server) + ",\n \"token\": " + jsonStr(c.token) +
			",\n \"device\": " + (c.deviceNum && !c.device.empty() ? c.device : jsonStr(c.device)) + ",\n \"name\": " + jsonStr(c.name) +
			",\n \"serverName\": " + jsonStr(c.serverName) + ",\n \"last\": " + jsonStr(c.last) + ",\n \"lastTime\": " + fmtNum(c.lastTime) +
			",\n \"state\": {\"etag\": " + jsonStr(c.etag) + ", \"files\": {";
		bool first = true;
		for (auto& kv : c.files) { o += std::string(first ? "\n  " : ",\n  ") + jsonStr(kv.first) + ": " + jsonStr(kv.second); first = false; }
		o += "},\n  \"gamelist\": {";
		first = true;
		for (auto& kv : c.gamelist)
		{
			o += std::string(first ? "\n  " : ",\n  ") + jsonStr(kv.first) + ": [";
			for (size_t i = 0; i < kv.second.size(); i++) o += (i ? ", " : "") + jsonStr(kv.second[i]);
			o += "]";
			first = false;
		}
		o += "}},\n \"saves\": " + savesJson(c.saves, c.backend == "romm") + ",\n \"states\": " + savesJson(c.states, true) + "\n}\n";
		std::string tmp = CONF + ".new";
		mkdirs(dirOf(CONF));
		int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);	// (the token: only root reads it)
		if (fd < 0) return false;
		bool ok = write(fd, o.data(), o.size()) == (ssize_t)o.size() && fsync(fd) == 0;
		ok &= close(fd) == 0;
		return ok && rename(tmp.c_str(), CONF.c_str()) == 0;
	}

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

	// the key of a file on the card, in the form the state keeps for the same manifest file
	std::string localKey(const MFile& f, const std::string& path)
	{
		if (f.generated) return "sha1:" + hashFile(path, "sha1");
		if (!f.sha1.empty()) return hashFile(path, "sha1");
		if (!f.md5.empty()) return "md5:" + hashFile(path, "md5");
		return "";
	}
	// a state key checked against the file it says is on the card ("" key: unknown, counts as unchanged)
	bool unchangedOnCard(const std::string& key, const std::string& path)
	{
		if (key.compare(0, 4, "url:") == 0) return true;	// (a cover: never changed by a person on purpose)
		if (key.compare(0, 5, "sha1:") == 0) return "sha1:" + hashFile(path, "sha1") == key;
		if (key.compare(0, 4, "md5:") == 0) return "md5:" + hashFile(path, "md5") == key;
		return hashFile(path, "sha1") == key;
	}

	struct Backend
	{
		Conf& c;
		std::set<std::string> skip;	// system folders this handheld skips (settings: homesync.<system> = off)
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
	// Romhost's rules (saves.py classify): saves/** always, states/** and drastic/savestates/** with states, never
	// *.bak .new .part .tmp, dot files or quick-resume states (*.resume.*)
	std::string saveKind(const std::string& rel)
	{
		std::string name = baseOf(rel);
		size_t a = 0;
		while (true)
		{
			size_t b = rel.find('/', a);
			if (rel[a] == '.') return "";
			if (b == std::string::npos) break;
			a = b + 1;
		}
		for (const char* x : { ".bak", ".new", ".part", ".tmp" })
			if (name.size() >= strlen(x) && name.compare(name.size() - strlen(x), std::string::npos, x) == 0) return "";
		if (name.find(".resume.") != std::string::npos) return "";
		if (rel.compare(0, 6, "saves/") == 0) return "save";
		if (rel.compare(0, 7, "states/") == 0 || rel.compare(0, 19, "drastic/savestates/") == 0) return "state";
		return "";
	}
	void walk(const std::string& dir, const std::string& rel, std::vector<std::string>& out, int depth = 0)
	{
		DIR* d = opendir(dir.c_str());
		if (!d) return;
		while (dirent* e = readdir(d))
		{
			std::string n = e->d_name;
			if (n[0] == '.') continue;
			std::string p = dir + "/" + n, r = rel + "/" + n;
			struct stat st;
			if (stat(p.c_str(), &st) != 0) continue;
			if (S_ISDIR(st.st_mode)) { if (depth < 4) walk(p, r, out, depth + 1); }
			else if (S_ISREG(st.st_mode)) out.push_back(r);
		}
		closedir(d);
	}
	std::map<std::string, std::string> localSaves(bool states)	// rel -> kind
	{
		std::map<std::string, std::string> m;
		for (const char* top : { "saves", "states", "drastic/savestates" })
		{
			std::vector<std::string> rels;
			walk(ROOT + "/" + top, top, rels);
			for (auto& r : rels)
			{
				std::string k = saveKind(r);
				if (k == "save" || (k == "state" && states)) m[r] = k;
			}
		}
		return m;
	}
	// BananaOS: DraStic's .dsv and BananaDS's .sav brought together first (the .sav is the one that syncs). Only on the
	// card's own /data (nds-saves knows no other root)
	void ndsSavesFirst()
	{
		const char* tool = getenv("BANANA_NDS_SAVES");
		std::string t = tool ? tool : (ROOT == "/data" ? "/usr/libexec/banana/nds-saves" : "");
		if (t.empty() || access(t.c_str(), X_OK) != 0) return;
		DIR* d = opendir((ROOT + "/drastic/backup").c_str());
		if (!d) return;
		std::vector<std::string> games;
		while (dirent* e = readdir(d)) { std::string n = e->d_name; if (n.size() > 4 && lower(n.substr(n.size() - 4)) == ".dsv") games.push_back(n.substr(0, n.size() - 4)); }
		closedir(d);
		for (auto& g : games)
		{
			std::string cmd = "'" + t + "' '" + ROMS + "/nds/";
			for (char ch : g) { if (ch == '\'') cmd += "'\\''"; else cmd += ch; }
			cmd += ".nds' >/dev/null 2>&1";
			if (system(cmd.c_str()) != 0) {}
		}
	}
	// a game running (bananad's /run/banana/game.pid): its emulator may be writing its save
	bool gameRunning()
	{
		int pid = 0;
		std::ifstream(RUN + "/game.pid") >> pid;
		return pid > 0 && kill(pid, 0) == 0;
	}
	// downloaded to <dst>.part, checked, renamed. The part file is left for a retry when the connection broke
	bool fetchTo(const std::string& url, const std::vector<std::string>& headers, const std::string& dst, long long size,
		const std::string& algo, const std::string& want, std::string& err, std::string* got = nullptr)
	{
		mkdirs(dirOf(dst));
		std::string part = dst + ".part";
		long long have = fileSize(part);
		if (have < 0 || (size >= 0 && have > size) || size < 0) { unlink(part.c_str()); have = 0; }
		Req q; q.url = url; q.headers = headers; q.timeout = 0;
		if (have > 0) q.headers.push_back("Range: bytes=" + std::to_string(have) + "-");
		FILE* f = nullptr;
		Resp r;
		if (!(size >= 0 && have == size))
		{
			f = fopen(part.c_str(), have > 0 ? "ab" : "wb");
			if (!f) { err = "can't write " + part + ": " + strerror(errno); return false; }
			q.out = f;
			r = http(q);
			bool wok = fflush(f) == 0 && fsync(fileno(f)) == 0;
			fclose(f);
			if (r.code == 200 && have > 0)	// (the server sent it all again: the part file has it after the old bytes)
			{
				unlink(part.c_str());
				return fetchTo(url, headers, dst, size, algo, want, err, got);
			}
			if (r.code != 200 && r.code != 206)
			{
				if (r.code != 0) unlink(part.c_str());
				err = r.code ? "HTTP " + std::to_string(r.code) : r.err;
				return false;
			}
			if (!wok) { err = "write failed (card full?)"; unlink(part.c_str()); return false; }
		}
		if (size >= 0 && fileSize(part) != size) { err = "size mismatch"; unlink(part.c_str()); return false; }
		if (!want.empty())
		{
			std::string h = hashFile(part, algo);
			if (got) *got = h;
			if (h != want) { err = "checksum mismatch"; unlink(part.c_str()); return false; }
		}
		else if (got) *got = hashFile(part, algo.empty() ? "sha1" : algo);
		if (rename(part.c_str(), dst.c_str()) != 0) { err = std::string("rename: ") + strerror(errno); return false; }
		return true;
	}

	// =============================================================================================================
	// Romhost (romhost/PROTOCOL.md)
	// =============================================================================================================
	struct Romhost : Backend
	{
		using Backend::Backend;
		bool manifest(Manifest& m, const std::set<std::string>& skip, std::string& err) override
		{
			std::vector<std::string> h;
			if (!c.etag.empty()) h.push_back("If-None-Match: " + c.etag);
			Resp r = call("GET", "/api/v1/manifest", nullptr, h);
			if (r.code == 304) { m.unchanged = true; return true; }
			if (r.code != 200) { err = r.code == 401 ? "unpaired" : r.error(); return false; }
			JPtr j = r.json();
			if (!j || !j->isObj()) { err = "the server's list can't be read"; return false; }
			m.generation = (long long)j->num("generation");
			m.etag = r.hdr["etag"];
			if (const JVal* ss = j->get("systems"))
				for (auto& s : ss->arr)
				{
					MSystem sys;
					sys.folder = s->str("folder");
					if (!safeRel(sys.folder) || skip.count(s->str("id"))) continue;
					if (const JVal* gs = s->get("games"))
						for (auto& g : gs->arr)
						{
							MGame game;
							game.title = g->str("title");
							bool ok = true;
							if (const JVal* fs = g->get("files"))
								for (auto& f : fs->arr)
								{
									MFile mf;
									mf.path = f->str("path");
									mf.size = (long long)f->num("size", -1);
									mf.sha1 = f->str("sha1");
									mf.media = f->str("kind") == "media";
									mf.url = c.server + "/api/v1/files/" + pathEsc(mf.path);
									if (!safeRel(mf.path) || mf.path.compare(0, sys.folder.size() + 1, sys.folder + "/") != 0) { ok = false; break; }
									game.files.push_back(mf);
								}
							if (const JVal* gl = g->get("gamelist"))
								for (auto& kv : gl->obj)
									game.gl.push_back({ kv.first, kv.second->isNum() ? fmtNum(kv.second->n) : kv.second->s });
							if (ok) sys.games.push_back(game); else m.skipped++;
						}
					m.systems.push_back(sys);
				}
			return true;
		}
		void report(const Manifest& m, int downloaded, int removed, const std::vector<std::string>& conflicts, const std::vector<std::string>& errors) override
		{
			std::string b = "{\"generation\": " + std::to_string(errors.empty() ? m.generation : 0) + ", \"downloaded\": " + std::to_string(downloaded) +
				", \"removed\": " + std::to_string(removed) + ", \"conflicts\": [";
			for (size_t i = 0; i < conflicts.size() && i < 50; i++) b += (i ? ", " : "") + jsonStr(conflicts[i]);
			b += "], \"errors\": [";
			for (size_t i = 0; i < errors.size() && i < 50; i++) b += (i ? ", " : "") + jsonStr(errors[i]);
			b += "], \"free\": " + std::to_string(freeBytes(ROMS)) + ", \"files\": " + std::to_string(c.files.size()) + ", \"client\": " + jsonStr(VERSION) + "}";
			call("POST", "/api/v1/report", &b);
		}
		// PROTOCOL.md "Saves": the plan, then each action; the server decides conflicts and keeps every version
		bool saves(bool, int& up, int& down, int& conflicts, std::vector<std::string>& errors, std::string& note) override
		{
			Resp r = call("GET", "/api/v1/saves/rules");
			if (r.code != 200) { errors.push_back("saves: " + (r.code == 401 ? std::string("unpaired") : r.error())); return false; }
			JPtr rules = r.json();
			std::string mode = rules ? rules->str("mode", "off") : "off";
			if (mode == "off") { note = "save sync is off for this handheld (Homeport: ROMs > Devices)"; return true; }
			ndsSavesFirst();
			auto local = localSaves(mode == "states");
			std::map<std::string, std::string> sums;
			std::string b = "{\"files\": [";
			bool first = true;
			for (auto& kv : local)
			{
				std::string p = ROOT + "/" + kv.first;
				sums[kv.first] = hashFile(p, "sha1");
				if (sums[kv.first].empty()) continue;
				auto st = c.saves.find(kv.first);
				char mt[32]; snprintf(mt, sizeof mt, "%.3f", fileTime(p));
				b += std::string(first ? "" : ", ") + "{\"path\": " + jsonStr(kv.first) + ", \"sha1\": " + jsonStr(sums[kv.first]) + ", \"size\": " +
					std::to_string(fileSize(p)) + ", \"mtime\": " + mt + ", \"base\": " + (st == c.saves.end() ? "0" : fmtNum(st->second.version)) +
					", \"base_sha1\": " + jsonStr(st == c.saves.end() ? "" : st->second.hash) + "}";
				first = false;
			}
			b += "], \"deleted\": [";
			first = true;
			for (auto& kv : c.saves)
				if (!local.count(kv.first)) { b += std::string(first ? "" : ", ") + "{\"path\": " + jsonStr(kv.first) + ", \"base\": " + fmtNum(kv.second.version) + "}"; first = false; }
			b += "]}";
			r = call("POST", "/api/v1/saves/plan", &b);
			JPtr plan = r.code == 200 ? r.json() : nullptr;
			if (!plan || !plan->get("actions")) { errors.push_back("saves: " + r.error()); return false; }
			int n = 0, total = (int)plan->get("actions")->arr.size();
			for (auto& a : plan->get("actions")->arr)
			{
				std::string rel = a->str("path"), act = a->str("action");
				n++;
				if (!safeRel(rel) || saveKind(rel).empty()) continue;
				status("running " + std::to_string(n) + " " + std::to_string(total) + " Saves|" + baseOf(rel));
				std::string p = ROOT + "/" + rel;
				if (act == "same") c.saves[rel] = { a->num("version"), a->str("sha1"), "" };
				else if (act == "forget") c.saves.erase(rel);
				else if ((act == "upload" || act == "conflict") && !a->flag("waiting"))
				{
					if (!local.count(rel)) continue;
					std::string body = readFile(p);
					if (hashStr(body, "sha1") != sums[rel]) { errors.push_back(rel + ": changed while syncing, next time"); continue; }
					std::string qs = act == "upload" ? "expect=" + fmtNum(a->num("expect")) : "conflict=1&base=" + (c.saves.count(rel) ? fmtNum(c.saves[rel].version) : std::string("0"));
					char mt[48]; snprintf(mt, sizeof mt, "X-Save-Mtime: %.3f", fileTime(p));
					if (dryRun) { say("  upload %s", rel.c_str()); continue; }
					Req q; q.method = "PUT"; q.url = c.server + "/api/v1/saves/file/" + pathEsc(rel) + "?" + qs; q.headers = auth();
					q.headers.push_back("X-Save-Sha1: " + sums[rel]); q.headers.push_back(mt); q.headers.push_back("Content-Type: application/octet-stream");
					q.body = &body; q.timeout = 0;
					Resp u = http(q);
					if (u.code != 200) { errors.push_back(rel + ": " + u.error()); continue; }
					if (act == "upload") { JPtr res = u.json(); c.saves[rel] = { res ? res->num("version") : 0, sums[rel], "" }; up++; say("  up %s", rel.c_str()); }
					else { conflicts++; say("  conflict (kept here; Homeport decides) %s", rel.c_str()); }
				}
				else if (act == "conflict") conflicts++;
				else if (act == "download")
				{
					if (exists(p) && hashFile(p, "sha1") != sums[rel]) { errors.push_back(rel + ": changed while syncing, next time"); continue; }
					if (dryRun) { say("  download %s", rel.c_str()); continue; }
					std::string err;
					if (!fetchTo(c.server + "/api/v1/saves/file/" + pathEsc(rel) + "?version=" + fmtNum(a->num("version")), auth(), p, -1, "sha1", a->str("sha1"), err))
					{ errors.push_back(rel + ": " + err); continue; }
					// (its time is now: newer than DraStic's .dsv, so nds-saves passes it on at the next start)
					c.saves[rel] = { a->num("version"), a->str("sha1"), "" };
					down++;
					say("  down %s", rel.c_str());
				}
				saveConf(c);
			}
			return true;
		}
	};

	// =============================================================================================================
	// RomM (rommapp/romm 5.x REST API, /api)
	// =============================================================================================================
	// RomM's platform slugs (utils/platform_slugs.py) -> BananaOS's system folders (etc/banana/systems.conf); a
	// platform whose folder (fs_slug) is one of ours is taken as that system too
	const std::map<std::string, std::string> ROMM_SLUGS = {
		{ "nds", "nds" }, { "gba", "gba" }, { "gbc", "gbc" }, { "gb", "gb" }, { "nes", "nes" }, { "famicom", "nes" },
		{ "snes", "snes" }, { "sfam", "snes" }, { "genesis", "megadrive" }, { "genesis-slash-megadrive", "megadrive" },
		{ "megadrive", "megadrive" }, { "psx", "psx" }, { "ps", "psx" }, { "n64", "n64" },
	};
	const std::set<std::string> BANANA_FOLDERS = { "nds", "gba", "gbc", "gb", "nes", "snes", "megadrive", "psx", "n64" };
	struct RommRom { long long id; std::string folder, stem, fsName; };
	struct Romm : Backend
	{
		using Backend::Backend;
		std::map<long long, std::string> platforms;	// platform id -> our folder
		std::vector<RommRom> roms;					// every ROM of those platforms (for the saves)
		bool listed = false;
		std::vector<JPtr> romJson;
		bool list(const std::set<std::string>& skip, std::string& err)
		{
			if (listed) return true;
			Resp r = call("GET", "/api/platforms");
			if (r.code != 200) { err = r.code == 401 || r.code == 403 ? "unpaired" : "platforms: " + r.error(); return false; }
			JPtr j = r.json();
			if (!j || !j->isArr()) { err = "the server's platforms can't be read"; return false; }
			for (auto& p : j->arr)
			{
				auto it = ROMM_SLUGS.find(p->str("slug"));
				std::string folder = it != ROMM_SLUGS.end() ? it->second : BANANA_FOLDERS.count(p->str("fs_slug")) ? p->str("fs_slug") : "";
				if (folder.empty() || skip.count(folder)) continue;
				platforms[(long long)p->num("id")] = folder;
			}
			for (auto& pf : platforms)
			{
				for (long long off = 0;; off += 500)
				{
					std::string q = "/api/roms?platform_ids=" + std::to_string(pf.first) + "&platform_id=" + std::to_string(pf.first) +
						"&limit=500&offset=" + std::to_string(off) + "&order_by=name&with_char_index=false&with_filter_values=false&with_rom_id_index=false";
					Resp rr = call("GET", q);
					if (rr.code != 200) { err = "roms: " + rr.error(); return false; }
					JPtr page = rr.json();
					const JVal* items = page ? (page->isArr() ? page.get() : page->get("items")) : nullptr;
					if (!items || !items->isArr()) { err = "the server's games can't be read"; return false; }
					for (auto& it : items->arr)
					{
						if ((long long)it->num("platform_id") != pf.first) continue;
						romJson.push_back(it);
						roms.push_back({ (long long)it->num("id"), pf.second, it->str("fs_name_no_ext", stemOf(it->str("fs_name"))), it->str("fs_name") });
					}
					long long total = page->isArr() ? (long long)items->arr.size() : (long long)page->num("total", -1);
					if (page->isArr() || items->arr.size() < 500 || (total >= 0 && off + 500 >= total)) break;
				}
			}
			listed = true;
			return true;
		}
		bool manifest(Manifest& m, const std::set<std::string>& skip, std::string& err) override
		{
			if (!list(skip, err)) return false;
			std::map<std::string, MSystem> by;
			for (auto& f : platforms) by[f.second].folder = f.second;
			for (auto& it : romJson)
			{
				std::string folder = platforms[(long long)it->num("platform_id")], fsName = it->str("fs_name");
				long long id = (long long)it->num("id");
				if (!safeRel(folder + "/" + fsName) || it->flag("missing_from_fs")) { m.skipped++; continue; }
				MGame g;
				g.title = it->str("name", fsName);
				const JVal* files = it->get("files");
				std::string entry = fsName;
				bool multi = it->flag("has_multiple_files") || (files && files->isArr() && files->arr.size() > 1);
				if (!multi)
				{
					MFile f;
					f.path = folder + "/" + fsName;
					f.size = (long long)it->num("fs_size_bytes", -1);
					if (files && files->isArr() && files->arr.size() == 1)
					{
						f.sha1 = lower(files->arr[0]->str("sha1_hash")); f.md5 = lower(files->arr[0]->str("md5_hash"));
						if (files->arr[0]->get("file_size_bytes")) f.size = (long long)files->arr[0]->num("file_size_bytes");
					}
					f.url = c.server + "/api/roms/" + std::to_string(id) + "/content/" + urlEsc(fsName);
					g.files.push_back(f);
				}
				else if (folder == "psx" && files && files->isArr())
				{
					// a disc set: its files in psx/.discs/<name>/, an .m3u of its discs as the game (Romhost's layout)
					std::string m3u;
					std::vector<std::string> discs;
					for (auto& fj : files->arr)
					{
						std::string fn = fj->str("file_name");
						if (fn.empty() || !safeRel(fn)) continue;
						std::string ext = extOf(fn);
						if (ext == "m3u" || ext == "txt" || ext == "pdf" || ext == "jpg" || ext == "png") continue;
						MFile f;
						f.path = "psx/.discs/" + fsName + "/" + fn;
						f.size = (long long)fj->num("file_size_bytes", -1);
						f.sha1 = lower(fj->str("sha1_hash")); f.md5 = lower(fj->str("md5_hash"));
						f.url = c.server + "/api/roms/" + std::to_string((long long)fj->num("id")) + "/files/content/" + urlEsc(fn);
						g.files.push_back(f);
						if (ext == "cue" || ext == "chd" || ext == "pbp" || ext == "iso") discs.push_back(".discs/" + fsName + "/" + fn);
					}
					if (discs.empty()) { m.skipped++; continue; }
					std::sort(discs.begin(), discs.end());
					for (auto& d : discs) m3u += d + "\n";
					MFile mf; mf.path = "psx/" + fsName + ".m3u"; mf.generated = true; mf.content = m3u; mf.size = (long long)m3u.size();
					g.files.push_back(mf);
					entry = fsName + ".m3u";
				}
				else { m.skipped++; continue; }	// (a game of several files on a cartridge system: not one the menu can start)
				std::string stem = stemOf(entry);
				// the cover, as the menu's library reads it (media/box/<rom name>.png or .jpg)
				std::string cover = it->str("path_cover_large");
				std::string cext = extOf(cover.substr(0, cover.find('?')));
				std::string image;
				if (!cover.empty() && (cext == "png" || cext == "jpg" || cext == "jpeg"))
				{
					MFile f;
					f.media = true;
					f.path = folder + "/media/box/" + stem + "." + (cext == "png" ? "png" : "jpg");
					f.url = cover.compare(0, 4, "http") == 0 ? cover : c.server + cover;
					if (safeRel(f.path)) { g.files.push_back(f); image = "./media/box/" + stem + "." + (cext == "png" ? "png" : "jpg"); }
				}
				g.gl.push_back({ "path", "./" + entry });
				g.gl.push_back({ "name", it->str("name", stem) });
				if (!it->str("summary").empty()) g.gl.push_back({ "desc", it->str("summary") });
				if (!image.empty()) g.gl.push_back({ "image", image });
				if (const JVal* md = it->get("metadatum"))
				{
					if (const JVal* gs = md->get("genres")) if (gs->isArr() && !gs->arr.empty()) g.gl.push_back({ "genre", gs->arr[0]->s });
					double d = md->num("first_release_date", 0);
					if (d > 0)
					{
						time_t t = (time_t)(d > 1e11 ? d / 1000 : d);	// (ms in RomM's metadatum, seconds in IGDB's)
						char b[32]; strftime(b, sizeof b, "%Y%m%dT000000", gmtime(&t));
						g.gl.push_back({ "releasedate", b });
					}
					std::string pub = md->get("publishers") && md->get("publishers")->isArr() && !md->get("publishers")->arr.empty() ? md->get("publishers")->arr[0]->s : "";
					if (!pub.empty()) g.gl.push_back({ "publisher", pub });
				}
				by[folder].games.push_back(g);
			}
			for (auto& kv : by) m.systems.push_back(kv.second);
			return true;
		}
		// RomM keeps a save per ROM (and per file name). The card's save is matched to its ROM by name: saves/nds/<rom
		// name>.sav (BananaDS), saves/<rom name>.srm (RetroArch, one folder for every system), states/... the same.
		// The decision, per save: changed here only -> upload; changed on the server only -> download; changed on both
		// -> the newer one wins and the other is kept on the card as <file>.<side>-<date>.bak (never lost); the same
		// on both -> nothing. A save deleted here is forgotten, not deleted on the server.
		bool matchRom(const std::string& rel, RommRom& out)
		{
			// saves/<folder>/<name>.<ext> or saves/<name>.<ext>
			size_t s1 = rel.find('/');
			std::string rest = rel.substr(s1 + 1), folder, name = baseOf(rel);
			if (rest.find('/') != std::string::npos) folder = rest.substr(0, rest.find('/'));
			std::string stem = stemOf(name);
			std::vector<RommRom*> c1;
			for (auto& r : roms)
				if (r.stem == stem && (folder.empty() ? r.folder != "nds" : r.folder == folder)) c1.push_back(&r);
			if (c1.size() > 1)	// (the same name on two systems: the one on the card)
			{
				std::vector<RommRom*> c2;
				for (auto* r : c1) if (exists(ROMS + "/" + r->folder + "/" + r->fsName)) c2.push_back(r);
				c1 = c2;
			}
			if (c1.size() != 1) return false;
			out = *c1[0];
			return true;
		}
		bool saves(bool statesToo, int& up, int& down, int& conflicts, std::vector<std::string>& errors, std::string& note) override
		{
			std::string err;
			if (!list(skip, err)) { errors.push_back("saves: " + err); return false; }
			ndsSavesFirst();
			auto local = localSaves(statesToo);
			bool ok = sync("save", local, c.saves, up, down, conflicts, errors);
			if (statesToo) ok &= sync("state", local, c.states, up, down, conflicts, errors);
			(void)note;
			return ok;
		}
		bool sync(const std::string& kind, const std::map<std::string, std::string>& localAll, std::map<std::string, SaveSt>& state,
			int& up, int& down, int& conflicts, std::vector<std::string>& errors)
		{
			std::string api = kind == "save" ? "/api/saves" : "/api/states", field = kind == "save" ? "saveFile" : "stateFile";
			std::string dev = c.device.empty() ? "" : "device_id=" + urlEsc(c.device);
			// the server's, per platform
			struct Srv { long long id, rom; std::string name, hash, updated; };
			std::map<std::string, Srv> srv;	// card path -> the server's newest save of that name for that ROM
			std::map<long long, RommRom> romById;
			for (auto& r : roms) romById[r.id] = r;
			for (auto& pf : platforms)
			{
				Resp r = call("GET", api + "?platform_id=" + std::to_string(pf.first) + (kind == "save" && !dev.empty() ? "&" + dev : ""));
				if (r.code != 200) { errors.push_back(kind + "s: " + r.error()); return false; }
				JPtr j = r.json();
				if (!j || !j->isArr()) continue;
				for (auto& s : j->arr)
				{
					auto ri = romById.find((long long)s->num("rom_id"));
					if (ri == romById.end() || s->flag("missing_from_fs")) continue;
					if (!s->str("slot").empty()) continue;	// (another client's versioned slots: left to it)
					std::string name = s->str("file_name");
					if (!safeRel(name) || name.find('/') != std::string::npos || stemOf(name) != ri->second.stem) continue;
					std::string top = kind == "save" ? "saves/" : "states/";
					std::string rel = ri->second.folder == "nds" ? top + "nds/" + name : top + name;
					Srv v{ (long long)s->num("id"), ri->second.id, name, lower(s->str("content_hash")), s->str("updated_at") };
					auto e = srv.find(rel);
					if (e == srv.end() || e->second.updated < v.updated) srv[rel] = v;
				}
			}
			std::set<std::string> paths;
			for (auto& kv : localAll) if (kv.second == kind && kv.first.compare(0, kind == "save" ? 6 : 7, kind == "save" ? "saves/" : "states/") == 0) paths.insert(kv.first);
			for (auto& kv : srv) paths.insert(kv.first);
			for (auto& kv : state) paths.insert(kv.first);
			int n = 0, total = (int)paths.size();
			for (auto& rel : paths)
			{
				n++;
				status("running " + std::to_string(n) + " " + std::to_string(total) + (kind == "save" ? " Saves|" : " States|") + baseOf(rel));
				std::string p = ROOT + "/" + rel;
				bool here = localAll.count(rel) > 0;
				auto si = srv.find(rel);
				auto st = state.find(rel);
				if (!here)
				{
					if (st != state.end()) { state.erase(st); continue; }	// deleted here: forgotten (the server keeps its copy)
					if (si == srv.end()) continue;
					if (exists(p)) continue;	// (a file the rules leave out)
					SaveSt ns;
					if (download(api, si->second, p, ns, dev, false, errors)) { state[rel] = ns; down++; }
					continue;
				}
				std::string md5 = hashFile(p, "md5");
				if (md5.empty()) continue;
				RommRom rom;
				if (si == srv.end())
				{
					if (st != state.end() && st->second.version > 0 && !st->second.server.empty())
					{
						// it was on the server and is gone: deleted there on purpose (RomM's web UI); not sent back
						// until it changes here
						if (st->second.hash == md5) continue;
					}
					if (!matchRom(rel, rom)) continue;	// (no ROM of this name on the server)
					if (upload(api, field, rom.id, p, rel, md5, state, dev, false, errors)) up++;
					continue;
				}
				const Srv& s = si->second;
				bool known = st != state.end();
				bool localChanged = !known || st->second.hash != md5;
				bool serverChanged = !known || st->second.version != s.id || (st->second.server != s.updated && (s.hash.empty() || s.hash != st->second.hash));
				if (!s.hash.empty() && s.hash == md5)
				{
					state[rel] = { (double)s.id, md5, s.updated };
					continue;
				}
				if (!localChanged && !serverChanged) continue;
				if (localChanged && !serverChanged) { if (upload(api, field, s.rom, p, rel, md5, state, dev, false, errors)) up++; continue; }
				if (!localChanged && serverChanged) { SaveSt ns = st->second; if (download(api, s, p, ns, dev, false, errors)) { state[rel] = ns; down++; } continue; }
				// both changed (or a card that never synced holding another version): the newer wins, the other kept
				conflicts++;
				if (dryRun) { say("  conflict %s", rel.c_str()); continue; }
				struct tm tmv = {};
				time_t srvT = strptime(s.updated.substr(0, 19).c_str(), "%Y-%m-%dT%H:%M:%S", &tmv) ? timegm(&tmv) : 0;
				std::string stamp = isoNow();
				if (fileTime(p) >= srvT)
				{
					// the card's is newer: the server's kept here beside it, then the card's uploaded over it
					std::string keep = p + ".server-" + stamp + ".bak", e2;
					if (!fetchTo(c.server + api + "/" + std::to_string(s.id) + "/content", auth(), keep, -1, "", "", e2)) { errors.push_back(rel + ": " + e2); continue; }
					if (upload(api, field, s.rom, p, rel, md5, state, dev, true, errors)) { up++; say("  conflict: the card's (newer) kept, the server's saved as %s", baseOf(keep).c_str()); }
				}
				else
				{
					std::string keep = p + ".card-" + stamp + ".bak";
					if (link(p.c_str(), keep.c_str()) != 0) { std::string d = readFile(p); if (!writeAtomic(keep, d)) { errors.push_back(rel + ": can't keep the card's copy"); continue; } }
					SaveSt ns;
					if (download(api, s, p, ns, dev, true, errors)) { state[rel] = ns; down++; say("  conflict: the server's (newer) taken, the card's kept as %s", baseOf(keep).c_str()); }
				}
				saveConf(c);
			}
			saveConf(c);
			return true;
		}
		bool upload(const std::string& api, const std::string& field, long long romId, const std::string& p, const std::string& rel,
			const std::string& md5, std::map<std::string, SaveSt>& state, const std::string& dev, bool overwrite, std::vector<std::string>& errors)
		{
			if (dryRun) { say("  upload %s", rel.c_str()); return true; }
			// a copy of the file as it is now: the emulator can't change what is sent halfway
			std::string snap = RUN + "/sync-upload." + std::to_string(getpid());
			std::string body = readFile(p);
			if (hashStr(body, "md5") != md5 || !writeAtomic(snap, body)) { errors.push_back(rel + ": changed while syncing, next time"); unlink(snap.c_str()); return false; }
			Req q; q.method = "POST"; q.headers = auth(); q.timeout = 0;
			q.url = c.server + api + "?rom_id=" + std::to_string(romId) + (api == "/api/saves" && !dev.empty() ? "&" + dev : "") + (overwrite ? "&overwrite=true" : "") + "&emulator=" + urlEsc(baseOf(dirOf(rel)) == "nds" ? "bananaos-nds" : "retroarch");
			q.fileParts.push_back({ field, snap });
			q.partName = baseOf(rel);
			Resp r = http(q);
			unlink(snap.c_str());
			if (r.code == 409) { errors.push_back(rel + ": changed on the server meanwhile, next time"); return false; }
			JPtr j = r.json();
			if ((r.code != 200 && r.code != 201) || !j || !j->isObj()) { errors.push_back(rel + ": " + r.error()); return false; }
			state[rel] = { j->num("id"), md5, j->str("updated_at") };
			say("  up %s", rel.c_str());
			saveConf(c);
			return true;
		}
		template <class S> bool download(const std::string& api, const S& s, const std::string& p, SaveSt& st, const std::string& dev, bool force, std::vector<std::string>& errors)
		{
			if (dryRun) { say("  download %s", p.c_str()); return false; }
			if (!force && exists(p) && hashFile(p, "md5") != st.hash) { errors.push_back(baseOf(p) + ": changed while syncing, next time"); return false; }
			std::string err, got;
			std::string url = c.server + api + "/" + std::to_string(s.id) + "/content" + (api == "/api/saves" && !dev.empty() ? "?" + dev : "");
			if (!fetchTo(url, auth(), p, -1, "md5", "", err, &got)) { errors.push_back(baseOf(p) + ": " + err); return false; }
			st = { (double)s.id, got, s.updated };
			say("  down %s", p.c_str());
			saveConf(c);
			return true;
		}
	};

	std::unique_ptr<Backend> backendFor(Conf& c)
	{
		if (c.backend == "romm") return std::unique_ptr<Backend>(new Romm(c));
		return std::unique_ptr<Backend>(new Romhost(c));
	}

	// ---- the ROMs: the card made to match the server's list (PROTOCOL.md "What a device sync must do") ----------------
	struct RomResult { int downloaded = 0, removed = 0; std::vector<std::string> conflicts, errors; bool changed = false; };
	// Choosing games (owner, 0.5.0): settings homesync_games = all | chosen; the chosen ones in banana/homesync-games.txt,
	// a line each: the game's key, its first ROM file's roms-relative path ("gba/Name.gba"), as `banana-sync games` lists
	std::string gameKey(const MGame& g)
	{
		for (auto& f : g.files) if (!f.media) return f.path;
		return g.files.empty() ? std::string() : g.files[0].path;
	}
	std::string choiceFile() { return ROOT + "/banana/homesync-games.txt"; }
	std::set<std::string> readChoice()
	{
		std::set<std::string> out;
		std::ifstream f(choiceFile());
		std::string l;
		while (std::getline(f, l)) { while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back(); if (!l.empty() && l[0] != '#') out.insert(l); }
		return out;
	}

	// chosen: null = every game; else only those games come to the card, and the others' files already on it stay (an
	// unticked game isn't removed: un-choosing never deletes, as a skipped system's games stay)
	RomResult syncRoms(Backend& b, Conf& c, const Manifest& m, const std::set<std::string>& skipFolders, const std::set<std::string>* chosen = nullptr)
	{
		RomResult res;
		std::map<std::string, const MFile*> want;
		std::set<std::string> keep;
		for (auto& s : m.systems) for (auto& g : s.games)
		{
			bool on = !chosen || chosen->count(gameKey(g));
			for (auto& f : g.files) { if (on) want[f.path] = &f; else keep.insert(f.path); }
		}
		std::vector<const MFile*> todo;
		for (auto& kv : want)
		{
			const MFile& f = *kv.second;
			std::string dst = ROMS + "/" + f.path;
			auto st = c.files.find(f.path);
			bool there = exists(dst);
			if (st != c.files.end() && st->second == f.key() && there && (f.size < 0 || f.media || fileSize(dst) == f.size)) continue;
			if (there && st == c.files.end())
			{
				if (f.media) continue;	// (the user's own art, banana-scrape's: kept, not ours)
				std::string lk = localKey(f, dst);
				bool same = !lk.empty() ? lk == f.key() : (f.size >= 0 && fileSize(dst) == f.size);
				if (same) { c.files[f.path] = f.key(); continue; }	// the same file, copied by hand: adopted
				res.conflicts.push_back(f.path);	// the user's own file at that path: never overwritten
				continue;
			}
			if (there && st != c.files.end() && !f.media && !unchangedOnCard(st->second, dst))
			{
				res.conflicts.push_back(f.path);	// ours, but changed on the card since (a patched ROM): left alone
				continue;
			}
			todo.push_back(&f);
		}
		std::vector<std::string> gone;
		bool emptyList = want.empty() && !c.files.empty();
		for (auto& kv : c.files)
		{
			if (want.count(kv.first)) continue;
			std::string folder = kv.first.substr(0, kv.first.find('/'));
			if (skipFolders.count(folder)) continue;	// (a system skipped on this handheld: its games stay as they are)
			if (keep.count(kv.first)) continue;			// (a game not chosen: left as it is on the card)
			gone.push_back(kv.first);
		}
		if (emptyList) gone.clear();	// a server listing nothing at all while the card has its games: nothing removed
		long long need = 0;
		for (auto* f : todo) need += std::max(0LL, f->size);
		long long fr = freeBytes(ROMS);
		say("roms: %zu to download (%.1f MB), %zu to remove, %zu conflicts, %.1f GB free%s", todo.size(), need / 1e6, gone.size(), res.conflicts.size(), fr / 1e9,
			m.skipped ? (", " + std::to_string(m.skipped) + " games skipped").c_str() : "");
		if (fr >= 0 && need > fr - (64LL << 20)) { res.errors.push_back("not enough space on the card for this sync"); return res; }
		if (dryRun)
		{
			for (auto* f : todo) say("  + %s", f->path.c_str());
			for (auto& g : gone) say("  - %s", g.c_str());
			for (auto& g : res.conflicts) say("  ! (yours, left alone) %s", g.c_str());
			return res;
		}
		long long doneBytes = 0;
		int i = 0;
		for (auto* f : todo)
		{
			i++;
			std::string dst = ROMS + "/" + f->path, err;
			std::string label = baseOf(f->path);
			auto show = [&](long long now) {
				long long mb = (doneBytes + now) >> 20, tot = std::max(1LL, need >> 20);
				status("running " + std::to_string(std::min(mb, tot)) + " " + std::to_string(tot) + " Games " + std::to_string(i) + "/" + std::to_string(todo.size()) + "|" + label);
			};
			show(0);
			progress = [&](long long now, long long) { show(now); };
			bool ok;
			if (f->generated) ok = writeAtomic(dst, f->content);
			else
			{
				std::string algo = !f->sha1.empty() ? "sha1" : !f->md5.empty() ? "md5" : "";
				ok = fetchTo(f->url, b.auth(), dst, f->media ? -1 : f->size, algo, !f->sha1.empty() ? f->sha1 : f->md5, err);
			}
			progress = nullptr;
			doneBytes += std::max(0LL, f->size);
			if (!ok) { res.errors.push_back(f->path + ": " + (err.empty() ? "write failed" : err)); continue; }
			c.files[f->path] = f->key();
			res.downloaded++;
			res.changed = true;
			saveConf(c);	// progress kept: a sync cut short goes on from here
			say("  [%d/%zu] %s", i, todo.size(), f->path.c_str());
		}
		for (auto& rel : gone)
		{
			std::string p = ROMS + "/" + rel;
			if (exists(p) && fileSize(p) > 0 && !unchangedOnCard(c.files[rel], p)) res.errors.push_back(rel + ": changed on the card, kept");
			else if (unlink(p.c_str()) == 0 || errno == ENOENT) { pruneDirs(dirOf(p), ROMS); res.removed++; res.changed = true; }
			c.files.erase(rel);
		}
		// gamelist.xml per system: the entries of the games whose files are all in place
		for (auto& s : m.systems)
		{
			std::vector<std::vector<std::pair<std::string, std::string>>> ours;
			for (auto& g : s.games)
			{
				bool all = !g.gl.empty();
				for (auto& f : g.files) { auto st = c.files.find(f.path); if (!f.media && (st == c.files.end() || st->second != f.key())) all = false; }
				if (!all) continue;
				// the picture only when it is there
				auto gl = g.gl;
				for (auto it = gl.begin(); it != gl.end();)
				{
					if ((it->first == "image" || it->first == "thumbnail") && !exists(ROMS + "/" + s.folder + "/" + normPath(it->second))) it = gl.erase(it);
					else ++it;
				}
				ours.push_back(gl);
			}
			std::string path = ROMS + "/" + s.folder + "/gamelist.xml", out;
			auto& before = c.gamelist[s.folder];
			std::set<std::string> drop(before.begin(), before.end());
			bool had = exists(path);
			if (ours.empty() && drop.empty()) { c.gamelist.erase(s.folder); continue; }
			if (!mergeGamelist(had ? readFile(path) : "", drop, ours, out)) { res.errors.push_back(s.folder + "/gamelist.xml can't be read: left alone"); continue; }
			if ((!had || readFile(path) != out) && !writeAtomic(path, out)) { res.errors.push_back(s.folder + "/gamelist.xml: write failed"); continue; }
			before.clear();
			for (auto& e : ours) for (auto& kv : e) if (kv.first == "path") before.push_back(kv.second);
			res.changed = true;
		}
		std::set<std::string> listed;
		for (auto& s : m.systems) listed.insert(s.folder);
		for (auto it = c.gamelist.begin(); it != c.gamelist.end();)
		{
			if (listed.count(it->first) || skipFolders.count(it->first) || emptyList) { ++it; continue; }
			std::string path = ROMS + "/" + it->first + "/gamelist.xml", out;
			std::set<std::string> drop(it->second.begin(), it->second.end());
			if (exists(path) && mergeGamelist(readFile(path), drop, {}, out)) writeAtomic(path, out);
			it = c.gamelist.erase(it);
		}
		if (res.errors.empty()) c.etag = m.etag;
		saveConf(c);
		b.report(m, res.downloaded, res.removed, res.conflicts, res.errors);
		return res;
	}

	// ---- discovery and pairing ---------------------------------------------------------------------------------------
	std::string normUrl(std::string a, int defPort)
	{
		a = trim(a);
		while (!a.empty() && a.back() == '/') a.pop_back();
		if (a.find("://") == std::string::npos) a = "http://" + a;
		size_t host = a.find("://") + 3;
		if (a.find(':', host) == std::string::npos && a.find('/', host) == std::string::npos && defPort) a += ":" + std::to_string(defPort);
		return a;
	}
	std::vector<std::pair<std::string, std::string>> discover(double wait)
	{
		std::vector<std::pair<std::string, std::string>> out;
		int s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s < 0) return out;
		int one = 1;
		setsockopt(s, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
		timeval tv = { 0, 300000 };
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
		sockaddr_in a = {};
		a.sin_family = AF_INET;
		const char* port = getenv("BANANA_SYNC_DISCOVER_PORT");
		a.sin_port = htons(port ? atoi(port) : 8092);
		const char* to = getenv("BANANA_SYNC_DISCOVER_ADDR");	// (tests: a unicast address)
		a.sin_addr.s_addr = to ? inet_addr(to) : INADDR_BROADCAST;
		std::set<std::string> seen;
		for (int tries = 0; tries < 2; tries++)
		{
			sendto(s, "ROMHOST?", 8, 0, (sockaddr*)&a, sizeof a);
			timeval t0; gettimeofday(&t0, nullptr);
			while (true)
			{
				timeval t1; gettimeofday(&t1, nullptr);
				if ((t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec) / 1e6 > wait / 2) break;
				char buf[4096];
				sockaddr_in from = {}; socklen_t fl = sizeof from;
				ssize_t n = recvfrom(s, buf, sizeof buf - 1, 0, (sockaddr*)&from, &fl);
				if (n <= 0) continue;
				buf[n] = 0;
				JPtr j = banana::parseJson(buf);
				if (!j || j->str("service") != "romhost") continue;
				std::string url = "http://" + std::string(inet_ntoa(from.sin_addr)) + ":" + fmtNum(j->num("port", 8092));
				if (seen.insert(url).second) out.push_back({ url, j->str("name") });
			}
			if (!out.empty()) break;
		}
		close(s);
		return out;
	}
	// what answers at an address: Romhost's hello, RomM's heartbeat
	std::string probe(const std::string& addr, std::string& url, std::string& name)
	{
		std::vector<std::string> tries;
		std::string a = trim(addr);
		bool hasPort = a.find(':', a.find("://") == std::string::npos ? 0 : a.find("://") + 3) != std::string::npos;
		if (hasPort || a.find("://") != std::string::npos) tries.push_back(normUrl(a, 0));
		if (!hasPort) { tries.push_back(normUrl(a, 8092)); tries.push_back(normUrl(a, 8080)); tries.push_back(normUrl(a, 80)); }
		for (auto& u : tries)
		{
			Req q; q.url = u + "/api/v1/hello"; q.timeout = 5;
			Resp r = http(q);
			JPtr j = r.code == 200 ? r.json() : nullptr;
			if (j && j->str("service") == "romhost") { url = u; name = j->str("name"); return "romhost"; }
			q.url = u + "/api/heartbeat";
			r = http(q);
			j = r.code == 200 ? r.json() : nullptr;
			if (j && j->get("SYSTEM")) { url = u; name = "RomM " + j->get("SYSTEM")->str("VERSION"); return "romm"; }
		}
		return "none";
	}
	std::string deviceName()
	{
		std::string model = trim(readFile("/proc/device-tree/model"));
		model.erase(std::find(model.begin(), model.end(), '\0'), model.end());
		if (model.find("Plus") != std::string::npos) return "RG DS Plus";
		if (model.find("RG") != std::string::npos || model.find("DS") != std::string::npos) return "RG DS";
		char h[64] = {}; gethostname(h, sizeof h - 1);
		return h[0] ? h : "BananaOS";
	}
	int pair(const std::string& addr, const std::string& codeIn)
	{
		std::string url, name;
		std::string kind = probe(addr, url, name);
		if (kind == "none") { status("error no server at " + addr); say("no Romhost or RomM answers at %s", addr.c_str()); return 1; }
		Conf c = loadConf();
		std::string code = trim(codeIn), dev = getenv("BANANA_SYNC_NAME") ? getenv("BANANA_SYNC_NAME") : deviceName();
		Conf n;
		n.backend = kind; n.server = url; n.serverName = name; n.name = dev;
		// a new server: a new state (another server's files and versions mean nothing here); the files stay on the card
		if (c.server == url && c.backend == kind) { n.files = c.files; n.gamelist = c.gamelist; n.saves = c.saves; n.states = c.states; }
		if (kind == "romhost")
		{
			if (code.empty()) { status("error the pairing code from Homeport is needed"); return 1; }
			std::string b = "{\"code\": " + jsonStr(code) + ", \"name\": " + jsonStr(dev) + "}";
			Req q; q.method = "POST"; q.url = url + "/api/v1/pair"; q.body = &b; q.headers = { "Content-Type: application/json" };
			Resp r = http(q);
			JPtr j = r.code == 200 ? r.json() : nullptr;
			if (!j || j->str("token").empty()) { status("error " + r.error()); say("pairing failed: %s", r.error().c_str()); return 1; }
			n.token = j->str("token"); n.device = fmtNum(j->num("device")); n.deviceNum = true; n.name = j->str("deviceName", dev);
			if (const JVal* s = j->get("server")) n.serverName = s->str("name", name);
		}
		else if (code.compare(0, 4, "rmm_") == 0) n.token = code;	// a client token pasted
		else if (!code.empty())
		{
			// a client token's pairing code (RomM: Profile > Client tokens > Pair)
			std::string b = "{\"code\": " + jsonStr(code) + "}";
			Req q; q.method = "POST"; q.url = url + "/api/client-tokens/exchange"; q.body = &b; q.headers = { "Content-Type: application/json" };
			Resp r = http(q);
			JPtr j = r.code == 200 ? r.json() : nullptr;
			if (!j || j->str("raw_token").empty()) { status("error " + r.error()); say("pairing failed: %s", r.error().c_str()); return 1; }
			n.token = j->str("raw_token"); n.device = j->str("device_id");
		}
		else
		{
			// RomM's device pairing: a code shown here, typed (approved) in RomM's web UI at /pair/device
			char host[64] = {}; gethostname(host, sizeof host - 1);
			std::string ident = "bananaos-" + std::string(host[0] ? host : "handheld") + "-" + trim(readFile("/etc/machine-id")).substr(0, 8);
			std::string b = "{\"client_device_identifier\": " + jsonStr(ident) + ", \"name\": " + jsonStr(dev) + ", \"client\": \"BananaOS\", \"platform\": \"linux\", \"client_version\": \"1.0\", "
				"\"requested_scopes\": [\"me.read\", \"roms.read\", \"platforms.read\", \"assets.read\", \"assets.write\", \"devices.read\", \"devices.write\"]}";
			Req q; q.method = "POST"; q.url = url + "/api/auth/device/init"; q.body = &b; q.headers = { "Content-Type: application/json" };
			Resp r = http(q);
			JPtr j = (r.code == 200 || r.code == 201) ? r.json() : nullptr;
			if (!j || j->str("device_code").empty()) { status("error " + (r.code == 404 ? std::string("this RomM has no device pairing (update it, or type a client token's code)") : r.error())); return 1; }
			std::string dc = j->str("device_code"), uc = j->str("user_code");
			int interval = std::max(2, (int)j->num("interval", 5)), left = (int)j->num("expires_in", 600);
			// the page's address with the code in it (verification_path_complete): RomM's /pair/device has no field to
			// type a code into, it takes it from ?user_code= only (#141)
			std::string verify = url + j->str("verification_path_complete", j->str("verification_path", "/pair/device") + "?user_code=" + urlEsc(uc));
			status("pair " + uc + " " + verify);
			say("On RomM, open %s and enter %s", verify.c_str(), uc.c_str());
			std::string body = "{\"device_code\": " + jsonStr(dc) + "}";
			for (; left > 0; left -= interval)
			{
				sleep(interval);
				Req t; t.method = "POST"; t.url = url + "/api/auth/device/token"; t.body = &body; t.headers = { "Content-Type: application/json" };
				Resp tr = http(t);
				JPtr tj = tr.code == 200 ? tr.json() : nullptr;
				if (tj && !tj->str("access_token").empty()) { n.token = tj->str("access_token"); n.device = tj->str("device_id"); break; }
				std::string why = tr.error();
				if (why == "slow_down") interval += 5;
				else if (why != "authorization_pending" && tr.code != 0) { status("error " + (why == "access_denied" ? std::string("refused in RomM") : why == "expired_token" ? std::string("the code ran out") : why)); return 1; }
			}
			if (n.token.empty()) { status("error the code ran out"); return 1; }
		}
		if (!saveConf(n)) { status("error can't write " + CONF); return 1; }
		status("paired " + (n.serverName.empty() ? url : n.serverName));
		say("paired with %s (%s) as \"%s\"", n.serverName.c_str(), url.c_str(), n.name.c_str());
		return 0;
	}

	// the awake lock (bananad doesn't sleep for idleness while it names a live process): taken only when free
	struct Awake
	{
		bool mine = false;
		void take()
		{
			std::string f = RUN + "/awake.pid";
			int pid = 0;
			std::ifstream(f) >> pid;
			if (pid > 0 && pid != getpid() && kill(pid, 0) == 0) return;	// (banana-scrape holds it: it keeps the handheld up for both)
			if (FILE* o = fopen(f.c_str(), "w")) { fprintf(o, "%d\n", (int)getpid()); fclose(o); mine = true; }
		}
		~Awake()
		{
			if (!mine) return;
			int pid = 0;
			std::ifstream(RUN + "/awake.pid") >> pid;
			if (pid == getpid()) unlink((RUN + "/awake.pid").c_str());
		}
	};

	int doSync(bool savesOnly, const std::string& autoWhy)
	{
		auto set = readConf(SETTINGS);
		auto get = [&](const std::string& k, const std::string& d) { auto i = set.find(k); return i == set.end() || i->second.empty() ? d : i->second; };
		Conf c = loadConf();
		if (!c.paired()) { if (autoWhy.empty()) status("error not paired"); return 2; }
		if (!autoWhy.empty())
		{
			std::string a = get("homesync_auto", "off");
			if (a == "off" || (autoWhy == "wifi" && a != "both")) return 0;
			if (autoWhy == "quit") savesOnly = true;	// (leaving a game: only the saves, it's quick)
		}
		bool roms = !savesOnly && get("homesync_roms", "on") == "on";
		bool states = get("homesync_states", "off") == "on";
		std::set<std::string> skip;
		for (auto& kv : set) if (kv.first.compare(0, 9, "homesync.") == 0 && kv.second == "off") skip.insert(kv.first.substr(9));
		auto b = backendFor(c);
		b->skip = skip;
		bool pick = get("homesync_games", "all") == "chosen";
		std::set<std::string> chosen;
		if (pick) { chosen = readChoice(); c.etag.clear(); }	// (the list in full: the choice may have changed since)
		Awake awake;
		if (roms) awake.take();
		status("running 0 0 |" + (c.serverName.empty() ? c.server : c.serverName));
		std::vector<std::string> errors, conflicts;
		int downloaded = 0, removed = 0, up = 0, down = 0, sconf = 0;
		std::string note;
		if (roms)
		{
			Manifest m;
			std::string err;
			if (!b->manifest(m, skip, err))
			{
				if (err == "unpaired") { status("error unpaired"); c.last = "unpaired"; saveConf(c); return 1; }
				errors.push_back(err);
			}
			else if (m.unchanged) say("roms: up to date");
			else
			{
				RomResult r = syncRoms(*b, c, m, skip, pick ? &chosen : nullptr);
				downloaded = r.downloaded; removed = r.removed; conflicts = r.conflicts;
				for (auto& e : r.errors) errors.push_back(e);
			}
		}
		if (gameRunning()) errors.push_back("saves: a game is running, next time");
		else b->saves(states, up, down, sconf, errors, note);
		for (auto& e : errors) say("  error: %s", e.c_str());
		if (!note.empty()) say("%s", note.c_str());
		char sum[256];
		snprintf(sum, sizeof sum, "%d %d %d %d %d", downloaded, removed, up, down, sconf + (int)conflicts.size());
		c.last = std::string(errors.empty() ? "ok " : "errors ") + sum + (errors.empty() ? "" : "|" + errors[0]);
		c.lastTime = (double)time(nullptr);
		saveConf(c);
		status("done " + std::string(sum) + " " + std::to_string(errors.size()) + "|" + (errors.empty() ? note : errors[0]));
		say("done: %d games down, %d removed, %d saves up, %d down, %d conflicts, %zu errors", downloaded, removed, up, down, sconf + (int)conflicts.size(), errors.size());
		return errors.empty() ? 0 : 1;
	}
}

int main(int argc, char** argv)
{
	std::vector<std::string> args;
	std::string autoWhy;
	bool savesOnly = false;
	for (int i = 1; i < argc; i++)
	{
		std::string a = argv[i];
		auto val = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (a == "--root") ROOT = val();
		else if (a == "--roms") ROMS = val();
		else if (a == "--conf") CONF = val();
		else if (a == "--settings") SETTINGS = val();
		else if (a == "--status") STATUS = val();
		else if (a == "--run") RUN = val();
		else if (a == "--dry-run") dryRun = true;
		else if (a == "--saves-only") savesOnly = true;
		else if (a == "--auto") autoWhy = val();
		else if (a == "-v") verbose = true;
		else args.push_back(a);
	}
	if (ROMS.empty()) ROMS = ROOT + "/roms";
	if (CONF.empty()) CONF = ROOT + "/banana/romsync.json";
	if (SETTINGS.empty()) SETTINGS = ROOT + "/banana/settings.conf";
	if (args.empty()) { fprintf(stderr, "usage: banana-sync discover | probe <address> | pair <address> [code] | forget | sync [--saves-only] [--auto quit|wifi] [--dry-run] | status | games\n"); return 2; }
	std::string cmd = args[0];
	if (!cu.load()) { status("error libcurl is missing"); fprintf(stderr, "banana-sync: libcurl is missing\n"); return 1; }
	if (cmd == "discover")
	{
		for (auto& s : discover(2.0)) printf("ROMHOST\t%s\t%s\n", s.first.c_str(), s.second.c_str());
		return 0;
	}
	if (cmd == "probe" && args.size() > 1)
	{
		std::string url, name;
		std::string k = probe(args[1], url, name);
		printf("%s %s %s\n", k.c_str(), url.c_str(), name.c_str());
		return k == "none";
	}
	if (cmd == "status")
	{
		Conf c = loadConf();
		if (!c.paired()) { printf("not paired\n"); return 0; }
		printf("paired with %s (%s, %s) as %s; %zu files, %zu saves synced; last: %s\n", c.serverName.c_str(), c.server.c_str(), c.backend.c_str(), c.name.c_str(),
			c.files.size(), c.saves.size() + c.states.size(), c.last.c_str());
		return 0;
	}
	// banana-sync games: the server's games for the menu's chooser, a line each: system folder TAB key TAB title TAB
	// bytes TAB chosen (1/0) TAB on the card (1/0); "error <why>" on failure
	if (cmd == "games")
	{
		Conf c = loadConf();
		if (!c.paired()) { printf("error not paired\n"); return 2; }
		auto b = backendFor(c);
		c.etag.clear();
		b->c.etag.clear();
		Manifest m;
		std::string err;
		if (!b->manifest(m, {}, err)) { printf("error %s\n", err.c_str()); return 1; }
		std::set<std::string> chosen = readChoice();
		for (auto& s : m.systems)
			for (auto& g : s.games)
			{
				std::string k = gameKey(g);
				if (k.empty()) continue;
				long long bytes = 0; bool there = true;
				for (auto& f : g.files) if (!f.media) { bytes += std::max(0LL, f.size); if (!exists(ROMS + "/" + f.path)) there = false; }
				std::string t = g.title.empty() ? baseOf(k) : g.title;
				for (char& ch : t) if (ch == '\t' || ch == '\n') ch = ' ';
				printf("%s\t%s\t%s\t%lld\t%d\t%d\n", s.folder.c_str(), k.c_str(), t.c_str(), bytes, chosen.count(k) ? 1 : 0, there ? 1 : 0);
			}
		return 0;
	}
	if (cmd == "forget")
	{
		if (unlink(CONF.c_str()) != 0 && errno != ENOENT) return 1;
		status("");
		return 0;
	}
	// one at a time (the menu's Sync now beside an automatic one)
	int lk = open((RUN + "/sync.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	if (lk >= 0 && flock(lk, LOCK_EX | LOCK_NB) != 0) { if (autoWhy.empty()) fprintf(stderr, "banana-sync: already running\n"); return 3; }
	if (cmd == "pair" && args.size() > 1) return pair(args[1], args.size() > 2 ? args[2] : "");
	if (cmd == "sync")
	{
		// the pid for the menu's Stop (Settings > Network > Home server > Sync now while it runs)
		catchKill();
		std::ofstream(RUN + "/sync.pid") << getpid() << "\n";
		int rc = doSync(savesOnly, autoWhy);
		unlink((RUN + "/sync.pid").c_str());
		return rc;
	}
	fprintf(stderr, "banana-sync: unknown command %s\n", cmd.c_str());
	return 2;
}
