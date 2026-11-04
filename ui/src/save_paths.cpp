// where a game's save is on the card (save_paths.h)
#include "save_paths.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <set>
#include <sstream>

namespace bsync
{
	namespace
	{
	std::string libretroDir() { const char* d = getenv("BANANA_LIBRETRO"); return d ? d : "/usr/lib/libretro"; }
	// (the menu merges the card's systems.conf over the image's into /run at its start)
	std::string systemsConf()
	{
		if (const char* p = getenv("BANANA_SYSTEMS")) return p;
		return exists("/run/banana/systems.conf") ? "/run/banana/systems.conf" : "/etc/banana/systems.conf";
	}
	// one [system] section of systems.conf
	std::map<std::string, std::string> section(const std::string& system)
	{
		std::map<std::string, std::string> m;
		std::ifstream f(systemsConf());
		bool in = false;
		for (std::string ln; std::getline(f, ln);)
		{
			ln = trim(ln);
			if (ln.empty() || ln[0] == '#') continue;
			if (ln[0] == '[') { in = ln == "[" + system + "]"; continue; }
			size_t e = ln.find('=');
			if (in && e != std::string::npos) m[trim(ln.substr(0, e))] = trim(ln.substr(e + 1));
		}
		return m;
	}
	// a setting as run-retroarch reads it: the card's, else the image's template
	std::string setting(const std::string& key)
	{
		auto card = readConf(SETTINGS);
		auto i = card.find(key);
		if (i != card.end() && !i->second.empty()) return i->second;
		auto tmpl = readConf("/etc/banana/settings.conf");
		i = tmpl.find(key);
		return i == tmpl.end() ? "" : i->second;
	}
	bool stamp(const std::string& p, long long& size, long long& mtime)
	{
		struct stat st;
		if (stat(p.c_str(), &st) != 0) return false;
		size = (long long)st.st_size; mtime = (long long)st.st_mtime;
		return true;
	}

	// ---- the caches: path, size, mtime, value; one line each --------------------------------------------------------
	struct Cache
	{
		struct Entry { long long size, mtime; std::string value; };
		std::string file;
		std::map<std::string, Entry> m;
		bool dirty = false;
		void load(const std::string& path)
		{
			if (file == path) return;
			file = path; m.clear(); dirty = false;
			std::ifstream f(path);
			for (std::string ln; std::getline(f, ln);)
			{
				std::istringstream s(ln);
				std::string k, a, b, v;
				if (std::getline(s, k, '\t') && std::getline(s, a, '\t') && std::getline(s, b, '\t') && std::getline(s, v) && !v.empty())
					m[k] = { atoll(a.c_str()), atoll(b.c_str()), v };
			}
		}
		const std::string* get(const std::string& k, long long size, long long mtime)
		{
			auto i = m.find(k);
			return i != m.end() && i->second.size == size && i->second.mtime == mtime ? &i->second.value : nullptr;
		}
		void put(const std::string& k, long long size, long long mtime, const std::string& v) { m[k] = { size, mtime, v }; dirty = true; }
		void save()
		{
			if (!dirty || file.empty()) return;
			std::string out;
			for (auto& kv : m) out += kv.first + "\t" + std::to_string(kv.second.size) + "\t" + std::to_string(kv.second.mtime) + "\t" + kv.second.value + "\n";
			mkdirs(dirOf(file));
			if (writeAtomic(file, out)) dirty = false;
		}
	};
	Cache romHashes;

	// the ROM's MD5, from the cache while its size and time are the same
	std::string romMd5(const std::string& rel)
	{
		std::string p = romPath(rel);
		long long size, mtime;
		if (!stamp(p, size, mtime)) return "";
		romHashes.load(ROOT + "/banana/rom-hashes.tsv");
		if (const std::string* v = romHashes.get(rel, size, mtime)) return *v;
		std::string m = hashFile(p, "md5");
		if (!m.empty()) romHashes.put(rel, size, mtime, m);
		return m;
	}

