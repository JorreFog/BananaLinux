// banana-ui: GBA Pokémon ROM hacks built from pokeemerald-expansion (expansion.h).
#include "gba/expansion.h"
#include "gba/gen3rom.h"
#include "nds/nitro.h"
#include "nds/rom.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <map>
#include <mutex>
#include <tuple>
#include <set>
#include <unordered_map>

namespace banana::gba
{
	using namespace banana::nds;

	namespace
	{
		uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
		uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

		// a ROM pointer at rom[at] to len bytes inside the ROM: its offset, -1 not one
		long ptrAt(const uint8_t* rom, size_t n, size_t at, size_t len = 1)
		{
			if (at + 4 > n) return -1;
			uint32_t v = le32(rom + at);
			if (v < 0x08000000u || (size_t)(v - 0x08000000u) + len > n) return -1;
			return (long)(v - 0x08000000u);
		}
		uint32_t u32At(const uint8_t* rom, size_t n, size_t at) { return at + 4 <= n ? le32(rom + at) : 0; }
		uint16_t u16At(const uint8_t* rom, size_t n, size_t at) { return at + 2 <= n ? le16(rom + at) : 0; }

		// ---- text ---------------------------------------------------------------------------------------------
		// a text in the games' character set: letters, digits, signs and spaces up to the end (0xFF) within max bytes,
		// at least one letter (control codes allowed in long texts)
		bool isText(const uint8_t* rom, size_t n, long at, size_t max, bool allowEmpty = false, size_t minLen = 1)
		{
			if (at < 0) return false;
			size_t letters = 0;
			for (size_t k = 0; k < max && (size_t)at + k < n; k++)
			{
				uint8_t c = rom[at + k];
				if (c == 0xff) return k >= minLen ? letters > 0 : allowEmpty && k == 0;
				if (c >= 0xbb && c <= 0xee) letters++;
				else if (c == 0xfc || c == 0xfd) { k++; continue; }
				else if (c >= 0xf7 && c < 0xfa) return false;
			}
			return false;
		}
		bool isName(const uint8_t* rom, size_t n, long at, size_t max) { return isText(rom, n, at, max, false, 1); }

		std::string text(const uint8_t* rom, size_t n, long at, size_t max)
		{
			std::string s;
			if (at < 0) return s;
			static const uint8_t FC_ARGS[0x19] = { 0, 1, 1, 1, 3, 1, 1, 0, 1, 0, 0, 2, 1, 1, 1, 0, 2, 1, 1, 1, 1, 0, 0, 0, 0 };
			for (size_t i = 0; (size_t)at + i < n && i < max; i++)
			{
				uint8_t b = rom[at + i];
				if (b == 0xff) break;
				if (b == 0xfe || b == 0xfa || b == 0xfb)
				{
					if (!s.empty() && s.back() != ' ' && s.back() != '-') s += ' ';
					continue;
				}
				if (b == 0xfc)
				{
					uint8_t c = (size_t)at + i + 1 < n ? rom[at + i + 1] : 0;
					i += 1 + (c < sizeof FC_ARGS ? FC_ARGS[c] : 0);
					continue;
				}
				if (b == 0xfd) { i++; continue; }
				static const char* const LIG[7] = { "PK", "MN", "PO", "Ké", "BL", "OC", "K" };
				if (b >= 0x53 && b <= 0x59) { s += LIG[b - 0x53]; continue; }
				s += pkmn::decodeText(&b, 1);
			}
			while (!s.empty() && s.back() == ' ') s.pop_back();
			return s;
		}
		// a name the ROM keeps in capitals ("BULBASAUR") as the DS games write it; one in mixed case as it is
		std::string niceName(const std::string& s)
		{
			for (char c : s) if (c >= 'a' && c <= 'z') return s;
			return nameCase(s);
		}
		// letters and digits only, lower case, é as e ("POKé BALL" = "Poké Ball")
		std::string norm(const std::string& s)
		{
			std::string o;
			for (size_t i = 0; i < s.size(); i++)
			{
				unsigned char c = (unsigned char)s[i];
				if (c == 0xc3 && i + 1 < s.size() && ((unsigned char)s[i + 1] == 0xa9 || (unsigned char)s[i + 1] == 0x89)) { o += 'e'; i++; continue; }
				if (isalnum(c)) o += (char)tolower(c);
			}
			return o;
		}
		// a TM's or HM's number by its name ("TM01", "TM01 Work Up"): TMs 1-, HMs 1-8 as 92 + n (the DS games'); 0 none
		int tmNumberOf(const std::string& k)
		{
			if (k.size() < 4 || (k[0] != 't' && k[0] != 'h') || k[1] != 'm' || !isdigit((unsigned char)k[2]) || !isdigit((unsigned char)k[3])) return 0;
			if (k.size() > 4 && isdigit((unsigned char)k[4])) return 0;		// (TM100 and on: not the DS games')
			const int num = (k[2] - '0') * 10 + (k[3] - '0');
			if (k[0] == 't') return num >= 1 && num <= 92 ? num : 0;
			return num >= 1 && num <= 8 ? 92 + num : 0;
		}
		uint32_t fnv(const std::string& s)
		{
			uint32_t h = 2166136261u;
			for (unsigned char c : s) { h ^= c; h *= 16777619u; }
			return h;
		}

		// ---- types ----------------------------------------------------------------------------------------------
		// expansion's TYPE_ numbering (1.9 on: NONE 0, NORMAL 1 .. STEEL 9, MYSTERY 10, FIRE 11 .. DARK 18, FAIRY 19,
		// STELLAR 20) as nds::Type and as pkmn's (??? at 9, Fairy at 23)
		Type ndsTypeOf(uint8_t t)
		{
			if (t >= 1 && t <= 9) return (Type)(t - 1);
			if (t >= 11 && t <= 18) return (Type)(t - 2);
			if (t == 19) return FAIRY;
			return NO_TYPE;
		}
		uint8_t pkmnTypeOf(uint8_t t)
		{
			if (t >= 1 && t <= 18) return (uint8_t)(t - 1);
			if (t == 19) return pkmn::FAIRY;
			return 255;
		}
	}

	// ---- smol (pokeemerald-expansion's decompress.c: tANS-coded lengths/offsets and symbols, then LZ-like
	// instructions), the format's facts as that file documents them --------------------------------------------------
	namespace
	{
		struct BitReader
		{
			const uint8_t* rom; size_t n; size_t at; uint32_t cur = 0; uint32_t bit = 0; uint32_t state = 0; bool bad = false;
			uint32_t word(size_t a) { if (a + 4 > n) { bad = true; return 0; } return le32(rom + a); }
		};
		// the decoding table for 16 symbols' frequencies (three words: five 6-bit frequencies each, the 16th from the
		// top two bits of each)
		bool buildTable(const uint8_t* rom, size_t n, size_t at, uint32_t table[64])
		{
			if (at + 12 > n) return false;
			uint32_t f[16] = {};
			for (int i = 0; i < 3; i++)
			{
				uint32_t w = le32(rom + at + 4 * i);
				for (int k = 0; k < 5; k++) f[i * 5 + k] = (w >> (6 * k)) & 0x3f;
				f[15] += (w & 0xc0000000u) >> (30 - 2 * i);
			}
			// sYkTemplate[x]: k = 6 - floor(log2 x) (x < 64), y = (x << k) - 64, mask = (1 << k) - 1
			auto tmpl = [](uint32_t x) -> uint32_t {
				int k = 0;
				while (((x << k)) < 64) k++;
				uint32_t y = (x << k) - 64, mask = (1u << k) - 1;
				return (uint32_t)(k & 7) | y << 8 | mask << 16;
			};
			uint32_t pos = 0;
			for (uint32_t i = 0; i < 16; i++)
				for (uint32_t j = 0; j < f[i]; j++)
				{
					if (pos >= 64) return false;
					table[pos++] = tmpl(f[i] + j) | i << 3;
				}
			return pos == 64;
		}
		// one symbol (4 bits) from the stream
		uint32_t tansNibble(BitReader& r, const uint32_t table[64])
		{
			uint32_t yk = table[r.state & 63];
			uint32_t sym = (yk & 0xff) >> 3, k = yk & 7;
			r.state = ((yk >> 8) & 0xff) + ((r.cur >> r.bit) & (yk >> 16));
			r.bit += k;
			if (r.bit >= 32)
			{
				r.at += 4;
				r.cur = r.word(r.at);
				r.bit -= 32;
				if (r.bit) r.state += (r.cur & ((1u << r.bit) - 1)) << (k - r.bit);
			}
			return sym;
		}

		bool smol(const uint8_t* rom, size_t n, size_t at, std::vector<uint8_t>& out, size_t maxOut)
		{
			if (at + 8 > n) return false;
			const uint32_t w0 = le32(rom + at), w1 = le32(rom + at + 4);
			const uint32_t mode = w0 & 15, imageSize = (w0 >> 4) & 0x3fff, symSize = w0 >> 18;
			const uint32_t initial = w1 & 63, bitstream = (w1 >> 6) & 0x1fff, loSize = w1 >> 19;
			if (mode < 1 || mode > 6 || !loSize || !symSize) return false;
			const size_t size = (size_t)imageSize * 4;
			if (size == 0 || size > maxOut || size % 32) return false;
			const size_t data = at + 8;
			const bool loEnc = mode == 4 || mode == 5 || mode == 6, symEnc = mode == 2 || mode == 3 || mode == 5 || mode == 6,
				delta = mode == 3 || mode == 6;
			std::vector<uint8_t> lo(loSize);
			std::vector<uint16_t> sym(symSize);
			size_t left = data;
			if (mode == 1)
			{
				if (data + symSize * 2 + loSize > n) return false;
				for (uint32_t i = 0; i < symSize; i++) sym[i] = le16(rom + data + 2 * i);
				memcpy(lo.data(), rom + data + symSize * 2, loSize);
			}
			else
			{
				size_t loFreq = 0, symFreq = 0, stream = 0;
				if (mode == 4) { loFreq = data; stream = data + 12; }
				else if (mode == 2 || mode == 3) { symFreq = data; stream = data + 12; }
				else { loFreq = data; symFreq = data + 12; stream = data + 24; }
				BitReader r{ rom, n, stream };
				r.cur = r.word(stream);
				r.state = initial;
				uint32_t table[64];
				if (loEnc)
				{
					if (!buildTable(rom, n, loFreq, table)) return false;
					for (uint32_t i = 0; i < loSize; i++)
					{
						uint32_t a = tansNibble(r, table), b = tansNibble(r, table);
						lo[i] = (uint8_t)(a | b << 4);
					}
					left += 12;
				}
				if (symEnc)
				{
					if (!buildTable(rom, n, symFreq, table)) return false;
					uint32_t acc = 0;
					for (uint32_t i = 0; i < symSize; i++)
					{
						uint32_t v = 0;
						for (int k = 0; k < 4; k++)
						{
							uint32_t s = tansNibble(r, table);
							if (delta) { acc = (acc + s) & 15; s = acc; }
							v |= s << (4 * k);
						}
						sym[i] = (uint16_t)v;
					}
					left += 12;
				}
				if (r.bad) return false;
				left += 4 * (size_t)bitstream;
				if (!symEnc)
				{
					if (left + symSize * 2 > n) return false;
					for (uint32_t i = 0; i < symSize; i++) sym[i] = le16(rom + left + 2 * i);
					left += symSize * 2;
				}
				if (!loEnc)
				{
					if (left + loSize > n) return false;
					memcpy(lo.data(), rom + left, loSize);
				}
			}
			// the instructions: a length and an offset (7 bits each, a second byte when the top bit is set)
			std::vector<uint16_t> dst;
			dst.reserve(size / 2);
			size_t li = 0, si = 0;
			auto loAt = [&](size_t i) -> uint32_t { return i < lo.size() ? lo[i] : 0; };
			while (li < lo.size())
			{
				uint32_t len, off;
				if (loAt(li) & 0x80)
				{
					len = (loAt(li) & 0x7f) | loAt(li + 1) << 7;
					off = loAt(li + 2) & 0x7f;
					if (loAt(li + 2) & 0x80) { off |= loAt(li + 3) << 7; li += 4; } else li += 3;
				}
				else
				{
					len = loAt(li) & 0x7f;
					off = loAt(li + 1) & 0x7f;
					if (loAt(li + 1) & 0x80) { off |= loAt(li + 2) << 7; li += 3; } else li += 2;
				}
				if (dst.size() + (len ? len + 1 : off) > size / 2 + 64) return false;
				if (len)
				{
					if (si >= sym.size()) return false;
					const uint16_t v = sym[si++];
					dst.push_back(v);
					if (off == 1) dst.insert(dst.end(), len, v);
					else
					{
						if (off == 0 || off > dst.size()) return false;
						for (uint32_t k = 0; k < len; k++) dst.push_back(dst[dst.size() - off]);
					}
				}
				else
				{
					if (si + off > sym.size()) return false;
					dst.insert(dst.end(), sym.begin() + (long)si, sym.begin() + (long)(si + off));
					si += off;
				}
			}
			if (dst.size() * 2 < size) return false;
			out.resize(size);
			for (size_t i = 0; i < size / 2; i++) { out[2 * i] = (uint8_t)dst[i]; out[2 * i + 1] = (uint8_t)(dst[i] >> 8); }
			return true;
		}
	}

	bool decompressPic(const uint8_t* rom, size_t n, size_t at, std::vector<uint8_t>& out, size_t maxOut)
	{
		out.clear();
		if (at + 8 > n || at % 4) return false;
		if (rom[at] == 0x10) return pkmn::lz77(rom, n, at, out, maxOut);
		return smol(rom, n, at, out, maxOut);
	}

	bool expansionVersion(const uint8_t* rom, size_t n, int& major, int& minor, int& patch)
	{
		for (size_t a = 0x100; a + 0x18 <= n && a < 0x400; a += 2)
			if (!memcmp(rom + a, "RHHEXP", 6)) { major = rom[a + 6]; minor = rom[a + 7]; patch = rom[a + 8]; return true; }
		return false;
	}

	// ---- the code: literal pools ---------------------------------------------------------------------------------
	namespace
	{
		// every Thumb "ldr rd, [pc, #imm]" in the code (the first 16 MB) whose word is in RAM: the word, the instruction's
		// offset and its register
		struct Load { uint32_t at; uint8_t reg; };
		struct Code
		{
			const uint8_t* rom; size_t n, end;
			std::unordered_map<uint32_t, std::vector<Load>> loads;		// RAM word -> its loads
			std::unordered_map<uint32_t, int> words;					// RAM word -> times it is in the ROM at all
			Code(const uint8_t* r, size_t nn) : rom(r), n(nn), end(std::min<size_t>(nn, 16u << 20))
			{
				for (size_t a = 0; a + 4 <= end; a += 4)
				{
					uint32_t v = le32(rom + a);
					if ((v >> 24 == 2 && (v & 0xffffff) < 0x40000) || (v >> 24 == 3 && (v & 0xffffff) < 0x8000)) words[v]++;
				}
				for (size_t a = 0; a + 2 <= end; a += 2)
				{
					uint16_t i = le16(rom + a);
					if (i >> 11 != 0x09) continue;
					size_t t = ((a + 4) & ~(size_t)3) + (size_t)(i & 0xff) * 4;
					if (t + 4 > end) continue;
					uint32_t v = le32(rom + t);
					if ((v >> 24 == 2 && (v & 0xffffff) < 0x40000) || (v >> 24 == 3 && (v & 0xffffff) < 0x8000))
						loads[v].push_back({ (uint32_t)a, (uint8_t)(i >> 8 & 7) });
				}
			}
			uint16_t h(size_t a) const { return a + 2 <= end ? le16(rom + a) : 0; }
			// the word an instruction at a loads from its pool (0 none)
			uint32_t literal(size_t a) const
			{
				uint16_t i = h(a);
				if (i >> 11 != 0x09) return 0;
				size_t t = ((a + 4) & ~(size_t)3) + (size_t)(i & 0xff) * 4;
				return t + 4 <= end ? le32(rom + t) : 0;
			}
			const std::vector<Load>& of(uint32_t v) const { static const std::vector<Load> none; auto it = loads.find(v); return it == loads.end() ? none : it->second; }
			int count(uint32_t v) const { auto it = words.find(v); return it == words.end() ? 0 : it->second; }

