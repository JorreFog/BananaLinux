/*
 * banana-hwd: the AYN Thor's own hardware (fan, stick lights, clocks, panels' backlights, statistics) behind one
 * line-based socket. The hardware layer; the socket and the Home button are in banana-hwd.c.
 *
 * Every path is under a root (BANANA_SYS for /sys, BANANA_PROC for /proc, BANANA_DEV for /dev), so the same code
 * runs on the handheld and against a made-up tree on a PC (tests/hwd-test.sh), as BananaOS's shell tools do.
 */
#ifndef BANANA_HWD_HW_H
#define BANANA_HWD_HW_H

#include <stddef.h>

#define HWD_PATH 512
#define HWD_MAX_ZONES 64
#define HWD_MAX_POLICIES 8
#define HWD_LED_RINGS 16
#define HWD_MAX_TRIPS 96

struct hwd_config {
    char sys[HWD_PATH];          /* /sys */
    char proc[HWD_PATH];         /* /proc */
    char dev[HWD_PATH];          /* /dev */
    char state_file[HWD_PATH];   /* where the settings are kept between boots */
    /* overrides from hwd.conf (empty: found by looking) */
    char fan_pwm[HWD_PATH];      /* e.g. class/hwmon/hwmon3/pwm1 */
    char backlight_top[128];     /* class/backlight/<name> */
    char backlight_bottom[128];
    char gpu_devfreq[HWD_PATH];  /* e.g. class/devfreq/3d00000.gpu */
    char led_nodes[512];         /* the stick lights: class/leds names, space separated (empty: found by looking) */
    char led_order[8];           /* the order multi_intensity takes R, G and B: "rgb", "bgr" or "auto" */
    char fan_backend[16];        /* auto | trips | pwm | none */
    char home_keys[128];         /* key codes that are the Home button, comma separated ("194") */
    char home_devices[256];      /* input device names to watch (exact names, comma separated; empty: any) */
    int home_hold_ms;            /* held this long: the quick menu (3000) */
    int fan_interval_ms;         /* the fan curve's period (3000) */
    long gpu_boost_hz;           /* the GPU's raised top clock (1000000000), where the kernel's table has it */
    long gpu_stock_hz;           /* and as shipped (680000000) */
};

struct hwd_state {               /* what is kept between boots, and what the launcher set */
    char fan_mode[16];           /* quiet | moderate | aggressive | max | off | manual */
    int fan_manual_pct;          /* 0-100, for manual */
    char leds_mode[16];          /* off | battery | system | color */
    unsigned leds_rgb;           /* 0xRRGGBB, for color */
    int leds_level;              /* 0-255 */
    char leds_effect[16];        /* none | breathe */
    char leds_system[32];        /* the system playing (its colour, for "system") */
    char perf[16];               /* ui | powersave | balanced | steady | performance | superdrastic */
    int gpu_boost;               /* 0 | 1 */
    int suspended;               /* lights and fan off for a sleep */
};

struct hwd {
    struct hwd_config cfg;
    struct hwd_state st;
    /* found at start */
    char fan_pwm[HWD_PATH];
    char zones[HWD_MAX_ZONES][HWD_PATH];   /* temp files the fan follows (cpu*, gpu*) */
    int nzones;
    char policies[HWD_MAX_POLICIES][HWD_PATH];
    int npolicies;
    char gpu[HWD_PATH];
    char rings[HWD_LED_RINGS][HWD_PATH];
    char ring_order[HWD_LED_RINGS][4];      /* "rgb" (AYN's vendor kernel) or "bgr" (ROCKNIX's htr3212 wiring) */
    int nrings;
    /* the fan, the vendor kernel's way: trip points bound to the pwm-fan cooling device (step_wise drives pwm1) */
    char fan_backend[16];                  /* trips | pwm | none */
    char trip_path[HWD_MAX_TRIPS][HWD_PATH];
    char trip_zone[HWD_MAX_TRIPS][32];     /* the zone's type: cpu-0-0-0, gpuss-0, usb-therm ... */
    long trip_orig[HWD_MAX_TRIPS];         /* as the kernel had them at boot */
    int ntrips;
    char fan_cdev[HWD_PATH];               /* thermal/cooling_deviceN of type pwm-fan */
    /* running */
    int fan_last;
    int breathe_step;
    unsigned long long stat_total, stat_idle;
};

/* setup */
void hwd_config_defaults(struct hwd_config *c);
int hwd_config_load(struct hwd_config *c, const char *file);
void hwd_probe(struct hwd *h);
void hwd_state_defaults(struct hwd_state *s);
int hwd_state_load(struct hwd *h);
int hwd_state_save(const struct hwd *h);
void hwd_apply_all(struct hwd *h);

/* capabilities, a word each, space separated: fan leds perf gpuboost backlight_top backlight_bottom */
void hwd_caps(struct hwd *h, char *out, size_t n);

/* fan (PROTOCOL.md: "trips" moves the thermal trips bound to the fan, "pwm" runs ROCKNIX's curve on pwm1) */
int hwd_fan_mode(struct hwd *h, const char *mode);
int hwd_fan_manual(struct hwd *h, int pct);
void hwd_fan_tick(struct hwd *h);              /* the curve, every fan_interval_ms */
void hwd_fan_get(struct hwd *h, char *out, size_t n);
void hwd_fan_trips(struct hwd *h, char *out, size_t n);   /* "<path>=<now>/<boot>" each */
int hwd_soc_temp_mc(struct hwd *h);            /* mean of the zones, millidegrees */

/* lights */
int hwd_leds_mode(struct hwd *h, const char *mode);
int hwd_leds_color(struct hwd *h, const char *spec);
int hwd_leds_level(struct hwd *h, const char *spec);
int hwd_leds_effect(struct hwd *h, const char *effect);
int hwd_leds_system(struct hwd *h, const char *system);
void hwd_leds_apply(struct hwd *h);
void hwd_leds_tick_breathe(struct hwd *h);     /* every 100 ms while breathing */
void hwd_leds_tick_battery(struct hwd *h);     /* every 30 s in battery mode */
void hwd_leds_get(struct hwd *h, char *out, size_t n);
int hwd_parse_color(const char *spec, unsigned *rgb);
const char *hwd_system_colour(const char *system);

/* clocks */
int hwd_perf(struct hwd *h, const char *profile);
int hwd_gpu_boost(struct hwd *h, int on);
int hwd_gpu_boost_available(struct hwd *h);
void hwd_perf_get(struct hwd *h, char *out, size_t n);

/* panels */
int hwd_backlight_get(struct hwd *h, const char *which, long *val, long *max);
int hwd_backlight_set(struct hwd *h, const char *which, long val);

/* sleep */
void hwd_suspend(struct hwd *h);
void hwd_resume(struct hwd *h);

/* statistics, sysstat's keys: "cpu=12 cpu_mhz=1996 ..." */
void hwd_stats(struct hwd *h, char *out, size_t n);

/* helpers */
int hwd_read_long(const char *path, long *v);
int hwd_read_line(const char *path, char *buf, size_t n);
int hwd_write_str(const char *path, const char *s);
int hwd_write_long(const char *path, long v);
void hwd_join(char *out, size_t n, const char *root, const char *rel);

#endif
