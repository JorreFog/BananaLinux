// banana-ui: the DS ROM's containers and text (nitro.h)
#include "nds/nitro.h"
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace banana::nds::nitro
{
	// limits on sizes read from the ROM, checked before allocating: the games' ARM9 binaries and overlays unpack to
	// under 2 MiB and their file tables are a few KiB (a broken or hostile size must not take the handheld's RAM)
	static const size_t MAX_UNPACKED = 16u << 20, MAX_TABLE = 4u << 20;

	// ---- the cartridge's file system ------------------------------------------------------------------------------
	Rom::~Rom()
	{
		if (fd_ >= 0) close(fd_);
	}

	bool Rom::open(const std::string& path, std::string* err)
	{
		fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
		if (fd_ < 0) { if (err) *err = "can't open " + path; return false; }
		off_t end = lseek(fd_, 0, SEEK_END);
		size_ = end > 0 ? (uint64_t)end : 0;
		return load(err);
	}

	bool Rom::openMemory(Bytes image, std::string* err)
	{
		mem_ = std::move(image);
		size_ = mem_.size();
		return load(err);
	}

	bool Rom::readAt(uint64_t off, size_t n, uint8_t* dst) const
	{
		if (off > size_ || n > size_ - off) return false;
		if (fd_ < 0) { if (n) memcpy(dst, mem_.data() + off, n); return true; }
		while (n)
		{
			ssize_t r = pread(fd_, dst, n, (off_t)off);
			if (r <= 0) return false;
			dst += r; off += (uint64_t)r; n -= (size_t)r;
		}
		return true;
	}

	bool Rom::load(std::string* err)
	{
		auto fail = [&](const char* why) { if (err) *err = why; return false; };
		if (!readAt(0, sizeof hdr_, hdr_)) return fail("too small for a DS ROM");
		uint32_t fntOff = rd32(hdr_ + 0x40), fntLen = rd32(hdr_ + 0x44), fatOff = rd32(hdr_ + 0x48), fatLen = rd32(hdr_ + 0x4c);
		// (sizes from the file are checked against it before anything is allocated for them)
		if (fntOff > size_ || fntLen > size_ - fntOff || fatOff > size_ || fatLen > size_ - fatOff || fntLen > MAX_TABLE || fatLen > MAX_TABLE)
			return fail("file tables outside the ROM");
		Bytes fat(fatLen), fnt(fntLen);
		if (!readAt(fatOff, fatLen, fat.data()) || !readAt(fntOff, fntLen, fnt.data())) return fail("file tables outside the ROM");
		fat_.resize(fatLen / 8);
		for (size_t i = 0; i < fat_.size(); i++)
		{
			fat_[i] = { rd32(&fat[i * 8]), rd32(&fat[i * 8 + 4]) };
			if (fat_[i].second < fat_[i].first || fat_[i].second > size_) return fail("a file outside the ROM");
		}
		// FNT: per directory (id 0xF000 + n) an 8-byte entry: its sub-table's offset and its first file's id; the
		// sub-table lists names: a length byte (bit 7: a directory, followed by its id), 0 ends it
		if (fntLen < 8) return fail("no file names");
		size_t dirs = rd16(&fnt[6]);
		if (dirs == 0 || dirs > 4096 || dirs * 8 > fntLen) return fail("broken file names");
		std::vector<std::pair<uint16_t, std::string>> todo = { { 0xf000, "" } };
		size_t visited = 0;
		while (!todo.empty())
		{
			auto [dir, prefix] = todo.back();
			todo.pop_back();
			size_t d = dir & 0xfff;
			if (d >= dirs || ++visited > dirs) return fail("broken file names");
			size_t p = rd32(&fnt[d * 8]);
			int id = rd16(&fnt[d * 8 + 4]);
			while (p < fntLen)
			{
				uint8_t len = fnt[p++];
				if (len == 0) break;
				size_t nl = len & 0x7f;
				if (p + nl > fntLen) return fail("broken file names");
				std::string name((const char*)&fnt[p], nl);
				p += nl;
				if (len & 0x80)
				{
					if (p + 2 > fntLen) return fail("broken file names");
					todo.push_back({ rd16(&fnt[p]), prefix + name + "/" });
					p += 2;
				}
				else names_[prefix + name] = id++;
			}
		}
		return true;
	}

	int Rom::fileId(const std::string& path) const
	{
		auto it = names_.find(path);
		return it == names_.end() ? -1 : it->second;
	}

	bool Rom::read(int id, Bytes& out) const
	{
		if (id < 0 || (size_t)id >= fat_.size()) return false;
		out.resize(fat_[id].second - fat_[id].first);
		return readAt(fat_[id].first, out.size(), out.data());
	}

	bool Rom::arm9(Bytes& out) const
	{
		uint32_t at = rd32(hdr_ + 0x20), len = rd32(hdr_ + 0x2c);
		if (at > size_ || len > size_ - at || len > MAX_UNPACKED) return false;
		Bytes raw(len);
		if (!readAt(at, raw.size(), raw.data())) return false;
		return arm9Unpack(raw, rd32(hdr_ + 0x28), out);
	}

	bool Rom::overlay9(int n, Bytes& out) const
	{
		// the overlay table: 32 bytes each (id, RAM address, sizes, static initialisers, file id, packed size | flags)
		uint32_t at = rd32(hdr_ + 0x50), len = rd32(hdr_ + 0x54);
		if (n < 0 || (uint32_t)(n + 1) * 32 > len) return false;
		uint8_t e[32];
		if (!readAt(at + (uint64_t)n * 32, 32, e)) return false;
		Bytes raw;
		if (!read((int)rd32(e + 24), raw)) return false;
		if (!(e[31] & 1)) { out = std::move(raw); return true; }
		return blzDecompress(raw, out);
	}

	// ---- NARC -----------------------------------------------------------------------------------------------------
	bool Narc::parse(const uint8_t* d, size_t n)
	{
		members_.clear();
		// NARC header, then BTAF (the members' start/end), BTNF (names, unused here), GMIF (the members' bytes)
		if (n < 16 || memcmp(d, "NARC", 4) != 0) return false;
		size_t p = rd16(d + 12);
		if (p + 12 > n || memcmp(d + p, "BTAF", 4) != 0) return false;
		size_t btafLen = rd32(d + p + 4), count = rd32(d + p + 8);
		if (btafLen < 12 || p + btafLen > n || count > (btafLen - 12) / 8) return false;
		size_t fat = p + 12;
		p += btafLen;
		if (p + 8 > n || memcmp(d + p, "BTNF", 4) != 0) return false;
		size_t btnfLen = rd32(d + p + 4);
		if (btnfLen < 8 || p + btnfLen > n) return false;
		p += btnfLen;
		if (p + 8 > n || memcmp(d + p, "GMIF", 4) != 0) return false;
		size_t img = p + 8;
		members_.resize(count);
		for (size_t i = 0; i < count; i++)
		{
			size_t s = rd32(d + fat + i * 8), e = rd32(d + fat + i * 8 + 4);
			if (e < s || img + e > n) { members_.clear(); return false; }
			members_[i] = { d + img + s, e - s };
		}
		return true;
	}

	// ---- BLZ --------------------------------------------------------------------------------------------------------
	bool blzDecompress(const Bytes& in, Bytes& out)
	{
		size_t n = in.size();
		if (n < 8) return false;
		uint32_t info = rd32(&in[n - 8]), grow = rd32(&in[n - 4]);
		if (grow == 0) { out = in; return true; }
		size_t hdr = info >> 24, packed = info & 0xffffff;
		if (hdr < 8 || hdr > packed || packed > n || grow > MAX_UNPACKED || n + grow > MAX_UNPACKED) return false;
		size_t total = n + grow;
		Bytes o(total);
		size_t limit = n - packed;
		memcpy(o.data(), in.data(), limit);
		size_t src = n - hdr, dst = total;
		while (src > limit)
		{
			uint8_t flags = in[--src];
			for (int bit = 0; bit < 8 && src > limit; bit++, flags <<= 1)
			{
				if (flags & 0x80)
				{
					if (src < limit + 2) return false;
					src -= 2;
					uint16_t v = (uint16_t)(in[src] | in[src + 1] << 8);
					size_t disp = (v & 0xfff) + 3, len = (v >> 12) + 3;
					if (len > dst - limit || dst - 1 + disp >= total) return false;	// (copies from what is already out)
					for (size_t k = 0; k < len; k++) { dst--; o[dst] = o[dst + disp]; }
				}
				else
				{
					if (dst <= limit) return false;
					o[--dst] = in[--src];
				}
			}
		}
		if (dst != limit) return false;
		out = std::move(o);
		return true;
	}

	bool arm9Unpack(const Bytes& arm9, uint32_t ramAddress, Bytes& out)
	{
		// the SDK's module parameters end with its nitro code (0xDEC00621, 0x2106C0DE); 0x14 into them: the RAM
		// address where the packed part ends (0: not packed)
		for (size_t i = 0x1c; i + 8 <= arm9.size(); i += 4)
		{
			if (rd32(&arm9[i]) != 0xdec00621 || rd32(&arm9[i + 4]) != 0x2106c0de) continue;
			uint32_t end = rd32(&arm9[i - 0x1c + 0x14]);
			if (end == 0) break;
			if (end < ramAddress || end - ramAddress > arm9.size()) return false;
			size_t at = end - ramAddress;
			Bytes head(arm9.begin(), arm9.begin() + at), unpacked;
			if (!blzDecompress(head, unpacked)) return false;
			unpacked.insert(unpacked.end(), arm9.begin() + at, arm9.end());
			out = std::move(unpacked);
			return true;
		}
		out = arm9;
		return true;
	}

	// ---- LZ77 (0x10 / 0x11) -------------------------------------------------------------------------------------------
	bool lzDecompress(const uint8_t* in, size_t n, Bytes& out)
	{
		if (n < 4 || (in[0] != 0x10 && in[0] != 0x11)) return false;
		bool ext = in[0] == 0x11;
		size_t size = in[1] | in[2] << 8 | in[3] << 16, p = 4;
		if (size == 0) { if (n < 8) return false; size = rd32(in + 4); p = 8; }
		if (size > MAX_UNPACKED) return false;
		Bytes o;
		o.reserve(size);
		while (o.size() < size)
		{
			if (p >= n) return false;
			uint8_t flags = in[p++];
			for (int bit = 0; bit < 8 && o.size() < size; bit++, flags <<= 1)
			{
				if (!(flags & 0x80)) { if (p >= n) return false; o.push_back(in[p++]); continue; }
				size_t len, disp;
				if (p + 2 > n) return false;
				uint8_t b0 = in[p], b1 = in[p + 1];
				if (!ext) { len = (b0 >> 4) + 3; disp = ((b0 & 15) << 8 | b1) + 1; p += 2; }
				else if (b0 >> 4 == 0) { if (p + 3 > n) return false; len = ((b0 & 15) << 4 | b1 >> 4) + 0x11; disp = ((b1 & 15) << 8 | in[p + 2]) + 1; p += 3; }
				else if (b0 >> 4 == 1) { if (p + 4 > n) return false; len = ((b0 & 15) << 12 | b1 << 4 | in[p + 2] >> 4) + 0x111; disp = ((in[p + 2] & 15) << 8 | in[p + 3]) + 1; p += 4; }
				else { len = (b0 >> 4) + 1; disp = ((b0 & 15) << 8 | b1) + 1; p += 2; }
				if (disp > o.size()) return false;
				for (size_t k = 0; k < len && o.size() < size; k++) o.push_back(o[o.size() - disp]);
			}
		}
		out = std::move(o);
		return true;
	}

	// ---- text -----------------------------------------------------------------------------------------------------
	// Gen IV's character set from 0x0001 (pret's pokeplatinum tools/msgenc/charmap.txt; 0x01E0/0x01E1 are the
	// games' "PK" "MN" glyphs, 0x01DE and 0x01E2 spaces). Japanese kana and full-width letters included; the kanji
	// (Japanese releases only) are not.
	static const char* const G4[] = {
		"", "　", "ぁ", "あ", "ぃ", "い", "ぅ", "う", "ぇ", "え", "ぉ", "お", "か", "が", "き", "ぎ",
		"く", "ぐ", "け", "げ", "こ", "ご", "さ", "ざ", "し", "じ", "す", "ず", "せ", "ぜ", "そ", "ぞ",
		"た", "だ", "ち", "ぢ", "っ", "つ", "づ", "て", "で", "と", "ど", "な", "に", "ぬ", "ね", "の",
		"は", "ば", "ぱ", "ひ", "び", "ぴ", "ふ", "ぶ", "ぷ", "へ", "べ", "ぺ", "ほ", "ぼ", "ぽ", "ま",
		"み", "む", "め", "も", "ゃ", "や", "ゅ", "ゆ", "ょ", "よ", "ら", "り", "る", "れ", "ろ", "わ",
		"を", "ん", "ァ", "ア", "ィ", "イ", "ゥ", "ウ", "ェ", "エ", "ォ", "オ", "カ", "ガ", "キ", "ギ",
		"ク", "グ", "ケ", "ゲ", "コ", "ゴ", "サ", "ザ", "シ", "ジ", "ス", "ズ", "セ", "ゼ", "ソ", "ゾ",
		"タ", "ダ", "チ", "ヂ", "ッ", "ツ", "ヅ", "テ", "デ", "ト", "ド", "ナ", "ニ", "ヌ", "ネ", "ノ",
		"ハ", "バ", "パ", "ヒ", "ビ", "ピ", "フ", "ブ", "プ", "ヘ", "ベ", "ペ", "ホ", "ボ", "ポ", "マ",
		"ミ", "ム", "メ", "モ", "ャ", "ヤ", "ュ", "ユ", "ョ", "ヨ", "ラ", "リ", "ル", "レ", "ロ", "ワ",
		"ヲ", "ン", "０", "１", "２", "３", "４", "５", "６", "７", "８", "９", "Ａ", "Ｂ", "Ｃ", "Ｄ",
		"Ｅ", "Ｆ", "Ｇ", "Ｈ", "Ｉ", "Ｊ", "Ｋ", "Ｌ", "Ｍ", "Ｎ", "Ｏ", "Ｐ", "Ｑ", "Ｒ", "Ｓ", "Ｔ",
		"Ｕ", "Ｖ", "Ｗ", "Ｘ", "Ｙ", "Ｚ", "ａ", "ｂ", "ｃ", "ｄ", "ｅ", "ｆ", "ｇ", "ｈ", "ｉ", "ｊ",
		"ｋ", "ｌ", "ｍ", "ｎ", "ｏ", "ｐ", "ｑ", "ｒ", "ｓ", "ｔ", "ｕ", "ｖ", "ｗ", "ｘ", "ｙ", "ｚ",
		"", "！", "？", "、", "。", "⋯", "・", "／", "「", "」", "『", "』", "（", "）", "㊚", "㊛",
		"＋", "－", "⊗", "⊘", "＝", "ｚ", "：", "；", "．", "，", "♤", "♧", "♡", "♢", "☆", "◎",
		"○", "□", "△", "◇", "＠", "♫", "％", "☼", "☔", "☰", "❄", "☋", "♔", "♕", "☊", "⇗",
		"⇘", "☾", "¥", "♈", "♉", "♊", "♋", "♌", "♍", "♎", "♏", "←", "↑", "↓", "→", "‣",
		"＆", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E",
		"F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U",
		"V", "W", "X", "Y", "Z", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k",
		"l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "À",
		"Á", "Â", "Ã", "Ä", "Å", "Æ", "Ç", "È", "É", "Ê", "Ë", "Ì", "Í", "Î", "Ï", "Ð",
		"Ñ", "Ò", "Ó", "Ô", "Õ", "Ö", "×", "Ø", "Ù", "Ú", "Û", "Ü", "Ý", "Þ", "ß", "à",
		"á", "â", "ã", "ä", "å", "æ", "ç", "è", "é", "ê", "ë", "ì", "í", "î", "ï", "ð",
		"ñ", "ò", "ó", "ô", "õ", "ö", "÷", "ø", "ù", "ú", "û", "ü", "ý", "þ", "ÿ", "Œ",
		"œ", "Ş", "ş", "ª", "º", "¹", "²", "³", "$", "¡", "¿", "!", "?", ",", ".", "…",
		"·", "/", "‘", "’", "“", "”", "„", "《", "》", "(", ")", "♂", "♀", "+", "-", "*",
		"#", "=", "&", "~", ":", ";", "♠", "♣", "♥", "♦", "★", "◉", "●", "■", "▲", "◆",
		"@", "♪", "%", "☀", "☁", "☂", "☃", "☺", "♚", "♛", "☹", "↗", "↘", "☽", " ", "⁴",
		"PK", "MN", " ", "", "", "", "", "", "°", "_", "＿",
	};

	static_assert(sizeof G4 / sizeof G4[0] == 0x1eb, "Gen IV characters 0x0000-0x01EA");

	const char* gen4Char(uint16_t code)
	{
		return code < sizeof G4 / sizeof G4[0] ? G4[code] : "";
	}

	static void utf8(std::string& s, uint32_t c)
	{
		if (c < 0x80) s += (char)c;
		else if (c < 0x800) { s += (char)(0xc0 | c >> 6); s += (char)(0x80 | (c & 0x3f)); }
		else if (c < 0x10000) { s += (char)(0xe0 | c >> 12); s += (char)(0x80 | (c >> 6 & 0x3f)); s += (char)(0x80 | (c & 0x3f)); }
		else { s += (char)(0xf0 | c >> 18); s += (char)(0x80 | (c >> 12 & 0x3f)); s += (char)(0x80 | (c >> 6 & 0x3f)); s += (char)(0x80 | (c & 0x3f)); }
	}

	// a string packed 9 bits a character after 0xF100: Gen IV packs into 15 bits of each word (trainer names), Gen V
	// into all 16; 0x1FF ends it
	static std::vector<uint16_t> unpack9(const std::vector<uint16_t>& c, int wordBits)
	{
		std::vector<uint16_t> o;
		uint32_t acc = 0;
		int bits = 0;
		for (size_t i = 1; i < c.size(); i++)
		{
			acc |= (uint32_t)(c[i] & ((1u << wordBits) - 1)) << bits;
			bits += wordBits;
			while (bits >= 9)
			{
				uint16_t v = acc & 0x1ff;
				acc >>= 9;
				bits -= 9;
				if (v == 0x1ff) return o;
				o.push_back(v);
			}
		}
		return o;
	}

	std::vector<std::string> gen4Text(const uint8_t* d, size_t n)
	{
		std::vector<std::string> out;
		if (n < 4) return out;
		uint16_t count = rd16(d), seed = rd16(d + 2);
		if (4 + (size_t)count * 8 > n) return out;
		out.resize(count);
		for (uint32_t i = 0; i < count; i++)
		{
			uint32_t k = (seed * 765u * (i + 1)) & 0xffff;
			k |= k << 16;
			uint32_t off = rd32(d + 4 + 8 * i) ^ k, len = rd32(d + 8 + 8 * i) ^ k;
			if (off > n || len > (n - off) / 2) continue;
			std::vector<uint16_t> c(len);
			uint16_t key = (uint16_t)((i + 1) * 596947u);
			for (uint32_t j = 0; j < len; j++) { c[j] = rd16(d + off + 2 * j) ^ key; key = (uint16_t)(key + 18749); }
			if (!c.empty() && c[0] == 0xf100) { c = unpack9(c, 15); c.push_back(0xffff); }
			std::string& s = out[i];
			for (size_t j = 0; j < c.size(); j++)
			{
				uint16_t v = c[j];
				if (v == 0xffff) break;
				if (v == 0xe000 || v == 0x25bc || v == 0x25bd) s += '\n';
				else if (v == 0xfffe) j += 2 + (j + 2 < c.size() ? c[j + 2] : 0);	// command, parameter count, parameters
				else s += gen4Char(v);
			}
		}
		return out;
	}

	std::vector<std::string> gen5Text(const uint8_t* d, size_t n)
	{
		std::vector<std::string> out;
		if (n < 16) return out;
		uint16_t sections = rd16(d), count = rd16(d + 2);
		if (sections == 0) return out;
		size_t sec = rd32(d + 12);
		if (sec + 4 + (size_t)count * 8 > n) return out;
		out.resize(count);
		for (size_t i = 0; i < count; i++)
		{
			const uint8_t* e = d + sec + 4 + 8 * i;
			size_t off = sec + rd32(e), len = rd16(e + 4);
			if (off > n || len > (n - off) / 2) continue;
			std::vector<uint16_t> c(len);
			for (size_t j = 0; j < len; j++) c[j] = rd16(d + off + 2 * j);
			// XORed from the end: the last character is the end mark 0xFFFF, which gives the key
			if (len)
			{
				uint16_t key = c[len - 1] ^ 0xffff;
				for (size_t j = len; j-- > 0;) { c[j] ^= key; key = (uint16_t)(key >> 3 | key << 13); }
			}
			if (!c.empty() && c[0] == 0xf100) { c = unpack9(c, 16); c.push_back(0xffff); }
			std::string& s = out[i];
			for (size_t j = 0; j < c.size(); j++)
			{
				uint32_t v = c[j];
				if (v == 0xffff) break;
				if (v == 0xfffe) s += '\n';
				else if (v == 0xf000)	// a command: its code, parameter count, parameters (0xBE00/0xBE01 scroll the box)
				{
					uint16_t cmd = j + 1 < c.size() ? c[j + 1] : 0;
					if (cmd == 0xbe00 || cmd == 0xbe01) s += '\n';
					j += 2 + (j + 2 < c.size() ? c[j + 2] : 0);
				}
				else if (v == 0x246d) s += "♂";
				else if (v == 0x246e) s += "♀";
				else if (v == 0x2486) s += "PK";
				else if (v == 0x2487) s += "MN";
				else if (v >= 0xd800 && v < 0xdc00 && j + 1 < c.size() && c[j + 1] >= 0xdc00 && c[j + 1] < 0xe000)
				{
					utf8(s, 0x10000 + ((v - 0xd800) << 10) + (c[j + 1] - 0xdc00));
					j++;
				}
				else if (v < 0xd800 || v >= 0xe000) utf8(s, v);
			}
		}
		return out;
	}

	// ---- SHA-1 (FIPS 180-4) -----------------------------------------------------------------------------------------
	static uint32_t rol(uint32_t v, int s) { return v << s | v >> (32 - s); }

	Sha1::Sha1() : h_{ 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 } {}

	void Sha1::block(const uint8_t* b)
	{
		uint32_t w[80];
		for (int i = 0; i < 16; i++) w[i] = (uint32_t)b[i * 4] << 24 | (uint32_t)b[i * 4 + 1] << 16 | (uint32_t)b[i * 4 + 2] << 8 | b[i * 4 + 3];
		for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
		uint32_t a = h_[0], bb = h_[1], c = h_[2], d = h_[3], e = h_[4];
		for (int i = 0; i < 80; i++)
		{
			uint32_t f, k;
			if (i < 20) { f = (bb & c) | (~bb & d); k = 0x5a827999; }
			else if (i < 40) { f = bb ^ c ^ d; k = 0x6ed9eba1; }
			else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8f1bbcdc; }
			else { f = bb ^ c ^ d; k = 0xca62c1d6; }
			uint32_t t = rol(a, 5) + f + e + k + w[i];
			e = d; d = c; c = rol(bb, 30); bb = a; a = t;
		}
		h_[0] += a; h_[1] += bb; h_[2] += c; h_[3] += d; h_[4] += e;
	}

	void Sha1::add(const uint8_t* d, size_t n)
	{
		total_ += n;
		while (n)
		{
			size_t take = std::min(n, 64 - used_);
			memcpy(buf_ + used_, d, take);
			used_ += take; d += take; n -= take;
			if (used_ == 64) { block(buf_); used_ = 0; }
		}
	}

	std::string Sha1::hex()
	{
		uint64_t bits = total_ * 8;
		uint8_t pad = 0x80;
		add(&pad, 1);
		uint8_t zero = 0;
		while (used_ != 56) add(&zero, 1);
		uint8_t len[8];
		for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
		add(len, 8);
		static const char* hexd = "0123456789abcdef";
		std::string s;
		for (uint32_t v : h_) for (int i = 28; i >= 0; i -= 4) s += hexd[v >> i & 15];
		return s;
	}

	std::string sha1Hex(const uint8_t* d, size_t n)
	{
		Sha1 h;
		if (n) h.add(d, n);
		return h.hex();
	}
}
