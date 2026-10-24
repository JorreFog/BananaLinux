// banana-ui: the Pokémon companion's reading of the games (pokemon.h)
#include "pokemon.h"
#include "zip.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace banana::pkmn
{
	namespace
	{
		uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
		uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

		const char* const TYPE_NAMES[NUM_TYPES] = { "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug", "Ghost",
			"Steel", "???", "Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark" };
		const uint32_t TYPE_COLORS[NUM_TYPES] = { 0xa8a878, 0xc03028, 0xa890f0, 0xa040a0, 0xe0c068, 0xb8a038, 0xa8b820, 0x705898,
			0xb8b8d0, 0x68a090, 0xf08030, 0x6890f0, 0x78c850, 0xf8d030, 0xf85888, 0x98d8d8, 0x7038f8, 0x705848 };

		// the Generation III type chart: attacker, defender, 0 none / 1 half / 2 double; every pair not listed is normal
		const uint8_t CHART[][3] = {
			{ NORMAL, ROCK, 1 }, { NORMAL, STEEL, 1 }, { NORMAL, GHOST, 0 },
			{ FIRE, FIRE, 1 }, { FIRE, WATER, 1 }, { FIRE, GRASS, 2 }, { FIRE, ICE, 2 }, { FIRE, BUG, 2 }, { FIRE, ROCK, 1 },
			{ FIRE, DRAGON, 1 }, { FIRE, STEEL, 2 },
			{ WATER, FIRE, 2 }, { WATER, WATER, 1 }, { WATER, GRASS, 1 }, { WATER, GROUND, 2 }, { WATER, ROCK, 2 }, { WATER, DRAGON, 1 },
			{ ELECTRIC, WATER, 2 }, { ELECTRIC, ELECTRIC, 1 }, { ELECTRIC, GRASS, 1 }, { ELECTRIC, GROUND, 0 }, { ELECTRIC, FLYING, 2 },
			{ ELECTRIC, DRAGON, 1 },
			{ GRASS, FIRE, 1 }, { GRASS, WATER, 2 }, { GRASS, GRASS, 1 }, { GRASS, POISON, 1 }, { GRASS, GROUND, 2 }, { GRASS, FLYING, 1 },
			{ GRASS, BUG, 1 }, { GRASS, ROCK, 2 }, { GRASS, DRAGON, 1 }, { GRASS, STEEL, 1 },
			{ ICE, WATER, 1 }, { ICE, GRASS, 2 }, { ICE, ICE, 1 }, { ICE, GROUND, 2 }, { ICE, FLYING, 2 }, { ICE, DRAGON, 2 }, { ICE, STEEL, 1 },
			{ ICE, FIRE, 1 },
			{ FIGHTING, NORMAL, 2 }, { FIGHTING, ICE, 2 }, { FIGHTING, POISON, 1 }, { FIGHTING, FLYING, 1 }, { FIGHTING, PSYCHIC, 1 },
			{ FIGHTING, BUG, 1 }, { FIGHTING, ROCK, 2 }, { FIGHTING, DARK, 2 }, { FIGHTING, STEEL, 2 }, { FIGHTING, GHOST, 0 },
			{ POISON, GRASS, 2 }, { POISON, POISON, 1 }, { POISON, GROUND, 1 }, { POISON, ROCK, 1 }, { POISON, GHOST, 1 }, { POISON, STEEL, 0 },
			{ GROUND, FIRE, 2 }, { GROUND, ELECTRIC, 2 }, { GROUND, GRASS, 1 }, { GROUND, POISON, 2 }, { GROUND, FLYING, 0 }, { GROUND, BUG, 1 },
			{ GROUND, ROCK, 2 }, { GROUND, STEEL, 2 },
			{ FLYING, ELECTRIC, 1 }, { FLYING, GRASS, 2 }, { FLYING, FIGHTING, 2 }, { FLYING, BUG, 2 }, { FLYING, ROCK, 1 }, { FLYING, STEEL, 1 },
			{ PSYCHIC, FIGHTING, 2 }, { PSYCHIC, POISON, 2 }, { PSYCHIC, PSYCHIC, 1 }, { PSYCHIC, DARK, 0 }, { PSYCHIC, STEEL, 1 },
			{ BUG, FIRE, 1 }, { BUG, GRASS, 2 }, { BUG, FIGHTING, 1 }, { BUG, POISON, 1 }, { BUG, FLYING, 1 }, { BUG, PSYCHIC, 2 },
			{ BUG, GHOST, 1 }, { BUG, DARK, 2 }, { BUG, STEEL, 1 },
			{ ROCK, FIRE, 2 }, { ROCK, ICE, 2 }, { ROCK, FIGHTING, 1 }, { ROCK, GROUND, 1 }, { ROCK, FLYING, 2 }, { ROCK, BUG, 2 }, { ROCK, STEEL, 1 },
			{ GHOST, NORMAL, 0 }, { GHOST, PSYCHIC, 2 }, { GHOST, DARK, 1 }, { GHOST, STEEL, 1 }, { GHOST, GHOST, 2 },
			{ DRAGON, DRAGON, 2 }, { DRAGON, STEEL, 1 },
			{ DARK, FIGHTING, 1 }, { DARK, PSYCHIC, 2 }, { DARK, GHOST, 2 }, { DARK, DARK, 1 }, { DARK, STEEL, 1 },
			{ STEEL, FIRE, 1 }, { STEEL, WATER, 1 }, { STEEL, ELECTRIC, 1 }, { STEEL, ICE, 2 }, { STEEL, ROCK, 2 }, { STEEL, STEEL, 1 },
		};
		int one(int atk, int def)
		{
			for (auto& c : CHART) if (c[0] == atk && c[1] == def) return c[2] == 0 ? 0 : c[2] == 1 ? 1 : 4;	// (halves)
			return 2;
		}

		// the English releases (pret's symbol files: gPlayerPartyCount, gPlayerParty, gBattleMons, gBattleTypeFlags,
		// and gMain's byte with inBattle in bit 1: 0x439 in Emerald and FireRed, 0x43D in Ruby)
		const GameInfo GAMES[] = {
			{ "AXVE", "Pokémon Ruby", 0x03004350, 0x03004360, 0x02024a80, 0x020239f8, 0x03001770 + 0x43d },
			{ "AXPE", "Pokémon Sapphire", 0x03004350, 0x03004360, 0x02024a80, 0x020239f8, 0x03001770 + 0x43d },
			{ "BPEE", "Pokémon Emerald", 0x020244e9, 0x020244ec, 0x02024084, 0x02022fec, 0x030022c0 + 0x439 },
			{ "BPRE", "Pokémon FireRed", 0x02024029, 0x02024284, 0x02023be4, 0x02022b4c, 0x030030f0 + 0x439 },
			{ "BPGE", "Pokémon LeafGreen", 0x02024029, 0x02024284, 0x02023be4, 0x02022b4c, 0x030030f0 + 0x439 },
		};
		const int PARTY = 100, BATTLER = 0x58;

		// the Western character set: a byte's text ("" for one with none here)
		const char* glyph(uint8_t b)
		{
			static const char* const low[0x2f] = { " ", "À", "Á", "Â", "Ç", "È", "É", "Ê", "Ë", "Ì", "", "Î", "Ï", "Ò", "Ó", "Ô",
				"Œ", "Ù", "Ú", "Û", "Ñ", "ß", "à", "á", "", "ç", "è", "é", "ê", "ë", "ì", "", "î", "ï", "ò", "ó", "ô", "œ", "ù", "ú",
				"û", "ñ", "º", "ª", "", "&", "+" };
			static const char* const punct[0x1a] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "!", "?", ".", "-", "·",
				"…", "“", "”", "‘", "’", "♂", "♀", "¥", ",", "×", "/" };
			static const char* const umlaut[6] = { "Ä", "Ö", "Ü", "ä", "ö", "ü" };
			static char letter[2];
			if (b < 0x2f) return low[b];
			if (b >= 0xa1 && b <= 0xba) return punct[b - 0xa1];
			if (b >= 0xbb && b <= 0xd4) { letter[0] = (char)('A' + b - 0xbb); letter[1] = 0; return letter; }
			if (b >= 0xd5 && b <= 0xee) { letter[0] = (char)('a' + b - 0xd5); letter[1] = 0; return letter; }
			if (b >= 0xf1 && b <= 0xf6) return umlaut[b - 0xf1];
			switch (b)
			{
			case 0x34: return "Lv";
			case 0x35: return "=";
			case 0x36: return ";";
			case 0x51: return "¿";
			case 0x52: return "¡";
			case 0x5a: return "Í";
			case 0x5b: return "%";
			case 0x5c: return "(";
			case 0x5d: return ")";
			case 0x68: return "â";
			case 0x6f: return "í";
			case 0x85: return "<";
			case 0x86: return ">";
			case 0xef: return "▶";
			case 0xf0: return ":";
			}
			return "";
		}
		bool nameChar(uint8_t b) { return b != 0xff && *glyph(b); }

		// a table of fixed-size names: entry 0 is first (its bytes given), then count - 1 names, each a terminated
		// string of name characters filling no more than its entry
		long findNames(const uint8_t* rom, size_t n, const uint8_t* first, size_t firstLen, size_t entry, size_t count)
		{
			if (n < entry * count) return -1;
			for (const uint8_t* p = rom; (p = (const uint8_t*)memmem(p, n - (p - rom), first, firstLen)); p++)
			{
				size_t at = p - rom;
				if (at + entry * count > n) return -1;
				bool ok = true;
				for (size_t i = 1; i < count && ok; i++)
				{
					const uint8_t* e = rom + at + i * entry;
					size_t k = 0;
					while (k < entry && e[k] != 0xff) { if (!nameChar(e[k])) { ok = false; break; } k++; }
					if (k == 0 || k == entry) ok = false;	// (a name of none, or with no end in its entry)
				}
				if (ok) return (long)at;
			}
			return -1;
		}
		// a table of fixed-size records found by the bytes of two or three of its first records
		long findRecords(const uint8_t* rom, size_t n, const std::vector<std::vector<uint8_t>>& sig, size_t entry, size_t sigAt, size_t firstIndex, size_t count)
		{
			const auto& s0 = sig[0];
			for (const uint8_t* p = rom; (p = (const uint8_t*)memmem(p, n - (p - rom), s0.data(), s0.size())); p++)
			{
				size_t at = p - rom;
				if (at < sigAt + firstIndex * entry) continue;
				size_t start = at - sigAt - firstIndex * entry;
				if (start + entry * count > n) return -1;
				bool ok = true;
				for (size_t i = 1; i < sig.size() && ok; i++)
					ok = at + i * entry + sig[i].size() <= n && memcmp(rom + at + i * entry, sig[i].data(), sig[i].size()) == 0;
				if (ok) return (long)start;
			}
			return -1;
		}
	}

	const char* typeName(int t) { return t >= 0 && t < NUM_TYPES ? TYPE_NAMES[t] : ""; }
	uint32_t typeColor(int t) { return t >= 0 && t < NUM_TYPES ? TYPE_COLORS[t] : 0x808080; }

	int effect(int atk, int t1, int t2)
	{
		if (atk < 0 || atk >= NUM_TYPES || t1 >= NUM_TYPES) return 4;
		int m = one(atk, t1);	// in halves
		if (t2 != t1 && t2 < NUM_TYPES) return m * one(atk, t2);	// halves x halves = quarters
		return m * 2;
	}

	const GameInfo* detectGame(const uint8_t* h, size_t n)
	{
		if (n < 0xb0) return nullptr;
		for (auto& g : GAMES) if (memcmp(h + 0xac, g.code, 4) == 0) return &g;
		return nullptr;
	}

	std::string decodeText(const uint8_t* p, size_t max)
	{
		std::string s;
		for (size_t i = 0; i < max && p[i] != 0xff; i++) s += glyph(p[i]);
		return s;
	}

	bool decodeParty(const uint8_t* p, Mon& m)
	{
		m = Mon();
		uint32_t pid = le32(p), otid = le32(p + 4), key = pid ^ otid;
		uint8_t d[48];
		uint16_t sum = 0;
		for (int i = 0; i < 48; i += 4)
		{
			uint32_t v = le32(p + 0x20 + i) ^ key;
			d[i] = (uint8_t)v; d[i + 1] = (uint8_t)(v >> 8); d[i + 2] = (uint8_t)(v >> 16); d[i + 3] = (uint8_t)(v >> 24);
			sum = (uint16_t)(sum + le16(d + i) + le16(d + i + 2));
		}
		if (sum != le16(p + 0x1c)) return false;
		// the four 12-byte parts (growth, attacks, EVs and condition, miscellaneous) in one of the 24 orders, by the
		// personality value mod 24; the orders run in the lexicographic order of the parts' permutations
		int order[4] = { 0, 1, 2, 3 };
		for (uint32_t k = pid % 24; k > 0; k--) std::next_permutation(order, order + 4);
		const uint8_t* part[4];
		for (int pos = 0; pos < 4; pos++) part[order[pos]] = d + pos * 12;
		m.species = le16(part[0]);
		if (m.species == 0) return false;
		m.item = le16(part[0] + 2);
		m.ppBonuses = part[0][8];
		for (int i = 0; i < 4; i++) { m.moves[i] = le16(part[1] + i * 2); m.pp[i] = part[1][8 + i]; }
		m.egg = (p[0x13] & 4) || (le32(part[3] + 4) >> 30 & 1);
		m.nick = decodeText(p + 8, 10);
		m.status = le32(p + 0x50);
		m.level = p[0x54];
		m.hp = le16(p + 0x56);
		m.maxHp = le16(p + 0x58);
		return true;
	}

	bool decodeBattler(const uint8_t* p, Mon& m)
	{
		m = Mon();
		m.species = le16(p);
		if (m.species == 0 || m.species > 450) return false;
		for (int i = 0; i < 4; i++) { m.moves[i] = le16(p + 0x0c + i * 2); m.pp[i] = p[0x24 + i]; }
		m.types[0] = p[0x21]; m.types[1] = p[0x22];
		m.hp = le16(p + 0x28);
		m.level = p[0x2a];
		m.maxHp = le16(p + 0x2c);
		m.item = le16(p + 0x2e);
		m.nick = decodeText(p + 0x30, 10);
		m.ppBonuses = p[0x3b];
		m.status = le32(p + 0x4c);
		m.egg = p[0x17] & 0x40;
		return true;
	}

	const char* statusName(uint32_t s)
	{
		if (s & 7) return "SLP";
		if (s & 0x80) return "PSN";		// (badly)
		if (s & 0x08) return "PSN";
		if (s & 0x10) return "BRN";
		if (s & 0x20) return "FRZ";
		if (s & 0x40) return "PAR";
		return "";
	}

	int maxPP(int base, uint8_t bonuses, int slot) { return base + base * ((bonuses >> (2 * slot)) & 3) / 5; }

	std::string RomTables::speciesName(uint16_t i) const
	{
		if (i < species.size()) return species[i];
		return "#" + std::to_string(i);
	}
	std::string RomTables::moveName(uint16_t i) const
	{
		if (i < moves.size()) return moves[i];
		return "#" + std::to_string(i);
	}

	RomTables findTables(const uint8_t* rom, size_t n)
	{
		RomTables t;
		// species names: 11 bytes each, 412 (none + 411), the first "??????????"
		static const uint8_t q[11] = { 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xff };
		long at = findNames(rom, n, q, 11, 11, 412);
		if (at >= 0) for (int i = 0; i < 412; i++) t.species.push_back(decodeText(rom + at + i * 11, 11));
		// move names: 13 bytes each, 355 (none + 354), the first "-" (with zeros after it in Emerald, more ends in the
		// others)
		static const uint8_t dash[2] = { 0xae, 0xff };
		at = findNames(rom, n, dash, 2, 13, 355);
		if (at >= 0) for (int i = 0; i < 355; i++) t.moves.push_back(decodeText(rom + at + i * 13, 13));
		// base stats: 28 bytes a species, its types at 6 and 7; found by Bulbasaur's and Ivysaur's (HP, Attack, Defense,
		// Speed, Sp. Attack, Sp. Defense, types Grass and Poison)
		at = findRecords(rom, n, { { 45, 49, 49, 45, 65, 65, GRASS, POISON }, { 60, 62, 63, 60, 80, 80, GRASS, POISON } }, 28, 0, 1, 412);
		if (at >= 0) for (int i = 0; i < 412; i++) t.speciesTypes.push_back({ rom[at + i * 28 + 6], rom[at + i * 28 + 7] });
		// moves: 12 bytes each (effect, power, type, accuracy, PP, ...); found by Pound's, Karate Chop's and Double Slap's
		// power, type, accuracy and PP
		at = findRecords(rom, n, { { 40, NORMAL, 100, 35 }, { 50, FIGHTING, 100, 25 }, { 15, NORMAL, 85, 10 } }, 12, 1, 1, 355);
		if (at >= 0)
			for (int i = 0; i < 355; i++)
			{
				const uint8_t* e = rom + at + i * 12;
				t.moveInfo.push_back({ e[1], e[2], e[3], e[4] });
			}
		return t;
	}

	bool parseReply(const std::string& line, uint32_t& addr, std::vector<uint8_t>& bytes)
	{
		static const char P[] = "READ_CORE_MEMORY ";
		if (line.compare(0, sizeof P - 1, P) != 0) return false;
		const char* s = line.c_str() + sizeof P - 1;
		char* e;
		unsigned long a = strtoul(s, &e, 16);
		if (e == s) return false;
		addr = (uint32_t)a;
		bytes.clear();
		s = e;
		while (*s == ' ') s++;
		if (*s == '-') return true;	// "-1 no memory map defined"
		while (*s)
		{
			unsigned long v = strtoul(s, &e, 16);
			if (e == s || v > 0xff) break;
			bytes.push_back((uint8_t)v);
			s = e;
			while (*s == ' ') s++;
		}
		return true;
	}

	namespace
	{
		bool endsWithI(const std::string& s, const char* suf)
		{
			size_t n = strlen(suf);
			if (s.size() < n) return false;
			for (size_t i = 0; i < n; i++) if (tolower((unsigned char)s[s.size() - n + i]) != suf[i]) return false;
			return true;
		}
		// the ROM's bytes (limit: the first that many, enough for the header), from the file or the .gba in a .zip
		std::vector<uint8_t> readRom(const std::string& path, size_t limit)
		{
			std::vector<uint8_t> out;
			if (endsWithI(path, ".zip"))
			{
				readZip(path, [](const std::vector<rnds::ZipMember>& ms)
				{
					for (size_t i = 0; i < ms.size(); i++) if (endsWithI(ms[i].name, ".gba")) return (int)i;
					return ms.empty() ? -1 : 0;
				}, [&](const uint8_t* p, size_t n)
				{
					out.insert(out.end(), p, p + std::min(n, limit - out.size()));
					return out.size() < limit;
				});
				return out;
			}
			FILE* f = fopen(path.c_str(), "rb");
			if (!f) return out;
			if (fseeko(f, 0, SEEK_END) == 0)
			{
				long long n = ftello(f);
				if (n > 0) out.resize((size_t)std::min<long long>(n, (long long)limit));
			}
			if (fseeko(f, 0, SEEK_SET) != 0 || fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
			fclose(f);
			return out;
		}
	}

	bool Companion::open(const std::string& rom)
	{
		close();
		std::vector<uint8_t> h = readRom(rom, 0xc0);
		info = detectGame(h.data(), h.size());
		if (!info) return false;
		sh = std::make_shared<Shared>();
		std::shared_ptr<Shared> s = sh;
		std::thread([s, rom]
		{
			setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);	// (this thread only: beside the game starting)
			std::vector<uint8_t> all = readRom(rom, 32u << 20);
			auto t = std::make_shared<RomTables>(findTables(all.data(), all.size()));
			fprintf(stderr, "pokemon: %zu species, %zu moves, %zu species types, %zu move records from the ROM (%zu bytes)\n",
				t->species.size(), t->moves.size(), t->speciesTypes.size(), t->moveInfo.size(), all.size());
			std::lock_guard<std::mutex> l(s->mu);
			s->tables = t;
			s->ready = true;
		}).detach();
		return true;
	}

	void Companion::close()
	{
		info = nullptr;
		sh.reset();
		tablesSeen = false;
		nextAt = sentAt = 0;
		errorSince = -1;
		waiting = 0;
		part.clear();
		snap = next = Snapshot();
		changed = false;
	}

	void Companion::setTables(RomTables t)
	{
		sh = std::make_shared<Shared>();
		sh->tables = std::make_shared<RomTables>(std::move(t));
		sh->ready = true;
	}

	std::shared_ptr<const RomTables> Companion::tables() const
	{
		if (!sh) return nullptr;
		std::lock_guard<std::mutex> l(sh->mu);
		return sh->tables;
	}

	std::string Companion::requests(double now)
	{
		if (!info) return "";
		if (!tablesSeen && sh) { std::lock_guard<std::mutex> l(sh->mu); if (sh->ready) { tablesSeen = true; changed = true; } }
		if (now < nextAt || (waiting > 0 && now - sentAt < 10000)) return "";
		count.clear(); party.clear(); flags.clear(); mainB.clear(); battle.clear();
		next = Snapshot();
		wantBattle = snap.inBattle;
		char b[256];
		int n = snprintf(b, sizeof b, "READ_CORE_MEMORY %x 1\nREAD_CORE_MEMORY %x 1\nREAD_CORE_MEMORY %x %d\n",
			info->mainFlags, info->partyCount, info->party, 6 * PARTY);
		waiting = 3;
		if (wantBattle)
		{
			n += snprintf(b + n, sizeof b - n, "READ_CORE_MEMORY %x 4\nREAD_CORE_MEMORY %x %d\n", info->battleFlags, info->battleMons, 4 * BATTLER);
			waiting += 2;
		}
		sentAt = now;
		nextAt = now + 250;
		return std::string(b, n);
	}

	void Companion::feed(const char* data, size_t n)
	{
		part.append(data, n);
		size_t nl;
		while ((nl = part.find('\n')) != std::string::npos)
		{
			std::string ln = part.substr(0, nl);
			part.erase(0, nl + 1);
			uint32_t a;
			std::vector<uint8_t> v;
			if (!info || !parseReply(ln, a, v)) continue;
			if (v.empty() && next.error.empty())
			{
				size_t at = ln.find("-1");
				next.error = at == std::string::npos ? "no data" : ln.substr(std::min(ln.size(), at + 3));
			}
			if (a == info->mainFlags) mainB = v;
			else if (a == info->partyCount) count = v;
			else if (a == info->party) party = v;
			else if (a == info->battleFlags) flags = v;
			else if (a == info->battleMons) battle = v;
			else continue;
			if (waiting > 0 && --waiting == 0) finish();
		}
		if (part.size() > 65536) part.clear();	// (a line with no end: not ours)
	}

	void Companion::finish()
	{
		Snapshot s = next;
		s.seen = !mainB.empty() && !party.empty();
		std::shared_ptr<const RomTables> t = tables();
		auto typed = [&](Mon& m)
		{
			if (m.types[0] == 255 && t && m.species < t->speciesTypes.size())
			{
				m.types[0] = t->speciesTypes[m.species][0];
				m.types[1] = t->speciesTypes[m.species][1];
			}
		};
		if (s.seen)
		{
			s.inBattle = mainB[0] & 2;
			int c = count.empty() ? 6 : std::min<int>(count[0], 6);
			for (int i = 0; i < c && (size_t)(i + 1) * PARTY <= party.size(); i++)
			{
				Mon m;
				if (decodeParty(party.data() + i * PARTY, m)) { typed(m); s.party.push_back(m); }
			}
			if (s.inBattle && wantBattle && flags.size() >= 4 && battle.size() >= 4 * (size_t)BATTLER)
			{
				s.doubles = flags[0] & 1;
				decodeBattler(battle.data(), s.lead);
				for (int b : { 1, 3 })
				{
					if (b == 3 && !s.doubles) break;
					Mon m;
					if (decodeBattler(battle.data() + b * BATTLER, m)) s.foes.push_back(m);
				}
			}
			else if (s.inBattle) { s.lead = snap.lead; s.foes = snap.foes; s.doubles = snap.doubles; }	// (read in the next set)
			s.error.clear();
			errorSince = -1;
		}
		else if (!s.error.empty())
		{
			// said once it has lasted 5 s (the first sets can come before the game is loaded)
			if (errorSince < 0) errorSince = sentAt;
			if (sentAt - errorSince < 5000) s.error.clear();
		}
		// changed: anything the panel shows
		auto key = [](const Snapshot& x)
		{
			std::string k = std::to_string(x.seen) + x.error + std::to_string(x.inBattle) + std::to_string(x.doubles);
			auto one = [&](const Mon& m)
			{
				k += m.nick + "|" + std::to_string(m.species) + "," + std::to_string(m.level) + "," + std::to_string(m.hp) + "," +
					std::to_string(m.maxHp) + "," + std::to_string(m.status) + "," + std::to_string(m.types[0]) + "," + std::to_string(m.types[1]);
				for (int i = 0; i < 4; i++) k += "," + std::to_string(m.moves[i]) + ":" + std::to_string(m.pp[i]);
				k += ";";
			};
			for (auto& m : x.party) one(m);
			one(x.lead);
			for (auto& m : x.foes) one(m);
			return k;
		};
		if (key(s) != key(snap)) changed = true;
		snap = s;
	}
}
