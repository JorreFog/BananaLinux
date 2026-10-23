// win_test: render.2d_win_neon's vector loops against the scalar ones they replace, on random inputs:
//   ApplyWindow (a layer line masked by the window mask's bit), DrawBG_3D (the 3D line converted to a BG line), and
//   the quarters' merge with the window applied inside (MergeQuarter<..., win>) against DrawBG_3D + ApplyWindow +
//   MergeQuarter<raw = false>. The code is copied from gpu2d/GPU2D_Banana.cpp (perf/render @ the 2d_win_neon commit).
// Prints the number of mismatches; exit 1 on any.
#include <arm_neon.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
typedef uint32_t u32; typedef uint8_t u8;
typedef uint32x4_t V4;
static inline V4 VLoad(const u32* p) { return vld1q_u32(p); }
static inline void VStore(u32* p, V4 v) { vst1q_u32(p, v); }
static inline V4 VDup(u32 x) { return vdupq_n_u32(x); }
static inline V4 VZero() { return vdupq_n_u32(0); }
static inline V4 VNonZero(V4 a) { return vtstq_u32(a, a); }
static inline V4 VAnd(V4 a, V4 b) { return vandq_u32(a, b); }
static inline V4 VOr(V4 a, V4 b) { return vorrq_u32(a, b); }
static inline V4 VSel(V4 m, V4 a, V4 b) { return vbslq_u32(m, a, b); }
static inline V4 VFromBytes4(u32 x) { return vmovl_u16(vget_low_u16(vmovl_u8(vreinterpret_u8_u32(vdup_n_u32(x))))); }

// ---- the scalar loops (the base) ----
static void ApplyWindowScalar(u32* line, const u8* WindowMask, u8 bit)
{
    for (int i = 0; i < 256; i++)
        line[i] = (WindowMask[i] & bit) ? line[i] : 0;
}
static void DrawBG_3DScalar(u32* out, const u32* src)
{
    for (int i = 0; i < 256; i++)
    {
        u32 c = src[i];
        out[i] = (c >> 24) ? (c | 0x40000000) : 0;
    }
}

// ---- render.2d_win_neon ----
static inline void WindowLanes(const u8* wm, uint8x16_t vbit, uint32x4_t& w0, uint32x4_t& w1, uint32x4_t& w2, uint32x4_t& w3)
{
    uint8x16_t m = vtstq_u8(vld1q_u8(wm), vbit);
    uint8x16_t m0 = vzip1q_u8(m, m), m1 = vzip2q_u8(m, m);
    w0 = vreinterpretq_u32_u8(vzip1q_u8(m0, m0));
    w1 = vreinterpretq_u32_u8(vzip2q_u8(m0, m0));
    w2 = vreinterpretq_u32_u8(vzip1q_u8(m1, m1));
    w3 = vreinterpretq_u32_u8(vzip2q_u8(m1, m1));
}
static void ApplyWindowNeon(u32* line, const u8* WindowMask, u8 bit)
{
    const uint8x16_t vbit = vdupq_n_u8(bit);
    for (int i = 0; i < 256; i += 16)
    {
        uint32x4_t w0, w1, w2, w3;
        WindowLanes(&WindowMask[i], vbit, w0, w1, w2, w3);
        uint32x4x4_t v = vld1q_u32_x4(&line[i]);
        v.val[0] = vandq_u32(v.val[0], w0);
        v.val[1] = vandq_u32(v.val[1], w1);
        v.val[2] = vandq_u32(v.val[2], w2);
        v.val[3] = vandq_u32(v.val[3], w3);
        vst1q_u32_x4(&line[i], v);
    }
}
static void DrawBG_3DNeon(u32* out, const u32* src)
{
    const V4 flagbyte = VDup(0xFF000000), orv = VDup(0x40000000);
    for (int i = 0; i < 256; i += 4)
    {
        V4 c = VLoad(src + i);
        VStore(out + i, VAnd(VOr(c, orv), VNonZero(VAnd(c, flagbyte))));
    }
}

template<bool needSecond, bool raw, bool hasAbove, int belowKind, bool win>
static void MergeQuarter(const u32* c3line, const u8* wm, const u32* base, const u32* belowTop, const u32* belowSec, const u32* aboveTop, const u32* aboveSec, u32* outTop, u32* outSecond)
{
    const V4 flagbyte = VDup(0xFF000000), orv = VDup(0x40000000), one = VDup(1);
    const V4 backdrop = VDup(base[0]);
    for (int i = 0; i < 256; i += 4)
    {
        V4 c = VLoad(c3line + i);
        V4 m3;
        if (raw) { m3 = VNonZero(VAnd(c, flagbyte)); c = VOr(c, orv); }
        else     m3 = VNonZero(c);
        if (win) m3 = VAnd(m3, VNonZero(VAnd(VFromBytes4(*(const u32*)&wm[i]), one)));
        V4 bt = belowKind == 0 ? backdrop : belowKind == 1 ? VLoad(base + i) : VLoad(belowTop + i);
        V4 L = VSel(m3, c, bt);
        V4 top = L, ma = VZero();
        if (hasAbove)
        {
            V4 at = VLoad(aboveTop + i);
            ma = VNonZero(at);
            top = VSel(ma, at, L);
        }
        VStore(outTop + i, top);
        if (needSecond)
        {
            V4 bs = belowKind == 2 ? VLoad(belowSec + i) : VZero();
            V4 Ls = VSel(m3, bt, bs);
            V4 sec = Ls;
            if (hasAbove)
            {
                V4 as = VLoad(aboveSec + i);
                sec = VSel(ma, VSel(VNonZero(as), as, L), Ls);
            }
            VStore(outSecond + i, sec);
        }
    }
}

