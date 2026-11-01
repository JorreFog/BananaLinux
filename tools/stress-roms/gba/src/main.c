/* gbastress: a realistic GBA stress ROM for BananaOS's GBA emulators (own code; GPL-3.0 like BananaLinux).
 * Five scenes, each modelled on a kind of demanding GBA game (README.md), 30 s each, looping forever:
 *   0 racer   Mode 1 "Mode 7" track: per-line affine via HBlank DMA, scaled kart sprites, AI + collisions
 *   1 shooter Mode 0 parallax, ~110 sprites, bullet/enemy collisions, Y-sorted OAM, wave spikes with tile uploads
 *   2 town    Mode 0 RPG: 4 layers, big map streamed by rows/columns, NPC wander AI, VWF text box, SRAM saves
 *   3 raster  Mode 0 platformer water: an HBlank IRQ on every line (wave scroll, sky gradient, blend split),
 *             affine sprites, mosaic + window effects
 *   4 loader  scene-load spikes: BIOS LZ77 into VRAM, overlay code copied into IWRAM and run (two overlays
 *             taking turns), 256-colour palette cycling, big rotating sprites, Thumb->ARM call storm
 * Every frame: 6-voice software mixer at 18157 Hz into Direct Sound A/B (timer 0 + DMA 1/2), PSG effects, a
 * deterministic attract-mode script (no input needed), the companion probe block and a running checksum.
 * Results (per phase: frames, checksum, GBA CPU use) at 0x0203FF00, the probe block at 0x0203FFE0. */
#include "stress.h"
#include "data.h"

#ifndef PHASE_FRAMES
#define PHASE_FRAMES 1800
#endif
#define NSCENES 6
static const char* const scene_names[NSCENES] = { "RACER", "SHOOTER", "TOWN", "RASTER", "LOADER", "TACTICS" };

/* ---- results + probe (COMPANION-SPEC section 7) ---- */
typedef struct { u32 frames, checksum; u16 cpu_avg, cpu_max; u16 overruns, pad; } PhaseRes;
typedef struct { char magic[8]; u32 level, phases_done; PhaseRes r[12]; } Results;
#define RESULTS ((volatile Results*)0x0203FF00)
typedef struct { char magic[8]; u32 frame; u16 scene, phase; u32 checksum; u8 pad[12]; } Probe;
#define PROBE ((volatile Probe*)0x0203FFE0)

/* SRAM marker for the emulators' save-type detection */
__attribute__((used, aligned(4))) const char savetype[] = "SRAM_V113";

static u32 rng_state;
static u32 rnd(void) { rng_state = rng_state * 1664525u + 1013904223u; return rng_state >> 8; }

static u16 oam_shadow[128 * 4] __attribute__((aligned(4)));
static u32 frame, phase, scene, checksum;
static u32 cpu_sum, cpu_max, overruns, phase_frame;

static void (*volatile p_mix)(s8*, s8*) = mix_voices;	/* calls into IWRAM through pointers, as games do */
static void (*volatile p_m7)(s32, s32, u32, s32) = m7_build;
static u32 (*volatile p_hash)(u32, const u32*, u32) = hash_words;
static s32 (*volatile p_math)(s32, s32) = arm_math;

/* ---- text HUD on BG0 (font tiles at char block 2, map at screen block 23) ---- */
#define HUD_MAP ((volatile u16*)SCREENBLOCK(23))
static void hud_put(int x, int y, const char* s)
{
	for (; *s; s++, x++) HUD_MAP[y * 32 + x] = (u16)((*s - 32) | (15 << 12));
}
static void hud_num(int x, int y, u32 v, int digits)
{
	for (int i = digits - 1; i >= 0; i--) { HUD_MAP[y * 32 + x + i] = (u16)((16 + v % 10) | (15 << 12)); v /= 10; }
}
static void hud_init(void)
{
	dma3_copy32(font_tiles, (volatile void*)CHARBLOCK(2), sizeof font_tiles / 4);
	static const u32 zero = 0;
	dma3_fill32(&zero, HUD_MAP, 0x800 / 4);
	PAL_BG[15 * 16 + 1] = RGB(31, 31, 31);
	PAL_BG[15 * 16 + 2] = RGB(4, 4, 8);
	BGCNT(0) = (2 << 2) | (23 << 8);
	hud_put(0, 0, scene_names[scene]);
	hud_put(8, 0, "L");
	hud_num(9, 0, LEVEL, 2);
}

/* ---- procedural graphics ---- */
static void tile4(u32* t, int kind, u32 seed)
{
	for (int y = 0; y < 8; y++)
	{
		u32 row = 0;
		for (int x = 0; x < 8; x++)
		{
			u32 c;
			switch (kind)
			{
			case 0: c = ((x ^ y) & 1) ? 1 + (seed & 3) : 2 + ((seed >> 2) & 3); break;	/* ground */
			case 1: c = (x == 0 || y == 0) ? 5 : 6 + ((x * y + seed) & 3); break;	/* bricks */
			case 2: { s32 dx = x * 2 - 7, dy = y * 2 - 7; c = dx * dx + dy * dy < 40 ? 9 + (seed & 3) : 0; break; }	/* ball */
			case 3: c = ((rnd() & 15) == 0) ? 13 + (rnd() & 1) : 0; break;		/* stars */
			default: c = (seed + x + y) & 15; break;
			}
			row |= (c & 15) << (x * 4);
		}
		t[y] = row;
	}
}
static void tile8(u8* t, int kind, u32 seed)
{
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++)
		{
			u32 c;
			if (kind == 0) c = 16 + ((x + y + seed) & 7);		/* asphalt */
			else if (kind == 1) c = 32 + (((x >> 1) + (y >> 1) + seed) & 7);	/* grass */
			else c = ((x < 4) ^ (y < 4)) ? 48 : 49;		/* kerb */
			t[y * 8 + x] = (u8)c;
		}
}
static void palette_ramp(volatile u16* pal, int n, u32 seed)
{
	for (int i = 1; i < n; i++)
		pal[i] = RGB((i * 3 + seed) & 31, (i * 5 + seed * 3) & 31, (i * 7 + seed * 5) & 31);
}
static void obj_tiles(void)
{
	u32 t[8];
	volatile u32* dst = (volatile u32*)0x06010000;
	for (int i = 0; i < 256; i++)
	{
		tile4(t, i < 64 ? 2 : i < 128 ? 1 : 4, (u32)i);
		for (int k = 0; k < 8; k++) dst[i * 8 + k] = t[k];
	}
	palette_ramp(PAL_OBJ, 256, 7);
}

