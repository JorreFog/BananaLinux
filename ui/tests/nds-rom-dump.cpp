// What the companion's ROM reader makes of a ROM, as text (and sprites as PNGs), for looking at by eye and for the
// other workstreams' fixtures. Never commit what it prints: it is the game's own data.
//   make tests/nds-rom-dump && tests/nds-rom-dump <rom.nds> species|moves|items|zones|zone <id>|trainers|sprite <n> <out.png> [shiny] [female] [form]
#include "nds/rom.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

using namespace banana::nds;

static const char* TYPES[] = { "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug", "Ghost", "Steel", "Fire", "Water",
	"Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark" };
static const char* type(Type t) { return t < NUM_TYPES ? TYPES[t] : "???"; }
static const char* KINDS[] = { "grass", "surf", "old rod", "good rod", "super rod", "rock smash", "headbutt", "swarm", "radar", "dual slot",
	"season", "grotto", "special", "dark grass", "shaking grass", "ripple surf", "ripple fish", "hoenn sound", "sinnoh sound", "night fish" };
static const char* TIMES[] = { "", " morning", " day", " night" };

static std::string name(const RomData& r, uint16_t s) { return s < r.species.size() ? r.species[s].name : "#" + std::to_string(s); }
static std::string move(const RomData& r, uint16_t m) { return m < r.moves.size() ? r.moves[m].name : "#" + std::to_string(m); }

int main(int argc, char** argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s rom.nds species|moves|items|zones|zone <id>|trainers|sprite <n> <out.png> [shiny]\n", argv[0]); return 2; }
	std::string err;
	RomLoadInfo info;
	auto r = loadRom(argv[1], "", &err, &info);
	if (!r) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
	std::string what = argv[2];
	if (what == "species")
		for (size_t i = 0; i < r->species.size(); i++)
		{
			auto& s = r->species[i];
			printf("%zu %s [%s] %s/%s %d/%d/%d/%d/%d/%d catch %d ab %s/%s/%s evo:", i, s.name.c_str(), s.category.c_str(), type(s.types[0]), type(s.types[1]),
				s.base[0], s.base[1], s.base[2], s.base[3], s.base[4], s.base[5], s.catchRate,
				s.abilities[0] < r->abilities.size() ? r->abilities[s.abilities[0]].c_str() : "?", s.abilities[1] < r->abilities.size() ? r->abilities[s.abilities[1]].c_str() : "?",
				s.abilities[2] < r->abilities.size() ? r->abilities[s.abilities[2]].c_str() : "?");
			for (auto& e : s.evolutions) printf(" [%s -> %s]", e.how.c_str(), name(*r, e.into).c_str());
			printf(" moves %zu tms %zu\n  %s\n", s.learnset.size(), s.tms.size(), s.dexText.c_str());
		}
	else if (what == "moves")
		for (size_t i = 0; i < r->moves.size(); i++)
		{
			auto& m = r->moves[i];
			printf("%zu %s %s cat%d pow %d acc %d pp %d prio %d: %s\n", i, m.name.c_str(), type(m.type), (int)m.cat, m.power, m.accuracy, m.pp, (int8_t)m.priority, m.description.c_str());
		}
	else if (what == "items")
		for (size_t i = 0; i < r->items.size(); i++)
			printf("%zu %s pocket %d price %d: %s\n", i, r->items[i].name.c_str(), r->items[i].pocket, r->items[i].price, r->items[i].description.c_str());
	else if (what == "zones")
		for (auto& z : r->zones) printf("%u %s (%zu)\n", z.first, z.second.name.c_str(), z.second.encounters.size());
	else if (what == "zone" && argc > 3)
	{
		auto it = r->zones.find((uint16_t)atoi(argv[3]));
		if (it == r->zones.end()) return 1;
		printf("%s\n", it->second.name.c_str());
		for (auto& e : it->second.encounters)
			printf("  %-14s %-12s L%d-%d %d%%%s%s%s\n", KINDS[(int)e.kind], name(*r, e.species).c_str(), e.minLevel, e.maxLevel, e.percent, TIMES[(int)e.time],
				e.season != 0xff ? (" season " + std::to_string(e.season)).c_str() : "", e.form ? (" form " + std::to_string(e.form)).c_str() : "");
	}
	else if (what == "trainers")
		for (auto& t : r->trainers)
		{
			printf("%u %s %s%s:", t.first, t.second.cls.c_str(), t.second.name.c_str(), t.second.doubleBattle ? " (double)" : "");
			for (auto& m : t.second.team)
			{
				printf(" %s L%d", name(*r, m.species).c_str(), m.level);
				if (m.item && m.item < r->items.size()) printf(" @%s", r->items[m.item].name.c_str());
				printf(" (%s/%s/%s/%s)", move(*r, m.moves[0]).c_str(), move(*r, m.moves[1]).c_str(), move(*r, m.moves[2]).c_str(), move(*r, m.moves[3]).c_str());
			}
			printf("\n");
		}
	else if (what == "sprite" && argc > 4)
	{
		// options after the PNG: shiny, female, a form number
		bool shiny = false, female = false;
		int form = 0;
		for (int i = 5; i < argc; i++) { if (!strcmp(argv[i], "shiny")) shiny = true; else if (!strcmp(argv[i], "female")) female = true; else form = atoi(argv[i]); }
		auto s = r->sprite((uint16_t)atoi(argv[3]), female, (uint8_t)form);
		if (!s || s->frames.empty()) { fprintf(stderr, "no sprite\n"); return 1; }
		// the frames side by side (8 at most, spread over the animation), on grey
		std::vector<size_t> pick;
		size_t nf = s->frames.size();
		for (size_t k = 0; k < std::min<size_t>(8, nf); k++) pick.push_back(nf <= 8 ? k : k * (nf - 1) / 7);
		int W = s->w * (int)pick.size(), H = s->h;
		std::vector<uint8_t> rgba((size_t)W * H * 4, 0);
		for (size_t i = 0; i < rgba.size(); i += 4) { rgba[i] = rgba[i + 1] = rgba[i + 2] = 0x99; rgba[i + 3] = 255; }
		for (size_t f = 0; f < pick.size(); f++)
			for (int y = 0; y < s->h; y++)
				for (int x = 0; x < s->w; x++)
				{
					uint8_t v = s->frames[pick[f]][(size_t)(y * s->w + x)];
					uint32_t c = (shiny ? s->shiny : s->normal)[v];
					uint8_t* p = &rgba[((size_t)y * W + f * s->w + x) * 4];
					if (v) { p[0] = c >> 16; p[1] = c >> 8; p[2] = (uint8_t)c; }
				}
		stbi_write_png(argv[4], W, H, 4, rgba.data(), W * 4);
		printf("%dx%d, %zu frames, %zu steps\n", s->w, s->h, s->frames.size(), s->anim.size());
	}
	else { fprintf(stderr, "what?\n"); return 2; }
	return 0;
}
