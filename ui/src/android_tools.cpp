// banana-ui on Android: where the core's Linux-shaped calls leave it, taken and pointed at Android (android_platform.h
// says why this layer and not edits at each call). Linked with ld's --wrap (android/launcher/CMakeLists.txt), so the
// core's own objects, libc++ and FreeType call these instead of libc's, and nothing in the shared sources changes:
//
// - Paths. The fixed places of BananaOS's own image are mapped into the app's tree (<files>/root, made from the APK's
//   assets by the app): /etc/banana, /usr/share/banana, /run/banana, and the sysfs files the menu reads (the
//   backlights, the battery, Wi-Fi's link, the model), which the app keeps up to date there (Platform.java). A write
//   of a backlight's brightness (Settings' Brightness rows) is applied through the Platform when the file closes.
// - Tools. popen() and system() of a command naming one of the shell tools (/usr/libexec/banana/..., banana-perf) run
//   it on the Platform, through a small reading of the shell's syntax the core uses (quotes, ;, &&, ||, &, redirections
//   to /dev/null or a file). A child the core forked to exec one (spawn(), capture()) can't use the app's Java side
//   (no VM after fork): it hands the command line, its stdout and a pipe for the exit status to the app's process over
//   a socket and waits, and a thread here runs it.
// Everything else goes on to libc as it was.
#ifdef __ANDROID__
#include "android_host.h"
#include "android_platform.h"
#include <android/log.h>
#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <string>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C"
{
	FILE* __real_fopen(const char*, const char*);
	int __real_fclose(FILE*);
	int __real_open(const char*, int, ...);
	DIR* __real_opendir(const char*);
	int __real_access(const char*, int);
	int __real_stat(const char*, struct stat*);
	int __real_lstat(const char*, struct stat*);
	int __real_unlink(const char*);
	int __real_rename(const char*, const char*);
	int __real_mkdir(const char*, mode_t);
	int __real_rmdir(const char*);
	int __real_mkfifo(const char*, mode_t);
	int __real_chmod(const char*, mode_t);
	int __real_truncate(const char*, off_t);
	int __real_inotify_add_watch(int, const char*, uint32_t);
	int __real_utimensat(int, const char*, const struct timespec[2], int);
	char* __real_realpath(const char*, char*);
	FILE* __real_popen(const char*, const char*);
	int __real_pclose(FILE*);
	int __real_system(const char*);
	int __real_execv(const char*, char* const[]);
}

namespace banana
{
	namespace android
	{
		static std::string gRoot;
		const std::string& root() { return gRoot; }
		void setRoot(const std::string& r) { gRoot = r; }
	}

	namespace
	{
		// ---- paths ----------------------------------------------------------------------------------------------
		struct Map { const char* from; const char* to; };
		const Map MAPS[] = {
			{ "/etc/banana", "/etc/banana" },
			{ "/usr/share/banana", "/share/banana" },
			{ "/run/banana", "/run/banana" },
			{ "/sys/class/backlight", "/sys/class/backlight" },
			{ "/sys/class/power_supply", "/sys/class/power_supply" },
			{ "/sys/class/net/wlan0", "/sys/class/net/wlan0" },
			{ "/proc/device-tree/model", "/proc/device-tree/model" },
		};

		const char* mapPath(const char* p, char* buf)
		{
			const std::string& r = android::root();
			if (!p || p[0] != '/' || r.empty()) return p;
			for (const Map& m : MAPS)
			{
				size_t n = strlen(m.from);
				if (strncmp(p, m.from, n) == 0 && (p[n] == 0 || p[n] == '/'))
				{
					snprintf(buf, PATH_MAX, "%s%s%s", r.c_str(), m.to, p + n);
					return buf;
				}
			}
			return p;
		}

		// the backlights' brightness files open for writing: screen and the mapped file, applied at fclose
		std::mutex blM;
		std::map<FILE*, std::pair<int, std::string>> blOpen;

