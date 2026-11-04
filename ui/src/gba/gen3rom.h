#pragma once
// banana-ui: the GBA Pokémon games' ROM data for the DS companion's pages (docs/superpowers/specs/
// 2026-11-03-gba-pokemon-companion-design.md): Ruby, Sapphire, Emerald, FireRed and LeafGreen (English) as an
// nds::RomData, in the DS conventions the pages and the logic work with - species by national number, types in
// nds::Type's order, items in Gen IV's numbering, TMs as Gen IV numbers them (TM01-50 = 1-50, HM01-08 = 93-100),
// evolution methods in rom.h's nds::evo numbering, places keyed by zone = map group << 8 | map number.
//
// Everything is read from the player's own ROM at run time: today's GBA companion's tables (pokemon.h:
// pkmn::RomTables - names, Pokédex, wild tables, learnsets, map groups) and the tables below, found through the games'
// own ROM header (Emerald, FireRed, LeafGreen: pokeemerald/pokefirered's rom_header_gf) or by their shape (Ruby,
// Sapphire), each checked by its first entries. Layouts: the pret decompilations (pokeruby, pokeemerald, pokefirered)
// and Bulbapedia; nothing of their code is here.
#include "../nds/model.h"
#include "../pokemon.h"
#include <cstdint>
#include <memory>
#include <string>

namespace banana::gba
{
	// the game in a GBA ROM (its first 0xC0 bytes are enough for the code; the whole ROM for the hack check and the
	// hash): one of the five English releases (header 0xAC: AXVE AXPE BPEE BPRE BPGE, any revision at 0xBC), Gen III,
	// dexSize 386; or one of the ROM hacks gba/hacks.h knows, by the whole ROM's CRC-32 (crc: when known - the
	// companion's tables carry it, pkmn::RomTables::crc -, else 0 and it is worked out here: ~10 ms a 32 MB ROM on a
	// PC): GameId::HACK, Game::hack its key, its title, its Pokédex's size, modern when it has the Fairy type. NONE for
	// anything else, and for another CFRU hack (more species, the Fairy type: it keeps today's pages)
	nds::Game detectGame(const uint8_t* rom, size_t n, uint32_t crc = 0);

	// the whole RomData, from the ROM and today's tables (t.rom holds the whole ROM). Runs on a background thread (tens
	// of ms on a PC); sprite() is safe from any thread. cacheDir: unused (the reads are fast enough without one).
	// Null when the ROM is not one of the five or a hack read here (the Glazed releases: Emerald patched; Gaia: CFRU),
	// or a table isn't found.
	std::shared_ptr<const nds::RomData> readRomData(const pkmn::RomTables& t, const nds::Game& g, const std::string& cacheDir);

	// the game's own numbers in the RomData's (for the running game's state, gen3live): a species index -> its national
	// number and form (false: none such), an item id -> the RomData's (a hack's own items by their names). A RomData not
	// from readRomData: the retail games' rules
	bool speciesOf(const nds::RomData& r, uint16_t internal, uint16_t& national, uint8_t& form);
	uint16_t itemId(const nds::RomData& r, uint16_t gen3Item);

	// a Gen III item id in the numbering RomData::items uses (Gen IV's: Master Ball 1, Choice Band 220, TM01 328, HM01
	// 420). Items the DS games don't have (Gen III's key items, its mail, the unused ids) get GEN3_ONLY + their own id.
	// 0 for 0
	constexpr uint16_t GEN3_ONLY = 1000;
	// a name as the ROM keeps it (capitals) as the DS games write it: "SAND-ATTACK" -> "Sand-Attack", "TM26" stays
	std::string nameCase(const std::string& s);
	uint16_t itemToGen4(uint16_t gen3Item);
}
