#include "settings.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <net/if.h>
#include <signal.h>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

using rnds::Color;

namespace banana
{
	// ---- key = value files ------------------------------------------------------------------------------------------
	static std::string trim(const std::string& s)
	{
		size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
		return a == std::string::npos ? "" : s.substr(a, b - a + 1);
	}

	std::string getConfValue(const std::string& path, const std::string& key, const std::string& def)
	{
		std::ifstream f(path);
		std::string ln, r = def;
		while (std::getline(f, ln))
		{
			if (ln.empty() || ln[0] == '#') continue;
			size_t eq = ln.find('=');
			if (eq != std::string::npos && trim(ln.substr(0, eq)) == key) r = trim(ln.substr(eq + 1));
		}
		// shell files quote their values
		if (r.size() >= 2 && (r[0] == '"' || r[0] == '\'') && r.back() == r[0]) r = r.substr(1, r.size() - 2);
		return r;
	}

	void setConfValue(const std::string& path, const std::string& key, const std::string& value, const char* sep)
	{
		std::ifstream f(path);
		std::string ln, out;
		bool done = false;
		while (std::getline(f, ln))
		{
			size_t eq = ln.find('=');
			bool mine = !ln.empty() && ln[0] != '#' && eq != std::string::npos && trim(ln.substr(0, eq)) == key;
			if (mine && done) continue;	// a second line for the key would win over ours: drop it
			out += mine ? key + sep + value : ln;
			out += "\n";
			done |= mine;
		}
		if (!done) out += key + sep + value + "\n";
		std::string tmp = path + ".new";
		FILE* w = fopen(tmp.c_str(), "w");
		if (!w) return;
		fwrite(out.data(), 1, out.size(), w);
		fflush(w);
		fsync(fileno(w));
		fclose(w);
		rename(tmp.c_str(), path.c_str());
	}

	static std::string readLine(const std::string& path)
	{
		std::ifstream f(path);
		std::string s;
		std::getline(f, s);
		return trim(s);
	}

	// a program in the background, not waited for (Wi-Fi coming up takes seconds)
	static void spawn(const char* path, const char* arg, const char* arg2 = nullptr)
	{
		pid_t p = fork();
		if (p == 0)
		{
			if (fork() == 0) { setsid(); execl(path, path, arg, arg2, (char*)nullptr); _exit(127); }
			_exit(0);
		}
		if (p > 0) waitpid(p, nullptr, 0);
	}

	// bananad re-reads its settings on SIGHUP
	static void poke(const char* comm)
	{
		DIR* d = opendir("/proc");
		if (!d) return;
		while (dirent* e = readdir(d))
		{
			if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
			if (readLine(std::string("/proc/") + e->d_name + "/comm") == comm) kill(atoi(e->d_name), SIGHUP);
		}
		closedir(d);
	}

	// ---- palette: ROCKNIXDS Pixel's (RndsUI.cpp darkPal / lightPal), the colours this screen uses ----------------------
	struct SPal
	{
		Color ink, muted, line, edge, screenBg, screenFrame, screenBand, ditherA, ditherB;
		Color panelBand, panelBody, panelLow, bubbleBand, bubbleBody, bubbleLow, slotOn, kbdBg, kbdInk, blackish, shade45;
		Color accent;
	};
	static SPal pal(bool light)
	{
		SPal p;
		if (!light)
		{
			p.ink = Color::hex(0xf1ede2); p.muted = Color::hex(0xa39d8e); p.line = Color::hex(0x8a8374); p.edge = Color::hex(0xe8e2d3);
			p.screenBg = Color::hex(0x17161c); p.screenFrame = Color::hex(0x0b0a0d); p.screenBand = Color::hex(0x2e2b33);
			p.ditherA = Color(1, 1, 1, 0.14f); p.ditherB = Color(1, 1, 1, 0.07f);
			p.panelBand = Color::hex(0x3a3540); p.panelBody = Color::hex(0x17161c); p.panelLow = Color::hex(0x0b0a0d);
			p.bubbleBand = Color::hex(0x38333b); p.bubbleBody = Color::hex(0x191820); p.bubbleLow = Color::hex(0x0f0e13);
			p.slotOn = Color::hex(0xffffff); p.kbdBg = Color::hex(0xf1ede2); p.kbdInk = Color::hex(0x17161c);
			p.blackish = Color::hex(0x0b0a0d); p.shade45 = Color(0, 0, 0, 0.45f);
		}
		else
		{
			Color sh = Color::hex(0x3a2e14);
			p.ink = Color::hex(0x2a2620); p.muted = Color::hex(0x7a7262); p.line = Color::hex(0xa69d8c); p.edge = Color::hex(0x3a342c);
			p.screenBg = Color::hex(0xf3efe6); p.screenFrame = Color::hex(0xd4ccbd); p.screenBand = Color::hex(0xfdfcf9);
			p.ditherA = Color(sh.r, sh.g, sh.b, 0.07f); p.ditherB = Color(sh.r, sh.g, sh.b, 0.035f);
			p.panelBand = Color::hex(0xffffff); p.panelBody = Color::hex(0xf6f2ea); p.panelLow = Color::hex(0xd8d0c0);
			p.bubbleBand = Color::hex(0xffffff); p.bubbleBody = Color::hex(0xfaf7f0); p.bubbleLow = Color::hex(0xddd5c6);
			p.slotOn = Color::hex(0xf0b429); p.kbdBg = Color::hex(0x3a342c); p.kbdInk = Color::hex(0xf2f6fa);
			p.blackish = Color(sh.r, sh.g, sh.b, 0.3f); p.shade45 = Color(sh.r, sh.g, sh.b, 0.25f);
		}
		p.accent = light ? Color::hex(0xc07a00) : Color::hex(0xffd23f);	// the bananas' yellow for values being changed
		return p;
	}

	// ---- the settings ------------------------------------------------------------------------------------------------
	Settings::Settings(rnds::Backend* b, const std::string& a, const std::string& d, float scale, const std::string& variant)
		: be(b), assets(a), data(d), s(scale), light(variant == "light")
	{
		R = rnds::Font::load(assets + "/fonts/PixelifySans-Regular.ttf");
		M = rnds::Font::load(assets + "/fonts/PixelifySans-Medium.ttf");
		const char* tool = getenv("BANANA_WIFI");	// the test's stand-in
		wifiTool = tool ? tool : "/usr/libexec/banana/wifi";
		wifiConf = data + "/wifi.conf";
		build();
	}

	void Settings::setVariant(const std::string& v)
	{
		bool l = v == "light";
		if (l != light) { light = l; cache.clear(); }
	}

