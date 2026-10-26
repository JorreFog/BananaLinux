#pragma once
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
#include "../rnds/RndsUI.h"
#include "input.h"
#include "wifi.h"
#include "bluetooth.h"
#include "colors.h"
#include "pokemon.h"
#include "gamecomp.h"
#include <map>
#include <memory>
#include <mutex>
#include <functional>
#include <string>
#include <vector>

namespace banana
{
	struct SPal;	// (settings.cpp: the palette)
	class Settings
	{
	public:
		// every Settings row's help and every game page row's (as the panel would lay it out now: the language's too)
		// that doesn't fit its box at the smallest size, one line each to log; and how many were checked (ui/tests)
		int helpCheck(const std::function<void(const std::string&)>& log);
		Settings(rnds::Backend* be, const std::string& assets, const std::string& dataDir, float scale, const std::string& variant);
		void setVariant(const std::string& variant);	// "dark", "light", or "carts" (dark, in Inter: setFontDir first)
		void setFontDir(const std::string& dir) { fontDir = dir; }
		void open();
		void openPower();	// the Power list (Sleep, Restart, Shut down): Y on the groups
		void openSection(const std::string& section);	// a list with no tile (Shown systems)
		// the systems for Shown systems (id, name): settings.conf's shelf.<id>; a change there is SHELVES
		void setSystems(const std::vector<std::pair<std::string, std::string>>& systems);
		std::vector<std::pair<std::string, std::string>> shelfSystems;
		void addShelfRows();
		void close() { mOpen = false; page = LIST; wizard = false; }
		void setup() { mOpen = true; openSetup(); }	// the first-time setup, from the host
		bool isOpen() const { return mOpen; }
		std::vector<std::string> deletedFiles;	// what the last Delete game removed (DELETED)
		int gridCount() const;	// the groups with a tile (Power has none)
		int powerGroup() const;	// -1: none
		bool animating() const;	// the highlight or the list still easing into place: frames wanted

		// The game panel (settings.cpp, end): while a RetroArch game runs on one panel through a DRM lease, the other
		// shows the game's RetroAchievements (progress and the latest unlocks with their badges) and touch tiles for
		// RetroArch (save, load, its menu, fast forward, quit), in the in-game menu's look.
		enum GameAction { G_NONE, G_SAVE, G_LOAD, G_MENU, G_FFWD, G_QUIT };
		void gameOpen(const std::string& title, const std::string& system, int screen, bool touch);
		void gameClose();
		bool gameUpdate(double nowMs);		// true: the panel changed (a log line, a reply, a badge, a flash ending)
		// true once after something the player should see came up on the panel (an achievement unlocked): it wakes a
		// dimmed bottom panel, as a touch does (gameplay buttons don't)
		bool gameWake() { bool w = wake; wake = false; return w; }
		void gameRender();
		GameAction gameTouch(float x, float y, bool down);	// device px on the panel's screen
		bool gameProgress(int& unlocked, int& total);			// the game's achievements, once the server said
		// the Pokémon companion (pokemon.h), when the host runs one for this game: the party, the battle and the
		// achievements as tabs in the panel's middle box (set before gameOpen, null after gameClose)
		pkmn::Companion* companion = nullptr;
		// the game companion (gamecomp.h) for another GBA game with a definition: its seven pages as tabs, the same way
		// (set before gameOpen, null after gameClose; never both)
		gcomp::Engine* gameComp = nullptr;
		// (tests) every page of the game companion's game drawn once, its values as read so far: a line for each page
		// that needs more than one screen, a tab that falls back to the page's kind, a short reference page
		int gcAudit(const std::function<void(const std::string&)>& log);
		void gcShow(int view) { gcView = view; gcScreen = 0; gcList = gcRow = -1; if (gameComp) gameComp->want(view >= gcomp::NUM_PAGES ? -1 : view); }	// (tests)
		// box art (banana-scrape) finished since the last call: the library is read again
		bool takeLibraryChanged() { bool r = libChanged; libChanged = false; return r; }

