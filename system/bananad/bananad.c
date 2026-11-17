/* bananad: Banana's power daemon for the Anbernic RG DS and RG DS Plus.
 *
 *   - Sleep: a short press of the power button, closing the lid, or (in the menu) a few idle minutes put the
 *     handheld into suspend-to-RAM. The running game is paused first (SIGSTOP to its process group) and resumed
 *     after, the hooks in /etc/banana/sleep.d run around it, and banana-ui is told to draw its panels again.
 *     Opening the lid or pressing power wakes it (the device tree marks both as wakeup sources). A copy, move or
 *     delete of the shelf's Files (/run/banana/files.pid) holds the idle sleep; the lid still sleeps (it pauses). A
 *     download or an unpacking of Files' (banana-fetch's runner, /run/banana/transfer.pid) holds the idle sleep, and
 *     the lid's (the panels go dark): it sleeps once the job is done.
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
 *     Bluetooth headphones or speakers, while connected, come before both: the bluetooth script writes the one
 *     playing to /run/banana/bt/audio (from bluez-alsa's events), bananad watches that folder and plays through
 *     bluez-alsa's ALSA plugin, and back to the speakers or the jack when the file goes.
 *     On the speakers (only: never the headphones or Bluetooth) the sound goes through the speaker EQ, banana-ui's
 *     ALSA plugin banana_eq, which follows the profile Settings writes to /run/banana/speaker-eq (none: untouched).
 *
 * The menu's Sleep: banana-ui writes /run/banana/sleep-now and sends SIGHUP; the power button's sleep follows.
 * Settings: /etc/banana/bananad.conf, then /data/banana/bananad.conf (key = value lines, see bananad.conf); SIGHUP
 * reads them again (the menu's settings screen sends it).
 * It reads the input devices alongside banana-ui and the games (evdev allows many readers) and grabs nothing.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
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
	double autosleep_min;	/* idle minutes in the menu before sleeping; 0 = never */
	int lid_sleep;			/* closing the lid sleeps */
	int lid_start;			/* the power button (or reset) starts it with the lid shut too; no: off again at once */
	int power_long_ms;		/* holding power this long shuts down */
	int battery_off_pct;	/* at or under this, discharging: shut down */
	char mem_sleep[16];		/* deep (suspend to RAM) or s2idle */
	int led_brightness;		/* 0..255; 0: the LEDs stay off */
	int led_low, led_critical;	/* percent: red, blinking red */
	int fast_resume;		/* devices resume in parallel (/sys/power/pm_async) */
	double sleep_check_hours;	/* asleep: wake this often to look at the battery (0: never) */
	int sleep_poweroff_pct;	/* found at or under this while asleep: shut down cleanly */
	double dim_min;			/* idle minutes in the menu before the panels dim (0: never) */
	int dim_pct;			/* dimmed to this percent of their brightness */
	int mic_gain;			/* the rk817's microphone preamp, 0..15 (-1: left as it is) */
	double poweroff_after_min;	/* asleep this long on the battery: the game saved, then a clean shutdown (0: never) */
	int recorder;			/* the flight recorder (Developer options): everything bananad sees, written to banana/logs/sessions */
} cfg = { "auto", 5, 8, 10, 1, 0, 2000, 3, "deep", 128, 20, 5, 1, 4, 5, 2, 30, 12, 0, 0 };

static int devfd[MAXDEV], ndev;
static void leds_update(void);
static int amber_last, leds_asleep;	/* (leds_update's, below: suspend_now sets the charging light asleep) */
static void audio_route(void);
static pid_t sound_child;	/* (the sound's setup at start, sound_start) */
static void undim(void);
static void find_bottom_bl(void);
static char bottom_bl[64];	/* the bottom panel's backlight name; banana-ui's idle dim owns it while it runs */
static int headphones;
static int fn_held, power_down, lid_closed;
static int lid_retry_off;	/* a suspend failed with the lid shut: no retry until it opens */
static double lid_sleep_at;	/* the lid shut in a DS game: the sleep a moment later, once the game has seen it */
static double menu_sleep_at;	/* the menu's Sleep (banana-ui wrote /run/banana/sleep-now and sent SIGHUP): a moment later,
				 * once the button that chose it is let go */
static double power_since, last_input, ignore_power_until;
static int sel_down, sel_marked;	/* Select held: the recorder's marker at 2 s (once a hold) */
static double sel_since;

static volatile sig_atomic_t reload, stopping;
static void on_hup(int sig) { (void)sig; reload = 1; }
static volatile sig_atomic_t in_poll;	/* the main loop's poll: a TERM there is taken by the loop (the recording written out) */
/* rcK's: the recording written out, then gone. Anywhere but the main loop's poll (a sleep's or a shutdown's own waits,
 * which may not come back to the loop) at once, as before the recorder */
static void on_term(int sig) { (void)sig; stopping = 1; if (!in_poll) _exit(0); }

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
		else if (!strcmp(k, "lid_start")) cfg.lid_start = !strcmp(v, "yes") || !strcmp(v, "1");
		else if (!strcmp(k, "mic_gain")) cfg.mic_gain = atoi(v);
		else if (!strcmp(k, "power_long_ms")) cfg.power_long_ms = atoi(v);
		else if (!strcmp(k, "battery_shutdown_percent")) cfg.battery_off_pct = atoi(v);
		else if (!strcmp(k, "mem_sleep")) snprintf(cfg.mem_sleep, sizeof cfg.mem_sleep, "%s", v);
		else if (!strcmp(k, "led_brightness")) cfg.led_brightness = atoi(v);
		else if (!strcmp(k, "led_low_percent")) cfg.led_low = atoi(v);
		else if (!strcmp(k, "led_critical_percent")) cfg.led_critical = atoi(v);
		else if (!strcmp(k, "fast_resume")) cfg.fast_resume = !strcmp(v, "yes") || !strcmp(v, "1");
		else if (!strcmp(k, "sleep_check_hours")) cfg.sleep_check_hours = atof(v);
		else if (!strcmp(k, "sleep_poweroff_percent")) cfg.sleep_poweroff_pct = atoi(v);
		else if (!strcmp(k, "dim_minutes")) cfg.dim_min = atof(v);
		else if (!strcmp(k, "dim_percent")) cfg.dim_pct = atoi(v);
		else if (!strcmp(k, "poweroff_after_sleep_minutes")) cfg.poweroff_after_min = atof(v);
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

/* the pid in a pid file, when that process is still the one the file names: the file may hold "<pid> <starttime>"
 * (banana-ui's game.pid), and a pid whose process started at another time is a reused one; without it, the pid */
