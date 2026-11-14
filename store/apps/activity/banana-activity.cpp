/*
 * banana-activity.cpp - player activity for BananaOS.
 *
 * A standalone app in the menu's own look: the tinted sheet, banded panels, the 4x4
 * dither tile, Pixelify Sans (baked into activity-font.h, so the binary needs no
 * FreeType) and the accent from banana/settings.conf.
 *
 * Two views: LIBRARY (what I play) and TIMELINE (when I play); SELECT switches, and
 * TRENDS holds the longer view. L/R change the period, X filters by system, Y sorts.
 *
 * It reads the menu's banana/playstats.tsv (read only) and keeps its own history in
 * banana/activity/: sessions.tsv, snapshot.tsv and activity.log. No other file is
 * written. Everything is plain C++17 + DRM/evdev; no FreeType, no libdrm, no SDL.
 *
 *   banana-activity [--root /data] [--smoke PREFIX] [--view library|timeline|trends]
 *                   [--period week|month|all] [--day N] [--size 640x480|1024x768]
 *                   [--connector DSI-2]
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "activity-font.h"
#include "activity-logo.h"

#ifdef _WIN32
#define MAKE_DIR(p) _mkdir(p)
#else
#define MAKE_DIR(p) mkdir((p), 0755)
#endif

static void localTime(time_t t, struct tm *out)
{
#ifdef _WIN32
	localtime_s(out, &t);
#else
	localtime_r(&t, out);
#endif
}

/* ---- DRM/KMS + evdev, the same minimal shim the other standalone apps use ---- */
#ifndef _WIN32
#define DRM_IOCTL_BASE 'd'
#define DRM_IO(nr) _IO(DRM_IOCTL_BASE, nr)
#define DRM_IOWR(nr, type) _IOWR(DRM_IOCTL_BASE, nr, type)
#define DRM_MODE_CONNECTED 1
#define DRM_MODE_CONNECTOR_DSI 16
#define DRM_MODE_CONNECTOR_VIRTUAL 15
#define DRM_MODE_CONNECTOR_eDP 14

struct drm_mode_modeinfo {
	uint32_t clock;
	uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
	uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
	uint32_t vrefresh, flags, type;
	char name[32];
};
struct drm_mode_card_res {
	uint64_t fb_id_ptr, crtc_id_ptr, connector_id_ptr, encoder_id_ptr;
	uint32_t count_fbs, count_crtcs, count_connectors, count_encoders;
	uint32_t min_width, max_width, min_height, max_height;
};
struct drm_mode_get_connector {
	uint64_t encoders_ptr, modes_ptr, props_ptr, prop_values_ptr;
	uint32_t count_modes, count_props, count_encoders, encoder_id, connector_id;
	uint32_t connector_type, connector_type_id, connection, mm_width, mm_height, subpixel, pad;
};
struct drm_mode_get_encoder {
	uint32_t encoder_id, encoder_type, crtc_id, possible_crtcs, possible_clones;
};
struct drm_mode_crtc {
	uint64_t set_connectors_ptr;
	uint32_t count_connectors, crtc_id, fb_id, x, y, gamma_size, mode_valid;
	struct drm_mode_modeinfo mode;
};
struct drm_mode_create_dumb { uint32_t height, width, bpp, flags, handle, pitch; uint64_t size; };
struct drm_mode_map_dumb { uint32_t handle, pad; uint64_t offset; };
struct drm_mode_destroy_dumb { uint32_t handle; };
struct drm_mode_fb_cmd { uint32_t fb_id, width, height, pitch, bpp, depth, handle; };

#define DRM_IOCTL_SET_MASTER DRM_IO(0x1e)
#define DRM_IOCTL_DROP_MASTER DRM_IO(0x1f)
#define DRM_IOCTL_MODE_GETRESOURCES DRM_IOWR(0xA0, struct drm_mode_card_res)
#define DRM_IOCTL_MODE_GETCRTC DRM_IOWR(0xA1, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_SETCRTC DRM_IOWR(0xA2, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_GETENCODER DRM_IOWR(0xA6, struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_GETCONNECTOR DRM_IOWR(0xA7, struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_ADDFB DRM_IOWR(0xAE, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_RMFB DRM_IOWR(0xAF, unsigned int)
#define DRM_IOCTL_MODE_CREATE_DUMB DRM_IOWR(0xB2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB DRM_IOWR(0xB3, struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_DESTROY_DUMB DRM_IOWR(0xB4, struct drm_mode_destroy_dumb)

namespace banana {

enum class Key { UP, DOWN, LEFT, RIGHT, A, B, X, Y, L, R, SELECT, START, UNKNOWN };

struct InputEvent {
	enum Type { PRESS, RELEASE, TOUCH_UP } type;
	Key key = Key::UNKNOWN;
	int screen = 1;
	int x = 0, y = 0;
};

static int drm_ioctl(int fd, unsigned long req, void *arg)
{
	int ret;
	do {
		ret = ioctl(fd, req, arg);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));
	return ret;
}

static const char *conn_str(uint32_t t)
{
	if (t == DRM_MODE_CONNECTOR_DSI) return "DSI";
	if (t == DRM_MODE_CONNECTOR_VIRTUAL) return "Virtual";
	if (t == DRM_MODE_CONNECTOR_eDP) return "eDP";
	return "Other";
}

class Display {
	struct Panel {
		uint32_t connector_id = 0, crtc_id = 0;
		struct drm_mode_modeinfo mode = {};
		struct drm_mode_crtc saved_crtc = {};
		bool has_saved = false, connected = false;
		int w = 640, h = 480, front = 0;
		struct {
			uint32_t fb_id = 0, handle = 0, pitch = 0;
			uint64_t size = 0;
			uint32_t *map = nullptr;
		} bufs[2];
	} panels[2];
	int drm_fd = -1;

	bool alloc_fb(Panel &p, int idx)
	{
		struct drm_mode_create_dumb creq = { (uint32_t)p.h, (uint32_t)p.w, 32, 0, 0, 0, 0 };
		if (drm_ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) return false;
		p.bufs[idx].handle = creq.handle;
		p.bufs[idx].pitch = creq.pitch;
		p.bufs[idx].size = creq.size;
		struct drm_mode_fb_cmd fbcmd = { 0, (uint32_t)p.w, (uint32_t)p.h, creq.pitch, 32, 24, creq.handle };
		if (drm_ioctl(drm_fd, DRM_IOCTL_MODE_ADDFB, &fbcmd) < 0) return false;
		p.bufs[idx].fb_id = fbcmd.fb_id;
		struct drm_mode_map_dumb mreq = { creq.handle, 0, 0 };
		if (drm_ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) return false;
		p.bufs[idx].map = (uint32_t *)mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, mreq.offset);
		if (p.bufs[idx].map == MAP_FAILED) {
			p.bufs[idx].map = nullptr;
			return false;
		}
		memset(p.bufs[idx].map, 0, creq.size);
		return true;
	}

