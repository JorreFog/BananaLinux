#pragma once
// RomM's Get games list, banana/homesync-get.txt (the games asked for and not on the card yet, a key a line as banana-sync
// games lists them, in the order asked), and banana/homesync-get-failed.txt beside it ("<key> TAB <why>" for each game
// still on the list whose last download failed). Changed by the menu's Get games page and by banana-sync (its get
// command, and a sync taking each game off as it comes), both under get.lock in the run folder, with this one code so
// the two never write the files differently. Needs nothing of the sync's own, so banana-ui links it as it is.
#include <set>
#include <string>
#include <vector>

namespace bsync
{
	// a list file's lines: "#" a comment, a blank line nothing, trailing spaces and CRs not part of a line (a file
	// edited on a computer)
	std::vector<std::string> readListLines(const std::string& path);
	// a list file written whole, "# header" first, so a power cut or a full card leaves the old one or the new one
	bool writeListLines(const std::string& path, const std::string& header, const std::vector<std::string>& lines);
	// the games in add put on the list after those there (a game there already keeps its place, none twice), those in
	// drop taken off, and every game named in either losing its line in the failed file: asked for again, or taken
	// back, a game waits afresh. bananaDir holds the lists, runDir get.lock. False when a file can't be written (the
	// old one then left whole); waiting, when given, the games on the list after
	bool editGetList(const std::string& bananaDir, const std::string& runDir, const std::vector<std::string>& add,
		const std::set<std::string>& drop, int* waiting = nullptr);
}
