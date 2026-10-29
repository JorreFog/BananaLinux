// fileops' job on its own thread, as on the handheld (the headless runs do it in sync mode): Stop in the middle of a
// file, a move between two file systems stopped half-way, the Job destroyed while it works (banana-ui told to quit),
// a delete, the card pasted on taken out mid-way and before, a folder that can't be read (by root too); and the names:
// a long one copied, a long one with a number, Windows' devices, a change of case cut off, a PICO-8 cart's (2),
// Cyrillic sorted. Then on the cards' own file systems when it is given them (card()). Built with -fsanitize=thread
// where the compiler has it (tests/run.sh, which makes the two cards when it runs as root).
//   fileops-test <a folder> <a folder on another file system, or the same> [<a FAT32 card's folder> <an exFAT one's>]
#include "../src/fileops.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <string>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>

using namespace banana::files;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void sh(const std::string& c) { if (system(c.c_str()) != 0) printf("(command failed: %s)\n", c.c_str()); }
static bool there(const std::string& p) { struct stat st; return lstat(p.c_str(), &st) == 0; }
static off_t sizeOf(const std::string& p) { struct stat st; return lstat(p.c_str(), &st) == 0 ? st.st_size : -1; }
// our hidden names left in a folder (a copy or a change of case on its way)
static int leftovers(const std::string& dir)
{
	int n = 0;
	if (DIR* d = opendir(dir.c_str()))
	{
		while (dirent* e = readdir(d)) if (!strncmp(e->d_name, ".banana-", 8)) n++;
		closedir(d);
	}
	return n;
}

// until n bytes are copied (or the job is over)
static void waitBytes(Job& j, uint64_t n)
{
	for (int i = 0; i < 40000; i++) { if (j.progress().bytes >= n || !j.running()) return; usleep(250); }
}

static void waitOver(Job& j) { while (j.running()) usleep(1000); }

// root reads any folder (CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH): the two out of this thread's effective set, or back in
// (a job's thread started meanwhile has them as this one had them); false when the kernel wouldn't
static bool readAny(bool on)
{
	__user_cap_header_struct h = { _LINUX_CAPABILITY_VERSION_3, 0 };
	__user_cap_data_struct d[_LINUX_CAPABILITY_U32S_3];
	if (syscall(SYS_capget, &h, d) != 0) return false;
	uint32_t dac = 1u << CAP_DAC_OVERRIDE | 1u << CAP_DAC_READ_SEARCH;
	d[0].effective = on ? d[0].effective | (d[0].permitted & dac) : d[0].effective & ~dac;
	return syscall(SYS_capset, &h, d) == 0;
}

// ---- on a card's own file system (tests/run.sh's FAT32 and exFAT images, mounted as the handheld mounts its cards) ----
static std::string rep(const std::string& s, int n) { std::string o; while (n-- > 0) o += s; return o; }
static std::string utf8(uint16_t u) { return { (char)(0xc0 | u >> 6), (char)(0x80 | (u & 0x3f)) }; }	// (U+0080 to U+07FF)
// the UTF-16 characters a name takes (one outside the BMP takes two)
static size_t units(const std::string& s) { size_t n = 0; for (unsigned char c : s) if ((c & 0xc0) != 0x80) n += c >= 0xf0 ? 2 : 1; return n; }
static bool touch(const std::string& p) { int fd = open(p.c_str(), O_CREAT | O_WRONLY, 0644); return fd >= 0 && close(fd) == 0; }

// the first line of a file in the sysfs folder of dir's block device ("stat", "loop/backing_file"), or ""
static std::string sysBlock(const std::string& dir, const char* file)
{
	struct stat st;
	std::string l;
	if (stat(dir.c_str(), &st) != 0) return l;
	FILE* f = fopen(("/sys/dev/block/" + std::to_string(major(st.st_dev)) + ":" + std::to_string(minor(st.st_dev)) + "/" + file).c_str(), "r");
	char b[4096];
	if (f && fgets(b, sizeof b, f)) l = b;
	if (f) fclose(f);
	if (!l.empty() && l.back() == '\n') l.pop_back();
	return l;
}

