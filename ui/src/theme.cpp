#include "theme.h"
#include "../rnds/RndsUI.h"
#include "../third_party/stb_image.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>

namespace banana
{
	static const char* CARD_DIR = "/data/banana/themes";
	static const char* IMAGE_DIR = "/usr/share/banana/themes";

	// ---- a small JSON reader: objects, strings, numbers, true/false/null, arrays skipped; enough for a theme --------
	struct Json
	{
		const std::string& s;
		size_t i = 0;
		bool ok = true;
		explicit Json(const std::string& t) : s(t) {}
		void ws() { while (i < s.size() && (isspace((unsigned char)s[i]) || s[i] == '/')) { if (s[i] == '/') skipComment(); else i++; } }
		void skipComment()	// // and /* */ comments are allowed in a theme (JSON itself has none)
		{
			if (i + 1 < s.size() && s[i + 1] == '/') { while (i < s.size() && s[i] != '\n') i++; }
			else if (i + 1 < s.size() && s[i + 1] == '*') { size_t e = s.find("*/", i + 2); i = e == std::string::npos ? s.size() : e + 2; }
			else { ok = false; i = s.size(); }
		}
		std::string str()
		{
			std::string r;
			if (s[i] != '"') { ok = false; return r; }
			for (i++; i < s.size() && s[i] != '"'; i++)
			{
				if (s[i] == '\\' && i + 1 < s.size()) { i++; r += s[i] == 'n' ? '\n' : s[i] == 't' ? '\t' : s[i]; }
				else r += s[i];
			}
			if (i >= s.size()) ok = false;
			i++;
			return r;
		}
		// a value: strings and numbers as text, an object's members flattened into out as "key.sub", the rest skipped
		void value(const std::string& key, std::map<std::string, std::string>& out)
		{
			ws();
			if (i >= s.size()) { ok = false; return; }
			if (s[i] == '"') { out[key] = str(); return; }
			if (s[i] == '{') { object(key, out); return; }
			if (s[i] == '[') { int d = 0; do { if (s[i] == '[') d++; else if (s[i] == ']') d--; else if (s[i] == '"') { str(); continue; } i++; } while (i < s.size() && d > 0); return; }
			size_t b = i;
			while (i < s.size() && (isalnum((unsigned char)s[i]) || s[i] == '.' || s[i] == '-' || s[i] == '+')) i++;
			if (b == i) { ok = false; return; }
			out[key] = s.substr(b, i - b);
		}
		void object(const std::string& prefix, std::map<std::string, std::string>& out)
		{
			i++;	// {
			for (;;)
			{
				ws();
				if (i < s.size() && s[i] == '}') { i++; return; }
				std::string k = str();
				ws();
				if (i >= s.size() || s[i] != ':') { ok = false; return; }
				i++;
				value(prefix.empty() ? k : prefix + "." + k, out);
				if (!ok) return;
				ws();
				if (i < s.size() && s[i] == ',') { i++; continue; }
				if (i < s.size() && s[i] == '}') { i++; return; }
				ok = false; return;
			}
		}
	};

	static bool readTheme(const std::string& path, std::map<std::string, std::string>& out, std::string* why)
	{
		std::ifstream f(path);
		if (!f) { if (why) *why = "can't open " + path; return false; }
		std::stringstream ss; ss << f.rdbuf();
		std::string t = ss.str();
		Json j(t);
		j.ws();
		if (j.i >= t.size() || t[j.i] != '{') { if (why) *why = path + ": not a JSON object"; return false; }
		j.object("", out);
		if (!j.ok) { if (why) *why = path + ": a JSON error near byte " + std::to_string(j.i); return false; }
		return true;
	}

	// "#rgb", "#rrggbb" or "#rrggbbaa" (CSS order) -> 0xAARRGGBB
	static bool parseColor(std::string v, uint32_t& out)
	{
		if (v.empty() || v[0] != '#') return false;
		v.erase(0, 1);
		if (!std::all_of(v.begin(), v.end(), [](char c) { return isxdigit((unsigned char)c); })) return false;
		if (v.size() == 3) v = std::string{ v[0], v[0], v[1], v[1], v[2], v[2] };
		if (v.size() == 6) v += "ff";
		if (v.size() != 8) return false;
		uint32_t rgba = (uint32_t)strtoul(v.c_str(), nullptr, 16);
		out = (rgba >> 8) | (rgba & 0xff) << 24;
		return true;
	}
	static uint32_t mix(uint32_t c, uint32_t to, float t)	// RGB only
	{
		auto ch = [&](int sh) { float a = (c >> sh) & 0xff, b = (to >> sh) & 0xff; return (uint32_t)(a + (b - a) * t + 0.5f) << sh; };
		return ch(16) | ch(8) | ch(0);
	}

	static std::string pathFor(const std::string& stem)
	{
		std::string p = std::string(CARD_DIR) + "/" + stem + ".json";
		if (std::ifstream(p)) return p;
		return std::string(IMAGE_DIR) + "/" + stem + ".json";
	}

