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
	rnds::UI* ui = nullptr;
	std::mutex uiLock, wlock;
	std::condition_variable cv;
	bool stop = false, work = false;
	int wakeFd = -1;		// eventfd: a job finished, the main loop has something to upload
	std::thread th;

	void start(rnds::UI* u)
	{
		ui = u;
		wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		ui->setNotifiers([this] { kick(); }, [this] { uint64_t one = 1; if (write(wakeFd, &one, 8) < 0) {} });
		th = std::thread([this] { run(); });
	}
	// a new UI (the theme changed): called with uiLock held
	void attach(rnds::UI* u)
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
				std::lock_guard<std::mutex> l(uiLock);
				if (!ui->workOnce()) break;
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
static bool rearrange(Host& lib, rnds::UI& ui)
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
static int runHeadless(const Options& o, Host& lib, const std::string& variant)
{
	SoftBackend be(o.w, o.h);
	rnds::UI ui(&lib, &be, o.assets, o.h / 480.0f, variant);
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
			rnds::UI::CacheStats cs = ui.cacheStats();
			logf("mem: RSS%s; texture cache %zu entries, %.1f MB of a %.1f MB budget", rss.c_str(), cs.entries, cs.bytes / 1048576.0, cs.budget / 1048576.0);
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
			// N frames 16.7 ms apart, as the panels would show them: the engine's and the backend's time per frame
			int n = 60; cs >> n;
			double total = 0, worst = 0;
			be.skipped = 0;
			for (int i = 0; i < n; i++)
			{
				now += 1000.0 / 60;
				double t0 = nowMs();
				ui.update(now);
				ui.finishWork();
				ui.render();
				double dt = nowMs() - t0;
				total += dt; worst = std::max(worst, dt);
			}
			logf("bench: %d frames, %.2f ms average, %.2f ms worst, %d of %d screens unchanged", n, total / n, worst, be.skipped, 2 * n);
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
	int y0[2], y1[2];
	be.takeDamage(0, y0[0], y1[0]);
	be.takeDamage(1, y0[1], y1[1]);
	if (!disp.present(scr, y0, y1)) return;
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

// the splash hands over to the menu through an iris: on each panel a circle round its centre closes over the splash's
// last frame to black, holds, then opens on the menu. Its edge goes in steps of the splash's own pixels (2 at 480
// lines), as pixel art would draw it; timed by the clock, a frame each refresh
static void irisToMenu(Display& disp, SoftBackend& be, rnds::UI& ui)
{
	if (!gSplash.played()) return;
	const int W = be.width(), H = be.height();
	const size_t n = (size_t)W * H;
	std::vector<uint32_t> was[2] = { std::vector<uint32_t>(be.pixels(0), be.pixels(0) + n), std::vector<uint32_t>(be.pixels(1), be.pixels(1) + n) };
	// the menu's first frame drawn now, before the circle closes (its first draw takes ~130 ms: the glyphs and boxes
	// made; with the splash's last frame still and on screen that wait doesn't show), the splash's put back; its
	// frames are then drawn live as the circle opens
	ui.update(nowMs());
	ui.finishWork();
	be.invalidate();
	ui.render();
	for (int sc = 0; sc < 2; sc++) std::copy(was[sc].begin(), was[sc].end(), be.canvas(sc));
	std::vector<uint32_t> menu[2] = { std::vector<uint32_t>(n), std::vector<uint32_t>(n) };
	const int block = std::max(1, (int)std::lround(2 * H / 480.0f));
	const float full = std::sqrt((float)(W * W + H * H)) / 2 + block;	// the corners
	const double CLOSE = 360, HOLD = 70, OPEN = 450;
	auto frame = [&](const std::vector<uint32_t>* src, float r)
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
	};
	const double t0 = nowMs();
	for (;;)	// closing over the splash: slow at first, faster into the point (no lingering at a small circle)
	{
		double p = (nowMs() - t0) / CLOSE;
		if (p >= 1) break;
		frame(was, full * (float)(1 - p * p));
	}
	frame(was, 0);
	const double t1 = nowMs();
	// while it is black: the menu as it will be once its own intro is over (300 ms on, before the opening ends, so
	// the menu's clock never goes back), opened on as a still picture: a live menu frame (16-20 ms) each refresh
	// made the opening miss refreshes
	ui.update(t1 + 300);
	be.invalidate();
	ui.render();
	for (int sc = 0; sc < 2; sc++) std::copy(be.canvas(sc), be.canvas(sc) + n, menu[sc].begin());
	for (int sc = 0; sc < 2; sc++) std::fill(be.canvas(sc), be.canvas(sc) + n, 0xff000000u);	// black again
	be.invalidate();
	while (nowMs() - t1 < HOLD) showFrame(disp, be, -1);
	const double t2 = nowMs();
	for (;;)	// opening on the menu: fast at first, settling
	{
		double p = (nowMs() - t2) / OPEN;
		if (p >= 1) break;
		float e = (float)p; e = 1 - (1 - e) * (1 - e) * (1 - e);
		frame(menu, full * e);
	}
	for (int sc = 0; sc < 2; sc++) std::copy(menu[sc].begin(), menu[sc].end(), be.canvas(sc));
	be.invalidate();	// the menu's next frame is drawn in full
}

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
	Host lib(o.systems, o.roms, o.data, o.assets);
	lib.motion = settings["motion"] != "off";
	lib.twelveHour = settings["clock"] == "12h";
	lib.batteryShown = settings["battery"] == "percent" ? "text" : "icon";

	if (!o.headless.empty())
	{
		lib.scan();
		return runHeadless(o, lib, variant);
	}

	if (pipe2(sigPipe, O_CLOEXEC | O_NONBLOCK)) return 1;
	struct sigaction sa = {};
	sa.sa_handler = onSignal;
	sigaction(SIGTERM, &sa, nullptr);
	sigaction(SIGINT, &sa, nullptr);
	sigaction(SIGUSR1, &sa, nullptr);	// bananad after a resume: draw everything again
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
	sound.enabled = settings["sounds"] != "off";
	lib.onCue = [&sound](const std::string& n) { sound.play(n); };

	// the library and the menu are made on another thread while the boot animation plays
	std::unique_ptr<rnds::UI> uiPtr;
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
		uiPtr.reset(new rnds::UI(&lib, &be, o.assets, disp.height() / 480.0f, variant));
		uiPtr->setHome(startSys);
		// the menu's pictures decoded here too, while the animation plays (on the main thread at the hand-over they
		// stopped the screens for ~200 ms)
		uiPtr->update(nowMs());
		uiPtr->finishWork();
		ready = true;
	});
	playSplash(disp, be, o, variant, settings["splash"] != "off", ready, sound, sigPipe[0]);
	loader.join();
	rnds::UI* ui = uiPtr.get();
	ui->setLog([](const std::string& m) { logf("%s", m.c_str()); });
	Worker worker;
	worker.start(ui);
	irisToMenu(disp, be, *ui);
	bootMark("banana-ui: the menu");
	// the first boot after the installation: the setup (Wi-Fi, the time zone, the accounts) before anything else
	bool firstSetup = access((o.data + "/setup-done").c_str(), F_OK) != 0;

	Input input;
	input.open(o.touch, disp.width(), disp.height());

	Repeat rep;
	bool dirty = true, quit = false, drawn = false;	// drawn: a frame on the canvas waiting for the flips
	int lastMinute = -1;
	float scale = disp.height() / 480.0f;
	Settings settingsView(&be, o.assets, o.data, scale, variant);
	LowBattery low;
	bool batteryDue = true;
	if (firstSetup) { settingsView.setup(); logf("first-time setup"); }
	RomWatch roms;
	roms.watch(lib.watchDirs());
	be.overlay = [&](int screen) { if (screen == 0 && low.showing(nowMs())) settingsView.notice(low.title, low.body, low.percent); };

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
			uiPtr.reset(new rnds::UI(&lib, &be, o.assets, scale, variant));
			ui = uiPtr.get();
			ui->setLog([](const std::string& m) { logf("%s", m.c_str()); });
			worker.attach(ui);
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
			settingsView.setVariant(variant);
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
		// everything the game wants goes back to the system: the panels, the buttons, the sound device
		sound.release();
		input.close();
		disp.close();
		drawn = false;	// the menu is drawn afresh when the panels come back
		rep.key = Key::NONE;
		if (system(("banana-perf " + shellQuote(sd.perf.empty() ? "balanced" : sd.perf)).c_str()) != 0) {}
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
			execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
			_exit(127);
		}
		int status = 0;
		if (pid > 0)
		{
			std::ofstream("/run/banana/game.pid") << pid << "\n";	// bananad: the game to pause before a sleep
			while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
			unlink("/run/banana/game.pid");
		}
		long secs = (long)((nowMs() - start) / 1000);
		if (system("banana-perf ui") != 0) {}
		logf("game ended after %ld s (status %d)", secs, status);
		for (int tries = 0; !disp.open(o.top, err) && tries < 100; tries++) usleep(50000);
		be.invalidate();
		input.open(o.touch, disp.width(), disp.height());
		std::lock_guard<std::mutex> l(worker.uiLock);
		if (secs >= 5) lib.recordPlay(game, secs);	// a start that failed straight away isn't a play
		ui->dataChanged();
		ui->setLibrary(lib.currentShelf(), game);
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
			want = dirty || (!settingsView.isOpen() && (ui->animating() || ui->nextWake() == 0));
		}
		// the next frame is drawn while the last one's flips are still pending (present copies it out, so the canvas is
		// free again at once), and handed over when both panels have flipped: drawing (~10 ms on the RG DS) and the wait
		// for the two panels' unaligned vblanks overlap instead of adding up to two refreshes a frame
		if (want && disp.isOpen() && !drawn)
		{
			{
				std::lock_guard<std::mutex> l(worker.uiLock);
				if (settingsView.isOpen()) settingsView.render();
				else ui->render();
			}
			drawn = true;
			dirty = false;
		}
		if (drawn && disp.isOpen() && !disp.flipPending())
		{
			const uint32_t* scr[2] = { be.pixels(0), be.pixels(1) };
			int y0[2], y1[2];
			be.takeDamage(0, y0[0], y1[0]);
			be.takeDamage(1, y0[1], y1[1]);
			disp.present(scr, y0, y1);
			drawn = false;
			continue;	// see whether the next frame is due before sleeping
		}

		int timeout;
		{
			std::lock_guard<std::mutex> l(worker.uiLock);
			timeout = want || drawn ? -1 : (settingsView.isOpen() ? settingsView.nextWake(now) : ui->nextWake());	// a frame is wanted: the flip event wakes us
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
