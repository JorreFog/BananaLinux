#include "kms.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
#include <xf86drm.h>
#include <xf86drmMode.h>

namespace banana
{
	static_assert(sizeof(drmModeModeInfo) == 68, "mode size");

	static void flipDone(int, unsigned, unsigned, unsigned, void* data)
	{
		int* pending = (int*)data;
		if (*pending > 0) (*pending)--;
	}

	// an object's property id by name (0: none)
	static uint32_t propId(int fd, uint32_t obj, uint32_t type, const char* name)
	{
		uint32_t id = 0;
		drmModeObjectProperties* props = drmModeObjectGetProperties(fd, obj, type);
		for (uint32_t i = 0; props && i < props->count_props && !id; i++)
			if (drmModePropertyRes* pr = drmModeGetProperty(fd, props->props[i]))
			{
				if (!strcmp(pr->name, name)) id = pr->prop_id;
				drmModeFreeProperty(pr);
			}
		drmModeFreeObjectProperties(props);
		return id;
	}

	// a CRTC's primary plane: the one its pictures are on now, else a primary one it can use (0: none)
	static uint32_t primaryPlane(int fd, uint32_t crtc)
	{
		drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
		drmModeRes* res = drmModeGetResources(fd);
		drmModePlaneRes* pr = drmModeGetPlaneResources(fd);
		int idx = -1;
		for (int i = 0; res && i < res->count_crtcs; i++) if (res->crtcs[i] == crtc) idx = i;
		uint32_t plane = 0, any = 0;
		for (uint32_t j = 0; pr && idx >= 0 && j < pr->count_planes; j++)
		{
			drmModePlane* pl = drmModeGetPlane(fd, pr->planes[j]);
			if (!pl) continue;
			bool mine = pl->possible_crtcs & (1u << idx), primary = false;
			if (drmModeObjectProperties* pp = drmModeObjectGetProperties(fd, pl->plane_id, DRM_MODE_OBJECT_PLANE))
			{
				for (uint32_t k = 0; k < pp->count_props; k++)
					if (drmModePropertyRes* q = drmModeGetProperty(fd, pp->props[k]))
					{
						if (!strcmp(q->name, "type") && pp->prop_values[k] == DRM_PLANE_TYPE_PRIMARY) primary = true;
						drmModeFreeProperty(q);
					}
				drmModeFreeObjectProperties(pp);
			}
			if (mine && primary && pl->crtc_id == crtc) plane = pl->plane_id;
			if (mine && primary && !any) any = pl->plane_id;
			drmModeFreePlane(pl);
		}
		if (pr) drmModeFreePlaneResources(pr);
		if (res) drmModeFreeResources(res);
		return plane ? plane : any;
	}

