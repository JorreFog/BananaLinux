#pragma once
// banana-ui: the GBA Pokémon companion on the game panel. The DS companion's pages (nds/pages.h) with gba::Host's data,
// switched with the panel's menu (its tiles, Settings draws them) as the DS companion is with the game's bottom screen:
// one button in the top-right corner - the companion's icon on the menu's header (iconPicture, its red badge the
// pages' unseen notices), the pages' Menu where it was - and the back button (or F, the menu always). The pages show
// first: "Reading your game" till the ROM's data is read, "No game data" when it can't be; a ROM hack not supported:
// "Not supported" alone. A battle starting shows the Battle page, its end the Party page (while the pages show).
#include "../nds/pages.h"
#include <memory>
#include <string>

namespace rnds { class Backend; }

namespace banana::pkmn { class Companion; }

namespace banana::gba
{
	class Host;

	class Panel
	{
	public:
		// how the pages are drawn: the off-screen target, the panel's screen, its size in device px, the rnds assets,
		// the theme
		struct Look { rnds::Backend* be = nullptr; int screen = 1, w = 640, h = 480; std::string assets; bool light = false; };

		// a game with the companion starts (host and comp: the game's; both stay until close). The pages show at once
		void open(Host* host, pkmn::Companion* comp, const Look& look);
		void close();		// (before the host goes)
		bool active() const;
		bool pagesShown() const { return active() && pages && onPages; }

		// each turn of the menu's loop: the host's newest data, the battle's start and end. True: draw again
		bool update(double nowMs);
		void draw();		// the pages (pagesShown), within the frame the caller began
		// a touch on the pages (pagesShown): their Menu shows the menu
		void touch(bool down, int x, int y, double nowMs);
		void touchMove(int x, int y, double nowMs);

		// the switch: the pages (on the page a new notice is for, else Battle in a battle, else where they were) or the menu
		void showPages(double nowMs);
		void showMenu() { onPages = false; }
		void toggle(double nowMs) { if (pagesShown()) showMenu(); else showPages(nowMs); }
		int badge() const;			// the icon's red count: the pages' notices not yet seen

		std::string view() const;	// (tests) "pages:<page>", "game" (the menu) or "" without a game
		int ready() const;			// (tests) the ROM's data: 1 read, -1 couldn't be, -2 a hack not supported, 0 not yet (or no game)
		std::string title() const;	// (tests) the game's title, once read

	private:
		Host* host = nullptr;
		pkmn::Companion* comp = nullptr;
		std::unique_ptr<nds::Pages> pages;
		bool onPages = true, inBattle = false;
	};
}