	void walkRoms(const std::string& dir, const std::string& rel, std::map<std::string, std::string>& out, int depth)
	{
		DIR* d = opendir(dir.c_str());
		if (!d) return;
		std::vector<std::string> names;
		while (dirent* e = readdir(d)) names.push_back(e->d_name);
		closedir(d);
		std::sort(names.begin(), names.end());
		for (auto& n : names)
		{
			if (n[0] == '.' || (depth == 0 && n == "media")) continue;
			std::string p = dir + "/" + n, r = rel + "/" + n;
			struct stat st;
			if (stat(p.c_str(), &st) != 0) continue;
			if (S_ISDIR(st.st_mode)) { if (depth < 3) walkRoms(p, r, out, depth + 1); }
			else if (S_ISREG(st.st_mode)) out.emplace(n, r);
		}
	}

	bool fileMatches(const std::string& rel, const std::vector<RomFile>& files, const Conf& c)
	{
		// installed from it: the hash banana-sync kept for it when it wrote the file (MFile::key)
		auto k = c.files.find(rel);
		if (k != c.files.end())
			for (auto& f : files)
				if ((!f.sha1.empty() && k->second == lower(f.sha1)) || (!f.md5.empty() && k->second == "md5:" + lower(f.md5))) return true;
		bool members = false;
		if (extOf(rel) == "zip")
		{
			auto local = zipMembers(romPath(rel));
			std::set<std::pair<long long, std::string>> have(local.begin(), local.end());
			for (auto& f : files)
			{
				if (f.members.empty()) continue;
				members = true;
				bool all = true;
				for (auto& m : f.members) if (!have.count({ m.first, lower(m.second) })) { all = false; break; }
				if (all) return true;
			}
		}
		if (members) return false;	// (the server's MD5 of a zip it could read is of the members' bytes, never the file's)
		std::string m = romMd5(rel);
		for (auto& f : files) if (!m.empty() && lower(f.md5) == m) return true;
		return false;
	}

	uint32_t u32(const std::string& b, size_t at) { return (uint32_t)(unsigned char)b[at] | (uint32_t)(unsigned char)b[at + 1] << 8 | (uint32_t)(unsigned char)b[at + 2] << 16 | (uint32_t)(unsigned char)b[at + 3] << 24; }
	unsigned u16(const std::string& b, size_t at) { return (unsigned)(unsigned char)b[at] | (unsigned)(unsigned char)b[at + 1] << 8; }
	}

	std::string romFolder(const std::string& system)
	{
		auto s = section(system);
		std::string f = s.count("folder") && !s["folder"].empty() ? s["folder"] : system;
		return f[0] == '/' || !safeRel(f) ? "" : f;	// (an absolute folder is an app's)
	}

	std::map<std::string, std::string> romIndex(const std::string& folder)
	{
		std::map<std::string, std::string> out;
		walkRoms(ROMS + "/" + folder, folder, out, 0);
		// (and the second SD card's, while it is in: a game on either card is on the handheld, its save on this one)
		std::string sd2 = sd2Roms();
		if (!sd2.empty()) walkRoms(sd2 + "/" + folder, folder, out, 0);
		return out;
	}

	std::string coreFor(const std::string& system, std::string& err)
	{
		std::string want = setting("core." + system);
		if (!want.empty() && safeRel(want) && want.find('/') == std::string::npos)
			for (const std::string& d : { libretroDir(), ROOT + "/retroarch/cores" })
				if (exists(d + "/" + want + "_libretro.so")) return d + "/" + want + "_libretro.so";
		// systems.conf's: run-retroarch <core> %ROM%, or run-ps1 (PCSX-ReARMed through run-retroarch)
		std::string run = section(system)["run"], def;
		size_t at = run.find("/run-retroarch ");
		if (at != std::string::npos)
		{
			std::istringstream s(run.substr(at + 15));
			s >> def;
		}
		else if (run.find("/run-ps1") != std::string::npos) def = "pcsx_rearmed";
		if (def.empty() || !safeRel(def) || def.find('/') != std::string::npos) { err = system + ": its emulator isn't a RetroArch core"; return ""; }
		std::string p = libretroDir() + "/" + def + "_libretro.so";
		if (exists(p)) return p;
		err = system + ": " + (want.empty() || want == def ? "core " + def + " isn't installed" : "neither core " + want + " (Settings) nor " + def + " is installed");
		return "";
	}

