// banana-ui: Banana's frontend. ROCKNIXDS Pixel's engine (ui/rnds, imported unchanged) drawn in software straight
// onto the RG DS's two panels: no EmulationStation, no compositor, no GPU. It sleeps in poll() until a button, a
// finished picture or an animation step needs a frame, and gives the panels, the input devices and the sound device
// away completely while a game runs.
//
//   banana-ui [--assets DIR] [--systems FILE] [--roms DIR] [--data DIR]
//   banana-ui --headless OUTPREFIX --script "home 0; wait 2400; shot home" [--size 640x480] ...   (renders PNGs)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "../third_party/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

#include "../rnds/RndsUI.h"
#include "input.h"
#include "kms.h"
#include "library.h"
#include "soft.h"
#include "splash.h"
#include "lang.h"
#include "settings.h"
#include "settings_defaults.h"
#include "theme.h"
#include "colors.h"
#include "zip.h"
#include "sound.h"
#include "menu.h"
#include "carts.h"
#include "layout.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <future>
#include <list>
#include <sys/stat.h>
#include <map>
#include <memory>
#include <mutex>
#include <poll.h>
#include <sstream>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434	// (the toolchain's headers predate it; the handhelds' kernel has it)
#endif
#include <sys/inotify.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <sys/wait.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

using namespace banana;

static double nowMs()
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "banana-ui: ");
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

static rnds::Bitmap stbLoad(const std::string& path)
{
	rnds::Bitmap b;
	int w, h, n;
	unsigned char* p = stbi_load(path.c_str(), &w, &h, &n, 4);
	if (!p) return b;
	b.w = w; b.h = h;
	b.px.assign(p, p + (size_t)w * h * 4);
	stbi_image_free(p);
	return b;
}

// The last pictures decoded, kept a little: one box art makes several pictures, each its own job (Pixel's cartridge
// and cover, Carts' cartridge in the row, lifted and on the top screen), and decoding the PNG was most of each job's
// time. A file is known by its path, size and time (art scraped again is read again); a picture another worker is
// decoding is waited for, not decoded twice
typedef std::shared_ptr<const rnds::Bitmap> DecodedPic;
struct Decoded { std::string key; std::shared_future<DecodedPic> pic; size_t bytes; };
static std::mutex decodedLock;
static std::list<Decoded> decoded;		// the newest first
static size_t decodedBytes = 0;
static const size_t DECODED_BUDGET = 12u << 20;	// a dozen covers or so (a 500x450 one is 0.9 MB)

// (a game is starting: the memory is the game's)
static void forgetDecoded()
{
	std::lock_guard<std::mutex> l(decodedLock);
	decoded.clear();
	decodedBytes = 0;
}

static rnds::Bitmap stbDecode(const std::string& path)
{
	struct stat st;
	if (stat(path.c_str(), &st) != 0) return rnds::Bitmap();
	std::string key = path + '\0' + std::to_string((long long)st.st_size) + ':' + std::to_string((long long)st.st_mtime) + '.' +
		std::to_string((long)st.st_mtim.tv_nsec);
	auto same = [&](const Decoded& e) { return e.key == key; };
	std::shared_future<DecodedPic> pic;
	std::promise<DecodedPic> mine;
	bool decode = false;
	{
		std::lock_guard<std::mutex> l(decodedLock);
		auto it = std::find_if(decoded.begin(), decoded.end(), same);
		if (it != decoded.end())
		{
			decoded.splice(decoded.begin(), decoded, it);
			pic = it->pic;
		}
		else
		{
			pic = mine.get_future().share();
			decoded.push_front(Decoded{ key, pic, 0 });
			decode = true;
		}
	}
	if (decode)
	{
		auto b = std::make_shared<const rnds::Bitmap>(stbLoad(path));
		mine.set_value(b);
		std::lock_guard<std::mutex> l(decodedLock);
		auto it = std::find_if(decoded.begin(), decoded.end(), same);
		if (it != decoded.end()) { it->bytes = b->px.size(); decodedBytes += it->bytes; }
		while (decodedBytes > DECODED_BUDGET && decoded.size() > 1) { decodedBytes -= decoded.back().bytes; decoded.pop_back(); }
	}
	return *pic.get();
}

// settings.conf: key = value lines, the image's defaults under the card's own (settings_defaults.cpp)

static std::string shellQuote(const std::string& s)
{
	std::string r = "'";
	for (char c : s) { if (c == '\'') r += "'\\''"; else r += c; }
	return r + "'";
}

static std::string replaceAll(std::string s, const std::string& from, const std::string& to)
{
	for (size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size()) s.replace(at, from.size(), to);
	return s;
}

// ---- the Source with a few host settings on top -------------------------------------------------------------------
struct Host : Library
{
	using Library::Library;
	bool motion = true;
	bool topMotion() override { return motion; }
	std::string batteryShown = "icon";		// "icon", or "text": the percentage too
	std::string batteryMode() override { return batteryShown; }
};

// ---- the workers that decode and fit pictures off the main thread -----------------------------------------------
// A few threads (one fewer than the cores, at most three), so scrolling through a big library keeps up with its
// pictures; niced, so the frames' thread is never the one kept waiting for a core
struct Worker
{
	std::shared_ptr<Menu> ui;	// (shared: a menu doing its own locking is kept alive by the job running without uiLock)
	std::mutex uiLock, wlock;
	std::condition_variable cv;
	bool stop = false;
	uint64_t kicks = 0;		// jobs queued: each thread works until the queue is empty once it sees a new one
	int wakeFd = -1;		// eventfd: a job finished, the main loop has something to upload
	std::vector<std::thread> th;

	void start(std::shared_ptr<Menu> u)
	{
		ui = u;
		wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		ui->setNotifiers([this] { kick(); }, [this] { uint64_t one = 1; if (write(wakeFd, &one, 8) < 0) {} });
		int n = std::max(1, std::min(3, (int)std::thread::hardware_concurrency() - 1));
		for (int i = 0; i < n; i++) th.emplace_back([this] { run(); });
	}
	// a new UI (the theme changed): called with uiLock held
	void attach(std::shared_ptr<Menu> u)
	{
		ui = u;
		ui->setNotifiers([this] { kick(); }, [this] { uint64_t one = 1; if (write(wakeFd, &one, 8) < 0) {} });
	}
	void kick()
	{
		{ std::lock_guard<std::mutex> l(wlock); kicks++; }
		cv.notify_all();
	}
	void run()
	{
		setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 5);
		uint64_t seen = 0;
		for (;;)
		{
			{
				std::unique_lock<std::mutex> l(wlock);
				cv.wait(l, [&] { return stop || kicks != seen; });
				if (stop) return;
				seen = kicks;
			}
			for (;;)
			{
				{ std::lock_guard<std::mutex> l(wlock); if (stop) return; }
				std::shared_ptr<Menu> u;
				bool more;
				{
					std::unique_lock<std::mutex> l(uiLock);
					u = ui;
					// a menu that guards its own state (both do: their jobs only share the queue) works without the
					// lock the frames need; one that didn't would work with it
					if (u->ownLocking()) l.unlock();
					more = u->workOnce();
				}
				if (!more) break;
			}
		}
	}
	void finish()
	{
		{ std::lock_guard<std::mutex> l(wlock); stop = true; }
		cv.notify_all();
		for (auto& t : th) if (t.joinable()) t.join();
		if (wakeFd >= 0) close(wakeFd);
	}
};

// ---- signals: a self-pipe ---------------------------------------------------------------------------------------
static int sigPipe[2] = { -1, -1 };
static void onSignal(int sig)
{
	char c = (char)sig;
	int e = errno;
	if (write(sigPipe[1], &c, 1) < 0) {}
	errno = e;
}

struct Options
{
	std::string assets = "/usr/share/banana/rnds", splash = "/usr/share/banana/splash", banana = "/usr/share/banana/banana";
	std::string systems = "/etc/banana/systems.conf", roms = "/data/roms", data = "/data/banana";
	std::string top = "DSI-2", touch = "fe5e0000.i2c";
	std::string headless, script;
	std::string theme;		// --theme FILE: that theme's .json instead of the settings' (a theme being made)
	int w = 640, h = 480;
};

static std::string envOr(const char* name, const std::string& def)
{
	const char* v = getenv(name);
	return v && *v ? v : def;
}

// ---- held buttons repeat, like a list in any frontend -----------------------------------------------------------
struct Repeat
{
	Key key = Key::NONE;
	double since = 0, last = 0;
	bool due(double now)
	{
		if (key == Key::NONE) return false;
		double held = now - since, gap = held > 1500 ? 60 : 120;
		if (held < 400 || now - last < gap) return false;
		last = now;
		return true;
	}
	int wait(double now) const
	{
		if (key == Key::NONE) return -1;
		double held = now - since;
		if (held < 400) return (int)std::ceil(400 - held);
		double gap = held > 1500 ? 60 : 120;
		return (int)std::max(0.0, std::ceil(last + gap - now));
	}
};

static bool uiButton(Key k, rnds::UI::Button& b)
{
	switch (k)
	{
	case Key::LEFT: b = rnds::UI::LEFT; return true;
	case Key::RIGHT: b = rnds::UI::RIGHT; return true;
	case Key::UP: b = rnds::UI::UP; return true;
	case Key::DOWN: b = rnds::UI::DOWN; return true;
	case Key::A: b = rnds::UI::A; return true;
	case Key::B: b = rnds::UI::B; return true;
	case Key::X: b = rnds::UI::X; return true;
	case Key::Y: b = rnds::UI::Y; return true;
	case Key::L: b = rnds::UI::L; return true;
	case Key::R: b = rnds::UI::R; return true;
	default: return false;
	}
}

static const char* soundOf(Key k, rnds::UI::View before, rnds::UI::View after)
{
	switch (k)
	{
	case Key::LEFT: case Key::RIGHT: case Key::UP: case Key::DOWN: case Key::L: case Key::R: return "scroll";
	case Key::A: return after == rnds::UI::LAUNCH && before != rnds::UI::LAUNCH ? "ready" : "select";
	case Key::B: return "back";
	case Key::Y: return "fav";
	case Key::X: return "select";
	default: return nullptr;
	}
}

// a favourite starred or unstarred: once the shelf shows, the Favorites collection and the favourite systems are put
// right, and the cursor stays on the system it was on
template <class U> static bool rearrange(Host& lib, U& ui)
{
	if (!lib.favouritesChanged() || ui.view() != rnds::UI::HOME) return false;
	int at = ui.systemIndex();
	std::string id = at >= 0 && at < lib.systemCount() ? lib.shelf(at).def.id : "";
	lib.arrange();
	int to = 0;
	for (int i = 0; i < lib.systemCount(); i++)
		if (lib.shelf(i).def.id == id) to = i;
	ui.dataChanged();
	ui.setHome(to);
	return true;
}

// ---- games copied onto the card while the menu runs (file sharing) ------------------------------------------------
// inotify on the ROM folders; the library is read again once the copying has been quiet for 2 s, and only while the
// menu shows its home screen (a game list under the cursor isn't reshuffled)
struct RomWatch
{
	int fd = -1;
	double due = 0;
	std::map<int, std::string> dirOf;	// each watch's folder
	static constexpr uint32_t EVENTS = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE;
	void add(const std::string& d)
	{
		int wd = inotify_add_watch(fd, d.c_str(), EVENTS);
		if (wd >= 0) dirOf[wd] = d;
	}
	void watch(const std::vector<std::string>& dirs)
	{
		if (fd < 0) fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
		if (fd < 0) return;
		for (auto& d : dirs) add(d);	// a folder that isn't there yet is caught by its parent's watch, then added here
	}
	void read(double now)
	{
		alignas(inotify_event) char buf[4096];
		bool any = false;
		for (ssize_t n; (n = ::read(fd, buf, sizeof buf)) > 0;)
		{
			any = true;
			// a folder made or moved in (a game folder copied over file sharing): watched at once, so the games
			// copied into it afterwards are seen too (the rescan 2 s after the folder came may find it still empty)
			for (char* p = buf; p < buf + n;)
			{
				auto* e = (inotify_event*)p;
				auto it = dirOf.find(e->wd);
				if ((e->mask & IN_ISDIR) && (e->mask & (IN_CREATE | IN_MOVED_TO)) && e->len && it != dirOf.end() && e->name[0] != '.')
					add(it->second + "/" + e->name);
				p += sizeof(inotify_event) + e->len;
			}
		}
		if (any) due = now + 2000;
	}
};

// ---- boot timing: seconds since the kernel started, for logs/boot.txt (rcS gathers /run/banana/boot-times) ------
static void bootMark(const char* what)
{
	timespec t;
	clock_gettime(CLOCK_BOOTTIME, &t);
	double s = t.tv_sec + t.tv_nsec / 1e9;
	logf("%s at %.2f s after the kernel started", what, s);
	if (FILE* f = fopen("/run/banana/boot-times", "a")) { fprintf(f, "%-28s %6.2f s\n", what, s); fclose(f); }
}

// ---- the low-battery notice ---------------------------------------------------------------------------------------
// At 20% (the LED turns red), 10% and 5% (it blinks), on the battery: a notice over the top panel for 8 s, which any
// button hides. Each step once, until the charger has been on; a step passed during a game shows when the menu is
// back. bananad does the shutting down (battery_shutdown_percent); this only says it is coming.
struct LowBattery
{
	int shownStep = 101, percent = -1;
	double until = 0;
	std::string title, body;

