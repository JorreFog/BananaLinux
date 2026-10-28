// banana-bshare: Bshare, the shelf's tile (settings_bshare.cpp): files between this handheld and the phones
// and computers on the same Wi-Fi, through the open protocol of LocalSend (https://localsend.org, by Tien Do Nam and
// its contributors; protocol v2, https://github.com/localsend/protocol). Bshare is Banana's own small version of
// it: any LocalSend app sees the handheld and sends to it, and the handheld sends to them.
//   banana-bshare serve                       seen by LocalSend's apps, and their files received
//   banana-bshare send <fingerprint> <path>...   files (a folder: all of its files) sent to a device found
// The menu and this talk through small files in /run/banana (the menu watches the folder):
//   bshare-peers     the devices seen: fingerprint, address, port, http or https, name, model, type, when (serve)
//   bshare-request   a device asking to send (Receive on Ask): its id, name, how many files, how big (serve)
//   bshare-answer    "accept <id>", "accept <id> <folder>" (all of them there instead) or "decline <id>" (the menu)
//   bshare-receive   what is being received: waiting/receiving/done/stopped, from whom, files and bytes (serve)
//   bshare-send      what is being sent: the same for a send (send)
// Received files: a game (its extension one system's alone, systems.conf's ext) into that system's folder, anything
// else into banana/received, folders kept. Each transfer is a line in banana/logs/bshare.log.
// Receive (Settings' bshare_receive, read at each request): off (refused), ask (the menu asks), auto (saved at
// once, LocalSend's "Quick Save"). The handheld announces itself over plain HTTP: LocalSend's apps talk to it in it;
// it talks HTTPS to theirs (self-signed certificates, as LocalSend's own apps accept them).
#include "json.h"
#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <ifaddrs.h>
#include <map>
#include <memory>
#include <mutex>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace banana;

namespace
{
	const char* MCAST = "224.0.0.167";
	int port = 53317;
	std::string run = "/run/banana", data = "/data";

	double now()
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	std::string readFile(const std::string& p)
	{
		std::ifstream f(p, std::ios::binary);
		std::stringstream s;
		s << f.rdbuf();
		return s.str();
	}

	// written whole (a temporary file renamed over it): the menu never reads half of it
	void writeFile(const std::string& p, const std::string& s)
	{
		std::string t = p + ".new";
		FILE* f = fopen(t.c_str(), "w");
		if (!f) return;
		fwrite(s.data(), 1, s.size(), f);
		fclose(f);
		rename(t.c_str(), p.c_str());
	}

	std::string conf(const std::string& path, const std::string& key, const std::string& def)
	{
		std::ifstream f(path);
		std::string l, v = def;
		while (std::getline(f, l))
		{
			size_t e = l.find('=');
			if (e == std::string::npos || l[0] == '#') continue;
			std::string k = l.substr(0, e), val = l.substr(e + 1);
			while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
			while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.erase(0, 1);
			while (!val.empty() && (val.back() == ' ' || val.back() == '\r')) val.pop_back();
			if (k == key) v = val;
		}
		return v;
	}

	std::string esc(const std::string& s)
	{
		std::string o;
		for (unsigned char c : s)
		{
			if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
			else if (c == '\n') o += "\\n";
			else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
			else o += (char)c;
		}
		return o;
	}

	std::string randomHex(int n)
	{
		std::random_device rd;
		std::string s;
		for (int i = 0; i < n; i++) s += "0123456789abcdef"[rd() % 16];
		return s;
	}

	// the log on the card: a line a transfer
	void logLine(const std::string& what)
	{
		std::string dir = data + "/banana/logs";
		mkdir(dir.c_str(), 0755);
		FILE* f = fopen((dir + "/bshare.log").c_str(), "a");
		if (!f) return;
		char t[32];
		time_t tt = time(nullptr);
		struct tm tm;
		localtime_r(&tt, &tm);
		strftime(t, sizeof t, "%Y-%m-%d %H:%M:%S", &tm);
		fprintf(f, "%s %s\n", t, what.c_str());
		fclose(f);
		fprintf(stderr, "bshare: %s\n", what.c_str());
	}

	// ---- who this handheld is ------------------------------------------------------------------------------------
	struct Me { std::string alias, model, fingerprint; };

	Me me()
	{
		Me m;
		std::string board = readFile("/etc/banana/board");
		while (!board.empty() && (board.back() == '\n' || board.back() == ' ')) board.pop_back();
		m.model = board == "rgds" ? "RG DS" : "RG DS Plus";
		m.alias = conf(data + "/banana/settings.conf", "bshare_name", "");
		if (m.alias.empty()) m.alias = m.model;
		// the same fingerprint from one start to the next (LocalSend's apps know a device by it)
		std::string idf = data + "/banana/bshare.id";
		m.fingerprint = readFile(idf);
		while (!m.fingerprint.empty() && (m.fingerprint.back() == '\n')) m.fingerprint.pop_back();
		if (m.fingerprint.size() < 16)
		{
			m.fingerprint = randomHex(32);
			writeFile(idf, m.fingerprint + "\n");
		}
		return m;
	}

