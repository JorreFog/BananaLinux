// banana-sync's shared layer (sync_core.h says what is in it)
#include "sync_core.h"
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <signal.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

namespace bsync
{
	const char* VERSION = "banana-sync 1.0";
	std::string ROOT = "/data", ROMS, CONF, SETTINGS, SYSTEMS, STATUS = "/run/banana/sync.status", RUN = "/run/banana", CA;
	bool dryRun = false, verbose = false;
	bool tlsVerify = true;	// homesync_tls = off: a Home server's certificate taken unchecked (a name on the LAN, no CA for it)

	// ---- the status line the menu reads ---------------------------------------------------------------------------
	namespace
	{
		std::string statusLine;
	}
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
	namespace
	{
	char sigStatus[512], sigStatusTmp[520], sigPid[512];
	void onKill(int sig)
	{
		static const char msg[] = "error stopped\n";
		int fd = open(sigStatusTmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (fd >= 0) { ssize_t w = write(fd, msg, sizeof msg - 1); (void)w; close(fd); rename(sigStatusTmp, sigStatus); }
		unlink(sigPid);
		_exit(128 + sig);
	}
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
	// a sync's report also kept on the card, whoever started it (the menu, leaving a game, Wi-Fi: their output goes
	// nowhere): banana/logs/sync.log, the one before as sync.log.1
	// (opened at the first line said, so a sync that has nothing to say, as an automatic one with Auto sync off, doesn't
	// push the last report to sync.log.1)
	bool logWanted = false;
	namespace
	{
		FILE* logFile = nullptr;
		void openLog()
		{
			std::string dir = ROOT + "/banana/logs", path = dir + "/sync.log";
			if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST)
			{
				return;
			}
			rename(path.c_str(), (path + ".1").c_str());
			logFile = fopen(path.c_str(), "w");
			if (logFile)
			{
				// (UTC: the menu and the Wi-Fi script start syncs with different time zones)
				time_t now = time(nullptr);
				char when[32];
				strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S UTC", gmtime(&now));
				fprintf(logFile, "%s %s\n", VERSION, when);
			}
		}
	}
	void say(const char* fmt, ...)
	{
		va_list a; va_start(a, fmt);
		if (logWanted && !logFile)
		{
			logWanted = false;
			openLog();
		}
		if (logFile)
		{
			va_list b;
			va_copy(b, a);
			vfprintf(logFile, fmt, b);
			va_end(b);
			fputc('\n', logFile);
			fflush(logFile);
		}
		vprintf(fmt, a); va_end(a); printf("\n"); fflush(stdout);
	}

