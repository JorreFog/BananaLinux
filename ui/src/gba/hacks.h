#pragma once
// banana-ui: the GBA Pokémon ROM hacks the companion knows (owner's list, 2026-11-03: ~/Desktop/rom hacks), told apart
// by the whole ROM's CRC-32 (each is one release: another version of a hack is another ROM, added when it is
// checked). key: the hack's guide file (gba-pokemon/<key>.json) and Game::hack; engine: how its tables and RAM are
// laid out - BINARY: a patched retail game (its base game's layout, tables moved or grown), CFRU: Skeli's Complete
// Fire Red Upgrade, EXPANSION: built from the decompilation with pokeemerald-expansion (its own structs and addresses).
#include <cstddef>
#include <cstdint>

namespace banana::gba
{
	enum class Engine { BINARY, CFRU, EXPANSION };
	struct HackInfo
	{
		const char* key;		// "glazed"
		const char* title;		// "Glazed" (the companion's header)
		const char* base;		// the header's code: BPEE Emerald, BPRE FireRed
		uint32_t crc;			// the whole .gba's CRC-32
		Engine engine;
	};
	const HackInfo* hackByCrc(uint32_t crc);			// null: not one of them
	const HackInfo* hackByKey(const char* key);
	const HackInfo* allHacks(size_t& n);
	uint32_t romCrc32(const uint8_t* rom, size_t n);	// (zlib's crc32)
}
