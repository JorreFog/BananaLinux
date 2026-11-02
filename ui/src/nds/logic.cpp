// banana-ui: the DS Pokémon companion's advice (logic.h).
#include "nds/logic.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace banana::nds
{
	// ---- ids (the same in Gen IV and V; checked against both games' own name tables) ------------------------------
	enum Ability : uint16_t { A_VOLT_ABSORB = 10, A_WATER_ABSORB = 11, A_FLASH_FIRE = 18, A_WONDER_GUARD = 25,
		A_LEVITATE = 26, A_LIGHTNINGROD = 31, A_SWIFT_SWIM = 33, A_CHLOROPHYLL = 34, A_HUGE_POWER = 37, A_THICK_FAT = 47,
		A_HUSTLE = 55, A_GUTS = 62, A_OVERGROW = 65, A_BLAZE = 66, A_TORRENT = 67, A_SWARM = 68, A_PURE_POWER = 74,
		A_MOTOR_DRIVE = 78, A_HEATPROOF = 85, A_DRY_SKIN = 87, A_ADAPTABILITY = 91, A_NORMALIZE = 96, A_TECHNICIAN = 101,
		A_MOLD_BREAKER = 104, A_TINTED_LENS = 110, A_FILTER = 111, A_SCRAPPY = 113, A_STORM_DRAIN = 114,
		A_SOLID_ROCK = 116, A_SAP_SIPPER = 157 };
	enum Item : uint16_t { I_MASTER = 1, I_ULTRA, I_GREAT, I_POKE, I_SAFARI, I_NET, I_DIVE, I_NEST, I_REPEAT, I_TIMER,
		I_LUXURY, I_PREMIER, I_DUSK, I_HEAL, I_QUICK, I_CHERISH, I_CHOICE_BAND = 220, I_SILVERPOWDER = 222,
		I_METAL_COAT = 233, I_LIGHT_BALL = 236, I_THICK_CLUB = 258, I_MUSCLE_BAND = 266, I_WISE_GLASSES = 267,
		I_EXPERT_BELT = 268, I_LIFE_ORB = 270, I_CHOICE_SCARF = 287, I_CHOICE_SPECS = 297, I_FAST = 492, I_LEVEL,
		I_LURE, I_HEAVY, I_LOVE, I_FRIEND, I_MOON, I_SPORT, I_PARK, I_DREAM = 576 };

	// the type a held item powers up by 20 % (the type items 237..251 and the Plates 298..313), NO_TYPE for none
	static Type itemBoost(uint16_t it)
	{
		static const Type T237[] = { GROUND, ROCK, GRASS, DARK, FIGHTING, ELECTRIC, WATER, FLYING, POISON, ICE, GHOST,
			PSYCHIC, FIRE, DRAGON, NORMAL };		// Soft Sand .. Silk Scarf
		static const Type PLATE[] = { FIRE, WATER, ELECTRIC, GRASS, ICE, FIGHTING, POISON, GROUND, FLYING, PSYCHIC, BUG,
			ROCK, GHOST, DRAGON, DARK, STEEL };	// Flame Plate .. Iron Plate
		if (it >= 237 && it <= 251) return T237[it - 237];
		if (it >= 298 && it <= 313) return PLATE[it - 298];
		if (it == I_SILVERPOWDER) return BUG;
		if (it == I_METAL_COAT) return STEEL;
		return NO_TYPE;
	}

	static const Species* sp(const RomData& rom, uint16_t s) { return s && s < rom.species.size() ? &rom.species[s] : nullptr; }

	// ---- stats ------------------------------------------------------------------------------------------------------
	// nature n raises stat n / 5 and lowers n % 5, over Atk, Def, Speed, SpA, SpD (neutral when they are the same)
	static int natureTenths(int nature, int stat)
	{
		if (stat < 1 || stat > 5 || nature < 0 || nature >= 25) return 10;
		const int up = nature / 5 + 1, down = nature % 5 + 1;
		if (up == down) return 10;
		return stat == up ? 11 : stat == down ? 9 : 10;
	}
	int statValue(int stat, int base, int iv, int ev, int level, int nature)
	{
		const int core = (2 * base + iv + ev / 4) * level / 100;
		if (stat == 0) return base == 1 ? 1 : core + level + 10;		// (Shedinja: always 1)
		return (core + 5) * natureTenths(nature, stat) / 10;
	}
	static int staged(int v, int stage)
	{
		stage = std::max(-6, std::min(6, stage));
		return stage >= 0 ? v * (2 + stage) / 2 : v * 2 / (2 - stage);
	}

	Fighter fighterOf(const Mon& m, const RomData& rom)
	{
		Fighter f;
		f.species = m.species; f.ability = m.ability; f.item = m.item; f.level = m.level ? m.level : 1;
		f.hp = m.hp; f.maxHp = m.maxHp ? m.maxHp : 1;
		for (int i = 0; i < 5; i++) f.stats[i] = m.stats[i];
		f.status = m.status;
		if (auto* s = sp(rom, m.species)) { f.types[0] = s->types[0]; f.types[1] = s->types[1] == s->types[0] ? NO_TYPE : s->types[1]; }
		return f;
	}
	Fighter fighterOf(const Battler& b, const RomData& rom)
	{
		Fighter f = fighterOf(b.mon, rom);
		for (int i = 0; i < 8; i++) f.stages[i] = b.stages[i];
		if (b.types[0] != NO_TYPE)
		{
			f.types[0] = b.types[0];
			f.types[1] = b.types[1] == b.types[0] ? NO_TYPE : b.types[1];
		}
		return f;
	}
	Fighter fighterOf(const TrainerMon& t, const RomData& rom, int iv)
	{
		Fighter f;
		f.species = t.species; f.item = t.item; f.level = t.level ? t.level : 1;
		const Species* s = sp(rom, t.species);
		if (!s) return f;
		f.types[0] = s->types[0]; f.types[1] = s->types[1] == s->types[0] ? NO_TYPE : s->types[1];
		f.ability = s->abilities[0];
		f.maxHp = f.hp = (uint16_t)statValue(0, s->base[0], iv, 0, f.level, 0);
		for (int i = 0; i < 5; i++) f.stats[i] = (uint16_t)statValue(i + 1, s->base[i + 1], iv, 0, f.level, 0);
		return f;
	}

	// ---- damage -------------------------------------------------------------------------------------------------
	// Gen V's multipliers in 4096ths, rounded half down
	static int mod5(int v, int m4096) { return (int)(((int64_t)v * m4096 + 2047) >> 12); }

	DamageRange damage(Gen g, const MoveData& mv, const Fighter& a, const Fighter& d, Weather w)
	{
		DamageRange r;
		if (mv.cat == MoveCategory::STATUS || mv.power == 0) { r.note = "no damage"; r.quarters = 4; return r; }
		Type t = a.ability == A_NORMALIZE ? NORMAL : mv.type;
		const bool phys = mv.cat == MoveCategory::PHYSICAL;
		const bool breaker = a.ability == A_MOLD_BREAKER;
		const uint16_t dab = breaker ? 0 : d.ability;
		// type effectiveness (Scrappy: Normal and Fighting hit Ghost)
		auto eff1 = [&](Type dt) -> int
		{
			if (dt == NO_TYPE) return 4;
			if (a.ability == A_SCRAPPY && dt == GHOST && (t == NORMAL || t == FIGHTING)) return 4;
			return effectiveness(t, dt, NO_TYPE);
		};
		const int e1 = eff1(d.types[0]), e2 = d.types[1] != d.types[0] ? eff1(d.types[1]) : 4;
		r.quarters = e1 * e2 / 4;
		// immunities from abilities
		const bool immune = (t == GROUND && dab == A_LEVITATE) || (t == FIRE && dab == A_FLASH_FIRE) ||
			(t == WATER && (dab == A_WATER_ABSORB || dab == A_DRY_SKIN || (g == Gen::V && dab == A_STORM_DRAIN))) ||
			(t == ELECTRIC && (dab == A_VOLT_ABSORB || dab == A_MOTOR_DRIVE || (g == Gen::V && dab == A_LIGHTNINGROD))) ||
			(t == GRASS && g == Gen::V && dab == A_SAP_SIPPER) || (dab == A_WONDER_GUARD && r.quarters <= 4);
		if (immune) { r.quarters = 0; r.note = "its ability blocks it"; return r; }
		if (r.quarters == 0) { r.note = "no effect"; return r; }
		// power
		int power = mv.power;
		if (power <= 1) { r.note = "power varies"; return r; }
		if (a.ability == A_TECHNICIAN && power <= 60) power = power * 3 / 2;
		const bool pinch = a.hp * 3 <= a.maxHp;
		if (pinch && ((a.ability == A_OVERGROW && t == GRASS) || (a.ability == A_BLAZE && t == FIRE) ||
			(a.ability == A_TORRENT && t == WATER) || (a.ability == A_SWARM && t == BUG))) power = power * 3 / 2;
		if (itemBoost(a.item) == t) power = power * 12 / 10;
		if (a.item == I_MUSCLE_BAND && phys) power = power * 11 / 10;
		if (a.item == I_WISE_GLASSES && !phys) power = power * 11 / 10;
		if (dab == A_HEATPROOF && t == FIRE) power /= 2;
		if (dab == A_DRY_SKIN && t == FIRE) power = power * 5 / 4;
		// attack and defence with their stages
		int atk = phys ? a.stats[0] : a.stats[3], def = phys ? d.stats[1] : d.stats[4];
		atk = staged(atk, phys ? a.stages[1] : a.stages[4]);
		def = staged(def, phys ? d.stages[2] : d.stages[5]);
		if (phys && (a.ability == A_HUGE_POWER || a.ability == A_PURE_POWER)) atk *= 2;
		if (phys && a.ability == A_HUSTLE) atk = atk * 3 / 2;
		const bool statused = (a.status & 0xFF) != 0;
		if (phys && a.ability == A_GUTS && statused) atk = atk * 3 / 2;
		if (phys && a.item == I_CHOICE_BAND) atk = atk * 3 / 2;
		if (!phys && a.item == I_CHOICE_SPECS) atk = atk * 3 / 2;
		if (a.item == I_LIGHT_BALL && a.species == 25) atk *= 2;
		if (phys && a.item == I_THICK_CLUB && (a.species == 104 || a.species == 105)) atk *= 2;
		if (dab == A_THICK_FAT && (t == FIRE || t == ICE)) atk /= 2;
		atk = std::max(1, atk);
		def = std::max(1, def);
		const int lv = 2 * a.level / 5 + 2;
		int base = g == Gen::IV ? (lv * power * atk / 50) / def : (lv * power * atk / def) / 50;
		const bool burn = phys && (a.status & 0x10) && a.ability != A_GUTS;
		const int wx = (w == Weather::RAIN && t == WATER) || (w == Weather::SUN && t == FIRE) ? 3 :
			(w == Weather::RAIN && t == FIRE) || (w == Weather::SUN && t == WATER) ? 1 : 2;	// halves
		if (g == Gen::IV)
		{
			if (burn) base /= 2;
			base = base * wx / 2;
			base += 2;
		}
		else
		{
			base += 2;
			if (wx != 2) base = mod5(base, wx == 3 ? 6144 : 2048);
		}
		r.stab = t == a.types[0] || t == a.types[1];
		const int stab4096 = r.stab ? (a.ability == A_ADAPTABILITY ? 8192 : 6144) : 4096;
		auto roll = [&](int rnd) -> int
		{
			int x = base * rnd / 100;
			if (g == Gen::IV)
			{
				if (a.item == I_LIFE_ORB) x = x * 13 / 10;
				x = x * stab4096 / 4096;
				x = x * e1 / 4;
				x = x * e2 / 4;
				if (r.quarters > 4 && (dab == A_FILTER || dab == A_SOLID_ROCK)) x = x * 3 / 4;
				if (r.quarters > 4 && a.item == I_EXPERT_BELT) x = x * 6 / 5;
				if (r.quarters < 4 && a.ability == A_TINTED_LENS) x *= 2;
			}
			else
			{
				x = mod5(x, stab4096);
				x = x * r.quarters / 4;
				if (burn) x /= 2;
				int fin = 4096;
				if (r.quarters > 4 && (dab == A_FILTER || dab == A_SOLID_ROCK)) fin = fin * 3072 / 4096;
				if (r.quarters > 4 && a.item == I_EXPERT_BELT) fin = fin * 4915 / 4096;
				if (r.quarters < 4 && a.ability == A_TINTED_LENS) fin *= 2;
				if (a.item == I_LIFE_ORB) fin = fin * 5324 / 4096;
				x = mod5(x, fin);
			}
			return std::max(1, x);
		};
		r.lo = roll(85);
		r.hi = roll(100);
		return r;
	}

	int effectiveSpeed(Gen, const Fighter& f, Weather w)
	{
		int s = staged(f.stats[2], f.stages[3]);
		if (f.item == I_CHOICE_SCARF) s = s * 3 / 2;
		if ((f.ability == A_SWIFT_SWIM && w == Weather::RAIN) || (f.ability == A_CHLOROPHYLL && w == Weather::SUN)) s *= 2;
		if (f.status & 0x40) s /= 4;		// paralysed
		return s;
	}
	int movesFirst(Gen g, const MoveData& ma, const Fighter& a, const MoveData& mb, const Fighter& b, Weather w)
	{
		const int pa = (int8_t)ma.priority, pb = (int8_t)mb.priority;
		if (pa != pb) return pa > pb ? 1 : -1;
		const int sa = effectiveSpeed(g, a, w), sb = effectiveSpeed(g, b, w);
		return sa > sb ? 1 : sa < sb ? -1 : 0;
	}

	// ---- catching -------------------------------------------------------------------------------------------------
	float catchChance(Gen g, const RomData& rom, const Mon& wild, uint16_t ball, const CatchContext& c, float* mult)
	{
		const Species* s = sp(rom, wild.species);
		if (!s || !wild.maxHp) return 0;
		Type t1 = s->types[0], t2 = s->types[1];
		float b = 1;
		bool sure = false;
		switch (ball)
		{
		case I_MASTER: sure = true; break;
		case I_ULTRA: b = 2; break;
		case I_GREAT: case I_SAFARI: case I_SPORT: b = 1.5f; break;
		case I_NET: b = (t1 == WATER || t1 == BUG || t2 == WATER || t2 == BUG) ? 3 : 1; break;
		case I_DIVE: b = c.surfingOrFishing ? 3.5f : 1; break;
		case I_NEST: b = std::max(1.0f, ((g == Gen::IV ? 40 : 41) - wild.level) / 10.0f); break;
		case I_REPEAT: b = c.caughtBefore ? 3 : 1; break;
		case I_TIMER:
			// turns passed: Gen IV (turns + 10) / 10, Gen V 1 + turns * 1229/4096; both at most 4
			b = g == Gen::IV ? std::min(4.0f, (c.turn - 1 + 10) / 10.0f) : std::min(4.0f, 1 + (c.turn - 1) * 1229 / 4096.0f);
			break;
		case I_DUSK: b = (c.time == TimeOfDay::NIGHT || c.cave) ? 3.5f : 1; break;
		case I_QUICK: b = c.turn <= 1 ? (g == Gen::IV ? 4 : 5) : 1; break;
		case I_PARK: sure = true; break;
		case I_FAST: b = s->base[3] >= 100 ? 4 : 1; break;
		case I_LEVEL:
			b = c.ourLevel > 4 * wild.level ? 8 : c.ourLevel > 2 * wild.level ? 4 : c.ourLevel > wild.level ? 2 : 1; break;
		case I_LURE: b = c.fishing ? 3 : 1; break;
		case I_LOVE: b = (c.ourSpecies == wild.species && c.ourFemale != wild.female) ? 8 : 1; break;
		default: b = 1; break;	// Poké, Luxury, Premier, Heal, Cherish, Friend, Moon/Heavy (not counted), Dream
		}
		if (mult) *mult = sure ? 255 : b;
		if (sure) return 1;
		const int M = wild.maxHp, H = std::min<int>(wild.hp, M);
		const uint32_t st = wild.status;
		const bool sleepFreeze = (st & 7) || (st & 0x20);
		const bool other = st & (0x08 | 0x10 | 0x40 | 0x80);
		const float sm = sleepFreeze ? (g == Gen::IV ? 2.0f : 2.5f) : other ? 1.5f : 1.0f;
		if (g == Gen::IV)
		{
			int a = (int)((3 * M - 2 * H) * s->catchRate * b / (3 * M));
			a = (int)(a * sm);
			if (a >= 255) return 1;
			if (a < 1) a = 1;
			const int inner = (int)std::sqrt((double)(16711680 / a));
			const int bb = 1048560 / std::max(1, (int)std::sqrt((double)inner));
			const double p = bb / 65536.0;
			return (float)std::min(1.0, p * p * p * p);
		}
		double x = (3.0 * M - 2.0 * H) * s->catchRate * b / (3.0 * M) * sm;
		if (x >= 255) return 1;
		x = std::max(1.0, std::floor(x * 4096) / 4096);
		const double y = std::floor(65536.0 / std::sqrt(std::sqrt(255.0 / x))) / 65536.0;
		// critical captures (one shake check), more likely the more species are caught
		const int n = c.dexCaught;
		const double cp = n > 600 ? 2.5 : n > 450 ? 2 : n > 300 ? 1.5 : n > 150 ? 1 : n > 30 ? 0.5 : 0;
		const double cc = std::floor(std::min(255.0, x) * cp / 6) / 256.0;
		return (float)std::min(1.0, cc * y + (1 - cc) * y * y * y);
	}

	// ---- the guide's milestones -----------------------------------------------------------------------------------
	static bool flagSet(const Live& l, int n)
	{
		return n >= 0 && (size_t)(n >> 3) < l.flags.size() && (l.flags[n >> 3] >> (n & 7)) & 1;
	}
	static bool doneByRule(const std::string& w, const Live& l, bool& known)
	{
		known = true;
		if (!w.compare(0, 6, "badge:")) { const int n = atoi(w.c_str() + 6); return n >= 1 && n <= 16 && (l.badges >> (n - 1)) & 1; }
		if (!w.compare(0, 5, "flag:")) return flagSet(l, atoi(w.c_str() + 5));
		if (w == "party")	// the starter: a Pokémon (not an egg) in the party
		{
			for (auto& m : l.party) if (m.species && !m.egg) return true;
			return false;
		}
		known = false;
		return false;
	}
	bool milestoneDone(const Milestone& m, const Live& live, const GuideData& g, size_t index)
	{
		bool known;
		if (doneByRule(m.doneWhen, live, known)) return true;
		if (known || (m.doneWhen != "later" && m.doneWhen != "none")) return false;
		// "later" (and "none": an optional fight or legend must not hold the story's next goal): done once any later
		// milestone is
		for (size_t j = index + 1; j < g.milestones.size(); j++)
		{
			bool k;
			if (doneByRule(g.milestones[j].doneWhen, live, k)) return true;
		}
		return false;
	}
	const Milestone* nextMilestone(const GuideData& g, const Live& live)
	{
		for (size_t i = 0; i < g.milestones.size(); i++)
			if (!milestoneDone(g.milestones[i], live, g, i)) return &g.milestones[i];
		return nullptr;
	}
	std::set<uint16_t> reachableZones(const GuideData& g, const Live& live)
	{
		std::set<uint16_t> z;
		for (size_t i = 0; i < g.milestones.size(); i++)
		{
			z.insert(g.milestones[i].reachableZones.begin(), g.milestones[i].reachableZones.end());
			if (!milestoneDone(g.milestones[i], live, g, i)) break;
		}
		if (live.zone) z.insert(live.zone);
		return z;
	}

	int tmNumber(Gen g, uint16_t it)
	{
		if (it >= 328 && it <= 419) return it - 327;				// TM01..TM92
		if (g == Gen::IV) return it >= 420 && it <= 427 ? it - 327 : 0;	// HM01..08 = 93..100
		if (it >= 618 && it <= 620) return it - 618 + 93;			// Gen V TM93..95
		if (it >= 420 && it <= 425) return it - 420 + 96;			// Gen V HM01..06 = 96..101
		return 0;
	}

	// ---- evolution watch, EVs ---------------------------------------------------------------------------------------
	int evTotal(const Mon& m) { int t = 0; for (int i = 0; i < 6; i++) t += m.evs[i]; return t; }

	static bool inBag(const Live& l, uint16_t item)
	{
		for (auto& p : l.bag) for (auto& b : p.second) if (b.item == item && b.count) return true;
		return false;
	}
	std::vector<EvoStatus> evolutionWatch(Gen, const Mon& m, const RomData& rom, const Live& live, const GuideData* guide)
	{
		std::vector<EvoStatus> out;
		const Species* s = sp(rom, m.species);
		if (!s || m.egg) return out;
		for (auto& e : s->evolutions)
		{
			EvoStatus st;
			st.into = e.into; st.method = e.method; st.param = e.param; st.how = e.how;
			// (methods in Gen V's numbering, as RomData keeps them)
			switch (e.method)
			{
			case 1: case 2: case 3:		// friendship (any time / day / night)
				st.friendshipToGo = std::max(0, 220 - m.friendship);
				st.ready = st.friendshipToGo == 0 && (e.method == 1 || (e.method == 2) == (live.timeOfDay != TimeOfDay::NIGHT));
				break;
			case 4: case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 23: case 24: case 25: case 26: case 27:
				st.levelsToGo = e.method >= 25 ? -1 : std::max(0, (int)e.param - (int)m.level);	// (25-27: a place)
				if (e.method == 23 && m.female) continue;	// male only
				if (e.method == 24 && !m.female) continue;	// female only
				st.ready = st.levelsToGo == 0 && e.method >= 4 && e.method <= 15;
				if (e.method == 9) st.ready = st.ready && m.stats[0] > m.stats[1];
				if (e.method == 10) st.ready = st.ready && m.stats[0] == m.stats[1];
				if (e.method == 11) st.ready = st.ready && m.stats[0] < m.stats[1];
				break;
			case 8: case 17: case 18:	// use an item
				if (e.method == 17 && m.female) continue;
				if (e.method == 18 && !m.female) continue;
				st.needsItem = true; st.haveItem = inBag(live, e.param); st.ready = st.haveItem;
				break;
			case 6: case 19: case 20:	// hold an item (traded / at day / at night)
				st.needsItem = true; st.haveItem = m.item == e.param || inBag(live, e.param);
				st.ready = m.item == e.param && (e.method == 6 ? false :
					(e.method == 19) == (live.timeOfDay != TimeOfDay::NIGHT));
				break;
			case 21:					// knows a move
				for (auto mv : m.moves) if (mv == e.param) st.ready = true;
				break;
			default: break;				// trades, party, beauty: the words say how
			}
			if (guide)
			{
				auto it = guide->evolutionNotes.find(m.species);
				if (it != guide->evolutionNotes.end()) st.how += (st.how.empty() ? "" : " ") + it->second;
			}
			out.push_back(st);
		}
		return out;
	}
}

