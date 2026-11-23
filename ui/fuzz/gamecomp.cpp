// Fuzz target: the game companion (gamecomp.cpp): a definition on the card (/data/banana/companions, the card's own
// before the image's), its index.tsv, the game's ROM, and the game's memory as RetroArch answers for it. The input is
// those four parts, split at "\xff\xfe\xfd\xfc": the definition (JSON), the memory (repeated to fill the address space;
// a byte 0xEE where a read starts answers with RetroArch's error), the ROM (its header and ROM-held values), index.tsv.
// The definition is parsed, matched against the ROM, and its pages read a few sets each (every list opened), with every
// widget's values evaluated as the panel draws them. System maps are the image's (systemMap() keeps what it read).
#include "../src/gamecomp.h"
#include "../src/json.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace banana;
using namespace banana::gcomp;

static std::string dir;

static void put(const std::string& path, const std::string& s)
{
	if (FILE* f = fopen(path.c_str(), "wb")) { fwrite(s.data(), 1, s.size(), f); fclose(f); }
}

// RetroArch's answers to READ_CORE_MEMORY <hex addr> <len>, from the input's memory
static std::string answer(const std::string& req, const std::string& mem)
{
	std::string out;
	size_t at = 0;
	while (at < req.size())
	{
		size_t nl = req.find('\n', at);
		std::string ln = req.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
		at = nl == std::string::npos ? req.size() : nl + 1;
		unsigned a;
		int n;
		if (sscanf(ln.c_str(), "READ_CORE_MEMORY %x %d", &a, &n) != 2) continue;
		char b[64];
		snprintf(b, sizeof b, "READ_CORE_MEMORY %x", a);
		out += b;
		if (mem.empty() || (uint8_t)mem[a % mem.size()] == 0xee) { out += " -1 no descriptor for address\n"; continue; }
		for (int i = 0; i < n && i < 8192; i++) { snprintf(b, sizeof b, " %02X", (uint8_t)mem[(a + (unsigned)i) % mem.size()]); out += b; }
		out += "\n";
	}
	return out;
}

static void evalAll(const Engine& e, const Def& d)
{
	for (int p = 0; p < NUM_PAGES; p++)
	{
		e.pageOn(p);
		for (auto& w : d.pages[p].widgets)
		{
			for (const NumRef* r : { &w.value, &w.max, &w.total, &w.n, &w.when, &w.rowWhen }) e.eval(*r);
			if (!w.enumName.empty()) d.enumLabel(w.enumName, 1);
		}
	}
}

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
	dir = "/tmp/banana-fuzz-gc-" + std::to_string(getpid());
	mkdir(dir.c_str(), 0755);
	mkdir((dir + "/defs").c_str(), 0755);
	if (const char* s = getenv("BANANA_FUZZ_SYSTEMS_DIR")) setSystemsDir(s);
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	std::string in((const char*)data, size), part[4];
	static const char SEP[] = "\xff\xfe\xfd\xfc";
	size_t at = 0;
	for (int k = 0; k < 4; k++)
	{
		size_t e = k < 3 ? in.find(SEP, at, 4) : std::string::npos;
		part[k] = in.substr(at, e == std::string::npos ? std::string::npos : e - at);
		if (e == std::string::npos) break;
		at = e + 4;
	}
	const std::string rom = dir + "/game.gba", defs = dir + "/defs";
	put(rom, part[2]);
	put(defs + "/game.json", part[0]);
	put(defs + "/index.tsv", part[3]);
	// the card's index and the ROM's header: which definition is this game's
	RomId id;
	romId(rom, id, true);
	findDef(defs, rom);
	JPtr j = parseJson(part[0]);
	if (!j) return 0;
	std::string why;
	auto d = parseDef(*j, &why);
	if (!d) return 0;
	Engine e;
	e.openDef(d, rom);
	double now = 0;
	for (int p = 0; p < NUM_PAGES; p++)
	{
		std::vector<int> lists = { -1 };
		for (auto& w : d->pages[p].widgets) if (w.listId >= 0) lists.push_back(w.listId);
		for (int l : lists)
		{
			e.want(p, l);
			for (int set = 0; set < 2; set++)
			{
				now += 300;
				std::string a = answer(e.requests(now), part[1]);
				// (fed in pieces, as a pipe gives them)
				for (size_t i = 0; i < a.size(); i += 61) e.feed(a.data() + i, std::min<size_t>(61, a.size() - i));
			}
			evalAll(e, *d);
		}
	}
	e.close();
	return 0;
}