	// ---- files --------------------------------------------------------------------------------------------------------
	double fileTime(const std::string& p)
	{
		struct stat st;
		return stat(p.c_str(), &st) == 0 ? st.st_mtim.tv_sec + st.st_mtim.tv_nsec / 1e9 : 0;
	}
	std::string readFile(const std::string& p, bool* ok)
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
	// a rename made durable: the directory holding the new name synced too (exFAT and ext4 keep a rename in the
	// directory's own blocks, which the file's fsync doesn't write). EINVAL: a filesystem that can't sync a directory
	bool syncDir(const std::string& dir)
	{
		int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (fd < 0) return false;
		bool ok = fsync(fd) == 0 || errno == EINVAL;
		close(fd);
		return ok;
	}
	// written beside, synced, renamed over, the folder synced
	bool writeAtomic(const std::string& path, const std::string& data, mode_t mode)
	{
		mkdirs(dirOf(path));
		std::string tmp = path + ".new";
		unlink(tmp.c_str());	// (one left by a run cut short would keep its own mode, not this one)
		int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
		if (fd < 0) return false;
		size_t at = 0;
		while (at < data.size())
		{
			ssize_t w = write(fd, data.data() + at, data.size() - at);
			if (w < 0 && errno == EINTR) continue;
			if (w <= 0) break;
			at += (size_t)w;
		}
		bool ok = at == data.size() && fsync(fd) == 0;
		ok &= close(fd) == 0;
		if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
		return syncDir(dirOf(path));
	}
	long long freeBytes(const std::string& p)
	{
		struct statvfs v;
		std::string d = p;
		while (!exists(d) && d.size() > 1) d = dirOf(d);
		return statvfs(d.c_str(), &v) == 0 ? (long long)v.f_bavail * v.f_frsize : -1;
	}
	// the second SD card's roms folder, the one the library shows games from: the sd2 script writes its path in
	// <run>/sd2/roms while the card is in, has a roms folder and Settings shows its games; "" otherwise
	std::string sd2Roms()
	{
		std::string p = trim(readFile(RUN + "/sd2/roms"));
		if (p.empty() || p == ROMS || !exists(p))
		{
			return "";
		}
		return p;
	}
	std::string romsRelative(const std::string& path)
	{
		if (path.compare(0, ROMS.size() + 1, ROMS + "/") == 0) return path.substr(ROMS.size() + 1);
		std::string sd2 = sd2Roms();
		if (!sd2.empty() && path.compare(0, sd2.size() + 1, sd2 + "/") == 0) return path.substr(sd2.size() + 1);
		return path;
	}
	std::string romPath(const std::string& rel)
	{
		std::string p = ROMS + "/" + rel;
		if (exists(p)) return p;
		std::string sd2 = sd2Roms();
		return !sd2.empty() && exists(sd2 + "/" + rel) ? sd2 + "/" + rel : p;
	}
	// a second SD card that's in but has no roms folder (a fresh one): the folder made and the sd2 script run, as Files
	// does for a roms folder made there, so the library shows its games and sd2Roms has it. The card at /run/sd2 (the
	// test: BANANA_SD2, its scan BANANA_SD2_SCAN)
	void sd2MakeRoms()
	{
		const char* fake = getenv("BANANA_SD2");
		std::string mnt = fake ? fake : "/run/sd2";
		bool in = fake ? exists(mnt) : readFile("/proc/mounts").find(" " + mnt + " ") != std::string::npos;
		if (!in || dryRun)
		{
			return;
		}
		mkdirs(mnt + "/roms");
		if (!exists(mnt + "/roms"))
		{
			return;
		}
		say("made a roms folder on the second SD card");
		const char* scan = getenv("BANANA_SD2_SCAN");
		std::string cmd = scan ? "'" + std::string(scan) + "'" : "flock -o /run/banana/sd2.lock /usr/libexec/banana/sd2 scan";
		if (system((cmd + " </dev/null >/dev/null 2>&1").c_str()) != 0) {}
	}
	// empty folders left behind by a deleted file, up to (not including) top
	void pruneDirs(std::string d, const std::string& top)
	{
		while (d.size() > top.size() && d.compare(0, top.size(), top) == 0 && rmdir(d.c_str()) == 0) d = dirOf(d);
	}

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

	// the systems list (systems.conf, as the menu reads it): the folders of the systems that make their own covers
	// ("covers = own"), whose server covers aren't downloaded
	std::set<std::string> ownCoversOf(const std::string& path)
	{
		std::set<std::string> r;
		std::ifstream f(path);
		std::string id, folder, covers;
		auto done = [&]()
		{
			if (!id.empty() && covers == "own")
			{
				r.insert(folder.empty() ? id : folder);
			}
		};
		for (std::string ln; std::getline(f, ln);)
		{
			ln = trim(ln);
			if (ln.empty() || ln[0] == '#')
			{
				continue;
			}
			if (ln[0] == '[' && ln.back() == ']')
			{
				done();
				id = lower(trim(ln.substr(1, ln.size() - 2)));
				folder.clear();
				covers.clear();
				continue;
			}
			size_t e = ln.find('=');
			if (e == std::string::npos)
			{
				continue;
			}
			std::string k = lower(trim(ln.substr(0, e))), v = trim(ln.substr(e + 1));
			if (k == "folder")
			{
				folder = v;
			}
			else if (k == "covers")
			{
				covers = lower(v);
			}
		}
		done();
		return r;
	}

