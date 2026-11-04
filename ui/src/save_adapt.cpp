// blank saves (save_adapt.h). What each core writes before a game has saved was measured in a chroot of the image
// and read from each core's source at the commit its package pins
#include "save_adapt.h"

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
}