	SysSaves systemSaves(const std::string& system)
	{
		SysSaves s;
		// (the DS's save sent is always BananaDS's raw .sav, DraStic's brought into it by nds-saves with its footer cut
		// off, so it goes as melonDS's whichever of the two plays: a reader takes the label for the file's form)
		if (system == "nds") { s.folder = "nds"; s.ext = "sav"; s.label = "melonds"; return s; }
		// a core's file name without _libretro.so
		auto label = [](const std::string& core) { std::string b = stemOf(baseOf(core)); return lower(b.substr(0, b.find("_libretro"))); };
		// (run-retroarch points every GBA core at saves/gba itself, whatever RetroArch's settings say)
		if (system == "gba")
		{
			std::string e, core = coreFor(system, e);
			s.folder = "gba"; s.ext = "srm"; s.label = core.empty() ? "gba" : label(core);
			return s;
		}
		std::string cfg = ROOT + "/retroarch/retroarch.cfg";
		if (exists(cfg))
		{
			// RetroArch writes its settings back on exit, so a change made in its menu (or a game's override that was
			// never put back) is what the next game gets. Sorting by core isn't asked: run-retroarch turns it off at every
			// start, BananaFront never sorts, and saves-flat moves what a core's folder still holds
			auto m = readConf(cfg);
			std::string dir = m.count("savefile_directory") ? m["savefile_directory"] : "";
			while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
			if (dir != "/data/saves") { s.err = system + ": RetroArch's save folder is " + (dir.empty() ? "beside the games" : dir) + ", not /data/saves"; return s; }
		}
		std::string core = coreFor(system, s.err);
		if (core.empty()) return s;
		s.ext = "srm"; s.label = label(core);
		return s;
	}

	bool romMatches(const std::string& romsRel, const std::vector<RomFile>& files, const Conf& c)
	{
		if (extOf(romsRel) != "m3u") return fileMatches(romsRel, files, c);
		// a disc set: every disc the .m3u lists is one of the ROM's files
		std::ifstream f(romPath(romsRel));
		int discs = 0;
		std::string dir = dirOf(romsRel);
		for (std::string ln; std::getline(f, ln);)
		{
			ln = trim(ln);
			if (ln.empty() || ln[0] == '#') continue;
			std::string d = dir == "." ? ln : dir + "/" + ln;
			if (!safeRel(d) || !fileMatches(d, files, c)) return false;
			discs++;
		}
		return discs > 0;
	}

	std::vector<std::pair<long long, std::string>> zipMembers(const std::string& path)
	{
		std::vector<std::pair<long long, std::string>> out;
		std::ifstream f(path, std::ios::binary);
		if (!f) return out;
		f.seekg(0, std::ios::end);
		long long n = (long long)f.tellg();
		if (n < 22) return out;
		// the end of central directory record: 22 bytes and a comment of up to 64 KiB, at the very end
		long long tail = n < 65557 ? n : 65557;
		std::string b((size_t)tail, '\0');
		f.seekg(n - tail);
		f.read(&b[0], tail);
		if (!f) return out;
		size_t eocd = std::string::npos;
		for (size_t i = b.size() - 22 + 1; i-- > 0;)
			if (b.compare(i, 4, "PK\x05\x06", 4) == 0) { eocd = i; break; }
		if (eocd == std::string::npos) return out;
		unsigned entries = u16(b, eocd + 10);
		uint32_t cdSize = u32(b, eocd + 12), cdOff = u32(b, eocd + 16);
		if (cdOff == 0xFFFFFFFFu || entries == 0xFFFFu || (long long)cdOff + cdSize > n) return out;	// (zip64: not read)
		std::string cd(cdSize, '\0');
		f.seekg(cdOff);
		f.read(&cd[0], cdSize);
		if (!f) return out;
		size_t at = 0;
		unsigned seen = 0;
		while (at + 46 <= cd.size() && cd.compare(at, 4, "PK\x01\x02", 4) == 0)
		{
			uint32_t crc = u32(cd, at + 16), usize = u32(cd, at + 24);
			unsigned nl = u16(cd, at + 28), xl = u16(cd, at + 30), cl = u16(cd, at + 32);
			if (at + 46 + nl > cd.size()) return {};
			std::string name = cd.substr(at + 46, nl);
			if (usize == 0xFFFFFFFFu) return {};
			if (name.empty() || name.back() != '/')
			{
				char h[9];
				snprintf(h, sizeof h, "%08x", (unsigned)crc);
				out.push_back({ (long long)usize, h });
			}
			at += 46 + nl + xl + cl;
			seen++;
		}
		if (seen != entries) return {};
		return out;
	}

	void savePathCaches() { romHashes.save(); }
}
