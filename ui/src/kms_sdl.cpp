// banana-ui: both screens as windows of a compositor (SDL2: Wayland under sway, as on ROCKNIX), instead of the panels
// through KMS. Built with SDL=1 (BANANA_SDL); chosen when BANANA_DISPLAY=sdl, or when WAYLAND_DISPLAY is set and
// BANANA_DISPLAY isn't "kms". Each screen is a fullscreen window on its own output: the top one the output named like
// the top connector (BANANA_TOP, "DSI-2"; BANANA_SDL_TOP=<display index> overrides), else the wider one. The
// compositor turns the outputs (sway's "transform"), so the windows are upright and the menu's canvas is fitted to
// each without a turn (panelfit.h). The buttons and touchscreens are still read from evdev (input.cpp): the compositor
// doesn't grab them. A game is another client: the menu closes its windows for it (close()) and opens them after, as
// it gives the panels up and takes them back under KMS; there is no lease, no colour table, no interrupt storm here.
// With the Thor button (hw.conf HW_HOME), a game that takes both outputs gets them with both windows closed
// (leaseBoth), and the menu comes back over it by opening its windows again (borrow): sway is asked through its IPC
// socket to make them fullscreen and focused, and afterwards (giveBack) to make the game's own windows fullscreen and
// focused again, so the game finds its window as it left it. The quick menu over such a game is the bottom window alone
// (sheetOpen), over the game's bottom window, while the game goes on on the top output.
#ifdef BANANA_SDL
#include "kms.h"
#include "json.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <glob.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/un.h>
#include <unistd.h>

namespace banana
{
	bool Display::wantSdl()
	{
		const char* d = getenv("BANANA_DISPLAY");
		if (d && *d) return !strcmp(d, "sdl");
		const char* w = getenv("WAYLAND_DISPLAY");
		return w && *w;
	}

	static SDL_Window* W_(void* p) { return (SDL_Window*)p; }

