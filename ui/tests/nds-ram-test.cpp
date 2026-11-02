// the DS Pokémon companion's RAM reader (nds/ram.h) on synthetic RAM: Pokémon made with the games' own encryption,
// placed where each game keeps them, read through the same request/answer sets as on the handheld. With a folder
// argument it also reads the RAM snapshots recorded on the handhelds (~/nds-companion/ram-kit/out: the owner's games,
// never in git) and checks the values seen on the panels.
#include "nds/ram.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace banana::nds;
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void p16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t* p, uint32_t v) { p16(p, (uint16_t)v); p16(p + 2, (uint16_t)(v >> 16)); }

// a main-RAM image (0x02000000, 4 MB) that answers RamReader's sets as BananaDS's export would
struct Ram
{
	std::vector<uint8_t> m = std::vector<uint8_t>(0x400000);
	uint8_t* at(uint32_t a) { return m.data() + (a - 0x02000000); }
	std::vector<std::vector<uint8_t>> answer(const std::vector<Range>& w)
	{
		std::vector<std::vector<uint8_t>> out;
		for (auto& r : w)
		{
			if (!r.len || r.addr < 0x02000000 || r.addr + r.len > 0x02400000) { out.emplace_back(); continue; }
			out.emplace_back(at(r.addr), at(r.addr) + r.len);
		}
		return out;
	}
};
// run sets until one is used (the first ones find the pointers)
static bool settle(RamReader& rr, Ram& ram, int sets = 4)
{
	for (int i = 0; i < sets; i++)
		if (rr.deliver(ram.answer(rr.wants()), i * 250.0, 1762000000)) return true;
	return false;
}

// a plain Pokémon: species, level, a few moves and IVs; Gen IV text: 'A'.. = 0x012B..
static void makeMon(Gen g, uint8_t* plain, size_t n, uint32_t pid, uint16_t species, uint8_t level, uint16_t tid,
	uint16_t sid, uint16_t move0)
{
	memset(plain, 0, n);
	p32(plain, pid);
	p16(plain + 0x08, species);
	p16(plain + 0x0C, tid);
	p16(plain + 0x0E, sid);
	p32(plain + 0x10, 1000);
	plain[0x14] = 70;
	plain[0x15] = 65;
	plain[0x18] = 4;	// HP EVs
	p16(plain + 0x28, move0);
	plain[0x30] = 35;
	p32(plain + 0x38, 31u | 30u << 5 | 29u << 10 | 28u << 15 | 27u << 20 | 26u << 25);
	plain[0x40] = 1 << 1;	// female
	if (g == Gen::V) plain[0x41] = 7;	// Relaxed
	for (int i = 0; i < 11; i++) p16(plain + 0x48 + 2 * i, 0xFFFF);
	if (g == Gen::IV) { p16(plain + 0x48, 0x012B); p16(plain + 0x4A, 0x012C); }	// "AB"
	else { p16(plain + 0x48, 'A'); p16(plain + 0x4A, 'b'); }
	if (n > 136)
	{
		plain[0x8C] = level;
		p16(plain + 0x8E, 20);
		p16(plain + 0x90, 25);
		for (int i = 0; i < 5; i++) p16(plain + 0x92 + 2 * i, (uint16_t)(10 + i));
	}
}

