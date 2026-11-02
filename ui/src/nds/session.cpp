// banana-ui: one DS Pokémon game's companion while it runs (session.h).
#include "nds/session.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>

namespace banana::nds
{
	Session::Session(std::shared_ptr<const RomData> r, std::shared_ptr<const GuideData> g, std::string stateDir)
		: rom(std::move(r)), guide(std::move(g)), dir(std::move(stateDir)), rr(rom ? rom->game : Game{}) {}

	Session::~Session() { if (dirty) save(); }

	static std::string name(const RomData& rom, uint16_t s) { return s && s < rom.species.size() ? rom.species[s].name : "?"; }
	static bool bit(const std::vector<uint8_t>& v, int i) { return i >= 0 && (size_t)(i >> 3) < v.size() && (v[i >> 3] >> (i & 7)) & 1; }

	bool Session::tick(double now, time_t wall)
	{
		if (!rom || !rr.supported() || !send || !receive) return false;
		bool changed = false;
		if (waiting)
		{
			std::string r = receive();
			if (!r.empty())
			{
				waiting = false;
				std::vector<std::vector<uint8_t>> d;
				replyBytes(r, sent, d);
				if (rr.deliver(d, now, wall)) changed = true;
			}
			else if (now - sentAt > 1500) waiting = false;	// (not answered: the link gives such a set up; ask again)
		}
		if (!waiting && now - lastAt >= 250)
		{
			sent = rr.wants();
			const std::string q = requestLines(sent);
			if (!q.empty() && send(q)) { waiting = true; sentAt = lastAt = now; }
		}
		if (!changed) return false;
		auto l = rr.live();
		if (file.empty() && l->valid)
		{
			char b[96];
			snprintf(b, sizeof b, "/%s-%05u-%05u.tsv", rom->game.code.c_str(), l->tid, l->sid);
			file = dir + b;
			load();
		}
		track(*l, wall);
		adv = std::make_shared<const Advice>(computeAdvice(*rom, *l, guide.get(), st));
		if (dirty && now - savedAt > 5000) { save(); savedAt = now; }
		return true;
	}

	void Session::markSeen(const std::vector<std::string>& ids)
	{
		for (auto& i : ids) if (st.seen.insert(i).second) dirty = true;
		if (dirty && !ids.empty())
		{
			// (the notices recounted without the seen ones)
			if (auto l = rr.live(); l->valid && rom) adv = std::make_shared<const Advice>(computeAdvice(*rom, *l, guide.get(), st));
		}
	}
	std::vector<std::string> Session::seenList() const { return std::vector<std::string>(st.seen.begin(), st.seen.end()); }
	int Session::unseen() const { return (int)adv->notices.size(); }
	void Session::setChallenge(bool on) { if (jr->challenge != on) { jr->challenge = on; dirty = true; } }

	// the journey: what changed between two readings of the same playthrough
	void Session::track(const Live& l, time_t wall)
	{
		std::shared_ptr<const Live> was = prev;
		prev = rr.live();
		if (!was || !was->valid || !l.valid || was->tid != l.tid || was->sid != l.sid) return;
		auto add = [&](LogKind k, uint16_t species, const std::string& text)
		{
			LogEntry e; e.kind = k; e.playSeconds = l.playSeconds; e.when = (int64_t)wall; e.species = species; e.zone = l.zone;
			e.text = text;
			jr->log.push_back(e);
			dirty = true;
		};
		for (int b = 0; b < 16; b++)
			if ((l.badges >> b & 1) && !(was->badges >> b & 1)) add(LogKind::BADGE, 0, "Badge " + std::to_string(b + 1));
		const bool firstCatch = was->dexCaught.size() == l.dexCaught.size();
		for (size_t i = 0; firstCatch && i < l.dexCaught.size() * 8; i++)
			if (bit(l.dexCaught, (int)i) && !bit(was->dexCaught, (int)i))
			{
				const uint16_t s = (uint16_t)(i + 1);
				add(LogKind::CATCH, s, "Caught " + name(*rom, s));
				if (jr->challenge)
					for (auto& r : jr->routes) if (r.zone == l.zone && r.species == s && r.outcome == RouteRecord::OPEN) r.outcome = RouteRecord::CAUGHT;
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
			for (auto& r : jr->routes) have |= r.zone == l.zone;
			if (!have) { jr->routes.push_back({ l.zone, l.battle.foes[0].mon.species, RouteRecord::OPEN }); dirty = true; }
		}
		if (jr->challenge && wasWild && !wild)
			for (auto& r : jr->routes)
				if (r.zone == was->zone && r.outcome == RouteRecord::OPEN && !bit(l.dexCaught, r.species - 1)) { r.outcome = RouteRecord::MISSED; dirty = true; }
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
}
