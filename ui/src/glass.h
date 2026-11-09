#pragma once
// banana-ui: frosted glass (the Glass themes). The theme's screen pictures (rnds::themeFiles' wallTop, wallBottom) are
// blurred once, and a glass box shows its piece of the blurred picture inside its corners, its tint drawn over it.
// Settings' panels under a theme's "settings.glass" (settings.cpp) use it; the layout themes bake theirs into their pictures.
#include "../rnds/RndsRaster.h"
#include <memory>

namespace banana
{
	// the signed distance from a rounded box (half sizes hw, hh; corners rad: top left, top right, bottom right, bottom
	// left) of the point px, py from its centre; grow: the box made that much bigger all round
	float boxDistance(float px, float py, float hw, float hh, const float rad[4], float grow);

	// a box blur of radius r, three passes each way: close to a Gaussian, and as fast for any r
	void boxBlur(rnds::Bitmap& b, int r);

	// the screen's picture (0 top, 1 bottom) filled to w x h, as the theme's pictures are drawn, and blurred by r device
	// px (0: sharp): made once and kept (the last few). Null when the theme has no picture
	std::shared_ptr<const rnds::Bitmap> glassBackdrop(int screen, int w, int h, int r);
	// the theme's pictures made ahead, sharp and blurred by r, on a thread of their own: the first Settings open
	// waited for them (two pictures read, filled and blurred, ~half a second on the RG DS)
	void glassPrewarm(int w, int h, int r);

	// a box's piece of a blurred picture: the part under it (at x, y device px, w x h), its corners rounded
	rnds::Bitmap glassPiece(const rnds::Bitmap& backdrop, int x, int y, int w, int h, const float rad[4]);
}