static u32 rnd() { static u32 s = 0x9E3779B9u; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
// a pixel as the layers hold them: 18-bit colour, flag byte (0 = transparent); a 3D pixel: colour, alpha byte 0..31
static u32 rndpix(int zeroChance) { return (rnd() % 100 < (u32)zeroChance) ? 0 : ((rnd() & 0x3F3F3F) | ((rnd() & 0x3F | 1) << 24)); }
static u32 rnd3d(int zeroChance) { return (rnd() % 100 < (u32)zeroChance) ? (rnd() & 0x00FFFFFF) : ((rnd() & 0x3F3F3F) | ((rnd() % 31 + 1) << 24)); }

alignas(16) static u32 c3[256], base[256], belowTop[256], belowSec[256], aboveTop[256], aboveSec[256];
alignas(16) static u32 conv[256], line[256], lineRef[256], outA[256], outB[256], secA[256], secB[256];
alignas(16) static u8 wm[256];

template<bool needSecond, bool hasAbove, int belowKind>
static int caseMerge()
{
    // the reference: DrawBG_3D (scalar), ApplyWindow (scalar), the merge without the window on the converted line
    DrawBG_3DScalar(conv, c3);
    ApplyWindowScalar(conv, wm, 1);
    memset(secA, 0, sizeof(secA)); memset(secB, 0, sizeof(secB));
    MergeQuarter<needSecond, false, hasAbove, belowKind, false>(conv, nullptr, base, belowTop, belowSec, aboveTop, aboveSec, outA, secA);
    MergeQuarter<needSecond, true, hasAbove, belowKind, true>(c3, wm, base, belowTop, belowSec, aboveTop, aboveSec, outB, secB);
    int bad = memcmp(outA, outB, sizeof(outA)) != 0;
    if (needSecond) bad |= memcmp(secA, secB, sizeof(secA)) != 0;
    return bad;
}

int main(int argc, char** argv)
{
    int cases = argc > 1 ? atoi(argv[1]) : 5000;
    int badWin = 0, bad3D = 0, badMerge = 0;
    for (int n = 0; n < cases; n++)
    {
        int z = rnd() % 100;
        for (int i = 0; i < 256; i++)
        {
            c3[i] = rnd3d(z); base[i] = rndpix(0); belowTop[i] = rndpix(z); belowSec[i] = rndpix(z);
            aboveTop[i] = rndpix(z); aboveSec[i] = rndpix(z); line[i] = rndpix(z);
            wm[i] = (rnd() % 4 == 0) ? (rnd() & 0xFF) : (rnd() & 1 ? 0x3F : 0x00);
        }
        if (n % 8 == 0) for (int i = 0; i < 256; i++) base[i] = base[0];   // a constant backdrop
        // ApplyWindow for every bit a layer can use
        for (u8 bit = 1; bit < 0x20; bit <<= 1)
        {
            memcpy(lineRef, line, sizeof(line)); memcpy(outA, line, sizeof(line));
            ApplyWindowScalar(lineRef, wm, bit); ApplyWindowNeon(outA, wm, bit);
            if (memcmp(lineRef, outA, sizeof(line))) badWin++;
        }
        DrawBG_3DScalar(outA, c3); DrawBG_3DNeon(outB, c3);
        if (memcmp(outA, outB, sizeof(outA))) bad3D++;
        badMerge += caseMerge<false, false, 0>() + caseMerge<false, false, 1>() + caseMerge<false, false, 2>()
                  + caseMerge<false, true, 0>()  + caseMerge<false, true, 1>()  + caseMerge<false, true, 2>()
                  + caseMerge<true, false, 0>()  + caseMerge<true, false, 1>()  + caseMerge<true, false, 2>()
                  + caseMerge<true, true, 0>()   + caseMerge<true, true, 1>()   + caseMerge<true, true, 2>();
    }
    printf("win_test: %d cases: ApplyWindow mismatches %d (of %d), DrawBG_3D mismatches %d, merge-with-window mismatches %d (of %d)\n",
           cases, badWin, cases * 5, bad3D, badMerge, cases * 12);
    return (badWin | bad3D | badMerge) ? 1 : 0;
}
