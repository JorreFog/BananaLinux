/* bovl.c - see bovl.h and COMPANION-SPEC.md section 10. MIT. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE	/* (O_CLOEXEC, ftruncate under a strict -std=c11) */
#endif
#include "bovl.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define LD(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define ST(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define LDSC(p) __atomic_load_n((p), __ATOMIC_SEQ_CST)
#define STSC(p, v) __atomic_store_n((p), (v), __ATOMIC_SEQ_CST)

/* the layout is part of the format: a change here is a new version (banana-ui builds this file as C++) */
#ifdef __cplusplus
#define BOVL_ASSERT static_assert
#else
#define BOVL_ASSERT _Static_assert
#endif
BOVL_ASSERT(offsetof(bovl_shm, painter_pid) == 64, "painter block");
BOVL_ASSERT(offsetof(bovl_shm, frame) == 120, "painter frame");
BOVL_ASSERT(offsetof(bovl_shm, presenter_pid) == 256, "presenter block");
BOVL_ASSERT(offsetof(bovl_shm, shown_frame) == 320, "presenter frame");
BOVL_ASSERT(offsetof(bovl_shm, touch) == 1024, "touch ring");
BOVL_ASSERT(offsetof(bovl_shm, slot) == 2048, "slots");
BOVL_ASSERT(offsetof(bovl_shm, hist) == 2208, "history");
BOVL_ASSERT(offsetof(bovl_shm, icon_px) == 4096, "icon");
BOVL_ASSERT(sizeof(bovl_shm) == BOVL_HEADER, "header size");

static uint32_t stride_of(uint32_t w) { return w * 2; }
static uint32_t slot_size_of(uint32_t w, uint32_t h) { return (uint32_t)(((uint64_t)stride_of(w) * h + 4095) & ~(uint64_t)4095); }

size_t bovl_size(uint32_t w, uint32_t h, uint32_t slots)
{
	return BOVL_HEADER + (size_t)slots * slot_size_of(w, h);
}

static int empty(bovl_rect r) { return r.w <= 0 || r.h <= 0; }

bovl_rect bovl_union(bovl_rect a, bovl_rect b)
{
	if (empty(a)) return b;
	if (empty(b)) return a;
	int32_t x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
	int32_t x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w, y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
	bovl_rect r = { x0, y0, x1 - x0, y1 - y0 };
	return r;
}

static bovl_rect clip(const bovl_shm* s, bovl_rect r)
{
	int32_t x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
	int32_t x1 = r.x + r.w, y1 = r.y + r.h;
	if (x1 > (int32_t)s->width) x1 = (int32_t)s->width;
	if (y1 > (int32_t)s->height) y1 = (int32_t)s->height;
	bovl_rect c = { x0, y0, x1 - x0, y1 - y0 };
	if (empty(c)) c.w = c.h = 0;
	return c;
}

static bovl_rect whole(const bovl_shm* s)
{
	bovl_rect r = { 0, 0, (int32_t)s->width, (int32_t)s->height };
	return r;
}

bovl_rect bovl_changed(const bovl_shm* s, uint64_t has, uint64_t want)
{
	bovl_rect u = { 0, 0, 0, 0 };
	if (want == has) return u;
	/* (the range stays clear of the entry the painter may be writing for the frame after `want`) */
	if (!has || want < has || want - has > BOVL_HIST - 2) return whole(s);
	for (uint64_t f = has + 1; f <= want; f++)
	{
		const bovl_hist_ent* e = &s->hist[f % BOVL_HIST];
		uint64_t f1 = LD(&e->frame);
		bovl_rect r;	/* (relaxed atomic reads: the painter may be rewriting it, which the frame check catches) */
		r.x = __atomic_load_n(&e->dirty.x, __ATOMIC_RELAXED);
		r.y = __atomic_load_n(&e->dirty.y, __ATOMIC_RELAXED);
		r.w = __atomic_load_n(&e->dirty.w, __ATOMIC_RELAXED);
		r.h = __atomic_load_n(&e->dirty.h, __ATOMIC_RELAXED);
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		if (f1 != f || LD(&e->frame) != f) return whole(s);	/* overwritten (or being written): all of it */
		u = bovl_union(u, r);
	}
	return clip(s, u);
}

/* ---- the creator ---- */

bovl_shm* bovl_create(const char* path, uint32_t w, uint32_t h, uint32_t slots)
{
	if (!w || !h || w > 4096 || h > 4096 || slots < 2 || slots > BOVL_MAX_SLOTS) return NULL;
	const size_t size = bovl_size(w, h, slots);
	unlink(path);
	int fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd < 0) return NULL;
	if (ftruncate(fd, (off_t)size) != 0) { close(fd); unlink(path); return NULL; }
	void* m = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (m == MAP_FAILED) { unlink(path); return NULL; }
	bovl_shm* s = (bovl_shm*)m;
	s->version = BOVL_VERSION;
	s->header_size = BOVL_HEADER;
	s->total_size = (uint32_t)size;
	s->slot_off = BOVL_HEADER;
	s->slot_size = slot_size_of(w, h);
	s->slots = slots;
	s->width = w;
	s->height = h;
	s->stride = stride_of(w);
	s->format = BOVL_FMT_RGB565;
	s->latest = BOVL_NONE;
	s->reading = BOVL_NONE;
	__atomic_thread_fence(__ATOMIC_RELEASE);
	memcpy(s->magic, BOVL_MAGIC, 8);	/* last: whoever sees the magic sees the rest */
	return s;
}