	// time zones as POSIX TZ rules (glibc reads them from TZ with no zoneinfo files): the offset and the summer time
	// rules as they stand, one entry per rule with the places that follow it
	static const struct { const char* name; const char* tz; } ZONES[] = {
		{ "UTC", "UTC0" },
		{ "London, Dublin, Lisbon", "GMT0BST,M3.5.0/1,M10.5.0" },
		{ "Lagos, Algiers", "WAT-1" },
		{ "Amsterdam, Berlin, Paris, Rome", "CET-1CEST,M3.5.0,M10.5.0/3" },
		{ "Johannesburg", "SAST-2" },
		{ "Athens, Helsinki, Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
		{ "Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24" },
		{ "Istanbul, Riyadh, Nairobi", "<+03>-3" },
		{ "Moscow", "MSK-3" },
		{ "Dubai", "<+04>-4" },
		{ "Karachi", "PKT-5" },
		{ "India", "IST-5:30" },
		{ "Bangkok, Jakarta", "<+07>-7" },
		{ "Beijing, Singapore, Perth", "CST-8" },
		{ "Tokyo, Seoul", "JST-9" },
		{ "Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3" },
		{ "Brisbane", "AEST-10" },
		{ "Sydney, Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
		{ "Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3" },
		{ "Honolulu", "HST10" },
		{ "Anchorage", "AKST9AKDT,M3.2.0,M11.1.0" },
		{ "Los Angeles, Vancouver", "PST8PDT,M3.2.0,M11.1.0" },
		{ "Phoenix", "MST7" },
		{ "Denver, Calgary", "MST7MDT,M3.2.0,M11.1.0" },
		{ "Mexico City", "CST6" },
		{ "Chicago, Winnipeg", "CST6CDT,M3.2.0,M11.1.0" },
		{ "New York, Toronto", "EST5EDT,M3.2.0,M11.1.0" },
		{ "Halifax", "AST4ADT,M3.2.0,M11.1.0" },
		{ "Santiago", "<-04>4<-03>,M9.1.6/24,M4.1.6/24" },
		{ "Sao Paulo, Buenos Aires", "<-03>3" },
	};

	void Settings::build()
	{
		std::string menu = data + "/settings.conf", power = data + "/bananad.conf", sd = data + "/superdrastic.conf";
		std::string dcfg = "/data/drastic/config/drastic.cfg";
		auto add = [&](const char* section, const char* icon, const char* label, const char* help, const std::string& file,
			const char* key, std::vector<std::string> names, std::vector<std::string> values, Item::Kind kind = Item::CHOICE)
		{
			Item it;
			it.section = section; it.icon = icon; it.label = label; it.help = help; it.file = file; it.key = key;
			it.names = names; it.values = values; it.kind = kind;
			items.push_back(it);
		};
		const char* D = "Display & sound";
		add(D, "picture", "Theme", "ROCKNIXDS Pixel's two palettes: dark, or light for daylight.", menu, "theme", { "Dark", "Light" }, { "dark", "light" });
		add(D, "picture", "Brightness", "Both panels' backlights. F + volume changes it anywhere, in games too.", "", "",
			{ "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" }, { "100", "200", "300", "400", "500", "600", "700", "800", "900", "1000" }, Item::BRIGHTNESS);
		add(D, "picture", "Motion", "The top screen's bob and shine. Off holds it still and saves a little battery.", menu, "motion", { "On", "Off" }, { "on", "off" });
		add(D, "music", "Volume", "The speakers or the headphones, whichever is playing. The volume keys change it anywhere.", "", "",
			{ "0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" }, { "0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100" }, Item::VOLUME);
		add(D, "music", "Menu sounds", "The menu's clicks, and the cartridge going into the console.", menu, "sounds", { "On", "Off" }, { "on", "off" });
		add(D, "picture", "Battery", "The battery on the top screen: its icon, or its icon and how full it is.", menu, "battery", { "Icon", "Icon and %" }, { "icon", "percent" });
		add(D, "picture", "Clock", "How the time on the top screen is written.", menu, "clock", { "24-hour", "12-hour" }, { "24h", "12h" });
		add(D, "picture", "Boot animation", "The bananas and the cartridge while Banana starts. Off goes straight to the menu once it is ready.", menu, "splash", { "On", "Off" }, { "on", "off" });
		const char* P = "Sleep & power";
		add(P, "moon", "Sleep when idle", "In the menu, after this long with no button pressed. Never while a game is running.", power, "autosleep_minutes",
			{ "Never", "5 minutes", "10 minutes", "15 minutes", "30 minutes" }, { "0", "5", "10", "15", "30" });
		add(P, "moon", "Dim when idle", "In the menu, the panels go down to a third after this long with no button pressed, and back at the first press. Never while a game is running.", power, "dim_minutes",
			{ "Never", "1 minute", "2 minutes", "5 minutes" }, { "0", "1", "2", "5" });
		add(P, "moon", "Closing the lid", "Sleep with the lid: the game pauses, opening the lid carries on where you were.", power, "lid_sleep", { "Sleeps", "Does nothing" }, { "yes", "no" });
		add(P, "moon", "Waking up", "Fast: the handheld's parts wake up together. Careful: one after the other, slower but the safest. Banana switches to Careful by itself if a sleep ever fails.",
			power, "fast_resume", { "Fast", "Careful" }, { "yes", "no" });
		add(P, "moon", "LEDs", "Green, red when the battery is low, blinking red when nearly empty, amber while charging.", power, "led_brightness",
			{ "Bright", "Dim", "Off" }, { "255", "40", "0" });
		const char* N = "Nintendo DS";
		add(N, "nds", "Emulator", "SuperDrastic: DraStic straight onto both panels, with its own renderer and governor. BananaDS: Banana's own emulator, once it is installed (until then DraStic runs either way).",
			menu, "nds_emulator", { "SuperDrastic", "BananaDS" }, { "superdrastic", "bananads" });
		add(N, "nds", "Renderer", "Gengis Engine draws the same picture as DraStic with about 12% less CPU work a frame.", sd, "DSFLIP_RAST", { "Gengis Engine", "DraStic" }, { "1", "0" });
		add(N, "nds", "Shader", "Crisp: exact pixel-area scaling. Grid: the DS's LCD grid. FSR: smooth edges, the heaviest. None costs no GPU work.", sd, "DSFLIP_SHADER",
			{ "None", "Crisp", "Crisp + DS colours", "LCD grid", "LCD grid + colours", "FSR" }, { "none", "ds-crisp", "ds-crisp-color", "ds-grid", "ds-grid-color", "ds-fsr" });
		add(N, "nds", "3D resolution", "2x draws the DS's 3D at twice its size on these panels, at full speed with SuperDrastic.", dcfg, "hires_3d", { "2x", "1x" }, { "1", "0" });
		const char* O = "Other systems";
		add(O, "gamepad", "Screen", "Which panel the systems other than the DS use (RetroArch). The other panel is switched off.",
			"/data/retroarch/retroarch.cfg", "video_monitor_index", { "Top", "Bottom" }, { "2", "1" });
		const char* W = "Network";
		add(W, "phone", "Wi-Fi", "", menu, "wifi", { "On", "Off" }, { "on", "off" });
		add(W, "phone", "Network", "The networks around: A on one joins it, with a password typed on the keyboard here (or by touch).", "", "network", {}, {}, Item::PAGE);
		add(W, "phone", "File sharing", "", menu, "share", { "Off", "On" }, { "off", "on" });
		const char* S = "System";
		{
			std::vector<std::string> names, tzs;
			for (auto& z : ZONES) { names.push_back(z.name); tzs.push_back(z.tz); }
			add(S, "gear", "Time zone", "The clock's time zone, summer time included, for the menu and the games (a DS game's clock too). The time itself comes from Wi-Fi or the handheld's own clock.",
				menu, "timezone", names, tzs);
		}
		add(S, "gear", "Update", "A new version of Banana from a package on the card, written to the other slot and run from the next restart; this one stays as it is until then.", "", "update", {}, {}, Item::PAGE);
		add(S, "gear", "Previous version", "Restarts into the other slot: the version that ran before the last update. Press A twice.", "", "switch", { "" }, { "" }, Item::ACTION);
		add(S, "gear", "SSH", "", menu, "ssh", { "Off", "On" }, { "off", "on" });
		add(S, "gear", "First-time setup", "The steps from the first boot again: Wi-Fi, the time zone, the RetroAchievements and ScreenScraper accounts.", "", "setup", {}, {}, Item::PAGE);
		add(S, "gear", "About", "", "", "", {}, {}, Item::INFO);
		add(S, "gear", "Restart", "Restarts the handheld. Press A twice.", "", "reboot", { "" }, { "" }, Item::ACTION);
		add(S, "gear", "Shut down", "Turns the handheld off. Press A twice. (Or hold the power button.)", "", "poweroff", { "" }, { "" }, Item::ACTION);
	}

	void Settings::load(Item& it)
	{
		if (it.kind == Item::BRIGHTNESS)
		{
			// the first backlight's level, in tenths
			int pm = -1;
			if (DIR* d = opendir("/sys/class/backlight"))
			{
				while (dirent* e = readdir(d))
				{
					if (e->d_name[0] == '.') continue;
					std::string b = std::string("/sys/class/backlight/") + e->d_name;
					int max = atoi(readLine(b + "/max_brightness").c_str()), cur = atoi(readLine(b + "/brightness").c_str());
					if (max > 0) { pm = cur * 1000 / max; break; }
				}
				closedir(d);
			}
			it.at = pm < 0 ? 9 : std::min(9, std::max(0, (pm + 50) / 100 - 1));
			return;
		}
		if (it.kind == Item::VOLUME)
		{
			// the playing card's control (bananad writes which), its level from amixer: "[57%]"
			int pct = -1;
			std::string mixer = readLine("/run/banana/mixer");
			if (!mixer.empty())
			{
				FILE* p = popen(("amixer -c " + mixer.substr(0, mixer.find(' ')) + " sget '" + mixer.substr(mixer.find(' ') + 1) + "' 2>/dev/null").c_str(), "r");
				if (p)
				{
					char ln[256];
					while (pct < 0 && fgets(ln, sizeof ln, p))
						if (const char* b = strchr(ln, '[')) { int v; if (sscanf(b, "[%d%%]", &v) == 1) pct = v; }
					pclose(p);
				}
			}
			it.at = pct < 0 ? 5 : std::min(10, std::max(0, (pct + 5) / 10));
			return;
		}
		if (it.kind != Item::CHOICE) return;
		std::string def = it.values[0];
		// the system's defaults where the data partition has none yet
		if (it.file == data + "/bananad.conf") def = getConfValue("/etc/banana/bananad.conf", it.key, def);
		if (it.file == data + "/settings.conf") def = getConfValue("/etc/banana/settings.conf", it.key, def);
		if (it.key == "led_brightness") def = getConfValue("/etc/banana/bananad.conf", it.key, "128");
		if (it.key == "video_monitor_index") def = getConfValue("/etc/banana/retroarch.cfg", it.key, "2");
		std::string v = getConfValue(it.file, it.key, def);
		it.at = 0;
		for (size_t i = 0; i < it.values.size(); i++)
			if (it.values[i] == v) it.at = (int)i;
		// a value between the choices (the LEDs at 128): the nearest one
		if (it.key == "led_brightness")
		{
			int n = atoi(v.c_str());
			it.at = n == 0 ? 2 : (n < 100 ? 1 : 0);
		}
	}

	void Settings::refreshInfo()
	{
		version = readLine("/etc/banana/version");
		slot = readLine("/run/banana/slot");
		board = readLine("/etc/banana/board");
		std::string model;
		{
			std::ifstream f("/proc/device-tree/model");
			std::getline(f, model, '\0');
		}
		int battery = -1;
		if (DIR* d = opendir("/sys/class/power_supply"))
		{
			while (dirent* e = readdir(d))
			{
				std::string b = std::string("/sys/class/power_supply/") + e->d_name;
				if (readLine(b + "/type") == "Battery") { battery = atoi(readLine(b + "/capacity").c_str()); break; }
			}
			closedir(d);
		}
		struct statvfs vf;
		char space[64] = "";
		if (statvfs("/data", &vf) == 0)
			snprintf(space, sizeof space, "%.1f GB free of %.1f GB", vf.f_bavail * (double)vf.f_frsize / 1e9, vf.f_blocks * (double)vf.f_frsize / 1e9);
		// Wi-Fi: the address, if there is one
		wifiInfo = "Off";
		ip.clear();
		std::string state = readLine("/sys/class/net/wlan0/operstate");
		if (!state.empty())
		{
			wifiInfo = state == "up" ? "Connected" : "Not connected";
			int fd = socket(AF_INET, SOCK_DGRAM, 0);
			ifreq ifr = {};
			strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ - 1);
			if (fd >= 0 && ioctl(fd, SIOCGIFADDR, &ifr) == 0)
			{
				ip = inet_ntoa(((sockaddr_in*)&ifr.ifr_addr)->sin_addr);
				wifiInfo = "Connected: " + ip;
			}
			if (fd >= 0) ::close(fd);
		}
		std::ostringstream a;
		a << "Banana " << (version.empty() ? "dev" : version) << (slot.empty() ? "" : " (slot " + slot + ")") << " on the " << (model.empty() ? "handheld" : model) << ". ";
		if (battery >= 0) a << "Battery " << battery << "%. ";
		if (space[0]) a << "Card: " << space << ".";
		about = a.str();
	}

	void Settings::open()
	{
		for (auto& it : items) load(it);
		refreshInfo();
		armed = -1;
		page = LIST;
		message.clear();
		if (wifiOn() && !statusJob.running()) statusJob.start(wifiTool + " status", sync);	// the network's name on its row
		mOpen = true;
	}

	void Settings::save(const Item& it)
	{
		const std::string& v = it.values[it.at];
		if (it.kind == Item::BRIGHTNESS)
		{
			int pm = atoi(v.c_str());
			if (DIR* d = opendir("/sys/class/backlight"))
			{
				while (dirent* e = readdir(d))
				{
					if (e->d_name[0] == '.') continue;
					std::string b = std::string("/sys/class/backlight/") + e->d_name;
					int max = atoi(readLine(b + "/max_brightness").c_str());
					std::ofstream(b + "/brightness") << std::max(1, max * pm / 1000) << "\n";
				}
				closedir(d);
			}
			std::ofstream(data + "/backlight") << pm << "\n";	// bananad restores it at boot
			return;
		}
		if (it.kind == Item::VOLUME)
		{
			std::string mixer = readLine("/run/banana/mixer");
			if (mixer.empty()) return;
			std::string card = mixer.substr(0, mixer.find(' ')), control = mixer.substr(mixer.find(' ') + 1);
			if (system(("amixer -q -c " + card + " sset '" + control + "' " + v + "% 2>/dev/null").c_str()) != 0) {}
			poke("bananad");	// it keeps the level across reboots
			return;
		}
		if (it.file.find("superdrastic.conf") != std::string::npos) setConfValue(it.file, it.key, v, "=");
		else if (it.file.find("retroarch.cfg") != std::string::npos)
		{
			// RetroArch's settings: from Banana's defaults the first time
			if (access(it.file.c_str(), F_OK) != 0)
			{
				if (system("mkdir -p /data/retroarch && cp -n /etc/banana/retroarch.cfg /data/retroarch/ 2>/dev/null") != 0) {}
			}
			setConfValue(it.file, it.key, "\"" + v + "\"");	// RetroArch quotes its values
		}
		else if (it.file.find("drastic.cfg") != std::string::npos)
		{
			// DraStic's own settings live with DraStic on the data partition, made on its first run: make them now
			if (access(it.file.c_str(), F_OK) != 0)
			{
				if (system("mkdir -p /data/drastic/config && cp -n /usr/share/drastic/config/drastic.cfg /data/drastic/config/ 2>/dev/null") != 0) {}
			}
			setConfValue(it.file, it.key, v);
		}
		else setConfValue(it.file, it.key, v);
		if (it.file == data + "/bananad.conf") poke("bananad");
		if (it.key == "timezone") { setenv("TZ", v.c_str(), 1); tzset(); poke("bananad"); }	// bananad's logs too
		if (it.key == "wifi") spawn("/usr/libexec/banana/wifi", v == "on" ? "up" : "down");
		if (it.key == "ssh") spawn("/usr/libexec/banana/ssh", v == "on" ? "up" : "down");
		if (it.key == "share") spawn("/usr/libexec/banana/share", v == "on" ? "up" : "down");
	}

	Settings::Result Settings::change(int dir)
	{
		Item& it = items[sel];
		if (it.kind == Item::INFO) return NONE;
		if (it.kind == Item::PAGE)
		{
			if (dir <= 0) return NONE;
			if (it.key == "update") openUpdates(); else if (it.key == "setup") openSetup(); else openNetworks();
			return MOVED;
		}
		if (it.kind == Item::ACTION)
		{
			if (dir <= 0) { armed = -1; return MOVED; }
			if (armed != sel) { armed = sel; return MOVED; }
			if (it.key == "switch")
			{
				// the other slot made the default, then a restart; the script says why not when it can't
				armed = -1;
				FILE* p = popen("/usr/libexec/banana/update switch 2>&1", "r");
				std::string out;
				if (p) { char ln[256]; while (fgets(ln, sizeof ln, p)) out += ln; if (pclose(p) == 0) return REBOOT; }
				message = out.empty() ? "The other slot holds no system." : out;
				return MOVED;
			}
			return it.key == "reboot" ? REBOOT : POWEROFF;
		}
		int n = (int)it.values.size();
		int at = it.at + dir;
		if (it.kind == Item::BRIGHTNESS || it.kind == Item::VOLUME) at = std::min(n - 1, std::max(0, at));	// a slider stops at its ends
		else at = (at + n) % n;
		if (at == it.at) return NONE;
		it.at = at;
		save(it);
		if (it.key == "theme") { setVariant(it.values[at]); return THEME; }
		return CHANGED;
	}

	Settings::Result Settings::press(Key k)
	{
		if (page == NETWORKS) return pressNetworks(k);
		if (page == KEYBOARD) return pressKeyboard(k);
		if (page == UPDATES) return pressUpdates(k);
		if (page == SETUP) return pressSetup(k);
		switch (k)
		{
		case Key::UP: case Key::DOWN:
		{
			int n = (int)items.size();
			sel = (sel + (k == Key::UP ? -1 : 1) + n) % n;
			armed = -1;
			if (sel < top) top = sel;
			if (sel >= top + ROWS) top = sel - ROWS + 1;
			if (items[sel].kind == Item::INFO) refreshInfo();
			return MOVED;
		}
		case Key::L: case Key::R:
		{
			// the first item of the previous / next section
			int n = (int)items.size(), i = sel;
			std::string cur = items[sel].section;
			if (k == Key::R) { while (i < n && items[i].section == cur) i++; if (i >= n) i = 0; }
			else
			{
				while (i > 0 && items[i - 1].section == cur) i--;
				if (i == 0) i = n - 1; else i--;
				std::string prev = items[i].section;
				while (i > 0 && items[i - 1].section == prev) i--;
			}
			sel = i; armed = -1;
			top = std::max(0, std::min(sel, (int)items.size() - ROWS));
			return MOVED;
		}
		case Key::LEFT: return change(-1);
		case Key::RIGHT: case Key::A: return change(1);
		case Key::B: case Key::START: mOpen = false; return CLOSED;
		default: return NONE;
		}
	}

	Settings::Result Settings::touch(float x, float y, bool down)
	{
		float cx = x / s, cy = y / s;
		if (down) { downX = cx; downY = cy; downOnRow = true; return NONE; }
		if (!downOnRow) return NONE;
		downOnRow = false;
		if (page == NETWORKS) return touchNetworks(cx, cy);
		if (page == KEYBOARD) return touchKeyboard(cx, cy);
		if (page == SETUP)
		{
			if (downY >= 418 && downY <= 466) return pressSetup(downX < 320 ? Key::B : Key::A);
			for (int r = 0; r < 5; r++)
			{
				float ry = 82 + r * 64;
				if (downY < ry || downY > ry + 56) continue;
				if (r != wsel) { wsel = r; return MOVED; }
				return pressSetup(Key::A);
			}
			return NONE;
		}
		if (page == UPDATES)
		{
			if (downY >= 418 && downY <= 466) return pressUpdates(downX < 320 ? Key::B : Key::A);
			for (int r = 0; r < ROWS && r < (int)pkgs.size(); r++)
			{
				float ry = 82 + r * 64;
				if (downY < ry || downY > ry + 56) continue;
				if (r != usel) { usel = r; armed = -1; return MOVED; }
				return pressUpdates(Key::A);
			}
			return NONE;
		}
		// the chips: B on the left, A on the right
		if (downY >= 418 && downY <= 466)
		{
			if (downX < 320) { mOpen = false; return CLOSED; }
			return change(1);
		}
		for (int r = 0; r < ROWS && top + r < (int)items.size(); r++)
		{
			float ry = 82 + r * 64;
			if (downY < ry || downY > ry + 56) continue;
			int i = top + r;
			if (i != sel) { sel = i; armed = -1; return MOVED; }
			// the selected row: its value's left half steps back, the rest forward
			return change(downX < 400 && items[i].kind != Item::ACTION ? -1 : 1);
		}
		// a swipe up or down scrolls
		if (std::fabs(cy - downY) > 40)
		{
			int n = (int)items.size();
			top = std::max(0, std::min(n - ROWS, top + (cy < downY ? ROWS : -ROWS)));
			sel = std::max(top, std::min(sel, top + ROWS - 1));
			return MOVED;
		}
		return NONE;
	}

	// ---- drawing ----------------------------------------------------------------------------------------------------
	void Settings::box(const std::string& kind, bool on, float x, float y, float w, float h)
	{
		SPal p = pal(light);
		int dw = (int)std::lround(X(w)), dh = (int)std::lround(X(h));
		std::string key = "box:" + kind + (on ? "1" : "0") + ":" + std::to_string(dw) + "x" + std::to_string(dh);
		auto it = cache.find(key);
		if (it == cache.end())
		{
			rnds::BoxStyle st;
			auto inset = [](float dx, float dy, float sp, Color c) { return rnds::Shadow{ dx, dy, sp, c, true }; };
			auto outer = [](float dx, float dy, Color c) { return rnds::Shadow{ dx, dy, 0, c, false }; };
			float b3 = s == 1 ? 3 : std::max(1.0f, std::floor(3 * s));
			if (kind == "screen")
			{
				st.radius = X(2); st.bg = p.screenBg; st.dither = true;
				st.shadows = { inset(0, 0, X(4), p.screenFrame), inset(0, X(8), 0, p.screenBand) };
			}
			else if (kind == "hero" || kind == "slot")
			{
				bool hero = kind == "hero";
				st.radius = X(hero ? 18 : 14); st.border = b3; st.borderColor = on ? p.slotOn : p.edge; st.bg = p.panelBody;
				st.bands = { rnds::Band{ 0, X(8), p.panelBand }, rnds::Band{ X(8), -1, p.panelBody } };
				st.shadows = { inset(0, -X(6), 0, p.panelLow), outer(X(hero ? 4 : 3), X(hero ? 4 : 3), p.shade45) };
			}
			else if (kind == "bubble")
			{
				st.radius = X(12); st.border = b3; st.borderColor = p.line; st.bg = p.bubbleBody;
				st.bands = { rnds::Band{ 0, X(10), p.bubbleBand }, rnds::Band{ X(10), -1, p.bubbleBody } };
				st.shadows = { inset(0, -X(6), 0, p.bubbleLow), outer(X(4), X(4), p.blackish) };
			}
			else if (kind == "face")
			{
				st.radius = X(10); st.border = b3; st.borderColor = p.edge; st.bg = p.panelBody;
				st.bands = { rnds::Band{ 0, X(8), p.bubbleBand }, rnds::Band{ X(8), -1, p.panelBody } };
				st.shadows = { inset(0, -X(4), 0, p.panelLow), outer(X(3), X(3), p.blackish) };
			}
			else if (kind == "kbd") { st.radius = X(4); st.bg = p.kbdBg; }
			st.ditherScale = s; st.ditherA = p.ditherA; st.ditherB = p.ditherB;
			rnds::PaintedBox pb = rnds::paintBox(st, dw, dh);
			Cached c;
			c.tex = be->upload(pb.bmp, true);
			c.ox = pb.originX; c.oy = pb.originY;
			it = cache.emplace(key, c).first;
		}
		const Cached& c = it->second;
		be->draw(c.tex, std::round(X(x)) - c.ox, std::round(X(y)) - c.oy, (float)c.tex->w, (float)c.tex->h, Color(1, 1, 1, 1));
	}

	float Settings::text(const std::string& t, bool medium, float size, float x, float top, const Color& c, bool upper, float ls, bool measureOnly)
	{
		if (t.empty()) return 0;
		rnds::TextStyle st;
		st.font = medium ? M : R; st.size = size; st.letterSpacing = ls; st.upper = upper;
		if (!st.font) return 0;
		std::string key = "txt:" + t + (medium ? "|m" : "|r") + std::to_string(size) + (upper ? "u" : "") + std::to_string(ls);
		auto it = cache.find(key);
		if (it == cache.end())
		{
			rnds::TextLine l = rnds::layoutLine(rnds::utf8to32(t), st);
			float asc = st.font->ascent(size), desc = st.font->descent(size), lh = asc + desc;
			rnds::TextBitmap tb = rnds::renderLine(l, st, s, asc + std::floor((lh - (asc + desc)) / 2));
			Cached cc;
			if (!tb.bmp.empty()) cc.tex = be->upload(tb.bmp, true);
			cc.ox = tb.padX; cc.oy = tb.padY; cc.w = l.width;
			it = cache.emplace(key, cc).first;
		}
		const Cached& cc = it->second;
		if (!measureOnly && cc.tex)
			be->draw(cc.tex, std::floor(X(x)) - cc.ox, std::floor(X(top)) - cc.oy, (float)cc.tex->w, (float)cc.tex->h, c);
		return cc.w;
	}

	void Settings::wrapped(const std::string& t, float size, float x, float top, float width, int maxLines, const Color& c)
	{
		rnds::TextStyle st;
		st.font = R; st.size = size;
		if (!R) return;
		std::vector<rnds::TextLine> lines = rnds::layoutWrap(rnds::utf8to32(t), st, (int)width, maxLines);
		float lh = std::round(size * 1.4f);
		for (size_t i = 0; i < lines.size(); i++)
		{
			std::string u8;
			for (char32_t ch : lines[i].text)
			{
				// back to UTF-8 for the cache key and text()
				if (ch < 0x80) u8 += (char)ch;
				else if (ch < 0x800) { u8 += (char)(0xc0 | (ch >> 6)); u8 += (char)(0x80 | (ch & 0x3f)); }
				else { u8 += (char)(0xe0 | (ch >> 12)); u8 += (char)(0x80 | ((ch >> 6) & 0x3f)); u8 += (char)(0x80 | (ch & 0x3f)); }
			}
			text(u8, false, size, x, top + i * lh, c);
		}
	}

	void Settings::icon(const std::string& name, float x, float y, int scale)
	{
		std::string key = "icon:" + name;
		auto it = cache.find(key);
		if (it == cache.end())
		{
			rnds::Bitmap b = rnds::decodeFile(assets + "/icons/" + name + ".png");
			Cached c;
			if (!b.empty()) c.tex = be->upload(b, false);
			it = cache.emplace(key, c).first;
		}
		if (!it->second.tex) return;
		int k = std::max(1, (int)std::lround(scale * s));	// whole device px per icon pixel
		be->draw(it->second.tex, std::round(X(x)), std::round(X(y)), (float)it->second.tex->w * k, (float)it->second.tex->h * k, Color(1, 1, 1, 1));
	}

	// the engine's ◀ ▶: a stepped triangle on the look's pixel grid
	void Settings::arrow(bool right, float x, float y, const Color& c)
	{
		int u = std::max(1, (int)std::lround(s));
		int rows = std::max(3, (int)std::lround(13 * s / u)) | 1;
		std::string key = std::string("arrow:") + (right ? "r" : "l") + std::to_string(rows) + ":" + std::to_string(u);
		auto it = cache.find(key);
		if (it == cache.end())
		{
			rnds::Bitmap b;
			b.w = (rows / 2 + 1) * u; b.h = rows * u; b.alpha = true;
			b.px.assign((size_t)b.w * b.h, 0);
			int half = rows / 2;
			for (int r = 0; r < rows; r++)
			{
				int span = (half + 1 - std::abs(r - half)) * u, x0 = right ? 0 : b.w - span;
				for (int yy = r * u; yy < (r + 1) * u; yy++)
					std::fill(b.px.begin() + (size_t)yy * b.w + x0, b.px.begin() + (size_t)yy * b.w + x0 + span, (uint8_t)255);
			}
			Cached cc;
			cc.tex = be->upload(b, false);
			it = cache.emplace(key, cc).first;
		}
		be->draw(it->second.tex, std::round(X(x)), std::round(X(y)), (float)it->second.tex->w, (float)it->second.tex->h, c);
	}

	float Settings::faceWidth(const std::string& key, const std::string& label)
	{
		return 15 + 12 + text(key, true, 16, 0, 0, Color(), false, 0, true) + 8 + text(label, true, 19, 0, 0, Color(), false, 0, true) + 18;
	}

	// the menu's B / A chips (RndsUI's drawFace)
	void Settings::face(const std::string& key, const std::string& label, float x, float w)
	{
		SPal p = pal(light);
		box("face", false, x, 422, w, 40);
		float kw = 12 + text(key, true, 16, 0, 0, p.ink, false, 0, true);
		box("kbd", false, x + 15, 430.5f, kw, 23);
		text(key, true, 16, x + 21, 432.5f, p.kbdInk);
		text(label, true, 19, x + 15 + kw + 8, 431, p.ink);
	}

	void Settings::notice(const std::string& title, const std::string& body, int percent)
	{
		SPal p = pal(light);
		box("bubble", false, 40, 344, 560, 104);
		// the status bar's battery, three times its size on the same pixel grid, its level red at 10% and under
		auto it = cache.find("battery");
		if (it == cache.end())
		{
			rnds::Bitmap b = rnds::decodeFile(assets + "/battery@1x.png");
			Cached c;
			if (!b.empty()) c.tex = be->upload(b, false);
			it = cache.emplace("battery", c).first;
		}
		int k = std::max(1, (int)std::lround(3 * s));
		float bx = std::round(X(66)), by = std::round(X(375));
		if (it->second.tex)
			be->draw(it->second.tex, bx, by, (float)it->second.tex->w * k, (float)it->second.tex->h * k, p.ink);
		int lv = percent <= 0 ? 0 : std::max(1, (int)std::lround(16 * std::min(100, percent) / 100.0f));
		Color lc = percent <= 10 ? (light ? Color::hex(0xc8382d) : Color::hex(0xff5a4f)) : p.accent;
		if (lv > 0) be->fill(bx + 3 * k, by + 3 * k, (float)lv * k, (float)8 * k, lc);
		text(title, true, 22, 172, 360, p.ink);
		wrapped(body, 18, 172, 392, 404, 2, p.muted);
	}

	void Settings::render()
	{
		if (page == NETWORKS) { renderNetworks(); return; }
		if (page == KEYBOARD) { renderKeyboard(); return; }
		if (page == UPDATES) { renderUpdates(); return; }
		if (page == SETUP) { renderSetup(); return; }
		SPal p = pal(light);
		const Item& cur = items[sel];

		// ---- top: what the selected setting is ----
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon(cur.icon, 50, 56, 3);
		text(cur.section, true, 17, 170, 62, p.muted, true, 1.4f);
		text(cur.label, true, 30, 170, 88, p.ink);
		std::string value = cur.kind == Item::CHOICE || cur.kind == Item::BRIGHTNESS || cur.kind == Item::VOLUME ? cur.names[cur.at] : "";
		if (!value.empty()) text(value, true, 22, 170, 130, p.accent);
		std::string help = cur.help;
		if (cur.kind == Item::INFO) help = about;
		if (cur.key == "wifi") help = "Wi-Fi: " + wifiInfo + ". Network, below, joins one; networks can also be written in banana/wifi.conf on a computer (see banana/README.txt).";
		if (cur.key == "ssh")
			help = "For working on Banana itself: ssh root@" + (ip.empty() ? std::string("<address>") : ip) +
				", with the public keys in banana/ssh/authorized_keys on the card (no passwords). Files: scp -O (dropbear has no SFTP), or File sharing.";
		if (cur.key == "switch" && !message.empty()) help = message;
		if (cur.key == "share")
		{
			std::string at = ip.empty() ? "<address>" : ip;
			help = "The card on the network: \\\\" + at + "\\banana in Windows' File Explorer, smb://" + at +
				"/banana on a Mac (Finder, Go, Connect to Server). Anyone on the network can open it with no password: "
				"leave it off on networks you don't trust.";
			if (ip.empty()) help += " (Not connected to Wi-Fi now.)";
		}
		wrapped(help, 20, 50, 196, 540, 7, p.muted);
		be->endScreen();

		// ---- bottom: the list ----
		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("bubble", false, 18, 14, 604, 54);
		text("Settings", true, 22, 38, 28, p.ink);
		char count[32];
		snprintf(count, sizeof count, "%02d / %02d", sel + 1, (int)items.size());
		float cw = text(count, true, 17, 0, 0, p.muted, false, 0, true);
		text(count, true, 17, 602 - cw, 31, p.muted);
		for (int r = 0; r < ROWS && top + r < (int)items.size(); r++)
		{
			int i = top + r;
			const Item& it = items[i];
			bool on = i == sel;
			float y = 82 + r * 64;
			box("slot", on, 18, y, 604, 56);
			text(it.label, true, 20, 38, y + 16, on ? p.ink : p.muted);
			std::string v;
			if (it.kind == Item::CHOICE || it.kind == Item::BRIGHTNESS || it.kind == Item::VOLUME) v = it.names[it.at];
			else if (it.kind == Item::ACTION) v = armed == i ? "Press A again" : "";
			else if (it.kind == Item::INFO) v = "";
			else if (it.kind == Item::PAGE) v = wstat.connected ? fit(wstat.ssid, true, 19, 300) : (wifiOn() ? "Choose" : "");
			if (v.empty()) continue;
			float vw = text(v, true, 19, 0, 0, p.ink, false, 0, true);
			Color vc = on ? (armed == i ? p.accent : p.ink) : p.muted;
			if (it.kind == Item::ACTION || it.kind == Item::PAGE) { text(v, true, 19, 586 - vw, y + 17, vc); continue; }
			arrow(true, 576, y + 21, vc);
			text(v, true, 19, 566 - vw, y + 17, vc);
			arrow(false, 556 - vw - 2 - std::round(13 / 2.0f + 1), y + 21, vc);
		}
		float bw = faceWidth("B", "Back");
		face("B", "Back", 18, bw);
		std::string act = cur.kind == Item::ACTION ? (armed == sel ? "Confirm" : cur.label) : cur.kind == Item::INFO ? "" : cur.kind == Item::PAGE ? "Open" : "Change";
		if (!act.empty())
		{
			float aw = faceWidth("A", act);
			face("A", act, 622 - aw, aw);
		}
		be->endScreen();
	}

	// ---- Wi-Fi: the networks around, and a keyboard for the password ----------------------------------------------
	bool Settings::wifiOn() const
	{
		return getConfValue(data + "/settings.conf", "wifi", getConfValue("/etc/banana/settings.conf", "wifi", "on")) != "off";
	}

	void Settings::openNetworks()
	{
		page = NETWORKS;
		nsel = ntop = 0;
		message.clear();
		wifiEnabled = wifiOn();
		if (!wifiEnabled) { nets.clear(); return; }
		startScan();
		pollStatus();
	}

	void Settings::startScan()
	{
		if (scanJob.running()) return;
		scanning = true;
		scanJob.start(wifiTool + " scan", sync);
		nextScan = now + 15000;		// again while the page is open: the list follows the handheld around
	}

	void Settings::pollStatus()
	{
		if (!statusJob.running()) statusJob.start(wifiTool + " status", sync);
		nextStatus = now + 1500;
	}

	void Settings::markNetworks()
	{
		std::string selName = nsel < (int)nets.size() ? nets[nsel].ssid : "";
		auto saved = wifi::savedNames(wifiConf);
		for (auto& n : nets)
		{
			n.saved = std::find(saved.begin(), saved.end(), n.ssid) != saved.end();
			n.current = wstat.connected && n.ssid == wstat.ssid;
		}
		std::stable_partition(nets.begin(), nets.end(), [](const wifi::Network& n) { return n.current; });	// the connected one first
		// the cursor stays on the network it was on
		for (int i = 0; i < (int)nets.size(); i++) if (nets[i].ssid == selName) nsel = i;
		nsel = std::max(0, std::min(nsel, (int)nets.size() - 1));
		if (nsel < ntop) ntop = nsel;
		if (nsel >= ntop + ROWS) ntop = nsel - ROWS + 1;
	}

	bool Settings::update(double t)
	{
		now = t;
		if (!mOpen) return false;
		if (page == NETWORKS && wifiEnabled)
		{
			if (!joining.empty() && now >= nextStatus) pollStatus();
			if (joining.empty() && now >= nextScan) startScan();
		}
		bool changed = false;
		if (page == UPDATES && applying && now >= nextStatus2)
		{
			std::string before = ustatus;
			pollUpdate();
			changed |= ustatus != before;
		}
		std::string out;
		if (scanJob.take(out))
		{
			nets = wifi::parseScan(out);
			scanning = false;
			scannedOnce = true;
			markNetworks();
			changed = true;
		}
		if (statusJob.take(out))
		{
			wifi::Status st = wifi::parseStatus(out);
			changed |= st.connected != wstat.connected || st.ssid != wstat.ssid || st.ip != wstat.ip;
			wstat = st;
			if (!joining.empty() && st.connected && st.ssid == joining && !st.ip.empty())
			{
				message = "Connected to " + joining + " (" + st.ip + ").";
				joining.clear();
				changed = true;
			}
			markNetworks();
		}
		if (!joining.empty() && now - joinStart > 30000)
		{
			message = wstat.connected && wstat.ssid == joining ? "Joined " + joining + ", but it gave the handheld no address."
				: "Couldn't join " + joining + ". If the password was wrong, A on it types it again.";
			joining.clear();
			changed = true;
		}
		return changed;
	}

	int Settings::nextWake(double t) const
	{
		if (!mOpen) return -1;
		if (scanJob.running() || statusJob.running()) return 100;
		if (page == UPDATES && applying) return std::max(0, (int)(nextStatus2 - t));
		if (page != NETWORKS || !wifiEnabled) return -1;
		double due = joining.empty() ? nextScan : std::min(nextStatus, joinStart + 30001);
		return std::max(0, (int)(due - t));
	}

	void Settings::join(const wifi::Network& n, const std::string& password)
	{
		page = NETWORKS;
		if (!wifi::remember(wifiConf, n, password)) { message = "Couldn't write banana/wifi.conf on the card."; return; }
		if (!wifiEnabled)
		{
			setConfValue(data + "/settings.conf", "wifi", "on");
			for (auto& it : items) if (it.key == "wifi") load(it);
			wifiEnabled = true;
		}
		spawn(wifiTool.c_str(), "restart");		// wpa_supplicant again, with the network first
		joining = n.ssid;
		joinStart = now;
		nextStatus = now + 2000;
		message = "Joining " + n.ssid + "...";
		markNetworks();
	}

	Settings::Result Settings::pressNetworks(Key k)
	{
		int n = (int)nets.size();
		switch (k)
		{
		case Key::UP: case Key::DOWN:
			if (!n) return NONE;
			nsel = (nsel + (k == Key::UP ? -1 : 1) + n) % n;
			if (nsel < ntop) ntop = nsel;
			if (nsel >= ntop + ROWS) ntop = nsel - ROWS + 1;
			return MOVED;
		case Key::A: case Key::RIGHT:
		{
			if (!n || !joining.empty()) return NONE;
			const wifi::Network& net = nets[nsel];
			if (net.unsupported)
				message = net.ssid + " signs in a way the menu can't (a company network, or the old WEP): it can be written in banana/wifi.conf on a computer.";
			else if (net.current)
				message = "Connected to " + net.ssid + (wstat.ip.empty() ? "." : " (" + wstat.ip + ").");
			else if (!net.secured)
				join(net, "");
			else
			{
				target = net;
				startEntry(ENTRY_WIFI, "Password for", net.ssid, NETWORKS);
			}
			return MOVED;
		}
		case Key::X:
			message.clear();
			if (!wifiEnabled)
			{
				setConfValue(data + "/settings.conf", "wifi", "on");
				for (auto& it : items) if (it.key == "wifi") load(it);
				wifiEnabled = true;
			}
			startScan();
			return MOVED;
		case Key::Y:
		{
			if (!n || !nets[nsel].saved) return NONE;
			std::string name = nets[nsel].ssid;
			if (wifi::forget(wifiConf, name))
			{
				message = name + " forgotten: its password is gone from banana/wifi.conf.";
				if (name == joining) joining.clear();
				spawn(wifiTool.c_str(), "restart");
				markNetworks();
			}
			return MOVED;
		}
		case Key::B: case Key::LEFT: page = wizard ? SETUP : LIST; message.clear(); return MOVED;
		case Key::START: if (wizard) { page = SETUP; return MOVED; } page = LIST; mOpen = false; return CLOSED;
		default: return NONE;
		}
	}

	// the keyboard: four rows of ten keys in three layers, and a row of wider keys
	static const char* KEYS[3][4] = {
		{ "1234567890", "qwertyuiop", "asdfghjkl;", "zxcvbnm,./" },
		{ "!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL:", "ZXCVBNM<>?" },
		{ "~`'\"-_=+\\|", "[]{}<>()!?", "@#$%^&*;:/", ",.?!@#&*-_" },
	};
	static const struct { const char* name; int c0, span; } FKEYS[] = {
		{ "Shift", 0, 2 }, { "#+=", 2, 2 }, { "Space", 4, 3 }, { "Del", 7, 1 }, { "Done", 8, 2 },
	};
	static int fkeyAt(int col)
	{
		for (int i = 0; i < 5; i++) if (col >= FKEYS[i].c0 && col < FKEYS[i].c0 + FKEYS[i].span) return i;
		return 0;
	}
	static const float KX = 18, KPITCH = 60.4f, KY = 20, KROW = 80, KH = 70;

	Settings::Result Settings::keyboardKey(int row, int col)
	{
		if (row < 4)
		{
			if (pw.size() < 63) pw += KEYS[layer][row][col];
			message.clear();
			return MOVED;
		}
		switch (fkeyAt(col))
		{
		case 0: layer = layer == 1 ? 0 : 1; break;
		case 1: layer = layer == 2 ? 0 : 2; break;
		case 2: if (pw.size() < 63) pw += ' '; break;
		case 3: if (!pw.empty()) pw.pop_back(); break;
		case 4: return finishEntry();
		}
		message.clear();
		return MOVED;
	}

	Settings::Result Settings::pressKeyboard(Key k)
	{
		int d = k == Key::LEFT || k == Key::UP ? -1 : 1;
		switch (k)
		{
		case Key::LEFT: case Key::RIGHT:
			if (krow == 4) kcol = FKEYS[(fkeyAt(kcol) + d + 5) % 5].c0;
			else kcol = (kcol + d + 10) % 10;
			return MOVED;
		case Key::UP: case Key::DOWN:
			krow = (krow + d + 5) % 5;
			return MOVED;
		case Key::A: return keyboardKey(krow, kcol);
		case Key::B:
			if (pw.empty()) { page = entryBack; message.clear(); return MOVED; }
			pw.pop_back();
			return MOVED;
		case Key::X: layer = layer == 1 ? 0 : 1; return MOVED;
		case Key::Y: if (pw.size() < 63) pw += ' '; return MOVED;
		case Key::START: return keyboardKey(4, FKEYS[4].c0);
		default: return NONE;
		}
	}

	Settings::Result Settings::touchNetworks(float cx, float cy)
	{
		if (downY >= 418 && downY <= 466)
		{
			if (downX < 320) return pressNetworks(Key::B);
			return pressNetworks(Key::A);
		}
		for (int r = 0; r < ROWS && ntop + r < (int)nets.size(); r++)
		{
			float ry = 82 + r * 64;
			if (downY < ry || downY > ry + 56) continue;
			if (ntop + r != nsel) { nsel = ntop + r; return MOVED; }
			return pressNetworks(Key::A);
		}
		if (std::fabs(cy - downY) > 40 && !nets.empty())
		{
			int n = (int)nets.size();
			ntop = std::max(0, std::min(n - ROWS, ntop + (cy < downY ? ROWS : -ROWS)));
			nsel = std::max(ntop, std::min(nsel, ntop + ROWS - 1));
			return MOVED;
		}
		return NONE;
	}

	Settings::Result Settings::touchKeyboard(float cx, float cy)
	{
		(void)cx; (void)cy;
		if (downY >= 418 && downY <= 466)
		{
			if (downX < 320) return pressKeyboard(Key::B);
			return pressKeyboard(Key::START);
		}
		int row = (int)std::floor((downY - KY) / KROW);
		if (row < 0 || row > 4 || downY - KY - row * KROW > KH) return NONE;
		int col = (int)std::floor((downX - KX) / KPITCH);
		if (col < 0 || col > 9) return NONE;
		krow = row;
		kcol = row == 4 ? FKEYS[fkeyAt(col)].c0 : col;
		return keyboardKey(row, col);
	}

	// ---- Wi-Fi: drawing --------------------------------------------------------------------------------------------
	std::string Settings::fit(const std::string& t, bool medium, float size, float width)
	{
		if (text(t, medium, size, 0, 0, Color(), false, 0, true) <= width) return t;
		std::string r = t;
		while (!r.empty())
		{
			do r.pop_back(); while (!r.empty() && ((unsigned char)r.back() & 0xc0) == 0x80);	// a whole UTF-8 character
			if (!r.empty() && ((unsigned char)r.back() & 0xc0) == 0xc0) r.pop_back();
			if (text(r + "...", medium, size, 0, 0, Color(), false, 0, true) <= width) return r + "...";
		}
		return "...";
	}

	// signal: three steps, the lit ones in ink
	void Settings::bars(int n, float x, float y, const Color& on, const Color& off)
	{
		for (int i = 0; i < 3; i++)
		{
			float h = 8 + 6 * i, bx = x + i * 9;
			be->fill(std::round(X(bx)), std::round(X(y - h)), std::round(X(6)), std::round(X(h)), i < n ? on : off);
		}
	}

	// a padlock on a 2 px grid
	void Settings::lock(float x, float y, const Color& c)
	{
		auto r = [&](float rx, float ry, float rw, float rh) { be->fill(std::round(X(x + rx)), std::round(X(y + ry)), std::round(X(rw)), std::round(X(rh)), c); };
		r(3, 0, 6, 2);
		r(1, 2, 2, 6);
		r(9, 2, 2, 6);
		r(0, 8, 12, 10);
	}

	void Settings::renderNetworks()
	{
		SPal p = pal(light);
		const wifi::Network* cur = nsel < (int)nets.size() ? &nets[nsel] : nullptr;
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon("phone", 50, 56, 3);
		text("Network", true, 17, 170, 62, p.muted, true, 1.4f);
		text("Wi-Fi networks", true, 30, 170, 88, p.ink);
		std::string st = !wifiEnabled ? "Wi-Fi is off" : !joining.empty() ? "Joining " + joining
			: wstat.connected ? "Connected to " + wstat.ssid : "Not connected";
		text(fit(st, true, 22, 420), true, 22, 170, 130, p.accent);
		std::string help = message;
		if (help.empty())
			help = !wifiEnabled ? "Wi-Fi is switched off. X switches it on and searches."
				: "A joins the network chosen, with its password typed on the keyboard (or by touch). X searches again, Y forgets a saved network. Passwords are kept in banana/wifi.conf on the card.";
		wrapped(help, 20, 50, 196, 540, 7, p.muted);
		be->endScreen();

		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("bubble", false, 18, 14, 604, 54);
		text("Networks", true, 22, 38, 28, p.ink);
		std::string count = scanning && nets.empty() ? "Searching..." : std::to_string(nets.size()) + (nets.size() == 1 ? " network" : " networks");
		float cw = text(count, true, 17, 0, 0, p.muted, false, 0, true);
		text(count, true, 17, 602 - cw, 31, p.muted);
		Color dim(p.muted.r, p.muted.g, p.muted.b, 0.35f);
		for (int r = 0; r < ROWS && ntop + r < (int)nets.size(); r++)
		{
			const wifi::Network& n = nets[ntop + r];
			bool on = ntop + r == nsel;
			float y = 82 + r * 64;
			box("slot", on, 18, y, 604, 56);
			Color c = on ? p.ink : p.muted;
			float right = 572;
			bars(n.bars(), right, y + 40, c, dim);
			right -= 10;
			if (n.secured || n.unsupported) { right -= 12; lock(right, y + 19, c); right -= 12; }
			std::string tag = n.current ? "Connected" : n.ssid == joining ? "Joining" : n.saved ? "Saved" : "";
			if (!tag.empty())
			{
				float tw = text(tag, true, 17, 0, 0, c, false, 0, true);
				right -= tw;
				text(tag, true, 17, right, y + 18, n.current ? p.accent : c);
				right -= 14;
			}
			text(fit(n.ssid, true, 20, right - 38), true, 20, 38, y + 16, c);
		}
		if (nets.empty() && wifiEnabled && !scanning && scannedOnce) text("No networks found. X searches again.", true, 20, 38, 98, p.muted);
		float bw = faceWidth("B", "Back");
		face("B", "Back", 18, bw);
		std::string act = !cur || cur->current || cur->unsupported || !joining.empty() ? "" : cur->secured ? "Password" : "Join";
		if (!act.empty())
		{
			float aw = faceWidth("A", act);
			face("A", act, 622 - aw, aw);
		}
		be->endScreen();
	}

	void Settings::renderKeyboard()
	{
		SPal p = pal(light);
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon("phone", 50, 56, 3);
		text(entryTitle, true, 17, 170, 62, p.muted, true, 1.4f);
		text(fit(entrySub, true, 30, 420), true, 30, 170, 88, p.ink);
		box("slot", true, 50, 150, 540, 56);
		// the end of the password when it is longer than the field
		std::string shown = pw;
		float w = text(shown, true, 22, 0, 0, p.ink, false, 0, true);
		while (w > 480 && shown.size() > 1) { shown.erase(0, 1); w = text("..." + shown, true, 22, 0, 0, p.ink, false, 0, true); }
		if (shown != pw) shown = "..." + shown;
		if (pw.empty()) w = 0;
		else text(shown, true, 22, 70, 165, p.ink);
		be->fill(std::round(X(72 + w)), std::round(X(166)), std::round(X(10)), std::round(X(24)), p.accent);	// the cursor
		std::string n = std::to_string(pw.size()) + (pw.size() == 1 ? " character" : " characters");
		text(n, true, 17, 50, 216, p.muted);
		std::string help = !message.empty() ? message
			: entry == ENTRY_WIFI ? "A types the key, B deletes, X capitals, Y a space, START joins. Or type on the bottom screen. The password is kept in banana/wifi.conf on the card."
			: "A types the key, B deletes, X capitals, Y a space, START when it is typed. Or type on the bottom screen. Accounts are kept in banana/accounts.conf on the card.";
		wrapped(help, 20, 50, 252, 540, 6, message.empty() ? p.muted : p.accent);
		be->endScreen();

		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 10; c++)
			{
				float x = KX + c * KPITCH, y = KY + r * KROW;
				bool on = r == krow && c == kcol;
				box("slot", on, x, y, KPITCH - 6, KH);
				std::string k(1, KEYS[layer][r][c]);
				float tw = text(k, true, 26, 0, 0, p.ink, false, 0, true);
				text(k, true, 26, x + std::round((KPITCH - 6 - tw) / 2), y + 18, on ? p.ink : p.muted);
			}
		int fsel = krow == 4 ? fkeyAt(kcol) : -1;
		for (int i = 0; i < 5; i++)
		{
			float x = KX + FKEYS[i].c0 * KPITCH, y = KY + 4 * KROW, kw = FKEYS[i].span * KPITCH - 6;
			box("slot", i == fsel, x, y, kw, KH);
			std::string k = i == 1 && layer == 2 ? "abc" : FKEYS[i].name;
			bool lit = (i == 0 && layer == 1) || (i == 1 && layer == 2);
			float tw = text(k, true, 20, 0, 0, p.ink, false, 0, true);
			text(k, true, 20, x + std::round((kw - tw) / 2), y + 22, lit ? p.accent : i == fsel ? p.ink : p.muted);
		}
		float bw = faceWidth("B", pw.empty() ? "Back" : "Delete");
		face("B", pw.empty() ? "Back" : "Delete", 18, bw);
		const char* done = entry == ENTRY_WIFI ? "Join" : "Done";
		float aw = faceWidth("START", done);
		face("START", done, 622 - aw, aw);
		be->endScreen();
	}

