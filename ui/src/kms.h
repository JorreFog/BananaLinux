#pragma once
// banana-ui: both panels through KMS, with no compositor and no GPU. Each panel gets two dumb scanout buffers;
// present() copies the part that changed into the hidden one and flips to it on the next vblank.
#include <cstdint>
#include <string>
#include "colors.h"
#include "damage.h"
#include "panelfit.h"

namespace banana
{
	class Display
	{
	public:
		~Display() { close(); }
		// the first DRM device with two connected panels of top's connector type; top is the connector shown as the
		// top screen ("DSI-2" on the handheld; "Virtual-2" on QEMU's virtio GPU, see tools/qemu-boot.sh)
		bool open(const std::string& top, std::string& err);
		// the boot's early light-up (banana-ui --panels-on, from rcS's first lines): both panels powered up while the
		// rest of the boot runs; the master given up as soon as the modesets are queued, so the menu's own open() takes the
		// display over at once (the panels lit, no second power-up) whenever it comes
		bool handOver = false;
		void close();
		bool isOpen() const { return fd >= 0; }
		// the canvas both screens are drawn on: the panels' size on the RG DS; on panels that differ (the AYN Thor) a 4:3
		// canvas each panel shows fitted (panelfit.h)
		int width() const { return W; }
		int height() const { return H; }
		// a touch at (px, py) of screen s's panel, in the canvas's px; false in the black bars around it
		bool panelToCanvas(int s, float px, float py, float& x, float& y) const { return p[s].fit.toCanvas(px, py, x, y); }
		// a touch as a fraction of its touchscreen's range (panelfit.h's touchToCanvas)
		bool touchToCanvas(int s, float nx, float ny, bool wide, float& x, float& y) const { return p[s].fit.touchToCanvas(nx, ny, wide, x, y); }
		bool fitted() const { return !p[0].fit.identity() || !p[1].fit.identity(); }
		// each screen's own canvas (the composed rows present() takes): the panel upright when its shape isn't the menu's
		// (the AYN Thor's 1920x1080 and 1240x1080; the menu's 4:3 is staged in it, soft.h's setScreens), else
		// width() x height(). BANANA_STAGE=fit: the 4:3 canvas on every screen, fitted with black bars instead
		int screenWidth(int s) const { return CW[s] ? CW[s] : W; }
		int screenHeight(int s) const { return CH[s] ? CH[s] : H; }
		// the screens are a compositor's windows (SDL2: Wayland under sway, on ROCKNIX; kms_sdl.cpp), not the panels
		bool compositor() const { return sdl; }
		// a window was uncovered or resized (SDL): the menu draws everything again; true once per event
		bool takeExposed() { bool e = exposed; exposed = false; return e; }
		// SDL rather than KMS for this run (BANANA_DISPLAY, WAYLAND_DISPLAY; false without SDL=1)
		static bool wantSdl();
		int panelWidth(int s) const { return p[s].fit.panelW() ? p[s].fit.panelW() : W; }
		int panelHeight(int s) const { return p[s].fit.panelH() ? p[s].fit.panelH() : H; }
		int pollFd() const { return fd; }
		bool flipPending() const { return pending > 0; }
		bool masterAtOpen = false;	// the master was had at open() (else another program still held it after the wait)
		int failedFlips = 0, failedSets = 0;	// page flips refused, and of those not put up by a modeset either (no master)
		// the composed screens (rows of width() words) and what changed on each since the last present
		bool present(const uint32_t* const screens[2], const Damage dmg[2]);
		void handleEvents();	// the flip events from pollFd()
		// after a resume or a lost master: show the current buffers again
		void restore();
		// after a game: the panels the menu owns (not a leased one) blacked at once, both buffers zeroed, the first
		// modeset up, front and pending reset. Whatever a game left on a panel (its last frame, a panel whose flip was
		// refused on the way back) is gone before the menu draws over it, so the launcher never shows stale game UI.
		void blank();
		// screen s's panel (its connector, CRTC and primary plane) leased to a program that draws on it itself (a
		// RetroArch game while the menu keeps the other panel for the game panel): the lease's fd, which is that
		// program's DRM device, or -1. present() and restore() leave the screen alone until close().
		int lease(int s);
		int leasedScreen() const { return leased; }
		// the display controller's interrupt storm (VOP2 POST_BUF_EMPTY, a panel's scanout underflowing ~100k times a
		// second, seen after RetroArch's leased panel was given back): measured over 200 ms, and when it storms both
		// CRTCs are switched off and on again, the one thing that ends it. The rate after, or -1 when it was calm.
		long stormCheck();
		// the interrupts so far, for a cheap look every few seconds (-1: unknown)
		static long long vopIrqCount();
		// the screen colours' table (colors.h): on c.screen's CRTC (GAMMA_LUT), and none on the other (the display
		// controller has one table); neutral values: none on either. "" when it took, else what the kernel said
		std::string setColors(const ScreenColors& c);

	private:
		struct Buf { uint32_t handle = 0, fb = 0, pitch = 0; uint64_t size = 0; uint8_t* map = nullptr; };
		struct Panel { uint32_t conn = 0, crtc = 0; uint8_t mode[68]; Buf buf[2]; int front = 0; Damage missed[2]; PanelFit fit; };
		int fd = -1, W = 0, H = 0, pending = 0, leased = -1;
		int lightWaiting = 0;	// lightBoth's flips still to come (a member: an event late past its wait lands here)
		Panel p[2];
		bool makeBuf(Buf& b, int w, int h);
		bool sdl = false, exposed = false;
		int CW[2] = { 0, 0 }, CH[2] = { 0, 0 };
		// the screens' canvases for panels (or windows) of upright size uw x uh each (screenWidth)
		void screensFor(const int uw[2], const int uh[2]);
		void* sdlWin[2] = { nullptr, nullptr };
		int sdlTimer = -1;
		bool openSdl(const std::string& top, std::string& err);
		void closeSdl();
		bool presentSdl(const uint32_t* const screens[2], const Damage dmg[2]);
		void handleSdl();
		void blankSdl();
		int leaseSdl(int s);
		bool lightBoth();
		void freeBuf(Buf& b);
	};
}
