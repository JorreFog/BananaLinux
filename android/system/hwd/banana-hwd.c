/*
 * banana-hwd: BananaOS's hardware daemon for the AYN Thor on Android. One line-based Unix socket for the launcher
 * (PROTOCOL.md): the fan, the stick lights, the clock profiles, both panels' backlights, statistics, and the Home
 * (AYN) button, which it reads from its input device and reports to subscribed clients as events.
 *
 *   banana-hwd [-c <hwd.conf>] [-s <socket path>]
 *
 * On Android init creates the socket (banana-hwd.rc: "socket banana-hwd stream 0666 system system") and passes it in
 * ANDROID_SOCKET_banana-hwd; elsewhere (tests, a PC) -s or BANANA_HWD_SOCKET names a path to bind. Roots for tests:
 * BANANA_SYS, BANANA_PROC, BANANA_DEV, BANANA_HWD_STATE.
 */
#include "hw.h"

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
#include <strings.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define HWD_VERSION "1"
#define MAX_CLIENTS 16
#define MAX_INPUTS 16
#define LINE_MAX_LEN 512
#define OUT_MAX 4096

struct client {
    int fd;
    int subscribed;
    char in[LINE_MAX_LEN];
    size_t inlen;
};

struct input {
    int fd;
    char path[HWD_PATH];
    char name[128];
};

static struct hwd H;
static struct client clients[MAX_CLIENTS];
static struct input inputs[MAX_INPUTS];
static int listen_fd = -1, inotify_fd = -1;
static volatile sig_atomic_t quit;
static int home_codes[16], nhome;
static int home_down;          /* a Home key is down */
static int home_hold_sent;     /* and the hold was reported */
static long long home_since;   /* when it went down (ms) */

static long long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void logf_(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("banana-hwd: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static void on_signal(int s)
{
    (void)s;
    quit = 1;
}

/* ---- clients ---------------------------------------------------------------------------------------------- */

static void client_close(struct client *c)
{
    if (c->fd >= 0)
        close(c->fd);
    c->fd = -1;
    c->subscribed = 0;
    c->inlen = 0;
}

static void send_line(struct client *c, const char *s)
{
    size_t n = strlen(s), off = 0;
    char buf[OUT_MAX + 2];
    if (c->fd < 0)
        return;
    if (n > OUT_MAX)
        n = OUT_MAX;
    memcpy(buf, s, n);
    buf[n++] = '\n';
    while (off < n) {
        ssize_t w = send(c->fd, buf + off, n - off, MSG_NOSIGNAL);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0) {   /* a client that doesn't read is dropped, never waited for */
            client_close(c);
            return;
        }
        off += (size_t)w;
    }
}

static void broadcast(const char *s)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].fd >= 0 && clients[i].subscribed)
            send_line(&clients[i], s);
}

static void reply(struct client *c, const char *fmt, ...)
{
    char b[OUT_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    send_line(c, b);
}

/* ---- the Home button -------------------------------------------------------------------------------------- */

static void parse_home_keys(void)
{
    char buf[sizeof H.cfg.home_keys], *save = NULL;
    nhome = 0;
    snprintf(buf, sizeof buf, "%s", H.cfg.home_keys);
    for (char *t = strtok_r(buf, " ,", &save); t && nhome < 16; t = strtok_r(NULL, " ,", &save))
        home_codes[nhome++] = atoi(t);
}

static int is_home(int code)
{
    for (int i = 0; i < nhome; i++)
        if (home_codes[i] == code)
            return 1;
    return 0;
}

/* a device is watched if its name is one of home_devices (or that list is empty) and it can send a Home key */
static int wanted_device(int fd, char *name, size_t n)
{
    unsigned char bits[KEY_MAX / 8 + 1];
    char want[sizeof H.cfg.home_devices], *save = NULL;
    int named = !H.cfg.home_devices[0];
    name[0] = 0;
    if (ioctl(fd, EVIOCGNAME(n - 1), name) < 0)
        name[0] = 0;   /* not an evdev node: a test's pipe */
    snprintf(want, sizeof want, "%s", H.cfg.home_devices);
    for (char *t = strtok_r(want, ",", &save); t && !named; t = strtok_r(NULL, ",", &save)) {
        while (*t == ' ')
            t++;
        named = !strcmp(t, name);
    }
    if (!named)
        return 0;
    memset(bits, 0, sizeof bits);
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0)
        return 1;      /* (a pipe: taken as it is) */
    for (int i = 0; i < nhome; i++)
        if (home_codes[i] >= 0 && home_codes[i] <= KEY_MAX && (bits[home_codes[i] / 8] & (1 << (home_codes[i] % 8))))
            return 1;
    return 0;
}

