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
#include "settings.h"
#include "zip.h"
#include "sound.h"
#include "menu.h"
#include "carts.h"

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
#include <sys/stat.h>
#include <map>
#include <memory>
#include <mutex>
#include <poll.h>
#include <sstream>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <thread>
#include <time.h>
#include <unistd.h>

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

static rnds::Bitmap stbDecode(const std::string& path)
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

// settings.conf: key = value lines (theme, motion, sounds, clock)
static std::map<std::string, std::string> readSettings(const std::string& path)
{
	std::map<std::string, std::string> r;
	std::ifstream f(path);
	std::string ln;
	while (std::getline(f, ln))
	{
		size_t eq = ln.find('=');
		if (ln.empty() || ln[0] == '#' || eq == std::string::npos) continue;
		auto t = [](std::string s) { size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r"); return a == std::string::npos ? std::string() : s.substr(a, b - a + 1); };
		r[t(ln.substr(0, eq))] = t(ln.substr(eq + 1));
	}
	return r;
}

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

// ---- the worker that decodes and fits pictures off the main thread ----------------------------------------------
struct Worker
{
	std::shared_ptr<Menu> ui;	// (shared: a menu doing its own locking is kept alive by the job running without uiLock)
	std::mutex uiLock, wlock;
	std::condition_variable cv;
	bool stop = false, work = false;
	int wakeFd = -1;		// eventfd: a job finished, the main loop has something to upload
	std::thread th;

	void start(std::shared_ptr<Menu> u)
	{
		ui = u;
		wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		ui->setNotifiers([this] { kick(); }, [this] { uint64_t one = 1; if (write(wakeFd, &one, 8) < 0) {} });
		th = std::thread([this] { run(); });
	}
	// a new UI (the theme changed): called with uiLock held
	void attach(std::shared_ptr<Menu> u)
	{
		ui = u;
		ui->setNotifiers([this] { kick(); }, [this] { uint64_t one = 1; if (write(wakeFd, &one, 8) < 0) {} });
	}
	void kick()
	{
		{ std::lock_guard<std::mutex> l(wlock); work = true; }
		cv.notify_all();
	}
	void run()
	{
		for (;;)
		{
			{
				std::unique_lock<std::mutex> l(wlock);
				cv.wait(l, [this] { return stop || work; });
				if (stop) return;
				work = false;
			}
			for (;;)
			{
				{ std::lock_guard<std::mutex> l(wlock); if (stop) return; }
				std::shared_ptr<Menu> u;
				bool more;
				{
					std::unique_lock<std::mutex> l(uiLock);
					u = ui;
					// a menu that guards its own state (the Carts theme, whose jobs render whole cartridges for tens of
					// milliseconds) works without the lock the frames need; the rest with it
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
		if (th.joinable()) th.join();
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
	void watch(const std::vector<std::string>& dirs)
	{
		if (fd < 0) fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
		if (fd < 0) return;
		for (auto& d : dirs)	// a folder that isn't there yet is caught by its parent's watch, then added here
			inotify_add_watch(fd, d.c_str(), IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE);
	}
	void read(double now)
	{
		char buf[4096];
		bool any = false;
		while (::read(fd, buf, sizeof buf) > 0) any = true;
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

// ---- headless: the same UI rendered to PNGs, for tests and screenshots ------------------------------------------
static int runHeadless(const Options& o, Host& lib, const std::string& variant, const std::string& theme)
{
	SoftBackend be(o.w, o.h);
	std::unique_ptr<Menu> uiPtr(theme == "carts" ? (Menu*)new CartMenu(&lib, &be, o.banana + "/fonts", o.h / 480.0f)
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
	settings.setSync(true);
	LowBattery low;
	be.overlay = [&](int screen) { if (screen == 0 && low.showing(now)) settings.notice(low.title, low.body, low.percent); };
	while (std::getline(ss, cmd, ';'))
	{
		std::stringstream cs(cmd);
		std::string op;
		cs >> op;
		ui.update(now);
		if (op == "home") { int s = 0; cs >> s; ui.setHome(s); }
		else if (op == "lib") { int s = 0, g = 0; cs >> s >> g; lib.openLibrary(s); ui.setLibrary(s, g); }
		else if (op == "wait") { double ms = 0; cs >> ms; now += ms; }
		else if (op == "battery")
		{
			// battery <percent> [charging]: what /sys would say from now on
			std::string ch;
			cs >> lib.testPower.percent >> ch;
			lib.testPower.charging = ch == "charging";
			if (low.check(lib.power(), shutdownPercent(o.data), now)) logf("battery %d%%: notice %s", lib.testPower.percent, low.showing(now) ? low.title.c_str() : "hidden");
		}
		else if (op == "press")
		{
			std::string b; cs >> b;
			static const std::map<std::string, Key> keys = { { "left", Key::LEFT }, { "right", Key::RIGHT }, { "up", Key::UP }, { "down", Key::DOWN },
				{ "a", Key::A }, { "b", Key::B }, { "x", Key::X }, { "y", Key::Y }, { "l", Key::L }, { "r", Key::R }, { "start", Key::START } };
			rnds::UI::Button btn;
			auto it = keys.find(b);
			if (it == keys.end()) { logf("unknown button %s", b.c_str()); return 2; }
			if (low.hide(now)) logf("press %s: notice hidden", b.c_str());
			if (settings.isOpen() || it->second == Key::START)
			{
				if (!settings.isOpen()) { settings.open(); logf("press %s: settings open", b.c_str()); continue; }
				int r = (int)settings.press(it->second);
				logf("press %s: settings %s", b.c_str(), settings.isOpen() ? ("result " + std::to_string(r)).c_str() : "closed");
				continue;
			}
			if (!uiButton(it->second, btn)) { logf("unknown button %s", b.c_str()); return 2; }
			bool used = ui.press(btn);
			logf("press %s: %s, view %d sys %d game %d", b.c_str(), used ? "used" : "unused", (int)ui.view(), ui.systemIndex(), ui.gameIndex());
		}
		else if (op == "setup") { settings.setup(); logf("setup open"); }
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
		else if (op == "bench")
		{
			// bench N [EVERY BUTTON]: N frames 16.7 ms apart, as the panels would show them: the engine's and the backend's
			// time per frame (with the picture worker's, done in the frame here; the drawing alone after it), pressing
			// BUTTON every EVERY frames (scrolling)
			int n = 60, every = 0; std::string bn;
			cs >> n >> every >> bn;
			Key bk = bn == "left" ? Key::LEFT : bn == "up" ? Key::UP : bn == "down" ? Key::DOWN : Key::RIGHT;
			double total = 0, worst = 0, draw = 0, drawWorst = 0;
			be.skipped = 0;
			be.redrawn = 0;
			for (int i = 0; i < n; i++)
			{
				now += 1000.0 / 60;
				rnds::UI::Button btn;
				if (every > 0 && i % every == 0 && uiButton(bk, btn)) ui.press(btn);
				double t0 = nowMs();
				ui.update(now);
				ui.finishWork();
				double t1 = nowMs();
				ui.render();
				double t2 = nowMs(), dt = t2 - t0;
				total += dt; worst = std::max(worst, dt);
				draw += t2 - t1; drawWorst = std::max(drawWorst, t2 - t1);
			}
			logf("bench: %d frames, %.2f ms average, %.2f ms worst (drawing %.2f / %.2f), %d of %d screens unchanged, %.1f%% of pixels drawn again", n, total / n, worst,
				draw / n, drawWorst, be.skipped, 2 * n, 100.0 * be.redrawn / (2.0 * n * be.width() * be.height()));
		}
		else if (!op.empty()) { logf("unknown command %s", op.c_str()); return 2; }
		if (settings.isOpen()) { settings.update(now); settings.update(now); }
		if (lib.pendingLaunch() >= 0) { logf("launch %d: %s", lib.pendingLaunch(), lib.gameAt(lib.pendingLaunch()).path.c_str()); lib.clearLaunch(); }
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
	static constexpr double CLOSE = 360, HOLD = 70, OPEN = 450;
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
	// black for a moment, then the circle opening on the menu: fast at first, settling. The menu as it will be 300 ms
	// on (before the opening ends, so its clock never goes back), opened on as a still picture: a live menu frame
	// (16-20 ms) each refresh made the opening miss refreshes
	void openOn(Menu& ui, double hold)
	{
		const double t1 = nowMs();
		std::vector<uint32_t> menu[2] = { std::vector<uint32_t>(n), std::vector<uint32_t>(n) };
		ui.update(t1 + 300);
		ui.finishWork();
		be.invalidate();
		ui.render();
		for (int sc = 0; sc < 2; sc++) std::copy(be.canvas(sc), be.canvas(sc) + n, menu[sc].begin());
		for (int sc = 0; sc < 2; sc++) std::fill(be.canvas(sc), be.canvas(sc) + n, 0xff000000u);	// black again
		be.invalidate();
		while (nowMs() - t1 < hold) showFrame(disp, be, -1);
		const double t2 = nowMs();
		for (;;)
		{
			double p = (nowMs() - t2) / OPEN;
			if (p >= 1) break;
			float e = (float)p; e = 1 - (1 - e) * (1 - e) * (1 - e);
			frame(menu, full * e);
		}
		for (int sc = 0; sc < 2; sc++) std::copy(menu[sc].begin(), menu[sc].end(), be.canvas(sc));
		be.invalidate();	// the menu's next frame is drawn in full
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
	const double t0 = nowMs();
	for (;;)	// closing over the splash: slow at first, faster into the point (no lingering at a small circle)
	{
		double p = (nowMs() - t0) / Iris::CLOSE;
		if (p >= 1) break;
		iris.frame(was, iris.full * (float)(1 - p * p));
	}
	iris.frame(was, 0);
	iris.openOn(ui, Iris::HOLD);
}

// a game ended: the panels come back black (its pictures went with it), and the menu opens through the iris as it
// does after the boot animation
static void irisFromGame(Display& disp, SoftBackend& be, Menu& ui)
{
	Iris iris(disp, be);
	for (int sc = 0; sc < 2; sc++) std::fill(be.canvas(sc), be.canvas(sc) + iris.n, 0xff000000u);
	be.invalidate();
	showFrame(disp, be, -1);
	iris.openOn(ui, 120);
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

// BANANA_FRAME_STATS=1: a log line for each second the menu presented frames: how many, the gaps between presents in
// refreshes, and the time to draw a frame and to copy it to the panels (for judging the menu's smoothness on a device)
struct FrameStats
{
	bool on = getenv("BANANA_FRAME_STATS") != nullptr;
	double start = 0, last = 0, drawSum = 0, drawMax = 0, copySum = 0, copyMax = 0;
	int frames = 0, draws = 0, gap[4] = {};
	void draw(double ms) { if (!on) return; draws++; drawSum += ms; drawMax = std::max(drawMax, ms); }
	void present(double t, double copyMs)
	{
		if (!on) return;
		if (last > 0 && t - last < 500) { int g = (int)((t - last) / 16.667 + 0.5); gap[std::min(3, std::max(0, g - 1))]++; }
		if (start == 0) start = t;
		last = t;
		frames++; copySum += copyMs; copyMax = std::max(copyMax, copyMs);
		if (t - start >= 1000)
		{
			logf("frames: %d in %.0f ms, gaps 1:%d 2:%d 3:%d 4+:%d refreshes, draw %.1f/%.1f ms, copy %.1f/%.1f ms (avg/max)",
				frames, t - start, gap[0], gap[1], gap[2], gap[3], draws ? drawSum / draws : 0, drawMax, copySum / frames, copyMax);
			*this = FrameStats();
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
		else if (a == "--size") { std::string s = next(); if (sscanf(s.c_str(), "%dx%d", &o.w, &o.h) != 2) { logf("bad size"); return 2; } }
		else { logf("unknown option %s", a.c_str()); return 2; }
	}

	rnds::setDecoder(stbDecode);
	rnds::setZipReader(readZip);	// the icon of a DS ROM kept in a .zip
	auto settings = readSettings(o.data + "/settings.conf");
	// the time zone, for the clock and every game started from here (a POSIX rule, see settings.cpp)
	if (!settings["timezone"].empty()) { setenv("TZ", settings["timezone"].c_str(), 1); tzset(); }
	std::string variant = settings["theme"] == "light" ? "light" : "dark";
	rnds::setAccent(settings["accent"]);	// before anything is drawn: the splash and the menu in it
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

	Sound sound(o.assets + "/sounds");
	FrameStats fstat;
	ClockBoost boost;
	sound.enabled = settings["sounds"] != "off";
	lib.onCue = [&sound](const std::string& n) { sound.play(n); };

	// the library and the menu are made on another thread while the boot animation plays
	std::shared_ptr<Menu> uiPtr;
	// the theme's menu: Carts (Banana's own), else ROCKNIXDS Pixel's engine in the dark or light palette
	auto makeMenu = [&](const std::string& theme, float sc) -> Menu*
	{
		if (theme == "carts") return new CartMenu(&lib, &be, o.banana + "/fonts", sc);
		return new RndsMenu(&lib, &be, o.assets, sc, theme == "light" ? "light" : "dark");
	};
	std::atomic<bool> ready{ false };
	std::thread loader([&]
	{
		double t0 = nowMs();
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

	Repeat rep;
	bool dirty = true, quit = false, drawn = false;	// drawn: a frame on the canvas waiting for the flips
	double quietUntil = 0;	// no frame before this: the last one flipped nothing (see present below)
	int lastMinute = -1;
	float scale = disp.height() / 480.0f;
	Settings settingsView(&be, o.assets, o.data, scale, variant);
	settingsView.setFontDir(o.banana + "/fonts");
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
	be.overlay = [&](int screen)
	{
		if (screen != 0) return;
		double t = nowMs();
		if (low.showing(t)) settingsView.notice(low.title, low.body, low.percent);
		else if (osdUntil > t) settingsView.level(osdLabel, osdPercent);
		else settingsView.drawToast(t);	// (only while Settings is open: the account check's result)
	};

	// what a change on the settings screen means for the menu (called with uiLock held)
	auto applySettings = [&](Settings::Result r)
	{
		switch (r)
		{
		case Settings::MOVED: sound.play("scroll"); break;
		case Settings::CHANGED:
		{
			settings = readSettings(o.data + "/settings.conf");
			lib.motion = settings["motion"] != "off";
			lib.twelveHour = settings["clock"] == "12h";
			lib.batteryShown = settings["battery"] == "percent" ? "text" : "icon";
			sound.enabled = settings["sounds"] != "off";
			ui->dataChanged();
			sound.play("select");
			break;
		}
		case Settings::THEME:
		{
			// the menu again in the other palette, on the system it was showing
			settings = readSettings(o.data + "/settings.conf");
			variant = settings["theme"] == "light" ? "light" : "dark";
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
		case Settings::REBOOT: logf("restarting"); kill(1, SIGTERM); break;		// busybox init: SIGTERM reboots
		case Settings::POWEROFF: logf("shutting down"); kill(1, SIGUSR2); break;	// and SIGUSR2 powers off
		default: break;
		}
		dirty = true;
	};

	auto handle = [&](const InputEvent& ev)
	{
		if (ev.type == InputEvent::PRESS || ev.type == InputEvent::TOUCH_DOWN) boost.frame(nowMs());	// what it starts drawn fast
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
		if (ev.type == InputEvent::PRESS && ev.key == Key::START)
		{
			std::lock_guard<std::mutex> l(worker.uiLock);
			if (ui->view() == rnds::UI::LAUNCH) return;
			ui->touchCancel();
			settingsView.setVariant(settings["theme"] == "carts" ? "carts" : variant);
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
		if (ev.key == Key::LEFT || ev.key == Key::RIGHT || ev.key == Key::UP || ev.key == Key::DOWN || ev.key == Key::L || ev.key == Key::R)
		{
			rep.key = ev.key; rep.since = rep.last = now;
		}
	};

	// the game panel's loop while a RetroArch game runs on the other panel: its achievements and tiles drawn when they
	// change, touch on the bottom panel (when the panel is there) turned into RetroArch's commands through the pipe
	// on its stdin (stdin_cmd_enable), until the game ends
	auto gamePanel = [&](pid_t pid, int& status, int gameScreen, int cmdFd, const std::string& title, const std::string& sysName)
	{
		{
			std::lock_guard<std::mutex> l(worker.uiLock);
			settingsView.gameOpen(title, sysName, 1 - gameScreen, gameScreen == 0);
		}
		be.invalidate();
		bool want = true;
		for (;;)
		{
			pid_t w = waitpid(pid, &status, WNOHANG);
			if (w == pid || (w < 0 && errno != EINTR)) break;
			double t = nowMs();
			{
				std::lock_guard<std::mutex> l(worker.uiLock);
				if (settingsView.gameUpdate(t)) want = true;
				if (want && !disp.flipPending())
				{
					settingsView.gameRender();
					const uint32_t* scr[2] = { be.pixels(0), be.pixels(1) };
					Damage dmg[2] = { be.takeDamage(0), be.takeDamage(1) };
					disp.present(scr, dmg);
					want = false;
				}
			}
			std::vector<pollfd> fds = { { disp.pollFd(), POLLIN, 0 } };
			for (int fd : input.fds()) fds.push_back({ fd, POLLIN, 0 });
			if (poll(fds.data(), fds.size(), 100) <= 0) continue;
			if (fds[0].revents & POLLIN) disp.handleEvents();
			for (size_t i = 1; i < fds.size(); i++)
				if (fds[i].revents & POLLIN)
					input.read(fds[i].fd, [&](const InputEvent& ev)
					{
						if (gameScreen != 0 || ev.screen != 1) return;	// buttons are the game's; touch only on the panel
						if (ev.type != InputEvent::TOUCH_DOWN && ev.type != InputEvent::TOUCH_UP) return;
						Settings::GameAction a;
						{
							std::lock_guard<std::mutex> l(worker.uiLock);
							a = settingsView.gameTouch(ev.x, ev.y, ev.type == InputEvent::TOUCH_DOWN);
						}
						want = true;
						const char* c = a == Settings::G_SAVE ? "SAVE_STATE\n" : a == Settings::G_LOAD ? "LOAD_STATE\n" :
							a == Settings::G_MENU ? "MENU_TOGGLE\n" : a == Settings::G_FFWD ? "FAST_FORWARD\n" : a == Settings::G_QUIT ? "QUIT\n" : nullptr;
						if (c)
						{
							logf("game panel: %.*s", (int)strlen(c) - 1, c);
							if (write(cmdFd, c, strlen(c)) < 0) {}
						}
					});
		}
	};

	auto runGame = [&](int game)
	{
		const SystemDef& sd = lib.systemOf(game);
		std::string rom = lib.gameAt(game).path;
		std::string cmd = replaceAll(replaceAll(sd.run, "%ROM%", shellQuote(rom)), "%SYSTEM%", shellQuote(sd.id));
		logf("starting %s: %s", sd.id.c_str(), cmd.c_str());
		{
			std::ofstream f(o.data + "/last-system");
			f << ui->systemIndex() << "\n";
		}
		// everything the game wants goes back to the system: the panels, the buttons, the sound device. A RetroArch
		// game with the game panel on (settings.conf retroarch_panel) gets only its own panel, through a DRM lease
		// (Display::lease): the menu keeps the other for the game panel (Settings::gameRender) and reads its touch
		sound.release();
		boost.release();
		const char* FIFO = "/run/banana/retroarch.cmd";
		// (PICO-8 too when it plays in RetroArch's fake-08: the official PICO-8, when it is on the card, takes the panels)
		bool ra = sd.run.find("run-retroarch") != std::string::npos ||
			(sd.run.find("run-pico8") != std::string::npos && access("/data/roms/pico-8/pico8_64", F_OK) != 0);
		bool panel = ra && settings["retroarch_panel"] != "off" && disp.isOpen();
		int gameScreen = 0, leaseFd = -1, cmdFd = -1;
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
				::close(leaseFd);	// (the game has its copy)
				gamePanel(pid, status, gameScreen, cmdFd, lib.gameAt(game).title, sd.name);
			}
			else while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
			unlink("/run/banana/game.pid");
		}
		if (panel)
		{
			{
				std::lock_guard<std::mutex> l(worker.uiLock);
				int a, b;
				if (settingsView.gameProgress(a, b)) std::ofstream(PROGRESS) << a << " " << b << "\n";	// RetroArch's, as the panel had it
				settingsView.gameClose();
			}
			::close(cmdFd);
			unlink(FIFO);
			input.close();
			disp.close();
		}
		long secs = (long)((nowMs() - start) / 1000);
		if (system("banana-perf ui") != 0) {}
		logf("game ended after %ld s (status %d)", secs, status);
		for (int tries = 0; !disp.open(o.top, err) && tries < 100; tries++) usleep(50000);
		be.invalidate();
		input.open(o.touch, disp.width(), disp.height());
		std::lock_guard<std::mutex> l(worker.uiLock);
		{
			int achA = -1, achB = 0;
			std::ifstream(PROGRESS) >> achA >> achB;
			lib.recordPlay(game, secs, achA, achB);	// (a start that failed straight away isn't a play)
		}
		ui->dataChanged();
		ui->setLibrary(lib.currentShelf(), game);
		if (disp.isOpen()) irisFromGame(disp, be, *ui);
		dirty = true;
		batteryDue = true;	// a step passed during the game shows now
	};

	if (system("banana-perf ui") != 0) {}
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
		if (settingsView.isOpen())
		{
			std::lock_guard<std::mutex> l(worker.uiLock);
			if (settingsView.update(now)) dirty = true;	// a Wi-Fi scan came back, a network joined
			if (settingsView.takeLibraryChanged()) roms.due = now;	// box art fetched: the library again, at home
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
			fstat.draw(nowMs() - t0);
			drawn = true;
			dirty = false;
		}
		if (drawn && disp.isOpen() && !disp.flipPending())
		{
			const uint32_t* scr[2] = { be.pixels(0), be.pixels(1) };
			Damage dmg[2] = { be.takeDamage(0), be.takeDamage(1) };
			double t0 = nowMs();
			bool flipped = disp.present(scr, dmg);
			fstat.present(t0, nowMs() - t0);
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
					// after a wake: the kernel has the panels showing our buffers again; a redraw brings the clock and
					// the battery up to date (no modeset: that would run the panels' power-on sequence once more)
					if (c == SIGUSR1) { std::lock_guard<std::mutex> l(worker.uiLock); be.invalidate(); ui->dataChanged(); dirty = true; batteryDue = true; }
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
							dirty = true;
						}
					}
				}
			}
			else if (p.fd == roms.fd)
				roms.read(nowMs());
			else if (p.fd == worker.wakeFd)
			{
				uint64_t v;
				if (read(worker.wakeFd, &v, 8) < 0) {}
				dirty = true;
			}
			else if (disp.isOpen() && p.fd == disp.pollFd())
				disp.handleEvents();
			else
				input.read(p.fd, handle);
		}
	}
	{
		std::ofstream f(o.data + "/last-system");
		f << ui->systemIndex() << "\n";
	}
	worker.finish();
	return 0;
}
