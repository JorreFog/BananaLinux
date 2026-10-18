// comp_bench: BananaRenderer2D's CompositeLoop (verbatim from gpu2d/GPU2D_Banana.cpp at 00c34c78e8de) against
// restructured variants, at the device's -O2, counted under qemu (jitcount per symbol). Each scenario's call sits in
// a noinline function of its own, so the plugin attributes the loop's instructions to it.
#include <arm_neon.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <utility>
typedef uint32_t u32;
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

struct CompStep
{
    const u32* line;
    const u32* prio;
    u32 prioval;
    u32 priomask;
    bool raw3d;
};

// ---- A: verbatim ----
template<int N, bool needSecond, bool has3d, bool onBackdrop>
static void CompositeLoopA(const CompStep* steps, const u32* startLine, u32* outTop, u32* outSecond)
{
    const u32* line[N > 0 ? N : 1];
    const u32* prio[N > 0 ? N : 1];
    V4 prioval[N > 0 ? N : 1];
    V4 priomask[N > 0 ? N : 1];
    V4 orv[N > 0 ? N : 1];
    for (int k = 0; k < N; k++)
    {
        line[k] = steps[k].line;
        prio[k] = steps[k].prio;
        prioval[k] = VDup(steps[k].prioval);
        priomask[k] = VDup(steps[k].priomask);
        orv[k] = VDup(steps[k].raw3d ? 0x40000000 : 0);
    }
    const V4 flagbyte = VDup(0xFF000000);
    const V4 backdrop = VDup(startLine[0]);
    for (int i = 0; i < 256; i += 4)
    {
        V4 top = onBackdrop ? backdrop : VLoad(startLine + i);
        V4 sec = VZero();
        for (int k = 0; k < N; k++)
        {
            V4 c = VLoad(line[k] + i);
            V4 m;
            if (has3d)
            {
                m = VNonZero(VAnd(c, flagbyte));
                c = VOr(c, orv[k]);
            }
            else
                m = VNonZero(c);
            if (prio[k])
                m = VAnd(m, VEq(VAnd(VLoad(prio[k] + i), priomask[k]), prioval[k]));
            if (needSecond)
                sec = VSel(m, top, sec);
            top = VSel(m, c, top);
        }
        VStore(outTop + i, top);
        if (needSecond)
            VStore(outSecond + i, sec);
    }
}

// ---- C: the step loop unrolled by a fold over the steps (each step's constants are its own variables, so they
// can live in registers at -O2 as well), scalars dup'd, no V4 arrays ----
template<int K, bool needSecond, bool has3d>
static inline void CompStepC(const u32* line, const u32* prio, V4 prioval, V4 priomask, V4 orv, V4 flagbyte, int i, V4& top, V4& sec)
{
    V4 c = VLoad(line + i);
    V4 m;
    if (has3d)
    {
        m = VNonZero(VAnd(c, flagbyte));
        c = VOr(c, orv);
    }
    else
        m = VNonZero(c);
    if (prio)
        m = VAnd(m, VEq(VAnd(VLoad(prio + i), priomask), prioval));
    if (needSecond)
        sec = VSel(m, top, sec);
    top = VSel(m, c, top);
}

template<int N, bool needSecond, bool has3d, bool onBackdrop, size_t... K>
static void CompositeLoopC_(const CompStep* steps, const u32* startLine, u32* outTop, u32* outSecond, std::index_sequence<K...>)
{
    const u32* line[N > 0 ? N : 1] = { steps[K].line... };
    const u32* prio[N > 0 ? N : 1] = { steps[K].prio... };
    const V4 prioval[N > 0 ? N : 1] = { VDup(steps[K].prioval)... };
    const V4 priomask[N > 0 ? N : 1] = { VDup(steps[K].priomask)... };
    const V4 orv[N > 0 ? N : 1] = { VDup(steps[K].raw3d ? 0x40000000u : 0u)... };
    const V4 flagbyte = VDup(0xFF000000);
    const V4 backdrop = VDup(startLine[0]);
    for (int i = 0; i < 256; i += 4)
    {
        V4 top = onBackdrop ? backdrop : VLoad(startLine + i);
        V4 sec = VZero();
        (CompStepC<K, needSecond, has3d>(line[K], prio[K], prioval[K], priomask[K], orv[K], flagbyte, i, top, sec), ...);
        VStore(outTop + i, top);
        if (needSecond)
            VStore(outSecond + i, sec);
    }
}
template<int N, bool needSecond, bool has3d, bool onBackdrop>
static void CompositeLoopC(const CompStep* steps, const u32* startLine, u32* outTop, u32* outSecond)
{
    CompositeLoopC_<N, needSecond, has3d, onBackdrop>(steps, startLine, outTop, outSecond, std::make_index_sequence<N>{});
}