	// ---- libcurl, dlopen()ed as banana-ui and banana-scrape do it --------------------------------------------------
	Curl cu;
	namespace
	{
	enum
	{
		O_WRITEDATA = 10001, O_URL = 10002, O_POSTFIELDS = 10015, O_USERAGENT = 10018, O_HTTPHEADER = 10023,
		O_HEADERDATA = 10029, O_CUSTOMREQUEST = 10036, O_MIMEPOST = 10269, O_WRITEFUNCTION = 20011, O_HEADERFUNCTION = 20079,
		O_XFERINFOFUNCTION = 20219, O_XFERINFODATA = 10057, O_NOPROGRESS = 43, O_TIMEOUT = 13, O_LOW_SPEED_LIMIT = 19,
		O_LOW_SPEED_TIME = 20, O_CONNECTTIMEOUT = 78, O_NOSIGNAL = 99, O_FOLLOWLOCATION = 52, O_POSTFIELDSIZE_LARGE = 30120,
		O_HTTPGET = 80, O_CAINFO = 10065, O_SSL_VERIFYPEER = 64, O_SSL_VERIFYHOST = 81, I_RESPONSE_CODE = 0x200002
	};
	}
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
	// a URL a server gave as it is made safe to request: what a URL can't hold escaped (RomM's cover is
	// ...big.png?ts=<its updated_at>, a datetime with a space and a "+": curl refused every cover), the rest kept
	std::string urlFix(const std::string& u)
	{
		std::string r;
		for (unsigned char c : u)
		{
			if (c <= ' ' || c >= 0x7f || c == '"' || c == '<' || c == '>' || c == '\\' || c == '^' || c == '`' || c == '{' || c == '|' || c == '}' || c == '+')
			{
				char b[4];
				snprintf(b, sizeof b, "%%%02X", c);
				r += b;
			}
			else
			{
				r += (char)c;
			}
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

	namespace
	{
	size_t onBody(char* p, size_t sz, size_t n, void* u) { ((std::string*)u)->append(p, sz * n); return sz * n; }
	size_t onFile(char* p, size_t sz, size_t n, void* u) { return fwrite(p, sz, n, (FILE*)u) * sz; }
	size_t onHeader(char* p, size_t sz, size_t n, void* u)
	{
		std::string l(p, sz * n);
		size_t c = l.find(':');
		if (c != std::string::npos) (*(std::map<std::string, std::string>*)u)[lower(trim(l.substr(0, c)))] = trim(l.substr(c + 1));
		return sz * n;
	}
	}
	std::function<void(long long, long long)> progress;	// (bytes so far, of) for the download running
	namespace
	{
	int onProgress(void*, long long dltotal, long long dlnow, long long, long long)
	{
		if (progress) progress(dlnow, dltotal);
		return 0;
	}
	}

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
		if (!CA.empty()) cu.setopt(h, O_CAINFO, CA.c_str());	// (the owner's own CA, for a server with a certificate it signed)
		if (!tlsVerify) { cu.setopt(h, O_SSL_VERIFYPEER, 0L); cu.setopt(h, O_SSL_VERIFYHOST, 0L); }	// (a self-signed one, the owner's choice)
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
		if (rc != 0)
		{
			r.curl = rc;
			r.err = cu.strerror(rc);
			if (r.err == "Error")
			{
				r.err += " (curl " + std::to_string(rc) + ")";	// (a libcurl without its messages, as the image's: which one)
			}
			cu.cleanup(h);
			h = nullptr;
			return r;
		}
		cu.getinfo(h, I_RESPONSE_CODE, &r.code);
		return r;
	}

	// ---- the card's sync state: /data/banana/romsync.json --------------------------------------------------------
	namespace
	{
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
	void readNotes(const JVal* j, std::map<std::string, SlotNote>& out)
	{
		if (!j || !j->isObj()) return;
		for (auto& kv : j->obj)
		{
			const JVal& v = *kv.second;
			out[kv.first] = { v.str("path"), v.str("why"), v.str("local_md5"), v.str("server_md5"), v.str("device"), v.str("updated_at"), v.str("vault"), (long long)v.num("save_id") };
		}
	}
	std::string notesJson(const std::map<std::string, SlotNote>& m)
	{
		std::string o = "{";
		bool first = true;
		for (auto& kv : m)
		{
			const SlotNote& n = kv.second;
			o += std::string(first ? "\n" : ",\n") + "  " + jsonStr(kv.first) + ": {\"path\": " + jsonStr(n.rel) + (n.why.empty() ? "" : ", \"why\": " + jsonStr(n.why)) +
				", \"local_md5\": " + jsonStr(n.localMd5) + ", \"save_id\": " + std::to_string(n.saveId) + ", \"server_md5\": " + jsonStr(n.serverMd5) +
				", \"device\": " + jsonStr(n.device) + ", \"updated_at\": " + jsonStr(n.updated) + ", \"vault\": " + jsonStr(n.vault) + "}";
			first = false;
		}
		return o + (first ? "}" : "\n }");
	}
	}
	Conf loadConf()
	{
		Conf c;
		JPtr j = banana::readJsonFile(CONF);
		if (!j || !j->isObj()) return c;
		c.backend = j->str("backend", "romhost");
		c.server = j->str("server"); c.token = j->str("token"); c.name = j->str("name"); c.serverName = j->str("serverName"); c.serverVersion = j->str("serverVersion");
		c.last = j->str("last"); c.lastTime = j->num("lastTime");
		if (const JVal* d = j->get("device")) { c.deviceNum = d->isNum(); c.device = d->isNum() ? fmtNum(d->n) : d->s; }
		if (const JVal* st = j->get("state"))
		{
			c.etag = st->str("etag");
			if (const JVal* f = st->get("files")) if (f->isObj()) for (auto& kv : f->obj) c.files[kv.first] = kv.second->s;
			if (const JVal* w = st->get("where"))
			{
				if (w->isObj())
				{
					for (auto& kv : w->obj)
					{
						c.where[kv.first] = kv.second->s;
					}
				}
			}
			if (const JVal* g = st->get("gamelist")) if (g->isObj())
				for (auto& kv : g->obj) for (auto& p : kv.second->arr) c.gamelist[kv.first].push_back(p->s);
		}
		readSaves(j->get("saves"), c.saves);
		readSaves(j->get("states"), c.states);
		c.stateVersion = (int)j->num("stateVersion", 1);
		if (const JVal* sl = j->get("slots")) if (sl->isObj())
			for (auto& kv : sl->obj) c.slots[kv.first] = { kv.second->str("md5"), kv.second->str("path"), (long long)kv.second->num("save_id"), kv.second->str("server_md5") };
		readNotes(j->get("conflicts"), c.conflicts);
		readNotes(j->get("held"), c.held);
		if (const JVal* qu = j->get("queue")) if (qu->isObj())
			for (auto& kv : qu->obj) c.queue[kv.first] = { kv.second->str("md5"), kv.second->str("vault"), kv.second->num("since") };
		return c;
	}
	bool saveConf(const Conf& c)
	{
		if (dryRun) return true;
		std::string o = "{\n \"backend\": " + jsonStr(c.backend) + ",\n \"server\": " + jsonStr(c.server) + ",\n \"token\": " + jsonStr(c.token) +
			",\n \"device\": " + (c.deviceNum && !c.device.empty() ? c.device : jsonStr(c.device)) + ",\n \"name\": " + jsonStr(c.name) +
			",\n \"serverName\": " + jsonStr(c.serverName) + ",\n \"serverVersion\": " + jsonStr(c.serverVersion) + ",\n \"last\": " + jsonStr(c.last) + ",\n \"lastTime\": " + fmtNum(c.lastTime) +
			",\n \"state\": {\"etag\": " + jsonStr(c.etag) + ", \"files\": {";
		bool first = true;
		for (auto& kv : c.files) { o += std::string(first ? "\n  " : ",\n  ") + jsonStr(kv.first) + ": " + jsonStr(kv.second); first = false; }
		o += "},\n  \"where\": {";
		first = true;
		for (auto& kv : c.where)
		{
			o += std::string(first ? "\n  " : ",\n  ") + jsonStr(kv.first) + ": " + jsonStr(kv.second);
			first = false;
		}
		o += "},\n  \"gamelist\": {";
		first = true;
		for (auto& kv : c.gamelist)
		{
			o += std::string(first ? "\n  " : ",\n  ") + jsonStr(kv.first) + ": [";
			for (size_t i = 0; i < kv.second.size(); i++) o += (i ? ", " : "") + jsonStr(kv.second[i]);
			o += "]";
			first = false;
		}
		o += "}},\n \"saves\": " + savesJson(c.saves, c.backend == "romm") + ",\n \"states\": " + savesJson(c.states, true);
		if (c.stateVersion >= 2)
		{
			o += ",\n \"stateVersion\": " + std::to_string(c.stateVersion) + ",\n \"slots\": {";
			first = true;
			for (auto& kv : c.slots)
			{
				o += std::string(first ? "\n  " : ",\n  ") + jsonStr(kv.first) + ": {\"md5\": " + jsonStr(kv.second.md5) + ", \"save_id\": " + std::to_string(kv.second.saveId) + ", \"path\": " + jsonStr(kv.second.rel) +
					(kv.second.wire.empty() ? "" : ", \"server_md5\": " + jsonStr(kv.second.wire)) + "}";
				first = false;
			}
			o += std::string(first ? "}" : "\n }") + ",\n \"conflicts\": " + notesJson(c.conflicts) + ",\n \"held\": " + notesJson(c.held);
			o += ",\n \"queue\": {";
			first = true;
			for (auto& kv : c.queue)
			{
				o += std::string(first ? "\n  " : ",\n  ") + jsonStr(kv.first) + ": {\"md5\": " + jsonStr(kv.second.md5) + ", \"vault\": " + jsonStr(kv.second.vault) + ", \"since\": " + fmtNum(kv.second.since) + "}";
				first = false;
			}
			o += first ? "}" : "\n }";
		}
		o += "\n}\n";
		return writeAtomic(CONF, o, 0600);	// (the token: only root reads it)
	}

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
	namespace
	{
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
	// BananaOS: DraStic's .dsv and BananaDS's .sav brought together (nds-saves <rom>), before a sync (the .sav is the one
	// that syncs) and after a DS save is installed (DraStic's then matches it). Only on the card's own /data (nds-saves
	// knows no other root)
	namespace
	{
	std::string ndsTool()
	{
		const char* tool = getenv("BANANA_NDS_SAVES");
		std::string t = tool ? tool : (ROOT == "/data" ? "/usr/libexec/banana/nds-saves" : "");
		return t.empty() || access(t.c_str(), X_OK) != 0 ? "" : t;
	}
	void runNdsSaves(const std::string& tool, const std::string& rom)
	{
		std::string cmd = "'" + tool + "' '";
		for (char ch : rom) { if (ch == '\'') cmd += "'\\''"; else cmd += ch; }
		cmd += "' >/dev/null 2>&1";
		if (system(cmd.c_str()) != 0) {}
	}
	}
	void savesFlatFirst()
	{
		const char* tool = getenv("BANANA_SAVES_FLAT");
		std::string t = tool ? tool : (ROOT == "/data" ? "/usr/libexec/banana/saves-flat" : "");
		if (t.empty() || access(t.c_str(), X_OK) != 0) return;
		fflush(stdout); fflush(stderr);
		pid_t pid = fork();
		if (pid == 0)
		{
			int nul = open("/dev/null", O_WRONLY);
			if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); }
			setenv("BANANA_DATA", ROOT.c_str(), 1);
			setenv("BANANA_RUN", RUN.c_str(), 1);
			execl(t.c_str(), t.c_str(), (char*)nullptr);
			_exit(127);
		}
		int st;
		if (pid > 0) while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
	}
	void ndsSavesFirst()
	{
		std::string t = ndsTool();
		if (t.empty()) return;
		DIR* d = opendir((ROOT + "/drastic/backup").c_str());
		if (!d) return;
		std::vector<std::string> games;
		while (dirent* e = readdir(d)) { std::string n = e->d_name; if (n.size() > 4 && lower(n.substr(n.size() - 4)) == ".dsv") games.push_back(n.substr(0, n.size() - 4)); }
		closedir(d);
		for (auto& g : games) runNdsSaves(t, ROMS + "/nds/" + g + ".nds");
	}
	void ndsSaves(const std::string& romPath)
	{
		std::string t = ndsTool();
		if (!t.empty()) runNdsSaves(t, romPath);
	}
	// a game running (bananad's /run/banana/game.pid): its emulator may be writing its save
	bool gameRunning()
	{
		int pid = 0;
		unsigned long long start = 0;
		std::ifstream(RUN + "/game.pid") >> pid >> start;
		if (pid <= 0 || kill(pid, 0) != 0) return false;
		if (!start) return true;	// (a pid file from before the start time was kept)
		std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
		std::string ln;
		if (!std::getline(f, ln)) return false;
		size_t q = ln.rfind(')');
		if (q == std::string::npos) return false;
		std::istringstream in(ln.substr(q + 1));
		std::string v;
		for (int i = 0; i < 20 && in >> v; i++) if (i == 19) return strtoull(v.c_str(), nullptr, 10) == start;
		return false;
	}
	// downloaded to <dst>.part, checked, renamed. The part file is left for a retry when the connection broke
	bool fetchTo(const std::string& url, const std::vector<std::string>& headers, const std::string& dst, long long size,
		const std::string& algo, const std::string& want, std::string& err, std::string* got)
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
		if (!syncDir(dirOf(dst))) { err = std::string("sync: ") + strerror(errno); return false; }
		return true;
	}

