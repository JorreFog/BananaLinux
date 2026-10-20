#include "colors.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <unistd.h>

namespace banana
{
	float ScreenColors::apply(int ch, float in) const
	{
		float gain = ch == 0 ? red : ch == 1 ? green : blue;
		float v = black + (1 - black) * gain * bright * std::pow(std::max(0.0f, std::min(1.0f, in)), gamma);
		return std::max(0.0f, std::min(1.0f, v));
	}

	uint32_t ScreenColors::apply(uint32_t c) const
	{
		uint32_t out = c & 0xff000000;
		for (int ch = 0; ch < 3; ch++)
		{
			int sh = 16 - ch * 8;
			float v = apply(ch, ((c >> sh) & 255) / 255.0f);
			out |= (uint32_t)std::lround(v * 255) << sh;
		}
		return out;
	}

	ScreenColors ScreenColors::load(const std::string& path)
	{
		ScreenColors c;
		std::ifstream f(path);
		std::string ln;
		while (std::getline(f, ln))
		{
			size_t eq = ln.find('=');
			if (ln.empty() || ln[0] == '#' || eq == std::string::npos) continue;
			std::string k = ln.substr(0, eq), v = ln.substr(eq + 1);
			k.erase(k.find_last_not_of(" \t") + 1); v.erase(0, v.find_first_not_of(" \t")); v.erase(v.find_last_not_of(" \t\r") + 1);
			float x = (float)atof(v.c_str());
			if (k == "screen") c.screen = v == "top" ? 0 : v == "bottom" ? 1 : -1;
			else if (k == "red") c.red = std::max(0.5f, std::min(1.0f, x));
			else if (k == "green") c.green = std::max(0.5f, std::min(1.0f, x));
			else if (k == "blue") c.blue = std::max(0.5f, std::min(1.0f, x));
			else if (k == "gamma") c.gamma = std::max(0.6f, std::min(1.6f, x));
			else if (k == "brightness") c.bright = std::max(0.4f, std::min(1.0f, x));
			else if (k == "black") c.black = std::max(0.0f, std::min(0.12f, x));
		}
		return c;
	}

	bool ScreenColors::save(const std::string& path) const
	{
		std::string tmp = path + ".new";
		FILE* f = fopen(tmp.c_str(), "w");
		if (!f) return false;
		fprintf(f, "# Settings > Display & sound > Screen colours: one screen corrected towards the other (the handheld has one\n"
			"# colour table). Per channel out = black + (1 - black) * gain * brightness * in^gamma.\n"
			"screen = %s\nred = %.2f\ngreen = %.2f\nblue = %.2f\ngamma = %.2f\nbrightness = %.2f\nblack = %.3f\n",
			screen == 0 ? "top" : screen == 1 ? "bottom" : "none", red, green, blue, gamma, bright, black);
		bool ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
		ok &= fclose(f) == 0;
		if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
		return true;
	}
}
