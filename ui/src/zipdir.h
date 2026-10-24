#pragma once
// A .zip's central directory, read without loading the archive: each member's name as stored, its sizes, method, flags
// and where its data is (zip64 archives too), and a member's bytes streamed out, stored or deflated (zlib), its CRC-32
// checked. banana-ui reads a DS ROM's icon from a zip with it (zip.cpp), banana-fetch unpacks zips with it.
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace banana
{
	struct ZipEntry
	{
		std::string name, extra;					// as stored: the name in the archive's encoding unless utf8()
		uint16_t flags = 0, method = 0, madeBy = 0, dosTime = 0, dosDate = 0;
		uint32_t crc = 0, attrs = 0;				// attrs: the external attributes (a Unix mode in the top 16 bits)
		uint64_t csize = 0, size = 0, local = 0;	// local: where its local header is
		bool encrypted() const { return flags & 1; }
		bool utf8() const { return flags & 0x800; }
		bool dir() const;
		bool symlink() const;						// made on Unix with S_IFLNK in its mode
		time_t mtime() const;						// the DOS date and time (stored as local time)
	};

	class ZipDir
	{
	public:
		ZipDir() = default;
		ZipDir(const ZipDir&) = delete;
		ZipDir& operator=(const ZipDir&) = delete;
		~ZipDir() { if (f) fclose(f); }
		// false: not a zip (no end of central directory), or its directory is cut short or damaged
		bool open(const std::string& path);
		const std::vector<ZipEntry>& entries() const { return list; }
		// the member's bytes in order to sink (false from it stops early, and the call returns true); false when the data
		// is cut short or fails its CRC, or is encrypted, or in a method other than stored and deflate
		bool read(const ZipEntry& e, const std::function<bool(const uint8_t*, size_t)>& sink);
	private:
		FILE* f = nullptr;
		uint64_t total = 0;
		std::vector<ZipEntry> list;
		bool at(uint64_t pos);
		bool get(void* p, size_t n);
	};
}
