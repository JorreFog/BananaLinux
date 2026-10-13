#pragma once
// The Carts theme's cartridges: a Nintendo DS game card, modelled at its real size (35 x 33 x 3.8 mm) as a signed
// distance field: the notched top-right corner, filleted edges, the seam where the two shells meet, the notch in its
// left side for the slot's spring, a recessed label well with a bevelled lip, three grip ridges and the insertion
// arrow above it. Every system's games get this card, the shell in the system's colour. The card is ray-marched once
// per pose into a geometry buffer (which material each pixel shows, where on the label, how much light and shine it
// gets: three lights, ambient occlusion, and a photo studio's softboxes reflected with Fresnel, softly on the satin
// plastic and sharper on the laminated label); a game's cartridge is then only its art composited into that buffer
// (a few milliseconds), its highlights bloomed and rolled off by a soft shoulder instead of clipping.
#include "../rnds/RndsRaster.h"
#include <string>

namespace banana
{
	struct CartPose
	{
		float yaw = -25;	// degrees, turned to the left: its right side shows
		float pitch = 8;	// degrees, tipped back: its top edge shows
		int height = 300;	// the picture's height in pixels (its width follows the angle)
	};
	// the card for a system id ("gba", "nds", "n64", ...: the shell's colour), with label art (RGBA, any size: covered
	// into the label below its band; empty: a plain label with the system's icon, RGBA, in the middle) and the band's
	// text (an A8 bitmap, drawn centred in the light band across the label's top; empty: a plain band)
	rnds::Bitmap renderCart(const std::string& system, const rnds::Bitmap& label, const CartPose& pose,
		const rnds::Bitmap& icon = rnds::Bitmap(), const rnds::Bitmap& bandText = rnds::Bitmap());
	// the empty margin round the card in its picture (room for its highlights' bloom), as a share of the picture's height:
	// the card stands that far above the picture's bottom edge
	const float CART_MARGIN = 0.03f / 1.06f;
	// the card seen straight on: width / height (the same for every system)
	float cartAspect(const std::string& system);
	// the label's size in millimetres below the band (for rendering the band's text and covering the art to fit)
	void cartLabelSize(float& w, float& bandH, float& artH);
}
