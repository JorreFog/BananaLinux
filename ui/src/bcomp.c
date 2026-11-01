/* bcomp.c - see bcomp.h and COMPANION-SPEC.md section 3. MIT. */
#include "bcomp.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LD(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define ST(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)

static void copystr(char* dst, size_t n, const char* src)
{
	if (!src) src = "";
	size_t l = strlen(src);
	if (l >= n) l = n - 1;
	memcpy(dst, src, l);
	memset(dst + l, 0, n - l);
}

/* ---- consumer ---- */

bcomp_shm* bcomp_create(const char* path)
{
	unlink(path);
	int fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd < 0) return NULL;
	if (ftruncate(fd, BCOMP_SIZE) != 0) { close(fd); unlink(path); return NULL; }
	void* m = mmap(NULL, BCOMP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (m == MAP_FAILED) { unlink(path); return NULL; }
	bcomp_shm* s = (bcomp_shm*)m;
	s->version = BCOMP_VERSION;
	s->total_size = BCOMP_SIZE;
	s->data_off = BCOMP_DATA_OFF;
	s->data_size = BCOMP_DATA_SIZE;
	__atomic_thread_fence(__ATOMIC_RELEASE);
	memcpy(s->magic, BCOMP_MAGIC, 8);	/* last: a producer that sees the magic sees the rest */
	return s;
}

void bcomp_unmap(bcomp_shm* s)
{
	if (s) munmap(s, BCOMP_SIZE);
}

int bcomp_request(bcomp_shm* s, const bcomp_range* r, int n)
{
	if (n < 0 || n > BCOMP_MAX_REQ) return -1;
	uint64_t total = 0;
	for (int i = 0; i < n; i++) total += r[i].len;
	if (total > BCOMP_DATA_SIZE) return -1;
	memcpy(s->req, r, (size_t)n * sizeof *r);
	s->req_count = (uint32_t)n;
	ST(&s->req_seq, s->req_seq + 1);
	return 0;
}

int bcomp_reply(const bcomp_shm* s, uint64_t* frame)
{
	uint32_t want = LD(&s->req_seq);
	if (want == 0 || LD(&s->ack_seq) != want) return 0;
	if (frame) *frame = s->ack_frame;
	return 1;
}

int bcomp_ra_read(const bcomp_shm* s, bcomp_ra* out)
{
	for (int tries = 0; tries < 3; tries++)
	{
		uint32_t a = LD(&s->ra.seq);
		if (a & 1) continue;
		memcpy(out, (const void*)&s->ra, sizeof *out);
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		if (LD(&s->ra.seq) == a) { out->seq = a; return 0; }
	}
	return -1;
}

/* ---- producer ---- */

int bcomp_attach(bcomp_producer* p, const char* path)
{
	memset(p, 0, sizeof *p);
	if (!path) path = getenv(BCOMP_ENV);
	if (!path || !*path) return -1;
	int fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) return -1;
	struct stat st;
	if (fstat(fd, &st) != 0 || st.st_size != (off_t)BCOMP_SIZE) { close(fd); return -1; }
	void* m = mmap(NULL, BCOMP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (m == MAP_FAILED) return -1;
	bcomp_shm* s = (bcomp_shm*)m;
	if (memcmp(s->magic, BCOMP_MAGIC, 8) != 0 || s->version != BCOMP_VERSION || s->data_off != BCOMP_DATA_OFF)
	{
		munmap(m, BCOMP_SIZE);
		return -1;
	}
	p->shm = s;
	s->pid = (uint32_t)getpid();
	s->state = BCOMP_ST_LOADING;
	p->last_seq = LD(&s->ack_seq);
	return 0;
}

void bcomp_detach(bcomp_producer* p)
{
	if (!p->shm) return;
	p->shm->state = BCOMP_ST_EXITING;
	ST(&p->shm->attach_seq, 0);
	munmap(p->shm, BCOMP_SIZE);
	p->shm = NULL;
}

void bcomp_set_info(bcomp_producer* p, const char* system, const char* core, const char* frontend)
{
	if (!p->shm) return;
	copystr(p->shm->system, sizeof p->shm->system, system);
	copystr(p->shm->core, sizeof p->shm->core, core);
	copystr(p->shm->frontend, sizeof p->shm->frontend, frontend);
}

