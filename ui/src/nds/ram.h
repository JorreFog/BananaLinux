#pragma once
// banana-ui: the DS Pokémon companion's RAM reader. It turns a running game's main RAM (read through BananaDS's
// companion export, CompLink, ~/banana-emu/common/COMPANION-SPEC.md) into model.h's Live: the party, the battle, the
// bag, the Pokédex, money, badges, play time, where the player is, roamers, the daycare and the story flags.
//
// Where everything is: ~/nds-companion/RESEARCH.md (its "Verified on device" section says which of it was checked on
// a running game on the handhelds, and on which). In short:
//  - Gen IV (Diamond, Pearl, Platinum, HeartGold, SoulSilver) keeps its whole save in one heap object; the pointer to
//    it is the first word of the static .bss + 0x20 (Pt 0x02101D40, DP 0x02106FC0, HGSS 0x02111880). Party, bag,
//    Pokédex ... are at PKHeX's save offsets from that object's body. The battle's own copies are at fixed distances
//    from it (the NDS Ironmon Tracker's).
//  - Black 2/White 2 keep a pointer at 0x02000024; the save image is 0x920 past it. Black/White keep theirs at a fixed
//    address (Black 0x0221BBAC, White 0x20 further); their battle data sits at fixed distances from it.
//  - Pokémon are the games' 136/220/236-byte structures: shuffled, encrypted blocks with a checksum. A Pokémon whose
//    checksum fails (the game is writing it) is not used: the last good reading of that slot stays until a good one.
//
// Found and checked, not trusted: those addresses are where the English releases keep things, the first guess. A
// guess is used once what it points at checks out - a trainer block and a party whose Pokémon pass the game's
// checksums - and another revision, language or a moved heap is found by looking: the pointer the ARM9's module
// parameters give (Gen IV), else a search of main RAM for a party that checks out, then for the static word pointing
// at its save. The battle's copies are found the same way when they don't match the party during a battle. What was
// found is kept per game + ROM (anchors(): the Session keeps it on the card) and tried first next time.
// "In a battle" is the battle overlay's code being in RAM (the games load it where the field's code was): its bytes
// from the player's own ROM (RamHints, rom.h ramHints()), else the known word in it.
//
// It only reads. Each read set is answered by the emulator between two frames, so everything in one set is from the
// same frame; pointers are read in the same set as the data behind them, and a set whose pointers moved is dropped
// (the next set asks at the new place).
#include "nds/model.h"
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace banana::nds
{
	struct Range { uint32_t addr = 0, len = 0; };

	// ---- the Pokémon structure (exposed for the tests) ------------------------------------------------------------
	// unshuffle + decrypt a 136 (boxed), 220 (Gen V party) or 236-byte (Gen IV party) Pokémon into out (same size,
	// blocks in A,B,C,D order); false when its checksum doesn't match (being written, or not a Pokémon)
	bool decryptMon(const uint8_t* raw, size_t n, uint8_t* out);
	// the reverse (tests: synthetic Pokémon); the checksum is computed and stored
	void encryptMon(const uint8_t* plain, size_t n, uint8_t* out);
	// a decrypted Pokémon as model.h's Mon (gen: the nickname's encoding and where the nature is); false: no species
	bool parseMon(Gen gen, const uint8_t* plain, size_t n, Mon& out);
	// a raw (encrypted) Pokémon checked and parsed in one; false: bad checksum, empty slot or out-of-range fields
	bool readMon(Gen gen, const uint8_t* raw, size_t n, Mon& out);
	// the games' text: Gen IV's own character codes / Gen V's UTF-16, up to 0xFFFF
	std::string monText(Gen gen, const uint8_t* p, int maxChars);
	// the time of day the games use for encounters (decomp: 4-9 morning, 10-19 day, 20-3 night) and Gen V's season
	TimeOfDay timeOfDay(int hour);
	uint8_t season(int month);	// 1..12 -> 0 spring, 1 summer, 2 autumn, 3 winter (Gen V: month - 1 mod 4)
	// Gen IV's type numbers (??? at 9) to model.h's Type; Gen V's are model.h's own
	Type typeFromGame(Gen gen, uint8_t t);

	// what the player's ROM tells the RAM reader (rom.h ramHints): bytes of the battle overlay's code that only it has
	// where it is loaded, so "they are in RAM" = a battle is on, in any revision
	struct RamHints { Range battleSig; std::vector<uint8_t> battleBytes; };

	class RamReader
	{
	public:
		explicit RamReader(const Game& g);
		uint32_t battleSearches() const { return battleScanCount; }	// searches for a battle's copies so far
		void setHints(const RamHints& h) { hints = h; }
		// where this game + ROM was found to keep things (a few lines of text; the Session keeps it on the card),
		// tried before the known addresses; changed() once a new place checked out
		std::string anchors() const;
		void setAnchors(const std::string& text);
		bool anchorsChanged() const { return anchorsDirty; }
		void anchorsSaved() { anchorsDirty = false; }
		// the save's base as used now (Gen IV: SaveData; B2W2: the 0x02000024 pointer's value; BW: the save image), the
		// pointer it came from (0: found directly), and whether it checked out (a party that passed)
		uint32_t saveBase() const { return base; }
		uint32_t savePointer() const { return cand().ptr; }
		bool saveChecked() const { return checked; }
		bool scanning() const { return mode != NORMAL; }
		// (tests, and /run/banana/nds-ramsearch on a handheld) as if no known place held: the save is searched for
		void searchFromScratch(double nowMs);
		int32_t battleOffset() const { return battleShift; }	// where the battle copies were found against the known place
		bool supported() const { return layout != nullptr; }
		// the ranges for the next set (<= 64, a few KB; more while a battle runs)
		std::vector<Range> wants() const;
		// the set's answers, one per range of the last wants(), in order (an empty one: not mapped); nowMs and the
		// wall clock the game's RTC follows (BananaDS's RTC is the handheld's clock). True when Live changed.
		bool deliver(const std::vector<std::vector<uint8_t>>& data, double nowMs, time_t wall);
		// the newest consistent reading (valid false until the first one checks out)
		std::shared_ptr<const Live> live() const { return cur; }
		uint64_t generation() const { return gen; }
		// why the last set was not used ("" when it was), for the log and the tests
		const std::string& lastProblem() const { return problem; }
		struct Layout;
	private:
		// a pointer's address, or the base itself; when it last stopped checking out
		struct Anchor { uint32_t ptr = 0, direct = 0; double failedAt = -1e12; };
		enum Mode { NORMAL, SCAN_SAVE, SCAN_PTR, SCAN_BATTLE };
		enum Verdict { GOOD, EMPTY, BUSY, BAD };		// party checked out / a save with no game yet / being written / not a save
		Game game;
		const Layout* layout = nullptr;
		RamHints hints;
		std::vector<Anchor> cands;	// the places to try, in order: what was found before, the known one, the module params'
		size_t ci = 0;				// the one in use
		Anchor cached;				// what checked out last (anchors())
		bool anchorsDirty = false, checked = false;
		int bad = 0;				// sets in a row the one in use didn't check out
		uint32_t base = 0;			// the save's base now (from the pointer, or the anchor itself)
		Mode mode = NORMAL;
		uint32_t scanAt = 0;		// the next address a search reads
		double lastScan = -1e12, lastBattleScan = -1e12;
		std::vector<std::pair<int, uint32_t>> found;	// a search's finds (score, address; SCAN_PTR: which save, address)
		std::vector<std::pair<int, uint32_t>> saves;	// SCAN_SAVE's saves (score, base), best first: SCAN_PTR looks for them
		std::vector<uint32_t> foundMons;				// SCAN_BATTLE: Gen IV battle Pokémon structures
		int32_t battleShift = 0, monShift = 0;	// where the battle copies / Gen IV's battle Pokémon were found, against the known
		int battleBad = 0;			// sets in a row in a battle whose copies didn't match the party
		int battleScans = 0;		// searches for its copies in this battle (2 at most: one that doesn't check out after
									// them isn't searched again - each is ~4 s of a core, Origin HeartGold 2026-11-04)
		uint32_t battleScanCount = 0;	// (all, for the tests and the log)
		bool paramsDone = false;	// (Gen IV: the module params' pointer looked for)
		uint32_t battlePtrs[4] = {};	// Gen V: the battler objects as last read (0: none)
		uint32_t battleMons[4] = {};	// ...and their Pokémon
		std::shared_ptr<const Live> cur = std::make_shared<Live>();
		uint64_t gen = 0;
		std::string problem;
		std::vector<Mon> lastParty;		// the last good Pokémon per slot (kept while a slot is being written)
		uint64_t lastHash = 0;			// of the last set used (an unchanged set changes nothing)
		Verdict verdict = BAD;
		const Anchor& cand() const { static const Anchor none; return ci < cands.size() ? cands[ci] : none; }
		// a save found by searching with no pointer to it, in a game that keeps one (Gen IV, B2W2): only a stand-in - the
		// games keep a copy of the save while loading it that stays as loaded (the Plus's HeartGold at its title)
		bool standIn(const Anchor& a) const;
		void addCand(Anchor a, bool front = false);
		void nextCand(double nowMs);
		void startScan(Mode m, double nowMs);
		std::vector<Range> scanWants() const;
		void scanDeliver(const std::vector<std::vector<uint8_t>>& d, double nowMs);
		bool battleOn(const std::vector<std::vector<uint8_t>>& d) const;
		bool parse(const std::vector<std::vector<uint8_t>>& d, Live& out, time_t wall);
		void battle4(const std::vector<std::vector<uint8_t>>& d, Live& out);
		void battle5(const std::vector<std::vector<uint8_t>>& d, Live& out);
	};

	// RamReader over CompLink's READ_CORE_MEMORY lines (what banana-ui's link speaks): request() gives the lines of the
	// next set, reply() takes the answer lines back (all of one set at once)
	std::string requestLines(const std::vector<Range>& r);
	bool replyBytes(const std::string& lines, const std::vector<Range>& r, std::vector<std::vector<uint8_t>>& out);
}
