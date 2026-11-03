// The GBA games' Snapshot as the DS companion's Live (src/gba/gen3live.cpp): types in nds order, species by national
// number, items through the map, the battle's kind, the bag's pockets, the place and the trainer.
#include "gba/gen3live.h"
#include <cstdio>

using namespace banana;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct FakeRom : nds::RomData
{
	std::shared_ptr<const nds::Sprite> sprite(uint16_t, bool, uint8_t) const override { return nullptr; }
};

int main()
{
	CHECK(gba::ndsType(0) == nds::NORMAL && gba::ndsType(8) == nds::STEEL && gba::ndsType(9) == nds::NO_TYPE);
	CHECK(gba::ndsType(10) == nds::FIRE && gba::ndsType(17) == nds::DARK && gba::ndsType(23) == nds::FAIRY);
	pkmn::RomTables t;
	t.natDex.assign(412, 0);
	t.natDex[25] = 25;
	t.natDex[277] = 252;	// (Treecko: internal 277, national 252)
	FakeRom rom;
	rom.species.resize(400);
	rom.species[252].abilities[0] = 65;	// Overgrow
	rom.species[25].abilities[0] = 9;	// Static
	pkmn::Snapshot s;
	CHECK(!gba::toLive(s, &t, &rom)->valid);
	s.seen = true;
	pkmn::Mon m;
	m.species = 277; m.level = 12; m.pid = 0x12345678; m.otId = 0xBEEF0102; m.item = 13; m.ppBonuses = 0b11100100;
	m.moves[0] = 1; m.pp[0] = 35; m.ivs[3] = 31; m.stats[2] = 20;
	s.party.push_back(m);
	s.player = "MAY"; s.tid = 258; s.sid = 48879; s.playSeconds = 3725;
	s.group = 0; s.num = 16; s.x = 5; s.y = 9; s.money = 3000; s.badges = 0b101;
	s.dexOwned = { 0x01 }; s.dexSeen = { 0x02 };
	s.bag = true;
	s.pockets[2].push_back({ 4, 10 });		// Poké Balls x10
	s.pockets[0].push_back({ 13, 2 });		// Potion (Gen III 13)
	auto item = [](uint16_t i) -> uint16_t { return i == 13 ? 17 : i; };	// (Gen IV's Potion is 17)
	auto l = gba::toLive(s, &t, &rom, item);
	CHECK(l->valid && l->trainerName == "MAY" && l->tid == 258 && l->sid == 48879 && l->playSeconds == 3725);
	CHECK(l->party.size() == 1);
	const nds::Mon& p = l->party[0];
	CHECK(p.species == 252 && p.level == 12 && p.item == 17 && p.ability == 65 && p.nature == 0x12345678 % 25);
	CHECK(p.otId == 0x0102 && p.otSecret == 0xBEEF);
	CHECK(p.ppUps[0] == 0 && p.ppUps[1] == 1 && p.ppUps[2] == 2 && p.ppUps[3] == 3);
	CHECK(p.moves[0] == 1 && p.pp[0] == 35 && p.ivs[3] == 31 && p.stats[2] == 20);
	CHECK(l->zone == 16 && l->mapX == 5 && l->mapY == 9 && l->money == 3000 && l->badges == 5);
	CHECK(l->dexCaught.size() == 1 && l->dexSeen[0] == 0x03);	// (caught is seen)
	CHECK(l->bag[nds::POCKET_BALLS].size() == 1 && l->bag[nds::POCKET_BALLS][0].item == 4 && l->bag[nds::POCKET_BALLS][0].count == 10);
	CHECK(l->bag[nds::POCKET_ITEMS][0].item == 17);
	CHECK(l->battle.kind == nds::BattleKind::NONE);
	// a wild battle: the lead and the foe with the battle copy's types
	s.inBattle = true;
	s.lead = m; s.lead.hp = 30; s.lead.types[0] = 12; s.lead.types[1] = 12;	// Grass
	pkmn::Mon f; f.species = 25; f.level = 5; f.hp = 19; f.types[0] = 13; f.types[1] = 13;	// Electric
	s.foes.push_back(f);
	l = gba::toLive(s, &t, &rom, item);
	CHECK(l->battle.kind == nds::BattleKind::WILD);
	CHECK(l->battle.ours[0].present && l->battle.ours[0].types[0] == nds::GRASS && l->battle.ours[0].mon.species == 252);
	CHECK(l->battle.foes[0].present && l->battle.foes[0].types[0] == nds::ELECTRIC && l->battle.foes[0].mon.ability == 9);
	CHECK(!l->battle.foes[1].present);
	s.trainerBattle = true; s.doubles = true;
	CHECK(gba::toLive(s, &t, &rom, item)->battle.kind == nds::BattleKind::DOUBLE_TRAINER);
	if (fails) { fprintf(stderr, "gba-live: %d FAILED\n", fails); return 1; }
	fprintf(stderr, "gba-live: all passed\n");
	return 0;
}
