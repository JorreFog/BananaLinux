#pragma once
#include "complink.h"
#include <sys/types.h>
#include <cmath>
// banana-ui: the settings screen (Start in the menu), drawn in ROCKNIXDS Pixel's look with the engine's own pieces
// (its box styles painted by rnds::paintBox, Pixelify Sans, the theme's pixel icons). Bottom panel: the list, one
// row per setting in the slots' style, ◀ value ▶ on the right, and the B / A chips; top panel: the setting's
// section icon, its name and what it does. Every change is applied and saved at once:
//   /data/banana/settings.conf     the menu's (theme, motion, sounds, clock, splash, Wi-Fi, Bluetooth)
//   /data/banana/bananad.conf      sleep and the LEDs (bananad reloads it on SIGHUP)
//   /data/banana/superdrastic.conf SuperDrastic's shader and CPU clock (run-drastic reads it)
//   /data/bananads/bananads.conf   BananaDS's own settings (shader, 3D resolution, ...; BananaDS reads it)
//   /data/drastic/config/drastic.cfg  the DS's 3D resolution
//   /sys/class/backlight + /data/banana/backlight   brightness
// and the shelf's Files (settings_files.cpp): the cards' folders, copied, moved, renamed and deleted through fileops.h
// and its servers (settings_servers.cpp): banana/nas.conf through banana-fetch, its jobs in banana/downloads
#include "../rnds/RndsUI.h"
#include "input.h"
#include "wifi.h"
#include "bluetooth.h"
#include "colors.h"
#include "speaker_eq.h"
#include "pokemon.h"
#include "gamecomp.h"
#include "gba/panel.h"
#include "lang.h"
#include <map>
#include <memory>
#include <mutex>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace banana::gba { class Host; }

namespace banana
{
	struct SPal;	// (settings_draw.h: the palette)
	class VirtualKeyboard;	// (vkbd.h: the panel's keyboard, on /dev/uinput)
	class Settings
	{
	public:
		// every Settings row's help and every game page row's (as the panel would lay it out now: the language's too)
		// that doesn't fit its box at the smallest size, one line each to log; and how many were checked (ui/tests)
		int helpCheck(const std::function<void(const std::string&)>& log);
		Settings(rnds::Backend* be, const std::string& assets, const std::string& dataDir, float scale, const std::string& variant);
		void setVariant(const std::string& variant);	// "dark", "light", or "carts" (dark, in Inter: setFontDir first)
		void setFontDir(const std::string& dir) { fontDir = dir; }
		// a layout theme's own layer behind the pages (its screens.settings: a status bar, a glow), drawn by the host's
		// layout engine over each screen's background; null: none
		std::function<void(int screen)> chrome;
		// the library's systems.conf and ROM folder (Files: the card is the ROM folder's parent, the systems' folders stay put)
		void setLibraryPaths(const std::string& systems, const std::string& roms) { systemsConf = systems; romRoot = roms; }
		// the systems list, said before a Settings is made (its Emulators rows are made with it: which systems there are,
		// and the core each one starts with)
		static void useSystemsList(const std::string& path);
		void open();
		void openPower();	// the Power list (Sleep, Restart, Shut down): Y on the groups
		// the quick menu (the AYN Thor's Home held in a game; settings_quick.cpp): the screens' brightness, the volume, the
		// running game's performance mode (gameRom's game: per-game-perf.conf, as its game page's Performance row) and
		// the handheld's fan and stick lights where it has them, on the bottom screen only; B closes it
		void openQuick();
		std::string quickState();	// (the tests) its rows and their values, as a line
		void openSection(const std::string& section);	// a list with no tile (Shown systems)
		// the systems for Shown systems (id, name): settings.conf's shelf.<id>; a change there is SHELVES
		void setSystems(const std::vector<std::pair<std::string, std::string>>& systems);
		std::vector<std::pair<std::string, std::string>> shelfSystems;
		void addShelfRows();
		void close();
		// the shelf's Files tile: the cards' folders and files, copied, moved, renamed and deleted (settings_files.cpp)
		void openFiles();
		// the headless test's Files jobs on their own thread, as on the handheld (BANANA_FILES_ASYNC; else in sync mode
		// they are done at once)
		bool filesAsync = false;
		bool filesWorking() const;	// a Files job still at work on its thread (the headless test waits for it)
		// a roms folder on the second card not offered to the library yet (a download or an unpacking made it): offered
		void offerSd2Roms();
		// banana-fetch's session (Files' servers) stopped: when Settings closes, and the menu at its end
		void closeServers();
		// a card came or went (the host saw the mount table change): Files' cards again; true when that changed the page
		bool cardsChanged();
		// a line for the log (a Files job's outcome, an action refused): the host's logf
		std::function<void(const std::string&)> log;
		// the first-time setup, from the host: boot = the first boot's (it ends in the host's Welcome and the menu:
		// takeSetupFinished), else as from Settings > System (it ends back in Settings); at: the slide to start on (a
		// setup cut off by a restart or a language change: banana/setup-step)
		void setup(bool boot = true, const std::string& at = "") { mOpen = true; openSetup(boot, at); }
		bool takeSetupFinished() { bool r = setupEnded; setupEnded = false; return r; }
		std::string ownerName() { return dsName(); }
		std::string setupState();	// (the tests) the slide, its choices and the checks' results, as a line	// the DS's name ("" none): the Welcome says it
		// banana/setup-step: the slide a setup was on ("" none) and whether it was the boot's
		static std::string setupStep(const std::string& dataDir, bool* boot = nullptr);
		static bool setupNeeded(const std::string& dataDir);	// no setup-done, or one from before 0.5.0's setup
		static constexpr const char* SETUP_MARK = "setup 2";
		// (the host's library) how many games have a cover, of how many: the Box art slide says so for art fetched before
		std::function<std::pair<int, int>()> artCount;
		// a title card on black (the host's arrival and Welcome): l1 on the top panel at y (CSS px), l2 under it in the
		// muted colour; the bottom panel black
		void renderCard(const std::string& l1, const std::string& l2, float y, float size);
		// the shelf's Bshare tile: files to and from phones and computers with LocalSend (settings_bshare.cpp)
		void openBshare();
		// Bshare's helper serving (seen by LocalSend's apps, receiving): from the start while Receive is on
		static void bshareServe(bool want);
		bool bshareReceiving() const;
		bool isOpen() const { return mOpen; }
		std::vector<std::string> deletedFiles;	// what the last Delete game removed (DELETED)
		int gridCount() const;	// the groups with a tile (Power has none)
		int powerGroup() const;	// -1: none
		// the on-screen keyboard is up (a password or an account being typed): the flight recorder leaves touches'
		// positions out while it is
		bool typing() const { return mOpen && page == KEYBOARD; }
		bool devOn = false;		// System's Developer options row unlocked (settings.conf's developer; PR #94)
		int aboutTaps = 0;		// A's on System > About so far (seven unlock it)
		bool animating() const;	// the highlight or the list still easing into place: frames wanted