	std::string infoJson(const Me& m, bool announce, bool withAnnounce)
	{
		std::string j = "{\"alias\":\"" + esc(m.alias) + "\",\"version\":\"2.0\",\"deviceModel\":\"" + esc(m.model) +
			"\",\"deviceType\":\"mobile\",\"fingerprint\":\"" + esc(m.fingerprint) + "\",\"port\":" + std::to_string(port) +
			",\"protocol\":\"http\",\"download\":false";
		if (withAnnounce) j += std::string(",\"announce\":") + (announce ? "true" : "false");
		return j + "}";
	}

	// ---- the devices seen ------------------------------------------------------------------------------------------
	struct Peer { std::string fingerprint, ip, protocol, alias, model, type; int port = 53317; long long seen = 0; };
	std::mutex peersMu;
	std::map<std::string, Peer> peers;

	void writePeers()
	{
		std::string s;
		long long t = (long long)time(nullptr);
		for (auto& p : peers)
			if (t - p.second.seen < 600)	// (gone 10 minutes: not offered any more)
				s += p.second.fingerprint + "\t" + p.second.ip + "\t" + std::to_string(p.second.port) + "\t" + p.second.protocol + "\t" +
					p.second.alias + "\t" + p.second.model + "\t" + p.second.type + "\t" + std::to_string(p.second.seen) + "\n";
		writeFile(run + "/bshare-peers", s);
	}

	void notePeer(const JVal& j, const std::string& ip, const Me& m)
	{
		std::string fp = j.str("fingerprint");
		if (fp.empty() || fp == m.fingerprint) return;
		Peer p;
		p.fingerprint = fp; p.ip = ip;
		p.port = (int)j.num("port", 53317);
		p.protocol = j.str("protocol", "https") == "http" ? "http" : "https";
		p.alias = j.str("alias", ip);
		p.model = j.str("deviceModel");
		p.type = j.str("deviceType");
		for (auto* s : { &p.alias, &p.model, &p.type }) for (auto& c : *s) if (c == '\t' || c == '\n') c = ' ';
		p.seen = (long long)time(nullptr);
		std::lock_guard<std::mutex> l(peersMu);
		peers[fp] = p;
		writePeers();
	}

	// ---- where a received file goes ------------------------------------------------------------------------------
	// a name the card takes, folders kept: no "..", no absolute path, FAT's forbidden characters made _
	std::string safeRel(const std::string& name)
	{
		std::string out, part;
		auto flush = [&]()
		{
			while (!part.empty() && (part.back() == '.' || part.back() == ' ')) part.pop_back();
			if (!part.empty() && part != "." && part != "..") out += (out.empty() ? "" : "/") + part;
			part.clear();
		};
		for (unsigned char c : name)
		{
			if (c == '/' || c == '\\') { flush(); continue; }
			part += (c < 0x20 || strchr("\"*:<>?|", c)) ? '_' : (char)c;
		}
		flush();
		return out.empty() ? "file" : out;
	}

	// systems.conf's games: an extension that is one system's alone -> its folder under roms
	std::map<std::string, std::string> gameFolders()
	{
		std::string path = data + "/banana/systems.conf";
		std::ifstream f(path);
		if (!f) f.open("/etc/banana/systems.conf");
		std::map<std::string, std::vector<std::pair<std::string, bool>>> by;	// ext -> (folder, its first ext)
		std::string l, id, folder;
		std::vector<std::string> exts;
		auto end = [&]()
		{
			if (!id.empty() && (folder.empty() || folder[0] != '/'))
				for (size_t i = 0; i < exts.size(); i++) by[exts[i]].push_back({ folder.empty() ? id : folder, i == 0 });
			id.clear(); folder.clear(); exts.clear();
		};
		while (std::getline(f, l))
		{
			if (!l.empty() && l[0] == '[') { end(); id = l.substr(1, l.find(']') - 1); continue; }
			size_t e = l.find('=');
			if (e == std::string::npos || l[0] == '#') continue;
			std::string k = l.substr(0, e), v = l.substr(e + 1);
			while (!k.empty() && k.back() == ' ') k.pop_back();
			if (k == "folder") { std::istringstream s(v); s >> folder; }
			if (k == "ext") { std::istringstream s(v); std::string x; while (s >> x) exts.push_back(x); }
		}
		end();
		std::map<std::string, std::string> out;
		for (auto& b : by)
		{
			if (b.first == "zip" || b.first == "7z" || b.first == "rar" || b.first == "app") continue;	// (any system's)
			std::string pick;
			for (auto& c : b.second) if (c.second) pick = c.first;	// the system it is the first extension of
			if (pick.empty() && b.second.size() == 1) pick = b.second[0].first;
			if (!pick.empty()) out[b.first] = pick;
		}
		return out;
	}

	std::string lowerExt(const std::string& name)
	{
		size_t d = name.rfind('.');
		if (d == std::string::npos || name.find('/', d) != std::string::npos) return "";
		std::string e = name.substr(d + 1);
		for (auto& c : e) c = (char)tolower((unsigned char)c);
		return e;
	}

	void mkdirs(const std::string& p)
	{
		for (size_t i = 1; i <= p.size(); i++)
			if (i == p.size() || p[i] == '/') mkdir(p.substr(0, i).c_str(), 0755);
	}

