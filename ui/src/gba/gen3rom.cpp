// banana-ui: the GBA Pokémon games' ROM data as the DS companion's nds::RomData (gen3rom.h).
//
// The tables, as the decompilations (pret: pokeruby, pokeemerald, pokefirered) and Bulbapedia describe them:
//  - gBaseStats: 28 bytes a species (HP, Atk, Def, Speed, SpAtk, SpDef, two types, catch rate, exp yield, EV yield
//    (2 bits a stat), two held items, gender ratio, egg cycles, friendship, growth, egg groups, two abilities, ...)
//  - gBattleMoves: 12 bytes a move (effect, power, type, accuracy, PP, effect chance, target, priority, flags)
//  - gItems: 44 bytes an item (name[14], index, price, hold effect, parameter, description pointer, importance, ?,
//    pocket, type, field use, battle usage, battle use, secondary id)
//  - gEvolutionTable: 5 x 8 bytes a species (method, parameter, into, padding)
//  - gTMHMLearnsets: a u64 a species (bit k: TM k+1, TMs 1-50 then HMs 1-8); sTMHMMoves: the 58 moves
//  - gMoveDescriptionPointers (move 1 first), gAbilityDescriptionPointers, gNatureNamePointers
//  - gTrainers: 40 bytes a trainer (party flags, class, music, picture, name[12], items[4], double battle, AI, party
//    size, party pointer), gTrainerClassNames: 13 bytes a name
//  - gMonFrontPicTable / gMonPaletteTable / gMonShinyPaletteTable: {LZ77 data pointer, size, tag} (tag = species,
//    shiny palettes species + 500); a front picture is 64x64 at 4 bits a pixel in 8x8 tiles (Emerald: two frames, one
//    under the other)
// Emerald, FireRed and LeafGreen point at most of these from their ROM header (0x100 on); Ruby and Sapphire don't, so
// every table is also found by its shape, and each one is checked by its first entries either way.
#include "gba/gen3rom.h"
#include "gba/gen3map.h"
#include "gba/expansion.h"
#include "gba/hacks.h"
#include "nds/nitro.h"
#include "nds/rom.h"
#include <algorithm>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <map>
#include <mutex>

namespace banana::gba
{
	using namespace banana::nds;

	namespace
	{
		struct ItemMap { uint16_t gen4; uint8_t pocket; };
		const ItemMap ITEMS[] = {
#include "gba/gen3items.inc"
		};
		constexpr size_t NUM_ITEMS3 = sizeof ITEMS / sizeof ITEMS[0];
		// the Gen IV items by a hash of their names (a ROM hack's item table: its own items in its own order)
		struct ItemName { uint32_t hash; uint16_t gen4; uint8_t pocket; };
		const ItemName ITEM_NAMES[] = {
#include "gba/gen4names.inc"
		};

		// a name to compare (gba-items-gen.cpp's norm, hashed): letters and digits in lower case, é as e
		uint32_t nameHash(const std::string& s)
		{
			uint32_t h = 2166136261u;
			auto add = [&](char c) { h ^= (unsigned char)c; h *= 16777619u; };
			for (size_t i = 0; i < s.size(); i++)
			{
				unsigned char c = (unsigned char)s[i];
				if (c == 0xc3 && i + 1 < s.size() && ((unsigned char)s[i + 1] == 0xa9 || (unsigned char)s[i + 1] == 0x89)) { add('e'); i++; continue; }
				if (isalnum(c)) add((char)tolower(c));
			}
			return h;
		}
		const ItemName* itemByName(const std::string& s)
		{
			const uint32_t h = nameHash(s);
			auto it = std::lower_bound(std::begin(ITEM_NAMES), std::end(ITEM_NAMES), h, [](const ItemName& e, uint32_t v) { return e.hash < v; });
			return it != std::end(ITEM_NAMES) && it->hash == h ? it : nullptr;
		}

		// how a ROM is read: a retail game's rules, or a hack's (gba/hacks.h)
		struct Src
		{
			bool hack = false, binary = false;	// binary: a patched Emerald (its code's own pointers)
			bool cfru = false;			// FireRed's tables grown (CFRU, or a patched FireRed so: Gaia): 16 evolutions a species,
										// shiny palettes tagged species + 1024, the classes and natures renamed
			bool emerald = false;		// Emerald's layout (its pockets, its two-frame pictures)
			int fairy = -1;				// the Fairy type's id
		};

		uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
		uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

		// a ROM pointer at rom[at] to len bytes inside the ROM: its offset, -1 not one
		long ptr(const uint8_t* rom, size_t n, size_t at, size_t len = 1)
		{
			if (at + 4 > n) return -1;
			uint32_t v = le32(rom + at);
			if (v < 0x08000000u || v - 0x08000000u + len > n) return -1;
			return (long)(v - 0x08000000u);
		}

		// each place pattern is found (memmem), until f says true: its offset, -1 none
		template <class F> long each(const uint8_t* rom, size_t n, const uint8_t* pat, size_t len, F f)
		{
			for (const uint8_t* p = rom; n >= len && (p = (const uint8_t*)memmem(p, n - (p - rom), pat, len)); p++)
				if (f((size_t)(p - rom))) return (long)(p - rom);
			return -1;
		}

		// the Western character set's letters, digits and a few signs (for finding texts), with the end
		std::vector<uint8_t> enc(const char* s)
		{
			std::vector<uint8_t> o;
			for (; *s; s++)
			{
				char c = *s;
				if (c >= 'A' && c <= 'Z') o.push_back((uint8_t)(0xbb + c - 'A'));
				else if (c >= 'a' && c <= 'z') o.push_back((uint8_t)(0xd5 + c - 'a'));
				else if (c >= '0' && c <= '9') o.push_back((uint8_t)(0xa1 + c - '0'));
				else if (c == ' ') o.push_back(0);
				else if (c == '.') o.push_back(0xad);
				else if (c == '-') o.push_back(0xae);
				else if (c == '\n') o.push_back(0xfe);
			}
			return o;
		}

		// the game's text at rom[at] (no more than max bytes) as UTF-8: line breaks as spaces (one after a hyphen
		// joined), control codes left out, the PK MN and POKéBLOCK ligatures spelled out
		std::string text(const uint8_t* rom, size_t n, long at, size_t max)
		{
			std::string s;
			if (at < 0) return s;
			// (the arguments of the FC control codes: pokeemerald's EXT_CTRL_CODE_*)
			static const uint8_t FC_ARGS[0x19] = { 0, 1, 1, 1, 3, 1, 1, 0, 1, 0, 0, 2, 1, 1, 1, 0, 2, 1, 1, 1, 1, 0, 0, 0, 0 };
			for (size_t i = 0; (size_t)at + i < n && i < max; i++)
			{
				uint8_t b = rom[at + i];
				if (b == 0xff) break;
				if (b == 0xfe || b == 0xfa || b == 0xfb)
				{
					if (!s.empty() && s.back() != ' ' && s.back() != '-') s += ' ';
					continue;
				}
				if (b == 0xfc)
				{
					uint8_t c = (size_t)at + i + 1 < n ? rom[at + i + 1] : 0;
					i += 1 + (c < sizeof FC_ARGS ? FC_ARGS[c] : 0);
					continue;
				}
				if (b == 0xfd) { i++; continue; }	// (a placeholder: the player's name, ...)
				static const char* const LIG[7] = { "PK", "MN", "PO", "Ké", "BL", "OC", "K" };
				if (b >= 0x53 && b <= 0x59) { s += LIG[b - 0x53]; continue; }
				s += pkmn::decodeText(&b, 1);
			}
			while (!s.empty() && s.back() == ' ') s.pop_back();
			return s;
		}
		std::string name(const uint8_t* rom, size_t n, long at, size_t len) { return text(rom, n, at, len); }

		// a fixed-size name: an end within len bytes, and not empty unless allowed
		bool nameAt(const uint8_t* rom, size_t n, size_t at, size_t len, bool empty = false)
		{
			for (size_t k = 0; k < len && at + k < n; k++)
				if (rom[at + k] == 0xff) return empty || k > 0;
			return false;
		}

