// quarters_test: render.2d_quarters' split painter's pass (CompositeRange below / above + MergeQuarter) against the
// full sequential pass (CompositeLoop) per quarter, on random layer stacks. The code is copied from
// gpu2d/GPU2D_Banana.cpp (perf/render @ the 2d_quarters commit). Prints the number of mismatches; exit 1 on any.
#include <arm_neon.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <utility>
typedef uint32_t u32; typedef uint8_t u8;
typedef uint32x4_t V4;
static inline V4 VLoad(const u32* p) { return vld1q_u32(p); }
static inline void VStore(u32* p, V4 v) { vst1q_u32(p, v); }
static inline V4 VDup(u32 x) { return vdupq_n_u32(x); }
static inline V4 VZero() { return vdupq_n_u32(0); }
static inline V4 VNonZero(V4 a) { return vtstq_u32(a, a); }
static inline V4 VEq(V4 a, V4 b) { return vceqq_u32(a, b); }
static inline V4 VAnd(V4 a, V4 b) { return vandq_u32(a, b); }
static inline V4 VOr(V4 a, V4 b) { return vorrq_u32(a, b); }
static inline V4 VSel(V4 m, V4 a, V4 b) { return vbslq_u32(m, a, b); }
struct CompStep { const u32* line; const u32* prio; u32 prioval; u32 priomask; bool raw3d; };
alignas(16) static const u32 ZeroLine[256] = {};

template<int N, bool needSecond, bool has3d, bool onBackdrop>
static void CompositeLoop(const CompStep* steps, const u32* startLine, u32* outTop, u32* outSecond)
{
    const u32* line[N > 0 ? N : 1]; const u32* prio[N > 0 ? N : 1];
    V4 prioval[N > 0 ? N : 1]; V4 priomask[N > 0 ? N : 1]; V4 orv[N > 0 ? N : 1];
    for (int k = 0; k < N; k++) { line[k] = steps[k].line; prio[k] = steps[k].prio; prioval[k] = VDup(steps[k].prioval); priomask[k] = VDup(steps[k].priomask); orv[k] = VDup(steps[k].raw3d ? 0x40000000 : 0); }
    const V4 flagbyte = VDup(0xFF000000);
    const V4 backdrop = VDup(startLine[0]);
    for (int i = 0; i < 256; i += 4)
    {
        V4 top = onBackdrop ? backdrop : VLoad(startLine + i);
        V4 sec = VZero();
        for (int k = 0; k < N; k++)
        {
            V4 c = VLoad(line[k] + i); V4 m;
            if (has3d) { m = VNonZero(VAnd(c, flagbyte)); c = VOr(c, orv[k]); } else m = VNonZero(c);
            if (prio[k]) m = VAnd(m, VEq(VAnd(VLoad(prio[k] + i), priomask[k]), prioval[k]));
            if (needSecond) sec = VSel(m, top, sec);
            top = VSel(m, c, top);
        }
        VStore(outTop + i, top);
        if (needSecond) VStore(outSecond + i, sec);
    }
}
template<int N, bool needSecond, bool has3d>
static void CompositeN(const CompStep* steps, const u32* startLine, bool onBackdrop, u32* outTop, u32* outSecond)
{
    if (onBackdrop) CompositeLoop<N, needSecond, has3d, true>(steps, startLine, outTop, outSecond);
    else            CompositeLoop<N, needSecond, has3d, false>(steps, startLine, outTop, outSecond);
}
static void CompositeRange(const CompStep* s, int cnt, const u32* startLine, bool onBackdrop, bool needSecond, bool has3d, u32* outTop, u32* outSecond)
{
    switch (cnt)
    {
#define BANANA_COMP(N) case N: \
        if (has3d) { if (needSecond) CompositeN<N, true, true>(s, startLine, onBackdrop, outTop, outSecond); else CompositeN<N, false, true>(s, startLine, onBackdrop, outTop, outSecond); } \
        else       { if (needSecond) CompositeN<N, true, false>(s, startLine, onBackdrop, outTop, outSecond); else CompositeN<N, false, false>(s, startLine, onBackdrop, outTop, outSecond); } \
        break;
    BANANA_COMP(0) BANANA_COMP(1) BANANA_COMP(2) BANANA_COMP(3)
    BANANA_COMP(4) BANANA_COMP(5) BANANA_COMP(6) BANANA_COMP(7) BANANA_COMP(8)
    default: break;
#undef BANANA_COMP
    }
}
template<bool needSecond, bool raw, bool hasAbove, int belowKind>
static void MergeQuarter(const u32* c3line, const u32* base, const u32* belowTop, const u32* belowSec, const u32* aboveTop, const u32* aboveSec, u32* outTop, u32* outSecond)
{
    const V4 flagbyte = VDup(0xFF000000), orv = VDup(0x40000000);
    const V4 backdrop = VDup(base[0]);
    for (int i = 0; i < 256; i += 4)
    {
        V4 c = VLoad(c3line + i); V4 m3;
        if (raw) { m3 = VNonZero(VAnd(c, flagbyte)); c = VOr(c, orv); } else m3 = VNonZero(c);
        V4 bt = belowKind == 0 ? backdrop : belowKind == 1 ? VLoad(base + i) : VLoad(belowTop + i);
        V4 L = VSel(m3, c, bt);
        V4 top = L, ma = VZero();
        if (hasAbove) { V4 at = VLoad(aboveTop + i); ma = VNonZero(at); top = VSel(ma, at, L); }
        VStore(outTop + i, top);
        if (needSecond)
        {
            V4 bs = belowKind == 2 ? VLoad(belowSec + i) : VZero();
            V4 Ls = VSel(m3, bt, bs);
            V4 sec = Ls;
            if (hasAbove) { V4 as = VLoad(aboveSec + i); sec = VSel(ma, VSel(VNonZero(as), as, L), Ls); }
            VStore(outSecond + i, sec);
        }
    }
}
template<bool needSecond, bool raw>
static void MergeQuarterN(bool hasAbove, int belowKind, const u32* c3line, const u32* base, const u32* belowTop, const u32* belowSec, const u32* aboveTop, const u32* aboveSec, u32* outTop, u32* outSecond)
{
    if (hasAbove)
    {
        if (belowKind == 0)      MergeQuarter<needSecond, raw, true, 0>(c3line, base, belowTop, belowSec, aboveTop, aboveSec, outTop, outSecond);
        else if (belowKind == 1) MergeQuarter<needSecond, raw, true, 1>(c3line, base, belowTop, belowSec, aboveTop, aboveSec, outTop, outSecond);
        else                     MergeQuarter<needSecond, raw, true, 2>(c3line, base, belowTop, belowSec, aboveTop, aboveSec, outTop, outSecond);
    }
    else
    {
        if (belowKind == 0)      MergeQuarter<needSecond, raw, false, 0>(c3line, base, belowTop, belowSec, aboveTop, aboveSec, outTop, outSecond);
        else if (belowKind == 1) MergeQuarter<needSecond, raw, false, 1>(c3line, base, belowTop, belowSec, aboveTop, aboveSec, outTop, outSecond);
        else                     MergeQuarter<needSecond, raw, false, 2>(c3line, base, belowTop, belowSec, aboveTop, aboveSec, outTop, outSecond);
    }
}