namespace banana::nds
{
	static const char* TYPE_NAME[NUM_TYPES] = { "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug",
		"Ghost", "Steel", "Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark" };
	static std::string typeName(Type t) { return t < NUM_TYPES ? TYPE_NAME[t] : "?"; }
	static std::string speciesName(const RomData& rom, uint16_t s) { auto* x = sp(rom, s); return x ? x->name : "?"; }
	static const MoveData* move(const RomData& rom, uint16_t m) { return m && m < rom.moves.size() ? &rom.moves[m] : nullptr; }
	static bool damaging(const MoveData* m) { return m && m->cat != MoveCategory::STATUS && m->power > 1; }
	static int effOn(Type atk, const Species* s)
	{
		if (!s) return 4;
		return effectiveness(atk, s->types[0], s->types[1] == s->types[0] ? NO_TYPE : s->types[1]);
	}
	static bool hasBit(const std::vector<uint8_t>& v, uint16_t species)
	{
		const int b = species - 1;
		return species && (size_t)(b >> 3) < v.size() && (v[b >> 3] >> (b & 7)) & 1;
	}

	// the next big fight's team, from the ROM (the first of its trainer ids the ROM has)
	static const Trainer* foeTrainer(const RomData& rom, const Milestone* m)
	{
		if (!m) return nullptr;
		for (auto id : m->trainerIds)
		{
			auto it = rom.trainers.find(id);
			if (it != rom.trainers.end() && !it->second.team.empty()) return &it->second;
		}
		return nullptr;
	}
	static int foeAce(const Milestone* m, const Trainer* t)
	{
		int ace = m ? std::max(m->levelCap, m->foeAceLevel) : 0;
		if (t) for (auto& x : t->team) ace = std::max<int>(ace, x.level);
		return ace;
	}
	// the attacking types our party's damaging moves have
	static std::set<Type> ourMoveTypes(const RomData& rom, const Live& l)
	{
		std::set<Type> t;
		for (auto& m : l.party)
			if (!m.egg) for (auto mv : m.moves) if (auto* d = move(rom, mv); damaging(d)) t.insert(d->type);
		return t;
	}