static void input_add(const char *path)
{
    int slot = -1, fd;
    char name[128];
    for (int i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].fd >= 0 && !strcmp(inputs[i].path, path))
            return;
        if (inputs[i].fd < 0 && slot < 0)
            slot = i;
    }
    if (slot < 0)
        return;
    /* (never EVIOCGRAB: Android's own input reader keeps the device, so a game still gets KEYCODE_F24) */
    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return;
    if (!wanted_device(fd, name, sizeof name)) {
        close(fd);
        return;
    }
    inputs[slot].fd = fd;
    snprintf(inputs[slot].path, sizeof inputs[slot].path, "%s", path);
    snprintf(inputs[slot].name, sizeof inputs[slot].name, "%s", name);
    logf_("watching %s (%s) for the Home button", path, name[0] ? name : "?");
}

static void input_scan(void)
{
    char dir[HWD_PATH], p[HWD_PATH + 300];
    DIR *d;
    struct dirent *e;
    hwd_join(dir, sizeof dir, H.cfg.dev, "input");
    if (!(d = opendir(dir)))
        return;
    while ((e = readdir(d)))
        if (!strncmp(e->d_name, "event", 5)) {
            snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            input_add(p);
        }
    closedir(d);
}

static void home_key(int value)
{
    long long t = now_ms();
    if (value == 1 && !home_down) {
        home_down = 1;
        home_hold_sent = 0;
        home_since = t;
        broadcast("EVENT home down");
    } else if (value == 0 && home_down) {
        home_down = 0;
        /* a tap: the menu over the game, or back into it; after a hold the release only ends it */
        broadcast(home_hold_sent ? "EVENT home release" : "EVENT home tap");
    }
}

static void input_read(struct input *in)
{
    struct input_event ev[32];
    for (;;) {
        ssize_t r = read(in->fd, ev, sizeof ev);
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0 && errno == EAGAIN)
            return;
        if (r <= 0) {   /* the device went away (ENODEV), or a test's pipe closed */
            close(in->fd);
            in->fd = -1;
            if (home_down)
                home_key(0);
            return;
        }
        for (size_t i = 0; i < (size_t)r / sizeof ev[0]; i++)
            if (ev[i].type == EV_KEY && is_home(ev[i].code) && ev[i].value != 2)
                home_key(ev[i].value);
    }
}

/* ---- commands --------------------------------------------------------------------------------------------- */

static int eq(const char *a, const char *b)
{
    return a && !strcasecmp(a, b);
}

static void save(void)
{
    if (hwd_state_save(&H))
        logf_("could not keep the settings in %s: %s", H.cfg.state_file, strerror(errno));
}

static void result(struct client *c, int r, const char *what)
{
    if (r == 0) {
        save();
        reply(c, "OK");
    } else if (r == -2)
        reply(c, "ERR unsupported %s", what);
    else if (r == -3)
        reply(c, "ERR failed %s", what);
    else
        reply(c, "ERR badarg %s", what);
}

