// banana-ui: the DS Pokémon companion's ROM reader (rom.h)
#include "nds/rom.h"
#include "nds/nitro.h"
#include "nds/townmap.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

namespace banana::nds
{
	using nitro::Bytes;
	using nitro::Narc;
	using nitro::Span;

	namespace
	{
		// ---- where each game keeps what (RESEARCH.md §6.3; the text banks checked on the English releases) ------
		struct Files
		{
			const char *text, *personal, *learn, *evo, *moves, *items, *enc, *trdata, *trpoke, *sprites;
			// text banks
			int bSpecies, bMoves, bMoveDesc, bAbilities, bAbilityDesc, bItems, bItemDesc, bNatures, bLocations, bTrNames,
				bTrClasses, bDex, bCategory;
		};

		const Files* filesFor(GameId id)
		{
			static const Files D = { "msgdata/msg.narc", "poketool/personal/personal.narc", "poketool/personal/wotbl.narc",
				"poketool/personal/evo.narc", "poketool/waza/waza_tbl.narc", "itemtool/itemdata/item_data.narc",
				"fielddata/encountdata/d_enc_data.narc", "poketool/trainer/trdata.narc", "poketool/trainer/trpoke.narc",
				"poketool/pokegra/pokegra.narc", 362, 588, 587, 552, 554, 344, 343, 190, 382, 559, 560, 615, 621 };
			static const Files P = [] { Files f = D; f.personal = "poketool/personal_pearl/personal.narc";
				f.enc = "fielddata/encountdata/p_enc_data.narc"; f.bDex = 616; return f; }();
			static const Files PT = { "msgdata/pl_msg.narc", "poketool/personal/pl_personal.narc", "poketool/personal/wotbl.narc",
				"poketool/personal/evo.narc", "poketool/waza/pl_waza_tbl.narc", "itemtool/itemdata/pl_item_data.narc",
				"fielddata/encountdata/pl_enc_data.narc", "poketool/trainer/trdata.narc", "poketool/trainer/trpoke.narc",
				"poketool/pokegra/pl_pokegra.narc", 412, 647, 646, 610, 612, 392, 391, 202, 433, 618, 619, 706, 711 };
			static const Files HG = { "a/0/2/7", "a/0/0/2", "a/0/3/3", "a/0/3/4", "a/0/1/1", "a/0/1/7", "a/0/3/7", "a/0/5/5",
				"a/0/5/6", "a/0/0/4", 237, 750, 749, 720, 722, 222, 221, 34, 279, 729, 730, 803, 816 };
			static const Files SS = [] { Files f = HG; f.enc = "a/1/3/6"; f.bDex = 804; return f; }();
			static const Files B = { "a/0/0/2", "a/0/1/6", "a/0/1/8", "a/0/1/9", "a/0/2/1", "a/0/2/4", "a/1/2/6", "a/0/9/2",
				"a/0/9/3", "a/0/0/4", 70, 203, 202, 182, 183, 54, 53, 24, 89, 190, 191, 236, 260 };
			// (Black's Pokédex text is bank 236, White's 235: Reshiram's and Zekrom's entries tell them apart)
			static const Files W = [] { Files f = B; f.bDex = 235; return f; }();
			static const Files B2 = { "a/0/0/2", "a/0/1/6", "a/0/1/8", "a/0/1/9", "a/0/2/1", "a/0/2/4", "a/1/2/7", "a/0/9/1",
				"a/0/9/2", "a/0/0/4", 90, 403, 402, 374, 375, 64, 63, 27, 109, 382, 383, 442, 464 };
			switch (id)
			{
			case GameId::DIAMOND: return &D;
			case GameId::PEARL: return &P;
			case GameId::PLATINUM: return &PT;
			case GameId::HEARTGOLD: return &HG;
			case GameId::SOULSILVER: return &SS;
			case GameId::BLACK: return &B;
			case GameId::WHITE: return &W;
			case GameId::BLACK2: case GameId::WHITE2: return &B2;
			default: return nullptr;
			}
		}

		// Gen IV's heights (0.1 m) and weights (0.1 kg): members 0 and 1, a u32 per species (Gen V has them in the
		// personal data)
		const char* dexDataFor(GameId id)
		{
			if (id == GameId::HEARTGOLD || id == GameId::SOULSILVER) return "a/0/7/4";
			if (id == GameId::DIAMOND || id == GameId::PEARL || id == GameId::PLATINUM) return "application/zukanlist/zkn_data/zukan_data.narc";
			return nullptr;
		}

		bool isHgss(GameId id) { return id == GameId::HEARTGOLD || id == GameId::SOULSILVER; }
		bool isB2w2(GameId id) { return id == GameId::BLACK2 || id == GameId::WHITE2; }
		bool isDp(GameId id) { return id == GameId::DIAMOND || id == GameId::PEARL; }

		// Gen IV's type numbers have the ??? type at 9 (Curse's); model.h's Type has none
		Type typeOf(uint8_t t, Gen gen)
		{
			if (gen == Gen::IV) { if (t == 9) return NO_TYPE; if (t > 9) t--; }
			return t < NUM_TYPES ? (Type)t : NO_TYPE;
		}

		// soft line breaks of the DS's text boxes become spaces (the companion lays text out itself)
		std::string flowed(std::string s)
		{
			for (size_t i = 0; i < s.size(); i++)
				if (s[i] == '\n') s[i] = (i > 0 && s[i - 1] == '-') ? '\x01' : ' ';	// ("Backen-\ntaschen": joined)
			s.erase(std::remove(s.begin(), s.end(), '\x01'), s.end());
			return s;
		}
	}

	// ---- the RomData the loader returns ----------------------------------------------------------------------------
	class RomImpl : public RomData
	{
	public:
		std::string path;
		std::map<uint16_t, std::pair<uint16_t, uint8_t>> forms;	// (while decoding) species -> index of form 1, number of forms
		std::map<uint16_t, std::pair<uint16_t, uint8_t>> formSprites;	// Gen V: species -> its form 1's picture (after the species'), forms
		std::shared_ptr<const Sprite> sprite(uint16_t species, bool female, uint8_t form) const override;
		mutable std::mutex lock;
		mutable std::unique_ptr<nitro::Rom> rom;
		mutable std::map<uint32_t, std::shared_ptr<const Sprite>> sprites;	// the last few asked for
		mutable nitro::NarcFile spriteNarc;	// the sprite archive's member table (sprite())
		mutable int formOrigin = -1;	// Gen V: where the forms' pictures start in the sprite archive (0: not found)
	};

	uint16_t speciesIndex(const RomData& r, uint16_t species, uint8_t form)
	{
		if (form == 0 || species >= r.species.size()) return species;
		const Species& s = r.species[species];
		if (s.formsAt == 0 || form >= s.formCount) return species;
		uint16_t i = (uint16_t)(s.formsAt + form - 1);
		return i < r.species.size() ? i : species;
	}

	// ---- decoding -----------------------------------------------------------------------------------------------------
	namespace
	{
		struct Decoder
		{
			const nitro::Rom& rom;
			const Files& f;
			RomImpl& out;
			std::string err;
			Gen gen() const { return out.game.gen; }
			GameId id() const { return out.game.id; }

			bool narc(const char* path, Bytes& buf, Narc& n)
			{
				if (!rom.file(path, buf) || !n.parse(buf)) { err = std::string("can't read ") + path; return false; }
				return true;
			}

			Bytes textBuf;
			Narc text;
			std::vector<int> zoneWild;	// per zone its encounter file (-1 none)
			size_t mapHeaders = SIZE_MAX;	// Gen IV: where the map header table is in the ARM9 (zones())
			std::vector<std::string> bank(int i)
			{
				Span s = text[(size_t)i];
				return gen() == Gen::IV ? nitro::gen4Text(s.p, s.n) : nitro::gen5Text(s.p, s.n);
			}

			std::string evoHow(uint16_t method, uint16_t param) const
			{
				auto item = [&](uint16_t i) { return i < out.items.size() ? out.items[i].name : std::string("an item"); };
				auto move = [&](uint16_t m) { return m < out.moves.size() ? out.moves[m].name : std::string("a move"); };
				auto mon = [&](uint16_t s) { return s < out.species.size() ? out.species[s].name : std::string("a Pokémon"); };
				std::string lv = "Level " + std::to_string(param);
				switch (method)
				{
				case evo::FRIENDSHIP: return "Level up with high friendship";
				case evo::FRIENDSHIP_DAY: return "Level up with high friendship by day";
				case evo::FRIENDSHIP_NIGHT: return "Level up with high friendship at night";
				case evo::LEVEL: return lv;
				case evo::TRADE: return "Trade";
				case evo::TRADE_ITEM: return "Trade holding " + item(param);
				case evo::TRADE_SPECIES: return "Trade for " + mon(param);
				case evo::USE_ITEM: return "Use " + item(param);
				case evo::LEVEL_ATK_GT_DEF: return lv + " with Attack higher than Defense";
				case evo::LEVEL_ATK_EQ_DEF: return lv + " with Attack equal to Defense";
				case evo::LEVEL_ATK_LT_DEF: return lv + " with Defense higher than Attack";
				case evo::LEVEL_PID_LOW: case evo::LEVEL_PID_HIGH: return lv + " (one of two, by its personality)";
				case evo::LEVEL_NINJASK: return lv;
				case evo::LEVEL_SHEDINJA: return lv + " with a free party slot and a spare Poké Ball";
				case evo::BEAUTY: return "Level up with high Beauty";
				case evo::USE_ITEM_MALE: return "Use " + item(param) + " on a male";
				case evo::USE_ITEM_FEMALE: return "Use " + item(param) + " on a female";
				case evo::HOLD_ITEM_DAY: return "Level up holding " + item(param) + " by day";
				case evo::HOLD_ITEM_NIGHT: return "Level up holding " + item(param) + " at night";
				case evo::KNOWS_MOVE: return "Level up knowing " + move(param);
				case evo::SPECIES_IN_PARTY: return "Level up with " + mon(param) + " in the party";
				case evo::LEVEL_MALE: return lv + ", male";
				case evo::LEVEL_FEMALE: return lv + ", female";
				case evo::MAGNETIC_FIELD: return "Level up in a special magnetic field";
				case evo::MOSS_ROCK: return "Level up near a Moss Rock";
				case evo::ICE_ROCK: return "Level up near an Ice Rock";
				default: return "";
				}
			}

