// banana-ui: one DS Pokémon game's companion while it runs (session.h).
#include "nds/session.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <chrono>
#include <ctime>

namespace banana::nds
{
	Session::Session(std::shared_ptr<const RomData> r, std::shared_ptr<const GuideData> g, std::string stateDir)
		: rom(std::move(r)), guide(std::move(g)), dir(std::move(stateDir)), rr(rom ? rom->game : Game{})
	{
		// where the RAM reader found this game + ROM's save and battle data before: tried first
		const std::string f = anchorFile();
		if (f.empty()) return;
		std::ifstream in(f);
		std::stringstream t;
		t << in.rdbuf();
		rr.setAnchors(t.str());
	}

	std::string Session::anchorFile() const
	{
		if (!rom || dir.empty() || rom->game.romHash.empty()) return "";
		return dir + "/" + rom->game.code + "-" + rom->game.romHash + ".ram";
	}

	Session::~Session() { if (dirty) save(); }

	static std::string name(const RomData& rom, uint16_t s) { return s && s < rom.species.size() ? rom.species[s].name : "?"; }
	static bool bit(const std::vector<uint8_t>& v, int i) { return i >= 0 && (size_t)(i >> 3) < v.size() && (v[i >> 3] >> (i & 7)) & 1; }

	bool Session::tick(double now, time_t wall)
	{
		if (fed)	// (fed by another reader: only a new season pick to work out again)
		{
			if (!redo) return false;
			redo = false;
			return apply(fed, now, wall);
		}
		if (!rom || !rr.supported() || !send || !receive) return false;
		bool changed = false;
		if (!searchChecked)
		{
			// (a test on a handheld: the RAM reader as if this were a revision whose places nobody knows)
			searchChecked = true;
			struct stat st {};
			if (stat("/run/banana/nds-ramsearch", &st) == 0) { rr.searchFromScratch(now); fprintf(stderr, "banana-ui: nds companion: RAM: searching from scratch (test)\n"); }
		}
		if (waiting)
		{
			std::string r = receive();
			if (!r.empty())
			{
				waiting = false;
				std::vector<std::vector<uint8_t>> d;
				replyBytes(r, sent, d);
				if (rr.deliver(d, now, wall)) changed = true;
				// (the log: where the save and the battle copies were found, and the searches)
				if (rr.scanning() != wasScanning)
				{
					wasScanning = rr.scanning();
					fprintf(stderr, "banana-ui: nds companion: RAM: %s at %.1f s%s%s\n", wasScanning ? "searching" : "search done", now / 1000.0,
						rr.lastProblem().empty() ? "" : ": ", rr.lastProblem().c_str());
				}
				if (rr.anchorsChanged())
				{
					// (a new place checked out: kept for the next start; rare - once per game + ROM, or after a search)
					const std::string f = anchorFile();
					if (!f.empty())
					{
						mkdir(dir.c_str(), 0755);
						{ std::ofstream o(f + ".tmp"); o << "# where the DS Pokémon companion found this game's data in RAM\n" << rr.anchors(); }
						rename((f + ".tmp").c_str(), f.c_str());
					}
					fprintf(stderr, "banana-ui: nds companion: RAM: the save at %08x (pointer %08x), battle copies %+d: kept for the next start\n",
						rr.saveBase(), rr.savePointer(), (int)rr.battleOffset());
					rr.anchorsSaved();
				}
			}
			else if (now - sentAt > 1500) waiting = false;	// (not answered: the link gives such a set up; ask again)
		}
		if (!waiting && now - lastAt >= 250)
		{
			sent = rr.wants();
			const std::string q = requestLines(sent);
			if (!q.empty() && send(q)) { waiting = true; sentAt = lastAt = now; }
		}
		if (redo && rr.live()->valid) { changed = true; redo = false; }
		if (!changed) return false;
		return apply(rr.live(), now, wall);
	}

	bool Session::feed(std::shared_ptr<const Live> l, double now, time_t wall)
	{
		if (!rom || !l) return false;
		fed = l;
		redo = false;
		return apply(l, now, wall);
	}

