// The game companion's engine (gamecomp.h) on made-up GBA memory: matching a ROM to its definition (header code, CRC-32
// for a hack, never a Pokémon game), the reads it asks RetroArch for (few, coalesced), the values decoded from the
// replies, pointers that move, RetroArch's error replies, and "changed" only when something changed. With the
// definitions in tests/companions (real ones from the companion data). It leaves made-up ROMs and RetroArch replies in
// $1 for tests/run.sh's game panel run. The companion idea is PokeDaisy's (Lidor Itzhari).
#include "gamecomp.h"
#include "json.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <zlib.h>

using namespace banana;
using namespace banana::gcomp;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fails++; } } while (0)

// a GBA's memory as RetroArch's gpSP core maps it
struct FakeGba
{
	std::vector<uint8_t> ewram = std::vector<uint8_t>(0x40000), iwram = std::vector<uint8_t>(0x8000);
	int reads = 0;
	uint8_t* at(uint32_t a)
	{
		if (a >= 0x02000000 && a < 0x02040000) return &ewram[a - 0x02000000];
		if (a >= 0x03000000 && a < 0x03008000) return &iwram[a - 0x03000000];
		return nullptr;
	}
	void put(uint32_t a, uint32_t v, int n) { for (int i = 0; i < n; i++) at(a)[i] = (uint8_t)(v >> (8 * i)); }
	void text(uint32_t a, const char* s) { memcpy(at(a), s, strlen(s) + 1); }
	// RetroArch's answers to the requests (READ_CORE_MEMORY <hex addr> <len>)
	std::string answer(const std::string& req)
	{
		std::istringstream in(req);
		std::string ln, out;
		while (std::getline(in, ln))
		{
			unsigned a; int n;
			if (sscanf(ln.c_str(), "READ_CORE_MEMORY %x %d", &a, &n) != 2) continue;
			reads++;
			char b[64];
			snprintf(b, sizeof b, "READ_CORE_MEMORY %x", a);
			out += b;
			uint8_t* p = at(a);
			if (!p) { out += " -1 no descriptor for address\n"; continue; }
			uint32_t end = a < 0x03000000 ? 0x02040000 : 0x03008000;
			n = std::min<long>(n, (long)(end - a));
			for (int i = 0; i < n; i++) { snprintf(b, sizeof b, " %02X", p[i]); out += b; }
			out += "\n";
		}
		return out;
	}
};

static std::vector<uint8_t> makeRom(const char* title, const char* code, size_t size = 0x400)
{
	std::vector<uint8_t> r(size);
	for (size_t i = 0; i < size; i++) r[i] = (uint8_t)(i * 7);
	memset(&r[0xa0], 0, 12);
	memcpy(&r[0xa0], title, strlen(title));
	memcpy(&r[0xac], code, 4);
	return r;
}
static void writeFile(const std::string& p, const std::vector<uint8_t>& d) { std::ofstream(p, std::ios::binary).write((const char*)d.data(), d.size()); }
static void writeText(const std::string& p, const std::string& d) { std::ofstream(p) << d; }

// one set: the requests, the fake's answers, fed back in pieces (a pipe gives what it has)
static std::string oneSet(Engine& e, FakeGba& g, double& now)
{
	now += 300;
	std::string r = e.requests(now);
	std::string a = g.answer(r);
	for (size_t i = 0; i < a.size(); i += 77) e.feed(a.data() + i, std::min<size_t>(77, a.size() - i));
	return a;
}
static double num(const Engine& e, const char* var) { auto it = e.snapshot().vals.find(var); return it != e.snapshot().vals.end() && it->second.ok ? it->second.v.num() : -12345; }
static std::string str(const Engine& e, const char* var) { auto it = e.snapshot().vals.find(var); return it != e.snapshot().vals.end() && it->second.ok ? it->second.v.str() : "<n/a>"; }