			bool texts()
			{
				if (!narc(f.text, textBuf, text)) return false;
				auto names = bank(f.bSpecies);
				if (names.size() <= (size_t)out.game.dexSize) { err = "the species names aren't where this game keeps them"; return false; }
				out.species.resize(names.size());
				for (size_t i = 0; i < names.size(); i++) out.species[i].name = names[i];
				auto cats = bank(f.bCategory), dex = bank(f.bDex);
				for (size_t i = 0; i < out.species.size(); i++)
				{
					if (i < cats.size()) out.species[i].category = cats[i];
					if (i < dex.size()) out.species[i].dexText = flowed(dex[i]);
				}
				auto mn = bank(f.bMoves), md = bank(f.bMoveDesc);
				out.moves.resize(mn.size());
				for (size_t i = 0; i < mn.size(); i++) { out.moves[i].name = mn[i]; if (i < md.size()) out.moves[i].description = flowed(md[i]); }
				out.abilities = bank(f.bAbilities);
				auto ad = bank(f.bAbilityDesc);
				out.abilityText.resize(out.abilities.size());
				for (size_t i = 0; i < out.abilityText.size() && i < ad.size(); i++) out.abilityText[i] = flowed(ad[i]);
				out.natures = bank(f.bNatures);
				auto in = bank(f.bItems), idesc = bank(f.bItemDesc);
				out.items.resize(in.size());
				for (size_t i = 0; i < in.size(); i++) { out.items[i].name = in[i]; if (i < idesc.size()) out.items[i].description = flowed(idesc[i]); }
				out.locationNames = bank(f.bLocations);
				return true;
			}

			// personal data: 44 bytes (Gen IV), 60 (Black/White), 76 (Black 2/White 2): RESEARCH.md §6.4
			bool personal()
			{
				Bytes buf; Narc n;
				if (!narc(f.personal, buf, n)) return false;
				size_t count = n.size();
				if (gen() == Gen::V) while (count && n[count - 1].n < 0x28) count--;	// (the table files after the entries)
				if (count <= (size_t)out.game.dexSize) { err = "too few species in the personal data"; return false; }
				if (out.species.size() < count) out.species.resize(count);
				for (size_t i = 0; i < count; i++)
				{
					Span p = n[i];
					Species& s = out.species[i];
					if (p.n < 0x28) continue;
					for (int k = 0; k < 6; k++) s.base[k] = p.u8(k);
					s.types[0] = typeOf(p.u8(6), gen());
					s.types[1] = typeOf(p.u8(7), gen());
					s.catchRate = p.u8(8);
					uint16_t ev = p.u16(0x0a);
					for (int k = 0; k < 6; k++) s.evYield[k] = ev >> (2 * k) & 3;
					uint32_t tmAt;
					if (gen() == Gen::IV)
					{
						s.baseExp = p.u8(9);
						s.genderRatio = p.u8(0x10); s.baseFriendship = p.u8(0x12); s.growth = p.u8(0x13);
						s.abilities[0] = p.u8(0x16); s.abilities[1] = p.u8(0x17);
						tmAt = 0x1c;
					}
					else
					{
						s.genderRatio = p.u8(0x12); s.baseFriendship = p.u8(0x14); s.growth = p.u8(0x15);
						s.abilities[0] = p.u8(0x18); s.abilities[1] = p.u8(0x19); s.abilities[2] = p.u8(0x1a);
						s.baseExp = p.u16(0x22); s.height = p.u16(0x24); s.weight = p.u16(0x26);
						tmAt = 0x28;
						uint16_t formsAt = p.u16(0x1c);
						uint8_t formCount = p.u8(0x20);
						if (formCount > 1 && formsAt > 0 && formsAt < count && i <= (size_t)out.game.dexSize) out.forms[(uint16_t)i] = { formsAt, formCount };
						// the forms' pictures: 0x1E counts from the first picture after the species'. Species whose forms
						// are only other colours (Arceus) say 0 too: a start belongs to the lowest species claiming it.
						if (formCount > 1 && i <= (size_t)out.game.dexSize)
						{
							uint16_t first = p.u16(0x1e);
							bool taken = false;
							for (auto& fs : out.formSprites) taken = taken || fs.second.first == first;
							if (!taken) out.formSprites[(uint16_t)i] = { first, formCount };
						}
					}
					// TM/HM bits: TM01.. then the HMs, in the order model.h numbers them
					int tmCount = gen() == Gen::IV ? 100 : 101;
					for (int k = 0; k < tmCount; k++) if (p.u8(tmAt + k / 8) >> (k % 8) & 1) s.tms.push_back((uint16_t)(k + 1));
					if (s.abilities[1] == s.abilities[0]) s.abilities[1] = 0;
				}
				if (const char* dd = dexDataFor(id()))
				{
					Bytes zb; Narc zn;
					if (rom.file(dd, zb) && zn.parse(zb) && zn.size() >= 2)
						for (size_t i = 0; i < out.species.size() && i <= (size_t)out.game.dexSize; i++)
						{
							out.species[i].height = (uint16_t)zn[0].u32(4 * i);
							out.species[i].weight = (uint16_t)zn[1].u32(4 * i);
						}
				}
				// Gen IV's forms: after the species and the two egg entries, in the order the games' code looks them up
				// (pret pokeplatinum Pokemon_GetFormNarcIndex): Deoxys' three, Wormadam's two, then Platinum/HGSS's
				// Giratina, Shaymin and Rotom's five
				if (gen() == Gen::IV)
				{
					struct { uint16_t species; uint8_t count; } table[] = { { 386, 4 }, { 413, 3 }, { 487, 2 }, { 492, 2 }, { 479, 6 } };
					uint16_t at = 496;
					for (auto& t : table)
					{
						if ((size_t)(at + t.count - 1) > count) break;
						out.forms[t.species] = { at, t.count };
						at = (uint16_t)(at + t.count - 1);
					}
				}
				for (auto& fm : out.forms)
					if (fm.first < out.species.size()) { out.species[fm.first].formsAt = fm.second.first; out.species[fm.first].formCount = fm.second.second; }
				// the forms' names: their species'
				for (auto& fm : out.forms)
					for (int k = 1; k < fm.second.second; k++)
					{
						size_t i = fm.second.first + k - 1;
						if (i < out.species.size() && fm.first < out.species.size())
						{
							Species& s = out.species[i];
							if (s.name.empty() || i > (size_t)out.game.dexSize) s.name = out.species[fm.first].name;
							if (s.category.empty()) s.category = out.species[fm.first].category;
						}
					}
				return true;
			}

			bool learnsets()
			{
				Bytes buf; Narc n;
				if (!narc(f.learn, buf, n)) return false;
				for (size_t i = 0; i < n.size() && i < out.species.size(); i++)
				{
					Span p = n[i];
					auto& ls = out.species[i].learnset;
					if (gen() == Gen::IV)
						for (size_t o = 0; o + 2 <= p.n; o += 2)
						{
							uint16_t v = p.u16(o);
							if (v == 0xffff) break;
							ls.push_back({ (uint8_t)(v >> 9), (uint16_t)(v & 0x1ff) });
						}
					else
						for (size_t o = 0; o + 4 <= p.n; o += 4)
						{
							uint16_t m = p.u16(o), l = p.u16(o + 2);
							if (m == 0xffff && l == 0xffff) break;
							ls.push_back({ (uint8_t)l, m });
						}
				}
				return true;
			}

			bool evolutions()
			{
				Bytes buf; Narc n;
				if (!narc(f.evo, buf, n)) return false;
				for (size_t i = 0; i < n.size() && i < out.species.size(); i++)
				{
					Span p = n[i];
					for (size_t k = 0; k < 7 && k * 6 + 6 <= p.n; k++)
					{
						uint16_t method = p.u16(k * 6), param = p.u16(k * 6 + 2), into = p.u16(k * 6 + 4);
						if (method == 0 || into == 0) continue;
						if (gen() == Gen::IV && method >= 7) method++;	// (to Gen V's numbering)
						out.species[i].evolutions.push_back({ method, param, into, "" });
					}
				}
				return true;
			}

			bool moves()
			{
				Bytes buf; Narc n;
				if (!narc(f.moves, buf, n)) return false;
				if (out.moves.size() < n.size()) out.moves.resize(n.size());
				for (size_t i = 0; i < n.size(); i++)
				{
					Span p = n[i];
					MoveData& m = out.moves[i];
					if (gen() == Gen::IV)
					{
						// u16 effect, u8 class, u8 power, u8 type, u8 accuracy (0 never misses), u8 PP, u8 effect
						// chance, u16 range, s8 priority
						if (p.n < 12) continue;
						m.effect = p.u16(0); m.cat = (MoveCategory)std::min<uint8_t>(p.u8(2), 2); m.power = p.u8(3);
						m.type = typeOf(p.u8(4), gen()); m.accuracy = p.u8(5); m.pp = p.u8(6); m.effectChance = p.u8(7);
						m.priority = p.u8(10);
					}
					else
					{
						// type, effect category, class, power, accuracy (101 never misses), PP, s8 priority, ...,
						// status chance at 10, effect at 16
						if (p.n < 18) continue;
						// (class: 0 status, 1 physical, 2 special)
						static const MoveCategory CLS[3] = { MoveCategory::STATUS, MoveCategory::PHYSICAL, MoveCategory::SPECIAL };
						m.type = typeOf(p.u8(0), gen()); m.cat = CLS[std::min<uint8_t>(p.u8(2), 2)]; m.power = p.u8(3);
						m.accuracy = p.u8(4) > 100 ? 0 : p.u8(4); m.pp = p.u8(5); m.priority = p.u8(6);
						m.effectChance = p.u8(10); m.effect = p.u16(16);
					}
				}
				return true;
			}

			bool items()
			{
				Bytes buf; Narc n;
				if (!narc(f.items, buf, n)) return false;
				for (size_t i = 0; i < n.size() && i < out.items.size(); i++)
				{
					Span p = n[i];
					if (p.n < 10) continue;
					// u16 price (Gen V keeps a tenth of it), ..., at 8 a u16 whose bits 7-10 are the bag pocket
					out.items[i].price = (uint16_t)std::min<uint32_t>(p.u16(0) * (gen() == Gen::V ? 10u : 1u), 0xffff);
					out.items[i].pocket = (uint8_t)(p.u16(8) >> 7 & 15);
				}
				return true;
			}

