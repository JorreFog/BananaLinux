// banana-ui: the DS Pokémon companion's shared helpers (model.h): the game in a ROM header, the type chart of the DS
// games (Generations IV and V: no Fairy; Steel resists Ghost and Dark), a Pokémon's shininess and Hidden Power.
#include "nds/model.h"
#include <atomic>
#include <algorithm>
#include <iterator>
#include <cstring>

namespace banana::nds
{
	Game detectGame(const uint8_t* h, size_t n)
	{
		Game g;
		if (!h || n < 0x200) return g;
		g.code.assign((const char*)h + 0x0c, 4);
		g.revision = h[0x1e];
		g.dsiEnhanced = (h[0x12] & 0x02) != 0;	// unit code: 2 = DS + DSi
		static const struct { const char* c; GameId id; Gen gen; const char* t; } G[] = {
			{ "ADA", GameId::DIAMOND, Gen::IV, "Diamond" }, { "APA", GameId::PEARL, Gen::IV, "Pearl" },
			{ "CPU", GameId::PLATINUM, Gen::IV, "Platinum" }, { "IPK", GameId::HEARTGOLD, Gen::IV, "HeartGold" },
			{ "IPG", GameId::SOULSILVER, Gen::IV, "SoulSilver" }, { "IRB", GameId::BLACK, Gen::V, "Black" },
			{ "IRA", GameId::WHITE, Gen::V, "White" }, { "IRE", GameId::BLACK2, Gen::V, "Black 2" },
			{ "IRD", GameId::WHITE2, Gen::V, "White 2" } };
		// the game by its first three letters, any revision (header 0x1E: the RAM reader finds and checks its places,
		// ram.h); the language by the fourth, from the releases whose text banks the ROM reader was checked on. To add
		// one (D German, F French, I Italian, S Spanish, J Japanese, K Korean): check rom.cpp's text banks and names on
		// that ROM (tests/nds-rom-test), then list its letter here
		static const char CHECKED_LANGUAGES[] = {
			'E',	// USA, and the English European dumps that carry it
			'O',	// Black 2/White 2's international English code
		};
		const char lang = g.code.size() == 4 ? g.code[3] : 0;
		if (std::find(std::begin(CHECKED_LANGUAGES), std::end(CHECKED_LANGUAGES), lang) == std::end(CHECKED_LANGUAGES)) return g;
		for (auto& e : G)
			if (!g.code.compare(0, 3, e.c))
			{
				g.id = e.id; g.gen = e.gen; g.title = e.t;
				g.dexSize = e.gen == Gen::IV ? 493 : 649;
				return g;
			}
		return g;
	}

