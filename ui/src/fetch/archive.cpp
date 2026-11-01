#include "archive.h"
#include "charsets.h"
#include "names.h"
#include "transfer.h"
#include "../zipdir.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <set>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

namespace fs = std::filesystem;

namespace banana
{
	namespace fetch
	{
		const char* const TEMP_DIR = ".banana-unpack";

		namespace
		{
			// a member as the archive tells it
			struct Member
			{
				std::string name;
				bool dir = false, link = false;
				int64_t size = 0, mtime = 0;
				int index = -1;			// its entry in the zip's directory
			};
			// one to unpack: its path on the card, relative and with slashes
			struct Planned
			{
				Member m;
				std::string rel;
				bool renamed = false;	// rel isn't its name in the archive (the cards' rules)
			};

			bool stopped(const ArchiveOptions& o) { return o.stop && *o.stop; }

			// a member's name as a safe relative path ("" for the root itself, to skip); false for one that would land
			// outside the folder (zip-slip), which refuses the whole archive
			bool cleanName(const std::string& name, std::string& rel)
			{
				std::string n = name;
				for (auto& c : n) if (c == '\\') c = '/';
				if (!n.empty() && n[0] == '/') return false;
				if (n.size() >= 2 && n[1] == ':' && isalpha((unsigned char)n[0])) return false;	// a drive letter
				std::vector<std::string> parts;
				std::string cur;
				for (size_t i = 0; i <= n.size(); i++)
				{
					char c = i < n.size() ? n[i] : '/';
					if (c != '/') { cur += c; continue; }
					if (cur == "..") { if (parts.empty() || parts.back() == "..") parts.push_back(".."); else parts.pop_back(); }
					else if (!cur.empty() && cur != ".") parts.push_back(cur);
					cur.clear();
				}
				rel.clear();
				for (auto& p : parts)
				{
					if (p == "..") return false;
					rel += (rel.empty() ? "" : "/") + p;
				}
				return true;
			}

			// what macOS's and Windows' archivers add, and nothing a ROM folder wants
			bool isJunk(const std::string& rel)
			{
				std::string base;
				for (size_t a = 0; a <= rel.size();)
				{
					size_t b = rel.find('/', a);
					if (b == std::string::npos) b = rel.size();
					base = rel.substr(a, b - a);
					if (base == "__MACOSX" || base == TEMP_DIR) return true;
					a = b + 1;
				}
				return base == ".DS_Store" || base == "Thumbs.db" || base.compare(0, 2, "._") == 0;
			}

			std::string cardPath(const std::string& rel)
			{
				std::string out;
				for (size_t a = 0; a <= rel.size();)
				{
					size_t b = rel.find('/', a);
					if (b == std::string::npos) b = rel.size();
					out += (out.empty() ? "" : "/") + safeName(rel.substr(a, b - a));
					a = b + 1;
				}
				return out;
			}

			// p, or "name (2).ext", "name (3).ext"... when the card has it already (FAT ignores case)
			std::string unused(const std::string& p, const std::set<std::string>& taken)
			{
				if (!taken.count(lower(p))) return p;
				size_t slash = p.rfind('/'), dot = p.rfind('.');
				std::string base = p, ext;
				if (dot != std::string::npos && (slash == std::string::npos || dot > slash + 1)) { base = p.substr(0, dot); ext = p.substr(dot); }
				for (int n = 2;; n++)
				{
					std::string c = base + " (" + std::to_string(n) + ")" + ext;
					if (!taken.count(lower(c))) return c;
				}
			}

			Result plan(const std::vector<Member>& raw, std::vector<Planned>& out, int64_t& total, int64_t& largest)
			{
				total = largest = 0;
				for (auto& m : raw)
				{
					std::string rel;
					if (!cleanName(m.name, rel))
						return Result(Err::UNSAFE, "The archive has a file that would land outside its folder (" + m.name + "): it isn't unpacked.");
					if (rel.empty() || isJunk(rel) || m.link) continue;
					Planned p;
					p.m = m;
					p.rel = cardPath(rel);
					p.renamed = p.rel != rel;
					out.push_back(p);
					if (!m.dir) { total += m.size; largest = std::max(largest, m.size); }
				}
				// a renamed file mustn't land on another member ("a:b" and "a_b"): the names that needed no change first
				std::set<std::string> taken;
				for (auto& p : out) if (!p.renamed || p.m.dir) taken.insert(lower(p.rel));
				for (auto& p : out)
					if (p.renamed && !p.m.dir) { p.rel = unused(p.rel, taken); taken.insert(lower(p.rel)); }
				return Result();
			}

