/* bananad: Banana's power daemon for the Retroid Pocket Classic (SM4450).
 *
 *   - Sleep: a short press of the power button, or (in the menu) a few idle minutes put the handheld into
 *     suspend-to-RAM. The running game is paused first (SIGSTOP to its process group) and resumed after, the hooks
 *     in /etc/banana/sleep.d run around it, and banana-ui is told to draw its panel again. Pressing power wakes it
 *     (the device tree marks it a wakeup source).
 *   - A long press of the power button shuts down cleanly.
 *   - Volume keys change the mixer; F (BTN_MODE) + volume changes the backlight.
 *   - A battery about to run out shuts the handheld down before the data partition can be cut off mid-write.
 *   - The LEDs: the power one green, red at 20%, blinking red at 5%; the charging one amber while the charger is
 *     plugged in and the battery isn't full (steady: from the charger's online state, not the gauge's status, which
 *     says "Charging" only for a moment). The kernel blinks the red one (its timer trigger): bananad only changes
 *     them when the power state changes (power_supply uevents, and the battery poll).
 *   - Brightness and volume are kept across reboots (/data/banana/backlight, /data/banana/asound.state).
 *   - Sound goes where it should: PipeWire (started by rcS) owns the RPC's audioreach card, and bananad mixes the
 *     DSP's frontends into the speaker I2S or the WCD937x codec by hand (no UCM profile does it on this image),
 *     because the DSP takes the mixer values it finds when a frontend's graph is instantiated. The route it chose is
 *     published in /run/banana/audio-route and spelled out in /run/banana/asound.conf for anything that opens the
 *     hardware directly (ALSA's *default* is PipeWire's plugin now, not that file); the volume keys change the card
 *     that plays.
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
	int power_long_ms;		/* holding power this long shuts down */
	int battery_off_pct;	/* at or under this, discharging: shut down */
	char mem_sleep[16];		/* deep (suspend to RAM) or s2idle */
	int led_brightness;		/* 0..255; 0: the LEDs stay off */
	int led_low, led_critical;	/* percent: red, blinking red */
	int fast_resume;		/* devices resume in parallel (/sys/power/pm_async) */
	double sleep_check_hours;	/* asleep: wake this often to look at the battery (0: never) */
	int sleep_poweroff_pct;	/* found at or under this while asleep: shut down cleanly */
	double dim_min;			/* idle minutes in the menu before the panel dims (0: never) */
	int dim_pct;			/* dimmed to this percent of their brightness */
	int autosleep_set;		/* autosleep_minutes was written down somewhere: it is asked for, not defaulted */
} cfg = { "auto", 5, 8, 10, 2000, 3, "deep", 128, 20, 5, 1, 4, 5, 2, 30, 0 };

static int devfd[MAXDEV], ndev;
static void leds_update(void);
static void audio_route(void);
static int headphones_in_settled(void);
static void undim(void);
static void find_bottom_bl(void);
static char bottom_bl[64];	/* the panel's backlight name; banana-ui's idle dim owns it while it runs */
static int headphones;
static int fn_held, select_held, power_down;
static int panel_blanked;	/* bananad blanked the panel (a suspend it could not do): the next key brings it back */
static double power_since, last_input, ignore_power_until;
static void sleep_quiet_begin(void);	/* the panel dark and the codec muted, before the DSP sleeps */
static void sleep_quiet_end(void);	/* and back, on the way out of it */

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

/* load_conf <file> <asked>: read the settings. <asked> is 1 for the file on the data partition -- the one a person
 * or the settings screen wrote -- and 0 for the image's own defaults, which are only the starting point. It decides
 * which numbers count as a wish (see the idle rule in main()). */
static void load_conf(const char *path, int asked)
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
		else if (!strcmp(k, "autosleep_minutes")) { cfg.autosleep_min = atoi(v); if (asked) cfg.autosleep_set = 1; }
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
 * Pressing power (or an idle timer): the game is paused, the pre-sleep hooks run (Wi-Fi down), the card is flushed
 * (this kernel skips its own sync), an RTC alarm is set for the battery check, and the SoC suspends to RAM. On waking,
 * the game and the menu go on first, then the post-sleep hooks in the background. Woken by the battery check:
 * straight back to sleep, without running anything again. */