		// a table of count pointers whose first points at target: its offset, -1 none
		long pointerTable(const uint8_t* rom, size_t n, long target, size_t count)
		{
			if (target < 0) return -1;
			uint8_t p[4];
			uint32_t v = (uint32_t)target + 0x08000000u;
			for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
			return each(rom, n, p, 4, [&](size_t a) {
				if (a % 4 || a + 4 * count > n) return false;
				for (size_t i = 0; i < count; i++) if (ptr(rom, n, a + 4 * i, 2) < 0) return false;
				return true;
			});
		}
		// the pointer table of the first text found
		long textTable(const uint8_t* rom, size_t n, std::initializer_list<const char*> firsts, size_t count)
		{
			for (const char* f : firsts)
			{
				auto e = enc(f);
				long t = -1;
				each(rom, n, e.data(), e.size(), [&](size_t a) { t = pointerTable(rom, n, (long)a, count); return t >= 0; });
				if (t >= 0) return t;
			}
			return -1;
		}

		// a gen3 type id (??? at 9) in nds::Type's order (fairy: a hack's Fairy type's id)
		Type typeOf(uint8_t t, int fairy = -1)
		{
			if (fairy >= 0 && t == fairy) return FAIRY;
			if (t < 9) return (Type)t;
			if (t >= 10 && t <= 17) return (Type)(t - 1);
			return NO_TYPE;
		}

		// where the tables are (offsets in the ROM; -1 not found)
		struct Tables
		{
			long base = -1, moves = -1, items = -1, evo = -1, tmCompat = -1, tmMoves = -1, moveDesc = -1, abilityDesc = -1,
				natures = -1, classes = -1, trainers = -1, front = -1, pals = -1, shiny = -1;
			int numClasses = 0, numTrainers = 0, numItems = 0;
			int evoStride = 40, shinyTag = 500;
			size_t moveDescs = 354;				// (the move descriptions' table: its pointers)	// (CFRU: 16 evolutions a species; the shiny palettes tagged species + 1024)
		};

		bool trainerAt(const uint8_t* rom, size_t n, size_t a)
		{
			if (a + 40 > n || rom[a] > 3 || rom[a + 32] < 1 || rom[a + 32] > 6) return false;
			if (!nameAt(rom, n, a + 4, 12, true)) return false;
			return ptr(rom, n, a + 36, (rom[a] & 1 ? 16 : 8) * (size_t)rom[a + 32]) >= 0;
		}

