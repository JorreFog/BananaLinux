#include "colors.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
		// (a file edited by hand: keys and values trimmed on both sides, the screen's name in any case, and a value
		// that isn't a number, or is empty, keeps the default: atof's 0 became the bottom of the range, a strong cast)
		ScreenColors c;
		std::ifstream f(path);
		std::string ln;
		auto trim = [](std::string t) { t.erase(0, t.find_first_not_of(" \t\r")); t.erase(t.find_last_not_of(" \t\r") + 1); return t; };
		auto num = [](const std::string& v, float& out, float lo, float hi)
		{
			char* end = nullptr;
			float x = strtof(v.c_str(), &end);
			if (v.empty() || !end || *end != 0 || x != x) return;
			out = std::max(lo, std::min(hi, x));
		};
		while (std::getline(f, ln))
		{
			size_t eq = ln.find('=');
			std::string k = trim(ln.substr(0, eq == std::string::npos ? 0 : eq));
			if (k.empty() || k[0] == '#' || eq == std::string::npos) continue;
			std::string v = trim(ln.substr(eq + 1));
			for (auto& ch : k) ch = (char)tolower((unsigned char)ch);
			if (k == "screen") { for (auto& ch : v) ch = (char)tolower((unsigned char)ch); c.screen = v == "top" ? 0 : v == "bottom" ? 1 : -1; }
			else if (k == "red") num(v, c.red, 0.5f, 1);
			else if (k == "green") num(v, c.green, 0.5f, 1);
			else if (k == "blue") num(v, c.blue, 0.5f, 1);
			else if (k == "gamma") num(v, c.gamma, 0.6f, 1.6f);
			else if (k == "brightness") num(v, c.bright, 0.4f, 1);
			else if (k == "black") num(v, c.black, 0, 0.12f);
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
