#pragma once
// banana-ui tests: made-up data for the DS Pokémon companion's pages (nds/pages.h), shaped like what the ROM reader,
// the RAM reader, the guide loader and the logic give them. Two playthroughs: Platinum (Generation IV, at Eterna Gym
// with one badge) and Black 2 (Generation V, at Floccesy Ranch, challenge mode on). Species, moves and places are the
// games' facts written by hand for the test; the sprites are drawn here from a seed (pixel creatures in the species'
// type colours), never the games' own; the Pokédex lines, guide text and advice are ours.
#include "nds/pages.h"
#include <cmath>
#include <map>
#include <memory>
#include <mutex>

namespace ndsfx
{
	using namespace banana::nds;

	// ---- sprites made from a seed ------------------------------------------------------------------------------------
	inline uint32_t hsv(float h, float s, float v)
	{
		h = std::fmod(h + 1.0f, 1.0f) * 6;
		int i = (int)h; float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
		float r, g, b;
		switch (i) { case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = t; break;
			case 3: r = p; g = q; b = v; break; case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q; }
		return ((uint32_t)(r * 255) << 16) | ((uint32_t)(g * 255) << 8) | (uint32_t)(b * 255);
	}
	inline void toHsv(uint32_t c, float& h, float& s, float& v)
	{
		float r = ((c >> 16) & 255) / 255.0f, g = ((c >> 8) & 255) / 255.0f, b = (c & 255) / 255.0f;
		float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), d = mx - mn;
		v = mx; s = mx > 0 ? d / mx : 0;
		if (d == 0) h = 0;
		else if (mx == r) h = std::fmod((g - b) / d / 6 + 1, 1.0f);
		else if (mx == g) h = ((b - r) / d + 2) / 6;
		else h = ((r - g) / d + 4) / 6;
	}
	inline std::array<uint32_t, 16> paletteFor(uint32_t a, uint32_t b, float hueShift)
	{
		float ha, sa, va, hb, sb, vb;
		toHsv(a, ha, sa, va); toHsv(b, hb, sb, vb);
		ha += hueShift; hb += hueShift;
		std::array<uint32_t, 16> p{};
		p[1] = hsv(ha, std::min(1.0f, sa + 0.2f), va * 0.28f);		// outline
		p[2] = hsv(ha, sa, va);										// body
		p[3] = hsv(ha, std::min(1.0f, sa + 0.12f), va * 0.72f);		// shade
		p[4] = hsv(ha, sa * 0.45f, std::min(1.0f, va * 1.12f + 0.08f));	// light
		p[5] = hsv(hb, sb, vb);										// second colour
		p[6] = hsv(hb, std::min(1.0f, sb + 0.1f), vb * 0.7f);
		p[7] = 0xf8f8f0; p[8] = 0x181820;								// eyes
		p[9] = hsv(ha, sa * 0.25f, 0.96f);							// belly
		return p;
	}
	struct Lcg { uint32_t s; float f() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; } };

	inline std::shared_ptr<Sprite> makeSprite(uint16_t seed, uint32_t c1, uint32_t c2, bool gen5)
	{
		auto sp = std::make_shared<Sprite>();
		int W = gen5 ? 96 : 80;
		sp->w = sp->h = W;
		Lcg r{ seed * 2654435761u + 7 };
		for (int i = 0; i < 3; i++) r.f();
		float br = W * (0.15f + 0.08f * r.f()), hr = br * (0.62f + 0.3f * r.f());
		float bodyY = W * 0.62f, headY = bodyY - br * (0.75f + 0.25f * r.f()) - hr * 0.6f;
		bool ears = r.f() > 0.35f, tail = r.f() > 0.4f, arms = r.f() > 0.3f, tallEars = r.f() > 0.5f, crest = r.f() > 0.7f;
		float earX = hr * (0.45f + 0.3f * r.f()), earR = hr * (0.28f + 0.15f * r.f());
		int nf = gen5 ? 6 : 2;
		static const float BOB5[6] = { 0, -1, -2, -3, -2, -1 };
		for (int f = 0; f < nf; f++)
		{
			float dy = gen5 ? BOB5[f] * W / 96.0f : (f ? -2.0f : 0.0f);
			bool blink = gen5 && f == 3;
			std::vector<uint8_t> px((size_t)W * W, 0);
			auto put = [&](float cx, float cy, float rad, uint8_t col, bool shade, float sy = 1)
			{
				for (int y = 0; y < W; y++)
					for (int x = 0; x < W; x++)
					{
						float ddx = (x + 0.5f - cx) / rad, ddy = (y + 0.5f - cy) / (rad * sy);
						if (ddx * ddx + ddy * ddy > 1) continue;
						uint8_t c = col;
						if (shade)
						{
							float lit = -0.6f * ddx - 0.75f * ddy, dark = 0.55f * ddx + 0.8f * ddy;
							if (dark > 0.5f) c = col == 2 ? 3 : 6;
							else if (lit > 0.62f && col == 2) c = 4;
						}
						px[(size_t)y * W + x] = c;
					}
			};
			float cx = W / 2.0f;
			if (tail) put(cx + br * 1.05f, bodyY + dy * 0.5f - br * 0.5f, br * 0.42f, 5, true, 1.4f);
			put(cx, bodyY + dy * 0.5f, br, 2, true, 0.92f);
			put(cx, bodyY + dy * 0.5f + br * 0.2f, br * 0.55f, 9, false, 0.85f);
			if (arms) { put(cx - br * 0.95f, bodyY + dy, br * 0.32f, 2, true); put(cx + br * 0.95f, bodyY + dy, br * 0.32f, 2, true); }
			put(cx - br * 0.5f, bodyY + br * 0.88f, br * 0.33f, 5, true, 0.7f);
			put(cx + br * 0.5f, bodyY + br * 0.88f, br * 0.33f, 5, true, 0.7f);
			if (ears)
			{
				float ey = headY + dy - hr * 0.8f;
				put(cx - earX, ey, earR, 5, true, tallEars ? 1.9f : 1.1f);
				put(cx + earX, ey, earR, 5, true, tallEars ? 1.9f : 1.1f);
			}
			if (crest) put(cx, headY + dy - hr * 1.0f, hr * 0.3f, 5, true, 1.6f);
			put(cx, headY + dy, hr, 2, true);
			// the outline: a filled pixel next to an empty one
			std::vector<uint8_t> out = px;
			for (int y = 0; y < W; y++)
				for (int x = 0; x < W; x++)
				{
					if (!px[(size_t)y * W + x]) continue;
					bool edge = x == 0 || y == 0 || x == W - 1 || y == W - 1 || !px[(size_t)y * W + x - 1] || !px[(size_t)y * W + x + 1] ||
						!px[(size_t)(y - 1) * W + x] || !px[(size_t)(y + 1) * W + x];
					if (edge) out[(size_t)y * W + x] = 1;
				}
			// the eyes
			int ey = (int)(headY + dy + hr * 0.05f), ex = (int)(hr * 0.42f), es = std::max(2, W / 32);
			for (int side = -1; side <= 1; side += 2)
				for (int yy = 0; yy < es + 2; yy++)
					for (int xx = 0; xx < es; xx++)
					{
						int x = (int)cx + side * ex + xx - es / 2, y = ey + yy - 1;
						if (x < 0 || y < 0 || x >= W || y >= W) continue;
						uint8_t c = blink ? (yy == es ? 1 : 0) : (yy < 1 ? 7 : 8);
						if (c) out[(size_t)y * W + x] = c;
					}
			sp->frames.push_back(out);
		}
		sp->normal = paletteFor(c1, c2, 0);
		sp->shiny = paletteFor(c1, c2, 0.38f);
		if (gen5) for (int f = 0; f < nf; f++) sp->anim.push_back({ (uint16_t)f, (uint16_t)(f == 3 ? 160 : 90) });
		else sp->anim = { { 0, 700 }, { 1, 300 } };
		return sp;
	}

	struct FixtureRom : RomData
	{
		mutable std::mutex mu;
		mutable std::map<uint32_t, std::shared_ptr<const Sprite>> made;
		std::shared_ptr<const Sprite> sprite(uint16_t n, bool female, uint8_t form) const override
		{
			std::lock_guard<std::mutex> l(mu);
			uint32_t key = n | (female << 16) | (form << 17);
			auto it = made.find(key);
			if (it != made.end()) return it->second;
			if (n == 0 || n >= species.size() || species[n].name.empty()) return nullptr;
			const Species& s = species[n];
			uint32_t c1 = typeColor(s.types[0]), c2 = s.types[1] != s.types[0] ? typeColor(s.types[1]) : (c1 ^ 0x303030);
			auto sp = makeSprite(n, c1, c2, game.gen == Gen::V);
			made[key] = sp;
			return sp;
		}
	};

	// ---- the tables ----------------------------------------------------------------------------------------------------
	inline std::vector<std::string> natures()
	{
		return { "Hardy", "Lonely", "Brave", "Adamant", "Naughty", "Bold", "Docile", "Relaxed", "Impish", "Lax", "Timid", "Hasty",
			"Serious", "Jolly", "Naive", "Modest", "Mild", "Quiet", "Bashful", "Rash", "Calm", "Gentle", "Sassy", "Careful", "Quirky" };
	}
	enum Ab { NOAB, BLAZE, IRON_FIST, NATURAL_CURE, POISON_POINT, INTIMIDATE, RIVALRY, SIMPLE, UNAWARE, SYNCHRONIZE, INNER_FOCUS,
		KEEN_EYE, OVERGROW, CHLOROPHYLL, STEADFAST, JUSTIFIED, PRANKSTER, SUPER_LUCK, PICKUP, VITAL_SPIRIT, THICK_FAT, HUGE_POWER,
		STATIC, DAMP, CONTRARY, BIG_PECKS, RUN_AWAY, SHED_SKIN, LEVITATE, SWARM, SHIELD_DUST, INSOMNIA, CUTE_CHARM, SAP_SIPPER,
		ROCK_HEAD, STURDY, MOLD_BREAKER, GUTS, CLOUD_NINE, FLOWER_GIFT, HONEY_GATHER, NUM_AB };
	inline std::vector<std::string> abilities()
	{
		return { "", "Blaze", "Iron Fist", "Natural Cure", "Poison Point", "Intimidate", "Rivalry", "Simple", "Unaware", "Synchronize",
			"Inner Focus", "Keen Eye", "Overgrow", "Chlorophyll", "Steadfast", "Justified", "Prankster", "Super Luck", "Pickup",
			"Vital Spirit", "Thick Fat", "Huge Power", "Static", "Damp", "Contrary", "Big Pecks", "Run Away", "Shed Skin", "Levitate",
			"Swarm", "Shield Dust", "Insomnia", "Cute Charm", "Sap Sipper", "Rock Head", "Sturdy", "Mold Breaker", "Guts",
			"Cloud Nine", "Flower Gift", "Honey Gather" };
	}
	// moves: our own numbering for the test (the ROM's are the game's)
	enum Mv { NOMOVE, SCRATCH, LEER, EMBER, TAUNT, MACH_PUNCH, FURY_SWIPES, FLAME_WHEEL, FEINT, CLOSE_COMBAT, FLARE_BLITZ,
		CALM_MIND, MAGICAL_LEAF, STUN_SPORE, GRASS_KNOT, LEECH_SEED, WING_ATTACK, QUICK_ATTACK, TACKLE, HEADBUTT, SPARK,
		CONFUSION, TELEPORT, ABSORB, POISON_STING, GROWTH, VINE_WHIP, WRAP, BITE, ROAR, FORCE_PALM, COUNTER, ENDURE, QUICK_GUARD,
		AERIAL_ACE, ROCK_TOMB, ASTONISH, LICK, SHADOW_BALL, NIGHT_SHADE, PSYBEAM, SAFEGUARD, WATER_GUN, MUD_SLAP, GUST,
		STRING_SHOT, HARDEN, POUND, SEED_BOMB, NUM_MV };
	inline void addMove(RomData& r, Mv id, const char* name, Type t, MoveCategory c, int pw, int acc, int pp)
	{
		if (r.moves.size() <= (size_t)id) r.moves.resize(id + 1);
		MoveData& m = r.moves[id];
		m.name = name; m.type = t; m.cat = c; m.power = (uint8_t)pw; m.accuracy = (uint8_t)acc; m.pp = (uint8_t)pp;
	}
	inline void moves(RomData& r)
	{
		auto P = MoveCategory::PHYSICAL, S = MoveCategory::SPECIAL, X = MoveCategory::STATUS;
		addMove(r, SCRATCH, "Scratch", NORMAL, P, 40, 100, 35); addMove(r, LEER, "Leer", NORMAL, X, 0, 100, 30);
		addMove(r, EMBER, "Ember", FIRE, S, 40, 100, 25); addMove(r, TAUNT, "Taunt", DARK, X, 0, 100, 20);
		addMove(r, MACH_PUNCH, "Mach Punch", FIGHTING, P, 40, 100, 30); addMove(r, FURY_SWIPES, "Fury Swipes", NORMAL, P, 18, 80, 15);
		addMove(r, FLAME_WHEEL, "Flame Wheel", FIRE, P, 60, 100, 25); addMove(r, FEINT, "Feint", NORMAL, P, 30, 100, 10);
		addMove(r, CLOSE_COMBAT, "Close Combat", FIGHTING, P, 120, 100, 5); addMove(r, FLARE_BLITZ, "Flare Blitz", FIRE, P, 120, 100, 15);
		addMove(r, CALM_MIND, "Calm Mind", PSYCHIC, X, 0, 0, 20); addMove(r, MAGICAL_LEAF, "Magical Leaf", GRASS, S, 60, 0, 20);
		addMove(r, STUN_SPORE, "Stun Spore", GRASS, X, 0, 75, 30); addMove(r, GRASS_KNOT, "Grass Knot", GRASS, S, 1, 100, 20);
		addMove(r, LEECH_SEED, "Leech Seed", GRASS, X, 0, 90, 10); addMove(r, WING_ATTACK, "Wing Attack", FLYING, P, 60, 100, 35);
		addMove(r, QUICK_ATTACK, "Quick Attack", NORMAL, P, 40, 100, 30); addMove(r, TACKLE, "Tackle", NORMAL, P, 50, 100, 35);
		addMove(r, HEADBUTT, "Headbutt", NORMAL, P, 70, 100, 15); addMove(r, SPARK, "Spark", ELECTRIC, P, 65, 100, 20);
		addMove(r, CONFUSION, "Confusion", PSYCHIC, S, 50, 100, 25); addMove(r, TELEPORT, "Teleport", PSYCHIC, X, 0, 0, 20);
		addMove(r, ABSORB, "Absorb", GRASS, S, 20, 100, 25); addMove(r, POISON_STING, "Poison Sting", POISON, P, 15, 100, 35);
		addMove(r, GROWTH, "Growth", NORMAL, X, 0, 0, 40); addMove(r, VINE_WHIP, "Vine Whip", GRASS, P, 35, 100, 15);
		addMove(r, WRAP, "Wrap", NORMAL, P, 15, 85, 20); addMove(r, BITE, "Bite", DARK, P, 60, 100, 25);
		addMove(r, ROAR, "Roar", NORMAL, X, 0, 100, 20); addMove(r, FORCE_PALM, "Force Palm", FIGHTING, P, 60, 100, 10);
		addMove(r, COUNTER, "Counter", FIGHTING, P, 1, 100, 20); addMove(r, ENDURE, "Endure", NORMAL, X, 0, 0, 10);
		addMove(r, QUICK_GUARD, "Quick Guard", FIGHTING, X, 0, 0, 15); addMove(r, AERIAL_ACE, "Aerial Ace", FLYING, P, 60, 0, 20);
		addMove(r, ROCK_TOMB, "Rock Tomb", ROCK, P, 50, 80, 10); addMove(r, ASTONISH, "Astonish", GHOST, P, 30, 100, 15);
		addMove(r, LICK, "Lick", GHOST, P, 20, 100, 30); addMove(r, SHADOW_BALL, "Shadow Ball", GHOST, S, 80, 100, 15);
		addMove(r, NIGHT_SHADE, "Night Shade", GHOST, S, 1, 100, 15); addMove(r, PSYBEAM, "Psybeam", PSYCHIC, S, 65, 100, 20);
		addMove(r, SAFEGUARD, "Safeguard", NORMAL, X, 0, 0, 25); addMove(r, WATER_GUN, "Water Gun", WATER, S, 40, 100, 25);
		addMove(r, MUD_SLAP, "Mud-Slap", GROUND, S, 20, 100, 10); addMove(r, GUST, "Gust", FLYING, S, 40, 100, 35);
		addMove(r, STRING_SHOT, "String Shot", BUG, X, 0, 95, 40); addMove(r, HARDEN, "Harden", NORMAL, X, 0, 0, 30);
		addMove(r, POUND, "Pound", NORMAL, P, 40, 100, 35); addMove(r, SEED_BOMB, "Seed Bomb", GRASS, P, 80, 100, 15);
	}
	enum It { NOITEM, POKE_BALL = 4, GREAT_BALL = 3, QUICK_BALL = 15, DUSK_BALL = 13, NET_BALL = 6, ORAN_BERRY = 155,
		MIRACLE_SEED = 239, CHARCOAL = 249, TM40 = 367, TM39 = 366, TM76 = 403, TM86 = 413, TM46 = 373, EVERSTONE = 229, NUM_IT = 420 };
	inline void items(RomData& r)
	{
		r.items.resize(NUM_IT);
		auto add = [&](It i, const char* n, int pocket) { r.items[i].name = n; r.items[i].pocket = (uint8_t)pocket; };
		add(POKE_BALL, "Poké Ball", 3); add(GREAT_BALL, "Great Ball", 3); add(QUICK_BALL, "Quick Ball", 3); add(DUSK_BALL, "Dusk Ball", 3);
		add(NET_BALL, "Net Ball", 3); add(ORAN_BERRY, "Oran Berry", 4); add(MIRACLE_SEED, "Miracle Seed", 1); add(CHARCOAL, "Charcoal", 1);
		add(TM40, "TM40 Aerial Ace", 2); add(TM39, "TM39 Rock Tomb", 2); add(TM76, "TM76 Stealth Rock", 2); add(TM86, "TM86 Grass Knot", 2);
		add(TM46, "TM46 Thief", 2); add(EVERSTONE, "Everstone", 1);
	}

	struct Sp
	{
		uint16_t n; const char* name; const char* cat; Type t1, t2; uint8_t b[6]; uint16_t ab[3]; uint8_t growth, catchRate, gender;
		uint8_t ev[6]; uint16_t height, weight; const char* text;
	};
	inline void species(RomData& r, const std::vector<Sp>& list)
	{
		r.species.resize(r.game.dexSize + 1);
		for (auto& s : list)
		{
			Species& x = r.species[s.n];
			x.name = s.name; x.category = s.cat; x.types[0] = s.t1; x.types[1] = s.t2;
			for (int i = 0; i < 6; i++) { x.base[i] = s.b[i]; x.evYield[i] = s.ev[i]; }
			for (int i = 0; i < 3; i++) x.abilities[i] = s.ab[i];
			x.growth = s.growth; x.catchRate = s.catchRate; x.genderRatio = s.gender; x.height = s.height; x.weight = s.weight;
			x.dexText = s.text ? s.text : "";
		}
	}
	inline void evo(RomData& r, uint16_t from, uint16_t method, uint16_t param, uint16_t into, const char* how)
	{
		Evolution e; e.method = method; e.param = param; e.into = into; e.how = how;
		r.species[from].evolutions.push_back(e);
	}
	inline void learn(RomData& r, uint16_t n, std::vector<std::pair<int, Mv>> l)
	{
		for (auto& x : l) r.species[n].learnset.push_back({ (uint8_t)x.first, (uint16_t)x.second });
	}
	inline Encounter enc(uint16_t sp, int lo, int hi, int pct, EncounterKind k = EncounterKind::GRASS, TimeOfDay t = TimeOfDay::ANY)
	{
		Encounter e; e.species = sp; e.minLevel = (uint8_t)lo; e.maxLevel = (uint8_t)hi; e.percent = (uint8_t)pct; e.kind = k; e.time = t;
		return e;
	}
	// a Pokédex bit as Live keeps it: species n at bit n - 1 (ram.h)
	inline void setBit(std::vector<uint8_t>& v, int n) { n--; if ((size_t)(n >> 3) >= v.size()) v.resize((n >> 3) + 1); v[n >> 3] |= 1 << (n & 7); }
	inline Mon mon(uint16_t sp, int lv, const char* nick, std::array<uint16_t, 4> mv, std::array<uint8_t, 4> pp, int hp, int maxHp,
		std::array<uint16_t, 5> st, uint8_t nature, uint16_t ability, bool female = false)
	{
		Mon m;
		m.species = sp; m.level = (uint8_t)lv; m.nick = nick ? nick : ""; m.hp = (uint16_t)hp; m.maxHp = (uint16_t)maxHp;
		for (int i = 0; i < 4; i++) { m.moves[i] = mv[i]; m.pp[i] = pp[i]; }
		for (int i = 0; i < 5; i++) m.stats[i] = st[i];
		m.nature = nature; m.ability = ability; m.female = female;
		m.pid = 0x5a3c1e00u + sp * 977u; m.otId = 31402; m.otSecret = 8817;
		return m;
	}

	// ---- Platinum ------------------------------------------------------------------------------------------------------
	enum PtZone : uint16_t { Z_ETERNA = 65, Z_ETERNA_FOREST = 168, Z_R205 = 344, Z_R204 = 342, Z_R203 = 341, Z_OREBURGH_GATE = 160,
		Z_HEARTHOME = 75, Z_R206 = 345, Z_R213 = 352, Z_SOLACEON = 90 };
	inline std::shared_ptr<FixtureRom> platinumRom()
	{
		auto r = std::make_shared<FixtureRom>();
		r->game.id = GameId::PLATINUM; r->game.gen = Gen::IV; r->game.code = "CPUE"; r->game.title = "Platinum"; r->game.dexSize = 493;
		r->natures = natures(); r->abilities = abilities();
		moves(*r); items(*r);
		species(*r, {
			{ 387, "Turtwig", "Tiny Leaf Pokémon", GRASS, GRASS, { 55, 68, 64, 31, 45, 55 }, { OVERGROW }, 3, 45, 31, { 0, 1, 0, 0, 0, 0 }, 4, 102, nullptr },
			{ 390, "Chimchar", "Chimp Pokémon", FIRE, FIRE, { 44, 58, 44, 61, 58, 44 }, { BLAZE }, 3, 45, 31, { 0, 0, 0, 1, 0, 0 }, 5, 62,
				"Its tail burns with a gas made in its belly. Rain can't put the fire out, but it gets weak when the monkey is tired." },
			{ 391, "Monferno", "Playful Pokémon", FIRE, FIGHTING, { 64, 78, 52, 81, 78, 52 }, { BLAZE }, 3, 45, 31, { 0, 1, 0, 1, 0, 0 }, 9, 220,
				"It jumps from wall to wall to strike from above, and keeps the flame on its tail small to stay hidden." },
			{ 392, "Infernape", "Flame Pokémon", FIRE, FIGHTING, { 76, 104, 71, 108, 104, 71 }, { BLAZE, 0, IRON_FIST }, 3, 45, 31, { 0, 1, 0, 1, 1, 0 }, 12, 550,
				"A fighter that uses every limb at once. The fire on its head shows how hard it is fighting." },
			{ 396, "Starly", "Starling Pokémon", NORMAL, FLYING, { 40, 55, 30, 60, 30, 30 }, { KEEN_EYE }, 3, 255, 127, { 0, 0, 0, 1, 0, 0 }, 3, 20, nullptr },
			{ 397, "Staravia", "Starling Pokémon", NORMAL, FLYING, { 55, 75, 50, 80, 40, 40 }, { INTIMIDATE }, 3, 120, 127, { 0, 0, 0, 2, 0, 0 }, 6, 155, nullptr },
			{ 398, "Staraptor", "Predator Pokémon", NORMAL, FLYING, { 85, 120, 70, 100, 50, 50 }, { INTIMIDATE }, 3, 45, 127, { 0, 3, 0, 0, 0, 0 }, 12, 249, nullptr },
			{ 399, "Bidoof", "Plump Mouse Pokémon", NORMAL, NORMAL, { 59, 45, 40, 31, 35, 40 }, { SIMPLE, UNAWARE }, 0, 255, 127, { 1, 0, 0, 0, 0, 0 }, 5, 200, nullptr },
			{ 400, "Bibarel", "Beaver Pokémon", NORMAL, WATER, { 79, 85, 60, 71, 55, 60 }, { SIMPLE, UNAWARE }, 0, 127, 127, { 0, 2, 0, 0, 0, 0 }, 10, 315, nullptr },
			{ 403, "Shinx", "Flash Pokémon", ELECTRIC, ELECTRIC, { 45, 65, 34, 45, 40, 34 }, { RIVALRY, INTIMIDATE }, 3, 235, 127, { 0, 1, 0, 0, 0, 0 }, 5, 95, nullptr },
			{ 404, "Luxio", "Spark Pokémon", ELECTRIC, ELECTRIC, { 60, 85, 49, 60, 60, 49 }, { RIVALRY, INTIMIDATE }, 3, 120, 127, { 0, 2, 0, 0, 0, 0 }, 9, 305, nullptr },
			{ 405, "Luxray", "Gleam Eyes Pokémon", ELECTRIC, ELECTRIC, { 80, 120, 79, 70, 95, 79 }, { RIVALRY, INTIMIDATE }, 3, 45, 127, { 0, 3, 0, 0, 0, 0 }, 14, 420, nullptr },
			{ 406, "Budew", "Bud Pokémon", GRASS, POISON, { 40, 30, 35, 55, 50, 70 }, { NATURAL_CURE, POISON_POINT }, 3, 255, 127, { 0, 0, 0, 0, 1, 0 }, 2, 12, nullptr },
			{ 315, "Roselia", "Thorn Pokémon", GRASS, POISON, { 50, 60, 45, 65, 100, 80 }, { NATURAL_CURE, POISON_POINT }, 3, 150, 127, { 0, 0, 0, 0, 2, 0 }, 3, 20, nullptr },
			{ 407, "Roserade", "Bouquet Pokémon", GRASS, POISON, { 60, 70, 65, 90, 125, 105 }, { NATURAL_CURE, POISON_POINT }, 3, 75, 127, { 0, 0, 0, 0, 3, 0 }, 9, 145, nullptr },
			{ 63, "Abra", "Psi Pokémon", PSYCHIC, PSYCHIC, { 25, 20, 15, 90, 105, 55 }, { SYNCHRONIZE, INNER_FOCUS }, 3, 200, 63, { 0, 0, 0, 0, 1, 0 }, 9, 195, nullptr },
			{ 64, "Kadabra", "Psi Pokémon", PSYCHIC, PSYCHIC, { 40, 35, 30, 105, 120, 70 }, { SYNCHRONIZE, INNER_FOCUS }, 3, 100, 63, { 0, 0, 0, 0, 2, 0 }, 13, 565, nullptr },
			{ 65, "Alakazam", "Psi Pokémon", PSYCHIC, PSYCHIC, { 55, 50, 45, 120, 135, 85 }, { SYNCHRONIZE, INNER_FOCUS }, 3, 50, 63, { 0, 0, 0, 0, 3, 0 }, 15, 480, nullptr },
			{ 41, "Zubat", "Bat Pokémon", POISON, FLYING, { 40, 45, 35, 55, 30, 40 }, { INNER_FOCUS }, 0, 255, 127, { 0, 0, 0, 1, 0, 0 }, 8, 75, nullptr },
			{ 42, "Golbat", "Bat Pokémon", POISON, FLYING, { 75, 80, 70, 90, 65, 75 }, { INNER_FOCUS }, 0, 90, 127, { 0, 0, 0, 2, 0, 0 }, 16, 550, nullptr },
			{ 169, "Crobat", "Bat Pokémon", POISON, FLYING, { 85, 90, 80, 130, 70, 80 }, { INNER_FOCUS }, 0, 90, 127, { 0, 0, 0, 3, 0, 0 }, 18, 750, nullptr },
			{ 265, "Wurmple", "Worm Pokémon", BUG, BUG, { 45, 45, 35, 20, 20, 30 }, { SHIELD_DUST }, 0, 255, 127, { 1, 0, 0, 0, 0, 0 }, 3, 36, nullptr },
			{ 266, "Silcoon", "Cocoon Pokémon", BUG, BUG, { 50, 35, 55, 15, 25, 25 }, { SHED_SKIN }, 0, 120, 127, { 0, 0, 2, 0, 0, 0 }, 6, 100, nullptr },
			{ 267, "Beautifly", "Butterfly Pokémon", BUG, FLYING, { 60, 70, 50, 65, 90, 50 }, { SWARM }, 0, 45, 127, { 0, 0, 0, 0, 3, 0 }, 10, 284, nullptr },
			{ 268, "Cascoon", "Cocoon Pokémon", BUG, BUG, { 50, 35, 55, 15, 25, 25 }, { SHED_SKIN }, 0, 120, 127, { 0, 0, 2, 0, 0, 0 }, 7, 115, nullptr },
			{ 198, "Murkrow", "Darkness Pokémon", DARK, FLYING, { 60, 85, 42, 91, 85, 42 }, { INSOMNIA, SUPER_LUCK }, 3, 30, 127, { 0, 0, 0, 1, 0, 0 }, 5, 21, nullptr },
			{ 420, "Cherubi", "Cherry Pokémon", GRASS, GRASS, { 45, 35, 45, 35, 62, 53 }, { CHLOROPHYLL }, 0, 190, 127, { 0, 0, 0, 0, 1, 0 }, 4, 33, nullptr },
			{ 421, "Cherrim", "Blossom Pokémon", GRASS, GRASS, { 70, 60, 70, 85, 87, 78 }, { FLOWER_GIFT }, 0, 75, 127, { 0, 0, 0, 0, 2, 0 }, 5, 93, nullptr },
			{ 427, "Buneary", "Rabbit Pokémon", NORMAL, NORMAL, { 55, 66, 44, 85, 44, 56 }, { RUN_AWAY, CUTE_CHARM }, 0, 190, 127, { 0, 0, 0, 1, 0, 0 }, 4, 55, nullptr },
			{ 401, "Kricketot", "Cricket Pokémon", BUG, BUG, { 37, 25, 41, 25, 25, 41 }, { SHED_SKIN }, 3, 255, 127, { 0, 0, 1, 0, 0, 0 }, 3, 22, nullptr },
			{ 92, "Gastly", "Gas Pokémon", GHOST, POISON, { 30, 35, 30, 80, 100, 35 }, { LEVITATE }, 3, 190, 127, { 0, 0, 0, 0, 1, 0 }, 13, 1, nullptr },
			{ 93, "Haunter", "Gas Pokémon", GHOST, POISON, { 45, 50, 45, 95, 115, 55 }, { LEVITATE }, 3, 90, 127, { 0, 0, 0, 0, 2, 0 }, 16, 1, nullptr },
			{ 355, "Duskull", "Requiem Pokémon", GHOST, GHOST, { 20, 40, 90, 25, 30, 90 }, { LEVITATE }, 4, 190, 127, { 0, 0, 0, 0, 0, 1 }, 8, 150, nullptr },
			{ 429, "Mismagius", "Magical Pokémon", GHOST, GHOST, { 60, 60, 60, 105, 105, 105 }, { LEVITATE }, 4, 45, 127, { 0, 0, 0, 0, 1, 1 }, 9, 44, nullptr },
			{ 418, "Buizel", "Sea Weasel Pokémon", WATER, WATER, { 55, 65, 35, 85, 60, 30 }, { SWARM }, 0, 190, 127, { 0, 0, 0, 1, 0, 0 }, 7, 295, nullptr },
			{ 74, "Geodude", "Rock Pokémon", ROCK, GROUND, { 40, 80, 100, 20, 30, 30 }, { ROCK_HEAD, STURDY }, 3, 255, 127, { 0, 0, 1, 0, 0, 0 }, 4, 200, nullptr },
			{ 408, "Cranidos", "Head Butt Pokémon", ROCK, ROCK, { 67, 125, 40, 58, 30, 30 }, { MOLD_BREAKER }, 2, 45, 31, { 0, 1, 0, 0, 0, 0 }, 9, 315, nullptr },
			{ 415, "Combee", "Tiny Bee Pokémon", BUG, FLYING, { 30, 30, 42, 70, 30, 42 }, { HONEY_GATHER }, 3, 120, 31, { 0, 0, 0, 1, 0, 0 }, 3, 55, nullptr },
			{ 29, "Nidoran♀", "Poison Pin Pokémon", POISON, POISON, { 55, 47, 52, 41, 40, 40 }, { POISON_POINT }, 3, 235, 254, { 1, 0, 0, 0, 0, 0 }, 4, 70, nullptr },
			{ 481, "Mesprit", "Emotion Pokémon", PSYCHIC, PSYCHIC, { 80, 105, 105, 80, 105, 105 }, { LEVITATE }, 5, 3, 255, { 1, 1, 0, 0, 1, 0 }, 3, 3, nullptr },
		});
		evo(*r, 390, 4, 14, 391, "Lv 14"); evo(*r, 391, 4, 36, 392, "Lv 36");
		evo(*r, 396, 4, 14, 397, "Lv 14"); evo(*r, 397, 4, 34, 398, "Lv 34");
		evo(*r, 399, 4, 15, 400, "Lv 15"); evo(*r, 403, 4, 15, 404, "Lv 15"); evo(*r, 404, 4, 30, 405, "Lv 30");
		evo(*r, 406, 2, 0, 315, "Level up when friendly, in the day"); evo(*r, 315, 7, 107, 407, "Use a Shiny Stone");
		evo(*r, 63, 4, 16, 64, "Lv 16"); evo(*r, 64, 5, 0, 65, "Trade");
		evo(*r, 41, 4, 22, 42, "Lv 22"); evo(*r, 42, 1, 0, 169, "Level up when friendly");
		evo(*r, 265, 4, 7, 266, "Lv 7, by its personality"); evo(*r, 265, 4, 7, 268, "Lv 7, by its personality"); evo(*r, 266, 4, 10, 267, "Lv 10");
		evo(*r, 420, 4, 25, 421, "Lv 25"); evo(*r, 92, 4, 25, 93, "Lv 25");
		learn(*r, 390, { { 1, SCRATCH }, { 1, LEER }, { 7, EMBER }, { 9, TAUNT }, { 15, FURY_SWIPES }, { 17, FLAME_WHEEL }, { 23, FEINT }, { 33, FLARE_BLITZ } });
		learn(*r, 391, { { 1, SCRATCH }, { 1, LEER }, { 7, EMBER }, { 9, TAUNT }, { 14, MACH_PUNCH }, { 16, FURY_SWIPES }, { 19, FLAME_WHEEL },
			{ 26, FEINT }, { 33, CLOSE_COMBAT }, { 49, FLARE_BLITZ } });
		learn(*r, 392, { { 1, SCRATCH }, { 1, LEER }, { 7, EMBER }, { 9, TAUNT }, { 14, MACH_PUNCH }, { 16, FURY_SWIPES }, { 19, FLAME_WHEEL },
			{ 29, FEINT }, { 41, CLOSE_COMBAT }, { 45, FLARE_BLITZ }, { 49, CALM_MIND } });
		learn(*r, 397, { { 1, TACKLE }, { 1, QUICK_ATTACK }, { 23, WING_ATTACK } });
		learn(*r, 404, { { 1, TACKLE }, { 1, LEER }, { 18, SPARK }, { 26, BITE }, { 31, ROAR } });
		learn(*r, 64, { { 1, TELEPORT }, { 16, CONFUSION }, { 21, PSYBEAM }, { 28, CALM_MIND } });
		learn(*r, 400, { { 1, TACKLE }, { 18, HEADBUTT } });
		learn(*r, 406, { { 1, ABSORB }, { 4, GROWTH }, { 7, POISON_STING }, { 10, STUN_SPORE } });
		// places and who lives there (the encounter tables the ROM has; Platinum's grass by time of day)
		auto& zs = r->zones;
		zs[Z_ETERNA] = { Z_ETERNA, "Eterna City", {} };
		zs[Z_HEARTHOME] = { Z_HEARTHOME, "Hearthome City", {} };
		zs[Z_SOLACEON] = { Z_SOLACEON, "Solaceon Town", {} };
		zs[Z_R213] = { Z_R213, "Route 213", {} };
		zs[Z_ETERNA_FOREST] = { Z_ETERNA_FOREST, "Eterna Forest", {
			enc(265, 10, 12, 20), enc(266, 11, 11, 10), enc(268, 11, 11, 10), enc(406, 10, 12, 10),
			enc(427, 11, 13, 10, EncounterKind::GRASS, TimeOfDay::MORNING), enc(427, 11, 13, 10, EncounterKind::GRASS, TimeOfDay::DAY),
			enc(401, 10, 12, 20, EncounterKind::GRASS, TimeOfDay::MORNING), enc(401, 10, 12, 10, EncounterKind::GRASS, TimeOfDay::NIGHT),
			enc(198, 11, 13, 20, EncounterKind::GRASS, TimeOfDay::NIGHT), enc(92, 10, 12, 20, EncounterKind::GRASS, TimeOfDay::NIGHT),
			enc(420, 10, 12, 30, EncounterKind::GRASS, TimeOfDay::MORNING), enc(420, 10, 12, 40, EncounterKind::GRASS, TimeOfDay::DAY),
			enc(420, 10, 12, 10, EncounterKind::GRASS, TimeOfDay::NIGHT), enc(415, 10, 12, 10, EncounterKind::SPECIAL),
			enc(41, 10, 12, 20, EncounterKind::SWARM) } };
		zs[Z_R205] = { Z_R205, "Route 205", { enc(418, 12, 14, 30), enc(406, 12, 13, 20), enc(399, 12, 14, 30), enc(422 > 0 ? 418 : 418, 14, 14, 20, EncounterKind::SURF) } };
		zs[Z_R204] = { Z_R204, "Route 204", { enc(396, 4, 6, 30), enc(406, 4, 6, 40), enc(265, 4, 6, 30) } };
		zs[Z_R203] = { Z_R203, "Route 203", { enc(63, 6, 8, 20), enc(403, 5, 7, 40), enc(396, 5, 7, 40) } };
		zs[Z_OREBURGH_GATE] = { Z_OREBURGH_GATE, "Oreburgh Gate", { enc(41, 5, 7, 70), enc(74, 6, 7, 30) } };
		// the gym leaders' teams (Platinum's)
		auto trainer = [&](uint16_t id, const char* cls, const char* name, std::vector<TrainerMon> team)
		{ Trainer t; t.id = id; t.cls = cls; t.name = name; t.team = team; r->trainers[id] = t; };
		auto tm = [](uint16_t sp, int lv, std::array<uint16_t, 4> mv) { TrainerMon m; m.species = sp; m.level = (uint8_t)lv; for (int i = 0; i < 4; i++) m.moves[i] = mv[i]; return m; };
		trainer(246, "Leader", "Roark", { tm(74, 12, { TACKLE, ROCK_TOMB }), tm(95, 12, { ROCK_TOMB }), tm(408, 14, { HEADBUTT, LEER }) });
		trainer(247, "Leader", "Gardenia", { tm(420, 19, { GROWTH, MAGICAL_LEAF, LEECH_SEED }), tm(387, 19, { ABSORB, GRASS_KNOT }),
			tm(407, 22, { MAGICAL_LEAF, STUN_SPORE, GRASS_KNOT, LEECH_SEED }) });
		trainer(248, "Leader", "Fantina", { tm(355, 24, { NIGHT_SHADE, ASTONISH }), tm(93, 24, { SHADOW_BALL, LICK }), tm(429, 26, { SHADOW_BALL, PSYBEAM, MAGICAL_LEAF }) });
		return r;
	}

	struct Fixture
	{
		std::shared_ptr<FixtureRom> rom;
		std::shared_ptr<Live> live;
		std::shared_ptr<GuideData> guide;
		std::shared_ptr<Advice> advice;
		std::shared_ptr<Journey> journey;
		PageData data() const
		{
			PageData d; d.rom = rom; d.live = live; d.guide = guide; d.advice = advice; d.journey = journey;
			return d;
		}
	};

	inline Fixture platinum()
	{
		Fixture f;
		f.rom = platinumRom();
		auto lv = std::make_shared<Live>();
		lv->valid = true; lv->trainerName = "Lumi"; lv->tid = 31402; lv->sid = 8817;
		lv->zone = Z_ETERNA; lv->money = 8240; lv->playSeconds = 9 * 3600 + 41 * 60; lv->badges = 1;
		lv->timeOfDay = TimeOfDay::NIGHT;
		Mon a = mon(391, 21, nullptr, { FLAME_WHEEL, MACH_PUNCH, FURY_SWIPES, TAUNT }, { 18, 28, 15, 20 }, 52, 61, { 45, 30, 46, 43, 30 }, 3, BLAZE);
		a.exp = 6900; a.friendship = 142; a.item = CHARCOAL;
		uint8_t ivs[6] = { 21, 28, 9, 30, 14, 17 }, evs[6] = { 12, 64, 8, 58, 4, 0 };
		for (int i = 0; i < 6; i++) { a.ivs[i] = ivs[i]; a.evs[i] = evs[i]; }
		Mon b = mon(397, 19, "Pip", { WING_ATTACK, QUICK_ATTACK, TACKLE, LEER }, { 30, 22, 35, 30 }, 49, 49, { 37, 25, 37, 18, 18 }, 13, INTIMIDATE, true);
		b.exp = 5600; b.friendship = 98;
		Mon c = mon(400, 17, nullptr, { HEADBUTT, TACKLE, GROWTH, NOMOVE }, { 15, 33, 40, 0 }, 31, 52, { 33, 24, 24, 20, 24 }, 8, SIMPLE);
		c.exp = 3920; c.status = 0x08;
		Mon d = mon(404, 17, "Volt", { SPARK, BITE, LEER, TACKLE }, { 20, 25, 30, 35 }, 47, 47, { 34, 21, 22, 23, 21 }, 0, INTIMIDATE);
		d.exp = 4100; d.friendship = 120;
		Mon e = mon(64, 18, nullptr, { CONFUSION, TELEPORT, NOMOVE, NOMOVE }, { 22, 20, 0, 0 }, 0, 40, { 14, 13, 38, 43, 25 }, 15, SYNCHRONIZE, true);
		e.exp = 4800;
		Mon g = mon(406, 14, nullptr, { ABSORB, GROWTH, POISON_STING, STUN_SPORE }, { 25, 40, 35, 30 }, 33, 35, { 14, 16, 22, 23, 26 }, 20, NATURAL_CURE, true);
		g.exp = 2744; g.friendship = 206;
		lv->party = { a, b, c, d, e, g };
		for (auto& m : lv->party) { if (!m.friendship) m.friendship = 70; for (int i = 0; i < 6; i++) if (!m.ivs[i]) m.ivs[i] = (uint8_t)((m.species * 7 + i * 11) % 32); }
		for (int n : { 29, 387, 390, 391, 396, 397, 399, 400, 403, 404, 406, 63, 64, 41, 74, 95, 408, 265, 266, 420, 427, 401, 418, 407, 315 }) setBit(lv->dexSeen, n);
		for (int n : { 390, 391, 396, 397, 399, 400, 403, 404, 406, 63, 64, 265 }) setBit(lv->dexCaught, n);
		setBit(lv->dexSeen, 493);
		lv->bag[3] = { { POKE_BALL, 12 }, { GREAT_BALL, 3 }, { DUSK_BALL, 2 } };
		lv->bag[2] = { { TM40, 1 }, { TM39, 1 }, { TM76, 1 } };
		// the battle: Gardenia's Roserade, her last
		Battle& bt = lv->battle;
		bt.kind = BattleKind::TRAINER; bt.trainerId = 247; bt.turn = 3;
		bt.ours[0].mon = lv->party[0]; bt.ours[0].types[0] = FIRE; bt.ours[0].types[1] = FIGHTING; bt.ours[0].present = true;
		bt.ours[0].stages[1] = 1;	// (Atk +1: stages[0] is HP, never used)
		Mon ro = mon(407, 22, nullptr, { MAGICAL_LEAF, STUN_SPORE, GRASS_KNOT, LEECH_SEED }, { 20, 30, 20, 10 }, 34, 61, { 38, 35, 52, 64, 56 }, 15, NATURAL_CURE, true);
		bt.foes[0].mon = ro; bt.foes[0].types[0] = GRASS; bt.foes[0].types[1] = POISON; bt.foes[0].present = true;
		Mon ch = mon(420, 19, nullptr, {}, {}, 0, 49, {}, 0, 0), tw = mon(387, 19, nullptr, {}, {}, 0, 55, {}, 0, 0);
		bt.foeParty = { ch, tw, ro };
		f.live = lv;

		// the guide (ours, as the guide workstream's JSON gives it)
		auto g2 = std::make_shared<GuideData>();
		auto ms = [&](const char* id, const char* name, const char* place, std::vector<uint16_t> trainers, Type theme, Quality q)
		{ Milestone m; m.id = id; m.name = name; m.place = place; m.trainerIds = trainers; m.theme = theme; m.q = q; g2->milestones.push_back(m); };
		ms("roark", "Roark", "Oreburgh City", { 246 }, ROCK, Quality::REFERENCE_ONLY);
		ms("gardenia", "Gardenia", "Eterna City", { 247 }, GRASS, Quality::REFERENCE_ONLY);
		ms("rival-3", "Your rival", "Route 209 entrance", {}, NO_TYPE, Quality::REFERENCE_ONLY);
		ms("fantina", "Fantina", "Hearthome City", { 248 }, GHOST, Quality::REFERENCE_ONLY);
		ms("maylene", "Maylene", "Veilstone City", {}, FIGHTING, Quality::REFERENCE_ONLY);
		ms("wake", "Crasher Wake", "Pastoria City", {}, WATER, Quality::REFERENCE_ONLY);
		ms("byron", "Byron", "Canalave City", {}, STEEL, Quality::REFERENCE_ONLY);
		ms("candice", "Candice", "Snowpoint City", {}, ICE, Quality::REFERENCE_ONLY);
		ms("volkner", "Volkner", "Sunyshore City", {}, ELECTRIC, Quality::REFERENCE_ONLY);
		ms("league", "The Pokémon League", "Victory Road's end", {}, NO_TYPE, Quality::REFERENCE_ONLY);
		auto step = [&](const char* id, const char* title, const char* text, std::vector<uint16_t> zones, Quality q)
		{ GuideStep s; s.id = id; s.title = title; s.text = text; s.zones = zones; s.q = q; g2->steps.push_back(s); };
		step("cut", "Get HM01 Cut after the badge", "Cynthia waits by the Pokémon Center in Eterna City and hands it over once you have the Forest Badge.",
			{ Z_ETERNA }, Quality::REFERENCE_ONLY);
		step("galactic-eterna", "Clear the Galactic building", "The team's Eterna base is north of the gym; their boss there leads with a Poison type.",
			{ Z_ETERNA }, Quality::REFERENCE_ONLY);
		step("bike", "Pick up the Bicycle", "The shop in Eterna City gives one once the building is cleared. Route 206's Cycling Road needs it.",
			{ Z_ETERNA }, Quality::REFERENCE_ONLY);
		step("old-chateau", "The Old Chateau after dark", "The empty house in Eterna Forest has ghosts at night. Bring a light team.", { Z_ETERNA_FOREST }, Quality::REFERENCE_ONLY);
		Secret s1; s1.id = "ef-tm"; s1.text = "A hidden Great Ball in the bushes east of the old house."; s1.zone = Z_ETERNA_FOREST; s1.x = 41; s1.y = 18; s1.q = Quality::REFERENCE_ONLY;
		Secret s2; s2.id = "ef-honey"; s2.text = "Slather Honey on the gold tree by the forest exit: Combee and Burmy come to it overnight."; s2.zone = Z_ETERNA_FOREST; s2.q = Quality::LIVE_VERIFIED;
		g2->secrets = { s1, s2 };
		g2->evolutionNotes[64] = "Kadabra needs a trade to evolve. BananaOS has no link cable: trade it with yourself between two saves, or keep it.";
		g2->evolutionNotes[406] = "Budew evolves at high friendship in the day: walk with it in front, and the Eterna City masseuse helps.";
		g2->sources = { "Bulbapedia, Pokémon Platinum walkthrough", "Serebii, Platinum gym leaders", "pret/pokeplatinum" };
		f.guide = g2;

		auto ad = std::make_shared<Advice>();
		auto mv = [](uint16_t m, int q, float lo, float hi, bool best, bool first, const char* note)
		{ MoveVerdict v; v.move = m; v.quarters = q; v.dmgLo = lo; v.dmgHi = hi; v.best = best; v.movesFirst = first; v.note = note; return v; };
		ad->moves = { mv(FLAME_WHEEL, 8, 58, 69, true, true, ""), mv(MACH_PUNCH, 2, 10, 12, false, true, "always first"),
			mv(FURY_SWIPES, 4, 6, 8, false, true, "2-5 times a turn"), mv(TAUNT, 4, 0, 0, false, true, "Stops Stun Spore and Leech Seed") };
		ad->teamScore = 64;
		ad->scoreParts = { { "Type coverage", 17, 25, {} }, { "Defense", 11, 25, {} }, { "Levels", 21, 25, {} }, { "Moves", 15, 25, {} } };
		ad->next = &g2->milestones[1];
		ad->nextProblems = { "Monferno's Flame Wheel wins this: her team is all Grass.", "Pip's Wing Attack is the backup; Volt and Bibarel take big hits.",
			"Kadabra has fainted: heal before the rematch." };
		CatchSuggestion c1; c1.species = 41; c1.where = "Oreburgh Gate, any time"; c1.why = "Poison and Flying both resist Grass: Gardenia's moves barely hurt it."; c1.value = 0.9f;
		CatchSuggestion c2; c2.species = 198; c2.where = "Eterna Forest, at night"; c2.why = "A Dark type for Fantina's Ghosts in the next gym."; c2.value = 0.8f;
		CatchSuggestion c3; c3.species = 265; c3.where = "Eterna Forest"; c3.why = "Evolves fast; a Beautifly hits Grass with Flying and Bug moves."; c3.value = 0.5f;
		ad->catchNow = { c1, c2, c3 };
		ad->tmPlan = { { TM40, "Pip: a second Flying move" }, { TM39, "Bibarel: a Rock move for the Flying types ahead" } };
		Notice n1; n1.id = "pt-catch-zubat"; n1.page = "team"; n1.title = "Zubat would fill a gap"; n1.text = "Your team has nothing that shrugs off Grass moves.";
		Notice n2; n2.id = "pt-hidden-ef"; n2.page = "here"; n2.title = "A hidden item close by";
		ad->notices = { n1, n2 };
		f.advice = ad;

		auto j = std::make_shared<Journey>();
		auto le = [](LogKind k, uint32_t t, uint16_t sp, uint16_t z, const char* text) { LogEntry e; e.kind = k; e.playSeconds = t; e.species = sp; e.zone = z; e.text = text; return e; };
		j->log = { le(LogKind::FIRST, 600, 390, 0, "Chose Chimchar"), le(LogKind::CATCH, 1500, 396, Z_R203, "Caught Starly"),
			le(LogKind::CATCH, 2100, 399, Z_R203, "Caught Bidoof"), le(LogKind::CATCH, 4300, 403, Z_R203, "Caught Shinx"),
			le(LogKind::EVOLVE, 5400, 391, Z_OREBURGH_GATE, "Chimchar evolved into Monferno"), le(LogKind::BADGE, 7200, 0, 0, "Coal Badge from Roark"),
			le(LogKind::CATCH, 8000, 63, Z_R203, "Caught Abra"), le(LogKind::CATCH, 8900, 406, Z_R204, "Caught Budew"),
			le(LogKind::EVOLVE, 9900, 404, Z_R205, "Shinx evolved into Luxio"), le(LogKind::EVOLVE, 11800, 64, Z_ETERNA_FOREST, "Abra evolved into Kadabra"),
			le(LogKind::GIFT, 30000, 403, Z_ETERNA, "Received Shinx (a trade)"),
			le(LogKind::FIRST, 34800, 0, Z_ETERNA, "Arrived in Eterna City") };
		f.journey = j;
		return f;
	}

	// ---- Black 2 -------------------------------------------------------------------------------------------------------
	enum B2Zone : uint16_t { Z_ASPERTIA = 1, Z_R19 = 2, Z_FLOCCESY_TOWN = 3, Z_R20 = 4, Z_FLOCCESY_RANCH = 5, Z_VIRBANK = 6 };
	inline Fixture black2()
	{
		Fixture f;
		auto r = std::make_shared<FixtureRom>();
		r->game.id = GameId::BLACK2; r->game.gen = Gen::V; r->game.code = "IREO"; r->game.title = "Black 2"; r->game.dexSize = 649;
		r->natures = natures(); r->abilities = abilities();
		moves(*r); items(*r);
		species(*r, {
			{ 495, "Snivy", "Grass Snake Pokémon", GRASS, GRASS, { 45, 45, 55, 63, 45, 55 }, { OVERGROW, 0, CONTRARY }, 3, 45, 31, { 0, 0, 0, 1, 0, 0 }, 6, 81, nullptr },
			{ 496, "Servine", "Grass Snake Pokémon", GRASS, GRASS, { 60, 60, 75, 83, 60, 75 }, { OVERGROW, 0, CONTRARY }, 3, 45, 31, { 0, 0, 0, 2, 0, 0 }, 8, 160, nullptr },
			{ 497, "Serperior", "Regal Pokémon", GRASS, GRASS, { 75, 75, 95, 113, 75, 95 }, { OVERGROW, 0, CONTRARY }, 3, 45, 31, { 0, 0, 0, 3, 0, 0 }, 33, 630, nullptr },
			{ 506, "Lillipup", "Puppy Pokémon", NORMAL, NORMAL, { 45, 60, 45, 55, 25, 45 }, { VITAL_SPIRIT, PICKUP, RUN_AWAY }, 3, 255, 127, { 0, 1, 0, 0, 0, 0 }, 4, 41, nullptr },
			{ 507, "Herdier", "Loyal Dog Pokémon", NORMAL, NORMAL, { 65, 80, 65, 60, 35, 65 }, { INTIMIDATE }, 3, 120, 127, { 0, 2, 0, 0, 0, 0 }, 9, 147, nullptr },
			{ 519, "Pidove", "Tiny Pigeon Pokémon", NORMAL, FLYING, { 50, 55, 50, 43, 36, 30 }, { BIG_PECKS, SUPER_LUCK }, 3, 255, 127, { 0, 1, 0, 0, 0, 0 }, 3, 21, nullptr },
			{ 298, "Azurill", "Polka Dot Pokémon", NORMAL, NORMAL, { 50, 20, 40, 20, 20, 40 }, { THICK_FAT, HUGE_POWER, SAP_SIPPER }, 4, 150, 191, { 1, 0, 0, 0, 0, 0 }, 2, 20, nullptr },
			{ 183, "Marill", "Aqua Mouse Pokémon", WATER, WATER, { 70, 20, 50, 40, 20, 50 }, { THICK_FAT, HUGE_POWER, SAP_SIPPER }, 4, 190, 127, { 2, 0, 0, 0, 0, 0 }, 4, 85, nullptr },
			{ 179, "Mareep", "Wool Pokémon", ELECTRIC, ELECTRIC, { 55, 40, 40, 35, 65, 45 }, { STATIC }, 3, 235, 127, { 0, 0, 0, 0, 1, 0 }, 6, 78, nullptr },
			{ 54, "Psyduck", "Duck Pokémon", WATER, WATER, { 50, 52, 48, 55, 65, 50 }, { DAMP, CLOUD_NINE }, 0, 190, 127, { 0, 0, 0, 0, 1, 0 }, 8, 196, nullptr },
			{ 447, "Riolu", "Emanation Pokémon", FIGHTING, FIGHTING, { 40, 70, 40, 60, 35, 40 }, { STEADFAST, INNER_FOCUS, PRANKSTER }, 3, 75, 31, { 0, 1, 0, 0, 0, 0 }, 7, 202,
				"It reads the feelings of others by the waves they give off, and shows its own with the same waves when it is scared or sad." },
			{ 448, "Lucario", "Aura Pokémon", FIGHTING, STEEL, { 70, 110, 70, 90, 115, 70 }, { STEADFAST, INNER_FOCUS, JUSTIFIED }, 3, 45, 31, { 0, 1, 0, 0, 1, 0 }, 12, 540, nullptr },
			{ 504, "Patrat", "Scout Pokémon", NORMAL, NORMAL, { 45, 55, 39, 42, 35, 39 }, { RUN_AWAY, KEEN_EYE }, 0, 255, 127, { 0, 1, 0, 0, 0, 0 }, 5, 116, nullptr },
			{ 509, "Purrloin", "Devious Pokémon", DARK, DARK, { 41, 50, 37, 66, 50, 37 }, { 0, PRANKSTER }, 0, 255, 127, { 0, 0, 0, 1, 0, 0 }, 4, 101, nullptr },
			{ 540, "Sewaddle", "Sewing Pokémon", BUG, GRASS, { 45, 53, 70, 42, 40, 60 }, { SWARM, CHLOROPHYLL }, 3, 255, 127, { 0, 0, 1, 0, 0, 0 }, 3, 25, nullptr },
		});
		for (auto& s : r->species) (void)s;
		evo(*r, 495, 4, 17, 496, "Lv 17"); evo(*r, 496, 4, 36, 497, "Lv 36");
		evo(*r, 506, 4, 16, 507, "Lv 16"); evo(*r, 298, 1, 0, 183, "Level up when friendly");
		evo(*r, 447, 2, 0, 448, "Level up when friendly, in the day");
		learn(*r, 447, { { 1, QUICK_ATTACK }, { 1, ENDURE }, { 6, COUNTER }, { 11, FORCE_PALM }, { 15, FEINT }, { 24, QUICK_GUARD } });
		learn(*r, 495, { { 1, TACKLE }, { 4, LEER }, { 7, VINE_WHIP }, { 10, WRAP }, { 13, GROWTH }, { 28, SEED_BOMB } });
		auto& zs = r->zones;
		zs[Z_ASPERTIA] = { Z_ASPERTIA, "Aspertia City", {} };
		zs[Z_R19] = { Z_R19, "Route 19", { enc(504, 2, 4, 50), enc(509, 2, 4, 50) } };
		zs[Z_FLOCCESY_TOWN] = { Z_FLOCCESY_TOWN, "Floccesy Town", {} };
		zs[Z_R20] = { Z_R20, "Route 20", { enc(504, 4, 6, 30), enc(509, 4, 6, 30), enc(519, 4, 6, 20), enc(540, 5, 6, 20) } };
		zs[Z_FLOCCESY_RANCH] = { Z_FLOCCESY_RANCH, "Floccesy Ranch", { enc(179, 5, 7, 30), enc(298, 5, 7, 20), enc(54, 5, 7, 5),
			enc(447, 5, 7, 5), enc(298, 5, 7, 60, EncounterKind::GROTTO), enc(179, 5, 7, 40, EncounterKind::GROTTO) } };
		// (a table per season, as Gen V keeps them: Lillipup's slot is Herdier's in autumn here, made up to tell them apart)
		for (uint8_t se = 0; se < 4; se++)
		{
			Encounter e = enc(se == 2 ? 507 : 506, 5, 7, 40);
			e.season = se;
			zs[Z_FLOCCESY_RANCH].encounters.push_back(e);
		}
		zs[Z_VIRBANK] = { Z_VIRBANK, "Virbank City", {} };
		f.rom = r;
		auto lv = std::make_shared<Live>();
		lv->valid = true; lv->trainerName = "Nate"; lv->tid = 51203; lv->sid = 2208;
		lv->zone = Z_FLOCCESY_RANCH; lv->money = 3120; lv->playSeconds = 2 * 3600 + 7 * 60; lv->badges = 1; lv->season = 2;
		lv->timeOfDay = TimeOfDay::DAY;
		Mon a = mon(495, 12, "Ivy", { TACKLE, LEER, VINE_WHIP, WRAP }, { 30, 28, 12, 20 }, 36, 36, { 19, 22, 22, 18, 22 }, 10, OVERGROW, true);
		a.exp = 1728; a.friendship = 120;
		Mon b = mon(506, 10, nullptr, { TACKLE, LEER, BITE, NOMOVE }, { 32, 30, 25, 0 }, 22, 31, { 18, 14, 15, 11, 14 }, 3, PICKUP);
		Mon c = mon(519, 9, nullptr, { GUST, QUICK_ATTACK, LEER, NOMOVE }, { 30, 28, 30, 0 }, 26, 28, { 16, 15, 13, 11, 10 }, 13, BIG_PECKS, true);
		lv->party = { a, b, c };
		for (auto& m : lv->party) { if (!m.friendship) m.friendship = 70; for (int i = 0; i < 6; i++) m.ivs[i] = (uint8_t)((m.species * 5 + i * 13) % 32); }
		for (int n : { 495, 504, 509, 506, 519, 298, 179, 447 }) setBit(lv->dexSeen, n);
		for (int n : { 495, 504, 506, 519 }) setBit(lv->dexCaught, n);
		lv->bag[3] = { { POKE_BALL, 9 }, { GREAT_BALL, 2 }, { QUICK_BALL, 1 } };
		Battle& bt = lv->battle;
		bt.kind = BattleKind::WILD; bt.turn = 1;
		bt.ours[0].mon = a; bt.ours[0].types[0] = GRASS; bt.ours[0].types[1] = GRASS; bt.ours[0].present = true;
		Mon ri = mon(447, 7, nullptr, { QUICK_ATTACK, ENDURE, COUNTER, NOMOVE }, { 30, 10, 20, 0 }, 22, 24, { 14, 10, 13, 9, 10 }, 13, PRANKSTER);
		uint8_t iv[6] = { 24, 31, 11, 29, 6, 18 };
		for (int i = 0; i < 6; i++) ri.ivs[i] = iv[i];
		ri.otId = 0; ri.otSecret = 0; ri.pid = 0x7c1e93b4;
		bt.foes[0].mon = ri; bt.foes[0].types[0] = FIGHTING; bt.foes[0].types[1] = FIGHTING; bt.foes[0].present = true;
		bt.foeParty = { ri };
		f.live = lv;
		auto g2 = std::make_shared<GuideData>();
		Milestone m1; m1.id = "cheren"; m1.name = "Cheren"; m1.place = "Aspertia City"; m1.theme = NORMAL; m1.q = Quality::REFERENCE_ONLY;
		Milestone m2; m2.id = "roxie"; m2.name = "Roxie"; m2.place = "Virbank City"; m2.theme = POISON; m2.q = Quality::REFERENCE_ONLY;
		g2->milestones = { m1, m2 };
		Secret s1; s1.id = "fr-grotto"; s1.text = "The Hidden Grotto in the ranch's north-west corner: walk out and back in to the area for another roll."; s1.zone = Z_FLOCCESY_RANCH; s1.q = Quality::REFERENCE_ONLY;
		g2->secrets = { s1 };
		g2->evolutionNotes[447] = "Riolu evolves when friendly during the day; a Soothe Bell from Nimbasa helps.";
		g2->sources = { "Bulbapedia, Black 2 and White 2 walkthrough", "Serebii, Hidden Grottos" };
		f.guide = g2;
		auto ad = std::make_shared<Advice>();
		auto mv = [](uint16_t m, int q, float lo, float hi, bool best, bool first, const char* note)
		{ MoveVerdict v; v.move = m; v.quarters = q; v.dmgLo = lo; v.dmgHi = hi; v.best = best; v.movesFirst = first; v.note = note; return v; };
		ad->moves = { mv(TACKLE, 4, 26, 31, false, true, ""), mv(LEER, 4, 0, 0, false, true, "Lowers its Defense"),
			mv(VINE_WHIP, 4, 33, 40, true, true, "weakens it, no KO"), mv(WRAP, 4, 6, 8, false, true, "keeps it from fleeing") };
		CatchSuggestion c1; c1.species = 447;
		ad->catching = { { QUICK_BALL, 88, "First turn only: Quick Ball now." }, { GREAT_BALL, 41, "" }, { POKE_BALL, 28, "" } };
		ad->catching[2].tip = "";
		ad->teamScore = 41;
		ad->scoreParts = { { "Type coverage", 9, 25, {} }, { "Defense", 10, 25, {} }, { "Levels", 14, 25, {} }, { "Moves", 8, 25, {} } };
		ad->next = &g2->milestones[1];
		ad->nextProblems = { "Roxie's Poison types resist Ivy's Grass moves.", "A Fighting or Ground type would help a lot." };
		Notice n1; n1.id = "b2-riolu"; n1.page = "battle"; n1.title = "Riolu is rare here: 5%";
		ad->notices = { n1 };
		f.advice = ad;
		auto j = std::make_shared<Journey>();
		j->challenge = true;
		auto le = [](LogKind k, uint32_t t, uint16_t sp, uint16_t z, const char* text) { LogEntry e; e.kind = k; e.playSeconds = t; e.species = sp; e.zone = z; e.text = text; return e; };
		j->log = { le(LogKind::FIRST, 400, 495, Z_ASPERTIA, "Chose Snivy"), le(LogKind::CATCH, 1500, 506, Z_R19, "Caught Lillipup"),
			le(LogKind::CATCH, 3100, 519, Z_R20, "Caught Pidove"), le(LogKind::LOST, 4800, 504, Z_R20, "Patrat fainted: out for good"),
			le(LogKind::BADGE, 6900, 0, Z_ASPERTIA, "Basic Badge from Cheren") };
		RouteRecord r1; r1.zone = Z_R19; r1.species = 506; r1.outcome = RouteRecord::CAUGHT;
		RouteRecord r2; r2.zone = Z_R20; r2.species = 519; r2.outcome = RouteRecord::CAUGHT;
		RouteRecord r3; r3.zone = Z_FLOCCESY_TOWN; r3.species = 504; r3.outcome = RouteRecord::MISSED;
		RouteRecord r4; r4.zone = Z_FLOCCESY_RANCH; r4.species = 447; r4.outcome = RouteRecord::OPEN;
		j->routes = { r1, r2, r3, r4 };
		Mon lost = mon(504, 6, "Scout", {}, {}, 0, 21, {}, 0, RUN_AWAY);
		j->lost = { lost };
		f.journey = j;
		return f;
	}
}
