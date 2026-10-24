#include <ctime>
#include "settings.h"
#include "lang.h"
#include "settings_defaults.h"
#include "theme.h"
#include <algorithm>
#include <dlfcn.h>
#include <thread>
#include <cctype>
#include <arpa/inet.h>
#include <chrono>
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
#include <sys/utsname.h>
#include <sys/stat.h>
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
		// a file that is there but can't be read (a card error): left alone, not replaced by one with only this key
		if (!f && access(path.c_str(), F_OK) == 0) return;
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
		// (a full card: the short copy is dropped and the old file kept, RetroArch's whole retroarch.cfg among them)
		bool ok = fwrite(out.data(), 1, out.size(), w) == out.size();
		ok &= fflush(w) == 0 && fsync(fileno(w)) == 0;
		ok &= fclose(w) == 0;
		if (!ok || rename(tmp.c_str(), path.c_str()) != 0) unlink(tmp.c_str());
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
		// the selected row of a list: the yellow plate the shelf's cartridge stands on (the engine's START plate), dark
		// text on it, and a dark amber for what the accent marks elsewhere ("Connected", "Press A again")
		Color pick, pickBand, pickLow, pickEdge, onPick, onPickAccent;
	};
	// Update's row: a download going on, a newer version the last check found, or how many update packages wait in
	// banana/updates on the card ("" for none); looked at once every 2 s at most (the list is drawn on every change)
	static std::string updatesWaiting(const std::string& updates, const std::string& run, const std::string& running)
	{
		static time_t at = 0;
		static std::string v;
		time_t now = time(nullptr);
		if (now - at < 2 && at) return v;
		at = now;
		{
			long long a = 0, t = 0;
			int pid = 0;
			std::ifstream(run + "/update-download.pid") >> pid;
			if (pid > 0 && kill(pid, 0) == 0 && sscanf(readLine(run + "/update-download").c_str(), "downloading %lld %lld", &a, &t) == 2 && t > 0)
				return v = "Downloading " + std::to_string((int)(a * 100 / t)) + "%";
			std::string state, last, current, latest;
			std::ifstream f(updates + "/.check");
			for (std::string ln; std::getline(f, ln);)
			{
				if (ln.compare(0, 6, "state=") == 0) state = ln.substr(6);
				else if (ln.compare(0, 11, "last_state=") == 0) last = ln.substr(11);
				else if (ln.compare(0, 8, "current=") == 0) current = ln.substr(8);
				else if (ln.compare(0, 7, "latest=") == 0) latest = ln.substr(7);
			}
			// (a check made by the version before an update isn't this one's)
			if ((state == "newer" || (state == "error" && last == "newer")) && !latest.empty() && (running.empty() || current == running))
				return v = latest + " available";
		}
		int n = 0;
		if (DIR* d = opendir(updates.c_str()))
		{
			while (dirent* e = readdir(d))
			{
				size_t l = strlen(e->d_name);
				if (l > 11 && !strcmp(e->d_name + l - 11, ".update.tar")) n++;
			}
			closedir(d);
		}
		v = n == 0 ? "" : n == 1 ? "1 package" : std::to_string(n) + " packages";
		return v;
	}

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
			p.slotOn = Color::hex(rnds::accent.base); p.kbdBg = Color::hex(0x3a342c); p.kbdInk = Color::hex(0xf2f6fa);
			p.blackish = Color(sh.r, sh.g, sh.b, 0.3f); p.shade45 = Color(sh.r, sh.g, sh.b, 0.25f);
		}
		p.accent = Color::hex(rnds::tintAccent(light ? 0xffc07a00u : 0xffffd23fu) & 0xffffff);	// the accent for values being changed
		p.pick = Color::hex(rnds::accent.base); p.pickBand = Color::hex(rnds::accent.band); p.pickLow = Color::hex(rnds::accent.low);
		p.pickEdge = light ? Color::hex(rnds::accent.dark) : Color::hex(0xffffff);
		p.onPick = Color::hex(0x17161c); p.onPickAccent = Color::hex(0x7a3a00);
		// a user theme's colours by the same names as the engine's palette (RndsUI.cpp), and two of this screen's own
		struct { const char* n; Color SPal::* m; } names[] = {
			{ "ink", &SPal::ink }, { "muted", &SPal::muted }, { "line", &SPal::line }, { "edge", &SPal::edge },
			{ "screenBg", &SPal::screenBg }, { "screenFrame", &SPal::screenFrame }, { "screenBand", &SPal::screenBand },
			{ "ditherA", &SPal::ditherA }, { "ditherB", &SPal::ditherB }, { "panelBand", &SPal::panelBand },
			{ "panelBody", &SPal::panelBody }, { "panelLow", &SPal::panelLow }, { "bubbleBand", &SPal::bubbleBand },
			{ "bubbleBody", &SPal::bubbleBody }, { "bubbleLow", &SPal::bubbleLow }, { "slotOn", &SPal::slotOn },
			{ "kbdBg", &SPal::kbdBg }, { "kbdInk", &SPal::kbdInk }, { "blackish", &SPal::blackish }, { "shade45", &SPal::shade45 },
			{ "onPick", &SPal::onPick }, { "onPickAccent", &SPal::onPickAccent },
		};
		for (auto& e : names) rnds::themeColor(e.n, p.*e.m);
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
		const char* btt = getenv("BANANA_BLUETOOTH");	// (the test's stand-in)
		btTool = btt ? btt : "/usr/libexec/banana/bluetooth";
		const char* up = getenv("BANANA_UPDATE");	// (the test: the repo's script, pointed at a test feed)
		updateTool = up ? up : "/usr/libexec/banana/update";
		const char* run = getenv("BANANA_RUN");
		runDir = run ? run : "/run/banana";
		updatesDir = data + "/updates";
		build();
	}

	void Settings::setVariant(const std::string& value)
	{
		// the Theme setting's value (theme.cpp): its palette, a user theme's colours, files and accent set (or cleared)
		std::string v = applyTheme(value, getConfValue(data + "/settings.conf", "accent", "yellow"));
		bool l = v == "light";
		light = l; cache.clear();	// (also for a new accent: the boxes are drawn again)
		// the Carts theme writes in Inter (a clean typeface), the others in the theme's Pixelify Sans
		bool clean = v == "carts" && !fontDir.empty();
		std::string fonts = clean ? "carts" : rnds::themeFiles.fontRegular + "|" + rnds::themeFiles.fontMedium;
		if (clean != cleanFonts || !R || fonts != fontsLoaded)
		{
			cleanFonts = clean;
			fontsLoaded = fonts;
			// a user theme's fonts (rnds::themeFiles) on its Pixel palettes; one that doesn't load: the theme's
			const std::string& ownR = rnds::themeFiles.fontRegular, &ownM = rnds::themeFiles.fontMedium;
			R = rnds::Font::load(clean ? fontDir + "/Inter-Regular.ttf" : !ownR.empty() ? ownR : assets + "/fonts/PixelifySans-Regular.ttf");
			M = rnds::Font::load(clean ? fontDir + "/Inter-Medium.ttf" : !ownM.empty() ? ownM : assets + "/fonts/PixelifySans-Medium.ttf");
			if (!R || !M)
			{
				R = rnds::Font::load(assets + "/fonts/PixelifySans-Regular.ttf");
				M = rnds::Font::load(assets + "/fonts/PixelifySans-Medium.ttf");
				cleanFonts = false;
			}
		}
	}

	// time zones as POSIX TZ rules (glibc reads them from TZ with no zoneinfo files): the offset and the summer time
	// rules as they stand, one entry per rule with the places that follow it (from tzdata 2026e's footers: tail -1 of
	// the zoneinfo file; Casablanca is left out, its Ramadan changes are no POSIX rule)
	static const struct { const char* name; const char* tz; } ZONES[] = {
		{ "UTC", "UTC0" },
		{ "Reykjavik, Accra, Dakar", "GMT0" },
		{ "London, Dublin, Lisbon", "GMT0BST,M3.5.0/1,M10.5.0" },
		{ "Lagos, Algiers, Kinshasa", "WAT-1" },
		{ "Amsterdam, Berlin, Paris, Rome", "CET-1CEST,M3.5.0,M10.5.0/3" },
		{ "Johannesburg, Harare", "SAST-2" },
		{ "Athens, Helsinki, Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
		{ "Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24" },
		{ "Istanbul, Riyadh, Nairobi", "<+03>-3" },
		{ "Moscow", "MSK-3" },
		{ "Tehran", "<+0330>-3:30" },
		{ "Dubai, Baku", "<+04>-4" },
		{ "Kabul", "<+0430>-4:30" },
		{ "Karachi, Tashkent, Almaty", "PKT-5" },
		{ "India, Sri Lanka", "IST-5:30" },
		{ "Kathmandu", "<+0545>-5:45" },
		{ "Dhaka", "<+06>-6" },
		{ "Yangon", "<+0630>-6:30" },
		{ "Bangkok, Jakarta, Hanoi", "<+07>-7" },
		{ "Beijing, Singapore, Perth", "CST-8" },
		{ "Tokyo, Seoul", "JST-9" },
		{ "Darwin", "ACST-9:30" },
		{ "Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3" },
		{ "Brisbane, Guam", "AEST-10" },
		{ "Sydney, Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
		{ "Noumea, Solomon Islands", "<+11>-11" },
		{ "Fiji", "<+12>-12" },
		{ "Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3" },
		{ "Tonga, Samoa", "<+13>-13" },
		{ "Pago Pago", "SST11" },
		{ "Honolulu", "HST10" },
		{ "Anchorage", "AKST9AKDT,M3.2.0,M11.1.0" },
		{ "Los Angeles, Tijuana", "PST8PDT,M3.2.0,M11.1.0" },
		{ "Phoenix, Vancouver, Yukon", "MST7" },
		{ "Denver", "MST7MDT,M3.2.0,M11.1.0" },
		{ "Mexico City, Calgary", "CST6" },
		{ "Chicago", "CST6CDT,M3.2.0,M11.1.0" },
		{ "Bogota, Lima, Panama, Winnipeg", "<-05>5" },
		{ "Havana", "CST5CDT,M3.2.0/0,M11.1.0/1" },
		{ "New York, Toronto", "EST5EDT,M3.2.0,M11.1.0" },
		{ "Caracas, La Paz, Manaus", "<-04>4" },
		{ "Puerto Rico, Santo Domingo", "AST4" },
		{ "Halifax", "AST4ADT,M3.2.0,M11.1.0" },
		{ "Santiago", "<-04>4<-03>,M9.1.6/24,M4.1.6/24" },
		{ "St. John's", "NST3:30NDT,M3.2.0,M11.1.0" },
		{ "Buenos Aires, Sao Paulo", "<-03>3" },
		{ "Fernando de Noronha", "<-02>2" },
		{ "Azores", "<-01>1<+00>,M3.5.0/0,M10.5.0/1" },
		{ "Cape Verde", "<-01>1" },
	};

	// on the RG DS Plus a shader's GPU pass heats the chip enough to slow the CPU in a long session (Golden Sun: Dark
	// Dawn at 2x: none 0.0% missed refreshes in 6 min, the LCD grid 0.6-1%, measured 2026-10-18)
	static const char* PLUS_SHADER_NOTE = " On the RG DS Plus a shader also heats the chip: a heavy 3D game can drop frames after a few minutes with one. Set such a game to None on its own page (X in the library).";

	// Settings > Nintendo DS > Shader's choices: Banana's shaders, then the card's own (banana/shaders/<name>.frag,
	// SuperDrastic's shader format: README.txt there). Made again each time Settings loads the row (load()), so a shader
	// copied over file sharing while the menu runs is listed without a restart
	static void shaderChoices(const std::string& data, std::vector<std::string>& shaderNames, std::vector<std::string>& shaderValues)
	{
		shaderNames = { "None (smooth)", "Sharp pixels (integer)", "Crisp", "Crisp + DS colours", "LCD grid", "LCD grid + colours", "FSR", "LCD (DS colours)", "LCD 1x", "LCD 3x", "Fast LCD",
			  "DS colours", "Retro", "Bevel", "Sharp bilinear", "Sharp + scanlines", "Fast CRT", "FXAA", "Integer fill", "LCD grid 2x",
			  "Dot (MiZai)", "Dot (MiZai, RG DS+)", "Retro tiles", "DraStic LCD", "DraStic LCD + colours", "DraStic Quilez",
			  "DraStic sharp", "DraStic sharp + colours", "DraStic fast LCD", "DraStic fast LCD + colours" };
		shaderValues = { "none", "ds-integer", "ds-crisp", "ds-crisp-color", "ds-grid", "ds-grid-color", "ds-fsr", "lcd1x-nds", "lcd1x", "lcd3x", "zfast-lcd",
			  "nds-color", "retro-v2", "bevel", "sharp-bilinear", "sharp-bilinear-scanlines", "zfast-crt", "fxaa", "ds-integer-fill", "ds-grid-2x",
			  "nnddss-dot-mizai", "nnddss-dot-mizai-rgds", "nnddss-retro-tiles", "drastic-lcd1x", "drastic-lcd1x-nds-color", "drastic-quilez",
			  "drastic-sharp-bilinear", "drastic-sharp-bilinear-nds-color", "drastic-zfast-lcd", "drastic-zfast-lcd-nds-color" };
		if (DIR* d = opendir((data + "/shaders").c_str()))
		{
			std::vector<std::string> own;
			while (dirent* e = readdir(d))
			{
				std::string n = e->d_name;
				if (n.size() > 5 && n[0] != '.' && n.compare(n.size() - 5, 5, ".frag") == 0)
				{
					// only names of letters, digits and . _ + - (the README says so): the name goes into the launchers'
					// settings files, and one with a space, a ";" or a "$" broke them (or ran what it held, before kv-env)
					std::string base = n.substr(0, n.size() - 5);
					if (base.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._+-") == std::string::npos)
						own.push_back(base);
				}
			}
			closedir(d);
			std::sort(own.begin(), own.end());
			for (auto& n : own)
				if (std::find(shaderValues.begin(), shaderValues.end(), n) == shaderValues.end()) { shaderNames.push_back(n + " (card)"); shaderValues.push_back(n); }
		}
	}

	// the panels' backlights (N2): backlight1 lights the top panel, backlight0 the bottom one (both handhelds' device
	// trees); a level file holds "<top> <bottom>" in thousandths, or one number for both (before 0.4.2)
	static const char* BACKLIGHT[2] = { "backlight1", "backlight0" };
	static int backlightPair(const std::string& path, int i)
	{
		int t = 0, b = 0;
		FILE* f = fopen(path.c_str(), "r");
		if (!f) return -1;
		int n = fscanf(f, "%d %d", &t, &b);
		fclose(f);
		if (n < 1) return -1;
		if (n == 1) b = t;
		int v = i == 0 ? t : b;
		return v > 0 && v <= 1000 ? v : -1;
	}

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
		{
			// Banana's three, then the themes in banana/themes on the card and the image's examples (theme.h)
			std::vector<std::string> names = { "Dark", "Light", "Carts" }, values = { "dark", "light", "carts" };
			for (auto& t : userThemes()) { names.push_back(t.name); values.push_back(t.value); }
			add(D, "picture", "Theme", "ROCKNIXDS Pixel's two palettes, dark or light for daylight; Carts (experimental): your games as cartridges on a shelf, in a clean typeface; or a theme of your own: a .json in banana/themes on the card (banana/themes/README.txt says how).",
				menu, "theme", names, values);
		}
		add(D, "picture", "Accent colour", "The colour of the selection, the logo and the boot animation's banana.", menu, "accent",
			{ "Yellow", "Blue", "Red", "Green", "Purple", "Pink" }, { "yellow", "blue", "red", "green", "purple", "pink" });
		// each panel its own backlight (N2); F + volume steps both
		add(D, "picture", "Top brightness", "The top screen's backlight. F + volume changes both screens anywhere, in games too.", "", "top",
			{ "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" }, { "100", "200", "300", "400", "500", "600", "700", "800", "900", "1000" }, Item::BRIGHTNESS);
		add(D, "picture", "Bottom brightness", "The bottom screen's backlight. F + volume changes both screens anywhere, in games too.", "", "bottom",
			{ "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" }, { "100", "200", "300", "400", "500", "600", "700", "800", "900", "1000" }, Item::BRIGHTNESS);
		add(D, "picture", "Screen colours", "", "", "colors", {}, {}, Item::PAGE);
		add(D, "picture", "Motion", "The top screen's bob and shine. Off holds it still and saves a little battery.", menu, "motion", { "On", "Off" }, { "on", "off" });
		add(D, "music", "Volume", "The speakers or the headphones, whichever is playing. The volume keys change it anywhere.", "", "",
			{ "0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" }, { "0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100" }, Item::VOLUME);
		add(D, "music", "Menu sounds", "The menu's clicks, and the cartridge going into the console.", menu, "sounds", { "On", "Off" }, { "on", "off" });
		add(D, "picture", "Battery", "The battery on the top screen: its icon, or its icon and how full it is.", menu, "battery", { "Icon", "Icon and %" }, { "icon", "percent" });
		add(D, "picture", "Clock", "How the time on the top screen is written.", menu, "clock", { "24-hour", "12-hour" }, { "24h", "12h" });
		add(D, "picture", "Boot animation", "The bananas and the cartridge while Banana starts, and backwards when it shuts down. Off goes straight to the menu once it is ready, and straight to black.", menu, "splash", { "On", "Off" }, { "on", "off" });
		const char* P = "Sleep & power";
		add(P, "moon", "Sleep when idle", "In the menu, after this long with no button pressed. Never while a game is running.", power, "autosleep_minutes",
			{ "Never", "5 minutes", "10 minutes", "15 minutes", "30 minutes" }, { "0", "5", "10", "15", "30" });
		add(P, "moon", "Dim when idle", "In the menu, the panels go down to a third after this long with no button pressed, and back at the first press. Never while a game is running.", power, "dim_minutes",
			{ "Never", "1 minute", "2 minutes", "5 minutes" }, { "0", "1", "2", "5" });
		add(P, "moon", "Closing the lid", "Sleep with the lid: the game pauses, opening the lid carries on where you were.", power, "lid_sleep", { "Sleeps", "Does nothing" }, { "yes", "no" });
		add(P, "moon", "Power off after sleeping", "Asleep this long (not while charging), the game is saved where you are and the handheld turns itself off, so the battery keeps for weeks. Press power to start it again: it goes straight back into the game.",
			power, "poweroff_after_sleep_minutes", { "Never", "15 minutes", "30 minutes", "1 hour", "2 hours" }, { "0", "15", "30", "60", "120" });
		add(P, "moon", "Waking up", "Fast: the handheld's parts wake up together. Careful: one after the other, slower but the safest. Banana switches to Careful by itself if a sleep ever fails.",
			power, "fast_resume", { "Fast", "Careful" }, { "yes", "no" });
		add(P, "moon", "LEDs", "Green, red when the battery is low, blinking red when nearly empty, amber while charging.", power, "led_brightness",
			{ "Bright", "Dim", "Off" }, { "255", "40", "0" });
		// Nintendo DS: the emulator and the quick resume are shared; below them only the chosen emulator's own settings,
		// each in that emulator's files (switching the emulator builds the list again: change()). Both draw the DS's 3D
		// with SuperDrastic's Gengis Engine, always (run-drastic, run-bananads).
		const char* N = "Nintendo DS";
		const std::string emu = getConfValue(menu, "nds_emulator", getConfValue("/etc/banana/settings.conf", "nds_emulator", "superdrastic"));
		add(N, "nds", "Emulator", "SuperDrastic: DraStic straight onto both panels, with its own governor. BananaDS: Banana's own emulator (melonDS's core). Both draw 3D with the Gengis Engine at 2x and have their menu on L3; each keeps its own settings below. X on a game in the library picks one for that game alone.",
			menu, "nds_emulator", { "SuperDrastic", "BananaDS" }, { "superdrastic", "bananads" });
		add(N, "nds", "Quick resume", "On: quitting keeps where you are, and the game goes on from there next time (a shutdown too). Off: quitting ends the game, which starts from its title next time.",
			menu, "quick_resume", { "Off", "On" }, { "off", "on" });
		// the DS's own language (its firmware's), in both emulators at once: a game that has several picks it (#22)
		add(N, "nds", "DS language", "The language the DS itself is set to: a game that has several starts in it. Both emulators.",
			"", "ds_language", { "English", "Japanese", "French", "German", "Italian", "Spanish" }, { "1", "0", "2", "3", "4", "5" });
		// the DS's own name (its firmware's user name), in both emulators at once: what a game calls you (#30)
		add(N, "nds", "DS name", "The name the DS itself is set to, as on a real one: Mario Kart's records, Pictochat, a game's save. 10 characters at most. Both emulators; empty: Dr DraStic in SuperDrastic, Banana in BananaDS.",
			"", "ds_name", {}, {}, Item::PAGE);
		std::vector<std::string> shaderNames, shaderValues;
		shaderChoices(data, shaderNames, shaderValues);
		std::string shaderHelpS = "Sharp pixels: every DS pixel an exact square, no blur (4x4 on the RG DS Plus, 2x2 with a frame on the RG DS). None scales smoothly. Your own: .frag files in banana/shaders, named with letters, digits and . _ + - only. Crisp: exact pixel-area scaling. Grid and LCD: the DS's screen grid. Colours: the DS's own, duller colours. CRT: a TV's scanlines. FSR: smooth edges, the heaviest. None costs no GPU work.";
		if (readLine("/etc/banana/board") == "rgds-plus") shaderHelpS += PLUS_SHADER_NOTE;
		const char* shaderHelp = shaderHelpS.c_str();
		if (emu == "bananads")
		{
			const std::string bds = "/data/bananads/bananads.conf";
			add(N, "nds", "Shader", shaderHelp, bds, "kms.shader", shaderNames, shaderValues);
			add(N, "nds", "3D resolution", "2x draws the DS's 3D at twice its size on these panels. 1x is the DS's own, the lightest.",
				bds, "render.scale", { "2x", "1x" }, { "2", "1" });
			add(N, "nds", "Edge marking", "The outlines some games draw round their 3D models (Zelda, Kirby, many Pokémon battles). Off leaves them out.",
				bds, "render.edge_marking", { "On", "Off" }, { "on", "off" });
			add(N, "nds", "Fog", "The distance fog some games draw in 3D. Off leaves it out.", bds, "render.fog", { "On", "Off" }, { "on", "off" });
			add(N, "nds", "Frame pacing", "Panels runs the DS at the panels' 60 Hz, a new frame every refresh (0.3% fast, the sound matched). DS rate keeps the DS's own 59.83 Hz, a frame shown twice every ~6 s.",
				bds, "kms.pace", { "Panels", "DS rate" }, { "display", "ds" });
			// experimental: BananaDS's kms.lowlat (its docs/SETTINGS.md): no queue, frames aimed at the vblank; run-ahead
			add(N, "nds", "Low latency (experimental)", "Experimental. On: no frame queue, each frame ready just before the panels' next refresh: a press shows about two refreshes sooner. Run-ahead also runs 1 or 2 frames ahead and back every frame, so the game answers sooner than its own lag; it needs twice the CPU or more (light 2D games at 1x) and turns itself off where it doesn't fit.",
				bds, "kms.lowlat", { "Off", "On", "Run-ahead 1 frame", "Run-ahead 2 frames" }, { "off", "on", "runahead1", "runahead2" });
			add(N, "nds", "CPU clock", "Saves battery finds the lowest clock each game runs at full speed and remembers it. Always full keeps the top clock.",
				bds, "cpugov", { "Saves battery", "Always full" }, { "on", "off" });
			add(N, "nds", "Timing", "Accurate is melonDS's own. Fast lets the two CPUs run longer between meeting (about 10% faster), but it can break a game: Golden Sun: Dark Dawn loses its 3D world.",
				bds, "core.slice", { "Accurate", "Fast" }, { "64", "auto" });
			add(N, "nds", "Microphone", "On: the game hears the real microphone (a blow, a voice; the DS's own games that ask for one). Off: it hears nothing from the mic, and the in-game menu's Blow still works for a game that wants one.",
				bds, "mic.source", { "On", "Off" }, { "device", "none" });
			add(N, "nds", "Mic sensitivity", "How far above the room's noise a word or a blow must be to reach the game. Gate off lets the game hear everything the microphone picks up, its own sound from the speaker included.",
				bds, "mic.threshold", { "Medium", "High", "Low", "Gate off" }, { "0.05", "0.02", "0.15", "0" });
			add(N, "nds", "Reset BananaDS", "BananaDS's settings back to Banana's. Saves, save states and each game's clock stay. Press A twice.",
				"", "nds-reset-bananads", { "" }, { "" }, Item::ACTION);
		}
		else
		{
			add(N, "nds", "Shader", shaderHelp, sd, "DSFLIP_SHADER", shaderNames, shaderValues);
			add(N, "nds", "3D resolution", "2x draws the DS's 3D at twice its size on these panels, at full speed with SuperDrastic.", dcfg, "hires_3d", { "2x", "1x" }, { "1", "0" });
			add(N, "nds", "Threaded 3D", "On draws the DS's 3D beside the game, the faster way. Off draws it in step with the game: for a game whose 3D shows on the wrong screen or a frame late with it on (the Dragon Quest games).",
				dcfg, "threaded_3d", { "On", "Off" }, { "1", "0" });
			add(N, "nds", "CPU clock", "Saves battery finds the lowest clock each game runs at full speed and remembers it. Always full keeps the top clock.",
				sd, "DSFLIP_CPUGOV", { "Saves battery", "Always full" }, { "1", "0" });
			add(N, "nds", "Frame pacing", "Panels runs the DS at the panels' 60 Hz, a new frame every refresh (0.3% fast, the sound matched). DS rate keeps the DS's own 59.83 Hz, a frame shown twice every ~6 s.",
				sd, "DSFLIP_PACE", { "Panels", "DS rate" }, { "panels", "ds" });
			// (Off first: SuperDrastic's own default, as in 0.4.2; its scale is not BananaDS's)
			add(N, "nds", "Microphone", "How loud a word or a blow must be to reach the game. Off: the game hears nothing from the mic, and the in-game menu's Blow still works for a game that wants one.",
				sd, "DSHOOK_MIC_THRESH", { "Off", "Medium", "High", "Low" }, { "0", "0.15", "0.03", "0.3" });
			// experimental: SuperDrastic's DSFLIP_LOWLAT (run-drastic): no queue, each frame at the next vblank, DraStic held
			add(N, "nds", "Low latency (experimental)", "Experimental. On: no frame queue: each frame goes to the panels at the next refresh it can make, and DraStic starts each frame as late as that allows, so a press shows about two refreshes sooner (the sound a little sooner too). Uneven frames (2x, heavy scenes) can show as small stutters the queue would hide.",
				sd, "DSFLIP_LOWLAT", { "Off", "On" }, { "0", "1" });
			add(N, "nds", "Reset buttons", "DraStic's buttons back to Banana's, for a button setup gone wrong in DraStic's menu: the pad as printed, F or L3 the menu, R2 save, L2 load, R3 fast forward. (HOME always opens the menu.) Press A twice.",
				"", "nds-reset-buttons", { "" }, { "" }, Item::ACTION);
			add(N, "nds", "Reset DraStic", "All of DraStic's and SuperDrastic's settings back to Banana's, the buttons too. Saves, save states, cheats and the DS's name and birthday stay. Press A twice.",
				"", "nds-reset-drastic", { "" }, { "" }, Item::ACTION);
		}
		const char* O = "Other systems";
		add(O, "gamepad", "Screen", "Which panel the systems other than the DS use (RetroArch). The other one shows the game panel (Second screen), or is switched off.",
			"/data/retroarch/retroarch.cfg", "video_monitor_index", { "Top", "Bottom" }, { "2", "1" });
		add(O, "gamepad", "Second screen", "While a game plays on one screen, the other shows its RetroAchievements and tiles to save, load, open RetroArch's menu, fast forward and quit (by touch when they are on the bottom screen). Off: the other screen stays dark.",
			menu, "retroarch_panel", { "Game panel", "Off" }, { "panel", "off" });
		add(O, "picture", "Game panel when idle", "With a game on the top screen, what the game panel on the bottom one does when nothing has touched it or pressed a button for a while: stays on, dims, or turns off. A touch or any button lights it again.",
			menu, "bottom_idle_mode", { "Dims", "Turns off", "Stays on" }, { "dim", "off", "on" });
		add(O, "picture", "Game panel idle time", "How long the game panel waits, with no touch and no button, before it dims or turns off (Game panel when idle).",
			menu, "bottom_idle_seconds", { "After 10 s", "After 30 s", "After 1 minute" }, { "10", "30", "60" });
		add(O, "gamepad", "Pokémon companion", "Pokémon Ruby, Sapphire, Emerald, FireRed and LeafGreen (the English releases) on the second screen: the party with levels, HP and moves, and in a battle the foe, its types and how each move does against it. Read from the game as it plays; other games keep the panel as it is. Needs Second screen on. Inspired by PokeDaisy by Lidor Itzhari.",
			menu, "pokemon_companion", { "Off", "On" }, { "off", "on" });
		add(O, "gamepad", "RetroAchievements", "Softcore: achievements unlock with save states, rewind and cheats still there. Hardcore: those are locked for the game, and the unlocks count as hardcore on retroachievements.org. RetroArch's own menu changes the same setting.",
			"/data/retroarch/retroarch.cfg", "cheevos_hardcore_mode_enable", { "Softcore", "Hardcore" }, { "false", "true" });
		add(O, "gamepad", "Quick resume", "On: quitting keeps where you are, and the game goes on from there next time (RetroArch's automatic save state; not in Hardcore). Off: a game starts from its title.", "/data/retroarch/retroarch.cfg", "savestate_auto_save", { "Off", "On" }, { "false", "true" });
		add(O, "picture", "Shader", "A look for the systems RetroArch runs: a handheld's LCD grid, a CRT's scanlines, or sharp pixels at any size. RetroArch's own Shaders menu still picks any of libretro's presets.",
			menu, "ra_shader", { "Off", "LCD", "CRT", "Sharp pixels" }, { "off", "lcd", "crt", "sharp" });
		add(O, "music", "Audio latency", "RetroArch's audio buffer. Low is the quickest to react and can crackle on a busy game; Safe has the most margin (and the most delay).",
			menu, "ra_audio_latency", { "Low (32 ms)", "Default (64 ms)", "Safe (128 ms)" }, { "32", "64", "128" });
		add(O, "picture", "VSync", "On waits for the panel's refresh before drawing the next frame, so nothing tears. Off lets a core run as fast as it can; the panel can show a half-drawn frame.",
			menu, "ra_vsync", { "On", "Off" }, { "on", "off" });
		if (access("/usr/lib/libretro/mgba_libretro.so", F_OK) == 0)
			add(O, "gamepad", "Game Boy Advance", "The emulator for GBA games. gpSP: the fastest. mGBA: the most accurate, and the one RetroAchievements accepts for hardcore.", menu, "core.gba", { "gpSP", "mGBA" }, { "gpsp", "mgba" });
		add(O, "gamepad", "RetroArch hotkeys", "F + a button in a game: X the menu, Start quits, L and R load and save a state, Y the frame counter, L2 rewind, R2 fast forward. Off: only F + X and F + Start, the two needed to get out, and none bound in RetroArch's own menu either.",
			menu, "ra_hotkeys", { "On", "Off" }, { "on", "off" });
		add(O, "gamepad", "Reset RetroArch", "RetroArch's settings back to Banana's: for a button bound away in RetroArch's menu (an A that does something else, a hotkey on a face button). Its controls, remaps and overrides go to retroarch/backup-<date> on the card. Saves, states, the RetroAchievements account and these settings stay. Press A twice.",
			"", "ra_reset", { "" }, { "" }, Item::ACTION);
		const char* W = "Network";
		add(W, "phone", "Wi-Fi", "", menu, "wifi", { "On", "Off" }, { "on", "off" });
		add(W, "phone", "Network", "The networks around: A on one joins it, with a password typed on the keyboard here (or by touch).", "", "network", {}, {}, Item::PAGE);
		add(W, "phone", "Bluetooth", "Controllers, headphones and speakers. Off (the default) leaves the radio unpowered, which saves battery; Bluetooth devices, below, pairs them. While Bluetooth headphones are connected the sound plays on them.",
			menu, "bluetooth", { "Off", "On" }, { "off", "on" });
		add(W, "phone", "Bluetooth devices", "The devices paired and the ones around: A pairs with one in pairing mode, connects a paired one or disconnects it.", "", "bt_devices", {}, {}, Item::PAGE);
		add(W, "phone", "File sharing", "", menu, "share", { "Off", "On" }, { "off", "on" });
		add(W, "phone", "Sharing login", "Name and password: the card asks for the name and password below (Windows refuses a share with no password). Guest: anyone on the network opens it with none; only on a network you trust.",
			menu, "share_login", { "Name and password", "Guest" }, { "password", "guest" });
		add(W, "phone", "Sharing name and password", "", "", "share_account", {}, {}, Item::PAGE);
		add(W, "phone", "SSH", "", menu, "ssh", { "Off", "On" }, { "off", "on" });
		const char* S = "System";
		{
			// the languages there are files for, each in its own name; the help names who translated them
			std::vector<std::string> names, codes;
			std::string help = "The language of the menu and the settings. Lines not yet translated stay in English.";
			for (auto& l : lang::available(assets + "/../lang"))
			{
				names.push_back(l.name); codes.push_back(l.code);
				if (!l.by.empty()) help += " " + l.name + " (" + l.nameEn + "): translated by " + l.by + ".";
			}
			langHelp = help;
			if (names.size() > 1) add(S, "gear", "Language", langHelp.c_str(), menu, "language", names, codes);
		}
		{
			std::vector<std::string> names, tzs;
			for (auto& z : ZONES) { names.push_back(z.name); tzs.push_back(z.tz); }
			add(S, "gear", "Time zone", "The clock's time zone, summer time included, for the menu and the games (a DS game's clock too). The time itself comes from Wi-Fi or the handheld's own clock.",
				menu, "timezone", names, tzs);
		}
		add(S, "gear", "Update", "A new version of Banana, downloaded from GitHub over Wi-Fi or from a package on the card, written to the other slot and run from the next restart; this one stays as it is until then.", "", "update", {}, {}, Item::PAGE);
		add(S, "gear", "Test versions", "Whether Update's check counts test versions (GitHub pre-releases). Every BananaOS release so far is one, so with them left out the check finds nothing until a stable version comes out.",
			menu, "update_prerelease", { "Included", "Left out" }, { "on", "off" });
		add(S, "gear", "Update check", "When asked: GitHub is only asked when you choose Check for updates. Daily: also when Wi-Fi connects, at most once a day, so the Update row says when a new version is out. Nothing is downloaded without you.",
			menu, "update_daily", { "When asked", "Daily" }, { "off", "on" });
		add(S, "picture", "Box art", "Covers, title screens, genres and years for your games, from libretro's free collections (no account, Wi-Fi needed). It keeps going in the background; games that already have art are skipped, so running it again only fetches what is new.",
			"", "scrape", {}, {}, Item::PAGE);
		add(S, "picture", "Box art from", "libretro: libretro's collections. libretro + Twilight: TwilightBoxart (boxart.kirovair.com) for the covers libretro has none of. Twilight first: TwilightBoxart's covers where it has them. TwilightBoxart is asked one game a second.",
			menu, "boxart_source", { "libretro", "libretro + Twilight", "Twilight first" }, { "libretro", "both", "twilight" });
		add(S, "gear", "Previous version", "Restarts into the other slot: the version that ran before the last update. Press A twice.", "", "switch", { "" }, { "" }, Item::ACTION);
		if (access("/run/banana/sd2/slot", F_OK) == 0)	// a second card slot (bananad's sd2 script): the RG DS Plus
		add(S, "gear", "Second SD card", "Games on the card in the second slot (its roms folder, with the same system folders as this card) show up in the library with the others while it's in. exFAT or FAT32.", menu, "sd2_roms", { "Show its games", "Off" }, { "on", "off" });
		add(S, "gear", "First-time setup", "The steps from the first boot again: Wi-Fi, the time zone and the RetroAchievements account (ScreenScraper: not available yet).", "", "setup", {}, {}, Item::PAGE);
		add(S, "gear", "Reset system settings", "Puts the menu, display, sound, power and network settings back to their defaults, the time zone too (the language stays). Your games, saves and the emulators' settings are untouched. Press A twice.",
			"", "reset_system", { "" }, { "" }, Item::ACTION);
		add(S, "gear", "Reset emulator settings", "Puts the emulator choices back to their defaults: the DS emulator, Quick resume, the Game Boy Advance's emulator, and RetroArch's shader, audio latency, VSync and hotkeys. Your menu settings, saves and games stay. Press A twice.",
			"", "reset_emulator", { "" }, { "" }, Item::ACTION);
		add(S, "gear", "About", "", "", "", {}, {}, Item::INFO);
		add(S, "gear", "Restart", "Restarts the handheld. Press A twice.", "", "reboot", { "" }, { "" }, Item::ACTION);
		add(S, "gear", "Shut down", "Turns the handheld off. Press A twice. (Or hold the power button.)", "", "poweroff", { "" }, { "" }, Item::ACTION);
	}

	void Settings::load(Item& it)
	{
		if (it.key == "ds_language")
		{
			std::string v = getConfValue("/data/bananads/bananads.conf", "firmware.language", "");
			if (v.empty()) v = getConfValue("/data/drastic/config/drastic.cfg", "firmware.language",
				getConfValue("/usr/share/drastic/config/drastic.cfg", "firmware.language", "1"));
			for (size_t i = 0; i < it.values.size(); i++) if (it.values[i] == v) it.at = (int)i;
			return;
		}
		if (it.key == "theme")
		{
			// the themes as they are on the card now: one copied over file sharing while the menu runs is listed the
			// next time Settings opens (the list was made once, when banana-ui started)
			it.names = { "Dark", "Light", "Carts" }; it.values = { "dark", "light", "carts" };
			for (auto& t : userThemes()) { it.names.push_back(t.name); it.values.push_back(t.value); }
		}
		if (it.key == "DSFLIP_SHADER" || it.key == "kms.shader")
			shaderChoices(data, it.names, it.values);	// the card's shaders as they are now (one copied in while the menu runs)
		if (it.key == "accent")
		{
			// a theme with its own accent: the row says so and has nothing to pick (it came back to life when the theme
			// changes: the Theme row loads it again)
			static const std::vector<std::string> NAMES = { "Yellow", "Blue", "Red", "Green", "Purple", "Pink" },
				VALUES = { "yellow", "blue", "red", "green", "purple", "pink" };
			bool own = themeSetsAccent(getConfValue(data + "/settings.conf", "theme", "dark"));
			it.names = own ? std::vector<std::string>{ "From the theme" } : NAMES;
			it.values = own ? std::vector<std::string>{ getConfValue(it.file, "accent", "yellow") } : VALUES;
			it.help = own ? "This theme sets its own accent colour. Pick Dark, Light or a theme without one to choose it here."
				: "The colour of the selection, the logo and the boot animation's banana.";
		}
		if (it.kind == Item::BRIGHTNESS)
		{
			// this panel's level as set (bananad keeps "<top> <bottom>" in banana/backlight; the live one is lower while
			// the idle menu is dimmed, and the press that opened Settings undims it at the same moment), else its
			// backlight's now, in tenths
			int i = it.key == "bottom" ? 1 : 0, pm = backlightPair("/run/banana/dim", i);	// (dimmed now: the level before)
			if (pm <= 0) pm = backlightPair(data + "/backlight", i);
			if (pm <= 0)
			{
				std::string b = std::string("/sys/class/backlight/") + BACKLIGHT[i];
				int max = atoi(readLine(b + "/max_brightness").c_str()), cur = atoi(readLine(b + "/brightness").c_str());
				if (max > 0) pm = cur * 1000 / max;
			}
			it.at = pm <= 0 ? 9 : std::min(9, std::max(0, (pm + 50) / 100 - 1));
			return;
		}
		if (it.kind == Item::VOLUME)
		{
			// the playing card's level, from the volume script (bananad writes which card plays)
			int pct = -1;
			{
				FILE* p = popen("/usr/libexec/banana/volume get 2>/dev/null", "r");
				if (p)
				{
					char ln[64];
					if (fgets(ln, sizeof ln, p)) { int v; if (sscanf(ln, "%d", &v) == 1) pct = v; }
					pclose(p);
				}
			}
			it.at = pct < 0 ? 5 : std::min(10, std::max(0, (pct + 5) / 10));
			return;
		}
		if (it.kind != Item::CHOICE) return;
		// the schema's default (settings_defaults.cpp), else the row's first choice
		std::string def = settingDefault(it.key);
		if (def.empty()) def = it.values[0];
		// the system's defaults where the data partition has none yet
		if (it.file == data + "/bananad.conf") def = getConfValue("/etc/banana/bananad.conf", it.key, def);
		if (it.file == data + "/settings.conf") def = getConfValue("/etc/banana/settings.conf", it.key, def);
		if (it.key == "led_brightness") def = getConfValue("/etc/banana/bananad.conf", it.key, "128");
		if (it.file.find("retroarch.cfg") != std::string::npos) def = getConfValue("/etc/banana/retroarch.cfg", it.key, def);
		std::string v = getConfValue(it.file, it.key, def);
		it.at = -1;
		for (size_t i = 0; i < it.values.size(); i++)
			if (it.values[i] == v) it.at = (int)i;
		// a value the list doesn't have (set by hand, or a shader added later): shown as it is and kept, rather than
		// shown as the first choice and lost at the first press
		// (not the theme: a user theme whose file has gone shows Dark, which is what the menu draws then)
		// (not a DS shader that is nowhere: a card shader whose file has gone shows None, which is what the emulators draw then)
		bool goneShader = (it.key == "DSFLIP_SHADER" || it.key == "kms.shader") && access(("/usr/lib/superdrastic/shaders/" + v + ".frag").c_str(), F_OK) != 0;
		if (it.at < 0 && !v.empty() && it.key != "led_brightness" && it.key != "theme" && !goneShader) { it.names.push_back(v); it.values.push_back(v); it.at = (int)it.values.size() - 1; }
		if (it.at < 0) it.at = 0;
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
		if (space[0]) a << "Card: " << space << ". ";
		// the kernel too (#70): its version as uname gives it
		utsname un;
		if (uname(&un) == 0) a << "Linux " << un.release << ".";
		about = a.str();
	}

	int Settings::groupOf(int item) const
	{
		for (int g = 0; g < (int)groups.size(); g++) if (items[item].section == groups[g]) return g;
		return 0;
	}
	int Settings::groupStart(int g) const
	{
		for (int i = 0; i < (int)items.size(); i++) if (items[i].section == groups[g]) return i;
		return 0;
	}
	int Settings::groupEnd(int g) const
	{
		int i = groupStart(g);
		while (i < (int)items.size() && items[i].section == groups[g]) i++;
		return i;
	}
	std::string Settings::groupHint(int g) const
	{
		// the first few settings' names, as the in-game menu's tiles say what is behind them
		std::string h;
		int n = 0;
		for (int i = groupStart(g); i < groupEnd(g) && n < 2; i++, n++)
		{
			std::string l = lang::tr(items[i].label);	// (each name translated: the joined line has no translation)
			if (!lang::active()) for (auto& ch : l) ch = (char)std::tolower((unsigned char)ch);	// (English's style; "Wi-Fi" stays)
			h += (n ? ", " : "") + l;
		}
		return h;
	}

	void Settings::open()
	{
		if (groups.empty())
			for (auto& it : items)
				if (std::find(groups.begin(), groups.end(), it.section) == groups.end()) groups.push_back(it.section);
		inGroups = true;
		gsel = groupOf(sel);
		for (auto& it : items) load(it);
		refreshInfo();
		armed = -1;
		page = LIST;
		message.clear();
		if (wifiOn() && !statusJob.running()) statusJob.start(wifiTool + " status", sync);	// the network's name on its row
		btEnabled = btOn();
		if (btEnabled && !btListJob.running()) btListJob.start(btTool + " devices", sync);	// what is connected, on its row
		mOpen = true;
	}

	void Settings::save(const Item& it)
	{
		const std::string& v = it.values[it.at];
		if (it.key == "ds_language")
		{
			if (access("/data/drastic/config/drastic.cfg", F_OK) != 0)
			{
				if (system("mkdir -p /data/drastic/config && cp -n /usr/share/drastic/config/drastic.cfg /data/drastic/config/ 2>/dev/null") != 0) {}
			}
			setConfValue("/data/drastic/config/drastic.cfg", "firmware.language", v);
			if (system("mkdir -p /data/bananads") != 0) {}
			setConfValue("/data/bananads/bananads.conf", "firmware.language", v);
			return;
		}
		if (it.kind == Item::BRIGHTNESS)
		{
			int pm = atoi(v.c_str()), i = it.key == "bottom" ? 1 : 0;
			std::string b = std::string("/sys/class/backlight/") + BACKLIGHT[i];
			int max = atoi(readLine(b + "/max_brightness").c_str());
			if (max > 0) std::ofstream(b + "/brightness") << std::max(1, max * pm / 1000) << "\n";
			// both panels' levels kept for bananad to restore at boot: this one changed, the other as it was
			int other = backlightPair(data + "/backlight", 1 - i);
			if (other <= 0)
			{
				std::string o = std::string("/sys/class/backlight/") + BACKLIGHT[1 - i];
				int omax = atoi(readLine(o + "/max_brightness").c_str());
				other = omax > 0 ? atoi(readLine(o + "/brightness").c_str()) * 1000 / omax : pm;
			}
			std::ofstream(data + "/backlight") << (i == 0 ? pm : other) << " " << (i == 0 ? other : pm) << "\n";
			return;
		}
		if (it.kind == Item::VOLUME)
		{
			if (system(("/usr/libexec/banana/volume set " + std::to_string(atoi(v.c_str())) + " 2>/dev/null").c_str()) != 0) {}
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
			// Settings' Threaded 3D chosen now: a game's own value left from a run cut short mustn't come back over it
			if (it.key == "threaded_3d") unlink("/data/drastic/config/.threaded_3d.settings");
		}
		else
		{
			if (it.file.compare(0, 15, "/data/bananads/") == 0 && system("mkdir -p /data/bananads") != 0) {}	// before BananaDS's first run
			setConfValue(it.file, it.key, v);
		}
		if (it.file == data + "/bananad.conf") poke("bananad");
		if (it.key == "timezone") { setenv("TZ", v.c_str(), 1); tzset(); poke("bananad"); }	// bananad's logs too
		if (it.key == "wifi") spawn("/usr/libexec/banana/wifi", v == "on" ? "up" : "down");
		if (it.key == "bluetooth")
		{
			btEnabled = v == "on";
			if (!btEnabled) { btDevs.clear(); btScanned = false; }
			spawn(btTool.c_str(), btEnabled ? "up" : "down");
		}
		if (it.key == "ssh") spawn("/usr/libexec/banana/ssh", v == "on" ? "up" : "down");
		if (it.key == "share") spawn("/usr/libexec/banana/share", v == "on" ? "up" : "down");
		if (it.key == "share_login") spawn("/usr/libexec/banana/share", "restart");	// (nothing when it is off)
		if (it.key == "sd2_roms") spawn("/usr/libexec/banana/sd2", "scan");
	}

	Settings::Result Settings::change(int dir)
	{
		Item& it = items[sel];
		if (it.kind == Item::INFO) return NONE;
		if (it.kind == Item::PAGE)
		{
			if (dir <= 0) return NONE;
			if (it.key == "scrape") { startScrape(); return MOVED; }
			if (it.key == "colors") { openColors(); return MOVED; }
			if (it.key == "share_account") { startEntry(ENTRY_SMB_USER, "File sharing", "Name", LIST, shareUser()); return MOVED; }
			if (it.key == "ds_name") { startEntry(ENTRY_DS_NAME, "The DS's name", "10 characters at most", LIST, dsName()); return MOVED; }
			if (it.key == "bt_devices") { openBluetooth(); return MOVED; }
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
			if (it.key.compare(0, 10, "nds-reset-") == 0)
			{
				// nds-reset; then the list again, so the rows above show the settings as they are now
				armed = -1;
				FILE* p = popen(("/usr/libexec/banana/nds-reset " + it.key.substr(10) + " 2>&1").c_str(), "r");
				std::string out, key = it.key;
				int rc = -1;
				if (p) { char ln[256]; while (fgets(ln, sizeof ln, p)) out += ln; rc = pclose(p); }
				items.clear();
				build();
				for (auto& x : items) load(x);
				for (size_t i = 0; i < items.size(); i++) if (items[i].key == key) sel = (int)i;
				message = rc == 0 ? (key == "nds-reset-buttons" ? "DraStic's buttons are Banana's again." : "Back to Banana's settings.")
					: out.empty() ? "Couldn't reset: the card may be read-only." : out;
				return CHANGED;
			}
			if (it.key == "ra_reset")
			{
				armed = -1;
				FILE* p = popen("/usr/libexec/banana/ra-reset 2>&1", "r");
				std::string out;
				if (p) { char ln[256]; while (fgets(ln, sizeof ln, p)) out += ln; }
				bool ok = p && pclose(p) == 0;
				while (!out.empty() && out.back() == '\n') out.pop_back();
				message = ok ? "RetroArch is back to Banana's settings. The old ones are in " + out + " on the card." : out.empty() ? "RetroArch couldn't be reset." : out;
				for (auto& x : items) if (x.file.find("retroarch.cfg") != std::string::npos) load(x);
				return MOVED;
			}
			if (it.key == "reset_system" || it.key == "reset_emulator")
			{
				armed = -1;
				return resetApply(it.key == "reset_emulator");
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
		if (it.key == "nds_emulator")
		{
			// the other emulator's own settings in the list, the cursor staying on Emulator
			items.clear();
			build();
			for (auto& x : items) load(x);
			for (size_t i = 0; i < items.size(); i++) if (items[i].key == "nds_emulator") sel = (int)i;
			armed = -1;
			return CHANGED;
		}
		if (it.key == "language") return LANGUAGE;	// (saved above; the menu starts again in it)
		if (it.key == "theme")
		{
			setVariant(it.values[at]);
			for (auto& x : items) if (x.key == "accent") load(x);	// "From the theme", or the colours again
			return THEME;
		}
		if (it.key == "accent") { setVariant(getConfValue(data + "/settings.conf", "theme", "dark")); return THEME; }	// (saved above)
		return CHANGED;
	}

	Settings::Result Settings::resetApply(bool emulator)
	{
		const std::string menu = data + "/settings.conf";
		std::string beforeTheme = getConfValue(menu, "theme", "dark");
		std::string beforeAccent = getConfValue(menu, "accent", "yellow");

		resetSettings(data, emulator ? ResetScope::Emulator : ResetScope::System);

		if (!emulator)
		{
			// what the settings screen keeps outside settings.conf: bananad's sleep/LED rows (same defaults the rows
			// show when the card has no line), the backlights and the mixer's level
			static const struct { const char* key; const char* def; } BANANAD[] = {
				{ "autosleep_minutes", "10" }, { "dim_minutes", "2" }, { "lid_sleep", "yes" },
				{ "poweroff_after_sleep_minutes", "0" }, { "fast_resume", "yes" }, { "led_brightness", "128" } };
			for (const auto& b : BANANAD)
				setConfValue(data + "/bananad.conf", b.key, getConfValue("/etc/banana/bananad.conf", b.key, b.def));
			if (DIR* d = opendir("/sys/class/backlight"))
			{
				while (dirent* e = readdir(d))
				{
					if (e->d_name[0] == '.') continue;
					std::string b = std::string("/sys/class/backlight/") + e->d_name;
					int max = atoi(readLine(b + "/max_brightness").c_str());
					if (max > 0) std::ofstream(b + "/brightness") << max << "\n";
				}
				closedir(d);
			}
			std::ofstream(data + "/backlight") << "1000\n";
			if (system("/usr/libexec/banana/volume set 50 2>/dev/null") != 0) {}
			// the side effects save() runs for keys that live outside settings.conf
			std::map<std::string, std::string> rs = readSettings(data);
			if (!rs["timezone"].empty()) { setenv("TZ", rs["timezone"].c_str(), 1); tzset(); }
			spawn("/usr/libexec/banana/wifi", rs["wifi"] == "off" ? "down" : "up");
			spawn("/usr/libexec/banana/ssh", rs["ssh"] == "on" ? "up" : "down");
			spawn("/usr/libexec/banana/share", rs["share"] == "on" ? "up" : "down");
			spawn("/usr/libexec/banana/sd2", "scan");
			poke("bananad");
		}

		// the rows again: a reset emulator or theme changes which rows exist and what they show
		items.clear();
		build();
		for (auto& x : items) load(x);
		const std::string action = emulator ? "reset_emulator" : "reset_system";
		sel = 0;
		for (size_t i = 0; i < items.size(); i++) if (items[i].key == action) { sel = (int)i; break; }
		top = std::max(0, std::min(sel, std::max(0, groupEnd(groupOf(sel)) - ROWS)));
		armed = -1;

		toast(emulator ? "Emulator settings reset" : "System settings reset",
			emulator ? "The core choices and RetroArch's options are back to their defaults."
			         : "The menu, display, sound and power settings are back to their defaults.",
			0x6ad07a, 1, 5000);	// the RetroAchievements success green, a tick, 5 s

		std::string afterTheme = getConfValue(menu, "theme", "dark");
		std::string afterAccent = getConfValue(menu, "accent", "yellow");
		bool themeChanged = !emulator && (afterTheme != beforeTheme || afterAccent != beforeAccent);
		if (themeChanged) setVariant(afterTheme);	// this screen's own palette now (the menu's is the THEME result)
		return themeChanged ? THEME : CHANGED;
	}

	Settings::Result Settings::press(Key k)
	{
		if (page == NETWORKS) return pressNetworks(k);
		if (page == BLUETOOTH) return pressBluetooth(k);
		if (page == KEYBOARD) return pressKeyboard(k);
		if (page == GAME) return pressGame(k);
		if (page == ARTPICK) return pressArt(k);
		if (page == COLORS) return pressColors(k);
		if (page == UPDATES) return pressUpdates(k);
		if (page == SETUP) return pressSetup(k);
		if (inGroups) return pressGroups(k);
		int g = groupOf(sel), b = groupStart(g), e = groupEnd(g);
		switch (k)
		{
		case Key::UP: case Key::DOWN:
		{
			// round within the group's own list
			int n = e - b;
			sel = b + (sel - b + (k == Key::UP ? -1 : 1) + n) % n;
			armed = -1;
			if (sel < top) top = sel;
			if (sel >= top + ROWS) top = sel - ROWS + 1;
			if (items[sel].kind == Item::INFO) refreshInfo();
			return MOVED;
		}
		case Key::L: case Key::R:
		{
			// the previous / next group's list
			int n = (int)groups.size();
			gsel = (g + (k == Key::R ? 1 : -1) + n) % n;
			sel = top = groupStart(gsel);
			armed = -1;
			if (items[sel].kind == Item::INFO) refreshInfo();
			return MOVED;
		}
		case Key::LEFT: return change(-1);
		case Key::RIGHT: case Key::A: return change(1);
		case Key::B: inGroups = true; gsel = g; armed = -1; return MOVED;	// back to the groups
		case Key::START: mOpen = false; return CLOSED;
		default: return NONE;
		}
	}

	Settings::Result Settings::pressGroups(Key k)
	{
		int n = (int)groups.size();
		switch (k)
		{
		// a grid of three columns, as the in-game menu's main page
		case Key::LEFT: gsel = (gsel + n - 1) % n; return MOVED;
		case Key::RIGHT: gsel = (gsel + 1) % n; return MOVED;
		case Key::UP: case Key::DOWN: gsel = (gsel + (k == Key::UP ? n - 3 : 3)) % n; return MOVED;
		case Key::L: gsel = (gsel + n - 1) % n; return MOVED;
		case Key::R: gsel = (gsel + 1) % n; return MOVED;
		case Key::A:
			inGroups = false;
			sel = top = groupStart(gsel);
			armed = -1;
			if (items[sel].kind == Item::INFO) refreshInfo();
			return MOVED;
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
		if (page == BLUETOOTH) return touchBluetooth(cx, cy);
		if (page == KEYBOARD) return touchKeyboard(cx, cy);
		if (page == GAME) return touchGame(cx, cy);
		if (page == ARTPICK) return touchArt(cx, cy);
		if (page == COLORS) return touchColors(cx, cy);
		if (page == SETUP)
		{
			if (downY >= 418 && downY <= 466) return pressSetup(downX < 320 ? Key::B : Key::A);
			for (int r = 0; r < SETUP_STEPS; r++)
			{
				float ry = 82 + r * SETUP_PITCH;
				if (downY < ry || downY > ry + 50) continue;
				if (r != wsel) { wsel = r; return MOVED; }
				return pressSetup(Key::A);
			}
			return NONE;
		}
		if (page == UPDATES)
		{
			if (downY >= 418 && downY <= 466) return pressUpdates(downX < 320 ? Key::B : Key::A);
			for (int r = 0; r < ROWS && utop + r < 1 + (int)pkgs.size(); r++)
			{
				float ry = 82 + r * 64;
				if (downY < ry || downY > ry + 56) continue;
				if (utop + r != usel) { usel = utop + r; armed = -1; return MOVED; }
				return pressUpdates(Key::A);
			}
			return NONE;
		}
		if (inGroups)
		{
			if (downY >= 418 && downY <= 466) return pressGroups(downX < 320 ? Key::B : Key::A);
			for (int g = 0; g < (int)groups.size(); g++)
			{
				float x = 18 + (g % 3) * 204, y = 82 + (g / 3) * 166;
				if (downX < x || downX > x + 196 || downY < y || downY > y + 158) continue;
				if (g != gsel) { gsel = g; return MOVED; }
				return pressGroups(Key::A);
			}
			return NONE;
		}
		int gEnd = groupEnd(groupOf(sel));
		// the chips: B on the left (back to the groups), A on the right
		if (downY >= 418 && downY <= 466)
		{
			if (downX < 320) return press(Key::B);
			return change(1);
		}
		for (int r = 0; r < ROWS && top + r < gEnd; r++)
		{
			float ry = 82 + r * 64;
			if (downY < ry || downY > ry + 56) continue;
			int i = top + r;
			if (i != sel) { sel = i; armed = -1; return MOVED; }
			// the selected row: its value's left half steps back, the rest forward
			return change(downX < 400 && items[i].kind != Item::ACTION ? -1 : 1);
		}
		// a swipe up or down scrolls (within the group)
		if (std::fabs(cy - downY) > 40)
		{
			int gb = groupStart(groupOf(sel));
			top = std::max(gb, std::min(std::max(gb, gEnd - ROWS), top + (cy < downY ? ROWS : -ROWS)));
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
			else if (kind == "pick")	// a list's selected row ("slot" otherwise)
			{
				st.radius = X(14); st.border = b3; st.borderColor = p.pickEdge; st.bg = p.pick;
				st.bands = { rnds::Band{ 0, X(8), p.pickBand }, rnds::Band{ X(8), -1, p.pick } };
				st.shadows = { inset(0, -X(6), 0, p.pickLow), outer(X(3), X(3), p.shade45) };
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
			else if (kind == "osd")	// ROCKNIXDS's pop-up card (SuperDrastic's ui.c): a light border, dark and dotted inside
			{
				st.radius = X(10); st.border = std::max(2.0f, std::round(2 * s)); st.borderColor = Color::hex(0xf1ede2);
				st.bg = Color::hex(0x211f26); st.dither = true;
				p.ditherA = Color(1, 1, 1, 0.10f); p.ditherB = Color(1, 1, 1, 0.05f);
			}
			else if (kind == "kbd") { st.radius = X(4); st.bg = p.kbdBg; }
			st.ditherScale = s; st.ditherA = p.ditherA; st.ditherB = p.ditherB;
			if (cleanFonts)
			{
				// the Carts theme: flat panels, a hairline edge, no dither, bands or hard shadows
				rnds::BoxStyle c;
				float hair = std::max(1.0f, std::round(s));
				if (kind == "screen") { c.bg = Color::hex(0x111215); }
				else if (kind == "pick") { c.radius = X(12); c.bg = p.pick; }
				else if (kind == "kbd") { c.radius = X(6); c.bg = Color::hex(0xe6e7ea); }	// (a key: light, its letter dark)
				else
				{
					c.radius = X(kind == "hero" ? 16 : kind == "face" ? 10 : 12);
					c.bg = Color::hex(kind == "hero" ? 0x191a1f : 0x1f2126);
					c.border = hair;
					c.borderColor = on ? p.pick : Color(1, 1, 1, kind == "osd" ? 0.5f : 0.08f);
				}
				st = c;
			}
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

	// an icon centred on cx (its own width: the icons aren't all the same size)
	void Settings::iconCentred(const std::string& name, float cx, float y, int scale)
	{
		icon(name, -10000, -10000, scale);	// (loads it into the cache; off screen)
		auto it = cache.find("icon:" + name);
		if (it == cache.end() || !it->second.tex) return;
		int k = std::max(1, (int)std::lround(scale * s));
		float w = (float)it->second.tex->w * k;	// device px
		be->draw(it->second.tex, std::round(X(cx) - w / 2), std::round(X(y)), w, (float)it->second.tex->h * k, Color(1, 1, 1, 1));
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

	void Settings::level(const std::string& label, int percent)
	{
		// ROCKNIXDS's volume indicator (SuperDrastic's ui.c render_volume), in the same place: a pill at the top of the
		// top panel, a speaker (crossed out at 0) or a sun, the in-game menu's 10-segment bar and the percent
		static const char* const VOL[12] = { "............", ".....#......", "....##...#..", "...###....#.", "####.#..#..#", "#..#.#...#.#",
			"#..#.#...#.#", "####.#..#..#", "...###....#.", "....##...#..", ".....#......", "............" };
		static const char* const MUTE[12] = { "............", ".....#......", "....##......", "...###.#...#", "####.#..#.#.", "#..#.#...#..",
			"#..#.#...#..", "####.#..#.#.", "...###.#...#", "....##......", ".....#......", "............" };
		static const char* const SUN[12] = { ".....##.....", ".#...##...#.", "..#......#..", "....####....", "...######...", "##.######.##",
			"##.######.##", "...######...", "....####....", "..#......#..", ".#...##...#.", ".....##....." };
		percent = std::min(100, std::max(0, percent));
		bool sun = label == "Brightness";
		const Color light = Color::hex(0xf1ede2), bar = Color::hex(0xffe68a), off = Color::hex(0x3a3540), red = Color::hex(0xff8a96);
		const float w = 310, h = 42, x0 = (640 - w) / 2, y0 = 4, x1 = x0 + w, cy = y0 + h / 2;
		box("osd", false, x0, y0, w, h);
		int k = std::max(2, (int)std::lround(2 * s));	// device px per icon pixel
		const char* const* ic = sun ? SUN : percent <= 0 ? MUTE : VOL;
		float ix = std::round(X(x0 + 14)), iy = std::round(X(cy)) - 6 * k;
		for (int r = 0; r < 12; r++)
			for (int c = 0; c < 12; c++)
				if (ic[r][c] == '#') be->fill(ix + c * k, iy + r * k, (float)k, (float)k, !sun && percent <= 0 ? red : light);
		char t[8];
		snprintf(t, sizeof t, "%d%%", percent);
		float tw = text(t, true, 19, 0, 0, bar, false, 0, true);
		text(t, true, 19, x1 - 14 - tw, y0 + 11, bar);
		float bx0 = x0 + 14 + 12.0f * k / s + 12, bx1 = x1 - 14 - text("100%", true, 19, 0, 0, bar, false, 0, true) - 10;
		float seg = (bx1 - bx0) / 10, gap = std::max(2.0f, std::round(3 * s)) / s;
		for (int i = 0; i < 10; i++)
		{
			float sx = std::round(X(bx0 + i * seg)), ex = std::round(X(bx0 + (i + 1) * seg - gap));
			be->fill(sx, std::round(X(cy - 8)), ex - sx, std::round(X(16)), i < (percent + 5) / 10 ? bar : off);
		}
	}

	// the groups: a grid of tiles in the in-game menu's look (its main page), the group's icon, name and what is in it;
	// the top panel says what the selected group holds
	static double steadyMs()
	{
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	bool Settings::animating() const
	{
		if (!mOpen || page != LIST) return false;
		if (inGroups) return aCol != gsel % 3 || aRow != gsel / 3;
		return aPick != sel || aTop != top;
	}

	void Settings::advance()
	{
		double t = steadyMs();
		double dt = aMoving ? std::min(50.0, t - aLast) : 1000.0 / 60;	// (a first step from rest: one refresh)
		aLast = t;
		float k = 1 - (float)std::exp(-dt / 55.0);
		auto ease = [&](float& v, float to) { if (v < -0.5f) v = to; v += (to - v) * k; if (std::fabs(to - v) < 0.01f) v = to; };
		if (inGroups) { ease(aCol, (float)(gsel % 3)); ease(aRow, (float)(gsel / 3)); }
		else
		{
			int g = groupOf(sel);
			if (g != aGroup) { aGroup = g; aPick = (float)sel; aTop = (float)top; }	// another group: no slide across
			ease(aPick, (float)sel); ease(aTop, (float)top);
		}
		aMoving = animating();
	}

	void Settings::renderGroups()
	{
		SPal p = pal(light);
		int g0 = std::max(0, std::min(gsel, (int)groups.size() - 1));
		const Item& first = items[groupStart(g0)];

		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon(first.icon, 50, 56, 3);
		text("Settings", true, 17, 170, 62, p.muted, true, 1.4f);
		text(groups[g0], true, 30, 170, 88, p.ink);
		char n[32];
		snprintf(n, sizeof n, "%d settings", groupEnd(g0) - groupStart(g0));
		text(n, true, 22, 170, 130, p.accent);
		std::string all;
		for (int i = groupStart(g0); i < groupEnd(g0); i++) all += (all.empty() ? "" : ", ") + lang::tr(items[i].label);
		wrapped(all + ".", 20, 50, 196, 540, 7, p.muted);
		be->endScreen();

		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("bubble", false, 18, 14, 604, 54);
		text("Settings", true, 22, 38, 28, p.ink);
		const char* sub = "Choose a group";
		float sw = text(sub, true, 17, 0, 0, p.muted, false, 0, true);
		text(sub, true, 17, 602 - sw, 31, p.muted);
		advance();
		aGroup = -1;	// (the list starts still when a group opens)
		for (int g = 0; g < (int)groups.size() && g < 6; g++) box("slot", false, 18 + (g % 3) * 204, 82 + (g / 3) * 166, 196, 158);
		box("pick", true, 18 + aCol * 204, 82 + aRow * 166, 196, 158);
		for (int g = 0; g < (int)groups.size() && g < 6; g++)
		{
			bool on = std::fabs(g % 3 - aCol) < 0.5f && std::fabs(g / 3 - aRow) < 0.5f;
			float x = 18 + (g % 3) * 204, y = 82 + (g / 3) * 166;
			iconCentred(items[groupStart(g)].icon, x + 98, y + 22, 2);
			std::string label = fit(groups[g], true, 19, 180);
			float lw = text(label, true, 19, 0, 0, p.ink, false, 0, true);
			text(label, true, 19, x + 98 - lw / 2, y + 84, on ? p.onPick : p.ink);
			std::string hint = fit(groupHint(g), false, 14, 176);
			float hw = text(hint, false, 14, 0, 0, p.muted, false, 0, true);
			text(hint, false, 14, x + 98 - hw / 2, y + 114, on ? p.onPickAccent : p.muted);
		}
		float bw = faceWidth("B", "Close");
		face("B", "Close", 18, bw);
		float aw = faceWidth("A", "Open");
		face("A", "Open", 622 - aw, aw);
		be->endScreen();
	}

	void Settings::render()
	{
		if (page == NETWORKS) { renderNetworks(); return; }
		if (page == BLUETOOTH) { renderBluetooth(); return; }
		if (page == KEYBOARD) { renderKeyboard(); return; }
		if (page == GAME) { renderGame(); return; }
		if (page == ARTPICK) { renderArt(); return; }
		if (page == COLORS) { renderColors(); return; }
		if (page == UPDATES) { renderUpdates(); return; }
		if (page == SETUP) { renderSetup(); return; }
		if (inGroups) { renderGroups(); return; }
		SPal p = pal(light);
		const Item& cur = items[sel];
		int gb = groupStart(groupOf(sel)), ge = groupEnd(groupOf(sel));

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
		if ((cur.key == "switch" || cur.key == "ra_reset" || cur.key.compare(0, 10, "nds-reset-") == 0) && !message.empty()) help = message;
		if (cur.key == "colors")
		{
			ScreenColors c = ScreenColors::load(data + "/screen-colors.conf");
			help = std::string("The two screens rarely show the same white. This corrects one of them towards the other: the same test pattern on both, "
				"and red, green, blue, gamma, brightness and black for the one corrected (the handheld has one colour table, so one screen). Now: ") +
				(c.neutral() ? "no correction." : c.screen == 0 ? "the top screen corrected." : "the bottom screen corrected.");
		}
		if (cur.key == "share_account")
			help = "What a computer asks for when it opens the share: name " + shareUser() + ", password " + sharePassword() + ". A changes them (they are kept in banana/accounts.conf, which the share never shows).";
		if (cur.key == "share")
		{
			std::string at = ip.empty() ? "<address>" : ip;
			help = "The card on the network: \\\\" + at + "\\banana in Windows' File Explorer, smb://" + at +
				"/banana on a Mac (Finder, Go, Connect to Server). " +
				(getConfValue(data + "/settings.conf", "share_login", "password") == "guest" ? std::string("Anyone on the network can open it with no password (Sharing login): leave it off on networks you don't trust.")
					: "Name " + shareUser() + ", password " + sharePassword() + " (Sharing name and password, below).");
			if (ip.empty()) help += " (Not connected to Wi-Fi now.)";
		}
		if (cur.key == "scrape" && scraping())
		{
			wrapped("Fetching covers and title screens. The menu shows them as they come, and it carries on if you leave this screen.", 20, 50, 196, 540, 4, p.muted);
			scrapeBar(50, 330, 540);
		}
		else wrapped(help, 20, 50, 196, 540, 7, p.muted);
		be->endScreen();

		// ---- bottom: the list ----
		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("bubble", false, 18, 14, 604, 54);
		text(cur.section, true, 22, 38, 28, p.ink);
		char count[32];
		snprintf(count, sizeof count, "%02d / %02d", sel - gb + 1, ge - gb);
		float cw = text(count, true, 17, 0, 0, p.muted, false, 0, true);
		text(count, true, 17, 602 - cw, 31, p.muted);
		// the rows, the highlight and the scroll where their easing has them; rows clipped to the list's band, so one
		// scrolling in slides from under the header or the buttons
		advance();
		be->pushClip(0, X(76), X(640), X(ROWS * 64 + 4));
		int r0 = std::max(gb, (int)std::floor(aTop)), r1 = std::min(ge, (int)std::ceil(aTop) + ROWS);
		for (int i = r0; i < r1; i++) box("slot", false, 18, 82 + (i - aTop) * 64, 604, 56);
		box("pick", true, 18, 82 + (aPick - aTop) * 64, 604, 56);
		for (int i = r0; i < r1; i++)
		{
			const Item& it = items[i];
			bool on = std::fabs(i - aPick) < 0.5f;
			float y = 82 + (i - aTop) * 64;
			text(it.label, true, 20, 38, y + 16, on ? p.onPick : p.muted);
			std::string v;
			if (it.kind == Item::CHOICE || it.kind == Item::BRIGHTNESS || it.kind == Item::VOLUME) v = it.names[it.at];
			else if (it.kind == Item::ACTION) v = armed == i ? "Press A again" : "";
			else if (it.kind == Item::INFO) v = "";
			// a page's row: Network the network it is on; Update the packages waiting on the card; the others nothing
			else if (it.kind == Item::PAGE && it.key == "network") v = wstat.connected ? fit(wstat.ssid, true, 19, 300) : (wifiOn() ? "Choose" : "");
			else if (it.kind == Item::PAGE && it.key == "bt_devices") v = btRow();
			else if (it.kind == Item::PAGE && it.key == "update") v = updatesWaiting(updatesDir, runDir, version);
			else if (it.kind == Item::PAGE && it.key == "share_account") v = fit(shareUser(), true, 19, 300);
			else if (it.kind == Item::PAGE && it.key == "ds_name") v = dsName().empty() ? "Not set" : dsName();
			else if (it.kind == Item::PAGE && it.key == "scrape") v = scrapeShort();
			if (v.empty()) continue;
			float vw = text(v, true, 19, 0, 0, p.ink, false, 0, true);
			Color vc = on ? (armed == i ? p.onPickAccent : p.onPick) : p.muted;
			if (it.kind == Item::ACTION || it.kind == Item::PAGE) { text(v, true, 19, 586 - vw, y + 17, vc); continue; }
			arrow(true, 576, y + 21, vc);
			text(v, true, 19, 566 - vw, y + 17, vc);
			arrow(false, 556 - vw - 2 - std::round(13 / 2.0f + 1), y + 21, vc);
		}
		be->popClip();
		float bw = faceWidth("B", "Back");
		face("B", "Back", 18, bw);
		std::string act = cur.kind == Item::ACTION ? (armed == sel ? "Confirm" : cur.label) : cur.kind == Item::INFO ? "" : cur.key == "scrape" ? (scraping() ? "" : "Start") : cur.kind == Item::PAGE ? "Open" : "Change";
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
		bool scrapeChanged = pollScrape(t);
		if (raCheck)
		{
			std::lock_guard<std::mutex> l(raCheck->mu);
			if (raCheck->done)
			{
				const int kind = raCheck->kind;
				toast(kind == 1 ? "RetroAchievements" : "RetroAchievements login failed", raCheck->l2,
					kind == 1 ? 0x6ad07a : 0xff7a4a, kind == 1 ? 1 : 2, kind == 1 ? 5000 : 7000);
				message = kind == 1 ? "" : raCheck->l2;
				raCheck.reset();
				return true;
			}
		}
		if (page == NETWORKS && wifiEnabled)
		{
			if (!joining.empty() && now >= nextStatus) pollStatus();
			if (joining.empty() && now >= nextScan) startScan();
		}
		bool changed = scrapeChanged;
		changed |= btUpdate();
		if (page == UPDATES && applying && now >= nextStatus2)
		{
			std::string before = ustatus;
			pollUpdate();
			changed |= ustatus != before;
		}
		if (page == UPDATES && downloading && now >= nextDl)
		{
			std::string before = dlLine;
			pollDownload();
			changed |= dlLine != before || !downloading;
		}
		changed |= checkDone();
		std::string out;
		if (scanJob.take(out))
		{
			nets = wifi::parseScan(out);
			scanning = false;
			scannedOnce = true;
			markNetworks();
			changed = true;
		}
		if (artJob.take(out))
		{
			// the search's names, or what the fetch said
			std::vector<std::string> names;
			std::istringstream in(out);
			std::string err;
			for (std::string ln; std::getline(in, ln);)
			{
				if (ln.compare(0, 5, "NAME\t") == 0) names.push_back(ln.substr(5));
				else if (ln.compare(0, 6, "ERROR ") == 0) err = ln.substr(6);
				else if (ln.compare(0, 2, "OK") == 0) err = "ok";
			}
			if (artMatching)
			{
				artMatching = false;
				if (err == "ok") { message.clear(); toast("Box art", artNames.empty() ? "" : artNames[asel], 0x6ad07a, 1, 4000); page = GAME; pgArtDone = true; }
				else message = err.empty() ? "The pictures didn't come: no connection?" : "Not fetched: " + err + ".";
			}
			else
			{
				artNames = names;
				message = !err.empty() ? (err == "no connection" ? "No connection: is Wi-Fi connected?" : err + ".") :
					names.empty() ? "Nothing in libretro's collection for \"" + artWords + "\". Fewer words, or another spelling (B, then A to type again)." : "";
			}
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
			bool assoc = wstat.connected && wstat.ssid == joining;
			message = assoc ? "Joined " + joining + ", but it gave the handheld no address."
				: "Couldn't join " + joining + ". If the password was wrong, A on it types it again.";
			if (!assoc) failed = joining;	// the next A on it opens the keyboard, saved or not
			joining.clear();
			changed = true;
		}
		return changed;
	}

	int Settings::nextWake(double t) const
	{
		if (!mOpen) return -1;
		int w = pageWake(t);
		// the account check's answer, and the pop-up going away
		int mine = raCheck ? 200 : toastUntil > t ? std::max(0, (int)(toastUntil - t) + 1) : -1;
		if (mine >= 0 && (w < 0 || mine < w)) w = mine;
		if (scraping() && (w < 0 || w > 500)) w = std::max(0, (int)(scrapeNext - t));	// the box art's progress
		return w;
	}

	int Settings::pageWake(double t) const
	{
		if (scanJob.running() || statusJob.running() || artJob.running() || checkJob.running()) return 100;
		if (btListJob.running() || btScanJob.running() || btActJob.running()) return 100;
		if (page == UPDATES && applying) return std::max(0, (int)(nextStatus2 - t));
		if (page == UPDATES && downloading) return std::max(0, (int)(nextDl - t));
		if (page == BLUETOOTH && btEnabled) return std::max(0, (int)(btNextList - t));
		if (page != NETWORKS || !wifiEnabled) return -1;
		double due = joining.empty() ? nextScan : std::min(nextStatus, joinStart + 30001);
		return std::max(0, (int)(due - t));
	}

	void Settings::join(const wifi::Network& n, const std::string& password)
	{
		page = NETWORKS;
		// a saved network with no password typed: its block in wifi.conf as it is, moved first
		bool ok = n.secured && password.empty() ? wifi::prefer(wifiConf, n.ssid) : wifi::remember(wifiConf, n, password);
		if (!ok) { message = "Couldn't write banana/wifi.conf on the card."; return; }
		failed.clear();
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
			else if (!net.secured || (net.saved && net.ssid != failed))
				join(net, "");	// open, or saved: no password to type
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
	// the bottom panel: the text being typed in a field at the top, the keys under it, the button chips at the bottom
	static const float KX = 18, KPITCH = 60.4f, KY = 80, KROW = 66, KH = 58;

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
		case 3: popChar(pw); break;
		case 4: return finishEntry();
		}
		message.clear();
		return MOVED;
	}

	// the last character off a text, a whole UTF-8 sequence (a scraped title's é is two bytes: one alone left the rest
	// as invalid text in game-titles.tsv and the menu)
	void Settings::popChar(std::string& t)
	{
		while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back();
		if (!t.empty()) t.pop_back();
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
			if (pw.empty() && entry == ENTRY_SEARCH) { close(); return CLOSED; }
			if (pw.empty()) { page = entryBack; message.clear(); return MOVED; }
			popChar(pw);
			return MOVED;
		case Key::X: layer = layer == 1 ? 0 : 1; return MOVED;
		case Key::L: layer = layer == 2 ? 0 : 2; return MOVED;	// the symbols
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
		Color dim(p.muted.r, p.muted.g, p.muted.b, 0.35f), dimOn(0.09f, 0.09f, 0.11f, 0.3f);
		for (int r = 0; r < ROWS && ntop + r < (int)nets.size(); r++)
		{
			const wifi::Network& n = nets[ntop + r];
			bool on = ntop + r == nsel;
			float y = 82 + r * 64;
			box(on ? "pick" : "slot", on, 18, y, 604, 56);
			Color c = on ? p.onPick : p.muted;
			float right = 572;
			bars(n.bars(), right, y + 40, c, on ? dimOn : dim);
			right -= 10;
			if (n.secured || n.unsupported) { right -= 12; lock(right, y + 19, c); right -= 12; }
			std::string tag = n.current ? "Connected" : n.ssid == joining ? "Joining" : n.saved ? "Saved" : "";
			if (!tag.empty())
			{
				float tw = text(tag, true, 17, 0, 0, c, false, 0, true);
				right -= tw;
				text(tag, true, 17, right, y + 18, n.current ? (on ? p.onPickAccent : p.accent) : c);
				right -= 14;
			}
			text(fit(n.ssid, true, 20, right - 38), true, 20, 38, y + 16, c);
		}
		if (nets.empty() && wifiEnabled && !scanning && scannedOnce) text("No networks found. X searches again.", true, 20, 38, 98, p.muted);
		float bw = faceWidth("B", "Back");
		face("B", "Back", 18, bw);
		std::string act = !cur || cur->current || cur->unsupported || !joining.empty() ? "" : cur->secured && (!cur->saved || cur->ssid == failed) ? "Password" : "Join";
		if (!act.empty())
		{
			float aw = faceWidth("A", act);
			face("A", act, 622 - aw, aw);
		}
		be->endScreen();
	}

	// ---- Bluetooth: the devices, pairing and connecting (the bluetooth script) -------------------------------------
	bool Settings::btOn() const
	{
		return getConfValue(data + "/settings.conf", "bluetooth", getConfValue("/etc/banana/settings.conf", "bluetooth", "off")) == "on";
	}

	// switched on from the devices page (X): kept as the Bluetooth row's would be (the scan's command brings it up)
	void Settings::btSetOn(bool on)
	{
		setConfValue(data + "/settings.conf", "bluetooth", on ? "on" : "off");
		for (auto& it : items) if (it.key == "bluetooth") load(it);
		btEnabled = on;
	}

	std::string Settings::btRow() const
	{
		std::string names;
		for (auto& d : btDevs) if (d.connected) names += (names.empty() ? "" : ", ") + d.name;
		if (!names.empty()) return names;
		return btEnabled ? "Choose" : "";
	}

	void Settings::openBluetooth()
	{
		page = BLUETOOTH;
		bsel = btop = 0;
		message.clear();
		btEnabled = btOn();
		if (!btEnabled) { btDevs.clear(); return; }
		btScan();	// the paired ones and the ones around, as the Wi-Fi page searches at once
	}

	void Settings::btScan()
	{
		if (btScanJob.running()) return;
		btScanning = true;
		// (up first: switched on a moment ago, bluetoothd may not be running yet; up waits for it, or does nothing)
		btScanJob.start(btTool + " up >/dev/null 2>&1; " + btTool + " scan", sync);
		btNextList = now + 3000;
	}

	void Settings::btAct(const char* verb, const bt::Device& d)
	{
		if (btActJob.running()) return;
		btBusy = d.mac;
		btBusyName = d.name;
		btVerb = verb;
		const std::string v = verb;
		message = v == "pair" ? "Pairing with " + d.name + "..." : v == "connect" ? "Connecting to " + d.name + "..."
			: v == "disconnect" ? "Disconnecting " + d.name + "..." : "Forgetting " + d.name + "...";
		btActJob.start(btTool + " " + v + " " + d.mac, sync);	// (the address checked by the parser: no quoting needed)
	}

	bool Settings::btUpdate()
	{
		bool changed = false;
		std::string out;
		// the cursor stays on the device it was on
		auto take = [&](const std::string& o) {
			std::string selMac = bsel < (int)btDevs.size() ? btDevs[bsel].mac : "";
			btDevs = bt::parseDevices(o);
			for (int i = 0; i < (int)btDevs.size(); i++) if (btDevs[i].mac == selMac) bsel = i;
			bsel = std::max(0, std::min(bsel, (int)btDevs.size() - 1));
			if (bsel < btop) btop = bsel;
			if (bsel >= btop + ROWS) btop = bsel - ROWS + 1;
			btop = std::max(0, std::min(btop, std::max(0, (int)btDevs.size() - ROWS)));
			changed = true;
		};
		if (btScanJob.take(out))
		{
			std::string why;
			if (!bt::parseResult(out, why) && !why.empty()) message = "Bluetooth didn't come on: " + why + ".";
			else take(out);
			btScanning = false;
			btScanned = true;
			changed = true;
		}
		if (btListJob.take(out) && btEnabled) take(out);
		if (btActJob.take(out))
		{
			std::string why;
			bool ok = bt::parseResult(out, why);
			const std::string& n = btBusyName;
			const std::string tail = why.empty() ? "." : ": " + why + ".";
			if (btVerb == "pair")
				message = ok ? "Paired with " + n + "."
					: "Couldn't pair with " + n + tail + " Put it in pairing mode again (its manual says how), then A.";
			else if (btVerb == "connect")
				message = ok ? "Connected to " + n + "." : "Couldn't connect to " + n + tail + " Is it on and near the handheld?";
			else if (btVerb == "disconnect") message = ok ? n + " disconnected." : "Couldn't disconnect " + n + tail;
			else message = ok ? n + " forgotten: pair with it again to use it." : "Couldn't forget " + n + tail;
			btBusy.clear();
			if (!btListJob.running()) btListJob.start(btTool + " devices", sync);	// connected, paired, gone: the list again
			btNextList = now + 3000;
			changed = true;
		}
		// the page open: the list again every 3 s (a controller calling back, headphones switched off)
		if (page == BLUETOOTH && btEnabled && now >= btNextList && !btListJob.running() && !btScanJob.running() && !btActJob.running())
		{
			btListJob.start(btTool + " devices", sync);
			btNextList = now + 3000;
		}
		return changed;
	}

	Settings::Result Settings::pressBluetooth(Key k)
	{
		int n = (int)btDevs.size();
		switch (k)
		{
		case Key::UP: case Key::DOWN:
			if (!n) return NONE;
			bsel = (bsel + (k == Key::UP ? -1 : 1) + n) % n;
			if (bsel < btop) btop = bsel;
			if (bsel >= btop + ROWS) btop = bsel - ROWS + 1;
			return MOVED;
		case Key::A: case Key::RIGHT:
		{
			if (!n || !btBusy.empty() || !btEnabled) return NONE;
			const bt::Device& d = btDevs[bsel];
			btAct(!d.paired ? "pair" : d.connected ? "disconnect" : "connect", d);
			return MOVED;
		}
		case Key::X:
			message.clear();
			if (!btEnabled) btSetOn(true);
			btScan();
			return MOVED;
		case Key::Y:
			if (!n || !btBusy.empty() || !btDevs[bsel].paired) return NONE;
			btAct("forget", btDevs[bsel]);
			return MOVED;
		case Key::B: case Key::LEFT: page = LIST; message.clear(); return MOVED;
		case Key::START: page = LIST; mOpen = false; return CLOSED;
		default: return NONE;
		}
	}

	Settings::Result Settings::touchBluetooth(float cx, float cy)
	{
		if (downY >= 418 && downY <= 466) return pressBluetooth(downX < 320 ? Key::B : Key::A);
		for (int r = 0; r < ROWS && btop + r < (int)btDevs.size(); r++)
		{
			float ry = 82 + r * 64;
			if (downY < ry || downY > ry + 56) continue;
			if (btop + r != bsel) { bsel = btop + r; return MOVED; }
			return pressBluetooth(Key::A);
		}
		if (std::fabs(cy - downY) > 40 && !btDevs.empty())
		{
			int n = (int)btDevs.size();
			btop = std::max(0, std::min(n - ROWS, btop + (cy < downY ? ROWS : -ROWS)));
			bsel = std::max(btop, std::min(bsel, btop + ROWS - 1));
			return MOVED;
		}
		return NONE;
	}

	void Settings::renderBluetooth()
	{
		SPal p = pal(light);
		const bt::Device* cur = bsel < (int)btDevs.size() ? &btDevs[bsel] : nullptr;
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon("phone", 50, 56, 3);
		text("Network", true, 17, 170, 62, p.muted, true, 1.4f);
		text("Bluetooth devices", true, 30, 170, 88, p.ink);
		std::string connected;
		for (auto& d : btDevs) if (d.connected) { connected = d.name; break; }
		std::string st = !btEnabled ? "Bluetooth is off"
			: !btBusy.empty() ? (btVerb == "pair" ? "Pairing with " : btVerb == "connect" ? "Connecting to " : btVerb == "disconnect" ? "Disconnecting " : "Forgetting ") + btBusyName
			: !connected.empty() ? "Connected to " + connected : "Nothing connected";
		text(fit(st, true, 22, 420), true, 22, 170, 130, p.accent);
		std::string help = message;
		if (help.empty())
			help = !btEnabled ? "Bluetooth is switched off. X switches it on and searches."
				: "A pairs with the device chosen (put it in pairing mode first), connects a paired one, or disconnects it. X searches again, Y forgets a paired device. Headphones play the sound while they are connected; controllers work in the games.";
		wrapped(help, 20, 50, 196, 540, 7, p.muted);
		be->endScreen();

		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("bubble", false, 18, 14, 604, 54);
		text("Devices", true, 22, 38, 28, p.ink);
		std::string count = btScanning ? "Searching..." : std::to_string(btDevs.size()) + (btDevs.size() == 1 ? " device" : " devices");
		float cw = text(count, true, 17, 0, 0, p.muted, false, 0, true);
		text(count, true, 17, 602 - cw, 31, p.muted);
		for (int r = 0; r < ROWS && btop + r < (int)btDevs.size(); r++)
		{
			const bt::Device& d = btDevs[btop + r];
			bool on = btop + r == bsel;
			float y = 82 + r * 64;
			box(on ? "pick" : "slot", on, 18, y, 604, 56);
			Color c = on ? p.onPick : p.muted;
			float right = 586;
			std::string tag = d.mac == btBusy ? "..." : d.connected ? "Connected" : d.paired ? "Paired" : "";
			std::string kind = bt::kindName(d.kind);
			if (!kind.empty()) tag = tag.empty() ? kind : lang::tr(kind) + "  " + lang::tr(tag);	// (each translated: the joined line has none)
			if (!tag.empty())
			{
				float tw = text(tag, true, 17, 0, 0, c, false, 0, true);
				right -= tw;
				text(tag, true, 17, right, y + 18, d.connected ? (on ? p.onPickAccent : p.accent) : c);
				right -= 14;
			}
			text(fit(d.name, true, 20, right - 38), true, 20, 38, y + 16, c);
		}
		if (btDevs.empty() && btEnabled && !btScanning && btScanned) text("No devices found. X searches again.", true, 20, 38, 98, p.muted);
		float bw = faceWidth("B", "Back");
		face("B", "Back", 18, bw);
		std::string act = !cur || !btBusy.empty() || !btEnabled ? "" : !cur->paired ? "Pair" : cur->connected ? "Disconnect" : "Connect";
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
		icon(entry == ENTRY_SEARCH ? "search" : "phone", 50, 56, 3);
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
		if (entry == ENTRY_SEARCH && searchCount)
		{
			int m = pw.empty() ? -1 : searchCount(pw);
			n = m < 0 ? "Every system's games" : m == 0 ? "No games match" : std::to_string(m) + (m == 1 ? " game matches" : " games match");
		}
		text(n, true, 17, 50, 216, p.muted);
		std::string help = !message.empty() ? message
			: entry == ENTRY_SEARCH ? "A types the key, B deletes, X capitals, Y a space, START searches: the games found open as a shelf of their own, first on the shelf. Part of a name is enough; an empty search takes the shelf away."
			: entry == ENTRY_WIFI ? "A types the key, B deletes, X capitals, L symbols, Y a space, START joins. Or tap the keys. The password is kept in banana/wifi.conf on the card."
			: entry == ENTRY_DS_NAME ? "A types the key, B deletes, X capitals, L symbols, Y a space, START when it is typed. Kept in both emulators' settings (DraStic's drastic.cfg, BananaDS's bananads.conf)."
			: entry == ENTRY_GAME_NAME || entry == ENTRY_GAME_SORT ? "A types the key, B deletes, X capitals, L symbols, Y a space, START when it is typed. Kept in banana/game-titles.tsv on the card."
			: entry == ENTRY_ART_SEARCH ? "A types the key, B deletes, X capitals, L symbols, Y a space, START looks. A few words of the name are enough: the closest names come first."
			: "A types the key, B deletes, X capitals, L symbols, Y a space, START when it is typed. Or tap the keys. Accounts are kept in banana/accounts.conf on the card.";
		wrapped(help, 20, 50, 252, 540, 6, message.empty() ? p.muted : p.accent);
		be->endScreen();

		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		// the text being typed, here too, so the keys and what they make are seen together
		box("slot", true, 18, 12, 604, 56);
		{
			// at the right: the length of a password; for a search the games it finds (a bare number there read as a
			// match count)
			std::string n = std::to_string(pw.size());
			if (entry == ENTRY_SEARCH && searchCount)
			{
				int m = pw.empty() ? -1 : searchCount(pw);
				n = m < 0 ? "" : m == 0 ? "No games" : std::to_string(m) + (m == 1 ? " game" : " games");
			}
			float nw = text(n, true, 17, 0, 0, p.muted, false, 0, true);
			const float room = std::min(500.f, 600 - nw - 16 - 40);	// the text stops short of it
			std::string t = pw;
			float tw = text(t, true, 24, 0, 0, p.ink, false, 0, true);
			while (tw > room && t.size() > 1) { t.erase(0, 1); tw = text("..." + t, true, 24, 0, 0, p.ink, false, 0, true); }
			if (t != pw) t = "..." + t;
			if (pw.empty()) { tw = 0; text(entrySub.empty() ? "Type here" : entrySub, true, 22, 38, 28, p.muted); }
			else text(t, true, 24, 38, 27, p.ink);
			be->fill(std::round(X(40 + tw)), std::round(X(26)), std::round(X(10)), std::round(X(28)), p.accent);	// the cursor
			text(n, true, 17, 600 - nw, 31, p.muted);
		}
		// the keys: the selected one in the menu's yellow, the others light on dark
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 10; c++)
			{
				float x = KX + c * KPITCH, y = KY + r * KROW;
				bool on = r == krow && c == kcol;
				box(on ? "pick" : "slot", on, x, y, KPITCH - 6, KH);
				std::string k(1, KEYS[layer][r][c]);
				float tw = text(k, true, 28, 0, 0, p.ink, false, 0, true);
				text(k, true, 28, x + std::round((KPITCH - 6 - tw) / 2), y + 13, on ? p.onPick : p.ink);
			}
		int fsel = krow == 4 ? fkeyAt(kcol) : -1;
		for (int i = 0; i < 5; i++)
		{
			float x = KX + FKEYS[i].c0 * KPITCH, y = KY + 4 * KROW, kw = FKEYS[i].span * KPITCH - 6;
			bool on = i == fsel;
			box(on ? "pick" : "slot", on, x, y, kw, KH);
			std::string k = i == 1 && layer == 2 ? "abc" : i == 0 && layer == 1 ? "SHIFT" : FKEYS[i].name;
			if (i == 4) k = entry == ENTRY_WIFI ? "Join" : "Done";
			bool lit = (i == 0 && layer == 1) || (i == 1 && layer == 2);
			float tw = text(k, true, 21, 0, 0, p.ink, false, 0, true);
			text(k, true, 21, x + std::round((kw - tw) / 2), y + 17, on ? p.onPick : lit ? p.accent : p.ink);
		}
		// the buttons that work anywhere on the keyboard
		const std::string bl = pw.empty() ? "Back" : "Delete", done = entry == ENTRY_WIFI ? "Join" : "Done";
		float x = 18;
		struct { const char* key; std::string label; } chips[] = { { "B", bl }, { "X", "Shift" }, { "Y", "Space" } };
		for (auto& c : chips)
		{
			float w = faceWidth(c.key, c.label);
			face(c.key, c.label, x, w);
			x += w + 8;
		}
		float aw = faceWidth("START", done);
		face("START", done, 622 - aw, aw);
		be->endScreen();
	}

	// ---- updates: the packages on the card, one applied by the update script ---------------------------------------
	void Settings::openUpdates()
	{
		page = UPDATES;
		usel = 0; utop = 0; armed = -1;
		message.clear();
		ustatus.clear();
		applying = false; updated = false;
		version = readLine("/etc/banana/version");
		slot = readLine(runDir + "/slot");
		board = readLine("/etc/banana/board");
		listPkgs();
		readOnline();
		downloading = false;
		pollDownload();		// a download started before (it goes on while the page is closed)
	}

	void Settings::listPkgs()
	{
		pkgs.clear();
		if (FILE* p = popen(("'" + updateTool + "' list 2>/dev/null").c_str(), "r"))
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
		ustatus = readLine(runDir + "/update-status");
		if (ustatus.compare(0, 4, "done") == 0) { applying = false; updated = true; }
		else if (ustatus.compare(0, 6, "failed") == 0) applying = false;
	}

	// ---- over the air (#69): the update script's check and download -------------------------------------------------
	void Settings::readOnline()
	{
		Online o;
		std::ifstream f(updatesDir + "/.check");
		for (std::string ln; std::getline(f, ln);)
		{
			size_t eq = ln.find('=');
			if (eq == std::string::npos) continue;
			std::string k = ln.substr(0, eq), v = ln.substr(eq + 1);
			if (k == "state") o.state = v;
			else if (k == "last_state") o.lastState = v;
			else if (k == "current") o.current = v;
			else if (k == "latest") o.latest = v;
			else if (k == "file") o.file = v;
			else if (k == "notes") o.notes = v;
			else if (k == "error") o.error = v;
			else if (k == "board") o.board = v;
			else if (k == "prerelease_setting") o.preSetting = v;
			else if (k == "prerelease") o.pre = v == "1";
			else if (k == "size") o.size = atoll(v.c_str());
			else if (k == "checked") o.checked = atoll(v.c_str());
		}
		// an answer for the version before an update, another handheld's card, or with Test versions set the other
		// way since: as if never asked
		std::string pre = getConfValue(data + "/settings.conf", "update_prerelease", "on");
		if (!o.state.empty() && ((!version.empty() && o.current != version) || (!board.empty() && o.board != board) || o.preSetting != pre))
			o = Online();
		online = o;
	}

	bool Settings::onlineNewer() const
	{
		return !online.latest.empty() && !online.file.empty() && (online.state == "newer" || (online.state == "error" && online.lastState == "newer"));
	}

	int Settings::onlinePkg() const
	{
		if (!onlineNewer()) return -1;
		for (size_t i = 0; i < pkgs.size(); i++)
		{
			const std::string& p = pkgs[i].path;
			if (p.size() >= online.file.size() && p.compare(p.size() - online.file.size(), std::string::npos, online.file) == 0 &&
				(p.size() == online.file.size() || p[p.size() - online.file.size() - 1] == '/'))
				return (int)i;
		}
		return -1;
	}

	long long Settings::partialBytes() const
	{
		struct stat st;
		if (online.file.empty() || stat((updatesDir + "/.partial/" + online.file).c_str(), &st) != 0) return 0;
		return st.st_size;
	}

	void Settings::startCheck()
	{
		message.clear();
		dlLine.clear();
		armed = -1;
		checkJob.start("'" + updateTool + "' check 2>&1", sync);
		if (sync) checkDone();	// (the headless test: its answer at once)
	}

	// the check's answer: the script kept it in .check (also when it failed: the error, and what it found before)
	bool Settings::checkDone()
	{
		std::string out;
		if (!checkJob.take(out)) return false;
		readOnline();
		if (online.state.empty()) message = trim(out).empty() ? "The check didn't answer." : trim(out);
		usel = 0; utop = 0;
		return true;
	}

	void Settings::startDownload()
	{
		message.clear();
		unlink((runDir + "/update-download").c_str());
		dlLine = "downloading " + std::to_string(partialBytes()) + " " + std::to_string(online.size);
		downloading = true;
		dlStarted = now;
		nextDl = now + 500;
		spawn(updateTool.c_str(), "download");
	}

	void Settings::pollDownload()
	{
		nextDl = now + 500;
		std::string ln = readLine(runDir + "/update-download");
		int pid = 0;
		std::ifstream(runDir + "/update-download.pid") >> pid;
		bool alive = pid > 0 && kill(pid, 0) == 0;
		bool going = ln.compare(0, 12, "downloading ") == 0 || ln == "verifying";
		if (!downloading)
		{
			// the page opened: one still running from before, or how the last one ended
			downloading = alive && going;
			dlLine = ln;
			return;
		}
		if (ln.empty() && now - dlStarted < 5000) return;	// just started: its first line isn't there yet
		dlLine = ln;
		if (ln.compare(0, 6, "ready ") == 0)
		{
			downloading = false;
			listPkgs();
			usel = 0; utop = 0; armed = -1;		// the online row: Install
		}
		else if (ln.compare(0, 7, "failed:") == 0 || ln.compare(0, 9, "cancelled") == 0) downloading = false;
		else if (!alive && now - dlStarted >= 5000)
		{
			downloading = false;	// gone without a word (killed, or it never started)
			dlLine = "failed: the download stopped";
		}
	}

	// the online row: its name, the tag on its right and what A does
	std::string Settings::onlineRow(std::string& tag, std::string& act) const
	{
		auto mb = [](long long b) { return std::to_string((b + 524288) / 1048576); };
		tag.clear(); act.clear();
		if (checkJob.running()) return "Checking for updates...";
		if (downloading)
		{
			if (dlLine == "verifying") return "Checking the download...";
			long long a = 0, t = online.size;
			sscanf(dlLine.c_str(), "downloading %lld %lld", &a, &t);
			int pc = t > 0 ? (int)std::min<long long>(100, a * 100 / t) : 0;
			tag = std::to_string(pc) + "% · " + mb(a) + " of " + mb(t) + " MB";
			act = "Cancel";
			return "Downloading Banana " + online.latest;
		}
		if (onlineNewer())
		{
			if (onlinePkg() >= 0)
			{
				tag = armed == 2000 ? "Press A again" : "Downloaded";
				act = armed == 2000 ? "Confirm" : "Install";
				return "Install Banana " + online.latest;
			}
			long long have = partialBytes();
			tag = have > 0 && online.size > 0 ? "Resume: " + mb(have) + " of " + mb(online.size) + " MB" : mb(online.size) + " MB";
			act = "Download";
			return "Download Banana " + online.latest;
		}
		act = "Check";
		if (online.state == "current") tag = "Up to date";
		else if (online.state == "none") tag = "None for this handheld";
		else if (online.state == "error") tag = "Failed";
		return "Check for updates";
	}

	Settings::Result Settings::pressOnline()
	{
		if (checkJob.running()) return NONE;
		if (downloading)
		{
			// A cancels: what came stays in .partial, and the next Download carries on from there
			if (FILE* p = popen(("'" + updateTool + "' cancel 2>&1").c_str(), "r")) { char ln[256]; while (fgets(ln, sizeof ln, p)) {} pclose(p); }
			nextDl = now;
			return MOVED;
		}
		if (onlineNewer())
		{
			int k = onlinePkg();
			if (k < 0) { startDownload(); return MOVED; }
			if (armed != 2000) { armed = 2000; return MOVED; }	// A again to go ahead
			armed = -1;
			startApply(k);
			return MOVED;
		}
		startCheck();
		return MOVED;
	}

	void Settings::startApply(int i)
	{
		unlink((runDir + "/update-status").c_str());
		ustatus = "starting";
		applying = true;
		nextStatus2 = now + 500;
		spawn(updateTool.c_str(), "apply", pkgs[i].path.c_str());
	}

	std::string Settings::updateState() const
	{
		std::string tag, act;
		std::string row = onlineRow(tag, act);
		return "row \"" + row + "\" tag \"" + tag + "\" act \"" + act + "\" state " + (online.state.empty() ? "-" : online.state) +
			" latest " + (online.latest.empty() ? "-" : online.latest) + " dl \"" + dlLine + "\" packages " + std::to_string(pkgs.size()) +
			" status \"" + ustatus + "\" message \"" + message + "\"";
	}

	// ---- Screen colours (#N3) --------------------------------------------------------------------------------------
	// The two panels rarely show the same white: one is corrected towards the other through the display's one colour
	// table. The page shows the same test pattern on both; the screen being corrected draws it through the values
	// tried (in software: the table off meanwhile, as changing it blanks the panel for a frame), A keeps them.
	namespace
	{
		struct ColRow { const char* label; float lo, hi, step; };
		const ColRow COL_ROWS[] = { { "Screen", 0, 0, 0 }, { "Red", 0.5f, 1, 0.01f }, { "Green", 0.5f, 1, 0.01f }, { "Blue", 0.5f, 1, 0.01f },
			{ "Gamma", 0.6f, 1.6f, 0.02f }, { "Brightness", 0.4f, 1, 0.01f }, { "Black", 0, 0.12f, 0.005f } };
		const int COL_N = 7;
		const char* const COL_PATTERNS[] = { "Grey steps", "Colour bars", "White", "Mid grey" };
		float& colField(ScreenColors& c, int row)
		{
			switch (row) { case 1: return c.red; case 2: return c.green; case 3: return c.blue; case 4: return c.gamma; case 5: return c.bright; default: return c.black; }
		}
	}

	void Settings::openColors()
	{
		colSaved = colEdit = ScreenColors::load(data + "/screen-colors.conf");
		if (colEdit.screen < 0) { colEdit = ScreenColors(); colEdit.screen = 1; }	// (none kept: neutral; the bottom one first)
		colRow = 0; colPattern = 0;
		if (setColors) setColors(ScreenColors());	// the table off: the page draws the values itself
		message.clear();
		page = COLORS;
	}

	Settings::Result Settings::pressColors(Key k)
	{
		switch (k)
		{
		case Key::UP: colRow = (colRow + COL_N - 1) % COL_N; return MOVED;
		case Key::DOWN: colRow = (colRow + 1) % COL_N; return MOVED;
		case Key::LEFT: case Key::RIGHT:
		{
			int d = k == Key::LEFT ? -1 : 1;
			if (colRow == 0)
			{
				static const int order[3] = { 1, 0, -1 };	// the bottom corrected, the top, none
				int i = colEdit.screen == 1 ? 0 : colEdit.screen == 0 ? 1 : 2;
				colEdit.screen = order[(i + d + 3) % 3];
			}
			else
			{
				const ColRow& r = COL_ROWS[colRow];
				float& v = colField(colEdit, colRow);
				v = std::max(r.lo, std::min(r.hi, std::round((v + d * r.step) / r.step) * r.step));
			}
			return MOVED;
		}
		case Key::L: case Key::R: colPattern = (colPattern + (k == Key::L ? 3 : 1)) % 4; return MOVED;
		case Key::X:
			// no correction, the Screen row too (it said "Correct the top" over values that no longer correct anything)
			colEdit = ScreenColors();
			message = "Back to no correction: A keeps that, B goes back to what was kept.";
			return MOVED;
		case Key::A:
		{
			// kept: the file, and the table on the screen it corrects
			ScreenColors keep = colEdit;
			if (keep.neutral()) keep = ScreenColors();	// (None, or nothing to correct: no values kept either)
			// the table first: one the display refuses isn't kept, and the page says why
			std::string e = setColors ? setColors(keep) : "";
			if (!e.empty())
			{
				if (setColors) setColors(ScreenColors());	// (off again under the page)
				message = "The display refused it: " + e + ". Nothing was kept.";
				return MOVED;
			}
			if (!keep.save(data + "/screen-colors.conf")) { message = "The card can't be written."; if (setColors) setColors(ScreenColors()); return MOVED; }
			colSaved = keep;
			page = LIST; message.clear();
			toast("Screen colours", keep.screen < 0 ? "No correction" : keep.screen == 0 ? "The top screen corrected" : "The bottom screen corrected", 0x6ad07a, 1, 3000);
			return CHANGED;
		}
		case Key::B: case Key::START:
			if (setColors) setColors(colSaved);	// the table as it was kept
			page = LIST; message.clear();
			if (k == Key::START) { mOpen = false; return CLOSED; }
			return MOVED;
		default: return NONE;
		}
	}

	Settings::Result Settings::touchColors(float cx, float cy)
	{
		(void)cx;
		if (downY >= 422) return pressColors(downX < 320 ? Key::B : Key::A);
		for (int r = 0; r < COL_N; r++)
		{
			float ry = 206 + r * 30;
			if (downY < ry || downY > ry + 28) continue;
			if (r != colRow) { colRow = r; return MOVED; }
			return pressColors(downX < 320 ? Key::LEFT : Key::RIGHT);
		}
		(void)cy;
		return NONE;
	}

	// the pattern on a screen, from y0 for h (640 x 480 units), through the values tried on the screen they correct
	void Settings::colorsPattern(int screen, float y0, float h)
	{
		const bool through = colEdit.screen == screen && !colEdit.neutral();
		auto fillc = [&](float x, float y, float w, float hh, uint32_t rgb)
		{
			uint32_t c = through ? colEdit.apply(0xff000000 | rgb) : (0xff000000 | rgb);
			be->fill(X(x), X(y), X(x + w) - X(x), X(y + hh) - X(y), Color::hex(c & 0xffffff));
		};
		switch (colPattern)
		{
		case 0:	// sixteen grey steps, black to white
			for (int i = 0; i < 16; i++) { uint32_t g = (uint32_t)std::lround(i * 255.0 / 15); fillc(i * 40.0f, y0, 40, h, g << 16 | g << 8 | g); }
			break;
		case 1:	// the colour bars, and below them their greys
		{
			static const uint32_t bars[8] = { 0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000, 0x0000ff, 0x000000 };
			for (int i = 0; i < 8; i++) fillc(i * 80.0f, y0, 80, h * 0.7f, bars[i]);
			for (int i = 0; i < 8; i++) { uint32_t g = (uint32_t)std::lround(i * 255.0 / 7); fillc(i * 80.0f, y0 + h * 0.7f, 80, h * 0.3f, g << 16 | g << 8 | g); }
			break;
		}
		case 2: fillc(0, y0, 640, h, 0xffffff); break;
		default: fillc(0, y0, 640, h, 0x808080); break;
		}
	}

	void Settings::renderColors()
	{
		SPal p = pal(light);
		// the top screen: the pattern, its name on a strip at the top
		be->beginScreen(0);
		colorsPattern(0, 0, 480);
		be->fill(0, 0, X(640), X(30), Color::hex(0x101014));
		text(std::string("Screen colours  ·  ") + COL_PATTERNS[colPattern] + "  ·  L / R: another pattern", true, 15, 12, 7, Color::hex(0xd0d0d0));
		be->fill(0, X(450), X(640), X(30), Color::hex(0x101014));
		std::string hint = !message.empty() ? message : "Make the screen you correct look like the other one. X: no correction.";
		text(fit(hint, true, 14, 616), true, 14, 12, 457, Color::hex(0xd0d0d0));
		be->endScreen();
		// the bottom screen: the same pattern above, the values below
		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		colorsPattern(1, 0, 196);
		be->fill(0, X(196), X(640), X(2), p.line);
		for (int r = 0; r < COL_N; r++)
		{
			bool on = r == colRow;
			float y = 206 + r * 30;
			if (on) box("pick", true, 18, y, 604, 28);
			Color c = on ? p.onPick : p.ink;
			text(COL_ROWS[r].label, true, 17, 38, y + 5, c);
			char v[32];
			if (r == 0) snprintf(v, sizeof v, "%s", colEdit.screen == 0 ? "Correct the top" : colEdit.screen == 1 ? "Correct the bottom" : "None");
			else if (r == 4) snprintf(v, sizeof v, "%.2f", colField(colEdit, r));
			else if (r == 6) snprintf(v, sizeof v, "%.1f%%", colField(colEdit, r) * 100);
			else snprintf(v, sizeof v, "%d%%", (int)std::lround(colField(colEdit, r) * 100));
			float vw = text(v, true, 17, 0, 0, c, false, 0, true);
			arrow(true, 576, y + 9, c);
			text(v, true, 17, 566 - vw, y + 5, c);
			arrow(false, 556 - vw - 2 - std::round(13 / 2.0f + 1), y + 9, c);
		}
		float bw = faceWidth("B", "Cancel");
		face("B", "Cancel", 18, bw);
		float aw = faceWidth("A", "Keep");
		face("A", "Keep", 622 - aw, aw);
		be->endScreen();
	}

	// ---- the game page's Box art (#28): libretro's names for the words typed, one picked ---------------------------
	std::string Settings::scrapeTool() const
	{
		const char* prog = getenv("BANANA_SCRAPE");	// (another build of it, for testing)
		return prog && *prog ? prog : "/usr/bin/banana-scrape";
	}

	Settings::Result Settings::pressArt(Key k)
	{
		int n = (int)artNames.size();
		bool busy = artJob.running();
		switch (k)
		{
		case Key::UP: case Key::DOWN:
			if (!n || busy) return NONE;
			asel = (asel + (k == Key::UP ? -1 : 1) + n) % n;
			if (asel < atop) atop = asel;
			if (asel >= atop + ROWS) atop = asel - ROWS + 1;
			return MOVED;
		case Key::A:
		{
			if (!n || busy) return NONE;
			// its cover and title screen for this game, over what it had (banana-scrape keeps the match for the
			// scrapes after)
			auto q = [](const std::string& s) { std::string r = "'"; for (char c : s) { if (c == '\'') r += "'\\''"; else r += c; } return r + "'"; };
			std::string sys = gameKey.substr(0, gameKey.find('/'));
			artMatching = true;
			message = "Fetching the cover and the title screen...";
			artJob.start(q(scrapeTool()) + " --match " + q(sys) + " " + q(pgPath) + " " + q(artNames[asel]) + " 2>/dev/null", sync);
			return MOVED;
		}
		case Key::B: case Key::LEFT:
			// not while banana-scrape runs: a search typed again would wait for this one to end (the job's thread is
			// joined), with the whole menu still until it does
			if (busy) return NONE;
			page = GAME; message.clear();
			return MOVED;
		case Key::START:
			if (busy) return NONE;
			page = LIST; mOpen = false;
			return pgArtDone ? TITLES : CLOSED;	// (a cover fetched: the library again)
		default: return NONE;
		}
	}

	Settings::Result Settings::touchArt(float cx, float cy)
	{
		(void)cx; (void)cy;
		if (downY >= 418 && downY <= 466) return pressArt(downX < 320 ? Key::B : Key::A);
		for (int r = 0; r < ROWS && atop + r < (int)artNames.size(); r++)
		{
			float ry = 82 + r * 64;
			if (downY < ry || downY > ry + 56) continue;
			if (atop + r != asel) { asel = atop + r; return MOVED; }
			return pressArt(Key::A);
		}
		return NONE;
	}

	void Settings::renderArt()
	{
		SPal p = pal(light);
		bool busy = artJob.running();
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon("picture", 50, 56, 3);
		text("Box art for", true, 17, 170, 62, p.muted, true, 1.4f);
		text(fit(pgTitle, true, 30, 420), true, 30, 170, 88, p.ink);
		text(fit(artNames.empty() ? "\"" + artWords + "\"" : artNames[asel], true, 22, 420), true, 22, 170, 130, p.accent);
		std::string help = !message.empty() ? message + (busy && !artMatching ? " (B works once it has answered.)" : "")
			: "libretro's names for the words typed, the closest first. A fetches that game's cover and title screen for this one "
			  "(over what it had), and every scrape after keeps them. B types other words.";
		wrapped(help, 20, 50, 196, 540, 7, p.muted);
		be->endScreen();

		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("bubble", false, 18, 14, 604, 54);
		text("Box art", true, 22, 38, 28, p.ink);
		std::string count = busy && !artMatching ? "Looking..." : std::to_string(artNames.size()) + (artNames.size() == 1 ? " name" : " names");
		float cw = text(count, true, 17, 0, 0, p.muted, false, 0, true);
		text(count, true, 17, 602 - cw, 31, p.muted);
		for (int r = 0; r < ROWS && atop + r < (int)artNames.size(); r++)
		{
			int i = atop + r;
			bool on = i == asel;
			float y = 82 + r * 64;
			box(on ? "pick" : "slot", on, 18, y, 604, 56);
			text(fit(artNames[i], true, 18, 560), true, 18, 38, y + 18, on ? p.onPick : p.muted);
		}
		float bw = faceWidth("B", "Back");
		if (!(busy && artMatching)) face("B", "Back", 18, bw);
		const char* act = busy ? "" : artNames.empty() ? "" : "Use";
		if (*act)
		{
			float aw = faceWidth("A", act);
			face("A", act, 622 - aw, aw);
		}
		be->endScreen();
	}

	Settings::Result Settings::pressUpdates(Key k)
	{
		int n = 1 + (int)pkgs.size();	// the online row, then the packages on the card
		switch (k)
		{
		case Key::UP: case Key::DOWN:
			if (applying) return NONE;
			usel = (usel + (k == Key::UP ? -1 : 1) + n) % n;
			armed = -1;
			if (usel < utop) utop = usel;
			if (usel >= utop + ROWS) utop = usel - ROWS + 1;
			return MOVED;
		case Key::A: case Key::RIGHT:
		{
			if (updated) return REBOOT;			// "Restart": the new version runs from the next boot
			if (applying) return NONE;
			if (usel == 0) return pressOnline();
			int i = usel - 1;
			if (armed != 1000 + i) { armed = 1000 + i; return MOVED; }	// A again to go ahead
			armed = -1;
			startApply(i);
			return MOVED;
		}
		case Key::X:
			// asks GitHub again (a version found, or a download that failed: is there a newer one?)
			if (applying || downloading || checkJob.running() || usel != 0) return NONE;
			startCheck();
			return MOVED;
		case Key::B: case Key::LEFT:
			if (applying) return NONE;			// not while a slot is being written (a download goes on)
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
		const std::string& bd = board.empty() ? online.board : board;	// (/etc/banana/board, or the check's)
		const std::string handheld = bd == "rgds-plus" ? "RG DS Plus" : bd == "rgds" ? "RG DS" : "handheld";
		auto mb = [](long long b) { return std::to_string((b + 524288) / 1048576); };
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon("gear", 50, 56, 3);
		text("System", true, 17, 170, 62, p.muted, true, 1.4f);
		text("Update", true, 30, 170, 88, p.ink);
		std::string st = applying ? "Writing the update: " + ustatus : updated ? "Written: restart to run it" : ustatus.compare(0, 6, "failed") == 0 ? "Failed" : "Running " + (version.empty() ? std::string("dev") : version) + (slot.empty() ? "" : ", slot " + slot);
		text(fit(st, true, 22, 420), true, 22, 170, 130, p.accent);
		// what the last check found
		std::string found;
		if (!applying && !updated)
		{
			if (onlineNewer()) found = online.pre ? "Newest for the " + handheld + ": " + online.latest + " (a test version)" : "Newest for the " + handheld + ": " + online.latest;
			else if (online.state == "current") found = "Up to date: " + online.latest + " is the newest for the " + handheld;
			else if (online.state == "none") found = "No release for the " + handheld + " yet";
			else if (online.state == "error") found = "The check failed";
		}
		if (!found.empty()) text(fit(found, false, 20, 540), false, 20, 50, 176, p.ink);
		std::string help, notes;
		bool failedDl = !downloading && dlLine.compare(0, 8, "failed: ") == 0;
		if (applying) help = "The new version is being written to the other slot and checked; this one isn't touched. Don't turn the handheld off. It takes a minute or two.";
		else if (updated) help = "The new version is in the other slot and will run from the next restart. A restarts now. If it ever fails to start, Previous version brings this one back.";
		else if (ustatus.compare(0, 6, "failed") == 0) help = "The update wasn't applied: " + ustatus.substr(8) + ". This version is untouched.";
		else if (usel == 0)
		{
			if (checkJob.running()) help = "Asking GitHub which version is the newest for this handheld...";
			else if (downloading && dlLine == "verifying") help = "Checking the download against the release's SHA-256...";
			else if (downloading) help = "Downloading to banana/updates on the card. B goes back and the download carries on (this page shows how far it is); A cancels it, and the next Download carries on from where it stopped.";
			else if (failedDl) help = "The download failed: " + dlLine.substr(8) + ".";
			else if (!message.empty()) help = message;
			else if (online.state == "error") help = "The check failed: " + online.error + ".";
			else if (onlineNewer() && onlinePkg() >= 0) help = "Downloaded and checked against the release's SHA-256. A, then A again, writes it to the other slot; this version stays as it is until the restart.";
			else if (onlineNewer())
			{
				help = "A downloads it (" + mb(online.size) + " MB) to banana/updates on the card and checks it; nothing is installed until you choose Install. X checks again.";
				notes = online.notes;
			}
			else if (online.state == "current") help = "This handheld has the newest version. A checks again.";
			else if (online.state == "none") help = "GitHub has no release for this handheld. With Settings > System > Test versions left out, only stable versions count, and there are none yet.";
			else help = "A asks GitHub (Wi-Fi needed) whether a newer version is out for this handheld. Nothing is downloaded until you choose Download.";
		}
		else if (pkgs.empty()) help = "No update packages found. Put a banana-" + (board.empty() ? std::string("<board>") : board) + "-<version>.update.tar from a release in banana/updates on the card (File sharing can copy it over Wi-Fi), then come back here.";
		else help = "A on a package writes it to the other slot and checks it; the running version stays until the restart. Packages are in banana/updates on the card.";
		float hy = found.empty() ? 196 : 214;
		wrapped(help, 20, 50, hy, 540, notes.empty() ? 7 : 4, p.muted);
		if (!notes.empty()) wrapped(notes, 18, 50, hy + 4 * 28 + 6, 540, 4, p.ink);
		be->endScreen();

		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("bubble", false, 18, 14, 604, 54);
		text("Update packages", true, 22, 38, 28, p.ink);
		std::string count = std::to_string(pkgs.size()) + (pkgs.size() == 1 ? " package" : " packages");
		float cw = text(count, true, 17, 0, 0, p.muted, false, 0, true);
		text(count, true, 17, 602 - cw, 31, p.muted);
		std::string otag, oact;
		std::string orow = onlineRow(otag, oact);
		for (int r = 0; r < ROWS && utop + r < 1 + (int)pkgs.size(); r++)
		{
			int i = utop + r;
			bool on = i == usel;
			float y = 82 + r * 64;
			box(on ? "pick" : "slot", on, 18, y, 604, 56);
			Color c = on ? p.onPick : p.muted;
			std::string name, tag;
			bool hot;
			if (i == 0) { name = orow; tag = otag; hot = armed == 2000 || downloading; }
			else
			{
				const Pkg& k = pkgs[i - 1];
				name = "Banana " + k.version;
				hot = armed == 1000 + i - 1;
				tag = hot ? "Press A again" : (k.board == board || board.empty() || k.board == "universal" ? (k.board == "universal" ? std::string("for both handhelds") : "for the " + k.board) : "not for this handheld");
			}
			float tw = tag.empty() ? 0 : text(tag, true, 17, 0, 0, c, false, 0, true);
			text(fit(name, true, 20, 560 - tw - 24), true, 20, 38, y + 16, c);
			if (!tag.empty()) text(tag, true, 17, 586 - tw, y + 18, hot ? (on ? p.onPickAccent : p.accent) : c);
		}
		if (pkgs.empty() && utop == 0) text("Nothing in banana/updates on the card.", true, 20, 38, 162, p.muted);
		float bw = faceWidth("B", "Back");
		if (!applying) face("B", "Back", 18, bw);
		std::string act = updated ? "Restart" : applying ? "" : usel == 0 ? oact : armed == 1000 + usel - 1 ? "Confirm" : "Update";
		if (!act.empty())
		{
			float aw = faceWidth("A", act);
			face("A", act, 622 - aw, aw);
		}
		be->endScreen();
	}

	// ---- the game page: a DS game's own emulator and shader ------------------------------------------------------
	static const char* GAME_EMUS[] = { "", "superdrastic", "bananads" };
	static const char* GAME_EMU_NAMES[] = { "", "SuperDrastic", "BananaDS" };

	// a game's own choice in banana/<file> ("<rom file name> = <value>" a line, the name as on the card without its
	// folder; the last line for a game counts): nds-per-game.conf its emulator (run-nds), nds-per-game-shader.conf its
	// shader (run-drastic, run-bananads, through nds-game-shader)
	std::string Settings::perGame(const std::string& file, const std::string& rom)
	{
		std::ifstream f(data + "/" + file);
		std::string ln, r;
		while (std::getline(f, ln))
		{
			size_t eq = ln.rfind(" = ");	// (the last one: a name may have " = " in it)
			if (ln.empty() || ln[0] == '#' || eq == std::string::npos) continue;
			if (ln.substr(0, eq) == rom) r = trim(ln.substr(eq + 3));
		}
		return r;
	}

	void Settings::setPerGame(const std::string& file, const std::string& rom, const std::string& value)
	{
		std::string path = data + "/" + file, out, ln;
		{
			std::ifstream f(path);
			// a file that is there but can't be read (a card error): left alone
			if (!f && access(path.c_str(), F_OK) == 0) return;
			while (std::getline(f, ln))
			{
				size_t eq = ln.rfind(" = ");
				if (!ln.empty() && ln[0] != '#' && eq != std::string::npos && ln.substr(0, eq) == rom) continue;
				out += ln + "\n";
			}
		}
		if (!value.empty()) out += rom + " = " + value + "\n";
		// the last game back to "As in Settings": no file rather than an empty one (the launchers and the tests look for
		// the file; a card with nothing chosen per game has none)
		if (out.find_first_not_of(" \t\r\n") == std::string::npos)
		{
			unlink(path.c_str());
			return;
		}
		std::string tmp = path + ".new";
		FILE* f = fopen(tmp.c_str(), "w");
		if (!f) return;
		bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
		ok = fflush(f) == 0 && fsync(fileno(f)) == 0 && ok;
		fclose(f);
		if (ok) rename(tmp.c_str(), path.c_str());
		else unlink(tmp.c_str());
	}

	// a DS game's own Threaded 3D (SuperDrastic: DraStic's drastic.cfg for its runs) and microphone (both emulators'
	// threshold), #27: index 0 "As in Settings", then these values (nds-per-game-3d.conf, nds-per-game-mic.conf)
	static const char* const GAME_3D_VALUES[] = { "", "1", "0" };
	static const char* const GAME_3D_NAMES[] = { "", "On", "Off" };
	// (a level, which each launcher turns into its emulator's own threshold: BananaDS low 0.15, medium 0.05, high 0.02
	// as Settings' Microphone; SuperDrastic low 0.3, medium 0.15, high 0.03, its own scale)
	static const char* const GAME_MIC_VALUES[] = { "", "low", "medium", "high", "off" };
	static const char* const GAME_MIC_NAMES[] = { "", "Low", "Medium", "High", "Off" };
	static const float MIC_BANANADS[] = { 0, 0.15f, 0.05f, 0.02f, 0 }, MIC_SUPERDRASTIC[] = { 0, 0.3f, 0.15f, 0.03f, 0 };

	std::string Settings::gameEmulator(const std::string& rom) { return perGame("nds-per-game.conf", rom); }
	void Settings::setGameEmulator(const std::string& rom, const std::string& emu) { setPerGame("nds-per-game.conf", rom, emu); }

	void Settings::openGame(const std::string& title, const std::string& romName, bool nds, const std::string& key,
		const std::string& name, const std::string& sort, const std::string& path, const std::string& scraped)
	{
		open();
		pgScraped = scraped.empty() ? (name.empty() ? title : "") : scraped;
		pgTitle = title; pgRom = romName; pgNds = nds; pgPath = path; pgArtDone = false; artWords.clear();
		gameKey = key; gameName = name; gameSort = sort;
		pgRows.clear();
		if (nds) pgRows = { PG_EMU, PG_SHADER, PG_3D, PG_MIC };
		pg3d = pgMic = 0;
		{
			std::string v = perGame("nds-per-game-3d.conf", romName), m = perGame("nds-per-game-mic.conf", romName);
			for (int i = 1; i < 3; i++) if (v == GAME_3D_VALUES[i]) pg3d = i;
			for (int i = 1; i < 5; i++) if (m == GAME_MIC_VALUES[i]) pgMic = i;
		}
		if (!key.empty()) { pgRows.push_back(PG_NAME); pgRows.push_back(PG_SORT); }
		if (!key.empty() && !path.empty()) pgRows.push_back(PG_ART);
		std::string cur = gameEmulator(romName);
		pgEmu = 0;
		for (int i = 1; i < 3; i++) if (cur == GAME_EMUS[i]) pgEmu = i;
		// the shaders as Settings lists them (the card's included, as they are now), after "As in Settings"
		std::vector<std::string> names, values;
		shaderChoices(data, names, values);
		pgShaderNames = { "" }; pgShaderValues = { "" };
		pgShaderNames.insert(pgShaderNames.end(), names.begin(), names.end());
		pgShaderValues.insert(pgShaderValues.end(), values.begin(), values.end());
		std::string sh = perGame("nds-per-game-shader.conf", romName);
		pgShader = 0;
		for (size_t i = 1; i < pgShaderValues.size(); i++) if (pgShaderValues[i] == sh) pgShader = (int)i;
		if (!sh.empty() && pgShader == 0)	// a card shader that has gone: shown (and kept) as it is
		{
			pgShaderNames.push_back(sh + " (not on the card)"); pgShaderValues.push_back(sh);
			pgShader = (int)pgShaderValues.size() - 1;
		}
		pgRow = pgTop = 0;
		message.clear();
		page = GAME;
	}

	Settings::Result Settings::pressGame(Key k)
	{
		int n = (int)pgRows.size();
		if (!n) { if (k == Key::B || k == Key::A) { close(); return CLOSED; } return NONE; }
		PgRow row = pgRows[std::min(pgRow, n - 1)];
		switch (k)
		{
		case Key::UP: case Key::DOWN:
			pgRow = (pgRow + (k == Key::UP ? n - 1 : 1)) % n;
			if (pgRow < pgTop) pgTop = pgRow;
			if (pgRow >= pgTop + ROWS) pgTop = pgRow - ROWS + 1;
			return MOVED;
		case Key::LEFT: case Key::RIGHT:
		{
			int d = k == Key::LEFT ? -1 : 1;
			if (row == PG_EMU) pgEmu = (pgEmu + d + 3) % 3;
			else if (row == PG_3D) pg3d = (pg3d + d + 3) % 3;
			else if (row == PG_MIC) pgMic = (pgMic + d + 5) % 5;
			else if (row == PG_SHADER)
			{
				int ns = (int)pgShaderValues.size();
				pgShader = (pgShader + d + ns) % ns;
			}
			else return NONE;
			return MOVED;
		}
		case Key::A: case Key::START:
			// on the name or the sort title: the keyboard (kept as soon as it's typed); else Done
			if (k == Key::A && row == PG_ART)
			{
				if (!wifiOn()) { message = "Wi-Fi is off: switch it on in Settings > Network first."; return MOVED; }
				startEntry(ENTRY_ART_SEARCH, "Box art: the game's name in libretro's collection", pgTitle, GAME, artWords.empty() ? pgTitle : artWords);
				return MOVED;
			}
			if (k == Key::A && (row == PG_NAME || row == PG_SORT))
			{
				bool nm = row == PG_NAME;
				startEntry(nm ? ENTRY_GAME_NAME : ENTRY_GAME_SORT, nm ? "This game's name" : "This game's sort title", pgTitle, GAME,
					nm ? (gameName.empty() ? pgTitle : gameName) : gameSort);
				return MOVED;
			}
			if (pgNds)
			{
				setGameEmulator(pgRom, GAME_EMUS[pgEmu]);
				setPerGame("nds-per-game-shader.conf", pgRom, pgShaderValues[pgShader]);
				setPerGame("nds-per-game-3d.conf", pgRom, GAME_3D_VALUES[pg3d]);
				setPerGame("nds-per-game-mic.conf", pgRom, GAME_MIC_VALUES[pgMic]);
			}
			close();
			return pgArtDone ? TITLES : CHANGED;	// (a new cover: the library again)
		case Key::B: close(); return pgArtDone ? TITLES : CLOSED;
		default: return NONE;
		}
	}

	Settings::Result Settings::touchGame(float cx, float cy)
	{
		(void)cx; (void)cy;
		if (downY >= 418 && downY <= 466) return pressGame(downX < 320 ? Key::B : Key::A);
		for (int r = pgTop; r < (int)pgRows.size() && r < pgTop + ROWS; r++)
		{
			float ry = 82 + (r - pgTop) * 64;
			if (downY < ry || downY > ry + 56) continue;
			if (r != pgRow) { pgRow = r; return MOVED; }
			if (pgRows[r] == PG_NAME || pgRows[r] == PG_SORT || pgRows[r] == PG_ART) return pressGame(Key::A);	// a tap again: the keyboard
			return pressGame(downX < 320 ? Key::LEFT : Key::RIGHT);	// the value's left or right half steps it
		}
		return NONE;
	}

	void Settings::renderGame()
	{
		SPal p = pal(light);
		const std::string menu = data + "/settings.conf";
		std::string global = getConfValue(menu, "nds_emulator", getConfValue("/etc/banana/settings.conf", "nds_emulator", "superdrastic"));
		if (global != "bananads") global = "superdrastic";
		// the emulator this game runs in, and Settings' shader for that emulator (each keeps its own)
		const std::string emu = pgEmu ? GAME_EMUS[pgEmu] : global;
		std::string gsh = emu == "bananads" ? getConfValue("/data/bananads/bananads.conf", "kms.shader", "") : "";
		if (gsh.empty()) gsh = getConfValue(data + "/superdrastic.conf", "DSFLIP_SHADER", "none");
		std::string gshName = gsh;
		for (size_t i = 1; i < pgShaderValues.size(); i++) if (pgShaderValues[i] == gsh) gshName = pgShaderNames[i];
		const std::string emuValue = pgEmu ? std::string(GAME_EMU_NAMES[pgEmu]) : std::string("As in Settings (") + (global == "bananads" ? "BananaDS" : "SuperDrastic") + ")";
		const std::string shValue = pgShader ? pgShaderNames[pgShader] : "As in Settings: " + gshName;	// (names have brackets of their own)
		const bool plus = readLine("/etc/banana/board") == "rgds-plus";
		// Settings' Threaded 3D (DraStic's drastic.cfg) and the microphone of the emulator this game runs in
		const std::string g3d = getConfValue("/data/drastic/config/drastic.cfg", "threaded_3d", getConfValue("/usr/share/drastic/config/drastic.cfg", "threaded_3d", "0"));
		const std::string gmic = emu == "bananads" ? getConfValue("/data/bananads/bananads.conf", "mic.threshold", "0.05")
			: getConfValue(data + "/superdrastic.conf", "DSHOOK_MIC_THRESH", "0");
		std::string gmicName = gmic;
		const float* scale = emu == "bananads" ? MIC_BANANADS : MIC_SUPERDRASTIC;	// (Settings' value in that emulator's scale)
		for (int i = 1; i < 5; i++) if (std::fabs(atof(gmic.c_str()) - scale[i]) < 0.001) gmicName = GAME_MIC_NAMES[i];
		// BananaDS: Settings' Microphone Off is mic.source = none (its threshold 0 is the gate off, every sound through)
		if (emu == "bananads" && getConfValue("/data/bananads/bananads.conf", "mic.source", "device") == "none") gmicName = "Off";
		else if (emu == "bananads" && atof(gmic.c_str()) == 0) gmicName = "Gate off";
		const std::string v3d = pg3d ? std::string(GAME_3D_NAMES[pg3d]) : "As in Settings (" + std::string(g3d == "1" ? "On" : "Off") + ")";
		const std::string vmic = pgMic ? std::string(GAME_MIC_NAMES[pgMic]) : "As in Settings (" + gmicName + ")";

		const int n = (int)pgRows.size();
		const PgRow row = n ? pgRows[std::min(pgRow, n - 1)] : PG_NAME;
		const std::string nameValue = gameName.empty() ? "As scraped" : gameName, sortValue = gameSort.empty() ? "By its name" : gameSort;
		auto label = [](PgRow r) { return r == PG_EMU ? "Emulator" : r == PG_SHADER ? "Shader" : r == PG_3D ? "Threaded 3D" : r == PG_MIC ? "Microphone"
			: r == PG_NAME ? "Name" : r == PG_SORT ? "Sort title" : "Box art"; };
		auto value = [&](PgRow r) { return r == PG_EMU ? emuValue : r == PG_SHADER ? shValue : r == PG_3D ? v3d : r == PG_MIC ? vmic
			: r == PG_NAME ? nameValue : r == PG_SORT ? sortValue : std::string(pgArtDone ? "Fetched" : "Find it by name"); };

		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon(pgNds ? "nds" : "gamepad", 50, 56, 3);
		text(row == PG_EMU ? "This game's emulator" : row == PG_SHADER ? "This game's shader" : row == PG_3D ? "This game's 3D" : row == PG_MIC ? "This game's microphone" : row == PG_NAME ? "This game's name" : row == PG_SORT ? "This game's sort title" : "This game's box art",
			true, 17, 170, 62, p.muted, true, 1.4f);
		text(fit(pgTitle, true, 30, 420), true, 30, 170, 88, p.ink);
		text(fit(value(row), true, 22, 420), true, 22, 170, 130, p.accent);
		std::string help = row == PG_EMU
			? "SuperDrastic: DraStic onto both panels, the fastest. BananaDS: Banana's own emulator (melonDS's core), the most "
			  "accurate. The choice is for this game only; the others follow Settings > Nintendo DS > Emulator."
			: row == PG_SHADER ? std::string("The picture's shader for this game only, in either emulator; the others follow Settings > Nintendo DS > Shader. "
			  "None scales smoothly and costs no GPU work.") + (plus ? PLUS_SHADER_NOTE : "")
			: row == PG_3D ? "SuperDrastic (DraStic) only. On draws the DS's 3D beside the game, the faster way; Off draws it in step with "
			  "the game, for a game whose 3D shows on the wrong screen or a frame late (the Dragon Quest games). The others follow "
			  "Settings > Nintendo DS > Threaded 3D."
			: row == PG_MIC ? "How loud a word or a blow must be to reach this game, in either emulator: High hears a quiet voice, Low "
			  "only a loud one, Off none. For a game that needs the microphone (Nintendogs) or one it bothers."
			: row == PG_NAME ? "The name the menu shows for this game. A types it; an empty one goes back to the scraped name. Kept in "
			  "banana/game-titles.tsv, which scraping the box art again leaves alone."
			: row == PG_SORT ? "The library's order goes by this instead of the name: Dragon Quest 04, 05 and 09 keep IV, V and IX in their order. "
			  "A types it; an empty one sorts by the name again."
			: !message.empty() ? message
			: "For a game the box art scrape got wrong or didn't find (a translation, a hack, another name): A types its name as "
			  "libretro's collection has it, then you pick the right one from what it finds. Wi-Fi needed.";
		wrapped(help, 20, 50, 196, 540, 7, p.muted);
		be->endScreen();

		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("bubble", false, 18, 14, 604, 54);
		text("This game", true, 22, 38, 28, p.ink);
		for (int r = pgTop; r < n && r < pgTop + ROWS; r++)
		{
			bool on = r == pgRow;
			float y = 82 + (r - pgTop) * 64;
			box(on ? "pick" : "slot", on, 18, y, 604, 56);
			Color c = on ? p.onPick : p.muted;
			text(label(pgRows[r]), true, 20, 38, y + 16, c);
			bool typed = pgRows[r] == PG_NAME || pgRows[r] == PG_SORT || pgRows[r] == PG_ART;
			std::string v = fit(value(pgRows[r]), true, 19, typed ? 400 : 380);
			float vw = text(v, true, 19, 0, 0, p.ink, false, 0, true);
			if (typed) { text(v, true, 19, 586 - vw, y + 17, c); continue; }
			arrow(true, 576, y + 21, c);
			text(v, true, 19, 566 - vw, y + 17, c);
			arrow(false, 556 - vw - 2 - std::round(13 / 2.0f + 1), y + 21, c);
		}
		float bw = faceWidth("B", "Back");
		face("B", "Back", 18, bw);
		const char* act = row == PG_NAME || row == PG_SORT ? "Type" : row == PG_ART ? "Find" : "Done";
		float aw = faceWidth("A", act);
		face("A", act, 622 - aw, aw);
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

	void Settings::openSearch(const std::string& start)
	{
		open();
		startEntry(ENTRY_SEARCH, "Search games", "Type a name", LIST, start);
	}

	std::string Settings::account(const char* key)
	{
		return getConfValue(data + "/accounts.conf", key, "");
	}

	// the file sharing login (the share script's defaults where accounts.conf has none)
	std::string Settings::shareUser()
	{
		std::string u = account("smb_user");
		return u.empty() ? "banana" : u;
	}
	// the DS's name (#30): BananaDS's firmware.nickname, else DraStic's firmware.username when it isn't its default;
	// "" for none
	std::string Settings::dsName()
	{
		std::string n = getConfValue("/data/bananads/bananads.conf", "firmware.nickname", "");
		if (n.empty()) n = getConfValue("/data/drastic/config/drastic.cfg", "firmware.username", "");
		return n == getConfValue("/usr/share/drastic/config/drastic.cfg", "firmware.username", "Dr DraStic") ? "" : n;
	}
	void Settings::setDsName(const std::string& name)
	{
		// DraStic's own settings made first (as save() does); empty: its default name back, and BananaDS's line gone
		if (access("/data/drastic/config/drastic.cfg", F_OK) != 0)
		{
			if (system("mkdir -p /data/drastic/config && cp -n /usr/share/drastic/config/drastic.cfg /data/drastic/config/ 2>/dev/null") != 0) {}
		}
		setConfValue("/data/drastic/config/drastic.cfg", "firmware.username",
			name.empty() ? getConfValue("/usr/share/drastic/config/drastic.cfg", "firmware.username", "Dr DraStic") : name);
		if (system("mkdir -p /data/bananads") != 0) {}
		const std::string bc = "/data/bananads/bananads.conf";
		if (!name.empty()) setConfValue(bc, "firmware.nickname", name);
		else if (!getConfValue(bc, "firmware.nickname", "").empty())
		{
			std::ifstream f(bc);
			std::string ln, out;
			while (std::getline(f, ln)) if (trim(ln.substr(0, ln.find('='))) != "firmware.nickname") out += ln + "\n";
			f.close();
			std::ofstream(bc + ".new") << out;
			rename((bc + ".new").c_str(), bc.c_str());
		}
	}

	std::string Settings::sharePassword()
	{
		std::string p = account("smb_password");
		return p.empty() ? "linux" : p;
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
			startRaCheck(account("ra_user"), pw);	// says at once whether the login works
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
		case ENTRY_SMB_USER:
		{
			bool ok = !pw.empty() && pw.size() <= 32;
			for (unsigned char c : pw) if (!isalnum(c) && c != '.' && c != '_' && c != '-') ok = false;
			if (!ok) { message = "A name of letters, digits, dots, dashes or underscores, at most 32."; return MOVED; }
			smbNewUser = pw;	// (written with the password: a B out of that step leaves both as they were)
			startEntry(ENTRY_SMB_PASS, "File sharing password for", pw, entryBack, sharePassword());
			return MOVED;
		}
		case ENTRY_SMB_PASS:
			pw = trim(pw);	// (as the share and this screen read it back: spaces at either end aren't kept)
			if (pw.empty()) { message = "The password can't be empty: Sharing login > Guest is for a share with none."; return MOVED; }
			if (!smbNewUser.empty()) setConfValue(data + "/accounts.conf", "smb_user", smbNewUser, "=");
			setConfValue(data + "/accounts.conf", "smb_password", pw, "=");
			smbNewUser.clear();
			spawn("/usr/libexec/banana/share", "restart");	// the new login now, if it is on
			pw.clear();
			page = entryBack;
			message.clear();
			return CHANGED;
		case ENTRY_DS_NAME:
		{
			std::string v = trim(pw);
			if (v.size() > 10) { message = "The DS takes 10 characters at most; this one has " + std::to_string(v.size()) + "."; return MOVED; }
			setDsName(v);
			pw.clear();
			page = entryBack;
			message.clear();
			return CHANGED;
		}
		case ENTRY_ART_SEARCH:
		{
			// the collection's names for these words, looked for in the background (the list page waits for them)
			artWords = trim(pw);
			pw.clear();
			if (artWords.empty()) { page = GAME; return MOVED; }
			auto q = [](const std::string& s) { std::string r = "'"; for (char c : s) { if (c == '\'') r += "'\\''"; else r += c; } return r + "'"; };
			std::string sys = gameKey.substr(0, gameKey.find('/'));
			artNames.clear(); asel = atop = 0; artMatching = false;
			message = "Looking in libretro's collection...";
			artJob.start(q(scrapeTool()) + " --search " + q(sys) + " " + q(artWords) + " 2>/dev/null", sync);
			page = ARTPICK;
			return MOVED;
		}
		case ENTRY_GAME_NAME: case ENTRY_GAME_SORT:
		{
			// the same as the scraped name: none of its own (spaces at either end don't count, as in the library)
			std::string v = trim(pw);
			// (the scraped name typed again, or cleared: none of its own, and the page shows the scraped one)
			if (entry == ENTRY_GAME_NAME && !pgScraped.empty() && v == trim(pgScraped)) v.clear();
			(entry == ENTRY_GAME_NAME ? gameName : gameSort) = v;
			if (entry == ENTRY_GAME_NAME) pgTitle = v.empty() && !pgScraped.empty() ? pgScraped : v.empty() ? pgTitle : v;
			pw.clear();
			page = GAME;
			message.clear();
			return TITLES;
		}
		case ENTRY_SEARCH:
			searchText = pw;
			pw.clear();
			close();
			return SEARCH;
		}
		return NONE;
	}

	// ---- the RetroAchievements account check --------------------------------------------------------------------
	// The server's own login (dorequest.php r=login2, what rcheevos sends) with the name and password just typed: a
	// pop-up says whether they work before the first game does. libcurl is dlopen()ed (busybox's wget has no HTTPS).
	namespace
	{
		struct Curl
		{
			void* (*init)(void) = nullptr;
			int (*setopt)(void*, int, ...) = nullptr;
			int (*perform)(void*) = nullptr;
			int (*getinfo)(void*, int, ...) = nullptr;
			void (*cleanup)(void*) = nullptr;
			char* (*escape)(void*, const char*, int) = nullptr;
			void (*freep)(void*) = nullptr;
			bool load()
			{
				void* l = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
				if (!l) return false;
				init = (void* (*)(void))dlsym(l, "curl_easy_init");
				setopt = (int (*)(void*, int, ...))dlsym(l, "curl_easy_setopt");
				perform = (int (*)(void*))dlsym(l, "curl_easy_perform");
				getinfo = (int (*)(void*, int, ...))dlsym(l, "curl_easy_getinfo");
				cleanup = (void (*)(void*))dlsym(l, "curl_easy_cleanup");
				escape = (char* (*)(void*, const char*, int))dlsym(l, "curl_easy_escape");
				freep = (void (*)(void*))dlsym(l, "curl_free");
				return init && setopt && perform && getinfo && cleanup && escape && freep;
			}
		};
		size_t onBody(char* p, size_t sz, size_t n, void* u) { ((std::string*)u)->append(p, sz * n); return sz * n; }
		// "Key":"value" or "Key":123 in the server's JSON (enough for its flat login reply)
		std::string jsonField(const std::string& j, const std::string& key)
		{
			size_t at = j.find("\"" + key + "\"");
			if (at == std::string::npos) return "";
			at = j.find(':', at);
			if (at == std::string::npos) return "";
			at++;
			while (at < j.size() && j[at] == ' ') at++;
			if (at < j.size() && j[at] == '"')
			{
				std::string v;
				for (size_t i = at + 1; i < j.size() && j[i] != '"'; i++) v += j[i] == '\\' && i + 1 < j.size() ? j[++i] : j[i];
				return v;
			}
			size_t e = at;
			while (e < j.size() && j[e] != ',' && j[e] != '}') e++;
			return j.substr(at, e - at);
		}
	}

	void Settings::startRaCheck(const std::string& user, const std::string& pass)
	{
		if (const char* e = getenv("BANANA_RA_CHECK"); e && !strcmp(e, "0")) return;	// (the headless test: no server)
		auto c = std::make_shared<RaCheck>();
		raCheck = c;
		toast("RetroAchievements", "Checking the account " + user + "...", 0x6ab0ff, 0, 30000);
		std::thread([c, user, pass]
		{
			int kind = 3;
			std::string l2 = "No connection: check Wi-Fi, then try again from the setup";
			Curl cu;
			if (cu.load())
				if (void* h = cu.init())
				{
					enum { WRITEDATA = 10001, URL = 10002, POSTFIELDS = 10015, USERAGENT = 10018, WRITEFUNCTION = 20011, TIMEOUT = 13,
						NOSIGNAL = 99, RESPONSE_CODE = 0x200002 };
					char* eu = cu.escape(h, user.c_str(), (int)user.size());
					char* ep = cu.escape(h, pass.c_str(), (int)pass.size());
					std::string post = std::string("r=login2&u=") + (eu ? eu : "") + "&p=" + (ep ? ep : "");
					if (eu) cu.freep(eu);
					if (ep) cu.freep(ep);
					std::string body;
					long status = 0;
					cu.setopt(h, URL, "https://retroachievements.org/dorequest.php");
					cu.setopt(h, POSTFIELDS, post.c_str());
					cu.setopt(h, USERAGENT, "BananaOS");
					cu.setopt(h, NOSIGNAL, 1L);
					cu.setopt(h, TIMEOUT, 20L);
					cu.setopt(h, WRITEFUNCTION, onBody);
					cu.setopt(h, WRITEDATA, &body);
					if (cu.perform(h) == 0)
					{
						cu.getinfo(h, RESPONSE_CODE, &status);
						if (jsonField(body, "Success") == "true")
						{
							kind = 1;
							std::string name = jsonField(body, "User"), score = jsonField(body, "Score");
							l2 = "Logged in as " + (name.empty() ? user : name) + (score.empty() || score == "0" ? "" : " \xc2\xb7 " + score + " points");
						}
						else
						{
							kind = 2;
							std::string err = jsonField(body, "Error");
							bool wrong = status == 401 || err.find("nvalid") != std::string::npos || err.find("assword") != std::string::npos;
							l2 = wrong ? "Wrong account name or password" : !err.empty() ? err : "The server said no (" + std::to_string(status) + ")";
						}
					}
					cu.cleanup(h);
				}
			std::lock_guard<std::mutex> l(c->mu);
			c->kind = kind;
			c->l2 = l2;
			c->done = true;
		}).detach();
	}

	void Settings::toast(const std::string& l1, const std::string& l2, uint32_t accent, int icon, double ms)
	{
		toast1 = l1; toast2 = l2; toastAccent = accent; toastIcon = icon;
		toastUntil = now + ms;
	}

	// ROCKNIXDS's pop-up card (SuperDrastic's ui.c render_popup): across the top of the top panel, a tile in the accent
	// colour with its pixel sign, the first line in the accent colour and the second in light text
	void Settings::drawToast(double t)
	{
		if (!mOpen || toastUntil <= t) return;
		static const char* const SIGN[3][9] = {
			{ "...###...", "..#####..", "..##.##..", "....##...", "...##....", "...##....", ".........", "...##....", "...##...." },
			{ ".........", "........#", ".......##", "......##.", "#....##..", "##..##...", ".####....", "..##.....", "........." },
			{ "##.....##", ".##...##.", "..##.##..", "...###...", "...###...", "..##.##..", ".##...##.", "##.....##", "........." } };
		const Color light = Color::hex(0xf1ede2), acc = Color::hex(toastAccent);
		box("osd", false, 8, 4, 624, 64);
		float bs = 48, tx = 16, ty = 12;
		be->fill(std::round(X(tx)), std::round(X(ty)), std::round(X(bs)), std::round(X(bs)), acc);
		int k = std::max(1, (int)std::lround(X(bs) / 14));
		float ox = std::round(X(tx + bs / 2)) - 9 * k / 2.0f, oy = std::round(X(ty + bs / 2)) - 9 * k / 2.0f;
		for (int r = 0; r < 9; r++)
			for (int c = 0; c < 9; c++)
				if (SIGN[toastIcon][r][c] == '#') be->fill(std::round(ox + c * k), std::round(oy + r * k), (float)k, (float)k, Color::hex(0x12181f));
		text(fit(toast1, true, 17, 540), true, 17, 78, 14, acc);
		text(fit(toast2, false, 20, 540), false, 20, 78, 37, light);
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

	// the time on the clock now, in the time zone in effect (save() sets TZ when the zone changes)
	static std::string clockNow()
	{
		time_t t = time(nullptr);
		struct tm lt;
		localtime_r(&t, &lt);
		char b[8];
		snprintf(b, sizeof b, "%02d:%02d", lt.tm_hour, lt.tm_min);
		return b;
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
			wsel = (wsel + (k == Key::UP ? -1 : 1) + SETUP_STEPS) % SETUP_STEPS;
			message.clear();
			return MOVED;
		case Key::LEFT: case Key::RIGHT:
			if (wsel != 1 || !tz) return NONE;
			{
				int n = (int)tz->values.size();
				tz->at = (tz->at + (k == Key::LEFT ? -1 : 1) + n) % n;
				save(*tz);
				message.clear();
			}
			return CHANGED;
		case Key::A:
			switch (wsel)
			{
			case 0: openNetworks(); return MOVED;
			case 1: return pressSetup(Key::RIGHT);
			case 2: startEntry(ENTRY_DS_NAME, "The DS's name", "10 characters at most", SETUP, dsName()); return MOVED;
			case 3: startEntry(ENTRY_RA_USER, "RetroAchievements", "Account name", SETUP, account("ra_user")); return MOVED;
			case 4: startScrape(); return MOVED;	// box art: on its own, while the setup goes on
			default:
				// the clock is UTC until a zone is picked, and a setup finished with the first entry left the menu's clock
				// hours off: the first A on Finish goes to the time zone (with the time it would show), a second finishes
				if (tz && tz->at == 0 && !tzAsked)
				{
					tzAsked = true;
					wsel = 1;
					message = "The time zone is still UTC, so the clock shows " + clockNow() + ". Left and right pick yours; Finish again to keep UTC.";
					return MOVED;
				}
				setupDone();
				return CLOSED;
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
		static const char* names[SETUP_STEPS] = { "Wi-Fi", "Time zone", "DS name", "RetroAchievements", "Box art", "Finish" };
		std::string status[SETUP_STEPS] = {
			wstat.connected ? "Connected to " + wstat.ssid : wifiOn() ? "Not connected" : "Off",
			tz ? tz->names[tz->at] + " (" + clockNow() + ")" : "",	// (the time it gives, so a wrong zone shows)
			dsName().empty() ? "Not set" : dsName(),
			ra.empty() ? "Not signed in" : ra,
			scrapeShort(),
			"" };
		static const char* helps[SETUP_STEPS] = {
			"Your home network: the games' achievements, the clock and the art need it. A picks it from the networks around; the password goes in on the keyboard.",
			"The clock on the top screen, and the games' own clocks. Left and right change it.",
			"Your name on the DS, as a real one asks for it the first time: Mario Kart's records, Pictochat, a game's save. A types it (10 characters at most). Leave it and the emulators use their own.",
			"Your retroachievements.org account: DraStic and RetroArch unlock achievements with it. A types the name, then the password. Leave it if you have none.",
			"Covers, title screens, genres and years for the games on the card, from libretro's free collections: no account. A starts it (Wi-Fi first); it keeps going while you finish the setup, and the menu shows the art as it comes. Settings > System > Box art runs it again for new games.",
			"Everything here is in the settings too (Start in the menu), and this setup is under System > First-time setup. A finishes." };
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon("gear", 50, 56, 3);
		text("First-time setup", true, 17, 170, 62, p.muted, true, 1.4f);
		text(names[wsel], true, 30, 170, 88, p.ink);
		if (!status[wsel].empty()) text(fit(status[wsel], true, 22, 420), true, 22, 170, 130, p.accent);
		if (wsel == 4 && scraping())
		{
			wrapped("Fetching covers and title screens. It carries on while you finish the setup.", 20, 50, 196, 540, 4, p.muted);
			scrapeBar(50, 330, 540);
		}
		else wrapped(message.empty() ? helps[wsel] : message, 20, 50, 196, 540, 7, p.muted);
		be->endScreen();

		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("bubble", false, 18, 14, 604, 54);
		text("Welcome to Banana", true, 22, 38, 28, p.ink);
		std::string count = std::to_string(wsel + 1) + " / " + std::to_string(SETUP_STEPS);
		float cw = text(count, true, 17, 0, 0, p.muted, false, 0, true);
		text(count, true, 17, 602 - cw, 31, p.muted);
		for (int r = 0; r < SETUP_STEPS; r++)
		{
			bool on = r == wsel;
			float y = 82 + r * SETUP_PITCH;	// (six rows a little closer than the lists' five)
			box(on ? "pick" : "slot", on, 18, y, 604, 50);
			Color c = on ? p.onPick : p.muted;
			text(names[r], true, 20, 38, y + 13, c);
			if (status[r].empty()) continue;
			std::string v = fit(status[r], true, 19, 300);
			float vw = text(v, true, 19, 0, 0, c, false, 0, true);
			if (r == 1) { arrow(true, 576, y + 18, c); text(v, true, 19, 566 - vw, y + 14, c); arrow(false, 556 - vw - 2 - std::round(13 / 2.0f + 1), y + 18, c); }
			else text(v, true, 19, 586 - vw, y + 14, c);
		}
		float bw = faceWidth("B", "Skip");
		face("B", "Skip", 18, bw);
		const char* act = wsel == 5 ? "Finish" : wsel == 1 ? "Next zone" : wsel == 4 ? (scraping() ? "Running" : "Start") : wsel == 2 ? "Type" : "Open";
		float aw = faceWidth("A", act);
		face("A", act, 622 - aw, aw);
		be->endScreen();
	}

	// ---- box art: banana-scrape --------------------------------------------------------------------------------
	bool Settings::pollScrape(double t)
	{
		if (t < scrapeNext) return false;
		scrapeNext = t + 500;
		std::string ln = readLine("/run/banana/scrape.status");
		// a run that is gone (banana-scrape crashed, was killed, or never started): "running" with no word for 90 s
		// and no scraper alive (its pid in awake.pid) is over, so the row can be started again (it said Running until
		// a restart)
		struct stat st;
		if (ln.compare(0, 8, "running ") == 0 && stat("/run/banana/scrape.status", &st) == 0 && time(nullptr) - st.st_mtime > 90)
		{
			int pid = 0;
			std::ifstream("/run/banana/awake.pid") >> pid;
			if (pid <= 0 || kill(pid, 0) != 0)
			{
				ln = "error stopped";
				std::ofstream("/run/banana/scrape.status") << ln << "\n";
			}
		}
		if (ln == scrapeLine) return false;
		bool was = scraping();
		scrapeLine = ln;
		if (was && ln.compare(0, 5, "done ") == 0) libChanged = true;	// the art is there: the library again
		return true;
	}

	void Settings::startScrape()
	{
		if (scraping()) return;
		if (!wifiOn()) { message = "Wi-Fi is off: switch it on in Network first."; return; }
		std::ofstream("/run/banana/scrape.status") << "running 0 0 |starting\n";
		scrapeLine.clear();
		scrapeNext = 0;
		const char* prog = getenv("BANANA_SCRAPE");	// (another build of it, for testing)
		spawn(prog && *prog ? prog : "/usr/bin/banana-scrape", nullptr);
	}

	std::string Settings::scrapeShort() const
	{
		std::istringstream in(scrapeLine);
		std::string state;
		in >> state;
		if (state == "running") { int a = 0, b = 0; in >> a >> b; return b ? std::to_string(a) + " / " + std::to_string(b) : "Starting"; }
		if (state == "done") { int a = 0, b = 0; in >> a >> b; return std::to_string(a) + " of " + std::to_string(b) + " found"; }
		if (state == "error") return scrapeLine.find("connection") != std::string::npos ? "No connection" : "Didn't work";
		return "Not yet";
	}

	void Settings::scrapeBar(float x, float y, float w)
	{
		SPal p = pal(light);
		std::istringstream in(scrapeLine);
		std::string state, rest;
		int a = 0, b = 0;
		in >> state >> a >> b;
		std::getline(in, rest);
		rest = trim(rest);
		std::string sys = rest.substr(0, rest.find('|')), game = rest.find('|') == std::string::npos ? "" : rest.substr(rest.find('|') + 1);
		be->fill(X(x), X(y), X(w), X(14), p.line);
		if (b > 0) be->fill(X(x + 2), X(y + 2), X((w - 4) * a / b), X(10), p.accent);
		text(fit(sys.empty() ? "Starting" : sys, true, 17, w), true, 17, x, y + 26, p.ink);
		text(fit(game, false, 16, w), false, 16, x, y + 52, p.muted);
	}

	// ---- the game panel -----------------------------------------------------------------------------------------
	// RetroArch logs (--verbose, run-retroarch's game panel mode) what its RetroAchievements client does: the hash it
	// made, the game it identified ("Identified game: 264 "EarthBound" (hash)") and each unlock ("Awarding achievement
	// 1234: Title"). The game's list, the account's unlocks and the badges come from the server on a thread, with
	// the account RetroArch keeps in its retroarch.cfg (cheevos_username, cheevos_token).
	namespace
	{
		struct RaAch { uint32_t id = 0; std::string title, desc, badge; unsigned points = 0; };
		// the objects of an array in the server's JSON ("Achievements":[{...},{...}]), as text
		std::vector<std::string> jsonObjects(const std::string& j, const std::string& key)
		{
			std::vector<std::string> out;
			size_t at = j.find("\"" + key + "\"");
			if (at == std::string::npos) return out;
			at = j.find('[', at);
			if (at == std::string::npos) return out;
			int depth = 0;
			bool str = false;
			size_t start = 0;
			for (size_t i = at + 1; i < j.size(); i++)
			{
				char c = j[i];
				if (str) { if (c == '\\') i++; else if (c == '"') str = false; continue; }
				if (c == '"') str = true;
				else if (c == '{') { if (depth++ == 0) start = i; }
				else if (c == '}') { if (--depth == 0) out.push_back(j.substr(start, i - start + 1)); }
				else if (c == ']' && depth == 0) break;
			}
			return out;
		}
		std::string cfgValue(const std::string& path, const std::string& key)
		{
			std::string v = getConfValue(path, key, "");
			return v;
		}
		// a POST to the server's dorequest.php; "" when it can't be reached
		std::string raPost(const std::string& fields)
		{
			Curl cu;
			if (!cu.load()) return "";
			void* h = cu.init();
			if (!h) return "";
			enum { WRITEDATA = 10001, URL = 10002, POSTFIELDS = 10015, USERAGENT = 10018, WRITEFUNCTION = 20011, TIMEOUT = 13, NOSIGNAL = 99 };
			std::string body;
			cu.setopt(h, URL, "https://retroachievements.org/dorequest.php");
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
		bool fetchFile(const std::string& url, const std::string& path)
		{
			Curl cu;
			if (!cu.load()) return false;
			void* h = cu.init();
			if (!h) return false;
			enum { WRITEDATA = 10001, URL = 10002, USERAGENT = 10018, WRITEFUNCTION = 20011, TIMEOUT = 13, NOSIGNAL = 99, RESPONSE_CODE = 0x200002 };
			std::string body;
			long status = 0;
			cu.setopt(h, URL, url.c_str());
			cu.setopt(h, USERAGENT, "BananaOS/0.2 rcheevos");
			cu.setopt(h, NOSIGNAL, 1L);
			cu.setopt(h, TIMEOUT, 20L);
			cu.setopt(h, WRITEFUNCTION, onBody);
			cu.setopt(h, WRITEDATA, &body);
			bool ok = cu.perform(h) == 0;
			if (ok) cu.getinfo(h, RESPONSE_CODE, &status);
			cu.cleanup(h);
			if (!ok || status != 200 || body.size() < 8) return false;
			std::string tmp = path + ".part";
			FILE* f = fopen(tmp.c_str(), "wb");
			if (!f) return false;
			fwrite(body.data(), 1, body.size(), f);
			fclose(f);
			return rename(tmp.c_str(), path.c_str()) == 0;
		}
		const char* const BADGES = "/data/banana/cache/ra-badges";
		const char* const IC_SAVE[12] = { "##########..", "#..#####.##.", "#..#####..##", "#..#####...#", "#..........#", "#..........#",
			"#.########.#", "#.#......#.#", "#.#......#.#", "#.#......#.#", "#.########.#", "############" };
		const char* const IC_LOAD[12] = { "............", ".####.......", "#....#......", "#.....######", "#..........#", "#....##....#",
			"#....##....#", "#..######..#", "#...####...#", "#....##....#", "#..........#", "############" };
		const char* const IC_LIST[12] = { "............", "##.#########", "##.#########", "............", "............", "##.#########",
			"##.#########", "............", "............", "##.#########", "##.#########", "............" };
		const char* const IC_FFWD[12] = { "............", "#.....#.....", "##....##....", "###...###...", "####..####..", "#####.#####.",
			"#####.#####.", "####..####..", "###...###...", "##....##....", "#.....#.....", "............" };
		const char* const IC_EXIT[12] = { "######......", "#....#......", "#....#...#..", "#....#...##.", "#....#######", "#....#######",
			"#....#...##.", "#....#...#..", "#....#......", "#....#......", "######......", "............" };
		struct GTile { const char* label; const char* keys; const char* const* icon; };
		const GTile GTILES[5] = { { "Save", "F + R", IC_SAVE }, { "Load", "F + L", IC_LOAD }, { "Menu", "F + X", IC_LIST },
			{ "Fast", "F + R2", IC_FFWD }, { "Quit", "F + START", IC_EXIT } };
	}

	struct Settings::RaWatch
	{
		std::mutex mu;
		// 0 waiting for RetroArch's client, 1 looking the game up, 2 ready, 3 no achievements for this game, 4 no
		// account, 5 no connection
		int state = 0;
		std::string game, hash;
		uint32_t gameId = 0;
		std::vector<RaAch> all;
		std::map<uint32_t, long long> unlocked;		// id -> when (unix time)
		std::map<std::string, bool> badgeReady;		// badge name -> its file is in BADGES
		int gen = 0;
		std::string user, token;
		time_t cfgSeen = 0;	// retroarch.cfg's mtime when the token was last looked for in it
		long long pos = 0;
		std::string part;
		double since = 0;
		// the badge's file, fetched on a thread when it isn't cached yet
		void wantBadge(const std::shared_ptr<RaWatch>& self, const std::string& name)
		{
			if (name.empty() || badgeReady.count(name)) return;
			std::string path = std::string(BADGES) + "/" + name + ".png";
			if (access(path.c_str(), R_OK) == 0) { badgeReady[name] = true; return; }
			badgeReady[name] = false;
			std::thread([self, name, path]
			{
				if (system((std::string("mkdir -p ") + BADGES).c_str()) != 0) {}
				bool ok = fetchFile("https://media.retroachievements.org/Badge/" + name + ".png", path);
				std::lock_guard<std::mutex> l(self->mu);
				if (ok) { self->badgeReady[name] = true; self->gen++; }
			}).detach();
		}
		static void fetch(std::shared_ptr<RaWatch> w)
		{
			std::string user, token, hash;
			uint32_t id;
			{
				std::lock_guard<std::mutex> l(w->mu);
				user = w->user; token = w->token; hash = w->hash; id = w->gameId;
			}
			Curl cu;
			std::string g = std::to_string(id);
			std::string patch = raPost("r=patch&u=" + user + "&t=" + token + "&g=" + g);
			fprintf(stderr, "game panel: game %s: the server sent %zu bytes (%s)\n", g.c_str(), patch.size(),
				patch.empty() ? "no reply" : jsonField(patch, "Success") == "true" ? "ok" : jsonField(patch, "Error").c_str());
			if (patch.empty() || jsonField(patch, "Success") != "true")
			{
				std::lock_guard<std::mutex> l(w->mu);
				w->state = patch.empty() ? 5 : 3;
				w->gen++;
				return;
			}
			std::vector<RaAch> all;
			for (auto& o : jsonObjects(patch, "Achievements"))
			{
				if (jsonField(o, "Flags") != "3") continue;	// core achievements only (5: unofficial)
				RaAch a;
				a.id = (uint32_t)strtoul(jsonField(o, "ID").c_str(), nullptr, 10);
				if (a.id == 101000001) continue;	// the server's "Warning: Unknown Emulator", not one of the game's
				a.title = jsonField(o, "Title");
				a.desc = jsonField(o, "Description");
				a.badge = jsonField(o, "BadgeName");
				a.points = (unsigned)atoi(jsonField(o, "Points").c_str());
				all.push_back(a);
			}
			// (softcore: the panel only reads; RetroArch keeps its own session, and a hardcore one from a client the
			// server doesn't know gets its warning)
			std::string sess = raPost("r=startsession&u=" + user + "&t=" + token + "&g=" + g + "&h=0" + (hash.empty() ? "" : "&m=" + hash));
			std::map<uint32_t, long long> un;
			for (const char* k : { "Unlocks", "HardcoreUnlocks" })
				for (auto& o : jsonObjects(sess, k))
					un[(uint32_t)strtoul(jsonField(o, "ID").c_str(), nullptr, 10)] = atoll(jsonField(o, "When").c_str());
			std::lock_guard<std::mutex> l(w->mu);
			w->all = all;
			for (auto& u : un) if (!w->unlocked.count(u.first)) w->unlocked[u.first] = u.second;
			w->state = all.empty() ? 3 : 2;
			w->gen++;
		}
		// RetroArch's log since the last look
		void poll(const std::shared_ptr<RaWatch>& self, double nowMs)
		{
			FILE* f = fopen("/run/banana/retroarch.log", "r");
			if (!f) return;
			// a new log (shorter than what was read): from its start
			if (fseek(f, 0, SEEK_END) == 0 && ftell(f) < (long)pos) { pos = 0; part.clear(); }
			if (fseek(f, (long)pos, SEEK_SET) != 0) { fclose(f); return; }
			char buf[8192];
			size_t n;
			while ((n = fread(buf, 1, sizeof buf, f)) > 0) { part.append(buf, n); pos += (long long)n; }
			fclose(f);
			size_t nl;
			while ((nl = part.find('\n')) != std::string::npos)
			{
				std::string ln = part.substr(0, nl);
				part.erase(0, nl + 1);
				if (ln.find("[RCHEEVOS]") == std::string::npos) continue;
				std::lock_guard<std::mutex> l(mu);
				size_t at;
				if ((at = ln.find("Generated hash ")) != std::string::npos) hash = ln.substr(at + 15, 32);
				else if ((at = ln.find("Identified game: ")) != std::string::npos)
				{
					gameId = (uint32_t)strtoul(ln.c_str() + at + 17, nullptr, 10);
					size_t q0 = ln.find('"', at), q1 = ln.rfind('"');
					if (q0 != std::string::npos && q1 > q0) game = ln.substr(q0 + 1, q1 - q0 - 1);
					if (!gameId) { state = 3; gen++; continue; }
					fprintf(stderr, "game panel: RetroArch identified game %u (%s)\n", gameId, game.c_str());
					state = 1; gen++;
					std::thread(fetch, self).detach();
				}
				else if (ln.find("Unknown game") != std::string::npos || ln.find("not recognized") != std::string::npos) { state = 3; gen++; }
				else if (ln.find("Login failed") != std::string::npos && state < 2) { state = ln.find("No response") != std::string::npos ? 5 : 4; gen++; }
				else if ((at = ln.find("Awarding achievement ")) != std::string::npos)
				{
					uint32_t id = (uint32_t)strtoul(ln.c_str() + at + 21, nullptr, 10);
					if (id == 101000001) continue;	// (the server's warning)
					unlocked[id] = (long long)time(nullptr);
					gen++;
				}
			}
			std::lock_guard<std::mutex> l(mu);
			if (state == 0 && since > 0 && nowMs - since > 30000) { state = 5; gen++; fprintf(stderr, "game panel: no game from RetroArch's log in 30 s\n"); }	// RetroArch's client never got the game
		}
	};

	void Settings::gameOpen(const std::string& title, const std::string& system, int screen, bool touch)
	{
		gTitle = title; gSystem = system; gScreen = screen; gTouch = touch;
		gHotkeys = getConfValue(data + "/settings.conf", "ra_hotkeys", "on") != "off";
		gFlash = gDown = -1; gSeen = -1;
		pkView = PK_PARTY; pkMon = 0; pkBattle = false;
		raw = std::make_shared<RaWatch>();
		raw->since = now;
		std::string cfg = "/data/retroarch/retroarch.cfg";
		raw->user = cfgValue(cfg, "cheevos_username");
		raw->token = cfgValue(cfg, "cheevos_token");
		// since 0.4.2 ra-secrets moves RetroArch's token out of retroarch.cfg (file sharing shows it) into
		// accounts.conf, for the same account: read from there, or every request got "Invalid user/token combination"
		// and the panel showed no achievements
		if (raw->token.empty() && !raw->user.empty() && account("ra_user") == raw->user) raw->token = account("ra_retroarch_token");
		if (cfgValue(cfg, "cheevos_enable") != "true" || raw->user.empty()) raw->state = 4;
	}

	void Settings::gameClose()
	{
		raw.reset();
		gBadges.clear();
	}

	bool Settings::gameUpdate(double t)
	{
		now = t;
		bool changed = false;
		if (raw)
		{
			if (raw->since == 0) raw->since = t;
			raw->poll(raw, t);
			std::lock_guard<std::mutex> l(raw->mu);
			// a token RetroArch wrote while the game started (a first login): looked for when the file changes, not on
			// every pass (the whole retroarch.cfg ten times a second, beside the game, for as long as it had none)
			struct stat st;
			if (raw->token.empty() && raw->state != 4 && stat("/data/retroarch/retroarch.cfg", &st) == 0 && st.st_mtime != raw->cfgSeen)
			{
				raw->cfgSeen = st.st_mtime;
				raw->token = cfgValue("/data/retroarch/retroarch.cfg", "cheevos_token");
			}
			if (raw->gen != gSeen) changed = true;
		}
		if (companion && companion->takeChanged())
		{
			changed = true;
			// a battle starting shows the battle, its end the party (unless another tab was picked meanwhile)
			bool b = companion->snapshot().inBattle;
			if (b && !pkBattle && pkView != PK_ACH) pkView = PK_BATTLE;
			if (!b && pkBattle && pkView == PK_BATTLE) pkView = PK_PARTY;
			pkBattle = b;
		}
		if (gFlash >= 0 && t >= gFlashUntil && gDown < 0) { gFlash = -1; changed = true; }
		return changed;
	}

	bool Settings::gameProgress(int& unlocked, int& total)
	{
		if (!raw) return false;
		std::lock_guard<std::mutex> l(raw->mu);
		if (raw->state != 2 || raw->all.empty()) return false;
		total = (int)raw->all.size();
		unlocked = 0;
		for (auto& a : raw->all) if (raw->unlocked.count(a.id)) unlocked++;
		return true;
	}

	Settings::GameAction Settings::gameTouch(float x, float y, bool down)
	{
		float lx = x / s, ly = y / s;
		int hit = -1;
		for (int i = 0; i < 5; i++)
		{
			float tx = 18 + i * 122;
			if (lx >= tx && lx < tx + 114 && ly >= 300 && ly < 420) hit = i;
		}
		// the companion's box: a tab, a party member (its moves), or back to the party from them
		if (companion && companion->active() && !down && ly >= 80 && ly < 286)
		{
			if (ly < 116)
			{
				for (int i = 0; i < 3; i++)
					if (pkTabW[i] > 0 && lx >= pkTabX[i] - 8 && lx < pkTabX[i] + pkTabW[i] + 8) pkView = i == 0 ? PK_PARTY : i == 1 ? PK_BATTLE : PK_ACH;
			}
			else if (pkView == PK_PARTY)
			{
				int c = lx < 320 ? 0 : 1, r = (int)((ly - 118) / 54);
				int i = r * 2 + c;
				if (r >= 0 && r < 3 && i < (int)companion->snapshot().party.size()) { pkMon = i; pkView = PK_MON; }
			}
			else if (pkView == PK_MON) pkView = PK_PARTY;
			return G_NONE;
		}
		if (down) { gDown = gFlash = hit; gFlashUntil = now + 10000; return G_NONE; }
		int was = gDown;
		gDown = -1;
		gFlashUntil = now + 220;
		if (hit < 0 || hit != was) { gFlash = -1; return G_NONE; }
		static const GameAction acts[5] = { G_SAVE, G_LOAD, G_MENU, G_FFWD, G_QUIT };
		return acts[hit];
	}

	void Settings::gameRender()
	{
		SPal p = pal(light);
		int state;
		std::string game;
		unsigned total = 0, got = 0;
		struct Row { std::string title, desc, badge; long long when; unsigned points; };
		std::vector<Row> rows;
		{
			std::lock_guard<std::mutex> l(raw->mu);
			gSeen = raw->gen;
			state = raw->state;
			game = raw->game;
			total = (unsigned)raw->all.size();
			for (auto& a : raw->all)
			{
				auto u = raw->unlocked.find(a.id);
				if (u == raw->unlocked.end()) continue;
				got++;
				rows.push_back({ a.title, a.desc, a.badge, u->second, a.points });
			}
			std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.when > b.when; });
			if (rows.size() > 2) rows.resize(2);
			for (auto& r : rows) raw->wantBadge(raw, r.badge);
		}
		be->beginScreen(gScreen);
		box("screen", false, 0, 0, 640, 480);
		// ---- the header: the game, the system
		box("bubble", false, 18, 14, 604, 54);
		std::string sys = fit(gSystem, true, 17, 200);
		float sw = text(sys, true, 17, 0, 0, p.muted, false, 0, true);
		text(sys, true, 17, 602 - sw, 31, p.muted);
		text(fit(gTitle, true, 22, 540 - sw), true, 22, 38, 28, p.ink);
		// ---- the achievements (or the companion's tabs, the achievements one of them)
		box("slot", false, 18, 80, 604, 206);
		bool pk = companion && companion->active();
		if (pk) pokeRender(p);
		else text("Latest achievements", true, 15, 40, 96, p.muted, true, 1.2f);
		if (pk && pkView != PK_ACH) {}
		else if (state == 2 && total)
		{
			std::string v = std::to_string(got) + " / " + std::to_string(total);
			float vw = text(v, true, 15, 0, 0, p.muted, false, 0, true);
			text(v, true, 15, 600 - vw, 96, p.accent);
			float bx = 40, bw = 560, by = 122;
			be->fill(X(bx), X(by), X(bw), X(10), p.line);
			be->fill(X(bx + 2), X(by + 2), X((bw - 4) * got / total), X(6), p.accent);
			if (rows.empty()) text("None unlocked yet. Good luck!", false, 18, 40, 150, p.muted);
			for (size_t i = 0; i < rows.size(); i++)
			{
				float y = 146 + i * 66;
				auto& r = rows[i];
				// the badge: its picture when cached, else a tile with a star
				rnds::TexPtr tex;
				auto bi = gBadges.find(r.badge);
				if (bi != gBadges.end()) tex = bi->second;
				else
				{
					bool ready = false;
					{ std::lock_guard<std::mutex> l(raw->mu); auto b = raw->badgeReady.find(r.badge); ready = b != raw->badgeReady.end() && b->second; }
					if (ready)
					{
						rnds::Bitmap b = rnds::decodeFile(std::string(BADGES) + "/" + r.badge + ".png");
						if (!b.empty()) { tex = be->upload(b, true); gBadges[r.badge] = tex; }
					}
				}
				box("bubble", false, 40, y, 56, 56);
				if (tex) be->draw(tex, X(43), X(y + 3), X(50), X(50), Color(1, 1, 1, 1));
				else text("*", true, 30, 58, y + 10, p.accent);
				// when: "just now", "12 min ago", today's time, or the date
				char w[40];
				long long age = (long long)time(nullptr) - r.when;
				if (age < 60) snprintf(w, sizeof w, "just now");
				else if (age < 3600) snprintf(w, sizeof w, "%lld min ago", age / 60);
				else
				{
					time_t tt = (time_t)r.when, nt = time(nullptr);
					struct tm a, b;
					localtime_r(&tt, &a); localtime_r(&nt, &b);
					if (a.tm_yday == b.tm_yday && a.tm_year == b.tm_year) strftime(w, sizeof w, "today %H:%M", &a);
					else strftime(w, sizeof w, "%d %b %Y", &a);
				}
				float ww = text(w, false, 15, 0, 0, p.muted, false, 0, true);
				text(w, false, 15, 600 - ww, y + 6, p.muted);
				text(fit(r.title, true, 19, 480 - ww), true, 19, 110, y + 4, p.ink);
				text(fit(r.desc, false, 15, 490), false, 15, 110, y + 32, p.muted);
			}
		}
		else
		{
			const char* msg = state == 0 ? "Waiting for RetroArch..." : state == 1 ? "Looking the game up..." :
				state == 3 ? "This game has no achievements." :
				state == 4 ? "RetroAchievements is off, or its login failed: check the account in Settings > System > First-time setup." :
				"No connection to RetroAchievements: check Wi-Fi.";
			wrapped(msg, 18, 40, 130, 560, 3, p.muted);
		}
		// ---- the tiles: RetroArch's commands by touch, and their buttons
		for (int i = 0; i < 5; i++)
		{
			float x = 18 + i * 122, y = 300;
			bool on = gFlash == i;
			box(on ? "pick" : "slot", on, x, y, 114, 120);
			Color fg = on ? p.onPick : (i == 4 ? Color::hex(0xff8a96) : p.ink);
			int k = std::max(1, (int)std::lround(3 * s));
			float ix = std::round(X(x + 57) - 6 * k), iy = std::round(X(y + 16));
			for (int r = 0; r < 12; r++)
				for (int c = 0; c < 12; c++)
					if (GTILES[i].icon[r][c] == '#') be->fill(ix + c * k, iy + r * k, (float)k, (float)k, fg);
			// (the document's own translation for this panel: its "Fast" is fast-forward, the settings' is speed)
			std::string lb = lang::tr("Second screen (RetroArch games)", GTILES[i].label);
			float lw = text(lb, true, 19, 0, 0, fg, false, 0, true);
			text(lb, true, 19, x + 57 - lw / 2, y + 60, fg);
			std::string kk = gHotkeys || i == 2 || i == 4 ? GTILES[i].keys : "";	// (hotkeys off: the menu's and quit's alone)
			float kw = text(kk, false, 14, 0, 0, p.muted, false, 0, true);
			text(kk, false, 14, x + 57 - kw / 2, y + 90, on ? p.onPickAccent : p.muted);
		}
		std::string hint = !gHotkeys ? (gTouch ? "Tap a tile; F + X opens RetroArch's menu, F + Start quits" : "F + X opens RetroArch's menu, F + Start quits")
			: gTouch ? "Tap a tile, or hold F with the button under it" : "Hold F with the button under each tile";
		float hw = text(hint, false, 15, 0, 0, p.muted, false, 0, true);
		text(hint, false, 15, 320 - hw / 2, 438, p.muted);
		be->endScreen();
	}

	// ---- the Pokémon companion's box (pokemon.h): tabs, then the party, a member's moves, or the battle
	// numbers a character at a time (HP and PP change all the time: one cached texture for each digit, not each value)
	float Settings::num(const std::string& t, float size, float x, float top, const Color& c, bool measureOnly)
	{
		float w = 0;
		for (char ch : t)
		{
			std::string one(1, ch);
			if (ch == ' ') { w += size * 0.3f; continue; }
			w += text(one, true, size, x + w, top, c, false, 0, measureOnly);
		}
		return w;
	}

	void Settings::typeChip(int type, float x, float y)
	{
		const char* n = pkmn::typeName(type);
		if (!*n) return;
		be->fill(X(x), X(y), X(62), X(18), Color::hex(pkmn::typeColor(type)));
		std::string u = lang::tr("Pokémon types", n);	// (its own context: "Dark" is a theme too)
		float w = text(u, true, 12, 0, 0, Color(1, 1, 1, 1), true, 0.5f, true);
		text(u, true, 12, x + 31 - w / 2, y + 3, Color(1, 1, 1, 1), true, 0.5f);
	}

	void Settings::hpBar(int hp, int maxHp, float x, float y, float w)
	{
		SPal p = pal(light);
		be->fill(X(x), X(y), X(w), X(8), p.line);
		if (maxHp <= 0 || hp <= 0) return;
		float f = std::min(1.0f, (float)hp / maxHp);
		Color c = f > 0.5f ? Color::hex(0x58d068) : f > 0.2f ? Color::hex(0xf8c030) : Color::hex(0xf05848);
		be->fill(X(x + 1), X(y + 1), std::max(1.0f, X((w - 2) * f)), X(6), c);
	}

	void Settings::pokeRender(const SPal& p)
	{
		const pkmn::Snapshot& sn = companion->snapshot();
		auto tb = companion->tables();
		pkmn::RomTables none;
		const pkmn::RomTables& t = tb ? *tb : none;
		if (pkView == PK_BATTLE && !sn.inBattle) pkView = PK_PARTY;
		if (pkView == PK_MON && pkMon >= (int)sn.party.size()) pkView = PK_PARTY;
		// the tabs: Party, Battle (in one), Achievements
		const char* tabs[3] = { "Party", "Battle", "Achievements" };
		float tx = 40;
		for (int i = 0; i < 3; i++)
		{
			pkTabW[i] = 0;
			if (i == 1 && !sn.inBattle) continue;
			bool on = (i == 0 && (pkView == PK_PARTY || pkView == PK_MON)) || (i == 1 && pkView == PK_BATTLE) || (i == 2 && pkView == PK_ACH);
			float w = text(tabs[i], true, 15, tx, 96, on ? p.accent : p.muted, true, 1.2f);
			if (on) be->fill(X(tx), X(114), X(w), X(2), p.accent);
			pkTabX[i] = tx; pkTabW[i] = w;
			tx += w + 28;
		}
		if (pkView == PK_ACH) return;
		auto name = [&](const pkmn::Mon& m) { return m.egg ? std::string("EGG") : !m.nick.empty() ? m.nick : t.speciesName(m.species); };
		auto lv = [&](const pkmn::Mon& m, float right, float y, float size)
		{
			std::string n = std::to_string(m.level);
			float nw = num(n, size, 0, 0, p.ink, true), lw = text("Lv", false, size - 2, 0, 0, p.muted, false, 0, true);
			text("Lv", false, size - 2, right - nw - lw - 4, y + 2, p.muted);
			num(n, size, right - nw, y, p.ink);
			return nw + lw + 4;
		};
		if (!sn.seen)
		{
			std::string why = !sn.error.empty() ? "RetroArch can't read the game's memory with this emulator: " + sn.error : "Waiting for the game...";
			wrapped(why, 18, 40, 130, 560, 3, p.muted);
			return;
		}
		if (pkView == PK_PARTY)
		{
			if (sn.party.empty()) { wrapped("No Pokémon in the party yet.", 18, 40, 130, 560, 3, p.muted); return; }
			for (size_t i = 0; i < sn.party.size() && i < 6; i++)
			{
				const pkmn::Mon& m = sn.party[i];
				float x = 32 + (i % 2) * 294, y = 120 + (i / 2) * 54;
				box("bubble", false, x, y, 282, 50);
				bool down = !m.egg && m.hp == 0;
				float right = x + 270;
				float used = m.egg ? 0 : lv(m, right, y + 5, 16);
				const char* st = down ? "FNT" : pkmn::statusName(m.status);
				if (*st)
				{
					float sw = text(st, true, 13, 0, 0, p.ink, false, 0, true);
					float sx = right - used - 12 - sw;
					be->fill(X(sx - 4), X(y + 5), X(sw + 8), X(18), down ? p.line : Color::hex(0xc05068));
					text(st, true, 13, sx, y + 8, Color(1, 1, 1, 1));
					used += sw + 20;
				}
				text(fit(name(m), true, 17, 250 - used), true, 17, x + 12, y + 5, down ? p.muted : p.ink);
				if (m.egg) continue;
				hpBar(m.hp, m.maxHp, x + 12, y + 33, 160);
				std::string hp = std::to_string(m.hp) + "/" + std::to_string(m.maxHp);
				float hw = num(hp, 13, 0, 0, p.muted, true);
				num(hp, 13, right - hw, y + 29, p.muted);
			}
			return;
		}
		// a member's moves, or the lead's in a battle with how each does against the foe
		auto moves = [&](const pkmn::Mon& m, float y0, const std::vector<pkmn::Mon>* foes)
		{
			for (int i = 0; i < 4; i++)
			{
				float x = 32 + (i % 2) * 294, y = y0 + (i / 2) * 44;
				box("bubble", false, x, y, 282, 40);
				uint16_t mv = m.moves[i];
				if (!mv) { text("-", false, 16, x + 12, y + 10, p.muted); continue; }
				bool known = mv < t.moveInfo.size();
				float right = x + 270;
				if (foes && known && t.moveInfo[mv].power > 0)
				{
					// the move against each foe: x4, x2, x1, x1/2, x1/4, x0
					std::string e;
					for (size_t f = 0; f < foes->size(); f++)
					{
						int q = pkmn::effect(t.moveInfo[mv].type, (*foes)[f].types[0], (*foes)[f].types[1]);
						if (f) e += " ";
						e += q == 0 ? "x0" : q == 1 ? "x1/4" : q == 2 ? "x1/2" : q == 4 ? "x1" : q == 8 ? "x2" : "x4";
					}
					int q0 = foes->empty() ? 4 : pkmn::effect(t.moveInfo[mv].type, (*foes)[0].types[0], (*foes)[0].types[1]);
					Color c = q0 >= 8 ? p.accent : q0 == 0 ? Color::hex(0xff8a96) : q0 < 4 ? p.muted : p.ink;
					float ew = num(e, 15, 0, 0, c, true);
					num(e, 15, right - ew, y + 11, c);
					right -= ew + 10;
				}
				else if (!foes)
				{
					int mx = known ? pkmn::maxPP(t.moveInfo[mv].pp, m.ppBonuses, i) : 0;
					std::string pp = std::to_string(m.pp[i]) + (mx ? "/" + std::to_string(mx) : "");
					float pw = num(pp, 14, 0, 0, p.muted, true), lw = text("PP", false, 12, 0, 0, p.muted, false, 0, true);
					num(pp, 14, right - pw, y + 12, m.pp[i] ? p.ink : Color::hex(0xff8a96));
					text("PP", false, 12, right - pw - lw - 4, y + 14, p.muted);
					right -= pw + lw + 14;
				}
				if (known) { typeChip(t.moveInfo[mv].type, right - 62, y + 11); right -= 70; }
				text(fit(t.moveName(mv), true, 16, right - x - 20), true, 16, x + 12, y + 11, p.ink);
			}
		};
		auto header = [&](const pkmn::Mon& m, float x, float y, float w)
		{
			float right = x + w;
			float used = lv(m, right, y, 17);
			std::string sp = t.speciesName(m.species);
			std::string n = name(m);
			text(fit(n, true, 18, w - used - 150), true, 18, x, y, p.ink);
			float nw = text(fit(n, true, 18, w - used - 150), true, 18, 0, 0, p.ink, false, 0, true);
			if (!m.egg && sp != n && sp[0] != '#') text(fit(sp, false, 14, 140), false, 14, x + nw + 8, y + 4, p.muted);
			float cx = x;
			for (int k = 0; k < 2; k++)
				if (m.types[k] < pkmn::NUM_TYPES && (k == 0 || m.types[1] != m.types[0])) { typeChip(m.types[k], cx, y + 26); cx += 68; }
			hpBar(m.hp, m.maxHp, cx + 6, y + 31, right - cx - 6 - 80);
			std::string hp = std::to_string(m.hp) + "/" + std::to_string(m.maxHp);
			float hw = num(hp, 14, 0, 0, p.muted, true);
			num(hp, 14, right - hw, y + 26, p.muted);
		};
		if (pkView == PK_MON)
		{
			const pkmn::Mon& m = sn.party[pkMon];
			header(m, 40, 124, 560);
			moves(m, 186, nullptr);
			return;
		}
		// the battle: the foe (two side by side in a double battle), what it is weak to, the lead's moves against it
		if (sn.foes.empty()) { wrapped("Waiting for the battle...", 18, 40, 130, 560, 2, p.muted); return; }
		if (sn.foes.size() == 1)
		{
			const pkmn::Mon& f = sn.foes[0];
			header(f, 40, 122, 560);
			std::string weak;
			for (int a = 0; a < pkmn::NUM_TYPES; a++)
			{
				if (a == pkmn::MYSTERY) continue;
				int q = pkmn::effect(a, f.types[0], f.types[1]);
				if (q >= 8) weak += std::string(weak.empty() ? "" : ", ") + lang::tr("Pokémon types", pkmn::typeName(a)) + (q == 16 ? " x4" : "");
			}
			if (!weak.empty()) text(fit(lang::tr("Weak to") + ": " + weak, false, 14, 560), false, 14, 40, 171, p.muted);
		}
		else
			for (size_t k = 0; k < 2; k++) header(sn.foes[k], 40 + k * 290, 122, 270);
		moves(sn.lead, 192, &sn.foes);
	}
}
