#pragma once
// Files' servers: file names and the cards, shared by banana-ui's server pages and banana-fetch. Which files are archives
// (and the volumes of a multi-volume one), names the cards' exFAT and FAT32 take, sizes in words, and the free space
// of the card a folder is on.
#include <cstdint>
#include <string>
#include <vector>

namespace banana
{
	namespace fetch
	{
		enum Kind { NONE, ZIP, SEVENZ, RAR };
		// .zip, .7z (or .7z.001) and .rar; a multi-volume RAR only by its first volume (.part1.rar, or the .rar that the
		// old naming's .r00, .r01... go on from)
		Kind kindOf(const std::string& name);
		inline bool isArchive(const std::string& name) { return kindOf(name) != NONE; }
		// a file's place in an archive: the volume to unpack from, and a key every volume of the same archive shares
		// (false: not an archive, nor a volume of one). A single archive is its own first volume.
		bool volumeOf(const std::string& path, std::string& first, std::string& key);
		// every file of the archive that is there now: all the volumes of a multi-volume one (to delete after unpacking)
		std::vector<std::string> volumes(const std::string& path);
		// the name without an archive's suffix: "Game (USA).nds.7z" -> "Game (USA).nds", "Game.part1.rar" -> "Game"
		std::string stripArchive(const std::string& name);

		// a name the cards take, as Files' own names are (fileops.h's nameProblem): " * / : < > ? \ | and control
		// characters become _, the dots and spaces at its end go (FAT drops them itself, and the rename after a download
		// then misses the file), a hidden name or a Windows device name (CON, NUL, COM1...) gets a _ before it, and a name
		// over NAME_UNITS UTF-16 characters is cut before its extension
		std::string safeName(const std::string& name);
		const size_t NAME_UNITS = 250;	// the cards' 255, less ".part"
		// paths with slashes (a server's, or the card's)
		std::string cleanPath(const std::string& p);	// "/a/b": backslashes as slashes, no "." or ".." parts
		std::string joinPath(const std::string& dir, const std::string& name);
		std::string parentPath(const std::string& p);
		std::string baseName(const std::string& p);
		std::string lower(std::string s);
		std::string trim(const std::string& s);		// without the spaces, tabs and line ends at either end
		// "4.1 GB", "350 MB", "12 KB", "512 B" (1024s)
		std::string bytes(long long n);

		// the card a path is on (the nearest folder that is there): its free space, and whether it is FAT32 (4 GB a file
		// at most; the second SD card can be). false when it can't be told.
		struct Space { uint64_t free = 0; bool fat32 = false; };
		bool spaceAt(const std::string& path, Space& out);
		const uint64_t FAT32_MAX = 0xffffffffull;	// the largest file FAT32 holds
		// on the same filesystem (a rename moves between them): false also when either isn't there
		bool sameFilesystem(const std::string& a, const std::string& b);
	}
}
