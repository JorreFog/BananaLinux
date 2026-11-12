// banana-ui: both screens as windows of a compositor (SDL2: Wayland under sway, as on ROCKNIX), instead of the panels
// through KMS. Built with SDL=1 (BANANA_SDL); chosen when BANANA_DISPLAY=sdl, or when WAYLAND_DISPLAY is set and
// BANANA_DISPLAY isn't "kms". Each screen is a fullscreen window on its own output: the top one the output named like
// the top connector (BANANA_TOP, "DSI-2"; BANANA_SDL_TOP=<display index> overrides), else the wider one. The
// compositor turns the outputs (sway's "transform"), so the windows are upright and the menu's canvas is fitted to
// each without a turn (panelfit.h). The buttons and touchscreens are still read from evdev (input.cpp): the compositor
// doesn't grab them. A game is another client: the menu closes its windows for it (close()) and opens them after, as
// it gives the panels up and takes them back under KMS; there is no lease, no colour table, no interrupt storm here.
#ifdef BANANA_SDL
#include "kms.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/timerfd.h>
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

	bool Display::openSdl(const std::string& top, std::string& err)
	{
		if (!SDL_WasInit(SDL_INIT_VIDEO) && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) { err = std::string("SDL: ") + SDL_GetError(); return false; }
		int n = SDL_GetNumVideoDisplays();
		if (n < 1) { err = "SDL: no display"; return false; }
		// the top screen's display: asked for, named like the top connector, else the widest
		int ti = -1;
		if (const char* e = getenv("BANANA_SDL_TOP")) ti = atoi(e);
		for (int i = 0; ti < 0 && i < n; i++)
			if (const char* nm = SDL_GetDisplayName(i)) if (strstr(nm, top.c_str())) ti = i;
		if (ti < 0 || ti >= n)
		{
			ti = 0;
			SDL_Rect a, b;
			for (int i = 1; i < n; i++)
				if (SDL_GetDisplayBounds(i, &a) == 0 && SDL_GetDisplayBounds(ti, &b) == 0 && a.w > b.w) ti = i;
		}
		// the bottom: the other one (one display only, a desktop's test: both windows on it)
		int bi = n > 1 ? (ti == 0 ? 1 : 0) : ti;
		int idx[2] = { ti, bi };
		int pw[2], ph[2];
		for (int s = 0; s < 2; s++)
		{
			SDL_Rect r;
			if (SDL_GetDisplayBounds(idx[s], &r) != 0) { r.w = 640; r.h = 480; }
			// (a single display: two windows side by side at half its width, BANANA_SDL_WINDOWED, for a look on a desktop)
			bool windowed = getenv("BANANA_SDL_WINDOWED") != nullptr;
			Uint32 flags = windowed ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP;
			int w = windowed ? r.w / 2 : r.w, h = windowed ? r.h / 2 : r.h;
			SDL_Window* win = SDL_CreateWindow(s == 0 ? "banana-ui top" : "banana-ui bottom",
				SDL_WINDOWPOS_CENTERED_DISPLAY(idx[s]), SDL_WINDOWPOS_CENTERED_DISPLAY(idx[s]), w, h, flags | SDL_WINDOW_BORDERLESS);
			if (!win) { err = std::string("SDL: ") + SDL_GetError(); close(); return false; }
			sdlWin[s] = win;
			SDL_ShowCursor(SDL_DISABLE);
			SDL_Surface* sf = SDL_GetWindowSurface(win);
			if (!sf) { err = std::string("SDL: ") + SDL_GetError(); close(); return false; }
			if (sf->format->BytesPerPixel != 4)
			{ err = "SDL: the window's surface isn't 32-bit"; close(); return false; }
			pw[s] = sf->w; ph[s] = sf->h;
			fprintf(stderr, "banana-ui: display (SDL %s): screen %d on display %d \"%s\", %dx%d\n", SDL_GetCurrentVideoDriver(), s,
				idx[s], SDL_GetDisplayName(idx[s]) ? SDL_GetDisplayName(idx[s]) : "?", pw[s], ph[s]);
		}
		canvasFor(pw[0], ph[0], 0, pw[1], ph[1], 0, W, H);
		for (int s = 0; s < 2; s++) p[s].fit.set(W, H, pw[s], ph[s], 0);
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

	void Display::closeSdl()
	{
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
			d.x1 = std::min(d.x1, W); d.y1 = std::min(d.y1, H);
			if (d.empty() || !sdlWin[s]) continue;
			SDL_Surface* sf = SDL_GetWindowSurface(W_(sdlWin[s]));
			if (!sf) continue;
			if (sf->w != p[s].fit.panelW() || sf->h != p[s].fit.panelH())
			{
				// the compositor gave the window another size (an output's mode changed): fitted again, all of it drawn
				p[s].fit.set(W, H, sf->w, sf->h, 0);
				SDL_FillRect(sf, nullptr, 0);
				d.x0 = d.y0 = 0; d.x1 = W; d.y1 = H;
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
