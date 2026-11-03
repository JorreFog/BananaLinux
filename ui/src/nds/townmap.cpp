// banana-ui: the DS Pokémon companion's region maps (townmap.h)
#include "nds/townmap.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace banana::nds::townmap
{
	using nitro::Bytes;
	using nitro::Narc;
	using nitro::Span;

	namespace
	{
		bool isHgss(GameId id) { return id == GameId::HEARTGOLD || id == GameId::SOULSILVER; }
		bool isDp(GameId id) { return id == GameId::DIAMOND || id == GameId::PEARL; }
		bool isB2w2(GameId id) { return id == GameId::BLACK2 || id == GameId::WHITE2; }

		// ---- Nitro's 2D formats: NCLR palettes, NCGR characters, NSCR screens --------------------------------------
		// a member as it is, or unpacked when it is LZ-packed (Gen V packs some); keep is where unpacked bytes live
		Span member(const Narc& n, size_t i, Bytes& keep)
		{
			Span s = n[i];
			if (s.n > 4 && (s.p[0] == 0x10 || s.p[0] == 0x11) && memcmp(s.p, "RLCN", 4) != 0 && nitro::lzDecompress(s.p, s.n, keep))
				return { keep.data(), keep.size() };
			return s;
		}

		// every colour of an NCLR (BGR555) as 0xFFRRGGBB
		bool nclr(Span f, std::vector<uint32_t>& out)
		{
			if (f.n < 0x28 || memcmp(f.p, "RLCN", 4) != 0 || memcmp(f.p + 16, "TTLP", 4) != 0) return false;
			size_t at = 16 + 8 + f.u32(16 + 20), len = f.u32(16 + 16);
			out.clear();
			for (size_t k = 0; 2 * k + 2 <= len && at + 2 * k + 2 <= f.n; k++)
			{
				uint16_t c = f.u16(at + 2 * k);
				uint32_t r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
				out.push_back(0xff000000u | (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2));
			}
			return !out.empty();
		}

		// an NCGR's 8x8 tiles: 4 or 8 bits a pixel
		struct Chr { Span data; bool bpp8 = false; size_t tiles() const { return data.n / (bpp8 ? 64 : 32); } };
		bool ncgr(Span f, Chr& c)
		{
			if (f.n < 0x30 || memcmp(f.p, "RGCN", 4) != 0 || memcmp(f.p + 16, "RAHC", 4) != 0) return false;
			uint32_t depth = f.u32(16 + 12);
			if (depth != 3 && depth != 4) return false;
			c.bpp8 = depth == 4;
			size_t len = f.u32(16 + 24), at = 16 + 8 + f.u32(16 + 28);
			if (at > f.n || len > f.n - at) return false;
			c.data = { f.p + at, len };
			return true;
		}

		// an NSCR: w x h tiles, row by row (the screens used here are read as one plain grid, 32 or 64 tiles a row)
		struct Scr { int w = 0, h = 0; std::vector<uint16_t> e; uint16_t at(int x, int y) const { return e[(size_t)y * w + x]; } };
		bool nscr(Span f, Scr& s)
		{
			if (f.n < 0x24 || memcmp(f.p, "RCSN", 4) != 0 || memcmp(f.p + 16, "NRCS", 4) != 0) return false;
			s.w = f.u16(16 + 8) / 8; s.h = f.u16(16 + 10) / 8;
			size_t len = f.u32(16 + 16);
			if ((size_t)s.w * s.h * 2 > len || 16 + 20 + len > f.n || s.w == 0 || s.h == 0) return false;
			s.e.resize((size_t)s.w * s.h);
			for (size_t i = 0; i < s.e.size(); i++) s.e[i] = f.u16(16 + 20 + 2 * i);
			return true;
		}

		// a picture being put together
		struct Canvas
		{
			int w = 0, h = 0;
			std::vector<uint32_t> px;
			std::vector<uint8_t> drawn;		// (a layer's own pixels: 1 where it drew one that isn't see-through)
			Canvas(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_, 0xff000000u), drawn((size_t)w_ * h_, 0) {}
		};

		// screen tiles (sx, sy) .. + (tw, th) onto the canvas at its top left; colour 0 see-through unless opaque
		bool layer(Canvas& cv, const Scr& s, const Chr& c, const std::vector<uint32_t>& pal, int sx, int sy, int tw, int th,
			bool opaque)
		{
			if (sx < 0 || sy < 0 || sx + tw > s.w || sy + th > s.h || tw * 8 > cv.w || th * 8 > cv.h) return false;
			std::fill(cv.drawn.begin(), cv.drawn.end(), 0);
			const size_t tiles = c.tiles();
			for (int ty = 0; ty < th; ty++)
				for (int tx = 0; tx < tw; tx++)
				{
					uint16_t v = s.at(sx + tx, sy + ty);
					size_t t = v & 0x3ff;
					if (t >= tiles) continue;
					bool hf = v & 0x400, vf = v & 0x800;
					size_t bank = c.bpp8 ? 0 : (size_t)(v >> 12) * 16;
					for (int y = 0; y < 8; y++)
						for (int x = 0; x < 8; x++)
						{
							int ix = hf ? 7 - x : x, iy = vf ? 7 - y : y;
							uint8_t k;
							if (c.bpp8) k = c.data.p[t * 64 + iy * 8 + ix];
							else { uint8_t b = c.data.p[t * 32 + iy * 4 + ix / 2]; k = ix & 1 ? b >> 4 : b & 15; }
							if (k == 0 && !opaque) continue;
							size_t o = (size_t)(ty * 8 + y) * cv.w + tx * 8 + x;
							if (bank + k < pal.size()) cv.px[o] = pal[bank + k];
							cv.drawn[o] = k != 0;
						}
				}
			return true;
		}

		RegionMap picture(const Canvas& cv, const std::string& name, int x0, int y0, int w, int h)
		{
			RegionMap m;
			m.name = name; m.w = w; m.h = h;
			m.rgba.resize((size_t)w * h);
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++) m.rgba[(size_t)y * w + x] = cv.px[(size_t)(y + y0) * cv.w + x + x0];
			return m;
		}

		// a picture that is one colour (or nearly) is not one
		bool blank(const RegionMap& m)
		{
			std::set<uint32_t> c;
			for (size_t i = 0; i < m.rgba.size() && c.size() < 8; i += 7) c.insert(m.rgba[i]);
			return c.size() < 8;
		}

		// ---- the main map matrix: which zone each 32 x 32-tile square is ----------------------------------------
		struct Matrix
		{
			int w = 0, h = 0;
			std::vector<int> zone;		// w * h, -1 none
			int at(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h ? zone[(size_t)y * w + x] : -1; }
		};
		// Gen IV: {u8 w, u8 h, u8 has headers, u8 has heights, u8 name length, name, u16 headers[w*h], ...} (0 none)
		bool matrix4(Span m, Matrix& mx)
		{
			if (m.n < 5 || !m.u8(2)) return false;
			mx.w = m.u8(0); mx.h = m.u8(1);
			size_t at = 5 + m.u8(4);
			if (at + 2 * (size_t)mx.w * mx.h > m.n) return false;
			mx.zone.resize((size_t)mx.w * mx.h);
			for (size_t i = 0; i < mx.zone.size(); i++) { uint16_t z = m.u16(at + 2 * i); mx.zone[i] = z ? z : -1; }
			return true;
		}
		// Gen V: {u32 has headers, u16 w, u16 h, u32 maps[w*h], u32 headers[w*h]} (0xffffffff none)
		bool matrix5(Span m, Matrix& mx)
		{
			if (m.n < 8 || !m.u32(0)) return false;
			mx.w = m.u16(4); mx.h = m.u16(6);
			size_t at = 8 + 4 * (size_t)mx.w * mx.h;
			if (at + 4 * (size_t)mx.w * mx.h > m.n) return false;
			mx.zone.resize((size_t)mx.w * mx.h);
			for (size_t i = 0; i < mx.zone.size(); i++) { uint32_t z = m.u32(at + 4 * i); mx.zone[i] = z > 0xffff ? -1 : (int)z; }
			return true;
		}
		// each zone's squares on the matrix: min x, min y, max x, max y
		struct Box { int x0 = 1 << 20, y0 = 1 << 20, x1 = -1, y1 = -1; void add(int x, int y) { x0 = std::min(x0, x); y0 = std::min(y0, y);
			x1 = std::max(x1, x); y1 = std::max(y1, y); } bool empty() const { return x1 < 0; } };
		std::map<int, Box> boxes(const Matrix& mx)
		{
			std::map<int, Box> b;
			for (int y = 0; y < mx.h; y++) for (int x = 0; x < mx.w; x++) if (mx.at(x, y) >= 0) b[mx.at(x, y)].add(x, y);
			return b;
		}
		// the zone's place on the overworld grid (32 tiles a square)
		void tiles(MapSpot& s, const Box& b)
		{
			s.tileX = (uint16_t)(b.x0 * 32); s.tileY = (uint16_t)(b.y0 * 32);
			s.tilesW = (uint16_t)((b.x1 - b.x0 + 1) * 32); s.tilesH = (uint16_t)((b.y1 - b.y0 + 1) * 32);
		}

		// ---- Gen IV's warps: where a house, a gate or a cave comes out on the overworld -----------------------------
		// a zone's events (fielddata/eventdata/zone_event*, the header's events id @16): u32 count + 20-byte background
		// events, u32 + 32-byte objects, u32 + 12-byte warps {u16 x, u16 z, u16 to zone, u16 to warp, u32} (pokeplatinum
		// map_header_data.h)
		struct Warp { uint16_t x, z, to, toWarp; };
		std::vector<std::vector<Warp>> warps4(const nitro::Rom& rom, const char* path, const Bytes& arm9, size_t headers, size_t nz)
		{
			std::vector<std::vector<Warp>> w(nz);
			Bytes eb;
			Narc en;
			if (!rom.file(path, eb) || !en.parse(eb)) return w;
			for (size_t z = 0; z < nz; z++)
			{
				Span e = en[nitro::rd16(&arm9[headers + 24 * z + 16])];
				size_t o = 0;
				uint32_t k = e.u32(o);
				if (k > 512) continue;
				o += 4 + 20 * (size_t)k;
				k = e.u32(o);
				if (k > 512) continue;
				o += 4 + 32 * (size_t)k;
				k = e.u32(o);
				if (k > 256 || o + 4 + 12 * (size_t)k > e.n) continue;
				for (uint32_t i = 0; i < k; i++)
				{
					size_t a = o + 4 + 12 * i;
					Warp v{ e.u16(a), e.u16(a + 2), e.u16(a + 4), e.u16(a + 6) };
					if (v.to < nz) w[z].push_back(v);
				}
			}
			return w;
		}
		// the zones not on the map get the overworld square their way out leads to (the town map shows the player where
		// they went in: pokeplatinum's TownMapContext takes the exit location there), through up to five rooms or floors;
		// or the spot of a place on the way that has one of its own (Mt. Coronet). cell: a square's spot on the map
		template <class Cell>
		void entrances(RomData& out, const std::vector<std::vector<Warp>>& warps, const Matrix& mx, const Cell& cell)
		{
			const size_t nz = warps.size();
			std::vector<char> main(nz, 0);
			for (int z : mx.zone) if (z >= 0 && (size_t)z < nz) main[(size_t)z] = 1;
			auto spotOf = [&](size_t z) { auto it = out.zones.find((uint16_t)z); return it == out.zones.end() ? MapSpot{} : it->second.spot; };
			std::map<uint16_t, MapSpot> add;
			for (auto& zl : out.zones)
			{
				const size_t start = zl.first;
				if (zl.second.spot.map >= 0 || start >= nz || main[start]) continue;
				std::vector<size_t> now{ start };
				std::set<size_t> seen{ start };
				MapSpot found;
				for (int depth = 0; depth < 5 && found.map < 0 && !now.empty(); depth++)
				{
					std::vector<size_t> next;
					for (size_t z : now)
					{
						for (const Warp& w : warps[z])
						{
							if (seen.count(w.to)) continue;
							if (main[w.to])
							{	// the warp it arrives at, in the overworld's tiles
								if (w.toWarp >= warps[w.to].size()) continue;
								const Warp& d = warps[w.to][w.toWarp];
								found = cell(d.x / 32, d.z / 32);
							}
							else if (spotOf(w.to).map >= 0) found = spotOf(w.to);
							if (found.map >= 0) break;
							seen.insert(w.to);
							next.push_back(w.to);
						}
						if (found.map >= 0) break;
					}
					now = std::move(next);
				}
				if (found.map < 0) continue;
				found.tilesW = found.tilesH = 0; found.tileX = found.tileY = 0;
				add[zl.first] = found;
			}
			for (auto& a : add) out.zones[a.first].spot = a.second;
		}

		// the zones the map doesn't show by themselves (a house, a gate, a floor of a tower) get the place's spot they
		// belong to (owner: Gen V's parent zone; else the zone with the same name that has one), the marker in its middle
		void inherit(RomData& out, const std::vector<int>& nameOf, const std::vector<int>& owner)
		{
			std::map<int, MapSpot> byName;		// the union of the spots of the zones with that name (one map)
			for (auto& z : out.zones)
			{
				const MapSpot& s = z.second.spot;
				if (s.map < 0 || z.first >= nameOf.size() || nameOf[z.first] <= 0) continue;	// (name 0: "Mystery Zone", the unused zones')
				auto it = byName.find(nameOf[z.first]);
				if (it == byName.end()) { byName[nameOf[z.first]] = s; continue; }
				MapSpot& u = it->second;
				// (two places with one name far apart: the first's)
				if (u.map != s.map || s.x > u.x + u.w + 8 || u.x > s.x + s.w + 8 || s.y > u.y + u.h + 8 || u.y > s.y + s.h + 8) continue;
				float x1 = std::max(u.x + u.w, s.x + s.w), y1 = std::max(u.y + u.h, s.y + s.h);
				u.x = std::min(u.x, s.x); u.y = std::min(u.y, s.y); u.w = x1 - u.x; u.h = y1 - u.y;
			}
			std::map<uint16_t, MapSpot> add;
			for (auto& z : out.zones)
			{
				if (z.second.spot.map >= 0) continue;
				MapSpot s;
				int o = z.first;
				for (int k = 0; k < 4 && o >= 0 && (size_t)o < owner.size() && owner[o] >= 0 && owner[o] != o && s.map < 0; k++)
				{
					o = owner[o];
					auto it = out.zones.find((uint16_t)o);
					if (it != out.zones.end() && it->second.spot.map >= 0) s = it->second.spot;
				}
				if (s.map < 0 && z.first < nameOf.size())
				{
					auto it = byName.find(nameOf[z.first]);
					if (it != byName.end()) s = it->second;
				}
				if (s.map < 0) continue;
				s.tilesW = s.tilesH = 0; s.tileX = s.tileY = 0;
				add[z.first] = s;
			}
			for (auto& a : add) out.zones[a.first].spot = a.second;
		}

		// tap cells from the spots, where the game has no grid of its own (Gen V): an 8 px cell is the smallest spot's
		// it lies in
		void cellsFromSpots(RomData& out, int map, const std::vector<int>& nameOf, const std::set<uint16_t>& own)
		{
			RegionMap& m = out.regionMaps[(size_t)map];
			m.cell = 8; m.cellX = 0; m.cellY = 0; m.gridW = m.w / 8; m.gridH = m.h / 8;
			m.cells.assign((size_t)m.gridW * m.gridH, 0xFFFF);
			std::vector<float> area(m.cells.size(), 1e9f);
			for (auto& z : out.zones)
			{
				const MapSpot& s = z.second.spot;
				if (s.map != map || !own.count(z.first) || z.first >= nameOf.size()) continue;
				for (int gy = 0; gy < m.gridH; gy++)
					for (int gx = 0; gx < m.gridW; gx++)
					{
						float cx = gx * 8 + 4.f, cy = gy * 8 + 4.f;
						size_t i = (size_t)gy * m.gridW + gx;
						if (cx < s.x || cy < s.y || cx > s.x + s.w || cy > s.y + s.h || s.w * s.h >= area[i]) continue;
						area[i] = s.w * s.h; m.cells[i] = (uint16_t)nameOf[z.first];
					}
			}
		}

		// ---- Diamond, Pearl, Platinum --------------------------------------------------------------------------
		bool readDppt(const nitro::Rom& rom, const Bytes& arm9, size_t headers, RomData& out, std::string& why)
		{
			const bool dp = isDp(out.game.id);
			Bytes gb, mb, bl;
			Narc g, mm;
			if (!rom.file("graphic/tmap_gra.narc", gb) || !g.parse(gb)) { why = "no tmap_gra.narc"; return false; }
			std::vector<uint32_t> pal;
			Chr chr;
			Scr bg, fg;
			if (!nclr(g[0], pal) || !ncgr(g[19], chr) || !nscr(g[22], bg) || !nscr(g[24], fg)) { why = "the town map's graphics aren't where expected"; return false; }
			Canvas cv(256, 192);
			if (!layer(cv, bg, chr, pal, 0, 0, 32, 24, true) || !layer(cv, fg, chr, pal, 0, 0, 32, 24, false)) { why = "the town map's screens"; return false; }
			// (the top screen's last three rows are the app's bar, not the map)
			out.regionMaps.push_back(picture(cv, "Sinnoh", 0, 0, 256, 168));

			if (!rom.file("fielddata/mapmatrix/map_matrix.narc", mb) || !mm.parse(mb)) { why = "no map matrix"; return false; }
			Matrix mx;
			if (!matrix4(mm[0], mx)) { why = "the main matrix"; return false; }
			if (!rom.file("data/tmap_block.dat", bl) || bl.size() < 4) { why = "no tmap_block.dat"; return false; }
			const size_t nz = out.zones.empty() ? 0 : out.zones.rbegin()->first + 1;
			if (headers == SIZE_MAX || headers + 24 * nz > arm9.size()) { why = "no map headers"; return false; }
			std::vector<int> nameOf(nz, -1), owner(nz, -1);
			for (size_t z = 0; z < nz; z++)
				nameOf[z] = dp ? nitro::rd16(&arm9[headers + 24 * z + 18]) : arm9[headers + 24 * z + 18];
			// the town map's squares {u16 x, u16 z, ..., u16 hidden-place flags @20}: 7 px each, (22, -37) the corner of 0,0
			// (TOWN_MAP_GRID_X/Y less half a square). Those that aren't on the matrix are Mt. Coronet's (and the Fight
			// Area's gate at 20,12): the town map names them by hand, so does this (header ids: pokeplatinum, pokediamond)
			const int CORONET = 207, GATE = dp ? 192 : 560;
			std::map<int, Box> shown;		// zone -> its squares on the town map
			std::map<std::pair<int, int>, int> cellZone;
			const size_t nb = std::min<size_t>(nitro::rd32(bl.data()), (bl.size() - 4) / 24);
			for (size_t i = 0; i < nb; i++)
			{
				int x = nitro::rd16(&bl[4 + 24 * i]), y = nitro::rd16(&bl[4 + 24 * i + 2]);
				int z = mx.at(x, y);
				if (z < 0) z = x == 20 && y == 12 ? GATE : CORONET;
				if ((size_t)z >= nz) continue;
				shown[z].add(x, y);
				cellZone[{ x, y }] = z;
			}
			auto onMatrix = boxes(mx);
			for (auto& s : shown)
			{
				auto zi = out.zones.find((uint16_t)s.first);
				if (zi == out.zones.end()) continue;
				MapSpot sp;
				sp.map = 0;
				// a zone on the matrix: all its squares (the town map leaves out none of those it has); Mt. Coronet: its blocks
				auto on = onMatrix.find(s.first);
				const Box& b = on != onMatrix.end() ? on->second : s.second;
				sp.x = 22.f + 7 * b.x0; sp.y = -37.f + 7 * b.y0; sp.w = 7.f * (b.x1 - b.x0 + 1); sp.h = 7.f * (b.y1 - b.y0 + 1);
				if (on != onMatrix.end()) tiles(sp, b);
				zi->second.spot = sp;
			}
			entrances(out, warps4(rom, dp ? "fielddata/eventdata/zone_event_release.narc" : "fielddata/eventdata/zone_event.narc", arm9, headers, nz),
				mx, [&](int x, int y) { MapSpot c; c.map = 0; c.x = 22.f + 7 * x; c.y = -37.f + 7 * y; c.w = c.h = 7; return c; });
			inherit(out, nameOf, owner);
			// the grid: the town map's squares, from the first row that has one
			RegionMap& m = out.regionMaps[0];
			int y0 = 1 << 20, y1 = -1;
			for (auto& c : cellZone) { y0 = std::min(y0, c.first.second); y1 = std::max(y1, c.first.second); }
			if (y1 >= y0)
			{
				m.cell = 7; m.cellX = 22; m.cellY = -37 + 7 * y0; m.gridW = mx.w; m.gridH = y1 - y0 + 1;
				m.cells.assign((size_t)m.gridW * m.gridH, 0xFFFF);
				for (auto& c : cellZone)
					if (c.first.first < m.gridW && nameOf[(size_t)c.second] >= 0)
						m.cells[(size_t)(c.first.second - y0) * m.gridW + c.first.first] = (uint16_t)nameOf[(size_t)c.second];
			}
			return true;
		}

		// ---- HeartGold, SoulSilver -------------------------------------------------------------------------------
		// the Pokégear's regions (Pokegear_RegionFromCoords): 0 Johto, 1 Kanto (with the Indigo Plateau)
		int hgRegion(int x, int y)
		{
			if (x > 21 && !(x == 25 && y == 8)) return 1;
			return 0;
		}

		bool readHgss(const nitro::Rom& rom, const Bytes& arm9, size_t headers, RomData& out, std::string& why)
		{
			Bytes gb, mb;
			Narc g, mm;
			if (!rom.file("a/1/4/4", gb) || !g.parse(gb)) { why = "no Pokégear map archive"; return false; }
			std::vector<uint32_t> pal;
			Chr chr;
			Scr s;
			if (!nclr(g[20], pal) || !ncgr(g[10], chr) || !chr.bpp8 || !nscr(g[11], s)) { why = "the Pokégear map's graphics aren't where expected"; return false; }
			// the whole map: the screen's first 47 x 20 tiles (one tile a matrix square, two rows down)
			const int TW = 47, TH = 20, KANTO = 22, JOHTO_W = 28;
			Canvas cv(TW * 8, TH * 8);
			if (!layer(cv, s, chr, pal, 0, 0, TW, TH, true)) { why = "the Pokégear map's screen"; return false; }
			out.regionMaps.push_back(picture(cv, "Johto", 0, 0, JOHTO_W * 8, TH * 8));
			out.regionMaps.push_back(picture(cv, "Kanto", KANTO * 8, 0, (TW - KANTO) * 8, TH * 8));
			const int left[2] = { 0, KANTO };

			if (!rom.file("a/0/4/1", mb) || !mm.parse(mb)) { why = "no map matrix"; return false; }
			Matrix mx;
			if (!matrix4(mm[0], mx)) { why = "the main matrix"; return false; }
			const size_t nz = out.zones.empty() ? 0 : out.zones.rbegin()->first + 1;
			if (headers == SIZE_MAX || headers + 24 * nz > arm9.size()) { why = "no map headers"; return false; }
			// the header: {u8 wild, u8 area, u16 move models:4 world map x:6 y:6, u16 matrix, ..., u8 name @18}
			std::vector<int> nameOf(nz, -1), owner(nz, -1);
			for (size_t z = 0; z < nz; z++) nameOf[z] = arm9[headers + 24 * z + 18];
			auto place = [&](MapSpot& sp, const Box& b) {
				sp.map = (int16_t)hgRegion(b.x0, b.y0);
				sp.x = 8.f * (b.x0 - left[sp.map]); sp.y = 8.f * (b.y0 + 2);
				sp.w = 8.f * (b.x1 - b.x0 + 1); sp.h = 8.f * (b.y1 - b.y0 + 1);
				if (sp.x < 0) sp.map = -1;	// (a Kanto place drawn left of Kanto's picture: none)
			};
			auto onMatrix = boxes(mx);
			for (auto& z : out.zones)
			{
				if (z.first >= nz) continue;
				MapSpot sp;
				auto on = onMatrix.find(z.first);
				if (on != onMatrix.end()) { place(sp, on->second); if (sp.map >= 0) tiles(sp, on->second); }
				else
				{
					uint16_t v = nitro::rd16(&arm9[headers + 24 * z.first + 2]);
					int x = v >> 4 & 63, y = v >> 10 & 63;
					if (x || y) { Box b; b.add(x, y); place(sp, b); }
				}
				z.second.spot = sp;
			}
			entrances(out, warps4(rom, "a/0/3/2", arm9, headers, nz), mx, [&](int x, int y) { MapSpot c; Box b; b.add(x, y); place(c, b); return c; });
			inherit(out, nameOf, owner);
			for (int r = 0; r < 2; r++)
			{
				RegionMap& m = out.regionMaps[(size_t)r];
				m.cell = 8; m.cellX = 0; m.cellY = 16; m.gridW = m.w / 8; m.gridH = mx.h;
				m.cells.assign((size_t)m.gridW * m.gridH, 0xFFFF);
				for (int y = 0; y < m.gridH; y++)
					for (int x = 0; x < m.gridW; x++)
					{
						int z = mx.at(x + left[r], y);
						if (z >= 0 && (size_t)z < nz && hgRegion(x + left[r], y) == r) m.cells[(size_t)y * m.gridW + x] = (uint16_t)nameOf[(size_t)z];
					}
			}
			return true;
		}

		// ---- Black, White, Black 2, White 2 ---------------------------------------------------------------------
		bool readGen5(const nitro::Rom& rom, RomData& out, std::string& why)
		{
			const bool b2 = isB2w2(out.game.id);
			Bytes gb, tb, mb, zb, k0, k1, k2, k3, k4;
			Narc g, tn, mm, zn;
			if (!rom.file(b2 ? "a/0/8/4" : "a/0/8/5", gb) || !g.parse(gb)) { why = "no town map archive"; return false; }
			std::vector<uint32_t> pal;
			Chr land, lines;
			Scr ls, rs;
			if (!nclr(member(g, 0, k0), pal) || !ncgr(member(g, 7, k1), land) || !ncgr(member(g, 5, k2), lines) ||
				!nscr(member(g, 16, k3), ls) || !nscr(member(g, 17, k4), rs) || ls.w < 32 || ls.h < 24 || rs.w < 32 || rs.h < 24)
			{ why = "the town map's graphics aren't where expected"; return false; }
			Canvas cv(256, 192);
			if (!layer(cv, ls, land, pal, 0, 0, 32, 24, true)) { why = "the town map's screen"; return false; }
			if (!layer(cv, rs, lines, pal, 0, 0, 32, 24, false)) { why = "the town map's routes"; return false; }
			std::vector<uint8_t> routes = cv.drawn;		// the routes' and towns' own pixels
			// (below the map the screen is one colour: the picture ends at the last row that isn't)
			int h = 192;
			for (; h > 64; h--)
			{
				bool one = true;
				for (int x = 1; x < 256 && one; x++) one = cv.px[(size_t)(h - 1) * 256 + x] == cv.px[(size_t)(h - 1) * 256];
				if (!one) break;
			}
			out.regionMaps.push_back(picture(cv, "Unova", 0, 0, 256, h));

			if (!rom.file(b2 ? "a/0/8/5" : "a/0/8/6", tb) || !tn.parse(tb) || tn.size() < 1) { why = "no town map places"; return false; }
			if (!rom.file("a/0/0/9", mb) || !mm.parse(mb)) { why = "no map matrix"; return false; }
			Matrix mx;
			if (!matrix5(mm[0], mx)) { why = "the main matrix"; return false; }
			if (!rom.file("a/0/1/2", zb) || !zn.parse(zb) || zn.size() < 1) { why = "no zone headers"; return false; }
			Span zh = zn[0];
			const size_t nz = zh.n / 48;
			std::vector<int> nameOf(nz, -1), owner(nz, -1);
			for (size_t z = 0; z < nz; z++) { nameOf[z] = zh.u8(48 * z + 26); owner[z] = zh.u16(48 * z + 24); }
			// the places: {u16 zone, u16, u16 x, u16 y, u16 x, y (three more points), ..., u16 kind @22 (1 a route)}
			struct Place { int zone, x, y; bool route; };
			std::vector<Place> places;
			Span t = tn[0];
			for (size_t o = 0; o + 54 <= t.n; o += 54)
			{
				Place p{ t.u16(o), t.u16(o + 4), t.u16(o + 6), t.u16(o + 22) == 1 };
				if ((size_t)p.zone < nz && p.x < 256 && p.y < h) places.push_back(p);
			}
			if (places.size() < 20) { why = "the town map's places aren't where expected"; return false; }
			auto onMatrix = boxes(mx);
			std::set<uint16_t> own;
			for (auto& p : places)
			{
				auto zi = out.zones.find((uint16_t)p.zone);
				if (zi == out.zones.end() || zi->second.spot.map >= 0) continue;
				// a town or a cave: its circle (13 px); a route: the line drawn for it, as far as it goes from its point
				// without reaching another place's circle
				float x0 = p.x - 6.5f, y0 = p.y - 6.5f, x1 = p.x + 6.5f, y1 = p.y + 6.5f;
				if (p.route)
				{
					x0 = p.x - 4.f; y0 = p.y - 4.f; x1 = p.x + 4.f; y1 = p.y + 4.f;
					auto near = [&](int x, int y) {
						for (auto& q : places) if (!q.route && (x - q.x) * (x - q.x) + (y - q.y) * (y - q.y) <= 81) return true;
						return false;
					};
					// start on the line's pixel nearest the point
					int sx = -1, sy = -1, best = 1 << 20;
					for (int dy = -3; dy <= 3; dy++)
						for (int dx = -3; dx <= 3; dx++)
						{
							int x = p.x + dx, y = p.y + dy;
							if (x >= 0 && y >= 0 && x < 256 && y < h && routes[(size_t)y * 256 + x] && dx * dx + dy * dy < best) { best = dx * dx + dy * dy; sx = x; sy = y; }
						}
					std::vector<uint8_t> seen((size_t)256 * h, 0);
					std::vector<std::pair<int, int>> st;
					if (sx >= 0) st.push_back({ sx, sy });
					while (!st.empty())
					{
						auto [x, y] = st.back();
						st.pop_back();
						if (x < 0 || y < 0 || x >= 256 || y >= h || std::abs(x - p.x) > 48 || std::abs(y - p.y) > 48) continue;
						size_t i = (size_t)y * 256 + x;
						if (seen[i] || !routes[i]) continue;
						seen[i] = 1;
						if (near(x, y)) continue;
						x0 = std::min(x0, (float)x); y0 = std::min(y0, (float)y); x1 = std::max(x1, x + 1.f); y1 = std::max(y1, y + 1.f);
						st.push_back({ x + 1, y }); st.push_back({ x - 1, y }); st.push_back({ x, y + 1 }); st.push_back({ x, y - 1 });
					}
				}
				MapSpot sp;
				sp.map = 0;
				sp.x = std::max(0.f, x0); sp.y = std::max(0.f, y0);
				sp.w = std::min(256.f, x1) - sp.x; sp.h = std::min((float)h, y1) - sp.y;
				auto on = onMatrix.find(p.zone);
				if (on != onMatrix.end()) tiles(sp, on->second);
				zi->second.spot = sp;
				own.insert((uint16_t)p.zone);
			}
			// (the zones of a place: Route 4's two halves, a town's two parts on the matrix): the place's spot, still
			// moving the marker by their own squares when they are on the matrix
			for (auto& z : out.zones)
			{
				if (z.second.spot.map >= 0 || z.first >= nz) continue;
				auto on = onMatrix.find(z.first);
				if (on == onMatrix.end()) continue;
				for (auto& p : places)
				{
					auto pz = out.zones.find((uint16_t)p.zone);
					if (pz == out.zones.end() || nameOf[(size_t)p.zone] != nameOf[z.first] || pz->second.spot.map < 0) continue;
					z.second.spot = pz->second.spot;
					tiles(z.second.spot, on->second);
					break;
				}
			}
			inherit(out, nameOf, owner);
			cellsFromSpots(out, 0, nameOf, own);
			return true;
		}
	}

	bool read(const nitro::Rom& rom, const Bytes& arm9, size_t headers, RomData& out, std::string* why)
	{
		std::string w;
		out.regionMaps.clear();
		for (auto& z : out.zones) z.second.spot = MapSpot{};
		bool ok = out.game.gen == Gen::V ? readGen5(rom, out, w)
			: isHgss(out.game.id) ? readHgss(rom, arm9, headers, out, w)
			: out.game.gen == Gen::IV ? readDppt(rom, arm9, headers, out, w) : (w = "not a DS game", false);
		for (auto& m : out.regionMaps) if (ok && blank(m)) { ok = false; w = "the town map came out blank"; }
		if (!ok)
		{
			out.regionMaps.clear();
			for (auto& z : out.zones) z.second.spot = MapSpot{};
			if (why) *why = w;
		}
		return ok;
	}

	bool marker(const MapSpot& s, uint16_t mapX, uint16_t mapY, float& x, float& y)
	{
		if (s.map < 0) return false;
		x = s.x + s.w / 2; y = s.y + s.h / 2;
		if (s.tilesW && s.tilesH)
		{
			float fx = (mapX - (float)s.tileX + 0.5f) / s.tilesW, fy = (mapY - (float)s.tileY + 0.5f) / s.tilesH;
			x = s.x + std::clamp(fx, 0.f, 1.f) * s.w;
			y = s.y + std::clamp(fy, 0.f, 1.f) * s.h;
		}
		return true;
	}

	// ---- the cache ------------------------------------------------------------------------------------------------
	namespace
	{
		void w8(Bytes& b, uint8_t v) { b.push_back(v); }
		void w16(Bytes& b, uint16_t v) { w8(b, (uint8_t)v); w8(b, (uint8_t)(v >> 8)); }
		void w32(Bytes& b, uint32_t v) { w16(b, (uint16_t)v); w16(b, (uint16_t)(v >> 16)); }
		void wf(Bytes& b, float f) { uint32_t v; memcpy(&v, &f, 4); w32(b, v); }
		struct In
		{
			const uint8_t* p; size_t n; size_t& at; bool ok = true;
			bool need(size_t k) { if (!ok || k > n - at) ok = false; return ok; }
			uint8_t u8() { return need(1) ? p[at++] : 0; }
			uint16_t u16() { uint16_t v = u8(); return (uint16_t)(v | u8() << 8); }
			uint32_t u32() { uint32_t v = u16(); return v | (uint32_t)u16() << 16; }
			float f() { uint32_t v = u32(); float r; memcpy(&r, &v, 4); return r; }
		};
	}

	void save(const RomData& r, Bytes& b)
	{
		w32(b, (uint32_t)r.regionMaps.size());
		for (auto& m : r.regionMaps)
		{
			w32(b, (uint32_t)m.name.size()); b.insert(b.end(), m.name.begin(), m.name.end());
			w16(b, (uint16_t)m.w); w16(b, (uint16_t)m.h);
			// the picture by its colours (a town map has a few hundred at most): up to 256 one byte a pixel, else raw
			std::map<uint32_t, uint8_t> idx;
			for (uint32_t c : m.rgba) { if (idx.size() > 256) break; idx.emplace(c, 0); }
			if (idx.size() <= 256)
			{
				w16(b, (uint16_t)idx.size());
				uint8_t k = 0;
				for (auto& c : idx) { c.second = k++; w32(b, c.first); }
				for (uint32_t c : m.rgba) w8(b, idx[c]);
			}
			else { w16(b, 0); for (uint32_t c : m.rgba) w32(b, c); }
			w32(b, (uint32_t)m.cellX); w32(b, (uint32_t)m.cellY); w16(b, (uint16_t)m.cell);
			w16(b, (uint16_t)m.gridW); w16(b, (uint16_t)m.gridH);
			for (uint16_t c : m.cells) w16(b, c);
		}
		uint32_t n = 0;
		for (auto& z : r.zones) n += z.second.spot.map >= 0;
		w32(b, n);
		for (auto& z : r.zones)
		{
			const MapSpot& s = z.second.spot;
			if (s.map < 0) continue;
			w16(b, z.first); w16(b, (uint16_t)s.map); wf(b, s.x); wf(b, s.y); wf(b, s.w); wf(b, s.h);
			w16(b, s.tilesW); w16(b, s.tilesH); w16(b, s.tileX); w16(b, s.tileY);
		}
	}

	bool load(RomData& r, const uint8_t* p, size_t n, size_t& at)
	{
		In in{ p, n, at };
		uint32_t maps = in.u32();
		if (maps > 8) return false;
		r.regionMaps.assign(maps, RegionMap{});
		for (auto& m : r.regionMaps)
		{
			uint32_t k = in.u32();
			if (!in.need(k)) return false;
			m.name.assign((const char*)p + at, k); at += k;
			m.w = in.u16(); m.h = in.u16();
			const size_t px = (size_t)m.w * m.h;
			uint16_t colours = in.u16();
			if (colours > 256 || !in.need(4 * (size_t)colours)) return false;
			std::vector<uint32_t> pal(colours);
			for (auto& c : pal) c = in.u32();
			if (!in.need(px * (colours ? 1 : 4))) return false;
			m.rgba.resize(px);
			for (auto& c : m.rgba) { if (colours) { uint8_t i = in.u8(); c = i < colours ? pal[i] : 0; } else c = in.u32(); }
			m.cellX = (int32_t)in.u32(); m.cellY = (int32_t)in.u32(); m.cell = in.u16();
			m.gridW = in.u16(); m.gridH = in.u16();
			if (!in.need(2 * (size_t)m.gridW * m.gridH)) return false;
			m.cells.resize((size_t)m.gridW * m.gridH);
			for (auto& c : m.cells) c = in.u16();
		}
		uint32_t spots = in.u32();
		if (!in.need((size_t)spots * 28)) return false;
		for (uint32_t i = 0; i < spots; i++)
		{
			uint16_t id = in.u16();
			MapSpot s;
			s.map = (int16_t)in.u16(); s.x = in.f(); s.y = in.f(); s.w = in.f(); s.h = in.f();
			s.tilesW = in.u16(); s.tilesH = in.u16(); s.tileX = in.u16(); s.tileY = in.u16();
			auto it = r.zones.find(id);
			if (it != r.zones.end() && s.map >= 0 && (size_t)s.map < r.regionMaps.size()) it->second.spot = s;
		}
		return in.ok;
	}
}
