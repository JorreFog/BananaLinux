// banana-ui: the GBA Pokémon companion on the game panel (panel.h).
#include "gba/panel.h"
#include "gba/host.h"
#include "pokemon.h"

namespace banana::gba
{
	void Panel::open(Host* h, pkmn::Companion* c, const Look& look)
	{
		close();
		if (!h || !c || !c->active()) return;
		host = h;
		comp = c;
		pages = std::make_unique<nds::Pages>(look.be, look.screen, look.w, look.h, look.assets, look.light);
		pages->setDecoderThread(true);	// (the panel never waits on a picture's decode)
		pages->setBackLabel("Menu");	// (in the top-right corner, where the menu's icon is: the same spot switches)
		pages->show(nds::Page::PARTY);
		onPages = true;
		inBattle = false;
		comp->wantExt(true);	// (the pages need the save's data from the start: the place, the bag, the Pokédex)
	}

	void Panel::close()
	{
		pages.reset();
		host = nullptr;
		comp = nullptr;
	}

	bool Panel::active() const { return host && comp && comp->active(); }

	bool Panel::update(double now)
	{
		if (!active()) return false;
		bool changed = false;
		if (host->tick(now))
		{
			nds::PageData d;
			if (host->takeData(d)) pages->setData(d);
			changed = true;
		}
		host->markSeen(pages->takeSeen());
		if (pages->update(now)) changed = true;
		// a battle starting shows the battle, its end the party (unless another page was picked meanwhile; the tiles
		// stay the tiles)
		const bool b = comp->snapshot().inBattle;
		if (b != inBattle)
		{
			if (b) pages->show(nds::Page::BATTLE);
			else if (pages->page() == nds::Page::BATTLE) pages->show(nds::Page::PARTY);
			inBattle = b;
			changed = true;
		}
		return changed;
	}

	void Panel::draw()
	{
		if (pagesShown()) pages->draw();
	}

	void Panel::touch(bool down, int x, int y, double now)
	{
		if (!pagesShown()) return;
		const nds::Pages::Action a = down ? pages->touchDown(x, y, now) : pages->touchUp(x, y, now);
		if (a == nds::Pages::Action::CLOSE) onPages = false;
	}

	void Panel::touchMove(int x, int y, double now)
	{
		if (pagesShown()) pages->touchMove(x, y, now);
	}

	void Panel::showPages(double now)
	{
		if (!active()) return;
		if (!onPages) pages->opened(now);
		onPages = true;
	}

	int Panel::badge() const { return active() ? pages->unseen() : 0; }

	std::string Panel::view() const
	{
		if (!active()) return "";
		return pagesShown() ? std::string("pages:") + nds::pageName(pages->page()) : "game";
	}

	int Panel::ready() const { return !active() ? 0 : host->ready() ? 1 : host->unsupported() ? -2 : host->failed() ? -1 : 0; }
	std::string Panel::title() const { return active() ? host->title() : std::string(); }
}
