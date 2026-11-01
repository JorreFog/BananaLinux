/* gbastress shared state. GPL-3.0. */
#pragma once
#include "gba.h"
#ifndef LEVEL
#define LEVEL 5
#endif
#define SCALE(n) (((n) * LEVEL + 4) / 5)
#define NSAMP 304			/* 18157 Hz: 280896 cycles / 924 per sample = 304 per frame */
#define NVOICE 8
typedef struct { const s8* data; u32 pos, step, len, loop; s32 voll, volr; } Voice;
extern Voice voices[NVOICE];
extern s8 sndbuf_a[2][NSAMP], sndbuf_b[2][NSAMP];
extern volatile u32 snd_cur;
extern volatile u32 vblanks;
extern volatile u32 scene_irq;		/* which raster handler the HBlank IRQ runs (0 none) */
extern volatile u32 hblank_count;
extern s16 m7_table[160 * 8];		/* per line: PA PB PC PD X(2) Y(2) */
extern const s16 sin_q12[256];
void mix_voices(s8* outl, s8* outr);
void irq_handler(void);
void m7_build(s32 camx, s32 camy, u32 angle, s32 height);
u32 hash_words(u32 h, const u32* p, u32 n);
s32 arm_math(s32 a, s32 b);			/* small ARM routine called from Thumb (interworking, like m4a/libraries) */
typedef void (*OvlFn)(s32* data, u32 n, s32 k);