// the 512-byte sectors dir's card has written
static unsigned long long written(const std::string& dir)
{
	unsigned long long v[7] = {};
	sscanf(sysBlock(dir, "stat").c_str(), "%llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]);
	return v[6];
}

// five of character u in a row, as both cards keep a name (UTF-16LE), in a card's image: on the card, not in the RAM
static bool onCard(const std::string& image, uint16_t u)
{
	std::string pat;
	for (int i = 0; i < 5; i++) { pat += (char)(u & 0xff); pat += (char)(u >> 8); }
	FILE* f = fopen(image.c_str(), "rb");
	if (!f) return false;
	std::string b(1 << 20, '\0'), s;
	bool found = false;
	for (size_t n; !found && (n = fread(&b[0], 1, b.size(), f)) > 0;)
	{
		s = s.substr(s.size() - std::min(s.size(), pat.size() - 1)) + b.substr(0, n);
		found = s.find(pat) != std::string::npos;
	}
	fclose(f);
	return found;
}

// what tmpfs can't show, on a FAT32 or an exFAT card (at: its folder; A: the test's own, for a 4 GB file): a name's
// limit as the card counts it, a folder made or renamed on the card at once and none of the card's other writes made
// for it, a change of case, FAT32's 4 GB a file
static void card(const std::string& at, const std::string& A, bool fat)
{
	const char* fs = fat ? "FAT32" : "exFAT";
	struct statfs sf;
	if (statfs(at.c_str(), &sf) != 0 || sf.f_type != (fat ? 0x4d44 : 0x2011bab0)) { CHECK(false, "%s isn't on a %s card", at.c_str(), fs); return; }
	std::string C = at + "/fo", why;
	sh("rm -rf '" + C + "' && mkdir -p '" + C + "'");
	// the longest names, 255 UTF-16 characters on both (Я two bytes, 😀 two characters): as the card takes them, so
	// nameProblem says
	std::string ya = utf8(0x42f), smile = "\xf0\x9f\x98\x80";
	struct { const char* what; std::string name; bool fits; } names[] = { { "255 Я", rep(ya, 255), true }, { "256 Я", rep(ya, 256), false },
		{ "253 Я and 😀", rep(ya, 253) + smile, true }, { "254 Я and 😀", rep(ya, 254) + smile, false }, { "255 a", std::string(255, 'a'), true },
		{ "256 a", std::string(256, 'a'), false } };
	for (auto& n : names)
	{
		bool takes = mkdir((C + "/" + n.name).c_str(), 0755) == 0;
		if (takes) rmdir((C + "/" + n.name).c_str());
		std::string says = nameProblem(n.name, C);
		CHECK(takes == n.fits && says.empty() == n.fits, "%s: a name of %s (%zu characters, %zu bytes): the card %s it, nameProblem says '%s'", fs,
			n.what, units(n.name), n.name.size(), takes ? "takes" : "refuses", says.c_str());
	}
	// a folder of 255 made and renamed, and the "(2)" of a folder and of a file kept within them
	std::string f = rep(ya, 255), to = rep(ya, 254) + utf8(0x416), g = rep(ya, 251) + ".gba";
	CHECK(makeFolder(C, f, why) && renameEntry(C, f, to, why) && there(C + "/" + to), "%s: a folder of 255 characters: %s", fs, why.c_str());
	std::string f2 = freeName(C, to, true), g2 = touch(C + "/" + g) ? freeName(C, g) : "";
	CHECK(units(f2) == 255 && mkdir((C + "/" + f2).c_str(), 0755) == 0, "%s: a folder's (2): %zu characters", fs, units(f2));
	CHECK(units(g2) == 255 && g2.size() > 8 && g2.compare(g2.size() - 8, 8, " (2).gba") == 0 && touch(C + "/" + g2), "%s: a file's (2): %zu characters",
		fs, units(g2));
	// a change of case: the card takes the two names for one (exists and freeName as blind to case as it), so it goes
	// through the hidden name, and the card lists the new case
	std::vector<Entry> es;
	CHECK(touch(C + "/zelda.gba") && exists(C + "/ZELDA.GBA") && freeName(C, "ZELDA.GBA") == "ZELDA (2).GBA", "%s: not blind to case", fs);
	bool renamed = renameEntry(C, "zelda.gba", "Zelda.gba", why), listed = false, old = false;
	if (list(C, es, why, false)) for (auto& e : es) { listed |= e.name == "Zelda.gba"; old |= e.name == "zelda.gba"; }
	CHECK(renamed && listed && !old && leftovers(C) == 0, "%s: a change of case: %s", fs, why.c_str());
	// a folder made, then renamed: on the card at once (its image has the name), and the card's other writes left to the
	// kernel, here 16 MB still in the RAM (flushCard: the folder's entries, not the card's syncfs)
	std::string image = sysBlock(C, "loop/backing_file");
	if (image.empty()) printf("(%s: a folder made on the card at once: skipped, %s isn't on a loop device)\n", fs, C.c_str());
	else
	{
		unsigned long long w = written(C), w0, w1;
		int fd = open((C + "/big.bin").c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
		std::string mb(1 << 20, 'x');
		for (int i = 0; i < 16 && fd >= 0; i++) if (write(fd, mb.data(), mb.size()) != (ssize_t)mb.size()) break;
		w0 = written(C);
		// (100 characters, 200 bytes: within the limit counted either way)
		bool made = makeFolder(C, rep(utf8(0x42e), 100), why) && onCard(image, 0x42e);	// (Ю)
		bool moved = made && renameEntry(C, rep(utf8(0x42e), 100), rep(utf8(0x429), 100), why) && onCard(image, 0x429);	// (Щ)
		w1 = written(C);
		CHECK(made && moved, "%s: a folder %s isn't on the card at once: %s", fs, made ? "renamed" : "made", why.c_str());
		if ((w0 - w) * 512 >= (8u << 20)) printf("(%s: what else a folder made writes: skipped, the 16 MB went to the card before)\n", fs);
		else CHECK((w1 - w0) * 512 < (4u << 20), "%s: a folder made and renamed wrote %llu KB to the card: the 16 MB waiting too", fs, (w1 - w0) / 2);
		if (fd >= 0) close(fd);
	}
	// FAT32's 4 GB a file (space's fat32), refused before a byte; the exFAT card is only too small for it
	Space sp;
	Job j;
	j.start(Op::COPY, { A + "/huge.bin" }, C, Clash::KEEP_BOTH, true);
	Progress p = j.progress();
	CHECK(space(C, sp) && sp.fat32 == fat && p.error.find(fat ? "this card is FAT32" : "Not enough room") != std::string::npos &&
		!there(C + "/huge.bin") && leftovers(C) == 0, "%s: a 4 GB file: '%s'", fs, p.error.c_str());
	sh("rm -rf '" + C + "'");
}

int main(int argc, char** argv)
{
	if (argc < 3) { fprintf(stderr, "usage: fileops-test <dir> <dir on another file system> [<dir on a FAT32 card> <dir on an exFAT card>]\n"); return 2; }
	std::string A = std::string(argv[1]) + "/fo", B = std::string(argv[2]) + "/fo";
	// a slow card's pace (2 ms a MiB): a Stop after 4 MiB comes well before the 32 MB are over, however fast the disk
	setenv("BANANA_FILES_PACE", "2", 1);
	sh("rm -rf '" + A + "' '" + B + "' && mkdir -p '" + A + "/src' '" + A + "/dst' '" + B + "'");
	// 32 MB: copied in several windows, stopped in the middle (and room for it on a container's 64 MB /dev/shm)
	sh("head -c 33554432 /dev/urandom > '" + A + "/src/big.bin'");
	for (int i = 1; i <= 3; i++) sh("echo " + std::to_string(i) + " > '" + A + "/src/f" + std::to_string(i) + "'");

	{
		// Stop in the middle of the big file: f1 done, big.bin left out (no hidden copy left), f2 not begun
		Job j;
		std::atomic<bool> told{ false };
		j.start(Op::COPY, { A + "/src/f1", A + "/src/big.bin", A + "/src/f2" }, A + "/dst", Clash::KEEP_BOTH, false, [&] { told = true; });
		waitBytes(j, 4u << 20);
		j.stop();
		waitOver(j);
		Progress p = j.progress();
		CHECK(p.stopped, "a Stop in the middle of a file is a stop");
		CHECK(p.done == 1 && p.sourcesDone == 1, "done %d, sources done %d", p.done, p.sourcesDone);
		CHECK(there(A + "/dst/f1") && !there(A + "/dst/big.bin") && !there(A + "/dst/f2"), "what was copied");
		CHECK(leftovers(A + "/dst") == 0, "a hidden copy left behind");
		for (int i = 0; i < 2000 && !told; i++) usleep(1000);	// (done() comes just after the state says DONE)
		CHECK(told, "the host wasn't told");
	}
	{
		// a move to another file system, stopped half-way: f1 moved (gone from src), big.bin and f3 still in src
		Job j;
		j.start(Op::MOVE, { A + "/src/f1", A + "/src/big.bin", A + "/src/f3" }, B, Clash::KEEP_BOTH, false);
		waitBytes(j, 4u << 20);
		j.stop();
		waitOver(j);
		Progress p = j.progress();
		CHECK(p.stopped && p.done == 1, "stopped %d done %d", p.stopped, p.done);
		CHECK(there(B + "/f1") && !there(A + "/src/f1"), "f1 moved");
		CHECK(sizeOf(A + "/src/big.bin") == 33554432 && there(A + "/src/f3") && !there(B + "/big.bin"), "the rest stayed");
		CHECK(leftovers(B) == 0, "a hidden copy left behind");
	}
	{
		// the Job destroyed while it copies (banana-ui quitting): stopped, waited for, nothing half-made left
		{
			Job j;
			j.start(Op::COPY, { A + "/src/big.bin" }, B, Clash::KEEP_BOTH, false);
			waitBytes(j, 4u << 20);
		}
		CHECK(!there(B + "/big.bin") && leftovers(B) == 0, "a copy cut off by the end left something");
	}
	{
		// names: a 244-byte one copied (its hidden name is cut to fit), "(2)" kept within 255 bytes
		std::string longName(240, 'A');
		longName += ".gba";
		sh("echo x > '" + A + "/src/" + longName + "'");
		Job j;
		j.start(Op::COPY, { A + "/src/" + longName }, B, Clash::KEEP_BOTH, true);
		Progress p = j.progress();
		CHECK(p.failed == 0 && there(B + "/" + longName), "a long name wasn't copied: %s", p.error.c_str());
		j.start(Op::COPY, { A + "/src/" + longName }, B, Clash::KEEP_BOTH, true);
		p = j.progress();
		CHECK(p.failed == 0 && p.placed.size() == 1 && p.placed[0].size() <= 255, "a long name's (2): %s", p.error.c_str());
		CHECK(!nameProblem("nul.txt").empty() && !nameProblem("COM1").empty() && nameProblem("console.txt").empty(), "Windows' devices");
	}
	{
		// a delete of a folder: every entry, the folder too
		Job j;
		j.start(Op::DELETE, { A + "/src" }, "", Clash::SKIP, false);
		waitOver(j);
		Progress p = j.progress();
		CHECK(!there(A + "/src") && p.sourcesDone == 1 && !p.stopped, "delete");
	}
	struct stat sa, sb;
	if (stat(A.c_str(), &sa) == 0 && stat(B.c_str(), &sb) == 0 && sa.st_dev != sb.st_dev)
	{
		// the card pasted on taken out mid-way (the sd2 script's umount -l): its path is then another file system's folder
		// (the mount point's own, in /run's tmpfs), here a link to one on A's. A move from A: nothing goes there, no
		// source is deleted, the job ends and says so
		sh("mkdir -p '" + A + "/out/src' '" + A + "/out/elsewhere' '" + B + "/card' && head -c 33554432 /dev/urandom > '" + A + "/out/src/big.bin'");
		for (int i = 1; i <= 2; i++) sh("echo " + std::to_string(i) + " > '" + A + "/out/src/s" + std::to_string(i) + "'");
		Job j;
		j.start(Op::MOVE, { A + "/out/src/big.bin", A + "/out/src/s1", A + "/out/src/s2" }, B + "/card", Clash::KEEP_BOTH, false);
		waitBytes(j, 4u << 20);
		CHECK(rename((B + "/card").c_str(), (B + "/card.gone").c_str()) == 0 && symlink((A + "/out/elsewhere").c_str(), (B + "/card").c_str()) == 0, "the card's path not swapped");
		waitOver(j);
		Progress p = j.progress();
		CHECK(there(A + "/out/src/big.bin") && there(A + "/out/src/s1") && there(A + "/out/src/s2"), "a source deleted after the card went");
		CHECK(!there(A + "/out/elsewhere/s1") && !there(A + "/out/elsewhere/s2") && leftovers(A + "/out/elsewhere") == 0, "a file put in the mount point's own folder");
		CHECK(p.failed > 0 && p.unwritten, "the card gone not said: failed %d, unwritten %d", p.failed, (int)p.unwritten);
		// and taken out before the job began (the pages saw it mounted: Card::dev): nothing is tried
		j.start(Op::COPY, { A + "/out/src/s1" }, B + "/card", Clash::KEEP_BOTH, true, nullptr, sb.st_dev);
		p = j.progress();
		CHECK(p.error.find("taken out") != std::string::npos && !there(A + "/out/elsewhere/s1"), "a paste on a card gone: '%s'", p.error.c_str());
	}
	else printf("(the card taken out: skipped, %s and %s are one file system)\n", A.c_str(), B.c_str());
	{
		// a paste into a folder gone from a card still in: refused, and why (not "the card was taken out")
		sh("mkdir -p '" + A + "/gone' && echo g > '" + A + "/gone/g'");
		Job j;
		j.start(Op::COPY, { A + "/gone/g" }, A + "/gone/nowhere", Clash::KEEP_BOTH, true, nullptr, sa.st_dev);
		Progress p = j.progress();
		CHECK(p.error == "nowhere: it isn't there any more. Nothing was copied." && p.failed == 1, "a paste into a folder gone: '%s'", p.error.c_str());
	}
	bool root = geteuid() == 0;
	if (!root || readAny(false))
	{
		// a folder that can't be read, in one copied: failed, and why; not "Copied 1 folder" with it missing (as root,
		// without the capabilities that let it read any)
		sh("mkdir -p '" + A + "/un/X/locked' && echo x > '" + A + "/un/X/locked/game.nds' && echo y > '" + A + "/un/X/ok' && chmod 000 '" + A + "/un/X/locked'");
		Job j;
		j.start(Op::COPY, { A + "/un/X" }, B, Clash::KEEP_BOTH, true);
		Progress p = j.progress();
		CHECK(p.failed == 1 && p.sourcesDone == 0 && p.error.compare(0, 7, "locked:") == 0, "an unreadable folder: failed %d, sources done %d, '%s'", p.failed, p.sourcesDone, p.error.c_str());
		if (root) readAny(true);
		sh("chmod 755 '" + A + "/un/X/locked'");
	}
	else printf("(a folder that can't be read: skipped, root reads any, and the kernel wouldn't take that from it)\n");
	{
		// a change of case cut off between its two renames: the entry back under its name at the next listing
		sh("mkdir -p '" + A + "/rn' && echo z > '" + A + "/rn/.banana-rename-Zelda.gba'");
		std::vector<Entry> es;
		std::string why;
		CHECK(list(A + "/rn", es, why, true) && es.size() == 1 && es[0].name == "Zelda.gba" && leftovers(A + "/rn") == 0, "a change of case cut off stays hidden");
		// "(2)" before a two-part extension (a PICO-8 cart stays one); Windows' COM0 and LPT¹; Cyrillic sorted case aside
		sh("echo c > '" + A + "/rn/cart.p8.png'");
		CHECK(freeName(A + "/rn", "cart.p8.png") == "cart (2).p8.png", "a PICO-8 cart's (2): %s", freeName(A + "/rn", "cart.p8.png").c_str());
		CHECK(!nameProblem("com0").empty() && !nameProblem("LPT\xc2\xb9.txt").empty() && nameProblem("computer.txt").empty(), "Windows' ports");
		sh("mkdir -p '" + A + "/srt' && cd '" + A + "/srt' && : > 'Яблоко2.nds' && : > 'яблоко.nds' && : > 'Banana.nds' && : > 'apple.nds'");
		CHECK(list(A + "/srt", es, why, false) && es.size() == 4 && es[0].name == "apple.nds" && es[1].name == "Banana.nds" && es[2].name == "яблоко.nds" &&
			es[3].name == "Яблоко2.nds", "Cyrillic sorted by case");
	}
	if (argc >= 5)
	{
		sh("truncate -s 4G '" + A + "/huge.bin'");	// (sparse: no room taken)
		card(argv[3], A, true);
		card(argv[4], A, false);
	}
	else printf("(FAT32 and exFAT: skipped, no cards given: tests/run.sh makes them when it runs as root)\n");
	sh("rm -rf '" + A + "' '" + B + "'");
	printf("fileops-test: %s\n", fails ? "FAILED" : "all checks passed");
	return fails ? 1 : 0;
}