			// TM01.. as the ARM9 lists them: 100 (Gen IV: 92 TMs, 8 HMs) or 101 (Gen V: TM01-92, HM01-06, TM93-95)
			// move numbers, all different, then a 0. No table offsets per release: the first such run is the one.
			bool tms(const Bytes& arm9)
			{
				size_t n = gen() == Gen::IV ? 100 : 101, maxMove = out.moves.size();
				for (size_t i = 0; i + 2 * (n + 1) <= arm9.size(); i += 2)
				{
					bool ok = true;
					std::vector<bool> seen(maxMove, false);
					for (size_t k = 0; k < n && ok; k++)
					{
						uint16_t m = nitro::rd16(&arm9[i + 2 * k]);
						ok = m > 0 && m < maxMove && !seen[m];
						if (ok) seen[m] = true;
					}
					if (!ok || nitro::rd16(&arm9[i + 2 * n]) != 0) continue;
					std::vector<uint16_t> t(n);
					for (size_t k = 0; k < n; k++) t[k] = nitro::rd16(&arm9[i + 2 * k]);
					if (gen() == Gen::V) std::rotate(t.begin() + 92, t.begin() + 98, t.end());	// TM93-95 before the HMs
					out.tmMoves = t;
					return true;
				}
				err = "no TM list in the ARM9";
				return false;
			}

			// ---- wild encounters ---------------------------------------------------------------------------------
			// cond: Encounter::condition (the dual-slot game 1 Ruby .. 5 LeafGreen; the swarm's kind 0 grass 1 surfing
			// 2 fishing; the headbutt tree 0 common, 1 rare, 2 the third list); entries differing in it stay apart
			static void add(std::vector<Encounter>& v, uint16_t species, uint8_t lo, uint8_t hi, uint8_t pct, EncounterKind k,
				TimeOfDay t = TimeOfDay::ANY, uint8_t season = 0xff, uint8_t form = 0, uint8_t cond = 0)
			{
				if (species == 0 || pct == 0) return;
				if (lo > hi) std::swap(lo, hi);
				// the same Pokémon in two slots of one kind and time: one entry with both odds
				for (size_t i = 0; i < v.size(); i++)
				{
					Encounter& e = v[i];
					if (e.species == species && e.kind == k && e.time == t && e.season == season && e.form == form && e.condition == cond)
					{
						e.minLevel = std::min(e.minLevel, lo); e.maxLevel = std::max(e.maxLevel, hi);
						e.percent = (uint8_t)std::min(100, e.percent + pct);
						return;
					}
				}
				Encounter e;
				e.species = species; e.minLevel = lo; e.maxLevel = hi; e.percent = pct; e.kind = k; e.time = t; e.season = season; e.form = form; e.condition = cond;
				v.push_back(e);
			}

			// HeartGold/SoulSilver's headbutt trees (a/2/5/2, one member per zone): u16 common trees, u16 rare
			// trees, then 3 x 6 slots {u16 species, u8 min, u8 max} (common, rare, and a third list), then the trees'
			// coordinates. Slot odds from pokeheartgold field/encounter_check.c: 50, 15, 15, 10, 5, 5.
			void headbutt(Span p, std::vector<Encounter>& v)
			{
				static const uint8_t PCT[6] = { 50, 15, 15, 10, 5, 5 };
				if (p.n < 4 + 18 * 4) return;
				for (int t = 0; t < 3; t++)
					for (int k = 0; k < 6; k++)
					{
						size_t o = 4 + (size_t)(t * 6 + k) * 4;
						add(v, p.u16(o), p.u8(o + 2), p.u8(o + 3), PCT[k], EncounterKind::HEADBUTT, TimeOfDay::ANY, 0xff, 0, (uint8_t)t);
					}
			}

			static constexpr uint8_t GRASS_PCT[12] = { 20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1 };

			// Diamond/Pearl/Platinum, 424 bytes (RESEARCH.md §6.4; decomp overlay006 wild_encounters.h)
			std::vector<Encounter> encDppt(Span p)
			{
				std::vector<Encounter> v;
				if (p.n < 424) return v;
				auto sp = [&](size_t o) { return (uint16_t)(p.u32(o) & 0xffff); };
				uint8_t lv[12];
				uint16_t base[12];
				for (int k = 0; k < 12; k++) { lv[k] = (uint8_t)p.u32(4 + 8 * k); base[k] = sp(8 + 8 * k); }
				if (p.u32(0))
				{
					// slots 2 and 3 are the morning's: by day and at night the game puts its day/night pair there
					uint16_t day[2] = { sp(0x6c), sp(0x70) }, night[2] = { sp(0x74), sp(0x78) };
					for (int k = 0; k < 12; k++)
					{
						bool timed = (k == 2 || k == 3) && (day[k - 2] != base[k] || night[k - 2] != base[k]);
						add(v, base[k], lv[k], lv[k], GRASS_PCT[k], EncounterKind::GRASS, timed ? TimeOfDay::MORNING : TimeOfDay::ANY);
						if (timed) { add(v, day[k - 2], lv[k], lv[k], GRASS_PCT[k], EncounterKind::GRASS, TimeOfDay::DAY);
							add(v, night[k - 2], lv[k], lv[k], GRASS_PCT[k], EncounterKind::GRASS, TimeOfDay::NIGHT); }
					}
					for (int k = 0; k < 2; k++) add(v, sp(0x64 + 4 * k), lv[k], lv[k], GRASS_PCT[k], EncounterKind::SWARM);
					static const int radarSlot[4] = { 4, 5, 10, 11 };
					for (int k = 0; k < 4; k++) add(v, sp(0x7c + 4 * k), lv[radarSlot[k]], lv[radarSlot[k]], GRASS_PCT[radarSlot[k]], EncounterKind::RADAR);
					// a Gen III game in the DS's second slot: slots 8 and 9 (Ruby, Sapphire, Emerald, FireRed, LeafGreen)
					for (int g = 0; g < 5; g++)
						for (int k = 0; k < 2; k++) add(v, sp(0xa4 + 8 * g + 4 * k), lv[8 + k], lv[8 + k], GRASS_PCT[8 + k], EncounterKind::DUAL_SLOT, TimeOfDay::ANY, 0xff, 0, (uint8_t)(g + 1));
				}
				// water: rate, then 5 x {max, min, pad, s32 species}
				static const uint8_t SURF[5] = { 60, 30, 5, 4, 1 }, OLD[5] = { 60, 30, 5, 4, 1 }, ROD[5] = { 40, 40, 15, 4, 1 };
				auto water = [&](size_t o, const uint8_t* pct, EncounterKind k) {
					if (!p.u32(o)) return;
					for (int s = 0; s < 5; s++) add(v, sp(o + 4 + 8 * s + 4), p.u8(o + 4 + 8 * s + 1), p.u8(o + 4 + 8 * s), pct[s], k);
				};
				water(0xcc, SURF, EncounterKind::SURF);
				water(0x124, OLD, EncounterKind::OLD_ROD);
				water(0x150, ROD, EncounterKind::GOOD_ROD);
				water(0x17c, ROD, EncounterKind::SUPER_ROD);
				return v;
			}

			// HeartGold/SoulSilver, 196 bytes (pokeheartgold wild_encounter.h; slot odds and the swarm, radio and
			// night-fishing replacements from its field/encounter_check.c)
			// swarm: which of the file's swarm species this zone can have (-1 none, 0 grass, 1 surfing, 2 fishing; the
			// games keep a swarm species in every file but use it only on the maps of their swarm table)
			std::vector<Encounter> encHgss(Span p, int swarm)
			{
				std::vector<Encounter> v;
				if (p.n < 0xc4) return v;
				if (p.u8(0))
				{
					for (int k = 0; k < 12; k++)
					{
						uint8_t l = p.u8(8 + k);
						uint16_t m = p.u16(0x14 + 2 * k), d = p.u16(0x2c + 2 * k), n = p.u16(0x44 + 2 * k);
						if (m == d && d == n) add(v, m, l, l, GRASS_PCT[k], EncounterKind::GRASS);
						else { add(v, m, l, l, GRASS_PCT[k], EncounterKind::GRASS, TimeOfDay::MORNING);
							add(v, d, l, l, GRASS_PCT[k], EncounterKind::GRASS, TimeOfDay::DAY);
							add(v, n, l, l, GRASS_PCT[k], EncounterKind::GRASS, TimeOfDay::NIGHT); }
					}
					// the radio's Hoenn and Sinnoh sounds: the first species in slots 2-3, the second in 4-5
					for (int s = 0; s < 2; s++)
						for (int k = 0; k < 2; k++)
						{
							uint8_t l = p.u8(8 + 2 + 2 * k);
							add(v, p.u16(0x5c + 4 * s + 2 * k), l, p.u8(8 + 3 + 2 * k), 20, s == 0 ? HOENN_SOUND : SINNOH_SOUND);
						}
					if (swarm == 0) add(v, p.u16(0xbc), p.u8(8), p.u8(9), 40, EncounterKind::SWARM);
				}
				static const uint8_t SURF[5] = { 60, 30, 5, 4, 1 }, ROD[5] = { 40, 30, 15, 10, 5 }, ROCK[2] = { 80, 20 };
				auto group = [&](size_t o, int n, size_t rate, const uint8_t* pct, EncounterKind k) {
					if (!p.u8(rate)) return;
					for (int s = 0; s < n; s++) add(v, p.u16(o + 4 * s + 2), p.u8(o + 4 * s), p.u8(o + 4 * s + 1), pct[s], k);
				};
				group(0x64, 5, 1, SURF, EncounterKind::SURF);
				group(0x78, 2, 2, ROCK, EncounterKind::ROCK_SMASH);
				group(0x80, 5, 3, ROD, EncounterKind::OLD_ROD);
				group(0x94, 5, 4, ROD, EncounterKind::GOOD_ROD);
				group(0xa8, 5, 5, ROD, EncounterKind::SUPER_ROD);
				if (p.u8(1) && swarm == 1) add(v, p.u16(0xbe), p.u8(0x64), p.u8(0x65), 60, EncounterKind::SWARM, TimeOfDay::ANY, 0xff, 0, 1);
				// at night the Good Rod's slot 3 and the Super Rod's slot 1 hold the night fish
				if (p.u8(4)) add(v, p.u16(0xc0), p.u8(0x94 + 12), p.u8(0x94 + 13), ROD[3], NIGHT_FISH, TimeOfDay::NIGHT);
				if (p.u8(5)) add(v, p.u16(0xc0), p.u8(0xa8 + 4), p.u8(0xa8 + 5), ROD[1], NIGHT_FISH, TimeOfDay::NIGHT);
				if (p.u8(5) && swarm == 2) add(v, p.u16(0xc2), p.u8(0xa8), p.u8(0xa9), 100, EncounterKind::SWARM, TimeOfDay::ANY, 0xff, 0, 2);
				return v;
			}

