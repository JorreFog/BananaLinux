#pragma once
// banana-ui: both panels through KMS, with no compositor and no GPU. Each panel gets two dumb scanout buffers;
// present() copies the part that changed into the hidden one and flips to it on the next vblank.
#include <cstdint>
#include <string>

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

	private:
		struct Buf { uint32_t handle = 0, fb = 0, pitch = 0; uint64_t size = 0; uint8_t* map = nullptr; };
		struct Panel { uint32_t conn = 0, crtc = 0; uint8_t mode[68]; Buf buf[2]; int front = 0; Damage missed[2]; };
		int fd = -1, W = 0, H = 0, pending = 0, leased = -1;
		Panel p[2];
		bool makeBuf(Buf& b);
		void freeBuf(Buf& b);
	};
}