	// ---- updates: the packages on the card, one applied by the update script ---------------------------------------
	void Settings::openUpdates()
	{
		page = UPDATES;
		usel = 0; armed = -1;
		message.clear();
		ustatus.clear();
		applying = false; updated = false;
		pkgs.clear();
		if (FILE* p = popen("/usr/libexec/banana/update list 2>/dev/null", "r"))
		{
			char ln[1024];
			while (fgets(ln, sizeof ln, p))
			{
				std::string l = trim(ln);
				size_t t1 = l.find('\t'), t2 = t1 == std::string::npos ? t1 : l.find('\t', t1 + 1);
				if (t1 == std::string::npos || t2 == std::string::npos) continue;
				pkgs.push_back({ l.substr(0, t1), l.substr(t1 + 1, t2 - t1 - 1), l.substr(t2 + 1) });
			}
			pclose(p);
		}
	}

	void Settings::pollUpdate()
	{
		nextStatus2 = now + 500;
		ustatus = readLine("/run/banana/update-status");
		if (ustatus.compare(0, 4, "done") == 0) { applying = false; updated = true; }
		else if (ustatus.compare(0, 6, "failed") == 0) applying = false;
	}

	Settings::Result Settings::pressUpdates(Key k)
	{
		int n = (int)pkgs.size();
		switch (k)
		{
		case Key::UP: case Key::DOWN:
			if (!n || applying) return NONE;
			usel = (usel + (k == Key::UP ? -1 : 1) + n) % n;
			armed = -1;
			return MOVED;
		case Key::A: case Key::RIGHT:
			if (updated) return REBOOT;			// "Restart": the new version runs from the next boot
			if (!n || applying) return NONE;
			if (armed != 1000 + usel) { armed = 1000 + usel; return MOVED; }	// A again to go ahead
			armed = -1;
			unlink("/run/banana/update-status");
			ustatus = "starting";
			applying = true;
			nextStatus2 = now + 500;
			spawn("/usr/libexec/banana/update", "apply", pkgs[usel].path.c_str());
			return MOVED;
		case Key::B: case Key::LEFT:
			if (applying) return NONE;			// not while a slot is being written
			page = LIST; armed = -1; message.clear();
			return MOVED;
		case Key::START:
			if (applying) return NONE;
			page = LIST; mOpen = false; return CLOSED;
		default: return NONE;
		}
	}