			// how the code reads and writes through a base: (size 1/2/4, offset) -> times, over the instructions after
			// each load of it (the register followed through register adds; deref: after "ldr r, [r]" first)
			std::map<std::pair<int, int>, int> fields(uint32_t base, bool deref = false) const
			{
				std::map<std::pair<int, int>, int> c;
				for (const Load& l : of(base))
				{
					uint8_t regs = (uint8_t)(1 << l.reg);
					size_t a = l.at + 2;
					if (deref)
					{
						uint16_t j = h(a);
						if (!(j >> 11 == 0x0d && ((j >> 6) & 31) == 0 && ((j >> 3) & 7) == l.reg)) continue;
						regs = (uint8_t)(1 << (j & 7));
						a += 2;
					}
					for (int k = 0; k < 9; k++, a += 2)
					{
						uint16_t j = h(a);
						if (j >> 9 == 0x0c)		// adds rd, rn, rm
						{
							int rm = (j >> 6) & 7, rn = (j >> 3) & 7;
							if (regs >> rm & 1 || regs >> rn & 1) regs |= (uint8_t)(1 << (j & 7));
							continue;
						}
						int op = j >> 11;
						if (op >= 0x0c && op <= 0x11)
						{
							int rn = (j >> 3) & 7, imm = (j >> 6) & 31;
							int sz = op <= 0x0d ? 4 : op <= 0x0f ? 1 : 2;
							if (regs >> rn & 1) c[{ sz, imm * sz }]++;
						}
						if ((j & 0xff00) == 0xbd00 || j == 0x4770) break;	// (pop pc, bx lr)
					}
				}
				return c;
			}
			// constants loaded from pools within w instructions of a load of base, in [lo, hi)
			std::map<uint32_t, int> nearConsts(uint32_t base, uint32_t lo, uint32_t hi, int w = 6) const
			{
				std::map<uint32_t, int> c;
				for (const Load& l : of(base))
					for (int k = -w; k <= w; k++)
					{
						if (k == 0 || (long)l.at + 2 * k < 0) continue;
						uint32_t v = literal(l.at + 2 * k);
						if (v >= lo && v < hi) c[v]++;
					}
				return c;
			}
			// the bit tests of a u32 read at base ("ldr r, =base; ldr r, [r]; lsls/ands/tst"): how many
			int bitTests(uint32_t base) const
			{
				int k = 0;
				for (const Load& l : of(base))
					for (size_t d : { 2, 4 })
					{
						uint16_t j = h(l.at + d);
						if (j >> 11 == 0x0d && ((j >> 6) & 31) == 0 && ((j >> 3) & 7) == l.reg)
						{
							uint16_t x = h(l.at + d + 2);
							int r = j & 7;
							if ((x >> 11 == 0 && ((x >> 3) & 7) == r) || (x >> 6 == 0x100 && ((x >> 3) & 7) == r) || x >> 6 == 0x108) k++;
							break;
						}
					}
				return k;
			}
			// the multipliers next to the loads of base ("movs r, #imm; ... muls"): imm -> times
			std::map<int, int> strides(uint32_t base) const
			{
				std::map<int, int> c;
				for (const Load& l : of(base))
					for (int k = -7; k <= 7; k++)
					{
						long a = (long)l.at + 2 * k;
						if (a < 0) continue;
						uint16_t j = h((size_t)a);
						if (j >> 11 != 0x04) continue;
						int imm = j & 0xff;
						if (imm < 0x40) continue;
						for (int q = 1; q <= 3; q++) if (h((size_t)a + 2 * q) >> 6 == 0x10d) { c[imm]++; break; }
					}
				return c;
			}
		};
		int get(const std::map<std::pair<int, int>, int>& m, int sz, int off) { auto it = m.find({ sz, off }); return it == m.end() ? 0 : it->second; }
		std::string hex(uint32_t v) { char b[16]; snprintf(b, sizeof b, "0x%x", v); return b; }
	}

	// ---- finding the tables ---------------------------------------------------------------------------------------
	namespace
	{
		bool codePtr(uint32_t v, size_t n, uint32_t flag, long* off = nullptr)
		{
			if (!(v & 1)) return false;
			uint32_t a = v & ~1u;
			if (flag && a >= 0x08000000u + flag) a -= flag;
			if (a < 0x08000000u || a - 0x08000000u >= n) return false;
			if (off) *off = (long)(a - 0x08000000u);
			return true;
		}

		bool findSpecies(const uint8_t* rom, size_t n, ExpansionLayout& L)
		{
			L.species = ptrAt(rom, n, 0x1bc);
			if (L.species < 0) { L.why = "no gSpeciesInfo in the GF header"; return false; }
			const size_t sp = (size_t)L.species;
			// SPECIES_NONE's name: "??????????"
			static const uint8_t Q[4] = { 0xac, 0xac, 0xac, 0xac };
			const uint8_t* q = (const uint8_t*)memmem(rom + sp, std::min<size_t>(0x80, n - sp), Q, 4);
			if (!q) { L.why = "no species name in record 0"; return false; }
			L.spName = (int)(q - (rom + sp));
			L.spCategory = L.spName - 13;
			const int cnt = std::min(L.numSpecies - 1, 400);
			int best = 0;
			for (int s = (L.spName + 16) & ~3; s <= 0x200; s += 4)
			{
				if (sp + (size_t)s * (cnt + 1) > n) break;
				int k = 0;
				for (int i = 1; i <= cnt; i++)
				{
					const size_t r = sp + (size_t)s * i;
					if (rom[r] && rom[r + 6] <= 20 && rom[r + 7] <= 20 && isName(rom, n, (long)(r + L.spName), 13)) k++;
				}
				if (k > best) { best = k; L.spStride = s; }
			}
			if (best < 60) { L.why = "no species record size"; return false; }
			const int S = L.spStride;
			auto rec = [&](int i) { return sp + (size_t)S * (size_t)i; };
			auto valid = [&](int i) { const size_t r = rec(i); return rom[r] && isName(rom, n, (long)(r + L.spName), 13); };
			std::vector<int> some;
			for (int i = 1; i < L.numSpecies && some.size() < 300; i++) if (valid(i)) some.push_back(i);
			auto most = [&](const std::function<bool(size_t)>& ok) { int k = 0; for (int i : some) k += ok(rec(i)); return k * 10 >= (int)some.size() * 8; };
			// the Pokédex text's pointer, the height and weight before it (then four u16 scales: the trainer's 256)
			for (int o = (L.spName + 13 + 3) & ~3; o + 4 <= S; o += 4)
				if (most([&](size_t r) { return isText(rom, n, ptrAt(rom, n, r + o), 600); })) { L.spDesc = o; break; }
			if (!L.spDesc) { L.why = "no Pokédex text pointer"; return false; }
			int bestH = -1, bestHk = -1;
			for (int h : { L.spDesc - 14, L.spDesc - 12 })
			{
				int k = 0;
				for (int i : some) k += u16At(rom, n, rec(i) + h + 8) == 256;
				if (k > bestHk) { bestHk = k; bestH = h; }
			}
			L.spHeight = bestH; L.spWeight = bestH + 2; L.spNatDex = bestH - 2;
			// the pictures: the first two pointers to pictures are the front's and the back's, the next two that aren't
			// the palettes (normal, shiny); checked on a few species
			int ref = some.empty() ? 1 : some[0];
			{
				std::vector<uint8_t> px;
				int pics = 0, pals = 0;
				for (int o = L.spDesc + 4; o + 4 <= S && pals < 2; o += 4)
				{
					long p = ptrAt(rom, n, rec(ref) + o, 32);
					if (p < 0) continue;
					if (pics < 2)
					{
						if (decompressPic(rom, n, (size_t)p, px, 0x4000) && px.size() >= 0x800) { if (pics++ == 0) L.spFront = o; continue; }
						if (pics == 0) continue;			// (the animation's frames before the pictures)
					}
					if (pics == 2) { if (pals++ == 0) L.spPal = o; else L.spShiny = o; }
				}
			}
			if (L.spFront < 0 || L.spPal < 0 || L.spShiny < 0) { L.why = "no pictures in the species records"; return false; }
			// the move lists and the evolutions, after them
			auto levelUpAt = [&](long p) {
				if (p < 0) return false;
				int last = 0, k = 0;
				for (; k < 120; k++)
				{
					size_t a = (size_t)p + 4 * k;
					if (a + 4 > n) return false;
					uint16_t mv = le16(rom + a), lv = le16(rom + a + 2);
					if (mv == 0xffff) return k > 0;
					if (mv == 0 || mv >= L.numMoves || lv > 100 || lv + 10 < last) return false;
					last = lv;
				}
				return false;
			};
			auto moveListAt = [&](long p) {
				if (p < 0) return false;
				for (int k = 0; k < 400; k++)
				{
					size_t a = (size_t)p + 2 * k;
					if (a + 2 > n) return false;
					uint16_t mv = le16(rom + a);
					if (mv == 0xffff) return true;
					if (mv == 0 || mv >= L.numMoves) return false;
				}
				return false;
			};
			for (int o = L.spShiny + 4; o + 4 <= S; o += 4)
				if (L.spLearn < 0 && most([&](size_t r) { return levelUpAt(ptrAt(rom, n, r + o)); })) L.spLearn = o;
				else if (L.spLearn >= 0 && L.spTeach < 0 && most([&](size_t r) { return moveListAt(ptrAt(rom, n, r + o)); })) L.spTeach = o;
				else if (L.spTeach >= 0 && L.spEgg < 0 && o == L.spTeach + 4 && most([&](size_t r) { return u32At(rom, n, r + o) == 0 || moveListAt(ptrAt(rom, n, r + o)); })) L.spEgg = o;
			if (L.spLearn < 0) { L.why = "no level-up lists"; return false; }
			// evolutions: entries of (method, parameter, target species), 6 bytes, or 12 with a pointer to conditions
			// (1.11 on), to the end mark 0xFFFF
			for (int o = (L.spEgg >= 0 ? L.spEgg : L.spTeach >= 0 ? L.spTeach : L.spLearn) + 4; o + 4 <= S && L.spEvo < 0; o += 4)
				for (int stride : { 12, 8, 6 })
				{
					int some2 = 0, bad = 0;
					for (int i : some)
					{
						const uint32_t w = u32At(rom, n, rec(i) + o);
						if (!w) continue;
						long p = ptrAt(rom, n, rec(i) + o, 2);
						if (p < 0) { bad++; continue; }
						int k = 0;
						for (; k < 10; k++)
						{
							size_t a = (size_t)p + (size_t)stride * k;
							if (a + 6 > n) { bad++; break; }
							uint16_t m = le16(rom + a), into = le16(rom + a + 4);
							if (m == 0xffff) break;
							if (m == 0xfffe) continue;		// (EVO_NONE: a form's offspring)
							if (m > 80 || into == 0 || into >= L.numSpecies) { bad++; break; }
							if (stride == 12) { uint32_t c = u32At(rom, n, a + 8); if (c && ptrAt(rom, n, a + 8) < 0) { bad++; break; } }
						}
						if (k > 0 && k < 10) some2++;
					}
					if (some2 >= 20 && bad * 10 < (int)some.size()) { L.spEvo = o; L.evoStride = stride; L.evoConditions = stride == 12; break; }
				}
			// the abilities: u16s from 0x18 to the Safari byte before the category (3, or more in a hack with its own)
			L.abilitySlots = std::max(1, (L.spCategory - 0x18 - 1) / 2);
			return true;
		}

		bool findMoves(const uint8_t* rom, size_t n, ExpansionLayout& L)
		{
			L.moves = ptrAt(rom, n, 0x1cc);
			if (L.moves < 0) { L.why = "no gMovesInfo in the GF header"; return false; }
			// Pound: Normal (1), physical, 40 power, 100 %, 35 PP; Karate Chop: Fighting (2), 50, 100 %, 25 PP
			auto is = [&](size_t r, int type, int power, int pp) {
				if (r + 0x10 > n) return false;
				uint16_t b = le16(rom + r + 0xa);
				return (b & 31) == type && (b >> 7) == power && (rom[r + 0xc] & 0x7f) == 100 && rom[r + 0xe] == pp;
			};
			for (int s = 0x10; s <= 0x80; s += 4)
				if (is((size_t)L.moves + s, 1, 40, 35) && is((size_t)L.moves + 2 * s, 2, 50, 25)) { L.mvStride = s; break; }
			if (!L.mvStride) { L.why = "no move record size"; return false; }
			// the priority (s32 priority:4): the first byte after the PP where Quick Attack (98) has +1 and Tackle (33) 0,
			// Extreme Speed (245) +2 and Protect (182) +4
			auto rb = [&](int m, int o) { return (int)(int8_t)(rom[(size_t)L.moves + (size_t)L.mvStride * m + o] << 4) >> 4; };
			for (int o = 0x10; o < L.mvStride && o < 0x20; o++)
				if (rb(98, o) == 1 && rb(33, o) == 0 && rb(245, o) == 2 && rb(182, o) == 4) { L.mvPriority = o; break; }
			return true;
		}

