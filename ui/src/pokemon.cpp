// banana-ui: the Pokémon companion's reading of the games (pokemon.h)
#include "pokemon.h"
#include "zip.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <zlib.h>

namespace banana::pkmn
{
	namespace
	{
		uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
		uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

		const char* const TYPE_NAMES[NUM_TYPES] = { "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug", "Ghost",
			"Steel", "???", "Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark", "", "", "", "", "", "Fairy" };
		const uint32_t TYPE_COLORS[NUM_TYPES] = { 0xa8a878, 0xc03028, 0xa890f0, 0xa040a0, 0xe0c068, 0xb8a038, 0xa8b820, 0x705898,
			0xb8b8d0, 0x68a090, 0xf08030, 0x6890f0, 0x78c850, 0xf8d030, 0xf85888, 0x98d8d8, 0x7038f8, 0x705848, 0x808080, 0x808080,
			0x808080, 0x808080, 0x808080, 0xe898b8 };

		// the Generation III type chart: attacker, defender, 0 none / 1 half / 2 double; every pair not listed is normal
		const uint8_t CHART[][3] = {
			{ NORMAL, ROCK, 1 }, { NORMAL, STEEL, 1 }, { NORMAL, GHOST, 0 },
			{ FIRE, FIRE, 1 }, { FIRE, WATER, 1 }, { FIRE, GRASS, 2 }, { FIRE, ICE, 2 }, { FIRE, BUG, 2 }, { FIRE, ROCK, 1 },
			{ FIRE, DRAGON, 1 }, { FIRE, STEEL, 2 },
			{ WATER, FIRE, 2 }, { WATER, WATER, 1 }, { WATER, GRASS, 1 }, { WATER, GROUND, 2 }, { WATER, ROCK, 2 }, { WATER, DRAGON, 1 },
			{ ELECTRIC, WATER, 2 }, { ELECTRIC, ELECTRIC, 1 }, { ELECTRIC, GRASS, 1 }, { ELECTRIC, GROUND, 0 }, { ELECTRIC, FLYING, 2 },
			{ ELECTRIC, DRAGON, 1 },
			{ GRASS, FIRE, 1 }, { GRASS, WATER, 2 }, { GRASS, GRASS, 1 }, { GRASS, POISON, 1 }, { GRASS, GROUND, 2 }, { GRASS, FLYING, 1 },
			{ GRASS, BUG, 1 }, { GRASS, ROCK, 2 }, { GRASS, DRAGON, 1 }, { GRASS, STEEL, 1 },
			{ ICE, WATER, 1 }, { ICE, GRASS, 2 }, { ICE, ICE, 1 }, { ICE, GROUND, 2 }, { ICE, FLYING, 2 }, { ICE, DRAGON, 2 }, { ICE, STEEL, 1 },
			{ ICE, FIRE, 1 },
			{ FIGHTING, NORMAL, 2 }, { FIGHTING, ICE, 2 }, { FIGHTING, POISON, 1 }, { FIGHTING, FLYING, 1 }, { FIGHTING, PSYCHIC, 1 },
			{ FIGHTING, BUG, 1 }, { FIGHTING, ROCK, 2 }, { FIGHTING, DARK, 2 }, { FIGHTING, STEEL, 2 }, { FIGHTING, GHOST, 0 },
			{ POISON, GRASS, 2 }, { POISON, POISON, 1 }, { POISON, GROUND, 1 }, { POISON, ROCK, 1 }, { POISON, GHOST, 1 }, { POISON, STEEL, 0 },
			{ GROUND, FIRE, 2 }, { GROUND, ELECTRIC, 2 }, { GROUND, GRASS, 1 }, { GROUND, POISON, 2 }, { GROUND, FLYING, 0 }, { GROUND, BUG, 1 },
			{ GROUND, ROCK, 2 }, { GROUND, STEEL, 2 },
			{ FLYING, ELECTRIC, 1 }, { FLYING, GRASS, 2 }, { FLYING, FIGHTING, 2 }, { FLYING, BUG, 2 }, { FLYING, ROCK, 1 }, { FLYING, STEEL, 1 },
			{ PSYCHIC, FIGHTING, 2 }, { PSYCHIC, POISON, 2 }, { PSYCHIC, PSYCHIC, 1 }, { PSYCHIC, DARK, 0 }, { PSYCHIC, STEEL, 1 },
			{ BUG, FIRE, 1 }, { BUG, GRASS, 2 }, { BUG, FIGHTING, 1 }, { BUG, POISON, 1 }, { BUG, FLYING, 1 }, { BUG, PSYCHIC, 2 },
			{ BUG, GHOST, 1 }, { BUG, DARK, 2 }, { BUG, STEEL, 1 },
			{ ROCK, FIRE, 2 }, { ROCK, ICE, 2 }, { ROCK, FIGHTING, 1 }, { ROCK, GROUND, 1 }, { ROCK, FLYING, 2 }, { ROCK, BUG, 2 }, { ROCK, STEEL, 1 },
			{ GHOST, NORMAL, 0 }, { GHOST, PSYCHIC, 2 }, { GHOST, DARK, 1 }, { GHOST, STEEL, 1 }, { GHOST, GHOST, 2 },
			{ DRAGON, DRAGON, 2 }, { DRAGON, STEEL, 1 },
			{ DARK, FIGHTING, 1 }, { DARK, PSYCHIC, 2 }, { DARK, GHOST, 2 }, { DARK, DARK, 1 }, { DARK, STEEL, 1 },
			{ STEEL, FIRE, 1 }, { STEEL, WATER, 1 }, { STEEL, ELECTRIC, 1 }, { STEEL, ICE, 2 }, { STEEL, ROCK, 2 }, { STEEL, STEEL, 1 },
		};
		int one(int atk, int def, bool modern)
		{
			if (modern)
			{
				// since Generation VI: Fairy, and Steel no longer resists Ghost and Dark
				if (def == STEEL && (atk == GHOST || atk == DARK)) return 2;
				if (atk == FAIRY) return def == FIGHTING || def == DRAGON || def == DARK ? 4 : def == FIRE || def == POISON || def == STEEL ? 1 : 2;
				if (def == FAIRY) return atk == POISON || atk == STEEL ? 4 : atk == FIGHTING || atk == BUG || atk == DARK ? 1 : atk == DRAGON ? 0 : 2;
			}
			for (auto& c : CHART) if (c[0] == atk && c[1] == def) return c[2] == 0 ? 0 : c[2] == 1 ? 1 : 4;	// (halves)
			return 2;
		}

		// the saves' layouts (pokeruby / pokeemerald / pokefirered: SaveBlock1, SaveBlock2)
		const SaveLayout SAVE_RS = { 0x1220, 0x807, 0x800, 0x490, 0, { 0x560, 0x5b0, 0x600, 0x640, 0x740 }, { 20, 20, 16, 64, 46 } };
		const SaveLayout SAVE_E = { 0x1270, 0x867, 0x864, 0x490, 0xac, { 0x560, 0x5d8, 0x650, 0x690, 0x790 }, { 30, 30, 16, 64, 46 } };
		const SaveLayout SAVE_FR = { 0x0ee0, 0x820, 0x82c, 0x290, 0xf20, { 0x310, 0x3b8, 0x430, 0x464, 0x54c }, { 42, 30, 13, 58, 43 } };

		// the English releases (pret's symbol files: gPlayerPartyCount, gPlayerParty, gBattleMons, gBattleTypeFlags, gEnemyParty,
		// and gMain's byte with inBattle in bit 1: 0x439 in Emerald and FireRed, 0x43D in Ruby; then the save: Ruby's
		// gSaveBlock1/2 themselves, the others' gSaveBlock1Ptr/2Ptr; gMapHeader)
		const GameProfile GAMES[] = {
			{ "AXVE", "Pokémon Ruby", 0, RUBY_SAPPHIRE, 0x03004350, 0x03004360, 0x02024a80, 0x020239f8, 0x03001770 + 0x43d, 0x030045c0, true, 0x02025734, 0x02024ea4, 0x0202e828, SAVE_RS, {} },
			{ "AXPE", "Pokémon Sapphire", 0, RUBY_SAPPHIRE, 0x03004350, 0x03004360, 0x02024a80, 0x020239f8, 0x03001770 + 0x43d, 0x030045c0, true, 0x02025734, 0x02024ea4, 0x0202e828, SAVE_RS, {} },
			{ "BPEE", "Pokémon Emerald", 0, EMERALD, 0x020244e9, 0x020244ec, 0x02024084, 0x02022fec, 0x030022c0 + 0x439, 0x02024744, false, 0x03005d8c, 0x03005d90, 0x02037318, SAVE_E, {} },
			{ "BPRE", "Pokémon FireRed", 0, FIRERED, 0x02024029, 0x02024284, 0x02023be4, 0x02022b4c, 0x030030f0 + 0x439, 0x0202402c, false, 0x03005008, 0x0300500c, 0x02036dfc, SAVE_FR, {} },
			{ "BPGE", "Pokémon LeafGreen", 0, FIRERED, 0x02024029, 0x02024284, 0x02023be4, 0x02022b4c, 0x030030f0 + 0x439, 0x0202402c, false, 0x03005008, 0x0300500c, 0x02036dfc, SAVE_FR, {} },
		};
		const uint32_t DEX_LEN = 0x68;	// SaveBlock2's Pokédex: owned[52], then seen[52]

		// the Western character set: a byte's text ("" for one with none here)
		const char* glyph(uint8_t b)
		{
			static const char* const low[0x2f] = { " ", "À", "Á", "Â", "Ç", "È", "É", "Ê", "Ë", "Ì", "", "Î", "Ï", "Ò", "Ó", "Ô",
				"Œ", "Ù", "Ú", "Û", "Ñ", "ß", "à", "á", "", "ç", "è", "é", "ê", "ë", "ì", "", "î", "ï", "ò", "ó", "ô", "œ", "ù", "ú",
				"û", "ñ", "º", "ª", "", "&", "+" };
			static const char* const punct[0x1a] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "!", "?", ".", "-", "·",
				"…", "“", "”", "‘", "’", "♂", "♀", "¥", ",", "×", "/" };
			static const char* const umlaut[6] = { "Ä", "Ö", "Ü", "ä", "ö", "ü" };
			static char letter[2];
			if (b < 0x2f) return low[b];
			if (b >= 0xa1 && b <= 0xba) return punct[b - 0xa1];
			if (b >= 0xbb && b <= 0xd4) { letter[0] = (char)('A' + b - 0xbb); letter[1] = 0; return letter; }
			if (b >= 0xd5 && b <= 0xee) { letter[0] = (char)('a' + b - 0xd5); letter[1] = 0; return letter; }
			if (b >= 0xf1 && b <= 0xf6) return umlaut[b - 0xf1];
			switch (b)
			{
			case 0x34: return "Lv";
			case 0x35: return "=";
			case 0x36: return ";";
			case 0x51: return "¿";
			case 0x52: return "¡";
			case 0x5a: return "Í";
			case 0x5b: return "%";
			case 0x5c: return "(";
			case 0x5d: return ")";
			case 0x68: return "â";
			case 0x6f: return "í";
			case 0x85: return "<";
			case 0x86: return ">";
			case 0xef: return "▶";
			case 0xf0: return ":";
			}
			return "";
		}
		bool nameChar(uint8_t b) { return b != 0xff && *glyph(b); }