static void command(struct client *c, char *line)
{
    char *argv[8], *save_p = NULL, out[OUT_MAX];
    int argc = 0;
    for (char *t = strtok_r(line, " \t", &save_p); t && argc < 8; t = strtok_r(NULL, " \t", &save_p))
        argv[argc++] = t;
    if (!argc)
        return;
    for (int i = argc; i < 8; i++)
        argv[i] = NULL;
    const char *cmd = argv[0], *a1 = argv[1], *a2 = argv[2];

    if (eq(cmd, "PING")) {
        reply(c, "OK pong " HWD_VERSION);
    } else if (eq(cmd, "VERSION")) {
        reply(c, "OK " HWD_VERSION);
    } else if (eq(cmd, "CAPS")) {
        hwd_caps(&H, out, sizeof out);
        reply(c, "OK %s home", out);
    } else if (eq(cmd, "STATS")) {
        hwd_stats(&H, out, sizeof out);
        reply(c, "OK %s", out);
    } else if (eq(cmd, "SUBSCRIBE")) {
        c->subscribed = 1;
        reply(c, "OK");
    } else if (eq(cmd, "UNSUBSCRIBE")) {
        c->subscribed = 0;
        reply(c, "OK");
    } else if (eq(cmd, "FAN")) {
        if (!a1 || eq(a1, "GET")) {
            hwd_fan_get(&H, out, sizeof out);
            reply(c, "OK %s", out);
        } else if (eq(a1, "MODE") && a2) {
            result(c, hwd_fan_mode(&H, a2), "fan mode");
        } else if (eq(a1, "SET") && a2) {
            char *end;
            long v = strtol(a2, &end, 10);
            result(c, *end ? -1 : hwd_fan_manual(&H, (int)v), "fan set");
        } else if (eq(a1, "TRIPS")) {
            hwd_fan_trips(&H, out, sizeof out);
            reply(c, "OK %s", out);
        } else
            reply(c, "ERR badarg fan");
    } else if (eq(cmd, "LEDS")) {
        if (!a1 || eq(a1, "GET")) {
            hwd_leds_get(&H, out, sizeof out);
            reply(c, "OK %s", out);
        } else if (!a2)
            reply(c, "ERR badarg leds");
        else if (eq(a1, "MODE"))
            result(c, hwd_leds_mode(&H, a2), "leds mode");
        else if (eq(a1, "COLOR") || eq(a1, "COLOUR"))
            result(c, hwd_leds_color(&H, a2), "leds color");
        else if (eq(a1, "LEVEL") || eq(a1, "BRIGHTNESS"))
            result(c, hwd_leds_level(&H, a2), "leds level");
        else if (eq(a1, "EFFECT"))
            result(c, hwd_leds_effect(&H, a2), "leds effect");
        else if (eq(a1, "SYSTEM"))
            result(c, hwd_leds_system(&H, a2), "leds system");
        else
            reply(c, "ERR badarg leds");
    } else if (eq(cmd, "PERF")) {
        if (!a1 || eq(a1, "GET")) {
            hwd_perf_get(&H, out, sizeof out);
            reply(c, "OK %s", out);
        } else if (eq(a1, "SET") && a2)
            result(c, hwd_perf(&H, a2), "perf");
        else
            reply(c, "ERR badarg perf");
    } else if (eq(cmd, "GPUBOOST")) {
        if (!a1)
            reply(c, "OK %s available=%d", H.st.gpu_boost ? "on" : "off", hwd_gpu_boost_available(&H));
        else if (eq(a1, "ON") || eq(a1, "OFF"))
            result(c, hwd_gpu_boost_available(&H) ? hwd_gpu_boost(&H, eq(a1, "ON")) : -2, "gpuboost");
        else
            reply(c, "ERR badarg gpuboost");
    } else if (eq(cmd, "BACKLIGHT")) {
        long v, m;
        if (!a1 || (!eq(a1, "top") && !eq(a1, "bottom")))
            reply(c, "ERR badarg backlight");
        else if (!a2) {
            char which[8];
            snprintf(which, sizeof which, "%s", eq(a1, "top") ? "top" : "bottom");
            if (hwd_backlight_get(&H, which, &v, &m))
                reply(c, "ERR unsupported backlight");
            else
                reply(c, "OK %ld %ld", v, m);
        } else {
            char *end;
            v = strtol(a2, &end, 10);
            if (*end)
                reply(c, "ERR badarg backlight");
            else if (hwd_backlight_set(&H, eq(a1, "top") ? "top" : "bottom", v))
                reply(c, "ERR failed backlight");
            else
                reply(c, "OK");   /* (not kept: the framework owns brightness across boots) */
        }
    } else if (eq(cmd, "GAME")) {
        /* a game starts: its system's colour on the lights and its clock profile; ends: the menu's profile */
        if (eq(a1, "START") && a2) {
            hwd_leds_system(&H, a2);
            if (argv[3] && hwd_perf(&H, argv[3])) {
                reply(c, "ERR badarg perf");
                return;
            }
            save();
            reply(c, "OK");
        } else if (eq(a1, "END")) {
            hwd_perf(&H, "ui");
            save();
            reply(c, "OK");
        } else
            reply(c, "ERR badarg game");
    } else if (eq(cmd, "SUSPEND")) {
        hwd_suspend(&H);
        reply(c, "OK");
    } else if (eq(cmd, "RESUME")) {
        hwd_resume(&H);
        reply(c, "OK");
    } else if (eq(cmd, "QUIT")) {
        reply(c, "OK bye");
        client_close(c);
    } else
        reply(c, "ERR unknown %s", cmd);
}