		bool findItems(const uint8_t* rom, size_t n, ExpansionLayout& L)
		{
			L.items = ptrAt(rom, n, 0x1c8);
			if (L.items < 0) { L.why = "no gItemsInfo in the GF header"; return false; }
			const size_t it = (size_t)L.items;
			// the name: inline ("????????" in record 0, 1.9) or a pointer (1.11 on) - where record 0 has one or the other
			static const uint8_t Q[4] = { 0xac, 0xac, 0xac, 0xac };
			const uint8_t* q = (const uint8_t*)memmem(rom + it, std::min<size_t>(0x30, n - it), Q, 4);
			if (q) { L.itName = (int)(q - (rom + it)); L.itNamePtr = false; }
			else
			{
				L.itNamePtr = true;
				for (int o : { 0x14, 0x10, 0x18, 0x1c, 0x0c, 0x08 }) if (long p = ptrAt(rom, n, it + o); p >= 0 && rom[p] == 0xac && rom[p + 1] == 0xac) { L.itName = o; break; }
			}
			auto nameOf = [&](int i, int s) -> std::string {
				size_t r = it + (size_t)s * i;
				long at = L.itNamePtr ? ptrAt(rom, n, r + L.itName) : (long)(r + L.itName);
				if (at < 0 || !rom[at] || !isText(rom, n, at, L.itNamePtr ? 40 : 21)) return "";
				return text(rom, n, at, L.itNamePtr ? 40 : 21);
			};
			for (int s = 0x20; s <= 0x80; s += 4)
				if (norm(nameOf(1, s)) == "pokeball" && norm(nameOf(2, s)) == "greatball") { L.itStride = s; break; }
			if (!L.itStride) { L.why = "no item record size"; return false; }
			// the pocket: a byte (or its top five bits) with one value for the balls, another for the TMs, the berries,
			// the key items and the rest, the same for items of a kind (anchors by name)
			std::map<std::string, int> byName;
			for (int i = 1; i < L.numItems; i++) { std::string k = norm(nameOf(i, L.itStride)); if (!k.empty() && !byName.count(k)) byName[k] = i; }
			auto id = [&](std::initializer_list<const char*> names) { for (const char* k : names) { auto f = byName.find(k); if (f != byName.end()) return f->second; } return 0; };
			const int ball = id({ "pokeball" }), potion = id({ "potion" }), berry = id({ "cheriberry", "oranberry" }), tm = [&] { for (auto& e : byName) if (tmNumberOf(e.first) == 1) return e.second; return 0; }(),
				key = id({ "bicycle", "machbike", "acrobike", "oldrod", "coincase", "itemfinder", "dowsingmachine", "townmap" }), held = id({ "leftovers" });
			if (!ball || !potion || !berry || !tm)
			{ L.why = "no anchor items for the pockets (" + std::to_string(ball) + " " + std::to_string(potion) + " " + std::to_string(berry) + " " + std::to_string(tm) + ")"; return false; }
			const int nameEnd = L.itNamePtr ? L.itName + 8 : L.itName + 20;
			for (int b = nameEnd; b < L.itStride && !L.itPocketByte; b++)
				for (int sh : { 0, 3 })
				{
					auto v = [&](int i) { return (rom[it + (size_t)L.itStride * i + b] >> sh) & (sh ? 31 : 255); };
					int vb = v(ball), vp = v(potion), vr = v(berry), vt = v(tm), vk = key ? v(key) : -1;
					std::set<int> d = { vb, vp, vr, vt };
					if (key) d.insert(vk);
					if (d.size() < 4 || (key && d.size() < 5) || vb > 8 || vp > 8 || vr > 8 || vt > 8 || vk > 8) continue;
					bool all = true;
					for (int i = 1; i < L.numItems && all; i++) all = v(i) <= 8;
					if (!all) continue;
					L.itPocketByte = b; L.itPocketShift = sh;
					L.pocketMap[vb] = POCKET_BALLS + 1; L.pocketMap[vt] = POCKET_TMS + 1; L.pocketMap[vr] = POCKET_BERRIES + 1;
					if (key) L.pocketMap[vk] = POCKET_KEY + 1;
					L.pocketMap[vp] = POCKET_MEDICINE + 1;
					if (held && !L.pocketMap[v(held)]) L.pocketMap[v(held)] = POCKET_ITEMS + 1;
					for (int i = 0; i < 9; i++) if (!L.pocketMap[i]) L.pocketMap[i] = POCKET_ITEMS + 1;
					break;
				}
			return true;
		}

		bool findAbilities(const uint8_t* rom, size_t n, ExpansionLayout& L, long table)
		{
			L.abilities = table;
			if (table < 0) { L.why = "no gAbilitiesInfo in the RHH header"; return false; }
			for (int s = 0x14; s <= 0x40; s += 4)
				if (norm(text(rom, n, table + s, 17)) == "stench" && norm(text(rom, n, table + 2 * s, 17)) == "drizzle") { L.abStride = s; break; }
			if (!L.abStride) { L.why = "no ability record size"; return false; }
			return true;
		}

		bool findTrainers(const uint8_t* rom, size_t n, ExpansionLayout& L)
		{
			// the layouts of gTrainers: 1.9 (aiFlags u32, party, items, class, music, pic, name[11]...), 1.11 on (aiFlags
			// u64, party, items, starting status, class, music, pic, name[11], battle type, party size, pools ...)
			struct Lay { int stride, party, cls, name, size, btype; };
			static const Lay LAYS[] = { { 0x24, 4, 0x10, 0x13, 0x20, 0x1e }, { 0x34, 8, 0x1c, 0x1f, 0x2b, 0x2a } };
			auto recOk = [&](size_t a, const Lay& y, bool& empty) {
				if (a + (size_t)y.stride > n) return false;
				const uint8_t sz = rom[a + y.size];
				const uint32_t p = le32(rom + a + y.party);
				empty = !sz && !p;
				if (empty) return true;
				if (sz < 1 || sz > 24 || ptrAt(rom, n, a + y.party, 8) < 0) return false;
				return isText(rom, n, (long)(a + y.name), 11, true);
			};
			int best = 0;
			for (const Lay& y : LAYS)
				for (size_t a = 0; a + (size_t)y.stride * 2 <= n; a += 4)
				{
					bool e;
					if (!recOk(a, y, e)) continue;
					int k = 0, filled = 0;
					while (recOk(a + (size_t)y.stride * k, y, e)) { filled += !e; k++; }
					if (filled > best && filled > 50)
					{
						best = filled; L.trainers = (long)a; L.numTrainers = k; L.trStride = y.stride; L.trParty = y.party;
						L.trClass = y.cls; L.trName = y.name; L.trSize = y.size; L.trBattleType = y.btype;
					}
					a += (size_t)y.stride * (size_t)std::max(0, k - 1);
				}
			if (L.trainers < 0) { L.checks.push_back("no trainers"); return false; }
			// (TRAINER_NONE first: the run may start a record late)
			// the party records' size: the distance from one party to the next, in records of their sizes
			std::map<int, int> votes;
			for (int i = 0; i + 1 < L.numTrainers; i++)
			{
				const size_t a = (size_t)L.trainers + (size_t)L.trStride * i, b = a + L.trStride;
				long p = ptrAt(rom, n, a + L.trParty), q = ptrAt(rom, n, b + L.trParty);
				int sz = rom[a + L.trSize];
				if (p < 0 || q <= p || !sz) continue;
				long d = q - p;
				if (d % sz == 0 && d / sz >= 0x18 && d / sz <= 0x40) votes[(int)(d / sz)]++;
			}
			int bv = 0;
			for (auto& v : votes) if (v.second > bv) { bv = v.second; L.tmonStride = v.first; }
			if (!L.tmonStride) L.tmonStride = L.trStride == 0x24 ? 0x20 : 0x24;
			// its level: the byte (after the species at 0x14) with 1-100 in nearly every party Pokémon and many values
			{
				std::vector<size_t> mons;
				for (int i = 1; i < L.numTrainers && mons.size() < 3000; i++)
				{
					const size_t a = (size_t)L.trainers + (size_t)L.trStride * i;
					long p = ptrAt(rom, n, a + L.trParty, (size_t)L.tmonStride * 6);
					for (int k = 0; p >= 0 && k < rom[a + L.trSize] && k < 6; k++) mons.push_back((size_t)p + (size_t)L.tmonStride * k);
				}
				int bestLv = -1; size_t bestK = 0;
				for (int o = 0x16; o < L.tmonStride; o++)
				{
					size_t k = 0; std::set<int> vals;
					for (size_t m : mons) { int v = rom[m + o]; if (v >= 1 && v <= 100) { k++; vals.insert(v); } }
					if (vals.size() >= 8 && k > bestK) { bestK = k; bestLv = o; }
				}
				if (bestLv >= 0 && bestK * 10 >= mons.size() * 9) L.tmonLevel = bestLv;
				L.checks.push_back("trainers: " + std::to_string(L.numTrainers) + " records of " + std::to_string(L.trStride) + " bytes, party Pokémon " +
					std::to_string(L.tmonStride) + " bytes, level at " + std::to_string(L.tmonLevel) + " (1-100 in " + std::to_string(bestK) + " of " + std::to_string(mons.size()) + ")");
			}
			// the trainer classes: 16 bytes each (name[13], money, ball), the most names in a row
			int bc = 0;
			for (size_t a = 0; a + 32 <= n; a += 4)
			{
				if (!isText(rom, n, (long)a, 13)) continue;
				int k = 0;
				while (a + 16 * (size_t)k + 16 <= n && isText(rom, n, (long)(a + 16 * (size_t)k), 13) && le16(rom + a + 16 * k + 14) < L.numItems) k++;
				if (k > bc) { bc = k; L.classes = (long)a; L.numClasses = k; }
				if (k > 1) a += 16 * (size_t)(k - 1);
			}
			if (bc < 20) L.classes = -1;
			return true;
		}

		bool findWild(const uint8_t* rom, size_t n, ExpansionLayout& L)
		{
			// gWildMonHeaders: group, number, then times x kinds pointers to {rate, list} (0xFF 0xFF at the end)
			auto infoOk = [&](size_t a) {
				uint32_t w = le32(rom + a);
				if (!w) return true;
				long p = ptrAt(rom, n, a, 8);
				return p >= 0 && rom[p] <= 100 && ptrAt(rom, n, (size_t)p + 4, 4) >= 0;
			};
			int best = 0;
			for (int kinds : { 4, 5 })
				for (int times : { 1, 4, 2, 3 })
				{
					const int stride = 4 + 4 * kinds * times;
					for (size_t a = 0; a + (size_t)stride * 20 <= n; a += 4)
					{
						auto rec = [&](size_t r) {
							if (r + stride > n || rom[r] > 60 || rom[r + 2] || rom[r + 3]) return false;
							for (int k = 0; k < kinds * times; k++) if (!infoOk(r + 4 + 4 * k)) return false;
							return true;
						};
						auto used = [&](size_t r) { for (int k = 0; k < kinds * times; k++) if (le32(rom + r + 4 + 4 * k)) return true; return false; };
						if (!rec(a) || !used(a)) continue;
						int k = 0, filled = 0;
						while (rec(a + (size_t)stride * k)) filled += used(a + (size_t)stride * k++);
						if (k > best && filled * 2 >= k && a + (size_t)stride * k + 2 <= n && rom[a + (size_t)stride * k] == 0xff)
						{ best = k; L.wild = (long)a; L.wildStride = stride; L.wildTimes = times; L.wildKinds = kinds; }
						a += (size_t)stride * (size_t)std::max(0, k - 1);
					}
				}
			if (best < 10) { L.wild = -1; L.checks.push_back("no wild encounter table"); return false; }
			return true;
		}
	}

	namespace
	{
		// gRegionMapEntries: {x, y, width, height, name} (8 bytes) a region map section; the longest run of them with
		// names (sections with no place on the map have a null name). Sections past 255 (SoulGold): the map headers
		// keep a u16 section there
		void findMapSections(const uint8_t* rom, size_t n, ExpansionLayout& L)
		{
			auto rec = [&](size_t r) {
				if (r + 8 > n || rom[r] > 60 || rom[r + 1] > 40 || rom[r + 2] > 40 || rom[r + 3] > 40) return 0;
				uint32_t w = le32(rom + r + 4);
				if (!w) return 1;
				long p = ptrAt(rom, n, r + 4);
				if (p >= 0 && rom[p] == 0xff) return 1;		// (an empty name: a section of another region)
				if (!isName(rom, n, p, 26)) return 0;
				for (long q = p; rom[q] != 0xff; q++) if (rom[q] >= 0xf7) return 1;	// (control codes: a name of another kind, not counted)
				return 2;
			};
			// the code points at the table's start (the run may start with sections that have no name)
			auto refs = [&](size_t b) {
				const uint32_t v = (uint32_t)b + 0x08000000u;
				const uint8_t p[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
				int k = 0;
				const size_t end = std::min<size_t>(n, 16u << 20);
				for (const uint8_t* q = rom; k < 3 && (q = (const uint8_t*)memmem(q, end - (q - rom), p, 4)); q++) if ((q - rom) % 4 == 0) k++;
				return k;
			};
			int best = 0;
			for (size_t a = 0; a + 8 * 40 <= n; a += 4)
			{
				if (rec(a) != 2) continue;
				int k = 0, named = 0, r;
				while ((r = rec(a + 8 * (size_t)k))) { named += r == 2; k++; }
				if (named >= 30 && named > best)
				{
					size_t b = a;
					for (size_t c = a; c >= 8 && a - c < 8 * 256 && rec(c - 8); c -= 8) if (refs(c - 8) >= 1) b = c - 8;
					if (refs(b) >= 1) { best = named; L.mapsecs = (long)b; L.numMapsecs = k + (int)((a - b) / 8); }
				}
				a += 8 * (size_t)std::max(0, k - 1);
			}
			L.checks.push_back("region map sections: " + std::to_string(L.numMapsecs) + " at " + hex((uint32_t)L.mapsecs + 0x08000000u) + " (" + std::to_string(best) + " named)");
		}
	}

