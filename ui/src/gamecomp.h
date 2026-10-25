#pragma once
// banana-ui: the game companion on the game panel (Settings > Other systems > Game companion): for a Game Boy Advance
// game that has a definition, the other screen shows seven pages about the game as it plays (Status, Detail, Battle,
// Map, Bag, Collection, Progress), in the Pokémon companion's look (pokemon.h), read from the game's memory through
// RetroArch's READ_CORE_MEMORY the same way. The Pokémon games keep the Pokémon companion.
//
// The companion idea is PokeDaisy's (https://github.com/lidor30/pokedaisy, by Lidor Itzhari).
//
// A game's definition is a JSON file in /usr/share/banana/companions/ (the format: docs/COMPANIONS.md): how to find the
// game (its ROM header's code and title, or the ROM's CRC-32 for hacks and translations), the values to read (address,
// type, post-processing, pointers, expressions over other values) and the pages' widgets. index.tsv in that folder
// lists every file's match keys, so a game's start reads one small file, not every definition.
//
// Reading: the values a page needs (the page shown, and every page's "when") are gathered into as few reads as
// possible (addresses within 512 bytes share one, up to 4 KB each), a set every 250 ms, the next only once the last
// was answered. A value behind a pointer is read at the address the pointer had in the last set; when the pointer has
// moved, the old value is kept and the set asked again at once. ROM addresses (name tables) come from the ROM file.
#include "expr.h"
#include "json.h"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace banana::gcomp
{
	// what a value is read as
	enum VType { U8, U16, U32, S8, S16, S32, BCD8, BCD16, BCD32, BOOL, TEXT, INDEX };	// (INDEX: a list's row number, read from nowhere)
	struct Mem
	{
		VType type = U8;
		int size = 1;					// bytes (text: len)
		uint32_t addr = 0;				// (ptr: unused)
		bool ptr = false;
		uint32_t ptrAddr = 0;
		int32_t ptrOff = 0;
		uint32_t mask = 0xffffffffu;
		int shift = 0;
		double mul = 1;
		long div = 1, add = 0;
		int term = 0;					// text: the end byte (-1 none)
		std::string charmap;			// text: an enum the bytes are looked up in ("" ASCII)
	};
	struct Var
	{
		std::string name;
		enum Kind { MEM, EXPR, CONST } kind = CONST;
		Mem m;
		Expr expr;
		std::vector<std::string> deps;	// the names an expression uses
		EV value;
	};
	// a number that is a literal or an expression (a var's name is an expression too)
	struct NumRef
	{
		bool set = false;
		double lit = 0;
		Expr expr;
		std::vector<std::string> deps;
	};
	struct Field { std::string name, label, enumName, format; Mem m; uint32_t off = 0; };
	struct Widget
	{
		enum Type { NUMBER, TEXT_W, BAR, FLAGS, COUNT, LIST, STATIC, GRID, KV } type = STATIC;
		std::string label, text, enumName, format;
		NumRef value, max, total, n;
		NumRef when;
		uint32_t color = 0;				// bar: 0 the accent
		std::vector<std::string> items;
		bool invert = false, bits = false;
		std::vector<std::pair<std::string, std::string>> rows;	// kv
		// list
		bool basePtr = false;
		uint32_t base = 0, ptrAddr = 0;
		int32_t ptrOff = 0;
		int stride = 0;
		std::vector<Field> fields;
		NumRef rowWhen;
		int listId = -1;
	};
	enum PageId { P_STATUS, P_DETAIL, P_BATTLE, P_MAP, P_BAG, P_COLLECTION, P_PROGRESS, NUM_PAGES };
	const char* pageIdName(int p);
	struct Page
	{
		std::string title, tab;		// (tab: the tab's own short name, "" none)
		NumRef when;
		bool reference = false;
		std::vector<Widget> widgets;
	};
	struct Def
	{
		std::string file, title, quality;
		uint32_t accent = 0xd0a030;
		std::vector<Var> vars;
		std::map<std::string, int> varIndex;
		std::map<std::string, std::map<long, std::string>> enums;
		Page pages[NUM_PAGES];
		int lists = 0;
		bool romData = false;			// a value or a table in the ROM: the ROM file is read
		const Var* var(const std::string& n) const { auto it = varIndex.find(n); return it == varIndex.end() ? nullptr : &vars[it->second]; }
		std::string enumLabel(const std::string& e, long v) const;
	};
	// a definition from its JSON (null and why in *why when it isn't one)
	std::shared_ptr<Def> parseDef(const JVal& j, std::string* why = nullptr);
	std::shared_ptr<Def> loadDef(const std::string& path, std::string* why = nullptr);

	// the running ROM: its header's code and title, its CRC-32 (from the zip's directory, or worked out for a .gba
	// when asked), its file name without folder and extension
	struct RomId { std::string code, title, base; uint32_t crc = 0; bool crcKnown = false; };
	bool romId(const std::string& path, RomId& id, bool computeCrc);
	// the definition file for a ROM in dir ("" none): index.tsv when there is one, else every *.json read
	std::string findDef(const std::string& dir, const std::string& rom, std::string* why = nullptr);
	// index.tsv's lines for the files in dir (the install's step; tests)
	std::string makeIndex(const std::string& dir);

	struct Val { bool ok = false; EV v; bool operator==(const Val& o) const { return ok == o.ok && (!ok || (v.isStr == o.v.isStr && v.n == o.v.n && v.s == o.v.s)); } };
	struct Snapshot
	{
		bool seen = false;				// a set came back with data (or the definition reads nothing)
		std::string error;				// RetroArch's reply when it had no data
		std::map<std::string, Val> vals;
		std::vector<std::vector<std::vector<Val>>> lists;	// by list id: rows of fields (rows whose rowWhen is false left out)
	};

	class Engine
	{
	public:
		// the game in rom: false (nothing to do) when no definition in dir matches it, or it is a Pokémon game the
		// Pokémon companion takes
		bool open(const std::string& rom, const std::string& dir = "/usr/share/banana/companions");
		bool openDef(std::shared_ptr<const Def> d, const std::string& rom = "");	// (tests)
		void close();
		bool active() const { return (bool)def; }
		const Def* game() const { return def.get(); }
		// the page the panel shows (its values are read; the others' only for their "when"), and the list opened
		void want(int page, int list = -1) { if (page != wantPage || list != wantList) { wantPage = page; wantList = list; nextAt = 0; } }
		std::string requests(double nowMs);	// RetroArch's stdin: a set every 250 ms (see pokemon.h)
		void feed(const char* data, size_t n);
		bool takeChanged() { bool c = changed; changed = false; return c; }
		const Snapshot& snapshot() const { return snap; }
		// a page's "when" now (no when: true; a value not read yet: false)
		bool pageOn(int page) const;
		// a number/text/... widget's value in the snapshot (ok false: not available)
		Val eval(const NumRef& r) const;
		Val evalIn(const NumRef& r, const Scope& sc, const std::map<std::string, Val>& vals) const;
		int reads() const { return lastReads; }	// how many READ_CORE_MEMORY the last set had (tests)
	private:
		std::shared_ptr<const Def> def;
		std::string romPath;
		struct RomShared { std::mutex mu; std::shared_ptr<const std::vector<uint8_t>> rom; bool started = false; };
		std::shared_ptr<RomShared> rs;
		int wantPage = P_STATUS, wantList = -1;
		double nextAt = 0, sentAt = 0;
		int waiting = 0, lastReads = 0;
		std::string part;
		Snapshot snap;
		std::string err;
		struct Chunk { uint32_t addr; int len; std::vector<uint8_t> bytes; bool done; };
		std::vector<Chunk> chunks;
		std::map<std::string, uint32_t> ptrSeen;	// a pointer's value in the last set (var name, or "#list<id>")
		std::map<std::string, uint32_t> ptrUsed;	// the target this set's read of it used
		bool changed = false;
		void startRom();
		std::shared_ptr<const std::vector<uint8_t>> romBytes() const;
		const uint8_t* at(uint32_t addr, int n) const;
		void finish();
		void neededVars(std::vector<bool>& need) const;
	};
}