/* ---- OAM shadow ---- */
static int oam_n;
static void oam_clear(void)
{
	for (int i = 0; i < 128; i++) { oam_shadow[i * 4] = 0x0200; oam_shadow[i * 4 + 1] = 0; oam_shadow[i * 4 + 2] = 0; }
	oam_n = 0;
}
static void oam_add(s32 x, s32 y, u16 a0flags, u16 a1flags, u16 tile)
{
	if (oam_n >= 128) return;
	u16* o = &oam_shadow[oam_n++ * 4];
	o[0] = (u16)((y & 255) | a0flags);
	o[1] = (u16)((x & 511) | a1flags);
	o[2] = tile;
}
static void oam_affine(int i, s32 pa, s32 pb, s32 pc, s32 pd)
{
	oam_shadow[i * 16 + 3] = (u16)pa; oam_shadow[i * 16 + 7] = (u16)pb;
	oam_shadow[i * 16 + 11] = (u16)pc; oam_shadow[i * 16 + 15] = (u16)pd;
}

/* ---- music: a 4-track sequencer driving the mixer's voices, plus PSG effects ---- */
static u8 patterns[6][64];
static u32 seq_tick, seq_step;
static void music_init(void)
{
	for (int c = 0; c < 6; c++)
		for (int i = 0; i < 64; i++) patterns[c][i] = (u8)((rnd() % 5 == 0) ? 255 : (rnd() % 24));
}
static void voice_start(int v, int inst, int note, s32 vol, s32 pan)
{
	Voice* vo = &voices[v];
	vo->data = 0;
	vo->pos = 0;
	vo->step = note_steps[inst][note];
	vo->len = inst_len[inst] << 12;
	vo->loop = inst_loop[inst] << 12;
	vo->voll = (vol * (16 - pan)) >> 4;
	vo->volr = (vol * pan) >> 4;
	vo->data = inst_data[inst];
}
static void music_tick(void)
{
	if (++seq_tick < 7) return;
	seq_tick = 0;
	seq_step = (seq_step + 1) & 63;
	for (int c = 0; c < 6; c++)
	{
		u8 n = patterns[c][seq_step];
		if (n != 255) voice_start(c, c < 4 ? c : c - 3, n, 10 + (c & 3), 3 + c * 2);
	}
}
static void sfx_pcm(int v, int note) { voice_start(6 + (v & 1), 3, note, 14, 8); }
static void sfx_shot(u32 f) { sfx_pcm(0, (int)(f & 7) + 12); SND2CNT = 0xA180; SND2FREQ = (u16)(0x8000 | (1800 + (f & 63))); }
static void sfx_boom(void) { sfx_pcm(1, 2); SND4CNT = 0xF13F; SND4FREQ = 0x8063; }
static void sfx_engine(u32 speed) { SND1CNT = 0x7080; SND1FREQ = (u16)(((frame & 15) == 0 ? 0x8000 : 0) | (1200 + (speed & 511))); }

static void sound_init(void)
{
	SNDCNT_X = 0x80;
	SNDCNT_L = 0xBB77;		/* PSG 1, 2, 4 both sides, full volume */
	SNDCNT_H = 0x9A0E;	/* PSG 100 %, A+B 100 %, A left, B right, both on timer 0, FIFOs reset */
	SNDCNT_H = 0x120E;
	DMADAD(1) = FIFO_A; DMADAD(2) = FIFO_B;
	DMASAD(1) = (u32)sndbuf_a[0]; DMASAD(2) = (u32)sndbuf_b[0];
	DMACNT_H(1) = 0xB640; DMACNT_H(2) = 0xB640;
	TMCNT_L(0) = (u16)(65536 - 924);
	TMCNT_H(0) = 0x80;
}

