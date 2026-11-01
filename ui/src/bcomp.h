/* bcomp.h - BananaOS universal companion: the shared-memory memory export (COMPANION-SPEC.md section 3).
 *
 * One file, /run/banana/companion.shm (BCOMP_SIZE bytes), created by banana-ui (the consumer) before it starts the
 * emulator, named in the emulator's environment as BANANA_COMPANION. The producer (BananaFront, BananaDS) attaches to
 * it, publishes its memory regions, answers read requests between two frames, and keeps the RetroAchievements block.
 *
 * Pull model: nothing is copied unless the companion asks; when nothing is asked bcomp_service() is one atomic load.
 *
 * MIT licence (as banana-ui). C99 + GCC/Clang __atomic builtins. No allocation, no threads.
 */
#ifndef BCOMP_H
#define BCOMP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BCOMP_MAGIC "BNCOMP\0\1"
#define BCOMP_VERSION 1u
#define BCOMP_SIZE (1u << 20)
#define BCOMP_ENV "BANANA_COMPANION"
#define BCOMP_DEFAULT_PATH "/run/banana/companion.shm"

#define BCOMP_MAX_REGIONS 48
#define BCOMP_MAX_REQ 64
#define BCOMP_RA_NEAR 8
#define BCOMP_RA_EVENTS 32

/* region flags */
#define BCOMP_R_RAM 0x01u    /* live, changes every frame */
#define BCOMP_R_SRAM 0x02u   /* battery save */
#define BCOMP_R_ROM 0x04u    /* constant */
#define BCOMP_R_VRAM 0x08u
#define BCOMP_R_IO 0x10u
#define BCOMP_R_BE 0x20u     /* stored in the system's big-endian byte order */
#define BCOMP_R_SWAP16 0x40u /* 16-bit words kept host-endian: guest byte a is at host byte a ^ 1 */

/* producer states */
enum { BCOMP_ST_NONE, BCOMP_ST_LOADING, BCOMP_ST_RUNNING, BCOMP_ST_PAUSED, BCOMP_ST_EXITING };
/* request status */
enum { BCOMP_OK, BCOMP_UNMAPPED, BCOMP_PARTIAL, BCOMP_TOOBIG };
/* RA block states */
enum { BCOMP_RA_NONE, BCOMP_RA_LOGIN, BCOMP_RA_LOADING, BCOMP_RA_ACTIVE, BCOMP_RA_NO_SET, BCOMP_RA_UNKNOWN_GAME,
	BCOMP_RA_OFFLINE, BCOMP_RA_LOGIN_FAILED };
/* RA events */
enum { BCOMP_EV_NONE, BCOMP_EV_UNLOCK, BCOMP_EV_MASTERY, BCOMP_EV_LB_START, BCOMP_EV_LB_SUBMIT, BCOMP_EV_LB_FAIL,
	BCOMP_EV_TRACKER_SHOW, BCOMP_EV_TRACKER_UPDATE, BCOMP_EV_TRACKER_HIDE, BCOMP_EV_SERVER_ERROR };

typedef struct { uint32_t addr, len; } bcomp_range;

typedef struct {
	char name[16];
	uint32_t start, size, flags, ra_base;
	uint32_t reserved[2];
} bcomp_region; /* 40 bytes */

typedef struct {
	uint32_t id, percent;
	char measured[24];
	char title[64];
} bcomp_ra_near; /* 96 */

typedef struct {
	uint32_t type, id;
	char value[32];
	char title[64];
} bcomp_ra_event; /* 104 */

typedef struct {
	uint32_t seq; /* seqlock: odd while the producer writes */
	uint32_t state, game_id, hardcore, unlocked, total, points_got, points_total;
	char rp[512];
	bcomp_ra_near near_[BCOMP_RA_NEAR];
	uint32_t ev_head, reserved0; /* ev_head: events written so far; the newest is ev[(ev_head-1) % 32] */
	bcomp_ra_event ev[BCOMP_RA_EVENTS];
} bcomp_ra;

