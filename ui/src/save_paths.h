#pragma once
// Where a game's save is on the card: the one file its emulator reads at the next start, which is the one that syncs.
// The DS's is saves/nds/<stem>.sav (BananaDS's; nds-saves keeps DraStic's .dsv in step with it), every GBA core's
// saves/gba/<stem>.srm (run-retroarch points them all there), and any other RetroArch system's saves/<stem>.srm, the
// one flat folder RetroArch and BananaFront both keep (run-retroarch turns RetroArch's sorting by core off at every
// start, and saves-flat moves what a core's folder from before 0.5.3 still holds). <stem> is the ROM file's name
// without its extension (a zip's or an .m3u's own). A game on the card is taken to be the server's only when its
// contents are: the ROM banana-sync installed from it, the same MD5 as one of its files, a zip holding the same
// members (sizes and CRC32s from the zip's own directory, nothing unpacked), or an .m3u whose discs all are; MD5s are
// kept in banana/rom-hashes.tsv by size and time, so a large ROM is read once.
#include "sync_core.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bsync
{
	// a system's save folder under saves/ ("" for saves/ itself) and the save's extension; err when it can't be known
	// for sure: the system not run by a RetroArch core that is installed, or RetroArch's own settings putting saves
	// somewhere else. label: the emulator a save is sent to RomM as, the one whose form the file sent is in (melonds for
	// the DS whichever DS emulator plays, its save always BananaDS's raw .sav; a RetroArch core's file name such as gpsp
	// or snes9x); it decides nothing in this sync, and RomM keeps a slot's versions apart by it
	struct SysSaves
	{
		std::string folder, ext, err, label;
		bool known() const { return err.empty() && !ext.empty(); }
		// the save of the game whose ROM file is <stem>.<anything>, data-relative
		std::string rel(const std::string& stem) const { return "saves/" + (folder.empty() ? "" : folder + "/") + stem + "." + ext; }
	};
	SysSaves systemSaves(const std::string& system);
	// the core a RetroArch system runs, as run-retroarch picks it: core.<system> (the card's settings.conf, then the
	// image's) when installed, else the one its systems.conf run line names; "" with err when there is none
	std::string coreFor(const std::string& system, std::string& err);
	// the system's ROM folder under the roms folder (systems.conf's folder, the id by default)
	std::string romFolder(const std::string& system);
	// the ROMs in that folder and the folders below it (media/ and dot folders left out), on the card and then on the
	// second SD card while it is in: file name -> roms-relative path, the first found when two folders hold the same name
	std::map<std::string, std::string> romIndex(const std::string& folder);

	// one of a server ROM's files as its listing gives them, a zip's members as (size, CRC32 in hex)
	struct RomFile { std::string md5, sha1; std::vector<std::pair<long long, std::string>> members; };
	// the ROM at romsRel is that server ROM's (by content, never by name)
	bool romMatches(const std::string& romsRel, const std::vector<RomFile>& files, const Conf& c);
	// a zip's members (directories left out), from its central directory; empty when it can't be read
	std::vector<std::pair<long long, std::string>> zipMembers(const std::string& path);
	// the ROM hashes learnt this run, written back (the caches are only an optimisation: a failed write costs a rehash)
	void savePathCaches();
}