int main(int argc, char** argv)
{
	std::string out = argc > 1 ? argv[1] : "/tmp";
	std::string defs = argc > 2 ? argv[2] : "tests/companions";	// (tests/run.sh: the folder's full path)
	std::string why;

	// ---- every definition shipped for the tests parses
	for (const char* f : { "castlevania-aria-of-sorrow", "golden-sun", "tetris-worlds" })
	{
		auto d = loadDef(defs + "/" + f + ".json", &why);
		CHECK(d, "%s: %s", f, why.c_str());
	}
	auto tetris = loadDef(defs + "/tetris-worlds.json");
	CHECK(tetris && tetris->pages[P_BAG].reference, "Tetris Worlds' pages are reference pages");

	// ---- matching: the header's code and title, a CRC-32 for a hack, the index, never a Pokémon game
	std::string cvRom = out + "/gc-castlevania.gba", gsRom = out + "/gc-goldensun.gba", tetRom = out + "/gc-tetris.gba";
	writeFile(cvRom, makeRom("CASTLEVANIA2", "A2CE"));
	writeFile(gsRom, makeRom("Golden_Sun_A", "AGSE"));
	writeFile(tetRom, makeRom("TETRISWORLDS", "ATWE"));
	writeFile(out + "/gc-emerald.gba", makeRom("POKEMON EMER", "BPEE"));
	writeFile(out + "/gc-unknown.gba", makeRom("NOTHINGHERE", "ZZZE"));
	CHECK(findDef(defs, cvRom) == defs + "/castlevania-aria-of-sorrow.json", "Castlevania by its header: %s", findDef(defs, cvRom).c_str());
	CHECK(findDef(defs, gsRom) == defs + "/golden-sun.json", "Golden Sun by its header");
	CHECK(findDef(defs, out + "/gc-emerald.gba", &why).empty() && why.find("Pokémon") != std::string::npos, "Emerald is the Pokémon companion's: %s", why.c_str());
	CHECK(findDef(defs, out + "/gc-unknown.gba").empty(), "an unknown game: no definition");
	{
		// a translation: an exclusive definition matched by CRC-32 only, its header the base game's; the index too
		std::string dir = out + "/gc-defs";
		system(("rm -rf '" + dir + "' && mkdir -p '" + dir + "' && cp '" + defs + "'/*.json '" + dir + "/'").c_str());
		std::vector<uint8_t> hack = makeRom("CASTLEVANIA2", "A2CE", 0x800);
		hack[0x700] = 0x42;
		uint32_t crc = (uint32_t)crc32(crc32(0L, Z_NULL, 0), hack.data(), (uInt)hack.size());
		char c[16]; snprintf(c, sizeof c, "%08x", crc);
		std::ifstream f(defs + "/castlevania-aria-of-sorrow.json");
		std::stringstream b; b << f.rdbuf();
		std::string j = b.str();
		size_t m = j.find("\"match\"");
		size_t e = j.find('}', m);
		j = j.substr(0, m) + "\"match\": {\"code\": \"A2CE\", \"crc32\": [\"" + c + "\"], \"exclusive\": true}" + j.substr(e + 1);
		j.replace(j.find("\"title\": \""), 10, "\"title\": \"HACK ");
		writeText(dir + "/tr-castlevania.json", j);
		writeFile(out + "/gc-hack.gba", hack);
		CHECK(findDef(dir, out + "/gc-hack.gba") == dir + "/tr-castlevania.json", "the hack by its CRC-32: %s", findDef(dir, out + "/gc-hack.gba").c_str());
		CHECK(findDef(dir, cvRom) == dir + "/castlevania-aria-of-sorrow.json", "the base game is not the hack's");
		writeText(dir + "/index.tsv", makeIndex(dir));
		CHECK(findDef(dir, out + "/gc-hack.gba") == dir + "/tr-castlevania.json", "the hack through index.tsv");
		CHECK(findDef(dir, gsRom) == dir + "/golden-sun.json", "Golden Sun through index.tsv");
	}

	// ---- reading Castlevania: few reads, the values decoded
	FakeGba g;
	g.put(0x02000010, 4, 1);			// state: in the game
	g.put(0x0200009E, 3, 1);			// area
	g.put(0x02013279, 21, 1);			// level
	g.put(0x0201327A, 250, 2); g.put(0x0201327E, 320, 2);	// HP
	g.put(0x0201327C, 80, 2); g.put(0x02013280, 120, 2);	// MP
	g.put(0x02013290, 123456, 4);		// gold
	g.put(0x020000AC, 60 * 3725, 4);	// the time (frames)
	g.put(0x02013392, 0xAB000005, 4);	// abilities (masked to 24 bits)
	for (int i = 0; i < 32; i++) g.put(0x02013294 + i, i % 3 == 0 ? (uint32_t)(i + 1) : 0, 1);	// items
	Engine e;
	CHECK(e.open(cvRom, defs), "Castlevania opens");
	double now = 0;
	CHECK(!e.snapshot().seen, "nothing seen before a reply");
	std::string a = oneSet(e, g, now);
	CHECK(e.snapshot().seen, "seen after the first set");
	CHECK(e.takeChanged(), "the first set changed the panel");
	CHECK(e.reads() <= 6, "the Status page in at most 6 reads (%d)", e.reads());
	CHECK(num(e, "hp") == 250 && num(e, "hpmax") == 320 && num(e, "lv") == 21, "hp %g/%g lv %g", num(e, "hp"), num(e, "hpmax"), num(e, "lv"));
	CHECK(num(e, "gold") == 123456, "gold %g", num(e, "gold"));
	oneSet(e, g, now);
	CHECK(!e.takeChanged(), "the same values again: no redraw");
	g.put(0x0201327A, 249, 2);
	oneSet(e, g, now);
	CHECK(e.takeChanged() && num(e, "hp") == 249, "a hit: changed");
	// not due yet: nothing asked; and a set still waiting for its replies isn't sent again
	CHECK(e.requests(now + 10).empty(), "250 ms between sets");
	std::string pending = e.requests(now + 400);
	CHECK(!pending.empty() && e.requests(now + 800).empty(), "no second set while one is unanswered");
	e.feed(g.answer(pending).data(), g.answer(pending).size());
	// the Bag page: its list (the items with a count)
	e.want(P_BAG);
	oneSet(e, g, now);
	CHECK(e.snapshot().lists.size() == 1 && e.snapshot().lists[0].size() == 11, "the items list: %zu rows", e.snapshot().lists.empty() ? 0 : e.snapshot().lists[0].size());
	e.want(P_COLLECTION);
	oneSet(e, g, now);
	CHECK(num(e, "ability") == 5, "the mask: %g", num(e, "ability"));
	// replies for tests/run.sh's game panel: one file a page (each page asks for its own values)
	g.reads = 0;
	for (int p = 0; p < NUM_PAGES; p++)
	{
		Engine x;
		x.open(cvRom, defs);
		x.want(p);
		writeText(out + "/gc-cv-" + pageIdName(p) + ".txt", g.answer(x.requests(1e9)));
	}

	// ---- Golden Sun: the party list (text names, rowWhen)
	FakeGba gs;
	const char* names[4] = { "Isaac", "Garet", "Ivan", "Mia" };
	for (int i = 0; i < 3; i++)	// (Mia not met yet: level 0)
	{
		uint32_t b = 0x02000500 + i * 332;
		gs.text(b, names[i]);
		gs.put(b + 15, 10 + i, 1);
		gs.put(b + 56, 60 + i, 2); gs.put(b + 52, 90, 2);
		gs.put(b + 58, 20, 2); gs.put(b + 54, 30, 2);
	}
	Engine e2;
	CHECK(e2.open(gsRom, defs), "Golden Sun opens");
	e2.want(P_DETAIL);
	oneSet(e2, gs, now);
	const auto& L = e2.snapshot().lists;
	CHECK(L.size() == 1 && L[0].size() == 3, "the party: 3 members (%zu)", L.empty() ? 0 : L[0].size());
	if (L.size() == 1 && L[0].size() == 3)
		CHECK(L[0][1][0].v.str() == "Garet" && L[0][1][1].v.num() == 11, "Garet, level 11: %s %g", L[0][1][0].v.str().c_str(), L[0][1][1].v.num());
	for (int p = 0; p < NUM_PAGES; p++)
	{
		Engine x;
		x.open(gsRom, defs);
		x.want(p);
		writeText(out + "/gc-gs-" + pageIdName(p) + ".txt", gs.answer(x.requests(1e9)));
	}

	// ---- a made-up definition: types, BCD, signed, text from the ROM, a pointer that moves, coalescing, an error
	{
		std::string rom = out + "/gc-synth.gba";
		std::vector<uint8_t> r = makeRom("SYNTH", "SYNE", 0x2000);
		memcpy(&r[0x1000], "GREEN HILL", 11);
		writeFile(rom, r);
		const char* J = R"J({"schema": 1, "title": "Synth", "match": {"code": "SYNE"}, "accent": "#3080f0", "quality": "live-unverified", "sources": [],
			"vars": {
				"a": {"addr": "0x02000000", "type": "u8"}, "b": {"addr": "0x02000010", "type": "s16"},
				"c": {"addr": "0x02000100", "type": "bcd32"}, "far": {"addr": "0x02030000", "type": "u16", "mul": 2, "add": 1},
				"iw": {"addr": "0x03007FF0", "type": "u32", "shift": 4, "mask": "0xF0"},
				"zone": {"addr": "0x08001000", "type": "text", "len": 12},
				"boss": {"ptr": ["0x03000100", 8], "type": "u16"},
				"fight": {"expr": "boss > 0 && bit(a, 1)"}, "sum": {"expr": "a + b"}, "loop1": {"expr": "loop2 + 1"}, "loop2": {"expr": "loop1"}
			},
			"enums": {"names": {"0": "Alpha", "1": "Beta", "2": "Gamma"}},
			"pages": [
				{"id": "status", "title": "S", "widgets": [{"type": "number", "value": "sum"}, {"type": "number", "value": "c"}, {"type": "number", "value": "far"},
					{"type": "number", "value": "iw"}, {"type": "text", "value": "zone"}, {"type": "number", "value": "loop1"}]},
				{"id": "detail", "title": "D", "widgets": [{"type": "static", "text": "x"}]},
				{"id": "battle", "title": "B", "when": "fight", "widgets": [{"type": "bar", "value": "boss", "max": 100}]},
				{"id": "map", "title": "M", "widgets": [{"type": "static", "text": "x"}]},
				{"id": "bag", "title": "B", "widgets": [{"type": "list", "base": "0x02000200", "stride": 2, "n": 3, "rowWhen": "cnt > 0",
					"fields": [{"name": "who", "type": "index", "enum": "names"}, {"name": "cnt", "off": 1, "type": "u8"}]}]},
				{"id": "collection", "title": "C", "widgets": [{"type": "static", "text": "x"}]},
				{"id": "progress", "title": "P", "widgets": [{"type": "static", "text": "x"}]}]})J";
		JPtr j = parseJson(J, &why);
		auto d = j ? parseDef(*j, &why) : nullptr;
		CHECK(d, "the made-up definition: %s", why.c_str());
		if (d)
		{
			CHECK(d->romData && !d->pages[P_STATUS].reference && d->pages[P_MAP].reference, "ROM data, live and reference pages");
			FakeGba f;
			f.put(0x02000000, 2, 1); f.put(0x02000010, (uint32_t)-5, 2); f.put(0x02000100, 0x00123456, 4);
			f.put(0x02030000, 7, 2); f.put(0x03007FF0, 0xA5, 4);
			f.put(0x03000100, 0x02001000, 4); f.put(0x02001008, 40, 2);
			Engine s;
			s.openDef(d, rom);
			for (int i = 0; i < 100 && str(s, "zone") != "GREEN HILL"; i++) { oneSet(s, f, now); usleep(10000); }
			CHECK(num(s, "sum") == -3 && num(s, "c") == 123456 && num(s, "far") == 15 && num(s, "iw") == 10, "sum %g bcd %g far %g iw %g",
				num(s, "sum"), num(s, "c"), num(s, "far"), num(s, "iw"));
			CHECK(str(s, "zone") == "GREEN HILL", "text from the ROM: %s", str(s, "zone").c_str());
			CHECK(str(s, "loop1") == "<n/a>", "a loop of expressions: not available");
			// four places: 0x02000000-0x02000103 (one read: within 256 bytes), 0x02001008 (the pointer's), 0x02030000,
			// 0x03000100 + 0x03007FF0 (not merged: further apart)
			CHECK(s.reads() == 5, "coalesced into 5 reads: %d", s.reads());
			CHECK(num(s, "boss") == 40 && s.pageOn(P_BATTLE), "the pointer's value: %g, in a fight", num(s, "boss"));
			// the pointer moves: the old value kept for a set, then the new place read
			f.put(0x03000100, 0x02002000, 4); f.put(0x02002008, 77, 2);
			oneSet(s, f, now);
			CHECK(num(s, "boss") == 40, "moved: the old value kept for a set (%g)", num(s, "boss"));
			std::string rq = s.requests(now + 1);
			CHECK(!rq.empty(), "moved: asked again at once");
			std::string an = f.answer(rq);
			s.feed(an.data(), an.size());
			CHECK(num(s, "boss") == 77, "moved: the new place read (%g)", num(s, "boss"));
			Engine s2;
			s2.openDef(d, rom);
			// RetroArch with no memory map: an error, nothing seen
			std::string req = s2.requests(1000);
			std::string err;
			for (size_t at = 0; (at = req.find("READ_CORE_MEMORY ", at)) != std::string::npos; at++)
			{
				unsigned ad; sscanf(req.c_str() + at, "READ_CORE_MEMORY %x", &ad);
				char b[96]; snprintf(b, sizeof b, "READ_CORE_MEMORY %x -1 no memory map defined\n", ad); err += b;
			}
			s2.feed(err.data(), err.size());
			CHECK(!s2.snapshot().seen && s2.snapshot().error == "no memory map defined", "the error reply: '%s'", s2.snapshot().error.c_str());
			// a pointer of 0: not available, the battle page off
			f.put(0x03000100, 0, 4);
			oneSet(s, f, now); oneSet(s, f, now);
			CHECK(num(s, "boss") == -12345 && !s.pageOn(P_BATTLE), "a null pointer: not available");
			// a list whose rows are named by their number (an index field)
			f.put(0x02000201, 5, 1); f.put(0x02000205, 9, 1);
			s.want(P_BAG);
			oneSet(s, f, now);
			const auto& rows = s.snapshot().lists[0];
			CHECK(rows.size() == 2 && rows[0][0].v.num() == 0 && rows[1][0].v.num() == 2 && rows[1][1].v.num() == 9 && d->enumLabel("names", 2) == "Gamma",
				"index rows: %zu", rows.size());
		}
	}
	// a reference-only game: nothing to read, seen at once
	{
		Engine t;
		CHECK(t.open(tetRom, defs), "Tetris Worlds opens");
		CHECK(t.requests(0).empty() && t.snapshot().seen, "a reference game reads nothing and is shown at once");
	}
	// every definition the system ships (argv[3]: rootfs/usr/share/banana/companions) loads in the engine, and a ROM
	// with its header finds it (no other definition takes its code)
	if (argc > 3)
	{
		std::string dir = argv[3];
		std::istringstream idx(makeIndex(dir));
		std::string ln;
		int n = 0;
		while (std::getline(idx, ln))
		{
			if (ln.empty() || ln[0] == '#') continue;
			std::string file = ln.substr(0, ln.find('\t'));
			auto d = loadDef(dir + "/" + file, &why);
			CHECK(d, "%s: %s", file.c_str(), why.c_str());
			n++;
			if (!d) continue;
			// the header from the index line: codes, title12
			std::vector<std::string> c;
			size_t a = 0, b;
			while ((b = ln.find('\t', a)) != std::string::npos) { c.push_back(ln.substr(a, b - a)); a = b + 1; }
			c.push_back(ln.substr(a));
			if (c.size() < 6 || c[1].empty() || c[4] == "1") continue;
			std::string code = c[1].substr(0, 4);
			if (code == "AXVE" || code == "AXPE" || code == "BPEE" || code == "BPRE" || code == "BPGE") continue;
			writeFile(out + "/gc-any.gba", makeRom(c[2].c_str(), code.c_str()));
			std::string got = findDef(dir, out + "/gc-any.gba");
			CHECK(got == dir + "/" + file, "%s (%s \"%s\") is found as %s", file.c_str(), code.c_str(), c[2].c_str(), got.c_str());
		}
		fprintf(stderr, "gamecomp-test: %d shipped definitions\n", n);
	}
	if (fails) { fprintf(stderr, "%d checks failed\n", fails); return 1; }
	fprintf(stderr, "gamecomp-test: all passed\n");
	return 0;
}
