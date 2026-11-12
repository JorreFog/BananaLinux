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
#include <sys/stat.h>

namespace banana
{
	// (BANANA_THEMES: another folder for the card's, the host's tests)
	static const char* const CARD_DIR = getenv("BANANA_THEMES") && *getenv("BANANA_THEMES") ? getenv("BANANA_THEMES") : "/data/banana/themes";
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

	static bool readOne(const std::string& path, std::map<std::string, std::string>& out, std::string* why)
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

	static std::string pathFor(const std::string& stem);

	// the theme a theme extends ("extends": "<file name without .json>"): beside it, else the card's, else the image's
	static std::string basePath(const std::string& path, const std::string& name)
	{
		if (name.empty() || name.find('/') != std::string::npos || name.find("..") != std::string::npos) return "";
		std::string p = path.substr(0, path.rfind('/') + 1) + name + ".json";
		if (p != path && std::ifstream(p)) return p;
		p = pathFor(name);
		return p != path && std::ifstream(p) ? p : "";
	}

	// a theme's members by name ("colors.ink"), over those of the theme it extends (and that one's, four deep)
	static bool readTheme(const std::string& path, std::map<std::string, std::string>& out, std::string* why, int depth = 0)
	{
		std::map<std::string, std::string> own;
		if (!readOne(path, own, why)) return false;
		auto ex = own.find("extends");
		if (ex != own.end())
		{
			std::string base = depth < 4 ? basePath(path, ex->second) : "";
			if (base.empty()) { if (why) *why = path + ": the theme it extends, \"" + ex->second + "\", isn't there"; return false; }
			if (!readTheme(base, out, why, depth + 1)) return false;
			own.erase("extends");
		}
		for (auto& e : own) out[e.first] = e.second;
		return true;
	}

