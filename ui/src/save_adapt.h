#pragma once
// What a save holds, read from its bytes: whether it is blank (a core's fresh save, before the game has saved
// anything), which counts as no save at all, so it is never sent to a server and never installed from one; and
// whether a server's save can be this handheld's for a game of a system (its id, nds, gba, snes, ...): a file of
// a name the system's saves come under, of a size its cartridges' saves have, DraStic's DeSmuME footer cut off a DS
// save. A system with no rule here takes only the card's own extension, at any size.
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

	// a server file of this name can hold the system's save (by extension, RomM's time tag before it not counted;
	// own: the card's save's extension, the one rule for a system with none here). A zip, a folder save, never
	bool wireOk(const std::string& system, const std::string& name, const std::string& own);
	// the largest file a server's save for the system can be (-1: no limit known), so a larger one isn't fetched
	long long maxSave(const std::string& system);
	// a server's save as this handheld's for the system (a DS save's footer cut off, in place): "" when it can be,
	// else why not. local: the card's save for the game, if there is one, whose size it must have (or, for the
	// GBA, the 128 KiB gpSP pads every save to, or less when the card's extra bytes are all 0xFF)
	std::string adaptIn(const std::string& system, std::string& bytes, const std::string* local);
}
