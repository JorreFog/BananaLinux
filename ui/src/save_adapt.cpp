// blank saves and a server's saves taken in (save_adapt.h). What each core writes before a game has saved, and the
// sizes of its saves, were measured in a chroot of the image and read from each core's source at the commit its
// package pins; the DS's from melonDS (BananaDS) and DeSmuME's footer, which DraStic writes too
#include "save_adapt.h"
#include "sync_core.h"
#include <cstdint>

namespace bsync
{
	std::string saveFolder(const std::string& rel)
	{
		if (rel.compare(0, 6, "saves/") != 0) return "";
		size_t s = rel.find('/', 6);
		return s == std::string::npos ? "" : rel.substr(6, s - 6);
	}

	namespace
	{
	bool allOf(const std::string& b, size_t at, size_t n, unsigned char c)
	{
		for (size_t i = at; i < at + n; i++) if ((unsigned char)b[i] != c) return false;
		return true;
	}
	// Mupen64Plus-Next's one save for every game: EEPROM 0x800, four controller paks of 0x8000, SRAM 0x8000, FlashRAM
	// 0x20000 (libretro/libretro_memory.h). Fresh, the cart's regions are 0xFF and each pak is format_mempak's: an ID
	// block (with a random serial, so not compared) and its backups, an index table of 123 free pages (0x0003 each,
	// its checksum at byte 1) with its backup, then zeros (mupen64plus-core device/controllers/paks/mempak.c)
	bool n64Blank(const std::string& b)
	{
		enum { EEP = 0x800, PAK = 0x8000, SRAM = 0x8000, FLASH = 0x20000 };
		if (b.size() != EEP + 4 * PAK + SRAM + FLASH) return false;
		if (!allOf(b, 0, EEP, 0xFF) || !allOf(b, EEP + 4 * PAK, SRAM + FLASH, 0xFF)) return false;
		for (int k = 0; k < 4; k++)
		{
			std::string p = b.substr(EEP + k * PAK, PAK);
			for (int blk : { 0, 2, 5, 7 }) if (!allOf(p, blk * 32, 32, 0)) return false;
			for (int blk : { 3, 4, 6 }) if (p.compare(blk * 32, 32, p, 32, 32) != 0) return false;
			std::string idx(256, '\0');
			for (int i = 5; i < 128; i++) idx[2 * i + 1] = 3;
			idx[1] = (char)(123 * 3);
			if (p.compare(256, 256, idx) != 0 || p.compare(512, 256, idx) != 0 || !allOf(p, 768, PAK - 768, 0)) return false;
		}
		return true;
	}
	}

	// frontend/libretro.c init_memcard: the "MC" header frame, 15 free directory frames, 20 unused broken-sector frames
	const std::string& pcsxBlankCard()
	{
		static const std::string card = [] {
			std::string m(131072, '\0');
			size_t o = 0;
			m[o++] = 'M'; m[o++] = 'C'; o += 0x7d; m[o++] = 0x0e;
			for (int i = 0; i < 15; i++) { m[o++] = (char)0xa0; o += 7; m[o++] = (char)0xff; m[o++] = (char)0xff; o += 0x75; m[o++] = (char)0xa0; }
			for (int i = 0; i < 20; i++) { for (int k = 0; k < 4; k++) m[o++] = (char)0xff; o += 4; m[o++] = (char)0xff; m[o++] = (char)0xff; o += 0x76; }
			return m;
		}();
		return card;
	}

	bool blankSave(const std::string& b, const std::string& folder)
	{
		if (b.empty() || allOf(b, 0, b.size(), 0x00) || allOf(b, 0, b.size(), 0xFF)) return true;
		// (the flat saves/ holds every RetroArch system's but the GBA's, so a save there is checked against each
		// core's fresh save: all one byte, or a structure whose size alone is the core's, nothing a game writes)
		bool flat = folder.empty();
		// Snes9x fills fresh SRAM with 0x60, a few listed games with 0x6B (memmap.cpp)
		if ((flat || folder == "Snes9x") && (allOf(b, 0, b.size(), 0x60) || allOf(b, 0, b.size(), 0x6B))) return true;
		if ((flat || folder == "PCSX-ReARMed") && b == pcsxBlankCard()) return true;
		if ((flat || folder == "Mupen64Plus-Next") && n64Blank(b)) return true;
		return false;
	}