alignas(16) static u32 layer[8][256], prioline[256], backdrop[256], q3d[4][256];
alignas(16) static u32 refTop[256], refSec[256], belowTop[256], belowSec[256], aboveTop[256], aboveSec[256], outTop[256], outSec[256];
static u32 seed = 1;
static u32 rnd() { seed = seed * 1103515245u + 12345u; return seed >> 8; }

int main(int argc, char** argv)
{
    const int iters = argc > 1 ? atoi(argv[1]) : 2000;
    int bad = 0, cases = 0;
    for (int it = 0; it < iters; it++)
    {
        const int n = rnd() % 9;                    // steps
        const bool needSecond = rnd() & 1;
        const bool raw = rnd() & 1;
        const int density = 1 + rnd() % 4;          // opaque pixels: 1 in `density`
        CompStep steps[8]; u8 full[8];
        int bg0 = -1;
        for (int k = 0; k < n; k++)
        {
            const bool obj = (rnd() % 4) == 0;
            const u32 flag = obj ? 0x80 : (1u << (rnd() % 4));
            for (int i = 0; i < 256; i++) layer[k][i] = (rnd() % density) == 0 ? ((rnd() & 0x3F3F3F) | flag << 24) : 0;
            full[k] = 0;
            if (!obj && (rnd() % 5) == 0) { for (int i = 0; i < 256; i++) layer[k][i] = (rnd() & 0x3F3F3F) | flag << 24; full[k] = 1; }
            steps[k] = { layer[k], obj && (rnd() & 1) ? prioline : nullptr, (rnd() % 4) << 16, 0x30000, false };
        }
        for (int i = 0; i < 256; i++) prioline[i] = (rnd() % 4) << 16;
        for (int i = 0; i < 256; i++) backdrop[i] = 0x0F0F0F | 0xFF000000;
        // the 3D step: one of the BG steps, with a line per quarter (raw: alpha byte; else DrawBG_3D's conversion)
        int bgsteps = 0;
        for (int k = 0; k < n; k++) bgsteps += !steps[k].prio;
        if (bgsteps && (rnd() % 8))
        {
            do { bg0 = rnd() % n; } while (steps[bg0].prio);   // (a BG step exists: this ends)
            full[bg0] = 0;
            steps[bg0].raw3d = raw;
            for (int q = 0; q < 4; q++)
                for (int i = 0; i < 256; i++)
                {
                    const u32 a = (rnd() % density) == 0 ? (1 + rnd() % 31) : 0;
                    q3d[q][i] = raw ? ((rnd() & 0x3F3F3F) | a << 24) : (a ? ((rnd() & 0x3F3F3F) | a << 24 | 0x40000000) : 0);
                }
        }
        // start, as BuildSteps
        int start = 0, wanted = needSecond ? 2 : 1, found = 0; const u32* startLine = backdrop;
        for (int k = n - 1; k >= 0; k--) if (full[k]) { found++; if (found == wanted) { start = k + 1; startLine = steps[k].line; break; } }
        const bool onBackdrop = startLine == backdrop && (rnd() & 1);   // (as Banana2DFast off: the base read as a line)
        // reference: the full pass per quarter
        for (int q = 0; q < 4; q++)
        {
            CompStep s2[8]; memcpy(s2, steps, sizeof s2);
            if (bg0 >= 0) s2[bg0].line = q3d[q];
            bool has3d = false; for (int k = start; k < n; k++) has3d |= s2[k].raw3d;
            memset(refTop, 0xAA, sizeof refTop); memset(refSec, 0xAA, sizeof refSec);
            CompositeRange(s2 + start, n - start, startLine, onBackdrop, needSecond, has3d, refTop, refSec);
            // the split
            memset(outTop, 0xAA, sizeof outTop); memset(outSec, 0xAA, sizeof outSec);
            if (bg0 < start)
            {
                CompositeRange(steps + start, n - start, startLine, onBackdrop, needSecond, false, outTop, outSec);
            }
            else
            {
                const int nBelow = bg0 - start, nAbove = n - bg0 - 1;
                memset(belowTop, 0xBB, sizeof belowTop); memset(belowSec, 0xBB, sizeof belowSec); memset(aboveTop, 0xBB, sizeof aboveTop); memset(aboveSec, 0xBB, sizeof aboveSec);
                if (nBelow) CompositeRange(steps + start, nBelow, startLine, onBackdrop, needSecond, false, belowTop, belowSec);
                if (nAbove) CompositeRange(steps + bg0 + 1, nAbove, ZeroLine, false, needSecond, false, aboveTop, aboveSec);
                const int belowKind = nBelow ? 2 : onBackdrop ? 0 : 1;
                const u32* c3 = q3d[q];
                if (needSecond) { if (raw) MergeQuarterN<true, true>(nAbove > 0, belowKind, c3, startLine, belowTop, belowSec, aboveTop, aboveSec, outTop, outSec); else MergeQuarterN<true, false>(nAbove > 0, belowKind, c3, startLine, belowTop, belowSec, aboveTop, aboveSec, outTop, outSec); }
                else            { if (raw) MergeQuarterN<false, true>(nAbove > 0, belowKind, c3, startLine, belowTop, belowSec, aboveTop, aboveSec, outTop, nullptr); else MergeQuarterN<false, false>(nAbove > 0, belowKind, c3, startLine, belowTop, belowSec, aboveTop, aboveSec, outTop, nullptr); }
            }
            cases++;
            if (memcmp(refTop, outTop, sizeof refTop) || (needSecond && memcmp(refSec, outSec, sizeof refSec)))
            {
                bad++;
                if (bad < 5) printf("mismatch: it %d q %d n %d bg0 %d start %d needSecond %d raw %d\n", it, q, n, bg0, start, needSecond, raw);
            }
        }
    }
    printf("%d quarter cases, %d mismatches\n", cases, bad);
    return bad != 0;
}
