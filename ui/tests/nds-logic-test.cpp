// the DS Pokémon companion's advice (nds/logic.h) on a small made-up RomData: the damage formula against the worked
// examples of the games' mechanics (Bulbapedia's "Damage" and "Stat" pages), catch chances, milestones, the TM and
// evolution helpers, and the whole Advice for a battle and a team.
#include "nds/logic.h"
#include "nds/session.h"
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <cmath>
#include <sstream>
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
	// "party": done once the player has a Pokémon (the starter); "none" (an optional fight, a legend) doesn't hold the
	// story up: done once a later one is, like "later" (found on the Plus: HeartGold on Route 30 with a Cyndaquil kept
	// "Choose your first partner" as the goal; Platinum's lake legends ("none") before Volkner would have stuck there)
	{
		GuideData h;
		Milestone p0; p0.id = "partner"; p0.doneWhen = "party";
		Milestone p1; p1.id = "legend"; p1.doneWhen = "none";
		Milestone p2; p2.id = "gym1"; p2.doneWhen = "badge:1";
		Milestone p3; p3.id = "post"; p3.doneWhen = "none";
		h.milestones = { p0, p1, p2, p3 };
		Live q; q.valid = true;
		CHECK(nextMilestone(h, q) == &h.milestones[0]);
		Mon egg; egg.species = 175; egg.egg = true;
		q.party = { egg };
		CHECK(nextMilestone(h, q) == &h.milestones[0]);	// (an egg isn't a partner yet)
		Mon cyn; cyn.species = 155; cyn.level = 5;
		q.party = { cyn };
		CHECK(nextMilestone(h, q) == &h.milestones[1]);
		q.badges = 1;
		CHECK(nextMilestone(h, q) == &h.milestones[3]);	// the legend before the gym passed, the one after stays
	}
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
	{	// a foe whose first attack has priority (Quick Attack) doesn't make the much faster lead "slower": speed is
		// weighed against its first ordinary attack (Black 2: Krookodile, Speed 117, "slower" than a Tranquill)
		Live q = l;
		q.battle.foes[0].mon.moves[0] = 98; q.battle.foes[0].mon.moves[1] = 33;
		Advice b = computeAdvice(r, q, &g, st);
		CHECK(b.moves.size() == 2 && b.moves[1].movesFirst);
	}
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
	{	// a secret with a condition ("after Red", "after Oak's Kanto starter") can't be told reachable: no badge for it
		// (HeartGold on the Plus: the post-game Blue Orb raised "1 secret here" on Route 30 at 0 badges)
		GuideData g2 = g;
		g2.secrets[0].condition = "after the Champion";
		Advice c = computeAdvice(r, l, &g2, st);
		CHECK(c.notices.empty());
	}
	// evolution watch: Turtwig at Lv 17 of 18
	auto ev = evolutionWatch(Gen::IV, twig, r, l, &g);
	CHECK(ev.size() == 1 && ev[0].into == 388 && ev[0].levelsToGo == 1 && !ev[0].ready);
	twig.level = 18;
	ev = evolutionWatch(Gen::IV, twig, r, l, &g);
	CHECK(ev[0].ready);
	{	// roamer: and evolve: notices are not verified on a running game yet: off unless asked for (AdviceState), the
		// rest as before
		Live r2 = l;
		r2.party[1].level = 18;
		Roamer mesprit; mesprit.species = 481; mesprit.zone = 30; mesprit.level = 50; mesprit.active = true;
		r2.roamers = { mesprit };
		auto has = [](const Advice& a, const char* pre) { for (auto& n : a.notices) if (n.id.rfind(pre, 0) == 0) return true; return false; };
		AdviceState off;
		Advice a1 = computeAdvice(r, r2, &g, off);
		CHECK(!has(a1, "evolve:") && !has(a1, "roamer:") && has(a1, "next:") && has(a1, "secrets:"));
		AdviceState on; on.unverifiedNotices = true;
		Advice a2 = computeAdvice(r, r2, &g, on);
		CHECK(has(a2, "evolve:2:388") && has(a2, "roamer:481:30"));
	}
	Mon e; e.evs[0] = 252; e.evs[1] = 252; e.evs[3] = 6;
	CHECK(evTotal(e) == 510);
	// no battle: no move verdicts, no catching
	l.battle = Battle{};
	a = computeAdvice(r, l, &g, AdviceState{});
	CHECK(a.moves.empty() && a.catching.empty());
}

