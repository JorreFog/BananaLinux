// banana-ui on Android: the launch table (android_launch.h)
#include "android_launch.h"
#include <fstream>
#include <sstream>

namespace banana
{
	namespace launch
	{
		static std::string trim(const std::string& s)
		{
			size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
			return a == std::string::npos ? "" : s.substr(a, b - a + 1);
		}

		static std::vector<std::string> words(const std::string& s)
		{
			std::vector<std::string> r;
			std::istringstream in(s);
			for (std::string w; in >> w;) r.push_back(w);
			return r;
		}

		bool Table::load(const std::string& path)
		{
			std::ifstream f(path);
			if (!f) return false;
			std::map<std::string, Entry> got;
			Entry* cur = nullptr;
			int n = 0;
			auto warn = [&](const std::string& what) { warnings.push_back(path + ":" + std::to_string(n) + ": " + what); };
			for (std::string line; std::getline(f, line);)
			{
				n++;
				std::string l = trim(line);
				if (l.empty() || l[0] == '#' || l[0] == ';') continue;
				if (l[0] == '[')
				{
					size_t e = l.find(']');
					std::string id = e == std::string::npos ? "" : trim(l.substr(1, e - 1));
					if (id.empty()) { warn("a section with no name"); cur = nullptr; continue; }
					cur = &got[id];
					*cur = Entry();
					cur->system = id;
					cur->file = path;
					continue;
				}
				size_t eq = l.find('=');
				if (eq == std::string::npos) { warn("no '=' in \"" + l + "\""); continue; }
				if (!cur) { warn("a key before any [system]"); continue; }
				std::string k = trim(l.substr(0, eq)), v = trim(l.substr(eq + 1));
				if (k == "package") cur->package = v;
				else if (k == "activity") cur->activity = v;
				else if (k == "action") cur->action = v.empty() ? "android.intent.action.VIEW" : v;
				else if (k == "data")
				{
					// none, file, content, or a URI template ("scheme:..."): anything else is a mistake
					if (v == "none" || v == "file" || v == "content" || v.empty()) cur->data = v.empty() ? "none" : v;
					else if (v.find(':') != std::string::npos && v.find(':') > 0) cur->data = v;
					else warn("data is none, file, content or a URI, not \"" + v + "\"");
				}
				else if (k == "mime") cur->mime = v;
				else if (k == "core") cur->core = v;
				else if (k == "flags") cur->flags = words(v);
				else if (k.compare(0, 6, "extra.") == 0 && k.size() > 6) cur->extras.emplace_back(k.substr(6), v);
				else warn("unknown key \"" + k + "\"");
			}
			for (auto& kv : got)
			{
				if (kv.second.package.empty()) { warnings.push_back(path + ": [" + kv.first + "] has no package: left out"); continue; }
				entries[kv.first] = kv.second;
			}
			return true;
		}

		int Table::loadAll(const std::string& apkCopy, const std::string& cardCopy)
		{
			int n = 0;
			for (const std::string& p : { apkCopy, std::string("/vendor/etc/banana/launch.conf"), std::string("/system_ext/etc/banana/launch.conf"), cardCopy })
				if (!p.empty() && load(p)) n++;
			return n;
		}

		const Entry* Table::find(const std::string& system, const std::string& variant) const
		{
			auto it = variant.empty() ? entries.end() : entries.find(system + "." + variant);
			if (it == entries.end()) it = entries.find(system);
			return it == entries.end() || it->second.package == "none" ? nullptr : &it->second;
		}

		bool Table::hidden(const std::string& system) const
		{
			auto it = entries.find(system);
			return it != entries.end() && it->second.package == "none";
		}

		std::string pageOf(const Entry& e)
		{
			return e.package.compare(0, 7, "banana:") == 0 ? e.package.substr(7) : "";
		}

		std::string coreOf(const std::string& run, const std::string& chosen, const std::string& table)
		{
			if (!chosen.empty()) return chosen;
			if (!table.empty()) return table;
			// the word after a RetroArch launcher's name: run-retroarch <core> %ROM%
			std::vector<std::string> w = words(run);
			for (size_t i = 0; i + 1 < w.size(); i++)
			{
				const std::string& t = w[i];
				std::string base = t.substr(t.rfind('/') + 1);
				if ((base == "run-retroarch" || base == "run-bananafront" || base == "run-ps1") && w[i + 1].find('%') == std::string::npos &&
					w[i + 1][0] != '-')
					return w[i + 1];
			}
			return "";
		}

		// the ROM file's first non-empty line (a Vita game's title id, a ScummVM game's id), trimmed
		static std::string romText(const std::string& rom)
		{
			std::ifstream f(rom);
			for (std::string l; std::getline(f, l);)
				if (!trim(l).empty()) return trim(l);
			return "";
		}

		// "k = v" in the ROM file (a PC game's .pc: steam = 1145360)
		static std::string romKey(const std::string& rom, const std::string& key)
		{
			std::ifstream f(rom);
			for (std::string l; std::getline(f, l);)
			{
				size_t eq = l.find('=');
				if (eq != std::string::npos && trim(l.substr(0, eq)) == key) return trim(l.substr(eq + 1));
			}
			return "";
		}

		std::string expand(const std::string& v, const std::string& rom, const std::string& core)
		{
			std::string r;
			for (size_t i = 0; i < v.size();)
			{
				if (v.compare(i, 5, "%ROM%") == 0) { r += rom; i += 5; }
				else if (v.compare(i, 6, "%CORE%") == 0) { r += core; i += 6; }
				else if (v.compare(i, 9, "%ROMTEXT%") == 0) { r += romText(rom); i += 9; }
				else if (v.compare(i, 8, "%ROMKEY:") == 0 && v.find('%', i + 8) != std::string::npos)
				{
					size_t e = v.find('%', i + 8);
					r += romKey(rom, v.substr(i + 8, e - i - 8));
					i = e + 1;
				}
				else r += v[i++];
			}
			return r;
		}

		bool filterSystems(const Table& t, const std::string& from, const std::string& to)
		{
			std::ifstream in(from);
			if (!in) return false;
			std::ostringstream out;
			bool skip = false;
			for (std::string l; std::getline(in, l);)
			{
				std::string tl = trim(l);
				if (!tl.empty() && tl[0] == '[')
				{
					size_t e = tl.find(']');
					skip = e != std::string::npos && t.hidden(trim(tl.substr(1, e - 1)));
					if (skip) out << "# [" << trim(tl.substr(1, e - 1)) << "]: not on Android (launch.conf: package = none)\n";
				}
				if (!skip) out << l << "\n";
			}
			std::ofstream o(to);
			o << out.str();
			return (bool)o;
		}

		std::vector<std::string> intentSpec(const Entry& e, const std::string& rom, const std::string& core)
		{
			std::vector<std::string> s;
			s.push_back("system=" + e.system);
			s.push_back("package=" + e.package);
			if (!e.activity.empty()) s.push_back("activity=" + e.activity);
			s.push_back("action=" + e.action);
			s.push_back("data=" + (e.data == "none" || e.data == "file" || e.data == "content" ? e.data : expand(e.data, rom, core)));
			if (!e.mime.empty()) s.push_back("mime=" + e.mime);
			std::string fl;
			for (auto& f : e.flags) fl += (fl.empty() ? "" : " ") + f;
			if (!fl.empty()) s.push_back("flags=" + fl);
			s.push_back("rom=" + rom);
			s.push_back("core=" + core);
			for (auto& x : e.extras) s.push_back("extra." + x.first + "=" + expand(x.second, rom, core));
			return s;
		}
	}
}
