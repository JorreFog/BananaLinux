// banana-leaseshim.so, preloaded into a game that draws on one panel while the menu keeps the other (RetroArch in the
// game panel's mode, see run-retroarch): opening /dev/dri/card0 gives a copy of the DRM lease banana-ui made for it
// (its fd in BANANA_DRM_LEASE_FD), which holds only that panel's connector, CRTC and plane. Everything else opens as
// it would.
#define _GNU_SOURCE
#undef _FILE_OFFSET_BITS	/* open and open64 both defined here: not one renamed to the other */
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int lease(const char* path, int flags)
{
	const char* e = getenv("BANANA_DRM_LEASE_FD");
	if (!e || !path || strcmp(path, "/dev/dri/card0")) return -1;
	return fcntl(atoi(e), (flags & O_CLOEXEC) ? F_DUPFD_CLOEXEC : F_DUPFD, 3);
}
static mode_t mode_of(int flags, va_list a) { return (flags & O_CREAT) ? (mode_t)va_arg(a, int) : 0; }

int open(const char* path, int flags, ...)
{
	int l = lease(path, flags);
	if (l >= 0) return l;
	va_list a; va_start(a, flags); mode_t m = mode_of(flags, a); va_end(a);
	static int (*real)(const char*, int, ...);
	if (!real) real = (int (*)(const char*, int, ...))dlsym(RTLD_NEXT, "open");
	return real(path, flags, m);
}
int open64(const char* path, int flags, ...)
{
	int l = lease(path, flags);
	if (l >= 0) return l;
	va_list a; va_start(a, flags); mode_t m = mode_of(flags, a); va_end(a);
	static int (*real)(const char*, int, ...);
	if (!real) real = (int (*)(const char*, int, ...))dlsym(RTLD_NEXT, "open64");
	return real(path, flags, m);
}
int openat(int dir, const char* path, int flags, ...)
{
	int l = lease(path, flags);
	if (l >= 0) return l;
	va_list a; va_start(a, flags); mode_t m = mode_of(flags, a); va_end(a);
	static int (*real)(int, const char*, int, ...);
	if (!real) real = (int (*)(int, const char*, int, ...))dlsym(RTLD_NEXT, "openat");
	return real(dir, path, flags, m);
}