		int screenOfBacklight(const char* p)
		{
			// /sys/class/backlight/<name>/brightness, the names BANANA_BACKLIGHT_TOP/_BOTTOM give (the app: top, bottom)
			const char* pre = "/sys/class/backlight/";
			size_t n = strlen(pre);
			if (!p || strncmp(p, pre, n) != 0) return -1;
			const char* slash = strchr(p + n, '/');
			if (!slash || strcmp(slash, "/brightness") != 0) return -1;
			std::string name(p + n, slash - (p + n));
			const char* top = getenv("BANANA_BACKLIGHT_TOP");
			const char* bottom = getenv("BANANA_BACKLIGHT_BOTTOM");
			if (top && name == top) return 0;
			if (bottom && name == bottom) return 1;
			return -1;
		}

		int readInt(const std::string& path)
		{
			FILE* f = __real_fopen(path.c_str(), "r");
			if (!f) return -1;
			int v = -1;
			if (fscanf(f, "%d", &v) != 1) v = -1;
			__real_fclose(f);
			return v;
		}

		// ---- the shell's syntax, as much of it as the core uses -------------------------------------------------
		struct Tok { std::string s; bool op = false; };

		std::vector<Tok> tokenize(const std::string& c)
		{
			std::vector<Tok> t;
			std::string cur;
			bool have = false;
			auto flush = [&] { if (have) t.push_back({ cur, false }); cur.clear(); have = false; };
			for (size_t i = 0; i < c.size();)
			{
				char ch = c[i];
				if (ch == '\'')
				{
					size_t e = c.find('\'', i + 1);
					if (e == std::string::npos) e = c.size();
					cur += c.substr(i + 1, e - i - 1); have = true; i = e + 1;
				}
				else if (ch == '"')
				{
					have = true;
					for (i++; i < c.size() && c[i] != '"'; i++)
					{
						if (c[i] == '\\' && i + 1 < c.size() && strchr("\"\\$`", c[i + 1])) i++;
						cur += c[i];
					}
					i++;
				}
				else if (ch == '\\' && i + 1 < c.size()) { cur += c[i + 1]; have = true; i += 2; }
				else if (ch == ' ' || ch == '\t' || ch == '\n') { flush(); i++; }
				else if (ch == ';' || ch == '(' || ch == ')') { flush(); t.push_back({ std::string(1, ch), true }); i++; }
				else if (ch == '&' || ch == '|')
				{
					// (a redirection's &: 2>&1, 9>&-)
					if (ch == '&' && !cur.empty() && cur.back() == '>') { cur += ch; i++; continue; }
					flush();
					if (i + 1 < c.size() && c[i + 1] == ch) { t.push_back({ std::string(2, ch), true }); i += 2; }
					else { t.push_back({ std::string(1, ch), true }); i++; }
				}
				else if ((ch == '>' || ch == '<') && (!have || cur.find_first_not_of("0123456789") == std::string::npos))
				{
					// a redirection: [n]>, [n]>>, <, its target attached or the next word
					cur += ch; have = true; i++;
					if (ch == '>' && i < c.size() && c[i] == '>') { cur += '>'; i++; }
					if (i < c.size() && c[i] == '&') { cur += '&'; i++; while (i < c.size() && (isdigit((unsigned char)c[i]) || c[i] == '-')) cur += c[i++]; }
					t.push_back({ cur, true });
					cur.clear(); have = false;
				}
				else { cur += ch; have = true; i++; }
			}
			flush();
			return t;
		}

		struct Seg
		{
			std::vector<std::string> words;
			std::string out;		// "": captured; "/dev/null": dropped; else a file
			bool append = false, background = false, discardAll = false;
			std::string next;		// the operator after it: ";", "&&", "||", ""
		};

