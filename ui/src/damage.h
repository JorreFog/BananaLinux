#pragma once
// banana-ui: the part of a screen that changed since the last frame

namespace banana
{
	// a rectangle of a screen that changed: [x0, x1) x [y0, y1)
	struct Damage
	{
		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		bool empty() const { return x0 >= x1 || y0 >= y1; }
		void add(const Damage& o)
		{
			if (o.empty()) return;
			if (empty()) { *this = o; return; }
			x0 = x0 < o.x0 ? x0 : o.x0; y0 = y0 < o.y0 ? y0 : o.y0;
			x1 = x1 > o.x1 ? x1 : o.x1; y1 = y1 > o.y1 ? y1 : o.y1;
		}
	};
}
