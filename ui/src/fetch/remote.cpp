#include "remote.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace banana
{
	namespace fetch
	{
		std::unique_ptr<Backend> makeDir(const Server& s);
#ifndef FETCH_NO_SMB
		std::unique_ptr<Backend> makeSmb(const Server& s);
#endif
		std::unique_ptr<Backend> makeSftp(const Server& s);
		std::unique_ptr<Backend> makeFtp(const Server& s);

		static const char* const WORDS[] = { "ok", "auth", "hostkey", "not_found", "denied", "no_resume", "network", "timeout",
			"stopped", "local", "no_space", "fat32", "password", "wrong_password", "unsafe", "no_memory", "unsupported", "other" };

		const char* errWord(Err e) { return WORDS[(int)e]; }

		Err errFromWord(const std::string& w)
		{
			for (size_t i = 0; i < sizeof WORDS / sizeof WORDS[0]; i++) if (w == WORDS[i]) return (Err)i;
			return Err::OTHER;
		}

		bool transient(Err e) { return e == Err::NETWORK || e == Err::TIMEOUT; }

		std::unique_ptr<Backend> makeBackend(const Server& s)
		{
			if (s.protocol == "dir") return makeDir(s);
#ifndef FETCH_NO_SMB
			if (s.protocol == "smb") return makeSmb(s);
#endif
			if (s.protocol == "sftp") return makeSftp(s);
			if (s.protocol == "ftp") return makeFtp(s);
			return nullptr;
		}

		void sortEntries(std::vector<Entry>& v)
		{
			std::stable_sort(v.begin(), v.end(), [](const Entry& a, const Entry& b)
			{
				if (a.dir != b.dir) return a.dir;
				return lower(a.name) < lower(b.name);
			});
		}

		int dialTcp(const std::string& host, int port, int timeoutMs, std::string& why)
		{
			addrinfo hints = {}, *res = nullptr;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_family = AF_UNSPEC;
			int g = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
			if (g != 0)
			{
				why = g == EAI_NONAME || g == EAI_NODATA ? "No server named " + host + " was found." : std::string("Looking up ") + host + ": " + gai_strerror(g);
				return -1;
			}
			int fd = -1, err = 0;
			for (addrinfo* a = res; a && fd < 0; a = a->ai_next)
			{
				int s = socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, a->ai_protocol);
				if (s < 0) { err = errno; continue; }
				int r = connect(s, a->ai_addr, a->ai_addrlen);
				if (r != 0 && errno == EINPROGRESS)
				{
					pollfd p = { s, POLLOUT, 0 };
					r = poll(&p, 1, timeoutMs);
					if (r == 0) { err = ETIMEDOUT; r = -1; }
					else if (r > 0)
					{
						socklen_t l = sizeof err;
						getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &l);
						r = err ? -1 : 0;
					}
					else err = errno;
				}
				else if (r != 0) err = errno;
				if (r != 0) { close(s); continue; }
				fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK);
				// a link that went away (Wi-Fi gone, the handheld asleep) is noticed in about 30 s even with nothing to send
				int on = 1, idle = 15, intvl = 5, cnt = 3;
				setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof on);
				setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle);
				setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof intvl);
				setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof cnt);
				setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
				fd = s;
			}
			freeaddrinfo(res);
			if (fd < 0) why = connectError(host, err);
			return fd;
		}

		std::string connectError(const std::string& host, int err)
		{
			return err == ECONNREFUSED ? host + " refused the connection: is the protocol and the port right?"
				: err == ETIMEDOUT ? host + " didn't answer in time."
				: err == EHOSTUNREACH || err == ENETUNREACH ? host + " can't be reached: is Wi-Fi on, and the server on this network?"
				: "Connecting to " + host + ": " + strerror(err);
		}

		bool pinMatches(const std::string& pin, const std::string& got)
		{
			if (pin.empty()) return true;
			auto split = [](const std::string& s, std::string& type, std::string& hash)
			{
				size_t sp = s.find(' ');
				type = sp == std::string::npos ? "" : s.substr(0, sp);
				hash = sp == std::string::npos ? s : s.substr(sp + 1);
			};
			std::string pt, ph, gt, gh;
			split(pin, pt, ph);
			split(got, gt, gh);
			return ph == gh && (pt.empty() || pt == gt);
		}

		std::string base64(const unsigned char* p, size_t n, bool pad)
		{
			static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string r;
			for (size_t i = 0; i < n; i += 3)
			{
				unsigned v = p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
				r += T[v >> 18 & 63];
				r += T[v >> 12 & 63];
				if (i + 1 < n) r += T[v >> 6 & 63]; else if (pad) r += '=';
				if (i + 2 < n) r += T[v & 63]; else if (pad) r += '=';
			}
			return r;
		}
	}
}