	namespace
	{
	// the rules a system's saves are checked by: DS, GBA, cartridge RAM (Game Boy, NES, SNES, Mega Drive), a
	// PlayStation memory card, Mupen64Plus-Next's struct
	enum class Kind { None, DS, GBA, Cart, PSX, N64 };
	Kind kindOf(const std::string& s)
	{
		if (s == "nds") return Kind::DS;
		if (s == "gba") return Kind::GBA;
		if (s == "gb" || s == "gbc" || s == "nes" || s == "snes" || s == "megadrive") return Kind::Cart;
		if (s == "psx") return Kind::PSX;
		if (s == "n64") return Kind::N64;
		return Kind::None;
	}
	bool pow2(size_t n) { return n && !(n & (n - 1)); }
	uint32_t u32(const std::string& b, size_t at)
	{
		return (unsigned char)b[at] | (unsigned char)b[at + 1] << 8 | (unsigned char)b[at + 2] << 16 | (uint32_t)(unsigned char)b[at + 3] << 24;
	}
	// DeSmuME's footer off a DS save, as nds-saves cuts it: the cookie last, version 0 at 20 bytes from the end, and
	// at 36 the padded size, which is the file less the footer, 122 bytes (or 40, an older DraStic's, without the
	// line saying where to cut). No footer: as it is. false: a footer whose fields don't add up
	bool cutFooter(std::string& b)
	{
		size_t n = b.size();
		if (n <= 40 || b.compare(n - 16, 16, "|-DESMUME SAVE-|") != 0) return true;
		if (u32(b, n - 20) != 0) return false;
		long long p = u32(b, n - 36);
		if (p != (long long)n - 122 && p != (long long)n - 40) return false;
		b.resize((size_t)p);
		return true;
	}
	}

	bool wireOk(const std::string& system, const std::string& name, const std::string& own)
	{
		std::string e = extOf(name);
		switch (kindOf(system))
		{
		case Kind::DS: return e == "sav" || e == "dsv";
		case Kind::GBA: case Kind::Cart: return e == "srm" || e == "sav";
		case Kind::PSX: return e == "srm" || e == "mcd" || e == "mcr";
		case Kind::N64: return e == "srm";
		default: return e == lower(own) && e != "zip";
		}
	}

	long long maxSave(const std::string& s)
	{
		switch (kindOf(s))
		{
		case Kind::DS: return (8 << 20) + 122;
		case Kind::GBA: return (128 << 10) + 16;
		case Kind::PSX: return 131072;
		case Kind::N64: return 296960;
		case Kind::Cart: return s == "nes" ? 32 << 10 : s == "megadrive" ? 64 << 10 : 128 << 10;
		default: return -1;
		}
	}

	std::string adaptIn(const std::string& s, std::string& b, const std::string* local)
	{
		const char* size = "not a size this system's saves come in";
		const char* other = "another size than this handheld's save for the game";
		size_t n = b.size();
		bool same = !local || local->size() == n;
		switch (kindOf(s))
		{
		case Kind::DS:
			// (melonDS never refuses a size: it cuts the file short or pads it to the one its game list gives, so a
			// wrong one is stopped here)
			if (!cutFooter(b)) return "a DS save whose footer doesn't add up";
			n = b.size();
			if (!pow2(n) || n < 512 || n > (8u << 20)) return size;
			return !local || local->size() == n ? "" : other;
		case Kind::GBA:
		{
			// (gpSP, and this image's patched mGBA and BananaGBA, write every save as 128 KiB, upstream's mGBA on another
			// device the size the game reached, the same bytes in the same places. Standalone mGBA
			// also writes the game's clock, 16 bytes, after the save of a cartridge that has one, the Pokemon games'
			// 128 KiB coming out as 131088: left off, as the cores here keep no clock in the save and read it as they
			// would a save without one)
			auto gba = [](size_t k) { return k == 512 || k == 8 << 10 || k == 32 << 10 || k == 64 << 10 || k == 128 << 10; };
			if (!gba(n) && n > 16 && gba(n - 16)) b.resize(n -= 16);
			if (!gba(n)) return size;
			same = !local || local->size() == n;
			if (same || n == 128 << 10) return "";
			return local->size() == 128 << 10 && allOf(*local, n, local->size() - n, 0xFF) ? "" : other;
		}
		case Kind::Cart:
		{
			bool ok = s == "nes" ? n >= 128 && n <= 32 << 10
				: s == "snes" ? pow2(n) && n >= 1 << 10 && n <= 128 << 10
				: s == "megadrive" ? n % 2 == 0 && n > 0 && n <= 64 << 10
				// (Game Boy: the RAM sizes a cartridge's header can name, 2 to 128 KiB, and MBC2's 512 bytes. Gambatte, the
				// core here, writes 8, 32 or 128 KiB only (whole banks, MBC2's as a bank), but it reads a save of any of
				// these, so one another emulator made is taken in, and the card's own are all within them)
				: n == 512 || n == 2 << 10 || n == 8 << 10 || n == 32 << 10 || n == 64 << 10 || n == 128 << 10;
			if (!ok) return size;
			return same ? "" : other;
		}
		case Kind::PSX: return n == 131072 ? "" : size;
		case Kind::N64: return n == 296960 ? "" : size;
		default: return "";
		}
	}
}
