#include "kms.h"
#include "turn.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
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

	bool Display::makeBuf(Buf& b)
	{
		drm_mode_create_dumb c = {};
		c.width = SW; c.height = SH; c.bpp = 32;
		if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c)) return false;
		b.handle = c.handle; b.pitch = c.pitch; b.size = c.size;
		uint32_t handles[4] = { b.handle }, pitches[4] = { b.pitch }, offsets[4] = { 0 };
		if (drmModeAddFB2(fd, SW, SH, 0x34325258 /* XR24 */, handles, pitches, offsets, &b.fb, 0)) return false;
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

	// how the panel is mounted, from the connector's "panel orientation" (the device tree's rotation): the degrees
	// present() turns the menu by. banana.rotate=0|90|180|270 on the kernel command line overrides it, for a panel
	// that reports the wrong one.
	static int panelRotation(int fd, uint32_t conn)
	{
		int rot = 0;
		if (drmModeObjectProperties* props = drmModeObjectGetProperties(fd, conn, DRM_MODE_OBJECT_CONNECTOR))
		{
			for (uint32_t i = 0; i < props->count_props; i++)
			{
				drmModePropertyRes* p = drmModeGetProperty(fd, props->props[i]);
				if (!p) continue;
				if (!strcmp(p->name, "panel orientation"))
				{
					// 0 normal, 1 upside down, 2 left side up, 3 right side up (the device tree's rotation is counter
					// clockwise: rotation=<90> is RIGHT_UP, and the image then turns 90 clockwise)
					uint64_t v = props->prop_values[i];
					rot = v == 1 ? 180 : v == 2 ? 270 : v == 3 ? 90 : 0;
				}
				drmModeFreeProperty(p);
			}
			drmModeFreeObjectProperties(props);
		}
		if (FILE* f = fopen("/proc/cmdline", "r"))
		{
			char line[1024] = "";
			if (fgets(line, sizeof line, f))
				for (char* p = strstr(line, "banana.rotate="); p; p = strstr(p + 1, "banana.rotate="))
				{
					int v = atoi(p + strlen("banana.rotate="));
					if (v == 0 || v == 90 || v == 180 || v == 270) rot = v;
				}
			fclose(f);
		}
		return rot;
	}

	// the part of the composed screen that changed, into a scanout buffer the panel turns by rot: a turned rectangle
	// is written a panel row at a time (contiguous writes, strided reads of the cached screen). The quarter turns are
	// a transpose, done four pixels at a time -- see src/turn.h (and its unit test, tests/turn-test.cpp).
	void Display::turn(const uint32_t* src, Buf& bf, const Damage& d)
	{
		turnRect(src, bf.map, bf.pitch, W, H, rot, d.x0, d.y0, d.x1, d.y1);
	}

	// one panel: the connector named like top when it is connected, else any connected connector with a mode (the
	// type is only a preference here)
	bool Display::openSingle(const std::string& top, std::string& err)
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
		// the panel's connector type from the name ("DSI-1"; "Virtual-1" on QEMU's virtio GPU)
		std::string type = top.substr(0, top.find('-'));
		err = "no DRM device with a connected " + type + " panel";
		for (auto& path : cards)
		{
			int f = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
			if (f < 0) continue;
			drmModeRes* res = drmModeGetResources(f);
			if (!res) { ::close(f); continue; }
			struct Found { uint32_t conn, crtc; drmModeModeInfo mode; bool top; };
			std::vector<Found> found;
			// the connector to take: the named type first, else any connected one
			for (int pass = 0; pass < 2 && found.empty(); pass++)
			{
				for (int i = 0; i < res->count_connectors; i++)
				{
					drmModeConnector* c = drmModeGetConnector(f, res->connectors[i]);
					if (!c) continue;
					const char* tn = drmModeGetConnectorTypeName(c->connector_type);
					bool take = tn && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0;
					if (take && type != tn) take = pass == 1;
					if (take)
					{
						// the mode: the kernel command line's (video=<connector>:<mode>) first, then the panel's preferred one
						drmModeModeInfo mode = c->modes[0];
						for (int m = 0; m < c->count_modes; m++)
							if (c->modes[m].type & DRM_MODE_TYPE_PREFERRED) { mode = c->modes[m]; break; }
						for (int m = 0; m < c->count_modes; m++)
							if (c->modes[m].type & DRM_MODE_TYPE_USERDEF) { mode = c->modes[m]; break; }
						// a CRTC one of this connector's encoders can drive
						uint32_t crtc = 0;
						for (int e = 0; e < c->count_encoders && !crtc; e++)
						{
							drmModeEncoder* enc = drmModeGetEncoder(f, c->encoders[e]);
							if (!enc) continue;
							for (int k = 0; k < res->count_crtcs && !crtc; k++)
								if (enc->possible_crtcs & (1u << k)) crtc = res->crtcs[k];
							drmModeFreeEncoder(enc);
						}
						if (crtc)
						{
							char name[32];
							snprintf(name, sizeof name, "%s-%u", tn, c->connector_type_id);
							found.push_back({ c->connector_id, crtc, mode, top == name });
						}
					}
					drmModeFreeConnector(c);
					if (!found.empty()) break;
				}
			}
			drmModeFreeResources(res);
			if (found.empty()) { ::close(f); continue; }
			fd = f;
			count = 1;
			rot = panelRotation(f, found[0].conn);
			SW = found[0].mode.hdisplay; SH = found[0].mode.vdisplay;
			// a panel mounted turned: the menu draws the other way round (1240x1080 on a 1080x1240 panel)
			W = rot == 90 || rot == 270 ? SH : SW;
			H = rot == 90 || rot == 270 ? SW : SH;
			p[0] = Panel();
			p[0].conn = found[0].conn; p[0].crtc = found[0].crtc;
			memcpy(p[0].mode, &found[0].mode, sizeof p[0].mode);
			if (!makeBuf(p[0].buf[0]) || !makeBuf(p[0].buf[1])) { err = "can't make scanout buffers"; close(); return false; }
			restore();
			return true;
		}
		return false;
	}

	void Display::restore()
	{
		if (fd < 0) return;
		drmSetMaster(fd);
		for (int s = 0; s < count; s++)
			drmModeSetCrtc(fd, p[s].crtc, p[s].buf[p[s].front].fb, 0, 0, &p[s].conn, 1, (drmModeModeInfo*)p[s].mode);
		pending = 0;
	}

	void Display::close()
	{
		if (fd < 0) return;
		for (auto& pn : p) { freeBuf(pn.buf[0]); freeBuf(pn.buf[1]); }
		::close(fd);
		fd = -1;
		pending = 0;
		rot = 0; SW = SH = 0;
	}

	bool Display::present(const uint32_t* const screens[2], const Damage dmg[2])
	{
		if (fd < 0 || pending) return false;
		bool any = false;
		for (int s = 0; s < count; s++)
		{
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
			if (rot)
				turn(screens[s], bf, d);
			else
			{
				// only the columns that changed: the scanout buffer is uncached, every byte written costs
				size_t n = (size_t)(d.x1 - d.x0) * 4;
				for (int y = d.y0; y < d.y1; y++)
					memcpy(bf.map + (size_t)y * bf.pitch + (size_t)d.x0 * 4, screens[s] + (size_t)y * W + d.x0, n);
			}
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
}