			// ---- zip names --------------------------------------------------------------------------------------------
			bool hasHigh(const std::string& s)
			{
				for (unsigned char c : s) if (c >= 0x80) return true;
				return false;
			}

			// Info-ZIP's Unicode Path field (0x7075): version 1, the CRC-32 of the name as stored, the UTF-8 name (the CRC
			// guards against a field left behind by a tool that renamed the member without updating it)
			bool unicodePath(const std::string& extra, const std::string& raw, std::string& out)
			{
				const uint8_t* x = (const uint8_t*)extra.data();
				size_t n = extra.size();
				for (size_t q = 0; q + 4 <= n;)
				{
					size_t tag = x[q] | x[q + 1] << 8, sz = x[q + 2] | x[q + 3] << 8;
					if (q + 4 + sz > n) return false;
					if (tag == 0x7075 && sz >= 5 && x[q + 4] == 1)
					{
						uint32_t crc = x[q + 5] | x[q + 6] << 8 | x[q + 7] << 16 | (uint32_t)x[q + 8] << 24;
						std::string u((const char*)x + q + 9, sz - 5);
						if (crc == crc32(0, (const Bytef*)raw.data(), (uInt)raw.size()) && charset::validUtf8(u)) { out = u; return true; }
					}
					q += 4 + sz;
				}
				return false;
			}

			bool foreignName(const ZipEntry& e) { return !e.utf8() && hasHigh(e.name) && !charset::validUtf8(e.name); }

			// the code page of the names that aren't UTF-8 (Windows' own zipper writes its OEM one): CP866 when every such
			// byte is one of its Cyrillic letters, else zip's CP437. French in CP437 can pass for Cyrillic, but Russian
			// is by far the likelier here.
			const uint16_t* guessCharset(const std::vector<ZipEntry>& es)
			{
				bool seen = false;
				for (auto& e : es)
				{
					if (!foreignName(e)) continue;
					for (unsigned char b : e.name)
					{
						if (b < 0x80) continue;
						seen = true;
						if (!(b <= 0xaf || (b >= 0xe0 && b <= 0xf1))) return charset::CP437;
					}
				}
				return seen ? charset::CP866 : charset::CP437;
			}

			std::string zipName(const ZipEntry& e, const uint16_t* guess)
			{
				if (e.utf8() || !hasHigh(e.name)) return e.name;
				std::string u;
				if (unicodePath(e.extra, e.name, u)) return u;
				if (charset::validUtf8(e.name)) return e.name;	// UTF-8 without the flag (Linux's zip)
				return charset::toUtf8(e.name, guess);
			}

			// ---- 7zz --------------------------------------------------------------------------------------------------
			struct Run { int status = -1; std::string text; };

			// "65%" in a line of 7zz's progress
			int percentIn(const std::string& line)
			{
				int found = -1;
				for (size_t i = line.find('%'); i != std::string::npos; i = line.find('%', i + 1))
				{
					size_t b = i;
					while (b > 0 && isdigit((unsigned char)line[b - 1])) b--;
					if (b < i) found = atoi(line.substr(b, i - b).c_str());
				}
				return found >= 0 && found <= 100 ? found : -1;
			}

