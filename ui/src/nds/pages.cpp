#include "pages.h"
#include "../settings_draw.h"
#include "../lang.h"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <set>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

// The pages are drawn like Settings' (settings.cpp): rnds boxes cached as textures by kind and size, lines of text
// cached as textures by text and size, plain fills for bars, and the pixel pictures (balls, badges, gender signs) as
// runs of fills on the look's pixel grid. Positions are CSS px of 640x480, X() makes them device px.
namespace banana::nds
{
	using rnds::Color;

	namespace
	{
		// the companion's own pixel pictures ('#' the colour given, other letters as each caller says)
		const char* const PX_BALL[10] = { "...####...", ".##RRRR##.", ".#RRRRRR#.", "#RRR##RRR#", "####WW####",
			"####WW####", "#WWW##WWW#", ".#WWWWWW#.", ".##WWWW##.", "...####..." };
		const char* const PX_RING[10] = { "...####...", ".##....##.", ".#......#.", "#........#", "#........#",
			"#........#", "#........#", ".#......#.", ".##....##.", "...####..." };
		const char* const PX_MALE[9] = { ".....####", "......###", ".....##.#", "..####...", ".##..##..", "##....##.",
			"##....##.", ".##..##..", "..####..." };
		const char* const PX_FEMALE[9] = { "..###..", ".##.##.", "##...##", "##...##", ".##.##.", "..###..", "...#...",
			".#####.", "...#..." };
		// a badge of the badge case: an octagon with a shine
		const char* const PX_BADGE[8] = { "..####..", ".#HH###.", "#HH#####", "#H######", "########", "########",
			".######.", "..####.." };
		const char* const PX_CHECK[7] = { "......#", ".....##", "#...##.", "##.##..", ".###...", "..#....", "......." };
		// reference-only advice: a hollow diamond (from guides, not yet checked on a running game)
		const char* const PX_DIAMOND[7] = { "...#...", "..#.#..", ".#...#.", "#.....#", ".#...#.", "..#.#..", "...#..." };
		const char* const PX_SPARK[7] = { "...#...", "...#...", "..###..", "#######", "..###..", "...#...", "...#..." };
		const char* const PX_HEART[6] = { ".##.##.", "#######", "#######", ".#####.", "..###..", "...#..." };
		const char* const PX_HEART_EMPTY[6] = { ".##.##.", "#..#..#", "#.....#", ".#...#.", "..#.#..", "...#..." };
		const char* const PX_SKULL[7] = { ".#####.", "#######", "#..#..#", "#######", ".#####.", ".#.#.#.", "......." };
		const char* const PX_ARROW_R[7] = { "#......", "##.....", "###....", "####...", "###....", "##.....", "#......" };
		const char* const PX_UP[4] = { "..#..", ".###.", "#####", "....." }, * const PX_DOWN[4] = { "#####", ".###.", "..#..", "....." };

		const uint32_t TYPE_COLORS[NUM_TYPES] = { 0xa8a878, 0xc03028, 0xa890f0, 0xa040a0, 0xe0c068, 0xb8a038, 0xa8b820,
			0x705898, 0xb8b8d0, 0xf08030, 0x6890f0, 0x78c850, 0xf8d030, 0xf85888, 0x98d8d8, 0x7038f8, 0x705848 };
		const char* const TYPE_NAMES[NUM_TYPES] = { "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug",
			"Ghost", "Steel", "Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark" };
		const uint32_t HP_GREEN = 0x58d068, HP_YELLOW = 0xf8c030, HP_RED = 0xf05848, EXP_BLUE = 0x58a8f8, BADGE_RED = 0xe8433a;
		const float DEX_ROW = 36;	// the Dex list's rows (CSS px): a finger's width on the RG DS's 4" panels
		const uint32_t GOOD = 0x58d068, BAD = 0xf06858;

		const char* const STAT_NAMES[6] = { "HP", "Attack", "Defense", "Speed", "Sp. Atk", "Sp. Def" };	// (the personal data's order)
		const char* const STAT_SHORT[6] = { "HP", "Atk", "Def", "Spe", "SpA", "SpD" };

		std::string toUtf8(const std::u32string& u)
		{
			std::string o;
			for (char32_t ch : u)
			{
				if (ch < 0x80) o += (char)ch;
				else if (ch < 0x800) { o += (char)(0xc0 | (ch >> 6)); o += (char)(0x80 | (ch & 0x3f)); }
				else if (ch < 0x10000) { o += (char)(0xe0 | (ch >> 12)); o += (char)(0x80 | ((ch >> 6) & 0x3f)); o += (char)(0x80 | (ch & 0x3f)); }
				else { o += (char)(0xf0 | (ch >> 18)); o += (char)(0x80 | ((ch >> 12) & 0x3f)); o += (char)(0x80 | ((ch >> 6) & 0x3f)); o += (char)(0x80 | (ch & 0x3f)); }
			}
			return o;
		}
		std::string times(int q)	// a multiplier in quarters, as the games write it
		{
			// (decimals: the pixel font's ½ and ¼ read as 1/8 at chip size)
			return q == 0 ? "×0" : q == 1 ? "×0.25" : q == 2 ? "×0.5" : q == 4 ? "×1" : q == 8 ? "×2" : "×4";
		}
		const char* verdict(int q)
		{
			return q == 0 ? "No effect" : q < 4 ? "Not very effective" : q > 4 ? "Super effective" : "Normal hit";
		}
		std::string tr(const char* s) { return lang::tr(s); }
		std::string trType(Type t) { return t < NUM_TYPES ? lang::tr("Pokémon types", TYPE_NAMES[t]) : std::string(); }
		std::string playTime(uint32_t sec)
		{
			char b[32];
			snprintf(b, sizeof b, "%uh %02um", sec / 3600, (sec / 60) % 60);
			return b;
		}
		// the games' status word: sleep turns (bits 0-2), poison 3, burn 4, freeze 5, paralysis 6, bad poison 7
		const char* statusName(uint32_t st)
		{
			return (st & 7) ? "SLP" : (st & 0x80) ? "TOX" : (st & 8) ? "PSN" : (st & 0x10) ? "BRN" : (st & 0x20) ? "FRZ" : (st & 0x40) ? "PAR" : "";
		}
		uint32_t statusColour(const std::string& st)
		{
			return st == "SLP" ? 0x8890a0 : st == "PSN" || st == "TOX" ? 0xa050c0 : st == "BRN" ? 0xe07040 : st == "FRZ" ? 0x58b0d8 : st == "PAR" ? 0xc8a020 : 0x707070;
		}
		// the kinds whose Encounter.condition picks one of several lists (model.h), and that list's name
		bool listed(EncounterKind k) { return k == EncounterKind::HEADBUTT || k == EncounterKind::DUAL_SLOT || k == EncounterKind::SWARM; }
		const char* listName(EncounterKind k, uint8_t c)
		{
			if (k == EncounterKind::HEADBUTT) return c == 0 ? "common trees" : c == 1 ? "rare trees" : "the third tree list";
			if (k == EncounterKind::DUAL_SLOT)
				return c == 1 ? "with Ruby in slot 2" : c == 2 ? "with Sapphire in slot 2" : c == 3 ? "with Emerald in slot 2" :
					c == 4 ? "with FireRed in slot 2" : c == 5 ? "with LeafGreen in slot 2" : nullptr;
			if (k == EncounterKind::SWARM) return c == 1 ? "surfing" : c == 2 ? "fishing" : nullptr;
			return nullptr;
		}
		const char* kindName(EncounterKind k)
		{
			switch (k)
			{
			case EncounterKind::GRASS: return "Walking in grass";
			case EncounterKind::SURF: return "Surfing";
			case EncounterKind::OLD_ROD: return "Old Rod";
			case EncounterKind::GOOD_ROD: return "Good Rod";
			case EncounterKind::SUPER_ROD: return "Super Rod";
			case EncounterKind::ROCK_SMASH: return "Rock Smash";
			case EncounterKind::HEADBUTT: return "Headbutt";
			case EncounterKind::SWARM: return "Swarm";
			case EncounterKind::RADAR: return "Poké Radar";
			case EncounterKind::DUAL_SLOT: return "Dual-slot";
			case EncounterKind::SEASON: return "This season";
			case EncounterKind::GROTTO: return "Hidden Grotto";
			case EncounterKind::SPECIAL: return "Special";
			default: break;
			}
			// the kinds the ROM reader adds after SPECIAL (rom.h, asked for in model.h)
			switch ((int)k)
			{
			case 13: return "Dark grass";
			case 14: return "Shaking grass";
			case 15: return "Rippling water";
			case 16: return "Fishing in rippling water";
			case 17: return "Hoenn Sound";
			case 18: return "Sinnoh Sound";
			case 19: return "Fishing at night";
			default: return "Special";
			}
		}
		const char* timeName(TimeOfDay t) { return t == TimeOfDay::MORNING ? "Morning" : t == TimeOfDay::DAY ? "Day" : t == TimeOfDay::NIGHT ? "Night" : "Any time"; }
		const char* seasonName(uint8_t s) { static const char* const n[4] = { "Spring", "Summer", "Autumn", "Winter" }; return s < 4 ? n[s] : ""; }
		bool bit(const std::vector<uint8_t>& v, int n) { return n >= 0 && (size_t)(n >> 3) < v.size() && (v[n >> 3] >> (n & 7)) & 1; }
	}

	const char* typeName(Type t) { return t < NUM_TYPES ? TYPE_NAMES[t] : ""; }
	uint32_t typeColor(Type t) { return t < NUM_TYPES ? TYPE_COLORS[t] : 0x808080; }
	const char* pageName(Page p)
	{
		static const char* const n[(int)Page::COUNT] = { "Battle", "Party", "Team", "Here", "Guide", "Dex", "Log" };
		return p < Page::COUNT ? n[(int)p] : "";
	}
	uint32_t expForLevel(uint8_t growth, int n)
	{
		if (n <= 1) return 0;
		double c = (double)n * n * n;
		switch (growth)
		{
		case 1:	// erratic
			if (n <= 50) return (uint32_t)(c * (100 - n) / 50);
			if (n <= 68) return (uint32_t)(c * (150 - n) / 100);
			if (n <= 98) return (uint32_t)(c * ((1911 - 10 * n) / 3) / 500);
			return (uint32_t)(c * (160 - n) / 100);
		case 2:	// fluctuating
			if (n <= 15) return (uint32_t)(c * ((n + 1) / 3 + 24) / 50);
			if (n <= 36) return (uint32_t)(c * (n + 14) / 50);
			return (uint32_t)(c * (n / 2 + 32) / 50);
		case 3: return (uint32_t)std::max(0.0, 1.2 * c - 15.0 * n * n + 100.0 * n - 140);	// medium slow
		case 4: return (uint32_t)(4 * c / 5);	// fast
		case 5: return (uint32_t)(5 * c / 4);	// slow
		default: return (uint32_t)c;			// medium fast
		}
	}

	std::string goalWithPlace(const Milestone& m)
	{
		if (m.place.empty()) return m.name;
		auto low = [](std::string t) { std::transform(t.begin(), t.end(), t.begin(), ::tolower); return t; };
		const std::string name = low(m.name);
		std::string place = low(m.place);
		if (name.find(place) != std::string::npos) return m.name;
		// (a town named without its "City"/"Town": "Plasma Sage Rood in Driftveil" + "Driftveil City")
		for (const char* tail : { " city", " town" })
		{
			const size_t n = std::char_traits<char>::length(tail);
			if (place.size() > n + 2 && place.compare(place.size() - n, n, tail) == 0 && name.find(place.substr(0, place.size() - n)) != std::string::npos)
				return m.name;
		}
		return m.name + ", " + m.place;
	}

	// ---- the implementation ------------------------------------------------------------------------------------------
	struct Pages::Impl
	{
		Pages& o;
		rnds::Backend* be;
		int screen, W, H;
		float s;
		std::string assets;
		bool light;
		SPal p;
		std::shared_ptr<rnds::Font> R, M;
		PageData dt;
		float cornerX = 585, cornerY = 2, cornerW = 53, cornerH = 53;	// CSS px (the overlay's icon: top-right)
		// Back to game's plate (CSS px): it covers the icon's place, so the same spot opens and closes. With the icon in a
		// top corner it is the header row's right end (the headers leave it free); in a bottom one the dock's
		struct Area { float x0, y0, x1, y1; bool in(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; } };
		bool backTop() const { return cornerY + cornerH / 2 < 240; }
		Area backArea() const
		{
			const float x0 = std::min(512.0f, cornerX), x1 = std::max(628.0f, std::min(640.0f, cornerX + cornerW));
			// (a header button, inside the header's margins: the icon's place up to the panel's edges is in its taps)
			if (backTop()) return { x0, 6, 628, 50 };
			return { x0, std::min(424.0f, cornerY), x1, std::max(472.0f, std::min(480.0f, cornerY + cornerH)) };
		}

		Impl(Pages& owner, rnds::Backend* b, int sc, int w, int h, const std::string& a, bool l)
			: o(owner), be(b), screen(sc), W(w), H(h), s(w / 640.0f), assets(a), light(l)
		{
			loadFonts();
			p = pal(light);
		}
		~Impl()
		{
			if (!dec) return;
			{
				std::lock_guard<std::mutex> g(dec->m);
				dec->stop = true;
			}
			dec->cv.notify_all();
			dec->th.join();
		}

		// ---- caches -------------------------------------------------------------------------------------------------
		struct Cached { rnds::TexPtr tex; int ox = 0, oy = 0; float w = 0; uint64_t used = 0; bool drawn = false; };
		std::unordered_map<std::string, Cached> cache;
		uint64_t frame = 1;
		struct SprTex { rnds::TexPtr tex; int w = 0, h = 0; uint64_t used = 0; };	// a sprite frame cropped to its picture
		std::unordered_map<std::string, SprTex> sprites;

		// the pictures' sources: a species' decoded Sprite and the union of its frames (bounds()), asked of the ROM once
		// per species, not on every draw. (RomData keeps only its last 4: a page showing more species decoded each of
		// them again on every draw, and walked every frame of a Gen V animation for the bounds - Party 626 ms a picture
		// with a Black 2 save on the RG DS Plus.) The Sprites (Gen V: ~0.5 MB each, up to 1.8) are let go of, least
		// recently drawn first, past SPR_KEEP bytes; their bounds and drawn pictures stay.
		// With the decoder thread on (setDecoderThread: the overlay's pages), a species not decoded yet is decoded there
		// and drawn as a ring until it is: the paint loop never waits on a decode (a Gen V picture with its animation
		// is tens of ms on the handhelds)
		struct SprSrc
		{
			std::shared_ptr<const Sprite> spr;
			int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0, frames = 0;
			std::vector<Sprite::Step> anim;
			bool missing = false;
			uint64_t used = 0;
			size_t bytes = 0;
		};
		static constexpr size_t SPR_KEEP = 24u << 20;
		std::unordered_map<uint32_t, SprSrc> sprSrc;
		size_t sprBytes = 0;
		const RomData* sprRom = nullptr;
		static uint32_t sprKey(uint16_t sp, bool female, uint8_t form) { return (uint32_t)sp << 9 | (uint32_t)form << 1 | (female ? 1u : 0u); }
		static SprSrc decodeSrc(const RomData& rom, uint32_t key)
		{
			SprSrc r;
			r.spr = rom.sprite((uint16_t)(key >> 9), key & 1, (uint8_t)(key >> 1));
			if (!r.spr || r.spr->frames.empty() || r.spr->w <= 0) { r.spr = nullptr; r.missing = true; return r; }
			bounds(*r.spr, r.bx0, r.by0, r.bx1, r.by1);
			r.frames = (int)r.spr->frames.size();
			r.anim = r.spr->anim;
			for (auto& f : r.spr->frames) r.bytes += f.size();
			return r;
		}
		struct Decoder
		{
			std::thread th;
			std::mutex m;
			std::condition_variable cv;
			struct Job { uint32_t key; std::shared_ptr<const RomData> rom; };
			std::deque<Job> q;
			struct Done { uint32_t key; const RomData* rom; SprSrc src; };
			std::vector<Done> done;
			std::set<uint32_t> asked;	// (queued or being decoded: asked once)
			bool stop = false;
		};
		std::unique_ptr<Decoder> dec;
		void startDecoder()
		{
			if (dec) return;
			dec = std::make_unique<Decoder>();
			Decoder* d = dec.get();
			dec->th = std::thread([d]
			{
				// (below the game's threads and the paint loop: it fills in pictures, nothing waits on it)
				setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);
				std::unique_lock<std::mutex> g(d->m);
				for (;;)
				{
					d->cv.wait(g, [d] { return d->stop || !d->q.empty(); });
					if (d->stop) return;
					Decoder::Job j = d->q.front();
					d->q.pop_front();
					g.unlock();
					SprSrc r = decodeSrc(*j.rom, j.key);
					g.lock();
					d->done.push_back({ j.key, j.rom.get(), std::move(r) });
				}
			});
		}
		// the ROM changed (the pages' first data, another game): nothing of the last one is kept
		void sprFor(const RomData* rom)
		{
			if (rom == sprRom) return;
			sprRom = rom;
			std::fill(std::begin(warmed), std::end(warmed), false);
			sprSrc.clear();
			sprites.clear();
			sprBytes = 0;
			if (!dec) return;
			std::lock_guard<std::mutex> g(dec->m);
			dec->q.clear();
			dec->asked.clear();
		}
		// a species' picture to decode on the thread; now: wanted on the screen (before the ones asked ahead)
		void ask(uint32_t key, bool now)
		{
			if (!dec || !dt.rom) return;
			{
				std::lock_guard<std::mutex> g(dec->m);
				if (!dec->asked.insert(key).second)
				{
					if (now)	// (asked ahead, still waiting: to the front)
						for (auto it = dec->q.begin(); it != dec->q.end(); ++it)
							if (it->key == key) { Decoder::Job j = *it; dec->q.erase(it); dec->q.push_front(j); break; }
					return;
				}
				if (now) dec->q.push_front({ key, dt.rom });
				else dec->q.push_back({ key, dt.rom });
			}
			dec->cv.notify_one();
		}
		void keep(uint32_t key, SprSrc&& r)
		{
			auto it = sprSrc.find(key);
			if (it != sprSrc.end()) sprBytes -= it->second.spr ? it->second.bytes : 0;
			r.used = it != sprSrc.end() ? it->second.used : frame;
			sprBytes += r.spr ? r.bytes : 0;
			sprSrc[key] = std::move(r);
		}
		// the thread's decoded pictures in: true when any came (the page is drawn again)
		bool takeDecoded()
		{
			if (!dec) return false;
			std::vector<Decoder::Done> got;
			{
				std::lock_guard<std::mutex> g(dec->m);
				got.swap(dec->done);
				for (auto& d : got) dec->asked.erase(d.key);
			}
			bool any = false;
			for (auto& d : got)
				if (d.rom == sprRom) { keep(d.key, std::move(d.src)); any = true; }
			return any;
		}
		// a species' source (null: not decoded yet, asked of the thread)
		SprSrc* src(uint16_t sp, bool female, uint8_t form)
		{
			sprFor(dt.rom.get());
			const uint32_t key = sprKey(sp, female, form);
			auto it = sprSrc.find(key);
			if (it == sprSrc.end())
			{
				if (dec) { ask(key, true); return nullptr; }
				keep(key, decodeSrc(*dt.rom, key));
				it = sprSrc.find(key);
			}
			it->second.used = frame;
			return &it->second;
		}
		// past SPR_KEEP: the least recently drawn Sprites go (none drawn in the last 16 frames)
		void trimSprites()
		{
			if (sprBytes <= SPR_KEEP) return;
			std::vector<std::pair<uint64_t, uint32_t>> old;
			for (auto& e : sprSrc)
				if (e.second.spr && e.second.used + 16 < frame) old.push_back({ e.second.used, e.first });	// (a warming round draws all seven)
			std::sort(old.begin(), old.end());
			for (auto& o2 : old)
			{
				if (sprBytes <= SPR_KEEP * 2 / 3) break;
				SprSrc& e = sprSrc[o2.second];
				sprBytes -= e.bytes;
				e.spr = nullptr;
			}
		}
		// the species the pages will show (the party, the battle, this place's wild ones, roamers, the day care), decoded
		// ahead on the thread
		void askAhead()
		{
			if (!dec || !dt.rom || !dt.live || !dt.live->valid) return;
			sprFor(dt.rom.get());
			auto want = [&](uint16_t sp, bool female, uint8_t form)
			{
				if (!sp || sp > dexSize()) return;
				const uint32_t key = sprKey(sp, female, form);
				if (!sprSrc.count(key)) ask(key, false);
			};
			const Live& lv = *dt.live;
			for (auto& m : lv.party) want(m.species, m.female, m.form);
			for (auto& b : lv.battle.foes) if (b.present) want(b.mon.species, b.mon.female, b.mon.form);
			for (auto& m : lv.battle.foeParty) want(m.species, m.female, m.form);
			auto z = dt.rom->zones.find(lv.zone);
			if (z != dt.rom->zones.end())
				for (auto& e : z->second.encounters) want(e.species, false, 0);
			for (auto& r : lv.roamers) if (r.active) want(r.species, false, 0);
			for (auto& m : lv.daycare) want(m.species, m.female, m.form);
		}