static void testCrypto()
{
	for (Gen g : { Gen::IV, Gen::V })
		for (size_t n : { (size_t)136, g == Gen::IV ? (size_t)236 : (size_t)220 })
			for (uint32_t pid : { 0x00000000u, 0x12345678u, 0xFFFFFFFFu, 0x698b86b2u })
			{
				uint8_t plain[236], enc[236], back[236];
				makeMon(g, plain, n, pid, 25, 12, 1234, 5678, 33);
				encryptMon(plain, n, enc);
				CHECK(memcmp(enc + 8, plain + 8, 128) != 0);	// really encrypted
				CHECK(decryptMon(enc, n, back));
				CHECK(memcmp(back + 8, plain + 8, n - 8) == 0);
				Mon m;
				CHECK(readMon(g, enc, n, m));
				CHECK(m.species == 25 && m.pid == pid && m.otId == 1234 && m.otSecret == 5678);
				CHECK(m.moves[0] == 33 && m.pp[0] == 35 && m.ivs[0] == 31 && m.ivs[5] == 26 && m.evs[0] == 4);
				CHECK(m.female);
				CHECK(m.nick == (g == Gen::IV ? "AB" : "Ab"));
				CHECK(m.nature == (g == Gen::IV ? pid % 25 : 7));
				if (n > 136) CHECK(m.level == 12 && m.hp == 20 && m.maxHp == 25 && m.stats[2] == 12);
				// one flipped byte: the checksum says no
				enc[40] ^= 1;
				CHECK(!decryptMon(enc, n, back));
				CHECK(!readMon(g, enc, n, m));
			}
	// "decrypted in place" (the game is working on it): read as plain
	uint8_t plain[236], back[236];
	makeMon(Gen::IV, plain, 236, 0x1111, 1, 5, 1, 2, 33);
	uint16_t s = 0;
	for (int i = 0; i < 64; i++) s = (uint16_t)(s + (plain[8 + 2 * i] | plain[9 + 2 * i] << 8));
	p16(plain + 6, s);
	p16(plain + 4, 3);
	// (stored shuffled: shuffle by hand through encrypt with the flags then undo the cipher: simplest is a pid whose
	// order is ABCD: (pid >> 13) & 31 == 0)
	CHECK(decryptMon(plain, 236, back));
	CHECK(back[8] == 1);
	CHECK(timeOfDay(3) == TimeOfDay::NIGHT && timeOfDay(4) == TimeOfDay::MORNING && timeOfDay(10) == TimeOfDay::DAY &&
		timeOfDay(19) == TimeOfDay::DAY && timeOfDay(20) == TimeOfDay::NIGHT);
	CHECK(season(1) == 0 && season(2) == 1 && season(4) == 3 && season(5) == 0 && season(12) == 3);
	CHECK(typeFromGame(Gen::IV, 10) == FIRE && typeFromGame(Gen::IV, 9) == NO_TYPE && typeFromGame(Gen::IV, 17) == DARK);
	CHECK(typeFromGame(Gen::V, 9) == FIRE && typeFromGame(Gen::V, 16) == DARK);
}

static Game game(GameId id, Gen g) { Game x; x.id = id; x.gen = g; return x; }