int bcomp_add_region(bcomp_producer* p, const char* name, uint32_t start, uint32_t size, uint32_t flags,
	uint32_t ra_base, const void* host)
{
	if (p->n >= BCOMP_MAX_REGIONS) return -1;
	bcomp_hostmap* h = &p->map[p->n];
	h->host = (const uint8_t*)host;
	h->start = start;
	h->size = size;
	h->flags = flags;
	if (p->shm)
	{
		bcomp_region* r = &p->shm->regions[p->n];
		copystr(r->name, sizeof r->name, name);
		r->start = start;
		r->size = size;
		r->flags = flags;
		r->ra_base = ra_base;
	}
	p->n++;
	return 0;
}

void bcomp_clear_regions(bcomp_producer* p)
{
	p->n = 0;
}

void bcomp_publish(bcomp_producer* p)
{
	if (!p->shm) return;
	p->shm->region_count = (uint32_t)p->n;
	ST(&p->shm->regions_seq, p->shm->regions_seq + 1);
	ST(&p->shm->attach_seq, p->shm->attach_seq + 1);
}

void bcomp_set_state(bcomp_producer* p, uint32_t state, uint32_t fps_x100)
{
	if (!p->shm) return;
	p->shm->state = state;
	p->shm->fps_x100 = fps_x100;
}

/* copy guest [addr, addr+len) to dst; unmapped bytes 0. returns the bytes found */
static uint32_t resolve(const bcomp_producer* p, uint32_t addr, uint32_t len, uint8_t* dst)
{
	memset(dst, 0, len);
	uint32_t found = 0;
	for (int i = 0; i < p->n; i++)
	{
		const bcomp_hostmap* h = &p->map[i];
		if (!h->host) continue;
		uint64_t lo = addr > h->start ? addr : h->start;
		uint64_t hi1 = (uint64_t)addr + len, hi2 = (uint64_t)h->start + h->size;
		uint64_t hi = hi1 < hi2 ? hi1 : hi2;
		if (lo >= hi) continue;
		uint32_t off = (uint32_t)(lo - h->start), n = (uint32_t)(hi - lo);
		uint8_t* d = dst + (lo - addr);
		if (h->flags & BCOMP_R_SWAP16)
			for (uint32_t k = 0; k < n; k++) d[k] = h->host[(off + k) ^ 1u];
		else
			memcpy(d, h->host + off, n);
		found += n;
	}
	return found;
}

static uint64_t now_ns(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

void bcomp_service(bcomp_producer* p, uint64_t frame)
{
	bcomp_shm* s = p->shm;
	if (!s) return;
	s->frames = frame;
	uint32_t seq = LD(&s->req_seq);
	if (seq == p->last_seq) return;
	uint64_t t0 = now_ns();
	uint32_t n = s->req_count;
	if (n > BCOMP_MAX_REQ) n = BCOMP_MAX_REQ;
	uint8_t* data = (uint8_t*)s + BCOMP_DATA_OFF;
	uint64_t at = 0;
	for (uint32_t i = 0; i < n; i++)
	{
		uint32_t len = s->req[i].len;
		if (at + len > BCOMP_DATA_SIZE) { s->status[i] = BCOMP_TOOBIG; continue; }
		uint32_t got = resolve(p, s->req[i].addr, len, data + at);
		s->status[i] = got == len ? BCOMP_OK : got == 0 ? BCOMP_UNMAPPED : BCOMP_PARTIAL;
		at += len;
	}
	s->ack_frame = frame;
	s->st_services++;
	s->st_bytes += at;
	s->st_ns += now_ns() - t0;
	p->last_seq = seq;
	ST(&s->ack_seq, seq);
}

void bcomp_ra_begin(bcomp_producer* p)
{
	if (!p->shm) return;
	__atomic_store_n(&p->shm->ra.seq, p->shm->ra.seq + 1, __ATOMIC_RELAXED);
	__atomic_thread_fence(__ATOMIC_RELEASE);
}

void bcomp_ra_end(bcomp_producer* p)
{
	if (!p->shm) return;
	ST(&p->shm->ra.seq, p->shm->ra.seq + 1);
}

void bcomp_ra_push(bcomp_producer* p, uint32_t type, uint32_t id, const char* value, const char* title)
{
	if (!p->shm) return;
	bcomp_ra* ra = &p->shm->ra;
	bcomp_ra_event* e = &ra->ev[ra->ev_head % BCOMP_RA_EVENTS];
	e->type = type;
	e->id = id;
	copystr(e->value, sizeof e->value, value);
	copystr(e->title, sizeof e->title, title);
	ra->ev_head++;
}

uint32_t bcomp_peek(const bcomp_producer* p, uint32_t addr, uint32_t len, uint8_t* dst)
{
	return resolve(p, addr, len, dst);
}
