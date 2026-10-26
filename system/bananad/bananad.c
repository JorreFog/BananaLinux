/* bananad: Banana's power daemon for the Anbernic RG DS and RG DS Plus.
 *
 *   - Sleep: a short press of the power button, closing the lid, or (in the menu) a few idle minutes put the
 *     handheld into suspend-to-RAM. The running game is paused first (SIGSTOP to its process group) and resumed
 *     after, the hooks in /etc/banana/sleep.d run around it, and banana-ui is told to draw its panels again.
 *     Opening the lid or pressing power wakes it (the device tree marks both as wakeup sources).
 *   - A long press of the power button shuts down cleanly.
 *   - Volume keys change the mixer; F (BTN_MODE) + volume changes both panels' backlights.
 *   - A battery about to run out shuts the handheld down before the data partition can be cut off mid-write.
 *   - The LEDs, as on ROCKNIX's RG DS: the upper one green, red at 20%, blinking red at 5%; the lower one amber while
 *     the charger is plugged in and the battery isn't full (steady: from the charger's online state, not the gauge's
 *     status, which says "Charging" only for a moment). The kernel blinks the red one (its timer trigger): bananad
 *     only changes them when the power state changes (power_supply uevents, and the battery poll).
 *   - Brightness and volume are kept across reboots (/data/banana/backlight, /data/banana/asound.state).
 *   - Sound goes where it should with no sound server: the RG DS has one card (the rk817 codec, its playback mux set
 *     to HP, which drives the speaker amplifier and the jack, as ROCKNIX sets it; the amplifier is switched off
 *     while headphones are in, its "Internal Speakers Switch"); the RG DS Plus has two (the
 *     aw88166 speaker amplifiers, and the rk817 for the headphones and the microphone). Playback goes to the
 *     speakers, or to the rk817 while headphones are in (the jack's switch event); recording always comes from the
 *     rk817, whose capture gain bananad sets for the DS's microphone (audio_setup). The route is ALSA's default
 *     device, written to /run/banana/asound.conf (/etc/asound.conf points
 *     there), so everything opened after a change follows it; the volume keys change the card that plays.
 *
 * Settings: /etc/banana/bananad.conf, then /data/banana/bananad.conf (key = value lines, see bananad.conf); SIGHUP
 * reads them again (the menu's settings screen sends it).
 * It reads the input devices alongside banana-ui and the games (evdev allows many readers) and grabs nothing.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/netlink.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define MAXDEV 32

static struct {
	char mixer[64];			/* ALSA simple control the volume keys change; "auto": the playing card's own */
	int volume_step;		/* percent */
	int brightness_step;	/* percent of max */
	double autosleep_min;	/* idle minutes before sleeping (0: never); in the menu, or everywhere (idle_in_games) */
	int lid_sleep;			/* closing the lid sleeps */
	int power_long_ms;		/* holding power this long shuts down */
	int battery_off_pct;	/* at or under this, discharging: shut down */
	char mem_sleep[16];		/* deep (suspend to RAM) or s2idle */
	int led_brightness;		/* 0..255; 0: the LEDs stay off */
	int led_low, led_critical;	/* percent: red, blinking red */
	int led_sleep;			/* the upper LED lit while asleep (the device tree's retain-state-suspended keeps it) */
	int fast_resume;		/* devices resume in parallel (/sys/power/pm_async) */
	double sleep_check_hours;	/* asleep: wake this often to look at the battery (0: never) */
	int sleep_poweroff_pct;	/* found at or under this while asleep: shut down cleanly */
	double dim_min;			/* idle minutes before the panels dim (0: never) */
	int dim_pct;			/* dimmed to this percent of their brightness */
	double poweroff_min;	/* asleep this long (any sleep), or idle this long when it never sleeps: off (0: never) */
	int idle_in_games;		/* the idle timers (dim, sleep, off) in games too, not only in the menu */
	int recorder;			/* the flight recorder (below): everything bananad sees, written to banana/logs/sessions */
} cfg = { "auto", 5, 8, 10, 1, 2000, 3, "deep", 128, 20, 5, 1, 1, 4, 5, 2, 30, 60, 0, 0 };

static int devfd[MAXDEV], ndev;
static void leds_update(void);
extern int leds_last;	/* (leds_update's, below) */
static int leds_asleep;	/* asleep: the upper LED as led_sleep says (suspend_now, until the wake) */
/* the sleep test's "kernel": a write of "mem" returns once a byte comes here (BANANAD_TEST_WAKE). A FIFO at
 * /sys/power/state alone returned at once, and a sleep straight after a wake raced the test's reader. While it
 * waits, <that path>.asleep is there: the test's "asleep", the same on every kernel (a wait channel's name isn't) */
static int test_wake = -1;
static char test_asleep[300];
static void audio_route(void);
static pid_t sound_child;	/* (the sound's setup at start, below) */
static void undim(void);
static void find_bottom_bl(void);
static char bottom_bl[64];	/* the bottom panel's backlight name; banana-ui's idle dim owns it while it runs */
static int headphones;
static int fn_held, power_down, lid_closed;
static int lid_retry_off;	/* a suspend failed with the lid shut: no retry until it opens */
static double power_since, last_input, ignore_power_until;
static int sel_down, sel_marked;	/* Select held: the recorder's marker at 2 s (once a hold) */
static double sel_since;

static volatile sig_atomic_t reload, stopping;
static void on_hup(int sig) { (void)sig; reload = 1; }
static void on_term(int sig) { (void)sig; stopping = 1; }	/* rcK's: the recording written out, then gone */

static double mono(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsyslog(LOG_INFO, fmt, ap);
	va_end(ap);
}

static void trim(char *s)
{
	char *e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
	char *b = s;
	while (*b && isspace((unsigned char)*b)) b++;
	memmove(s, b, strlen(b) + 1);
}

static void load_conf(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) return;
	char ln[256];
	while (fgets(ln, sizeof ln, f)) {
		char *eq = strchr(ln, '=');
		if (ln[0] == '#' || !eq) continue;
		*eq = 0;
		char *k = ln, *v = eq + 1;
		trim(k); trim(v);
		if (!strcmp(k, "mixer")) snprintf(cfg.mixer, sizeof cfg.mixer, "%s", v);
		else if (!strcmp(k, "volume_step")) cfg.volume_step = atoi(v);
		else if (!strcmp(k, "brightness_step")) cfg.brightness_step = atoi(v);
		else if (!strcmp(k, "autosleep_minutes")) cfg.autosleep_min = atof(v);
		else if (!strcmp(k, "lid_sleep")) cfg.lid_sleep = !strcmp(v, "yes") || !strcmp(v, "1");
		else if (!strcmp(k, "power_long_ms")) cfg.power_long_ms = atoi(v);
		else if (!strcmp(k, "battery_shutdown_percent")) cfg.battery_off_pct = atoi(v);
		else if (!strcmp(k, "mem_sleep")) snprintf(cfg.mem_sleep, sizeof cfg.mem_sleep, "%s", v);
		else if (!strcmp(k, "led_brightness")) cfg.led_brightness = atoi(v);
		else if (!strcmp(k, "led_low_percent")) cfg.led_low = atoi(v);
		else if (!strcmp(k, "led_critical_percent")) cfg.led_critical = atoi(v);
		else if (!strcmp(k, "led_sleep")) cfg.led_sleep = !strcmp(v, "on") || !strcmp(v, "yes") || !strcmp(v, "1");
		else if (!strcmp(k, "fast_resume")) cfg.fast_resume = !strcmp(v, "yes") || !strcmp(v, "1");
		else if (!strcmp(k, "sleep_check_hours")) cfg.sleep_check_hours = atof(v);
		else if (!strcmp(k, "sleep_poweroff_percent")) cfg.sleep_poweroff_pct = atoi(v);
		else if (!strcmp(k, "dim_minutes")) cfg.dim_min = atof(v);
		else if (!strcmp(k, "dim_percent")) cfg.dim_pct = atoi(v);
		else if (!strcmp(k, "poweroff_minutes")) cfg.poweroff_min = atof(v);
		else if (!strcmp(k, "idle_in_games")) cfg.idle_in_games = !strcmp(v, "yes") || !strcmp(v, "1");
		else if (!strcmp(k, "recorder")) cfg.recorder = !strcmp(v, "on") || !strcmp(v, "yes") || !strcmp(v, "1");
	}
	fclose(f);
}

/* the time zone the settings screen keeps in banana-ui's settings.conf (a POSIX rule), for sleep.log's times */
static void load_timezone(void)
{
	FILE *f = fopen("/data/banana/settings.conf", "r");
	if (!f) return;
	char ln[256];
	while (fgets(ln, sizeof ln, f)) {
		char *eq = strchr(ln, '=');
		if (ln[0] == '#' || !eq) continue;
		*eq = 0;
		char *k = ln, *v = eq + 1;
		trim(k); trim(v);
		if (!strcmp(k, "timezone") && *v) { setenv("TZ", v, 1); tzset(); }
	}
	fclose(f);
}

static int write_file(const char *path, const char *s)
{
	int fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);	/* as a shell's > does (sysfs takes it) */
	if (fd < 0) return -1;
	ssize_t n = write(fd, s, strlen(s));
	int e = errno;
	close(fd);
	errno = e;
	return n == (ssize_t)strlen(s) ? 0 : -1;
}

static int read_int(const char *path, int def)
{
	FILE *f = fopen(path, "r");
	if (!f) return def;
	int v = def;
	if (fscanf(f, "%d", &v) != 1) v = def;
	fclose(f);
	return v;
}

static void read_str(const char *path, char *out, size_t n)
{
	out[0] = 0;
	FILE *f = fopen(path, "r");
	if (!f) return;
	if (fgets(out, (int)n, f)) trim(out);
	fclose(f);
}

/* fork + exec, no shell; waits */
static int run(char *const argv[])
{
	pid_t p = fork();
	if (p == 0) {
		execvp(argv[0], argv);
		_exit(127);
	}
	int st = 0;
	if (p > 0) while (waitpid(p, &st, 0) < 0 && errno == EINTR) {}
	return st;
}

/* every executable in /etc/banana/sleep.d, in name order, with "pre" or "post"; background: not waited for (after a
 * wake, the game and the menu come first and Wi-Fi catches up) */
static void hooks_run(const char *when);
static void hooks(const char *when, int background)
{
	if (!background) { hooks_run(when); return; }
	pid_t p = fork();
	if (p == 0) {
		if (fork() == 0) { setsid(); hooks_run(when); _exit(0); }
		_exit(0);
	}
	if (p > 0) while (waitpid(p, NULL, 0) < 0 && errno == EINTR) {}
}

