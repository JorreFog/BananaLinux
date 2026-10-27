#pragma once
// banana-ui: the first boot's arrival, between the boot animation and the first-time setup (the owner's request,
// 2026-10-27). The boot animation plays as on every boot; then, instead of the iris to the menu:
//   the camera zooms in on the cartridge in the console (bottom panel) while the top panel goes dark,
//   the cartridge rumbles and bananas burst out of it, up off the bottom panel and into the top one,
//   the camera follows one banana (it stays in the middle of the top panel) with the others flying behind it, on
//   both panels, the stars streaming past,
//   the banana lights up until both panels are white, then black,
//   after a second "Welcome to" and the BANANAOS word fade in on the top panel, stay 2 s and fade out, and
//   the panels stay dark a moment; the host then fades the setup in.
// Drawn in software on both panels each refresh, in the boot animation's pixel look: the sprites at whole device
// pixels per pixel, the fades in steps (as an old console's palette fade), the zoom nearest-neighbour. ~8.7 s.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace banana
{
	class Arrival
	{
	public:
		// dir: the boot animation's pictures (the BANANAOS word); w, h: a panel
		bool load(const std::string& dir, int w, int h);
		// where the camera starts: the boot animation's last frame on both panels
		void setStart(const uint32_t* top, const uint32_t* bot);
		// the line over the BANANAOS word ("Welcome to", in the menu's font and language): a top panel picture, text on
		// black (the host draws it with the settings screen's text)
		void setCaption(const uint32_t* top);
		// both panels at t ms
		void draw(uint32_t* top, uint32_t* bot, double t);
		// the same picture as at the last call to draw (nothing moves): nothing to send to the panels
		bool still(double t) const;
		// "burst" as the bananas come out, "flash" as the panels go white
		std::function<void(const std::string&)> onCue;

		// the phases (ms)
		static constexpr double ZOOM_END = 700, RUMBLE_END = 1000, LOCK = 1950, FOLLOW_END = 3500, WHITE = 4400,
			BLACK_AT = 4550, CARD_AT = 5550, CARD_IN = 400, CARD_HOLD = 2000, CARD_OUT = 400, END = 8700;

	private:
		int W = 0, H = 0;
		float s = 1;
		int u = 1;		// the look's pixel: device px per art pixel (2 CSS px)
		std::vector<uint32_t> startTop, startBot, card;
		std::vector<uint32_t> dim[3];	// the top panel's picture going dark: its three steps between itself and the sky
		uint32_t wl = 0;				// the white this frame (0..255): every colour drawn goes that far to white (one pass, not two)
		bool cardMade = false, burstCued = false, flashCued = false;
		std::vector<uint8_t> word;	// BANANAOS (white on black)
		int wordW = 0, wordH = 0;
		std::vector<uint32_t> caption;
		mutable long lastKey = -2;

		struct Fruit
		{
			float x0, vx, vy, launch;	// launch: ms
			float slotX, slotY, sway, swayRate, phase;	// where it flies behind the lead (CSS px of the two panels)
			int k;						// art pixels: device px per sprite pixel = k * u
			int spin;					// quarter turns a second (0: none)
		};
		std::vector<Fruit> fruit;		// [0] is the lead
		struct Star { float x, y; int layer; };
		std::vector<Star> stars;

		int X(float v) const;
		long key(double t) const;
		void zoomed(uint32_t* d, const std::vector<uint32_t>& src, float ax, float ay, float cx, float cy, float z, int dy) const;
		void sky(uint32_t* d, int screen, double t) const;
		void banana(uint32_t* top, uint32_t* bot, float x, float y, int k, int turn, int white) const;
		void halo(uint32_t* top, uint32_t* bot, float x, float y, double t) const;
		void rect(uint32_t* d, int x, int y, int w, int h, uint32_t c) const;
		uint32_t C(uint32_t c) const;
		void makeCard();
	};
}