		Tables find(const uint8_t* rom, size_t n, bool header, const Src& src = Src())
		{
			Tables x;
			// (a patched Emerald: where its code points, each checked as below)
			auto em = [&](pkmn::EmTable w) { return src.binary ? pkmn::emeraldTable(rom, n, w) : -1L; };
			// base stats: Bulbasaur (45 49 49 45 65 65, Grass/Poison) and Ivysaur after it
			auto baseOk = [&](long a) { static const uint8_t B[8] = { 45, 49, 49, 45, 65, 65, 12, 3 }, I[6] = { 60, 62, 63, 60, 80, 80 };
				return a >= 0 && (size_t)a + 28 * 412 <= n && !memcmp(rom + a + 28, B, 8) && !memcmp(rom + a + 56, I, 6); };
			if (header && baseOk(ptr(rom, n, 0x1bc))) x.base = ptr(rom, n, 0x1bc);
			else { static const uint8_t B[8] = { 45, 49, 49, 45, 65, 65, 12, 3 }; long a = each(rom, n, B, 8, [&](size_t a) { return a >= 28 && baseOk((long)a - 28); }); if (a >= 0) x.base = a - 28; }
			// moves: Pound (40, Normal, 100 %, 35 PP), Karate Chop (50, Fighting, 100, 25), Double Slap (15, Normal, 85, 10)
			auto movesOk = [&](long a) { static const uint8_t M[3][4] = { { 40, 0, 100, 35 }, { 50, 1, 100, 25 }, { 15, 0, 85, 10 } };
				if (a < 0 || (size_t)a + 12 * 355 > n) return false;
				for (int i = 0; i < (src.hack ? 1 : 3); i++) if (memcmp(rom + a + 12 * (i + 1) + 1, M[i], 4)) return false;	// (a hack: Pound's alone)
				return true; };
			if (header && movesOk(ptr(rom, n, 0x1cc))) x.moves = ptr(rom, n, 0x1cc);
			else { static const uint8_t P[5] = { 0, 40, 0, 100, 35 }; long a = each(rom, n, P, 5, [&](size_t a) { return a >= 12 && movesOk((long)a - 12); }); if (a >= 0) x.moves = a - 12; }
			// items: "????????" first, each with its own index at 14
			auto itemsOk = [&](long a) { if (a < 0 || (size_t)a + 44 * 3 > n) return false;
				for (int i = 0; i < 3; i++) if (le16(rom + a + 44 * i + 14) != i) return false;
				return true; };
			if (header && itemsOk(ptr(rom, n, 0x1c8))) x.items = ptr(rom, n, 0x1c8);
			else { static const uint8_t Q[9] = { 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xff }; x.items = each(rom, n, Q, 9, [&](size_t a) { return itemsOk((long)a); }); }
			// evolutions: none for species 0, Bulbasaur's (level 16 into Ivysaur, 2), Ivysaur's (32, Venusaur)
			{
				static const uint8_t E[8] = { 4, 0, 16, 0, 2, 0, 0, 0 }, Z[40] = {};
				long a = each(rom, n, E, 8, [&](size_t a) { return a >= 40 && a % 4 == 0 && a + 40 * 412 <= n && !memcmp(rom + a - 40, Z, 40) &&
					le16(rom + a + 40) == 4 && le16(rom + a + 42) == 32 && le16(rom + a + 44) == 3; });
				if (a >= 0) x.evo = a - 40;
				// (a hack: Emerald's code's own, or CFRU's of 16 a species - species 0's need not be empty)
				long e = em(pkmn::EmTable::EVOLUTIONS);
				if (e >= 0 && (size_t)e + 80 + 8 <= n && !memcmp(rom + e + 40, E, 8)) x.evo = e;
				if (src.cfru)
				{
					a = each(rom, n, E, 8, [&](size_t a) { return a >= 128 && a % 4 == 0 && a + 128 * 412 <= n &&
						le16(rom + a + 128) == 4 && le16(rom + a + 130) == 32 && le16(rom + a + 132) == 3; });
					if (a >= 0) { x.evo = a - 128; x.evoStride = 128; }
				}
			}
			// TM/HM learnsets: Mew (151) learns all 58; species 0 and the placeholders after Celebi (252-276) none
			{
				static const uint8_t M[8] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03 }, Z[8] = {};
				long a = each(rom, n, M, 8, [&](size_t a) {
					if (a < 151 * 8 || (a - 151 * 8) % 4 || a - 151 * 8 + 412 * 8 > n) return false;
					size_t s = a - 151 * 8;
					if (memcmp(rom + s, Z, 8)) return false;
					for (int i = 252; i <= 276; i++) if (memcmp(rom + s + 8 * i, Z, 8)) return false;
					return true; });
				if (a >= 0) x.tmCompat = a - 151 * 8;
				// (a hack fills the placeholders after Celebi: Emerald's code's own, or species 0 learning none and Mew all)
				long e = em(pkmn::EmTable::TM_COMPAT);
				if (e >= 0 && (size_t)e + 8 * 412 <= n && !memcmp(rom + e, Z, 8) && !memcmp(rom + e + 151 * 8, M, 8)) x.tmCompat = e;
				else if (src.hack && x.tmCompat < 0)
				{
					a = each(rom, n, M, 8, [&](size_t a) { return a >= 151 * 8 && (a - 151 * 8) % 4 == 0 && a - 151 * 8 + 412 * 8 <= n &&
						!memcmp(rom + a - 151 * 8, Z, 8) && memcmp(rom + a - 150 * 8, Z, 8); });
					if (a >= 0) x.tmCompat = a - 151 * 8;
				}
			}
			// the TM/HM moves: Focus Punch, Dragon Claw, Water Pulse, Calm Mind ...; the HMs Cut, Fly, Surf, Strength, Flash,
			// Rock Smash, Waterfall, Dive
			{
				static const uint8_t T[8] = { 0x08, 0x01, 0x51, 0x01, 0x60, 0x01, 0x5b, 0x01 };
				static const uint16_t HM[8] = { 15, 19, 57, 70, 148, 249, 127, 291 };
				auto hmsAt = [&](size_t a) {
					if (a % 2 || a + 116 > n) return false;
					for (int i = 0; i < 8; i++) if (le16(rom + a + 100 + 2 * i) != HM[i]) return false;
					return true; };
				x.tmMoves = each(rom, n, T, 8, hmsAt);
				long e = em(pkmn::EmTable::TM_MOVES);
				if (e >= 0 && hmsAt((size_t)e)) x.tmMoves = e;
			}
			// texts: Pound's description (Ruby/Sapphire/Emerald's, FireRed/LeafGreen's), no ability's, Hardy
			x.moveDesc = textTable(rom, n, { "Pounds the foe with", "A physical attack\ndelivered with a" }, 354);
			{
				long e = em(pkmn::EmTable::MOVE_DESC);
				if (e >= 0 && ptr(rom, n, (size_t)e) >= 0) x.moveDesc = e;
				// (a hack with more moves keeps a longer table beside its base game's: of those pointing at Pound's, the
				// longest run of pointers)
				if (src.hack && x.moveDesc >= 0)
				{
					auto run = [&](size_t a) { size_t k = 0; while (ptr(rom, n, a + 4 * k) >= 0) k++; return k; };
					const uint32_t first = le32(rom + x.moveDesc);
					uint8_t pat[4] = { (uint8_t)first, (uint8_t)(first >> 8), (uint8_t)(first >> 16), (uint8_t)(first >> 24) };
					size_t best = run((size_t)x.moveDesc);
					each(rom, n, pat, 4, [&](size_t a) { if (a % 4 == 0 && run(a) > best) { best = run(a); x.moveDesc = (long)a; } return false; });
					x.moveDescs = best;
				}
			}
			x.abilityDesc = textTable(rom, n, { "No special ability." }, 78);
			if (src.hack && header && ptr(rom, n, 0x1c4, 4 * 78) >= 0 && ptr(rom, n, (size_t)ptr(rom, n, 0x1c4)) >= 0) x.abilityDesc = ptr(rom, n, 0x1c4);
			{
				long e = em(pkmn::EmTable::NATURE_NAMES);	// (a patched Emerald's may be renamed: Golden Glazed's "HARDY(Neutral)")
				if (e >= 0 && ptr(rom, n, (size_t)e + 4 * 24) >= 0) x.natures = e;
			}
			for (const char* w : { "HARDY", "Hardy" })	// (a CFRU hack's in mixed case)
			{
				if (x.natures >= 0 || (!src.hack && w[1] == 'a')) break;
				auto h = enc(w); h.push_back(0xff);
				long t = -1;
				each(rom, n, h.data(), h.size(), [&](size_t a) { t = pointerTable(rom, n, (long)a, 25); return t >= 0; });
				x.natures = t;
			}
			// trainer classes: 13 bytes each, the first two "{PKMN} TRAINER". A hack renames the first ones: a patched
			// Emerald's where its code points; a FireRed hack's by its gym leaders' class (0x54) named "Leader"
			{
				long e = em(pkmn::EmTable::TRAINER_CLASSES);
				if (e >= 0 && nameAt(rom, n, (size_t)e, 13) && nameAt(rom, n, (size_t)e + 13, 13)) x.classes = e;
				if (src.cfru)
					for (const char* w : { "Leader", "LEADER" })
					{
						auto l = enc(w); l.push_back(0xff);
						each(rom, n, l.data(), l.size(), [&](size_t a) {
							if (x.classes >= 0 || a < 0x54 * 13) return x.classes >= 0;
							const size_t s0 = a - 0x54 * 13;
							for (int k = 0; k <= 0x54; k++) if (!nameAt(rom, n, s0 + 13 * (size_t)k, 13)) return false;
							x.classes = (long)s0;
							return true; });
					}
				if (x.classes >= 0)
					while (x.numClasses < 255 && nameAt(rom, n, x.classes + 13 * (size_t)x.numClasses, 13)) x.numClasses++;
			}
			for (const char* w : { "TRAINER", "Trainer" })
			{
				if (x.classes >= 0) break;
				std::vector<uint8_t> p = { 0x53, 0x54, 0x00 };
				auto tr = enc(w); p.insert(p.end(), tr.begin(), tr.end()); p.push_back(0xff);
				x.classes = each(rom, n, p.data(), p.size(), [&](size_t a) { return a + 26 <= n && !memcmp(rom + a + 13, p.data(), p.size()); });
				if (x.classes >= 0)
					while (x.numClasses < (src.hack ? 255 : 120) && nameAt(rom, n, x.classes + 13 * (size_t)x.numClasses, 13)) x.numClasses++;
			}
			// trainers: the longest run of records after an empty one (TRAINER_NONE: no party); a patched Emerald's where
			// its code points
			if (em(pkmn::EmTable::TRAINERS) >= 0 && trainerAt(rom, n, (size_t)em(pkmn::EmTable::TRAINERS) + 40))
			{
				x.trainers = em(pkmn::EmTable::TRAINERS);
				while (trainerAt(rom, n, (size_t)x.trainers + 40 * (size_t)(x.numTrainers + 1))) x.numTrainers++;
				x.numTrainers++;
			}
			else
			{
				int best = 0;
				for (size_t a = 0; a + 80 <= n; a += 4)
				{
					if (rom[a + 32] || le32(rom + a + 36) || !trainerAt(rom, n, a + 40)) continue;
					int k = 1;
					while (trainerAt(rom, n, a + 40 * (size_t)k)) k++;
					if (k > best) { best = k; x.trainers = (long)a; x.numTrainers = k; }
					a += 40 * (size_t)(k - 1);
				}
				if (best < 100) x.trainers = -1;
			}
			// the front pictures and their palettes: entries {pointer, size, tag}
			auto picsOk = [&](long a) { if (a < 0 || (size_t)a + 8 * 440 > n) return false;
				for (int i = 1; i < 252; i++) if (le16(rom + a + 8 * i + 6) != i || le16(rom + a + 8 * i + 4) != 0x800 || ptr(rom, n, a + 8 * i) < 0) return false;
				return true; };
			auto palsOk = [&](long a, int add) { if (a < 0 || (size_t)a + 8 * 440 > n) return false;
				for (int i = 1; i < 252; i++) if (le16(rom + a + 8 * i + 4) != i + add || le16(rom + a + 8 * i + 6) != 0 || ptr(rom, n, a + 8 * i) < 0) return false;
				return true; };
			// (a CFRU hack tags the shiny palettes species + 1024)
			if (src.cfru && header && ptr(rom, n, 0x134, 16) >= 0) x.shinyTag = le16(rom + ptr(rom, n, 0x134) + 8 + 4) - 1;
			if (header && picsOk(ptr(rom, n, 0x128)) && palsOk(ptr(rom, n, 0x130), 0) && palsOk(ptr(rom, n, 0x134), x.shinyTag))
			{ x.front = ptr(rom, n, 0x128); x.pals = ptr(rom, n, 0x130); x.shiny = ptr(rom, n, 0x134); }
			else
			{
				// (the first such table is the front pictures' - the back pictures' follows it - then the palettes)
				static const uint8_t S[4] = { 0x00, 0x08, 0x01, 0x00 };
				long a = each(rom, n, S, 4, [&](size_t a) { return a >= 12 && (a - 12) % 4 == 0 && picsOk((long)a - 12); });
				if (a >= 0) x.front = a - 12;
				static const uint8_t P[4] = { 0x01, 0x00, 0x00, 0x00 }, SP[4] = { 0xf5, 0x01, 0x00, 0x00 };
				a = each(rom, n, P, 4, [&](size_t a) { return a >= 12 && (a - 12) % 4 == 0 && palsOk((long)a - 12, 0); });
				if (a >= 0) x.pals = a - 12;
				a = each(rom, n, SP, 4, [&](size_t a) { return a >= 12 && (a - 12) % 4 == 0 && palsOk((long)a - 12, 500); });
				if (a >= 0) x.shiny = a - 12;
			}
			return x;
		}