// Platinum: SaveData* at 0x02101D40, body 0x14 past it
static void testPlatinum()
{
	Ram ram;
	const uint32_t S = 0x0227E118, B = S + 0x14;
	p32(ram.at(0x02101D40), S);
	p32(ram.at(B + 0x98), 6);
	p32(ram.at(B + 0x9C), 2);
	uint8_t plain[236];
	makeMon(Gen::IV, plain, 236, 0x9F5452B2, 396, 2, 34691, 38453, 33);
	encryptMon(plain, 236, ram.at(B + 0xA0));
	makeMon(Gen::IV, plain, 236, 0x01020304, 387, 9, 34691, 38453, 75);
	encryptMon(plain, 236, ram.at(B + 0xA0 + 236));
	p16(ram.at(B + 0x68), 0x0134); p16(ram.at(B + 0x6A), 0xFFFF);	// "J"
	p16(ram.at(B + 0x78), 34691); p16(ram.at(B + 0x7A), 38453);
	p32(ram.at(B + 0x7C), 100);
	ram.at(B + 0x82)[0] = 0x05;
	p16(ram.at(B + 0x8A), 1); ram.at(B + 0x8C)[0] = 2; ram.at(B + 0x8D)[0] = 3;
	p32(ram.at(B + 0x1280), 420);
	p32(ram.at(B + 0x1288), 70); p32(ram.at(B + 0x128C), 80);
	ram.at(B + 0x1328 + 4)[0] = 0x03;		// caught: 1, 2
	ram.at(B + 0x1328 + 4 + 61)[0] = 0xFF;	// (493 = 61 * 8 + 5: the bits past it are padding)
	ram.at(B + 0x1328 + 0x44)[0] = 0x07;
	p16(ram.at(B + 0x630 + 0x6BC), 4); p16(ram.at(B + 0x630 + 0x6BE), 16);	// 16 Poké Balls
	p16(ram.at(B + 0x630 + 0x35C), 354); p16(ram.at(B + 0x630 + 0x35E), 1);	// a TM
	// a roamer (Mesprit) on map 30
	p32(ram.at(B + 0x7FF4), 30); p16(ram.at(B + 0x7FF4 + 0xC), 481); ram.at(B + 0x7FF4 + 0x10)[0] = 50;
	ram.at(B + 0x7FF4 + 0x12)[0] = 1;
	p16(ram.at(0x0224A55A), 0x2800);

	{	// a fresh game at its title or before the player is named: the save block is there but empty (no name, no
		// party). Not a playthrough yet: no pages, no badge (Diamond on the Plus showed Twinleaf Town and a badge at
		// the title of a game with no save)
		Ram fresh;
		p32(fresh.at(0x02101D40), S);
		p32(fresh.at(B + 0x98), 6);
		RamReader fr(game(GameId::PLATINUM, Gen::IV));
		settle(fr, fresh);
		CHECK(!fr.live()->valid);
	}
	RamReader rr(game(GameId::PLATINUM, Gen::IV));
	CHECK(rr.supported());
	CHECK(!rr.live()->valid);
	CHECK(settle(rr, ram));
	auto l = rr.live();
	CHECK(l->valid);
	CHECK(l->party.size() == 2 && l->party[0].species == 396 && l->party[1].species == 387 && l->party[1].level == 9);
	CHECK(l->trainerName == "J" && l->tid == 34691 && l->sid == 38453 && l->money == 100 && l->badges == 5);
	CHECK(l->playSeconds == 3600 + 120 + 3);
	CHECK(l->zone == 420 && l->mapX == 70 && l->mapY == 80);
	CHECK(l->dexCaught.size() == 62 && l->dexCaught[0] == 3 && l->dexCaught[61] == 0x1F && l->dexSeen[0] == 7);
	CHECK(l->bag.at(POCKET_BALLS).size() == 1 && l->bag.at(POCKET_BALLS)[0].item == 4 && l->bag.at(POCKET_BALLS)[0].count == 16);
	CHECK(l->bag.at(POCKET_TMS).size() == 1);
	CHECK(l->roamers.size() == 1 && l->roamers[0].species == 481 && l->roamers[0].zone == 30 && l->roamers[0].active);
	CHECK(l->battle.kind == BattleKind::NONE);
	CHECK(l->flags.size() == 0xB60 / 8);
	// nothing changed: no new Live
	const uint64_t g0 = rr.generation();
	CHECK(!rr.deliver(ram.answer(rr.wants()), 1000, 1762000000));
	CHECK(rr.generation() == g0);
	// a Pokémon being written (bad checksum) keeps its last good reading
	ram.at(B + 0xA0 + 236 + 20)[0] ^= 0x55;
	p32(ram.at(B + 0x7C), 101);
	CHECK(rr.deliver(ram.answer(rr.wants()), 1250, 1762000000));
	CHECK(rr.live()->party.size() == 2 && rr.live()->party[1].species == 387 && rr.live()->money == 101);
	// the save moved (a reset): the set is dropped, the next one reads the new place
	Ram moved = ram;
	const uint32_t S2 = S + 0x40;
	memmove(moved.at(S2), moved.at(S), 0x9000);
	p32(moved.at(0x02101D40), S2);
	ram.at(B + 0xA0 + 236 + 20)[0] ^= 0x55;
	CHECK(!rr.deliver(moved.answer(rr.wants()), 1500, 1762000000));
	CHECK(rr.lastProblem() == "pointer moved");
	CHECK(rr.deliver(moved.answer(rr.wants()), 1750, 1762000000));

	// a wild battle: the "in battle" word, BattleMon[0] ours, [1] the foe (plain), the foe's party copy encrypted
	p16(ram.at(0x0224A55A), 0x2100);
	const uint32_t bm = S + 0x47620 - 0x68;
	auto mon4 = [&](uint32_t a, uint16_t sp, uint8_t lv, uint32_t pid, uint8_t t1, uint8_t t2, uint8_t atkStage)
	{
		uint8_t* p = ram.at(a);
		p16(p, sp);
		for (int i = 0; i < 5; i++) p16(p + 2 + 2 * i, 10);
		p16(p + 0x0C, 33);
		for (int i = 0; i < 8; i++) p[0x18 + i] = 6;
		p[0x19] = atkStage;
		p[0x24] = t1; p[0x25] = t2;
		p[0x34] = lv;
		p32(p + 0x4C, 13); p32(p + 0x50, 13);
		p32(p + 0x68, pid);
	};
	mon4(bm, 396, 2, 0x9F5452B2, 0, 2, 6);
	mon4(bm + 0xC0, 167, 2, 0x61E1FAD6, 6, 3, 5);
	makeMon(Gen::IV, plain, 236, 0x61E1FAD6, 167, 2, 34691, 38453, 40);
	encryptMon(plain, 236, ram.at(S + 0x4BE5C));
	CHECK(settle(rr, ram));
	l = rr.live();
	CHECK(l->battle.kind == BattleKind::WILD);
	CHECK(l->battle.ours[0].present && l->battle.ours[0].mon.species == 396 && l->battle.ours[0].mon.evs[0] == 4);
	CHECK(l->battle.foes[0].present && l->battle.foes[0].mon.species == 167 && l->battle.foes[0].stages[1] == -1);
	CHECK(l->battle.foes[0].types[0] == BUG && l->battle.foes[0].types[1] == POISON);
	CHECK(!l->battle.foes[1].present && l->battle.foeParty.empty());
	// a trainer
	p16(ram.at(S + 0x4189E), 12);
	CHECK(settle(rr, ram));
	CHECK(rr.live()->battle.kind == BattleKind::TRAINER && rr.live()->battle.trainerId == 12 && rr.live()->battle.foeParty.size() == 1);
	// over: the word says the field, the leftovers in the battle copies are not a battle
	p16(ram.at(0x0224A55A), 0x2800);
	CHECK(settle(rr, ram));
	CHECK(rr.live()->battle.kind == BattleKind::NONE);
}