	// a free name: "x.gba", then "x (2).gba"...
	std::string freeName(const std::string& path)
	{
		struct stat st;
		if (stat(path.c_str(), &st) != 0) return path;
		size_t slash = path.rfind('/'), dot = path.rfind('.');
		if (dot == std::string::npos || dot < slash) dot = path.size();
		for (int i = 2; i < 1000; i++)
		{
			std::string c = path.substr(0, dot) + " (" + std::to_string(i) + ")" + path.substr(dot);
			if (stat(c.c_str(), &st) != 0) return c;
		}
		return path;
	}

	// ---- receiving -------------------------------------------------------------------------------------------------
	struct RFile { std::string name, token, dest; long long size = -1; bool done = false; };
	struct Session
	{
		std::string id, ip, from;
		std::map<std::string, RFile> files;
		long long total = 0, got = 0;
		int done = 0, failed = 0;
		double last = 0;
		bool games = false, other = false;	// (where they went: a system's folder, banana/received)
		std::string where;			// (a folder chosen instead)
	};
	std::mutex sessMu;
	std::unique_ptr<Session> session;
	bool asking = false;	// a device's request waiting for the menu's answer (one at a time)
	double stateAt = 0;

	void receiveState(const Session& s, const char* what)
	{
		writeFile(run + "/bshare-receive", std::string(what) + "\t" + s.from + "\t" + std::to_string(s.done) + "\t" +
			std::to_string(s.files.size()) + "\t" + std::to_string(s.got) + "\t" + std::to_string(s.total) + "\t" +
			(!s.where.empty() ? s.where : s.games && s.other ? "both" : s.games ? "games" : "received") + "\n");
	}

	void endSession(const char* what)	// (sessMu held)
	{
		if (!session) return;
		Session& s = *session;
		receiveState(s, what);
		logLine(std::string("received ") + std::to_string(s.done) + " of " + std::to_string(s.files.size()) + " files (" +
			std::to_string(s.got) + " bytes) from " + s.from + " (" + s.ip + "): " + what);
		session.reset();
	}