/* =============================== scene 0: racer =============================== */
#define NKART 8
typedef struct { s32 x, y, speed; u32 angle, wp, lap; } Kart;
static Kart karts[NKART];
static s32 cam_x, cam_y; static u32 cam_a;
static const s16 waypoints[8][2] = { { 200, 200 }, { 800, 180 }, { 900, 500 }, { 820, 860 }, { 500, 900 }, { 180, 820 }, { 120, 520 }, { 160, 300 } };
static void racer_init(void)
{
	DISPCNT = 0x0080;
	u8 t[64];
	volatile u16* cb = (volatile u16*)CHARBLOCK(0);
	for (int i = 0; i < 64; i++)
	{
		tile8(t, i < 24 ? 0 : i < 56 ? 1 : 2, (u32)i);
		for (int k = 0; k < 32; k++) cb[i * 32 + k] = (u16)(t[k * 2] | (t[k * 2 + 1] << 8));
	}
	palette_ramp(PAL_BG, 64, 3);
	/* the track: a ring of asphalt through grass, 128x128 tiles (16 KB at screen block 24) */
	volatile u16* map = (volatile u16*)SCREENBLOCK(24);
	for (int y = 0; y < 128; y++)
		for (int x = 0; x < 128; x += 2)
		{
			u8 b[2];
			for (int k = 0; k < 2; k++)
			{
				s32 dx = (x + k) - 64, dy = y - 64, r2 = dx * dx + dy * dy;
				b[k] = (u8)(r2 > 1300 && r2 < 2500 ? (rnd() & 15) : r2 >= 2500 && r2 < 2650 ? 56 + ((x + y) & 7) : 24 + (rnd() & 31));
			}
			map[(y * 128 + x) >> 1] = (u16)(b[0] | (b[1] << 8));
		}
	BGCNT(2) = (3 << 14) | (24 << 8) | (1 << 13) | 3;	/* 1024x1024, wrap */
	hud_init();
	obj_tiles();
	for (int i = 0; i < NKART; i++)
	{
		karts[i].x = (waypoints[0][0] + i * 9) << 8; karts[i].y = (waypoints[0][1] + (i & 1) * 12) << 8;
		karts[i].speed = 300 + i * 20; karts[i].wp = 1; karts[i].angle = 0; karts[i].lap = 0;
	}
	DISPCNT = 0x1000 | 0x0400 | 0x0100 | 0x0040 | 1;	/* mode 1, BG0 + BG2 + OBJ, 1D */
}
static void racer_frame(void)
{
	/* AI: steer to the next waypoint (atan by search over the sine table), speed, kart-kart bumps */
	int nk = SCALE(NKART) > NKART ? NKART : SCALE(NKART);
	for (int i = 0; i < nk; i++)
	{
		Kart* k = &karts[i];
		s32 tx = waypoints[k->wp][0] << 8, ty = waypoints[k->wp][1] << 8;
		s32 dx = (tx - k->x) >> 8, dy = (ty - k->y) >> 8;
		if (dx * dx + dy * dy < 40 * 40) { k->wp = (k->wp + 1) & 7; if (!k->wp) k->lap++; }
		u32 best = k->angle; s32 bestd = 0x7fffffff;
		for (int a = -6; a <= 6; a += 2)
		{
			u32 an = (k->angle + (u32)a) & 255;
			s32 px = (k->x >> 8) + ((sin_q12[(an + 64) & 255] * 32) >> 12) - (tx >> 8);
			s32 py = (k->y >> 8) + ((sin_q12[an] * 32) >> 12) - (ty >> 8);
			s32 d = px * px + py * py;
			if (d < bestd) { bestd = d; best = an; }
		}
		k->angle = best;
		for (int sub = 0; sub < 3; sub++)
		{
			k->x += (sin_q12[(k->angle + 64) & 255] * k->speed) / (3 << 12);
			k->y += (sin_q12[k->angle] * k->speed) / (3 << 12);
			/* the surface under the kart (the track map in VRAM): grass slows */
			u32 tx2 = (u32)(k->x >> 11) & 127, ty2 = (u32)(k->y >> 11) & 127;
			u8 tile = ((volatile u8*)SCREENBLOCK(24))[ty2 * 128 + tx2];
			if (tile >= 24 && tile < 56) k->speed -= 2;
			for (int j = 0; j < nk; j++)
			{
				if (j == i) continue;
				s32 ex = (karts[j].x - k->x) >> 8, ey = (karts[j].y - k->y) >> 8;
				if (ex * ex + ey * ey < 100) { k->speed -= 7; karts[j].speed += 3; }
			}
		}
		k->speed += (420 - k->speed) >> 4;
	}
	/* camera behind kart 0 */
	cam_a = karts[0].angle;
	cam_x = (karts[0].x >> 8) - ((sin_q12[(cam_a + 64) & 255] * 60) >> 12);
	cam_y = (karts[0].y >> 8) - ((sin_q12[cam_a] * 60) >> 12);
	p_m7(cam_x, cam_y, (cam_a + 192) & 255, 256 + ((sin_q12[(frame * 2) & 255]) >> 8));
	/* sprites: project the others, scale by distance (affine OBJ, double size), plus coins */
	oam_clear();
	s32 c = sin_q12[(cam_a + 64) & 255], s = sin_q12[cam_a];
	int aff = 0;
	for (int i = 1; i < nk; i++)
	{
		s32 dx = (karts[i].x >> 8) - cam_x, dy = (karts[i].y >> 8) - cam_y;
		s32 z = (dx * c + dy * s) >> 12, xx = (-dx * s + dy * c) >> 12;
		if (z < 8 || z > 400) continue;
		s32 inv = bios_div(1 << 16, z);
		s32 sx = 120 + ((xx * inv) >> 8), sy = 40 + ((60 * inv) >> 8);
		s32 sc = bios_div(z << 8, 96);
		if (sc < 64) sc = 64;
		if (aff < 31) { oam_affine(aff, sc, 0, 0, sc); oam_add(sx - 32, sy - 32, 0x0300, (u16)(0x8000 | (aff << 9)), (u16)(i * 16)); aff++; }
	}
	/* item boxes / coins on the track: projected like the karts, each tested against every kart */
	int ni = SCALE(32) > 64 ? 64 : SCALE(32);
	for (int i = 0; i < ni; i++)
	{
		u32 a = (u32)i * 256 / (u32)ni;
		s32 wx = 512 + ((sin_q12[(a + 64) & 255] * 300) >> 12), wy = 512 + ((sin_q12[a] * 300) >> 12);
		for (int k = 0; k < nk; k++)
		{
			s32 ex = (karts[k].x >> 8) - wx, ey = (karts[k].y >> 8) - wy;
			if (ex * ex + ey * ey < 64) { karts[k].speed += 30; checksum += (u32)(i * 7 + k); }
		}
		s32 dx = wx - cam_x, dy = wy - cam_y;
		s32 z = (dx * c + dy * s) >> 12, xx = (-dx * s + dy * c) >> 12;
		if (z < 8 || z > 500) continue;
		s32 inv = bios_div(1 << 16, z);
		oam_add(120 + ((xx * inv) >> 8) - 4, 40 + ((60 * inv) >> 8) - 4, 0, 0, (u16)(64 + (frame >> 3) % 4));
	}
	oam_add(100, 120, 0x0000, 0x8000 | 0x4000, 0);	/* the player's kart, 32x32 */
	sfx_engine((u32)karts[0].speed);
	hud_num(14, 0, karts[0].lap, 2);
}

