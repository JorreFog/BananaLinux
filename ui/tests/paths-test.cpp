// where a game's save is (save_paths.cpp): each system's save folder and extension, and the emulator it is sent as,
// from the core it runs; the setting that picks a core; RetroArch's own settings; which ROM on the card is the
// server's, by content. Run by tests/paths-test.sh, which installs the test cores: paths-test <dir>
#include "save_paths.h"
#include "sync_core.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>
using namespace bsync;
static int fails;
static void expect(bool got, bool want, const std::string& what) { if (got != want) { fprintf(stderr, "FAIL %s: %s\n", what.c_str(), got ? "yes" : "no"); fails++; } }
static void same(const std::string& got, const std::string& want, const std::string& what)
{
	if (got != want) { fprintf(stderr, "FAIL %s: \"%s\", not \"%s\"\n", what.c_str(), got.c_str(), want.c_str()); fails++; }
}
static void put(const std::string& p, const std::string& data) { mkdirs(dirOf(p)); writeAtomic(p, data); }
static bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: paths-test <dir> (tests/paths-test.sh)\n"); return 2; }
	std::string D = argv[1], LIB = D + "/lib";
	ROOT = D + "/card"; ROMS = ROOT + "/roms"; SETTINGS = ROOT + "/banana/settings.conf";
	mkdirs(ROOT + "/banana");

	// each system's folder (the DS's and the GBA's own, the flat saves/ for every other RetroArch system) and label,
	// from systems.conf's core
	struct { const char* system; const char* rel; const char* label; } want[] = {
		{ "nds", "saves/nds/G.sav", "melonds" }, { "gba", "saves/gba/G.srm", "gpsp" }, { "gb", "saves/G.srm", "gambatte" },
		{ "gbc", "saves/G.srm", "gambatte" }, { "nes", "saves/G.srm", "banananes" }, { "snes", "saves/G.srm", "snes9x" },
		{ "megadrive", "saves/G.srm", "picodrive" }, { "psx", "saves/G.srm", "pcsx_rearmed" }, { "n64", "saves/G.srm", "mupen64plus_next" },
	};
	for (auto& w : want)
	{
		SysSaves s = systemSaves(w.system);
		same((s.known() ? s.rel("G") : "") + " " + s.label + s.err, std::string(w.rel) + " " + w.label, std::string(w.system) + "'s saves");
	}
	SysSaves none = systemSaves("ports");
	expect(!none.known() && !none.err.empty(), true, "a system that isn't a RetroArch core's has no folder: " + none.err);

	// the setting: an installed core (the image's, or one on the card) over systems.conf's; one not installed ignored
	put(SETTINGS, "core.gba = mgba\ncore.snes = nothere\ncore.nes = nestopia\nnds_emulator = bananads\n");
	same(systemSaves("snes").label, "snes9x", "core.snes not installed: systems.conf's core");
	same(systemSaves("gba").rel("G") + " " + systemSaves("gba").label, "saves/gba/G.srm mgba", "gba is saves/gba whichever core");
	same(systemSaves("nes").rel("G") + " " + systemSaves("nes").label, "saves/G.srm nestopia", "a core on the card, flat as the image's");
	same(systemSaves("nds").label, "melonds", "the DS's saves sent as melonDS's, with either DS emulator");
	std::string err;
	same(coreFor("nes", err), ROOT + "/retroarch/cores/nestopia_libretro.so", "a core on the card (RetroArch's Core Downloader)");
	unlink((D + "/lib/snes9x_libretro.so").c_str());
	put(SETTINGS, "");
	SysSaves s = systemSaves("snes");
	expect(!s.known() && has(s.err, "snes9x isn't installed"), true, "no core installed: " + s.err);
	put(D + "/lib/snes9x_libretro.so", "");

	// RetroArch's settings, as run-retroarch and RetroArch leave them: sorting by core makes no difference (run-retroarch
	// turns it off at every start, saves-flat empties the core's folders), the save folder does
	put(ROOT + "/retroarch/retroarch.cfg", "savefile_directory = \"/data/saves\"\nsort_savefiles_enable = \"false\"\n");
	same(systemSaves("snes").rel("G"), "saves/G.srm", "RetroArch's settings as the image has them");
	put(ROOT + "/retroarch/retroarch.cfg", "savefile_directory = \"/data/saves/\"\nsort_savefiles_enable = \"true\"\n");
	same(systemSaves("snes").rel("G"), "saves/G.srm", "a trailing slash; sorting on from before 0.5.3, until the next game");
	put(ROOT + "/retroarch/retroarch.cfg", "savefile_directory = \"/data/saves/gba\"\nsort_savefiles_enable = \"false\"\n");
	s = systemSaves("snes");
	expect(!s.known() && has(s.err, "/data/saves/gba"), true, "the GBA game's folder left behind: " + s.err);
	same(systemSaves("gba").rel("G") + " " + systemSaves("nds").rel("G"), "saves/gba/G.srm saves/nds/G.sav", "gba and nds whatever RetroArch's settings");
	put(ROOT + "/retroarch/retroarch.cfg", "sort_savefiles_enable = \"false\"\n");
	s = systemSaves("snes");
	expect(!s.known() && has(s.err, "beside the games"), true, "no save folder (RetroArch's default): " + s.err);
	unlink((ROOT + "/retroarch/retroarch.cfg").c_str());

	// the ROMs on the card
	same(romFolder("gba") + "," + romFolder("megadrive") + "," + romFolder("dodakvarter"), "gba,megadrive,", "ROM folders");
	put(ROMS + "/gba/Banana Run (USA).gba", "gba rom banana");
	put(ROMS + "/gba/Hacks/Peel (USA).gba", "gba rom peel");
	put(ROMS + "/gba/media/box/Banana Run (USA).png", "png");
	put(ROMS + "/gba/.hidden/Banana Run (USA).gba", "hidden");
	auto idx = romIndex("gba");
	same(std::to_string(idx.size()) + " " + idx["Banana Run (USA).gba"] + " " + idx["Peel (USA).gba"], "2 gba/Banana Run (USA).gba gba/Hacks/Peel (USA).gba", "the GBA folder's ROMs");

	// which is the server's
	Conf c;
	RomFile plain; plain.md5 = hashStr("gba rom banana", "md5"); plain.sha1 = hashStr("gba rom banana", "sha1");
	expect(romMatches("gba/Banana Run (USA).gba", { plain }, c), true, "a ROM with the server's MD5");
	expect(exists(ROOT + "/banana/rom-hashes.tsv"), false, "the hashes kept only when asked to");
	savePathCaches();
	expect(has(readFile(ROOT + "/banana/rom-hashes.tsv"), "gba/Banana Run (USA).gba\t14\t"), true, "its MD5 kept in banana/rom-hashes.tsv");
	RomFile other; other.md5 = hashStr("another dump", "md5"); other.sha1 = hashStr("another dump", "sha1");
	expect(romMatches("gba/Banana Run (USA).gba", { other }, c), false, "the same name, other bytes");
	c.files["gba/Banana Run (USA).gba"] = other.sha1;
	expect(romMatches("gba/Banana Run (USA).gba", { other }, c), true, "installed by banana-sync from that ROM (its SHA-1)");
	c.files["gba/Banana Run (USA).gba"] = "md5:" + other.md5;
	RomFile md5only; md5only.md5 = other.md5;
	expect(romMatches("gba/Banana Run (USA).gba", { md5only }, c), true, "installed by banana-sync from that ROM (its MD5)");
	c.files.clear();

	// a zip: its members by size and CRC32, from its own directory
	auto zm = zipMembers(D + "/game.zip");
	same(std::to_string(zm.size()) + (zm.size() == 2 ? " " + std::to_string(zm[0].first) + "/" + zm[0].second + " " + std::to_string(zm[1].first) + "/" + zm[1].second : ""),
		"2 4500/eff6b43b 130/9cb81542", "a zip's members, its folder left out");
	expect(zipMembers(ROMS + "/gba/Banana Run (USA).gba").empty(), true, "not a zip: no members");
	{
		std::ifstream src(D + "/game.zip", std::ios::binary);
		put(ROMS + "/gba/Zip Game (USA).zip", std::string(std::istreambuf_iterator<char>(src), {}));
	}
	RomFile z; z.md5 = "b9b165930ad554b5506ab32218751cd7"; z.members = { { 130, "9CB81542" }, { 4500, "eff6b43b" } };
	expect(romMatches("gba/Zip Game (USA).zip", { z }, c), true, "a zip holding the server's members");
	z.members[1].second = "00000000";
	expect(romMatches("gba/Zip Game (USA).zip", { z }, c), false, "a zip with another member");
	RomFile raw; raw.md5 = hashFile(ROMS + "/gba/Zip Game (USA).zip", "md5");
	expect(romMatches("gba/Zip Game (USA).zip", { raw }, c), true, "a zip the server couldn't read: the file's own MD5");

	// a disc set: every disc the .m3u lists
	put(ROMS + "/psx/.discs/Disc Game (USA)/Disc 1.chd", "disc one");
	put(ROMS + "/psx/.discs/Disc Game (USA)/Disc 2.chd", "disc two");
	put(ROMS + "/psx/Disc Game (USA).m3u", ".discs/Disc Game (USA)/Disc 1.chd\n.discs/Disc Game (USA)/Disc 2.chd\n");
	RomFile d1, d2; d1.md5 = hashStr("disc one", "md5"); d2.md5 = hashStr("disc two", "md5");
	expect(romMatches("psx/Disc Game (USA).m3u", { d1, d2 }, c), true, "a disc set whose discs are the server's");
	expect(romMatches("psx/Disc Game (USA).m3u", { d1 }, c), false, "a disc set with a disc the server hasn't");
	put(ROMS + "/psx/Empty (USA).m3u", "# nothing\n");
	expect(romMatches("psx/Empty (USA).m3u", { d1 }, c), false, "an .m3u listing no disc");
	put(ROMS + "/psx/Out (USA).m3u", "../../banana/romsync.json\n");
	expect(romMatches("psx/Out (USA).m3u", { d1 }, c), false, "an .m3u naming a file outside the roms folder");

	if (!fails) fprintf(stderr, "paths-test: all passed\n");
	return fails ? 1 : 0;
}