			// Black/White/Black 2/White 2: 232 bytes per season (one, or four: spring, summer, autumn, winter):
			// 7 rates, then grass, dark grass, shaking grass (12 each), surf, rippling surf, fishing, rippling fishing
			// (5 each) of {u16 species:11 | form:5, u8 min, u8 max}
			std::vector<Encounter> encGen5(Span p)
			{
				std::vector<Encounter> v;
				size_t seasons = p.n / 232;
				if (seasons != 1 && seasons != 4) return v;
				static const uint8_t SURF[5] = { 60, 30, 5, 4, 1 }, ROD[5] = { 40, 40, 15, 4, 1 };
				struct G { size_t at; int n; const uint8_t* pct; EncounterKind k; } groups[] = {
					{ 0x08, 12, GRASS_PCT, EncounterKind::GRASS }, { 0x38, 12, GRASS_PCT, DARK_GRASS }, { 0x68, 12, GRASS_PCT, SHAKING_GRASS },
					{ 0x98, 5, SURF, EncounterKind::SURF }, { 0xac, 5, SURF, RIPPLE_SURF }, { 0xc0, 5, ROD, EncounterKind::SUPER_ROD },
					{ 0xd4, 5, ROD, RIPPLE_FISH } };
				// seasons the same as spring are listed once, for every season
				auto same = [&](size_t a, size_t b) { return memcmp(p.p + a * 232, p.p + b * 232, 232) == 0; };
				bool allSame = seasons == 4 && same(0, 1) && same(0, 2) && same(0, 3);
				for (size_t s = 0; s < (allSame ? 1 : seasons); s++)
				{
					size_t b = s * 232;
					uint8_t season = seasons == 1 || allSame ? 0xff : (uint8_t)s;
					for (int gi = 0; gi < 7; gi++)
					{
						if (!p.u8(b + gi)) continue;
						const G& g = groups[gi];
						for (int k = 0; k < g.n; k++)
						{
							size_t o = b + g.at + 4 * k;
							uint16_t raw = p.u16(o);
							add(v, raw & 0x7ff, p.u8(o + 2), p.u8(o + 3), g.pct[k], g.k, TimeOfDay::ANY, season, (uint8_t)(raw >> 11));
						}
					}
				}
				return v;
			}

			// the zones: the map headers (Gen IV: a 24-byte table in the ARM9, found by its shape; Gen V: a/0/1/2)
			// give each zone its name and its encounter file
			bool zones(const Bytes& arm9)
			{
				Bytes eb; Narc en;
				if (!narc(f.enc, eb, en)) return false;
				const size_t names = out.locationNames.size(), encs = en.size();
				std::vector<std::pair<int, int>> zone;	// name index, encounter file (-1 none)
				if (gen() == Gen::IV)
				{
					Bytes mn;
					if (!rom.file("fielddata/maptable/mapname.bin", mn)) { err = "no map name table"; return false; }
					size_t count = mn.size() / 16;
					const bool hg = isHgss(id()), dp = isDp(id());
					// an entry: Diamond/Pearl/Platinum {..., u16 day music @10, night music @12, wild file @14, ...,
					// name @18 (u16 in D/P, u8 in Pt)}; HGSS {u8 wild file @0, ..., music @12/@14, name u8 @18}.
					// Music ids are the sound archive's sequences (1000 and up), which nothing before the table has.
					auto entry = [&](size_t b, int& name, int& wild) {
						if (hg) { wild = arm9[b] == 0xff ? -1 : arm9[b]; name = arm9[b + 18]; }
						else { uint16_t w = nitro::rd16(&arm9[b + 14]); wild = w == 0xffff ? -1 : w; name = dp ? nitro::rd16(&arm9[b + 18]) : arm9[b + 18]; }
						uint16_t m1 = nitro::rd16(&arm9[b + (hg ? 12 : 10)]), m2 = nitro::rd16(&arm9[b + (hg ? 14 : 12)]);
						return m1 >= 1000 && m1 < 2000 && m2 >= 1000 && m2 < 2000 && (size_t)name < names && wild < (int)encs;
					};
					size_t at = SIZE_MAX;
					for (size_t s = 0; count && s + 24 * count <= arm9.size() && at == SIZE_MAX; s += 4)
					{
						int nm, w;
						size_t m = 0;
						while (m < count && entry(s + 24 * m, nm, w)) m++;
						if (m == count) at = s;
					}
					if (at == SIZE_MAX) { err = "no map headers in the ARM9"; return false; }
					mapHeaders = at;
					for (size_t m = 0; m < count; m++) { int nm, w; entry(at + 24 * m, nm, w); zone.push_back({ nm, w }); }
				}
				else
				{
					Bytes zb; Narc zn;
					if (!narc("a/0/1/2", zb, zn) || zn.size() < 1) return false;
					Span z = zn[0];
					const bool b2 = isB2w2(id());
					for (size_t m = 0; m + 48 <= z.n; m += 48)
					{
						int w = b2 ? (z.u8(m + 20) == 0xff ? -1 : z.u8(m + 20)) : (z.u16(m + 20) == 0xffff ? -1 : z.u16(m + 20));
						int nm = z.u8(m + 26);
						zone.push_back({ (size_t)nm < names ? nm : 0, w < (int)encs ? w : -1 });
					}
				}
				for (auto& z : zone) zoneWild.push_back(z.second);
				// HGSS's swarm table: 20 {u16 map, u16 0 grass / 1 surfing / 2 fishing} in the ARM9 (pokeheartgold
				// unk_02097F6C.c), found by its shape: 20 different zones with wild Pokémon, all three kinds
				std::map<int, int> swarmAt;
				if (isHgss(id()))
					for (size_t s = 0; s + 80 <= arm9.size() && swarmAt.empty(); s += 2)
					{
						std::map<int, int> t;
						int kinds = 0;
						for (int k = 0; k < 20; k++)
						{
							uint16_t m = nitro::rd16(&arm9[s + 4 * k]), ty = nitro::rd16(&arm9[s + 4 * k + 2]);
							if (m == 0 || m >= zone.size() || zone[m].second < 0 || ty > 2 || t.count(m)) break;
							t[m] = ty;
							kinds |= 1 << ty;
						}
						if (t.size() == 20 && kinds == 7) swarmAt = t;
					}
				std::map<int, std::vector<Encounter>> decoded;
				for (size_t m = 0; m < zone.size(); m++)
				{
					Location& l = out.zones[(uint16_t)m];
					l.id = (uint16_t)m;
					l.name = out.locationNames[zone[m].first];
					int w = zone[m].second;
					if (w < 0) continue;
					int swarm = swarmAt.count((int)m) ? swarmAt[(int)m] : -1;
					int key = w * 4 + swarm + 1;
					auto it = decoded.find(key);
					if (it == decoded.end())
					{
						Span p = en[(size_t)w];
						it = decoded.emplace(key, gen() == Gen::V ? encGen5(p) : isHgss(id()) ? encHgss(p, swarm) : encDppt(p)).first;
					}
					l.encounters = it->second;
				}
				if (isHgss(id()))
				{
					Bytes hb; Narc hn;
					if (rom.file("a/2/5/2", hb) && hn.parse(hb))
						for (size_t m = 0; m < zone.size() && m < hn.size(); m++) headbutt(hn[m], out.zones[(uint16_t)m].encounters);
				}
				return true;
			}

			// ---- Black 2/White 2's Hidden Grottos: a/2/7/3 has one 220-byte record per grotto: six blocks of
			// {u16 species[4], u8 max level[4], u8 min level[4], u8 female %[4], u8 form[4], u16} (blocks 0-2 Black
			// 2's, 3-5 White 2's; the four columns the grotto's four groups), then 32 item numbers. Which grotto is
			// where is a table in an overlay: per grotto {u32 zone, u32 x, u32 height, u32 y} (found by its shape:
			// as many entries as grottos, every zone a real one with wild Pokémon, coordinates in a map's range).
			// The odds of each block are not in these files: GROTTO encounters carry percent 0 (unknown).
			bool grottos(const std::vector<int>& zoneWild)
			{
				Bytes gb; Narc gn;
				if (!rom.file("a/2/7/3", gb) || !gn.parse(gb) || gn.size() == 0) return true;	// (no grottos: nothing to add)
				const size_t count = gn.size(), Z = zoneWild.size();
				auto fits = [&](const Bytes& d, size_t at) {
					const uint8_t* e = &d[at];
					uint32_t z = nitro::rd32(e), x = nitro::rd32(e + 4), h = nitro::rd32(e + 8), y = nitro::rd32(e + 12);
					return z < Z && zoneWild[z] >= 0 && x < 4096 && h < 256 && y < 4096;
				};
				// the table ends where its run of such entries ends (a city's entry before it has the same shape)
				std::vector<uint16_t> where;
				Bytes ov;
				for (int i = 0; where.empty() && rom.overlay9(i, ov); i++)
					for (size_t at = 0; at + 16 * count <= ov.size(); at += 4)
					{
						size_t run = 0;
						while (at + 16 * (run + 1) <= ov.size() && fits(ov, at + 16 * run)) run++;
						if (run < count) continue;
						size_t from = at + 16 * (run - count);
						for (size_t k = 0; k < count; k++) where.push_back((uint16_t)nitro::rd32(&ov[from + 16 * k]));
						break;
					}
				if (where.empty()) return true;	// (a release laid out differently: no grottos rather than wrong ones)
				const size_t half = id() == GameId::WHITE2 ? 3 : 0;
				for (size_t g = 0; g < count; g++)
				{
					Span p = gn[g];
					if (p.n < 156) continue;
					auto& v = out.zones[where[g]].encounters;
					for (size_t b = half; b < half + 3; b++)
						for (int c = 0; c < 4; c++)
						{
							size_t o = b * 26;
							uint16_t sp = p.u16(o + 2 * c);
							if (sp == 0 || sp > out.game.dexSize) continue;
							bool dup = false;
							for (auto& e : v) dup = dup || (e.kind == EncounterKind::GROTTO && e.species == sp && e.form == p.u8(o + 20 + c));
							if (dup) continue;
							Encounter e;
							e.species = sp; e.maxLevel = p.u8(o + 8 + c); e.minLevel = p.u8(o + 12 + c); e.percent = 0;
							e.kind = EncounterKind::GROTTO; e.form = p.u8(o + 20 + c);
							if (e.minLevel > e.maxLevel) std::swap(e.minLevel, e.maxLevel);
							v.push_back(e);
						}
				}
				return true;
			}