		std::vector<Seg> segments(const std::vector<Tok>& t)
		{
			std::vector<Seg> r(1);
			for (size_t i = 0; i < t.size(); i++)
			{
				const Tok& k = t[i];
				Seg& s = r.back();
				if (!k.op) { s.words.push_back(k.s); continue; }
				if (k.s == "(" || k.s == ")") continue;
				if (k.s == ";" || k.s == "&&" || k.s == "||" || k.s == "|" || k.s == "&")
				{
					// (a group's redirections and & after its ")": the command before it)
					Seg& to = s.words.empty() && r.size() > 1 ? r[r.size() - 2] : s;
					if (k.s == "&") to.background = true;
					to.next = k.s == "&" ? ";" : k.s == "|" ? ";" : k.s;
					if (!s.words.empty()) r.emplace_back();
					continue;
				}
				// a redirection: what stdout becomes (stderr and stdin don't matter here)
				size_t gt = k.s.find('>');
				std::string target;
				if (k.s.back() == '>' || k.s.back() == '<') { if (i + 1 < t.size() && !t[i + 1].op) target = t[++i].s; }
				if (gt == std::string::npos) continue;	// (<: stdin)
				const std::string fdPart = k.s.substr(0, gt);
				if (!fdPart.empty() && fdPart != "1") continue;	// (2>, 9>&-)
				if (k.s.find('&') != std::string::npos) continue;	// (>&2: as good as dropped)
				Seg& to = s.words.empty() && r.size() > 1 ? r[r.size() - 2] : s;
				to.out = target.empty() ? k.s.substr(gt + (k.s.compare(gt, 2, ">>") == 0 ? 2 : 1)) : target;
				to.append = k.s.compare(gt, 2, ">>") == 0;
			}
			while (!r.empty() && r.back().words.empty()) r.pop_back();
			return r;
		}

		bool mentionsTool(const char* cmd)
		{
			return cmd && (strstr(cmd, "/usr/libexec/banana/") || strstr(cmd, "banana-perf"));
		}

		std::string requote(const std::vector<std::string>& w)
		{
			std::string r;
			for (auto& s : w)
			{
				r += r.empty() ? "'" : " '";
				for (char c : s) { if (c == '\'') r += "'\\''"; else r += c; }
				r += "'";
			}
			return r;
		}

		int runSeg(const Seg& s, std::string& out)
		{
			size_t at = 0;
			while (at < s.words.size() && platform::toolName(s.words[at]).empty()) at++;
			std::string o;
			int st;
			if (at < s.words.size())
			{
				std::vector<std::string> argv(s.words.begin() + at, s.words.end());
				st = platform::runTool(argv, "", o);
			}
			else
			{
				// not one of ours (rm, mkdir, cp...): the system's shell, as it was
				std::string c = requote(s.words) + (s.out.empty() ? "" : " >/dev/null") + " 2>/dev/null";
				int w = __real_system(c.c_str());
				st = WIFEXITED(w) ? WEXITSTATUS(w) : 1;
			}
			if (s.out.empty()) out += o;
			else if (s.out != "/dev/null")
				if (FILE* f = fopen(s.out.c_str(), s.append ? "a" : "w")) { fwrite(o.data(), 1, o.size(), f); fclose(f); }
			return st;
		}

		// the command line run: its output (what wasn't redirected) and its status
		int shell(const std::string& cmd, std::string& out)
		{
			std::vector<Seg> segs = segments(tokenize(cmd));
			int st = 0;
			std::string prev = ";";
			for (const Seg& s : segs)
			{
				bool run = prev == ";" || (prev == "&&" && st == 0) || (prev == "||" && st != 0);
				prev = s.next.empty() ? ";" : s.next;
				if (!run) continue;
				if (s.background)
				{
					Seg copy = s;
					std::thread([copy] { std::string o; runSeg(copy, o); }).detach();
					st = 0;
					continue;
				}
				st = runSeg(s, out);
			}
			__android_log_print(ANDROID_LOG_DEBUG, "banana-ui", "tool: %s -> %d", cmd.c_str(), st);
			return st;
		}

		// ---- popen's streams over a tool's output ---------------------------------------------------------------
		struct Mem { std::string data; size_t at = 0; };
		std::mutex popM;
		std::map<FILE*, int> popStatus;

		int memRead(void* c, char* buf, int n)
		{
			Mem* m = (Mem*)c;
			size_t k = std::min((size_t)n, m->data.size() - m->at);
			memcpy(buf, m->data.data() + m->at, k);
			m->at += k;
			return (int)k;
		}
		int memClose(void* c) { delete (Mem*)c; return 0; }

		// ---- a forked child's tool, run in the app's process ---------------------------------------------------
		int fwdChild = -1, fwdParent = -1;

