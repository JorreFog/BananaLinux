#pragma once
// banana-ui: the GBA Pokémon companion on the game panel. The DS companion's pages (nds/pages.h) with gba::Host's data
// have all of the panel; their Back to game shows the panel's own tiles (Settings draws them), whose tab bar is the
// pages' tabs and Game, each opening its page. From the game's start: "Reading your game" till the ROM's data is read,
// "No game data" when it can't be. A battle starting shows the Battle page, its end the Party page (on the pages).
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
		enum { TABS = (int)nds::Page::COUNT + 1 };	// the tiles' tab bar: the pages' tabs, then Game
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
		// a touch on the pages (pagesShown): Back to game shows the tiles
		void touch(bool down, int x, int y, double nowMs);
		void touchMove(int x, int y, double nowMs);

		// the tiles' tab bar: tab i's label, whether it can be opened now (Battle only in a battle), and opening it
		static std::string tabLabel(int i);
		bool tabOn(int i) const;
		void openTab(int i);
		std::string view() const;	// (tests) "pages:<page>", "game" or "" without a game
		int ready() const;			// (tests) the ROM's data: 1 read, -1 couldn't be, 0 not yet (or no game)
		std::string title() const;	// (tests) the game's title, once read

	private:
		Host* host = nullptr;
		pkmn::Companion* comp = nullptr;
		std::unique_ptr<nds::Pages> pages;
		bool onPages = true, inBattle = false;
	};
}
