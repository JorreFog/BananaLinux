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
		err = "no DRM device with two connected DSI panels";
		for (auto& path : cards)
		{
			int f = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
			if (f < 0) continue;
			drmModeRes* res = drmModeGetResources(f);
			if (!res) { ::close(f); continue; }
			struct Found { uint32_t conn, crtc; drmModeModeInfo mode; bool top; };
			std::vector<Found> found;
			std::vector<uint32_t> used;
			for (int i = 0; i < res->count_connectors && found.size() < 2; i++)
			{
				drmModeConnector* c = drmModeGetConnector(f, res->connectors[i]);
				if (!c) continue;
				if (c->connector_type == DRM_MODE_CONNECTOR_DSI && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0)
				{
					drmModeModeInfo mode = c->modes[0];
					for (int m = 0; m < c->count_modes; m++)
						if (c->modes[m].type & DRM_MODE_TYPE_PREFERRED) { mode = c->modes[m]; break; }
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
						snprintf(name, sizeof name, "DSI-%u", c->connector_type_id);
						used.push_back(crtc);
						found.push_back({ c->connector_id, crtc, mode, top == name });
					}
				}
				drmModeFreeConnector(c);
			}
			drmModeFreeResources(res);
			if (found.size() < 2) { ::close(f); continue; }
			// screen 0 is the top panel: the named connector, else the second one
			if (!found[0].top && !found[1].top) std::swap(found[0], found[1]);
			if (found[1].top) std::swap(found[0], found[1]);
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
		for (auto& pn : p)
			drmModeSetCrtc(fd, pn.crtc, pn.buf[pn.front].fb, 0, 0, &pn.conn, 1, (drmModeModeInfo*)pn.mode);
		pending = 0;
	}

	void Display::close()
	{
		if (fd < 0) return;
		for (auto& pn : p) { freeBuf(pn.buf[0]); freeBuf(pn.buf[1]); }
		::close(fd);
		fd = -1;
		pending = 0;
	}

	bool Display::present(const uint32_t* const screens[2], const int y0[2], const int y1[2])
	{
		if (fd < 0 || pending) return false;
		bool any = false;
		for (int s = 0; s < 2; s++)
		{
			Panel& pn = p[s];
			// the hidden buffer missed what changed since it was shown: this frame's rows and the last frame's
			int back = pn.front ^ 1;
			int a = y0[s], b = y1[s];
			if (pn.dmg0[back] < pn.dmg1[back])
			{
				if (a >= b) { a = pn.dmg0[back]; b = pn.dmg1[back]; }
				else { a = std::min(a, pn.dmg0[back]); b = std::max(b, pn.dmg1[back]); }
			}
			pn.dmg0[back] = 0; pn.dmg1[back] = 0;
			if (a >= b) continue;
			a = std::max(a, 0); b = std::min(b, H);
			Buf& bf = pn.buf[back];
			for (int y = a; y < b; y++)
				memcpy(bf.map + (size_t)y * bf.pitch, screens[s] + (size_t)y * W, (size_t)W * 4);
			pn.dmg0[pn.front] = y0[s]; pn.dmg1[pn.front] = y1[s];	// the buffer going hidden misses this frame
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