		[[noreturn]] void forward(const std::string& cmd)
		{
			int ack[2];
			if (fwdChild < 0 || pipe(ack) != 0) _exit(127);
			int fds[2] = { 1, ack[1] };
			char ctl[CMSG_SPACE(sizeof fds)] = {};
			iovec iov = { (void*)cmd.data(), cmd.size() };
			msghdr mh = {};
			mh.msg_iov = &iov; mh.msg_iovlen = 1;
			mh.msg_control = ctl; mh.msg_controllen = sizeof ctl;
			cmsghdr* cm = CMSG_FIRSTHDR(&mh);
			cm->cmsg_level = SOL_SOCKET; cm->cmsg_type = SCM_RIGHTS; cm->cmsg_len = CMSG_LEN(sizeof fds);
			memcpy(CMSG_DATA(cm), fds, sizeof fds);
			if (sendmsg(fwdChild, &mh, MSG_NOSIGNAL) < 0) _exit(127);
			close(ack[1]);
			unsigned char st = 127;
			while (read(ack[0], &st, 1) < 0 && errno == EINTR) {}
			_exit(st);
		}

		void serve()
		{
			for (;;)
			{
				char buf[65536];
				int fds[2] = { -1, -1 };
				char ctl[CMSG_SPACE(sizeof fds)] = {};
				iovec iov = { buf, sizeof buf - 1 };
				msghdr mh = {};
				mh.msg_iov = &iov; mh.msg_iovlen = 1;
				mh.msg_control = ctl; mh.msg_controllen = sizeof ctl;
				ssize_t n = recvmsg(fwdParent, &mh, MSG_CMSG_CLOEXEC);
				if (n < 0) { if (errno == EINTR) continue; return; }
				for (cmsghdr* cm = CMSG_FIRSTHDR(&mh); cm; cm = CMSG_NXTHDR(&mh, cm))
					if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS) memcpy(fds, CMSG_DATA(cm), std::min(sizeof fds, (size_t)(cm->cmsg_len - CMSG_LEN(0))));
				std::string cmd(buf, (size_t)n);
				std::thread([cmd, fds] {
					std::string out;
					int st = shell(cmd, out);
					if (fds[0] >= 0) { for (size_t at = 0; at < out.size();) { ssize_t w = write(fds[0], out.data() + at, out.size() - at); if (w <= 0) break; at += (size_t)w; } close(fds[0]); }
					unsigned char b = (unsigned char)st;
					if (fds[1] >= 0) { if (write(fds[1], &b, 1) < 0) {} close(fds[1]); }
				}).detach();
			}
		}
	}

	namespace android
	{
		void toolsInit()
		{
			int sv[2];
			if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv) != 0) return;
			fwdParent = sv[0]; fwdChild = sv[1];
			std::thread(serve).detach();
		}
		int runShell(const std::string& cmd, std::string& out) { return shell(cmd, out); }
	}
}

using namespace banana;

