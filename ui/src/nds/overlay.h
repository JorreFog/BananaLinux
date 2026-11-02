#pragma once
// banana-ui: the DS companion's overlay, the painter's side of the companion overlay (COMPANION-SPEC section 10,
// ~/banana-emu/common/COMPANION-SPEC.md; bovl.h/bovl.c from common, copied as bcomp's are). A DS game takes both panels,
// so the companion has none of its own: banana-ui draws its pages off-screen at the bottom panel's size and hands each
// changed picture to BananaDS through /run/banana/companion-overlay.shm (BANANA_COMPANION_OVERLAY in the game's
// environment). BananaDS blends banana-ui's icon (with its red badge) into the corner of the game's bottom screen; a
// tap on it opens the companion: BananaDS then shows the picture instead of the game's bottom screen and passes every
// touch on that panel here, while the game keeps running with all of its buttons. A tap where the icon was ("Back to
// game") closes it again. The design: docs/superpowers/specs/2026-11-02-nds-pokemon-companion-design.md.
//
// The pages draw on screen 1 of backend() (a SoftBackend of the panel's size: its own, the menu's stays as it was)
// between begin() and end(); end() sends what changed. Nothing is drawn or sent while the companion is closed.
#include "../bovl.h"
#include "../soft.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <sys/types.h>

namespace banana::nds
{
	struct Rect
	{
		int x = 0, y = 0, w = 0, h = 0;
		bool empty() const { return w <= 0 || h <= 0; }
		bool in(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
	};
	// a touch on the companion's picture (its pixels: the bottom panel's), from BananaDS
	struct Touch
	{
		enum Type { DOWN = BOVL_TOUCH_DOWN, MOVE = BOVL_TOUCH_MOVE, UP = BOVL_TOUCH_UP } type = DOWN;
		int x = 0, y = 0;
		uint32_t ms = 0;	// CLOCK_MONOTONIC
	};

	// the icon's corner: top-right. The games put Back/Cancel/close in the bottom-right corner of nearly every touch menu
	// (HGSS party/bag/Pokégear, Gen V menu X and bag arrow, battle submenus) and SoulSilver's touch-only "A Touch" there;
	// the top-right one holds the least (validate-plus, validate-rgds). Pages put Back to game where it is
	constexpr uint32_t ICON_CORNER = BOVL_CORNER_TR;
	// a tap counts this far past the icon's plate (a fingertip, ~8 mm across, touching it), on both panel sizes
	constexpr float ICON_SLOP_MM = 4.0f;

	class Overlay
	{
	public:
		~Overlay() { close(); }
		// before the game starts: the file for a w x h panel (the game's environment then names it: env()). false: none
		bool create(int w, int h, const std::string& path = BOVL_DEFAULT_PATH);
		void close();		// after the game: unmapped and removed
		bool valid() const { return shm != nullptr; }
		const std::string& file() const { return path; }
		int width() const { return W; }
		int height() const { return H; }
		float scale() const { return H / 480.0f; }	// CSS px -> panel px (as Settings')
		bovl_shm* raw() const { return shm; }

		// BananaDS attached and can show the picture
		bool presenter() const;
		// the picture is on the panel instead of the game's bottom screen (or about to be: the first frame for this
		// opening is drawn and sent as soon as it is seen)
		bool shown() const { return shm && bovl_is_open(shm); }
		uint32_t openSeq() const { return shm ? bovl_open_seq(shm) : 0; }
		void requestOpen(bool open);	// (a page's own Back to game, or a test: BananaDS opens or closes)

		// the icon over the game: drawn here in BananaOS's look (the accent plate with a cream lip, a page glyph, the red
		// badge with the count when badge > 0), half see-through, in ICON_CORNER
		void setIcon(bool on, int badge);
		// how far around the icon's picture a tap still counts (panel px; BananaDS takes it while closed)
		int iconSlop() const;
		// where the icon is, in picture pixels (BananaDS placed it): Back to game goes there. Before BananaDS placed it,
		// where it will be
		Rect iconRect() const;

		// the off-screen render target and the frame around a page's drawing
		rnds::Backend& backend() { return *be; }
		void begin();
		// what changed since the last frame sent: false when no slot was free (BananaDS copying the last one): call
		// send() again shortly, the picture is kept
		bool end();
		bool send();
		bool pending() const { return dirtyPending || seqPending; }
		const uint32_t* pixels() const { return be->pixels(1); }	// (tests)

		// the next touch from BananaDS: false when none
		bool pollTouch(Touch& t);
		uint64_t framesSent = 0, bytesSent = 0;

	private:
		bovl_shm* shm = nullptr;
		std::string path;
		int W = 0, H = 0;
		std::unique_ptr<SoftBackend> be;
		bovl_rect dirty{ 0, 0, 0, 0 };
		bool dirtyPending = false, seqPending = false;
		uint32_t sentSeq = 0, touchTail = 0;
		int iconW = 0, iconH = 0, iconBadge = -1;
		bool iconOn = false;
	};

	// what the companion shows: the companion's pages (pages.*), or the overlay's test page
	class OverlayPage
	{
	public:
		virtual ~OverlayPage() {}
		virtual void opened() {}						// shown again (after an open)
		virtual bool tick(double nowMs) = 0;			// true: something to draw again (new data, an animation)
		// the whole page, on screen 1 of be (beginScreen/endScreen are the overlay's), w x h panel pixels; back: where
		// Back to game is (the icon's place; a tap there closes without coming here)
		virtual void draw(rnds::Backend& be, int w, int h, const Rect& back) = 0;
		virtual void touch(const Touch& t) = 0;
		virtual bool wantsClose() { return false; }		// a page's own close (a wider Back to game button)
		virtual int badge() const { return 0; }
		virtual bool iconOn() const { return true; }
	};

	// the theme's folder (the menu's --assets: the Pixelify fonts in <dir>/fonts) for the icon's badge and the test page
	void setAssets(const std::string& dir);
	// the overlay's test page (settings.conf nds_companion = test, any DS game): what the loop does and costs, a dot
	// that follows the finger, a little animation the size of a Dex sprite, Back to game
	std::unique_ptr<OverlayPage> makeTestPage(Overlay& ov);

	// the companion's loop while a DS game runs (instead of a plain waitpid): touches to the page, the page drawn and
	// sent while open (at most 30 times a second, 60 while a finger moves), the icon's badge. Returns when the game
	// ends, status as waitpid's. idle: called each turn (the memory reads' pump: CompLink), may be empty
	void runWhileGame(Overlay& ov, OverlayPage& page, pid_t pid, int& status, const std::function<void(double)>& idle = {});
}