		void loadFonts()
		{
			const auto& f = rnds::themeFiles;
			R = rnds::Font::load(f.fontRegular.empty() ? assets + "/fonts/PixelifySans-Regular.ttf" : f.fontRegular);
			M = rnds::Font::load(f.fontMedium.empty() ? assets + "/fonts/PixelifySans-Medium.ttf" : f.fontMedium);
		}
		float X(float v) const { return v * s; }
		float px() const { return std::max(1.0f, std::round(s)); }	// one pixel of the look, device px
		int k() const { return std::max(1, (int)std::lround(2 * s)); }	// the pixel pictures' pixel: 2 (640), 3 (1024)

		// ---- what is visible: the scroll area being drawn clips everything (and anything wholly outside is skipped)
		float clipY0 = 0, clipY1 = 480;
		bool visible(float y, float h) const { return y + h > clipY0 && y < clipY1; }

		void fill(float x, float y, float w, float h, const Color& c)
		{
			if (!visible(y, h) || w <= 0 || h <= 0) return;
			float x0 = std::round(X(x)), y0 = std::round(X(y));
			be->fill(x0, y0, std::max(1.0f, std::round(X(x + w)) - x0), std::max(1.0f, std::round(X(y + h)) - y0), c);
		}
		// an outline inside the rectangle, t device px thick
		void outline(float x, float y, float w, float h, float t, const Color& c)
		{
			if (!visible(y, h)) return;
			float x0 = std::round(X(x)), y0 = std::round(X(y)), x1 = std::round(X(x + w)), y1 = std::round(X(y + h));
			be->fill(x0, y0, x1 - x0, t, c); be->fill(x0, y1 - t, x1 - x0, t, c);
			be->fill(x0, y0, t, y1 - y0, c); be->fill(x1 - t, y0, t, y1 - y0, c);
		}
		void pixels(const char* const* rows, int h, float x, float y, int kk, const std::function<Color(char)>& colour)
		{
			if (!visible(y, h * kk / s)) return;
			float x0 = std::round(X(x)), y0 = std::round(X(y));
			for (int r = 0; r < h; r++)
				for (int c = 0; rows[r][c]; c++)
					if (rows[r][c] != '.')
					{
						// (runs of one colour as one fill)
						int e = c;
						while (rows[r][e + 1] == rows[r][c]) e++;
						be->fill(x0 + c * kk, y0 + r * kk, (float)(e - c + 1) * kk, (float)kk, colour(rows[r][c]));
						c = e;
					}
		}

		void box(const std::string& kind, float x, float y, float w, float h, uint32_t tint = 0)
		{
			if (!visible(y - 6, h + 12)) return;
			int dw = (int)std::lround(X(w)), dh = (int)std::lround(X(h));
			std::string key = "box:" + kind + ":" + std::to_string(tint) + ":" + std::to_string(dw) + "x" + std::to_string(dh);
			auto it = cache.find(key);
			if (it == cache.end())
			{
				rnds::BoxStyle st;
				auto inset = [](float dx, float dy, float sp, Color c) { return rnds::Shadow{ dx, dy, sp, c, true }; };
				auto outer = [](float dx, float dy, Color c) { return rnds::Shadow{ dx, dy, 0, c, false }; };
				float b3 = s == 1 ? 3 : std::max(1.0f, std::floor(3 * s)), b2 = std::max(1.0f, std::round(2 * s));
				st.ditherScale = s; st.ditherA = p.ditherA; st.ditherB = p.ditherB;
				if (kind == "screen")
				{
					st.radius = X(2); st.bg = p.screenBg; st.dither = rnds::themeFiles.dither;
					st.shadows = { inset(0, 0, X(4), p.screenFrame), inset(0, X(8), 0, p.screenBand) };
				}
				else if (kind == "bubble")	// a page's panel: the cream edge, the band, the lip
				{
					st.radius = X(12); st.border = b3; st.borderColor = p.line; st.bg = p.bubbleBody;
					st.bands = { rnds::Band{ 0, X(10), p.bubbleBand }, rnds::Band{ X(10), -1, p.bubbleBody } };
					st.shadows = { inset(0, -X(6), 0, p.bubbleLow), outer(X(4), X(4), p.blackish) };
				}
				else if (kind == "hero")	// the panel that matters most on a page: the cream (theme's edge) border
				{
					st.radius = X(14); st.border = b3; st.borderColor = p.edge; st.bg = p.panelBody;
					st.bands = { rnds::Band{ 0, X(10), p.panelBand }, rnds::Band{ X(10), -1, p.panelBody } };
					st.shadows = { inset(0, -X(6), 0, p.panelLow), outer(X(4), X(4), p.shade45) };
				}
				else if (kind == "best")	// the best of several (the move to use, the first catch, what's new): the accent's edge
				{
					st.radius = X(14); st.border = b3; st.borderColor = p.accent; st.bg = p.panelBody;
					st.bands = { rnds::Band{ 0, X(10), p.panelBand }, rnds::Band{ X(10), -1, p.panelBody } };
					st.shadows = { inset(0, -X(6), 0, p.panelLow), outer(X(4), X(4), p.shade45) };
				}
				else if (kind == "slot")	// a row of a list
				{
					st.radius = X(10); st.border = b2; st.borderColor = p.line; st.bg = p.panelBody;
					st.bands = { rnds::Band{ 0, X(6), p.panelBand }, rnds::Band{ X(6), -1, p.panelBody } };
					st.shadows = { inset(0, -X(4), 0, p.panelLow) };
				}
				else if (kind == "pick")	// the selected row, the active tab: the accent's plate
				{
					st.radius = X(10); st.border = b2; st.borderColor = p.pickEdge; st.bg = p.pick;
					st.bands = { rnds::Band{ 0, X(6), p.pickBand }, rnds::Band{ X(6), -1, p.pick } };
					st.shadows = { inset(0, -X(4), 0, p.pickLow), outer(X(2), X(2), p.shade45) };
				}
				else if (kind == "back")	// Back to game: the accent's plate, bigger lip
				{
					st.radius = X(14); st.border = b3; st.borderColor = p.pickEdge; st.bg = p.pick;
					st.bands = { rnds::Band{ 0, X(10), p.pickBand }, rnds::Band{ X(10), -1, p.pick } };
					st.shadows = { inset(0, -X(6), 0, p.pickLow), outer(X(4), X(4), p.shade45) };
				}
				else if (kind == "stage")	// where a Pokémon stands: the screen's dots over its type's colour
				{
					Color base = light ? p.screenBand : p.screenBg;
					st.radius = X(10); st.bg = rnds::mix(Color::hex(tint), light ? 0.22f : 0.20f, base); st.dither = true;
					st.shadows = { inset(0, -X(8), 0, rnds::mix(Color::hex(tint), light ? 0.32f : 0.10f, light ? p.panelLow : p.screenFrame)) };
				}
				else if (kind == "chip")	// a type's chip, a small label on colour
				{
					st.radius = X(5); st.bg = Color::hex(tint);
					st.shadows = { inset(0, -X(2), 0, Color(0, 0, 0, 0.22f)) };
				}
				else if (kind == "well")	// a quiet sunken area (the switch's track, an empty place)
				{
					st.radius = X(8); st.bg = light ? p.screenFrame : p.screenFrame; st.border = std::max(1.0f, std::round(s));
					st.borderColor = rnds::mix(p.line, 0.5f, p.screenBg);
				}
				rnds::PaintedBox pb = rnds::paintBox(st, dw, dh);
				Cached c;
				c.tex = be->upload(pb.bmp, true);
				c.ox = pb.originX; c.oy = pb.originY;
				it = cache.emplace(key, c).first;
			}
			it->second.used = frame;
			const Cached& c = it->second;
			be->draw(c.tex, std::round(X(x)) - c.ox, std::round(X(y)) - c.oy, (float)c.tex->w, (float)c.tex->h, Color(1, 1, 1, 1));
		}

		// a line of text. raw: the game's own text (names from the ROM), not looked up in the menu's translations.
		// maxW > 0: cut with an ellipsis to fit. Returns its width (CSS px)
		float text(const std::string& t, bool medium, float size, float x, float top, const Color& c, bool raw = false,
			float maxW = 0, bool measureOnly = false)
		{
			if (t.empty()) return 0;
			// a name ending in the male or female sign (NIDORAN♀): the sign drawn in pixels, the fonts have none
			if (raw && t.size() > 3 && (!t.compare(t.size() - 3, 3, "♂") || !t.compare(t.size() - 3, 3, "♀")))
			{
				bool male = !t.compare(t.size() - 3, 3, "♂");
				int kk = std::max(1, (int)std::lround(s * size / 11));
				float sw = (male ? 9.0f : 7.0f) * kk / s + 3;
				float w = text(t.substr(0, t.size() - 3), medium, size, x, top, c, raw, maxW > 0 ? maxW - sw : 0, measureOnly);
				if (!measureOnly) pixels(male ? PX_MALE : PX_FEMALE, 9, x + w + 3, top + size * 0.2f, kk, [&](char) { return c; });
				return w + sw;
			}
			rnds::TextStyle st;
			st.font = medium ? M : R; st.size = size;
			if (!st.font) return 0;
			int mw = maxW > 0 ? (int)maxW : 0;
			std::string key = "txt:" + t + (medium ? "|m" : "|r") + std::to_string(size) + (raw ? "|raw" : "") + "|" + std::to_string(mw);
			auto it = cache.find(key);
			bool fresh = it == cache.end();
			if (fresh) it = cache.emplace(key, Cached()).first;
			Cached& cc = it->second;
			cc.used = frame;
			if (!measureOnly && !visible(top - 4, size * 1.6f + 8)) return cc.w > 0 || !fresh ? cc.w : measureLine(t, st, raw, mw, cc);
			if (fresh || (!measureOnly && !cc.drawn))
			{
				rnds::TextLine l = layout(t, st, raw, mw);
				cc.w = l.width;
				if (!measureOnly)
				{
					float asc = st.font->ascent(size);
					rnds::TextBitmap tb = rnds::renderLine(l, st, s, asc);
					if (!tb.bmp.empty()) cc.tex = be->upload(tb.bmp, true);
					cc.ox = tb.padX; cc.oy = tb.padY;
					cc.drawn = true;
				}
			}
			if (!measureOnly && cc.tex)
				be->draw(cc.tex, std::floor(X(x)) - cc.ox, std::floor(X(top)) - cc.oy, (float)cc.tex->w, (float)cc.tex->h, c);
			return cc.w;
		}
		float measureLine(const std::string& t, const rnds::TextStyle& st, bool raw, int mw, Cached& cc)
		{
			cc.w = layout(t, st, raw, mw).width;
			return cc.w;
		}
		rnds::TextLine layout(const std::string& t, const rnds::TextStyle& st, bool raw, int mw)
		{
			rnds::Translator tf = rnds::translator();
			if (raw) rnds::setTranslator(nullptr);
			rnds::TextLine l = mw > 0 ? rnds::layoutEllipsis(rnds::utf8to32(t), st, mw) : rnds::layoutLine(rnds::utf8to32(t), st);
			rnds::setTranslator(tf);
			return l;
		}
		float measure(const std::string& t, bool medium, float size, bool raw = false)
		{
			return text(t, medium, size, 0, 0, Color(), raw, 0, true);
		}
		// right-aligned at x
		float textR(const std::string& t, bool medium, float size, float x, float top, const Color& c, bool raw = false)
		{
			float w = measure(t, medium, size, raw);
			text(t, medium, size, x - w, top, c, raw);
			return w;
		}
		// wrapped at spaces; returns the height it took (CSS px)
		// the lines a text wraps to (laid out once, kept with the texts)
		const std::vector<std::string>& lines(const std::string& t, float size, float width, int maxLines, bool raw, bool medium)
		{
			static const std::vector<std::string> none;
			if (t.empty() || !R) return none;
			std::string key = "wrap:" + t + "|" + std::to_string(size) + "|" + std::to_string((int)width) + "|" + std::to_string(maxLines) + (raw ? "r" : "") + (medium ? "m" : "");
			auto it = cache.find(key);
			if (it == cache.end())
			{
				rnds::TextStyle st;
				st.font = medium ? M : R; st.size = size;
				rnds::Translator tf = rnds::translator();
				if (raw) rnds::setTranslator(nullptr);
				std::vector<rnds::TextLine> ls = rnds::layoutWrap(rnds::utf8to32(t), st, (int)width, maxLines);
				rnds::setTranslator(tf);
				it = cache.emplace(key, Cached()).first;
				auto& v = wrapLines[key];
				v.clear();
				for (auto& l : ls) v.push_back(toUtf8(l.text));
			}
			it->second.used = frame;
			return wrapLines[key];
		}
		float lineHeight(float size, float lineH) const { return lineH > 0 ? lineH : std::round(size * 1.45f); }
		// the height a wrapped text takes (wrap() draws it)
		float wrapHeight(const std::string& t, float size, float width, int maxLines, bool raw = false, bool medium = false, float lineH = 0)
		{
			return lines(t, size, width, maxLines, raw, medium).size() * lineHeight(size, lineH);
		}
		// wrapped at spaces; returns the height it took (CSS px)
		float wrap(const std::string& t, float size, float x, float top, float width, int maxLines, const Color& c,
			bool raw = false, bool medium = false, float lineH = 0)
		{
			const auto& ls = lines(t, size, width, maxLines, raw, medium);
			float lh = lineHeight(size, lineH);
			for (size_t i = 0; i < ls.size(); i++) text(ls[i], medium, size, x, top + i * lh, c, true);	// (translated already)
			return ls.size() * lh;
		}
		std::unordered_map<std::string, std::vector<std::string>> wrapLines;

