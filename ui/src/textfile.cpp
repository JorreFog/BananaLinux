// banana-ui: a text file made ready for the reader (textfile.h)
#include "textfile.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace banana
{
	namespace textfile
	{
		namespace
		{
			// Windows-1252's 0x80-0x9f (0: none there); the rest of the code page is Latin-1, the first 256 code points
			const char16_t CP1252[32] = {
				0x20ac, 0, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017d, 0,
				0, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0, 0x017e, 0x0178 };

			// one UTF-8 character at p (n bytes left): its length, 0 when it isn't one (a stray byte, an overlong form, a
			// surrogate, past U+10FFFF), -1 when it is cut short by the end of the bytes
			int utf8At(const unsigned char* p, size_t n, char32_t& cp)
			{
				unsigned char c = p[0];
				int len;
				if (c < 0x80) { cp = c; return 1; }
				else if (c >= 0xc2 && c <= 0xdf) { len = 2; cp = c & 0x1f; }
				else if (c >= 0xe0 && c <= 0xef) { len = 3; cp = c & 0x0f; }
				else if (c >= 0xf0 && c <= 0xf4) { len = 4; cp = c & 0x07; }
				else return 0;
				for (int k = 1; k < len; k++)
				{
					if ((size_t)k >= n) return -1;
					if ((p[k] & 0xc0) != 0x80) return 0;
					cp = (cp << 6) | (p[k] & 0x3f);
				}
				if ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10ffff)) || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
				return len;
			}

			bool cjk(char32_t c) { return (c >= 0x2e80 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) || (c >= 0xff00 && c <= 0xffef) || (c >= 0x20000 && c <= 0x2fa1f); }
			// (the menu's own wrap's punctuation: no break before a closing mark nor after an opening one)
			bool closing(char32_t c) { return std::u32string_view(U"，。、；：！？）」』》〉】〕…—,.;:!?)]%").find(c) != std::u32string_view::npos; }
			bool opening(char32_t c) { return std::u32string_view(U"（「『《〈【〔([").find(c) != std::u32string_view::npos; }
			bool breakBetween(char32_t a, char32_t b) { return (cjk(a) || cjk(b)) && !closing(b) && !opening(a); }
		}

		bool readable(const std::string& name)
		{
			size_t dot = name.rfind('.');
			if (dot == std::string::npos || dot == 0) return false;
			std::string ext = name.substr(dot + 1);
			for (auto& c : ext) c = (char)tolower((unsigned char)c);
			return ext == "txt" || ext == "md" || ext == "nfo" || ext == "log";
		}

		Text decode(const std::string& bytes, bool cut)
		{
			Text t;
			const unsigned char* p = (const unsigned char*)bytes.data();
			size_t n = bytes.size();
			// the characters first
			std::u32string cs;
			cs.reserve(n);
			if (n >= 2 && ((p[0] == 0xff && p[1] == 0xfe) || (p[0] == 0xfe && p[1] == 0xff)))
			{
				// UTF-16 with its mark (Windows Notepad's "Unicode"); a pair split by the cut, or an odd last byte, left out
				bool le = p[0] == 0xff;
				for (size_t i = 2; i + 1 < n; i += 2)
				{
					char32_t u = le ? (p[i] | p[i + 1] << 8) : (p[i] << 8 | p[i + 1]);
					if (u >= 0xd800 && u <= 0xdbff && i + 3 < n)
					{
						char32_t v = le ? (p[i + 2] | p[i + 3] << 8) : (p[i + 2] << 8 | p[i + 3]);
						if (v >= 0xdc00 && v <= 0xdfff) { cs.push_back(0x10000 + ((u - 0xd800) << 10) + (v - 0xdc00)); i += 2; continue; }
					}
					if (u >= 0xd800 && u <= 0xdfff) continue;	// (half a pair)
					cs.push_back(u);
				}
			}
			else
			{
				// UTF-8 (its mark skipped); a byte that isn't part of a character is read as Windows-1252, so a file
				// written in it (most guides from before UTF-8) reads as it was meant to, and a stray byte in a UTF-8
				// file costs that byte alone
				size_t i = n >= 3 && p[0] == 0xef && p[1] == 0xbb && p[2] == 0xbf ? 3 : 0;
				while (i < n)
				{
					char32_t cp;
					int len = utf8At(p + i, n - i, cp);
					if (len < 0 && cut) break;	// (the cut went through this character)
					if (len > 0) { cs.push_back(cp); i += len; continue; }
					unsigned char c = p[i++];
					t.legacy = true;
					if (c >= 0x80 && c < 0xa0) { if (CP1252[c - 0x80]) cs.push_back(CP1252[c - 0x80]); }
					else cs.push_back(c);
				}
			}
			// not text: NUL bytes and control characters a text file doesn't have, more than one in 16 of its characters
			// (a log's NULs after a power cut, a few, still read as text: they are left out with the rest)
			size_t odd = 0;
			for (char32_t c : cs) if (c == 0 || (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != '\v' && c != 0x1a && c != 0x1b && c != '\b')) odd++;
			if (odd * 16 > cs.size()) { t.binary = true; return t; }
			// then the lines
			std::u32string line;
			size_t col = 0;
			auto end = [&]()
			{
				while (!line.empty() && line.back() == ' ') line.pop_back();
				t.lines.push_back(std::move(line));
				line.clear();
				col = 0;
			};
			for (size_t i = 0; i < cs.size(); i++)
			{
				char32_t c = cs[i];
				if (c == '\n' || c == '\f' || c == 0x2028 || c == 0x2029) { end(); continue; }
				if (c == '\r') { if (i + 1 < cs.size() && cs[i + 1] == '\n') i++; end(); continue; }
				if (c == '\t') { do { line.push_back(' '); col++; } while (col % 4); continue; }	// (to the next stop of 4)
				if (c == 0x1b)
				{
					// an ANSI colour code (an .nfo's): ESC [ ... and its final letter, left out
					if (i + 1 < cs.size() && cs[i + 1] == '[')
					{
						i += 2;
						while (i < cs.size() && !(cs[i] >= 0x40 && cs[i] <= 0x7e)) i++;
					}
					continue;
				}
				// control characters, and the marks that draw nothing (a byte order mark in the middle, a zero-width space)
				if (c < 0x20 || (c >= 0x7f && c < 0xa0) || c == 0xfeff || c == 0x200b) continue;
				line.push_back(c);
				col++;
			}
			// (the last line: a file's last line end makes no empty line after it)
			if (!line.empty() || (!cs.empty() && cs.back() != '\n' && cs.back() != '\r' && cs.back() != '\f')) end();
			return t;
		}

		bool read(const std::string& path, size_t cap, std::string& bytes, uint64_t& size, std::string& why)
		{
			bytes.clear();
			size = 0;
			int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
			if (fd < 0) { why = strerror(errno); return false; }
			struct stat st;
			if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { why = "not a file"; close(fd); return false; }
			size = (uint64_t)st.st_size;
			bytes.resize((size_t)std::min<uint64_t>(size, cap));
			size_t got = 0;
			while (got < bytes.size())
			{
				ssize_t r = ::read(fd, &bytes[got], bytes.size() - got);
				if (r < 0 && errno == EINTR) continue;
				if (r < 0) { why = strerror(errno); close(fd); bytes.clear(); return false; }
				if (r == 0) break;	// (shorter than it was: a log written as it is read)
				got += (size_t)r;
			}
			bytes.resize(got);
			close(fd);
			return true;
		}

		std::vector<std::pair<uint32_t, uint32_t>> wrap(const std::u32string& s, int width, const std::function<int(char32_t, char32_t)>& advance)
		{
			std::vector<std::pair<uint32_t, uint32_t>> out;
			const size_t n = s.size();
			if (n == 0) { out.push_back({ 0, 0 }); return out; }
			size_t a = 0;
			while (a < n)
			{
				// [a, b) grows while it fits: run is its width without its last character's own (that one's advance
				// followed by the next is in run; at a piece's end, followed by nothing)
				long long run = 0;
				size_t b = a, brkEnd = 0, brkNext = 0;	// the last place it can break: the piece's end, the next one's start
				bool brk = false, words = false;		// a break found; a character other than a space since a
				for (; b < n; b++)
				{
					if ((long long)run + advance(s[b], 0) > width && b > a) break;
					if (s[b] == ' ')
					{
						// (indentation is no break: the line's own spaces before its first word stay with it)
						if (words) { brk = true; brkEnd = b; brkNext = b + 1; }
					}
					else
					{
						words = true;
						if (b + 1 < n && s[b + 1] != ' ' && breakBetween(s[b], s[b + 1])) { brk = true; brkEnd = b + 1; brkNext = b + 1; }
					}
					run += b + 1 < n ? advance(s[b], s[b + 1]) : 0;
				}
				size_t end, next;
				if (b == n) { end = n; next = n; }
				else if (s[b] == ' ' && words) { end = b; next = b; }	// (full just before a space)
				else if (brk) { end = brkEnd; next = brkNext; }
				else { end = b; next = b; }	// (a word longer than the line: broken where it is full)
				while (end > a && s[end - 1] == ' ' && words) end--;
				out.push_back({ (uint32_t)a, (uint32_t)end });
				while (next < n && s[next] == ' ') next++;
				a = next;
			}
			return out;
		}

		std::string utf8(const std::u32string& u, size_t from, size_t to)
		{
			std::string o;
			to = std::min(to, u.size());
			for (size_t i = from; i < to; i++)
			{
				char32_t ch = u[i];
				if (ch < 0x80) o += (char)ch;
				else if (ch < 0x800) { o += (char)(0xc0 | (ch >> 6)); o += (char)(0x80 | (ch & 0x3f)); }
				else if (ch < 0x10000) { o += (char)(0xe0 | (ch >> 12)); o += (char)(0x80 | ((ch >> 6) & 0x3f)); o += (char)(0x80 | (ch & 0x3f)); }
				else { o += (char)(0xf0 | (ch >> 18)); o += (char)(0x80 | ((ch >> 12) & 0x3f)); o += (char)(0x80 | ((ch >> 6) & 0x3f)); o += (char)(0x80 | (ch & 0x3f)); }
			}
			return o;
		}
	}
}
