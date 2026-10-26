// banana-fetch: downloads from a NAS or a computer (SMB2/3, SFTP, FTP/FTPS) and unpacking archives (zip, 7z, rar) onto
// the cards, for the shelf's Downloads (banana-ui's settings_downloads.cpp). It runs only while there is work:
//   banana-fetch serve                          the Downloads pages' session: requests on stdin, answers on stdout
//   banana-fetch run                            the jobs in banana/downloads, one after another (runner.h)
//   banana-fetch ls <server id> <path>          (over SSH) a folder of a saved server
//   banana-fetch get <server id> <path> <file>  (over SSH) one file, resumed from <file>.part
//   banana-fetch inspect <archive> [password]   (over SSH) what is in an archive
//   banana-fetch unpack <archive> <folder> [password]
// Its files: BANANA_DATA (banana/ on the card: nas.conf, nas.key, downloads/, logs/downloads.log) and BANANA_RUN
// (/run/banana: fetch/status, fetch.finished, transfer.pid). The tests' own (BANANA_FETCH_TEST=1, nothing else): the
// "dir" protocol (remote_dir.cpp), _FAST tries again at once, _RATE a job's downloads at that many bytes a second at
// most, _FREE and _MEM a card's room and the memory free, _SERIAL another handheld, _LINGER the runner's last look at
// the queue that many ms before it lets go of its lock.
//
// The session's requests, one a line, the fields separated by tabs (tabs, line ends and backslashes escaped); each
// answer ends with a line "ok ...", "end ..." or "err <word> <message> ...":
//   connect <id> [<password>]          the login, with the password typed, else the one kept: ok <fingerprint>
//                                      <home> (where the login lands: the start without a start folder);
//                                      err hostkey <message> <pinned> <seen>; err password <message> (none kept)
//   list <path>                        "e <d|f|l> <size> <mtime> <name>" a line, then end <count>
//   walk <dir> <to> <hidden> <merge> <d|f|l>:<size>:<name>...
//                                      the files under the chosen entries, folders gone through: "f <size> <remote>
//                                      <local>" a line, then end <count> <bytes>. merge: folder names (lower case,
//                                      comma-separated) whose files go into <to> itself (the server's GBA into roms/gba)
//   inspect <archive> [<password>]     "file <name>" lines (the first 100), then end <files> <bytes> <largest>
//                                      <dictionary> <encrypted 0|1> <memory free>
//   put <key=value>...                 a server added (no id) or changed: ok <id>. password=... sets the password,
//                                      none keeps the one kept
//   delete <id>, trust <id> <fingerprint>, target <id> <remote folder> <card:folder>: ok
//   quit
#include "archive.h"
#include "jobfile.h"
#include "names.h"
#include "paths.h"
#include "runner.h"
#include "servers.h"
#include "transfer.h"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <map>
#include <poll.h>
#include <set>
#include <unistd.h>

using namespace banana::fetch;

namespace
{
	void say(const std::string& text)
	{
		for (size_t w = 0; w < text.size();)
		{
			ssize_t k = write(1, text.data() + w, text.size() - w);
			if (k < 0 && errno == EINTR) continue;
			if (k <= 0) _exit(0);	// the menu went away
			w += (size_t)k;
		}
	}

	std::string line(const std::vector<std::string>& f) { return joinFields(f) + "\n"; }
	std::string errLine(const Result& r) { return line({ "err", errWord(r.err), r.msg }); }

	// a saved server, its password opened (or the one typed: an empty one is a password too, an account's that has
	// none); err password when there is none
	Result openServer(const Paths& p, const std::string& id, const std::string* typed, ServerRec& out)
	{
		std::vector<ServerRec> list = loadServers(p.servers());
		ServerRec* s = findServer(list, id);
		if (!s) return Result(Err::NOT_FOUND, "That server isn't in the list any more.");
		out = *s;
		if (out.guest) return Result();
		if (typed) { out.password = *typed; return Result(); }
		SecretBox box;
		if (!out.sealed.empty() && box.open(p.key()) && box.unseal(out.sealed, out, out.password)) return Result();
		out.password.clear();
		return Result(Err::PASSWORD, "The password for " + out.title() + " is needed.");
	}

	// a password sealed without the SoC's serial number (the device tree had none): banana/nas.key alone opens it, so the
	// card does without the handheld. Said in the log, as the docs promise more.
	void noteUnbound(const Paths& p, const SecretBox& box, const ServerRec& s)
	{
		if (!box.bound()) logLine(p, "no serial number in /proc/device-tree: the password of " + s.title() + " is sealed with banana/nas.key alone");
	}