		// The game panel (settings.cpp, end): while a RetroArch game runs on one panel through a DRM lease, the other
		// shows the game's RetroAchievements (progress and the latest unlocks with their badges) and touch tiles for
		// RetroArch (save, load, its menu, fast forward, quit), in the in-game menu's look.
		enum GameAction { G_NONE, G_SAVE, G_LOAD, G_MENU, G_FFWD, G_QUIT, G_LOADSLOT };
		void gameOpen(const std::string& title, const std::string& system, int screen, bool touch);
		// the panel's on-screen keyboard (a computer's game: C64, ZX Spectrum): the virtual keyboard the emulator
		// reads as a USB one (vkbd.h), made before the game starts. null: no Keys tile on the panel
		void gameKeyboard(VirtualKeyboard* k) { vkbd = k; }
		// the game's file (before gameOpen): its save states' names (RetroArch's: <name>.state, .state1 ... in
		// /data/states, thumbnails <state>.png) for the panel's States view (a tap on the achievements box)
		void gameRom(const std::string& rom, const std::string& stateDir = "/data/states");
		int gameSlot() const { return gSlotPick; }	// G_LOADSLOT's slot
		void gameClose();
		bool gameUpdate(double nowMs);		// true: the panel changed (a log line, a reply, a badge, a flash ending)
		// true once after something the player should see came up on the panel (an achievement unlocked): it wakes a
		// dimmed bottom panel, as a touch does (gameplay buttons don't)
		bool gameWake() { bool w = wake; wake = false; return w; }
		void gameRender();
		GameAction gameTouch(float x, float y, bool down);	// device px on the panel's screen
		bool gameKeysOpen() const { return gKeysOpen; }	// (tests: the panel's keyboard page is up)
		// (tests: what the GBA Pokémon companion's panel shows: "game" (the tiles), "pages:<page>", "" without one; the
		// game companion's: "game" or "gc:<page>")
		std::string gamePanelView() const;
		// a button pressed while the game runs (its evdev code; the game has it too): F shows the menu (its tiles, where
		// F's combos are printed), a button whose job is Companion (Settings > Other systems > Buttons > Game Boy
		// Advance: Back's, by default) switches between the menu and the companion. True: the panel changed
		bool gameButton(int code, double nowMs);
		void gameTouchMove(float x, float y);				// a press moving (the GBA companion's pages scroll by drag)
		// the GBA Pokémon companion's pages (gba/panel.h) for the game the host runs one for: host, null none (after
		// gameOpen, with companion set); gbaRelease before the host goes
		void gbaAttach(gba::Host* host);
		void gbaRelease();
		int gbaReady() const { return gbaPanel.ready(); }			// (tests: 1 its data read, -1 none, 0 not yet)
		std::string gbaTitle() const { return gbaPanel.title(); }
		bool gameProgress(int& unlocked, int& total);			// the game's achievements, once the server said
		// the Pokémon companion (pokemon.h), when the host runs one for this game: the party, the battle and the
		// achievements as tabs in the panel's middle box (set before gameOpen, null after gameClose)
		pkmn::Companion* companion = nullptr;
		// the game companion (gamecomp.h) for another GBA game with a definition: its seven pages as tabs, the same way
		// (set before gameOpen, null after gameClose; never both)
		gcomp::Engine* gameComp = nullptr;
		// the emulator's memory export (complink.h), when it attached to it: its RetroAchievements block (rich presence,
		// unlocks, the achievements closest to done) replaces RetroArch's log (set before gameOpen, null after gameClose)
		const CompLink* link = nullptr;
		// (tests) every page of the game companion's game drawn once, its values as read so far: a line for each page
		// that needs more than one screen, a tab that falls back to the page's kind, a short reference page
		int gcAudit(const std::function<void(const std::string&)>& log);
		void gcShow(int view) { gcView = view; gcScreen = 0; gcList = gcRow = -1; if (gameComp) gameComp->want(view >= gcomp::NUM_PAGES ? -1 : view); }	// (tests)
		// box art (banana-scrape) finished since the last call: the library is read again
		bool takeLibraryChanged() { bool r = libChanged; libChanged = false; return r; }

		// what the host has to do after an input (ACTED: a page did something that changes no setting, as Files' copy or
		// rename: the select sound alone, not CHANGED's settings.conf read again and the menu's texts made again)
		enum Result { NONE, MOVED, CHANGED, CLOSED, THEME, REBOOT, POWEROFF, SEARCH, TITLES, LANGUAGE, DELETED, SHELVES, ACTED };
		// DELETED: the game page's Delete game removed the ROM (deletedFiles says what went): the library read again
		// SELECT in the menu: the keyboard alone, for a search of the games; SEARCH when it is done, with the text in
		// searchText (empty: the search taken away)
		void openSearch(const std::string& start);
		// X on a DS game in the library: which emulator runs this one game (title shown, ROM file name as the key)
		// X on a game: its own page. A DS game's emulator and shader, and every game's name and sort title (#29):
		// TITLES when one of those was typed, with them in gameName / gameSort for gameKey ("" each: as scraped)
		// a game's own performance mode (banana-perf's profile; its per-game options' Performance row), "" for its system's
		std::string gamePerf(const std::string& romName) { return perGame("per-game-perf.conf", romName); }
		void openGame(const std::string& title, const std::string& romName, bool nds = true,
			const std::string& key = "", const std::string& name = "", const std::string& sort = "", const std::string& path = "",
			const std::string& scraped = "", const std::string& rocknixSystem = "");
		std::string gameKey, gameName, gameSort;
		// the display's colour table (main: Display::setColors), for Screen colours: off while its page is up, the saved
		// values when it closes
		std::function<std::string(const ScreenColors&)> setColors;	// "" when it took, else why not
		bool colorsOpen() const { return mOpen && page == COLORS; }
		std::string searchText;
		std::function<int(const std::string&)> searchCount;	// the games the text typed so far finds (the host's library)
		// wakes the host's loop from another thread (a Files job finished: update() takes it); unset, the pages look
		// for it on their own wake instead
		std::function<void()> wakeHost;
		Result press(Key k);
		Result touch(float x, float y, bool down);	// bottom panel, device px
		void render();
		// the textures the pages keep (the headless test's mem): their entries, the bytes they hold, the byte budget
		struct CacheStats { size_t entries = 0, bytes = 0, budget = 0; };
		CacheStats cacheStats() const;
		// background work (the Wi-Fi scan, joining a network): true when something changed and wants a frame
		bool update(double nowMs);
		// ms until update() has something to do, -1 nothing pending
		int nextWake(double nowMs) const;
		int pageWake(double nowMs) const;	// (the pages' own)
		// run commands at once instead of in the background (the headless test)
		void setSync(bool on) { sync = on; }

		// a notice over the screen being drawn (between the backend's beginScreen and endScreen): the battery icon at
		// percent, a title and a line or two, in the bubble style near the bottom of the panel
		void notice(const std::string& title, const std::string& body, int percent);
		// a notification's banner at the top (Bshare's): sign 1 a tick, 2 a cross, 3 "!", in the accent's square
		void banner(const std::string& title, const std::string& body, int sign, uint32_t accent);
		// the volume or brightness keys' level: a small bubble near the bottom of the panel, its label, the percent and
		// a bar (drawn like notice)
		void level(const std::string& label, int percent);
		// a pop-up along the top of the top panel in ROCKNIXDS's look (the RetroAchievements account check's result);
		// the host draws it over the top panel while Settings is open
		void drawToast(double nowMs);
		// banana-ui's own arguments for drawing a theme's picture headless (--assets, --banana, --systems, --roms)
		std::string thumbArgs;
		// the same pop-up for the menu (Settings closed): the host's own message ("i" sign, the accent colour)
		void popup(const std::string& l1, const std::string& l2);

