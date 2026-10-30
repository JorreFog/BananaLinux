#pragma once
// Screen colours (#N3): one panel corrected towards the other. The RG DS's display controller (RK3566 VOP2) has a
// single colour table: the kernel takes one on one screen at a time, and switches it off for a frame to change it
// (so the calibration page previews in software). Per channel out = black + (1 - black) * gain * brightness * in^gamma.
// Kept in banana/screen-colors.conf (screen = top | bottom | none, red, green, blue, gamma, brightness, black).
#include <cstdint>
#include <string>

namespace banana
{
	struct ScreenColors
	{
		int screen = -1;		// 0 the top, 1 the bottom, -1 none
		float red = 1, green = 1, blue = 1, gamma = 1, bright = 1, black = 0;
		bool neutral() const { return screen < 0 || (red == 1 && green == 1 && blue == 1 && gamma == 1 && bright == 1 && black == 0); }
		// a channel (0 red, 1 green, 2 blue) through the curve, in and out 0..1
		float apply(int ch, float in) const;
		// an 0xAARRGGBB colour through it
		uint32_t apply(uint32_t argb) const;
		static ScreenColors load(const std::string& path);
		bool save(const std::string& path) const;
	};
}
