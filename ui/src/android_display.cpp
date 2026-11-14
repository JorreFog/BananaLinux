// banana-ui on Android: the Display (kms.h) on the launcher's surfaces instead of the panels through KMS. Built in
// place of kms.cpp and kms_sdl.cpp (android/launcher/CMakeLists.txt). The core still renders both screens in software
// (soft.h); present() copies each screen's damaged part into its ANativeWindow (ANativeWindow_lock, the panel fit of
// panelfit.h, unlockAndPost), at each display's real size:
//   slot 0  the top screen: HomeActivity's surface on display 0
//   slot 1  the bottom screen: BottomActivity's surface on display 1
//   slot 2  the quick sheet: an overlay window on display 1, over a game that goes on running (sheetOpen..sheetClose)
// The surfaces come and go with the activities (a game in front stops them): a screen without one is skipped, and a
// surface that comes back has the whole screen drawn again (the compositor() path of main.cpp: takeExposed).
// Pacing: unlockAndPost queues the buffer, and the next lock waits for a free one (the BufferQueue's back-pressure),
// so frames go at the display's rate with no flip events: flipPending() is always false here.
#ifdef __ANDROID__
#include "kms.h"
#include "android_host.h"
#include <android/log.h>
#include <android/native_window.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sys/eventfd.h>
#include <unistd.h>

namespace banana
{
	namespace
	{
		struct Win
		{
			std::mutex m;			// held while the core draws on it, and while the UI thread replaces it
			ANativeWindow* w = nullptr;
			int width = 0, height = 0;
			bool fresh = true;		// came (or changed size) since the last frame drawn on it: all of it drawn again
		};
		Win wins[3];
		std::mutex waitM;
		std::condition_variable waitCv;
		int changedFd = -1;			// the Display's pollFd(): a surface came, went or changed
		int bottomW = 0, bottomH = 0;	// display 1's size from the system (no activity on it yet)

