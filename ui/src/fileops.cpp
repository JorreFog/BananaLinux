#include "fileops.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace banana
{
	namespace files
	{
		static const char* TMP = ".banana-copy-";		// a copy on its way (fileops.h)
		static const char* RENAMING = ".banana-rename-";	// a change of case on its way
		static const char* PIDFILE = "/run/banana/files.pid";
		static const size_t BLOCK = 1u << 20;			// a file is copied, and Stop looked for, a MiB at a time
		static const uint64_t WINDOW = 8u << 20;		// what a copy has written is sent to the card this often (copyFile)
		static const size_t NAME_BYTES = 255;			// the longest name, in bytes (and in UTF-16 characters on exFAT and FAT32)
		static const long EXFAT_MAGIC = 0x2011bab0;		// statfs's f_type of an exFAT card
		static const long VFAT_MAGIC = 0x4d44;			// and of a FAT32 one (MSDOS_SUPER_MAGIC, vfat)

		static std::string lower(std::string s)
		{
			for (auto& c : s) c = (char)tolower((unsigned char)c);
			return s;
		}

		// a name's sort key, case aside: ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic capitals lowered (Яблоко
		// beside яблоко, not before every small letter), the rest as it is; compared as bytes, in code point order
		static std::string fold(const std::string& s)
		{
			std::string o;
			o.reserve(s.size());
			for (size_t i = 0; i < s.size();)
			{
				unsigned char c = s[i];
				size_t n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
				if (i + n > s.size()) n = 1;
				uint32_t u = n == 1 ? c : n == 2 ? (c & 0x1f) : n == 3 ? (c & 0x0f) : (c & 0x07);
				for (size_t k = 1; k < n; k++) u = (u << 6) | ((unsigned char)s[i + k] & 0x3f);
				i += n;
				if (n == 1 && c >= 0x80) { o += (char)c; continue; }	// (not UTF-8: the byte itself)
				if ((u >= 'A' && u <= 'Z') || (u >= 0xc0 && u <= 0xde && u != 0xd7) || (u >= 0x391 && u <= 0x3ab && u != 0x3a2) ||
					(u >= 0x410 && u <= 0x42f)) u += 0x20;
				else if (u >= 0x400 && u <= 0x40f) u += 0x50;
				else if ((u >= 0x100 && u <= 0x137) || (u >= 0x14a && u <= 0x177)) u |= 1;
				else if ((u >= 0x139 && u <= 0x148) || (u >= 0x179 && u <= 0x17e)) u += u & 1;
				else if (u == 0x178) u = 0xff;
				if (u < 0x80) o += (char)u;
				else if (u < 0x800) { o += (char)(0xc0 | (u >> 6)); o += (char)(0x80 | (u & 0x3f)); }
				else if (u < 0x10000) { o += (char)(0xe0 | (u >> 12)); o += (char)(0x80 | ((u >> 6) & 0x3f)); o += (char)(0x80 | (u & 0x3f)); }
				else { o += (char)(0xf0 | (u >> 18)); o += (char)(0x80 | ((u >> 12) & 0x3f)); o += (char)(0x80 | ((u >> 6) & 0x3f)); o += (char)(0x80 | (u & 0x3f)); }
			}
			return o;
		}

		static std::string trim(const std::string& s)
		{
			size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
			return a == std::string::npos ? "" : s.substr(a, b - a + 1);
		}

		static std::string join(const std::string& dir, const std::string& name)
		{
			return !dir.empty() && dir.back() == '/' ? dir + name : dir + "/" + name;
		}

		static double monoSec()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		// a text's first bytes, n at most, cut between two characters (UTF-8)
		static std::string utf8Head(const std::string& s, size_t n)
		{
			if (s.size() <= n) return s;
			while (n > 0 && ((unsigned char)s[n] & 0xc0) == 0x80) n--;
			return s.substr(0, n);
		}

		// a hidden name of ours for an entry on its way (a copy, a change of case): the prefix and as much of the name as
		// fits in a name (one at a time in a folder, so the start of a long name is enough)
		static std::string hiddenName(const char* prefix, const std::string& name)
		{
			return prefix + utf8Head(name, NAME_BYTES - strlen(prefix));
		}

		// the folder's entries written to the card (on exFAT an fsync writes the card's whole table, on FAT the folder's),
		// so a name put in place stays put when the card is taken out or the power goes. dev: the card's; a folder on
		// another file system (the card taken out: its mount point's own folder in /run) is ENODEV, not a flush of the RAM
		static int syncDir(const std::string& dir, dev_t dev)
		{
			int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
			if (fd < 0) return errno;
			struct stat st;
			int e = fstat(fd, &st) != 0 ? errno : st.st_dev != dev ? ENODEV : fsync(fd) == 0 ? 0 : errno;
			close(fd);
			return e;
		}

		// a card's file system written out (syncfs) before the outcome says what is on it; false when it refused, or when
		// dir isn't on dev any more (the card taken out)
		static bool flushed(const std::string& dir, dev_t dev)
		{
			int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
			if (fd < 0) return false;
			struct stat st;
			bool ok = fstat(fd, &st) == 0 && st.st_dev == dev && syncfs(fd) == 0;
			close(fd);
			return ok;
		}

		// (a folder made, an entry renamed: on its card at once, as the second card may be taken out right after. The
		// folder is enough: the card's syncfs would hold the page up for all else on its way to the card, a big file
		// coming over SMB, say)
		static void flushCard(const std::string& dir)
		{
			struct stat st;
			if (stat(dir.c_str(), &st) == 0) syncDir(dir, st.st_dev);
		}

		// what went wrong, as the top panel says it
		static std::string errText(int e)
		{
			switch (e)
			{
			case ENOSPC: return "the card is full";
			case EROFS: return "the card can't be written to (a card error made it read-only)";
			case EIO: return "the card couldn't be read or written";
			case ENOENT: return "it isn't there any more";
			case EACCES: case EPERM: return "that isn't allowed";
			case ENAMETOOLONG: return "the name is too long";
			case EEXIST: return "the name is taken";
			case ENOTEMPTY: return "the folder isn't empty";
			case EINVAL: return "the card refused the name";
			case EFBIG: return "the file is too big for the card";
			case ENODEV: case ENXIO: case ENOMEDIUM: return "the card was taken out";
			default: return strerror(e);
			}
		}

		std::string baseName(const std::string& path)
		{
			size_t at = path.find_last_of('/');
			return at == std::string::npos ? path : path.substr(at + 1);
		}

		std::string parent(const std::string& path)
		{
			size_t at = path.find_last_of('/');
			return at == std::string::npos ? "" : at == 0 ? "/" : path.substr(0, at);
		}

		bool inside(const std::string& path, const std::string& dir)
		{
			return path == dir || (path.size() > dir.size() && path.compare(0, dir.size(), dir) == 0 && path[dir.size()] == '/');
		}

		bool exists(const std::string& path)
		{
			struct stat st;
			return lstat(path.c_str(), &st) == 0;
		}

		// ---- the cards -------------------------------------------------------------------------------------------
		static bool mounted(const std::string& path)
		{
			std::ifstream f("/proc/mounts");
			std::string dev, at, rest;
			while (f >> dev >> at && std::getline(f, rest)) if (at == path) return true;
			return false;
		}

		std::vector<Card> cards(const std::string& romRoot)
		{
			std::vector<Card> out;
			std::string root = romRoot.size() > 5 && romRoot.compare(romRoot.size() - 5, 5, "/roms") == 0 ? romRoot.substr(0, romRoot.size() - 5) : romRoot;
			out.push_back({ "data", "This card", root });
			// the RG DS Plus's second card, while it is in (the sd2 script mounts it at /run/sd2)
			const char* t = getenv("BANANA_SD2");
			std::string sd2 = t && *t ? t : "/run/sd2";
			if ((t && *t) || mounted(sd2)) out.push_back({ "sd2", "Second SD card", sd2 });
			for (auto& c : out) { struct stat st; if (stat(c.root.c_str(), &st) == 0) c.dev = st.st_dev; }	// (while mounted)
			return out;
		}

		// ---- a folder ------------------------------------------------------------------------------------------
		bool list(const std::string& dir, std::vector<Entry>& out, std::string& why, bool tidy)
		{
			out.clear();
			DIR* d = opendir(dir.c_str());
			if (!d) { why = errText(errno); return false; }
			std::vector<std::string> renamed;	// changes of case cut off between their two renames
			while (dirent* e = readdir(d))
			{
				std::string n = e->d_name;
				if (n.empty() || n[0] == '.')
				{
					if (tidy && n.compare(0, strlen(TMP), TMP) == 0) unlink(join(dir, n).c_str());	// (a copy cut off)
					if (tidy && n.compare(0, strlen(RENAMING), RENAMING) == 0 && n.size() > strlen(RENAMING)) renamed.push_back(n);
					continue;
				}
				Entry en;
				en.name = n;
				// the directory's own word for what each entry is (exFAT and FAT give it): a stat only when it doesn't say
				if (e->d_type == DT_DIR) en.dir = true;
				else if (e->d_type == DT_UNKNOWN || e->d_type == DT_LNK)
				{
					struct stat st;
					if (stat(join(dir, n).c_str(), &st) != 0 || (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))) continue;
					en.dir = S_ISDIR(st.st_mode);
				}
				else if (e->d_type != DT_REG) continue;
				out.push_back(en);
			}
			closedir(d);
			// (the power or the card gone between a change of case's two renames: the entry, hidden till now, under its new
			// name again, as far as its hidden name kept it; under another when that is taken)
			for (auto& h : renamed)
			{
				struct stat st;
				if (lstat(join(dir, h).c_str(), &st) != 0) continue;
				std::string n = freeName(dir, h.substr(strlen(RENAMING)), S_ISDIR(st.st_mode));
				if (rename(join(dir, h).c_str(), join(dir, n).c_str()) != 0) continue;
				Entry en;
				en.name = n;
				en.dir = S_ISDIR(st.st_mode);
				out.push_back(en);
			}
			// (each name folded once, not at every comparison: a folder of thousands of names)
			std::vector<std::pair<std::string, size_t>> keys(out.size());
			for (size_t i = 0; i < out.size(); i++) keys[i] = { fold(out[i].name), i };
			std::sort(keys.begin(), keys.end(), [&out](const std::pair<std::string, size_t>& a, const std::pair<std::string, size_t>& b)
			{
				const Entry &ea = out[a.second], &eb = out[b.second];
				if (ea.dir != eb.dir) return ea.dir;
				return a.first != b.first ? a.first < b.first : ea.name < eb.name;
			});
			std::vector<Entry> sorted;
			sorted.reserve(out.size());
			for (auto& k : keys) sorted.push_back(std::move(out[k.second]));
			out.swap(sorted);
			return true;
		}

		void details(const std::string& dir, Entry& e)
		{
			struct stat st;
			e.known = true;
			if (stat(join(dir, e.name).c_str(), &st) != 0) return;
			e.size = (int64_t)st.st_size;
			e.mtime = (int64_t)st.st_mtime;
		}

		int count(const std::string& dir)
		{
			DIR* d = opendir(dir.c_str());
			if (!d) return -1;
			int n = 0;
			while (dirent* e = readdir(d))
			{
				if (e->d_name[0] == '.') continue;
				// (as list() takes them: folders and files, not a device or a pipe)
				if (e->d_type == DT_UNKNOWN || e->d_type == DT_LNK)
				{
					struct stat st;
					if (stat(join(dir, e->d_name).c_str(), &st) != 0 || (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))) continue;
				}
				else if (e->d_type != DT_DIR && e->d_type != DT_REG) continue;
				n++;
			}
			closedir(d);
			return n;
		}

		bool space(const std::string& path, Space& s)
		{
			struct statvfs v;
			if (statvfs(path.c_str(), &v) != 0) return false;
			s.free = (uint64_t)v.f_bavail * v.f_frsize;
			s.total = (uint64_t)v.f_blocks * v.f_frsize;
			s.cluster = v.f_frsize ? v.f_frsize : 4096;
			struct statfs f;
			s.fat32 = statfs(path.c_str(), &f) == 0 && f.f_type == VFAT_MAGIC;
			return true;
		}

		// ---- names -----------------------------------------------------------------------------------------------
		static long fsType(const std::string& dir)
		{
			struct statfs f;
			return !dir.empty() && statfs(dir.c_str(), &f) == 0 ? (long)f.f_type : 0;
		}

		// dir's card counts a name's 255 in UTF-16 characters, not bytes: exFAT, and FAT32's long names (vfat as the sd2
		// script mounts it, with utf8: without it, in its charset's bytes); Linux's own file systems count bytes
		static bool utf16Names(const std::string& dir)
		{
			long t = fsType(dir);
			return t == EXFAT_MAGIC || t == VFAT_MAGIC;
		}

		// the UTF-16 characters a name takes on exFAT and FAT32 (one outside the BMP takes two)
		static size_t utf16Units(const std::string& s)
		{
			size_t n = 0;
			for (unsigned char c : s)
				if ((c & 0xc0) != 0x80) n += c >= 0xf0 ? 2 : 1;
			return n;
		}

		// a text's first characters, as many as take n UTF-16 units at most (exFAT's and FAT32's measure of a name)
		static std::string utf16Head(const std::string& s, size_t n)
		{
			size_t i = 0, units = 0;
			while (i < s.size())
			{
				unsigned char c = s[i];
				size_t len = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1, u = c >= 0xf0 ? 2 : 1;
				if (units + u > n) break;
				units += u;
				i += len;
			}
			return s.substr(0, std::min(i, s.size()));
		}

		std::string nameProblem(const std::string& name, const std::string& dir)
		{
			if (trim(name).empty()) return "Type a name.";
			if (name == "." || name == "..") return "A name can't be . or ..";
			if (name[0] == '.') return "A name that starts with a dot would be hidden here.";
			for (unsigned char c : name)
			{
				if (c < 0x20 || c == 0x7f) return "A name can't have control characters in it.";
				if (strchr("\\/:*?\"<>|", c)) return std::string("A name can't have ") + (char)c + " in it: the cards and computers don't allow \\ / : * ? \" < > |.";
			}
			if (name.back() == ' ' || name.back() == '.') return "A name can't end with a space or a dot (the card drops it, and Windows can't open such a name).";
			// Windows's names for its devices: a file called so (CON, nul.txt, COM1.log...) can't be opened there
			std::string stem = lower(trim(name.substr(0, name.find('.'))));
			// (COM0 to COM9 and LPT0 to LPT9, and with a superscript ¹ ² ³ as Windows also counts them)
			std::string port = stem.size() > 3 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) ? stem.substr(3) : "";
			if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" || (port.size() == 1 && port[0] >= '0' && port[0] <= '9') ||
				port == "\xc2\xb9" || port == "\xc2\xb2" || port == "\xc2\xb3")
				return "Windows keeps the name " + name.substr(0, name.find('.')) + " for a device: a file called so can't be opened there.";
			// the card's limit: exFAT and FAT32 count UTF-16 characters, Linux's own file systems bytes
			bool utf16 = utf16Names(dir);
			if (utf16 ? utf16Units(name) > 255 : name.size() > NAME_BYTES)
				return utf16 ? "That name is too long: 255 characters at most." : "That name is too long: 255 bytes at most.";
			return "";
		}

		std::string freeName(const std::string& dir, const std::string& name, bool folder)
		{
			if (!exists(join(dir, name))) return name;
			size_t dot = folder ? std::string::npos : name.rfind('.');
			// (an extension of two parts stays whole: "cart (2).p8.png" is still a PICO-8 cart, "cart.p8 (2).png" was a
			// picture the library doesn't list)
			for (const char* x : { ".p8.png", ".tar.gz" })
				if (!folder && name.size() > strlen(x) && lower(name.substr(name.size() - strlen(x))) == x) dot = name.size() - strlen(x);
			std::string stem = dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
			std::string ext = dot == std::string::npos || dot == 0 ? "" : name.substr(dot);
			bool utf16 = utf16Names(dir);	// (exFAT's and FAT32's limit: 255 UTF-16 characters, not bytes)
			for (int n = 2;; n++)
			{
				// (a long name's stem shortened so that the number fits in a name)
				std::string suffix = " (" + std::to_string(n) + ")" + ext;
				size_t used = utf16 ? utf16Units(suffix) : suffix.size(), room = used < NAME_BYTES ? NAME_BYTES - used : 0;
				std::string c = (utf16 ? utf16Head(stem, room) : utf8Head(stem, room)) + suffix;
				if (!exists(join(dir, c))) return c;
			}
		}

		std::vector<std::string> systemFolders(const std::string& systemsConf)
		{
			std::vector<std::string> out;
			std::ifstream f(systemsConf);
			std::string ln, id, folder;
			auto done = [&] { if (!id.empty() && (folder.empty() || folder[0] != '/')) out.push_back(lower(folder.empty() ? id : folder)); };
			while (std::getline(f, ln))
			{
				ln = trim(ln);
				if (ln.empty() || ln[0] == '#') continue;
				if (ln[0] == '[' && ln.back() == ']') { done(); id = ln.substr(1, ln.size() - 2); folder.clear(); continue; }
				size_t eq = ln.find('=');
				if (eq != std::string::npos && trim(ln.substr(0, eq)) == "folder") folder = trim(ln.substr(eq + 1));
			}
			done();
			return out;
		}

		std::string keptWhy(const std::string& path, const std::vector<Card>& cards, const std::vector<std::string>& systems)
		{
			for (const Card& c : cards)
			{
				if (!inside(path, c.root)) continue;
				if (path == c.root) return "The card itself.";
				std::string rel = lower(path.substr(c.root.size() + (c.root.back() == '/' ? 0 : 1)));
				if (c.id == "data" && (rel == "banana" || rel == "bios" || rel == "saves" || rel == "states"))
					return "One of the card's own folders: Banana makes it again at every start and looks for its files there. What is in it can be changed.";
				if (rel == "roms") return "The games' folder: the library looks for each system's games in it. What is in it can be changed.";
				if (rel.compare(0, 5, "roms/") == 0 && rel.find('/', 5) == std::string::npos &&
					std::find(systems.begin(), systems.end(), rel.substr(5)) != systems.end())
					return c.id == "data" ? "A system's folder: the library looks for its games there, and Banana makes it again at every start. The games in it can be changed."
						: "A system's folder: the library looks for its games there. The games in it can be changed.";	// (data-layout makes this card's alone)
			}
			return "";
		}

		std::string bytes(uint64_t n)
		{
			if (n < 1024) return std::to_string(n) + " B";
			static const char* U[] = { "KB", "MB", "GB", "TB" };
			double v = (double)n;
			int u = -1;
			while (v >= 1024 && u < 3) { v /= 1024; u++; }
			char b[32];
			snprintf(b, sizeof b, v < 10 ? "%.1f %s" : "%.0f %s", v, U[u]);
			return b;
		}

		static bool same(const struct stat& a, const struct stat& b) { return a.st_dev == b.st_dev && a.st_ino == b.st_ino; }

		std::vector<std::string> clashes(const std::vector<std::string>& sources, const std::string& dest)
		{
			std::vector<std::string> out;
			for (const auto& s : sources)
			{
				struct stat a, b;
				std::string t = join(dest, baseName(s));
				if (lstat(t.c_str(), &b) != 0) continue;
				if (lstat(s.c_str(), &a) == 0 && same(a, b)) continue;
				out.push_back(baseName(s));
			}
			return out;
		}

		std::string pasteProblem(const std::vector<std::string>& sources, const std::string& dest)
		{
			for (const auto& s : sources)
			{
				struct stat st;
				if (lstat(s.c_str(), &st) != 0) return baseName(s) + " isn't there any more.";
				if (S_ISDIR(st.st_mode) && inside(dest, s)) return "A folder can't go into itself or a folder of its own (" + baseName(s) + ").";
			}
			return "";
		}

		bool makeFolder(const std::string& dir, const std::string& name, std::string& why)
		{
			why = nameProblem(name, dir);
			if (!why.empty()) return false;
			if (exists(join(dir, name))) { why = "There is already something called " + name + " here."; return false; }
			if (mkdir(join(dir, name).c_str(), 0755) != 0) { why = "The folder couldn't be made: " + errText(errno) + "."; return false; }
			flushCard(dir);
			return true;
		}

		bool renameEntry(const std::string& dir, const std::string& from, const std::string& to, std::string& why)
		{
			why = nameProblem(to, dir);
			if (!why.empty()) return false;
			if (from == to) return true;
			std::string a = join(dir, from), b = join(dir, to);
			struct stat sa, sb;
			if (lstat(a.c_str(), &sa) != 0) { why = from + " isn't there any more."; return false; }
			if (lstat(b.c_str(), &sb) == 0)
			{
				if (!same(sa, sb)) { why = "There is already something called " + to + " here."; return false; }
				// the same entry under the new name: a change of case, through a hidden name
				std::string t = join(dir, hiddenName(RENAMING, to));
				if (rename(a.c_str(), t.c_str()) != 0) { why = "It couldn't be renamed: " + errText(errno) + "."; return false; }
				if (rename(t.c_str(), b.c_str()) != 0) { why = "It couldn't be renamed: " + errText(errno) + "."; rename(t.c_str(), a.c_str()); return false; }
				flushCard(dir);
				return true;
			}
			if (rename(a.c_str(), b.c_str()) != 0) { why = "It couldn't be renamed: " + errText(errno) + "."; return false; }
			flushCard(dir);
			return true;
		}

		// ---- the job ---------------------------------------------------------------------------------------------
		struct Job::Run
		{
			Job& j;
			Op op;
			Clash clash;
			dev_t destDev = 0;
			std::vector<char> buf;
			bool cut = false;	// Stop took effect: something was left undone
			bool gone = false;	// the card pasted on went (taken out): nothing more is tried
			int counted = 0;	// what the counting has found so far (its page says it)
			// the tests' stand-in for a slow card (BANANA_FILES_PACE): ms after each block, so a job is still at work when
			// its page is shot and its Stop pressed (a test machine's disk takes 512 MB in a tenth of a second)
			int paceMs = 0;

			Run(Job& job, Op o, Clash c) : j(job), op(o), clash(c)
			{
				if (const char* t = getenv("BANANA_FILES_PACE")) paceMs = atoi(t);
			}

			// Stop pressed: looked at between two pieces of work, and what it cuts short remembered (a Stop that comes
			// after the last of them cuts nothing: the outcome isn't "Stopped"); the card pasted on gone ends it too
			bool halted()
			{
				if (j.halt) cut = true;
				return cut || gone;
			}

			void fail(const std::string& what, int e)
			{
				std::lock_guard<std::mutex> l(j.mu);
				j.p.failed++;
				if (j.p.error.empty()) j.p.error = what + ": " + errText(e) + ".";
			}
			void skip() { std::lock_guard<std::mutex> l(j.mu); j.p.skipped++; }
			void doneOne() { std::lock_guard<std::mutex> l(j.mu); j.p.done++; }
			void at(const std::string& name) { std::lock_guard<std::mutex> l(j.mu); j.p.current = name; }
			// a source asked for, over: done whole when nothing under it failed, was left out or was cut short
			void sourceOver(int failedBefore, int skippedBefore)
			{
				std::lock_guard<std::mutex> l(j.mu);
				if (j.p.failed == failedBefore && j.p.skipped == skippedBefore && !cut) j.p.sourcesDone++;
			}
			// one more found by the counting, said every 256 (a tree of thousands takes a while on a card)
			void tally()
			{
				if ((++counted & 255) != 0) return;
				std::lock_guard<std::mutex> l(j.mu);
				j.p.items = counted;
			}

			// the card pasted on is gone: taken out, the sd2 script unmounts it (umount -l) and its path is the mount
			// point's own folder in /run, a tmpfs, so what is made there next is in the RAM alone (and a move deletes its
			// source after it). The job ends here: everything else would go the same way
			void lost(const std::string& name)
			{
				gone = true;
				// (said over the file in hand's own failure, an EIO or an ENOENT: this is why it failed)
				std::lock_guard<std::mutex> l(j.mu);
				j.p.failed++;
				j.p.error = name + ": " + errText(ENODEV) + ".";
			}
			// what was just made (a copy's hidden file, a folder) is on the card pasted on
			bool onCard(dev_t dev, const std::string& name)
			{
				if (dev == destDev) return true;
				lost(name);
				return false;
			}

			// an entry's names in a folder, hidden ones too (they are the folder's), not the dots. false (errno) when the
			// folder can't be read whole (a card error, the card taken out): one read in part was taken for all of it, so a
			// copy of it said "Copied" with files missing, and a move left them behind without a word
			static bool names(const std::string& dir, std::vector<std::string>& out)
			{
				out.clear();
				DIR* d = opendir(dir.c_str());
				if (!d) return false;
				int e = 0;
				for (;;)
				{
					errno = 0;
					dirent* de = readdir(d);
					if (!de) { e = errno; break; }
					if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) out.push_back(de->d_name);
				}
				closedir(d);
				errno = e;
				return e == 0;
			}

			// what a source holds: its files and their bytes (each rounded up to whole clusters for the room), and the
			// largest file, for FAT32's 4 GB. Stop is looked at here too: a tree of thousands isn't walked to its end
			void measure(const std::string& path, uint64_t cluster, int& files, uint64_t& bytes, uint64_t& need, uint64_t& largest, std::string& largestName)
			{
				struct stat st;
				if (halted() || lstat(path.c_str(), &st) != 0) return;
				if (S_ISDIR(st.st_mode))
				{
					need += cluster;
					std::vector<std::string> kids;
					names(path, kids);	// (one that can't be read fails when it is copied, with its reason)
					for (auto& n : kids) measure(join(path, n), cluster, files, bytes, need, largest, largestName);
				}
				else if (S_ISREG(st.st_mode))
				{
					files++;
					tally();
					bytes += (uint64_t)st.st_size;
					need += ((uint64_t)st.st_size + cluster - 1) / cluster * cluster;
					if ((uint64_t)st.st_size > largest) { largest = (uint64_t)st.st_size; largestName = baseName(path); }
				}
			}

			int entries(const std::string& path)
			{
				struct stat st;
				if (halted() || lstat(path.c_str(), &st) != 0) return 0;
				tally();
				int n = 1;
				std::vector<std::string> kids;
				if (S_ISDIR(st.st_mode) && names(path, kids)) for (auto& c : kids) n += entries(join(path, c));
				return n;
			}

			// one file copied into place (fileops.h): false when it wasn't (Stop pressed, or a failure, counted)
			bool copyFile(const std::string& src, const std::string& target)
			{
				std::string tmp = join(parent(target), hiddenName(TMP, baseName(target)));
				int in = open(src.c_str(), O_RDONLY | O_CLOEXEC);
				if (in < 0) { fail(baseName(src), errno); return false; }
				struct stat st;
				if (fstat(in, &st) != 0) { int e = errno; close(in); fail(baseName(src), e); return false; }
				posix_fadvise(in, 0, 0, POSIX_FADV_SEQUENTIAL);
				int out = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
				if (out < 0) { int e = errno; close(in); fail(baseName(src), e); return false; }
				auto give_up = [&](int e)
				{
					close(in); close(out); unlink(tmp.c_str());
					if (e) fail(baseName(src), e);
					return false;
				};
				// (made on the card, not in the mount point's own folder after the card went: lost() says why not)
				struct stat os;
				if (fstat(out, &os) != 0) return give_up(errno);
				if (!onCard(os.st_dev, baseName(src))) return give_up(0);
				// every WINDOW bytes the window just written is sent to the card and the one before it waited for (and
				// both files' pages let go of): the bar shows what is on the card, not in the RAM, Stop waits a window at
				// most, and the fsync at the end has little left to write
				off_t off = 0, sent = 0, prevAt = 0, prevLen = 0;
				for (;;)
				{
					if (halted()) return give_up(0);
					ssize_t n = read(in, buf.data(), buf.size());
					if (n < 0 && errno == EINTR) continue;
					if (n < 0) return give_up(errno);
					if (n == 0) break;
					for (ssize_t w = 0; w < n;)
					{
						ssize_t k = write(out, buf.data() + w, (size_t)(n - w));
						if (k < 0 && errno == EINTR) continue;
						if (k < 0) return give_up(errno);
						w += k;
					}
					off += n;
					if ((uint64_t)(off - sent) >= WINDOW)
					{
						sync_file_range(out, sent, off - sent, SYNC_FILE_RANGE_WRITE);
						if (prevLen > 0)
						{
							if (sync_file_range(out, prevAt, prevLen, SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER) != 0 && errno == EIO)
								return give_up(EIO);
							posix_fadvise(out, prevAt, prevLen, POSIX_FADV_DONTNEED);
						}
						posix_fadvise(in, sent, off - sent, POSIX_FADV_DONTNEED);
						prevAt = sent; prevLen = off - sent; sent = off;
					}
					if (paceMs > 0) usleep(paceMs * 1000);
					std::lock_guard<std::mutex> l(j.mu);
					j.p.bytes += (uint64_t)n;
				}
				struct timespec ts[2] = { st.st_atim, st.st_mtim };
				futimens(out, ts);	// (its time, as on the computer it came from: before the fsync, which writes it too)
				if (fsync(out) != 0) return give_up(errno);
				close(in);
				if (close(out) != 0) { int e = errno; unlink(tmp.c_str()); fail(baseName(src), e); return false; }
				if (rename(tmp.c_str(), target.c_str()) != 0) { int e = errno; unlink(tmp.c_str()); fail(baseName(src), e); return false; }
				return true;
			}

			// a source into a folder under a name, the clash rule applied; top: one the paste was asked for
			void place(const std::string& src, const std::string& destDir, const std::string& name, bool top)
			{
				if (halted()) return;
				struct stat ss, ts;
				if (lstat(src.c_str(), &ss) != 0) { fail(name, errno); return; }
				if (!S_ISDIR(ss.st_mode) && !S_ISREG(ss.st_mode)) { skip(); return; }
				bool dir = S_ISDIR(ss.st_mode);
				std::string target = join(destDir, name);
				bool there = lstat(target.c_str(), &ts) == 0, merge = false;
				if (there && same(ss, ts))
				{
					// its own target: a copy beside it, a move is nothing to do
					if (op == Op::MOVE) { skip(); if (top) { std::lock_guard<std::mutex> l(j.mu); j.p.placed.push_back(name); } return; }
					target = join(destDir, freeName(destDir, name, dir));
				}
				else if (there)
				{
					if (clash == Clash::SKIP) { skip(); return; }
					bool dirs = dir && S_ISDIR(ts.st_mode), regs = !dir && S_ISREG(ts.st_mode);
					// (a file never takes a folder's place, nor a folder a file's)
					if (clash == Clash::KEEP_BOTH || (!dirs && !regs)) target = join(destDir, freeName(destDir, name, dir));
					else merge = dirs;
				}
				if (top) { std::lock_guard<std::mutex> l(j.mu); j.p.placed.push_back(baseName(target)); }
				// (a move on one card counts its sources, as the counting did: not what a merge renames inside them)
				if (op == Op::MOVE && ss.st_dev == destDev && !merge)
				{
					at(name);
					if (rename(src.c_str(), target.c_str()) != 0) fail(name, errno);
					else if (top) doneOne();
					return;
				}
				if (dir)
				{
					// (read first: nothing is made for a folder that can't be read, and why is said)
					std::vector<std::string> kids;
					if (!names(src, kids)) { fail(name, errno); return; }
					if (!merge)
					{
						struct stat ms;
						if (mkdir(target.c_str(), 0755) != 0) { fail(name, errno); return; }
						if (lstat(target.c_str(), &ms) != 0 || ms.st_dev != destDev) { rmdir(target.c_str()); lost(name); return; }
					}
					int failed0, skipped0;
					{ std::lock_guard<std::mutex> l(j.mu); failed0 = j.p.failed; skipped0 = j.p.skipped; }
					for (auto& c : kids)
					{
						if (halted()) return;
						if (c.compare(0, strlen(TMP), TMP) == 0)
						{
							// (a copy cut off in the source: ours, left out; a move deletes it, or it kept the folder)
							if (op == Op::MOVE) unlink(join(src, c).c_str());
							continue;
						}
						place(join(src, c), target, c, false);
					}
					if (op == Op::MOVE && rmdir(src.c_str()) != 0)
					{
						// one with something left in it that was skipped or failed stays (counted already); anything else
						// that keeps it (a file come in meanwhile, the card) is said
						int e = errno;
						bool said;
						{ std::lock_guard<std::mutex> l(j.mu); said = j.p.failed != failed0 || j.p.skipped != skipped0; }
						if (!said) fail(name, e);
					}
					if (op == Op::MOVE && ss.st_dev == destDev && top) doneOne();
					return;
				}
				at(name);
				if (!copyFile(src, target)) return;
				if (op == Op::MOVE)
				{
					// the copy's name on its card before the source goes: taken out right after (or the power cut), the
					// card kept the hidden .banana-copy- file alone, which the next listing deletes. The folder flushed is
					// the card's own (syncDir: not the mount point's folder once the card went)
					if (int e = syncDir(destDir, destDev)) { if (e == ENODEV) lost(name); else fail(name, e); return; }
					if (unlink(src.c_str()) != 0) { fail(name, errno); return; }
				}
				doneOne();
			}

			void remove(const std::string& path)
			{
				if (halted()) return;
				struct stat st;
				if (lstat(path.c_str(), &st) != 0) { fail(baseName(path), errno); return; }
				at(baseName(path));
				if (S_ISDIR(st.st_mode))
				{
					// (one that can't be read: why, not "the folder isn't empty" from the rmdir after)
					std::vector<std::string> kids;
					if (!names(path, kids)) { fail(baseName(path), errno); return; }
					for (auto& c : kids) { if (halted()) return; remove(join(path, c)); }
					if (rmdir(path.c_str()) != 0) { fail(baseName(path), errno); return; }
				}
				else if (unlink(path.c_str()) != 0) { fail(baseName(path), errno); return; }
				doneOne();
			}
		};

		void Job::run(Op op, std::vector<std::string> sources, std::string dest, Clash clash, std::function<void()> done, dev_t dev)
		{
			if (FILE* f = fopen(PIDFILE, "w")) { fprintf(f, "%d\n", (int)getpid()); fclose(f); }	// (bananad: no idle sleep)
			Run r(*this, op, clash);
			struct stat ds;
			int destErr = op != Op::DELETE && stat(dest.c_str(), &ds) != 0 ? errno : 0;
			if (op != Op::DELETE && !destErr) r.destDev = ds.st_dev;
			// the card a move or a delete takes entries from: flushed at the end, as the one pasted on is
			dev_t srcDev = 0;
			if (op != Op::COPY && !sources.empty() && stat(parent(sources[0]).c_str(), &ds) == 0) srcDev = ds.st_dev;
			// counting: what is to be done, and whether it fits
			int items = 0;
			uint64_t total = 0, need = 0, largest = 0;
			std::string largestName, refused;
			Space sp;
			bool spaceKnown = op != Op::DELETE && space(dest, sp);
			// (the card pasted on taken out since the pages saw it mounted: dest is its mount point's own folder now, or
			// not there at all; a folder gone from a card still in says so)
			std::string nothing = std::string(" Nothing was ") + (op == Op::COPY ? "copied." : "moved.");
			if (destErr) refused = baseName(dest) + ": " + errText(destErr) + "." + nothing;
			if (op != Op::DELETE && dev && r.destDev != dev && refused.empty()) refused = "The card was taken out." + nothing;
			for (const auto& s : sources)
			{
				if (!refused.empty() || r.halted()) break;
				struct stat st, ts;
				if (lstat(s.c_str(), &st) != 0) continue;	// (failed below, with its reason)
				// (a name already taken that Skip those leaves out takes no room)
				if (op != Op::DELETE && clash == Clash::SKIP && lstat(join(dest, baseName(s)).c_str(), &ts) == 0 && !same(st, ts)) continue;
				if (op == Op::DELETE) items += r.entries(s);
				else if (op == Op::MOVE && st.st_dev == r.destDev) items++;	// a rename
				else r.measure(s, spaceKnown ? sp.cluster : 4096, items, total, need, largest, largestName);
			}
			if (!refused.empty() || r.halted()) {}	// (stopped while counting: "Stopped", not a room the count never finished)
			else if (spaceKnown && sp.fat32 && largest > 0xffffffffull)
				refused = largestName + " is " + bytes(largest) + ": this card is FAT32, which holds files of up to 4 GB. Nothing was copied.";
			else if (spaceKnown && need > sp.free)
				refused = "Not enough room: " + bytes(need) + " needed, " + bytes(sp.free) + " free. Nothing was copied.";
			{
				std::lock_guard<std::mutex> l(mu);
				p.items = items;
				p.total = total;
				p.state = Progress::WORKING;
				if (!refused.empty()) { p.error = refused; p.failed = (int)sources.size(); }
			}
			if (refused.empty())
			{
				r.buf.resize(BLOCK);
				for (const auto& s : sources)
				{
					if (r.halted()) break;
					int failedBefore, skippedBefore;
					{ std::lock_guard<std::mutex> l(mu); failedBefore = p.failed; skippedBefore = p.skipped; }
					if (op == Op::DELETE) r.remove(s);
					else r.place(s, dest, baseName(s), true);
					r.sourceOver(failedBefore, skippedBefore);
				}
				// everything on the cards themselves before the outcome says it is there: the one pasted on (what the files'
				// own fsyncs left: the last names put in place), and the one a move or a delete took entries from (deleted
				// ones stay deleted when the second card is taken out right after). A write a card refused here is no one
				// file's, and a card gone meanwhile flushes nothing: said on their own
				bool bad = op != Op::DELETE && !flushed(dest, r.destDev);
				if (srcDev && srcDev != r.destDev && !flushed(parent(sources[0]), srcDev)) bad = true;
				if (bad) { std::lock_guard<std::mutex> l(mu); p.unwritten = true; }
			}
			unlink(PIDFILE);
			{
				std::lock_guard<std::mutex> l(mu);
				p.stopped = r.cut;
				p.current.clear();
				p.seconds = monoSec() - started;
				p.state = Progress::DONE;
			}
			if (done) done();
		}

		void Job::start(Op op, const std::vector<std::string>& sources, const std::string& dest, Clash clash, bool sync, std::function<void()> done, dev_t dev)
		{
			if (th.joinable()) th.join();
			halt = false;
			{
				std::lock_guard<std::mutex> l(mu);
				p = Progress();
				p.op = op;
				p.state = Progress::COUNTING;
			}
			started = monoSec();
			if (sync) { run(op, sources, dest, clash, nullptr, dev); return; }
			th = std::thread([this, op, sources, dest, clash, done, dev]
			{
				setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 5);	// (the frames' thread first, as the picture workers)
				run(op, sources, dest, clash, done, dev);
			});
		}

		bool Job::running() const
		{
			std::lock_guard<std::mutex> l(mu);
			return p.state == Progress::COUNTING || p.state == Progress::WORKING;
		}

		Progress Job::progress() const
		{
			std::lock_guard<std::mutex> l(mu);
			Progress r = p;
			if (p.state != Progress::DONE && p.state != Progress::IDLE) r.seconds = monoSec() - started;
			return r;
		}

		Job::~Job()
		{
			halt = true;
			if (th.joinable()) th.join();
		}
	}
}
