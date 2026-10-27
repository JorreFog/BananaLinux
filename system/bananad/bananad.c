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
 *     Bluetooth headphones or speakers, while connected, come before both: the bluetooth script writes the one
 *     playing to /run/banana/bt/audio (from bluez-alsa's events), bananad watches that folder and plays through
 *     bluez-alsa's ALSA plugin, and back to the speakers or the jack when the file goes.
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
	int autosleep_min;		/* idle minutes in the menu before sleeping; 0 = never */
	int lid_sleep;			/* closing the lid sleeps */
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
} cfg = { "auto", 5, 8, 10, 1, 2000, 3, "deep", 128, 20, 5, 1, 4, 5, 2, 30, 12, 0 };

static int devfd[MAXDEV], ndev;
static void leds_update(void);
static void audio_route(void);
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

static volatile sig_atomic_t reload;
static void on_hup(int sig) { (void)sig; reload = 1; }

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
		else if (!strcmp(k, "autosleep_minutes")) cfg.autosleep_min = atoi(v);
		else if (!strcmp(k, "lid_sleep")) cfg.lid_sleep = !strcmp(v, "yes") || !strcmp(v, "1");
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
/* (the fallback to s2idle is for the one sleep that needed it: the next sleep, and a sleep again after the battery
 * check, try deep again. Both handhelds' xhci-hcd fails a suspend now and then ("Clearing Run/Stop bit failed",
 * -ETIMEDOUT), with the devices one at a time too, and two of those in one boot used to leave every later sleep in
 * s2idle until a reboot, which drains the battery more) */

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
/* dark and awake on the charger: waits up to ms for a press of the power button (on its release), the other
 * devices' events dropped (a key while dark wakes nothing, as in a sleep) */
static int power_pressed(int ms)
{
	struct pollfd pf[MAXDEV];
	for (int i = 0; i < ndev; i++) pf[i] = (struct pollfd){ devfd[i], POLLIN, 0 };
	if (poll(pf, ndev, ms) <= 0) return 0;
	int got = 0, readable = 0;
	for (int i = 0; i < ndev; i++) {
		if (!(pf[i].revents & POLLIN)) continue;
		readable = 1;
		struct input_event ev[32];
		ssize_t r;
		while ((r = read(devfd[i], ev, sizeof ev)) > 0)
			for (int k = 0; k < r / (ssize_t)sizeof ev[0]; k++)
				if (ev[k].type == EV_KEY && ev[k].code == KEY_POWER && ev[k].value == 0) got = 1;
	}
	/* only a device's hang-up or error (a USB pad pulled out while dark): wait the time out anyway, not a busy loop */
	if (!readable) usleep(ms * 1000);
	return got;
}
/* dark on the charger: the buttons and touchscreens grabbed for bananad alone, so a press there reaches neither the
 * menu nor a game (it looks asleep: A launched the game under the cursor, a tap hit RetroArch's game panel); the lid's
 * hall sensor stays shared, as BananaDS and SuperDrastic must see the lid open to wake the game they put to sleep */
