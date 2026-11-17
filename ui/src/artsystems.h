#pragma once
// The systems games' artworks are fetched for (banana-scrape): a system's id -> its collection in libretro's thumbnails
// (thumbnails.libretro.com/<collection>/). Settings > Look & sound > Scrape artworks > Systems lists these; the fetch
// goes through them, skipping the ones set to Skip there (settings.conf's art.<id> = off).
#include <map>
#include <string>

namespace banana
{
	inline const std::map<std::string, std::string>& artCollections()
	{
		static const std::map<std::string, std::string> m = {
			{ "nds", "Nintendo - Nintendo DS" }, { "gba", "Nintendo - Game Boy Advance" }, { "gbc", "Nintendo - Game Boy Color" },
			{ "gb", "Nintendo - Game Boy" }, { "nes", "Nintendo - Nintendo Entertainment System" },
			{ "snes", "Nintendo - Super Nintendo Entertainment System" }, { "megadrive", "Sega - Mega Drive - Genesis" },
			{ "c64", "Commodore - 64" }, { "zxspectrum", "Sinclair - ZX Spectrum" },
			{ "psx", "Sony - PlayStation" }, { "n64", "Nintendo - Nintendo 64" }, { "psp", "Sony - PlayStation Portable" },
			{ "arcade", "FBNeo - Arcade Games" }, { "neogeo", "FBNeo - Arcade Games" }, { "cps1", "FBNeo - Arcade Games" },
			{ "cps2", "FBNeo - Arcade Games" }, { "cps3", "FBNeo - Arcade Games" } };
		return m;
	}
}