		// ---- small pieces ----------------------------------------------------------------------------------------
		float chip(const std::string& label, uint32_t rgb, float x, float y, bool raw = true, bool measureOnly = false, float h = 18)
		{
			float w = std::round(measure(label, true, 12, raw) + 14);
			if (measureOnly) return w;
			box("chip", x, y, w, h, rgb);
			text(label, true, 12, x + 7, y + (h - 15) / 2, Color::hex(rnds::inkOn(rgb)), raw);
			return w;
		}
		float typeChip(Type t, float x, float y, bool measureOnly = false, float h = 18)
		{
			if (t >= NUM_TYPES) return 0;
			return chip(trType(t), TYPE_COLORS[t], x, y, true, measureOnly, h);
		}
		// both of a species' types (one when they're the same); returns the right end
		float typeChips(const Type* ts, float x, float y)
		{
			x += typeChip(ts[0], x, y) + 5;
			if (ts[1] != ts[0]) x += typeChip(ts[1], x, y) + 5;
			return x;
		}
		void bar(float x, float y, float w, float h, float f, const Color& c, const Color* track = nullptr)
		{
			fill(x, y, w, h, track ? *track : barTrack());
			f = std::max(0.0f, std::min(1.0f, f));
			if (f > 0) fill(x, y, std::max(1.0f / s, w * f), h, c);
		}
		Color good() const { return Color::hex(light ? 0x2c8a3c : GOOD); }
		Color warn() const { return Color::hex(light ? 0xa86c00 : HP_YELLOW); }
		Color bad() const { return Color::hex(light ? 0xc23a2c : BAD); }
		Color barTrack() const { return light ? rnds::mix(p.line, 0.45f, p.screenBand) : rnds::mix(p.line, 0.28f, p.screenBg); }
		// the HP bar: blocks, coloured by how much is left
		void hpBar(int hp, int maxHp, float x, float y, float w, float h = 10)
		{
			float f = maxHp > 0 ? std::min(1.0f, (float)hp / maxHp) : 0;
			uint32_t rgb = f > 0.5f ? HP_GREEN : f > 0.2f ? HP_YELLOW : HP_RED;
			int blocks = std::max(4, (int)(w / 9));
			float gap = 2, bw = (w - (blocks - 1) * gap) / blocks;
			int on = hp > 0 ? std::max(1, (int)std::lround(f * blocks)) : 0;
			for (int i = 0; i < blocks; i++)
				fill(x + i * (bw + gap), y, bw, h, i < on ? Color::hex(rgb) : barTrack());
		}
		void ball(float x, float y, int state, int kk)	// 0 a ring (seen), 1 caught, 2 fainted
		{
			if (state == 0) { pixels(PX_RING, 10, x, y, kk, [&](char) { return p.line; }); return; }
			pixels(PX_BALL, 10, x, y, kk, [&](char c)
				{ return c == '#' ? Color::hex(0x2a2830) : c == 'R' ? Color::hex(state == 2 ? 0x8a8890 : 0xe8504a) : Color::hex(state == 2 ? 0xb8b4b0 : 0xf4f0e8); });
		}
		void unchecked(float x, float y)	// the reference-only mark
		{
			pixels(PX_DIAMOND, 7, x, y, std::max(1, (int)std::lround(s * 1.5f)), [&](char) { return p.muted; });
		}
		// a page section's heading (and a quieter line after it); returns where its content starts
		float heading(const std::string& t, float x, float y, const std::string& sub = "", bool raw = false)
		{
			float w = text(t, true, 15, x, y, p.ink, raw);
			if (!sub.empty()) text(sub, false, 12, x + w + 10, y + 3, p.muted);
			return y + 24;
		}
		// a name with its gender sign (the game's names: raw)
		float monName(const std::string& n, int gender, float size, float x, float y, float maxW, const Color& c)
		{
			int kk = std::max(1, (int)std::lround(s * size / 11));
			float gw = gender < 2 ? 9.0f * kk / s + 6 : 0;
			float w = text(n, true, size, x, y, c, true, maxW - gw);
			if (gender == 0) pixels(PX_MALE, 9, x + w + 5, y + size * 0.2f, kk, [](char) { return Color::hex(0x58a8f8); });
			if (gender == 1) pixels(PX_FEMALE, 9, x + w + 5, y + size * 0.2f, kk, [](char) { return Color::hex(0xf07890); });
			return w + gw;
		}

		// ---- the game's data, guarded ------------------------------------------------------------------------------
		const Species* species(uint16_t n) const
		{
			return dt.rom && n > 0 && n < dt.rom->species.size() ? &dt.rom->species[n] : nullptr;
		}
		std::string speciesName(uint16_t n) const
		{
			const Species* sp = species(n);
			return sp && !sp->name.empty() ? sp->name : "#" + std::to_string(n);
		}
		std::string monLabel(const Mon& m) const { return m.egg ? tr("Egg") : !m.nick.empty() ? m.nick : speciesName(m.species); }
		const MoveData* move(uint16_t n) const { return dt.rom && n > 0 && n < dt.rom->moves.size() ? &dt.rom->moves[n] : nullptr; }
		std::string moveName(uint16_t n) const { const MoveData* m = move(n); return m ? m->name : "-"; }
		std::string itemName(uint16_t n) const
		{
			return dt.rom && n > 0 && n < dt.rom->items.size() ? dt.rom->items[n].name : std::string();
		}
		std::string abilityName(uint16_t n) const
		{
			return dt.rom && n > 0 && n < dt.rom->abilities.size() ? dt.rom->abilities[n] : std::string();
		}
		std::string zoneName(uint16_t z) const
		{
			if (!dt.rom) return {};
			auto it = dt.rom->zones.find(z);
			return it != dt.rom->zones.end() ? it->second.name : std::string();
		}
		Type monType(const Mon& m, int i) const { const Species* sp = species(m.species); return sp ? sp->types[i] : NO_TYPE; }
		int gender(const Mon& m) const
		{
			const Species* sp = species(m.species);
			if (!sp || sp->genderRatio == 255 || m.egg) return 2;
			return m.female ? 1 : 0;
		}
		// (Live's Pokédex bit sets keep species n at bit n - 1, as the games do: ram.h)
		bool caught(uint16_t n) const { return dt.live && n && bit(dt.live->dexCaught, n - 1); }
		bool seen(uint16_t n) const { return dt.live && n && (bit(dt.live->dexSeen, n - 1) || bit(dt.live->dexCaught, n - 1)); }
		int dexSize() const { return dt.rom ? dt.rom->game.dexSize : 493; }