/* =============================== scene 1: shooter =============================== */
#define NBUL 64
#define NENEMY 32
#define NBOOM 16
typedef struct { s16 x, y, vx, vy; u8 on, kind; } Obj;
static Obj bullets[NBUL], enemies[NENEMY], booms[NBOOM];
#define NEBUL 64
static Obj ebul[NEBUL];
static s32 ship_hits;
static s32 ship_x, ship_y;
static void shooter_init(void)
{
	DISPCNT = 0x0080;
	u32 t[8];
	volatile u32* cb = (volatile u32*)CHARBLOCK(0);
	for (int i = 0; i < 256; i++) { tile4(t, i < 32 ? 3 : i < 160 ? 1 : 0, (u32)i); for (int k = 0; k < 8; k++) cb[i * 8 + k] = t[k]; }
	palette_ramp(PAL_BG, 240, 11);
	for (int l = 0; l < 3; l++)
	{
		volatile u16* m = (volatile u16*)SCREENBLOCK(24 + l * 2);
		for (int i = 0; i < 64 * 32; i++)
			m[i] = (u16)(l == 0 ? (rnd() & 31) : l == 1 ? ((rnd() & 3) ? 0 : 32 + (rnd() & 127)) : ((i / 64) > 26 ? 160 + (rnd() & 63) : 0));
		BGCNT(1 + l) = (u16)((1 << 14) | ((24 + l * 2) << 8) | (3 - l));
	}
	hud_init();
	obj_tiles();
	for (int i = 0; i < NBUL; i++) bullets[i].on = 0;
	for (int i = 0; i < NENEMY; i++) enemies[i].on = 0;
	for (int i = 0; i < NBOOM; i++) booms[i].on = 0;
	for (int i = 0; i < NEBUL; i++) ebul[i].on = 0;
	ship_x = 30; ship_y = 80;
	DISPCNT = 0x1000 | 0x0F00 | 0x0040;
}
static void spawn_enemy(void)
{
	for (int i = 0; i < SCALE(30) && i < NENEMY; i++)
		if (!enemies[i].on)
		{
			enemies[i] = (Obj){ (s16)(240 + (rnd() & 31)), (s16)(10 + rnd() % 130), (s16)(-1 - (s16)(rnd() & 1)), 0, 1, (u8)(rnd() & 3) };
			return;
		}
}
static void boom_at(s32 x, s32 y)
{
	for (int i = 0; i < NBOOM; i++)
		if (!booms[i].on) { booms[i] = (Obj){ (s16)x, (s16)y, 0, 0, 16, 0 }; sfx_boom(); return; }
}
static void shooter_frame(void)
{
	BGHOFS(1) = (u16)(frame >> 2); BGHOFS(2) = (u16)(frame >> 1); BGHOFS(3) = (u16)frame;
	ship_y = 80 + ((sin_q12[(frame * 2) & 255] * 50) >> 12);
	/* fire: 3-way every 6 frames */
	if (frame % 6 == 0)
		for (int s = -1; s <= 1; s++)
			for (int i = 0; i < NBUL; i++)
				if (!bullets[i].on) { bullets[i] = (Obj){ (s16)(ship_x + 16), (s16)(ship_y + 4), 4, (s16)s, 1, 0 }; break; }
	if (frame % 6 == 0) sfx_shot(frame);
	/* waves: a steady stream, and every 3 s a big wave with a tile upload (the spike) */
	if (frame % 6 == 0) spawn_enemy();
	if (frame % 180 == 0)
	{
		for (int k = 0; k < SCALE(16); k++) spawn_enemy();
		dma3_copy32(font_tiles, (volatile void*)(0x06010000 + 0x4000), 0x1000 / 4);	/* new enemy graphics */
		for (int i = 0; i < 256; i++) PAL_OBJ[i] = (u16)(PAL_OBJ[i] ^ 0x1084);
	}
	for (int i = 0; i < NBUL; i++)
		if (bullets[i].on) { bullets[i].x += bullets[i].vx; bullets[i].y += bullets[i].vy; if (bullets[i].x > 240 || bullets[i].y < 0 || bullets[i].y > 160) bullets[i].on = 0; }
	for (int i = 0; i < NENEMY; i++)
		if (enemies[i].on)
		{
			Obj* e = &enemies[i];
			e->x += e->vx;
			e->y += (s16)((sin_q12[(frame * 4 + i * 16) & 255] * 2) >> 12);
			if (e->x < -16) { e->on = 0; continue; }
			for (int b = 0; b < NBUL; b++)
				if (bullets[b].on && bullets[b].x + 8 > e->x && bullets[b].x < e->x + 16 && bullets[b].y + 8 > e->y && bullets[b].y < e->y + 16)
				{ bullets[b].on = 0; e->on = 0; boom_at(e->x, e->y); break; }
		}
	/* enemies fire aimed shots (a divide per shot), every shot tested against the ship */
	int neb = SCALE(48) > NEBUL ? NEBUL : SCALE(48);
	for (int i = 0; i < NENEMY; i++)
		if (enemies[i].on && ((frame + (u32)i * 7) % 50) == 0)
			for (int b = 0; b < neb; b++)
				if (!ebul[b].on)
				{
					s32 dx = ship_x - enemies[i].x, dy = ship_y - enemies[i].y;
					s32 d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) + 1;
					ebul[b] = (Obj){ enemies[i].x, enemies[i].y, (s16)bios_div(dx * 3, d), (s16)bios_div(dy * 3, d), 1, 0 };
					break;
				}
	for (int b = 0; b < neb; b++)
		if (ebul[b].on)
		{
			ebul[b].x += ebul[b].vx; ebul[b].y += ebul[b].vy;
			if (ebul[b].x < -8 || ebul[b].x > 240 || ebul[b].y < -8 || ebul[b].y > 160) { ebul[b].on = 0; continue; }
			if (ebul[b].x + 4 > ship_x && ebul[b].x < ship_x + 32 && ebul[b].y + 4 > ship_y && ebul[b].y < ship_y + 16) { ebul[b].on = 0; ship_hits++; }
			for (int k = 0; k < NBUL; k++)	/* shots cancel each other */
				if (bullets[k].on && (u32)(bullets[k].x - ebul[b].x + 6) < 12 && (u32)(bullets[k].y - ebul[b].y + 6) < 12) { bullets[k].on = 0; ebul[b].on = 0; break; }
		}
	for (int i = 0; i < NBOOM; i++) if (booms[i].on) booms[i].on--;
	/* OAM: everything, then Y-sorted (insertion sort, as many shooters do for priority/flicker) */
	oam_clear();
	oam_add(ship_x, ship_y, 0x4000, 0x4000, 0);
	for (int i = 0; i < NENEMY; i++) if (enemies[i].on) oam_add(enemies[i].x, enemies[i].y, 0, 0x4000, (u16)(4 + enemies[i].kind * 4));
	for (int i = 0; i < NBOOM; i++) if (booms[i].on) oam_add(booms[i].x, booms[i].y, 0, 0x4000, (u16)(128 + ((16 - booms[i].on) >> 2) * 4));
	for (int i = 0; i < NBUL; i++) if (bullets[i].on) oam_add(bullets[i].x, bullets[i].y, 0, 0, 1);
	for (int i = 0; i < NEBUL; i++) if (ebul[i].on) oam_add(ebul[i].x, ebul[i].y, 0, 0, 2);
	for (int i = 1; i < oam_n; i++)
	{
		u16 a = oam_shadow[i * 4], b = oam_shadow[i * 4 + 1], cc = oam_shadow[i * 4 + 2];
		int j = i - 1;
		while (j >= 0 && (oam_shadow[j * 4] & 255) > (a & 255)) { oam_shadow[(j + 1) * 4] = oam_shadow[j * 4]; oam_shadow[(j + 1) * 4 + 1] = oam_shadow[j * 4 + 1]; oam_shadow[(j + 1) * 4 + 2] = oam_shadow[j * 4 + 2]; j--; }
		oam_shadow[(j + 1) * 4] = a; oam_shadow[(j + 1) * 4 + 1] = b; oam_shadow[(j + 1) * 4 + 2] = cc;
	}
	hud_num(14, 0, (u32)oam_n, 3);
}

/* =============================== scene 2: town =============================== */
#define WORLD 128
EWRAM_BSS static u16 world[2][WORLD * WORLD];
#define NNPC 32
typedef struct { s16 x, y; u8 dir, anim, wait, pad; } Npc;
static Npc npcs[NNPC];
static s32 tcam_x, tcam_y, tcol, trow;
EWRAM_BSS static u32 textbuf[26 * 4 * 8];	/* the text box's 4bpp tiles, drawn by the VWF renderer */
static u32 text_pos, text_line;
static const char* const lines[] = { "WELCOME TO BANANA TOWN, TRAVELLER.", "THE INN IS NORTH OF THE FOUNTAIN.",
	"PRESS A TO TALK TO PEOPLE. SAVED!", "THE BRIDGE WAS WASHED AWAY LAST NIGHT." };