	bool Display::makeBuf(Buf& b)
	{
		drm_mode_create_dumb c = {};
		c.width = W; c.height = H; c.bpp = 32;
		if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c)) return false;
		b.handle = c.handle; b.pitch = c.pitch; b.size = c.size;
		uint32_t handles[4] = { b.handle }, pitches[4] = { b.pitch }, offsets[4] = { 0 };
		if (drmModeAddFB2(fd, W, H, 0x34325258 /* XR24 */, handles, pitches, offsets, &b.fb, 0)) return false;
		drm_mode_map_dumb m = {};
		m.handle = b.handle;
		if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m)) return false;
		void* p = mmap(nullptr, b.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
		if (p == MAP_FAILED) return false;
		b.map = (uint8_t*)p;
		memset(b.map, 0, b.size);
		return true;
	}

	void Display::freeBuf(Buf& b)
	{
		if (b.map) munmap(b.map, b.size);
		if (b.fb) drmModeRmFB(fd, b.fb);
		if (b.handle) { drm_mode_destroy_dumb d = {}; d.handle = b.handle; drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d); }
		b = Buf();
	}

	bool Display::open(const std::string& top, std::string& err)
	{
		close();
		std::vector<std::string> cards;
		if (DIR* d = opendir("/dev/dri"))
		{
			while (dirent* e = readdir(d))
				if (!strncmp(e->d_name, "card", 4)) cards.push_back(std::string("/dev/dri/") + e->d_name);
			closedir(d);
		}
		std::sort(cards.begin(), cards.end());
		// the panels' connector type from the top one's name ("DSI-2"; "Virtual-2" on QEMU's virtio GPU)
		std::string type = top.substr(0, top.find('-'));
		err = "no DRM device with two connected " + type + " panels";
		for (auto& path : cards)
		{
			int f = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
			if (f < 0) continue;
			drmModeRes* res = drmModeGetResources(f);
			if (!res) { ::close(f); continue; }
			struct Found { uint32_t conn, crtc; drmModeModeInfo mode; bool top; std::vector<drmModeModeInfo> modes; };
			std::vector<Found> found;
			std::vector<uint32_t> used;
			for (int i = 0; i < res->count_connectors && found.size() < 2; i++)
			{
				drmModeConnector* c = drmModeGetConnector(f, res->connectors[i]);
				if (!c) continue;
				const char* tn = drmModeGetConnectorTypeName(c->connector_type);
				if (tn && type == tn && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0)
				{
					// the mode: the kernel command line's (video=<connector>:<mode>) first, then the panel's preferred one
					drmModeModeInfo mode = c->modes[0];
					for (int m = 0; m < c->count_modes; m++)
						if (c->modes[m].type & DRM_MODE_TYPE_PREFERRED) { mode = c->modes[m]; break; }
					for (int m = 0; m < c->count_modes; m++)
						if (c->modes[m].type & DRM_MODE_TYPE_USERDEF) { mode = c->modes[m]; break; }
					std::vector<drmModeModeInfo> modes(c->modes, c->modes + c->count_modes);
					// a CRTC this connector's encoders can drive and the other panel doesn't have
					uint32_t crtc = 0;
					for (int e = 0; e < c->count_encoders && !crtc; e++)
					{
						drmModeEncoder* enc = drmModeGetEncoder(f, c->encoders[e]);
						if (!enc) continue;
						for (int k = 0; k < res->count_crtcs && !crtc; k++)
							if ((enc->possible_crtcs & (1u << k)) && std::find(used.begin(), used.end(), res->crtcs[k]) == used.end())
								crtc = res->crtcs[k];
						drmModeFreeEncoder(enc);
					}
					if (crtc)
					{
						char name[32];
						snprintf(name, sizeof name, "%s-%u", tn, c->connector_type_id);
						used.push_back(crtc);
						found.push_back({ c->connector_id, crtc, mode, top == name, modes });
					}
				}
				drmModeFreeConnector(c);
			}
			drmModeFreeResources(res);
			if (found.size() < 2) { ::close(f); continue; }
			// screen 0 is the top panel: the named connector, else the second one
			if (!found[0].top && !found[1].top) std::swap(found[0], found[1]);
			if (found[1].top) std::swap(found[0], found[1]);
			// the panels' preferred modes differ (a display that isn't the RG DS's pair): the bottom one takes the top's
			// size when it has it
			if (found[0].mode.hdisplay != found[1].mode.hdisplay || found[0].mode.vdisplay != found[1].mode.vdisplay)
				for (auto& m : found[1].modes)
					if (m.hdisplay == found[0].mode.hdisplay && m.vdisplay == found[0].mode.vdisplay) { found[1].mode = m; break; }
			if (found[0].mode.hdisplay != found[1].mode.hdisplay || found[0].mode.vdisplay != found[1].mode.vdisplay)
			{
				err = "the two panels have different modes";
				::close(f);
				continue;
			}
			fd = f;
			// the master: the boot's early light-up (--panels-on) may still hold it for a moment; it gives it up as soon
			// as its modesets are queued (a second at most)
			for (int t = 0; !handOver && drmSetMaster(fd) != 0 && t < 100; t++) usleep(10000);
			W = found[0].mode.hdisplay; H = found[0].mode.vdisplay;
			for (int s = 0; s < 2; s++)
			{
				p[s] = Panel();
				p[s].conn = found[s].conn; p[s].crtc = found[s].crtc;
				memcpy(p[s].mode, &found[s].mode, sizeof p[s].mode);
				if (!makeBuf(p[s].buf[0]) || !makeBuf(p[s].buf[1])) { err = "can't make scanout buffers"; close(); return false; }
			}
			// panels still dark (the boot): both lit at once; else (lit, or the driver won't) as always
			if (!lightBoth() && !handOver) restore();
			return true;
		}
		return false;
	}

	// Both panels lit at the same time. At the boot they are off, and lighting one sleeps ~200 ms in the panel driver
	// (generic_panel_prepare: 20 + 10 + 50 ms, its init sequence, 120 ms); drmModeSetCrtc did one after the other
	// (~0.65 s from banana-ui's start to its first frame on the RG DS, 20 ms on a restart with them lit). One nonblocking
	// atomic modeset per CRTC instead, so the two power-ups overlap (the display controller's own part is under its
	// lock), then the wait for both. false, with nothing changed, when both are lit already or the driver won't
	bool Display::lightBoth()
	{
		bool dark = false;
		for (int s = 0; s < 2; s++)
			if (drmModeCrtc* c = drmModeGetCrtc(fd, p[s].crtc)) { if (!c->mode_valid) dark = true; drmModeFreeCrtc(c); }
		if (!dark || drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1)) return false;
		drmModeAtomicReq* req[2] = {};
		uint32_t blob[2] = {};
		bool ok = true;
		for (int s = 0; s < 2 && ok; s++)
		{
			uint32_t plane = primaryPlane(fd, p[s].crtc);
			uint32_t modeId = propId(fd, p[s].crtc, DRM_MODE_OBJECT_CRTC, "MODE_ID");
			uint32_t active = propId(fd, p[s].crtc, DRM_MODE_OBJECT_CRTC, "ACTIVE");
			uint32_t connCrtc = propId(fd, p[s].conn, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
			const char* names[] = { "FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H", "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H" };
			const uint64_t values[] = { p[s].buf[p[s].front].fb, p[s].crtc, 0, 0, (uint64_t)W << 16, (uint64_t)H << 16, 0, 0, (uint64_t)W, (uint64_t)H };
			ok = plane && modeId && active && connCrtc && !drmModeCreatePropertyBlob(fd, p[s].mode, sizeof p[s].mode, &blob[s]);
			req[s] = drmModeAtomicAlloc();
			ok = ok && req[s] && drmModeAtomicAddProperty(req[s], p[s].crtc, modeId, blob[s]) >= 0 &&
				drmModeAtomicAddProperty(req[s], p[s].crtc, active, 1) >= 0 && drmModeAtomicAddProperty(req[s], p[s].conn, connCrtc, p[s].crtc) >= 0;
			for (int k = 0; k < 10 && ok; k++)
			{
				uint32_t id = propId(fd, plane, DRM_MODE_OBJECT_PLANE, names[k]);
				ok = id && drmModeAtomicAddProperty(req[s], plane, id, values[k]) >= 0;
			}
		}
		// (the flip events' counter is a member, not a local: one that comes after the second's wait below would
		// otherwise be written into a stack frame that is gone, review 0.4.3)
		int& waiting = lightWaiting;
		waiting = 0;
		int lit = 0;
		for (int s = 0; s < 2 && ok; s++)
			if (drmModeAtomicCommit(fd, req[s], DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, &lightWaiting) == 0)
				{ waiting++; lit++; }
		// --panels-on: the master given up now (the menu can take the display while the panels power up), the wait
		// longer (the RG DS Plus's panels take ~1.2 s): its fd closed on the way, the scanout's black buffers go with it
		// and the panels stay lit for the menu
		if (handOver) drmDropMaster(fd);
		// both done (a flip event each), a second at most (3 for --panels-on)
		for (int t = 0; t < (handOver ? 300 : 100) && waiting > 0; t++)
		{
			pollfd pf = { fd, POLLIN, 0 };
			if (poll(&pf, 1, 10) > 0)
			{
				drmEventContext ev = {};
				ev.version = 2;
				ev.page_flip_handler = flipDone;
				drmHandleEvent(fd, &ev);
			}
		}
		for (int s = 0; s < 2; s++)
		{
			if (req[s]) drmModeAtomicFree(req[s]);
			if (blob[s]) drmModeDestroyPropertyBlob(fd, blob[s]);
		}
		return lit == 2 && waiting == 0;
	}

	void Display::restore()
	{
		if (fd < 0) return;
		drmSetMaster(fd);
		for (int s = 0; s < 2; s++)
			if (s != leased) drmModeSetCrtc(fd, p[s].crtc, p[s].buf[p[s].front].fb, 0, 0, &p[s].conn, 1, (drmModeModeInfo*)p[s].mode);
		pending = 0;
	}

	// a CRTC's property by name: its id, and its value (0, 0 when there is none)
	static uint32_t crtcProp(int fd, uint32_t crtc, const char* name, uint64_t* value = nullptr)
	{
		uint32_t id = 0;
		drmModeObjectProperties* props = drmModeObjectGetProperties(fd, crtc, DRM_MODE_OBJECT_CRTC);
		for (uint32_t i = 0; props && i < props->count_props && !id; i++)
		{
			drmModePropertyRes* pr = drmModeGetProperty(fd, props->props[i]);
			if (pr && !strcmp(pr->name, name)) { id = pr->prop_id; if (value) *value = props->prop_values[i]; }
			drmModeFreeProperty(pr);
		}
		drmModeFreeObjectProperties(props);
		return id;
	}

	std::string Display::setColors(const ScreenColors& c)
	{
		if (fd < 0) return "no display";
		int target = c.neutral() ? -1 : c.screen;
		// the other screen's table off first: the kernel refuses a second one
		for (int s = 0; s < 2; s++)
		{
			if (s == target) continue;
			uint64_t cur = 0;
			uint32_t id = crtcProp(fd, p[s].crtc, "GAMMA_LUT", &cur);
			if (id && cur && drmModeObjectSetProperty(fd, p[s].crtc, DRM_MODE_OBJECT_CRTC, id, 0) != 0)
				return std::string("clearing screen ") + std::to_string(s) + "'s table: " + strerror(errno);
		}
		if (target < 0) return "";
		uint64_t size = 0;
		uint32_t id = crtcProp(fd, p[target].crtc, "GAMMA_LUT", nullptr);
		crtcProp(fd, p[target].crtc, "GAMMA_LUT_SIZE", &size);
		if (!id || size < 2) return "the screen has no colour table";
		std::vector<drm_color_lut> lut(size);
		for (uint64_t i = 0; i < size; i++)
		{
			float in = (float)i / (float)(size - 1);
			lut[i].red = (uint16_t)std::lround(c.apply(0, in) * 65535);
			lut[i].green = (uint16_t)std::lround(c.apply(1, in) * 65535);
			lut[i].blue = (uint16_t)std::lround(c.apply(2, in) * 65535);
			lut[i].reserved = 0;
		}
		// the same table already there: left alone (setting one switches the table off for a frame: a flash on every
		// return from a game)
		{
			uint64_t cur = 0;
			crtcProp(fd, p[target].crtc, "GAMMA_LUT", &cur);
			drmModePropertyBlobRes* b = cur ? drmModeGetPropertyBlob(fd, (uint32_t)cur) : nullptr;
			bool same = b && b->length == lut.size() * sizeof(drm_color_lut) && !memcmp(b->data, lut.data(), b->length);
			drmModeFreePropertyBlob(b);
			if (same) return "";
		}
		uint32_t blob = 0;
		if (drmModeCreatePropertyBlob(fd, lut.data(), lut.size() * sizeof(drm_color_lut), &blob) != 0)
			return std::string("the table: ") + strerror(errno);
		int r = drmModeObjectSetProperty(fd, p[target].crtc, DRM_MODE_OBJECT_CRTC, id, blob);
		int e = errno;
		drmModeDestroyPropertyBlob(fd, blob);	// (the CRTC keeps its own reference)
		return r == 0 ? "" : std::string("setting the table: ") + strerror(e);
	}

	void Display::close()
	{
		if (fd < 0) return;
		for (auto& pn : p) { freeBuf(pn.buf[0]); freeBuf(pn.buf[1]); }
		::close(fd);	// (a lease ends with its lessor's master)
		fd = -1;
		pending = 0;
		leased = -1;
	}

	bool Display::present(const uint32_t* const screens[2], const Damage dmg[2])
	{
		if (fd < 0 || pending) return false;
		bool any = false;
		for (int s = 0; s < 2; s++)
		{
			if (s == leased) continue;	// a game draws there
			Panel& pn = p[s];
			// the hidden buffer missed what changed since it was shown: this frame's part and the last frame's
			int back = pn.front ^ 1;
			Damage d = dmg[s];
			d.add(pn.missed[back]);
			pn.missed[back] = Damage();
			d.x0 = std::max(d.x0, 0); d.y0 = std::max(d.y0, 0);
			d.x1 = std::min(d.x1, W); d.y1 = std::min(d.y1, H);
			if (d.empty()) continue;
			Buf& bf = pn.buf[back];
			// only the columns that changed: the scanout buffer is uncached, every byte written costs
			size_t n = (size_t)(d.x1 - d.x0) * 4;
			for (int y = d.y0; y < d.y1; y++)
				memcpy(bf.map + (size_t)y * bf.pitch + (size_t)d.x0 * 4, screens[s] + (size_t)y * W + d.x0, n);
			pn.missed[pn.front] = dmg[s];	// the buffer going hidden misses this frame
			if (drmModePageFlip(fd, pn.crtc, bf.fb, DRM_MODE_PAGE_FLIP_EVENT, &pending) == 0)
			{
				pending++;
				pn.front = back;
				any = true;
			}
			else
			{
				// no flip (lost master, a modeset in the way): put the buffer up directly
				drmModeSetCrtc(fd, pn.crtc, bf.fb, 0, 0, &pn.conn, 1, (drmModeModeInfo*)pn.mode);
				pn.front = back;
			}
		}
		return any;
	}

	void Display::handleEvents()
	{
		if (fd < 0) return;
		drmEventContext ev = {};
		ev.version = 2;
		ev.page_flip_handler = flipDone;
		drmHandleEvent(fd, &ev);
	}

	// the display controller's interrupts so far (/proc/interrupts: fe040000.vop, all CPUs), -1 if not found
	static long long vopIrqs()
	{
		FILE* f = fopen("/proc/interrupts", "r");
		if (!f) return -1;
		char line[512];
		long long n = -1;
		while (fgets(line, sizeof line, f))
		{
			if (!strstr(line, "fe040000.vop")) continue;
			char* q = strchr(line, ':');
			if (!q) break;
			n = 0;
			for (q++;;) { char* e; long long v = strtoll(q, &e, 10); if (e == q) break; n += v; q = e; }
			break;
		}
		fclose(f);
		return n;
	}

	long long Display::vopIrqCount() { return vopIrqs(); }

	long Display::stormCheck()
	{
		if (fd < 0 || leased >= 0) return -1;
		long long a = vopIrqs();
		if (a < 0) return -1;
		usleep(200000);
		long long rate = (vopIrqs() - a) * 5;
		static const long MIN = getenv("BANANA_STORM_MIN") ? atol(getenv("BANANA_STORM_MIN")) : 20000;	// (tests: 0 forces it)
		if (rate < MIN) return -1;	// calm: two panels' vblanks and flips are ~120 a second
		for (int s = 0; s < 2; s++) drmModeSetCrtc(fd, p[s].crtc, 0, 0, 0, nullptr, 0, nullptr);
		usleep(50000);
		restore();
		a = vopIrqs();
		usleep(200000);
		long after = (long)((vopIrqs() - a) * 5);
		fprintf(stderr, "banana-ui: display interrupt storm (%lld a second): both panels off and on, %ld a second now\n", rate, after);
		return after;
	}

	int Display::lease(int s)
	{
		if (fd < 0 || s < 0 || s > 1) return -1;
		uint32_t plane = primaryPlane(fd, p[s].crtc);
		if (!plane) return -1;
		uint32_t objs[3] = { p[s].conn, p[s].crtc, plane }, lessee = 0;
		int l = drmModeCreateLease(fd, objs, 3, 0, &lessee);	// (no CLOEXEC: the game's process inherits it)
		if (l >= 0) leased = s;
		return l;
	}
}