	// the first connection to a server pins its key (or certificate): trust on first use
	void pinFirst(const Paths& p, ServerRec& s, const std::string& fp)
	{
		if (fp.empty() || !s.hostkey.empty()) return;
		s.hostkey = fp;
		updateServers(p.servers(), [&](std::vector<ServerRec>& list)
		{
			ServerRec* x = findServer(list, s.id);
			if (!x || !x->hostkey.empty()) return false;
			x->hostkey = fp;
			return true;
		});
	}

	Result connectTo(const Paths& p, ServerRec& s, std::unique_ptr<Backend>& be)
	{
		be = makeBackend(s);
		if (!be) return Result(Err::UNSUPPORTED, "This build of Banana can't talk " + s.protocol + ".");
		Result r = be->connect();
		if (!r) { be.reset(); return r; }
		pinFirst(p, s, be->fingerprint());
		return r;
	}

	std::string hostkeyError(const Result& r, const ServerRec& s)
	{
		return line({ "err", "hostkey", r.msg, s.hostkey, r.got });
	}

	// ---- walk: the chosen entries' files, folders gone through -------------------------------------------------------
	struct Walk
	{
		std::function<Result(const std::string&, std::vector<Entry>&)> list;	// the session's, which connects again
		bool hidden;
		std::string out;
		int count = 0;
		int64_t bytes = 0;
		Result err;
	};

	// a link: a folder when it lists. false (w.err) when the connection, not the link, failed
	bool linkIsDir(Walk& w, const std::string& remote, bool& dir)
	{
		std::vector<Entry> probe;
		Result r = w.list(remote, probe);
		if (!r && transient(r.err)) { w.err = r; return false; }
		dir = (bool)r;
		return true;
	}

	void addFile(Walk& w, const std::string& remote, const std::string& local, int64_t size)
	{
		w.out += line({ "f", std::to_string(size), remote, local });
		w.count++;
		if (size > 0) w.bytes += size;
		if (w.count > 20000) w.err = Result(Err::OTHER, "That is more than 20000 files: choose fewer.");
	}

	bool walkDir(Walk& w, const std::string& remote, const std::string& local, int depth)
	{
		if (depth > 16) { w.err = Result(Err::OTHER, remote + ": the folders go too deep."); return false; }
		std::vector<Entry> es;
		Result r = w.list(remote, es);
		if (!r) { w.err = r; return false; }
		sortEntries(es);
		for (auto& e : es)
		{
			if (!w.hidden && !e.name.empty() && e.name[0] == '.') continue;
			std::string rp = joinPath(remote, e.name), lp = local + "/" + safeName(e.name);
			bool dir = e.dir;
			if (e.link && !linkIsDir(w, rp, dir)) return false;
			if (dir) { if (!walkDir(w, rp, lp, depth + 1)) return false; continue; }
			addFile(w, rp, lp, e.link ? -1 : e.size);
			if (!w.err.msg.empty()) return false;
		}
		return true;
	}

	// ---- the session ---------------------------------------------------------------------------------------------
	class Session
	{
	public:
		explicit Session(const Paths& paths) : p(paths) {}

		void handle(const std::vector<std::string>& f)
		{
			const std::string& cmd = f[0];
			if (cmd == "connect" && f.size() >= 2) connect(f);
			else if (cmd == "list" && f.size() >= 2) list(f[1]);
			else if (cmd == "walk" && f.size() >= 5) walk(f);
			else if (cmd == "inspect" && f.size() >= 2) inspect(f);
			else if (cmd == "put") put(f);
			else if (cmd == "delete" && f.size() >= 2) del(f[1]);
			else if (cmd == "trust" && f.size() >= 3) trust(f[1], f[2]);
			else if (cmd == "target" && f.size() >= 4) target(f[1], f[2], f[3]);
			else say(line({ "err", "other", "Unknown request: " + cmd }));
		}

		void idle(double idleMs)
		{
			if (be && idleMs > 30000) be->keepalive();
		}

	private:
		const Paths& p;
		std::unique_ptr<Backend> be;
		ServerRec cur;
		Result lost = Result(Err::OTHER, "Not connected.");	// be empty: why (the last try's failure)

		void connect(const std::vector<std::string>& f)
		{
			be.reset();
			Result r = openServer(p, f[1], f.size() > 2 ? &f[2] : nullptr, cur);
			if (r) r = connectTo(p, cur, be);
			if (!r) lost = r;
			if (r.err == Err::HOSTKEY) { say(hostkeyError(r, cur)); return; }
			if (!r) { say(errLine(r)); return; }
			if (f.size() > 2 && cur.remember && !cur.guest) keepPassword();
			say(line({ "ok", be->fingerprint(), be->home() }));
		}

