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
		if ((autoSync != "quit" && autoSync != "both") || beforePlaying == "off") return false;
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
	std::string sd2Roms(const std::string& run)
	{
		std::string p;
		std::getline(std::ifstream(run + "/sd2/roms"), p);
		return p;
	}
	bool syncsAfterGame(const std::string& autoSync) { return autoSync == "quit" || autoSync == "both"; }
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

	void Wait::begin(const std::string& t, const std::string& r, const std::string& run, double now)
	{
		tool = t; rom = r; runDir = run;
		status = run + "/launch.status";
		from = now;
		shown = answered = false;
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
	void Wait::stop()
	{
		if (child <= 0) return;
		kill(child, SIGTERM);
		// (banana-sync leaves at once on it, unless it is placing a save, which it finishes first: a few writes and their
		// syncs, under a second on a card that isn't busy, so it is given a few. Only then is it killed outright, which
		// the journal recovers, and a process a failing card won't let die isn't waited for: the menu goes on)
		int st;
		bool gone = false;
		for (int i = 0; i < 300 && !(gone = waitpid(child, &st, WNOHANG) == child); i++) usleep(10000);
		if (!gone)
		{
			kill(child, SIGKILL);
			for (int i = 0; i < 200 && !(gone = waitpid(child, &st, WNOHANG) == child); i++) usleep(10000);
			if (!gone) say("sync before the game: banana-sync won't die");
		}
		// (what it was working on, on /run: memory, and left behind by every B otherwise)
		std::string pid = std::to_string(child);
		for (const std::string& f : { "/sync-dl." + pid, "/sync-dl." + pid + ".part", "/sync-upload." + pid }) unlink((runDir + f).c_str());
		child = -1;
	}
	void Wait::notice(const std::string& l1, const std::string& l2, double now)
	{
		line1 = l1; line2 = l2;
		until = now + NOTICE;
		state = SHOWING;
		say("sync before the game: " + l1);
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
		if (held > 0) notice("Save conflict", "Playing this handheld's save", now);
		else if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) { say("sync before the game: done"); state = IDLE; }
		else notSynced(now);
	}
	bool Wait::step(double now)
	{
		bool was = shown;
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
					stop();
					notSynced(now);
				}
			}
		}
		else if (state == SHOWING && now >= until) state = IDLE;
		if ((state == WIFI || state == SYNCING) && now - from >= SHOW_AFTER) shown = true;
		return state != st || shown != was;
	}
	void Wait::skip()
	{
		if (state == IDLE) return;
		if (state != SHOWING) say("sync before the game: B, played now");
		stop();
		state = IDLE;
	}
	void Wait::press(bool isB)
	{
		if (isB || state == SHOWING) skip();
	}
	bool Wait::text(std::string& l1, std::string& l2) const
	{
		if (state == SHOWING) { l1 = line1; l2 = line2; return true; }
		if (state == IDLE || !shown) return false;
		l1 = state == WIFI ? "Waiting for Wi-Fi..." : "Syncing this game's save...";
		l2 = "B: play now";
		return true;
	}
}