			// 7zz with these arguments (base7z's), the archive's password on its stdin when there is one: its messages and
			// progress read as text (their last 64 KB, for what went wrong), or (out >= 0) its output into out, the data
			// of "e -so", with its messages read. percent gets each "NN%", line each line of the output whole (a listing,
			// however long); a stop ends it.
			Run run7z(const std::vector<std::string>& args, const std::string& password, int out, std::atomic<bool>* stop,
				const std::function<void(int)>& percent, const std::function<void(const std::string&)>& line = nullptr)
			{
				Run r;
				int pfd[2], ifd[2] = { -1, -1 };
				if (pipe2(pfd, O_CLOEXEC) != 0) return r;
				if (!password.empty() && pipe2(ifd, O_CLOEXEC) != 0) { close(pfd[0]); close(pfd[1]); return r; }
				std::vector<char*> argv;
				for (auto& a : args) argv.push_back((char*)a.c_str());
				argv.push_back(nullptr);
				pid_t pid = fork();
				if (pid < 0)
				{
					for (int fd : { pfd[0], pfd[1], ifd[0], ifd[1] }) if (fd >= 0) close(fd);
					return r;
				}
				if (pid == 0)
				{
					int in = ifd[0] >= 0 ? ifd[0] : open("/dev/null", O_RDWR);
					if (in >= 0) dup2(in, 0);
					dup2(out >= 0 ? out : pfd[1], 1);
					dup2(pfd[1], 2);
					signal(SIGPIPE, SIG_DFL);	// (banana-fetch's own is ignored, and 7zz would inherit that)
					execv(argv[0], argv.data());
					_exit(127);
				}
				close(pfd[1]);
				if (ifd[1] >= 0)
				{
					// (asked for once, when 7zz meets the encryption; a line in a pipe that has room for it, so no wait)
					std::string l = password + "\n";
					if (write(ifd[1], l.data(), l.size()) < 0) {}
					close(ifd[0]);
					close(ifd[1]);
				}
				bool killed = false;
				std::string piece, whole;
				char buf[4096];
				for (;;)
				{
					pollfd p = { pfd[0], POLLIN, 0 };
					int n = poll(&p, 1, 200);
					if (stop && *stop && !killed) { kill(pid, SIGTERM); killed = true; }
					if (n <= 0) continue;
					ssize_t k = read(pfd[0], buf, sizeof buf);
					if (k < 0 && errno == EINTR) continue;
					if (k <= 0) break;
					r.text.append(buf, (size_t)k);
					if (r.text.size() > 65536) r.text.erase(0, r.text.size() - 65536);
					for (ssize_t i = 0; i < k; i++)
					{
						char c = buf[i];
						if (line)
						{
							if (c != '\n') whole += c;
							else
							{
								if (!whole.empty() && whole.back() == '\r') whole.pop_back();
								line(whole);
								whole.clear();
							}
						}
						// 7zz draws its progress over itself with backspaces
						if (c != '\r' && c != '\n' && c != '\b') { piece += c; continue; }
						int pc = percent ? percentIn(piece) : -1;
						if (pc >= 0) percent(pc);
						piece.clear();
					}
				}
				if (line && !whole.empty()) line(whole);
				close(pfd[0]);
				int st = 0;
				while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
				r.status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
				return r;
			}

			std::string lastLine(const std::string& t)
			{
				std::string cur, last;
				for (char c : t)
				{
					if (c == '\n' || c == '\r' || c == '\b') { size_t a = cur.find_first_not_of(' '); if (a != std::string::npos) last = cur.substr(a); cur.clear(); }
					else cur += c;
				}
				size_t a = cur.find_first_not_of(' ');
				return a != std::string::npos ? cur.substr(a) : last;
			}

			Result sevenZipError(const Run& r, const std::string& password, const std::string& what)
			{
				const std::string& t = r.text;
				auto has = [&](const char* s) { return t.find(s) != std::string::npos; };
				if (r.status == 127 || r.status < 0) return Result(Err::UNSUPPORTED, "7-Zip (7zz) couldn't be run.");
				if (has("Wrong password") || has("Cannot open encrypted archive") || has("Can not open encrypted archive"))
					return password.empty() ? Result(Err::PASSWORD, "The archive is protected by a password.")
						: Result(Err::WRONG_PASSWORD, "The password is wrong.");
				if (r.status == 8 || has("Can't allocate required memory") || has("Not enough memory") || has("Can not allocate"))
					return Result(Err::NO_MEMORY, "There isn't enough memory to unpack it.");
				if (has("Unsupported Method")) return Result(Err::UNSUPPORTED, "The archive uses a method 7-Zip can't decode.");
				if (has("No space left") || has("not enough space")) return Result(Err::NO_SPACE, "The card is full.");
				if (has("Missing volume") || has("Unexpected end of archive")) return Result(Err::OTHER, "A part of the archive is missing or cut short.");
				if (has("Cannot open the file as") || has("Can not open the file as") || has("Is not archive"))
					return Result(Err::UNSUPPORTED, "It isn't an archive 7-Zip reads, or it is damaged.");
				return Result(Err::OTHER, what + ": " + lastLine(t));
			}

