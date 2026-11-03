#pragma once
// banana-ui: the GBA Pokémon games' running state for the DS companion's pages (docs/superpowers/specs/
// 2026-11-03-gba-pokemon-companion-design.md). The reads are today's GBA companion's (pokemon.h: pkmn::Companion,
// its Snapshot of the party, the battle and the save's data); this turns one Snapshot into the nds::Live the pages,
// the logic and the journey log work from - species by national number, types in nds order, items in Gen IV's
// numbering (as the Gen III RomData has them), the bag's pockets as nds Pockets, the place as a zone id
// (map group << 8 | map number, as the Gen III RomData keys its Locations).
#include "../nds/model.h"
#include "../pokemon.h"
#include <cstdint>
#include <functional>
#include <memory>

namespace banana::gba
{
	// a Gen III type id (pkmn: ??? at 9) in nds::Type's order (NO_TYPE for ??? and anything else). fairy: the game's
	// Fairy type's id (pkmn::RomTables::Features::fairyType: CFRU's 23, a Glazed release's 9)
	nds::Type ndsType(uint8_t t, int fairy = pkmn::FAIRY);
	// the item ids: Gen III's to the numbering the RomData's items use (Gen IV's); identity when not given
	using ItemMap = std::function<uint16_t(uint16_t)>;
	// the species: the game's own index to the RomData's national number and form (false: none); when not given, the
	// tables' national numbers (gen3rom.h: speciesOf)
	using SpeciesMap = std::function<bool(uint16_t, uint16_t&, uint8_t&)>;

	// one Snapshot as Live (valid: a party was read). tables: the ROM's (natDex, genders); rom: the Gen III RomData
	// (abilities by species), either may be null (then species are passed as they are and abilities left 0)
	std::shared_ptr<nds::Live> toLive(const pkmn::Snapshot& s, const pkmn::RomTables* tables, const nds::RomData* rom,
		const ItemMap& item = nullptr, const SpeciesMap& species = nullptr);
	// one Pokémon (a party member or a battler)
	nds::Mon toMon(const pkmn::Mon& m, const pkmn::RomTables* tables, const nds::RomData* rom, const ItemMap& item, bool party,
		const SpeciesMap& species = nullptr);
}
