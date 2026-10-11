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
	static void spawn(const char* path, const char* arg)
	{
		pid_t p = fork();
		if (p == 0)
		{
			if (fork() == 0) { setsid(); execl(path, path, arg, (char*)nullptr); _exit(127); }
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
			p.ink = Color::hex(0xe7eef6); p.muted = Color::hex(0x93a0b0); p.line = Color::hex(0x7d8b9c); p.edge = Color::hex(0xd5dee8);
			p.screenBg = Color::hex(0x121820); p.screenFrame = Color::hex(0x070a0e); p.screenBand = Color::hex(0x243040);
			p.ditherA = Color(1, 1, 1, 0.14f); p.ditherB = Color(1, 1, 1, 0.07f);
			p.panelBand = Color::hex(0x2e3a48); p.panelBody = Color::hex(0x121820); p.panelLow = Color::hex(0x070a0e);
			p.bubbleBand = Color::hex(0x2c3848); p.bubbleBody = Color::hex(0x141b24); p.bubbleLow = Color::hex(0x0c1218);
			p.slotOn = Color::hex(0xffffff); p.kbdBg = Color::hex(0xe7eef6); p.kbdInk = Color::hex(0x121820);
			p.blackish = Color::hex(0x070a0e); p.shade45 = Color(0, 0, 0, 0.45f);
		}
		else
		{
			Color sh = Color::hex(0x14233a);
			p.ink = Color::hex(0x1d2733); p.muted = Color::hex(0x66758a); p.line = Color::hex(0x8d9bab); p.edge = Color::hex(0x2b3644);
			p.screenBg = Color::hex(0xe6ebf0); p.screenFrame = Color::hex(0xb9c4d0); p.screenBand = Color::hex(0xfbfcfd);
			p.ditherA = Color(sh.r, sh.g, sh.b, 0.07f); p.ditherB = Color(sh.r, sh.g, sh.b, 0.035f);
			p.panelBand = Color::hex(0xffffff); p.panelBody = Color::hex(0xedf1f6); p.panelLow = Color::hex(0xc5cfda);
			p.bubbleBand = Color::hex(0xffffff); p.bubbleBody = Color::hex(0xf4f7fa); p.bubbleLow = Color::hex(0xcfd8e2);
			p.slotOn = Color::hex(0x1a6ea3); p.kbdBg = Color::hex(0x2b3644); p.kbdInk = Color::hex(0xf2f6fa);
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
		build();
	}

	void Settings::setVariant(const std::string& v)
	{
		bool l = v == "light";
		if (l != light) { light = l; cache.clear(); }
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
		add(D, "picture", "Theme", "ROCKNIXDS Pixel's two palettes: dark, or light for daylight.", menu, "theme", { "Dark", "Light" }, { "dark", "light" });
		add(D, "picture", "Brightness", "Both panels' backlights. F + volume changes it anywhere, in games too.", "", "",
			{ "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" }, { "100", "200", "300", "400", "500", "600", "700", "800", "900", "1000" }, Item::BRIGHTNESS);
		add(D, "picture", "Motion", "The top screen's bob and shine. Off holds it still and saves a little battery.", menu, "motion", { "On", "Off" }, { "on", "off" });
		add(D, "music", "Menu sounds", "The menu's clicks, and the cartridge going into the console.", menu, "sounds", { "On", "Off" }, { "on", "off" });
		add(D, "picture", "Clock", "How the time on the top screen is written.", menu, "clock", { "24-hour", "12-hour" }, { "24h", "12h" });
		add(D, "picture", "Boot animation", "The bananas and the cartridge while Banana starts. Off goes straight to the menu once it is ready.", menu, "splash", { "On", "Off" }, { "on", "off" });
		const char* P = "Sleep & power";
		add(P, "moon", "Sleep when idle", "In the menu, after this long with no button pressed. Never while a game is running.", power, "autosleep_minutes",
			{ "Never", "5 minutes", "10 minutes", "15 minutes", "30 minutes" }, { "0", "5", "10", "15", "30" });
		add(P, "moon", "Closing the lid", "Sleep with the lid: the game pauses, opening the lid carries on where you were.", power, "lid_sleep", { "Sleeps", "Does nothing" }, { "yes", "no" });
		add(P, "moon", "Waking up", "Fast: the handheld's parts wake up together. Careful: one after the other, slower but the safest. Banana switches to Careful by itself if a sleep ever fails.",
			power, "fast_resume", { "Fast", "Careful" }, { "yes", "no" });
		add(P, "moon", "LEDs", "Green, red when the battery is low, blinking red when nearly empty, amber while charging.", power, "led_brightness",
			{ "Bright", "Dim", "Off" }, { "255", "40", "0" });
		const char* N = "Nintendo DS";
		add(N, "nds", "Renderer", "Gengis Engine draws the same picture as DraStic with about 12% less CPU work a frame.", sd, "DSFLIP_RAST", { "Gengis Engine", "DraStic" }, { "1", "0" });
		add(N, "nds", "Shader", "Crisp: exact pixel-area scaling. Grid: the DS's LCD grid. FSR: smooth edges, the heaviest. None costs no GPU work.", sd, "DSFLIP_SHADER",
			{ "None", "Crisp", "Crisp + DS colours", "LCD grid", "LCD grid + colours", "FSR" }, { "none", "ds-crisp", "ds-crisp-color", "ds-grid", "ds-grid-color", "ds-fsr" });
		add(N, "nds", "3D resolution", "2x draws the DS's 3D at twice its size on these panels, at full speed with SuperDrastic.", dcfg, "hires_3d", { "2x", "1x" }, { "1", "0" });
		const char* O = "Other systems";
		add(O, "gamepad", "Screen", "Which panel the systems other than the DS use (RetroArch). The other panel is switched off.",
			"/data/retroarch/retroarch.cfg", "video_monitor_index", { "Top", "Bottom" }, { "2", "1" });
		const char* W = "Network";
		add(W, "phone", "Wi-Fi", "", menu, "wifi", { "On", "Off" }, { "on", "off" });
		const char* S = "System";
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
		std::string version = readLine("/etc/banana/version");
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
		std::string state = readLine("/sys/class/net/wlan0/operstate");
		if (!state.empty())
		{
			wifiInfo = state == "up" ? "Connected" : "Not connected";
			int fd = socket(AF_INET, SOCK_DGRAM, 0);
			ifreq ifr = {};
			strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ - 1);
			if (fd >= 0 && ioctl(fd, SIOCGIFADDR, &ifr) == 0)
				wifiInfo = std::string("Connected: ") + inet_ntoa(((sockaddr_in*)&ifr.ifr_addr)->sin_addr);
			if (fd >= 0) ::close(fd);
		}
		std::ostringstream a;
		a << "Banana " << (version.empty() ? "dev" : version) << " on the " << (model.empty() ? "handheld" : model) << ". ";
		if (battery >= 0) a << "Battery " << battery << "%. ";
		if (space[0]) a << "Card: " << space << ".";
		about = a.str();
	}

	void Settings::open()
	{
		for (auto& it : items) load(it);
		refreshInfo();
		armed = -1;
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
		if (it.key == "wifi") spawn("/usr/libexec/banana/wifi", v == "on" ? "up" : "down");
	}

	Settings::Result Settings::change(int dir)
	{
		Item& it = items[sel];
		if (it.kind == Item::INFO) return NONE;
		if (it.kind == Item::ACTION)
		{
			if (dir <= 0) { armed = -1; return MOVED; }
			if (armed != sel) { armed = sel; return MOVED; }
			return it.key == "reboot" ? REBOOT : POWEROFF;
		}
		int n = (int)it.values.size();
		int at = it.at + dir;
		if (it.kind == Item::BRIGHTNESS) at = std::min(n - 1, std::max(0, at));	// a slider stops at its ends
		else at = (at + n) % n;
		if (at == it.at) return NONE;
		it.at = at;
		save(it);
		if (it.key == "theme") { setVariant(it.values[at]); return THEME; }
		return CHANGED;
	}

	Settings::Result Settings::press(Key k)
	{
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
		SPal p = pal(light);
		const Item& cur = items[sel];

		// ---- top: what the selected setting is ----
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		icon(cur.icon, 50, 56, 3);
		text(cur.section, true, 17, 170, 62, p.muted, true, 1.4f);
		text(cur.label, true, 30, 170, 88, p.ink);
		std::string value = cur.kind == Item::CHOICE || cur.kind == Item::BRIGHTNESS ? cur.names[cur.at] : "";
		if (!value.empty()) text(value, true, 22, 170, 130, p.accent);
		std::string help = cur.help;
		if (cur.kind == Item::INFO) help = about;
		if (cur.key == "wifi") help = "Wi-Fi: " + wifiInfo + ". Networks go in banana/wifi.conf on the card (see banana/README.txt).";
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
			if (it.kind == Item::CHOICE || it.kind == Item::BRIGHTNESS) v = it.names[it.at];
			else if (it.kind == Item::ACTION) v = armed == i ? "Press A again" : "";
			else if (it.kind == Item::INFO) v = "";
			if (v.empty()) continue;
			float vw = text(v, true, 19, 0, 0, p.ink, false, 0, true);
			Color vc = on ? (armed == i ? p.accent : p.ink) : p.muted;
			if (it.kind == Item::ACTION) { text(v, true, 19, 586 - vw, y + 17, vc); continue; }
			arrow(true, 576, y + 21, vc);
			text(v, true, 19, 566 - vw, y + 17, vc);
			arrow(false, 556 - vw - 2 - std::round(13 / 2.0f + 1), y + 21, vc);
		}
		float bw = faceWidth("B", "Back");
		face("B", "Back", 18, bw);
		std::string act = cur.kind == Item::ACTION ? (armed == sel ? "Confirm" : cur.label) : cur.kind == Item::INFO ? "" : "Change";
		if (!act.empty())
		{
			float aw = faceWidth("A", act);
			face("A", act, 622 - aw, aw);
		}
		be->endScreen();
	}
}