static void town_init(void)
{
	DISPCNT = 0x0080;
	u32 t[8];
	volatile u32* cb = (volatile u32*)CHARBLOCK(0);
	for (int i = 0; i < 512; i++) { tile4(t, i < 128 ? 0 : i < 384 ? 1 : 2, (u32)i * 7); for (int k = 0; k < 8; k++) cb[i * 8 + k] = t[k]; }
	palette_ramp(PAL_BG, 240, 21);
	for (int y = 0; y < WORLD; y++)	/* houses, roads, a river (the scene's load) */
		for (int x = 0; x < WORLD; x++)
		{
			u32 house = ((x / 12) + (y / 10)) % 3 == 0 && (x % 12) > 2 && (y % 10) > 3;
			u32 road = (x % 12) == 0 || (y % 10) == 0, river = ((x + y / 3) % 40) < 3;
			world[0][y * WORLD + x] = (u16)(river ? 300 + (rnd() & 7) : road ? 100 + (rnd() & 15) : rnd() & 63);
			world[1][y * WORLD + x] = (u16)(house ? 200 + ((x + y) & 63) : 0);
		}
	tcam_x = 0; tcam_y = 0;
	for (int l = 0; l < 2; l++)
	{
		volatile u16* m = (volatile u16*)SCREENBLOCK(24 + l);
		for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) m[y * 32 + x] = world[l][y * WORLD + x];
		BGCNT(2 + l) = (u16)((24 + l) << 8 | (3 - l));
	}
	tcol = 0; trow = 0;
	BGCNT(1) = (u16)((26 << 8) | 0);	/* the text box layer: char block 0 tiles 448.. are the box's */
	volatile u16* tm = (volatile u16*)SCREENBLOCK(26);
	for (int i = 0; i < 1024; i++) tm[i] = 0;
	for (int y = 0; y < 4; y++) for (int x = 0; x < 26; x++) tm[(15 + y) * 32 + 2 + x] = (u16)(448 + y * 26 + x);
	hud_init();
	obj_tiles();
	for (int i = 0; i < NNPC; i++) npcs[i] = (Npc){ (s16)(20 + rnd() % 200), (s16)(10 + rnd() % 100), (u8)(rnd() & 3), 0, (u8)(rnd() & 31), 0 };
	WIN0H = (8 << 8) | 232; WIN0V = (116 << 8) | 152; WININ = 0x3F; WINOUT = 0x3D;
	BLDCNT = 0x3E42 & ~0x40; BLDCNT = 0x0442; BLDALPHA = 0x0C04;
	text_pos = 0; text_line = 0;
	DISPCNT = 0x1000 | 0x0F00 | 0x2000 | 0x0040;
}
static int glyph_w(char c) { return c == ' ' ? 4 : font_width[(u8)(c - 32)]; }
static void vwf_draw(const char* s, u32 n, u32 row)
{
	u32* base = textbuf + row * 26 * 8;
	for (u32 i = 0; i < 26 * 8; i++) base[i] = 0x22222222;	/* the box's colour */
	u32 px = 2;
	for (u32 i = 0; i < n && s[i]; i++)
	{
		const u8* g = font_1bpp + (s[i] - 32) * 8;
		for (int y = 0; y < 8; y++)
			for (int x = 0; x < 8; x++)
				if (g[y] & (0x80 >> x))
				{
					u32 X = px + (u32)x;
					if (X >= 26 * 8) continue;
					u32* w = &base[(X >> 3) * 8 + (u32)y];
					u32 sh = (X & 7) * 4;
					*w = (*w & ~(15u << sh)) | (1u << sh);
				}
		px += (u32)glyph_w(s[i]);
	}
}
static void town_frame(void)
{
	/* the camera walks a square path; new rows/columns streamed in as it crosses tiles */
	u32 seg = (phase_frame / 300) & 3;
	tcam_x += seg == 0 ? 1 : seg == 2 ? -1 : 0;
	tcam_y += seg == 1 ? 1 : seg == 3 ? -1 : 0;
	if (tcam_x < 0) tcam_x = 0;
	if (tcam_y < 0) tcam_y = 0;
	s32 col = tcam_x >> 3, row = tcam_y >> 3;
	for (int l = 0; l < 2; l++)
	{
		volatile u16* m = (volatile u16*)SCREENBLOCK(24 + l);
		if (col != tcol) { s32 c = col + 31; for (int y = 0; y < 32; y++) m[((row + y) & 31) * 32 + (c & 31)] = world[l][((row + y) & 127) * WORLD + (c & 127)]; }
		if (row != trow) { s32 r = row + 21; for (int x = 0; x < 32; x++) m[(r & 31) * 32 + ((col + x) & 31)] = world[l][(r & 127) * WORLD + ((col + x) & 127)]; }
		BGHOFS(2 + l) = (u16)tcam_x; BGVOFS(2 + l) = (u16)tcam_y;
	}
	tcol = col; trow = row;
	/* NPCs: wander with map collision checks */
	oam_clear();
	int nn = SCALE(32) > NNPC ? NNPC : SCALE(32);
	for (int i = 0; i < nn; i++)
	{
		Npc* n = &npcs[i];
		if (n->wait) n->wait--;
		else
		{
			s32 nx = n->x + (n->dir == 0) - (n->dir == 1), ny = n->y + (n->dir == 2) - (n->dir == 3);
			u32 wx = (u32)((nx + tcam_x) >> 3) & 127, wy = (u32)((ny + 24 + tcam_y) >> 3) & 127;
			if (world[1][wy * WORLD + wx] || nx < 0 || nx > 224 || ny < 0 || ny > 128) { n->dir = (u8)(rnd() & 3); n->wait = (u8)(rnd() & 15); }
			else { n->x = (s16)nx; n->y = (s16)ny; n->anim++; }
			for (int j = 0; j < i; j++)	/* NPCs avoid each other */
				if ((u32)(npcs[j].x - n->x + 12) < 24 && (u32)(npcs[j].y - n->y + 12) < 24) { n->dir ^= 1; break; }
		}
		oam_add(n->x, n->y, 0x8000, 0x8000, (u16)(192 + ((n->anim >> 3) & 3) * 8 + n->dir * 2));
	}
	/* text box: a character a frame; every 4 s the whole box at once (a menu opening: the spike) */
	const char* s = lines[text_line & 3];
	if (phase_frame % 240 == 0) { for (u32 r = 0; r < 4; r++) vwf_draw(lines[(text_line + r) & 3], 64, r); }
	else { text_pos++; vwf_draw(s, text_pos, text_line & 3); }
	if (!s[text_pos]) { text_pos = 0; text_line++; }
	dma3_copy32(textbuf, (volatile void*)(CHARBLOCK(0) + 448 * 32), 26 * 4 * 8);
	/* a save every 10 s: 1 KB, byte by byte as SRAM needs, then read back */
	if (phase_frame % 600 == 300)
	{
		const u8* src = (const u8*)npcs;
		for (u32 i = 0; i < 1024; i++) SRAM[0x1000 + i] = src[i % sizeof npcs];
		u32 sum = 0;
		for (u32 i = 0; i < 1024; i++) sum += SRAM[0x1000 + i];
		checksum ^= sum;
	}
	hud_num(14, 0, (u32)col, 3);
}

