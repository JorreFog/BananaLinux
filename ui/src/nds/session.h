#pragma once
// banana-ui: one DS Pokémon game's companion while it runs: the RAM reader polled over CompLink (ram.h), the advice
// worked out again when the game changed (logic.h), the journey log, and what the player has seen - kept on the card
// per game + trainer (the game code, trainer ID and secret ID: two playthroughs of one game keep their own).
//
// Wiring (main.cpp's runWhileGame idle hook, beside the ROM's background read):
//   nds::Session s(romData, guide, o.data + "/cache/nds-companion");
//   s.send = [&](const std::string& l) { return compLink.send(l); };  s.receive = [&] { return compLink.receive(); };
//   ... each tick: if (s.tick(now, time(nullptr))) { d.live = s.live(); d.advice = s.advice(); d.journey = s.journey();
//                  d.seenNotices = s.seenList(); pages->setData(d); }   and  s.markSeen(pages->takeSeen());
#include "nds/logic.h"
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace banana::nds
{
	// how a species new in the Pokédex came, for the journey log: false when a party member evolved or hatched into it
	// (logged as that); else CATCH when a wild battle is on or recentWild (one just ended), GIFT otherwise (gifts,
	// trades, events: White's Tepig was logged as caught at 0h00m)
	bool newEntryKind(const Live& was, const Live& now, uint16_t species, bool recentWild, LogKind& kind);

	class Session
	{
	public:
		Session(std::shared_ptr<const RomData> rom, std::shared_ptr<const GuideData> guide, std::string stateDir);
		~Session();
		// the link (CompLink's send/receive: READ_CORE_MEMORY lines out, the reply lines back; "" = not yet)
		std::function<bool(const std::string&)> send;
		std::function<std::string()> receive;
		// call from the menu's idle loop (any rate; a set goes out every 250 ms): true when Live/Advice changed
		bool tick(double nowMs, time_t wall);
		// the game's state from another reader (the GBA games: ui/src/gba/gen3live.*): the same journey, advice and
		// notices as tick's, from l. No RAM read of its own then (send/receive unused)
		bool feed(std::shared_ptr<const Live> l, double nowMs, time_t wall);
		std::shared_ptr<const Live> live() const { return cur ? cur : rr.live(); }
		std::shared_ptr<const Advice> advice() const { return adv; }
		std::shared_ptr<const Journey> journey() const { return std::make_shared<const Journey>(*jr); }	// (a copy: the pages keep it)
		// notices the pages showed (Pages::takeSeen): never counted again, kept on the card
		void markSeen(const std::vector<std::string>& ids);
		std::vector<std::string> seenList() const;
		int unseen() const;					// the badge's count
		void setChallenge(bool on);
		// Gen V's season as the player picked it on the Here page (Pages::seasonPick; -1 the handheld's month): Live and
		// the advice go by it (the game's own season is not read)
		void setSeason(int season);
		const RamReader& reader() const { return rr; }
		// what the player's ROM tells the RAM reader (rom.h ramHints: the battle overlay's bytes)
		void setHints(const RamHints& h) { rr.setHints(h); }
		// (tests) where the RAM reader's finds for this game + ROM are kept (RamReader::anchors)
		std::string anchorFile() const;
		// (tests) the file this playthrough's state is kept in ("" until the trainer is known)
		const std::string& stateFile() const { return file; }
	private:
		std::shared_ptr<const RomData> rom;
		std::shared_ptr<const GuideData> guide;
		std::string dir, file;
		RamReader rr;
		AdviceState st;
		std::shared_ptr<const Advice> adv = std::make_shared<Advice>();
		std::shared_ptr<Journey> jr = std::make_shared<Journey>();
		std::shared_ptr<const Live> prev;	// the last Live the journey looked at
		std::vector<Range> sent;
		bool waiting = false;
		double sentAt = -1e9, lastAt = -1e9;
		double savedAt = -1e9;
		bool dirty = false;
		int challengeWanted = -1;	// setChallenge's (the Settings row), applied again over the card's when it is read
		int64_t lastWild = -1000;
		int seasonPick = -1;
		bool redo = false;
		bool searchChecked = false, wasScanning = false;	// (the RAM reader's test flag looked at; a search running)					// (a new season pick: Live and the advice again at the next tick)
		std::shared_ptr<const Live> cur;	// rr's Live with the picked season	// the play time (s) of the last reading in a wild battle (a catch registers around its end)
		std::shared_ptr<const Live> fed;	// (feed's last: setSeason works it out again from this)
		bool apply(std::shared_ptr<const Live> l, double now, time_t wall);
		void track(const Live& now, time_t wall);
		void load();
		void save();
	};

	// A Session on a thread of its own, so the companion's paint loop never waits for it: the RAM's replies parsed and
	// the advice worked out took ~6 ms of every 250 ms poll on the RG DS and up to 17 ms on the Plus, on the thread that
	// takes the touches and draws (a stalled picture in every fourth of a drag). The paint loop takes what changed
	// (take) and hands over what the pages saw (markSeen, setSeason).
	class SessionThread
	{
	public:
		struct Snapshot
		{
			std::shared_ptr<const Live> live;
			std::shared_ptr<const Advice> advice;
			std::shared_ptr<const Journey> journey;
			std::vector<std::string> seen;
		};
		// s: set up (send/receive/challenge) before; the thread's from now on (send/receive are called on it). The
		// Session ticks every periodMs
		explicit SessionThread(std::unique_ptr<Session> s, int periodMs = 10);
		~SessionThread();	// stops, then the Session goes (saving what it holds) on the caller's thread
		// what changed since the last take: false when nothing did
		bool take(Snapshot& out);
		void markSeen(const std::vector<std::string>& ids);
		void setSeason(int season);
		// the game's state from another reader (Session::feed on the thread; the newest one only)
		void feed(std::shared_ptr<const Live> l);
	private:
		std::shared_ptr<const Live> feedIn;
		std::unique_ptr<Session> ss;
		std::mutex m;
		std::condition_variable cv;
		bool stop = false, fresh = false;
		Snapshot snap;
		std::vector<std::string> seenIn;
		int season = -1;
		bool seasonIn = false;
		std::thread th;
		void run(int periodMs);
	};
}
