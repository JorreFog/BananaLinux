// the reader's text (textfile.cpp): the encodings, line ends, tabs and control characters a file may have, a file that
// isn't text, and the wrap (every piece fits, nothing is lost but the spaces at a break) with a made-up font: 10 px a
// character, 20 a CJK one, kerning between "A" and "V"
#include "textfile.h"
#include <cstdio>
#include <string>

using namespace banana;
static int fails;
static void expect(bool ok, const std::string& what) { if (!ok) { fprintf(stderr, "FAIL %s\n", what.c_str()); fails++; } }

static int adv(char32_t c, char32_t next)
{
	int a = c >= 0x2e80 ? 20 : 10;
	if (c == 'A' && next == 'V') a -= 3;
	return a;
}

static int widthOf(const std::u32string& s, size_t a, size_t b)
{
	int w = 0;
	for (size_t i = a; i < b; i++) w += adv(s[i], i + 1 < b ? s[i + 1] : 0);
	return w;
}

static std::string lines(const textfile::Text& t)
{
	std::string r;
	for (size_t i = 0; i < t.lines.size(); i++) r += (i ? "|" : "") + textfile::utf8(t.lines[i]);
	return r;
}

static void decodes(const std::string& bytes, const std::string& want, const char* what, bool legacy = false, bool cut = false)
{
	textfile::Text t = textfile::decode(bytes, cut);
	expect(!t.binary, std::string(what) + ": read as not text");
	expect(lines(t) == want, std::string(what) + ": \"" + lines(t) + "\", not \"" + want + "\"");
	expect(t.legacy == legacy, std::string(what) + ": legacy " + (t.legacy ? "set" : "not set"));
}

// every piece fits (or is one character), the pieces keep the line's order, and between them only spaces are left out
static void wraps(const std::u32string& s, int width, const char* what)
{
	auto ps = textfile::wrap(s, width, adv);
	size_t was = 0;
	for (auto& p : ps)
	{
		expect(p.first <= p.second && p.second <= s.size(), std::string(what) + ": a piece out of the line");
		expect(p.second - p.first <= 1 || widthOf(s, p.first, p.second) <= width, std::string(what) + ": a piece too wide: " + textfile::utf8(s, p.first, p.second));
		for (size_t i = was; i < p.first; i++) expect(s[i] == ' ', std::string(what) + ": a character lost");
		expect(p.first >= was, std::string(what) + ": pieces out of order");
		was = p.second;
	}
	for (size_t i = was; i < s.size(); i++) expect(s[i] == ' ', std::string(what) + ": the end lost");
}