		// a password typed at the login of a server that remembers it: kept (sealed) now that it opened
		void keepPassword()
		{
			SecretBox box;
			if (!box.open(p.key())) return;
			std::string sealed = box.seal(cur.password, cur);
			if (sealed.empty()) return;
			bool kept = false;
			updateServers(p.servers(), [&](std::vector<ServerRec>& list)
			{
				ServerRec* x = findServer(list, cur.id);
				if (!x || !x->remember || x->guest || x->user != cur.user || x->host != cur.host) return false;
				x->sealed = sealed;
				return kept = true;
			});
			if (kept) noteUnbound(p, box, cur);
		}

		// a listing, connecting again if the connection died meanwhile (an idle NAS hangs up; Wi-Fi slept), or if the
		// last try failed on the network (Wi-Fi not back yet after a sleep): once a request (again: whether it may still)
		Result listing(const std::string& path, std::vector<Entry>& out, bool& again)
		{
			Result r = be ? be->list(path, out) : lost;
			if (r || !transient(r.err) || !again) return r;
			again = false;
			out.clear();
			if (!(r = connectTo(p, cur, be))) { lost = r; return r; }
			return be->list(path, out);
		}

		void list(const std::string& path)
		{
			std::vector<Entry> es;
			bool again = true;
			Result r = listing(cleanPath(path), es, again);
			if (r.err == Err::HOSTKEY) { say(hostkeyError(r, cur)); return; }
			if (!r) { say(errLine(r)); return; }
			sortEntries(es);
			std::string out;
			for (auto& e : es)
				out += line({ "e", e.dir ? "d" : e.link ? "l" : "f", std::to_string(e.size), std::to_string(e.mtime), e.name });
			say(out + line({ "end", std::to_string(es.size()) }));
		}

		void walk(const std::vector<std::string>& f)
		{
			std::string dir = cleanPath(f[1]), to = f[2];
			std::set<std::string> merge;
			for (size_t a = 0; a <= f[4].size();)
			{
				size_t b = f[4].find(',', a);
				if (b == std::string::npos) b = f[4].size();
				if (b > a) merge.insert(f[4].substr(a, b - a));
				a = b + 1;
			}
			Walk w;
			bool again = true;	// (the walk's listings, like one: a new connection once at most)
			w.list = [&](const std::string& path, std::vector<Entry>& out) { return listing(path, out, again); };
			w.hidden = f[3] == "1";
			for (size_t i = 5; i < f.size() && w.err.msg.empty(); i++)
			{
				// "d::name", "f:1234:name", "l::name"
				const std::string& e = f[i];
				size_t c1 = e.find(':'), c2 = c1 == std::string::npos ? c1 : e.find(':', c1 + 1);
				if (c2 == std::string::npos) continue;
				char kind = e[0];
				int64_t size = atoll(e.substr(c1 + 1, c2 - c1 - 1).c_str());
				std::string name = e.substr(c2 + 1), rp = joinPath(dir, name), lp = to + "/" + safeName(name);
				bool isDir = kind == 'd';
				if (kind == 'l' && !linkIsDir(w, rp, isDir)) break;
				if (isDir)
				{
					// the server's folder of the same system as the target (its "GBA" into roms/gba): not a folder in it
					if (merge.count(lower(name))) lp = to;
					walkDir(w, rp, lp, 1);
				}
				else addFile(w, rp, lp, kind == 'l' ? -1 : size);
			}
			if (w.err.err == Err::HOSTKEY) { say(hostkeyError(w.err, cur)); return; }
			if (!w.err.msg.empty()) { say(errLine(w.err)); return; }
			say(w.out + line({ "end", std::to_string(w.count), std::to_string(w.bytes) }));
		}

		void inspect(const std::vector<std::string>& f)
		{
			ArchiveOptions o;
			if (f.size() > 2) o.password = f[2];
			ArchiveInfo info;
			Result r = inspectArchive(f[1], o, info);
			if (!r) { say(errLine(r)); return; }
			std::string out;
			for (size_t i = 0; i < info.files.size() && i < 100; i++) out += line({ "file", info.files[i] });
			say(out + line({ "end", std::to_string(info.files.size()), std::to_string(info.unpacked), std::to_string(info.largest),
				std::to_string(info.dictionary), info.encrypted ? "1" : "0", std::to_string(memAvailable()) }));
		}