			// ---- trainers: trdata {flags, class, battle type/sprite, count, items, AI, ...} + trpoke --------------
			bool trainers()
			{
				Bytes db, pb; Narc dn, pn;
				if (!narc(f.trdata, db, dn) || !narc(f.trpoke, pb, pn)) return false;
				auto names = bank(f.bTrNames), classes = bank(f.bTrClasses);
				const bool g5 = gen() == Gen::V, seal = !g5 && !isDp(id());
				for (size_t i = 1; i < dn.size() && i < pn.size(); i++)
				{
					Span d = dn[i], p = pn[i];
					if (d.n < 20) continue;
					uint8_t flags = d.u8(0), cls = d.u8(1), count = d.u8(3);
					bool moves = flags & 1, item = flags & 2;
					Trainer t;
					t.id = (uint16_t)i;
					t.cls = cls < classes.size() ? classes[cls] : "";
					t.name = i < names.size() ? names[i] : "";
					t.doubleBattle = g5 ? d.u8(2) != 0 : d.u32(16) != 0;
					size_t size = (g5 ? 8 : 6) + (item ? 2 : 0) + (moves ? 8 : 0) + (seal ? 2 : 0);
					for (size_t k = 0; k < count && (k + 1) * size <= p.n; k++)
					{
						size_t o = k * size;
						TrainerMon m;
						if (g5) { m.level = (uint8_t)p.u16(o + 2); m.species = p.u16(o + 4); }
						else { m.level = (uint8_t)p.u16(o + 2); m.species = p.u16(o + 4) & 0x3ff; }
						size_t q = o + (g5 ? 8 : 6);
						if (item) { m.item = p.u16(q); q += 2; }
						if (moves) for (int j = 0; j < 4; j++) m.moves[j] = p.u16(q + 2 * j);
						if (!moves && m.species < out.species.size()) defaultMoves(m);
						t.team.push_back(m);
					}
					out.trainers[t.id] = std::move(t);
				}
				return true;
			}

			// the moves a Pokémon without its own list knows: the last four it learns by levelling up to its level
			void defaultMoves(TrainerMon& m) const
			{
				std::vector<uint16_t> known;
				for (auto& lm : out.species[m.species].learnset)
				{
					if (lm.level > m.level) break;
					if (std::find(known.begin(), known.end(), lm.move) != known.end()) continue;
					known.push_back(lm.move);
				}
				size_t from = known.size() > 4 ? known.size() - 4 : 0;
				for (size_t j = from; j < known.size(); j++) m.moves[j - from] = known[j];
			}
		};
	}

	// ---- sprites ------------------------------------------------------------------------------------------------------
	namespace
	{
		// an NCLR's first 16 colours (BGR555) as 0xRRGGBB
		bool palette(Span f, std::array<uint32_t, 16>& out)
		{
			if (f.n < 0x28 || memcmp(f.p, "RLCN", 4) != 0 || memcmp(f.p + 16, "TTLP", 4) != 0) return false;
			size_t at = 16 + 8 + f.u32(16 + 20), len = f.u32(16 + 16);
			for (size_t k = 0; k < 16 && at + 2 * k + 2 <= f.n && 2 * k < len; k++)
			{
				uint16_t c = f.u16(at + 2 * k);
				uint32_t r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
				out[k] = (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2);
			}
			return true;
		}

		// an NCGR's pixel data (4 bits a pixel) and its size in tiles; tiled: 8x8 tiles, else rows of pixels
		struct Chars { Span data; int wTiles = 0, hTiles = 0; bool tiled = true; };
		bool chars(Span f, Chars& c)
		{
			if (f.n < 0x30 || memcmp(f.p, "RGCN", 4) != 0 || memcmp(f.p + 16, "RAHC", 4) != 0) return false;
			if (f.u32(16 + 12) != 3) return false;	// 4 bits a pixel
			c.hTiles = f.u16(16 + 8); c.wTiles = f.u16(16 + 10);
			c.tiled = f.u32(16 + 20) == 0;
			size_t len = f.u32(16 + 24), at = 16 + 8 + f.u32(16 + 28);
			if (at > f.n || len > f.n - at) return false;
			c.data = { f.p + at, len };
			return true;
		}


		// ---- Gen V's animation: cells of the parts sheet (NCER), each cell's animation (NANR: which cell, and
		// rotation/scale/translation, per 1/60 s), multi-cells placing those animations (NMCR), and the multi-cell
		// animation picking a multi-cell over time (NMAR). Nitro G2D's formats (as NNS writes them).
		struct G2dSeq { uint16_t loopStart = 0; uint32_t mode = 0; struct F { uint16_t ticks; uint16_t cell; uint16_t rot; int32_t sx, sy; int16_t tx, ty; }; std::vector<F> frames; uint32_t total = 0; };

		// NANR/NMAR ("KNBA"): sequences {u16 frames, u16 loop start, u16 element type, u16, u32 play mode, u32 frame
		// offset}; frames {u32 element offset, u16 ticks, u16}; elements: 0 index, 1 index+SRT, 2 index+translation
		bool g2dAnims(Span f, std::vector<G2dSeq>& out)
		{
			if (f.n < 0x30 || memcmp(f.p + 16, "KNBA", 4) != 0) return false;
			size_t c = 16 + 8, count = f.u16(16 + 8);
			size_t seqs = c + f.u32(16 + 12), frames = c + f.u32(16 + 16), data = c + f.u32(16 + 20);
			if (count > 256) return false;
			for (size_t i = 0; i < count; i++)
			{
				size_t e = seqs + 16 * i;
				if (e + 16 > f.n) return false;
				G2dSeq q;
				uint16_t n = f.u16(e), type = f.u16(e + 4);
				q.loopStart = f.u16(e + 2); q.mode = f.u32(e + 8);
				size_t fo = frames + f.u32(e + 12);
				if (n > 1024) return false;
				for (size_t k = 0; k < n; k++)
				{
					size_t fr = fo + 8 * k;
					if (fr + 8 > f.n) return false;
					size_t el = data + f.u32(fr);
					G2dSeq::F x{ f.u16(fr + 4), f.u16(el), 0, 4096, 4096, 0, 0 };
					if (type == 1) { if (el + 16 > f.n) return false; x.rot = f.u16(el + 2); x.sx = (int32_t)f.u32(el + 4); x.sy = (int32_t)f.u32(el + 8); x.tx = (int16_t)f.u16(el + 12); x.ty = (int16_t)f.u16(el + 14); }
					else if (type == 2) { if (el + 8 > f.n) return false; x.tx = (int16_t)f.u16(el + 4); x.ty = (int16_t)f.u16(el + 6); }
					else if (el + 2 > f.n) return false;
					q.total += x.ticks;
					q.frames.push_back(x);
				}
				out.push_back(std::move(q));
			}
			return true;
		}

		// the frame of a sequence at a tick: play modes 1 once (then its last frame), others loop from the loop start
		const G2dSeq::F* g2dAt(const G2dSeq& q, uint32_t t)
		{
			if (q.frames.empty()) return nullptr;
			if (q.total == 0) return &q.frames[0];
			if (t >= q.total)
			{
				if (q.mode == 1) return &q.frames.back();
				uint32_t head = 0;
				for (size_t k = 0; k < q.loopStart && k < q.frames.size(); k++) head += q.frames[k].ticks;
				t = q.total > head ? head + (t - head) % (q.total - head) : q.total - 1;
			}
			for (auto& fr : q.frames) { if (t < fr.ticks) return &fr; t -= fr.ticks; }
			return &q.frames.back();
		}

		struct Oam { int x, y, w, h, tile; bool hflip, vflip; };
		struct G2d
		{
			std::vector<std::vector<Oam>> cells;
			std::vector<G2dSeq> cellAnims, multiAnims;
			struct Node { uint16_t anim; int16_t x, y; };
			std::vector<std::vector<Node>> multi;
			Chars sheet;
		};

		bool g2dCells(Span f, std::vector<std::vector<Oam>>& cells)
		{
			// NCER ("KBEC"): u16 cells, u16 bounds flag (entries of 16 bytes instead of 8), u32 table offset;
			// entries {u16 OAM count, u16 attributes, u32 OAM offset (after the table)}; OAMs are the DS's 3 words
			if (f.n < 0x30 || memcmp(f.p + 16, "KBEC", 4) != 0) return false;
			size_t count = f.u16(16 + 8), each = f.u16(16 + 10) == 1 ? 16 : 8, table = 16 + 8 + f.u32(16 + 12), oams = table + count * each;
			static const int SIZE[3][4][2] = { { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } }, { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
				{ { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } } };
			for (size_t i = 0; i < count; i++)
			{
				size_t e = table + i * each;
				if (e + 8 > f.n) return false;
				size_t n = f.u16(e), at = oams + f.u32(e + 4);
				std::vector<Oam> cell;
				for (size_t k = 0; k < n; k++)
				{
					size_t o = at + 6 * k;
					if (o + 6 > f.n) return false;
					uint16_t a0 = f.u16(o), a1 = f.u16(o + 2), a2 = f.u16(o + 4);
					int shape = a0 >> 14, size = a1 >> 14;
					if (shape > 2) continue;
					Oam m;
					m.w = SIZE[shape][size][0]; m.h = SIZE[shape][size][1];
					m.y = (int8_t)(a0 & 0xff); m.x = (a1 & 0x1ff) >= 256 ? (int)(a1 & 0x1ff) - 512 : (a1 & 0x1ff);
					m.tile = a2 & 0x3ff;
					bool affine = a0 & 0x100;
					if (affine && (a0 & 0x200)) { m.x += m.w / 2; m.y += m.h / 2; }	// (double-size: placed by its doubled box)
					m.hflip = !affine && (a1 & 0x1000); m.vflip = !affine && (a1 & 0x2000);
					cell.push_back(m);
				}
				cells.push_back(std::move(cell));
			}
			return true;
		}

