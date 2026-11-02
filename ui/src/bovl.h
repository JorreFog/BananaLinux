/* bovl.h - BananaOS companion overlay: a picture one program paints and another shows on a panel in place of a game's
 * screen, a small icon (with a badge) the shower draws over the game, and the touches that come back
 * (COMPANION-SPEC.md section 10; overlay format version 1).
 *
 * One file, /run/banana/companion-overlay.shm, created by banana-ui before it starts the game, named in the game's
 * environment as BANANA_COMPANION_OVERLAY. Two roles:
 * - the PAINTER draws the picture (RGB565, the panel's size) into one of the file's slots, and the icon picture;
 *   banana-ui for the DS Pokemon companion (BananaDS shows it), BananaFront for its bottom-panel menu (KAN-148,
 *   banana-ui shows it).
 * - the PRESENTER puts the newest slot on the panel while the overlay is open, draws the icon over the game's picture
 *   while it is closed, and owns the touchscreen: a tap on the icon opens or closes, touches while open are queued
 *   for the painter, everything else goes to the game.
 *
 * The slots are a triple-buffer handshake without locks: the painter never writes the newest slot nor the one the
 * presenter has claimed, the presenter claims the newest and checks it still is (both sides' stores and loads of
 * `latest`/`reading` are sequentially consistent). Each published frame carries the rectangle that changed since the
 * frame before; a history of the last BOVL_HIST rectangles lets both sides copy only what changed since the frame a
 * buffer of theirs holds.
 *
 * The file is separate from companion.shm (bcomp.h) on purpose: bcomp producers check that file's exact size, and
 * the picture (1.5 MiB a slot at 1024x768) does not fit its 1 MiB.
 *
 * MIT licence (as banana-ui). C99 + GCC/Clang __atomic builtins. No allocation, no threads.
 */
#ifndef BOVL_H
#define BOVL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOVL_MAGIC "BNOVL\0\0\1"
#define BOVL_VERSION 1u
#define BOVL_ENV "BANANA_COMPANION_OVERLAY"
#define BOVL_DEFAULT_PATH "/run/banana/companion-overlay.shm"

#define BOVL_HEADER 65536u   /* the header page and the icon picture; the slots follow */
#define BOVL_MAX_SLOTS 4
#define BOVL_ICON_MAX 120    /* the icon picture is at most 120 x 120 */
#define BOVL_TOUCHES 64      /* the touch ring */
#define BOVL_HIST 8          /* dirty rectangles kept: frame f's at hist[f % BOVL_HIST] */
#define BOVL_NONE 0xFFFFFFFFu

enum { BOVL_FMT_RGB565 = 1 };                       /* little-endian RGB565 = DRM_FORMAT_RGB565 */
enum { BOVL_TOUCH_DOWN = 1, BOVL_TOUCH_MOVE = 2, BOVL_TOUCH_UP = 3 };
enum { BOVL_CORNER_BR, BOVL_CORNER_BL, BOVL_CORNER_TR, BOVL_CORNER_TL };
enum { BOVL_PAINTER_NONE, BOVL_PAINTER_READY };       /* painter_state */
enum { BOVL_PRESENTER_NONE, BOVL_PRESENTER_READY };   /* presenter_state: it can show the picture and route touch */

typedef struct { int32_t x, y, w, h; } bovl_rect;
typedef struct { uint32_t type; int32_t x, y; uint32_t ms; } bovl_touch;   /* picture coordinates; ms: CLOCK_MONOTONIC */
typedef struct { uint64_t frame; bovl_rect dirty; } bovl_hist_ent;           /* 24 */
typedef struct {
	uint64_t frame;          /* the frame in the slot (0: never painted) */
	bovl_rect dirty;         /* what changed since frame - 1 (the whole picture when unsure) */
	uint32_t open_seq;       /* the presenter's open_seq it was painted for */
	uint32_t reserved[3];
} bovl_slot;                 /* 40 */