	// sway's IPC (i3's protocol: "i3-ipc", the payload's length, the message type, the payload): SWAYSOCK, else the
	// socket sway made in XDG_RUNTIME_DIR. The reply's payload, or "" when there is no sway to ask
	static std::string swayAsk(uint32_t type, const std::string& msg)
	{
		std::string path;
		if (const char* e = getenv("SWAYSOCK")) path = e;
		if (path.empty())
		{
			const char* rt = getenv("XDG_RUNTIME_DIR");
			glob_t g = {};
			if (rt && glob((std::string(rt) + "/sway-ipc.*.sock").c_str(), 0, nullptr, &g) == 0 && g.gl_pathc) path = g.gl_pathv[0];
			globfree(&g);
		}
		if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) return "";
		const int f = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
		if (f < 0) return "";
		sockaddr_un a = {};
		a.sun_family = AF_UNIX;
		memcpy(a.sun_path, path.c_str(), path.size() + 1);
		timeval tv = { 1, 0 };	// (a sway that doesn't answer: the menu goes on without it)
		setsockopt(f, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
		setsockopt(f, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
		std::string out;
		if (connect(f, (sockaddr*)&a, sizeof a) == 0)
		{
			std::string m("i3-ipc");
			const uint32_t hdr[2] = { (uint32_t)msg.size(), type };
			m.append((const char*)hdr, sizeof hdr);
			m += msg;
			char h[14];
			size_t got = 0;
			if (send(f, m.data(), m.size(), MSG_NOSIGNAL) == (ssize_t)m.size())
				for (ssize_t n; got < sizeof h && (n = recv(f, h + got, sizeof h - got, 0)) > 0;) got += (size_t)n;
			if (got == sizeof h && !memcmp(h, "i3-ipc", 6))
			{
				uint32_t len;
				memcpy(&len, h + 6, 4);
				out.resize(len);
				size_t at = 0;
				for (ssize_t n; at < len && (n = recv(f, &out[at], len - at, 0)) > 0;) at += (size_t)n;
				out.resize(at);
			}
		}
		::close(f);
		return out;
	}
	static const uint32_t SWAY_COMMAND = 0, SWAY_OUTPUTS = 3, SWAY_TREE = 4;
	static void swayRun(const std::string& cmd)
	{
		const std::string r = swayAsk(SWAY_COMMAND, cmd);
		if (r.find("\"success\":false") != std::string::npos) fprintf(stderr, "banana-ui: sway: %s: %s\n", cmd.c_str(), r.c_str());
	}

	// the views of sway's tree: each one's container id, output, whether it is fullscreen and focused, its pid
	struct SwayView { long id; std::string output; int ox, oy; bool fs, focused; long pid; };
	static void swayViews(const JVal* n, std::string out, int ox, int oy, std::vector<SwayView>& v)
	{
		if (!n || !n->isObj()) return;
		if (n->str("type") == "output")
		{
			out = n->str("name");
			if (const JVal* r = n->get("rect")) { ox = (int)r->num("x"); oy = (int)r->num("y"); }
		}
		if (n->num("pid", 0) > 0)
			v.push_back({ (long)n->num("id"), out, ox, oy, n->num("fullscreen_mode") == 1, n->flag("focused"), (long)n->num("pid") });
		for (const char* k : { "nodes", "floating_nodes" })
			if (const JVal* a = n->get(k))
				if (a->isArr()) for (const JPtr& c : a->arr) swayViews(c.get(), out, ox, oy, v);
	}
	static std::vector<SwayView> swayTree()
	{
		std::vector<SwayView> v;
		const std::string t = swayAsk(SWAY_TREE, "");
		if (JPtr j = t.empty() ? nullptr : parseJson(t)) swayViews(j.get(), "", 0, 0, v);
		return v;
	}

	bool Display::openSdl(const std::string& top, std::string& err)
	{
		if (!SDL_WasInit(SDL_INIT_VIDEO) && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) { err = std::string("SDL: ") + SDL_GetError(); return false; }
		int n = SDL_GetNumVideoDisplays();
		if (n < 1) { err = "SDL: no display"; return false; }
		// the top screen's display: asked for, named like the top connector, else the widest
		// (each display's output by sway's name for it, found by its place: SDL's name is the output's description, which
		// on a panel holds the name in brackets, "... (DSI-2)", and on a headless output doesn't)
		std::vector<std::string> swayName(n);
		if (JPtr outs = parseJson(swayAsk(SWAY_OUTPUTS, "")))
			if (outs->isArr())
				for (int i = 0; i < n; i++)
				{
					SDL_Rect b;
					if (SDL_GetDisplayBounds(i, &b) != 0) continue;
					for (const JPtr& o : outs->arr)
						if (const JVal* r = o->get("rect"))
							if ((int)r->num("x") == b.x && (int)r->num("y") == b.y) swayName[i] = o->str("name");
				}
		int ti = -1;
		if (const char* e = getenv("BANANA_SDL_TOP")) ti = atoi(e);
		for (int i = 0; ti < 0 && i < n; i++)
			if (const char* nm = SDL_GetDisplayName(i)) if (strstr(nm, top.c_str())) ti = i;
		for (int i = 0; ti < 0 && i < n && !top.empty(); i++)
			if (swayName[i] == top) ti = i;
		if (ti < 0 || ti >= n)
		{
			ti = 0;
			SDL_Rect a, b;
			for (int i = 1; i < n; i++)
				if (SDL_GetDisplayBounds(i, &a) == 0 && SDL_GetDisplayBounds(ti, &b) == 0 && a.w > b.w) ti = i;
		}
		// the bottom: the other one (one display only, a desktop's test: both windows on it)
		int bi = n > 1 ? (ti == 0 ? 1 : 0) : ti;
		// (more than two outputs, an external display plugged in: the bottom one by its name, BANANA_BOTTOM_OUTPUT)
		if (const char* bo = getenv("BANANA_BOTTOM_OUTPUT"))
			for (int i = 0; i < n && *bo; i++)
				if (i != ti && swayName[i] == bo) bi = i;
		sdlIdx[0] = ti; sdlIdx[1] = bi;
		int pw[2], ph[2];
		for (int s = 0; s < 2; s++)
		{
			const char* nm = SDL_GetDisplayName(sdlIdx[s]);
			sdlOut[s] = nm ? nm : "";
			SDL_Rect r;
			if (SDL_GetDisplayBounds(sdlIdx[s], &r) == 0) { sdlRect[s][0] = r.x; sdlRect[s][1] = r.y; }
			if (!sdlWindow(s, err)) { close(); return false; }
			SDL_Surface* sf = SDL_GetWindowSurface(W_(sdlWin[s]));
			pw[s] = sf->w; ph[s] = sf->h;
			fprintf(stderr, "banana-ui: display (SDL %s): screen %d on display %d \"%s\", %dx%d\n", SDL_GetCurrentVideoDriver(), s,
				sdlIdx[s], nm ? nm : "?", pw[s], ph[s]);
		}
		for (int s = 0; s < 2; s++)
			if (!swayName[sdlIdx[s]].empty())
			{
				sdlOut[s] = swayName[sdlIdx[s]];
				fprintf(stderr, "banana-ui: display (SDL): screen %d is sway's output %s\n", s, sdlOut[s].c_str());
			}
		canvasFor(pw[0], ph[0], 0, pw[1], ph[1], 0, W, H);
		screensFor(pw, ph);
		for (int s = 0; s < 2; s++) p[s].fit.set(screenWidth(s), screenHeight(s), pw[s], ph[s], 0);
		// the loop's wake-up: SDL's events (the compositor's pings, a resize) are pumped every 50 ms
		sdlTimer = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
		itimerspec its = {};
		its.it_interval.tv_nsec = its.it_value.tv_nsec = 50 * 1000000L;
		if (sdlTimer >= 0) timerfd_settime(sdlTimer, 0, &its, nullptr);
		fd = sdlTimer >= 0 ? sdlTimer : 0;	// (isOpen() is fd >= 0)
		sdl = true;
		masterAtOpen = true;
		blank();
		return true;
	}

	// screen s's window, fullscreen on its display (sdlIdx), with a 32-bit surface
	bool Display::sdlWindow(int s, std::string& err)
	{
		SDL_Rect r;
		if (SDL_GetDisplayBounds(sdlIdx[s], &r) != 0) { r.w = 640; r.h = 480; }
		// (a single display: two windows side by side at half its width, BANANA_SDL_WINDOWED, for a look on a desktop)
		bool windowed = getenv("BANANA_SDL_WINDOWED") != nullptr;
		Uint32 flags = windowed ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP;
		int w = windowed ? r.w / 2 : r.w, h = windowed ? r.h / 2 : r.h;
		SDL_Window* win = SDL_CreateWindow(s == 0 ? "banana-ui top" : "banana-ui bottom",
			SDL_WINDOWPOS_CENTERED_DISPLAY(sdlIdx[s]), SDL_WINDOWPOS_CENTERED_DISPLAY(sdlIdx[s]), w, h, flags | SDL_WINDOW_BORDERLESS);
		if (!win) { err = std::string("SDL: ") + SDL_GetError(); return false; }
		sdlWin[s] = win;
		SDL_ShowCursor(SDL_DISABLE);
		SDL_Surface* sf = SDL_GetWindowSurface(win);
		if (!sf) { err = std::string("SDL: ") + SDL_GetError(); return false; }
		if (sf->format->BytesPerPixel != 4) { err = "SDL: the window's surface isn't 32-bit"; return false; }
		return true;
	}

	// what is on screen s's output before the menu's window goes over it: the game's view there (the fullscreen one,
	// else any), and the view that has the focus, for sdlRestore
	void Display::sdlRemember(int s)
	{
		theirCon[s] = 0; theirFs[s] = false;
		const long me = (long)getpid();
		for (const SwayView& v : swayTree())
		{
			if (v.pid == me) continue;
			if (v.focused) theirFocus = v.id;
			// (the output by its name, else by its place: SDL's name for a display is the output's on sway)
			const bool here = (!sdlOut[s].empty() && v.output == sdlOut[s]) || (v.ox == sdlRect[s][0] && v.oy == sdlRect[s][1]);
			if (here && (!theirCon[s] || (v.fs && !theirFs[s]))) { theirCon[s] = v.id; theirFs[s] = v.fs; }
		}
	}

	// the menu's window on screen s over whatever is on its output: fullscreen there (sway lets one view be fullscreen on
	// a workspace: the game's is no longer, sdlRestore makes it so again) and focused
	void Display::sdlRaise(int s)
	{
		char c[160];
		snprintf(c, sizeof c, "[pid=%d title=\"^banana-ui %s$\"] fullscreen enable, focus", (int)getpid(), s == 0 ? "top" : "bottom");
		swayRun(c);
	}

	// the game's views as they were (sdlRemember) on the screens in mask: fullscreen again, and the focus back
	void Display::sdlRestore(int mask)
	{
		for (int s = 0; s < 2; s++)
			if ((mask & (1 << s)) && theirCon[s] && theirFs[s])
				swayRun("[con_id=" + std::to_string(theirCon[s]) + "] fullscreen enable");
		if (theirFocus) swayRun("[con_id=" + std::to_string(theirFocus) + "] focus");
		for (int s = 0; s < 2; s++) if (mask & (1 << s)) { theirCon[s] = 0; theirFs[s] = false; }
		theirFocus = 0;
	}

	// a game on both outputs with the Thor button (leaseBoth): both windows closed, as for a game without it, but the
	// menu keeps SDL, its pump and the buttons, to come back over the game (borrow) or open its sheet (sheetOpen)
	int Display::leaseBothSdl()
	{
		if (leased >= 0 || !sdlWin[0] || !sdlWin[1]) return -1;
		for (auto& w : sdlWin) { SDL_DestroyWindow(W_(w)); w = nullptr; }
		leased = 2;
		return ::open("/dev/null", O_RDONLY);
	}

	// the menu over the game (stopped first by the caller): the windows in mask that the game has, opened again and
	// raised over the game's. Nothing of the game's is touched but its fullscreen flag, given back by giveBack
	bool Display::borrowSdl(int mask)
	{
		if (leased < 0) return false;
		bool any = false;
		for (int s = 0; s < 2; s++)
		{
			if (!(mask & (1 << s)) || !theirs(s) || sdlWin[s]) continue;
			sdlRemember(s);
			std::string err;
			if (!sdlWindow(s, err))
			{
				fprintf(stderr, "banana-ui: the menu over the game: no window on screen %d (%s)\n", s, err.c_str());
				if (sdlWin[s]) SDL_DestroyWindow(W_(sdlWin[s]));
				sdlWin[s] = nullptr;
				continue;
			}
			sdlRaise(s);
			borrowMask |= 1 << s;
			any = true;
		}
		if (any) exposed = true;
		return any;
	}

	void Display::giveBackSdl()
	{
		if (!borrowMask) return;
		for (int s = 0; s < 2; s++)
			if ((borrowMask & (1 << s)) && sdlWin[s]) { SDL_DestroyWindow(W_(sdlWin[s])); sdlWin[s] = nullptr; }
		SDL_PumpEvents();
		sdlRestore(borrowMask);
		borrowMask = 0;
	}

	// the quick menu's sheet over a game that has both outputs: the bottom window alone, over the game's bottom one
	// (the game isn't stopped: it goes on on the top output). The caller composes the slide (sheetFrame) and presents it
	bool Display::sheetOpenSdl()
	{
		if (leased != 2 || (borrowMask & 2)) return false;
		if (sdlSheet) return true;
		sdlRemember(1);
		std::string err;
		if (!sdlWindow(1, err))
		{
			fprintf(stderr, "banana-ui: the bottom screen's sheet: no window (%s)\n", err.c_str());
			if (sdlWin[1]) SDL_DestroyWindow(W_(sdlWin[1]));
			sdlWin[1] = nullptr;
			return false;
		}
		sdlRaise(1);
		sdlSheet = true;
		return true;
	}

	void Display::sheetCloseSdl()
	{
		if (!sdlSheet) return;
		sdlSheet = false;
		if (sdlWin[1]) { SDL_DestroyWindow(W_(sdlWin[1])); sdlWin[1] = nullptr; }
		SDL_PumpEvents();
		sdlRestore(2);
	}

	// what the game shows on the bottom output, for the sheet to slide over: a screenshot of it (grim, through sway's
	// screencopy), taken before the sheet's window goes up, at the screen's own size only (else nothing: black under)
	bool Display::sheetUnder(std::vector<uint32_t>& under)
	{
		if (!sdl || sdlOut[1].empty() || sdlOut[1].find('\'') != std::string::npos) return false;
		const int w = screenWidth(1), h = screenHeight(1);
		FILE* f = popen(("grim -t ppm -o '" + sdlOut[1] + "' - 2>/dev/null").c_str(), "r");
		if (!f) return false;
		int pw = 0, ph = 0, mx = 0;
		bool ok = fscanf(f, "P6 %d %d %d", &pw, &ph, &mx) == 3 && fgetc(f) != EOF && pw == w && ph == h && mx == 255;
		std::vector<uint8_t> rgb;
		if (ok)
		{
			rgb.resize((size_t)w * h * 3);
			ok = fread(rgb.data(), 1, rgb.size(), f) == rgb.size();
		}
		pclose(f);
		if (!ok) return false;
		under.resize((size_t)w * h);
		for (size_t i = 0; i < under.size(); i++)
			under[i] = 0xff000000u | (uint32_t)rgb[i * 3] << 16 | (uint32_t)rgb[i * 3 + 1] << 8 | rgb[i * 3 + 2];
		return true;
	}

	// a game on screen s while the menu keeps the other for the game panel: that screen's window closed, so the
	// compositor gives its output to the game's window; an fd that stands for the lease (the caller closes it, the game
	// never uses it: /dev/null), -1 when it can't
	int Display::leaseSdl(int s)
	{
		if (s < 0 || s > 1 || !sdlWin[s] || !sdlWin[1 - s]) return -1;
		SDL_DestroyWindow(W_(sdlWin[s]));
		sdlWin[s] = nullptr;
		leased = s;
		return ::open("/dev/null", O_RDONLY);
	}

	void Display::closeSdl()
	{
		leased = -1;
		borrowMask = 0;
		sdlSheet = false;
		theirCon[0] = theirCon[1] = 0; theirFs[0] = theirFs[1] = false; theirFocus = 0;
		for (auto& w : sdlWin) { if (w) SDL_DestroyWindow(W_(w)); w = nullptr; }
		if (sdlTimer >= 0) ::close(sdlTimer);
		sdlTimer = -1;
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		sdl = false;
		fd = -1;
		pending = 0;
	}

	bool Display::presentSdl(const uint32_t* const screens[2], const Damage dmg[2])
	{
		bool any = false;
		for (int s = 0; s < 2; s++)
		{
			Damage d = dmg[s];
			d.x0 = std::max(d.x0, 0); d.y0 = std::max(d.y0, 0);
			d.x1 = std::min(d.x1, screenWidth(s)); d.y1 = std::min(d.y1, screenHeight(s));
			if (d.empty() || !sdlWin[s]) continue;
			SDL_Surface* sf = SDL_GetWindowSurface(W_(sdlWin[s]));
			if (!sf) continue;
			if (sf->w != p[s].fit.panelW() || sf->h != p[s].fit.panelH())
			{
				// the compositor gave the window another size (an output's mode changed): fitted again, all of it drawn
				p[s].fit.set(screenWidth(s), screenHeight(s), sf->w, sf->h, 0);
				SDL_FillRect(sf, nullptr, 0);
				d.x0 = d.y0 = 0; d.x1 = screenWidth(s); d.y1 = screenHeight(s);
			}
			if (SDL_MUSTLOCK(sf)) SDL_LockSurface(sf);
			p[s].fit.blit(screens[s], (uint8_t*)sf->pixels, (uint32_t)sf->pitch, d);
			if (SDL_MUSTLOCK(sf)) SDL_UnlockSurface(sf);
			Damage pd = p[s].fit.toPanel(d);
			SDL_Rect r = { pd.x0, pd.y0, pd.x1 - pd.x0, pd.y1 - pd.y0 };
			SDL_UpdateWindowSurfaceRects(W_(sdlWin[s]), &r, 1);
			any = true;
		}
		return any;
	}

	void Display::handleSdl()
	{
		uint64_t ticks;
		if (sdlTimer >= 0 && read(sdlTimer, &ticks, sizeof ticks) < 0) {}
		SDL_Event ev;
		bool redraw = false;
		while (SDL_PollEvent(&ev))	// (input is evdev's: only the windows' own events matter here)
			if (ev.type == SDL_WINDOWEVENT && (ev.window.event == SDL_WINDOWEVENT_EXPOSED || ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
				redraw = true;
		if (redraw) exposed = true;
	}

	void Display::blankSdl()
	{
		for (int s = 0; s < 2; s++)
			if (sdlWin[s])
				if (SDL_Surface* sf = SDL_GetWindowSurface(W_(sdlWin[s]))) { SDL_FillRect(sf, nullptr, 0); SDL_UpdateWindowSurface(W_(sdlWin[s])); }
	}
}
#endif