	static void teamScore(const RomData& rom, const Live& l, const Milestone* next, const Trainer* foe, Advice& a)
	{
		std::vector<const Mon*> team;
		for (auto& m : l.party) if (!m.egg && m.species) team.push_back(&m);
		const std::set<Type> mt = ourMoveTypes(rom, l);
		// 1. coverage (25): 12 for the 17 types in general, 13 for the next fight's team
		ScorePart cov; cov.name = "Coverage";
		int hit = 0;
		for (int t = 0; t < NUM_TYPES; t++)
		{
			bool se = false;
			for (Type x : mt) if (effectiveness(x, (Type)t, NO_TYPE) > 4) se = true;
			hit += se;
		}
		int part = hit * 12 / NUM_TYPES;
		if (foe)
		{
			int beaten = 0;
			std::vector<std::string> missing;
			for (auto& f : foe->team)
			{
				bool se = false;
				for (Type x : mt) if (effOn(x, sp(rom, f.species)) > 4) se = true;
				beaten += se;
				if (!se) missing.push_back(speciesName(rom, f.species));
			}
			part += beaten * 13 / (int)foe->team.size();
			if (!missing.empty())
			{
				std::string w = "nothing super effective against ";
				for (size_t i = 0; i < missing.size(); i++) w += (i ? ", " : "") + missing[i];
				cov.why.push_back(w);
			}
		}
		else part += hit * 13 / NUM_TYPES;
		cov.score = std::min(25, part);
		cov.why.insert(cov.why.begin(), std::to_string(hit) + " of 17 types hit super effectively");
		// 2. defence (25): a type is a hole when at least half the team (2 or more) takes double from it and nobody
		// resists it; 5 points each
		ScorePart def; def.name = "Defence";
		int holes = 0;
		for (int t = 0; t < NUM_TYPES && !team.empty(); t++)
		{
			int weak = 0, resist = 0;
			for (auto* m : team)
			{
				const int e = effOn((Type)t, sp(rom, m->species));
				weak += e > 4; resist += e < 4;
			}
			if (weak >= 2 && weak * 2 >= (int)team.size() && !resist)
			{
				holes++;
				def.why.push_back(typeName((Type)t) + " hits " + std::to_string(weak) + " of them hard, nobody resists it");
			}
		}
		def.score = std::max(0, 25 - 5 * holes);
		// 3. levels (25) against the next fight's ace: 3 points per level below it (the three strongest count)
		ScorePart lv; lv.name = "Levels";
		std::vector<int> levels;
		for (auto* m : team) levels.push_back(m->level);
		std::sort(levels.rbegin(), levels.rend());
		int sum = 0, n = std::min<int>(3, (int)levels.size());
		for (int i = 0; i < n; i++) sum += levels[i];
		const int ours = n ? sum / n : 0, ace = foeAce(next, foe);
		if (ace) { lv.score = std::max(0, std::min(25, 25 - 3 * (ace - ours))); lv.why.push_back("your best three average Lv " +
			std::to_string(ours) + ", the ace is Lv " + std::to_string(ace)); }
		else { lv.score = levels.empty() ? 0 : 25; lv.why.push_back("no big fight known yet"); }
		// 4. moves (25): per Pokémon, a STAB attack (0.5), two attacking types (0.25), a strong attack for its level (0.25)
		ScorePart mv; mv.name = "Moves";
		float pts = 0;
		for (auto* m : team)
		{
			const Species* s = sp(rom, m->species);
			bool stab = false, strong = false;
			std::set<Type> types;
			for (int k = 0; k < 4; k++)
			{
				const MoveData* d = move(rom, m->moves[k]);
				if (!damaging(d) || (m->pp[k] == 0)) continue;
				types.insert(d->type);
				if (s && (d->type == s->types[0] || d->type == s->types[1]) && d->power >= 40) stab = true;
				if (d->power >= (m->level >= 30 ? 70 : 50)) strong = true;
			}
			const float p = (stab ? 0.5f : 0) + (types.size() >= 2 ? 0.25f : 0) + (strong ? 0.25f : 0);
			pts += p;
			if (!stab) mv.why.push_back(m->nick + " has no attack of its own type");
		}
		mv.score = team.empty() ? 0 : (int)(25 * pts / team.size() + 0.5f);
		a.scoreParts = { cov, def, lv, mv };
		a.teamScore = cov.score + def.score + lv.score + mv.score;
	}