typedef struct {
	/* 0: header (the creator) */
	char magic[8];
	uint32_t version, header_size, total_size, slot_off, slot_size, slots;
	uint32_t width, height, stride, format;   /* the picture: stride in bytes */
	uint8_t reserved_h[16];
	/* 64: the painter */
	uint32_t painter_pid, painter_state;
	uint32_t latest;                          /* the newest complete slot (BOVL_NONE: none yet) */
	uint32_t req_seq, req_open;               /* the painter asks to open (1) or close (0): req_open, then req_seq++ */
	uint32_t icon_seq;                        /* seqlock over icon_* and icon_px (odd while the painter writes) */
	uint32_t icon_on, badge;                  /* the icon shown over the game; the badge's count (0: none) */
	uint32_t icon_corner, icon_margin;        /* where: a corner of the game's picture, this far in (picture px) */
	uint32_t icon_w, icon_h, icon_slop;       /* the picture's size; taps reach icon_slop px around it (closed) */
	uint32_t reserved_p0;
	uint64_t frame;                           /* the newest frame published */
	uint8_t reserved_p[256 - 64 - 64];
	/* 256: the presenter */
	uint32_t presenter_pid, presenter_state;
	uint32_t open, open_seq;                  /* open: the picture shown instead of the game's screen; seq bumped at each change */
	uint32_t reading;                         /* the slot being copied (BOVL_NONE) */
	uint32_t touch_head;                      /* touches written so far; the newest at touch[(touch_head - 1) % BOVL_TOUCHES] */
	bovl_rect icon_rect;                      /* where the icon is drawn (picture coordinates; w 0: not shown) */
	bovl_rect view_rect;                      /* where the game's picture is on the panel (picture coordinates) */
	uint32_t req_seen, reserved_s0;           /* the painter's last request taken */
	uint64_t shown_frame;                     /* the painter's frame on the panel now (0: none) */
	uint64_t st_frames, st_copies, st_bytes, st_copy_ns, st_icon, st_icon_ns, st_touches;   /* the presenter's counters */
	uint8_t reserved_s[1024 - 256 - 128];
	/* 1024: the touch ring (presenter -> painter) */
	bovl_touch touch[BOVL_TOUCHES];
	/* 2048: the slots' headers, and the last frames' dirty rectangles (frame f's at hist[f % BOVL_HIST]) */
	bovl_slot slot[BOVL_MAX_SLOTS];
	bovl_hist_ent hist[BOVL_HIST];
	uint8_t reserved_t[4096 - 2048 - BOVL_MAX_SLOTS * 40 - BOVL_HIST * 24];
	/* 4096: the icon picture, premultiplied 0xAARRGGBB, icon_w x icon_h, rows packed */
	uint32_t icon_px[BOVL_ICON_MAX * BOVL_ICON_MAX];
	uint8_t reserved_i[BOVL_HEADER - 4096 - BOVL_ICON_MAX * BOVL_ICON_MAX * 4];
	/* 65536: the slots (slot_off + i * slot_size) */
} bovl_shm;

/* a consistent copy of the icon (bovl_icon_read) */
typedef struct {
	uint32_t seq, on, badge, corner, margin, w, h, slop;
	uint32_t px[BOVL_ICON_MAX * BOVL_ICON_MAX];
} bovl_icon;

/* the file's size for a picture of w x h with n slots */
size_t bovl_size(uint32_t w, uint32_t h, uint32_t slots);
static inline uint16_t* bovl_pixels(bovl_shm* s, uint32_t slot)
{
	return (uint16_t*)((uint8_t*)s + s->slot_off + (size_t)slot * s->slot_size);
}
/* a U b (an empty rectangle: w or h <= 0) */
bovl_rect bovl_union(bovl_rect a, bovl_rect b);
/* the rectangle to copy to bring a buffer holding frame `has` up to frame `want` (the union of hist's rectangles of
 * the frames in between), or the whole picture when the history doesn't reach (has 0, too old, overwritten) */
bovl_rect bovl_changed(const bovl_shm* s, uint64_t has, uint64_t want);

/* ---- the creator (banana-ui) ---- */
/* create (an old file at path is removed first; O_EXCL) and map: w x h RGB565, n slots (2..4). NULL on error */
bovl_shm* bovl_create(const char* path, uint32_t w, uint32_t h, uint32_t slots);
/* map an existing file (either side): NULL when there is none or it isn't overlay version 1 */
bovl_shm* bovl_open(const char* path);
void bovl_unmap(bovl_shm* s);

