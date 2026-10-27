// Banana Theme Studio: the menu's own engine, compiled to WebAssembly, for live previews in the browser.
//
// The same files banana-ui is built from (layout.cpp, expr.cpp, json.cpp, library.cpp, theme.cpp, soft.cpp and the
// rnds rasterizer), driven the way `banana-ui --headless` drives them: a SoftBackend draws both screens into memory
// and the page copies them onto two canvases. Nothing here re-implements the engine, so a preview is the device's
// picture, pixel for pixel (as serve.py's PNGs are). The library is ui/tests/fixture.sh's made-up one, embedded at
// /fx; the built-in fonts and icons are embedded at /rnds and /banana; the theme being edited is written by the page
// to /theme/theme.json with its files beside it (MEMFS).
//
// Built by tools/theme-studio/wasm/build.sh into banana-engine.js (one file: the wasm inlined, so the page works
// from file://). The calls are plain C, used from JS with cwrap (wasm/engine.js).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "../../../ui/third_party/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../../ui/third_party/stb_image_write.h"

#include "../../../ui/rnds/RndsUI.h"
#include "../../../ui/src/layout.h"
#include "../../../ui/src/library.h"
#include "../../../ui/src/soft.h"
#include "../../../ui/src/theme.h"
#include "../../../ui/src/zip.h"

#include <emscripten/emscripten.h>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>

using namespace banana;

namespace
{
	const char* ASSETS = "/rnds";
	const char* BANANA = "/banana";
	// the library: fixture.sh's (/fx, the one serve.py's previews use), or the same with a game for every system in
	// systems.conf (/fxall, for seeing each system's icon; its own systems.conf has the apps in /fxall/apps)
	std::string FX = "/fx", SYSTEMS = "/fx/systems.conf", ROMS = "/fx/roms", DATA = "/fx/data";

	// as main.cpp's: the Source with the host settings on top
	struct Host : Library
	{
		using Library::Library;
		bool motion = true;
		bool topMotion() override { return motion; }
		std::string batteryShown = "icon";
		std::string batteryMode() override { return batteryShown; }
	};

	rnds::Bitmap stbLoad(const std::string& path)
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

	std::unique_ptr<SoftBackend> be;
	std::unique_ptr<Host> lib;
	std::unique_ptr<LayoutMenu> ui;
	std::string themeFile, lastLog, cues;
	int W = 640, H = 480;
	bool drawn = false;

	// the fixture's state as fixture.sh leaves it (a star from the last preview taken away again), its play stats
	// relative to now (fixture.sh writes them with `date`), so "last played" reads as it does on the device: an hour,
	// a day and ten days ago
	void freshData()
	{
		std::ofstream(DATA + "/favorites.txt") << "nds/Peel Quest.nds\n";
		remove((DATA + "/favorite-systems.txt").c_str());
		long now = (long)time(nullptr);
		std::ofstream f(DATA + "/playstats.tsv");
		f << "nds/Banana Kart DS.nds\t14\t79920\t" << now - 3600 << "\n"
		  << "nds/Peel Quest.nds\t3\t5400\t" << now - 90000 << "\n"
		  << "gba/Orchard Tactics.gba\t1\t900\t" << now - 900000 << "\n";
	}

	void makeLibrary()
	{
		freshData();
		lib.reset(new Host(SYSTEMS, ROMS, DATA, ASSETS));
		lib->testPower.percent = 76;
		lib->onCue = [](const std::string& n) { cues += n + "\n"; };
		lib->scan();
	}
}