	private:
		struct Item
		{
			std::string section, icon, label, help;
			std::string mirror;						// a hotkeys page's L2 to R3 (INFO): the row whose value it shows
			std::string file, key;					// where it is kept ("" for the special ones)
			std::vector<std::string> names, values;	// shown, stored
			std::vector<std::string> hints;			// what each option does, a short line (optional; the top screen)
			int at = 0;
			enum Kind { CHOICE, BRIGHTNESS, VOLUME, INFO, ACTION, PAGE } kind = CHOICE;
		};
		// the backend the pages draw through: the host's, the top screen's drawing moved sideways by dx device px while a
		// theme puts Settings' text column elsewhere on a wider screen (settings.left; renderPage only)
		struct StageShift : rnds::Backend
		{
			rnds::Backend* real = nullptr;
			float dx = 0, cw = 640, ch = 480;	// (the canvas, device px)
			float left = -1, k = 1;				// the column's edge from the screen's (units; < 0 none), device px a unit
			int screen = -1;
			// a theme's pages on the screen's own shape (settings.layout "screen"; lay): what a page draws in the canvas's
			// units is put on the screen scaled by g from (tx, ty), in whole screen pixels. The bottom screen: the
			// canvas's 18..622 across the screen with the design's 64 px margins, its top a little over the screen's, the
			// button hints (chips, while face() draws them) dropped to the screen's bottom edge. The top screen: the text
			// column at `left`, what is under the header (from bodyFrom on) moved down by bodyDy to make room for the
			// large title. rk: screen pixels a device px there (what is drawn is made at that size: nothing is scaled)
			bool lay = false, chips = false;
			float g = 1, tx = 0, ty = 0, drop = 0, sk = 1, q = 1, ax = 0, ay = 0, bodyFrom = 1e9f, bodyDy = 0;
			bool on() const { return lay && screen >= 0; }
			float rk() const { return on() ? g * sk : 1.0f; }
			float px() const { return on() ? q : k / 2.25f; }
			float u1 = 0;	// the bottom screen's units a design px (set when it was begun)	// screen pixels a pixel of the 1080-line design
			float snapX(float v) const { return std::round((v - ax) * sk) / sk + ax; }
			float snapY(float v) const { return std::round((v - ay) * sk) / sk + ay; }
			float mapX(float x) const { return tx + x * g; }
			float mapY(float y) const { return ty + y * g + (chips ? drop : 0) + (y >= bodyFrom ? bodyDy : 0); }
			float off() const { return screen == 0 ? dx : 0; }
			rnds::TexPtr upload(const rnds::Bitmap& b, bool smooth) override { return real->upload(b, smooth); }
			void draw(const rnds::TexPtr& t, float x, float y, float w, float h, const rnds::Color& tint) override
			{
				if (on()) real->draw(t, snapX(mapX(x)), snapY(mapY(y)), w * g, h * g, tint);
				else real->draw(t, x + off(), y, w, h, tint);
			}
			void fill(float x, float y, float w, float h, const rnds::Color& c) override
			{
				if (!on()) { real->fill(x + off(), y, w, h, c); return; }
				float x0 = snapX(mapX(x)), x1 = snapX(mapX(x + w)), y0 = snapY(mapY(y)), y1 = snapY(y0 + h * g);
				if (w > 0 && x1 - x0 < 1 / sk) x1 = x0 + 1 / sk;
				if (h > 0 && y1 - y0 < 1 / sk) y1 = y0 + 1 / sk;
				real->fill(x0, y0, x1 - x0, y1 - y0, c);
			}
			void pushClip(float x, float y, float w, float h) override
			{
				if (!on()) { real->pushClip(x + off(), y, w, h); return; }
				float x0 = snapX(mapX(x)), y0 = snapY(mapY(y));
				real->pushClip(x0, y0, snapX(mapX(x + w)) - x0, snapY(y0 + h * g) - y0);
			}
			void popClip() override { real->popClip(); }
			void beginScreen(int sc) override
			{
				screen = sc;
				real->beginScreen(sc);
				// the text column (x 50 of the canvas) put at `left` from the screen's own left edge
				dx = 0;
				float ax0 = 0, ay0 = 0, aw = cw, ah = ch;
				const bool staged = real->stageArea(ax0, ay0, aw, ah);
				if (!staged) { ax0 = 0; ay0 = 0; aw = cw; ah = ch; }
				if (sc == 0 && left >= 0) dx = std::round(ax0 + (left - 50) * k);
				chips = false;
				if (!lay) return;
				ax = ax0; ay = ay0;
				sk = staged ? std::max(0.05f, real->stageScale()) : 1.0f;
				q = ah * sk / 1080;
				if (sc == 1)
				{
					g = (aw * sk - 128 * q) / 604 / (k * sk);
					tx = 64 * q / sk + ax - 18 * k * g; ty = -14 * q / sk + ay;
					drop = (968 + 14) * q / sk - 422 * k * g;
					bodyFrom = 1e9f; bodyDy = 0;
					u1 = q / (k * g * sk);
				}
				else { g = 1; tx = dx; ty = 0; drop = 0; bodyFrom = 140 * k; bodyDy = 24 * k; }
			}
			void endScreen() override { screen = -1; real->endScreen(); }	// (the host's overlay, drawn in it, isn't moved)
			bool stageArea(float& x, float& y, float& w, float& h) const override
			{
				bool r = real->stageArea(x, y, w, h);
				if (on())
				{
					if (!r) { x = 0; y = 0; w = cw; h = ch; }
					x = (x - tx) / g; y = (y - ty) / g; w /= g; h /= g;
					return true;
				}
				if (!off()) return r;
				if (!r) { x = 0; y = 0; w = cw; h = ch; }
				x -= off();
				return true;
			}
			// a touch on the bottom screen (device px of the canvas, as the host hands it) where the page drew it: the
			// hints' band (the screen's last 120 px of the design's 1080) back up at the canvas's chips
			void untouch(float& x, float& y) const
			{
				if (!lay) return;
				const float yChips = (1080 - 120) * q / sk + ay;
				if (y >= yChips) y = (y - ty - drop) / g;
				else y = std::min((y - ty) / g, 414 * k);
				x = (x - tx) / g;
			}
		};
		StageShift shift;
		rnds::Backend* be;
		std::string assets, data;
		float s;
		bool light = false, mOpen = false;
		std::vector<Item> items;
		std::string langHelp;	// (the Language row's help: who translated each)
		std::string coreSig;	// the RetroArch cores installed when the rows were made (coreSignature)
		int sel = 0, top = 0, armed = -1;			// armed: an action waiting for its second A
		// the groups (the items' sections, in order): Settings opens on a grid of them, as the in-game menu's main page;
		// A opens one's list, B goes back to the grid
		std::vector<std::string> groups;
		std::map<std::string, std::string> groupIcon;	// a section's icon: its first row's as build adds them, whatever ORDER puts first
		int gsel = 0;
		bool inGroups = true;
		int groupOf(int item) const;			// the group an item is in
		int groupStart(int g) const, groupEnd(int g) const;	// its items: [start, end)
		std::string groupHint(int g, int names = 2) const;	// what is in it, for its tile (its first rows' names)
		// a section's place: the tile's section down to its own, through the rows that open each subpage
		// (Nintendo DS, SuperDrastic, SuperDrastic hotkeys); a tile's section alone
		std::vector<std::string> sectionPath(const std::string& section) const;
		const Item* itemByKey(const std::string& key) const;	// the first row with that key, or none
		Result pressGroups(Key k);
		void renderGroups();
		bool downOnRow = false;
		float downX = 0, downY = 0;
		std::string about, wifiInfo, ip;