static void shutdown_now(const char *why);
static int battery_discharging_pct(void);
static int sleep_fallback;		/* 0 as configured; 1 devices one at a time; 2 also s2idle (a suspend failed) */

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

/* the backlights off and on again without changing their level */
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

static int charger_online(void);
static void suspend_now(const char *why)
{
	double t0 = mono();
	pid_t game = pid_of("/run/banana/game.pid");
	pid_t pg = game ? getpgid(game) : 0;
	if (pg > 0) kill(-pg, SIGSTOP);	/* an emulator's dynarec mustn't run on while the clocks drop */
	/* the menu's ambient loop, if any: stop it and have it release the DSP before the ADSP sleeps (SIGHUP), and give
	 * it a moment to close the device; then darken the panel and mute the codec so nothing flashes or pops */
	if (!game) { pid_t u = ui_pid(); if (u > 0) { kill(u, SIGHUP); usleep(200000); } }
	sleep_quiet_begin();
	hooks("pre", 0);
	sync();
	sleep_mode();
	int cycles = 0, rtc_wakes = 0, failures = 0;
	double asleep = 0;
	const char *woke = "?";
	for (;;) {
		time_t alarm = rtc_alarm((int)(cfg.sleep_check_hours * 3600));
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
			/* it didn't suspend: devices one at a time, then the lighter s2idle, then give up and blank the panel */
			failures++;
			sleep_log("suspend failed (%s)%s", strerror(err), sleep_fallback < 2 ? ": trying the safer way" : "");
			if (sleep_fallback < 2 && failures <= 2) { sleep_fallback++; sleep_mode(); usleep(200000); continue; }
			woke = "failed";
			/* It never slept: leave the panel dark -- that is what a sleep would have looked like, and the
			 * next press brings it back (see panel_blanked in handle()) -- and let the game run again, because
			 * a frozen emulator behind a dark screen is worse. Nothing here is a wake, so the wake bookkeeping
			 * below is skipped. */
			if (pg > 0) kill(-pg, SIGCONT);
			sleep_quiet_end();
			backlights_power(0);
			panel_blanked = 1;
			sleep_log("could not suspend after %d tries: the panel is dark until the next press", failures);
			last_input = mono();
			return;
		}
		asleep += (b1.tv_sec - b0.tv_sec) + (b1.tv_nsec - b0.tv_nsec) / 1e9;
		/* why it woke: the battery check's alarm, or the power button */
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
		woke = "power button";
		break;
	}
	/* awake: the game and the menu first */
	double t_wake = mono();
	backlights_power(1);
	sleep_quiet_end();
	if (pg > 0) kill(-pg, SIGCONT);
	/* The compositor's output too: a panel its driver disabled on the way down can come back dark, with cage's
	 * frames going nowhere, and only a modeset clears it. cage has no session manager to hear that the system
	 * slept (Banana's seat is deliberately not VT-bound), so it re-modesets on SIGUSR1 -- the pid run-retroarch
	 * wrote when it started it. */
	{
		pid_t cg = pid_of("/run/banana/cage.pid");
		if (cg > 0) kill(cg, SIGUSR1);
	}
	pid_t ui = ui_pid();
	if (ui && !game) kill(ui, SIGUSR1);	/* draws the panel again (the clock and the battery moved on) */
	double t_back = mono();
	undim();
	hooks("post", 1);
	/* the press that woke us comes in now: it is not a new request */
	drain_inputs();
	leds_update();
	int jack = headphones_in_settled();
	if (jack != headphones) { headphones = jack; audio_route(); }
	ignore_power_until = mono() + 1.0;
	power_down = 0;
	last_input = mono();
	sleep_log("awake (%s) after %.0f s asleep%s%s, game and menu back %.1f ms after the kernel", woke, asleep,
		rtc_wakes ? ", battery checked" : "", asleep < 5.0 ? " (it did not take)" : "", (t_back - t_wake) * 1000);
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

/* ---- sound: the card that plays ----------------------------------------------------------------------------------- */
static int card_play = 0;	/* the card sound plays on */
static int card_ar = -1;	/* the audioreach card (the RPC): its DSP graph is routed by mixer controls, no UCM */

