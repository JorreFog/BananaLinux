/*
 * rpc-gpu-probe: which layer of the Retroid Pocket Classic's picture dies -- the Adreno, the panel, or the two
 * together?
 *
 * banana-ui draws with the CPU into dumb buffers (the panel alone, and that works); RetroArch draws with the GPU
 * into GBM buffers and scans those out (both at once, and that hangs the handheld). Nothing in between is tested
 * on its own, so this probes each layer by itself. Every step is printed and flushed before the thing that can hang
 * is tried: a hang leaves the last step reached on the console, and in the log tools/rpc-hang-watch.sh keeps on
 * the card.
 *
 *   rpc-gpu-probe info             the card, its connectors and modes, the EGL/GL driver Mesa picks
 *   rpc-gpu-probe offscreen [N]    GPU only: renderD128 + GBM + EGL + N draws, no modeset, no flip
 *   rpc-gpu-probe dumb [N]         panel only: dumb buffers, a real modeset, N flips, no GPU
 *   rpc-gpu-probe kms [N]          GPU + panel: the same mode, the GPU's own buffers scanned out, N frames
 *
 * banana-ui has to be stopped for kms/dumb (it holds the card; kill it and the panel keeps its last frame).
 * Build it with tools/rpc-probe-build.sh, run it on the handheld from /data. Exit 0 when every step passed.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm.h>

static int fd = -1;
static unsigned frames_done;
static volatile unsigned flips_seen;

/* the probe's place in the sequence: out before the step that may hang, flushed so a hang keeps it */
static void step(const char *what)
{
	printf("[probe] %s\n", what);
	fflush(stdout);
}

static void bail(const char *what)
{
	printf("[probe] FAILED %s\n", what);
	fflush(stdout);
	exit(1);
}

static void bail_errno(const char *what)
{
	printf("[probe] FAILED %s: %s\n", what, strerror(errno));
	fflush(stdout);
	exit(1);
}

static void egl_bail(const char *what)
{
	printf("[probe] FAILED %s: EGL error 0x%x\n", what, eglGetError());
	fflush(stdout);
	exit(1);
}

/* this libdrm's handler takes the fd as well (2.4.120 and later) */
static void on_flip(int f, unsigned frame, unsigned sec, unsigned usec, void *data)
{
	(void)f; (void)frame; (void)sec; (void)usec; (void)data;
	flips_seen++;
}

static void beat(unsigned i, const char *kind)
{
	if ((i & 31) == 0) {
		printf("[probe] %s: frame %u\n", kind, i);
		fflush(stdout);
	}
	frames_done = i + 1;
}

/* planes here: can any of them turn the picture 90 degrees in hardware? if the primary
 * plane can, RetroArch's window itself can be made landscape (1240x1080) and the panel's
 * turn becomes one property on the scanout -- no rotated menu, no integer scaling computed
 * against the wrong axes, no per-frame copy of anything. */
static void probe_planes(void)
{
	drmModeRes *res = drmModeGetResources(fd);
	drmModePlaneResPtr pr;
	if (!res) { printf("[probe] no resources\n"); return; }
	pr = drmModeGetPlaneResources(fd);
	if (!pr) { printf("[probe] no plane enumeration\n"); drmModeFreeResources(res); return; }
	for (uint32_t i = 0; i < pr->count_planes; i++) {
		drmModePlane *pl = drmModeGetPlane(fd, pr->planes[i]);
		drmModeObjectProperties *po;
		if (!pl) continue;
		printf("[probe] plane %u possible-crtcs 0x%x\n", pl->plane_id, pl->possible_crtcs);
		po = drmModeObjectGetProperties(fd, pr->planes[i], DRM_MODE_OBJECT_PLANE);
		if (po) {
			for (uint32_t j = 0; j < po->count_props; j++) {
				drmModePropertyRes *prop = drmModeGetProperty(fd, po->props[j]);
				if (!prop) continue;
				if (!strcmp(prop->name, "rotation") || !strcmp(prop->name, "type")) {
					printf("    %-9s flags 0x%x", prop->name, prop->flags);
					if (prop->flags & DRM_MODE_PROP_ENUM) {
						printf("  enum:");
						for (uint32_t k = 0; k < prop->count_enums; k++)
							printf(" %s=0x%x", prop->enums[k].name, (unsigned)prop->enums[k].value);
					}
					printf("\n");
				}
				drmModeFreeProperty(prop);
			}
			drmModeFreeObjectProperties(po);
		}
		drmModeFreePlane(pl);
	}
	drmModeFreePlaneResources(pr);
	drmModeFreeResources(res);
}