	bool Session::apply(std::shared_ptr<const Live> l, double now, time_t wall)
	{
		if (seasonPick >= 0 && l->season != seasonPick)
		{
			auto c = std::make_shared<Live>(*l);
			c->season = (uint8_t)seasonPick;
			l = c;
		}
		// (an hg-engine hack - a DS game with game.hack - keeps a Pokémon's ability slot, 0 or 1 - 2 its hidden one -, not
		// the ability, where Gen IV keeps the ability: Origin HeartGold's party, 2026-11-04. The battle's own copies hold
		// the ability)
		if (!rom->game.hack.empty() && rom->game.gen == Gen::IV)
		{
			auto c = std::make_shared<Live>(*l);
			auto fix = [&](Mon& m)
			{
				if (m.species >= rom->species.size() || m.ability > 2) return;
				const uint16_t* a = rom->species[m.species].abilities;
				m.ability = a[m.ability] ? a[m.ability] : a[0];
			};
			for (auto& m : c->party) fix(m);
			for (auto& m : c->daycare) fix(m);
			for (auto& m : c->battle.foeParty) fix(m);
			l = c;
		}
		cur = l;
		// (the playthrough named by its trainer: not before the trainer id is read)
		if (file.empty() && l->valid && l->trainerKnown)
		{
			char b[96];
			snprintf(b, sizeof b, "/%s-%05u-%05u.tsv", rom->game.code.c_str(), l->tid, l->sid);
			file = dir + b;
			load();
			// the Settings row wins over what the card kept for this playthrough (load() read the card's value)
			if (challengeWanted >= 0 && jr->challenge != (challengeWanted == 1)) { jr->challenge = challengeWanted == 1; dirty = true; }
		}
		if (l->trainerKnown) track(*l, wall);
		else prev = cur;	// (nothing compared against a reading with no trainer: the next is a baseline)
		adv = std::make_shared<const Advice>(computeAdvice(*rom, *l, guide.get(), st));
		if (dirty && now - savedAt > 5000) { save(); savedAt = now; }
		return true;
	}

	void Session::setSeason(int season)
	{
		season = season >= 0 ? season & 3 : -1;
		if (season == seasonPick) return;
		seasonPick = season;
		redo = true;
	}

	void Session::markSeen(const std::vector<std::string>& ids)
	{
		for (auto& i : ids) if (st.seen.insert(i).second) dirty = true;
		if (dirty && !ids.empty())
		{
			// (the notices recounted without the seen ones)
			if (auto l = live(); l->valid && rom) adv = std::make_shared<const Advice>(computeAdvice(*rom, *l, guide.get(), st));
		}
	}
	std::vector<std::string> Session::seenList() const { return std::vector<std::string>(st.seen.begin(), st.seen.end()); }
	int Session::unseen() const { return (int)adv->notices.size(); }
	void Session::setChallenge(bool on)
	{
		challengeWanted = on ? 1 : 0;
		if (jr->challenge != on) { jr->challenge = on; dirty = true; }
	}

	static bool wildBattle(const Live& l) { return l.battle.kind == BattleKind::WILD || l.battle.kind == BattleKind::DOUBLE_WILD; }

	bool newEntryKind(const Live& was, const Live& now, uint16_t species, bool recentWild, LogKind& kind)
	{
		for (auto& m : now.party)
			for (auto& o : was.party)
				if (m.pid && m.pid == o.pid && m.species == species && !m.egg && (o.egg || o.species != species)) return false;
		kind = recentWild || wildBattle(was) || wildBattle(now) ? LogKind::CATCH : LogKind::GIFT;
		return true;
	}