	void Settings::renderUpdates()
	{
		SPal p = pal(light);
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon("gear", 50, 56, 3);
		text("System", true, 17, 170, 62, p.muted, true, 1.4f);
		text("Update", true, 30, 170, 88, p.ink);
		std::string st = applying ? "Writing the update: " + ustatus : updated ? "Written: restart to run it" : ustatus.compare(0, 6, "failed") == 0 ? "Failed" : "Running " + (version.empty() ? std::string("dev") : version) + (slot.empty() ? "" : ", slot " + slot);
		text(fit(st, true, 22, 420), true, 22, 170, 130, p.accent);
		std::string help;
		if (applying) help = "The new version is being written to the other slot and checked; this one isn't touched. Don't turn the handheld off. It takes a minute or two.";
		else if (updated) help = "The new version is in the other slot and will run from the next restart. A restarts now. If it ever fails to start, Previous version brings this one back.";
		else if (ustatus.compare(0, 6, "failed") == 0) help = "The update wasn't applied: " + ustatus.substr(8) + ". This version is untouched.";
		else if (pkgs.empty()) help = "No update packages found. Put a banana-" + (board.empty() ? std::string("<board>") : board) + "-<version>.update.tar from a release in banana/updates on the card (File sharing can copy it over Wi-Fi), then come back here.";
		else help = "A on a package writes it to the other slot and checks it; the running version stays until the restart. Packages are in banana/updates on the card.";
		wrapped(help, 20, 50, 196, 540, 7, p.muted);
		be->endScreen();

		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("bubble", false, 18, 14, 604, 54);
		text("Update packages", true, 22, 38, 28, p.ink);
		std::string count = std::to_string(pkgs.size()) + (pkgs.size() == 1 ? " package" : " packages");
		float cw = text(count, true, 17, 0, 0, p.muted, false, 0, true);
		text(count, true, 17, 602 - cw, 31, p.muted);
		for (int r = 0; r < ROWS && r < (int)pkgs.size(); r++)
		{
			const Pkg& k = pkgs[r];
			bool on = r == usel;
			float y = 82 + r * 64;
			box("slot", on, 18, y, 604, 56);
			Color c = on ? p.ink : p.muted;
			text(fit("Banana " + k.version, true, 20, 330), true, 20, 38, y + 16, c);
			std::string tag = armed == 1000 + r ? "Press A again" : (k.board == board || board.empty() ? "for the " + k.board : "not for this handheld");
			float tw = text(tag, true, 17, 0, 0, c, false, 0, true);
			text(tag, true, 17, 586 - tw, y + 18, armed == 1000 + r ? p.accent : c);
		}
		if (pkgs.empty()) text("Nothing in banana/updates on the card.", true, 20, 38, 98, p.muted);
		float bw = faceWidth("B", "Back");
		if (!applying) face("B", "Back", 18, bw);
		std::string act = updated ? "Restart" : applying || pkgs.empty() ? "" : armed == 1000 + usel ? "Confirm" : "Update";
		if (!act.empty())
		{
			float aw = faceWidth("A", act);
			face("A", act, 622 - aw, aw);
		}
		be->endScreen();
	}