static void hooks_run(const char *when)
{
	struct dirent **list;
	int n = scandir("/etc/banana/sleep.d", &list, NULL, alphasort);
	if (n < 0) return;
	for (int i = 0; i < n; i++) {
		char path[300];
		snprintf(path, sizeof path, "/etc/banana/sleep.d/%s", list[i]->d_name);
		struct stat st;
		if (list[i]->d_name[0] != '.' && !stat(path, &st) && S_ISREG(st.st_mode) && (st.st_mode & 0111)) {
			char *argv[] = { path, (char *)when, NULL };
			run(argv);
		}
		free(list[i]);
	}
	free(list);
}

static pid_t pid_of(const char *pidfile)
{
	int p = read_int(pidfile, 0);
	return p > 0 && kill(p, 0) == 0 ? p : 0;
}

/* banana-ui's pid, by its name in /proc */
static pid_t ui_pid(void)
{
	DIR *d = opendir("/proc");
	if (!d) return 0;
	struct dirent *e;
	pid_t r = 0;
	while (!r && (e = readdir(d))) {
		if (!isdigit((unsigned char)e->d_name[0])) continue;
		char path[300], comm[32];
		snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
		read_str(path, comm, sizeof comm);
		if (!strcmp(comm, "banana-ui")) r = atoi(e->d_name);
	}
	closedir(d);
	return r;
}

/* the menu's level card for a volume or brightness key: what changed and its percent in /run/banana/osd, then
 * SIGUSR2 tells banana-ui to show it (it ignores a file older than a moment, e.g. one written during a game) */
static void osd(const char *what, int percent)
{
	char v[64];
	snprintf(v, sizeof v, "%s %d\n", what, percent);
	FILE *f = fopen("/run/banana/osd.new", "w");
	if (!f) return;
	fputs(v, f);
	fclose(f);
	rename("/run/banana/osd.new", "/run/banana/osd");
	pid_t ui = ui_pid();
	if (ui > 0) kill(ui, SIGUSR2);
}