/* the card, from /proc/asound/cards (" 0 [sm8450 ]: simple-card - ...") */
static void audio_find_cards(void)
{
	FILE *f = fopen("/proc/asound/cards", "r");
	if (!f) return;
	char ln[256];
	while (fgets(ln, sizeof ln, f)) {
		int n;
		if (sscanf(ln, " %d [", &n) != 1) continue;
		card_play = n;
		/* the RPC's card: audioreach, with the frontends (MultiMedia1/2) mixed into the speaker I2S and the codec
		 * by mixer controls whose names say so -- what alsa-ucm-conf's profile would set. Nothing does it on this
		 * image, so without this the DSP says "no backend DAIs enabled" and plays nothing. */
		char cmd[128];
		snprintf(cmd, sizeof cmd, "amixer -c %d scontrols 2>/dev/null", n);
		FILE *p = popen(cmd, "r");
		char cl[256];
		while (p && fgets(cl, sizeof cl, p)) if (strstr(cl, "SENARY_MI2S_RX Audio Mixer")) { card_ar = n; break; }
		if (p) pclose(p);
		break;	/* the first card is the one sound plays on */
	}
	fclose(f);
}

static void cset(int card, const char *name, const char *val)
{
	char c[8], spec[128];
	snprintf(c, sizeof c, "%d", card);
	snprintf(spec, sizeof spec, "name=%s", name);
	char *argv[] = { "amixer", "-c", c, "cset", spec, (char *)val, NULL };
	int st = run(argv);
	if (st) say("sound: amixer cset %s %s -> %d", name, val, st);
}

/* the switch controls (HPHL, HPHL_RDAC, ...) are simple-mixer elements: cset name= doesn't reach them on this
 * codec, sset does */
static void sset(int card, const char *name, const char *val)
{
	char c[8];
	snprintf(c, sizeof c, "%d", card);
	char *argv[] = { "amixer", "-c", c, "sset", (char *)name, (char *)val, NULL };
	int st = run(argv);
	if (st) say("sound: amixer sset %s %s -> %d", name, val, st);
}

/* amixer_get <card> <control> <out> <size>: the values the control holds, "" when there is no such control. Asked
 * with `cset` and no value (the only way to name a control with spaces in it), amixer answers with a line that ends
 * in ": values=on,on" -- the label before the colon is empty on the DSP's mixers, so the colon is the anchor, not
 * the word "value:" many codecs print. */
static int amixer_get(int card, const char *name, char *out, size_t n)
{
	char c[8], cmd[256];
	snprintf(c, sizeof c, "%d", card);
	snprintf(cmd, sizeof cmd,
		"amixer -c %s cset name='%s' 2>/dev/null | sed -n 's/.*: *values=//p' | head -n 1", c, name);
	FILE *p = popen(cmd, "r");
	if (!p) { out[0] = 0; return 0; }
	int got = fgets(out, (int)n, p) ? (int)strlen(out) : 0;
	pclose(p);
	while (got && (out[got - 1] == '\n' || out[got - 1] == '\r')) out[--got] = 0;
	return got;
}

/* A sleep: the panel dark and the codec's playback volumes muted before the ADSP's clock stops, so the AMOLED does not
 * flash and the amplifiers do not pop; both put back on the way out. The brightness levels are held here and not saved
 * to the card: the user's own level is untouched. */
