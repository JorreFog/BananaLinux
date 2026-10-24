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
static std::vector<uint8_t> partySlot(const Plain& m, bool breakSum = false)
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
	e[0] = 4; e[6] = 10;
	put32(x + 4, (31u) | (m.egg ? 1u << 30 : 0));
	uint8_t d[48];
	const char* ord = ORDERS[m.pid % 24];
	for (int pos = 0; pos < 4; pos++)
		memcpy(d + pos * 12, ord[pos] == 'G' ? g : ord[pos] == 'A' ? a : ord[pos] == 'E' ? e : x, 12);
	uint16_t sum = 0;
	for (int i = 0; i < 48; i += 2) sum = (uint16_t)(sum + (d[i] | (d[i + 1] << 8)));
	put16(&p[0x1c], (uint16_t)(sum + (breakSum ? 1 : 0)));
	uint32_t key = m.pid ^ m.otid;
	for (int i = 0; i < 48; i += 4)
	{
		uint32_t v = (d[i] | (d[i + 1] << 8) | (d[i + 2] << 16) | ((uint32_t)d[i + 3] << 24)) ^ key;
		put32(&p[0x20 + i], v);
	}
	put32(&p[0x50], m.status); p[0x54] = m.level; put16(&p[0x56], m.hp); put16(&p[0x58], m.maxHp);
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
	for (int sp = 0; sp < N; sp++)
	{
		// none and Bulbasaur share an icon, the 25 placeholders after Celebi one question mark (their own made-up one)
		int src = sp == 0 ? 1 : (sp >= 252 && sp <= 276) ? 252 : sp;
		put32(&rom[table + sp * 4], 0x08000000u + (uint32_t)(tiles + src * 1024));
		rom[pals + sp] = (uint8_t)(sp == 1 ? 1 : (sp >= 252 && sp <= 276) ? 0 : sp % 3);
	}
	for (int sp = 1; sp < N; sp++)
	{
		uint8_t* g = &rom[tiles + sp * 1024];
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
	EQ(std::string(typeName(DARK)), std::string("Dark"));

	// ---- the game from the header's code
	{
		std::vector<uint8_t> h(0xc0, 0);
		memcpy(&h[0xa0], "POKEMON EMER", 12);
		memcpy(&h[0xac], "BPEE", 4);
		const GameInfo* g = detectGame(h.data(), h.size());
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
	}
	{
		Plain m{ 7, 99, "EGG", 152, 0, { 33, 45, 0, 0 }, { 35, 40, 0, 0 }, 0, 0, 5, 20, 20, true };
		Mon d;
		CHECK(decodeParty(partySlot(m).data(), d) && d.egg);
		CHECK(!decodeParty(partySlot(m, true).data(), d));		// a wrong checksum
		std::vector<uint8_t> empty(100, 0);
		CHECK(!decodeParty(empty.data(), d));					// an empty slot (all zero: its sum fits, species 0)
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
		const GameInfo* g = detectGame(h.data(), h.size());
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
		std::vector<uint8_t> rom(1u << 20, 0);
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

	printf("pokemon-test: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
