#include "settings_defaults.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <unistd.h>
#include <vector>

namespace banana
{
	namespace
	{
		enum Type { BOOL, ENUM, INTEGER, TEXT, TIMEZONE, CORE };

		struct KeyDef
		{
			const char* key;
			const char* def;
			Type type;
			const char* const* allowed;	// ENUM
			int min, max;				// INTEGER
			bool emulator = false;		// EMULATOR scope (also true for Type CORE below)
		};

		// each list ends in nullptr: valid() walks it to the end
		const char* const ON_OFF[] = { "on", "off", nullptr };
		const char* const ACCENTS[] = { "yellow", "blue", "red", "green", "purple", "pink", nullptr };
		const char* const SHADERS[] = { "off", "lcd", "crt", "sharp", nullptr };
		const char* const NDS[] = { "superdrastic", "bananads", nullptr };
		const char* const PANEL[] = { "panel", "off", nullptr };
		const char* const BOXART[] = { "libretro", "both", "twilight", nullptr };
		const char* const LATENCY[] = { "32", "64", "128", nullptr };
		const char* const LOGIN[] = { "password", "guest", nullptr };

		// (language is left out on purpose: a reset keeps the menu's language, and the codes are lang/'s files)
		// The values /etc/banana/settings.conf documents, and the ones a card is completed with when that file is
		// missing (the host test, or a card whose root is older than the menu). Keep in step with the template file.
		const KeyDef KEYS[] = {
			{ "accent", "yellow", ENUM, ACCENTS, 0, 0 },
			{ "theme", "dark", TEXT, nullptr, 0, 0 },
			{ "motion", "on", ENUM, ON_OFF, 0, 0 },
			{ "sounds", "on", ENUM, ON_OFF, 0, 0 },
			{ "battery", "icon", TEXT, nullptr, 0, 0 },
			{ "clock", "24h", TEXT, nullptr, 0, 0 },
			{ "splash", "on", ENUM, ON_OFF, 0, 0 },
			{ "bottom_idle_seconds", "10", INTEGER, nullptr, 0, 3600 },
			{ "bottom_dim_percent", "0", INTEGER, nullptr, 0, 100 },
			{ "wifi", "on", ENUM, ON_OFF, 0, 0 },
			{ "bluetooth", "off", ENUM, ON_OFF, 0, 0 },
			{ "quick_resume", "off", ENUM, ON_OFF, 0, 0, true },
			{ "sd2_roms", "on", ENUM, ON_OFF, 0, 0 },
			{ "ra_shader", "off", ENUM, SHADERS, 0, 0, true },
			{ "ra_audio_latency", "64", ENUM, LATENCY, 0, 0, true },
			{ "ra_vsync", "on", ENUM, ON_OFF, 0, 0, true },
			{ "ra_hotkeys", "on", ENUM, ON_OFF, 0, 0, true },
			{ "nds_emulator", "superdrastic", ENUM, NDS, 0, 0, true },
			{ "share", "off", ENUM, ON_OFF, 0, 0 },
			{ "share_login", "password", ENUM, LOGIN, 0, 0 },
			{ "ssh", "off", ENUM, ON_OFF, 0, 0 },
			{ "timezone", "UTC0", TIMEZONE, nullptr, 0, 0 },
			{ "retroarch_panel", "panel", ENUM, PANEL, 0, 0 },
			{ "pokemon_companion", "off", ENUM, ON_OFF, 0, 0, true },
			{ "boxart_source", "libretro", ENUM, BOXART, 0, 0 },
			{ "boxart_twilight_url", "https://boxart.kirovair.com", TEXT, nullptr, 0, 0 },
			{ "boxart_twilight_key", "tb2_9f4c1d7a3e8b5062", TEXT, nullptr, 0, 0 },
			{ "core.gba", "gpsp", CORE, nullptr, 0, 0 },
		};

		const KeyDef* find(const std::string& key)
		{
			for (const auto& k : KEYS) if (key == k.key) return &k;
			return nullptr;
		}

		std::string trim(const std::string& s)
		{
			size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
			return a == std::string::npos ? "" : s.substr(a, b - a + 1);
		}

		std::string unquote(const std::string& s)
		{
			std::string r = trim(s);
			if (r.size() >= 2 && (r[0] == '"' || r[0] == '\'') && r.back() == r[0]) r = r.substr(1, r.size() - 2);
			return r;
		}

		// key = value, one per line; "" when the line is a comment or has no '='.
		bool split(const std::string& line, std::string& key, std::string& value)
		{
			std::string ln = line;
			if (!ln.empty() && ln.back() == '\r') ln.pop_back();
			size_t eq = ln.find('=');
			if (ln.empty() || ln[0] == '#' || eq == std::string::npos) return false;
			key = trim(ln.substr(0, eq));
			value = unquote(ln.substr(eq + 1));
			return !key.empty();
		}

