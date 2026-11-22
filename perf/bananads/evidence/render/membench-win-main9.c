/* membench ARM9: a game-like frame of the memory accesses JIT code can't do inline (BananaDS's docs/PERF.md),
   paced by VBlank as games are: each pass is one frame's work, then the ARM9 halts until VBlank.
   Per pass: BG map and tile updates in engine A's BG VRAM (bank A) and reads back, sprite tiles in engine A's OBJ
   VRAM (bank B), OAM (A by STRH, B by memcpy's LDM/STM), palettes (16- and 32-bit) and reads back, the divider
   and square root (polling DIVCNT/SQRTCNT), VCOUNT/DISPSTAT/KEYINPUT polls, IME critical sections and IE/IF
   acknowledges, IPC FIFO batches to the ARM7 (which halts until the FIFO wakes it, and replies; the ARM9 halts
   for the reply), geometry commands by single STRs to the command ports and by STM into the GX FIFO, and stores
   into a main-RAM page that holds code (data next to the code, not in the same 16 bytes).
   Built as Thumb (games' main code) with some routines in ARM. The bottom screen (engine B, a 16-bit bitmap in
   bank C) shows the pass count, then the checksums as they were after pass 16, so a JIT and the interpreter can
   be compared once both have run 16 passes. Written for this benchmark. */
typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8; typedef int s32; typedef short s16;
typedef unsigned long long u64; typedef long long s64;
#define R32(a) (*(volatile u32 *)(a))
#define R16(a) (*(volatile u16 *)(a))
#define R8(a) (*(volatile u8 *)(a))
#define ARM __attribute__((target("arm"), noinline))
#define NOINLINE __attribute__((noinline))

#define DISPSTAT 0x04000004
#define VCOUNT 0x04000006
#define KEYINPUT 0x04000130
#define IPCSYNC 0x04000180
#define IPCFIFOCNT 0x04000184
#define IPCFIFOSEND 0x04000188
#define IPCFIFORECV 0x04100000
#define IME 0x04000208
#define IE 0x04000210
#define IF 0x04000214
#define IRQ_VBLANK 1u
#define IRQ_IPCRECV (1u << 18)