		// one 60 Hz tick of the multi-cell animation, drawn on a canvas (cw x cw, origin at its centre)
		void g2dDraw(const G2d& g, uint32_t tick, std::vector<uint8_t>& canvas, int cw)
		{
			std::fill(canvas.begin(), canvas.end(), 0);
			const G2dSeq::F* mf = g2dAt(g.multiAnims[0], tick);
			if (!mf || mf->cell >= g.multi.size()) return;
			// (time inside the current multi-cell: its cell animations start with it)
			uint32_t t = tick % std::max<uint32_t>(1, g.multiAnims[0].total), start = 0;
			for (auto& fr : g.multiAnims[0].frames) { if (&fr == mf) break; start += fr.ticks; }
			t = t >= start ? t - start : 0;
			const int sw = g.sheet.wTiles * 8, sh = g.sheet.hTiles * 8;
			auto sheetPx = [&](int x, int y) -> uint8_t {
				if (x < 0 || y < 0 || x >= sw || y >= sh) return 0;
				size_t i = g.sheet.tiled ? ((size_t)(y / 8 * g.sheet.wTiles + x / 8) * 32 + (y % 8) * 4 + (x % 8) / 2) : (size_t)(y * sw + x) / 2;
				return i < g.sheet.data.n ? (uint8_t)(g.sheet.data.p[i] >> (4 * (x & 1)) & 15) : 0;
			};
			const auto& nodes = g.multi[mf->cell];
			// the first node and the first piece of a cell are on top (as the DS's OAM order draws them): draw backwards
			for (size_t ni = nodes.size(); ni-- > 0;)
			{
				const auto& node = nodes[ni];
				if (node.anim >= g.cellAnims.size()) continue;
				const G2dSeq::F* e = g2dAt(g.cellAnims[node.anim], t);
				if (!e || e->cell >= g.cells.size()) continue;
				double ang = e->rot * (2 * 3.14159265358979 / 65536), ca = cos(ang), sa = sin(ang);
				double kx = e->sx ? e->sx / 4096.0 : 1, ky = e->sy ? e->sy / 4096.0 : 1;
				double ox = cw / 2 + node.x + e->tx, oy = cw / 2 + node.y + e->ty;
				const auto& cell = g.cells[e->cell];
				for (size_t k = cell.size(); k-- > 0;)
				{
					const Oam& m = cell[k];
					// the piece's corners on the canvas, then every canvas pixel in that box mapped back into the piece
					double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
					for (int c = 0; c < 4; c++)
					{
						double px = (m.x + (c & 1 ? m.w : 0)) * kx, py = (m.y + (c & 2 ? m.h : 0)) * ky;
						double X = ox + px * ca - py * sa, Y = oy + px * sa + py * ca;
						minX = std::min(minX, X); maxX = std::max(maxX, X); minY = std::min(minY, Y); maxY = std::max(maxY, Y);
					}
					int x0 = std::max(0, (int)floor(minX)), x1 = std::min(cw - 1, (int)ceil(maxX)), y0 = std::max(0, (int)floor(minY)), y1 = std::min(cw - 1, (int)ceil(maxY));
					for (int y = y0; y <= y1; y++)
						for (int x = x0; x <= x1; x++)
						{
							double dx = x + 0.5 - ox, dy = y + 0.5 - oy;
							double cx = (dx * ca + dy * sa) / kx, cy = (-dx * sa + dy * ca) / ky;
							int lx = (int)floor(cx) - m.x, ly = (int)floor(cy) - m.y;
							if (lx < 0 || ly < 0 || lx >= m.w || ly >= m.h) continue;
							if (m.hflip) lx = m.w - 1 - lx;
							if (m.vflip) ly = m.h - 1 - ly;
							uint8_t v = sheetPx(m.tile % 32 * 8 + lx, m.tile / 32 * 8 + ly);
							if (v) canvas[(size_t)(y * cw + x)] = v;
						}
				}
			}
		}

		bool box(const std::vector<uint8_t>& px, int w, int& x0, int& y0, int& x1, int& y1)
		{
			x0 = y0 = 1 << 30; x1 = y1 = -1;
			for (size_t i = 0; i < px.size(); i++)
				if (px[i]) { int x = (int)(i % w), y = (int)(i / w); x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
			return x1 >= 0;
		}

		// the animation as frames the size of the still, at 20 per second, consecutive repeats merged; aligned so
		// its first frame sits where the still does. false: no animation (the still stays alone).
		bool gen5Anim(const nitro::NarcFile& n, size_t at, bool female, Sprite& s)
		{
			Bytes sheetB, cellB, animB, multiB, multiAnimB;
			auto get = [&](size_t k, Bytes& b) { Span f = n[at + k]; if (!f.n) return false; if (f.p[0] == 0x10 || f.p[0] == 0x11) return nitro::lzDecompress(f.p, f.n, b); b.assign(f.p, f.p + f.n); return true; };
			if (!(female && get(3, sheetB)) && !get(2, sheetB)) return false;
			if (!get(4, cellB) || !get(5, animB) || !get(6, multiB) || !get(7, multiAnimB)) return false;
			G2d g;
			if (!chars({ sheetB.data(), sheetB.size() }, g.sheet) || !g2dCells({ cellB.data(), cellB.size() }, g.cells)) return false;
			if (!g2dAnims({ animB.data(), animB.size() }, g.cellAnims) || !g2dAnims({ multiAnimB.data(), multiAnimB.size() }, g.multiAnims) || g.multiAnims.empty())
				return false;
			// NMCR ("KBCM"): u16 multi-cells, u32 table offset, u32 node offset; entries {u16 nodes, u16, u32 offset};
			// nodes {u16 cell animation, s16 x, s16 y, u16 flags}
			Span m{ multiB.data(), multiB.size() };
			if (m.n < 0x2c || memcmp(m.p + 16, "KBCM", 4) != 0) return false;
			size_t mc = m.u16(16 + 8), table = 24 + m.u32(16 + 12), nodes = 24 + m.u32(16 + 16);
			for (size_t i = 0; i < mc; i++)
			{
				size_t e = table + 8 * i;
				if (e + 8 > m.n) return false;
				std::vector<G2d::Node> v;
				for (size_t k = 0; k < m.u16(e); k++)
				{
					size_t o = nodes + m.u32(e + 4) + 8 * k;
					if (o + 8 > m.n) return false;
					v.push_back({ m.u16(o), (int16_t)m.u16(o + 2), (int16_t)m.u16(o + 4) });
				}
				g.multi.push_back(std::move(v));
			}
			const uint32_t total = std::min<uint32_t>(g.multiAnims[0].total, 600), step = 3;	// (10 s at most)
			if (total < step) return false;
			const int cw = 256;
			std::vector<uint8_t> canvas((size_t)cw * cw);
			g2dDraw(g, 0, canvas, cw);
			int cx0, cy0, cx1, cy1, sx0, sy0, sx1, sy1;
			if (!box(canvas, cw, cx0, cy0, cx1, cy1) || !box(s.frames[0], s.w, sx0, sy0, sx1, sy1)) return false;
			// (centre on centre: the first frame and the still are the same pose)
			int dx = (sx0 + sx1 - cx0 - cx1) / 2, dy = (sy0 + sy1 - cy0 - cy1) / 2;
			std::vector<std::vector<uint8_t>> frames;
			std::vector<Sprite::Step> steps;
			for (uint32_t t = 0; t < total; t += step)
			{
				if (t) g2dDraw(g, t, canvas, cw);
				std::vector<uint8_t> f((size_t)(s.w * s.h), 0);
				for (int y = 0; y < s.h; y++)
					for (int x = 0; x < s.w; x++)
					{
						int X = x - dx, Y = y - dy;
						if (X >= 0 && Y >= 0 && X < cw && Y < cw) f[(size_t)(y * s.w + x)] = canvas[(size_t)(Y * cw + X)];
					}
				uint16_t ms = (uint16_t)(std::min(step, total - t) * 1000 / 60);
				if (!frames.empty() && frames.back() == f) { steps.back().ms = (uint16_t)(steps.back().ms + ms); continue; }
				frames.push_back(std::move(f));
				steps.push_back({ (uint16_t)frames.size(), ms });	// (frames[0] is the still: animation frames follow it)
			}
			if (frames.size() < 2) return false;
			for (auto& f : frames) s.frames.push_back(std::move(f));
			s.anim = std::move(steps);
			return true;
		}

	}