/* the one connected connector with a mode, and a CRTC that can drive it */
static drmModeConnector *find_panel(uint32_t *crtc_out)
{
	drmModeRes *res = drmModeGetResources(fd);
	if (!res)
		return NULL;
	drmModeConnector *conn = NULL;
	for (int i = 0; i < res->count_connectors && !conn; i++) {
		drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
		if (!c)
			continue;
		if (c->connection == DRM_MODE_CONNECTED && c->count_modes > 0)
			conn = c;
		else
			drmModeFreeConnector(c);
	}
	if (conn) {
		drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
		*crtc_out = enc ? (uint32_t)enc->crtc_id : (uint32_t)res->crtcs[0];
		if (enc)
			drmModeFreeEncoder(enc);
		printf("[probe] connector %u type %u, CRTC %u, mode %ux%u@%uHz, %u modes\n", conn->connector_id,
		       conn->connector_type, *crtc_out, conn->modes[0].hdisplay, conn->modes[0].vdisplay,
		       conn->modes[0].vrefresh, conn->count_modes);
	}
	drmModeFreeResources(res);
	return conn;
}

/* a shader and a triangle: work that really reaches the command processor, not just a clear */
static void draw(unsigned n)
{
	static const char *vsrc = "#version 100\nattribute vec2 position;void main(){gl_Position=vec4(position,0.,1.);}";
	static const char *fsrc = "#version 100\nprecision mediump float;uniform vec4 c;void main(){gl_FragColor=c;}";
	static GLuint prog;
	if (!prog) {
		GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(v, 1, &vsrc, NULL);
		glCompileShader(v);
		glShaderSource(f, 1, &fsrc, NULL);
		glCompileShader(f);
		prog = glCreateProgram();
		glAttachShader(prog, v);
		glAttachShader(prog, f);
		glBindAttribLocation(prog, 0, "position");
		glLinkProgram(prog);
		glUseProgram(prog);
		static const GLfloat tri[] = { 0.f, .8f, -.8f, -.8f, .8f, -.8f };
		GLuint b;
		glGenBuffers(1, &b);
		glBindBuffer(GL_ARRAY_BUFFER, b);
		glBufferData(GL_ARRAY_BUFFER, sizeof tri, tri, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
	}
	glUniform4f(glGetUniformLocation(prog, "c"), .1f, .2f, (n & 7) / 8.f, 1.f);
	glClearColor(.05f, .05f, .1f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

/* ------------------------------------------------------------------ EGL on a GBM device ---------------------------- */
struct gl_ctx {
	EGLDisplay dpy;
	EGLSurface surf;
	EGLContext ctx;
	struct gbm_surface *gs;
};

static void gl_init(struct gl_ctx *g, uint32_t w, uint32_t h, int scanout)
{
	step("gbm_create_device + eglGetPlatformDisplay(EGL_MESA_platform_gbm)");
	struct gbm_device *dev = gbm_create_device(fd);
	if (!dev)
		bail_errno("gbm_create_device");
	EGLDisplay dpy = EGL_NO_DISPLAY;
	const char *exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	PFNEGLGETPLATFORMDISPLAYEXTPROC get =
		exts && strstr(exts, "EGL_EXT_platform_base")
			? (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT") : NULL;
	if (get && exts && strstr(exts, "EGL_MESA_platform_gbm"))
		dpy = get(EGL_PLATFORM_GBM_MESA, dev, NULL);
	if (dpy == EGL_NO_DISPLAY)
		dpy = eglGetDisplay((EGLNativeDisplayType)dev);
	if (dpy == EGL_NO_DISPLAY)
		bail("no EGL display for this GBM device");

	/* the line that matters most: eglInitialize is where Mesa's loader dlopen()s its GL driver */
	step("eglInitialize (Mesa loads its GL driver here)");
	int mj = 0, mn = 0;
	if (!eglInitialize(dpy, &mj, &mn))
		egl_bail("eglInitialize -- no GL driver for this GPU");
	printf("[probe] EGL %d.%d, vendor %s, client APIs: %s\n", mj, mn, eglQueryString(dpy, EGL_VENDOR),
	       eglQueryString(dpy, EGL_CLIENT_APIS));
	if (!eglBindAPI(EGL_OPENGL_ES_API))
		egl_bail("eglBindAPI(GLES)");

	EGLConfig cfg;
	int n = 0;
	/* 8 bits a channel and no alpha asked for explicitly: without EGL_BUFFER_SIZE Mesa hands back its first
	   config, which here is the 10-bit XR30 one -- a format the panel cannot scan out */
	EGLint ta[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_COLOR_BUFFER_TYPE, EGL_RGB_BUFFER,
			EGL_BUFFER_SIZE, 24, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	if (!eglChooseConfig(dpy, ta, &cfg, 1, &n) || n < 1)
		egl_bail("eglChooseConfig");
	uint32_t use = scanout ? GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT : GBM_BO_USE_RENDERING;
	/* the surface's format has to be the one the config's visual is, or eglCreateWindowSurface answers
	   EGL_BAD_MATCH: ask the config, do not guess (XRGB8888 is only the fallback) */
	uint32_t visual = GBM_FORMAT_XRGB8888;
	eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_VISUAL_ID, (EGLint *)&visual);
	step("gbm_surface_create + eglCreateWindowSurface + eglMakeCurrent");
	printf("[probe] GBM format 0x%x (%s), %s\n", visual,
	       visual == GBM_FORMAT_XRGB8888 ? "XRGB8888" : visual == GBM_FORMAT_ARGB8888 ? "ARGB8888" : "other",
	       scanout ? "scanout + rendering" : "rendering only");
	g->gs = gbm_surface_create(dev, w, h, visual, use);
	if (!g->gs)
		bail_errno("gbm_surface_create");
	g->dpy = dpy;
	g->surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)g->gs, NULL);
	if (g->surf == EGL_NO_SURFACE)
		egl_bail("eglCreateWindowSurface");
	EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	g->ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ca);
	if (g->ctx == EGL_NO_CONTEXT)
		egl_bail("eglCreateContext");
	if (!eglMakeCurrent(dpy, g->surf, g->surf, g->ctx))
		egl_bail("eglMakeCurrent");
	printf("[probe] GL vendor %s, renderer %s, version %s\n", glGetString(GL_VENDOR),
	       glGetString(GL_RENDERER), glGetString(GL_VERSION));

	step("first draw + glFinish (the first submits to the GPU)");
	draw(0);
	glFinish();		/* the call that never comes back when the GPU has stopped answering */
	if (glGetError() != GL_NO_ERROR)
		printf("[probe] GL error on the first draw\n");
}

/* Would the hardware turn the whole scanout for us? If the primary plane accepts
 * ROTATE_90, RetroArch can be given a landscape window (1240x1080, what the user is
 * looking at) with video_rotation off, and the panel's turn becomes one property on the
 * commit: the menu, the integer scaling, the aspect fit and the overlays all come out
 * right on their own, because nothing is being fitted into the wrong-shaped window in the
 * first place. This asks the kernel, and leaves 8 seconds to look at the panel. */
static int probe_rotate(void)
{
	drmModeRes *res;
	drmModeConnector *conn;
	drmModePlaneResPtr pr;
	uint32_t crtc = 0, fb = 0, w, h, stride, handle, i, j;
	struct drm_mode_create_dumb cd;
	int rc = 1;

	step("drmSetMaster");
	if (drmSetMaster(fd) && errno != EACCES) { bail_errno("drmSetMaster"); }
	res = drmModeGetResources(fd);
	if (!res) return 1;
	for (i = 0; i < res->count_connectors && !conn; i++) {
		drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
		if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) conn = c;
		else if (c) drmModeFreeConnector(c);
	}
	if (!conn) { bail("no connected panel"); }
	w = conn->modes[0].hdisplay;
	h = conn->modes[0].vdisplay;
	{
		drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
		crtc = enc ? (uint32_t)enc->crtc_id : (uint32_t)res->crtcs[0];
		if (enc) drmModeFreeEncoder(enc);
	}
	memset(&cd, 0, sizeof cd);
	cd.width = w; cd.height = h; cd.bpp = 32;
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd)) bail_errno("create dumb");
	if (drmModeAddFB(fd, w, h, 24, 32, cd.pitch, cd.handle, &fb)) bail_errno("addfb");
	stride = cd.pitch; handle = cd.handle; (void)stride; (void)handle;
	if (drmModeSetCrtc(fd, crtc, fb, 0, 0, &conn->connector_id, 1, &conn->modes[0]))
		bail_errno("SetCrtc");
	printf("[probe] panel is up at %ux%u; looking for the primary plane's rotation\n", w, h);

	pr = drmModeGetPlaneResources(fd);
	if (pr) {
		for (i = 0; i < pr->count_planes; i++) {
			drmModePlane *pl = drmModeGetPlane(fd, pr->planes[i]);
			drmModeObjectProperties *po;
			if (!pl) continue;
			if (!(pl->possible_crtcs & (1 << (crtc ? 0 : 0)))) { /* checked below by index */ }
			po = drmModeObjectGetProperties(fd, pr->planes[i], DRM_MODE_OBJECT_PLANE);
			if (po) {
				for (j = 0; j < po->count_props; j++) {
					drmModePropertyRes *prop = drmModeGetProperty(fd, po->props[j]);
					if (!prop) continue;
					if (!strcmp(prop->name, "rotation")) {
						unsigned long back = 0;
						printf("[probe] plane %u has rotation (flags 0x%x); setting ROTATE_90=0x%x\n",
							pl->plane_id, prop->flags, (unsigned)DRM_MODE_ROTATE_90);
						errno = 0;
						if (drmModeObjectSetProperty(fd, pl->plane_id, DRM_MODE_OBJECT_PLANE,
								prop->prop_id, DRM_MODE_ROTATE_90) == 0)
						{
							printf("[probe] ACCEPTED - look at the panel for 8 s: the test picture should have turned\n");
							back = 1;
						}
						else
							printf("[probe] REFUSED: %s\n", strerror(errno));
						fflush(stdout);
						/* Put it back. A plane property outlives the process that set it (the state belongs to the
						   plane, not the fd), so leaving it turned would hand the next master -- banana-ui -- a plane
						   whose 1080x1240 framebuffer will not fit a 1080x1240 CRTC once it is turned: the commit
						   fails and the panel goes dark on a machine that is otherwise fine. A reboot clears it; a
						   probe has no business needing one. */
						if (back)
						{
							struct timespec t = { 8, 0 };
							nanosleep(&t, NULL);
							if (drmModeObjectSetProperty(fd, pl->plane_id, DRM_MODE_OBJECT_PLANE,
									prop->prop_id, DRM_MODE_ROTATE_0) == 0)
								printf("[probe] rotation restored to none\n");
							else
								printf("[probe] could not restore rotation (%s): reboot before handing the panel back\n",
									strerror(errno));
							fflush(stdout);
						}
						rc = 0;
					}
					drmModeFreeProperty(prop);
					if (rc == 0) break;
				}
				drmModeFreeObjectProperties(po);
			}
			if (rc == 0) { drmModeFreePlane(pl); break; }
			drmModeFreePlane(pl);
		}
		drmModeFreePlaneResources(pr);
	}
	drmModeFreeConnector(conn);
	drmModeFreeResources(res);
	return 0;
}