		// the Wi-Fi pages: the networks around, and the keyboard for a password (settings-wifi.cpp)
		enum Page { LIST, NETWORKS, KEYBOARD, UPDATES, SETUP, GAME, ARTPICK, COLORS, BLUETOOTH, THEMES, CREDITS, PICKGAMES, FILES, BSHARE, SERVERS, SPEAKEREQ, QUICK } page = LIST;
		// Home server > Choose games: the server's games (banana-sync games), a system at a time, ticked to sync
		struct PickGame { std::string sys, key, title; long long bytes = 0; bool on = false, there = false; };
		std::vector<PickGame> pickGames;
		std::vector<std::string> pickSys;		// the systems' folders, in the server's order
		int pickLevel = 0, pickSysSel = 0, pickSel = 0, pickTop = 0;	// level 0: the systems, 1: a system's games
		bool pickLoading = false, pickDirty = false;
		std::string pickError;
		wifi::Job pickJob;
		void openPickGames();
		bool pollPickGames();
		Result pressPickGames(Key k);
		void renderPickGames();
		void savePickGames();
		std::vector<int> pickList() const;		// the games of the system open (indices into pickGames)
		void stopSync();
		// System > Credits: the people who made this version, from the image's /etc/banana/credits.txt ("[Section]",
		// then "Name = Title" lines; BANANA_CREDITS another file, the host's tests). Never the card's: they belong to the
		// build. No file, or nothing in it: no row
		struct Credit { std::string section, name, title; };
		std::vector<Credit> credits;
		int crSel = 0, crTop = 0;
		static std::vector<Credit> readCredits();
		void openCredits();
		Result pressCredits(Key k);
		void renderCredits();
		// the credits list's lines: a section's heading, then its names (index into credits; -1 - section for a heading)
		std::vector<int> crLines;
		// Display & sound > Theme, A: the themes as a gallery, each a picture of its home screen (the theme's own
		// "preview", else drawn by banana-ui itself, headless, once per theme file, kept in banana/themes/.thumbs)
		std::vector<std::string> thNames, thValues;
		int thSel = 0, thTop = 0;
		wifi::Job thumbJob;
		std::map<std::string, rnds::TexPtr> thumbTex;	// by file (with its time: a picture made again is read again)
		double thumbPoll = 0;
		void openThemes();
		Result pressThemes(Key k);
		Result touchThemes(float cx, float cy);
		void renderThemes();
		std::string thumbFile(const std::string& value, bool bottom) const;
		rnds::TexPtr thumb(const std::string& value, bool bottom);
		void startThumbs();
		// the game page (X on a DS game in the library): its emulator, this game only (banana/nds-per-game.conf, read by
		// run-nds: "<ROM file name> = superdrastic|bananads", no line = Settings > Nintendo DS > Emulator)
		// the game page (X on a DS game): its own emulator and shader, each "As in Settings" (index 0) or a choice
		std::string pgTitle, pgRom;
		int pgRow = 0, pgTop = 0, pgEmu = 0, pgShader = 0, pg3d = 0, pgMic = 0, pgRes = 0, pgTiming = 0, pgCpu = 0, pgPerf = 0;	// (pg3d, pgMic, pgRes, pgTiming, pgCpu: 0 as in Settings)
		bool pgNds = true;
		enum PgRow { PG_EMU, PG_SHADER, PG_3D, PG_MIC, PG_NAME, PG_SORT, PG_ART, PG_DELETE, PG_RES, PG_TIMING, PG_CPU, PG_PERF, PG_SCOPE,
			PG_RX = 100, PG_RX_END = 255 };	// (PG_RX + i: pgRx[i])
		// a ROCKNIX game (its system's run line through run-rocknix): its emulator and core and the options they have, as
		// usr/libexec/banana/rocknix-options lists them from ROCKNIX's es_systems.cfg and es_features.cfg, for this game
		// or (the For row) every game of its system; at is the choice shown, 0 the next level's ("As the system's")
		struct RxRow { std::string key, label, help; std::vector<std::string> names, values; int at = 0; };
		std::vector<RxRow> pgRx;
		std::string pgRxSystem;	// "" for a game that isn't ROCKNIX's
		bool pgRxWhole = false;	// the For row: the whole system's choices, not this game's
		void loadRx();
		void setRx(const RxRow& r);
		bool pgDelAll = false, pgDelArmed = false;	// Delete game: its saves, states and art too; A pressed once
		std::string pgPath;	// the game's file (Box art: banana-scrape --match)
		std::string pgScraped;	// the game's title without its own name (the page's name compares with it)
		bool pgArtDone = false;	// a cover fetched on the page: the library read again when it closes
		std::vector<PgRow> pgRows;
		std::vector<std::string> pgShaderNames, pgShaderValues;
		std::string gameEmulator(const std::string& rom);
		void setGameEmulator(const std::string& rom, const std::string& emu);
		std::string perGame(const std::string& file, const std::string& rom);
		void setPerGame(const std::string& file, const std::string& rom, const std::string& value);
		Result pressGame(Key k);
		Result touchGame(float cx, float cy);
		void renderGame();
		// the keyboard types a text for a purpose: a Wi-Fi password, or an account's name or password
		// (banana/accounts.conf: RetroAchievements for DraStic and RetroArch, ScreenScraper for the scraper to come)
		enum Entry { ENTRY_WIFI, ENTRY_RA_USER, ENTRY_RA_PASS, ENTRY_SS_USER, ENTRY_SS_PASS, ENTRY_SMB_USER, ENTRY_SMB_PASS, ENTRY_GAME_NAME, ENTRY_GAME_SORT, ENTRY_ART_SEARCH, ENTRY_DS_NAME, ENTRY_SEARCH, ENTRY_HS_ADDR, ENTRY_HS_CODE, ENTRY_BETA_PASS, ENTRY_FOLDER, ENTRY_RENAME, ENTRY_BSHARE_PIN, ENTRY_BSHARE_MYPIN, ENTRY_SERVER, ENTRY_STEAM_USER, ENTRY_STEAM_PASS, ENTRY_STEAM_CODE, ENTRY_SGDB_KEY } entry = ENTRY_WIFI;
		std::string entryTitle, entrySub;
		std::string entryHelp;		// what the keyboard is for, when the purpose says it itself (Files' names, Bshare's PINs)
		size_t entryMax = 63;		// the longest text (Wi-Fi's 63; a path or an address is longer)
		Page entryBack = NETWORKS;
		void startEntry(Entry e, const std::string& title, const std::string& sub, Page back, const std::string& start = "");
		Result finishEntry();
		std::string account(const char* key);
		std::string shareUser();
		static void popChar(std::string& t);
		std::string smbNewUser;	// the sharing name typed, kept with the password (both at once, or neither)
		std::string dsName();
		void setDsName(const std::string& name);
		std::string sharePassword();
		// the RetroAchievements account typed in the setup, checked with the server at once (on a thread: libcurl)
		struct RaCheck { std::mutex mu; bool done = false; int kind = 0; std::string l2; };	// kind: 1 ok, 2 refused, 3 no network
		std::shared_ptr<RaCheck> raCheck;
		void startRaCheck(const std::string& user, const std::string& pass, const std::string& token = "");
		std::string toast1, toast2;
		uint32_t toastAccent = 0;
		int toastIcon = 0;			// 0 "i", 1 a tick, 2 a cross
		double toastUntil = 0;
		void toast(const std::string& l1, const std::string& l2, uint32_t accent, int icon, double ms);
		void drawPopup();
		// the first-time setup (owner, 2026-10-27: "one thing at a time"): a slide per step, each a title, what it is
		// for, the result so far and its choices (bottom panel), B back and A on a choice, START past it. Run once after
		// the installation (banana/setup-done written at the end) and from Settings > System > First-time setup
		// (Home server, Steam and the handheld's controls only where their tool or hardware is: setupSlides())
		enum Slide { SL_LANG, SL_WIFI, SL_TZ, SL_NAME, SL_RA, SL_ART, SL_DONE, SL_HOME, SL_STEAM, SL_CONTROLS };
		enum SetupAct { SA_NEXT, SA_SKIP, SA_LANG, SA_NETWORKS, SA_TEST, SA_TZ, SA_NAME, SA_RA, SA_RACHECK, SA_ART, SA_FINISH, SA_WAIT,
			SA_HOME, SA_STEAM, SA_ITEM, SA_STEAMQR };
		struct Choice { std::string label, value; SetupAct act; std::string key = ""; };	// key: SA_ITEM's settings row
		std::vector<Slide> slides;
		std::vector<bool> wskipped;		// the slides passed with Skip (the summary says so)
		bool wizard = false, wizardBoot = false, setupEnded = false;
		int wsel = 0, wpick = 0;		// the slide, the choice picked on it
		int langAt = -1;				// the Language slide's choice (saved with Next: a change restarts the menu)
		// Wi-Fi's test (wifi test: connected with an address, the internet, the clock), its lines as they come in
		// runDir/wifi-test; -1 not known yet, 0 failed, 1 fine
		wifi::Job testJob;
		int tLink = -1, tNet = -1, tTime = -1;
		std::string tIp, tSsid;
		bool testing = false, testAfterJoin = false, autoTested = false, raAuto = false;
		bool statusKnown = false;		// the Wi-Fi status has come in since the setup opened
		double testPoll = 0;
		void startTest();
		bool pollTest();				// true when something to show changed
		// the RetroAchievements check as the slide shows it: 0 not checked, 1 logged in, 2 refused, 3 no answer
		int raKind = 0;
		std::string raLine;
		void openSetup(bool boot, const std::string& at);
		void setupGo(int slide);
		std::vector<Choice> setupChoices();
		Result setupAct(SetupAct a);
		Result pressSetup(Key k);
		void renderSetup();
		void setupDone();
		static const char* slideKey(Slide s);
		// the slides this handheld gets: the RG DS's six and the summary; Home server where banana-sync is and the board
		// isn't an RG DS, Steam where its tool is, Controls where the handheld tool has a fan or stick lights
		std::vector<Slide> setupSlides();
		std::string hhCaps;				// usr/libexec/banana/handheld caps (build(): "" none)
		std::vector<std::string> controlKeys() const;	// the Controls slide's rows (settings keys), those this handheld has
		// Steam (usr/libexec/banana/steam: steam status / login / code / cancel / logout / library). The login runs in the
		// background, its password on its stdin; its state is the one-line runDir/steam.state
		std::string steamTool;			// "" no Steam on this handheld
		pid_t steamPid = -1, steamLibPid = -1;
		std::string steamState;			// the state file's line as last read
		std::string steamUser;			// the account logged in ("" none), from steam status or the login's ok
		std::string steamAcct;			// the account being logged in
		bool steamReady = false;		// the downloader is installed (steam status's ready=yes)
		int steamKind = 0;				// 0 not tried, 1 logged in, 2 failed, 3 logging in
		std::string steamLine;			// the failure's reason
		std::string steamGames;			// the library's size ("" not known)
		double steamPoll = 0;
		bool steamCodeOpen = false;		// the Steam Guard code's entry is up for the state it asked in
		// the sign-in with the Steam app (steam login-qr): its code (runDir/steam.qr, a row of '1' and '0' a line, read
		// again whenever the state's "qr <n>" counts up), and the way the last login went (the QR or the password)
		bool steamQr = false, steamCancelled = false;
		int steamQrN = -1;
		std::vector<std::string> steamQrRows;
		int steamQrK = 0;				// device px a module, as last drawn (the tests' setupstate)
		void startSteamQr();
		void steamCancel();				// steam cancel: the login stops, back to the choices (no failure shown)
		bool steamQrShown() const { return steamPid > 0 && steamQr; }
		// the code on the top screen in the room the page leaves (x, y, w, h: CSS px of the canvas), the steps beside or
		// under it (from textX, textY)
		void drawSteamQr(float textX, float textY);
		void steamStatus();				// steam status, read now
		void startSteamLogin(const std::string& user, const std::string& pass);
		void steamRun(const std::string& args);	// steam <args> in the background, not waited for
		bool pollSteam();				// true: something to show changed
		std::string deviceName() const;	// "DS", or the handheld's own short name (the AYN Thor: "Thor")
		std::string wifiTool, wifiConf;
		bool sync = false;
		wifi::Job scanJob, statusJob;
		wifi::Job volJob;			// the Volume row's level (the volume script), read as Settings opens without waiting on it
		bool volPending = false;	// its answer still to be shown (a press on the row first: the press's level stays)
		// the game page's Box art (#28): the names banana-scrape --search found for the words typed, one picked, and
		// banana-scrape --match fetching its pictures
		wifi::Job artJob;
		std::vector<std::string> artNames;
		int asel = 0, atop = 0;
		bool artMatching = false;
		std::string artWords;
		void saveGameRows();
		bool pgChanged = false;	// a DS row changed (and kept) since the game page opened
		Result pressArt(Key k);
		Result touchArt(float cx, float cy);
		void renderArt();
		std::string scrapeTool() const;
		// Screen colours (#N3): the values being tried (drawn in software over the test pattern of the screen they
		// correct; the hardware table is off while the page is up) and the pattern shown
		ScreenColors colEdit, colSaved;
		int colRow = 0, colPattern = 0;
		void openColors();
		Result pressColors(Key k);
		Result touchColors(float cx, float cy);
		void renderColors();
		void colorsPattern(int screen, float y0, float h);
		// Speaker EQ: what is being tried (playing already: the plugin follows the file) and what was kept; the
		// profiles Profile offers, read when the page opens
		speakereq::State eqEdit, eqSaved;
		std::vector<speakereq::Choice> eqChoices;
		int eqRow = 0;
		std::string eqText;				// what the plugin was given last (the curve drawn is its)
		std::vector<double> eqCurve, eqProfCurve;	// eqText's curve and the profile's own, at the graph's frequencies (dB)
		// what each slider shows: the curve's level at its frequency (the profile with the changes made to it), and the
		// profile's own there (the tick on its track)
		double eqBand[speakereq::BANDS] = {}, eqBandProfile[speakereq::BANDS] = {};
		std::string eqShare() const { return speakereq::shareDir(assets); }	// (/usr/share/banana/speaker-eq)
		void openEq();
		void eqLive(const speakereq::State& s);
		Result pressEq(Key k);
		Result touchEq(float cx, float cy);
		void renderEq();
		std::vector<wifi::Network> nets;
		wifi::Status wstat;
		bool scanning = false, scannedOnce = false, wifiEnabled = true;
		int nsel = 0, ntop = 0;
		std::string joining, message;	// the network being joined; the last thing to tell
		std::string failed;				// the last network that couldn't be joined: A on it asks for the password again
		double joinStart = 0, nextStatus = 0, nextScan = 0, now = 0;
		wifi::Network target;			// the network the keyboard is for
		std::string pw;
		int kcol = 0, krow = 1, layer = 0;	// the keyboard's selection (column 0..9, row 0..4), lower / upper / symbols