/* the lid's state as the kernel has it now (EVIOCGSW), not from events: closed means closed on any device */
static const char *test_lid;	/* tests (tests/sleep-test.sh): the lid's state from this file, not the kernel */
static void read_lid(void)
{
	if (test_lid) { lid_closed = read_int(test_lid, 0); return; }
	lid_closed = 0;
	for (int i = 0; i < ndev; i++) {
		unsigned long sw[(SW_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = { 0 };
		if (ioctl(devfd[i], EVIOCGSW(sizeof sw), sw) >= 0 && (sw[SW_LID / (8 * sizeof(long))] >> (SW_LID % (8 * sizeof(long))) & 1))
			lid_closed = 1;
	}
}

/* the headphone jack's state as the kernel has it (the codec's jack is an input device with a switch) */
static int headphones_in(void)
{
	for (int i = 0; i < ndev; i++) {
		unsigned long sw[(SW_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = { 0 };
		if (ioctl(devfd[i], EVIOCGSW(sizeof sw), sw) >= 0 &&
		    (sw[SW_HEADPHONE_INSERT / (8 * sizeof(long))] >> (SW_HEADPHONE_INSERT % (8 * sizeof(long))) & 1))
			return 1;
	}
	return 0;
}

static void drain_inputs(void)
{
	struct input_event ev[64];
	for (int i = 0; i < ndev; i++)
		while (read(devfd[i], ev, sizeof ev) > 0) {}
}

/* ---- sleep ------------------------------------------------------------------------------------------------------
 * Closing the lid: the game is paused, the pre-sleep hooks run (Wi-Fi down), the card is flushed (this kernel skips
 * its own sync), an RTC alarm is set for the battery check, and the SoC suspends to RAM. Opening the lid: the game
 * and the menu go on first, then the post-sleep hooks in the background. Woken with the lid still shut (the power
 * button pressed in a bag, the battery check): straight back to sleep, without running anything again. */
static void shutdown_now(const char *why);
static int battery_discharging_pct(void);
static int sleep_fallback;		/* 0 as configured; 1 devices one at a time; 2 also s2idle (a suspend failed) */

/* ---- the flight recorder (Settings > System > Developer: recorder = on) -------------------------------------------
 * What bananad sees, a line each, to find later what happened: buttons and touches, the lid and the jack, the
 * backlights and the LEDs, volume and brightness, sleep and wake, shutdown, the battery and the charger, Wi-Fi.
 *   HH:MM:SS.mmm  <seconds since boot, sleep counted: a gap is a sleep>  <source>  <what>
 * Lines wait in memory and go to the card every 30 s, before a sleep or a shutdown, and when the buffer fills: a file
 * a session (banana/logs/sessions/session-<date>-<time>.txt), a new one past 8 MB, the newest 10 kept. While it
 * records /run/banana/recording is there (banana-ui's dot). Select held 2 s puts a marker in (rec_mark, below). */
#define REC_DIR "/data/banana/logs/sessions"
#define REC_KEEP 10
static struct { int on; char path[192]; char buf[32768]; size_t len; double flush_at; long long written; } recst;	/* (the recorder's state; rec() writes a line) */

static void rec_start(void);
static void rec_flush(void)
{
	if (!recst.len) return;
	FILE *f = fopen(recst.path, "a");
	if (f) {
		fwrite(recst.buf, 1, recst.len, f);
		fflush(f);
		fsync(fileno(f));
		fclose(f);
		recst.written += (long long)recst.len;
	}
	recst.len = 0;
	if (recst.written > 8LL * 1024 * 1024) rec_start();	/* (a new file past 8 MB) */
}

/* a line, at monotonic time t (a touch's own time; now for the rest) */
static void rec_vat(double t, const char *src, const char *fmt, va_list ap)
{
	if (!recst.on) return;
	char what[256], line[320];
	vsnprintf(what, sizeof what, fmt, ap);
	struct timespec wall, boot;
	clock_gettime(CLOCK_REALTIME, &wall);
	clock_gettime(CLOCK_BOOTTIME, &boot);
	double ago = mono() - t;
	if (ago < 0) ago = 0;
	double w = wall.tv_sec + wall.tv_nsec / 1e9 - ago, b = boot.tv_sec + boot.tv_nsec / 1e9 - ago;
	time_t ws = (time_t)w;
	struct tm tm;
	localtime_r(&ws, &tm);
	int n = snprintf(line, sizeof line, "%02d:%02d:%02d.%03d %10.3f  %-8s %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec,
		(int)((w - (double)ws) * 1000), b, src, what);
	if (n < 0) return;
	if ((size_t)n >= sizeof line) n = (int)sizeof line - 1;
	if (recst.len + (size_t)n > sizeof recst.buf) rec_flush();
	memcpy(recst.buf + recst.len, line, (size_t)n);
	recst.len += (size_t)n;
}
static void rec(const char *src, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void rec(const char *src, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	rec_vat(mono(), src, fmt, ap);
	va_end(ap);
}
static void rec_at(double t, const char *src, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void rec_at(double t, const char *src, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	rec_vat(t, src, fmt, ap);
	va_end(ap);
}

static int rec_session_file(const struct dirent *e) { return !strncmp(e->d_name, "session-", 8); }
/* a new session file (the newest REC_KEEP kept), and its header */
static void rec_start(void)
{
	recst.len = 0; recst.written = 0;
	mkdir("/data/banana/logs", 0755);
	mkdir(REC_DIR, 0755);
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char name[64];
	strftime(name, sizeof name, "session-%Y-%m-%d-%H%M%S.txt", &tm);
	snprintf(recst.path, sizeof recst.path, REC_DIR "/%s", name);
	struct dirent **list;
	int n = scandir(REC_DIR, &list, rec_session_file, alphasort);	/* (names sort by date) */
	for (int i = 0; i < n; i++) {
		if (i < n - (REC_KEEP - 1)) {
			char old[320];
			snprintf(old, sizeof old, REC_DIR "/%s", list[i]->d_name);
			unlink(old);
		}
		free(list[i]);
	}
	if (n >= 0) free(list);
	char board[32] = "", slot[8] = "";
	read_str("/etc/banana/board", board, sizeof board);
	read_str("/run/banana/slot", slot, sizeof slot);
	char head[256];
	int k = snprintf(head, sizeof head, "# BananaOS flight recorder: %s, slot %s, %04d-%02d-%02d\n"
		"# time of day, seconds since boot (sleep counted), source, what\n", board, slot,
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
	if (k > 0 && (size_t)k < sizeof recst.buf) { memcpy(recst.buf, head, (size_t)k); recst.len = (size_t)k; }
	int fd = open("/run/banana/recording", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);	/* (write_file doesn't create) */
	if (fd >= 0) { if (write(fd, recst.path, strlen(recst.path)) < 0) {} close(fd); }
}
/* recorder on or off, as bananad.conf says now */
static void rec_apply(void)
{
	if (cfg.recorder && !recst.on) {
		recst.on = 1;
		rec_start();
		recst.flush_at = mono() + 30;
		rec("recorder", "on");
	} else if (!cfg.recorder && recst.on) {
		rec("recorder", "off");
		rec_flush();
		recst.on = 0;
		unlink("/run/banana/recording");
	}
}

/* a button's name, as the RG DS's buttons are */
static const char *key_name(int code)
{
	switch (code) {
	case KEY_POWER: return "power"; case KEY_VOLUMEUP: return "volume-up"; case KEY_VOLUMEDOWN: return "volume-down";
	case BTN_SOUTH: return "a"; case BTN_EAST: return "b"; case BTN_NORTH: return "x"; case BTN_WEST: return "y";
	case BTN_TL: return "l"; case BTN_TR: return "r"; case BTN_TL2: return "l2"; case BTN_TR2: return "r2";
	case BTN_SELECT: return "select"; case BTN_START: return "start"; case BTN_MODE: return "f"; case BTN_Z: return "back";
	case BTN_THUMBL: return "l3"; case BTN_THUMBR: return "r3";
	case BTN_DPAD_UP: return "up"; case BTN_DPAD_DOWN: return "down"; case BTN_DPAD_LEFT: return "left"; case BTN_DPAD_RIGHT: return "right";
	}
	return NULL;
}

static void sleep_log(const char *fmt, ...)
{
	char line[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	say("%s", line);
	const char *path = "/data/banana/logs/sleep.log";
	struct stat st;
	if (!stat(path, &st) && st.st_size > 64 * 1024) rename(path, "/data/banana/logs/sleep.log.1");
	FILE *f = fopen(path, "a");
	if (!f) return;
	time_t now = time(NULL);
	char when[32];
	strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", localtime(&now));
	fprintf(f, "%s %s\n", when, line);
	fclose(f);
	rec("sleep", "%s", line);
}

/* the RTC alarm that wakes the handheld for the battery check; 0 clears it. Returns the alarm's time (0: none) */
static time_t rtc_alarm(int seconds)
{
	write_file("/sys/class/rtc/rtc0/wakealarm", "0");
	if (seconds <= 0) return 0;
	char v[32];
	snprintf(v, sizeof v, "+%d", seconds);
	if (write_file("/sys/class/rtc/rtc0/wakealarm", v)) return 0;
	return time(NULL) + seconds;
}

static void sleep_mode(void)
{
	/* deep: suspend to RAM (TF-A turns the SoC off, DRAM in self-refresh); s2idle as the fallback */
	char modes[128];
	read_str("/sys/power/mem_sleep", modes, sizeof modes);
	const char *want = sleep_fallback >= 2 ? "s2idle" : cfg.mem_sleep;
	char chosen[32];
	snprintf(chosen, sizeof chosen, "[%s]", want);
	if (strstr(modes, want) && !strstr(modes, chosen)) write_file("/sys/power/mem_sleep", want);	/* only if it changes */
	write_file("/sys/power/pm_async", cfg.fast_resume && sleep_fallback == 0 ? "1" : "0");
}

/* the backlights off and on again without changing their level (the lid shut with sleep off) */
static void backlights_power(int on)
{
	DIR *d = opendir("/sys/class/backlight");
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[320];
		snprintf(p, sizeof p, "/sys/class/backlight/%s/bl_power", e->d_name);
		write_file(p, on ? "0" : "4");
	}
	closedir(d);
	rec("screen", on ? "backlights on" : "backlights off");
}

/* a game on one panel (RetroArch) keeps the other one's backlight off: run-retroarch names it in this file, and a
 * wake-up or the lid opening, which switch the backlights on, leave that one dark again */
static void panel_off_again(void)
{
	char bl[256] = "";
	FILE *f = fopen("/run/banana/panel-off", "r");
	if (!f) return;
	if (fgets(bl, sizeof bl, f)) bl[strcspn(bl, "\n")] = 0;
	fclose(f);
	if (!strncmp(bl, "/sys/class/backlight/", 21) && !strstr(bl, "..")) {
		char p[300];
		snprintf(p, sizeof p, "%s/bl_power", bl);
		write_file(p, "4");
	}
}

static int charger_online(void);
static void suspend_now(const char *why)
{
	double t0 = mono();
	pid_t game = pid_of("/run/banana/game.pid");
	pid_t pg = game ? getpgid(game) : 0;
	if (pg > 0) kill(-pg, SIGSTOP);	/* DraStic's dynarec mustn't run on while the clocks drop (ROCKNIX's finding) */
	/* the panels dark at once: the kernel only turns them off once its suspend is under way, after the hooks (Wi-Fi
	 * down) and the card's sync, which left the lid shut on lit screens for a while. Back at the wake (below) */
	backlights_power(0);
	hooks("pre", 0);
	sync();
	sleep_mode();
	leds_asleep = 1; leds_last = -2; leds_update();	/* the upper LED lit (led_sleep): asleep, not off */
	int cycles = 0, rtc_wakes = 0, failures = 0, dark = 0;
	double asleep = 0, dark_from = 0, led_at = 0;
	const char *woke = "?";
	for (;;) {
		/* charging with the lid shut: no suspend, which turns the charging light off (it is a PWM LED, and the PWM
		 * stops in suspend). Dark and awake instead, the game still paused, until the lid opens (back as from a
		 * sleep) or the charger is pulled out (then a real sleep). */
		if (lid_closed && charger_online()) {
			if (!dark) {
				dark = 1; dark_from = mono();
				backlights_power(0);
				sleep_log("%s while charging: dark and awake, so the charging light stays on%s", why, game ? ", game paused" : "");
			}
			if (mono() >= led_at) { leds_update(); led_at = mono() + 10; }	/* (it goes out when the battery is full) */
			usleep(500000);
			read_lid();
			if (!lid_closed) { woke = "lid opened"; break; }
			continue;
		}
		/* the RTC's alarm: the battery check, or turning off (poweroff_minutes asleep, every sleep of this one counted),
		 * whichever comes first */
		int secs = (int)(cfg.sleep_check_hours * 3600);
		double off_in = cfg.poweroff_min > 0 ? cfg.poweroff_min * 60 - asleep : -1;
		if (off_in > 0 && (secs <= 0 || off_in < secs)) secs = (int)off_in + (off_in > (int)off_in);
		time_t alarm = rtc_alarm(secs);
		double t_in = mono();
		struct timespec b0, b1;
		clock_gettime(CLOCK_BOOTTIME, &b0);	/* BOOTTIME counts the time asleep, MONOTONIC doesn't */
		if (cycles == 0) sleep_log("sleep (%s): %.0f ms to suspend%s", why, (t_in - t0) * 1000, game ? ", game paused" : "");
		/* the RTC's wakeup count and clock now, to tell its alarm's wake from a button's, and that its clock runs */
		rec_flush();	/* (the recording on the card before the sleep) */
		int wc0 = read_int("/sys/class/rtc/rtc0/device/power/wakeup_count", -1);
		char rt[32] = "";
		read_str("/sys/class/rtc/rtc0/since_epoch", rt, sizeof rt);
		long rtc0 = atol(rt);
		int r = write_file("/sys/power/state", "mem");
		int err = errno;
		if (!r && test_wake >= 0) {
			char c;
			int fd = open(test_asleep, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);	/* (write_file doesn't create) */
			if (fd >= 0) close(fd);
			while (read(test_wake, &c, 1) < 0 && errno == EINTR && !stopping) {}	/* (stopped asleep: rcK, the test) */
			unlink(test_asleep);
		}
		clock_gettime(CLOCK_BOOTTIME, &b1);
		/* the alarm woke it: the RTC's wakeup count went up, or it cleared wakealarm (it does when the alarm fires).
		 * Not "the time is past the alarm's": a power press after it was taken for the alarm, and turned off a
		 * handheld someone had just woken (when the alarm hadn't woken it) */
		char pending[32];
		read_str("/sys/class/rtc/rtc0/wakealarm", pending, sizeof pending);
		int wc1 = read_int("/sys/class/rtc/rtc0/device/power/wakeup_count", -1);
		int rtc_fired = alarm && ((wc0 >= 0 && wc1 > wc0) || !pending[0]);
		if (alarm) {
			read_str("/sys/class/rtc/rtc0/since_epoch", rt, sizeof rt);
			double slept = (b1.tv_sec - b0.tv_sec) + (b1.tv_nsec - b0.tv_nsec) / 1e9;
			/* (an RTC whose clock doesn't run never reaches its alarm: "moved 0 s" over a long sleep says so) */
			sleep_log("woke after %.0f s: %s; the RTC's clock moved %ld s", slept,
				rtc_fired ? "the RTC's alarm" : "not the RTC's alarm (a button, the lid)", atol(rt) - rtc0);
		}
		rtc_alarm(0);
		cycles++;
		if (r) {
			/* it didn't suspend: devices one at a time, then the lighter s2idle, then give up and blank the panels */
			failures++;
			sleep_log("suspend failed (%s)%s", strerror(err), sleep_fallback < 2 ? ": trying the safer way" : "");
			if (sleep_fallback < 2 && failures <= 2) { sleep_fallback++; sleep_mode(); usleep(200000); continue; }
			woke = "failed";
			if (lid_closed) backlights_power(0);	/* can't sleep: at least dark until the lid opens */
			lid_retry_off = 1;	/* ...and not tried again every 5 s with the lid still shut */
			break;
		}
		asleep += (b1.tv_sec - b0.tv_sec) + (b1.tv_nsec - b0.tv_nsec) / 1e9;
		/* why it woke: the battery check's alarm, or someone (lid, power button) */
		if (rtc_fired) {
			rtc_wakes++;
			int pct = battery_discharging_pct();
			if (pct >= 0 && pct <= cfg.sleep_poweroff_pct) {
				sleep_log("battery check: %d%% after %.1f h asleep: shutting down", pct, asleep / 3600);
				if (pg > 0) kill(-pg, SIGCONT);	/* the game gets SIGTERM next, to write its saves */
				shutdown_now("battery empty while asleep");
				return;
			}
			/* asleep long enough: off, as a held power button does (not on the charger: off, the RG DS Plus doesn't
			 * charge; it sleeps on, the next alarm the battery check's) */
			if (cfg.poweroff_min > 0 && asleep >= cfg.poweroff_min * 60 - 1 && !charger_online()) {
				sleep_log("asleep %.0f min: turning off", asleep / 60);
				if (pg > 0) kill(-pg, SIGCONT);	/* the game gets SIGTERM next, to write its saves */
				shutdown_now("idle, asleep");
				return;
			}
			continue;	/* enough battery: back to sleep at once */
		}
		read_lid();
		if (lid_closed && cfg.lid_sleep) { woke = "lid shut"; continue; }	/* not meant: back to sleep */
		woke = lid_closed ? "power button" : "lid opened";
		break;
	}
	/* awake: the game and the menu first */
	double t_wake = mono();
	/* the panels on when the lid is open: dark while charging, or dark from a lid shut with lid_sleep off before a
	 * power-button sleep (the lid-open event that would light them is drained below) */
	if (dark || !lid_closed) backlights_power(!lid_closed);
	if (pg > 0) kill(-pg, SIGCONT);
	pid_t ui = ui_pid();
	if (ui && !game) kill(ui, SIGUSR1);	/* draws both panels again (the clock and the battery moved on) */
	double t_back = mono();
	undim();
	leds_asleep = 0; leds_last = -2;	/* (leds_update below: the upper LED back at its level) */
	hooks("post", 1);
	panel_off_again();
	/* the press that woke us comes in now: it is not a new request. Draining also drops the lid-open event, so the
	 * lid's state is read from the kernel instead */
	drain_inputs();
	read_lid();
	leds_update();
	if (headphones_in() != headphones) { headphones = !headphones; if (!sound_child) audio_route(); }
	ignore_power_until = mono() + 1.0;
	power_down = 0;
	last_input = mono();
	if (dark) sleep_log("dark while charging for %.0f s", t_wake - dark_from);
	sleep_log("awake (%s) after %.0f s asleep%s%s, game and menu back %.1f ms after the kernel", woke, asleep,
		rtc_wakes ? ", battery checked" : "", cycles - rtc_wakes > 1 ? ", slept again with the lid shut" : "",
		(t_back - t_wake) * 1000);
}

static void shutdown_now(const char *why)
{
	static int once;	/* (the reasons can line up: an idle turn-off and the battery in the same pass) */
	if (once++) return;
	say("shutting down (%s)", why);
	rec("power", "shutting down (%s)", why);
	rec_flush();
	pid_t game = pid_of("/run/banana/game.pid");
	if (game) {
		pid_t pg = getpgid(game);
		kill(pg > 0 ? -pg : game, SIGTERM);	/* let the emulator write its saves */
		for (int i = 0; i < 50 && kill(game, 0) == 0; i++) usleep(100000);
	}
	sync();
	/* busybox init runs /etc/init.d/rcK (unmount, sync) on SIGUSR2 = poweroff */
	if (kill(1, SIGUSR2)) reboot(RB_POWER_OFF);
	/* and nothing more from here until rcK stops bananad: back in the loop, with the lid still shut or the menu idle,
	 * it slept again in the middle of the shutdown (rcK frozen half-way until the next wake: "off" only when someone
	 * pressed power, or when the RTC woke it again; sleep.log showed "turning off" twice, a sleep between) */
	for (;;) { pause(); if (stopping) _exit(0); }	/* (the recording was written out above) */
}

static double save_volume_at, save_backlight_at;	/* debounced: written a moment after the last key */

/* ---- sound: which card plays ----------------------------------------------------------------------------------- */
static int card_codec = -1, card_speaker = -1, card_play = 0;	/* rk817; aw88166 (RG DS Plus only); the one playing */

/* the cards by what they are, from /proc/asound/cards (" 0 [rk817hp ]: simple-card - rk817_hp") */
static void audio_find_cards(void)
{
	FILE *f = fopen("/proc/asound/cards", "r");
	if (!f) return;
	char ln[256];
	while (fgets(ln, sizeof ln, f)) {
		int n;
		if (sscanf(ln, " %d [", &n) != 1) continue;
		for (char *c = ln; *c; c++) *c = (char)tolower((unsigned char)*c);
		if (strstr(ln, "aw88166")) card_speaker = n;
		else if (strstr(ln, "rk817") && card_codec < 0) card_codec = n;
	}
	fclose(f);
	/* a card that isn't the rk817 or the aw88166 (a USB sound card alone): the first one; none at all stays -1 */
	if (card_codec < 0 && card_speaker < 0) {
		FILE *g = fopen("/proc/asound/cards", "r");
		char ln[256];
		int n;
		while (g && fgets(ln, sizeof ln, g)) if (sscanf(ln, " %d [", &n) == 1) { card_codec = n; break; }
		if (g) fclose(g);
	}
}

static void audio_route(void)
{
	card_play = card_speaker >= 0 && !headphones ? card_speaker : card_codec;
	rec("audio", "sound to the %s", card_play < 0 ? "null device (no card)" : card_play == card_speaker ? "speakers" : headphones ? "headphones" : "codec");
	/* the RG DS: one card, whose HP output feeds both the jack and the speaker amplifier; the amplifier is the card's
	 * "Internal Speakers Switch" (a pin switch), off while headphones are in, or the speakers play along (#13) */
	if (card_speaker < 0 && card_codec >= 0) {
		char c[16];
		snprintf(c, sizeof c, "%d", card_codec);
		char *argv[] = { "amixer", "-q", "-c", c, "cset", "name=Internal Speakers Switch", headphones ? "off" : "on", NULL };
		run(argv);
	}
	/* The RG DS Plus's aw88166 amplifiers play 48 kHz whatever rate the I2S is set to (measured: 44.1 kHz audio is
	 * taken at 48,150 samples a second, 9% fast and pitched up; 48 kHz at 48,080), so their stream is always opened
	 * at 48 kHz and ALSA converts. Without it DraStic's 44.1 kHz ran its audio ring dry (stutter, crackles). The
	 * slave is pinned to S16 stereo 48 kHz so every open gets the same stream instead of renegotiating it.
	 *
	 * The same pin for capture, which is the microphone every DS game records through: playback and capture of one
	 * card share its I2S DAI, and the rockchip-i2s-tdm driver marks that DAI symmetric_rate (the RG DS's single
	 * rk817 card; the RG DS Plus's rk817 while headphones are in), so the second stream's hw_params is refused when
	 * its rate differs from the first's. The DS emulators play at 48 kHz but open the mic at 44.1 kHz, so the
	 * capture never started whatever bananad set the gains to (a standalone arecord, with no playback running, was
	 * fine). Pinning the capture's slave to 48 kHz makes the emulator's 44.1 kHz capture run at the playback's
	 * rate, ALSA's plug resampling it, and the two streams agree. */
	char play[160], cap[160];
	if (card_play >= 0)
		snprintf(play, sizeof play, "{ type plug slave { pcm \"hw:%d\" format S16_LE channels 2 rate 48000 } }", card_play);
	else
		snprintf(play, sizeof play, "\"null\"");
	if (card_codec >= 0)
		snprintf(cap, sizeof cap, "{ type plug slave { pcm \"hw:%d\" format S16_LE channels 2 rate 48000 } }", card_codec);
	else
		snprintf(cap, sizeof cap, "\"null\"");
	char conf[768];
	int n = snprintf(conf, sizeof conf,
		"# bananad: where sound plays now (%s)\n"
		"pcm.!default { type asym playback.pcm %s capture.pcm %s }\n",
		card_play == card_speaker ? "speakers" : headphones ? "headphones" : "the codec", play, cap);
	/* no card yet (the codec probed late, QEMU): "null" keeps the alias valid instead of a "plughw:-1" that would
	 * fail every later open; the ctl defaults to one, so its line goes only with a real card */
	if (card_play >= 0 && n > 0 && (size_t)n < sizeof conf)
		n += snprintf(conf + n, sizeof conf - (size_t)n, "ctl.!default { type hw card %d }\n", card_play);
	/* snprintf returns what it would have written, not what it did: clamp before the buffer goes to fwrite, or a
	 * truncated (or negative) n would read past conf */
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof conf) n = (int)sizeof conf - 1;
	FILE *f = fopen("/run/banana/asound.conf.new", "w");
	if (f) {
		fwrite(conf, 1, (size_t)n, f);
		fclose(f);
		rename("/run/banana/asound.conf.new", "/run/banana/asound.conf");
	}
	/* the control the volume keys change, for the settings screen's Volume (amixer -c <card> sset <control>) */
	f = fopen("/run/banana/mixer.new", "w");
	if (f) {
		fprintf(f, "%d %s\n", card_play, strcmp(cfg.mixer, "auto") ? cfg.mixer : card_play == card_speaker ? "PCM" : "Master");
		fclose(f);
		rename("/run/banana/mixer.new", "/run/banana/mixer");
	}
	/* a running RetroArch game panel (banana-ui's command pipe to it) opens its sound again where it plays now: alsa-lib
	 * reads its configuration once per process (Banana's RetroArch patch forgets it on AUDIO_REINIT). The other
	 * players watch the file themselves (banana-ui's sound, SuperDrastic, BananaDS). */
	int rc = open("/run/banana/retroarch.cmd", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (rc >= 0) {
		if (write(rc, "AUDIO_REINIT\n", 13) < 0) {}
		close(rc);
	}
	say("sound: playback on card %d (%s), recording on card %d (both 48 kHz, the shared DAI's one rate)", card_play,
	    card_play == card_speaker ? "speakers" : headphones ? "headphones" : "codec", card_codec);
}

static void audio_setup(void)
{
	audio_find_cards();
	if (card_codec < 0) { audio_route(); return; }	/* no sound card at all (QEMU): nothing to set */
	char c[16];
	snprintf(c, sizeof c, "%d", card_codec);
	/* the rk817's output path: HP drives both its jack and (on the RG DS) the speaker amplifier */
	char *argv[] = { "amixer", "-q", "-c", c, "sset", "Playback Mux", "HP", NULL };
	run(argv);
	/* ...and its input path, the microphone every DS emulator records from (asound.conf sends "default"'s capture
	 * here; SuperDrastic and BananaDS both open it). Only the gains are userspace's to set: DAPM powers the ADC and
	 * the microphone itself when a capture stream opens, and the codec has no capture mute. Set them anyway, so a
	 * muted or turned-down state restored from another firmware's asound.state (or left by a program) can't leave a
	 * game without a microphone. Exact hardware values, since as a percentage each reads backwards: the ADC volume's 0x00
	 * is 0 dB (full) and 0xff is -95 dB, and the PGA "Mic Capture Gain" is -18 dB (0) to +27 dB (0xf) in 3 dB steps,
	 * so 10 is +12 dB, a little over unity for the DS's quiet microphone. The RG DS Plus's mic is differential, which
	 * its device tree enables in the codec (rockchip,mic-in-differential). */
	char *mic[] = { "amixer", "-q", "-c", c, "sset", "Mic Capture Gain", "10", NULL };
	run(mic);
	char *cap[] = { "amixer", "-q", "-c", c, "sset", "Master Capture Volume", "0", NULL };
	run(cap);
	audio_route();
}

/* ---- the sound at start, without holding up the buttons ------------------------------------------------------
 * alsactl's restore and audio_setup's amixers are programs, and at boot they load from the squashfs while the menu,
 * the library and the GPU's and Wi-Fi's modules come up too: run before the loop, they kept the power and volume
 * keys deaf for seconds after the boot animation began. A child does them now while the loop takes the buttons;
 * volume steps pressed meanwhile wait for it (a restore after a press would undo the press). When it's done,
 * /run/banana/bananad-ready tells rcS (which waits for it before the menu starts) that the buttons and the sound are
 * up. A sound card that turns up later (a late probe) is set up the same way when its uevent comes. */
static pid_t sound_child;	/* setting the sound up, 0: not */
static int sound_ready, sound_again, sound_hp, volume_pending;

/* a boot milestone in /run/banana/boot-times, as rcS's mark() writes them (logs/boot.txt) */
static void boot_mark(const char *what)
{
	char up[32] = "";
	read_str("/proc/uptime", up, sizeof up);
	FILE *f = fopen("/run/banana/boot-times", "a");
	if (f) { fprintf(f, "%-28s %6.2f s\n", what, atof(up)); fclose(f); }
}

static void sound_start(void)
{
	if (sound_child > 0) { sound_again = 1; return; }	/* (one at a time: a card's uevents come in a burst) */
	sound_ready = 0;
	sound_hp = headphones;
	pid_t p = fork();
	if (p == 0) {
		/* the saved volumes first, then the route: the restore also brings back the speakers' switch as it was last
		 * saved (off if headphones were in then), and the route sets it from the jack as it is now */
		if (access("/data/banana/asound.state", R_OK) == 0) {
			char *argv[] = { "alsactl", "-f", "/data/banana/asound.state", "restore", NULL };
			run(argv);
		}
		audio_setup();
		_exit(0);
	}
	if (p < 0) { audio_setup(); sound_ready = 1; return; }	/* (no fork: as before, in line) */
	sound_child = p;
}

static void volume(int dir);
/* the loop's look at the child: done, the cards as it found them, what waited applied */
static void sound_poll(void)
{
	if (sound_child <= 0 || waitpid(sound_child, NULL, WNOHANG) != sound_child) return;
	sound_child = 0;
	audio_find_cards();
	if (headphones != sound_hp) audio_route();	/* plugged or pulled while it ran */
	if (sound_again) { sound_again = 0; sound_start(); return; }
	sound_ready = 1;
	if (access("/run/banana/bananad-ready", F_OK) != 0) {
		boot_mark("bananad: sound ready");
		FILE *f = fopen("/run/banana/bananad-ready", "w");
		if (f) fclose(f);
	}
	for (; volume_pending > 0; volume_pending--) volume(1);
	for (; volume_pending < 0; volume_pending++) volume(-1);
}

static void volume(int dir)
{
	if (!sound_ready) { volume_pending += dir; return; }	/* (the sound still being set up: sound_poll) */
	/* the volume script changes the control /run/banana/mixer names, the aw88166's as an attenuation (see it) */
	char step[16];
	snprintf(step, sizeof step, "%d", cfg.volume_step);
	char cmd[96];
	snprintf(cmd, sizeof cmd, "/usr/libexec/banana/volume %s %s 2>/dev/null", dir > 0 ? "up" : "down", step);
	save_volume_at = mono() + 2;
	int pct = -1;
	FILE *p = popen(cmd, "r");	/* (it prints the new level) */
	if (p) {
		if (fscanf(p, "%d", &pct) != 1) pct = -1;
		pclose(p);
	}
	if (pct >= 0) { osd("volume", pct); rec("audio", "volume %d%%", pct); }
}

static void brightness(int dir)
{
	DIR *d = opendir("/sys/class/backlight");
	if (!d) return;
	struct dirent *e;
	int pct = -1;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[320], v[32];
		snprintf(p, sizeof p, "/sys/class/backlight/%s/max_brightness", e->d_name);
		int max = read_int(p, 0);
		snprintf(p, sizeof p, "/sys/class/backlight/%s/brightness", e->d_name);
		int cur = read_int(p, 0);
		int step = max * cfg.brightness_step / 100;
		if (step < 1) step = 1;
		int nv = cur + dir * step;
		if (nv < step) nv = step;	/* never fully dark: a black panel looks like a dead one */
		if (nv > max) nv = max;
		snprintf(v, sizeof v, "%d", nv);
		write_file(p, v);
		if (max > 0) pct = nv * 100 / max;
	}
	closedir(d);
	save_backlight_at = mono() + 2;
	if (pct >= 0) { osd("brightness", pct); rec("screen", "brightness %d%%", pct); }
}

/* both backlights at a fraction of their maximum */
static void backlight_set_permille(int pm)
{
	DIR *d = opendir("/sys/class/backlight");
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[320], v[32];
		snprintf(p, sizeof p, "/sys/class/backlight/%s/max_brightness", e->d_name);
		int max = read_int(p, 0);
		int nv = max * pm / 1000;
		if (nv < 1) nv = 1;
		snprintf(p, sizeof p, "/sys/class/backlight/%s/brightness", e->d_name);
		snprintf(v, sizeof v, "%d", nv);
		write_file(p, v);
	}
	closedir(d);
}

/* the first backlight's level, in thousandths of its maximum */
static int backlight_permille(void)
{
	DIR *d = opendir("/sys/class/backlight");
	if (!d) return -1;
	struct dirent *e;
	int r = -1;
	while (r < 0 && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[320];
		snprintf(p, sizeof p, "/sys/class/backlight/%s/max_brightness", e->d_name);
		int max = read_int(p, 0);
		snprintf(p, sizeof p, "/sys/class/backlight/%s/brightness", e->d_name);
		if (max > 0) r = read_int(p, 0) * 1000 / max;
	}
	closedir(d);
	return r;
}

/* the bottom panel's backlight, from the backlight-path script (the DSI node names it, as one-panel does). While
 * banana-ui is managing the bottom's idle dim itself (it writes /run/banana/bottom-ui, see ui/src/main.cpp), the
 * menu's own dim leaves that one backlight to it: it would otherwise dim the bottom twice and undo banana-ui's
 * restore. Only the name is kept: the empty string means no bottom panel to single out (QEMU). */
static void find_bottom_bl(void)
{
	FILE *p = popen("/usr/libexec/banana/backlight-path bottom 2>/dev/null", "r");
	if (!p) return;
	char path[256] = "";
	if (fgets(path, sizeof path, p)) path[strcspn(path, "\n")] = 0;
	pclose(p);
	if (!path[0] || strncmp(path, "/sys/class/backlight/", 21)) return;
	const char *s = strrchr(path, '/');
	if (!s || !s[1]) return;
	snprintf(bottom_bl, sizeof bottom_bl, "%s", s + 1);
	/* published for banana-ui, whose idle timer dims the bottom itself (ui/src/main.cpp) */
	FILE *f = fopen("/run/banana/backlight-bottom.new", "w");
	if (f) {
		fprintf(f, "%s\n", path);
		fclose(f);
		rename("/run/banana/backlight-bottom.new", "/run/banana/backlight-bottom");
	}
	say("bottom panel's backlight: %s", bottom_bl);
}

/* idle in the menu: the panels down to dim_percent of what they were, back at the first press (the levels are
 * remembered here, not saved: the user's brightness is untouched) */
static int dimmed, dim_count;
static char dim_name[4][256];
static int dim_level[4];
static void dim(void)
{
	DIR *d = opendir("/sys/class/backlight");
	if (!d) return;
	struct dirent *e;
	dim_count = 0;
	while ((e = readdir(d)) && dim_count < 4) {
		if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof dim_name[0]) continue;
		if (bottom_bl[0] && !strcmp(e->d_name, bottom_bl) && !access("/run/banana/bottom-ui", F_OK)) continue;
		char p[600], v[32];
		snprintf(dim_name[dim_count], sizeof dim_name[0], "%s", e->d_name);
		snprintf(p, sizeof p, "/sys/class/backlight/%.255s/brightness", dim_name[dim_count]);
		int cur = read_int(p, -1);
		if (cur < 0) continue;
		dim_level[dim_count++] = cur;
		int nv = cur * cfg.dim_pct / 100;
		if (nv < 1) nv = 1;
		snprintf(v, sizeof v, "%d", nv);
		write_file(p, v);
	}
	closedir(d);
	dimmed = dim_count > 0;
	if (dimmed) rec("screen", "dimmed to %d%%", cfg.dim_pct);
	/* the level before dimming, for the settings screen (opened by the press that undims) */
	if (dimmed) {
		char p[600];
		snprintf(p, sizeof p, "/sys/class/backlight/%.255s/max_brightness", dim_name[0]);
		int max = read_int(p, 0);
		if (max > 0) { char v[16]; snprintf(v, sizeof v, "%d\n", dim_level[0] * 1000 / max); write_file("/run/banana/dim", v); }
	}
}
static void undim(void)
{
	if (!dimmed) return;
	for (int i = 0; i < dim_count; i++) {
		char p[600], v[32];
		snprintf(p, sizeof p, "/sys/class/backlight/%.255s/brightness", dim_name[i]);
		snprintf(v, sizeof v, "%d", dim_level[i]);
		write_file(p, v);
	}
	dimmed = 0;
	unlink("/run/banana/dim");
	rec("screen", "undimmed");
}

static void save_settings(double now)
{
	if (save_backlight_at > 0 && now >= save_backlight_at) {
		save_backlight_at = 0;
		char v[16];
		snprintf(v, sizeof v, "%d\n", backlight_permille());
		FILE *f = fopen("/data/banana/backlight", "w");
		if (f) { fputs(v, f); fclose(f); }
	}
	if (save_volume_at > 0 && now >= save_volume_at) {
		save_volume_at = 0;
		char *argv[] = { "alsactl", "-f", "/data/banana/asound.state", "store", NULL };
		run(argv);
	}
}

static void restore_settings(void)
{
	int pm = read_int("/data/banana/backlight", -1);
	if (pm > 0 && pm <= 1000) backlight_set_permille(pm);
	/* started again while the panels were dimmed (/run/banana/dim: the level before): undimmed, as the last one would */
	int dimmed_from = read_int("/run/banana/dim", -1);
	if (dimmed_from > 0 && dimmed_from <= 1000) { backlight_set_permille(dimmed_from); unlink("/run/banana/dim"); }
}

/* ---- the LEDs -------------------------------------------------------------------------------------------------- */
/* the LED whose name ends in :<function> (green:power, red:status, amber:charging), "" if none */
static void led_path(const char *function, char *out, size_t n)
{
	out[0] = 0;
	DIR *d = opendir("/sys/class/leds");
	if (!d) return;
	struct dirent *e;
	size_t fl = strlen(function);
	while ((e = readdir(d))) {
		size_t l = strlen(e->d_name);
		if (l > fl && e->d_name[l - fl - 1] == ':' && !strcmp(e->d_name + l - fl, function)) {
			snprintf(out, n, "/sys/class/leds/%s", e->d_name);
			break;
		}
	}
	closedir(d);
}

static void led_write(const char *led, const char *file, const char *v)
{
	if (!led[0]) return;
	char p[400];
	snprintf(p, sizeof p, "%s/%s", led, file);
	write_file(p, v);
}

/* one LED: off, on, blinking (the kernel's timer trigger) or following a trigger (the charger's) */
static void led_set(const char *led, const char *how, int level)
{
	char v[16];
	snprintf(v, sizeof v, "%d", level);
	if (!strcmp(how, "off")) { led_write(led, "trigger", "none"); led_write(led, "brightness", "0"); }
	else if (!strcmp(how, "on")) { led_write(led, "trigger", "none"); led_write(led, "brightness", v); }
	else if (!strcmp(how, "blink")) {
		led_write(led, "trigger", "timer");
		led_write(led, "delay_on", "500");
		led_write(led, "delay_off", "500");
		led_write(led, "brightness", v);
	} else { led_write(led, "trigger", how); led_write(led, "brightness", v); }
}

/* the battery: its directory under /sys/class/power_supply, capacity and whether it charges */
static int battery_state(char *name, size_t n, int *pct, int *charging)
{
	DIR *d = opendir("/sys/class/power_supply");
	if (!d) return 0;
	struct dirent *e;
	int found = 0;
	while (!found && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[320], s[32];
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/type", e->d_name);
		read_str(p, s, sizeof s);
		if (strcmp(s, "Battery")) continue;
		snprintf(name, n, "%s", e->d_name);
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/capacity", e->d_name);
		*pct = read_int(p, -1);
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/status", e->d_name);
		read_str(p, s, sizeof s);
		*charging = !strcmp(s, "Charging");
		found = 1;
	}
	closedir(d);
	return found;
}

/* a charger plugged in: a power supply that isn't the battery (USB, Mains) and is online. The battery's own status is
 * no use for the charging LED: the rk817's gauge says "Charging" only for a moment after the cable goes in, then
 * "Discharging" or "Not charging" while it still charges (the amber LED on its trigger lit up and went out again) */
static int charger_online(void)
{
	DIR *d = opendir("/sys/class/power_supply");
	if (!d) return 0;
	struct dirent *e;
	int on = 0;
	while (!on && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[320], s[32];
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/type", e->d_name);
		read_str(p, s, sizeof s);
		if (!strcmp(s, "Battery")) continue;
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/online", e->d_name);
		on = read_int(p, 0) > 0;
	}
	closedir(d);
	return on;
}

int leds_last = -2;	/* the upper LED's state as last set; -2: set it again */
static int amber_last = -2;	/* the charging LED's, the same way */
static void leds_update(void)
{
	static char green[300], red[300], amber[300];
	static int looked;
	int last = leds_last;
	/* looked for again while one is missing: a LED driver that probes after bananad starts (pwm-leds waits for its
	 * PWM) was never found, and that LED stayed as the kernel left it all session (both handhelds have all three) */
	if (!looked || !green[0] || !red[0] || !amber[0]) {
		looked = 1;
		led_path("power", green, sizeof green);
		led_path("status", red, sizeof red);
		led_path("charging", amber, sizeof amber);
	}
	char bat[256] = "";
	int pct = -1, charging = 0;
	if (!battery_state(bat, sizeof bat, &pct, &charging)) pct = 100;
	int plugged = charger_online();
	if (plugged) charging = 1;	/* plugged in counts as charging (see charger_online) */
	int level = cfg.led_brightness;
	/* the upper LED: green, red when low, blinking red when critical (not while charging). Asleep, with led_sleep on:
	 * full (a PWM at 100% holds its pin high even if the SoC's sleep stops its clock; the LED core leaves it lit, the
	 * kernel's retain-state-suspended), or off; off all the time with the LEDs off */
	int upper = !leds_asleep ? level : level > 0 && cfg.led_sleep ? 255 : 0;
	int state = upper <= 0 ? 0 : (!charging && pct >= 0 && pct <= cfg.led_critical) ? 3 : (!charging && pct >= 0 && pct <= cfg.led_low) ? 2 : 1;
	if (state != last) {
		leds_last = state;
		led_set(green, state == 1 ? "on" : "off", upper);
		led_set(red, state == 3 ? "blink" : state == 2 ? "on" : "off", upper);
		say("LEDs: battery %d%%%s", pct, charging ? ", charging" : "");
		static const char *const looks[] = { "off", "green", "red", "red blinking" };
		rec("led", "upper %s%s (battery %d%%%s)", looks[state], leds_asleep ? ", asleep" : "", pct, charging ? ", charging" : "");
	}
	/* the lower LED: amber, steady, the whole time the charger is plugged in and the battery isn't full */
	int amber_on = level > 0 && plugged && (pct < 0 || pct < 100);
	if (amber_on != amber_last || last == -2) {
		amber_last = amber_on;
		led_set(amber, amber_on ? "on" : "off", level);
		rec("led", "charging light %s", amber_on ? "on" : "off");
	}
}

/* the kernel's uevents (no udev here): the battery and the charger, and drivers for USB devices plugged in */
static int uevent_open(void)
{
	int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
	if (fd < 0) return -1;
	struct sockaddr_nl a = { .nl_family = AF_NETLINK, .nl_groups = 1 };
	if (bind(fd, (struct sockaddr *)&a, sizeof a)) { close(fd); return -1; }
	return fd;
}

/* a program in the background, not waited for (a double fork: no zombie, SIGCHLD stays default) */
static void spawn_bg(char *const argv[])
{
	pid_t p = fork();
	if (p == 0) {
		if (fork() == 0) { execv(argv[0], argv); _exit(127); }
		_exit(0);
	}
	if (p > 0) while (waitpid(p, NULL, 0) < 0 && errno == EINTR) {}
}

/* a USB device's driver, as udev would load it: the module its MODALIAS names (an Xbox pad's xpad, a USB serial
 * adapter's); the handheld's own hardware is built into the kernel */
static void load_driver(const char *alias)
{
	if (strncmp(alias, "usb:", 4)) return;
	char a[256];
	snprintf(a, sizeof a, "%s", alias);
	char *argv[] = { "/sbin/modprobe", "-q", a, NULL };
	spawn_bg(argv);
}

/* the USB devices there before bananad started (a pad plugged in at boot) */
static void load_drivers_present(void)
{
	DIR *d = opendir("/sys/bus/usb/devices");
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[320], alias[256];
		snprintf(p, sizeof p, "/sys/bus/usb/devices/%s/modalias", e->d_name);
		FILE *f = fopen(p, "r");
		if (!f) continue;
		if (fgets(alias, sizeof alias, f)) { alias[strcspn(alias, "\n")] = 0; load_driver(alias); }
		fclose(f);
	}
	closedir(d);
}

/* the second SD card: mounted, and its games offered to the library, to match the slot (/usr/libexec/banana/sd2).
 * In the background (a slow card's mount mustn't hold up the power button or sleep), one scan at a time: a card
 * going in sends several uevents */
static void sd2_scan(void)
{
	char *argv[] = { "/bin/sh", "-c", "flock /run/banana/sd2.lock /usr/libexec/banana/sd2 scan </dev/null >/dev/null 2>&1 &", NULL };
	run(argv);
}

/* the kernel's uevents: true when one was about the battery or the charger; a device added gets its driver */
static int uevent_read(int fd)
{
	char buf[8192];
	ssize_t n;
	int hit = 0;
	while ((n = recv(fd, buf, sizeof buf - 1, 0)) > 0) {
		buf[n] = 0;
		const char *alias = NULL;
		int add = 0, block = 0, mmc = 0, sound = 0;
		for (char *p = buf; p < buf + n; p += strlen(p) + 1) {
			if (!strcmp(p, "SUBSYSTEM=power_supply")) hit = 1;
			else if (!strcmp(p, "SUBSYSTEM=block")) block = 1;
			else if (!strcmp(p, "SUBSYSTEM=sound")) sound = 1;
			else if (!strncmp(p, "DEVNAME=mmcblk", 14)) mmc = 1;
			else if (!strcmp(p, "ACTION=add")) add = 1;
			else if (!strncmp(p, "MODALIAS=", 9)) alias = p + 9;
		}
		if (add && alias) load_driver(alias);
		if (block && mmc) sd2_scan();	/* a card in or out of the second slot (the script looks at what's there) */
		if (sound && add) sound_start();	/* a sound card probed late: its volumes, its route */
	}
	return hit;
}

/* ---- input the idle timers can't hear on devfd ------------------------------------------------------------------
 * The timers count from the last input, and devfd carries only keys and switches: the touchscreen's and the sticks'
 * motion woke bananad 60-200 times a second (open_inputs). But a DS game played with the stylus alone, or a stick
 * alone, isn't idle. Each touchscreen and stick gets a second descriptor that takes only EV_ABS, stamped with the
 * monotonic clock and never polled: quiet_scan() reads what piled up when the timers are looked at, and moves
 * last_input to the newest touch, or the newest time a stick was pushed at least halfway out (not its jitter round
 * the centre; a stick held out counts as now). */
#define QUIET_AXES 4
static const int quiet_axis[QUIET_AXES] = { ABS_X, ABS_Y, ABS_RX, ABS_RY };
static struct quiet { int fd, touch; int lo[QUIET_AXES], hi[QUIET_AXES]; int x, y, down, pend; } quiet[MAXDEV];
static int nquiet;

static int test_bit(const unsigned long *bits, int n) { return (bits[n / (8 * sizeof *bits)] >> (n % (8 * sizeof *bits))) & 1; }

static void quiet_add(const char *path, int touch_only)
{
	if (nquiet >= MAXDEV) return;
	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) return;
	struct quiet q = { .fd = fd, .touch = touch_only };
	if (!touch_only) {
		unsigned long abs[(ABS_CNT + 8 * sizeof(long) - 1) / (8 * sizeof(long))] = { 0 };
		unsigned long keys[(KEY_CNT + 8 * sizeof(long) - 1) / (8 * sizeof(long))] = { 0 };
		if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs) < 0) { close(fd); return; }	/* (not an evdev node) */
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
		q.touch = test_bit(abs, ABS_MT_POSITION_X) || test_bit(keys, BTN_TOUCH);
		int any = q.touch;
		for (int a = 0; a < QUIET_AXES && !q.touch; a++) {
			struct input_absinfo ai;
			if (test_bit(abs, quiet_axis[a]) && !ioctl(fd, EVIOCGABS(quiet_axis[a]), &ai) && ai.maximum > ai.minimum) {
				q.lo[a] = ai.minimum; q.hi[a] = ai.maximum; any = 1;
			}
		}
		if (!any) { close(fd); return; }	/* buttons only: devfd hears them */
		unsigned long types = 1ul << EV_ABS;	/* (EV_SYN always comes; an empty report doesn't) */
		struct input_mask m = { .type = 0, .codes_size = sizeof types, .codes_ptr = (uint64_t)(uintptr_t)&types };
		ioctl(fd, EVIOCSMASK, &m);
		int clk = CLOCK_MONOTONIC;
		ioctl(fd, EVIOCSCLOCKID, &clk);
	}
	quiet[nquiet++] = q;
}