		int changed()
		{
			static std::once_flag once;
			std::call_once(once, [] { changedFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); });
			return changedFd;
		}
		void signal()
		{
			uint64_t one = 1;
			if (changed() >= 0 && write(changed(), &one, 8) < 0) {}
			waitCv.notify_all();
		}
		bool hasWin(int slot)
		{
			std::lock_guard<std::mutex> l(wins[slot].m);
			return wins[slot].w != nullptr;
		}
		// waits up to ms for slot's surface
		bool waitWin(int slot, int ms)
		{
			auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
			std::unique_lock<std::mutex> l(waitM);
			while (!hasWin(slot))
				if (waitCv.wait_until(l, until) == std::cv_status::timeout) return hasWin(slot);
			return true;
		}
		// XRGB8888 (the core's) to the RGBX/RGBA_8888 Android lays out in memory: red and blue swapped, opaque
		inline uint32_t toAndroid(uint32_t p) { return 0xff000000u | (p & 0x0000ff00u) | ((p >> 16) & 0xffu) | ((p & 0xffu) << 16); }
		void swizzle(uint8_t* bits, int stridePx, const Damage& r)
		{
			for (int y = r.y0; y < r.y1; y++)
			{
				uint32_t* row = (uint32_t*)bits + (size_t)y * stridePx;
				for (int x = r.x0; x < r.x1; x++) row[x] = toAndroid(row[x]);
			}
		}
	}

	namespace android
	{
		void setWindow(int slot, ANativeWindow* w)
		{
			if (slot < 0 || slot > 2) return;
			{
				std::lock_guard<std::mutex> l(wins[slot].m);
				if (w) ANativeWindow_acquire(w);
				if (wins[slot].w) ANativeWindow_release(wins[slot].w);
				wins[slot].w = w;
				wins[slot].width = w ? ANativeWindow_getWidth(w) : 0;
				wins[slot].height = w ? ANativeWindow_getHeight(w) : 0;
				wins[slot].fresh = true;
				// (the sheet is see-through above its top edge: the game shows there)
				if (w) ANativeWindow_setBuffersGeometry(w, 0, 0, slot == 2 ? WINDOW_FORMAT_RGBA_8888 : WINDOW_FORMAT_RGBX_8888);
			}
			__android_log_print(ANDROID_LOG_INFO, "banana-ui", "surface %d: %s %dx%d", slot, w ? "here" : "gone", wins[slot].width, wins[slot].height);
			signal();
		}

		void setBottomSize(int w, int h) { bottomW = w; bottomH = h; }
	}

	bool Display::wantSdl() { return false; }

	void Display::screensFor(const int uw[2], const int uh[2])
	{
		const char* st = getenv("BANANA_STAGE");
		bool fit = st && !strcmp(st, "fit");
		for (int s = 0; s < 2; s++)
		{
			bool same = uw[s] == W && uh[s] == H;
			CW[s] = fit || same ? W : uw[s];
			CH[s] = fit || same ? H : uh[s];
		}
	}

	bool Display::open(const std::string&, std::string& err)
	{
		if (isOpen()) return true;
		// the top screen's surface: HomeActivity is starting as the core does (the first open waits for it)
		if (!waitWin(0, 10000)) { err = "no surface for the top screen yet"; return false; }
		// the bottom one: BottomActivity on display 1, a moment later; without it (one display, or it isn't up) the
		// bottom screen is drawn nowhere until its surface comes, at display 1's size
		if (bottomW > 0) waitWin(1, 3000);
		int pw[2], ph[2];
		for (int s = 0; s < 2; s++)
		{
			std::lock_guard<std::mutex> l(wins[s].m);
			pw[s] = wins[s].w ? wins[s].width : s == 1 && bottomW > 0 ? bottomW : 1240;
			ph[s] = wins[s].w ? wins[s].height : s == 1 && bottomH > 0 ? bottomH : 1080;
		}
		canvasFor(pw[0], ph[0], 0, pw[1], ph[1], 0, W, H);
		screensFor(pw, ph);
		for (int s = 0; s < 2; s++)
		{
			p[s] = Panel();
			p[s].fit.set(screenWidth(s), screenHeight(s), pw[s], ph[s], 0);
			fprintf(stderr, "banana-ui: display (Android): screen %d %dx%d, its canvas %dx%d%s\n", s, pw[s], ph[s], screenWidth(s), screenHeight(s),
				hasWin(s) ? "" : " (no surface yet)");
		}
		fd = changed();
		if (fd < 0) { err = "no eventfd"; return false; }
		sdl = true;			// (compositor(): the surfaces come and go, and are drawn whole when they come back)
		masterAtOpen = true;
		exposed = true;
		return true;
	}

	void Display::close()
	{
		if (sheetOn) sheetClose();
		fd = -1;
		pending = 0;
		leased = -1;
		borrowMask = 0;
	}

	void Display::handleEvents()
	{
		uint64_t v;
		if (fd >= 0 && read(fd, &v, 8) == 8) exposed = true;
	}

	void Display::restore() { exposed = true; }

	void Display::blank()
	{
		for (int s = 0; s < 2; s++)
		{
			Win& w = wins[s];
			std::lock_guard<std::mutex> l(w.m);
			if (!w.w) continue;
			ANativeWindow_Buffer b;
			if (ANativeWindow_lock(w.w, &b, nullptr) != 0) continue;
			for (int y = 0; y < b.height; y++) memset((uint32_t*)b.bits + (size_t)y * b.stride, 0, (size_t)b.width * 4);
			ANativeWindow_unlockAndPost(w.w);
		}
	}

	bool Display::present(const uint32_t* const screens[2], const Damage dmg[2])
	{
		bool any = false;
		for (int s = 0; s < 2; s++)
		{
			Damage d = dmg[s];
			d.x0 = std::max(d.x0, 0); d.y0 = std::max(d.y0, 0);
			d.x1 = std::min(d.x1, screenWidth(s)); d.y1 = std::min(d.y1, screenHeight(s));
			if (s == 1 && sheetOn)
			{
				// the bottom screen goes to the quick sheet while it is up (BottomActivity's own surface is under it; the
				// bottom screen's fit is the sheet's then: sheetOpen)
				if (!d.empty() && !sheetBuf[0].map) continue;
				sheetDrawn = false;
				if (!d.empty()) p[1].fit.blit(screens[1], sheetBuf[0].map, sheetBuf[0].pitch, d);
				sheetPlace();
				any = true;
				continue;
			}
			Win& w = wins[s];
			std::lock_guard<std::mutex> l(w.m);
			if (!w.w) continue;
			if (w.fresh || w.width != p[s].fit.panelW() || w.height != p[s].fit.panelH())
			{
				// a surface that came back, or of another size: fitted again, and all of it drawn
				if (w.width != p[s].fit.panelW() || w.height != p[s].fit.panelH())
					p[s].fit.set(screenWidth(s), screenHeight(s), w.width, w.height, 0);
				d = Damage{ 0, 0, screenWidth(s), screenHeight(s) };
			}
			if (d.empty()) continue;
			Damage pd = p[s].fit.toPanel(d);
			ARect r = { pd.x0, pd.y0, pd.x1, pd.y1 };
			ANativeWindow_Buffer b;
			if (ANativeWindow_lock(w.w, &b, &r) != 0) continue;
			bool whole = w.fresh || r.left != pd.x0 || r.top != pd.y0 || r.right != pd.x1 || r.bottom != pd.y1;
			if (whole)
			{
				// (a buffer with nothing of the last frame in it: the bars black, the whole picture in)
				for (int y = 0; y < b.height; y++) memset((uint32_t*)b.bits + (size_t)y * b.stride, 0, (size_t)b.width * 4);
				d = Damage{ 0, 0, screenWidth(s), screenHeight(s) };
				pd = p[s].fit.toPanel(d);
			}
			pd.x1 = std::min(pd.x1, (int)b.width); pd.y1 = std::min(pd.y1, (int)b.height);
			p[s].fit.blit(screens[s], (uint8_t*)b.bits, (uint32_t)b.stride * 4, d);
			swizzle((uint8_t*)b.bits, b.stride, pd);
			ANativeWindow_unlockAndPost(w.w);
			w.fresh = false;
			any = true;
		}
		return any;
	}

	bool Display::sheetOpen()
	{
		if (sheetOn) return true;
		if (!android::showSheet(true)) return false;
		int sw, sh;
		{
			std::lock_guard<std::mutex> l(wins[2].m);
			if (!wins[2].w) return false;
			sw = wins[2].width; sh = wins[2].height;
		}
		// the sheet's picture: the bottom screen fitted to the overlay's size, kept here between its frames
		freeBuf(sheetBuf[0]);
		sheetBuf[0].pitch = (uint32_t)sw * 4;
		sheetBuf[0].size = (uint64_t)sw * sh * 4;
		sheetBuf[0].map = (uint8_t*)calloc((size_t)sheetBuf[0].size, 1);
		sheetBuf[0].handle = (uint32_t)sw; sheetBuf[0].fb = (uint32_t)sh;	// (its width and height)
		p[1].fit.set(screenWidth(1), screenHeight(1), sw, sh, 0);
		sheetOn = true;
		sheetShown = 0;
		sheetDrawn = false;
		return true;
	}

	void Display::sheetAt(float shown)
	{
		if (!sheetOn) return;
		sheetShown = shown;
		sheetPlace();
	}

	// the sheet drawn on the overlay at sheetShown: its top edge (1 - shown) of the way down, the rows above it clear
	bool Display::sheetPlace()
	{
		Win& w = wins[2];
		std::lock_guard<std::mutex> l(w.m);
		if (!w.w || !sheetBuf[0].map) return false;
		const int sw = (int)sheetBuf[0].handle, sh = (int)sheetBuf[0].fb;
		ANativeWindow_Buffer b;
		if (ANativeWindow_lock(w.w, &b, nullptr) != 0) return false;
		const int off = (int)std::lround((1.0f - sheetShown) * sh);
		for (int y = 0; y < b.height; y++)
		{
			uint32_t* dst = (uint32_t*)b.bits + (size_t)y * b.stride;
			const int sy = y - off;
			if (sy < 0 || sy >= sh) { memset(dst, 0, (size_t)b.width * 4); continue; }
			const uint32_t* src = (const uint32_t*)(sheetBuf[0].map + (size_t)sy * sheetBuf[0].pitch);
			const int n = std::min(sw, (int)b.width);
			for (int x = 0; x < n; x++) dst[x] = toAndroid(src[x]);
			if (n < b.width) memset(dst + n, 0, (size_t)(b.width - n) * 4);
		}
		ANativeWindow_unlockAndPost(w.w);
		sheetDrawn = true;
		return true;
	}

	void Display::sheetClose()
	{
		if (!sheetOn) return;
		sheetOn = false;
		android::showSheet(false);
		freeBuf(sheetBuf[0]);
		// the bottom activity's surface fitted again, and drawn whole when the menu draws next
		std::lock_guard<std::mutex> l(wins[1].m);
		if (wins[1].w) p[1].fit.set(screenWidth(1), screenHeight(1), wins[1].width, wins[1].height, 0);
		wins[1].fresh = true;
		exposed = true;
	}

	void Display::freeBuf(Buf& b)
	{
		free(b.map);
		b = Buf();
	}

	// no leases on Android: a game is another app, in front of the launcher (android_jni.cpp)
	int Display::lease(int) { return -1; }
	int Display::leaseBoth() { return -1; }
	bool Display::borrow(int) { return false; }
	void Display::giveBack() {}
	long Display::stormCheck() { return -1; }
	long long Display::vopIrqCount() { return -1; }

	// TODO(android): Settings > Display & sound > Screen colours: a colour matrix per display (SurfaceControl's
	// colour transform, a privileged call) instead of the DRM gamma table
	std::string Display::setColors(const ScreenColors& c)
	{
		return c.neutral() ? "" : "screen colours aren't applied on Android yet";
	}
}
#endif
