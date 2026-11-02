#pragma once
// banana-ui: the DS Pokémon companion's pages (docs/superpowers/specs/2026-11-02-nds-pokemon-companion-design.md):
// Battle, Party, Team, Here, Guide, Dex and Log, drawn for the DS bottom screen while the companion is open. They are
// drawn with the menu's own engine (rnds boxes and Pixelify Sans, banana-ui's palette and the theme's accent) into a
// screen of an off-screen rnds::Backend at the bottom panel's size (the overlay owns it: a banana::SoftBackend, whose
// pixels go into the shared image BananaDS shows instead of the game's bottom screen).
//
// Touch only: the game keeps every button while the companion is open, so tabs, rows, scrolling (drag), the
// Normal/Shiny switch and Back to game are taps and drags. Back to game covers the corner where BananaDS draws the
// icon, so the same spot opens and closes.
//
// Layout in CSS px of a 640x480 screen, scaled to the panel (x1.6 on the RG DS Plus), as the rest of banana-ui.
// Everything of the games (names, sprites, text) comes from RomData/Live, read from the player's own ROM and RAM.
#include "model.h"
#include "../../rnds/RndsUI.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace banana::nds
{
	// what the pages draw from (any may be null: the pages say what is missing)
	struct PageData
	{
		std::shared_ptr<const RomData> rom;
		std::shared_ptr<const Live> live;
		std::shared_ptr<const GuideData> guide;
		std::shared_ptr<const Advice> advice;
		std::shared_ptr<const Journey> journey;
		// the ROM still being read (decoded once, in the background): 0..1 and what it is on, shown instead of the pages
		bool reading = false;
		float readProgress = 0;
		std::string readWhat;
		std::vector<std::string> seenNotices;	// notice ids the player has already seen (no count on the tabs for them)
	};

	enum class Page : uint8_t { BATTLE, PARTY, TEAM, HERE, GUIDE, DEX, LOG, COUNT };

	class Pages
	{
	public:
		// be: the off-screen target; screen: the screen index to draw (beginScreen/endScreen); w x h: the panel's
		// device px (640x480 or 1024x768); assets: the rnds assets folder (fonts/PixelifySans-*.ttf); light: the
		// theme's palette (banana::pal(light), with the user theme's colours and the accent set by the host)
		Pages(rnds::Backend* be, int screen, int w, int h, const std::string& assets, bool light = false);
		~Pages();

		void setData(const PageData& d);
		void setLook(bool light);		// the theme changed (the host has applied it): everything drawn anew
		// the icon's rectangle on the bottom panel (device px): Back to game covers it. Default: the bottom-right
		// 48x48 CSS px
		void setCorner(int x, int y, int w, int h);

		// the companion opened: lands on the page an unseen notice is for, else Battle in a battle, else where it was
		void opened(double nowMs);
		void show(Page p);
		Page page() const { return cur; }
		void showSpecies(uint16_t species);	// the Dex at that species

		// touches, in device px of the panel
		enum class Action { NONE, REDRAW, CLOSE };
		Action touchDown(int x, int y, double nowMs);
		Action touchMove(int x, int y, double nowMs);
		Action touchUp(int x, int y, double nowMs);

		// animation: true when something moved since the last render; nextWake: ms until it next will (-1 never)
		bool update(double nowMs);
		int nextWake(double nowMs) const;
		void render();		// the page between the screen's beginScreen/endScreen
		void draw();		// the same within a frame the caller began (the overlay's begin()/end())

		int unseen() const;	// notices not yet seen (the icon's red count)

		// Gen V's season for the Here page's odds: the game's own is not read (not found in its RAM), so the page shows
		// which season the odds are for - the handheld's month (Live::season) until the player taps the one their game
		// shows (a game resumed from a state keeps the season it started in). season(): -1 when the game has none
		int season() const;
		int seasonPick() const { return pickedSeason; }	// -1: not picked (the logic may take it: Session::setSeason)

		// notices shown since the last call (their page was opened): the logic marks them seen
		std::vector<std::string> takeSeen();

		// (tests) the scroll of the page's main list and its most
		float scrollOf(Page p) const;
		float scrollOf(Page p, int list) const;		// (the page's list'th scroll area: Dex 0 = the list)
		float scrollMax(Page p) const;
		bool shiny() const { return dexShiny; }
		uint16_t dexSpecies() const { return dexSel; }
		int partySel() const { return partyIdx; }

	private:
		struct Impl;
		std::unique_ptr<Impl> d;
		Page cur = Page::PARTY;
		bool dexShiny = false;
		uint16_t dexSel = 1;
		int partyIdx = 0;
		int pickedSeason = -1;
		friend struct Impl;
	};

	// the companion's names for things the ROM has no text for (English; drawn through the menu's translations)
	const char* typeName(Type t);
	uint32_t typeColor(Type t);		// 0xRRGGBB for a type's chip
	const char* pageName(Page p);
	// the experience a level needs for the games' six growth rates (personal data's growth byte: 0 medium fast,
	// 1 erratic, 2 fluctuating, 3 medium slow, 4 fast, 5 slow)
	uint32_t expForLevel(uint8_t growth, int level);
	// a goal's name and its place for one line: "Gardenia, Eterna City"; the place left out when the name has it already
	// ("Rival Barry: Route 203")
	std::string goalWithPlace(const Milestone& m);
}
