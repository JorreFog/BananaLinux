// The DS Pokémon companion's ROM reader (src/nds/nitro.cpp, src/nds/rom.cpp) on data made here from the documented
// layouts (no game data in it), and, when a folder of the player's own ROMs is given, on those: counts and spot
// values every game must show.
//   make tests/nds-rom-test && tests/nds-rom-test [roms-dir [cache-dir]]
#include "nds/nitro.h"
#include "nds/rom.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <set>
#include <sys/resource.h>
#include <unistd.h>
#include <string>
#include <vector>

using namespace banana::nds;
using nitro::Bytes;

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define EQ(a, b) do { checks++; auto _a = (a); auto _b = (b); if (!(_a == _b)) { fails++; fprintf(stderr, "FAIL %s:%d: %s == %s (%s vs %s)\n", __FILE__, __LINE__, #a, #b, str(_a).c_str(), str(_b).c_str()); } } while (0)
static std::string str(const std::string& s) { return "\"" + s + "\""; }
static std::string str(const char* s) { return str(std::string(s)); }
template <class T> static std::string str(T v) { return std::to_string(v); }

static void put16(Bytes& b, size_t o, uint16_t v) { if (b.size() < o + 2) b.resize(o + 2); b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); }
static void put32(Bytes& b, size_t o, uint32_t v) { if (b.size() < o + 4) b.resize(o + 4); for (int i = 0; i < 4; i++) b[o + i] = (uint8_t)(v >> (8 * i)); }
static void add16(Bytes& b, uint16_t v) { put16(b, b.size(), v); }
static void add32(Bytes& b, uint32_t v) { put32(b, b.size(), v); }
static Bytes bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

// ---- SHA-1 (FIPS 180's examples) ---------------------------------------------------------------------------------
static void testSha1()
{
	EQ(nitro::sha1Hex((const uint8_t*)"abc", 3), std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
	EQ(nitro::sha1Hex(nullptr, 0), std::string("da39a3ee5e6b4b0d3255bfef95601890afd80709"));
	std::string m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	EQ(nitro::sha1Hex((const uint8_t*)m.data(), m.size()), std::string("84983e441c3bd26ebaae4aa1f95129e5e54670f1"));
	// in pieces of odd sizes, as the ROM reader feeds it
	nitro::Sha1 h;
	for (size_t i = 0; i < m.size(); i += 7) h.add((const uint8_t*)m.data() + i, std::min<size_t>(7, m.size() - i));
	EQ(h.hex(), std::string("84983e441c3bd26ebaae4aa1f95129e5e54670f1"));
}

// ---- NARC ------------------------------------------------------------------------------------------------------
static Bytes makeNarc(const std::vector<Bytes>& members)
{
	Bytes img;
	std::vector<std::pair<uint32_t, uint32_t>> fat;
	for (auto& m : members) { while (img.size() % 4) img.push_back(0xff); fat.push_back({ (uint32_t)img.size(), (uint32_t)(img.size() + m.size()) }); img.insert(img.end(), m.begin(), m.end()); }
	while (img.size() % 4) img.push_back(0xff);
	Bytes b;
	b.insert(b.end(), { 'N', 'A', 'R', 'C', 0xfe, 0xff, 0x00, 0x01 });
	add32(b, 0); add16(b, 16); add16(b, 3);
	b.insert(b.end(), { 'B', 'T', 'A', 'F' }); add32(b, 12 + 8 * (uint32_t)fat.size()); add32(b, (uint32_t)fat.size());
	for (auto& f : fat) { add32(b, f.first); add32(b, f.second); }
	b.insert(b.end(), { 'B', 'T', 'N', 'F' }); add32(b, 16); add32(b, 4); add16(b, 0); add16(b, 1);
	b.insert(b.end(), { 'G', 'M', 'I', 'F' }); add32(b, 8 + (uint32_t)img.size());
	b.insert(b.end(), img.begin(), img.end());
	put32(b, 8, (uint32_t)b.size());
	return b;
}

static void testNarc()
{
	Bytes n = makeNarc({ bytes("first"), bytes(""), bytes("third member") });
	nitro::Narc a;
	CHECK(a.parse(n));
	EQ(a.size(), (size_t)3);
	EQ(std::string((const char*)a[0].p, a[0].n), std::string("first"));
	EQ(a[1].n, (size_t)0);
	EQ(std::string((const char*)a[2].p, a[2].n), std::string("third member"));
	EQ(a[7].n, (size_t)0);	// (past the end: empty, not a crash)
	// a member past the archive's end, or the wrong magic: refused
	Bytes bad = n; put32(bad, 0x10 + 12 + 8 * 2 + 4, 4000);
	CHECK(!nitro::Narc().parse(bad));
	Bytes notNarc = n; notNarc[0] = 'X';
	CHECK(!nitro::Narc().parse(notNarc));
	CHECK(!nitro::Narc().parse(n.data(), 10));
}

// ---- the cartridge's file system ---------------------------------------------------------------------------------
// a small ROM: root has "top.txt" and dir "a" which has "x.bin" and dir "b" with "y"; arm9 is "ARM9" at 0x4000
static Bytes makeRom(Bytes arm9 = bytes("ARM9DATA"), uint32_t ramAddr = 0x02000000)
{
	Bytes rom(0x4000, 0);
	memcpy(&rom[0], "POKEMON PL\0\0", 12);
	memcpy(&rom[12], "CPUE", 4);
	// files: 0 top.txt, 1 x.bin, 2 y
	std::vector<Bytes> files = { bytes("top file"), bytes("x contents"), bytes("deep y") };
	put32(rom, 0x20, 0x4000); put32(rom, 0x28, ramAddr); put32(rom, 0x2c, (uint32_t)arm9.size());
	rom.insert(rom.end(), arm9.begin(), arm9.end());
	while (rom.size() % 0x200) rom.push_back(0xff);
	// FNT: main table of 3 dirs (root 0xf000, a 0xf001, b 0xf002), then the sub-tables
	Bytes fnt(3 * 8, 0);
	Bytes t0, t1, t2;
	t0.push_back(7); for (char c : std::string("top.txt")) t0.push_back(c);
	t0.push_back(0x80 | 1); t0.push_back('a'); t0.push_back(0x01); t0.push_back(0xf0); t0.push_back(0);
	t1.push_back(5); for (char c : std::string("x.bin")) t1.push_back(c);
	t1.push_back(0x80 | 1); t1.push_back('b'); t1.push_back(0x02); t1.push_back(0xf0); t1.push_back(0);
	t2.push_back(1); t2.push_back('y'); t2.push_back(0);
	put32(fnt, 0, 24); put16(fnt, 4, 0); put16(fnt, 6, 3);
	put32(fnt, 8, 24 + (uint32_t)t0.size()); put16(fnt, 12, 1); put16(fnt, 14, 0xf000);
	put32(fnt, 16, 24 + (uint32_t)(t0.size() + t1.size())); put16(fnt, 20, 2); put16(fnt, 22, 0xf001);
	fnt.insert(fnt.end(), t0.begin(), t0.end()); fnt.insert(fnt.end(), t1.begin(), t1.end()); fnt.insert(fnt.end(), t2.begin(), t2.end());
	put32(rom, 0x40, (uint32_t)rom.size()); put32(rom, 0x44, (uint32_t)fnt.size());
	rom.insert(rom.end(), fnt.begin(), fnt.end());
	while (rom.size() % 4) rom.push_back(0xff);
	size_t fatAt = rom.size();
	put32(rom, 0x48, (uint32_t)fatAt); put32(rom, 0x4c, (uint32_t)files.size() * 8);
	rom.resize(fatAt + files.size() * 8);
	for (size_t i = 0; i < files.size(); i++)
	{
		uint32_t at = (uint32_t)rom.size();
		rom.insert(rom.end(), files[i].begin(), files[i].end());
		put32(rom, fatAt + i * 8, at); put32(rom, fatAt + i * 8 + 4, at + (uint32_t)files[i].size());
	}
	return rom;
}

static void testFileSystem()
{
	nitro::Rom r;
	CHECK(r.openMemory(makeRom()));
	EQ(r.fileCount(), (size_t)3);
	EQ(r.fileId("top.txt"), 0);
	EQ(r.fileId("a/x.bin"), 1);
	EQ(r.fileId("a/b/y"), 2);
	EQ(r.fileId("a/b/z"), -1);
	EQ(r.fileId("a"), -1);
	Bytes b;
	CHECK(r.file("a/b/y", b));
	EQ(std::string(b.begin(), b.end()), std::string("deep y"));
	CHECK(r.read(0, b));
	EQ(std::string(b.begin(), b.end()), std::string("top file"));
	CHECK(!r.read(3, b));
	CHECK(r.arm9(b));
	EQ(std::string(b.begin(), b.end()), std::string("ARM9DATA"));
	EQ(std::string((const char*)r.header() + 12, 4), std::string("CPUE"));
	// too small to be a ROM
	CHECK(!nitro::Rom().openMemory(Bytes(0x100, 0)));
	// a FAT pointing past the end
	Bytes broken = makeRom(); put32(broken, 0x48, (uint32_t)broken.size() + 16);
	CHECK(!nitro::Rom().openMemory(broken));
	// table and ARM9 sizes far past the image: refused, nothing huge allocated
	Bytes huge = makeRom(); put32(huge, 0x44, 0xfffffff0);
	CHECK(!nitro::Rom().openMemory(huge));
	nitro::Rom big;
	Bytes hugeArm9 = makeRom(); put32(hugeArm9, 0x2c, 0xfffffff0);
	CHECK(big.openMemory(hugeArm9));
	CHECK(!big.arm9(b));
}

// ---- BLZ ---------------------------------------------------------------------------------------------------------
// The packer, written from the format (GBATEK "LZ Decompression Functions", the backward variant the SDK uses for
// ARM9 binaries): the output is built from its end down; the stream is read from the packed part's end down: a flag
// byte, then per bit (high first) a literal byte or two bytes (high first) of length-3 << 12 | distance-3.
static Bytes blzPack(const Bytes& plain, size_t keep)	// keep: bytes at the start left as they are
{
	std::vector<uint8_t> s;	// in read order
	size_t p = plain.size();
	while (p > keep)
	{
		size_t flagAt = s.size(); s.push_back(0);
		for (int bit = 0; bit < 8 && p > keep; bit++)
		{
			size_t bestLen = 0, bestDisp = 0;
			for (size_t disp = 3; disp <= 0x1002 && p - 1 + disp < plain.size(); disp++)
			{
				size_t len = 0;
				while (len < 18 && p - len > keep && p - 1 - len + disp < plain.size() && plain[p - 1 - len] == plain[p - 1 - len + disp]) len++;
				if (len > bestLen) { bestLen = len; bestDisp = disp; }
			}
			if (bestLen >= 3)
			{
				s[flagAt] |= 0x80 >> bit;
				uint16_t v = (uint16_t)((bestLen - 3) << 12 | (bestDisp - 3));
				s.push_back((uint8_t)(v >> 8)); s.push_back((uint8_t)v);
				p -= bestLen;
			}
			else { s.push_back(plain[--p]); }
		}
	}
	Bytes out(plain.begin(), plain.begin() + keep);
	for (size_t i = s.size(); i-- > 0;) out.push_back(s[i]);
	size_t hdr = 8;
	while ((out.size() + 8) % 4) { out.push_back(0xff); hdr++; }
	uint32_t encLen = (uint32_t)(s.size() + hdr);
	add32(out, encLen | (uint32_t)hdr << 24);
	add32(out, (uint32_t)(plain.size() - out.size() - 4));
	return out;
}

static void testBlz()
{
	Bytes plain;
	for (int i = 0; i < 3000; i++) plain.push_back((uint8_t)(i % 7 == 0 ? i : i % 13));
	for (int i = 0; i < 200; i++) plain.push_back('Z');
	Bytes packed = blzPack(plain, 100);
	CHECK(packed.size() < plain.size());
	Bytes out;
	CHECK(nitro::blzDecompress(packed, out));
	CHECK(out == plain);
	// no growth (inc 0): stored as it is
	Bytes raw = bytes("not packed at all"); add32(raw, 0); add32(raw, 0);
	CHECK(nitro::blzDecompress(raw, out));
	EQ(out.size(), raw.size());
	// a stream that runs past its start is refused
	Bytes bad = packed; put32(bad, bad.size() - 8, 0x08000000 | (uint32_t)(bad.size() + 50));
	CHECK(!nitro::blzDecompress(bad, out));
	CHECK(!nitro::blzDecompress(Bytes(5, 0), out));
	// a growth no DS binary has (4 GiB): refused before anything is allocated
	Bytes huge = packed; put32(huge, huge.size() - 4, 0xfffffff0);
	CHECK(!nitro::blzDecompress(huge, out));

	// an ARM9 binary packed after its first 0x80 bytes, its module parameters saying where the packed part ends
	Bytes arm9(0x80, 0x11);
	put32(arm9, 0x20, 0);	// (the module parameters at 0x20)
	for (int i = 0; i < 2000; i++) arm9.push_back((uint8_t)(i * 3 % 17));
	Bytes params(9 * 4, 0);
	put32(params, 7 * 4, 0xdec00621); put32(params, 8 * 4, 0x2106c0de);
	memcpy(&arm9[0x20], params.data(), params.size());
	Bytes packedArm9 = blzPack(arm9, 0x80);
	put32(packedArm9, 0x20 + 5 * 4, 0x02000000 + (uint32_t)packedArm9.size());	// compressed_static_end
	Bytes unpacked;
	CHECK(nitro::arm9Unpack(packedArm9, 0x02000000, unpacked));
	EQ(unpacked.size(), arm9.size());
	CHECK(unpacked.size() == arm9.size() && memcmp(unpacked.data() + 0x80, arm9.data() + 0x80, arm9.size() - 0x80) == 0);
	// through the ROM
	nitro::Rom r;
	CHECK(r.openMemory(makeRom(packedArm9)));
	Bytes viaRom;
	CHECK(r.arm9(viaRom));
	CHECK(viaRom == unpacked);
}

// ---- LZ77 ------------------------------------------------------------------------------------------------------
static void testLz()
{
	// 0x10: "abcabcabcX": literals a b c, then a copy of 6 from 3 back, then X
	Bytes a = { 0x10, 10, 0, 0, 0x10, 'a', 'b', 'c', (6 - 3) << 4, 3 - 1, 'X' };
	Bytes out;
	CHECK(nitro::lzDecompress(a.data(), a.size(), out));
	EQ(std::string(out.begin(), out.end()), std::string("abcabcabcX"));
	// 0x11: 'z', then a copy of 20 from 1 back (the 3-byte form: indicator 0, length-17 in the next 8 bits), then
	// one of 3 (the 2-byte form: indicator = length-1)
	Bytes b = { 0x11, 24, 0, 0, 0x60, 'z', 0x00, 0x30, 0x00, 0x20, 0x00 };
	CHECK(nitro::lzDecompress(b.data(), b.size(), out));
	EQ(out.size(), (size_t)24);
	EQ(std::string(out.begin(), out.end()), std::string(24, 'z'));
	// a copy from before the start, a truncated stream, and a size no sprite has: refused
	Bytes c = { 0x10, 10, 0, 0, 0x80, 0x30, 5 };
	CHECK(!nitro::lzDecompress(c.data(), c.size(), out));
	CHECK(!nitro::lzDecompress(a.data(), 6, out));
	Bytes d = { 0x11, 0, 0, 0, 0xff, 0xff, 0xff, 0x7f };
	CHECK(!nitro::lzDecompress(d.data(), d.size(), out));
}

// ---- text --------------------------------------------------------------------------------------------------------
// Gen IV codes for ASCII (charmap: 0x0121 '0', 0x012B 'A', 0x0145 'a', 0x01DE space, 0xE000 newline)
static std::vector<uint16_t> g4(const std::string& s)
{
	std::vector<uint16_t> o;
	for (char c : s)
		o.push_back(c >= '0' && c <= '9' ? 0x121 + (c - '0') : c >= 'A' && c <= 'Z' ? 0x12b + (c - 'A') : c >= 'a' && c <= 'z' ? 0x145 + (c - 'a') :
			c == ' ' ? 0x1de : c == '\n' ? 0xe000 : c == '.' ? 0x1ae : 0x1ac);
	o.push_back(0xffff);
	return o;
}

// a bank as the games keep it (pokeplatinum src/message.c): count, seed, then (offset, length) per entry XORed with
// a key from the seed and the entry's number, then the strings XORed with a key that grows per character
static Bytes gen4Bank(const std::vector<std::vector<uint16_t>>& entries, uint16_t seed)
{
	Bytes b; add16(b, (uint16_t)entries.size()); add16(b, seed);
	b.resize(4 + 8 * entries.size());
	for (size_t i = 0; i < entries.size(); i++)
	{
		uint32_t k = (seed * 765u * (uint32_t)(i + 1)) & 0xffff; k |= k << 16;
		put32(b, 4 + 8 * i, (uint32_t)b.size() ^ k); put32(b, 8 + 8 * i, (uint32_t)entries[i].size() ^ k);
		uint16_t key = (uint16_t)((i + 1) * 596947);
		for (uint16_t c : entries[i]) { add16(b, c ^ key); key = (uint16_t)(key + 18749); }
	}
	return b;
}

// a trainer name packed 9 bits a character into 15-bit words after 0xF100 (pokeplatinum String_ConcatTrainerName)
static std::vector<uint16_t> g4Packed(const std::string& s)
{
	std::vector<uint16_t> codes = g4(s); codes.back() = 0x1ff;
	std::vector<uint16_t> o = { 0xf100 };
	uint32_t acc = 0; int bits = 0;
	for (uint16_t c : codes) { acc |= (uint32_t)c << bits; bits += 9; while (bits >= 15) { o.push_back(acc & 0x7fff); acc >>= 15; bits -= 15; } }
	if (bits) o.push_back((uint16_t)(acc & 0x7fff));
	o.push_back(0xffff);
	return o;
}

static void testGen4Text()
{
	std::vector<uint16_t> withCommand = g4("Hi ");
	withCommand.pop_back();
	// a colour command (0xFFFE, the command, its parameter count, the parameters) then "you"
	for (uint16_t c : { 0xfffe, 0xff00, 1, 2 }) withCommand.push_back(c);
	for (uint16_t c : g4("you")) withCommand.push_back(c);
	std::vector<uint16_t> accented = { 0x13a, 0x153, 0x14f, 0x188, 0x151, 0x153, 0x152, 0xffff };	// Pokémon
	Bytes bank = gen4Bank({ g4("Bulbasaur"), g4("Line one\nLine two."), withCommand, g4Packed("BARRY"), accented, g4("") }, 0x5a3c);
	auto s = nitro::gen4Text(bank.data(), bank.size());
	EQ(s.size(), (size_t)6);
	if (s.size() == 6)
	{
		EQ(s[0], std::string("Bulbasaur"));
		EQ(s[1], std::string("Line one\nLine two."));
		EQ(s[2], std::string("Hi you"));
		EQ(s[3], std::string("BARRY"));
		EQ(s[4], std::string("Pokémon"));
		EQ(s[5], std::string(""));
	}
	EQ(std::string(nitro::gen4Char(0x1bb)), std::string("♂"));
	// an entry pointing past the bank: empty, the rest still read
	Bytes broken = bank; uint32_t k0 = (0x5a3c * 765u) & 0xffff; put32(broken, 4, 0xffff0 ^ (k0 | k0 << 16));
	auto t = nitro::gen4Text(broken.data(), broken.size());
	EQ(t.size(), (size_t)6);
	if (t.size() == 6) { EQ(t[0], std::string("")); EQ(t[1], std::string("Line one\nLine two.")); }
	CHECK(nitro::gen4Text(bank.data(), 3).empty());
}

// Gen V: UTF-16 (0xFFFE newline, 0xFFFF end) XORed from the end with a key rotated right by 3 each character
static Bytes gen5Bank(const std::vector<std::u16string>& entries)
{
	Bytes sec; add32(sec, 0);
	sec.resize(4 + 8 * entries.size());
	for (size_t i = 0; i < entries.size(); i++)
	{
		std::vector<uint16_t> c(entries[i].begin(), entries[i].end());
		for (auto& ch : c) if (ch == u'\n') ch = 0xfffe;
		c.push_back(0xffff);
		uint16_t key = (uint16_t)(0x2983 * (i + 3));
		std::vector<uint16_t> e(c.size());
		for (size_t j = c.size(); j-- > 0;) { e[j] = c[j] ^ key; key = (uint16_t)(key >> 3 | key << 13); }
		put32(sec, 4 + 8 * i, (uint32_t)sec.size()); put16(sec, 8 + 8 * i, (uint16_t)c.size()); put16(sec, 10 + 8 * i, 0);
		for (uint16_t v : e) add16(sec, v);
	}
	put32(sec, 0, (uint32_t)sec.size());
	Bytes b; add16(b, 1); add16(b, (uint16_t)entries.size()); add32(b, (uint32_t)sec.size()); add32(b, 0); add32(b, 16);
	b.insert(b.end(), sec.begin(), sec.end());
	return b;
}

static void testGen5Text()
{
	Bytes bank = gen5Bank({ u"Snivy", u"Pokémon\nCenter", u"", u"⑭⑮" });
	auto s = nitro::gen5Text(bank.data(), bank.size());
	EQ(s.size(), (size_t)4);
	if (s.size() == 4)
	{
		EQ(s[0], std::string("Snivy"));
		EQ(s[1], std::string("Pokémon\nCenter"));
		EQ(s[2], std::string(""));
		EQ(s[3], std::string("♂♀"));
	}
	CHECK(nitro::gen5Text(bank.data(), 8).empty());
}


// ---- the player's own ROMs (when a folder of them is given) ---------------------------------------------------------
// What every game must show, from facts about the games (Bulbapedia / the games themselves), not from the decoder.
static const Location* zoneNamed(const RomData& r, const std::string& name, bool withEncounters = true)
{
	for (auto& z : r.zones) if (z.second.name == name && (!withEncounters || !z.second.encounters.empty())) return &z.second;
	return nullptr;
}
static bool hasEncounter(const Location* z, uint16_t species, EncounterKind kind = EncounterKind::GRASS)
{
	if (!z) return false;
	for (auto& e : z->encounters) if (e.species == species && e.kind == kind) return true;
	return false;
}
static const Trainer* trainerNamed(const RomData& r, const std::string& cls, const std::string& name)
{
	for (auto& t : r.trainers) if (t.second.cls == cls && t.second.name == name && !t.second.team.empty()) return &t.second;
	return nullptr;
}
static bool learns(const Species& s, uint16_t move, uint8_t level)
{
	for (auto& m : s.learnset) if (m.move == move && m.level == level) return true;
	return false;
}
static int evoCount(const Species& s, uint16_t method) { int n = 0; for (auto& e : s.evolutions) n += e.method == method; return n; }

static void checkGame(const RomData& r)
{
	const Game& g = r.game;
	const bool g5 = g.gen == Gen::V;
	const bool hgss = g.id == GameId::HEARTGOLD || g.id == GameId::SOULSILVER;
	const bool b2w2 = g.id == GameId::BLACK2 || g.id == GameId::WHITE2;
	fprintf(stderr, "  %s: %zu species, %zu moves, %zu items, %zu zones, %zu trainers\n", g.title.c_str(), r.species.size(), r.moves.size(), r.items.size(), r.zones.size(), r.trainers.size());
	EQ(g.romHash.size(), (size_t)40);
	CHECK(r.species.size() > (size_t)g.dexSize);
	if (r.species.size() <= (size_t)g.dexSize) return;
	// Bulbasaur
	const Species& b = r.species[1];
	// (Gen IV's English names are in capitals, as the games show them)
	EQ(b.name, std::string(g5 ? "Bulbasaur" : "BULBASAUR"));
	EQ((int)b.types[0], (int)GRASS); EQ((int)b.types[1], (int)POISON);
	EQ((int)b.base[0], 45); EQ((int)b.base[1], 49); EQ((int)b.base[3], 45); EQ((int)b.base[4], 65);
	EQ((int)b.catchRate, 45);
	EQ((int)b.height, 7); EQ((int)b.weight, 69);	// 0.7 m, 6.9 kg
	EQ((int)b.evYield[4], 1);	// one Sp. Atk point
	EQ(b.category, std::string("Seed Pokémon"));
	CHECK(b.dexText.size() > 40 && b.dexText.find('\n') == std::string::npos);
	if (b.abilities[0] < r.abilities.size()) EQ(r.abilities[b.abilities[0]], std::string("Overgrow"));
	if (g5) { CHECK(b.abilities[2] < r.abilities.size()); if (b.abilities[2] < r.abilities.size()) EQ(r.abilities[b.abilities[2]], std::string("Chlorophyll")); }
	CHECK(learns(b, 33, 1));	// Tackle at level 1
	EQ(b.evolutions.size(), (size_t)1);
	if (!b.evolutions.empty()) { EQ((int)b.evolutions[0].method, (int)evo::LEVEL); EQ((int)b.evolutions[0].param, 16); EQ((int)b.evolutions[0].into, 2); }
	CHECK(std::find(b.tms.begin(), b.tms.end(), 6) != b.tms.end());	// TM06 Toxic
	// Squirtle: Surf is HM03, after the 92 (Gen IV) or 95 (Gen V) TMs
	const auto& sq = r.species[7].tms;
	CHECK(std::find(sq.begin(), sq.end(), (g5 ? 95 : 92) + 3) != sq.end());
	// Pikachu: Thunderstone; Eevee: three stones, two friendship, two places (Moss Rock, Ice Rock)
	const Species& pika = r.species[25];
	EQ(pika.name, std::string(g5 ? "Pikachu" : "PIKACHU"));
	CHECK(learns(pika, 84, 1));	// Thunder Shock
	if (!pika.evolutions.empty()) { EQ((int)pika.evolutions[0].method, (int)evo::USE_ITEM); EQ((int)pika.evolutions[0].into, 26);
		if (pika.evolutions[0].param < r.items.size()) EQ(r.items[pika.evolutions[0].param].name, std::string("Thunderstone")); }
	CHECK(!pika.evolutions.empty() && pika.evolutions[0].how.find("Thunderstone") != std::string::npos);
	const Species& eevee = r.species[133];
	EQ(evoCount(eevee, evo::USE_ITEM), 3);
	EQ(evoCount(eevee, evo::FRIENDSHIP_DAY), 1);
	EQ(evoCount(eevee, evo::FRIENDSHIP_NIGHT), 1);
	EQ(evoCount(eevee, evo::MOSS_ROCK), 1);
	EQ(evoCount(eevee, evo::ICE_ROCK), 1);
	EQ(r.species[133].category, std::string("Evolution Pokémon"));
	// Magneton -> Magnezone at Mt. Coronet's (or Chargestone Cave's) magnetic field; Onix: trade holding Metal Coat
	EQ(evoCount(r.species[82], evo::MAGNETIC_FIELD), 1);
	EQ(evoCount(r.species[95], evo::TRADE_ITEM), 1);
	if (g5) { EQ(evoCount(r.species[588], evo::TRADE_SPECIES), 1); }	// Karrablast for Shelmet
	// each version's own Pokédex text: Black's Reshiram entry tells of its flames, White's of the weather
	if (g.id == GameId::BLACK) CHECK(r.species[643].dexText.find("sends flames") != std::string::npos);
	if (g.id == GameId::WHITE) CHECK(r.species[643].dexText.find("weather") != std::string::npos);
	if (g.id == GameId::DIAMOND) CHECK(r.species[25].dexText.find("lives in forests") != std::string::npos);
	if (g.id == GameId::PEARL) CHECK(r.species[25].dexText.find("crackling") != std::string::npos);
	if (g.id == GameId::PLATINUM) CHECK(r.species[25].dexText.find("recharge") != std::string::npos);
	if (g.id == GameId::HEARTGOLD) CHECK(r.species[25].dexText.find("roasts") != std::string::npos);	// (Gold's)
	if (g.id == GameId::SOULSILVER) CHECK(r.species[25].dexText.find("raises its tail") != std::string::npos);	// (Silver's)
	EQ(r.species[g.dexSize].name, std::string(g5 ? "Genesect" : "ARCEUS"));
	// moves: Pound; Tackle got stronger in Gen V; Curse is the ??? type in Gen IV
	CHECK(r.moves.size() > (size_t)(g5 ? 559 : 467));
	if (r.moves.size() > 174)
	{
		EQ(r.moves[1].name, std::string("Pound"));
		EQ((int)r.moves[1].power, 40); EQ((int)r.moves[1].accuracy, 100); EQ((int)r.moves[1].pp, 35);
		EQ((int)r.moves[1].cat, (int)MoveCategory::PHYSICAL);
		EQ((int)r.moves[33].power, g5 ? 50 : 35);
		EQ((int)r.moves[33].type, (int)NORMAL);
		EQ((int)r.moves[53].type, (int)FIRE);	// Flamethrower
		EQ((int)r.moves[53].cat, (int)MoveCategory::SPECIAL);
		EQ((int)r.moves[129].accuracy, 0);	// Swift never misses
		EQ((int)r.moves[174].type, (int)(g5 ? GHOST : NO_TYPE));
		EQ((int)(int8_t)r.moves[98].priority, 1);	// Quick Attack
		CHECK(!r.moves[1].description.empty());
	}
	// abilities, natures, items
	CHECK(r.abilities.size() > (size_t)(g5 ? 164 : 123));
	if (r.abilities.size() > 1) EQ(r.abilities[1], std::string("Stench"));
	EQ(r.abilityText.size(), r.abilities.size());
	EQ(r.natures.size(), (size_t)25);
	if (r.natures.size() == 25) { EQ(r.natures[0], std::string("Hardy")); EQ(r.natures[24], std::string("Quirky")); }
	CHECK(r.items.size() > 400);
	if (r.items.size() > 17)
	{
		EQ(r.items[1].name, std::string("Master Ball"));
		EQ(r.items[17].name, std::string("Potion"));
		EQ((int)r.items[17].price, 300);
		EQ((int)r.items[17].pocket, 1);	// medicine
		CHECK(!r.items[17].description.empty());
	}
	// TMs: TM01 Focus Punch (Gen IV) / Hone Claws (Gen V); Gen V's TM95 Snarl before HM01 Cut
	EQ(r.tmMoves.size(), (size_t)(g5 ? 101 : 100));
	if (r.tmMoves.size() >= 100)
	{
		EQ((int)r.tmMoves[0], g5 ? 468 : 264);
		EQ((int)r.tmMoves[g5 ? 95 : 92], 15);	// HM01 Cut
		if (g5) EQ((int)r.tmMoves[94], 555);
	}
	// places
	const char* home = g.id == GameId::DIAMOND || g.id == GameId::PEARL || g.id == GameId::PLATINUM ? "Twinleaf Town" : hgss ? "New Bark Town" : b2w2 ? "Aspertia City" : "Nuvema Town";
	CHECK(std::find(r.locationNames.begin(), r.locationNames.end(), home) != r.locationNames.end());
	CHECK(zoneNamed(r, home, false) != nullptr);
	if (g.id == GameId::PLATINUM) { CHECK(r.zones.count(200) && r.zones.at(200).name == "Valley Windworks"); }
	if (hgss) { CHECK(r.zones.count(10) && r.zones.at(10).name == "Route 2"); }
	if (b2w2) { CHECK(r.zones.count(200) && r.zones.at(200).name == "Twist Mountain"); }
	// wild Pokémon on the first route
	if (g.gen == Gen::IV && !hgss) { auto z = zoneNamed(r, "Route 201"); CHECK(hasEncounter(z, 396) && hasEncounter(z, 399)); }	// Starly, Bidoof
	if (hgss)
	{
		auto z = zoneNamed(r, "Route 29");
		CHECK(hasEncounter(z, 16) && hasEncounter(z, 161));	// Pidgey, Sentret
		// swarms only where the games have them: Qwilfish fished on Route 32, none on Route 29
		int swarms = 0;
		if (z) for (auto& e : z->encounters) swarms += e.kind == EncounterKind::SWARM;
		EQ(swarms, 0);
		CHECK(hasEncounter(zoneNamed(r, "Route 32"), 211, EncounterKind::SWARM));
	}
	if (g.id == GameId::BLACK || g.id == GameId::WHITE) { auto z = zoneNamed(r, "Route 1"); CHECK(hasEncounter(z, 504) && hasEncounter(z, 506)); }	// Patrat, Lillipup
	if (b2w2) { auto z = zoneNamed(r, "Route 19"); CHECK(hasEncounter(z, 504) && hasEncounter(z, 509)); }	// Patrat, Purrloin
	// Black 2/White 2's Hidden Grottos: twenty, one in Floccesy Ranch with Herdier at 10-15
	if (b2w2)
	{
		int grottoZones = 0;
		for (auto& z : r.zones) { bool any = false; for (auto& e : z.second.encounters) any = any || e.kind == EncounterKind::GROTTO; grottoZones += any; }
		EQ(grottoZones, 16);	// (Routes 3, 6, 13 and Abundant Shrine have two each)
		bool herdier = false;
		for (auto& z : r.zones)
			if (z.second.name == "Floccesy Ranch")
				for (auto& e : z.second.encounters) herdier = herdier || (e.kind == EncounterKind::GROTTO && e.species == 507 && e.minLevel == 10 && e.maxLevel == 15);
		CHECK(herdier);
	}
	// HeartGold/SoulSilver's headbutt trees: Pineco in many places
	if (hgss)
	{
		int zones = 0;
		for (auto& z : r.zones) zones += hasEncounter(&z.second, 204, EncounterKind::HEADBUTT);
		CHECK(zones > 10);
	}
	// surfing somewhere: Tentacool (72) in Gen IV, Basculin (550) in Gen V
	bool surf = false;
	for (auto& z : r.zones) surf = surf || hasEncounter(&z.second, g5 ? 550 : 72, EncounterKind::SURF);
	CHECK(surf);
	// every encounter is a real species with sane levels and odds
	int bad = 0;
	for (auto& z : r.zones) for (auto& e : z.second.encounters) bad += e.species == 0 || e.species > g.dexSize || e.minLevel == 0 || e.minLevel > e.maxLevel || e.maxLevel > 100 ||
		(e.kind == EncounterKind::GROTTO ? e.percent != 0 : e.percent == 0) || e.percent > 100;
	EQ(bad, 0);
	// every slot list adds up to 100% at every time of day (grass, water, rods, rock smash; per season in Gen V)
	int off = 0;
	for (auto& z : r.zones)
	{
		std::map<std::pair<int, int>, int> sum[4];	// time -> (kind, season) -> percent
		for (auto& e : z.second.encounters)
		{
			int k = (int)e.kind;
			bool slots = e.kind == EncounterKind::GRASS || e.kind == EncounterKind::SURF || e.kind == EncounterKind::OLD_ROD || e.kind == EncounterKind::GOOD_ROD ||
				e.kind == EncounterKind::SUPER_ROD || e.kind == EncounterKind::ROCK_SMASH || e.kind == DARK_GRASS || e.kind == SHAKING_GRASS || e.kind == RIPPLE_SURF || e.kind == RIPPLE_FISH;
			if (!slots) continue;
			for (int t = 1; t <= 3; t++) if (e.time == TimeOfDay::ANY || (int)e.time == t) sum[t][{ k, e.season }] += e.percent;
		}
		for (int t = 1; t <= 3; t++) for (auto& kv : sum[t]) if (kv.second != 100) { if (off++ < 3) fprintf(stderr, "  %s zone %u kind %d season %d time %d: %d%%\n", g.title.c_str(), z.first, kv.first.first, kv.first.second, t, kv.second); }
	}
	EQ(off, 0);
	// the first gym leader's team
	if (g.id == GameId::DIAMOND || g.id == GameId::PEARL || g.id == GameId::PLATINUM)
	{
		auto t = trainerNamed(r, "Leader", "Roark");
		CHECK(t && t->team.size() == 3);
		if (t && t->team.size() == 3) { EQ((int)t->team[0].species, 74); EQ((int)t->team[0].level, 12); EQ((int)t->team[2].species, 408); EQ((int)t->team[2].level, 14); }
	}
	if (hgss)
	{
		auto t = trainerNamed(r, "Leader", "Falkner");
		CHECK(t && t->team.size() == 2);
		if (t && t->team.size() == 2) { EQ((int)t->team[0].species, 16); EQ((int)t->team[0].level, 9); EQ((int)t->team[1].species, 17); EQ((int)t->team[1].level, 13); }
	}
	if (g.id == GameId::BLACK || g.id == GameId::WHITE)
	{
		auto t = trainerNamed(r, "Leader", "Lenora");
		CHECK(t && t->team.size() == 2);
		if (t && t->team.size() == 2) { EQ((int)t->team[0].species, 507); EQ((int)t->team[0].level, 18); EQ((int)t->team[1].species, 505); EQ((int)t->team[1].level, 20); }
	}
	if (b2w2)
	{
		auto t = trainerNamed(r, "Leader", "Cheren");
		CHECK(t && t->team.size() == 2);
		if (t && t->team.size() == 2) { EQ((int)t->team[0].species, 504); EQ((int)t->team[0].level, 11); EQ((int)t->team[1].species, 506); EQ((int)t->team[1].level, 13); }
	}
	// every trainer's Pokémon knows something (from the ROM, or the game's own default from the learnset)
	int noMoves = 0;
	for (auto& t : r.trainers) for (auto& m : t.second.team) noMoves += m.moves[0] == 0;
	EQ(noMoves, 0);
	// Species.formsAt/formCount: Deoxys has four formes, the Attack Forme right at formsAt; Pikachu none
	EQ((int)r.species[386].formCount, 4);
	CHECK(r.species[386].formsAt > g.dexSize && r.species[386].formsAt < r.species.size());
	if (r.species[386].formsAt < r.species.size()) EQ((int)r.species[r.species[386].formsAt].base[1], 180);
	EQ((int)r.species[479].formCount, g.id == GameId::DIAMOND || g.id == GameId::PEARL ? 1 : 6);	// Rotom's appliances from Platinum on
	EQ((int)r.species[25].formCount, 1); EQ((int)r.species[25].formsAt, 0);
	// Encounter.condition: Platinum's Route 201 dual slot names its GBA game; HGSS's Route 32 fishing swarm says
	// fishing (2); HGSS headbutt trees say their kind (0 common, 1 rare); Diamond's Route 201 swarm is grass (0)
	if (g.id == GameId::PLATINUM)
	{
		std::set<int> games;
		for (auto& z : r.zones) if (z.second.name == "Route 201") for (auto& e : z.second.encounters) if (e.kind == EncounterKind::DUAL_SLOT) games.insert(e.condition);
		CHECK(games == std::set<int>({ 1, 2, 3, 4, 5 }));
	}
	if (hgss)
	{
		bool fishSwarm = false, rare = false, common = false;
		for (auto& z : r.zones)
			for (auto& e : z.second.encounters)
			{
				fishSwarm = fishSwarm || (z.second.name == "Route 32" && e.kind == EncounterKind::SWARM && e.species == 211 && e.condition == 2);
				if (e.kind == EncounterKind::HEADBUTT) { common = common || e.condition == 0; rare = rare || e.condition == 1; }
			}
		CHECK(fishSwarm); CHECK(common); CHECK(rare);
	}
	if (g.id == GameId::DIAMOND) { auto z = zoneNamed(r, "Route 201"); bool ok = false; if (z) for (auto& e : z->encounters) ok = ok || (e.kind == EncounterKind::SWARM && e.condition == 0); CHECK(ok); }
	// forms: Deoxys' Attack Forme has 180 Attack
	uint16_t dx = speciesIndex(r, 386, 1);
	CHECK(dx > g.dexSize && dx < r.species.size());
	if (dx < r.species.size()) EQ((int)r.species[dx].base[1], 180);
	EQ((int)speciesIndex(r, 25, 0), 25);
	// sprites: Gen IV two 80x80 frames, Gen V 96x96 (animated); normal and shiny palettes differ
	auto t0 = std::chrono::steady_clock::now();
	auto sp = r.sprite(25);
	double spMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	fprintf(stderr, "  sprite 25: %.1f ms, %zu frames\n", spMs, sp ? sp->frames.size() : 0);
	CHECK(sp != nullptr);
	if (sp)
	{
		EQ(sp->w, g5 ? 96 : 80); EQ(sp->h, g5 ? 96 : 80);
		CHECK(sp->frames.size() >= (g5 ? 1u : 2u));
		CHECK(sp->normal != sp->shiny);
		size_t opaque = 0;
		if (!sp->frames.empty()) for (uint8_t v : sp->frames[0]) opaque += v != 0;
		CHECK(opaque > 500 && opaque < (size_t)(sp->w * sp->h) * 3 / 4);
		CHECK(sp->anim.size() >= 2);
	}
	// female-only species (Gen IV keeps only the female front: Nidoran F, Chansey, Jynx, Happiny) have a picture when
	// asked for as not female, as the pages do for wild rows (Platinum's Route 201 Nidoran F showed an empty circle)
	for (uint16_t n : { 29, 113, 124, 440 })
	{
		auto f = r.sprite(n, false, 0);
		CHECK(f != nullptr && !f->frames.empty());
	}
	// forms have their own pictures: Deoxys' Attack Forme isn't the Normal one
	{
		auto a = r.sprite(386, false, 0), b2 = r.sprite(386, false, 1);
		CHECK(a && b2 && !a->frames.empty() && !b2->frames.empty() && a->frames[0] != b2->frames[0]);
		// Unown's forms share its palette; Castform's rainy form has its own
		auto u = r.sprite(201, false, 5), u0 = r.sprite(201), c0 = r.sprite(351), c2 = r.sprite(351, false, 2);
		CHECK(u && u0 && u->normal == u0->normal && u->frames[0] != u0->frames[0]);
		CHECK(c0 && c2 && c0->normal != c2->normal);
		if (!g5 && !(g.id == GameId::DIAMOND || g.id == GameId::PEARL))	// Rotom's appliances: Platinum on
		{
			auto r0 = r.sprite(479), r1 = r.sprite(479, false, 1);
			CHECK(r0 && r1 && r0->normal != r1->normal);
		}
	}
}

// everything RomData holds, flattened (to compare a decode with its cache)
static std::string digestOf(const RomData& r)
{
	std::string d;
	auto n = [&](long v) { d += std::to_string(v); d += ','; };
	for (auto& s : r.species)
	{
		d += s.name + s.category + s.dexText; n(s.types[0]); n(s.types[1]);
		for (int k = 0; k < 6; k++) { n(s.base[k]); n(s.evYield[k]); }
		for (int k = 0; k < 3; k++) n(s.abilities[k]);
		n(s.catchRate); n(s.growth); n(s.genderRatio); n(s.baseFriendship); n(s.baseExp); n(s.height); n(s.weight); n(s.formsAt); n(s.formCount);
		for (auto& e : s.evolutions) { n(e.method); n(e.param); n(e.into); d += e.how; }
		for (auto& m : s.learnset) { n(m.level); n(m.move); }
		for (auto t : s.tms) n(t);
	}
	for (auto& m : r.moves) { d += m.name + m.description; n(m.type); n((int)m.cat); n(m.power); n(m.accuracy); n(m.pp); n(m.priority); n(m.effect); n(m.effectChance); }
	for (auto* v : { &r.abilities, &r.natures, &r.abilityText, &r.locationNames }) for (auto& s : *v) d += s + "|";
	for (auto& i : r.items) { d += i.name + i.description; n(i.pocket); n(i.price); }
	for (auto& z : r.zones) { n(z.first); d += z.second.name; for (auto& e : z.second.encounters) { n(e.species); n(e.minLevel); n(e.maxLevel); n(e.percent); n((int)e.kind); n((int)e.time); n(e.season); n(e.form); n(e.condition); } }
	for (auto& t : r.trainers) { n(t.first); d += t.second.cls + t.second.name; n(t.second.doubleBattle); for (auto& m : t.second.team) { n(m.species); n(m.item); n(m.level); for (auto mv : m.moves) n(mv); } }
	for (auto m : r.tmMoves) n(m);
	for (size_t i = 0; i < r.species.size(); i++) n(speciesIndex(r, (uint16_t)i, 1));
	return d;
}

// the battle overlay's bytes for the RAM reader: there, and (with NDS_RAM_DUMPS = ~/nds-companion/ram-kit/out, the RAM
// recorded on the handhelds) in RAM during the recorded battles and not on the field
static std::vector<uint8_t> dumpAt(const std::string& f, uint32_t addr, uint32_t len)
{
	std::vector<uint8_t> v(len);
	FILE* fp = fopen(f.c_str(), "rb");
	if (!fp) return {};
	const bool ok = fseek(fp, (long)(addr - 0x02000000), SEEK_SET) == 0 && fread(v.data(), 1, len, fp) == len;
	fclose(fp);
	return ok ? v : std::vector<uint8_t>();
}
static void checkHints(const RomData& r)
{
	const RamHints h = ramHints(r);
	CHECK(h.battleBytes.size() == 32 && h.battleSig.len == 32 && h.battleSig.addr >= 0x02000000 && h.battleSig.addr < 0x02400000);
	fprintf(stderr, "%s: battle overlay bytes at %08x\n", r.game.code.c_str(), h.battleSig.addr);
	const char* dumps = getenv("NDS_RAM_DUMPS");
	if (!dumps || h.battleBytes.empty()) return;
	const std::string d = dumps;
	struct { const char* code; const char* file; bool battle; } S[] = {
		{ "IPKE", "/plus-hgc/b1.bin", true }, { "IPKE", "/plus-hgf/b6.bin", true }, { "IPKE", "/plus-hgc/ow.bin", false },
		{ "IPKE", "/plus-hgd/c0.bin", false }, { "CPUE", "/plus-pta/ow.bin", false }, { "CPUE", "/rgds-pta/ow.bin", false },
		{ "IREO", "/plus-b2c/b1.bin", true }, { "IREO", "/plus-b2a/ow.bin", false }, { "IREO", "/plus-b2b/p0.bin", false } };
	for (auto& x : S)
	{
		if (r.game.code != x.code) continue;
		const auto v = dumpAt(d + x.file, h.battleSig.addr, h.battleSig.len);
		if (v.empty()) continue;
		CHECK((v == h.battleBytes) == x.battle);
		fprintf(stderr, "%s %s: %s\n", x.code, x.file, v == h.battleBytes ? "battle" : "field");
	}
}

static void testRoms(const std::string& dir, const std::string& cache)
{
	DIR* d = opendir(dir.c_str());
	if (!d) { fprintf(stderr, "FAIL no ROM folder %s\n", dir.c_str()); fails++; return; }
	std::vector<std::string> files;
	while (dirent* e = readdir(d)) { std::string n = e->d_name; if (n.size() > 4 && n.compare(n.size() - 4, 4, ".nds") == 0) files.push_back(dir + "/" + n); }
	closedir(d);
	std::sort(files.begin(), files.end());
	for (auto& f : files)
	{
		// with a cache: drop this ROM's, decode (and write it), then read it back; both must say the same
		std::string digest[2];
		for (int pass = 0; pass < (cache.empty() ? 1 : 2); pass++)
		{
			std::string err;
			RomLoadInfo info;
			auto r = loadRom(f, cache, &err, &info);
			CHECK(r != nullptr);
			if (!r) { fprintf(stderr, "FAIL %s: %s\n", f.c_str(), err.c_str()); break; }
			if (pass == 0 && info.fromCache) { unlink(info.cacheFile.c_str()); r = loadRom(f, cache, &err, &info); if (!r) break; }
			fprintf(stderr, "%s: %s in %.0f ms (cache %zu KiB)\n", r->game.code.c_str(), info.fromCache ? "cache read" : "decoded", info.ms, info.cacheBytes / 1024);
			if (!cache.empty()) EQ(info.fromCache, pass == 1);
			checkGame(*r);
			digest[pass] = digestOf(*r);
			if (pass == 0) checkHints(*r);
		}
		if (!cache.empty()) CHECK(!digest[0].empty() && digest[0] == digest[1]);
	}
	rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	fprintf(stderr, "peak memory %ld MiB\n", ru.ru_maxrss / 1024);
}

int main(int argc, char** argv)
{
	testSha1();
	testNarc();
	testFileSystem();
	testBlz();
	testLz();
	testGen4Text();
	testGen5Text();
	if (argc > 1) testRoms(argv[1], argc > 2 ? argv[2] : "");
	fprintf(stderr, "nds-rom-test: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