/* a stick at least halfway out */
static int quiet_pushed(const struct quiet *q, int code, int value)
{
	for (int a = 0; a < QUIET_AXES; a++)
		if (quiet_axis[a] == code && q->hi[a] > q->lo[a]) {
			long mid = ((long)q->lo[a] + q->hi[a]) / 2, half = ((long)q->hi[a] - q->lo[a]) / 2;
			return labs((long)value - mid) * 2 > half;
		}
	return 0;
}

/* 1 when it moved last_input on */
static int quiet_scan(void)
{
	double newest = 0, now = mono();
	for (int i = 0; i < nquiet; i++) {
		struct quiet *q = &quiet[i];
		struct input_event ev[64];
		ssize_t r;
		while ((r = read(q->fd, ev, sizeof ev)) > 0)
			for (int k = 0; k < r / (ssize_t)sizeof ev[0]; k++) {
				const struct input_event *e = &ev[k];
				/* (SYN_DROPPED: the buffer overflowed with motion, at its time) */
				int moved = q->touch ? (e->type == EV_ABS || (e->type == EV_SYN && e->code == SYN_DROPPED))
					: (e->type == EV_ABS && quiet_pushed(q, e->code, e->value));
				double t = e->input_event_sec + e->input_event_usec / 1e6;
				if (moved && t > newest) newest = t > now ? now : t;
				/* the recorder: a touch down and its lift, where (the first finger; no position while the on-screen
				 * keyboard is open, /run/banana/keyboard from banana-ui: a password mustn't be read back) */
				if (recst.on && q->touch) {
					if (e->type == EV_ABS && (e->code == ABS_MT_POSITION_X || e->code == ABS_X)) q->x = e->value;
					else if (e->type == EV_ABS && (e->code == ABS_MT_POSITION_Y || e->code == ABS_Y)) q->y = e->value;
					else if (e->type == EV_ABS && e->code == ABS_MT_TRACKING_ID) {
						if (e->value >= 0 && !q->down) { q->down = 1; q->pend |= 1; }
						else if (e->value < 0 && q->down) { q->down = 0; q->pend |= 2; }
					} else if (e->type == EV_SYN && e->code == SYN_REPORT && q->pend) {
						int typing = access("/run/banana/keyboard", F_OK) == 0;
						for (int b = 1; b <= 2; b <<= 1) {
							if (!(q->pend & b)) continue;
							if (typing) rec_at(t, "touch", "%s (keyboard open: where not recorded)", b == 1 ? "down" : "up");
							else rec_at(t, "touch", "%s %d,%d", b == 1 ? "down" : "up", q->x, q->y);
						}
						q->pend = 0;
					}
				}
			}
		for (int a = 0; a < QUIET_AXES && !q->touch; a++) {
			struct input_absinfo ai;
			if (q->hi[a] > q->lo[a] && !ioctl(q->fd, EVIOCGABS(quiet_axis[a]), &ai) && quiet_pushed(q, quiet_axis[a], ai.value)) newest = now;
		}
	}
	if (newest <= last_input) return 0;
	last_input = newest;
	return 1;
}

