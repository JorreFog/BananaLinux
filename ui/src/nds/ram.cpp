// banana-ui: the DS Pokémon companion's RAM reader (ram.h). Layouts: ~/nds-companion/RESEARCH.md, checked on the
// handhelds (its "Verified on device" section).
#include "nds/ram.h"
#include "nds/nitro.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace banana::nds
{
	static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
	static uint32_t u32(const uint8_t* p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
	static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

	// ---- the Pokémon structure ------------------------------------------------------------------------------------
	// the order the four 32-byte blocks are stored in, by (PID >> 13) & 31 (24..31 wrap): ABCD, ABDC ... DCBA; each
	// entry is the letter (0 A .. 3 D) in each storage slot
	static const uint8_t SHUFFLE[24][4] = {
		{0,1,2,3}, {0,1,3,2}, {0,2,1,3}, {0,2,3,1}, {0,3,1,2}, {0,3,2,1}, {1,0,2,3}, {1,0,3,2}, {1,2,0,3}, {1,2,3,0},
		{1,3,0,2}, {1,3,2,0}, {2,0,1,3}, {2,0,3,1}, {2,1,0,3}, {2,1,3,0}, {2,3,0,1}, {2,3,1,0}, {3,0,1,2}, {3,0,2,1},
		{3,1,0,2}, {3,1,2,0}, {3,2,0,1}, {3,2,1,0} };

	// the games' stream cipher (a linear congruential generator's high halves XORed on each 16-bit word)
	static void lcrng(uint8_t* p, size_t words, uint32_t seed)
	{
		for (size_t i = 0; i < words; i++)
		{
			seed = seed * 0x41C64E6Du + 0x6073u;
			put16(p + 2 * i, (uint16_t)(u16(p + 2 * i) ^ (seed >> 16)));
		}
	}

	bool decryptMon(const uint8_t* raw, size_t n, uint8_t* out)
	{
		if (n != 136 && n != 220 && n != 236) return false;
		memcpy(out, raw, n);
		const uint32_t pid = u32(raw);
		const uint16_t flags = u16(raw + 4), sum = u16(raw + 6);
		// the "decrypted in place" flags (bit 0 party, bit 1 box: the game is working on it): then it is plain now
		if (!(flags & 3)) lcrng(out + 8, 64, sum);
		if (n > 136 && !(flags & 1)) lcrng(out + 136, (n - 136) / 2, pid);
		uint16_t s = 0;
		for (int i = 0; i < 64; i++) s = (uint16_t)(s + u16(out + 8 + 2 * i));
		if (s != sum) return false;
		uint8_t blocks[128];
		memcpy(blocks, out + 8, 128);
		const uint8_t* order = SHUFFLE[((pid >> 13) & 31) % 24];
		for (int k = 0; k < 4; k++) memcpy(out + 8 + 32 * order[k], blocks + 32 * k, 32);
		return true;
	}

	void encryptMon(const uint8_t* plain, size_t n, uint8_t* out)
	{
		memcpy(out, plain, n);
		const uint32_t pid = u32(plain);
		uint16_t s = 0;
		for (int i = 0; i < 64; i++) s = (uint16_t)(s + u16(plain + 8 + 2 * i));
		put16(out + 6, s);
		put16(out + 4, 0);
		const uint8_t* order = SHUFFLE[((pid >> 13) & 31) % 24];
		for (int k = 0; k < 4; k++) memcpy(out + 8 + 32 * k, plain + 8 + 32 * order[k], 32);
		lcrng(out + 8, 64, s);
		if (n > 136) lcrng(out + 136, (n - 136) / 2, pid);
	}

	static void utf8(std::string& s, uint32_t c)
	{
		if (c < 0x80) s += (char)c;
		else if (c < 0x800) { s += (char)(0xC0 | c >> 6); s += (char)(0x80 | (c & 0x3F)); }
		else { s += (char)(0xE0 | c >> 12); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
	}

	std::string monText(Gen gen, const uint8_t* p, int maxChars)
	{
		std::string s;
		for (int i = 0; i < maxChars; i++)
		{
			const uint16_t c = u16(p + 2 * i);
			if (c == 0xFFFF) break;
			if (gen == Gen::IV) { s += nitro::gen4Char(c); continue; }
			// Gen V: UTF-16, with the games' own codes for the gender signs in names (PKHeX StringConverter5)
			if (c == 0x246D) s += "♂";
			else if (c == 0x246E) s += "♀";
			else if (c >= 0xD800 && c <= 0xDFFF) continue;
			else if (c) utf8(s, c);
		}
		return s;
	}

	bool parseMon(Gen gen, const uint8_t* d, size_t n, Mon& m)
	{
		m = Mon{};
		m.pid = u32(d);
		m.species = u16(d + 0x08);
		if (!m.species || m.species > (gen == Gen::IV ? 493 : 649)) return false;
		m.item = u16(d + 0x0A);
		m.otId = u16(d + 0x0C);
		m.otSecret = u16(d + 0x0E);
		m.exp = u32(d + 0x10);
		m.friendship = d[0x14];
		m.ability = d[0x15];
		for (int i = 0; i < 6; i++) m.evs[i] = d[0x18 + i];
		for (int i = 0; i < 4; i++)
		{
			m.moves[i] = u16(d + 0x28 + 2 * i);
			m.pp[i] = d[0x30 + i];
			m.ppUps[i] = d[0x34 + i];
			if (m.moves[i] >= 1000) return false;
		}
		const uint32_t iv = u32(d + 0x38);
		for (int i = 0; i < 6; i++) m.ivs[i] = (uint8_t)((iv >> (5 * i)) & 31);
		m.egg = (iv >> 30) & 1;
		m.female = ((d[0x40] >> 1) & 3) == 1;
		m.form = d[0x40] >> 3;
		m.nature = gen == Gen::V ? d[0x41] : (uint8_t)(m.pid % 25);
		if (m.nature >= 25) return false;
		m.nick = monText(gen, d + 0x48, 11);
		m.otName = monText(gen, d + 0x68, 8);
		// met place: Platinum/HGSS write theirs at 0x46 (DP's field then says "faraway place")
		m.metLocation = gen == Gen::IV && u16(d + 0x46) ? u16(d + 0x46) : u16(d + 0x80);
		m.pokerus = d[0x82];
		m.ball = gen == Gen::IV && d[0x86] ? d[0x86] : d[0x83];	// (HGSS's Apricorn balls at 0x86)
		m.metLevel = d[0x84] & 0x7F;
		if (n > 136)
		{
			m.status = u32(d + 0x88);
			m.level = d[0x8C];
			m.hp = u16(d + 0x8E);
			m.maxHp = u16(d + 0x90);
			for (int i = 0; i < 5; i++) m.stats[i] = u16(d + 0x92 + 2 * i);
			if (m.level < 1 || m.level > 100) return false;
			if (!m.egg && (m.maxHp == 0 || m.hp > m.maxHp)) return false;
		}
		return true;
	}

	bool readMon(Gen gen, const uint8_t* raw, size_t n, Mon& out)
	{
		uint8_t plain[236];
		if (n > sizeof plain || (!u32(raw) && !u16(raw + 6))) return false;	// (an empty slot: all zero)
		if (!decryptMon(raw, n, plain)) return false;
		return parseMon(gen, plain, n, out);
	}

	TimeOfDay timeOfDay(int h)
	{
		if (h >= 4 && h <= 9) return TimeOfDay::MORNING;
		if (h >= 10 && h <= 19) return TimeOfDay::DAY;
		return TimeOfDay::NIGHT;
	}
	uint8_t season(int month) { return (uint8_t)((month + 11) % 4); }

	Type typeFromGame(Gen gen, uint8_t t)
	{
		if (gen == Gen::IV)
		{
			if (t == 9) return NO_TYPE;		// ???
			if (t > 9) t--;
		}
		return t < NUM_TYPES ? (Type)t : NO_TYPE;
	}
}

namespace banana::nds
{
	// ---- where each game keeps things ---------------------------------------------------------------------------
	struct PocketAt { uint8_t pocket; uint16_t offset, slots; };
	struct RamReader::Layout
	{
		Gen gen;
		// Gen IV: the SaveData pointer's address, its header (the body's start), the battle's offsets from SaveData.
		// Gen V: B2W2's pointer (0x02000024) and the save image's distance from it, or BW's fixed save image (the
		// known places: the first guess, see RamReader)
		uint32_t ptr = 0, head = 0, fixedBase = 0;
		// general-block / save-image offsets (from the body / save image)
		uint32_t name, tid, money, badges, kanto, time, party, partyCount, bag, map, dex, daycare, roamers;
		uint8_t nRoamers;
		uint32_t repel, flags, flagBytes;
		std::vector<PocketAt> pockets;
		// battle: Gen IV from SaveData: our party's battle copy, the foe's party, BattleMon[0], the trainer id; the
		// absolute "in battle" word. Gen V (from the B2W2 pointer, or BW's save image): the same + the battler table
		uint32_t bOurs, bFoes, bMon0, bTrainer, inBattle, bTable, bDouble;
		uint32_t posMap, posX, posZ;	// Gen V save position block (map, x, z: as saved, not live)
		uint32_t liveZone, livePos;		// Gen V: the current zone (Ironmon's childMapHeader) and the player's map object
										// x (+0, z at +4): from the B2W2 pointer / BW's save image
	};

	static const std::vector<PocketAt> POCKETS_DP = { { POCKET_ITEMS, 0, 165 }, { POCKET_KEY, 0x294, 50 },
		{ POCKET_TMS, 0x35C, 100 }, { POCKET_MAIL, 0x4EC, 12 }, { POCKET_MEDICINE, 0x51C, 40 },
		{ POCKET_BERRIES, 0x5BC, 64 }, { POCKET_BALLS, 0x6BC, 15 }, { POCKET_BATTLE, 0x6F8, 30 } };
	static const std::vector<PocketAt> POCKETS_HGSS = { { POCKET_ITEMS, 0, 165 }, { POCKET_KEY, 0x294, 50 },
		{ POCKET_TMS, 0x35C, 101 }, { POCKET_MAIL, 0x4F0, 12 }, { POCKET_MEDICINE, 0x520, 40 },
		{ POCKET_BERRIES, 0x5C0, 64 }, { POCKET_BALLS, 0x6C0, 24 }, { POCKET_BATTLE, 0x720, 30 } };
	static const std::vector<PocketAt> POCKETS_V = { { POCKET_ITEMS, 0, 310 }, { POCKET_KEY, 0x4D8, 83 },
		{ POCKET_TMS, 0x624, 109 }, { POCKET_MEDICINE, 0x7D8, 48 }, { POCKET_BERRIES, 0x898, 64 } };

	static RamReader::Layout gen4(uint32_t ptr, uint32_t head, bool hgss, bool dp)
	{
		RamReader::Layout l{};
		l.gen = Gen::IV; l.ptr = ptr; l.head = head;
		if (dp)
		{
			l.name = 0x64; l.tid = 0x74; l.money = 0x78; l.badges = 0x7E; l.time = 0x86; l.partyCount = 0x94; l.party = 0x98;
			l.bag = 0x624; l.map = 0x1238; l.dex = 0x12DC; l.daycare = 0x141C; l.roamers = 0x73A0; l.nRoamers = 3;
			l.repel = 0x7438; l.flags = 0xFDC;
			l.bOurs = 0x4C7D8; l.bFoes = 0x4CD88; l.bMon0 = 0x485E8 - 0x68; l.bTrainer = 0x42A8E; l.inBattle = 0x0223BB38;
		}
		else if (!hgss)
		{
			l.name = 0x68; l.tid = 0x78; l.money = 0x7C; l.badges = 0x82; l.time = 0x8A; l.partyCount = 0x9C; l.party = 0xA0;
			l.bag = 0x630; l.map = 0x1280; l.dex = 0x1328; l.daycare = 0x1654; l.roamers = 0x7FF4; l.nRoamers = 6;
			l.repel = 0x8073; l.flags = 0xFEC;
			l.bOurs = 0x4B8AC; l.bFoes = 0x4BE5C; l.bMon0 = 0x47620 - 0x68; l.bTrainer = 0x4189E; l.inBattle = 0x0224A55A;
		}
		else
		{
			l.name = 0x64; l.tid = 0x74; l.money = 0x78; l.badges = 0x7E; l.kanto = 0x83; l.time = 0x86; l.partyCount = 0x94;
			l.party = 0x98; l.bag = 0x644; l.map = 0x1234; l.dex = 0x12B8; l.daycare = 0x15FC; l.roamers = 0x68B4;
			l.nRoamers = 4; l.repel = 0x6909; l.flags = 0x10C4;
			l.bOurs = 0x4EA98; l.bFoes = 0x4F068; l.bMon0 = 0x49E7C - 0x68; l.bTrainer = 0x440AA; l.inBattle = 0x02246F48;
		}
		l.flagBytes = 0xB60 / 8;
		l.pockets = hgss ? POCKETS_HGSS : POCKETS_DP;
		return l;
	}
	static RamReader::Layout gen5(bool b2w2, uint32_t shift)
	{
		RamReader::Layout l{};
		l.gen = Gen::V;
		if (b2w2) { l.ptr = 0x02000024; l.head = 0x920; }
		else l.fixedBase = 0x0221BBAC + shift;
		l.bag = 0x18400; l.partyCount = 0x18E04; l.party = 0x18E08;
		l.name = 0x19404; l.tid = 0x19414; l.time = 0x19424;
		l.posMap = 0x19580; l.posX = 0x19586; l.posZ = 0x1958A;
		l.money = b2w2 ? 0x21100 : 0x21200; l.badges = l.money + 4;
		l.dex = b2w2 ? 0x21400 : 0x21600;
		l.daycare = b2w2 ? 0x20D00 : 0x20E00;
		l.roamers = (b2w2 ? 0x21900 : 0x21B00) + 4; l.nRoamers = 2;
		l.repel = b2w2 ? 0x2192D : 0x21B31;
		l.flags = b2w2 ? 0x1FF00 + 0x1AF * 2 : 0x20100 + 0x13E * 2;
		l.flagBytes = (b2w2 ? 0xBF8 : 0xB60) / 8;
		l.pockets = POCKETS_V;
		// where the player is now (the save's position block keeps the saved spot): the zone and the player's map
		// object's tile x/z (Black 2: x went 110 -> 111 with a step, the save's stayed)
		// (BW: Ironmon's absolute addresses, Black's; White's are all 0x20 further, its save image too: so they are kept
		// as distances from the save image, which moves them with it when it is found elsewhere)
		const uint32_t bw = 0x0221BBAC;
		l.liveZone = b2w2 ? 0x41B5C : 0x022592B2 - bw;
		l.livePos = b2w2 ? 0x38CE8 + 0x2E : 0x022521EC + 0x2E - bw;
		if (b2w2)
		{	// from the 0x02000024 pointer (Ironmon's B2W2 offsets); the "in battle" word is absolute (+0x40 in White 2)
			l.bOurs = 0x53610; l.bFoes = 0x53B70; l.bTrainer = 0x5262E; l.bTable = 0x526A8; l.bDouble = 0x900A0;
			l.inBattle = 0x021B5138 + shift;
		}
		else
		{	// from the save image (Black's absolute ones less its image's address); the word is absolute
			l.bOurs = 0x0226A794 - bw; l.bFoes = 0x0226B254 - bw; l.bTrainer = 0x022697BE - bw;
			l.bTable = 0x02269838 - bw; l.bDouble = 0x022A62F8 - bw; l.inBattle = 0x021D0798 + shift;
		}
		return l;
	}
	static const RamReader::Layout L_DP = gen4(0x02106FC0, 0x214, false, true);
	static const RamReader::Layout L_PT = gen4(0x02101D40, 0x14, false, false);
	static const RamReader::Layout L_HGSS = gen4(0x02111880, 0x10, true, false);
	static const RamReader::Layout L_B = gen5(false, 0), L_W = gen5(false, 0x20);
	static const RamReader::Layout L_B2 = gen5(true, 0), L_W2 = gen5(true, 0x40);
}

namespace banana::nds
{
	static const uint32_t RAM_LO = 0x02000000, RAM_HI = 0x02400000;	// main RAM in DS mode (BananaDS's 4 MB export)
	static bool inRam(uint32_t a, uint32_t len = 1) { return a >= RAM_LO && a + len <= RAM_HI && a + len > a; }
	static const uint32_t BMON = 0xC0, BATTLER = 0x140;	// Gen IV BattleMon; the part of a Gen V battler object read
	// a search reads main RAM in pieces of 64 KB, four a set (with a little of each side, so a structure across a
	// piece's edge is seen whole): 16 sets for the 4 MB, ~4 s at the Session's pace, only when a guess failed
	static const uint32_t SCAN_PIECE = 0x10000, SCAN_PIECES = 4, SCAN_LEAD = 0x100, SCAN_TAIL = 0x800;
	// how long a guess may not check out before the next is tried (sets, 4 a second): a wrong place shows it at once,
	// the right one only while the game rebuilds its heap (a reset) or writes a Pokémon; a pointer that holds no
	// address yet (the game's first seconds); a save with no game in it yet (the title, a new game's intro)
	static const int BAD_SETS = 8, NO_POINTER_SETS = 40, EMPTY_SETS = 240;
	// the searches' pace: after one found nothing, the next no sooner than this (ms)
	static const double SCAN_AGAIN_MS = 60000, SCAN_AGAIN_EMPTY_MS = 300000, BATTLE_SCAN_AGAIN_MS = 30000;
	static const int BATTLE_BAD_SETS = 40;	// (10 s: a battle's first seconds - its intro - may come before its copies)

	RamReader::RamReader(const Game& g) : game(g)
	{
		switch (g.id)
		{
		case GameId::DIAMOND: case GameId::PEARL: layout = &L_DP; break;
		case GameId::PLATINUM: layout = &L_PT; break;
		case GameId::HEARTGOLD: case GameId::SOULSILVER: layout = &L_HGSS; break;
		case GameId::BLACK: layout = &L_B; break;
		case GameId::WHITE: layout = &L_W; break;
		case GameId::BLACK2: layout = &L_B2; break;
		case GameId::WHITE2: layout = &L_W2; break;
		default: break;
		}
		// (any revision: the known places are only the first guess, checked before use. Platinum Rev 1, which this
		// refused before, has Rev 0's ARM9 and overlays byte for byte but overlay 94, so the same RAM layout)
		if (layout) addCand(layout->ptr ? Anchor{ layout->ptr, 0 } : Anchor{ 0, layout->fixedBase });
	}

	bool RamReader::standIn(const Anchor& a) const { return !a.ptr && layout && layout->ptr; }

	void RamReader::addCand(Anchor a, bool front)
	{
		for (size_t i = 0; i < cands.size(); i++)
			if (cands[i].ptr == a.ptr && cands[i].direct == a.direct)
			{
				if (!front || i == 0) return;
				cands.erase(cands.begin() + i);
				if (ci >= i && ci > 0) ci--;
				break;
			}
		if (front) { cands.insert(cands.begin(), a); ci = 0; }
		else cands.push_back(a);
	}

	std::string RamReader::anchors() const
	{
		char b[160];
		snprintf(b, sizeof b, "game %s\nptr %x\ndirect %x\nbattle %d\nmon %d\n", game.code.c_str(), cached.ptr, cached.direct,
			(int)battleShift, (int)monShift);
		return b;
	}

	void RamReader::setAnchors(const std::string& text)
	{
		Anchor a;
		int bs = 0, ms = 0;
		std::string code;
		char key[16];
		unsigned v;
		int iv;
		for (size_t at = 0; at < text.size();)
		{
			size_t nl = text.find('\n', at);
			if (nl == std::string::npos) nl = text.size();
			const std::string ln = text.substr(at, nl - at);
			at = nl + 1;
			char c[8] = {};
			if (sscanf(ln.c_str(), "game %7s", c) == 1) code = c;
			else if (sscanf(ln.c_str(), "%15s %x", key, &v) == 2 && (!strcmp(key, "ptr") || !strcmp(key, "direct")))
				(key[0] == 'p' ? a.ptr : a.direct) = v;
			if (sscanf(ln.c_str(), "battle %d", &iv) == 1) bs = iv;
			if (sscanf(ln.c_str(), "mon %d", &iv) == 1) ms = iv;
		}
		if (!layout || code != game.code) return;	// (another game's: not used)
		// (shifts larger than main RAM are not a place)
		if (bs > -0x400000 && bs < 0x400000) battleShift = bs;
		if (ms > -0x400000 && ms < 0x400000) monShift = ms;
		// (a save with no pointer is kept only for a game without one, BW: elsewhere it was a stand-in)
		if ((a.ptr && inRam(a.ptr, 4)) || (!a.ptr && !layout->ptr && inRam(a.direct)))
		{
			cached = a;
			addCand(a, true);
			ci = 0;
		}
	}

	// the general part of the save read in one piece (Gen IV: from the body to past the daycare; Gen V: the bag to
	// the position, and the money to the roamers)
	static uint32_t gen4Span(const RamReader::Layout& l)
	{
		uint32_t e = l.daycare + 2 * 236 + 8;
		auto mx = [&](uint32_t v) { if (v > e) e = v; };
		mx(l.dex + 4 + 4 * 0x40); mx(l.flags + l.flagBytes); mx(l.map + 4); mx(l.party + 6 * 236);
		for (auto& p : l.pockets) mx(l.bag + p.offset + 4u * p.slots);
		return e;
	}

	// the ranges, in this order (deliver() reads them by these indexes)
	enum { W_PTR, W_MAIN, W_ROAM, W_REPEL, W_OURS, W_FOES, W_BMON, W_TRAINER, W_INBATTLE, W_V_MISC, W_V_DAYCARE,
		W_V_FLAGS, W_V_TABLE, W_V_DOUBLE, W_V_ZONE, W_V_POS, W_PARAMS, W_SIG, W_C0, W_C1, W_C2, W_C3, W_COUNT };
	static const size_t PEEK = 4;	// the other pointers read in each set (W_C0..): one that holds an address is tried at once
	// (Gen IV: where the ARM9's module parameters are looked for - 0x02000B68 DP, 0x02000BA0 Pt/HGSS)
	static const uint32_t PARAMS_AT = 0x02000800, PARAMS_LEN = 0x800;

	std::vector<Range> RamReader::wants() const
	{
		if (!layout) return {};
		if (mode != NORMAL) return scanWants();
		std::vector<Range> w(W_COUNT);
		const Layout& l = *layout;
		const Anchor& a = cand();
		if (a.ptr) w[W_PTR] = { a.ptr, 4 };
		for (size_t i = 0; i < PEEK && i < cands.size(); i++) if (cands[i].ptr) w[W_C0 + i] = { cands[i].ptr, 4 };
		if (l.gen == Gen::IV && !paramsDone) w[W_PARAMS] = { PARAMS_AT, PARAMS_LEN };
		if (!hints.battleBytes.empty() && inRam(hints.battleSig.addr, hints.battleSig.len)) w[W_SIG] = hints.battleSig;
		w[W_INBATTLE] = { l.inBattle, 2 };
		const uint32_t s = a.ptr ? base : a.direct;	// Gen IV: SaveData; B2W2: the pointer's value; BW: the save image
		if (!inRam(s)) return w;
		if (l.gen == Gen::IV)
		{
			const uint32_t b = s + l.head;
			w[W_MAIN] = { b, gen4Span(l) };
			w[W_ROAM] = { b + l.roamers, 0x14u * l.nRoamers };
			w[W_REPEL] = { b + l.repel, 1 };
			w[W_OURS] = { s + l.bOurs + battleShift, 6 * 236 };
			w[W_FOES] = { s + l.bFoes + battleShift, 6 * 236 };
			w[W_BMON] = { s + l.bMon0 + monShift, 4 * BMON };
			w[W_TRAINER] = { s + l.bTrainer + battleShift, 2 };
			return w;
		}
		const uint32_t v = s + l.head;	// the save image (BW: head 0)
		const uint32_t rel = s;			// the battle's and the field's offsets: from the pointer (B2W2) or the image (BW)
		w[W_MAIN] = { v + l.bag, l.posZ + 2 - l.bag };
		w[W_V_MISC] = { v + l.money, l.roamers + 0x14u * l.nRoamers - l.money };
		w[W_V_DAYCARE] = { v + l.daycare, 0x1D0 };
		w[W_V_FLAGS] = { v + l.flags, l.flagBytes };
		w[W_REPEL] = { v + l.repel, 1 };
		w[W_OURS] = { rel + l.bOurs + battleShift, 6 * 220 };
		w[W_FOES] = { rel + l.bFoes + battleShift, 6 * 220 };
		w[W_TRAINER] = { rel + l.bTrainer + battleShift, 2 };
		w[W_V_TABLE] = { rel + l.bTable + battleShift, 4 * 0x1C };
		w[W_V_DOUBLE] = { rel + l.bDouble + battleShift, 1 };
		w[W_V_ZONE] = { rel + l.liveZone, 2 };
		w[W_V_POS] = { rel + l.livePos, 6 };
		// the battler objects where the last table pointed, and the Pokémon they pointed at (checked against this
		// set's own table and objects before they are used)
		for (int i = 0; i < 4; i++)
		{
			if (inRam(battlePtrs[i], BATTLER)) w.push_back({ battlePtrs[i], BATTLER });
			else w.push_back({});
			if (inRam(battleMons[i], 220)) w.push_back({ battleMons[i], 220 });
			else w.push_back({});
		}
		return w;
	}
}

namespace banana::nds
{
	static uint64_t fnv(uint64_t h, const std::vector<uint8_t>& v)
	{
		for (uint8_t c : v) h = (h ^ c) * 1099511628211ull;
		return h;
	}
	static std::vector<uint8_t> bits(const uint8_t* p, int count)	// the first `count` bits, the rest cleared
	{
		std::vector<uint8_t> v(p, p + (count + 7) / 8);
		if (count % 8) v.back() &= (uint8_t)((1u << (count % 8)) - 1);
		return v;
	}
	static int8_t stage(uint8_t raw) { return raw <= 12 ? (int8_t)(raw - 6) : 0; }

	// the next place to try; all tried: a search (no sooner than a minute after the last that found nothing)
	void RamReader::nextCand(double nowMs)
	{
		bad = 0;
		checked = false;
		base = 0;
		memset(battlePtrs, 0, sizeof battlePtrs);
		memset(battleMons, 0, sizeof battleMons);
		if (ci < cands.size()) cands[ci].failedAt = nowMs;
		if (++ci < cands.size()) return;
		ci = 0;
		if (nowMs - lastScan >= (verdict == EMPTY ? SCAN_AGAIN_EMPTY_MS : SCAN_AGAIN_MS)) startScan(SCAN_SAVE, nowMs);
	}

	void RamReader::searchFromScratch(double nowMs)
	{
		if (!layout) return;
		cands.clear();
		ci = 0; bad = 0; base = 0; checked = false;
		cached = Anchor{};
		battleShift = monShift = 0;
		paramsDone = true;
		startScan(SCAN_SAVE, nowMs);
	}

	void RamReader::startScan(Mode m, double nowMs)
	{
		mode = m;
		scanAt = RAM_LO;
		found.clear();
		foundMons.clear();
		if (m == SCAN_BATTLE) lastBattleScan = nowMs;
		else if (m == SCAN_SAVE) lastScan = nowMs;
	}

	std::vector<Range> RamReader::scanWants() const
	{
		std::vector<Range> w;
		for (uint32_t k = 0; k < SCAN_PIECES; k++)
		{
			const uint32_t a = scanAt + k * SCAN_PIECE;
			if (a >= RAM_HI) break;
			const uint32_t lo = a - RAM_LO >= SCAN_LEAD ? a - SCAN_LEAD : RAM_LO;
			const uint32_t hi = RAM_HI - a > SCAN_PIECE + SCAN_TAIL ? a + SCAN_PIECE + SCAN_TAIL : RAM_HI;
			w.push_back({ lo, hi - lo });
		}
		return w;
	}

	// a trainer's name as the games keep it: up to 8 characters ending in 0xFFFF (Gen IV's own codes, Gen V UTF-16), not
	// empty, no control codes
	static bool nameOk(Gen g, const uint8_t* p)
	{
		for (int i = 0; i < 8; i++)
		{
			const uint16_t c = u16(p + 2 * i);
			if (c == 0xFFFF) return i > 0;
			if (c == 0 || (g == Gen::V && c < 0x20)) return false;
		}
		return false;
	}

	void RamReader::scanDeliver(const std::vector<std::vector<uint8_t>>& d, double nowMs)
	{
		const Layout& l = *layout;
		const Gen g = l.gen;
		const size_t ps = g == Gen::IV ? 236 : 220;
		const std::vector<Range> asked = scanWants();
		const uint32_t blockAt = l.partyCount - 4;	// the party block in the save image (Gen IV {capacity, count}, Gen V count at +4)
		const uint32_t saveParty = base + l.head + l.party;
		for (size_t i = 0; i < asked.size() && i < d.size(); i++)
		{
			const auto& v = d[i];
			if (v.size() != asked[i].len) continue;	// (not mapped: nothing there)
			const uint32_t lo = asked[i].addr, hi = lo + asked[i].len, a = scanAt + (uint32_t)i * SCAN_PIECE;
			const uint8_t* m = v.data();
			auto at = [&](uint32_t addr) { return m + (addr - lo); };
			for (uint32_t p = a; p < a + SCAN_PIECE && p + 8 + 6 * ps <= hi; p += 4)
			{
				if (mode == SCAN_PTR)
				{
					const uint32_t v = u32(at(p));
					if (v > p && inRam(v))
						for (size_t k = 0; k < saves.size(); k++) if (saves[k].second == v) { found.push_back({ (int)k, p }); break; }
					continue;
				}
				if (mode == SCAN_SAVE)
				{
					// a party: Gen IV {u32 6, u32 1..6}, Gen V a count byte 1..6 at +4; every Pokémon passing its checksum
					const uint32_t n = g == Gen::IV ? u32(at(p + 4)) : *at(p + 4);
					if ((g == Gen::IV && u32(at(p)) != 6) || n < 1 || n > 6) continue;
					bool ok = true;
					std::vector<Mon> ms(n);
					for (uint32_t k = 0; k < n && ok; k++) ok = readMon(g, at(p + 8 + (uint32_t)ps * k), ps, ms[k]);
					if (!ok || p < blockAt) continue;
					const uint32_t image = p - blockAt;
					// ...in a save: the trainer's name and ID where the game keeps them (a battle's party copy has no name
					// before it); the ID one of the party's own Pokémon carry adds to the score
					const uint32_t nameAt = image + l.name, tidAt = image + l.tid;
					if (nameAt < lo || nameAt + 16 > hi || tidAt < lo || tidAt + 2 > hi || !nameOk(g, at(nameAt))) continue;
					int score = 2;
					for (auto& x : ms) if (x.otId == u16(at(tidAt))) { score++; break; }
					if (image - RAM_LO >= l.head) found.push_back({ score, image - l.head });
					continue;
				}
				// SCAN_BATTLE: the battle's copy of the party (its first Pokémon, encrypted as in the save, not the save's
				// own); Gen IV: a battle Pokémon (plain: species, PID at +0x68, level at +0x34) of one of the party
				if (lastParty.empty()) break;
				const Mon& first = lastParty[0];
				Mon x;
				if (p != saveParty && u32(at(p)) == first.pid && readMon(g, at(p), ps, x) && x.species == first.species)
					found.push_back({ 0, p });
				if (g == Gen::IV)
					for (auto& pm : lastParty)
						if (u16(at(p)) == pm.species && u32(at(p + 0x68)) == pm.pid && *at(p + 0x34) == pm.level) { foundMons.push_back(p); break; }
			}
		}
		scanAt += SCAN_PIECES * SCAN_PIECE;
		if (scanAt < RAM_HI) return;
		// ---- the whole RAM seen
		const Mode was = mode;
		mode = NORMAL;
		if (was == SCAN_SAVE)
		{
			if (found.empty()) { problem = "searched RAM: no save with a party"; return; }
			std::stable_sort(found.begin(), found.end(), [](const std::pair<int, uint32_t>& x, const std::pair<int, uint32_t>& y) { return x.first > y.first; });
			// (the games keep more than one copy of the save while loading it - one at the Plus's Platinum intro had no
			// pointer to it and stays as it was loaded: the one a static word points at is the game's own)
			saves.clear();
			for (auto& f : found) if (saves.size() < 8) saves.push_back(f);
			startScan(SCAN_PTR, nowMs);
			problem = "searched RAM: " + std::to_string(saves.size()) + " save(s) found, looking for the pointer";
			return;
		}
		if (was == SCAN_PTR)
		{
			// the first save (best scored) that a word points at, its lowest words first (static data comes before the
			// heap); then the saves themselves, the pointed-at one first
			size_t pick = saves.size();
			for (auto& f : found) if ((size_t)f.first < pick) pick = (size_t)f.first;
			std::vector<uint32_t> ptrs;
			for (auto& f : found) if ((size_t)f.first == pick) ptrs.push_back(f.second);
			std::sort(ptrs.begin(), ptrs.end());
			if (pick == saves.size()) pick = 0;
			// (a game that keeps a pointer: the saves themselves after every pointer - stand-ins until one holds; BW: first)
			const bool front = !l.ptr;
			if (front) for (size_t k = saves.size(); k-- > 0;) if (k != pick) addCand({ 0, saves[k].second }, true);
			if (front) addCand({ 0, saves[pick].second }, true);
			else { addCand({ 0, saves[pick].second }); for (size_t k = 0; k < saves.size(); k++) if (k != pick) addCand({ 0, saves[k].second }); }
			for (size_t i = std::min<size_t>(ptrs.size(), 4); i-- > 0;) addCand({ ptrs[i], 0 }, true);
			bad = 0; base = 0;
			// the first pointer, else the found save (a stand-in)
			ci = 0;
			if (ptrs.empty()) for (size_t i = 0; i < cands.size(); i++) if (cands[i].ptr == 0 && cands[i].direct == saves[pick].second) { ci = i; break; }
			problem = ptrs.empty() ? "searched RAM: no pointer to the save" : "searched RAM: pointers to the save tried next";
			return;
		}
		// SCAN_BATTLE: the find nearest to where it was expected
		auto nearest = [](const std::vector<uint32_t>& xs, uint32_t want) -> int64_t
		{
			int64_t best = 0; bool any = false;
			for (uint32_t x : xs) { const int64_t dlt = (int64_t)x - want; if (!any || llabs(dlt) < llabs(best)) { best = dlt; any = true; } }
			return any ? best : INT64_MIN;
		};
		const uint32_t s = cand().ptr ? base : cand().direct;
		std::vector<uint32_t> ours;
		for (auto& f : found) ours.push_back(f.second);
		const int64_t bsh = nearest(ours, s + l.bOurs);
		if (bsh != INT64_MIN) battleShift = (int32_t)bsh;
		if (g == Gen::IV)
		{
			const int64_t msh = nearest(foundMons, s + l.bMon0);
			if (msh != INT64_MIN) monShift = (int32_t)msh;
		}
		battleBad = 0;
		if (bsh != INT64_MIN) { anchorsDirty = true; problem = "searched RAM: the battle's copies found"; }
		else problem = "searched RAM: no battle copy of the party";
	}

	// a battle is on: the battle overlay's code is in RAM (the player's ROM's bytes), else the known word in it
	bool RamReader::battleOn(const std::vector<std::vector<uint8_t>>& d) const
	{
		if (!hints.battleBytes.empty() && d[W_SIG].size() == hints.battleBytes.size()) return d[W_SIG] == hints.battleBytes;
		// (Ironmon's "in battle" word: 0x2100 / 0x2101 there, 0x2800 on the field - Thumb code of the battle overlay or of
		// the field's, loaded at the same address)
		if (d[W_INBATTLE].size() != 2) return false;
		const uint16_t v = u16(d[W_INBATTLE].data());
		return v == 0x2100 || v == 0x2101;
	}

	bool RamReader::deliver(const std::vector<std::vector<uint8_t>>& d, double nowMs, time_t wall)
	{
		problem.clear();
		if (!layout) { problem = "game not supported"; return false; }
		if (mode != NORMAL) { scanDeliver(d, nowMs); return false; }
		if (d.size() < W_COUNT) { problem = "short reply"; return false; }
		const Layout& l = *layout;
		// Gen IV: the pointer the module parameters give (the first .bss word + 0x20), another guess
		if (l.gen == Gen::IV && !paramsDone && d[W_PARAMS].size() == PARAMS_LEN)
		{
			paramsDone = true;
			const uint8_t* p = d[W_PARAMS].data();
			for (uint32_t i = 0x1C; i + 4 <= PARAMS_LEN; i += 4)
				if (u32(p + i) == 0xDEC00621)
				{
					const uint32_t ptr = u32(p + i - 0x1C + 8) + 0x20;
					if (inRam(ptr, 4)) addCand({ ptr, 0 });
					break;
				}
		}
		if (standIn(cand()))
		{
			// a pointer that holds an address (and didn't just fail) is the game's own save: taken at once
			for (size_t i = 0; i < PEEK && i < cands.size(); i++)
				if (cands[i].ptr && d[W_C0 + i].size() == 4 && inRam(u32(d[W_C0 + i].data())) && nowMs - cands[i].failedAt >= SCAN_AGAIN_MS)
				{
					ci = i; bad = 0; base = 0; checked = false;
					problem = "a pointer holds the save now";
					return false;
				}
			// none: the search again now and then, for the pointer (the game may not have had its save in place yet)
			if (nowMs - lastScan >= SCAN_AGAIN_MS) { startScan(SCAN_SAVE, nowMs); problem = "on a stand-in: searching again"; return false; }
		}
		const Anchor a = cand();
		if (a.ptr)
		{
			if (d[W_PTR].size() != 4) { problem = "no pointer"; return false; }
			const uint32_t p = u32(d[W_PTR].data());
			if (p != base || !inRam(p))
			{
				// the save moved (the game started, or reset): the data in this set is from the old place
				base = p;
				checked = false;
				memset(battlePtrs, 0, sizeof battlePtrs);
				memset(battleMons, 0, sizeof battleMons);
				problem = inRam(p) ? "pointer moved" : "no save in RAM yet";
				if (!inRam(p))
				{
					// another guess holding an address is tried now; a pointer that never holds one is not the one
					for (size_t i = 0; i < PEEK && i < cands.size(); i++)
						if (i != ci && cands[i].ptr && d[W_C0 + i].size() == 4 && inRam(u32(d[W_C0 + i].data())))
						{
							ci = i; bad = 0; base = 0;
							return false;
						}
					if (++bad >= NO_POINTER_SETS)
					{
						// (not a failure of the place: taken as soon as it holds one)
						const size_t was = ci;
						nextCand(nowMs);
						if (was < cands.size()) cands[was].failedAt = -1e12;
					}
				}
				return false;
			}
		}
		else base = a.direct;
		uint64_t h = 1469598103934665603ull;
		for (auto& v : d) h = fnv(h, v);
		if (h == lastHash && cur->valid)
		{
			// (nothing changed: a battle whose copies didn't match still doesn't)
			if (battleBad > 0 && ++battleBad >= BATTLE_BAD_SETS && checked && nowMs - lastBattleScan >= BATTLE_SCAN_AGAIN_MS) startScan(SCAN_BATTLE, nowMs);
			return false;
		}
		Live out;
		const bool ok = parse(d, out, wall);
		// how the guess fared: checked out (kept, and remembered for this ROM), a save with no game yet, or not a save
		if (verdict == GOOD)
		{
			bad = 0;
			checked = true;
			if (!standIn(a) && (cached.ptr != a.ptr || cached.direct != a.direct)) { cached = a; anchorsDirty = true; }
		}
		else if (++bad >= (verdict == EMPTY ? EMPTY_SETS : BAD_SETS)) nextCand(nowMs);
		// in a battle whose copies don't match the party: they are elsewhere in this revision - look for them
		if (battleBad >= BATTLE_BAD_SETS && checked && nowMs - lastBattleScan >= BATTLE_SCAN_AGAIN_MS) startScan(SCAN_BATTLE, nowMs);
		if (!ok) return false;
		lastHash = h;
		out.valid = true;
		cur = std::make_shared<const Live>(std::move(out));
		gen++;
		return true;
	}

	// a party slot: the Pokémon, or (being written right now) the last good reading of the same Pokémon
	static bool partySlot(Gen g, const uint8_t* raw, size_t n, const std::vector<Mon>& last, size_t i, Mon& m)
	{
		if (readMon(g, raw, n, m)) return true;
		if (i < last.size() && last[i].pid == u32(raw)) { m = last[i]; return true; }
		return false;
	}

	bool RamReader::parse(const std::vector<std::vector<uint8_t>>& d, Live& o, time_t wall)
	{
		const Layout& l = *layout;
		const Gen g = l.gen;
		const auto& m = d[W_MAIN];
		verdict = BAD;
		if (m.empty()) { problem = "no save in RAM yet"; return false; }
		const size_t ps = g == Gen::IV ? 236 : 220;
		uint32_t rel = g == Gen::IV ? 0 : l.bag;	// offsets in the main range are from its start
		auto at = [&](uint32_t off) { return m.data() + (off - rel); };
		// the party: Gen IV {u32 capacity 6, u32 count}, Gen V a u8 count
		uint32_t count;
		if (g == Gen::IV)
		{
			if (u32(at(l.partyCount - 4)) != 6) { problem = "party capacity is not 6 (no save loaded)"; return false; }
			count = u32(at(l.partyCount));
		}
		else count = *at(l.partyCount);
		if (count > 6) { problem = "party count out of range"; return false; }
		for (uint32_t i = 0; i < count; i++)
		{
			Mon mon;
			if (!partySlot(g, at(l.party + ps * i), ps, lastParty, i, mon)) { problem = "a party Pokémon is being written"; verdict = BUSY; return false; }
			o.party.push_back(mon);
		}
		lastParty = o.party;
		o.trainerName = monText(g, at(l.name), 8);
		// a fresh game at its title or before the player is named: the save block is there, empty (Diamond with no save
		// showed Twinleaf Town and a badge at the title). A playthrough has a name (and soon a party). (Only a party that
		// passed its checksums says this is the save; until the first Pokémon a guess stays unproven: EMPTY)
		verdict = o.party.empty() ? EMPTY : GOOD;
		if (o.party.empty() && o.trainerName.empty()) { problem = "no game started yet (the title or a new game's intro)"; return false; }
		o.tid = u16(at(l.tid));
		o.sid = u16(at(l.tid + 2));
		const uint8_t* t = at(l.time);
		o.playSeconds = u16(t) * 3600u + t[2] * 60u + t[3];
		const int dexSize = g == Gen::IV ? 493 : 649;
		std::vector<Range> unused;
		if (g == Gen::IV)
		{
			o.money = u32(at(l.money));
			o.badges = *at(l.badges) | (l.kanto ? *at(l.kanto) << 8 : 0);
			o.zone = (uint16_t)u32(at(l.map));
			// the save's Location {map, warp id, x, z}: x and z follow every step (seen on HeartGold)
			o.mapX = (uint16_t)u32(at(l.map + 8));
			o.mapY = (uint16_t)u32(at(l.map + 12));
			o.dexCaught = bits(at(l.dex + 4), dexSize);
			o.dexSeen = bits(at(l.dex + 0x44), dexSize);
			for (size_t j = 0; j < o.dexSeen.size(); j++) o.dexSeen[j] |= o.dexCaught[j];	// (caught is seen)
			o.flags.assign(at(l.flags), at(l.flags) + l.flagBytes);
			// the daycare: two boxed Pokémon (136 bytes of each 236-byte slot) and the EXP each gained there
			for (int i = 0; i < 2; i++)
			{
				const uint8_t* s = at(l.daycare + 236 * i);
				Mon dm;
				if (readMon(g, s, 136, dm)) { dm.exp += u32(s + 232); o.daycare.push_back(dm); }
			}
			o.eggSteps = *at(l.daycare + 2 * 236 + 4);
			const auto& r = d[W_ROAM];
			for (int i = 0; r.size() >= 0x14u * l.nRoamers && i < l.nRoamers; i++)
			{
				const uint8_t* p = r.data() + 0x14 * i;
				Roamer ro;
				ro.zone = (uint16_t)u32(p); ro.species = u16(p + 0xC); ro.hp = u16(p + 0xE); ro.level = p[0x10];
				ro.active = p[0x12] != 0;
				if (ro.species && ro.species <= dexSize) o.roamers.push_back(ro);
			}
		}
		else
		{
			const auto& mi = d[W_V_MISC];
			if (mi.size() < l.roamers + 0x14u * l.nRoamers - l.money) { problem = "short misc block"; verdict = BAD; return false; }
			auto atm = [&](uint32_t off) { return mi.data() + (off - l.money); };
			o.money = u32(atm(l.money));
			o.badges = *atm(l.badges);
			// the live zone and tile; the save's position block only when they didn't read
			o.zone = d[W_V_ZONE].size() == 2 ? u16(d[W_V_ZONE].data()) : u16(at(l.posMap));
			if (d[W_V_POS].size() == 6) { o.mapX = u16(d[W_V_POS].data()); o.mapY = u16(d[W_V_POS].data() + 4); }
			o.dexCaught = bits(atm(l.dex + 8), dexSize);
			std::vector<uint8_t> seen(bits(atm(l.dex + 0x5C), dexSize));
			for (int k = 1; k < 4; k++)		// male, female, shiny male, shiny female: seen = any of them
			{
				auto b = bits(atm(l.dex + 0x5C + 0x54 * k), dexSize);
				for (size_t j = 0; j < seen.size(); j++) seen[j] |= b[j];
			}
			for (size_t j = 0; j < seen.size(); j++) seen[j] |= o.dexCaught[j];	// (caught is seen)
			o.dexSeen = seen;
			for (int i = 0; i < l.nRoamers; i++)
			{
				const uint8_t* p = atm(l.roamers + 0x14 * i);
				Roamer ro;
				ro.species = u16(p + 0xC); ro.hp = u16(p + 0xE); ro.level = p[0x10]; ro.active = p[0x12] != 0;
				if (ro.species && ro.species <= dexSize) o.roamers.push_back(ro);
			}
			const auto& dc = d[W_V_DAYCARE];
			for (int i = 0; dc.size() >= 2 * 228 && i < 2; i++)
			{
				const uint8_t* s = dc.data() + 228 * i;
				Mon dm;
				if (u32(s) && readMon(g, s + 4, 220, dm)) { dm.exp += u32(s + 224); o.daycare.push_back(dm); }
			}
			o.flags = d[W_V_FLAGS];
		}
		if (d[W_REPEL].size() == 1) o.repelSteps = d[W_REPEL][0];
		for (auto& p : l.pockets)
		{
			auto& v = o.bag[p.pocket];
			const uint8_t* b = at(l.bag + p.offset);
			for (int i = 0; i < p.slots; i++)
			{
				const uint16_t it = u16(b + 4 * i), n = u16(b + 4 * i + 2);
				if (it && n) v.push_back({ it, n });
			}
		}
		// Gen V keeps its balls among the items: ids 1..16 (Master Ball .. Cherish Ball) and 576 (Dream Ball)
		if (g == Gen::V)
		{
			auto& items = o.bag[POCKET_ITEMS];
			auto& balls = o.bag[POCKET_BALLS];
			for (auto it = items.begin(); it != items.end();)
				if ((it->item >= 1 && it->item <= 16) || it->item == 576) { balls.push_back(*it); it = items.erase(it); } else ++it;
		}
		struct tm lt;
		localtime_r(&wall, &lt);
		o.timeOfDay = timeOfDay(lt.tm_hour);
		o.season = season(lt.tm_mon + 1);
		o.weekday = (uint8_t)lt.tm_wday;
		if (g == Gen::IV) battle4(d, o); else battle5(d, o);
		return true;
	}
}

namespace banana::nds
{
	// Gen IV: BattleMon (plain, 0xC0 bytes; slots player 0, foe 0, player 1, foe 1)
	static bool battleMon4(const uint8_t* p, Battler& b)
	{
		Mon& m = b.mon;
		m.species = u16(p);
		if (!m.species || m.species > 493) return false;
		for (int i = 0; i < 5; i++) m.stats[i] = u16(p + 2 + 2 * i);	// Atk, Def, Speed, SpA, SpD
		const uint32_t iv = u32(p + 0x14);
		for (int i = 0; i < 6; i++) m.ivs[i] = (uint8_t)((iv >> (5 * i)) & 31);
		for (int i = 0; i < 8; i++) b.stages[i] = stage(p[0x18 + i]);	// HP (unused), Atk, Def, Speed, SpA, SpD, Acc, Eva
		b.types[0] = typeFromGame(Gen::IV, p[0x24]);
		b.types[1] = typeFromGame(Gen::IV, p[0x25]);
		m.form = p[0x26] & 0x1F;
		m.ability = p[0x27];
		for (int i = 0; i < 4; i++) { m.moves[i] = u16(p + 0x0C + 2 * i); m.pp[i] = p[0x2C + i]; m.ppUps[i] = p[0x30 + i]; }
		m.level = p[0x34];
		m.friendship = p[0x35];
		m.nick = monText(Gen::IV, p + 0x36, 11);
		const int32_t hp = (int32_t)u32(p + 0x4C);
		m.hp = (uint16_t)(hp < 0 ? 0 : hp);
		m.maxHp = (uint16_t)u32(p + 0x50);
		m.otName = monText(Gen::IV, p + 0x54, 8);
		m.exp = u32(p + 0x64);
		m.pid = u32(p + 0x68);
		m.status = u32(p + 0x6C);
		m.otId = u16(p + 0x74);
		m.otSecret = u16(p + 0x76);
		m.item = u16(p + 0x78);
		m.nature = (uint8_t)(m.pid % 25);
		if (m.level < 1 || m.level > 100 || !m.maxHp || m.hp > m.maxHp) return false;
		b.present = true;
		return true;
	}

	// what the party copy knows that the battle copy doesn't (EVs, met data, the gender bit...): taken by PID
	static void fillFrom(const std::vector<Mon>& party, Mon& m)
	{
		for (auto& p : party)
			if (p.pid == m.pid && p.species)
			{
				for (int i = 0; i < 6; i++) m.evs[i] = p.evs[i];
				m.female = p.female; m.egg = p.egg; m.pokerus = p.pokerus; m.metLocation = p.metLocation;
				m.metLevel = p.metLevel; m.ball = p.ball; m.nature = p.nature;
				return;
			}
	}

	static std::vector<Mon> partyCopy(Gen g, const std::vector<uint8_t>& raw)
	{
		std::vector<Mon> out;
		const size_t ps = g == Gen::IV ? 236 : 220;
		for (size_t i = 0; i + ps <= raw.size(); i += ps)
		{
			Mon m;
			if (!readMon(g, raw.data() + i, ps, m)) break;
			out.push_back(m);
		}
		return out;
	}

	void RamReader::battle4(const std::vector<std::vector<uint8_t>>& d, Live& o)
	{
		const auto& bm = d[W_BMON];
		if (!battleOn(d)) { battleBad = 0; return; }
		Battle& b = o.battle;
		Battler slots[4];
		if (bm.size() == 4 * BMON) for (int i = 0; i < 4; i++) battleMon4(bm.data() + BMON * i, slots[i]);
		const std::vector<Mon> ours = partyCopy(Gen::IV, d[W_OURS]);
		// the battle's copies where this revision keeps them? our active Pokémon is one of the party, the copy of the
		// party starts with its first (else they are looked for: deliver)
		bool mine = false;
		for (auto& p : o.party) if (slots[0].present && p.pid == slots[0].mon.pid) mine = true;
		const bool copyOk = !ours.empty() && !o.party.empty() && ours[0].pid == o.party[0].pid;
		battleBad = mine && copyOk ? 0 : battleBad + 1;
		if (!slots[0].present || !slots[1].present) return;
		b.foeParty = partyCopy(Gen::IV, d[W_FOES]);
		b.ours[0] = slots[0]; b.foes[0] = slots[1]; b.ours[1] = slots[2]; b.foes[1] = slots[3];
		for (auto* x : { &b.ours[0], &b.ours[1] }) if (x->present) { fillFrom(ours, x->mon); fillFrom(o.party, x->mon); }
		for (auto* x : { &b.foes[0], &b.foes[1] }) if (x->present) fillFrom(b.foeParty, x->mon);
		b.trainerId = d[W_TRAINER].size() == 2 ? u16(d[W_TRAINER].data()) : 0;
		const bool dbl = slots[2].present || slots[3].present;
		b.kind = b.trainerId ? (dbl ? BattleKind::DOUBLE_TRAINER : BattleKind::TRAINER)
			: (dbl ? BattleKind::DOUBLE_WILD : BattleKind::WILD);
		if (!b.trainerId) b.foeParty.clear();	// (a wild battle's "party" is the wild Pokémon itself)
	}

	// Gen V: battler objects through the table (block k = 0x1C bytes: six pointers + a word; block 0 ours, 1 the foe's)
	void RamReader::battle5(const std::vector<std::vector<uint8_t>>& d, Live& o)
	{
		const auto& tb = d[W_V_TABLE];
		uint32_t want[4] = {};
		const bool on = battleOn(d), in = on && tb.size() == 4 * 0x1C;
		const int dbl = d[W_V_DOUBLE].size() == 1 ? d[W_V_DOUBLE][0] : 0;
		if (in)
		{
			want[0] = u32(tb.data());			// our first
			want[1] = u32(tb.data() + 0x1C);	// the foe's first
			if (dbl == 1) { want[2] = u32(tb.data() + 4); want[3] = u32(tb.data() + 0x1C + 4); }
		}
		Battle& b = o.battle;
		Battler* slot[4] = { &b.ours[0], &b.foes[0], &b.ours[1], &b.foes[1] };
		uint32_t nextMons[4] = {};
		for (int i = 0; i < 4; i++)
		{
			const size_t bi = W_COUNT + 2 * i;
			if (!want[i] || !inRam(want[i], BATTLER)) continue;
			// this set read the battler where the table points now? then its Pokémon pointer is current
			if (want[i] != battlePtrs[i] || bi + 1 >= d.size() || d[bi].size() != BATTLER) continue;
			const uint8_t* p = d[bi].data();
			nextMons[i] = u32(p);
			if (nextMons[i] != battleMons[i] || d[bi + 1].size() != 220) continue;
			Battler& x = *slot[i];
			if (!readMon(Gen::V, d[bi + 1].data(), 220, x.mon)) continue;
			Mon& m = x.mon;
			m.maxHp = u16(p + 0x0E); m.hp = u16(p + 0x10); m.level = p[0x18]; m.status = u32(p + 0x20);
			// battle stats: Atk, Def, SpA, SpD, Speed -> Mon's Atk, Def, Speed, SpA, SpD
			m.stats[0] = u16(p + 0xEE); m.stats[1] = u16(p + 0xF0); m.stats[3] = u16(p + 0xF2);
			m.stats[4] = u16(p + 0xF4); m.stats[2] = u16(p + 0xF6);
			// stages: Atk, Def, SpA, SpD, Speed, Acc, Eva -> HP, Atk, Def, Speed, SpA, SpD, Acc, Eva
			x.stages[0] = 0; x.stages[1] = stage(p[0xFC]); x.stages[2] = stage(p[0xFD]); x.stages[4] = stage(p[0xFE]);
			x.stages[5] = stage(p[0xFF]); x.stages[3] = stage(p[0x100]); x.stages[6] = stage(p[0x101]);
			x.stages[7] = stage(p[0x102]);
			for (int k = 0; k < 4; k++) { m.moves[k] = u16(p + 0x104 + 14 * k); m.pp[k] = p[0x106 + 14 * k]; }
			// (Gen V's battler has no type bytes here: the types come from the species, RomData)
			x.types[0] = x.types[1] = NO_TYPE;
			x.present = m.level >= 1 && m.level <= 100 && m.maxHp && m.hp <= m.maxHp;
		}
		memcpy(battlePtrs, want, sizeof want);
		memcpy(battleMons, nextMons, sizeof nextMons);
		// the battle's copies where this revision keeps them? (the copy of the party starts with its first; our battler
		// is read in a few sets - the table, its objects, their Pokémon - so it may be missing a little while)
		if (!on) battleBad = 0;
		else
		{
			const std::vector<Mon> ours = partyCopy(Gen::V, d[W_OURS]);
			const bool copyOk = !ours.empty() && !o.party.empty() && ours[0].pid == o.party[0].pid;
			battleBad = copyOk && b.ours[0].present ? 0 : battleBad + 1;
		}
		if (!in || !b.ours[0].present || !b.foes[0].present) { b = Battle{}; return; }
		b.foeParty = partyCopy(Gen::V, d[W_FOES]);
		for (auto* x : { &b.ours[0], &b.ours[1] }) if (x->present) fillFrom(o.party, x->mon);
		b.trainerId = d[W_TRAINER].size() == 2 ? u16(d[W_TRAINER].data()) : 0;
		const bool two = b.ours[1].present || b.foes[1].present;
		b.kind = b.trainerId ? (two ? BattleKind::DOUBLE_TRAINER : BattleKind::TRAINER)
			: (two ? BattleKind::DOUBLE_WILD : BattleKind::WILD);
		if (!b.trainerId) b.foeParty.clear();
	}

	// ---- CompLink's READ_CORE_MEMORY lines --------------------------------------------------------------------
	std::string requestLines(const std::vector<Range>& r)
	{
		std::string out;
		char b[64];
		for (auto& x : r)
			if (x.len) { snprintf(b, sizeof b, "READ_CORE_MEMORY %x %u\n", x.addr, x.len); out += b; }
		return out;
	}

	bool replyBytes(const std::string& lines, const std::vector<Range>& r, std::vector<std::vector<uint8_t>>& out)
	{
		out.assign(r.size(), {});
		size_t pos = 0;
		static const char P[] = "READ_CORE_MEMORY ";
		bool any = false;
		while (pos < lines.size())
		{
			size_t nl = lines.find('\n', pos);
			if (nl == std::string::npos) nl = lines.size();
			if (!lines.compare(pos, sizeof P - 1, P))
			{
				const char* s = lines.c_str() + pos + sizeof P - 1;
				char* e;
				const uint32_t a = (uint32_t)strtoul(s, &e, 16);
				std::vector<uint8_t> v;
				s = e;
				while (*s == ' ') s++;
				if (*s != '-')
					while (s < lines.c_str() + nl)
					{
						const unsigned long x = strtoul(s, &e, 16);
						if (e == s || x > 0xff) break;
						v.push_back((uint8_t)x);
						s = e;
						while (*s == ' ') s++;
					}
				// the first range at that address still unanswered, of that length
				for (size_t i = 0; i < r.size(); i++)
					if (r[i].len && r[i].addr == a && out[i].empty() && v.size() == r[i].len) { out[i] = std::move(v); any = true; break; }
			}
			pos = nl + 1;
		}
		return any;
	}
}