bovl_shm* bovl_open(const char* path)
{
	if (!path || !*path) return NULL;
	int fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) return NULL;
	struct stat st;
	if (fstat(fd, &st) != 0 || st.st_size < (off_t)BOVL_HEADER) { close(fd); return NULL; }
	void* m = mmap(NULL, (size_t)st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (m == MAP_FAILED) return NULL;
	bovl_shm* s = (bovl_shm*)m;
	int ok = memcmp(s->magic, BOVL_MAGIC, 8) == 0;
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	ok = ok && s->version == BOVL_VERSION && s->header_size == BOVL_HEADER && s->total_size == (uint64_t)st.st_size &&
		s->format == BOVL_FMT_RGB565 && s->slots >= 2 && s->slots <= BOVL_MAX_SLOTS && s->slot_off >= BOVL_HEADER &&
		s->width && s->height && s->stride >= s->width * 2 && (uint64_t)s->stride * s->height <= s->slot_size &&
		(uint64_t)s->slot_off + (uint64_t)s->slots * s->slot_size <= s->total_size;
	if (!ok) { munmap(m, (size_t)st.st_size); return NULL; }
	return s;
}

void bovl_unmap(bovl_shm* s)
{
	if (s) munmap(s, s->total_size);
}

/* ---- the painter ---- */

void bovl_painter_start(bovl_shm* s)
{
	s->painter_pid = (uint32_t)getpid();
	ST(&s->painter_state, BOVL_PAINTER_READY);
}

void bovl_painter_stop(bovl_shm* s)
{
	bovl_set_icon(s, NULL, s->icon_w, s->icon_h, 0, 0, s->icon_corner, s->icon_margin, s->icon_slop);
	ST(&s->painter_state, BOVL_PAINTER_NONE);
}

void bovl_set_icon(bovl_shm* s, const uint32_t* px, uint32_t w, uint32_t h, uint32_t on, uint32_t badge,
	uint32_t corner, uint32_t margin, uint32_t slop)
{
	if (w > BOVL_ICON_MAX) w = BOVL_ICON_MAX;
	if (h > BOVL_ICON_MAX) h = BOVL_ICON_MAX;
	__atomic_store_n(&s->icon_seq, s->icon_seq + 1, __ATOMIC_RELAXED);	/* odd: being written */
	__atomic_thread_fence(__ATOMIC_RELEASE);
	if (px) memcpy(s->icon_px, px, (size_t)w * h * 4);
	s->icon_w = w;
	s->icon_h = h;
	s->icon_on = on;
	s->badge = badge;
	s->icon_corner = corner;
	s->icon_margin = margin;
	s->icon_slop = slop;
	ST(&s->icon_seq, s->icon_seq + 1);
}

void bovl_request_open(bovl_shm* s, uint32_t open)
{
	s->req_open = open;
	ST(&s->req_seq, s->req_seq + 1);
}

int bovl_paint_begin(bovl_shm* s, bovl_rect dirty, bovl_rect* must)
{
	const uint32_t latest = LDSC(&s->latest), reading = LDSC(&s->reading);
	int best = -1;
	for (uint32_t i = 0; i < s->slots; i++)
	{
		if (i == latest || i == reading) continue;
		if (best < 0 || s->slot[i].frame > s->slot[best].frame) best = (int)i;	/* (the newest free one: least to redraw) */
	}
	if (best < 0) return -1;
	if (must) *must = clip(s, bovl_union(bovl_changed(s, s->slot[best].frame, s->frame), dirty));
	return best;
}

uint64_t bovl_paint_end(bovl_shm* s, int slot, bovl_rect dirty, uint32_t open_seq)
{
	const uint64_t n = s->frame + 1;
	dirty = clip(s, dirty);
	bovl_hist_ent* e = &s->hist[n % BOVL_HIST];
	ST(&e->frame, 0);	/* (a reader that sees 0, or another frame after reading the rectangle, takes everything) */
	__atomic_thread_fence(__ATOMIC_RELEASE);
	__atomic_store_n(&e->dirty.x, dirty.x, __ATOMIC_RELAXED);
	__atomic_store_n(&e->dirty.y, dirty.y, __ATOMIC_RELAXED);
	__atomic_store_n(&e->dirty.w, dirty.w, __ATOMIC_RELAXED);
	__atomic_store_n(&e->dirty.h, dirty.h, __ATOMIC_RELAXED);
	ST(&e->frame, n);
	bovl_slot* h = &s->slot[slot];
	h->frame = n;
	h->dirty = dirty;
	h->open_seq = open_seq;
	ST(&s->frame, n);
	STSC(&s->latest, (uint32_t)slot);
	return n;
}