// how a species new in the Pokédex came: caught in a wild battle (on, or just over), from an evolution or a hatch in
// the party (logged as that), else received (gifts, trades, events)
static void testNewEntries()
{
	Live was, now;
	was.valid = now.valid = true;
	was.playSeconds = 100; now.playSeconds = 101;
	Mon a; a.pid = 7; a.species = 387; a.nick = "TWIG";
	was.party = { a };
	now.party = { a };
	LogKind k = LogKind::NOTE;
	CHECK(newEntryKind(was, now, 25, false, k) && k == LogKind::GIFT);
	CHECK(newEntryKind(was, now, 25, true, k) && k == LogKind::CATCH);		// (a wild battle ended a moment ago)
	Live inBattle = now; inBattle.battle.kind = BattleKind::WILD;
	CHECK(newEntryKind(was, inBattle, 25, false, k) && k == LogKind::CATCH);
	Live trainer = now; trainer.battle.kind = BattleKind::TRAINER;
	CHECK(newEntryKind(was, trainer, 25, false, k) && k == LogKind::GIFT);	// (no catching in a trainer battle)
	Live evolved = now; evolved.party[0].species = 388;
	CHECK(!newEntryKind(was, evolved, 388, true, k));
	Live egg = was; egg.party[0].egg = true; egg.party[0].species = 175;
	Live hatched = now; hatched.party[0].species = 175;
	CHECK(!newEntryKind(egg, hatched, 175, false, k));
}