static void open_inputs(void)
{
	for (int i = 0; i < ndev; i++) close(devfd[i]);
	ndev = 0;
	for (int i = 0; i < nquiet; i++) close(quiet[i].fd);
	nquiet = 0;
	/* the sleep test's stand-in touchscreen (a FIFO of input_event records with monotonic times) */
	const char *test_touch = getenv("BANANAD_TEST_TOUCH");
	if (test_touch) quiet_add(test_touch, 1);
	DIR *d = opendir("/dev/input");
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d)) && ndev < MAXDEV) {
		if (strncmp(e->d_name, "event", 5)) continue;
		char path[300];
		snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0) continue;
		/* only keys and switches reach us (handle() reads nothing else): the touchscreens' and the sticks' motion during
		 * a game woke bananad 60-200 times a second for nothing. The kernel drops a report left empty, so no wake;
		 * EV_SYN stays, its report is what wakes us for the rest (no EVIOCSMASK, an old kernel: everything, as before) */
		unsigned long types = (1ul << EV_SYN) | (1ul << EV_KEY) | (1ul << EV_SW);
		struct input_mask m = { .type = 0, .codes_size = sizeof types, .codes_ptr = (uint64_t)(uintptr_t)&types };
		ioctl(fd, EVIOCSMASK, &m);
		devfd[ndev++] = fd;
		quiet_add(path, 0);
	}
	closedir(d);
	read_lid();
}

