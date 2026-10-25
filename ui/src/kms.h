#pragma once
// banana-ui: both panels through KMS, with no compositor and no GPU. Each panel gets two dumb scanout buffers;
// present() copies the part that changed into the hidden one and flips to it on the next vblank.
#include <cstdint>
#include <string>
#include "colors.h"

namespace banana
{
	// a rectangle of a screen that changed: [x0, x1) x [y0, y1)
	struct Damage
	{
		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		bool empty() const { return x0 >= x1 || y0 >= y1; }
		void add(const Damage& o)
		{
			if (o.empty()) return;
			if (empty()) { *this = o; return; }
			x0 = x0 < o.x0 ? x0 : o.x0; y0 = y0 < o.y0 ? y0 : o.y0;
			x1 = x1 > o.x1 ? x1 : o.x1; y1 = y1 > o.y1 ? y1 : o.y1;
		}
	};

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
		int width() const { return W; }
		int height() const { return H; }
		int pollFd() const { return fd; }
		bool flipPending() const { return pending > 0; }
		// the composed screens (rows of width() words) and what changed on each since the last present
		bool present(const uint32_t* const screens[2], const Damage dmg[2]);
		void handleEvents();	// the flip events from pollFd()
		// after a resume or a lost master: show the current buffers again
		void restore();
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
		struct Panel { uint32_t conn = 0, crtc = 0; uint8_t mode[68]; Buf buf[2]; int front = 0; Damage missed[2]; };
		int fd = -1, W = 0, H = 0, pending = 0, leased = -1;
		int lightWaiting = 0;	// lightBoth's flips still to come (a member: an event late past its wait lands here)
		Panel p[2];
		bool makeBuf(Buf& b);
		bool lightBoth();
		void freeBuf(Buf& b);
	};
}