	static void problems(const RomData& rom, const Live& l, const Milestone* next, const Trainer* foe, Advice& a)
	{
		if (!next) return;
		// who it is, for the sentences: the leader, the foe's name, or just "the next foe"
		const std::string who = !next->leader.empty() ? next->leader : !next->foeName.empty() && next->foeName != "Cedric" &&
			next->foeName != "Rival" ? next->foeName : std::string("the next foe");
		if (foe)
		{
			// our move types that do nothing to part of the foe's team
			std::map<Type, int> useless;
			for (Type x : ourMoveTypes(rom, l))
				for (auto& f : foe->team) if (effOn(x, sp(rom, f.species)) == 0) useless[x]++;
			std::string list;
			int nu = 0;
			for (auto& u : useless) { list += (nu++ ? " and " : "") + typeName(u.first); }
			if (nu) a.nextProblems.push_back(list + " moves can't touch " + (useless.size() == 1 && useless.begin()->second ==
				(int)foe->team.size() ? who + "'s whole team" : "part of " + who + "'s team"));
			// their attacking types that hit most of us hard
			std::set<Type> theirs;
			for (auto& f : foe->team)
			{
				if (auto* s = sp(rom, f.species)) { theirs.insert(s->types[0]); theirs.insert(s->types[1]); }
				for (auto mvid : f.moves) if (auto* d = move(rom, mvid); damaging(d)) theirs.insert(d->type);
			}
			int team = 0;
			for (auto& m : l.party) team += !m.egg;
			for (Type t : theirs)
			{
				if (t >= NUM_TYPES) continue;
				int weak = 0;
				for (auto& m : l.party) if (!m.egg && effOn(t, sp(rom, m.species)) > 4) weak++;
				if (team && weak * 2 > team) a.nextProblems.push_back(typeName(t) + " attacks hit " + std::to_string(weak) + " of your " +
					std::to_string(team) + " hard");
			}
		}
		int best = 0;
		for (auto& m : l.party) if (!m.egg) best = std::max<int>(best, m.level);
		const int ace = foeAce(next, foe);
		if (ace && best && best < ace)
			a.nextProblems.push_back("your strongest is Lv " + std::to_string(best) + ", " + who + "'s ace is Lv " + std::to_string(ace));
	}

