// The sync around a game, the menu's side: the wait before it starts, while banana-sync brings that game's save up to
// date, and the save lock held for the game's length. The wait is one step at a time, called from the menu's loop with
// the time, so the menu goes on drawing and reading keys meanwhile.
#pragma once
#include "sync_util.h"
#include <functional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace gamesync
{
	using bsync::shellQuote;
	// a line of banana-sync's output, its fields
	inline std::vector<std::string> splitTabs(const std::string& ln)
	{
		std::vector<std::string> f;
		size_t a = 0;
		for (size_t b; (b = ln.find('\t', a)) != std::string::npos; a = b + 1) f.push_back(ln.substr(a, b - a));
		f.push_back(ln.substr(a));
		return f;
	}
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

	// whether this game waits for a sync: Auto sync on, Sync before playing on (off when not set), paired with RomM, a game under the card's
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
	// the game's save to the server now the game has let go of it, in the background, its lines in report (the
	// banner's file for this game's end, so an earlier game's sync still going writes to its own). False: no banana-sync
	bool syncAfterGame(const std::string& tool, const std::string& rom, const std::string& run, const std::string& report);
	// a DS game over: nds-saves told (--played, in the background) what its saves hold now, which is what the game's
	// resume state belongs to; a save replaced by hand before the next start then isn't played under that state.
	// False: not a DS game, or no nds-saves
	bool gamePlayed(const std::string& system, const std::string& rom);

	class Wait
	{
	public:
		static constexpr double SHOW_AFTER = 400;		// ms: a quick sync shows nothing at all
		static constexpr double WIFI_GRACE = 8000;
		static constexpr double FIRST_ANSWER = 5000;
		static constexpr double CAP = 20000;
		static constexpr double NOTICE = 1500;
		static constexpr double STOP_GRACE = 3000;		// ms banana-sync is given to finish placing a save, once asked to stop
		static constexpr double KILL_GRACE = 2000;		// then, killed, to be gone

		std::function<WifiFacts()> wifi;
		std::function<void(const std::string&)> log;

		void begin(const std::string& tool, const std::string& rom, const std::string& run, double now);
		bool step(double now);	// true: what is shown changed, by this step or by a press since the last
		void skip();			// no more waiting: the game now (once the sync has stopped, which the steps see to)
		void end();				// the same, waited out here: the menu is ending
		// a press while it waits ('a', 'b', 'x', or 0 for any other): B plays now (unless a newer save was still coming, which
		// is then asked about), anything ends a notice; a question takes its own buttons
		void press(char button);
		bool backed() { bool b = back; back = false; return b; }	// the owner went back: this game isn't to start
		bool active() const { return state != IDLE; }
		// the pop-up's lines (false: nothing shown)
		bool text(std::string& l1, std::string& l2) const;

	private:
		enum State { IDLE, WIFI, SYNCING, SHOWING, CONFLICT, SETTLING, OLDER, UNSETTLED, STOPPING } state = IDLE;
		// what follows a stop, once banana-sync is gone: the game (or the question of a newer save not fetched), the
		// same or else the notice that nothing synced, or the question of a choice not carried out
		enum After { PLAY, PLAY_OR_NOTICE, UNSETTLE } after = PLAY;
		double stopAt = 0;
		bool killed = false;
		std::string tool, rom, status, older, conflicts, conflictKey;	// (older: launch.older, the sync's mark of a newer save not fetched; conflicts: launch.conflict, what it held for the game)
		bool back = false;
		bool serverOnly = false;	// the question is of a save gone or blank here: the server's to take back, or none
		std::string runDir;
		double from = 0, asked = 0, until = 0;
		bool shown = false, answered = false, pressed = false;	// (pressed: a press changed the state, for the next step to tell)
		pid_t child = -1;
		std::string line1, line2;
		void act(char button);
		void spawn(double now);
		void settle(const char* how);
		std::string heldConflict();
		void stop(After then, double now);	// asked to stop: what follows at once when it is gone in a few ms, else STOPPING, stepped until it is
		void stopped(double now);	// gone: its files cleared, then what was to follow
		void killGroup();
		void finished(int wstatus, double now);
		void notice(const std::string& l1, const std::string& l2, double now);
		bool askIfOlder();	// the sync left launch.older: OLDER
		void notSynced(double now);
		void say(const std::string& s) { if (log) log(s); }
	};
}