	std::shared_ptr<const Sprite> RomImpl::sprite(uint16_t species, bool female, uint8_t form) const
	{
		if (species == 0 || species > game.dexSize) return nullptr;
		std::lock_guard<std::mutex> g(lock);
		uint32_t key = (uint32_t)species << 9 | (uint32_t)form << 1 | (female ? 1 : 0);
		auto hit = sprites.find(key);
		if (hit != sprites.end()) return hit->second;
		if (!rom)
		{
			rom.reset(new nitro::Rom);
			if (!rom->open(path)) { rom.reset(); return nullptr; }
		}
		const Files* f = filesFor(game.id);
		// (the archive's member table kept; only this picture's members read, and let go of after it)
		if (!f || (!spriteNarc.isOpen() && !spriteNarc.open(*rom, f->sprites))) return nullptr;
		nitro::NarcFile& n = spriteNarc;
		struct Forget { nitro::NarcFile& n; ~Forget() { n.forget(); } } forget{ n };
		auto s = std::make_shared<Sprite>();
		if (game.gen == Gen::IV)
		{
			// per species 6 members: back (female, male), front (female, male), normal and shiny palette. The front
			// is 160x80, two 80x80 frames side by side, scrambled with the LC RNG (pret pokemon_sprite.c: Diamond and
			// Pearl from the last word down, Platinum and HGSS from the first up)
			size_t at = (size_t)species * 6;
			// (a female-only species - Nidoran F, Chansey, Jynx, Happiny... - has only the female front: the male
			// member is empty, so that one is used whatever was asked)
			Span front = (female && n[at + 2].n) || !n[at + 3].n ? n[at + 2] : n[at + 3], pal = n[at + 4], shinyPal = n[at + 5];
			// forms: their own archive, laid out by the games' code (pret pokeplatinum/pokeheartgold/pokediamond
			// pokemon.c, the sprite template switch): per species the front picture of form 0 and how far each form
			// moves it, the palette and how far each form moves it, and the shiny palette's distance
			Bytes ob;
			Narc on;
			if (form > 0)
			{
				struct OF { uint16_t species; uint8_t forms; uint16_t chr; uint8_t chrStep; uint16_t palDp, palPt; uint8_t palStep, shinyStep; };
				// (HeartGold/SoulSilver: Platinum's palettes moved up 4 by Pichu's pictures)
				static const OF T[] = { { 386, 4, 0x01, 2, 134, 154, 0, 1 }, { 201, 28, 0x09, 2, 136, 156, 0, 1 }, { 351, 4, 0x44, 1, 138, 158, 1, 4 },
					{ 412, 3, 0x49, 2, 146, 166, 2, 1 }, { 413, 3, 0x4f, 2, 152, 172, 2, 1 }, { 422, 2, 0x56, 1, 158, 178, 2, 1 },
					{ 423, 2, 0x5a, 1, 162, 182, 2, 1 }, { 421, 2, 0x5e, 1, 166, 186, 1, 2 }, { 493, 18, 0x61, 2, 170, 190, 2, 1 },
					{ 492, 2, 0x87, 2, 0, 228, 2, 1 }, { 479, 6, 0x8b, 2, 0, 232, 2, 1 }, { 487, 2, 0x97, 2, 0, 244, 2, 1 }, { 172, 2, 0x9b, 2, 0, 0, 2, 1 } };
				const bool dp = isDp(game.id), hg = isHgss(game.id);
				const char* other = dp ? "poketool/pokegra/otherpoke.narc" : hg ? "a/1/1/4" : "poketool/pokegra/pl_otherpoke.narc";
				for (auto& t : T)
				{
					if (t.species != species || form >= t.forms) continue;
					uint16_t palBase = dp ? t.palDp : hg ? (t.species == 172 ? 252 : (uint16_t)(t.palPt + 4)) : t.palPt;
					if (palBase == 0 || !rom->file(other, ob) || !on.parse(ob)) break;
					front = on[t.chr + (size_t)t.chrStep * form];
					size_t pn = palBase + (size_t)t.palStep * form;
					pal = on[pn]; shinyPal = on[pn + t.shinyStep];
					break;
				}
			}
			Chars c;
			if (!chars(front, c) || c.data.n < 6400 || !palette(pal, s->normal) || !palette(shinyPal, s->shiny)) return nullptr;
			std::vector<uint16_t> w(3200);
			for (size_t k = 0; k < w.size(); k++) w[k] = c.data.u16(2 * k);
			const bool fromEnd = isDp(game.id);
			uint32_t seed = fromEnd ? w.back() : w.front();
			for (size_t k = 0; k < w.size(); k++)
			{
				size_t i = fromEnd ? w.size() - 1 - k : k;
				w[i] ^= (uint16_t)seed;
				seed = seed * 1103515245u + 24691u;
			}
			s->w = s->h = 80;
			s->frames.assign(2, std::vector<uint8_t>(80 * 80));
			for (int y = 0; y < 80; y++)
				for (int x = 0; x < 160; x++)
				{
					uint8_t b = (uint8_t)(w[(y * 160 + x) / 4] >> (4 * (x & 3)));
					s->frames[x / 80][y * 80 + x % 80] = b & 15;
				}
			// the two frames as the games show them on the summary: the second briefly, then back
			s->anim = { { 0, 1000 }, { 1, 250 }, { 0, 250 }, { 1, 250 } };
		}
		else
		{
			// per species 20 members: 0/1 the still front (male, female), 2/3 the parts sheet the animation is made
			// of, 4-8 its cells and animations, 9-17 the same for the back, 18/19 normal and shiny palette.
			// Members are LZ-packed (0x11).
			size_t at = (size_t)species * 20;
			auto fs = formSprites.find(species);
			if (form > 0 && fs != formSprites.end() && form < fs->second.second)
			{
				// the forms' pictures follow the species' and a few others (eggs, ...): where they start is where the
				// species owning the first ones (Unown) has its run of pictures with its own palette
				if (formOrigin < 0)
				{
					formOrigin = 0;
					for (auto& f0 : formSprites)
					{
						if (f0.second.first != 0) continue;
						Span pal = n[(size_t)f0.first * 20 + 18];
						size_t run = f0.second.second - 1u, entries = n.size() / 20;
						for (size_t i = (size_t)game.dexSize + 1; formOrigin == 0 && i + run <= entries; i++)
						{
							bool same = true;
							for (size_t k = 0; k < run && same; k++) { Span q = n[(i + k) * 20 + 18]; same = q.n == pal.n && memcmp(q.p, pal.p, pal.n) == 0; }
							if (same) formOrigin = (int)i;
						}
					}
				}
				if (formOrigin > 0) at = ((size_t)formOrigin + fs->second.first + form - 1) * 20;
			}
			if (at + 20 > n.size()) return nullptr;
			Span still = female && n[at + 1].n ? n[at + 1] : n[at];
			Bytes img;
			Chars c;
			if (!nitro::lzDecompress(still.p, still.n, img) || !chars({ img.data(), img.size() }, c) || !palette(n[at + 18], s->normal) || !palette(n[at + 19], s->shiny))
				return nullptr;
			// 96x96 stored as the DS's sprite pieces would draw it: a 64x64 block, the 32x64 right of it, the 64x32
			// below, the 32x32 corner, each 8x8 tiles row by row
			s->w = s->h = 96;
			if (c.data.n < 96 * 96 / 2) return nullptr;
			s->frames.assign(1, std::vector<uint8_t>(96 * 96));
			static const int BLOCKS[4][4] = { { 0, 0, 8, 8 }, { 8, 0, 4, 8 }, { 0, 8, 8, 4 }, { 8, 8, 4, 4 } };
			size_t t = 0;
			for (auto& b : BLOCKS)
				for (int ty = 0; ty < b[3]; ty++)
					for (int tx = 0; tx < b[2]; tx++, t++)
						for (int y = 0; y < 8; y++)
							for (int x = 0; x < 8; x++)
								s->frames[0][(size_t)(((b[1] + ty) * 8 + y) * 96 + (b[0] + tx) * 8 + x)] = (uint8_t)(c.data.p[t * 32 + y * 4 + x / 2] >> (4 * (x & 1)) & 15);
			gen5Anim(n, at, female, *s);	// (none or broken: the still alone)
		}
		if (sprites.size() >= 4) sprites.clear();	// (an animated Gen V sprite is up to a few MB)
		sprites[key] = s;
		return s;
	}

	// ---- the cache on the card ---------------------------------------------------------------------------------------
	namespace
	{
		struct W
		{
			Bytes b;
			void u8(uint8_t v) { b.push_back(v); }
			void u16(uint16_t v) { u8((uint8_t)v); u8((uint8_t)(v >> 8)); }
			void u32(uint32_t v) { u16((uint16_t)v); u16((uint16_t)(v >> 16)); }
			void str(const std::string& s) { u32((uint32_t)s.size()); b.insert(b.end(), s.begin(), s.end()); }
			void strs(const std::vector<std::string>& v) { u32((uint32_t)v.size()); for (auto& s : v) str(s); }
		};
		struct R
		{
			const uint8_t* p; size_t n, at = 0; bool ok = true;
			bool need(size_t k) { if (!ok || k > n - at) ok = false; return ok; }
			uint8_t u8() { return need(1) ? p[at++] : 0; }
			uint16_t u16() { uint16_t v = u8(); return (uint16_t)(v | u8() << 8); }
			uint32_t u32() { uint32_t v = u16(); return v | (uint32_t)u16() << 16; }
			std::string str() { uint32_t k = u32(); if (!need(k)) return ""; std::string s((const char*)p + at, k); at += k; return s; }
			size_t count(size_t each) { uint32_t k = u32(); if (!ok || (each && k > (n - at) / each)) { ok = false; return 0; } return k; }
			std::vector<std::string> strs() { std::vector<std::string> v(count(4)); for (auto& s : v) s = str(); return v; }
		};
		const char MAGIC[8] = { 'B', 'N', 'D', 'S', 'R', 'O', 'M', 0 };

		Bytes save(const RomImpl& r)
		{
			W w;
			w.b.insert(w.b.end(), MAGIC, MAGIC + 8);
			w.u32(ROM_CACHE_VERSION);
			w.str(r.game.romHash);
			w.u32((uint32_t)r.species.size());
			for (auto& s : r.species)
			{
				w.str(s.name); w.str(s.category); w.str(s.dexText);
				w.u8(s.types[0]); w.u8(s.types[1]);
				for (uint8_t v : s.base) w.u8(v);
				for (uint16_t v : s.abilities) w.u16(v);
				w.u8(s.catchRate); w.u8(s.growth); w.u8(s.genderRatio); w.u8(s.baseFriendship);
				for (uint8_t v : s.evYield) w.u8(v);
				w.u16(s.baseExp); w.u16(s.height); w.u16(s.weight); w.u16(s.formsAt); w.u8(s.formCount);
				w.u32((uint32_t)s.evolutions.size());
				for (auto& e : s.evolutions) { w.u16(e.method); w.u16(e.param); w.u16(e.into); w.str(e.how); }
				w.u32((uint32_t)s.learnset.size());
				for (auto& m : s.learnset) { w.u8(m.level); w.u16(m.move); }
				w.u32((uint32_t)s.tms.size());
				for (uint16_t t : s.tms) w.u16(t);
			}
			w.u32((uint32_t)r.moves.size());
			for (auto& m : r.moves)
			{
				w.str(m.name); w.str(m.description);
				w.u8(m.type); w.u8((uint8_t)m.cat); w.u8(m.power); w.u8(m.accuracy); w.u8(m.pp); w.u8(m.priority);
				w.u16(m.effect); w.u8(m.effectChance);
			}
			w.strs(r.abilities); w.strs(r.natures); w.strs(r.abilityText);
			w.u32((uint32_t)r.items.size());
			for (auto& i : r.items) { w.str(i.name); w.str(i.description); w.u8(i.pocket); w.u16(i.price); }
			w.strs(r.locationNames);
			w.u32((uint32_t)r.zones.size());
			for (auto& z : r.zones)
			{
				w.u16(z.first); w.str(z.second.name);
				w.u32((uint32_t)z.second.encounters.size());
				for (auto& e : z.second.encounters)
				{
					w.u16(e.species); w.u8(e.minLevel); w.u8(e.maxLevel); w.u8(e.percent); w.u8((uint8_t)e.kind);
					w.u8((uint8_t)e.time); w.u8(e.season); w.u8(e.form); w.u8(e.condition);
				}
			}
			w.u32((uint32_t)r.trainers.size());
			for (auto& t : r.trainers)
			{
				w.u16(t.first); w.str(t.second.cls); w.str(t.second.name); w.u8(t.second.doubleBattle);
				w.u32((uint32_t)t.second.team.size());
				for (auto& m : t.second.team) { w.u16(m.species); w.u16(m.item); w.u8(m.level); for (uint16_t mv : m.moves) w.u16(mv); }
			}
			w.u32((uint32_t)r.tmMoves.size());
			for (uint16_t m : r.tmMoves) w.u16(m);
			w.u32((uint32_t)r.formSprites.size());
			for (auto& f : r.formSprites) { w.u16(f.first); w.u16(f.second.first); w.u8(f.second.second); }
			townmap::save(r, w.b);
			return w.b;
		}