// ---- D: as C, but the sprite steps' priority test only when the line has one (the branch per step hoisted:
// two loops, chosen once per step configuration) -- here simply C with prio known null at compile time for BG steps
// is not possible at runtime; D keeps C's shape and drops the has3d AND for steps that are not raw3d (orv == 0):
// the test VNonZero(c & flagbyte) equals VNonZero(c) for a layer line (opaque pixels carry a flag byte) ----
template<bool needSecond, bool has3d>
static inline void CompStepD(const u32* line, const u32* prio, V4 prioval, V4 priomask, bool raw, V4 flagbyte, int i, V4& top, V4& sec)
{
    V4 c = VLoad(line + i);
    V4 m;
    if (has3d && raw)
    {
        m = VNonZero(VAnd(c, flagbyte));
        c = VOr(c, VDup(0x40000000));
    }
    else
        m = VNonZero(c);
    if (prio)
        m = VAnd(m, VEq(VAnd(VLoad(prio + i), priomask), prioval));
    if (needSecond)
        sec = VSel(m, top, sec);
    top = VSel(m, c, top);
}
template<int N, bool needSecond, bool has3d, bool onBackdrop, size_t... K>
static void CompositeLoopD_(const CompStep* steps, const u32* startLine, u32* outTop, u32* outSecond, std::index_sequence<K...>)
{
    const u32* line[N > 0 ? N : 1] = { steps[K].line... };
    const u32* prio[N > 0 ? N : 1] = { steps[K].prio... };
    const V4 prioval[N > 0 ? N : 1] = { VDup(steps[K].prioval)... };
    const V4 priomask[N > 0 ? N : 1] = { VDup(steps[K].priomask)... };
    const bool raw[N > 0 ? N : 1] = { steps[K].raw3d... };
    const V4 flagbyte = VDup(0xFF000000);
    const V4 backdrop = VDup(startLine[0]);
    for (int i = 0; i < 256; i += 4)
    {
        V4 top = onBackdrop ? backdrop : VLoad(startLine + i);
        V4 sec = VZero();
        (CompStepD<needSecond, has3d>(line[K], prio[K], prioval[K], priomask[K], raw[K], flagbyte, i, top, sec), ...);
        VStore(outTop + i, top);
        if (needSecond)
            VStore(outSecond + i, sec);
    }
}
template<int N, bool needSecond, bool has3d, bool onBackdrop>
static void CompositeLoopD(const CompStep* steps, const u32* startLine, u32* outTop, u32* outSecond)
{
    CompositeLoopD_<N, needSecond, has3d, onBackdrop>(steps, startLine, outTop, outSecond, std::make_index_sequence<N>{});
}

alignas(16) static u32 lines[6][256], objprio[256], backdrop[256], outT[256], outS[256];
static CompStep steps[8];

#define SCEN(name, V, N, S, H, B) \
    __attribute__((noinline)) void name() { CompositeLoop##V<N, S, H, B>(steps, backdrop, outT, outS); }
SCEN(A_n2, A, 2, false, false, true) SCEN(C_n2, C, 2, false, false, true) SCEN(D_n2, D, 2, false, false, true)
SCEN(A_n3s, A, 3, true, false, true) SCEN(C_n3s, C, 3, true, false, true) SCEN(D_n3s, D, 3, true, false, true)
SCEN(A_n4h, A, 4, false, true, true) SCEN(C_n4h, C, 4, false, true, true) SCEN(D_n4h, D, 4, false, true, true)
SCEN(A_n4hs, A, 4, true, true, true) SCEN(C_n4hs, C, 4, true, true, true) SCEN(D_n4hs, D, 4, true, true, true)
SCEN(A_n1, A, 1, false, false, false) SCEN(C_n1, C, 1, false, false, false) SCEN(D_n1, D, 1, false, false, false)

int main(int argc, char** argv)
{
    u32 seed = 12345;
    auto rnd = [&] { seed = seed * 1103515245u + 12345u; return seed >> 8; };
    for (int l = 0; l < 6; l++)
        for (int i = 0; i < 256; i++) lines[l][i] = (rnd() & 1) ? (rnd() & 0x3F3F3F) | ((0x01 << l) << 24) : 0;
    for (int i = 0; i < 256; i++) { objprio[i] = (rnd() & 3) << 16; backdrop[i] = 0x0F0F0F | 0xFF000000; }
    // steps: BG3, BG2 (prio null), OBJ step with prio line, BG0 raw 3D
    steps[0] = { lines[3], nullptr, 0, 0, false };
    steps[1] = { lines[2], nullptr, 0, 0, false };
    steps[2] = { lines[4], objprio, 1u << 16, 0x30000, false };
    steps[3] = { lines[0], nullptr, 0, 0, true };
    const int reps = argc > 1 ? atoi(argv[1]) : 1000;
    u32 sum = 0;
    for (int r = 0; r < reps; r++)
    {
        A_n2(); sum += outT[r & 255]; C_n2(); sum += outT[r & 255]; D_n2(); sum += outT[r & 255];
        A_n3s(); sum += outS[r & 255]; C_n3s(); sum += outS[r & 255]; D_n3s(); sum += outS[r & 255];
        A_n4h(); sum += outT[r & 255]; C_n4h(); sum += outT[r & 255]; D_n4h(); sum += outT[r & 255];
        A_n4hs(); sum += outS[r & 255]; C_n4hs(); sum += outS[r & 255]; D_n4hs(); sum += outS[r & 255];
        A_n1(); sum += outT[r & 255]; C_n1(); sum += outT[r & 255]; D_n1(); sum += outT[r & 255];
    }
    // exactness of the variants against A on these inputs
    int bad = 0;
    alignas(16) u32 ta[256], sa[256];
    A_n4hs(); memcpy(ta, outT, sizeof ta); memcpy(sa, outS, sizeof sa);
    C_n4hs(); bad += memcmp(ta, outT, sizeof ta) != 0 || memcmp(sa, outS, sizeof sa) != 0;
    D_n4hs(); bad += memcmp(ta, outT, sizeof ta) != 0 || memcmp(sa, outS, sizeof sa) != 0;
    A_n3s(); memcpy(ta, outT, sizeof ta); memcpy(sa, outS, sizeof sa);
    C_n3s(); bad += memcmp(ta, outT, sizeof ta) != 0 || memcmp(sa, outS, sizeof sa) != 0;
    D_n3s(); bad += memcmp(ta, outT, sizeof ta) != 0 || memcmp(sa, outS, sizeof sa) != 0;
    printf("sum %u, variants differing from A: %d\n", sum, bad);
    return bad;
}