	// ---- the keyboard's purposes, and the accounts file -------------------------------------------------------
	void Settings::startEntry(Entry e, const std::string& title, const std::string& sub, Page back, const std::string& start)
	{
		entry = e; entryTitle = title; entrySub = sub; entryBack = back;
		pw = start;
		layer = 0; krow = 1; kcol = 0;
		message.clear();
		page = KEYBOARD;
	}

	std::string Settings::account(const char* key)
	{
		return getConfValue(data + "/accounts.conf", key, "");
	}

	Settings::Result Settings::finishEntry()
	{
		switch (entry)
		{
		case ENTRY_WIFI:
			if (!wifi::passwordOk(pw))
			{
				message = "A Wi-Fi password has 8 to 63 characters; this one has " + std::to_string(pw.size()) + ".";
				return MOVED;
			}
			join(target, pw);
			pw.clear();
			if (wizard) page = SETUP;
			return CHANGED;
		case ENTRY_RA_USER:
			if (pw.empty()) { message = "The account's name, as on retroachievements.org."; return MOVED; }
			setConfValue(data + "/accounts.conf", "ra_user", pw, "=");
			startEntry(ENTRY_RA_PASS, "RetroAchievements password for", pw, entryBack);
			return CHANGED;
		case ENTRY_RA_PASS:
			setConfValue(data + "/accounts.conf", "ra_password", pw, "=");
			pw.clear();
			page = entryBack;
			message.clear();
			return CHANGED;
		case ENTRY_SS_USER:
			if (pw.empty()) { message = "The account's name, as on screenscraper.fr."; return MOVED; }
			setConfValue(data + "/accounts.conf", "ss_user", pw, "=");
			startEntry(ENTRY_SS_PASS, "ScreenScraper password for", pw, entryBack);
			return CHANGED;
		case ENTRY_SS_PASS:
			setConfValue(data + "/accounts.conf", "ss_password", pw, "=");
			pw.clear();
			page = entryBack;
			message.clear();
			return CHANGED;
		}
		return NONE;
	}