		bool load(RomImpl& r, const Bytes& b)
		{
			R in{ b.data(), b.size() };
			if (b.size() < 12 || memcmp(b.data(), MAGIC, 8) != 0) return false;
			in.at = 8;
			if (in.u32() != ROM_CACHE_VERSION || in.str() != r.game.romHash) return false;
			r.species.resize(in.count(43));
			for (auto& s : r.species)
			{
				s.name = in.str(); s.category = in.str(); s.dexText = in.str();
				s.types[0] = (Type)in.u8(); s.types[1] = (Type)in.u8();
				for (uint8_t& v : s.base) v = in.u8();
				for (uint16_t& v : s.abilities) v = in.u16();
				s.catchRate = in.u8(); s.growth = in.u8(); s.genderRatio = in.u8(); s.baseFriendship = in.u8();
				for (uint8_t& v : s.evYield) v = in.u8();
				s.baseExp = in.u16(); s.height = in.u16(); s.weight = in.u16(); s.formsAt = in.u16(); s.formCount = in.u8();
				s.evolutions.resize(in.count(10));
				for (auto& e : s.evolutions) { e.method = in.u16(); e.param = in.u16(); e.into = in.u16(); e.how = in.str(); }
				s.learnset.resize(in.count(3));
				for (auto& m : s.learnset) { m.level = in.u8(); m.move = in.u16(); }
				s.tms.resize(in.count(2));
				for (uint16_t& t : s.tms) t = in.u16();
			}
			r.moves.resize(in.count(17));
			for (auto& m : r.moves)
			{
				m.name = in.str(); m.description = in.str();
				m.type = (Type)in.u8(); m.cat = (MoveCategory)in.u8(); m.power = in.u8(); m.accuracy = in.u8(); m.pp = in.u8();
				m.priority = in.u8(); m.effect = in.u16(); m.effectChance = in.u8();
			}
			r.abilities = in.strs(); r.natures = in.strs(); r.abilityText = in.strs();
			r.items.resize(in.count(11));
			for (auto& i : r.items) { i.name = in.str(); i.description = in.str(); i.pocket = in.u8(); i.price = in.u16(); }
			r.locationNames = in.strs();
			size_t zones = in.count(10);
			for (size_t k = 0; k < zones && in.ok; k++)
			{
				uint16_t id = in.u16();
				Location& l = r.zones[id];
				l.id = id; l.name = in.str();
				l.encounters.resize(in.count(11));
				for (auto& e : l.encounters)
				{
					e.species = in.u16(); e.minLevel = in.u8(); e.maxLevel = in.u8(); e.percent = in.u8(); e.kind = (EncounterKind)in.u8();
					e.time = (TimeOfDay)in.u8(); e.season = in.u8(); e.form = in.u8(); e.condition = in.u8();
				}
			}
			size_t trainers = in.count(15);
			for (size_t k = 0; k < trainers && in.ok; k++)
			{
				uint16_t id = in.u16();
				Trainer& t = r.trainers[id];
				t.id = id; t.cls = in.str(); t.name = in.str(); t.doubleBattle = in.u8() != 0;
				t.team.resize(in.count(13));
				for (auto& m : t.team) { m.species = in.u16(); m.item = in.u16(); m.level = in.u8(); for (uint16_t& mv : m.moves) mv = in.u16(); }
			}
			r.tmMoves.resize(in.count(2));
			for (uint16_t& m : r.tmMoves) m = in.u16();
			size_t fsprites = in.count(5);
			for (size_t k = 0; k < fsprites && in.ok; k++) { uint16_t s = in.u16(); uint16_t a = in.u16(); r.formSprites[s] = { a, in.u8() }; }
			if (!in.ok || !townmap::load(r, in.p, in.n, in.at)) return false;
			return in.at == in.n;
		}

		bool readFile(const std::string& p, Bytes& out)
		{
			FILE* f = fopen(p.c_str(), "rb");
			if (!f) return false;
			out.clear();
			uint8_t buf[65536];
			size_t k;
			while ((k = fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + k);
			fclose(f);
			return true;
		}

		bool writeFile(const std::string& p, const Bytes& b)
		{
			std::string tmp = p + ".tmp";
			FILE* f = fopen(tmp.c_str(), "wb");
			if (!f) return false;
			bool ok = fwrite(b.data(), 1, b.size(), f) == b.size();
			ok = fflush(f) == 0 && ok;
			ok = fsync(fileno(f)) == 0 && ok;
			fclose(f);
			if (!ok || rename(tmp.c_str(), p.c_str()) != 0) { unlink(tmp.c_str()); return false; }
			return true;
		}
	}

	RamHints ramHints(const RomData& d)
	{
		RamHints h;
		const RomImpl* r = dynamic_cast<const RomImpl*>(&d);
		if (!r) return h;
		uint32_t battleId, fieldId;
		switch (d.game.id)
		{
		case GameId::DIAMOND: case GameId::PEARL: battleId = 11; fieldId = 6; break;
		case GameId::PLATINUM: battleId = 16; fieldId = 7; break;
		case GameId::HEARTGOLD: case GameId::SOULSILVER: battleId = 12; fieldId = 2; break;
		case GameId::BLACK: case GameId::WHITE: battleId = 93; fieldId = 21; break;
		case GameId::BLACK2: case GameId::WHITE2: battleId = 167; fieldId = 36; break;
		default: return h;
		}
		std::lock_guard<std::mutex> g(r->lock);
		if (!r->rom)
		{
			r->rom.reset(new nitro::Rom);
			if (!r->rom->open(r->path)) { r->rom.reset(); return h; }
		}
		const auto table = r->rom->overlays9();
		int bi = -1, fi = -1;
		for (size_t i = 0; i < table.size(); i++)
		{
			if (table[i].id == battleId) bi = (int)i;
			if (table[i].id == fieldId) fi = (int)i;
		}
		Bytes bo, fo;
		if (bi < 0 || fi < 0 || !r->rom->overlay9(bi, bo) || !r->rom->overlay9(fi, fo)) return h;
		const nitro::Rom::Overlay& b = table[bi], & f = table[fi];
		// where both are loaded (their code and data), the first 32 bytes of the battle's that differ from the field's in
		// at least a quarter of them and aren't one byte repeated
		const uint32_t lo = std::max(b.ram, f.ram), hi = std::min(b.ram + (uint32_t)bo.size(), f.ram + (uint32_t)fo.size());
		for (uint32_t a = (lo + 31) & ~31u; a + 32 <= hi; a += 32)
		{
			const uint8_t* x = &bo[a - b.ram], * y = &fo[a - f.ram];
			int differ = 0;
			bool same = true;
			for (int i = 0; i < 32; i++) { differ += x[i] != y[i]; same = same && x[i] == x[0]; }
			if (differ < 8 || same) continue;
			h.battleSig = { a, 32 };
			h.battleBytes.assign(x, x + 32);
			return h;
		}
		return h;
	}

	// ---- the loader ---------------------------------------------------------------------------------------------------
	std::shared_ptr<RomData> loadRom(const std::string& path, const std::string& cacheDir, std::string* err, RomLoadInfo* info,
		const std::function<void(float)>& progress)
	{
		auto t0 = std::chrono::steady_clock::now();
		auto fail = [&](const std::string& why) -> std::shared_ptr<RomData> { if (err) *err = why; return nullptr; };
		auto step = [&](float v) { if (progress) progress(v); };
		auto rom = std::make_unique<nitro::Rom>();
		std::string e;
		if (!rom->open(path, &e)) return fail(e);
		auto r = std::make_shared<RomImpl>();
		r->path = path;
		r->game = detectGame(rom->header(), 0x200);
		const Files* files = filesFor(r->game.id);
		if (!files) return fail("not one of the DS Pokémon games the companion knows");
		// the cache key: the header and the ARM9 (cheap; differs per release and revision)
		Bytes arm9;
		if (!rom->arm9(arm9)) return fail("can't read the ARM9 binary");
		{
			nitro::Sha1 h;
			h.add(rom->header(), 0x200);
			h.add(arm9.data(), arm9.size());
			r->game.romHash = h.hex();
		}
		std::string cacheFile = cacheDir.empty() ? "" : cacheDir + "/" + r->game.code + "-" + r->game.romHash + ".bin";
		if (info) info->cacheFile = cacheFile;
		auto done = [&](bool cached, size_t bytes) {
			if (info)
			{
				info->fromCache = cached; info->cacheBytes = bytes;
				info->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			}
		};
		Bytes cached;
		if (!cacheFile.empty() && readFile(cacheFile, cached))
		{
			if (load(*r, cached)) { r->rom = std::move(rom); done(true, cached.size()); step(1); return r; }
			// a stale or broken cache: decode again
			Game g = r->game;
			r = std::make_shared<RomImpl>();
			r->path = path;
			r->game = g;
		}
		Decoder d{ *rom, *files, *r, "" };
		step(0.05f);
		if (!d.texts()) return fail(d.err);
		step(0.3f);
		if (!d.personal() || !d.learnsets() || !d.evolutions()) return fail(d.err);
		step(0.45f);
		if (!d.moves() || !d.items()) return fail(d.err);
		for (auto& s : r->species) for (auto& ev : s.evolutions) ev.how = d.evoHow(ev.method, ev.param);
		step(0.55f);
		if (!d.tms(arm9)) return fail(d.err);
		if (!d.zones(arm9)) return fail(d.err);
		if (isB2w2(r->game.id) && !d.grottos(d.zoneWild)) return fail(d.err);
		{
			auto t1 = std::chrono::steady_clock::now();
			townmap::read(*rom, arm9, d.mapHeaders, *r);	// (no town map: no Map page, the rest still works)
			if (info) info->townmapMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
		}
		step(0.8f);
		if (!d.trainers()) return fail(d.err);
		step(0.95f);
		size_t bytes = 0;
		if (!cacheFile.empty())
		{
			mkdir(cacheDir.c_str(), 0755);
			Bytes c = save(*r);
			bytes = c.size();
			writeFile(cacheFile, c);	// (no cache is not an error: the next start decodes again)
		}
		r->rom = std::move(rom);
		done(false, bytes);
		step(1);
		return r;
	}
}
