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
 *     charging. The kernel drives them (the charger's LED trigger, the timer trigger for the blink): bananad only
 *     changes them when the battery's state changes, which the kernel announces (power_supply uevents).
 *   - Brightness and volume are kept across reboots (/data/banana/backlight, /data/banana/asound.state).
 *   - Sound goes where it should with no sound server: the RG DS has one card (the rk817 codec, its playback mux set
 *     to HP, which drives the speaker amplifier and the jack, as ROCKNIX sets it); the RG DS Plus has two (the
 *     aw88166 speaker amplifiers, and the rk817 for the headphones and the microphone). Playback goes to the
 *     speakers, or to the rk817 while headphones are in (the jack's switch event); recording always comes from the
 *     rk817. The route is ALSA's default device, written to /run/banana/asound.conf (/etc/asound.conf points
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
} cfg = { "auto", 5, 8, 10, 1, 2000, 3, "deep", 128, 20, 5, 1, 4, 5 };

static int devfd[MAXDEV], ndev;
static void leds_update(void);
static void audio_route(void);
static int headphones;
static int fn_held, power_down, lid_closed;
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
		else if (!strcmp(k, "power_long_ms")) cfg.power_long_ms = atoi(v);
		else if (!strcmp(k, "battery_shutdown_percent")) cfg.battery_off_pct = atoi(v);
		else if (!strcmp(k, "mem_sleep")) snprintf(cfg.mem_sleep, sizeof cfg.mem_sleep, "%s", v);
		else if (!strcmp(k, "led_brightness")) cfg.led_brightness = atoi(v);
		else if (!strcmp(k, "led_low_percent")) cfg.led_low = atoi(v);
		else if (!strcmp(k, "led_critical_percent")) cfg.led_critical = atoi(v);
		else if (!strcmp(k, "fast_resume")) cfg.fast_resume = !strcmp(v, "yes") || !strcmp(v, "1");
		else if (!strcmp(k, "sleep_check_hours")) cfg.sleep_check_hours = atof(v);
		else if (!strcmp(k, "sleep_poweroff_percent")) cfg.sleep_poweroff_pct = atoi(v);
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

static void suspend_now(const char *why)
{
	double t0 = mono();
	pid_t game = pid_of("/run/banana/game.pid");
	pid_t pg = game ? getpgid(game) : 0;
	if (pg > 0) kill(-pg, SIGSTOP);	/* DraStic's dynarec mustn't run on while the clocks drop (ROCKNIX's finding) */
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
			/* it didn't suspend: devices one at a time, then the lighter s2idle, then give up and blank the panels */
			failures++;
			sleep_log("suspend failed (%s)%s", strerror(err), sleep_fallback < 2 ? ": trying the safer way" : "");
			if (sleep_fallback < 2 && failures <= 2) { sleep_fallback++; sleep_mode(); usleep(200000); continue; }
			woke = "failed";
			if (lid_closed) backlights_power(0);	/* can't sleep: at least dark until the lid opens */
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
			continue;	/* enough battery: back to sleep at once */
		}
		read_lid();
		if (lid_closed && cfg.lid_sleep) { woke = "lid shut"; continue; }	/* not meant: back to sleep */
		woke = lid_closed ? "power button" : "lid opened";
		break;
	}
	/* awake: the game and the menu first */
	double t_wake = mono();
	if (pg > 0) kill(-pg, SIGCONT);
	pid_t ui = ui_pid();
	if (ui && !game) kill(ui, SIGUSR1);	/* draws both panels again (the clock and the battery moved on) */
	double t_back = mono();
	hooks("post", 1);
	/* the press that woke us comes in now: it is not a new request. Draining also drops the lid-open event, so the
	 * lid's state is read from the kernel instead */
	drain_inputs();
	read_lid();
	leds_update();
	if (headphones_in() != headphones) { headphones = !headphones; audio_route(); }
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
	if (card_codec < 0) card_codec = 0;
}

static void audio_route(void)
{
	card_play = card_speaker >= 0 && !headphones ? card_speaker : card_codec;
	char conf[512];
	int n = snprintf(conf, sizeof conf,
		"# bananad: where sound plays now (%s)\n"
		"pcm.!default { type asym playback.pcm \"plughw:%d\" capture.pcm \"plughw:%d\" }\n"
		"ctl.!default { type hw card %d }\n",
		card_play == card_speaker ? "speakers" : headphones ? "headphones" : "the codec", card_play, card_codec, card_play);
	FILE *f = fopen("/run/banana/asound.conf.new", "w");
	if (f) {
		fwrite(conf, 1, n, f);
		fclose(f);
		rename("/run/banana/asound.conf.new", "/run/banana/asound.conf");
	}
	say("sound: playback on card %d (%s), recording on card %d", card_play,
	    card_play == card_speaker ? "speakers" : headphones ? "headphones" : "codec", card_codec);
}

static void audio_setup(void)
{
	audio_find_cards();
	/* the rk817's output path: HP drives both its jack and (on the RG DS) the speaker amplifier */
	char c[8];
	snprintf(c, sizeof c, "%d", card_codec);
	char *argv[] = { "amixer", "-q", "-c", c, "sset", "Playback Mux", "HP", NULL };
	run(argv);
	audio_route();
}

static void volume(int dir)
{
	char step[16], c[8];
	snprintf(step, sizeof step, "%d%%%c", cfg.volume_step, dir > 0 ? '+' : '-');
	snprintf(c, sizeof c, "%d", card_play);
	/* the aw88166's own control is "PCM"; the rk817's is "Master" */
	char *control = strcmp(cfg.mixer, "auto") ? cfg.mixer : card_play == card_speaker ? "PCM" : "Master";
	char *argv[] = { "amixer", "-q", "-c", c, "sset", control, step, NULL };
	run(argv);
	save_volume_at = mono() + 2;
}