	bool showing(double now) const { return until > now; }
	// true when the notice came or went
	bool check(const Library::Power& p, int shutdownAt, double now)
	{
		if (p.percent < 0) return false;
		if (p.charging)
		{
			shownStep = 101;
			return hide(now);
		}
		int step = p.percent <= 5 ? 5 : p.percent <= 10 ? 10 : p.percent <= 20 ? 20 : 0;
		if (!step || step >= shownStep) return false;
		shownStep = step;
		percent = p.percent;
		until = now + 8000;
		std::string off = std::to_string(shutdownAt) + "%";
		if (step == 5)
		{
			title = "Battery nearly empty: " + std::to_string(percent) + "%";
			body = "Save your game now. At " + off + " Banana closes it and shuts down.";
		}
		else
		{
			title = "Battery low: " + std::to_string(percent) + "%";
			body = step == 10 ? "Plug in the charger soon. At " + off + " Banana closes the game and shuts down."
				: "Time to find the charger.";
		}
		return true;
	}
	bool hide(double now)
	{
		bool was = showing(now);
		until = 0;
		return was;
	}
};

static int shutdownPercent(const std::string& data)
{
	return atoi(getConfValue(data + "/bananad.conf", "battery_shutdown_percent",
		getConfValue("/etc/banana/bananad.conf", "battery_shutdown_percent", "3")).c_str());
}

// a game in the library by its path, launched as A on it in its own system's shelf would (false: not in the library)
static bool launchPath(Host& lib, const std::string& want)
{
	for (int s = 0; s < lib.systemCount(); s++)
		for (int i = 0; i < (int)lib.shelf(s).games.size(); i++)
			if (lib.shelf(s).games[i].path == want && !lib.shelf(s).favorites) { lib.openLibrary(s); lib.launch(i); return true; }
	return false;
}

// Settings > Sleep & power > Power off after sleeping: bananad saved the game's resume state and shut the handheld
// down, naming the game in banana/resume-boot ("rom=", "emulator=" lines). It starts again once, at this boot: the
// marker goes first (a game that fails at once isn't started at every boot), and once (/run/banana/resume-once) names
// it for its launcher, which loads the resume state then, Quick resume on or off. (once null: the headless test.)
static void resumeAfterPoweroff(Host& lib, const std::string& data, const char* once)
{
	const std::string path = data + "/resume-boot";
	std::string rom, emu, ln;
	{
		std::ifstream f(path);
		if (!f) return;
		while (std::getline(f, ln))
		{
			if (ln.compare(0, 4, "rom=") == 0) rom = ln.substr(4);
			else if (ln.compare(0, 9, "emulator=") == 0) emu = ln.substr(9);
		}
	}
	unlink(path.c_str());
	sync();
	if (rom.empty()) return;
	if (once) std::ofstream(once) << rom << "\n";
	if (launchPath(lib, rom)) logf("resume after power-off: %s (%s)", rom.c_str(), emu.c_str());
	else
	{
		logf("resume after power-off: no game at %s", rom.c_str());
		if (once) unlink(once);
	}
}

// ---- headless: the same UI rendered to PNGs, for tests and screenshots ------------------------------------------
// a game deleted on its page (Settings::DELETED): the card read again, back in the same system's list on the game
// after it (the home row when that list is now empty), and what went in the log
template <class U> static void afterDelete(Host& lib, U& ui, const std::vector<std::string>& gone)
{
	std::string id = lib.systemCount() ? lib.shelf(ui.systemIndex()).def.id : "";
	int g = ui.gameIndex();
	for (auto& f : gone) logf("deleted %s", f.c_str());
	lib.scan();
	ui.dataChanged();
	for (int i = 0; i < lib.systemCount(); i++)
		if (lib.shelf(i).def.id == id)
		{
			int n = lib.openLibrary(i) >= 0 ? lib.gameCount() : 0;
			if (n > 0) { ui.setLibrary(i, std::min(std::max(0, g), n - 1)); logf("deleted: %s's list again, %d games", id.c_str(), n); return; }
			ui.setHome(i);
			logf("deleted: %s's last game, the home row", id.c_str());
			return;
		}
	ui.setHome(0);
}

// the systems for Settings > Display & sound > Shown systems (id, name), from systems.conf
static std::vector<std::pair<std::string, std::string>> shelfSystems(const Host& lib)
{
	std::vector<std::pair<std::string, std::string>> r;
	for (auto& d : lib.systemDefs()) r.push_back({ d.id, d.name });
	return r;
}

// a change in Shown systems (Settings::SHELVES): the home row read again, on the same system when it is still there
template <class U> static void afterShelves(Host& lib, U& ui)
{
	std::string id = lib.systemCount() ? lib.shelf(ui.systemIndex()).def.id : "";
	lib.scan();
	ui.dataChanged();
	int at = 0;
	for (int i = 0; i < lib.systemCount(); i++) if (lib.shelf(i).def.id == id) at = i;
	ui.setHome(at);
	logf("shown systems: %d on the home row", lib.systemCount());
}

