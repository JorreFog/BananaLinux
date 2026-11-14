// banana-ui: the GBA Pokémon ROM hacks the companion knows (hacks.h).
#include "gba/hacks.h"
#include <cstring>
#include <zlib.h>

namespace banana::gba
{
	// (engines as surveyed on the ROMs 2026-11-03: the Glazed releases keep Emerald's layout with moved tables; Gaia is
	// a patched FireRed too - none of CFRU's own patches (Pokémon kept encrypted, the Pokédex and the bag where FireRed
	// keeps them), its tables grown in place of them: 1024 species slots, 16 evolutions a species, 511 moves with the
	// physical/special split, Fairy at 23; the other four are decompilation builds)
	static const HackInfo HACKS[] = {
		{ "glazed", "Glazed", "BPEE", 0xb56d1a8au, Engine::BINARY },					// Glazed v8.6.3
		{ "golden-glazed", "Golden Glazed", "BPEE", 0x4cd5e52du, Engine::BINARY },		// v2.6
		{ "glazed-unofficial", "Glazed (Unofficial)", "BPEE", 0xfda9453bu, Engine::BINARY },	// Unofficial v9.1.0
		{ "glazed-unofficial", "Glazed", "BPEE", 0xe4ceffe2u, Engine::BINARY },			// v9.2.0 (the 9.x line: 9.1.0's guide, all 56 foes found)
		{ "gaia", "Gaia", "BPRE", 0xb9465c5au, Engine::BINARY },						// v3.2
		{ "heart-and-soul", "Heart & Soul", "BPEE", 0x01713508u, Engine::EXPANSION },	// v2.0.6
		{ "lazarus", "Lazarus", "BPEE", 0x558ae42fu, Engine::EXPANSION },				// v2
		{ "seaglass", "Seaglass", "BPEE", 0x14465a33u, Engine::EXPANSION },				// v3.0
		{ "soulgold", "SoulGold", "BPEE", 0xba7394a6u, Engine::EXPANSION },				// v1.1.4
		{ "soulgold", "SoulGold", "BPEE", 0xf47cd99cu, Engine::EXPANSION },				// v1.2 (the 1.1.4 guide: every foe and place found, 2026-11-06)
		{ "soulgold", "SoulGold", "BPEE", 0x5578ebeau, Engine::EXPANSION },				// v1.2.1 (the same)
		{ "unbound", "Unbound", "BPRE", 0x4b3d4957u, Engine::CFRU },					// v2.1.1.1
	};

	const HackInfo* hackByCrc(uint32_t crc)
	{
		for (const HackInfo& h : HACKS) if (h.crc == crc) return &h;
		return nullptr;
	}
	const HackInfo* hackByKey(const char* key)
	{
		if (!key) return nullptr;
		for (const HackInfo& h : HACKS) if (!strcmp(h.key, key)) return &h;
		return nullptr;
	}
	const HackInfo* allHacks(size_t& n) { n = sizeof HACKS / sizeof HACKS[0]; return HACKS; }
	uint32_t romCrc32(const uint8_t* rom, size_t n) { return (uint32_t)crc32(0L, rom, (uInt)n); }
}
