// the DS Pokémon companion's advice (nds/logic.h) on a small made-up RomData: the damage formula against the worked
// examples of the games' mechanics (Bulbapedia's "Damage" and "Stat" pages), catch chances, milestones, the TM and
// evolution helpers, and the whole Advice for a battle and a team.
#include "nds/logic.h"
#include <cmath>
#include <cstdio>

using namespace banana::nds;
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct FakeRom : RomData
{
	std::shared_ptr<const Sprite> sprite(uint16_t, bool, uint8_t) const override { return nullptr; }
};

static Species mk(const char* name, Type t1, Type t2, std::array<uint8_t, 6> base, uint8_t rate)
{
	Species s;
	s.name = name; s.types[0] = t1; s.types[1] = t2;
	for (int i = 0; i < 6; i++) s.base[i] = base[i];
	s.catchRate = rate;
	return s;
}
static MoveData mv(const char* n, Type t, MoveCategory c, int power, int prio = 0)
{
	MoveData m; m.name = n; m.type = t; m.cat = c; m.power = (uint8_t)power; m.priority = (uint8_t)(int8_t)prio; m.pp = 10;
	return m;
}

static FakeRom rom(Gen g)
{
	FakeRom r;
	r.game.gen = g;
	r.species.resize(650);
	r.species[25] = mk("Pikachu", ELECTRIC, ELECTRIC, { 35, 55, 30, 90, 50, 40 }, 190);
	r.species[74] = mk("Geodude", ROCK, GROUND, { 40, 80, 100, 20, 30, 30 }, 255);
	r.species[92] = mk("Gastly", GHOST, POISON, { 30, 35, 30, 80, 100, 35 }, 190);
	r.species[129] = mk("Magikarp", WATER, WATER, { 20, 10, 55, 80, 15, 20 }, 255);
	r.species[387] = mk("Turtwig", GRASS, GRASS, { 55, 68, 64, 31, 45, 55 }, 45);
	r.species[387].evolutions.push_back({ 4, 18, 388, "level 18" });
	r.species[387].tms = { 53 };
	r.species[388] = mk("Grotle", GRASS, GRASS, { 75, 89, 85, 36, 55, 65 }, 45);
	r.species[445] = mk("Garchomp", DRAGON, GROUND, { 108, 130, 95, 102, 80, 85 }, 45);
	r.species[471] = mk("Glaceon", ICE, ICE, { 65, 60, 110, 65, 130, 95 }, 45);
	r.moves.resize(600);
	r.moves[33] = mv("Tackle", NORMAL, MoveCategory::PHYSICAL, 35);
	r.moves[45] = mv("Growl", NORMAL, MoveCategory::STATUS, 0);
	r.moves[75] = mv("Razor Leaf", GRASS, MoveCategory::PHYSICAL, 55);
	r.moves[85] = mv("Thunderbolt", ELECTRIC, MoveCategory::SPECIAL, 95);
	r.moves[89] = mv("Earthquake", GROUND, MoveCategory::PHYSICAL, 100);
	r.moves[98] = mv("Quick Attack", NORMAL, MoveCategory::PHYSICAL, 40, 1);
	r.moves[423] = mv("Ice Fang", ICE, MoveCategory::PHYSICAL, 65);
	r.moves[202] = mv("Giga Drain", GRASS, MoveCategory::SPECIAL, 60);
	r.tmMoves.assign(100, 0);
	r.tmMoves[52] = 202;	// TM53 Giga Drain (in this made-up ROM)
	Trainer roark; roark.id = 7; roark.name = "Roark"; roark.cls = "Leader";
	roark.team = { { 74, 0, 12, { 33 } }, { 74, 0, 14, { 33 } } };
	r.trainers[7] = roark;
	Location rt; rt.id = 30; rt.name = "Route 30";
	rt.encounters = { { 129, 5, 10, 60, EncounterKind::SURF }, { 92, 10, 12, 20, EncounterKind::GRASS, TimeOfDay::NIGHT } };
	r.zones[30] = rt;
	return r;
}

