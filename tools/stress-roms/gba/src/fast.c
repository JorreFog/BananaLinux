/* gbastress: the IWRAM ARM code (as games put their hot loops: mixer, IRQ handler, raster tables). GPL-3.0. */
#include "stress.h"

Voice voices[NVOICE];
s8 sndbuf_a[2][NSAMP] __attribute__((aligned(4)));
s8 sndbuf_b[2][NSAMP] __attribute__((aligned(4)));
volatile u32 snd_cur, vblanks, scene_irq, hblank_count;
s16 m7_table[160 * 8] __attribute__((aligned(4)));
static s16 accl[NSAMP], accr[NSAMP];

IWRAM_CODE void mix_voices(s8* outl, s8* outr)
{
	for (int i = 0; i < NSAMP; i++) { accl[i] = 0; accr[i] = 0; }
	for (int v = 0; v < NVOICE; v++)
	{
		Voice* vo = &voices[v];
		if (!vo->data) continue;
		const s8* d = vo->data;
		u32 pos = vo->pos, step = vo->step, len = vo->len;
		s32 vl = vo->voll, vr = vo->volr;
		for (int i = 0; i < NSAMP; i++)
		{
			s32 s = d[pos >> 12];
			accl[i] += (s16)(s * vl);
			accr[i] += (s16)(s * vr);
			pos += step;
			if (pos >= len)
			{
				if (!vo->loop) { vo->data = 0; break; }
				pos -= vo->loop;
			}
		}
		vo->pos = pos;
	}
	for (int i = 0; i < NSAMP; i++)
	{
		s32 l = accl[i] >> 5, r = accr[i] >> 5;
		outl[i] = (s8)(l > 127 ? 127 : l < -128 ? -128 : l);
		outr[i] = (s8)(r > 127 ? 127 : r < -128 ? -128 : r);
	}
}

/* raster handlers: 1 = water/wave scene (per-line scroll + sky gradient + a mid-screen blend switch) */
IWRAM_CODE static void hblank_wave(void)
{
	u32 y = VCOUNT;
	if (y >= 159) return;
	y++;
	u32 t = vblanks;
	BGHOFS(1) = (u16)(sin_q12[(y * 4 + t * 3) & 255] >> 9);
	PAL_BG[0] = (u16)(RGB((y >> 3) & 31, (y >> 2) & 31, 31 - ((y >> 3) & 31)));
	if (y == 100) { BLDCNT = 0x3F42; BLDALPHA = 0x0A06; }
	else if (y == 1) { BLDCNT = 0; }
}

IWRAM_CODE void irq_handler(void)
{
	u16 f = IF & IE;
	if (f & IRQ_VBLANK)
	{
		/* audio: the next half of the double buffer, both FIFOs (the games' way) */
		u32 c = snd_cur ^ 1;
		DMACNT_H(1) = 0; DMACNT_H(2) = 0;
		DMASAD(1) = (u32)sndbuf_a[c]; DMASAD(2) = (u32)sndbuf_b[c];
		DMACNT_H(1) = 0xB640; DMACNT_H(2) = 0xB640;
		snd_cur = c;
		vblanks++;
	}
	if (f & IRQ_HBLANK)
	{
		hblank_count++;
		if (scene_irq == 1) hblank_wave();
	}
	IF = f;
	BIOS_IF |= f;
}

/* Mode-7 line table: per line a scale from the camera height and the line's distance to the horizon */
static const u16 recip[160] = {
#include "recip.inc"
};
IWRAM_CODE void m7_build(s32 camx, s32 camy, u32 angle, s32 height)
{
	s32 c = sin_q12[(angle + 64) & 255], s = sin_q12[angle & 255];
	s16* t = m7_table;
	for (int y = 0; y < 160; y++, t += 8)
	{
		s32 lam = (height * recip[y]) >> 12;		/* the line's scale, .8 fixed */
		s32 pa = (c * lam) >> 12, pc = (s * lam) >> 12;
		t[0] = (s16)pa; t[1] = (s16)(-(s * lam) >> 12); t[2] = (s16)pc; t[3] = (s16)((c * lam) >> 12);
		s32 lx = -120 * pa, ly = -120 * pc;
		s32 dz = lam * 160;
		s32 x = (camx << 8) + lx + ((s * dz) >> 12);
		s32 yy = (camy << 8) + ly - ((c * dz) >> 12);
		*(s32*)&t[4] = x;
		*(s32*)&t[6] = yy;
	}
}

IWRAM_CODE u32 hash_words(u32 h, const u32* p, u32 n)
{
	for (u32 i = 0; i < n; i++) { h ^= p[i]; h = (h << 5) | (h >> 27); h += 0x9E3779B9u; }
	return h;
}

IWRAM_CODE s32 arm_math(s32 a, s32 b)
{
	s32 r = a * b;
	r += (a >> 3) ^ (b << 1);
	return r >> 4;
}

/* overlays: position-independent ARM leaf code that the loader scene copies into one IWRAM buffer in turn
 * (code written to RAM and run: the recompilers' invalidation path) */
__attribute__((section(".ovl_a"), target("arm"), noinline)) void ovl_particles(s32* d, u32 n, s32 k)
{
	for (u32 i = 0; i < n; i += 4)
	{
		d[i + 2] += k;				/* vy += gravity */
		d[i] += d[i + 1];			/* x += vx */
		d[i + 3] += d[i + 2];			/* y += vy */
		if (d[i + 3] > (150 << 8)) { d[i + 3] = 150 << 8; d[i + 2] = -(d[i + 2] >> 1); }
		if (d[i] < 0 || d[i] > (240 << 8)) d[i + 1] = -d[i + 1];
	}
}
__attribute__((section(".ovl_b"), target("arm"), noinline)) void ovl_blur(s32* d, u32 n, s32 k)
{
	s32 prev = d[0];
	for (u32 i = 1; i < n; i++) { s32 cur = d[i]; d[i] = (prev + cur * 3 + k) >> 2; prev = cur; }
}