	// ---- this card as a RomM device ------------------------------------------------------------------------------------
	std::string cardId()
	{
		std::string p = ROOT + "/banana/sync-id", id = trim(readFile(p));
		bool ok = id.size() == 12 && id.find_first_not_of("0123456789abcdef") == std::string::npos;
		if (ok) return id;
		std::ifstream r("/dev/urandom", std::ios::binary);
		unsigned char b[6] = {};
		if (!r.read((char*)b, sizeof b)) return "";
		char h[13];
		snprintf(h, sizeof h, "%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
		mkdirs(dirOf(p));
		return writeAtomic(p, std::string(h) + "\n") ? h : "";
	}
	std::string osVersion()
	{
		std::string v = trim(readFile("/etc/banana/version"));
		return v.empty() ? "unknown" : v;
	}
	std::string deviceFields(const Conf& c)
	{
		return "\"name\": " + jsonStr(c.name.empty() ? "BananaOS" : c.name) + ", \"platform\": \"BananaOS\", \"client\": \"bananaos\", \"client_version\": " +
			jsonStr(osVersion()) + ", \"hostname\": " + jsonStr("bananaos-" + cardId()) + ", \"sync_mode\": \"api\"";
	}
	std::string registerDevice(Conf& c)
	{
		if (cardId().empty()) return "no banana/sync-id";
		std::string fields = deviceFields(c);
		Req q; q.headers = { "Authorization: Bearer " + c.token, "Content-Type: application/json" };
		if (!c.device.empty())
		{
			// (the one held: its version and name brought up to date)
			std::string b = "{" + fields + "}";
			q.method = "PUT"; q.url = c.server + "/api/devices/" + urlEsc(c.device); q.body = &b;
			Resp r = http(q);
			if (r.code == 200) return "";
			if (r.code != 404) return r.error();
			c.device.clear();	// (deleted in RomM's web UI: a new one)
		}
		// (allow_existing: RomM gives back the device with this hostname and platform, when it has one)
		std::string b = "{" + fields + ", \"allow_existing\": true}";
		q.method = "POST"; q.url = c.server + "/api/devices"; q.body = &b;
		Resp r = http(q);
		JPtr j = r.code == 200 || r.code == 201 ? r.json() : nullptr;
		if (!j || !j->isObj() || j->str("device_id").empty()) return r.error();
		c.device = j->str("device_id"); c.deviceNum = false;
		return "";
	}
}
