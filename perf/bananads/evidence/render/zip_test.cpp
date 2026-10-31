// zip_test: render.2d_emit_zip's WriteZipNeon against WriteLineNeon + ZipLine (copied from gpu2d/GPU_Banana.cpp),
// random 18-bit lines, every master-brightness mode and factor. Prints mismatches; exit 1 on any.
#include <arm_neon.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
typedef uint32_t u32; typedef uint8_t u8; typedef uint16_t u16;
static inline uint8x16_t Expand6(uint8x16_t c) { uint8x16_t t = vshlq_n_u8(c, 2); return vsriq_n_u8(t, t, 6); }
template<int mode>
static inline uint8x16_t Bright6(uint8x16_t c, uint8x8_t f)
{
    c = vandq_u8(c, vdupq_n_u8(0x3F));
    if (mode == 1)
    {
        uint8x16_t d = vsubq_u8(vdupq_n_u8(0x3F), c);
        uint8x8_t lo = vshrn_n_u16(vmull_u8(vget_low_u8(d), f), 4);
        uint8x16_t r = vshrn_high_n_u16(lo, vmull_high_u8(d, vcombine_u8(f, f)), 4);
        return vaddq_u8(c, r);
    }
    else
    {
        uint16x8_t bias = vdupq_n_u16(15);
        uint8x8_t lo = vshrn_n_u16(vmlal_u8(bias, vget_low_u8(c), f), 4);
        uint8x16_t r = vshrn_high_n_u16(lo, vmlal_high_u8(bias, c, vcombine_u8(f, f)), 4);
        return vsubq_u8(c, r);
    }
}
template<int mode>
static void WriteLineNeon(u32* dst, const u32* src, u32 factor)
{
    uint8x8_t f = vdup_n_u8((u8)factor);
    for (int i = 0; i < 256; i += 16)
    {
        uint8x16x4_t s = vld4q_u8((const u8*)(src + i));
        uint8x16_t r = s.val[0], g = s.val[1], b = s.val[2];
        if (mode) { r = Bright6<mode>(r, f); g = Bright6<mode>(g, f); b = Bright6<mode>(b, f); }
        uint8x16x4_t o;
        o.val[0] = Expand6(b); o.val[1] = Expand6(g); o.val[2] = Expand6(r); o.val[3] = vdupq_n_u8(0xFF);
        vst4q_u8((u8*)(dst + i), o);
    }
}
static void ZipLine(u32* dst, const u32* even, const u32* odd)
{
    for (int i = 0; i < 256; i += 4)
    {
        uint32x4x2_t z = {{ vld1q_u32(even + i), vld1q_u32(odd + i) }};
        vst2q_u32(dst + 2 * i, z);
    }
}
template<int mode>
static void WriteZipNeon(u32* dst, const u32* even, const u32* odd, u32 factor)
{
    uint8x8_t f = vdup_n_u8((u8)factor);
    for (int i = 0; i < 256; i += 16)
    {
        uint8x16x4_t e = vld4q_u8((const u8*)(even + i));
        uint8x16x4_t o = vld4q_u8((const u8*)(odd + i));
        uint8x16_t er = e.val[0], eg = e.val[1], eb = e.val[2];
        uint8x16_t od = o.val[0], og = o.val[1], ob = o.val[2];
        if (mode)
        {
            er = Bright6<mode>(er, f); eg = Bright6<mode>(eg, f); eb = Bright6<mode>(eb, f);
            od = Bright6<mode>(od, f); og = Bright6<mode>(og, f); ob = Bright6<mode>(ob, f);
        }
        er = Expand6(er); eg = Expand6(eg); eb = Expand6(eb);
        od = Expand6(od); og = Expand6(og); ob = Expand6(ob);
        uint8x16x4_t lo, hi;
        lo.val[0] = vzip1q_u8(eb, ob); hi.val[0] = vzip2q_u8(eb, ob);
        lo.val[1] = vzip1q_u8(eg, og); hi.val[1] = vzip2q_u8(eg, og);
        lo.val[2] = vzip1q_u8(er, od); hi.val[2] = vzip2q_u8(er, od);
        lo.val[3] = vdupq_n_u8(0xFF);
        hi.val[3] = vdupq_n_u8(0xFF);
        vst4q_u8((u8*)(dst + 2 * i), lo);
        vst4q_u8((u8*)(dst + 2 * i + 16), hi);
    }
}
template<int mode>
static void WriteDoubleNeon(u32* dst, const u32* src, u32 factor)
{
    uint8x8_t f = vdup_n_u8((u8)factor);
    for (int i = 0; i < 256; i += 16)
    {
        uint8x16x4_t s = vld4q_u8((const u8*)(src + i));
        uint8x16_t r = s.val[0], g = s.val[1], b = s.val[2];
        if (mode) { r = Bright6<mode>(r, f); g = Bright6<mode>(g, f); b = Bright6<mode>(b, f); }
        r = Expand6(r); g = Expand6(g); b = Expand6(b);
        uint8x16x4_t lo, hi;
        lo.val[0] = vzip1q_u8(b, b); hi.val[0] = vzip2q_u8(b, b);
        lo.val[1] = vzip1q_u8(g, g); hi.val[1] = vzip2q_u8(g, g);
        lo.val[2] = vzip1q_u8(r, r); hi.val[2] = vzip2q_u8(r, r);
        lo.val[3] = vdupq_n_u8(0xFF);
        hi.val[3] = vdupq_n_u8(0xFF);
        vst4q_u8((u8*)(dst + 2 * i), lo);
        vst4q_u8((u8*)(dst + 2 * i + 16), hi);
        vst4q_u8((u8*)(dst + 512 + 2 * i), lo);
        vst4q_u8((u8*)(dst + 512 + 2 * i + 16), hi);
    }
}
static void DoubleLine(u32* dst, const u32* src)
{
    for (int i = 0; i < 256; i += 4)
    {
        uint32x4_t v = vld1q_u32(src + i);
        uint32x4x2_t z = {{ v, v }};
        vst2q_u32(dst + 2 * i, z);
        vst2q_u32(dst + 512 + 2 * i, z);
    }
}
alignas(16) static u32 dref[1024], dout[1024];
alignas(16) static u32 even[256], odd[256], a[256], b[256], ref[512], out[512];
static u32 seed = 7;
static u32 rnd() { seed = seed * 1103515245u + 12345u; return seed >> 8; }
int main()
{
    int bad = 0, cases = 0;
    for (int it = 0; it < 3000; it++)
    {
        for (int i = 0; i < 256; i++) { even[i] = rnd() & 0xFF3F3F3F; odd[i] = rnd() & 0xFF3F3F3F; }
        const int mode = it % 3, factor = (it / 3) % 17;
        if (mode == 0)      { WriteLineNeon<0>(a, even, 0); WriteLineNeon<0>(b, odd, 0); WriteZipNeon<0>(out, even, odd, 0); }
        else if (mode == 1) { WriteLineNeon<1>(a, even, factor); WriteLineNeon<1>(b, odd, factor); WriteZipNeon<1>(out, even, odd, factor); }
        else                { WriteLineNeon<2>(a, even, factor); WriteLineNeon<2>(b, odd, factor); WriteZipNeon<2>(out, even, odd, factor); }
        ZipLine(ref, a, b);
        cases++;
        if (memcmp(ref, out, sizeof ref)) { bad++; if (bad < 5) printf("mismatch: it %d mode %d factor %d\n", it, mode, factor); }
        // the doubling: WriteLineNeon + DoubleLine vs WriteDoubleNeon
        if (mode == 0)      WriteDoubleNeon<0>(dout, even, 0);
        else if (mode == 1) WriteDoubleNeon<1>(dout, even, factor);
        else                WriteDoubleNeon<2>(dout, even, factor);
        DoubleLine(dref, a);
        cases++;
        if (memcmp(dref, dout, sizeof dref)) { bad++; if (bad < 5) printf("double mismatch: it %d mode %d factor %d\n", it, mode, factor); }
    }
    printf("%d cases, %d mismatches\n", cases, bad);
    return bad != 0;
}
