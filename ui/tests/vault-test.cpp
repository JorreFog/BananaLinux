// the save sync's local safety layer on its own: which saves are blank (save_adapt.cpp), on the fresh saves each
// shipped core writes (measured in a chroot of the image, and read from each core's source at its pinned commit);
// which server saves a system takes, at which sizes, a DS save's DeSmuME footer cut off;
// the vault and the journal (save_vault.cpp) and the atomic write under them on a throwaway card, vault-test <dir>
#include "save_adapt.h"
#include "save_vault.h"
#include "sync_core.h"
#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
using namespace bsync;
static int fails;
static void expect(bool got, bool want, const char* what) { if (got != want) { fprintf(stderr, "FAIL %s: %s\n", what, got ? "yes" : "no"); fails++; } }
static std::string fill(size_t n, int c) { return std::string(n, (char)c); }
static std::string poked(std::string s, size_t at) { s[at] ^= 0x01; return s; }

// Mupen64Plus-Next's fresh save, as format_saved_memory() writes it: 0xFF regions, four formatted paks
static std::string n64Fresh()
{
	std::string b = fill(0x800, 0xff);
	for (int k = 0; k < 4; k++)
	{
		std::string p(0x8000, '\0');
		std::string id(32, '\0');
		for (int i = 0; i < 24; i++) id[i] = (char)(0x11 * (k + 1) + i);	// (the serial: random per load)
		id[25] = 1; id[26] = 1; id[27] = 1;
		for (int blk : { 1, 3, 4, 6 }) p.replace(blk * 32, 32, id);
		for (int i = 5; i < 128; i++) { p[256 + 2 * i + 1] = 3; p[512 + 2 * i + 1] = 3; }
		p[256 + 1] = p[512 + 1] = (char)(123 * 3);
		b += p;
	}
	return b + fill(0x8000 + 0x20000, 0xff);
}

static int count(const std::string& dir)
{
	int n = 0;
	if (DIR* d = opendir(dir.c_str())) { while (dirent* e = readdir(d)) n += e->d_name[0] != '.'; closedir(d); }
	return n;
}
static void put(const std::string& rel, const std::string& data) { writeAtomic(ROOT + "/" + rel, data); }

static void vault()
{
	const std::string rel = "saves/Peel Kart (USA).srm", game = vaultDir() + "/saves/Peel Kart (USA)";
	put(rel, "progress 1");
	std::string kept, kept2, err;
	expect(vaultKeep(rel, ROOT + "/" + rel, kept, err), true, "a save kept in the vault");
	expect(readFile(kept) == "progress 1" && dirOf(kept) == game, true, "the copy where the save's folder and stem say");
	expect(kept.size() > 13 && kept.compare(kept.size() - 13, 13, "-" + hashStr("progress 1", "md5").substr(0, 8) + ".srm") == 0, true, "named by its MD5");
	expect(vaultKeep(rel, ROOT + "/" + rel, kept2, err) && kept2 == kept && count(game) == 1, true, "the same bytes kept once");
	put(rel, "progress 2");
	expect(vaultKeep(rel, ROOT + "/" + rel, kept2, err) && kept2 != kept && count(game) == 2, true, "other bytes kept beside");
	expect(!vaultKeep(rel, ROOT + "/saves/none.srm", kept2, err), true, "a missing file is not kept");
	expect(!vaultKeep("../x.srm", ROOT + "/" + rel, kept2, err), true, "a path outside the card is refused");

	expect(vaultGood(rel, "progress 2") && readFile(game + "/last-good.srm") == "progress 2", true, "the last good copy");
	expect(!vaultGood(rel, std::string(8192, 0x60)) && readFile(game + "/last-good.srm") == "progress 2", true, "a blank never replaces the last good copy");

	// a vault that can't be written (here a file where its folder goes; a full card the same): nothing kept, an error
	put("saves/gba/Banana Run (USA).srm", "gba");
	writeAtomic(vaultDir() + "/saves/gba", "in the way");
	expect(!vaultKeep("saves/gba/Banana Run (USA).srm", ROOT + "/saves/gba/Banana Run (USA).srm", kept2, err) && !err.empty(), true, "a vault that can't be written fails");

	// nothing in the vault is a save the sync sees
	for (auto& kv : localSaves(true)) expect(kv.first.find("save-vault") != std::string::npos, false, ("listed: " + kv.first).c_str());
	expect(saveKind("banana/save-vault/saves/Peel Kart (USA)/last-good.srm").empty(), true, "the vault's kind");
}