		// the updates page: the packages in banana/updates on the card, one being applied (settings-update)
		struct Pkg { std::string path, version, board; };
		std::vector<Pkg> pkgs;
		int usel = 0;
		std::string ustatus;			// the updater's status line ("writing 42%", "done 0.2.0", "failed: ...")
		bool applying = false, updated = false;
		// the install's own screen: from A on a package (or Install) until the result is read (QA: a single line on
		// the top panel was all that showed the install, while the package list stayed on the bottom one)
		bool installView = false;
		std::string installVersion;
		void renderInstall();
		double nextStatus2 = 0;
		std::string slot, version, board;
		void openUpdates();
		void listPkgs();
		// the owner's request (2026-10-27): old update packages can go from the card. Y on a package deletes it, X removes
		// every package not newer than the version running now (each after a second press); only banana/updates' own
		// .update.tar files, never while a slot is being written or a download runs
		bool pkgOld(const Pkg& k) const;			// not newer than the running version (the same one included)
		int oldPkgs(long long* bytes) const;		// how many of those, and their size
		bool deletePkg(int i);
		int deleteOldPkgs();
		void pollUpdate();
		void startApply(int pkg);
		Result pressUpdates(Key k);
		void renderUpdates();
		// over the air (#69): the update script's check (the newest release on GitHub for this board, its answer kept
		// in banana/updates/.check) and its download (a process of its own, which goes on when the page is left: its
		// line in /run/banana/update-download, polled). The page's first row is the online one, the packages follow.
		struct Online
		{
			std::string state, lastState, current, latest, file, notes, error, board, preSetting, channel;
			bool pre = false;
			long long size = 0, checked = 0;
		};
		Online online;
		wifi::Job checkJob;
		std::string dlLine;				// "downloading <bytes> <total>", "verifying", "ready <package>", "failed: ..."
		bool downloading = false;
		double dlStarted = 0, nextDl = 0;
		int utop = 0;					// the page's first row shown
		std::string updateTool, runDir, updatesDir;
		void readOnline();
		void pollDownload();
		void startCheck();
		bool checkDone();				// the check's answer taken (true when there was one)
		void startDownload();
		bool onlineNewer() const;		// the last check found a newer version for this board
		int onlinePkg() const;			// that version's package among pkgs (downloaded), or -1
		long long partialBytes() const;	// how much of it an unfinished download left in banana/updates/.partial
		std::string onlineRow(std::string& tag, std::string& act) const;
		Result pressOnline();
		// the beta channel (the page's second row): test builds from the BananaOS site's password-protected folder
		// instead of GitHub. The password typed once, checked with the site by the update script (beta-login, the
		// password on its stdin: never on a command line), kept on the card for the script alone (banana/updates/.beta)
		wifi::Job betaJob;
		bool betaOn() const;
		bool betaKept() const;					// the beta password on the card (on or only turned off)
		std::string betaRun(const char* what);	// update beta-on / beta-off / beta-forget			// the password is there: check and download ask the beta server
		bool betaDone();				// the password check's answer taken
		Result pressBeta();
	public:
		std::string updateState() const;	// the page as text (the headless test's "upstate")
		void typeText(const std::string& t) { if (page == KEYBOARD) pw += t; }	// (the headless test: text typed at once)
	private:

