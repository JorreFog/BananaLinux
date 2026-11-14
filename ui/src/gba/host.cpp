// banana-ui: the DS companion's pages for a GBA Pokémon game (host.h).
#include "gba/host.h"
#include "gba/expansion.h"
#include "gba/gen3live.h"
#include "gba/gen3rom.h"
#include "nds/guide.h"
#include <chrono>
#include <cstdio>
#include <ctime>

namespace banana::gba
{
	Host::Host(pkmn::Companion& c, std::string sd, bool ch)
		: comp(c), stateDir(std::move(sd)), challenge(ch) {}

	Host::~Host()
	{
		session.reset();	// (stopped first: it saves the journey on this thread)
		if (load.valid()) load.wait();
	}

	bool Host::tick(double now)
	{
		// (till the ROM's data is read: the pages say so)
		if (!started) { started = true; out = nds::PageData(); out.reading = true; fresh = true; }
		// the ROM's data: once the companion's tables are read, on a thread of its own
		if (!loading && !base.rom && !loadFailed)
		{
			std::shared_ptr<const pkmn::RomTables> t = comp.tables();
			if (t && t->rom)
			{
				loading = true;
				load = std::async(std::launch::async, [t]
				{
					nds::PageData d;
					const auto t0 = std::chrono::steady_clock::now();
					bool unsupported = false;
					d.rom = readGame(*t, nullptr, &unsupported);	// (by the CRC-32 the companion worked out)
					if (!d.rom) { d.unsupported = unsupported; return d; }
					auto gd = std::make_shared<nds::GuideData>();
					const nds::GuideResolver res = nds::guideResolver(*d.rom);
					std::string why;
					if (!nds::loadGuide(d.rom->game, *gd, &why, &res))
					{
						fprintf(stderr, "banana-ui: gba companion: no guide: %s\n", why.c_str());
						*gd = nds::GuideData();
					}
					d.guide = gd;
					fprintf(stderr, "banana-ui: gba companion: %s read in %.0f ms\n", d.rom->game.title.c_str(),
						std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
					return d;
				});
			}
		}
		if (loading && load.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			loading = false;
			base = load.get();
			if (!base.rom)	// ("Not supported" for a ROM hack not in the list, else "No game data")
			{
				loadFailed = true;
				out = nds::PageData();
				out.unsupported = base.unsupported;
				fresh = true;
				return true;
			}
			nds::useModernTypeChart(base.rom->game.modern);	// (Fairy and Generation VI's chart for a hack that has them)
			auto ss = std::make_unique<nds::Session>(base.rom, base.guide, stateDir);
			ss->setChallenge(challenge);
			session = std::make_unique<nds::SessionThread>(std::move(ss));
			out = base;
			fresh = true;
			lastSerial = 0;
		}
		if (!session) return fresh;
		// a new Snapshot: as Live to the Session
		if (comp.serial() != lastSerial)
		{
			lastSerial = comp.serial();
			auto t = comp.tables();
			const nds::RomData& r = *base.rom;
			if (!itemMap)	// (an expansion build's own item ids, else the Gen III reader's map)
			{
				itemMap = expansionItemMap(&r);
				if (!itemMap) itemMap = [&r](uint16_t i) { return itemId(r, i); };
				uint16_t n = 0; uint8_t f = 0;	// (species by the Gen III reader's own table where it has one: hacks' forms)
				if (speciesOf(r, 1, n, f)) speciesMap = [&r](uint16_t s, uint16_t& nat, uint8_t& form) { return speciesOf(r, s, nat, form); };
			}
			auto l = toLive(comp.snapshot(), t.get(), base.rom.get(), itemMap, speciesMap);
			if (l->valid) { session->feed(l); fed++; }
		}
		// no save to show yet: why, when the emulator says (no memory map in this core, a release with no profile)
		const pkmn::Snapshot& cs = comp.snapshot();
		const std::string why = cs.seen ? "" : cs.unknownRelease ? "This release of the game isn't one the companion knows." :
			!cs.error.empty() ? "RetroArch can't read the game's memory with this emulator: " + cs.error : "";
		if (why != base.waitWhy) { base.waitWhy = why; out.waitWhy = why; fresh = true; }
		nds::SessionThread::Snapshot sn;
		if (session->take(sn))
		{
			out = base;
			out.live = sn.live;
			out.advice = sn.advice;
			out.journey = sn.journey;
			out.seenNotices = sn.seen;
			fresh = true;
		}
		(void)now;
		return fresh;
	}

	bool Host::takeData(nds::PageData& d)
	{
		if (!fresh) return false;
		d = out;
		fresh = false;
		return true;
	}

	void Host::markSeen(const std::vector<std::string>& ids)
	{
		if (session && !ids.empty()) session->markSeen(ids);
	}
}
