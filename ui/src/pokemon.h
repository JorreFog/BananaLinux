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
//    pokefirered). Another game, or another language, shows nothing: the panel is as before.
//  - names, types, move data: read from the player's own ROM file once at the start (found by their shape, not by an
//    address, so any revision works): nothing of the games is in Banana.
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
		PSYCHIC, ICE, DRAGON, DARK, NUM_TYPES };
	const char* typeName(int t);		// English ("" for none / out of range)
	uint32_t typeColor(int t);			// 0xRRGGBB for the type's chip
	// one attacking type against a defender's one or two types (t2 == t1 or 255: one), in quarters: 0 no effect,
	// 1 a quarter, 2 half, 4 normal, 8 double, 16 four times
	int effect(int atk, int t1, int t2);

	struct GameInfo
	{
		const char* code;		// the header's four letters
		const char* name;
		uint32_t partyCount, party, battleMons, battleFlags, mainFlags, enemyParty;	// RAM addresses
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
	// a party slot (100 bytes): false when empty, or when the checksum is wrong (a slot being written, a wrong address)
	bool decodeParty(const uint8_t* p, Mon& m);
	// one battler of the battle's own copy (0x58 bytes, not encrypted)
	bool decodeBattler(const uint8_t* p, Mon& m);
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
		std::string speciesName(uint16_t i) const;
		std::string moveName(uint16_t i) const;
		// 0 male, 1 female, 2 neither (or not known)
		int gender(uint16_t species, uint32_t pid) const;
		// the experience a level needs (-1: not known)
		long long expFor(uint16_t species, int level) const;
		// a species' icon as 32x32 RGBA (false: none in the ROM's tables); egg: the egg's
		bool iconRGBA(uint16_t species, bool egg, std::vector<uint8_t>& rgba) const;
	};
	RomTables findTables(const uint8_t* rom, size_t n);

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
		bool changed = false;
		void finish();
	};
}