static void journal()
{
	const std::string a = "saves/gba/A.srm", b = "saves/gba/B.srm", c = "saves/gba/C.srm", d = "saves/gba/D.srm";
	put(a, "a before"); put(b, "b after"); put(c, "c played since"); put(d + ".new", "half");
	std::string ha = hashStr("a before", "md5"), hb = hashStr("b after", "md5");
	std::string ia = journalBegin({ "install", a, ha, "1", "x", "v", hashStr("a after", "md5") });
	std::string ib = journalBegin({ "install", b, hashStr("b before", "md5"), "2", "x", "v", hb });
	std::string ic = journalBegin({ "install", c, hashStr("c before", "md5"), "3", "x", "v", hashStr("c after", "md5") });
	journalBegin({ "install", d, "", "4", "x", "", hashStr("d", "md5") });
	std::string ie = journalBegin({ "install", "saves/gba/E.srm", "", "5", "x", "", "e" });
	journalEnd(ie, "ok", "e");
	expect(!ia.empty() && ia != ib, true, "begin lines written, each its own id");
	expect(journalBegin({ "install", "saves/gba/a\tb.srm", "", "", "", "", "" }).empty(), true, "a tab in a name: not journalled, so not run");
	std::string r = journalRecover();
	expect(r.find("unchanged " + a + "\n") != std::string::npos, true, "killed before the install: unchanged");
	expect(r.find("installed " + b + "\n") != std::string::npos, true, "killed after the install: installed");
	expect(r.find("changed-since " + c + "\n") != std::string::npos, true, "neither: changed since, left as it is");
	expect(readFile(ROOT + "/" + c) == "c played since", true, "a changed save kept");
	expect(r.find("unchanged " + d + "\n") != std::string::npos && !exists(ROOT + "/" + d + ".new"), true, "no save before or after, the .new gone");
	expect(r.find("E.srm") == std::string::npos, true, "an ended operation not recovered");
	expect(journalRecover().find("saves/gba/") == std::string::npos, true, "recovered once");

	// the power lost while a line was written: the torn line has no newline, and the next begin isn't joined to it
	put("saves/gba/F.srm", "f before");
	{ std::ofstream(journalPath(), std::ios::app) << "begin\t1.2.3\tinstall\tsaves/gba/F"; }
	journalBegin({ "install", "saves/gba/F.srm", hashStr("f before", "md5"), "6", "x", "v", "f" });
	expect(journalRecover().find("unchanged saves/gba/F.srm\n") != std::string::npos, true, "a begin after a torn line recovered");
}

