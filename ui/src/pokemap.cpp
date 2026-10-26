// banana-ui: the Pokémon companion's region map pictures (pokemon.h RomTables::mapImages), the games' own, read from
// the player's ROM once when the companion's tables are (a thread at the game's start), never per frame. The idea of
// the companion is PokeDaisy's (Lidor Itzhari); none of the games' data is in Banana.
//
// Where the pictures are (pret's pokeruby / pokeemerald / pokefirered region_map.c):
//  - Ruby, Sapphire, Emerald: one background, 8 bits a pixel (LZ77), its pixels 0 or palette entries 0x70 on, the
//    32-colour palette just before it and a 64 x 64 affine tilemap (a byte a tile, LZ77) just after. The map's 28 x 15
//    sections start at tile 1, 2.
//  - FireRed, LeafGreen: 4 bits a pixel tiles (LZ77) and four 30 x 20 tilemaps (Kanto, Sevii 1-3, 4-5, 6-7; LZ77, a
//    halfword a tile with its palette and flips), an 80-colour palette. The 22 x 15 sections start at tile 4, 4. The
//    region map code's literal pool names them (the palette, a top bar palette, the tiles, then the four tilemaps), which
//    is how a hack that moved them is read too; the official ROMs also by their known places (checked like a find).
#include "pokemon.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace banana::pkmn
{
	bool lz77(const uint8_t* rom, size_t n, size_t at, std::vector<uint8_t>& out, size_t maxOut, size_t* used)
	{
		out.clear();
		if (at + 4 > n || rom[at] != 0x10) return false;
		size_t size = rom[at + 1] | rom[at + 2] << 8 | (size_t)rom[at + 3] << 16;
		if (size == 0 || size > maxOut) return false;
		out.reserve(size);
		size_t i = at + 4;
		while (out.size() < size)
		{
			if (i >= n) return false;
			uint8_t flags = rom[i++];
			for (int b = 0; b < 8 && out.size() < size; b++)
			{
				if (flags & (0x80 >> b))
				{
					if (i + 1 >= n) return false;
					unsigned v = rom[i] << 8 | rom[i + 1];
					i += 2;
					size_t len = (v >> 12) + 3, dist = (v & 0xfff) + 1;
					if (dist > out.size()) return false;
					for (size_t k = 0; k < len && out.size() < size; k++) out.push_back(out[out.size() - dist]);
				}
				else
				{
					if (i >= n) return false;
					out.push_back(rom[i++]);
				}
			}
		}
		if (used) *used = i - at;
		return true;
	}

	namespace
	{
		uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
		uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
		long lzSize(const uint8_t* rom, size_t n, size_t at)
		{
			if (at + 4 > n || rom[at] != 0x10) return -1;
			return rom[at + 1] | rom[at + 2] << 8 | (long)rom[at + 3] << 16;
		}
		// a GBA colour (BGR555) to RGBA bytes
		void put(std::vector<uint8_t>& px, size_t i, uint16_t c)
		{
			auto f = [](int x) { return (uint8_t)((x << 3) | (x >> 2)); };
			px[i * 4] = f(c & 31); px[i * 4 + 1] = f((c >> 5) & 31); px[i * 4 + 2] = f((c >> 10) & 31); px[i * 4 + 3] = 255;
		}

		// ---- Hoenn (Ruby, Sapphire, Emerald)
		struct Hoenn { size_t pal = 0, tiles = 0, map = 0; int colours = 0; std::vector<uint8_t> t, m; };
		// tiles at a: 8 bpp, every pixel 0 or 0x70.., the tilemap right after (4-aligned), every tile in it there
		bool hoennAt(const uint8_t* rom, size_t n, size_t a, Hoenn& h)
		{
			long s = lzSize(rom, n, a);
			if (s <= 0 || s % 64 || s < 64 * 100 || s > 64 * 512) return false;
			size_t used = 0;
			if (!lz77(rom, n, a, h.t, 64 * 512, &used)) return false;
			int mx = 0;
			for (uint8_t v : h.t) { if (v && (v < 0x70 || v >= 0xa0)) return false; mx = std::max<int>(mx, v); }
			if (mx < 0x70) return false;
			size_t e = (a + used + 3) & ~(size_t)3;
			if (lzSize(rom, n, e) != 0x1000 || !lz77(rom, n, e, h.m, 0x1000)) return false;
			size_t nt = h.t.size() / 64;
			for (uint8_t v : h.m) if (v >= nt) return false;
			h.colours = ((mx - 0x70) / 16 + 1) * 16;
			if (a < (size_t)h.colours * 2) return false;
			h.pal = a - h.colours * 2; h.tiles = a; h.map = e;
			for (int i = 0; i < h.colours; i++) if (le16(rom + h.pal + 2 * i) & 0x8000) return false;
			return true;
		}
		// the screen (30 x 20 tiles of the affine map at no scroll), cut to the map and a tile around it
		void hoennImage(const uint8_t* rom, const Hoenn& h, RomTables::MapImage& img)
		{
			const int X0 = 0, Y0 = 1, TW = 30, TH = 17;	// (tiles: the sections at 1, 2 -> at 8, 8 in the picture)
			img.w = TW * 8; img.h = TH * 8; img.cellX = 8 - X0 * 8; img.cellY = 16 - Y0 * 8;
			img.rgba.assign((size_t)img.w * img.h * 4, 0);
			for (int ty = 0; ty < TH; ty++)
				for (int tx = 0; tx < TW; tx++)
				{
					int tile = h.m[(ty + Y0) * 64 + tx + X0];
					for (int y = 0; y < 8; y++)
						for (int x = 0; x < 8; x++)
						{
							int c = h.t[(size_t)tile * 64 + y * 8 + x];
							// (0: the backdrop, the sea's first colour)
							uint16_t col = le16(rom + h.pal + 2 * (c ? c - 0x70 : 0));
							put(img.rgba, (size_t)(ty * 8 + y) * img.w + tx * 8 + x, col);
						}
				}
		}

		// ---- Kanto (FireRed, LeafGreen and the hacks built on them)
		struct Kanto { size_t pal = 0; std::vector<uint8_t> t; std::vector<std::vector<uint8_t>> maps; };
		bool kantoTiles(const uint8_t* rom, size_t n, size_t a, Kanto& k)
		{
			long s = lzSize(rom, n, a);
			if (s <= 0 || s % 32 || s < 32 * 64 || s > 32 * 1024) return false;
			return lz77(rom, n, a, k.t, 32 * 1024);
		}
		bool kantoMap(const uint8_t* rom, size_t n, size_t a, std::vector<uint8_t>& m)
		{
			long s = lzSize(rom, n, a);
			if (s < 0x400 || s > 30 * 20 * 2 || !lz77(rom, n, a, m, 30 * 20 * 2)) return false;
			m.resize(30 * 20 * 2, 0);
			return true;
		}
		// every tile the tilemaps use is in the tiles, and their palettes in the 80 colours
		bool kantoOk(const uint8_t* rom, size_t n, const Kanto& k)
		{
			if (k.maps.empty() || k.pal + 160 > n) return false;
			for (int i = 0; i < 80; i++) if (le16(rom + k.pal + 2 * i) & 0x8000) return false;
			size_t nt = k.t.size() / 32;
			for (auto& m : k.maps)
			{
				int used = 0;
				for (size_t i = 0; i + 1 < m.size(); i += 2)
				{
					uint16_t e = le16(m.data() + i);
					if ((e & 0x3ff) >= nt || (e >> 12) >= 5) return false;
					used += (e & 0x3ff) != 0;
				}
				if (used < 50) return false;
			}
			return true;
		}
		// the region map code's literal pool: the palette, the top bar's palette, the tiles, then the tilemaps (the
		// first four of the pointers to tilemap-sized blobs after the tiles)
		bool kantoPool(const uint8_t* rom, size_t n, size_t at, Kanto& k)
		{
			auto ptr = [&](size_t o) -> long { uint32_t v = le32(rom + o); return v >= 0x08000000u && v - 0x08000000u < n ? (long)(v - 0x08000000u) : -1; };
			long tiles = ptr(at);
			if (tiles < 0 || !kantoTiles(rom, n, (size_t)tiles, k)) return false;
			int seen = 0;
			long pal = -1;
			for (size_t o = at; o >= 4 && o + 0x60 > at && seen < 2; )
			{
				o -= 4;
				long p = ptr(o);
				if (p < 0) continue;
				if (++seen == 2) pal = p;
			}
			if (pal < 0 || lzSize(rom, n, (size_t)pal) >= 0) return false;
			k.pal = (size_t)pal;
			k.maps.clear();
			for (size_t o = at + 4; o + 4 <= n && o < at + 0x100 && k.maps.size() < 4; o += 4)
			{
				long p = ptr(o);
				std::vector<uint8_t> m;
				if (p >= 0 && kantoMap(rom, n, (size_t)p, m)) k.maps.push_back(m);
			}
			return kantoOk(rom, n, k);
		}
		void kantoImage(const uint8_t* rom, const Kanto& k, const std::vector<uint8_t>& m, RomTables::MapImage& img)
		{
			const int X0 = 3, Y0 = 3, TW = 24, TH = 17;	// (the sections at 4, 4 -> 8, 8 in the picture)
			img.w = TW * 8; img.h = TH * 8; img.cellX = (4 - X0) * 8; img.cellY = (4 - Y0) * 8;
			img.rgba.assign((size_t)img.w * img.h * 4, 0);
			size_t nt = k.t.size() / 32;
			for (int ty = 0; ty < TH; ty++)
				for (int tx = 0; tx < TW; tx++)
				{
					uint16_t e = le16(m.data() + ((ty + Y0) * 30 + tx + X0) * 2);
					size_t tile = e & 0x3ff;
					int pl = e >> 12;
					bool hf = e & 0x400, vf = e & 0x800;
					for (int y = 0; y < 8; y++)
						for (int x = 0; x < 8; x++)
						{
							int sx = hf ? 7 - x : x, sy = vf ? 7 - y : y;
							int c = 0;
							if (tile < nt) { uint8_t b = k.t[tile * 32 + sy * 4 + sx / 2]; c = sx & 1 ? b >> 4 : b & 15; }
							uint16_t col = le16(rom + k.pal + 2 * (c ? pl * 16 + c : 0));
							put(img.rgba, (size_t)(ty * 8 + y) * img.w + tx * 8 + x, col);
						}
				}
		}

		// the official ROMs' places (pret-matching US/European releases; another revision or a hack: found as below).
		// Each is checked as a find would be before it's used.
		struct Known { const char* code; int rev; uint32_t a; };	// Hoenn: the tiles; Kanto: the literal pool's tiles word
		const Known KNOWN[] = {
			{ "BPEE", 0, 0x59f77c }, { "AXVE", 2, 0x3e5dbc }, { "AXPE", 2, 0x3e5e18 },
			{ "BPRE", 1, 0x0c0344 }, { "BPGE", 1, 0x0c0318 },
		};
	}

	namespace { void findRegionMapsIn(RomTables& t, const uint8_t* rom, size_t n, int gen, const char*& how); }

	void findRegionMaps(RomTables& t, const uint8_t* rom, size_t n, int gen)
	{
		auto t0 = std::chrono::steady_clock::now();
		const char* how = "none";
		findRegionMapsIn(t, rom, n, gen, how);
		int on = 0;
		for (int g : t.gridImage) on += g >= 0;
		// (an FNV-1a of each picture's pixels: a device test compares them with the ones recorded for its ROM)
		std::string sums;
		for (auto& im : t.mapImages)
		{
			uint32_t h = 2166136261u;
			for (uint8_t b : im.rgba) h = (h ^ b) * 16777619u;
			char x[16]; snprintf(x, sizeof x, " %08x", h); sums += x;
		}
		fprintf(stderr, "pokemon: %zu region map pictures (%s), %d of %zu grids on one, %.0f ms;%s\n", t.mapImages.size(), how, on,
			t.grids.size(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), sums.c_str());
	}

	namespace {
	void findRegionMapsIn(RomTables& t, const uint8_t* rom, size_t n, int gen, const char*& how)
	{
		t.mapImages.clear();
		t.gridImage.assign(t.grids.size(), -1);
		if (n < 0x200 || t.grids.empty()) return;
		int rev = rom[0xbc];
		const Known* kn = nullptr;
		for (auto& k : KNOWN) if (!memcmp(rom + 0xac, k.code, 4) && k.rev == rev) kn = &k;
		if (gen != 2)
		{
			Hoenn h;
			bool ok = kn && hoennAt(rom, n, kn->a, h);
			how = ok ? "known place" : "found";
			for (size_t a = 0; !ok && a + 4 <= n; a += 4)
				if (rom[a] == 0x10 && hoennAt(rom, n, a, h)) ok = true;
			if (!ok) return;
			RomTables::MapImage img;
			hoennImage(rom, h, img);
			t.mapImages.push_back(std::move(img));
			for (size_t i = 0; i < t.grids.size(); i++) if (t.grids[i].w == 28 && t.grids[i].h == 15) t.gridImage[i] = 0;
			return;
		}
		Kanto k;
		bool ok = kn && kantoPool(rom, n, kn->a, k);
		how = ok ? "known place" : "found";
		// the literal pool anywhere: a word pointing to a tiles blob with tilemaps named after it
		for (size_t a = 0x200; !ok && a + 4 <= n; a += 4)
		{
			uint32_t v = le32(rom + a);
			if (v < 0x08000000u || v - 0x08000000u + 4 > n) continue;
			size_t p = v - 0x08000000u;
			long s = lzSize(rom, n, p);
			if (s <= 0 || s % 32 || s < 32 * 64 || s > 32 * 1024) continue;
			ok = kantoPool(rom, n, a, k);
		}
		if (!ok) return;
		// the four tilemaps (a hack can name one for all four): a picture each; a grid goes with the one its sections lie
		// on land in (a tile the sea doesn't use: the sea is the few tiles that fill most of a map)
		std::vector<int> mapImage;
		for (size_t i = 0; i < k.maps.size(); i++)
		{
			int same = -1;
			for (size_t j = 0; j < i; j++) if (k.maps[j] == k.maps[i]) same = mapImage[j];
			if (same >= 0) { mapImage.push_back(same); continue; }
			RomTables::MapImage img;
			kantoImage(rom, k, k.maps[i], img);
			mapImage.push_back((int)t.mapImages.size());
			t.mapImages.push_back(std::move(img));
		}
		// pokefirered's eight layers (four maps, two layers each): by their order
		if (t.grids.size() == 8 && k.maps.size() == 4 && mapImage.size() == 4)
		{
			for (size_t i = 0; i < 8; i++) t.gridImage[i] = mapImage[i / 2];
			return;
		}
		std::vector<std::vector<bool>> land;
		for (auto& m : k.maps)
		{
			std::vector<int> count(0x10000, 0);
			for (size_t i = 0; i + 1 < m.size(); i += 2) count[le16(m.data() + i)]++;
			std::vector<bool> l(30 * 20);
			for (int i = 0; i < 30 * 20; i++) l[i] = count[le16(m.data() + i * 2)] * 10 < 30 * 20;
			land.push_back(l);
		}
		for (size_t i = 0; i < t.grids.size(); i++)
		{
			const auto& g = t.grids[i];
			if (g.w != 22 || g.h != 15) continue;
			int best = -1, bestScore = 0, cells = 0;
			for (size_t m = 0; m < land.size(); m++)
			{
				int score = 0;
				cells = 0;
				for (int y = 0; y < g.h; y++)
					for (int x = 0; x < g.w; x++)
					{
						uint8_t v = g.cells[y * g.w + x];
						if (v == t.mapsecNone || v == 0xff) continue;
						cells++;
						score += land[m][(y + 4) * 30 + x + 4];
					}
				if (score > bestScore) { bestScore = score; best = (int)m; }
			}
			if (best >= 0 && bestScore * 2 >= cells) t.gridImage[i] = mapImage[best];
		}
	}
}
}