	// ---- HTTP: a request read, an answer written ------------------------------------------------------------------
	struct Req
	{
		std::string method, path, query, ip;
		std::map<std::string, std::string> head;	// (names in lower case)
		std::string buf;	// what came after the headers already
		int fd = -1;
		std::string param(const std::string& k) const
		{
			std::string q = "&" + query + "&", want = "&" + k + "=";
			size_t a = q.find(want);
			if (a == std::string::npos) return "";
			a += want.size();
			std::string v = q.substr(a, q.find('&', a) - a), o;
			for (size_t i = 0; i < v.size(); i++)
			{
				if (v[i] == '%' && i + 2 < v.size()) { o += (char)strtol(v.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
				else o += v[i] == '+' ? ' ' : v[i];
			}
			return o;
		}
	};

	bool sendAll(int fd, const std::string& s)
	{
		size_t o = 0;
		while (o < s.size())
		{
			ssize_t n = ::send(fd, s.data() + o, s.size() - o, MSG_NOSIGNAL);
			if (n <= 0) { if (n < 0 && errno == EINTR) continue; return false; }
			o += (size_t)n;
		}
		return true;
	}

	void answer(int fd, int code, const std::string& body = "")
	{
		const char* text = code == 200 ? "OK" : code == 204 ? "No Content" : code == 400 ? "Bad Request" : code == 403 ? "Forbidden" :
			code == 404 ? "Not Found" : code == 409 ? "Conflict" : code == 429 ? "Too Many Requests" : "Internal Server Error";
		sendAll(fd, "HTTP/1.1 " + std::to_string(code) + " " + text + "\r\nContent-Type: application/json\r\nContent-Length: " +
			std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
	}

	bool readHead(int fd, Req& r)
	{
		std::string b;
		char c[4096];
		size_t end;
		while ((end = b.find("\r\n\r\n")) == std::string::npos)
		{
			if (b.size() > 16384) return false;
			ssize_t n = recv(fd, c, sizeof c, 0);
			if (n < 0 && errno == EINTR) continue;
			if (n <= 0) return false;
			b.append(c, (size_t)n);
		}
		r.buf = b.substr(end + 4);
		std::istringstream in(b.substr(0, end));
		std::string line, target, ver;
		std::getline(in, line);
		std::istringstream first(line);
		first >> r.method >> target >> ver;
		size_t q = target.find('?');
		r.path = target.substr(0, q);
		if (q != std::string::npos) r.query = target.substr(q + 1);
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			size_t k = line.find(':');
			if (k == std::string::npos) continue;
			std::string name = line.substr(0, k), v = line.substr(k + 1);
			for (auto& ch : name) ch = (char)tolower((unsigned char)ch);
			while (!v.empty() && v[0] == ' ') v.erase(0, 1);
			r.head[name] = v;
		}
		return !r.method.empty();
	}

	// the body, in pieces, to sink (false from it: stop): by Content-Length, or chunked. -1: broken off
	long long readBody(Req& r, const std::function<bool(const char*, size_t)>& sink, long long limit)
	{
		long long got = 0;
		auto more = [&](std::string& b) -> bool
		{
			char c[65536];
			ssize_t n;
			while ((n = recv(r.fd, c, sizeof c, 0)) < 0 && errno == EINTR) {}
			if (n <= 0) return false;
			b.append(c, (size_t)n);
			return true;
		};
		auto te = r.head.find("transfer-encoding");
		if (te != r.head.end() && te->second.find("chunked") != std::string::npos)
		{
			std::string& b = r.buf;
			for (;;)
			{
				size_t e;
				while ((e = b.find("\r\n")) == std::string::npos) if (!more(b)) return -1;
				long long n = strtoll(b.substr(0, e).c_str(), nullptr, 16);
				b.erase(0, e + 2);
				if (n == 0) return got;
				while ((long long)b.size() < n + 2) if (!more(b)) return -1;
				if (got + n > limit || !sink(b.data(), (size_t)n)) return -1;
				got += n;
				b.erase(0, (size_t)n + 2);
			}
		}
		long long want = r.head.count("content-length") ? atoll(r.head["content-length"].c_str()) : 0;
		if (want > limit) return -1;
		if (!r.buf.empty())
		{
			size_t n = (size_t)std::min<long long>(want, (long long)r.buf.size());
			if (n && !sink(r.buf.data(), n)) return -1;
			got += (long long)n;
			r.buf.clear();
		}
		char c[65536];
		while (got < want)
		{
			ssize_t n = recv(r.fd, c, (size_t)std::min<long long>((long long)sizeof c, want - got), 0);
			if (n < 0 && errno == EINTR) continue;
			if (n <= 0) return -1;
			if (!sink(c, (size_t)n)) return -1;
			got += n;
		}
		return got;
	}

	std::string bodyText(Req& r)
	{
		std::string s;
		if (readBody(r, [&](const char* p, size_t n) { s.append(p, n); return true; }, 1 << 20) < 0) return "";
		return s;
	}

	long long freeBytes(const std::string& dir)
	{
		struct statvfs v;
		if (statvfs(dir.c_str(), &v) != 0) return -1;
		return (long long)v.f_bavail * (long long)v.f_frsize;
	}

	// a device asks to send: refused, asked (the menu), or taken at once; its files' places chosen now
	void prepareUpload(Req& r, const Me& m)
	{
		std::string mode = conf(data + "/banana/settings.conf", "bshare_receive", "ask");
		std::string body = bodyText(r);
		JPtr j = parseJson(body);
		const JVal* info = j ? j->get("info") : nullptr;
		const JVal* files = j ? j->get("files") : nullptr;
		if (!j || !files || !files->isObj()) { answer(r.fd, 400, "{\"message\":\"Invalid body\"}"); return; }
		if (info) notePeer(*info, r.ip, m);
		std::string from = info ? info->str("alias", r.ip) : r.ip;
		for (auto& c : from) if (c == '\t' || c == '\n') c = ' ';
		if (mode == "off") { answer(r.fd, 403, "{\"message\":\"Receiving is off\"}"); logLine("refused " + from + " (" + r.ip + "): receiving is off"); return; }
		{
			std::lock_guard<std::mutex> l(sessMu);
			if (asking || (session && now() - session->last < 60)) { answer(r.fd, 409, "{\"message\":\"Blocked by another session\"}"); return; }
			if (session) endSession("stopped");	// (one that went quiet a minute ago)
		}
		auto s = std::make_unique<Session>();
		s->ip = r.ip; s->from = from; s->last = now();
		s->id = randomHex(24);
		std::map<std::string, std::string> games = gameFolders();
		for (auto& f : files->obj)
		{
			if (!f.second || !f.second->isObj()) continue;
			RFile rf;
			rf.name = safeRel(f.second->str("fileName", f.first));
			rf.size = (long long)f.second->num("size", -1);
			rf.token = randomHex(24);
			std::string ext = lowerExt(rf.name);
			auto g = rf.name.find('/') == std::string::npos ? games.find(ext) : games.end();
			if (g != games.end()) { rf.dest = data + "/roms/" + g->second + "/" + rf.name; s->games = true; }
			else { rf.dest = data + "/banana/received/" + rf.name; s->other = true; }
			if (rf.size > 0) s->total += rf.size;
			s->files[f.first] = rf;
		}
		if (s->files.empty()) { answer(r.fd, 204); return; }
		long long space = freeBytes(data);
		if (space >= 0 && s->total + (64LL << 20) > space)
		{
			answer(r.fd, 403, "{\"message\":\"Not enough space\"}");
			logLine("refused " + from + ": " + std::to_string(s->total) + " bytes, the card has " + std::to_string(space) + " free");
			writeFile(run + "/bshare-receive", "nospace\t" + from + "\t0\t" + std::to_string(s->files.size()) + "\t0\t" + std::to_string(s->total) + "\treceived\n");
			return;
		}
		if (mode != "auto")
		{
			// the menu asks (a notification, and Bshare's page): a minute to answer, as LocalSend's own apps wait
			{
				std::lock_guard<std::mutex> l(sessMu);
				if (asking) { answer(r.fd, 409, "{\"message\":\"Blocked by another session\"}"); return; }
				asking = true;
			}
			std::string id = randomHex(12), ans = run + "/bshare-answer";
			unlink(ans.c_str());
			std::string first = s->files.begin()->second.name;
			writeFile(run + "/bshare-request", "id\t" + id + "\nfrom\t" + from + "\nmodel\t" + (info ? info->str("deviceModel") : "") +
				"\nfiles\t" + std::to_string(s->files.size()) + "\nbytes\t" + std::to_string(s->total) + "\nfirst\t" + first + "\n");
			std::string got, where;
			for (int i = 0; i < 600 && got.empty(); i++)
			{
				std::string a = readFile(ans);
				while (!a.empty() && (a.back() == '\n' || a.back() == '\r')) a.pop_back();
				size_t sp = a.find(' ');
				if (sp != std::string::npos && a.compare(sp + 1, id.size(), id) == 0)
				{
					got = a.substr(0, sp);
					if (a.size() > sp + 1 + id.size() + 1) where = a.substr(sp + 1 + id.size() + 1);
				}
				else usleep(100000);
			}
			// a folder chosen (Choose where): on a card, there, and real; then every file goes into it, folders kept
			if (got == "accept" && !where.empty())
			{
				char real[4096];
				struct stat st;
				std::string w = realpath(where.c_str(), real) ? real : "";
				bool ok = !w.empty() && stat(w.c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
					(w == data || w.compare(0, data.size() + 1, data + "/") == 0 || w == "/run/sd2" || w.compare(0, 9, "/run/sd2/") == 0);
				if (ok)
				{
					s->games = false; s->other = true;
					for (auto& f : s->files) f.second.dest = w + "/" + f.second.name;
					s->where = w;
				}
			}
			unlink((run + "/bshare-request").c_str());
			unlink(ans.c_str());
			{ std::lock_guard<std::mutex> l(sessMu); asking = false; }
			if (got != "accept")
			{
				answer(r.fd, 403, "{\"message\":\"Rejected\"}");
				logLine("declined " + std::to_string(s->files.size()) + " files from " + from + " (" + r.ip + ")" + (got.empty() ? ": no answer in a minute" : ""));
				writeFile(run + "/bshare-receive", "declined\t" + from + "\t0\t" + std::to_string(s->files.size()) + "\t0\t" + std::to_string(s->total) + "\treceived\n");
				return;
			}
		}
		std::string out = "{\"sessionId\":\"" + s->id + "\",\"files\":{";
		bool first = true;
		for (auto& f : s->files) { out += std::string(first ? "" : ",") + "\"" + esc(f.first) + "\":\"" + f.second.token + "\""; first = false; }
		out += "}}";
		{
			std::lock_guard<std::mutex> l(sessMu);
			if (session) { answer(r.fd, 409, "{\"message\":\"Blocked by another session\"}"); return; }
			session = std::move(s);
			receiveState(*session, "receiving");
		}
		answer(r.fd, 200, out);
	}

	void upload(Req& r)
	{
		std::string sid = r.param("sessionId"), fid = r.param("fileId"), token = r.param("token");
		std::string tmp, dest, from;
		long long size;
		{
			std::lock_guard<std::mutex> l(sessMu);
			if (!session || session->id != sid) { answer(r.fd, 403, "{\"message\":\"Invalid session\"}"); return; }
			auto f = session->files.find(fid);
			if (f == session->files.end() || f->second.token != token || f->second.done || r.ip != session->ip) { answer(r.fd, 403, "{\"message\":\"Invalid token or IP address\"}"); return; }
			dest = f->second.dest; size = f->second.size; from = session->from;
			session->last = now();
		}
		size_t slash = dest.rfind('/');
		mkdirs(dest.substr(0, slash));
		tmp = dest.substr(0, slash + 1) + "." + dest.substr(slash + 1) + ".part";
		FILE* out = fopen(tmp.c_str(), "wb");
		if (!out) { answer(r.fd, 500, "{\"message\":\"Can't write on the card\"}"); return; }
		long long n = readBody(r, [&](const char* p, size_t len)
		{
			if (fwrite(p, 1, len, out) != len) return false;
			std::lock_guard<std::mutex> l(sessMu);
			if (!session || session->id != sid) return false;	// (cancelled meanwhile)
			session->got += (long long)len;
			session->last = now();
			if (now() - stateAt > 0.5) { stateAt = now(); receiveState(*session, "receiving"); }
			return true;
		}, size >= 0 ? size : (1LL << 40));
		bool ok = fclose(out) == 0 && n >= 0 && (size < 0 || n == size);
		std::lock_guard<std::mutex> l(sessMu);
		if (!ok || !session || session->id != sid)
		{
			unlink(tmp.c_str());
			if (session && session->id == sid) { session->failed++; session->got -= std::max(0LL, n); }
			answer(r.fd, 500, "{\"message\":\"The file didn't arrive whole\"}");
			return;
		}
		std::string final = freeName(dest);
		rename(tmp.c_str(), final.c_str());
		auto f = session->files.find(fid);
		if (f != session->files.end()) f->second.done = true;
		session->done++;
		answer(r.fd, 200);
		if (session->done + session->failed >= (int)session->files.size()) endSession(session->failed ? "partly" : "done");
		else receiveState(*session, "receiving");
	}

	void cancel(Req& r)
	{
		std::lock_guard<std::mutex> l(sessMu);
		if (session && (session->id == r.param("sessionId") || r.param("sessionId").empty()) && r.ip == session->ip) endSession("stopped");
		answer(r.fd, 200);
	}

	void serveOne(int fd, std::string ip, Me m)
	{
		timeval tv = { 30, 0 };	// (a device gone quiet mid-file: the connection given up after half a minute)
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
		Req r;
		r.fd = fd; r.ip = ip;
		if (readHead(fd, r))
		{
			const std::string& p = r.path;
			if (p == "/api/localsend/v2/register" && r.method == "POST")
			{
				JPtr j = parseJson(bodyText(r));
				if (j) notePeer(*j, ip, m);
				answer(fd, 200, infoJson(m, false, false));
			}
			else if (p == "/api/localsend/v2/info" || p == "/api/localsend/v1/info") answer(fd, 200, infoJson(m, false, false));
			else if (p == "/api/localsend/v2/prepare-upload" && r.method == "POST") prepareUpload(r, m);
			else if (p == "/api/localsend/v2/upload" && r.method == "POST") upload(r);
			else if (p == "/api/localsend/v2/cancel" && r.method == "POST") cancel(r);
			else answer(fd, 404, "{\"message\":\"Not found\"}");
		}
		close(fd);
	}

	// ---- discovery: LocalSend's multicast announcements ---------------------------------------------------------
	std::string wlanAddress()
	{
		std::string a;
		ifaddrs* ifs = nullptr;
		if (getifaddrs(&ifs) != 0) return a;
		for (ifaddrs* i = ifs; i && a.empty(); i = i->ifa_next)
			if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && i->ifa_name && strcmp(i->ifa_name, "lo") && (i->ifa_flags & IFF_UP))
			{
				char b[INET_ADDRSTRLEN] = "";
				inet_ntop(AF_INET, &((sockaddr_in*)i->ifa_addr)->sin_addr, b, sizeof b);
				a = b;
			}
		freeifaddrs(ifs);
		return a;
	}

	void announce(int udp, const Me& m, bool announcing)
	{
		sockaddr_in to = {};
		to.sin_family = AF_INET;
		to.sin_port = htons((uint16_t)port);
		inet_pton(AF_INET, MCAST, &to.sin_addr);
		std::string s = infoJson(m, announcing, true);
		sendto(udp, s.data(), s.size(), 0, (sockaddr*)&to, sizeof to);
	}

	volatile sig_atomic_t stopping = 0;
	void onStop(int) { stopping = 1; }

	int serve()
	{
		Me m = me();
		signal(SIGTERM, onStop);
		signal(SIGINT, onStop);
		signal(SIGPIPE, SIG_IGN);
		int tcp = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
		int one = 1;
		setsockopt(tcp, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		sockaddr_in a = {};
		a.sin_family = AF_INET;
		a.sin_port = htons((uint16_t)port);
		a.sin_addr.s_addr = htonl(INADDR_ANY);
		if (bind(tcp, (sockaddr*)&a, sizeof a) != 0 || listen(tcp, 16) != 0)
		{
			fprintf(stderr, "bshare: port %d: %s\n", port, strerror(errno));
			return 1;
		}
		int udp = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
		setsockopt(udp, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		setsockopt(udp, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
		if (bind(udp, (sockaddr*)&a, sizeof a) != 0) fprintf(stderr, "bshare: multicast port: %s\n", strerror(errno));
		unsigned char ttl = 1;	// (the home network only)
		setsockopt(udp, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
		writeFile(run + "/bshare.pid", std::to_string(getpid()) + "\n");
		writeFile(run + "/bshare-peers", "");
		std::string joinedOn;	// the address the group was joined on: a new one (Wi-Fi back) joins and announces again
		double lookAt = 0;
		logLine("on, as " + m.alias + " (port " + std::to_string(port) + ")");
		while (!stopping)
		{
			if (now() >= lookAt)
			{
				lookAt = now() + 10;
				std::string addr = wlanAddress();
				if (addr != joinedOn)
				{
					if (!addr.empty())
					{
						ip_mreq mr = {};
						inet_pton(AF_INET, MCAST, &mr.imr_multiaddr);
						inet_pton(AF_INET, addr.c_str(), &mr.imr_interface);
						setsockopt(udp, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mr, sizeof mr);
						if (setsockopt(udp, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mr, sizeof mr) == 0 || errno == EADDRINUSE)
						{
							in_addr ifa;
							inet_pton(AF_INET, addr.c_str(), &ifa);
							setsockopt(udp, IPPROTO_IP, IP_MULTICAST_IF, &ifa, sizeof ifa);
							announce(udp, m, true);
						}
					}
					joinedOn = addr;
				}
				std::lock_guard<std::mutex> l(sessMu);
				if (session && now() - session->last > 60) endSession("stopped");	// (the device went away mid-way)
			}
			pollfd pf[2] = { { tcp, POLLIN, 0 }, { udp, POLLIN, 0 } };
			if (poll(pf, 2, 10000) <= 0) continue;
			if (pf[0].revents & POLLIN)
			{
				sockaddr_in from = {};
				socklen_t fl = sizeof from;
				int c = accept4(tcp, (sockaddr*)&from, &fl, SOCK_CLOEXEC);
				if (c >= 0)
				{
					char ip[INET_ADDRSTRLEN] = "";
					inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
					std::thread(serveOne, c, std::string(ip), m).detach();
				}
			}
			if (pf[1].revents & POLLIN)
			{
				char b[4096];
				sockaddr_in from = {};
				socklen_t fl = sizeof from;
				ssize_t n = recvfrom(udp, b, sizeof b - 1, 0, (sockaddr*)&from, &fl);
				if (n > 0)
				{
					b[n] = 0;
					JPtr j = parseJson(std::string(b, (size_t)n));
					char ip[INET_ADDRSTRLEN] = "";
					inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
					if (j && j->isObj() && j->str("fingerprint") != m.fingerprint)
					{
						notePeer(*j, ip, m);
						if (j->flag("announce", false)) announce(udp, m, false);	// (an app looking: here we are)
					}
				}
			}
		}
		{
			std::lock_guard<std::mutex> l(sessMu);
			if (session) endSession("stopped");
		}
		unlink((run + "/bshare.pid").c_str());
		unlink((run + "/bshare-peers").c_str());
		logLine("off");
		return 0;
	}

	// ---- sending ---------------------------------------------------------------------------------------------------
	struct SFile { std::string path, rel, id, token; long long size = 0; };

	void collect(const std::string& path, const std::string& rel, std::vector<SFile>& out)
	{
		struct stat st;
		if (stat(path.c_str(), &st) != 0) return;
		if (S_ISDIR(st.st_mode))
		{
			DIR* d = opendir(path.c_str());
			if (!d) return;
			std::vector<std::string> names;
			while (dirent* e = readdir(d)) if (e->d_name[0] != '.') names.push_back(e->d_name);
			closedir(d);
			std::sort(names.begin(), names.end());
			for (auto& n : names) collect(path + "/" + n, rel + "/" + n, out);
			return;
		}
		if (!S_ISREG(st.st_mode)) return;
		SFile f;
		f.path = path; f.rel = rel; f.size = st.st_size;
		f.id = randomHex(16);
		out.push_back(f);
	}

	size_t toString(char* p, size_t s, size_t n, void* u) { ((std::string*)u)->append(p, s * n); return s * n; }

	struct Sending { std::string alias; int i = 0, n = 0; long long done = 0, total = 0, base = 0; double at = 0; };
	Sending sending;
	volatile sig_atomic_t sendStop = 0;
	void onSendStop(int) { sendStop = 1; }

	void sendState(const char* what)
	{
		writeFile(run + "/bshare-send", std::string(what) + "\t" + sending.alias + "\t" + std::to_string(sending.i) + "\t" +
			std::to_string(sending.n) + "\t" + std::to_string(sending.done) + "\t" + std::to_string(sending.total) + "\n");
	}

	int progress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t ulnow)
	{
		if (sendStop) return 1;
		sending.done = sending.base + (long long)ulnow;
		if (now() - sending.at > 0.5) { sending.at = now(); sendState("sending"); }
		return 0;
	}

	CURL* curlFor(const std::string& url, std::string& reply)
	{
		CURL* c = curl_easy_init();
		curl_easy_setopt(c, CURLOPT_URL, url.c_str());
		curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);	// (LocalSend's certificates are its apps' own, self-signed)
		curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
		curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L);
		curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, toString);
		curl_easy_setopt(c, CURLOPT_WRITEDATA, &reply);
		return c;
	}

	int send(const std::string& fp, const std::vector<std::string>& paths)
	{
		signal(SIGTERM, onSendStop);
		signal(SIGINT, onSendStop);
		signal(SIGPIPE, SIG_IGN);
		Me m = me();
		Peer to;
		{
			std::ifstream f(run + "/bshare-peers");
			std::string l;
			while (std::getline(f, l))
			{
				std::vector<std::string> c;
				std::stringstream s(l);
				std::string x;
				while (std::getline(s, x, '\t')) c.push_back(x);
				if (c.size() >= 7 && c[0] == fp) { to.fingerprint = c[0]; to.ip = c[1]; to.port = atoi(c[2].c_str()); to.protocol = c[3]; to.alias = c[4]; }
			}
		}
		sending.alias = to.alias.empty() ? fp : to.alias;
		if (to.ip.empty()) { sendState("error"); logLine("send: no device " + fp); return 1; }
		std::vector<SFile> files;
		for (auto& p : paths)
		{
			std::string base = p.substr(p.rfind('/') + 1);
			collect(p, base, files);
		}
		if (files.empty()) { sendState("error"); return 1; }
		sending.n = (int)files.size();
		for (auto& f : files) sending.total += f.size;
		std::string root = to.protocol + "://" + to.ip + ":" + std::to_string(to.port) + "/api/localsend/v2/";
		curl_global_init(CURL_GLOBAL_DEFAULT);
		// the other side asks its user first: up to two minutes
		std::string body = "{\"info\":" + infoJson(m, false, false) + ",\"files\":{";
		for (size_t i = 0; i < files.size(); i++)
			body += std::string(i ? "," : "") + "\"" + files[i].id + "\":{\"id\":\"" + files[i].id + "\",\"fileName\":\"" + esc(files[i].rel) +
				"\",\"size\":" + std::to_string(files[i].size) + ",\"fileType\":\"application/octet-stream\"}";
		body += "}}";
		sendState("waiting");
		std::string reply;
		CURL* c = curlFor(root + "prepare-upload", reply);
		curl_slist* h = curl_slist_append(nullptr, "Content-Type: application/json");
		curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
		curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
		curl_easy_setopt(c, CURLOPT_TIMEOUT, 130L);
		curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, (curl_xferinfo_callback)[](void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int { return sendStop ? 1 : 0; });
		CURLcode rc = curl_easy_perform(c);
		long code = 0;
		curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
		curl_easy_cleanup(c);
		curl_slist_free_all(h);
		if (sendStop) { sendState("stopped"); return 1; }
		if (rc != CURLE_OK || (code != 200 && code != 204))
		{
			const char* what = code == 403 ? "declined" : code == 409 ? "busy" : "error";
			sendState(what);
			logLine("send " + std::to_string(files.size()) + " files to " + sending.alias + " (" + to.ip + "): " + what +
				(rc != CURLE_OK ? std::string(" (") + curl_easy_strerror(rc) + ")" : " (" + std::to_string(code) + ")"));
			return 1;
		}
		std::string session;
		if (code == 200)
		{
			JPtr j = parseJson(reply);
			session = j ? j->str("sessionId") : "";
			const JVal* tok = j ? j->get("files") : nullptr;
			for (auto& f : files) if (tok) f.token = tok->str(f.id);
		}
		int sent = 0;
		for (size_t i = 0; i < files.size() && !sendStop; i++)
		{
			SFile& f = files[i];
			sending.i = (int)i + 1;
			if (f.token.empty()) { sending.base += f.size; continue; }	// (the other side already has it)
			FILE* in = fopen(f.path.c_str(), "rb");
			if (!in) continue;
			std::string r2;
			char* sid = curl_easy_escape(nullptr, session.c_str(), 0);
			std::string url = root + "upload?sessionId=" + sid + "&fileId=" + f.id + "&token=" + f.token;
			curl_free(sid);
			CURL* u = curlFor(url, r2);
			curl_slist* uh = curl_slist_append(nullptr, "Content-Type: application/octet-stream");
			curl_easy_setopt(u, CURLOPT_HTTPHEADER, uh);
			curl_easy_setopt(u, CURLOPT_POST, 1L);
			curl_easy_setopt(u, CURLOPT_READDATA, in);
			curl_easy_setopt(u, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)f.size);
			curl_easy_setopt(u, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(u, CURLOPT_XFERINFOFUNCTION, progress);
			curl_easy_setopt(u, CURLOPT_LOW_SPEED_LIMIT, 1L);	// (stalled half a minute: given up)
			curl_easy_setopt(u, CURLOPT_LOW_SPEED_TIME, 30L);
			CURLcode urc = curl_easy_perform(u);
			long ucode = 0;
			curl_easy_getinfo(u, CURLINFO_RESPONSE_CODE, &ucode);
			curl_easy_cleanup(u);
			curl_slist_free_all(uh);
			fclose(in);
			sending.base += f.size;
			sending.done = sending.base;
			if (urc == CURLE_OK && ucode == 200) sent++;
			else if (!sendStop) logLine("send: " + f.rel + " to " + sending.alias + ": " + (urc != CURLE_OK ? curl_easy_strerror(urc) : std::to_string(ucode)));
			sendState("sending");
		}
		if (sendStop && !session.empty())
		{
			std::string r3;
			CURL* x = curlFor(root + "cancel?sessionId=" + session, r3);
			curl_easy_setopt(x, CURLOPT_POSTFIELDS, "");
			curl_easy_setopt(x, CURLOPT_TIMEOUT, 5L);
			curl_easy_perform(x);
			curl_easy_cleanup(x);
		}
		sending.i = sent;
		const char* what = sendStop ? "stopped" : sent == (int)files.size() || code == 204 ? "done" : "partly";
		sendState(what);
		logLine("sent " + std::to_string(sent) + " of " + std::to_string(files.size()) + " files to " + sending.alias + " (" + to.ip + "): " + what);
		return sent == (int)files.size() ? 0 : 1;
	}
}

int main(int argc, char** argv)
{
	if (const char* e = getenv("BSHARE_RUN")) run = e;		// (the test's folders and port)
	if (const char* e = getenv("BSHARE_DATA")) data = e;
	if (const char* e = getenv("BSHARE_PORT")) port = atoi(e);
	std::string cmd = argc > 1 ? argv[1] : "";
	if (cmd == "serve") return serve();
	if (cmd == "send" && argc > 3) return send(argv[2], std::vector<std::string>(argv + 3, argv + argc));
	fprintf(stderr, "usage: banana-bshare serve | send <fingerprint> <path>...\n");
	return 2;
}
