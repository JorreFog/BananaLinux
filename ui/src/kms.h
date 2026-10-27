#pragma once
// banana-ui: the panel through KMS, with no compositor and no GPU. The panel gets two dumb scanout buffers;
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
		// the panel: the RPC's single 1240x1080 screen. The connector named like top is used when it is connected,
		// else any connected connector with a mode (the type is only a preference here).
		bool openSingle(const std::string& top, std::string& err);
		void close();
		bool isOpen() const { return fd >= 0; }
		// how many panels present() drives: 1 on this handheld
		int panels() const { return count; }
		// how the panel is mounted, in degrees (the connector's "panel orientation": the RPC's panel is 1080x1240
		// portrait, its 90 in the device tree): present() turns the composed screen by it into the scanout buffers
		int rotation() const { return rot; }
		// the panel's own size (its scanout buffers'): 1080x1240 on the RPC while the menu draws 1240x1080
		int panelWidth() const { return SW; }
		int panelHeight() const { return SH; }
		int width() const { return W; }
		int height() const { return H; }
		int pollFd() const { return fd; }
		bool flipPending() const { return pending > 0; }
		// the composed screens (rows of width() words) and what changed on each since the last present
		bool present(const uint32_t* const screens[2], const Damage dmg[2]);
		void handleEvents();	// the flip events from pollFd()
		// after a resume or a lost master: show the current buffers again
		void restore();

	private:
		struct Buf { uint32_t handle = 0, fb = 0, pitch = 0; uint64_t size = 0; uint8_t* map = nullptr; };
		struct Panel { uint32_t conn = 0, crtc = 0; uint8_t mode[68]; Buf buf[2]; int front = 0; Damage missed[2]; };
		int fd = -1, W = 0, H = 0, pending = 0, count = 1;
		int rot = 0, SW = 0, SH = 0;	// the panel's rotation, and its size in its own rows (the scanout buffers')
		Panel p[2];
		void turn(const uint32_t* src, Buf& bf, const Damage& d);
		bool makeBuf(Buf& b);
		void freeBuf(Buf& b);
	};
}
