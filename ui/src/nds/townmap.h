#pragma once
// banana-ui: the DS Pokémon companion's region maps (model.h RegionMap, MapSpot): the town map as each game draws it,
// read from the player's own ROM, and where every zone is on it.
//
//   Diamond/Pearl/Platinum: graphic/tmap_gra.narc (top screen: palette 0, characters 19, screens 22 + 24), the town
//     map's blocks (data/tmap_block.dat: {u16 x, u16 z, ...} on the main map matrix, 7 px a square) and the main
//     matrix's map headers (fielddata/mapmatrix/map_matrix.narc 0). The layout is pret's pokeplatinum
//     (applications/town_map: TOWN_MAP_GRID_*, TownMapBlock, the Mt. Coronet squares that aren't on the matrix).
//   HeartGold/SoulSilver: the Pokégear map (a/1/4/4: 8-bit characters 10, screen 11's first 47 x 20 tiles, palette
//     20), one 8 px tile a matrix square two rows down (pret's pokeheartgold application/pokegear/map), the main
//     matrix (a/0/4/1 0) and the map headers' town map square for the maps that aren't on it (MapHeader worldMapX/Y).
//     Split into Johto and Kanto as the Pokégear does (Pokegear_RegionFromCoords).
//   Black/White, Black 2/White 2: the town map (BW a/0/8/5, B2W2 a/0/8/4: 8-bit characters 7 under screen 16, the
//     routes and towns 5 under 17, palette 0) and its places (BW a/0/8/6, B2W2 a/0/8/5: 54-byte records {u16 zone,
//     u16, u16 x, u16 y, ..., u16 kind @22}: found in the ROMs, 2026-11-03), the main matrix (a/0/0/9 0) and the zone
//     headers' parent zone (a/0/1/2 @24).
// Gen IV's houses, gates and caves (not on the matrix) get the square their way out leads to: the zones' warps
// (fielddata/eventdata/zone_event*, HGSS a/0/3/2; pokeplatinum map_header_data.h), as the game's own town map shows the
// player where they went in. Gen V's get their parent zone's spot; what is left, the spot of a place with its name.
// Our own implementation from those descriptions; nothing of the games is in BananaOS.
#include "nds/model.h"
#include "nds/nitro.h"
#include <string>

namespace banana::nds::townmap
{
	// the game's town map(s) into out.regionMaps and every out.zones[...].spot (out.zones and out.locationNames must
	// be read already). headers: Gen IV: where the map header table is in arm9 (24 bytes an entry); unused for Gen V.
	// false (why set): the town map isn't where this game keeps it; out has no maps then (no Map page, not an error)
	bool read(const nitro::Rom& rom, const nitro::Bytes& arm9, size_t headers, RomData& out, std::string* why = nullptr);

	// where on the spot's map picture the marker goes for the player at Live::mapX/mapY (model.h MapSpot); false: the
	// spot is on no map
	bool marker(const MapSpot& s, uint16_t mapX, uint16_t mapY, float& x, float& y);

	// the decode cache (rom.cpp's file): the maps and the spots, appended; load reads them back from at (advanced)
	void save(const RomData& r, nitro::Bytes& out);
	bool load(RomData& r, const uint8_t* p, size_t n, size_t& at);
}