	// ---- the first-time setup ----------------------------------------------------------------------------------
	void Settings::openSetup()
	{
		for (auto& it : items) load(it);
		refreshInfo();
		wizard = true;
		wsel = 0; armed = -1;
		message.clear();
		page = SETUP;
		if (wifiOn() && !statusJob.running()) statusJob.start(wifiTool + " status", sync);
	}

	void Settings::setupDone()
	{
		std::ofstream(data + "/setup-done") << "done\n";
		wizard = false;
		page = LIST;
		mOpen = false;
	}

	Settings::Result Settings::pressSetup(Key k)
	{
		Item* tz = nullptr;
		for (auto& it : items) if (it.key == "timezone") tz = &it;
		switch (k)
		{
		case Key::UP: case Key::DOWN:
			wsel = (wsel + (k == Key::UP ? -1 : 1) + 5) % 5;
			message.clear();
			return MOVED;
		case Key::LEFT: case Key::RIGHT:
			if (wsel != 1 || !tz) return NONE;
			{
				int n = (int)tz->values.size();
				tz->at = (tz->at + (k == Key::LEFT ? -1 : 1) + n) % n;
				save(*tz);
			}
			return CHANGED;
		case Key::A:
			switch (wsel)
			{
			case 0: openNetworks(); return MOVED;
			case 1: return pressSetup(Key::RIGHT);
			case 2: startEntry(ENTRY_RA_USER, "RetroAchievements", "Account name", SETUP, account("ra_user")); return MOVED;
			case 3: startEntry(ENTRY_SS_USER, "ScreenScraper", "Account name", SETUP, account("ss_user")); return MOVED;
			default: setupDone(); return CLOSED;
			}
		case Key::B: case Key::START:
			setupDone();		// skipped: Settings > System > First-time setup brings it back
			return CLOSED;
		default: return NONE;
		}
	}