		bool only(const std::string& v, const char* chars)
		{
			return !v.empty() && v.find_first_not_of(chars) == std::string::npos;
		}

		bool valid(const KeyDef& k, const std::string& value)
		{
			if (value.empty() || value.find_first_of("\r\n") != std::string::npos) return false;
			switch (k.type)
			{
			case BOOL:
			case ENUM:
				for (const char* const* a = k.allowed; a && *a; a++) if (value == *a) return true;
				return false;
			case INTEGER:
			{
				if (value.find_first_not_of("0123456789") != std::string::npos) return false;
				long n = strtol(value.c_str(), nullptr, 10);
				return n >= k.min && n <= k.max;
			}
			case TIMEZONE:
				// a POSIX TZ rule: GMT0BST,M3.5.0/1,M10.5.0 and the like
				return only(value, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-/:,.<>");
			case CORE:
				return only(value, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_+-.");
			case TEXT:
			default:
				for (unsigned char c : value) if (c < 0x20 || c == 0x7f) return false;
				return true;
			}
		}

		bool validKey(const std::string& key, const std::string& value)
		{
			if (const KeyDef* k = find(key)) return valid(*k, value);
			// core.<system>: a system added to systems.conf gets the same core rule without a schema entry of its own
			if (key.rfind("core.", 0) == 0)
			{
				KeyDef k = { "", "", CORE, nullptr, 0, 0 };
				return valid(k, value);
			}
			return true;
		}

		void readConf(const std::string& path, std::map<std::string, std::string>& out)
		{
			std::ifstream f(path);
			std::string ln, key, value;
			while (std::getline(f, ln)) if (split(ln, key, value)) out[key] = value;
		}

		std::string readAll(const std::string& path)
		{
			std::ifstream f(path, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		}

		std::string defaultFor(const std::string& key)
		{
			if (const KeyDef* k = find(key)) return k->def;
			return "";
		}

		// The template's keys in its own order (the compiled table when the image's file isn't there, as in the host test)
		std::vector<std::pair<std::string, std::string>> templateKeys()
		{
			std::vector<std::pair<std::string, std::string>> out;
			std::ifstream f("/etc/banana/settings.conf");
			std::string ln, key, value;
			while (std::getline(f, ln))
				if (split(ln, key, value)) out.emplace_back(key, value);
			if (!f && out.empty())
				for (const auto& k : KEYS) out.emplace_back(k.key, k.def);
			return out;
		}

		bool writeAtomic(const std::string& path, const std::string& text)
		{
			std::string tmp = path + ".new";
			FILE* w = fopen(tmp.c_str(), "w");
			if (!w) return false;
			bool ok = fwrite(text.data(), 1, text.size(), w) == text.size();
			ok &= fflush(w) == 0 && fsync(fileno(w)) == 0;
			ok &= fclose(w) == 0;
			if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
			return true;
		}
	}

	std::string settingDefault(const std::string& key)
	{
		std::map<std::string, std::string> t;
		readConf("/etc/banana/settings.conf", t);
		auto it = t.find(key);
		return it == t.end() ? defaultFor(key) : it->second;
	}

	bool settingValid(const std::string& key, const std::string& value)
	{
		return validKey(key, unquote(value));
	}

	bool settingIsEmulator(const std::string& key)
	{
		if (key.rfind("core.", 0) == 0) return true;
		const KeyDef* k = find(key);
		return k && (k->emulator || k->type == CORE);
	}

	std::map<std::string, std::string> readSettings(const std::string& dataDir)
	{
		// compiled defaults, then the image's template (it may be newer than the menu), then the card's own values
		std::map<std::string, std::string> r;
		for (const auto& k : KEYS) r[k.key] = k.def;
		readConf("/etc/banana/settings.conf", r);
		readConf(dataDir + "/settings.conf", r);
		return r;
	}

	bool normalizeSettings(const std::string& dataDir)
	{
		const std::string path = dataDir + "/settings.conf";
		const std::vector<std::pair<std::string, std::string>> defs = templateKeys();
		std::map<std::string, std::string> defmap;
		for (const auto& d : defs) defmap[d.first] = d.second;
		auto fallback = [&](const std::string& key) -> std::string
		{
			auto it = defmap.find(key);
			if (it != defmap.end() && validKey(key, it->second)) return it->second;
			std::string v = defaultFor(key);
			return validKey(key, v) ? v : "";
		};

		// a card with no settings.conf gets the image's, comments included; with no image file (the host test) the
		// schema's values written out plainly
		if (access(path.c_str(), F_OK) != 0)
		{
			std::string text = readAll("/etc/banana/settings.conf");
			if (text.empty())
				for (const auto& d : defs)
					if (validKey(d.first, d.second)) text += d.first + " = " + d.second + "\n";
			return !text.empty() && writeAtomic(path, text);
		}

		std::string original = readAll(path);
		std::ifstream in(path);
		// there but unreadable (a card error): left alone, as setConfValue does
		if (!in) return false;

		struct Line { std::string raw, key, value; bool known = false; };
		std::vector<Line> lines;
		std::map<std::string, int> last;	// the line that counts for a known key (the last, as getConfValue reads it)
		std::string ln;
		while (std::getline(in, ln))
		{
			Line l;
			l.raw = ln;
			if (!ln.empty() && ln.back() == '\r') l.raw.pop_back();
			if (split(ln, l.key, l.value))
			{
				l.known = find(l.key) != nullptr || l.key.rfind("core.", 0) == 0;
				if (l.known) last[l.key] = (int)lines.size();
			}
			lines.push_back(l);
		}

		std::map<std::string, bool> seen;
		std::string out;
		for (size_t i = 0; i < lines.size(); i++)
		{
			const Line& l = lines[i];
			if (!l.known) { out += l.raw + "\n"; continue; }	// comments, blanks and the user's own keys stay
			if (last[l.key] != (int)i) continue;					// an earlier duplicate: dropped
			std::string v = validKey(l.key, l.value) ? l.value : fallback(l.key);
			if (v.empty()) continue;								// no default to put in its place: dropped
			out += l.key + " = " + v + "\n";
			seen[l.key] = true;
		}
		for (const auto& d : defs)
		{
			if (seen[d.first]) continue;
			std::string v = validKey(d.first, d.second) ? d.second : fallback(d.first);
			if (!v.empty()) out += d.first + " = " + v + "\n";
		}

		if (out == original) return false;
		return writeAtomic(path, out);
	}

	bool resetSettings(const std::string& dataDir, ResetScope scope)
	{
		const std::string path = dataDir + "/settings.conf";
		const std::vector<std::pair<std::string, std::string>> defs = templateKeys();
		std::map<std::string, std::string> defmap;
		for (const auto& d : defs) defmap[d.first] = d.second;
		auto fallback = [&](const std::string& key) -> std::string
		{
			auto it = defmap.find(key);
			if (it != defmap.end() && validKey(key, it->second)) return it->second;
			std::string v = defaultFor(key);
			return validKey(key, v) ? v : "";
		};
		auto known = [&](const std::string& key) { return find(key) != nullptr || key.rfind("core.", 0) == 0; };
		auto inScope = [&](const std::string& key)
		{
			bool emu = settingIsEmulator(key);
			return scope == ResetScope::Emulator ? emu : !emu;
		};

		// a card with no settings.conf yet: make it whole from the template rather than invent an empty one
		if (access(path.c_str(), F_OK) != 0) return normalizeSettings(dataDir);

		std::string original = readAll(path);
		std::ifstream in(path);
		if (!in) return false;						// there but unreadable (a card error): left alone

		struct Line { std::string raw, key, value; bool known = false; };
		std::vector<Line> lines;
		std::map<std::string, int> last;			// the line that counts for a known key (the last)
		std::string ln;
		while (std::getline(in, ln))
		{
			Line l;
			l.raw = ln;
			if (!ln.empty() && ln.back() == '\r') l.raw.pop_back();
			if (split(ln, l.key, l.value))
			{
				l.known = known(l.key);
				if (l.known) last[l.key] = (int)lines.size();
			}
			lines.push_back(l);
		}

		std::map<std::string, bool> seen;
		std::string out;
		for (size_t i = 0; i < lines.size(); i++)
		{
			const Line& l = lines[i];
			if (!l.known) { out += l.raw + "\n"; continue; }	// comments, blanks, the user's own keys stay
			if (last[l.key] != (int)i) continue;				// an earlier duplicate: dropped
			if (!inScope(l.key)) { out += l.raw + "\n"; seen[l.key] = true; continue; }
			std::string v = fallback(l.key);
			if (v.empty()) continue;							// no default (an unknown core.<system>): dropped
			out += l.key + " = " + v + "\n";
			seen[l.key] = true;
		}
		for (const auto& d : defs)								// an in-scope key the card never had
		{
			if (seen[d.first] || !inScope(d.first)) continue;
			std::string v = fallback(d.first);
			if (!v.empty()) { out += d.first + " = " + v + "\n"; seen[d.first] = true; }
		}

		if (out == original) return false;
		return writeAtomic(path, out);
	}
}
