#pragma once
// banana-ui: a text file made ready for the reader (settings_text.cpp, #173, #203), without drawing anything. The
// bytes as the card has them (UTF-8 with or without its mark, UTF-16 with its mark, or an old file's Windows-1252),
// Windows', old Macs' and Unix's line ends, tabs, and whatever else a file may hold, made into lines of characters the
// menu's fonts can draw; and a line wrapped to the panel's width at its spaces (a word too long for a line broken where
// the line is full; Chinese and Japanese between any two characters, as the menu wraps them).
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace banana
{
	namespace textfile
	{
		// the files Files opens in the reader: .txt, .md (as it is written: no headings or links drawn), .nfo, .log
		bool readable(const std::string& name);

		struct Text
		{
			std::vector<std::u32string> lines;	// the file's lines: tabs as spaces, control characters gone
			bool binary = false;				// not text (NUL bytes, or many control characters): no lines
			bool legacy = false;				// not UTF-8: read as Windows-1252, the code page old guides were written in
		};
		// bytes: the file's (or its start: cut says so, and a character the cut split is left out)
		Text decode(const std::string& bytes, bool cut = false);

		// the first cap bytes of a file (size: all of it); false, and why, when it can't be read
		bool read(const std::string& path, size_t cap, std::string& bytes, uint64_t& size, std::string& why);

		// a line in pieces no wider than width: greedy, at spaces (the spaces at a break dropped, a line's own
		// indentation kept), else where the line is full; at least one character a piece. advance(c, next): the width
		// of c followed by next (0 at a piece's end), as the font lays the line out. Returns each piece's [start, end)
		// in line
		std::vector<std::pair<uint32_t, uint32_t>> wrap(const std::u32string& line, int width, const std::function<int(char32_t, char32_t)>& advance);

		// UTF-32 back to UTF-8
		std::string utf8(const std::u32string& s, size_t from = 0, size_t to = std::u32string::npos);
	}
}