	// ---- RAM, from the code ------------------------------------------------------------------------------------------
	namespace
	{
		void findRam(const uint8_t* rom, size_t n, ExpansionLayout& L)
		{
			Code c(rom, n);
			auto note = [&](const std::string& s) { L.checks.push_back(s); };
			// gSpecials: the longest table of code pointers (tagged ones too) whose first function steps through the
			// party (HealPlayerParty): its count (a byte read first) and the party (stepped by the size of a Pokémon)
			const uint32_t flag = (uint32_t)n >= (16u << 20) ? 0x02000000u : 0x01000000u;
			L.romSizeFlag = flag;
			for (size_t a = 0; a + 4 * 300 <= std::min<size_t>(n, 16u << 20) && !L.party; a += 4)
			{
				long f;
				if (!codePtr(le32(rom + a), n, flag, &f)) continue;
				int k = 0;
				while (a + 4 * (size_t)k + 4 <= n && codePtr(le32(rom + a + 4 * k), n, flag)) k++;
				if (k < 250) { a += 4 * (size_t)k; continue; }
				// the first function: "ldr rC, =count; ldrb [rC]" and "ldr rP, =party ... adds rP, #size"
				uint32_t count = 0, party = 0; int size = 0;
				for (size_t i = (size_t)f; i < (size_t)f + 0x40; i += 2)
				{
					uint32_t v = c.literal(i);
					uint16_t j = c.h(i);
					if (v >> 24 == 2 && !count)
					{
						uint16_t x = c.h(i + 2);
						if (x >> 11 == 0x0f && ((x >> 3) & 7) == ((j >> 8) & 7)) { count = v; continue; }
					}
					if (v >> 24 == 2 && count && !party)
					{
						const int r = (j >> 8) & 7;
						for (size_t q = i + 2; q < i + 0x20; q += 2)
						{
							uint16_t x = c.h(q);
							if (x >> 11 == 0x06 && ((x >> 8) & 7) == r && (x & 0xff) >= 0x50 && (x & 0xff) <= 0x80) { party = v; size = x & 0xff; break; }
						}
					}
				}
				if (count && party) { L.partyCount = count; L.party = party; L.monSize = size; note("party: gSpecials at " + hex((uint32_t)a + 0x08000000u) + "[0] reads count " + hex(count) + ", steps party " + hex(party) + " by " + std::to_string(size)); }
				a += 4 * (size_t)k;
			}
			if (L.party)
			{
				// check: other code reads both (the count and the party in one pool)
				int both = 0;
				for (const Load& l : c.of(L.party))
					for (int k = -8; k <= 8; k++) if (k && (long)l.at + 2 * k >= 0 && c.literal(l.at + 2 * k) == L.partyCount) { both++; break; }
				note("party: " + std::to_string(both) + " other loads of the party with the count beside it; party read " + std::to_string(c.count(L.party)) + " times");
				L.enemyParty = L.party + 6 * (uint32_t)L.monSize;
				note("enemy party = party + 6 x " + std::to_string(L.monSize) + " = " + hex(L.enemyParty) + ", read " + std::to_string(c.count(L.enemyParty)) + " times");
				if (c.count(L.enemyParty) < 5) { note("enemy party: too few reads, not trusted"); L.enemyParty = 0; }
			}
			// gBattleTypeFlags: the EWRAM u32 most often bit-tested
			{
				uint32_t best = 0, second = 0; int bk = 0, sk = 0;
				for (auto& e : c.loads)
				{
					if (e.first >> 24 != 2) continue;
					int k = c.bitTests(e.first);
					if (k > bk) { second = best; sk = bk; best = e.first; bk = k; } else if (k > sk) { second = e.first; sk = k; }
				}
				if (bk >= 40 && bk >= sk * 3 / 2) L.battleFlags = best;
				note("battle type flags: " + hex(best) + " bit-tested " + std::to_string(bk) + " times (next " + hex(second) + " " + std::to_string(sk) + ")" +
					(L.battleFlags ? "" : ": not clear enough"));
			}
			// gBattleMons: the base whose HP (u16 0x2A), max HP (0x2E), item (0x30) and ability (0x20) are read most; its
			// size the multiplier beside its loads
			{
				uint32_t best = 0; int bk = 0;
				for (auto& e : c.loads)
				{
					if (e.first >> 24 != 2 || e.second.size() < 50) continue;
					auto f = c.fields(e.first);
					int k = std::min({ get(f, 2, 0x2a), get(f, 2, 0x2e), get(f, 2, 0x30), get(f, 2, 0x20) });
					if (k > bk) { bk = k; best = e.first; }
				}
				auto s = c.strides(best);
				int bs = 0, bsk = 0;
				for (auto& v : s) if (v.first >= 0x58 && v.first <= 0xa0 && v.second > bsk) { bsk = v.second; bs = v.first; }
				if (bk >= 10 && bs) { L.battleMons = best; L.battlerSize = bs; }
				note("battle mons: " + hex(best) + " (HP/max HP/item/ability each read >= " + std::to_string(bk) + " times), size " + hex((uint32_t)bs) +
					" (multiplied by it " + std::to_string(bsk) + " times)");
			}
			// gMain: the IWRAM base read with the offset 0x439 (inBattle's byte) most; checked: its keys (u16 0x2C/0x2E) read
			{
				uint32_t best = 0; int bk = 0;
				for (auto& e : c.loads)
				{
					if (e.first >> 24 != 3 || e.second.size() < 30) continue;
					auto k = c.nearConsts(e.first, 0x439, 0x43a);
					int v = k.empty() ? 0 : k.begin()->second;
					if (v > bk) { bk = v; best = e.first; }
				}
				auto f = c.fields(best);
				if (bk >= 10) L.gMain = best;
				note("gMain: " + hex(best) + " with 0x439 beside " + std::to_string(bk) + " loads; keys read " + std::to_string(get(f, 2, 0x2c) + get(f, 2, 0x2e)) + " times");
			}
			// gSaveBlock1Ptr: the pointer read with the money/PC item offsets beside it (pcItems - 8, - 4, - 2); gSaveBlock2Ptr
			// the one 4 bytes from it, read with the player's gender / trainer id (bytes 8, 0x0A)
			{
				uint32_t best = 0; int bk = 0;
				const uint32_t pc = L.pcItemsOffset;
				for (auto& e : c.loads)
				{
					if (e.first >> 24 != 3 || e.second.size() < 100) continue;
					auto k = c.nearConsts(e.first, pc - 8, pc + 1);
					int v = 0;
					for (auto& x : k) v += x.second;
					if (v > bk) { bk = v; best = e.first; }
				}
				uint32_t other = 0; int ok = -1;
				for (uint32_t o : { best + 4, best - 4 })
				{
					auto f = c.fields(o, true);
					int v = get(f, 1, 8) + get(f, 2, 0xa) + get(f, 1, 9);
					if ((int)c.of(o).size() > 100 && v > ok) { ok = v; other = o; }
				}
				if (bk >= 3 && other) { L.sb1 = best; L.sb2 = other; }
				// the player's place in SaveBlock1: its map's group and number (bytes) the most read pair at 4 or 8, the
				// position (two s16) before it
				auto f1 = c.fields(best, true);
				int p4 = std::min(get(f1, 1, 4), get(f1, 1, 5)), p8 = std::min(get(f1, 1, 8), get(f1, 1, 9));
				L.sb1Pos = p8 > p4 ? 4 : 0;
				note("save blocks: SB1 " + hex(best) + " (with the money/PC offsets beside " + std::to_string(bk) + " loads), SB2 " + hex(other) +
					" (gender/id read " + std::to_string(ok) + " times); the place at SB1+" + std::to_string(L.sb1Pos) + " (map bytes read " +
					std::to_string(p4) + " at 4, " + std::to_string(p8) + " at 8)");
			}
			// gMapHeader: the EWRAM base whose region map section (byte 0x14) and layout (word 0) are read most
			{
				uint32_t best = 0; int bk = 0;
				for (auto& e : c.loads)
				{
					if (e.first >> 24 != 2 || e.second.size() < 30) continue;
					auto f = c.fields(e.first);
					int k = std::min(get(f, 1, 0x14), get(f, 4, 0) / 2) + std::min(get(f, 1, 0x17), 20);
					if (get(f, 1, 0x14) >= 8 && get(f, 4, 4) + get(f, 4, 0xc) > 0 && k > bk) { bk = k; best = e.first; }
				}
				if (bk >= 8) L.mapHeader = best;
				note("map header: " + hex(best) + " (score " + std::to_string(bk) + ")");
			}
		}

		// FLAG_BADGE01_GET: a u16 table of eight flags in a row the code points at, or the level caps' (flag, level)
		// pairs; else the game clear flag + 3 (expansion's flags.h)
		void findBadges(const uint8_t* rom, size_t n, ExpansionLayout& L)
		{
			std::map<int, int> votes;
			for (size_t a = 0; a + 64 <= n; a += 2)
			{
				uint16_t f = le16(rom + a);
				if ((uint32_t)f + 0x40 < L.gameClearFlag || (uint32_t)f > L.gameClearFlag + 0x40) continue;	// (the system flags, near game clear)
				bool run = true;
				for (int i = 1; i < 8 && run; i++) run = le16(rom + a + 2 * i) == f + i;
				if (run && a % 4 == 0 && (a < 2 || le16(rom + a - 2) != f - 1))
				{
					// referenced by the code
					uint8_t p[4] = { (uint8_t)a, (uint8_t)(a >> 8), (uint8_t)(a >> 16), (uint8_t)((a >> 24) + 8) };
					int refs = 0;
					for (const uint8_t* q = rom; (q = (const uint8_t*)memmem(q, n - (q - rom), p, 4)) && refs < 3; q++) if ((q - rom) % 4 == 0) refs++;
					if (refs >= 2) votes[f] += 2;
				}
				if (a % 4 == 0 && f && le16(rom + a + 2) == 0)
				{
					bool caps = true;
					for (int i = 0; i < 8 && caps; i++) caps = le32(rom + a + 8 * i) == (uint32_t)f + i && le32(rom + a + 8 * i + 4) >= 1 && le32(rom + a + 8 * i + 4) <= 100;
					if (caps) votes[f] += 2;
				}
			}
			int best = 0;
			for (auto& v : votes) if (v.second > best) { best = v.second; L.badgeFlag = v.first; }
			const int fromClear = (int)L.gameClearFlag + 3;
			if (!L.badgeFlag) L.badgeFlag = fromClear;
			L.checks.push_back("badges: FLAG_BADGE01_GET " + hex((uint32_t)L.badgeFlag) + (best ? " (a table of the eight in the ROM)" : " (game clear + 3)") +
				(L.badgeFlag == fromClear ? ", = game clear + 3" : ", not game clear + 3 (" + hex((uint32_t)fromClear) + ")"));
		}
	}

	bool findExpansion(const uint8_t* rom, size_t n, ExpansionLayout& L)
	{
		L = ExpansionLayout();
		long rhh = -1;
		for (size_t a = 0x100; a + 0x18 <= n && a < 0x400; a += 2) if (!memcmp(rom + a, "RHHEXP", 6)) { rhh = (long)a; break; }
		if (rhh < 0 || n < 0x400) { L.why = "no RHH header"; return false; }
		const uint8_t* h = rom + rhh;
		L.major = h[6]; L.minor = h[7]; L.patch = h[8];
		L.numMoves = le16(h + 0xa); L.numSpecies = le16(h + 0xc); L.numAbilities = le16(h + 0xe);
		const long abil = ptrAt(rom, n, (size_t)rhh + 0x10);
		L.numItems = le16(h + 0x14);
		// the GF header's save offsets
		L.flagsOffset = le32(rom + 0x150); L.varsOffset = le32(rom + 0x154); L.dexSeenOffset = le32(rom + 0x15c); L.dexCount = le32(rom + 0x170);
		L.sb2Size = le32(rom + 0x188); L.sb1Size = le32(rom + 0x18c); L.gameClearFlag = le32(rom + 0x1dc);
		for (int i = 0; i < 5; i++) L.bagCount[i] = rom[0x1e4 + i];
		L.pcItemsCount = rom[0x1e9]; L.pcItemsOffset = le32(rom + 0x1ec);
		if (!findSpecies(rom, n, L) || !findMoves(rom, n, L) || !findItems(rom, n, L) || !findAbilities(rom, n, L, abil)) return false;
		auto t0 = std::chrono::steady_clock::now();
		auto lap = [&](const char* what) {
			auto t1 = std::chrono::steady_clock::now();
			L.times += std::string(L.times.empty() ? "" : ", ") + what + " " + std::to_string((int)std::chrono::duration<double, std::milli>(t1 - t0).count()) + " ms";
			t0 = t1;
		};
		lap("tables");
		findTrainers(rom, n, L); lap("trainers");
		findWild(rom, n, L); lap("wild");
		findMapSections(rom, n, L); lap("sections");
		findBadges(rom, n, L); lap("badges");
		findRam(rom, n, L); lap("RAM");
		return true;
	}

	// ---- the layouts cached by ROM (the companion's thread finds them, the pages' reader uses them) ----------------
	namespace
	{
		std::mutex cacheMu;
		// (by the buffer and a hash of its headers: another ROM read into the same buffer is another key)
		std::map<std::tuple<const uint8_t*, size_t, uint32_t>, std::shared_ptr<const ExpansionLayout>> cache;
		std::shared_ptr<const ExpansionLayout> layoutOf(const uint8_t* rom, size_t n)
		{
			uint32_t hh = 2166136261u;
			for (size_t i = 0; i < std::min<size_t>(n, 0x400); i++) { hh ^= rom[i]; hh *= 16777619u; }
			for (size_t i = 0x400; i + 4 <= n; i += 0x40000) { hh ^= le32(rom + i); hh *= 16777619u; }
			const auto key = std::make_tuple(rom, n, hh);
			{
				std::lock_guard<std::mutex> l(cacheMu);
				auto it = cache.find(key);
				if (it != cache.end()) return it->second;
			}
			auto L = std::make_shared<ExpansionLayout>();
			if (!findExpansion(rom, n, *L)) return L->major ? std::shared_ptr<const ExpansionLayout>(L) : nullptr;
			std::lock_guard<std::mutex> l(cacheMu);
			if (cache.size() > 4) cache.clear();
			cache[key] = L;
			return L;
		}
	}

	// ---- the Pokémon in RAM ----------------------------------------------------------------------------------------
	namespace
	{
		// expansion's BoxPokemon: personality, OT id, nickname[10], language/hidden nature, flags (bad egg, has species,
		// egg), OT name[7], markings, checksum, HP lost; then four 12-byte parts, encrypted (XOR personality ^ OT id)
		// and in one of 24 orders: species:11 (part 0), held item:10, experience:21, PP bonuses, friendship; moves:11 x4,
		// PP:7 x4 (part 1); EVs (part 2); IVs 5 x6, egg bit 30 (part 3, word 1), ability number bits 29-30 (word 2).
		// The party's: status 0x50, level 0x54, HP 0x56, max HP 0x58, the five stats from 0x5A
		bool decodeExpansion(const uint8_t* p, pkmn::Mon& m)
		{
			m = pkmn::Mon();
			const uint32_t pid = le32(p), otid = le32(p + 4), key = pid ^ otid;
			uint8_t d[48];
			uint16_t sum = 0;
			for (int i = 0; i < 48; i += 4)
			{
				uint32_t v = le32(p + 0x20 + i) ^ key;
				d[i] = (uint8_t)v; d[i + 1] = (uint8_t)(v >> 8); d[i + 2] = (uint8_t)(v >> 16); d[i + 3] = (uint8_t)(v >> 24);
				sum = (uint16_t)(sum + le16(d + i) + le16(d + i + 2));
			}
			if (sum != le16(p + 0x1c)) return false;
			int order[4] = { 0, 1, 2, 3 };
			for (uint32_t k = pid % 24; k > 0; k--) std::next_permutation(order, order + 4);
			const uint8_t* part[4];
			for (int pos = 0; pos < 4; pos++) part[order[pos]] = d + pos * 12;
			m.species = le16(part[0]) & 0x7ff;
			if (!m.species) return false;
			m.item = le16(part[0] + 2) & 0x3ff;
			m.exp = le32(part[0] + 4) & 0x1fffff;
			m.ppBonuses = part[0][8];
			m.friendship = part[0][9];
			for (int i = 0; i < 4; i++) { m.moves[i] = le16(part[1] + 2 * i) & 0x7ff; m.pp[i] = part[1][8 + i] & 0x7f; }
			for (int i = 0; i < 6; i++) m.evs[i] = part[2][i];
			const uint32_t iv = le32(part[3] + 4);
			for (int i = 0; i < 6; i++) m.ivs[i] = (uint8_t)(iv >> (5 * i) & 31);
			m.abilitySlot = (uint8_t)(le32(part[3] + 8) >> 29 & 3);
			m.egg = (p[0x13] & 4) || (iv >> 30 & 1);
			m.pid = pid; m.otId = otid;
			m.nick = pkmn::decodeText(p + 8, 10);
			m.status = le32(p + 0x50);
			m.level = p[0x54];
			m.hp = le16(p + 0x56);
			m.maxHp = le16(p + 0x58);
			for (int i = 0; i < 5; i++) m.stats[i] = le16(p + 0x5a + 2 * i);
			return true;
		}
		// SoulGold's own (found in its GetBoxMonData, expansion.cpp's notes): 76 bytes, not encrypted, no checksum read
		// here: nickname[12] at 8, species:11 at 0x20, held item:10 at 0x22, experience:24 at 0x24, PP bonuses 0x2A,
		// friendship 0x2B, moves:11 at 0x2C.., PP:7 at 0x34.., EVs 0x38.., ability number bits 4-5 of 0x33, IVs at 0x48
		// (egg bit 30); the party's status 0x4C, level 0x50, HP 0x52, max HP 0x54, stats from 0x56
		bool decodePlain96(const uint8_t* p, pkmn::Mon& m)
		{
			m = pkmn::Mon();
			m.species = le16(p + 0x20) & 0x7ff;
			if (!m.species || (p[0x15] & 1)) return false;
			m.level = p[0x50];
			m.hp = le16(p + 0x52);
			m.maxHp = le16(p + 0x54);
			if (m.level < 1 || m.level > 100 || m.hp > m.maxHp) return false;
			m.pid = le32(p); m.otId = le32(p + 4);
			m.item = le16(p + 0x22) & 0x3ff;
			m.exp = le32(p + 0x24) & 0xffffff;
			m.ppBonuses = p[0x2a];
			m.friendship = p[0x2b];
			for (int i = 0; i < 4; i++) { m.moves[i] = le16(p + 0x2c + 2 * i) & 0x7ff; m.pp[i] = p[0x34 + i] & 0x7f; }
			for (int i = 0; i < 6; i++) m.evs[i] = p[0x38 + i];
			const uint32_t iv = le32(p + 0x48);
			for (int i = 0; i < 6; i++) m.ivs[i] = (uint8_t)(iv >> (5 * i) & 31);
			m.egg = iv >> 30 & 1;
			m.abilitySlot = (uint8_t)(p[0x33] >> 4 & 3);
			m.nick = pkmn::decodeText(p + 8, 12);
			m.status = le32(p + 0x4c);
			for (int i = 0; i < 5; i++) m.stats[i] = le16(p + 0x56 + 2 * i);
			return true;
		}
		// BattlePokemon (1.9 and 1.15 alike up to status1): species, stats, moves 0x0C, IVs, stages, ability 0x20, types
		// 0x22 (three), PP 0x25, HP 0x2A, level 0x2C, friendship, max HP 0x2E, item 0x30, nickname 0x32, PP bonuses 0x3F,
		// OT name, experience 0x48, personality 0x4C, status1 0x50 (or all of these 2 bytes on: below)
		uint32_t expAt(int growth, int n);
		// an experience a Pokémon of this level can have under some growth rate: at least the least any rate needs for the
		// level, below the most any rate needs for the next (level 100: at most the most for 100)
		bool expFitsLevel(uint32_t e, int level)
		{
			if (level < 1 || level > 100) return false;
			uint32_t lo = UINT32_MAX, hi = 0;
			for (int g = 0; g < 6; g++)
			{
				lo = std::min(lo, level <= 1 ? 0u : expAt(g, level));
				hi = std::max(hi, expAt(g, std::min(level + 1, 100)));
			}
			return e >= lo && (level == 100 ? e <= hi : e < hi);
		}

