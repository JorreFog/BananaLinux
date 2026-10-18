#pragma once
// banana-ui: the game library, as the Pixel engine's Source. Systems come from systems.conf (which folder, which
// files, how to start them), their look (icon, colour, maker) from the theme's rnds/systems.cfg, games from the ROM
// folders, names and art from an EmulationStation gamelist.xml when there is one (a card scraped on ROCKNIX keeps
// its art) or from media/<kind>/<rom name>.png, and play counts, play time and favourites from Banana's own files.
#include "../rnds/RndsUI.h"
#include <ctime>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace banana
{
	struct SystemDef
	{
		std::string id, name, maker, folder, run, perf;
		std::vector<std::string> exts;	// lower case, no dot
		bool always = false;			// on the shelf with no games (the DS: a fresh card shows where games go)
		bool flat = false;				// games only in the folder itself (ports: their own folders hold scripts too)
	};

	struct Game
	{
		std::string path, rel, title, genre, year;	// rel: the path under the system's folder
		std::string box, cart, label;
		int plays = 0;
		long seconds = 0;
		time_t last = 0;
		int achA = -1, achB = 0;	// RetroAchievements: unlocked / total, as the emulator last said (-1: never said)
		bool fav = false;
	};

	struct Shelf
	{
		SystemDef def;
		bool favorites = false;		// the Favorites collection
		bool search = false;		// the Search collection (also favorites = true: a collection of copies, sysOf)
		std::vector<Game> games;	// favourites: copies, with sysOf giving each one's own system
		std::vector<int> sysOf;
		int order = 0;				// its place in systems.conf
	};

	class Library : public rnds::Source
	{
	public:
		// romRoot: where the system folders are; dataDir: Banana's own files (stats, favourites); assets: the theme's rnds/
		Library(const std::string& confPath, const std::string& romRoot, const std::string& dataDir, const std::string& assets);
		void scan();	// (re)read everything from disk
		// the folders whose changes change the library: the ROM root, each system's folder and its media folders
		std::vector<std::string> watchDirs() const;
		// the folders with system folders in them: romRoot, then the second SD card's roms folder while it's in and
		// Settings lets it be read (bananad's sd2 script names it in /run/banana/sd2/roms); its games' rel is "@sd2/..."
		std::vector<std::string> roots() const;
		// a favourite was starred or unstarred since the shelf was arranged: arrange() puts the Favorites collection
		// and the favourite systems right (from memory; the host calls it when the shelf is showing)
		bool favouritesChanged() const { return changed; }
		void arrange();
		// Search: the games whose name has every word of the query, as a collection first on the shelf (an empty query
		// takes it away); the shelf's index, or -1
		void setSearch(const std::string& q) { query = q; arrange(); }
		const std::string& searchQuery() const { return query; }
		int countMatches(const std::string& q) const;	// the games a search for q would find
		int searchShelf() const { return !shelves.empty() && shelves[0].search ? 0 : -1; }
		int pendingLaunch() const { return launchGame; }
		void clearLaunch() { launchGame = -1; }
		const Shelf& shelf(int i) const { return shelves[i]; }
		int currentShelf() const { return lib; }
		// the game's own system and entry, for a game of the current shelf (favourites point into their system)
		const SystemDef& systemOf(int game) const;
		const Game& gameAt(int game) const { return shelves[lib].games[game]; }
		// after a game ran: one more play, its time, now as its last play; and its RetroAchievements progress when the
		// emulator wrote it (unlocked total in /run/banana/ra-progress, docs/EMULATORS.md), achB 0 when it didn't
		void recordPlay(int game, long seconds, int achA = -1, int achB = 0);
		std::function<void(const std::string&)> onCue;
		bool twelveHour = false;

		// the battery: percent (-1 unknown) and whether it is charging (or full on the charger)
		struct Power { int percent = -1; bool charging = false; };
		Power power();
		Power testPower;	// headless: used instead of /sys when its percent is set
		Power powerSeen;	// the last reading of /sys and when (power(), online(): at most every 2 s)
		double powerAt = 0, onlineAt = 0;
		bool onlineSeen = false;

		// rnds::Source
		int systemCount() override { return (int)shelves.size(); }
		rnds::SysInfo system(int i) override;
		int gameCount() override;
		rnds::GameInfo game(int i) override;
		std::string clockText() override;
		std::string dateText() override;
		float battery() override;
		bool online() override;
		void selectSystem(int i) override { home = i; }
		void selectGame(int) override {}
		int openLibrary(int sys) override;
		void backToHome() override {}
		void launch(int game) override { launchGame = game; }
		void toggleSystemFav(int sys) override;
		void toggleGameFav(int game) override;
		int resumeGame(int sys) override;
		void cue(const std::string& name) override { if (onCue) onCue(name); }
		std::string emptyTitle() override;
		std::string emptyHint() override;
		std::vector<std::pair<std::string, std::string>> hints(bool library, int sys, int game) override;

	private:
		struct Look { std::string icon, maker; rnds::Color accent; bool hasAccent = false, app = false; };
		std::string confPath, romRoot, dataDir, assets;
		std::vector<SystemDef> defs;
		std::vector<Shelf> shelves;
		std::map<std::string, Look> looks;
		std::set<std::string> favGames, favSystems;	// "<system>/<rel>", system ids
		struct Stat { int plays = 0; long seconds = 0; time_t last = 0; int achA = -1, achB = 0; };
		std::map<std::string, Stat> stats;			// "<system>/<rel>"
		int lib = 0, home = 0, launchGame = -1;
		bool changed = false;
		std::string query;

		void loadDefs();
		void loadLooks();
		void loadState();
		void saveFavs();
		void saveStats();
		Look lookOf(const std::string& id) const;
		int resumeIndex(const Shelf& s) const;
		Game* sourceOf(int shelf, int game, int* sys = nullptr);
	};
}
