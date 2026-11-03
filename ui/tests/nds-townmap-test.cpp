// The DS Pokémon companion's region maps (src/nds/townmap.cpp): the marker's arithmetic and the cache on made-up data,
// and, when a folder of the player's own ROMs is given (or NDS_ROMS names one), every game's town map: the pictures
// decode, the first towns and routes have spots inside them in the right order, the cache gives back the same. With
// an output folder it writes each map with every spot outlined (red: spots that move the marker, blue: the middle
// only) for a look by eye. Never commit those pictures: they are the game's own.
//   make tests/nds-townmap-test && tests/nds-townmap-test [roms-dir [out-dir]]
#include "nds/rom.h"
#include "nds/townmap.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

using namespace banana::nds;

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKM(c, ...) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

struct Fake : RomData
{
	std::shared_ptr<const Sprite> sprite(uint16_t, bool, uint8_t) const override { return nullptr; }
};

// ---- the marker ----------------------------------------------------------------------------------------------------
static void testMarker()
{
	MapSpot s;
	float x = 0, y = 0;
	CHECK(!townmap::marker(s, 0, 0, x, y));		// on no map
	s.map = 0; s.x = 10; s.y = 20; s.w = 14; s.h = 7; s.tileX = 64; s.tileY = 96; s.tilesW = 64; s.tilesH = 32;
	CHECK(townmap::marker(s, 64, 96, x, y));
	CHECK(x > 10 && x < 10.5f && y > 20 && y < 20.5f);		// the first tile: the rectangle's top left corner
	townmap::marker(s, 127, 127, x, y);
	CHECK(x > 23.5f && x <= 24 && y > 26.5f && y <= 27);	// the last: its bottom right
	townmap::marker(s, 96, 112, x, y);
	CHECK(std::abs(x - 17.1f) < 0.2f && std::abs(y - 23.6f) < 0.2f);
	townmap::marker(s, 3000, 0, x, y);
	CHECK(x == 24 && y == 20);			// outside the zone (a stale position): clamped to the rectangle
	s.tilesW = s.tilesH = 0;			// a house: the middle
	townmap::marker(s, 5, 7, x, y);
	CHECK(x == 17 && y == 23.5f);
}

// ---- the cache: what save writes, load gives back -------------------------------------------------------------------
static void testCache()
{
	Fake a;
	RegionMap m;
	m.name = "Test"; m.w = 20; m.h = 10;
	for (int i = 0; i < 200; i++) m.rgba.push_back(0xff000000u | (uint32_t)(i % 37) * 0x010203);
	m.cellX = -3; m.cellY = 5; m.cell = 7; m.gridW = 3; m.gridH = 2; m.cells = { 1, 0xFFFF, 2, 3, 4, 0xFFFF };
	a.regionMaps.push_back(m);
	RegionMap big = m;	// more than 256 colours: kept raw
	for (int i = 0; i < 200; i++) big.rgba[(size_t)i] = 0xff000000u | (uint32_t)i * 1031;
	big.rgba.resize(400); big.h = 20;
	for (int i = 200; i < 400; i++) big.rgba[(size_t)i] = 0xff800000u | (uint32_t)i;
	a.regionMaps.push_back(big);
	a.zones[3].spot = { 1, 1.5f, 2.5f, 7, 14, 64, 32, 96, 640 };
	a.zones[9];
	nitro::Bytes b = { 0xAA };
	townmap::save(a, b);
	Fake r;
	r.zones[3]; r.zones[9];
	size_t at = 1;
	CHECK(townmap::load(r, b.data(), b.size(), at));
	CHECK(at == b.size());
	CHECK(r.regionMaps.size() == 2);
	for (size_t i = 0; i < 2 && i < r.regionMaps.size(); i++)
	{
		const RegionMap &x = a.regionMaps[i], &y = r.regionMaps[i];
		CHECK(x.name == y.name && x.w == y.w && x.h == y.h && x.rgba == y.rgba && x.cellX == y.cellX && x.cellY == y.cellY &&
			x.cell == y.cell && x.gridW == y.gridW && x.gridH == y.gridH && x.cells == y.cells);
	}
	const MapSpot& s = r.zones[3].spot;
	CHECK(s.map == 1 && s.x == 1.5f && s.y == 2.5f && s.w == 7 && s.h == 14 && s.tilesW == 64 && s.tilesH == 32 && s.tileX == 96 && s.tileY == 640);
	CHECK(r.zones[9].spot.map == -1);
	size_t at2 = 1;	// cut short: false, not a crash
	CHECK(!townmap::load(r, b.data(), b.size() - 5, at2));
}

