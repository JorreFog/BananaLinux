// timed()'s unit loop (Run9GX, render.gx_dmatime) with and without the 4-unit step, on random states
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <random>
typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64;
static u32 timed(bool gxtime, const u8* tbm, const u8* tab, bool burststart, u32 limit, u64 ts0, u64 target, int clockshift, u32 cnt0, u64& ts, u32& cnt, bool& cur)
{
    ts = ts0; cnt = cnt0; cur = false;
    if (limit == 0) return 0;
    const u8* tb = tbm;
    if (burststart || tb[cnt] == 0) { cnt = 0; cur = true; tb = tab; }
    ts += (u64)tb[cnt++] << clockshift;
    u32 k = 1;
    if (ts >= target) return k;
    if (gxtime)
    {
        while (k + 4 <= limit && cnt + 4 <= 256)
        {
            u32 w; memcpy(&w, tb + cnt, 4);
            if (((w - 0x01010101u) & ~w & 0x80808080u) | (w & 0xC0C0C0C0u)) break;
            const u64 ts4 = ts + ((u64)((w * 0x01010101u) >> 24) << clockshift);
            if (ts4 >= target) break;
            ts = ts4; cnt += 4; k += 4;
        }
    }
    while (k < limit)
    {
        u32 t = tb[cnt];
        if (t == 0) { cnt = 0; cur = true; tb = tab; t = tb[0]; }
        cnt++; ts += (u64)t << clockshift; k++;
        if (ts >= target) break;
    }
    return k;
}
int main()
{
    std::mt19937_64 r(12345); long bad = 0, n = 0;
    static u8 tabs[4][257];
    for (long it = 0; it < 4000000; it++)
    {
        if (it % 1000 == 0)
            for (int t = 0; t < 4; t++)
            {
                // a table: random small entries (some large) and a 0 end at a random place (always one by 255)
                int len = 1 + r() % 255; int big = r() % 3;
                for (int i = 0; i < 256; i++) tabs[t][i] = big == 0 ? (u8)(1 + r() % 255) : big == 1 ? (u8)(1 + r() % 14) : (u8)(i == 0 ? 9 : 2);
                if (r() % 4 == 0) for (int i = 0; i < 256; i++) if (r() % 50 == 0) tabs[t][i] = 0;
                tabs[t][len] = 0; tabs[t][255] = 0; tabs[t][256] = 0;
            }
        const u8* tbm = tabs[r() % 4]; const u8* tab = tabs[r() % 4];
        // a wrapped table must not start with 0 (the static tables don't): keep tab[0] != 0
        if (tab[0] == 0) continue;
        bool bs = r() % 8 == 0; u32 limit = r() % 520; u32 cnt0 = r() % 256; int sh = r() % 3;
        u64 ts0 = r() % 100000; u64 target = ts0 + (r() % 4 == 0 ? r() % 20 : r() % 6000);
        u64 a1, a2; u32 c1, c2; bool cu1, cu2;
        u32 k1 = timed(false, tbm, tab, bs, limit, ts0, target, sh, cnt0, a1, c1, cu1);
        u32 k2 = timed(true, tbm, tab, bs, limit, ts0, target, sh, cnt0, a2, c2, cu2);
        n++;
        if (k1 != k2 || a1 != a2 || c1 != c2 || cu1 != cu2) { if (bad < 5) printf("diff it %ld\n", it); bad++; }
    }
    printf("cases %ld bad %ld\n", n, bad);
}
