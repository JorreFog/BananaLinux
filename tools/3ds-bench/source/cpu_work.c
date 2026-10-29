// Game-logic-like CPU workload (ARM mode, VFP floats).
// Entity physics/AI with a spatial grid, a skeletal matrix hierarchy,
// render-list memcpy/memset, plus Thumb-mode sorting / hashing / CRC.
// Everything is deterministic: fixed seeds, fixed dt, no wall-clock input.
#include "bench.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#define MAX_ENTS 2048
#define GRID     32
#define WORLD    64.0f   // entities live in [-WORLD, WORLD]^2
#define DT       (1.0f / 60.0f)

typedef struct {
	float px, py, pz;
	float vx, vy, vz;
	float hp, timer;
	u32 state, target, cell, kills;
} Ent;

static Ent ents[MAX_ENTS];
static u16 cellHead[GRID * GRID];
static u16 cellNext[MAX_ENTS];
static u32 rng;

#define MEM_BYTES (256 * 1024)
static u8* memA;
static u8* memB;

#define MAX_SORT 4096
static u32 *sortK, *sortV, *tmpK, *tmpV;

typedef struct { float m[16]; } M4;
#define SKEL_BONES 64
#define MAX_SKELS  4
static M4 boneWorld[MAX_SKELS][SKEL_BONES];

static inline u32 xs(void) {
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}
static inline float frand(void) { return (float)(xs() & 0xFFFFFF) * (1.0f / 16777216.0f); }

static void spawn(Ent* e) {
	e->px = (frand() * 2.0f - 1.0f) * WORLD;
	e->pz = (frand() * 2.0f - 1.0f) * WORLD;
	e->py = frand() * 4.0f;
	e->vx = e->vy = e->vz = 0.0f;
	e->hp = 50.0f + frand() * 50.0f;
	e->timer = frand() * 2.0f;
	e->state = 0;
	e->target = 0;
}

void cpu_init(void) {
	rng = 0x9E3779B9u;
	for (int i = 0; i < MAX_ENTS; i++) {
		spawn(&ents[i]);
		ents[i].kills = 0;
	}
	memA = (u8*)memalign(64, MEM_BYTES);
	memB = (u8*)memalign(64, MEM_BYTES);
	memset(memA, 0, MEM_BYTES);
	memset(memB, 0, MEM_BYTES);
	sortK = (u32*)malloc(MAX_SORT * 4);
	sortV = (u32*)malloc(MAX_SORT * 4);
	tmpK = (u32*)malloc(MAX_SORT * 4);
	tmpV = (u32*)malloc(MAX_SORT * 4);
	thumb_init();
}

static inline u32 fbits(float f) {
	union { float f; u32 u; } c = { f };
	return c.u;
}

static void m4_mul(M4* restrict o, const M4* restrict a, const M4* restrict b) {
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			o->m[r * 4 + c] = a->m[r * 4 + 0] * b->m[0 * 4 + c] + a->m[r * 4 + 1] * b->m[1 * 4 + c] +
			                  a->m[r * 4 + 2] * b->m[2 * 4 + c] + a->m[r * 4 + 3] * b->m[3 * 4 + c];
}

static void m4_local(M4* o, float ang, float len, int axis) {
	float s = sinf(ang), c = cosf(ang);
	memset(o, 0, sizeof(*o));
	o->m[15] = 1.0f;
	if (axis) { // rotate about Z
		o->m[0] = c; o->m[1] = -s; o->m[4] = s; o->m[5] = c; o->m[10] = 1.0f;
	} else {    // rotate about X
		o->m[0] = 1.0f; o->m[5] = c; o->m[6] = -s; o->m[9] = s; o->m[10] = c;
	}
	o->m[7] = len; // translate along Y
}

