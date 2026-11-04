#pragma once
// banana-ui: the DS companion's pages for a GBA Pokémon game (docs/superpowers/specs/2026-11-03-gba-pokemon-companion-
// design.md). While today's GBA companion (pokemon.h: pkmn::Companion) reads the game's memory, this works the rest
// out as the DS companion does: the ROM's data as an nds::RomData (gen3rom.h, read once on a thread when the
// companion's tables are), the game's guide (gba-pokemon/<game>.json), a Session on its own thread fed with each new
// Snapshot as an nds::Live (gen3live.h: the journey log, the advice, the notices), and the PageData the game panel's
// pages draw (Settings shows them in place of today's Party/Battle/Dex/Guide views).
#include "../nds/model.h"
#include "../nds/pages.h"
#include "../nds/session.h"
#include "../pokemon.h"
#include <functional>
#include <future>
#include <memory>
#include <string>

namespace banana::gba
{
	class Host
	{
	public:
		// c: the game's companion (open, a retail game); stateDir: where the journey log is kept on the card
		// (cache/gba-companion); cacheDir: the RomData's cache ("" none). challenge: the Settings row's
		Host(pkmn::Companion& c, std::string stateDir, std::string cacheDir, bool challenge);
		~Host();
		// each turn of the menu's loop: the loaded data picked up, a new Snapshot handed to the Session, the Session's
		// newest taken. True when the pages have new data (takeData)
		bool tick(double nowMs);
		bool ready() const { return base.rom != nullptr; }	// the ROM's data read: the pages can show
		bool failed() const { return loadFailed; }			// it couldn't be (not one of the five games): today's views stay
		bool takeData(nds::PageData& out);
		// what the pages saw (Pages::takeSeen) and the season (none in Gen III)
		void markSeen(const std::vector<std::string>& ids);
		std::string title() const { return base.rom ? base.rom->game.title : std::string(); }
		uint64_t fed = 0;		// (statistics: Snapshots handed over)
	private:
		pkmn::Companion& comp;
		std::string stateDir, cacheDir;
		bool challenge;
		std::future<nds::PageData> load;
		bool loading = false, loadFailed = false;
		nds::PageData base, out;
		bool fresh = false;
		std::unique_ptr<nds::SessionThread> session;
		uint64_t lastSerial = 0;
		std::function<uint16_t(uint16_t)> itemMap;	// (the game's item ids in the RomData's numbering)
		std::function<bool(uint16_t, uint16_t&, uint8_t&)> speciesMap;	// (its species: national number and form)
	};
}