/* =============================== scene 3: raster =============================== */
#define NRING 104
static s16 ring_x[NRING], ring_y[NRING];
static s32 pl_x, pl_y, pl_vy, rings_got;
static void raster_init(void)
{
	DISPCNT = 0x0080;
	u32 t[8];
	volatile u32* cb = (volatile u32*)CHARBLOCK(0);
	for (int i = 0; i < 256; i++) { tile4(t, i < 64 ? 0 : i < 192 ? 1 : 4, (u32)i * 3); for (int k = 0; k < 8; k++) cb[i * 8 + k] = t[k]; }
	palette_ramp(PAL_BG, 240, 5);
	for (int l = 0; l < 3; l++)
	{
		volatile u16* m = (volatile u16*)SCREENBLOCK(24 + l * 2);
		for (int i = 0; i < 64 * 32; i++) m[i] = (u16)((i / 64) > 12 + l * 4 ? (64 + (rnd() & 127)) : (l == 2 ? (rnd() & 63) : 0));
		BGCNT(1 + l) = (u16)((1 << 14) | ((24 + l * 2) << 8) | (1 + l) | (l == 0 ? 0x40 : 0));
	}
	hud_init();
	obj_tiles();
	for (int i = 0; i < NRING; i++) { ring_x[i] = (s16)(rnd() % 240); ring_y[i] = (s16)(rnd() % 150); }
	WIN1H = (40 << 8) | 120; WIN1V = (20 << 8) | 100; WININ = 0x3F00 | 0x3F; WINOUT = 0x1F;
	scene_irq = 1;
	DISPSTAT = 0x18;		/* VBlank + HBlank IRQs */
	IE = IRQ_VBLANK | IRQ_HBLANK;
	DISPCNT = 0x1000 | 0x0F00 | 0x4000 | 0x0040;
}
static void raster_frame(void)
{
	BGHOFS(2) = (u16)(frame); BGHOFS(3) = (u16)(frame * 2);
	MOSAIC = (u16)(((frame >> 4) & 3) * 0x11);
	WIN1H = (u16)((((40 + (frame & 63)) & 255) << 8) | ((120 + (frame & 63)) & 255));
	oam_clear();
	int nr = SCALE(96) > NRING ? NRING : SCALE(96);	/* (Aladdin keeps ~120 OBJ on screen: BananaGBA's fingerprint) */
	pl_vy += 40; pl_y += pl_vy; pl_x = 60 << 8;
	if (pl_y > (120 << 8)) { pl_y = 120 << 8; pl_vy = -900 - (s32)((frame & 7) << 6); }
	for (int i = 0; i < nr; i++)
	{
		s32 dx = ring_x[i] - (pl_x >> 8), dy = ring_y[i] - (pl_y >> 8);
		if (dx * dx + dy * dy < 200) { rings_got++; ring_x[i] = (s16)(ring_x[i] + 120); }
		for (int j = i + 1; j < nr && j < i + 6; j++)	/* rings bounce apart */
			if ((u32)(ring_x[j] - ring_x[i] + 8) < 16 && (u32)(ring_y[j] - ring_y[i] + 8) < 16) { ring_y[j] = (s16)(ring_y[j] + 9); while (ring_y[j] >= 150) ring_y[j] = (s16)(ring_y[j] - 150); }
	}
	oam_add(pl_x >> 8, pl_y >> 8, 0x8000, 0x8000, 192);
	checksum += (u32)rings_got;
	for (int i = 0; i < nr; i++)
	{
		ring_x[i] = (s16)(ring_x[i] + 238);	/* (mod 240 without a divide: games avoid the software one) */
		while (ring_x[i] >= 240) ring_x[i] = (s16)(ring_x[i] - 240);
		s32 y = ring_y[i] + ((sin_q12[(frame * 3 + i * 21) & 255] * 6) >> 12);
		if (i < 16)
		{
			u32 a = (frame * 4 + (u32)i * 16) & 255;
			s32 c = sin_q12[(a + 64) & 255] >> 4, s = sin_q12[a] >> 4;
			oam_affine(i, c, -s, s, c);
			oam_add(ring_x[i], y, 0x0100, (u16)(0x4000 | (i << 9)), (u16)(16 + (i & 3) * 4));
		}
		else oam_add(ring_x[i], y, 0, 0, (u16)(64 + ((frame >> 2) + (u32)i) % 8));
	}
	hud_num(14, 0, hblank_count & 0xffff, 5);
}