		bool decodeBattlerExp(const uint8_t* p, pkmn::Mon& m, int maxSpecies)
		{
			m = pkmn::Mon();
			m.species = le16(p);
			if (!m.species || m.species > maxSpecies) return false;
			for (int i = 0; i < 4; i++) { m.moves[i] = le16(p + 0x0c + 2 * i); m.pp[i] = p[0x25 + i]; }
			m.types[0] = pkmnTypeOf(p[0x22]); m.types[1] = pkmnTypeOf(p[0x23]);
			if (m.types[1] == 255) m.types[1] = m.types[0];
			m.hp = le16(p + 0x2a);
			m.level = p[0x2c];
			m.friendship = p[0x2d];
			m.maxHp = le16(p + 0x2e);
			m.item = le16(p + 0x30);
			// the fields after the item: where 1.9 and 1.15 keep them, or 2 bytes on (SoulGold 1.1.4, a 1.15.2 fork with a
			// 0x98-byte BattlePokemon: a u16 after the item; its personality was read from the experience - the gender
			// flipped - and the status from the personality - a burn that wasn't there - on the RG DS Plus 2026-11-04).
			// The one where the experience (at most 1,640,000 in Gen III's curves) and status1 (bits up to 0xffff) are
			// possible; the usual place first
			// SoulGold 1.1.4 as read from its RAM in Crystal's first battle (RG DS Plus, 2026-11-04): the nickname 2 bytes on
			// (0x34) but the OT name, experience, personality and status 4 bytes on (0x42, 0x4c, 0x50, 0x54) - with only 0 or
			// 2 tried, neither fit and 0 was taken: "ë Chespin" (the u16 before the name) and Sleep on both sides. The
			// experience must also be one the level can have (between the six growth rates' extremes), not just small.
			int s = 0, sn = 0;
			auto possible = [&](int o)
			{
				const uint32_t e = le32(p + 0x48 + o);
				return e <= 2000000 && le32(p + 0x50 + o) <= 0xffff && expFitsLevel(e, m.level);
			};
			for (int o : { 0, 2, 4 }) if (possible(o)) { s = o; sn = std::min(o, 2); break; }
			m.nick = pkmn::decodeText(p + 0x32 + sn, 12);
			m.ppBonuses = p[0x3f + sn];	// (SoulGold: 0x41, before the OT name at 0x42 - a byte of the OT name gave Tackle 42 PP)
			m.exp = le32(p + 0x48 + s);
			m.pid = le32(p + 0x4c + s);
			m.status = le32(p + 0x50 + s);
			for (int i = 0; i < 5; i++) m.stats[i] = le16(p + 2 + 2 * i);
			// the IVs: 5 bits each at 0x14, between the moves and the stat stages (as vanilla's BattlePokemon); they were
			// left 0 - a wild Rattata showed no IVs and Hidden Power Fighting 30 on the RG DS Plus 2026-11-04
			const uint32_t iv = le32(p + 0x14);
			for (int i = 0; i < 6; i++) m.ivs[i] = (uint8_t)(iv >> (5 * i) & 31);
			return m.level >= 1 && m.level <= 100;
		}

		// the experience a level needs, by growth rate (expansion's: medium fast, erratic, fluctuating, medium slow,
		// fast, slow - Gen III's order)
		uint32_t expAt(int growth, int n)
		{
			const long long c = (long long)n * n * n;
			switch (growth)
			{
			case 0: return (uint32_t)c;
			case 1:
				if (n <= 50) return (uint32_t)(c * (100 - n) / 50);
				if (n <= 68) return (uint32_t)(c * (150 - n) / 100);
				if (n <= 98) return (uint32_t)(c * ((1911 - 10 * n) / 3) / 500);
				return (uint32_t)(c * (160 - n) / 100);
			case 2:
				if (n <= 15) return (uint32_t)(c * ((n + 1) / 3 + 24) / 50);
				if (n <= 36) return (uint32_t)(c * (n + 14) / 50);
				return (uint32_t)(c * (n / 2 + 32) / 50);
			case 3: { long long v = 6 * c / 5 - 15LL * n * n + 100LL * n - 140; return n <= 1 ? 0 : (uint32_t)std::max(0LL, v); }
			case 4: return (uint32_t)(4 * c / 5);
			case 5: return (uint32_t)(5 * c / 4);
			}
			return (uint32_t)c;
		}
	}

	// ---- the tables both sides use ------------------------------------------------------------------------------------
	namespace
	{
		struct Reader
		{
			const uint8_t* rom; size_t n; const ExpansionLayout& L;
			size_t sp(int s) const { return (size_t)L.species + (size_t)L.spStride * (size_t)s; }
			bool speciesValid(int s) const { return s > 0 && s < L.numSpecies && sp(s) + L.spStride <= n && rom[sp(s)] && isName(rom, n, (long)(sp(s) + L.spName), 13); }
			std::string speciesName(int s) const { return niceName(text(rom, n, (long)(sp(s) + L.spName), 13)); }
			size_t mv(int m) const { return (size_t)L.moves + (size_t)L.mvStride * (size_t)m; }
			std::string moveName(int m) const { return niceName(text(rom, n, ptrAt(rom, n, mv(m)), 40)); }
			size_t it(int i) const { return (size_t)L.items + (size_t)L.itStride * (size_t)i; }
			std::string itemName(int i) const
			{
				return niceName(L.itNamePtr ? text(rom, n, ptrAt(rom, n, it(i) + L.itName), 30) : text(rom, n, (long)(it(i) + L.itName), 20));
			}
			int itemPocket(int i) const	// nds::Pocket (POCKET_ITEMS when not known)
			{
				int v = (rom[it(i) + L.itPocketByte] >> L.itPocketShift) & (L.itPocketShift ? 31 : 255);
				return v < 32 && L.pocketMap[v] ? L.pocketMap[v] - 1 : POCKET_ITEMS;
			}
			size_t ab(int a) const { return (size_t)L.abilities + (size_t)L.abStride * (size_t)a; }
			std::string abilityName(int a) const { return a > 0 && a < L.numAbilities ? niceName(text(rom, n, (long)ab(a), 17)) : ""; }
		};

		// Gen IV's item ids by name (the player's own GBA ROMs' names matched with the DS games' - only the numbers and a
		// hash of the name kept, made by tests/gba-items-gen); the DS games' TMs and HMs by number
		const std::pair<uint32_t, uint16_t> GEN4_ITEMS[] = {
#include "gba/expitems.inc"
		};
		uint16_t gen4ByName(const std::string& name)
		{
			const std::string k = norm(name);
			if (const int tm = tmNumberOf(k)) return (uint16_t)(327 + tm);	// (TM01 328 .. TM92 419, HM01 420 .. HM08 427)
			static const std::unordered_map<uint32_t, uint16_t> M = [] { std::unordered_map<uint32_t, uint16_t> m; for (auto& e : GEN4_ITEMS) m.emplace(e.first, e.second); return m; }();
			auto f = M.find(fnv(k));
			return f == M.end() ? 0 : f->second;
		}

		std::vector<uint16_t> itemMapOf(const Reader& r)
		{
			std::vector<uint16_t> map((size_t)r.L.numItems, 0);
			std::vector<bool> used(1000, false);
			for (int i = 1; i < r.L.numItems; i++)
			{
				uint16_t g = gen4ByName(r.itemName(i));
				if (g && g < 1000 && !used[g]) { used[g] = true; map[i] = g; }
				else map[i] = (uint16_t)(GEN3_ONLY + i);
			}
			return map;
		}

		// RomData index (national number, or a form's place after the Pokédex) by species id, and back
		struct Dex { std::vector<uint16_t> index; std::vector<uint16_t> species; int dexSize = 0; };
		Dex dexOf(const Reader& r)
		{
			Dex d;
			const ExpansionLayout& L = r.L;
			d.dexSize = (int)std::min<uint32_t>(L.dexCount ? L.dexCount : 1025, 2000);
			d.index.assign((size_t)L.numSpecies, 0);
			d.species.assign((size_t)d.dexSize + 1, 0);
			std::vector<int> forms;
			for (int s = 1; s < L.numSpecies; s++)
			{
				if (!r.speciesValid(s)) continue;
				const int nat = le16(r.rom + r.sp(s) + L.spNatDex);
				if (nat >= 1 && nat <= d.dexSize && !d.species[nat]) { d.species[nat] = (uint16_t)s; d.index[s] = (uint16_t)nat; }
				else forms.push_back(s);
			}
			for (int s : forms)
			{
				d.index[s] = (uint16_t)d.species.size();
				d.species.push_back((uint16_t)s);
			}
			return d;
		}
	}

	// ---- the companion's profile and tables --------------------------------------------------------------------------
	namespace
	{
		struct OwnProfile : pkmn::GameProfile { std::string title; };
	}

	bool expansionProfile(const uint8_t* rom, size_t n, std::shared_ptr<const pkmn::GameProfile>& profile, pkmn::RomTables& t)
	{
		auto Lp = layoutOf(rom, n);
		if (!Lp || !Lp->why.empty()) return false;
		const ExpansionLayout& L = *Lp;
		for (auto& c : L.checks) fprintf(stderr, "pokemon: expansion %d.%d.%d: %s\n", L.major, L.minor, L.patch, c.c_str());
		if (!L.ramOk()) { fprintf(stderr, "pokemon: expansion: RAM not all found, nothing read\n"); return false; }
		Reader r{ rom, n, L };
		// the tables pkmn's views and reads use (map names, region maps: today's finder)
		t = pkmn::findTables(rom, n, nullptr);
		if (L.mapsecs >= 0)
		{
			t.mapNames.assign((size_t)L.numMapsecs, "");
			for (int i = 0; i < L.numMapsecs; i++) t.mapNames[(size_t)i] = text(rom, n, ptrAt(rom, n, (size_t)L.mapsecs + 8 * (size_t)i + 4), 30);
		}
		t.numSpecies = L.numSpecies;
		t.has.modernTypes = true;
		t.has.ownDex = true;
		t.has.leadersFromTrainers = false;
		t.has.plainMons = false;
		const Dex dex = dexOf(r);
		t.species.assign((size_t)L.numSpecies, "");
		t.speciesTypes.assign((size_t)L.numSpecies, { 255, 255 });
		t.genderRatio.assign((size_t)L.numSpecies, 0xff);
		t.growth.assign((size_t)L.numSpecies, 0);
		t.speciesAbilities.assign((size_t)L.numSpecies, { 0, 0 });
		t.natDex.assign((size_t)L.numSpecies, 0);
		for (int s = 1; s < L.numSpecies; s++)
		{
			if (!r.speciesValid(s)) continue;
			const uint8_t* b = rom + r.sp(s);
			t.species[s] = r.speciesName(s);
			t.speciesTypes[s] = { pkmnTypeOf(b[6]), pkmnTypeOf(b[7]) };
			if (t.speciesTypes[s][1] == 255) t.speciesTypes[s][1] = t.speciesTypes[s][0];
			t.genderRatio[s] = b[0x12];
			t.growth[s] = b[0x15];
			t.speciesAbilities[s] = { (uint8_t)std::min<uint16_t>(255, le16(b + 0x18)), (uint8_t)std::min<uint16_t>(255, le16(b + 0x1a)) };
			t.natDex[s] = dex.index[s];
		}
		t.dexSpecies = dex.species;
		t.expTable.assign(6, {});
		for (int g = 0; g < 6; g++) for (int lv = 1; lv <= 100; lv++) t.expTable[g][lv] = expAt(g, lv);
		t.moves.assign((size_t)L.numMoves, "");
		t.moveInfo.assign((size_t)L.numMoves, {});
		for (int m = 1; m < L.numMoves; m++)
		{
			const size_t a = r.mv(m);
			if (a + 0x10 > n) break;
			t.moves[m] = r.moveName(m);
			const uint16_t bits = le16(rom + a + 0xa);
			pkmn::MoveInfo& mi = t.moveInfo[m];
			mi.type = pkmnTypeOf(bits & 31); if (mi.type == 255) mi.type = 0;
			mi.power = (uint8_t)std::min(255, bits >> 7);
			if ((bits >> 5 & 3) == 2) mi.power = 0;
			mi.accuracy = rom[a + 0xc] & 0x7f;
			mi.pp = rom[a + 0xe];
		}
		t.abilities.assign((size_t)L.numAbilities, "");
		for (int a = 1; a < L.numAbilities; a++) t.abilities[a] = r.abilityName(a);
		t.items.assign((size_t)L.numItems, {});
		for (int i = 1; i < L.numItems; i++) t.items[i].name = r.itemName(i);
		t.icons.clear(); t.iconPal.clear();		// (the party icons: not read for these yet)
		t.leaders.clear(); t.leaderTypes.clear();
		t.dexEntries = -1; t.wildHeaders = -1; t.trainers = -1; t.learnsets = -1;

		auto p = std::make_shared<OwnProfile>();
		p->title = "Pokémon (pokeemerald-expansion " + std::to_string(L.major) + "." + std::to_string(L.minor) + "." + std::to_string(L.patch) + ")";
		p->code = "BPEE";
		p->name = p->title.c_str();
		p->crc = 0;
		p->family = pkmn::EMERALD;
		p->partyCount = L.partyCount; p->party = L.party; p->battleMons = L.battleMons; p->battleFlags = L.battleFlags;
		p->mainFlags = L.gMain + 0x439; p->enemyParty = L.enemyParty;
		p->fixedSave = false; p->sb1 = L.sb1; p->sb2 = L.sb2; p->mapHeader = L.mapHeader;
		pkmn::SaveLayout& S = p->save;
		S.flags = L.flagsOffset; S.badge0 = L.badgeFlag; S.clear = (int)L.gameClearFlag;
		S.money = L.pcItemsOffset - 8; S.key = 0xac;
		uint32_t at = L.pcItemsOffset + 4u * L.pcItemsCount;
		for (int k = 0; k < 5; k++) { S.pocket[k] = at; S.count[k] = L.bagCount[k]; at += 4u * L.bagCount[k]; }
		S.dexOwned = 0;
		S.pos = (uint32_t)L.sb1Pos;
		S.dexSb1 = L.dexSeenOffset; S.dexBytes = (int)((L.dexCount + 1 + 7) / 8);
		pkmn::MonLayout& M = p->mon;
		M.size = L.monSize;
		M.coding = pkmn::MonCoding::ENCRYPTED;
		M.speciesMask = 0x7ff;
		if (L.monSize == 0x60) M.decode = decodePlain96;
		else M.decode = decodeExpansion;
		M.battler.size = L.battlerSize;
		M.battler.decode = decodeBattlerExp;
		profile = p;
		return true;
	}