		// Bluetooth (Settings > Network > Bluetooth > Bluetooth devices): the devices known and found, paired, connected, through
		// the bluetooth script (BANANA_BLUETOOTH: the test's stand-in)
		std::string btTool;
		wifi::Job btListJob, btScanJob, btActJob;
		std::vector<bt::Device> btDevs;
		int bsel = 0, btop = 0;
		bool btScanning = false, btScanned = false, btEnabled = false;
		std::string btBusy, btBusyName, btVerb;	// the device a pair / connect / ... is running for, its name, the verb
		double btNextList = 0;
		bool btOn() const;
		bool btUpdate();	// (update's part: the jobs' answers) true when something changed
		void btSetOn(bool on);
		void openBluetooth();
		void btScan();
		void btAct(const char* verb, const bt::Device& d);
		Result pressBluetooth(Key k);
		Result touchBluetooth(float cx, float cy);
		void renderBluetooth();
		std::string btRow() const;	// the Bluetooth devices row: what is connected

		bool wifiOn() const;
		void openNetworks();
		void startScan();
		void pollStatus();
		void markNetworks();
		void join(const wifi::Network& n, const std::string& password);
		Result pressNetworks(Key k);
		Result pressKeyboard(Key k);
		Result touchNetworks(float cx, float cy);
		Result touchKeyboard(float cx, float cy);
		Result keyboardKey(int row, int col);
		void renderNetworks();
		void renderKeyboard();
		void bars(int n, float x, float y, const rnds::Color& on, const rnds::Color& off);
		void lock(float x, float y, const rnds::Color& c);
		// (upper, ls: the style the text is drawn in, as text() takes them: measured otherwise, a label in capitals and
		// letter-spaced ran past its width)
		std::string fit(const std::string& t, bool medium, float size, float width, bool upper = false, float ls = 0);
		std::string fitEnd(const std::string& t, bool medium, float size, float width, bool upper = false, float ls = 0);	// "..." and the end that fits

		std::shared_ptr<rnds::Font> R, M;
		std::string fontDir;		// Inter's TTFs (the Carts theme)
		bool cleanFonts = false;
		std::shared_ptr<rnds::Font> B;	// the help texts' font (a theme's settings.bodyFont), else R
		std::string bodyLoaded;
		bool bodyNow = false;			// wrapped() is drawing: text() takes B
		void loadBody();				// B as the theme says (again when it changed)
		// Thor OLED's look and layout (settings_v3.cpp): a theme's "panels": "material" and "settings.layout": "screen"
		std::shared_ptr<rnds::Font> IM, IR;	// Inter Medium and Regular (labels and values in the material look)
		bool material() const { return rnds::themeFiles.panels == "material" && IM && IR; }
		bool lay() const { return shift.on(); }
		void role(bool medium, float& size, std::shared_ptr<rnds::Font>& font, bool& upper, float& ls) const;	// a text's font and size by its role
		bool mbox(const std::string& kind, bool on, float x, float y, float w, float h);	// a box in the material look (false: not one of its)
		void mpaint(const std::string& style, float x, float y, float w, float h);	// one of the look's surfaces by name (canvas units)
		void glyph(const std::string& name, int size, float x, float y, const rnds::Color& c);	// a line icon (design px size: 32, 88), tinted
		bool hasGlyph(const std::string& name, int size);
		float keycapWidth(const std::string& key);	// a button hint's key in the shape of the button (units)
		void keycap(const std::string& key, float x, float y, int state);	// state: 0 plain, 1 the main action's (plate), 2 a warning's (pink)
		void mswitch(float x, float y, bool on);	// a switch (88 x 48 design px)
		void mbar(float x, float y, float w, double fraction);	// a 12 px bar, its fill the plate
		rnds::Color mcol(const char* name) const;	// one of the look's colours (the theme's "colors", else Thor OLED's)
		// the groups' tiles and the list's rows, where they are (canvas units): the canvas's, or the design's when the
		// pages are laid out on the screen (after the bottom screen was drawn once)
		struct Measures { float rowY = 82, rowH = 56, rowPitch = 64, gridY = 82, gridW = 196, gridH = 158, gridPX = 204, gridPY = 166; };
		Measures measures() const;
		int listRows() const { return rnds::themeFiles.panels == "material" && shift.lay ? 7 : ROWS; }
		// a Settings row in the material look: an icon tile, the label, the value (a switch for on and off, a meter
		// for a level, the accent's dot), chevrons either side of it on the picked row
		void listRow(const Item& it, float y, float h, bool on, const std::string& value, bool arrows, bool armedNow);
		std::string rowGlyph(const Item& it) const;
		int iconPx = 0;		// (iconCentred: screen px an icon pixel, when a page says)
		float crumbEnd = 0;		// (the top screen's header: where the crumb ended, for the value after it)
		float px(float v) const { return v * shift.px() / (s * shift.rk()); }	// design px (of 1080 lines) in the page's units
		void iconTile(float x, float y, float w, float h);	// a theme's lit tile behind a top screen icon (settings.icons "tile")
		std::string fontsLoaded;	// the fonts R and M are (setVariant): a user theme's, or "carts"
		std::string drawnLook;	// what the cache's pictures were drawn for (setVariant): the same look again keeps them
		// what the pages draw, by what it is (a box's kind and size, a line's text and style, an icon): its texture, and a
		// line's width. A line only measured (a value's width, a name too long for its row, which fit() cuts) is laid out,
		// not drawn: its bitmap is made when something draws it. After each frame (render(), gameRender()) the least
		// recently used go while the cache holds more than its budget (evict): Files lists any number of names
		struct Cached { rnds::TexPtr tex; int ox = 0, oy = 0; float w = 0; bool drawn = false; size_t bytes = 0; uint64_t used = 0; };
		std::map<std::string, Cached> cache;
		uint64_t frame = 0;					// render()'s count: an entry's used is the last frame that drew or measured it
		size_t cacheBytes = 0, budget = 0;	// what the entries hold; CACHE_1X at this scale
		std::map<std::string, Cached>::iterator put(const std::string& key, Cached c, const rnds::Bitmap& b);
		Cached& keep(std::map<std::string, Cached>::iterator it) { it->second.used = frame; return it->second; }
		void evict();
		void renderPage();	// render()'s frame: the page open
		void gameFrame();	// gameRender()'s frame: the game panel, a companion's view or the tiles

