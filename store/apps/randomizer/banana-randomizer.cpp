/*
 * banana-randomizer.cpp - one random game from the card, for BananaOS.
 *
 * Open it and a game is already chosen. The top screen is the game (its box art, or the DS
 * game's own icon when Settings > Display & sound > Menu look > DS cartridges is "Game icon");
 * the bottom screen has the three choices: START GAME (A), RANDOMIZE AGAIN (X), CLOSE (B).
 * START writes the game's path to /run/banana/launch and exits; the menu starts it exactly as
 * A on the game would.
 *
 * It reads the menu's files and writes nothing but /run/banana/launch. Plain C++17: no
 * FreeType, no libdrm, no SDL, no runtime libraries. Pixelify Sans is baked into
 * randomizer-font.h; PNG/JPEG pictures come from stb_image, compiled in.
 *
 *   banana-randomizer [--root /data] [--systems FILE] [--roms DIR] [--assets DIR]
 *                     [--smoke PREFIX] [--size 640x480|1024x768] [--seed N] [--pick N]
 *                     [--setting box|icon] [--press a,x,b] [--touch X,Y] [--launch-out FILE]
 *                     [--accent NAME] [--theme dark|light] [--connector DSI-2] [--list] [--input-debug]
 *
 * The drawing primitives and the DRM/evdev shim are the ones from the Activity app
 * (store/apps/activity, MIT), reduced and adapted.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define unlink _unlink
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

#include "randomizer-font.h"
#include "randomizer-logo.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static bool gLight = false;	// the menu's theme: false dark, true light

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
				fprintf(stderr, "randomizer: input %s \"%s\" %s\n", path.c_str(), iname,
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
			if (debug && ev.type == EV_KEY) fprintf(stderr, "randomizer: key %d = %d\n", ev.code, ev.value);
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
static uint32_t C_INK = 0xf1ede2, C_MUTED = 0xa39d8e, C_LINE = 0x8a8374, C_EDGE = 0xe8e2d3;
static uint32_t C_NAVY = 0xfff6dc, C_RED = 0xff3b4a;
static uint32_t C_BG = 0x17161c, C_FRAME = 0x0b0a0d, C_BAND = 0x2e2b33;
static uint32_t C_PANEL_BAND = 0x3a3540, C_PANEL = 0x17161c, C_PANEL_LOW = 0x0b0a0d;
static uint32_t C_CHIP_BAND = 0x2e2b33, C_CHIP = 0x121117;
static uint32_t C_BUBBLE_BAND = 0x38333b, C_BUBBLE = 0x191820, C_BUBBLE_LOW = 0x0f0e13;
static uint32_t C_TRACK = 0x100f14, C_METER = 0x15141a, C_RULE = 0x45403d, C_BRAND = 0x262219;
static uint32_t C_KBD = 0xf1ede2, C_KBD_INK = 0x17161c;
static uint32_t C_FAV_BAND = 0x5a2430, C_FAV = 0x2a1016;

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
						uint32_t dot = gLight ? 0x3a2e14u : 0xffffffu;
						if (u == 0 && v == 0) blend(xx, yy, dot, gLight ? 20 : 36);
						else if (u == 2 && v == 2) blend(xx, yy, dot, gLight ? 10 : 18);
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


/* ============================================================================================
 * Randomizer
 * ============================================================================================ */

/* ============================================================================================
 * Randomizer: what the app is
 * ============================================================================================
 * One random game from the card, the moment the app opens. The top screen is the game (box
 * art, or the DS game's own icon when Settings > Display & sound > Menu look > DS cartridges
 * is "Game icon"), the bottom one has the three choices: start it, pick another, close.
 *
 * It reads the same files as the menu: /run/banana/systems.conf (the merged systems list),
 * the ROM folders under /data/roms and the second card, gamelist.xml and media/box pictures,
 * banana/playstats.tsv, banana/game-titles.tsv, banana/settings.conf and the store's
 * hidden-systems. It writes one thing: the chosen game's path to /run/banana/launch, which the
 * menu starts exactly as A on the game would.
 *
 * No libraries beyond the C++ standard library and libc. Text is Pixelify Sans, baked
 * into randomizer-font.h; pictures come from stb_image (PNG/JPEG), compiled in.
 * ============================================================================================ */

/* ---- the battery and the network, as the menu's status row shows them ------------------------------------------- */

struct Power {
	int pct = -1;
	bool charging = false;
};

