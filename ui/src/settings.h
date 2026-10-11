#pragma once
// banana-ui: the settings screen (Start in the menu), drawn in ROCKNIXDS Pixel's look with the engine's own pieces
// (its box styles painted by rnds::paintBox, Pixelify Sans, the theme's pixel icons). Bottom panel: the list, one
// row per setting in the slots' style, ◀ value ▶ on the right, and the B / A chips; top panel: the setting's
// section icon, its name and what it does. Every change is applied and saved at once:
//   /data/banana/settings.conf     the menu's (theme, motion, sounds, clock, splash, Wi-Fi)
//   /data/banana/bananad.conf      sleep and the LEDs (bananad reloads it on SIGHUP)
//   /data/banana/superdrastic.conf the DS renderer and shader (run-drastic reads it)
//   /data/drastic/config/drastic.cfg  the DS's 3D resolution
//   /sys/class/backlight + /data/banana/backlight   brightness
#include "../rnds/RndsUI.h"
#include "input.h"
#include "wifi.h"
#include <map>
#include <string>
#include <vector>

namespace banana
{
	class Settings
	{
	public:
		Settings(rnds::Backend* be, const std::string& assets, const std::string& dataDir, float scale, const std::string& variant);
		void setVariant(const std::string& variant);
		void open();
		void close() { mOpen = false; page = LIST; wizard = false; }
		void setup() { mOpen = true; openSetup(); }	// the first-time setup, from the host
		bool isOpen() const { return mOpen; }

		// what the host has to do after an input
		enum Result { NONE, MOVED, CHANGED, CLOSED, THEME, REBOOT, POWEROFF };
		Result press(Key k);
		Result touch(float x, float y, bool down);	// bottom panel, device px
		void render();
		// background work (the Wi-Fi scan, joining a network): true when something changed and wants a frame
		bool update(double nowMs);
		// ms until update() has something to do, -1 nothing pending
		int nextWake(double nowMs) const;
		// run commands at once instead of in the background (the headless test)
		void setSync(bool on) { sync = on; }

		// a notice over the screen being drawn (between the backend's beginScreen and endScreen): the battery icon at
		// percent, a title and a line or two, in the bubble style near the bottom of the panel
		void notice(const std::string& title, const std::string& body, int percent);

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
		bool downOnRow = false;
		float downX = 0, downY = 0;
		std::string about, wifiInfo, ip;

		// the Wi-Fi pages: the networks around, and the keyboard for a password (settings-wifi.cpp)
		enum Page { LIST, NETWORKS, KEYBOARD, UPDATES, SETUP } page = LIST;
		// the keyboard types a text for a purpose: a Wi-Fi password, or an account's name or password
		// (banana/accounts.conf: RetroAchievements for DraStic and RetroArch, ScreenScraper for the scraper to come)
		enum Entry { ENTRY_WIFI, ENTRY_RA_USER, ENTRY_RA_PASS, ENTRY_SS_USER, ENTRY_SS_PASS } entry = ENTRY_WIFI;
		std::string entryTitle, entrySub;
		Page entryBack = NETWORKS;
		void startEntry(Entry e, const std::string& title, const std::string& sub, Page back, const std::string& start = "");
		Result finishEntry();
		std::string account(const char* key);
		// the first-time setup: the steps (Wi-Fi, the time zone, RetroAchievements, ScreenScraper), run once after
		// the installation (banana/setup-done is written at the end) and from Settings > System
		bool wizard = false;
		int wsel = 0;
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
		struct Cached { rnds::TexPtr tex; int ox = 0, oy = 0; float w = 0; };
		std::map<std::string, Cached> cache;

		void build();
		void load(Item& it);
		Result change(int dir);
		void save(const Item& it);
		void refreshInfo();

		// drawing, in CSS px of a 640x480 panel
		float X(float v) const { return v * s; }
		void box(const std::string& kind, bool on, float x, float y, float w, float h);
		float text(const std::string& t, bool medium, float size, float x, float top, const rnds::Color& c, bool upper = false, float ls = 0, bool measureOnly = false);
		void wrapped(const std::string& t, float size, float x, float top, float width, int maxLines, const rnds::Color& c);
		void icon(const std::string& name, float x, float y, int scale);
		void arrow(bool right, float x, float y, const rnds::Color& c);
		void face(const std::string& key, const std::string& label, float x, float w);
		float faceWidth(const std::string& key, const std::string& label);
		static constexpr int ROWS = 5;
	};

	// key = value files: the line for key changed (or added), the rest kept; written beside and renamed over
	void setConfValue(const std::string& path, const std::string& key, const std::string& value, const char* sep = " = ");
	std::string getConfValue(const std::string& path, const std::string& key, const std::string& def);
}
