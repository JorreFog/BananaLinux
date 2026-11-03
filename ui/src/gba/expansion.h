#pragma once
// banana-ui: GBA Pokémon ROM hacks built from pokeemerald-expansion (RHH's decompilation base: Heart & Soul, Lazarus,
// Seaglass, SoulGold - hacks.cpp's Engine::EXPANSION). Their structs, tables and RAM addresses are their own build's:
// each is a different expansion version (the ROM says which, in RHH's header "RHHEXP" at 0x200), most are forks that
// changed structs again (SoulGold keeps its Pokémon unencrypted in 96 bytes, Heart & Soul moved the save's position),
// and every build places RAM differently. So nothing here is a fixed address or size: everything is found in the ROM
// itself at open time and cross-checked:
//  - ROM tables: through the GF header (0x100: gSpeciesInfo, gItemsInfo, gMovesInfo, the save blocks' offsets and the
//    bag's sizes) and the RHH header (counts, gAbilitiesInfo); every record's size and field offsets by the shape of
//    the data (names, pointers to texts and lists, the first species/moves/items); the trainers, wild encounters and
//    map groups by their shape.
//  - RAM: from the code, by what reads it (literal pools): gSpecials[0] (HealPlayerParty) for the party and its count,
//    the u32 most often bit-tested for gBattleTypeFlags, the EWRAM base whose fields HP/max HP/item/ability are read
//    for gBattleMons (and the multiplier next to it for its size), the IWRAM base read with the offset 0x439 for gMain,
//    the pointer pair next to the save's money/PC offsets for gSaveBlock1Ptr/2Ptr, the base whose region map section
//    (0x14) and layout are read for gMapHeader; each with a second method where one exists (ExpansionLayout::checks).
// Layouts from the decompilation (github.com/rh-hideout/pokeemerald-expansion, tags expansion/1.9.x, 1.15.x); nothing
// of its code or of the games is here. Design: docs/superpowers/specs/2026-11-03-gba-pokemon-companion-design.md.
#include "../nds/model.h"
#include "../pokemon.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace banana::gba
{
	// the RHH header's version ("RHHEXP" then major, minor, patch): false when the ROM has none
	bool expansionVersion(const uint8_t* rom, size_t n, int& major, int& minor, int& patch);

	// what was found where in one build (offsets in the ROM; -1 / 0 not found)
	struct ExpansionLayout
	{
		int major = 0, minor = 0, patch = 0;
		int numMoves = 0, numSpecies = 0, numAbilities = 0, numItems = 0;
		uint32_t romSizeFlag = 0;		// (1.15: specials/script commands tagged by + ROM_SIZE)
		// gSpeciesInfo: record size and field offsets
		long species = -1; int spStride = 0, spName = 0, spCategory = 0, spNatDex = 0, spHeight = 0, spWeight = 0, spDesc = 0;
		int spFront = -1, spPal = -1, spShiny = -1, spLearn = -1, spTeach = -1, spEgg = -1, spEvo = -1;
		int abilitySlots = 3;
		int evoStride = 6; bool evoConditions = false;	// (1.11 on: method + condition list, 12 bytes an entry)
		long moves = -1; int mvStride = 0, mvPriority = 0;
		long items = -1; int itStride = 0, itName = 0x14; bool itNamePtr = false; int itPocketByte = 0, itPocketShift = 0;
		uint8_t pocketMap[32] = {};		// the game's pocket value -> nds::Pocket + 1 (0 not known)
		long abilities = -1; int abStride = 0;
		long trainers = -1; int trStride = 0, trParty = 0, trClass = 0, trName = 0, trSize = 0, trBattleType = -1, numTrainers = 0;
		int tmonStride = 0, tmonSpecies = 0x14, tmonItem = 0x16, tmonLevel = 0x1a, tmonMoves = 0x0c;
		long classes = -1; int numClasses = 0;
		long wild = -1; int wildStride = 0, wildTimes = 1, wildKinds = 4;
		long mapsecs = -1; int numMapsecs = 0;	// gRegionMapEntries (8 bytes a section)
		// the save (GF header): SaveBlock1/2 offsets
		uint32_t flagsOffset = 0, varsOffset = 0, dexSeenOffset = 0, dexCount = 0, sb1Size = 0, sb2Size = 0, pcItemsOffset = 0;
		uint32_t gameClearFlag = 0; uint8_t bagCount[5] = {}, pcItemsCount = 0;
		int badgeFlag = 0;				// FLAG_BADGE01_GET (found: a table of the eight, else game clear + 3)
		// RAM
		uint32_t partyCount = 0, party = 0, enemyParty = 0, battleMons = 0, battleFlags = 0, gMain = 0, sb1 = 0, sb2 = 0, mapHeader = 0;
		int monSize = 0, battlerSize = 0, sb1Pos = 0;
		bool plainMons = false;			// (SoulGold: no encryption, its own field layout)
		std::vector<std::string> checks;	// how each was found and what agreed (or didn't)
		std::string why;				// the first thing not found (empty: all found)
		std::string times;				// (how long each part took)
		bool ramOk() const { return partyCount && party && enemyParty && battleMons && battleFlags && gMain && sb1 && sb2 && monSize && battlerSize; }
	};
	// the whole ROM's layout: false when it isn't an expansion build or its tables aren't found (L.why says)
	bool findExpansion(const uint8_t* rom, size_t n, ExpansionLayout& L);

	// the companion's RAM side (pokemon.h): the profile of this build (its addresses and layouts) and the tables the
	// old GBA views and the live reads use, from the whole ROM. False: not an expansion build, or its RAM not found
	bool expansionProfile(const uint8_t* rom, size_t n, std::shared_ptr<const pkmn::GameProfile>& profile, pkmn::RomTables& t);

	// the DS companion's pages: the whole RomData (t.rom: the whole ROM). Null when it isn't an expansion build.
	std::shared_ptr<const nds::RomData> readExpansionRomData(const pkmn::RomTables& t, const nds::Game& g);
	// the item ids of a RomData read here (Gen IV ids where the names match, else GEN3_ONLY + the game's id); null for
	// any other RomData (then gen3rom's itemToGen4)
	std::function<uint16_t(uint16_t)> expansionItemMap(const nds::RomData* rom);

	// (tests) the smol/LZ77 decompression of a sprite sheet (pokeemerald-expansion 1.12 on compresses pictures with its
	// own tANS coder, "smol"): false when the data at rom[at] is neither
	bool decompressPic(const uint8_t* rom, size_t n, size_t at, std::vector<uint8_t>& out, size_t maxOut);
}
