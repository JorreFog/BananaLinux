// The tools' layer (ui/src/android_tools.cpp) on a PC: the shell's syntax the core uses, read and run on stand-in
// tools, and the paths mapped into the app's tree. Built as android_tools.cpp is for Android, with libc's own functions
// standing in for the __real_ ones and the Platform's runTool replaced by a recorder:
//   c++ -std=c++17 -D__ANDROID__ -Ihost -include host/funopen.h -I../../../ui/src -o tools-test tools-test.cpp ../../../ui/src/android_tools.cpp -lpthread
//   ./tools-test
#include "android_host.h"
#include "android_platform.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// ---- libc for the __real_ functions, and BSD's funopen over glibc's fopencookie ----
extern "C"
{
	FILE* __real_fopen(const char* p, const char* m) { return fopen(p, m); }
	int __real_fclose(FILE* f) { return fclose(f); }
	int __real_open(const char* p, int f, ...) { va_list ap; va_start(ap, f); int m = va_arg(ap, int); va_end(ap); return open(p, f, m); }
	DIR* __real_opendir(const char* p) { return opendir(p); }
	int __real_access(const char* p, int m) { return access(p, m); }
	int __real_stat(const char* p, struct stat* s) { return stat(p, s); }
	int __real_lstat(const char* p, struct stat* s) { return lstat(p, s); }
	int __real_unlink(const char* p) { return unlink(p); }
	int __real_rename(const char* a, const char* b) { return rename(a, b); }
	int __real_mkdir(const char* p, mode_t m) { return mkdir(p, m); }
	int __real_rmdir(const char* p) { return rmdir(p); }
	int __real_mkfifo(const char* p, mode_t m) { return mkfifo(p, m); }
	int __real_chmod(const char* p, mode_t m) { return chmod(p, m); }
	int __real_truncate(const char* p, off_t n) { return truncate(p, n); }
	int __real_inotify_add_watch(int fd, const char* p, uint32_t m) { return inotify_add_watch(fd, p, m); }
	int __real_utimensat(int d, const char* p, const struct timespec t[2], int f) { return utimensat(d, p, t, f); }
	char* __real_realpath(const char* p, char* o) { return realpath(p, o); }
	FILE* __real_popen(const char* c, const char* m) { return popen(c, m); }
	int __real_pclose(FILE* f) { return pclose(f); }
	int __real_system(const char* c) { return system(c); }
	int __real_execv(const char* p, char* const a[]) { return execv(p, a); }

	FILE* __wrap_fopen(const char*, const char*);
	FILE* __wrap_popen(const char*, const char*);
	int __wrap_pclose(FILE*);
	int __wrap_system(const char*);
	int __wrap_access(const char*, int);
	int __wrap_execl(const char*, const char*, ...);
}

struct Cookie { void* c; int (*r)(void*, char*, int); int (*cl)(void*); };
extern "C" FILE* funopen(const void* c, int (*r)(void*, char*, int), int (*)(void*, const char*, int), fpos_t (*)(void*, fpos_t, int), int (*cl)(void*))
{
	cookie_io_functions_t io = {};
	io.read = [](void* k, char* b, size_t n) -> ssize_t { Cookie* x = (Cookie*)k; return x->r(x->c, b, (int)n); };
	io.close = [](void* k) -> int { Cookie* x = (Cookie*)k; int r = x->cl ? x->cl(x->c) : 0; delete x; return r; };
	return fopencookie(new Cookie{ (void*)c, r, cl }, "r", io);
}

// ---- the Platform's side: every tool run is written down ----
static std::vector<std::string> ran;
namespace banana
{
	namespace platform
	{
		std::string toolName(const std::string& path)
		{
			std::string base = path.substr(path.rfind('/') + 1);
			if (base == "banana-perf") return base;
			return path.compare(0, 20, "/usr/libexec/banana/") == 0 ? base : "";
		}
		int runTool(const std::vector<std::string>& argv, const std::string&, std::string& out)
		{
			std::string l;
			for (size_t i = 0; i < argv.size(); i++) l += i == 0 ? toolName(argv[0]) : "|" + argv[i];
			ran.push_back(l);
			if (argv.size() > 1 && argv[1] == "get") out = "42\n";
			else if (argv.size() > 1 && argv[1] == "fail") return 3;
			else if (argv.size() > 1) out = argv[1] + " out\n";
			return 0;
		}
		Platform& get() { static Platform* p = nullptr; return *p; }
	}
}

static int failed = 0;
static void check(bool ok, const std::string& what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
	if (!ok) failed++;
}
static std::string all() { std::string r; for (auto& s : ran) r += (r.empty() ? "" : " ; ") + s; ran.clear(); return r; }
static std::string pread(const char* cmd, int* st = nullptr)
{
	FILE* f = __wrap_popen(cmd, "r");
	std::string o;
	char b[256];
	while (f && fgets(b, sizeof b, f)) o += b;
	int s = f ? __wrap_pclose(f) : -1;
	if (st) *st = s;
	return o;
}