extern "C"
{
	// the panels' size (640x480 or 1024x768) and the library (0 fixture.sh's, 1 a game for every system), read again.
	// 0 when the size is unusable
	EMSCRIPTEN_KEEPALIVE int bt_init(int w, int h, int every)
	{
		FX = every ? "/fxall" : "/fx";
		SYSTEMS = FX + "/systems.conf"; ROMS = FX + "/roms"; DATA = FX + "/data";
		if (w < 64 || h < 48 || w > 4096 || h > 4096) return 0;
		static bool once = false;
		if (!once)
		{
			once = true;
			rnds::setDecoder(stbLoad);
			rnds::setZipReader(readZip);
			mkdir("/sys", 0755); mkdir("/sys/class", 0755); mkdir("/sys/class/net", 0755); mkdir("/sys/class/net/wlan0", 0755);
			std::ofstream("/sys/class/net/wlan0/operstate") << "up\n";
		}
		ui.reset();
		W = w; H = h;
		be.reset(new SoftBackend(W, H));
		makeLibrary();
		return 1;
	}

	EMSCRIPTEN_KEEPALIVE const char* bt_errors();

	// (re)load the theme at path (its files beside it), keeping the view and the selection when keep is set.
	// Returns the theme's problems, one a line ("" when there are none)
	EMSCRIPTEN_KEEPALIVE const char* bt_load(const char* path, int keep)
	{
		if (!be) bt_init(640, 480, 0);
		themeFile = path;
		int view = ui ? (int)ui->view() : 0, sys = ui ? ui->systemIndex() : 0, game = ui ? ui->gameIndex() : 0;
		ui.reset();
		std::string why;
		// the theme's colours and accent (Settings' palette, the accent hooks), as banana-ui sets them
		applyTheme("file:" + themeFile, "", &why);
		lastLog.clear();
		if (!why.empty()) lastLog += "theme: " + why + "\n";
		if (!isLayoutTheme(themeFile))
		{
			lastLog += "theme: not a layout theme (it needs a \"screens\" member)\n";
			return lastLog.c_str();
		}
		ui.reset(new LayoutMenu(lib.get(), be.get(), themeFile, H / 480.0f, std::string(ASSETS) + "/fonts", std::string(BANANA) + "/fonts"));
		be->invalidate();
		if (sys < 0 || sys >= lib->systemCount()) sys = 0;
		if (keep && view != (int)rnds::UI::HOME)
		{
			lib->openLibrary(sys);
			ui->setLibrary(sys, game < 0 ? 0 : game);
		}
		else ui->setHome(keep ? sys : 0);
		drawn = false;
		return bt_errors();
	}

	// one frame at now (ms): the menu advanced, its pictures finished, drawn. 1 when it drew something new
	EMSCRIPTEN_KEEPALIVE int bt_frame(double now, int force)
	{
		if (!ui) return 0;
		ui->update(now);
		bool want = force || !drawn || ui->animating() || ui->nextWake() == 0;
		if (ui->workOnce()) { ui->finishWork(); want = true; }
		if (!want) return 0;
		ui->update(now);
		ui->render();
		drawn = true;
		Damage a = be->takeDamage(0), b = be->takeDamage(1);
		return a.empty() && b.empty() && !force ? 0 : 1;
	}

	// ms until the menu wants to draw again (-1: not until something happens)
	EMSCRIPTEN_KEEPALIVE int bt_wake() { return ui ? ui->nextWake() : -1; }
	EMSCRIPTEN_KEEPALIVE int bt_animating() { return ui && ui->animating() ? 1 : 0; }

	// a screen's pixels: W x H words 0xXXRRGGBB
	EMSCRIPTEN_KEEPALIVE const uint32_t* bt_pixels(int screen) { return be ? be->pixels(screen ? 1 : 0) : nullptr; }
	EMSCRIPTEN_KEEPALIVE int bt_width() { return W; }
	EMSCRIPTEN_KEEPALIVE int bt_height() { return H; }

	// a button: rnds::UI::Button's order (0 left, 1 right, 2 up, 3 down, 4 a, 5 b, 6 x, 7 y, 8 l, 9 r). 1 when used
	EMSCRIPTEN_KEEPALIVE int bt_press(int b)
	{
		static const rnds::UI::Button order[] = { rnds::UI::LEFT, rnds::UI::RIGHT, rnds::UI::UP, rnds::UI::DOWN, rnds::UI::A,
			rnds::UI::B, rnds::UI::X, rnds::UI::Y, rnds::UI::L, rnds::UI::R };
		if (!ui || b < 0 || b >= (int)(sizeof order / sizeof order[0])) return 0;
		return ui->press(order[b]) ? 1 : 0;
	}

	// a touch on a screen, in panel pixels: down, then up (a tap acts on the release, as on the device)
	EMSCRIPTEN_KEEPALIVE int bt_touch(int screen, float x, float y, int down)
	{
		if (!ui) return 0;
		return ui->touch(screen, x, y, down != 0) ? 1 : 0;
	}

	// 0 home, 1 library, 2 launch; the selection
	EMSCRIPTEN_KEEPALIVE int bt_view() { return ui ? (int)ui->view() : 0; }
	EMSCRIPTEN_KEEPALIVE int bt_system() { return ui ? ui->systemIndex() : 0; }
	EMSCRIPTEN_KEEPALIVE int bt_game() { return ui ? ui->gameIndex() : 0; }
	EMSCRIPTEN_KEEPALIVE int bt_systemCount() { return lib ? lib->systemCount() : 0; }

	EMSCRIPTEN_KEEPALIVE void bt_home(int sys)
	{
		if (!ui) return;
		if (sys < 0 || sys >= lib->systemCount()) sys = 0;
		ui->setHome(sys);
		drawn = false;
	}
	EMSCRIPTEN_KEEPALIVE void bt_library(int sys, int game)
	{
		if (!ui) return;
		if (sys < 0 || sys >= lib->systemCount()) sys = 0;
		lib->openLibrary(sys);
		ui->setLibrary(sys, game < 0 ? 0 : game);
		drawn = false;
	}

	// the battery the hooks read (percent, -1 none), the clock's style and the motion setting, as Settings sets them
	EMSCRIPTEN_KEEPALIVE void bt_host(int percent, int charging, int twelveHour, int motion, int batteryText)
	{
		if (!lib) return;
		lib->testPower.percent = percent;	// (-1: none, as a computer with no battery reads; the hooks say -1)
		lib->testPower.charging = charging != 0;
		lib->twelveHour = twelveHour != 0;
		lib->motion = motion != 0;
		lib->batteryShown = batteryText ? "text" : "icon";
		drawn = false;
	}

	// Wi-Fi up or down (the online hook), as /sys says it on the device
	EMSCRIPTEN_KEEPALIVE void bt_online(int up)
	{
		if (up) std::ofstream("/sys/class/net/wlan0/operstate") << "up\n";
		else remove("/sys/class/net/wlan0/operstate");
		if (lib) lib->onlineAt = 0;
		drawn = false;
	}

	// the theme's problems so far (the load's and the drawing's: a bad expression shows when it is first drawn), one
	// a line, as banana-ui prints them after "theme: "
	EMSCRIPTEN_KEEPALIVE const char* bt_errors()
	{
		static std::string out;
		out = lastLog;
		if (ui) for (const std::string& e : ui->errors()) out += "theme: " + e + "\n";
		return out.c_str();
	}
	// banana-ui --headless's script, as runHeadless runs it (home, lib, wait, press, touch, shot; the time starting at
	// 100000 ms): each shot's screens are written to /out/<name>-top.rgb and -bot.rgb (W x H x 3 bytes). For stills
	// the same as serve.py's, and for tests/engine.test.js, which compares them with the native build's PNGs. Returns
	// the shots' names, one a line, or "error: ..."
	EMSCRIPTEN_KEEPALIVE const char* bt_script(const char* script)
	{
		static std::string out;
		out.clear();
		if (!ui) return (out = "error: no theme").c_str();
		mkdir("/out", 0755);
		double now = 100000;
		auto settle = [&] { ui->update(now); ui->finishWork(); ui->update(now); };
		auto save = [&](int s, const std::string& path)
		{
			std::vector<uint8_t> rgb((size_t)W * H * 3);
			const uint32_t* p = be->pixels(s);
			for (size_t i = 0; i < (size_t)W * H; i++) { rgb[i * 3] = (p[i] >> 16) & 255; rgb[i * 3 + 1] = (p[i] >> 8) & 255; rgb[i * 3 + 2] = p[i] & 255; }
			FILE* f = fopen(path.c_str(), "wb");
			if (!f) return false;
			bool ok = fwrite(rgb.data(), 1, rgb.size(), f) == rgb.size();
			return fclose(f) == 0 && ok;
		};
		static const std::map<std::string, int> keys = { { "left", 0 }, { "right", 1 }, { "up", 2 }, { "down", 3 }, { "a", 4 },
			{ "b", 5 }, { "x", 6 }, { "y", 7 }, { "l", 8 }, { "r", 9 } };
		ui->setHome(0);
		std::stringstream ss(script);
		std::string cmd;
		while (std::getline(ss, cmd, ';'))
		{
			std::stringstream cs(cmd);
			std::string op;
			cs >> op;
			ui->update(now);
			if (op.empty()) continue;
			if (op == "home") { int s = 0; cs >> s; bt_home(s); }
			else if (op == "lib") { int s = 0, g = 0; cs >> s >> g; bt_library(s, g); }
			else if (op == "wait") { double ms = 0; cs >> ms; now += ms; }
			else if (op == "press")
			{
				std::string b; cs >> b;
				auto it = keys.find(b);
				if (it == keys.end()) return (out = "error: the preview has no button " + b).c_str();
				bt_press(it->second);
			}
			else if (op == "touch")
			{
				int scr = 1; float x = 0, y = 0;
				cs >> scr >> x >> y;
				float k = H / 480.0f;
				ui->touch(scr, x * k, y * k, true);
				ui->touch(scr, x * k, y * k, false);
			}
			else if (op == "shot")
			{
				std::string name; cs >> name;
				settle(); ui->render(); settle(); ui->render();
				ui->render();
				if (!save(0, "/out/" + name + "-top.rgb") || !save(1, "/out/" + name + "-bot.rgb")) return (out = "error: can't write " + name).c_str();
				out += name + "\n";
			}
			else return (out = "error: the preview can't run \"" + op + "\"").c_str();
		}
		be->invalidate();
		drawn = false;
		return out.c_str();
	}

	// the sound cues the menu played since the last call, one a line
	EMSCRIPTEN_KEEPALIVE const char* bt_cues()
	{
		static std::string out;
		out = cues;
		cues.clear();
		return out.c_str();
	}
}