	// the journey: what changed between two readings of the same playthrough
	void Session::track(const Live& l, time_t wall)
	{
		std::shared_ptr<const Live> was = prev;
		// (this reading as apply() took it: cur. Not rr.live(), the RAM reader's - the same for the DS, but empty for a
		// fed Session (the GBA host's), so the GBA games' journey and challenge mode never saw a change)
		prev = cur;
		if (!was || !was->valid || !l.valid || was->tid != l.tid || was->sid != l.sid) return;
		// (the catch's Pokédex entry can come a little after the battle's end: the nickname question, the Pokédex page)
		if (wildBattle(*was) || wildBattle(l)) lastWild = l.playSeconds;
		const bool recentWild = (int64_t)l.playSeconds - lastWild <= 60;
		auto add = [&](LogKind k, uint16_t species, const std::string& text)
		{
			LogEntry e; e.kind = k; e.playSeconds = l.playSeconds; e.when = (int64_t)wall; e.species = species; e.zone = l.zone;
			e.text = text;
			jr->log.push_back(e);
			dirty = true;
		};
		// a save that has just loaded turns up in one step (its badges and Pokédex were not in memory yet at the reading
		// before): that is the baseline, not a run of new badges and catches (it filled the log with the whole Pokédex)
		int newBadges = 0, newCatches = 0;
		for (int b = 0; b < 16; b++) newBadges += (l.badges >> b & 1) && !(was->badges >> b & 1);
		const bool firstCatch = was->dexCaught.size() == l.dexCaught.size();
		for (size_t i = 0; firstCatch && i < l.dexCaught.size() * 8; i++)
			newCatches += bit(l.dexCaught, (int)i) && !bit(was->dexCaught, (int)i);
		const bool loaded = newCatches >= 6 || newBadges >= 3;
		for (int b = 0; b < 16 && !loaded; b++)
			if ((l.badges >> b & 1) && !(was->badges >> b & 1)) add(LogKind::BADGE, 0, "Badge " + std::to_string(b + 1));
		for (size_t i = 0; firstCatch && !loaded && i < l.dexCaught.size() * 8; i++)
			if (bit(l.dexCaught, (int)i) && !bit(was->dexCaught, (int)i))
			{
				const uint16_t s = (uint16_t)(i + 1);
				LogKind k;
				if (!newEntryKind(*was, l, s, recentWild, k)) continue;
				add(k, s, (k == LogKind::CATCH ? "Caught " : "Received ") + name(*rom, s));
				if (jr->challenge && k == LogKind::CATCH)
					for (auto& r : jr->routes) if (sameArea(rom.get(), r.zone, l.zone) && r.species == s && r.outcome == RouteRecord::OPEN) r.outcome = RouteRecord::CAUGHT;
			}
		for (auto& m : l.party)
			for (auto& o : was->party)
				if (m.pid == o.pid && m.pid)
				{
					if (o.egg && !m.egg) add(LogKind::HATCH, m.species, name(*rom, m.species) + " hatched");
					else if (!m.egg && o.species != m.species) add(LogKind::EVOLVE, m.species, o.nick + " evolved into " + name(*rom, m.species));
					// challenge mode: a Pokémon that fainted counts as lost (once)
					if (jr->challenge && !m.egg && o.hp > 0 && m.hp == 0 && m.maxHp)
					{
						bool known = false;
						for (auto& x : jr->lost) known |= x.pid == m.pid;
						if (!known) { jr->lost.push_back(m); add(LogKind::LOST, m.species, m.nick + " fainted"); }
					}
				}
		// challenge mode: the first wild Pokémon met on each route
		const bool wild = l.battle.kind == BattleKind::WILD || l.battle.kind == BattleKind::DOUBLE_WILD;
		const bool wasWild = was->battle.kind == BattleKind::WILD || was->battle.kind == BattleKind::DOUBLE_WILD;
		if (jr->challenge && wild && !wasWild && l.battle.foes[0].present && l.zone)
		{
			bool have = false;
			for (auto& r : jr->routes) have |= sameArea(rom.get(), r.zone, l.zone);
			if (!have) { jr->routes.push_back({ l.zone, l.battle.foes[0].mon.species, RouteRecord::OPEN }); dirty = true; }
		}
		if (jr->challenge && wasWild && !wild)
			for (auto& r : jr->routes)
				if (sameArea(rom.get(), r.zone, was->zone) && r.outcome == RouteRecord::OPEN && !bit(l.dexCaught, r.species - 1)) { r.outcome = RouteRecord::MISSED; dirty = true; }
	}

