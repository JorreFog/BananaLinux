#include "guide.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include "../json.h"

namespace banana::nds
{
	namespace
	{
		struct GameRow { GameId id; const char* key; const char* code; int top; };
		const GameRow GAMES[] = {
			{ GameId::DIAMOND, "diamond", "ADAE", 493 }, { GameId::PEARL, "pearl", "APAE", 493 },
			{ GameId::PLATINUM, "platinum", "CPUE", 493 }, { GameId::HEARTGOLD, "heartgold", "IPKE", 493 },
			{ GameId::SOULSILVER, "soulsilver", "IPGE", 493 }, { GameId::BLACK, "black", "IRBO", 649 },
			{ GameId::WHITE, "white", "IRAO", 649 }, { GameId::BLACK2, "black2", "IREO", 649 },
			{ GameId::WHITE2, "white2", "IRDO", 649 },
		};
		const GameRow* rowFor(GameId id)
		{
			for (const GameRow& r : GAMES) if (r.id == id) return &r;
			return nullptr;
		}

		// "Mt. Coronet" == "MT CORONET" == "mt.coronet": letters and digits only, lower case
		std::string fold(const std::string& s)
		{
			std::string o;
			for (unsigned char c : s) if (isalnum(c)) o += (char)tolower(c);
			return o;
		}

		Quality quality(const JVal& e) { return e.str("quality") == "live-verified" ? Quality::LIVE_VERIFIED : Quality::REFERENCE_ONLY; }

		// the ids list members that are integers in range (others are dropped)
		std::vector<uint16_t> ints(const JVal* a, int lo, int hi)
		{
			std::vector<uint16_t> o;
			if (a && a->isArr())
				for (const JPtr& v : a->arr)
					if (v->isNum() && v->n >= lo && v->n <= hi && v->n == (int)v->n) o.push_back((uint16_t)v->n);
			return o;
		}
		std::vector<std::string> strings(const JVal* a)
		{
			std::vector<std::string> o;
			if (a && a->isArr()) for (const JPtr& v : a->arr) if (v->isStr()) o.push_back(v->s);
			return o;
		}
		void addUnique(std::vector<uint16_t>& to, const std::vector<uint16_t>& from)
		{
			for (uint16_t z : from) if (std::find(to.begin(), to.end(), z) == to.end()) to.push_back(z);
		}

		struct Place { std::string name; std::vector<uint16_t> zones; bool known = false; };

		struct Loader
		{
			std::map<std::string, Place> places;
			std::vector<std::string> skipped;
			GuideData* out = nullptr;
			std::set<std::string> unresolvedSeen;

			void skip(const std::string& where, const char* why) { skipped.push_back(where + " (" + why + ")"); }

			void readPlaces(const JVal& root, const GuideResolver* res)
			{
				const JVal* p = root.get("places");
				if (!p || !p->isObj()) return;
				for (auto& kv : p->obj)
				{
					const JVal& v = *kv.second;
					if (!v.isObj()) continue;
					Place pl;
					pl.name = v.str("name");
					if (pl.name.empty()) continue;
					const JVal* z = v.get("zone");
					if (z && z->isNum()) pl.zones.push_back((uint16_t)z->n);
					addUnique(pl.zones, ints(v.get("zones"), 0, 0xFFFF));
					if (pl.zones.empty() && res && res->zones) pl.zones = res->zones(pl.name);
					pl.known = !pl.zones.empty();
					if (!pl.known && unresolvedSeen.insert(pl.name).second) out->unresolved.push_back(pl.name);
					places[kv.first] = pl;
				}
			}
			const Place* place(const std::string& key) const
			{
				auto it = places.find(key);
				return it == places.end() ? nullptr : &it->second;
			}
			// the zones of place keys, and their names
			void gather(const JVal* keys, std::vector<uint16_t>& zones, std::vector<std::string>* names, std::vector<std::string>* keyList) const
			{
				if (!keys || !keys->isArr()) return;
				for (const JPtr& k : keys->arr)
				{
					const Place* p = k->isStr() ? place(k->s) : nullptr;
					if (!p) continue;
					addUnique(zones, p->zones);
					if (names && std::find(names->begin(), names->end(), p->name) == names->end()) names->push_back(p->name);
					if (keyList) keyList->push_back(k->s);
				}
			}
		};
	}

	std::string guideFileName(GameId id)
	{
		const GameRow* r = rowFor(id);
		return r ? std::string(r->key) + ".json" : std::string();
	}
	const char* guideDir()
	{
		const char* e = getenv("BANANA_NDS_GUIDE");
		return e && *e ? e : "/usr/share/banana/companions/nds-pokemon";
	}

