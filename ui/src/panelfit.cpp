// banana-ui: the canvas fitted to a panel of another size or way round (panelfit.h)
#include "panelfit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace banana
{
	static int normRot(int r)
	{
		r %= 360;
		if (r < 0) r += 360;
		return r == 90 || r == 180 || r == 270 ? r : 0;
	}

	void PanelFit::set(int lw_, int lh_, int pw_, int ph_, int rot_)
	{
		lw = lw_; lh = lh_; pw = pw_; ph = ph_; rot = normRot(rot_);
		bool side = rot == 90 || rot == 270;
		uw = side ? ph : pw; uh = side ? pw : ph;
		ident = rot == 0 && lw == pw && lh == ph;
		s = std::min((double)uw / lw, (double)uh / lh);
		dw = std::min(uw, (int)std::lround(lw * s)); dh = std::min(uh, (int)std::lround(lh * s));
		ox = (uw - dw) / 2; oy = (uh - dh) / 2;
		colOf.assign(uw, -1); rowOf.assign(uh, -1);
		for (int x = 0; x < dw; x++) colOf[ox + x] = std::min(lw - 1, (int)((x + 0.5) * lw / dw));
		for (int y = 0; y < dh; y++) rowOf[oy + y] = std::min(lh - 1, (int)((y + 0.5) * lh / dh));
	}

	// rot is how far the canvas is turned clockwise on the panel: 90 puts the upright picture's top along the panel's
	// right edge
	void PanelFit::toPhys(int ux, int uy, int& px, int& py) const
	{
		switch (rot)
		{
		case 90: px = pw - 1 - uy; py = ux; break;
		case 180: px = pw - 1 - ux; py = ph - 1 - uy; break;
		case 270: px = uy; py = ph - 1 - ux; break;
		default: px = ux; py = uy; break;
		}
	}

	void PanelFit::toUpright(int px, int py, int& ux, int& uy) const
	{
		switch (rot)
		{
		case 90: ux = py; uy = pw - 1 - px; break;
		case 180: ux = pw - 1 - px; uy = ph - 1 - py; break;
		case 270: ux = ph - 1 - py; uy = px; break;
		default: ux = px; uy = py; break;
		}
	}

	Damage PanelFit::toPanel(const Damage& d) const
	{
		if (d.empty()) return Damage();
		if (ident) return d;
		// the canvas rectangle on the upright panel, rounded out by a pixel (nearest-neighbour's edges)
		int x0 = ox + (int)std::floor(d.x0 * s) - 1, x1 = ox + (int)std::ceil(d.x1 * s) + 1;
		int y0 = oy + (int)std::floor(d.y0 * s) - 1, y1 = oy + (int)std::ceil(d.y1 * s) + 1;
		x0 = std::max(x0, ox); y0 = std::max(y0, oy); x1 = std::min(x1, ox + dw); y1 = std::min(y1, oy + dh);
		if (x0 >= x1 || y0 >= y1) return Damage();
		// its corners turned onto the panel
		int ax, ay, bx, by;
		toPhys(x0, y0, ax, ay);
		toPhys(x1 - 1, y1 - 1, bx, by);
		Damage o;
		o.x0 = std::min(ax, bx); o.x1 = std::max(ax, bx) + 1;
		o.y0 = std::min(ay, by); o.y1 = std::max(ay, by) + 1;
		return o;
	}

	bool PanelFit::toCanvas(float px, float py, float& lx, float& ly) const
	{
		if (ident) { lx = px; ly = py; return px >= 0 && py >= 0 && px < lw && py < lh; }
		int ux, uy;
		toUpright((int)px, (int)py, ux, uy);
		if (ux < ox || uy < oy || ux >= ox + dw || uy >= oy + dh) return false;
		lx = (float)((ux - ox + 0.5) / s);
		ly = (float)((uy - oy + 0.5) / s);
		lx = std::min(lx, (float)lw - 1); ly = std::min(ly, (float)lh - 1);
		return true;
	}

	bool PanelFit::touchToCanvas(float nx, float ny, bool wide, float& lx, float& ly) const
	{
		bool panelWide = pw > ph, uprightWide = uw > uh;
		if (panelWide == uprightWide || wide == panelWide) return toCanvas(nx * pw, ny * ph, lx, ly);
		// the touchscreen is turned already: its range is the upright panel's
		float ux = nx * uw, uy = ny * uh;
		if (ux < ox || uy < oy || ux >= ox + dw || uy >= oy + dh) return false;
		lx = std::min((float)((ux - ox) / s), (float)lw - 1);
		ly = std::min((float)((uy - oy) / s), (float)lh - 1);
		return true;
	}

	void PanelFit::blit(const uint32_t* canvas, uint8_t* dst, uint32_t pitch, const Damage& d) const
	{
		if (ident)
		{
			size_t n = (size_t)(d.x1 - d.x0) * 4;
			for (int y = d.y0; y < d.y1; y++)
				memcpy(dst + (size_t)y * pitch + (size_t)d.x0 * 4, canvas + (size_t)y * lw + d.x0, n);
			return;
		}
		Damage p = toPanel(d);
		if (p.empty()) return;
		// a row at a time into a local line, then one copy: the scanout buffer is uncached
		std::vector<uint32_t> line(p.x1 - p.x0);
		for (int py = p.y0; py < p.y1; py++)
		{
			for (int px = p.x0; px < p.x1; px++)
			{
				int ux, uy;
				toUpright(px, py, ux, uy);
				int cx = colOf[ux], cy = rowOf[uy];
				line[px - p.x0] = cx < 0 || cy < 0 ? 0 : canvas[(size_t)cy * lw + cx];
			}
			memcpy(dst + (size_t)py * pitch + (size_t)p.x0 * 4, line.data(), line.size() * 4);
		}
	}

	void canvasFor(int w0, int h0, int rot0, int w1, int h1, int rot1, int& lw, int& lh)
	{
		if (const char* e = getenv("BANANA_CANVAS"))
			if (sscanf(e, "%dx%d", &lw, &lh) == 2 && lw >= 320 && lh >= 240 && lw <= 4096 && lh <= 4096) return;
		rot0 = normRot(rot0); rot1 = normRot(rot1);
		if (w0 == w1 && h0 == h1 && rot0 == 0 && rot1 == 0) { lw = w0; lh = h0; return; }
		int uw = rot0 == 90 || rot0 == 270 ? h0 : w0, uh = rot0 == 90 || rot0 == 270 ? w0 : h0;
		lh = std::min(uh, uw * 3 / 4);
		lh -= lh % 2;
		lw = lh * 4 / 3;
		lw -= lw % 2;
	}

	void panelRotations(int& top, int& bottom)
	{
		top = bottom = 0;
		if (const char* e = getenv("BANANA_PANEL_ROTATE")) sscanf(e, "%d,%d", &top, &bottom);
		top = normRot(top); bottom = normRot(bottom);
	}
}