void *memset(void *d, int c, unsigned long n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
/* word copy, 8 words a step (an LDM/STM pair in ARM), as libc's memcpy does for aligned buffers */
ARM void *memcpy(void *d, const void *s, unsigned long n)
{
    u32 *dw = d; const u32 *sw = s;
    if ((((u32)d | (u32)s) & 3) == 0) {
        while (n >= 32) { u32 a = sw[0], b = sw[1], c = sw[2], e = sw[3], f = sw[4], g = sw[5], h = sw[6], i = sw[7];
                          dw[0] = a; dw[1] = b; dw[2] = c; dw[3] = e; dw[4] = f; dw[5] = g; dw[6] = h; dw[7] = i;
                          dw += 8; sw += 8; n -= 32; }
        while (n >= 4) { *dw++ = *sw++; n -= 4; }
    }
    u8 *db = (u8 *)dw; const u8 *sb = (const u8 *)sw;
    while (n--) *db++ = *sb++;
    return d;
}
/* the ARM9's halt (CP15 wait for interrupt): wakes when IE & IF, IRQs stay masked in the CPSR */
ARM void halt9(void) { __asm__ volatile("mov r0, #0\n\tmcr p15, 0, r0, c7, c0, 4" ::: "r0", "memory"); }
/* n words into the GX FIFO, 8 at a time with LDM/STM as games' display-list uploads do */
ARM void gx_stm(const u32 *p, int n)
{
    while (n >= 8) {
        register const u32 *rp __asm__("r0") = p;
        register u32 rf __asm__("r1") = 0x04000400;
        __asm__ volatile("ldmia %0!, {r2-r8, r12}\n\tstmia %1, {r2-r8, r12}" : "+r"(rp) : "r"(rf)
                         : "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r12", "memory");
        p = rp; n -= 8;
    }
    while (n--) R32(0x04000400) = *p++;
}

static u32 iosink;
static inline void poll_lines(void) { iosink += R16(VCOUNT) + R16(DISPSTAT); }

/* stores into a main-RAM page that holds compiled code: this array sits between hot functions (build.py's
   linker script puts .text.near after the first ones); its 16-byte chunks hold no code */
__attribute__((section(".text.near"), aligned(16))) u32 nearcode[64] = {1};
static NOINLINE u32 near_stores(u32 pass)
{
    u32 s = 0;
    for (int i = 0; i < 48; i++) nearcode[i] = nearcode[i] * 3 + pass + (u32)i;
    for (int i = 0; i < 32; i++) ((volatile u16 *)&nearcode[48])[i] = (u16)(pass * 7 + (u32)i);
    for (int i = 0; i < 32; i++) ((volatile u8 *)&nearcode[56])[i] = (u8)(pass + (u32)i * 5);
    for (int i = 0; i < 64; i++) s = (s ^ nearcode[i]) * 0x01000193;
    return s;
}

/* 1. BG map: a 32x24 window of engine A's BG1 map, rewritten (scrolling text), and a quarter read back */
static NOINLINE u32 map_update(u32 pass)
{
    volatile u16 *map = (volatile u16 *)0x0600F800;
    u32 s = 0;
    for (int r = 0; r < 24; r++) {
        for (int c = 0; c < 32; c++) map[r * 32 + c] = (u16)(((u32)(c + r) + pass) & 63) | (u16)((r & 3) << 12);
        poll_lines();
    }
    for (int i = 0; i < 192; i++) s = s * 31 + map[(i * 4 + pass) & 511];
    return s;
}
/* 2. BG tiles: 16 4bpp tiles built in RAM and copied (LDM/STM) to bank A, one tile row at a time by STR, and
   read back */
static u32 tilebuf[16 * 8];
static NOINLINE u32 tile_upload(u32 pass)
{
    volatile u32 *tiles = (volatile u32 *)0x06000000;
    for (int i = 0; i < 16 * 8; i++) tilebuf[i] = (u32)i * 0x11111111u ^ (pass << (i & 7));
    memcpy((void *)(0x06000000 + ((pass & 3) * 16 * 32)), tilebuf, sizeof(tilebuf));
    for (int i = 0; i < 64; i++) tiles[64 * 8 + i] = tilebuf[i] + pass;
    u32 s = 0;
    for (int i = 0; i < 64; i++) s += tiles[(pass & 3) * 16 * 8 + i * 2] ^ (u32)i;
    return s;
}
/* 3. sprites: OAM A by halfwords, OAM B by memcpy from a shadow, eight sprite tiles in bank B by halfwords */
static u16 oamshadow[128 * 4];
static NOINLINE u32 sprites(u32 pass)
{
    volatile u16 *oam = (volatile u16 *)0x07000000, *obj = (volatile u16 *)0x06400000;
    for (int i = 0; i < 128; i++) {
        u32 x = (u32)i * 13 + pass, y = (u32)i * 7 + pass * 2;
        oam[i * 4 + 0] = (u16)(y & 0x7F) | (i < 64 ? 0 : 0x0200);
        oam[i * 4 + 1] = (u16)(x & 0x1FF);
        oam[i * 4 + 2] = (u16)(i & 7);
        oamshadow[i * 4 + 0] = (u16)(y & 0xFF); oamshadow[i * 4 + 1] = (u16)(x & 0x1FF); oamshadow[i * 4 + 2] = (u16)i;
    }
    memcpy((void *)0x07000400, oamshadow, sizeof(oamshadow));
    for (int i = 0; i < 8 * 16; i++) obj[i] = (u16)((u32)i * 0x1111u + pass);
    u32 s = 0;
    for (int i = 0; i < 32; i++) s += oam[i * 4 + 1] + ((volatile u16 *)0x07000400)[i * 4];
    return s;
}
/* 4. palettes: a BG fade by halfwords, OBJ palette by words, engine B's backdrop; some read back */
static NOINLINE u32 palettes(u32 pass)
{
    volatile u16 *pal = (volatile u16 *)0x05000000; volatile u32 *opal = (volatile u32 *)0x05000200;
    for (int i = 1; i < 256; i++) pal[i] = (u16)(((u32)i * 0x421 + pass) & 0x7FFF);
    for (int i = 0; i < 8; i++) opal[i] = ((u32)i * 0x04210421u + pass * 0x10001u) & 0x7FFF7FFF;
    u32 s = 0;
    for (int i = 0; i < 16; i++) s += pal[i * 16 + 1] ^ opal[i & 7];
    return s;
}
/* 5. the divider (32/32, 64/32, 64/64) and the square root unit, polling their busy bits */
static NOINLINE u32 maths(u32 pass)
{
    u32 s = 0;
    for (int i = 0; i < 48; i++) {
        R16(0x04000280) = 0; R32(0x04000290) = pass * 1000 + (u32)i * 77; R32(0x04000298) = (u32)i + 3;
        while (R16(0x04000280) & 0x8000) ;
        s += R32(0x040002A0) + R32(0x040002A8);
    }
    for (int i = 0; i < 8; i++) {
        R16(0x04000280) = 1; *(volatile u64 *)0x04000290 = (u64)pass << 33 | (u32)i; R32(0x04000298) = (u32)i * 1000 + 7;
        while (R16(0x04000280) & 0x8000) ;
        s += R32(0x040002A0);
    }
    for (int i = 0; i < 8; i++) {
        R16(0x04000280) = 2; *(volatile u64 *)0x04000290 = (u64)(pass + 9) << 40; *(volatile u64 *)0x04000298 = (u64)(i + 1) << 20;
        while (R16(0x04000280) & 0x8000) ;
        s += R32(0x040002A0) ^ R32(0x040002A4);
    }
    for (int i = 0; i < 24; i++) {
        R16(0x040002B0) = 0; R32(0x040002B8) = pass * 12345 + (u32)i * 999;
        while (R16(0x040002B0) & 0x8000) ;
        s += R32(0x040002B4);
    }
    for (int i = 0; i < 8; i++) {
        R16(0x040002B0) = 1; *(volatile u64 *)0x040002B8 = (u64)(pass + (u32)i) << 36;
        while (R16(0x040002B0) & 0x8000) ;
        s += R32(0x040002B4);
    }
    return s;
}
/* 6. IPC: four batches of 16 words to the ARM7; it sums each batch with the sync nibble and replies; the ARM9
   halts until the reply is in its FIFO (IME critical sections around the shared state, as drivers do) */
static NOINLINE u32 ipc(u32 pass)
{
    u32 s = 0;
    for (int b = 0; b < 4; b++) {
        u32 old = R32(IME); R32(IME) = 0;
        R16(IPCSYNC) = (u16)(((pass + (u32)b) & 15) << 8);
        R32(IME) = old;
        for (int i = 0; i < 16; i++) R32(IPCFIFOSEND) = pass * 0x9E3779B9u + (u32)(b * 16 + i);
        while (R16(IPCFIFOCNT) & 0x100) {          /* receive FIFO empty */
            halt9();
            R32(IF) = IRQ_IPCRECV;
        }
        s = s * 33 + R32(IPCFIFORECV);
    }
    return s;
}
/* 7. geometry: 24 triangles by single STRs to the command ports, 24 more packed into the GX FIFO by STM */
static u32 gxlist[24 * 8];
static NOINLINE void geometry(u32 pass)
{
    R32(0x04000440) = 0; R32(0x04000454) = 0;      /* MTX_MODE projection, identity */
    R32(0x04000440) = 2; R32(0x04000454) = 0;      /* position & vector, identity */
    R32(0x040004A4) = 0x001F00C0;                  /* POLY_ATTR: opaque, both sides */
    R32(0x04000500) = 0;                           /* BEGIN_VTXS: triangles */
    for (int t = 0; t < 24; t++) {
        s32 x = (s32)((t % 6) * 1300) - 3900 + (s32)(pass & 63) * 8, y = (s32)((t / 6) * 1600) - 3200;
        R32(0x04000480) = ((u32)t * 0x0421u + pass) & 0x7FFF;
        R32(0x0400048C) = ((u32)(y & 0xFFFF) << 16) | (u32)(x & 0xFFFF); R32(0x0400048C) = 0;
        R32(0x0400048C) = ((u32)(y & 0xFFFF) << 16) | (u32)((x + 1000) & 0xFFFF); R32(0x0400048C) = 0;
        R32(0x0400048C) = ((u32)((y + 1200) & 0xFFFF) << 16) | (u32)(x & 0xFFFF); R32(0x0400048C) = 0;
    }
    for (int t = 0; t < 24; t++) {
        s32 x = (s32)((t % 6) * 1300) - 3900, y = (s32)((t / 6) * 1600) + 200 - (s32)(pass & 31) * 8;
        u32 *p = &gxlist[t * 8];
        p[0] = 0x23232320;                         /* COLOR, VTX_16 x3 */
        p[1] = (0x7C00u + (u32)t * 0x21u) & 0x7FFF;
        p[2] = ((u32)(y & 0xFFFF) << 16) | (u32)(x & 0xFFFF); p[3] = 0;
        p[4] = ((u32)((y + 1200) & 0xFFFF) << 16) | (u32)((x + 1000) & 0xFFFF); p[5] = 0;
        p[6] = ((u32)((y + 1200) & 0xFFFF) << 16) | (u32)(x & 0xFFFF); p[7] = 0;
    }
    gx_stm(gxlist, 24 * 8);
    R32(0x04000504) = 0;                           /* END_VTXS */
    R32(0x04000540) = 0;                           /* SWAP_BUFFERS */
}

/* the checksums, drawn as 4x4 blocks into engine B's bitmap (bank C): 9 rows of 32 bits */
static void draw(const u32 *v, int n)
{
    volatile u16 *fb = (volatile u16 *)0x06200000;
    for (int r = 0; r < n; r++)
        for (int b = 0; b < 32; b++) {
            u16 col = (v[r] >> b) & 1 ? 0xFFFF : (u16)(0x8010 + r * 0x0C00);
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++) fb[(8 + r * 6 + y) * 256 + 8 + b * 7 + x] = col;
        }
}

