// The Pokémon companion's decoder (src/pokemon.cpp) on data made here from the documented layouts (Bulbapedia:
// "Pokémon data structure (Generation III)", "Character encoding (Generation III)"): no ROM or save in it.
//   g++ -std=c++17 -I src tests/pokemon-test.cpp src/pokemon.cpp src/zip.cpp -lz -lpthread && ./a.out
#include "pokemon.h"
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <unistd.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

using namespace banana::pkmn;

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define EQ(a, b) do { checks++; auto _a = (a); auto _b = (b); if (!(_a == _b)) { fails++; fprintf(stderr, "FAIL %s:%d: %s == %s (%s vs %s)\n", __FILE__, __LINE__, #a, #b, str(_a).c_str(), str(_b).c_str()); } } while (0)
static std::string str(const std::string& s) { return "\"" + s + "\""; }
template <class T> static std::string str(T v) { return std::to_string(v); }

static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

// the games' text from ASCII (capitals, small letters, digits, space)
static std::vector<uint8_t> enc(const std::string& s)
{
	std::vector<uint8_t> o;
	for (char c : s)
		o.push_back(c >= 'A' && c <= 'Z' ? 0xbb + (c - 'A') : c >= 'a' && c <= 'z' ? 0xd5 + (c - 'a') : c >= '0' && c <= '9' ? 0xa1 + (c - '0') :
			c == ' ' ? 0x00 : c == '?' ? 0xac : c == '-' ? 0xae : c == '.' ? 0xad : 0xac);
	return o;
}

// the 24 orders of the four parts, as Bulbapedia's table lists them (G growth, A attacks, E EVs, M misc), by
// personality % 24
static const char* const ORDERS[24] = { "GAEM", "GAME", "GEAM", "GEMA", "GMAE", "GMEA", "AGEM", "AGME", "AEGM", "AEMG", "AMGE", "AMEG",
	"EGAM", "EGMA", "EAGM", "EAMG", "EMGA", "EMAG", "MGAE", "MGEA", "MAGE", "MAEG", "MEGA", "MEAG" };

struct Plain { uint32_t pid, otid; std::string nick; uint16_t species, item; uint16_t moves[4]; uint8_t pp[4]; uint8_t ppBonuses;
	uint32_t status; uint8_t level; uint16_t hp, maxHp; bool egg; };

// a party slot as the game keeps it: the 48 bytes of parts in the personality's order, summed, then XORed with
// personality ^ OT ID a word at a time
// plain: as a CFRU hack keeps it (the parts in the first order, not encrypted)
static std::vector<uint8_t> partySlot(const Plain& m, bool breakSum = false, bool plain = false)
{
	std::vector<uint8_t> p(100, 0);
	put32(&p[0], m.pid); put32(&p[4], m.otid);
	auto n = enc(m.nick);
	for (size_t i = 0; i < 10; i++) p[8 + i] = i < n.size() ? n[i] : 0xff;
	p[0x12] = 2;	// English
	p[0x13] = m.egg ? 0x06 : 0x02;
	uint8_t g[12] = {}, a[12] = {}, e[12] = {}, x[12] = {};
	put16(g, m.species); put16(g + 2, m.item); put32(g + 4, (uint32_t)(m.level * m.level * m.level + 40 * m.level)); g[8] = m.ppBonuses; g[9] = 70;
	for (int i = 0; i < 4; i++) { put16(a + i * 2, m.moves[i]); a[8 + i] = m.pp[i]; }
	e[0] = 4; e[1] = 252; e[3] = 6; e[6] = 10;
	// IVs 31, 20, 15, 10, 5, 0 (HP, Attack, Defense, Speed, Sp. Atk, Sp. Def), the second ability when the personality is odd
	put32(x + 4, 31u | 20u << 5 | 15u << 10 | 10u << 15 | 5u << 20 | (m.egg ? 1u << 30 : 0) | (m.pid & 1) << 31);
	uint8_t d[48];
	const char* ord = ORDERS[plain ? 0 : m.pid % 24];
	for (int pos = 0; pos < 4; pos++)
		memcpy(d + pos * 12, ord[pos] == 'G' ? g : ord[pos] == 'A' ? a : ord[pos] == 'E' ? e : x, 12);
	uint16_t sum = 0;
	for (int i = 0; i < 48; i += 2) sum = (uint16_t)(sum + (d[i] | (d[i + 1] << 8)));
	put16(&p[0x1c], (uint16_t)(sum + (breakSum ? 1 : 0)));
	uint32_t key = plain ? 0 : m.pid ^ m.otid;
	for (int i = 0; i < 48; i += 4)
	{
		uint32_t v = (d[i] | (d[i + 1] << 8) | (d[i + 2] << 16) | ((uint32_t)d[i + 3] << 24)) ^ key;
		put32(&p[0x20 + i], v);
	}
	put32(&p[0x50], m.status); p[0x54] = m.level; put16(&p[0x56], m.hp); put16(&p[0x58], m.maxHp);
	for (int i = 0; i < 5; i++) put16(&p[0x5a + i * 2], (uint16_t)(m.level * 2 + 1 + i));	// Attack, Defense, Speed, Sp. Atk, Sp. Def
	return p;
}

static std::vector<uint8_t> battler(uint16_t species, const std::string& nick, uint8_t level, uint16_t hp, uint16_t maxHp, uint8_t t1, uint8_t t2,
	std::vector<uint16_t> moves)
{
	std::vector<uint8_t> b(0x58, 0);
	put16(&b[0], species);
	for (size_t i = 0; i < moves.size(); i++) { put16(&b[0x0c + i * 2], moves[i]); b[0x24 + i] = 10; }
	b[0x21] = t1; b[0x22] = t2;
	put16(&b[0x28], hp); b[0x2a] = level; put16(&b[0x2c], maxHp);
	auto n = enc(nick);
	for (size_t i = 0; i < 11; i++) b[0x30 + i] = i < n.size() ? n[i] : 0xff;
	return b;
}

// the rest a made-up ROM needs for icons, genders and experience, at base: a menu icon for every species (our own
// drawing: a round creature, its colour and eyes by species), their palette numbers just after the pointer table,
// the three palettes behind sprite-palette records (tags 56000-56005), the six experience tables; and in the base
// stats at st (28 bytes a species) a gender ratio and growth rate
static uint8_t iconPixel(int species, int x, int y)
{
	if (species == 412)	// the egg: an oval with spots
	{
		float dx = (x - 15.5f) / 10, dy = (y - 17.5f) / 13, d = dx * dx + dy * dy;
		return d > 1.15f ? 0 : d > 0.85f ? 1 : ((x / 4 + y / 5) % 3 == 0 ? 5 : 15);
	}
	int r = 9 + species % 4;
	float dx = (x - 15.5f) / r, dy = (y - 18.5f) / (r - 1), d = dx * dx + dy * dy;
	if (d > 1.2f) return (y < 9 && (x == 9 + species % 3 || x == 22 - species % 3) && y > 3) ? 1 : 0;	// ears
	if (d > 0.88f) return 1;
	if ((x == 12 || x == 19) && (y == 16 || y == 17)) return 1;	// eyes
	if (x == 11 && y == 15) return 15;
	if (y > 22 && d < 0.5f) return 14;							// a lighter belly
	return (uint8_t)(2 + species % 6);
}
static void addExtras(std::vector<uint8_t>& rom, size_t st, size_t base)
{
	const int N = 413;
	size_t table = base, pals = base + N * 4 + 3, recs = base + 0xa00, palData = recs + 0x40, exp = base + 0x1000, tiles = base + 0x2000;
	// a look-alike before it, as in Emerald: the footprint table, the same shape (pointers by species, the same ones
	// shared) and its pictures as far apart, each just after an icon (the first finder took it: noise for every
	// icon on the device). Each icon here 2 KB apart, its "footprint" in the second KB
	for (int sp = 0; sp < N; sp++)
	{
		int src = sp == 0 ? 1 : (sp >= 252 && sp <= 276) ? 252 : sp;
		put32(&rom[base - 0x800 + sp * 4], 0x08000000u + (uint32_t)(tiles + src * 2048 + 1024));
		for (int k = 0; k < 32; k++) rom[tiles + src * 2048 + 1024 + k] = (uint8_t)(0x9a + k * 7);
	}
	for (int sp = 0; sp < N; sp++)
	{
		// none and Bulbasaur share an icon, the 25 placeholders after Celebi one question mark (their own made-up one)
		int src = sp == 0 ? 1 : (sp >= 252 && sp <= 276) ? 252 : sp;
		put32(&rom[table + sp * 4], 0x08000000u + (uint32_t)(tiles + src * 2048));
		rom[pals + sp] = (uint8_t)(sp == 1 ? 1 : (sp >= 252 && sp <= 276) ? 0 : sp % 3);
	}
	for (int sp = 1; sp < N; sp++)
	{
		uint8_t* g = &rom[tiles + sp * 2048];
		for (int y = 0; y < 32; y++)
			for (int x = 0; x < 32; x++)
			{
				uint8_t v = iconPixel(sp, x, y);
				uint8_t& b = g[((y / 8) * 4 + x / 8) * 32 + (y % 8) * 4 + (x % 8) / 2];
				b = x & 1 ? (uint8_t)((b & 15) | v << 4) : (uint8_t)((b & 0xf0) | v);
			}
	}
	static const uint16_t base3[3][6] = { { 0x7e94, 0x1f3f, 0x03df, 0x2a5f, 0x5ef7, 0x6318 }, { 0x26f3, 0x4b8b, 0x7f0e, 0x3e9f, 0x2bff, 0x575f },
		{ 0x5d9f, 0x7a2e, 0x4ed3, 0x1e7d, 0x6b5a, 0x3ad6 } };
	for (int i = 0; i < 6; i++)
	{
		put32(&rom[recs + i * 8], 0x08000000u + (uint32_t)(palData + (i % 3) * 32));
		put16(&rom[recs + i * 8 + 4], (uint16_t)(56000 + i));
		put16(&rom[recs + i * 8 + 6], 0);
	}
	for (int i = 0; i < 3; i++)
		for (int k = 0; k < 16; k++)
			put16(&rom[palData + i * 32 + k * 2], k == 0 ? 0x7c1f : k == 1 ? 0x0c63 : k == 14 ? 0x6f7b : k == 15 ? 0x7fff : base3[i][(k - 2) % 6]);
	// Medium Fast n^3 (with level 1: 1), and five more rising curves
	for (int g = 0; g < 6; g++)
		for (int l = 0; l <= 100; l++)
		{
			uint32_t v = l == 0 ? 0 : l == 1 ? 1 : g == 0 ? (uint32_t)(l * l * l) : (uint32_t)(l * l * l * (4 + g) / 5);
			put32(&rom[exp + (g * 101 + l) * 4], v);
		}
	for (int sp = 0; sp < 412; sp++) { rom[st + sp * 28 + 0x10] = (uint8_t)(sp % 4 == 0 ? 255 : sp % 4 == 1 ? 31 : sp % 4 == 2 ? 127 : 254); rom[st + sp * 28 + 0x13] = (uint8_t)(sp % 6); }
}

// icons side by side, 4x, on a grey ground, to a PNG for a look
static bool iconsPng(const RomTables& t, const std::vector<std::pair<int, bool>>& mons, const std::string& path)
{
	const int K = 4, W = (int)mons.size() * 36 * K, H = 36 * K;
	std::vector<uint8_t> img((size_t)W * H * 3, 0x60);
	for (size_t m = 0; m < mons.size(); m++)
	{
		std::vector<uint8_t> rgba;
		if (!t.iconRGBA((uint16_t)mons[m].first, mons[m].second, rgba)) return false;
		for (int y = 0; y < 32 * K; y++)
			for (int x = 0; x < 32 * K; x++)
			{
				const uint8_t* c = &rgba[((y / K) * 32 + x / K) * 4];
				if (!c[3]) continue;
				uint8_t* d = &img[((size_t)(y + 2 * K) * W + m * 36 * K + 2 * K + x) * 3];
				d[0] = c[0]; d[1] = c[1]; d[2] = c[2];
			}
	}
	return stbi_write_png(path.c_str(), W, H, 3, img.data(), W * 3) != 0;
}

// a 32x32 icon decoded here on its own from 1 KB of GBA tiles (4 bits a pixel, 8x8 tiles of 32 bytes, 4x4 of them)
static std::vector<uint8_t> tilesToPixels(const uint8_t* g)
{
	std::vector<uint8_t> px(32 * 32);
	for (int y = 0; y < 32; y++)
		for (int x = 0; x < 32; x++)
		{
			int tile = (y / 8) * 4 + x / 8, off = tile * 32 + (y % 8) * 4 + (x % 8) / 2;
			px[y * 32 + x] = (x % 2) ? g[off] >> 4 : g[off] & 15;
		}
	return px;
}

static std::string reply(uint32_t addr, const std::vector<uint8_t>& v)
{
	char h[32];
	snprintf(h, sizeof h, "READ_CORE_MEMORY %x", addr);
	std::string s = h;
	for (uint8_t b : v) { snprintf(h, sizeof h, " %02X", b); s += h; }
	return s + "\n";
}

int main(int argc, char** argv)
{
	// ---- the type chart (Generation III: no Fairy; Steel resists Ghost and Dark)
	EQ(effect(FIRE, GRASS, STEEL), 16);
	EQ(effect(FIRE, GRASS, GRASS), 8);
	EQ(effect(FIRE, GRASS, 255), 8);
	EQ(effect(ELECTRIC, GROUND, FLYING), 0);
	EQ(effect(WATER, WATER, DRAGON), 1);
	EQ(effect(NORMAL, NORMAL, 255), 4);
	EQ(effect(GHOST, NORMAL, 255), 0);
	EQ(effect(GHOST, STEEL, 255), 2);
	EQ(effect(DARK, STEEL, 255), 2);
	EQ(effect(ICE, DRAGON, FLYING), 16);
	EQ(effect(GROUND, FLYING, ROCK), 0);
	EQ(effect(FIGHTING, NORMAL, ROCK), 16);
	EQ(effect(BUG, PSYCHIC, DARK), 16);
	EQ(effect(POISON, STEEL, 255), 0);
	EQ(effect(PSYCHIC, DARK, 255), 0);
	EQ(effect(GRASS, WATER, GROUND), 16);
	EQ(effect(ROCK, FIRE, FLYING), 16);
	// the modern chart (CFRU hacks): Fairy, and Steel no longer resisting Ghost and Dark; the old one unchanged
	EQ(effect(FAIRY, DRAGON, 255, true), 8);
	EQ(effect(DRAGON, FAIRY, 255, true), 0);
	EQ(effect(POISON, FAIRY, GRASS, true), 16);
	EQ(effect(GHOST, STEEL, 255, true), 4);
	EQ(effect(GHOST, STEEL, 255), 2);
	CHECK(typeInGame(FAIRY, true) && !typeInGame(FAIRY, false) && !typeInGame(MYSTERY, true) && !typeInGame(20, true));
	EQ(std::string(typeName(DARK)), std::string("Dark"));

	// ---- the game from the header's code
	{
		std::vector<uint8_t> h(0xc0, 0);
		memcpy(&h[0xa0], "POKEMON EMER", 12);
		memcpy(&h[0xac], "BPEE", 4);
		const GameProfile* g = detectGame(h.data(), h.size());
		CHECK(g && std::string(g->name) == "Pokémon Emerald" && g->party == 0x020244ec);
		memcpy(&h[0xac], "BPRE", 4);
		g = detectGame(h.data(), h.size());
		CHECK(g && g->party == 0x02024284 && g->partyCount == 0x02024029);
		memcpy(&h[0xac], "AXVE", 4);
		g = detectGame(h.data(), h.size());
		CHECK(g && g->party == 0x03004360);
		memcpy(&h[0xac], "BPED", 4);	// German Emerald: other addresses, not known
		CHECK(detectGame(h.data(), h.size()) == nullptr);
		memcpy(&h[0xac], "AGBE", 4);
		CHECK(detectGame(h.data(), h.size()) == nullptr);
		CHECK(detectGame(h.data(), 0x80) == nullptr);
	}

	// ---- text
	{
		auto t = enc("PIKACHU"); t.push_back(0xff); t.push_back(0xbb);
		EQ(decodeText(t.data(), t.size()), std::string("PIKACHU"));
		uint8_t s[] = { 0xca, 0xc9, 0xc5, 0x1b, 0x00, 0xb5, 0xb6, 0xa1, 0xa9, 0xff };
		EQ(decodeText(s, sizeof s), std::string("POKé ♂♀08"));
		uint8_t full[10]; auto n = enc("ABCDEFGHIJ"); memcpy(full, n.data(), 10);	// ten letters: no end byte
		EQ(decodeText(full, 10), std::string("ABCDEFGHIJ"));
	}

	// ---- a party slot in each of the 24 orders
	for (uint32_t k = 0; k < 24; k++)
	{
		Plain m{ 0x9e3779b0u + k, 0x12345678u, "Sparky", 25, 13, { 84, 39, 86, 98 }, { 30, 28, 20, 29 }, 0x1b, 0x40, 23, 41, 52, false };
		CHECK(m.pid % 24 == (0x9e3779b0u % 24 + k) % 24);
		Mon d;
		bool ok = decodeParty(partySlot(m).data(), d);
		CHECK(ok);
		EQ(d.species, (uint16_t)25);
		EQ(d.item, (uint16_t)13);
		EQ(d.nick, std::string("Sparky"));
		EQ(d.level, (uint8_t)23);
		EQ(d.hp, (uint16_t)41);
		EQ(d.maxHp, (uint16_t)52);
		EQ(d.moves[0], (uint16_t)84); EQ(d.moves[3], (uint16_t)98);
		EQ(d.pp[1], (uint8_t)28);
		EQ(d.ppBonuses, (uint8_t)0x1b);
		EQ(std::string(statusName(d.status)), std::string("PAR"));
		CHECK(!d.egg);
		EQ(d.friendship, (uint8_t)70);
		EQ(d.stats[0], (uint16_t)47); EQ(d.stats[2], (uint16_t)49); EQ(d.stats[4], (uint16_t)51);
		EQ(d.ivs[0], (uint8_t)31); EQ(d.ivs[1], (uint8_t)20); EQ(d.ivs[3], (uint8_t)10); EQ(d.ivs[5], (uint8_t)0);
		EQ(d.evs[0], (uint8_t)4); EQ(d.evs[1], (uint8_t)252); EQ(d.evs[3], (uint8_t)6); EQ(d.evs[5], (uint8_t)0);
		EQ((int)d.abilitySlot, (int)(m.pid & 1));
		EQ(d.nature(), (int)(m.pid % 25));
	}
	// ---- the natures: five by five, the row raised, the column lowered (Attack, Defense, Speed, Sp. Atk, Sp. Def)
	{
		int up, down;
		EQ(std::string(natureName(3)), std::string("Adamant"));
		natureStats(3, up, down); EQ(up, 0); EQ(down, 3);		// Adamant: Attack up, Sp. Atk down
		EQ(std::string(natureName(10)), std::string("Timid"));
		natureStats(10, up, down); EQ(up, 2); EQ(down, 0);		// Timid: Speed up, Attack down
		EQ(std::string(natureName(24)), std::string("Quirky"));
		natureStats(24, up, down); EQ(up, -1); EQ(down, -1);	// Quirky: neither
		natureStats(12, up, down); EQ(up, -1);					// Serious
		EQ(std::string(natureName(25)), std::string(""));
	}
	{
		Plain m{ 7, 99, "EGG", 152, 0, { 33, 45, 0, 0 }, { 35, 40, 0, 0 }, 0, 0, 5, 20, 20, true };
		Mon d;
		CHECK(decodeParty(partySlot(m).data(), d) && d.egg);
		CHECK(!decodeParty(partySlot(m, true).data(), d));		// a wrong checksum
		std::vector<uint8_t> empty(100, 0);
		CHECK(!decodeParty(empty.data(), d));					// an empty slot (all zero: its sum fits, species 0)
	}
	// ---- a CFRU hack's party slots (Pokémon Unbound): not encrypted, the first order, the checksum as before
	for (uint32_t k = 0; k < 24; k++)
	{
		Plain m{ 0x9e3779b0u + k, 0x12345678u, "Sparky", 906, 13, { 84, 39, 86, 700 }, { 30, 28, 20, 29 }, 0x1b, 0x40, 23, 41, 52, false };
		Mon d;
		auto plain = partySlot(m, false, true), enc = partySlot(m);
		CHECK(decodeParty(plain.data(), d, MonCoding::PLAIN) && d.species == 906 && d.moves[3] == 700 && d.level == 23 && d.nick == "Sparky" && !d.egg);
		CHECK(!decodeParty(plain.data(), d, MonCoding::ENCRYPTED));	// (the games' decoding: noise, the empty party)
		CHECK(decodeParty(plain.data(), d, MonCoding::EITHER) && d.species == 906);
		CHECK(decodeParty(enc.data(), d, MonCoding::EITHER) && d.species == 906);
		CHECK(!decodeParty(enc.data(), d, MonCoding::PLAIN));
		CHECK(!decodeParty(partySlot(m, true, true).data(), d, MonCoding::PLAIN));
		// Pokémon Unbound leaves the checksum 0: the slot counts by its "has a species" bit (0x13 bit 1)
		auto z = plain; z[0x1c] = z[0x1d] = 0;
		CHECK(decodeParty(z.data(), d, MonCoding::PLAIN) && d.species == 906);
		z[0x13] = 0;
		CHECK(!decodeParty(z.data(), d, MonCoding::PLAIN));
		z[0x13] = 3;	// a bad egg
		CHECK(!decodeParty(z.data(), d, MonCoding::PLAIN));
	}
	{
		// the Larvitar Pokémon Unbound 2.1.1.1 gives at its start, as gpSP's EWRAM had it (gPlayerParty[0])
		auto h = [](const char* x) { std::vector<uint8_t> v; for (; x[0] && x[1]; x += 2) v.push_back((uint8_t)strtoul(std::string(x, 2).c_str(), nullptr, 16)); return v; };
		auto lv = h("4f5bf479a95be606c6d5e6eadde8d5e6ffef0202bbd5ffffffffff0000000000f6002c0030020000002303002b005800c9002c001e0f0a1900000000000000000000000000850a02397ecb1e00000000000000000aff20002000110012000f000f001100");
		Mon d;
		CHECK(lv.size() == 100 && decodeParty(lv.data(), d, MonCoding::PLAIN));
		EQ(d.species, (uint16_t)246);
		EQ(d.nick, std::string("Larvitar"));
		EQ((int)d.level, 10);
		CHECK(!decodeParty(lv.data(), d, MonCoding::ENCRYPTED));
		CHECK(decodeParty(lv.data(), d, MonCoding::EITHER) && d.species == 246);
	}
	EQ(maxPP(35, 0, 0), 35);
	EQ(maxPP(35, 0x0c, 1), 56);	// three PP Ups on the second move: 35 + 21
	EQ(maxPP(5, 0x03, 0), 8);
	EQ(std::string(statusName(3)), std::string("SLP"));
	EQ(std::string(statusName(0x80)), std::string("PSN"));
	EQ(std::string(statusName(0)), std::string(""));

	// ---- the battle's own copy
	{
		auto b = battler(277, "TREECKO", 9, 15, 27, GRASS, GRASS, { 1, 43, 71 });
		Mon d;
		CHECK(decodeBattler(b.data(), d));
		EQ(d.species, (uint16_t)277); EQ(d.nick, std::string("TREECKO")); EQ(d.level, (uint8_t)9);
		EQ(d.hp, (uint16_t)15); EQ(d.maxHp, (uint16_t)27); EQ(d.types[0], (uint8_t)GRASS); EQ(d.moves[2], (uint16_t)71);
		std::vector<uint8_t> z(0x58, 0);
		CHECK(!decodeBattler(z.data(), d));
	}

	// ---- the layouts as data: a species packed with other bits, a battler with its fields elsewhere
	{
		Plain m{ 0x1234567u, 0x89abcdefu, "Kid", 155 | (5 << 11), 0, { 33, 0, 0, 0 }, { 35, 0, 0, 0 }, 0, 0, 5, 20, 20, false };
		MonLayout L;
		L.speciesMask = 0x07ff;
		Mon d;
		CHECK(decodeParty(partySlot(m).data(), d, L) && d.species == 155);
		CHECK(decodeParty(partySlot(m).data(), d) && d.species == (155 | (5 << 11)));	// (the games': as it is)
		Plain zero{ 0x1234567u, 0x89abcdefu, "Kid", 5 << 11, 0, { 33, 0, 0, 0 }, { 35, 0, 0, 0 }, 0, 0, 5, 20, 20, false };
		CHECK(!decodeParty(partySlot(zero).data(), d, L));		// (no species in its bits)
		// a battler 8 bytes longer, everything after the types moved on by 8
		auto b = battler(277, "TREECKO", 9, 15, 27, GRASS, GRASS, { 1, 43, 71 });
		std::vector<uint8_t> w(b.begin(), b.begin() + 0x23);
		w.insert(w.end(), 8, 0);
		w.insert(w.end(), b.begin() + 0x23, b.end());
		BattlerLayout W;
		W.size = 0x60; W.pp += 8; W.hp += 8; W.level += 8; W.maxHp += 8; W.item += 8; W.nick += 8; W.ppBonuses += 8; W.exp += 8; W.pid += 8; W.status += 8;
		CHECK(decodeBattler(w.data(), d, 450, W) && d.species == 277 && d.nick == "TREECKO" && d.level == 9 && d.hp == 15 && d.maxHp == 27 && d.moves[2] == 71);
		CHECK(decodeBattler(w.data(), d) && d.level != 9);		// (read as the games' keep it: wrong)
	}

	// ---- profiles: a release told by its CRC-32 before its code's own
	{
		std::vector<uint8_t> h(0xc0, 0);
		memcpy(&h[0xac], "BPEE", 4);
		const GameProfile* e = detectGame(h.data(), h.size());
		CHECK(e && e->family == EMERALD && !e->fixedSave && e->save.key == 0xac && e->mon.size == 100 && e->mon.battler.size == 0x58);
		CHECK(!crcMatters(h.data(), h.size()));		// (none listed yet)
		GameProfile hack = *e;
		hack.name = "A hack"; hack.crc = 0x12345678; hack.party = 0x02019c20;
		GameProfile list[] = { *e, hack };
		CHECK(detectGameIn(list, 2, h.data(), h.size(), 0x12345678)->party == 0x02019c20);
		CHECK(detectGameIn(list, 2, h.data(), h.size(), 0x87654321)->party == 0x020244ec);	// (another release: the code's)
		CHECK(detectGameIn(list, 2, h.data(), h.size(), 0)->party == 0x020244ec);
		CHECK(detectGameIn(list + 1, 1, h.data(), h.size(), 0) == nullptr);		// (only a release: not this one, not known)
		memcpy(&h[0xac], "AXVE", 4);
		const GameProfile* r = detectGame(h.data(), h.size());
		CHECK(r && r->family == RUBY_SAPPHIRE && r->fixedSave && r->save.key == 0);

		// the companion reads where its profile says, slots as long as its layout's
		hack.mon.size = 104;
		Companion c;
		c.openWith(&hack);
		RomTables t;
		for (int i = 0; i < 412; i++) { t.species.push_back("S" + std::to_string(i)); t.speciesTypes.push_back({ (uint8_t)WATER, (uint8_t)GROUND }); }
		c.setTables(t);
		std::string q = c.requests(0);
		CHECK(q.find("READ_CORE_MEMORY 2019c20 624\n") != std::string::npos);
	}

	// ---- the tables, found in a made-up ROM by their shape (with look-alikes before them)
	{
		std::mt19937 rng(42);
		std::vector<uint8_t> rom(4u << 20);
		for (auto& b : rom) b = (uint8_t)rng();
		auto names = [&](size_t at, size_t entry, size_t count, const std::vector<uint8_t>& first, const char* prefix, bool dollars)
		{
			for (size_t i = 0; i < count; i++)
			{
				uint8_t* e = &rom[at + i * entry];
				memset(e, dollars ? 0xff : 0x00, entry);
				std::vector<uint8_t> n = i == 0 ? first : enc(prefix + std::to_string(i));
				memcpy(e, n.data(), n.size());
				if (n.size() < entry && i > 0) e[n.size()] = 0xff;
			}
		};
		std::vector<uint8_t> q(11, 0xac); q[10] = 0xff;
		// a decoy: "??????????" with garbage after it
		memcpy(&rom[1000], q.data(), q.size());
		names(0x100003, 11, 412, q, "MON", false);
		std::vector<uint8_t> dash = { 0xae, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };	// FireRed's "-$$$$$$"
		rom[2000] = 0xae; rom[2001] = 0xff;
		names(0x180001, 13, 355, dash, "MOVE", false);
		// base stats: Bulbasaur at entry 1, Ivysaur at 2, then made-up ones with types by index
		size_t st = 0x200000;
		uint8_t bulba[8] = { 45, 49, 49, 45, 65, 65, GRASS, POISON }, ivy[8] = { 60, 62, 63, 60, 80, 80, GRASS, POISON };
		memcpy(&rom[3000], bulba, 8);	// (a decoy: no Ivysaur after it)
		for (int i = 0; i < 412; i++) { rom[st + i * 28 + 6] = (uint8_t)(i % 18); rom[st + i * 28 + 7] = (uint8_t)((i + 1) % 18); }
		memcpy(&rom[st + 28], bulba, 8); memcpy(&rom[st + 56], ivy, 8);
		size_t mv = 0x280004;
		for (int i = 0; i < 355; i++) { uint8_t* e = &rom[mv + i * 12]; e[1] = (uint8_t)(i % 150); e[2] = (uint8_t)(i % 18); e[3] = 100; e[4] = (uint8_t)(5 + i % 35); }
		uint8_t pound[5] = { 0, 40, NORMAL, 100, 35 }, chop[5] = { 43, 50, FIGHTING, 100, 25 }, slap[5] = { 29, 15, NORMAL, 85, 10 };
		memcpy(&rom[mv + 12], pound, 5); memcpy(&rom[mv + 24], chop, 5); memcpy(&rom[mv + 36], slap, 5);
		addExtras(rom, st, 0x300000);
		RomTables t = findTables(rom.data(), rom.size());
		EQ(t.species.size(), (size_t)412);
		EQ(t.speciesName(0), std::string("??????????"));
		EQ(t.speciesName(25), std::string("MON25"));
		EQ(t.speciesName(411), std::string("MON411"));
		EQ(t.speciesName(500), std::string("#500"));
		EQ(t.moves.size(), (size_t)355);
		EQ(t.moveName(0), std::string("-"));
		EQ(t.moveName(354), std::string("MOVE354"));
		EQ(t.speciesTypes.size(), (size_t)412);
		EQ((int)t.speciesTypes[1][0], (int)GRASS); EQ((int)t.speciesTypes[1][1], (int)POISON);
		EQ((int)t.speciesTypes[100][0], 100 % 18);
		EQ(t.moveInfo.size(), (size_t)355);
		EQ((int)t.moveInfo[1].power, 40); EQ((int)t.moveInfo[2].type, (int)FIGHTING); EQ((int)t.moveInfo[3].pp, 10);
		EQ((int)t.moveInfo[200].pp, 5 + 200 % 35);
		// icons, their palettes, genders, experience
		EQ(t.icons.size(), (size_t)413);
		EQ(t.iconPal.size(), (size_t)413);
		EQ(t.palettes.size(), (size_t)3);
		if (t.icons.size() == 413)
		{
			EQ((int)t.icons[25][0], 0);						// a corner: clear
			EQ((int)t.icons[25][18 * 32 + 15], 2 + 25 % 6);	// the body
			EQ((int)t.icons[25][16 * 32 + 12], 1);			// an eye
			EQ((int)t.icons[412][17 * 32 + 15] != 0, true);	// the egg
			CHECK(t.icons[0] == t.icons[1]);
		}
		std::vector<uint8_t> rgba;
		CHECK(t.iconRGBA(25, false, rgba) && rgba.size() == 32 * 32 * 4);
		if (rgba.size() == 4096)
		{
			EQ((int)rgba[3], 0);										// colour 0 clear
			int i = (16 * 32 + 12) * 4;								// colour 1: 0x0c63 -> 3, 3, 3 (5 bits) -> 24
			EQ((int)rgba[i], 24); EQ((int)rgba[i + 1], 24); EQ((int)rgba[i + 2], 24); EQ((int)rgba[i + 3], 255);
		}
		CHECK(t.iconRGBA(7, true, rgba));		// the egg's
		CHECK(!t.iconRGBA(500, false, rgba));
		EQ(t.gender(4, 0), 2);					// ratio 255: neither
		EQ(t.gender(5, 30), 1); EQ(t.gender(5, 31), 0);		// 31: female below 31
		EQ(t.gender(7, 0x12345600), 1);			// 254: always female
		EQ(t.gender(500, 0), 2);
		EQ(t.expFor(6, 10), 1000LL);			// growth 0: n^3
		EQ(t.expFor(7, 10), 1000LL * 5 / 5);	// growth 1: n^3 (4+1)/5
		EQ(t.expFor(8, 10), 1000LL * 6 / 5);
		EQ(t.expFor(8, 1), 1LL);
		EQ(t.expFor(500, 10), -1LL);
		// nothing like them: nothing found (and no crash on a short ROM)
		std::vector<uint8_t> none(1 << 16, 0x11);
		RomTables n = findTables(none.data(), none.size());
		CHECK(n.species.empty() && n.moves.empty() && n.speciesTypes.empty() && n.moveInfo.empty());
		RomTables tiny = findTables(rom.data() + 0x100003, 64);
		CHECK(tiny.species.empty());
	}

	// ---- RetroArch's replies
	{
		uint32_t a; std::vector<uint8_t> v;
		CHECK(parseReply("READ_CORE_MEMORY 20244ec 01 FF 0a", a, v) && a == 0x20244ec && v.size() == 3 && v[1] == 0xff && v[2] == 0x0a);
		CHECK(parseReply("READ_CORE_MEMORY 2024284 -1 no memory map defined", a, v) && a == 0x2024284 && v.empty());
		CHECK(!parseReply("GET_STATUS PLAYING", a, v));
		CHECK(!parseReply("[libretro INFO] READ_CORE_MEMORY 1 2", a, v));
	}

	// ---- the companion's round: requests, then replies in pieces among other output
	{
		std::vector<uint8_t> h(0xc0, 0);
		memcpy(&h[0xac], "BPEE", 4);
		const GameProfile* g = detectGame(h.data(), h.size());
		Companion c;
		c.openWith(g);
		RomTables t;
		for (int i = 0; i < 412; i++) { t.species.push_back("S" + std::to_string(i)); t.speciesTypes.push_back({ (uint8_t)WATER, (uint8_t)GROUND }); }
		c.setTables(t);
		std::string r = c.requests(0);
		CHECK(r.find("READ_CORE_MEMORY 30026f9 1\n") != std::string::npos);
		CHECK(r.find("READ_CORE_MEMORY 20244e9 1\n") != std::string::npos);
		CHECK(r.find("READ_CORE_MEMORY 20244ec 600\n") != std::string::npos);
		CHECK(r.find(" 4\n") == std::string::npos);			// no battle read out of a battle
		EQ(c.requests(100), std::string(""));				// (still waiting)
		std::vector<uint8_t> party;
		Plain a{ 0x11111111, 0x22222222, "MUDKIP", 283, 0, { 33, 45, 55, 0 }, { 35, 40, 25, 0 }, 0, 0, 12, 30, 35, false };
		Plain b{ 0x33333335, 0x22222222, "ZIGZAG", 288, 0, { 33, 0, 0, 0 }, { 35, 0, 0, 0 }, 0, 0x08, 7, 0, 22, false };
		for (auto& p : { partySlot(a), partySlot(b) }) party.insert(party.end(), p.begin(), p.end());
		party.resize(600, 0);
		std::string out = "[INFO] something else on stdout\n" + reply(0x30026f9, { 0x00 }) + reply(0x20244e9, { 2 }) + reply(0x20244ec, party);
		c.feed(out.data(), 50);
		CHECK(!c.snapshot().seen);
		c.feed(out.data() + 50, out.size() - 50);
		CHECK(c.takeChanged());
		const Snapshot& s = c.snapshot();
		CHECK(s.seen && !s.inBattle);
		EQ(s.party.size(), (size_t)2);
		if (s.party.size() == 2)
		{
			EQ(s.party[0].nick, std::string("MUDKIP")); EQ(s.party[0].level, (uint8_t)12); EQ((int)s.party[0].types[0], (int)WATER);
			EQ(s.party[1].hp, (uint16_t)0); EQ(std::string(statusName(s.party[1].status)), std::string("PSN"));
		}
		EQ(c.requests(200), std::string(""));				// (the next set at 250 ms)
		// a battle starts: the next set reads it as well
		r = c.requests(260);
		CHECK(r.find("READ_CORE_MEMORY 20244ec 600\n") != std::string::npos);
		out = reply(0x30026f9, { 0x02 }) + reply(0x20244e9, { 2 }) + reply(0x20244ec, party);
		c.feed(out.data(), out.size());
		CHECK(c.takeChanged() && c.snapshot().inBattle && c.snapshot().foes.empty());
		r = c.requests(520);
		CHECK(r.find("READ_CORE_MEMORY 2022fec 4\n") != std::string::npos && r.find("READ_CORE_MEMORY 2024084 352\n") != std::string::npos &&
			r.find("READ_CORE_MEMORY 2024744 600\n") != std::string::npos);
		std::vector<uint8_t> mons;
		for (auto& m : { battler(283, "MUDKIP", 12, 30, 35, WATER, WATER, { 33, 45, 55 }), battler(286, "POOCHYENA", 6, 18, 20, DARK, DARK, { 33 }),
			std::vector<uint8_t>(0x58, 0), std::vector<uint8_t>(0x58, 0) })
			mons.insert(mons.end(), m.begin(), m.end());
		std::vector<uint8_t> foeParty = partySlot(Plain{ 0x99999999, 0x1234, "POOCHYENA", 286, 0, { 33, 44, 0, 0 }, { 35, 25, 0, 0 }, 0, 0, 13, 17, 36, false });
		foeParty.resize(600, 0);
		out = reply(0x30026f9, { 0x02 }) + reply(0x20244e9, { 2 }) + reply(0x20244ec, party) + reply(0x2022fec, { 0x04, 0, 0, 0 }) + reply(0x2024084, mons) +
			reply(0x2024744, foeParty);
		c.feed(out.data(), out.size());
		CHECK(c.takeChanged());
		const Snapshot& s2 = c.snapshot();
		CHECK(s2.inBattle && !s2.doubles);
		EQ(s2.foes.size(), (size_t)1);
		if (!s2.foes.empty()) { EQ(s2.foes[0].nick, std::string("POOCHYENA")); EQ((int)s2.foes[0].types[0], (int)DARK); }
		EQ(s2.lead.species, (uint16_t)283);
		EQ(s2.foeParty.size(), (size_t)1);
		if (!s2.foeParty.empty()) { EQ(s2.foeParty[0].pid, 0x99999999u); EQ(s2.foeParty[0].level, (uint8_t)13); }
		// the same again: nothing changed
		c.requests(800);
		c.feed(out.data(), out.size());
		CHECK(!c.takeChanged());
		// an answer with no data (no content, no memory map): an error, not a party
		c.requests(1100);
		out = "READ_CORE_MEMORY 30026f9 -1 no memory map defined\nREAD_CORE_MEMORY 20244e9 -1 no memory map defined\nREAD_CORE_MEMORY 20244ec -1 no memory map defined\n"
			"READ_CORE_MEMORY 2022fec -1 no memory map defined\nREAD_CORE_MEMORY 2024084 -1 no memory map defined\n"
			"READ_CORE_MEMORY 2024744 -1 no memory map defined\n";
		c.feed(out.data(), out.size());
		CHECK(!c.snapshot().seen && c.snapshot().error.empty());	// (not said at once: RetroArch may still be loading the game)
		for (double t = 1400; t <= 6200; t += 300) { c.requests(t); c.feed(out.data(), out.size()); }
		CHECK(!c.snapshot().seen && c.snapshot().error == "no memory map defined");	// (5 s of them)
		// no answer at all (RetroArch busy): nothing more asked until 10 s have gone
		CHECK(!c.requests(7000).empty());
		EQ(c.requests(7300), std::string(""));
		EQ(c.requests(16900), std::string(""));
		CHECK(!c.requests(17100).empty());
	}

	// ---- a made-up Emerald for the panel's headless test (tests/run.sh): a ROM with the header and the four tables
	// (a few names of the game, made-up ones for the rest), and RetroArch's replies out of a battle and in one
	{
		std::string dir = argc > 1 ? argv[1] : "";
		std::vector<uint8_t> rom(2u << 20, 0);
		memcpy(&rom[0xa0], "POKEMON EMER", 12);
		memcpy(&rom[0xac], "BPEE", 4);
		auto names = [&](size_t at, size_t entry, size_t count, const std::vector<uint8_t>& first, auto nameOf)
		{
			for (size_t i = 0; i < count; i++)
			{
				uint8_t* e = &rom[at + i * entry];
				std::vector<uint8_t> n = i == 0 ? first : enc(nameOf(i));
				memcpy(e, n.data(), n.size());
				if (n.size() < entry && i > 0) e[n.size()] = 0xff;
			}
		};
		std::vector<uint8_t> q(11, 0xac); q[10] = 0xff;
		names(0x10000, 11, 412, q, [](size_t i) -> std::string
		{
			return i == 283 ? "MUDKIP" : i == 286 ? "POOCHYENA" : i == 288 ? "ZIGZAGOON" : i == 309 ? "WINGULL" : i == 280 ? "TORCHIC" : "MON" + std::to_string(i);
		});
		names(0x20000, 13, 355, { 0xae, 0xff }, [](size_t i) -> std::string
		{
			return i == 33 ? "TACKLE" : i == 45 ? "GROWL" : i == 55 ? "WATER GUN" : i == 189 ? "MUD-SLAP" : i == 44 ? "BITE" : i == 10 ? "SCRATCH" :
				i == 52 ? "EMBER" : i == 98 ? "QUICK ATTACK" : "MOVE" + std::to_string(i);
		});
		size_t st = 0x30000;
		uint8_t bulba[8] = { 45, 49, 49, 45, 65, 65, GRASS, POISON }, ivy[8] = { 60, 62, 63, 60, 80, 80, GRASS, POISON };
		memcpy(&rom[st + 28], bulba, 8); memcpy(&rom[st + 56], ivy, 8);
		auto types = [&](int sp, int a, int b) { rom[st + sp * 28 + 6] = (uint8_t)a; rom[st + sp * 28 + 7] = (uint8_t)b; };
		types(283, WATER, WATER); types(286, DARK, DARK); types(288, NORMAL, NORMAL); types(309, WATER, FLYING); types(280, FIRE, FIRE);
		size_t mv = 0x40000;
		auto move = [&](int i, int power, int type, int acc, int pp) { uint8_t* e = &rom[mv + i * 12]; e[1] = (uint8_t)power; e[2] = (uint8_t)type; e[3] = (uint8_t)acc; e[4] = (uint8_t)pp; };
		move(1, 40, NORMAL, 100, 35); move(2, 50, FIGHTING, 100, 25); move(3, 15, NORMAL, 85, 10);
		move(33, 35, NORMAL, 95, 35); move(45, 0, NORMAL, 100, 40); move(55, 40, WATER, 100, 25); move(189, 20, GROUND, 100, 10);
		move(44, 60, DARK, 100, 25); move(10, 40, NORMAL, 100, 35); move(52, 40, FIRE, 100, 25); move(98, 40, NORMAL, 100, 30);
		names(0x48000, 13, 78, { 0xae, 0xae, 0xae, 0xae, 0xae, 0xae, 0xae, 0xff }, [](size_t i) -> std::string
		{
			return i == 67 ? "TORRENT" : i == 53 ? "PICKUP" : i == 51 ? "KEEN EYE" : i == 44 ? "RAIN DISH" : i == 66 ? "BLAZE" : "ABILITY" + std::to_string(i);
		});
		auto abilities = [&](int sp, int a, int b) { rom[st + sp * 28 + 0x16] = (uint8_t)a; rom[st + sp * 28 + 0x17] = (uint8_t)b; };
		abilities(283, 67, 0); abilities(288, 53, 0); abilities(309, 51, 44); abilities(280, 66, 0);
		for (int i = 0; i < 20; i++)
		{
			uint8_t* e = &rom[0x49000 + i * 44];
			std::vector<uint8_t> n = i == 0 ? std::vector<uint8_t>(8, 0xac) : enc(i == 13 ? "POTION" : "ITEM" + std::to_string(i));
			memcpy(e, n.data(), n.size()); e[n.size()] = 0xff;
			put16(e + 14, (uint16_t)i);
		}
		addExtras(rom, st, 0x50000);
		Plain a{ 0x11111111, 0x22222222, "MUDKIP", 283, 13, { 33, 45, 55, 189 }, { 35, 40, 21, 10 }, 0x30, 0, 14, 38, 41, false };
		Plain b{ 0x33333335, 0x22222222, "Ziggy", 288, 0, { 33, 45, 0, 0 }, { 35, 40, 0, 0 }, 0, 0x08, 9, 11, 30, false };
		Plain c2{ 0x55555557, 0x22222222, "WINGULL", 309, 0, { 45, 55, 98, 0 }, { 40, 25, 30, 0 }, 0, 0, 11, 0, 29, false };
		Plain d{ 0x77777779, 0x22222222, "TORCHIC", 280, 0, { 10, 45, 52, 0 }, { 35, 40, 25, 0 }, 0, 0x02, 8, 24, 24, false };
		std::vector<uint8_t> party;
		for (auto& p : { partySlot(a), partySlot(b), partySlot(c2), partySlot(d) }) party.insert(party.end(), p.begin(), p.end());
		party.resize(600, 0);
		std::vector<uint8_t> mons;
		for (auto& m : { battler(283, "MUDKIP", 14, 38, 41, WATER, WATER, { 33, 45, 55, 189 }), battler(286, "POOCHYENA", 13, 17, 36, DARK, DARK, { 33, 44 }),
			std::vector<uint8_t>(0x58, 0), std::vector<uint8_t>(0x58, 0) })
			mons.insert(mons.end(), m.begin(), m.end());
		std::string outside = reply(0x30026f9, { 0x00 }) + reply(0x20244e9, { 4 }) + reply(0x20244ec, party);
		// a trainer's three: the one out, one fainted, one to come
		std::vector<uint8_t> foeParty;
		for (auto& p : { partySlot(Plain{ 0x0bad5eed, 0x4321, "POOCHYENA", 286, 0, { 33, 44, 0, 0 }, { 35, 25, 0, 0 }, 0, 0, 13, 17, 36, false }),
			partySlot(Plain{ 0x0bad5eef, 0x4321, "WINGULL", 309, 0, { 45, 55, 0, 0 }, { 40, 25, 0, 0 }, 0, 0, 12, 0, 31, false }),
			partySlot(Plain{ 0x0bad5ef1, 0x4321, "TORCHIC", 280, 0, { 10, 52, 0, 0 }, { 35, 25, 0, 0 }, 0, 0, 12, 30, 30, false }) })
			foeParty.insert(foeParty.end(), p.begin(), p.end());
		foeParty.resize(600, 0);
		std::string inside = reply(0x30026f9, { 0x02 }) + reply(0x20244e9, { 4 }) + reply(0x20244ec, party) + reply(0x2022fec, { 0x0c, 0, 0, 0 }) + reply(0x2024084, mons) +
			reply(0x2024744, foeParty);
		std::string scratch = dir.empty() ? "/tmp" : dir;
		std::string romPath = scratch + "/poke-test-" + std::to_string(getpid()) + ".gba";
		if (!dir.empty()) romPath = dir + "/poke.gba";
		FILE* f = fopen(romPath.c_str(), "wb");
		CHECK(f != nullptr);
		if (f) { fwrite(rom.data(), 1, rom.size(), f); fclose(f); }
		if (!dir.empty())
		{
			if ((f = fopen((dir + "/poke-party.txt").c_str(), "wb"))) { fputs(outside.c_str(), f); fclose(f); }
			if ((f = fopen((dir + "/poke-battle.txt").c_str(), "wb"))) { fputs(inside.c_str(), f); fclose(f); }
		}
		// the companion on the file: the game from its header, the tables read on the thread
		Companion cc;
		CHECK(cc.open(romPath));
		CHECK(cc.game() && std::string(cc.game()->code) == "BPEE");
		std::shared_ptr<const RomTables> tt;
		for (int i = 0; i < 200 && !(tt = cc.tables()); i++) usleep(10000);
		CHECK(tt != nullptr);
		if (tt)
		{
			EQ(tt->speciesName(283), std::string("MUDKIP"));
			EQ(tt->moveName(55), std::string("WATER GUN"));
			EQ((int)tt->speciesTypes[309][1], (int)FLYING);
			EQ((int)tt->moveInfo[189].type, (int)GROUND);
			EQ(tt->icons.size(), (size_t)413);
			EQ(tt->palettes.size(), (size_t)3);
			EQ(tt->abilities.size(), (size_t)78);
			EQ(tt->itemName(13), std::string("POTION"));
			Mon mk; mk.species = 283; mk.abilitySlot = 1;
			EQ(tt->abilityName(mk), std::string("TORRENT"));	// (one ability: the second slot is the first)
			mk.species = 309;
			EQ(tt->abilityName(mk), std::string("RAIN DISH"));
			mk.abilitySlot = 0;
			EQ(tt->abilityName(mk), std::string("KEEN EYE"));
			mk.species = 411;
			EQ(tt->abilityName(mk), std::string(""));			// (none in the table)
			if (!dir.empty()) CHECK(iconsPng(*tt, { { 283, false }, { 288, false }, { 309, false }, { 280, false }, { 286, false }, { 0, true } }, dir + "/poke-icons.png"));
		}
		// another game's header: nothing to do
		rom[0xac] = 'X';
		if ((f = fopen(romPath.c_str(), "wb"))) { fwrite(rom.data(), 1, 0xc0, f); fclose(f); }
		Companion other;
		CHECK(!other.open(romPath) && !other.active());
		if (dir.empty()) unlink(romPath.c_str());
		else if ((f = fopen(romPath.c_str(), "wb"))) { rom[0xac] = 'B'; fwrite(rom.data(), 1, rom.size(), f); fclose(f); }
		CHECK(!other.open("/nonexistent/x.gba"));
	}

	// ---- a CFRU hack told by its byte replacements: the Pokémon not encrypted, the Pokédex flags at a fixed address;
	// the companion reads them (seen, then caught) while the Dex page is open
	{
		std::vector<uint8_t> rom(0x110000, 0);
		memcpy(&rom[0xac], "BPRE", 4);
		const uint8_t order0[] = { 0x00, 0x20, 0xc0, 0x46, 0xc0, 0x46 };
		memcpy(&rom[0x3f94c], order0, 6);
		rom[0x3f906] = rom[0x3f92a] = 0xc0; rom[0x3f907] = rom[0x3f92b] = 0x46;
		RomTables plainOnly = findTables(rom.data(), rom.size());
		CHECK(plainOnly.has.plainMons && plainOnly.has.dexRam == 0);
		for (int a : { 0x104b00, 0x104b02 }) { rom[a] = 0xc0; rom[a + 1] = 0x46; }
		rom[0x549b6] = 0x7d; rom[0x549b7] = 0x22; rom[0x549be] = 0x7d; rom[0x549bf] = 0x30;
		put32(&rom[0x104b10], 0x0202583c); put32(&rom[0x549d0], 0x0202583c);
		CHECK(!findTables(rom.data(), rom.size()).has.bagPockets);
		rom[0x99dfc] = 0x80; rom[0x99dfd] = 0x88; rom[0x98984] = 0xc0; rom[0x98985] = 0x46;
		RomTables t = findTables(rom.data(), rom.size());
		CHECK(t.has.plainMons && t.has.dexRam == 0x0202583c && t.has.dexBytes == 0x7d && t.has.bagPockets == 0x0203988c);
		memcpy(&rom[0xac], "BPEE", 4);
		RomTables em = findTables(rom.data(), rom.size());
		CHECK(!em.has.plainMons && em.has.dexRam == 0);		// (only FireRed's offsets)

		Companion c;
		std::vector<uint8_t> h(0xc0, 0); memcpy(&h[0xac], "BPRE", 4);
		c.openWith(detectGame(h.data(), h.size()));
		c.setTables(t);
		c.wantExt(true);
		std::string r = c.requests(1000);
		CHECK(r.find("READ_CORE_MEMORY 202583c 250\n") != std::string::npos);
		CHECK(r.find("READ_CORE_MEMORY 300500c") == std::string::npos);		// (no SaveBlock2 Pokédex)
		std::vector<uint8_t> dex(250, 0);
		dex[0] = 0x07; dex[0x7d] = 0x01; dex[100] = 0x80;	// seen 1-3 and 808, caught 1
		Plain m{ 0x1234567u, 0x89abcdefu, "Kid", 906, 0, { 33, 0, 0, 0 }, { 35, 0, 0, 0 }, 0, 0, 5, 20, 20, false };
		std::string feed;
		std::vector<uint8_t> pt = partySlot(m, false, true); pt.resize(600, 0);
		std::vector<uint8_t> main(1, 0), cnt(1, 1), sbp(8, 0), mh(0x1c, 0), bp(40, 0);
		{ uint32_t pk[5][2] = { { 0x0203bb20, 450 }, { 0x0203c228, 75 }, { 0x0203c354, 50 }, { 0x0203c41c, 128 }, { 0x0203c61c, 75 } };
		  for (int k = 0; k < 5; k++) { put32(&bp[8 * k], pk[k][0]); put32(&bp[8 * k + 4], pk[k][1]); } }
		put32(&sbp[0], 0x0202552c); put32(&sbp[4], 0x02024588);
		auto all = [&](const std::string& req)
		{
			std::string f;
			size_t at = 0;
			while ((at = req.find("READ_CORE_MEMORY ", at)) != std::string::npos)
			{
				unsigned a = 0; int n = 0;
				sscanf(req.c_str() + at, "READ_CORE_MEMORY %x %d", &a, &n);
				at += 17;
				std::vector<uint8_t> v((size_t)n, 0);
				if (a == 0x02024284) v = pt;
				else if (a == 0x02024029) v = cnt;
				else if (a == 0x03005008) v = sbp;
				else if (a == 0x0202583c) v = dex;
				else if (a == 0x0203988c) v = bp;
				else if (a == 0x0203bb20) { v[0] = 13; v[2] = 2; v[4] = 14; v[6] = 1; }	// 2 Potions, an Antidote
				else if (a == 0x0203c354) { v[0] = 4; v[2] = 5; }					// 5 Poké Balls
				f += reply(a, v);
			}
			return f;
		};
		feed = all(r);
		c.feed(feed.data(), feed.size());
		const Snapshot& s = c.snapshot();
		CHECK(s.seen && s.party.size() == 1 && s.party[0].species == 906 && s.party[0].nick == "Kid");
		r = c.requests(2000);
		feed = all(r);
		c.feed(feed.data(), feed.size());
		const Snapshot& s2 = c.snapshot();
		CHECK(s2.ext && s2.dexSeen.size() == 0x7d && s2.dexOwned.size() == 0x7d);
		CHECK(s2.dexSeen.size() == 0x7d && s2.dexSeen[0] == 0x07 && s2.dexSeen[100] == 0x80 && s2.dexOwned[0] == 0x01);
		CHECK(r.find("READ_CORE_MEMORY 202583c 250\n") != std::string::npos && r.find("READ_CORE_MEMORY 2025") != std::string::npos);
		// the bag: gBagPockets read in the second set, the pockets where it points in the third (not FireRed's own
		// bag in the save block, where a CFRU hack keeps the Pokédex)
		CHECK(r.find("READ_CORE_MEMORY 203988c 40\n") != std::string::npos);
		r = c.requests(3000);
		CHECK(r.find("READ_CORE_MEMORY 203bb20 1800\n") != std::string::npos && r.find("READ_CORE_MEMORY 203c354 200\n") != std::string::npos);
		CHECK(r.find("READ_CORE_MEMORY 202583c 250\n") != std::string::npos);
		CHECK(r.find(" 202583c 516") == std::string::npos);		// (no read of FireRed's bag over the Pokédex flags)
		feed = all(r);
		c.feed(feed.data(), feed.size());
		const Snapshot& s3 = c.snapshot();
		CHECK(s3.bag && s3.pockets[0].size() == 2 && s3.pockets[0][0].item == 13 && s3.pockets[0][0].count == 2 && s3.pockets[0][1].item == 14);
		CHECK(s3.pockets[2].size() == 1 && s3.pockets[2][0].count == 5 && s3.pockets[1].empty());
	}

	// ---- Pokémon Unbound itself, when UNBOUND_ROM names it (never in the repository)
	if (const char* up = getenv("UNBOUND_ROM"))
	{
		Companion cc;
		CHECK(cc.open(up) && std::string(cc.game()->code) == "BPRE");
		std::shared_ptr<const RomTables> tt;
		for (int i = 0; i < 3000 && !(tt = cc.tables()); i++) usleep(10000);
		CHECK(tt && tt->has.modernTypes && tt->has.ownDex && tt->has.leadersFromTrainers && tt->has.plainMons && tt->has.dexRam == 0x0202583c &&
			tt->has.dexBytes == 0x7d && tt->has.bagPockets == 0x0203988c && !tt->has.bagUnsure);
	}

	// ---- the region map pictures (pokemap.cpp): the LZ77 decoder, then made-up Hoenn and Kanto maps found and drawn
	{
		// a block of literals, then a back-reference copying 5 bytes from 3 back (overlapping: a run), then literals
		std::vector<uint8_t> rom = { 0x10, 12, 0, 0, 0x08, 'a', 'b', 'c', 'd', 0x20, 0x02, 'x', 'y', 'z' };
		std::vector<uint8_t> out;
		size_t used = 0;
		CHECK(lz77(rom.data(), rom.size(), 0, out, 64, &used));
		EQ(std::string(out.begin(), out.end()), std::string("abcdbcdbcxyz"));
		EQ(used, rom.size());
		CHECK(!lz77(rom.data(), rom.size(), 0, out, 8));			// (more than asked for)
		CHECK(!lz77(rom.data(), rom.size() - 2, 0, out, 64));		// (cut short)
		std::vector<uint8_t> far = { 0x10, 6, 0, 0, 0x40, 'a', 0x00, 0x05 };	// (a reference before the start)
		CHECK(!lz77(far.data(), far.size(), 0, out, 64));
		rom[0] = 0x11;
		CHECK(!lz77(rom.data(), rom.size(), 0, out, 64));			// (not type 0x10)
	}
	{
		// literals only, 8 a block
		auto lzLit = [](const std::vector<uint8_t>& d)
		{
			std::vector<uint8_t> o = { 0x10, (uint8_t)d.size(), (uint8_t)(d.size() >> 8), (uint8_t)(d.size() >> 16) };
			for (size_t i = 0; i < d.size(); i += 8) { o.push_back(0); for (size_t k = i; k < i + 8 && k < d.size(); k++) o.push_back(d[k]); }
			while (o.size() % 4) o.push_back(0);
			return o;
		};
		// Hoenn: a 32-colour palette, 100 tiles of 8 bpp (tile k: pixels 0x70 + k % 32), a 64 x 64 tilemap (tile x + y)
		std::vector<uint8_t> rom(0x200, 0);
		memcpy(&rom[0xac], "AXVE", 4);
		size_t pal = rom.size();
		for (int i = 0; i < 32; i++) { uint16_t c = (uint16_t)(i | (31 - i) << 10); rom.push_back((uint8_t)c); rom.push_back((uint8_t)(c >> 8)); }
		std::vector<uint8_t> tiles(100 * 64);
		for (int k = 0; k < 100; k++) for (int i = 0; i < 64; i++) tiles[k * 64 + i] = (uint8_t)(i == 0 ? 0 : 0x70 + k % 32);
		auto tl = lzLit(tiles);
		rom.insert(rom.end(), tl.begin(), tl.end());
		std::vector<uint8_t> map(0x1000);
		for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) map[y * 64 + x] = (uint8_t)((x + y) % 100);
		auto ml = lzLit(map);
		rom.insert(rom.end(), ml.begin(), ml.end());
		(void)pal;
		RomTables t;
		RomTables::Grid g; g.w = 28; g.h = 15; g.cells.assign(28 * 15, 0);
		t.grids.push_back(g);
		findRegionMaps(t, rom.data(), rom.size(), 0);
		CHECK(t.mapImages.size() == 1 && t.gridImage.size() == 1 && t.gridImage[0] == 0);
		if (t.mapImages.size() == 1)
		{
			const auto& im = t.mapImages[0];
			EQ(im.w, 240); EQ(im.h, 136); EQ(im.cellX, 8); EQ(im.cellY, 8);
			// the picture's pixel 1,1 of screen tile 3,2: tile (3 + 2) = 5, colour 5: red 5 -> 41, blue 26 -> 214
			const uint8_t* px = &im.rgba[((size_t)((2 - 1) * 8 + 1) * im.w + 3 * 8 + 1) * 4];
			EQ((int)px[0], (5 << 3) | (5 >> 2)); EQ((int)px[2], (26 << 3) | (26 >> 2)); EQ((int)px[3], 255);
			// pixel 0 of a tile: the backdrop, colour 0 (blue 31)
			const uint8_t* bd = &im.rgba[((size_t)((2 - 1) * 8) * im.w + 3 * 8) * 4];
			EQ((int)bd[0], 0); EQ((int)bd[2], 255);
		}
		// a tilemap naming a tile the tiles don't have: not taken (the squares stay)
		for (int i = 0; i < 4; i++) map[i] = 200;
		std::vector<uint8_t> bad(rom.begin(), rom.begin() + (rom.size() - ml.size()));
		auto ml2 = lzLit(map);
		bad.insert(bad.end(), ml2.begin(), ml2.end());
		RomTables t2; t2.grids.push_back(g);
		findRegionMaps(t2, bad.data(), bad.size(), 0);
		CHECK(t2.mapImages.empty() && t2.gridImage[0] == -1);
	}
	{
		// Kanto: the literal pool (palette, top bar palette, tiles, four tilemaps) and what it points to
		auto lzLit = [](const std::vector<uint8_t>& d)
		{
			std::vector<uint8_t> o = { 0x10, (uint8_t)d.size(), (uint8_t)(d.size() >> 8), (uint8_t)(d.size() >> 16) };
			for (size_t i = 0; i < d.size(); i += 8) { o.push_back(0); for (size_t k = i; k < i + 8 && k < d.size(); k++) o.push_back(d[k]); }
			while (o.size() % 4) o.push_back(0);
			return o;
		};
		std::vector<uint8_t> rom(0x400, 0);
		memcpy(&rom[0xac], "BPRE", 4);
		rom[0xbc] = 7;		// (a revision with no known place: found by its pool)
		auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; i++) rom[at + i] = (uint8_t)(v >> (8 * i)); };
		size_t pal = rom.size();
		for (int i = 0; i < 80; i++) { uint16_t c = (uint16_t)(i % 32); rom.push_back((uint8_t)c); rom.push_back((uint8_t)(c >> 8)); }
		size_t tiles = rom.size();
		std::vector<uint8_t> tl(64 * 32);
		for (int k = 0; k < 64; k++) for (int i = 0; i < 32; i++) tl[k * 32 + i] = (uint8_t)(k % 16 | (k % 16) << 4);
		auto tz = lzLit(tl); rom.insert(rom.end(), tz.begin(), tz.end());
		size_t maps[4];
		for (int m = 0; m < 4; m++)
		{
			std::vector<uint8_t> mp(30 * 20 * 2);
			for (int i = 0; i < 600; i++) { uint16_t e = (uint16_t)((i + m) % 64 | (m % 5) << 12); mp[i * 2] = (uint8_t)e; mp[i * 2 + 1] = (uint8_t)(e >> 8); }
			maps[m] = rom.size();
			auto mz = lzLit(mp); rom.insert(rom.end(), mz.begin(), mz.end());
		}
		size_t pool = 0x300;
		put32(pool, 0x08000000 + (uint32_t)pal); put32(pool + 8, 0x08000000 + (uint32_t)pal); put32(pool + 0x40, 0x08000000 + (uint32_t)tiles);
		for (int m = 0; m < 4; m++) put32(pool + 0x50 + m * 0x10, 0x08000000 + (uint32_t)maps[m]);
		RomTables t;
		RomTables::Grid g; g.w = 22; g.h = 15; g.cells.assign(22 * 15, 0x58);
		for (int i = 0; i < 8; i++) t.grids.push_back(g);
		findRegionMaps(t, rom.data(), rom.size(), 2);
		CHECK(t.mapImages.size() == 4 && t.gridImage.size() == 8 && t.gridImage[0] == 0 && t.gridImage[3] == 1 && t.gridImage[7] == 3);
		if (t.mapImages.size() == 4)
		{
			const auto& im = t.mapImages[1];
			EQ(im.w, 192); EQ(im.h, 136); EQ(im.cellX, 8); EQ(im.cellY, 8);
			// picture 1's pixel 2,0 of its tile 0,0: the tilemap's tile 3,3 = entry (3 * 30 + 3 + 1) % 64 = 30, palette 1:
			// colour index 30 % 16 = 14 -> palette entry 16 + 14 = 30 -> red 30
			EQ((int)im.rgba[(2) * 4], (30 << 3) | (30 >> 2));
		}
	}

	// ---- a region map picture by hand (POKE_MAPDUMP=<a .gba>, never in the repository): each picture to a PNG, the
	// sections of the grids on it outlined (for a look: does the grid line up with the map?), and a checksum of each
	if (const char* mp = getenv("POKE_MAPDUMP"))
	{
		FILE* f = fopen(mp, "rb");
		std::vector<uint8_t> rom(32u << 20);
		rom.resize(f ? fread(rom.data(), 1, rom.size(), f) : 0);
		if (f) fclose(f);
		RomTables t = findTables(rom.data(), rom.size());
		printf("pokemon-test: %s: %zu grids, %zu map pictures\n", mp, t.grids.size(), t.mapImages.size());
		for (size_t i = 0; i < t.mapImages.size(); i++)
		{
			auto im = t.mapImages[i];
			uint32_t sum = 2166136261u;
			for (uint8_t b : im.rgba) sum = (sum ^ b) * 16777619u;
			for (size_t g = 0; g < t.grids.size(); g++)
			{
				if (t.gridImage[g] != (int)i || (getenv("POKE_MAPGRID") && atoi(getenv("POKE_MAPGRID")) != (int)g)) continue;
				const auto& gr = t.grids[g];
				for (int y = 0; y < gr.h; y++)
					for (int x = 0; x < gr.w; x++)
					{
						int v = gr.cells[y * gr.w + x];
						if (v == t.mapsecNone || v == 0xff) continue;
						for (int k = 0; k < 8; k++)
							for (int e = 0; e < 4; e++)
							{
								int nx = x + (e == 0) - (e == 1), ny = y + (e == 2) - (e == 3);
								bool edge = nx < 0 || ny < 0 || nx >= gr.w || ny >= gr.h || gr.cells[ny * gr.w + nx] != v;
								if (!edge) continue;
								int px = im.cellX + x * 8 + (e == 0 ? 7 : e == 1 ? 0 : k), py = im.cellY + y * 8 + (e == 2 ? 7 : e == 3 ? 0 : k);
								if (px < 0 || py < 0 || px >= im.w || py >= im.h) continue;
								uint8_t* d = &im.rgba[((size_t)py * im.w + px) * 4];
								d[0] = 255; d[1] = 0; d[2] = 255;
							}
					}
			}
			std::string out = (argc > 1 ? std::string(argv[1]) : std::string("/tmp")) + "/map-" + std::to_string(i) + ".png";
			std::vector<uint8_t> rgb;
			for (size_t p = 0; p < im.rgba.size(); p += 4) rgb.insert(rgb.end(), { im.rgba[p], im.rgba[p + 1], im.rgba[p + 2] });
			stbi_write_png(out.c_str(), im.w, im.h, 3, rgb.data(), im.w * 3);
			printf("pokemon-test: map %zu %dx%d cell origin %d,%d fnv %08x -> %s\n", i, im.w, im.h, im.cellX, im.cellY, sum, out.c_str());
		}
		for (size_t g = 0; g < t.grids.size(); g++)
		{
			std::string secs;
			for (uint8_t v : t.grids[g].cells) if (v != t.mapsecNone && v != 0xff && secs.find(t.mapName(v)) == std::string::npos && secs.size() < 160) { char hx[8]; snprintf(hx, sizeof hx, "=%x,", v); secs += t.mapName(v) + hx; }
			printf("  grid %zu %dx%d -> picture %d: %s\n", g, t.grids[g].w, t.grids[g].h, t.gridImage[g], secs.c_str());
		}
	}

	// ---- the player's own Emerald, when POKE_ROM names it (a .gba or a .zip; never in the repository): Torchic's icon
	// as the game keeps it (pret's pokeemerald.sym: gMonIcon_Torchic at 0x08ce0028, gMonIconPaletteIndices at
	// 0x0857c388), and the icons of a made-up party to a PNG to look at
	if (const char* rp = getenv("POKE_ROM"))
	{
		Companion cc;
		CHECK(cc.open(rp) && std::string(cc.game()->code) == "BPEE");
		std::shared_ptr<const RomTables> tt;
		for (int i = 0; i < 1000 && !(tt = cc.tables()); i++) usleep(10000);
		CHECK(tt != nullptr);
		if (tt)
		{
			EQ(tt->speciesName(280), std::string("TORCHIC"));
			EQ(tt->icons.size(), (size_t)413);
			CHECK(tt->palettes.size() >= 3);	// (the header's table: six, the last three unused)
			FILE* f = fopen(rp, "rb");
			std::vector<uint8_t> rom;
			if (f && std::string(rp).find(".zip") == std::string::npos)
			{
				rom.resize(16u << 20);
				rom.resize(fread(rom.data(), 1, rom.size(), f));
			}
			if (f) fclose(f);
			if (rom.size() > 0xce0028 + 1024 && tt->icons.size() == 413)
			{
				CHECK(tt->icons[280] == tilesToPixels(&rom[0xce0028]));
				EQ((int)tt->iconPal[280], (int)rom[0x57c388 + 280]);
			}
			if (tt->icons.size() == 413)
			{
				// a real icon: a shape (some of it clear, a good part not), colours 1-15 on it
				int opaque = 0;
				for (uint8_t v : tt->icons[280]) opaque += v != 0;
				CHECK(opaque > 150 && opaque < 800);
			}
			std::string out = (argc > 1 ? std::string(argv[1]) : std::string("/tmp")) + "/poke-icons-real.png";
			// Treecko, Torchic, Mudkip, Pikachu, Zigzagoon, an egg
			CHECK(iconsPng(*tt, { { 277, false }, { 280, false }, { 283, false }, { 25, false }, { 288, false }, { 0, true } }, out));
			printf("pokemon-test: the real ROM's party icons in %s\n", out.c_str());
		}
	}
	else printf("pokemon-test: no POKE_ROM, the real ROM's checks skipped\n");

	printf("pokemon-test: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
