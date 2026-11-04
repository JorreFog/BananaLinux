// The sync around a game, the menu's side: the wait before it starts, while banana-sync brings that game's save up to
// date, and the save lock held for the game's length. The wait is one step at a time, called from the menu's loop with
// the time, so the menu goes on drawing and reading keys meanwhile.
#pragma once
#include <functional>
#include <string>
#include <sys/types.h>

namespace gamesync
{
	// what can be read of wlan0 without asking wpa_supplicant
	struct WifiFacts
	{
		bool operUp = false;	// operstate "up", which comes before DHCP's address
		bool hasAddr = false;
		bool wasUp = false;		// /run/banana/wifi-was-up: back from a sleep, the driver still loading
		double wpaAge = -1;		// seconds since wpa_supplicant started (-1: not running)
		bool busy = false;		// /run/banana/wifi.lock held: "wifi up" under way, before wpa_supplicant has started
	};
	enum class Wifi { Online, ComingUp, Down };
	Wifi wifiStage(const WifiFacts& f);
	WifiFacts readWifi(const std::string& run);

	// whether this game waits for a sync: Auto sync on, Sync before playing on, paired with RomM, a game under the card's
	// ROMs folder or the second SD card's (sd2Roms, "" when it isn't in), and not the game started again after a
	// power-off, whose resume state belongs to the save on the card
	bool wanted(const std::string& autoSync, const std::string& beforePlaying, const std::string& conf,
		const std::string& roms, const std::string& rom, const std::string& resumeOnce, const std::string& sd2Roms = "");
	// the second SD card's roms folder, as the sd2 script names it in <run>/sd2/roms ("" when it isn't in)
	std::string sd2Roms(const std::string& run);

	// run's save.lock for the game's length, so no sync touches a save in use: its fd, or -1 when it can't be had within
	// waitMs (a sync of every save holding it stops at its next game once the game runs). The game doesn't inherit it
	int holdSaves(const std::string& run, int waitMs = 0);
	void releaseSaves(int fd);

	std::string syncTool();
	bool syncsAfterGame(const std::string& autoSync);
	// the game's save to the server now the game has let go of it, in the background. False: no banana-sync
	bool syncAfterGame(const std::string& tool, const std::string& rom);

	class Wait
	{
	public:
		static constexpr double SHOW_AFTER = 400;		// ms: a quick sync shows nothing at all
		static constexpr double WIFI_GRACE = 8000;
		static constexpr double FIRST_ANSWER = 5000;
		static constexpr double CAP = 20000;
		static constexpr double NOTICE = 1500;

		std::function<WifiFacts()> wifi;
		std::function<void(const std::string&)> log;

		void begin(const std::string& tool, const std::string& rom, const std::string& run, double now);
		bool step(double now);	// true: what is shown changed
		void skip();			// no more waiting: the game now
		void press(bool isB);	// a press while it waits: B plays now, anything ends a notice
		bool active() const { return state != IDLE; }
		// the pop-up's lines (false: nothing shown)
		bool text(std::string& l1, std::string& l2) const;

	private:
		enum State { IDLE, WIFI, SYNCING, SHOWING } state = IDLE;
		std::string tool, rom, status;
		std::string runDir;
		double from = 0, asked = 0, until = 0;
		bool shown = false, answered = false;
		pid_t child = -1;
		std::string line1, line2;
		void spawn(double now);
		void stop();
		void finished(int wstatus, double now);
		void notice(const std::string& l1, const std::string& l2, double now);
		void notSynced(double now);
		void say(const std::string& s) { if (log) log(s); }
	};
}
