#pragma once
// banana-sync's pieces that have no business with a server: SHA-1 and MD5 of files, JSON written out, an
// EmulationStation gamelist.xml merged, paths checked. Header-only, so the host tests build them on their own.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace bsync
{
	// ---- SHA-1 and MD5 (a file's, streamed) ------------------------------------------------------------------------
	struct Sha1
	{
		uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
		uint8_t buf[64]; size_t n = 0; uint64_t len = 0;
		static uint32_t rol(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }
		void block(const uint8_t* p)
		{
			uint32_t w[80];
			for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
			for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
			uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
			for (int i = 0; i < 80; i++)
			{
				uint32_t f, k;
				if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
				else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
				else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
				else { f = b ^ c ^ d; k = 0xCA62C1D6; }
				uint32_t t = rol(a, 5) + f + e + k + w[i];
				e = d; d = c; c = rol(b, 30); b = a; a = t;
			}
			h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
		}
		void add(const void* data, size_t sz)
		{
			const uint8_t* p = (const uint8_t*)data;
			len += sz;
			while (sz)
			{
				size_t k = std::min(sz, 64 - n);
				memcpy(buf + n, p, k); n += k; p += k; sz -= k;
				if (n == 64) { block(buf); n = 0; }
			}
		}
		std::string hex()
		{
			uint64_t bits = len * 8;
			uint8_t pad = 0x80; add(&pad, 1);
			uint8_t z = 0; while (n != 56) add(&z, 1);
			uint8_t L[8]; for (int i = 0; i < 8; i++) L[i] = (uint8_t)(bits >> (56 - 8 * i));
			add(L, 8);
			char s[41];
			for (int i = 0; i < 5; i++) snprintf(s + i * 8, 9, "%08x", h[i]);
			return s;
		}
	};

	struct Md5
	{
		uint32_t h[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
		uint8_t buf[64]; size_t n = 0; uint64_t len = 0;
		void block(const uint8_t* p)
		{
			static const uint32_t K[64] = {
				0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
				0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
				0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
				0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
				0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
				0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
				0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
				0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
			static const int R[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
				4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
			uint32_t m[16];
			for (int i = 0; i < 16; i++) m[i] = p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 | (uint32_t)p[i * 4 + 3] << 24;
			uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
			for (int i = 0; i < 64; i++)
			{
				uint32_t f; int g;
				if (i < 16) { f = (b & c) | (~b & d); g = i; }
				else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
				else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
				else { f = c ^ (b | ~d); g = (7 * i) % 16; }
				uint32_t t = d; d = c; c = b;
				uint32_t x = a + f + K[i] + m[g];
				b = b + ((x << R[i]) | (x >> (32 - R[i])));
				a = t;
			}
			h[0] += a; h[1] += b; h[2] += c; h[3] += d;
		}
		void add(const void* data, size_t sz)
		{
			const uint8_t* p = (const uint8_t*)data;
			len += sz;
			while (sz)
			{
				size_t k = std::min(sz, 64 - n);
				memcpy(buf + n, p, k); n += k; p += k; sz -= k;
				if (n == 64) { block(buf); n = 0; }
			}
		}
		std::string hex()
		{
			uint64_t bits = len * 8;
			uint8_t pad = 0x80; add(&pad, 1);
			uint8_t z = 0; while (n != 56) add(&z, 1);
			uint8_t L[8]; for (int i = 0; i < 8; i++) L[i] = (uint8_t)(bits >> (8 * i));
			add(L, 8);
			char s[33];
			for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) snprintf(s + i * 8 + j * 2, 3, "%02x", (h[i] >> (8 * j)) & 0xff);
			return s;
		}
	};

	// "sha1" or "md5" of a file ("" when it can't be read)
	inline std::string hashFile(const std::string& path, const std::string& algo)
	{
		FILE* f = fopen(path.c_str(), "rb");
		if (!f) return "";
		Sha1 s; Md5 m;
		std::vector<char> b(1 << 20);
		size_t r;
		while ((r = fread(b.data(), 1, b.size(), f)) > 0) { if (algo == "md5") m.add(b.data(), r); else s.add(b.data(), r); }
		bool err = ferror(f);
		fclose(f);
		if (err) return "";
		return algo == "md5" ? m.hex() : s.hex();
	}
	inline std::string hashStr(const std::string& d, const std::string& algo)
	{
		if (algo == "md5") { Md5 m; m.add(d.data(), d.size()); return m.hex(); }
		Sha1 s; s.add(d.data(), d.size()); return s.hex();
	}

	// ---- text --------------------------------------------------------------------------------------------------------
	inline std::string jsonStr(const std::string& s)
	{
		std::string r = "\"";
		for (unsigned char c : s)
		{
			if (c == '"' || c == '\\') { r += '\\'; r += (char)c; }
			else if (c == '\n') r += "\\n";
			else if (c == '\r') r += "\\r";
			else if (c == '\t') r += "\\t";
			else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); r += b; }
			else r += (char)c;
		}
		return r + "\"";
	}
	inline std::string xmlEsc(const std::string& s)
	{
		std::string r;
		for (char c : s)
		{
			if (c == '&') r += "&amp;"; else if (c == '<') r += "&lt;"; else if (c == '>') r += "&gt;";
			else if (c == '"') r += "&quot;"; else r += c;
		}
		return r;
	}
	inline std::string xmlUnesc(const std::string& s)
	{
		std::string r;
		for (size_t i = 0; i < s.size(); i++)
		{
			if (s[i] != '&') { r += s[i]; continue; }
			size_t e = s.find(';', i);
			if (e == std::string::npos) { r += s[i]; continue; }
			std::string n = s.substr(i + 1, e - i - 1);
			if (n == "amp") r += '&'; else if (n == "lt") r += '<'; else if (n == "gt") r += '>';
			else if (n == "quot") r += '"'; else if (n == "apos") r += '\'';
			else if (!n.empty() && n[0] == '#') { long v = n.size() > 1 && (n[1] == 'x' || n[1] == 'X') ? strtol(n.c_str() + 2, nullptr, 16) : strtol(n.c_str() + 1, nullptr, 10); if (v > 0 && v < 128) r += (char)v; else r += "&" + n + ";"; }
			else { r += "&" + n + ";"; }
			i = e;
		}
		return r;
	}
	inline std::string trim(const std::string& s)
	{
		size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
		return a == std::string::npos ? "" : s.substr(a, b - a + 1);
	}
	inline std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

	// a path from a server, relative to the card's roms or data folder: no absolute path, no "..", no empty part,
	// nothing starting with a dot except the .discs folders a PS1 set lives in
	inline bool safeRel(const std::string& p)
	{
		if (p.empty() || p[0] == '/' || p.find('\\') != std::string::npos || p.find('\0') != std::string::npos) return false;
		size_t a = 0;
		while (a <= p.size())
		{
			size_t b = p.find('/', a);
			if (b == std::string::npos) b = p.size();
			std::string part = p.substr(a, b - a);
			if (part.empty() || part == "." || part == "..") return false;
			if (part[0] == '.' && part != ".discs") return false;
			a = b + 1;
		}
		return true;
	}

	// ---- gamelist.xml: the entries a sync manages replaced, everything else as it was --------------------------------
	// text: the file ("" when there is none). removePaths: entry paths (with or without "./") to drop; add: the new
	// entries, each a list of (tag, value) with "path" among them. Returns false (out untouched) when the file is
	// there but isn't a gameList we can read: the user's own list is never replaced by one with only our games.
	inline std::string normPath(std::string p) { p = trim(p); if (p.compare(0, 2, "./") == 0) p = p.substr(2); return p; }
	inline bool mergeGamelist(const std::string& text, const std::set<std::string>& removePaths,
		const std::vector<std::vector<std::pair<std::string, std::string>>>& add, std::string& out)
	{
		std::set<std::string> drop;
		for (auto& p : removePaths) drop.insert(normPath(p));
		for (auto& e : add) for (auto& kv : e) if (kv.first == "path") drop.insert(normPath(kv.second));
		std::string head, body, tail;
		if (trim(text).empty())
		{
			head = "<?xml version=\"1.0\"?>\n<gameList>\n";
			tail = "</gameList>\n";
		}
		else
		{
			size_t open = text.find("<gameList");
			size_t openEnd = open == std::string::npos ? open : text.find('>', open);
			size_t close = text.rfind("</gameList>");
			if (open == std::string::npos || openEnd == std::string::npos || close == std::string::npos || close < openEnd) return false;
			if (text[openEnd - 1] == '/') return false;	// (<gameList/>: rare; left alone)
			head = text.substr(0, openEnd + 1);
			body = text.substr(openEnd + 1, close - openEnd - 1);
			tail = text.substr(close);
		}
		// the body's <game> blocks, the ones to drop taken out (with the whitespace before them)
		std::string kept;
		size_t at = 0;
		while (true)
		{
			size_t g = body.find("<game", at);
			while (g != std::string::npos && !(body[g + 5] == '>' || body[g + 5] == ' ' || body[g + 5] == '\t' || body[g + 5] == '\n')) g = body.find("<game", g + 5);
			if (g == std::string::npos) { kept += body.substr(at); break; }
			size_t e = body.find("</game>", g);
			if (e == std::string::npos) return false;
			e += 7;
			std::string block = body.substr(g, e - g);
			size_t ps = block.find("<path>"), pe = block.find("</path>");
			std::string path = ps != std::string::npos && pe != std::string::npos && pe > ps ? normPath(xmlUnesc(block.substr(ps + 6, pe - ps - 6))) : "";
			std::string before = body.substr(at, g - at);
			if (!path.empty() && drop.count(path))
			{
				// the indentation before the block goes with it
				size_t ws = before.find_last_not_of(" \t\r\n");
				kept += ws == std::string::npos ? "" : before.substr(0, ws + 1);
			}
			else kept += before + block;
			at = e;
		}
		// trailing whitespace of the kept part, then the new entries
		size_t last = kept.find_last_not_of(" \t\r\n");
		kept = last == std::string::npos ? "" : kept.substr(0, last + 1);
		size_t first = kept.find_first_not_of("\r\n");
		kept = first == std::string::npos ? "" : kept.substr(first);
		std::string adds;
		for (auto& e : add)
		{
			adds += "\t<game>\n";
			for (auto& kv : e) adds += "\t\t<" + kv.first + ">" + xmlEsc(kv.second) + "</" + kv.first + ">\n";
			adds += "\t</game>\n";
		}
		out = head + (head.back() == '\n' ? "" : "\n") + kept + (kept.empty() ? "" : "\n") + adds + tail;
		return true;
	}
}