int main()
{
	expect(textfile::readable("Walkthrough.TXT") && textfile::readable("a.md") && textfile::readable("x.nfo") && textfile::readable("boot.log"), "readable");
	expect(!textfile::readable("game.nds") && !textfile::readable(".txt") && !textfile::readable("txt"), "not readable");

	decodes("one\r\ntwo\r\n\r\nfour\n", "one|two||four", "CRLF");
	decodes("old\rmac\r", "old|mac", "CR");
	decodes("no end", "no end", "no line end at the end");
	decodes("", "", "empty");
	decodes("\n", "", "one empty line");
	decodes("a\tb\n\tc\n", "a   b|    c", "tabs to the next stop of 4");
	decodes("\xef\xbb\xbfmark", "mark", "UTF-8's mark");
	decodes("caf\xc3\xa9 \xe4\xb8\xad\xe6\x96\x87 \xf0\x9f\x8d\x8c", "caf\xc3\xa9 \xe4\xb8\xad\xe6\x96\x87 \xf0\x9f\x8d\x8c", "UTF-8: Latin, CJK, beyond the BMP");
	decodes("caf\xe9 \x93quoted\x94", "caf\xc3\xa9 \xe2\x80\x9cquoted\xe2\x80\x9d", "Windows-1252", true);
	decodes("ok \xc3\xa9 then \xff", "ok \xc3\xa9 then \xc3\xbf", "UTF-8 with a stray byte", true);
	decodes("\xed\xa0\x80", "\xc3\xad\xc2\xa0\xe2\x82\xac", "a surrogate in UTF-8 isn't a character", true);
	decodes("\xc0\xaf", "\xc3\x80\xc2\xaf", "an overlong form isn't one", true);
	decodes("cut \xe4\xb8", "cut", "a character the cut split", false, true);
	decodes(std::string("\xff\xfe" "a\0\r\0\n\0" "\x2d\x4e", 10), "a|\xe4\xb8\xad", "UTF-16LE");
	decodes(std::string("\xfe\xff\0b\xd8\x3c\xdf\x4c", 8), "b\xf0\x9f\x8d\x8c", "UTF-16BE, a pair");
	decodes(std::string("\xff\xfe" "a\0\x00\xd8" "b\0", 8), "ab", "UTF-16: half a pair left out");
	decodes("\x1b[1;33mbright\x1b[0m\n", "bright", "ANSI colours");
	decodes("bell\x07 del\x7f \xc2\x85x\xe2\x80\x8b and some words\n", "bell del x and some words", "control characters left out");
	decodes("trailing   \n", "trailing", "spaces at a line's end");
	decodes("page\fbreak", "page|break", "a form feed");
	// a log's NULs after a power cut: a few, still text; a program: not text
	std::string log(2000, 'a');
	log += std::string(20, '\0');
	expect(!textfile::decode(log).binary, "a few NULs: text");
	std::string bin;
	for (int i = 0; i < 4096; i++) bin += (char)((i * 7919) & 0xff);
	expect(textfile::decode(bin).binary && textfile::decode(bin).lines.empty(), "bytes of all kinds: not text");
	expect(textfile::decode(std::string("\xff\xfe\0\0\0\0", 6)).binary, "UTF-16 NULs: not text");

	// the wrap
	auto pieces = [](const std::u32string& s, int w)
	{
		std::string r;
		for (auto& p : textfile::wrap(s, w, adv)) r += (r.empty() ? "" : "|") + textfile::utf8(s, p.first, p.second);
		return r;
	};
	auto same = [&](const std::u32string& s, int w, const std::string& want, const char* what)
	{
		std::string got = pieces(s, w);
		expect(got == want, std::string(what) + ": \"" + got + "\", not \"" + want + "\"");
	};
	same(U"", 100, "", "an empty line: one empty piece");
	expect(textfile::wrap(U"", 100, adv).size() == 1, "an empty line is a line");
	same(U"short", 100, "short", "a line that fits");
	same(U"the quick brown fox", 100, "the quick|brown fox", "at a space");
	same(U"the   quick", 50, "the|quick", "the spaces at a break left out");
	same(U"    indented line here", 120, "    indented|line here", "indentation kept");
	same(U"abcdefghijklmno", 50, "abcde|fghij|klmno", "a word longer than the line");
	same(U"a abcdefghijklmno", 50, "a|abcde|fghij|klmno", "a long word after a short one");
	same(U"AVAVAVAV", 58, "AVAVAV|AV", "kerning counted");
	same(U"中文的文字，很长", 60, "中文的|文字，|很长", "CJK between characters, not before its comma");
	same(U"x", 0, "x", "no room at all: a character a piece");
	std::u32string longLine;
	for (int i = 0; i < 20000; i++) longLine += i % 9 == 8 ? U' ' : (char32_t)('a' + i % 26);
	wraps(longLine, 300, "a long line of words");
	std::u32string mixed = U"   Chapter 1 中文的文字，很长 and a verylongwordthatdoesnotfitanywhere   end  ";
	for (int w : { 0, 5, 10, 25, 60, 113, 400 }) wraps(mixed, w, "a mixed line");
	wraps(std::u32string(500, U' '), 50, "spaces alone");
	if (!fails) fprintf(stderr, "textfile-test: all passed\n");
	return fails ? 1 : 0;
}
