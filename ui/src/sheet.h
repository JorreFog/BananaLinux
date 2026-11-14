#pragma once
// banana-ui: the quick menu's sheet, the way it comes onto the bottom screen and leaves it. Held for three seconds,
// the Thor button brings it up from below the panel with a bounce; pressed again, it hops and drops back down. The
// sheet covers the bottom screen only: what is on the top screen (the game) is never touched and never stops.
#include <cmath>
#include <cstdint>
#include <cstring>

namespace banana
{
	static constexpr double SHEET_UP_MS = 460, SHEET_DOWN_MS = 300;
	// how much of the sheet shows at t (0..1 of its way): 0 below the panel, 1 in its place, past 1 a little above
	// it. Up: a spring that passes its place by about 4% of the panel and settles. Down: a short hop up, then the
	// drop, faster the further it goes.
	inline float sheetUpAt(double t)
	{
		if (t <= 0) return 0;
		if (t >= 1) return 1;
		const double a = 9.5 * t;
		return (float)(1 - std::exp(-a) * std::cos(a));
	}
	inline float sheetDownAt(double t)
	{
		if (t <= 0) return 1;
		if (t >= 1) return 0;
		const float v = (float)(1 - (2.1 * t * t * t - 1.1 * t * t));
		return v < 0 ? 0 : v;
	}
	// one frame of it without a plane to move: the sheet (w x h) over what is under it, both the screen's size
	inline void sheetFrame(const uint32_t* under, const uint32_t* sheet, int w, int h, float shown, uint32_t* out)
	{
		int off = (int)((1.f - shown) * h + (shown < 1 ? .5f : -.5f));
		if (off > h) off = h;
		if (off < -h) off = -h;
		if (off >= 0)
		{
			if (off) memcpy(out, under, (size_t)w * off * 4);
			if (off < h) memcpy(out + (size_t)w * off, sheet, (size_t)w * (h - off) * 4);
		}
		else
		{
			// (past its place: its top rows over the panel's edge, and what is under it shows below its last row)
			memcpy(out, sheet + (size_t)w * -off, (size_t)w * (h + off) * 4);
			memcpy(out + (size_t)w * (h + off), under + (size_t)w * (h + off), (size_t)w * -off * 4);
		}
	}
}