// the session over a made-up link answering from a Platinum RAM image: Live, Advice, the journey, seen notices kept
static void p16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t* p, uint32_t v) { p16(p, (uint16_t)v); p16(p + 2, (uint16_t)(v >> 16)); }
static void testSession(const char* tmp)
{
	auto r = std::make_shared<FakeRom>(rom(Gen::IV));
	r->game.id = GameId::PLATINUM; r->game.code = "CPUE";
	auto g = std::make_shared<GuideData>();
	Milestone gym; gym.id = "gym1"; gym.name = "Roark"; gym.doneWhen = "badge:1"; gym.trainerIds = { 7 }; gym.reachableZones = { 30 };
	g->milestones = { gym };
	std::vector<uint8_t> ram(0x400000);
	auto at = [&](uint32_t a) { return ram.data() + (a - 0x02000000); };
	const uint32_t S = 0x0227E118, B = S + 0x14;
	p32(at(0x02101D40), S); p32(at(B + 0x98), 6); p32(at(B + 0x9C), 1);
	uint8_t plain[236] = {};
	p32(plain, 0x1234); p16(plain + 8, 387); plain[0x8C] = 17; p16(plain + 0x8E, 40); p16(plain + 0x90, 50);
	for (int i = 0; i < 11; i++) p16(plain + 0x48 + 2 * i, 0xFFFF);
	encryptMon(plain, 236, at(B + 0xA0));
	p16(at(B + 0x78), 111); p16(at(B + 0x7A), 222); p32(at(B + 0x1280), 30);
	at(B + 0x1328 + 4)[48] = 0x04;	// caught: 387 = bit 386
	// the link: send stores the request, receive answers it from the image (READ_CORE_MEMORY lines)
	std::string pending;
	auto mkSession = [&](Session& s)
	{
		s.send = [&](const std::string& q) { pending = q; return true; };
		s.receive = [&]() -> std::string
		{
			std::string out, line;
			std::istringstream in(pending);
			while (std::getline(in, line))
			{
				unsigned a = 0, n = 0;
				if (sscanf(line.c_str(), "READ_CORE_MEMORY %x %u", &a, &n) != 2) continue;
				char b[16];
				snprintf(b, sizeof b, "%x", a);
				out += "READ_CORE_MEMORY " + std::string(b);
				for (unsigned i = 0; i < n; i++) { snprintf(b, sizeof b, " %02x", at(a)[i]); out += b; }
				out += "\n";
			}
			pending.clear();
			return out;
		};
	};
	{
		Session s(r, g, tmp);
		mkSession(s);
		double t = 0;
		for (int i = 0; i < 10; i++, t += 300) s.tick(t, 1762000000);
		CHECK(s.live()->valid && s.live()->party.size() == 1 && s.live()->party[0].species == 387);
		CHECK(s.advice()->next && s.advice()->next->id == "gym1");
		CHECK(s.unseen() >= 1);
		// an evolution: the journey has it, and the Pokédex entry it brings is not a catch
		at(B + 0x1328 + 4)[48] |= 0x08;	// 388
		p16(plain + 8, 388); encryptMon(plain, 236, at(B + 0xA0));
		for (int i = 0; i < 4; i++, t += 300) s.tick(t, 1762000100);
		auto j = s.journey();
		bool caught = false, evolved = false;
		for (auto& e : j->log) { caught |= e.kind == LogKind::CATCH && e.species == 388; evolved |= e.kind == LogKind::EVOLVE && e.species == 388; }
		CHECK(!caught && evolved);
		// a new Pokédex entry with no wild battle about: a gift (White's Tepig was logged "Caught" at 0h00m)
		at(B + 0x1328 + 4)[16] |= 0x10;	// 133 = bit 132
		for (int i = 0; i < 4; i++, t += 300) s.tick(t, 1762000120);
		bool gift = false;
		for (auto& e : s.journey()->log) gift |= e.kind == LogKind::GIFT && e.species == 133 && e.text.rfind("Received ", 0) == 0;
		CHECK(gift);
		// a whole Pokédex turning up in one step (a save that has just loaded) is not a run of catches
		auto catches = [&] { size_t n = 0; for (auto& e : s.journey()->log) n += e.kind == LogKind::CATCH; return n; };
		const size_t catchesBefore = catches();
		for (int i = 0; i < 8; i++) at(B + 0x1328 + 4)[1 + i] = 0xFF;	// species 9..72
		for (int i = 0; i < 4; i++, t += 300) s.tick(t, 1762000150);
		CHECK(catches() == catchesBefore);
		// Gen V's season as the player picked it on the Here page (the game's own isn't read): Live carries it, so the
		// logic's catch-now goes by the same season as the page
		const uint8_t monthSeason = s.live()->season;
		s.setSeason((monthSeason + 1) % 4);
		for (int i = 0; i < 2; i++, t += 300) s.tick(t, 1762000160);
		CHECK(s.live()->season == (monthSeason + 1) % 4);
		s.setSeason(-1);
		for (int i = 0; i < 2; i++, t += 300) s.tick(t, 1762000170);
		CHECK(s.live()->season == monthSeason);
		const int before = s.unseen();
		s.markSeen({ "next:gym1" });
		CHECK(s.unseen() == before - 1);
		CHECK(!s.stateFile().empty());
	}	// (saved when it ends)
	{
		Session s(r, g, tmp);
		mkSession(s);
		double t = 0;
		for (int i = 0; i < 6; i++, t += 300) s.tick(t, 1762000200);
		auto sl = s.seenList();
		CHECK(sl.size() == 1 && sl[0] == "next:gym1");
		CHECK(s.journey()->log.size() >= 2);
		CHECK(!s.journey()->challenge);
	}
	{	// Challenge mode switched on in Settings for a playthrough the card already knows: the setting wins over the
		// card's "challenge 0" (it was read after the setting and switched it back off)
		Session s(r, g, tmp);
		mkSession(s);
		s.setChallenge(true);
		double t = 0;
		for (int i = 0; i < 6; i++, t += 300) s.tick(t, 1762000300);
		CHECK(s.live()->valid && s.journey()->challenge);
	}
	{	// and off again
		Session s(r, g, tmp);
		mkSession(s);
		s.setChallenge(false);
		double t = 0;
		for (int i = 0; i < 6; i++, t += 300) s.tick(t, 1762000400);
		CHECK(s.live()->valid && !s.journey()->challenge);
	}
	{	// on its own thread (the companion's paint loop takes snapshots): the same Live and advice come through take(),
		// what the pages saw goes back, and it stops (saving) when it goes
		auto ss = std::make_unique<Session>(r, g, tmp);
		mkSession(*ss);
		SessionThread st(std::move(ss), 5);
		SessionThread::Snapshot sn;
		bool got = false;
		for (int i = 0; i < 300 && !got; i++)
		{
			usleep(10000);
			got = st.take(sn) && sn.live && sn.live->valid;
		}
		CHECK(got && sn.live->party.size() == 1 && sn.advice && sn.journey);
		CHECK(!st.take(sn));	// (nothing new since: the RAM image didn't change)
		st.markSeen({ "x:test" });
		at(B + 0x1328 + 4)[20] |= 0x01;	// a new Pokédex entry: the next poll changes Live
		bool seen = false;
		for (int i = 0; i < 300 && !seen; i++)
		{
			usleep(10000);
			if (st.take(sn)) seen = std::find(sn.seen.begin(), sn.seen.end(), "x:test") != sn.seen.end();
		}
		CHECK(seen);
	}
	unlink((std::string(tmp) + "/CPUE-00111-00222.tsv").c_str());
}

int main()
{
	testNewEntries();
	testDamage();
	testCatch();
	testGuide();
	testAdvice();
	{
		char tmp[] = "/tmp/nds-logic-test-XXXXXX";
		if (mkdtemp(tmp)) { testSession(tmp); rmdir(tmp); }
	}
	if (fails) { fprintf(stderr, "%d failed\n", fails); return 1; }
	fprintf(stderr, "nds-logic-test: all passed\n");
	return 0;
}
