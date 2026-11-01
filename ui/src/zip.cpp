#include "zip.h"
#include "zipdir.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace banana
{
	namespace
	{
		// 7-Zip (7zz, shipped for Files): a .7z, and zip members compressed with bzip2, LZMA, Deflate64... (QA's fresh
		// 0.5.1 card: Emerald in such a zip got "game companion: no ROM header")
		const char* sevenZip()
		{
			static const char* c[] = { "/usr/bin/7zz", "/usr/libexec/banana/7zz", "/usr/bin/7z" };
			for (const char* p : c) if (access(p, X_OK) == 0) return p;
			return nullptr;
		}
		// 7zz with these arguments, its stdout handed to out until out says stop (then 7zz is stopped); no shell
		bool run7z(std::vector<std::string> args, const std::function<bool(const uint8_t*, size_t)>& out)
		{
			const char* z = sevenZip();
			if (!z) return false;
			args.insert(args.begin(), z);
			std::vector<char*> argv;
			for (std::string& a : args) argv.push_back(&a[0]);
			argv.push_back(nullptr);
			int pfd[2];
			if (pipe2(pfd, O_CLOEXEC) != 0) return false;
			posix_spawn_file_actions_t fa;
			posix_spawn_file_actions_init(&fa);
			posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);
			posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
			pid_t pid;
			int e = posix_spawn(&pid, z, &fa, nullptr, argv.data(), environ);
			posix_spawn_file_actions_destroy(&fa);
			close(pfd[1]);
			if (e != 0) { close(pfd[0]); return false; }
			uint8_t buf[16384];
			bool more = true, any = false;
			ssize_t r;
			while (more && (r = read(pfd[0], buf, sizeof buf)) > 0) { any = true; more = out(buf, (size_t)r); }
			close(pfd[0]);
			if (more) { int st; while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {} return any && WIFEXITED(st) && WEXITSTATUS(st) == 0; }
			kill(pid, SIGTERM);	// (what was wanted is read: the rest of the member isn't needed)
			int st;
			while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
			return any;
		}
		// the members as 7zz lists them (-slt: "Path = ", "Size = ", "CRC = ", "Folder = +" a line each)
		std::vector<rnds::ZipMember> list7z(const std::string& path)
		{
			std::string text;
			run7z({ "l", "-slt", "-ba", "--", path }, [&](const uint8_t* p, size_t n) { text.append((const char*)p, n); return text.size() < (4u << 20); });
			std::vector<rnds::ZipMember> ms;
			rnds::ZipMember m;
			bool dir = false, have = false;
			auto flush = [&] { if (have && !dir && !m.name.empty()) ms.push_back(m); m = rnds::ZipMember(); dir = false; have = false; };
			size_t at = 0;
			while (at < text.size())
			{
				size_t nl = text.find('\n', at);
				std::string l = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
				at = nl == std::string::npos ? text.size() : nl + 1;
				if (!l.empty() && l.back() == '\r') l.pop_back();
				if (l.compare(0, 7, "Path = ") == 0) { flush(); m.name = l.substr(7); have = true; }
				else if (l.compare(0, 7, "Size = ") == 0) m.size = strtoull(l.c_str() + 7, nullptr, 10);
				else if (l.compare(0, 6, "CRC = ") == 0) m.crc = (uint32_t)strtoul(l.c_str() + 6, nullptr, 16);
				else if (l.compare(0, 10, "Folder = +") == 0 || l.compare(0, 14, "Attributes = D") == 0) dir = true;
			}
			flush();
			return ms;
		}
		bool endsWith7z(const std::string& s) { return s.size() > 3 && strcasecmp(s.c_str() + s.size() - 3, ".7z") == 0; }
	}

	bool readZip(const std::string& zip, const std::function<int(const std::vector<rnds::ZipMember>&)>& pick,
		const std::function<bool(const uint8_t*, size_t)>& sink)
	{
		if (endsWith7z(zip))
		{
			std::vector<rnds::ZipMember> ms = list7z(zip);
			int k = pick(ms);
			if (k < 0 || k >= (int)ms.size()) return false;
			return run7z({ "e", "-so", "--", zip, ms[k].name }, sink);
		}
		ZipDir z;
		if (!z.open(zip)) return false;
		std::vector<rnds::ZipMember> members;
		for (const ZipEntry& e : z.entries())
		{
			rnds::ZipMember m;
			m.name = e.name;
			m.size = e.size;
			m.crc = e.crc;
			members.push_back(m);
		}
		int k = pick(members);
		if (k < 0 || k >= (int)members.size()) return false;
		const ZipEntry& e = z.entries()[k];
		// (a method zlib can't do - bzip2 12, LZMA 14, Deflate64 9...: 7zz streams the member instead)
		if (!e.encrypted() && e.method != 0 && e.method != 8) return run7z({ "e", "-so", "--", zip, e.name }, sink);
		return z.read(e, sink);
	}
}
