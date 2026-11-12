#pragma once
// banana-ui: the menu's canvas on a panel of another size or way round. The menu draws both screens on one canvas
// size (4:3, as the RG DS's two equal panels); a handheld whose panels differ (the AYN Thor: a 16:9 top and a
// near-square bottom, either possibly mounted portrait) shows that canvas on each panel scaled to fit, centred,
// black around it, and turned by the panel's mounting. Nearest-neighbour: the menu is pixel art and flat colour.
// On a panel the canvas's own size and upright (the RG DS) the fit is the identity and the display copies rows
// as before.
#include <cstdint>
#include <vector>
#include "damage.h"

namespace banana
{
	struct PanelFit
	{
		// lw x lh: the canvas; pw x ph: the panel's mode (its scanout buffer); rot: how the panel is mounted, the
		// clockwise degrees (0, 90, 180, 270) the canvas is turned by to be upright on it
		void set(int lw, int lh, int pw, int ph, int rot);
		bool identity() const { return ident; }
		int panelW() const { return pw; }
		int panelH() const { return ph; }
		// the panel's pixels a canvas rectangle lands on (empty if none)
		Damage toPanel(const Damage& d) const;
		// a point on the panel (px, py) to the canvas; false outside the picture (the black bars)
		bool toCanvas(float px, float py, float& lx, float& ly) const;
		// a touch as a fraction of the touchscreen's range (nx, ny in 0..1) to the canvas. wide: the touchscreen's
		// range is wider than tall. A touchscreen whose range has the panel's own shape reports the panel's pixels
		// (turned with it); one whose range has the upright shape (the device tree's touchscreen-swapped-x-y and
		// -inverted-x/y already turned it) reports the upright picture's
		bool touchToCanvas(float nx, float ny, bool wide, float& lx, float& ly) const;
		// the canvas's rectangle d drawn into the panel's buffer (rows of pitch bytes, XRGB8888)
		void blit(const uint32_t* canvas, uint8_t* dst, uint32_t pitch, const Damage& d) const;

		// the upright picture's place on the upright panel: (ox, oy), dw x dh, at scale s
		int ox = 0, oy = 0, dw = 0, dh = 0;
		double s = 1;

	private:
		int lw = 0, lh = 0, pw = 0, ph = 0, rot = 0, uw = 0, uh = 0;
		bool ident = true;
		std::vector<int> colOf, rowOf;	// upright panel column / row -> canvas column / row, -1 in the bars
		// a point of the upright panel to the panel, and back
		void toPhys(int ux, int uy, int& px, int& py) const;
		void toUpright(int px, int py, int& ux, int& uy) const;
	};

	// the canvas for two panels: their common size when they have one (and are upright), else 4:3 fitted to the top
	// panel upright, height first (the Thor's 1920x1080 top: 1440x1080). BANANA_CANVAS=WxH overrides.
	void canvasFor(int w0, int h0, int rot0, int w1, int h1, int rot1, int& lw, int& lh);
	// BANANA_PANEL_ROTATE=<top>,<bottom> (degrees clockwise; the board's profile sets it: /etc/banana/hw.conf)
	void panelRotations(int& top, int& bottom);
}