// Black 2: the pointer at 0x02000024, the save image 0x920 past it
static void testBlack2()
{
	Ram ram;
	const uint32_t M = 0x02204CC4, V = M + 0x920;
	p32(ram.at(0x02000024), M);
	ram.at(V + 0x18E04)[0] = 1;
	uint8_t plain[236];
	makeMon(Gen::V, plain, 220, 0x698B86B2, 553, 49, 33054, 28192, 44);
	encryptMon(plain, 220, ram.at(V + 0x18E08));
	const char16_t* name = u"Joar";
	for (int i = 0; i < 4; i++) p16(ram.at(V + 0x19404 + 2 * i), name[i]);
	p16(ram.at(V + 0x19404 + 8), 0xFFFF);
	p16(ram.at(V + 0x19414), 33054); p16(ram.at(V + 0x19416), 28192);
	p16(ram.at(V + 0x19424), 9); ram.at(V + 0x19426)[0] = 1; ram.at(V + 0x19427)[0] = 54;
	p16(ram.at(V + 0x19580), 95); p16(ram.at(V + 0x19586), 1); p16(ram.at(V + 0x1958A), 2);	// (the saved spot)
	p16(ram.at(M + 0x41B5C), 96); p16(ram.at(M + 0x38CE8 + 0x2E), 177); p16(ram.at(M + 0x38CE8 + 0x32), 434);	// (now)
	p32(ram.at(V + 0x21100), 52832); ram.at(V + 0x21104)[0] = 0x0F;
	ram.at(V + 0x21400 + 8)[0] = 0x01;
	ram.at(V + 0x21400 + 0x5C + 0x54)[0] = 0x02;	// seen as a female only
	p16(ram.at(V + 0x18400), 4); p16(ram.at(V + 0x18402), 16);	// Poké Balls among the items
	p16(ram.at(V + 0x18404), 50); p16(ram.at(V + 0x18406), 1);
	p16(ram.at(0x021B5138), 0x2800);
	RamReader rr(game(GameId::BLACK2, Gen::V));
	CHECK(settle(rr, ram));
	auto l = rr.live();
	CHECK(l->valid && l->party.size() == 1 && l->party[0].species == 553 && l->party[0].level == 49);
	CHECK(l->party[0].shiny() && l->party[0].nature == 7);
	CHECK(l->trainerName == "Joar" && l->tid == 33054 && l->money == 52832 && l->badges == 0x0F);
	CHECK(l->playSeconds == 9 * 3600 + 60 + 54 && l->zone == 96 && l->mapX == 177 && l->mapY == 434);
	CHECK(l->dexCaught[0] == 1 && l->dexSeen[0] == 3 && l->dexCaught.size() == 82);
	CHECK(l->bag.at(POCKET_BALLS).size() == 1 && l->bag.at(POCKET_ITEMS).size() == 1);
	// a battle: table -> battler objects -> their Pokémon
	p16(ram.at(0x021B5138), 0x2101);
	const uint32_t T = M + 0x526A8, ob = 0x02300000, fb = 0x02300400, om = 0x02301000, fm = 0x02301100;
	p32(ram.at(T), ob); p32(ram.at(T + 0x1C), fb);
	p32(ram.at(ob), om); p32(ram.at(fb), fm);
	encryptMon(plain, 220, ram.at(om));
	makeMon(Gen::V, plain, 220, 0x55555555, 524, 10, 1, 2, 88);
	encryptMon(plain, 220, ram.at(fm));
	for (uint32_t b : { ob, fb })
	{
		p16(ram.at(b + 0x0E), 30); p16(ram.at(b + 0x10), 21); ram.at(b + 0x18)[0] = b == ob ? 49 : 10;
		for (int i = 0; i < 7; i++) ram.at(b + 0xFC + i)[0] = 6;
		p16(ram.at(b + 0x104), 99); ram.at(b + 0x106)[0] = 7;
	}
	ram.at(fb + 0x100)[0] = 8;	// the foe's Speed +2
	// (three sets: the table, then the battlers it points at, then their Pokémon)
	for (int i = 0; i < 3; i++) rr.deliver(ram.answer(rr.wants()), 2000 + 250 * i, 1762000000);
	l = rr.live();
	CHECK(l->battle.kind == BattleKind::WILD);
	CHECK(l->battle.ours[0].present && l->battle.ours[0].mon.species == 553 && l->battle.ours[0].mon.hp == 21);
	CHECK(l->battle.foes[0].present && l->battle.foes[0].mon.species == 524 && l->battle.foes[0].stages[3] == 2);
	CHECK(l->battle.foes[0].mon.moves[0] == 99 && l->battle.foes[0].mon.pp[0] == 7);
	p16(ram.at(0x021B5138), 0x2800);
	CHECK(settle(rr, ram));
	CHECK(rr.live()->battle.kind == BattleKind::NONE);
}

