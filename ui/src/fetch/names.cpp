#include "names.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace banana
{
	namespace fetch
	{
		std::string cleanPath(const std::string& p)
		{
			std::vector<std::string> parts;
			std::string cur;
			for (size_t i = 0; i <= p.size(); i++)
			{
				char c = i < p.size() ? p[i] : '/';
				if (c == '\\') c = '/';
				if (c != '/') { cur += c; continue; }
				if (cur == "..") { if (!parts.empty()) parts.pop_back(); }
				else if (!cur.empty() && cur != ".") parts.push_back(cur);
				cur.clear();
			}
			std::string r;
			for (auto& x : parts) r += "/" + x;
			return r.empty() ? "/" : r;
		}

		std::string joinPath(const std::string& dir, const std::string& name) { return cleanPath(dir + "/" + name); }
		std::string parentPath(const std::string& p) { return cleanPath(cleanPath(p) + "/.."); }

		std::string baseName(const std::string& p)
		{
			std::string c = cleanPath(p);
			return c == "/" ? "" : c.substr(c.rfind('/') + 1);
		}

		std::string lower(std::string s)
		{
			for (auto& c : s) c = (char)tolower((unsigned char)c);
			return s;
		}

		std::string trim(const std::string& s)
		{
			size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
			return a == std::string::npos ? "" : s.substr(a, b - a + 1);
		}

		// (suffix in lower case)
		static bool endsWith(const std::string& s, const char* suffix)
		{
			size_t n = strlen(suffix);
			if (s.size() < n) return false;
			for (size_t i = 0; i < n; i++)
				if (tolower((unsigned char)s[s.size() - n + i]) != suffix[i]) return false;
			return true;
		}

		static bool exists(const std::string& p)
		{
			struct stat st;
			return stat(p.c_str(), &st) == 0;
		}

		// "<stem>.7z.001": stem is "<name>.7z"
		static bool sevenZipVolume(const std::string& name, std::string& stem)
		{
			size_t n = name.size();
			if (n < 8 || name[n - 4] != '.') return false;
			for (size_t i = n - 3; i < n; i++) if (!isdigit((unsigned char)name[i])) return false;
			stem = name.substr(0, n - 4);
			return endsWith(stem, ".7z");
		}

		// "<stem>.part<N>.rar"
		static bool rarPart(const std::string& name, std::string& stem, std::string& num)
		{
			if (!endsWith(name, ".rar")) return false;
			size_t e = name.size() - 4, b = e;
			while (b > 0 && isdigit((unsigned char)name[b - 1])) b--;
			if (b == e || b < 5 || lower(name.substr(b - 5, 5)) != ".part") return false;
			stem = name.substr(0, b - 5);
			num = name.substr(b, e - b);
			return true;
		}

		// "<stem>.r00" or ".s00" (the old RAR naming's later volumes, after <stem>.rar)
		static bool rarOldVolume(const std::string& name, std::string& stem, char& letter)
		{
			size_t n = name.size();
			if (n < 5 || name[n - 4] != '.' || !isdigit((unsigned char)name[n - 2]) || !isdigit((unsigned char)name[n - 1])) return false;
			letter = name[n - 3];
			if (tolower((unsigned char)letter) != 'r' && tolower((unsigned char)letter) != 's') return false;
			stem = name.substr(0, n - 4);
			return true;
		}

		Kind kindOf(const std::string& name)
		{
			std::string stem, num;
			if (endsWith(name, ".zip")) return ZIP;
			if (endsWith(name, ".7z") || endsWith(name, ".7z.001")) return SEVENZ;
			if (endsWith(name, ".rar"))
			{
				if (rarPart(name, stem, num))
				{
					size_t nz = num.find_first_not_of('0');
					if (nz == std::string::npos || num.substr(nz) != "1") return NONE;	// a later volume
				}
				return RAR;
			}
			return NONE;
		}

		bool volumeOf(const std::string& path, std::string& first, std::string& key)
		{
			size_t slash = path.rfind('/');
			std::string dir = slash == std::string::npos ? "" : path.substr(0, slash + 1);
			std::string name = path.substr(dir.size()), stem, num;
			char letter;
			if (sevenZipVolume(name, stem))
			{
				first = dir + stem + ".001";
				key = lower(dir + stem);
				return true;
			}
			if (rarPart(name, stem, num))
			{
				char n[32];
				snprintf(n, sizeof n, "%0*d", (int)num.size(), 1);
				first = dir + stem + ".part" + n + name.substr(name.size() - 4);
				key = lower(dir + stem) + ".part.rar";
				return true;
			}
			if (rarOldVolume(name, stem, letter))
			{
				first = dir + stem + (isupper((unsigned char)letter) ? ".RAR" : ".rar");
				key = lower(dir + stem) + ".rar";
				return true;
			}
			if (endsWith(name, ".rar") && kindOf(name) == RAR)
			{
				first = path;
				key = lower(path.substr(0, path.size() - 4)) + ".rar";
				return true;
			}
			if (kindOf(name) == NONE) return false;
			first = path;
			key = lower(path);
			return true;
		}

		std::vector<std::string> volumes(const std::string& path)
		{
			size_t slash = path.rfind('/');
			std::string dir = slash == std::string::npos ? "" : path.substr(0, slash + 1);
			std::string name = path.substr(dir.size()), stem, num;
			std::vector<std::string> out;
			char n[16];
			if (sevenZipVolume(name, stem))
			{
				for (int i = 1; i < 1000; i++)
				{
					snprintf(n, sizeof n, ".%03d", i);
					if (!exists(dir + stem + n)) break;
					out.push_back(dir + stem + n);
				}
				return out;
			}
			if (rarPart(name, stem, num))
			{
				for (int i = 1; i < 1000; i++)
				{
					snprintf(n, sizeof n, "%0*d", (int)num.size(), i);
					std::string v = dir + stem + ".part" + n + name.substr(name.size() - 4);
					if (!exists(v)) break;
					out.push_back(v);
				}
				return out;
			}
			out.push_back(path);
			if (endsWith(name, ".rar"))
			{
				// the old naming: game.rar, game.r00 ... game.r99, game.s00 ... game.s99
				std::string base = path.substr(0, path.size() - 4);
				char r = path.compare(path.size() - 4, 4, ".RAR") == 0 ? 'R' : 'r';
				for (int i = 0; i < 200; i++)
				{
					snprintf(n, sizeof n, ".%c%02d", r + i / 100, i % 100);
					if (!exists(base + n)) break;
					out.push_back(base + n);
				}
			}
			return out;
		}

		std::string stripArchive(const std::string& name)
		{
			std::string stem, num;
			if (sevenZipVolume(name, stem)) return stem.substr(0, stem.size() - 3);
			if (endsWith(name, ".7z")) return name.substr(0, name.size() - 3);
			if (endsWith(name, ".zip")) return name.substr(0, name.size() - 4);
			if (rarPart(name, stem, num)) return stem;
			if (endsWith(name, ".rar")) return name.substr(0, name.size() - 4);
			return name;
		}

		// UTF-16 units of a UTF-8 text (a four-byte character is two), as exFAT and FAT32 count a name
		static size_t units16(const std::string& s, size_t from, size_t to)
		{
			size_t n = 0;
			for (size_t i = from; i < to; i++)
			{
				unsigned char c = (unsigned char)s[i];
				if ((c & 0xc0) != 0x80) n += c >= 0xf0 ? 2 : 1;
			}
			return n;
		}

		// Windows' device names (con, nul, com1, lpt¹...): before the first dot, whatever the case
		static bool deviceName(const std::string& name)
		{
			std::string b = lower(name.substr(0, name.find('.')));
			while (!b.empty() && b.back() == ' ') b.pop_back();
			if (b == "con" || b == "prn" || b == "aux" || b == "nul") return true;
			if (b.size() < 4 || (b.compare(0, 3, "com") != 0 && b.compare(0, 3, "lpt") != 0)) return false;
			std::string d = b.substr(3);
			return (d.size() == 1 && d[0] >= '0' && d[0] <= '9') || d == "\xc2\xb9" || d == "\xc2\xb2" || d == "\xc2\xb3";
		}

		std::string safeName(const std::string& name)
		{
			std::string out;
			for (unsigned char c : name)
				out += c < 0x20 || c == 0x7f || strchr("\"*/:<>?\\|", c) ? '_' : (char)c;
			while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
			if (out.empty() || out == "." || out == "..") return "_";
			// a hidden name (Files doesn't show it) and a device name (Windows won't open it) get a _ before them
			if (out[0] == '.' || deviceName(out)) out = "_" + out;
			// the cards' limit, 255 UTF-16 characters, less the ".part" a download goes into first: the name cut before its
			// extension (a short one), on a character's start
			if (units16(out, 0, out.size()) > NAME_UNITS)
			{
				size_t dot = out.rfind('.');
				std::string ext = dot != std::string::npos && dot > 0 && out.size() - dot <= 16 ? out.substr(dot) : "";
				std::string stem = out.substr(0, out.size() - ext.size());
				size_t room = NAME_UNITS - units16(ext, 0, ext.size()), end = 0, used = 0;
				for (size_t i = 0; i < stem.size();)
				{
					size_t k = i + 1;
					while (k < stem.size() && ((unsigned char)stem[k] & 0xc0) == 0x80) k++;
					size_t u = units16(stem, i, k);
					if (used + u > room) break;
					used += u;
					end = i = k;
				}
				stem.resize(end);
				while (!stem.empty() && (stem.back() == '.' || stem.back() == ' ')) stem.pop_back();
				out = (stem.empty() ? "_" : stem) + ext;
			}
			return out;
		}

		std::string bytes(long long n)
		{
			static const char* const UNIT[] = { "B", "KB", "MB", "GB", "TB" };
			double v = (double)(n < 0 ? 0 : n);
			int u = 0;
			while (v >= 1024 && u < 4) { v /= 1024; u++; }
			char b[32];
			if (u == 0) snprintf(b, sizeof b, "%lld B", n < 0 ? 0 : n);
			else snprintf(b, sizeof b, v < 10 ? "%.1f %s" : "%.0f %s", v, UNIT[u]);
			return b;
		}

		bool spaceAt(const std::string& path, Space& out)
		{
			std::string p = path.empty() ? "/" : path;
			while (!exists(p))
			{
				size_t slash = p.rfind('/');
				if (slash == std::string::npos || p == "/") return false;
				p = slash == 0 ? "/" : p.substr(0, slash);
			}
			struct statvfs vf;
			struct statfs sf;
			if (statvfs(p.c_str(), &vf) != 0) return false;
			out.free = (uint64_t)vf.f_bavail * vf.f_frsize;
			out.fat32 = statfs(p.c_str(), &sf) == 0 && sf.f_type == 0x4d44;	// MSDOS_SUPER_MAGIC (vfat)
			// the tests' card: a size to run out of (BANANA_FETCH_TEST only)
			const char* t = getenv("BANANA_FETCH_TEST");
			const char* f = getenv("BANANA_FETCH_TEST_FREE");
			if (t && !strcmp(t, "1") && f && *f) out.free = strtoull(f, nullptr, 10);
			return true;
		}

		bool sameFilesystem(const std::string& a, const std::string& b)
		{
			struct stat sa, sb;
			return stat(a.c_str(), &sa) == 0 && stat(b.c_str(), &sb) == 0 && sa.st_dev == sb.st_dev;
		}
	}
}