static char sleep_bl_name[4][128];
static int sleep_bl_val[4], sleep_bl_n;
static char sleep_rx0[32], sleep_rx1[32];
static void sleep_quiet_begin(void)
{
	sleep_bl_n = 0;
	DIR *d = opendir("/sys/class/backlight");
	struct dirent *e;
	while (d && (e = readdir(d)) && sleep_bl_n < 4) {
		if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof sleep_bl_name[0]) continue;
		char p[600];
		snprintf(p, sizeof p, "/sys/class/backlight/%.255s/brightness", e->d_name);
		int cur = read_int(p, -1);
		if (cur < 0) continue;
		snprintf(sleep_bl_name[sleep_bl_n], sizeof sleep_bl_name[0], "%s", e->d_name);
		sleep_bl_val[sleep_bl_n++] = cur;
		write_file(p, "0");
	}
	if (d) closedir(d);
	if (card_ar >= 0) {
		amixer_get(card_ar, "RX_RX0 Digital Volume", sleep_rx0, sizeof sleep_rx0);
		amixer_get(card_ar, "RX_RX1 Digital Volume", sleep_rx1, sizeof sleep_rx1);
		cset(card_ar, "RX_RX0 Digital Volume", "0");
		cset(card_ar, "RX_RX1 Digital Volume", "0");
	}
}
static void sleep_quiet_end(void)
{
	for (int i = 0; i < sleep_bl_n; i++) {
		char p[600], v[32];
		snprintf(p, sizeof p, "/sys/class/backlight/%.255s/brightness", sleep_bl_name[i]);
		snprintf(v, sizeof v, "%d", sleep_bl_val[i]);
		write_file(p, v);
	}
	sleep_bl_n = 0;
	if (card_ar >= 0) {
		if (sleep_rx0[0]) cset(card_ar, "RX_RX0 Digital Volume", sleep_rx0);
		if (sleep_rx1[0]) cset(card_ar, "RX_RX1 Digital Volume", sleep_rx1);
	}
	sleep_rx0[0] = sleep_rx1[0] = 0;
}

/* The audioreach card (the RPC): the frontends (MultiMedia1, MultiMedia2 -- the menu and RetroArch each open one)
 * have to be mixed into the speaker I2S (the AW88266 amps) or into the WCD937x codec (headphones) by hand. A UCM
 * profile would do this; this image has none, and without it the DSP refuses every open with "no backend DAIs
 * enabled for MultiMedia1 Playback". */
static void audio_route_ar(void)
{
	if (card_ar < 0) return;
	const char *spk = headphones ? "off" : "on", *hp = headphones ? "on" : "off";
	for (int i = 1; i <= 2; i++) {
		char n[64];
		snprintf(n, sizeof n, "SENARY_MI2S_RX Audio Mixer MultiMedia%d", i);
		cset(card_ar, n, spk);
		snprintf(n, sizeof n, "RX_CODEC_DMA_RX_0 Audio Mixer MultiMedia%d", i);
		cset(card_ar, n, hp);
	}
	/* Say what was chosen, for whoever opens a stream next. The DSP latches these switches when the frontend's
	 * graph is instantiated -- at *that* program's open, not at ours -- and the graph is torn down again when its
	 * last stream closes, so the route has to be written every time by whoever is about to play: banana-ui before
	 * its first cue after a game, run-retroarch before RetroArch opens the device. They read the jack's decision
	 * here rather than guessing "speakers", so plugging in does not end up un-muting the amps. */
	FILE *f = fopen("/run/banana/audio-route.new", "w");
	if (f) {
		/* both frontends: the card has two (MultiMedia1/2) and they are interchangeable, and which one a
		 * sound server ends up on is not ours to choose any more (PipeWire's sink is the second) */
		for (int i = 1; i <= 2; i++) {
			fprintf(f, "SENARY_MI2S_RX Audio Mixer MultiMedia%d=%s,%s\n", i, spk, spk);
			fprintf(f, "RX_CODEC_DMA_RX_0 Audio Mixer MultiMedia%d=%s,%s\n", i, hp, hp);
		}
		fclose(f);
		rename("/run/banana/audio-route.new", "/run/banana/audio-route");
	}
}

/* The jack's answer, taken twice in a row before it is believed. The WCD937x's MBHC answers before it has measured
 * the socket: on the Retroid Pocket Classic the very first read of a boot (and, worse, a first read after a wake)
 * can say "headphones in" with nothing plugged in, and acting on that routes the sound to a codec feeding an empty
 * jack -- a silent handheld, until something else writes the speaker route back (which is what made the menu come
 * back to life after a game had run). Two answers agreeing is the cheapest proof the codec has finished measuring;
 * a real plug-in still takes effect within a few hundred milliseconds, and its own event drives the same variable
 * afterwards. */
static int headphones_in_settled(void)
{
	int a = headphones_in();
	for (int i = 0; i < 6; i++) {
		usleep(200000);
		int b = headphones_in();
		if (a == b) return b;
		a = b;
	}
	return a;	/* never agreed: the codec is still moving; take the answer we just had */
}