static pid_t pid_of(const char *pidfile)
{
	FILE *f = fopen(pidfile, "r");
	if (!f) return 0;
	int p = 0;
	unsigned long long started = 0;
	int got = fscanf(f, "%d %llu", &p, &started);
	fclose(f);
	if (got < 1 || p <= 0 || kill(p, 0) != 0) return 0;
	/* (no file-time test: /proc/<pid>'s times are when its inode was first looked up, not the process's start, so a
	 * live game looked "newer" than its pid file; only the start time in the file can tell a reused pid) */
	if (!started) return p;
	char path[32], buf[1024];
	snprintf(path, sizeof path, "/proc/%d/stat", p);
	f = fopen(path, "r");
	if (!f || !fgets(buf, sizeof buf, f)) { if (f) fclose(f); return 0; }
	fclose(f);
	char *q = strrchr(buf, ')');
	if (!q) return 0;
	q += 2;
	for (int i = 0; i < 19 && *q; i++) { while (*q && *q != ' ') q++; while (*q == ' ') q++; }
	return strtoull(q, NULL, 10) == started ? p : 0;
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
/* shutting down (#46): shutdown_now started, or rcK runs (a shutdown from the menu or over SSH writes
 * /run/banana/powering-off first). No sleep from then on: the lid still shut while the game saves and the card is
 * unmounted put the handheld to sleep instead, found asleep hours later */
static int shutting_down;
static int powering_off(void) { return shutting_down || access("/run/banana/powering-off", F_OK) == 0; }
static int sleep_fallback;		/* 0 as configured; 1 devices one at a time; 2 also s2idle (a suspend failed) */
/* (the fallback to s2idle is for the one sleep that needed it: the next sleep, and a sleep again after the battery
 * check, try deep again. Both handhelds' xhci-hcd fails a suspend now and then ("Clearing Run/Stop bit failed",
 * -ETIMEDOUT), with the devices one at a time too, and two of those in one boot used to leave every later sleep in
 * s2idle until a reboot, which drains the battery more) */

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
	case BTN_SOUTH: return "b"; case BTN_EAST: return "a";	/* (the RG DS pad: A is BTN_EAST, B BTN_SOUTH, as banana-ui reads them) */ case BTN_NORTH: return "x"; case BTN_WEST: return "y";
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

/* a process's state, the letter after its name in /proc/<pid>/stat (R, S, T for stopped, Z...); 0 unknown */
static char proc_state(pid_t pid)
{
	char path[64], line[512];
	snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
	read_str(path, line, sizeof line);
	const char *name_end = strrchr(line, ')');	/* (the name may hold spaces and parentheses) */
	return name_end && name_end[1] == ' ' ? name_end[2] : 0;
}

/* a download or an unpacking of Files' (banana-fetch's runner, its pid in /run/banana/transfer.pid) with no game
 * running: the lid shut makes the panels dark, and the sleep waits until it is done (the "lid still closed" check then
 * sleeps). During a game the runner is stopped anyway, and the lid sleeps as ever; the power button always does. A
 * runner left stopped (banana-ui went away during a game and didn't let it go on) or ended unreaped is no job either:
 * it would hold the sleep until a reboot. */
static int lid_held;	/* the lid was shut during a job: dark and awake */
static int transfer_hold(void)
{
	pid_t p = pid_of("/run/banana/transfer.pid");
	if (!p || pid_of("/run/banana/game.pid")) return 0;
	char st = proc_state(p);
	return st != 'T' && st != 't' && st != 'Z';
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
/* ---- Power off after sleeping (poweroff_after_sleep_minutes) -------------------------------------------------------
 * Asleep that long on the battery, the RTC wakes the handheld quietly (the panels stay dark, no hooks, nothing for the
 * menu), the game saves a resume state and quits, and it shuts down cleanly; /data/banana/resume-boot then names the
 * game, which banana-ui starts again once at the next boot with its resume state loaded (the launchers' resume-once).
 * The lid can't switch it back on: the hall sensor is the SoC's GPIO0_C3 (gpio-keys-hall), which is unpowered once the
 * RK817 has shut down, and the RK817 powers up only from its own PWRON key (and a charger). Power starts it again. */

/* the pid of the process in process group pg named comm (/proc/<pid>/stat), 0 if none */
static pid_t pgrp_member(pid_t pg, const char *comm)
{
	DIR *d = opendir("/proc");
	if (!d) return 0;
	struct dirent *e;
	pid_t r = 0;
	while (!r && (e = readdir(d))) {
		if (!isdigit((unsigned char)e->d_name[0])) continue;
		char path[300], st[512];
		snprintf(path, sizeof path, "/proc/%s/stat", e->d_name);
		read_str(path, st, sizeof st);
		/* "pid (comm) state ppid pgrp ...": the name may hold spaces and parentheses, so from the last ')' */
		char *open = strchr(st, '('), *close = strrchr(st, ')');
		if (!open || !close || close < open) continue;
		int pgrp = 0, ppid;
		char state;
		if (sscanf(close + 1, " %c %d %d", &state, &ppid, &pgrp) != 3 || pgrp != pg) continue;
		*close = 0;
		if (!strcmp(open + 1, comm)) r = atoi(e->d_name);
	}
	closedir(d);
	return r;
}

/* the game's ROM, as banana-ui gave it to the launcher (BANANA_ROM in its environment); "" if none */
static void game_rom(pid_t game, char *out, size_t n)
{
	out[0] = 0;
	char path[64], env[16384];
	snprintf(path, sizeof path, "/proc/%d/environ", (int)game);
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return;
	ssize_t len = read(fd, env, sizeof env - 1);
	close(fd);
	if (len <= 0) return;
	env[len] = 0;
	for (char *p = env; p < env + len; p += strlen(p) + 1)
		if (!strncmp(p, "BANANA_ROM=", 11)) { snprintf(out, n, "%s", p + 11); return; }
}

/* the resume state the DS launchers name for rom (run-drastic, run-bananads): <dir>/<file name without its
 * extension><ext>; "" for another emulator (RetroArch's QUIT waits for its own save, so it isn't checked) */
/* BananaFront's automatic state (RetroArch's name: <state_dir>/<rom_base>.state.auto), from the game-id file it
 * writes at the start (COMPANION-SPEC.md 2; read before the game is told to quit: it removes the file on exit) */
static char bf_resume[1300];
static void bf_resume_path(void)
{
	char dir[600] = "", base[600] = "", ln[600];
	bf_resume[0] = 0;
	FILE *f = fopen("/run/banana/game-id", "r");
	if (!f) return;
	while (fgets(ln, sizeof ln, f)) {
		ln[strcspn(ln, "\n")] = 0;
		if (!strncmp(ln, "state_dir=", 10)) snprintf(dir, sizeof dir, "%s", ln + 10);
		else if (!strncmp(ln, "rom_base=", 9)) snprintf(base, sizeof base, "%s", ln + 9);
	}
	fclose(f);
	if (dir[0] && base[0]) snprintf(bf_resume, sizeof bf_resume, "%s/%s.state.auto", dir, base);
}

static void resume_state_path(const char *emu, const char *rom, char *out, size_t n)
{
	out[0] = 0;
	const char *dir, *ext;
	if (!strcmp(emu, "superdrastic")) dir = "/data/drastic/savestates", ext = ".resume.dss";
	else if (!strcmp(emu, "bananads")) dir = "/data/states/nds", ext = ".resume.mst";
	else if (!strcmp(emu, "bananafront")) { snprintf(out, n, "%s", bf_resume); return; }
	else return;
	const char *b = strrchr(rom, '/');
	b = b ? b + 1 : rom;
	const char *dot = strrchr(b, '.');
	int len = dot && dot != b ? (int)(dot - b) : (int)strlen(b);
	snprintf(out, n, "%s/%.*s%s", dir, len, b, ext);
}

/* a command for RetroArch on its stdin (run-retroarch's pipe, or banana-ui's for the game panel); 0: the pipe is
 * gone (the game exited) or full for 100 ms */
static int ra_command(const char *c)
{
	int fd = open("/run/banana/retroarch.cmd", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) return 0;
	size_t n = strlen(c), at = 0;
	while (at < n) {
		ssize_t w = write(fd, c + at, n - at);
		if (w > 0) { at += (size_t)w; continue; }
		if (w < 0 && errno == EINTR) continue;
		if (w < 0 && errno == EAGAIN) {
			struct pollfd p = { fd, POLLOUT, 0 };
			if (poll(&p, 1, 100) > 0) continue;
		}
		close(fd);
		return 0;
	}
	close(fd);
	return 1;
}

/* L2, R2, L3 or R3 pressed alone in a RetroArch game: its job's command (Settings > Other systems > Buttons > the system;
 * run-retroarch's /run/banana/ra-buttons, "<button> <command>" lines, there only while a game it applies to runs) */
static void ra_button(const char *name)	/* (c: a command of up to 38, the newline and its end fit) */
{
	FILE *f = fopen("/run/banana/ra-buttons", "r");
	if (!f) return;
	char line[64], b[8], c[40];
	while (fgets(line, sizeof line, f))
		if (sscanf(line, "%7s %38s", b, c) == 2 && !strcmp(b, name)) {
			if (strcmp(c, "-")) { strcat(c, "\n"); ra_command(c); }
			break;
		}
	fclose(f);
}

/* waits up to secs for the game to quit, the backlights kept off meanwhile (a display handed back, or banana-ui's
 * modeset, can switch a panel's backlight on again); 1 when it has */
static int game_gone(pid_t game, double secs)
{
	for (double t0 = mono(); mono() - t0 < secs; usleep(100000)) {
		if (kill(game, 0) != 0) return 1;
		backlights_power(0);
	}
	return kill(game, 0) != 0;
}

static void sleep_poweroff(pid_t game, pid_t pg, double asleep)
{
	sleep_log("asleep %.0f min: the game saved, then shutting down (poweroff_after_sleep_minutes)", asleep / 60);
	backlights_power(0);
	if (game) {
		/* quiet: the game runs again for a moment to save. The levels kept first (the next boot restores them), then
		 * the volume down; not Bluetooth headphones, whose own volume would stay down */
		char mixer[64];
		read_str("/run/banana/mixer", mixer, sizeof mixer);
		if (strncmp(mixer, "bluealsa", 8)) {
			char *store[] = { "alsactl", "-f", "/data/banana/asound.state", "store", NULL };
			run(store);
			char *mute[] = { "/usr/libexec/banana/volume", "set", "0", NULL };
			run(mute);
		}
		char rom[1024];
		game_rom(game, rom, sizeof rom);
		const char *emu = NULL;
		pid_t p, grp = pg > 0 ? -pg : game;
		double wait = 60;	/* (the emulators' own limits are shorter: never cut a save short) */
		struct timespec asked;	/* the resume state must be newer than this to count as this save's */
		clock_gettime(CLOCK_REALTIME, &asked);
		asked.tv_sec -= 1;	/* (a filesystem's timestamps may be coarser than the clock) */
		if (pg > 0 && (p = pgrp_member(pg, "drastic"))) {
			/* SuperDrastic: SIGUSR1 saves the resume state (DSFLIP_RESUME_FILE, which run-drastic always sets) and
			 * quits; sent while stopped, it is the first thing the game does on SIGCONT */
			emu = "superdrastic";
			kill(p, SIGUSR1);
			kill(grp, SIGCONT);
		} else if (pg > 0 && (p = pgrp_member(pg, "bananads"))) {
			/* BananaDS: SIGUSR1 quits with the resume state written whatever resume.on_quit says, then the save */
			emu = "bananads";
			kill(p, SIGUSR1);
			kill(grp, SIGCONT);
		} else if (pg > 0 && (p = pgrp_member(pg, "bananafront"))) {
			/* BananaFront: SIGUSR1 quits with the automatic state written (RetroArch's .state.auto, the one Quick
			 * resume loads) whatever the setting, then the save */
			emu = "bananafront";
			bf_resume_path();
			kill(p, SIGUSR1);
			kill(grp, SIGCONT);
		} else if (pg > 0 && (p = pgrp_member(pg, "retroarch"))) {
			/* RetroArch: a state in its automatic slot (-1: the slot down past 0, from wherever it is; the one
			 * savestate_auto_load reads), then QUIT, which waits for the save task to finish. run-retroarch puts
			 * the slot back after. No state in Hardcore: then it only quits (its SRAM is written on the way). */
			emu = "retroarch";
			kill(grp, SIGCONT);
			usleep(500000);	/* (the pipe is read once a frame: running first) */
			int slot = 0;
			FILE *f = fopen("/data/retroarch/retroarch.cfg", "r");
			if (f) {
				char ln[256];
				while (fgets(ln, sizeof ln, f)) sscanf(ln, "state_slot = \"%d\"", &slot);
				fclose(f);
			}
			if (slot < 0) slot = 0;
			if (slot > 997) slot = 997;
			int ok = 1;
			for (int i = 0; i < slot + 2 && ok; i++) { ok = ra_command("STATE_SLOT_MINUS\n"); usleep(100000); }	/* (one a frame or more: each is a press) */
			if (ok) { ok = ra_command("SAVE_STATE\n"); usleep(500000); }
			if (ok) ra_command("QUIT\n");
			else {
				/* no state asked for (RetroArch gone, or not reading its pipe): nothing of this save to come back to,
				 * and a resume-boot would load an older automatic state once at the next boot as if it were this one */
				sleep_log("RetroArch's command pipe closed: no resume state saved");
				emu = NULL;
			}
		} else {
			/* the rest (PortMaster ports, PICO-8): SIGTERM, as a shutdown; no resume state to come back to */
			kill(grp, SIGCONT);
			kill(grp, SIGTERM);
			wait = 10;
		}
		if (game_gone(game, wait)) {
			/* SuperDrastic quits after 5 s whether or not DraStic wrote the state, and BananaDS when it can't
			 * write it: then an older resume state (or none) is all there is, and the next boot mustn't start the
			 * game from it as if it were this one (review 0.4.3: resume-boot was written either way) */
			char rs[1200];
			struct stat st;
			if (emu && rom[0]) resume_state_path(emu, rom, rs, sizeof rs); else rs[0] = 0;
			if (rs[0] && (stat(rs, &st) || st.st_mtim.tv_sec < asked.tv_sec)) {
				sleep_log("%s quit without a new resume state (%s): not started again at the next boot", emu, rs);
				emu = NULL;
			}
			if (emu && rom[0]) {
				FILE *f = fopen("/data/banana/resume-boot.new", "w");
				if (f) {
					fprintf(f, "rom=%s\nemulator=%s\n", rom, emu);
					fflush(f);
					fsync(fileno(f));
					fclose(f);
					rename("/data/banana/resume-boot.new", "/data/banana/resume-boot");
				}
				sleep_log("%s saved %s: it starts again at the next boot", emu, rom);
			}
		} else sleep_log("the game hasn't quit after %.0f s: shutting down anyway", wait);
	}
	backlights_power(0);
	shutdown_now("asleep too long");
	for (;;) pause();	/* init is shutting down: nothing more to do (not a sleep again with the lid shut) */
}

static void suspend_now(const char *why)
{
	if (powering_off()) { static int told; if (!told++) say("no sleep (%s): shutting down", why); return; }
	double t0 = mono();
	/* the panels dark first: the pre-sleep hooks (Wi-Fi down and its driver unloaded, Bluetooth down) take ~2 s on the
	 * RG DS Plus, and the panels stayed lit that long with the lid shut. The wake lights them again (below: the lid
	 * open) */
	backlights_power(0);
	lid_held = 0;
	pid_t game = pid_of("/run/banana/game.pid");
	pid_t pg = game ? getpgid(game) : 0;
	if (pg > 0) kill(-pg, SIGSTOP);	/* DraStic's dynarec mustn't run on while the clocks drop (ROCKNIX's finding) */
	hooks("pre", 0);
	sync();
	if (sleep_fallback > 1) sleep_fallback = 1;	/* deep again (devices one at a time), see sleep_fallback */
	sleep_mode();
	int cycles = 0, rtc_wakes = 0, failures = 0;
	double asleep = 0;
	const char *woke = "?";
	/* the charging light at full while asleep: a PWM LED keeps shining through a suspend only at 100% (its
	 * retain-state-suspended, 2-linux-0029), so it stays lit on the charger as a phone's does */
	leds_asleep = 1; amber_last = -2; leds_update();
	/* the lid's sleeps ("lid closed", "lid still closed", "lid closed, the download done"): the lid looked at again
	 * before each suspend, below */
	int by_lid = !strncmp(why, "lid", 3);
	/* Power off after sleeping: the wall clock's deadline (it goes on while asleep), from the first real sleep */
	time_t off_at = 0;
	for (;;) {
		/* a shutdown begun meanwhile (rcK, while awake for a look or the battery check): awake, so the
		 * game, paused, gets its SIGTERM and writes its saves */
		if (powering_off()) { woke = "shutting down"; break; }
		/* the charging light as the cable is now, before each suspend: plugged in or pulled out while asleep woke it (the
		 * RK817's interrupt), and the light follows at once; full, out at the 10-minute look */
		leds_update();
		/* the RTC alarm: the sooner of the battery check and the deadline (passed: woken for it, or anything else
		 * after it with the lid shut, and the game is saved and the handheld shut down) */
		/* (on the charger: no Power off after sleeping, as a phone sleeps on while plugged in; unplugged, the deadline
		 * starts at the next look) */
		if (charger_online()) off_at = 0;
		else if (cfg.poweroff_after_min > 0 && !off_at) off_at = time(NULL) + (time_t)(cfg.poweroff_after_min * 60 + 0.5);
		if (off_at && time(NULL) >= off_at) {
			sleep_poweroff(game, pg, asleep);
			return;
		}
		/* a sleep the lid started: the lid as it is now, just before the suspend. Opened again while the hooks took
		 * Wi-Fi down (a second or more), its edge has gone by when the SoC suspends, and nothing but the power button
		 * would wake it, the panels going dark under the user's eyes; its event is drained after the wake, so it is
		 * read here. Open: awake as from a sleep (the game goes on, the post hooks bring Wi-Fi back) */
		if (by_lid) {
			read_lid();
			if (!lid_closed) {
				sleep_log("lid opened again before the suspend (%s): awake", why);
				woke = "lid opened before the suspend";
				break;
			}
		}
		int secs = (int)(cfg.sleep_check_hours * 3600);
		if (off_at && (secs <= 0 || off_at - time(NULL) < secs)) secs = (int)(off_at - time(NULL));
		/* on the charger: a look every 10 minutes for the charging light (out once the battery is full) and the cable
		 * (its coming out wakes the handheld too: the RK817's interrupt, 5-mainline-rockchip-010) */
		if (charger_online()) secs = secs > 0 && secs < 600 ? secs : 600;
		time_t alarm = rtc_alarm(secs);
		double t_in = mono();
		struct timespec b0, b1;
		clock_gettime(CLOCK_BOOTTIME, &b0);	/* BOOTTIME counts the time asleep, MONOTONIC doesn't */
		if (cycles == 0) sleep_log("sleep (%s): %.0f ms to suspend%s", why, (t_in - t0) * 1000, game ? ", game paused" : "");
		rec_flush();	/* (the recording on the card before the sleep) */
		int r = write_file("/sys/power/state", "mem");
		int err = errno;
		clock_gettime(CLOCK_BOOTTIME, &b1);
		/* the RTC clears wakealarm when the alarm fires: set before, empty now, is the battery check's wake */
		char pending[32];
		read_str("/sys/class/rtc/rtc0/wakealarm", pending, sizeof pending);
		int rtc_fired = alarm && (!pending[0] || time(NULL) >= alarm);
		rtc_alarm(0);
		cycles++;
		if (r) {
			/* it didn't suspend: devices one at a time, then the lighter s2idle, then give up and blank the panels.
			 * Already one at a time (an earlier sleep failed): that once more before s2idle */
			failures++;
			int again = sleep_fallback < 2 && failures <= 2, safer = sleep_fallback == 0 || failures >= 2;
			sleep_log("suspend failed (%s)%s", strerror(err), !again ? "" : safer ? ": trying the safer way" : ": trying again");
			if (again) { if (safer) sleep_fallback++; sleep_mode(); usleep(200000); continue; }
			woke = "failed";
			if (lid_closed) backlights_power(0);	/* can't sleep: at least dark until the lid opens */
			/* ...and not tried again every 5 s with the lid still shut. (Only with it shut: set after a failed sleep by
			 * the power button, lid open, nothing took it back - the lid's opening after a sleep is read below, not
			 * as an event - and "lid still closed" never slept again: a lid shut during a download stayed awake) */
			if (lid_closed) lid_retry_off = 1;
			break;
		}
		asleep += (b1.tv_sec - b0.tv_sec) + (b1.tv_nsec - b0.tv_nsec) / 1e9;
		failures = 0;	/* it slept: a sleep again (the battery check, the lid still shut) starts from deep */
		if (sleep_fallback > 1) { sleep_fallback = 1; sleep_mode(); }
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
			continue;	/* enough battery: back to sleep at once */
		}
		read_lid();
		if (lid_closed && cfg.lid_sleep) { woke = "lid shut"; continue; }	/* not meant: back to sleep */
		woke = lid_closed ? "power button" : "lid opened";
		break;
	}
	/* awake: the game and the menu first */
	double t_wake = mono();
	/* the panels on when the lid is open (dark too from a lid shut with lid_sleep off before a power-button sleep:
	 * the lid-open event that would light them is drained below) */
	if (!lid_closed) backlights_power(1);
	if (pg > 0) kill(-pg, SIGCONT);
	pid_t ui = ui_pid();
	if (ui && !game) kill(ui, SIGUSR1);	/* draws both panels again (the clock and the battery moved on) */
	double t_back = mono();
	undim();
	hooks("post", 1);
	panel_off_again();
	/* the press that woke us comes in now: it is not a new request. Draining also drops the lid-open event, so the
	 * lid's state is read from the kernel instead */
	drain_inputs();
	read_lid();
	if (!lid_closed) lid_retry_off = 0;	/* (the lid's opening was drained above: what its event would have done) */
	/* a sleep the menu asked for half a second before this one began (Power > Sleep, then the lid shut at once) is
	 * this sleep: left pending, it put the handheld back to sleep the moment the lid opened */
	menu_sleep_at = 0;
	leds_asleep = 0; amber_last = -2; leds_update();	/* (the charging light back at the user's level) */
	if (headphones_in() != headphones) { headphones = !headphones; if (!sound_child) audio_route(); }	/* (else sound_poll routes) */
	ignore_power_until = mono() + 1.0;
	power_down = 0;
	last_input = mono();
	sleep_log("awake (%s) after %.0f s asleep%s%s, game and menu back %.1f ms after the kernel", woke, asleep,
		rtc_wakes ? ", battery checked" : "", cycles - rtc_wakes > 1 ? ", slept again with the lid shut" : "",
		(t_back - t_wake) * 1000);
}

static void shutdown_now(const char *why)
{
	say("shutting down (%s)", why);
	shutting_down = 1;
	rec("power", "shutting down (%s)", why);
	rec_flush();
	pid_t game = pid_of("/run/banana/game.pid");
	if (game) {
		pid_t pg = getpgid(game);
		kill(pg > 0 ? -pg : game, SIGTERM);	/* let the emulator write its saves */
		for (int i = 0; i < 50 && kill(game, 0) == 0; i++) usleep(100000);
	}
	sync();
	close(open("/run/banana/poweroff", O_WRONLY | O_CREAT | O_CLOEXEC, 0644));	/* (rcK: off, not a restart) */
	/* busybox init runs /etc/init.d/rcK (unmount, sync) on SIGUSR2 = poweroff */
	if (kill(1, SIGUSR2)) reboot(RB_POWER_OFF);
}

static double save_volume_at, save_backlight_at;	/* debounced: written a moment after the last key */

/* ---- sound: which card plays ----------------------------------------------------------------------------------- */
static int card_codec = -1, card_speaker = -1, card_play = 0;	/* rk817; aw88166 (RG DS Plus only); the one playing */
#define BT_AUDIO_DIR "/run/banana/bt"
static char bt_mac[18], bt_pcm[160];	/* Bluetooth headphones playing now ("" none): their address, bluez-alsa's PCM */

/* the bluetooth script's /run/banana/bt/audio: "<MAC> <PCM path>" while A2DP headphones are connected */
static int bt_audio_read(void)
{
	char mac[32] = "", pcm[160] = "";
	FILE *f = fopen(BT_AUDIO_DIR "/audio", "r");
	if (f) {
		if (fscanf(f, "%31s %159s", mac, pcm) != 2) mac[0] = pcm[0] = 0;
		fclose(f);
	}
	/* AA:BB:CC:DD:EE:FF, and a path with nothing ALSA's configuration would read as its own */
	int ok = strlen(mac) == 17 && strncmp(pcm, "/org/bluealsa/", 14) == 0;
	for (int i = 0; ok && i < 17; i++) ok = i % 3 == 2 ? mac[i] == ':' : isxdigit((unsigned char)mac[i]);
	for (char *c = pcm; ok && *c; c++) ok = isalnum((unsigned char)*c) || *c == '/' || *c == '_';
	if (!ok) mac[0] = pcm[0] = 0;
	snprintf(bt_mac, sizeof bt_mac, "%s", mac);
	snprintf(bt_pcm, sizeof bt_pcm, "%s", pcm);
	return bt_mac[0] != 0;
}

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
	int bt = bt_audio_read();
	/* Bluetooth headphones being called back (after a wake, or a codec change: the bluetooth script's bt/hold) and
	 * not back yet: the codec with no speaker on it (the RG DS's amplifier switched off; on the Plus the rk817 drives
	 * only the jack), so a game plays on at its pace, through a real device's clock, and nothing comes out loud */
	int hold = !bt && access(BT_AUDIO_DIR "/hold", F_OK) == 0;
	card_play = card_speaker >= 0 && !headphones && !hold ? card_speaker : card_codec;
	rec("audio", "sound to the %s", card_play < 0 ? "null device (no card)" : card_play == card_speaker ? "speakers" : headphones ? "headphones" : "codec");
	/* the RG DS: one card, whose HP output feeds both the jack and the speaker amplifier; the amplifier is the card's
	 * "Internal Speakers Switch" (a pin switch), off while headphones are in, or the speakers play along (#13) */
	if (card_speaker < 0 && card_codec >= 0) {
		char c[16];
		snprintf(c, sizeof c, "%d", card_codec);
		char *argv[] = { "amixer", "-q", "-c", c, "cset", "name=Internal Speakers Switch", headphones || hold ? "off" : "on", NULL };
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
	char play[256], cap[160];
	/* the speakers, through the speaker EQ (banana-ui's ALSA plugin, ui/src/eq): a tiny speaker's peaks and dips
	 * evened out, in each program's own audio thread, after plug has made the sound S16 stereo 48 kHz. It reads
	 * /run/banana/speaker-eq, which Settings writes, every 10 ms: a change is heard at once, and no
	 * file passes the sound through untouched. Never the headphones or Bluetooth: the profile is the speakers'.
	 * (A build without the plugin, or one missing from the image: the plain route, no EQ.) The plugin is named by its
	 * path in the config, so a program with its own libasound (a port's, whose plugin folder may be elsewhere) finds
	 * it too instead of failing to open the speakers. */
	static const char eq_lib[] = "/usr/lib/alsa-lib/libasound_module_pcm_banana_eq.so";
	const int speakers = !bt && !hold && card_play >= 0 && (card_play == card_speaker || (card_speaker < 0 && !headphones));
	const int eq = speakers && access(eq_lib, R_OK) == 0;
	/* Bluetooth headphones first: bluez-alsa's PCM plugin (A2DP), plug converting the rest; the microphone stays the
	 * rk817's */
	if (bt)
		snprintf(play, sizeof play, "{ type plug slave.pcm { type bluealsa device \"%s\" profile \"a2dp\" } }", bt_mac);
	else if (eq)
		snprintf(play, sizeof play, "{ type plug slave { pcm { type banana_eq slave.pcm \"hw:%d\" } format S16_LE channels 2 rate 48000 } }", card_play);
	else if (card_play >= 0)
		snprintf(play, sizeof play, "{ type plug slave { pcm \"hw:%d\" format S16_LE channels 2 rate 48000 } }", card_play);
	else
		snprintf(play, sizeof play, "\"null\"");
	if (card_codec >= 0)
		/* (route_policy copy: a program that records one channel - the DS emulators' microphone - gets the left one,
		 * where the microphone is, as it is. The plug's own way is the two channels' average: half the level when the
		 * right one is silent, and nothing at all when it is the left one's mirror, as a differential input's can
		 * be: "the game only hears me when I shout", #14) */
		snprintf(cap, sizeof cap, "{ type plug route_policy copy slave { pcm \"hw:%d\" format S16_LE channels 2 rate 48000 } }", card_codec);
	else
		snprintf(cap, sizeof cap, "\"null\"");
	const char *where = bt ? "Bluetooth headphones" : hold ? "nowhere: Bluetooth headphones being called back"
		: eq ? "speakers, through the speaker EQ" : card_play == card_speaker ? "speakers" : headphones ? "headphones" : "the codec";
	char conf[768];
	int n = snprintf(conf, sizeof conf,
		"# bananad: where sound plays now (%s)\n"
		"%s%s%s"
		"pcm.!default { type asym playback.pcm %s capture.pcm %s }\n",
		where, eq ? "pcm_type.banana_eq { lib \"" : "", eq ? eq_lib : "", eq ? "\" }\n" : "", play, cap);
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
		if (bt) fprintf(f, "bluealsa %s\n", bt_pcm);	/* (the volume script: the headphones' own volume) */
		else fprintf(f, "%d %s\n", card_play, strcmp(cfg.mixer, "auto") ? cfg.mixer : card_play == card_speaker ? "PCM" : "Master");
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
	if (bt) say("sound: playback on Bluetooth %s, recording on card %d (48 kHz)", bt_mac, card_codec);
	else say("sound: playback on card %d (%s), recording on card %d (both 48 kHz, the shared DAI's one rate)", card_play,
	    where, card_codec);
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
	/* the microphone (#14): the rk817's "Master" is the DAC's volume AND the ADC's, and the volume keys set both, so
	 * with the volume down the games heard next to nothing (a card saved at volume 0 recorded at -95 dB, at 43% at
	 * -54 dB). The ADC at 0 dB always (the volume script changes playback only now), and the preamp at mic_gain: a
	 * word from 30 cm reached an RMS of 0.01 at the old 6, 0.08 at 12 (the room's noise 0.012), where the games' and
	 * BananaDS's thresholds expect a voice */
	char *cap[] = { "amixer", "-q", "-c", c, "cset", "name=Master Capture Volume", "255,255", NULL };
	run(cap);
	if (cfg.mic_gain >= 0 && cfg.mic_gain <= 15) {
		char g[8];
		snprintf(g, sizeof g, "%d", cfg.mic_gain);
		char *gain[] = { "amixer", "-q", "-c", c, "sset", "Mic Capture Gain", g, NULL };
		run(gain);
	}
	audio_route();
}

/* ---- the sound at start, without holding up the buttons (#87) ------------------------------------------------
 * alsactl's restore and audio_setup's amixers are programs, and at boot they load from the squashfs while the menu
 * and the GPU's and Wi-Fi's modules come up too: run before the loop, they kept the power and volume keys deaf for
 * the first seconds of the boot animation. A child does them now while the loop takes the buttons; volume steps
 * pressed meanwhile wait for it (a restore after a press would undo the press). (Closed PR #86's, without its wait
 * for the sound in rcS.) */
static int sound_ready, volume_pending;

/* a boot milestone in /run/banana/boot-times, as rcS's mark() writes them (logs/boot.txt) */
static void boot_mark(const char *what)
{
	char up[32] = "";
	read_str("/proc/uptime", up, sizeof up);
	FILE *f = fopen("/run/banana/boot-times", "a");
	if (f) { fprintf(f, "%-28s %6.2f s\n", what, atof(up)); fclose(f); }
}

/* the saved volumes first, then the route: the restore also brings back the speakers' switch as it was last saved
 * (off if headphones were in then), and the route sets it from the jack as it is now */
static void sound_setup(void)
{
	if (access("/data/banana/asound.state", R_OK) == 0) {
		char *argv[] = { "alsactl", "-f", "/data/banana/asound.state", "restore", NULL };
		run(argv);
	}
	audio_setup();
}

static void sound_start(void)
{
	pid_t p = fork();
	if (p == 0) { sound_setup(); _exit(0); }
	if (p < 0) { sound_setup(); sound_ready = 1; return; }	/* (no fork: in line, as before) */
	sound_child = p;
}

static void volume(int dir);
/* the loop's look at the child: done, the route once more from here (the jack, Bluetooth, the cards as this process
 * knows them, whatever changed while it ran), then the volume steps that waited */
static void sound_poll(void)
{
	if (sound_child <= 0) return;
	pid_t r = waitpid(sound_child, NULL, WNOHANG);
	if (r == 0 || (r < 0 && errno == EINTR)) return;	/* still running (any other answer: it is gone, go on) */
	sound_child = 0;
	sound_ready = 1;
	audio_find_cards();
	audio_route();
	boot_mark("bananad: sound ready");
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

/* The two panels' backlights, each its own level (N2): backlight1 lights the top panel (dsi1, DSI-2), backlight0 the
 * bottom one (dsi0, DSI-1), as the device trees of both handhelds have it. The level kept in /data/banana/backlight is
 * "<top> <bottom>" in thousandths of the maximum; a file with one number (before 0.4.2) is both. */
static const char *BL[2] = { "backlight1", "backlight0" };	/* top, bottom */
static void backlight_set_one(int i, int pm)
{
	char p[96], v[32];
	snprintf(p, sizeof p, "/sys/class/backlight/%s/max_brightness", BL[i]);
	int max = read_int(p, 0);
	if (max <= 0) return;
	int nv = (max * pm + 500) / 1000;	/* (rounded both ways: the panels' 1666 steps kept in thousandths, a level
						 * saved and restored at every shutdown no longer creeps down a step each time) */
	if (nv < 1) nv = 1;
	snprintf(p, sizeof p, "/sys/class/backlight/%s/brightness", BL[i]);
	snprintf(v, sizeof v, "%d", nv);
	write_file(p, v);
}
static void backlight_set_pair(int top, int bottom) { backlight_set_one(0, top); backlight_set_one(1, bottom); }
/* a level file ("<top> <bottom>" or one number for both): 0 when it has none */
static int read_pair(const char *path, int *top, int *bottom)
{
	FILE *f = fopen(path, "r");
	if (!f) return 0;
	int n = fscanf(f, "%d %d", top, bottom);
	fclose(f);
	if (n < 1) return 0;
	if (n == 1) *bottom = *top;
	/* one panel's level unusable (a 0 kept while that panel was off): the other's for both, instead of the whole
	 * file ignored and both panels left at the kernel's 50% */
	int okt = *top > 0 && *top <= 1000, okb = *bottom > 0 && *bottom <= 1000;
	if (!okt && okb) *top = *bottom;
	else if (okt && !okb) *bottom = *top;
	return okt || okb;
}
/* a panel's level now, in thousandths of its maximum (-1: no such backlight) */
static int backlight_permille(int i)
{
	char p[96];
	snprintf(p, sizeof p, "/sys/class/backlight/%s/max_brightness", BL[i]);
	int max = read_int(p, 0);
	snprintf(p, sizeof p, "/sys/class/backlight/%s/brightness", BL[i]);
	return max > 0 ? (read_int(p, 0) * 1000 + max / 2) / max : -1;
}

/* the bottom panel's backlight (BL[1], backlight0 on both handhelds). While banana-ui is managing the bottom's idle dim
 * itself (it writes /run/banana/bottom-ui, see ui/src/main.cpp), the menu's own dim leaves that one backlight to it:
 * it would otherwise dim the bottom twice and undo banana-ui's restore. The empty string: no bottom panel (QEMU). */
static void find_bottom_bl(void)
{
	char path[96];
	snprintf(path, sizeof path, "/sys/class/backlight/%s", BL[1]);
	if (access(path, F_OK)) return;
	snprintf(bottom_bl, sizeof bottom_bl, "%s", BL[1]);
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
		/* (dim_level is per backlight already; the pair, top and bottom, from those levels before dimming) */
		int lv[2] = { -1, -1 };
		for (int k = 0; k < dim_count; k++)
			for (int i = 0; i < 2; i++)
				if (!strcmp(dim_name[k], BL[i])) {
					char p[96];
					snprintf(p, sizeof p, "/sys/class/backlight/%s/max_brightness", BL[i]);
					int max = read_int(p, 0);
					if (max > 0) lv[i] = dim_level[k] * 1000 / max;
				}
		/* (fopen, not write_file: that one opens without O_CREAT, for sysfs, and this file is new each time: never
		 * written, rcK saved the dimmed level as the user's at every shutdown during a dim, 30% less each time: the
		 * black screens of 0.5.0-beta.3) */
		if (lv[0] > 0) {
			FILE *f = fopen("/run/banana/dim", "w");
			if (f) { fprintf(f, "%d %d\n", lv[0], lv[1] > 0 ? lv[1] : lv[0]); fclose(f); }
			else say("dim: /run/banana/dim not written: %s", strerror(errno));
		}
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

/* A saved level under 5% (in thousandths) is never one a person chose: the Settings rows go down to 10% and F + volume
 * to one brightness step; under it the panels look black, as if dead. Such a level (a dim saved as the user's, 0.5.0
 * beta.3's black screens after a reboot) comes back as 50%. */
#define BL_FLOOR_PM 50
static int bl_sane(int pm) { return pm < BL_FLOOR_PM || pm > 1000 ? 500 : pm; }
static void save_settings(double now)
{
	if (save_backlight_at > 0 && now >= save_backlight_at) {
		save_backlight_at = 0;
		int t = backlight_permille(0), b = backlight_permille(1);
		if (t >= BL_FLOOR_PM && (b <= 0 || b >= BL_FLOOR_PM)) {
			FILE *f = fopen("/data/banana/backlight", "w");
			if (f) { fprintf(f, "%d %d\n", t, b > 0 ? b : t); fclose(f); }
		}
	}
	if (save_volume_at > 0 && now >= save_volume_at) {
		save_volume_at = 0;
		char *argv[] = { "alsactl", "-f", "/data/banana/asound.state", "store", NULL };
		run(argv);
	}
}

static void restore_settings(void)
{
	int t, b;
	if (read_pair("/data/banana/backlight", &t, &b)) backlight_set_pair(bl_sane(t), bl_sane(b));
	/* started again while the panels were dimmed (/run/banana/dim: the levels before): undimmed, as the last one would */
	if (read_pair("/run/banana/dim", &t, &b)) { backlight_set_pair(bl_sane(t), bl_sane(b)); unlink("/run/banana/dim"); }
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

/* the kernel's side of a wake, as Android's wakeup reasons: the interrupt that woke it (/sys/power/pm_wakeup_irq,
 * PM_SLEEP_DEBUG; named from /proc/interrupts) and the wakeup sources whose event count went up across the sleep.
 * For sleep.log: a press that never woke it shows as no wake at all, one that woke it as the PMIC's interrupt */
#define WSRC_MAX 48
static struct { char dir[24], name[40]; int count; } wsrc[WSRC_MAX];
static int nwsrc;
static int wsrc_count(const char *dir, const char *what)
{
	char p[300];
	snprintf(p, sizeof p, "/sys/class/wakeup/%s/%s", dir, what);
	return read_int(p, -1);
}
static void wake_sources_snapshot(void)
{
	nwsrc = 0;
	DIR *d = opendir("/sys/class/wakeup");
	struct dirent *e;
	while (d && nwsrc < WSRC_MAX && (e = readdir(d))) {
		if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof wsrc[0].dir) continue;
		char p[300];
		snprintf(wsrc[nwsrc].dir, sizeof wsrc[0].dir, "%s", e->d_name);
		snprintf(p, sizeof p, "/sys/class/wakeup/%s/name", e->d_name);
		read_str(p, wsrc[nwsrc].name, sizeof wsrc[0].name);
		wsrc[nwsrc].count = wsrc_count(e->d_name, "event_count");
		nwsrc++;
	}
	if (d) closedir(d);
}
static void wake_sources_fired(char *out, size_t n)
{
	size_t len = 0;
	out[0] = 0;
	int irq = read_int("/sys/power/pm_wakeup_irq", -1);
	if (irq >= 0) {
		char name[64] = "", ln[512], want[16];
		snprintf(want, sizeof want, "%d:", irq);
		FILE *f = fopen("/proc/interrupts", "r");
		while (f && fgets(ln, sizeof ln, f)) {
			char *t = ln + strspn(ln, " ");
			if (strncmp(t, want, strlen(want))) continue;
			ln[strcspn(ln, "\n")] = 0;
			char *last = strrchr(ln, ' ');
			snprintf(name, sizeof name, "%s", last ? last + 1 : "");
			break;
		}
		if (f) fclose(f);
		len += snprintf(out + len, n - len, "irq %d%s%s", irq, name[0] ? " " : "", name);
	}
	for (int i = 0; i < nwsrc && len < n; i++) {
		int c = wsrc_count(wsrc[i].dir, "event_count");
		if (c > wsrc[i].count && wsrc[i].count >= 0)
			len += snprintf(out + len, n - len, "%s%s", len ? ", " : "", wsrc[i].name[0] ? wsrc[i].name : wsrc[i].dir);
	}
}

/* ---- charging while off ---------------------------------------------------------------------------------------- */
/* A charger plugged into the handheld while it is off starts the RK817, and so the whole boot (U-Boot no longer turns
 * it straight off again: "Power Off due to plug-in event"); turned off with the charger in, rcK restarts it instead
 * (banana/.charge-next). Either way rcS runs "bananad --charge" before anything else, and it charges as a phone does
 * while off: the panels dark, one CPU at its lowest clock, the amber LED until the battery is full, then the green
 * one. The power button held a second starts the handheld (exit 0: rcS goes on); the charger pulled out turns it off
 * (exit 3). Not a charger's start: exit 1 at once. banana/logs/charging.log says what each one did. */
#define RK817_ON_SOURCE 0xf5	/* why the RK817 started: bit 7 the power key, bit 6 a charger plugged in */

/* a register of the PMIC (I2C address 0x20, its bus found in sysfs), -1 if it can't be read (QEMU: none) */
static int pmic_read(unsigned char reg)
{
	int bus = -1;
	DIR *d = opendir("/sys/bus/i2c/devices");
	struct dirent *e;
	while (d && (e = readdir(d))) {
		int b;
		char p[300], name[32];
		if (sscanf(e->d_name, "%d-0020", &b) != 1 || !strstr(e->d_name, "-0020")) continue;
		snprintf(p, sizeof p, "/sys/bus/i2c/devices/%s/name", e->d_name);
		read_str(p, name, sizeof name);
		if (!strncmp(name, "rk8", 3)) { bus = b; break; }
	}
	if (d) closedir(d);
	if (bus < 0) return -1;
	char dev[32];
	snprintf(dev, sizeof dev, "/dev/i2c-%d", bus);
	int fd = open(dev, O_RDWR | O_CLOEXEC);
	if (fd < 0) return -1;
	/* the register's number and its value in one transfer (the kernel's driver shares the bus) */
	unsigned char v = 0;
	struct i2c_msg m[2] = { { 0x20, 0, 1, &reg }, { 0x20, I2C_M_RD, 1, &v } };
	struct i2c_rdwr_ioctl_data x = { m, 2 };
	int r = ioctl(fd, I2C_RDWR, &x);
	close(fd);
	return r == 2 ? v : -1;
}

static void charge_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void charge_log(const char *fmt, ...)
{
	char line[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	say("%s", line);
	FILE *f = fopen("/data/banana/logs/charging.log", "a");
	if (!f) return;
	time_t now = time(NULL);
	char when[32];
	strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", localtime(&now));
	fprintf(f, "%s %s\n", when, line);
	fclose(f);
}

static void cpus_low(int low)
{
	write_file("/sys/devices/system/cpu/cpufreq/policy0/scaling_governor", low ? "powersave" : "schedutil");
	for (int c = 1; c < 8; c++) {
		char p[64];
		snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/online", c);
		write_file(p, low ? "0" : "1");
	}
}

/* the lid shut now, read at once from every input device that has it (charging while off has none open yet) */
static int lid_shut_now(void)
{
	if (test_lid) return read_int(test_lid, 0);
	int shut = 0;
	DIR *d = opendir("/dev/input");
	struct dirent *e;
	while (d && !shut && (e = readdir(d))) {
		if (strncmp(e->d_name, "event", 5)) continue;
		char p[300];
		snprintf(p, sizeof p, "/dev/input/%s", e->d_name);
		int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0) continue;
		unsigned long sw[(SW_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = { 0 };
		if (ioctl(fd, EVIOCGSW(sizeof sw), sw) >= 0 && (sw[SW_LID / (8 * sizeof(long))] >> (SW_LID % (8 * sizeof(long))) & 1))
			shut = 1;
		close(fd);
	}
	if (d) closedir(d);
	return shut;
}

/* ---- the start guard, a phone's (Android's charger mode, Rockchip's on the stock RG DS: uboot-low-power-voltage 3.6 V,
 * uboot-exit-charge-level 6%, uboot-exit-charge-voltage 3.75 V): starting draws a burst of current, and a nearly empty
 * battery sags under it and cuts out half way, a start-and-die loop that also wears the cell */
#define START_MIN_UV	3600000	/* without a charger: under this, not started at all */
#define START_CHG_PCT	6	/* from charging while off: at least this */
#define START_CHG_UV	3750000	/* and this */

/* the battery's voltage now (microvolts), -1 unknown */
static int battery_uv(void)
{
	char bat[256] = "", p[320];
	int pct = -1, charging = 0;
	if (!battery_state(bat, sizeof bat, &pct, &charging)) return -1;
	snprintf(p, sizeof p, "/sys/class/power_supply/%s/voltage_now", bat);
	return read_int(p, -1);
}

/* the phone's "battery too low" picture, on the one light there is: the red LED blinks three times */
static void blink_too_low(void)
{
	char red[300];
	led_path("status", red, sizeof red);
	if (!red[0]) return;
	for (int i = 0; i < 3; i++) {
		led_set(red, "on", 255);
		usleep(250000);
		led_set(red, "off", 255);
		usleep(250000);
	}
}

/* enough to start from charging: the percent and the voltage both (the gauge alone can be wrong after a long rest) */
static int start_ok(int *pct_out, int *uv_out)
{
	char bat[256] = "";
	int pct = -1, charging = 0;
	battery_state(bat, sizeof bat, &pct, &charging);
	int uv = battery_uv();
	if (pct_out) *pct_out = pct;
	if (uv_out) *uv_out = uv;
	return (pct < 0 || pct >= START_CHG_PCT) && (uv < 0 || uv >= START_CHG_UV);
}

static int uevent_open(void);
static int charge_mode(void)
{
	/* a restart (rcK's mark): the RK817 still says what started it the first time, a charger perhaps */
	if (!unlink("/data/banana/.restart")) { unlink("/data/banana/.charge-next"); return 1; }
	const char *why = !unlink("/data/banana/.charge-next") ? "turned off with the charger in" : NULL;
	int on = pmic_read(RK817_ON_SOURCE);
	if (!why && on >= 0 && (on & 0x40) && !(on & 0x80)) why = "the charger plugged in";
	/* the power button (or reset) with the lid shut, in a bag perhaps: not meant, as a press with it shut doesn't wake
	 * the handheld either. Off again (exit 3, as the charger pulled out), or charging if a charger is in; U-Boot left
	 * the power LED dark. Opening the lid is what starts it; bananad.conf's lid_start = yes, as before */
	int lid_start = !why && !cfg.lid_start && lid_shut_now();
	if (lid_start && charger_online()) why = "started with the lid shut, the charger in";
	if (lid_start && !why) { charge_log("started with the lid shut: turned off again"); return 3; }
	/* the start guard: a press of the power button with the battery too low. Without a charger, under 3.6 V twice a
	 * second apart (not a moment's sag): off again. With one in, under 6% or 3.75 V: charging while off, as the
	 * stock system's charge mode */
	if (!why) {
		int uv = battery_uv();
		if (!charger_online() && uv > 0 && uv < START_MIN_UV) {
			sleep(1);
			int uv2 = battery_uv();
			if (uv2 > 0 && uv2 < START_MIN_UV) {
				charge_log("the battery too low to start (%d mV): turned off again; charge it first", uv2 / 1000);
				blink_too_low();
				return 3;
			}
		}
		int pct;
		if (charger_online() && !start_ok(&pct, &uv)) why = "the battery too low to start, the charger in";
	}
	if (!why) return 1;
	/* started by the cable, but no charger seen: the RK817's charger reads offline for the first seconds of a boot
	 * with the cable in (sorting out what is plugged in: logs-test19, +1.5 A into the battery while "offline"), and
	 * that start went on to the menu. Up to 6 s for it; none by then: the cable is out again (plugged in and straight
	 * out), and it is off again, not the menu, as a phone off stays off */
	if (!charger_online()) {
		double w0 = mono();
		while (!charger_online() && mono() - w0 < 6) usleep(250000);
		if (!charger_online()) { charge_log("%s, but the charger out again already: turned off again", why); return 3; }
		charge_log("(the charger seen %.1f s into charging while off)", mono() - w0);
	}
	char bat[256] = "";
	int pct = -1, charging = 0;
	battery_state(bat, sizeof bat, &pct, &charging);
	char amb[300], bp[320];
	led_path("charging", amb, sizeof amb);
	snprintf(bp, sizeof bp, "%s/brightness", amb);
	int lit = amb[0] ? read_int(bp, -1) : -1;	/* (U-Boot lights it with a charger in, uboot-patches/004) */
	charge_log("charging while off (%s): battery %d%%, the charging light %s", why, pct,
		lit > 0 ? "already on (U-Boot)" : lit == 0 ? "off until now" : "not found");
	close(open("/run/banana/charging", O_WRONLY | O_CREAT | O_CLOEXEC, 0644));	/* (for a boot's own waits) */
	backlights_power(0);
	cpus_low(1);
	char green[300], red[300], amber[300];
	led_path("power", green, sizeof green);
	led_path("status", red, sizeof red);
	led_path("charging", amber, sizeof amber);
	/* the LEDs are all it shows, and at full: asleep, a PWM LED stays lit only at 100% (retain-state-suspended) */
	int level = 255;
	led_set(red, "off", level);
	/* the power button: every input device that has it */
	struct pollfd pf[16];
	int n = 0;
	DIR *d = opendir("/dev/input");
	struct dirent *e;
	while (d && n < 16 && (e = readdir(d))) {
		if (strncmp(e->d_name, "event", 5)) continue;
		char p[300];
		snprintf(p, sizeof p, "/dev/input/%s", e->d_name);
		int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0) continue;
		unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = { 0 };
		int k = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0 ||	/* (the test's FIFOs: can't say, kept) */
			(keys[KEY_POWER / (8 * sizeof(long))] & (1UL << (KEY_POWER % (8 * sizeof(long)))));
		if (k) pf[n++] = (struct pollfd){ fd, POLLIN, 0 };
		else close(fd);
	}
	if (d) closedir(d);
	struct input_event ev;
	for (int i = 0; i < n; i++) while (read(pf[i].fd, &ev, sizeof ev) > 0) {}	/* (what came before isn't for us) */
	int keys_n = n;
	/* the kernel's uevents (the battery's, the charger's): a look at once when one comes, so the lights follow the
	 * cable while it is awake, not at the next look */
	int uev = uevent_open();
	if (uev >= 0 && n < 16) pf[n++] = (struct pollfd){ uev, POLLIN, 0 };
	/* Android's charger mode (healthd_mode_charger): the cable pulled out turns the lights off at once and the handheld
	 * off 10 s later (UNPLUGGED_SHUTDOWN_TIME), the cable back meanwhile calls that off; awake until then */
	double t0 = mono(), held = 0, look = 0, unplugged = 0;
	int full = -1, r = -1, slept = 0, cant_sleep = 0, wc_missed = 0, busy = 0;
	/* the pre-sleep hooks (Wi-Fi and USB off) in a child, beside the loop: run in line they took a minute at times (the
	 * Wi-Fi's module still coming up this early in the boot), and the power button and the cable went unheard meanwhile:
	 * the charging light stayed on that long after the cable came out, and holding power to start did nothing. Android's
	 * charger mode answers both at once, whatever else it does */
	pid_t pre = 0;
	int pre_done = 0;
	double pre_t = 0;
	/* how long the power button is held to start it: a second, half of one more when the press is what woke it (its
	 * wake took about a second and a half already: Android's charger mode wants 2 s in all, POWER_ON_KEY_TIME) */
	double need = 1.0;
	char wc[32] = "", woke[200] = "";	/* (the wakeup count at the look; what woke the last sleep, for the log) */
	write_file("/sys/class/rtc/rtc0/device/power/wakeup", "enabled");	/* (the looks' alarm: before main() sets it) */
	while (r < 0) {
		poll(pf, n, held || pre > 0 ? 100 : look ? 2000 : 0);	/* (the first look at once) */
		double now = mono();
		if (pre > 0 && waitpid(pre, NULL, WNOHANG) == pre) {
			pre = 0; pre_done = 1;
			if (now - pre_t > 2) charge_log("the pre-sleep steps (Wi-Fi and USB off) took %.0f s", now - pre_t);
			sleep_mode();
			look = 0;	/* (asleep now: looked at once) */
		}
		for (int i = 0; i < keys_n; i++) {
			while (read(pf[i].fd, &ev, sizeof ev) == (ssize_t)sizeof ev)
				if (ev.type == EV_KEY && ev.code == KEY_POWER) {
					held = ev.value ? (held ? held : now) : 0;
					if (!ev.value) need = 1.0;	/* (let go: the next press is a new one) */
				}
		}
		if (uev >= 0 && (pf[n - 1].revents & POLLIN)) {
			char buf[2048];
			while (recv(uev, buf, sizeof buf, MSG_DONTWAIT) > 0) {}
			look = 0;	/* (the charger or the battery changed: looked at now) */
		}
		if (held && now - held >= need && !lid_shut_now()) {	/* (not with the lid shut: open it first) */
			int sp, suv;
			if (start_ok(&sp, &suv)) { r = 0; break; }
			/* too low to start yet (6% and 3.75 V): the red light says so, and it charges on */
			charge_log("the power button: the battery too low to start (%d%%, %d mV), charging on until %d%% and %.2f V",
				sp, suv / 1000, START_CHG_PCT, START_CHG_UV / 1e6);
			blink_too_low();
			held = 0;
		}
		if (now < look) continue;
		look = now + 2;
		/* the kernel's wakeup count before the look, written back before the suspend: a cable pulled out between the
		 * two (its event already in) calls the suspend off instead of being slept through (the kernel's way) */
		read_str("/sys/power/wakeup_count", wc, sizeof wc);
		if (!charger_online()) {
			if (!unplugged) {
				unplugged = now;
				led_set(amber, "off", level);
				led_set(green, "off", level);
				full = -1;	/* (back: the lights set again) */
				charge_log("the charger pulled out: off in 10 s unless it comes back");
			} else if (now - unplugged >= 10) { r = 3; break; }
			continue;	/* (awake until then, a look every 2 s) */
		}
		if (unplugged) { unplugged = 0; charge_log("the charger back: charging on"); }
		char st[32] = "", p[320];
		battery_state(bat, sizeof bat, &pct, &charging);
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/status", bat);
		read_str(p, st, sizeof st);
		int f = pct >= 100 || !strcmp(st, "Full");
		if (f != full) {
			full = f;
			led_set(amber, f ? "off" : "on", level);
			led_set(green, f ? "on" : "off", level);
			if (f) charge_log("full after %.0f min", (now - t0) / 60);
		}
		/* nothing to wait for awake: asleep, as a phone charging while off, until the power button, the cable
		 * coming out or the next look in 10 minutes (the LED amber or green). Not while the cable looks out (its
		 * second look comes awake), nor after a suspend that failed (awake then, as before) */
		if (held || cant_sleep) continue;
		if (!pre_done) {
			/* (USB and Wi-Fi off, as for a sleep: in a child; asleep once it is done) */
			if (!pre) {
				pre_t = now;
				pre = fork();
				if (pre == 0) { hooks_run("pre"); _exit(0); }
				if (pre < 0) { pre = 0; hooks("pre", 0); pre_done = 1; sleep_mode(); }
			}
			if (!pre_done) continue;
		}
		if (wc[0] && write_file("/sys/power/wakeup_count", wc) && wc_missed++ < 3) { look = 0; continue; }
		wc_missed = 0;
		/* the next look in 20 s: the cable's going wakes it at once (the RK817's interrupt), but should that ever be
		 * missed, the lights still go out within 20 s. On the charger the looks cost the battery nothing */
		rtc_alarm(20);
		wake_sources_snapshot();
		int sr = write_file("/sys/power/state", "mem");
		int err = errno;
		rtc_alarm(0);
		if (!sr) wake_sources_fired(woke, sizeof woke);
		/* a sleep refused (busy: an event came as it went, the cable's perhaps): tried again at the next look; three in a
		 * row, awake from then on */
		if (sr) {
			busy++;
			if (busy >= 3) cant_sleep = 1;
			charge_log("a sleep refused while charging (%s)%s", strerror(err), cant_sleep ? ": awake from now on" : ": again at the next look");
		} else busy = 0;
		slept++;
		look = 0;	/* (the look at once after the wake) */
		/* the power button held: the press that woke it may be over before its event is read, or still going */
		for (int i = 0; i < keys_n; i++) {
			unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = { 0 };
			if (ioctl(pf[i].fd, EVIOCGKEY(sizeof keys), keys) >= 0 &&
			    (keys[KEY_POWER / (8 * sizeof(long))] & (1UL << (KEY_POWER % (8 * sizeof(long))))) && !held)
				{ held = mono(); need = 0.5; }
		}
	}
	/* starting: USB and Wi-Fi back. Pre-sleep steps still at work (power pressed in the first moments): left to end on
	 * their own, the post ones after them, in the background, so the start isn't held up */
	if (pre > 0) {
		if (!r) {
			pid_t w = fork();
			if (w == 0) {
				/* (not this one's child: its end watched, a zombie counting as ended) */
				char st;
				while (kill(pre, 0) == 0 && (st = proc_state(pre)) != 'Z' && st != 0) usleep(100000);
				hooks_run("post");
				_exit(0);
			}
		}
	} else if (pre_done && !r) hooks("post", 0);
	for (int i = 0; i < n; i++) close(pf[i].fd);
	unlink("/run/banana/charging");
	battery_state(bat, sizeof bat, &pct, &charging);
	charge_log("%s after %.0f min: battery %d%%%s%s%s", r ? "the charger pulled out: turning off" : "the power button: starting",
		(mono() - t0) / 60, pct, r && slept ? " (the last wake: " : "", r && slept ? (woke[0] ? woke : "not known") : "",
		r && slept ? ")" : "");
	led_set(amber, "off", level);
	led_set(green, r ? "off" : "on", level);
	if (!r) cpus_low(0);	/* (the boot's banana-perf sets the governor it wants) */
	return r;
}

int leds_last = -2;	/* the upper LED's state as last set; -2: set it again */
static int amber_last = -2;	/* the charging LED's, the same way */
static int leds_asleep;		/* suspend_now's: the charging light at full (asleep, a PWM LED stays lit only at 100%) */
static void leds_update(void)
{
	static char green[300], red[300], amber[300];
	static int looked;
	int last = leds_last;
	if (!looked) {
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
	/* the upper LED: green, red when low, blinking red when critical (not while charging) */
	int state = level <= 0 ? 0 : (!charging && pct >= 0 && pct <= cfg.led_critical) ? 3 : (!charging && pct >= 0 && pct <= cfg.led_low) ? 2 : 1;
	if (state != last) {
		leds_last = state;
		led_set(green, state == 1 ? "on" : "off", level);
		led_set(red, state == 3 ? "blink" : state == 2 ? "on" : "off", level);
		say("LEDs: battery %d%%%s", pct, charging ? ", charging" : "");
		static const char *const looks[] = { "off", "green", "red", "red blinking" };
		rec("led", "upper %s%s (battery %d%%%s)", looks[state], "", pct, charging ? ", charging" : "");
	}
	/* the lower LED: amber, steady, the whole time the charger is plugged in and the battery isn't full */
	int amber_on = level > 0 && plugged && (pct < 0 || pct < 100);
	if (amber_on != amber_last || last == -2) {
		amber_last = amber_on;
		led_set(amber, amber_on ? "on" : "off", leds_asleep ? 255 : level);
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

/* a USB or HID device's driver, as udev would load it: the module its MODALIAS names (an Xbox pad's xpad, a USB
 * serial adapter's; a Bluetooth controller's HID driver, hid-nintendo for a Switch Pro controller); the handheld's own
 * hardware is built into the kernel */
static void load_driver(const char *alias)
{
	if (strncmp(alias, "usb:", 4) && strncmp(alias, "hid:", 4)) return;
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
 * going in sends several uevents. -o: the scan's children don't get the lock's descriptor (its "share restart"
 * starts ksmbd.mountd, which kept the lock for as long as it ran, and every later scan waited forever: the card's
 * games missing until Settings scanned again, #65) */
static void sd2_scan(void)
{
	char *argv[] = { "/bin/sh", "-c", "flock -o /run/banana/sd2.lock /usr/libexec/banana/sd2 scan </dev/null >/dev/null 2>&1 &", NULL };
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
		int add = 0, block = 0, mmc = 0;
		for (char *p = buf; p < buf + n; p += strlen(p) + 1) {
			if (!strcmp(p, "SUBSYSTEM=power_supply")) hit = 1;
			else if (!strcmp(p, "SUBSYSTEM=block")) block = 1;
			else if (!strncmp(p, "DEVNAME=mmcblk", 14)) mmc = 1;
			else if (!strcmp(p, "ACTION=add")) add = 1;
			else if (!strncmp(p, "MODALIAS=", 9)) alias = p + 9;
		}
		if (add && alias) load_driver(alias);
		if (block && mmc) sd2_scan();	/* a card in or out of the second slot (the script looks at what's there) */
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
		quiet_add(path, 0);	/* (the flight recorder's touches and sticks: a descriptor of their own, never polled) */
	}
	closedir(d);
	read_lid();
}

/* the game running is a DS one (its command line): its emulator passes the lid on to the game */
static int ds_game(void)
{
	pid_t g = pid_of("/run/banana/game.pid");
	if (!g) return 0;
	char p[64], cmd[512] = "";
	snprintf(p, sizeof p, "/proc/%d/cmdline", (int)g);
	FILE *f = fopen(p, "r");
	if (!f) return 0;
	size_t n = fread(cmd, 1, sizeof cmd - 1, f);
	fclose(f);
	for (size_t i = 0; i < n; i++) if (!cmd[i]) cmd[i] = ' ';
	cmd[n] = 0;
	/* (the launchers exec the emulator: /usr/bin/bananads, superdrastic-run then /data/drastic/drastic) */
	return strstr(cmd, "bananads") || strstr(cmd, "drastic") || strstr(cmd, "run-nds");
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
		/* (the recorder: not which button while the on-screen keyboard is open, as a touch's place isn't: the
		 * keyboard is typed on with the d-pad and A too, its cursor always starting on the same key, so the buttons'
		 * names in a session file read back as the password. The power and volume keys type nothing) */
		int typing = recst.on && e->code != KEY_POWER && e->code != KEY_VOLUMEUP && e->code != KEY_VOLUMEDOWN &&
			access("/run/banana/keyboard", F_OK) == 0;
		if (typing) rec("button", "%s (keyboard open: which not recorded)", e->value == 1 ? "down" : e->value == 2 ? "held" : "up");
		else if (k) rec("button", "%s %s", k, e->value == 1 ? "down" : e->value == 2 ? "held" : "up");
		else rec("button", "key %d %s", e->code, e->value == 1 ? "down" : e->value == 2 ? "held" : "up");
		if (e->code == BTN_SELECT && e->value != 2) { sel_down = e->value; sel_since = now; sel_marked = 0; }	/* (the loop times the hold) */
	}
	if (e->type == EV_SW && e->code == SW_LID) rec("lid", e->value ? "closed" : "opened");
	if (e->type == EV_SW && e->code == SW_HEADPHONE_INSERT) rec("audio", e->value ? "headphones in" : "headphones out");
	if (e->type == EV_KEY) { last_input = now; undim(); }
	if (e->type == EV_SW && e->code == SW_LID) {
		lid_closed = e->value;
		if (!lid_closed) { if (lid_sleep_at > 0) say("lid opened again before the sleep"); undim(); lid_retry_off = 0; lid_sleep_at = 0; lid_held = 0; }
		/* a DS game: the panels dark now, the sleep 1.5 s later, so the game sees its lid close first (BananaDS and
		 * SuperDrastic read the hall sensor too: its sleep mode, its lid sound); opened before that, no sleep */
		if (lid_closed && cfg.lid_sleep && ds_game()) { lid_sleep_at = now + 1.5; backlights_power(0); say("lid shut in a DS game: asleep in 1.5 s"); }
		else if (lid_closed && cfg.lid_sleep && transfer_hold()) {
			backlights_power(0);
			if (!lid_held) sleep_log("lid closed during a download: dark and awake until it is done");
			lid_held = 1;
		}
		else if (lid_closed && cfg.lid_sleep) suspend_now("lid closed");
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
	/* (with F held they are RetroArch's own hotkeys: F + L2 rewinds) */
	case BTN_TL2: if (e->value == 1 && !fn_held) ra_button("l2"); break;
	case BTN_TR2: if (e->value == 1 && !fn_held) ra_button("r2"); break;
	case BTN_THUMBL: if (e->value == 1 && !fn_held) ra_button("l3"); break;
	case BTN_THUMBR: if (e->value == 1 && !fn_held) ra_button("r3"); break;
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
	openlog("bananad", LOG_PID, LOG_DAEMON);
	test_lid = getenv("BANANAD_TEST_LID");
	if (argc > 1 && !strcmp(argv[1], "--charge")) {	/* rcS's, before the menu: charging while off (charge_mode) */
		load_conf("/etc/banana/bananad.conf");
		load_conf("/data/banana/bananad.conf");
		load_timezone();
		return charge_mode();
	}
	load_conf("/etc/banana/bananad.conf");
	load_conf("/data/banana/bananad.conf");
	load_timezone();
	/* the LEDs at the user's level first: everything below (the inputs, alsactl, the mixer, the second card) takes a
	 * while, and the power LED is the first sign the handheld is starting (U-Boot lit it, the kernel kept it on) */
	leds_update();
	unlink("/run/banana/recording");	/* (left by a bananad stopped before: as bananad.conf says now) */
	rec_apply();
	rec("bananad", "started");
	signal(SIGCHLD, SIG_DFL);
	signal(SIGPIPE, SIG_IGN);	/* a game's pipe that closed must not take the daemon with it */
	struct sigaction sa = { 0 };
	sa.sa_handler = on_hup;		/* no SA_RESTART: poll() returns, and the loop reloads */
	sigaction(SIGHUP, &sa, NULL);
	sa.sa_handler = on_term;
	sigaction(SIGTERM, &sa, NULL);
	mkdir("/run/banana", 0755);
	/* a menu Sleep asked for while bananad wasn't running would otherwise come true at the next, unrelated SIGHUP
	 * (any settings change): dropped */
	unlink("/run/banana/sleep-now");
	find_bottom_bl();	/* which backlight is the bottom one: banana-ui's idle dim owns it */
	mkdir(BT_AUDIO_DIR, 0755);

	int ino = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	int wd_input = -1, wd_bt = -1;
	if (ino >= 0) {
		wd_input = inotify_add_watch(ino, "/dev/input", IN_CREATE | IN_DELETE);
		/* Bluetooth headphones coming and going (its own folder: nothing else written in /run/banana wakes us) */
		wd_bt = inotify_add_watch(ino, BT_AUDIO_DIR, IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE);
	}
	unlink("/run/banana/bananad-ready");
	open_inputs();
	headphones = headphones_in();
	restore_settings();	/* the backlight */
	audio_find_cards();	/* (the loop's own idea of the cards; the child writes the route) */
	sound_start();		/* the volumes and the route, in a child (above) */
	int uev = uevent_open();
	int recsock = rec_sock_open();	/* (the recorder's events from banana-ui) */
	sd2_scan();	/* a card already in the second slot at boot */
	load_drivers_present();
	leds_update();
	last_input = mono();
	double next_battery = 0;
	int low_count = 0;
	say("running: lid %s, auto-sleep %g min, %s", cfg.lid_sleep ? "sleeps" : "ignored", cfg.autosleep_min, cfg.mem_sleep);
	/* the power and volume keys heard from here: rcS waits for this before the menu (and its boot animation) starts */
	boot_mark("bananad: buttons live");
	FILE *rf = fopen("/run/banana/bananad-ready", "w");
	if (rf) fclose(rf);

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
		/* a job that must not be slept through (banana-scrape's downloads: its pid in /run/banana/awake.pid; a Files
		 * job's: banana-ui's in /run/banana/files.pid; a download of Files': transfer_hold); looked at again every 5 s,
		 * so the idle sleep, or the lid's, comes once it is over */
		int awake = pid_of("/run/banana/awake.pid") != 0 || pid_of("/run/banana/files.pid") != 0 || transfer_hold();
		if (awake && wait > 5) wait = 5;
		/* (only deadlines still ahead: one already passed was acted on after the last poll, or is held back by what
		 * the action checks (the charger, the lid, no backlight) and a uevent or an input event brings it back; counted
		 * here it spun this loop every 20 ms, on the charger for as long as the menu sat idle) */
		if (!game) {
			if (cfg.autosleep_min > 0 && !awake) { double d = cfg.autosleep_min * 60.0 - (tnow - last_input); if (d > 0 && d < wait) wait = d; }
			if (cfg.dim_min > 0 && !dimmed) { double d = cfg.dim_min * 60.0 - (tnow - last_input); if (d > 0 && d < wait) wait = d; }
		}
		/* recording: the touchscreen read every 0.2 s, before its buffer of motion can overflow (a touch's own time is
		 * in its event, so it's recorded when it happened). Dimmed: the same, so a touch lights the panels again at
		 * once and not at the next minute's look (only while dimmed in the menu: nothing else wakes bananad for it) */
		if ((recst.on || dimmed) && nquiet && wait > 0.2) wait = 0.2;
		if (lid_closed && cfg.lid_sleep && !lid_retry_off) { double d = 5.0 - (tnow - last_input); if (d > 0 && d < wait) wait = d; }
		if ((save_volume_at > 0 || save_backlight_at > 0) && wait > 0.5) wait = 0.5;
		if (power_down && wait > 0.1) wait = 0.1;
		if (recst.on && sel_down && !sel_marked && wait > 0.1) wait = 0.1;	/* (Select held: the marker at 2 s) */
		if (sound_child > 0 && wait > 0.05) wait = 0.05;	/* the sound's setup running: its end looked for (the first second or so) */
		if (lid_sleep_at > 0) { double d = lid_sleep_at - tnow; if (d < wait) wait = d > 0 ? d : 0; }
		if (menu_sleep_at > 0) { double d = menu_sleep_at - tnow; if (d < wait) wait = d > 0 ? d : 0; }
		int timeout = wait <= 0 ? 20 : (int)(wait * 1000) + 20;
		in_poll = 1;
		if (!stopping) poll(pf, n, timeout);
		in_poll = 0;
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
			say("settings read again: lid %s, auto-sleep %g min, LEDs %d", cfg.lid_sleep ? "sleeps" : "ignored", cfg.autosleep_min, cfg.led_brightness);
			/* the menu's Power > Sleep: the same sleep as a press of the power button, half a second on */
			if (unlink("/run/banana/sleep-now") == 0) { menu_sleep_at = mono() + 0.5; say("the menu asked for a sleep"); }
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
			char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
			ssize_t len;
			int inputs = 0, bt = 0;
			while ((len = read(ino, buf, sizeof buf)) > 0)
				for (char *p = buf; p < buf + len; p += sizeof(struct inotify_event) + ((struct inotify_event *)p)->len) {
					struct inotify_event *ev = (struct inotify_event *)p;
					if (ev->wd == wd_input) inputs = 1;
					else if (ev->wd == wd_bt && ev->len && (!strcmp(ev->name, "audio") || !strcmp(ev->name, "hold"))) bt = 1;
				}
			if (bt && !sound_child) audio_route();
			if (inputs) {
				usleep(200000);	/* udev/mdev sets the node's permissions */
				open_inputs();
			}
		}

		if (power_down && now - power_since >= cfg.power_long_ms / 1000.0) { power_down = 0; shutdown_now("power held"); }
		if (recst.on && sel_down && !sel_marked && now - sel_since >= 2.0) { sel_marked = 1; rec_mark(); }

		/* idle in the menu (no game running): the panels dim, then it sleeps */
		game = pid_of("/run/banana/game.pid") != 0;
		/* touches and sticks, recording or not: they count for the idle timers below (a menu used by touch alone, the
		 * on-screen keyboard, dimmed and slept while touched when only the flight recorder read them, so turning it on
		 * changed what it recorded); quiet_scan records them only while the recorder is on */
		if (quiet_scan()) undim();
		if (recst.on && now >= recst.flush_at) { rec_flush(); recst.flush_at = now + 30; }
		if (cfg.dim_min > 0 && !dimmed && !game && !lid_closed && now - last_input >= cfg.dim_min * 60.0) dim();
		/* on the charger too, as a phone sleeps on it (the charging light stays lit asleep) */
		if (cfg.autosleep_min > 0 && !game && now - last_input >= cfg.autosleep_min * 60.0 &&
		    !pid_of("/run/banana/awake.pid") && !pid_of("/run/banana/files.pid") && !transfer_hold())
			suspend_now("idle");
		if (lid_sleep_at > 0 && now >= lid_sleep_at) { lid_sleep_at = 0; if (lid_closed) suspend_now("lid closed"); }
		if (menu_sleep_at > 0 && now >= menu_sleep_at) { menu_sleep_at = 0; suspend_now("the menu's Sleep"); }
		/* started or woken with the lid shut, or shut during a download that is over now */
		if (lid_closed && cfg.lid_sleep && !lid_retry_off && lid_sleep_at == 0 && now - last_input > 5 && !transfer_hold())
			suspend_now(lid_held ? "lid closed, the download done" : "lid still closed");

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