		// ---- the RomData -----------------------------------------------------------------------------------------------
		struct Gen3Data : RomData
		{
			std::shared_ptr<const std::vector<uint8_t>> rom;
			Tables tab;
			Src src;
			std::vector<uint16_t> internal;	// RomData::species index (national number; forms after dexSize) -> the game's species index
			std::vector<std::pair<uint16_t, uint8_t>> natOf;	// the game's species index -> national number, form (a hack's)
			std::vector<uint16_t> itemMap;	// the game's item id -> RomData::items' (a hack's; empty: itemToGen4)
			mutable std::mutex mu;
			mutable std::map<uint32_t, std::shared_ptr<const Sprite>> sprites;

			bool palette(long table, uint16_t i, std::array<uint32_t, 16>& out) const
			{
				const uint8_t* r = rom->data();
				size_t n = rom->size();
				long p = ptr(r, n, (size_t)table + 8 * i, 4);
				std::vector<uint8_t> c;
				if (p < 0 || !pkmn::lz77(r, n, (size_t)p, c, 128) || c.size() < 32) return false;	// (Castform: four, one a form)
				for (int k = 0; k < 16; k++)
				{
					uint16_t v = le16(c.data() + 2 * k);
					auto f = [](int x) { return (uint32_t)(x << 3 | x >> 2); };
					out[k] = f(v & 31) << 16 | f(v >> 5 & 31) << 8 | f(v >> 10 & 31);
				}
				return true;
			}

			std::shared_ptr<const Sprite> sprite(uint16_t species, bool, uint8_t form) const override
			{
				if (src.hack && form && species < this->species.size())	// (a hack's forms: species of their own, after dexSize)
				{
					const Species& b = this->species[species];
					if (b.formsAt && form < b.formCount) species = (uint16_t)(b.formsAt + form - 1);
					form = 0;
				}
				if (species == 0 || species >= internal.size() || !internal[species] || tab.front < 0 || tab.pals < 0 || tab.shiny < 0) return nullptr;
				uint16_t sp = internal[species], pic = sp;
				if (species == 201 && form >= 1 && form <= 27) pic = (uint16_t)(412 + form);	// (Unown B..?: after the egg)
				const uint32_t key = (uint32_t)pic << 8 | form;
				std::lock_guard<std::mutex> lk(mu);
				auto it = sprites.find(key);
				if (it != sprites.end()) return it->second;
				const uint8_t* r = rom->data();
				size_t n = rom->size();
				long p = ptr(r, n, (size_t)tab.front + 8 * pic, 4);
				std::vector<uint8_t> px;
				auto s = std::make_shared<Sprite>();
				if (p < 0 || !pkmn::lz77(r, n, (size_t)p, px, 0x2000) || px.size() < 0x800) return nullptr;
				if (px.size() > 0x1000) px.resize(0x800);	// (Castform: its four forms, one under the other; the first)
				if (!palette(tab.pals, sp, s->normal) || !palette(tab.shiny, sp, s->shiny)) return nullptr;
				if (px.size() > 0x800 && px.size() < 0x1000) px.resize(0x800);	// (a hack's picture with some bytes more)
				s->w = s->h = 64;
				for (size_t f = 0; f * 0x800 + 0x800 <= px.size(); f++)
				{
					std::vector<uint8_t> fr(64 * 64);
					const uint8_t* d = px.data() + f * 0x800;
					for (int t = 0; t < 64; t++)
						for (int y = 0; y < 8; y++)
							for (int x = 0; x < 8; x++)
								fr[(size_t)((t / 8 * 8 + y) * 64 + t % 8 * 8 + x)] = (uint8_t)(d[t * 32 + y * 4 + x / 2] >> (4 * (x & 1)) & 15);
					s->frames.push_back(std::move(fr));
				}
				// Deoxys: its normal form, then the one this game shows (FireRed Attack, LeafGreen Defense, Emerald Speed),
				// which is the still. Else Emerald's two frames, played as the DS companion plays Gen IV's: the still a
				// second, then a quick twitch
				if (species == 386 && s->frames.size() >= 2 && !src.hack && game.id != GameId::RUBY && game.id != GameId::SAPPHIRE)
					s->frames.erase(s->frames.begin());
				else if (src.emerald && s->frames.size() >= 2 && s->frames[1] != s->frames[0])
					s->anim = { { 0, 1000 }, { 1, 250 }, { 0, 250 }, { 1, 250 } };
				s->frames.resize(s->anim.empty() ? 1 : 2);
				sprites[key] = s;
				return s;
			}
		};

		// the moves a trainer's Pokémon knows without a list of its own: the last four it learns by levelling up to its
		// level (the games' GiveMonInitialMoveset)
		void defaultMoves(const RomData& r, TrainerMon& m)
		{
			if (m.species >= r.species.size()) return;
			std::vector<uint16_t> known;
			for (auto& lm : r.species[m.species].learnset)
			{
				if (lm.level > m.level) break;
				if (std::find(known.begin(), known.end(), lm.move) != known.end()) continue;
				known.push_back(lm.move);
			}
			size_t from = known.size() > 4 ? known.size() - 4 : 0;
			for (size_t j = from; j < known.size(); j++) m.moves[j - from] = known[j];
		}