	// attacker x defender in quarters (4 = normal); only the cells that differ from 4 are listed
	static const struct { Type a, d; uint8_t q; } CHART[] = {
		{ NORMAL, ROCK, 2 }, { NORMAL, GHOST, 0 }, { NORMAL, STEEL, 2 },
		{ FIGHTING, NORMAL, 8 }, { FIGHTING, FLYING, 2 }, { FIGHTING, POISON, 2 }, { FIGHTING, ROCK, 8 }, { FIGHTING, BUG, 2 },
		{ FIGHTING, GHOST, 0 }, { FIGHTING, STEEL, 8 }, { FIGHTING, PSYCHIC, 2 }, { FIGHTING, ICE, 8 }, { FIGHTING, DARK, 8 },
		{ FLYING, FIGHTING, 8 }, { FLYING, ROCK, 2 }, { FLYING, BUG, 8 }, { FLYING, STEEL, 2 }, { FLYING, GRASS, 8 }, { FLYING, ELECTRIC, 2 },
		{ POISON, POISON, 2 }, { POISON, GROUND, 2 }, { POISON, ROCK, 2 }, { POISON, GHOST, 2 }, { POISON, STEEL, 0 }, { POISON, GRASS, 8 },
		{ GROUND, FLYING, 0 }, { GROUND, POISON, 8 }, { GROUND, ROCK, 8 }, { GROUND, BUG, 2 }, { GROUND, STEEL, 8 }, { GROUND, FIRE, 8 },
		{ GROUND, GRASS, 2 }, { GROUND, ELECTRIC, 8 },
		{ ROCK, FIGHTING, 2 }, { ROCK, FLYING, 8 }, { ROCK, GROUND, 2 }, { ROCK, BUG, 8 }, { ROCK, STEEL, 2 }, { ROCK, FIRE, 8 }, { ROCK, ICE, 8 },
		{ BUG, FIGHTING, 2 }, { BUG, FLYING, 2 }, { BUG, POISON, 2 }, { BUG, GHOST, 2 }, { BUG, STEEL, 2 }, { BUG, FIRE, 2 }, { BUG, GRASS, 8 },
		{ BUG, PSYCHIC, 8 }, { BUG, DARK, 8 },
		{ GHOST, NORMAL, 0 }, { GHOST, GHOST, 8 }, { GHOST, STEEL, 2 }, { GHOST, PSYCHIC, 8 }, { GHOST, DARK, 2 },
		{ STEEL, ROCK, 8 }, { STEEL, STEEL, 2 }, { STEEL, FIRE, 2 }, { STEEL, WATER, 2 }, { STEEL, ELECTRIC, 2 }, { STEEL, ICE, 8 },
		{ FIRE, ROCK, 2 }, { FIRE, BUG, 8 }, { FIRE, STEEL, 8 }, { FIRE, FIRE, 2 }, { FIRE, WATER, 2 }, { FIRE, GRASS, 8 }, { FIRE, ICE, 8 },
		{ FIRE, DRAGON, 2 },
		{ WATER, GROUND, 8 }, { WATER, ROCK, 8 }, { WATER, FIRE, 8 }, { WATER, WATER, 2 }, { WATER, GRASS, 2 }, { WATER, DRAGON, 2 },
		{ GRASS, FLYING, 2 }, { GRASS, POISON, 2 }, { GRASS, GROUND, 8 }, { GRASS, ROCK, 8 }, { GRASS, BUG, 2 }, { GRASS, STEEL, 2 },
		{ GRASS, FIRE, 2 }, { GRASS, WATER, 8 }, { GRASS, GRASS, 2 }, { GRASS, DRAGON, 2 },
		{ ELECTRIC, FLYING, 8 }, { ELECTRIC, GROUND, 0 }, { ELECTRIC, WATER, 8 }, { ELECTRIC, GRASS, 2 }, { ELECTRIC, ELECTRIC, 2 },
		{ ELECTRIC, DRAGON, 2 },
		{ PSYCHIC, FIGHTING, 8 }, { PSYCHIC, POISON, 8 }, { PSYCHIC, STEEL, 2 }, { PSYCHIC, PSYCHIC, 2 }, { PSYCHIC, DARK, 0 },
		{ ICE, FLYING, 8 }, { ICE, GROUND, 8 }, { ICE, STEEL, 2 }, { ICE, FIRE, 2 }, { ICE, WATER, 2 }, { ICE, GRASS, 8 }, { ICE, ICE, 2 },
		{ ICE, DRAGON, 8 },
		{ DRAGON, STEEL, 2 }, { DRAGON, DRAGON, 8 },
		{ DARK, FIGHTING, 2 }, { DARK, GHOST, 8 }, { DARK, STEEL, 2 }, { DARK, PSYCHIC, 8 }, { DARK, DARK, 2 },
	};
	// Generation VI's changes: Fairy, and Steel no longer resisting Ghost and Dark
	static const struct { Type a, d; uint8_t q; } FAIRY_CHART[] = {
		{ FAIRY, FIGHTING, 8 }, { FAIRY, DRAGON, 8 }, { FAIRY, DARK, 8 }, { FAIRY, POISON, 2 }, { FAIRY, STEEL, 2 }, { FAIRY, FIRE, 2 },
		{ FIGHTING, FAIRY, 2 }, { BUG, FAIRY, 2 }, { DARK, FAIRY, 2 }, { DRAGON, FAIRY, 0 }, { POISON, FAIRY, 8 }, { STEEL, FAIRY, 8 },
	};
	static std::atomic<bool> modernChart{ false };
	void useModernTypeChart(bool on) { modernChart = on; }
	bool modernTypeChart() { return modernChart; }
	int numTypes() { return modernChart ? (int)NUM_TYPES : (int)FAIRY; }
	static int one(Type a, Type d)
	{
		const bool m = modernChart;
		if (a >= NUM_TYPES || d >= NUM_TYPES) return 4;
		if (a == FAIRY || d == FAIRY)
		{
			if (!m) return 4;
			for (auto& c : FAIRY_CHART) if (c.a == a && c.d == d) return c.q;
			return 4;
		}
		if (m && d == STEEL && (a == GHOST || a == DARK)) return 4;
		for (auto& c : CHART) if (c.a == a && c.d == d) return c.q;
		return 4;
	}
	int effectiveness(Type atk, Type d1, Type d2)
	{
		int q = one(atk, d1);
		if (d2 != NO_TYPE && d2 != d1) q = q * one(atk, d2) / 4;
		return q;
	}

	// shiny: (TID ^ SID ^ PID high ^ PID low) < 8 in Generations IV and V
	bool Mon::shiny() const { return ((otId ^ otSecret ^ (pid >> 16) ^ (pid & 0xffff)) & 0xffff) < 8; }

	// Hidden Power from the IVs' lowest and second-lowest bits (HP, Atk, Def, Speed, SpA, SpD), Generations III-V:
	// type = (bits * 15 / 63) over Fighting..Dark (Normal never), power 30..70
	int Mon::hiddenPowerType() const
	{
		int a = 0;
		for (int i = 0; i < 6; i++) a |= (ivs[i] & 1) << i;
		static const Type T[16] = { FIGHTING, FLYING, POISON, GROUND, ROCK, BUG, GHOST, STEEL, FIRE, WATER, GRASS,
			ELECTRIC, PSYCHIC, ICE, DRAGON, DARK };
		return T[a * 15 / 63];
	}
	int Mon::hiddenPowerPower() const
	{
		int b = 0;
		for (int i = 0; i < 6; i++) b |= ((ivs[i] >> 1) & 1) << i;
		return b * 40 / 63 + 30;
	}

	bool sameArea(const RomData* rom, uint16_t a, uint16_t b)
	{
		if (a == b) return true;
		if (!rom || !a || !b) return false;
		auto x = rom->zones.find(a), y = rom->zones.find(b);
		return x != rom->zones.end() && y != rom->zones.end() && !x->second.name.empty() && x->second.name == y->second.name;
	}
}
