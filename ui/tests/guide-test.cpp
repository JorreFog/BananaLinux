// The DS Pokémon companion's guide loader (src/nds/guide.cpp) on a made-up file in the real schema
// (tests/nds-guide/platinum.json), and, when given the folder, on the shipped data files.
//   g++ -std=c++17 -Isrc -o guide-test tests/guide-test.cpp src/nds/guide.cpp src/json.cpp && ./guide-test tests/nds-guide [shipped dir]
#include "nds/guide.h"
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

using namespace banana::nds;

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// the pieces of RomData the resolver reads; the rest is empty
struct FakeRom : RomData
{
	std::shared_ptr<const Sprite> sprite(uint16_t, bool, uint8_t) const override { return nullptr; }
};

static bool has(const std::vector<uint16_t>& v, uint16_t z) { for (uint16_t x : v) if (x == z) return true; return false; }

int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "FAIL usage: guide-test <fixture dir> [shipped dir]\n"); return 2; }
	std::string dir = argv[1];

	FakeRom rom;
	rom.zones[200].name = "BETA CITY";				// the ROM's text may differ in case and punctuation
	rom.zones[201].name = "Beta City";				// two maps by that name: both come
	rom.zones[300].name = "Route 1";
	Trainer t; t.id = 5; t.name = "Tester"; t.cls = "Leader"; t.team = { TrainerMon{ 74, 0, 12, {} }, TrainerMon{ 95, 0, 14, {} } };
	rom.trainers[5] = t;
	Trainer r2 = t; r2.id = 6; r2.cls = "Leader (rematch)"; r2.team[1].level = 40;
	rom.trainers[6] = r2;
	Trainer other = t; other.id = 7; other.name = "Someone"; rom.trainers[7] = other;
	GuideResolver res = guideResolver(rom);

	// -- with the resolver
	GuideData g; std::string why;
	CHECK(loadGuideFile(dir + "/platinum.json", g, GameId::PLATINUM, &why, &res));
	CHECK(g.game == "platinum" && g.revision == 3 && g.title == "Pokemon Platinum (test)");
	CHECK(g.sources.size() == 1);
	// the doubled id, the entry without text, the secret in an unlisted place and the out-of-range species are skipped
	CHECK(g.milestones.size() == 3);
	CHECK(g.steps.size() == 1 && g.secrets.size() == 1 && g.evolutionNotes.size() == 1 && g.notes.size() == 1);
	CHECK(why.find("4 entries skipped") == 0);
	if (g.milestones.size() == 3)
	{
		const Milestone& m1 = g.milestones[0];
		CHECK(m1.id == "gym-1" && m1.kind == "gym" && m1.badge == 1 && m1.theme == ROCK && m1.levelCap == 14 && m1.leader == "Tester");
		CHECK(m1.place == "Alpha Town" && m1.placeZones.size() == 1 && m1.placeZones[0] == 10);
		CHECK(m1.q == Quality::REFERENCE_ONLY && m1.tips == "Water and grass work.");
		// the foe is found by name, class and the team's top level: the rematch (top 40) and the other trainer aren't it
		CHECK(m1.trainerIds.size() == 1 && m1.trainerIds[0] == 5);
		CHECK(m1.foeName == "Tester" && m1.foeClass == "Leader" && m1.foeAceLevel == 14);
		// opens: Alpha Town (10) + Route 1 (found by name: 300)
		CHECK(m1.reachableZones.size() == 2 && has(m1.reachableZones, 10) && has(m1.reachableZones, 300));
		CHECK(m1.reachablePlaces.size() == 2 && m1.reachablePlaces[0] == "Alpha Town");
		const Milestone& m2 = g.milestones[1];
		CHECK(m2.id == "rival-1" && m2.badge == 0 && m2.doneWhen == "later" && m2.q == Quality::LIVE_VERIFIED);
		CHECK(m2.trainerIds.size() == 1 && m2.trainerIds[0] == 77);		// explicit ids win
		// cumulative: the first milestone's zones, then Beta City (200, 201); Nowhere Land has no id but its name stays
		CHECK(m2.reachableZones.size() == 4 && has(m2.reachableZones, 200) && has(m2.reachableZones, 201));
		CHECK(m2.reachablePlaces.size() == 4 && m2.reachablePlaces[3] == "Nowhere Land");
		const Milestone& m3 = g.milestones[2];
		CHECK(m3.id == "gym-2" && m3.theme == NO_TYPE && m3.badge == 2);			// an unknown type name is "no type"
		CHECK(has(m3.reachableZones, 30) && has(m3.reachableZones, 31) && has(m3.reachableZones, 32) && m3.reachableZones.size() == 7);
		CHECK(m3.reachablePlaces.size() == 5);
		// a list of foes: every one that the ROM has is collected (Tester 5, Someone 7), the first names the entry
		CHECK(m3.trainerIds.size() == 2 && m3.trainerIds[0] == 5 && m3.trainerIds[1] == 7 && m3.foeName == "Tester");										// the unlisted key is ignored
	}
	CHECK(g.unresolved.size() == 1 && g.unresolved[0] == "Nowhere Land");
	if (!g.steps.empty())
		CHECK(g.steps[0].stage == "gym-1" && g.steps[0].places.size() == 2 && has(g.steps[0].zones, 200) && has(g.steps[0].zones, 10));
	if (!g.secrets.empty())
	{
		const Secret& s = g.secrets[0];
		CHECK(s.zone == 30 && s.zones.size() == 3 && s.x == 5 && s.y == 9 && s.condition == "needs Cut" && s.place == "Gamma Cave" && s.grid == "world");
	}
	CHECK(g.evolutionNotes.count(64) && g.evolutionNotes[64] == "Trade it.\nOr the GTS.");
	if (!g.notes.empty())
		CHECK(g.notes[0].species.size() == 2 && g.notes[0].species[0] == 488 && g.notes[0].zones == std::vector<uint16_t>{ 300 });

	// -- without a resolver: ids that are in the file stay, the rest are reported
	GuideData g2;
	CHECK(loadGuideFile(dir + "/platinum.json", g2, GameId::NONE, nullptr, nullptr));
	CHECK(g2.unresolved.size() == 3 && g2.milestones.size() == 3);
	if (!g2.milestones.empty()) CHECK(g2.milestones[0].reachableZones.size() == 1 && g2.milestones[0].trainerIds.empty());

	// -- refusals
	GuideData g3;
	CHECK(!loadGuideFile(dir + "/platinum.json", g3, GameId::DIAMOND, &why));			// another game's file
	CHECK(why.find("another game") != std::string::npos);
	CHECK(!loadGuideFile(dir + "/missing.json", g3, GameId::NONE, &why) && !why.empty());
	CHECK(!loadGuideFile("/dev/null", g3, GameId::NONE, &why));
	CHECK(guideFileName(GameId::NONE).empty() && guideFileName(GameId::BLACK2) == "black2.json" && guideFileName(GameId::SOULSILVER) == "soulsilver.json");
	setenv("BANANA_NDS_GUIDE", dir.c_str(), 1);
	Game pt; pt.id = GameId::PLATINUM;
	CHECK(loadGuide(pt, g3, &why, nullptr) && g3.milestones.size() == 3);
	Game none;
	CHECK(!loadGuide(none, g3, &why, nullptr));
	Game dp; dp.id = GameId::DIAMOND;
	CHECK(!loadGuide(dp, g3, &why, nullptr));											// no diamond.json in the fixture folder

	// -- the shipped data: every game's file loads, with the right shape for its game
	if (argc > 2)
	{
		struct { GameId id; int badges; } all[] = { { GameId::DIAMOND, 8 }, { GameId::PEARL, 8 }, { GameId::PLATINUM, 8 }, { GameId::HEARTGOLD, 16 },
			{ GameId::SOULSILVER, 16 }, { GameId::BLACK, 8 }, { GameId::WHITE, 8 }, { GameId::BLACK2, 8 }, { GameId::WHITE2, 8 } };
		for (auto& a : all)
		{
			GuideData d; std::string w;
			std::string path = std::string(argv[2]) + "/" + guideFileName(a.id);
			if (!loadGuideFile(path, d, a.id, &w, nullptr)) { fprintf(stderr, "FAIL %s: %s\n", path.c_str(), w.c_str()); fails++; continue; }
			if (!w.empty()) { fprintf(stderr, "FAIL %s: %s\n", path.c_str(), w.c_str()); fails++; }
			int badges = 0, champ = 0;
			std::set<int> seen;
			bool order = true;
			for (const Milestone& m : d.milestones)
			{
				// (the badge numbers are the game's badge bits, not the story order: Platinum's Fantina is the 5th badge)
				if (m.badge) { badges++; if (m.badge > a.badges || !seen.insert(m.badge).second) order = false; }
				if (m.kind == "champion") champ++;
				if (m.levelCap < 1 && m.kind != "story") order = false;
			}
			if (badges != a.badges || champ < 1 || !order || d.steps.empty() || d.secrets.empty() || d.evolutionNotes.empty())
			{
				fprintf(stderr, "FAIL %s: badges %d (want %d) champion %d badges-distinct-and-caps %d steps %zu secrets %zu evolutions %zu\n", path.c_str(), badges,
					a.badges, champ, (int)order, d.steps.size(), d.secrets.size(), d.evolutionNotes.size());
				fails++;
			}
			// the reachable sets only grow
			for (size_t i = 1; i < d.milestones.size(); i++)
				if (d.milestones[i].reachablePlaces.size() < d.milestones[i - 1].reachablePlaces.size()) { fprintf(stderr, "FAIL %s: reachable set shrinks at %s\n", path.c_str(), d.milestones[i].id.c_str()); fails++; break; }
			printf("%-14s ms %3zu steps %3zu secrets %3zu evolutions %3zu notes %3zu places-without-id %zu\n", guideFileName(a.id).c_str(), d.milestones.size(),
				d.steps.size(), d.secrets.size(), d.evolutionNotes.size(), d.notes.size(), d.unresolved.size());
		}
	}
	if (fails) { fprintf(stderr, "%d failed\n", fails); return 1; }
	printf("guide-test: ok\n");
	return 0;
}