		std::string evoHow(const RomData& r, GameId g, uint16_t method, uint16_t param)
		{
			auto item = [&](uint16_t i) { return i < r.items.size() && !r.items[i].name.empty() ? r.items[i].name : std::string("an item"); };
			std::string lv = "Level " + std::to_string(param);
			const bool noClock = g == GameId::FIRERED || g == GameId::LEAFGREEN;
			switch (method)
			{
			case evo::FRIENDSHIP: return "Level up with high friendship";
			case evo::FRIENDSHIP_DAY: return noClock ? "Level up with high friendship by day (no clock in this game)" : "Level up with high friendship by day";
			case evo::FRIENDSHIP_NIGHT: return noClock ? "Level up with high friendship at night (no clock in this game)" : "Level up with high friendship at night";
			case evo::LEVEL: return lv;
			case evo::TRADE: return "Trade";
			case evo::TRADE_ITEM: return "Trade holding " + item(param);
			case evo::USE_ITEM: return "Use " + item(param);
			case evo::LEVEL_ATK_GT_DEF: return lv + " with Attack higher than Defense";
			case evo::LEVEL_ATK_EQ_DEF: return lv + " with Attack equal to Defense";
			case evo::LEVEL_ATK_LT_DEF: return lv + " with Defense higher than Attack";
			case evo::LEVEL_PID_LOW: case evo::LEVEL_PID_HIGH: return lv + " (one of two, by its personality)";
			case evo::LEVEL_NINJASK: return lv;
			case evo::LEVEL_SHEDINJA: return lv + " with a free party slot and a spare Poké Ball";
			case evo::BEAUTY: return "Level up with high Beauty";
			// (a CFRU hack's)
			case evo::LEVEL_MALE: return lv + ", male";
			case evo::LEVEL_FEMALE: return lv + ", female";
			case evo::KNOWS_MOVE: return "Level up knowing " + (param < r.moves.size() && !r.moves[param].name.empty() ? r.moves[param].name : std::string("a move"));
			case evo::SPECIES_IN_PARTY: return "Level up with " + (param < r.species.size() && !r.species[param].name.empty() ? r.species[param].name : std::string("a Pokémon")) + " in the party";
			case evo::USE_ITEM_MALE: return "Use " + item(param) + " on a male";
			case evo::USE_ITEM_FEMALE: return "Use " + item(param) + " on a female";
			default: return "";
			}
		}
	}

	uint16_t itemToGen4(uint16_t i)
	{
		if (!i) return 0;
		if (i < NUM_ITEMS3 && ITEMS[i].gen4) return ITEMS[i].gen4;
		return (uint16_t)(GEN3_ONLY + i);
	}

	namespace
	{
		// the hacks read here: their Pokédex's size (the national numbers past it are their forms and extra slots)
		struct HackDex { const char* key; int dexSize; };
		const HackDex HACK_DEX[] = { { "glazed", 386 }, { "golden-glazed", 386 }, { "glazed-unofficial", 386 }, { "gaia", 721 } };

		// a patched Emerald that renamed ??? (type 9) its Fairy type
		bool fairyAt9(const uint8_t* rom, size_t n)
		{
			long tn = pkmn::emeraldTable(rom, n, pkmn::EmTable::TYPE_NAMES);
			if (tn < 0 || (size_t)tn + 7 * 18 > n) return false;
			auto f = enc("FAIRY");
			f.push_back(0xff);
			return !memcmp(rom + tn + 7 * 9, f.data(), f.size());
		}
	}

	Game detectGame(const uint8_t* rom, size_t n, uint32_t crc)
	{
		Game g;
		if (!rom || n < 0xc0) return g;
		g.code.assign((const char*)rom + 0xac, 4);
		g.revision = rom[0xbc];
		static const struct { const char* c; GameId id; const char* t; } G[] = {
			{ "AXVE", GameId::RUBY, "Ruby" }, { "AXPE", GameId::SAPPHIRE, "Sapphire" }, { "BPEE", GameId::EMERALD, "Emerald" },
			{ "BPRE", GameId::FIRERED, "FireRed" }, { "BPGE", GameId::LEAFGREEN, "LeafGreen" } };
		for (auto& e : G)
			if (g.code == e.c) { g.id = e.id; g.title = e.t; }
		if (g.id == GameId::NONE) return g;
		g.gen = Gen::III;
		g.dexSize = 386;
		// the hash: the header, and 64 bytes of every 64 KB (a quarter of a MB to read; a patched ROM differs somewhere
		// in its tables, which are spread over all of it)
		auto hash = [&]
		{
			nitro::Sha1 h;
			h.add(rom, std::min<size_t>(n, 0x200));
			for (size_t a = 0x200; a + 64 <= n; a += 0x10000) h.add(rom + a, 64);
			const uint8_t len[4] = { (uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24) };
			h.add(len, 4);
			g.romHash = h.hex();
		};
		// a ROM hack the companion knows (its base game's code, its whole ROM's CRC-32): who it is, its Pokédex, its chart
		if ((g.id == GameId::EMERALD || g.id == GameId::FIRERED) && n >= 0x1000000)
		{
			if (!crc) crc = romCrc32(rom, n);
			const HackInfo* h = hackByCrc(crc);
			if (h && g.code == h->base)
			{
				g.id = GameId::HACK;
				g.title = h->title;
				g.hack = h->key;
				g.dexSize = 0;		// (not known here: the reader of its engine says)
				for (auto& d : HACK_DEX) if (!strcmp(d.key, h->key)) g.dexSize = d.dexSize;
				g.modern = h->engine == Engine::CFRU || h->engine == Engine::EXPANSION || (h->engine == Engine::BINARY && fairyAt9(rom, n));
				// (a patched FireRed with the Fairy type at CFRU's 23 in its base stats: Gaia)
				long bs = ptr(rom, n, 0x1bc, 28 * 412);
				for (int i = 1; !g.modern && bs >= 0 && i < 412; i++) g.modern = rom[bs + 28 * i + 6] == 23 || rom[bs + 28 * i + 7] == 23;
				hash();
				return g;
			}
		}
		// a CFRU hack (and others built the same way) moves the species names to a longer table and adds the Fairy
		// type: the header's species names no longer end where its move names start (412 x 11 bytes), or a base stat
		// record has type 23
		if (g.id != GameId::RUBY && g.id != GameId::SAPPHIRE)
		{
			long sp = ptr(rom, n, 0x144), mv = ptr(rom, n, 0x148), bs = ptr(rom, n, 0x1bc, 28 * 412);
			bool hack = sp < 0 || mv < 0 || bs < 0 || mv - sp != 412 * 11;
			for (int i = 0; !hack && i < 412; i++) hack = rom[bs + 28 * i + 6] > 17 || rom[bs + 28 * i + 7] > 17;
			if (hack) { g.id = GameId::NONE; g.gen = Gen::IV; g.dexSize = 493; return g; }
		}
		hash();
		return g;
	}

	namespace
	{
		// a hack's species name (the ROM's own bytes: a Glazed release writes its Mega Evolutions with the symbol 0x57)
		std::string speciesName(const uint8_t* rom, size_t n, const pkmn::RomTables& t, uint16_t s)
		{
			long sp = ptr(rom, n, 0x144, 11 * ((size_t)s + 1));
			if (sp >= 0 && rom[sp + 11 * s] == 0x57) return "Mega " + nameCase(name(rom, n, sp + 11 * (long)s + 1, 10));
			return s < t.species.size() ? t.species[s] : "";
		}
		// a name not yet in the DS games' case: the ROM's capitals (a CFRU hack writes its own in mixed case already)
		std::string dsCase(const std::string& s, bool hack)
		{
			if (hack) for (char c : s) if (c >= 'a' && c <= 'z') return s;
			return nameCase(s);
		}
		bool placeholder(const std::string& s)
		{
			if (s.empty() || s[0] == '?' || s == "-" || s == "Bad Egg" || s == "BAD EGG") return true;
			for (char c : s) if (isalpha((unsigned char)c)) return false;
			return true;	// (no letters: an unused slot)
		}
	}

	std::shared_ptr<const RomData> readRomData(const pkmn::RomTables& t, const Game& game, const std::string&)
	{
		// (a pokeemerald-expansion build: its own structs and tables, gba/expansion.cpp)
		if (const HackInfo* h = hackByKey(game.hack.c_str())) if (h->engine == Engine::EXPANSION) return readExpansionRomData(t, game);
		if (!t.rom || game.gen != Gen::III || game.id == GameId::NONE) return nullptr;
		Src src;
		const HackInfo* hi = game.id == GameId::HACK ? hackByKey(game.hack.c_str()) : nullptr;
		if (game.id == GameId::HACK)
		{
			if (!hi) return nullptr;
			// (Engine::EXPANSION: the pokeemerald-expansion builds have a reader of their own, gba/expansion.*)
			if (hi->engine == Engine::EXPANSION) return nullptr;
			src.hack = true;
			src.emerald = !strcmp(hi->base, "BPEE");
			src.cfru = hi->engine == Engine::CFRU || (hi->engine == Engine::BINARY && !src.emerald && t.numSpecies > 412);
			src.binary = hi->engine == Engine::BINARY && src.emerald;
			src.fairy = t.has.fairyType >= 0 ? t.has.fairyType : src.cfru ? pkmn::FAIRY : -1;
		}
		else if (t.has.modernTypes || t.has.ownDex || t.numSpecies != 412) return nullptr;
		src.emerald = src.emerald || game.id == GameId::EMERALD;
		const GameId base = !hi ? game.id : src.emerald ? GameId::EMERALD : GameId::FIRERED;	// (the base game's rules)
		const uint8_t* rom = t.rom->data();
		const size_t n = t.rom->size();
		auto out = std::make_shared<Gen3Data>();
		out->rom = t.rom;
		out->game = game;
		out->src = src;
		const bool header = game.id != GameId::RUBY && game.id != GameId::SAPPHIRE;
		Tables& x = out->tab;
		x = find(rom, n, header, src);
		// (the items: as many as today's tables found - some records' own index isn't theirs)
		x.numItems = x.items >= 0 ? (int)std::min<size_t>(t.items.size(), (n - (size_t)x.items) / 44) : 0;
		const int nsp = t.numSpecies;
		if (x.base < 0 || x.moves < 0 || x.items < 0 || x.evo < 0 || x.tmCompat < 0 || x.tmMoves < 0 || (int)t.natDex.size() < nsp ||
			t.dexSpecies.size() < 387 || t.moves.size() < 355 || t.learnsets < 0 || (size_t)x.base + 28 * (size_t)nsp > n ||
			(size_t)x.evo + (size_t)x.evoStride * (size_t)nsp > n || (size_t)x.tmCompat + 8 * (size_t)nsp > n)
		{
			fprintf(stderr, "gba companion: %s: a table not found (base %ld moves %ld items %ld evolutions %ld TMs %ld/%ld, %zu national numbers, "
				"%zu moves, learnsets %ld)\n", game.title.c_str(), x.base, x.moves, x.items, x.evo, x.tmCompat, x.tmMoves, t.natDex.size(),
				t.moves.size(), t.learnsets);
			return nullptr;
		}

		// the species: by national number up to the Pokédex's size; a hack's other species (forms, Mega Evolutions, its
		// extra slots) after it, each the form of the species whose name it carries
		int dexSize = 386;
		if (src.hack)
		{
			dexSize = game.dexSize;
			if (dexSize <= 0) for (int s = 1; s < nsp; s++) dexSize = std::max(dexSize, (int)t.natDex[s]);
			dexSize = std::min(dexSize, (int)t.dexSpecies.size() - 1);
			out->game.dexSize = dexSize;
		}
		out->internal.assign((size_t)dexSize + 1, 0);
		for (int d = 1; d <= dexSize; d++) out->internal[d] = t.dexSpecies[d];
		std::vector<std::string> names((size_t)nsp);
		for (int s = 1; s < nsp; s++) names[s] = src.hack ? dsCase(speciesName(rom, n, t, (uint16_t)s), true) : s < (int)t.species.size() ? t.species[s] : "";
		if (src.hack)
		{
			out->natOf.assign((size_t)nsp, { 0, 0 });
			for (int d = 1; d <= dexSize; d++) if (out->internal[d]) out->natOf[out->internal[d]] = { (uint16_t)d, 0 };
			// the rest: the form of the species of its national number, else of the one its name starts with
			// ("Mega Venusaur", "Kyurem-B", "Blk Kyurem", "UnownB", "Charizarx" - the longest such name)
			auto lower = [](std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; };
			std::vector<std::string> baseName((size_t)dexSize + 1);
			for (int d = 1; d <= dexSize; d++) if (out->internal[d]) baseName[d] = lower(names[out->internal[d]]);
			std::map<uint16_t, std::vector<uint16_t>> forms;	// national -> the game's species
			std::vector<uint16_t> extras;		// a species past the Pokédex, no form of one in it (Glazed's Timburr): after the forms
			for (int s = 1; s < nsp; s++)
			{
				if (out->natOf[s].first || placeholder(names[s])) continue;
				const uint8_t* b = rom + x.base + 28 * s;
				if (!b[0] && !b[1] && !b[2]) continue;		// (no base stats: an unused slot)
				int d = t.natDex[s] >= 1 && t.natDex[s] <= dexSize ? t.natDex[s] : 0;
				if (!d)
				{
					std::string nm = lower(names[s]);
					if (nm.compare(0, 5, "mega ") == 0) nm = nm.substr(5);
					size_t best = 0;
					for (int k = 1; k <= dexSize; k++)
					{
						const std::string& bn = baseName[k];
						if (bn.size() < 3 || bn.size() <= best) continue;
						bool pre = nm.compare(0, bn.size(), bn) == 0;
						bool last = nm.size() > bn.size() && nm.compare(nm.size() - bn.size(), bn.size(), bn) == 0 && nm[nm.size() - bn.size() - 1] == ' ';
						if (pre || last) { best = bn.size(); d = k; }
					}
					// (a Mega Evolution's name cut short: "charizarx" for Charizard's X)
					for (int k = 1; !d && k <= dexSize; k++)
						if (baseName[k].size() >= 8 && nm.size() >= 8 && nm.compare(0, 8, baseName[k], 0, 8) == 0)
						{
							d = k;
							const char c = nm.back();
							if (names[s].compare(0, 5, "Mega ") == 0 && (c == 'x' || c == 'y') && nm.size() == baseName[k].size())
								names[s] = "Mega " + names[out->internal[k]] + " " + (char)toupper(c);
						}
				}
				if (d) forms[(uint16_t)d].push_back((uint16_t)s);
				else extras.push_back((uint16_t)s);
			}
			for (auto& f : forms)
			{
				if (!out->internal[f.first]) continue;
				const size_t at = out->internal.size();
				for (size_t k = 0; k < f.second.size() && k < 254; k++)
				{
					out->internal.push_back(f.second[k]);
					out->natOf[f.second[k]] = { f.first, (uint8_t)(k + 1) };
				}
				(void)at;
			}
			for (uint16_t s : extras) { out->natOf[s] = { (uint16_t)out->internal.size(), 0 }; out->internal.push_back(s); }
			// (the forms' links are set below, once the species vector is there)
			out->species.resize(out->internal.size());
			size_t i = (size_t)dexSize + 1;
			for (auto& f : forms)
			{
				if (!out->internal[f.first]) continue;
				out->species[f.first].formsAt = (uint16_t)i;
				out->species[f.first].formCount = (uint8_t)std::min<size_t>(f.second.size() + 1, 255);
				i += std::min<size_t>(f.second.size(), 254);
			}
		}
		else out->species.resize(387);
		auto nat = [&](uint16_t s) -> uint16_t {
			if (src.hack) return s < out->natOf.size() ? out->natOf[s].first : 0;
			return s < t.natDex.size() && t.natDex[s] <= 386 ? t.natDex[s] : 0; };

		// items (first: the evolutions and the trainers name them). A hack's by their names: Gen IV's ids where they
		// are Gen IV items, the rest GEN3_ONLY + their own
		auto pocketOf = [&](uint8_t p) -> uint8_t {
			if (src.emerald || !src.hack) return p == 2 ? POCKET_BALLS : p == 3 ? POCKET_TMS : p == 4 ? POCKET_BERRIES : p == 5 ? POCKET_KEY : POCKET_ITEMS;
			return p == 2 ? POCKET_KEY : p == 3 ? POCKET_BALLS : p == 4 ? POCKET_TMS : p == 5 ? POCKET_BERRIES : POCKET_ITEMS;	// (FireRed's order)
		};
		std::vector<uint8_t> itemPocket;
		if (src.hack)
		{
			out->itemMap.assign((size_t)std::max(x.numItems, 1), 0);
			itemPocket.assign(out->itemMap.size(), POCKET_ITEMS);
			for (int i = 1; i < x.numItems; i++)
			{
				const size_t a = (size_t)x.items + 44 * (size_t)i;
				const std::string nm = name(rom, n, (long)a, 14);
				const ItemName* g4 = placeholder(nm) ? nullptr : itemByName(nm);
				out->itemMap[i] = g4 ? g4->gen4 : (uint16_t)(GEN3_ONLY + i);
				itemPocket[i] = g4 ? g4->pocket : pocketOf(rom[a + 26]);
			}
		}
		auto item4 = [&](uint16_t i) -> uint16_t {
			if (!i) return 0;
			if (src.hack) return i < out->itemMap.size() ? out->itemMap[i] : (uint16_t)(GEN3_ONLY + i);
			return itemToGen4(i); };
		uint16_t top = 0;
		for (int i = 0; i < x.numItems; i++) top = std::max(top, item4((uint16_t)i));
		out->items.resize((size_t)top + 1);
		for (int i = 1; i < x.numItems; i++)
		{
			const size_t a = (size_t)x.items + 44 * (size_t)i;
			ItemData& it = out->items[item4((uint16_t)i)];
			if (src.hack && !it.name.empty()) continue;		// (two of a hack's items by one name: the first)
			it.name = name(rom, n, (long)a, 14);
			it.price = le16(rom + a + 16);
			it.description = text(rom, n, ptr(rom, n, a + 20), 200);
			if (src.hack) it.pocket = itemPocket[i];
			else if ((size_t)i < NUM_ITEMS3) it.pocket = ITEMS[i].pocket;
			else it.pocket = pocketOf(rom[a + 26]);
		}

		// moves (a hack's beyond the 354: as many as it names; the physical/special split, when its moves keep it in
		// byte 10 - 0 physical, 1 special, 2 status: the Glazed releases, CFRU)
		const int numMoves = src.hack ? (int)std::min<size_t>(t.moves.size(), (n - (size_t)x.moves) / 12) : 355;
		bool split = false;
		if (src.hack)
		{
			int k[3] = {};
			split = true;
			for (int m = 1; m < numMoves && split; m++) { uint8_t c = rom[x.moves + 12 * m + 10]; if (c > 2) split = false; else k[c]++; }
			split = split && k[1] > 20 && k[2] > 20;
		}
		out->moves.resize((size_t)numMoves);
		for (int m = 1; m < numMoves; m++)
		{
			const uint8_t* e = rom + x.moves + 12 * m;
			MoveData& d = out->moves[m];
			d.name = t.moves[m];
			d.effect = e[0]; d.power = e[1]; d.type = typeOf(e[2], src.fairy); d.accuracy = e[3]; d.pp = e[4]; d.effectChance = e[5];
			d.priority = e[7];
			// (Gen III: the category by the type - Normal to Steel physical, Fire to Dark special; no damage: status)
			if (split) d.cat = e[10] == 0 ? MoveCategory::PHYSICAL : e[10] == 1 ? MoveCategory::SPECIAL : MoveCategory::STATUS;
			else d.cat = d.power == 0 ? MoveCategory::STATUS : e[2] < 9 ? MoveCategory::PHYSICAL : MoveCategory::SPECIAL;
			if (x.moveDesc >= 0 && (size_t)m <= x.moveDescs) d.description = text(rom, n, ptr(rom, n, (size_t)x.moveDesc + 4 * (m - 1)), 200);
		}

		// abilities, natures
		out->abilities = t.abilities;
		out->abilityText.resize(out->abilities.size());
		for (size_t i = 0; x.abilityDesc >= 0 && i < out->abilities.size() && (src.hack || i < 78); i++)
			out->abilityText[i] = text(rom, n, ptr(rom, n, (size_t)x.abilityDesc + 4 * i), 200);
		for (int i = 0; x.natures >= 0 && i < 25; i++)
		{
			std::string nm = name(rom, n, ptr(rom, n, (size_t)x.natures + 4 * i), src.hack ? 32 : 16);
			if (src.hack && nm.find('(') != std::string::npos) nm.erase(nm.find('('));	// (its stats after the name: left out)
			out->natures.push_back(nm);
		}

		// TMs: Gen IV's numbers (TM01-50 = 1-50, HM01-08 = 93-100: tmNumber() in logic.cpp on Gen IV item ids)
		auto tmNumber = [](int k) { return k < 50 ? k + 1 : 93 + (k - 50); };
		out->tmMoves.assign(100, 0);
		for (int k = 0; k < 58; k++) out->tmMoves[(size_t)tmNumber(k) - 1] = le16(rom + x.tmMoves + 2 * k);

		// species
		for (size_t d = 1; d < out->species.size(); d++)
		{
			const uint16_t s = out->internal[d];
			if (!s || s >= nsp) continue;
			Species& sp = out->species[d];
			const uint8_t* b = rom + x.base + 28 * s;
			sp.name = names[s];
			sp.types[0] = typeOf(b[6], src.fairy); sp.types[1] = typeOf(b[7], src.fairy);
			for (int i = 0; i < 6; i++) sp.base[i] = b[i];
			sp.catchRate = b[8]; sp.baseExp = b[9];
			const uint16_t ev = le16(b + 10);
			for (int i = 0; i < 6; i++) sp.evYield[i] = (uint8_t)(ev >> (2 * i) & 3);
			sp.genderRatio = b[16]; sp.baseFriendship = b[18]; sp.growth = b[19];
			sp.abilities[0] = b[22]; sp.abilities[1] = b[23];
			// Pokédex: category[12], height, weight, the text's pointer (Ruby/Sapphire: two pages, 36 bytes an entry). A
			// form: its species' entry
			const size_t dn = d <= (size_t)dexSize ? d : src.hack && s < out->natOf.size() && out->natOf[s].second ? out->natOf[s].first : 0;
			if (t.dexEntries >= 0 && dn)
			{
				const size_t a = (size_t)t.dexEntries + dn * (size_t)t.dexStride;
				if (a + (size_t)t.dexStride <= n)
				{
					sp.category = name(rom, n, (long)a, 12);
					sp.height = le16(rom + a + 12); sp.weight = le16(rom + a + 14);
					sp.dexText = text(rom, n, ptr(rom, n, a + 16), 400);
					if (t.dexStride >= 36 && !src.hack)
					{
						std::string p2 = text(rom, n, ptr(rom, n, a + 20), 400);
						if (!p2.empty()) sp.dexText += (sp.dexText.empty() ? "" : " ") + p2;
					}
				}
			}
			// level-up moves: (move | level << 9), 0xffff at the end (a hack's: in any order - sorted here -, level 0 on
			// evolving, a filler of no move at 101)
			long l = ptr(rom, n, (size_t)t.learnsets + 4 * s, 2);
			for (int k = 0; l >= 0 && k < 64 && (size_t)l + 2 * k + 2 <= n; k++)
			{
				uint16_t e = le16(rom + l + 2 * k);
				if (e == 0xffff) break;
				if (!(e & 0x1ff) || (e >> 9) > 100) continue;
				sp.learnset.push_back({ (uint8_t)(e >> 9), (uint16_t)(e & 0x1ff) });
			}
			if (src.hack) std::stable_sort(sp.learnset.begin(), sp.learnset.end(), [](const LevelMove& p, const LevelMove& q) { return p.level < q.level; });
			// TMs and HMs
			const uint8_t* tm = rom + x.tmCompat + 8 * s;
			for (int k = 0; k < 58; k++)
				if (tm[k / 8] >> (k % 8) & 1) sp.tms.push_back((uint16_t)tmNumber(k));
		}
		// evolutions (after all species and items: their names). Gen III's methods 1-6 are Gen V's, from 7 one lower
		// (as Gen IV's: Gen V added a trade for a species at 7); item parameters as Gen IV ids. CFRU's own methods (16 a
		// species): 16 a move's type, 17 rain, 18 a Dark type in the party, 19/20 male/female level, 21 a move, 22 a
		// species in the party, 23 a map, 24/25 an item on a male/female; 254 Mega Evolution (a form: left out)
		auto zoneName = [&](int g, int m) -> std::string {
			if (g < (int)t.mapsecs.size() && m < (int)t.mapsecs[g].size())
			{
				int sec = t.mapsecs[g][m];
				if (sec != 255 && sec != t.mapsecNone) return t.mapName(sec);
			}
			return "";
		};
		for (size_t d = 1; d < out->species.size(); d++)
		{
			const uint16_t s = out->internal[d];
			if (!s || s >= nsp) continue;
			for (int k = 0; k < x.evoStride / 8; k++)
			{
				const uint8_t* e = rom + x.evo + (size_t)x.evoStride * s + 8 * k;
				uint16_t method = le16(e), param = le16(e + 2), into = nat(le16(e + 4));
				if (!method || !into || method == 0xffff) continue;
				// (a hack's Mega Evolutions by an item, there and back: forms, not evolutions)
				if (src.hack && (into == d || (s < out->natOf.size() && out->natOf[s].second && into == out->natOf[s].first))) continue;
				std::string how;
				if (method <= 15)
				{
					if (method >= 7) method++;
					if (method == evo::TRADE_ITEM || method == evo::USE_ITEM) param = item4(param);
				}
				else if (src.cfru)
				{
					auto lv = "Level " + std::to_string(param);
					switch (method)
					{
					case 16: method = 116; how = "Level up knowing a move of its new type"; break;
					case 17: method = 117; how = lv + " while it rains or is foggy"; break;
					case 18: method = 118; how = lv + " with a Dark-type Pokémon in the party"; break;
					case 19: method = evo::LEVEL_MALE; break;
					case 20: method = evo::LEVEL_FEMALE; break;
					case 21: method = evo::KNOWS_MOVE; break;
					case 22: method = evo::SPECIES_IN_PARTY; param = nat(param); break;
					case 23:
					{
						method = 123;
						std::string place = zoneName(param & 0xff, param >> 8);
						how = place.empty() ? "Level up in a special place" : "Level up in " + dsCase(place, true);
						break;
					}
					case 24: method = evo::USE_ITEM_MALE; param = item4(param); break;
					case 25: method = evo::USE_ITEM_FEMALE; param = item4(param); break;
					default: continue;	// (254: Mega Evolution; the rest unknown)
					}
				}
				else continue;
				Evolution ev{ method, param, into, how };
				if (ev.how.empty()) ev.how = evoHow(*out, base, method, param);
				out->species[d].evolutions.push_back(ev);
			}
		}

		// places: the met-location names (by region map section), then every map the map groups and the wild tables
		// know, named by its section, with its wild Pokémon
		out->locationNames = t.mapNames;
		if (out->locationNames.size() < 256) out->locationNames.resize(256);
		if (out->locationNames[0xfe].empty()) out->locationNames[0xfe] = "In-game trade";
		if (out->locationNames[0xff].empty()) out->locationNames[0xff] = "Fateful encounter";
		for (size_t g = 0; g < t.mapsecs.size(); g++)
			for (size_t m = 0; m < t.mapsecs[g].size(); m++)
			{
				const uint16_t z = (uint16_t)(g << 8 | m);
				out->zones[z].id = z;
				out->zones[z].name = zoneName((int)g, (int)m);
			}
		if (t.wildHeaders >= 0)
		{
			static const EncounterKind KIND[6] = { EncounterKind::GRASS, EncounterKind::SURF, EncounterKind::ROCK_SMASH,
				EncounterKind::OLD_ROD, EncounterKind::GOOD_ROD, EncounterKind::SUPER_ROD };
			for (size_t a = (size_t)t.wildHeaders; a + 20 <= n && rom[a] != 0xff; a += 20)
			{
				const int g = rom[a], m = rom[a + 1];
				const uint16_t z = (uint16_t)(g << 8 | m);
				Location& loc = out->zones[z];
				if (!loc.id) { loc.id = z; loc.name = zoneName(g, m); }
				if (!loc.encounters.empty()) continue;	// (a map's first table: the one the game uses)
				for (int k = 0; k < 6; k++)
					for (auto& w : t.wild(g, m, k))
					{
						Encounter e;
						e.species = nat(w.species);
						if (!e.species) continue;
						if (src.hack && w.species < out->natOf.size()) e.form = out->natOf[w.species].second;
						e.minLevel = w.minLv; e.maxLevel = w.maxLv; e.percent = (uint8_t)std::min(100, w.percent); e.kind = KIND[k];
						loc.encounters.push_back(e);
					}
			}
		}

		// trainers, by their index (the battle's opponent id)
		for (int i = 1; x.trainers >= 0 && i < x.numTrainers; i++)
		{
			const size_t a = (size_t)x.trainers + 40 * (size_t)i;
			Trainer tr;
			tr.id = (uint16_t)i;
			const uint8_t cls = rom[a + 1], flags = rom[a];
			if (x.classes >= 0 && cls < x.numClasses) tr.cls = name(rom, n, x.classes + 13 * (long)cls, 13);
			tr.name = name(rom, n, (long)a + 4, 12);
			tr.doubleBattle = le32(rom + a + 24) != 0;
			const int size = rom[a + 32], stride = flags & 1 ? 16 : 8;
			long p = ptr(rom, n, a + 36, (size_t)stride * size);
			for (int k = 0; p >= 0 && k < size; k++)
			{
				const uint8_t* e = rom + p + stride * k;
				TrainerMon m;
				m.level = (uint8_t)le16(e + 2);
				m.species = nat(le16(e + 4));
				size_t q = 6;
				if (flags & 2) { m.item = item4(le16(e + 6)); q = 8; }
				if (flags & 1) for (int j = 0; j < 4; j++) m.moves[j] = le16(e + q + 2 * j);
				else defaultMoves(*out, m);
				tr.team.push_back(m);
			}
			out->trainers[tr.id] = std::move(tr);
		}
		// the games keep their names in capitals ("BLAZIKEN", "ROUTE 101"): as the DS games write them (a CFRU hack's
		// mixed-case ones as they are)
		const bool h = src.hack;
		for (auto& sp : out->species) { sp.name = dsCase(sp.name, h); sp.category = dsCase(sp.category, h); }
		for (auto& m : out->moves) m.name = dsCase(m.name, h);
		for (auto& i : out->items) i.name = dsCase(i.name, h);
		for (auto& x : out->abilities) x = dsCase(x, h);
		for (auto& x : out->natures) x = dsCase(x, h);
		// Ruby and Sapphire name their villains' hideout "{EVIL_TEAM} HIDEOUT": the version's team
		for (auto& x : out->locationNames)
			if (x == " HIDEOUT" || x == "HIDEOUT") x = game.id == GameId::RUBY ? "MAGMA HIDEOUT" : game.id == GameId::SAPPHIRE ? "AQUA HIDEOUT" : x;
		for (auto& z : out->zones)
			if (z.second.name == " HIDEOUT" || z.second.name == "HIDEOUT") z.second.name = game.id == GameId::RUBY ? "MAGMA HIDEOUT" : game.id == GameId::SAPPHIRE ? "AQUA HIDEOUT" : z.second.name;
		for (auto& x : out->locationNames) x = dsCase(x, h);
		for (auto& z : out->zones) z.second.name = dsCase(z.second.name, h);
		for (auto& tr : out->trainers) { tr.second.cls = dsCase(tr.second.cls, h); tr.second.name = dsCase(tr.second.name, h); }
		fillRegionMaps(t, game, *out);	// (the Map page: the game's region maps and each zone's place on them)
		return out;
	}

	bool speciesOf(const RomData& r, uint16_t internal, uint16_t& national, uint8_t& form)
	{
		national = 0; form = 0;
		auto g = dynamic_cast<const Gen3Data*>(&r);
		if (!g) return false;
		if (g->src.hack)
		{
			if (internal >= g->natOf.size() || !g->natOf[internal].first) return false;
			national = g->natOf[internal].first; form = g->natOf[internal].second;
			return true;
		}
		for (size_t d = 1; d < g->internal.size(); d++) if (g->internal[d] == internal) { national = (uint16_t)d; return true; }
		return false;
	}

	uint16_t itemId(const RomData& r, uint16_t i)
	{
		auto g = dynamic_cast<const Gen3Data*>(&r);
		if (!i) return 0;
		if (!g || g->itemMap.empty()) return itemToGen4(i);
		return i < g->itemMap.size() ? g->itemMap[i] : (uint16_t)(GEN3_ONLY + i);
	}

	std::string nameCase(const std::string& s)
	{
		// each word's first letter kept, the rest in lower case; a word starts after a space, '-' or '.' ("Sand-Attack",
		// "S.S. Anne", "Mt. Moon"); HP, PP, TM/HM (and TM26), TV, UFO and Roman numerals stay as they are. Only ASCII
		// letters change ("POKéMON" -> "Pokémon")
		static const char* const KEEP[] = { "HP", "PP", "TV", "UFO", "II", "III", "IV", "EV", "SS", "PC", "TMS", "HMS" };
		std::string o = s;
		size_t i = 0;
		while (i < o.size())
		{
			size_t j = i;
			while (j < o.size() && o[j] != ' ' && o[j] != '-' && o[j] != '.') j++;
			const std::string w = o.substr(i, j - i);
			bool keep = false;
			for (const char* k : KEEP) keep = keep || w == k;
			if ((w.size() > 2 && (w.compare(0, 2, "TM") == 0 || w.compare(0, 2, "HM") == 0) && isdigit((unsigned char)w[2])) || w.size() <= 1) keep = true;
			if (!keep)
				for (size_t k = i + 1; k < j; k++)
					if (o[k] >= 'A' && o[k] <= 'Z') o[k] = (char)(o[k] - 'A' + 'a');
			i = j + 1;
		}
		return o;
	}
}