extern "C"
{
	FILE* __wrap_fopen(const char* p, const char* m)
	{
		char b[PATH_MAX];
		FILE* f = __real_fopen(mapPath(p, b), m);
		int s = f && m && (strchr(m, 'w') || strchr(m, 'a')) ? screenOfBacklight(p) : -1;
		if (s >= 0) { std::lock_guard<std::mutex> l(blM); blOpen[f] = { s, mapPath(p, b) }; }
		return f;
	}

	int __wrap_fclose(FILE* f)
	{
		std::pair<int, std::string> bl(-1, "");
		{
			std::lock_guard<std::mutex> l(blM);
			auto it = blOpen.find(f);
			if (it != blOpen.end()) { bl = it->second; blOpen.erase(it); }
		}
		int r = __real_fclose(f);
		if (bl.first >= 0)
		{
			// a brightness written (Settings' Brightness rows): on the screen, in thousandths of max_brightness
			std::string dir = bl.second.substr(0, bl.second.rfind('/'));
			int v = readInt(bl.second), max = readInt(dir + "/max_brightness");
			if (v >= 0 && max > 0) platform::get().setBrightness(bl.first, (int)((long)v * 1000 / max));
		}
		return r;
	}

	int __wrap_open(const char* p, int flags, ...)
	{
		mode_t mode = 0;
		if (flags & (O_CREAT | O_TMPFILE)) { va_list ap; va_start(ap, flags); mode = (mode_t)va_arg(ap, int); va_end(ap); }
		char b[PATH_MAX];
		return __real_open(mapPath(p, b), flags, mode);
	}

	DIR* __wrap_opendir(const char* p) { char b[PATH_MAX]; return __real_opendir(mapPath(p, b)); }
	int __wrap_access(const char* p, int m) { char b[PATH_MAX]; return __real_access(mapPath(p, b), m); }
	int __wrap_stat(const char* p, struct stat* st) { char b[PATH_MAX]; return __real_stat(mapPath(p, b), st); }
	int __wrap_lstat(const char* p, struct stat* st) { char b[PATH_MAX]; return __real_lstat(mapPath(p, b), st); }
	int __wrap_unlink(const char* p) { char b[PATH_MAX]; return __real_unlink(mapPath(p, b)); }
	int __wrap_rename(const char* a, const char* z) { char b1[PATH_MAX], b2[PATH_MAX]; return __real_rename(mapPath(a, b1), mapPath(z, b2)); }
	int __wrap_mkdir(const char* p, mode_t m) { char b[PATH_MAX]; return __real_mkdir(mapPath(p, b), m); }
	int __wrap_rmdir(const char* p) { char b[PATH_MAX]; return __real_rmdir(mapPath(p, b)); }
	int __wrap_mkfifo(const char* p, mode_t m) { char b[PATH_MAX]; return __real_mkfifo(mapPath(p, b), m); }
	int __wrap_chmod(const char* p, mode_t m) { char b[PATH_MAX]; return __real_chmod(mapPath(p, b), m); }
	int __wrap_truncate(const char* p, off_t n) { char b[PATH_MAX]; return __real_truncate(mapPath(p, b), n); }
	int __wrap_inotify_add_watch(int fd, const char* p, uint32_t m) { char b[PATH_MAX]; return __real_inotify_add_watch(fd, mapPath(p, b), m); }
	int __wrap_utimensat(int d, const char* p, const struct timespec t[2], int f) { char b[PATH_MAX]; return __real_utimensat(d, p ? mapPath(p, b) : p, t, f); }
	char* __wrap_realpath(const char* p, char* out) { char b[PATH_MAX]; return __real_realpath(mapPath(p, b), out); }

	FILE* __wrap_popen(const char* cmd, const char* mode)
	{
		if (!mentionsTool(cmd) || !mode || mode[0] != 'r') return __real_popen(cmd, mode);
		Mem* m = new Mem;
		int st = shell(cmd, m->data);
		FILE* f = funopen(m, memRead, nullptr, nullptr, memClose);
		if (!f) { delete m; return nullptr; }
		std::lock_guard<std::mutex> l(popM);
		popStatus[f] = st;
		return f;
	}

	int __wrap_pclose(FILE* f)
	{
		int st = -1;
		{
			std::lock_guard<std::mutex> l(popM);
			auto it = popStatus.find(f);
			if (it == popStatus.end()) return __real_pclose(f);
			st = it->second;
			popStatus.erase(it);
		}
		fclose(f);
		return (st & 0xff) << 8;	// (as waitpid says it: WEXITSTATUS)
	}

	int __wrap_system(const char* cmd)
	{
		if (!mentionsTool(cmd)) return __real_system(cmd);
		std::string out;
		return (shell(cmd, out) & 0xff) << 8;
	}

	// (in a child the core forked: a tool's command line goes to the app's process, see forward)
	int __wrap_execv(const char* path, char* const argv[])
	{
		if (path && !platform::toolName(path).empty())
		{
			std::vector<std::string> w;
			for (int i = 0; argv && argv[i]; i++) w.push_back(i == 0 ? path : argv[i]);
			forward(requote(w));
		}
		if (path && argv && argv[0] && argv[1] && argv[2] && !strcmp(argv[1], "-c") && strstr(path, "sh") && mentionsTool(argv[2]))
			forward(argv[2]);
		return __real_execv(path, argv);
	}

	int __wrap_execl(const char* path, const char* arg, ...)
	{
		std::vector<char*> a;
		a.push_back(const_cast<char*>(arg));
		va_list ap;
		va_start(ap, arg);
		while (char* p = va_arg(ap, char*)) a.push_back(p);
		va_end(ap);
		a.push_back(nullptr);
		return __wrap_execv(path, a.data());
	}
}
#endif