		void build();
		void load(Item& it);
		Result change(int dir);
		// the language switch asked first (owner: "Are you sure?"), in the language now and the one picked, No first:
		// langAsk the picked choice's index on the Language row (-1: not asking), langYes the button on
		int langAsk = -1;
		bool langYes = false;
		// the language list (2026-11-04, owner: "a list instead of left or right"): the Language row and the setup's
		// language slide open it; UP/DOWN pick, A takes it (Settings: then asked first, as before; the setup: the slide's
		// pick), B leaves it. langPick: the row picked, -1 closed
		int langPick = -1, langTop = 0;
		bool langPickSetup = false;
		std::vector<lang::Info> langList;
		void openLangPick(bool setup);
		Result pressLangPick(Key k);
		void renderLangPick();
		void loadCjkFont();
		bool rawText = false;	// text() draws the string as it is (not through the menu's language): the other language's
		// while one is alive, text() and fit() draw names as they are: a game's, a file's, a network's, a device's
		struct AsNamed
		{
			Settings& s; bool was;
			explicit AsNamed(Settings& x, bool on = true) : s(x), was(x.rawText) { s.rawText = was || on; }
			~AsNamed() { s.rawText = was; }
			AsNamed(const AsNamed&) = delete;
			AsNamed& operator=(const AsNamed&) = delete;
		};
		bool cjkLoaded = false;
		Result pressLangAsk(Key k);
		void renderLangAsk();
		// the same question's look for 4x (owner: it runs hot and drains the battery): asked before a DS 3D resolution
		// is set to 4x, the Settings row (1) or a game's own (2); hotAt the value it would get, hotOld the one kept
		int hotAsk = 0, hotAt = 0;
		bool hotYes = false;
		std::string hotOld;
		Result pressHotAsk(Key k);
		void renderHotAsk();
		// the questions' bottom panel: a line, then No and Yes (each its words in the menu's language, and a second line
		// under it when there is one), B Back and A OK; and a tap on it: -1 nothing, 0 the other button picked, 1 OK
		void askButtons(const std::string& title, const std::string& no1, const std::string& no2, const std::string& yes1,
			const std::string& yes2, bool yes);
		int askTouch(float cx, float cy, bool& yes);
		Result resetApply(bool emulator);	// the Reset rows: schema defaults written, live bits applied, rows reloaded
		void save(const Item& it);
		void refreshInfo();