// DeSmuME's footer on a DS save (as DraStic writes it; short: an older DraStic's, without the line saying where to
// cut), its padded size and version as given
static std::string footed(const std::string& raw, bool shortForm = false, long long pad = -1, unsigned version = 0)
{
	std::string f = shortForm ? "" : "|<--Snip above here to create a raw sav by excluding this DeSmuME savedata footer:";
	auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) f += (char)(v >> (8 * i)); };
	put32((uint32_t)raw.size()); put32((uint32_t)(pad < 0 ? (long long)raw.size() : pad)); put32(0); put32(2); put32((uint32_t)raw.size()); put32(version);
	return raw + f + "|-DESMUME SAVE-|";
}
// adaptIn on a copy: "" when taken, else why not (out: the bytes taken)
static std::string taken(const std::string& sys, std::string b, const std::string* local = nullptr, std::string* out = nullptr)
{
	std::string why = adaptIn(sys, b, local);
	if (out) *out = b;
	return why;
}
static void adapt()
{
	expect(wireOk("nds", "Peel Quest (USA) [2026-10-30_12-00-00].dsv", "sav") && wireOk("nds", "Peel Quest (USA).SAV", "sav"), true, "a DS save as .dsv (RomM's time tag in its name) or .SAV");
	expect(wireOk("gba", "Banana Run (USA).gba.sav", "srm") && wireOk("gba", "x.srm", "srm"), true, "a GBA save as Grout's .gba.sav, or .srm");
	expect(wireOk("psx", "x.mcd", "srm") && wireOk("psx", "x.mcr", "srm") && wireOk("snes", "x.sav", "srm"), true, "a memory card as .mcd or .mcr, a SNES .sav");
	expect(wireOk("n64", "x.eep", "srm") || wireOk("n64", "x.sav", "srm"), false, "a standalone N64 emulator's save");
	expect(wireOk("gba", "x.zip", "srm") || wireOk("pico-8", "x.zip", "zip") || wireOk("gb", "x.rtc", "srm"), false, "a folder save's zip, a Game Boy clock");
	expect(wireOk("pico-8", "x.srm", "srm") && !wireOk("pico-8", "x.sav", "srm"), true, "a system with no rule: the card's own extension only");
	expect(maxSave("nds") == (8 << 20) + 122 && maxSave("nes") == 32 << 10 && maxSave("psx") == 131072 && maxSave("pico-8") == -1, true, "the largest save each system takes");

	std::string raw = fill(8192, 0x5a), got, loc8 = fill(8192, 1), loc64 = fill(65536, 1);
	expect(taken("nds", footed(raw), &loc8, &got) == "" && got == raw, true, "DraStic's .dsv: the raw save under its 122-byte footer");
	expect(taken("nds", footed(raw, true), nullptr, &got) == "" && got == raw, true, "an older DraStic's 40-byte footer");
	expect(taken("nds", raw, nullptr, &got) == "" && got == raw, true, "a raw DS save as it is");
	expect(taken("nds", footed(raw, false, -1, 1)) != "", true, "a footer of another version");
	expect(taken("nds", footed(raw, false, 8196)) != "", true, "a footer whose padded size doesn't add up");
	expect(taken("nds", footed(raw), &loc64) != "", true, "a DS save of another size than the card's");
	expect(taken("nds", fill(512, 1)) == "" && taken("nds", fill(8 << 20, 1)) == "", true, "DS saves of 512 B and 8 MiB");
	expect(taken("nds", fill(256, 1)) != "" && taken("nds", fill(16 << 20, 1)) != "", true, "DS saves of 256 B or 16 MiB");
	expect(taken("nds", fill(8000, 1)) != "", true, "a DS save not a power of two");

	std::string k32 = fill(32 << 10, 1), k128 = fill(128 << 10, 1), padded = k32 + fill(96 << 10, 0xff);
	expect(taken("gba", k128, &k32) == "", true, "gpSP's 128 KiB over mGBA's 32 KiB");
	expect(taken("gba", k32, &padded) == "", true, "mGBA's 32 KiB over gpSP's padded 128 KiB");
	expect(taken("gba", k32, &k128) != "", true, "32 KiB over 128 KiB whose tail holds data");
	expect(taken("gba", fill(8 << 10, 1), &k32) != "", true, "8 KiB over 32 KiB");
	expect(taken("gba", fill(512, 1)) == "" && taken("gba", fill(64 << 10, 1)) == "", true, "GBA EEPROM and Flash sizes");
	expect(taken("gba", fill(16 << 10, 1)) != "" && taken("gba", fill(256 << 10, 1)) != "", true, "16 KiB or 256 KiB for a GBA");
	expect(taken("gba", k128 + fill(16, 2), &k128, &got) == "" && got == k128, true, "standalone mGBA's clock after a 128 KiB save left off");
	expect(taken("gba", fill(8 << 10, 1) + fill(16, 2), nullptr, &got) == "" && got == fill(8 << 10, 1), true, "the clock after an 8 KiB save left off");
	expect(taken("gba", fill((8 << 10) + 15, 1)) != "" && taken("gba", fill(16, 1)) != "", true, "8 KiB and 15 bytes, or the clock alone");
	expect(maxSave("gba") == (128 << 10) + 16, true, "the largest GBA save: 128 KiB and mGBA's clock");

	expect(taken("gb", fill(8 << 10, 1)) == "" && taken("gbc", fill(128 << 10, 1)) == "", true, "Game Boy RAM of 8 and 128 KiB");
	expect(taken("gb", fill(512, 1)) == "" && taken("gb", fill(2 << 10, 1)) == "" && taken("gbc", fill(32 << 10, 1)) == "" && taken("gbc", fill(64 << 10, 1)) == "", true,
		"Game Boy saves another emulator makes: MBC2's 512 bytes, 2, 32 and 64 KiB");
	expect(taken("gb", fill(16 << 10, 1)) != "" && taken("gb", "") != "" && taken("gb", fill((32 << 10) + 48, 1)) != "", true, "16 KiB, nothing, or a clock after a Game Boy save");
	expect(taken("gb", fill(8 << 10, 1), &k32) != "", true, "a Game Boy save of another size than the card's");
	expect(taken("nes", fill(128, 1)) == "" && taken("nes", fill(32 << 10, 1)) == "", true, "NES RAM of 128 B and 32 KiB");
	expect(taken("nes", fill(127, 1)) != "" && taken("nes", fill((32 << 10) + 1, 1)) != "", true, "NES RAM out of range");
	expect(taken("snes", fill(1 << 10, 1)) == "" && taken("snes", fill(128 << 10, 1)) == "", true, "SNES SRAM of 1 and 128 KiB");
	expect(taken("snes", fill(512, 1)) != "" && taken("snes", fill(3 << 10, 1)) != "", true, "SNES SRAM not 1 KiB << n");
	expect(taken("megadrive", fill(64 << 10, 1)) == "" && taken("megadrive", fill(2, 1)) == "", true, "Mega Drive SRAM, even sizes to 64 KiB");
	expect(taken("megadrive", fill(8191, 1)) != "" && taken("megadrive", fill((64 << 10) + 2, 1)) != "", true, "an odd Mega Drive size, or over 64 KiB");
	expect(taken("psx", fill(131072, 1)) == "" && taken("psx", fill(131073, 1)) != "", true, "a memory card, exactly 128 KiB");
	expect(taken("n64", fill(296960, 1)) == "" && taken("n64", fill(2048, 1)) != "", true, "Mupen64Plus-Next's struct only, not a lone EEPROM");
	expect(taken("pico-8", fill(3, 1), &k32) == "", true, "a system with no rule: any size");
}