static void testDamage()
{
	for (Gen g : { Gen::IV, Gen::V })
	{
		FakeRom r = rom(g);
		// the worked example: Lv 75 Glaceon (Atk 123) uses Ice Fang on Garchomp (Def 163): 168-196
		Fighter a, d;
		a.species = 471; a.level = 75; a.stats[0] = 123; a.types[0] = ICE; a.types[1] = NO_TYPE;
		d.species = 445; d.level = 75; d.stats[1] = 163; d.types[0] = DRAGON; d.types[1] = GROUND; d.hp = d.maxHp = 250;
		DamageRange x = damage(g, r.moves[423], a, d);
		CHECK(x.lo == 168 && x.hi == 196 && x.quarters == 16 && x.stab);
		// Earthquake on a Levitate foe; Tackle on a Ghost; Scrappy's Tackle does hit it
		Fighter lev = d; lev.ability = 26;
		CHECK(damage(g, r.moves[89], a, lev).quarters == 0);
		Fighter gh = d; gh.types[0] = GHOST; gh.types[1] = POISON;
		CHECK(damage(g, r.moves[33], a, gh).quarters == 0);
		Fighter scrappy = a; scrappy.ability = 113;
		CHECK(damage(g, r.moves[33], scrappy, gh).hi > 0);
		// a burn halves physical damage; status moves do none
		Fighter burnt = a; burnt.status = 0x10;
		const DamageRange b = damage(g, r.moves[423], burnt, d);
		CHECK(b.hi < x.hi * 6 / 10 && b.hi > x.hi * 4 / 10);
		CHECK(damage(g, r.moves[45], a, d).hi == 0);
		// stages: +2 Atk doubles it (about), -1 Def of the foe raises it
		Fighter up = a; up.stages[1] = 2;
		CHECK(std::abs(damage(g, r.moves[423], up, d).hi - 2 * x.hi) <= 8);
	}
	// stats: Garchomp Lv 78, base HP 108 IV 24 EV 74 -> 289; Atk base 130 IV 12 EV 195 Adamant (3) -> 278
	CHECK(statValue(0, 108, 24, 74, 78, 3) == 289);
	CHECK(statValue(1, 130, 12, 195, 78, 3) == 279);	// (by hand: (260 + 12 + 48) * 78 / 100 = 249, + 5, x 1.1)
	// speed: paralysis quarters it; priority beats speed
	Fighter f; f.stats[2] = 100; f.status = 0x40;
	CHECK(effectiveSpeed(Gen::IV, f) == 25);
	f.status = 0; f.stages[3] = 1;
	CHECK(effectiveSpeed(Gen::V, f) == 150);
	FakeRom r = rom(Gen::IV);
	Fighter slow, fast; slow.stats[2] = 10; fast.stats[2] = 200;
	CHECK(movesFirst(Gen::IV, r.moves[98], slow, r.moves[33], fast) == 1);
	CHECK(movesFirst(Gen::IV, r.moves[33], slow, r.moves[33], fast) == -1);
	CHECK(movesFirst(Gen::IV, r.moves[33], slow, r.moves[33], slow) == 0);
}

static void testCatch()
{
	FakeRom r4 = rom(Gen::IV), r5 = rom(Gen::V);
	Mon w; w.species = 387; w.level = 5; w.hp = w.maxHp = 20;
	CatchContext c;
	// catch rate 45 at full HP in a Poké Ball: about a / 255 = 15 / 255 in Gen IV, (15/255)^0.75 in Gen V
	CHECK(std::fabs(catchChance(Gen::IV, r4, w, 4, c) - 0.0625f) < 0.002f);
	CHECK(std::fabs(catchChance(Gen::V, r5, w, 4, c) - 0.1194f) < 0.003f);
	CHECK(catchChance(Gen::IV, r4, w, 1, c) == 1.0f);	// Master Ball
	// lower HP and sleep raise it; Ultra Ball doubles the rate
	Mon low = w; low.hp = 1; low.status = 2;
	CHECK(catchChance(Gen::IV, r4, low, 4, c) > 0.3f);
	CHECK(catchChance(Gen::IV, r4, w, 2, c) > catchChance(Gen::IV, r4, w, 3, c));
	// Net Ball on a Water type x3; Dusk Ball at night x3.5; Quick Ball only on the first turn
	Mon karp; karp.species = 129; karp.level = 5; karp.hp = karp.maxHp = 20;
	float m = 0;
	catchChance(Gen::IV, r4, karp, 6, c, &m); CHECK(m == 3);
	catchChance(Gen::IV, r4, w, 6, c, &m); CHECK(m == 1);
	c.time = TimeOfDay::NIGHT; catchChance(Gen::V, r5, w, 13, c, &m); CHECK(m == 3.5f);
	c.turn = 2; catchChance(Gen::V, r5, w, 15, c, &m); CHECK(m == 1);
	c.turn = 1; catchChance(Gen::V, r5, w, 15, c, &m); CHECK(m == 5);
	catchChance(Gen::IV, r4, w, 15, c, &m); CHECK(m == 4);
	c.turn = 11; catchChance(Gen::V, r5, w, 10, c, &m); CHECK(m == 4);
	catchChance(Gen::IV, r4, w, 8, c, &m); CHECK(std::fabs(m - 3.5f) < 0.01f);	// Nest Ball, Lv 5: (40-5)/10
}

