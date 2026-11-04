#include "gamesync.h"
#include "json.h"
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <net/if.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace gamesync
{
	Wifi wifiStage(const WifiFacts& f)
	{
		if (f.operUp && f.hasAddr) return Wifi::Online;
		// (15 s: wpa_supplicant just started and still looking. Longer than that it has no network in reach, and every game
		// would wait for one that isn't coming. A device pass tunes this with the others)
		if (f.wasUp || f.busy || f.operUp || (f.wpaAge >= 0 && f.wpaAge < 15)) return Wifi::ComingUp;
		return Wifi::Down;
	}
	WifiFacts readWifi(const std::string& run)
	{
		WifiFacts f;
		std::string state;
		std::ifstream("/sys/class/net/wlan0/operstate") >> state;
		f.operUp = state == "up";
		int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
		ifreq ifr = {};
		strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ - 1);
		if (fd >= 0 && ioctl(fd, SIOCGIFADDR, &ifr) == 0) f.hasAddr = true;
		if (fd >= 0) close(fd);
		f.wasUp = access((run + "/wifi-was-up").c_str(), F_OK) == 0;
		struct stat st;
		if (stat((run + "/wpa.pid").c_str(), &st) == 0) f.wpaAge = difftime(time(nullptr), st.st_mtime);
		// (the gap after a wake between wifi-was-up going and wpa.pid coming, which can be seconds of firmware loading)
		int lk = open((run + "/wifi.lock").c_str(), O_RDONLY | O_CLOEXEC);
		if (lk >= 0) { f.busy = flock(lk, LOCK_SH | LOCK_NB) != 0; close(lk); }
		return f;
	}

	bool wanted(const std::string& autoSync, const std::string& beforePlaying, const std::string& conf,
		const std::string& roms, const std::string& rom, const std::string& resumeOnce, const std::string& sd2Roms)
	{
		if ((autoSync != "quit" && autoSync != "both") || beforePlaying != "on") return false;
		auto under = [&](const std::string& dir) { return !dir.empty() && rom.compare(0, dir.size() + 1, dir + "/") == 0; };
		if (!under(roms) && !under(sd2Roms)) return false;
		// (as its launchers read it: another game's left behind says nothing of this one)
		std::string once;
		std::getline(std::ifstream(resumeOnce), once);
		if (once == rom) return false;
		// (Romhost's plan is all the saves at once: not while a game waits)
		banana::JPtr j = banana::readJsonFile(conf);
		return j && j->isObj() && !j->str("token").empty() && j->str("backend") == "romm";
	}

	int holdSaves(const std::string& run, int waitMs)
	{
		int fd = open((run + "/save.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
		if (fd < 0) return -1;
		// (a sync placing a save holds it for well under a second; one over every save, far longer: waited for a
		// little, not for that)
		for (int waited = 0; flock(fd, LOCK_EX | LOCK_NB) != 0; waited += 50)
		{
			if (errno != EWOULDBLOCK || waited >= waitMs) { close(fd); return -1; }
			usleep(50000);
		}
		return fd;
	}
	void releaseSaves(int fd) { if (fd >= 0) close(fd); }

	std::string syncTool()
	{
		const char* t = getenv("BANANA_SYNC");	// (another build of it, or the tests' stand-in)
		return t && *t ? t : "/usr/bin/banana-sync";
	}
	bool syncsAfterGame(const std::string& autoSync) { return autoSync == "quit" || autoSync == "both"; }
	std::string sd2Roms(const std::string& run)
	{
		std::string p;
		std::getline(std::ifstream(run + "/sd2/roms"), p);
		return p;
	}
	bool syncAfterGame(const std::string& tool, const std::string& rom)
	{
		if (access(tool.c_str(), X_OK) != 0) return false;
		pid_t p = fork();
		if (p == 0)
		{
			if (fork() == 0)
			{
				setsid();
				int nul = open("/dev/null", O_RDWR);
				if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
				execl(tool.c_str(), tool.c_str(), "sync", "--auto", "quit", "--rom", rom.c_str(), (char*)nullptr);
				_exit(127);
			}
			_exit(0);
		}
		if (p > 0) waitpid(p, nullptr, 0);
		return true;
	}
	bool gamePlayed(const std::string& system, const std::string& rom)
	{
		if (system != "nds") return false;
		const char* env = getenv("BANANA_NDS_SAVES");	// (the tests' stand-in)
		std::string tool = env ? env : "/usr/libexec/banana/nds-saves";
		if (access(tool.c_str(), X_OK) != 0) return false;
		// (in the background, as the sync after the game is: an MD5 or two of files on the card, which a busy card
		// can take its time over, and nothing of the menu's waits on it)
		pid_t p = fork();
		if (p == 0)
		{
			if (fork() == 0)
			{
				setsid();
				int nul = open("/dev/null", O_RDWR);
				if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
				execl(tool.c_str(), tool.c_str(), rom.c_str(), "--played", (char*)nullptr);
				_exit(127);
			}
			_exit(0);
		}
		if (p > 0) waitpid(p, nullptr, 0);
		return true;
	}

	void Wait::begin(const std::string& t, const std::string& r, const std::string& run, double now)
	{
		tool = t; rom = r; runDir = run;
		status = run + "/launch.status";
		older = run + "/launch.older";
		conflicts = run + "/launch.conflict";
		from = now;
		shown = answered = false;
		// (an earlier game's mark says nothing of this one, and a B before the sync starts would read it; nor do the
		// lines of what was held for it)
		unlink(older.c_str());
		unlink(conflicts.c_str());
		if (access(tool.c_str(), X_OK) != 0) { say("sync before the game: no banana-sync"); return; }
		Wifi w = wifiStage(wifi());
		if (w == Wifi::Online) spawn(now);
		else if (w == Wifi::ComingUp) { state = WIFI; say("sync before the game: waiting for Wi-Fi"); }
		else say("sync before the game: no Wi-Fi");
	}
	void Wait::spawn(double now)
	{
		unlink(status.c_str());
		asked = now;
		pid_t p = fork();
		if (p == 0)
		{
			setsid();
			sigset_t none;
			sigemptyset(&none);
			sigprocmask(SIG_SETMASK, &none, nullptr);
			signal(SIGPIPE, SIG_DFL);
			int nul = open("/dev/null", O_RDWR);
			if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
			execl(tool.c_str(), tool.c_str(), "--run", runDir.c_str(), "sync", "--auto", "launch", "--rom", rom.c_str(), (char*)nullptr);
			_exit(127);
		}
		if (p < 0) { say("sync before the game: can't start it"); state = IDLE; return; }
		child = p;
		state = SYNCING;
		say("sync before the game: started");
	}
	// (banana-sync leaves at once on SIGTERM, unless it is placing a save, which it finishes first: a few writes and their
	// syncs, under a second on a card that isn't busy, so it is given a few. Only then is it killed outright with all it
	// started (nds-saves, which runs with SIGTERM held off as banana-sync is while placing), which the journal recovers.
	// A process a failing card won't let die isn't waited for: the menu goes on. Stepped, so the panels and keys go on
	// meanwhile)
	void Wait::stop(After then, double now)
	{
		after = then;
		if (child <= 0) { stopped(now); return; }
		kill(child, SIGTERM);
		// (gone at once, as it is unless placing a save: a few ms, so what follows comes in this step)
		int st;
		for (int i = 0; i < 10; i++)
		{
			if (waitpid(child, &st, WNOHANG) == child) { stopped(now); return; }
			usleep(5000);
		}
		state = STOPPING;
		stopAt = 0;
		killed = false;
	}
	void Wait::killGroup()
	{
		// (spawn() made it a session of its own, so its group is it and what it started)
		kill(-child, SIGKILL);
		kill(child, SIGKILL);
		killed = true;
	}
	void Wait::stopped(double now)
	{
		if (child > 0)
		{
			// (what it was working on, on /run: memory, and left behind by every B otherwise)
			std::string pid = std::to_string(child);
			for (const std::string& f : { "/sync-dl." + pid, "/sync-dl." + pid + ".part", "/sync-upload." + pid }) unlink((runDir + f).c_str());
			child = -1;
		}
		state = IDLE;
		if (after == UNSETTLE) { state = UNSETTLED; return; }
		// (the sync had found the server's save newer and was fetching it: not played over unasked, a B or not)
		if (askIfOlder()) return;
		if (after == PLAY_OR_NOTICE && now >= 0) notSynced(now);
	}
	void Wait::end()
	{
		if (child > 0)
		{
			kill(child, SIGTERM);
			int st;
			bool gone = false;
			for (int i = 0; i < STOP_GRACE / 10 && !(gone = waitpid(child, &st, WNOHANG) == child); i++) usleep(10000);
			if (!gone)
			{
				killGroup();
				for (int i = 0; i < KILL_GRACE / 10 && !(gone = waitpid(child, &st, WNOHANG) == child); i++) usleep(10000);
			}
		}
		after = PLAY;
		stopped(-1);
		state = IDLE;
	}
	void Wait::notice(const std::string& l1, const std::string& l2, double now)
	{
		line1 = l1; line2 = l2;
		until = now + NOTICE;
		state = SHOWING;
		say("sync before the game: " + l1);
	}
	// this game's conflict, from the lines the sync before it left in launch.conflict (the Saves page's, for this game):
	// its key, and the two sides' sizes and the server's time for the question; or a save gone or blank here since the
	// last sync, with the server's copy to take back (serverOnly); "" when there is nothing to choose. A file on /run:
	// nothing here waits on the card
	std::string Wait::heldConflict()
	{
		serverOnly = false;
		std::string key;
		std::ifstream in(conflicts);
		for (std::string ln; key.empty() && std::getline(in, ln);)
		{
			std::vector<std::string> v = splitTabs(ln);
			if (v.size() < 12) continue;
			bool both = v[0] == "conflict" && v[11] == "both", gone = v[0] == "held" && v[11] == "server";
			if (!both && !gone) continue;
			key = v[1];
			serverOnly = gone;
			auto kb = [](const std::string& s) { return std::to_string((atoll(s.c_str()) + 512) >> 10) + " KB"; };
			std::string when = v[9].size() >= 16 ? " (" + v[9].substr(0, 10) + ")" : "";
			std::string here = kb(v[5]), server = kb(v[7]);
			line1 = gone ? "No save here: the server has one, " + server + when : "Save conflict: here " + here + ", server " + server + when;
		}
		return key;
	}
	void Wait::settle(const char* how)
	{
		pid_t p = fork();
		if (p == 0)
		{
			setsid();
			sigset_t none;
			sigemptyset(&none);
			sigprocmask(SIG_SETMASK, &none, nullptr);
			int nul = open("/dev/null", O_RDWR);
			if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
			execl(tool.c_str(), tool.c_str(), "--run", runDir.c_str(), "--rom", rom.c_str(), "resolve", conflictKey.c_str(), how, (char*)nullptr);
			_exit(127);
		}
		if (p < 0) { state = IDLE; return; }
		child = p;
		state = SETTLING;
		say(std::string("sync before the game: settling, ") + how);
	}
	bool Wait::askIfOlder()
	{
		if (access(older.c_str(), F_OK) != 0) return false;
		state = OLDER;
		say("sync before the game: the server's newer save couldn't be fetched");
		return true;
	}
	void Wait::notSynced(double now) { notice("Not synced yet", "It syncs when you quit", now); }
	void Wait::finished(int wstatus, double now)
	{
		child = -1;
		std::string word;
		long v[6] = {};
		{
			std::ifstream f(status);
			f >> word;
			for (long& x : v) f >> x;
		}
		long held = word == "done" ? v[4] : 0;
		// (a newer save on the server that couldn't be fetched: not played over without asking, which would fork it)
		if (askIfOlder()) return;
		if (held > 0)
		{
			conflictKey = heldConflict();
			if (conflictKey.empty()) notice("Save conflict", "Playing this handheld's save", now);
			else { state = CONFLICT; say("sync before the game: asking about a conflict"); }
		}
		else if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) { say("sync before the game: done"); state = IDLE; }
		else notSynced(now);
	}
	bool Wait::step(double now)
	{
		bool was = shown, byPress = pressed;
		pressed = false;
		State st = state;
		if (state == WIFI)
		{
			Wifi w = wifiStage(wifi());
			// (only an address ends it early: the signs of Wi-Fi coming up don't quite meet, and a gap between them mustn't
			// read as Wi-Fi gone)
			if (w == Wifi::Online) spawn(now);
			else if (now - from >= WIFI_GRACE) notSynced(now);
		}
		else if (state == SYNCING)
		{
			int ws;
			if (waitpid(child, &ws, WNOHANG) == child) finished(ws, now);
			else
			{
				if (!answered)
				{
					// (all it says before the server's first answer is "running 0 0"; after it the line moves on quickly.
					// "waiting": another sync has the saves, the one after the last game still placing, which it waits
					// out; the 5 s for a first answer start again once it has them, the cap goes on counting)
					std::string w;
					std::getline(std::ifstream(status) >> std::ws, w);
					if (w.compare(0, 8, "waiting ") == 0) asked = now;
					else answered = !w.empty() && w.compare(0, 10, "running 0 ") != 0;
				}
				if ((!answered && now - asked >= FIRST_ANSWER) || now - from >= CAP)
				{
					say(answered ? "sync before the game: stopped at the cap" : "sync before the game: no answer");
					stop(PLAY_OR_NOTICE, now);
				}
			}
		}
		else if (state == SETTLING)
		{
			if (!asked) asked = now;
			int ws;
			if (waitpid(child, &ws, WNOHANG) == child)
			{
				child = -1;
				if (WIFEXITED(ws) && WEXITSTATUS(ws) == 0) { say("sync before the game: settled"); state = IDLE; }
				else { state = UNSETTLED; say("sync before the game: Not settled, asking"); }
			}
			else if (now - asked >= CAP) { say("sync before the game: Not settled at the cap, asking"); stop(UNSETTLE, now); }
		}
		else if (state == STOPPING)
		{
			if (!stopAt) stopAt = now;
			int ws;
			if (waitpid(child, &ws, WNOHANG) == child) stopped(now);
			else if (!killed && now - stopAt >= STOP_GRACE) { killGroup(); stopAt = now; }
			else if (killed && now - stopAt >= KILL_GRACE) { say("sync before the game: banana-sync won't die"); stopped(now); }
		}
		else if (state == SHOWING && now >= until) state = IDLE;
		if ((state == WIFI || state == SYNCING) && now - from >= SHOW_AFTER) shown = true;
		return state != st || shown != was || byPress;
	}
	void Wait::skip()
	{
		if (state == IDLE || state == STOPPING) return;
		if (state != SHOWING) say("sync before the game: B, played now");
		stop(PLAY, -1);
	}
	// (a press's change is told by the next step, which compares the state it finds with the one it leaves: the menu
	// redraws on the step's word, so the question would stay on the panel through the settling without this)
	void Wait::press(char button)
	{
		State st = state;
		act(button);
		if (state != st) pressed = true;
	}
	void Wait::act(char button)
	{
		if (state == CONFLICT)
		{
			if (button == 'a' || button == 'x') { asked = 0; settle(serverOnly || button == 'x' ? "server" : "card"); }
			else if (button == 'b') { say("sync before the game: decided later"); state = IDLE; }
			return;
		}
		if (state == OLDER || state == UNSETTLED)
		{
			if (button == 'a') { say(state == UNSETTLED ? "sync before the game: played on this handheld's save, not settled" : "sync before the game: played on this handheld's older save"); state = IDLE; }
			else if (button == 'b') { say("sync before the game: back"); back = true; state = IDLE; }
			return;
		}
		if (state == SETTLING || state == STOPPING) return;	// (the owner's choice runs its course: the cap ends it)
		if (button == 'b' || state == SHOWING) skip();
	}
	bool Wait::text(std::string& l1, std::string& l2) const
	{
		if (state == SHOWING) { l1 = line1; l2 = line2; return true; }
		if (state == CONFLICT) { l1 = line1; l2 = serverOnly ? "A: take the server's  B: play without" : "A: this handheld's  X: the server's  B: later"; return true; }
		if (state == SETTLING) { l1 = "Settling the save..."; l2 = ""; return true; }
		if (state == UNSETTLED) { l1 = "The save couldn't be settled"; l2 = "A: play this handheld's  B: back"; return true; }
		if (state == OLDER) { l1 = "The server's newer save didn't arrive"; l2 = "A: play this handheld's (older)  B: back"; return true; }
		if (state == IDLE || !shown) return false;
		l1 = state == WIFI ? "Waiting for Wi-Fi..." : "Syncing this game's save...";
		l2 = state == STOPPING ? "" : "B: play now";
		return true;
	}
}