/* Select held 2 s: a marker in the recording, and the power LED blinked twice to say so (in a game too) */
static void rec_mark(void)
{
	static int marks;
	rec("MARK", "*** marker %d ***", ++marks);
	rec_flush();
	char green[300] = "";
	led_path("power", green, sizeof green);
	if (green[0]) {
		char p[340];
		snprintf(p, sizeof p, "%s/brightness", green);
		for (int i = 0; i < 2; i++) { write_file(p, "0"); usleep(120000); write_file(p, "255"); usleep(120000); }
		leds_last = -2;
		leds_update();
	}
}

static void handle(const struct input_event *e)
{
	double now = mono();
	if (e->type == EV_KEY && (e->value != 2 || e->code == KEY_VOLUMEUP || e->code == KEY_VOLUMEDOWN)) {
		const char *k = key_name(e->code);
		if (k) rec("button", "%s %s", k, e->value == 1 ? "down" : e->value == 2 ? "held" : "up");
		else rec("button", "key %d %s", e->code, e->value == 1 ? "down" : e->value == 2 ? "held" : "up");
		if (e->code == BTN_SELECT && e->value != 2) { sel_down = e->value; sel_since = now; sel_marked = 0; }	/* (the loop times the hold) */
	}
	if (e->type == EV_SW && e->code == SW_LID) rec("lid", e->value ? "closed" : "opened");
	if (e->type == EV_SW && e->code == SW_HEADPHONE_INSERT) rec("audio", e->value ? "headphones in" : "headphones out");
	if (e->type == EV_KEY) { last_input = now; undim(); }
	if (e->type == EV_SW && e->code == SW_LID) {
		lid_closed = e->value;
		if (!lid_closed) { undim(); lid_retry_off = 0; }
		if (lid_closed && cfg.lid_sleep) suspend_now("lid closed");
		else if (!lid_closed || !cfg.lid_sleep) { backlights_power(!lid_closed); if (!lid_closed) panel_off_again(); }	/* no sleep: at least the panels go dark */
		return;
	}
	if (e->type == EV_SW && e->code == SW_HEADPHONE_INSERT) {
		if (headphones != e->value) { headphones = e->value; if (!sound_child) audio_route(); }	/* (else sound_poll routes) */
		return;
	}
	if (e->type != EV_KEY) return;
	/* a volume key held down: the kernel's autorepeat (gpio-keys-volume's), a step every 150 ms so the level card keeps
	 * up; every other key's repeats mean nothing here */
	if (e->value == 2) {
		static double last_repeat;
		if ((e->code != KEY_VOLUMEUP && e->code != KEY_VOLUMEDOWN) || now - last_repeat < 0.15) return;
		last_repeat = now;
	}
	switch (e->code) {
	case BTN_MODE: fn_held = e->value; break;
	case KEY_POWER:
		if (now < ignore_power_until) break;
		if (e->value) { power_down = 1; power_since = now; }
		else if (power_down) { power_down = 0; suspend_now("power button"); }
		break;
	case KEY_VOLUMEUP: if (e->value) { if (fn_held) brightness(1); else volume(1); } break;
	case KEY_VOLUMEDOWN: if (e->value) { if (fn_held) brightness(-1); else volume(-1); } break;
	}
}

