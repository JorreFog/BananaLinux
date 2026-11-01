#pragma once
// banana-fetch: the code pages names come in when they aren't UTF-8: an old FTP server's Cyrillic (CP1251, CP866), and a
// zip made by Windows' own zipper, which writes its OEM code page (CP866 on Russian systems, CP437 elsewhere)
#include <cstdint>
#include <string>

namespace banana
{
	namespace fetch
	{
		namespace charset
		{
			// 0x80..0xff of each, as Unicode
			extern const uint16_t CP437[128], CP866[128], CP1251[128];
			// "cp1251", "cp866" or "cp437" (nullptr for anything else: UTF-8, nothing to convert)
			const uint16_t* byName(const std::string& name);
			std::string toUtf8(const std::string& bytes, const uint16_t* table);
			// back into the code page (for the server): a character it doesn't have becomes '?'
			std::string fromUtf8(const std::string& utf8, const uint16_t* table);
			bool validUtf8(const std::string& s);
			void appendUtf8(std::string& out, uint32_t cp);
		}
	}
}