/* =============================== scene 4: loader =============================== */
static u32 ovl_buf[512] __attribute__((aligned(4)));	/* in IWRAM (.bss): the overlay slot */
EWRAM_BSS static s32 particles[512 * 4];
extern const u8 __ovl_a_start[], __ovl_a_end[], __ovl_b_start[], __ovl_b_end[];
static u32 cur_ovl;
static void load_overlay(u32 which)
{
	const u8* s = which ? __ovl_b_start : __ovl_a_start;
	const u8* e = which ? __ovl_b_end : __ovl_a_end;
	dma3_copy32(s, ovl_buf, (u32)(e - s) / 4);
	cur_ovl = which;
}
static void loader_init(void)
{
	DISPCNT = 0x0080;
	bios_lz77_vram(lz_tiles, (volatile void*)CHARBLOCK(1));
	palette_ramp(PAL_BG, 256, 9);
	volatile u16* m = (volatile u16*)SCREENBLOCK(24);
	for (int i = 0; i < 1024; i++) m[i] = (u16)(i & 511);
	BGCNT(1) = (u16)((1 << 2) | (24 << 8) | 1);
	hud_init();
	obj_tiles();
	for (int i = 0; i < 512; i++) { particles[i * 4] = (s32)(rnd() % 240) << 8; particles[i * 4 + 1] = (s32)(rnd() & 511) - 256; particles[i * 4 + 2] = 0; particles[i * 4 + 3] = (s32)(rnd() % 100) << 8; }
	load_overlay(0);
	DISPCNT = 0x1000 | 0x0300 | 0x0040;
}
static void loader_frame(void)
{
	/* every 2 s a "room change": LZ77 tiles into VRAM through the BIOS */
	if (phase_frame % 120 == 60) bios_lz77_vram(lz_tiles, (volatile void*)CHARBLOCK(1));
	/* overlays take turns in one IWRAM slot every 30 frames, and the code is patched (self-modifying) */
	if (phase_frame % 30 == 0) load_overlay(cur_ovl ^ 1);
	if (phase_frame % 30 == 15) ((volatile u32*)ovl_buf)[1] = ovl_buf[1];
	OvlFn f = (OvlFn)(u32)ovl_buf;
	f(particles, (u32)SCALE(400) * 4 > 2048 ? 2048 : (u32)SCALE(400) * 4, cur_ovl ? 3 : 24);
	/* palette cycling: 256 colours a frame through CpuFastSet */
	static u16 pal[256] __attribute__((aligned(4)));
	for (int i = 0; i < 256; i++) pal[i] = (u16)(RGB((i + frame) & 31, (i * 2 + frame) & 31, (i * 3) & 31));
	pal[15 * 16 + 1] = RGB(31, 31, 31);
	bios_cpufastset(pal, PAL_BG, 128);
	/* Thumb -> ARM call storm: a game engine's small helpers in IWRAM */
	s32 acc = 0;
	for (int i = 0; i < SCALE(500); i++) acc += p_math(i, (s32)frame);
	checksum ^= (u32)acc;
	/* two big rotating sprites (64x64, double size) + particles as sprites */
	oam_clear();
	for (int i = 0; i < 2; i++)
	{
		u32 a = (frame * (2 + (u32)i)) & 255;
		s32 c = sin_q12[(a + 64) & 255] >> 4, s = sin_q12[a] >> 4;
		oam_affine(i, c, -s, s, c);
		oam_add(40 + i * 100, 20, 0x0300, (u16)(0xC000 | (i << 9)), (u16)(i * 64));
	}
	for (int i = 0; i < 90; i++) oam_add(particles[i * 4] >> 8, particles[i * 4 + 3] >> 8, 0, 0, (u16)(64 + (i & 7)));
	BGHOFS(1) = (u16)(frame); BGVOFS(1) = (u16)(frame >> 1);
	hud_num(14, 0, cur_ovl, 1);
}

/* =============================== scene 5: tactics =============================== */
/* A turn-based strategy map (Advance Wars' pattern, from BananaGBA's fingerprint of the real game: the GBA's CPU never
 * halts - it waits for the vblank in a loop - an HBlank IRQ on every line, all 128 OBJ in use): a 30x20 grid of
 * units, a cursor, path-finding (flood fill) for the selected unit every few frames, damage tables. */
#define GRID_W 30
#define GRID_H 20
EWRAM_BSS static u8 terrain[GRID_H][GRID_W], unit_at[GRID_H][GRID_W];
EWRAM_BSS static u8 reach[GRID_H][GRID_W];
static s16 cur_x, cur_y;
static u32 tactics_sum;
static void tactics_init(void)
{
	DISPCNT = 0x0080;
	u32 t[8];
	volatile u32* cb = (volatile u32*)CHARBLOCK(0);
	for (int i = 0; i < 256; i++) { tile4(t, i < 96 ? 0 : i < 192 ? 1 : 4, (u32)i * 5); for (int k = 0; k < 8; k++) cb[i * 8 + k] = t[k]; }
	palette_ramp(PAL_BG, 240, 13);
	for (int y = 0; y < GRID_H; y++)
		for (int x = 0; x < GRID_W; x++) { terrain[y][x] = (u8)(rnd() % 6); unit_at[y][x] = (rnd() % 7 == 0) ? (u8)(1 + rnd() % 12) : 0; }
	for (int l = 0; l < 3; l++)
	{
		volatile u16* m = (volatile u16*)SCREENBLOCK(24 + l);
		for (int i = 0; i < 1024; i++) m[i] = (u16)(l == 0 ? terrain[(i / 32) % GRID_H][(i % 32) % GRID_W] * 16 + (rnd() & 15) : l == 1 ? ((rnd() & 7) ? 0 : 96 + (rnd() & 63)) : 192 + (rnd() & 63));
		BGCNT(1 + l) = (u16)(((24 + l) << 8) | (1 + l));
	}
	hud_init();
	obj_tiles();
	cur_x = 10; cur_y = 8;
	scene_irq = 1;			/* the per-line HBlank handler, as the game's */
	DISPSTAT = 0x18;
	IE = IRQ_VBLANK | IRQ_HBLANK;
	DISPCNT = 0x1000 | 0x0F00 | 0x0040;
}
static void tactics_frame(void)
{
	/* the cursor walks the map; every 16 frames the unit under it gets its movement range (a flood fill over terrain
	 * costs, as these games show the blue squares) */
	if ((phase_frame & 15) == 0) { cur_x = (s16)((cur_x + 3) % GRID_W); cur_y = (s16)((cur_y + 1 + (cur_x & 1)) % GRID_H); }
	if ((phase_frame & 15) == 8)
	{
		for (int y = 0; y < GRID_H; y++) for (int x = 0; x < GRID_W; x++) reach[y][x] = 0;
		reach[cur_y][cur_x] = (u8)(6 + SCALE(2));
		for (int pass = 0; pass < 5; pass++)
			for (int y = 0; y < GRID_H; y++)
				for (int x = 0; x < GRID_W; x++)
				{
					u8 r = reach[y][x];
					if (r < 2) continue;
					u8 cost = (u8)(1 + (terrain[y][x] >> 1));
					if (x > 0 && reach[y][x - 1] + cost < r) reach[y][x - 1] = (u8)(r - cost);
					if (x + 1 < GRID_W && reach[y][x + 1] + cost < r) reach[y][x + 1] = (u8)(r - cost);
					if (y > 0 && reach[y - 1][x] + cost < r) reach[y - 1][x] = (u8)(r - cost);
					if (y + 1 < GRID_H && reach[y + 1][x] + cost < r) reach[y + 1][x] = (u8)(r - cost);
				}
	}
	/* damage forecast: every unit against its neighbours (a table lookup + a BIOS divide) */
	for (int y = 0; y < GRID_H; y++)
		for (int x = 0; x < GRID_W - 1; x++)
			if (unit_at[y][x] && unit_at[y][x + 1])
				tactics_sum += (u32)bios_div((s32)(unit_at[y][x] * 55 + terrain[y][x] * 10), (s32)(unit_at[y][x + 1] + 1));
	checksum += tactics_sum;
	/* all 128 OBJ: units (16x16), the reach markers (8x8), the cursor */
	oam_clear();
	oam_add(cur_x * 8 - 4, cur_y * 8 - 4, 0, 0x4000, (u16)(4 + ((frame >> 3) & 3) * 4));
	for (int y = 0; y < GRID_H && oam_n < 128; y++)
		for (int x = 0; x < GRID_W && oam_n < 128; x++)
		{
			if (unit_at[y][x]) oam_add(x * 8, y * 8, 0, 0, (u16)(64 + unit_at[y][x]));
			else if (reach[y][x] > 1 && ((x + y + (int)frame) & 1)) oam_add(x * 8, y * 8, 0x0400, 0, 2);	/* (semi-transparent) */
		}
	while (oam_n < 128) oam_add(240, 160, 0, 0, 1);		/* the rest parked off-screen, in use (as the game's) */
	BGHOFS(1) = (u16)(cur_x * 2); BGVOFS(1) = (u16)(cur_y);
	hud_num(14, 0, (u32)cur_x, 2);
}