static Power readPower()
{
	Power p;
#ifdef _WIN32
	return p;
#else
	DIR *d = opendir("/sys/class/power_supply");
	if (!d) return p;
	struct dirent *e;
	while ((e = readdir(d)) != nullptr)
	{
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
		if (st)
		{
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
	for (const char *n : { "wlan0", "eth0" })
	{
		std::ifstream f((std::string("/sys/class/net/") + n + "/operstate").c_str());
		std::string s;
		if (f >> s) return s == "up";
	}
	return true;
#endif
}

/* ---- settings, looks and systems ------------------------------------------------------------------------------- */

static std::string lowerStr(std::string s)
{
	for (auto &c : s) c = (char)std::tolower((unsigned char)c);
	return s;
}

static bool fileExists(const std::string &p)
{
	if (p.empty()) return false;
	std::error_code ec;
	return std::filesystem::exists(std::filesystem::path(p), ec);
}

static bool dirExists(const std::string &p)
{
	if (p.empty()) return false;
	std::error_code ec;
	return std::filesystem::is_directory(std::filesystem::path(p), ec);
}

static std::string trimLine(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && (unsigned char)s[a] <= ' ') a++;
	while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
	return s.substr(a, b - a);
}

struct Settings {
	std::string accent = "yellow";
	std::string timezone;
	std::string dsCartArt = "box";	// box | icon
	std::string clock = "24h";		// 24h | 12h
	std::string theme = "dark";		// the menu's dark/light palette; a user theme's own colours are not read
	std::map<std::string, std::string> shelf;	// settings.conf's shelf.<id>
	std::set<std::string> hidden;	// the Store's hidden-systems
	bool iconCart() const { return dsCartArt == "icon"; }
	bool twelveHour() const { return clock == "12h"; }
};

static Settings loadSettings(const std::string &root)
{
	Settings s;
	std::ifstream f((root + "/banana/settings.conf").c_str());
	std::string line;
	while (std::getline(f, line))
	{
		line = trimLine(line);
		if (line.empty() || line[0] == '#') continue;
		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string key = trimLine(line.substr(0, eq)), v = trimLine(line.substr(eq + 1));
		if (key == "accent") s.accent = v;
		else if (key == "timezone") s.timezone = v;
		else if (key == "ds_cart_art") s.dsCartArt = v == "icon" ? "icon" : "box";
		else if (key == "clock") s.clock = v == "12h" ? "12h" : "24h";
		else if (key == "theme") s.theme = v;
		else if (key.compare(0, 6, "shelf.") == 0 && key.size() > 6) s.shelf[key.substr(6)] = v;
	}
	std::ifstream h((root + "/store/hidden-systems").c_str());
	while (std::getline(h, line))
	{
		line = trimLine(line);
		if (!line.empty() && line[0] != '#') s.hidden.insert(lowerStr(line));
	}
	return s;
}

static bool applyAccent(const std::string &name)
{
	if (name == "yellow") gAccent = ACC_YELLOW;
	else if (name == "blue") gAccent = ACC_BLUE;
	else if (name == "red") gAccent = ACC_RED;
	else if (name == "green") gAccent = ACC_GREEN;
	else if (name == "purple") gAccent = ACC_PURPLE;
	else if (name == "pink") gAccent = ACC_PINK;
	else return false;
	return true;
}

/* the menu's two palettes (ui/src/settings_pal.cpp; the light values are the menu's own). The app follows
 * Settings > Display & sound > Theme: dark is the default, light is the paper one. A user theme's own colours
 * are not read (as in Activity): its accent and system looks are. */
static void applyTheme(const std::string &theme)
{
	gLight = theme == "light";
	if (!gLight)
	{
		C_INK = 0xf1ede2; C_MUTED = 0xa39d8e; C_LINE = 0x8a8374; C_EDGE = 0xe8e2d3;
		C_NAVY = 0xfff6dc; C_RED = 0xff3b4a;
		C_BG = 0x17161c; C_FRAME = 0x0b0a0d; C_BAND = 0x2e2b33;
		C_PANEL_BAND = 0x3a3540; C_PANEL = 0x17161c; C_PANEL_LOW = 0x0b0a0d;
		C_CHIP_BAND = 0x2e2b33; C_CHIP = 0x121117;
		C_BUBBLE_BAND = 0x38333b; C_BUBBLE = 0x191820; C_BUBBLE_LOW = 0x0f0e13;
		C_TRACK = 0x100f14; C_METER = 0x15141a; C_RULE = 0x45403d; C_BRAND = 0x262219;
		C_KBD = 0xf1ede2; C_KBD_INK = 0x17161c;
		C_FAV_BAND = 0x5a2430; C_FAV = 0x2a1016;
	}
	else
	{
		C_INK = 0x2a2620; C_MUTED = 0x6b6252; C_LINE = 0xa69d8c; C_EDGE = 0x3a342c;
		C_NAVY = 0x2a2620; C_RED = 0xe23b4a;
		C_BG = 0xf3efe6; C_FRAME = 0xd4ccbd; C_BAND = 0xfdfcf9;
		C_PANEL_BAND = 0xffffff; C_PANEL = 0xf6f2ea; C_PANEL_LOW = 0xd8d0c0;
		C_CHIP_BAND = 0xffffff; C_CHIP = 0xf6f2ea;
		C_BUBBLE_BAND = 0xffffff; C_BUBBLE = 0xfaf7f0; C_BUBBLE_LOW = 0xddd5c6;
		C_TRACK = 0xe6dfd0; C_METER = 0xe3dccd; C_RULE = 0xd6cfc0; C_BRAND = 0x262219;
		C_KBD = 0x3a342c; C_KBD_INK = 0xf2f6fa;
		C_FAV_BAND = 0xf2b8bb; C_FAV = 0xf7d7d8;
	}
	// the sheet: 34% of the accent over the dark ink, or 20% over the light muted (the menu's tints)
	gSheetFill = gLight ? mixColor(gAccent.base, 0.20, 0xa39d8e) : mixColor(gAccent.base, 0.34, 0x15141a);
}

/* ---- the theme's systems.cfg: each system's icon and accent ----------------------------------------------------- */

struct Look {
	std::string icon;
	uint32_t accent = 0x3d8ec4;
	bool hasAccent = false;
};

struct Looks {
	std::map<std::string, Look> byId;
	std::map<std::string, uint32_t> byMaker;
};

static bool parseHexColor(const std::string &s, uint32_t &out)
{
	if (s.size() != 7 || s[0] != '#') return false;
	char *end = nullptr;
	unsigned long v = strtoul(s.c_str() + 1, &end, 16);
	if (!end || *end) return false;
	out = (uint32_t)v;
	return true;
}

static Looks loadLooks(const std::string &assets)
{
	Looks out;
	std::ifstream f((assets + "/systems.cfg").c_str());
	std::string line;
	while (std::getline(f, line))
	{
		line = trimLine(line);
		if (line.empty() || line[0] == '#') continue;
		if (line[0] == '@')
		{
			size_t hash = line.rfind('#');
			if (hash == std::string::npos) continue;
			uint32_t c;
			if (parseHexColor(trimLine(line.substr(hash)), c))
				out.byMaker[lowerStr(trimLine(line.substr(1, hash - 1)))] = c;
			continue;
		}
		std::istringstream is(line);
		std::string names, icon, accent;
		is >> names >> icon >> accent;
		Look look;
		look.icon = icon == "-" || icon.empty() ? "" : assets + "/icons/" + icon;
		if (parseHexColor(accent, look.accent)) look.hasAccent = true;
		std::stringstream ns(names);
		std::string n;
		while (std::getline(ns, n, ','))
		{
			n = lowerStr(trimLine(n));
			if (!n.empty()) out.byId[n] = look;
		}
	}
	return out;
}

static Look lookOf(const Looks &looks, const std::string &id, const std::string &maker)
{
	auto it = looks.byId.find(lowerStr(id));
	if (it == looks.byId.end()) it = looks.byId.find("*");
	Look look = it == looks.byId.end() ? Look() : it->second;
	if (!look.hasAccent)
	{
		auto mk = looks.byMaker.find(lowerStr(maker));
		if (mk != looks.byMaker.end()) { look.accent = mk->second; look.hasAccent = true; }
	}
	return look;
}

/* ---- systems.conf ------------------------------------------------------------------------------------------------ */

struct SystemDef {
	std::string id, name, maker, folder, run;
	std::vector<std::string> exts;
	bool flat = false;
};

static std::vector<SystemDef> readSystems(const std::string &path)
{
	std::vector<SystemDef> defs;
	std::ifstream f(path.c_str());
	std::string line;
	SystemDef *cur = nullptr;
	while (std::getline(f, line))
	{
		line = trimLine(line);
		if (line.empty() || line[0] == '#') continue;
		if (line[0] == '[' && line.back() == ']')
		{
			SystemDef d;
			d.id = d.folder = lowerStr(trimLine(line.substr(1, line.size() - 2)));
			defs.push_back(d);
			cur = &defs.back();
			continue;
		}
		if (!cur) continue;
		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string key = lowerStr(trimLine(line.substr(0, eq)));
		std::string v = trimLine(line.substr(eq + 1));
		if (key == "name") cur->name = v;
		else if (key == "maker") cur->maker = v;
		else if (key == "folder") cur->folder = v;
		else if (key == "run") cur->run = v;
		else if (key == "flat") cur->flat = v == "yes" || v == "1" || v == "true";
		else if (key == "ext")
		{
			std::istringstream is(v);
			std::string e;
			while (is >> e)
			{
				if (!e.empty() && e[0] == '.') e = e.substr(1);
				if (!e.empty()) cur->exts.push_back(lowerStr(e));
			}
		}
	}
	return defs;
}

static std::string folderPath(const std::string &romRoot, const std::string &folder)
{
	return !folder.empty() && folder[0] == '/' ? folder : romRoot + "/" + folder;
}

static std::vector<std::string> romRoots(const std::string &roms)
{
	std::vector<std::string> out;
	if (!roms.empty()) out.push_back(roms);
	std::ifstream f("/run/banana/sd2/roms");
	std::string line;
	if (f && std::getline(f, line))
	{
		line = trimLine(line);
		if (!line.empty() && std::find(out.begin(), out.end(), line) == out.end()) out.push_back(line);
	}
	return out;
}

/* ---- ROMs, gamelist.xml and media/box --------------------------------------------------------------------------- */

static std::string cleanTitle(std::string t)
{
	for (auto &c : t) if (c == '_') c = ' ';
	std::string r = trimLine(t);
	for (;;)
	{
		if (r.size() < 3) break;
		char e = r.back();
		char o = e == ')' ? '(' : (e == ']' ? '[' : 0);
		if (!o) break;
		size_t at = r.rfind(o);
		if (at == std::string::npos || at == 0) break;
		std::string rest = trimLine(r.substr(0, at));
		if (rest.empty()) break;
		r = rest;
	}
	return r.empty() ? trimLine(t) : r;
}

static bool extMatch(const std::string &name, const std::vector<std::string> &exts)
{
	std::string n = lowerStr(name);
	for (const std::string &e : exts)
	{
		if (n.size() <= e.size() + 1) continue;
		if (n.compare(n.size() - e.size() - 1, std::string::npos, "." + e) == 0) return true;
	}
	return false;
}

static bool mediaDirName(const std::string &name)
{
	std::string n = lowerStr(name);
	return n == "media" || n == "images" || n == "videos" || n == "manuals" || n == "downloaded_images";
}

static void listRoms(const std::string &dir, const std::string &prefix, int depth, int maxDepth,
		     const std::vector<std::string> &exts, std::vector<std::string> &out)
{
	std::error_code ec;
	std::filesystem::directory_iterator it(std::filesystem::path(dir), ec), end;
	if (ec) return;
	for (; it != end; it.increment(ec))
	{
		if (ec) break;
		const std::filesystem::directory_entry &e = *it;
		std::string name = e.path().filename().string();
		if (name.empty() || name[0] == '.') continue;
		std::string rel = prefix.empty() ? name : prefix + "/" + name;
		std::error_code sec;
		bool isDir = e.is_directory(sec);
		if (isDir)
		{
			if (depth < maxDepth && !mediaDirName(name)) listRoms(e.path().string(), rel, depth + 1, maxDepth, exts, out);
			continue;
		}
		if (e.is_regular_file(sec) && extMatch(name, exts)) out.push_back(rel);
	}
}

struct GlEntry {
	std::string name, sortname, genre, date, image, thumb;
};

static std::string xmlText(const std::string &block, const char *tag)
{
	std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
	size_t a = block.find(open);
	if (a == std::string::npos) return "";
	a += open.size();
	size_t b = block.find(close, a);
	if (b == std::string::npos) return "";
	std::string s = block.substr(a, b - a), r;
	static const std::pair<const char *, char> ents[] = {
		{ "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' }
	};
	for (size_t i = 0; i < s.size();)
	{
		bool hit = false;
		if (s[i] == '&')
			for (const auto &e : ents)
				if (!s.compare(i, strlen(e.first), e.first)) { r += e.second; i += strlen(e.first); hit = true; break; }
		if (!hit) r += s[i++];
	}
	return trimLine(r);
}

static std::map<std::string, GlEntry> readGamelist(const std::string &dir)
{
	std::map<std::string, GlEntry> out;
	std::ifstream f((dir + "/gamelist.xml").c_str());
	if (!f) return out;
	std::stringstream ss;
	ss << f.rdbuf();
	std::string x = ss.str();
	size_t at = 0;
	auto resolve = [&dir](std::string p) {
		p = trimLine(p);
		if (p.empty()) return p;
		if (p.compare(0, 2, "./") == 0) return dir + "/" + p.substr(2);
		if (p[0] != '/') return dir + "/" + p;
		return p;
	};
	while ((at = x.find("<game", at)) != std::string::npos)
	{
		size_t end = x.find("</game>", at);
		if (end == std::string::npos) break;
		std::string b = x.substr(at, end - at);
		at = end + 7;
		std::string path = xmlText(b, "path");
		if (path.compare(0, 2, "./") == 0) path = path.substr(2);
		if (path.empty()) continue;
		GlEntry e;
		e.name = xmlText(b, "name");
		e.sortname = xmlText(b, "sortname");
		e.genre = xmlText(b, "genre");
		e.date = xmlText(b, "releasedate");
		e.image = resolve(xmlText(b, "image"));
		e.thumb = resolve(xmlText(b, "thumbnail"));
		out[path] = e;
	}
	return out;
}

// media/box/<rom name>.png or .jpg, matched without case (the card is exFAT) and listed once per folder
struct MediaIndex {
	std::map<std::string, std::map<std::string, std::string> > dirs;
	std::string find(const std::string &sysDir, const char *kind, const std::string &stem)
	{
		std::string d = sysDir + "/media/" + kind;
		auto it = dirs.find(d);
		if (it == dirs.end())
		{
			std::map<std::string, std::string> names;
			std::error_code ec;
			std::filesystem::directory_iterator fi(std::filesystem::path(d), ec), end;
			if (!ec)
				for (; fi != end; fi.increment(ec))
				{
					if (ec) break;
					std::string n = fi->path().filename().string();
					if (!n.empty() && n[0] != '.') names.emplace(lowerStr(n), n);
				}
			it = dirs.emplace(d, std::move(names)).first;
		}
		if (it->second.empty()) return "";
		for (const char *ext : { ".png", ".jpg" })
		{
			auto n = it->second.find(lowerStr(stem + ext));
			if (n != it->second.end()) return d + "/" + n->second;
		}
		return "";
	}
};

static std::string fileStem(const std::string &rel)
{
	size_t slash = rel.find_last_of('/');
	std::string base = slash == std::string::npos ? rel : rel.substr(slash + 1);
	size_t dot = base.rfind('.');
	std::string stem = dot == std::string::npos ? base : base.substr(0, dot);
	if (lowerStr(stem).size() > 3 && lowerStr(stem).compare(stem.size() - 3, 3, ".p8") == 0) stem.resize(stem.size() - 3);
	return stem;
}

struct Game {
	std::string path, rel, title, sysId, sysName, genre, year, box;
	uint32_t accent = 0x3d8ec4;
	long long plays = 0, secs = 0, last = 0;
};

static bool isDsRom(const std::string &path, const std::string &sysId)
{
	size_t dot = path.rfind('.');
	if (dot == std::string::npos) return false;
	std::string ext = lowerStr(path.substr(dot + 1));
	return ext == "nds" || (ext == "zip" && sysId == "nds");
}

/* the menu's playstats.tsv: "<system>/<rel>  plays  seconds  last [ achA achB ]" */
struct Stat { long long plays = 0, secs = 0, last = 0; };

static std::map<std::string, Stat> readStats(const std::string &root)
{
	std::map<std::string, Stat> out;
	std::ifstream f((root + "/banana/playstats.tsv").c_str());
	std::string line;
	while (std::getline(f, line))
	{
		std::vector<std::string> c;
		std::stringstream ls(line);
		std::string part;
		while (std::getline(ls, part, '\t')) c.push_back(part);
		if (c.size() < 4) continue;
		Stat s;
		s.plays = atoll(c[1].c_str());
		s.secs = atoll(c[2].c_str());
		s.last = atoll(c[3].c_str());
		out[c[0]] = s;
	}
	return out;
}

/* the game page's own name and sort title: "<system>/<rel>  name  sort title" */
static std::map<std::string, std::pair<std::string, std::string> > readTitles(const std::string &root)
{
	std::map<std::string, std::pair<std::string, std::string> > out;
	std::ifstream f((root + "/banana/game-titles.tsv").c_str());
	std::string line;
	while (std::getline(f, line))
	{
		std::vector<std::string> c;
		std::stringstream ls(line);
		std::string part;
		while (std::getline(ls, part, '\t')) c.push_back(part);
		if (c.size() < 2 || c[0].empty()) continue;
		out[c[0]] = std::make_pair(trimLine(c[1]), c.size() > 2 ? trimLine(c[2]) : std::string());
	}
	return out;
}

/* every ROM the menu would list: the systems' folders, without the built-in app tiles and collections */
static void scanLibrary(const Settings &st, const std::string &systemsPath, const std::string &roms,
			const std::string &root, const std::string &assets, std::vector<Game> &games, int &systemCount)
{
	games.clear();
	systemCount = 0;
	std::vector<SystemDef> defs = readSystems(systemsPath);
	Looks looks = loadLooks(assets);
	std::map<std::string, Stat> stats = readStats(root);
	auto titles = readTitles(root);
	MediaIndex media;
	std::set<std::string> seenSystems;

	for (const SystemDef &def : defs)
	{
		if (def.id.empty() || def.exts.empty()) continue;
		if (st.hidden.count(lowerStr(def.id))) continue;
		auto mode = st.shelf.find(lowerStr(def.id));
		if (mode != st.shelf.end() && mode->second == "hide") continue;
		if (!def.folder.empty() && def.folder[0] == '/') continue;	// an app shipped with the system
		if (def.run.compare(0, 7, "banana:") == 0) continue;		// one of the menu's own pages

		Look look = lookOf(looks, def.id, def.maker);
		std::string sysName = def.name.empty() ? def.id : def.name;
		bool added = false;
		std::vector<std::string> roots = romRoots(roms);
		for (size_t ri = 0; ri < roots.size(); ri++)
		{
			if (ri > 0 && !def.folder.empty() && def.folder[0] == '/') continue;
			std::string dir = folderPath(roots[ri], def.folder);
			if (!dirExists(dir)) continue;
			std::vector<std::string> rels;
			listRoms(dir, "", 0, def.flat ? 0 : 2, def.exts, rels);
			std::map<std::string, GlEntry> gl = readGamelist(dir);
			for (const std::string &rel : rels)
			{
				if (lowerStr(rel).size() >= 4 && lowerStr(rel).compare(rel.size() - 4, 4, ".app") == 0) continue;	// a built-in app's entry, not a game
				Game g;
				g.sysId = def.id;
				g.sysName = sysName;
				g.accent = look.accent;
				g.path = dir + "/" + rel;
				g.rel = (ri > 0 ? "@sd2/" : "") + rel;
				g.title = cleanTitle(fileStem(rel));
				auto it = gl.find(rel);
				if (it != gl.end())
				{
					if (!it->second.name.empty()) g.title = cleanTitle(it->second.name);
					std::string genre = it->second.genre;
					size_t cut = genre.find_first_of(",;/|");
					g.genre = trimLine(cut == std::string::npos ? genre : genre.substr(0, cut));
					const std::string &d = it->second.date;
					if (d.size() >= 4 && d.compare(0, 4, "0000") && std::all_of(d.begin(), d.begin() + 4, [](char c) { return c >= '0' && c <= '9'; }))
						g.year = d.substr(0, 4);
					g.box = fileExists(it->second.thumb) ? it->second.thumb : (fileExists(it->second.image) ? it->second.image : "");
				}
				std::string stem = fileStem(rel);
				if (g.box.empty()) g.box = media.find(dir, "box", stem);
				std::string key = def.id + "/" + g.rel;
				auto tt = titles.find(key);
				if (tt != titles.end() && !tt->second.first.empty()) g.title = tt->second.first;
				auto sg = stats.find(key);
				if (sg != stats.end()) { g.plays = sg->second.plays; g.secs = sg->second.secs; g.last = sg->second.last; }
				games.push_back(g);
				added = true;
			}
		}
		if (added) seenSystems.insert(def.id);
	}
	std::stable_sort(games.begin(), games.end(), [](const Game &a, const Game &b) {
		std::string ta = lowerStr(a.title), tb = lowerStr(b.title);
		if (ta != tb) return ta < tb;
		return a.path < b.path;
	});
	systemCount = (int)seenSystems.size();
}

/* ---- text and small helpers -------------------------------------------------------------------------------------- */

static std::string ellipsize(Canvas &c, const std::string &s, int px, bool medium, float ls, int maxW)
{
	if (c.textWidth(s, px, medium, ls, false) / c.s <= maxW) return s;
	std::string out = s;
	while (!out.empty() && c.textWidth(out + "\xE2\x80\xA6", px, medium, ls, false) / c.s > maxW)
	{
		// one whole UTF-8 character, not one byte (Pokémon, Döda)
		size_t n = 1;
		while (out.size() > n && ((unsigned char)out[out.size() - n - 1] & 0xC0) == 0x80) n++;
		out.resize(out.size() - n);
	}
	while (!out.empty() && out.back() == ' ') out.pop_back();
	return out + "\xE2\x80\xA6";
}

static std::vector<std::string> wrapText(Canvas &c, const std::string &text, int px, bool medium, float ls,
					 int maxW, int maxLines)
{
	std::vector<std::string> lines;
	std::istringstream is(text);
	std::vector<std::string> words;
	for (std::string w; is >> w;) words.push_back(w);
	if (words.empty()) return lines;

	std::string line;
	for (size_t i = 0; i < words.size(); i++)
	{
		std::string word = words[i];
		if (c.textWidth(word, px, medium, ls, false) / c.s > maxW)
			word = ellipsize(c, word, px, medium, ls, maxW);
		std::string tryLine = line.empty() ? word : line + " " + word;
		if (c.textWidth(tryLine, px, medium, ls, false) / c.s <= maxW || line.empty())
		{
			line = tryLine;
			continue;
		}
		lines.push_back(line);
		line = word;
		if ((int)lines.size() == maxLines) break;
	}
	if (line.empty()) return lines;
	if ((int)lines.size() < maxLines) lines.push_back(line);
	else if (!lines.empty()) lines.back() = ellipsize(c, lines.back() + " " + line, px, medium, ls, maxW);
	return lines;
}

static uint32_t inkOnColour(uint32_t rgb)
{
	int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
	int y = (r * 299 + g * 587 + b * 114) / 1000;
	return y >= 145 ? 0x17161c : 0xffffff;
}

static std::string clockText(time_t t, bool twelve)
{
	if (!twelve) return fmtClock(t);
	struct tm tm;
	localTime(t, &tm);
	char buf[24];
	snprintf(buf, sizeof(buf), "%d:%02d %s", tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12, tm.tm_min,
		 tm.tm_hour < 12 ? "AM" : "PM");
	return buf;
}

static std::string relDay(time_t t)
{
	if (t <= 0) return "never";
	time_t now = time(nullptr);
	struct tm a, b;
	localTime(t, &a);
	localTime(now, &b);
	struct tm da = a, db = b;
	da.tm_hour = db.tm_hour = 12;
	da.tm_min = db.tm_min = 0;
	da.tm_sec = db.tm_sec = 0;
	long days = (long)std::lround(difftime(mktime(&db), mktime(&da)) / 86400.0);
	static const char *wd[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
	static const char *mo[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	if (days <= 0) return "today";
	if (days == 1) return "yesterday";
	if (days < 7) return std::string(wd[a.tm_wday]);
	if (days < 14) return "last week";
	char buf[32];
	if (days < 330) snprintf(buf, sizeof(buf), "%s %d", mo[a.tm_mon], a.tm_mday);
	else snprintf(buf, sizeof(buf), "%s %d", mo[a.tm_mon], a.tm_year + 1900);
	return buf;
}

/* ---- pictures: PNG/JPEG box art, and a DS game's own 32x32 icon -------------------------------------------------- */

struct Image {
	int w = 0, h = 0;
	std::vector<uint32_t> px;	// 0xAARRGGBB
	bool ok() const { return w > 0 && h > 0 && px.size() == (size_t)w * h; }
};

static Image loadPicture(const std::string &path)
{
	Image im;
	if (path.empty()) return im;
	int w = 0, h = 0, n = 0;
	unsigned char *d = stbi_load(path.c_str(), &w, &h, &n, 4);
	if (!d) return im;
	im.w = w;
	im.h = h;
	im.px.resize((size_t)w * h);
	for (int i = 0; i < w * h; i++)
	{
		unsigned a = d[i * 4 + 3];
		im.px[i] = (a << 24) | ((unsigned)d[i * 4] << 16) | ((unsigned)d[i * 4 + 1] << 8) | d[i * 4 + 2];
	}
	stbi_image_free(d);
	return im;
}

static bool endsWithNoCase(const std::string &s, const char *suffix)
{
	size_t n = strlen(suffix);
	if (s.size() < n) return false;
	for (size_t i = 0; i < n; i++)
		if (tolower((unsigned char)s[s.size() - n + i]) != suffix[i]) return false;
	return true;
}

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* a DS banner's 32x32 icon (the same decode as ui/rnds/RndsRaster.cpp's ndsBannerIcon) */
static Image ndsBannerImage(const uint8_t *b)
{
	static const size_t NDS_BANNER = 0x240;
	Image out;
	unsigned version = b[0] | (b[1] << 8);
	if (version != 1 && version != 2 && version != 3 && version != 0x103) return out;
	uint8_t pal[16][4];
	for (int i = 0; i < 16; i++)
	{
		unsigned c = b[0x220 + 2 * i] | (b[0x221 + 2 * i] << 8);	// BGR555
		unsigned r = c & 31, g = (c >> 5) & 31, bl = (c >> 10) & 31;
		pal[i][0] = (uint8_t)((r << 3) | (r >> 2));
		pal[i][1] = (uint8_t)((g << 3) | (g >> 2));
		pal[i][2] = (uint8_t)((bl << 3) | (bl >> 2));
		pal[i][3] = i == 0 ? 0 : 255;
	}
	out.w = out.h = 32;
	out.px.assign(32 * 32, 0);
	for (int t = 0; t < 16; t++)	// tile t = ty * 4 + tx
		for (int y = 0; y < 8; y++)
			for (int x = 0; x < 8; x++)
			{
				uint8_t v = b[0x20 + t * 32 + y * 4 + x / 2];
				const uint8_t *p = pal[x & 1 ? v >> 4 : v & 15];
				out.px[((t / 4) * 8 + y) * 32 + (t % 4) * 8 + x] =
					((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
			}
	(void)NDS_BANNER;
	return out;
}

static Image loadNdsBanner(const std::string &rom)
{
	Image out;
	if (!endsWithNoCase(rom, ".nds")) return out;
	FILE *f = fopen(rom.c_str(), "rb");
	if (!f) return out;
	static const long NDS_BANNER_AT = 0x68, NDS_HEADER = 0x200, NDS_BANNER = 0x240;
	uint8_t head[NDS_BANNER_AT + 4], banner[NDS_BANNER];
	bool ok = fread(head, 1, sizeof(head), f) == sizeof(head) && fseek(f, 0, SEEK_END) == 0;
	long size = ok ? ftell(f) : -1;
	uint64_t at = ok ? le32(&head[NDS_BANNER_AT]) : 0;
	ok = ok && size > 0 && at >= (uint64_t)NDS_HEADER && at + NDS_BANNER <= (uint64_t)size &&
	     fseek(f, (long)at, SEEK_SET) == 0 && fread(banner, 1, sizeof(banner), f) == sizeof(banner);
	fclose(f);
	return ok ? ndsBannerImage(banner) : out;
}

/* the menu's icon cache: <root>/banana/cache/icons/<base>.<path hash>.<rom size>.<rom mtime>.<icon size>.png */
static uint64_t fnv1a(const std::string &s)
{
	uint64_t h = 0xcbf29ce484222325ULL;
	for (unsigned char c : s) { h ^= c; h *= 0x100000001b3ULL; }
	return h;
}

static Image loadCachedDsIcon(const std::string &root, const std::string &rom)
{
	Image out;
	std::error_code ec;
	struct stat rs;
	if (stat(rom.c_str(), &rs) != 0) return out;
	std::string base = rom.substr(rom.find_last_of("/\\") + 1);
	if (base.size() > 160)
	{
		size_t cut = 160;
		while (cut > 0 && ((unsigned char)base[cut] & 0xc0) == 0x80) cut--;
		base.resize(cut);
	}
	for (char &c : base) if (c == '\\') c = '_';
	char tail[80];
	snprintf(tail, sizeof(tail), ".%016llx.%llx.%llx.", (unsigned long long)fnv1a(rom),
		 (unsigned long long)rs.st_size, (unsigned long long)rs.st_mtime);
	std::string prefix = base + tail;
	std::string dir = root + "/banana/cache/icons";
	std::filesystem::directory_iterator it(std::filesystem::path(dir), ec), end;
	if (ec) return out;
	std::string best;
	uintmax_t bestBytes = 0;
	for (; it != end; it.increment(ec))
	{
		if (ec) break;
		std::string n = it->path().filename().string();
		if (n.size() < prefix.size() + 4 || n.compare(0, prefix.size(), prefix) != 0 || !endsWithNoCase(n, ".png")) continue;
		std::error_code sec;
		if (!it->is_regular_file(sec)) continue;
		uintmax_t bytes = it->file_size(sec);
		if (sec || bytes == 0) continue;
		if (best.empty() || bytes > bestBytes) { best = it->path().string(); bestBytes = bytes; }
	}
	if (!best.empty()) out = loadPicture(best);
	return out;
}

static Image loadDsIcon(const std::string &root, const std::string &rom)
{
	Image im = loadNdsBanner(rom);
	if (im.ok()) return im;
	return loadCachedDsIcon(root, rom);
}

/* ---- drawing pictures onto a Canvas ----------------------------------------------------------------------------- */

static void blitImage(Canvas &c, const Image &im, int x, int y, int w, int h, bool smooth)
{
	if (!im.ok() || w <= 0 || h <= 0) return;
	int X = c.U(x), Y = c.U(y), W = c.U(w), H = c.U(h);
	if (W <= 0 || H <= 0) return;
	for (int yy = 0; yy < H; yy++)
	{
		float sy = (yy + 0.5f) * im.h / H - 0.5f;
		for (int xx = 0; xx < W; xx++)
		{
			float sx = (xx + 0.5f) * im.w / W - 0.5f;
			uint32_t v;
			if (!smooth)
			{
				int ix = (int)std::floor(sx + 0.5f), iy = (int)std::floor(sy + 0.5f);
				if (ix < 0) ix = 0;
				if (iy < 0) iy = 0;
				if (ix >= im.w) ix = im.w - 1;
				if (iy >= im.h) iy = im.h - 1;
				v = im.px[(size_t)iy * im.w + ix];
			}
			else
			{
				int x0 = (int)std::floor(sx), y0 = (int)std::floor(sy);
				float fx = sx - x0, fy = sy - y0;
				int x1 = x0 + 1, y1 = y0 + 1;
				x0 = std::max(0, std::min(im.w - 1, x0));
				y0 = std::max(0, std::min(im.h - 1, y0));
				x1 = std::max(0, std::min(im.w - 1, x1));
				y1 = std::max(0, std::min(im.h - 1, y1));
				uint32_t a = im.px[(size_t)y0 * im.w + x0], b = im.px[(size_t)y0 * im.w + x1];
				uint32_t d = im.px[(size_t)y1 * im.w + x0], e = im.px[(size_t)y1 * im.w + x1];
				auto mix = [&](int shift) {
					float top = ((a >> shift) & 255) * (1 - fx) + ((b >> shift) & 255) * fx;
					float bot = ((d >> shift) & 255) * (1 - fx) + ((e >> shift) & 255) * fx;
					return (unsigned)std::lround(top * (1 - fy) + bot * fy);
				};
				unsigned al = mix(24), r = mix(16), g = mix(8), bl = mix(0);
				v = (al << 24) | (r << 16) | (g << 8) | bl;
			}
			unsigned alpha = (v >> 24) & 255;
			if (alpha == 0) continue;
			c.blend(X + xx, Y + yy, v & 0xffffff, (int)alpha);
		}
	}
}

struct Art {
	Image img;
	bool icon = false;
	bool systemIcon = false;
	std::string label;
};

static Art chooseArt(const Game &g, const Settings &st, const std::string &root, const Looks &looks)
{
	Art art;
	// DS cartridges: Game icon (the menu's setting): the game's own 32x32 icon, before any picture; the box
	// stays the fallback when the ROM has no icon, exactly as on the menu's shelves.
	if (isDsRom(g.path, g.sysId) && st.iconCart())
	{
		art.img = loadDsIcon(root, g.path);
		if (art.img.ok()) { art.icon = true; art.label = "DS CARTRIDGES: GAME ICON"; return art; }
	}
	if (!g.box.empty())
	{
		art.img = loadPicture(g.box);
		if (art.img.ok()) { art.label = "BOX ART"; return art; }
	}
	if (isDsRom(g.path, g.sysId))
	{
		art.img = loadDsIcon(root, g.path);
		if (art.img.ok()) { art.icon = true; art.label = "DS GAME ICON"; return art; }
	}
	Look look = lookOf(looks, g.sysId, "");
	if (!look.icon.empty())
	{
		art.img = loadPicture(look.icon);
		if (art.img.ok()) { art.systemIcon = true; art.label = "SYSTEM ICON"; return art; }
	}
	art.label = "NO ART";
	return art;
}

static BoxStyle artFrameStyle()
{
	BoxStyle b;
	b.r = 6;
	b.fill = C_FRAME;
	b.border = C_EDGE;
	b.borderW = 3;
	b.shadow = 0x000000;
	b.shadowDx = 5;
	b.shadowDy = 5;
	b.shadowA = 120;
	return b;
}

static void drawArtwork(Canvas &c, const Art &art)
{
	const int areaX = 42, areaY = 108, areaW = 250, areaH = 300;
	int w = 220, h = 300;
	if (art.img.ok())
	{
		double r = (double)art.img.w / art.img.h;
		if (art.icon || art.systemIcon)
		{
			// a 32x32 icon blown up on whole source pixels: 7x at 640 and 11x on the Plus's 1.6x
			// (240 would make some source pixels 7 and some 8 wide, and the stripes would look uneven).
			// The menu's cached icon is already enlarged for the panel (214 CSS px): draw it 1:1.
			if (art.img.w > 64) w = h = 214;
			else w = h = art.icon ? (c.s > 1.3f ? 220 : 224) : (c.s > 1.3f ? 200 : 192);
		}
		else
		{
			w = (int)std::lround(areaH * r);
			if (w > areaW) { w = areaW; h = (int)std::lround(w / r); }
		}
	}
	else
	{
		w = h = 220;
	}
	if (w > areaW) w = areaW;
	if (h > areaH) h = areaH;
	int x = areaX + (areaW - w) / 2, y = areaY + (areaH - h) / 2;
	BoxStyle frame = artFrameStyle();
	if (art.icon) frame.fill = mixColor(gAccent.base, 0.18, C_PANEL_LOW);
	c.box(x - 3, y - 3, w + 6, h + 6, frame);
	if (art.img.ok())
	{
		blitImage(c, art.img, x, y, w, h, !(art.icon || art.systemIcon));
	}
	else
	{
		c.fill(c.U(x), c.U(y), c.U(w), c.U(h), mixColor(gAccent.base, 0.18, C_PANEL_LOW));
		c.text("?", x + w / 2, y + h / 2 - 30, 64, C_MUTED, true, 1);
	}
	if (art.icon) c.fill(c.U(x), c.U(y + h - 3), c.U(w), c.U(3), gAccent.base);
	c.text(art.label, areaX + areaW / 2, 420, 11, C_MUTED, false, 1, 0, 1.4f, true);
}

/* ---- the screen's paper: the menu's dither, frame and top band -------------------------------------------------- */

static void drawBase(Canvas &c)
{
	c.clear(C_BG);
	int step = (int)std::lround(c.s);
	if (step < 1) step = 1;
	uint32_t dot = gLight ? 0x3a2e14 : 0xffffff;
	for (int y = 0; y < c.h; y++)
		for (int x = 0; x < c.w; x++)
		{
			int u = (x / step) % 4, v = (y / step) % 4;
			if (u == 0 && v == 0) c.blend(x, y, dot, gLight ? 20 : 36);
			else if (u == 2 && v == 2) c.blend(x, y, dot, gLight ? 10 : 18);
		}
	c.fillRoundRing(0, 0, c.w, c.h, c.U(2), c.U(4), C_FRAME);
	c.fill(c.U(4), c.U(4), c.w - c.U(8), c.U(8), C_BAND, 128);
	c.fill(c.U(4), c.U(12), c.w - c.U(8), 1, 0xffffff, 20);
}

/* ---- the app ---------------------------------------------------------------------------------------------------- */

struct Zone {
	int x, y, w, h, act;
};

enum { ACT_START = 1, ACT_REROLL, ACT_CLOSE };

class App {
public:
	Settings settings;
	Looks looks;
	std::vector<Game> games;
	int systemCount = 0;
	int index = -1;
	Art art;
	bool quit = false;
	bool online = true;
	Power power;
	long long now = 0;
	std::string toast;
	long long toastUntil = 0;
	std::string launchFile = "/run/banana/launch";
	std::string root = "/data";
	std::string assets = "/usr/share/banana/rnds";
	bool debug = false;
	std::mt19937 rng;

	std::vector<Zone> zones;
	Image emptyDie;
	bool emptyDieTried = false;

	App() : rng(std::random_device{}()) {}

	void init(const Settings &st, const std::string &systemsPath, const std::string &roms,
		  const std::string &dataRoot, const std::string &assetDir, int seed)
	{
		settings = st;
		root = dataRoot;
		assets = assetDir;
		looks = loadLooks(assets);
		if (seed >= 0) rng.seed((uint32_t)seed);
		else rng.seed((uint32_t)(std::random_device{}() ^ (uint32_t)time(nullptr) ^ (uint32_t)nowMs()));
		scanLibrary(settings, systemsPath, roms, root, assets, games, systemCount);
		now = nowSecs();
		power = readPower();
		online = readOnline();
		if (!games.empty()) pick(-1);
		if (debug) fprintf(stderr, "banana-randomizer: %d games, %d systems\n", (int)games.size(), systemCount);
	}

	const Game *game() const { return index >= 0 && index < (int)games.size() ? &games[(size_t)index] : nullptr; }

	void pick(int avoid)
	{
		if (games.empty()) { index = -1; return; }
		int n = (int)games.size();
		int i;
		if (n == 1) i = 0;
		else if (avoid >= 0) { std::uniform_int_distribution<int> d(1, n - 1); i = (avoid + d(rng)) % n; }
		else { std::uniform_int_distribution<int> d(0, n - 1); i = d(rng); }
		index = i;
		art = chooseArt(games[(size_t)i], settings, root, looks);
		toast.clear();
		toastUntil = 0;
		if (debug) fprintf(stderr, "banana-randomizer: pick %d/%d: %s (%s)\n", i + 1, n, games[(size_t)i].title.c_str(), games[(size_t)i].sysId.c_str());
	}

	void reroll()
	{
		if (games.size() < 2)
		{
			toast = "THAT'S THE ONLY GAME IN YOUR LIBRARY";
			toastUntil = nowMs() + 2200;
			return;
		}
		pick(index);
	}

	bool start()
	{
		const Game *g = game();
		if (!g) return false;
		if (!fileExists(g->path))
		{
			toast = "THAT GAME'S FILE IS GONE";
			toastUntil = nowMs() + 2600;
			return false;
		}
		FILE *f = fopen(launchFile.c_str(), "w");
		if (!f)
		{
			toast = "COULDN'T START THAT GAME";
			toastUntil = nowMs() + 2600;
			return false;
		}
		bool wrote = fprintf(f, "%s\n", g->path.c_str()) > 0;
		bool closed = fclose(f) == 0;
		if (!wrote || !closed)
		{
			unlink(launchFile.c_str());
			toast = "COULDN'T START THAT GAME";
			toastUntil = nowMs() + 2600;
			return false;
		}
		if (debug) fprintf(stderr, "banana-randomizer: launch %s\n", g->path.c_str());
		quit = true;
		return true;
	}

	void refresh()
	{
		now = nowSecs();
		power = readPower();
		online = readOnline();
	}

	bool tick()
	{
		bool dirty = false;
		long long secs = nowSecs();
		if (secs / 60 != now / 60) dirty = true;	// the clock (the battery and Wi-Fi are read every 30 s)
		now = secs;
		if (!toast.empty() && nowMs() >= toastUntil) { toast.clear(); dirty = true; }
		return dirty;
	}

	void onKey(Key k)
	{
		if (games.empty())
		{
			if (k == Key::B || k == Key::START) quit = true;
			return;
		}
		switch (k)
		{
		case Key::A: start(); break;
		case Key::X: reroll(); break;
		case Key::B:
		case Key::START: quit = true; break;
		default: break;
		}
	}

	void onTouch(int x, int y)
	{
		for (const Zone &z : zones)
			if (x >= z.x && y >= z.y && x < z.x + z.w && y < z.y + z.h)
			{
				if (z.act == ACT_START) start();
				else if (z.act == ACT_REROLL) reroll();
				else if (z.act == ACT_CLOSE) quit = true;
				return;
			}
	}

	void addZone(int x, int y, int w, int h, int act) { zones.push_back({ x, y, w, h, act }); }

	void render(Canvas &top, Canvas &bot)
	{
		zones.clear();
		drawBase(top);
		drawBase(bot);
		drawStatus(top);
		drawSheet(top);
		if (games.empty()) drawEmpty(top);
		else drawTop(top);
		drawBottom(bot);
	}

private:
	void drawStatus(Canvas &c)
	{
		std::string clock = clockText((time_t)now, settings.twelveHour());
		std::string date = fmtDateShort((time_t)now);
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
			c.fill(c.U(81), c.U(28), c.U(20.0 * power.pct / 100.0), c.U(8), power.charging ? gAccent.base : C_INK);
		BoxStyle plate;
		plate.r = 4;
		plate.fill = C_BRAND;
		plate.shadow = 0x000000;
		plate.shadowDx = 2;
		plate.shadowDy = 2;
		plate.shadowA = 229;
		c.box(124, 19, 172, 26, plate);
		drawLogo(c, 132, 22);
		c.text("RANDOMIZER", 157, 24, 16, 0xfffaf0, true, 0, 0, 1.3f);
		int cw = c.text(clock, 622, 20, 20, C_INK, true, 2);
		c.text(date, 622 - cw - 8, 20, 19, C_INK, false, 2);
	}

	void drawWifi(Canvas &c, int x, int y, uint32_t col)
	{
		int cx = c.U(x + 10), cy = c.U(y + 12);
		for (int yy = c.U(y); yy <= cy; yy++)
			for (int xx = c.U(x); xx <= c.U(x + 21); xx++)
			{
				double dx = (xx - cx) / c.s, dy = (cy - yy) / c.s;
				double dd = std::sqrt(dx * dx + dy * dy);
				double ang = std::fabs(std::atan2(dx, dy));
				if (dy < 0.5 || ang > 1.05) continue;
				if (std::fabs(dd - 9.5) < 0.6 || std::fabs(dd - 6.5) < 0.6 || std::fabs(dd - 3.2) < 0.6)
					c.blend(xx, yy, col, 255);
			}
		c.fill(c.U(x + 8), c.U(y + 11), c.U(4), c.U(2), col);
	}

	void drawLogo(Canvas &c, int x, int y)
	{
		const uint32_t *px;
		int w, h;
		if (c.s > 1.3f) { px = AF_LOGO_16_PX; w = AF_LOGO_16_W; h = AF_LOGO_16_H; }
		else { px = AF_LOGO_1X_PX; w = AF_LOGO_1X_W; h = AF_LOGO_1X_H; }
		int X = c.U(x), Y = c.U(y);
		for (int yy = 0; yy < h; yy++)
			for (int xx = 0; xx < w; xx++)
			{
				uint32_t v = px[yy * w + xx];
				int a = (int)((v >> 24) & 255);
				if (a) c.blend(X + xx, Y + yy, v & 0xffffff, a);
			}
	}

	void drawSheet(Canvas &c) { c.box(18, 60, 604, 406, sheetStyle()); }

	void drawTop(Canvas &c)
	{
		const Game *g = game();
		if (!g) return;
		c.text("YOUR LIBRARY CHOSE", 42, 84, 13, C_MUTED, false, 0, 0, 1.4f, true);
		c.text("1 OF " + std::to_string(games.size()), 598, 84, 12, C_MUTED, true, 2, 0, 1.2f, true);
		drawArtwork(c, art);

		// system plate in the system's own accent
		std::string sysName = upperStr(g->sysName);
		int sw = (int)std::lround(c.textWidth(sysName, 12, true, 0.8f, false) / c.s) + 40;
		if (sw > 286) { sysName = ellipsize(c, sysName, 12, true, 0.8f, 246); sw = 286; }
		BoxStyle chip;
		chip.r = 8;
		chip.fill = g->accent;
		chip.border = mixColor(g->accent, 0.55, 0xffffff);
		chip.borderW = 2;
		chip.bandTop = mixColor(g->accent, 0.75, 0xffffff);
		chip.bandTopH = 6;
		chip.shadow = 0x000000;
		chip.shadowDx = 2;
		chip.shadowDy = 2;
		chip.shadowA = 200;
		c.box(310, 106, sw, 24, chip);
		c.text(sysName, 310 + 14, 111, 12, inkOnColour(g->accent), true, 0, 0, 0.8f);

		std::vector<std::string> lines = wrapText(c, g->title, 34, true, 0.3f, 288, 3);
		int ty = 144;
		for (const std::string &ln : lines) { c.text(ln, 310, ty, 34, C_NAVY, true, 0, 0, 0.3f); ty += 40; }

		std::string meta = g->genre;
		if (!g->year.empty()) meta = meta.empty() ? g->year : meta + " \xC2\xB7 " + g->year;
		if (meta.empty()) meta = g->sysName;
		c.text(ellipsize(c, upperStr(meta), 13, false, 1.2f, 286), 310, 278, 13, C_MUTED, false, 0, 0, 1.2f, true);
		c.fill(c.U(310), c.U(298), c.U(288), c.U(2), C_RULE, 150);

		auto stat = [&](int y, const std::string &label, const std::string &value) {
			c.text(label, 310, y, 11, C_MUTED, false, 0, 0, 1.4f, true);
			c.text(value, 598, y - 2, 22, C_INK, true, 2);
		};
		stat(310, "PLAYS", g->plays > 0 ? std::to_string(g->plays) : "NEVER");
		stat(348, "PLAY TIME", g->secs > 0 ? fmtDuration(g->secs) : "\xE2\x80\x94");
		stat(386, "LAST PLAYED", g->last > 0 ? upperStr(relDay((time_t)g->last)) : "\xE2\x80\x94");
	}

	void drawEmpty(Canvas &c)
	{
		if (!emptyDieTried)
		{
			emptyDieTried = true;
			emptyDie = loadPicture(assets + "/icons/randomizer.png");
		}
		if (emptyDie.ok())
		{
			int x = 240, y = 96, w = 160, h = 160;
			c.box(x - 3, y - 3, w + 6, h + 6, artFrameStyle());
			blitImage(c, emptyDie, x, y, w, h, false);
		}
		c.text("NO GAMES YET", 320, 286, 40, C_NAVY, true, 1, 0, 0.5f);
		c.text("THE RANDOMIZER READS THE SAME FOLDERS AS THE MENU.", 320, 340, 12, C_MUTED, false, 1, 0, 1.2f, true);
		c.text("COPY .NDS, .GBA, .SFC FILES TO ROMS/<SYSTEM> AND OPEN IT AGAIN.", 320, 364, 12, C_MUTED, false, 1, 0, 1.2f, true);
	}

	void drawBottom(Canvas &c)
	{
		c.text("RANDOMIZER", 18, 20, 16, C_INK, true, 0, 0, 1.4f, true);
		c.text(std::to_string(games.size()) + " " + (games.size() == 1 ? "GAME" : "GAMES") + " \xC2\xB7 " +
			       std::to_string(systemCount) + (systemCount == 1 ? " SYSTEM" : " SYSTEMS"),
		       622, 20, 12, C_MUTED, false, 2, 0, 1.2f, true);

		if (games.empty())
		{
			c.box(24, 52, 592, 74, bubbleStyle());
			c.text("NOTHING TO PICK", 320, 62, 26, C_INK, true, 1, 0, 0.52f);
			c.text("YOUR LIBRARY IS EMPTY", 320, 100, 14, C_MUTED, false, 1, 0, 1.3f, true);
			c.tri(304, 126, 336, 126, 320, 142, C_LINE);
			c.tri(308, 129, 332, 129, 320, 140, C_BUBBLE);
			drawOption(c, 24, 170, "B", "CLOSE", "BACK TO THE MENU", ACT_CLOSE);
			c.text("B CLOSE \xC2\xB7 OR TOUCH THE BUTTON", 320, 430, 11, C_MUTED, false, 1, 0, 1.2f, true);
			return;
		}

		const Game *g = game();
		std::string sub = upperStr(g->sysName) + " \xC2\xB7 " + (g->plays > 0 ? std::to_string(g->plays) + " PLAYS" : "NEVER PLAYED");
		c.box(24, 52, 592, 74, bubbleStyle());
		c.text(ellipsize(c, g->title, 26, true, 0.52f, 548), 320, 62, 26, C_INK, true, 1, 0, 0.52f);
		c.text(ellipsize(c, sub, 13, false, 1.2f, 548), 320, 100, 13, C_MUTED, false, 1, 0, 1.2f, true);
		c.tri(304, 126, 336, 126, 320, 142, C_LINE);
		c.tri(308, 129, 332, 129, 320, 140, C_BUBBLE);

		drawOption(c, 24, 154, "A", "START GAME", "PLAY IT NOW", ACT_START);
		drawOption(c, 24, 230, "X", "RANDOMIZE AGAIN", "PICK ANOTHER", ACT_REROLL);
		drawOption(c, 24, 306, "B", "CLOSE", "BACK TO THE MENU", ACT_CLOSE);

		if (!toast.empty()) { drawToast(c, true); return; }
		c.text(std::to_string(games.size()) + " GAMES \xC2\xB7 " + std::to_string(systemCount) + " SYSTEMS \xC2\xB7 " +
			       (settings.iconCart() ? "GAME ICON" : "BOX ART"),
		       320, 430, 11, C_MUTED, false, 1, 0, 1.2f, true);
	}

	void drawOption(Canvas &c, int x, int y, const std::string &key, const std::string &title,
			const std::string &sub, int act)
	{
		const int h = 64;
		BoxStyle b;	// every option is one of the menu's quiet plates; the key cap and label say what it does
		b.r = 14;
		b.fill = C_PANEL;
		b.border = C_LINE;
		b.borderW = 3;
		b.bandTop = C_PANEL_BAND;
		b.bandTopH = 7;
		b.insetBot = C_PANEL_LOW;
		b.insetBotH = 4;
		b.shadow = 0x000000;
		b.shadowDx = 3;
		b.shadowDy = 3;
		b.shadowA = 210;
		c.box(x, y, 592, h, b);

		BoxStyle k;
		k.r = 8;
		k.fill = C_KBD;
		k.border = C_EDGE;
		k.borderW = 2;
		c.box(x + 14, y + 12, 40, 40, k);
		c.text(key, x + 34, y + 19, 22, C_KBD_INK, true, 1);

		c.text(title, x + 68, y + 12, 25, C_INK, true, 0, 0, 0.2f);
		c.text(sub, x + 578, y + 40, 12, C_MUTED, false, 2, 0, 1.2f, true);
		addZone(x, y, 592, h, act);
	}

	void drawToast(Canvas &c, bool bottom)
	{
		if (toast.empty()) return;
		int y = bottom ? 398 : 430;
		int x = 24, w = 592, h = 46;
		BoxStyle b;
		b.r = 12;
		b.fill = C_FAV;
		b.border = C_RED;
		b.borderW = 3;
		b.bandTop = C_FAV_BAND;
		b.bandTopH = 6;
		b.shadow = 0x000000;
		b.shadowDx = 3;
		b.shadowDy = 3;
		b.shadowA = 220;
		c.box(x, y, w, h, b);
		c.text(ellipsize(c, toast, 16, true, 0.8f, 556), 320, y + 13, 16, C_INK, true, 1, 0, 0.8f);
	}
};

/* ---- the loop and its command line ------------------------------------------------------------------------------- */

#ifndef _WIN32
static volatile sig_atomic_t gQuit = 0;
static void onSignal(int) { gQuit = 1; }
#endif

struct Args {
	std::string root = "/data";
	std::string systems;
	std::string roms;
	std::string assets = "/usr/share/banana/rnds";
	std::string smoke;
	std::string size = "640x480";
	std::string accent;
	std::string theme;
	std::string setting;
	std::string press;
	std::string touch;
	std::string connector = "DSI-2";
	std::string launchOut;
	int seed = -1;
	int pick = -1;
	bool list = false;
	bool inputDebug = false;
};

static void usage()
{
	printf("banana-randomizer [--root /data] [--systems FILE] [--roms DIR] [--assets DIR]\n");
	printf("                  [--smoke PREFIX] [--size 640x480|1024x768] [--seed N] [--pick N]\n");
	printf("                  [--setting box|icon] [--press a,x,b] [--touch X,Y] [--launch-out FILE]\n");
	printf("                  [--accent yellow|blue|red|green|purple|pink] [--theme dark|light] [--connector DSI-2]\n");
	printf("                  [--list] [--input-debug]\n");
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
	for (int i = 0; i < c.w * c.h; i++)
	{
		uint32_t v = c.px[i];
		unsigned char rgb[3] = { (unsigned char)((v >> 16) & 255), (unsigned char)((v >> 8) & 255), (unsigned char)(v & 255) };
		fwrite(rgb, 1, 3, f);
	}
	fclose(f);
	return true;
}

static void pressName(App &app, const std::string &name)
{
	Key k = Key::UNKNOWN;
	if (name == "a") k = Key::A;
	else if (name == "b") k = Key::B;
	else if (name == "x") k = Key::X;
	else if (name == "y") k = Key::Y;
	else if (name == "start") k = Key::START;
	else if (name == "select") k = Key::SELECT;
	else if (name == "up") k = Key::UP;
	else if (name == "down") k = Key::DOWN;
	else if (name == "left") k = Key::LEFT;
	else if (name == "right") k = Key::RIGHT;
	if (k != Key::UNKNOWN) app.onKey(k);
}

int main(int argc, char **argv)
{
	Args a;
	for (int i = 1; i < argc; i++)
	{
		std::string k = argv[i];
		if (k == "--help" || k == "-h") { usage(); return 0; }
		else if (k == "--root" && i + 1 < argc) a.root = argv[++i];
		else if (k == "--systems" && i + 1 < argc) a.systems = argv[++i];
		else if (k == "--roms" && i + 1 < argc) a.roms = argv[++i];
		else if (k == "--assets" && i + 1 < argc) a.assets = argv[++i];
		else if (k == "--smoke" && i + 1 < argc) a.smoke = argv[++i];
		else if (k == "--size" && i + 1 < argc) a.size = argv[++i];
		else if (k == "--accent" && i + 1 < argc) a.accent = argv[++i];
		else if (k == "--theme" && i + 1 < argc) a.theme = argv[++i];
		else if (k == "--setting" && i + 1 < argc) a.setting = argv[++i];
		else if (k == "--press" && i + 1 < argc) a.press = argv[++i];
		else if (k == "--touch" && i + 1 < argc) a.touch = argv[++i];
		else if (k == "--connector" && i + 1 < argc) a.connector = argv[++i];
		else if (k == "--launch-out" && i + 1 < argc) a.launchOut = argv[++i];
		else if (k == "--seed" && i + 1 < argc) a.seed = atoi(argv[++i]);
		else if (k == "--pick" && i + 1 < argc) a.pick = atoi(argv[++i]);
		else if (k == "--list") a.list = true;
		else if (k == "--input-debug") a.inputDebug = true;
	}
	if (a.systems.empty()) a.systems = fileExists("/run/banana/systems.conf") ? "/run/banana/systems.conf" : "/etc/banana/systems.conf";
	if (a.roms.empty()) a.roms = a.root + "/roms";

	Settings st = loadSettings(a.root);
	if (!a.accent.empty()) st.accent = a.accent;
	if (!a.theme.empty()) st.theme = a.theme;
	if (!a.setting.empty()) st.dsCartArt = a.setting == "icon" ? "icon" : "box";
	applyAccent(st.accent);
	applyTheme(st.theme);
#ifndef _WIN32
	if (!st.timezone.empty()) { setenv("TZ", st.timezone.c_str(), 1); tzset(); }
#endif

	App app;
	app.quit = false;
	app.launchFile = a.launchOut.empty() ? "/run/banana/launch" : a.launchOut;
	app.debug = a.inputDebug;
	if (a.pick >= 0 && !a.smoke.empty())
	{
		// (an index for the previews: scan first, then land on it)
		app.init(st, a.systems, a.roms, a.root, a.assets, a.seed);
		if (a.pick < (int)app.games.size())
		{
			app.index = a.pick;
			app.art = chooseArt(app.games[(size_t)a.pick], app.settings, app.root, app.looks);
		}
	}
	else
	{
		app.init(st, a.systems, a.roms, a.root, a.assets, a.seed);
	}

	if (a.list)
	{
		for (size_t i = 0; i < app.games.size(); i++)
			printf("%d\t%s\t%s\t%s\n", (int)i, app.games[i].sysId.c_str(), app.games[i].title.c_str(), app.games[i].path.c_str());
		return 0;
	}

	if (!a.smoke.empty())
	{
		int w = 640, h = 480;
		parseSize(a.size, w, h);
		std::vector<uint32_t> tb((size_t)w * h), bb((size_t)w * h);
		Canvas top, bot;
		top.px = tb.data(); top.w = w; top.h = h; top.s = (float)w / 640.0f;
		bot.px = bb.data(); bot.w = w; bot.h = h; bot.s = top.s;
		app.refresh();
		app.render(top, bot);	// (the touch zones are placed by a frame)
		if (!a.press.empty())
		{
			size_t pos = 0;
			while (pos < a.press.size())
			{
				size_t comma = a.press.find(',', pos);
				pressName(app, a.press.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
				if (comma == std::string::npos) break;
				pos = comma + 1;
			}
		}
		if (!a.touch.empty())
		{
			size_t comma = a.touch.find(',');
			if (comma != std::string::npos)
				app.onTouch(atoi(a.touch.substr(0, comma).c_str()), atoi(a.touch.substr(comma + 1).c_str()));
		}
		app.render(top, bot);
		std::string p1 = a.smoke + "-top.ppm", p2 = a.smoke + "-bottom.ppm";		if (!writePPM(p1, top) || !writePPM(p2, bot)) { fprintf(stderr, "banana-randomizer: cannot write %s\n", a.smoke.c_str()); return 1; }
		printf("Smoke screenshots written to %s and %s\n", p1.c_str(), p2.c_str());
		if (!a.launchOut.empty() && fileExists(a.launchOut))
		{
			std::ifstream lf(a.launchOut.c_str());
			std::string line;
			std::getline(lf, line);
			printf("Launch file: %s\n", line.c_str());
		}
		return 0;
	}

#ifdef _WIN32
	fprintf(stderr, "banana-randomizer: this host build renders with --smoke PREFIX\n");
	return 2;
#else
	signal(SIGINT, onSignal);
	signal(SIGTERM, onSignal);
	signal(SIGPIPE, SIG_IGN);

	banana::Display disp;
	std::string err;
	if (!disp.open(a.connector, err)) { fprintf(stderr, "banana-randomizer: %s\n", err.c_str()); return 1; }
	const int W = disp.width(), H = disp.height();
	std::vector<uint32_t> tb((size_t)W * H), bb((size_t)W * H);
	Canvas top, bot;
	top.px = tb.data(); top.w = W; top.h = H; top.s = (float)W / 640.0f;
	bot.px = bb.data(); bot.w = W; bot.h = H; bot.s = top.s;

	banana::Input in;
	in.debug = a.inputDebug;
	in.open(W, H);

	app.refresh();
	bool dirty = true;
	long long lastRefresh = nowMs();
	while (!gQuit && !app.quit)
	{
		if (nowMs() - lastRefresh > 30000) { app.refresh(); lastRefresh = nowMs(); dirty = true; }
		if (app.tick()) dirty = true;
		if (dirty)
		{
			app.render(top, bot);
			const uint32_t *screens[2] = { top.px, bot.px };
			disp.present(screens);
			dirty = false;
		}
		std::vector<int> fds = in.fds();
		std::vector<struct pollfd> pfds;
		for (int fd : fds) pfds.push_back({ fd, POLLIN, 0 });
		int rc = poll(pfds.data(), pfds.size(), 500);
		if (rc < 0)
		{
			if (errno == EINTR) continue;
			break;
		}
		if (rc == 0) continue;
		float sc = (float)W / 640.0f;
		for (size_t i = 0; i < pfds.size() && i < fds.size(); i++)
		{
			if (!(pfds[i].revents & POLLIN)) continue;
			in.read(fds[i], [&](const InputEvent &ev) {
				if (ev.type == InputEvent::TOUCH_UP) app.onTouch((int)std::lround(ev.x / sc), (int)std::lround(ev.y / sc));
				else if (ev.type == InputEvent::PRESS) app.onKey(ev.key);
				dirty = true;
			});
		}
	}
	return 0;
#endif
}