	void Settings::renderSetup()
	{
		SPal p = pal(light);
		Item* tz = nullptr;
		for (auto& it : items) if (it.key == "timezone") tz = &it;
		std::string ra = account("ra_user"), ss = account("ss_user");
		static const char* names[5] = { "Wi-Fi", "Time zone", "RetroAchievements", "ScreenScraper", "Finish" };
		std::string status[5] = {
			wstat.connected ? "Connected to " + wstat.ssid : wifiOn() ? "Not connected" : "Off",
			tz ? tz->names[tz->at] : "",
			ra.empty() ? "Not signed in" : ra,
			ss.empty() ? "Not signed in" : ss,
			"" };
		static const char* helps[5] = {
			"Your home network: the games' achievements, the clock and the art need it. A picks it from the networks around; the password goes in on the keyboard.",
			"The clock on the top screen, and the games' own clocks. Left and right change it.",
			"Your retroachievements.org account: DraStic and RetroArch unlock achievements with it. A types the name, then the password. Leave it if you have none.",
			"Your screenscraper.fr account, for the box art and cartridge labels when they are fetched. A types the name, then the password. Leave it if you have none.",
			"Everything here is in the settings too (Start in the menu), and this setup is under System > First-time setup. A finishes." };
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon("gear", 50, 56, 3);
		text("First-time setup", true, 17, 170, 62, p.muted, true, 1.4f);
		text(names[wsel], true, 30, 170, 88, p.ink);
		if (!status[wsel].empty()) text(fit(status[wsel], true, 22, 420), true, 22, 170, 130, p.accent);
		wrapped(message.empty() ? helps[wsel] : message, 20, 50, 196, 540, 7, p.muted);
		be->endScreen();

		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("bubble", false, 18, 14, 604, 54);
		text("Welcome to Banana", true, 22, 38, 28, p.ink);
		std::string count = std::to_string(wsel + 1) + " / 5";
		float cw = text(count, true, 17, 0, 0, p.muted, false, 0, true);
		text(count, true, 17, 602 - cw, 31, p.muted);
		for (int r = 0; r < 5; r++)
		{
			bool on = r == wsel;
			float y = 82 + r * 64;
			box("slot", on, 18, y, 604, 56);
			Color c = on ? p.ink : p.muted;
			text(names[r], true, 20, 38, y + 16, c);
			if (status[r].empty()) continue;
			std::string v = fit(status[r], true, 19, 300);
			float vw = text(v, true, 19, 0, 0, c, false, 0, true);
			if (r == 1) { arrow(true, 576, y + 21, c); text(v, true, 19, 566 - vw, y + 17, c); arrow(false, 556 - vw - 2 - std::round(13 / 2.0f + 1), y + 21, c); }
			else text(v, true, 19, 586 - vw, y + 17, c);
		}
		float bw = faceWidth("B", "Skip");
		face("B", "Skip", 18, bw);
		const char* act = wsel == 4 ? "Finish" : wsel == 1 ? "Next zone" : "Open";
		float aw = faceWidth("A", act);
		face("A", act, 622 - aw, aw);
		be->endScreen();
	}
}
