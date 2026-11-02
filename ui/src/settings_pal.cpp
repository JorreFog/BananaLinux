#include "settings_draw.h"

// the settings screen's palette (settings_draw.h), on its own so what else draws in the menu's look (the DS companion's
// pages, nds/pages.cpp) and its tests can have it without the settings
namespace banana
{
	using rnds::Color;

	SPal pal(bool light)
	{
		SPal p;
		if (!light)
		{
			p.ink = Color::hex(0xf1ede2); p.muted = Color::hex(0xa39d8e); p.line = Color::hex(0x8a8374); p.edge = Color::hex(0xe8e2d3);
			p.screenBg = Color::hex(0x17161c); p.screenFrame = Color::hex(0x0b0a0d); p.screenBand = Color::hex(0x2e2b33);
			p.ditherA = Color(1, 1, 1, 0.14f); p.ditherB = Color(1, 1, 1, 0.07f);
			p.panelBand = Color::hex(0x3a3540); p.panelBody = Color::hex(0x17161c); p.panelLow = Color::hex(0x0b0a0d);
			p.bubbleBand = Color::hex(0x38333b); p.bubbleBody = Color::hex(0x191820); p.bubbleLow = Color::hex(0x0f0e13);
			p.slotOn = Color::hex(0xffffff); p.kbdBg = Color::hex(0xf1ede2); p.kbdInk = Color::hex(0x17161c);
			p.blackish = Color::hex(0x0b0a0d); p.shade45 = Color(0, 0, 0, 0.45f);
		}
		else
		{
			Color sh = Color::hex(0x3a2e14);
			p.ink = Color::hex(0x2a2620); p.muted = Color::hex(0x7a7262); p.line = Color::hex(0xa69d8c); p.edge = Color::hex(0x3a342c);
			p.screenBg = Color::hex(0xf3efe6); p.screenFrame = Color::hex(0xd4ccbd); p.screenBand = Color::hex(0xfdfcf9);
			p.ditherA = Color(sh.r, sh.g, sh.b, 0.07f); p.ditherB = Color(sh.r, sh.g, sh.b, 0.035f);
			p.panelBand = Color::hex(0xffffff); p.panelBody = Color::hex(0xf6f2ea); p.panelLow = Color::hex(0xd8d0c0);
			p.bubbleBand = Color::hex(0xffffff); p.bubbleBody = Color::hex(0xfaf7f0); p.bubbleLow = Color::hex(0xddd5c6);
			p.slotOn = Color::hex(rnds::accent.base); p.kbdBg = Color::hex(0x3a342c); p.kbdInk = Color::hex(0xf2f6fa);
			p.blackish = Color(sh.r, sh.g, sh.b, 0.3f); p.shade45 = Color(sh.r, sh.g, sh.b, 0.25f);
		}
		p.accent = Color::hex(rnds::tintAccent(light ? 0xffc07a00u : 0xffffd23fu) & 0xffffff);	// the accent for values being changed
		p.pick = Color::hex(rnds::accent.base); p.pickBand = Color::hex(rnds::accent.band); p.pickLow = Color::hex(rnds::accent.low);
		p.pickEdge = light ? Color::hex(rnds::accent.dark) : Color::hex(0xffffff);
		// the text on the picked row (the accent): dark or light by contrast, and its second line (a hint, a value) in
		// the accent's own hue, darker or lighter until it reads (was always a brown: right on yellow only)
		p.onPick = Color::hex(rnds::inkOn(rnds::accent.base)); p.onPickAccent = Color::hex(rnds::subInkOn(rnds::accent.base));
		// a user theme's colours by the same names as the engine's palette (RndsUI.cpp), and two of this screen's own
		struct { const char* n; Color SPal::* m; } names[] = {
			{ "ink", &SPal::ink }, { "muted", &SPal::muted }, { "line", &SPal::line }, { "edge", &SPal::edge },
			{ "screenBg", &SPal::screenBg }, { "screenFrame", &SPal::screenFrame }, { "screenBand", &SPal::screenBand },
			{ "ditherA", &SPal::ditherA }, { "ditherB", &SPal::ditherB }, { "panelBand", &SPal::panelBand },
			{ "panelBody", &SPal::panelBody }, { "panelLow", &SPal::panelLow }, { "bubbleBand", &SPal::bubbleBand },
			{ "bubbleBody", &SPal::bubbleBody }, { "bubbleLow", &SPal::bubbleLow }, { "slotOn", &SPal::slotOn },
			{ "kbdBg", &SPal::kbdBg }, { "kbdInk", &SPal::kbdInk }, { "blackish", &SPal::blackish }, { "shade45", &SPal::shade45 },
			{ "onPick", &SPal::onPick }, { "onPickAccent", &SPal::onPickAccent },
		};
		for (auto& e : names) rnds::themeColor(e.n, p.*e.m);
		return p;
	}
}