	// can the player meet this encounter now? (the rod, Surf, the Poké Radar, Headbutt; swarms and the GBA slot not:
	// which swarm is on isn't known, and BananaDS has no GBA cartridge in slot 2)
	static bool canMeet(const Encounter& e, const Live& l)
	{
		auto knows = [&](uint16_t mv) { for (auto& m : l.party) for (auto x : m.moves) if (x == mv) return true; return false; };
		if (e.season != 0xff && e.season != l.season) return false;
		switch (e.kind)
		{
		case EncounterKind::OLD_ROD: return inBag(l, 445);
		case EncounterKind::GOOD_ROD: return inBag(l, 446);
		case EncounterKind::SUPER_ROD: return inBag(l, 447);
		case EncounterKind::SURF: return inBag(l, 422);
		case EncounterKind::ROCK_SMASH: return inBag(l, 425);
		case EncounterKind::RADAR: return inBag(l, 431);
		case EncounterKind::HEADBUTT: return knows(29);
		case EncounterKind::SWARM: case EncounterKind::DUAL_SLOT: return false;
		default: return true;
		}
	}

	static void catchNow(const RomData& rom, const Live& l, const GuideData* g, const Trainer* foe, Advice& a)
	{
		if (!g) return;
		const std::set<uint16_t> zones = reachableZones(*g, l);
		const std::set<Type> mt = ourMoveTypes(rom, l);
		std::set<uint16_t> have;
		for (auto& m : l.party) have.insert(m.species);
		std::map<uint16_t, CatchSuggestion> best;	// by species
		for (uint16_t z : zones)
		{
			auto loc = rom.zones.find(z);
			if (loc == rom.zones.end()) continue;
			for (auto& e : loc->second.encounters)
			{
				if (!e.species || have.count(e.species) || hasBit(l.dexCaught, e.species) || !canMeet(e, l)) continue;
				const Species* s = sp(rom, e.species);
				if (!s) continue;
				float v = 0;
				std::string why;
				// its own types against the next fight's team where we have nothing super effective
				if (foe)
				{
					int fills = 0;
					Type by = NO_TYPE;
					for (auto& f : foe->team)
					{
						const Species* fs = sp(rom, f.species);
						bool covered = false;
						for (Type x : mt) if (effOn(x, fs) > 4) covered = true;
						if (covered) continue;
						for (Type t : { s->types[0], s->types[1] }) if (effOn(t, fs) > 4) { fills++; by = t; break; }
					}
					if (fills) { v += 2.0f * fills; why = "its " + typeName(by) + " attacks beat " + std::to_string(fills) +
						" of the next team where you have nothing"; }
				}
				// a type the team doesn't have yet
				bool newType = true;
				for (auto& m : l.party) if (auto* ms = sp(rom, m.species))
					if (ms->types[0] == s->types[0] || ms->types[1] == s->types[0]) newType = false;
				if (newType) { v += 0.5f; if (why.empty()) why = "a " + typeName(s->types[0]) + " type your team doesn't have"; }
				if (v <= 0) continue;
				v += e.percent / 100.0f;		// (the commoner the easier)
				if (e.time != TimeOfDay::ANY && e.time != l.timeOfDay) v -= 0.3f;	// (another time of day: a little later)
				auto& cs = best[e.species];
				if (v > cs.value)
				{
					cs.species = e.species; cs.zone = z; cs.time = e.time; cs.value = v; cs.why = why;
					cs.where = loc->second.name;
				}
			}
		}
		for (auto& b : best) a.catchNow.push_back(b.second);
		std::sort(a.catchNow.begin(), a.catchNow.end(), [](const CatchSuggestion& x, const CatchSuggestion& y)
			{ return x.value != y.value ? x.value > y.value : x.species < y.species; });
		if (a.catchNow.size() > 8) a.catchNow.resize(8);
	}