/* percent, or -1 when there is no battery or it is charging */
static int battery_discharging_pct(void)
{
	/* (the rk817's gauge says "Discharging" while a charger charges it, see charger_online: plugged in at 2% must not
	 * shut down) */
	if (charger_online()) return -1;
	DIR *d = opendir("/sys/class/power_supply");
	if (!d) return -1;
	struct dirent *e;
	int r = -1;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[320], s[32];
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/type", e->d_name);
		read_str(p, s, sizeof s);
		if (strcmp(s, "Battery")) continue;
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/status", e->d_name);
		read_str(p, s, sizeof s);
		if (!strcmp(s, "Discharging")) {
			snprintf(p, sizeof p, "/sys/class/power_supply/%s/capacity", e->d_name);
			r = read_int(p, -1);
		}
		break;
	}
	closedir(d);
	return r;
}

/* the recorder's events from banana-ui (ui/src/recorder.cpp): datagrams "<source>\t<what>" on
 * /run/banana/recorder.sock, copied in as they come (banana-ui sends only while it records, and never waits) */
static int rec_sock_open(void)
{
	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0) return -1;
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	snprintf(a.sun_path, sizeof a.sun_path, "%s", "/run/banana/recorder.sock");
	unlink(a.sun_path);
	if (bind(fd, (struct sockaddr *)&a, sizeof a)) { close(fd); return -1; }
	return fd;
}
static void rec_sock_read(int fd)
{
	char m[400];
	ssize_t n;
	while ((n = recv(fd, m, sizeof m - 1, 0)) > 0) {
		m[n] = 0;
		for (char *p = m; *p; p++) if ((unsigned char)*p < 32 && *p != '\t') *p = ' ';	/* (a line each, always) */
		const char *src = "menu", *what = m;
		char *tab = strchr(m, '\t');
		if (tab) { *tab = 0; if (tab - m <= 8) src = m; what = tab + 1; }
		rec(src, "%s", what);
	}
}

/* the recorder: the charger plugged in or pulled out (a uevent, and each minute) */
static void rec_charger(void)
{
	static int last = -1;
	if (!recst.on) return;
	int on = charger_online();
	if (on != last) { if (last >= 0 || on) rec("power", on ? "charger in" : "charger out"); last = on; }
}
/* the recorder's look each minute: the battery as its gauge says, Wi-Fi when its state or signal changed */
static void rec_minute(void)
{
	if (!recst.on) return;
	rec_charger();
	DIR *d = opendir("/sys/class/power_supply");
	struct dirent *e;
	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[320], s[32] = "", st[32] = "";
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/type", e->d_name);
		read_str(p, s, sizeof s);
		if (strcmp(s, "Battery")) continue;
		#define PS(f) (snprintf(p, sizeof p, "/sys/class/power_supply/%s/" f, e->d_name), p)
		/* (the rk817's gauge has only the averages) */
		int pct = read_int(PS("capacity"), -1), mv = read_int(PS("voltage_now"), read_int(PS("voltage_avg"), 0)) / 1000;
		int ma = read_int(PS("current_now"), read_int(PS("current_avg"), 0)) / 1000, t = read_int(PS("temp"), -1000);
		read_str(PS("status"), st, sizeof st);
		#undef PS
		if (t > -1000) rec("battery", "%d%% %s, %d mV, %d mA, %d.%d C", pct, st, mv, ma, t / 10, abs(t % 10));
		else rec("battery", "%d%% %s, %d mV, %d mA", pct, st, mv, ma);
		break;
	}
	if (d) closedir(d);
	char op[16] = "";
	read_str("/sys/class/net/wlan0/operstate", op, sizeof op);
	int level = 0;
	FILE *f = fopen("/proc/net/wireless", "r");
	char ln[256];
	while (f && fgets(ln, sizeof ln, f)) {
		char ifc[32]; int status; float link, lvl;
		if (sscanf(ln, " %31[^:]: %x %f %f", ifc, &status, &link, &lvl) == 4 && !strcmp(ifc, "wlan0")) level = (int)lvl;
	}
	if (f) fclose(f);
	static char last_op[16] = "?";
	static int last_level = 1000;
	if (strcmp(op, last_op) || abs(level - last_level) >= 5) {
		if (!op[0]) rec("wifi", "no Wi-Fi");
		else if (level) rec("wifi", "%s, signal %d dBm", op, level);
		else rec("wifi", "%s", op);
		snprintf(last_op, sizeof last_op, "%s", op);
		last_level = level;
	}
}