/* ---- the painter ---- */
void bovl_painter_start(bovl_shm* s);   /* painter_pid, painter_state READY */
void bovl_painter_stop(bovl_shm* s);    /* painter_state NONE, icon off (the presenter closes) */
/* the icon: a premultiplied 0xAARRGGBB picture (w, h <= BOVL_ICON_MAX; NULL keeps the last pixels), shown or not */
void bovl_set_icon(bovl_shm* s, const uint32_t* px, uint32_t w, uint32_t h, uint32_t on, uint32_t badge,
	uint32_t corner, uint32_t margin, uint32_t slop);
/* ask the presenter to open (1) or close (0) */
void bovl_request_open(bovl_shm* s, uint32_t open);
static inline uint32_t bovl_is_open(const bovl_shm* s) { return __atomic_load_n(&s->open, __ATOMIC_ACQUIRE); }
static inline uint32_t bovl_open_seq(const bovl_shm* s) { return __atomic_load_n(&s->open_seq, __ATOMIC_ACQUIRE); }
/* begin a frame: a free slot (not the newest, not claimed). *must: what has to be drawn into it at least (what changed
 * since the frame it holds, given the new frame's own changes `dirty`). -1 when no slot is free (try again later). */
int bovl_paint_begin(bovl_shm* s, bovl_rect dirty, bovl_rect* must);
/* publish slot as the next frame (dirty: what changed since the last frame; open_seq: the open it was painted for) */
uint64_t bovl_paint_end(bovl_shm* s, int slot, bovl_rect dirty, uint32_t open_seq);
/* both in one: the changed part of a 0x00RRGGBB picture (w x h, stride in pixels) converted into a free slot and
 * published. The frame number, or 0 when no slot was free (nothing changed: call again later with the same dirty) */
uint64_t bovl_paint_xrgb(bovl_shm* s, const uint32_t* px, uint32_t stride, bovl_rect dirty, uint32_t open_seq);
/* the next touch after *tail (the painter's own counter, start at 0): 1 and *out, 0 none. Touches lost to a full ring
 * are skipped (*lost += how many, when lost isn't NULL) */
int bovl_touch_next(const bovl_shm* s, uint32_t* tail, bovl_touch* out, uint32_t* lost);

/* ---- the presenter ---- */
/* 0: attached (path NULL: getenv(BANANA_COMPANION_OVERLAY)), *out mapped; -1: no overlay (not an error) */
int bovl_attach(const char* path, bovl_shm** out);
void bovl_presenter_ready(bovl_shm* s, uint32_t ready);   /* presenter_pid + state */
void bovl_detach(bovl_shm* s);                             /* state NONE, closed, unmapped */
static inline int bovl_painting(const bovl_shm* s) { return __atomic_load_n(&s->painter_state, __ATOMIC_ACQUIRE) == BOVL_PAINTER_READY; }
/* the icon when it changed since *seq: 1 copied (*seq updated), 0 unchanged, -1 the painter was writing (next time) */
int bovl_icon_read(const bovl_shm* s, uint32_t* seq, bovl_icon* out);
/* open or close (open_seq bumped); the painter sees it at its next tick */
void bovl_set_open(bovl_shm* s, uint32_t open);
/* the painter's request since the last one taken: 1 (*open), 0 none */
int bovl_take_request(bovl_shm* s, uint32_t* open);
void bovl_set_rects(bovl_shm* s, bovl_rect icon, bovl_rect view);
/* queue a touch (picture coordinates) */
void bovl_touch_push(bovl_shm* s, uint32_t type, int32_t x, int32_t y, uint32_t ms);
/* claim the newest frame when it is newer than `newer_than`: its slot (header copied to *hdr), -1 none newer or it
 * changed while claiming. Copy its pixels, then bovl_release. */
int bovl_claim(bovl_shm* s, uint64_t newer_than, bovl_slot* hdr);
void bovl_release(bovl_shm* s);

#ifdef __cplusplus
}
#endif

#endif
