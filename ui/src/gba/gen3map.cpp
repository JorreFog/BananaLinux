// banana-ui: the GBA Pokémon games' region maps (gen3map.h).
#include "gba/gen3map.h"
#include <algorithm>

namespace banana::gba
{
	static uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

	// a picture's name: Hoenn's one, or FireRed/LeafGreen's Kanto and the Sevii Islands' three
	static std::string mapName(const nds::Game& g, int picture)
	{
		if (g.id == nds::GameId::HACK) return "";	// (a hack's own region: its name isn't in the ROM's map tables)
		const bool kanto = g.id == nds::GameId::FIRERED || g.id == nds::GameId::LEAFGREEN;
		if (!kanto) return "Hoenn";
		static const char* const K[] = { "Kanto", "Sevii Islands 1-3", "Sevii Islands 4-5", "Sevii Islands 6-7" };
		return picture >= 0 && picture < 4 ? K[picture] : "Sevii Islands";
	}

	void fillRegionMaps(const pkmn::RomTables& t, const nds::Game& g, nds::RomData& r)
	{
		r.regionMaps.clear();
		const int CELL = 8;
		for (size_t gi = 0; gi < t.grids.size(); gi++)
		{
			const auto& grid = t.grids[gi];
			nds::RegionMap m;
			const int pic = gi < t.gridImage.size() ? t.gridImage[gi] : -1;
			m.name = mapName(g, pic);
			m.cell = CELL;
			m.gridW = grid.w;
			m.gridH = grid.h;
			m.cells.resize(grid.cells.size());
			for (size_t i = 0; i < grid.cells.size(); i++) m.cells[i] = grid.cells[i] == t.mapsecNone ? 0xFFFF : grid.cells[i];
			if (pic >= 0 && pic < (int)t.mapImages.size() && !t.mapImages[pic].rgba.empty())
			{
				const auto& im = t.mapImages[pic];
				m.w = im.w; m.h = im.h; m.cellX = im.cellX; m.cellY = im.cellY;
				m.rgba.resize((size_t)im.w * im.h);
				for (size_t i = 0; i < m.rgba.size(); i++)
				{
					const uint8_t* p = &im.rgba[i * 4];
					m.rgba[i] = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
				}
			}
			else { m.w = grid.w * CELL; m.h = grid.h * CELL; }	// (no picture: the page draws the squares)
			r.regionMaps.push_back(std::move(m));
		}
		if (r.regionMaps.empty()) return;
		// each zone: the cells of its section on the first map that has them, and its map's size in tiles
		for (auto& kv : r.zones)
		{
			const int gr = kv.first >> 8, num = kv.first & 0xff;
			if (gr >= (int)t.mapsecs.size() || num >= (int)t.mapsecs[gr].size()) continue;
			const uint8_t ms = t.mapsecs[gr][num];
			if (ms == t.mapsecNone) continue;
			for (size_t mi = 0; mi < r.regionMaps.size(); mi++)
			{
				const nds::RegionMap& m = r.regionMaps[mi];
				int x0 = m.gridW, y0 = m.gridH, x1 = -1, y1 = -1;
				for (int y = 0; y < m.gridH; y++)
					for (int x = 0; x < m.gridW; x++)
						if (m.cells[(size_t)y * m.gridW + x] == ms) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
				if (x1 < 0) continue;
				nds::MapSpot& s = kv.second.spot;
				s.map = (int16_t)mi;
				s.x = (float)(m.cellX + x0 * m.cell); s.y = (float)(m.cellY + y0 * m.cell);
				s.w = (float)((x1 - x0 + 1) * m.cell); s.h = (float)((y1 - y0 + 1) * m.cell);
				if (gr < (int)t.mapHeaders.size() && num < (int)t.mapHeaders[gr].size() && t.rom)
				{
					const long h = t.mapHeaders[gr][num];
					int w = 0, hh = 0;
					if (h >= 0 && (size_t)h + 4 <= t.rom->size() && t.layoutSize(le32(t.rom->data() + h), w, hh)) { s.tilesW = (uint16_t)w; s.tilesH = (uint16_t)hh; }
				}
				break;
			}
		}
	}
}