	// ---- the card file: tab-separated lines ---------------------------------------------------------------------
	static std::string clean(std::string s) { for (auto& c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' '; return s; }

	void Session::load()
	{
		std::ifstream f(file);
		std::string line;
		while (std::getline(f, line))
		{
			std::vector<std::string> c;
			std::stringstream ss(line);
			for (std::string x; std::getline(ss, x, '\t');) c.push_back(x);
			if (c.empty()) continue;
			if (c[0] == "seen" && c.size() >= 2) st.seen.insert(c[1]);
			else if (c[0] == "challenge" && c.size() >= 2) jr->challenge = c[1] == "1";
			else if (c[0] == "log" && c.size() >= 7)
			{
				LogEntry e;
				e.kind = (LogKind)atoi(c[1].c_str()); e.playSeconds = (uint32_t)strtoul(c[2].c_str(), nullptr, 10);
				e.when = strtoll(c[3].c_str(), nullptr, 10); e.species = (uint16_t)atoi(c[4].c_str()); e.zone = (uint16_t)atoi(c[5].c_str());
				e.text = c[6];
				jr->log.push_back(e);
			}
			else if (c[0] == "route" && c.size() >= 4)
				jr->routes.push_back({ (uint16_t)atoi(c[1].c_str()), (uint16_t)atoi(c[2].c_str()), (RouteRecord::Outcome)atoi(c[3].c_str()) });
			else if (c[0] == "lost" && c.size() >= 5)
			{
				Mon m; m.pid = (uint32_t)strtoul(c[1].c_str(), nullptr, 10); m.species = (uint16_t)atoi(c[2].c_str());
				m.level = (uint8_t)atoi(c[3].c_str()); m.nick = c[4];
				jr->lost.push_back(m);
			}
		}
	}

	void Session::save()
	{
		if (file.empty()) return;
		mkdir(dir.c_str(), 0755);
		const std::string tmp = file + ".tmp";
		{
			std::ofstream f(tmp);
			f << "# the DS Pokémon companion's record of this playthrough (seen notices, journey log, challenge mode)\n";
			f << "challenge\t" << (jr->challenge ? 1 : 0) << "\n";
			for (auto& s : st.seen) f << "seen\t" << clean(s) << "\n";
			for (auto& e : jr->log) f << "log\t" << (int)e.kind << "\t" << e.playSeconds << "\t" << e.when << "\t" << e.species << "\t" << e.zone << "\t" << clean(e.text) << "\n";
			for (auto& r : jr->routes) f << "route\t" << r.zone << "\t" << r.species << "\t" << (int)r.outcome << "\n";
			for (auto& m : jr->lost) f << "lost\t" << m.pid << "\t" << m.species << "\t" << (int)m.level << "\t" << clean(m.nick) << "\n";
			if (!f) return;
		}
		if (rename(tmp.c_str(), file.c_str()) == 0) dirty = false;
	}

	// ---- the Session on its own thread ----

	SessionThread::SessionThread(std::unique_ptr<Session> s, int periodMs) : ss(std::move(s))
	{
		th = std::thread([this, periodMs] { run(periodMs); });
	}

	SessionThread::~SessionThread()
	{
		{
			std::lock_guard<std::mutex> g(m);
			stop = true;
		}
		cv.notify_all();
		th.join();
	}

	bool SessionThread::take(Snapshot& out)
	{
		std::lock_guard<std::mutex> g(m);
		if (!fresh) return false;
		out = snap;
		fresh = false;
		return true;
	}

	void SessionThread::markSeen(const std::vector<std::string>& ids)
	{
		if (ids.empty()) return;
		std::lock_guard<std::mutex> g(m);
		seenIn.insert(seenIn.end(), ids.begin(), ids.end());
	}

	void SessionThread::setSeason(int s)
	{
		std::lock_guard<std::mutex> g(m);
		if (s == season && !seasonIn) return;
		season = s;
		seasonIn = true;
	}

	void SessionThread::feed(std::shared_ptr<const Live> l)
	{
		std::lock_guard<std::mutex> g(m);
		feedIn = std::move(l);
		cv.notify_one();
	}

	void SessionThread::run(int periodMs)
	{
		std::unique_lock<std::mutex> g(m);
		for (;;)
		{
			cv.wait_for(g, std::chrono::milliseconds(periodMs), [this] { return stop; });
			if (stop) return;
			std::vector<std::string> seen;
			seen.swap(seenIn);
			const bool newSeason = seasonIn;
			const int sp = season;
			seasonIn = false;
			std::shared_ptr<const Live> fl;
			fl.swap(feedIn);
			g.unlock();
			ss->markSeen(seen);
			if (newSeason) ss->setSeason(sp);
			timespec ts;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			const double nowMs = ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
			bool changed = ss->tick(nowMs, time(nullptr));
			if (fl) changed = ss->feed(fl, nowMs, time(nullptr)) || changed;
			Snapshot n;
			if (changed) n = Snapshot{ ss->live(), ss->advice(), ss->journey(), ss->seenList() };
			g.lock();
			if (changed) { snap = std::move(n); fresh = true; }
		}
	}
}
