#include "sysfolders.h"
#include "fetch/names.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace banana
{
	namespace sysfolders
	{
		// what a server's folder for a system may be called (the usual names of Banana's systems)
		static const struct { const char* id; const char* names; } ALIASES[] = {
			{ "nds", "nds,ds,nintendo ds" }, { "gba", "gba,game boy advance,gameboy advance" },
			{ "gbc", "gbc,game boy color,gameboy color" }, { "gb", "gb,game boy,gameboy" },
			{ "nes", "nes,fc,famicom,nintendo entertainment system" }, { "snes", "snes,sfc,super nintendo,super famicom" },
			{ "megadrive", "md,megadrive,mega drive,genesis,sega genesis" }, { "psx", "psx,ps,ps1,playstation" },
			{ "n64", "n64,nintendo 64" }, { "pico-8", "pico,pico8,pico-8" }, { "ports", "ports" },
		};

		static void addNames(std::vector<std::string>& to, const std::string& list)
		{
			std::stringstream ss(list);
			std::string n;
			while (std::getline(ss, n, ','))
			{
				n = fetch::lower(fetch::trim(n));
				if (!n.empty() && std::find(to.begin(), to.end(), n) == to.end()) to.push_back(n);
			}
		}

		std::vector<Folder> folders(const std::string& systemsConf)
		{
			std::vector<Folder> out;
			std::ifstream f(systemsConf);
			std::string ln, absolute;
			Folder* cur = nullptr;
			auto done = [&] { if (cur && !absolute.empty()) out.pop_back(); cur = nullptr; absolute.clear(); };
			while (std::getline(f, ln))
			{
				ln = fetch::trim(ln);
				if (ln.empty() || ln[0] == '#') continue;
				if (ln[0] == '[' && ln.back() == ']')
				{
					done();
					out.push_back(Folder());
					cur = &out.back();
					cur->system = fetch::lower(ln.substr(1, ln.size() - 2));
					cur->rel = "roms/" + cur->system;
					cur->label = cur->system;
					continue;
				}
				size_t eq = ln.find('=');
				if (!cur || eq == std::string::npos) continue;
				std::string k = fetch::trim(ln.substr(0, eq)), v = fetch::trim(ln.substr(eq + 1));
				if (k == "name") cur->label = v;
				else if (k == "folder") { if (!v.empty() && v[0] == '/') absolute = v; else cur->rel = "roms/" + v; }	// (an app's own: no games go there)
				else if (k == "aliases") addNames(cur->names, v);
				else if (k == "ext")
				{
					std::istringstream es(v);
					std::string e;
					while (es >> e) cur->exts.push_back(fetch::lower(e[0] == '.' ? e.substr(1) : e));
				}
			}
			done();
			for (auto& fo : out)
			{
				fo.names.insert(fo.names.begin(), fetch::lower(fo.rel.substr(5)));
				addNames(fo.names, fo.system + "," + fo.label);
				for (auto& a : ALIASES) if (fo.system == a.id) addNames(fo.names, a.names);
				fo.playsZip = std::find(fo.exts.begin(), fo.exts.end(), "zip") != fo.exts.end();
			}
			Folder bios;
			bios.label = "BIOS files";
			bios.rel = "bios";
			bios.names = { "bios", "system", "firmware" };
			out.push_back(bios);
			Folder roms;
			roms.label = "The roms folder";
			roms.rel = "roms";
			out.push_back(roms);
			return out;
		}

		int suggest(const std::vector<Folder>& f, const std::string& name, const std::string& remoteDir)
		{
			std::string n = fetch::lower(fetch::stripArchive(name));
			size_t dot = n.rfind('.');
			if (!n.empty() && dot != std::string::npos)
			{
				std::string ext = n.substr(dot + 1);
				int hit = -1, count = 0;
				for (int i = 0; i < (int)f.size(); i++)
					for (auto& e : f[i].exts)
						if (e == ext && ext != "zip")
						{
							if (f[i].system == ext) return i;	// (an extension two systems take: the one named after it)
							count++;
							hit = i;
						}
				if (count == 1) return hit;
			}
			// the server's folders, the deepest first
			std::vector<std::string> parts;
			std::stringstream ss(remoteDir);
			std::string p;
			while (std::getline(ss, p, '/')) if (!p.empty()) parts.push_back(fetch::lower(p));
			for (int k = (int)parts.size() - 1; k >= 0; k--)
				for (int i = 0; i < (int)f.size(); i++)
					if (std::find(f[i].names.begin(), f[i].names.end(), parts[k]) != f[i].names.end()) return i;
			return -1;
		}

		int byRel(const std::vector<Folder>& f, const std::string& rel)
		{
			for (size_t i = 0; i < f.size(); i++) if (fetch::lower(f[i].rel) == fetch::lower(rel)) return (int)i;
			return -1;
		}

		int systemOf(const std::vector<Folder>& f, const std::string& rel)
		{
			std::string r = fetch::lower(rel);
			for (size_t i = 0; i < f.size(); i++)
			{
				if (f[i].system.empty()) continue;
				std::string s = fetch::lower(f[i].rel);
				if (r == s || (r.size() > s.size() && r.compare(0, s.size(), s) == 0 && r[s.size()] == '/')) return (int)i;
			}
			return -1;
		}
	}
}