		void put(const std::vector<std::string>& f)
		{
			std::string id, newId_;
			std::map<std::string, std::string> kv;
			bool setPassword = false;
			for (size_t i = 1; i < f.size(); i++)
			{
				size_t eq = f[i].find('=');
				if (eq == std::string::npos) continue;
				std::string k = f[i].substr(0, eq), v = f[i].substr(eq + 1);
				if (k == "id") id = v;
				else { kv[k] = v; if (k == "password") setPassword = true; }
			}
			SecretBox box;
			bool boxOk = box.open(p.key()), sealedNow = false;
			ServerRec saved;
			bool ok = updateServers(p.servers(), [&](std::vector<ServerRec>& list)
			{
				ServerRec* s = id.empty() ? nullptr : findServer(list, id);
				bool existing = s != nullptr;
				ServerRec old;
				if (s) old = *s;
				else
				{
					list.push_back(ServerRec());
					s = &list.back();
					s->id = newId();
				}
				auto get = [&](const char* k, const std::string& def) { auto it = kv.find(k); return it == kv.end() ? def : it->second; };
				s->name = get("name", s->name);
				s->protocol = get("protocol", s->protocol.empty() ? "smb" : s->protocol);
				s->host = get("host", s->host);
				s->port = atoi(get("port", std::to_string(s->port)).c_str());
				if (s->port <= 0 || s->port > 65535) s->port = s->defaultPort();
				s->guest = get("guest", s->guest ? "yes" : "no") == "yes" && s->protocol != "sftp";
				s->user = get("user", s->user);
				s->remember = get("remember", s->remember ? "yes" : "no") == "yes";
				s->domain = get("domain", s->domain);
				s->start = get("start", s->start);
				if (!s->start.empty()) s->start = cleanPath(s->start);
				s->tls = get("tls", s->tls ? "yes" : "no") == "yes";
				s->encoding = get("encoding", s->encoding.empty() ? "utf-8" : s->encoding);
				// another address may be another server: its pinned key goes (the targets stay: usually the same
				// NAS at a new address)
				bool moved = existing && (old.host != s->host || old.portOrDefault() != s->portOrDefault() || old.protocol != s->protocol);
				if (moved) s->hostkey.clear();
				if (s->guest) { s->user.clear(); s->sealed.clear(); }
				else if (!s->remember) s->sealed.clear();
				else if (setPassword)
				{
					// (an empty one too: the account has no password, and it is kept that it has none)
					s->sealed = boxOk ? box.seal(kv["password"], *s) : "";
					sealedNow = !s->sealed.empty();
				}
				else if (existing && !old.sealed.empty() && (moved || old.user != s->user))
				{
					// the same password for the server's new address: sealed again for it
					std::string plain;
					s->sealed = boxOk && box.unseal(old.sealed, old, plain) ? box.seal(plain, *s) : "";
				}
				newId_ = s->id;
				saved = *s;
				return true;
			});
			if (!ok) { say(errLine(Result(Err::LOCAL, "Couldn't write banana/nas.conf on the card."))); return; }
			if (sealedNow) noteUnbound(p, box, saved);
			say(line({ "ok", newId_ }));
		}

		void del(const std::string& id)
		{
			updateServers(p.servers(), [&](std::vector<ServerRec>& list)
			{
				size_t n = list.size();
				list.erase(std::remove_if(list.begin(), list.end(), [&](const ServerRec& s) { return s.id == id; }), list.end());
				return list.size() != n;
			});
			say(line({ "ok" }));
		}

		void trust(const std::string& id, const std::string& fp)
		{
			updateServers(p.servers(), [&](std::vector<ServerRec>& list)
			{
				ServerRec* s = findServer(list, id);
				if (!s) return false;
				s->hostkey = fp;
				return true;
			});
			if (cur.id == id) cur.hostkey = fp;
			say(line({ "ok" }));
		}

		void target(const std::string& id, const std::string& dir, const std::string& value)
		{
			updateServers(p.servers(), [&](std::vector<ServerRec>& list)
			{
				ServerRec* s = findServer(list, id);
				if (!s) return false;
				for (auto& t : s->targets) if (t.first == dir) { if (t.second == value) return false; t.second = value; return true; }
				s->targets.push_back({ dir, value });
				if (s->targets.size() > 100) s->targets.erase(s->targets.begin());	// (the oldest)
				return true;
			});
			say(line({ "ok" }));
		}
	};