	static void tmPlan(Gen gen, const RomData& rom, const Live& l, const Trainer* foe, Advice& a)
	{
		auto it = l.bag.find(POCKET_TMS);
		if (it == l.bag.end()) return;
		const std::set<Type> mt = ourMoveTypes(rom, l);
		for (auto& b : it->second)
		{
			const int n = tmNumber(gen, b.item);
			if (!n || (size_t)n > rom.tmMoves.size()) continue;
			const MoveData* d = move(rom, rom.tmMoves[n - 1]);
			if (!damaging(d)) continue;
			std::string bestWhy;
			float bestV = 0;
			for (auto& m : l.party)
			{
				const Species* s = sp(rom, m.species);
				if (!s || m.egg || std::find(s->tms.begin(), s->tms.end(), (uint16_t)n) == s->tms.end()) continue;
				bool knows = false;
				int bestSame = 0;
				for (auto mv : m.moves)
				{
					if (mv == rom.tmMoves[n - 1]) knows = true;
					if (auto* o = move(rom, mv); damaging(o) && o->type == d->type) bestSame = std::max<int>(bestSame, o->power);
				}
				if (knows) continue;
				float v = 0;
				std::string why;
				if (foe)
				{
					int fills = 0;
					for (auto& f : foe->team)
					{
						const Species* fs = sp(rom, f.species);
						bool covered = false;
						for (Type x : mt) if (effOn(x, fs) > 4) covered = true;
						if (!covered && effOn(d->type, fs) > 4) fills++;
					}
					if (fills) { v += 2.0f * fills; why = "beats " + std::to_string(fills) + " of the next team you can't hit hard now"; }
				}
				const bool stab = d->type == s->types[0] || d->type == s->types[1];
				if (stab && d->power > bestSame) { v += 1 + (d->power - bestSame) / 40.0f;
					if (why.empty()) why = "a stronger " + typeName(d->type) + " attack of its own type"; }
				if (!mt.count(d->type)) { v += 0.5f; if (why.empty()) why = "a " + typeName(d->type) + " attack the team doesn't have"; }
				if (v > bestV) { bestV = v; bestWhy = m.nick + ": " + why; }
			}
			if (bestV > 0) a.tmPlan.push_back({ b.item, bestWhy });
		}
	}