static void audio_route(void)
{
	static int last = -1;
	// say it when the answer moves: on the RPC this is the switch between the speaker I2S and the codec, and a
	// jack that changes its mind with nothing plugged in is exactly what silences the handheld
	if (card_ar >= 0 && last != headphones)
		say("sound: routing to the %s (jack read %s)", headphones ? "headphones" : "speakers",
		    headphones_in() ? "in" : "out");
	last = headphones;
	audio_route_ar();
	/* The audioreach card plays at 48 kHz (the rate the DSP graph and the amps expect), so "default"'s playback and
	 * capture are pinned to S16 stereo 48 kHz: every open gets the same stream instead of renegotiating it, and
	 * ALSA's plug resamples a core that asks for another rate. */
	char play[160], cap[160];
	if (card_play >= 0)
		snprintf(play, sizeof play, "{ type plug slave { pcm \"hw:%d\" format S16_LE channels 2 rate 48000 } }", card_play);
	else
		snprintf(play, sizeof play, "\"null\"");
	snprintf(cap, sizeof cap, "%s", play);
	char conf[1600];
	int n;
	n = snprintf(conf, sizeof conf,
			"# bananad: where sound plays now (%s)\n"
			"pcm.!default { type asym playback.pcm %s capture.pcm %s }\n",
			headphones ? "headphones" : "speakers", play, cap);
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
		const char *mix = cfg.mixer;
		if (!strcmp(mix, "auto"))
			/* PipeWire's sink is the card's *second* frontend (hw:0,1, "MultiMedia2"): the DSP allows one
			 * frontend at a time and the first (hw:0,0) refuses to open on this card at all, so that is where
			 * anything the menu or a game plays goes -- and its volume control is the one the keys own. */
			mix = card_ar >= 0 ? "stream1.vol_ctrl1 MultiMedia2 Playback Volu" : "Master";
		fprintf(f, "%d %s\n", card_play, mix);
		fclose(f);
		rename("/run/banana/mixer.new", "/run/banana/mixer");
	}
	/* a running RetroArch game opens its sound again where it plays now: alsa-lib reads its configuration once per
	 * process (Banana's RetroArch patch forgets it on AUDIO_REINIT). The menu's own sound watches the file itself. */
	int rc = open("/run/banana/retroarch.cmd", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (rc >= 0) {
		if (write(rc, "AUDIO_REINIT\n", 13) < 0) {}
		close(rc);
	}
	say("sound: playback on card %d (%s), 48 kHz", card_play, headphones ? "headphones" : "speakers");
}