	std::vector<UserTheme> userThemes()
	{
		std::vector<UserTheme> r;
		for (const char* dir : { CARD_DIR, IMAGE_DIR })
		{
			DIR* d = opendir(dir);
			if (!d) continue;
			std::vector<std::string> stems;
			while (dirent* e = readdir(d))
			{
				std::string n = e->d_name;
				if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0 && n[0] != '.') stems.push_back(n.substr(0, n.size() - 5));
			}
			closedir(d);
			std::sort(stems.begin(), stems.end());
			for (auto& st : stems)
			{
				if (std::any_of(r.begin(), r.end(), [&](const UserTheme& u) { return u.value == "user:" + st; })) continue;
				std::map<std::string, std::string> kv;
				std::string name = st;
				if (readTheme(std::string(dir) + "/" + st + ".json", kv, nullptr) && !kv["name"].empty()) name = kv["name"];
				r.push_back({ "user:" + st, name });
			}
		}
		return r;
	}

	// a file a theme names: relative to its folder (or absolute), used only when it's there
	static std::string themeFile(const std::string& dir, const std::string& name)
	{
		if (name.empty()) return "";
		std::string p = name[0] == '/' ? name : dir + "/" + name;
		return std::ifstream(p) ? p : "";
	}

	// a picture the theme names (a wallpaper, the logo, an icon): only one stb_image reads (PNG or JPEG) and at most
	// 4096 px a side, checked from its header before anything is decoded (an 8192 x 8192 wallpaper decoded is 256 MB,
	// and banana-ui stays in memory under the games); otherwise the default is used, and the log says why
	static std::string themeImage(const std::string& dir, const std::string& name)
	{
		std::string p = themeFile(dir, name);
		if (p.empty())
		{
			if (!name.empty()) fprintf(stderr, "banana-ui: theme: %s isn't there (the default instead)\n", name.c_str());
			return "";
		}
		int w = 0, h = 0, n = 0;
		if (!stbi_info(p.c_str(), &w, &h, &n))
		{
			fprintf(stderr, "banana-ui: theme: %s isn't a picture Banana reads (PNG or JPEG; the default instead)\n", p.c_str());
			return "";
		}
		if (w > 4096 || h > 4096)
		{
			fprintf(stderr, "banana-ui: theme: %s is %dx%d, the largest is 4096 (the default instead)\n", p.c_str(), w, h);
			return "";
		}
		return p;
	}

	bool themeSetsAccent(const std::string& value)
	{
		if (value.compare(0, 5, "user:") != 0) return false;
		std::map<std::string, std::string> kv;
		if (!readTheme(pathFor(value.substr(5)), kv, nullptr)) return false;
		uint32_t c;
		return parseColor(kv["accent"], c) || parseColor(kv["accent.base"], c) || !kv["accent"].empty();
	}

	std::string applyTheme(const std::string& value, const std::string& accentName, std::string* why)
	{
		rnds::themeColors.clear();
		rnds::themeFiles = rnds::ThemeFiles();
		rnds::setAccent(accentName);
		if (value.compare(0, 5, "user:") != 0) return value == "light" ? "light" : value == "carts" ? "carts" : "dark";
		std::map<std::string, std::string> kv;
		std::string path = pathFor(value.substr(5));
		if (!readTheme(path, kv, why)) return "dark";
		std::string dir = path.substr(0, path.rfind('/'));
		std::string base = kv["base"] == "light" ? "light" : "dark";
		// the accent: one colour (its lighter and darker shades worked out) or each of the five named
		uint32_t c;
		if (parseColor(kv["accent"], c))
		{
			rnds::Accent a;
			a.base = c & 0xffffff;
			a.light = mix(a.base, 0xffffff, 0.6f); a.dark = mix(a.base, 0, 0.62f);
			a.band = mix(a.base, 0xffffff, 0.3f); a.low = mix(a.base, 0, 0.2f);
			rnds::setAccent(a);
		}
		else if (parseColor(kv["accent.base"], c))
		{
			rnds::Accent a = rnds::accent;
			a.base = c & 0xffffff;
			uint32_t v;
			a.light = parseColor(kv["accent.light"], v) ? v & 0xffffff : mix(a.base, 0xffffff, 0.6f);
			a.dark = parseColor(kv["accent.dark"], v) ? v & 0xffffff : mix(a.base, 0, 0.62f);
			a.band = parseColor(kv["accent.band"], v) ? v & 0xffffff : mix(a.base, 0xffffff, 0.3f);
			a.low = parseColor(kv["accent.low"], v) ? v & 0xffffff : mix(a.base, 0, 0.2f);
			rnds::setAccent(a);
		}
		else if (!kv["accent"].empty()) rnds::setAccent(kv["accent"]);	// one of Banana's (yellow, blue, ...)
		// the palette's colours by name
		for (auto& e : kv)
			if (e.first.compare(0, 7, "colors.") == 0 && parseColor(e.second, c)) rnds::themeColors[e.first.substr(7)] = c;
		// its files: fonts, the logo, the screens' pictures, system icons and menu sounds, and the dots' switch
		rnds::ThemeFiles& f = rnds::themeFiles;
		f.fontRegular = themeFile(dir, kv["fonts.regular"]);
		f.fontMedium = themeFile(dir, kv["fonts.medium"]);
		f.logo = themeImage(dir, kv["logo"]);
		f.wallTop = themeImage(dir, kv["background.top"]);
		f.wallBottom = themeImage(dir, kv["background.bottom"]);
		f.dither = kv["background.dither"] != "false";
		for (auto& e : kv)
		{
			if (e.first.compare(0, 6, "icons.") == 0) { std::string p = themeImage(dir, e.second); if (!p.empty()) f.icons[e.first.substr(6)] = p; }
			if (e.first.compare(0, 7, "sounds.") == 0)
			{
				if (e.second == "off") f.sounds[e.first.substr(7)] = "off";
				else { std::string p = themeFile(dir, e.second); if (!p.empty()) f.sounds[e.first.substr(7)] = p; }
			}
		}
		return base;
	}

	std::string themedIcon(const std::string& systemId, const std::string& def)
	{
		auto it = rnds::themeFiles.icons.find(systemId);
		return it == rnds::themeFiles.icons.end() ? def : it->second;
	}
}
