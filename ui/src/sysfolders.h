#pragma once
// banana-ui: the folders a download (Files' servers) goes to by default: each system of systems.conf with a folder on
// the card (roms/<folder>), the BIOS folder and the roms folder itself, and the one a file looks like it belongs to.
// Files lets any folder be chosen instead; these are the guess.
#include <string>
#include <vector>

namespace banana
{
	namespace sysfolders
	{
		struct Folder
		{
			std::string label, rel, system;		// rel: from the card's root ("roms/nds", "bios", "roms")
			std::vector<std::string> exts, names;	// names: what a server's folder for it may be called (lower case)
			bool playsZip = false;					// its emulator takes the games zipped (systems.conf's ext has zip)
		};
		std::vector<Folder> folders(const std::string& systemsConf);
		// the folder a file belongs to: by its extension (an archive's suffix looked through; zip, and an extension two
		// systems share, say nothing), then by the server's folder names from the deepest up. -1: none
		int suggest(const std::vector<Folder>& f, const std::string& name, const std::string& remoteDir);
		int byRel(const std::vector<Folder>& f, const std::string& rel);	// -1: none
		// the system's folder rel (from a card's root) is, or is in (case aside, as the cards are): -1 for none, and for
		// bios and roms itself
		int systemOf(const std::vector<Folder>& f, const std::string& rel);
	}
}