	static void battleAdvice(Gen g, const RomData& rom, const Live& l, Advice& a)
	{
		const Battle& b = l.battle;
		if (b.kind == BattleKind::NONE || !b.ours[0].present || !b.foes[0].present) return;
		const Fighter me = fighterOf(b.ours[0], rom), foe = fighterOf(b.foes[0], rom);
		// the foe's move to compare speed against: its first attack without priority (a wild one's are in the battle
		// copy), else its first attack, else none. (Its first attack alone made a priority move the yardstick: Black 2's
		// Krookodile, Speed 117, read "slower" than a Tranquill with Quick Attack.)
		MoveData theirs;
		bool found = false;
		for (auto m : b.foes[0].mon.moves) if (auto* d = move(rom, m); damaging(d) && (int8_t)d->priority == 0) { theirs = *d; found = true; break; }
		if (!found) for (auto m : b.foes[0].mon.moves) if (auto* d = move(rom, m); damaging(d)) { theirs = *d; break; }
		float bestAvg = -1;
		size_t bestI = 0;
		for (int k = 0; k < 4; k++)
		{
			const MoveData* d = move(rom, b.ours[0].mon.moves[k]);
			if (!d) continue;
			MoveVerdict v;
			v.move = b.ours[0].mon.moves[k];
			const DamageRange r = damage(g, *d, me, foe);
			v.quarters = r.quarters;
			const float hp = std::max<int>(1, foe.maxHp);
			v.dmgLo = 100.0f * r.lo / hp; v.dmgHi = 100.0f * r.hi / hp;
			v.movesFirst = movesFirst(g, *d, me, theirs, foe) > 0;
			if (!r.note.empty()) v.note = r.note;
			else if (r.lo >= foe.hp) v.note = "KO";
			else if (r.hi >= foe.hp) v.note = "may KO";
			else if (r.hi > 0) { const int hits = (foe.hp + r.hi - 1) / r.hi; v.note = std::to_string(hits) + "+ hits"; }
			if (b.ours[0].mon.pp[k] == 0) v.note = "no PP left";
			else if (r.hi > 0 && (r.lo + r.hi) / 2.0f > bestAvg) { bestAvg = (r.lo + r.hi) / 2.0f; bestI = a.moves.size(); }
			a.moves.push_back(v);
		}
		if (bestAvg > 0) a.moves[bestI].best = true;
		if (b.kind != BattleKind::WILD && b.kind != BattleKind::DOUBLE_WILD) return;
		// catching: each ball in the bag
		auto bi = l.bag.find(POCKET_BALLS);
		if (bi == l.bag.end()) return;
		CatchContext c;
		c.time = l.timeOfDay;
		c.caughtBefore = hasBit(l.dexCaught, b.foes[0].mon.species);
		for (auto x : l.dexCaught) c.dexCaught += __builtin_popcount(x);
		c.ourLevel = b.ours[0].mon.level; c.ourSpecies = b.ours[0].mon.species; c.ourFemale = b.ours[0].mon.female;
		for (auto& ball : bi->second)
		{
			CatchChance cc;
			cc.ball = ball.item;
			cc.percent = 100 * catchChance(g, rom, b.foes[0].mon, ball.item, c);
			if (ball.item == I_QUICK) cc.tip = "on the first turn only";
			else if (ball.item == I_TIMER) cc.tip = "better the longer the battle lasts";
			else if (ball.item == I_DUSK && l.timeOfDay != TimeOfDay::NIGHT) cc.tip = "x3.5 at night and in caves";
			else if (ball.item == I_DIVE) cc.tip = "x3.5 on water";
			a.catching.push_back(cc);
		}
		std::sort(a.catching.begin(), a.catching.end(), [](const CatchChance& x, const CatchChance& y) { return x.percent > y.percent; });
		// what would raise it
		if (!a.catching.empty() && a.catching[0].percent < 50)
		{
			const Mon& w = b.foes[0].mon;
			std::string t;
			if (!(w.status & 0xFF)) t = g == Gen::IV ? "sleep or freeze doubles it" : "sleep or freeze: x2.5";
			if (w.hp * 2 > w.maxHp) t += std::string(t.empty() ? "" : "; ") + "lower its HP";
			a.catching[0].tip = a.catching[0].tip.empty() ? t : a.catching[0].tip + "; " + t;
		}
	}

