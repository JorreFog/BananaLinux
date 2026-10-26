#pragma once
// banana-ui: the Pokémon companion on the game panel (Settings > Other systems > Pokémon companion). While a Game Boy
// Advance Pokémon game plays in RetroArch on one panel, the other shows the party (levels, HP, status, moves and their
// PP) and, in a battle, the foe with its types and how each of the lead's moves does against it.
//
// The idea is PokeDaisy's (https://github.com/lidor30/pokedaisy, by Lidor Itzhari): a live companion for the Pokémon
// games on a handheld's second screen. None of its code is here (it is GPL-3.0, Banana MIT): the layouts below are
// from the games' public documentation and decompilations.
//
// Where it comes from:
//  - the game: the ROM header's code (0xAC: AXVE Ruby, AXPE Sapphire, BPEE Emerald, BPRE FireRed, BPGE LeafGreen; the
//    English releases, whose RAM addresses below are the decompilations' symbol files: pret/pokeruby, pokeemerald,
//    pokefirered). Another game, or another language, shows nothing: the panel is as before. ROM hacks keep their base
//    game's code: a FireRed one built on CFRU (Skeli's Complete Fire Red Upgrade: Pokémon Unbound and many others) is
//    told apart by its tables (more than 412 species, the Fairy type in the base stats) and read with them.
//  - names, types, move data: read from the player's own ROM file once at the start: through the pointers in the
//    games' own ROM header at 0x100 (Emerald, FireRed, LeafGreen and the hacks built on them, which keep those pointers
//    up to date when they move a table), else found by their shape (Ruby, Sapphire). Nothing of the games is in
//    Banana; the guide's goals (who leads which gym) are a few words of ours.
//  - the party and the battle: RetroArch's READ_CORE_MEMORY command (the core's memory map: mGBA and gpSP both give
//    EWRAM 0x02000000 and IWRAM 0x03000000), sent on its stdin with the game panel's commands and answered on its
//    stdout (run-retroarch sends that to a pipe when BANANA_RA_OUT is set), four times a second. Not the UDP network
//    command interface: that listens on every address, Wi-Fi too.
// Data layouts: Bulbapedia's "Pokémon data structure (Generation III)" and "Character encoding (Generation III)".
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace banana::pkmn
{
	enum { NORMAL, FIGHTING, FLYING, POISON, GROUND, ROCK, BUG, GHOST, STEEL, MYSTERY, FIRE, WATER, GRASS, ELECTRIC,
		PSYCHIC, ICE, DRAGON, DARK, FAIRY = 23, NUM_TYPES };	// (CFRU: Fairy at 23, 18-22 unused)
	const char* typeName(int t);		// English ("" for none / out of range)
	uint32_t typeColor(int t);			// 0xRRGGBB for the type's chip
	// one attacking type against a defender's one or two types (t2 == t1 or 255: one), in quarters: 0 no effect,
	// 1 a quarter, 2 half, 4 normal, 8 double, 16 four times. modern: the chart since Generation VI (Fairy; Steel no
	// longer resists Ghost and Dark), which CFRU games use
	int effect(int atk, int t1, int t2, bool modern = false);
	bool typeInGame(int t, bool modern);	// a type a move or a Pokémon can have in that game

	struct GameInfo
	{
		const char* code;		// the header's four letters
		const char* name;
		uint32_t partyCount, party, battleMons, battleFlags, mainFlags, enemyParty;	// RAM addresses
		int gen;				// 0 Ruby/Sapphire, 1 Emerald, 2 FireRed/LeafGreen (the save's layout)
		uint32_t sb1, sb2;		// the save blocks: their addresses (Ruby/Sapphire), or where the pointers to them are
		uint32_t mapHeader;		// gMapHeader (its region map section at 0x14)
	};
	const GameInfo* detectGame(const uint8_t* header, size_t n);	// the ROM's first bytes (0xB0 at least)

	std::string decodeText(const uint8_t* p, size_t max);			// the games' Western character set, to UTF-8

	struct Mon
	{
		uint16_t species = 0, item = 0;
		std::string nick;
		uint8_t level = 0, ppBonuses = 0;
		uint16_t hp = 0, maxHp = 0;
		uint32_t status = 0;	// sleep turns (bits 0-2), poison 3, burn 4, freeze 5, paralysis 6, bad poison 7
		uint16_t moves[4] = {};
		uint8_t pp[4] = {};
		uint8_t types[2] = { 255, 255 };	// in a battle (the battle's own copy); else from the ROM's table
		bool egg = false;
		uint32_t pid = 0, exp = 0;			// personality (gender, with the species' ratio), experience points
	};
	// a party slot (100 bytes): false when empty, or when the checksum is wrong (a slot being written, a wrong address).
	// coding: the games' own (encrypted with the personality and trainer ID, its four parts in one of 24 orders), or a
	// CFRU hack's (Pokémon Unbound...: CFRU's byte replacements take the encryption and the shuffle out), or either
	enum class MonCoding { ENCRYPTED, PLAIN, EITHER };
	bool decodeParty(const uint8_t* p, Mon& m, MonCoding coding = MonCoding::ENCRYPTED);
	// one battler of the battle's own copy (0x58 bytes, not encrypted); species above maxSpecies: none
	bool decodeBattler(const uint8_t* p, Mon& m, int maxSpecies = 450);
	const char* statusName(uint32_t status);	// "SLP", "PSN", ... or "" (none)
	int maxPP(int basePP, uint8_t ppBonuses, int slot);

	struct MoveInfo { uint8_t power = 0, type = 0, accuracy = 0, pp = 0; };
	struct RomTables
	{
		std::vector<std::string> species;	// by the games' species index (0: none)
		std::vector<std::string> moves;
		std::vector<std::array<uint8_t, 2>> speciesTypes;
		std::vector<MoveInfo> moveInfo;
		std::vector<uint8_t> genderRatio, growth;		// by species, from the base stats (0x10, 0x13)
		std::vector<std::array<uint32_t, 101>> expTable;	// the six growth rates' experience at each level
		// the party menu icons: 32x32, 4 bits a pixel (the first of their two frames, as palette indices, 0 clear), by
		// species (412 the egg); their palette (0-2) and the three palettes (RGBA, colour 0 clear)
		std::vector<std::vector<uint8_t>> icons;
		std::vector<uint8_t> iconPal;
		std::vector<std::array<uint32_t, 16>> palettes;
		// what the other pages need (empty: not found)
		bool cfru = false;					// a CFRU hack: expanded tables, the modern type chart
		bool plainMons = false;				// the Pokémon data not encrypted (CFRU's patch to GetSubstruct and co.)
		uint32_t dexRam = 0;				// CFRU: the Pokédex's seen flags (RAM, fixed; 0 the games' own), then caught
		int dexBytes = 0;					// each that many bytes
		bool plainBag = false;				// CFRU's "Expanded Bag": the pockets where gBagPockets points, counts not XORed
		int numSpecies = 412;				// the species table's length (with none at 0)
		std::vector<std::string> abilities;
		struct Item { std::string name; uint8_t pocket = 0; };
		std::vector<Item> items;
		std::vector<std::string> mapNames;	// by region map section (MAPSEC); "" none
		struct Grid { int w = 0, h = 0; std::vector<uint8_t> cells; };	// the region map: a MAPSEC a cell
		std::vector<Grid> grids;
		uint8_t mapsecNone = 0xff;
		std::vector<uint16_t> natDex;		// species -> national Pokédex number (0 none)
		std::vector<uint16_t> dexSpecies;	// national number -> species
		std::vector<std::string> leaders;	// CFRU: the gym leaders, weakest team first (the order they are met)
		std::vector<int> leaderTypes;		// and the type most of their Pokémon have, over all their battles
		std::shared_ptr<const std::vector<uint8_t>> rom;	// the whole ROM, for the look-ups below
		long dexEntries = -1, wildHeaders = -1, trainers = -1;
		int dexStride = 32, numTrainers = 0;
		std::string speciesName(uint16_t i) const;
		std::string moveName(uint16_t i) const;
		// 0 male, 1 female, 2 neither (or not known)
		int gender(uint16_t species, uint32_t pid) const;
		// the experience a level needs (-1: not known)
		long long expFor(uint16_t species, int level) const;
		// a species' icon as 32x32 RGBA (false: none in the ROM's tables); egg: the egg's
		bool iconRGBA(uint16_t species, bool egg, std::vector<uint8_t>& rgba) const;
		std::string itemName(uint16_t i) const;
		std::string mapName(int mapsec) const;
		// a Pokédex entry by national number: false when the ROM has none
		struct DexEntry { std::string category, text; int height = 0, weight = 0; };	// (decimetres, hectograms)
		bool dexEntry(int nat, DexEntry& e) const;
		// the wild Pokémon of a map, by how they are met (0 grass, 1 water, 2 rock smash, 3 fishing): species, levels
		// and the chance in percent (the slots of one species added up)
		struct Wild { uint16_t species; uint8_t minLv, maxLv; int percent; };
		std::vector<Wild> wild(int group, int num, int method) const;
		// a trainer's team (the first in the table with that name and, when given, that class)
		struct TeamMon { uint16_t species; uint8_t level; };
		std::vector<TeamMon> team(const std::string& name, int* trainerClass = nullptr) const;
		bool layoutSize(uint32_t layoutPtr, int& w, int& h) const;	// a map layout's size (from the map header)
	};
	RomTables findTables(const uint8_t* rom, size_t n);

	// the guide's goals: the gym leaders and the league in the order they are met (who, where, their type; rom: the
	// trainer's name in the ROM, for the team), by the game's gen
	struct Goal { const char* rom; const char* who; const char* where; int type; };
	const Goal* goalsFor(int gen, int& count);

	// one line of RetroArch's stdout: true for a READ_CORE_MEMORY reply with data (an error reply: true, bytes empty)
	bool parseReply(const std::string& line, uint32_t& addr, std::vector<uint8_t>& bytes);

	// what the panel draws (the last complete set of replies)
	struct Snapshot
	{
		bool seen = false;			// a reply with data has come
		std::string error;			// RetroArch's reply when it had none ("no memory map defined"...)
		std::vector<Mon> party;
		bool inBattle = false, doubles = false;
		Mon lead;					// the player's battler (battler 0)
		std::vector<Mon> foes;		// battler 1 (and 3 in a double battle), still standing
		std::vector<Mon> foeParty;	// the other side's party (its count and who has fainted)
		// the save's own data (read while the Map, Bag, Dex or Guide page is open: ext)
		bool ext = false;
		std::string player;
		int group = -1, num = -1, x = 0, y = 0, mapsec = -1;	// where the player is
		uint32_t layout = 0;		// the map's layout (its size, for the place on the region map)
		long money = -1;
		struct Slot { uint16_t item, count; };
		std::vector<Slot> pockets[5];	// Items, Key Items, Poké Balls, TMs & HMs, Berries
		bool bag = false;
		std::vector<uint8_t> dexOwned, dexSeen;	// the Pokédex: a bit a national number (from 1)
		int badges = -1;				// the eight badges' bits (-1 not read)
		bool champion = false;
	};

	class Companion
	{
	public:
		// the game in rom (a .gba or a .zip with one): false, and nothing to do, when it isn't one the companion knows.
		// The ROM's tables are read on a thread.
		bool open(const std::string& rom);
		void close();
		bool active() const { return info != nullptr; }
		const GameInfo* game() const { return info; }
		// the commands for RetroArch's stdin now ("" none): a set every 250 ms, the next once the last was answered
		// (RetroArch answers in its menu and paused too; asked again after 10 s without, never piling sets up)
		std::string requests(double nowMs);
		void feed(const char* data, size_t n);	// RetroArch's stdout
		bool takeChanged() { bool c = changed; changed = false; return c; }
		const Snapshot& snapshot() const { return snap; }
		std::shared_ptr<const RomTables> tables() const;	// null until read
		// (tests: the ROM's tables from memory)
		void setTables(RomTables t);
		void openWith(const GameInfo* g) { close(); info = g; }
		// the pages open on the panel that need the save's data (Map, Bag, Dex, Guide): read only while one is
		void wantExt(bool on) { ext = on; }
	private:
		const GameInfo* info = nullptr;
		struct Shared { std::mutex mu; std::shared_ptr<const RomTables> tables; bool ready = false; };
		std::shared_ptr<Shared> sh;
		bool tablesSeen = false;
		double nextAt = 0, sentAt = 0, errorSince = -1;
		int waiting = 0;			// replies still to come for the set sent
		bool wantBattle = false;	// the battle read in this set
		std::string part;
		Snapshot snap, next;
		std::vector<uint8_t> count, party, flags, mainB, battle, enemy;
		bool ext = false, wantSave = false;
		bool bagCfru = false;		// the set's bag read through gBagPockets (a CFRU hack's own bag)
		uint32_t bagSeen[5] = {}; int bagCap[5] = {};	// its pockets, as the last read of gBagPockets had them
		std::vector<uint8_t> bagPtrB, pocketB[5];
		int dexCfru = 0;			// the set's Pokédex read: a CFRU hack's own flags, each that many bytes (0 the games')
		uint32_t sbUsed[2] = {}, sbSeen[2] = {};	// the save blocks' addresses the set's reads used, and the latest read
		std::vector<uint8_t> sbp, sb1h, mh, bagB, moneyB, keyB, dexB, flagB, nameB;
		struct Want { uint32_t addr; int len; std::vector<uint8_t>* into; bool done; };
		std::vector<Want> wants;
		bool changed = false;
		void finish();
		void saveData(Snapshot& s);
	};
}