public:
	~Display()
	{
		if (drm_fd < 0) return;
		for (int p = 0; p < 2; ++p) {
			if (!panels[p].connected) continue;
			if (panels[p].has_saved) {
				panels[p].saved_crtc.set_connectors_ptr = (uintptr_t)&panels[p].connector_id;
				panels[p].saved_crtc.count_connectors = 1;
				drm_ioctl(drm_fd, DRM_IOCTL_MODE_SETCRTC, &panels[p].saved_crtc);
			}
			for (int b = 0; b < 2; ++b) {
				if (panels[p].bufs[b].map) munmap(panels[p].bufs[b].map, panels[p].bufs[b].size);
				if (panels[p].bufs[b].fb_id) drm_ioctl(drm_fd, DRM_IOCTL_MODE_RMFB, &panels[p].bufs[b].fb_id);
				if (panels[p].bufs[b].handle) {
					struct drm_mode_destroy_dumb d = { panels[p].bufs[b].handle };
					drm_ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
				}
			}
		}
		drm_ioctl(drm_fd, DRM_IOCTL_DROP_MASTER, nullptr);
		close(drm_fd);
	}

	bool open(const std::string &want_top, std::string &error)
	{
		drm_fd = ::open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
		if (drm_fd < 0) {
			error = "Cannot open /dev/dri/card0";
			return false;
		}
		drm_ioctl(drm_fd, DRM_IOCTL_SET_MASTER, nullptr);

		struct drm_mode_card_res res = {};
		if (drm_ioctl(drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
			error = "DRM GETRESOURCES failed";
			return false;
		}
		std::vector<uint32_t> conn_ids(res.count_connectors), crtc_ids(res.count_crtcs),
			fb_ids(res.count_fbs), enc_ids(res.count_encoders);
		res.connector_id_ptr = (uintptr_t)conn_ids.data();
		res.crtc_id_ptr = (uintptr_t)crtc_ids.data();
		res.fb_id_ptr = (uintptr_t)fb_ids.data();
		res.encoder_id_ptr = (uintptr_t)enc_ids.data();
		if (drm_ioctl(drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
			error = "DRM GETRESOURCES 2 failed";
			return false;
		}

		uint32_t used_crtcs = 0;
		for (uint32_t i = 0; i < res.count_connectors; ++i) {
			struct drm_mode_get_connector conn = {};
			conn.connector_id = conn_ids[i];
			if (drm_ioctl(drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) continue;
			if (conn.connection != DRM_MODE_CONNECTED || conn.count_modes == 0) continue;

			std::vector<struct drm_mode_modeinfo> modes(conn.count_modes);
			std::vector<uint32_t> c_encs(conn.count_encoders), props(conn.count_props);
			std::vector<uint64_t> pvals(conn.count_props);
			conn.modes_ptr = (uintptr_t)modes.data();
			conn.encoders_ptr = (uintptr_t)c_encs.data();
			conn.props_ptr = (uintptr_t)props.data();
			conn.prop_values_ptr = (uintptr_t)pvals.data();
			if (drm_ioctl(drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) continue;

			char cname[32];
			snprintf(cname, sizeof(cname), "%s-%u", conn_str(conn.connector_type), conn.connector_type_id);
			Panel *target = nullptr;
			if (want_top == cname && !panels[0].connected) target = &panels[0];
			else if (!panels[1].connected) target = &panels[1];
			else if (!panels[0].connected) target = &panels[0];
			if (!target) continue;

			target->connector_id = conn.connector_id;
			target->mode = modes[0];
			target->w = modes[0].hdisplay;
			target->h = modes[0].vdisplay;

			uint32_t crtc_id = 0;
			if (conn.encoder_id) {
				struct drm_mode_get_encoder enc = {};
				enc.encoder_id = conn.encoder_id;
				if (drm_ioctl(drm_fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0) crtc_id = enc.crtc_id;
			}
			for (uint32_t e = 0; e < conn.count_encoders && !crtc_id; ++e) {
				struct drm_mode_get_encoder enc = {};
				enc.encoder_id = c_encs[e];
				if (drm_ioctl(drm_fd, DRM_IOCTL_MODE_GETENCODER, &enc) < 0) continue;
				for (uint32_t c = 0; c < res.count_crtcs; ++c) {
					if ((enc.possible_crtcs & (1u << c)) && !(used_crtcs & (1u << c))) {
						crtc_id = crtc_ids[c];
						used_crtcs |= (1u << c);
						break;
					}
				}
			}
			if (crtc_id) {
				target->crtc_id = crtc_id;
				target->saved_crtc.crtc_id = crtc_id;
				if (drm_ioctl(drm_fd, DRM_IOCTL_MODE_GETCRTC, &target->saved_crtc) == 0) target->has_saved = true;
				if (alloc_fb(*target, 0) && alloc_fb(*target, 1)) target->connected = true;
			}
		}
		if (!panels[0].connected && panels[1].connected) std::swap(panels[0], panels[1]);
		if (!panels[0].connected) {
			error = "No connected DRM panel";
			return false;
		}
		return true;
	}

	int width() const { return panels[0].w; }
	int height() const { return panels[0].h; }

	void present(const uint32_t *screens[2])
	{
		for (int i = 0; i < 2; ++i) {
			Panel &p = panels[i];
			if (!p.connected || !screens[i]) continue;
			uint32_t *dst = p.bufs[p.front].map;
			memcpy(dst, screens[i], (size_t)p.w * p.h * 4);
			struct drm_mode_crtc setc = {};
			setc.crtc_id = p.crtc_id;
			setc.fb_id = p.bufs[p.front].fb_id;
			setc.set_connectors_ptr = (uintptr_t)&p.connector_id;
			setc.count_connectors = 1;
			setc.mode = p.mode;
			setc.mode_valid = 1;
			drm_ioctl(drm_fd, DRM_IOCTL_MODE_SETCRTC, &setc);
			p.front ^= 1;
		}
	}
};

class Input {
	struct Dev {
		int fd = -1;
		bool touch = false;
		int slot = 0;
		int tx[10] = {}, ty[10] = {}, tid[10] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
		int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
	};
	std::vector<Dev> devs;
	int scr_w = 640, scr_h = 480;

	static bool testBit(const unsigned long *bits, int n)
	{
		return (bits[n / (8 * sizeof(long))] >> (n % (8 * sizeof(long)))) & 1;
	}

	// the handhelds' layout: A is east, B south, X north, Y west (both device trees)
	static Key mapKey(int code)
	{
		switch (code) {
		case BTN_DPAD_LEFT: case KEY_LEFT: return Key::LEFT;
		case BTN_DPAD_RIGHT: case KEY_RIGHT: return Key::RIGHT;
		case BTN_DPAD_UP: case KEY_UP: return Key::UP;
		case BTN_DPAD_DOWN: case KEY_DOWN: return Key::DOWN;
		case BTN_EAST: case KEY_ENTER: return Key::A;
		case BTN_SOUTH: case KEY_BACKSPACE: case KEY_ESC: return Key::B;
		case BTN_NORTH: case KEY_X: return Key::X;
		case BTN_WEST: case KEY_Y: return Key::Y;
		case BTN_TL: case BTN_TL2: case KEY_PAGEUP: return Key::L;
		case BTN_TR: case BTN_TR2: case KEY_PAGEDOWN: return Key::R;
		case BTN_START: case KEY_S: return Key::START;
		case BTN_SELECT: case KEY_TAB: return Key::SELECT;
		default: return Key::UNKNOWN;
		}
	}

public:
	bool debug = false;

	~Input()
	{
		for (auto &d : devs)
			if (d.fd >= 0) close(d.fd);
	}

	void open(int w, int h)
	{
		for (auto &d : devs)
			if (d.fd >= 0) close(d.fd);
		devs.clear();
		scr_w = w;
		scr_h = h;
		DIR *dir = opendir("/dev/input");
		if (!dir) return;
		struct dirent *ent;
		while ((ent = readdir(dir)) != nullptr) {
			if (strncmp(ent->d_name, "event", 5) != 0) continue;
			std::string path = std::string("/dev/input/") + ent->d_name;
			int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (fd < 0) continue;

			// what the menu asks evdev before it keeps a device (ui/src/input.cpp): a touchscreen by its
			// multitouch axes, a pad by the keys it actually reports. Names are not trusted.
			unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = {};
			unsigned long absbits[ABS_MAX / (8 * sizeof(long)) + 1] = {};
			ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
			ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absbits), absbits);

			Dev d;
			d.fd = fd;
			bool mt = testBit(absbits, ABS_MT_POSITION_X) && testBit(absbits, ABS_MT_POSITION_Y);
			bool st = !mt && testBit(absbits, ABS_X) && testBit(absbits, ABS_Y) && testBit(keys, BTN_TOUCH);
			bool pad = false;
			for (int k = KEY_ESC; k < KEY_MAX && !pad; k++)
				if (testBit(keys, k) && mapKey(k) != Key::UNKNOWN) pad = true;
			if (testBit(absbits, ABS_HAT0X)) pad = true;

			if (mt || st) {
				d.touch = true;
				struct input_absinfo ax = {}, ay = {};
				ioctl(fd, EVIOCGABS(mt ? ABS_MT_POSITION_X : ABS_X), &ax);
				ioctl(fd, EVIOCGABS(mt ? ABS_MT_POSITION_Y : ABS_Y), &ay);
				d.min_x = ax.minimum;
				d.max_x = ax.maximum;
				d.min_y = ay.minimum;
				d.max_y = ay.maximum;
			} else if (!pad) {
				close(fd);
				continue;
			}
			if (debug) {
				char iname[128] = {};
				ioctl(fd, EVIOCGNAME(sizeof(iname) - 1), iname);
				fprintf(stderr, "activity: input %s \"%s\" %s\n", path.c_str(), iname,
					d.touch ? "touch" : "pad");
			}
			devs.push_back(d);
		}
		closedir(dir);
	}

	std::vector<int> fds() const
	{
		std::vector<int> out;
		for (const auto &d : devs) out.push_back(d.fd);
		return out;
	}

	void read(int fd, std::function<void(const InputEvent &)> cb)
	{
		Dev *dev = nullptr;
		for (auto &d : devs)
			if (d.fd == fd) {
				dev = &d;
				break;
			}
		if (!dev) return;
		struct input_event ev;
		while (::read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
			if (debug && ev.type == EV_KEY) fprintf(stderr, "activity: key %d = %d\n", ev.code, ev.value);
			if (dev->touch) {
				if (ev.type == EV_ABS) {
					if (ev.code == ABS_MT_SLOT && ev.value >= 0 && ev.value < 10) dev->slot = ev.value;
					else if (ev.code == ABS_MT_TRACKING_ID) {
						int s = dev->slot;
						if (ev.value >= 0) dev->tid[s] = ev.value;
						else if (dev->tid[s] != -1) {
							int x = dev->tx[s], y = dev->ty[s];
							int sp = dev->max_x > dev->min_x ? dev->max_x - dev->min_x + 1 : 0;
							int sn = dev->max_y > dev->min_y ? dev->max_y - dev->min_y + 1 : 0;
							if (sp > 0) x = (x - dev->min_x) * scr_w / sp;
							if (sn > 0) y = (y - dev->min_y) * scr_h / sn;
							cb({ InputEvent::TOUCH_UP, Key::UNKNOWN, 1, x, y });
							dev->tid[s] = -1;
						}
					} else if (ev.code == ABS_MT_POSITION_X) dev->tx[dev->slot] = ev.value;
					else if (ev.code == ABS_MT_POSITION_Y) dev->ty[dev->slot] = ev.value;
					else if (ev.code == ABS_X) dev->tx[dev->slot] = ev.value;
					else if (ev.code == ABS_Y) dev->ty[dev->slot] = ev.value;
				} else if (ev.type == EV_KEY && ev.code == BTN_TOUCH && ev.value == 0) {
					int x = dev->tx[0], y = dev->ty[0];
					int sp = dev->max_x > dev->min_x ? dev->max_x - dev->min_x + 1 : 0;
					int sn = dev->max_y > dev->min_y ? dev->max_y - dev->min_y + 1 : 0;
					if (sp > 0) x = (x - dev->min_x) * scr_w / sp;
					if (sn > 0) y = (y - dev->min_y) * scr_h / sn;
					cb({ InputEvent::TOUCH_UP, Key::UNKNOWN, 1, x, y });
				}
			} else if (ev.type == EV_KEY && ev.value != 2) {
				Key k = mapKey(ev.code);
				if (k != Key::UNKNOWN)
					cb({ ev.value ? InputEvent::PRESS : InputEvent::RELEASE, k, 0, 0, 0 });
			} else if (ev.type == EV_ABS && ev.value != 0) {
				// a pad whose d-pad is a hat (the RG DS's is GPIO keys; another pad may not be)
				if (ev.code == ABS_HAT0Y)
					cb({ InputEvent::PRESS, ev.value < 0 ? Key::UP : Key::DOWN, 0, 0, 0 });
				else if (ev.code == ABS_HAT0X)
					cb({ InputEvent::PRESS, ev.value < 0 ? Key::LEFT : Key::RIGHT, 0, 0, 0 });
			}
		}
	}
};

} // namespace banana

using banana::Input;
using banana::InputEvent;
using banana::Key;
#else
enum class Key { UP, DOWN, LEFT, RIGHT, A, B, X, Y, L, R, SELECT, START, UNKNOWN };
struct InputEvent {
	enum Type { PRESS, RELEASE, TOUCH_UP } type = PRESS;
	Key key = Key::UNKNOWN;
	int screen = 1;
	int x = 0, y = 0;
};
#endif

/* ---- palette: the menu's dark palette (ui/src/settings_pal.cpp) ---- */
static const uint32_t C_INK = 0xf1ede2, C_MUTED = 0xa39d8e, C_LINE = 0x8a8374, C_EDGE = 0xe8e2d3;
static const uint32_t C_NAVY = 0xfff6dc, C_RED = 0xff3b4a;
static const uint32_t C_BG = 0x17161c, C_FRAME = 0x0b0a0d, C_BAND = 0x2e2b33;
static const uint32_t C_PANEL_BAND = 0x3a3540, C_PANEL = 0x17161c, C_PANEL_LOW = 0x0b0a0d;
static const uint32_t C_CHIP_BAND = 0x2e2b33, C_CHIP = 0x121117;
static const uint32_t C_BUBBLE_BAND = 0x38333b, C_BUBBLE = 0x191820, C_BUBBLE_LOW = 0x0f0e13;
static const uint32_t C_TRACK = 0x100f14, C_METER = 0x15141a, C_RULE = 0x45403d, C_BRAND = 0x262219;
static const uint32_t C_KBD = 0xf1ede2, C_KBD_INK = 0x17161c;
static const uint32_t C_FAV_BAND = 0x5a2430, C_FAV = 0x2a1016;

struct Accent {
	uint32_t base, light, dark, band, low;
};
static const Accent ACC_YELLOW = { 0xf0b429, 0xffe68a, 0x8a5a00, 0xffcf5c, 0xc98f12 };
static const Accent ACC_BLUE = { 0x4a9fe0, 0xb7e1ff, 0x0f3f66, 0x7cc0f2, 0x2f7fc0 };
static const Accent ACC_RED = { 0xe8574d, 0xffb8b0, 0x7a1a14, 0xf58a80, 0xc23b32 };
static const Accent ACC_GREEN = { 0x56bf6b, 0xbff0c8, 0x1d5a2b, 0x86d896, 0x3d9a52 };
static const Accent ACC_PURPLE = { 0xa07ae6, 0xddd0ff, 0x43266e, 0xbfa4f2, 0x7f58c4 };
static const Accent ACC_PINK = { 0xec6fa8, 0xffc6e0, 0x7a1f4a, 0xf59cc4, 0xc9508a };

static Accent gAccent = ACC_YELLOW;
static uint32_t gSheetFill = 0x5f4a1f;

static uint32_t mixColor(uint32_t a, double p, uint32_t b)
{
	int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
	int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
	int r = (int)std::lround(ar * p + br * (1 - p));
	int g = (int)std::lround(ag * p + bg * (1 - p));
	int bl = (int)std::lround(ab * p + bb * (1 - p));
	return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

/* ---- text: the baked Pixelify atlas ---- */
static const AFFont *pickFont(bool medium, int px)
{
	const AFFont *arr = medium ? AFS_medium : AFS_regular;
	int best = 0, bestd = 1 << 30;
	for (int i = 0; i < AF_COUNT; i++) {
		int d = arr[i].px > px ? arr[i].px - px : px - arr[i].px;
		if (d < bestd) {
			bestd = d;
			best = i;
		}
	}
	return &arr[best];
}

static const AFGlyph *findGlyph(const AFFont *f, uint32_t cp)
{
	int lo = 0, hi = f->n - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (f->g[mid].cp == cp) return &f->g[mid];
		if (f->g[mid].cp < cp) lo = mid + 1;
		else hi = mid - 1;
	}
	return nullptr;
}

static void utf8Decode(const std::string &s, std::vector<uint32_t> &out)
{
	out.clear();
	size_t i = 0;
	while (i < s.size()) {
		unsigned char c = (unsigned char)s[i++];
		uint32_t cp = '?';
		if (c < 0x80) cp = c;
		else if ((c & 0xE0) == 0xC0 && i < s.size()) {
			cp = ((c & 0x1F) << 6) | (s[i++] & 0x3F);
		} else if ((c & 0xF0) == 0xE0 && i + 1 < s.size()) {
			cp = ((c & 0x0F) << 12) | ((s[i] & 0x3F) << 6) | (s[i + 1] & 0x3F);
			i += 2;
		} else if ((c & 0xF8) == 0xF0 && i + 2 < s.size()) {
			cp = ((c & 0x07) << 18) | ((s[i] & 0x3F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
			i += 3;
		}
		out.push_back(cp);
	}
}

static uint32_t upperCp(uint32_t cp)
{
	if (cp >= 'a' && cp <= 'z') return cp - 32;
	if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 0x20;
	if (cp == 0xFF) return 0x178;
	return cp;
}

/* ---- canvas: the drawing primitive the menu is built from ---- */
struct BoxStyle {
	uint32_t fill = 0;
	int r = 0;
	uint32_t border = 0;
	int borderW = 0;
	uint32_t bandTop = 0;
	int bandTopH = 0;
	uint32_t bandBot = 0;
	int bandBotH = 0;
	uint32_t insetTop = 0;
	int insetTopH = 0;
	uint32_t insetBot = 0;
	int insetBotH = 0;
	bool dither = false;
	uint32_t shadow = 0;
	int shadowDx = 0, shadowDy = 0, shadowA = 0;
};

class Canvas {
public:
	int w = 640, h = 480;
	float s = 1.0f;
	uint32_t *px = nullptr;

	int U(int v) const { return (int)std::lround(v * s); }

	inline void put(int x, int y, uint32_t c)
	{
		if (x >= 0 && y >= 0 && x < w && y < h) px[(size_t)y * w + x] = c;
	}

	inline void blend(int x, int y, uint32_t c, int a)
	{
		if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
		uint32_t *p = &px[(size_t)y * w + x];
		if (a >= 255) {
			*p = c;
			return;
		}
		uint32_t d = *p;
		int r = (((c >> 16) & 255) * a + ((d >> 16) & 255) * (255 - a)) / 255;
		int g = (((c >> 8) & 255) * a + ((d >> 8) & 255) * (255 - a)) / 255;
		int b = ((c & 255) * a + (d & 255) * (255 - a)) / 255;
		*p = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
	}

	void clear(uint32_t c)
	{
		for (int i = 0; i < w * h; i++) px[i] = c;
	}

	void fill(int x, int y, int ww, int hh, uint32_t c, int a = 255)
	{
		int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
		int x1 = x + ww > w ? w : x + ww, y1 = y + hh > h ? h : y + hh;
		for (int yy = y0; yy < y1; yy++)
			for (int xx = x0; xx < x1; xx++) blend(xx, yy, c, a);
	}

	static int rowInset(int dy, int hh, int r)
	{
		if (r <= 0) return 0;
		if (dy >= r && dy < hh - r) return 0;
		double cy = dy < r ? (double)r - 0.5 - dy : (double)dy - (hh - r) + 0.5;
		double d = (double)r * r - cy * cy;
		if (d < 0) d = 0;
		double in = (double)r - std::sqrt(d);
		int v = (int)std::floor(in + 0.5);
		return v < 0 ? 0 : v;
	}

	void fillRound(int x, int y, int ww, int hh, int r, uint32_t c, int a = 255)
	{
		if (r > hh / 2) r = hh / 2;
		if (r > ww / 2) r = ww / 2;
		for (int dy = 0; dy < hh; dy++) {
			int in = rowInset(dy, hh, r);
			fill(x + in, y + dy, ww - 2 * in, 1, c, a);
		}
	}

	void fillRoundBand(int x, int y, int ww, int hh, int r, int from, int to, uint32_t c, int a = 255)
	{
		if (r > hh / 2) r = hh / 2;
		if (r > ww / 2) r = ww / 2;
		if (from < 0) from = 0;
		if (to > hh) to = hh;
		for (int dy = from; dy < to; dy++) {
			int in = rowInset(dy, hh, r);
			fill(x + in, y + dy, ww - 2 * in, 1, c, a);
		}
	}

	void fillRoundRing(int x, int y, int ww, int hh, int r, int b, uint32_t c, int a = 255)
	{
		if (r > hh / 2) r = hh / 2;
		if (r > ww / 2) r = ww / 2;
		for (int dy = 0; dy < hh; dy++) {
			int in = rowInset(dy, hh, r);
			if (dy < b || dy >= hh - b) {
				fill(x + in, y + dy, ww - 2 * in, 1, c, a);
			} else {
				fill(x + in, y + dy, b, 1, c, a);
				fill(x + ww - in - b, y + dy, b, 1, c, a);
			}
		}
	}

	bool insideRound(int x, int y, int rx, int ry, int rw, int rh, int r) const
	{
		if (x < rx || y < ry || x >= rx + rw || y >= ry + rh) return false;
		if (r <= 0) return true;
		int cx = x < rx + r ? rx + r : (x >= rx + rw - r ? rx + rw - r : x);
		int cy = y < ry + r ? ry + r : (y >= ry + rh - r ? ry + rh - r : y);
		int dx = x - cx, dy = y - cy;
		return dx * dx + dy * dy <= r * r;
	}

	void box(int x, int y, int ww, int hh, const BoxStyle &st)
	{
		int X = U(x), Y = U(y), W = U(ww), H = U(hh), R = U(st.r);
		if (st.shadowA > 0)
			fillRound(X + U(st.shadowDx), Y + U(st.shadowDy), W, H, R, st.shadow, st.shadowA);
		int b = U(st.borderW);
		int ix = X + b, iy = Y + b, iw = W - 2 * b, ih = H - 2 * b, ir = R - b;
		if (iw > 0 && ih > 0) {
			if (st.fill) fillRound(ix, iy, iw, ih, ir < 0 ? 0 : ir, st.fill);
			if (st.bandTopH > 0 && st.bandTop)
				fillRoundBand(ix, iy, iw, ih, ir < 0 ? 0 : ir, 0, U(st.bandTopH), st.bandTop);
			if (st.bandBotH > 0 && st.bandBot)
				fillRoundBand(ix, iy, iw, ih, ir < 0 ? 0 : ir, ih - U(st.bandBotH), ih, st.bandBot);
			if (st.dither && st.fill) {
				int step = (int)std::lround(s);
				if (step < 1) step = 1;
				for (int yy = iy; yy < iy + ih; yy++) {
					for (int xx = ix; xx < ix + iw; xx++) {
						if (!insideRound(xx, yy, ix, iy, iw, ih, ir)) continue;
						int u = ((xx - ix) / step) % 4, v = ((yy - iy) / step) % 4;
						if (u == 0 && v == 0) blend(xx, yy, 0xffffff, 36);
						else if (u == 2 && v == 2) blend(xx, yy, 0xffffff, 18);
					}
				}
			}
			if (st.insetTopH > 0 && st.insetTop)
				fillRoundBand(ix, iy, iw, ih, ir < 0 ? 0 : ir, 0, U(st.insetTopH), st.insetTop, 26);
			if (st.insetBotH > 0 && st.insetBot)
				fillRoundBand(ix, iy, iw, ih, ir < 0 ? 0 : ir, ih - U(st.insetBotH), ih, st.insetBot, 90);
		}
		if (b > 0 && st.border) fillRoundRing(X, Y, W, H, R, b, st.border);
	}

	int textWidth(const std::string &str, int px, bool medium, float ls, bool up)
	{
		std::vector<uint32_t> cps;
		utf8Decode(str, cps);
		const AFFont *f = pickFont(medium, U(px));
		int total = 0, n = 0;
		for (auto cp : cps) {
			if (up) cp = upperCp(cp);
			const AFGlyph *g = findGlyph(f, cp);
			if (!g) g = findGlyph(f, '?');
			total += g ? g->adv : U(px) / 2;
			n++;
		}
		total += U(ls) * (n > 0 ? n - 1 : 0);
		return total;
	}

	/* y is the top of the line (the ascender line). Returns the width in CSS units. */
	int text(const std::string &str, int x, int y, int px, uint32_t color, bool medium = false,
	         int align = 0, int maxw = 0, float ls = 0, bool up = false)
	{
		std::vector<uint32_t> cps;
		utf8Decode(str, cps);
		if (up) {
			for (auto &cp : cps) cp = upperCp(cp);
		}
		const AFFont *f = pickFont(medium, U(px));
		int lsDev = (int)std::lround(ls * s);
		auto widthOf = [&](const std::vector<uint32_t> &v) {
			int t = 0;
			for (size_t i = 0; i < v.size(); i++) {
				const AFGlyph *g = findGlyph(f, v[i]);
				if (!g) g = findGlyph(f, '?');
				t += g ? g->adv : U(px) / 2;
				if (i + 1 < v.size()) t += lsDev;
			}
			return t;
		};
		int lim = maxw > 0 ? U(maxw) : 0;
		if (lim > 0 && widthOf(cps) > lim) {
			while (!cps.empty() && widthOf(cps) > lim) cps.pop_back();
			while (!cps.empty() && cps.back() == ' ') cps.pop_back();
			uint32_t ell = 0x2026;
			cps.push_back(ell);
			while (cps.size() > 1 && widthOf(cps) > lim) {
				cps.erase(cps.end() - 2);
			}
		}
		int tw = widthOf(cps);
		int x0 = U(x);
		if (align == 1) x0 -= tw / 2;
		else if (align == 2) x0 -= tw;
		int y0 = U(y);
		int pen = x0;
		for (size_t i = 0; i < cps.size(); i++) {
			const AFGlyph *g = findGlyph(f, cps[i]);
			if (!g) g = findGlyph(f, '?');
			if (!g) continue;
			if (g->w > 0) {
				int stride = (g->w + 7) / 8;
				for (int row = 0; row < g->h; row++) {
					for (int col = 0; col < g->w; col++) {
						if (f->bits[g->off + row * stride + col / 8] & (0x80 >> (col % 8)))
							blend(pen + g->ox + col, y0 + g->oy + row, color, 255);
					}
				}
			}
			pen += g->adv;
			if (i + 1 < cps.size()) pen += lsDev;
		}
		return (int)std::lround((double)tw / s);
	}

	void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t c, int a = 255)
	{
		int X0 = U(x0), Y0 = U(y0), X1 = U(x1), Y1 = U(y1), X2 = U(x2), Y2 = U(y2);
		int minx = std::min(X0, std::min(X1, X2)), maxx = std::max(X0, std::max(X1, X2));
		int miny = std::min(Y0, std::min(Y1, Y2)), maxy = std::max(Y0, std::max(Y1, Y2));
		double d = (double)(Y1 - Y2) * (X0 - X2) + (double)(X2 - X1) * (Y0 - Y2);
		if (std::fabs(d) < 0.0001) return;
		for (int y = miny; y <= maxy; y++) {
			for (int x = minx; x <= maxx; x++) {
				double a0 = ((double)(Y1 - Y2) * (x - X2) + (double)(X2 - X1) * (y - Y2)) / d;
				double b0 = ((double)(Y2 - Y0) * (x - X2) + (double)(X0 - X2) * (y - Y2)) / d;
				double c0 = 1 - a0 - b0;
				if (a0 >= -0.001 && b0 >= -0.001 && c0 >= -0.001) blend(x, y, c, a);
			}
		}
	}
};

/* ---- date/time helpers ---- */
static long long nowSecs() { return (long long)time(nullptr); }

static long long nowMs()
{
	using namespace std::chrono;
	return (long long)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static time_t dayStart(time_t t)
{
	struct tm tm;
	localTime(t, &tm);
	tm.tm_hour = 0;
	tm.tm_min = 0;
	tm.tm_sec = 0;
	tm.tm_isdst = -1;
	return mktime(&tm);
}

static std::string fmtDuration(long long secs)
{
	if (secs < 0) secs = 0;
	if (secs < 60) return "<1m";
	long long m = secs / 60;
	if (m < 60) return std::to_string((long long)m) + "m";
	long long hh = m / 60, mm = m % 60;
	char buf[32];
	snprintf(buf, sizeof(buf), "%lldh %02lldm", hh, mm);
	return buf;
}

static std::string fmtClock(time_t t)
{
	struct tm tm;
	localTime(t, &tm);
	char buf[16];
	snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
	return buf;
}

static std::string fmtDateShort(time_t t)
{
	struct tm tm;
	localTime(t, &tm);
	char buf[16];
	snprintf(buf, sizeof(buf), "%02d/%02d", tm.tm_mon + 1, tm.tm_mday);
	return buf;
}

static const char *MONTHS[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
static const char *WEEKDAYS[] = { "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY" };
static const char *WEEKDAYS3[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

static std::string fmtDateLong(time_t t)
{
	struct tm tm;
	localTime(t, &tm);
	char buf[32];
	snprintf(buf, sizeof(buf), "%02d %s", tm.tm_mday, MONTHS[tm.tm_mon]);
	return buf;
}

static std::string fmtWeekday(time_t t)
{
	struct tm tm;
	localTime(t, &tm);
	return WEEKDAYS[tm.tm_wday];
}

static std::string fmtDateNumeric(time_t t)
{
	struct tm tm;
	localTime(t, &tm);
	char buf[16];
	snprintf(buf, sizeof(buf), "%02d.%02d.%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
	return buf;
}

static std::string upperStr(const std::string &s)
{
	std::string r = s;
	for (auto &c : r) c = (char)std::toupper((unsigned char)c);
	return r;
}

static std::string trimStr(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && (unsigned char)s[a] <= ' ') a++;
	while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
	return s.substr(a, b - a);
}

/* ---- the app's data: the menu's playstats (read only) and its own session history ---- */
struct Game {
	std::string sys, rel, title;
	long long plays = 0, secs = 0, last = 0;
	int achA = -1, achB = 0;
};

struct Session {
	long long start = 0, end = 0;
	std::string sys, rel;
	int secs = 0;
};

struct Snap {
	long long plays = 0, secs = 0, last = 0;
};

static std::string sysName(const std::string &id)
{
	static const struct { const char *id, *name; } NAMES[] = {
		{ "nds", "NDS" }, { "gba", "GBA" }, { "gbc", "GBC" }, { "gb", "GB" }, { "snes", "SNES" },
		{ "nes", "NES" }, { "n64", "N64" }, { "psx", "PSX" }, { "megadrive", "MD" }, { "arcade", "ARCADE" },
		{ "ports", "PC" }, { "favorites", "FAV" }, { "recently-played", "RECENT" }, { "storeapps", "APPS" },
		{ "dodakvarter", "DODA" }, { "rocknixdsstore", "STORE" }, { "rocknixdsbank", "BANK" },
	};
	for (auto &n : NAMES)
		if (id == n.id) return n.name;
	std::string u = upperStr(id);
	if (u.size() > 6) u = u.substr(0, 6);
	return u;
}

static uint32_t sysColor(const std::string &id)
{
	static const struct { const char *id; uint32_t c; } COLORS[] = {
		{ "nds", 0x3d9e9a }, { "gba", 0x8b6ad8 }, { "gbc", 0x2eae62 }, { "gb", 0x8a9144 },
		{ "snes", 0xa07ad4 }, { "nes", 0xe25a5a }, { "n64", 0x6d8a58 }, { "psx", 0x5c86e0 },
		{ "megadrive", 0x2eabc8 }, { "arcade", 0xe8a020 }, { "ports", 0xe08a4e },
		{ "favorites", 0xe23d52 }, { "recently-played", 0xe8a33d }, { "storeapps", 0x2eae62 },
		{ "dodakvarter", 0xb8323a }, { "rocknixdsstore", 0x2eae62 }, { "rocknixdsbank", 0x3d6fb8 },
		{ "app", 0xe8a33d },
	};
	for (auto &c : COLORS)
		if (id == c.id) return c.c;
	return C_MUTED;
}

static std::string titleFromRel(const std::string &rel)
{
	size_t slash = rel.find_last_of('/');
	std::string name = slash == std::string::npos ? rel : rel.substr(slash + 1);
	size_t dot = name.find_last_of('.');
	if (dot != std::string::npos && dot > 0 && name.size() - dot <= 6) name = name.substr(0, dot);
	for (auto &c : name)
		if (c == '_') c = ' ';
	// "Game (USA)" / "Game [!]" tags are noise in a list this short
	size_t par = name.find(" (");
	if (par != std::string::npos) name = name.substr(0, par);
	size_t br = name.find(" [");
	if (br != std::string::npos) name = name.substr(0, br);
	return trimStr(name);
}

#ifdef _WIN32
static bool writeFileAtomic(const std::string &path, const std::string &data)
{
	std::string tmp = path + ".tmp";
	{
		std::ofstream f(tmp.c_str(), std::ios::binary | std::ios::trunc);
		if (!f) return false;
		f.write(data.data(), (std::streamsize)data.size());
		f.flush();
		if (!f) return false;
	}
	std::remove(path.c_str());
	return std::rename(tmp.c_str(), path.c_str()) == 0;
}
#else
static bool writeFileAtomic(const std::string &path, const std::string &data)
{
	std::string tmp = path + ".tmp";
	int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return false;
	size_t off = 0;
	while (off < data.size()) {
		ssize_t n = write(fd, data.data() + off, data.size() - off);
		if (n <= 0) {
			close(fd);
			unlink(tmp.c_str());
			return false;
		}
		off += (size_t)n;
	}
	fsync(fd);
	close(fd);
	if (rename(tmp.c_str(), path.c_str()) != 0) {
		unlink(tmp.c_str());
		return false;
	}
	size_t slash = path.find_last_of('/');
	if (slash != std::string::npos) {
		int dfd = open(path.substr(0, slash).c_str(), O_RDONLY | O_DIRECTORY);
		if (dfd >= 0) {
			fsync(dfd);
			close(dfd);
		}
	}
	return true;
}
#endif

static std::vector<std::string> splitTabs(const std::string &line)
{
	std::vector<std::string> out;
	size_t a = 0;
	for (;;) {
		size_t b = line.find('\t', a);
		if (b == std::string::npos) {
			out.push_back(line.substr(a));
			break;
		}
		out.push_back(line.substr(a, b - a));
		a = b + 1;
	}
	return out;
}

struct Data {
	std::string root = "/data";
	std::string dir, statsPath;
	std::vector<Game> games;
	std::map<std::string, int> gameIndex;
	std::vector<Session> sessions;
	std::map<std::string, Snap> snapshot;
	std::vector<std::string> systems;

	bool load(std::string &why);
	void sync();
	void saveSessions();
	void saveSnapshot();
	void refreshSystems();
	void logLine(const std::string &msg);
	void addSessions(const Game &g, long long dp, long long ds);
};

void Data::logLine(const std::string &msg)
{
	if (dir.empty()) return;
	std::ofstream f((dir + "/activity.log").c_str(), std::ios::app);
	if (!f) return;
	char ts[32];
	time_t t = time(nullptr);
	struct tm tm;
	localTime(t, &tm);
	strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
	f << ts << " " << msg << "\n";
}

void Data::refreshSystems()
{
	std::set<std::string> s;
	for (auto &g : games)
		if (g.plays > 0) s.insert(g.sys);
	systems.assign(s.begin(), s.end());
}

void Data::addSessions(const Game &g, long long dp, long long ds)
{
	long long end = g.last > 0 ? g.last : nowSecs();
	int n = (int)(dp > 0 ? dp : 1);
	if (n > 60) n = 60;
	if (ds < 0) ds = 0;
	long long part = n > 0 ? ds / n : 0;
	for (int i = 0; i < n; i++) {
		Session s;
		s.end = end - (long long)(n - 1 - i) * part;
		s.start = s.end - part;
		s.sys = g.sys;
		s.rel = g.rel;
		s.secs = (int)(part > 2000000000LL ? 2000000000LL : part);
		sessions.push_back(s);
	}
	logLine("recorded " + std::to_string(n) + " session(s) for " + g.sys + "/" + g.rel + " (" +
	        std::to_string(ds) + "s)");
}

bool Data::load(std::string &why)
{
	dir = root + "/banana/activity";
	statsPath = root + "/banana/playstats.tsv";
	MAKE_DIR(dir.c_str());

	// a reload (the A button) reads everything again: start from empty, or the games and
	// sessions would be appended to the ones already in memory and the totals would double
	games.clear();
	gameIndex.clear();
	sessions.clear();
	snapshot.clear();
	systems.clear();

	/* the menu's play counts, play time and last play: the only file read outside our folder */
	std::ifstream sf(statsPath.c_str());
	if (!sf) {
		why = "cannot read " + statsPath;
		return false;
	}
	std::string line;
	int bad = 0;
	while (std::getline(sf, line)) {
		if (line.empty() || line[0] == '#') continue;
		std::vector<std::string> f = splitTabs(line);
		if (f.size() < 4) {
			bad++;
			continue;
		}
		size_t slash = f[0].find('/');
		if (slash == std::string::npos || slash == 0 || slash + 1 >= f[0].size()) {
			bad++;
			continue;
		}
		Game g;
		g.sys = f[0].substr(0, slash);
		g.rel = f[0].substr(slash + 1);
		g.plays = strtoll(f[1].c_str(), nullptr, 10);
		g.secs = strtoll(f[2].c_str(), nullptr, 10);
		g.last = strtoll(f[3].c_str(), nullptr, 10);
		if (f.size() >= 6) {
			g.achA = (int)strtol(f[4].c_str(), nullptr, 10);
			g.achB = (int)strtol(f[5].c_str(), nullptr, 10);
		}
		if (g.plays < 0) g.plays = 0;
		if (g.secs < 0) g.secs = 0;
		g.title = titleFromRel(g.rel);
		games.push_back(g);
	}
	if (bad > 0) logLine("playstats: skipped " + std::to_string(bad) + " malformed line(s)");
	std::sort(games.begin(), games.end(), [](const Game &a, const Game &b) { return a.title < b.title; });
	for (size_t i = 0; i < games.size(); i++) gameIndex[games[i].sys + "/" + games[i].rel] = (int)i;

	/* our own history: schema line first; a file with another shape is set aside, never rewritten */
	std::string sfile = dir + "/sessions.tsv";
	std::ifstream inf(sfile.c_str(), std::ios::binary);
	if (inf) {
		std::string head;
		bool ours = false;
		std::streampos start = inf.tellg();
		while (std::getline(inf, head)) {
			if (head.empty()) continue;
			ours = head.rfind("# banana-activity sessions", 0) == 0;
			break;
		}
		if (!ours) {
			inf.close();
			std::string aside = dir + "/sessions.unread-" + std::to_string((long long)nowSecs()) + ".tsv";
			if (rename(sfile.c_str(), aside.c_str()) == 0)
				logLine("sessions.tsv had another format; kept as " + aside);
			else
				logLine("sessions.tsv had another format and could not be moved");
		} else {
			inf.clear();
			inf.seekg(start);
			int good = 0;
			while (std::getline(inf, line)) {
				if (line.empty() || line[0] == '#') continue;
				std::vector<std::string> f = splitTabs(line);
				if (f.size() < 5) continue;
				Session s;
				s.start = strtoll(f[0].c_str(), nullptr, 10);
				s.end = strtoll(f[1].c_str(), nullptr, 10);
				s.sys = f[2];
				s.rel = f[3];
				s.secs = (int)strtol(f[4].c_str(), nullptr, 10);
				if (s.end < s.start || s.sys.empty()) continue;
				sessions.push_back(s);
				good++;
			}
			(void)good;
		}
	}
	std::sort(sessions.begin(), sessions.end(), [](const Session &a, const Session &b) { return a.end < b.end; });

	/* the last values seen in playstats, so the next run can tell what is new */
	std::ifstream nf((dir + "/snapshot.tsv").c_str());
	if (nf) {
		bool ours = false;
		std::string head;
		while (std::getline(nf, head)) {
			if (head.empty()) continue;
			ours = head.rfind("# banana-activity snapshot", 0) == 0;
			break;
		}
		if (ours) {
			nf.clear();
			nf.seekg(0);
			while (std::getline(nf, line)) {
				if (line.empty() || line[0] == '#') continue;
				std::vector<std::string> f = splitTabs(line);
				if (f.size() < 5) continue;
				Snap s;
				s.plays = strtoll(f[2].c_str(), nullptr, 10);
				s.secs = strtoll(f[3].c_str(), nullptr, 10);
				s.last = strtoll(f[4].c_str(), nullptr, 10);
				snapshot[f[0] + "/" + f[1]] = s;
			}
		} else {
			nf.close();
			std::string aside = dir + "/snapshot.unread-" + std::to_string((long long)nowSecs()) + ".tsv";
			if (rename((dir + "/snapshot.tsv").c_str(), aside.c_str()) == 0)
				logLine("snapshot.tsv had another format; kept as " + aside);
		}
	}
	refreshSystems();
	return true;
}

void Data::saveSessions()
{
	std::string out = "# banana-activity sessions 1\n";
	out += "# start\tend\tsystem\trel\tseconds\n";
	for (auto &s : sessions) {
		out += std::to_string(s.start) + "\t" + std::to_string(s.end) + "\t" + s.sys + "\t" + s.rel + "\t" +
		       std::to_string(s.secs) + "\n";
	}
	writeFileAtomic(dir + "/sessions.tsv", out);
}

void Data::saveSnapshot()
{
	std::string out = "# banana-activity snapshot 1\n";
	out += "# system\trel\tplays\tseconds\tlast\n";
	for (auto &kv : snapshot) {
		size_t slash = kv.first.find('/');
		if (slash == std::string::npos) continue;
		out += kv.first.substr(0, slash) + "\t" + kv.first.substr(slash + 1) + "\t" +
		       std::to_string(kv.second.plays) + "\t" + std::to_string(kv.second.secs) + "\t" +
		       std::to_string(kv.second.last) + "\n";
	}
	writeFileAtomic(dir + "/snapshot.tsv", out);
}

void Data::sync()
{
	bool seeded = false, snapChanged = false, sessChanged = false;
	for (auto &g : games) {
		std::string key = g.sys + "/" + g.rel;
		auto it = snapshot.find(key);
		if (it == snapshot.end()) {
			Snap s;
			s.plays = g.plays;
			s.secs = g.secs;
			s.last = g.last;
			snapshot[key] = s;
			seeded = true;
			snapChanged = true;
			continue;
		}
		long long dp = g.plays - it->second.plays;
		long long ds = g.secs - it->second.secs;
		if (dp < 0 || ds < 0) {
			// the menu's counter went back (a reset or a replaced stats file): take it as it is
			dp = 0;
			ds = 0;
			logLine("playstats went back for " + key + "; not recording a negative session");
		}
		if (dp > 0 || ds > 0) {
			addSessions(g, dp, ds);
			sessChanged = true;
		}
		if (it->second.plays != g.plays || it->second.secs != g.secs || it->second.last != g.last) {
			it->second.plays = g.plays;
			it->second.secs = g.secs;
			it->second.last = g.last;
			snapChanged = true;
		}
	}
	if (!sessions.empty())
		std::sort(sessions.begin(), sessions.end(), [](const Session &a, const Session &b) { return a.end < b.end; });
	// only touch the card when something actually changed: a launch that sees no new play writes nothing
	if (sessChanged) saveSessions();
	if (snapChanged) saveSnapshot();
	if (seeded) logLine("first run: seeded the snapshot, no history invented");
}

/* ---- what the views ask of the history ---- */
enum Period { P_WEEK, P_MONTH, P_ALL };

struct Range {
	long long from = 0, to = 0;
};

static Range periodRange(Period p, long long now)
{
	Range r;
	long long today = dayStart((time_t)now);
	if (p == P_WEEK) r.from = today - 6 * 86400;
	else if (p == P_MONTH) r.from = today - 29 * 86400;
	else r.from = 0;
	r.to = now + 1;
	return r;
}

struct Agg {
	long long secs = 0;
	int sessions = 0;
	long long hours[24];
	long long tod[4];
	std::map<std::string, long long> sys;
	std::map<std::string, long long> game;
	std::map<long long, long long> days;
	int longest = 0;
	long long lastEndTod = -1;
	int firstEndTod = -1;
	Agg() { memset(hours, 0, sizeof(hours)); memset(tod, 0, sizeof(tod)); }
};

static int hourOf(long long t)
{
	time_t tt = (time_t)t;
	struct tm tm;
	localTime(tt, &tm);
	return tm.tm_hour;
}

static Agg aggregate(const Data &d, const Range &r, const std::string &filter)
{
	Agg a;
	for (auto &s : d.sessions) {
		if (s.end < r.from || s.end >= r.to) continue;
		if (!filter.empty() && s.sys != filter) continue;
		a.secs += s.secs;
		a.sessions++;
		if (s.secs > a.longest) a.longest = s.secs;
		int h = hourOf(s.end);
		a.hours[h] += s.secs;
		a.tod[h / 6] += s.secs;
		a.sys[s.sys] += s.secs;
		a.game[s.sys + "/" + s.rel] += s.secs;
		a.days[dayStart((time_t)s.end)] += s.secs;
		if (a.lastEndTod < 0 || s.end > a.lastEndTod) a.lastEndTod = s.end;
		if (a.firstEndTod < 0 || s.end < a.firstEndTod) a.firstEndTod = s.end;
	}
	return a;
}

static std::string topKey(const std::map<std::string, long long> &m, long long *val)
{
	std::string best;
	long long bv = 0;
	for (auto &kv : m) {
		if (kv.second > bv) {
			bv = kv.second;
			best = kv.first;
		}
	}
	if (val) *val = bv;
	return best;
}

struct Pick {
	int index = -1;
	int score = 0;
	std::string reason;
};

static int daysSince(long long last, long long now)
{
	if (last <= 0) return 9999;
	long long d = (now - last) / 86400;
	return d < 0 ? 0 : (int)d;
}

static int streakDays(const Data &d, long long now)
{
	std::set<long long> days;
	for (auto &s : d.sessions) days.insert(dayStart((time_t)s.end));
	int streak = 0;
	long long day = dayStart((time_t)now);
	while (days.count(day)) {
		streak++;
		day -= 86400;
	}
	return streak;
}

static int dustyCount(const Data &d, long long now)
{
	int n = 0;
	for (auto &g : d.games)
		if (g.plays > 0 && g.last > 0 && now - g.last > 30LL * 86400) n++;
	return n;
}

static const char *periodName(Period p)
{
	return p == P_WEEK ? "THIS WEEK" : p == P_MONTH ? "THIS MONTH" : "ALL TIME";
}

static const char *periodShort(Period p)
{
	return p == P_WEEK ? "WEEK" : p == P_MONTH ? "MONTH" : "ALL TIME";
}

static const char *periodPlayed(Period p)
{
	return p == P_WEEK ? "LAST 7 DAYS PLAYED" : p == P_MONTH ? "LAST 30 DAYS PLAYED" : "ALL TIME PLAYED";
}

static std::string insightText(const Data &d, const Agg &a, Period p, long long now)
{
	if (a.sessions == 0)
		return std::string("NO SESSIONS IN ") + periodName(p) + " YET";
	long long topSys = 0, topGame = 0;
	std::string ts = topKey(a.sys, &topSys);
	std::string tg = topKey(a.game, &topGame);
	int sessions = a.sessions;
	long long avg = sessions > 0 ? a.secs / sessions : 0;
	int nightPct = a.secs > 0 ? (int)(a.tod[0] * 100 / a.secs) : 0;
	int sysPct = a.secs > 0 ? (int)(topSys * 100 / a.secs) : 0;
	int gamePct = a.secs > 0 ? (int)(topGame * 100 / a.secs) : 0;
	int streak = streakDays(d, now);
	int dusty = dustyCount(d, now);

	char buf[160];
	if (sysPct >= 45 && !ts.empty()) {
		snprintf(buf, sizeof(buf), "%s IS %d%% OF YOUR PLAY TIME", sysName(ts).c_str(), sysPct);
		return buf;
	}
	if (nightPct >= 35) {
		snprintf(buf, sizeof(buf), "YOU PLAY MOST AT NIGHT (%d%%)", nightPct);
		return buf;
	}
	if (gamePct >= 25 && !tg.empty()) {
		auto it = d.gameIndex.find(tg);
		std::string name = it == d.gameIndex.end() ? tg : d.games[(size_t)it->second].title;
		snprintf(buf, sizeof(buf), "%s TAKES %d%% OF YOUR TIME", upperStr(name).c_str(), gamePct);
		return buf;
	}
	if (streak >= 3) {
		snprintf(buf, sizeof(buf), "%d-DAY PLAY STREAK \xC2\xB7 %d SESSIONS", streak, sessions);
		return buf;
	}
	if (avg >= 2700) {
		snprintf(buf, sizeof(buf), "YOUR SESSIONS RUN LONG: %s EACH", fmtDuration(avg).c_str());
		return buf;
	}
	if (dusty >= 3) {
		snprintf(buf, sizeof(buf), "%d GAMES UNTOUCHED FOR A MONTH", dusty);
		return buf;
	}
	snprintf(buf, sizeof(buf), "%d SESSIONS \xC2\xB7 %s IN %s", sessions, fmtDuration(a.secs).c_str(), periodName(p));
	return buf;
}

/* ======================= the app: state, layout, drawing ======================= */
enum View { V_LIBRARY = 0, V_TIMELINE = 1, V_TRENDS = 2 };

enum Act {
	ACT_NONE = 0, ACT_PERIOD_PREV, ACT_PERIOD_NEXT, ACT_DAY_PREV, ACT_DAY_NEXT,
	ACT_SYSTEM, ACT_SORT, ACT_VIEW, ACT_EXIT, ACT_ROW, ACT_PICK, ACT_REFRESH
};

struct Zone {
	int x, y, w, h, act, arg;
};

struct Power {
	int pct = -1;
	bool charging = false;
};

static std::string relTime(long long last, long long now)
{
	if (last <= 0) return "never";
	long long d = (now - last) / 86400;
	if (d <= 0) return "today";
	if (d == 1) return "yesterday";
	if (d < 30) return std::to_string((long long)d) + "d ago";
	if (d < 365) return std::to_string((long long)(d / 30)) + "mo ago";
	return std::to_string((long long)(d / 365)) + "y ago";
}

static int gameScore(const Game &g, long long now)
{
	if (g.plays <= 0 || g.secs < 60 || g.last <= 0) return -100000;
	int days = daysSince(g.last, now);
	if (days < 2) return -100000;
	int investment = (int)std::min<long long>(g.secs / 600, 60);
	int score = (int)std::min(days, 120) + investment + (g.plays <= 3 ? 40 : g.plays <= 8 ? 15 : 0);
	if (days > 180) score -= 30;
	return score;
}

static Pick pickAmong(const Data &d, const std::vector<int> &list, long long now)
{
	Pick best;
	for (size_t k = 0; k < list.size(); k++) {
		int i = list[k];
		if (i < 0 || i >= (int)d.games.size()) continue;
		const Game &g = d.games[(size_t)i];
		int sc = gameScore(g, now);
		if (sc <= best.score) continue;
		best.index = i;
		best.score = sc;
		int days = daysSince(g.last, now);
		if (days >= 60) {
			int months = days / 30;
			best.reason = months < 2 ? std::to_string(days) + " days untouched"
						 : std::to_string(months) + " months untouched";
		} else if (g.plays <= 2) {
			best.reason = "You stopped after " + std::to_string(g.plays) + " play" + (g.plays == 1 ? "" : "s");
		} else if (g.secs < 3600) {
			best.reason = "Only " + std::to_string(g.secs / 60) + " min in";
		} else {
			best.reason = "Not played in " + std::to_string(days) + " days";
		}
	}
	return best;
}

static Power readPower()
{
	Power p;
#ifdef _WIN32
	return p;
#else
	DIR *d = opendir("/sys/class/power_supply");
	if (!d) return p;
	struct dirent *e;
	while ((e = readdir(d)) != nullptr) {
		if (e->d_name[0] == '.') continue;
		std::string base = std::string("/sys/class/power_supply/") + e->d_name;
		int cap = -1;
		{
			std::ifstream f((base + "/capacity").c_str());
			if (f) f >> cap;
		}
		if (cap < 0) continue;
		p.pct = cap;
		std::ifstream st((base + "/status").c_str());
		if (st) {
			std::string s;
			st >> s;
			if (s == "Charging" || s == "Full") p.charging = true;
		}
		break;
	}
	closedir(d);
	return p;
#endif
}

static bool readOnline()
{
#ifdef _WIN32
	return true;
#else
	for (const char *n : { "wlan0", "eth0" }) {
		std::ifstream f((std::string("/sys/class/net/") + n + "/operstate").c_str());
		std::string s;
		if (f >> s) return s == "up";
	}
	return true;
#endif
}

static BoxStyle sheetStyle()
{
	BoxStyle b;
	b.fill = gSheetFill;
	b.r = 18;
	b.border = C_EDGE;
	b.borderW = 3;
	b.dither = true;
	b.insetTop = 0xffffff;
	b.insetTopH = 6;
	b.insetBot = 0x000000;
	b.insetBotH = 8;
	b.shadow = 0x000000;
	b.shadowDx = 4;
	b.shadowDy = 4;
	b.shadowA = 115;
	return b;
}

static BoxStyle cardStyle(bool sel)
{
	BoxStyle b;
	b.r = 12;
	b.fill = sel ? mixColor(gAccent.base, 0.16, C_PANEL) : C_PANEL;
	b.bandTop = C_PANEL_BAND;
	b.bandTopH = 6;
	b.border = sel ? C_EDGE : C_LINE;
	b.borderW = 3;
	if (!sel) {
		b.insetBot = C_PANEL_LOW;
		b.insetBotH = 4;
	}
	b.shadow = 0x000000;
	b.shadowDx = 3;
	b.shadowDy = 3;
	b.shadowA = 217;
	return b;
}

static BoxStyle chipStyle(bool on)
{
	BoxStyle b;
	b.r = 8;
	b.fill = on ? gAccent.base : C_CHIP;
	b.border = on ? gAccent.light : mixColor(C_LINE, 0.9, C_BG);
	b.borderW = 2;
	if (!on) {
		b.bandTop = C_CHIP_BAND;
		b.bandTopH = 6;
	}
	b.shadow = 0x000000;
	b.shadowDx = 2;
	b.shadowDy = 2;
	b.shadowA = 204;
	return b;
}

static BoxStyle bubbleStyle()
{
	BoxStyle b;
	b.r = 12;
	b.fill = C_BUBBLE;
	b.border = C_LINE;
	b.borderW = 3;
	b.bandTop = C_BUBBLE_BAND;
	b.bandTopH = 10;
	b.insetBot = C_BUBBLE_LOW;
	b.insetBotH = 6;
	b.shadow = 0x000000;
	b.shadowDx = 4;
	b.shadowDy = 4;
	b.shadowA = 229;
	return b;
}

static BoxStyle faceStyle(int kind)
{
	BoxStyle b;
	b.r = 10;
	b.borderW = 3;
	b.border = kind == 1 ? C_RED : C_EDGE;
	b.fill = kind == 1 ? C_FAV : C_PANEL;
	b.bandTop = kind == 1 ? C_FAV_BAND : C_BUBBLE_BAND;
	b.bandTopH = 8;
	b.insetBot = C_PANEL_LOW;
	b.insetBotH = 4;
	b.shadow = 0x000000;
	b.shadowDx = 3;
	b.shadowDy = 3;
	b.shadowA = 229;
	return b;
}

static BoxStyle keyStyle()
{
	BoxStyle b;
	b.r = 4;
	b.fill = C_KBD;
	return b;
}

class App {
public:
	Data data;
	View view;
	Period period;
	int dayOffset;
	int cursor;
	std::string filter;
	int sortMode;
	bool quit;
	std::vector<Zone> zones;
	Power power;
	bool online = true;
	long long now;

	std::vector<int> lib;
	std::vector<int> dayList;
	std::vector<std::pair<std::string, long long> > sysRows;
	Agg agg, prevAgg, weekAgg, aggAll;
	long long statsTotal = 0;
	Pick pick;
	Key heldDir = Key::UNKNOWN;
	long long repeatAt = 0;

	App()
		: view(V_LIBRARY), period(P_WEEK), dayOffset(0), cursor(0), sortMode(0), quit(false), now(0)
	{
	}

	void reload();
	void rebuild();
	void clampCursor();
	void moveCursor(int d);
	void cycleFilter(int d);
	void doAction(int act, int arg);
	void onKey(Key k);
	void onKeyUp(Key k);
	bool tick();
	long long repeatWait();
	void onTouch(int x, int y);

	void render(Canvas &top, Canvas &bot);

private:
	void drawStatus(Canvas &c);
	void drawWifi(Canvas &c, int x, int y, uint32_t col);
	void drawLogo(Canvas &c, int x, int y);
	void drawSheet(Canvas &c);
	void drawChips(Canvas &c, int right, int y);
	void drawBubble(Canvas &c, const std::string &title, const std::string &sub);
	void drawShoulders(Canvas &c, const std::string &left, const std::string &right);
	void drawPeriodArrows(Canvas &c, int x, int y, bool right);
	int drawKeyChip(Canvas &c, int x, int y, const std::string &key, int px);
	int drawFace(Canvas &c, int x, int y, const std::string &key, const std::string &label, int w = 0,
		     int act = ACT_NONE);
	void drawFooter(Canvas &c, const std::string &lk, const std::string &ll, int leftAct,
			const std::vector<std::pair<std::string, std::string> > &mids, const std::string &rk,
			const std::string &rl, int rightAct);
	void drawRail(Canvas &c, int top, int bottom, int index, int count);
	void drawStat(Canvas &c, int x, int y, const std::string &val, const std::string &lab, int vsize = 26);
	void drawLibRow(Canvas &c, int x, int y, int w, int h, bool sel, int rank, const Game &g, double share);
	void drawSessionRow(Canvas &c, int x, int y, int w, int h, bool sel, const Session &s);
	void drawSysRow(Canvas &c, int x, int y, int w, int h, bool sel, const std::string &sys, long long secs,
			double share);
	void drawNextCard(Canvas &c, int x, int y, int w, int h);

	void zone(int x, int y, int w, int h, int act, int arg = 0);
	void drawLibraryTop(Canvas &c);
	void drawLibraryBottom(Canvas &c);
	void drawTimelineTop(Canvas &c);
	void drawTimelineBottom(Canvas &c);
	void drawTrendsTop(Canvas &c);
	void drawTrendsBottom(Canvas &c);
};

void App::zone(int x, int y, int w, int h, int act, int arg)
{
	Zone z;
	z.x = (int)std::lround(x * 1.0);
	z.y = (int)std::lround(y * 1.0);
	z.w = w;
	z.h = h;
	z.act = act;
	z.arg = arg;
	zones.push_back(z);
}

void App::rebuild()
{
	now = nowSecs();
	power = readPower();
	online = readOnline();
	zones.clear();

	Range r = periodRange(period, now);
	agg = aggregate(data, r, filter);
	if (period != P_ALL) {
		long long len = r.to - r.from;
		Range pr;
		pr.from = r.from - len;
		pr.to = r.from;
		prevAgg = aggregate(data, pr, filter);
	} else {
		prevAgg = Agg();
	}
	aggAll = aggregate(data, r, "");
	statsTotal = 0;
	for (auto &g : data.games)
		if (g.plays > 0) statsTotal += g.secs;
	long long base = dayStart((time_t)now);
	Range wr;
	wr.from = base - 6 * 86400;
	wr.to = now + 1;
	weekAgg = aggregate(data, wr, filter);

	lib.clear();
	for (size_t i = 0; i < data.games.size(); i++) {
		const Game &g = data.games[i];
		if (g.plays <= 0) continue;
		if (!filter.empty() && g.sys != filter) continue;
		lib.push_back((int)i);
	}
	if (sortMode == 0) {
		std::stable_sort(lib.begin(), lib.end(), [this](int a, int b) {
			const Game &ga = data.games[(size_t)a], &gb = data.games[(size_t)b];
			if (ga.secs != gb.secs) return ga.secs > gb.secs;
			return ga.title < gb.title;
		});
	} else if (sortMode == 1) {
		std::stable_sort(lib.begin(), lib.end(), [this](int a, int b) {
			const Game &ga = data.games[(size_t)a], &gb = data.games[(size_t)b];
			if (ga.plays != gb.plays) return ga.plays > gb.plays;
			return ga.title < gb.title;
		});
	} else if (sortMode == 2) {
		std::stable_sort(lib.begin(), lib.end(), [this](int a, int b) {
			const Game &ga = data.games[(size_t)a], &gb = data.games[(size_t)b];
			if (ga.last != gb.last) return ga.last > gb.last;
			return ga.title < gb.title;
		});
	} else {
		std::stable_sort(lib.begin(), lib.end(), [this](int a, int b) {
			int sa = gameScore(data.games[(size_t)a], now), sb = gameScore(data.games[(size_t)b], now);
			if (sa != sb) return sa > sb;
			return data.games[(size_t)a].title < data.games[(size_t)b].title;
		});
	}
	pick = pickAmong(data, lib, now);

	long long day = dayStart((time_t)now) + (long long)dayOffset * 86400;
	dayList.clear();
	for (size_t i = 0; i < data.sessions.size(); i++) {
		const Session &s = data.sessions[i];
		if (s.end < day || s.end >= day + 86400) continue;
		if (!filter.empty() && s.sys != filter) continue;
		dayList.push_back((int)i);
	}
	std::sort(dayList.begin(), dayList.end(), [this](int a, int b) {
		return data.sessions[(size_t)a].end < data.sessions[(size_t)b].end;
	});

	sysRows.clear();
	{
		for (auto &kv : aggAll.sys) sysRows.push_back(std::make_pair(kv.first, kv.second));
		std::sort(sysRows.begin(), sysRows.end(),
			  [](const std::pair<std::string, long long> &a, const std::pair<std::string, long long> &b) {
				  if (a.second != b.second) return a.second > b.second;
				  return a.first < b.first;
			  });
	}
	clampCursor();
}

void App::clampCursor()
{
	int n = view == V_LIBRARY ? (int)lib.size() : view == V_TIMELINE ? (int)dayList.size() : (int)sysRows.size();
	if (n <= 0) cursor = 0;
	else if (cursor >= n) cursor = n - 1;
	else if (cursor < 0) cursor = 0;
}

void App::reload()
{
	std::string why;
	if (!data.load(why)) {
		fprintf(stderr, "banana-activity: %s\n", why.c_str());
		data.logLine("cannot load playstats: " + why);
	}
	data.sync();
	rebuild();
}

void App::moveCursor(int d)
{
	int n = view == V_LIBRARY ? (int)lib.size() : view == V_TIMELINE ? (int)dayList.size() : (int)sysRows.size();
	if (n <= 0) return;
	cursor += d;
	if (cursor < 0) cursor = 0;
	if (cursor >= n) cursor = n - 1;
}

void App::cycleFilter(int d)
{
	std::vector<std::string> opts;
	opts.push_back("");
	for (auto &s : data.systems) opts.push_back(s);
	if (opts.empty()) return;
	int at = 0;
	for (size_t i = 0; i < opts.size(); i++)
		if (opts[i] == filter) at = (int)i;
	at = (at + d + (int)opts.size()) % (int)opts.size();
	filter = opts[(size_t)at];
	cursor = 0;
	rebuild();
}

void App::doAction(int act, int arg)
{
	switch (act) {
	case ACT_PERIOD_PREV:
		period = period == P_WEEK ? P_ALL : period == P_MONTH ? P_WEEK : P_MONTH;
		cursor = 0;
		rebuild();
		break;
	case ACT_PERIOD_NEXT:
		period = period == P_WEEK ? P_MONTH : period == P_MONTH ? P_ALL : P_WEEK;
		cursor = 0;
		rebuild();
		break;
	case ACT_DAY_PREV:
		if (dayOffset > -365) dayOffset--;
		cursor = 0;
		rebuild();
		break;
	case ACT_DAY_NEXT:
		if (dayOffset < 0) dayOffset++;
		cursor = 0;
		rebuild();
		break;
	case ACT_SYSTEM:
		cycleFilter(1);
		break;
	case ACT_SORT:
		sortMode = (sortMode + 1) % (view == V_TRENDS ? 2 : 4);
		cursor = 0;
		rebuild();
		break;
	case ACT_VIEW:
		view = (View)(((int)view + 1) % 3);
		cursor = 0;
		rebuild();
		break;
	case ACT_EXIT:
		quit = true;
		break;
	case ACT_REFRESH:
		reload();
		break;
	case ACT_ROW:
		cursor = arg;
		break;
	case ACT_PICK:
		if (pick.index >= 0) {
			for (size_t i = 0; i < lib.size(); i++)
				if (lib[i] == pick.index) {
					cursor = (int)i;
					break;
				}
		}
		break;
	default:
		break;
	}
}

void App::onKey(Key k)
{
	if (k == Key::UP || k == Key::DOWN) {
		heldDir = k;
		repeatAt = nowMs() + 380;
	} else {
		heldDir = Key::UNKNOWN;
	}
	switch (k) {
	case Key::START:
		doAction(ACT_EXIT, 0);
		break;
	case Key::SELECT:
		doAction(ACT_VIEW, 0);
		break;
	case Key::L:
		doAction(ACT_PERIOD_PREV, 0);
		break;
	case Key::R:
		doAction(ACT_PERIOD_NEXT, 0);
		break;
	case Key::X:
		doAction(ACT_SYSTEM, 0);
		break;
	case Key::Y:
		doAction(ACT_SORT, 0);
		break;
	case Key::A:
		doAction(ACT_REFRESH, 0);
		break;
	case Key::UP:
		moveCursor(-1);
		break;
	case Key::DOWN:
		moveCursor(1);
		break;
	case Key::LEFT:
		if (view == V_TIMELINE) doAction(ACT_DAY_PREV, 0);
		else doAction(ACT_PERIOD_PREV, 0);
		break;
	case Key::RIGHT:
		if (view == V_TIMELINE) doAction(ACT_DAY_NEXT, 0);
		else doAction(ACT_PERIOD_NEXT, 0);
		break;
	case Key::B:
		// back to the library from either other view
		if (view != V_LIBRARY) {
			view = V_LIBRARY;
			cursor = 0;
			rebuild();
		}
		break;
	default:
		break;
	}
}

void App::onKeyUp(Key k)
{
	if (k == heldDir) heldDir = Key::UNKNOWN;
}

/* the kernel's joypad does not auto-repeat: a held d-pad scrolls the list here */
bool App::tick()
{
	if (heldDir == Key::UNKNOWN) return false;
	long long t = nowMs();
	if (t < repeatAt) return false;
	if (heldDir == Key::UP) moveCursor(-1);
	else if (heldDir == Key::DOWN) moveCursor(1);
	repeatAt = t + 110;
	return true;
}

long long App::repeatWait()
{
	if (heldDir == Key::UNKNOWN) return 1000;
	long long w = repeatAt - nowMs();
	return w < 0 ? 0 : w;
}

void App::onTouch(int x, int y)
{
	for (size_t i = 0; i < zones.size(); i++) {
		const Zone &z = zones[i];
		if (x >= z.x && y >= z.y && x < z.x + z.w && y < z.y + z.h) {
			doAction(z.act, z.arg);
			return;
		}
	}
}

/* ---- pieces ---- */
void App::drawWifi(Canvas &c, int x, int y, uint32_t col)
{
	int cx = c.U(x + 10), cy = c.U(y + 12);
	for (int yy = c.U(y); yy <= cy; yy++) {
		for (int xx = c.U(x); xx <= c.U(x + 21); xx++) {
			double dx = (xx - cx) / c.s, dy = (cy - yy) / c.s;
			double dd = std::sqrt(dx * dx + dy * dy);
			double ang = std::fabs(std::atan2(dx, dy));
			if (dy < 0.5 || ang > 1.05) continue;
			if (std::fabs(dd - 9.5) < 0.6 || std::fabs(dd - 6.5) < 0.6 || std::fabs(dd - 3.2) < 0.6)
				c.blend(xx, yy, col, 255);
		}
	}
	c.fill(c.U(x + 8), c.U(y + 11), c.U(4), c.U(2), col);
}

void App::drawLogo(Canvas &c, int x, int y)
{
	// the menu's own banana (ui/assets/rnds/logo@1x.png, cropped): 1x and the Plus's 1.6x
	const uint32_t *px;
	int w, h;
	if (c.s > 1.3f) {
		px = AF_LOGO_16_PX;
		w = AF_LOGO_16_W;
		h = AF_LOGO_16_H;
	} else {
		px = AF_LOGO_1X_PX;
		w = AF_LOGO_1X_W;
		h = AF_LOGO_1X_H;
	}
	int X = c.U(x), Y = c.U(y);
	for (int yy = 0; yy < h; yy++) {
		for (int xx = 0; xx < w; xx++) {
			uint32_t v = px[yy * w + xx];
			int a = (int)((v >> 24) & 255);
			if (a) c.blend(X + xx, Y + yy, v & 0xffffff, a);
		}
	}
}

void App::drawStatus(Canvas &c)
{
	std::string clock = fmtClock((time_t)now), date = fmtDateShort((time_t)now);
	drawWifi(c, 16, 23, online ? C_INK : mixColor(C_INK, 0.30, C_BG));
	if (power.pct >= 0) c.text(std::to_string(power.pct) + "%", 44, 20, 19, C_INK);
	else c.text("--%", 44, 20, 19, C_INK);
	BoxStyle bt;
	bt.r = 3;
	bt.border = C_INK;
	bt.borderW = 2;
	c.box(78, 25, 26, 14, bt);
	c.fill(c.U(104), c.U(29), c.U(3), c.U(6), C_INK);
	if (power.pct >= 0)
		c.fill(c.U(81), c.U(28), c.U(20.0 * power.pct / 100.0), c.U(8),
		       power.charging ? gAccent.base : C_INK);
	BoxStyle plate;
	plate.r = 4;
	plate.fill = C_BRAND;
	plate.shadow = 0x000000;
	plate.shadowDx = 2;
	plate.shadowDy = 2;
	plate.shadowA = 229;
	c.box(124, 19, 132, 26, plate);
	drawLogo(c, 132, 22);
	c.text("ACTIVITY", 157, 24, 16, 0xfffaf0, true, 0, 0, 1.4f);
	int cw = c.text(clock, 622, 20, 20, C_INK, true, 2);
	c.text(date, 622 - cw - 8, 20, 19, C_INK, false, 2);
}

void App::drawSheet(Canvas &c) { c.box(18, 60, 604, 406, sheetStyle()); }

void App::drawChips(Canvas &c, int right, int y)
{
	const char *names[3] = { "WEEK", "MONTH", "ALL TIME" };
	Period ps[3] = { P_WEEK, P_MONTH, P_ALL };
	int x = right;
	for (int i = 2; i >= 0; i--) {
		int w = (int)std::lround(c.textWidth(names[i], 12, true, 0.8f, false) / c.s) + 18;
		x -= w;
		bool on = period == ps[i];
		c.box(x, y, w, 22, chipStyle(on));
		c.text(names[i], x + w / 2, y + 4, 12, on ? C_KBD_INK : C_MUTED, true, 1, 0, 0.8f);
		x -= 6;
	}
}

void App::drawBubble(Canvas &c, const std::string &title, const std::string &sub)
{
	c.box(66, 58, 508, 84, bubbleStyle());
	c.text(title, 320, 69, 26, C_INK, true, 1, 0, 0.52f);
	c.text(sub, 320, 105, 14, C_MUTED, false, 1, 0, 1.3f, true);
	c.tri(304, 138, 336, 138, 320, 154, C_LINE);
	c.tri(308, 141, 332, 141, 320, 152, C_BUBBLE);
}

void App::drawPeriodArrows(Canvas &c, int x, int y, bool right)
{
	BoxStyle b = chipStyle(false);
	c.box(x, y, 26, 24, b);
	if (right) c.tri(x + 10, y + 6, x + 10, y + 18, x + 18, y + 12, C_INK);
	else c.tri(x + 16, y + 6, x + 16, y + 18, x + 8, y + 12, C_INK);
}

int App::drawKeyChip(Canvas &c, int x, int y, const std::string &key, int px)
{
	int w = 12 + (int)std::lround(c.textWidth(key, px, true, 0, false) / c.s);
	c.box(x, y, w, 23, keyStyle());
	c.text(key, x + 6, y + 3, px, C_KBD_INK, true);
	return w;
}

int App::drawFace(Canvas &c, int x, int y, const std::string &key, const std::string &label, int w, int act)
{
	int keyW = key.empty() ? 0 : 12 + (int)std::lround(c.textWidth(key, 16, true, 0, false) / c.s);
	if (w <= 0)
		w = 15 + keyW + (key.empty() ? 0 : 8) +
		    (int)std::lround(c.textWidth(label, 19, true, 0, false) / c.s) + 15;
	BoxStyle b = faceStyle(0);
	c.box(x, y, w, 40, b);
	int lx = x + 15;
	if (!key.empty()) {
		int kw = drawKeyChip(c, lx, y + 8, key, 16);
		lx += kw + 8;
	}
	c.text(label, lx, y + 9, 19, C_INK, true, 0, 0, 0);
	if (act) zone(x, y, w, 40, act);
	return w;
}

void App::drawFooter(Canvas &c, const std::string &lk, const std::string &ll, int leftAct,
		     const std::vector<std::pair<std::string, std::string> > &mids, const std::string &rk,
		     const std::string &rl, int rightAct)
{
	int lw = 15 + 12 + (int)std::lround(c.textWidth(lk, 16, true, 0, false) / c.s) + 8 +
		 (int)std::lround(c.textWidth(ll, 19, true, 0, false) / c.s) + 15;
	drawFace(c, 18, 422, lk, ll, lw, leftAct);
	int rw = 15 + 12 + (int)std::lround(c.textWidth(rk, 16, true, 0, false) / c.s) + 8 +
		 (int)std::lround(c.textWidth(rl, 19, true, 0, false) / c.s) + 15;
	drawFace(c, 622 - rw, 422, rk, rl, rw, rightAct);

	std::vector<int> widths;
	int total = -10;
	for (size_t i = 0; i < mids.size(); i++) {
		int mw = 15 + 12 + (int)std::lround(c.textWidth(mids[i].first, 16, true, 0, false) / c.s) + 8 +
			 (int)std::lround(c.textWidth(mids[i].second, 19, true, 0, false) / c.s) + 15;
		widths.push_back(mw);
		total += mw + 10;
	}
	int x = 18 + lw + (604 - lw - rw - total) / 2;
	for (size_t i = 0; i < mids.size(); i++) {
		int act = mids[i].first == "SELECT" ? ACT_VIEW : mids[i].first == "START" ? ACT_EXIT : ACT_NONE;
		drawFace(c, x, 422, mids[i].first, mids[i].second, widths[i], act);
		x += widths[i] + 10;
	}
}

void App::drawShoulders(Canvas &c, const std::string &left, const std::string &right)
{
	int kl = drawKeyChip(c, 18, 21, "L", 16);
	c.text(left, 18 + kl + 8, 21, 19, C_INK, false, 0, 0, 1.2f, true);
	int lw = (int)std::lround(c.textWidth(left, 19, false, 1.2f, true) / c.s);
	zone(10, 14, 18 + kl + 8 + lw + 8, 38, ACT_PERIOD_PREV);

	c.fill(c.U(284), c.U(31), c.U(72), c.U(3), C_RULE);

	int kr = 12 + (int)std::lround(c.textWidth("R", 16, true, 0, false) / c.s);
	c.box(622 - kr, 21, kr, 23, keyStyle());
	c.text("R", 622 - kr + 6, 24, 16, C_KBD_INK, true);
	int rw = (int)std::lround(c.textWidth(right, 19, false, 1.2f, true) / c.s);
	c.text(right, 622 - kr - 8, 21, 19, C_INK, false, 2, 0, 1.2f, true);
	zone(622 - kr - 8 - rw - 10, 14, kr + rw + 26, 38, ACT_PERIOD_NEXT);
}

void App::drawRail(Canvas &c, int top, int bottom, int index, int count)
{
	c.fill(c.U(620), c.U(top), c.U(5), c.U(bottom - top), C_METER);
	int h = bottom - top;
	int th = 36;
	if (count > 1) {
		int span = h - th;
		int y = top + (int)std::lround((double)span * index / (count - 1));
		BoxStyle b;
		b.r = 2;
		b.fill = gAccent.base;
		b.border = C_EDGE;
		b.borderW = 2;
		b.bandTop = gAccent.light;
		b.bandTopH = 4;
		c.box(619, y, 7, th, b);
	} else {
		c.fill(c.U(620), c.U(top), c.U(5), c.U(h), C_METER);
	}
	char a[16], bb[16];
	snprintf(a, sizeof(a), "%02d", count == 0 ? 0 : index + 1);
	snprintf(bb, sizeof(bb), "/ %02d", count);
	int aw = c.text(a, 584, 392, 15, C_INK, true, 2);
	c.text(bb, 588, 394, 13, C_MUTED, false, 0);
	(void)aw;
}

void App::drawStat(Canvas &c, int x, int y, const std::string &val, const std::string &lab, int vsize)
{
	c.text(val, x, y, vsize, C_INK, true);
	c.text(lab, x, y + vsize + 4, 12, C_MUTED, false, 0, 0, 1.4f, true);
}

void App::drawLibRow(Canvas &c, int x, int y, int w, int h, bool sel, int rank, const Game &g, double share)
{
	c.box(x, y, w, h, cardStyle(sel));
	c.fill(c.U(x), c.U(y + 3), c.U(4), c.U(h - 6), sel ? gAccent.base : sysColor(g.sys));
	char num[8];
	snprintf(num, sizeof(num), "%02d", rank);
	c.text(num, 38, y + 11, 13, sel ? gAccent.base : C_MUTED, true, 0, 0, 0.8f);

	bool dusty = g.last > 0 && now - g.last > 30LL * 86400;
	std::string sub = std::string(sysName(g.sys)) + " \xC2\xB7 " + std::to_string(g.plays) + " PLAY" +
			  (g.plays == 1 ? "" : "S");
	if (g.achA >= 0 && g.achB > 0)
		sub += " \xC2\xB7 " + std::to_string(g.achA) + "/" + std::to_string(g.achB) + " ACH";
	sub += " \xC2\xB7 " + relTime(g.last, now);
	if (dusty) sub = "DUSTY \xC2\xB7 " + sub;
	c.text(g.title, 64, y + 3, 19, C_INK, true, 0, 260);
	c.text(sub, 64, y + 22, 11, dusty && !sel ? gAccent.base : C_MUTED, false, 0, 0, 1.1f, true);
	c.text(fmtDuration(g.secs), 596, y + 8, 18, sel ? C_NAVY : C_INK, true, 2);
	c.fill(c.U(470), c.U(y + 27), c.U(126), c.U(5), C_METER);
	if (share > 0.01)
		c.fill(c.U(470), c.U(y + 27), c.U(126 * share), c.U(5), sel ? gAccent.light : gAccent.base);
}

void App::drawSessionRow(Canvas &c, int x, int y, int w, int h, bool sel, const Session &s)
{
	c.box(x, y, w, h, cardStyle(sel));
	c.fill(c.U(x), c.U(y + 3), c.U(4), c.U(h - 6), sel ? gAccent.base : sysColor(s.sys));
	c.text(fmtClock((time_t)s.end), 64, y + 10, 18, C_INK, true, 0, 0, 0.6f);
	std::string title = s.rel;
	auto it = data.gameIndex.find(s.sys + "/" + s.rel);
	if (it != data.gameIndex.end()) title = data.games[(size_t)it->second].title;
	c.text(title, 142, y + 10, 18, C_INK, true, 0, 240);
	int tw = (int)std::lround(c.textWidth(sysName(s.sys), 10, true, 1.0f, false) / c.s) + 12;
	int tx = 412;
	BoxStyle tag;
	tag.r = 4;
	tag.fill = mixColor(sysColor(s.sys), 0.20, C_PANEL);
	tag.border = mixColor(sysColor(s.sys), 0.8, C_PANEL);
	tag.borderW = 1;
	c.box(tx, y + 11, tw, 17, tag);
	c.text(sysName(s.sys), tx + 6, y + 14, 10, C_INK, true, 0, 0, 1.0f);
	c.text(fmtDuration(s.secs), 596, y + 10, 18, sel ? C_NAVY : C_INK, true, 2);
}

void App::drawSysRow(Canvas &c, int x, int y, int w, int h, bool sel, const std::string &sys, long long secs,
		     double share)
{
	c.box(x, y, w, h, cardStyle(sel));
	c.fill(c.U(x), c.U(y + 3), c.U(4), c.U(h - 6), sel ? gAccent.base : sysColor(sys));
	c.text(sysName(sys), 42, y + 8, 19, C_INK, true);
	c.text(fmtDuration(secs), 596, y + 8, 18, sel ? C_NAVY : C_INK, true, 2);
	char pct[16];
	snprintf(pct, sizeof(pct), "%d%%", (int)std::lround(share * 100));
	c.text(pct, 520, y + 9, 12, C_MUTED, false, 2, 0, 1.0f);
	c.fill(c.U(300), c.U(y + 20), c.U(180), c.U(6), C_METER);
	if (share > 0.01) c.fill(c.U(300), c.U(y + 20), c.U(180 * share), c.U(6), sel ? gAccent.light : sysColor(sys));
}

void App::drawNextCard(Canvas &c, int x, int y, int w, int h)
{
	BoxStyle b = cardStyle(false);
	b.fill = mixColor(gAccent.base, 0.14, C_PANEL);
	c.box(x, y, w, h, b);
	c.fill(c.U(x), c.U(y + 3), c.U(4), c.U(h - 6), gAccent.base);
	c.text("NEXT UP", x + 18, y + 4, 11, gAccent.base, false, 0, 0, 1.4f, true);
	if (pick.index >= 0 && pick.index < (int)data.games.size()) {
		const Game &g = data.games[(size_t)pick.index];
		c.text(g.title, x + 18, y + 16, 18, C_INK, true, 0, w - 200);
		c.text(pick.reason, x + w - 16, y + 5, 11, C_MUTED, false, 2, 0, 1.1f, true);
	} else {
		c.text("Play a few games and this fills in", x + 18, y + 18, 15, C_MUTED, false, 0, w - 40);
	}
	zone(x, y, w, h, ACT_PICK);
}

// VIEWS
static std::string fmtTotal(long long secs)
{
	if (secs <= 0) return "0m";
	return fmtDuration(secs);
}

static const char *sortLabel(int mode, bool trends)
{
	static const char *N[4] = { "Time", "Plays", "Last", "Next" };
	static const char *T[2] = { "Time", "Name" };
	if (trends) return T[mode % 2];
	return N[mode % 4];
}

static void drawBase(Canvas &c)
{
	c.clear(C_BG);
	int step = (int)std::lround(c.s);
	if (step < 1) step = 1;
	for (int y = 0; y < c.h; y++) {
		for (int x = 0; x < c.w; x++) {
			int u = (x / step) % 4, v = (y / step) % 4;
			if (u == 0 && v == 0) c.blend(x, y, 0xffffff, 36);
			else if (u == 2 && v == 2) c.blend(x, y, 0xffffff, 18);
		}
	}
	c.fillRoundRing(0, 0, c.w, c.h, c.U(2), c.U(4), C_FRAME);
	c.fill(c.U(4), c.U(4), c.w - c.U(8), c.U(8), C_BAND, 128);
	c.fill(c.U(4), c.U(12), c.w - c.U(8), 1, 0xffffff, 20);
}

void App::drawLibraryTop(Canvas &c)
{
	drawStatus(c);
	drawSheet(c);
	c.text(periodPlayed(period), 42, 92, 13, C_MUTED, false, 0, 0, 1.4f, true);
	drawChips(c, 598, 84);

	bool noHist = data.sessions.empty();
	c.text(period == P_ALL && noHist ? fmtTotal(statsTotal) : fmtTotal(agg.secs), 40, 110, 44, C_NAVY, true, 0, 0,
	       0.5f);

	if (period == P_ALL) {
		if (noHist) {
			c.text("FROM THE MENU \xC2\xB7 HISTORY STARTS NOW", 44, 166, 12, C_MUTED, false, 0, 0, 1.2f, true);
		} else {
			c.text("SINCE " + fmtDateNumeric((time_t)data.sessions.front().end), 44, 166, 12, C_MUTED, false,
			       0, 0, 1.2f, true);
		}
	} else {
		long long diff = agg.secs - prevAgg.secs;
		if (diff == 0) {
			c.text("SAME AS PREVIOUS " + std::string(period == P_WEEK ? "7 DAYS" : "30 DAYS"), 44, 166, 12,
			       C_MUTED, false, 0, 0, 1.2f, true);
		} else {
			uint32_t col = diff > 0 ? gAccent.base : C_RED;
			if (diff > 0) c.tri(44, 172, 52, 172, 48, 166, col);
			else c.tri(44, 166, 52, 166, 48, 172, col);
			std::string d = fmtDuration(diff > 0 ? diff : -diff);
			int w = c.text((diff > 0 ? "+" : "-") + d, 58, 164, 14, col, true, 0, 0, 0.6f);
			c.text(period == P_WEEK ? "VS PREVIOUS 7 DAYS" : "VS PREVIOUS 30 DAYS", 58 + w + 10, 165, 12,
			       C_MUTED, false, 0, 0, 1.2f, true);
		}
	}

	long long topSys = 0;
	std::string ts = topKey(agg.sys, &topSys);
	if (period == P_ALL && noHist) {
		std::map<std::string, long long> fromStats;
		for (auto &g : data.games)
			if (g.plays > 0) fromStats[g.sys] += g.secs;
		ts = topKey(fromStats, &topSys);
	}
	drawStat(c, 330, 118, std::to_string(agg.sessions), "SESSIONS");
	drawStat(c, 468, 118, agg.sessions > 0 ? fmtTotal(agg.secs / agg.sessions) : std::string("0m"), "AVG SESSION");
	drawStat(c, 330, 176, std::to_string(agg.days.size()), "ACTIVE DAYS");
	drawStat(c, 468, 176, ts.empty() ? std::string("-") : sysName(ts), "TOP SYSTEM");
	c.fill(c.U(316), c.U(120), c.U(2), c.U(92), C_LINE, 60);
	c.fill(c.U(42), c.U(236), c.U(556), c.U(2), C_LINE, 70);

	c.text("LAST 7 DAYS", 42, 248, 13, C_MUTED, false, 0, 0, 1.4f, true);
	long long base = dayStart((time_t)now);
	long long vals[7];
	long long mx = 0;
	for (int i = 0; i < 7; i++) {
		long long day = base - (long long)(6 - i) * 86400;
		auto it = weekAgg.days.find(day);
		vals[i] = it == weekAgg.days.end() ? 0 : it->second;
		if (vals[i] > mx) mx = vals[i];
	}
	if (mx > 0) c.text("PEAK " + fmtTotal(mx), 598, 248, 12, C_MUTED, false, 2, 0, 1.2f, true);
	else c.text("NO SESSIONS THIS WEEK", 598, 248, 12, C_MUTED, false, 2, 0, 1.2f, true);

	int bw = 40, gap = 30;
	int x0 = 42 + (556 - (7 * bw + 6 * gap)) / 2;
	int top = 282, bot = 378;
	for (int i = 0; i < 7; i++) {
		int x = x0 + i * (bw + gap);
		BoxStyle track;
		track.r = 8;
		track.fill = C_TRACK;
		track.border = mixColor(C_LINE, 0.5, C_BG);
		track.borderW = 2;
		c.box(x, top, bw, bot - top, track);
		if (vals[i] > 0 && mx > 0) {
			int fh = 6 + (int)((bot - top - 16) * (double)vals[i] / (double)mx);
			bool today = i == 6;
			BoxStyle fill;
			fill.r = 6;
			fill.fill = today ? gAccent.light : gAccent.base;
			fill.bandTop = gAccent.light;
			fill.bandTopH = 4;
			c.box(x + 3, bot - fh, bw - 6, fh, fill);
		}
		long long day = base - (long long)(6 - i) * 86400;
		struct tm tm;
		time_t dt = (time_t)day;
		localTime(dt, &tm);
		bool today = i == 6;
		c.text(WEEKDAYS3[tm.tm_wday], x + bw / 2, bot + 10, 12, today ? C_INK : C_MUTED, today, 1, 0, 1.2f,
		       true);
		if (today) c.fill(c.U(x + bw / 2 - 5), c.U(bot + 28), c.U(10), c.U(3), gAccent.base);
	}

	c.text(insightText(data, agg, period, now), 42, 418, 12, C_MUTED, false, 0, 556, 1.2f, true);
}

void App::drawTimelineTop(Canvas &c)
{
	drawStatus(c);
	drawSheet(c);
	c.text("DAILY TIMELINE", 42, 92, 13, C_MUTED, false, 0, 0, 1.4f, true);

	long long day = dayStart((time_t)now) + (long long)dayOffset * 86400;
	Range r;
	r.from = day;
	r.to = day + 86400;
	Agg d = aggregate(data, r, filter);

	if (dayOffset == 0) {
		BoxStyle b = chipStyle(true);
		c.box(532, 84, 66, 22, b);
		c.text("TODAY", 565, 89, 12, C_KBD_INK, true, 1, 0, 0.8f);
	} else if (dayOffset == -1) {
		BoxStyle b = chipStyle(false);
		c.box(514, 84, 84, 22, b);
		c.text("YESTERDAY", 556, 89, 12, C_MUTED, true, 1, 0, 0.8f);
	}

	c.text(fmtWeekday((time_t)day), 42, 116, 13, gAccent.base, false, 0, 0, 1.6f, true);
	int dw = c.text(fmtDateLong((time_t)day), 40, 132, 40, C_NAVY, true, 0, 0, 0.5f);
	char yr[8];
	struct tm tm;
	time_t dt = (time_t)day;
	localTime(dt, &tm);
	snprintf(yr, sizeof(yr), "%d", tm.tm_year + 1900);
	c.text(yr, 40 + dw + 12, 152, 15, C_MUTED, true, 0, 0, 1.4f, true);

	char pv[24], nx[24];
	struct tm ptm, ntm;
	time_t ptt = (time_t)(day - 86400), ntt = (time_t)(day + 86400);
	localTime(ptt, &ptm);
	localTime(ntt, &ntm);
	snprintf(pv, sizeof(pv), "%s %02d", WEEKDAYS3[ptm.tm_wday], ptm.tm_mday);
	snprintf(nx, sizeof(nx), "%s %02d", WEEKDAYS3[ntm.tm_wday], ntm.tm_mday);
	c.text(std::string(pv) + " \xC2\xB7 " + nx, 42, 186, 12, C_MUTED, false, 0, 0, 1.2f, true);

	drawStat(c, 330, 118, std::to_string(d.sessions), "SESSIONS");
	drawStat(c, 468, 118, fmtTotal(d.secs), "TIME");
	drawStat(c, 330, 176, d.sessions > 0 ? fmtTotal(d.secs / d.sessions) : std::string("0m"), "AVG");
	std::string last = d.lastEndTod > 0 ? fmtClock((time_t)d.lastEndTod) : std::string("-");
	drawStat(c, 468, 176, last, "LAST");
	c.fill(c.U(316), c.U(120), c.U(2), c.U(92), C_LINE, 60);
	c.fill(c.U(42), c.U(236), c.U(556), c.U(2), C_LINE, 70);

	c.text("ACTIVITY BY HOUR", 42, 248, 13, C_MUTED, false, 0, 0, 1.4f, true);
	long long hmax = 0;
	int hpeak = -1;
	for (int h = 0; h < 24; h++)
		if (d.hours[h] > hmax) {
			hmax = d.hours[h];
			hpeak = h;
		}
	if (hpeak >= 0) {
		char pk[32];
		snprintf(pk, sizeof(pk), "PEAK %02d:00", hpeak);
		c.text(pk, 598, 248, 12, C_MUTED, false, 2, 0, 1.2f, true);
	}

	int bw = 12, gap = 8;
	int x0 = 42 + (556 - (24 * bw + 23 * gap)) / 2;
	int top = 276, bot = 362;
	for (int h = 0; h < 24; h++) {
		int x = x0 + h * (bw + gap);
		if (d.hours[h] <= 0) continue;
		int fh = 6 + (int)((bot - top) * (double)d.hours[h] / (double)(hmax > 0 ? hmax : 1));
		BoxStyle fill;
		fill.r = 3;
		fill.fill = mixColor(gAccent.low, 0.35 + 0.65 * (double)d.hours[h] / (double)(hmax > 0 ? hmax : 1),
				     gAccent.base);
		fill.bandTop = gAccent.light;
		fill.bandTopH = 3;
		c.box(x, bot - fh, bw, fh, fill);
	}
	c.fill(c.U(x0 - 6), c.U(bot), c.U(24 * bw + 23 * gap + 12), c.U(2), C_LINE, 110);
	for (int h = 0; h < 24; h++) {
		if (h != 0 && h != 6 && h != 12 && h != 18 && h != 23) continue;
		char lab[8];
		snprintf(lab, sizeof(lab), "%02d", h);
		int x = x0 + h * (bw + gap) + bw / 2;
		c.text(lab, x, bot + 6, 11, C_MUTED, false, 1, 0, 1.0f);
	}
	if (d.sessions == 0) c.text("NO SESSIONS RECORDED IN THIS DATE WINDOW", 42, 300, 12, C_MUTED, false, 0, 0, 1.2f, true);

	// time of day: one bar, four parts of the day
	c.text("TIME OF DAY", 42, 388, 12, C_MUTED, false, 0, 0, 1.4f, true);
	uint32_t tcol[4] = { mixColor(gAccent.dark, 0.75, C_BG), gAccent.low, gAccent.base, gAccent.light };
	const char *tn[4] = { "NIGHT 0-6", "MORN 6-12", "AFT 12-18", "EVE 18-24" };
	int bx = 42, bwd = 556, by = 406, bh = 18;
	BoxStyle track;
	track.r = 6;
	track.fill = C_TRACK;
	track.border = mixColor(C_LINE, 0.6, C_BG);
	track.borderW = 2;
	c.box(bx, by, bwd, bh, track);
	int cx = bx + 2;
	for (int i = 0; i < 4; i++) {
		double frac = d.secs > 0 ? (double)d.tod[i] / (double)d.secs : 0;
		int seg = (int)std::lround((bwd - 4) * frac);
		if (seg > 0) {
			BoxStyle sb;
			sb.r = 4;
			sb.fill = tcol[i];
			c.box(cx, by + 2, seg, bh - 4, sb);
		}
		cx += seg;
	}
	for (int i = 0; i < 4; i++) {
		int lx = bx + i * 139;
		BoxStyle sw;
		sw.r = 2;
		sw.fill = tcol[i];
		c.box(lx, 428, 8, 8, sw);
		char leg[48];
		int pct = d.secs > 0 ? (int)std::lround(100.0 * d.tod[i] / d.secs) : 0;
		snprintf(leg, sizeof(leg), "%s \xC2\xB7 %d%%", tn[i], pct);
		c.text(leg, lx + 13, 427, 11, C_MUTED, false, 0, 0, 1.0f, true);
	}
}

void App::drawTrendsTop(Canvas &c)
{
	drawStatus(c);
	drawSheet(c);
	c.text("TRENDS", 42, 92, 13, C_MUTED, false, 0, 0, 1.4f, true);
	drawChips(c, 598, 84);

	c.text(period == P_ALL && data.sessions.empty() ? fmtTotal(statsTotal) : fmtTotal(aggAll.secs), 40, 110, 40,
	       C_NAVY, true, 0, 0, 0.5f);
	if (data.sessions.empty()) {
		c.text("FROM THE MENU \xC2\xB7 HISTORY STARTS NOW", 42, 158, 12, C_MUTED, false, 0, 0, 1.2f, true);
	} else {
		c.text("SINCE " + fmtDateNumeric((time_t)data.sessions.front().end), 42, 158, 12, C_MUTED, false, 0, 0,
		       1.2f, true);
	}
	drawStat(c, 330, 118, std::to_string(aggAll.days.size()), "DAYS PLAYED");
	drawStat(c, 468, 118, std::to_string(aggAll.game.size()), "GAMES");
	drawStat(c, 330, 176, std::to_string(aggAll.sys.size()), "SYSTEMS");
	drawStat(c, 468, 176, fmtTotal(aggAll.longest), "LONGEST");
	c.fill(c.U(316), c.U(120), c.U(2), c.U(92), C_LINE, 60);
	c.fill(c.U(42), c.U(228), c.U(556), c.U(2), C_LINE, 70);

	c.text("LAST 18 WEEKS", 42, 240, 13, C_MUTED, false, 0, 0, 1.4f, true);
	c.text("EACH SQUARE = A DAY", 598, 240, 11, C_MUTED, false, 2, 0, 1.2f, true);

	std::map<long long, long long> hm;
	for (auto &s : data.sessions) hm[dayStart((time_t)s.end)] += s.secs;
	time_t today = dayStart((time_t)now);
	struct tm tm;
	localTime(today, &tm);
	int mondayOffset = (tm.tm_wday + 6) % 7;
	long long monday = (long long)today - (long long)mondayOffset * 86400;
	long long first = monday - 17LL * 7 * 86400;
	uint32_t levels[5];
	levels[0] = 0x241f18;
	levels[1] = mixColor(gAccent.base, 0.35, C_BG);
	levels[2] = mixColor(gAccent.base, 0.55, C_BG);
	levels[3] = gAccent.base;
	levels[4] = gAccent.light;
	int gx = 178, gy = 258, cell = 12, step = 4;
	const char *wd[7] = { "M", "T", "W", "T", "F", "S", "S" };
	for (int d = 0; d < 7; d++) c.text(wd[d], gx - 14, gy + d * (cell + step) + 1, 9, C_MUTED, false, 0, 0, 0);
	for (int w = 0; w < 18; w++) {
		for (int d = 0; d < 7; d++) {
			long long day = first + (long long)(w * 7 + d) * 86400;
			long long secs = 0;
			auto it = hm.find(day);
			if (it != hm.end()) secs = it->second;
			long long mins = secs / 60;
			int lv = 0;
			if (mins > 0) lv = mins < 10 ? 1 : mins < 30 ? 2 : mins < 60 ? 3 : 4;
			BoxStyle b;
			b.r = 3;
			b.fill = levels[lv];
			c.box(gx + w * (cell + step), gy + d * (cell + step), cell, cell, b);
		}
	}

	// by system: one bar, then its legend
	c.text("BY SYSTEM", 42, 384, 12, C_MUTED, false, 0, 0, 1.4f, true);
	struct Seg {
		std::string name;
		long long secs;
		uint32_t col;
	};
	std::vector<Seg> segs;
	for (size_t i = 0; i < sysRows.size() && i < 4; i++) {
		Seg s;
		s.name = sysName(sysRows[i].first);
		s.secs = sysRows[i].second;
		s.col = sysColor(sysRows[i].first);
		segs.push_back(s);
	}
	if (sysRows.size() > 4) {
		long long other = 0;
		for (size_t i = 4; i < sysRows.size(); i++) other += sysRows[i].second;
		Seg s;
		s.name = "OTHER";
		s.secs = other;
		s.col = C_MUTED;
		segs.push_back(s);
	}
	BoxStyle st;
	st.r = 6;
	st.fill = C_TRACK;
	st.border = mixColor(C_LINE, 0.6, C_BG);
	st.borderW = 2;
	c.box(42, 400, 556, 18, st);
	int sx = 44;
	long long tot = aggAll.secs;
	for (size_t i = 0; i < segs.size(); i++) {
		int seg = tot > 0 ? (int)std::lround((552.0) * (double)segs[i].secs / (double)tot) : 0;
		if (seg > 0) {
			BoxStyle sb;
			sb.r = 4;
			sb.fill = segs[i].col;
			c.box(sx, 402, seg, 14, sb);
		}
		sx += seg;
	}
	if (tot == 0) c.text("NO PLAY TIME YET", 320, 403, 11, C_MUTED, false, 1, 0, 1.2f, true);
	for (size_t i = 0; i < segs.size(); i++) {
		int lx = 42 + (int)i * 110;
		BoxStyle sw;
		sw.r = 2;
		sw.fill = segs[i].col;
		c.box(lx, 426, 8, 8, sw);
		char leg[32];
		int pct = tot > 0 ? (int)std::lround(100.0 * (double)segs[i].secs / (double)tot) : 0;
		snprintf(leg, sizeof(leg), "%s %d%%", segs[i].name.c_str(), pct);
		c.text(leg, lx + 13, 425, 11, C_MUTED, false, 0, 0, 1.0f, true);
	}
}

void App::drawLibraryBottom(Canvas &c)
{
	Period next = period == P_WEEK ? P_MONTH : period == P_MONTH ? P_ALL : P_WEEK;
	Period prev = period == P_WEEK ? P_ALL : period == P_MONTH ? P_WEEK : P_MONTH;
	drawShoulders(c, periodShort(prev), periodShort(next));

	if (!lib.empty()) {
		const Game &g = data.games[(size_t)lib[(size_t)cursor]];
		std::string sub = std::string(sysName(g.sys)) + " \xC2\xB7 " + std::to_string(g.plays) + " PLAYS";
		if (g.achA >= 0 && g.achB > 0)
			sub += " \xC2\xB7 " + std::to_string(g.achA) + "/" + std::to_string(g.achB) + " ACH";
		sub += " \xC2\xB7 " + relTime(g.last, now);
		drawBubble(c, g.title, sub);
	} else {
		drawBubble(c, "NO GAMES", "Nothing to show for this filter");
	}

	drawNextCard(c, 24, 158, 588, 44);

	int n = (int)lib.size();
	int vis = 4;
	int first = cursor - 1;
	if (first < 0) first = 0;
	if (first > n - vis) first = n - vis;
	if (first < 0) first = 0;
	long long mx = 0;
	for (size_t i = 0; i < lib.size(); i++)
		if (data.games[(size_t)lib[i]].secs > mx) mx = data.games[(size_t)lib[i]].secs;
	for (int k = 0; k < vis; k++) {
		int idx = first + k;
		if (idx >= n) break;
		int gi = lib[(size_t)idx];
		const Game &g = data.games[(size_t)gi];
		int y = 206 + k * 46;
		drawLibRow(c, 24, y, 588, 40, idx == cursor, idx + 1, g, mx > 0 ? (double)g.secs / (double)mx : 0);
		zone(24, y, 588, 40, ACT_ROW, idx);
	}

	drawRail(c, 206, 384, cursor, n);
	drawFooter(c, "X", filter.empty() ? std::string("System") : sysName(filter), ACT_SYSTEM,
		   { std::make_pair(std::string("SELECT"), std::string("Timeline")),
		     std::make_pair(std::string("START"), std::string("Exit")) },
		   "Y", sortLabel(sortMode, false), ACT_SORT);
}

void App::drawTimelineBottom(Canvas &c)
{
	Period next = period == P_WEEK ? P_MONTH : period == P_MONTH ? P_ALL : P_WEEK;
	Period prev = period == P_WEEK ? P_ALL : period == P_MONTH ? P_WEEK : P_MONTH;
	drawShoulders(c, periodShort(prev), periodShort(next));

	long long day = dayStart((time_t)now) + (long long)dayOffset * 86400;
	Range r;
	r.from = day;
	r.to = day + 86400;
	Agg d = aggregate(data, r, filter);
	Range pr;
	pr.from = day - 86400;
	pr.to = day;
	Agg pv = aggregate(data, pr, filter);

	std::string sub = std::to_string(d.sessions) + " SESSION" + (d.sessions == 1 ? "" : "S") + " \xC2\xB7 " +
			  fmtTotal(d.secs);
	if (pv.sessions > 0 && d.sessions > 0) {
		long long diff = d.secs - pv.secs;
		if (diff != 0) {
			std::string dd = fmtDuration(diff >= 0 ? diff : -diff);
			sub += std::string(" \xC2\xB7 ") + (diff >= 0 ? "+" : "-") + dd + " VS " +
			       fmtWeekday((time_t)(day - 86400)).substr(0, 3);
		}
	}
	drawBubble(c, fmtWeekday((time_t)day) + " " + fmtDateLong((time_t)day), sub);

	// the day's arrows live here: the touch panel
	drawPeriodArrows(c, 28, 86, false);
	zone(26, 84, 30, 28, ACT_DAY_PREV);
	drawPeriodArrows(c, 586, 86, true);
	zone(584, 84, 30, 28, ACT_DAY_NEXT);

	int n = (int)dayList.size();
	int vis = 5;
	int first = cursor - 2;
	if (first < 0) first = 0;
	if (first > n - vis) first = n - vis;
	if (first < 0) first = 0;
	for (int k = 0; k < vis; k++) {
		int idx = first + k;
		if (idx >= n) break;
		int y = 160 + k * 45;
		drawSessionRow(c, 24, y, 588, 40, idx == cursor, data.sessions[(size_t)dayList[(size_t)idx]]);
		zone(24, y, 588, 40, ACT_ROW, idx);
	}

	drawRail(c, 160, 380, cursor, n);
	drawFooter(c, "X", filter.empty() ? std::string("System") : sysName(filter), ACT_SYSTEM,
		   { std::make_pair(std::string("SELECT"), std::string("Trends")),
		     std::make_pair(std::string("START"), std::string("Exit")) },
		   "A", "Refresh", ACT_REFRESH);
}

void App::drawTrendsBottom(Canvas &c)
{
	Period next = period == P_WEEK ? P_MONTH : period == P_MONTH ? P_ALL : P_WEEK;
	Period prev = period == P_WEEK ? P_ALL : period == P_MONTH ? P_WEEK : P_MONTH;
	drawShoulders(c, periodShort(prev), periodShort(next));

	drawBubble(c, "BY SYSTEM",
		   std::to_string(sysRows.size()) + " SYSTEMS \xC2\xB7 " + fmtTotal(aggAll.secs));

	int n = (int)sysRows.size();
	int vis = 5;
	int first = cursor - 2;
	if (first < 0) first = 0;
	if (first > n - vis) first = n - vis;
	if (first < 0) first = 0;
	long long tot = aggAll.secs;
	for (int k = 0; k < vis; k++) {
		int idx = first + k;
		if (idx >= n) break;
		int y = 160 + k * 45;
		drawSysRow(c, 24, y, 588, 40, idx == cursor, sysRows[(size_t)idx].first, sysRows[(size_t)idx].second,
			   tot > 0 ? (double)sysRows[(size_t)idx].second / (double)tot : 0);
		zone(24, y, 588, 40, ACT_ROW, idx);
	}

	drawRail(c, 160, 380, cursor, n);
	drawFooter(c, "Y", sortLabel(sortMode, true), ACT_SORT,
		   { std::make_pair(std::string("SELECT"), std::string("Library")),
		     std::make_pair(std::string("START"), std::string("Exit")) },
		   "A", "Refresh", ACT_REFRESH);
}

void App::render(Canvas &top, Canvas &bot)
{
	zones.clear();
	drawBase(top);
	drawBase(bot);
	if (view == V_LIBRARY) {
		drawLibraryTop(top);
		drawLibraryBottom(bot);
	} else if (view == V_TIMELINE) {
		drawTimelineTop(top);
		drawTimelineBottom(bot);
	} else {
		drawTrendsTop(top);
		drawTrendsBottom(bot);
	}
}




/* ======================= main: arguments, smoke shots, the loop ======================= */
#ifndef _WIN32
static volatile sig_atomic_t gQuit = 0;

static void onSignal(int)
{
	gQuit = 1;
}
#endif

struct Args {
	std::string root = "/data";
	std::string smoke;
	std::string view = "library";
	std::string period = "week";
	std::string connector = "DSI-2";
	std::string accent = "";
	std::string size = "640x480";
	std::string press;
	int day = 0;
	bool inputDebug = false;
};

static void usage()
{
	printf("banana-activity [--root /data] [--smoke PREFIX] [--view library|timeline|trends]\n");
	printf("                [--period week|month|all] [--day N] [--size 640x480] [--connector DSI-2]\n");
	printf("                [--accent yellow|blue|red|green|purple|pink] [--input-debug]\n");
}

static bool setAccentName(const std::string &v)
{
	if (v == "yellow") gAccent = ACC_YELLOW;
	else if (v == "blue") gAccent = ACC_BLUE;
	else if (v == "red") gAccent = ACC_RED;
	else if (v == "green") gAccent = ACC_GREEN;
	else if (v == "purple") gAccent = ACC_PURPLE;
	else if (v == "pink") gAccent = ACC_PINK;
	else return false;
	return true;
}

static void loadAccent(const std::string &root, const std::string &forced)
{
	std::ifstream f((root + "/banana/settings.conf").c_str());
	std::string line, tz;
	while (std::getline(f, line)) {
		size_t hash = line.find('#');
		if (hash != std::string::npos) line = line.substr(0, hash);
		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string key = trimStr(line.substr(0, eq));
		std::string v = trimStr(line.substr(eq + 1));
		if (key == "accent") setAccentName(v);
		else if (key == "timezone") tz = v;
	}
	if (!forced.empty()) setAccentName(forced);
#ifndef _WIN32
	if (!tz.empty()) {
		setenv("TZ", tz.c_str(), 1);
		tzset();
	}
#endif
}

static View parseView(const std::string &v)
{
	if (v == "timeline") return V_TIMELINE;
	if (v == "trends") return V_TRENDS;
	return V_LIBRARY;
}

static Period parsePeriod(const std::string &v)
{
	if (v == "month") return P_MONTH;
	if (v == "all") return P_ALL;
	return P_WEEK;
}

static bool parseSize(const std::string &s, int &w, int &h)
{
	size_t x = s.find('x');
	if (x == std::string::npos) return false;
	w = atoi(s.substr(0, x).c_str());
	h = atoi(s.substr(x + 1).c_str());
	return w >= 320 && h >= 240 && w <= 2048 && h <= 1536;
}

static bool writePPM(const std::string &path, const Canvas &c)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (!f) return false;
	fprintf(f, "P6\n%d %d\n255\n", c.w, c.h);
	for (int i = 0; i < c.w * c.h; i++) {
		uint32_t v = c.px[i];
		unsigned char rgb[3] = { (unsigned char)((v >> 16) & 255), (unsigned char)((v >> 8) & 255),
					 (unsigned char)(v & 255) };
		fwrite(rgb, 1, 3, f);
	}
	fclose(f);
	return true;
}

int main(int argc, char **argv)
{
	Args a;
	for (int i = 1; i < argc; i++) {
		std::string k = argv[i];
		if (k == "--help" || k == "-h") {
			usage();
			return 0;
		} else if (k == "--root" && i + 1 < argc) a.root = argv[++i];
		else if (k == "--smoke" && i + 1 < argc) a.smoke = argv[++i];
		else if (k == "--view" && i + 1 < argc) a.view = argv[++i];
		else if (k == "--period" && i + 1 < argc) a.period = argv[++i];
		else if (k == "--day" && i + 1 < argc) a.day = atoi(argv[++i]);
		else if (k == "--size" && i + 1 < argc) a.size = argv[++i];
		else if (k == "--connector" && i + 1 < argc) a.connector = argv[++i];
		else if (k == "--accent" && i + 1 < argc) a.accent = argv[++i];
		else if (k == "--press" && i + 1 < argc) a.press = argv[++i];
		else if (k == "--input-debug") a.inputDebug = true;
	}

	loadAccent(a.root, a.accent);
	gSheetFill = mixColor(gAccent.base, 0.34, 0x15141a);

	App app;
	app.data.root = a.root;
	app.view = parseView(a.view);
	app.period = parsePeriod(a.period);
	app.dayOffset = a.day;
	app.reload();

	if (!a.smoke.empty()) {
		int w = 640, h = 480;
		parseSize(a.size, w, h);
		std::vector<uint32_t> tb((size_t)w * h), bb((size_t)w * h);
		Canvas top, bot;
		top.px = tb.data();
		top.w = w;
		top.h = h;
		top.s = (float)w / 640.0f;
		bot.px = bb.data();
		bot.w = w;
		bot.h = h;
		bot.s = top.s;
		app.now = nowSecs();
		app.power = readPower();
		app.rebuild();
		// --press A,B,down: the headless build can exercise the buttons (tests, previews of a view)
		if (!a.press.empty()) {
			size_t pos = 0;
			while (pos < a.press.size()) {
				size_t comma = a.press.find(',', pos);
				std::string name = a.press.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
				Key k = Key::UNKNOWN;
				if (name == "a") k = Key::A;
				else if (name == "b") k = Key::B;
				else if (name == "x") k = Key::X;
				else if (name == "y") k = Key::Y;
				else if (name == "l") k = Key::L;
				else if (name == "r") k = Key::R;
				else if (name == "up") k = Key::UP;
				else if (name == "down") k = Key::DOWN;
				else if (name == "left") k = Key::LEFT;
				else if (name == "right") k = Key::RIGHT;
				else if (name == "select") k = Key::SELECT;
				else if (name == "start") k = Key::START;
				if (k != Key::UNKNOWN) app.onKey(k);
				if (comma == std::string::npos) break;
				pos = comma + 1;
			}
		}
		if (a.inputDebug)
			fprintf(stderr, "activity: %d games, %d sessions, %lld seconds\n", (int)app.data.games.size(),
				(int)app.data.sessions.size(), app.statsTotal);
		app.render(top, bot);
		std::string p1 = a.smoke + "-top.ppm", p2 = a.smoke + "-bottom.ppm";
		bool ok = writePPM(p1, top) && writePPM(p2, bot);
		if (!ok) {
			fprintf(stderr, "banana-activity: cannot write %s\n", a.smoke.c_str());
			return 1;
		}
		printf("Smoke screenshots written to %s and %s\n", p1.c_str(), p2.c_str());
		return 0;
	}

#ifdef _WIN32
	fprintf(stderr, "banana-activity: this host build renders with --smoke PREFIX\n");
	return 2;
#else
	signal(SIGINT, onSignal);
	signal(SIGTERM, onSignal);
	signal(SIGPIPE, SIG_IGN);

	banana::Display disp;
	std::string err;
	if (!disp.open(a.connector, err)) {
		fprintf(stderr, "banana-activity: %s\n", err.c_str());
		return 1;
	}
	const int W = disp.width(), H = disp.height();
	std::vector<uint32_t> tb((size_t)W * H), bb((size_t)W * H);
	Canvas top, bot;
	top.px = tb.data();
	top.w = W;
	top.h = H;
	top.s = (float)W / 640.0f;
	bot.px = bb.data();
	bot.w = W;
	bot.h = H;
	bot.s = top.s;

	banana::Input in;
	in.debug = a.inputDebug;
	in.open(W, H);

	app.rebuild();
	bool dirty = true;
	int lastMinute = -1;
	while (!gQuit && !app.quit) {
		long long t = nowSecs();
		time_t tt = (time_t)t;
		struct tm tm;
		localTime(tt, &tm);
		int minute = tm.tm_hour * 60 + tm.tm_min;
		if (minute != lastMinute) {
			lastMinute = minute;
			app.rebuild();
			dirty = true;
		}
		if (app.tick()) dirty = true;
		if (dirty) {
			app.render(top, bot);
			const uint32_t *screens[2] = { top.px, bot.px };
			disp.present(screens);
			dirty = false;
		}
		std::vector<int> fds = in.fds();
		std::vector<struct pollfd> pfds;
		for (size_t i = 0; i < fds.size(); i++) {
			struct pollfd p;
			p.fd = fds[i];
			p.events = POLLIN;
			p.revents = 0;
			pfds.push_back(p);
		}
		long long wait = app.repeatWait();
		int timeout = wait > 800 ? 800 : (wait < 0 ? 0 : (int)wait);
		int rc = poll(pfds.data(), pfds.size(), timeout);
		if (rc < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (rc == 0) {
			if (app.tick()) dirty = true;
			continue;
		}
		float sc = (float)W / 640.0f;
		for (size_t i = 0; i < pfds.size() && i < fds.size(); i++) {
			if (!(pfds[i].revents & POLLIN)) continue;
			in.read(fds[i], [&](const InputEvent &ev) {
				if (ev.type == InputEvent::TOUCH_UP) {
					app.onTouch((int)std::lround(ev.x / sc), (int)std::lround(ev.y / sc));
				} else if (ev.type == InputEvent::RELEASE) {
					app.onKeyUp(ev.key);
				} else {
					app.onKey(ev.key);
				}
				dirty = true;
			});
		}
	}
	return 0;
#endif
}