static void audio_setup(void)
{
	audio_find_cards();
	if (card_play < 0) { audio_route(); return; }	/* no sound card at all (QEMU): nothing to set */
	audio_route();
	/* The RPC's DSP reads the routes when a stream is instantiated, which is why rcS puts them in before the menu
	 * opens its own. rcS runs its own wait but gives up after 6 s, and a card that enumerated later than that leaves
	 * the handheld silent for the whole session with nothing in the log to say so. Look again here, briefly, and
	 * only when the jack says nothing is plugged in: with headphones in, the speaker route *should* read off, and
	 * waiting for it to read on is waiting for something wrong. Then the amps' level -- not the frontend's
	 * `stream0.vol_ctrl0 ...`, which is the user's volume (the volume keys and the saved setting own it). */
	if (card_ar >= 0) {
		/* One line every boot, because this codec's jack is what decides where sound goes and it is known to answer
		 * before it has measured anything: "headphones in" here with nothing plugged in is the whole story of a
		 * silent menu -- a game re-asserts the speaker route itself, which is why games do have sound. */
		say("sound: the jack says %s at start", headphones ? "headphones in" : "nothing in");
		/* ROCKNIX's UCM2 BootSequence levels for this card (there is no UCM on this image): the codec's and the RX
		 * macro's output volumes and the mic's. Set here as well as in rcS, because a card that enumerated after
		 * rcS's wait would otherwise run at the reset levels -- silent or very quiet -- all session. */
		cset(card_ar, "RX_RX0 Digital Volume", "84");
		cset(card_ar, "RX_RX1 Digital Volume", "84");
		cset(card_ar, "HPHL Volume", "20");
		cset(card_ar, "HPHR Volume", "20");
		cset(card_ar, "ADC2 Volume", "10");
		cset(card_ar, "Left PCM Playback Volume", "460");
		cset(card_ar, "Right PCM Playback Volume", "460");
		/* The headphone path, set here rather than only in rcS: bananad's restore_settings() replays the card's
		 * asound.state (saved by alsactl store when the volume changed) after rcS has run, which puts these back to
		 * their reset state, so rcS's own copy is not enough. The speaker path (SENARY MI2S -> AW88166) is separate
		 * and does not need them. */
		sset(card_ar, "HPHL", "on");
		sset(card_ar, "HPHR", "on");
		sset(card_ar, "HPHL_RDAC", "on");
		sset(card_ar, "HPHR_RDAC", "on");
		sset(card_ar, "HPHL_COMP", "on");
		sset(card_ar, "HPHR_COMP", "on");
		cset(card_ar, "RX HPH Mode", "CLS_H_LOHIFI");
		cset(card_ar, "RX_HPH PWR Mode", "LOHIFI");
		cset(card_ar, "RX_MACRO RX0 MUX", "AIF1_PB");
		cset(card_ar, "RX_MACRO RX1 MUX", "AIF1_PB");
		cset(card_ar, "RX INT0_1 MIX1 INP0", "RX0");
		cset(card_ar, "RX INT1_1 MIX1 INP0", "RX1");
		cset(card_ar, "RX INT0 DEM MUX", "CLSH_DSM_OUT");
		cset(card_ar, "RX INT1 DEM MUX", "CLSH_DSM_OUT");
	}
	if (card_ar >= 0 && !headphones) {
		char route[64], val[64];
		snprintf(route, sizeof route, "SENARY_MI2S_RX Audio Mixer MultiMedia1");
		int tries = 0;
		while (tries < 30) {
			audio_route_ar();
			amixer_get(card_ar, route, val, sizeof val);
			if (strstr(val, "on")) break;
			usleep(100000);
			tries++;
		}
		if (tries == 30)
			say("sound: %s still reads '%s' 3 s after rcS gave up", route, val[0] ? val : "nothing");
	}
}

static void volume(int dir)
{
	/* In a game the volume keys are RetroArch's: it changes its own volume live and shows its OSD. The card's
	 * mixer is the *menu's* level, and a game's frontend volume is only latched when its graph is instantiated
	 * (at the menu's next open), so editing it during a game reads as "nothing happened, then it applied when I
	 * quit" -- and all the way down it is a silent handheld with nothing on screen to say why. Hand the press to
	 * RetroArch instead, through the FIFO run-retroarch already keeps open (the AUDIO_REINIT path). */
	if (pid_of("/run/banana/game.pid") > 0)
	{
		int fd = open("/run/banana/retroarch.cmd", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd >= 0)
		{
			/* RetroArch's volume commands are 0.5 dB a step (command_event_set_volume), far finer than the
			 * menu's 5% (2.5 dB), so send five: a press is then the same step in a game as in the menu. */
			const char *c = dir > 0 ? "VOLUME_UP\n" : "VOLUME_DOWN\n";
			for (int i = 0; i < 5; i++)
				if (write(fd, c, strlen(c)) < 0) break;
			close(fd);
			return;
		}
		/* (no FIFO: a game that is not Banana's RetroArch -- leave the mixer to the script below) */
	}
	/* the menu's own volume: the script changes the control /run/banana/mixer names (see it) */
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

static void brightness(int dir, int step_pct)
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
		int step = max * step_pct / 100;
		if (step < 1) step = 1;
		int floor = max * 2 / 100;	/* a night-mode floor: 2%, dim but never a dead-looking panel */
		if (floor < 1) floor = 1;
		int nv = cur + dir * step;
		if (nv < floor) nv = floor;
		if (nv > max) nv = max;
		snprintf(v, sizeof v, "%d", nv);
		write_file(p, v);
		if (max > 0) pct = nv * 100 / max;
	}
	closedir(d);
	save_backlight_at = mono() + 2;
	if (pct >= 0) osd("brightness", pct);
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

