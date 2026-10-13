#include "kms.h"
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
			W = found[0].mode.hdisplay; H = found[0].mode.vdisplay;
			for (int s = 0; s < 2; s++)
			{
				p[s] = Panel();
				p[s].conn = found[s].conn; p[s].crtc = found[s].crtc;
				memcpy(p[s].mode, &found[s].mode, sizeof p[s].mode);
				if (!makeBuf(p[s].buf[0]) || !makeBuf(p[s].buf[1])) { err = "can't make scanout buffers"; close(); return false; }
			}
			restore();
			return true;
		}
		return false;
	}

	void Display::restore()
	{
		if (fd < 0) return;
		drmSetMaster(fd);
		for (int s = 0; s < 2; s++)
			if (s != leased) drmModeSetCrtc(fd, p[s].crtc, p[s].buf[p[s].front].fb, 0, 0, &p[s].conn, 1, (drmModeModeInfo*)p[s].mode);
		pending = 0;
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

	int Display::lease(int s)
	{
		if (fd < 0 || s < 0 || s > 1) return -1;
		drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
		drmModeRes* res = drmModeGetResources(fd);
		drmModePlaneRes* pr = drmModeGetPlaneResources(fd);
		int idx = -1;
		for (int i = 0; res && i < res->count_crtcs; i++) if (res->crtcs[i] == p[s].crtc) idx = i;
		// the CRTC's primary plane: the one its pictures are on now, else a primary one it can use
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
			if (mine && primary && pl->crtc_id == p[s].crtc) plane = pl->plane_id;
			if (mine && primary && !any) any = pl->plane_id;
			drmModeFreePlane(pl);
		}
		if (pr) drmModeFreePlaneResources(pr);
		if (res) drmModeFreeResources(res);
		if (!plane) plane = any;
		if (!plane) return -1;
		uint32_t objs[3] = { p[s].conn, p[s].crtc, plane }, lessee = 0;
		int l = drmModeCreateLease(fd, objs, 3, 0, &lessee);	// (no CLOEXEC: the game's process inherits it)
		if (l >= 0) leased = s;
		return l;
	}
}