static void client_read(struct client *c)
{
    char buf[1024];
    ssize_t r = recv(c->fd, buf, sizeof buf, 0);
    if (r < 0 && (errno == EINTR || errno == EAGAIN))
        return;
    if (r <= 0) {
        client_close(c);
        return;
    }
    for (ssize_t i = 0; i < r && c->fd >= 0; i++) {
        if (buf[i] == '\n') {
            c->in[c->inlen] = 0;
            if (c->inlen && c->in[c->inlen - 1] == '\r')
                c->in[c->inlen - 1] = 0;
            c->inlen = 0;
            command(c, c->in);
        } else if (c->inlen < sizeof c->in - 1)
            c->in[c->inlen++] = buf[i];
        else {   /* a line longer than any command: the client is broken */
            reply(c, "ERR toolong");
            client_close(c);
        }
    }
}

static void client_accept(void)
{
    int fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0)
        return;
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].fd < 0) {
            struct ucred cr;
            socklen_t l = sizeof cr;
            clients[i].fd = fd;
            clients[i].subscribed = 0;
            clients[i].inlen = 0;
            if (!getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &l))
                logf_("client uid %d pid %d", (int)cr.uid, (int)cr.pid);
            return;
        }
    send(fd, "ERR busy\n", 9, MSG_NOSIGNAL);
    close(fd);
}

/* ---- setup ------------------------------------------------------------------------------------------------ */

static int socket_from_init(void)
{
    const char *e = getenv("ANDROID_SOCKET_banana-hwd");
    char *end;
    long fd;
    if (!e || !*e)
        return -1;
    fd = strtol(e, &end, 10);
    if (*end || fd < 0)
        return -1;
    fcntl((int)fd, F_SETFD, FD_CLOEXEC);
    return (int)fd;
}

static int socket_bind(const char *path)
{
    struct sockaddr_un a;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof a.sun_path) {
        close(fd);
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(a.sun_path, path);
    unlink(path);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) || listen(fd, 8)) {
        close(fd);
        return -1;
    }
    chmod(path, 0666);
    return fd;
}