/* the panel's backlight, from the backlight-path script. While banana-ui is managing its own idle dim (it writes
 * /run/banana/bottom-ui, see ui/src/main.cpp), the menu's own dim leaves that backlight to it: it would otherwise
 * dim twice and undo banana-ui's restore. Only the name is kept: the empty string means nothing to single out. */
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
	/* published for banana-ui, whose idle timer dims the panel itself (ui/src/main.cpp) */
	FILE *f = fopen("/run/banana/backlight-bottom.new", "w");
	if (f) {
		fprintf(f, "%s\n", path);
		fclose(f);
		rename("/run/banana/backlight-bottom.new", "/run/banana/backlight-bottom");
	}
	say("panel backlight: %s", bottom_bl);
}

/* idle in the menu: the panel down to dim_percent of what it was, back at the first press (the level is
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
	/* started again while the panel was dimmed (/run/banana/dim: the level before): undimmed, as the last one would */
	int dimmed_from = read_int("/run/banana/dim", -1);
	if (dimmed_from > 0 && dimmed_from <= 1000) { backlight_set_permille(dimmed_from); unlink("/run/banana/dim"); }
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
 * no use for the charging LED: the gauge says "Charging" only for a moment after the cable goes in, then
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
}

static void handle(const struct input_event *e)
{
	double now = mono();
	if (e->type == EV_KEY) { last_input = now; undim(); if (panel_blanked) { panel_blanked = 0; backlights_power(1); } }
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
	case BTN_SELECT: select_held = e->value; break;
	case KEY_POWER:
		if (now < ignore_power_until) break;
		if (e->value) { power_down = 1; power_since = now; }
		else if (power_down) { power_down = 0; suspend_now("power button"); }
		break;
	/* F + volume: the backlight, in games too. Select + volume: the same in a game (in the menu Select is the
	 * search key), in 10% steps down to a 2% night-mode floor. */
	case KEY_VOLUMEUP:
		if (e->value) { int bright = fn_held || (select_held && pid_of("/run/banana/game.pid") > 0); if (bright) brightness(1, select_held && !fn_held ? 10 : cfg.brightness_step); else volume(1); }
		break;
	case KEY_VOLUMEDOWN:
		if (e->value) { int bright = fn_held || (select_held && pid_of("/run/banana/game.pid") > 0); if (bright) brightness(-1, select_held && !fn_held ? 10 : cfg.brightness_step); else volume(-1); }
		break;
	}
}