uint64_t bovl_paint_xrgb(bovl_shm* s, const uint32_t* px, uint32_t stride, bovl_rect dirty, uint32_t open_seq)
{
	bovl_rect m;
	const int i = bovl_paint_begin(s, dirty, &m);
	if (i < 0) return 0;
	uint16_t* dst = bovl_pixels(s, (uint32_t)i);
	const uint32_t ds = s->stride / 2;
	for (int32_t y = m.y; y < m.y + m.h; y++)
	{
		const uint32_t* a = px + (size_t)y * stride + m.x;
		uint16_t* d = dst + (size_t)y * ds + m.x;
		for (int32_t x = 0; x < m.w; x++)
		{
			const uint32_t p = a[x];
			d[x] = (uint16_t)(((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F));
		}
	}
	return bovl_paint_end(s, i, dirty, open_seq);
}

int bovl_touch_next(const bovl_shm* s, uint32_t* tail, bovl_touch* out, uint32_t* lost)
{
	for (;;)
	{
		const uint32_t head = LD(&s->touch_head);
		if (*tail == head) return 0;
		if (head - *tail > BOVL_TOUCHES)
		{
			if (lost) *lost += head - *tail - BOVL_TOUCHES;
			*tail = head - BOVL_TOUCHES;
		}
		*out = s->touch[*tail % BOVL_TOUCHES];
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		if (LD(&s->touch_head) - *tail > BOVL_TOUCHES) continue;	/* overwritten while copied */
		(*tail)++;
		return 1;
	}
}

/* ---- the presenter ---- */

int bovl_attach(const char* path, bovl_shm** out)
{
	*out = NULL;
	if (!path) path = getenv(BOVL_ENV);
	bovl_shm* s = bovl_open(path);
	if (!s) return -1;
	*out = s;
	return 0;
}

void bovl_presenter_ready(bovl_shm* s, uint32_t ready)
{
	s->presenter_pid = (uint32_t)getpid();
	ST(&s->presenter_state, ready ? BOVL_PRESENTER_READY : BOVL_PRESENTER_NONE);
}

void bovl_detach(bovl_shm* s)
{
	if (!s) return;
	if (LD(&s->open)) bovl_set_open(s, 0);
	STSC(&s->reading, BOVL_NONE);
	ST(&s->presenter_state, BOVL_PRESENTER_NONE);
	bovl_unmap(s);
}

int bovl_icon_read(const bovl_shm* s, uint32_t* seq, bovl_icon* out)
{
	const uint32_t a = LD(&s->icon_seq);
	if (a == *seq) return 0;
	if (a & 1) return -1;
	out->on = s->icon_on;
	out->badge = s->badge;
	out->corner = s->icon_corner;
	out->margin = s->icon_margin;
	out->w = s->icon_w > BOVL_ICON_MAX ? BOVL_ICON_MAX : s->icon_w;
	out->h = s->icon_h > BOVL_ICON_MAX ? BOVL_ICON_MAX : s->icon_h;
	out->slop = s->icon_slop;
	memcpy(out->px, (const void*)s->icon_px, (size_t)out->w * out->h * 4);
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	if (LD(&s->icon_seq) != a) return -1;
	out->seq = a;
	*seq = a;
	return 1;
}

void bovl_set_open(bovl_shm* s, uint32_t open)
{
	ST(&s->open, open ? 1u : 0u);
	ST(&s->open_seq, s->open_seq + 1);
}

int bovl_take_request(bovl_shm* s, uint32_t* open)
{
	const uint32_t r = LD(&s->req_seq);
	if (r == s->req_seen) return 0;
	*open = s->req_open;
	s->req_seen = r;
	return 1;
}

void bovl_set_rects(bovl_shm* s, bovl_rect icon, bovl_rect view)
{
	s->icon_rect = icon;
	s->view_rect = view;
}

void bovl_touch_push(bovl_shm* s, uint32_t type, int32_t x, int32_t y, uint32_t ms)
{
	const uint32_t h = s->touch_head;
	bovl_touch* t = &s->touch[h % BOVL_TOUCHES];
	t->type = type;
	t->x = x;
	t->y = y;
	t->ms = ms;
	s->st_touches++;
	ST(&s->touch_head, h + 1);
}

int bovl_claim(bovl_shm* s, uint64_t newer_than, bovl_slot* hdr)
{
	const uint32_t r = LDSC(&s->latest);
	if (r >= s->slots) return -1;
	STSC(&s->reading, r);
	if (LDSC(&s->latest) != r) { ST(&s->reading, BOVL_NONE); return -1; }	/* moved on meanwhile: next time */
	*hdr = s->slot[r];
	if (hdr->frame <= newer_than) { ST(&s->reading, BOVL_NONE); return -1; }
	return (int)r;
}

void bovl_release(bovl_shm* s)
{
	ST(&s->reading, BOVL_NONE);
}
