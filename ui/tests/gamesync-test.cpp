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
	expect(wanted("both", "", conf, roms, rom, once), "Auto sync both, the setting missing: it waits");
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
	if (!fails) fprintf(stderr, "gamesync-test: all checks passed\n");
	return fails ? 1 : 0;
}
