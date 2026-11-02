#pragma once
// banana-ui: where the DS Pokémon companion finds the game's cartridge image. The library lists .zip files for the DS
// too, and BananaDS opens plain files only: run-bananads unpacks a zipped game once into the card's cache, named after
// the zip (/data/bananads/unzipped/<zip name>.nds), and BananaDS runs that copy. The companion reads the header from
// inside the zip (to know the game before it starts) and the ROM's data from the unpacked copy once it is whole.
#include <atomic>
#include <cstdint>
#include <string>

namespace banana::nds
{
	// a .zip by its name (the ending, any case)
	bool romZipped(const std::string& path);
	// the cartridge header (the first 0x200 bytes) of a ROM file, or of the first .nds/.dsi/.cia in a .zip; false: none
	bool romHeader(const std::string& path, uint8_t hdr[0x200]);
	// the copy run-bananads unpacks a zipped game to (unzipDir/<the zip's name without its ending>.nds)
	std::string unzippedPath(const std::string& zip, const std::string& unzipDir);
	// the unpacked copy once it is whole and of this zip: there, no unpacking going on (run-bananads unpacks into
	// unzipDir/.unzip and moves the game in place in one step), not older than the zip (run-bananads unpacks an
	// older copy again first) and the same header as the zip's game. Waits up to timeoutMs, less when stop turns true (the game
	// ended: nothing more will be unpacked); "" when it never got there
	std::string waitUnzipped(const std::string& zip, const std::string& unzipDir, const uint8_t hdr[0x200],
		const std::atomic<bool>& stop, int timeoutMs);
}