			// a size in a Method field: "48k", "128K", "64m", or a power of two ("26": 64 MB) when log2
			int64_t sizeIn(const std::string& v, bool log2)
			{
				if (v.empty() || !isdigit((unsigned char)v[0])) return 0;
				size_t i = 0;
				int64_t n = 0;
				while (i < v.size() && isdigit((unsigned char)v[i])) n = n * 10 + (v[i++] - '0');
				char u = i < v.size() ? (char)tolower((unsigned char)v[i]) : 0;
				if (u == 'k') return n << 10;
				if (u == 'm') return n << 20;
				if (u == 'g') return n << 30;
				if (u == 'b') return n;
				return log2 ? (n < 48 ? (int64_t)1 << n : 0) : n;
			}

			// the memory a Method needs: "LZMA2:26", "LZMA:48k", "PPMD:o6:mem20", RAR's "v6:m3:128K" or "m3:17"
			int64_t dictionaryOf(const std::string& method)
			{
				int64_t best = 0;
				size_t a = 0;
				while (a < method.size())
				{
					size_t b = method.find(' ', a);
					if (b == std::string::npos) b = method.size();
					std::string tok = method.substr(a, b - a);
					a = b + 1;
					std::vector<std::string> parts;
					for (size_t x = 0; x <= tok.size();)
					{
						size_t y = tok.find(':', x);
						if (y == std::string::npos) y = tok.size();
						parts.push_back(tok.substr(x, y - x));
						x = y + 1;
					}
					const std::string& name = parts[0];
					for (size_t i = 1; i < parts.size(); i++)
					{
						const std::string& p = parts[i];
						if (name == "LZMA" || name == "LZMA2") best = std::max(best, sizeIn(p, true));
						else if (name == "PPMD" && p.compare(0, 3, "mem") == 0) best = std::max(best, sizeIn(p.substr(3), true));
						else if (name.size() >= 2 && name[0] == 'v' && isdigit((unsigned char)name[1])) best = std::max(best, sizeIn(p, true));
						else if (name.size() >= 2 && name[0] == 'm' && isdigit((unsigned char)name[1]) && parts.size() == 2) best = std::max(best, sizeIn(p, true));
					}
				}
				return best;
			}

			// 7zz's arguments. A password isn't one of them (any process can read a command line): run7z gives it on 7zz's
			// stdin, where it asks for it. With none, "-p": no password, and 7zz asks for none.
			std::vector<std::string> base7z(const std::string& cmd, const std::string& password, const std::string& codepage)
			{
				std::vector<std::string> a = { sevenZip(), cmd, "-sccUTF-8", "-scsUTF-8" };
				if (password.empty()) a.push_back("-p");
				if (!codepage.empty()) a.push_back("-mcp=" + codepage);
				return a;
			}

