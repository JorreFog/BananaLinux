#pragma once
// banana-ui: the DS Pokémon companion's ROM reader. RomData (model.h) from the player's own cartridge image: game
// detection by the header, then names, species, moves, items, places, wild encounters, trainers and Pokédex text out
// of the game's own files (nitro.h for the containers), sprites decoded when asked for. What it decoded is cached on
// the card by the ROM's hash, so the second start reads one small file instead of the ROM.
//
// Formats: ~/nds-companion/RESEARCH.md §6 (ROM-verified on the nine releases), pret's pokeplatinum / pokeheartgold
// decompilations (map headers, encounter slots and their odds, trainer data, sprite scrambling), PKHeX's
// PersonalInfo5 (Gen V personal data), Universal Pokémon Randomizer ZX's Gen5RomHandler (Gen V records).
#include "nds/model.h"
#include "nds/ram.h"
#include <functional>
#include <memory>
#include <string>

namespace banana::nds
{
	// Evolution::method, in Gen V's numbering for both generations (Gen IV's codes from 7 on are one lower there:
	// Gen V added "trade for a species" at 7)
	namespace evo
	{
		enum : uint16_t { NONE, FRIENDSHIP, FRIENDSHIP_DAY, FRIENDSHIP_NIGHT, LEVEL, TRADE, TRADE_ITEM, TRADE_SPECIES,
			USE_ITEM, LEVEL_ATK_GT_DEF, LEVEL_ATK_EQ_DEF, LEVEL_ATK_LT_DEF, LEVEL_PID_LOW, LEVEL_PID_HIGH, LEVEL_NINJASK,
			LEVEL_SHEDINJA, BEAUTY, USE_ITEM_MALE, USE_ITEM_FEMALE, HOLD_ITEM_DAY, HOLD_ITEM_NIGHT, KNOWS_MOVE,
			SPECIES_IN_PARTY, LEVEL_MALE, LEVEL_FEMALE, MAGNETIC_FIELD, MOSS_ROCK, ICE_ROCK };
	}

	// (the encounter kinds rom.cpp fills, now in model.h; the numbers the cache was written with)
	static_assert((int)EncounterKind::DARK_GRASS == 13 && (int)EncounterKind::NIGHT_FISH == 19, "cache numbering");
	constexpr EncounterKind DARK_GRASS = EncounterKind::DARK_GRASS, SHAKING_GRASS = EncounterKind::SHAKING_GRASS,
		RIPPLE_SURF = EncounterKind::RIPPLE_SURF, RIPPLE_FISH = EncounterKind::RIPPLE_FISH,
		HOENN_SOUND = EncounterKind::HOENN_SOUND, SINNOH_SOUND = EncounterKind::SINNOH_SOUND, NIGHT_FISH = EncounterKind::NIGHT_FISH;

	struct RomLoadInfo
	{
		bool fromCache = false;
		double ms = 0;				// time to decode (or read the cache)
		std::string cacheFile;		// where it was (or would have been) cached
		size_t cacheBytes = 0;
	};

	// Reads the ROM at path. cacheDir: where the decoded data is kept (made if missing; "" = no cache). progress
	// (optional) gets 0..1 while decoding. Returns null with err set when the file is not one of the nine games or
	// its files don't read as expected. Thread: call it off the UI thread (a first decode reads tens of MB);
	// the result's sprite() is safe from any thread.
	std::shared_ptr<RomData> loadRom(const std::string& path, const std::string& cacheDir, std::string* err = nullptr,
		RomLoadInfo* info = nullptr, const std::function<void(float)>& progress = {});

	// what the RAM reader can take from the player's ROM (ram.h RamHints): a piece of the battle overlay's code where the
	// field's overlay is loaded the rest of the time, so finding it in RAM = a battle is on, in any revision. The games
	// load their battle code over the field's (Ironmon's "in battle" word is a Thumb instruction of each: 0x2100 /
	// 0x2800); the overlays' numbers are the games' own (DP 11/6, Pt 16/7, HGSS 12/2, BW 93/21, B2W2 167/36), their
	// places and bytes come from the ROM. Empty when the ROM doesn't have them. Decodes two overlays (a few ms)
	RamHints ramHints(const RomData& rom);

	// the index in RomData::species of a species' form (0: the species itself; forms the game has no data of: the
	// species itself)
	uint16_t speciesIndex(const RomData& rom, uint16_t species, uint8_t form);

	// the cache file's format version (bump when RomData's fields or their meaning change)
	constexpr uint32_t ROM_CACHE_VERSION = 3;
}
