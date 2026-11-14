// banana-ui: a GBA Pokémon game's Snapshot as the DS companion's Live (gen3live.h).
#include "gba/gen3live.h"

namespace banana::gba
{
	nds::Type ndsType(uint8_t t, int fairy)
	{
		if (fairy >= 0 && t == fairy) return nds::FAIRY;	// (CFRU's 23, a Glazed release's 9)
		if (t < 9) return (nds::Type)t;					// Normal .. Steel
		if (t >= 10 && t <= 17) return (nds::Type)(t - 1);	// Fire .. Dark (??? at 9 in Gen III)
		return nds::NO_TYPE;
	}

	static uint16_t national(uint16_t species, const pkmn::RomTables* t)
	{
		if (!t || t->natDex.empty()) return species;
		return species < t->natDex.size() ? t->natDex[species] : 0;
	}

	nds::Mon toMon(const pkmn::Mon& m, const pkmn::RomTables* tables, const nds::RomData* rom, const ItemMap& item, bool party,
		const SpeciesMap& species)
	{
		nds::Mon o;
		o.pid = m.pid;
		uint16_t nat = 0; uint8_t form = 0;
		if (species && m.species && species(m.species, nat, form)) { o.species = nat; o.form = form; }
		else o.species = species && m.species ? 0 : national(m.species, tables);
		o.item = m.item ? (item ? item(m.item) : m.item) : 0;
		o.level = m.level;
		o.friendship = m.friendship;
		o.nick = m.nick;
		// (a Pokémon not nicknamed has its species' name in the game's capitals: as the DS pages write it)
		if (rom && o.species && o.species < rom->species.size())
		{
			std::string up = rom->species[o.species].name;
			for (char& c : up) if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
			if (m.nick == up) o.nick = rom->species[o.species].name;
		}
		o.otId = (uint16_t)(m.otId & 0xffff);
		o.otSecret = (uint16_t)(m.otId >> 16);
		o.exp = m.exp;
		o.hp = m.hp;
		o.maxHp = m.maxHp;
		for (int i = 0; i < 5; i++) o.stats[i] = m.stats[i];	// (both: Attack, Defense, Speed, Sp. Atk, Sp. Def)
		for (int i = 0; i < 6; i++) { o.ivs[i] = m.ivs[i]; o.evs[i] = m.evs[i]; }
		for (int k = 0; k < 4; k++)
		{
			o.moves[k] = m.moves[k];		// (Gen III's move ids are Gen IV's)
			o.pp[k] = m.pp[k];
			o.ppUps[k] = (uint8_t)((m.ppBonuses >> (2 * k)) & 3);
		}
		o.status = m.status;				// (the same bits: sleep turns 0-2, PSN, BRN, FRZ, PAR, TOX)
		o.egg = m.egg;
		o.nature = (uint8_t)(m.pid % 25);
		if (tables && m.species) o.female = tables->gender(m.species, m.pid) == 1;
		// the ability: the species' first or second (party members say which; a battler: the first)
		if (rom && o.species && o.species < rom->species.size())
		{
			// (a form's own: its abilities)
			const auto& base = rom->species[o.species];
			const size_t fi = o.form && base.formsAt && o.form < base.formCount ? base.formsAt + o.form - 1u : o.species;
			const auto& sp = fi < rom->species.size() ? rom->species[fi] : base;
			const int slot = party ? m.abilitySlot : 0;
			o.ability = slot == 1 && sp.abilities[1] ? sp.abilities[1] : sp.abilities[0];
		}
		return o;
	}

	static nds::Battler battler(const pkmn::Mon& m, const pkmn::RomTables* t, const nds::RomData* rom, const ItemMap& item,
		const SpeciesMap& species)
	{
		nds::Battler b;
		b.mon = toMon(m, t, rom, item, false, species);
		b.present = m.species != 0 && m.hp > 0;
		const uint8_t t0 = m.types[0], t1 = m.types[1] == 255 ? m.types[0] : m.types[1];
		const int fairy = t && t->has.fairyType >= 0 ? t->has.fairyType : pkmn::FAIRY;
		b.types[0] = ndsType(t0, fairy);
		b.types[1] = ndsType(t1, fairy);
		if (b.types[0] == nds::NO_TYPE && rom && b.mon.species < rom->species.size())	// (no battle copy's types: the species')
		{
			b.types[0] = rom->species[b.mon.species].types[0];
			b.types[1] = rom->species[b.mon.species].types[1];
		}
		return b;
	}

	std::shared_ptr<nds::Live> toLive(const pkmn::Snapshot& s, const pkmn::RomTables* t, const nds::RomData* rom, const ItemMap& item,
		const SpeciesMap& species)
	{
		auto l = std::make_shared<nds::Live>();
		l->valid = s.seen && !s.unknownRelease && !s.party.empty();
		if (!l->valid) return l;
		l->trainerName = s.player;
		if (s.tid >= 0) l->tid = (uint16_t)s.tid;
		l->trainerKnown = s.tid >= 0;
		if (s.sid >= 0) l->sid = (uint16_t)s.sid;
		for (const auto& m : s.party) l->party.push_back(toMon(m, t, rom, item, true, species));
		if (s.inBattle && s.lead.species)
		{
			nds::Battle& b = l->battle;
			b.kind = s.trainerBattle ? (s.doubles ? nds::BattleKind::DOUBLE_TRAINER : nds::BattleKind::TRAINER)
				: (s.doubles ? nds::BattleKind::DOUBLE_WILD : nds::BattleKind::WILD);
			b.ours[0] = battler(s.lead, t, rom, item, species);
			for (size_t i = 0; i < s.foes.size() && i < 2; i++) b.foes[i] = battler(s.foes[i], t, rom, item, species);
			for (const auto& m : s.foeParty) b.foeParty.push_back(toMon(m, t, rom, item, true, species));
			b.turn = 1;		// (not read: the logic's turn-counting balls take the first turn)
		}
		if (s.group >= 0 && s.num >= 0) l->zone = (uint16_t)(s.group << 8 | s.num);
		l->mapX = (uint16_t)s.x;
		l->mapY = (uint16_t)s.y;
		if (s.money >= 0) l->money = (uint32_t)s.money;
		if (s.playSeconds >= 0) l->playSeconds = (uint32_t)s.playSeconds;
		if (s.badges >= 0) l->badges = (uint16_t)s.badges;
		l->trainersBeaten = s.trainersBeaten;
		l->dexSeen = s.dexSeen;
		l->dexCaught = s.dexOwned;
		for (size_t j = 0; j < l->dexSeen.size() && j < l->dexCaught.size(); j++) l->dexSeen[j] |= l->dexCaught[j];	// (caught is seen)
		if (s.bag)
		{
			static const nds::Pocket POCKETS[5] = { nds::POCKET_ITEMS, nds::POCKET_KEY, nds::POCKET_BALLS, nds::POCKET_TMS, nds::POCKET_BERRIES };
			for (int k = 0; k < 5; k++)
				for (const auto& e : s.pockets[k]) l->bag[POCKETS[k]].push_back({ item ? item(e.item) : e.item, e.count });
		}
		l->timeOfDay = nds::TimeOfDay::DAY;	// (Gen III has no day and night that changes encounters)
		return l;
	}
}
