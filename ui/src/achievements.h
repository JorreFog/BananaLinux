#pragma once
// banana-ui: a game's RetroAchievements as a list (the Achievements page: X on a game > Achievements, and the game
// panel's List button while a RetroArch/BananaFront game runs). What the server says, read-only: the game identified
// from its ROM (rcheevos' rhash, ui/third_party/rcheevos; r=gameid), its core achievements (r=patch) and the account's
// unlocks, softcore and hardcore (r=unlocks h=0 / h=1). Never r=startsession: that would show the player as playing.
// Kept on the card for offline (<cache>/ids.tsv: a ROM -> its hash and game id; <cache>/game-<id>.tsv: the last list
// and unlocks), so the page shows the last list "as of" when there is no connection.
//
// The tests' stand-ins: BANANA_RA_FIXTURE=<dir> answers each request from <dir>/<r>.json (r=unlocks: unlocks-h0.json,
// unlocks-h1.json; a missing file is no reply) and a badge from <dir>/badges/<name>.png, logging each request's r and
// g/m (never the token) in <dir>/requests.log; BANANA_RA_URL=<url> sends the requests there instead of the site.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace banana::ach
{
	// rcheevos' RC_CONSOLE_* for a BananaOS system id (systems.conf's [id]); 0: no RetroAchievements for it
	uint32_t consoleFor(const std::string& systemId);

	// an unlock's time: 0 locked, UNKNOWN unlocked when the server doesn't say (r=unlocks has no dates; the game
	// panel's session does), else unix time
	constexpr long long UNKNOWN = 1;
	struct Item
	{
		uint32_t id = 0;
		std::string title, desc, badge;
		unsigned points = 0;
		long long soft = 0, hard = 0;
		bool unlocked() const { return soft || hard; }
		long long when() const { return hard > UNKNOWN ? hard : soft > UNKNOWN ? soft : hard ? hard : soft; }
	};
	struct List
	{
		uint32_t gameId = 0;
		std::string title;
		std::vector<Item> items;		// the server's display order
		long long fetched = 0;			// when the server last sent it (unix time)
		int unlocked() const;
		unsigned points() const, pointsEarned() const;
	};

	// r=patch's reply: the core achievements (Flags 3; not the server's "Unknown Emulator" warning); false when it isn't
	// a successful reply (l keeps nothing new)
	bool parsePatch(const std::string& json, List& l);
	// r=unlocks' reply (h=0 softcore, h=1 hardcore): which are unlocked; a time known from before (the cache, the game
	// panel) is kept. false when it isn't a successful reply
	bool parseUnlocks(const std::string& json, bool hardcore, List& l);
	// what an older copy knew of the same game (the cache): the unlock times the server's r=unlocks doesn't have
	void keepTimes(List& fresh, const List& old);

	// ---- the card's cache ----------------------------------------------------------------------------------------
	struct RomKey { std::string path; long long size = 0, mtime = 0; };
	RomKey romKey(const std::string& path);	// size and mtime 0: no such file
	// the ROM's hash and game id as last identified (the same file: its size and time unchanged); id 0 with a hash: the
	// server didn't know the game
	bool readId(const std::string& dir, const RomKey& k, std::string& hash, uint32_t& id);
	bool writeId(const std::string& dir, const RomKey& k, const std::string& hash, uint32_t id);
	bool readList(const std::string& dir, uint32_t gameId, List& l);
	bool writeList(const std::string& dir, const List& l);

	// ---- the ROM's hash ------------------------------------------------------------------------------------------
	// rcheevos' hash for the console ("" when it can't be made); a .zip's game (its largest member) hashed as RetroArch
	// hashes what it unpacks: in memory up to 64 MB, else through a file in tmpDir, removed after
	std::string hashRom(const std::string& path, uint32_t console, const std::string& tmpDir);

	// ---- the server ----------------------------------------------------------------------------------------------
	// a POST to dorequest.php (or the tests' stand-ins above); "" when there is no reply
	std::string post(const std::string& fields);
	// a badge (BadgeName, or BadgeName_lock: the locked one) into dir/<name>.png; false when it couldn't be fetched
	bool fetchBadge(const std::string& name, const std::string& dir);

	struct Account { std::string user, token, password; };

	// The Achievements page's loader: everything on a thread of its own, the page reads snapshots. With a game id
	// already known (the game panel identified it) the hashing is skipped.
	class Loader
	{
	public:
		enum State { LOADING, IDENTIFYING, READY, NO_ACCOUNT, NO_CONNECTION, NO_SYSTEM, NO_GAME, NO_FILE, LOGIN_FAILED };
		struct Snapshot
		{
			State state = LOADING;
			bool offline = false;		// READY from the cache: the server couldn't be reached
			List list;
			int gen = 0;				// changes whenever something does (a badge arrived too)
		};
		struct Job
		{
			std::string rom, system, cacheDir, badgeDir;
			uint32_t gameId = 0;		// known already (0: identified from the ROM)
			Account account;
			bool network = true;		// false: Wi-Fi is off (the cache only)
			std::function<std::string(const std::string&)> post;	// (tests; default ach::post)
		};
		static std::shared_ptr<Loader> start(Job job);
		Snapshot snapshot() const;
		int gen() const;
		bool busy() const;				// the list still being made (the tests wait for it)
		// a badge's picture wanted on the page: fetched on the badge thread (the page's rows first); true when its file is
		// there already
		bool badge(const std::string& name);
		void stop();					// the page closed: the threads end after what they are doing
	private:
		struct Shared;
		std::shared_ptr<Shared> sh;
	};
	const char* stateName(Loader::State s);	// (logs and tests)
}
