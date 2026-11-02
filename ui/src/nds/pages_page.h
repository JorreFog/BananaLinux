#pragma once
// banana-ui: the companion's pages (pages.h) as what the overlay shows (overlay.h's OverlayPage): the overlay owns the
// off-screen backend and the frame around a drawing, BananaDS the icon's corner (Back to game there closes without
// coming here; the rest of the wider button closes through wantsClose()).
#include "overlay.h"
#include "pages.h"
#include <algorithm>
#include <ctime>

namespace banana::nds
{
	class PagesPage : public OverlayPage
	{
	public:
		// assets: the rnds assets folder (fonts/); light: the theme's palette (pal(light), the theme already applied)
		PagesPage(Overlay& ov, const std::string& assets, bool light)
			: pages(&ov.backend(), 1, ov.width(), ov.height(), assets, light)
		{
			pages.setDecoderThread(true);	// (the paint loop never waits on a picture's decode)
		}

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
			// the touch's own time (BananaDS's CLOCK_MONOTONIC ms, 32 bits: its age by a wrapping difference), not the
			// loop's: touches come in batches, and a fling's speed is told by when the finger was where
			timespec ts;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			const double mono = ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
			const uint32_t age = (uint32_t)(uint64_t)mono - t.ms;
			const double at = t.ms && age < 2000 ? mono - age : mono;
			now = std::max(now, at);
			Pages::Action a = t.type == Touch::DOWN ? pages.touchDown(t.x, t.y, at) :
				t.type == Touch::MOVE ? pages.touchMove(t.x, t.y, at) : pages.touchUp(t.x, t.y, at);
			if (a == Pages::Action::REDRAW) dirty = true;
			if (a == Pages::Action::CLOSE) closing = true;
		}
		bool wantsClose() override { bool c = closing; closing = false; return c; }
		// the pages not shown yet drawn off-screen, one a call, so the first opening and each first tab are ready
		bool warm() override { return pages.warmNext(); }
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
