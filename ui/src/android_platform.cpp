// banana-ui on Android: the Platform (android_platform.h) on the launcher's Java side and banana-hwd, and the shell
// tools' command lines mapped onto it.
#ifdef __ANDROID__
#include "android_platform.h"
#include "android_host.h"
#include <android/log.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <poll.h>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace banana
{
	namespace android
	{
		// banana-hwd's socket (android/system/hwd/PROTOCOL.md, banana-hwd.c): one request line, one answer line, "OK
		// [what]" or "ERR <why>"; answer: what follows the OK. BANANA_HWD names another socket (a test's)
		bool hwd(const std::string& request, std::string* answer)
		{
			const char* e = getenv("BANANA_HWD");
			const std::string path = e && *e ? e : "/dev/socket/banana-hwd";
			int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
			if (fd < 0) return false;
			sockaddr_un a = {};
			a.sun_family = AF_UNIX;
			snprintf(a.sun_path, sizeof a.sun_path, "%s", path.c_str());
			if (connect(fd, (sockaddr*)&a, sizeof a) != 0) { close(fd); return false; }
			std::string line = request + "\n";
			if (send(fd, line.data(), line.size(), MSG_NOSIGNAL) != (ssize_t)line.size()) { close(fd); return false; }
			std::string got;
			while (got.find('\n') == std::string::npos)
			{
				pollfd p = { fd, POLLIN, 0 };
				if (poll(&p, 1, 1500) <= 0) break;
				char buf[1024];
				ssize_t n = recv(fd, buf, sizeof buf, 0);
				if (n <= 0) break;
				got.append(buf, (size_t)n);
			}
			close(fd);
			got = got.substr(0, got.find('\n'));
			if (got.compare(0, 2, "OK") != 0)
			{
				if (!got.empty()) fprintf(stderr, "banana-ui: hwd: %s: %s\n", request.c_str(), got.c_str());
				return false;
			}
			if (answer) *answer = got.size() > 3 ? got.substr(3) : "";
			return true;
		}
	}

	namespace platform
	{
		namespace
		{
			int toInt(const std::string& s, int def = -1)
			{
				char* end = nullptr;
				long v = strtol(s.c_str(), &end, 10);
				return end == s.c_str() ? def : (int)v;
			}

			std::vector<std::string> with(const char* first, const std::vector<std::string>& rest)
			{
				std::vector<std::string> a{ first };
				a.insert(a.end(), rest.begin(), rest.end());
				return a;
			}

			class AndroidPlatform : public Platform
			{
			public:
				int volume() override { return toInt(android::javaCall({ "volume", "get" })); }
				bool setVolume(int pc) override { int st = 1; android::javaCall({ "volume", "set", std::to_string(pc) }, &st); return st == 0; }
				// the daemon's BACKLIGHT top|bottom: "<value> <max>" in the panel's own steps
				static bool backlight(int s, long& v, long& max)
				{
					std::string a;
					if (!android::hwd(std::string("BACKLIGHT ") + (s ? "bottom" : "top"), &a)) return false;
					return sscanf(a.c_str(), "%ld %ld", &v, &max) == 2 && max > 0;
				}
				int brightness(int s) override
				{
					int st = 1;
					int v = toInt(android::javaCall({ "brightness", "get", std::to_string(s) }, &st));
					if (st == 0 && v >= 0) return v;
					long raw, max;
					return backlight(s, raw, max) ? (int)(raw * 1000 / max) : -1;
				}
				bool setBrightness(int s, int pm) override
				{
					// the Android way first (PROTOCOL.md: the framework owns brightness and would put its own back): each
					// display's through DisplayManager, else the top's Settings value; the daemon's raw override where
					// that doesn't reach (the bottom panel on a build that doesn't let the launcher)
					int st = 1;
					android::javaCall({ "brightness", "set", std::to_string(s), std::to_string(pm) }, &st);
					if (st == 0) return true;
					long v, max;
					return backlight(s, v, max) &&
						android::hwd(std::string("BACKLIGHT ") + (s ? "bottom " : "top ") + std::to_string(std::max(1L, (max * pm + 500) / 1000)));
				}
				int wifi(const std::vector<std::string>& args, std::string& out) override
				{
					int st = 1;
					out = android::javaCall(with("wifi", args), &st);
					return st;
				}
				int bluetooth(const std::vector<std::string>& args, std::string& out) override
				{
					int st = 1;
					out = android::javaCall(with("bluetooth", args), &st);
					return st;
				}
				// the handheld tool's verbs as the daemon's requests: the fan, the stick lights and the GPU are its (root, sysfs)
				int handheld(const std::vector<std::string>& args, std::string& out) override
				{
					out.clear();
					const std::string v = args.empty() ? "" : args[0], a1 = args.size() > 1 ? args[1] : "", a2 = args.size() > 2 ? args[2] : "";
					if (v == "caps")
					{
						// "fan fan_trips leds perf gpuboost backlight_top ... home": the tool's words a line (fan, leds, gpuboost)
						std::string caps;
						if (!android::hwd("CAPS", &caps)) return 1;
						std::istringstream in(caps);
						for (std::string w; in >> w;) if (w == "fan" || w == "leds" || w == "gpuboost") out += w + "\n";
						return 0;
					}
					bool ok = false;
					if (v == "fan" && !a1.empty()) ok = android::hwd("FAN MODE " + a1);
					else if (v == "leds" && !a1.empty())
					{
						ok = a1 == "off" || a1 == "battery" || a1 == "system" ? android::hwd("LEDS MODE " + a1) : android::hwd("LEDS COLOR " + a1);
						if (ok && !a2.empty()) ok = android::hwd("LEDS LEVEL " + a2);
					}
					else if (v == "leds-effect" && !a1.empty()) ok = android::hwd("LEDS EFFECT " + a1);
					else if (v == "leds-system" && !a1.empty()) ok = android::hwd("LEDS SYSTEM " + a1);
					else if (v == "gpu" && !a1.empty()) ok = android::hwd(std::string("GPUBOOST ") + (a1 == "on" ? "ON" : "OFF"));
					else if (v == "sleep") ok = android::hwd("SUSPEND");
					else if (v == "boot") ok = android::hwd("RESUME");
					else return 2;	// (poweroff-after: the system's own sleep, not the daemon's)
					return ok ? 0 : 1;
				}
				std::string sysstat() override
				{
					// the daemon's STATS: sysstat's names, on one line
					std::string a;
					if (android::hwd("STATS", &a)) { for (char& c : a) if (c == ' ') c = '\n'; return a + "\n"; }
					// without the daemon: what an app may read itself (the memory; the battery from the app's mirror)
					std::ostringstream o;
					std::ifstream mi("/proc/meminfo");
					long total = 0, avail = 0;
					for (std::string k; mi >> k;)
					{
						long v; std::string unit;
						mi >> v >> unit;
						if (k == "MemTotal:") total = v;
						else if (k == "MemAvailable:") avail = v;
					}
					if (total > 0)
						o << "mem_used_mb=" << (total - avail) / 1024 << "\nmem_total_mb=" << total / 1024 << "\nmem_pct=" << (total - avail) * 100 / total << "\n";
					std::ifstream cap("/sys/class/power_supply/battery/capacity"), sta("/sys/class/power_supply/battery/status");
					std::string c, s;
					if (std::getline(cap, c) && !c.empty()) o << "battery=" << c << "\n";
					if (std::getline(sta, s) && !s.empty()) o << "power_state=" << s << "\n";
					return o.str();
				}
				bool performance(const std::string& mode) override { return android::hwd("PERF SET " + mode); }
				bool setTimeZone(const std::string& tz) override { int st = 1; android::javaCall({ "timezone", tz }, &st); return st == 0; }
			};
		}

		Platform& get()
		{
			static AndroidPlatform p;
			return p;
		}

		// the tools this layer stands in for; the rest of /usr/libexec/banana is missing on Android (status 127)
		static const std::set<std::string> TOOLS = {
			"volume", "backlight", "handheld", "sysstat", "wifi", "bluetooth", "banana-perf", "timezone",
			// (TODO(android), each answered 127 for now: see android/launcher/README.md, "Stubbed")
			"update", "steam", "pc-games", "rocknix-options", "nds-reset", "ra-reset", "ssh", "share", "sd2", "boot-log",
			"saves-flat", "data-layout", "data-link", "tune", "panel-tune", "one-panel", "usb-debug", "sound-server",
		};

		std::string toolName(const std::string& path)
		{
			std::string base = path.substr(path.rfind('/') + 1);
			if (base == "banana-perf" && (path == base || path.compare(0, 9, "/usr/bin/") == 0 || path.compare(0, 20, "/usr/libexec/banana/") == 0)) return base;
			if (path.compare(0, 20, "/usr/libexec/banana/") != 0) return "";
			return base;	// (any of them: the ones not in TOOLS answer 127 below, as a missing program would)
		}

		static void todo(const std::string& tool, const std::vector<std::string>& argv)
		{
			static std::mutex m;
			static std::set<std::string> said;
			std::lock_guard<std::mutex> l(m);
			if (said.insert(tool).second)
				__android_log_print(ANDROID_LOG_WARN, "banana-ui", "TODO(android): the %s tool isn't on Android yet (asked: %s)", tool.c_str(),
					argv.size() > 1 ? argv[1].c_str() : "");
		}

		int runTool(const std::vector<std::string>& argv, const std::string&, std::string& out)
		{
			if (argv.empty()) return 127;
			const std::string t = toolName(argv[0]);
			const std::vector<std::string> args(argv.begin() + 1, argv.end());
			const std::string verb = args.empty() ? "" : args[0];
			Platform& p = get();
			if (t == "volume")
			{
				if (verb == "get") { int v = p.volume(); if (v < 0) return 1; out = std::to_string(v) + "\n"; return 0; }
				if (verb == "set" && args.size() > 1) return p.setVolume(std::max(0, std::min(100, toInt(args[1], 0)))) ? 0 : 1;
				if ((verb == "up" || verb == "down") && args.size() > 1)
				{
					int v = p.volume();
					if (v < 0) return 1;
					v = std::max(0, std::min(100, v + (verb == "up" ? 1 : -1) * toInt(args[1], 5)));
					if (!p.setVolume(v)) return 1;
					out = std::to_string(v) + "\n";
					return 0;
				}
				return 2;
			}
			if (t == "backlight")
			{
				if (verb == "names") { out = "top bottom\n"; return 0; }
				if (verb == "get" && args.size() > 1) { int v = p.brightness(toInt(args[1], 0)); if (v < 0) return 1; out = std::to_string(v) + "\n"; return 0; }
				if (verb == "set" && args.size() > 2) return p.setBrightness(toInt(args[1], 0), toInt(args[2], 500)) ? 0 : 1;
				if (verb == "restore" || verb == "sync")
				{
					// both as banana/backlight keeps them ("<top> <bottom>", thousandths)
					const char* d = getenv("BANANA_DATA");
					std::ifstream f(std::string(d ? d : "") + "/backlight");
					int a = -1, b = -1;
					if (f >> a >> b) { if (a > 0) p.setBrightness(0, a); if (b > 0) p.setBrightness(1, b); }
					return 0;
				}
				return 2;
			}
			if (t == "handheld") return p.handheld(args, out);
			if (t == "sysstat") { out = p.sysstat(); return 0; }
			if (t == "banana-perf") return p.performance(verb.empty() ? "balanced" : verb) ? 0 : 1;
			if (t == "wifi") return p.wifi(args, out);
			if (t == "bluetooth") return p.bluetooth(args, out);
			if (t == "timezone") return p.setTimeZone(verb) ? 0 : 1;
			todo(t, argv);
			return 127;
		}
	}
}
#endif
