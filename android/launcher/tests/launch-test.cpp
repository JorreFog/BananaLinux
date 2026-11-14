// The launch table's reader (ui/src/android_launch.cpp) on a PC:
//   sh run.sh (or: c++ -std=c++17 -I../../../ui/src -o launch-test launch-test.cpp ../../../ui/src/android_launch.cpp && ./launch-test)
#include "android_launch.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace banana::launch;

static int failed = 0;
static void check(bool ok, const char* what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok) failed++;
}

static bool has(const std::vector<std::string>& v, const std::string& s)
{
	for (auto& x : v) if (x == s) return true;
	return false;
}

int main(int argc, char** argv)
{
	std::string dir = argc > 1 ? argv[1] : ".";
	Table t;
	check(t.loadAll(dir + "/sample-launch.conf", dir + "/card-launch.conf") == 2, "two layers read (no /vendor or /system_ext on a PC)");
	check(t.size() == 8, "eight sections ([broken] has no package and is left out)");
	bool warnedPkg = false, warnedData = false;
	for (auto& w : t.warnings) { if (w.find("[broken] has no package") != std::string::npos) warnedPkg = true; if (w.find("bogus") != std::string::npos) warnedData = true; }
	check(warnedPkg, "a section with no package is warned about");
	check(warnedData, "a bad data value is warned about");

	const Entry* gba = t.find("gba");
	check(gba && gba->package == "com.retroarch.aarch64" && gba->action == "android.intent.action.MAIN", "gba: package and action");
	check(gba && gba->flags.size() == 2 && gba->flags[1] == "FLAG_ACTIVITY_CLEAR_TASK", "gba: flags split");
	check(gba && gba->extras.size() == 3 && gba->extras[0].first == "ROM", "gba: extras in the file's order");
	std::string core = coreOf("/usr/libexec/banana/run-retroarch mgba %ROM%", "");
	check(core == "mgba", "the core from the run line");
	check(coreOf("/usr/libexec/banana/run-retroarch mgba %ROM%", "gpsp") == "gpsp", "the card's core.<id> wins");
	check(coreOf("/usr/libexec/banana/run-ppsspp %ROM%", "") == "", "no core for a standalone emulator");
	auto spec = intentSpec(*gba, "/storage/ABCD-1234/roms/gba/Game (USA).gba", core);
	check(has(spec, "extra.ROM=/storage/ABCD-1234/roms/gba/Game (USA).gba"), "%ROM% put in");
	check(has(spec, "extra.LIBRETRO=/data/data/com.retroarch.aarch64/cores/mgba_libretro_android.so"), "%CORE% put in");
	check(has(spec, "flags=FLAG_ACTIVITY_NEW_TASK FLAG_ACTIVITY_CLEAR_TASK") && has(spec, "data=none"), "flags and the default data");

	const Entry* psp = t.find("psp");
	check(psp && psp->package == "org.ppsspp.ppssppgold" && psp->activity.empty(), "the card's [psp] replaces the ROM's whole");
	check(psp && psp->data == "none", "a bad data value leaves the default");
	const Entry* nds = t.find("nds");
	auto ns = intentSpec(*nds, "/sdcard/roms/nds/a.nds", "");
	check(has(ns, "data=content") && has(ns, "extra.uri=%ROMURI%"), "%ROMURI% left for the app (it makes the content URI)");
	check(!t.find("snes"), "a system with no entry: none");

	// the additions: core =, URI templates, %ROMTEXT%, %ROMKEY%, variants, banana:<page>, none
	std::string tmp = (getenv("TMPDIR") ? std::string(getenv("TMPDIR")) : std::string("/tmp")) + "/launch-test-";
	FILE* f = fopen((tmp + "Game.pc").c_str(), "w");
	if (f) { fputs("exe = Game.exe\nsteam = 1145360\n", f); fclose(f); }
	f = fopen((tmp + "monkey.scummvm").c_str(), "w");
	if (f) { fputs("\n  monkey1  \n", f); fclose(f); }
	auto pc = intentSpec(*t.find("pc"), tmp + "Game.pc", "");
	check(has(pc, "data=gamenative://run?appid=1145360&gamesource=STEAM"), "%ROMKEY:steam% from a .pc file, in a data template");
	const Entry* sv = t.find("scummvm");
	check(sv && coreOf("", "", sv->core) == "scummvm" && coreOf("", "x", sv->core) == "x", "core = (the card's core.<id> before it)");
	check(has(intentSpec(*sv, "/r", coreOf("", "", sv->core)), "extra.LIBRETRO=scummvm"), "%CORE% from core =");
	const Entry* st = t.find("scummvm", "standalone");
	check(st && st->package == "org.scummvm.scummvm", "a variant: [scummvm.standalone]");
	check(t.find("scummvm", "nope") == sv, "an unknown variant: the plain section");
	check(has(intentSpec(*st, tmp + "monkey.scummvm", ""), "data=scummvm:monkey1"), "%ROMTEXT%: the first non-empty line, trimmed");
	check(pageOf(*t.find("files")) == "files" && pageOf(*sv).empty(), "banana:<page>");
	check(!t.find("ports") && t.hidden("ports") && !t.hidden("files"), "package = none: hidden, no entry");
	remove((tmp + "Game.pc").c_str());
	remove((tmp + "monkey.scummvm").c_str());

	// the real table (android/system/launch.conf) against the Thor's systems
	Table real;
	const std::string repo = dir + "/../../..";
	if (real.load(repo + "/android/system/launch.conf"))
	{
		for (auto& w : real.warnings) printf("     warning: %s\n", w.c_str());
		check(real.warnings.empty(), "android/system/launch.conf reads with no warnings");
		std::string out = tmp + "systems.conf";
		check(filterSystems(real, repo + "/buildroot/board/thor/systems.conf", out), "systems.conf filtered");
		FILE* sf = fopen(out.c_str(), "r");
		int sections = 0, missing = 0;
		std::string none;
		char line[512];
		while (sf && fgets(line, sizeof line, sf))
			if (line[0] == '[')
			{
				std::string id(line + 1, strcspn(line + 1, "]"));
				sections++;
				if (!real.find(id)) { missing++; none += " " + id; }
			}
		if (sf) fclose(sf);
		remove(out.c_str());
		printf("     %d systems on Android's shelves%s%s\n", sections, missing ? ", with no entry:" : "", none.c_str());
		check(sections > 40 && missing == 0, "every system left on the shelves has an entry");
	}
	else printf("     (android/system/launch.conf not there: skipped)\n");
	printf("%s\n", failed ? "launch-test: FAILED" : "launch-test: all passed");
	return failed ? 1 : 0;
}
