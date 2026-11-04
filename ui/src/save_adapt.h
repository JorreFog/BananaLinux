#pragma once
// What a save holds, read from its bytes: whether it is blank (a core's fresh save, before the game has saved
// anything), which counts as no save at all, so it is never sent to a server and never installed from one.
#include <string>

namespace bsync
{
	// the folder under saves/ a save is in (gba, nds, or a core's library_name from before 0.5.3's flat saves), "" for
	// one in saves/ itself or anything else
	std::string saveFolder(const std::string& rel);
	// empty, all 0x00 or all 0xFF anywhere; in saves/ itself ("") or a core's own folder also the fresh fill or
	// formatted image of a core whose saves go there (Snes9x's, PCSX-ReARMed's, Mupen64Plus-Next's)
	bool blankSave(const std::string& bytes, const std::string& folder);
	// PCSX-ReARMed's formatted empty memory card (131072 bytes)
	const std::string& pcsxBlankCard();
}