static int runHeadless(const Options& o, Host& lib, const std::string& variant, const std::string& theme)
{
	SoftBackend be(o.w, o.h);
	std::string layout = themePath(theme);
	std::unique_ptr<Menu> uiPtr(theme == "carts" ? (Menu*)new CartMenu(&lib, &be, o.banana + "/fonts", o.h / 480.0f)
		: isLayoutTheme(layout) ? (Menu*)new LayoutMenu(&lib, &be, layout, o.h / 480.0f, o.assets + "/fonts", o.banana + "/fonts")
		: new RndsMenu(&lib, &be, o.assets, o.h / 480.0f, variant));
	Menu& ui = *uiPtr;
	double now = 100000;
	std::stringstream ss(o.script);
	std::string cmd;
	auto settle = [&] { ui.update(now); ui.finishWork(); ui.update(now); };
	auto save = [&](int s, const std::string& path)
	{
		std::vector<uint8_t> rgb((size_t)o.w * o.h * 3);
		const uint32_t* p = be.pixels(s);
		for (size_t i = 0; i < (size_t)o.w * o.h; i++) { rgb[i * 3] = (p[i] >> 16) & 255; rgb[i * 3 + 1] = (p[i] >> 8) & 255; rgb[i * 3 + 2] = p[i] & 255; }
		return stbi_write_png(path.c_str(), o.w, o.h, 3, rgb.data(), o.w * 3) != 0;
	};
	ui.setHome(0);
	Settings settings(&be, o.assets, o.data, o.h / 480.0f, variant);
	settings.thumbArgs = "--assets " + shellQuote(o.assets) + " --splash " + shellQuote(o.splash) + " --banana " + shellQuote(o.banana) +
		" --systems " + shellQuote(o.systems) + " --roms " + shellQuote(o.roms);
	settings.setSystems(shelfSystems(lib));
	settings.setSync(true);
	settings.searchCount = [&](const std::string& q) { return lib.countMatches(q); };
	LowBattery low;
	int looking = 0;	// "looking 1|2|0": the games-being-read pop-up as the device shows it (a change waiting, the second card)
	be.overlay = [&](int screen)
	{
		if (screen == 0 && low.showing(now)) settings.notice(low.title, low.body, low.percent);
		else if (screen == 0 && looking) settings.popup(looking == 2 ? "Reading the second SD card" : "Looking for games",
			looking == 2 ? "Its games join the list in a moment" : "The game list is updated in a moment");
	};
	while (std::getline(ss, cmd, ';'))
	{
		std::stringstream cs(cmd);
		std::string op;
		cs >> op;
		ui.update(now);
		if (op == "home") { int s = 0; cs >> s; ui.setHome(s); }
		else if (op == "lib") { int s = 0, g = 0; cs >> s >> g; lib.openLibrary(s); ui.setLibrary(s, g); }
		else if (op == "wait") { double ms = 0; cs >> ms; now += ms; }
		// sleep <ms>: as wait, but the time really passes (a helper process working meanwhile: the update download)
		else if (op == "sleep") { double ms = 0; cs >> ms; usleep((useconds_t)(ms * 1000)); now += ms; settings.update(now); }
		else if (op == "upstate") { settings.update(now); logf("upstate: %s", settings.updateState().c_str()); }
		// type <text>: typed on the open keyboard at once (the rest of the command, spaces too; never logged)
		else if (op == "type") { std::string t; std::getline(cs >> std::ws, t); settings.typeText(t); }
		else if (op == "resumeboot") resumeAfterPoweroff(lib, o.data, nullptr);	// as at boot after Power off after sleeping
		else if (op == "battery")
		{
			// battery <percent> [charging]: what /sys would say from now on
			std::string ch;
			cs >> lib.testPower.percent >> ch;
			lib.testPower.charging = ch == "charging";
			if (low.check(lib.power(), shutdownPercent(o.data), now)) logf("battery %d%%: notice %s", lib.testPower.percent, low.showing(now) ? low.title.c_str() : "hidden");
		}
		else if (op == "looking") { cs >> looking; ui.update(now); ui.render(); logf("looking %d", looking); }
		else if (op == "press")
		{
			std::string b; cs >> b;
			static const std::map<std::string, Key> keys = { { "left", Key::LEFT }, { "right", Key::RIGHT }, { "up", Key::UP }, { "down", Key::DOWN },
				{ "a", Key::A }, { "b", Key::B }, { "x", Key::X }, { "y", Key::Y }, { "l", Key::L }, { "r", Key::R }, { "start", Key::START }, { "select", Key::SELECT } };
			rnds::UI::Button btn;
			auto it = keys.find(b);
			if (it == keys.end()) { logf("unknown button %s", b.c_str()); return 2; }
			if (low.hide(now)) logf("press %s: notice hidden", b.c_str());
			if (!settings.isOpen() && it->second == Key::SELECT) { settings.openSearch(lib.searchQuery()); logf("press select: search open"); continue; }
			if (!settings.isOpen() && it->second == Key::X && ui.view() == rnds::UI::LIBRARY && ui.gameIndex() >= 0)
			{
				const std::string& path = lib.gameAt(ui.gameIndex()).path;
				std::string key = lib.gameKey(ui.gameIndex());
				auto t = lib.gameTitles(key);
				settings.openGame(lib.gameAt(ui.gameIndex()).title, path.substr(path.rfind('/') + 1), lib.systemOf(ui.gameIndex()).id == "nds", key, t.name, t.sort, path, lib.gameAt(ui.gameIndex()).scraped);
				logf("press x: game page for %s", path.c_str());
				continue;
			}
			if (settings.isOpen() || it->second == Key::START)
			{
				if (!settings.isOpen()) { settings.open(); logf("press %s: settings open", b.c_str()); continue; }
				int r = (int)settings.press(it->second);
				if (r == Settings::TITLES)
				{
					int sys = ui.systemIndex(), g = ui.gameIndex();
					std::string path = g >= 0 && g < lib.gameCount() ? lib.gameAt(g).path : "";
					lib.setGameTitles(settings.gameKey, settings.gameName, settings.gameSort);
					ui.dataChanged();
					for (int i = 0; i < lib.gameCount(); i++) if (lib.gameAt(i).path == path) { ui.setLibrary(sys, i); break; }
					logf("titles %s: \"%s\" / \"%s\"", settings.gameKey.c_str(), settings.gameName.c_str(), settings.gameSort.c_str());
				}
				if (r == Settings::DELETED) afterDelete(lib, ui, settings.deletedFiles);
				if (r == Settings::SHELVES) afterShelves(lib, ui);
				if (r == Settings::SEARCH)
				{
					lib.setSearch(settings.searchText);
					ui.dataChanged();
					int sh = lib.searchShelf();
					if (sh >= 0) { lib.openLibrary(sh); ui.setLibrary(sh, 0); }
					else ui.setHome(0);
					logf("search \"%s\": shelf %d, %d games", settings.searchText.c_str(), sh, sh >= 0 ? (int)lib.shelf(sh).games.size() : -1);
				}
				logf("press %s: settings %s", b.c_str(), settings.isOpen() ? ("result " + std::to_string(r)).c_str() : "closed");
				continue;
			}
			if (!uiButton(it->second, btn)) { logf("unknown button %s", b.c_str()); return 2; }
			bool used = ui.press(btn);
			logf("press %s: %s, view %d sys %d game %d", b.c_str(), used ? "used" : "unused", (int)ui.view(), ui.systemIndex(), ui.gameIndex());
		}
		else if (op == "setup") { settings.setup(); logf("setup open"); }
		// helpcheck: every Settings row's help (and a game page's) laid out as the top panel would: fits or OVERFLOW
		else if (op == "helpcheck") { int n = settings.helpCheck([&](const std::string& l) { logf("%s", l.c_str()); }); logf("helpcheck: %d texts", n); }
		// search <text>: the search keyboard open with that text typed (START then searches)
		else if (op == "search") { std::string t; std::getline(cs, t); size_t b = t.find_first_not_of(' '); settings.openSearch(b == std::string::npos ? "" : t.substr(b)); logf("search open: \"%s\"", t.c_str()); }
		else if (op == "inkon")
		{
			// inkon <rrggbb>: the text on that accent (Text on accent's mode) and its second line, with their contrast
			std::string h; cs >> h;
			uint32_t bg = (uint32_t)strtoul(h.c_str(), nullptr, 16), ink = rnds::inkOn(bg), sub = rnds::subInkOn(bg);
			logf("inkon %06x: ink %06x (%.1f:1), sub %06x (%.1f:1)", bg, ink, rnds::contrastRatio(ink, bg), sub, rnds::contrastRatio(sub, bg));
		}
		else if (op == "romicon")
		{
			// romicon <path>: the DS ROM's own icon (from a .nds or a .zip), and a pixel from its middle
			std::string path;
			std::getline(cs >> std::ws, path);
			rnds::Bitmap b = rnds::romIcon(path);
			if (b.empty()) logf("romicon %s: none", path.c_str());
			else
			{
				const uint8_t* px = &b.px[(16 * b.w + 16) * 4];
				logf("romicon %s: %dx%d, middle #%02x%02x%02x", path.c_str(), b.w, b.h, px[0], px[1], px[2]);
			}
		}
		else if (op == "mem")
		{
			// mem: what the process holds now (RSS) and the texture cache's share of it
			std::ifstream st("/proc/self/status");
			std::string ln, rss;
			while (std::getline(st, ln)) if (ln.compare(0, 6, "VmRSS:") == 0) rss = ln.substr(6);
			if (auto* rm = dynamic_cast<RndsMenu*>(&ui))
			{
				rnds::UI::CacheStats cs = rm->engine().cacheStats();
				logf("mem: RSS%s; texture cache %zu entries, %.1f MB of a %.1f MB budget", rss.c_str(), cs.entries, cs.bytes / 1048576.0, cs.budget / 1048576.0);
			}
			else logf("mem: RSS%s", rss.c_str());
		}
		else if (op == "touch")
		{
			// touch <screen> <x> <y>: a tap, in px of a 640x480 panel
			int scr = 1; float x = 0, y = 0;
			cs >> scr >> x >> y;
			float k = o.h / 480.0f;
			if (settings.isOpen() && scr == 1)
			{
				settings.touch(x * k, y * k, true);
				logf("touch %.0f %.0f: settings result %d", x, y, (int)settings.touch(x * k, y * k, false));
			}
			else
			{
				ui.touch(scr, x * k, y * k, true);
				logf("touch %.0f %.0f: %s", x, y, ui.touch(scr, x * k, y * k, false) ? "used" : "unused");
			}
		}
		else if (op == "gamepanel")
		{
			// gamepanel <screen> <rom>: the game panel for a RetroArch game on the other screen, with the Pokémon
			// companion when the ROM is a game it knows (its tables read before going on)
			int scr = 1; std::string rom;
			cs >> scr;
			std::getline(cs >> std::ws, rom);
			static pkmn::Companion comp;
			bool pk = comp.open(rom);
			for (int i = 0; pk && i < 300 && !comp.tables(); i++) usleep(10000);
			settings.companion = pk ? &comp : nullptr;
			settings.gameOpen("Pokemon Test", "Game Boy Advance", scr, true);
			logf("gamepanel %s: companion %s", rom.c_str(), pk ? comp.game()->name : "none");
		}
		else if (op == "pokefeed")
		{
			// pokefeed <file>: the companion's requests, then the file as RetroArch's answers to them
			std::string path;
			std::getline(cs >> std::ws, path);
			if (!settings.companion) { logf("pokefeed: no companion"); return 2; }
			std::string r = settings.companion->requests(now);
			std::ifstream f(path);
			std::stringstream b;
			b << f.rdbuf();
			std::string d = b.str();
			settings.companion->feed(d.data(), d.size());
			settings.gameUpdate(now);
			const pkmn::Snapshot& sn = settings.companion->snapshot();
			logf("pokefeed %s: asked %d, party %zu, battle %d, foes %zu", path.c_str(), (int)std::count(r.begin(), r.end(), '\n'),
				sn.party.size(), (int)sn.inBattle, sn.foes.size());
		}
		else if (op == "gcpanel")
		{
			// gcpanel <screen> <definitions folder> <rom>: the game panel with the game companion (gamecomp.h) when a
			// definition there matches the ROM
			int scr = 1; std::string dir, rom;
			cs >> scr >> dir;
			std::getline(cs >> std::ws, rom);
			static gcomp::Engine gc;
			bool on = gc.open(rom, dir);
			settings.companion = nullptr;
			settings.gameComp = on ? &gc : nullptr;
			settings.gameOpen("Companion Test", "Game Boy Advance", scr, true);
			logf("gcpanel %s: companion %s", rom.c_str(), on ? gc.game()->title.c_str() : "none");
		}
		else if (op == "gcaudit")
		{
			// gcaudit <definitions folder>: every definition's pages drawn once (nothing read): what may want a look
			std::string dir;
			std::getline(cs >> std::ws, dir);
			std::istringstream idx(gcomp::makeIndex(dir));
			std::string ln;
			int n = 0, issues = 0;
			while (std::getline(idx, ln))
			{
				if (ln.empty() || ln[0] == '#') continue;
				std::string why;
				auto d = gcomp::loadDef(dir + "/" + ln.substr(0, ln.find('\t')), &why);
				if (!d) { logf("gcaudit %s: %s", ln.c_str(), why.c_str()); issues++; continue; }
				static gcomp::Engine gc;
				gc.openDef(d);
				gc.requests(0);
				settings.companion = nullptr;
				settings.gameComp = &gc;
				settings.gameOpen(d->title, "Game Boy Advance", 1, true);
				issues += settings.gcAudit([&](const std::string& l) { logf("gcaudit %s", l.c_str()); });
				settings.gameClose();
				settings.gameComp = nullptr;
				n++;
			}
			logf("gcaudit: %d definitions, %d to look at", n, issues);
		}
		else if (op == "gcfeed")
		{
			// gcfeed <file>: the game companion's requests (logged), then the file as RetroArch's answers ("-": none)
			std::string path;
			std::getline(cs >> std::ws, path);
			if (!settings.gameComp) { logf("gcfeed: no companion"); return 2; }
			std::string r = settings.gameComp->requests(now + 1e9);	// (always due)
			std::string d;
			if (path != "-") { std::ifstream f(path); std::stringstream b; b << f.rdbuf(); d = b.str(); }
			settings.gameComp->feed(d.data(), d.size());
			settings.gameUpdate(now);
			std::string one = r;
			std::replace(one.begin(), one.end(), '\n', ';');
			logf("gcfeed %s: asked %d (%s) seen %d", path.c_str(), settings.gameComp->reads(), one.c_str(), (int)settings.gameComp->snapshot().seen);
		}
		else if (op == "gtouch")
		{
			// gtouch <x> <y>: a tap on the game panel, in px of a 640x480 panel
			float x = 0, y = 0;
			cs >> x >> y;
			float k = o.h / 480.0f;
			settings.gameTouch(x * k, y * k, true);
			logf("gtouch %.0f %.0f: action %d", x, y, (int)settings.gameTouch(x * k, y * k, false));
		}
		else if (op == "gshot")
		{
			std::string name; cs >> name;
			settings.gameUpdate(now);
			settings.gameRender();
			if (!save(0, o.headless + name + "-top.png") || !save(1, o.headless + name + "-bot.png")) { logf("can't write %s", name.c_str()); return 1; }
			logf("gshot %s", name.c_str());
		}
		else if (op == "shot")
		{
			std::string name; cs >> name;
			double t0 = 0, t1 = 0;
			if (settings.isOpen())
			{
				settings.update(now);	// what came back since the last command (a press skips the update below)
				settings.update(now);
				settings.render();
				t0 = nowMs(); settings.render(); t1 = nowMs();
			}
			else
			{
				settle(); ui.render(); settle(); ui.render();
				t0 = nowMs();
				ui.render();
				t1 = nowMs();
			}
			if (!save(0, o.headless + name + "-top.png") || !save(1, o.headless + name + "-bot.png")) { logf("can't write %s", name.c_str()); return 1; }
			logf("shot %s (frame %.2f ms, wake %d)", name.c_str(), t1 - t0, ui.nextWake());
		}
		else if (op == "splash")
		{
			// splash <name> <ms> [ready]: the boot animation at that time
			std::string name; double t = 0; int ready = 0;
			cs >> name >> t >> ready;
			static Splash sp;
			static bool loaded = false;
			if (!loaded && !(loaded = sp.load(o.splash, o.banana + "/banana-48.png", variant, o.w, o.h))) { logf("no splash assets in %s", o.splash.c_str()); return 1; }
			sp.draw(be.canvas(0), be.canvas(1), t, ready != 0);
			if (!save(0, o.headless + name + "-top.png") || !save(1, o.headless + name + "-bot.png")) return 1;
			logf("splash %s at %.0f ms", name.c_str(), t);
		}
		else if (op == "outro")
		{
			// outro <name> <ms>: the shutdown animation at that time
			std::string name; double t = 0;
			cs >> name >> t;
			static Splash sp;
			static bool loaded = false;
			if (!loaded && !(loaded = sp.load(o.splash, o.banana + "/banana-48.png", variant, o.w, o.h))) { logf("no splash assets in %s", o.splash.c_str()); return 1; }
			sp.onCue = [](const std::string& n) { logf("outro cue %s", n.c_str()); };
			sp.drawOutro(be.canvas(0), be.canvas(1), t);
			if (!save(0, o.headless + name + "-top.png") || !save(1, o.headless + name + "-bot.png")) return 1;
			logf("outro %s at %.0f ms", name.c_str(), t);
		}
		else if (op == "bench")
		{
			// bench N [EVERY BUTTON]: N frames 16.7 ms apart, as the panels would show them: the main thread's time per
			// frame (the engine's and the backend's; the drawing alone after it, then the picture worker's, done between
			// the two here), pressing
			// BUTTON every EVERY frames (scrolling)
			int n = 60, every = 0; std::string bn;
			cs >> n >> every >> bn;
			Key bk = bn == "left" ? Key::LEFT : bn == "up" ? Key::UP : bn == "down" ? Key::DOWN : Key::RIGHT;
			double total = 0, worst = 0, draw = 0, drawWorst = 0, work = 0, workWorst = 0;
			be.skipped = 0;
			be.redrawn = 0;
			for (int i = 0; i < n; i++)
			{
				now += 1000.0 / 60;
				rnds::UI::Button btn;
				if (every > 0 && i % every == 0 && uiButton(bk, btn)) ui.press(btn);
				double t0 = nowMs();
				ui.update(now);
				double t1 = nowMs();
				ui.finishWork();
				double t2 = nowMs();
				ui.render();
				double t3 = nowMs(), dt = (t1 - t0) + (t3 - t2);
				total += dt; worst = std::max(worst, dt);
				draw += t3 - t2; drawWorst = std::max(drawWorst, t3 - t2);
				work += t2 - t1; workWorst = std::max(workWorst, t2 - t1);
			}
			// (the main thread's time: the worker's runs beside it on the device)
			logf("bench: %d frames, %.2f ms average, %.2f ms worst (drawing %.2f / %.2f; the worker %.2f / %.2f), %d of %d screens unchanged, %.1f%% of pixels drawn again", n, total / n, worst,
				draw / n, drawWorst, work / n, workWorst, be.skipped, 2 * n, 100.0 * be.redrawn / (2.0 * n * be.width() * be.height()));
		}
		else if (!op.empty()) { logf("unknown command %s", op.c_str()); return 2; }
		if (settings.isOpen()) { settings.update(now); settings.update(now); }
		if (lib.pendingLaunch() >= 0)
		{
			int g = lib.pendingLaunch();
			logf("launch %d: %s (system %s)", g, lib.gameAt(g).path.c_str(), lib.systemOf(g).id.c_str());
			lib.clearLaunch();
		}
		ui.update(now);
		if (rearrange(lib, ui)) logf("shelf: %d systems, cursor on %s", lib.systemCount(), lib.shelf(ui.systemIndex()).def.id.c_str());
	}
	return 0;
}

// ---- the boot animation and the hand-over to the menu --------------------------------------------------------
// one frame to the panels, then wait for its flip (or a signal)
static void showFrame(Display& disp, SoftBackend& be, int sig)
{
	const uint32_t* scr[2] = { be.pixels(0), be.pixels(1) };
	Damage dmg[2] = { be.takeDamage(0), be.takeDamage(1) };
	if (!disp.present(scr, dmg)) return;
	while (disp.flipPending())
	{
		pollfd p[2] = { { disp.pollFd(), POLLIN, 0 }, { sig, POLLIN, 0 } };
		if (poll(p, sig >= 0 ? 2 : 1, 100) <= 0 || (sig >= 0 && (p[1].revents & POLLIN))) return;
		disp.handleEvents();
	}
}

static Splash gSplash;

// plays until the intro is over and the menu is ready (with the animation off: only the wait for the menu)
static void playSplash(Display& disp, SoftBackend& be, const Options& o, const std::string& variant, bool on,
	std::atomic<bool>& ready, Sound& sound, int sig)
{
	if (!on || !gSplash.load(o.splash, o.banana + "/banana-48.png", variant, be.width(), be.height()))
	{
		while (!ready) usleep(2000);
		return;
	}
	gSplash.onCue = [&sound](const std::string& n) { sound.play(n); };
	double t0 = nowMs();
	for (;;)
	{
		double t = nowMs() - t0;
		bool r = ready;
		if (gSplash.finished(t, r)) break;
		gSplash.draw(be.canvas(0), be.canvas(1), t, r);
		showFrame(disp, be, sig);
		int wait = gSplash.nextFrame(nowMs() - t0, ready);
		if (wait > 16) usleep(std::min(wait, 50) * 1000);	// waiting on the library: look again now and then
	}
}