// ---- the player's ROMs ------------------------------------------------------------------------------------------------
static void outline(std::vector<uint32_t>& px, int w, int h, const MapSpot& s, uint32_t c)
{
	int x0 = (int)s.x, y0 = (int)s.y, x1 = (int)(s.x + s.w + 0.5f) - 1, y1 = (int)(s.y + s.h + 0.5f) - 1;
	auto put = [&](int x, int y) { if (x >= 0 && y >= 0 && x < w && y < h) px[(size_t)y * w + x] = c; };
	for (int x = x0; x <= x1; x++) { put(x, y0); put(x, y1); }
	for (int y = y0; y <= y1; y++) { put(x0, y); put(x1, y); }
}

static void writePng(const std::string& path, const RegionMap& m, const RomData& r, int map)
{
	const int S = 3;	// (three times bigger, to see the outlines)
	std::vector<uint32_t> px((size_t)m.w * S * m.h * S);
	for (int y = 0; y < m.h * S; y++)
		for (int x = 0; x < m.w * S; x++) px[(size_t)y * m.w * S + x] = m.rgba[(size_t)(y / S) * m.w + x / S];
	for (int moving = 0; moving < 2; moving++)	// (the zones that move the marker on top)
		for (auto& z : r.zones)
		{
			MapSpot s = z.second.spot;
			if (s.map != map || (s.tilesW > 0) != (moving > 0)) continue;
			s.x *= S; s.y *= S; s.w *= S; s.h *= S;
			outline(px, m.w * S, m.h * S, s, s.tilesW ? 0xffff2020u : 0xff2040ffu);
		}
	std::vector<uint8_t> rgb;
	for (uint32_t c : px) { rgb.push_back((uint8_t)(c >> 16)); rgb.push_back((uint8_t)(c >> 8)); rgb.push_back((uint8_t)c); }
	stbi_write_png(path.c_str(), m.w * S, m.h * S, 3, rgb.data(), m.w * S * 3);
}

// the first zone with that name that has a spot on map (-1: any)
static const Location* place(const RomData& r, const std::string& name, bool needTiles = true)
{
	for (auto& z : r.zones)
		if (z.second.name == name && z.second.spot.map >= 0 && (!needTiles || z.second.spot.tilesW)) return &z.second;
	return nullptr;
}

