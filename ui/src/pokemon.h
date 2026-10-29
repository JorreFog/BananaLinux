#pragma once
// banana-ui: the Pokémon companion on the game panel (Settings > Other systems > Pokémon companion). While a Game Boy
// Advance Pokémon game plays in RetroArch on one panel, the other shows the party (levels, HP, status, moves and their
// PP; a member's stats, IVs, EVs, nature and ability) and, in a battle, the foe with its types and how each of the lead's moves does against it.
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
//    told apart by its tables (more than 412 species, the Fairy type in the base stats) and read with them. A game
//    is a GameProfile (its addresses, its save's and its Pokémon's layouts); a hack that moves its base game's RAM
//    gets one of its own, told by the ROM's CRC-32. What a game has beyond its base game is RomTables::Features.
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

	// how a game keeps a Pokémon in RAM. A party slot: 100 bytes, encrypted with the personality and trainer ID, its
	// four parts in one of 24 orders (the games'), or plain (CFRU's byte replacements take the encryption and the
	// shuffle out: Pokémon Unbound...), or either (not known yet)
	enum class MonCoding { ENCRYPTED, PLAIN, EITHER };
	// the battle's own copy of a battler (BattlePokemon: not encrypted), where each field is (the games': 0x58 bytes)
	struct BattlerLayout
	{
		int size = 0x58;
		int moves = 0x0c, types = 0x21, pp = 0x24, hp = 0x28, level = 0x2a, maxHp = 0x2c, item = 0x2e, nick = 0x30;
		int ppBonuses = 0x3b, exp = 0x44, pid = 0x48, status = 0x4c;
		int eggByte = 0x17, eggBit = 0x40;		// (the IV word's egg bit)
	};
	struct MonLayout
	{
		int size = 100;							// a party slot's bytes
		MonCoding coding = MonCoding::ENCRYPTED;
		uint16_t speciesMask = 0xffff;			// the species' bits in its halfword (a hack may pack more in beside it)
		BattlerLayout battler;
	};

	// the base game a game is (or a hack is built on): the save's layout, the region and its goals
	enum Family { RUBY_SAPPHIRE, EMERALD, FIRERED };
	// where the save keeps the bag, the money and the flags (SaveBlock1), and the key its item counts and money are
	// XORed with and the Pokédex (SaveBlock2; Ruby and Sapphire have no key)
	struct SaveLayout
	{
		uint32_t flags; int badge0, clear; uint32_t money, key;
		uint32_t pocket[5]; int count[5];		// Items, Key Items, Poké Balls, TMs & HMs, Berries
		uint32_t dexOwned = 0x28;				// owned[52], then seen[52]
	};
	// a game, or one release of it: what the companion reads and where. Told by the header's code, and by the ROM's
	// CRC-32 for a profile that names one (a hack that keeps its base game's code but not its RAM: the one with the
	// CRC is preferred over the code's own)
	struct GameProfile
	{
		const char* code;		// the header's four letters
		const char* name;
		uint32_t crc;			// 0: any ROM with the code
		Family family;
		uint32_t partyCount, party, battleMons, battleFlags, mainFlags, enemyParty;	// RAM addresses
		bool fixedSave;			// the save blocks at sb1, sb2 (Ruby, Sapphire); else sb1 and sb2 hold pointers to them
		uint32_t sb1, sb2;
		uint32_t mapHeader;		// gMapHeader (its region map section at 0x14)
		SaveLayout save;
		MonLayout mon;
	};
	// the ROM's first bytes (0xB0 at least); crc: the whole ROM's CRC-32 when known (0 not)
	const GameProfile* detectGame(const uint8_t* rom, size_t n, uint32_t crc = 0);
	// the same in a list of one's own (tests)
	const GameProfile* detectGameIn(const GameProfile* list, size_t count, const uint8_t* rom, size_t n, uint32_t crc);
	// whether a profile for the ROM's code is told by its CRC-32 (else not worth reading the whole ROM for)
	bool crcMatters(const uint8_t* rom, size_t n);

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
		// a party member's (not a battler's): its stats (HP is maxHp), IVs and EVs, friendship, which of the species'
		// two abilities it has (1 the second)
		uint16_t stats[5] = {};				// Attack, Defense, Speed, Sp. Atk, Sp. Def (the games' order)
		uint8_t ivs[6] = {}, evs[6] = {};	// HP, Attack, Defense, Speed, Sp. Atk, Sp. Def
		uint8_t friendship = 0, abilitySlot = 0;
		int nature() const { return (int)(pid % 25); }
	};
	// a party slot: false when empty, or when the checksum is wrong (a slot being written, a wrong address)
	bool decodeParty(const uint8_t* p, Mon& m, MonCoding coding = MonCoding::ENCRYPTED);
	bool decodeParty(const uint8_t* p, Mon& m, const MonLayout& layout);
	// one battler of the battle's own copy; species above maxSpecies: none
	bool decodeBattler(const uint8_t* p, Mon& m, int maxSpecies = 450, const BattlerLayout& layout = BattlerLayout());
	const char* statusName(uint32_t status);	// "SLP", "PSN", ... or "" (none)
	const char* natureName(int nature);		// English, by personality % 25 ("" out of range)
	// the stat a nature raises and the one it lowers, as Mon::stats numbers them (0 Attack ... 4 Sp. Def): -1 for both
	// when it changes neither (Hardy, Docile, Serious, Bashful, Quirky)
	void natureStats(int nature, int& up, int& down);
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
		// what the game has beyond its base game's, which the pages and the reads follow (found in the ROM: a CFRU hack's
		// tables and patches)
		struct Features
		{
			bool modernTypes = false;			// the type chart since Generation VI, with Fairy
			bool ownDex = false;				// a Pokédex past the 386 national numbers
			bool leadersFromTrainers = false;	// the gym leaders found in the trainer table (Leader class), not our goals
			bool plainMons = false;				// the Pokémon not encrypted (CFRU's patch to GetSubstruct and co.)
			uint32_t dexRam = 0;				// the Pokédex's seen flags (RAM, fixed; 0 the save's own), then caught
			int dexBytes = 0;					// each that many bytes
			uint32_t bagPockets = 0;			// the bag where gBagPockets (here) points, its counts not XORed (CFRU's
												// "Expanded Bag"; 0 the save's own)
			bool bagUnsure = false;				// the bag perhaps not the save's own, and not found elsewhere either
		};
		Features has;
		// what the other pages need (empty: not found)
		int numSpecies = 412;				// the species table's length (with none at 0)
		std::vector<std::string> abilities;
		std::vector<std::array<uint8_t, 2>> speciesAbilities;	// by species, from the base stats (0x16, 0x17)
		struct Item { std::string name; uint8_t pocket = 0; };
		std::vector<Item> items;
		std::vector<std::string> mapNames;	// by region map section (MAPSEC); "" none
		struct Grid { int w = 0, h = 0; std::vector<uint8_t> cells; };	// the region map: a MAPSEC a cell
		std::vector<Grid> grids;
		// the games' own region map pictures (pokemap.cpp), decoded once from the ROM: RGBA, with where the grid's cell
		// 0,0 is on it (8 pixels a cell); by grid: the picture it lies on (-1: none, the squares are drawn)
		struct MapImage { int w = 0, h = 0, cellX = 0, cellY = 0; std::vector<uint8_t> rgba; };
		std::vector<MapImage> mapImages;
		std::vector<int> gridImage;
		uint8_t mapsecNone = 0xff;
		std::vector<uint16_t> natDex;		// species -> national Pokédex number (0 none)
		std::vector<uint16_t> dexSpecies;	// national number -> species
		std::vector<std::string> leaders;	// has.leadersFromTrainers: the gym leaders, weakest team first (the order they are met)
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
		// a party member's ability ("" not known): the species' second when slot is 1 and it has one, else its first
		std::string abilityName(const Mon& m) const;
		std::string mapName(int mapsec) const;
		// a Pokédex entry by national number: false when the ROM has none
		struct DexEntry { std::string category, text; int height = 0, weight = 0; };	// (decimetres, hectograms)
		bool dexEntry(int nat, DexEntry& e) const;
		// the wild Pokémon of a map, by how they are met (0 grass, 1 water, 2 rock smash, 3 the old rod, 4 the good rod,
		// 5 the super rod): species, levels
		// and the chance in percent (the slots of one species added up)
		struct Wild { uint16_t species; uint8_t minLv, maxLv; int percent; };
		std::vector<Wild> wild(int group, int num, int method) const;
		// a trainer's team (the first in the table with that name and, when given, that class)
		struct TeamMon { uint16_t species; uint8_t level; };
		std::vector<TeamMon> team(const std::string& name, int* trainerClass = nullptr) const;
		bool layoutSize(uint32_t layoutPtr, int& w, int& h) const;	// a map layout's size (from the map header)
	};
	RomTables findTables(const uint8_t* rom, size_t n);

	// pokemap.cpp: the GBA BIOS's LZ77 (type 0x10) at rom[at]: false when it isn't one, runs past the end or decodes to
	// more than maxOut; used: the compressed bytes read
	bool lz77(const uint8_t* rom, size_t n, size_t at, std::vector<uint8_t>& out, size_t maxOut, size_t* used = nullptr);
	// the region map pictures for t.grids (gen: the game's Family)
	void findRegionMaps(RomTables& t, const uint8_t* rom, size_t n, int gen);

	// the guide's goals: the gym leaders and the league in the order they are met (who, where, their type; rom: the
	// trainer's name in the ROM, for the team), by the game's family
	struct Goal { const char* rom; const char* who; const char* where; int type; };
	const Goal* goalsFor(Family family, int& count);

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
		const GameProfile* game() const { return info; }
		// the commands for RetroArch's stdin now ("" none): a set every 250 ms, the next once the last was answered
		// (RetroArch answers in its menu and paused too; asked again after 10 s without, never piling sets up)
		std::string requests(double nowMs);
		void feed(const char* data, size_t n);	// RetroArch's stdout
		bool takeChanged() { bool c = changed; changed = false; return c; }
		const Snapshot& snapshot() const { return snap; }
		std::shared_ptr<const RomTables> tables() const;	// null until read
		// (tests: the ROM's tables from memory)
		void setTables(RomTables t);
		void openWith(const GameProfile* g) { close(); info = g; }
		// the pages open on the panel that need the save's data (Map, Bag, Dex, Guide): read only while one is
		void wantExt(bool on) { ext = on; }
	private:
		const GameProfile* info = nullptr;
		// (the tables, and the profile the whole ROM's CRC-32 tells, from the thread)
		struct Shared { std::mutex mu; std::shared_ptr<const RomTables> tables; const GameProfile* profile = nullptr; bool ready = false; };
		std::shared_ptr<Shared> sh;
		bool tablesSeen = false;
		double nextAt = 0, sentAt = 0, errorSince = -1;
		int waiting = 0;			// replies still to come for the set sent
		bool wantBattle = false;	// the battle read in this set
		std::string part;
		Snapshot snap, next;
		std::vector<uint8_t> count, party, flags, mainB, battle, enemy;
		bool ext = false, wantSave = false;
		uint32_t bagPockets = 0;	// the set's bag read through gBagPockets, here (0 the save's)
		uint32_t bagSeen[5] = {}; int bagCap[5] = {};	// its pockets, as the last read of gBagPockets had them
		std::vector<uint8_t> bagPtrB, pocketB[5];
		int dexOwn = 0;				// the set's Pokédex read: the game's own flags in RAM, each that many bytes (0 the save's)
		uint32_t sbUsed[2] = {}, sbSeen[2] = {};	// the save blocks' addresses the set's reads used, and the latest read
		std::vector<uint8_t> sbp, sb1h, mh, bagB, moneyB, keyB, dexB, flagB, nameB;
		struct Want { uint32_t addr; int len; std::vector<uint8_t>* into; bool done; };
		std::vector<Want> wants;
		bool changed = false;
		void finish();
		void saveData(Snapshot& s);
	};
}