// The iris: on each panel a circle round its centre, the picture inside it and black outside. Its edge goes in steps
// of the splash's own pixels (2 at 480 lines), as pixel art would draw it; timed by the clock, a frame each refresh.
struct Iris
{
	Display& disp;
	SoftBackend& be;
	int W, H, block;
	size_t n;
	float full;
	// the boot's (the user: the boot animation keeps its length, only the boot itself gets faster); after a game the
	// opening is GAME_OPEN, 3x as fast
	static constexpr double CLOSE = 360, HOLD = 70, OPEN = 450, GAME_OPEN = 150;
	Iris(Display& d, SoftBackend& b) : disp(d), be(b), W(b.width()), H(b.height())
	{
		n = (size_t)W * H;
		block = std::max(1, (int)std::lround(2 * H / 480.0f));
		full = std::sqrt((float)(W * W + H * H)) / 2 + block;	// the corners
	}
	void frame(const std::vector<uint32_t>* src, float r)
	{
		for (int sc = 0; sc < 2; sc++)
		{
			uint32_t* d = be.canvas(sc);
			for (int y0 = 0; y0 < H; y0 += block)
			{
				// the row band's half-width inside the circle, in whole blocks
				float dy = (y0 + block * 0.5f) - H * 0.5f;
				float h2 = r * r - dy * dy;
				int half = h2 > 0 ? (int)(std::sqrt(h2) / block) * block : -1;
				int xa = half < 0 ? W : std::max(0, W / 2 - half), xb = half < 0 ? W : std::min(W, W / 2 + half);
				for (int y = y0; y < std::min(H, y0 + block); y++)
				{
					uint32_t* row = d + (size_t)y * W;
					const uint32_t* sr = src[sc].data() + (size_t)y * W;
					if (xa > 0) std::fill(row, row + xa, 0xff000000u);
					if (xb > xa) std::copy(sr + xa, sr + xb, row + xa);
					if (xb < W) std::fill(row + std::max(xa, xb), row + W, 0xff000000u);
				}
			}
		}
		be.invalidate();
		showFrame(disp, be, -1);
	}
	// the circle closing over a still picture to black: slow at first, faster into the point (no lingering at a small
	// circle)
	void close(const std::vector<uint32_t>* pic, double dur)
	{
		const double t0 = nowMs();
		for (;;)
		{
			double p = (nowMs() - t0) / dur;
			if (p >= 1) break;
			frame(pic, full * (float)(1 - p * p));
		}
		frame(pic, 0);
	}
	// black until holdUntil, then the circle opening on a still picture: fast at first, settling; the picture is left on
	// the canvas
	void open(const std::vector<uint32_t>* pic, double holdUntil, double dur)
	{
		for (int sc = 0; sc < 2; sc++) std::fill(be.canvas(sc), be.canvas(sc) + n, 0xff000000u);
		be.invalidate();
		while (nowMs() < holdUntil) showFrame(disp, be, -1);
		const double t2 = nowMs();
		for (;;)
		{
			double p = (nowMs() - t2) / dur;
			if (p >= 1) break;
			float e = (float)p; e = 1 - (1 - e) * (1 - e) * (1 - e);
			frame(pic, full * e);
		}
		for (int sc = 0; sc < 2; sc++) std::copy(pic[sc].begin(), pic[sc].end(), be.canvas(sc));
		be.invalidate();	// the next frame is drawn in full
	}
	// black for a moment, then the circle opening on the menu. The menu as it will be 300 ms on (before the opening
	// ends, so its clock never goes back), opened on as a still picture: a live menu frame (16-20 ms) each refresh made
	// the opening miss refreshes
	void openOn(Menu& ui, double hold, double dur = OPEN)
	{
		const double t1 = nowMs();
		std::vector<uint32_t> menu[2] = { std::vector<uint32_t>(n), std::vector<uint32_t>(n) };
		ui.update(t1 + 300);
		ui.finishWork();
		be.invalidate();
		ui.render();
		for (int sc = 0; sc < 2; sc++) std::copy(be.canvas(sc), be.canvas(sc) + n, menu[sc].begin());
		open(menu, t1 + hold, dur);
	}
};

// the splash hands over to the menu through the iris: closing over the splash's last frame to black, holding, then
// opening on the menu
static void irisToMenu(Display& disp, SoftBackend& be, Menu& ui)
{
	if (!gSplash.played()) return;
	Iris iris(disp, be);
	std::vector<uint32_t> was[2] = { std::vector<uint32_t>(be.pixels(0), be.pixels(0) + iris.n), std::vector<uint32_t>(be.pixels(1), be.pixels(1) + iris.n) };
	// the menu's first frame drawn now, before the circle closes (its first draw takes ~130 ms: the glyphs and boxes
	// made; with the splash's last frame still and on screen that wait doesn't show), the splash's put back
	ui.update(nowMs());
	ui.finishWork();
	be.invalidate();
	ui.render();
	for (int sc = 0; sc < 2; sc++) std::copy(was[sc].begin(), was[sc].end(), be.canvas(sc));
	iris.close(was, Iris::CLOSE);
	iris.openOn(ui, Iris::HOLD);
}

// The shutdown: the menu (whatever is on the panels) closes through the iris, opens on the boot animation's last
// frame, and the animation plays backwards to black (Splash::drawOutro). ~1.9 s: rcK gives the menu 3 s to go.
static void playOutro(Display& disp, SoftBackend& be, const Options& o, const std::string& variant, Sound& sound)
{
	Splash sp;
	if (!sp.load(o.splash, o.banana + "/banana-48.png", variant, be.width(), be.height())) return;
	sp.onCue = [&sound](const std::string& n) { if (n == "eject") sound.play("back"); };
	Iris iris(disp, be);
	std::vector<uint32_t> was[2] = { std::vector<uint32_t>(be.pixels(0), be.pixels(0) + iris.n), std::vector<uint32_t>(be.pixels(1), be.pixels(1) + iris.n) };
	std::vector<uint32_t> first[2] = { std::vector<uint32_t>(iris.n), std::vector<uint32_t>(iris.n) };
	sp.drawOutro(first[0].data(), first[1].data(), 0);
	iris.close(was, 280);
	iris.open(first, nowMs() + Iris::HOLD, 300);
	const double t0 = nowMs();
	for (;;)
	{
		double t = nowMs() - t0;
		sp.drawOutro(be.canvas(0), be.canvas(1), t);
		be.invalidate();
		showFrame(disp, be, -1);
		if (t >= Splash::OUTRO_END) break;
	}
}

// a game ended: the panels come back black (its pictures went with it), and the menu opens through the iris as it
// does after the boot animation
static void irisFromGame(Display& disp, SoftBackend& be, Menu& ui)
{
	Iris iris(disp, be);
	for (int sc = 0; sc < 2; sc++) std::fill(be.canvas(sc), be.canvas(sc) + iris.n, 0xff000000u);
	be.invalidate();
	showFrame(disp, be, -1);
	iris.openOn(ui, 40, Iris::GAME_OPEN);
}

// The CPU's lowest clock raised to the menu's highest (banana-perf ui: schedutil up to 1416 MHz) from a press and while
// frames change the panels, and back 300 ms after the last one. schedutil only ramps up after an animation has started, so its first
// frames were drawn at 408 MHz, three times slower than the refresh allows on the RG DS Plus's 1024x768 panels (a
// scroll showed ~10-15% of its frames late). Starting a game puts it back as well (banana-perf sets the lowest clock).
struct ClockBoost
{
	const char* dir = "/sys/devices/system/cpu/cpufreq/policy0";
	bool on = false;
	double lastFrame = 0;
	std::string low;
	void write(const char* file, const std::string& v) { std::ofstream(std::string(dir) + "/" + file) << v << "\n"; }
	std::string read(const char* file) { std::ifstream f(std::string(dir) + "/" + file); std::string v; f >> v; return v; }
	void frame(double now)
	{
		lastFrame = now;
		if (on) return;
		if (read("scaling_governor") != "schedutil") return;	// a game's profile, or no cpufreq: leave it
		low = read("scaling_min_freq");
		std::string high = read("scaling_max_freq");
		if (low.empty() || high.empty()) return;
		write("scaling_min_freq", high);
		on = true;
	}
	void idle(double now) { if (on && now - lastFrame > 300) release(); }
	void release() { if (on) { write("scaling_min_freq", low); on = false; } }
	int wake(double now) const { return on ? std::max(1, (int)(lastFrame + 301 - now)) : -1; }	// ms until idle() has work
};

// The bottom panel's idle dim, for a RetroArch game running on the top screen (Mode A: its game panel is the bottom
// screen). After bottom_idle_seconds with no touch on the bottom screen (screen 1) and no button, the bottom panel's
// backlight goes down to bottom_dim_percent of its brightness (0: right off) and comes back at the next touch or
// button, in the same event handler, so the panel is lit again before the next frame. The menu and every non-RetroArch
// program keep the bottom screen awake: only runGame() takes ownership, and only when a RetroArch game panel is the
// bottom screen. Only the bottom backlight is written: the game on the top keeps running untouched, and the screen a
// game draws on is never dimmed. The bottom's path comes from bananad (/run/banana/backlight-bottom, from the
// backlight0); while banana-ui manages it, /run/banana/bottom-ui tells bananad's own menu dim (dim_minutes)
// to leave the bottom alone, so the two never fight over it.
struct BottomIdle
{
	std::string path;			// /sys/class/backlight/<bottom>; "" : no bottom panel (QEMU), nothing to do
	std::string data;			// the card's banana/ folder (/data/banana), for the brightness bananad saved
	int max = 0, level = 0;		// its max_brightness and the level to come back to
	double seconds = 0;			// the idle timeout; 0: off
	int percent = 0;			// dimmed to this percent of its brightness; 0: the backlight off
	bool owned = false;			// banana-ui is managing the bottom (the marker is written, the timer armed)
	bool dimmed = false;
	double last = 0;

	static int readInt(const std::string& p)
	{
		std::ifstream f(p);
		int v = 0;
		f >> v;
		return v;
	}
	void write(int v) { std::ofstream f(path + "/brightness"); f << v << "\n"; }
	// the timeout and the dim level from settings.conf, and the bottom's path from bananad (once, at startup)
	void configure(double sec, int pc, const std::string& dataDir)
	{
		data = dataDir;
		seconds = sec;
		percent = pc;
		path.clear(); max = level = 0;
		std::ifstream f("/run/banana/backlight-bottom");
		std::getline(f, path);
		while (!path.empty() && (path.back() == '\n' || path.back() == '\r' || path.back() == ' ')) path.pop_back();
		if (path.rfind("/sys/class/backlight/", 0) != 0) { path.clear(); return; }
		max = readInt(path + "/max_brightness");
		level = readInt(path + "/brightness");
		if (max <= 0) { path.clear(); return; }
	}
	// banana-ui has (or no longer has) the bottom panel to itself: the marker tells bananad, and losing it restores
	void setOwned(bool on, double now)
	{
		if (on && (seconds <= 0 || path.empty())) on = false;
		if (on == owned) return;
		owned = on;
		if (on) { std::ofstream("/run/banana/bottom-ui") << "1\n"; last = now; }
		else { if (dimmed) { write(want()); dimmed = false; } unlink("/run/banana/bottom-ui"); }
	}
	// the level to come back to: the user's brightness as bananad saved it (a brightness key pressed while the bottom
	// was dimmed changed it), else the level read when it dimmed
	int want()
	{
		// banana/backlight is "<top> <bottom>" in thousandths (one number before 0.4.2: both)
		std::ifstream f(data + "/backlight");
		int top = 0, bottom = 0;
		f >> top;
		if (!(f >> bottom)) bottom = top;
		if (bottom > 0 && bottom <= 1000 && max > 0) return std::max(1, max * bottom / 1000);
		return level;
	}
	// a touch on the panel, an achievement shown on it: the timer restarts, and a dimmed bottom comes back at once
	void activity(double now)
	{
		last = now;
		if (dimmed) { write(want()); dimmed = false; }
	}
	// bananad changed the brightness (F + volume writes every backlight): adopt the level it just set, so the next
	// restore cannot put an older one back
	void adopt(int pct, double now)
	{
		if (max > 0) level = std::max(1, max * std::max(0, std::min(100, pct)) / 100);
		dimmed = false;
		last = now;
	}
	// called from the game panel's loop while a RetroArch game runs (owned); a no-op anywhere else
	void tick(double now)
	{
		if (!owned || dimmed || now - last < seconds * 1000.0) return;
		level = readInt(path + "/brightness");
		// a percent of the brightness it has now, not of the panel's maximum (dimmed to 25% of max, a bottom panel set
		// to 15% got brighter when it "dimmed"); at least 1, so dimmed never reads as off. 0: off
		write(percent <= 0 ? 0 : std::max(1, level * percent / 100));
		dimmed = true;
	}
};