static u32 update_entities(u32 frame, int n) {
	u32 h = 0;
	// 1. spatial grid
	memset(cellHead, 0xFF, sizeof(cellHead));
	for (int i = 0; i < n; i++) {
		Ent* e = &ents[i];
		int cx = (int)((e->px + WORLD) * (GRID / (2.0f * WORLD)));
		int cz = (int)((e->pz + WORLD) * (GRID / (2.0f * WORLD)));
		cx = cx < 0 ? 0 : cx >= GRID ? GRID - 1 : cx;
		cz = cz < 0 ? 0 : cz >= GRID ? GRID - 1 : cz;
		e->cell = cz * GRID + cx;
		cellNext[i] = cellHead[e->cell];
		cellHead[e->cell] = (u16)i;
	}

	// 2. AI + physics
	for (int i = 0; i < n; i++) {
		Ent* e = &ents[i];
		float ax = 0.0f, az = 0.0f;
		e->timer -= DT;
		switch (e->state) {
		case 0: // idle: pick a new behaviour
			if (e->timer < 0.0f) {
				e->state = 1 + xs() % 3;
				e->target = xs() % n;
				e->timer = 1.0f + frand() * 3.0f;
			}
			break;
		case 1: // seek + attack
		case 2: { // flee
			Ent* t = &ents[e->target];
			float dx = t->px - e->px, dz = t->pz - e->pz;
			float d2 = dx * dx + dz * dz;
			if (d2 > 1e-4f) {
				float inv = 1.0f / sqrtf(d2);
				float k = e->state == 1 ? 6.0f : -5.0f;
				ax += dx * inv * k;
				az += dz * inv * k;
			}
			if (e->state == 1 && d2 < 4.0f) {
				t->hp -= 0.75f;
				if (t->hp <= 0.0f) e->kills++;
			}
			if (e->timer < 0.0f) { e->state = 0; e->timer = frand(); }
			break;
		}
		default: { // wander
			float a = e->timer * 3.0f + (float)i;
			ax += cosf(a) * 3.0f;
			az += sinf(a) * 3.0f;
			if (e->timer < 0.0f) { e->state = 0; e->timer = frand(); }
			break;
		}
		}

		// separation against up to 8 neighbours in the same cell
		int k = 0;
		for (u16 j = cellHead[e->cell]; j != 0xFFFF && k < 8; j = cellNext[j], k++) {
			if (j == i) continue;
			float dx = e->px - ents[j].px, dz = e->pz - ents[j].pz;
			float d2 = dx * dx + dz * dz;
			if (d2 < 1.0f && d2 > 1e-6f) {
				float inv = 1.0f / sqrtf(d2);
				ax += dx * inv * 4.0f;
				az += dz * inv * 4.0f;
			}
		}

		e->vx = (e->vx + ax * DT) * 0.98f;
		e->vz = (e->vz + az * DT) * 0.98f;
		e->vy -= 9.8f * DT;
		float s2 = e->vx * e->vx + e->vz * e->vz;
		if (s2 > 100.0f) {
			float k2 = 10.0f / sqrtf(s2);
			e->vx *= k2;
			e->vz *= k2;
		}
		e->px += e->vx * DT;
		e->py += e->vy * DT;
		e->pz += e->vz * DT;
		if (e->py < 0.0f) {
			e->py = 0.0f;
			e->vy = -e->vy * 0.4f + (e->state == 1 ? 2.0f : 0.0f);
		}
		if (e->px < -WORLD || e->px > WORLD) { e->vx = -e->vx; e->px = e->px < 0 ? -WORLD : WORLD; }
		if (e->pz < -WORLD || e->pz > WORLD) { e->vz = -e->vz; e->pz = e->pz < 0 ? -WORLD : WORLD; }

		e->hp += 0.05f;
		if (e->hp <= 0.0f) spawn(e);
		h = (h ^ fbits(e->px)) * 16777619u;
		h = (h ^ fbits(e->pz)) * 16777619u;
	}
	(void)frame;
	return h;
}

static u32 update_skeletons(u32 frame, int skels) {
	u32 h = 0;
	float t = (float)frame * DT;
	for (int s = 0; s < skels; s++) {
		M4 local, tmp;
		m4_local(&boneWorld[s][0], t + s, 0.0f, 0);
		for (int b = 1; b < SKEL_BONES; b++) {
			int parent = (b - 1) >> 1;
			m4_local(&local, sinf(t * 2.0f + b * 0.3f + s) * 0.5f, 0.25f + (b & 3) * 0.1f, b & 1);
			m4_mul(&tmp, &boneWorld[s][parent], &local);
			boneWorld[s][b] = tmp;
		}
		h ^= fbits(boneWorld[s][SKEL_BONES - 1].m[7]);
	}
	return h;
}

u32 cpu_frame(u32 frame, int n, int sortN, int hashN, int memKB) {
	if (n > MAX_ENTS) n = MAX_ENTS;
	if (sortN > MAX_SORT) sortN = MAX_SORT;
	u32 h = update_entities(frame, n);
	h ^= update_skeletons(frame, (n + 499) / 500);

	// render-list style buffer churn: memset + memcpy (~memKB per frame)
	u32 bytes = (u32)memKB * 1024;
	if (bytes > MEM_BYTES + MEM_BYTES / 2) bytes = MEM_BYTES + MEM_BYTES / 2;
	u32 setBytes = bytes / 3, cpyBytes = bytes - setBytes;
	if (cpyBytes > MEM_BYTES) cpyBytes = MEM_BYTES;
	memset(memA, (int)(frame & 0xFF), setBytes);
	memcpy(memA + 64, ents, sizeof(Ent) * (n < 512 ? n : 512)); // "serialize" some state
	memcpy(memB, memA, cpyBytes);

	// depth sort of a draw list (keys from entity positions), Thumb merge sort
	for (int i = 0; i < sortN; i++) {
		const Ent* e = &ents[i % n];
		sortK[i] = (u32)((e->pz + WORLD) * 256.0f) ^ ((u32)i << 20);
		sortV[i] = (u32)i;
	}
	h ^= thumb_sort(sortK, sortV, tmpK, tmpV, sortN);
	h ^= thumb_hash_bench(frame * 2654435761u, hashN);
	h ^= thumb_crc32(memB, 16 * 1024, h);
	return h;
}