static void testGuide()
{
	GuideData g;
	Milestone m1; m1.id = "rival"; m1.name = "Rival"; m1.doneWhen = "later"; m1.reachableZones = { 1, 2 };
	Milestone m2; m2.id = "gym1"; m2.name = "Roark"; m2.doneWhen = "badge:1"; m2.reachableZones = { 30 }; m2.trainerIds = { 7 };
	m2.leader = "Roark"; m2.tips = "Bring Water or Grass.";
	Milestone m3; m3.id = "event"; m3.name = "Event"; m3.doneWhen = "flag:9"; m3.reachableZones = { 40 };
	g.milestones = { m1, m2, m3 };
	Live l; l.valid = true;
	CHECK(nextMilestone(g, l) == &g.milestones[0]);
	auto z = reachableZones(g, l);
	CHECK(z.count(1) && !z.count(30));
	l.badges = 1;
	CHECK(nextMilestone(g, l) == &g.milestones[2]);	// rival done "later", the gym by its badge
	CHECK(reachableZones(g, l).count(40));
	l.flags.assign(2, 0); l.flags[1] = 0x02;
	CHECK(nextMilestone(g, l) == nullptr);
	CHECK(tmNumber(Gen::IV, 328) == 1 && tmNumber(Gen::IV, 420) == 93 && tmNumber(Gen::V, 618) == 93 && tmNumber(Gen::V, 420) == 96);
	CHECK(tmNumber(Gen::IV, 4) == 0);
}

