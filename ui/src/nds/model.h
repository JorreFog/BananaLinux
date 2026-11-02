#pragma once
// banana-ui: the DS Pokémon companion's data model (docs/superpowers/specs/2026-11-02-nds-pokemon-companion-design.md).
// The contract between the workstreams: the ROM reader (rom.*) fills RomData from the player's own ROM, the RAM
// reader (ram.*) fills Live from the running game through CompLink, the logic (logic.*) works out Advice from both
// plus the guide data (guide.* reading /usr/share/banana/companions/nds-pokemon/<game>.json), and the pages (pages.*)
// draw them. Nothing of the games is in BananaOS: every name, number, sprite and text below is read at run time.
//
// The idea of a live companion on a handheld is PokeDaisy's (https://github.com/lidor30/pokedaisy, Lidor Itzhari).
// Change this file only with the coordinator: four workstreams compile against it.
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace banana::nds
{
	// ---- the games -------------------------------------------------------------------------------------------
	enum class Gen { IV, V };
	enum class GameId { DIAMOND, PEARL, PLATINUM, HEARTGOLD, SOULSILVER, BLACK, WHITE, BLACK2, WHITE2, NONE };
	struct Game
	{
		GameId id = GameId::NONE;
		Gen gen = Gen::IV;
		std::string code;		// the header's game code (0x0C, 4 letters: CPUE, IPKE, IREO ...)
		std::string title;		// English name for the companion's header ("Platinum")
		uint8_t revision = 0;	// header 0x1E
		bool dsiEnhanced = false;
		int dexSize = 493;		// 493 (Gen IV), 649 (Gen V)
		std::string romHash;	// cache key (the ROM's SHA-1 of the header + ARM9, cheap and unique)
	};
	// the game in a ROM header (the first 0x200 bytes); NONE: not one of the nine
	Game detectGame(const uint8_t* header, size_t n);

	// ---- types and moves -------------------------------------------------------------------------------------
	enum Type : uint8_t { NORMAL, FIGHTING, FLYING, POISON, GROUND, ROCK, BUG, GHOST, STEEL, FIRE, WATER, GRASS,
		ELECTRIC, PSYCHIC, ICE, DRAGON, DARK, NUM_TYPES, NO_TYPE = 255 };	// (the DS games' order: no ??? type here)
	int effectiveness(Type atk, Type def1, Type def2);	// in quarters: 0, 1, 2, 4, 8, 16
	enum class MoveCategory : uint8_t { PHYSICAL, SPECIAL, STATUS };
	struct MoveData { std::string name; Type type = NORMAL; MoveCategory cat = MoveCategory::STATUS; uint8_t power = 0,
		accuracy = 0, pp = 0, priority = 0; uint16_t effect = 0; uint8_t effectChance = 0; std::string description; };

	// ---- species (the ROM's personal data, names, learnsets, evolutions, Pokédex) -----------------------------
	struct Evolution { uint16_t method = 0, param = 0, into = 0; std::string how; };	// how: our words, from method+param
	struct LevelMove { uint8_t level = 0; uint16_t move = 0; };
	struct Species
	{
		std::string name, category, dexText;	// category: "Flame" (ROM text); dexText in the game's language
		Type types[2] = { NORMAL, NORMAL };
		uint8_t base[6] = {};				// HP, Atk, Def, Speed, SpA, SpD (personal data order)
		uint16_t abilities[3] = {};			// [2]: Gen V hidden ability (0 none)
		uint8_t catchRate = 0, growth = 0, genderRatio = 0, baseFriendship = 0;
		uint8_t evYield[6] = {};
		uint16_t baseExp = 0, height = 0, weight = 0;
		std::vector<Evolution> evolutions;
		std::vector<LevelMove> learnset;
		std::vector<uint16_t> tms;			// TM/HM numbers it can learn (1-based; HMs after the TMs)
		uint16_t formsAt = 0; uint8_t formCount = 1;	// RomData::species index of form 1 (forms 1..count-1 follow)
	};
	// a sprite: indexed frames + a normal and a shiny palette (BGR555 -> 0xRRGGBB), and the animation (Gen V: the
	// game's own NANR sequence; Gen IV: its two frames alternating)
	struct Sprite
	{
		int w = 0, h = 0;
		std::vector<std::vector<uint8_t>> frames;	// 8-bit indices, w*h each (0 = transparent)
		std::array<uint32_t, 16> normal{}, shiny{};
		struct Step { uint16_t frame = 0, ms = 0; };
		std::vector<Step> anim;						// empty: a still picture (frames[0])
	};

	// ---- places, encounters, trainers, items --------------------------------------------------------------
	enum class TimeOfDay : uint8_t { ANY, MORNING, DAY, NIGHT };
	enum class EncounterKind : uint8_t { GRASS, SURF, OLD_ROD, GOOD_ROD, SUPER_ROD, ROCK_SMASH, HEADBUTT, SWARM,
		RADAR, DUAL_SLOT, SEASON, GROTTO, SPECIAL,
		DARK_GRASS, SHAKING_GRASS, RIPPLE_SURF, RIPPLE_FISH,	// Gen V
		HOENN_SOUND, SINNOH_SOUND, NIGHT_FISH };				// HGSS (the radio's sounds, fishing at night)
	struct Encounter { uint16_t species = 0; uint8_t minLevel = 0, maxLevel = 0, percent = 0; EncounterKind kind =
		EncounterKind::GRASS; TimeOfDay time = TimeOfDay::ANY; uint8_t season = 0xff; uint8_t form = 0;
		// DUAL_SLOT: the GBA game in slot 2 (1 Ruby 2 Sapphire 3 Emerald 4 FireRed 5 LeafGreen); SWARM: 0 grass 1 surf
		// 2 fishing; HEADBUTT: the tree kind
		uint8_t condition = 0; };
	struct Location { uint16_t id = 0; std::string name; std::vector<Encounter> encounters; };	// by map / zone id
	struct TrainerMon { uint16_t species = 0, item = 0; uint8_t level = 0; uint16_t moves[4] = {}; };
	struct Trainer { uint16_t id = 0; std::string cls, name; std::vector<TrainerMon> team; bool doubleBattle = false; };
	struct ItemData { std::string name, description; uint8_t pocket = 0; uint16_t price = 0; };

	// everything read from the ROM (the ROM reader's product; decoded once and cached on the card by romHash)
	struct RomData
	{
		Game game;
		std::vector<Species> species;			// index = national dex number (0 = none); forms after dexSize
		std::vector<MoveData> moves;
		std::vector<std::string> abilities, natures;
		std::vector<std::string> abilityText;
		std::vector<ItemData> items;
		std::vector<std::string> locationNames;	// by met-location / map name id
		std::map<uint16_t, Location> zones;		// by the game's zone/map id (encounters)
		std::map<uint16_t, Trainer> trainers;
		std::vector<uint16_t> tmMoves;			// TM/HM number-1 -> move
		// sprites on demand (decoding all is slow): species, female, shiny-agnostic (the palettes are in Sprite)
		virtual std::shared_ptr<const Sprite> sprite(uint16_t species, bool female = false, uint8_t form = 0) const = 0;
		virtual ~RomData() = default;
	};

	// ---- live state (the RAM reader's product, refreshed a few times a second) --------------------------------
	struct Mon
	{
		uint32_t pid = 0;
		uint16_t species = 0, item = 0, ability = 0;
		uint8_t form = 0, level = 0, friendship = 0, pokerus = 0;
		std::string nick, otName;
		uint16_t otId = 0, otSecret = 0;
		uint32_t exp = 0;
		uint16_t hp = 0, maxHp = 0, stats[5] = {};	// Atk, Def, Speed, SpA, SpD
		uint8_t ivs[6] = {}, evs[6] = {};			// HP, Atk, Def, Speed, SpA, SpD
		uint16_t moves[4] = {};
		uint8_t pp[4] = {}, ppUps[4] = {};
		uint32_t status = 0;						// the games' status word (sleep turns, PSN, BRN, FRZ, PAR, TOX)
		uint16_t metLocation = 0; uint8_t metLevel = 0, ball = 0;
		bool egg = false, female = false;
		uint8_t nature = 0;							// Gen IV: pid % 25; Gen V: its own byte
		bool shiny() const;							// (pid, otId, otSecret)
		int hiddenPowerType() const, hiddenPowerPower() const;
	};
	struct Battler { Mon mon; Type types[2] = { NORMAL, NORMAL }; int8_t stages[8] = {}; bool present = false; };
	enum class BattleKind : uint8_t { NONE, WILD, TRAINER, DOUBLE_WILD, DOUBLE_TRAINER };
	struct Battle
	{
		BattleKind kind = BattleKind::NONE;
		Battler ours[2], foes[2];
		uint16_t trainerId = 0;					// the opponent (RomData::trainers), 0 wild
		std::vector<Mon> foeParty;				// a trainer's whole team as the game holds it
		uint8_t weather = 0;
		uint32_t turn = 0;
	};
	// the bag's pockets in Live::bag below (the same keys for both generations; Gen V keeps its Poké Balls among the items)
	enum Pocket : uint8_t { POCKET_ITEMS, POCKET_MEDICINE, POCKET_BALLS, POCKET_TMS, POCKET_BERRIES, POCKET_MAIL,
		POCKET_BATTLE, POCKET_KEY };

	struct BagItem { uint16_t item = 0, count = 0; };
	struct Roamer { uint16_t species = 0, zone = 0; uint8_t level = 0; uint16_t hp = 0; bool active = false; };
	struct Live
	{
		bool valid = false;						// the game recognised and the reads checked (checksums)
		std::string trainerName; uint16_t tid = 0, sid = 0;
		std::vector<Mon> party;
		Battle battle;
		uint16_t zone = 0, mapX = 0, mapY = 0;	// where the player is
		uint32_t money = 0, playSeconds = 0;
		uint16_t badges = 0;					// bits
		std::vector<uint8_t> dexSeen, dexCaught;	// bit sets by national number
		std::map<uint8_t, std::vector<BagItem>> bag;	// pocket -> items
		std::vector<Roamer> roamers;
		std::vector<Mon> daycare; uint32_t eggSteps = 0;
		TimeOfDay timeOfDay = TimeOfDay::DAY; uint8_t season = 0, weekday = 0;
		uint16_t repelSteps = 0;
		std::vector<uint8_t> flags;				// story/event flags (for the guide's progress), as the game keeps them
	};

	// ---- guide data (ours, per game: /usr/share/banana/companions/nds-pokemon/<game>.json) ----------------------
	enum class Quality : uint8_t { LIVE_VERIFIED, REFERENCE_ONLY };
	// (guide.h loads these from the per-game JSON; the schema is in tools/companions/nds-validate.py and docs/COMPANIONS.md.
	// "zones" are the game's own map/zone ids: where a file only names a place, the loader looks the id up by that name
	// in the ROM's location names (guide.h: GuideResolver); the names that found no id are in GuideData::unresolved)
	struct GuideStep { std::string id, title, text; std::vector<uint16_t> zones; std::string doneWhen; Quality q = Quality::REFERENCE_ONLY;
		std::string stage, source; std::vector<std::string> places; };	// stage: the Milestone id it is shown during
	struct Milestone { std::string id, name, place; std::vector<uint16_t> trainerIds; Type theme = NO_TYPE;
		std::string doneWhen; std::vector<uint16_t> reachableZones; Quality q = Quality::REFERENCE_ONLY;		// gyms, E4, rivals, champion
		std::string kind;				// gym rival team elite4 champion postgame legendary story
		std::string leader, tips, badgeNote, source;
		int levelCap = 0;				// the highest level on the foe's side
		int badge = 0;					// done_when "badge:N": the Nth badge bit (0 = not a badge milestone)
		std::string foeName, foeClass; int foeAceLevel = 0;		// how the trainer is found in the ROM (when no trainerIds)
		std::vector<uint16_t> placeZones;			// the milestone's own place
		std::vector<std::string> reachablePlaces; };	// the same reachable set as names (zones the ROM had no id for too)
	struct Secret { std::string id, text; uint16_t zone = 0; int x = -1, y = -1; std::string condition; Quality q = Quality::REFERENCE_ONLY;
		std::string kind, place, source; std::vector<uint16_t> zones;
		std::string grid = "map"; };		// x/y on the place's own map grid ("map") or the overworld's global tile grid ("world")
	struct GuideNote { std::string id, kind, title, text, place, source; std::vector<uint16_t> species, zones; Quality q = Quality::REFERENCE_ONLY; };
	struct GuideData
	{
		std::vector<Milestone> milestones;		// in story order
		std::vector<GuideStep> steps;
		std::vector<Secret> secrets;			// hidden items, events, tips tied to a place
		std::map<uint16_t, std::string> evolutionNotes;	// species -> how to get it in this game (trades, items, places)
		std::vector<GuideNote> notes;			// version exclusives, roamers, grottos, gifts, legendaries, trades
		std::vector<std::string> sources;
		std::string game, title; int revision = 0;
		std::vector<std::string> unresolved;	// place names the ROM gave no zone id for
	};

	// ---- the journey log and challenge mode (the logic's product, kept per game + trainer on the card) ----------
	enum class LogKind : uint8_t { CATCH, EVOLVE, BADGE, FIRST, LOST, HATCH, NOTE, GIFT };	// (GIFT: received, not caught)
	struct LogEntry { LogKind kind = LogKind::NOTE; uint32_t playSeconds = 0; int64_t when = 0;	// unix s, 0 unknown
		uint16_t species = 0, zone = 0; std::string text; };								// text: our words, names filled
	struct RouteRecord { uint16_t zone = 0, species = 0; enum Outcome : uint8_t { OPEN, CAUGHT, MISSED } outcome = OPEN; };
	struct Journey { std::vector<LogEntry> log; bool challenge = false; std::vector<RouteRecord> routes;	// log: oldest first
		std::vector<Mon> lost; };

	// ---- advice (the logic's product, for the pages and the badge) --------------------------------------------
	struct MoveVerdict { uint16_t move = 0; int quarters = 4; float dmgLo = 0, dmgHi = 0; bool best = false;
		bool movesFirst = false; std::string note; };
	struct CatchChance { uint16_t ball = 0; float percent = 0; std::string tip; };
	struct ScorePart { std::string name; int score = 0, max = 25; std::vector<std::string> why; };
	struct CatchSuggestion { uint16_t species = 0; uint16_t zone = 0; TimeOfDay time = TimeOfDay::ANY;
		std::string where, why; float value = 0; };
	struct Notice { std::string id, page, title, text; };	// a badge item; id stays the same for the same thing
	struct Advice
	{
		std::vector<MoveVerdict> moves;			// the active battler's moves against the first foe
		std::vector<CatchChance> catching;		// wild battles
		int teamScore = 0; std::vector<ScorePart> scoreParts;
		const Milestone* next = nullptr; std::vector<std::string> nextProblems;
		std::vector<CatchSuggestion> catchNow;
		std::vector<std::pair<uint16_t, std::string>> tmPlan;	// TM item -> who and what gap
		std::vector<Notice> notices;			// unseen ones raise the badge
	};
}