			// 7zz's technical listing, line by line as it comes (a few hundred bytes a member): "Path = ..." starts a
			// member, then its "Key = value" lines
			Result list7z(const std::string& path, const std::string& password, const std::string& codepage, std::vector<Member>& out,
				int64_t& dict, bool& encrypted)
			{
				if (sevenZip().empty()) return Result(Err::UNSUPPORTED, "7-Zip (7zz) isn't on this system.");
				std::vector<std::string> a = base7z("l", password, codepage);
				a.insert(a.end(), { "-slt", "-ba", "--", path });
				size_t before = out.size();
				Member cur;
				bool have = false;
				auto flush = [&] { if (have) { cur.index = (int)out.size(); out.push_back(cur); } cur = Member(); have = false; };
				Run r = run7z(a, password, -1, nullptr, nullptr, [&](const std::string& ln)
				{
					size_t eq = ln.find(" = ");
					if (eq == std::string::npos) return;
					std::string k = ln.substr(0, eq), v = ln.substr(eq + 3);
					if (k == "Path") { flush(); cur.name = v; have = true; return; }
					if (!have) return;
					if (k == "Folder") cur.dir = cur.dir || v == "+";
					else if (k == "Size") cur.size = atoll(v.c_str());
					else if (k == "Encrypted") encrypted = encrypted || v == "+";
					else if (k == "Method") dict = std::max(dict, dictionaryOf(v));
					else if (k == "Symbolic Link") cur.link = cur.link || !v.empty();
					else if (k == "Modified")
					{
						struct tm t = {};
						if (sscanf(v.c_str(), "%d-%d-%d %d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) == 6)
						{
							t.tm_year -= 1900; t.tm_mon -= 1; t.tm_isdst = -1;
							cur.mtime = (int64_t)mktime(&t);
						}
					}
					else if (k == "Attributes")
					{
						// "A -rw-r--r--", " -rw-r--r--" (RAR), "D drwxr-xr-x", "A lrwxrwxrwx": DOS letters, then a Unix mode
						std::string letters, mode, tok;
						for (size_t i = 0; i <= v.size(); i++)
						{
							if (i < v.size() && v[i] != ' ') { tok += v[i]; continue; }
							if (tok.size() == 10 && strchr("-dlbcps", tok[0])) mode = tok; else letters += tok;
							tok.clear();
						}
						if (letters.find('D') != std::string::npos || (!mode.empty() && mode[0] == 'd')) cur.dir = true;
						if (!mode.empty() && mode[0] == 'l') cur.link = true;
					}
				});
				if (r.status != 0) { out.resize(before); return sevenZipError(r, password, "Listing the archive"); }
				flush();
				return Result();
			}

			// ---- the archive, opened ------------------------------------------------------------------------------------
			struct Opened
			{
				bool native = false;		// a zip read here; else through 7zz
				std::string codepage;		// a zip's names in an OEM code page: 7zz's -mcp
				ZipDir zip;
				std::vector<Member> raw;
				int64_t dict = 0;
				bool encrypted = false;
			};

			Result openArchive(const std::string& path, const std::string& password, Opened& a)
			{
				Kind k = kindOf(baseName(path));
				if (k == NONE) return Result(Err::UNSUPPORTED, "Not a .zip, .7z or .rar file.");
				if (k == ZIP && a.zip.open(path))
				{
					const uint16_t* guess = guessCharset(a.zip.entries());
					bool plain = true;
					const auto& es = a.zip.entries();
					for (size_t i = 0; i < es.size(); i++)
					{
						const ZipEntry& e = es[i];
						Member m;
						m.name = zipName(e, guess);
						m.dir = e.dir();
						m.link = e.symlink();
						m.size = (int64_t)e.size;
						m.mtime = (int64_t)e.mtime();
						m.index = (int)i;
						if (!m.dir && (e.encrypted() || (e.method != 0 && e.method != 8))) plain = false;
						if (e.encrypted()) a.encrypted = true;
						if (foreignName(e)) a.codepage = guess == charset::CP866 ? "866" : "437";
						a.raw.push_back(m);
					}
					if (plain) { a.native = true; return Result(); }
					// encrypted, or bzip2, LZMA, Deflate64...: 7zz, its own listing (the names as it will write them)
					a.raw.clear();
				}
				bool enc = false;
				Result r = list7z(path, password, a.codepage, a.raw, a.dict, enc);
				a.encrypted = a.encrypted || enc;
				return r;
			}

			// a password, tried on the first file (in a solid archive the start of its block: quick)
			Result checkPassword(const std::string& path, const Opened& a, const std::string& password)
			{
				for (auto& m : a.raw)
				{
					if (m.dir || m.link) continue;
					std::vector<std::string> args = base7z("t", password, a.codepage);
					args.insert(args.end(), { "-bso0", "-bsp0", "-bse1", "-spd", "--", path, m.name });
					Run r = run7z(args, password, -1, nullptr, nullptr);
					return r.status == 0 ? Result() : sevenZipError(r, password, "Checking the password");
				}
				return Result();
			}

			// ---- unpacking ----------------------------------------------------------------------------------------------
			bool writeAll(int fd, const void* p, size_t n, int& err)
			{
				for (size_t w = 0; w < n;)
				{
					ssize_t k = write(fd, (const char*)p + w, n - w);
					if (k < 0 && errno == EINTR) continue;
					if (k <= 0) { err = k < 0 ? errno : ENOSPC; return false; }
					w += (size_t)k;
				}
				return true;
			}

			Result localError(int err, const std::string& what)
			{
				return Result(err == ENOSPC ? Err::NO_SPACE : Err::LOCAL, what + ": " + strerror(err));
			}

			Result extractZip(ZipDir& z, const std::vector<Planned>& plan, const std::string& tmp, const ArchiveOptions& o, int64_t total)
			{
				int64_t written = 0;
				for (auto& p : plan)
				{
					if (stopped(o)) return Result(Err::STOPPED, "Stopped.");
					std::string target = tmp + "/" + p.rel;
					if (p.m.dir) { if (!makeDirs(target)) return localError(errno, "Making " + p.rel); continue; }
					if (!makeDirs(dirName(target))) return localError(errno, "Making the folder of " + p.rel);
					int fd = open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
					if (fd < 0) return localError(errno, "Writing " + p.rel);
					int err = 0;
					bool ok = z.read(z.entries()[p.m.index], [&](const uint8_t* d, size_t n)
					{
						if (stopped(o) || !writeAll(fd, d, n, err)) return false;
						written += (int64_t)n;
						if (o.progress && total > 0) o.progress((double)written / total);
						return true;
					});
					close(fd);
					if (stopped(o)) return Result(Err::STOPPED, "Stopped.");
					if (err) return localError(err, "Writing " + p.rel);
					if (!ok) return Result(Err::OTHER, p.m.name + " is damaged in the archive (its data doesn't check out).");
					if (p.m.mtime > 0)
					{
						struct timeval tv[2] = { { (time_t)p.m.mtime, 0 }, { (time_t)p.m.mtime, 0 } };
						utimes(target.c_str(), tv);
					}
				}
				return Result();
			}

			Result extract7z(const std::string& path, const Opened& a, const std::vector<Planned>& plan, const std::string& tmp,
				const ArchiveOptions& o, int64_t total)
			{
				// left to 7zz: what it writes as the archive names it. Not: links, the clutter left out of the plan, and the
				// names the card refuses ("a:b"), each of which comes out of 7zz's output under its card name below.
				std::set<std::string> planned;
				std::vector<std::string> excluded;
				int64_t streamed = 0;
				for (auto& p : plan)
				{
					planned.insert(p.m.name);
					if (p.renamed) { excluded.push_back(p.m.name); if (!p.m.dir) streamed += p.m.size; }
				}
				for (auto& m : a.raw) if (!planned.count(m.name) && !m.dir) excluded.push_back(m.name);
				std::string list = tmp + ".lst";	// (beside the folder, not in it)
				std::vector<std::string> args = base7z("x", o.password, a.codepage);
				args.insert(args.end(), { "-y", "-bso0", "-bsp1", "-bse1", "-mmt=1", "-spd", "-o" + tmp });
				if (!excluded.empty())
				{
					std::ofstream f(list);
					for (auto& e : excluded) f << e << "\n";
					if (!f) return localError(errno, "Writing " + list);
					args.push_back("-x@" + list);
				}
				args.insert(args.end(), { "--", path });
				// -mmt=1: LZMA2 decoded on several threads buffers whole blocks (500+ MB measured for a 345 MB ISO,
				// 38 MB on one thread), and the handheld shares its memory with the menu
				double share = total > 0 ? (double)(total - streamed) / total : 1;
				Run r = run7z(args, o.password, -1, o.stop, [&](int pc) { if (o.progress) o.progress(pc / 100.0 * share); });
				unlink(list.c_str());
				if (stopped(o)) return Result(Err::STOPPED, "Stopped.");
				if (r.status != 0) return sevenZipError(r, o.password, "Unpacking");
				int64_t done = total - streamed;
				for (auto& p : plan)
				{
					if (!p.renamed) continue;
					std::string target = tmp + "/" + p.rel;
					if (p.m.dir) { if (!makeDirs(target)) return localError(errno, "Making " + p.rel); continue; }
					if (!makeDirs(dirName(target))) return localError(errno, "Making the folder of " + p.rel);
					int fd = open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
					if (fd < 0) return localError(errno, "Writing " + p.rel);
					std::vector<std::string> e = base7z("e", o.password, a.codepage);
					e.insert(e.end(), { "-so", "-spd", "-bso0", "-bsp0", "--", path, p.m.name });
					Run er = run7z(e, o.password, fd, o.stop, nullptr);
					close(fd);
					if (stopped(o)) return Result(Err::STOPPED, "Stopped.");
					if (er.status != 0) return sevenZipError(er, o.password, "Unpacking " + p.m.name);
					done += p.m.size;
					if (o.progress && total > 0) o.progress((double)done / total);
				}
				return Result();
			}

			// links and clutter a 7zz run left (defence in depth: the exclusions should have kept them out)
			void removeJunkAndLinks(const std::string& root)
			{
				std::error_code ec;
				std::vector<fs::path> gone;
				for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
				{
					std::string rel = it->path().string().substr(root.size() + 1);
					bool link = it->is_symlink(ec);
					if (link || isJunk(rel))
					{
						gone.push_back(it->path());
						if (!link && it->is_directory(ec)) it.disable_recursion_pending();
					}
				}
				for (auto& p : gone) fs::remove_all(p, ec);
			}

			// <root>/.banana-unpack/x-XXXXXX, on the target's card (a rename moves the result into place)
			Result tempDir(const std::string& dst, const std::string& root, std::string& tmp)
			{
				std::string base = (root.empty() ? dst : root) + "/" + TEMP_DIR;
				if (!makeDirs(dst)) return localError(errno, "Making " + dst);
				if (!makeDirs(base)) return localError(errno, "Making " + base);
				std::string t = base + "/x-XXXXXX";
				std::vector<char> b(t.begin(), t.end());
				b.push_back(0);
				if (!mkdtemp(b.data())) return localError(errno, "Making a folder in " + base);
				tmp = b.data();
				// across filesystems the move would copy everything again (and the space check counted the target's card)
				if (!sameFilesystem(tmp, dst))
				{
					rmdir(tmp.c_str());
					return Result(Err::LOCAL, base + " isn't on the same card as " + dst + ".");
				}
				return Result();
			}

			std::string contentRoot(const std::string& tmp, bool unwrap)
			{
				if (!unwrap) return tmp;
				std::error_code ec;
				int n = 0;
				std::string only;
				bool dir = false;
				for (auto& e : fs::directory_iterator(tmp, ec)) { n++; only = e.path().string(); dir = e.is_directory(ec) && !e.is_symlink(ec); }
				return n == 1 && dir ? only : tmp;
			}

			// p, or "name (2).ext", "name (3).ext"... when the card has that name already (FAT and exFAT ignore case)
			std::string freeName(const std::string& p)
			{
				struct stat st;
				if (lstat(p.c_str(), &st) != 0) return p;
				size_t slash = p.rfind('/'), dot = p.rfind('.');
				std::string base = p, ext;
				if (dot != std::string::npos && (slash == std::string::npos || dot > slash + 1)) { base = p.substr(0, dot); ext = p.substr(dot); }
				for (int n = 2;; n++)
				{
					std::string c = base + " (" + std::to_string(n) + ")" + ext;
					if (lstat(c.c_str(), &st) != 0) return c;
				}
			}

			// src into dst: folders merged, files already there replaced. A file where the card has a folder of that name,
			// or a folder where it has a file, goes beside it ("roms (2)"), not over it: an archive's "saves" never takes
			// the card's saves folder with it.
			bool moveMerge(const std::string& src, const std::string& dst)
			{
				struct stat ss, ds;
				if (lstat(src.c_str(), &ss) != 0) return false;
				if (lstat(dst.c_str(), &ds) != 0) return rename(src.c_str(), dst.c_str()) == 0;
				if (S_ISDIR(ss.st_mode) && S_ISDIR(ds.st_mode))
				{
					std::error_code ec;
					std::vector<std::string> kids;
					for (auto& e : fs::directory_iterator(src, ec)) kids.push_back(e.path().filename().string());
					for (auto& k : kids) if (!moveMerge(src + "/" + k, dst + "/" + k)) return false;
					return rmdir(src.c_str()) == 0;
				}
				if (S_ISDIR(ss.st_mode) || S_ISDIR(ds.st_mode)) return rename(src.c_str(), freeName(dst).c_str()) == 0;
				return rename(src.c_str(), dst.c_str()) == 0;	// (a file over a file, or over a link: replaced)
			}

			bool moveInto(const std::string& src, const std::string& dst)
			{
				if (!makeDirs(dst)) return false;
				std::error_code ec;
				std::vector<std::string> kids;
				for (auto& e : fs::directory_iterator(src, ec)) kids.push_back(e.path().filename().string());
				for (auto& k : kids) if (!moveMerge(src + "/" + k, dst + "/" + k)) return false;
				return true;
			}
		}

		std::string sevenZip()
		{
			const char* env = getenv("BANANA_7ZZ");
			if (env && *env) return env;
			if (access("/usr/bin/7zz", X_OK) == 0) return "/usr/bin/7zz";
			const char* path = getenv("PATH");
			std::string p = path ? path : "";
			for (size_t a = 0; a <= p.size();)
			{
				size_t b = p.find(':', a);
				if (b == std::string::npos) b = p.size();
				std::string c = p.substr(a, b - a) + "/7zz";
				if (b > a && access(c.c_str(), X_OK) == 0) return c;
				a = b + 1;
			}
			return "";
		}

		int64_t memAvailable()
		{
			const char* t = getenv("BANANA_FETCH_TEST");
			const char* m = getenv("BANANA_FETCH_TEST_MEM");
			if (t && !strcmp(t, "1") && m && *m) return atoll(m);
			std::ifstream f("/proc/meminfo");
			std::string k;
			int64_t v;
			while (f >> k >> v)
			{
				if (k == "MemAvailable:") return v * 1024;
				f.ignore(256, '\n');
			}
			return -1;
		}

		void removeStaleTemp(const std::string& root)
		{
			std::error_code ec;
			fs::remove_all(root + "/" + TEMP_DIR, ec);
		}

		Result inspectArchive(const std::string& path, const ArchiveOptions& o, ArchiveInfo& info)
		{
			Opened a;
			Result r = openArchive(path, o.password, a);
			if (!r) return r;
			if (a.encrypted && o.password.empty()) return Result(Err::PASSWORD, "The archive is protected by a password.");
			std::vector<Planned> planned;
			int64_t total, largest;
			if (!(r = plan(a.raw, planned, total, largest))) return r;
			if (a.encrypted && !(r = checkPassword(path, a, o.password))) return r;
			info = ArchiveInfo();
			for (auto& p : planned) if (!p.m.dir) info.files.push_back(p.rel);
			info.unpacked = total;
			info.largest = largest;
			info.dictionary = a.dict;
			info.encrypted = a.encrypted;
			return Result();
		}

		Result extractArchive(const std::string& path, const std::string& dst, const ArchiveOptions& o)
		{
			Opened a;
			Result r = openArchive(path, o.password, a);
			if (!r) return r;
			if (a.encrypted && o.password.empty()) return Result(Err::PASSWORD, "The archive is protected by a password.");
			std::vector<Planned> planned;
			int64_t total, largest;
			if (!(r = plan(a.raw, planned, total, largest))) return r;
			// LZMA's dictionary is in memory all at once, and the handheld shares its memory with the menu
			int64_t avail = memAvailable();
			if (!o.ignoreMemory && a.dict > 0 && avail >= 0 && a.dict + (64 << 20) > avail)
				return Result(Err::NO_MEMORY, "Unpacking it takes " + bytes(a.dict) + " of memory, and " + bytes(avail) + " is free.");
			Space sp;
			if (spaceAt(dst, sp))
			{
				if (sp.fat32 && (uint64_t)largest > FAT32_MAX)
					return Result(Err::FAT32, "A file in it is " + bytes(largest) + ", and this card (FAT32) takes 4 GB a file at most.");
				if ((int64_t)sp.free < total + (1 << 20))
					return Result(Err::NO_SPACE, "Not enough room on the card: " + bytes(total) + " needed, " + bytes((long long)sp.free) + " free.");
			}
			std::string tmp;
			if (!(r = tempDir(dst, o.tempRoot, tmp))) return r;
			r = a.native ? extractZip(a.zip, planned, tmp, o, total) : extract7z(path, a, planned, tmp, o, total);
			if (r)
			{
				if (!a.native) removeJunkAndLinks(tmp);
				if (!moveInto(contentRoot(tmp, o.unwrap), dst)) r = localError(errno, "Moving the files into " + dst);
				else if (o.progress) o.progress(1);
			}
			std::error_code ec;
			fs::remove_all(tmp, ec);
			rmdir(dirName(tmp).c_str());	// (only when nothing else is unpacking there)
			return r;
		}
	}
}