static void testAdvice()
{
	FakeRom r = rom(Gen::IV);
	GuideData g;
	Milestone gym; gym.id = "gym1"; gym.name = "Oreburgh Gym"; gym.leader = "Roark"; gym.doneWhen = "badge:1";
	gym.trainerIds = { 7 }; gym.reachableZones = { 30 }; gym.tips = "Rock types.";
	g.milestones = { gym };
	Secret s; s.id = "hidden1"; s.zone = 30; g.secrets = { s };
	Live l; l.valid = true; l.zone = 30; l.timeOfDay = TimeOfDay::NIGHT;
	Mon pika; pika.species = 25; pika.nick = "PIKA"; pika.level = 10; pika.hp = pika.maxHp = 30; pika.pid = 1;
	pika.stats[0] = 15; pika.stats[1] = 12; pika.stats[2] = 30; pika.stats[3] = 20; pika.stats[4] = 15;
	pika.moves[0] = 85; pika.pp[0] = 10; pika.moves[1] = 33; pika.pp[1] = 10;
	Mon twig; twig.species = 387; twig.nick = "TWIG"; twig.level = 17; twig.hp = twig.maxHp = 50; twig.pid = 2;
	twig.moves[0] = 33; twig.pp[0] = 10;
	l.party = { pika, twig };
	l.bag[POCKET_BALLS] = { { 4, 5 }, { 13, 2 } };
	l.bag[POCKET_TMS] = { { (uint16_t)(327 + 53), 1 }, { 422, 1 } };	// TM53, HM03 (Surf: the surfing encounters count)
	l.dexCaught.assign(62, 0);
	// a wild Geodude battle
	l.battle.kind = BattleKind::WILD;
	l.battle.ours[0].present = true; l.battle.ours[0].mon = pika;
	l.battle.ours[0].types[0] = ELECTRIC; l.battle.ours[0].types[1] = ELECTRIC;
	Mon geo; geo.species = 74; geo.level = 8; geo.hp = geo.maxHp = 25; geo.stats[1] = 20; geo.stats[4] = 10; geo.stats[2] = 5;
	geo.moves[0] = 33;
	l.battle.foes[0].present = true; l.battle.foes[0].mon = geo; l.battle.foes[0].types[0] = ROCK; l.battle.foes[0].types[1] = GROUND;
	AdviceState st;
	Advice a = computeAdvice(r, l, &g, st);
	CHECK(a.moves.size() == 2);
	CHECK(a.moves[0].quarters == 0 && a.moves[0].dmgHi == 0);		// Thunderbolt on Rock/Ground
	CHECK(a.moves[1].quarters == 2 && a.moves[1].best && a.moves[1].movesFirst);	// Tackle, resisted, but the only one
	CHECK(a.catching.size() == 2 && a.catching[0].ball == 13);	// Dusk Ball at night first
	CHECK(a.next == &g.milestones[0]);
	CHECK(a.scoreParts.size() == 4 && a.teamScore == a.scoreParts[0].score + a.scoreParts[1].score + a.scoreParts[2].score + a.scoreParts[3].score);
	CHECK(a.scoreParts[2].score == 22);	// the best (here: both) average Lv 13 against an ace of 14: -3
	bool electricProblem = false;
	for (auto& p : a.nextProblems) if (p.find("Electric") != std::string::npos) electricProblem = true;
	CHECK(electricProblem);				// Electric moves can't touch Geodudes
	// catch now on Route 30: Gastly at night adds a new type; Magikarp (Water) beats both Geodudes
	CHECK(!a.catchNow.empty() && a.catchNow[0].species == 129 && a.catchNow[0].where == "Route 30");
	{	// without Surf the Magikarp on the water is out of reach
		Live noSurf = l;
		noSurf.bag[POCKET_TMS] = { { (uint16_t)(327 + 53), 1 } };
		Advice b = computeAdvice(r, noSurf, &g, st);
		for (auto& c : b.catchNow) CHECK(c.species != 129);
	}
	// TM53 (Giga Drain) for Turtwig: Grass against the Rock/Ground team
	CHECK(a.tmPlan.size() == 1 && a.tmPlan[0].first == 380 && a.tmPlan[0].second.find("TWIG") == 0);
	// notices: the next goal, the catch here, the secret here; seen ones stay away
	bool next = false, here = false, secret = false;
	for (auto& n : a.notices)
	{
		next |= n.id == "next:gym1"; here |= n.id == "catch:30:129"; secret |= n.id == "secrets:30";
	}
	CHECK(next && here && secret);
	st.seen = { "next:gym1", "catch:30:129" };
	a = computeAdvice(r, l, &g, st);
	CHECK(a.notices.size() == 1 && a.notices[0].id == "secrets:30");
	// evolution watch: Turtwig at Lv 17 of 18
	auto ev = evolutionWatch(Gen::IV, twig, r, l, &g);
	CHECK(ev.size() == 1 && ev[0].into == 388 && ev[0].levelsToGo == 1 && !ev[0].ready);
	twig.level = 18;
	ev = evolutionWatch(Gen::IV, twig, r, l, &g);
	CHECK(ev[0].ready);
	Mon e; e.evs[0] = 252; e.evs[1] = 252; e.evs[3] = 6;
	CHECK(evTotal(e) == 510);
	// no battle: no move verdicts, no catching
	l.battle = Battle{};
	a = computeAdvice(r, l, &g, AdviceState{});
	CHECK(a.moves.empty() && a.catching.empty());
}

int main()
{
	testDamage();
	testCatch();
	testGuide();
	testAdvice();
	if (fails) { fprintf(stderr, "%d failed\n", fails); return 1; }
	fprintf(stderr, "nds-logic-test: all passed\n");
	return 0;
}