// a file written with a mode has it, even over a temporary file a write cut short left with another
static void modes()
{
	std::string p = ROOT + "/banana/romsync.json";
	put("banana/romsync.json.new", "torn");
	chmod((p + ".new").c_str(), 0644);
	struct stat st;
	expect(writeAtomic(p, "{}", 0600) && stat(p.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600, true, "the token's file 0600 over a stale temporary file");
	expect(readFile(p) == "{}" && !exists(p + ".new"), true, "the file written, nothing left beside it");
}

int main(int argc, char** argv)
{
	expect(saveFolder("saves/Snes9x/Peel Kart (USA).srm") == "Snes9x", true, "the folder of a core's save from before 0.5.3");
	expect(saveFolder("saves/Banana Run (USA).srm") == "", true, "a flat save has no folder");
	expect(saveFolder("states/gpSP/x.state") == "", true, "a state has no save folder");

	expect(blankSave("", ""), true, "empty");
	expect(blankSave(fill(8192, 0xff), "Gambatte"), true, "Gambatte's fresh 0xFF");
	expect(blankSave(fill(131072, 0xff), "gba"), true, "gpSP's fresh 128 KiB in the shared GBA folder");
	expect(blankSave(fill(131072, 0x00), "gpSP"), true, "an all-zero GBA save");
	expect(blankSave(fill(8192, 0x00), "FCEUmm"), true, "FCEUmm's fresh 0x00");
	expect(blankSave(fill(8192, 0x60), "Snes9x"), true, "Snes9x's fresh 0x60");
	expect(blankSave(fill(8192, 0x6b), "Snes9x"), true, "Snes9x's 0x6B for its listed games");
	expect(blankSave(fill(8192, 0x60), "gba"), false, "0x60 is blank only in Snes9x's folder");
	expect(blankSave(fill(8192, 0x60), ""), true, "Snes9x's fresh 0x60 in the flat saves/");
	expect(blankSave(fill(8192, 0x60), "nds"), false, "0x60 is data in the DS's folder");
	expect(blankSave(poked(fill(8192, 0x60), 0), ""), false, "one byte written in a flat save");
	expect(blankSave(poked(fill(131072, 0xff), 4000), "gba"), false, "one byte written");
	expect(blankSave(poked(fill(8192, 0x60), 0), "Snes9x"), false, "one Snes9x byte written");

	std::string card = pcsxBlankCard();
	expect(card.size() == 131072 && card.compare(0, 2, "MC") == 0 && (unsigned char)card[0x7f] == 0x0e, true, "the PCSX card's header frame");
	expect((unsigned char)card[0x80] == 0xa0 && (unsigned char)card[0x88] == 0xff && (unsigned char)card[0xff] == 0xa0, true, "a free directory frame");
	expect(blankSave(card, "PCSX-ReARMed"), true, "PCSX-ReARMed's formatted card");
	expect(blankSave(card, "gba"), false, "the PCSX card is blank only in its folder");
	expect(blankSave(card, ""), true, "PCSX-ReARMed's formatted card in the flat saves/");
	expect(blankSave(poked(card, 0x2000), "PCSX-ReARMed"), false, "a card with a save on it");

	std::string n64 = n64Fresh();
	expect(n64.size() == 296960, true, "the N64 struct's size");
	expect(blankSave(n64, "Mupen64Plus-Next"), true, "Mupen64Plus-Next's fresh struct, whatever the pak serials");
	expect(blankSave(poked(n64, 10), "Mupen64Plus-Next"), false, "EEPROM written");
	expect(blankSave(poked(n64, 0x800 + 0x8000 + 0x300), "Mupen64Plus-Next"), false, "a note on the second pak");
	expect(blankSave(poked(n64, 0x800 + 0x101), "Mupen64Plus-Next"), false, "a pak's index checksum off");
	expect(blankSave(poked(n64, 0x800 + 0x60), "Mupen64Plus-Next"), false, "a pak's ID backup not the ID");
	expect(blankSave(poked(n64, 296000), "Mupen64Plus-Next"), false, "FlashRAM written");
	expect(blankSave(n64.substr(1), "Mupen64Plus-Next"), false, "the wrong size");
	expect(blankSave(n64, ""), true, "Mupen64Plus-Next's fresh struct in the flat saves/");
	expect(blankSave(poked(n64, 10), ""), false, "EEPROM written, in the flat saves/");
	adapt();

	if (argc > 1)
	{
		ROOT = argv[1];
		vault();
		journal();
		modes();
	}
	if (!fails) fprintf(stderr, "vault-test: all passed\n");
	return fails ? 1 : 0;
}