	static void notices(const RomData& rom, const Live& l, const GuideData* g, const Milestone* next, const Advice& adv,
		const AdviceState& st, std::vector<Notice>& out)
	{
		auto add = [&](const std::string& id, const std::string& page, const std::string& title, const std::string& text)
		{
			if (!st.seen.count(id)) out.push_back({ id, page, title, text });
		};
		if (next) add("next:" + next->id, "guide", "Next: " + next->name, next->tips);
		// the best Pokémon to catch, when it lives right here
		for (auto& c : adv.catchNow)
			if (c.zone == l.zone)
			{
				add("catch:" + std::to_string(c.zone) + ":" + std::to_string(c.species), "team", speciesName(rom, c.species) +
					" lives here", c.why);
				break;
			}
		// a roamer on this route (Gen IV keeps its place) (not verified on a game yet: AdviceState::unverifiedNotices)
		if (st.unverifiedNotices)
			for (auto& r : l.roamers)
				if (r.active && r.zone && r.zone == l.zone)
					add("roamer:" + std::to_string(r.species) + ":" + std::to_string(r.zone), "here", speciesName(rom, r.species) + " is on this route", "");
		// secrets and hidden items here
		if (g)
		{
			int n = 0;
			for (auto& s : g->secrets)
			{
				// (one with a condition, "after Red", is not known to be reachable now: on the Here page, not the badge)
				if (!s.condition.empty()) continue;
				bool here = s.zone == l.zone && l.zone;
				for (auto z : s.zones) if (z == l.zone && l.zone) here = true;
				n += here;
			}
			if (n) add("secrets:" + std::to_string(l.zone), "here", std::to_string(n) + (n == 1 ? " secret here" : " secrets here"), "");
		}
		// a party Pokémon that can evolve now (not verified on a game yet: AdviceState::unverifiedNotices)
		if (st.unverifiedNotices)
			for (auto& m : l.party)
				for (auto& e : evolutionWatch(rom.game.gen, m, rom, l, g))
					if (e.ready)
						add("evolve:" + std::to_string(m.pid) + ":" + std::to_string(e.into), "party", m.nick + " can evolve",
							"into " + speciesName(rom, e.into));
	}

	Advice computeAdvice(const RomData& rom, const Live& live, const GuideData* guide, const AdviceState& st)
	{
		Advice a;
		if (!live.valid) return a;
		const Gen g = rom.game.gen;
		const Milestone* next = guide ? nextMilestone(*guide, live) : nullptr;
		a.next = next;
		const Trainer* foe = foeTrainer(rom, next);
		battleAdvice(g, rom, live, a);
		teamScore(rom, live, next, foe, a);
		problems(rom, live, next, foe, a);
		catchNow(rom, live, guide, foe, a);
		tmPlan(g, rom, live, foe, a);
		notices(rom, live, guide, next, a, st, a.notices);
		return a;
	}
}