static void inputs_grab(int on)
{
	for (int i = 0; i < ndev; i++) {
		unsigned long sw[(SW_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = { 0 };
		ioctl(devfd[i], EVIOCGBIT(EV_SW, sizeof sw), sw);
		if (sw[SW_LID / (8 * sizeof(long))] >> (SW_LID % (8 * sizeof(long))) & 1) continue;
		ioctl(devfd[i], EVIOCGRAB, on ? 1 : 0);
	}
}
/* the battery's charge in percent whatever the charger says (-1: none): asleep dark on a charger too weak to keep up,
 * it still goes down */
static int battery_capacity(void)
{
	DIR *d = opendir("/sys/class/power_supply");
	if (!d) return -1;
	struct dirent *e;
	int r = -1;
	while (r < 0 && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[320], t[32];
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/type", e->d_name);
		read_str(p, t, sizeof t);
		if (strcmp(t, "Battery")) continue;
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/capacity", e->d_name);
		r = read_int(p, -1);
	}
	closedir(d);
	return r;
}
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
static void resume_state_path(const char *emu, const char *rom, char *out, size_t n)
{
	out[0] = 0;
	const char *dir, *ext;
	if (!strcmp(emu, "superdrastic")) dir = "/data/drastic/savestates", ext = ".resume.dss";
	else if (!strcmp(emu, "bananads")) dir = "/data/states/nds", ext = ".resume.mst";
	else return;
	const char *b = strrchr(rom, '/');
	b = b ? b + 1 : rom;
	const char *dot = strrchr(b, '.');
	int len = dot && dot != b ? (int)(dot - b) : (int)strlen(b);
	snprintf(out, n, "%s/%.*s%s", dir, len, b, ext);
}

/* a command for RetroArch on its stdin (run-retroarch's pipe, or banana-ui's for the game panel) */
static void ra_command(const char *c)
{
	int fd = open("/run/banana/retroarch.cmd", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) return;
	if (write(fd, c, strlen(c)) < 0) {}
	close(fd);
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
			for (int i = 0; i < slot + 2; i++) { ra_command("STATE_SLOT_MINUS\n"); usleep(100000); }	/* (one a frame or more: each is a press) */
			ra_command("SAVE_STATE\n");
			usleep(500000);
			ra_command("QUIT\n");
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
	double t0 = mono();
	pid_t game = pid_of("/run/banana/game.pid");
	pid_t pg = game ? getpgid(game) : 0;
	if (pg > 0) kill(-pg, SIGSTOP);	/* DraStic's dynarec mustn't run on while the clocks drop (ROCKNIX's finding) */
	hooks("pre", 0);
	sync();
	if (sleep_fallback > 1) sleep_fallback = 1;	/* deep again (devices one at a time), see sleep_fallback */
	sleep_mode();
	int cycles = 0, rtc_wakes = 0, failures = 0, dark = 0, dark_lid = 0;
	double asleep = 0, dark_from = 0, led_at = 0;
	int dark_cap = -1;	/* the battery when the dark state began (a charger that can't keep up: it falls from there) */
	const char *woke = "?";
	/* Power off after sleeping: the wall clock's deadline (it goes on while asleep), from the first real sleep */
	time_t off_at = 0;
	for (;;) {
		/* charging: no suspend, which turns the charging light off (it is a PWM LED, and the PWM stops in suspend).
		 * Dark and awake instead, the game still paused, until the lid opens or the power button is pressed (back as
		 * from a sleep), or the charger is pulled out (then a real sleep). Since 0.4.2 a power-button sleep too, not
		 * only the lid's (#8: the light went out when the handheld was put to sleep on the charger). */
		if (charger_online()) {
			/* (no Power off after sleeping here: nothing drains, the charging light would go out, and the handheld is
			 * picked up charged; unplugged, the deadline starts at the real sleep) */
			off_at = 0;
			if (!dark) {
				dark = 1; dark_from = mono(); dark_lid = lid_closed; dark_cap = battery_capacity();
				backlights_power(0);
				inputs_grab(1);
				sleep_log("%s while charging: dark and awake, so the charging light stays on%s", why, game ? ", game paused" : "");
			}
			if (mono() >= led_at) {
				leds_update(); led_at = mono() + 10;	/* (it goes out when the battery is full) */
				/* a charger too weak for the handheld: shut down cleanly before the battery is empty. Only once the
				 * level has fallen since the dark state began: a low battery on a good charger (plugged in at 4%, the
				 * lid shut) charges, and shutting down then cut the game and the charging light for nothing */
				int cap = battery_capacity();
				if (cap >= 0 && cap <= cfg.sleep_poweroff_pct && dark_cap >= 0 && cap < dark_cap) {
					sleep_log("dark on the charger, but the battery is down to %d%%: shutting down", cap);
					inputs_grab(0);
					if (pg > 0) kill(-pg, SIGCONT);	/* the game gets SIGTERM next, to write its saves */
					shutdown_now("battery empty while charging");
					return;
				}
			}
			/* the power button wakes it, as from a sleep; with the lid shut it doesn't (a sleep goes back to
			 * sleep then: a press in a bag mustn't run the game) */
			if (power_pressed(500)) { read_lid(); if (!(lid_closed && cfg.lid_sleep)) { woke = "power button"; break; } }
			read_lid();
			if (dark_lid && !lid_closed) { woke = "lid opened"; break; }
			if (!dark_lid && lid_closed) dark_lid = 1;	/* shut meanwhile: opening it wakes too */
			continue;
		}
		/* the RTC alarm: the sooner of the battery check and the deadline (passed: woken for it, or anything else
		 * after it with the lid shut, and the game is saved and the handheld shut down) */
		if (cfg.poweroff_after_min > 0 && !off_at) off_at = time(NULL) + (time_t)(cfg.poweroff_after_min * 60 + 0.5);
		if (off_at && time(NULL) >= off_at) {
			if (dark) inputs_grab(0);
			sleep_poweroff(game, pg, asleep);
			return;
		}
		int secs = (int)(cfg.sleep_check_hours * 3600);
		if (off_at && (secs <= 0 || off_at - time(NULL) < secs)) secs = (int)(off_at - time(NULL));
		time_t alarm = rtc_alarm(secs);
		double t_in = mono();
		struct timespec b0, b1;
		clock_gettime(CLOCK_BOOTTIME, &b0);	/* BOOTTIME counts the time asleep, MONOTONIC doesn't */
		if (cycles == 0) sleep_log("sleep (%s): %.0f ms to suspend%s", why, (t_in - t0) * 1000, game ? ", game paused" : "");
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
			lid_retry_off = 1;	/* ...and not tried again every 5 s with the lid still shut */
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
	/* the panels on when the lid is open: dark while charging, or dark from a lid shut with lid_sleep off before a
	 * power-button sleep (the lid-open event that would light them is drained below) */
	if (dark) inputs_grab(0);	/* the buttons and touchscreens shared again */
	if (dark || !lid_closed) backlights_power(!lid_closed);
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
	leds_update();
	if (headphones_in() != headphones) { headphones = !headphones; audio_route(); }
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
	say("shutting down (%s)", why);
	pid_t game = pid_of("/run/banana/game.pid");
	if (game) {
		pid_t pg = getpgid(game);
		kill(pg > 0 ? -pg : game, SIGTERM);	/* let the emulator write its saves */
		for (int i = 0; i < 50 && kill(game, 0) == 0; i++) usleep(100000);
	}
	sync();
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
	char play[192], cap[160];
	/* Bluetooth headphones first: bluez-alsa's PCM plugin (A2DP), plug converting the rest; the microphone stays the
	 * rk817's */
	if (bt)
		snprintf(play, sizeof play, "{ type plug slave.pcm { type bluealsa device \"%s\" profile \"a2dp\" } }", bt_mac);
	else if (card_play >= 0)
		snprintf(play, sizeof play, "{ type plug slave { pcm \"hw:%d\" format S16_LE channels 2 rate 48000 } }", card_play);
	else
		snprintf(play, sizeof play, "\"null\"");
	if (card_codec >= 0)
		snprintf(cap, sizeof cap, "{ type plug slave { pcm \"hw:%d\" format S16_LE channels 2 rate 48000 } }", card_codec);
	else
		snprintf(cap, sizeof cap, "\"null\"");
	const char *where = bt ? "Bluetooth headphones" : hold ? "nowhere: Bluetooth headphones being called back"
		: card_play == card_speaker ? "speakers" : headphones ? "headphones" : "the codec";
	char conf[768];
	int n = snprintf(conf, sizeof conf,
		"# bananad: where sound plays now (%s)\n"
		"pcm.!default { type asym playback.pcm %s capture.pcm %s }\n",
		where, play, cap);
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

static void volume(int dir)
{
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
	if (pct >= 0) osd("volume", pct);
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
	if (pct >= 0) osd("brightness", pct);
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
		if (lv[0] > 0) { char v[24]; snprintf(v, sizeof v, "%d %d\n", lv[0], lv[1] > 0 ? lv[1] : lv[0]); write_file("/run/banana/dim", v); }
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
	if (access("/data/banana/asound.state", R_OK) == 0) {
		char *argv[] = { "alsactl", "-f", "/data/banana/asound.state", "restore", NULL };
		run(argv);
	}
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
	}
	/* the lower LED: amber, steady, the whole time the charger is plugged in and the battery isn't full */
	int amber_on = level > 0 && plugged && (pct < 0 || pct < 100);
	if (amber_on != amber_last || last == -2) {
		amber_last = amber_on;
		led_set(amber, amber_on ? "on" : "off", level);
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

static void open_inputs(void)
{
	for (int i = 0; i < ndev; i++) close(devfd[i]);
	ndev = 0;
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

static void handle(const struct input_event *e)
{
	double now = mono();
	if (e->type == EV_KEY) { last_input = now; undim(); }
	if (e->type == EV_SW && e->code == SW_LID) {
		lid_closed = e->value;
		if (!lid_closed) { if (lid_sleep_at > 0) say("lid opened again before the sleep"); undim(); lid_retry_off = 0; lid_sleep_at = 0; }
		/* a DS game: the panels dark now, the sleep 1.5 s later, so the game sees its lid close first (BananaDS and
		 * SuperDrastic read the hall sensor too: its sleep mode, its lid sound); opened before that, no sleep */
		if (lid_closed && cfg.lid_sleep && ds_game()) { lid_sleep_at = now + 1.5; backlights_power(0); say("lid shut in a DS game: asleep in 1.5 s"); }
		else if (lid_closed && cfg.lid_sleep) suspend_now("lid closed");
		else if (!lid_closed || !cfg.lid_sleep) { backlights_power(!lid_closed); if (!lid_closed) panel_off_again(); }	/* no sleep: at least the panels go dark */
		return;
	}
	if (e->type == EV_SW && e->code == SW_HEADPHONE_INSERT) {
		if (headphones != e->value) { headphones = e->value; audio_route(); }
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

int main(int argc, char **argv)
{
	(void)argc; (void)argv;
	openlog("bananad", LOG_PID, LOG_DAEMON);
	test_lid = getenv("BANANAD_TEST_LID");
	load_conf("/etc/banana/bananad.conf");
	load_conf("/data/banana/bananad.conf");
	load_timezone();
	/* the LEDs at the user's level first: everything below (the inputs, alsactl, the mixer, the second card) takes a
	 * while, and the power LED is the first sign the handheld is starting (U-Boot lit it, the kernel kept it on) */
	leds_update();
	signal(SIGCHLD, SIG_DFL);
	struct sigaction sa = { 0 };
	sa.sa_handler = on_hup;		/* no SA_RESTART: poll() returns, and the loop reloads */
	sigaction(SIGHUP, &sa, NULL);
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
	open_inputs();
	headphones = headphones_in();
	/* the saved volumes first, then the route: the restore also brings back the speakers' switch as it was last
	 * saved (off if headphones were in then), and the route sets it from the jack as it is now */
	restore_settings();
	audio_setup();
	int uev = uevent_open();
	sd2_scan();	/* a card already in the second slot at boot */
	load_drivers_present();
	leds_update();
	last_input = mono();
	double next_battery = 0;
	int low_count = 0;
	say("running: lid %s, auto-sleep %d min, %s", cfg.lid_sleep ? "sleeps" : "ignored", cfg.autosleep_min, cfg.mem_sleep);

	for (;;) {
		struct pollfd pf[MAXDEV + 2];
		int n = 0;
		for (int i = 0; i < ndev; i++) pf[n++] = (struct pollfd){ devfd[i], POLLIN, 0 };
		int ino_at = -1, uev_at = -1;
		if (ino >= 0) { ino_at = n; pf[n++] = (struct pollfd){ ino, POLLIN, 0 }; }
		if (uev >= 0) { uev_at = n; pf[n++] = (struct pollfd){ uev, POLLIN, 0 }; }
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
		if (!game) {
			if (cfg.autosleep_min > 0 && !awake) { double d = cfg.autosleep_min * 60.0 - (tnow - last_input); if (d > 0 && d < wait) wait = d; }
			if (cfg.dim_min > 0 && !dimmed) { double d = cfg.dim_min * 60.0 - (tnow - last_input); if (d > 0 && d < wait) wait = d; }
		}
		if (lid_closed && cfg.lid_sleep && !lid_retry_off) { double d = 5.0 - (tnow - last_input); if (d > 0 && d < wait) wait = d; }
		if ((save_volume_at > 0 || save_backlight_at > 0) && wait > 0.5) wait = 0.5;
		if (power_down && wait > 0.1) wait = 0.1;
		if (lid_sleep_at > 0) { double d = lid_sleep_at - tnow; if (d < wait) wait = d > 0 ? d : 0; }
		if (menu_sleep_at > 0) { double d = menu_sleep_at - tnow; if (d < wait) wait = d > 0 ? d : 0; }
		int timeout = wait <= 0 ? 20 : (int)(wait * 1000) + 20;
		poll(pf, n, timeout);
		double now = mono();
		if (reload) {
			reload = 0;
			load_conf("/etc/banana/bananad.conf");
			load_conf("/data/banana/bananad.conf");
			load_timezone();
			save_volume_at = mono() + 2;	/* the settings screen may have set the volume: keep it as the keys' is kept */
			leds_last = -2;
			leds_update();
			say("settings read again: lid %s, auto-sleep %d min, LEDs %d", cfg.lid_sleep ? "sleeps" : "ignored", cfg.autosleep_min, cfg.led_brightness);
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
		if (uev_at >= 0 && (pf[uev_at].revents & POLLIN) && uevent_read(uev)) leds_update();
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
			if (bt) audio_route();
			if (inputs) {
				usleep(200000);	/* udev/mdev sets the node's permissions */
				open_inputs();
			}
		}

		if (power_down && now - power_since >= cfg.power_long_ms / 1000.0) { power_down = 0; shutdown_now("power held"); }

		/* idle in the menu (no game running): the panels dim, then it sleeps */
		game = pid_of("/run/banana/game.pid") != 0;
		if (cfg.dim_min > 0 && !dimmed && !game && !lid_closed && now - last_input >= cfg.dim_min * 60.0) dim();
		/* (not while charging: a sleep would put the charging light out; the menu stays dimmed instead) */
		if (cfg.autosleep_min > 0 && !game && now - last_input >= cfg.autosleep_min * 60.0 && !charger_online() &&
		    !pid_of("/run/banana/awake.pid"))
			suspend_now("idle");
		if (lid_sleep_at > 0 && now >= lid_sleep_at) { lid_sleep_at = 0; if (lid_closed) suspend_now("lid closed"); }
		if (menu_sleep_at > 0 && now >= menu_sleep_at) { menu_sleep_at = 0; suspend_now("the menu's Sleep"); }
		/* started or woken with the lid shut */
		if (lid_closed && cfg.lid_sleep && !lid_retry_off && lid_sleep_at == 0 && now - last_input > 5) suspend_now("lid still closed");

		if (now >= next_battery) {
			next_battery = now + 60;
			leds_update();	/* a capacity change has no uevent on every gauge */
			int pct = battery_discharging_pct();
			low_count = pct >= 0 && pct <= cfg.battery_off_pct ? low_count + 1 : 0;
			if (low_count >= 2) shutdown_now("battery empty");	/* two readings in a row: not a load spike */
		}
	}
}
