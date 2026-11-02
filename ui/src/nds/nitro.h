#pragma once
// banana-ui: the DS ROM's containers, for the DS Pokémon companion's ROM reader (rom.*): the cartridge's file system
// (NitroFS: the header, FNT and FAT), the NARC archives the games keep their data in, the backward LZ that packs the
// ARM9 binary and its overlays, the games' text banks (Gen IV's scrambled ones and Gen V's), and a SHA-1 for the
// decode cache's key. Layouts: GBATEK "DS Cartridge Header" / "DS Cartridge NitroROM and NitroARC File Systems";
// pret's pokeplatinum src/message.c and src/string_gf.c for Gen IV text; Project Pokémon's notes for Gen V text.
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace banana::nds::nitro
{
	using Bytes = std::vector<uint8_t>;
	inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
	inline uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

	// a piece of a buffer someone else keeps alive
	struct Span
	{
		const uint8_t* p = nullptr;
		size_t n = 0;
		uint8_t u8(size_t o) const { return o < n ? p[o] : 0; }
		uint16_t u16(size_t o) const { return o + 2 <= n ? rd16(p + o) : 0; }
		uint32_t u32(size_t o) const { return o + 4 <= n ? rd32(p + o) : 0; }
	};

	// The cartridge image. Files are read when asked for (pread), never the whole ROM: Black 2 is 512 MiB and the
	// handhelds have 1 GiB. Safe to read from several threads at once.
	class Rom
	{
	public:
		Rom() = default;
		Rom(const Rom&) = delete;
		Rom& operator=(const Rom&) = delete;
		~Rom();
		bool open(const std::string& path, std::string* err = nullptr);
		bool openMemory(Bytes image, std::string* err = nullptr);	// (tests)
		const uint8_t* header() const { return hdr_; }			// the first 0x200 bytes
		int fileId(const std::string& path) const;				// "a/0/1/6"; -1: none
		size_t fileCount() const { return fat_.size(); }
		bool read(int id, Bytes& out) const;
		bool file(const std::string& path, Bytes& out) const { int id = fileId(path); return id >= 0 && read(id, out); }
		// the ARM9 binary as it runs: unpacked when the SDK's module parameters say it is packed
		bool arm9(Bytes& out) const;
		// ARM9 overlay n, unpacked when the overlay table says it is
		bool overlay9(int n, Bytes& out) const;
		const std::map<std::string, int>& paths() const { return names_; }
	private:
		bool load(std::string* err);
		bool readAt(uint64_t off, size_t n, uint8_t* dst) const;
		int fd_ = -1;
		Bytes mem_;
		uint64_t size_ = 0;
		uint8_t hdr_[0x200] = {};
		std::vector<std::pair<uint32_t, uint32_t>> fat_;	// start, end
		std::map<std::string, int> names_;
	};

	// A NARC archive's members as views into its bytes (which the caller keeps alive and unchanged)
	class Narc
	{
	public:
		bool parse(const uint8_t* data, size_t n);
		bool parse(const Bytes& b) { return parse(b.data(), b.size()); }
		size_t size() const { return members_.size(); }
		Span operator[](size_t i) const { return i < members_.size() ? members_[i] : Span{}; }
	private:
		std::vector<Span> members_;
	};

	// The DS SDK's backward LZ (BLZ): the footer's last 8 bytes say how much is packed and how much it grows.
	// Returns false on a broken stream (out untouched).
	bool blzDecompress(const Bytes& in, Bytes& out);
	// the ARM9 binary, unpacked if its module parameters (found by the SDK's nitro code) say so; ramAddress: header 0x28
	bool arm9Unpack(const Bytes& arm9, uint32_t ramAddress, Bytes& out);

	// The BIOS-style LZ77 (type 0x10) and its longer-run variant (0x11, which Gen V's sprites use); false: not one
	// of them or broken
	bool lzDecompress(const uint8_t* in, size_t n, Bytes& out);

	// Gen IV text bank (msg.narc's members): every entry, as UTF-8. Line breaks become '\n'; the games' commands
	// (colours, placeholders for names and numbers) are dropped.
	std::vector<std::string> gen4Text(const uint8_t* data, size_t n);
	// Gen V text bank: every entry of the first section (the language's own), as UTF-8
	std::vector<std::string> gen5Text(const uint8_t* data, size_t n);
	// one Gen IV character code as UTF-8 ("" for codes with no character)
	const char* gen4Char(uint16_t code);

	std::string sha1Hex(const uint8_t* data, size_t n);
	// (incremental, for hashing pieces read from a file)
	class Sha1
	{
	public:
		Sha1();
		void add(const uint8_t* data, size_t n);
		std::string hex();
	private:
		void block(const uint8_t* b);
		uint32_t h_[5];
		uint8_t buf_[64];
		size_t used_ = 0;
		uint64_t total_ = 0;
	};
}