static void testLink()
{
	std::vector<Range> r = { { 0x02101D40, 4 }, {}, { 0x0227E12C, 2 } };
	std::string q = requestLines(r);
	CHECK(q == "READ_CORE_MEMORY 2101d40 4\nREAD_CORE_MEMORY 227e12c 2\n");
	std::vector<std::vector<uint8_t>> out;
	CHECK(replyBytes("READ_CORE_MEMORY 2101d40 18 e1 27 02\nREAD_CORE_MEMORY 227e12c -1 no memory\n", r, out));
	CHECK(out.size() == 3 && out[0].size() == 4 && out[0][3] == 2 && out[1].empty() && out[2].empty());
}

// the snapshots recorded on the handhelds (ndsram dumps of main RAM): the values seen on the panels
static std::vector<uint8_t> load(const std::string& f)
{
	std::vector<uint8_t> v;
	if (FILE* fp = fopen(f.c_str(), "rb")) { v.resize(0x400000); v.resize(fread(v.data(), 1, v.size(), fp)); fclose(fp); }
	return v;
}
static bool snap(const std::string& f, GameId id, Gen g, std::shared_ptr<const Live>& out)
{
	Ram ram;
	ram.m = load(f);
	if (ram.m.size() != 0x400000) { fprintf(stderr, "(no snapshot %s)\n", f.c_str()); return false; }
	RamReader rr(game(id, g));
	const bool ok = settle(rr, ram);
	if (!ok) fprintf(stderr, "snapshot %s: %s\n", f.c_str(), rr.lastProblem().c_str());
	out = rr.live();
	return ok;
}
static void testSnapshots(const std::string& dir)
{
	std::shared_ptr<const Live> l;
	if (snap(dir + "/plus-b2a/ow.bin", GameId::BLACK2, Gen::V, l))
	{
		CHECK(l->party.size() == 5 && l->party[0].species == 553 && l->party[0].level == 49 && l->party[4].species == 500);
		CHECK(l->party[4].level == 50 && l->party[0].moves[0] == 44 && l->party[0].item == 238);
		CHECK(l->trainerName == "Joar" && l->tid == 33054 && l->money == 52832 && l->badges == 0x0F && l->zone == 96);
		CHECK(l->battle.kind == BattleKind::NONE && l->bag.at(POCKET_BALLS).size() == 2);
	}
	// Black 2 walking on Route 6: the zone and the tile x follow the player (the save's block doesn't)
	if (snap(dir + "/plus-b2b/p0.bin", GameId::BLACK2, Gen::V, l)) CHECK(l->zone == 331 && l->mapX == 110 && l->mapY == 363);
	if (snap(dir + "/plus-b2b/p2.bin", GameId::BLACK2, Gen::V, l)) CHECK(l->zone == 331 && l->mapX == 111 && l->mapY == 363);
	if (snap(dir + "/plus-pta/ow.bin", GameId::PLATINUM, Gen::IV, l))
	{
		CHECK(l->party.size() == 1 && l->party[0].species == 396 && l->party[0].level == 2 && l->party[0].nick == "STARLY");
		CHECK(l->trainerName == "JOAR" && l->tid == 34691 && l->money == 100 && l->zone == 420);
	}
	if (snap(dir + "/rgds-pta/ow.bin", GameId::PLATINUM, Gen::IV, l))
		CHECK(l->party.empty() && l->trainerName == "Joar" && l->zone == 411);
	if (snap(dir + "/plus-hgc/ow.bin", GameId::HEARTGOLD, Gen::IV, l))
	{
		CHECK(l->party.size() == 1 && l->party[0].species == 155 && l->party[0].hp == 16 && l->zone == 34);
		CHECK(l->battle.kind == BattleKind::NONE);
	}
	if (snap(dir + "/plus-hgc/b1.bin", GameId::HEARTGOLD, Gen::IV, l))
	{
		CHECK(l->battle.kind == BattleKind::WILD && l->battle.foes[0].mon.species == 167 && l->battle.foes[0].mon.level == 2);
		CHECK(l->battle.foes[0].types[0] == BUG && l->battle.foes[0].types[1] == POISON && l->battle.ours[0].types[0] == FIRE);
		CHECK(l->battle.foes[0].mon.ivs[0] == 17 && l->battle.foes[0].mon.nature == 16 && l->battle.foes[0].mon.ability == 68);
	}
	{	// Black 2 wild battle (Route 6, Foongus L26): the battler table chain needs a few sets
		Ram ram;
		ram.m = load(dir + "/plus-b2c/b1.bin");
		if (ram.m.size() == 0x400000)
		{
			RamReader rr(game(GameId::BLACK2, Gen::V));
			for (int i = 0; i < 5; i++) rr.deliver(ram.answer(rr.wants()), i * 250.0, 1762000000);
			auto b = rr.live();
			CHECK(b->valid && b->battle.kind == BattleKind::WILD);
			CHECK(b->battle.foes[0].present && b->battle.foes[0].mon.species == 590 && b->battle.foes[0].mon.level == 26);
			CHECK(b->battle.foes[0].mon.hp == 71 && b->battle.foes[0].mon.maxHp == 71 && b->battle.foes[0].mon.nature == 24);
			CHECK(b->battle.ours[0].present && b->battle.ours[0].mon.species == 553 && b->battle.ours[0].mon.hp == 147);
			fprintf(stderr, "b2 battle: foe moves %d %d stages %d %d ours stats %d %d %d %d %d\n", b->battle.foes[0].mon.moves[0],
				b->battle.foes[0].mon.pp[0], b->battle.foes[0].stages[1], b->battle.foes[0].stages[3], b->battle.ours[0].mon.stats[0],
				b->battle.ours[0].mon.stats[1], b->battle.ours[0].mon.stats[2], b->battle.ours[0].mon.stats[3], b->battle.ours[0].mon.stats[4]);
		}
	}
	// HeartGold: our Leer lowered the wild Caterpie's Defence, its String Shot our Speed
	if (snap(dir + "/plus-hgf/b6.bin", GameId::HEARTGOLD, Gen::IV, l))
	{
		CHECK(l->battle.kind == BattleKind::WILD && l->battle.foes[0].mon.species == 10);
		CHECK(l->battle.foes[0].stages[2] == -1 && l->battle.foes[0].stages[1] == 0 && l->battle.ours[0].stages[3] == -1);
	}
	if (snap(dir + "/plus-hgd/c0.bin", GameId::HEARTGOLD, Gen::IV, l))
		CHECK(l->mapX == 566 && l->mapY == 328);
}

int main(int argc, char** argv)
{
	testCrypto();
	testPlatinum();
	testBlack2();
	testLink();
	if (argc > 1) testSnapshots(argv[1]);
	if (fails) { fprintf(stderr, "%d failed\n", fails); return 1; }
	fprintf(stderr, "nds-ram-test: all passed\n");
	return 0;
}