int main(int argc, char **argv)
{
	(void)argc; (void)argv;
	openlog("bananad", LOG_PID, LOG_DAEMON);
	test_lid = getenv("BANANAD_TEST_LID");
	if (getenv("BANANAD_TEST_WAKE")) {
		test_wake = open(getenv("BANANAD_TEST_WAKE"), O_RDWR | O_CLOEXEC);
		snprintf(test_asleep, sizeof test_asleep, "%s.asleep", getenv("BANANAD_TEST_WAKE"));
	}
	load_conf("/etc/banana/bananad.conf");
	load_conf("/data/banana/bananad.conf");
	load_timezone();
	unlink("/run/banana/recording");	/* (left by a bananad stopped before: as bananad.conf says now) */
	rec_apply();
	rec("bananad", "started");
	signal(SIGCHLD, SIG_DFL);
	struct sigaction sa = { 0 };
	sa.sa_handler = on_hup;		/* no SA_RESTART: poll() returns, and the loop reloads */
	sigaction(SIGHUP, &sa, NULL);
	sa.sa_handler = on_term;
	sigaction(SIGTERM, &sa, NULL);
	mkdir("/run/banana", 0755);
	/* the RTC may wake the handheld (its alarm: the battery check, turning off when idle) */
	write_file("/sys/class/rtc/rtc0/device/power/wakeup", "enabled");
	find_bottom_bl();	/* which backlight is the bottom one: banana-ui's idle dim owns it */

	int ino = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (ino >= 0) inotify_add_watch(ino, "/dev/input", IN_CREATE | IN_DELETE);
	unlink("/run/banana/bananad-ready");
	open_inputs();
	headphones = headphones_in();
	restore_settings();	/* the backlight */
	audio_find_cards();	/* (the loop's own idea of the cards until the child is done; it writes the route) */
	sound_start();		/* the volumes and the route, in a child (above) */
	int uev = uevent_open();
	int recsock = rec_sock_open();	/* (the recorder's events from banana-ui) */
	sd2_scan();	/* a card already in the second slot at boot */
	load_drivers_present();
	leds_update();
	boot_mark("bananad: buttons live");
	last_input = mono();
	double next_battery = 0;
	int low_count = 0;
	say("running: lid %s, auto-sleep %g min, off after %g min, idle timers %s, %s", cfg.lid_sleep ? "sleeps" : "ignored",
		cfg.autosleep_min, cfg.poweroff_min, cfg.idle_in_games ? "everywhere" : "in the menu", cfg.mem_sleep);

	for (;;) {
		struct pollfd pf[MAXDEV + 3];
		int n = 0;
		for (int i = 0; i < ndev; i++) pf[n++] = (struct pollfd){ devfd[i], POLLIN, 0 };
		int ino_at = -1, uev_at = -1, rs_at = -1;
		if (ino >= 0) { ino_at = n; pf[n++] = (struct pollfd){ ino, POLLIN, 0 }; }
		if (uev >= 0) { uev_at = n; pf[n++] = (struct pollfd){ uev, POLLIN, 0 }; }
		if (recsock >= 0) { rs_at = n; pf[n++] = (struct pollfd){ recsock, POLLIN, 0 }; }
		/* sleep until something is due: the battery look (a minute), the dim or the auto-sleep deadline, the lid
		 * check, a pending save, the power button's hold; nothing wakes bananad otherwise */
		double tnow = mono(), wait = next_battery - tnow;
		int game = pid_of("/run/banana/game.pid") != 0;
		/* a job that must not be slept through (banana-scrape's downloads): its pid in /run/banana/awake.pid; looked
		 * at again every 5 s, so the idle sleep comes once it is over */
		int awake = pid_of("/run/banana/awake.pid") != 0;
		if (awake && wait > 5) wait = 5;
		/* (only deadlines still ahead: one already passed was acted on after the last poll, or is held back by what
		 * the action checks (the charger, the lid, no backlight) and a uevent or an input event brings it back; counted
		 * here it spun this loop every 20 ms, on the charger for as long as the menu sat idle) */
		/* the idle timers: in the menu, or in games too (idle_in_games) */
		int idle_ok = !game || cfg.idle_in_games;
		if (idle_ok) {
			if (cfg.autosleep_min > 0 && !awake) { double d = cfg.autosleep_min * 60.0 - (tnow - last_input); if (d > 0 && d < wait) wait = d; }
			if (cfg.dim_min > 0 && !dimmed) { double d = cfg.dim_min * 60.0 - (tnow - last_input); if (d > 0 && d < wait) wait = d; }
			/* never sleeping: turning off counts from the last input instead */
			if (cfg.poweroff_min > 0 && cfg.autosleep_min <= 0 && !awake) { double d = cfg.poweroff_min * 60.0 - (tnow - last_input); if (d > 0 && d < wait) wait = d; }
			if (dimmed && nquiet && wait > 1) wait = 1;	/* dimmed: a touch brings the panels back within a second */
		}
		/* recording: the touchscreen read every 0.2 s, before its buffer of motion can overflow (a touch's own time is
		 * in its event, so it's recorded when it happened) */
		if (recst.on && nquiet && wait > 0.2) wait = 0.2;
		if (lid_closed && cfg.lid_sleep && !lid_retry_off) { double d = 5.0 - (tnow - last_input); if (d > 0 && d < wait) wait = d; }
		if ((save_volume_at > 0 || save_backlight_at > 0) && wait > 0.5) wait = 0.5;
		if (power_down && wait > 0.1) wait = 0.1;
		if (recst.on && sel_down && !sel_marked && wait > 0.1) wait = 0.1;	/* (Select held: the marker at 2 s) */
		if (sound_child > 0 && wait > 0.05) wait = 0.05;	/* the sound's setup running: its end looked for (the first second or so) */
		int timeout = wait <= 0 ? 20 : (int)(wait * 1000) + 20;
		poll(pf, n, timeout);
		double now = mono();
		sound_poll();
		if (stopping) {	/* a restart or a turn-off from the menu, an update: banana-ui's last events, then the card's copy */
			if (rs_at >= 0) rec_sock_read(recsock);
			rec("bananad", "stopped (the handheld restarts or turns off)");
			rec_flush();
			exit(0);
		}
		if (reload) {
			reload = 0;
			load_conf("/etc/banana/bananad.conf");
			load_conf("/data/banana/bananad.conf");
			load_timezone();
			rec_apply();
			rec("bananad", "settings read again");
			save_volume_at = mono() + 2;	/* the settings screen may have set the volume: keep it as the keys' is kept */
			leds_last = -2;
			leds_update();
			say("settings read again: lid %s, auto-sleep %g min, off after %g min, LEDs %d", cfg.lid_sleep ? "sleeps" : "ignored",
				cfg.autosleep_min, cfg.poweroff_min, cfg.led_brightness);
		}

		for (int i = 0; i < ndev; i++) {
			if (!(pf[i].revents & POLLIN)) continue;
			struct input_event ev[64];
			ssize_t r;
			while ((r = read(devfd[i], ev, sizeof ev)) > 0)
				for (int k = 0; k < r / (ssize_t)sizeof ev[0]; k++) handle(&ev[k]);
		}
		if (uev_at >= 0 && (pf[uev_at].revents & POLLIN) && uevent_read(uev)) { leds_update(); rec_charger(); }
		if (rs_at >= 0 && (pf[rs_at].revents & POLLIN)) rec_sock_read(recsock);
		save_settings(now);
		if (ino_at >= 0 && (pf[ino_at].revents & POLLIN)) {
			char buf[4096];
			while (read(ino, buf, sizeof buf) > 0) {}
			usleep(200000);	/* udev/mdev sets the node's permissions */
			open_inputs();
			/* the jack's input device comes with its sound card, which may probe after bananad starts: the headphones
			 * as they are now, not as they read before the device was there */
			int hp = headphones_in();
			if (hp != headphones) { headphones = hp; if (!sound_child) audio_route(); }
		}

		if (power_down && now - power_since >= cfg.power_long_ms / 1000.0) { power_down = 0; shutdown_now("power held"); }
		if (recst.on && sel_down && !sel_marked && now - sel_since >= 2.0) { sel_marked = 1; rec_mark(); }

		/* idle (in the menu, or in a game too with idle_in_games): the panels dim, then it sleeps, then it turns off
		 * (suspend_now counts the time asleep). A touch or a stick since the last look counts as input */
		game = pid_of("/run/banana/game.pid") != 0;
		int idle = !game || cfg.idle_in_games;
		if ((idle || recst.on) && quiet_scan() && idle) undim();	/* (recording: the touches read in games too) */
		if (recst.on && now >= recst.flush_at) { rec_flush(); recst.flush_at = now + 30; }
		if (cfg.dim_min > 0 && !dimmed && idle && !lid_closed && now - last_input >= cfg.dim_min * 60.0) dim();
		/* (not while charging: a sleep would put the charging light out; the menu stays dimmed instead) */
		if (cfg.autosleep_min > 0 && idle && now - last_input >= cfg.autosleep_min * 60.0 && !charger_online() &&
		    !pid_of("/run/banana/awake.pid"))
			suspend_now("idle");
		/* never sleeping (autosleep_minutes 0): off after poweroff_minutes without input, as a held power button does
		 * (the game saves first); not on the charger */
		if (cfg.poweroff_min > 0 && cfg.autosleep_min <= 0 && idle && now - last_input >= cfg.poweroff_min * 60.0 &&
		    !charger_online() && !pid_of("/run/banana/awake.pid")) {
			sleep_log("idle %.0f min: turning off", (now - last_input) / 60);
			shutdown_now("idle");
		}
		/* started or woken with the lid shut */
		if (lid_closed && cfg.lid_sleep && !lid_retry_off && now - last_input > 5) suspend_now("lid still closed");

		if (now >= next_battery) {
			next_battery = now + 60;
			leds_update();	/* a capacity change has no uevent on every gauge */
			rec_minute();
			int pct = battery_discharging_pct();
			low_count = pct >= 0 && pct <= cfg.battery_off_pct ? low_count + 1 : 0;
			if (low_count >= 2) shutdown_now("battery empty");	/* two readings in a row: not a load spike */
		}
	}
}
