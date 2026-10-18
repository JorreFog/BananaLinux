#pragma once
// banana-ui: the settings screen (Start in the menu), drawn in ROCKNIXDS Pixel's look with the engine's own pieces
// (its box styles painted by rnds::paintBox, Pixelify Sans, the theme's pixel icons). Bottom panel: the list, one
// row per setting in the slots' style, ◀ value ▶ on the right, and the B / A chips; top panel: the setting's
// section icon, its name and what it does. Every change is applied and saved at once:
//   /data/banana/settings.conf     the menu's (theme, motion, sounds, clock, splash, Wi-Fi)
//   /data/banana/bananad.conf      sleep and the LEDs (bananad reloads it on SIGHUP)
//   /data/banana/superdrastic.conf SuperDrastic's shader and CPU clock (run-drastic reads it)
//   /data/bananads/bananads.conf   BananaDS's own settings (shader, 3D resolution, ...; BananaDS reads it)
//   /data/drastic/config/drastic.cfg  the DS's 3D resolution
//   /sys/class/backlight + /data/banana/backlight   brightness
#include "../rnds/RndsUI.h"
#include "input.h"
#include "wifi.h"
#include <map>
#include <memory>
#include <mutex>
#include <functional>
#include <string>
#include <vector>

namespace banana
{
	class Settings
	{
	public:
		Settings(rnds::Backend* be, const std::string& assets, const std::string& dataDir, float scale, const std::string& variant);
		void setVariant(const std::string& variant);	// "dark", "light", or "carts" (dark, in Inter: setFontDir first)
		void setFontDir(const std::string& dir) { fontDir = dir; }
		void open();
		void close() { mOpen = false; page = LIST; wizard = false; }
		void setup() { mOpen = true; openSetup(); }	// the first-time setup, from the host
		bool isOpen() const { return mOpen; }
		bool animating() const;	// the highlight or the list still easing into place: frames wanted

		// The game panel (settings.cpp, end): while a RetroArch game runs on one panel through a DRM lease, the other
		// shows the game's RetroAchievements (progress and the latest unlocks with their badges) and touch tiles for
		// RetroArch (save, load, its menu, fast forward, quit), in the in-game menu's look.
		enum GameAction { G_NONE, G_SAVE, G_LOAD, G_MENU, G_FFWD, G_QUIT };
		void gameOpen(const std::string& title, const std::string& system, int screen, bool touch);
		void gameClose();
		bool gameUpdate(double nowMs);		// true: the panel changed (a log line, a reply, a badge, a flash ending)
		void gameRender();
		GameAction gameTouch(float x, float y, bool down);	// device px on the panel's screen
		bool gameProgress(int& unlocked, int& total);			// the game's achievements, once the server said
		// box art (banana-scrape) finished since the last call: the library is read again
		bool takeLibraryChanged() { bool r = libChanged; libChanged = false; return r; }

		// what the host has to do after an input
		enum Result { NONE, MOVED, CHANGED, CLOSED, THEME, REBOOT, POWEROFF, SEARCH };
		// SELECT in the menu: the keyboard alone, for a search of the games; SEARCH when it is done, with the text in
		// searchText (empty: the search taken away)
		void openSearch(const std::string& start);
		// X on a DS game in the library: which emulator runs this one game (title shown, ROM file name as the key)
		void openGame(const std::string& title, const std::string& romName);
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
		enum Page { LIST, NETWORKS, KEYBOARD, UPDATES, SETUP, GAME } page = LIST;
		// the game page (X on a DS game in the library): its emulator, this game only (banana/nds-per-game.conf, read by
		// run-nds: "<ROM file name> = superdrastic|bananads", no line = Settings > Nintendo DS > Emulator)
		std::string pgTitle, pgRom;
		int pgPick = 0;
		std::string gameEmulator(const std::string& rom);
		void setGameEmulator(const std::string& rom, const std::string& emu);
		Result pressGame(Key k);
		Result touchGame(float cx, float cy);
		void renderGame();
		// the keyboard types a text for a purpose: a Wi-Fi password, or an account's name or password
		// (banana/accounts.conf: RetroAchievements for DraStic and RetroArch, ScreenScraper for the scraper to come)
		enum Entry { ENTRY_WIFI, ENTRY_RA_USER, ENTRY_RA_PASS, ENTRY_SS_USER, ENTRY_SS_PASS, ENTRY_SEARCH } entry = ENTRY_WIFI;
		std::string entryTitle, entrySub;
		Page entryBack = NETWORKS;
		void startEntry(Entry e, const std::string& title, const std::string& sub, Page back, const std::string& start = "");
		Result finishEntry();
		std::string account(const char* key);
		// the RetroAchievements account typed in the setup, checked with the server at once (on a thread: libcurl)
		struct RaCheck { std::mutex mu; bool done = false; int kind = 0; std::string l2; };	// kind: 1 ok, 2 refused, 3 no network
		std::shared_ptr<RaCheck> raCheck;
		void startRaCheck(const std::string& user, const std::string& pass);
		std::string toast1, toast2;
		uint32_t toastAccent = 0;
		int toastIcon = 0;			// 0 "i", 1 a tick, 2 a cross
		double toastUntil = 0;
		void toast(const std::string& l1, const std::string& l2, uint32_t accent, int icon, double ms);
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
		std::vector<wifi::Network> nets;
		wifi::Status wstat;
		bool scanning = false, scannedOnce = false, wifiEnabled = true;
		int nsel = 0, ntop = 0;
		std::string joining, message;	// the network being joined; the last thing to tell
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
		double nextStatus2 = 0;
		std::string slot, version, board;
		void openUpdates();
		void pollUpdate();
		Result pressUpdates(Key k);
		void renderUpdates();

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
		struct Cached { rnds::TexPtr tex; int ox = 0, oy = 0; float w = 0; };
		std::map<std::string, Cached> cache;

		void build();
		void load(Item& it);
		Result change(int dir);
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
		void scrapeBar(float x, float y, float w);	// the progress bar and the game being fetched, while it runs
		struct RaWatch;							// the achievements: RetroArch's log and the server (settings.cpp)
		std::shared_ptr<RaWatch> raw;
		std::string gTitle, gSystem;
		int gScreen = 1, gFlash = -1, gDown = -1;
		bool gTouch = true;
		double gFlashUntil = 0;
		int gSeen = -1;						// the watch's generation last drawn
		std::map<std::string, rnds::TexPtr> gBadges;	// badge textures by file
		void box(const std::string& kind, bool on, float x, float y, float w, float h);
		float text(const std::string& t, bool medium, float size, float x, float top, const rnds::Color& c, bool upper = false, float ls = 0, bool measureOnly = false);
		void wrapped(const std::string& t, float size, float x, float top, float width, int maxLines, const rnds::Color& c);
		void icon(const std::string& name, float x, float y, int scale);
		void iconCentred(const std::string& name, float cx, float y, int scale);
		void arrow(bool right, float x, float y, const rnds::Color& c);
		void face(const std::string& key, const std::string& label, float x, float w);
		float faceWidth(const std::string& key, const std::string& label);
		static constexpr int ROWS = 5;
	};

	// key = value files: the line for key changed (or added), the rest kept; written beside and renamed over
	void setConfValue(const std::string& path, const std::string& key, const std::string& value, const char* sep = " = ");
	std::string getConfValue(const std::string& path, const std::string& key, const std::string& def);
}