/* -------------------------------------------------------------------- the three probes ---------------------------- */
static void probe_offscreen(unsigned frames)
{
	struct gl_ctx g = { 0 };
	gl_init(&g, 512, 512, 0);
	step("offscreen loop: draw, swap, glFinish");
	for (unsigned i = 0; i < frames; i++) {
		draw(i);
		if (!eglSwapBuffers(g.dpy, g.surf))
			egl_bail("eglSwapBuffers");
		glFinish();
		beat(i, "offscreen");
	}
	printf("[probe] offscreen: %u frames through the GPU, no display involved\n", frames_done);
}

static void probe_dumb(unsigned frames)
{
	step("drmSetMaster (banana-ui must be stopped first)");
	if (drmSetMaster(fd) && errno != EACCES)
		bail_errno("drmSetMaster");
	uint32_t crtc = 0;
	drmModeConnector *conn = find_panel(&crtc);
	if (!conn)
		bail("no connected panel");
	uint32_t w = conn->modes[0].hdisplay, h = conn->modes[0].vdisplay;
	uint32_t fb[2] = { 0, 0 };
	step("two dumb buffers + AddFB (what banana-ui does, and it works)");
	for (int i = 0; i < 2; i++) {
		struct drm_mode_create_dumb cd = { .width = w, .height = h, .bpp = 32 };
		if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd))
			bail_errno("CREATE_DUMB");
		if (drmModeAddFB(fd, w, h, 24, 32, cd.pitch, cd.handle, &fb[i]))
			bail_errno("drmModeAddFB");
	}
	step("drmModeSetCrtc: the first modeset after the bootloader's splash");
	if (drmModeSetCrtc(fd, crtc, fb[0], 0, 0, &conn->connector_id, 1, &conn->modes[0]))
		bail_errno("drmModeSetCrtc");
	step("page flips, each waiting for its vblank");
	for (unsigned i = 1; i < frames; i++) {
		if (drmModePageFlip(fd, crtc, fb[i & 1], DRM_MODE_PAGE_FLIP_EVENT, NULL))
			bail_errno("drmModePageFlip");
		drmEventContext ev = { .version = 4, .page_flip_handler = on_flip };
		if (drmHandleEvent(fd, &ev))
			bail_errno("drmHandleEvent (no vblank came)");
		beat(i, "dumb");
	}
	printf("[probe] dumb: %u flips on the panel, GPU never touched\n", frames_done);
	drmModeFreeConnector(conn);
}

