#pragma once
// banana-ui: the settings screen's colours (ROCKNIXDS Pixel's palettes, a user theme's over them), shared by the
// translation units that draw its pages (settings.cpp, settings_files.cpp, settings_bshare.cpp)
#include "../rnds/RndsUI.h"

namespace banana
{
	struct SPal
	{
		rnds::Color ink, muted, line, edge, screenBg, screenFrame, screenBand, ditherA, ditherB;
		rnds::Color panelBand, panelBody, panelLow, bubbleBand, bubbleBody, bubbleLow, slotOn, kbdBg, kbdInk, blackish, shade45;
		rnds::Color accent;
		// the selected row of a list: the yellow plate the shelf's cartridge stands on (the engine's START plate), dark
		// text on it, and a dark amber for what the accent marks elsewhere ("Connected", "Press A again")
		rnds::Color pick, pickBand, pickLow, pickEdge, onPick, onPickAccent;
	};
	SPal pal(bool light);
}