	Type typeFromName(const std::string& n)
	{
		static const char* names[NUM_TYPES] = { "normal", "fighting", "flying", "poison", "ground", "rock", "bug", "ghost", "steel",
			"fire", "water", "grass", "electric", "psychic", "ice", "dragon", "dark" };
		for (int i = 0; i < NUM_TYPES; i++) if (n == names[i]) return (Type)i;
		return NO_TYPE;
	}

	GuideResolver guideResolver(const RomData& rom)
	{
		GuideResolver r;
		const RomData* rd = &rom;	// (the lambdas outlive nothing: the caller keeps the RomData while it loads)
		r.zones = [rd](const std::string& name) {
			std::vector<uint16_t> o;
			std::string f = fold(name);
			if (f.empty()) return o;
			for (auto& kv : rd->zones) if (fold(kv.second.name) == f) o.push_back(kv.first);
			return o;
		};
		r.trainers = [rd](const std::string& name, const std::string& cls, int ace) {
			std::vector<uint16_t> o;
			std::string fn = fold(name), fc = fold(cls);
			if (fn.empty()) return o;
			for (auto& kv : rd->trainers)
			{
				const Trainer& t = kv.second;
				if (fold(t.name) != fn || t.team.empty()) continue;
				if (!fc.empty() && fold(t.cls).find(fc) == std::string::npos) continue;
				int top = 0;
				for (const TrainerMon& m : t.team) top = std::max<int>(top, m.level);
				if (ace > 0 && top != ace) continue;
				o.push_back(kv.first);
			}
			return o;
		};
		return r;
	}