/* =============================== main loop =============================== */
static void scene_start(void)
{
	IME = 0;
	scene_irq = 0;
	DISPSTAT = 0x08;
	IE = IRQ_VBLANK;
	DMACNT_H(0) = 0;
	BLDCNT = 0; MOSAIC = 0; WININ = 0; WINOUT = 0;
	for (int i = 0; i < 4; i++) { BGHOFS(i) = 0; BGVOFS(i) = 0; }
	BG2PA = 256; BG2PB = 0; BG2PC = 0; BG2PD = 256; BG2X = 0; BG2Y = 0;
	rng_state = 0x1234567u + phase * 7919u;
	switch (scene)
	{
	case 0: racer_init(); break;
	case 1: shooter_init(); break;
	case 2: town_init(); break;
	case 3: raster_init(); break;
	case 4: loader_init(); break;
	default: tactics_init(); break;
	}
	IF = 0xFFFF;
	IME = 1;
}

static void phase_end(void)
{
	volatile Results* r = RESULTS;
	u32 i = phase < 12 ? phase : 11;
	r->r[i].frames = phase_frame;
	r->r[i].checksum = checksum;
	r->r[i].cpu_avg = (u16)bios_div((s32)cpu_sum, (s32)phase_frame);
	r->r[i].cpu_max = (u16)cpu_max;
	r->r[i].overruns = (u16)overruns;
	r->phases_done = phase + 1;
	/* and in SRAM (the save file the runs copy back): "GBASTRES", then 16 bytes a phase */
	const volatile u8* b = (const volatile u8*)&r->r[i];
	for (u32 k = 0; k < sizeof(PhaseRes); k++) SRAM[16 + i * 16 + k] = b[k];
}

int main(void)
{
	WAITCNT = 0x4317;	/* the usual ROM wait states with the prefetch on */
	IRQ_VECTOR = (u32)irq_handler;
	volatile Results* r = RESULTS;
	const char* m = "GBASRES1";
	for (int i = 0; i < 8; i++) { r->magic[i] = m[i]; SRAM[i] = (u8)"GBASTRES"[i]; }
	r->level = LEVEL;
	volatile Probe* p = PROBE;
	const char* pm = "BNPROBE1";
	for (int i = 0; i < 8; i++) p->magic[i] = pm[i];
	for (int i = 0; i < 12; i++) p->pad[i] = 0xA5;
	rng_state = 42;
	music_init();
	sound_init();
	scene = 0; phase = 0;
	scene_start();
	u32 last_vb = vblanks;
	for (;;)
	{
		if (scene == 5)
		{
			/* the vblank waited for in a loop, the CPU never halting (the strategy game's way) */
			u32 seen = vblanks;
			while (*(volatile u32*)&vblanks == seen) {}
		}
		else bios_vblank_wait();
		u32 vb = vblanks;
		if (vb - last_vb > 1) overruns++;
		last_vb = vb;
		/* vblank work: OAM from the shadow, then the racer's per-line affine DMA */
		dma3_copy32(oam_shadow, OAM, 256);
		if (scene == 0)
		{
			const s16* t = m7_table;
			BG2PA = (u16)t[0]; BG2PB = (u16)t[1]; BG2PC = (u16)t[2]; BG2PD = (u16)t[3];
			BG2X = *(const u32*)&t[4]; BG2Y = *(const u32*)&t[6];
			DMACNT_H(0) = 0;
			DMASAD(0) = (u32)&t[8]; DMADAD(0) = 0x4000020; DMACNT_L(0) = 4;
			DMACNT_H(0) = 0xA660;
		}
		/* sound: mix the half the DMA isn't playing */
		music_tick();
		u32 half = snd_cur ^ 1;
		p_mix(sndbuf_a[half], sndbuf_b[half]);
		(void)KEYINPUT;	/* input polled like a game (the script drives everything) */
		switch (scene)
		{
		case 0: racer_frame(); break;
		case 1: shooter_frame(); checksum += (u32)ship_hits; break;
		case 2: town_frame(); break;
		case 3: raster_frame(); break;
		case 4: loader_frame(); break;
		default: tactics_frame(); break;
		}
		frame++;
		phase_frame++;
		checksum = p_hash(checksum, (const u32*)oam_shadow, 256);
		checksum = p_hash(checksum, (const u32*)sndbuf_a[half], NSAMP / 4);
		if (scene == 0) checksum = p_hash(checksum, (const u32*)m7_table, 160 * 4);
		hud_num(20, 0, frame, 6);
		p->frame = frame; p->scene = (u16)scene; p->phase = (u16)phase; p->checksum = checksum;
		/* the GBA CPU this frame used: lines since the vblank started */
		u32 lines = (VCOUNT + 228 - 160) % 228;
		u32 cpu = lines * 100 / 228;
		cpu_sum += cpu;
		if (cpu > cpu_max) cpu_max = cpu;
		hud_num(27, 0, cpu, 3);
		if (phase_frame >= PHASE_FRAMES)
		{
			phase_end();
			phase++;
			scene = phase % NSCENES;
			phase_frame = 0; cpu_sum = 0; cpu_max = 0; overruns = 0;
			checksum = 0;
			scene_start();
			last_vb = vblanks;
		}
	}
}

/* the compiler's helpers (no libc): */
void* memcpy(void* d, const void* s, unsigned n) { u8* a = d; const u8* b = s; while (n--) *a++ = *b++; return d; }
void* memset(void* d, int c, unsigned n) { u8* a = d; while (n--) *a++ = (u8)c; return d; }
unsigned __aeabi_uidiv(unsigned n, unsigned d)
{
	unsigned q = 0, r = 0;
	for (int i = 31; i >= 0; i--) { r = (r << 1) | ((n >> i) & 1); if (r >= d) { r -= d; q |= 1u << i; } }
	return q;
}
int __aeabi_idiv(int n, int d)
{
	int neg = (n < 0) ^ (d < 0);
	unsigned q = __aeabi_uidiv((unsigned)(n < 0 ? -n : n), (unsigned)(d < 0 ? -d : d));
	return neg ? -(int)q : (int)q;
}