// BANANA_FRAME_STATS=1: a log line for each second the menu presented frames: how many, the gaps between presents in
// refreshes, and the time to draw a frame and to copy it to the panels (for judging the menu's smoothness on a device)
struct FrameStats
{
	bool on = getenv("BANANA_FRAME_STATS") != nullptr;
	double start = 0, last = 0, drawSum = 0, drawMax = 0, copySum = 0, copyMax = 0;
	int frames = 0, draws = 0, gap[4] = {};
	// a press's way to the panels: the kernel's event, the frame it changed drawn (from, to), handed to the panels, and
	// both panels' flips done (the vblank that shows it), one log line a press
	double pressAt = 0, drawFrom = 0, drawTo = 0, presentAt = 0;
	void press(double t)
	{
		if (!on) return;
		if (t <= 0) t = nowMs();
		if (pressAt > 0 && t - pressAt > 1000) pressAt = drawFrom = drawTo = presentAt = 0;	// (one that drew nothing)
		if (pressAt == 0) pressAt = t;
	}
	void draw(double t0, double ms)
	{
		if (!on) return;
		draws++; drawSum += ms; drawMax = std::max(drawMax, ms);
		if (pressAt > 0 && drawFrom == 0) { drawFrom = t0; drawTo = t0 + ms; }
	}
	void flipped(double t)
	{
		if (!on || presentAt == 0) return;
		logf("press: %.1f ms to the panels (read+wait %.1f, draw %.1f, copy+flip wait %.1f)", t - pressAt, drawFrom - pressAt, drawTo - drawFrom, t - drawTo);
		pressAt = drawFrom = drawTo = presentAt = 0;
	}
	void present(double t, double copyMs, bool flipped = true)
	{
		if (!on) return;
		if (drawFrom > 0 && presentAt == 0) { if (flipped) presentAt = t; else pressAt = drawFrom = drawTo = 0; }
		if (last > 0 && t - last < 500) { int g = (int)((t - last) / 16.667 + 0.5); gap[std::min(3, std::max(0, g - 1))]++; }
		if (start == 0) start = t;
		last = t;
		frames++; copySum += copyMs; copyMax = std::max(copyMax, copyMs);
		if (t - start >= 1000)
		{
			logf("frames: %d in %.0f ms, gaps 1:%d 2:%d 3:%d 4+:%d refreshes, draw %.1f/%.1f ms, copy %.1f/%.1f ms (avg/max)",
				frames, t - start, gap[0], gap[1], gap[2], gap[3], draws ? drawSum / draws : 0, drawMax, copySum / frames, copyMax);
			double keep[4] = { pressAt, drawFrom, drawTo, presentAt };
			*this = FrameStats();
			pressAt = keep[0]; drawFrom = keep[1]; drawTo = keep[2]; presentAt = keep[3];
		}
	}
};

