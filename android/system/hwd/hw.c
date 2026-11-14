/*
 * banana-hwd's hardware layer: what BananaOS's Linux tools did with sysfs (usr/libexec/banana/handheld, sysstat,
 * usr/bin/banana-perf), in one process. Every write checks its file first; a node the kernel doesn't have is skipped,
 * so the same binary runs on the mainline kernel (ROCKNIX's device tree) and on a Qualcomm vendor kernel, where the
 * names differ (hwd.conf overrides each one; PROTOCOL.md lists them).
 */
#include "hw.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- helpers ---------------------------------------------------------------------------------------------- */

void hwd_join(char *out, size_t n, const char *root, const char *rel)
{
    while (*rel == '/')
        rel++;
    snprintf(out, n, "%s/%s", root, rel);
}

int hwd_read_line(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "re");
    if (!f)
        return -1;
    if (!fgets(buf, (int)n, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    buf[strcspn(buf, "\r\n")] = 0;
    return 0;
}

int hwd_read_long(const char *path, long *v)
{
    char b[64];
    char *end;
    if (hwd_read_line(path, b, sizeof b))
        return -1;
    errno = 0;
    *v = strtol(b, &end, 10);
    if (errno || end == b)
        return -1;
    return 0;
}

int hwd_write_str(const char *path, const char *s)
{
    FILE *f;
    int r;
    if (access(path, F_OK))
        return -1;
    f = fopen(path, "we");
    if (!f)
        return -1;
    r = fputs(s, f) < 0 ? -1 : 0;
    if (fputc('\n', f) == EOF)
        r = -1;
    if (fclose(f))
        r = -1;
    return r;
}

int hwd_write_long(const char *path, long v)
{
    char b[32];
    snprintf(b, sizeof b, "%ld", v);
    return hwd_write_str(path, b);
}

static int exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static void sysp(const struct hwd *h, char *out, size_t n, const char *rel)
{
    hwd_join(out, n, h->cfg.sys, rel);
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void copy(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src);
}

/* a word in a space separated list (scaling_available_governors) */
static int has_word(const char *list, const char *word)
{
    size_t l = strlen(word);
    const char *p = list;
    if (!l)
        return 0;
    while ((p = strstr(p, word))) {
        if ((p == list || isspace((unsigned char)p[-1])) && (!p[l] || isspace((unsigned char)p[l])))
            return 1;
        p += l;
    }
    return 0;
}

/* entries of <dir> whose names match <pattern>, sorted, as full paths */
static int list_dir(const char *dir, const char *pattern, char out[][HWD_PATH], int max)
{
    struct dirent **ents;
    int n = scandir(dir, &ents, NULL, alphasort), k = 0;
    if (n < 0)
        return 0;
    for (int i = 0; i < n; i++) {
        if (k < max && ents[i]->d_name[0] != '.' && !fnmatch(pattern, ents[i]->d_name, 0))
            snprintf(out[k++], HWD_PATH, "%s/%s", dir, ents[i]->d_name);
        free(ents[i]);
    }
    free(ents);
    return k;
}

static int is_trips(const struct hwd *h);
static int is_pwm(const struct hwd *h);

/* ---- configuration ---------------------------------------------------------------------------------------- */

void hwd_config_defaults(struct hwd_config *c)
{
    const char *e;
    memset(c, 0, sizeof *c);
    copy(c->sys, sizeof c->sys, (e = getenv("BANANA_SYS")) && *e ? e : "/sys");
    copy(c->proc, sizeof c->proc, (e = getenv("BANANA_PROC")) && *e ? e : "/proc");
    copy(c->dev, sizeof c->dev, (e = getenv("BANANA_DEV")) && *e ? e : "/dev");
    copy(c->state_file, sizeof c->state_file,
         (e = getenv("BANANA_HWD_STATE")) && *e ? e : "/data/misc/banana/hwd.state");
    copy(c->led_order, sizeof c->led_order, "auto");
    copy(c->fan_backend, sizeof c->fan_backend, "auto");
    /* the AYN button under the bottom screen: KEY_F24 on gpio-keys (thor-aynkey measured it on LineageOS) */
    copy(c->home_keys, sizeof c->home_keys, "194");
    copy(c->home_devices, sizeof c->home_devices, "gpio-keys");
    c->home_hold_ms = 3000;
    c->fan_interval_ms = 3000;
    c->gpu_boost_hz = 1000000000L;
    c->gpu_stock_hz = 680000000L;
}

/* hwd.conf: "key = value" lines, '#' comments */
int hwd_config_load(struct hwd_config *c, const char *file)
{
    char line[600];
    FILE *f = fopen(file, "re");
    if (!f)
        return -1;
    while (fgets(line, sizeof line, f)) {
        char *k = line, *v, *eq, *end;
        line[strcspn(line, "\r\n")] = 0;
        while (isspace((unsigned char)*k))
            k++;
        if (!*k || *k == '#' || !(eq = strchr(k, '=')))
            continue;
        *eq = 0;
        v = eq + 1;
        for (end = eq - 1; end >= k && isspace((unsigned char)*end); end--)
            *end = 0;
        while (isspace((unsigned char)*v))
            v++;
        for (end = v + strlen(v) - 1; end >= v && isspace((unsigned char)*end); end--)
            *end = 0;
#define STR(name) else if (!strcmp(k, #name)) copy(c->name, sizeof c->name, v)
#define NUM(name) else if (!strcmp(k, #name)) c->name = atol(v)
        if (0) {}
        STR(fan_pwm); STR(backlight_top); STR(backlight_bottom); STR(gpu_devfreq); STR(led_nodes); STR(led_order); STR(fan_backend);
        STR(home_keys); STR(home_devices); STR(state_file);
        NUM(home_hold_ms); NUM(fan_interval_ms); NUM(gpu_boost_hz); NUM(gpu_stock_hz);
#undef STR
#undef NUM
    }
    fclose(f);
    if (c->home_hold_ms < 100)
        c->home_hold_ms = 3000;
    if (c->fan_interval_ms < 100)
        c->fan_interval_ms = 3000;
    if (strcmp(c->led_order, "rgb") && strcmp(c->led_order, "bgr"))
        copy(c->led_order, sizeof c->led_order, "auto");
    return 0;
}

/* ---- probing ---------------------------------------------------------------------------------------------- */

/* the trip points bound to the pwm-fan cooling device (thor-fanctl's discover(): zone/cdevN/type == pwm-fan, its
 * trip in cdevN_trip_point), found by type, never by number: cooling_device numbering moves between boots */
static void probe_trips(struct hwd *h)
{
    char dir[HWD_PATH], z[256][HWD_PATH], c[32][HWD_PATH], p[HWD_PATH + 64], type[64], ztype[32];
    long tp, v;
    int nz;
    h->ntrips = 0;
    h->fan_cdev[0] = 0;
    sysp(h, dir, sizeof dir, "class/thermal");
    nz = list_dir(dir, "cooling_device*", z, 256);
    for (int i = 0; i < nz; i++) {
        snprintf(p, sizeof p, "%s/type", z[i]);
        if (!hwd_read_line(p, type, sizeof type) && !strcmp(type, "pwm-fan")) {
            copy(h->fan_cdev, sizeof h->fan_cdev, z[i]);
            break;
        }
    }
    nz = list_dir(dir, "thermal_zone*", z, 256);
    for (int i = 0; i < nz && h->ntrips < HWD_MAX_TRIPS; i++) {
        int nc;
        snprintf(p, sizeof p, "%s/type", z[i]);
        if (hwd_read_line(p, ztype, sizeof ztype))
            continue;
        nc = list_dir(z[i], "cdev*", c, 32);
        for (int k = 0; k < nc && h->ntrips < HWD_MAX_TRIPS; k++) {
            size_t l = strlen(c[k]);
            if ((l > 11 && !strcmp(c[k] + l - 11, "_trip_point")) || (l > 7 && !strcmp(c[k] + l - 7, "_weight")))
                continue;
            snprintf(p, sizeof p, "%s/type", c[k]);
            if (hwd_read_line(p, type, sizeof type) || strcmp(type, "pwm-fan"))
                continue;
            snprintf(p, sizeof p, "%s_trip_point", c[k]);
            if (hwd_read_long(p, &tp) || tp < 0)
                continue;
            snprintf(p, sizeof p, "%s/trip_point_%ld_temp", z[i], tp);
            if (hwd_read_long(p, &v) || access(p, W_OK))
                continue;
            copy(h->trip_path[h->ntrips], HWD_PATH, p);
            copy(h->trip_zone[h->ntrips], sizeof h->trip_zone[0], ztype);
            h->trip_orig[h->ntrips++] = v;
        }
    }
}

/* the trips as the kernel had them at boot: recorded once per boot (the boot id beside them), so a restarted daemon
 * never takes its own moved trips for the originals (thor-fanctl's capture at boot) */
static void trips_originals(struct hwd *h)
{
    char bootid[64] = "", p[HWD_PATH + 16], line[HWD_PATH + 64], path[HWD_PATH];
    FILE *f;
    long v;
    hwd_join(p, sizeof p, h->cfg.proc, "sys/kernel/random/boot_id");
    hwd_read_line(p, bootid, sizeof bootid);
    snprintf(p, sizeof p, "%s.trips", h->cfg.state_file);
    if ((f = fopen(p, "re"))) {
        int same = fgets(line, sizeof line, f) && !strncmp(line, "boot ", 5) &&
                   (line[strcspn(line, "\n")] = 0, !strcmp(line + 5, bootid));
        while (same && fgets(line, sizeof line, f))
            if (sscanf(line, "%511s %ld", path, &v) == 2)
                for (int i = 0; i < h->ntrips; i++)
                    if (!strcmp(h->trip_path[i], path))
                        h->trip_orig[i] = v;
        fclose(f);
        if (same)
            return;
    }
    if ((f = fopen(p, "we"))) {
        fprintf(f, "boot %s\n", bootid);
        for (int i = 0; i < h->ntrips; i++)
            fprintf(f, "%s %ld\n", h->trip_path[i], h->trip_orig[i]);
        fclose(f);
    }
}

static void probe_fan(struct hwd *h)
{
    char dir[HWD_PATH], hw[16][HWD_PATH], p[HWD_PATH + 16], name[64];
    int n;
    h->fan_pwm[0] = 0;
    if (h->cfg.fan_pwm[0]) {
        sysp(h, h->fan_pwm, sizeof h->fan_pwm, h->cfg.fan_pwm);
        if (!exists(h->fan_pwm))
            h->fan_pwm[0] = 0;
        return;
    }
    /* the pwm-fan driver's hwmon ("pwmfan"), else the first hwmon with a pwm1 (handheld's has_fan) */
    sysp(h, dir, sizeof dir, "class/hwmon");
    n = list_dir(dir, "hwmon*", hw, 16);
    for (int pass = 0; pass < 2 && !h->fan_pwm[0]; pass++)
        for (int i = 0; i < n; i++) {
            snprintf(p, sizeof p, "%s/name", hw[i]);
            if (pass == 0 && (hwd_read_line(p, name, sizeof name) || strcmp(name, "pwmfan")))
                continue;
            snprintf(p, sizeof p, "%s/pwm1", hw[i]);
            if (exists(p)) {
                copy(h->fan_pwm, sizeof h->fan_pwm, p);
                break;
            }
        }
}

static void probe_zones(struct hwd *h)
{
    char dir[HWD_PATH], z[HWD_MAX_ZONES * 2][HWD_PATH], p[HWD_PATH + 16], type[64];
    int n;
    sysp(h, dir, sizeof dir, "class/thermal");
    n = list_dir(dir, "thermal_zone*", z, HWD_MAX_ZONES * 2);
    h->nzones = 0;
    for (int i = 0; i < n && h->nzones < HWD_MAX_ZONES; i++) {
        snprintf(p, sizeof p, "%s/type", z[i]);
        if (hwd_read_line(p, type, sizeof type))
            continue;
        /* ROCKNIX's fancontrol: cpu* and gpuss* (mainline: cpu0-top-thermal, gpuss0-thermal; a Qualcomm vendor
         * kernel: cpu-0-0-0, gpuss-0) */
        if (!strncasecmp(type, "cpu", 3) || !strncasecmp(type, "gpu", 3))
            snprintf(h->zones[h->nzones++], HWD_PATH, "%s/temp", z[i]);
    }
}

static void probe_gpu(struct hwd *h)
{
    char dir[HWD_PATH], d[16][HWD_PATH];
    int n;
    h->gpu[0] = 0;
    if (h->cfg.gpu_devfreq[0]) {
        sysp(h, h->gpu, sizeof h->gpu, h->cfg.gpu_devfreq);
        return;
    }
    /* AYN's vendor kernel: kgsl's devfreq (its devfreq class name is "3d00000.qcom,kgsl-3d0"; a "*gpu*" match there
     * would find the GPU's bus-bandwidth devfreq, gpubw, instead) */
    sysp(h, dir, sizeof dir, "class/kgsl/kgsl-3d0/devfreq");
    if (exists(dir)) {
        copy(h->gpu, sizeof h->gpu, dir);
        return;
    }
    /* mainline msm: 3d00000.gpu */
    sysp(h, dir, sizeof dir, "class/devfreq");
    n = list_dir(dir, "*.gpu", d, 16);
    if (n)
        copy(h->gpu, sizeof h->gpu, d[0]);
}

static void add_ring(struct hwd *h, const char *name, const char *order)
{
    char p[HWD_PATH], rel[200];
    snprintf(rel, sizeof rel, "class/leds/%s", name);
    sysp(h, p, sizeof p, rel);
    if (!exists(p) || h->nrings >= HWD_LED_RINGS)
        return;
    copy(h->rings[h->nrings], HWD_PATH, p);
    copy(h->ring_order[h->nrings++], 4, strcmp(h->cfg.led_order, "auto") ? h->cfg.led_order : order);
}

/* the stick lights. AYN's vendor kernel (LineageOS): left:stick:0-3 and right:stick:0-3, one per LED, multi_intensity
 * "R G B" (LineageOS's hardware/ayn: AynParts' LightUtils, charger_rgb); where a kernel has only one per side,
 * left:stick / right:stick, and left:strip / right:strip beside them. Mainline with ROCKNIX's device tree: rgb:l1-4
 * and rgb:r1-4 (leds-group-multicolor), whose channels are wired blue, green, red (handheld's rings()) */
static void probe_rings(struct hwd *h)
{
    static const char *ayn_led[] = {"left:stick:0", "left:stick:1", "left:stick:2", "left:stick:3", "right:stick:0",
                                    "right:stick:1", "right:stick:2", "right:stick:3", NULL};
    static const char *ayn_side[] = {"left:stick", "right:stick", NULL};
    static const char *ayn_strip[] = {"left:strip", "right:strip", NULL};
    static const char *mainline[] = {"rgb:l1", "rgb:l2", "rgb:l3", "rgb:l4", "rgb:r1", "rgb:r2", "rgb:r3", "rgb:r4", NULL};
    h->nrings = 0;
    if (h->cfg.led_nodes[0]) {
        char buf[sizeof h->cfg.led_nodes], *save = NULL;
        copy(buf, sizeof buf, h->cfg.led_nodes);
        for (char *t = strtok_r(buf, " ,", &save); t; t = strtok_r(NULL, " ,", &save))
            add_ring(h, t, strncmp(t, "rgb:", 4) ? "rgb" : "bgr");
        return;
    }
    for (int i = 0; ayn_led[i]; i++)
        add_ring(h, ayn_led[i], "rgb");
    if (!h->nrings)
        for (int i = 0; ayn_side[i]; i++)
            add_ring(h, ayn_side[i], "rgb");
    for (int i = 0; ayn_strip[i]; i++)
        add_ring(h, ayn_strip[i], "rgb");
    if (!h->nrings)
        for (int i = 0; mainline[i]; i++)
            add_ring(h, mainline[i], "bgr");
    if (!h->nrings) {   /* handheld's platform path */
        char p[HWD_PATH], rel[200];
        for (int s = 0; s < 2; s++)
            for (int i = 1; i <= 4; i++) {
                snprintf(rel, sizeof rel, "devices/platform/multi-led%c%d/leds/rgb:%c%d", "lr"[s], i, "lr"[s], i);
                sysp(h, p, sizeof p, rel);
                if (exists(p) && h->nrings < HWD_LED_RINGS) {
                    copy(h->rings[h->nrings], HWD_PATH, p);
                    copy(h->ring_order[h->nrings++], 4, "bgr");
                }
            }
    }
}

static char default_cpu_gov[HWD_MAX_POLICIES][32];
static char default_gpu_gov[32];

void hwd_probe(struct hwd *h)
{
    char dir[HWD_PATH], p[HWD_PATH + 32];
    probe_fan(h);
    probe_trips(h);
    if (!strcmp(h->cfg.fan_backend, "auto"))
        copy(h->fan_backend, sizeof h->fan_backend, h->ntrips ? "trips" : h->fan_pwm[0] ? "pwm" : "none");
    else
        copy(h->fan_backend, sizeof h->fan_backend, h->cfg.fan_backend);
    if (!strcmp(h->fan_backend, "trips"))
        trips_originals(h);
    probe_zones(h);
    probe_gpu(h);
    probe_rings(h);
    sysp(h, dir, sizeof dir, "devices/system/cpu/cpufreq");
    h->npolicies = list_dir(dir, "policy[0-9]*", h->policies, HWD_MAX_POLICIES);
    /* the governors the kernel started with: what a profile falls back to where its own isn't offered (a vendor
     * kernel's "walt" instead of schedutil, kgsl's "msm-adreno-tz" instead of simple_ondemand) */
    for (int i = 0; i < h->npolicies; i++) {
        snprintf(p, sizeof p, "%s/scaling_governor", h->policies[i]);
        if (hwd_read_line(p, default_cpu_gov[i], sizeof default_cpu_gov[i]))
            default_cpu_gov[i][0] = 0;
    }
    if (h->gpu[0]) {
        snprintf(p, sizeof p, "%s/governor", h->gpu);
        if (hwd_read_line(p, default_gpu_gov, sizeof default_gpu_gov))
            default_gpu_gov[0] = 0;
    }
    h->fan_last = -1;
}

void hwd_caps(struct hwd *h, char *out, size_t n)
{
    long v, m;
    out[0] = 0;
    if (is_trips(h))
        strncat(out, "fan fan_trips ", n - strlen(out) - 1);
    else if (is_pwm(h))
        strncat(out, "fan fan_pwm ", n - strlen(out) - 1);
    if (h->nrings)
        strncat(out, "leds ", n - strlen(out) - 1);
    if (h->npolicies)
        strncat(out, "perf ", n - strlen(out) - 1);
    if (hwd_gpu_boost_available(h))
        strncat(out, "gpuboost ", n - strlen(out) - 1);
    if (!hwd_backlight_get(h, "top", &v, &m))
        strncat(out, "backlight_top ", n - strlen(out) - 1);
    if (!hwd_backlight_get(h, "bottom", &v, &m))
        strncat(out, "backlight_bottom ", n - strlen(out) - 1);
    if (h->nzones)
        strncat(out, "temps ", n - strlen(out) - 1);
    if (*out)
        out[strlen(out) - 1] = 0;
}

/* ---- state ------------------------------------------------------------------------------------------------ */

void hwd_state_defaults(struct hwd_state *s)
{
    memset(s, 0, sizeof *s);
    copy(s->fan_mode, sizeof s->fan_mode, "quiet");      /* handheld boot: ${f:-quiet} */
    s->fan_manual_pct = 50;
    copy(s->leds_mode, sizeof s->leds_mode, "system");   /* ${l:-system} */
    s->leds_rgb = 0xffbe00;                              /* the banana's yellow */
    s->leds_level = 140;                                 /* medium */
    copy(s->leds_effect, sizeof s->leds_effect, "none");
    copy(s->perf, sizeof s->perf, "balanced");
}

int hwd_state_load(struct hwd *h)
{
    char line[200];
    FILE *f = fopen(h->cfg.state_file, "re");
    struct hwd_state *s = &h->st;
    if (!f)
        return -1;
    while (fgets(line, sizeof line, f)) {
        char *v = strchr(line, '=');
        line[strcspn(line, "\r\n")] = 0;
        if (!v)
            continue;
        *v++ = 0;
        if (!strcmp(line, "fan_mode")) copy(s->fan_mode, sizeof s->fan_mode, v);
        else if (!strcmp(line, "fan_manual_pct")) s->fan_manual_pct = clamp(atoi(v), 0, 100);
        else if (!strcmp(line, "leds_mode")) copy(s->leds_mode, sizeof s->leds_mode, v);
        else if (!strcmp(line, "leds_rgb")) s->leds_rgb = (unsigned)strtoul(v, NULL, 16) & 0xffffff;
        else if (!strcmp(line, "leds_level")) s->leds_level = clamp(atoi(v), 0, 255);
        else if (!strcmp(line, "leds_effect")) copy(s->leds_effect, sizeof s->leds_effect, v);
        else if (!strcmp(line, "perf")) copy(s->perf, sizeof s->perf, v);
        else if (!strcmp(line, "gpu_boost")) s->gpu_boost = atoi(v) ? 1 : 0;
    }
    fclose(f);
    return 0;
}

int hwd_state_save(const struct hwd *h)
{
    char tmp[HWD_PATH + 8], dir[HWD_PATH];
    const struct hwd_state *s = &h->st;
    FILE *f;
    char *slash;
    copy(dir, sizeof dir, h->cfg.state_file);
    if ((slash = strrchr(dir, '/'))) {
        *slash = 0;
        mkdir(dir, 0770);
    }
    snprintf(tmp, sizeof tmp, "%s.new", h->cfg.state_file);
    if (!(f = fopen(tmp, "we")))
        return -1;
    fprintf(f, "fan_mode=%s\nfan_manual_pct=%d\nleds_mode=%s\nleds_rgb=%06x\nleds_level=%d\nleds_effect=%s\n"
               "perf=%s\ngpu_boost=%d\n",
            s->fan_mode, s->fan_manual_pct, s->leds_mode, s->leds_rgb, s->leds_level, s->leds_effect, s->perf,
            s->gpu_boost);
    if (fclose(f))
        return -1;
    return rename(tmp, h->cfg.state_file);
}

void hwd_apply_all(struct hwd *h)
{
    char fan[16];
    copy(fan, sizeof fan, h->st.fan_mode);
    if (hwd_fan_mode(h, fan))
        hwd_fan_mode(h, "quiet");
    hwd_leds_apply(h);
    hwd_perf(h, h->st.perf);
    if (h->st.gpu_boost)
        hwd_gpu_boost(h, 1);
}

/* ---- fan -------------------------------------------------------------------------------------------------- */

int hwd_soc_temp_mc(struct hwd *h)
{
    long s = 0, v;
    int n = 0;
    for (int i = 0; i < h->nzones; i++)
        if (!hwd_read_long(h->zones[i], &v)) {
            s += v;
            n++;
        }
    return n ? (int)(s / n) : 0;
}

static int hottest_mc(struct hwd *h)
{
    long m = 0, v;
    for (int i = 0; i < h->nzones; i++)
        if (!hwd_read_long(h->zones[i], &v) && v > m)
            m = v;
    return (int)m;
}

/* ROCKNIX's SM8550 curves (handheld's fan_loop): a speed for each step of temperature, the profile moving the steps */
static int fan_curve(const char *mode, int t)
{
    int base = !strcmp(mode, "aggressive") ? 50 : !strcmp(mode, "quiet") ? 60 : 55;
    int top = base == 60 ? 95 : base + 30;
    if (t > top) return 255;
    if (t > base + 25) return 204;
    if (t > base + 20) return 153;
    if (t > base + 15) return 119;
    if (t > base + 10) return 102;
    if (t > base + 5) return 77;
    if (t > base) return 51;
    return 0;
}

static void fan_write(struct hwd *h, int v)
{
    if (!h->fan_pwm[0] || v == h->fan_last)
        return;
    if (!hwd_write_long(h->fan_pwm, v))
        h->fan_last = v;
}

static int is_trips(const struct hwd *h)
{
    return !strcmp(h->fan_backend, "trips");
}

static int is_pwm(const struct hwd *h)
{
    return !strcmp(h->fan_backend, "pwm") && h->fan_pwm[0];
}

/* the "pwm" backend (a kernel where nothing else drives the fan: mainline with ROCKNIX's device tree), every
 * fan_interval_ms */
void hwd_fan_tick(struct hwd *h)
{
    int t, v;
    if (!is_pwm(h) || h->st.suspended)
        return;
    t = hwd_soc_temp_mc(h) / 1000;
    if (!strcmp(h->st.fan_mode, "max"))
        v = 255;
    else if (!strcmp(h->st.fan_mode, "off"))
        v = 0;
    else if (!strcmp(h->st.fan_mode, "manual"))
        v = h->st.fan_manual_pct * 255 / 100;
    else
        v = fan_curve(h->st.fan_mode, t);
    /* off and manual still give way to heat: on this backend this loop is all that spins the fan */
    if (hottest_mc(h) / 1000 >= 90)
        v = 255;
    fan_write(h, v);
}

/* The "trips" backend (AYN's vendor kernel, LineageOS): the fan is a thermal cooling device that the step_wise
 * governor drives from several zones; a write to pwm1 is undone at its next evaluation. A profile moves the trips
 * bound to the fan instead, as thor-fanctl does (Apache-2.0, github.com/Wingdom/thor-features, thor-fanctl/fanctl's
 * plan_for_mode), with its invariants: a CPU/GPU trip is only ever lowered from its boot value (more cooling, never
 * less) and not under 40 C; usb-therm, whose stock 43 C trip (from Qualcomm's reference design) spins the fan at
 * idle while charging, goes to 60 C and stays within 35-70 C. Every trip is written on each apply, which also makes
 * each zone evaluate again (the zones don't poll: a fan state latched before the change would otherwise stay).
 *   stock       the boot values
 *   quiet       ladder as at boot, usb-therm fixed (fanctl's quiet)
 *   moderate    ladder 5 C sooner (fanctl's balanced)
 *   aggressive  ladder 10 C sooner (fanctl's performance)
 *   max         ladder 30 C sooner, floored at 40 C: the fan runs from 40 C */
static long trip_target(const struct hwd *h, int i, const char *mode, long usb_first_orig, int usb_first)
{
    long orig = h->trip_orig[i], v, off;
    if (!strcmp(mode, "stock"))
        return orig;
    if (!strcmp(h->trip_zone[i], "usb-therm")) {
        if (usb_first)
            v = 60000;
        else {
            long d = orig - usb_first_orig;
            v = 60000 + (d < 2000 ? 2000 : d);
        }
        return v > 70000 ? 70000 : v < 35000 ? 35000 : v;
    }
    off = !strcmp(mode, "moderate") ? -5000 : !strcmp(mode, "aggressive") ? -10000 : !strcmp(mode, "max") ? -30000 : 0;
    v = orig + off;
    if (v < 40000)
        v = 40000;
    if (v > orig)   /* never above the boot value */
        v = orig;
    return v;
}

static int trips_apply(struct hwd *h, const char *mode)
{
    long usb_first_orig = 0;
    int usb_seen = 0, bad = 0;
    for (int i = 0; i < h->ntrips; i++) {
        int first = 0;
        long v, back;
        if (!strcmp(h->trip_zone[i], "usb-therm") && !usb_seen) {
            usb_seen = first = 1;
            usb_first_orig = h->trip_orig[i];
        }
        v = trip_target(h, i, mode, usb_first_orig, first);
        /* verified by reading it back, not by the write's status (thor-fanctl) */
        if (hwd_write_long(h->trip_path[i], v) || hwd_read_long(h->trip_path[i], &back) || back != v)
            bad++;
    }
    return bad ? -1 : 0;
}

int hwd_fan_mode(struct hwd *h, const char *mode)
{
    static const char *modes[] = {"quiet", "moderate", "aggressive", "max", "off", "manual", "stock", "custom", NULL};
    char en[HWD_PATH + 16];
    int ok = 0;
    for (int i = 0; modes[i]; i++)
        ok |= !strcmp(mode, modes[i]);
    if (!ok)
        return -1;
    /* (handheld's "custom" is ROCKNIX's own curve file; here it is moderate's) */
    if (!strcmp(mode, "custom"))
        mode = "moderate";
    if (is_trips(h)) {
        /* the kernel keeps the fan: no "off", and no fixed speed */
        if (!strcmp(mode, "off") || !strcmp(mode, "manual"))
            return -2;
        copy(h->st.fan_mode, sizeof h->st.fan_mode, mode);
        return trips_apply(h, mode) ? -3 : 0;
    }
    if (!is_pwm(h))
        return -2;
    if (!strcmp(mode, "stock"))
        mode = "quiet";
    copy(h->st.fan_mode, sizeof h->st.fan_mode, mode);
    snprintf(en, sizeof en, "%s_enable", h->fan_pwm);
    hwd_write_long(en, 1);   /* manual control of the PWM */
    h->fan_last = -1;
    hwd_fan_tick(h);
    return 0;
}

int hwd_fan_manual(struct hwd *h, int pct)
{
    if (pct < 0 || pct > 100)
        return -1;
    if (!is_pwm(h))
        return -2;
    h->st.fan_manual_pct = pct;
    return hwd_fan_mode(h, "manual");
}

void hwd_fan_get(struct hwd *h, char *out, size_t n)
{
    char p[HWD_PATH + 16];
    long v = -1, cur = -1, max = -1;
    if (h->fan_pwm[0])
        hwd_read_long(h->fan_pwm, &v);
    if (h->fan_cdev[0]) {
        snprintf(p, sizeof p, "%s/cur_state", h->fan_cdev);
        hwd_read_long(p, &cur);
        snprintf(p, sizeof p, "%s/max_state", h->fan_cdev);
        hwd_read_long(p, &max);
    }
    snprintf(out, n, "mode=%s backend=%s pwm=%ld pct=%ld state=%ld max_state=%ld trips=%d manual_pct=%d soc_c=%d",
             h->st.fan_mode, h->fan_backend, v, v < 0 ? -1 : v * 100 / 255, cur, max, h->ntrips,
             h->st.fan_manual_pct, hwd_soc_temp_mc(h) / 1000);
}

void hwd_fan_trips(struct hwd *h, char *out, size_t n)
{
    out[0] = 0;
    for (int i = 0; i < h->ntrips; i++) {
        long v = -1;
        size_t l = strlen(out);
        hwd_read_long(h->trip_path[i], &v);
        snprintf(out + l, n - l, "%s%s:%s=%ld/%ld", i ? " " : "", h->trip_zone[i],
                 strrchr(h->trip_path[i], '/') + 1, v, h->trip_orig[i]);
    }
}

/* ---- lights ----------------------------------------------------------------------------------------------- */

static const struct { const char *name; unsigned rgb; } colours[] = {
    {"yellow", 0xffbe00}, {"red", 0xff0000}, {"green", 0x00ff00}, {"blue", 0x0000ff}, {"purple", 0xa000ff},
    {"pink", 0xff3c96}, {"cyan", 0x00dcff}, {"white", 0xffffff}, {"amber", 0xff8c00}, {NULL, 0},
};

int hwd_parse_color(const char *spec, unsigned *rgb)
{
    char *end;
    unsigned long v;
    for (int i = 0; colours[i].name; i++)
        if (!strcasecmp(spec, colours[i].name)) {
            *rgb = colours[i].rgb;
            return 0;
        }
    if (*spec == '#')
        spec++;
    if (strlen(spec) != 6)
        return -1;
    v = strtoul(spec, &end, 16);
    if (*end)
        return -1;
    *rgb = (unsigned)v;
    return 0;
}

/* each system's colour, the way its maker's machines look (handheld's system_colour) */
const char *hwd_system_colour(const char *s)
{
    static const char *red[] = {"nds", "ndsiware", "3ds", "n3ds", "gb", "gbh", "gbc", "gbch", "gba", "gbah", "snes",
                                "sfc", "nes", "famicom", "fds", "n64", "gamecube", "wii", "wiiu", "switch",
                                "virtualboy", "pokemini", "gameandwatch", NULL};
    static const char *blue[] = {"megadrive", "genesis", "mastersystem", "gamegear", "sega32x", "segacd", "megacd",
                                 "saturn", "dreamcast", "naomi", "atomiswave", "sg-1000", NULL};
    static const char *cyan[] = {"psx", "ps2", "psp", "pspminis", "psvita", "ps3", NULL};
    static const char *purple[] = {"pcengine", "tg16", "pcenginecd", "tg16cd", "supergrafx", "pcfx", NULL};
    static const char *pink[] = {"arcade", "mame", "fbneo", "fbn", "cps1", "cps2", "cps3", "neogeo", "neocd", NULL};
    static const struct { const char **ids; const char *c; } t[] = {
        {red, "red"}, {blue, "blue"}, {cyan, "cyan"}, {purple, "purple"}, {pink, "pink"}};
    for (size_t k = 0; k < sizeof t / sizeof t[0]; k++)
        for (int i = 0; t[k].ids[i]; i++)
            if (!strcmp(s, t[k].ids[i]))
                return t[k].c;
    return "yellow";
}

/* rings <brightness> <rgb>: every light, its channels in its own order */
static void rings(struct hwd *h, int level, unsigned rgb)
{
    char p[HWD_PATH + 32], v[32];
    int c[3];
    for (int i = 0; i < h->nrings; i++) {
        for (int k = 0; k < 3; k++) {
            switch (h->ring_order[i][k]) {
            case 'r': c[k] = (rgb >> 16) & 255; break;
            case 'g': c[k] = (rgb >> 8) & 255; break;
            default: c[k] = rgb & 255; break;
            }
        }
        snprintf(v, sizeof v, "%d %d %d", c[0], c[1], c[2]);
        snprintf(p, sizeof p, "%s/multi_intensity", h->rings[i]);
        hwd_write_str(p, v);
        snprintf(p, sizeof p, "%s/brightness", h->rings[i]);
        hwd_write_long(p, level);
    }
}

static void rings_level(struct hwd *h, int level)
{
    char p[HWD_PATH + 32];
    for (int i = 0; i < h->nrings; i++) {
        snprintf(p, sizeof p, "%s/brightness", h->rings[i]);
        hwd_write_long(p, level);
    }
}

/* the battery's colour: green, amber while charging, red under 20 % */
static unsigned battery_colour(struct hwd *h)
{
    char dir[HWD_PATH], b[16][HWD_PATH], p[HWD_PATH + 16], v[32];
    long cap = 100;
    int charging = 0;
    sysp(h, dir, sizeof dir, "class/power_supply");
    int n = list_dir(dir, "*", b, 16);
    for (int i = 0; i < n; i++) {
        snprintf(p, sizeof p, "%s/type", b[i]);
        if (hwd_read_line(p, v, sizeof v) || strcmp(v, "Battery"))
            continue;
        snprintf(p, sizeof p, "%s/capacity", b[i]);
        hwd_read_long(p, &cap);
        snprintf(p, sizeof p, "%s/status", b[i]);
        charging = !hwd_read_line(p, v, sizeof v) && !strcmp(v, "Charging");
        break;
    }
    return charging ? 0xff8c00 : cap < 20 ? 0xff0000 : 0x00ff00;
}

static unsigned current_colour(struct hwd *h)
{
    unsigned rgb = h->st.leds_rgb;
    if (!strcmp(h->st.leds_mode, "battery"))
        return battery_colour(h);
    if (!strcmp(h->st.leds_mode, "system"))
        hwd_parse_color(hwd_system_colour(h->st.leds_system), &rgb);
    return rgb;
}

void hwd_leds_apply(struct hwd *h)
{
    if (!h->nrings)
        return;
    h->breathe_step = 0;
    if (h->st.suspended || !strcmp(h->st.leds_mode, "off"))
        rings(h, 0, 0);
    else
        rings(h, h->st.leds_level, current_colour(h));
}

int hwd_leds_mode(struct hwd *h, const char *mode)
{
    if (strcmp(mode, "off") && strcmp(mode, "battery") && strcmp(mode, "system") && strcmp(mode, "color"))
        return -1;
    copy(h->st.leds_mode, sizeof h->st.leds_mode, mode);
    hwd_leds_apply(h);
    return 0;
}

int hwd_leds_color(struct hwd *h, const char *spec)
{
    unsigned rgb;
    if (hwd_parse_color(spec, &rgb))
        return -1;
    h->st.leds_rgb = rgb;
    copy(h->st.leds_mode, sizeof h->st.leds_mode, "color");
    hwd_leds_apply(h);
    return 0;
}

int hwd_leds_level(struct hwd *h, const char *spec)
{
    char *end;
    long v;
    if (!strcmp(spec, "low")) v = 64;
    else if (!strcmp(spec, "medium")) v = 140;
    else if (!strcmp(spec, "high")) v = 255;
    else {
        v = strtol(spec, &end, 10);
        if (*end || end == spec || v < 0 || v > 255)
            return -1;
    }
    h->st.leds_level = (int)v;
    hwd_leds_apply(h);
    return 0;
}

int hwd_leds_effect(struct hwd *h, const char *effect)
{
    if (strcmp(effect, "none") && strcmp(effect, "breathe"))
        return -1;
    copy(h->st.leds_effect, sizeof h->st.leds_effect, effect);
    hwd_leds_apply(h);
    return 0;
}

int hwd_leds_system(struct hwd *h, const char *system)
{
    copy(h->st.leds_system, sizeof h->st.leds_system, system);
    if (!strcmp(h->st.leds_mode, "system"))
        hwd_leds_apply(h);
    return 0;
}

/* a slow pulse of the brightness (handheld's leds-effect breathe): 20 steps of 100 ms */
void hwd_leds_tick_breathe(struct hwd *h)
{
    static const int steps[] = {8, 16, 32, 48, 64, 96, 128, 160, 192, 224, 255, 224, 192, 160, 128, 96, 64, 48, 32, 16};
    if (!h->nrings || h->st.suspended || !strcmp(h->st.leds_mode, "off") || !strcmp(h->st.leds_mode, "battery"))
        return;
    rings_level(h, steps[h->breathe_step] * h->st.leds_level / 255);
    h->breathe_step = (h->breathe_step + 1) % (int)(sizeof steps / sizeof steps[0]);
}

void hwd_leds_tick_battery(struct hwd *h)
{
    if (h->nrings && !h->st.suspended && !strcmp(h->st.leds_mode, "battery"))
        rings(h, h->st.leds_level, battery_colour(h));
}

void hwd_leds_get(struct hwd *h, char *out, size_t n)
{
    snprintf(out, n, "mode=%s color=%06x level=%d effect=%s system=%s rings=%d", h->st.leds_mode, h->st.leds_rgb,
             h->st.leds_level, h->st.leds_effect, h->st.leds_system[0] ? h->st.leds_system : "-", h->nrings);
}

/* ---- clocks ----------------------------------------------------------------------------------------------- */

/* cluster <policy> <governor> <max share %> [min share %]: banana-perf's, the nearest available clocks at or under
 * the shares of the cluster's own top clock */
static void cluster(struct hwd *h, int idx, const char *gov, int maxpct, int minpct)
{
    const char *pol = h->policies[idx];
    char p[HWD_PATH + 40], fs[1024], govs[256];
    long lo = 0, hi = 0, mx, mn;
    snprintf(p, sizeof p, "%s/cpuinfo_min_freq", pol);
    hwd_read_long(p, &lo);
    snprintf(p, sizeof p, "%s/cpuinfo_max_freq", pol);
    hwd_read_long(p, &hi);
    if (hi <= 0)
        return;
    snprintf(p, sizeof p, "%s/scaling_available_frequencies", pol);
    if (hwd_read_line(p, fs, sizeof fs))
        fs[0] = 0;
    mx = hi;
    mn = lo;
    for (int pass = 0; pass < 2; pass++) {
        int pct = pass ? minpct : maxpct;
        long best = lo, cap = hi / 100 * pct;
        char *save = NULL, buf[1024];
        if (pct <= 0 || pct >= 100)
            continue;
        copy(buf, sizeof buf, fs);
        for (char *t = strtok_r(buf, " ", &save); t; t = strtok_r(NULL, " ", &save)) {
            long f = atol(t);
            if (f <= cap && f > best)
                best = f;
        }
        if (!*fs)
            best = cap;   /* a driver without a table (qcom-cpufreq-hw always has one) */
        if (pass)
            mn = best;
        else
            mx = best;
    }
    snprintf(p, sizeof p, "%s/scaling_min_freq", pol);
    hwd_write_long(p, lo);
    snprintf(p, sizeof p, "%s/scaling_max_freq", pol);
    hwd_write_long(p, mx);
    snprintf(p, sizeof p, "%s/scaling_min_freq", pol);
    hwd_write_long(p, mn);
    snprintf(p, sizeof p, "%s/scaling_available_governors", pol);
    if (hwd_read_line(p, govs, sizeof govs))
        govs[0] = 0;
    /* a governor this kernel doesn't offer (schedutil on a vendor kernel that has walt): its own default */
    if (!has_word(govs, gov))
        gov = default_cpu_gov[idx];
    if (*gov) {
        snprintf(p, sizeof p, "%s/scaling_governor", pol);
        hwd_write_str(p, gov);
    }
}

static void gpu_governor(struct hwd *h, const char *gov)
{
    char p[HWD_PATH + 32], govs[256];
    if (!h->gpu[0])
        return;
    snprintf(p, sizeof p, "%s/available_governors", h->gpu);
    if (hwd_read_line(p, govs, sizeof govs) || !has_word(govs, gov))
        gov = default_gpu_gov;
    if (*gov) {
        snprintf(p, sizeof p, "%s/governor", h->gpu);
        hwd_write_str(p, gov);
    }
}

int hwd_perf(struct hwd *h, const char *mode)
{
    int maxpct = 100, minpct = 0;
    const char *gov = "schedutil";
    if (!strcmp(mode, "ui") || !strcmp(mode, "balanced")) {}
    else if (!strcmp(mode, "powersave")) maxpct = 71;
    else if (!strcmp(mode, "steady")) minpct = 55;
    else if (!strcmp(mode, "performance") || !strcmp(mode, "superdrastic")) gov = "performance";
    else return -1;
    for (int i = 0; i < h->npolicies; i++)
        cluster(h, i, gov, maxpct, minpct);
    /* the GPU: performance only for "performance"; otherwise the kernel's own governor. (The Linux image put the
     * GPU on powersave for its software-drawn menu; Android's launcher is composited by the GPU, so it isn't) */
    gpu_governor(h, !strcmp(mode, "performance") ? "performance" : "");
    copy(h->st.perf, sizeof h->st.perf, mode);
    return 0;
}

int hwd_gpu_boost_available(struct hwd *h)
{
    char p[HWD_PATH + 32], fs[1024], want[32];
    if (!h->gpu[0])
        return 0;
    snprintf(p, sizeof p, "%s/available_frequencies", h->gpu);
    snprintf(want, sizeof want, "%ld", h->cfg.gpu_boost_hz);
    return !hwd_read_line(p, fs, sizeof fs) && has_word(fs, want);
}

int hwd_gpu_boost(struct hwd *h, int on)
{
    char p[HWD_PATH + 32];
    if (!hwd_gpu_boost_available(h))
        return -1;
    snprintf(p, sizeof p, "%s/max_freq", h->gpu);
    if (hwd_write_long(p, on ? h->cfg.gpu_boost_hz : h->cfg.gpu_stock_hz))
        return -1;
    h->st.gpu_boost = on;
    return 0;
}

void hwd_perf_get(struct hwd *h, char *out, size_t n)
{
    char p[HWD_PATH + 32], gov[32] = "-";
    long mx = 0, top = 0;
    size_t len;
    snprintf(out, n, "profile=%s gpu_boost=%d", h->st.perf, h->st.gpu_boost);
    for (int i = 0; i < h->npolicies; i++) {
        snprintf(p, sizeof p, "%s/scaling_governor", h->policies[i]);
        if (hwd_read_line(p, gov, sizeof gov))
            copy(gov, sizeof gov, "-");
        snprintf(p, sizeof p, "%s/scaling_max_freq", h->policies[i]);
        hwd_read_long(p, &mx);
        snprintf(p, sizeof p, "%s/cpuinfo_max_freq", h->policies[i]);
        hwd_read_long(p, &top);
        len = strlen(out);
        snprintf(out + len, n - len, " %s=%s:%ld/%ld", strrchr(h->policies[i], '/') + 1, gov, mx / 1000, top / 1000);
    }
}

/* ---- panels ----------------------------------------------------------------------------------------------- */

/* the panels' backlights: hwd.conf's names, else the mainline names (the DSI panel drivers name their backlight
 * "<host>.<channel>": top on dsi@ae96000, bottom on dsi@ae94000), else a Qualcomm vendor kernel's panelN-backlight */
static int backlight_dir(struct hwd *h, const char *which, char *out, size_t n)
{
    const char *conf = !strcmp(which, "top") ? h->cfg.backlight_top : !strcmp(which, "bottom") ? h->cfg.backlight_bottom : NULL;
    /* (panel0 is the top 6" panel, panel1 the bottom 3.92" one: thor-screenblank confirmed the pairing on hardware) */
    const char *cand_top[] = {"panel0-backlight", "ae96000.dsi.0", NULL};
    const char *cand_bot[] = {"panel1-backlight", "ae94000.dsi.0", NULL};
    const char **cand = !strcmp(which, "top") ? cand_top : cand_bot;
    char rel[200];
    if (!conf)
        return -1;
    if (*conf) {
        snprintf(rel, sizeof rel, "class/backlight/%s", conf);
        sysp(h, out, n, rel);
        return exists(out) ? 0 : -1;
    }
    for (int i = 0; cand[i]; i++) {
        snprintf(rel, sizeof rel, "class/backlight/%s", cand[i]);
        sysp(h, out, n, rel);
        if (exists(out))
            return 0;
    }
    return -1;
}

int hwd_backlight_get(struct hwd *h, const char *which, long *val, long *max)
{
    char d[HWD_PATH], p[HWD_PATH + 32];
    if (backlight_dir(h, which, d, sizeof d))
        return -1;
    snprintf(p, sizeof p, "%s/max_brightness", d);
    if (hwd_read_long(p, max))
        return -1;
    snprintf(p, sizeof p, "%s/brightness", d);
    return hwd_read_long(p, val);
}

int hwd_backlight_set(struct hwd *h, const char *which, long val)
{
    char d[HWD_PATH], p[HWD_PATH + 32];
    long max;
    if (backlight_dir(h, which, d, sizeof d))
        return -1;
    snprintf(p, sizeof p, "%s/max_brightness", d);
    if (hwd_read_long(p, &max) || val < 0 || val > max)
        return -1;
    snprintf(p, sizeof p, "%s/brightness", d);
    return hwd_write_long(p, val);
}

/* ---- sleep ------------------------------------------------------------------------------------------------ */

/* ROCKNIX turns the SM8550's stick lights off before a sleep (lit, they keep it from sleeping); the fan stops too */
void hwd_suspend(struct hwd *h)
{
    h->st.suspended = 1;
    if (h->nrings)
        rings(h, 0, 0);
    /* (the "trips" backend leaves the fan to the kernel's governor, asleep as awake) */
    if (is_pwm(h)) {
        h->fan_last = -1;
        fan_write(h, 0);
    }
}

void hwd_resume(struct hwd *h)
{
    h->st.suspended = 0;
    hwd_leds_apply(h);
    h->fan_last = -1;
    hwd_fan_tick(h);
}

/* ---- statistics (sysstat's keys) -------------------------------------------------------------------------- */

static void add(char *out, size_t n, const char *fmt, long v)
{
    size_t l = strlen(out);
    snprintf(out + l, n - l, fmt, v);
}

void hwd_stats(struct hwd *h, char *out, size_t n)
{
    char p[HWD_PATH + 48], line[512], dir[HWD_PATH], ent[128][HWD_PATH];
    unsigned long long u, ni, s, i, w, q, sq, st;
    long v, m;
    FILE *f;
    out[0] = 0;

    /* the CPU: busy since the last call (the first says nothing) */
    hwd_join(p, sizeof p, h->cfg.proc, "stat");
    if ((f = fopen(p, "re"))) {
        if (fgets(line, sizeof line, f) &&
            sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &u, &ni, &s, &i, &w, &q, &sq, &st) == 8) {
            unsigned long long tot = u + ni + s + i + w + q + sq + st, idle = i + w;
            if (h->stat_total && tot > h->stat_total) {
                unsigned long long dt = tot - h->stat_total, di = idle - h->stat_idle;
                add(out, n, "cpu=%ld ", (long)((100 * (dt - di) + dt / 2) / dt));
            }
            h->stat_total = tot;
            h->stat_idle = idle;
        }
        fclose(f);
    }
    m = 0;
    for (int k = 0; k < h->npolicies; k++) {
        snprintf(p, sizeof p, "%s/scaling_cur_freq", h->policies[k]);
        if (!hwd_read_long(p, &v) && v > m)
            m = v;
    }
    if (m > 0)
        add(out, n, "cpu_mhz=%ld ", m / 1000);
    /* the hottest zone of each kind */
    {
        long ct = 0, gt = 0;
        int nz;
        char type[64];
        sysp(h, dir, sizeof dir, "class/thermal");
        nz = list_dir(dir, "thermal_zone*", ent, 128);
        for (int k = 0; k < nz; k++) {
            snprintf(p, sizeof p, "%s/temp", ent[k]);
            if (hwd_read_long(p, &v))
                continue;
            snprintf(p, sizeof p, "%s/type", ent[k]);
            if (hwd_read_line(p, type, sizeof type))
                continue;
            if (!strncasecmp(type, "cpu", 3) || strstr(type, "-cpu"))
                ct = v > ct ? v : ct;
            else if (!strncasecmp(type, "gpu", 3) || strstr(type, "-gpu"))
                gt = v > gt ? v : gt;
        }
        if (ct > 0)
            add(out, n, "cpu_c=%ld ", ct / 1000);
        if (gt > 0)
            add(out, n, "gpu_c=%ld ", gt / 1000);
    }
    /* the GPU: its devfreq clock (kgsl's gpuclk on a vendor kernel); busy only where a driver says */
    if (h->gpu[0]) {
        snprintf(p, sizeof p, "%s/cur_freq", h->gpu);
        if (!hwd_read_long(p, &v))
            add(out, n, "gpu_mhz=%ld ", v / 1000000);
    } else {
        sysp(h, p, sizeof p, "class/kgsl/kgsl-3d0/gpuclk");
        if (!hwd_read_long(p, &v))
            add(out, n, "gpu_mhz=%ld ", v / 1000000);
    }
    sysp(h, p, sizeof p, "class/kgsl/kgsl-3d0/gpu_busy_percentage");
    if (!hwd_read_long(p, &v))
        add(out, n, "gpu=%ld ", v);
    else {
        sysp(h, dir, sizeof dir, "class/drm");
        int nc = list_dir(dir, "card[0-9]", ent, 32);
        for (int k = 0; k < nc; k++) {
            snprintf(p, sizeof p, "%s/device/gpu_busy_percent", ent[k]);
            if (!hwd_read_long(p, &v)) {
                add(out, n, "gpu=%ld ", v);
                break;
            }
        }
    }
    /* memory in use: all of it less what could be had at once */
    hwd_join(p, sizeof p, h->cfg.proc, "meminfo");
    if ((f = fopen(p, "re"))) {
        long t = 0, a = -1;
        while (fgets(line, sizeof line, f)) {
            sscanf(line, "MemTotal: %ld kB", &t);
            sscanf(line, "MemAvailable: %ld kB", &a);
        }
        fclose(f);
        if (t > 0 && a >= 0) {
            add(out, n, "mem_used_mb=%ld ", (t - a) / 1024);
            add(out, n, "mem_total_mb=%ld ", t / 1024);
            add(out, n, "mem_pct=%ld ", (t - a) * 100 / t);
        }
    }
    /* the battery: into it positive, out of it negative, whichever sign the driver uses */
    sysp(h, dir, sizeof dir, "class/power_supply");
    {
        int nb = list_dir(dir, "*", ent, 32);
        for (int k = 0; k < nb; k++) {
            char type[32], stt[32] = "";
            long cu, vo, pw, mw;
            int hp, hc;
            snprintf(p, sizeof p, "%s/type", ent[k]);
            if (hwd_read_line(p, type, sizeof type) || strcmp(type, "Battery"))
                continue;
            snprintf(p, sizeof p, "%s/status", ent[k]);
            if (!hwd_read_line(p, stt, sizeof stt) && *stt) {
                size_t l = strlen(out);
                snprintf(out + l, n - l, "power_state=%s ", stt);
            }
            snprintf(p, sizeof p, "%s/capacity", ent[k]);
            if (!hwd_read_long(p, &v))
                add(out, n, "battery=%ld ", v);
            snprintf(p, sizeof p, "%s/power_now", ent[k]);
            hp = !hwd_read_long(p, &pw);
            snprintf(p, sizeof p, "%s/current_now", ent[k]);
            hc = !hwd_read_long(p, &cu);
            snprintf(p, sizeof p, "%s/voltage_now", ent[k]);
            hc = hc && !hwd_read_long(p, &vo);
            if (hp)
                mw = pw / 1000;
            else if (hc)
                mw = (cu / 1000) * (vo / 1000) / 1000;
            else
                break;
            if (!strcmp(stt, "Discharging") && mw > 0)
                mw = -mw;
            else if ((!strcmp(stt, "Charging") || !strcmp(stt, "Full")) && mw < 0)
                mw = -mw;
            add(out, n, "power_mw=%ld ", mw);
            break;
        }
    }
    if (h->fan_pwm[0] && !hwd_read_long(h->fan_pwm, &v))
        add(out, n, "fan_pct=%ld ", v * 100 / 255);
    if (*out)
        out[strlen(out) - 1] = 0;
}