int main(int argc, char **argv)
{
    const char *conf = getenv("BANANA_HWD_CONF"), *sock = getenv("BANANA_HWD_SOCKET");
    char dir[HWD_PATH];
    int opt;
    long long next_fan = 0, next_breathe = 0, next_battery = 0;

    if (!conf || !*conf)
        conf = "/system_ext/etc/banana/hwd.conf";
    while ((opt = getopt(argc, argv, "c:s:h")) != -1) {
        if (opt == 'c')
            conf = optarg;
        else if (opt == 's')
            sock = optarg;
        else {
            fprintf(stderr, "usage: banana-hwd [-c hwd.conf] [-s socket]\n");
            return 2;
        }
    }

    hwd_config_defaults(&H.cfg);
    if (hwd_config_load(&H.cfg, conf))
        logf_("no %s, the defaults", conf);
    hwd_probe(&H);
    hwd_state_defaults(&H.st);
    hwd_state_load(&H);
    parse_home_keys();
    {
        char caps[256];
        hwd_caps(&H, caps, sizeof caps);
        logf_("started: %s; fan %s (%d trips), %d lights, %d clusters, gpu %s", caps, H.fan_backend, H.ntrips,
              H.nrings, H.npolicies, H.gpu[0] ? H.gpu : "-");
    }
    hwd_apply_all(&H);

    listen_fd = socket_from_init();
    if (listen_fd < 0) {
        if (!sock || !*sock)
            sock = "/dev/socket/banana-hwd";
        listen_fd = socket_bind(sock);
        if (listen_fd < 0) {
            logf_("cannot listen on %s: %s", sock, strerror(errno));
            return 1;
        }
    } else if (listen(listen_fd, 8)) {
        logf_("cannot listen on init's socket: %s", strerror(errno));
        return 1;
    }
    fcntl(listen_fd, F_SETFL, O_NONBLOCK);

    for (int i = 0; i < MAX_CLIENTS; i++)
        clients[i].fd = -1;
    for (int i = 0; i < MAX_INPUTS; i++)
        inputs[i].fd = -1;
    inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    hwd_join(dir, sizeof dir, H.cfg.dev, "input");
    if (inotify_fd >= 0)
        inotify_add_watch(inotify_fd, dir, IN_CREATE | IN_ATTRIB);
    input_scan();

    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    while (!quit) {
        struct pollfd pf[2 + MAX_CLIENTS + MAX_INPUTS];
        int kind[2 + MAX_CLIENTS + MAX_INPUTS], idx[2 + MAX_CLIENTS + MAX_INPUTS], n = 0;
        long long t = now_ms(), deadline = t + 60000;
        int timeout;

        /* timers: the fan's curve (pwm backend), the breathing lights, the battery colour, the Home hold */
        if (!strcmp(H.fan_backend, "pwm")) {
            if (t >= next_fan) {
                hwd_fan_tick(&H);
                next_fan = t + H.cfg.fan_interval_ms;
            }
            deadline = next_fan < deadline ? next_fan : deadline;
        }
        if (!strcmp(H.st.leds_effect, "breathe") && H.nrings && !H.st.suspended) {
            if (t >= next_breathe) {
                hwd_leds_tick_breathe(&H);
                next_breathe = t + 100;
            }
            deadline = next_breathe < deadline ? next_breathe : deadline;
        }
        if (!strcmp(H.st.leds_mode, "battery") && H.nrings) {
            if (t >= next_battery) {
                hwd_leds_tick_battery(&H);
                next_battery = t + 30000;
            }
            deadline = next_battery < deadline ? next_battery : deadline;
        }
        if (home_down && !home_hold_sent) {
            if (t - home_since >= H.cfg.home_hold_ms) {
                home_hold_sent = 1;
                broadcast("EVENT home hold");   /* the quick menu, while the button is still held */
            } else if (home_since + H.cfg.home_hold_ms < deadline)
                deadline = home_since + H.cfg.home_hold_ms;
        }
        timeout = deadline > t ? (int)(deadline - t) : 0;

        pf[n] = (struct pollfd){.fd = listen_fd, .events = POLLIN};
        kind[n++] = 0;
        if (inotify_fd >= 0) {
            pf[n] = (struct pollfd){.fd = inotify_fd, .events = POLLIN};
            kind[n++] = 1;
        }
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (clients[i].fd >= 0) {
                pf[n] = (struct pollfd){.fd = clients[i].fd, .events = POLLIN};
                kind[n] = 2;
                idx[n++] = i;
            }
        for (int i = 0; i < MAX_INPUTS; i++)
            if (inputs[i].fd >= 0) {
                pf[n] = (struct pollfd){.fd = inputs[i].fd, .events = POLLIN};
                kind[n] = 3;
                idx[n++] = i;
            }
        if (poll(pf, (nfds_t)n, timeout) < 0) {
            if (errno == EINTR)
                continue;
            logf_("poll: %s", strerror(errno));
            break;
        }
        for (int i = 0; i < n; i++) {
            if (!pf[i].revents)
                continue;
            switch (kind[i]) {
            case 0:
                client_accept();
                break;
            case 1: {
                char b[4096];
                while (read(inotify_fd, b, sizeof b) > 0) {}
                input_scan();   /* a pad or keyboard plugged in, or the node's permissions set by ueventd */
                break;
            }
            case 2:
                if (pf[i].revents & (POLLIN | POLLHUP | POLLERR))
                    client_read(&clients[idx[i]]);
                break;
            case 3:
                input_read(&inputs[idx[i]]);
                break;
            }
        }
    }
    logf_("stopping");
    save();
    return 0;
}
