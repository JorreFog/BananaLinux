#include "recorder.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace banana
{
	// "<source>\t<what>" to bananad's datagram socket (bananad.c: rec_sock)
	void recordEvent(const char* source, const char* fmt, ...)
	{
		static std::mutex m;
		static int fd = -1;
		static double looked = -10;
		static bool on = false;
		std::lock_guard<std::mutex> l(m);	// (the picture worker and the main thread both may)
		timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		double now = ts.tv_sec + ts.tv_nsec / 1e9;
		if (now - looked >= 1) { on = access("/run/banana/recording", F_OK) == 0; looked = now; }
		if (!on) return;
		if (fd < 0 && (fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) < 0) return;
		char msg[320];
		int n = snprintf(msg, sizeof msg, "%s\t", source);
		if (n < 0 || n >= (int)sizeof msg) return;
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(msg + n, sizeof msg - (size_t)n, fmt, ap);
		va_end(ap);
		sockaddr_un a = {};
		a.sun_family = AF_UNIX;
		strncpy(a.sun_path, "/run/banana/recorder.sock", sizeof a.sun_path - 1);
		if (sendto(fd, msg, strlen(msg), MSG_DONTWAIT, (const sockaddr*)&a, sizeof a) < 0) {}
	}
}
