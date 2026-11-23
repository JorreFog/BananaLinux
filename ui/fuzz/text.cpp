// Fuzz target: a text file in the reader (textfile.cpp, #173): any bytes a .txt on the card may hold (UTF-8, UTF-16,
// Windows-1252, a program's bytes, a cut in the middle of a character) decoded into lines, and each line wrapped at a
// width the input chooses with a made-up font (kerning that can be negative included). Every piece must be inside its
// line, in order, and fit unless it is a single character.
#include "../src/textfile.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>

using namespace banana;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	if (size < 2) return 0;
	bool cut = data[0] & 1;
	int width = data[1] * 3;
	std::string bytes((const char*)data + 2, size - 2);
	textfile::Text t = textfile::decode(bytes, cut);
	if (t.binary && !t.lines.empty()) abort();
	auto adv = [](char32_t c, char32_t next) { return (int)(c % 23) + (next == 'V' ? -4 : 0) + 2; };
	for (auto& l : t.lines)
	{
		size_t was = 0;
		for (auto& p : textfile::wrap(l, width, adv))
		{
			if (p.first > p.second || p.second > l.size() || p.first < was) abort();
			if (p.second - p.first > 1)
			{
				int w = 0;
				for (size_t i = p.first; i < p.second; i++) w += adv(l[i], i + 1 < p.second ? l[i + 1] : 0);
				if (w > width) abort();
			}
			for (size_t i = was; i < p.first; i++) if (l[i] != ' ') abort();	// (only spaces left out between pieces)
			was = p.second;
			textfile::utf8(l, p.first, p.second);
		}
		for (size_t i = was; i < l.size(); i++) if (l[i] != ' ') abort();
	}
	return 0;
}