		// a table of fixed-size names: entry 0 is first (its bytes given), then count - 1 names, each a terminated
		// string of name characters filling no more than its entry
		long findNames(const uint8_t* rom, size_t n, const uint8_t* first, size_t firstLen, size_t entry, size_t count)
		{
			if (n < entry * count) return -1;
			for (const uint8_t* p = rom; (p = (const uint8_t*)memmem(p, n - (p - rom), first, firstLen)); p++)
			{
				size_t at = p - rom;
				if (at + entry * count > n) return -1;
				bool ok = true;
				for (size_t i = 1; i < count && ok; i++)
				{
					const uint8_t* e = rom + at + i * entry;
					size_t k = 0;
					while (k < entry && e[k] != 0xff) { if (!nameChar(e[k])) { ok = false; break; } k++; }
					if (k == 0 || k == entry) ok = false;	// (a name of none, or with no end in its entry)
				}
				if (ok) return (long)at;
			}
			return -1;
		}
		// a table of fixed-size records found by the bytes of two or three of its first records
		long findRecords(const uint8_t* rom, size_t n, const std::vector<std::vector<uint8_t>>& sig, size_t entry, size_t sigAt, size_t firstIndex, size_t count)
		{
			const auto& s0 = sig[0];
			for (const uint8_t* p = rom; (p = (const uint8_t*)memmem(p, n - (p - rom), s0.data(), s0.size())); p++)
			{
				size_t at = p - rom;
				if (at < sigAt + firstIndex * entry) continue;
				size_t start = at - sigAt - firstIndex * entry;
				if (start + entry * count > n) return -1;
				bool ok = true;
				for (size_t i = 1; i < sig.size() && ok; i++)
					ok = at + i * entry + sig[i].size() <= n && memcmp(rom + at + i * entry, sig[i].data(), sig[i].size()) == 0;
				if (ok) return (long)start;
			}
			return -1;
		}
	}

	const char* typeName(int t) { return t >= 0 && t < NUM_TYPES ? TYPE_NAMES[t] : ""; }
	uint32_t typeColor(int t) { return t >= 0 && t < NUM_TYPES ? TYPE_COLORS[t] : 0x808080; }

	int effect(int atk, int t1, int t2, bool modern)
	{
		if (atk < 0 || atk >= NUM_TYPES || t1 >= NUM_TYPES) return 4;
		int m = one(atk, t1, modern);	// in halves
		if (t2 != t1 && t2 < NUM_TYPES) return m * one(atk, t2, modern);	// halves x halves = quarters
		return m * 2;
	}

	bool typeInGame(int t, bool modern) { return (t >= 0 && t <= DARK && t != MYSTERY) || (modern && t == FAIRY); }

	const GameProfile* detectGameIn(const GameProfile* list, size_t count, const uint8_t* h, size_t n, uint32_t crc)
	{
		if (n < 0xb0) return nullptr;
		const GameProfile* any = nullptr;
		for (size_t i = 0; i < count; i++)
		{
			const GameProfile& g = list[i];
			if (memcmp(h + 0xac, g.code, 4) != 0) continue;
			if (g.crc && crc && g.crc == crc) return &g;
			if (!g.crc && !any) any = &g;
		}
		return any;
	}

	const GameProfile* detectGame(const uint8_t* h, size_t n, uint32_t crc)
	{
		return detectGameIn(GAMES, sizeof GAMES / sizeof GAMES[0], h, n, crc);
	}

	namespace
	{
		// the first profile with the ROM's code, a release's too (a guess, till its CRC-32 is known)
		const GameProfile* anyWithCode(const uint8_t* h, size_t n)
		{
			if (n < 0xb0) return nullptr;
			for (auto& g : GAMES) if (memcmp(h + 0xac, g.code, 4) == 0) return &g;
			return nullptr;
		}
	}

	bool crcMatters(const uint8_t* h, size_t n)
	{
		if (n < 0xb0) return false;
		for (auto& g : GAMES) if (g.crc && memcmp(h + 0xac, g.code, 4) == 0) return true;
		return false;
	}

	std::string decodeText(const uint8_t* p, size_t max)
	{
		std::string s;
		for (size_t i = 0; i < max && p[i] != 0xff; i++) s += glyph(p[i]);
		return s;
	}

	bool decodeParty(const uint8_t* p, Mon& m, MonCoding coding)
	{
		if (coding == MonCoding::EITHER) return decodeParty(p, m, MonCoding::ENCRYPTED) || decodeParty(p, m, MonCoding::PLAIN);
		m = Mon();
		bool plain = coding == MonCoding::PLAIN;
		uint32_t pid = le32(p), otid = le32(p + 4), key = plain ? 0 : pid ^ otid;
		uint8_t d[48];
		uint16_t sum = 0;
		for (int i = 0; i < 48; i += 4)
		{
			uint32_t v = le32(p + 0x20 + i) ^ key;
			d[i] = (uint8_t)v; d[i + 1] = (uint8_t)(v >> 8); d[i + 2] = (uint8_t)(v >> 16); d[i + 3] = (uint8_t)(v >> 24);
			sum = (uint16_t)(sum + le16(d + i) + le16(d + i + 2));
		}
		// (a CFRU hack: the checksum kept, or 0 when the hack no longer works it out (Pokémon Unbound); a slot then
		// counts with the game's own "has a species" bit and not a bad egg)
		if (plain ? (le16(p + 0x1c) != 0 && sum != le16(p + 0x1c)) || (p[0x13] & 3) != 2 : sum != le16(p + 0x1c)) return false;
		// the four 12-byte parts (growth, attacks, EVs and condition, miscellaneous) in one of the 24 orders, by the
		// personality value mod 24; the orders run in the lexicographic order of the parts' permutations (a CFRU hack:
		// always the first order, not encrypted, the checksum still the sum of the parts' halfwords)
		int order[4] = { 0, 1, 2, 3 };
		for (uint32_t k = plain ? 0 : pid % 24; k > 0; k--) std::next_permutation(order, order + 4);
		const uint8_t* part[4];
		for (int pos = 0; pos < 4; pos++) part[order[pos]] = d + pos * 12;
		m.species = le16(part[0]);
		if (m.species == 0) return false;
		m.item = le16(part[0] + 2);
		m.pid = pid;
		m.exp = le32(part[0] + 4);
		m.ppBonuses = part[0][8];
		m.friendship = part[0][9];
		for (int i = 0; i < 4; i++) { m.moves[i] = le16(part[1] + i * 2); m.pp[i] = part[1][8 + i]; }
		for (int i = 0; i < 6; i++) m.evs[i] = part[2][i];
		// the IVs: five bits each (HP, Attack, Defense, Speed, Sp. Atk, Sp. Def), then the egg bit, the ability bit
		uint32_t iv = le32(part[3] + 4);
		for (int i = 0; i < 6; i++) m.ivs[i] = (uint8_t)(iv >> (5 * i) & 31);
		m.abilitySlot = (uint8_t)(iv >> 31);
		m.egg = (p[0x13] & 4) || (iv >> 30 & 1);
		m.nick = decodeText(p + 8, 10);
		m.status = le32(p + 0x50);
		m.level = p[0x54];
		m.hp = le16(p + 0x56);
		m.maxHp = le16(p + 0x58);
		for (int i = 0; i < 5; i++) m.stats[i] = le16(p + 0x5a + i * 2);
		return true;
	}

	bool decodeParty(const uint8_t* p, Mon& m, const MonLayout& L)
	{
		if (!decodeParty(p, m, L.coding)) return false;
		m.species &= L.speciesMask;
		return m.species != 0;
	}

	bool decodeBattler(const uint8_t* p, Mon& m, int maxSpecies, const BattlerLayout& L)
	{
		m = Mon();
		m.species = le16(p);
		if (m.species == 0 || m.species > maxSpecies) return false;
		for (int i = 0; i < 4; i++) { m.moves[i] = le16(p + L.moves + i * 2); m.pp[i] = p[L.pp + i]; }
		m.types[0] = p[L.types]; m.types[1] = p[L.types + 1];
		m.hp = le16(p + L.hp);
		m.level = p[L.level];
		m.maxHp = le16(p + L.maxHp);
		m.item = le16(p + L.item);
		m.nick = decodeText(p + L.nick, 10);
		m.ppBonuses = p[L.ppBonuses];
		m.status = le32(p + L.status);
		m.egg = p[L.eggByte] & L.eggBit;
		m.exp = le32(p + L.exp);
		m.pid = le32(p + L.pid);
		return true;
	}

	const char* statusName(uint32_t s)
	{
		if (s & 7) return "SLP";
		if (s & 0x80) return "PSN";		// (badly)
		if (s & 0x08) return "PSN";
		if (s & 0x10) return "BRN";
		if (s & 0x20) return "FRZ";
		if (s & 0x40) return "PAR";
		return "";
	}

	const char* natureName(int n)
	{
		static const char* const NATURES[25] = { "Hardy", "Lonely", "Brave", "Adamant", "Naughty", "Bold", "Docile", "Relaxed", "Impish",
			"Lax", "Timid", "Hasty", "Serious", "Jolly", "Naive", "Modest", "Mild", "Quiet", "Bashful", "Rash", "Calm", "Gentle", "Sassy",
			"Careful", "Quirky" };
		return n >= 0 && n < 25 ? NATURES[n] : "";
	}

	void natureStats(int n, int& up, int& down)
	{
		// (the natures run five by five: the one raised by the row, the one lowered by the column)
		up = n / 5; down = n % 5;
		if (n < 0 || n >= 25 || up == down) up = down = -1;
	}

	int maxPP(int base, uint8_t bonuses, int slot) { return base + base * ((bonuses >> (2 * slot)) & 3) / 5; }

	std::string RomTables::speciesName(uint16_t i) const
	{
		if (i < species.size()) return species[i];
		return "#" + std::to_string(i);
	}
	std::string RomTables::moveName(uint16_t i) const
	{
		if (i < moves.size()) return moves[i];
		return "#" + std::to_string(i);
	}

	void findExtras(RomTables& t, const uint8_t* rom, size_t n, const GameProfile* g);

	RomTables findTables(const uint8_t* rom, size_t n, const GameProfile* game)
	{
		RomTables t;
		// species names: 11 bytes each, 412 (none + 411), the first "??????????"
		static const uint8_t q[11] = { 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xff };
		long at = findNames(rom, n, q, 11, 11, 412);
		if (at >= 0) for (int i = 0; i < 412; i++) t.species.push_back(decodeText(rom + at + i * 11, 11));
		// move names: 13 bytes each, 355 (none + 354), the first "-" (with zeros after it in Emerald, more ends in the
		// others)
		static const uint8_t dash[2] = { 0xae, 0xff };
		at = findNames(rom, n, dash, 2, 13, 355);
		if (at >= 0) for (int i = 0; i < 355; i++) t.moves.push_back(decodeText(rom + at + i * 13, 13));
		// base stats: 28 bytes a species, its types at 6 and 7; found by Bulbasaur's and Ivysaur's (HP, Attack, Defense,
		// Speed, Sp. Attack, Sp. Defense, types Grass and Poison)
		at = findRecords(rom, n, { { 45, 49, 49, 45, 65, 65, GRASS, POISON }, { 60, 62, 63, 60, 80, 80, GRASS, POISON } }, 28, 0, 1, 412);
		if (at >= 0)
			for (int i = 0; i < 412; i++)
			{
				const uint8_t* e = rom + at + i * 28;
				t.speciesTypes.push_back({ e[6], e[7] });
				t.genderRatio.push_back(e[0x10]);
				t.growth.push_back(e[0x13]);
				t.speciesAbilities.push_back({ e[0x16], e[0x17] });
			}
		// moves: 12 bytes each (effect, power, type, accuracy, PP, ...); found by Pound's, Karate Chop's and Double Slap's
		// power, type, accuracy and PP
		at = findRecords(rom, n, { { 40, NORMAL, 100, 35 }, { 50, FIGHTING, 100, 25 }, { 15, NORMAL, 85, 10 } }, 12, 1, 1, 355);
		if (at >= 0)
			for (int i = 0; i < 355; i++)
			{
				const uint8_t* e = rom + at + i * 12;
				t.moveInfo.push_back({ e[1], e[2], e[3], e[4] });
			}
		// the experience tables: six growth rates of 101 levels (u32), Medium Fast's first: 0, 1, 8, 27, 64, 125, ...
		static const uint8_t cubes[32] = { 0, 0, 0, 0, 1, 0, 0, 0, 8, 0, 0, 0, 27, 0, 0, 0, 64, 0, 0, 0, 125, 0, 0, 0, 216, 0, 0, 0, 87, 1, 0, 0 };
		for (const uint8_t* p = rom; n >= 6 * 404 && (p = (const uint8_t*)memmem(p, n - (p - rom), cubes, sizeof cubes)); p++)
		{
			size_t a = p - rom;
			if (a % 4 || a + 6 * 404 > n) continue;
			std::vector<std::array<uint32_t, 101>> x(6);
			bool ok = true;
			for (int g = 0; g < 6 && ok; g++)
				for (int l = 0; l <= 100 && ok; l++)
				{
					x[g][l] = le32(p + (g * 101 + l) * 4);
					ok = l == 0 ? x[g][l] == 0 : x[g][l] > x[g][l - 1] && x[g][l] <= 2000000;	// (rising, none past 1,640,000)
				}
			if (ok) { t.expTable = x; break; }
		}
		// the menu icons: a table of ROM pointers by species, its first two the same (none and Bulbasaur share one) and
		// the 25 placeholders after Celebi (252-276) one question mark; each 1 KB of tiles (two 32x32 frames)
		auto ptr = [&](size_t a, size_t len) -> long
		{
			if (a + 4 > n) return -1;
			uint32_t v = le32(rom + a);
			if (v < 0x08000000u || v - 0x08000000u + len > n) return -1;
			return (long)(v - 0x08000000u);
		};
		const int ICONS = 413;	// (the egg's at 412)
		for (size_t a = 0; a + ICONS * 4 <= n; a += 4)
		{
			if (le32(rom + a) != le32(rom + a + 4) || ptr(a, 1024) < 0) continue;
			uint32_t qm = le32(rom + a + 252 * 4);
			bool ok = qm != le32(rom + a) && le32(rom + a + 8) != le32(rom + a);
			for (int i = 253; i <= 276 && ok; i++) ok = le32(rom + a + i * 4) == qm;
			for (int i = 0; i < ICONS && ok; i++) ok = ptr(a + i * 4, 1024) >= 0;
			if (!ok) continue;
			// their palette numbers: a byte each, just after (the same source file in all three games); 0-2,
			// Bulbasaur's 1, the placeholders' all one. Required: Emerald's footprint table, before it, has the same
			// shape, its pictures as far apart (each just after an icon), and no palette numbers after it
			auto pals = [&](size_t b)
			{
				if (b + ICONS > n || rom[b + 1] != 1) return false;
				int c[3] = {};
				for (int i = 0; i < ICONS; i++) { if (rom[b + i] > 2) return false; c[rom[b + i]]++; }
				for (int i = 253; i <= 276; i++) if (rom[b + i] != rom[b + 252]) return false;
				return c[0] > 40 && c[1] > 40 && c[2] > 40;
			};
			long pb = -1;
			for (size_t b = a + ICONS * 4; b < a + ICONS * 4 + 256 && pb < 0; b++) if (pals(b)) pb = (long)b;
			if (pb < 0) continue;
			t.iconPal.assign(rom + pb, rom + pb + ICONS);
			for (int i = 0; i < ICONS; i++)
			{
				// frame 1's 16 tiles of 8x8 (32 bytes each, two pixels a byte, the left one in the low half), four a row
				const uint8_t* g = rom + ptr(a + i * 4, 1024);
				std::vector<uint8_t> px(32 * 32);
				for (int tile = 0; tile < 16; tile++)
					for (int y = 0; y < 8; y++)
						for (int x = 0; x < 8; x++)
						{
							uint8_t b = g[tile * 32 + y * 4 + x / 2];
							px[((tile / 4) * 8 + y) * 32 + (tile % 4) * 8 + x] = x & 1 ? b >> 4 : b & 15;
						}
				t.icons.push_back(px);
			}
			break;
		}
		// the three icon palettes: sprite palettes { pointer, tag 56000 + i, 0 }, six of them (the last three unused)
		static const uint8_t tag0[4] = { 0xc0, 0xda, 0, 0 };
		for (const uint8_t* p = rom; n > 48 && (p = (const uint8_t*)memmem(p, n - (p - rom), tag0, 4)); p++)
		{
			size_t a = p - rom;
			if (a < 4 || (a - 4) % 4 || a + 44 > n) continue;
			bool ok = true;
			for (int i = 0; i < 3 && ok; i++)
				ok = le16(rom + a + i * 8) == 0xdac0 + i && le16(rom + a + i * 8 + 2) == 0 && ptr(a - 4 + i * 8, 32) >= 0;
			if (!ok) continue;
			for (int i = 0; i < 3; i++)
			{
				const uint8_t* c = rom + ptr(a - 4 + i * 8, 32);
				std::array<uint32_t, 16> pal;
				for (int k = 0; k < 16; k++)
				{
					uint16_t v = le16(c + k * 2);
					auto f = [](int x) { return (uint32_t)((x << 3) | (x >> 2)); };
					pal[k] = (k ? 0xff000000u : 0) | f(v & 31) | f((v >> 5) & 31) << 8 | f((v >> 10) & 31) << 16;	// (RGBA in memory)
				}
				t.palettes.push_back(pal);
			}
			break;
		}
		findExtras(t, rom, n, game ? game : detectGame(rom, n));
		// CFRU's "Decryption" byte replacements (FireRed 1.0): GetSubstruct's order always the first (movs r0, #0) and
		// the XOR in EncryptBoxMon / DecryptBoxMon gone (nops): the party is kept as it is, which the games' own
		// decoding read as noise (an empty party in Pokémon Unbound)
		static const uint8_t ORDER0[] = { 0x00, 0x20, 0xc0, 0x46, 0xc0, 0x46 }, NOP[] = { 0xc0, 0x46 };
		if (n > 0x3f960 && memcmp(rom + 0xac, "BPRE", 4) == 0 && memcmp(rom + 0x3f94c, ORDER0, 6) == 0 &&
			memcmp(rom + 0x3f906, NOP, 2) == 0 && memcmp(rom + 0x3f92a, NOP, 2) == 0)
			t.has.plainMons = true;
		// CFRU's "Expanded Pokedex RAM": the seen flags at a fixed address in the literal at 0x104b10 (gSaveBlock1's
		// dexSeenFlags, 0x0202583c), the caught ones right after; ResetPokedex clears 0x7d bytes of each (999
		// national numbers). The games' own (52 bytes in SaveBlock2) are no longer used
		if (n > 0x104b20 && memcmp(rom + 0xac, "BPRE", 4) == 0 && memcmp(rom + 0x104b00, NOP, 2) == 0 &&
			memcmp(rom + 0x104b02, NOP, 2) == 0 && rom[0x549b6] == 0x7d && rom[0x549b7] == 0x22 && rom[0x549be] == 0x7d &&
			rom[0x549bf] == 0x30)
		{
			uint32_t a = le32(rom + 0x104b10);
			if (a >> 24 == 2 && (a & 0xffffff) + 2 * 0x7d <= 0x40000 && le32(rom + 0x549d0) == a) { t.has.dexRam = a; t.has.dexBytes = 0x7d; }
		}
		// CFRU's "Expanded Bag": GetBagItemQuantity reads the count as it is (ldrh r0, [r0, #4] at 0x99dfc), the
		// pockets in RAM of their own that gBagPockets (0x0203988c: pointer and size, five of them) points to (a CFRU hack
		// without it: its bag may not be the save's either)
		if (n > 0x9a000 && memcmp(rom + 0xac, "BPRE", 4) == 0 && rom[0x99dfc] == 0x80 && rom[0x99dfd] == 0x88 &&
			memcmp(rom + 0x98984, NOP, 2) == 0)
		{
			t.has.bagPockets = 0x0203988c;
			t.has.bagUnsure = false;
		}
		return t;
	}

	// ---- the tables through the games' own ROM header (Emerald, FireRed, LeafGreen and the hacks built on them), and
	// what the Map, Bag, Dex and Guide pages read from the ROM
	namespace
	{
		long romPtr(const uint8_t* rom, size_t n, size_t at, size_t len = 1)
		{
			if (at + 4 > n) return -1;
			uint32_t v = le32(rom + at);
			if (v < 0x08000000u || v - 0x08000000u + len > n) return -1;
			return (long)(v - 0x08000000u);
		}
		// a name of no more than len bytes with an end: its length, -1 not one
		int nameAt(const uint8_t* p, size_t len)
		{
			size_t k = 0;
			while (k < len && p[k] != 0xff) { if (!nameChar(p[k])) return -1; k++; }
			return k == len ? -1 : (int)k;
		}
		// a region map section's name: as nameAt, a control code and its argument allowed in it (Ruby and Sapphire's
		// town names have one between the name and TOWN / CITY)
		int sectionName(const uint8_t* p, size_t len)
		{
			size_t k = 0;
			int chars = 0;
			while (k < len && p[k] != 0xff)
			{
				if (p[k] == 0xfc || p[k] == 0xfd) { k += 2; continue; }
				if (!nameChar(p[k])) return -1;
				k++; chars++;
			}
			return k >= len ? -1 : chars;
		}
		// a table of names, entry bytes each, from at: how many in a row are names (a blank one ends it unless allowed)
		int countNames(const uint8_t* rom, size_t n, long at, size_t entry, int max)
		{
			int c = 0;
			while (c < max && at + (c + 1) * entry <= n && nameAt(rom + at + c * entry, entry) > (c == 0 ? -1 : 0)) c++;
			return c;
		}
		// text with the games' line breaks as spaces, control codes left out
		std::string longText(const uint8_t* rom, size_t n, long at, size_t max)
		{
			std::string s;
			for (size_t i = 0; at >= 0 && (size_t)at + i < n && i < max; i++)
			{
				uint8_t b = rom[at + i];
				if (b == 0xff) break;
				if (b == 0xfe || b == 0xfa || b == 0xfb) { if (!s.empty() && s.back() != ' ') s += ' '; continue; }
				if (b == 0xfc || b == 0xfd) { i++; continue; }	// (a control code and its argument)
				s += glyph(b);
			}
			return s;
		}
		// text back to the games' characters (the glyphs above, longest first), with the end; false: one it hasn't
		bool encName(const std::string& s, std::vector<uint8_t>& o)
		{
			o.clear();
			for (size_t i = 0; i < s.size();)
			{
				int bestB = -1; size_t bestL = 0;
				for (int b = 0; b < 0xff; b++)
				{
					const char* g = glyph((uint8_t)b);
					size_t l = strlen(g);
					if (l > bestL && s.compare(i, l, g) == 0) { bestB = b; bestL = l; }
				}
				if (bestB < 0) return false;
				o.push_back((uint8_t)bestB);
				i += bestL;
			}
			o.push_back(0xff);
			return true;
		}
		// a trainer record (40 bytes: party flags, class, music, picture, name[12], items[4], doubles, AI flags, party
		// size, party pointer) that looks like one
		bool trainerAt(const uint8_t* rom, size_t n, size_t a)
		{
			if (a + 40 > n || rom[a] > 3 || rom[a + 32] < 1 || rom[a + 32] > 6) return false;
			if (nameAt(rom + a + 4, 12) <= 0) return false;
			return romPtr(rom, n, a + 36, 8 * rom[a + 32]) >= 0;
		}
	}

	namespace
	{
		// a CFRU hack, told by its tables (the Fairy type, more than 412 species): what it has beyond FireRed's
		// (its bag perhaps not the save's: until its own is found, below)
		void cfru(RomTables& t) { t.has.modernTypes = t.has.ownDex = t.has.leadersFromTrainers = t.has.bagUnsure = true; }

		void fromHeader(RomTables& t, const uint8_t* rom, size_t n)
		{
			// the header (pokeemerald / pokefirered rom_header_gf.c): 0x138 icons, 0x13c their palette numbers, 0x140
			// the icon palettes, 0x144 species names, 0x148 move names, 0x1bc base stats, 0x1c0 ability names, 0x1c8
			// items, 0x1cc moves. Each checked by its first entries; one that isn't kept as the shape search found it
			if (n < 0x200) return;
			long sp = romPtr(rom, n, 0x144, 11 * 2);
			if (sp < 0 || nameAt(rom + sp + 11, 11) <= 0) return;
			int ns = countNames(rom, n, sp, 11, 2000);
			long mvAt = romPtr(rom, n, 0x148, 26);
			if (mvAt > sp && (mvAt - sp) / 11 < ns) ns = (int)((mvAt - sp) / 11);	// (Emerald's move names follow right after)
			if (ns < 412) return;
			t.numSpecies = ns;
			t.species.clear();
			for (int i = 0; i < ns; i++) t.species.push_back(decodeText(rom + sp + i * 11, 11));
			long mv = romPtr(rom, n, 0x148, 13 * 2);
			if (mv >= 0 && nameAt(rom + mv + 13, 13) > 0)
			{
				int nm = countNames(rom, n, mv, 13, 2000);
				t.moves.clear();
				for (int i = 0; i < nm; i++) t.moves.push_back(decodeText(rom + mv + i * 13, 13));
				long mi = romPtr(rom, n, 0x1cc, 12 * (size_t)nm);
				// (Pound: power 40, Normal, accuracy 100, 35 PP; the table may sit at any byte: all its fields are bytes)
				if (mi >= 0 && rom[mi + 13] == 40 && rom[mi + 14] == NORMAL && rom[mi + 15] == 100 && rom[mi + 16] == 35)
				{
					t.moveInfo.clear();
					for (int i = 0; i < nm; i++) { const uint8_t* e = rom + mi + i * 12; t.moveInfo.push_back({ e[1], e[2], e[3], e[4] }); }
				}
			}
			long bs = romPtr(rom, n, 0x1bc, 28 * (size_t)ns);
			if (bs >= 0 && rom[bs + 28] == 45 && rom[bs + 28 + 6] == GRASS && rom[bs + 28 + 7] == POISON)
			{
				t.speciesTypes.clear(); t.genderRatio.clear(); t.growth.clear(); t.speciesAbilities.clear();
				for (int i = 0; i < ns; i++)
				{
					const uint8_t* e = rom + bs + i * 28;
					t.speciesTypes.push_back({ e[6], e[7] });
					t.genderRatio.push_back(e[0x10]);
					t.growth.push_back(e[0x13]);
					t.speciesAbilities.push_back({ e[0x16], e[0x17] });
					if (e[6] == FAIRY || e[7] == FAIRY) cfru(t);
				}
			}
			if (ns > 412) cfru(t);
			// abilities: their names' size from the gap between the first two ("-------", then Stench)
			long ab = romPtr(rom, n, 0x1c0, 64);
			if (ab >= 0 && nameAt(rom + ab, 32) > 0)
			{
				size_t k = nameAt(rom + ab, 32) + 1, e = k;
				while (e < 32 && rom[ab + e] == 0xff) e++;
				if (e < 32 && nameAt(rom + ab + e, e) > 0)
				{
					int na = countNames(rom, n, ab, e, 1000);
					t.abilities.clear();
					for (int i = 0; i < na; i++) t.abilities.push_back(decodeText(rom + ab + i * e, e));
				}
			}
			// items: 44 bytes (name[14], its own index, price, hold effect, parameter, description, importance, ?, pocket...)
			long it = romPtr(rom, n, 0x1c8, 44 * 2);
			if (it >= 0)
				for (int i = 0; i < 2000 && it + (i + 1) * 44 <= (long)n; i++)
				{
					const uint8_t* e = rom + it + i * 44;
					if (nameAt(e, 14) < 0 && le16(e + 14) != i) break;
					t.items.push_back({ decodeText(e, 14), e[0x1a] });
				}
			// icons: a pointer a species, a palette number a species, the palettes (sprite palettes: pointer, tag 56000 + i)
			long ic = romPtr(rom, n, 0x138, 4 * (size_t)(ns + 1)), ip = romPtr(rom, n, 0x13c, ns + 1), pl = romPtr(rom, n, 0x140, 8 * 3);
			if (ic >= 0 && ip >= 0 && pl >= 0 && le16(rom + pl + 4) == 0xdac0)
			{
				std::vector<std::array<uint32_t, 16>> pals;
				for (int i = 0; i < 16; i++)
				{
					long c = romPtr(rom, n, pl + i * 8, 32);
					if (c < 0 || le16(rom + pl + i * 8 + 4) != 0xdac0 + i) break;
					std::array<uint32_t, 16> pal;
					for (int k = 0; k < 16; k++)
					{
						uint16_t v = le16(rom + c + k * 2);
						auto f = [](int x) { return (uint32_t)((x << 3) | (x >> 2)); };
						pal[k] = (k ? 0xff000000u : 0) | f(v & 31) | f((v >> 5) & 31) << 8 | f((v >> 10) & 31) << 16;
					}
					pals.push_back(pal);
				}
				std::vector<std::vector<uint8_t>> icons;
				int count = std::max(ns, 413);
				for (int i = 0; i < count; i++)
				{
					long g = romPtr(rom, n, ic + i * 4, 1024);
					std::vector<uint8_t> px;
					if (g >= 0 && rom[ip + i] < pals.size())
					{
						px.resize(32 * 32);
						for (int tile = 0; tile < 16; tile++)
							for (int y = 0; y < 8; y++)
								for (int x = 0; x < 8; x++)
								{
									uint8_t b = rom[g + tile * 32 + y * 4 + x / 2];
									px[((tile / 4) * 8 + y) * 32 + (tile % 4) * 8 + x] = x & 1 ? b >> 4 : b & 15;
								}
					}
					icons.push_back(px);
				}
				if (!pals.empty())
				{
					t.icons = icons;
					t.iconPal.assign(rom + ip, rom + ip + count);
					t.palettes = pals;
				}
			}
		}

		// the region maps and their names
		void mapTables(RomTables& t, const uint8_t* rom, size_t n, Family gen)
		{
			if (gen == FIRERED)
			{
				// FireRed: the names of sections 0x58 (Pallet Town) on, a pointer each: the run of pointers to short
				// names nearest 109 long (another such table, of names people have, is longer)
				long best = -1; int bestLen = 0;
				for (size_t a = 0; a + 4 <= n; a += 4)
				{
					int k = 0;
					for (;; k++)
					{
						long p = romPtr(rom, n, a + k * 4, 2);
						int l = p < 0 ? -1 : nameAt(rom + p, 22);
						if (l < 2) break;
					}
					if (k >= 100 && k <= 140 && (best < 0 || abs(k - 109) < abs(bestLen - 109))) { best = (long)a; bestLen = k; }
					if (k > 0) a += (k - 1) * 4;
				}
				if (best >= 0)
				{
					t.mapNames.assign(0x58, "");
					for (int i = 0; i < bestLen; i++) t.mapNames.push_back(decodeText(rom + romPtr(rom, n, best + i * 4), 22));
				}
				// the region maps: 22 x 15 sections a layer (Kanto, the Sevii Islands; a map layer and a dungeon one)
				t.mapsecNone = 0xc5;
				const int W = 22, H = 15;
				for (size_t a = 0; a + W * H <= n; a += 2)
				{
					int none = 0, okc = 0, kinds = 0;
					bool seen[256] = {};
					for (int i = 0; i < W * H; i++)
					{
						uint8_t c = rom[a + i];
						if (c < 0x58 || c > 0xc5) break;
						okc++;
						if (c == 0xc5) none++;
						else if (!seen[c]) { seen[c] = true; kinds++; }
					}
					if (okc != W * H || none < W * H / 3 || none == W * H || kinds < 4) continue;
					// the first one found (Kanto's map layer) and the layers right after it, as pokefirered keeps them
					// (four maps, two layers each: the map, the dungeons; a layer can have few sections, Sevii's caves)
					for (size_t b = a; b + W * H <= n && t.grids.size() < 8; b += W * H)
					{
						bool in = true;
						for (int i = 0; i < W * H && in; i++) in = rom[b + i] >= 0x58 && rom[b + i] <= 0xc5;
						if (!in) break;
						RomTables::Grid g; g.w = W; g.h = H; g.cells.assign(rom + b, rom + b + W * H);
						t.grids.push_back(g);
					}
					break;
				}
				return;
			}
			// Emerald, Ruby, Sapphire: a record a section (x, y, width, height on a 28 x 15 map, the name's pointer)
			long best = -1; int bestLen = 0;
			for (size_t a = 0; a + 8 <= n; a += 4)
			{
				int k = 0;
				for (;; k++)
				{
					size_t r = a + k * 8;
					if (r + 8 > n || rom[r] >= 28 || rom[r + 1] >= 15 || rom[r + 2] == 0 || rom[r + 3] == 0 || rom[r + 2] > 28 || rom[r + 3] > 15) break;
					long p = romPtr(rom, n, r + 4, 2);
					if (p < 0 || (!k && sectionName(rom + p, 22) < 2)) break;	// (a few sections have no name)
				}
				if (k > bestLen) { best = (long)a; bestLen = k; }
			}
			if (bestLen < 60) return;
			RomTables::Grid g; g.w = 28; g.h = 15; g.cells.assign(28 * 15, 0xff);
			for (int i = bestLen - 1; i >= 0; i--)	// (towns, the first sections, over the routes)
			{
				const uint8_t* r = rom + best + i * 8;
				t.mapNames.resize(bestLen);
				t.mapNames[i] = longText(rom, n, romPtr(rom, n, best + i * 8 + 4), 22);	// (Ruby/Sapphire's town names: "LITTLEROOT{FC 00} TOWN")
				if (r[0] == 0 && r[1] == 0) continue;	// (sections that aren't on the map: the truck, the Battle Tower...)
				for (int y = r[1]; y < r[1] + r[3] && y < 15; y++)
					for (int x = r[0]; x < r[0] + r[2] && x < 28; x++) g.cells[y * 28 + x] = (uint8_t)i;
			}
			t.grids.push_back(g);
		}

		// trainers: every record with that name, the first of the class asked for (any: -1) from the byte from on
		long findTrainer(const uint8_t* rom, size_t n, const std::string& name, int cls, size_t from = 0)
		{
			std::vector<uint8_t> e;
			if (!encName(name, e) || from >= n) return -1;
			for (const uint8_t* p = rom + from; (p = (const uint8_t*)memmem(p, n - (p - rom), e.data(), e.size())); p++)
			{
				size_t a = p - rom;
				if (a < 4 || !trainerAt(rom, n, a - 4)) continue;
				if (cls < 0 || rom[a - 3] == cls) return (long)(a - 4);
			}
			return -1;
		}
		std::vector<RomTables::TeamMon> teamAt(const uint8_t* rom, size_t n, long a, int numSpecies)
		{
			std::vector<RomTables::TeamMon> out;
			if (a < 0) return out;
			int size = rom[a + 32], stride = rom[a] & 1 ? 16 : 8;
			long p = romPtr(rom, n, a + 36, (size_t)stride * size);
			for (int i = 0; p >= 0 && i < size; i++)
			{
				const uint8_t* m = rom + p + i * stride;
				uint16_t sp = le16(m + 4);
				if (sp == 0 || sp >= numSpecies || m[2] == 0 || m[2] > 100) { out.clear(); break; }
				out.push_back({ sp, m[2] });
			}
			return out;
		}
	}

	// the gym leaders and the league, in the order they are met (our own words: who, where, the type)
	const Goal GOALS_RS[] = { { "ROXANNE", "Roxanne", "Rustboro City gym", ROCK }, { "BRAWLY", "Brawly", "Dewford Town gym", FIGHTING },
		{ "WATTSON", "Wattson", "Mauville City gym", ELECTRIC }, { "FLANNERY", "Flannery", "Lavaridge Town gym", FIRE },
		{ "NORMAN", "Norman", "Petalburg City gym", NORMAL }, { "WINONA", "Winona", "Fortree City gym", FLYING },
		{ "TATE&LIZA", "Tate & Liza", "Mossdeep City gym", PSYCHIC }, { "WALLACE", "Wallace", "Sootopolis City gym", WATER },
		{ "SIDNEY", "Sidney", "the Pokémon League", DARK }, { "PHOEBE", "Phoebe", "the Pokémon League", GHOST },
		{ "GLACIA", "Glacia", "the Pokémon League", ICE }, { "DRAKE", "Drake", "the Pokémon League", DRAGON },
		{ "STEVEN", "Steven, the Champion", "the Pokémon League", STEEL } };
	const Goal GOALS_E[] = { { "ROXANNE", "Roxanne", "Rustboro City gym", ROCK }, { "BRAWLY", "Brawly", "Dewford Town gym", FIGHTING },
		{ "WATTSON", "Wattson", "Mauville City gym", ELECTRIC }, { "FLANNERY", "Flannery", "Lavaridge Town gym", FIRE },
		{ "NORMAN", "Norman", "Petalburg City gym", NORMAL }, { "WINONA", "Winona", "Fortree City gym", FLYING },
		{ "TATE&LIZA", "Tate & Liza", "Mossdeep City gym", PSYCHIC }, { "JUAN", "Juan", "Sootopolis City gym", WATER },
		{ "SIDNEY", "Sidney", "the Pokémon League", DARK }, { "PHOEBE", "Phoebe", "the Pokémon League", GHOST },
		{ "GLACIA", "Glacia", "the Pokémon League", ICE }, { "DRAKE", "Drake", "the Pokémon League", DRAGON },
		{ "WALLACE", "Wallace, the Champion", "the Pokémon League", WATER } };
	const Goal GOALS_FR[] = { { "BROCK", "Brock", "Pewter City gym", ROCK }, { "MISTY", "Misty", "Cerulean City gym", WATER },
		{ "LT. SURGE", "Lt. Surge", "Vermilion City gym", ELECTRIC }, { "ERIKA", "Erika", "Celadon City gym", GRASS },
		{ "KOGA", "Koga", "Fuchsia City gym", POISON }, { "SABRINA", "Sabrina", "Saffron City gym", PSYCHIC },
		{ "BLAINE", "Blaine", "Cinnabar Island gym", FIRE }, { "GIOVANNI", "Giovanni", "Viridian City gym", GROUND },
		{ "LORELEI", "Lorelei", "the Pokémon League", ICE }, { "BRUNO", "Bruno", "the Pokémon League", FIGHTING },
		{ "AGATHA", "Agatha", "the Pokémon League", GHOST }, { "LANCE", "Lance", "the Pokémon League", DRAGON } };

	const Goal* goalsFor(Family gen, int& count)
	{
		if (gen == RUBY_SAPPHIRE) { count = (int)(sizeof GOALS_RS / sizeof GOALS_RS[0]); return GOALS_RS; }
		if (gen == EMERALD) { count = (int)(sizeof GOALS_E / sizeof GOALS_E[0]); return GOALS_E; }
		count = (int)(sizeof GOALS_FR / sizeof GOALS_FR[0]); return GOALS_FR;
	}

	void findExtras(RomTables& t, const uint8_t* rom, size_t n, const GameProfile* g)
	{
		Family gen = g ? g->family : RUBY_SAPPHIRE;
		if (gen != RUBY_SAPPHIRE) fromHeader(t, rom, n);
		// no header pointers (Ruby, Sapphire): the ability names by their shape (13 bytes each, the 78 of the games, the
		// first "-------"), the items by theirs (44 bytes, the first "????????", each with its own index at 14)
		static const uint8_t none[8] = { 0xae, 0xae, 0xae, 0xae, 0xae, 0xae, 0xae, 0xff };
		for (const uint8_t* p = rom; t.abilities.empty() && (p = (const uint8_t*)memmem(p, n - (p - rom), none, sizeof none)); p++)
		{
			long a = p - rom;
			if (countNames(rom, n, a, 13, 78) < 78) continue;
			for (int i = 0; i < 78; i++) t.abilities.push_back(decodeText(rom + a + i * 13, 13));
		}
		static const uint8_t unknown[9] = { 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xff };
		for (const uint8_t* p = rom; t.items.empty() && (p = (const uint8_t*)memmem(p, n - (p - rom), unknown, sizeof unknown)); p++)
		{
			size_t a = p - rom;
			if (a + 3 * 44 > n || le16(p + 14) != 0 || le16(p + 44 + 14) != 1 || le16(p + 88 + 14) != 2) continue;
			for (int i = 0; i < 2000 && a + (i + 1) * 44 <= n; i++)
			{
				const uint8_t* e = rom + a + i * 44;
				if (nameAt(e, 14) < 0 && le16(e + 14) != i) break;
				t.items.push_back({ decodeText(e, 14), e[0x1a] });
			}
		}
		mapTables(t, rom, n, gen);
		findRegionMaps(t, rom, n, gen);
		// species -> national number: a run of u16s 1, 2, 3, ... (Bulbasaur to Celebi keep their numbers)
		uint8_t seq[58];
		for (int i = 0; i < 29; i++) { seq[i * 2] = (uint8_t)(i + 1); seq[i * 2 + 1] = 0; }
		for (const uint8_t* p = rom; (p = (const uint8_t*)memmem(p, n - (p - rom), seq, sizeof seq)); p++)
		{
			size_t a = p - rom;
			if (a % 2 || a + 2 * (size_t)t.numSpecies > n) continue;
			t.natDex.assign(1, 0);
			for (int s = 1; s < t.numSpecies; s++) t.natDex.push_back(le16(rom + a + (s - 1) * 2));
			for (int s = 1; s < t.numSpecies; s++)
			{
				int d = t.natDex[s];
				if (d <= 0 || d > 2000) continue;
				if ((int)t.dexSpecies.size() <= d) t.dexSpecies.resize(d + 1, 0);
				if (!t.dexSpecies[d]) t.dexSpecies[d] = (uint16_t)s;
			}
			break;
		}
		// the Pokédex entries: the category name first ("UNKNOWN" for none, "SEED" for Bulbasaur), 32 or 36 bytes each
		for (const char* pair : { "SEED", "Seed" })
		{
			std::vector<uint8_t> seed, unk;
			encName(pair, seed); encName(pair[1] == 'E' ? "UNKNOWN" : "Unknown", unk);
			for (const uint8_t* p = rom; t.dexEntries < 0 && (p = (const uint8_t*)memmem(p, n - (p - rom), seed.data(), seed.size())); p++)
			{
				size_t a = p - rom;
				for (int st : { 32, 36 })
					if (a >= (size_t)st && !memcmp(rom + a - st, unk.data(), unk.size()) && romPtr(rom, n, a + 16, 2) >= 0)
					{ t.dexEntries = (long)(a - st); t.dexStride = st; break; }
			}
		}
		// the wild Pokémon: headers of 20 bytes (map group, number, two zeros, four pointers: grass, water, rock smash,
		// fishing), the longest run of them (a hack may add smaller tables for times of day; those are left out)
		int best = 0;
		for (size_t a = 0; a + 20 <= n; a += 4)
		{
			int k = 0;
			for (;; k++)
			{
				size_t r = a + k * 20;
				if (r + 20 > n || rom[r] == 0xff || rom[r] >= 64 || rom[r + 2] || rom[r + 3]) break;
				bool any = false, bad = false;
				for (int i = 0; i < 4; i++)
				{
					uint32_t v = le32(rom + r + 4 + i * 4);
					if (!v) continue;
					any = true;
					if (romPtr(rom, n, r + 4 + i * 4, 8) < 0) bad = true;
				}
				if (bad || !any) break;
			}
			if (k > best && a + k * 20 + 1 < n && rom[a + k * 20] == 0xff) { best = k; t.wildHeaders = (long)a; }
			if (k > 0) a += (k - 1) * 20;
		}
		if (best < 20) t.wildHeaders = -1;
		// the gym leaders' teams for the guide; a CFRU hack's leaders (class 0x54, FireRed's) in the table's order
		if (t.has.leadersFromTrainers)
		{
			// (every record of that class anywhere: a hack can keep a table for each difficulty)
			for (size_t p = 0; p + 120 <= n; p += 4)
			{
				if (rom[p + 1] != 0x54 || !trainerAt(rom, n, p)) continue;
				std::string nm = decodeText(rom + p + 4, 12);
				if (nm.find_first_not_of(' ') == std::string::npos || std::find(t.leaders.begin(), t.leaders.end(), nm) != t.leaders.end()) continue;
				t.leaders.push_back(nm);
			}
			// in the order they are met: by their team's highest level (the gyms get harder)
			std::vector<std::pair<int, std::string>> by;
			std::map<std::string, int> lt;
			for (auto& nm : t.leaders)
			{
				// (the lowest of its battles' top levels: a hack keeps one for each difficulty and rematch)
				int top = 999;
				std::map<int, int> types;
				std::vector<uint8_t> e;
				if (encName(nm, e))
					for (const uint8_t* q = rom; (q = (const uint8_t*)memmem(q, n - (q - rom), e.data(), e.size())); q++)
					{
						size_t a = q - rom;
						if (a < 4 || rom[a - 3] != 0x54 || !trainerAt(rom, n, a - 4)) continue;
						int hi = 0;
						for (auto& m : teamAt(rom, n, (long)a - 4, t.numSpecies))
						{
							hi = std::max(hi, (int)m.level);
							if (m.species < t.speciesTypes.size())
							{
								types[t.speciesTypes[m.species][0]] += 2;	// (the first type counts more)
								if (t.speciesTypes[m.species][1] != t.speciesTypes[m.species][0]) types[t.speciesTypes[m.species][1]]++;
							}
						}
						if (hi) top = std::min(top, hi);
					}
				int ty = -1, most = 0;
				for (auto& x : types) if (x.second > most) { most = x.second; ty = x.first; }
				by.push_back({ top, nm });
				lt[nm] = ty;
			}
			std::stable_sort(by.begin(), by.end(), [](auto& x, auto& y) { return x.first < y.first; });
			t.leaders.clear();
			for (auto& b : by) if (b.first < 999) { t.leaders.push_back(b.second); t.leaderTypes.push_back(lt[b.second]); }
		}
	}

	std::string RomTables::itemName(uint16_t i) const
	{
		if (i < items.size()) return items[i].name;
		return "#" + std::to_string(i);
	}

	std::string RomTables::abilityName(const Mon& m) const
	{
		if (m.species >= speciesAbilities.size()) return "";
		const auto& a = speciesAbilities[m.species];
		int id = m.abilitySlot && a[1] ? a[1] : a[0];
		return id > 0 && id < (int)abilities.size() ? abilities[id] : "";
	}

	std::string RomTables::mapName(int mapsec) const
	{
		return mapsec >= 0 && mapsec < (int)mapNames.size() ? mapNames[mapsec] : "";
	}

	bool RomTables::dexEntry(int nat, DexEntry& e) const
	{
		if (!rom || dexEntries < 0 || nat <= 0) return false;
		const uint8_t* r = rom->data();
		size_t n = rom->size(), a = dexEntries + (size_t)nat * dexStride;
		if (a + dexStride > n || nameAt(r + a, 12) <= 0) return false;
		e.category = decodeText(r + a, 12);
		e.height = le16(r + a + 12);
		e.weight = le16(r + a + 14);
		e.text = longText(r, n, romPtr(r, n, a + 16, 2), 400);
		return true;
	}

	std::vector<RomTables::Wild> RomTables::wild(int group, int num, int method) const
	{
		std::vector<Wild> out;
		if (!rom || wildHeaders < 0 || method < 0 || method > 5) return out;
		const uint8_t* r = rom->data();
		size_t n = rom->size();
		static const int GRASS[12] = { 20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1 }, WATER5[5] = { 60, 30, 5, 4, 1 },
			FISH[10] = { 70, 30, 60, 20, 20, 40, 40, 15, 4, 1 };	// (fishing: the old rod's 2, the good rod's 3, the super rod's 5)
		for (size_t a = wildHeaders; a + 20 <= n && r[a] != 0xff; a += 20)
		{
			if (r[a] != group || r[a + 1] != num) continue;
			long info = romPtr(r, n, a + 4 + std::min(method, 3) * 4, 8);
			if (info < 0) return out;
			int slots = method == 0 ? 12 : method >= 3 ? 10 : 5;
			long mons = romPtr(r, n, info + 4, 4 * (size_t)slots);
			if (mons < 0) return out;
			// (a rod: its own slots of the fishing table, its chances adding up to 100 as the game rolls them)
			int from = method == 4 ? 2 : method == 5 ? 5 : 0, to = method == 3 ? 2 : method == 4 ? 5 : slots;
			for (int i = from; i < to; i++)
			{
				const uint8_t* m = r + mons + i * 4;
				uint16_t sp = le16(m + 2);
				if (!sp || sp >= numSpecies) continue;
				int pc = method == 0 ? GRASS[i] : method >= 3 ? FISH[i] : WATER5[i];
				auto it = std::find_if(out.begin(), out.end(), [&](const Wild& w) { return w.species == sp; });
				if (it == out.end()) out.push_back({ sp, m[0], m[1], pc });
				else { it->minLv = std::min(it->minLv, m[0]); it->maxLv = std::max(it->maxLv, m[1]); it->percent += pc; }
			}
			std::stable_sort(out.begin(), out.end(), [](const Wild& x, const Wild& y) { return x.percent > y.percent; });
			return out;
		}
		return out;
	}

	std::vector<RomTables::TeamMon> RomTables::team(const std::string& name, int* cls) const
	{
		if (!rom) return {};
		const uint8_t* r = rom->data();
		size_t n = rom->size();
		// every record of that trainer: the battle met first is among those of the lowest levels (rematches are far
		// higher: Emerald's, a hack's post-game ones at 80 and more); of those, one not in a run of records of the same
		// name (a hack's difficulty variants of one battle: Pokémon Unbound keeps three in a row, its own elsewhere),
		// and the one with the most Pokémon (Unbound's Tessy: a story battle of three, her gym's of five)
		struct Rec { long a; std::vector<TeamMon> team; int top; bool run; };
		std::vector<Rec> recs;
		for (long a = 0; (a = findTrainer(r, n, name, cls ? *cls : -1, (size_t)a + 8)) >= 0; )
		{
			Rec x{ a, teamAt(r, n, a, numSpecies), 0, false };
			if (x.team.empty()) continue;
			for (auto& m : x.team) x.top = std::max(x.top, (int)m.level);
			for (long b : { a - 40, a + 40 })
				x.run |= b >= 0 && (size_t)b + 40 <= n && r[b + 1] == r[a + 1] && memcmp(r + b + 4, r + a + 4, 12) == 0 && trainerAt(r, n, b);
			recs.push_back(std::move(x));
		}
		if (recs.empty()) return {};
		int low = 999;
		bool single = false;
		for (auto& x : recs) low = std::min(low, x.top);
		for (auto& x : recs) single |= x.top <= low + 10 && !x.run;
		const Rec* best = nullptr;
		for (auto& x : recs)
			if (x.top <= low + 10 && (!single || !x.run) && (!best || x.team.size() > best->team.size())) best = &x;
		if (cls) *cls = r[best->a + 1];
		return best->team;
	}

	bool RomTables::layoutSize(uint32_t p, int& w, int& h) const
	{
		if (!rom || p < 0x08000000u || p - 0x08000000u + 8 > rom->size()) return false;
		const uint8_t* r = rom->data() + (p - 0x08000000u);
		w = (int)le32(r); h = (int)le32(r + 4);
		return w > 0 && h > 0 && w < 1024 && h < 1024;
	}

	int RomTables::gender(uint16_t species, uint32_t pid) const
	{
		if (species >= genderRatio.size()) return 2;
		uint8_t r = genderRatio[species];
		if (r == 255) return 2;
		if (r == 254) return 1;
		if (r == 0) return 0;
		return (pid & 0xff) < r ? 1 : 0;
	}

	long long RomTables::expFor(uint16_t species, int level) const
	{
		if (species >= growth.size() || growth[species] >= expTable.size() || level < 0 || level > 100) return -1;
		return expTable[growth[species]][level];
	}

	bool RomTables::iconRGBA(uint16_t species, bool egg, std::vector<uint8_t>& rgba) const
	{
		if (egg) species = 412;
		if (species >= icons.size() || species >= iconPal.size() || iconPal[species] >= palettes.size()) return false;
		const auto& pal = palettes[iconPal[species]];
		rgba.resize(32 * 32 * 4);
		for (int i = 0; i < 32 * 32; i++)
		{
			uint32_t c = pal[icons[species][i]];
			rgba[i * 4] = (uint8_t)c; rgba[i * 4 + 1] = (uint8_t)(c >> 8); rgba[i * 4 + 2] = (uint8_t)(c >> 16); rgba[i * 4 + 3] = (uint8_t)(c >> 24);
		}
		return true;
	}

	bool parseReply(const std::string& line, uint32_t& addr, std::vector<uint8_t>& bytes)
	{
		static const char P[] = "READ_CORE_MEMORY ";
		if (line.compare(0, sizeof P - 1, P) != 0) return false;
		const char* s = line.c_str() + sizeof P - 1;
		char* e;
		unsigned long a = strtoul(s, &e, 16);
		if (e == s) return false;
		addr = (uint32_t)a;
		bytes.clear();
		s = e;
		while (*s == ' ') s++;
		if (*s == '-') return true;	// "-1 no memory map defined"
		while (*s)
		{
			unsigned long v = strtoul(s, &e, 16);
			if (e == s || v > 0xff) break;
			bytes.push_back((uint8_t)v);
			s = e;
			while (*s == ' ') s++;
		}
		return true;
	}

	namespace
	{
		bool endsWithI(const std::string& s, const char* suf)
		{
			size_t n = strlen(suf);
			if (s.size() < n) return false;
			for (size_t i = 0; i < n; i++) if (tolower((unsigned char)s[s.size() - n + i]) != suf[i]) return false;
			return true;
		}
		// the ROM's bytes (limit: the first that many, enough for the header), from the file or the .gba in a .zip
		std::vector<uint8_t> readRom(const std::string& path, size_t limit)
		{
			std::vector<uint8_t> out;
			if (endsWithI(path, ".zip"))
			{
				readZip(path, [](const std::vector<rnds::ZipMember>& ms)
				{
					for (size_t i = 0; i < ms.size(); i++) if (endsWithI(ms[i].name, ".gba")) return (int)i;
					return ms.empty() ? -1 : 0;
				}, [&](const uint8_t* p, size_t n)
				{
					out.insert(out.end(), p, p + std::min(n, limit - out.size()));
					return out.size() < limit;
				});
				return out;
			}
			FILE* f = fopen(path.c_str(), "rb");
			if (!f) return out;
			if (fseeko(f, 0, SEEK_END) == 0)
			{
				long long n = ftello(f);
				if (n > 0) out.resize((size_t)std::min<long long>(n, (long long)limit));
			}
			if (fseeko(f, 0, SEEK_SET) != 0 || fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
			fclose(f);
			return out;
		}
	}

	bool Companion::open(const std::string& rom)
	{
		close();
		std::vector<uint8_t> h = readRom(rom, 0xc0);
		info = detectGame(h.data(), h.size());
		// (a code with releases told by their CRC-32: which one it is once the whole ROM is read, nothing read till then;
		// a guess meanwhile, which may turn out none of them)
		byCrc = crcMatters(h.data(), h.size());
		if (byCrc && !info) info = anyWithCode(h.data(), h.size());
		if (!info) return false;
		sh = std::make_shared<Shared>();
		std::shared_ptr<Shared> s = sh;
		bool crc = byCrc;
		std::thread([s, rom, h, crc]
		{
			setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);	// (this thread only: beside the game starting)
			std::vector<uint8_t> all = readRom(rom, 32u << 20);
			const GameProfile* profile = crc ? detectGame(h.data(), h.size(), (uint32_t)crc32(0L, all.data(), (uInt)all.size())) : nullptr;
			auto t = std::make_shared<RomTables>(findTables(all.data(), all.size(), profile));
			size_t bytes = all.size();
			t->rom = std::make_shared<const std::vector<uint8_t>>(std::move(all));
			fprintf(stderr, "pokemon: %zu species, %zu moves, %zu species types, %zu move records, %zu icons (%zu palette numbers, %zu palettes), "
				"%zu growth rates from the ROM (%zu bytes); %s, %zu abilities, %zu items, %zu map names, %zu region maps, %zu dex numbers, "
				"dex entries %s, wild table %s, %zu CFRU gym leaders\n", t->species.size(), t->moves.size(), t->speciesTypes.size(), t->moveInfo.size(),
				t->icons.size(), t->iconPal.size(), t->palettes.size(), t->expTable.size(), bytes, t->has.modernTypes ? "modern types" : "the games' types",
				t->abilities.size(), t->items.size(), t->mapNames.size(), t->grids.size(), t->natDex.size(), t->dexEntries >= 0 ? "found" : "none",
				t->wildHeaders >= 0 ? "found" : "none", t->leaders.size());
			std::lock_guard<std::mutex> l(s->mu);
			s->tables = t;
			s->profile = profile;
			s->ready = true;
		}).detach();
		return true;
	}

	void Companion::close()
	{
		info = nullptr;
		byCrc = false;
		sh.reset();
		tablesSeen = false;
		nextAt = sentAt = 0;
		errorSince = -1;
		waiting = 0;
		part.clear();
		snap = next = Snapshot();
		changed = false;
	}

	void Companion::setTables(RomTables t)
	{
		sh = std::make_shared<Shared>();
		sh->tables = std::make_shared<RomTables>(std::move(t));
		sh->ready = true;
	}

	std::shared_ptr<const RomTables> Companion::tables() const
	{
		if (!sh) return nullptr;
		std::lock_guard<std::mutex> l(sh->mu);
		return sh->tables;
	}

	std::string Companion::requests(double now)
	{
		if (!info) return "";
		if (!tablesSeen && sh)
		{
			std::lock_guard<std::mutex> l(sh->mu);
			if (sh->ready)
			{
				tablesSeen = true; changed = true;
				if (byCrc && sh->profile) { info = sh->profile; byCrc = false; }		// (before any read: nothing to forget)
				else if (byCrc) snap.unknownRelease = true;
			}
		}
		if (byCrc) return "";
		if (now < nextAt || (waiting > 0 && now - sentAt < 10000)) return "";
		count.clear(); party.clear(); flags.clear(); mainB.clear(); battle.clear(); enemy.clear();
		bagPtrB.clear(); for (auto& b : pocketB) b.clear();
		sbp.clear(); sb1h.clear(); mh.clear(); bagB.clear(); moneyB.clear(); keyB.clear(); dexB.clear(); flagB.clear(); nameB.clear();
		next = Snapshot();
		wantBattle = snap.inBattle;
		wants.clear();
		wants.push_back({ info->mainFlags, 1, &mainB, false });
		wants.push_back({ info->partyCount, 1, &count, false });
		wants.push_back({ info->party, 6 * info->mon.size, &party, false });
		if (wantBattle)
		{
			wants.push_back({ info->battleFlags, 4, &flags, false });
			wants.push_back({ info->battleMons, 4 * info->mon.battler.size, &battle, false });
			wants.push_back({ info->enemyParty, 6 * info->mon.size, &enemy, false });
		}
		// the save's data for the other pages: Ruby and Sapphire keep it in one place; the others move it on every
		// map change, behind two pointers read in the same set (the data read at last set's addresses counts only
		// when they haven't moved)
		wantSave = ext;
		std::shared_ptr<const RomTables> tb = ext ? tables() : nullptr;
		const RomTables::Features* has = tb ? &tb->has : nullptr;
		dexOwn = has && has->dexRam ? has->dexBytes : 0;
		bagPockets = has ? has->bagPockets : 0;
		if (ext)
		{
			bool fixed = info->fixedSave;
			if (!fixed) wants.push_back({ info->sb1, 8, &sbp, false });
			uint32_t s1 = fixed ? info->sb1 : sbSeen[0], s2 = fixed ? info->sb2 : sbSeen[1];
			sbUsed[0] = s1; sbUsed[1] = s2;
			wants.push_back({ info->mapHeader, 0x1c, &mh, false });
			if (s1 >> 24 == 2 && s2 >> 24 == 2)
			{
				const SaveLayout& L = info->save;
				wants.push_back({ s1, 12, &sb1h, false });
				uint32_t b0 = L.pocket[0], b1 = L.pocket[4] + 4 * L.count[4];
				if (!bagPockets) wants.push_back({ s1 + b0, (int)(b1 - b0), &bagB, false });
				wants.push_back({ s1 + L.money, 4, &moneyB, false });
				if (L.key) wants.push_back({ s2 + L.key, 4, &keyB, false });
				if (!dexOwn) wants.push_back({ s2 + L.dexOwned, (int)DEX_LEN, &dexB, false });
				wants.push_back({ s1 + L.flags + 0x100, 16, &flagB, false });
				wants.push_back({ s2, 8, &nameB, false });
			}
			// the game's own Pokédex flags (seen, then caught), at a fixed address (a CFRU hack's)
			if (dexOwn) wants.push_back({ has->dexRam, 2 * dexOwn, &dexB, false });
			// the game's own bag (a CFRU hack's): gBagPockets, and the pockets where the last read of it pointed (both in
			// this set: the pockets count when the pointers haven't moved)
			if (bagPockets)
			{
				wants.push_back({ bagPockets, 40, &bagPtrB, false });
				for (int k = 0; k < 5; k++)
					if (bagSeen[k] >> 24 == 2 && bagCap[k] > 0) wants.push_back({ bagSeen[k], 4 * bagCap[k], &pocketB[k], false });
			}
		}
		std::string out;
		char b[64];
		for (auto& w : wants) { snprintf(b, sizeof b, "READ_CORE_MEMORY %x %d\n", w.addr, w.len); out += b; }
		waiting = (int)wants.size();
		sentAt = now;
		nextAt = now + 250;
		return out;
	}

	void Companion::feed(const char* data, size_t n)
	{
		part.append(data, n);
		size_t nl;
		while ((nl = part.find('\n')) != std::string::npos)
		{
			std::string ln = part.substr(0, nl);
			part.erase(0, nl + 1);
			uint32_t a;
			std::vector<uint8_t> v;
			if (!info || !parseReply(ln, a, v)) continue;
			if (v.empty() && next.error.empty())
			{
				size_t at = ln.find("-1");
				next.error = at == std::string::npos ? "no data" : ln.substr(std::min(ln.size(), at + 3));
			}
			auto w = std::find_if(wants.begin(), wants.end(), [&](const Want& x) { return x.addr == a && x.into && x.into->empty() && !x.done; });
			if (w == wants.end()) continue;
			*w->into = v;
			w->done = true;
			if (waiting > 0 && --waiting == 0) finish();
		}
		if (part.size() > 65536) part.clear();	// (a line with no end: not ours)
	}

	void Companion::saveData(Snapshot& s)
	{
		// (kept from the last set when this one didn't read them, or read them where the save no longer is)
		auto keep = [&] { s.ext = snap.ext; s.player = snap.player; s.group = snap.group; s.num = snap.num; s.x = snap.x; s.y = snap.y;
			s.mapsec = snap.mapsec; s.layout = snap.layout; s.money = snap.money; for (int i = 0; i < 5; i++) s.pockets[i] = snap.pockets[i];
			s.bag = snap.bag; s.dexOwned = snap.dexOwned; s.dexSeen = snap.dexSeen; s.badges = snap.badges; s.champion = snap.champion; };
		if (!wantSave) { keep(); return; }
		if (!info->fixedSave && sbp.size() >= 8)
		{
			uint32_t a = le32(sbp.data()), b = le32(sbp.data() + 4);
			bool moved = a != sbUsed[0] || b != sbUsed[1];
			sbSeen[0] = a; sbSeen[1] = b;
			if (moved) { keep(); nextAt = 0; return; }	// (asked again at once, at the new addresses)
		}
		const SaveLayout& L = info->save;
		s.ext = true;
		if (mh.size() >= 0x1c && mh[3] == 0x08) { s.mapsec = mh[0x14]; s.layout = le32(mh.data()); }	// (its first word a ROM pointer: it is a map header)
		else s.mapsec = -1;
		if (sb1h.size() >= 12) { s.group = sb1h[4]; s.num = sb1h[5]; s.x = (int16_t)le16(sb1h.data()); s.y = (int16_t)le16(sb1h.data() + 2); }
		if (nameB.size() >= 8) s.player = decodeText(nameB.data(), 8);
		uint32_t key = keyB.size() >= 4 ? le32(keyB.data()) : 0;
		if (moneyB.size() >= 4) s.money = (long)(le32(moneyB.data()) ^ key);
		uint32_t b0 = L.pocket[0];
		if (bagPockets)
		{
			// (the pointers and sizes read now; the pockets read in this set count when they were read where these point)
			bool same = bagPtrB.size() >= 40;
			if (same)
				for (int k = 0; k < 5; k++)
				{
					uint32_t a = le32(bagPtrB.data() + 8 * k), c = le32(bagPtrB.data() + 8 * k + 4);
					if (a >> 24 != 2 || c == 0 || c > 1000 || (a & 0xffffff) + 4 * c > 0x40000) { a = 0; c = 0; }
					if (a != bagSeen[k] || (int)c != bagCap[k]) { same = false; nextAt = 0; }	// (asked again at once)
					bagSeen[k] = a; bagCap[k] = (int)c;
				}
			bool all = same;
			for (int k = 0; k < 5 && all; k++) if ((int)pocketB[k].size() < 4 * bagCap[k]) all = false;
			if (all)
			{
				s.bag = true;
				for (int k = 0; k < 5; k++)
					for (int i = 0; i < bagCap[k]; i++)
					{
						const uint8_t* e = pocketB[k].data() + i * 4;
						uint16_t id = le16(e), c = le16(e + 2);
						if (id && c && c <= 999) s.pockets[k].push_back({ id, c });
					}
			}
			else { s.bag = snap.bag; for (int k = 0; k < 5; k++) s.pockets[k] = snap.pockets[k]; }
		}
		else if (bagB.size() >= L.pocket[4] + 4 * L.count[4] - b0)
		{
			s.bag = true;
			for (int k = 0; k < 5; k++)
				for (int i = 0; i < L.count[k]; i++)
				{
					const uint8_t* e = bagB.data() + L.pocket[k] - b0 + i * 4;
					uint16_t id = le16(e), c = (uint16_t)(le16(e + 2) ^ (key & 0xffff));
					if (id && c && c <= 999) s.pockets[k].push_back({ id, c });
				}
		}
		if (dexOwn > 0)
		{
			if (dexB.size() >= 2 * (size_t)dexOwn) { s.dexSeen.assign(dexB.begin(), dexB.begin() + dexOwn); s.dexOwned.assign(dexB.begin() + dexOwn, dexB.begin() + 2 * dexOwn); }
		}
		else if (dexB.size() >= DEX_LEN) { s.dexOwned.assign(dexB.begin(), dexB.begin() + 52); s.dexSeen.assign(dexB.begin() + 0x34, dexB.begin() + 0x68); }
		if (flagB.size() >= 16)
		{
			auto flag = [&](int f) { int o = f / 8 - 0x100; return o >= 0 && o < 16 && (flagB[o] >> (f % 8) & 1); };
			s.badges = 0;
			for (int i = 0; i < 8; i++) if (flag(L.badge0 + i)) s.badges |= 1 << i;
			s.champion = flag(L.clear);
		}
	}

	void Companion::finish()
	{
		Snapshot s = next;
		s.seen = !mainB.empty() && !party.empty();
		std::shared_ptr<const RomTables> t = tables();
		int maxSpecies = t && t->numSpecies > 412 ? t->numSpecies - 1 : 450;
		auto typed = [&](Mon& m)
		{
			if (m.types[0] == 255 && t && m.species < t->speciesTypes.size())
			{
				m.types[0] = t->speciesTypes[m.species][0];
				m.types[1] = t->speciesTypes[m.species][1];
			}
		};
		if (s.seen)
		{
			s.inBattle = mainB[0] & 2;
			// (the profile says how the game keeps its Pokémon, the tables whether a hack's patches took the encryption
			// out; until they are read, either)
			MonLayout L = info->mon;
			if (!t) L.coding = MonCoding::EITHER;
			else if (t->has.plainMons) L.coding = MonCoding::PLAIN;
			const size_t PARTY = L.size, BATTLER = L.battler.size;
			int c = count.empty() ? 6 : std::min<int>(count[0], 6);
			for (int i = 0; i < c && (i + 1) * PARTY <= party.size(); i++)
			{
				Mon m;
				if (decodeParty(party.data() + i * PARTY, m, L)) { typed(m); s.party.push_back(m); }
			}
			if (s.inBattle && wantBattle && flags.size() >= 4 && battle.size() >= 4 * BATTLER)
			{
				s.doubles = flags[0] & 1;
				decodeBattler(battle.data(), s.lead, maxSpecies, L.battler);
				for (int b : { 1, 3 })
				{
					if (b == 3 && !s.doubles) break;
					Mon m;
					if (decodeBattler(battle.data() + b * BATTLER, m, maxSpecies, L.battler)) s.foes.push_back(m);
				}
				for (int i = 0; i < 6 && (i + 1) * PARTY <= enemy.size(); i++)
				{
					Mon m;
					if (decodeParty(enemy.data() + i * PARTY, m, L)) s.foeParty.push_back(m);
				}
			}
			else if (s.inBattle) { s.lead = snap.lead; s.foes = snap.foes; s.doubles = snap.doubles; s.foeParty = snap.foeParty; }	// (read in the next set)
			saveData(s);
			s.error.clear();
			errorSince = -1;
		}
		else if (!s.error.empty())
		{
			// said once it has lasted 5 s (the first sets can come before the game is loaded)
			if (errorSince < 0) errorSince = sentAt;
			if (sentAt - errorSince < 5000) s.error.clear();
		}
		// changed: anything the panel shows
		auto key = [](const Snapshot& x)
		{
			std::string k = std::to_string(x.seen) + x.error + std::to_string(x.inBattle) + std::to_string(x.doubles);
			auto one = [&](const Mon& m)
			{
				k += m.nick + "|" + std::to_string(m.pid) + "," + std::to_string(m.exp) + "," + std::to_string(m.item) + "," + std::to_string(m.species) + "," + std::to_string(m.level) + "," + std::to_string(m.hp) + "," +
					std::to_string(m.maxHp) + "," + std::to_string(m.status) + "," + std::to_string(m.types[0]) + "," + std::to_string(m.types[1]);
				for (int i = 0; i < 4; i++) k += "," + std::to_string(m.moves[i]) + ":" + std::to_string(m.pp[i]);
				for (int i = 0; i < 5; i++) k += "," + std::to_string(m.stats[i]);
				for (int i = 0; i < 6; i++) k += "," + std::to_string(m.ivs[i]) + ":" + std::to_string(m.evs[i]);
				k += "," + std::to_string(m.friendship) + "," + std::to_string(m.abilitySlot) + ";";
			};
			for (auto& m : x.party) one(m);
			one(x.lead);
			for (auto& m : x.foes) one(m);
			k += "/";
			for (auto& m : x.foeParty) k += std::to_string(m.species) + "," + std::to_string(m.hp) + ";";
			k += "/" + std::to_string(x.ext) + x.player + "," + std::to_string(x.group) + "," + std::to_string(x.num) + "," + std::to_string(x.x) + "," +
				std::to_string(x.y) + "," + std::to_string(x.mapsec) + "," + std::to_string(x.money) + "," + std::to_string(x.badges) + std::to_string(x.champion);
			for (auto& p : x.pockets) { k += "|"; for (auto& e : p) k += std::to_string(e.item) + "x" + std::to_string(e.count) + ","; }
			k.append(x.dexOwned.begin(), x.dexOwned.end()); k.append(x.dexSeen.begin(), x.dexSeen.end());
			return k;
		};
		if (key(s) != key(snap)) changed = true;
		snap = s;
	}
}