int main()
{
	using banana::android::setRoot;
	char tmpl[] = "/tmp/tools-test-XXXXXX";
	std::string root = mkdtemp(tmpl);
	setRoot(root);

	// the volume row's read (settings.cpp: volJob)
	std::string o = pread("/usr/libexec/banana/volume get 2>/dev/null");
	check(o == "42\n" && all() == "volume|get", "popen: a tool's output, its stderr dropped");
	int st;
	pread("'/usr/libexec/banana/sysstat' 2>/dev/null", &st);
	check(all() == "sysstat" && st == 0, "a quoted path (settings_quick.cpp's sysstat)");
	o = pread("/usr/libexec/banana/bluetooth up >/dev/null 2>&1; /usr/libexec/banana/bluetooth scan");
	check(o == "scan out\n" && all() == "bluetooth|up ; bluetooth|scan", "two commands, the first's output dropped (Bluetooth's scan)");
	pread("/usr/libexec/banana/update fail 2>&1", &st);
	all();
	check(st == (3 << 8), "pclose's status as waitpid's");
	std::string file = root + "/test-out";
	o = pread(("'/usr/libexec/banana/wifi' test > '" + file + "' 2>/dev/null").c_str());
	FILE* f = fopen(file.c_str(), "r");
	char b[64] = "";
	if (f) { if (!fgets(b, sizeof b, f)) b[0] = 0; fclose(f); }
	check(o.empty() && std::string(b) == "test out\n" && all() == "wifi|test", "a redirection to a file (Wi-Fi's test)");
	int s = __wrap_system("( /usr/libexec/banana/backlight sync ) </dev/null >/dev/null 2>&1 &");
	usleep(200000);
	check(s == 0 && all() == "backlight|sync", "a group in the background (systems.conf's changed commands)");
	__wrap_system("banana-perf 'balanced' &");
	usleep(200000);
	check(all() == "banana-perf|balanced", "banana-perf by its bare name");
	__wrap_system("flock -o /run/banana/sd2.lock /usr/libexec/banana/sd2 scan </dev/null >/dev/null 2>&1");
	check(all() == "sd2|scan", "a tool behind a wrapper (flock): the tool from its path on");
	__wrap_system("/usr/libexec/banana/update fail && /usr/libexec/banana/volume set 5 || /usr/libexec/banana/volume set 9");
	check(all() == "update|fail ; volume|set|9", "&& and ||");
	o = pread("echo plain");
	check(o == "plain\n" && all().empty(), "anything else is the system's shell");

	// a child the core forked to exec a tool (settings.cpp's capture() and spawn()): run in the parent, its output on
	// the child's stdout, its status the child's
	banana::android::toolsInit();
	int pfd[2];
	if (pipe(pfd) == 0)
	{
		pid_t p = fork();
		if (p == 0) { dup2(pfd[1], 1); close(pfd[0]); close(pfd[1]); __wrap_execl("/usr/libexec/banana/volume", "volume", "get", (char*)nullptr); _exit(99); }
		close(pfd[1]);
		std::string got;
		char cb[64];
		for (ssize_t n; (n = read(pfd[0], cb, sizeof cb)) > 0;) got.append(cb, (size_t)n);
		close(pfd[0]);
		int ws = 0;
		waitpid(p, &ws, 0);
		check(got == "42\n" && WIFEXITED(ws) && WEXITSTATUS(ws) == 0 && all() == "volume|get", "a forked child's exec: run in the parent, its output and status the child's");
		p = fork();
		if (p == 0) { __wrap_execl("/bin/sh", "sh", "-c", "/usr/libexec/banana/update fail", (char*)nullptr); _exit(99); }
		waitpid(p, &ws, 0);
		check(WIFEXITED(ws) && WEXITSTATUS(ws) == 3 && all() == "update|fail", "sh -c with a tool in a child: its status");
	}

	// paths
	mkdir((root + "/etc").c_str(), 0755);
	mkdir((root + "/etc/banana").c_str(), 0755);
	if (FILE* w = fopen((root + "/etc/banana/board").c_str(), "w")) { fputs("thor\n", w); fclose(w); }
	FILE* r = __wrap_fopen("/etc/banana/board", "r");
	char bd[16] = "";
	if (r) { if (!fgets(bd, sizeof bd, r)) bd[0] = 0; fclose(r); }
	check(std::string(bd) == "thor\n", "/etc/banana mapped into the app's tree");
	check(__wrap_access("/usr/share/banana/nothing", F_OK) != 0 && __wrap_access("/etc/banana/board", F_OK) == 0, "access() mapped");
	check(__wrap_access("/tmp", F_OK) == 0, "other paths as they are");
	if (system(("rm -rf '" + root + "'").c_str()) != 0) {}
	printf("%s\n", failed ? "tools-test: FAILED" : "tools-test: all passed");
	return failed ? 1 : 0;
}