		// drawing, in CSS px of a 640x480 panel
		float X(float v) const { return v * s; }
		// motion: the list's highlight and scroll ease toward sel and top (in rows), the groups' highlight toward gsel
		// (column and row); advanced at each render by the time since the last one
		float aPick = -1, aTop = -1, aCol = -1, aRow = -1;
		int aGroup = -1;
		double aLast = 0;
		bool aMoving = false;
		void advance();
		// box art: banana-scrape runs on its own and says how far it got in /run/banana/scrape.status
		std::string scrapeLine;				// that file's line, as last read
		double scrapeNext = 0;
		bool libChanged = false;
		bool scraping() const { return scrapeLine.compare(0, 8, "running ") == 0; }
		bool pollScrape(double nowMs);		// true when the line changed
		void startScrape();
		std::string scrapeShort() const;	// for a row: "Not yet", "120 / 530", "410 of 530 found", "No connection"
		// home server sync (Network > Home server): banana-sync runs on its own and says how far it got in
		// /run/banana/sync.status; the pairing is in banana/romsync.json
		std::string syncLine;				// that file's line, as last read
		double syncNext = 0;
		std::string hsUrl, hsName, hsKind;	// the server being paired with
		int hsStep = 0;						// pairing: 0 not, 1 looking on the LAN, 2 asking an address, 3 banana-sync pair running
		wifi::Job hsJob;
		bool syncing() const { return syncLine.compare(0, 8, "running ") == 0; }
		bool pollSync(double t);			// true when something to show changed
		void startSync();
		void startPairing();
		void hsRun(const std::string& args);	// banana-sync <args> in the background
		std::string syncShort() const;		// for the Sync now row: "Not yet", "12 / 300", "Done", "Didn't work"
		std::string homeServer() const;		// the paired server's name, "" when none
		mutable std::string hsPaired;
		mutable double hsPairedAt = -1e18;
		void syncBar(float x, float y, float w);
		std::string syncTool() const;
		void scrapeBar(float x, float y, float w);	// the progress bar and the game being fetched, while it runs
		struct RaWatch;							// the achievements: RetroArch's log and the server (settings.cpp)
		std::shared_ptr<RaWatch> raw;
		// Files (settings_files.cpp): its pages' state, the job copying, moving or deleting (fileops.h)
		struct Files;
		std::shared_ptr<Files> fm;
		std::string systemsConf = "/etc/banana/systems.conf", romRoot = "/data/roms";
		Result pressFiles(Key k);
		Result touchFiles(float cx, float cy);
		void renderFiles();
		bool updateFiles(double nowMs);
		int filesWake(double nowMs) const;
		Result finishFilesEntry();
		void sd2Scan();	// the sd2 script again (Files: a roms folder made on the second card is offered to the library)
		// Files and its servers' pages (settings_servers.cpp), each other's ways in. Files lists the servers beside the
		// cards and chooses a folder by its own navigation; a server's files copied wait in Files' clipboard
		struct RemoteClip
		{
			struct Item { std::string name; bool dir = false, link = false; int64_t size = -1; };
			std::string server, title, dir;	// the server's id and name, the folder the items are in
			std::vector<Item> items;
		};
		struct ServerInfo { std::string id, title, protocol, address; bool plainFtp = false; };
		void filesShow(const std::string& dir, const std::string& focus, bool actions);	// Files on a folder (its actions open)
		void filesRoot(const std::string& focus, const std::string& message);	// Files' list: "job", "server:<id>"; its settings: "add", "options"
		void pickFolder(int purpose, const std::string& start, const std::string& what);	// 1 a download's folder, 2 an unpacking's
		void setRemoteClip(const RemoteClip& c);
		// Files' servers (settings_servers.cpp): a server's folders, a download's or an unpacking's page, the job in the
		// background, a server's editor, the options; banana-fetch does the network and the unpacking (nas.h)
		struct Servers;
		std::shared_ptr<Servers> srv;
		Servers& servers();
		void serversRefresh();			// the servers' list and the job read again (Files opened)
		std::vector<ServerInfo> serverList();
		// the job in the background, for Files' list: false when there is none
		bool fetchJobRow(std::string& label, std::string& value, std::string& title, std::string& state, bool& warn);
		std::set<std::string> fetchParts();	// the .part files the job at work still writes (Files leaves them alone)
		void openServer(const std::string& id);
		void editServer(const std::string& id);	// "" a new one
		void openServerOptions();
		void openFetchJob();
		std::string pasteDownload(const RemoteClip& c, const std::string& dir);	// "" when its page opened, else why not
		void unpackArchives(const std::vector<std::string>& archives, const std::string& dir);
		void folderPicked(const std::string& dir);
		void pickCancelled();
		bool serversCardsChanged();
		Result pressServers(Key k);
		Result touchServers(float cx, float cy);
		void renderServers();
		bool updateServers(double nowMs);
		int serversWake(double nowMs) const;
		Result finishServersEntry();
		// Bshare (settings_bshare.cpp): its pages, reading banana-bshare's files in /run/banana
		struct Bshare;
		std::shared_ptr<Bshare> bs;
		Result pressBshare(Key k);
		Result touchBshare(float cx, float cy);
		void renderBshare();
		bool updateBshare(double nowMs);
		int bshareWake(double nowMs) const;
		Result finishBshareEntry();
		void closeBshare();
		void tick(float x, float y, const rnds::Color& c);	// a mark on a 2 px grid (a chosen file)
		void meter(float x, float y, float w, double fraction);	// a progress bar
		std::string gTitle, gSystem;
		int gScreen = 1, gFlash = -1, gDown = -1;
		// the States view (universal companion tier 0): slots 0-9 with their thumbnails; a slot tapped twice loads it
		bool gStates = false;
		int gSlotSel = -1, gSlotPick = -1;
		std::string gStateBase;			// <state dir>/<the game's name> (the dir found at each scan)
		std::string gStateRoot, gStateName;	// /data/states, the game's name
		std::string gRomFile;			// the game's file name (gameRom): its per-game settings' key
		// the quick menu's rows: a settings row (its index in items) or the game's performance mode (-1)
		std::vector<int> qRows;
		int qSel = 0, qPerf = 0;
		float qY = 76, qPitch = 49, qChipY = 422;	// where renderQuick put the rows and the chips (the whole screen's height)
		Result pressQuick(Key k);
		Result touchQuick(float cx, float cy);
		Result quickStep(int row, int dir, int to = -1);	// a row one step either way, or a slider to step to
		void renderQuick();
		struct StateFile { long long mtime = 0; rnds::TexPtr tex; };
		StateFile gStateFiles[10];
		double gStatesScan = 0;
		void gameStatesScan();
		void gameStatesFrame(const SPal& p);
		bool gTouch = true;
		// the panel's on-screen keyboard (a computer's game: the C64's LOAD"*",8,1 with a finger on the bottom screen).
		// vkbd is the emulator's own keyboard (vkbd.h), made before the game started and gone after it; the Keys tile
		// opens the pages, a tap types the key into the game
		VirtualKeyboard* vkbd = nullptr;
		bool gKeysOpen = false;
		int gKeyPage = 0, gKeyShift = 0;
		std::string gKeyTyped;
		void gameKeysFrame(const SPal& p);
		void gameKeyTap(int code, bool shift, const char* label);
		double gFlashUntil = 0;
		int gSeen = -1;						// the watch's generation last drawn
		int awardsSeen = 0; bool wake = false;	// (gameWake)
		std::map<std::string, rnds::TexPtr> gBadges;	// badge textures by file
		gba::Panel gbaPanel;		// the GBA Pokémon companion's pages
		// the companion's switch (either companion): the icon on the menu's header, the button that switches
		std::vector<int> gCompKeys;	// (the buttons whose job is Companion: evdev codes, read at gameOpen)
		rnds::TexPtr gIconTex;
		int gIconBadge = -1;
		bool compOn() const;
		bool compCorner(float lx, float ly) const;	// (CSS px: the top-right corner, the icon's and Menu's place)
		void compIcon();
		void compToggle();
		void compMenu();
		void pager(const SPal& p, int page, int pages, const std::string& left);
		// a pager's back and on buttons, side by side (w x h each, gap between): drawn, greyed at the first and the
		// last page; a tap on them: -1 back, 1 on, 0 neither (each button's whole box and a little around it)
		struct PagerButtons { float x, y, w, h, gap; };
		static const PagerButtons PAGER;	// (the game companion's pages)
		void pagerButtons(const SPal& p, int page, int pages, const PagerButtons& b);
		int pagerStep(float lx, float ly, const PagerButtons& b) const;
		// the game companion's views: a page (gcomp::PageId) or GC_GAME (the panel's tiles); within a page, its screen
		// (a page longer than the panel is paged); a list's row opened (its fields in full); where the tabs and the
		// rows were drawn (for touch)
		enum { GC_GAME = gcomp::NUM_PAGES, GC_TABS = gcomp::NUM_PAGES + 1 };	// (GC_GAME: the menu)
		int gcView = 0, gcScreen = 0, gcScreens = 1, gcList = -1, gcRow = -1, gcLast = 0;	// (gcLast: the page the menu left)
		bool gcFightAtMenu = false;	// (a fight was on when the menu was shown: back to the page left, not Battle)
		bool gcBattle = false;
		float gcTabX[GC_TABS] = {}, gcTabW[GC_TABS] = {};
		struct GcHit { float y0, y1; int list, row; };
		std::vector<GcHit> gcHits;
		struct GcBlock { const gcomp::Widget* w; int part; float h; };	// a widget, or one of its rows/paragraphs
		void gcRender(const SPal& p);
		void gcTabs(const SPal& p);
		void gcMenuButton(const SPal& p);
		void gcRowView(const SPal& p);
		std::vector<GcBlock> gcBlocks(const gcomp::Page& pg, float width);
		void gcBlock(const SPal& p, const GcBlock& b, float x, float y, float w);
		std::string gcValue(const gcomp::Val& v, const std::string& enumName, const std::string& format) const;
		int wrapLines(const std::string& t, float size, float width);
		float num(const std::string& t, float size, float x, float top, const rnds::Color& c, bool measureOnly = false);
		float chip(const std::string& label, uint32_t rgb, float x, float y, bool measureOnly = false);
		void hpBar(int hp, int maxHp, float x, float y, float w, uint32_t rgb = 0);
		void sprite(const char* const* rows, int h, float x, float y, int k, const std::function<rnds::Color(char)>& colour);
		void box(const std::string& kind, bool on, float x, float y, float w, float h);
		float text(const std::string& t, bool medium, float size, float x, float top, const rnds::Color& c, bool upper = false, float ls = 0, bool measureOnly = false);
		// text()'s line in a font given (tag: the font's mark in the cache's key)
		float textWith(const std::string& t, const std::shared_ptr<rnds::Font>& font, const char* tag, float size, float x, float top, const rnds::Color& c, bool upper = false, float ls = 0, bool measureOnly = false);
		float headText(const std::string& t, float size, const rnds::Color& c);	// (the material look's top screen: crumb, title, value)
		void wrapped(const std::string& t, float size, float x, float top, float width, int maxLines, const rnds::Color& c);
		// text in a box of `width`: as it is when it fits, else cut with "...", or scrolling while picked (a marquee);
		// right: against the box's right edge
		float textFit(const std::string& t, bool medium, float size, float x, float top, float width, const rnds::Color& c, bool picked, bool right = false);
		void marqueeFrame();
		static constexpr float FACE_MAX = 196;	// a button chip's widest (the bottom bar's B / Y / A)	// (each render: the marquees drawn this frame start counting)
		std::map<std::string, std::pair<double, long>> mqSince;	// a marquee's text -> when it was first drawn picked, the frame last drawn
		long mqFrame = 0;
		bool mqMoving = false;
		double mqWake = -1;
		// the top panel's header and help text (Settings' rows and a game's page): where they go, in 640x480 CSS px
		static constexpr float HEAD_ICON_Y = 42, HEAD_SECTION_Y = 46, HEAD_LABEL_Y = 70, HEAD_VALUE_Y = 110;
		static constexpr float HELP_X = 50, HELP_W = 540, HELP_TOP = 152, HELP_BOTTOM = 438;
		static constexpr float LANG_Y = 80, LANG_H = 52, LANG_PITCH = 58;	// the language list's rows (bottom panel)
		static constexpr int LANG_VIS = 5;
		static constexpr float HINT_TOP = 300;	// a choice's hint, under the row's sentence
		bool helpFit(const std::string& t, float top, float bottom, float& size, int& lines) const;
		void helpText(const std::string& t, float top, float bottom, const rnds::Color& c);
		std::string helpOf(int item);
		std::string gameHelpOf(int row);
		void icon(const std::string& name, float x, float y, int scale);
		void iconCentred(const std::string& name, float cx, float y, int scale);
		void arrow(bool right, float x, float y, const rnds::Color& c);
		void face(const std::string& key, const std::string& label, float x, float w, float y = 422);
		float faceWidth(const std::string& key, const std::string& label, float most = FACE_MAX);
		// a page's buttons along the bottom panel: B and the page's others from the left, its main one (right) at the
		// right. Drawn and tapped from the same list, so a tap presses the chip it is on (or the nearest one).
		struct Chip { Key key; std::string face, label; bool right = false; };
		void drawChips(const std::vector<Chip>& cs);
		Key chipAt(const std::vector<Chip>& cs, float x);	// Key::NONE with no chips
		std::vector<float> chipXs(const std::vector<Chip>& cs, std::vector<float>& ws);
		std::vector<Chip> keyboardChips();
		static constexpr int ROWS = 5;
		static constexpr float SETUP_Y = 82, SETUP_PITCH = 66, SETUP_H = 58;	// the setup's choices on the bottom panel
	};

	// key = value files: the line for key changed (or added), the rest kept; written beside and renamed over
	void setConfValue(const std::string& path, const std::string& key, const std::string& value, const char* sep = " = ");
	std::string getConfValue(const std::string& path, const std::string& key, const std::string& def);
}
