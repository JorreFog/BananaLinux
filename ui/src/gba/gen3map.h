#pragma once
// banana-ui: the GBA Pokémon games' region maps for the companion's Map page (nds::RegionMap, Location::spot): the
// games' own pictures and their grids of map sections (pokemap.cpp, the tables pkmn::findTables read), each zone's
// rectangle on them (the cells of its section) and its size in tiles (its map layout). Live::mapX/mapY are the
// player's tile on the zone's own map (SaveBlock1's position), so the marker goes inside the rectangle by them.
#include "../nds/model.h"
#include "../pokemon.h"

namespace banana::gba
{
	// fills r.regionMaps and r.zones[...].spot from t (the zones already made: keyed group << 8 | num)
	void fillRegionMaps(const pkmn::RomTables& t, const nds::Game& g, nds::RomData& r);
}
