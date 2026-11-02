#pragma once
// banana-ui: the companion's pages (pages.h) as what the overlay shows (overlay.h's OverlayPage): the overlay owns the
// off-screen backend and the frame around a drawing, BananaDS the icon's corner (Back to game there closes without
// coming here; the rest of the wider button closes through wantsClose()).
#include "overlay.h"
#include "pages.h"

namespace banana::nds
{
	class PagesPage : public OverlayPage
	{
	public:
		// assets: the rnds assets folder (fonts/); light: the theme's palette (pal(light), the theme already applied)
		PagesPage(Overlay& ov, const std::string& assets, bool light)
			: pages(&ov.backend(), 1, ov.width(), ov.height(), assets, light) {}

		// new data from the readers and the logic (any thread's copy: the pointers are shared, the data not changed after)
		void setData(const PageData& d) { pages.setData(d); dirty = true; }
		void setLook(bool light) { pages.setLook(light); dirty = true; }
		Pages& raw() { return pages; }

		void opened() override { pages.opened(now); dirty = true; }
		bool tick(double nowMs) override
		{
			now = nowMs;
			bool again = pages.update(nowMs) || dirty;
			dirty = false;
			return again;
		}
		void draw(rnds::Backend&, int, int, const Rect& back) override
		{
			if (back.x != corner.x || back.y != corner.y || back.w != corner.w || back.h != corner.h)
			{
				corner = back;
				pages.setCorner(back.x, back.y, back.w, back.h);
			}
			pages.draw();
		}
		void touch(const Touch& t) override
		{
			Pages::Action a = t.type == Touch::DOWN ? pages.touchDown(t.x, t.y, now) :
				t.type == Touch::MOVE ? pages.touchMove(t.x, t.y, now) : pages.touchUp(t.x, t.y, now);
			if (a == Pages::Action::REDRAW) dirty = true;
			if (a == Pages::Action::CLOSE) closing = true;
		}
		bool wantsClose() override { bool c = closing; closing = false; return c; }
		int badge() const override { return pages.unseen(); }
		// the notices seen since the last call, for the logic to keep as seen
		std::vector<std::string> takeSeen() { return pages.takeSeen(); }

	private:
		Pages pages;
		Rect corner;
		double now = 0;
		bool dirty = true, closing = false;
	};
}