	// ---- the DS pages' RomData -----------------------------------------------------------------------------------
	namespace
	{
		struct ExpData : RomData
		{
			std::shared_ptr<const std::vector<uint8_t>> rom;
			std::shared_ptr<const ExpansionLayout> L;
			std::vector<uint16_t> speciesOf;		// RomData index -> species id
			std::vector<uint16_t> items;			// the game's item id -> RomData's
			mutable std::mutex mu;
			mutable std::map<uint32_t, std::shared_ptr<const Sprite>> sprites;

			bool palette(long p, std::array<uint32_t, 16>& out) const
			{
				const uint8_t* r = rom->data();
				const size_t n = rom->size();
				if (p < 0) return false;
				std::vector<uint8_t> c;
				const uint8_t* src = r + p;
				if (r[p] == 0x10 && pkmn::lz77(r, n, (size_t)p, c, 512) && c.size() >= 32) src = c.data();
				else if ((size_t)p + 32 > n) return false;
				for (int k = 0; k < 16; k++)
				{
					uint16_t v = le16(src + 2 * k);
					auto f = [](int x) { return (uint32_t)(x << 3 | x >> 2); };
					out[k] = f(v & 31) << 16 | f(v >> 5 & 31) << 8 | f(v >> 10 & 31);
				}
				return true;
			}
			std::shared_ptr<const Sprite> sprite(uint16_t index, bool, uint8_t form) const override
			{
				if (!index || index >= speciesOf.size() || !speciesOf[index]) return nullptr;
				uint16_t s = speciesOf[index];
				if (form && index < species.size() && species[index].formsAt && form < species[index].formCount)
				{
					const uint16_t fi = (uint16_t)(species[index].formsAt + form - 1);
					if (fi < speciesOf.size() && speciesOf[fi]) s = speciesOf[fi];
				}
				std::lock_guard<std::mutex> lk(mu);
				auto it = sprites.find(s);
				if (it != sprites.end()) return it->second;
				const uint8_t* r = rom->data();
				const size_t n = rom->size();
				const size_t rec = (size_t)L->species + (size_t)L->spStride * s;
				std::vector<uint8_t> px;
				long fp = ptrAt(r, n, rec + L->spFront, 8);
				auto sp = std::make_shared<Sprite>();
				if (fp < 0 || !decompressPic(r, n, (size_t)fp, px, 0x4000) || px.size() < 0x800 ||
					!palette(ptrAt(r, n, rec + L->spPal, 4), sp->normal) || !palette(ptrAt(r, n, rec + L->spShiny, 4), sp->shiny))
				{ sprites[s] = nullptr; return nullptr; }
				sp->w = sp->h = 64;
				for (size_t f = 0; f < 2 && f * 0x800 + 0x800 <= px.size(); f++)
				{
					std::vector<uint8_t> fr(64 * 64);
					const uint8_t* d = px.data() + f * 0x800;
					for (int t = 0; t < 64; t++)
						for (int y = 0; y < 8; y++)
							for (int x = 0; x < 8; x++)
								fr[(size_t)((t / 8 * 8 + y) * 64 + t % 8 * 8 + x)] = (uint8_t)(d[t * 32 + y * 4 + x / 2] >> (4 * (x & 1)) & 15);
					sp->frames.push_back(std::move(fr));
				}
				if (sp->frames.size() >= 2 && sp->frames[1] != sp->frames[0]) sp->anim = { { 0, 1000 }, { 1, 250 }, { 0, 250 }, { 1, 250 } };
				else sp->frames.resize(1);
				sprites[s] = sp;
				return sp;
			}
		};

		std::string evoText(const RomData& r, uint16_t method, uint16_t param, const std::string& extra)
		{
			auto item = [&](uint16_t i) { return i < r.items.size() && !r.items[i].name.empty() ? r.items[i].name : std::string("an item"); };
			std::string lv = "Level " + std::to_string(param), s;
			switch (method)
			{
			case evo::FRIENDSHIP: s = "Level up with high friendship"; break;
			case evo::FRIENDSHIP_DAY: s = "Level up with high friendship by day"; break;
			case evo::FRIENDSHIP_NIGHT: s = "Level up with high friendship at night"; break;
			case evo::LEVEL: s = param ? lv : "Level up"; break;
			case evo::TRADE: s = "Trade"; break;
			case evo::TRADE_ITEM: s = "Trade holding " + item(param); break;
			case evo::USE_ITEM: s = "Use " + item(param); break;
			case evo::USE_ITEM_MALE: s = "Use " + item(param) + " (male)"; break;
			case evo::USE_ITEM_FEMALE: s = "Use " + item(param) + " (female)"; break;
			case evo::LEVEL_ATK_GT_DEF: s = lv + " with Attack higher than Defense"; break;
			case evo::LEVEL_ATK_EQ_DEF: s = lv + " with Attack equal to Defense"; break;
			case evo::LEVEL_ATK_LT_DEF: s = lv + " with Defense higher than Attack"; break;
			case evo::LEVEL_PID_LOW: case evo::LEVEL_PID_HIGH: s = lv + " (one of two, by its personality)"; break;
			case evo::LEVEL_NINJASK: s = lv; break;
			case evo::LEVEL_SHEDINJA: s = lv + " with a free party slot and a spare Poké Ball"; break;
			case evo::BEAUTY: s = "Level up with high Beauty"; break;
			case evo::HOLD_ITEM_DAY: s = "Level up holding " + item(param) + " by day"; break;
			case evo::HOLD_ITEM_NIGHT: s = "Level up holding " + item(param) + " at night"; break;
			case evo::KNOWS_MOVE: s = "Level up knowing " + (param < r.moves.size() ? r.moves[param].name : std::string("a move")); break;
			case evo::LEVEL_MALE: s = lv + " (male)"; break;
			case evo::LEVEL_FEMALE: s = lv + " (female)"; break;
			case evo::SPECIES_IN_PARTY: s = "Level up with " + (param < r.species.size() ? r.species[param].name : std::string("a Pokémon")) + " in the party"; break;
			case evo::TRADE_SPECIES: s = "Trade for " + (param < r.species.size() ? r.species[param].name : std::string("a Pokémon")); break;
			default: s = "Special condition"; break;
			}
			return extra.empty() ? s : s + " " + extra;
		}
	}

	// the Map page: the region map's sections (gRegionMapEntries: x, y, width, height and the name, 8 bytes a section,
	// only those a map uses: the builds keep Emerald's and Kanto's records, some where their own routes now are) as the
	// grid, and each zone's rectangle on it (its section's) and size in tiles (its map header's layout).
	//  - A build that kept Emerald's 28 x 15 map and its picture's format (Lazarus, Seaglass): that picture
	//    (pokemap.cpp found it), one map.
	//  - One that drew its own (the Johto builds: Johto, Kanto and more regions in one table, their sections on the
	//    same cells): no picture, the squares of the places, a map a region. A region is the maps joined by their
	//    connections (walking from one to the next); a map without any (a cave, a house) is the region its warps lead
	//    to first; a section is its maps' region; regions that share no cells go on one map (islands, a region's far
	//    corner), named after the region's first town.
	static void regionMap(const uint8_t* rom, size_t n, const ExpansionLayout& L, const pkmn::RomTables& t,
		const std::map<uint16_t, std::pair<int, long>>& zoneSec, RomData& out)
	{
		out.regionMaps.clear();
		if (L.mapsecs < 0 || L.numMapsecs <= 0 || (size_t)L.mapsecs + 8 * (size_t)L.numMapsecs > n) return;
		const int NS = L.numMapsecs;
		std::vector<int> mapsOf((size_t)NS, 0);
		for (auto& z : zoneSec) if (z.second.first >= 0 && z.second.first < NS) mapsOf[(size_t)z.second.first]++;
		auto entry = [&](int i) { return rom + L.mapsecs + 8 * (size_t)i; };
		auto onMap = [&](int i) { const uint8_t* e = entry(i);
			return mapsOf[(size_t)i] && e[2] && e[3] && !(e[0] == 0 && e[1] == 0) && e[0] < 64 && e[1] < 64; };
		int on = 0;
		for (int i = 0; i < NS; i++) on += onMap(i);
		if (on < 8) return;	// (no region map to speak of)

		// each section's region (-1: none found; one region when the picture is Emerald's), and whether a map of it is
		// outdoors (joined to another by a connection: drawn over the caves and towers on the same cell)
		std::vector<int> secRegion((size_t)NS, -1);
		std::vector<bool> secOutdoor((size_t)NS, true);
		int gw0 = 0, gh0 = 0;
		for (int i = 0; i < NS; i++) if (onMap(i)) { gw0 = std::max(gw0, entry(i)[0] + entry(i)[2]); gh0 = std::max(gh0, entry(i)[1] + entry(i)[3]); }
		const bool picture = gw0 <= 28 && gh0 <= 15 && !t.mapImages.empty() && !t.mapImages[0].rgba.empty();
		if (picture)
			for (int i = 0; i < NS; i++) secRegion[(size_t)i] = onMap(i) ? 0 : -1;
		else
		{
			// the maps: index by zone, their connections and warps
			std::vector<uint16_t> zones;
			std::map<uint16_t, int> at;
			for (auto& z : zoneSec) { at[z.first] = (int)zones.size(); zones.push_back(z.first); }
			const int NM = (int)zones.size();
			std::vector<int> up((size_t)NM);
			for (int i = 0; i < NM; i++) up[(size_t)i] = i;
			std::function<int(int)> root = [&](int i) { while (up[(size_t)i] != i) i = up[(size_t)i] = up[(size_t)up[(size_t)i]]; return i; };
			std::vector<bool> outdoor((size_t)NM, false);
			std::vector<std::vector<int>> warps((size_t)NM);
			for (int i = 0; i < NM; i++)
			{
				const long h = zoneSec.at(zones[(size_t)i]).second;
				const int sec = zoneSec.at(zones[(size_t)i]).first;
				const long cn = le32(rom + h + 12) ? ptrAt(rom, n, (size_t)h + 12, 8) : -1;
				if (cn >= 0 && sec >= 0 && sec < NS && onMap(sec))
				{
					const uint32_t count = le32(rom + cn);
					const long list = count && count < 16 ? ptrAt(rom, n, (size_t)cn + 4, 12 * (size_t)count) : -1;
					for (uint32_t k = 0; list >= 0 && k < count; k++)
					{
						const uint8_t* c = rom + list + 12 * k;
						auto o = at.find((uint16_t)(c[8] << 8 | c[9]));
						if (o == at.end() || c[0] < 1 || c[0] > 6) continue;
						outdoor[(size_t)i] = true;
						// (only between places next to each other on the map: the road from one region to the next
						// joins two that are drawn apart)
						const int os = zoneSec.at(zones[(size_t)o->second]).first;
						if (os < 0 || os >= NS || !onMap(os)) continue;
						const uint8_t* a = entry(sec);
						const uint8_t* b = entry(os);
						const int dx = std::max({ 0, a[0] - (b[0] + b[2]), b[0] - (a[0] + a[2]) }), dy = std::max({ 0, a[1] - (b[1] + b[3]), b[1] - (a[1] + a[3]) });
						if (dx > 1 || dy > 1) continue;
						up[(size_t)root(i)] = root(o->second);
					}
				}
				const long ev = ptrAt(rom, n, (size_t)h + 4, 16);
				if (ev < 0) continue;
				const int nw = rom[ev + 1];
				const long wl = nw ? ptrAt(rom, n, (size_t)ev + 8, 8 * (size_t)nw) : -1;
				for (int k = 0; wl >= 0 && k < nw; k++)
				{
					const uint8_t* w = rom + wl + 8 * k;
					auto o = at.find((uint16_t)(w[7] << 8 | w[6]));
					if (o != at.end()) { warps[(size_t)i].push_back(o->second); warps[(size_t)o->second].push_back(i); }
				}
			}
			// every map's region: an outdoor map's own, the others the nearest outdoor one's by warps
			std::vector<int> region((size_t)NM, -1);
			std::vector<int> q;
			for (int i = 0; i < NM; i++) if (outdoor[(size_t)i]) { region[(size_t)i] = root(i); q.push_back(i); }
			for (size_t qi = 0; qi < q.size(); qi++)
				for (int j : warps[(size_t)q[qi]])
					if (region[(size_t)j] < 0) { region[(size_t)j] = region[(size_t)q[qi]]; q.push_back(j); }
			// each section: the region most of its maps are in (outdoor ones counting first)
			std::vector<std::map<int, int>> votes((size_t)NS);
			for (int i = 0; i < NM; i++)
			{
				const int sec = zoneSec.at(zones[(size_t)i]).first;
				if (sec >= 0 && sec < NS && region[(size_t)i] >= 0) votes[(size_t)sec][region[(size_t)i]] += outdoor[(size_t)i] ? 1000 : 1;
			}
			for (int i = 0; i < NS; i++)
			{
				int best = -1, bestV = 0;
				for (auto& v : votes[(size_t)i]) if (v.second > bestV) { best = v.first; bestV = v.second; }
				secRegion[(size_t)i] = onMap(i) ? best : -1;
				secOutdoor[(size_t)i] = bestV >= 1000;
			}
			// the regions onto maps: the biggest first, each on the first map whose cells it doesn't share
			std::map<int, std::set<int>> cellsOf;
			for (int i = 0; i < NS; i++)
				if (secRegion[(size_t)i] >= 0)
				{
					const uint8_t* e = entry(i);
					for (int y = e[1]; y < e[1] + e[3]; y++) for (int x = e[0]; x < e[0] + e[2]; x++) cellsOf[secRegion[(size_t)i]].insert(y * 64 + x);
				}
			std::vector<std::pair<int, int>> bySize;
			for (auto& c : cellsOf) bySize.push_back({ -(int)c.second.size(), c.first });
			std::sort(bySize.begin(), bySize.end());
			std::vector<std::set<int>> mapCells;
			std::map<int, int> mapOfRegion;
			for (auto& bs : bySize)
			{
				const auto& cs = cellsOf[bs.second];
				int mi = -1;
				for (size_t k = 0; k < mapCells.size() && mi < 0; k++)
				{
					bool clash = false;
					for (int c : cs) if (mapCells[k].count(c)) { clash = true; break; }
					if (!clash) mi = (int)k;
				}
				// (a few places of their own - a plateau the road reaches, a pass - go with the map they share the
				// fewest cells with, not on a map of their own)
				if (mi < 0 && !mapCells.empty() && cs.size() <= 12)
				{
					size_t fewest = SIZE_MAX;
					for (size_t k = 0; k < mapCells.size(); k++)
					{
						size_t shared = 0;
						for (int c : cs) shared += mapCells[k].count(c);
						if (shared < fewest) { fewest = shared; mi = (int)k; }
					}
				}
				if (mi < 0) { mi = (int)mapCells.size(); mapCells.emplace_back(); }
				mapCells[(size_t)mi].insert(cs.begin(), cs.end());
				mapOfRegion[bs.second] = mi;
			}
			for (int i = 0; i < NS; i++) if (secRegion[(size_t)i] >= 0) secRegion[(size_t)i] = mapOfRegion[secRegion[(size_t)i]];
		}

		// the maps: each one's grid (towns, the first sections, over the routes), its size from its own sections
		int maps = 0;
		for (int i = 0; i < NS; i++) maps = std::max(maps, secRegion[(size_t)i] + 1);
		for (int mi = 0; mi < maps; mi++)
		{
			RegionMap m;
			m.cell = 8;
			int x0 = 64, y0 = 64, x1 = 0, y1 = 0, count = 0;
			for (int i = 0; i < NS; i++)
				if (secRegion[(size_t)i] == mi) { const uint8_t* e = entry(i); count++; x0 = std::min<int>(x0, e[0]); y0 = std::min<int>(y0, e[1]); x1 = std::max(x1, e[0] + e[2]); y1 = std::max(y1, e[1] + e[3]); }
			if (picture) { x0 = 0; y0 = 0; x1 = std::max(x1, 28); y1 = std::max(y1, 15); }
			else { x0 = std::max(0, x0 - 1); y0 = std::max(0, y0 - 1); x1 += 1; y1 += 1; }	// (a cell around the places)
			m.gridW = x1; m.gridH = y1;	// (cell 0, 0 kept: a section's x, y is its cell; the picture starts at x0, y0)
			m.cells.assign((size_t)m.gridW * m.gridH, 0xFFFF);
			std::string first;
			for (int pass = 0; pass < 2; pass++)	// (the caves and towers first, the towns and routes over them)
				for (int i = NS - 1; i >= 0; i--)
				{
					if (secRegion[(size_t)i] != mi || secOutdoor[(size_t)i] != (pass == 1)) continue;
					const uint8_t* e = entry(i);
					for (int y = e[1]; y < e[1] + e[3]; y++) for (int x = e[0]; x < e[0] + e[2]; x++) m.cells[(size_t)y * m.gridW + x] = (uint16_t)i;
				}
			if (picture)
			{
				const auto& im = t.mapImages[0];
				m.w = im.w; m.h = im.h; m.cellX = im.cellX; m.cellY = im.cellY;
				m.rgba.resize((size_t)im.w * im.h);
				for (size_t i = 0; i < m.rgba.size(); i++)
				{
					const uint8_t* p = &im.rgba[i * 4];
					m.rgba[i] = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
				}
			}
			else
			{
				m.cellX = -x0 * m.cell; m.cellY = -y0 * m.cell;
				m.w = (x1 - x0) * m.cell; m.h = (y1 - y0) * m.cell;
				// (its name: the region's, by its first town as the games' new games start there; else that town's)
				static const char* const REGIONS[][2] = { { "New Bark Town", "Johto" }, { "Pallet Town", "Kanto" },
					{ "Littleroot Town", "Hoenn" }, { "Twinleaf Town", "Sinnoh" }, { "Iki Town", "Alola" }, { "Melemele Isle", "Alola" },
					{ "Sinjoh Ruins", "Sinjoh" }, { "New Sinjoh", "Sinjoh" } };
				std::string name, town;
				for (int i = 0; i < NS && name.empty(); i++)
				{
					if (secRegion[(size_t)i] != mi || (size_t)i >= out.locationNames.size()) continue;
					const std::string& nm = out.locationNames[(size_t)i];
					for (auto& r : REGIONS) if (nm == r[0]) name = r[1];
					if (town.empty() && (nm.find(" Town") != std::string::npos || nm.find(" City") != std::string::npos)) town = nm;
				}
				if (name.empty()) name = !town.empty() ? town : "Map " + std::to_string(mi + 1);
				for (auto& other : out.regionMaps) if (other.name == name) name += " " + std::to_string(mi + 1);	// (the page's chips are by name)
				m.name = name;
			}
			std::string few;
			for (int i = 0; count <= 8 && i < NS; i++) if (secRegion[(size_t)i] == mi && (size_t)i < out.locationNames.size()) few += " " + out.locationNames[(size_t)i] + ";";
			fprintf(stderr, "pokemon: expansion region map %d: %s, %d sections, %d x %d%s%s\n", mi, m.name.c_str(), count, x1 - x0, y1 - y0,
				picture ? ", the game's picture" : "", few.c_str());
			out.regionMaps.push_back(std::move(m));
		}

		// each zone: its section's rectangle on its map, its own size in tiles
		for (auto& kv : out.zones)
		{
			auto zs = zoneSec.find(kv.first);
			if (zs == zoneSec.end()) continue;
			const int sec = zs->second.first;
			if (sec < 0 || sec >= NS || secRegion[(size_t)sec] < 0) continue;	// (a section not on the map: a building, a cave)
			const RegionMap& rm = out.regionMaps[(size_t)secRegion[(size_t)sec]];
			const uint8_t* e = entry(sec);
			MapSpot& s = kv.second.spot;
			s.map = (int16_t)secRegion[(size_t)sec];
			s.x = (float)(rm.cellX + e[0] * rm.cell); s.y = (float)(rm.cellY + e[1] * rm.cell);
			s.w = (float)(e[2] * rm.cell); s.h = (float)(e[3] * rm.cell);
			const long layout = ptrAt(rom, n, (size_t)zs->second.second, 8);	// (the header's first word: its layout, width and height first)
			if (layout >= 0)
			{
				const uint32_t w = le32(rom + layout), h = le32(rom + layout + 4);
				if (w && h && w < 1024 && h < 1024) { s.tilesW = (uint16_t)w; s.tilesH = (uint16_t)h; }
			}
		}
	}