static void probe_kms(unsigned frames)
{
	step("drmSetMaster");
	if (drmSetMaster(fd) && errno != EACCES)
		bail_errno("drmSetMaster");
	uint32_t crtc = 0;
	drmModeConnector *conn = find_panel(&crtc);
	if (!conn)
		bail("no connected panel");
	uint32_t w = conn->modes[0].hdisplay, h = conn->modes[0].vdisplay;
	struct gl_ctx g = { 0 };
	gl_init(&g, w, h, 1);
	step("scan the GPU's own buffers out: AddFB of a GBM bo, SetCrtc, flips (what RetroArch does)");
	struct gbm_bo *prev = NULL;
	uint32_t prev_fb = 0;
	for (unsigned i = 0; i < frames; i++) {
		draw(i);
		if (!eglSwapBuffers(g.dpy, g.surf))
			egl_bail("eglSwapBuffers");
		struct gbm_bo *bo = gbm_surface_lock_front_buffer(g.gs);
		if (!bo)
			bail("gbm_surface_lock_front_buffer");
		uint32_t f = 0;
		if (drmModeAddFB(fd, w, h, 24, 32, gbm_bo_get_stride(bo), gbm_bo_get_handle(bo).u32, &f))
			bail_errno("drmModeAddFB on a GBM buffer");
		if (i == 0) {
			if (drmModeSetCrtc(fd, crtc, f, 0, 0, &conn->connector_id, 1, &conn->modes[0]))
				bail_errno("drmModeSetCrtc with a GPU buffer");
		} else {
			if (drmModePageFlip(fd, crtc, f, DRM_MODE_PAGE_FLIP_EVENT, NULL))
				bail_errno("drmModePageFlip");
			drmEventContext ev = { .version = 4, .page_flip_handler = on_flip };
			if (drmHandleEvent(fd, &ev))
				bail_errno("drmHandleEvent (no flip came)");
		}
		if (prev) {				/* the panel left it: give it back to the GPU */
			drmModeRmFB(fd, prev_fb);
			gbm_surface_release_buffer(g.gs, prev);
		}
		prev = bo;
		prev_fb = f;
		glFinish();
		beat(i, "kms");
	}
	printf("[probe] kms: %u GPU frames scanned out on the panel\n", frames_done);
	drmModeFreeConnector(conn);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);	/* a line out on its own: the last one survives the hang */
	const char *mode = argc > 1 ? argv[1] : "info";
	unsigned frames = argc > 2 ? (unsigned)atoi(argv[2]) : 120u;

	if (!strcmp(mode, "info")) {
		for (int i = 0; i < 4; i++) {
			char p[64];
			snprintf(p, sizeof p, "/dev/dri/card%d", i);
			int c = open(p, O_RDWR | O_CLOEXEC);
			if (c < 0)
				continue;
			drmVersion *v = drmGetVersion(c);
			printf("%s: driver %s\n", p, v && v->name ? v->name : "?");
			if (v)
				drmFreeVersion(v);
			close(c);
		}
		fd = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
		if (fd < 0)
			fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
		if (fd >= 0) {
			probe_planes();
			uint32_t crtc = 0;
			drmModeConnector *conn = find_panel(&crtc);
			if (conn) {
				for (int i = 0; i < conn->count_modes; i++)
					printf("  mode %d: %ux%u@%uHz type 0x%x flags 0x%x\n", i,
					       conn->modes[i].hdisplay, conn->modes[i].vdisplay,
					       conn->modes[i].vrefresh, conn->modes[i].type, conn->modes[i].flags);
				drmModeFreeConnector(conn);
			} else
				printf("no connected connector with a mode\n");
			close(fd);
		}
		return 0;
	}

	const char *path = !strcmp(mode, "offscreen") ? "/dev/dri/renderD128" : "/dev/dri/card1";
	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
		path = "/dev/dri/card0";
	}
	if (fd < 0)
		bail_errno("open a DRM device");
	printf("[probe] %s on %s, %u frames\n", mode, path, frames);
	fflush(stdout);

	if (!strcmp(mode, "offscreen"))
		probe_offscreen(frames);
	else if (!strcmp(mode, "dumb"))
		probe_dumb(frames);
	else if (!strcmp(mode, "kms"))
		probe_kms(frames);
	else if (!strcmp(mode, "rotate"))
		return probe_rotate();
	else {
		printf("usage: rpc-gpu-probe info|offscreen|dumb|kms [frames]|rotate\n");
		return 2;
	}
	printf("[probe] done\n");
	fflush(stdout);
	return 0;
}