static void testRom(const std::string& path, const std::string& out)
{
	std::string err;
	RomLoadInfo info;
	auto r = loadRom(path, "", &err, &info);
	if (!r) { fprintf(stderr, "skip %s: %s\n", path.c_str(), err.c_str()); return; }
	const Game& g = r->game;
	const char* t = g.title.c_str();
	const bool hg = g.id == GameId::HEARTGOLD || g.id == GameId::SOULSILVER;
	CHECKM(r->regionMaps.size() == (hg ? 2u : 1u), "%s: %zu maps", t, r->regionMaps.size());
	size_t onMap = 0, moving = 0;
	for (auto& z : r->zones) { onMap += z.second.spot.map >= 0; moving += z.second.spot.tilesW > 0; }
	for (size_t i = 0; i < r->regionMaps.size(); i++)
	{
		const RegionMap& m = r->regionMaps[i];
		CHECKM(m.w >= 160 && m.w <= 512 && m.h >= 120 && m.h <= 512 && m.rgba.size() == (size_t)m.w * m.h, "%s: map %zu is %dx%d", t, i, m.w, m.h);
		std::map<uint32_t, int> colours;
		for (uint32_t c : m.rgba) colours[c]++;
		CHECKM(colours.size() > 12, "%s: map %zu has %zu colours", t, i, colours.size());
		size_t named = 0;
		for (uint16_t c : m.cells) named += c != 0xFFFF && c < r->locationNames.size();
		CHECKM(m.cells.size() == (size_t)m.gridW * m.gridH && named >= 15, "%s: map %zu has %zu named cells", t, i, named);
		for (int y = 0; y < m.gridH; y++)	// the named cells lie on the picture
			for (int x = 0; x < m.gridW; x++)
				if (m.cells[(size_t)y * m.gridW + x] != 0xFFFF)
				{
					int cx = m.cellX + x * m.cell + m.cell / 2, cy = m.cellY + y * m.cell + m.cell / 2;
					CHECKM(cx >= 0 && cy >= 0 && cx < m.w && cy < m.h, "%s: map %zu cell %d,%d at %d,%d", t, i, x, y, cx, cy);
				}
		if (!out.empty()) writePng(out + "/" + g.code + "-" + std::to_string(i) + "-" + m.name + ".png", m, *r, (int)i);
	}
	for (auto& z : r->zones)
	{
		const MapSpot& s = z.second.spot;
		if (s.map < 0) continue;
		bool in = (size_t)s.map < r->regionMaps.size();
		if (in)
		{
			const RegionMap& m = r->regionMaps[(size_t)s.map];
			in = s.x >= 0 && s.y >= 0 && s.w > 0 && s.h > 0 && s.x + s.w <= m.w + 0.01f && s.y + s.h <= m.h + 0.01f;
		}
		CHECKM(in, "%s: zone %u %s at %d %.1f,%.1f %.1fx%.1f", t, z.first, z.second.name.c_str(), s.map, s.x, s.y, s.w, s.h);
		if (s.tilesW)
		{	// the zone's middle tile puts the marker inside its rectangle
			float x, y;
			townmap::marker(s, (uint16_t)(s.tileX + s.tilesW / 2), (uint16_t)(s.tileY + s.tilesH / 2), x, y);
			CHECK(x >= s.x && x <= s.x + s.w && y >= s.y && y <= s.y + s.h);
		}
	}
	// the first places: on the map, the marker moving inside them, in the right order
	struct Order { const char *a, *b; char dir; };	// b is to the dir (l/r/u/d) of a
	static const std::vector<Order> NONE;
	const std::vector<Order>* order = &NONE;
	const char* start = "";
	switch (g.id)
	{
	case GameId::DIAMOND: case GameId::PEARL: case GameId::PLATINUM: {
		start = "Twinleaf Town"; static const std::vector<Order> O = { { "Twinleaf Town", "Sandgem Town", 'r' }, { "Twinleaf Town", "Route 201", 'u' },
			{ "Sandgem Town", "Jubilife City", 'u' }, { "Jubilife City", "Oreburgh City", 'r' }, { "Jubilife City", "Canalave City", 'l' },
			{ "Hearthome City", "Snowpoint City", 'u' }, { "Hearthome City", "Sunyshore City", 'r' } };
		order = &O;
		break; }
	case GameId::HEARTGOLD: case GameId::SOULSILVER: {
		start = "New Bark Town"; static const std::vector<Order> O = { { "New Bark Town", "Route 29", 'l' }, { "New Bark Town", "Cherrygrove City", 'l' },
			{ "Cherrygrove City", "Violet City", 'u' }, { "Goldenrod City", "Ecruteak City", 'u' }, { "Olivine City", "Cianwood City", 'l' } };
		order = &O;
		break; }
	case GameId::BLACK: case GameId::WHITE: {
		start = "Nuvema Town"; static const std::vector<Order> O = { { "Nuvema Town", "Route 1", 'u' }, { "Nuvema Town", "Accumula Town", 'u' },
			{ "Striaton City", "Nacrene City", 'l' }, { "Castelia City", "Nimbasa City", 'u' }, { "Nimbasa City", "Driftveil City", 'l' } };
		order = &O;
		break; }
	case GameId::BLACK2: case GameId::WHITE2: {
		start = "Aspertia City"; static const std::vector<Order> O = { { "Aspertia City", "Route 19", 'r' }, { "Aspertia City", "Floccesy Town", 'r' },
			{ "Virbank City", "Castelia City", 'r' }, { "Castelia City", "Nimbasa City", 'u' } };
		order = &O;
		break; }
	default: break;
	}
	CHECKM(place(*r, start), "%s: %s has no spot", t, start);
	for (auto& o : *order)
	{
		const Location *a = place(*r, o.a, false), *b = place(*r, o.b, false);
		CHECKM(a && b, "%s: %s / %s: no spot", t, o.a, o.b);
		if (!a || !b) continue;
		if (a->spot.map != b->spot.map) continue;	// (HGSS: on two pictures)
		float ax = a->spot.x + a->spot.w / 2, ay = a->spot.y + a->spot.h / 2, bx = b->spot.x + b->spot.w / 2, by = b->spot.y + b->spot.h / 2;
		bool ok = o.dir == 'r' ? bx > ax : o.dir == 'l' ? bx < ax : o.dir == 'u' ? by < ay : by > ay;
		CHECKM(ok, "%s: %s (%.0f,%.0f) isn't %c of %s (%.0f,%.0f)", t, o.b, bx, by, o.dir, o.a, ax, ay);
	}
	// a house in the first town: its town's spot, the marker in the middle
	const Location* town = place(*r, start);
	size_t houses = 0;
	for (auto& z : r->zones)
		if (town && z.second.name == start && z.second.spot.map >= 0 && !z.second.spot.tilesW) houses++;
	CHECKM(houses > 0, "%s: no building of %s on the map", t, start);
	// the cache gives back the same
	char tmpl[] = "/tmp/nds-townmap-XXXXXX";
	if (mkdtemp(tmpl))
	{
		std::string dir = tmpl;
		RomLoadInfo i1, i2;
		auto c1 = loadRom(path, dir, &err, &i1), c2 = loadRom(path, dir, &err, &i2);
		CHECKM(c1 && c2 && !i1.fromCache && i2.fromCache, "%s: the cache", t);
		if (c2)
		{
			CHECK(c2->regionMaps.size() == r->regionMaps.size());
			for (size_t i = 0; i < c2->regionMaps.size() && i < r->regionMaps.size(); i++)
				CHECK(c2->regionMaps[i].rgba == r->regionMaps[i].rgba && c2->regionMaps[i].cells == r->regionMaps[i].cells);
			size_t same = 0;
			for (auto& z : r->zones)
			{
				auto it = c2->zones.find(z.first);
				const MapSpot &a = z.second.spot, &b = it->second.spot;
				same += it != c2->zones.end() && a.map == b.map && a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.tilesW == b.tilesW && a.tileX == b.tileX && a.tileY == b.tileY;
			}
			CHECK(same == r->zones.size());
		}
		fprintf(stderr, "%s: %zu maps, %zu of %zu zones on them (%zu moving the marker), town map %.1f ms of %.0f ms, cache %zu bytes (%.0f ms back)\n",
			t, r->regionMaps.size(), onMap, r->zones.size(), moving, info.townmapMs, info.ms, i1.cacheBytes, i2.ms);
		if (!i1.cacheFile.empty()) unlink(i1.cacheFile.c_str());
		rmdir(dir.c_str());
	}
}

int main(int argc, char** argv)
{
	testMarker();
	testCache();
	std::string roms = argc > 1 ? argv[1] : getenv("NDS_ROMS") ? getenv("NDS_ROMS") : "";
	std::string out = argc > 2 ? argv[2] : "";
	if (!out.empty()) mkdir(out.c_str(), 0755);
	if (roms.empty()) fprintf(stderr, "no ROMs given (tests/nds-townmap-test <roms-dir>, or NDS_ROMS): the games' own maps not tested\n");
	else if (DIR* d = opendir(roms.c_str()))
	{
		std::vector<std::string> files;
		while (dirent* e = readdir(d))
		{
			std::string n = e->d_name;
			if (n.size() > 4 && n.compare(n.size() - 4, 4, ".nds") == 0) files.push_back(roms + "/" + n);
		}
		closedir(d);
		std::sort(files.begin(), files.end());
		for (auto& f : files) testRom(f, out);
	}
	fprintf(stderr, "%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