typedef struct {
	/* 0: header (banana-ui) */
	char magic[8];
	uint32_t version, total_size, data_off, data_size;
	uint8_t reserved_h[40];
	/* 64: producer status */
	uint32_t pid, state;
	uint64_t frames;
	uint32_t fps_x100, attach_seq, regions_seq, region_count;
	char system[16];
	char core[64];
	char frontend[32];
	uint64_t st_services, st_bytes, st_ns; /* producer counters: answered sets, bytes copied, ns in bcomp_service */
	uint8_t reserved_p[24];
	/* 256: regions */
	bcomp_region regions[BCOMP_MAX_REGIONS];
	uint8_t reserved_r[3584 - 256 - BCOMP_MAX_REGIONS * 40];
	/* 3584: request (banana-ui) / reply (producer) */
	uint32_t req_seq, req_count;
	bcomp_range req[BCOMP_MAX_REQ];
	uint32_t ack_seq, reserved_a;
	uint64_t ack_frame;
	uint32_t status[BCOMP_MAX_REQ];
	uint8_t reserved_q[8192 - 4376];
	/* 8192: RetroAchievements */
	bcomp_ra ra;
	uint8_t reserved_ra[16384 - 8192 - sizeof(bcomp_ra)];
	/* 16384: data */
} bcomp_shm;

#define BCOMP_DATA_OFF 16384u
#define BCOMP_DATA_SIZE (BCOMP_SIZE - BCOMP_DATA_OFF)

/* ---- consumer (banana-ui) ---- */
/* create (O_EXCL; an old file at path is removed first) and map path, header written. NULL on error. */
bcomp_shm* bcomp_create(const char* path);
void bcomp_unmap(bcomp_shm* s);
/* the producer attached (and published its regions) */
static inline int bcomp_attached(const bcomp_shm* s) { return __atomic_load_n(&s->attach_seq, __ATOMIC_ACQUIRE) != 0; }
/* ask for n ranges (n <= 64, total <= BCOMP_DATA_SIZE): 0, or -1 when too many / too big */
int bcomp_request(bcomp_shm* s, const bcomp_range* r, int n);
/* the last request answered: 1 (data at bcomp_data(s), request i's bytes after the lengths before it; status[i];
 * *frame the frame it was taken after), 0 not yet */
int bcomp_reply(const bcomp_shm* s, uint64_t* frame);
static inline const uint8_t* bcomp_data(const bcomp_shm* s) { return (const uint8_t*)s + BCOMP_DATA_OFF; }
/* a consistent copy of the RA block: 0, or -1 when the producer kept writing (try again next tick) */
int bcomp_ra_read(const bcomp_shm* s, bcomp_ra* out);

/* ---- producer (BananaFront, BananaDS) ---- */
typedef struct {
	const uint8_t* host; /* the core's memory for guest address start */
	uint32_t start, size, flags;
} bcomp_hostmap;

typedef struct {
	bcomp_shm* shm; /* NULL: not attached, every call is a no-op */
	bcomp_hostmap map[BCOMP_MAX_REGIONS];
	int n;
	uint32_t last_seq;
} bcomp_producer;

/* attach to path (NULL: getenv(BANANA_COMPANION)); 0 attached, -1 no companion (p->shm NULL; not an error) */
int bcomp_attach(bcomp_producer* p, const char* path);
void bcomp_detach(bcomp_producer* p);
void bcomp_set_info(bcomp_producer* p, const char* system, const char* core, const char* frontend);
/* add a region (call for each, then bcomp_publish): 0, -1 table full */
int bcomp_add_region(bcomp_producer* p, const char* name, uint32_t start, uint32_t size, uint32_t flags,
	uint32_t ra_base, const void* host);
void bcomp_clear_regions(bcomp_producer* p);
/* make the region table visible (and mark attached) */
void bcomp_publish(bcomp_producer* p);
void bcomp_set_state(bcomp_producer* p, uint32_t state, uint32_t fps_x100);
/* once per frame, after retro_run (or at the menu's refresh while paused): answers a pending request */
void bcomp_service(bcomp_producer* p, uint64_t frame);
/* read guest memory through the producer's own region table (no shm needed; e.g. rcheevos' read_memory after the
 * RA -> bus address translation): the bytes found (unmapped ones are 0 in dst) */
uint32_t bcomp_peek(const bcomp_producer* p, uint32_t addr, uint32_t len, uint8_t* dst);
/* RA block: begin, change p->shm->ra fields, end (seqlock) */
void bcomp_ra_begin(bcomp_producer* p);
void bcomp_ra_end(bcomp_producer* p);
/* append an event (inside begin/end) */
void bcomp_ra_push(bcomp_producer* p, uint32_t type, uint32_t id, const char* value, const char* title);

#ifdef __cplusplus
}
#endif

#endif
