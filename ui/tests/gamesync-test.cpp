// the menu's side of the sync around a game: which Wi-Fi is worth waiting for, which games wait, and the save lock, on a
// throwaway folder: gamesync-test <dir>. The wait itself is driven through the menu by tests/sync.sh
#include "gamesync.h"
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace gamesync;
static int fails;
static void expect(bool ok, const char* what) { if (!ok) { fprintf(stderr, "FAIL %s\n", what); fails++; } }
static bool lockFree(const std::string& run)
{
	int fd = open((run + "/save.lock").c_str(), O_RDWR | O_CREAT, 0644);
	bool ok = flock(fd, LOCK_EX | LOCK_NB) == 0;
	close(fd);
	return ok;
}

int main(int argc, char** argv)
{
	std::string d = argc > 1 ? argv[1] : "/tmp/gamesync-test";
	mkdir(d.c_str(), 0755);
	auto w = [](bool up, bool addr, bool was, double age) { WifiFacts f; f.operUp = up; f.hasAddr = addr; f.wasUp = was; f.wpaAge = age; return wifiStage(f); };
	expect(w(true, true, false, 100) == Wifi::Online, "an address: online");
	expect(w(false, true, false, -1) == Wifi::ComingUp || w(false, true, false, -1) == Wifi::Down, "an address on a link that's down isn't online");
	expect(w(true, false, false, 100) == Wifi::ComingUp, "associated, no address: coming");
	expect(w(false, false, true, -1) == Wifi::ComingUp, "back from a sleep: coming");
	expect(w(false, false, false, 3) == Wifi::ComingUp, "wpa_supplicant just started: coming");
	expect(w(false, false, false, 100) == Wifi::Down, "no network in reach: not coming");
	{
		WifiFacts f;
		f.busy = true;
		expect(wifiStage(f) == Wifi::ComingUp, "wifi up under way: coming");
	}
	expect(w(false, false, false, -1) == Wifi::Down, "Wi-Fi off: not coming");

	std::string conf = d + "/romsync.json", roms = d + "/roms", rom = roms + "/gba/a.gba", once = d + "/resume-once";
	std::ofstream(conf) << "{\"backend\": \"romm\", \"token\": \"t\"}\n";
	expect(wanted("quit", "on", conf, roms, rom, once), "RomM, a game on the card: it waits");
	expect(wanted("both", "on", conf, roms, rom, once), "Auto sync both: it waits");
	expect(!wanted("quit", "", conf, roms, rom, once), "Sync before playing not set: off, its default");
	expect(!wanted("off", "on", conf, roms, rom, once), "Auto sync off");
	expect(!wanted("quit", "off", conf, roms, rom, once), "Sync before playing off");
	expect(!wanted("quit", "on", conf, roms, "/run/banana/sd2/roms/gba/a.gba", once), "a game outside the ROMs folder");
	expect(!wanted("quit", "on", conf, roms, roms + "2/gba/a.gba", once), "a folder that only starts like the ROMs folder");
	expect(wanted("quit", "on", conf, roms, "/run/sd2/roms/gba/a.gba", once, "/run/sd2/roms"), "a game on the second SD card, while it is in");
	std::ofstream(once) << rom << "\n";
	expect(!wanted("quit", "on", conf, roms, rom, once), "the resume launch");
	std::ofstream(once) << roms << "/gba/b.gba\n";
	expect(wanted("quit", "on", conf, roms, rom, once), "another game's resume marker");
	unlink(once.c_str());
	std::ofstream(conf) << "{\"backend\": \"romhost\", \"token\": \"t\"}\n";
	expect(!wanted("quit", "on", conf, roms, rom, once), "Romhost");
	unlink(conf.c_str());
	expect(!wanted("quit", "on", conf, roms, rom, once), "not paired");

	int fd = holdSaves(d);
	expect(fd >= 0, "the lock taken");
	expect(!lockFree(d), "the lock held");
	expect(holdSaves(d) < 0, "a lock already held isn't waited for");
	{
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		int again = holdSaves(d, 300);
		clock_gettime(CLOCK_MONOTONIC, &t1);
		double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
		expect(again < 0 && ms >= 300 && ms < 2000, "a lock held throughout is waited for as long as asked, then given up");
	}
	// (a game outliving its launcher's shell, or the menu's end, must not hold the saves: nothing past exec has it)
	pid_t p = fork();
	if (p == 0) { execl("/bin/sh", "sh", "-c", "sleep 1", (char*)nullptr); _exit(127); }
	usleep(200000);
	releaseSaves(fd);
	expect(lockFree(d), "free once let go, a program started meanwhile still running");
	waitpid(p, nullptr, 0);

	// a mark of a newer save not fetched, left by an earlier game's sync: it says nothing of this game, and a B while
	// Wi-Fi comes up (before this game's sync has started) asks nothing of it
	{
		std::string older = d + "/launch.older";
		std::ofstream(older) << "";
		Wait wt;
		wt.wifi = [] { WifiFacts f; f.operUp = true; return f; };	// (associated, no address yet: coming)
		wt.begin("/bin/true", rom, d, 0);
		expect(wt.active() && access(older.c_str(), F_OK) != 0, "an earlier game's mark gone when the wait begins");
		wt.press('b');
		std::string l1, l2;
		expect(!wt.active() && !wt.text(l1, l2), "a B before the sync, a stale mark: no question");
	}
	// a press on the question changes what is shown, and the next step says so: the menu redraws on the step's word
	// alone, and without it the question stayed on the panel while the choice was carried out
	{
		std::string tool = d + "/sync-tool";
		std::ofstream(tool) << "#!/bin/sh\n# the sync before a game: a conflict held, its line in launch.conflict; resolve: settled\n"
			"case \"$*\" in\n*' resolve '*) exit 0 ;;\n"
			"*) printf 'conflict\\t5/autosave\\tsaves/gba/a.srm\\t\\taa\\t8192\\tbb\\t8192\\t\\t\\t/v/a.srm\\tboth\\tgba/a.gba\\tboth\\n' > \"$2/launch.conflict\"\n"
			"   echo 'done 0 0 0 0 1 0|' > \"$2/launch.status\" ;;\nesac\n";
		chmod(tool.c_str(), 0755);
		Wait wt;
		wt.wifi = [] { WifiFacts f; f.operUp = f.hasAddr = true; return f; };
		double t = 0;
		wt.begin(tool, rom, d, t);
		std::string l1, l2;
		for (int i = 0; i < 200 && !(wt.text(l1, l2) && l2.compare(0, 2, "A:") == 0); i++) { usleep(20000); wt.step(t += 20); }
		expect(l2.compare(0, 2, "A:") == 0, "the conflict asked about");
		expect(!wt.step(t += 20), "a step with nothing new says nothing");
		wt.press('a');
		expect(wt.active() && wt.step(t += 20), "the step after a press says what is shown changed");
		for (int i = 0; i < 200 && wt.active(); i++) { usleep(20000); wt.step(t += 20); }
		expect(!wt.active(), "the choice carried out, the wait over");
	}
	// a sync that won't stop (a card too busy to finish placing): a B or the cap doesn't hold the menu's loop while it is
	// waited for, it is killed with what it started once its grace is over, and the notice that nothing synced is timed
	// from then, so it is still there to be drawn
	for (int cap = 0; cap < 2; cap++)
	{
		std::string tool = d + "/stuck-tool", kid = d + "/stuck-kid";
		unlink(kid.c_str());
		std::ofstream(tool) << "#!/bin/sh\ntrap '' TERM\nsleep 60 & echo $! > '" + kid + "'\nwhile :; do sleep 0.05; done\n";
		chmod(tool.c_str(), 0755);
		Wait wt;
		wt.wifi = [] { WifiFacts f; f.operUp = f.hasAddr = true; return f; };
		double t = 0;
		wt.begin(tool, rom, d, t);
		for (int i = 0; i < 100 && access(kid.c_str(), F_OK) != 0; i++) usleep(20000);
		int kp = 0;
		std::ifstream(kid) >> kp;
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		if (cap) wt.step(t += Wait::CAP);
		else wt.press('b');
		clock_gettime(CLOCK_MONOTONIC, &t1);
		double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
		expect(ms < 500 && wt.active(), cap ? "the cap doesn't wait for the sync in the menu's loop" : "a B doesn't wait for the sync in the menu's loop");
		wt.step(t += 20);
		wt.step(t += Wait::STOP_GRACE);	// (killed now)
		for (int i = 0; i < 100 && wt.active(); i++)
		{
			usleep(20000);
			wt.step(t += 20);
			std::string l1, l2;
			if (cap && wt.text(l1, l2) && l1 == "Not synced yet") break;
		}
		std::string l1, l2;
		if (cap) expect(wt.active() && wt.text(l1, l2) && l1 == "Not synced yet", "the notice after the cap is still there to be drawn");
		else expect(!wt.active(), "the game plays once the sync is gone");
		usleep(100000);
		// (gone, or a zombie no one reaps: a container's first process may not)
		std::string stat;
		std::getline(std::ifstream("/proc/" + std::to_string(kp) + "/stat"), stat);
		size_t rp = stat.rfind(')');
		expect(kp > 0 && (kill(kp, 0) != 0 || (rp != std::string::npos && stat.compare(rp + 2, 1, "Z") == 0)), "what the sync started is killed with it");
	}
	// a DS game's end told to nds-saves in the background, and the next game's start waiting for it: a stand-in that
	// takes its time has written its record by the time gamePlayedDone returns
	{
		std::string tool = d + "/nds-saves-slow", done = d + "/played.done";
		std::ofstream(tool) << "#!/bin/sh\nsleep 0.5\necho \"$*\" > '" << done << "'\n";
		chmod(tool.c_str(), 0755);
		unlink(done.c_str());
		setenv("BANANA_NDS_SAVES", tool.c_str(), 1);
		expect(!gamesync::gamePlayed("gba", "/roms/gba/Game.gba"), "a GBA game's end isn't told to nds-saves");
		expect(gamesync::gamePlayed("nds", "/roms/nds/Game.nds"), "a DS game's end is told to nds-saves");
		expect(access(done.c_str(), F_OK) != 0, "the telling runs in the background");
		gamesync::gamePlayedDone();
		std::string line;
		std::getline(std::ifstream(done), line);
		expect(line == "/roms/nds/Game.nds --played", "the next game's start waits for the telling to end");
		gamesync::gamePlayedDone();	// (nothing to wait for: returns at once)
		unsetenv("BANANA_NDS_SAVES");
	}
	if (!fails) fprintf(stderr, "gamesync-test: all checks passed\n");
	return fails ? 1 : 0;
}