	bool loadGuideFile(const std::string& path, GuideData& out, GameId expect, std::string* why, const GuideResolver* res)
	{
		auto fail = [&](const std::string& m) { if (why) *why = m; return false; };
		std::string jwhy;
		JPtr doc = readJsonFile(path, &jwhy);
		if (!doc || !doc->isObj()) return fail("can't read " + path + (jwhy.empty() ? "" : ": " + jwhy));
		if (doc->num("schema") != 1 || doc->str("kind") != "nds-pokemon-guide") return fail(path + " is not an nds-pokemon-guide, schema 1");
		const GameRow* row = nullptr;
		for (const GameRow& r : GAMES) if (doc->str("game") == r.key) row = &r;
		if (!row || doc->str("code") != row->code) return fail(path + ": unknown game or a code that doesn't fit it");
		if (expect != GameId::NONE && expect != row->id) return fail(path + " is the guide of another game");

		GuideData g;
		Loader L;
		L.out = &g;
		g.game = row->key;
		g.title = doc->str("title");
		g.revision = (int)doc->num("revision");
		g.sources = strings(doc->get("sources"));
		L.readPlaces(*doc, res);

		// milestones; a milestone's reachable set is everything opened up to and including it
		std::vector<uint16_t> reachZones;
		std::vector<std::string> reachNames;
		std::set<std::string> ids;
		if (const JVal* ms = doc->get("milestones"); ms && ms->isArr())
			for (size_t i = 0; i < ms->arr.size(); i++)
			{
				const JVal& e = *ms->arr[i];
				std::string where = "milestones[" + std::to_string(i) + "]";
				if (!e.isObj() || e.str("id").empty() || e.str("name").empty() || !ids.insert(e.str("id")).second) { L.skip(where, "no id/name or a repeated id"); continue; }
				Milestone m;
				m.id = e.str("id"); m.name = e.str("name"); m.kind = e.str("kind");
				if (const Place* p = L.place(e.str("place"))) { m.place = p->name; m.placeZones = p->zones; }
				m.leader = e.str("leader"); m.tips = e.str("tips"); m.badgeNote = e.str("badge_note"); m.source = e.str("source");
				m.theme = typeFromName(e.str("theme"));
				m.levelCap = (int)e.num("level_cap");
				m.doneWhen = e.str("done_when", "none");
				if (m.doneWhen.rfind("badge:", 0) == 0) m.badge = atoi(m.doneWhen.c_str() + 6);
				m.q = quality(e);
				m.trainerIds = ints(e.get("trainer_ids"), 0, 0xFFFF);
				// foe: one object or a list of them (every opponent the fight can have); the ids of all are kept
				if (const JVal* f = e.get("foe"))
				{
					std::vector<const JVal*> foes;
					if (f->isArr()) for (const JPtr& x : f->arr) foes.push_back(x.get()); else foes.push_back(f);
					bool first = true, explicitIds = !m.trainerIds.empty();
					for (const JVal* x : foes)
					{
						if (!x || !x->isObj()) continue;
						if (first) { m.foeName = x->str("name"); m.foeClass = x->str("class"); m.foeAceLevel = (int)x->num("ace_level"); first = false; }
						if (!explicitIds && res && res->trainers) addUnique(m.trainerIds, res->trainers(x->str("name"), x->str("class"), (int)x->num("ace_level")));
					}
				}
				L.gather(e.get("opens"), reachZones, &reachNames, nullptr);
				m.reachableZones = reachZones;
				m.reachablePlaces = reachNames;
				g.milestones.push_back(std::move(m));
			}

		if (const JVal* st = doc->get("steps"); st && st->isArr())
			for (size_t i = 0; i < st->arr.size(); i++)
			{
				const JVal& e = *st->arr[i];
				if (!e.isObj() || e.str("id").empty() || e.str("text").empty()) { L.skip("steps[" + std::to_string(i) + "]", "no id/text"); continue; }
				GuideStep s;
				s.id = e.str("id"); s.title = e.str("title"); s.text = e.str("text"); s.stage = e.str("stage");
				s.doneWhen = e.str("done_when", "none"); s.q = quality(e); s.source = e.str("source");
				L.gather(e.get("places"), s.zones, &s.places, nullptr);
				g.steps.push_back(std::move(s));
			}

		if (const JVal* sc = doc->get("secrets"); sc && sc->isArr())
			for (size_t i = 0; i < sc->arr.size(); i++)
			{
				const JVal& e = *sc->arr[i];
				const Place* p = e.isObj() ? L.place(e.str("place")) : nullptr;
				if (!e.isObj() || e.str("id").empty() || e.str("text").empty() || !p) { L.skip("secrets[" + std::to_string(i) + "]", "no id/text/place"); continue; }
				Secret s;
				s.id = e.str("id"); s.text = e.str("text"); s.kind = e.str("kind"); s.place = p->name; s.zones = p->zones;
				s.zone = p->zones.empty() ? 0 : p->zones[0];
				if (e.get("x") && e.get("y")) { s.x = (int)e.num("x"); s.y = (int)e.num("y"); }
				s.condition = e.str("condition"); s.grid = e.str("grid", "map"); s.q = quality(e); s.source = e.str("source");
				g.secrets.push_back(std::move(s));
			}

		if (const JVal* ev = doc->get("evolutions"); ev && ev->isArr())
			for (size_t i = 0; i < ev->arr.size(); i++)
			{
				const JVal& e = *ev->arr[i];
				int sp = e.isObj() ? (int)e.num("species") : 0;
				if (sp < 1 || sp > row->top || e.str("text").empty()) { L.skip("evolutions[" + std::to_string(i) + "]", "bad species/text"); continue; }
				std::string& t = g.evolutionNotes[(uint16_t)sp];
				if (!t.empty()) t += "\n";
				t += e.str("text");
			}

		if (const JVal* nt = doc->get("notes"); nt && nt->isArr())
			for (size_t i = 0; i < nt->arr.size(); i++)
			{
				const JVal& e = *nt->arr[i];
				if (!e.isObj() || e.str("id").empty() || e.str("text").empty()) { L.skip("notes[" + std::to_string(i) + "]", "no id/text"); continue; }
				GuideNote n;
				n.id = e.str("id"); n.kind = e.str("kind"); n.title = e.str("title"); n.text = e.str("text"); n.source = e.str("source");
				n.q = quality(e);
				n.species = ints(e.get("species"), 1, row->top);
				if (const Place* p = L.place(e.str("place"))) { n.place = p->name; n.zones = p->zones; }
				g.notes.push_back(std::move(n));
			}

		if (why)
		{
			why->clear();
			if (!L.skipped.empty())
			{
				*why = std::to_string(L.skipped.size()) + " entries skipped:";
				for (size_t i = 0; i < L.skipped.size() && i < 3; i++) *why += " " + L.skipped[i] + ";";
			}
		}
		out = std::move(g);
		return true;
	}

	bool loadGuide(const Game& game, GuideData& out, std::string* why, const GuideResolver* resolver)
	{
		std::string f = guideFileName(game.id);
		if (f.empty()) { if (why) *why = "not one of the nine games"; return false; }
		return loadGuideFile(std::string(guideDir()) + "/" + f, out, game.id, why, resolver);
	}
}
