// banana-fetch: "dir", a folder on this machine standing in for a server, for the tests (only with BANANA_FETCH_TEST=1).
// The server's host is the folder. What can go wrong on purpose, once per process:
//   BANANA_FETCH_TEST_CUT=<bytes>     the "connection" drops after that many bytes have been served
//   BANANA_FETCH_TEST_STALL=<bytes>   it goes quiet after that many, until the watchdog aborts it
//   BANANA_FETCH_TEST_DELAY=<ms>      a wait between pieces (a download slow enough to watch)
//   BANANA_FETCH_TEST_DROPLIST=<n>    the n-th listing (from 1) fails as a connection that died meanwhile does
//   BANANA_FETCH_TEST_OFFLINE=<n>     the n-th connection (from 1) fails as one made with Wi-Fi off does
//   BANANA_FETCH_TEST_PASSWORD=<pw>   the password it wants (another one is Err::AUTH; a guest needs none)
#include "remote.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace banana
{
	namespace fetch
	{
		namespace
		{
			long long envNum(const char* name)
			{
				const char* v = getenv(name);
				return v && *v ? atoll(v) : -1;
			}

			class DirBackend : public Backend
			{
			public:
				explicit DirBackend(const Server& srv) : s(srv) {}

				Result connect() override
				{
					const char* t = getenv("BANANA_FETCH_TEST");
					if (!t || strcmp(t, "1")) return Result(Err::UNSUPPORTED, "The dir protocol is for the tests only.");
					static long long connects = 0;
					if (++connects == envNum("BANANA_FETCH_TEST_OFFLINE")) return Result(Err::NETWORK, connectError(s.host, ENETUNREACH));
					struct stat st;
					if (stat(s.host.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return Result(Err::NETWORK, "No server named " + s.host + " was found.");
					const char* want = getenv("BANANA_FETCH_TEST_PASSWORD");
					if (want && *want && !s.guest && s.password != want) return Result(Err::AUTH, "The server refused the name or the password.");
					return Result();
				}

				Result list(const std::string& dir, std::vector<Entry>& out) override
				{
					static long long lists = 0;
					if (++lists == envNum("BANANA_FETCH_TEST_DROPLIST")) return Result(Err::NETWORK, "Can't list " + dir + ": the connection was lost (test).");
					std::string d = s.host + cleanPath(dir);
					DIR* h = opendir(d.c_str());
					if (!h) return Result(errno == ENOENT || errno == ENOTDIR ? Err::NOT_FOUND : Err::DENIED, "Can't list " + dir + ": " + strerror(errno));
					while (dirent* e = readdir(h))
					{
						if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
						struct stat st;
						if (stat((d + "/" + e->d_name).c_str(), &st) != 0) continue;	// a dangling link
						Entry x;
						x.name = e->d_name;
						x.dir = S_ISDIR(st.st_mode);
						x.size = x.dir ? 0 : (int64_t)st.st_size;
						x.mtime = st.st_mtime;
						out.push_back(x);
					}
					closedir(h);
					return Result();
				}

				Result fetch(const std::string& path, int64_t offset, const Sink& sink) override
				{
					static long long served = 0;
					static bool cut = false, stalled = false;
					long long cutAt = envNum("BANANA_FETCH_TEST_CUT"), stallAt = envNum("BANANA_FETCH_TEST_STALL");
					long long delay = envNum("BANANA_FETCH_TEST_DELAY");
					int fd = open((s.host + cleanPath(path)).c_str(), O_RDONLY | O_CLOEXEC);
					if (fd < 0) return Result(errno == ENOENT ? Err::NOT_FOUND : Err::DENIED, "Can't open " + path + ": " + strerror(errno));
					if (lseek(fd, offset, SEEK_SET) != offset) { close(fd); return Result(Err::NO_RESUME, "Can't start at " + std::to_string(offset)); }
					char buf[64 * 1024];
					Result r;
					for (;;)
					{
						if (aborted) { r = Result(Err::NETWORK, "Aborted."); break; }
						size_t want = sizeof buf;
						if (cutAt >= 0 && !cut && served + (long long)want > cutAt) want = (size_t)std::max(0LL, cutAt - served);
						if (stallAt >= 0 && !stalled && served + (long long)want > stallAt) want = (size_t)std::max(0LL, stallAt - served);
						if (want == 0)
						{
							if (cutAt >= 0 && !cut && served >= cutAt) { cut = true; r = Result(Err::NETWORK, "The connection dropped (test)."); break; }
							stalled = true;
							while (!aborted) std::this_thread::sleep_for(std::chrono::milliseconds(20));	// quiet, until the watchdog
							r = Result(Err::NETWORK, "No data came for a while (test).");
							break;
						}
						ssize_t n = read(fd, buf, want);
						if (n < 0) { r = Result(Err::NETWORK, strerror(errno)); break; }
						if (n == 0) break;
						served += n;
						if (!sink(buf, (size_t)n)) { r = Result(Err::STOPPED, "Stopped."); break; }
						if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
					}
					close(fd);
					return r;
				}

				void abort() override { aborted = true; }

			private:
				Server s;
				std::atomic<bool> aborted{ false };
			};
		}

		std::unique_ptr<Backend> makeDir(const Server& s) { return std::unique_ptr<Backend>(new DirBackend(s)); }
	}
}