/* percent, or -1 when there is no battery or it is charging */
static int battery_discharging_pct(void)
{
	/* (the gauge says "Discharging" while a charger charges it, see charger_online: plugged in at 2% must not
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

/* The idle timer of a slate (the Retroid Pocket Classic). There is nothing to close, so an idle suspend has no
 * meaning -- and on this SoC it still costs the picture: the DSI panel and its backlight resume is a gap ROCKNIX has
 * not closed either. So an idle handheld dims and stays awake, which is the battery saving you can see coming. The
 * power button still sleeps it, and writing autosleep_minutes in /data/banana/bananad.conf (Settings > Power) asks
 * for the timer back: the image's own default is not an ask. */
static void slate_sleep_default(void)
{
	if (cfg.autosleep_set || cfg.autosleep_min <= 0)
		return;
	say("sleep: idle only dims (%g min), never suspends; autosleep_minutes asks for the timer", cfg.dim_min);
	cfg.autosleep_min = 0;
}

int main(int argc, char **argv)
{
	(void)argc; (void)argv;
	openlog("bananad", LOG_PID, LOG_DAEMON);
	load_conf("/etc/banana/bananad.conf", 0);
	load_conf("/data/banana/bananad.conf", 1);
	load_timezone();
	signal(SIGCHLD, SIG_DFL);
	struct sigaction sa = { 0 };
	sa.sa_handler = on_hup;		/* no SA_RESTART: poll() returns, and the loop reloads */
	sigaction(SIGHUP, &sa, NULL);
	mkdir("/run/banana", 0755);
	find_bottom_bl();	/* which backlight banana-ui's idle dim owns */

	int ino = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (ino >= 0) inotify_add_watch(ino, "/dev/input", IN_CREATE | IN_DELETE);
	open_inputs();
	headphones = headphones_in_settled();
	slate_sleep_default();	/* the idle rule: dim but don't suspend */
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
	say("running: auto-sleep %d min, %s", cfg.autosleep_min, cfg.mem_sleep);

	for (;;) {
		struct pollfd pf[MAXDEV + 2];
		int n = 0;
		for (int i = 0; i < ndev; i++) pf[n++] = (struct pollfd){ devfd[i], POLLIN, 0 };
		int ino_at = -1, uev_at = -1;
		if (ino >= 0) { ino_at = n; pf[n++] = (struct pollfd){ ino, POLLIN, 0 }; }
		if (uev >= 0) { uev_at = n; pf[n++] = (struct pollfd){ uev, POLLIN, 0 }; }
		/* sleep until something is due: the battery look (a minute), the dim or the auto-sleep deadline, a pending
		 * save, the power button's hold; nothing wakes bananad otherwise */
		double tnow = mono(), wait = next_battery - tnow;
		int game = pid_of("/run/banana/game.pid") != 0;
		/* a job that must not be slept through (banana-scrape's downloads): its pid in /run/banana/awake.pid; looked
		 * at again every 5 s, so the idle sleep comes once it is over */
		int awake = pid_of("/run/banana/awake.pid") != 0;
		if (awake && wait > 5) wait = 5;
		/* (only deadlines still ahead: one already passed was acted on after the last poll, or is held back by what
		 * the action checks (the charger, no backlight) and a uevent or an input event brings it back; counted
		 * here it spun this loop every 20 ms, on the charger for as long as the menu sat idle) */
		if (!game) {
			if (cfg.autosleep_min > 0 && !awake) { double d = cfg.autosleep_min * 60.0 - (tnow - last_input); if (d > 0 && d < wait) wait = d; }
			if (cfg.dim_min > 0 && !dimmed) { double d = cfg.dim_min * 60.0 - (tnow - last_input); if (d > 0 && d < wait) wait = d; }
		}
		if ((save_volume_at > 0 || save_backlight_at > 0) && wait > 0.5) wait = 0.5;
		if (power_down && wait > 0.1) wait = 0.1;
		int timeout = wait <= 0 ? 20 : (int)(wait * 1000) + 20;
		poll(pf, n, timeout);
		double now = mono();
		if (reload) {
			reload = 0;
			cfg.autosleep_set = 0;
			load_conf("/etc/banana/bananad.conf", 0);
			load_conf("/data/banana/bananad.conf", 1);
			slate_sleep_default();	/* the settings screen may have just written a timer, or taken it away */
			load_timezone();
			save_volume_at = mono() + 2;	/* the settings screen may have set the volume: keep it as the keys' is kept */
			leds_last = -2;
			leds_update();
			say("settings read again: auto-sleep %d min, LEDs %d", cfg.autosleep_min, cfg.led_brightness);
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
			char buf[4096];
			while (read(ino, buf, sizeof buf) > 0) {}
			usleep(200000);	/* udev/mdev sets the node's permissions */
			open_inputs();
		}

		if (power_down && now - power_since >= cfg.power_long_ms / 1000.0) { power_down = 0; shutdown_now("power held"); }

		/* idle in the menu (no game running): the panel dims, then it sleeps */
		game = pid_of("/run/banana/game.pid") != 0;
		if (cfg.dim_min > 0 && !dimmed && !game && now - last_input >= cfg.dim_min * 60.0) dim();
		/* (not while charging: a sleep would put the charging light out; the menu stays dimmed instead) */
		if (cfg.autosleep_min > 0 && !game && now - last_input >= cfg.autosleep_min * 60.0 && !charger_online() &&
		    !pid_of("/run/banana/awake.pid"))
			suspend_now("idle");

		if (now >= next_battery) {
			next_battery = now + 60;
			leds_update();	/* a capacity change has no uevent on every gauge */
			int pct = battery_discharging_pct();
			low_count = pct >= 0 && pct <= cfg.battery_off_pct ? low_count + 1 : 0;
			if (low_count >= 2) shutdown_now("battery empty");	/* two readings in a row: not a load spike */
		}
	}
}