		// what the host has to do after an input
		enum Result { NONE, MOVED, CHANGED, CLOSED, THEME, REBOOT, POWEROFF, SEARCH, TITLES, LANGUAGE, DELETED, SHELVES };
		// DELETED: the game page's Delete game removed the ROM (deletedFiles says what went): the library read again
		// SELECT in the menu: the keyboard alone, for a search of the games; SEARCH when it is done, with the text in
		// searchText (empty: the search taken away)
		void openSearch(const std::string& start);
		// X on a DS game in the library: which emulator runs this one game (title shown, ROM file name as the key)
		// X on a game: its own page. A DS game's emulator and shader, and every game's name and sort title (#29):
		// TITLES when one of those was typed, with them in gameName / gameSort for gameKey ("" each: as scraped)
		void openGame(const std::string& title, const std::string& romName, bool nds = true,
			const std::string& key = "", const std::string& name = "", const std::string& sort = "", const std::string& path = "",
			const std::string& scraped = "");
		std::string gameKey, gameName, gameSort;
		// the display's colour table (main: Display::setColors), for Screen colours: off while its page is up, the saved
		// values when it closes
		std::function<std::string(const ScreenColors&)> setColors;	// "" when it took, else why not
		bool colorsOpen() const { return mOpen && page == COLORS; }
		std::string searchText;
		std::function<int(const std::string&)> searchCount;	// the games the text typed so far finds (the host's library)
		Result press(Key k);
		Result touch(float x, float y, bool down);	// bottom panel, device px
		void render();
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
			std::string file, key;					// where it is kept ("" for the special ones)
			std::vector<std::string> names, values;	// shown, stored
			int at = 0;
			enum Kind { CHOICE, BRIGHTNESS, VOLUME, INFO, ACTION, PAGE } kind = CHOICE;
		};
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
		int gsel = 0;
		bool inGroups = true;
		int groupOf(int item) const;			// the group an item is in
		int groupStart(int g) const, groupEnd(int g) const;	// its items: [start, end)
		std::string groupHint(int g) const;		// what is in it, for its tile
		Result pressGroups(Key k);
		void renderGroups();
		bool downOnRow = false;
		float downX = 0, downY = 0;
		std::string about, wifiInfo, ip;

		// the Wi-Fi pages: the networks around, and the keyboard for a password (settings-wifi.cpp)
		enum Page { LIST, NETWORKS, KEYBOARD, UPDATES, SETUP, GAME, ARTPICK, COLORS, BLUETOOTH, THEMES, CREDITS } page = LIST;
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
		int pgRow = 0, pgTop = 0, pgEmu = 0, pgShader = 0, pg3d = 0, pgMic = 0, pgRes = 0;	// (pg3d, pgMic, pgRes: 0 as in Settings)
		bool pgNds = true;
		enum PgRow { PG_EMU, PG_SHADER, PG_3D, PG_MIC, PG_NAME, PG_SORT, PG_ART, PG_DELETE, PG_RES };
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
		enum Entry { ENTRY_WIFI, ENTRY_RA_USER, ENTRY_RA_PASS, ENTRY_SS_USER, ENTRY_SS_PASS, ENTRY_SMB_USER, ENTRY_SMB_PASS, ENTRY_GAME_NAME, ENTRY_GAME_SORT, ENTRY_ART_SEARCH, ENTRY_DS_NAME, ENTRY_SEARCH, ENTRY_HS_ADDR, ENTRY_HS_CODE, ENTRY_BETA_PASS } entry = ENTRY_WIFI;
		std::string entryTitle, entrySub;
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
		void startRaCheck(const std::string& user, const std::string& pass);
		std::string toast1, toast2;
		uint32_t toastAccent = 0;
		int toastIcon = 0;			// 0 "i", 1 a tick, 2 a cross
		double toastUntil = 0;
		void toast(const std::string& l1, const std::string& l2, uint32_t accent, int icon, double ms);
		void drawPopup();
		// the first-time setup: the steps (Wi-Fi, the time zone, RetroAchievements, ScreenScraper), run once after
		// the installation (banana/setup-done is written at the end) and from Settings > System
		bool wizard = false;
		int wsel = 0;
		bool tzAsked = false;	// the setup's Finish has pointed at a time zone left at UTC once
		void openSetup();
		Result pressSetup(Key k);
		void renderSetup();
		void setupDone();
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
		bool betaOn() const;			// the password is there: check and download ask the beta server
		bool betaDone();				// the password check's answer taken
		Result pressBeta();
	public:
		std::string updateState() const;	// the page as text (the headless test's "upstate")
		void typeText(const std::string& t) { if (page == KEYBOARD) pw += t; }	// (the headless test: text typed at once)
	private:

		// Bluetooth (Settings > Network > Bluetooth devices): the devices known and found, paired, connected, through
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
		std::string fit(const std::string& t, bool medium, float size, float width);

		std::shared_ptr<rnds::Font> R, M;
		std::string fontDir;		// Inter's TTFs (the Carts theme)
		bool cleanFonts = false;
		std::string fontsLoaded;	// the fonts R and M are (setVariant): a user theme's, or "carts"
		std::string drawnLook;	// what the cache's pictures were drawn for (setVariant): the same look again keeps them
		struct Cached { rnds::TexPtr tex; int ox = 0, oy = 0; float w = 0; };
		std::map<std::string, Cached> cache;

		void build();
		void load(Item& it);
		Result change(int dir);
		// the language switch asked first (owner: "Are you sure?"), in the language now and the one picked, No first:
		// langAsk the picked choice's index on the Language row (-1: not asking), langYes the button on
		int langAsk = -1;
		bool langYes = false;
		bool rawText = false;	// text() draws the string as it is (not through the menu's language): the other language's
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
		std::string gTitle, gSystem;
		int gScreen = 1, gFlash = -1, gDown = -1;
		bool gTouch = true;
		bool gHotkeys = true;	// Settings > Other systems > RetroArch hotkeys: off, F + R and the like do nothing
		double gFlashUntil = 0;
		int gSeen = -1;						// the watch's generation last drawn
		int awardsSeen = 0; bool wake = false;	// (gameWake)
		std::map<std::string, rnds::TexPtr> gBadges;	// badge textures by file
		// the companion's tabs: what the middle box shows, the party member opened, the battle last seen; where the
		// tabs were drawn (for touch)
		// the companion's views: it has the whole panel, with a tab bar along the bottom (Party, Battle in a battle,
		// Game: the panel as without it, achievements and tiles); the member whose summary is open; the battle last
		// seen; where the tabs were drawn (for touch); the party icons as textures, by species (+1000 the egg)
		// Map, Bag, Dex and Guide read the save's own data (pokemon.h: Snapshot ext); the bag's pocket and page, the
		// Pokédex's page and the entry open; the guide's look-ups (a trainer's team: a ROM search) kept by goal
		enum { PK_PARTY, PK_BATTLE, PK_GAME, PK_MON, PK_MAP, PK_BAG, PK_DEX, PK_DEXMON, PK_GUIDE };
		enum { PK_TABS = 7 };
		int pkView = PK_PARTY, pkMon = 0;
		bool pkBattle = false;
		float pkTabX[PK_TABS] = {}, pkTabW[PK_TABS] = {};
		int pkPocket = 0, pkBagPage = 0, pkDexPage = 0, pkDexNat = 0, pkPages = 1;
		std::vector<int> pkDexShown;	// the national numbers on the Pokédex page drawn (for touch)
		// the Map page's grid as drawn (for touch: a tapped square's section named under the map), the section tapped
		float pkMapX = 0, pkMapY = 0, pkMapCell = 0;
		int pkMapW = 0, pkMapH = 0, pkMapPick = -1;
		std::vector<uint8_t> pkMapCells;
		std::string pkGoalKey;
		std::vector<std::pair<uint16_t, uint8_t>> pkGoalTeam;
		void pokeMap(const SPal& p);
		void pokeBag(const SPal& p);
		void pokeDex(const SPal& p);
		void pokeDexMon(const SPal& p);
		void pokeGuide(const SPal& p);
		void pager(const SPal& p, int page, int pages, const std::string& left);
		// the game companion's views: a page (gcomp::PageId) or GC_GAME (the panel's tiles); within a page, its screen
		// (a page longer than the panel is paged); a list's row opened (its fields in full); where the tabs and the
		// rows were drawn (for touch)
		enum { GC_GAME = gcomp::NUM_PAGES, GC_TABS = gcomp::NUM_PAGES + 1 };
		int gcView = 0, gcScreen = 0, gcScreens = 1, gcList = -1, gcRow = -1;
		bool gcBattle = false;
		float gcTabX[GC_TABS] = {}, gcTabW[GC_TABS] = {};
		struct GcHit { float y0, y1; int list, row; };
		std::vector<GcHit> gcHits;
		struct GcBlock { const gcomp::Widget* w; int part; float h; };	// a widget, or one of its rows/paragraphs
		void gcRender(const SPal& p);
		void gcTabs(const SPal& p);
		void gcRowView(const SPal& p);
		std::vector<GcBlock> gcBlocks(const gcomp::Page& pg, float width);
		void gcBlock(const SPal& p, const GcBlock& b, float x, float y, float w);
		std::string gcValue(const gcomp::Val& v, const std::string& enumName, const std::string& format) const;
		int wrapLines(const std::string& t, float size, float width);
		std::map<int, rnds::TexPtr> pkIcons;
		std::shared_ptr<const pkmn::RomTables> pkTables;	// (the tables the icons came from)
		std::map<int, rnds::TexPtr> pkMapTex;				// the region map pictures uploaded (by picture), and their tables
		std::shared_ptr<const pkmn::RomTables> pkMapTables;
		void pokeRender(const SPal& p);
		void pokeParty(const SPal& p);
		void pokeSummary(const SPal& p);
		void pokeBattle(const SPal& p);
		void pokeTabs(const SPal& p);
		float pokeMatchups(const SPal& p, int t1, int t2, float x, float y, float w, float bottom);
		float num(const std::string& t, float size, float x, float top, const rnds::Color& c, bool measureOnly = false);
		void typeChip(int type, float x, float y);
		float chip(const std::string& label, uint32_t rgb, float x, float y, bool measureOnly = false);
		void hpBar(int hp, int maxHp, float x, float y, float w, uint32_t rgb = 0);
		void monIcon(uint16_t species, bool egg, float x, float y, int k);	// at x, y (CSS px), k device px a pixel
		void sprite(const char* const* rows, int h, float x, float y, int k, const std::function<rnds::Color(char)>& colour);
		float pokeName(const std::string& name, int gender, bool medium, float size, float x, float y, float width, const rnds::Color& c);
		void box(const std::string& kind, bool on, float x, float y, float w, float h);
		float text(const std::string& t, bool medium, float size, float x, float top, const rnds::Color& c, bool upper = false, float ls = 0, bool measureOnly = false);
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
		bool helpFit(const std::string& t, float top, float bottom, float& size, int& lines) const;
		void helpText(const std::string& t, float top, float bottom, const rnds::Color& c);
		std::string helpOf(int item);
		std::string gameHelpOf(int row);
		void icon(const std::string& name, float x, float y, int scale);
		void iconCentred(const std::string& name, float cx, float y, int scale);
		void arrow(bool right, float x, float y, const rnds::Color& c);
		void face(const std::string& key, const std::string& label, float x, float w);
		float faceWidth(const std::string& key, const std::string& label);
		static constexpr int ROWS = 5;
		static constexpr int SETUP_STEPS = 6, SETUP_PITCH = 55;	// the first-time setup's steps (Wi-Fi .. Finish)
	};

	// key = value files: the line for key changed (or added), the rest kept; written beside and renamed over
	void setConfValue(const std::string& path, const std::string& key, const std::string& value, const char* sep = " = ");
	std::string getConfValue(const std::string& path, const std::string& key, const std::string& def);
}