int main(int argc, char** argv)
{
	Options o;
	o.assets = envOr("BANANA_ASSETS", o.assets);
	o.systems = envOr("BANANA_SYSTEMS", o.systems);
	o.roms = envOr("BANANA_ROMS", o.roms);
	o.data = envOr("BANANA_DATA", o.data);
	o.top = envOr("BANANA_TOP", o.top);
	o.touch = envOr("BANANA_TOUCH", o.touch);
	// --panels-on: rcS's first step at the boot (in the background): both panels powered up while the boot mounts the
	// card and starts the menu (on the RG DS Plus that power-up is ~1.2 s, on the RG DS ~0.3 s, and the menu waited for
	// it); the menu's own open then finds them lit. Nothing else read or drawn
	if (argc == 2 && !strcmp(argv[1], "--panels-on"))
	{
		Display early;
		early.handOver = true;
		std::string err;
		return early.open(o.top, err) ? 0 : 1;
	}
	for (int i = 1; i < argc; i++)
	{
		std::string a = argv[i];
		auto next = [&]() -> std::string { if (i + 1 >= argc) { logf("%s needs a value", a.c_str()); exit(2); } return argv[++i]; };
		if (a == "--assets") o.assets = next();
		else if (a == "--splash") o.splash = next();
		else if (a == "--banana") o.banana = next();
		else if (a == "--systems") o.systems = next();
		else if (a == "--roms") o.roms = next();
		else if (a == "--data") o.data = next();
		else if (a == "--headless") o.headless = next();
		else if (a == "--script") o.script = next();
		else if (a == "--theme") o.theme = next();
		else if (a == "--size") { std::string s = next(); if (sscanf(s.c_str(), "%dx%d", &o.w, &o.h) != 2) { logf("bad size"); return 2; } }
		else { logf("unknown option %s", a.c_str()); return 2; }
	}

	rnds::setDecoder(stbDecode);
	rnds::setZipReader(readZip);	// the icon of a DS ROM kept in a .zip
	// the card's settings.conf made whole (missing keys added, invalid values put back): first boot and after an update
	normalizeSettings(o.data);
	auto settings = readSettings(o.data);
	if (!o.theme.empty()) settings["theme"] = "file:" + o.theme;	// --theme: the theme being made (headless render)
	// the menu's language (Settings > System > Language): every text laid out goes through its translations; the lines
	// with none stay English and are listed in /run/banana/lang-missing.txt (for the translator's next round)
	const std::string langDir = o.assets + "/../lang";
	if (lang::load(langDir, settings["language"], { "menu", "settings", "systems" }) && lang::active())
	{
		lang::missingLog(envOr("BANANA_LANG_MISSING", "/run/banana/lang-missing.txt"));
		rnds::setTranslator(lang::tr32);
		logf("language: %s (%s), translated by %s", lang::current().name.c_str(), lang::current().code.c_str(), lang::current().by.c_str());
	}
	// the characters Pixelify Sans lacks (CJK) from Fusion Pixel 12px, a pixel font drawn at whole multiples of 12 px;
	// only read for a language that needs it (7 MB). scale: device px per CSS px
	auto cjkFont = [&](float scale)
	{
		if (!lang::active()) return;
		auto f = rnds::Font::load(o.assets + "/fonts/fusion-pixel-12px-proportional-zh_hans.ttf");
		if (f) rnds::setFallbackFont(f, 12, scale);
		else logf("language: no CJK font in %s/fonts", o.assets.c_str());
	};
	// banana/themes on the card, for themes of one's own: the image's README and examples to start from (once)
	if (access((o.data + "/themes").c_str(), F_OK) != 0 && access("/usr/share/banana/themes", F_OK) == 0)
		if (system(("mkdir -p '" + o.data + "/themes' && cp -r /usr/share/banana/themes/. '" + o.data + "/themes/' 2>/dev/null").c_str()) != 0) {}	// (-r: a theme's folder of pictures too)
	// the time zone, for the clock and every game started from here (a POSIX rule, see settings.cpp)
	if (!settings["timezone"].empty()) { setenv("TZ", settings["timezone"].c_str(), 1); tzset(); }
	// the palette (a user theme's colours and accent set on the way) and the accent, before anything is drawn: the
	// splash and the menu in it
	rnds::accentTextMode = rnds::accentTextModeOf(settings["accent_text"]);	// the text on the accent: auto, dark or light
	std::string themeWhy, variant = applyTheme(settings["theme"], settings["accent"], &themeWhy);
	if (!themeWhy.empty()) logf("theme: %s (dark instead)", themeWhy.c_str());
	if (variant == "carts") variant = "dark";
	// beside the logo: the release stage and the version ("0.3.0-dev" -> "Alpha 0.3.0"; /etc/banana/stage names
	// another stage)
	{
		std::string v, stage = "Alpha";
		std::ifstream("/etc/banana/version") >> v;
		std::ifstream sf("/etc/banana/stage"); std::string st; if (std::getline(sf, st) && !st.empty()) stage = st;
		if (!v.empty()) rnds::brandNote = stage + " " + v.substr(0, v.find('-'));
	}
	Host lib(o.systems, o.roms, o.data, o.assets);
	lib.motion = settings["motion"] != "off";
	lib.twelveHour = settings["clock"] == "12h";
	lib.batteryShown = settings["battery"] == "percent" ? "text" : "icon";

	if (!o.headless.empty())
	{
		cjkFont(o.h / 480.0f);
		lib.scan();
		return runHeadless(o, lib, variant, settings["theme"]);
	}

	if (pipe2(sigPipe, O_CLOEXEC | O_NONBLOCK)) return 1;
	struct sigaction sa = {};
	sa.sa_handler = onSignal;
	sigaction(SIGTERM, &sa, nullptr);
	sigaction(SIGINT, &sa, nullptr);
	sigaction(SIGUSR1, &sa, nullptr);	// bananad after a resume: draw everything again
	sigaction(SIGUSR2, &sa, nullptr);	// bananad after a volume or brightness key: the level card (/run/banana/osd)
	signal(SIGPIPE, SIG_IGN);

	Display disp;
	std::string err;
	// the panels' drivers may still be probing when init starts us
	for (int tries = 0; !disp.open(o.top, err); tries++)
	{
		if (tries >= 100) { logf("%s", err.c_str()); return 1; }
		usleep(50000);
	}
	SoftBackend be(disp.width(), disp.height());
	logf("panels %dx%d", disp.width(), disp.height());
	bootMark("banana-ui: panels open");
	// the screen colours (Settings > Display & sound > Screen colours): the one colour table, on the screen it corrects
	auto applyColors = [&](const char* when)
	{
		ScreenColors c = ScreenColors::load(o.data + "/screen-colors.conf");
		std::string e = disp.setColors(c);
		if (!e.empty()) logf("screen colours (%s): %s", when, e.c_str());
		else if (!c.neutral()) logf("screen colours (%s): the %s screen corrected", when, c.screen == 0 ? "top" : "bottom");
	};
	applyColors("start");

	Sound sound(o.assets + "/sounds");
	FrameStats fstat;
	ClockBoost boost;
	sound.enabled = settings["sounds"] != "off";
	// (a library's cue is a launch animation's: its sound the last before the game, so the device closes once it has played)
	lib.onCue = [&sound](const std::string& n) { sound.play(n, true); };

	// the library and the menu are made on another thread while the boot animation plays
	std::shared_ptr<Menu> uiPtr;
	// the theme's menu: Carts (Banana's own), else ROCKNIXDS Pixel's engine in the dark or light palette
	auto makeMenu = [&](const std::string& theme, float sc) -> Menu*
	{
		if (theme == "carts") return new CartMenu(&lib, &be, o.banana + "/fonts", sc);
		std::string layout = themePath(theme);
		if (isLayoutTheme(layout)) { applyTheme(theme, settings["accent"]); return new LayoutMenu(&lib, &be, layout, sc, o.assets + "/fonts", o.banana + "/fonts"); }
		return new RndsMenu(&lib, &be, o.assets, sc, applyTheme(theme, settings["accent"]) == "light" ? "light" : "dark");
	};
	std::atomic<bool> ready{ false };
	std::thread loader([&]
	{
		double t0 = nowMs();
		cjkFont(disp.height() / 480.0f);	// (here, off the boot animation's thread)
		lib.scan();
		logf("library: %d shelves in %.0f ms", lib.systemCount(), nowMs() - t0);
		int startSys = 0;
		{
			std::ifstream f(o.data + "/last-system");
			f >> startSys;
			if (startSys < 0 || startSys >= lib.systemCount()) startSys = 0;
		}
		uiPtr.reset(makeMenu(settings["theme"], disp.height() / 480.0f));
		uiPtr->setHome(startSys);
		// the menu's pictures decoded here too, while the animation plays (on the main thread at the hand-over they
		// stopped the screens for ~200 ms)
		uiPtr->update(nowMs());
		uiPtr->finishWork();
		ready = true;
	});
	playSplash(disp, be, o, variant, settings["splash"] != "off", ready, sound, sigPipe[0]);
	loader.join();
	// in English (no language active) the Language row still names each language in its own script (简体中文) and its
	// help credits the translator: the CJK font read once the menu is up, off the boot's path (7 MB, ~80 ms from the
	// card), so those names aren't boxes
	if (!lang::active() && lang::available(langDir).size() > 1)
	{
		const float sc = disp.height() / 480.0f;
		const std::string path = o.assets + "/fonts/fusion-pixel-12px-proportional-zh_hans.ttf";
		std::thread([path, sc] { if (auto f = rnds::Font::load(path)) rnds::setFallbackFont(f, 12, sc); }).detach();
	}
	Menu* ui = uiPtr.get();
	ui->setLog([](const std::string& m) { logf("%s", m.c_str()); });
	Worker worker;
	worker.start(uiPtr);
	irisToMenu(disp, be, *ui);
	bootMark("banana-ui: the menu");
	// the first boot after the installation: the setup (Wi-Fi, the time zone, the accounts) before anything else
	bool firstSetup = access((o.data + "/setup-done").c_str(), F_OK) != 0;

	Input input;
	input.open(o.touch, disp.width(), disp.height());

	// the bottom panel's idle dim (settings.conf's bottom_idle_seconds / bottom_dim_percent): the bottom backlight's
	// path comes from bananad. It runs only for a RetroArch game whose panel is the bottom screen (Mode A): the menu
	// and every other program keep the bottom screen awake, so ownership is never taken here, only by runGame().
	BottomIdle bottomIdle;
	auto bottomReload = [&]()
	{
		auto cv = [&](const char* k, const char* d) { return getConfValue(o.data + "/settings.conf", k, getConfValue("/etc/banana/settings.conf", k, d)); };
		bottomIdle.setOwned(false, nowMs());	// the menu never dims the bottom screen
		// Settings > Other systems > Game panel when idle: stays on (no timer), dims (to bottom_dim_percent, 25 unless
		// set), or turns off (the backlight at 0); a mode-less card from before the row: a dim percent of 0 is off
		std::string mode = cv("bottom_idle_mode", "");
		int pc = atoi(cv("bottom_dim_percent", "25").c_str());
		if (mode.empty()) mode = pc > 0 ? "dim" : "off";
		double sec = mode == "on" ? 0 : atof(cv("bottom_idle_seconds", "30").c_str());
		bottomIdle.configure(sec, mode == "off" ? 0 : (pc > 0 ? pc : 25), o.data);
	};
	bottomReload();

	Repeat rep;
	bool dirty = true, quit = false, drawn = false;	// drawn: a frame on the canvas waiting for the flips
	bool shuttingDown = false;	// SIGTERM: rcK (a shutdown or a restart), so the shutdown animation plays
	double gameEnded = -1e9;
	double quietUntil = 0;	// no frame before this: the last one flipped nothing (see present below)
	int lastMinute = -1;
	float scale = disp.height() / 480.0f;
	Settings settingsView(&be, o.assets, o.data, scale, variant);
	settingsView.thumbArgs = "--assets " + shellQuote(o.assets) + " --splash " + shellQuote(o.splash) + " --banana " + shellQuote(o.banana) +
		" --systems " + shellQuote(o.systems) + " --roms " + shellQuote(o.roms);	// (the theme gallery's pictures)
	settingsView.setSystems(shelfSystems(lib));
	pkmn::Companion companion;	// the game panel's Pokémon companion (pokemon.h), for the game running
	gcomp::Engine gameComp;		// or its game companion (gamecomp.h), for another GBA game with a definition
	settingsView.setColors = [&](const ScreenColors& c) { std::string e = disp.setColors(c); if (!e.empty()) logf("screen colours: %s", e.c_str()); return e; };
	settingsView.setFontDir(o.banana + "/fonts");
	settingsView.searchCount = [&](const std::string& q) { return lib.countMatches(q); };
	settingsView.setVariant(settings["theme"]);	// (Carts: Inter)
	LowBattery low;
	bool batteryDue = true;
	if (firstSetup) { settingsView.setup(); logf("first-time setup"); }
	RomWatch roms;
	roms.watch(lib.watchDirs());
	// the volume and brightness keys' level card, 1.5 s from the last key (bananad changes the level and says so)
	std::string osdLabel;
	int osdPercent = 0;
	double osdUntil = 0;
	// the games being read again (QA: after an update the game list was empty for a while, then everything came, and
	// nothing said the menu was at work): a pop-up while the second SD card is being mounted (sd2 marks it) and while
	// a change on the card waits for the list to be read again (2 s after the copying went quiet; on the home screen)
	auto looking = [&]() -> int
	{
		if (settingsView.isOpen()) return 0;
		if (access("/run/banana/sd2/busy", F_OK) == 0) return 2;
		return roms.due > 0 ? 1 : 0;
	};
	be.overlay = [&](int screen)
	{
		if (screen != 0) return;
		double t = nowMs();
		if (low.showing(t)) settingsView.notice(low.title, low.body, low.percent);
		else if (osdUntil > t) settingsView.level(osdLabel, osdPercent);
		else if (int k = looking())
			settingsView.popup(k == 2 ? "Reading the second SD card" : "Looking for games",
				k == 2 ? "Its games join the list in a moment" : ui->view() == rnds::UI::HOME ? "The game list is updated in a moment"
				: "The list is updated on the home screen");
		else settingsView.drawToast(t);	// (only while Settings is open: the account check's result)
	};

	// what a change on the settings screen means for the menu (called with uiLock held)
	auto applySettings = [&](Settings::Result r)
	{
		switch (r)
		{
		case Settings::MOVED: sound.play("scroll"); break;
		case Settings::LANGUAGE:
			// every text cached in the menus is in the old language: banana-ui starts again (init runs it anew)
			logf("language changed to %s: starting again", getConfValue(o.data + "/settings.conf", "language", "en").c_str());
			sound.play("select");
			quit = true;
			break;
		case Settings::CHANGED:
		{
			settings = readSettings(o.data);
			lib.motion = settings["motion"] != "off";
			lib.twelveHour = settings["clock"] == "12h";
			lib.batteryShown = settings["battery"] == "percent" ? "text" : "icon";
			sound.enabled = settings["sounds"] != "off";
			bottomReload();	// the idle dim's timeout or level may have changed
			ui->dataChanged();
			sound.play("select");
			break;
		}
		case Settings::THEME:
		{
			// the menu again in the other palette, on the system it was showing
			settings = readSettings(o.data);
			rnds::accentTextMode = rnds::accentTextModeOf(settings["accent_text"]);
			variant = applyTheme(settings["theme"], settings["accent"]);
			if (variant == "carts") variant = "dark";
			int sys = ui->systemIndex();
			uiPtr.reset(makeMenu(settings["theme"], scale));
			ui = uiPtr.get();
			ui->setLog([](const std::string& m) { logf("%s", m.c_str()); });
			worker.attach(uiPtr);
			ui->setHome(sys);
			sound.play("select");
			break;
		}
		case Settings::CLOSED: sound.play("back"); break;
		case Settings::TITLES:
		{
			// a game's own name or sort title typed on its page: the library again, the cursor on the same game
			int sys = ui->systemIndex(), g = ui->gameIndex();
			std::string path = g >= 0 && g < lib.gameCount() ? lib.gameAt(g).path : "";
			lib.setGameTitles(settingsView.gameKey, settingsView.gameName, settingsView.gameSort);
			ui->dataChanged();
			for (int i = 0; i < lib.gameCount(); i++) if (lib.gameAt(i).path == path) { ui->setLibrary(sys, i); break; }
			logf("titles %s: \"%s\" / \"%s\"", settingsView.gameKey.c_str(), settingsView.gameName.c_str(), settingsView.gameSort.c_str());
			sound.play("select");
			break;
		}
		case Settings::SEARCH:
		{
			// the Search collection first on the shelf, opened on its first game (an empty text takes it away)
			lib.setSearch(settingsView.searchText);
			ui->dataChanged();
			int s = lib.searchShelf();
			if (s >= 0) { lib.openLibrary(s); ui->setLibrary(s, 0); }
			else ui->setHome(0);
			logf("search \"%s\": %d games", settingsView.searchText.c_str(), s >= 0 ? (int)lib.shelf(s).games.size() : -1);
			sound.play("select");
			break;
		}
		case Settings::SHELVES: afterShelves(lib, *ui); roms.watch(lib.watchDirs()); break;
		case Settings::DELETED:
		{
			afterDelete(lib, *ui, settingsView.deletedFiles);
			roms.watch(lib.watchDirs());
			sound.play("select");
			break;
		}
		case Settings::REBOOT: logf("restarting"); kill(1, SIGTERM); break;		// busybox init: SIGTERM reboots
		case Settings::POWEROFF: logf("shutting down"); kill(1, SIGUSR2); break;	// and SIGUSR2 powers off
		default: break;
		}
		dirty = true;
	};

	auto handle = [&](const InputEvent& ev)
	{
		// (the bottom screen's idle dim is a game-panel thing only, so its touch handling is in gamePanel's own sink)
		if (ev.type == InputEvent::PRESS || ev.type == InputEvent::TOUCH_DOWN) { boost.frame(nowMs()); fstat.press(ev.t); }	// what it starts drawn fast
		if ((ev.type == InputEvent::PRESS || ev.type == InputEvent::TOUCH_DOWN) && low.hide(nowMs())) dirty = true;
		if (settingsView.isOpen())
		{
			std::lock_guard<std::mutex> l(worker.uiLock);
			if (ev.type == InputEvent::TOUCH_DOWN && ev.screen == 1) settingsView.touch(ev.x, ev.y, true);
			else if (ev.type == InputEvent::TOUCH_UP && ev.screen == 1) applySettings(settingsView.touch(ev.x, ev.y, false));
			else if (ev.type == InputEvent::RELEASE) { if (rep.key == ev.key) rep.key = Key::NONE; }
			else if (ev.type == InputEvent::PRESS)
			{
				applySettings(settingsView.press(ev.key));
				if (ev.key == Key::UP || ev.key == Key::DOWN || ev.key == Key::LEFT || ev.key == Key::RIGHT)
				{
					rep.key = ev.key; rep.since = rep.last = nowMs();
				}
			}
			return;
		}
		if (ev.type == InputEvent::PRESS && ev.key == Key::X)
		{
			// X on a game in the library: its own page (Settings' game page): a DS game's emulator and shader, and any
			// game's name and sort title
			std::lock_guard<std::mutex> l(worker.uiLock);
			int g = ui->gameIndex();
			if (ui->view() == rnds::UI::LIBRARY && g >= 0 && g < lib.gameCount())
			{
				const std::string& path = lib.gameAt(g).path;
				std::string key = lib.gameKey(g);
				auto t = lib.gameTitles(key);
				ui->touchCancel();
				settingsView.setVariant(settings["theme"]);
				settingsView.openGame(lib.gameAt(g).title, path.substr(path.rfind('/') + 1), lib.systemOf(g).id == "nds", key, t.name, t.sort, path, lib.gameAt(g).scraped);
				rep.key = Key::NONE;
				sound.play("select");
				dirty = true;
				return;
			}
		}
		if (ev.type == InputEvent::PRESS && ev.key == Key::SELECT)
		{
			// SELECT: the keyboard, to search the games
			std::lock_guard<std::mutex> l(worker.uiLock);
			if (ui->view() == rnds::UI::LAUNCH) return;
			ui->touchCancel();
			settingsView.setVariant(settings["theme"]);
			settingsView.openSearch(lib.searchQuery());
			rep.key = Key::NONE;
			sound.play("select");
			dirty = true;
			return;
		}
		if (ev.type == InputEvent::PRESS && ev.key == Key::START)
		{
			std::lock_guard<std::mutex> l(worker.uiLock);
			if (ui->view() == rnds::UI::LAUNCH) return;
			ui->touchCancel();
			settingsView.setVariant(settings["theme"]);
			settingsView.open();
			rep.key = Key::NONE;
			sound.play("select");
			dirty = true;
			return;
		}
		if (ev.type == InputEvent::TOUCH_DOWN) { std::lock_guard<std::mutex> l(worker.uiLock); dirty |= ui->touch(ev.screen, ev.x, ev.y, true); return; }
		if (ev.type == InputEvent::TOUCH_MOVE) { std::lock_guard<std::mutex> l(worker.uiLock); dirty |= ui->touchMove(ev.screen, ev.x, ev.y); return; }
		if (ev.type == InputEvent::TOUCH_UP) { std::lock_guard<std::mutex> l(worker.uiLock); dirty |= ui->touch(ev.screen, ev.x, ev.y, false); return; }
		if (ev.type == InputEvent::RELEASE) { if (rep.key == ev.key) rep.key = Key::NONE; return; }
		rnds::UI::Button b;
		if (!uiButton(ev.key, b)) return;
		double now = nowMs();
		std::lock_guard<std::mutex> l(worker.uiLock);
		ui->update(now);
		auto before = ui->view();
		if (ui->press(b))
		{
			dirty = true;
			if (const char* s = soundOf(ev.key, before, ui->view())) sound.play(s);
		}
		if (fstat.on) logf("press %d: view %d sys %d game %d", (int)ev.key, (int)ui->view(), ui->systemIndex(), ui->gameIndex());
		if (ev.key == Key::LEFT || ev.key == Key::RIGHT || ev.key == Key::UP || ev.key == Key::DOWN || ev.key == Key::L || ev.key == Key::R)
		{
			rep.key = ev.key; rep.since = rep.last = now;
		}
	};

	// the game panel's loop while a RetroArch game runs on the other panel: its achievements and tiles drawn when they
	// change, touch on the panel's screen (when the panel is there and that screen has a touchscreen: the RG DS's top one
	// too, with the game on the bottom) turned into RetroArch's commands through the pipe
	// on its stdin (stdin_cmd_enable), until the game ends
	// (outFd: RetroArch's stdout, for the Pokémon companion's replies; -1 without one)
	auto gamePanel = [&](pid_t pid, int& status, int gameScreen, int cmdFd, int outFd, const std::string& title, const std::string& sysName)
	{
		const int panelScreen = 1 - gameScreen;
		{
			std::lock_guard<std::mutex> l(worker.uiLock);
			settingsView.companion = outFd >= 0 && companion.active() ? &companion : nullptr;
			settingsView.gameComp = outFd >= 0 && gameComp.active() ? &gameComp : nullptr;
			settingsView.gameOpen(title, sysName, panelScreen, input.hasTouch(panelScreen));
		}
		be.invalidate();
		bool want = true;
		// the game's end wakes the loop at once (a pidfd is readable then): the 100 ms poll alone kept the menu up to that
		// much longer before it saw it
		const int pidFd = (int)syscall(SYS_pidfd_open, pid, 0);
		for (;;)
		{
			pid_t w = waitpid(pid, &status, WNOHANG);
			if (w == pid || (w < 0 && errno != EINTR)) break;
			double t = nowMs();
			bottomIdle.tick(t);	// the bottom game panel dims on its own idle timer too (the game is on the other)
			if (outFd >= 0)
			{
				// the companion's reads of the game's memory, on RetroArch's stdin beside the tiles' commands
				std::string r = companion.active() ? companion.requests(t) : gameComp.requests(t);
				if (!r.empty() && write(cmdFd, r.data(), r.size()) < 0) {}
			}
			{
				std::lock_guard<std::mutex> l(worker.uiLock);
				if (settingsView.gameUpdate(t)) want = true;
				if (settingsView.gameWake()) bottomIdle.activity(t);	// an achievement on the panel: shown lit
				if (want && !disp.flipPending())
				{
					settingsView.gameRender();
					const uint32_t* scr[2] = { be.pixels(0), be.pixels(1) };
					Damage dmg[2] = { be.takeDamage(0), be.takeDamage(1) };
					disp.present(scr, dmg);
					want = false;
				}
			}
			// for testing over SSH (as /run/banana/launch): "x y" in /run/banana/panel-tap, a tap on the game panel at
			// that place of a 640x480 panel (the companion's tabs and pages, with no hands on the handheld)
			if (access("/run/banana/panel-tap", F_OK) == 0)
			{
				// ("tab N": the game companion's page N, 0-6, 7 the Game tab)
				float tx = -1, ty = -1;
				std::string w;
				{ std::ifstream f("/run/banana/panel-tap"); f >> w; if (w == "tab") f >> tx; else { tx = (float)atof(w.c_str()); f >> ty; } }
				unlink("/run/banana/panel-tap");
				float k = disp.height() / 480.0f;
				std::lock_guard<std::mutex> l(worker.uiLock);
				if (w == "tab") settingsView.gcShow((int)tx);
				else if (tx >= 0 && ty >= 0) { settingsView.gameTouch(tx * k, ty * k, true); settingsView.gameTouch(tx * k, ty * k, false); }
				want = true;
				logf("panel tap %s %.0f %.0f", w.c_str(), tx, ty);
			}
			std::vector<pollfd> fds = { { disp.pollFd(), POLLIN, 0 } };
			for (int fd : input.fds()) fds.push_back({ fd, POLLIN, 0 });
			if (outFd >= 0) fds.push_back({ outFd, POLLIN, 0 });
			if (pidFd >= 0) fds.push_back({ pidFd, POLLIN, 0 });
			if (poll(fds.data(), fds.size(), 100) <= 0) continue;
			if (pidFd >= 0)
			{
				if (fds.back().revents & POLLIN) continue;	// it ended: waitpid above
				fds.pop_back();
			}
			if (fds[0].revents & POLLIN) disp.handleEvents();
			if (outFd >= 0 && (fds.back().revents & POLLIN))
			{
				// (drained every time: RetroArch would wait on a full pipe)
				char buf[16384];
				ssize_t n;
				while ((n = read(outFd, buf, sizeof buf)) > 0)
				{
					if (companion.active()) companion.feed(buf, (size_t)n);
					else
					{
						std::lock_guard<std::mutex> l(worker.uiLock);	// (the panel reads its snapshot while drawing)
						gameComp.feed(buf, (size_t)n);
					}
				}
				fds.pop_back();
			}
			else if (outFd >= 0) fds.pop_back();
			for (size_t i = 1; i < fds.size(); i++)
				if (fds[i].revents & POLLIN)
					input.read(fds[i].fd, [&](const InputEvent& ev)
					{
						if (ev.screen != panelScreen) return;	// buttons are the game's; touch only on the panel
						if (ev.type != InputEvent::TOUCH_DOWN && ev.type != InputEvent::TOUCH_UP) return;
						// a touch on the panel brings a dimmed bottom back (BottomIdle); the game's buttons don't: the
						// panel dims beside a game being played (a QA tester: "goes back with button input, which
						// defeats the point")
						bottomIdle.activity(nowMs());
						Settings::GameAction a;
						{
							std::lock_guard<std::mutex> l(worker.uiLock);
							a = settingsView.gameTouch(ev.x, ev.y, ev.type == InputEvent::TOUCH_DOWN);
						}
						want = true;
						const char* c = a == Settings::G_SAVE ? "SAVE_STATE\n" : a == Settings::G_LOAD ? "LOAD_STATE\n" :
							a == Settings::G_MENU ? "MENU_TOGGLE\n" : a == Settings::G_FFWD ? "FAST_FORWARD\n" :
							a == Settings::G_QUIT ? "QUIT\n" : nullptr;
						if (c)
						{
							logf("game panel: %.*s", (int)strlen(c) - 1, c);
							if (write(cmdFd, c, strlen(c)) < 0) {}
						}
					});
		}
		if (pidFd >= 0) ::close(pidFd);
	};

	auto runGame = [&](int game)
	{
		const SystemDef& sd = lib.systemOf(game);
		std::string rom = lib.gameAt(game).path;
		std::string cmd = replaceAll(replaceAll(sd.run, "%ROM%", shellQuote(rom)), "%SYSTEM%", shellQuote(sd.id));
		const double letGo = nowMs();
		logf("starting %s: %s", sd.id.c_str(), cmd.c_str());
		{
			std::ofstream f(o.data + "/last-system");
			f << ui->systemIndex() << "\n";
		}
		// everything the game wants goes back to the system: the panels, the buttons, the sound device. A RetroArch
		// game with the game panel on (settings.conf retroarch_panel) gets only its own panel, through a DRM lease
		// (Display::lease): the menu keeps the other for the game panel (Settings::gameRender) and reads its touch
		sound.release();
		const double soundMs = nowMs() - letGo;
		boost.release();
		forgetDecoded();
		{
			std::lock_guard<std::mutex> l(worker.uiLock);
			ui->trim();
		}
		const char* FIFO = "/run/banana/retroarch.cmd";
		// run-ps1 and run-pico8's fake-08 fallback end up in run-retroarch just the same, so they get the game panel
		// (settings.conf retroarch_panel) and its touch tiles too; the official PICO-8, when it is on the card, takes
		// the panels itself.
		bool ra = sd.run.find("run-retroarch") != std::string::npos || sd.run.find("run-ps1") != std::string::npos ||
			(sd.run.find("run-pico8") != std::string::npos && access("/data/roms/pico-8/pico8_64", F_OK) != 0);
		bool panel = ra && settings["retroarch_panel"] != "off" && disp.isOpen();
		int gameScreen = 0, leaseFd = -1, cmdFd = -1, outFd = -1;
		const char* OUT = "/run/banana/retroarch.out";	// RetroArch's stdout, for the Pokémon companion
		if (panel)
		{
			// the panel RetroArch is set to use (Settings > Other systems > Screen: video_monitor_index 2 the top, 1 the
			// bottom)
			gameScreen = getConfValue("/data/retroarch/retroarch.cfg", "video_monitor_index", "2") == "1" ? 1 : 0;
			leaseFd = disp.lease(gameScreen);
			unlink(FIFO);
			unlink("/run/banana/retroarch.log");	// (the last game's: the panel must not read it as this one's)
			if (leaseFd >= 0 && mkfifo(FIFO, 0600) == 0) cmdFd = ::open(FIFO, O_RDWR | O_NONBLOCK | O_CLOEXEC);	// (read too: RetroArch never sees an end)
			if (leaseFd < 0 || cmdFd < 0)
			{
				logf("game panel: no lease (%d) or command pipe (%d): the game takes both panels", leaseFd, cmdFd);
				if (leaseFd >= 0) ::close(leaseFd);
				if (cmdFd >= 0) ::close(cmdFd);
				leaseFd = cmdFd = -1;
				panel = false;
			}
		}
		// the idle dim is armed for a RetroArch game in Mode A with its panel on the bottom (the game on the top): the
		// only case where the bottom screen is Banana's own while a game runs. A game drawing on the bottom, or on
		// both, is never dimmed. Tell bananad, which leaves the bottom to us for the game's length.
		bottomIdle.setOwned(panel && gameScreen == 0, nowMs());
		// Settings > Other systems > Pokémon companion, for a Pokémon game it knows (the ROM header): RetroArch's stdout
		// through a pipe the panel reads (held open both ways here: no end seen when RetroArch quits, and RetroArch
		// never meets a pipe with no reader)
		companion.close();
		gameComp.close();
		// Settings > Other systems > Game companion: the same for another GBA game with a definition in
		// /data/banana/companions (the card's own, written or tried out there) or /usr/share/banana/companions
		// (gamecomp.h; the Pokémon games are the Pokémon companion's)
		bool pkOn = panel && sd.id == "gba" && settings["pokemon_companion"] == "on" && companion.open(rom);
		bool gcOn = !pkOn && panel && sd.id == "gba" && settings["game_companion"] == "on" &&
			((access("/data/banana/companions", F_OK) == 0 && gameComp.open(rom, "/data/banana/companions")) || gameComp.open(rom));	// (the card's own first)
		if (pkOn || gcOn)
		{
			unlink(OUT);
			if (mkfifo(OUT, 0600) == 0) outFd = ::open(OUT, O_RDWR | O_NONBLOCK | O_CLOEXEC);
			if (outFd < 0) { logf("companion: no pipe for RetroArch's replies"); companion.close(); gameComp.close(); }
			else if (pkOn) logf("pokemon companion: %s", companion.game()->name);
			else logf("game companion: %s (%s)", gameComp.game()->title.c_str(), gameComp.game()->file.c_str());
		}
		if (!panel)
		{
			input.close();
			disp.close();
		}
		drawn = false;	// the menu is drawn afresh when the panels come back
		rep.key = Key::NONE;
		if (system(("banana-perf " + shellQuote(sd.perf.empty() ? "balanced" : sd.perf)).c_str()) != 0) {}
		const char* PROGRESS = "/run/banana/ra-progress";	// "<unlocked> <total>", from the emulator (docs/EMULATORS.md)
		unlink(PROGRESS);
		double start = nowMs();
		pid_t pid = fork();
		// The parent lets go of its copy of the lease right away, whether the fork worked or not: the child has its own
		// through the fd table it inherited, and a fork that failed (pid < 0) would otherwise leak the lease fd, which
		// nothing else closes on that path.
		if (pid != 0 && leaseFd >= 0) { ::close(leaseFd); leaseFd = -1; }
		if (pid < 0) logf("fork failed: %s", strerror(errno));
		if (pid > 0) logf("game start: the menu let go in %.0f ms (the sound device %.0f ms)", start - letGo, soundMs);
		if (pid == 0)
		{
			setsid();
			signal(SIGPIPE, SIG_DFL);
			sigset_t none;
			sigemptyset(&none);
			sigprocmask(SIG_SETMASK, &none, nullptr);
			setenv("BANANA_ROM", rom.c_str(), 1);
			setenv("BANANA_SYSTEM", sd.id.c_str(), 1);
			setenv("BANANA_RA_PROGRESS", PROGRESS, 1);
			if (panel)
			{
				setenv("BANANA_DRM_LEASE_FD", std::to_string(leaseFd).c_str(), 1);
				setenv("BANANA_RA_CMD", FIFO, 1);
				if (outFd >= 0) setenv("BANANA_RA_OUT", OUT, 1);
			}
			execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
			_exit(127);
		}
		int status = 0;
		if (pid > 0)
		{
			std::ofstream("/run/banana/game.pid") << pid << "\n";	// bananad: the game to pause before a sleep
			if (panel)
			{
				gamePanel(pid, status, gameScreen, cmdFd, outFd, lib.gameAt(game).title, sd.name);
			}
			else while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
			unlink("/run/banana/game.pid");
		}
		const double ended = nowMs();
		if (panel)
		{
			{
				std::lock_guard<std::mutex> l(worker.uiLock);
				int a, b;
				if (settingsView.gameProgress(a, b)) std::ofstream(PROGRESS) << a << " " << b << "\n";	// RetroArch's, as the panel had it
				settingsView.gameClose();
				settingsView.companion = nullptr;
				settingsView.gameComp = nullptr;
			}
			::close(cmdFd);
			unlink(FIFO);
			if (outFd >= 0) { ::close(outFd); unlink(OUT); }
			companion.close();
			gameComp.close();
			input.close();
			disp.close();
		}
		long secs = (long)((nowMs() - start) / 1000);
		logf("game ended after %ld s (status %d)", secs, status);
		int openTries = 0;
		for (; !disp.open(o.top, err) && openTries < 100; openTries++) usleep(50000);
		const double opened = nowMs();
		// the display controller's interrupt storm (from the game's panel, or the lease given back): counted over the
		// iris instead of a 200 ms wait before it (every game's way back waited for it); the full check only if it storms
		const char* forceStorm = getenv("BANANA_STORM_MIN");	// (tests: the full check as before)
		if (forceStorm) disp.stormCheck();
		long long stormIrqs = Display::vopIrqCount();
		double stormFrom = nowMs();
		// (a game's own modeset may have dropped it; the Screen colours page, still up after a game started through the
		// launch file, draws the values itself: no table under it then)
		if (settingsView.colorsOpen()) disp.setColors(ScreenColors());
		else applyColors("after the game");
		be.invalidate();
		input.open(o.touch, disp.width(), disp.height());
		bottomIdle.setOwned(false, nowMs());	// the game session is over: the menu keeps the bottom screen awake
		std::lock_guard<std::mutex> l(worker.uiLock);
		{
			int achA = -1, achB = 0;
			std::ifstream(PROGRESS) >> achA >> achB;
			lib.recordPlay(game, secs, achA, achB);	// (a start that failed straight away isn't a play)
		}
		ui->dataChanged();
		ui->setLibrary(lib.currentShelf(), game);
		if (disp.isOpen()) irisFromGame(disp, be, *ui);
		if (!forceStorm && stormIrqs >= 0 && disp.isOpen())
		{
			long long c = Display::vopIrqCount();
			double ms = nowMs() - stormFrom;
			if (c >= 0 && ms > 50 && (c - stormIrqs) * 1000 / ms > 20000) disp.stormCheck();	// (calm: ~120 a second)
		}
		gameEnded = nowMs();
		// Settings > Network > Home server > Auto sync: the saves to the server now the game has let go of them
		// (banana-sync reads the setting too, and does nothing unpaired or while another sync runs)
		if (settings["homesync_auto"] == "quit" || settings["homesync_auto"] == "both")
		{
			const char* tool = getenv("BANANA_SYNC") ? getenv("BANANA_SYNC") : "/usr/bin/banana-sync";
			if (access(tool, X_OK) == 0)
			{
				pid_t p = fork();
				if (p == 0)
				{
					if (fork() == 0)
					{
						setsid();
						int nul = open("/dev/null", O_RDWR);
						if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
						execl(tool, tool, "sync", "--auto", "quit", (char*)nullptr);
						_exit(127);
					}
					_exit(0);
				}
				if (p > 0) waitpid(p, nullptr, 0);
				logf("game end: home server sync started");
			}
		}
		// the menu's clocks only now: the panels opened again and the iris ran at the game's (~1.4 GHz at most on schedutil
		// otherwise, from the game's end)
		if (system("banana-perf ui") != 0) {}
		logf("game end: the menu back in %.0f ms (its iris over; the panels open in %.0f ms, %d retries)", gameEnded - ended,
			opened - ended, openTries);
		dirty = true;
		batteryDue = true;	// a step passed during the game shows now
	};

	if (system("banana-perf ui") != 0) {}
	// the launch hook (below) wakes the menu when it is written: an idle menu sleeps in poll() until a press otherwise,
	// and a launch written then started its game only at the next press, in the middle of whatever came next
	// the kernel's uevents for the charger (as bananad listens to them): the bolt on the battery icon as the cable goes in
	// or out, not at the next minute's redraw
	int uevent = socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
	{
		sockaddr_nl a = {};
		a.nl_family = AF_NETLINK;
		a.nl_groups = 1;
		if (uevent >= 0 && bind(uevent, (sockaddr*)&a, sizeof a) < 0) { close(uevent); uevent = -1; }
	}
	bool charging = lib.power().charging;
	int launchWatch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (launchWatch >= 0 && inotify_add_watch(launchWatch, "/run/banana", IN_CLOSE_WRITE | IN_MOVED_TO) < 0)
	{
		logf("launch file: can't watch /run/banana: %s", strerror(errno));
		close(launchWatch);
		launchWatch = -1;
	}
	resumeAfterPoweroff(lib, o.data, "/run/banana/resume-once");	// (its game starts at the loop's first turn)
	while (!quit)
	{
		double now = nowMs();
		{
			std::lock_guard<std::mutex> l(worker.uiLock);
			ui->update(now);
		}
		{
			std::lock_guard<std::mutex> l(worker.uiLock);
			if (rearrange(lib, *ui)) dirty = true;
		}
		if (input.rescanDue()) input.open(o.touch, disp.width(), disp.height());	// a pad came or went
		{
			// the display controller's interrupt storm, looked at every 10 s without waiting: over 20000 a second on
			// average since the last look, the full check (it measures again and switches the panels off and on)
			static double stormAt = 0;
			static long long stormIrqs = -1;
			if (now >= stormAt)
			{
				long long c = Display::vopIrqCount();
				if (stormIrqs >= 0 && c >= 0 && stormAt > 0 && (c - stormIrqs) / 10 > 20000) disp.stormCheck();
				stormIrqs = Display::vopIrqCount();
				stormAt = now + 10000;
			}
		}
		if (settingsView.isOpen())
		{
			std::lock_guard<std::mutex> l(worker.uiLock);
			if (settingsView.update(now)) dirty = true;	// a Wi-Fi scan came back, a network joined
			if (settingsView.takeLibraryChanged()) roms.due = now;	// box art fetched: the library again, at home
		}
		// for testing over SSH: a game's path written to /run/banana/launch starts it, as A on it in the library would
		if (access("/run/banana/launch", F_OK) == 0)
		{
			std::string want;
			{
				std::ifstream f("/run/banana/launch");
				std::getline(f, want);
			}
			unlink("/run/banana/launch");
			if (!launchPath(lib, want)) logf("launch file: no game at %s", want.c_str());
		}
		int g = lib.pendingLaunch();
		if (g >= 0)
		{
			lib.clearLaunch();
			runGame(g);
			continue;
		}
		if (rep.due(now))
		{
			rnds::UI::Button b;
			std::lock_guard<std::mutex> l(worker.uiLock);
			if (settingsView.isOpen()) applySettings(settingsView.press(rep.key));
			else if (uiButton(rep.key, b) && ui->press(b)) { dirty = true; sound.play("scroll"); }
		}
		// the clock on the top screen
		time_t wall = time(nullptr);
		int minute = (int)(wall / 60);
		if (minute != lastMinute) { lastMinute = minute; dirty = true; batteryDue = true; }
		if (roms.due > 0 && now >= roms.due && !settingsView.isOpen())
		{
			std::lock_guard<std::mutex> l(worker.uiLock);
			if (ui->view() == rnds::UI::HOME)
			{
				roms.due = 0;
				std::string id = lib.systemCount() ? lib.shelf(ui->systemIndex()).def.id : "";
				lib.scan();
				int at = 0;
				for (int i = 0; i < lib.systemCount(); i++) if (lib.shelf(i).def.id == id) at = i;
				ui->dataChanged();
				ui->setHome(at);
				roms.watch(lib.watchDirs());
				logf("the games on the card changed: %d systems", lib.systemCount());
				dirty = true;
			}
		}
		if (batteryDue)
		{
			batteryDue = false;
			if (low.check(lib.power(), shutdownPercent(o.data), now)) dirty = true;
		}

		bool want;
		{
			std::lock_guard<std::mutex> l(worker.uiLock);
			want = dirty || (settingsView.isOpen() ? settingsView.animating() : (ui->animating() || ui->nextWake() == 0));
		}
		// the next frame is drawn while the last one's flips are still pending (present copies it out, so the canvas is
		// free again at once), and handed over when both panels have flipped: drawing (~10 ms on the RG DS) and the wait
		// for the two panels' unaligned vblanks overlap instead of adding up to two refreshes a frame
		if (want && disp.isOpen() && !drawn && nowMs() >= quietUntil)
		{
			double t0 = nowMs();
			{
				std::lock_guard<std::mutex> l(worker.uiLock);
				if (settingsView.isOpen()) settingsView.render();
				else ui->render();
			}
			fstat.draw(t0, nowMs() - t0);
			drawn = true;
			dirty = false;
		}
		if (drawn && disp.isOpen() && !disp.flipPending())
		{
			const uint32_t* scr[2] = { be.pixels(0), be.pixels(1) };
			Damage dmg[2] = { be.takeDamage(0), be.takeDamage(1) };
			double t0 = nowMs();
			bool flipped = disp.present(scr, dmg);
			fstat.present(t0, nowMs() - t0, flipped);
			drawn = false;
			// a frame that changed nothing on the panels flips nothing, so no flip event paces the next one: it waits a
			// refresh instead (an animation that draws the same pixels, a picture still loading, ran this loop at
			// 400-900 frames a second on one core)
			if (!flipped) quietUntil = t0 + 1000.0 / 60;
			else boost.frame(t0);	// moving: the clock stays up (frames that change nothing don't keep it)
			continue;	// see whether the next frame is due before sleeping
		}

		int timeout;
		{
			std::lock_guard<std::mutex> l(worker.uiLock);
			timeout = want || drawn ? -1 : (settingsView.isOpen() ? settingsView.nextWake(now) : ui->nextWake());	// a frame is wanted: the flip event wakes us
		}
		if (want && !drawn && !disp.flipPending())	// ...unless no flip is pending: then the end of the quiet refresh
			timeout = std::max(1, (int)(quietUntil - nowMs() + 0.999));
		boost.idle(nowMs());
		if (!want && !drawn)
		{
			int b = boost.wake(nowMs());
			if (b >= 0 && (timeout < 0 || b < timeout)) timeout = b;
		}
		int toMinute = (int)((60 - wall % 60) * 1000);
		if (timeout < 0 || timeout > toMinute) timeout = want || drawn ? timeout : toMinute;
		int r = rep.wait(now);
		if (r >= 0 && (timeout < 0 || r < timeout)) timeout = r;
		bool rescanCan;
		{
			std::lock_guard<std::mutex> l(worker.uiLock);
			rescanCan = roms.due > 0 && !settingsView.isOpen() && ui->view() == rnds::UI::HOME;	// else the next press brings it
		}
		if (rescanCan)
		{
			int left = std::max(0, (int)(roms.due - now)) + 1;
			if (timeout < 0 || left < timeout) timeout = left;
		}
		if (low.showing(now))
		{
			int left = (int)(low.until - now) + 1;
			if (timeout < 0 || left < timeout) timeout = left;
		}
		if (osdUntil > now)
		{
			int left = (int)(osdUntil - now) + 1;
			if (timeout < 0 || left < timeout) timeout = left;
		}

		std::vector<pollfd> fds;
		fds.push_back({ sigPipe[0], POLLIN, 0 });
		fds.push_back({ worker.wakeFd, POLLIN, 0 });
		if (roms.fd >= 0) fds.push_back({ roms.fd, POLLIN, 0 });
		if (launchWatch >= 0) fds.push_back({ launchWatch, POLLIN, 0 });
		if (uevent >= 0) fds.push_back({ uevent, POLLIN, 0 });
		if (disp.isOpen()) fds.push_back({ disp.pollFd(), POLLIN, 0 });
		for (int fd : input.fds()) fds.push_back({ fd, POLLIN, 0 });
		int n = poll(fds.data(), fds.size(), timeout);
		if (n == 0 && !want) dirty = true;	// the engine's next step is due
		if (n <= 0) continue;
		for (auto& p : fds)
		{
			if (!(p.revents & POLLIN)) continue;
			if (p.fd == sigPipe[0])
			{
				char c;
				while (read(sigPipe[0], &c, 1) == 1)
				{
					if (c == SIGTERM || c == SIGINT) quit = true;
					if (c == SIGTERM) shuttingDown = true;
					// after a wake: the kernel has the panels showing our buffers again; a redraw brings the clock and
					// the battery up to date (no modeset: that would run the panels' power-on sequence once more)
					if (c == SIGUSR1) { bottomIdle.activity(nowMs()); std::lock_guard<std::mutex> l(worker.uiLock); be.invalidate(); ui->dataChanged(); dirty = true; batteryDue = true; }
					if (c == SIGUSR2)
					{
						// "volume 85" / "brightness 40", written just now (an old one is from a key pressed in a game)
						struct stat st;
						std::ifstream f("/run/banana/osd");
						std::string what; int pct = -1;
						if (stat("/run/banana/osd", &st) == 0 && time(nullptr) - st.st_mtime <= 2 && (f >> what >> pct) && pct >= 0)
						{
							osdLabel = what == "brightness" ? "Brightness" : "Volume";
							osdPercent = pct;
							osdUntil = nowMs() + 1500;
							if (what == "brightness") bottomIdle.adopt(pct, nowMs());	// bananad already wrote it
							dirty = true;
						}
					}
				}
			}
			else if (p.fd == roms.fd)
			{
				roms.read(nowMs());
				dirty = true;	// (the pop-up saying so)
			}
			else if (p.fd == launchWatch)
			{
				alignas(inotify_event) char buf[1024];
				while (read(launchWatch, buf, sizeof buf) > 0) {}	// (the loop's next pass looks for the launch file)
			}
			else if (p.fd == uevent)
			{
				char buf[4096];
				ssize_t got;
				bool power = false;
				while ((got = recv(uevent, buf, sizeof buf - 1, 0)) > 0)
				{
					buf[got] = 0;
					for (char* s = buf; s < buf + got; s += strlen(s) + 1) if (!strcmp(s, "SUBSYSTEM=power_supply")) power = true;
				}
				if (power)
				{
					lib.powerChanged();
					bool c = lib.power().charging;
					if (c != charging) { charging = c; dirty = true; batteryDue = true; logf("charger %s", c ? "in" : "out"); }
				}
			}
			else if (p.fd == worker.wakeFd)
			{
				uint64_t v;
				if (read(worker.wakeFd, &v, 8) < 0) {}
				dirty = true;
			}
			else if (disp.isOpen() && p.fd == disp.pollFd())
			{
				disp.handleEvents();
				if (!disp.flipPending()) fstat.flipped(nowMs());
			}
			else
				input.read(p.fd, handle);
		}
	}
	{
		std::ofstream f(o.data + "/last-system");
		f << ui->systemIndex() << "\n";
	}
	bottomIdle.setOwned(false, nowMs());	// the backlight and the marker back as they were
	worker.finish();
	// not straight after a game: a shutdown from it (the power button held) gives rcK's 3 s to its saves instead
	if (shuttingDown && disp.isOpen() && settings["splash"] != "off" && nowMs() - gameEnded > 2000)
	{
		logf("shutdown animation");
		playOutro(disp, be, o, variant, sound);
	}
	return 0;
}
