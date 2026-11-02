// The DS Pokémon companion's ROM reader (src/nds/nitro.cpp, src/nds/rom.cpp) on data made here from the documented
// layouts (no game data in it), and, when a folder of the player's own ROMs is given, on those: counts and spot
// values every game must show.
//   make tests/nds-rom-test && tests/nds-rom-test [roms-dir [cache-dir]]
#include "nds/nitro.h"
#include "nds/rom.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
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

int main(int argc, char** argv)
{
	testSha1();
	testNarc();
	testFileSystem();
	testBlz();
	testGen4Text();
	testGen5Text();
	fprintf(stderr, "nds-rom-test: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