	std::shared_ptr<const RomData> readExpansionRomData(const pkmn::RomTables& t, const Game& game)
	{
		if (!t.rom) return nullptr;
		const uint8_t* rom = t.rom->data();
		const size_t n = t.rom->size();
		auto Lp = layoutOf(rom, n);
		if (!Lp || !Lp->why.empty()) return nullptr;
		const ExpansionLayout& L = *Lp;
		Reader r{ rom, n, L };
		auto out = std::make_shared<ExpData>();
		out->rom = t.rom;
		out->L = Lp;
		out->game = game;
		out->game.gen = Gen::III;
		out->game.modern = true;
		const Dex dex = dexOf(r);
		out->game.dexSize = dex.dexSize;
		out->speciesOf = dex.species;
		auto idx = [&](int s) -> uint16_t { return s > 0 && s < (int)dex.index.size() ? dex.index[s] : 0; };

		// items: Gen IV ids where the names match, else GEN3_ONLY + the game's id
		out->items = itemMapOf(r);
		uint16_t top = 0;
		for (uint16_t v : out->items) top = std::max(top, v);
		out->RomData::items.resize((size_t)top + 1);
		for (int i = 1; i < L.numItems; i++)
		{
			ItemData& it = out->RomData::items[out->items[i]];
			it.name = r.itemName(i);
			it.price = (uint16_t)std::min<uint32_t>(65535, le32(rom + r.it(i)));
			it.description = text(rom, n, ptrAt(rom, n, r.it(i) + 0xc), 300);
			it.pocket = (uint8_t)r.itemPocket(i);
		}
		auto item = [&](uint16_t i) -> uint16_t { return i && i < out->items.size() ? out->items[i] : 0; };

		// moves: the category is each move's own (bits 5-6: physical, special, status)
		out->moves.resize((size_t)L.numMoves);
		for (int m = 1; m < L.numMoves; m++)
		{
			const size_t a = r.mv(m);
			if (a + 0x14 > n) break;
			MoveData& d = out->moves[m];
			d.name = r.moveName(m);
			d.description = text(rom, n, ptrAt(rom, n, a + 4), 300);
			const uint16_t bits = le16(rom + a + 0xa);
			d.effect = le16(rom + a + 8);
			d.type = ndsTypeOf(bits & 31);
			if (d.type == NO_TYPE) d.type = NORMAL;
			const int cat = bits >> 5 & 3;
			d.cat = cat == 0 ? MoveCategory::PHYSICAL : cat == 1 ? MoveCategory::SPECIAL : MoveCategory::STATUS;
			d.power = (uint8_t)std::min(255, bits >> 7);
			d.accuracy = rom[a + 0xc] & 0x7f;
			d.pp = rom[a + 0xe];
			const int pr = L.mvPriority ? (int)(int8_t)(rom[a + L.mvPriority] << 4) >> 4 : 0;	// (s32 priority:4)
			d.priority = (uint8_t)(int8_t)pr;
		}

		// abilities, natures (the standard 25: their English names), TMs (the TM/HM items' moves)
		out->abilities.assign((size_t)L.numAbilities, "");
		out->abilityText.assign((size_t)L.numAbilities, "");
		int abDesc = 0;
		for (int o = 0x14; o + 4 <= L.abStride && !abDesc; o += 4) if (isText(rom, n, ptrAt(rom, n, r.ab(1) + o), 300)) abDesc = o;
		for (int a = 1; a < L.numAbilities; a++)
		{
			out->abilities[a] = r.abilityName(a);
			if (abDesc) out->abilityText[a] = text(rom, n, ptrAt(rom, n, r.ab(a) + abDesc), 300);
		}
		for (int i = 0; i < 25; i++) out->natures.push_back(pkmn::natureName(i));
		out->tmMoves.assign(100, 0);
		std::map<uint16_t, int> tmOfMove;		// move -> TM/HM number (TM01-92 1-92, HM01-08 93-100)
		// (the move: the item's secondaryId (1.9), else gTMHMItemMoveIds' {item, move} pairs (1.11 on: {0, 0} first, then
		// the TMs' item ids in a row), found by TM01's id)
		std::map<int, uint16_t> machineMove;
		{
			int tm01 = 0, tm02 = 0;
			for (int i = 1; i < L.numItems && !tm02; i++) { int k = tmNumberOf(norm(r.itemName(i))); if (k == 1) tm01 = i; else if (k == 2 && tm01) tm02 = i; }
			if (tm01 && tm02 == tm01 + 1 && !le16(rom + r.it(tm01) + 4))
			{
				for (size_t a = 0; a + 16 <= n; a += 4)
					if (!le32(rom + a) && le16(rom + a + 4) == tm01 && le16(rom + a + 8) == tm02 && le16(rom + a + 12) == tm02 + 1 &&
						le16(rom + a + 6) && le16(rom + a + 6) < L.numMoves && le16(rom + a + 10) < L.numMoves)
					{
						for (size_t k = 1; a + 4 * k + 4 <= n && k < 200; k++)
						{
							const uint16_t itm = le16(rom + a + 4 * k), mv = le16(rom + a + 4 * k + 2);
							if (!itm || itm >= L.numItems || !mv || mv >= L.numMoves) break;
							machineMove[itm] = mv;
						}
						break;
					}
			}
		}
		for (int i = 1; i < L.numItems; i++)
		{
			const int tm = tmNumberOf(norm(r.itemName(i)));
			if (!tm) continue;
			const uint16_t mv = machineMove.count(i) ? machineMove[i] : le16(rom + r.it(i) + 4);	// (secondaryId: the move)
			if (tm < 1 || tm > 100 || !mv || mv >= L.numMoves) continue;
			out->tmMoves[(size_t)tm - 1] = mv;
			if (!tmOfMove.count(mv)) tmOfMove[mv] = tm;
		}

		// species, by RomData index
		out->species.resize(dex.species.size());
		for (size_t d = 1; d < dex.species.size(); d++)
		{
			const int s = dex.species[d];
			if (!s) continue;
			Species& sp = out->species[d];
			const size_t a = r.sp(s);
			const uint8_t* b = rom + a;
			sp.name = r.speciesName(s);
			sp.types[0] = ndsTypeOf(b[6]); sp.types[1] = ndsTypeOf(b[7]);
			if (sp.types[0] == NO_TYPE) sp.types[0] = NORMAL;
			if (sp.types[1] == NO_TYPE) sp.types[1] = sp.types[0];
			for (int i = 0; i < 6; i++) sp.base[i] = b[i];
			sp.catchRate = b[8]; sp.baseExp = le16(b + 0xa);
			const uint16_t ev = le16(b + 0xc);
			for (int i = 0; i < 6; i++) sp.evYield[i] = (uint8_t)(ev >> (2 * i) & 3);
			sp.genderRatio = b[0x12]; sp.baseFriendship = b[0x14]; sp.growth = b[0x15];
			for (int i = 0; i < 3 && i < L.abilitySlots; i++) sp.abilities[i] = le16(b + 0x18 + 2 * i);
			if (sp.abilities[1] == 0) sp.abilities[1] = 0;
			sp.category = niceName(text(rom, n, (long)(a + L.spCategory), 13));
			sp.height = le16(b + L.spHeight); sp.weight = le16(b + L.spWeight);
			sp.dexText = text(rom, n, ptrAt(rom, n, a + L.spDesc), 600);
			// level-up moves
			long l = ptrAt(rom, n, a + L.spLearn, 4);
			for (int k = 0; l >= 0 && k < 120 && (size_t)l + 4 * k + 4 <= n; k++)
			{
				const uint16_t mv = le16(rom + l + 4 * k), lv = le16(rom + l + 4 * k + 2);
				if (mv == 0xffff) break;
				sp.learnset.push_back({ (uint8_t)std::min<uint16_t>(lv, 255), mv });
			}
			// TMs and HMs: the teachable moves that a TM/HM teaches
			if (L.spTeach >= 0)
			{
				long q = ptrAt(rom, n, a + L.spTeach, 2);
				for (int k = 0; q >= 0 && k < 400 && (size_t)q + 2 * k + 2 <= n; k++)
				{
					const uint16_t mv = le16(rom + q + 2 * k);
					if (mv == 0xffff) break;
					auto f = tmOfMove.find(mv);
					if (f != tmOfMove.end()) sp.tms.push_back((uint16_t)f->second);
				}
				std::sort(sp.tms.begin(), sp.tms.end());
				sp.tms.erase(std::unique(sp.tms.begin(), sp.tms.end()), sp.tms.end());
			}
		}
		// evolutions (after all names): the old methods (1.9: Gen III's 1-15 and more), or 1.11's method + conditions.
		// (Lazarus: Pikachu's Thunder Stone is method 6, not EVO_ITEM's 7 - its own list, one shorter from 5 on: taken
		// as EVO_TRADE left out)
		int oldShift = 0;
		if (!L.evoConditions && L.spEvo >= 0)
			for (size_t d = 1; d < dex.species.size(); d++)
				if (dex.species[d] && out->species[d].name == "Pikachu")
				{
					long p = ptrAt(rom, n, r.sp(dex.species[d]) + L.spEvo, 6);
					if (p >= 0 && le16(rom + p) == 6) oldShift = 1;
					break;
				}
		for (size_t d = 1; d < dex.species.size() && L.spEvo >= 0; d++)
		{
			const int s = dex.species[d];
			if (!s) continue;
			long p = ptrAt(rom, n, r.sp(s) + L.spEvo, 6);
			for (int k = 0; p >= 0 && k < 12; k++)
			{
				const size_t e = (size_t)p + (size_t)L.evoStride * k;
				if (e + 6 > n) break;
				uint16_t m = le16(rom + e), param = le16(rom + e + 2);
				const uint16_t into = idx(le16(rom + e + 4));
				if (m == 0xffff) break;
				if (!into || m == 0xfffe) continue;
				uint16_t method = evo::LEVEL; std::string extra;
				if (!L.evoConditions)
				{
					if (oldShift && m >= 5) m = (uint16_t)(m + oldShift);
					static const uint16_t OLD[] = { 0, evo::FRIENDSHIP, evo::FRIENDSHIP_DAY, evo::FRIENDSHIP_NIGHT, evo::LEVEL, evo::TRADE, evo::TRADE_ITEM,
						evo::USE_ITEM, evo::LEVEL_ATK_GT_DEF, evo::LEVEL_ATK_EQ_DEF, evo::LEVEL_ATK_LT_DEF, evo::LEVEL_PID_LOW, evo::LEVEL_PID_HIGH,
						evo::LEVEL_NINJASK, evo::LEVEL_SHEDINJA, evo::BEAUTY, evo::LEVEL_FEMALE, evo::LEVEL_MALE, evo::LEVEL, evo::LEVEL, evo::LEVEL,
						evo::HOLD_ITEM_DAY, evo::HOLD_ITEM_NIGHT, evo::KNOWS_MOVE, 0, 0, evo::USE_ITEM_MALE, evo::USE_ITEM_FEMALE, evo::LEVEL,
						evo::SPECIES_IN_PARTY, evo::LEVEL, evo::TRADE_SPECIES, 0, evo::LEVEL, evo::LEVEL, 0, 0, 0, 0, evo::USE_ITEM, evo::USE_ITEM, evo::HOLD_ITEM_DAY };
					method = m < sizeof OLD / sizeof OLD[0] ? OLD[m] : 0;
					if (m == 18) extra = "at night"; else if (m == 19) extra = "by day"; else if (m == 20) extra = "at dusk";
					else if (m == 28) extra = "in the rain"; else if (m == 39) extra = "at night"; else if (m == 40) extra = "by day";
					else if (m == 41) extra = "(held)";
					if (method == evo::TRADE_ITEM || method == evo::USE_ITEM || method == evo::USE_ITEM_MALE || method == evo::USE_ITEM_FEMALE ||
						method == evo::HOLD_ITEM_DAY || method == evo::HOLD_ITEM_NIGHT) param = item(param);
					else if (method == evo::SPECIES_IN_PARTY || method == evo::TRADE_SPECIES) param = idx(param);
				}
				else
				{
					// EVO_LEVEL 1, EVO_TRADE 2, EVO_ITEM 3 (others: special); conditions IF_GENDER 0, IF_TIME 1, IF_NOT_TIME 2,
					// IF_MIN_FRIENDSHIP 3, IF_ATK_GT/EQ/LT_DEF 4-6, IF_HOLD_ITEM 7, IF_MIN_BEAUTY 11, IF_SPECIES_IN_PARTY 16,
					// IF_KNOWS_MOVE 19, IF_TRADE_PARTNER_SPECIES 20 (1.15's numbering)
					std::vector<std::array<uint16_t, 4>> cond;
					long c = ptrAt(rom, n, e + 8, 8);
					for (int q = 0; c >= 0 && q < 6 && (size_t)c + 8 * q + 8 <= n; q++)
					{
						const uint16_t id = le16(rom + c + 8 * q);
						if (id >= 39) break;
						cond.push_back({ id, le16(rom + c + 8 * q + 2), le16(rom + c + 8 * q + 4), le16(rom + c + 8 * q + 6) });
					}
					auto has = [&](uint16_t id, uint16_t* arg = nullptr) { for (auto& x : cond) if (x[0] == id) { if (arg) *arg = x[1]; return true; } return false; };
					uint16_t a1 = 0;
					if (m == 1)
					{
						method = evo::LEVEL;
						if (has(3)) { method = evo::FRIENDSHIP; uint16_t tm; if (has(1, &tm)) method = tm == 3 ? evo::FRIENDSHIP_NIGHT : evo::FRIENDSHIP_DAY; param = 0; }
						else if (has(7, &a1)) { method = evo::HOLD_ITEM_DAY; uint16_t tm; if (has(1, &tm) && tm == 3) method = evo::HOLD_ITEM_NIGHT; param = item(a1); }
						else if (has(19, &a1)) { method = evo::KNOWS_MOVE; param = a1; }
						else if (has(16, &a1)) { method = evo::SPECIES_IN_PARTY; param = idx(a1); }
						else if (has(4)) method = evo::LEVEL_ATK_GT_DEF;
						else if (has(5)) method = evo::LEVEL_ATK_EQ_DEF;
						else if (has(6)) method = evo::LEVEL_ATK_LT_DEF;
						else if (has(11)) method = evo::BEAUTY;
						else if (has(0, &a1)) method = a1 == 0 ? evo::LEVEL_MALE : evo::LEVEL_FEMALE;
						else { uint16_t tm; if (has(1, &tm)) extra = tm == 3 ? "at night" : "by day"; }
					}
					else if (m == 2)
					{
						method = evo::TRADE;
						if (has(7, &a1)) { method = evo::TRADE_ITEM; param = item(a1); }
						else if (has(20, &a1)) { method = evo::TRADE_SPECIES; param = idx(a1); }
					}
					else if (m == 3) { method = evo::USE_ITEM; param = item(param); uint16_t g; if (has(0, &g)) method = g == 0 ? evo::USE_ITEM_MALE : evo::USE_ITEM_FEMALE; }
					else method = 0;
					if (!cond.empty() && method == evo::LEVEL && extra.empty() && cond.size() > 0) extra = "(and a condition)";
				}
				Evolution ev{ method, param, into, "" };
				ev.how = evoText(*out, method, param, extra);
				out->species[d].evolutions.push_back(ev);
			}
		}
		// forms: a base species' forms (the same national number) after the Pokédex
		for (size_t d = (size_t)dex.dexSize + 1; d < dex.species.size(); d++)
		{
			const int s = dex.species[d];
			const int nat = le16(rom + r.sp(s) + L.spNatDex);
			if (nat < 1 || nat > dex.dexSize) continue;
			Species& base = out->species[(size_t)nat];
			if (!base.formsAt) base.formsAt = (uint16_t)d;
			if (base.formsAt + base.formCount - 1 == (int)d) base.formCount++;
		}

		// places: the region map section names, the maps the wild table names (zone = group << 8 | number)
		out->locationNames = t.mapNames;
		if (L.mapsecs >= 0)
		{
			out->locationNames.assign((size_t)L.numMapsecs, "");
			for (int i = 0; i < L.numMapsecs; i++) out->locationNames[(size_t)i] = text(rom, n, ptrAt(rom, n, (size_t)L.mapsecs + 8 * (size_t)i + 4), 30);
		}
		if (out->locationNames.size() < 256) out->locationNames.resize(256);
		for (auto& x : out->locationNames) x = niceName(x);
		if (L.wild >= 0)
		{
			static const int GRASS[12] = { 20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1 }, WATER[5] = { 60, 30, 5, 4, 1 },
				OLD[2] = { 70, 30 }, GOOD[3] = { 60, 20, 20 }, SUPER[5] = { 40, 40, 15, 4, 1 }, HIDDEN[3] = { 60, 30, 10 };
			for (size_t a = (size_t)L.wild; a + (size_t)L.wildStride <= n && rom[a] != 0xff; a += (size_t)L.wildStride)
			{
				const int g = rom[a], m = rom[a + 1];
				const uint16_t z = (uint16_t)(g << 8 | m);
				Location& loc = out->zones[z];
				if (loc.id || !loc.encounters.empty()) continue;	// (a map's first table)
				loc.id = z;
				// each time of day's tables (1.11 on: morning, day, evening, night; one without any falls back in the game):
				// all the same -> any time
				std::vector<std::vector<Encounter>> byTime((size_t)L.wildTimes);
				for (int ti = 0; ti < L.wildTimes; ti++)
					for (int k = 0; k < L.wildKinds; k++)
					{
						long info = ptrAt(rom, n, a + 4 + 4 * (ti * L.wildKinds + k), 8);
						if (info < 0) continue;
						long list = ptrAt(rom, n, (size_t)info + 4, 4);
						if (list < 0) continue;
						auto add = [&](int from, int cnt, const int* pct, EncounterKind kind) {
							std::map<uint16_t, Encounter> one;
							for (int q = 0; q < cnt; q++)
							{
								const size_t w = (size_t)list + 4 * (size_t)(from + q);
								if (w + 4 > n) return;
								const uint16_t spc = idx(le16(rom + w + 2));
								if (!spc) continue;
								Encounter& e = one[spc];
								if (!e.species) { e.species = spc; e.minLevel = rom[w]; e.maxLevel = rom[w + 1]; e.kind = kind; }
								e.minLevel = std::min(e.minLevel, rom[w]); e.maxLevel = std::max(e.maxLevel, rom[w + 1]);
								e.percent = (uint8_t)std::min(100, e.percent + pct[q]);
							}
							for (auto& x : one) byTime[(size_t)ti].push_back(x.second);
						};
						if (k == 0) add(0, 12, GRASS, EncounterKind::GRASS);
						else if (k == 1) add(0, 5, WATER, EncounterKind::SURF);
						else if (k == 2) add(0, 5, WATER, EncounterKind::ROCK_SMASH);
						else if (k == 3) { add(0, 2, OLD, EncounterKind::OLD_ROD); add(2, 3, GOOD, EncounterKind::GOOD_ROD); add(5, 5, SUPER, EncounterKind::SUPER_ROD); }
						else add(0, 3, HIDDEN, EncounterKind::SPECIAL);
					}
				auto key = [](const std::vector<Encounter>& v) { std::string k; for (auto& e : v) k += std::to_string(e.species) + "," + std::to_string(e.minLevel) + "," +
					std::to_string(e.maxLevel) + "," + std::to_string(e.percent) + "," + std::to_string((int)e.kind) + ";"; return k; };
				std::vector<int> used;
				for (int ti = 0; ti < L.wildTimes; ti++) if (!byTime[(size_t)ti].empty()) used.push_back(ti);
				bool same = true;
				for (int ti : used) same = same && key(byTime[(size_t)ti]) == key(byTime[(size_t)used[0]]);
				static const TimeOfDay TOD[4] = { TimeOfDay::MORNING, TimeOfDay::DAY, TimeOfDay::DAY, TimeOfDay::NIGHT };
				std::vector<std::string> done;
				for (int ti : used)
				{
					const std::string k = key(byTime[(size_t)ti]);
					if (std::find(done.begin(), done.end(), k) != done.end()) continue;	// (evening = day's)
					done.push_back(k);
					for (Encounter e : byTime[(size_t)ti])
					{
						e.time = same || L.wildTimes != 4 ? TimeOfDay::ANY : TOD[ti];
						loc.encounters.push_back(e);
					}
					if (same) break;
				}
			}
		}
		// the zones' names: each map's region map section (the map groups, found by their shape against the wild maps)
		std::map<uint16_t, std::pair<int, long>> zoneSec;	// (each zone's section and map header: the Map page below)
		if (!out->zones.empty())
		{
			int maxGroup = 0;
			for (auto& z : out->zones) maxGroup = std::max(maxGroup, z.first >> 8);
			auto headerOk = [&](long h) { return h >= 0 && ptrAt(rom, n, (size_t)h) >= 0 && ptrAt(rom, n, (size_t)h + 4) >= 0 &&
				(!le32(rom + h + 8) || ptrAt(rom, n, (size_t)h + 8) >= 0) && (!le32(rom + h + 12) || ptrAt(rom, n, (size_t)h + 12) >= 0); };
			auto mapAt = [&](size_t table, int g, int m) -> long {
				long grp = ptrAt(rom, n, table + 4 * (size_t)g, 4);
				if (grp < 0) return -1;
				long h = ptrAt(rom, n, (size_t)grp + 4 * (size_t)m, 0x1c);
				return headerOk(h) ? h : -1;
			};
			long groups = -1; int numGroups = 0;
			for (size_t a = 0; a + 4 * (size_t)(maxGroup + 1) <= n && groups < 0; a += 4)
			{
				int run = 0;
				while (run < 256 && ptrAt(rom, n, a + 4 * (size_t)run) >= 0) run++;
				if (run < 8 || run * 4 < (maxGroup + 1) * 3) { a += 4 * (size_t)std::max(0, run - 1); continue; }
				// (every map the wild table names but a few: a hack's table may name a map it removed)
				if (mapAt(a, 0, 0) < 0) continue;
				int bad = 0;
				for (auto& z : out->zones) if (((z.first >> 8) >= run || mapAt(a, z.first >> 8, z.first & 0xff) < 0) && ++bad > (int)out->zones.size() / 8 + 1) break;
				if (bad <= (int)out->zones.size() / 8 + 1) { groups = (long)a; numGroups = run; }
			}
			// each group's maps: headers until the next group's list starts (the lists lie one after another)
			std::set<long> starts;
			for (int g = 0; groups >= 0 && g < numGroups; g++) starts.insert(ptrAt(rom, n, (size_t)groups + 4 * (size_t)g, 4));
			if (groups >= 0)
				for (int g = 0; g < numGroups; g++)
				{
					long grp = ptrAt(rom, n, (size_t)groups + 4 * (size_t)g, 4);
					if (grp < 0) break;
					for (int m = 0; m < 255; m++)
					{
						if (m && starts.count(grp + 4 * m)) break;
						long h = mapAt((size_t)groups, g, m);
						if (h < 0) break;
						const uint16_t z = (uint16_t)(g << 8 | m);
						const int sec = L.numMapsecs > 256 ? le16(rom + h + 0x14) : rom[h + 0x14];
						Location& loc = out->zones[z];
						loc.id = z;
						if (sec < (int)out->locationNames.size()) loc.name = out->locationNames[sec];
						zoneSec[z] = { sec, h };
					}
				}
		}
		regionMap(rom, n, L, t, zoneSec, *out);

		// trainers, by their index (the battle's opponent id)
		for (int i = 1; L.trainers >= 0 && i < L.numTrainers; i++)
		{
			const size_t a = (size_t)L.trainers + (size_t)L.trStride * i;
			Trainer tr;
			tr.id = (uint16_t)i;
			const uint8_t cls = rom[a + L.trClass];
			if (L.classes >= 0 && cls < L.numClasses) tr.cls = niceName(text(rom, n, L.classes + 16 * (long)cls, 13));
			tr.name = niceName(text(rom, n, (long)(a + L.trName), 11));
			tr.doubleBattle = L.trBattleType >= 0 && (rom[a + L.trBattleType] & (L.trStride == 0x24 ? 1 : 3)) != 0;
			const int size = rom[a + L.trSize];
			long p = ptrAt(rom, n, a + L.trParty, (size_t)L.tmonStride * (size_t)std::max(1, size));
			for (int k = 0; p >= 0 && k < size && k < 6; k++)
			{
				const size_t e = (size_t)p + (size_t)L.tmonStride * k;
				TrainerMon m;
				m.species = idx(le16(rom + e + L.tmonSpecies));
				m.item = item(le16(rom + e + L.tmonItem));
				m.level = rom[e + L.tmonLevel];
				for (int j = 0; j < 4; j++) m.moves[j] = le16(rom + e + L.tmonMoves + 2 * j);
				if (!m.moves[0] && m.species < out->species.size())
				{
					std::vector<uint16_t> known;
					for (auto& lm : out->species[m.species].learnset)
						if (lm.level <= m.level && std::find(known.begin(), known.end(), lm.move) == known.end()) known.push_back(lm.move);
					size_t from = known.size() > 4 ? known.size() - 4 : 0;
					for (size_t j = from; j < known.size(); j++) m.moves[j - from] = known[j];
				}
				tr.team.push_back(m);
			}
			if (!tr.team.empty() || !tr.name.empty()) out->trainers[tr.id] = std::move(tr);
		}
		return out;
	}

	std::function<uint16_t(uint16_t)> expansionItemMap(const RomData* rom)
	{
		auto e = dynamic_cast<const ExpData*>(rom);
		if (!e) return nullptr;
		std::shared_ptr<const std::vector<uint16_t>> m = std::make_shared<std::vector<uint16_t>>(e->items);
		return [m](uint16_t i) -> uint16_t { return i < m->size() ? (*m)[i] : (uint16_t)(GEN3_ONLY + i); };
	}

	namespace
	{
		// pkmn::Companion's hook: an expansion build's profile and tables, from the whole ROM
		bool ownProfile(const uint8_t* rom, size_t n, std::shared_ptr<const pkmn::GameProfile>& p, pkmn::RomTables& t)
		{
			int a, b, c;
			if (!expansionVersion(rom, n, a, b, c)) return false;
			return expansionProfile(rom, n, p, t);
		}
		const bool registered = (pkmn::setOwnProfileReader(ownProfile), true);
	}
}
