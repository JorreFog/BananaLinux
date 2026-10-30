#pragma once
// banana-fetch: unpacking .zip, .7z and .rar onto a card. A zip stored or deflated is read
// here (zipdir.h); 7z, RAR and any other zip (encrypted, bzip2, LZMA...) go through 7-Zip's 7zz, which only decodes:
// the listing's checks and the moving are done here. Everything is unpacked into a folder of its own on the same card
// (<card>/.banana-unpack) and moved into place when it is all there, so a stop or a failure never leaves half a game in
// a ROM folder. An archive with a name that would land outside the folder (zip-slip) is refused whole; names are made
// valid on the cards; macOS's and Windows' clutter and links are left out; an archive holding one folder can be
// unwrapped.
#include "remote.h"
#include <atomic>
#include <functional>

namespace banana
{
	namespace fetch
	{
		struct ArchiveInfo
		{
			std::vector<std::string> files;	// as they will be named on the card
			int64_t unpacked = 0, largest = 0;
			int64_t dictionary = 0;			// the memory its decoder takes (LZMA's dictionary, PPMd's model, RAR's window)
			bool encrypted = false;
		};

		struct ArchiveOptions
		{
			std::string password;
			bool unwrap = true;				// an archive of one folder: what is in the folder
			std::string tempRoot;			// the card's root, where the unpacking folder goes ("": the target itself)
			bool ignoreMemory = false;		// unpack even with a dictionary bigger than the memory free
			std::atomic<bool>* stop = nullptr;
			std::function<void(double)> progress;	// 0..1
		};

		// the archive's files and what unpacking it takes; Err::PASSWORD when it is encrypted and no password was given
		// (Err::WRONG_PASSWORD for a wrong one, where the archive can tell)
		Result inspectArchive(const std::string& path, const ArchiveOptions& o, ArchiveInfo& info);
		Result extractArchive(const std::string& path, const std::string& dst, const ArchiveOptions& o);
		// what an unpacking cut short (a power cut) left on a card: <root>/.banana-unpack
		void removeStaleTemp(const std::string& root);
		extern const char* const TEMP_DIR;
		std::string sevenZip();				// 7zz's path, "" when there is none
		int64_t memAvailable();				// MemAvailable, -1 unknown
	}
}