		// ---- sprites -------------------------------------------------------------------------------------------------
		double now = 0;
		double animNext = -1;	// when a sprite drawn in the last frame next changes its frame (-1 none)
		static int frameAt(const SprSrc& sp, double t, double* next)
		{
			if (sp.anim.empty()) return 0;
			uint32_t total = 0;
			for (auto& st : sp.anim) total += std::max<uint16_t>(st.ms, 1);
			double at = std::fmod(t, (double)total), acc = 0;
			for (auto& st : sp.anim)
			{
				acc += std::max<uint16_t>(st.ms, 1);
				if (at < acc) { if (next) *next = t + (acc - at); return std::min<int>(st.frame, sp.frames - 1); }
			}
			return 0;
		}
		// the union of every frame's picture (so the animation doesn't wander as frames change)
		static void bounds(const Sprite& sp, int& x0, int& y0, int& x1, int& y1)
		{
			x0 = sp.w; y0 = sp.h; x1 = 0; y1 = 0;
			for (auto& f : sp.frames)
				for (int y = 0; y < sp.h; y++)
					for (int x = 0; x < sp.w; x++)
						if ((size_t)(y * sp.w + x) < f.size() && f[y * sp.w + x])
						{
							x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x + 1); y1 = std::max(y1, y + 1);
						}
			if (x1 <= x0) { x0 = y0 = 0; x1 = sp.w; y1 = sp.h; }
		}
		// a species' picture centred in the box (x, y, w, h); look: 0 normal, 1 shiny, 2 a silhouette (not seen yet).
		// Whole-pixel scaling when it fits at 1x or more, else made smaller smoothly. animate: its frames (the Sprite
		// steps), else the first frame
		void mon(uint16_t sp, bool female, uint8_t form, int look, float x, float y, float w, float h, bool animate,
			float lift = 0)
		{
			if (!visible(y, h) || !dt.rom || !sp) return;
			auto ring = [&] { pixels(PX_RING, 10, x + w / 2 - 5.0f * k() / s, y + h / 2 - 5.0f * k() / s, k(), [&](char) { return p.line; }); };
			SprSrc* sc = src(sp, female, form);
			if (!sc || sc->missing) { ring(); return; }
			int fr = 0;
			if (animate)
			{
				double nx = -1;
				fr = frameAt(*sc, now, &nx);
				if (nx > 0 && (animNext < 0 || nx < animNext)) animNext = nx;
			}
			const int bx0 = sc->bx0, by0 = sc->by0, bx1 = sc->bx1, by1 = sc->by1;
			int bw = bx1 - bx0, bh = by1 - by0;
			float dw = X(w), dh = X(h);
			float kf = std::min(dw / bw, dh / bh);
			int ow, oh;
			if (kf >= 1) { int kk = (int)kf; ow = bw * kk; oh = bh * kk; }
			else { ow = std::max(1, (int)(bw * kf)); oh = std::max(1, (int)(bh * kf)); }
			std::string key = std::to_string(sp) + (female ? "f" : "m") + std::to_string(form) + ":" + std::to_string(look) + ":" + std::to_string(fr) +
				(kf >= 1 ? std::string(":n") : ":" + std::to_string(ow) + "x" + std::to_string(oh));
			auto it = sprites.find(key);
			if (it == sprites.end())
			{
				if (!sc->spr)	// (let go of: decoded again)
				{
					if (dec) { ask(sprKey(sp, female, form), true); ring(); return; }
					keep(sprKey(sp, female, form), decodeSrc(*dt.rom, sprKey(sp, female, form)));
					sc = &sprSrc[sprKey(sp, female, form)];
					if (!sc->spr) { ring(); return; }
				}
				const std::shared_ptr<const Sprite> spr = sc->spr;
				rnds::Bitmap b;
				b.w = bw; b.h = bh; b.px.assign((size_t)bw * bh * 4, 0);
				const auto& f = spr->frames[fr];
				const auto& palette = look == 1 ? spr->shiny : spr->normal;
				Color sil = light ? rnds::mix(p.ink, 0.55f, p.screenBand) : rnds::mix(p.line, 0.45f, p.screenBg);
				for (int yy = 0; yy < bh; yy++)
					for (int xx = 0; xx < bw; xx++)
					{
						size_t at = (size_t)(yy + by0) * spr->w + xx + bx0;
						uint8_t ci = at < f.size() ? f[at] : 0;
						if (!ci) continue;
						uint32_t rgb = look == 2 ? (sil.rgba8() >> 8) : palette[ci & 15];
						uint8_t* q = &b.px[((size_t)yy * bw + xx) * 4];
						q[0] = rgb >> 16; q[1] = rgb >> 8; q[2] = rgb; q[3] = 255;
					}
				SprTex t;
				if (kf < 1) b = rnds::resize(b, ow, oh);
				t.tex = be->upload(b, false);
				t.w = b.w; t.h = b.h;
				it = sprites.emplace(key, t).first;
			}
			it->second.used = frame;
			float ox = std::round(X(x) + (dw - ow) / 2), oy = std::round(X(y) + (dh - oh) / 2 - X(lift));
			be->draw(it->second.tex, ox, oy, (float)ow, (float)oh, Color(1, 1, 1, 1));
		}
		// the stage a Pokémon stands on (its first type's colour) with the Pokémon on it
		void stage(uint16_t sp, bool female, uint8_t form, int look, float x, float y, float w, float h, bool animate)
		{
			const Species* spc = species(sp);
			uint32_t tint = look == 2 || !spc ? (light ? 0x9a9488 : 0x5a5560) : TYPE_COLORS[spc->types[0] < NUM_TYPES ? spc->types[0] : 0];
			box("stage", x, y, w, h, tint);
			float pad = std::max(2.0f, std::min(w, h) * 0.03f);
			mon(sp, female, form, look, x + pad, y + pad, w - 2 * pad, h - 2 * pad, animate);
		}

		// ---- hits and scrolling ------------------------------------------------------------------------------------
		struct Hit { float x, y, w, h; std::function<void()> act; };
		std::vector<Hit> hits;
		void hit(float x, float y, float w, float h, std::function<void()> act)
		{
			// (within what is visible of the scroll area being drawn)
			float y0 = std::max(y, clipY0), y1 = std::min(y + h, clipY1);
			if (y1 > y0) hits.push_back({ x, y0, w, y1 - y0, std::move(act) });
		}
		struct Scroll { float y = 0, max = 0; float vx = 0, vy = 0, vw = 0, vh = 0; bool shown = false; };
		std::map<int, Scroll> scrolls;	// by id: page * 4 + area
		int scrollId(Page pg, int area) const { return (int)pg * 4 + area; }
		// a scroll area: what's drawn between begin and end moves up by its scroll, clipped to it
		float beginScroll(int id, float x, float y, float w, float h)
		{
			Scroll& sc = scrolls[id];
			sc.vx = x; sc.vy = y; sc.vw = w; sc.vh = h; sc.shown = true;
			sc.y = std::max(0.0f, std::min(sc.y, sc.max));	// (max: the last frame's, or set by the page before)
			be->pushClip(std::round(X(x)), std::round(X(y)), std::round(X(x + w)) - std::round(X(x)), std::round(X(y + h)) - std::round(X(y)));
			clipY0 = y; clipY1 = y + h;
			return y - sc.y;
		}
		// bottom: where its content ended (in the scrolled coordinates)
		void endScroll(int id, float bottom)
		{
			Scroll& sc = scrolls[id];
			be->popClip();
			clipY0 = 0; clipY1 = 480;
			float content = bottom + sc.y - sc.vy;
			sc.max = std::max(0.0f, content - sc.vh);
			sc.y = std::max(0.0f, std::min(sc.y, sc.max));
			if (sc.max > 0)
			{
				// the thumb: how much of it shows and where, along the area's right edge
				float tx = sc.vx + sc.vw - 4, th = std::max(24.0f, sc.vh * sc.vh / (sc.vh + sc.max));
				float ty = sc.vy + (sc.vh - th) * (sc.y / sc.max);
				fill(tx, sc.vy + 4, 3, sc.vh - 8, rnds::mix(p.line, 0.35f, p.screenBg));
				fill(tx, std::max(sc.vy + 4, ty), 3, std::min(th, sc.vh - 8), p.accent);
			}
		}
		// the Dex list's rail: dragging along it runs through the whole Pokédex
		struct Rail { float x = 0, y = 0, w = 0, h = 0; int id = -1; } rail;

		// touch state; pressed: what the finger is on (shown lit until it lifts or drags)
		bool down = false, dragging = false, railDrag = false;
		bool pressed = false; float pressX = 0, pressY = 0, pressW = 0, pressH = 0;
		float downX = 0, downY = 0, lastY = 0, startScroll = 0;
		int dragId = -1;
		// momentum: where the finger was over the last moments of a drag (ms, y), and the fling it leaves behind (CSS px a
		// ms, slowing down; flingAt: the frame time it last moved, -1 until the first update after the lift)
		std::vector<std::pair<double, float>> trail;
		int flingId = -1; float flingV = 0; double flingAt = -1;
		bool stopTap = false;		// a press that stopped a fast fling on its list: not a tap
		static constexpr float FLING_MIN = 0.3f, FLING_STOP = 0.02f, FLING_TAP = 0.12f;	// CSS px a ms
		static constexpr double FLING_TAU = 325;	// ms: the speed falls to 1/e in this long (a phone list's feel)
		void stopFling() { flingId = -1; flingV = 0; flingAt = -1; }

		// ---- the frame -------------------------------------------------------------------------------------------
		std::set<std::string> seenQueue;	// notice ids shown, not yet taken
		// the notices new when this page was opened, kept as they were: the logic drops a notice from Advice as soon as
		// it is marked seen (one tick later), and the page must still say what raised the badge
		std::vector<Notice> freshNow;
		Page freshFor = Page::COUNT;

		int noticeCount(Page pg) const
		{
			if (!dt.advice) return 0;
			int n = 0;
			for (auto& nt : dt.advice->notices)
				if (pageOf(nt.page) == pg && std::find(dt.seenNotices.begin(), dt.seenNotices.end(), nt.id) == dt.seenNotices.end() && !seenQueue.count(nt.id)) n++;
			return n;
		}
		static Page pageOf(const std::string& n)
		{
			for (int i = 0; i < (int)Page::COUNT; i++)
			{
				std::string a = pageName((Page)i), b = n;
				std::transform(a.begin(), a.end(), a.begin(), ::tolower);
				std::transform(b.begin(), b.end(), b.begin(), ::tolower);
				if (a == b) return (Page)i;
			}
			return Page::COUNT;
		}
		void markSeen(Page pg)
		{
			freshNow.clear();
			freshFor = pg;
			if (!dt.advice) return;
			for (auto& nt : dt.advice->notices)
				if (pageOf(nt.page) == pg && std::find(dt.seenNotices.begin(), dt.seenNotices.end(), nt.id) == dt.seenNotices.end())
				{
					if (!seenQueue.count(nt.id)) freshNow.push_back(nt);
					seenQueue.insert(nt.id);
				}
		}
		bool inBattle() const { return dt.live && dt.live->valid && dt.live->battle.kind != BattleKind::NONE; }

		void render(bool frame);

		// warming: a page drawn through a backend that draws nothing (its textures still made by the real one), so its
		// texts, boxes and sprites are ready before it is shown. A page's first drawing rasterizes all of its text and
		// makes its sprites' pictures: on the Plus a first visit took 30-120 ms (Team 118 ms) and the first opening 218
		// ms; later ones 7-23 ms. Warmed again after new data and new sprites (a page whose caches are ready costs
		// ~1 ms). What the drawing leaves behind for touches (hits, the rail, which scroll areas show, the next
		// animation step) is the shown page's again after
		struct WarmBackend : rnds::Backend
		{
			rnds::Backend* real;
			explicit WarmBackend(rnds::Backend* r) : real(r) {}
			rnds::TexPtr upload(const rnds::Bitmap& b, bool smooth) override { return real->upload(b, smooth); }
			void draw(const rnds::TexPtr&, float, float, float, float, const Color&) override {}
			void fill(float, float, float, float, const Color&) override {}
			void pushClip(float, float, float, float) override {}
			void popClip() override {}
			void beginScreen(int) override {}
			void endScreen() override {}
		};
		bool warmed[(int)Page::COUNT] = {};
		void warm(Page pg)
		{
			const Page was = o.cur;
			rnds::Backend* realBe = be;
			WarmBackend wb(be);
			std::vector<Hit> keepHits;
			keepHits.swap(hits);
			const double keepAnim = animNext;
			const Rail keepRail = rail;
			std::map<int, bool> shown;
			for (auto& a : scrolls) shown[a.first] = a.second.shown;
			o.cur = pg;
			be = &wb;
			render(true);
			be = realBe;
			o.cur = was;
			hits.swap(keepHits);
			animNext = keepAnim;
			rail = keepRail;
			for (auto& a : scrolls) { auto it = shown.find(a.first); a.second.shown = it != shown.end() && it->second; }
		}
		void header(const std::string& title, const std::string& sub, bool rawTitle = false, bool rawSub = false, bool where = true);
		void dock();
		void notices(Page pg, float x, float& y, float w);
		void message(const std::string& title, const std::string& body, float progress = -1, const std::string& what = "");
		void battle();
		void battlerCard(const Battler& b, bool foe, float x, float y, float w, float h, bool compact, const std::string& speed);
		void moveCard(const Mon& m, int i, const MoveVerdict* v, const Battler& foe, float x, float y, float w, float h, bool compact,
			const std::string& firstNote);
		void party();
		void team();
		void here();
		void guide();
		void dex();
		void dexDetail(uint16_t n, float x, float y, float w, float& bottom);
		void log();
	};

	// ---- the frame -----------------------------------------------------------------------------------------------------
	void Pages::Impl::render(bool own)
	{
		frame++;
		hits.clear();
		animNext = -1;
		for (auto& sc : scrolls) sc.second.shown = false;
		rail.id = -1;
		if (own) be->beginScreen(screen);
		box("screen", 0, 0, 640, 480);
		if (dt.reading)
			message("Reading your game", "The companion reads names, sprites and places from your own game once, and keeps them on the card. The game plays on meanwhile.",
				dt.readProgress, dt.readWhat);
		else if (!dt.rom)
			message("No game data", "The companion couldn't read this game's data. Back to game plays on as usual.");
		else if (!dt.live || !dt.live->valid)
			message("Waiting for your save", "Load your save in the game: your party, the battle and your Pokédex show up once the game has loaded it.");
		else
		{
			switch (o.cur)
			{
			case Page::BATTLE: battle(); break;
			case Page::PARTY: party(); break;
			case Page::TEAM: team(); break;
			case Page::HERE: here(); break;
			case Page::GUIDE: guide(); break;
			case Page::DEX: dex(); break;
			default: log(); break;
			}
		}
		dock();
		if (pressed)
		{
			Color c = p.accent; c.a = 0.22f;
			fill(pressX, pressY, pressW, pressH, c);
		}
		if (own) be->endScreen();
		// (texts and sprites not drawn for a while go, once there are many)
		// (the Dex alone keeps ~1300 entries, measured lines of its whole list: at 900 every page drawn after it had all
		// of its text rasterized again; textures of all seven pages are a few MB)
		if (cache.size() > 4000)
			for (auto it = cache.begin(); it != cache.end();)
			{
				if (it->second.used + 2 < frame) { wrapLines.erase(it->first); it = cache.erase(it); }
				else ++it;
			}
		if (sprites.size() > 160)
			for (auto it = sprites.begin(); it != sprites.end();)
				it = it->second.used + 2 < frame ? sprites.erase(it) : std::next(it);
		trimSprites();
	}

	void Pages::Impl::header(const std::string& title, const std::string& sub, bool rawTitle, bool rawSub, bool showWhere)
	{
		// the page's title and a line under it on the left; on the right the badge case and where the player is (left of
		// Back to game when the icon is in the top corner)
		float right = backTop() ? backArea().x0 - 14 : 628;
		const Live* lv = dt.live.get();
		if (lv && lv->valid)
		{
			int kk = std::max(1, (int)std::lround(1.5f * s));
			float bw = 8.0f * kk / s, gap = 4;
			float bx = right - 8 * bw - 7 * gap;
			for (int i = 0; i < 8; i++)
			{
				bool got = (lv->badges >> i) & 1;
				Color on = p.accent, hl = light ? Color::hex(rnds::accent.light) : Color::hex(0xfff6d0);
				Color off = light ? rnds::mix(p.line, 0.55f, p.screenBand) : rnds::mix(p.line, 0.4f, p.screenBg);
				pixels(PX_BADGE, 8, bx + i * (bw + gap), 14, kk, [&](char c) { return got ? (c == 'H' ? hl : on) : off; });
			}
			std::string where = zoneName(lv->zone);
			if (!where.empty() && showWhere) textR(where, false, 12, right, 34, p.muted, true);
			right = bx - 12;
		}
		text(title, true, 20, 16, 9, p.ink, rawTitle, right - 16);
		if (!sub.empty()) text(sub, false, 12, 16, 35, p.muted, rawSub, right - 16);
	}

	void Pages::Impl::dock()
	{
		// the tabs in one panel along the bottom, Back to game at the corner where the icon is (the same spot opens and
		// closes the companion: the header's right end, or the dock's); a red count on a tab with something new for the
		// player
		const Area ba = backArea();
		const float bx0 = ba.x0, by0 = ba.y0, bx1 = ba.x1, by1 = ba.y1;
		const float dockEnd = backTop() ? 628 : bx0 - 8;
		box("bubble", 12, 424, dockEnd - 12, 48);
		bool usable = !dt.reading && dt.rom && dt.live && dt.live->valid;
		int n = (int)Page::COUNT;
		float cell = (dockEnd - 12 - 12) / n;
		for (int i = 0; i < n; i++)
		{
			Page pg = (Page)i;
			float tx = 18 + i * cell;
			bool on = usable && pg == o.cur;
			bool dim = !usable || (pg == Page::BATTLE && !inBattle());
			std::string label = tr(pageName(pg));
			if (on) box("pick", tx + 1, 430, cell - 2, 36);
			float lw = measure(label, true, 14, true);
			text(label, true, 14, tx + cell / 2 - lw / 2, 439, on ? p.onPick : dim ? p.line : p.ink, true, cell - 6);
			int cnt = usable ? noticeCount(pg) : 0;
			if (cnt > 0 && !on)
			{
				// the count's red dot over the tab's top right
				std::string c = std::to_string(std::min(cnt, 9));
				float d = 16, cx = tx + cell - d - 1, cy = 421;
				int kk = std::max(1, (int)std::lround(s));
				(void)kk;
				box("chip", cx, cy, d, d, BADGE_RED);
				float cw = measure(c, true, 12, true);
				text(c, true, 12, cx + d / 2 - cw / 2, cy + 0.5f, Color::hex(0xffffff), true);
			}
			if (usable) hit(tx, 424, cell, 52, [this, pg] { o.show(pg); });
		}
		box("back", bx0, by0, bx1 - bx0, by1 - by0);
		std::string b = tr("Back to game");
		float bw = measure(b, true, 14, true);
		float cxm = (bx0 + bx1) / 2;
		if (bw > bx1 - bx0 - 16)
		{
			std::string a = tr("Back"), c = tr("to game");
			text(a, true, 14, cxm - measure(a, true, 14, true) / 2, by0 + 8, p.onPick, true);
			text(c, true, 14, cxm - measure(c, true, 14, true) / 2, by0 + 24, p.onPick, true);
		}
		else text(b, true, 14, cxm - bw / 2, (by0 + by1) / 2 - 10, p.onPick, true);
	}

	void Pages::Impl::message(const std::string& title, const std::string& body, float progress, const std::string& what)
	{
		box("hero", 60, 110, 520, 230);
		text(title, true, 22, 88, 136, p.ink);
		float y = 176 + wrap(body, 14, 88, 176, 464, 4, p.muted);
		if (progress >= 0)
		{
			y += 18;
			// the progress as the HP bar's blocks, in the accent
			int blocks = 40;
			float w = 464, gap = 2, bw = (w - (blocks - 1) * gap) / blocks;
			int on = (int)std::lround(std::max(0.0f, std::min(1.0f, progress)) * blocks);
			for (int i = 0; i < blocks; i++) fill(88 + i * (bw + gap), y, bw, 14, i < on ? p.accent : barTrack());
			char pc[16]; snprintf(pc, sizeof pc, "%d%%", (int)std::lround(progress * 100));
			textR(pc, true, 14, 552, y + 22, p.ink, true);
			if (!what.empty()) text(what, false, 13, 88, y + 23, p.muted, true, 380);
		}
	}

	// the notices for this page, new since it was last opened: a line each at the top of its content
	void Pages::Impl::notices(Page pg, float x, float& y, float w)
	{
		if (freshFor != pg) return;
		for (auto& nt : freshNow)
		{
			if (pageOf(nt.page) != pg) continue;
			float th = 20 + wrapHeight(nt.text, 12, w - 48, 2, true);
			box("best", x, y, w, th + 12);
			pixels(PX_SPARK, 7, x + 14, y + 12, k(), [&](char) { return p.accent; });
			text(nt.title, true, 14, x + 34, y + 9, p.ink, true, w - 48);
			if (!nt.text.empty()) wrap(nt.text, 12, x + 34, y + 28, w - 48, 2, p.muted, true);
			y += th + 20;
		}
	}

	// ---- Battle --------------------------------------------------------------------------------------------------------
	// speed: who moves first ("faster" / "slower", "" not known), under the lead's ability
	void Pages::Impl::battlerCard(const Battler& b, bool foe, float x, float y, float w, float h, bool compact, const std::string& speed)
	{
		const Mon& m = b.mon;
		box(foe ? "bubble" : "hero", x, y, w, h);
		float st = compact ? h - 16 : h - 20;
		stage(m.species, m.female, m.form, m.shiny() ? 1 : 0, x + 10, y + 10, st, st, true);
		float tx = x + 10 + st + 12, tw = x + w - 12 - tx;
		// the name, the level at the right
		std::string lv = std::to_string(m.level);
		float lw = measure(lv, true, compact ? 14 : 17, true);
		textR(lv, true, compact ? 14 : 17, x + w - 12, y + (compact ? 9 : 10), p.ink, true);
		textR(tr("Lv"), false, 12, x + w - 12 - lw - 4, y + (compact ? 11 : 14), p.muted);
		monName(m.nick.empty() ? speciesName(m.species) : m.nick, gender(m), compact ? 15 : 18, tx, y + (compact ? 8 : 9), tw - lw - 26, p.ink);
		float cy = y + (compact ? 30 : 36);
		Type ts[2] = { b.types[0], b.types[1] };
		float ex = typeChips(ts, tx, cy);
		std::string sn = statusName(m.status);
		if (m.hp == 0) sn = "FNT";
		if (!sn.empty()) ex += chip(sn, statusColour(sn), ex, cy) + 5;
		if (m.shiny()) { pixels(PX_SPARK, 7, ex + 2, cy + 2, k(), [](char) { return Color::hex(0xf8d030); }); ex += 20; }
		// the stat stages that aren't 0 (Battler.stages: HP (never used), Atk, Def, Speed, SpA, SpD, accuracy, evasion)
		if (!compact)
		{
			static const char* const STG[8] = { "", "Atk", "Def", "Spe", "SpA", "SpD", "Acc", "Eva" };
			for (int i = 1; i < 8; i++)
				if (b.stages[i] && ex < x + w - 60)
				{
					char l[24]; snprintf(l, sizeof l, "%s %+d", STG[i], b.stages[i]);
					ex += chip(l, b.stages[i] > 0 ? 0x3c8c50 : 0xa84848, ex, cy) + 4;
				}
		}
		if (compact)
		{
			// (a double battle's half-height card: the HP bar beside the types)
			float bx = ex + 4;
			if (bx < x + w - 40) hpBar(m.hp, m.maxHp, bx, cy + 6, x + w - 12 - bx, 7);
			return;
		}
		float by = y + h - 38;
		hpBar(m.hp, m.maxHp, tx, by, tw, 10);
		{
			std::string hp = std::to_string(m.hp) + " / " + std::to_string(m.maxHp);
			text(hp, true, 13, tx, by + 15, p.ink, true);
			text(tr("HP"), false, 12, tx + measure(hp, true, 13, true) + 5, by + 16, p.muted);
			std::string ab = abilityName(m.ability);
			float sw = speed.empty() ? 0 : textR(speed, false, 12, x + w - 12, by + 16, speed == tr("faster") ? good() : warn(), true);
			if (!ab.empty()) textR(ab + (sw > 0 ? "," : ""), false, 12, x + w - 12 - (sw > 0 ? sw + 5 : 0), by + 16, p.muted, true);
		}
	}

	// a move against the foe. compact (wild battles, where the catch helper needs the room): the type beside the name
	// and no words for the effectiveness, the multiplier says it. firstNote: what to say about moving first when this
	// move differs from the others (a priority move)
	void Pages::Impl::moveCard(const Mon& m, int i, const MoveVerdict* v, const Battler& foe, float x, float y, float w, float h,
		bool compact, const std::string& firstNote)
	{
		uint16_t mv = m.moves[i];
		const MoveData* md = move(mv);
		bool best = v && v->best;
		box(best ? "best" : "bubble", x, y, w, h);
		if (!md) { text("-", true, 16, x + 14, y + 10, p.muted, true); return; }
		bool out = m.pp[i] == 0;
		int maxPP = md->pp + md->pp * m.ppUps[i] / 5;
		std::string pp = std::to_string(m.pp[i]) + "/" + std::to_string(maxPP);
		float pw = textR(pp, true, 13, x + w - 14, y + 11, out ? bad() : p.ink, true);
		textR(tr("PP"), false, 11, x + w - 14 - pw - 4, y + 13, p.muted);
		int q = v ? v->quarters : 4;
		bool status = md->cat == MoveCategory::STATUS || md->power == 0;
		uint32_t mc = q == 0 ? 0x55525a : q > 4 ? 0x3c9c4c : q < 4 ? 0xc89a20 : (light ? 0xd8d0c0 : 0x4a4650);
		float nameW;
		if (compact)
		{
			float tw = typeChip(md->type, 0, 0, true);
			nameW = text(md->name, true, 16, x + 14, y + 9, out ? p.muted : p.ink, true, w - 110 - pw - tw);
			typeChip(md->type, x + 14 + nameW + 8, y + 9);
		}
		else nameW = text(md->name, true, 16, x + 14, y + 9, out ? p.muted : p.ink, true, w - 90 - pw);
		(void)nameW;
		float by;	// the damage bar's top
		float mw = 0;
		if (!compact)
		{
			float cy = y + 32;
			float cx = x + 14 + typeChip(md->type, x + 14, cy) + 8;
			Color vc = status ? p.muted : q == 0 ? p.line : q > 4 ? good() : q < 4 ? warn() : p.ink;
			std::string vt = status ? (v && !v->note.empty() ? v->note : tr("Status move")) : tr(verdict(q));
			if (!status)
			{
				mw = chip(times(q), mc, 0, 0, true, true, 20);
				chip(times(q), mc, x + w - 14 - mw, cy - 1, true, false, 20);
			}
			text(vt, false, 12, cx, cy + 2, vc, status && v && !v->note.empty(), x + w - 20 - mw - cx);
			by = y + h - 28;
		}
		else
		{
			by = y + 34;
			if (status)
			{
				std::string vt = v && !v->note.empty() ? v->note : tr("Status move");
				text(vt, false, 12, x + 14, y + 36, p.muted, v && !v->note.empty(), w - 28);
				return;
			}
			mw = chip(times(q), mc, 0, 0, true, true, 18);
			chip(times(q), mc, x + w - 14 - mw, by - 5, true, false, 18);
		}
		if (status || !v) return;
		// the damage against the foe's HP: the bar is the foe's whole HP, the red line where its HP is now (the KO
		// line); the sure part solid, the rest of the range lighter. The words under it say what that means
		float bw = w - 28 - (compact ? mw + 8 : 0);
		float cur = foe.mon.maxHp > 0 ? 100.0f * foe.mon.hp / foe.mon.maxHp : 100;
		float scaleMax = std::max(100.0f, std::min(150.0f, v->dmgHi));
		auto at = [&](float pct) { return x + 14 + bw * std::min(pct, scaleMax) / scaleMax; };
		fill(x + 14, by, bw, 8, barTrack());
		Color solid = best ? p.accent : q > 4 ? Color::hex(GOOD) : q < 4 ? Color::hex(HP_YELLOW) : p.ink;
		Color soft = rnds::mix(solid, 0.45f, light ? p.screenBand : p.bubbleBody);
		fill(x + 14, by, at(v->dmgLo) - (x + 14), 8, solid);
		fill(at(v->dmgLo), by, at(v->dmgHi) - at(v->dmgLo), 8, soft);
		fill(at(cur) - 1, by - 3, 2, 14, Color::hex(BAD));
		char d[64];
		snprintf(d, sizeof d, "%d-%d%%", (int)std::lround(v->dmgLo), (int)std::lround(v->dmgHi));
		std::string what;
		if (v->dmgLo >= cur) what = tr("sure KO");
		else if (v->dmgHi >= cur) what = tr("may KO");
		else if (v->dmgHi > 0)
		{
			int a = (int)std::ceil(cur / v->dmgHi), b = (int)std::ceil(cur / std::max(0.1f, v->dmgLo));
			char hb[48];
			if (a == b) snprintf(hb, sizeof hb, "%d", a); else snprintf(hb, sizeof hb, "%d-%d", a, std::min(b, 99));
			what = std::string(hb) + " " + tr("hits");
		}
		float ty = by + 11;
		float dw = text(d, true, 12, x + 14, ty, p.ink, true);
		float ww = text(what, false, 12, x + 14 + dw + 6, ty, best ? p.accent : p.muted, true);
		std::string right = !v->note.empty() ? v->note : firstNote;
		if (!right.empty()) text(right, false, 12, x + 14 + dw + ww + 18, ty, p.muted, !v->note.empty(), x + w - 14 - (x + 14 + dw + ww + 18));
	}

	void Pages::Impl::battle()
	{
		const Battle& b = dt.live->battle;
		if (b.kind == BattleKind::NONE)
		{
			header(tr("Battle"), tr("No battle right now"));
			box("bubble", 12, 60, 616, 140);
			text(tr("This page fills in when a battle starts"), true, 16, 32, 80, p.ink);
			wrap(tr("Your lead's moves against the foe with the damage each does, who moves first, what the foe knows, and in the wild the catch chance with each ball and the wild Pokémon's nature and IVs."),
				13, 32, 108, 576, 4, p.muted);
			return;
		}
		bool wild = b.kind == BattleKind::WILD || b.kind == BattleKind::DOUBLE_WILD;
		bool dbl = b.kind == BattleKind::DOUBLE_WILD || b.kind == BattleKind::DOUBLE_TRAINER;
		const Trainer* tr0 = nullptr;
		if (!wild && dt.rom)
		{
			auto it = dt.rom->trainers.find(b.trainerId);
			if (it != dt.rom->trainers.end()) tr0 = &it->second;
		}
		// the title: who you're up against (where is at the header's right)
		std::string title, sub;
		if (wild) title = tr("Wild") + " " + speciesName(b.foes[0].mon.species);
		else if (tr0) title = tr0->cls + " " + tr0->name;
		else title = tr("Trainer battle");
		if (tr0 && b.foeParty.size() > 1)
		{
			int left = 0;
			for (auto& m : b.foeParty) left += m.hp > 0;
			sub = std::to_string(left) + " " + tr("of") + " " + std::to_string(b.foeParty.size()) + " " + tr("left") + ", ";
		}
		if (b.turn) sub += tr("turn") + " " + std::to_string(b.turn);
		header(title, sub, true, true);
		// who moves first: what most of the lead's moves say (a priority move that differs says so itself)
		const Mon& lead = b.ours[0].mon;
		int firsts = 0, rated = 0;
		if (dt.advice)
			for (auto& mv : dt.advice->moves) { rated++; firsts += mv.movesFirst; }
		bool mostlyFirst = rated && firsts * 2 > rated;
		std::string speed = rated ? tr(mostlyFirst ? "faster" : "slower") : "";
		float y = 58;
		float ch = wild ? 104 : 112;
		if (!dbl)
		{
			battlerCard(b.ours[0], false, 12, y, 304, ch, false, speed);
			battlerCard(b.foes[0], true, 324, y, 304, ch, false, "");
			y += ch + 8;
		}
		else
		{
			for (int i = 0; i < 2; i++)
			{
				if (b.ours[i].present) battlerCard(b.ours[i], false, 12, y + i * 58, 304, 54, true, "");
				if (b.foes[i].present) battlerCard(b.foes[i], true, 324, y + i * 58, 304, 54, true, "");
			}
			y += 120;
		}
		// the lead's four moves against the first foe
		float mh = wild ? 62 : 78;
		for (int i = 0; i < 4; i++)
		{
			const MoveVerdict* v = nullptr;
			if (dt.advice)
				for (auto& mv : dt.advice->moves) if (mv.move == lead.moves[i] && lead.moves[i]) v = &mv;
			std::string fn = v && v->movesFirst != mostlyFirst ? tr(v->movesFirst ? "moves first" : "moves second") : "";
			moveCard(lead, i, v, b.foes[0], 12 + (i % 2) * 312, y + (i / 2) * (mh + 8), 304, mh, wild, fn);
		}
		y += 2 * (mh + 8);
		float bottom = 416;
		if (wild)
		{
			// the catch helper, and what this wild one is (nature, IVs, Hidden Power)
			const Mon& f = b.foes[0].mon;
			box("bubble", 12, y, 340, bottom - y);
			text(tr("Catch chance"), true, 14, 26, y + 8, p.ink);
			float ry = y + 30;
			if (dt.advice && !dt.advice->catching.empty())
			{
				for (auto& c : dt.advice->catching)
				{
					if (ry > bottom - 34) break;
					std::string bn = itemName(c.ball);
					int have = 0;
					for (auto& pk : dt.live->bag) for (auto& it : pk.second) if (it.item == c.ball) have += it.count;
					text(bn, false, 13, 26, ry, p.ink, true, 110);
					if (have) text("×" + std::to_string(have), false, 12, 140, ry + 1, p.muted, true);
					char pc[16]; snprintf(pc, sizeof pc, "%.0f%%", c.percent);
					bar(176, ry + 4, 110, 8, c.percent / 100, c.percent >= 50 ? Color::hex(GOOD) : c.percent >= 20 ? Color::hex(HP_YELLOW) : Color::hex(BAD));
					textR(pc, true, 13, 338, ry, p.ink, true);
					ry += 18;
				}
				for (auto& c : dt.advice->catching)
					if (!c.tip.empty())
					{
						pixels(PX_SPARK, 7, 26, bottom - 21, k(), [&](char) { return p.accent; });
						text(c.tip, false, 12, 46, bottom - 24, p.accent, true, 296);
						break;
					}
			}
			else text(tr("No balls in your bag"), false, 13, 26, ry, p.muted);
			box("bubble", 360, y, 268, bottom - y);
			std::string nat = dt.rom && f.nature < dt.rom->natures.size() ? dt.rom->natures[f.nature] : "";
			text(nat, true, 14, 374, y + 8, p.ink, true);
			// (the stats the nature raises and lowers: arrows over the IV columns below)
			int up = f.nature / 5 + 1, dn = f.nature % 5 + 1;
			// Hidden Power at the right of the nature
			{
				Type ht = (Type)f.hiddenPowerType(); int hpow = f.hiddenPowerPower();
				std::string pw = std::to_string(hpow);
				float pww = textR(pw, true, 13, 614, y + 8, p.ink, true);
				float cw = typeChip(ht, 0, 0, true);
				typeChip(ht, 614 - pww - 6 - cw, y + 7);
				textR(tr("Hidden Power"), false, 12, 614 - pww - 12 - cw, y + 10, p.muted);
			}
			// the six IVs as columns (31 the top)
			float iy = y + 32, cw = 40, hgt = std::max(16.0f, bottom - y - 32 - 34);
			for (int i = 0; i < 6; i++)
			{
				float cx = 374 + i * cw;
				float f31 = f.ivs[i] / 31.0f;
				fill(cx, iy, cw - 10, hgt, barTrack());
				fill(cx, iy + hgt * (1 - f31), cw - 10, hgt * f31, f.ivs[i] >= 30 ? p.accent : f.ivs[i] >= 20 ? Color::hex(GOOD) : f.ivs[i] >= 10 ? Color::hex(HP_YELLOW) : Color::hex(BAD));
				std::string v = std::to_string(f.ivs[i]);
				float vw = measure(v, true, 12, true), lw2 = measure(STAT_SHORT[i], false, 11, true);
				text(v, true, 12, cx + (cw - 10) / 2 - vw / 2, iy + hgt + 2, p.ink, true);
				text(STAT_SHORT[i], false, 11, cx + (cw - 10) / 2 - lw2 / 2, iy + hgt + 16, p.muted, true);
				if (up != dn && (i == up || i == dn))
					pixels(i == up ? PX_UP : PX_DOWN, 4, cx + (cw - 10) / 2 + lw2 / 2 + 2, iy + hgt + 20, k() > 2 ? 2 : 1,
						[&](char) { return Color::hex(i == up ? GOOD : BAD); });
			}
		}
		else
		{
			// what the foe knows (the trainer's team in the ROM), and its team: the fainted ones as silhouettes
			box("bubble", 12, y, 616, bottom - y);
			const Mon& f = b.foes[0].mon;
			std::vector<std::string> known;
			if (tr0)
				for (auto& tm : tr0->team)
					if (tm.species == f.species && tm.level == f.level)
						for (uint16_t mv : tm.moves) if (mv) known.push_back(moveName(mv));
			if (known.empty()) for (uint16_t mv : f.moves) if (mv) known.push_back(moveName(mv));
			float teamW = tr0 && b.foeParty.size() > 1 ? 40.0f * b.foeParty.size() : 0;
			float lw = text(speciesName(f.species), true, 13, 26, y + 10, p.accent, true);
			text(tr("knows"), false, 13, 26 + lw + 6, y + 10, p.muted);
			float mx = 26, my = y + 34;
			for (auto& k2 : known)
			{
				float w = measure(k2, true, 13, true) + 18;
				if (mx + w > 614) { mx = 26; my += 26; }
				if (my > bottom - 20) break;
				box("slot", mx, my - 3, w, 22);
				text(k2, true, 13, mx + 9, my + 1, p.ink, true);
				mx += w + 6;
			}
			if (teamW > 0)
			{
				float sx = 614 - teamW;
				for (size_t i = 0; i < b.foeParty.size(); i++)
				{
					const Mon& t = b.foeParty[i];
					mon(t.species, t.female, t.form, t.hp == 0 ? 2 : 0, sx + i * 40, y + 6, 36, 26, false);
				}
			}
		}
	}

	// ---- Party ---------------------------------------------------------------------------------------------------------
	void Pages::Impl::party()
	{
		const Live& lv = *dt.live;
		if (lv.party.empty()) { header(tr("Your party"), ""); message(tr("No Pokémon yet"), tr("Your party shows here once you have your first Pokémon.")); return; }
		int& sel = o.partyIdx;
		sel = std::max(0, std::min(sel, (int)lv.party.size() - 1));
		const Mon& m = lv.party[sel];
		std::string sub = std::to_string(lv.party.size()) + " " + tr("in your party") + ", " + tr("money") + " " + std::to_string(lv.money);
		header(tr("Your party"), sub);
		// the six on the left
		for (size_t i = 0; i < lv.party.size() && i < 6; i++)
		{
			const Mon& pm = lv.party[i];
			float y = 58 + i * 60, x = 12, w = 196, h = 54;
			bool on = (int)i == sel;
			box(on ? "pick" : "slot", x, y, w, h);
			Color ink = on ? p.onPick : p.ink, sub2 = on ? p.onPickAccent : p.muted;
			mon(pm.species, pm.female, pm.form, pm.shiny() ? 1 : 0, x + 6, y + 5, 42, 40, false);
			float nw = monName(monLabel(pm), gender(pm), 14, x + 54, y + 8, 100, ink);
			(void)nw;
			if (!pm.egg)
			{
				std::string l = std::to_string(pm.level);
				textR(l, true, 13, x + w - 10, y + 9, ink, true);
				textR(tr("Lv"), false, 11, x + w - 12 - measure(l, true, 13, true), y + 11, sub2);
				std::string st = pm.hp == 0 ? "FNT" : statusName(pm.status);
				hpBar(pm.hp, pm.maxHp, x + 54, y + 33, st.empty() ? w - 64 : w - 104, 6);
				if (!st.empty()) chip(st, statusColour(st), x + w - 46, y + 28, true, false, 16);
			}
			int idx = (int)i;
			hit(x, y, w, h, [this, idx] { o.partyIdx = idx; scrolls[scrollId(Page::PARTY, 1)].y = 0; });
		}
		// the one picked, on the right (scrolls)
		const float X0 = 216, W0 = 412;
		box("hero", X0, 58, W0, 358);
		int id = scrollId(Page::PARTY, 1);
		float y = beginScroll(id, X0 + 3, 61, W0 - 6, 352) + 9;
		float x = X0 + 14, w = W0 - 28;
		notices(Page::PARTY, x, y, w);
		const Species* sp = species(m.species);
		stage(m.species, m.female, m.form, m.shiny() ? 1 : 0, x, y, 104, 104, true);
		float tx = x + 116, tw = x + w - tx;
		monName(monLabel(m), gender(m), 19, tx, y, tw - 60, p.ink);
		if (!m.egg)
		{
			std::string l = std::to_string(m.level);
			textR(l, true, 18, x + w, y, p.ink, true);
			textR(tr("Lv"), false, 12, x + w - measure(l, true, 18, true) - 4, y + 5, p.muted);
		}
		if (!m.nick.empty() && sp) text(sp->name, false, 12, tx, y + 24, p.muted, true);
		if (sp) typeChips(sp->types, tx, y + 42);
		float ly = y + 66;
		auto row = [&](const std::string& label, const std::string& value, bool raw, float yy)
		{
			float lw = text(label, false, 12, tx, yy + 1, p.muted);
			text(value, true, 13, tx + std::max(56.0f, lw + 8), yy, p.ink, raw, tw - 60);
		};
		if (!m.egg)
		{
			std::string nat = dt.rom && m.nature < dt.rom->natures.size() ? dt.rom->natures[m.nature] : "";
			row(tr("Nature"), nat, true, ly);
			row(tr("Ability"), abilityName(m.ability), true, ly + 17);
			std::string it = itemName(m.item);
			row(tr("Item"), it.empty() ? tr("none") : it, !it.empty(), ly + 34);
		}
		y += 116;
		if (m.egg)
		{
			wrap(tr("It moves now and then. It hatches as you walk."), 13, x, y, w, 2, p.muted);
			endScroll(id, y + 40);
			return;
		}
		// EXP to the next level, friendship as hearts
		if (sp)
		{
			uint32_t a = expForLevel(sp->growth, m.level), b = expForLevel(sp->growth, std::min(100, m.level + 1));
			float f = b > a ? (float)(m.exp - std::min(m.exp, a)) / (b - a) : 1;
			text(tr("EXP"), false, 12, x, y + 1, p.muted);
			bar(x + 36, y + 4, 180, 8, f, Color::hex(EXP_BLUE));
			if (m.level < 100)
			{
				std::string tn = std::to_string(b > m.exp ? b - m.exp : 0) + " " + tr("to the next level");
				text(tn, false, 12, x + 224, y + 1, p.muted, true, w - 224);
			}
			y += 20;
		}
		{
			text(tr("Friendship"), false, 12, x, y + 1, p.muted);
			int hearts = (m.friendship * 5 + 127) / 255, kk = std::max(1, (int)std::lround(1.5f * s));
			float hx = x + 80;
			for (int i = 0; i < 5; i++)
				pixels(i < hearts ? PX_HEART : PX_HEART_EMPTY, 6, hx + i * 14, y + 3, kk, [&](char) { return Color::hex(0xf07890); });
			text(std::to_string(m.friendship), true, 12, hx + 76, y + 1, p.ink, true);
			y += 26;
		}
		// the stats: value, IV, EV (and the nature's up and down)
		y = heading(tr("Stats"), x, y);
		text(tr("IV"), false, 11, x + 146, y - 20, p.muted);
		text(tr("Effort points"), false, 11, x + 214, y - 20, p.muted);
		int up = m.nature / 5 + 1, dn = m.nature % 5 + 1;	// (in the six's order, HP first)
		int evTotal = 0;
		for (int i = 0; i < 6; i++) evTotal += m.evs[i];
		for (int i = 0; i < 6; i++)
		{
			// stats[] is Atk, Def, Speed, SpA, SpD; the six rows are HP, Atk, Def, Speed, SpA, SpD
			int val = i == 0 ? m.maxHp : m.stats[i - 1];
			float ry = y + i * 20;
			text(STAT_NAMES[i], false, 12, x, ry + 1, p.muted);
			text(std::to_string(val), true, 13, x + 62, ry, p.ink, true);
			if (up != dn && i == up) pixels(PX_UP, 4, x + 96, ry + 5, k(), [](char) { return Color::hex(GOOD); });
			if (up != dn && i == dn) pixels(PX_DOWN, 4, x + 96, ry + 5, k(), [](char) { return Color::hex(BAD); });
			// IV: a number, the accent at 31
			std::string iv = std::to_string(m.ivs[i]);
			text(iv, true, 12, x + 140 - measure(iv, true, 12, true), ry + 1, m.ivs[i] == 31 ? p.accent : p.ink, true);
			bar(x + 146, ry + 5, 50, 6, m.ivs[i] / 31.0f, m.ivs[i] >= 25 ? Color::hex(GOOD) : m.ivs[i] >= 10 ? Color::hex(HP_YELLOW) : Color::hex(BAD));
			// EVs out of 252
			bar(x + 214, ry + 5, w - 214 - 34, 6, m.evs[i] / 252.0f, Color::hex(EXP_BLUE));
			textR(std::to_string(m.evs[i]), true, 12, x + w, ry + 1, p.ink, true);
		}
		y += 6 * 20 + 2;
		{
			std::string t = tr("Effort points") + " " + std::to_string(evTotal) + " / 510";
			textR(t, false, 12, x + w, y, p.muted, true);
			y += 22;
		}
		// moves with PP
		y = heading(tr("Moves"), x, y);
		for (int i = 0; i < 4; i++)
		{
			const MoveData* md = move(m.moves[i]);
			float ry = y + i * 24;
			if (!md) { text("-", true, 13, x, ry, p.muted, true); continue; }
			float cx = x + typeChip(md->type, x, ry - 1) + 8;
			text(md->name, true, 13, cx, ry, p.ink, true, 150);
			std::string pw = md->power ? std::to_string(md->power) : "-";
			text(pw, false, 12, x + 250, ry + 1, p.muted, true);
			int maxPP = md->pp + md->pp * m.ppUps[i] / 5;
			textR(std::to_string(m.pp[i]) + " / " + std::to_string(maxPP), true, 12, x + w, ry + 1, m.pp[i] == 0 ? bad() : p.ink, true);
		}
		y += 4 * 24 + 6;
		// the evolution watch
		if (sp && !sp->evolutions.empty())
		{
			y = heading(tr("Evolution"), x, y);
			for (auto& e : sp->evolutions)
			{
				box("slot", x, y, w, 50);
				mon(e.into, false, 0, seen(e.into) ? 0 : 2, x + 6, y + 5, 40, 40, false);
				text(seen(e.into) ? speciesName(e.into) : "???", true, 14, x + 54, y + 7, p.ink, true, 150);
				text(e.how, false, 12, x + 54, y + 27, p.muted, true, w - 66);
				// a level evolution: how far, as a bar
				if (e.method == 4 && e.param > m.level)
				{
					std::string togo = std::to_string(e.param - m.level) + " " + tr("levels to go");
					textR(togo, true, 12, x + w - 12, y + 8, p.accent, true);
				}
				else if (e.method == 1 || e.method == 2 || e.method == 3)	// friendship (any time, day, night)
				{
					bar(x + w - 112, y + 12, 100, 6, std::min(1.0f, m.friendship / 220.0f), Color::hex(0xf07890));
				}
				y += 56;
			}
			if (dt.guide)
			{
				auto it = dt.guide->evolutionNotes.find(m.species);
				if (it != dt.guide->evolutionNotes.end()) { y += wrap(it->second, 12, x, y, w, 3, p.muted, true) + 6; }
			}
			y += 4;
		}
		// the next moves it learns by levelling up
		if (sp)
		{
			std::vector<const LevelMove*> next;
			for (auto& lm : sp->learnset) if (lm.level > m.level && next.size() < 3) next.push_back(&lm);
			if (!next.empty())
			{
				y = heading(tr("Learns next"), x, y);
				for (auto* lm : next)
				{
					text(tr("Lv") + " " + std::to_string(lm->level), false, 12, x, y + 1, p.muted, true);
					const MoveData* md = move(lm->move);
					text(moveName(lm->move), true, 13, x + 50, y, p.ink, true, 180);
					if (md) typeChip(md->type, x + 240, y - 1);
					y += 22;
				}
				y += 6;
			}
		}
		// the EV tracker: what to beat here for the effort points it gives
		if (dt.rom)
		{
			auto z = dt.rom->zones.find(lv.zone);
			if (z != dt.rom->zones.end() && !z->second.encounters.empty())
			{
				// by the stat they give: "Speed: Buneary, Murkrow, Zubat"
				std::vector<std::string> by[6];
				std::set<uint16_t> done;
				for (auto& en : z->second.encounters)
				{
					const Species* es = species(en.species);
					if (!es || done.count(en.species)) continue;
					done.insert(en.species);
					for (int i = 0; i < 6; i++)
						if (es->evYield[i]) by[i].push_back(speciesName(en.species) + (es->evYield[i] > 1 ? " +" + std::to_string(es->evYield[i]) : ""));
				}
				bool any = false;
				for (int i = 0; i < 6; i++) any |= !by[i].empty();
				if (any)
				{
					y = heading(tr("Effort points here"), x, y, tr("who gives which"));
					for (int i = 0; i < 6; i++)
					{
						if (by[i].empty()) continue;
						std::string l;
						for (size_t j = 0; j < by[i].size(); j++) l += (j ? ", " : "") + by[i][j];
						text(STAT_NAMES[i], false, 12, x, y + 1, p.muted);
						y += wrap(l, 12, x + 70, y + 1, w - 70, 3, p.ink, true, true) + 4;
					}
				}
			}
		}
		endScroll(id, y + 8);
	}

	// ---- Team ----------------------------------------------------------------------------------------------------------
	void Pages::Impl::team()
	{
		const Advice* a = dt.advice.get();
		const Live& lv = *dt.live;
		std::string sub;
		if (a && a->next) sub = tr("Ready for") + " " + a->next->name + "?";
		header(tr("Your team"), sub, false, true);
		int id = scrollId(Page::TEAM, 0);
		float y = beginScroll(id, 0, 56, 640, 362) + 2;
		notices(Page::TEAM, 12, y, 616);
		// the score: big, then its parts as bars
		box("hero", 12, y, 216, 222);
		text(tr("Team score"), false, 13, 120 - measure(tr("Team score"), false, 13) / 2, y + 14, p.muted, true);
		std::string sc = a ? std::to_string(a->teamScore) : "-";
		float sw = measure(sc, true, 64, true);
		text(sc, true, 64, 120 - sw / 2, y + 30, p.accent, true);
		std::string of = tr("out of 100");
		text(of, false, 12, 120 - measure(of, false, 12) / 2, y + 108, p.muted, true);
		if (a)
		{
			float py = y + 134;
			for (auto& part : a->scoreParts)
			{
				if (py > y + 210) break;
				text(part.name, false, 12, 26, py, p.ink, true, 90);
				float f = part.max > 0 ? (float)part.score / part.max : 0;
				bar(118, py + 4, 70, 7, f, f >= 0.7f ? Color::hex(GOOD) : f >= 0.45f ? Color::hex(HP_YELLOW) : Color::hex(BAD));
				textR(std::to_string(part.score), true, 12, 214, py, p.ink, true);
				py += 19;
			}
		}
		// the party in a row
		box("bubble", 236, y, 392, 84);
		float cw = 392.0f / 6;
		for (size_t i = lv.party.size(); i < 6; i++)	// (the places still free)
			box("well", 236 + i * cw + cw / 2 - 18, y + 16, 36, 36);
		for (size_t i = 0; i < lv.party.size() && i < 6; i++)
		{
			const Mon& m = lv.party[i];
			float cx = 236 + i * cw;
			mon(m.species, m.female, m.form, m.shiny() ? 1 : 0, cx + 8, y + 8, cw - 16, 44, false);
			std::string n = monLabel(m);
			// (a ten-letter name like Krookodile or Feraligatr is wider than its place at 12 px: a smaller size first)
			float fs = 12;
			while (fs > 9 && measure(n, true, fs, true) > cw - 6) fs -= 1;
			float nw = std::min(cw - 6, measure(n, true, fs, true));
			text(n, true, fs, cx + cw / 2 - nw / 2, y + 52 + (12 - fs) / 2, p.ink, true, cw - 6);
			if (!m.egg)
			{
				std::string l = tr("Lv") + " " + std::to_string(m.level);
				text(l, false, 11, cx + cw / 2 - measure(l, false, 11, true) / 2, y + 66, p.muted, true);
			}
		}
		// the next big fight and what goes wrong in it
		float ny = y + 92, nh = 130;
		box("bubble", 236, ny, 392, nh);
		if (a && a->next)
		{
			const Milestone& ms = *a->next;
			float tx = 250;
			float w1 = text(tr("Next") + ":", false, 13, tx, ny + 11, p.muted, true);
			text(goalWithPlace(ms), true, 15, tx + w1 + 6, ny + 9, p.ink, true, 590 - (tx + w1 + 6));	// (short of the unchecked marker at 602)
			float cy = ny + 32, cx = tx;
			if (ms.theme < NUM_TYPES) cx += typeChip(ms.theme, cx, cy) + 8;
			// the ace: the strongest of the first trainer's team
			if (dt.rom && !ms.trainerIds.empty())
			{
				auto t = dt.rom->trainers.find(ms.trainerIds[0]);
				if (t != dt.rom->trainers.end() && !t->second.team.empty())
				{
					const TrainerMon* ace = &t->second.team[0];
					for (auto& tm : t->second.team) if (tm.level >= ace->level) ace = &tm;
					std::string as = tr("ace") + " " + speciesName(ace->species) + ", " + tr("Lv") + " " + std::to_string(ace->level);
					text(as, false, 12, cx, cy + 2, p.muted, true);
				}
			}
			if (ms.q == Quality::REFERENCE_ONLY) unchecked(602, ny + 13);
			float py = cy + 26;
			for (size_t i = 0; i < a->nextProblems.size(); i++)
			{
				if (py > ny + nh - 18) break;
				fill(tx, py + 6, 4, 4, p.accent);
				py += wrap(a->nextProblems[i], 12, tx + 12, py, 360, 2, p.ink, true);
			}
		}
		else text(tr("No big fight left in the guide"), false, 13, 250, ny + 12, p.muted);
		y += 232;
		// catch now: what fills the gaps, reachable now
		if (a && !a->catchNow.empty())
		{
			y = heading(tr("Catch now"), 16, y + 2, tr("reachable now, the best first"));
			for (size_t i = 0; i < a->catchNow.size(); i++)
			{
				const CatchSuggestion& c = a->catchNow[i];
				bool first = i == 0;
				box(first ? "best" : "bubble", 12, y, 616, 58);
				stage(c.species, false, 0, 0, 22, y + 8, 42, 42, false);
				float nw = text(speciesName(c.species), true, 15, 74, y + 9, p.ink, true, 150);
				const Species* sp = species(c.species);
				if (sp) typeChips(sp->types, 74 + nw + 10, y + 8);
				text(c.where, false, 12, 74, y + 33, first ? p.accent : p.muted, true, 230);
				wrap(c.why, 12, 330, y + 12, 284, 2, p.ink, true);
				uint16_t spc = c.species;
				hit(12, y, 616, 58, [this, spc] { o.showSpecies(spc); });
				y += 66;
			}
		}
		// the TM planner
		if (a && !a->tmPlan.empty())
		{
			y = heading(tr("TMs to use"), 16, y + 4, tr("from your bag, for a gap"));
			for (auto& t : a->tmPlan)
			{
				box("slot", 12, y, 616, 40);
				std::string tn = itemName(t.first);
				text(tn, true, 13, 26, y + 12, p.accent, true, 150);
				text(t.second, false, 12, 186, y + 13, p.ink, true, 430);
				y += 46;
			}
		}
		if (dt.journey && dt.journey->challenge)
		{
			std::string c = tr("Challenge mode") + ": " + std::to_string(dt.journey->lost.size()) + " " + tr("lost") + ", " +
				std::to_string(dt.journey->routes.size()) + " " + tr("routes used");
			text(c, false, 12, 16, y + 4, p.muted, true);
			y += 22;
		}
		endScroll(id, y + 6);
	}

	// ---- Here ------------------------------------------------------------------------------------------------------------
	void Pages::Impl::here()
	{
		const Live& lv = *dt.live;
		const Location* loc = nullptr;
		auto z = dt.rom->zones.find(lv.zone);
		if (z != dt.rom->zones.end()) loc = &z->second;
		// the wild ones, one row each (a species met more ways: a row for each way)
		// (condition: which of a kind's lists - the headbutt tree kind, the GBA game, the swarm's way: each list's odds
		// add up to 100 on their own, so they are rows of their own, not summed: Hoothoot showed 100% on Route 30)
		struct Row { uint16_t species; EncounterKind kind; uint8_t cond = 0; uint8_t lo = 255, hi = 0; int pct[4] = {}; bool timed = false; };
		std::vector<Row> rows;
		// (Gen V: the season the odds are for, the handheld's month or the player's pick; it matters where the place
		// keeps a table per season)
		const int season = o.season();
		const bool seasonal = loc && season >= 0 &&
			std::any_of(loc->encounters.begin(), loc->encounters.end(), [](const Encounter& e) { return e.season != 0xff; });
		if (loc)
			for (auto& e : loc->encounters)
			{
				// (Gen V stores a table per season on any kind of encounter: only this season's counts, or the odds add up over four)
				if (e.season != 0xff && e.season != season) continue;
				const uint8_t cond = listed(e.kind) ? e.condition : 0;
				auto it = std::find_if(rows.begin(), rows.end(), [&](const Row& r) { return r.species == e.species && r.kind == e.kind && r.cond == cond; });
				if (it == rows.end()) { rows.push_back(Row{ e.species, e.kind, cond }); it = rows.end() - 1; }
				it->lo = std::min(it->lo, e.minLevel); it->hi = std::max(it->hi, e.maxLevel);
				if (e.time == TimeOfDay::ANY) for (int t = 1; t < 4; t++) it->pct[t] += e.percent;
				else { it->pct[(int)e.time] += e.percent; it->timed = true; }
			}
		std::set<uint16_t> kinds;
		int got = 0, total = 0;
		{
			std::set<uint16_t> sps;
			for (auto& r : rows) sps.insert(r.species);
			total = (int)sps.size();
			for (uint16_t s2 : sps) got += caught(s2);
		}
		std::string sub = total ? std::to_string(got) + " " + tr("of") + " " + std::to_string(total) + " " + tr("caught here") : tr("No wild Pokémon here");
		sub += ", " + tr(timeName(lv.timeOfDay));
		if (lv.repelSteps) sub += ", " + tr("repel") + " " + std::to_string(lv.repelSteps) + " " + tr("steps");
		std::string title = loc && !loc->name.empty() ? loc->name : tr("Here");
		header(title, sub, loc && !loc->name.empty(), true, !(loc && !loc->name.empty()));
		int id = scrollId(Page::HERE, 0);
		float y = beginScroll(id, 0, 56, 640, 362) + 2;
		notices(Page::HERE, 12, y, 616);

		// the season the odds below are for, and the four to pick from: the game's own is not readable, the handheld's
		// month is the first guess (a game resumed from a state keeps the season it started in)
		if (seasonal)
		{
			box("bubble", 12, y, 616, 44);
			text(tr("Season"), true, 14, 26, y + 7, p.ink);
			text(tr(o.pickedSeason >= 0 ? "picked by you" : "a guess from the date: tap your game's"), false, 12, 26, y + 25, p.muted, false, 270);
			const float cw = 78, x0 = 628 - 4 * cw;
			box("well", x0 - 2, y + 7, 4 * cw - 8, 30);
			for (int i = 0; i < 4; i++)
			{
				const bool on = i == season;
				if (on) box("pick", x0 + i * cw, y + 9, cw - 12, 26);
				std::string l = tr(seasonName((uint8_t)i));
				float lw = measure(l, true, 13, true);
				text(l, true, 13, x0 + i * cw + (cw - 12) / 2 - lw / 2, y + 13, on ? p.onPick : p.muted, true);
				hit(x0 + i * cw - 4, y, cw, 44, [this, i] { o.pickedSeason = i; });
			}
			y += 52;
		}

		// challenge mode: this route's first encounter
		if (dt.journey && dt.journey->challenge)
		{
			const RouteRecord* rr = nullptr;
			for (auto& r : dt.journey->routes) if (r.zone == lv.zone) rr = &r;
			box("bubble", 12, y, 616, 44);
			pixels(PX_SKULL, 7, 26, y + 15, k(), [&](char) { return p.accent; });
			std::string t;
			if (!rr || !rr->species) t = tr("Challenge: your first encounter here is the one you may catch");
			else t = tr("Challenge: first encounter here") + ": " + speciesName(rr->species) + ", " +
				tr(rr->outcome == RouteRecord::CAUGHT ? "caught" : rr->outcome == RouteRecord::MISSED ? "missed, no catch here" : "not caught yet");
			text(t, true, 13, 48, y + 14, p.ink, true, 566);
			y += 52;
		}
		// the encounter table, by how they're met
		if (!rows.empty())
		{
			// by how they're met, then the likeliest now first
			int nowCol = lv.timeOfDay == TimeOfDay::ANY ? 2 : (int)lv.timeOfDay;
			std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b)
				{ return a.kind != b.kind ? a.kind < b.kind : a.cond != b.cond ? a.cond < b.cond : a.pct[nowCol] > b.pct[nowCol]; });
			EncounterKind last = (EncounterKind)255;
			int lastCond = -1;
			int tcol = (int)lv.timeOfDay;
			for (auto& r : rows)
			{
				if (r.kind != last || r.cond != lastCond)
				{
					last = r.kind;
					lastCond = r.cond;
					const char* list = listName(r.kind, r.cond);
					y = heading(tr(kindName(r.kind)), 16, y + 2, list ? list : "");
					// the times of day as columns when this way of meeting them has them
					bool timed = std::any_of(rows.begin(), rows.end(), [&](const Row& x) { return x.kind == r.kind && x.timed; });
					if (timed)
						for (int t = 1; t < 4; t++)
						{
							std::string tn = tr(timeName((TimeOfDay)t));
							float cx = 420 + (t - 1) * 66 + 33;
							text(tn, t == tcol, 12, cx - measure(tn, t == tcol, 12) / 2, y - 22, t == tcol ? p.accent : p.muted);
						}
				}
				bool groupTimed = std::any_of(rows.begin(), rows.end(), [&](const Row& x) { return x.kind == r.kind && x.timed; });
				box("slot", 12, y, 616, 40);
				int state = caught(r.species) ? 1 : seen(r.species) ? 0 : -1;
				mon(r.species, false, 0, 0, 20, y + 3, 40, 34, false);
				bool none = groupTimed && tcol >= 1 && tcol <= 3 && r.pct[tcol] == 0;	// (not at this time of day)
				text(speciesName(r.species), true, 14, 70, y + 11, none ? p.muted : p.ink, true, 150);
				if (state >= 0) ball(226, y + 13, state, std::max(1, (int)std::lround(1.4f * s)));
				std::string lvs = tr("Lv") + " " + std::to_string(r.lo) + (r.hi != r.lo ? "-" + std::to_string(r.hi) : "");
				text(lvs, false, 12, 250, y + 13, p.muted, true);
				const Species* sp = species(r.species);
				if (sp) typeChip(sp->types[0], 330, y + 11);
				if (groupTimed)
					for (int t = 1; t < 4; t++)
					{
						std::string pc = std::to_string(r.pct[t]) + "%";
						float cx = 420 + (t - 1) * 66 + 33;
						if (t == tcol) box("well", cx - 28, y + 7, 56, 26);
						text(pc, true, 13, cx - measure(pc, true, 13, true) / 2, y + 12, t == tcol ? p.accent : r.pct[t] ? p.ink : p.line, true);
					}
				else
				{
					std::string pc = std::to_string(r.pct[1]) + "%";
					textR(pc, true, 14, 614, y + 11, p.ink, true);
				}
				uint16_t spc = r.species;
				hit(12, y, 616, 40, [this, spc] { o.showSpecies(spc); });
				y += 44;
			}
			y += 6;
		}
		// hidden items and secrets here
		if (dt.guide)
		{
			bool any = false;
			for (auto& sc : dt.guide->secrets)
			{
				// (a place's secret holds all its zones - a town and its houses - as the badge counts them, logic.cpp)
				if (!lv.zone || (sc.zone != lv.zone && std::find(sc.zones.begin(), sc.zones.end(), lv.zone) == sc.zones.end())) continue;
				if (!any) { y = heading(tr("Secrets and hidden items"), 16, y + 2); any = true; }
				float th = std::max(17.0f, wrapHeight(sc.text, 12, sc.x >= 0 ? 470 : 540, 2, true));
				box("slot", 12, y, 616, th + 18);
				pixels(PX_SPARK, 7, 24, y + 11, k(), [&](char) { return p.accent; });
				wrap(sc.text, 12, 46, y + 9, sc.x >= 0 ? 470 : 540, 2, p.ink, true);
				if (sc.x >= 0) textR("x " + std::to_string(sc.x) + ", y " + std::to_string(sc.y), false, 12, 588, y + 10, p.muted, true);
				if (sc.q == Quality::REFERENCE_ONLY) unchecked(600, y + 10);
				y += th + 24;
			}
		}
		// the roamers: where each is now
		bool roamers = false;
		for (auto& r : lv.roamers)
		{
			if (!r.active) continue;
			if (!roamers) { y = heading(tr("Roaming"), 16, y + 2, tr("they move each time you change areas")); roamers = true; }
			bool hereNow = r.zone == lv.zone;
			box(hereNow ? "best" : "slot", 12, y, 616, 44);
			mon(r.species, false, 0, seen(r.species) ? 0 : 2, 20, y + 4, 40, 36, false);
			text(speciesName(r.species), true, 14, 70, y + 13, p.ink, true, 140);
			text(tr("Lv") + " " + std::to_string(r.level), false, 12, 220, y + 15, p.muted, true);
			std::string zn = zoneName(r.zone);
			text(hereNow ? tr("Here, now") : zn, true, 13, 290, y + 14, hereNow ? p.accent : p.ink, !hereNow, 200);
			if (r.hp) textR(tr("HP") + " " + std::to_string(r.hp), false, 12, 614, y + 15, p.muted, true);
			y += 50;
		}
		// the daycare
		if (!lv.daycare.empty())
		{
			y = heading(tr("Day Care"), 16, y + 2);
			box("slot", 12, y, 616, 52);
			float cx = 20;
			for (auto& m : lv.daycare)
			{
				mon(m.species, m.female, m.form, m.shiny() ? 1 : 0, cx, y + 6, 40, 40, false);
				float nw = monName(monLabel(m), gender(m), 13, cx + 46, y + 10, 140, p.ink);
				text(tr("Lv") + " " + std::to_string(m.level), false, 12, cx + 46, y + 29, p.muted, true);
				(void)nw;
				cx += 210;
			}
			std::string st = std::to_string(lv.eggSteps) + " " + tr("steps");
			textR(st, true, 13, 614, y + 18, p.ink, true);
			y += 60;
		}
		if (rows.empty() && !roamers && lv.daycare.empty())
		{
			box("bubble", 12, y, 616, 70);
			wrap(tr("No wild Pokémon live here. Their odds, hidden items and roamers show on this page wherever there are some."), 13, 30, y + 16, 580, 2, p.muted);
			y += 78;
		}
		endScroll(id, y + 4);
	}

	// ---- Guide -----------------------------------------------------------------------------------------------------------
	void Pages::Impl::guide()
	{
		const GuideData* g = dt.guide.get();
		const Advice* a = dt.advice.get();
		header(tr("Guide"), g ? tr("What's next, and how to get ready") : tr("No guide for this game"));
		if (!g) return;
		int id = scrollId(Page::GUIDE, 0);
		float y = beginScroll(id, 0, 56, 640, 362) + 2;
		notices(Page::GUIDE, 12, y, 616);
		int nextIdx = -1;
		if (a && a->next)
			for (size_t i = 0; i < g->milestones.size(); i++) if (&g->milestones[i] == a->next || g->milestones[i].id == a->next->id) nextIdx = (int)i;
		// the next goal: who, where, their team with the levels, what to prepare
		if (nextIdx >= 0)
		{
			const Milestone& ms = g->milestones[nextIdx];
			const Trainer* t = nullptr;
			if (!ms.trainerIds.empty()) { auto it = dt.rom->trainers.find(ms.trainerIds[0]); if (it != dt.rom->trainers.end()) t = &it->second; }
			float h = 92 + (t ? 64 : 0) + (a ? 18.0f * std::min<size_t>(4, a->nextProblems.size()) : 0);
			box("hero", 12, y, 616, h);
			text(tr("Next"), false, 12, 28, y + 12, p.accent);
			if (ms.q == Quality::REFERENCE_ONLY) unchecked(600, y + 14);
			text(ms.name, true, 20, 28, y + 28, p.ink, true, 440);
			float cx = 28, cy = y + 56;
			if (ms.theme < NUM_TYPES) cx += typeChip(ms.theme, cx, cy) + 8;
			if (goalWithPlace(ms) != ms.name) text(ms.place, false, 13, cx, cy + 1, p.muted, true);	// (not when the name has it)
			float ty = y + 84;
			if (t)
			{
				size_t nt = std::min<size_t>(6, t->team.size());
				float cwid = std::min(190.0f, 584.0f / std::max<size_t>(1, nt));
				for (size_t i = 0; i < nt; i++)
				{
					const TrainerMon& tm = t->team[i];
					float sx = 28 + i * cwid;
					stage(tm.species, false, 0, seen(tm.species) ? 0 : 2, sx, ty, 44, 44, false);
					text(seen(tm.species) ? speciesName(tm.species) : "???", true, 13, sx + 52, ty + 7, p.ink, true, cwid - 58);
					text(tr("Lv") + " " + std::to_string(tm.level), false, 12, sx + 52, ty + 25, p.muted, true);
				}
				ty += 58;
			}
			if (a)
				for (size_t i = 0; i < a->nextProblems.size() && i < 4; i++)
				{
					fill(28, ty + 6, 4, 4, p.accent);
					text(a->nextProblems[i], false, 12, 40, ty, p.ink, true, 576);
					ty += 18;
				}
			y += h + 10;
		}
		// along the way: the guide's steps, the ones in this area first
		if (!g->steps.empty())
		{
			std::vector<const GuideStep*> st;
			uint16_t zn = dt.live->zone;
			// a step belongs to its stage (the milestone it is shown during, model.h): only the current goal's, never a
			// later or an earlier one's (HeartGold's post-game Embedded Tower step showed on Route 30 at the start
			// because it names Route 30; Platinum's "Pick your first partner" after the partner was picked). Steps
			// without a known stage show as before.
			auto shown = [&](const GuideStep& s2)
			{
				if (nextIdx < 0 || s2.stage.empty()) return true;
				int si = -1;
				for (size_t i = 0; i < g->milestones.size(); i++) if (g->milestones[i].id == s2.stage) si = (int)i;
				return si < 0 || si == nextIdx;
			};
			for (auto& s2 : g->steps)
				if (std::find(s2.zones.begin(), s2.zones.end(), zn) != s2.zones.end() && shown(s2)) st.push_back(&s2);
			for (auto& s2 : g->steps)
				if (std::find(s2.zones.begin(), s2.zones.end(), zn) == s2.zones.end() && shown(s2)) st.push_back(&s2);
			if (!st.empty()) y = heading(tr("Along the way"), 16, y + 2);
			for (size_t i = 0; i < st.size() && i < 8; i++)
			{
				const GuideStep& s2 = *st[i];
				bool hereNow = std::find(s2.zones.begin(), s2.zones.end(), zn) != s2.zones.end();
				float th = wrapHeight(s2.text, 12, 576, 3, true);
				box("slot", 12, y, 616, 30 + th);
				if (hereNow) fill(18, y + 9, 3, 12 + th, p.accent);
				text(s2.title, true, 14, 28, y + 8, p.ink, true, 560);
				if (s2.q == Quality::REFERENCE_ONLY) unchecked(600, y + 10);
				if (th > 0) wrap(s2.text, 12, 28, y + 27, 576, 3, p.muted, true);
				y += 36 + th;
			}
			y += 4;
		}
		// the road: every milestone, done ones ticked
		if (!g->milestones.empty())
		{
			y = heading(tr("The road"), 16, y + 2, tr("gyms, rivals and the League in story order"));
			float lx = 30;
			for (size_t i = 0; i < g->milestones.size(); i++)
			{
				const Milestone& ms = g->milestones[i];
				bool done = nextIdx >= 0 && (int)i < nextIdx, next = (int)i == nextIdx;
				if (i + 1 < g->milestones.size()) fill(lx + 5, y + 14, 2, 22, done ? p.accent : p.line);
				if (done) pixels(PX_CHECK, 7, lx - 1, y + 3, k(), [&](char) { return p.accent; });
				else
				{
					fill(lx, y + 4, 12, 12, next ? p.accent : p.line);
					if (!next) fill(lx + 2, y + 6, 8, 8, light ? p.screenBand : p.screenBg);
				}
				text(ms.name, next, 13, lx + 22, y + 2, done ? p.muted : p.ink, true, 300);
				if (goalWithPlace(ms) != ms.name) text(ms.place, false, 12, 360, y + 3, p.muted, true, 200);
				if (ms.theme < NUM_TYPES) typeChip(ms.theme, 560, y);
				y += 24;
			}
			y += 8;
		}
		// how this game's evolutions work, for the party
		{
			bool any = false;
			std::set<uint16_t> done;
			for (auto& m : dt.live->party)
			{
				auto it = g->evolutionNotes.find(m.species);
				if (it == g->evolutionNotes.end() || done.count(m.species)) continue;
				done.insert(m.species);
				if (!any) { y = heading(tr("Evolving your team"), 16, y + 2); any = true; }
				float th = wrapHeight(it->second, 12, 540, 3, true);
				box("slot", 12, y, 616, 32 + th);
				mon(m.species, m.female, m.form, 0, 20, y + 6, 40, 38, false);
				text(speciesName(m.species), true, 13, 70, y + 8, p.ink, true, 200);
				wrap(it->second, 12, 70, y + 26, 540, 3, p.muted, true);
				y += 38 + th;
			}
		}
		// where the guide's facts come from
		if (!g->sources.empty())
		{
			y += 6;
			pixels(PX_DIAMOND, 7, 16, y + 3, std::max(1, (int)std::lround(s * 1.5f)), [&](char) { return p.muted; });
			text(tr("From guides, not yet checked in a running game"), false, 12, 32, y, p.muted);
			y += 20;
			std::string src = tr("Sources") + ": ";
			for (size_t i = 0; i < g->sources.size(); i++) src += (i ? ", " : "") + g->sources[i];
			y += wrap(src, 11, 16, y, 608, 3, p.muted, true);
		}
		endScroll(id, y + 8);
	}

	// ---- Dex -------------------------------------------------------------------------------------------------------------
	void Pages::Impl::dex()
	{
		const Live& lv = *dt.live;
		int n = dexSize(), seenN = 0, caughtN = 0;
		for (int i = 1; i <= n; i++) { seenN += seen(i); caughtN += caught(i); }
		std::string sub = tr("Caught") + " " + std::to_string(caughtN) + ", " + tr("seen") + " " + std::to_string(seenN) + " " + tr("of") + " " + std::to_string(n);
		header(tr("Pokédex"), sub, false, true);
		(void)lv;
		// the list on the left (drag it, or along its rail for the whole Pokédex)
		box("bubble", 12, 58, 196, 358);
		int lid = scrollId(Page::DEX, 0);
		const float rowH = DEX_ROW;
		Scroll& ls = scrolls[lid];
		ls.max = std::max(0.0f, n * rowH + 4 - 346);
		float top = beginScroll(lid, 15, 64, 176, 346);
		int first = std::max(1, (int)((ls.y) / rowH)), last = std::min(n, (int)((ls.y + 346) / rowH) + 2);
		for (int i = first; i <= last; i++)
		{
			float y = top + (i - 1) * rowH;
			bool on = i == o.dexSel;
			if (on) box("pick", 17, y + 1, 172, rowH - 2);
			char num[16]; snprintf(num, sizeof num, "%03d", i);
			Color ink = on ? p.onPick : p.ink, mut = on ? p.onPickAccent : p.muted;
			text(num, false, 12, 26, y + 11, mut, true);
			bool sn = seen(i);
			text(sn ? speciesName(i) : "???", true, 14, 60, y + 10, sn ? ink : mut, true, 100);
			if (caught(i)) ball(166, y + 12, 1, std::max(1, (int)std::lround(1.2f * s)));
			else if (sn) ball(166, y + 12, 0, std::max(1, (int)std::lround(1.2f * s)));
			hit(15, y, 176, rowH, [this, i] { o.dexSel = i; scrolls[scrollId(Page::DEX, 1)].y = 0; });
		}
		endScroll(lid, top + n * rowH + 4);
		rail = { 192, 64, 14, 346, lid };
		// the detail on the right
		box("hero", 216, 58, 412, 358);
		int did = scrollId(Page::DEX, 1);
		float y = beginScroll(did, 219, 61, 406, 352) + 9;
		notices(Page::DEX, 230, y, 384);
		float bottom = y;
		dexDetail(o.dexSel, 230, y, 384, bottom);
		endScroll(did, bottom + 8);
	}

	void Pages::Impl::dexDetail(uint16_t n, float x, float y, float w, float& bottom)
	{
		const Species* sp = species(n);
		bool sn = seen(n), got = caught(n);
		// the picture: big, animated with the game's own frames; Normal and Shiny under it
		float st = 150;
		stage(n, false, 0, sn ? (o.dexShiny ? 1 : 0) : 2, x, y, st, st, sn);
		float sy = y + st + 8;
		if (sn)
		{
			box("well", x, sy, st, 26);
			float hw = st / 2;
			for (int i = 0; i < 2; i++)
			{
				bool on = (i == 1) == o.dexShiny;
				if (on) box("pick", x + 2 + i * hw, sy + 2, hw - 4, 22);
				std::string l = tr(i ? "Shiny" : "Normal");
				float lw = measure(l, true, 13, true);
				text(l, true, 13, x + i * hw + hw / 2 - lw / 2, sy + 5, on ? p.onPick : p.muted, true);
				bool shiny = i == 1;
				hit(x + i * hw, sy - 4, hw, 34, [this, shiny] { o.dexShiny = shiny; });
			}
		}
		// the name and what it is
		float tx = x + st + 14, tw = x + w - tx;
		char num[16]; snprintf(num, sizeof num, "%03d", n);
		text(tr("No.") + " " + num, false, 12, tx, y, p.muted, true);
		if (got) ball(x + w - 14, y + 1, 1, std::max(1, (int)std::lround(1.3f * s)));
		text(sn && sp ? sp->name : "???", true, 20, tx, y + 16, p.ink, true, tw);
		if (!sn || !sp)
		{
			wrap(tr("Not seen yet. Its page fills in once you have met one."), 13, tx, y + 48, tw, 3, p.muted);
			bottom = sy + 4;
			return;
		}
		std::string cat = sp->category;
		if (!cat.empty()) text(cat, false, 12, tx, y + 42, p.muted, true, tw);
		typeChips(sp->types, tx, y + 60);
		if (sp->height || sp->weight)
		{
			char hw2[64];
			snprintf(hw2, sizeof hw2, "%d.%d m   %d.%d kg", sp->height / 10, sp->height % 10, sp->weight / 10, sp->weight % 10);
			text(hw2, false, 12, tx, y + 84, p.muted, true);
		}
		// abilities (the hidden one, Gen V, quieter)
		float ay = y + 102;
		text(tr("Ability"), false, 11, tx, ay, p.muted);
		ay += 15;
		for (int i = 0; i < 3; i++)
		{
			std::string ab = abilityName(sp->abilities[i]);
			if (ab.empty() || (i == 1 && sp->abilities[1] == sp->abilities[0])) continue;
			float aw = text(ab, true, 13, tx, ay, i == 2 ? p.muted : p.ink, true, tw);
			if (i == 2) text(tr("hidden"), false, 11, tx + aw + 6, ay + 2, p.muted);
			ay += 18;
		}
		y = std::max(sy + 36, ay + 8);
		// base stats
		y = heading(tr("Base stats"), x, y);
		int total = 0;
		for (int i = 0; i < 6; i++)
		{
			total += sp->base[i];
			float ry = y + i * 18;
			text(STAT_NAMES[i], false, 12, x, ry, p.muted);
			std::string v = std::to_string(sp->base[i]);
			textR(v, true, 12, x + 96, ry, p.ink, true);
			float f = sp->base[i] / 180.0f;
			bar(x + 104, ry + 4, w - 104, 7, f, sp->base[i] >= 100 ? Color::hex(GOOD) : sp->base[i] >= 60 ? Color::hex(HP_YELLOW) : Color::hex(BAD));
		}
		y += 6 * 18;
		text(tr("Total"), false, 12, x, y, p.muted);
		textR(std::to_string(total), true, 12, x + 96, y, p.ink, true);
		y += 26;
		// the Pokédex text (the game's)
		if (!sp->dexText.empty()) y += wrap(sp->dexText, 13, x, y, w, 6, p.ink, true) + 10;
		// the evolution family: from its first stage
		{
			uint16_t root = n;
			for (int guard = 0; guard < 4; guard++)
			{
				bool found = false;
				for (size_t i = 1; i < dt.rom->species.size() && !found; i++)
					for (auto& e : dt.rom->species[i].evolutions) if (e.into == root) { root = (uint16_t)i; found = true; break; }
				if (!found) break;
			}
			std::vector<std::pair<uint16_t, const Evolution*>> chain{ { root, nullptr } };
			for (size_t i = 0; i < chain.size() && chain.size() < 8; i++)
			{
				const Species* cs = species(chain[i].first);
				if (cs) for (auto& e : cs->evolutions) chain.push_back({ e.into, &e });
			}
			if (chain.size() > 1)
			{
				y = heading(tr("Evolution"), x, y);
				// a cell each: the stage, the name under it, how it's reached; arrows between (rows of four at most)
				size_t perRow = std::min<size_t>(4, chain.size());
				float cw = std::min(120.0f, (w - 16 * (perRow - 1)) / perRow);
				float cx = x;
				for (size_t i = 0; i < chain.size(); i++)
				{
					uint16_t cs = chain[i].first;
					if (i && i % perRow == 0) { cx = x; y += 100; }
					else if (i) { pixels(PX_ARROW_R, 7, cx - 12, y + 18, k(), [&](char) { return p.line; }); }
					bool cseen = seen(cs);
					stage(cs, false, 0, cseen ? 0 : 2, cx + cw / 2 - 24, y, 48, 48, false);
					std::string nm = cseen ? speciesName(cs) : "???";
					float nw = std::min(cw, measure(nm, true, 12, true));
					text(nm, true, 12, cx + cw / 2 - nw / 2, y + 54, cs == n ? p.accent : p.ink, true, cw);
					if (chain[i].second)
					{
						const std::string& how = chain[i].second->how;
						float hw = std::min(cw, measure(how, false, 11, true));
						if (hw < cw) text(how, false, 11, cx + cw / 2 - hw / 2, y + 70, p.muted, true);
						else wrap(how, 11, cx, y + 70, cw, 2, p.muted, true, false, 13);
					}
					if (cs != n) hit(cx, y, cw, 90, [this, cs] { o.dexSel = cs; scrolls[scrollId(Page::DEX, 1)].y = 0; });
					cx += cw + 16;
				}
				y += 100;
				if (dt.guide)
				{
					auto it = dt.guide->evolutionNotes.find(n);
					if (it != dt.guide->evolutionNotes.end()) y += wrap(it->second, 12, x, y, w, 3, p.muted, true) + 6;
				}
				y += 6;
			}
		}
		// what it takes extra damage from (and nothing from)
		{
			std::vector<std::pair<Type, int>> weak, immune;
			for (int a = 0; a < NUM_TYPES; a++)
			{
				int q = effectiveness((Type)a, sp->types[0], sp->types[1]);
				if (q > 4) weak.push_back({ (Type)a, q });
				else if (q == 0) immune.push_back({ (Type)a, q });
			}
			std::stable_sort(weak.begin(), weak.end(), [](auto& a, auto& b) { return a.second > b.second; });
			if (!weak.empty() || !immune.empty())
			{
				y = heading(tr("Weak to"), x, y);
				float cx = x;
				for (auto& wk : weak)
				{
					std::string l = trType(wk.first) + " " + times(wk.second);
					float cw = chip(l, TYPE_COLORS[wk.first], 0, 0, true, true);
					if (cx + cw > x + w) { cx = x; y += 24; }
					chip(l, TYPE_COLORS[wk.first], cx, y);
					cx += cw + 5;
				}
				y += 26;
				if (!immune.empty())
				{
					float iw = text(tr("Immune to"), false, 12, x, y + 2, p.muted);
					float cx2 = x + iw + 8;
					for (auto& im : immune) cx2 += typeChip(im.first, cx2, y) + 5;
					y += 26;
				}
				y += 4;
			}
		}
		// where it lives in this game
		{
			std::vector<std::string> where;
			for (auto& z : dt.rom->zones)
			{
				int pct = 0; uint8_t lo = 255, hi = 0; EncounterKind kind = EncounterKind::GRASS;
				for (auto& e : z.second.encounters)
					if (e.species == n) { pct = std::max<int>(pct, e.percent); lo = std::min(lo, e.minLevel); hi = std::max(hi, e.maxLevel); kind = e.kind; }
				if (pct) where.push_back(z.second.name + "|" + tr(kindName(kind)) + ", " + tr("Lv") + " " + std::to_string(lo) + (hi != lo ? "-" + std::to_string(hi) : ""));
			}
			y = heading(tr("Where it lives"), x, y);
			if (where.empty()) { text(tr("Not found in the wild in this game"), false, 12, x, y, p.muted); y += 22; }
			for (size_t i = 0; i < where.size() && i < 12; i++)
			{
				size_t bar2 = where[i].find('|');
				text(where[i].substr(0, bar2), true, 13, x, y, p.ink, true, 190);
				text(where[i].substr(bar2 + 1), false, 12, x + 196, y + 1, p.muted, true, w - 196);
				y += 19;
			}
			y += 8;
		}
		// the moves it learns by level
		if (!sp->learnset.empty())
		{
			y = heading(tr("Learns"), x, y, tr("by level"));
			for (auto& lm : sp->learnset)
			{
				const MoveData* md = move(lm.move);
				text(lm.level ? tr("Lv") + " " + std::to_string(lm.level) : tr("Start"), false, 12, x, y + 1, p.muted, true);
				text(moveName(lm.move), true, 13, x + 50, y, p.ink, true, 160);
				if (md)
				{
					typeChip(md->type, x + 220, y - 1);
					if (md->power) textR(std::to_string(md->power), false, 12, x + w, y + 1, p.muted, true);
				}
				y += 21;
			}
		}
		bottom = y;
	}

	// ---- Log ---------------------------------------------------------------------------------------------------------------
	void Pages::Impl::log()
	{
		const Live& lv = *dt.live;
		const Journey* j = dt.journey.get();
		// (the log holds what happened while the companion ran: its catches, not the Pokédex's, which says "Caught N")
		int catches = 0;
		if (j) for (auto& e : j->log) catches += e.kind == LogKind::CATCH;
		std::string sub = tr("Played") + " " + playTime(lv.playSeconds);
		if (j && catches) sub += ", " + std::to_string(catches) + " " + tr(catches == 1 ? "catch logged" : "catches logged");
		header(tr("Journey"), sub, false, true);
		int id = scrollId(Page::LOG, 0);
		float y = beginScroll(id, 0, 56, 640, 362) + 2;
		notices(Page::LOG, 12, y, 616);
		// challenge mode: the routes and the lost
		if (j && j->challenge)
		{
			y = heading(tr("Challenge"), 16, y + 2, std::to_string(j->routes.size()) + " " + tr("routes") + ", " + std::to_string(j->lost.size()) + " " + tr("lost"));
			float cx = 12;
			for (auto& r : j->routes)
			{
				std::string zn = zoneName(r.zone);
				uint32_t c = r.outcome == RouteRecord::CAUGHT ? 0x3c8c50 : r.outcome == RouteRecord::MISSED ? 0xa84848 : (light ? 0xb8b0a0 : 0x4a4650);
				float cw = chip(zn, c, 0, 0, true, true, 20);
				if (cx + cw > 628) { cx = 12; y += 26; }
				chip(zn, c, cx, y, true, false, 20);
				cx += cw + 6;
			}
			y += 30;
			if (!j->lost.empty())
			{
				box("bubble", 12, y, 616, 70);
				pixels(PX_SKULL, 7, 26, y + 10, k(), [&](char) { return p.muted; });
				text(tr("Lost"), true, 13, 46, y + 9, p.muted);
				float lx = 26;
				for (auto& m : j->lost)
				{
					if (lx > 560) break;
					mon(m.species, m.female, m.form, 2, lx, y + 28, 36, 32, false);
					text(monLabel(m), false, 11, lx + 38, y + 40, p.muted, true, 54);
					lx += 96;
				}
				y += 80;
			}
		}
		if (!j || j->log.empty())
		{
			box("bubble", 12, y, 616, 80);
			text(tr("Your journey starts here"), true, 16, 30, y + 14, p.ink);
			wrap(tr("Catches, evolutions, badges and firsts are noted here as they happen, with the play time."), 13, 30, y + 40, 580, 2, p.muted);
			endScroll(id, y + 88);
			return;
		}
		// the timeline, newest first: the play time, a mark, the Pokémon, what happened
		auto markColour = [](LogKind k) -> uint32_t
		{
			switch (k)
			{
			case LogKind::CATCH: return 0xe8504a;
			case LogKind::GIFT: return 0xa070e0;	// (received: a gift, a trade)
			case LogKind::EVOLVE: return EXP_BLUE;
			case LogKind::BADGE: return rnds::accent.base & 0xffffff;
			case LogKind::LOST: return 0x8a8890;
			case LogKind::HATCH: return 0xf07890;
			default: return 0x58d068;
			}
		};
		y = heading(tr("Timeline"), 16, y + 2);
		float lineX = 92;
		float top = y;
		for (size_t r = j->log.size(); r-- > 0;)
		{
			const LogEntry& e = j->log[r];
			const uint32_t mc = markColour(e.kind);
			textR(playTime(e.playSeconds), false, 12, lineX - 14, y + 13, p.muted, true);
			if (r + 1 < j->log.size() || y > top) {}
			fill(lineX - 5, y + 13, 12, 12, Color::hex(mc));
			fill(lineX - 3, y + 15, 8, 8, Color::hex(mc));
			float tx = lineX + 22;
			if (e.species)
			{
				mon(e.species, false, 0, e.kind == LogKind::LOST ? 2 : 0, tx, y + 1, 40, 36, false);
				tx += 48;
			}
			else if (e.kind == LogKind::BADGE)
			{
				int kk = std::max(1, (int)std::lround(2.5f * s));
				pixels(PX_BADGE, 8, tx + 10, y + 9, kk, [&](char c) { return c == 'H' ? Color::hex(0xfff6d0) : p.accent; });
				tx += 48;
			}
			std::string zn = zoneName(e.zone);
			text(e.text, true, 13, tx, y + 5, p.ink, true, 628 - tx - 8);
			if (!zn.empty()) text(zn, false, 12, tx, y + 23, p.muted, true, 300);
			y += 46;
		}
		fill(lineX, top, 2, y - top - 6, p.line);
		// (the marks over the line: drawn again on top)
		y = top;
		for (size_t r = j->log.size(); r-- > 0;)
		{
			const LogEntry& e = j->log[r];
			const uint32_t mc = markColour(e.kind);
			fill(lineX - 5, y + 13, 12, 12, light ? p.screenBand : p.screenBg);
			fill(lineX - 3, y + 15, 8, 8, Color::hex(mc));
			y += 46;
		}
		endScroll(id, y + 6);
	}

	// ---- Pages ---------------------------------------------------------------------------------------------------------
	Pages::Pages(rnds::Backend* be, int screen, int w, int h, const std::string& assets, bool light)
		: d(new Impl(*this, be, screen, w, h, assets, light)) {}
	Pages::~Pages() = default;

	void Pages::setData(const PageData& data)
	{
		d->dt = data;
		std::fill(std::begin(d->warmed), std::end(d->warmed), false);	// (new texts to make ready)
		if (data.rom && dexSel > data.rom->game.dexSize) dexSel = 1;
		d->askAhead();
	}
	bool Pages::warmNext()
	{
		if (d->dt.reading || !d->dt.rom || !d->dt.live || !d->dt.live->valid) return false;
		for (int i = 0; i < (int)Page::COUNT; i++)
		{
			if (d->warmed[i]) continue;
			d->warmed[i] = true;
			d->warm((Page)i);
			return true;
		}
		return false;
	}
	void Pages::setDecoderThread(bool on)
	{
		if (on) d->startDecoder();
	}
	void Pages::setLook(bool light)
	{
		d->light = light;
		d->p = pal(light);
		d->cache.clear(); d->wrapLines.clear(); d->sprites.clear();
		std::fill(std::begin(d->warmed), std::end(d->warmed), false);
		d->loadFonts();
	}
	void Pages::setCorner(int x, int y, int w, int h)
	{
		d->cornerX = x / d->s; d->cornerY = y / d->s; d->cornerW = w / d->s; d->cornerH = h / d->s;
	}
	void Pages::opened(double nowMs)
	{
		d->now = nowMs;
		// a battle on: the Battle page (what the player opens the companion for then; the notices wait), else the page
		// the oldest unseen notice is for, else where it was
		if (d->inBattle()) { show(Page::BATTLE); return; }
		if (d->dt.advice)
			for (auto& nt : d->dt.advice->notices)
			{
				Page pg = Impl::pageOf(nt.page);
				if (pg != Page::COUNT && d->noticeCount(pg) > 0) { show(pg); return; }
			}
		if (cur == Page::BATTLE) cur = Page::PARTY;
		show(cur);
	}
	void Pages::show(Page pg)
	{
		if (pg >= Page::COUNT) return;
		if (pg != cur) d->stopFling();
		cur = pg;
		d->markSeen(pg);
	}
	void Pages::showSpecies(uint16_t sp)
	{
		if (!sp) return;
		dexSel = sp;
		d->scrolls[d->scrollId(Page::DEX, 1)].y = 0;
		// the list scrolled so the row shows (in its middle)
		Impl::Scroll& ls = d->scrolls[d->scrollId(Page::DEX, 0)];
		ls.y = std::max(0.0f, (sp - 1) * DEX_ROW - 160);
		show(Page::DEX);
	}

	Pages::Action Pages::touchDown(int x, int y, double nowMs)
	{
		d->now = nowMs;
		float cx = x / d->s, cy = y / d->s;
		d->down = true; d->dragging = false; d->railDrag = false;
		d->downX = cx; d->downY = cy; d->lastY = cy; d->dragId = -1;
		d->trail.assign(1, { nowMs, cy });
		// a press on a list that is still flinging fast only stops it (as a phone's list); anywhere else, or once it has
		// slowed down, the press is what it would be anyway (a tab right after a fast drag is taken at once)
		d->stopTap = false;
		if (d->flingId >= 0)
		{
			auto f = d->scrolls.find(d->flingId);
			if (f != d->scrolls.end() && std::fabs(d->flingV) > Impl::FLING_TAP && cx >= f->second.vx && cx < f->second.vx + f->second.vw &&
				cy >= f->second.vy && cy < f->second.vy + f->second.vh)
				d->stopTap = true;
			d->stopFling();
		}
		// along the Dex list's rail: straight to that part of the Pokédex (the zone reaches left over the thin drawn track,
		// which sits at x 187-189: a finger on it must not land on a row)
		const auto& r = d->rail;
		if (cur == Page::DEX && r.id >= 0 && cx >= r.x - 18 && cx < r.x + r.w + 6 && cy >= r.y && cy < r.y + r.h)
		{
			d->railDrag = true; d->dragId = r.id;
			return touchMove(x, y, nowMs);
		}
		for (auto& sc : d->scrolls)
			if (sc.second.shown && cx >= sc.second.vx && cx < sc.second.vx + sc.second.vw && cy >= sc.second.vy && cy < sc.second.vy + sc.second.vh)
				d->dragId = sc.first;
		if (d->dragId >= 0) d->startScroll = d->scrolls[d->dragId].y;
		if (d->stopTap) return Action::REDRAW;
		for (size_t i = d->hits.size(); i-- > 0;)
		{
			const auto& h = d->hits[i];
			if (cx >= h.x && cx < h.x + h.w && cy >= h.y && cy < h.y + h.h)
			{
				d->pressed = true; d->pressX = h.x; d->pressY = h.y; d->pressW = h.w; d->pressH = h.h;
				return Action::REDRAW;
			}
		}
		return Action::NONE;
	}

	Pages::Action Pages::touchMove(int x, int y, double nowMs)
	{
		d->now = nowMs;
		if (!d->down) return Action::NONE;
		float cy = y / d->s;
		// (the last ~100 ms of the finger's way: the speed it lifts at)
		d->trail.push_back({ nowMs, cy });
		while (d->trail.size() > 2 && nowMs - d->trail.front().first > 100) d->trail.erase(d->trail.begin());
		if (d->railDrag)
		{
			Impl::Scroll& sc = d->scrolls[d->dragId];
			float f = std::max(0.0f, std::min(1.0f, (cy - d->rail.y) / d->rail.h));
			float ny = f * sc.max;
			if (ny == sc.y) return Action::NONE;
			sc.y = ny;
			return Action::REDRAW;
		}
		bool was = d->pressed;
		if (!d->dragging && std::fabs(cy - d->downY) > 7) { d->dragging = true; d->pressed = false; }
		if (!d->dragging || d->dragId < 0) return was != d->pressed ? Action::REDRAW : Action::NONE;
		Impl::Scroll& sc = d->scrolls[d->dragId];
		float ny = std::max(0.0f, std::min(sc.max, d->startScroll - (cy - d->downY)));
		if (ny == sc.y) return Action::NONE;
		sc.y = ny;
		return Action::REDRAW;
	}

	Pages::Action Pages::touchUp(int x, int y, double nowMs)
	{
		d->now = nowMs;
		if (!d->down) return Action::NONE;
		d->down = false;
		bool was = d->pressed;
		d->pressed = false;
		float cx = x / d->s, cy = y / d->s;
		if (d->railDrag) { d->railDrag = false; return Action::REDRAW; }
		if (d->dragging)
		{
			d->dragging = false;
			// let go while moving: the list flings on at the finger's speed over its last 100 ms (none when the finger
			// had stopped: its last move long before the lift, or no time between the moves to tell a speed by)
			if (d->dragId >= 0 && d->trail.size() >= 2)
			{
				const auto& a = d->trail.front();
				const auto& b = d->trail.back();
				const double dt = b.first - a.first;
				const float v = dt >= 8 && nowMs - b.first < 60 ? -(b.second - a.second) / (float)dt : 0;
				if (std::fabs(v) >= Impl::FLING_MIN) { d->flingId = d->dragId; d->flingV = v; d->flingAt = -1; }
			}
			return was ? Action::REDRAW : Action::NONE;
		}
		if (d->stopTap) { d->stopTap = false; return Action::REDRAW; }
		// Back to game: its plate (to the panel's edges) and the icon's corner
		Impl::Area ba = d->backArea();
		ba.x1 = 640;
		(d->backTop() ? ba.y0 : ba.y1) = d->backTop() ? 0 : 480;
		if (ba.in(cx, cy) ||
			(cx >= d->cornerX && cx < d->cornerX + d->cornerW && cy >= d->cornerY && cy < d->cornerY + d->cornerH))
			return Action::CLOSE;
		// the last drawn on top: the last hit that holds both the touch's start and end
		for (size_t i = d->hits.size(); i-- > 0;)
		{
			const auto& h = d->hits[i];
			auto in = [&](float px, float py) { return px >= h.x && px < h.x + h.w && py >= h.y && py < h.y + h.h; };
			if (in(cx, cy) && in(d->downX, d->downY)) { auto act = h.act; act(); return Action::REDRAW; }
		}
		return was ? Action::REDRAW : Action::NONE;
	}

	bool Pages::update(double nowMs)
	{
		d->now = nowMs;
		const bool decoded = d->takeDecoded();
		if (decoded) std::fill(std::begin(d->warmed), std::end(d->warmed), false);	// (their pictures to make)
		bool moved = false;
		if (d->flingId >= 0)
		{
			auto f = d->scrolls.find(d->flingId);
			if (f == d->scrolls.end() || !f->second.shown) d->stopFling();
			else if (d->flingAt < 0) { d->flingAt = nowMs; moved = true; }	// (the first frame after the lift)
			else
			{
				Impl::Scroll& sc = f->second;
				const double dt = std::min(100.0, std::max(0.0, nowMs - d->flingAt));
				d->flingAt = nowMs;
				// the distance covered under a speed falling off exponentially, then the speed left
				const float k = (float)std::exp(-dt / Impl::FLING_TAU);
				const float ny = std::max(0.0f, std::min(sc.max, sc.y + d->flingV * (float)Impl::FLING_TAU * (1 - k)));
				d->flingV *= k;
				moved = ny != sc.y;
				sc.y = ny;
				if (!moved || std::fabs(d->flingV) < Impl::FLING_STOP || ny <= 0 || ny >= sc.max) d->stopFling();
			}
		}
		return decoded || moved || (d->animNext >= 0 && nowMs >= d->animNext);
	}
	int Pages::nextWake(double nowMs) const
	{
		if (d->flingId >= 0) return 16;
		if (d->animNext < 0) return -1;
		return (int)std::max(0.0, std::ceil(d->animNext - nowMs));
	}
	void Pages::render() { d->render(true); }
	void Pages::draw() { d->render(false); }
	int Pages::season() const
	{
		if (!d->dt.rom || d->dt.rom->game.gen != Gen::V || !d->dt.live || !d->dt.live->valid) return -1;
		return pickedSeason >= 0 ? pickedSeason : d->dt.live->season & 3;
	}
	int Pages::unseen() const
	{
		int n = 0;
		for (int i = 0; i < (int)Page::COUNT; i++) n += d->noticeCount((Page)i);
		return n;
	}

	std::vector<std::string> Pages::takeSeen()
	{
		std::vector<std::string> v(d->seenQueue.begin(), d->seenQueue.end());
		// (still counted as seen for the tabs until the logic's next PageData has them)
		for (auto& id : v) if (std::find(d->dt.seenNotices.begin(), d->dt.seenNotices.end(), id) == d->dt.seenNotices.end()) d->dt.seenNotices.push_back(id);
		d->seenQueue.clear();
		return v;
	}
	float Pages::scrollOf(Page pg) const
	{
		auto it = d->scrolls.find(d->scrollId(pg, pg == Page::PARTY || pg == Page::DEX ? 1 : 0));
		return it == d->scrolls.end() ? 0 : it->second.y;
	}
	float Pages::scrollOf(Page pg, int list) const
	{
		auto it = d->scrolls.find(d->scrollId(pg, list));
		return it == d->scrolls.end() ? 0 : it->second.y;
	}
	float Pages::scrollMax(Page pg) const
	{
		auto it = d->scrolls.find(d->scrollId(pg, pg == Page::PARTY || pg == Page::DEX ? 1 : 0));
		return it == d->scrolls.end() ? 0 : it->second.max;
	}
}