static void brightness(int dir)
{
	DIR *d = opendir("/sys/class/backlight");
	if (!d) return;
	struct dirent *e;
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
	}
	closedir(d);
	save_backlight_at = mono() + 2;
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

int leds_last = -2;	/* the upper LED's state as last set; -2: set it again */
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
	int level = cfg.led_brightness;
	/* the upper LED: green, red when low, blinking red when critical (not while charging) */
	int state = level <= 0 ? 0 : (!charging && pct >= 0 && pct <= cfg.led_critical) ? 3 : (!charging && pct >= 0 && pct <= cfg.led_low) ? 2 : 1;
	if (state != last) {
		leds_last = state;
		led_set(green, state == 1 ? "on" : "off", level);
		led_set(red, state == 3 ? "blink" : state == 2 ? "on" : "off", level);
		/* the lower LED: amber while charging, driven by the kernel from the charger's state */
		char trig[300];
		snprintf(trig, sizeof trig, "%s-charging", bat);
		if (level <= 0 || !bat[0]) led_set(amber, "off", 0);
		else led_set(amber, trig, level);
		say("LEDs: battery %d%%%s", pct, charging ? ", charging" : "");
	}
}

/* the kernel's uevents (no udev here): a power_supply one means the battery or the charger changed */
static int uevent_open(void)
{
	int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
	if (fd < 0) return -1;
	struct sockaddr_nl a = { .nl_family = AF_NETLINK, .nl_groups = 1 };
	if (bind(fd, (struct sockaddr *)&a, sizeof a)) { close(fd); return -1; }
	return fd;
}

static int uevent_power(int fd)
{
	char buf[8192];
	ssize_t n;
	int hit = 0;
	while ((n = recv(fd, buf, sizeof buf - 1, 0)) > 0) {
		buf[n] = 0;
		for (char *p = buf; p < buf + n; p += strlen(p) + 1)
			if (!strcmp(p, "SUBSYSTEM=power_supply")) hit = 1;
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
		devfd[ndev++] = fd;
	}
	closedir(d);
	read_lid();
}

static void handle(const struct input_event *e)
{
	double now = mono();
	if (e->type == EV_KEY) last_input = now;
	if (e->type == EV_SW && e->code == SW_LID) {
		lid_closed = e->value;
		if (lid_closed && cfg.lid_sleep) suspend_now("lid closed");
		else if (!lid_closed || !cfg.lid_sleep) backlights_power(!lid_closed);	/* no sleep: at least the panels go dark */
		return;
	}
	if (e->type == EV_SW && e->code == SW_HEADPHONE_INSERT) {
		if (headphones != e->value) { headphones = e->value; audio_route(); }
		return;
	}
	if (e->type != EV_KEY || e->value == 2) return;
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
	signal(SIGCHLD, SIG_DFL);
	struct sigaction sa = { 0 };
	sa.sa_handler = on_hup;		/* no SA_RESTART: poll() returns, and the loop reloads */
	sigaction(SIGHUP, &sa, NULL);
	mkdir("/run/banana", 0755);

	int ino = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (ino >= 0) inotify_add_watch(ino, "/dev/input", IN_CREATE | IN_DELETE);
	open_inputs();
	headphones = headphones_in();
	audio_setup();
	restore_settings();
	int uev = uevent_open();
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
		int timeout = power_down ? 100 : (save_volume_at > 0 || save_backlight_at > 0) ? 500 : 15000;
		poll(pf, n, timeout);
		double now = mono();
		if (reload) {
			reload = 0;
			load_conf("/etc/banana/bananad.conf");
			load_conf("/data/banana/bananad.conf");
			load_timezone();
			leds_last = -2;
			leds_update();
			say("settings read again: lid %s, auto-sleep %d min, LEDs %d", cfg.lid_sleep ? "sleeps" : "ignored", cfg.autosleep_min, cfg.led_brightness);
		}

		for (int i = 0; i < ndev; i++) {
			if (!(pf[i].revents & POLLIN)) continue;
			struct input_event ev[64];
			ssize_t r;
			while ((r = read(devfd[i], ev, sizeof ev)) > 0)
				for (int k = 0; k < r / (ssize_t)sizeof ev[0]; k++) handle(&ev[k]);
		}
		if (uev_at >= 0 && (pf[uev_at].revents & POLLIN) && uevent_power(uev)) leds_update();
		save_settings(now);
		if (ino_at >= 0 && (pf[ino_at].revents & POLLIN)) {
			char buf[4096];
			while (read(ino, buf, sizeof buf) > 0) {}
			usleep(200000);	/* udev/mdev sets the node's permissions */
			open_inputs();
		}

		if (power_down && now - power_since >= cfg.power_long_ms / 1000.0) { power_down = 0; shutdown_now("power held"); }

		/* idle in the menu (no game running) */
		if (cfg.autosleep_min > 0 && !pid_of("/run/banana/game.pid") && now - last_input >= cfg.autosleep_min * 60.0)
			suspend_now("idle");
		/* started or woken with the lid shut */
		if (lid_closed && cfg.lid_sleep && now - last_input > 5) suspend_now("lid still closed");

		if (now >= next_battery) {
			next_battery = now + 60;
			leds_update();	/* a capacity change has no uevent on every gauge */
			int pct = battery_discharging_pct();
			low_count = pct >= 0 && pct <= cfg.battery_off_pct ? low_count + 1 : 0;
			if (low_count >= 2) shutdown_now("battery empty");	/* two readings in a row: not a load spike */
		}
	}
}