	int serve(const Paths& p)
	{
		Session s(p);
		std::string buf;
		double last = steadyMs();
		for (;;)
		{
			pollfd pf = { 0, POLLIN, 0 };
			int n = poll(&pf, 1, 30000);
			double now = steadyMs();
			if (n == 0)
			{
				if (now - last > 10 * 60 * 1000.0) return 0;	// nothing asked for ten minutes: the menu forgot us
				s.idle(now - last);
				continue;
			}
			if (n < 0) { if (errno == EINTR) continue; return 1; }
			char tmp[4096];
			ssize_t k = read(0, tmp, sizeof tmp);
			if (k < 0 && errno == EINTR) continue;
			if (k <= 0) return 0;	// the menu closed its end
			buf.append(tmp, (size_t)k);
			for (size_t nl; (nl = buf.find('\n')) != std::string::npos;)
			{
				std::string ln = buf.substr(0, nl);
				buf.erase(0, nl + 1);
				if (!ln.empty() && ln.back() == '\r') ln.pop_back();
				if (ln.empty()) continue;
				std::vector<std::string> f = splitFields(ln);
				if (f[0] == "quit") return 0;
				s.handle(f);
				last = steadyMs();
			}
		}
	}

	int usage()
	{
		fprintf(stderr, "usage: banana-fetch serve | run | ls <server id> <path> | get <server id> <path> <file> |\n"
			"       inspect <archive> [password] | unpack <archive> <folder> [password]\n");
		return 2;
	}
}

int main(int argc, char** argv)
{
	signal(SIGPIPE, SIG_IGN);	// (a server, or the menu, gone while something goes out to it: an error, not the end)
	Paths p = Paths::fromEnv();
	std::string cmd = argc > 1 ? argv[1] : "";
	if (cmd == "serve") return serve(p);
	if (cmd == "run") return runJobs(p);
	if ((cmd == "ls" && argc >= 4) || (cmd == "get" && argc >= 5))
	{
		ServerRec s;
		std::unique_ptr<Backend> be;
		Result r = openServer(p, argv[2], nullptr, s);
		if (r) r = connectTo(p, s, be);
		if (!r) { fprintf(stderr, "banana-fetch: %s\n", r.msg.c_str()); return 1; }
		if (cmd == "ls")
		{
			std::vector<Entry> es;
			if (!(r = be->list(argv[3], es))) { fprintf(stderr, "banana-fetch: %s\n", r.msg.c_str()); return 1; }
			sortEntries(es);
			for (auto& e : es) printf("%s %12lld  %s\n", e.dir ? "d" : e.link ? "l" : "-", (long long)e.size, e.name.c_str());
			return 0;
		}
		// one file, as a job's download: resumed, tried again, the status in fetch/status
		be.reset();
		Downloader dl;
		dl.connect = [&](Result& cr) { std::unique_ptr<Backend> b; cr = connectTo(p, s, b); return b; };
		std::vector<Entry> es;
		Item it;
		it.remote = cleanPath(argv[3]);
		it.local = argv[4];
		{
			std::unique_ptr<Backend> b;
			if (connectTo(p, s, b) && b->list(parentPath(it.remote), es))
				for (auto& e : es) if (e.name == baseName(it.remote)) it.size = e.size;
		}
		Progress prog;
		r = dl.download(it, prog);
		if (!r) { fprintf(stderr, "banana-fetch: %s\n", r.msg.c_str()); return 1; }
		printf("%s: %s\n", it.local.c_str(), bytes(fileSize(it.local)).c_str());
		return 0;
	}
	if ((cmd == "inspect" && argc >= 3) || (cmd == "unpack" && argc >= 4))
	{
		ArchiveOptions o;
		const char* pw = argv[cmd == "inspect" ? 3 : 4];
		if (argc > (cmd == "inspect" ? 3 : 4)) o.password = pw;
		if (cmd == "inspect")
		{
			ArchiveInfo info;
			Result r = inspectArchive(argv[2], o, info);
			if (!r) { fprintf(stderr, "banana-fetch: %s (%s)\n", r.msg.c_str(), errWord(r.err)); return 1; }
			for (auto& f : info.files) printf("%s\n", f.c_str());
			printf("%zu files, %s, the largest %s, dictionary %s%s\n", info.files.size(), bytes(info.unpacked).c_str(),
				bytes(info.largest).c_str(), bytes(info.dictionary).c_str(), info.encrypted ? ", encrypted" : "");
			return 0;
		}
		o.progress = [](double f) { fprintf(stderr, "\r%3d%%", (int)(f * 100)); };
		Result r = extractArchive(argv[2], argv[3], o);
		fprintf(stderr, "\n");
		if (!r) { fprintf(stderr, "banana-fetch: %s (%s)\n", r.msg.c_str(), errWord(r.err)); return 1; }
		return 0;
	}
	return usage();
}
