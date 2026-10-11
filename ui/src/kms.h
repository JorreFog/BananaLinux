#pragma once
// banana-ui: both panels through KMS, with no compositor and no GPU. Each panel gets two dumb scanout buffers;
// present() copies the rows that changed into the hidden one and flips to it on the next vblank.
#include <cstdint>
#include <string>

namespace banana
{
	class Display
	{
	public:
		~Display() { close(); }
		// the first DRM device with two connected DSI panels; top is the connector shown as the top screen ("DSI-2")
		bool open(const std::string& top, std::string& err);
		void close();
		bool isOpen() const { return fd >= 0; }
		int width() const { return W; }
		int height() const { return H; }
		int pollFd() const { return fd; }
		bool flipPending() const { return pending > 0; }
		// the composed screens (rows of width() words); rows [y0, y1) changed on each since the last present
		bool present(const uint32_t* const screens[2], const int y0[2], const int y1[2]);
		void handleEvents();	// the flip events from pollFd()
		// after a resume or a lost master: show the current buffers again
		void restore();

	private:
		struct Buf { uint32_t handle = 0, fb = 0, pitch = 0; uint64_t size = 0; uint8_t* map = nullptr; };
		struct Panel { uint32_t conn = 0, crtc = 0; uint8_t mode[68]; Buf buf[2]; int front = 0; int dmg0[2] = { 0, 0 }, dmg1[2] = { 0, 0 }; };
		int fd = -1, W = 0, H = 0, pending = 0;
		Panel p[2];
		bool makeBuf(Buf& b);
		void freeBuf(Buf& b);
	};
}