int main(void)
{
    R32(0x04000304) = 0x820F;                       /* POWCNT1: LCDs, 2D A and B, 3D, engine A on top */
    R8(0x04000240) = 0x81;                          /* VRAM A: engine A BG */
    R8(0x04000241) = 0x82;                          /* VRAM B: engine A OBJ */
    R8(0x04000242) = 0x84;                          /* VRAM C: engine B BG */
    R32(0x04000000) = 0x00013318;                   /* DISPCNT A: graphics, BG0 = 3D, BG0, BG1, OBJ (1D), WIN0 */
    R16(0x04000040) = (24 << 8) | 232;              /* WIN0H: x 24..231 (membench-win: a HUD-like window over the 3D scene) */
    R16(0x04000044) = (16 << 8) | 176;              /* WIN0V: y 16..175 */
    R16(0x04000048) = 0x003F;                       /* WININ: inside WIN0 every layer and the colour effect */
    R16(0x0400004A) = 0x0012;                       /* WINOUT: outside only BG1 and OBJ (no 3D) */
    R16(0x0400000A) = 0x1F00;                       /* BG1CNT A: char base 0, map base 31, 16 colours */
    R32(0x04000060) = 0;                            /* DISP3DCNT */
    R32(0x04000350) = 0;                            /* CLEAR_COLOR: transparent (BG1 shows through) */
    R32(0x04000354) = 0x7FFF;                       /* CLEAR_DEPTH */
    R32(0x04000580) = 0xBFFF0000;                   /* VIEWPORT: the screen */
    R32(0x04001000) = 0x00010805;                   /* DISPCNT B: graphics, mode 5, BG3 */
    R16(0x0400100E) = 0x4084;                       /* BG3CNT B: 256x256 direct colour bitmap */
    R16(0x04001030) = 0x100; R16(0x04001032) = 0; R16(0x04001034) = 0; R16(0x04001036) = 0x100;
    R16(DISPSTAT) = 0x0008;                         /* VBlank IRQ */
    R16(IPCFIFOCNT) = 0x8408;                       /* FIFO on, receive-not-empty IRQ, send FIFO cleared */
    R32(IE) = IRQ_VBLANK | IRQ_IPCRECV;
    R32(IF) = ~0u;
    R32(IME) = 1;                                   /* (the CPSR keeps IRQs masked: halts wake, no handler runs) */
    static u32 sums[8], frozen[9];
    u32 pass = 0;
    for (;;) {
        pass++;
        sums[0] += map_update(pass);
        sums[1] ^= tile_upload(pass);
        sums[2] += sprites(pass) + palettes(pass);
        sums[3] += maths(pass);
        sums[4] ^= ipc(pass);
        sums[5] += near_stores(pass);
        geometry(pass);
        sums[6] += R16(KEYINPUT) & 0x3FF;
        sums[7] = sums[7] * 3 + (R32(IE) ^ R32(IME));
        if (pass == 16) for (int i = 0; i < 8; i++) frozen[i + 1] = sums[i];
        frozen[0] = pass;
        if ((pass & 7) == 0) draw(frozen, 9);
        /* wait for VBlank as games do (IntrWait): acknowledge, poll once, halt until it is flagged, acknowledge */
        R32(IF) = IRQ_VBLANK;
        iosink += R16(DISPSTAT) & 1;
        while (!(R32(IF) & IRQ_VBLANK)) halt9();
        R32(IF) = IRQ_VBLANK;
    }
}
