#pragma once
// banana-ui: the DS Pokémon companion's advice (model.h: Advice) from the ROM's data (RomData), the running game
// (Live, ram.h) and our guide (GuideData): how much each move does to the foe, who moves first, the catch chance per
// ball, the team score, the next big fight and its problems, what to catch now, the TM plan, evolution watch, EV totals
// and the notices that raise the icon's badge.
//
// The formulas are the games' own as documented (Bulbapedia's "Damage", "Catch rate", "Stat" pages and the pret
// decompilations, written here from the rules, not copied): Generation IV and V each their own way of rounding.
// Damage is shown as a range over the game's random factor (85-100 %), critical hits left out; abilities and held
// items are counted where the rule is a plain multiplier (the list is in logic.cpp). What can't be known from RAM
// (the foe's held item in a wild battle, weather in Gen V, the battle's turn count) is left at its neutral value and the
// note says so.
#include "nds/model.h"
#include "nds/ram.h"
#include <set>
#include <string>
#include <vector>

namespace banana::nds
{
	// ---- damage -------------------------------------------------------------------------------------------------
	struct Fighter
	{
		uint16_t species = 0, ability = 0, item = 0;
		uint8_t level = 1;
		uint16_t hp = 1, maxHp = 1, stats[5] = {};	// Atk, Def, Speed, SpA, SpD (the battle's own, before stages)
		int8_t stages[8] = {};						// HP, Atk, Def, Speed, SpA, SpD, Acc, Eva: -6..+6
		Type types[2] = { NORMAL, NO_TYPE };
		uint32_t status = 0;						// the games' status word (bit 4 burn, bit 6 paralysis)
	};
	// a Battler (Live) as a Fighter; Gen V battlers carry no types: then the species' from the ROM
	Fighter fighterOf(const Battler& b, const RomData& rom);
	// a party Pokémon as a Fighter (its stats as the game computed them)
	Fighter fighterOf(const Mon& m, const RomData& rom);
	// a ROM trainer's Pokémon at its level with the trainer's IVs (Gen IV scale: 0-255 -> 0-31) and no EVs
	Fighter fighterOf(const TrainerMon& t, const RomData& rom, int ivs = 0);
	enum class Weather : uint8_t { NONE, RAIN, SUN, SAND, HAIL };
	struct DamageRange { int lo = 0, hi = 0; int quarters = 4; bool stab = false; std::string note; };
	DamageRange damage(Gen g, const MoveData& m, const Fighter& att, const Fighter& def, Weather w = Weather::NONE);
	// the Speed the game compares (stages, paralysis, Choice Scarf, Swift Swim/Chlorophyll in their weather)
	int effectiveSpeed(Gen g, const Fighter& f, Weather w = Weather::NONE);
	// 1: a moves first, -1: b, 0: a tie (random)
	int movesFirst(Gen g, const MoveData& ma, const Fighter& a, const MoveData& mb, const Fighter& b, Weather w = Weather::NONE);
	// a stat from base, IV, EV, level, nature (stat: 0 HP, 1 Atk, 2 Def, 3 Speed, 4 SpA, 5 SpD)
	int statValue(int stat, int base, int iv, int ev, int level, int nature);

	// ---- catching -------------------------------------------------------------------------------------------------
	struct CatchContext
	{
		TimeOfDay time = TimeOfDay::DAY;
		bool cave = false, surfingOrFishing = false, fishing = false;
		int turn = 1;					// the battle's turn (1 = the first); unknown -> 1
		bool caughtBefore = false;		// this species in the Pokédex as caught (Repeat Ball)
		int dexCaught = 0;				// Gen V's critical capture
		uint8_t ourLevel = 0;			// (Level Ball)
		uint16_t ourSpecies = 0; bool ourFemale = false;	// (Love Ball)
	};
	// the chance (0..1) that one throw of `ball` catches the wild Pokémon; *mult (optional) the ball's multiplier used
	float catchChance(Gen g, const RomData& rom, const Mon& wild, uint16_t ball, const CatchContext& c, float* mult = nullptr);

	// ---- the rest of the advice ------------------------------------------------------------------------------------
	// the badge bits a milestone's done_when names, and whether it's done
	bool milestoneDone(const Milestone& m, const Live& live, const GuideData& g, size_t index);
	// a guide step whose done_when the game shows as done ("party", "badge:N", "flag:N"): not shown any more
	bool stepDone(const GuideStep& s, const Live& live);
	// the first milestone not done (nullptr: all done or no guide)
	const Milestone* nextMilestone(const GuideData& g, const Live& live);
	// the zones reachable now: everything the milestones up to and including the next one open
	std::set<uint16_t> reachableZones(const GuideData& g, const Live& live);

	// TM/HM item -> its number (1-based, HMs after the TMs as RomData::tmMoves and Species::tms count); 0: not a TM
	int tmNumber(Gen g, uint16_t item);

	// evolution watch for one party Pokémon
	struct EvoStatus
	{
		uint16_t into = 0;
		uint16_t method = 0, param = 0;
		int levelsToGo = -1;			// level evolutions: levels left (0: at the next level up)
		int friendshipToGo = -1;		// friendship evolutions: points left to 220
		bool ready = false;				// the condition holds now (a level evolution: the level is reached)
		bool needsItem = false;			// a stone / held item (param), in the bag or not
		bool haveItem = false;
		std::string how;				// RomData's words for the method + the guide's note for the species
	};
	std::vector<EvoStatus> evolutionWatch(Gen g, const Mon& m, const RomData& rom, const Live& live, const GuideData* guide);
	int evTotal(const Mon& m);			// 510 is the most a Pokémon can have

	// what the logic remembers between calls (notices seen, the last battle's foe for the EV tracker)
	struct AdviceState
	{
		std::set<std::string> seen;		// notice ids the player has seen (kept on the card per game + trainer)
		// the badge's roamer: and evolve: notices: not yet seen right on a running game (no roamer met, no party member
		// ready to evolve in the validation runs), so off for shipping; next:, catch: and secrets: were checked
		bool unverifiedNotices = false;
	};
	// everything at once (a few times a second at most)
	Advice computeAdvice(const RomData& rom, const Live& live, const GuideData* guide, const AdviceState& st);
}