	// ---- the same for the whole document (a layout theme's screens, components and vars) ---------------------------
	// over's members over base's: objects member by member, anything else replaced, a null takes the member away
	static JPtr mergeJson(const JPtr& base, const JPtr& over)
	{
		if (!base || !over || !base->isObj() || !over->isObj()) return over;
		auto r = std::make_shared<JVal>(*base);
		for (auto& e : over->obj)
		{
			if (e.first == "extends") continue;
			auto it = std::find_if(r->obj.begin(), r->obj.end(), [&](const std::pair<std::string, JPtr>& m) { return m.first == e.first; });
			if (it == r->obj.end()) { if (e.second->kind != JVal::NUL) r->obj.push_back(e); }
			else if (e.second->kind == JVal::NUL) r->obj.erase(it);
			else it->second = mergeJson(it->second, e.second);
		}
		return r;
	}
	static JPtr readDoc(const std::string& path, std::string* why, std::vector<std::string>* dirs, int depth)
	{
		JPtr j = readJsonFile(path, why);
		if (!j || !j->isObj()) return j;
		std::string dir = path.substr(0, path.rfind('/'));
		if (dirs && std::find(dirs->begin(), dirs->end(), dir) == dirs->end()) dirs->push_back(dir);
		std::string ex = j->str("extends");
		if (ex.empty()) return j;
		std::string base = depth < 4 ? basePath(path, ex) : "";
		if (base.empty()) { if (why) *why = path + ": the theme it extends, \"" + ex + "\", isn't there"; return nullptr; }
		JPtr b = readDoc(base, why, dirs, depth + 1);
		return b ? mergeJson(b, j) : nullptr;
	}
	JPtr readThemeDoc(const std::string& path, std::string* why, std::vector<std::string>* dirs)
	{
		return readDoc(path, why, dirs, 0);
	}
	std::vector<std::string> themeDirs(const std::string& path)
	{
		std::vector<std::string> dirs;
		readDoc(path, nullptr, &dirs, 0);
		if (dirs.empty()) dirs.push_back(path.substr(0, path.rfind('/')));
		return dirs;
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

	std::string themePath(const std::string& value)
	{
		if (value.compare(0, 5, "user:") == 0) return pathFor(value.substr(5));
		if (value.compare(0, 5, "file:") == 0) return value.substr(5);
		return "";
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
	// (a theme that extends another: its own folder first, then that one's)
	typedef std::vector<std::string> Dirs;
	static std::string themeFile(const Dirs& dirs, const std::string& name)
	{
		if (name.empty()) return "";
		if (name[0] == '/') return std::ifstream(name) ? name : "";
		for (auto& dir : dirs) { std::string p = dir + "/" + name; if (std::ifstream(p)) return p; }
		return "";
	}

	// a picture the theme names (a wallpaper, the logo, an icon): only one stb_image reads (PNG or JPEG) and at most
	// 4096 px a side, checked from its header before anything is decoded (an 8192 x 8192 wallpaper decoded is 256 MB,
	// and banana-ui stays in memory under the games); otherwise the default is used, and the log says why
	static std::string themeImage(const Dirs& dir, const std::string& name)
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

	std::string themePreview(const std::string& value)
	{
		std::string path = themePath(value);
		if (path.empty()) return "";
		std::map<std::string, std::string> kv;
		if (!readTheme(path, kv, nullptr) || kv["preview"].empty()) return "";
		return themeImage(themeDirs(path), kv["preview"]);
	}

	bool themeSetsAccent(const std::string& value)
	{
		std::string path = themePath(value);
		if (path.empty()) return false;
		std::map<std::string, std::string> kv;
		if (!readTheme(path, kv, nullptr)) return false;
		uint32_t c;
		return parseColor(kv["accent"], c) || parseColor(kv["accent.base"], c) || !kv["accent"].empty();
	}

	static rnds::Accent accentOf(uint32_t c)
	{
		rnds::Accent a;
		a.base = c & 0xffffff;
		a.light = mix(a.base, 0xffffff, 0.6f); a.dark = mix(a.base, 0, 0.62f);
		a.band = mix(a.base, 0xffffff, 0.3f); a.low = mix(a.base, 0, 0.2f);
		return a;
	}

	std::vector<ThemeAccent> themeAccents(const std::string& value)
	{
		static const char* const V[] = { "yellow", "blue", "red", "green", "purple", "pink" };
		static const char* const N[] = { "Yellow", "Blue", "Red", "Green", "Purple", "Pink" };
		std::vector<ThemeAccent> r;
		std::string path = themePath(value);
		JPtr j = path.empty() ? nullptr : readThemeDoc(path);
		const JVal* acc = j ? j->get("accents") : nullptr;
		auto add = [&](const std::string& v, const JVal* o)
		{
			if (std::any_of(r.begin(), r.end(), [&](const ThemeAccent& t) { return t.value == v; })) return;
			ThemeAccent t;
			t.value = v;
			rnds::Accent a;
			uint32_t c;
			for (int k = 0; k < 6; k++) if (v == V[k]) { t.name = N[k]; rnds::accentNamed(v, a); t.color = a.base; }
			if (o && o->isObj())
			{
				t.name = o->str("name", t.name);
				if (parseColor(o->str("color"), c)) t.color = c & 0xffffff;
				else if (v == "theme") return;	// (its own needs a colour)
			}
			else if (v == "theme") return;
			if (t.name.empty()) t.name = "Theme";
			r.push_back(t);
		};
		if (acc && acc->isObj())
		{
			add("theme", acc->get("own"));
			for (auto& e : acc->obj) for (const char* v : V) if (e.first == v) add(v, e.second.get());
		}
		for (const char* v : V) add(v, nullptr);
		return r;
	}

	std::string themeOwnAccent(const std::string& value)
	{
		std::string path = themePath(value);
		if (path.empty()) return "";
		std::map<std::string, std::string> kv;
		uint32_t c;
		return readTheme(path, kv, nullptr) && parseColor(kv["accents.own.color"], c) ? "theme" : "";
	}

	std::string applyTheme(const std::string& value, const std::string& accentName, std::string* why)
	{
		rnds::themeColors.clear();
		rnds::themeFiles = rnds::ThemeFiles();
		rnds::setAccent(accentName);
		rnds::accentInk = 0;
		std::string path = themePath(value);
		if (path.empty()) return value == "light" ? "light" : value == "carts" ? "carts" : "dark";
		std::map<std::string, std::string> kv;
		if (!readTheme(path, kv, why)) return "dark";
		const Dirs dir = themeDirs(path);
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
		else
		{
			// the Accent colour setting's, as this theme has it ("accents": its names, colours and inks for the six,
			// and its own: the setting's "theme"; a theme without its own shows the yellow for that)
			std::string k = accentName == "theme" ? "own" : accentName;
			rnds::Accent six;
			if (k == "own" ? !parseColor(kv["accents.own.color"], c) : !rnds::accentNamed(k, six)) k = "yellow";
			if (parseColor(kv["accents." + k + ".color"], c)) rnds::setAccent(accentOf(c));
			if (parseColor(kv["accents." + k + ".ink"], c)) rnds::accentInk = 0xff000000u | (c & 0xffffff);
		}
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
		f.accentLogo = kv["accentLogo"] != "false";
		f.panels = kv["panels"] == "flat" || kv["panels"] == "material" ? kv["panels"] : "";
		// Settings' own look (its "settings" member)
		f.setTop = kv["settings.top"] == "open" ? "open" : "";
		f.setLeft = -1;
		if (!kv["settings.left"].empty()) { char* end = nullptr; double v = strtod(kv["settings.left"].c_str(), &end); if (end && !*end && v >= 0 && v <= 400) f.setLeft = (float)v; }
		f.setIcons = kv["settings.icons"] == "tile" || kv["settings.icons"] == "none" ? kv["settings.icons"] : "";
		f.setPick = kv["settings.pick"] == "outline" ? "outline" : "";
		f.setHints = kv["settings.hints"] == "pill" ? "pill" : "";
		f.setHeader = kv["settings.header"] == "rule" ? "rule" : "";
		f.setLayout = kv["settings.layout"] == "screen" ? "screen" : "";
		{
			auto nums = [&](const char* key, float* m)
			{
				static const char* const K[] = { "light", "at", "dark", "lip" };
				static const float MAX[] = { 1, 1, 1, 24 };
				for (int k = 0; k < 4; k++)
				{
					const std::string& t = kv[std::string("settings.") + key + "." + K[k]];
					char* end = nullptr;
					double v = t.empty() ? -1 : strtod(t.c_str(), &end);
					if (!t.empty() && end && !*end && v >= 0 && v <= MAX[k]) m[k] = (float)v;
				}
			};
			nums("plate", f.plateMix);
			nums("state", f.stateMix);
			f.setTexture = themeImage(dir, kv["settings.texture"]);
		}
		f.setGlyphs.clear();
		if (!kv["settings.glyphs"].empty() && kv["settings.glyphs"].find("..") == std::string::npos)
			for (auto& d : dir)
			{
				struct stat st;
				if (stat((d + "/" + kv["settings.glyphs"]).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { f.setGlyphs = d + "/" + kv["settings.glyphs"]; break; }
			}
		{
			// a font of the theme's (a .ttf beside it) or a built-in name (Inter's: the menu's fonts folder, found by Settings)
			const std::string& bf = kv["settings.bodyFont"];
			f.bodyFont = bf.compare(0, 5, "inter") == 0 && bf.find('.') == std::string::npos ? "builtin:" + bf : themeFile(dir, bf);
		}
		f.transitions = kv["transitions"] == "fade" ? "fade" : "";
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
