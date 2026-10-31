#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arm_neon.h>
typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8; typedef int32_t s32; typedef uint64_t u64;
#define BANANA_NEON 1
namespace melonDS
{
typedef uint32x4_t V4;
static inline V4 VLoad(const u32* p) { return vld1q_u32(p); }
static inline void VStore(u32* p, V4 v) { vst1q_u32(p, v); }
static inline V4 VDup(u32 x) { return vdupq_n_u32(x); }
static inline V4 VZero() { return vdupq_n_u32(0); }
static inline V4 VNonZero(V4 a) { return vtstq_u32(a, a); }
static inline V4 VEq(V4 a, V4 b) { return vceqq_u32(a, b); }
static inline V4 VAnd(V4 a, V4 b) { return vandq_u32(a, b); }
static inline V4 VOr(V4 a, V4 b) { return vorrq_u32(a, b); }
static inline V4 VNot(V4 a) { return vmvnq_u32(a); }
static inline V4 VLtSmall(V4 a, V4 b) { return vcltq_u32(a, b); }        // a < b, lanes well below 2^31
static inline V4 VSel(V4 m, V4 a, V4 b) { return vbslq_u32(m, a, b); }   // m ? a : b, per bit
static inline V4 VFromBytes4(u32 x) { return vmovl_u16(vget_low_u16(vmovl_u8(vreinterpret_u8_u32(vdup_n_u32(x))))); }
static inline V4 VLoadU16x4(const u16* p) { return vmovl_u16(vld1_u16(p)); }
template<int N> static inline V4 VShl(V4 a) { return vshlq_n_u32(a, N); }
template<int N> static inline V4 VShr(V4 a) { return vshrq_n_u32(a, N); }
static inline u32 VHorOr(V4 a) { uint32x2_t t = vorr_u32(vget_low_u32(a), vget_high_u32(a)); return vget_lane_u32(t, 0) | vget_lane_u32(t, 1); }
static inline u32 VHorAnd(V4 a) { uint32x2_t t = vand_u32(vget_low_u32(a), vget_high_u32(a)); return vget_lane_u32(t, 0) & vget_lane_u32(t, 1); }
}

namespace melonDS {
bool Banana2DFast=true, Banana2DConv16=true, Banana2DPrefetch16=false;
static inline u32 Conv15(u32 c)
{
    u32 r = (c & 0x001F) << 1;
    u32 g = ((c & 0x03E0) >> 4) | ((c & 0x8000) >> 15);
    u32 b = (c & 0x7C00) >> 9;
    return r | (g << 8) | (b << 16);
}

#ifdef BANANA_NEON
// AND / OR of the 32-bit pixels whose low and high halves are the lanes of lo and hi
static inline u32 HorAnd16(uint16x8_t lo, uint16x8_t hi)
{
    uint64x2_t l = vreinterpretq_u64_u16(lo), h = vreinterpretq_u64_u16(hi);
    u64 a = vgetq_lane_u64(l, 0) & vgetq_lane_u64(l, 1), b = vgetq_lane_u64(h, 0) & vgetq_lane_u64(h, 1);
    a &= a >> 32; a &= a >> 16; b &= b >> 32; b &= b >> 16;
    return (u32)(a & 0xFFFF) | ((u32)(b & 0xFFFF) << 16);
}
static inline u32 HorOr16(uint16x8_t lo, uint16x8_t hi)
{
    uint64x2_t l = vreinterpretq_u64_u16(lo), h = vreinterpretq_u64_u16(hi);
    u64 a = vgetq_lane_u64(l, 0) | vgetq_lane_u64(l, 1), b = vgetq_lane_u64(h, 0) | vgetq_lane_u64(h, 1);
    a |= a >> 32; a |= a >> 16; b |= b >> 32; b |= b >> 16;
    return (u32)(a & 0xFFFF) | ((u32)(b & 0xFFFF) << 16);
}
#endif

#ifdef BANANA_NEON
// 8 direct-colour pixels to the 18-bit format with the layer's flag (zero where bit 15 is clear), stored interleaved:
// low halves R | G << 8, high halves B | the flag
static inline void Conv16x8(u32* out, uint16x8_t c, uint16x8_t m3e, uint16x8_t vflag)
{
    uint16x8_t a = vreinterpretq_u16_s16(vshrq_n_s16(vreinterpretq_s16_u16(c), 15));
    uint16x8_t r = vandq_u16(vshlq_n_u16(c, 1), m3e);
    uint16x8_t g = vandq_u16(vshrq_n_u16(c, 4), m3e);
    uint16x8_t b = vandq_u16(vshrq_n_u16(c, 9), m3e);
    uint16x8x2_t o;
    o.val[0] = vandq_u16(vsliq_n_u16(r, g, 8), a);
    o.val[1] = vandq_u16(vorrq_u16(b, vflag), a);
    vst2q_u16((u16*)out, o);
}
#endif

static inline void ConvertRun16(u32* out, const u16* src, int n, u32 flag, u32& acc, u32& all)
{
    int j = 0;
#ifdef BANANA_NEON
    if (Banana2DPrefetch16)
    {
        // render.2d_prefetch16: this run's VRAM row and the 512 bytes after it (a 256-pixel bitmap's next row) asked
        // for at once, so their misses overlap instead of coming one at a time as the loop reaches them
        const u8* p = (const u8*)src;
        const u8* end = (const u8*)(src + n) + 512;
        for (p = (const u8*)((uintptr_t)p & ~(uintptr_t)63); p < end; p += 64)
            __builtin_prefetch(p);
    }
    if (Banana2DFast && Banana2DConv16 && n >= 16)
    {
        // render.2d_conv16: 16 pixels a step, and acc/all from the sources' bit 15 alone: the callers only test
        // whether acc and all are zero, and an opaque pixel always carries the (non-zero) flag, so "some pixel
        // opaque" and "every pixel opaque" are all they need
        const uint16x8_t m3e = vdupq_n_u16(0x3E), vflag = vdupq_n_u16((u16)(flag >> 16));
        uint16x8_t any = vdupq_n_u16(0), every = vdupq_n_u16(0xFFFF);
        for (; j + 16 <= n; j += 16)
        {
            uint16x8_t c0 = vld1q_u16(src + j), c1 = vld1q_u16(src + j + 8);
            any = vorrq_u16(any, vorrq_u16(c0, c1));
            every = vandq_u16(every, vandq_u16(c0, c1));
            Conv16x8(out + j, c0, m3e, vflag);
            Conv16x8(out + j + 8, c1, m3e, vflag);
        }
        if (vmaxvq_u16(any) & 0x8000) acc |= flag;
        if (!(vminvq_u16(every) & 0x8000)) all = 0;
    }
    if (Banana2DFast && n - j >= 8)
    {
        // 8 pixels in 16-bit lanes: low halves R | G << 8, high halves B | the flag, zeroed where bit 15 is clear,
        // stored interleaved (st2)
        const uint16x8_t m3e = vdupq_n_u16(0x3E), vflag = vdupq_n_u16((u16)(flag >> 16));
        uint16x8_t accLo = vdupq_n_u16(0), accHi = accLo, allLo = vdupq_n_u16(0xFFFF), allHi = allLo;
        for (; j + 8 <= n; j += 8)
        {
            uint16x8_t c = vld1q_u16(src + j);
            uint16x8_t a = vreinterpretq_u16_s16(vshrq_n_s16(vreinterpretq_s16_u16(c), 15));
            uint16x8_t r = vandq_u16(vshlq_n_u16(c, 1), m3e);
            uint16x8_t g = vandq_u16(vshrq_n_u16(c, 4), m3e);
            uint16x8_t b = vandq_u16(vshrq_n_u16(c, 9), m3e);
            uint16x8x2_t o;
            o.val[0] = vandq_u16(vsliq_n_u16(r, g, 8), a);
            o.val[1] = vandq_u16(vorrq_u16(b, vflag), a);
            vst2q_u16((u16*)(out + j), o);
            accLo = vorrq_u16(accLo, o.val[0]); accHi = vorrq_u16(accHi, o.val[1]);
            allLo = vandq_u16(allLo, o.val[0]); allHi = vandq_u16(allHi, o.val[1]);
        }
        acc |= HorOr16(accLo, accHi);
        all &= HorAnd16(allLo, allHi);
    }
#endif
    if (n >= 4)
    {
        V4 vacc = VZero(), vall = VDup(~0u);
        V4 vflag = VDup(flag);
        V4 m1f = VDup(0x1F), m3e0 = VDup(0x3E0), m7c00 = VDup(0x7C00), m8000 = VDup(0x8000);
        for (; j + 4 <= n; j += 4)
        {
            V4 c = VLoadU16x4(src + j);
            V4 alpha = VNonZero(VAnd(c, m8000));
            V4 col = VOr(VOr(VShl<1>(VAnd(c, m1f)), VShl<4>(VAnd(c, m3e0))), VOr(VShl<7>(VAnd(c, m7c00)), vflag));
            col = VAnd(col, alpha);
            VStore(out + j, col);
            vacc = VOr(vacc, col);
            vall = VAnd(vall, col);
        }
        acc |= VHorOr(vacc);
        all &= VHorAnd(vall);
    }
    for (; j < n; j++)
    {
        u32 color = src[j];
        u32 c = (color & 0x8000) ? (Conv15(color & 0x7FFF) | flag) : 0;
        out[j] = c;
        acc |= c;
        all &= c;
    }
}

static inline void ConvertRun16Old(u32* out, const u16* src, int n, u32 flag, u32& acc, u32& all)
{
    int j = 0;
#ifdef BANANA_NEON
    if (Banana2DFast && n >= 8)
    {
        // 8 pixels in 16-bit lanes: low halves R | G << 8, high halves B | the flag, zeroed where bit 15 is clear,
        // stored interleaved (st2)
        const uint16x8_t m3e = vdupq_n_u16(0x3E), vflag = vdupq_n_u16((u16)(flag >> 16));
        uint16x8_t accLo = vdupq_n_u16(0), accHi = accLo, allLo = vdupq_n_u16(0xFFFF), allHi = allLo;
        for (; j + 8 <= n; j += 8)
        {
            uint16x8_t c = vld1q_u16(src + j);
            uint16x8_t a = vreinterpretq_u16_s16(vshrq_n_s16(vreinterpretq_s16_u16(c), 15));
            uint16x8_t r = vandq_u16(vshlq_n_u16(c, 1), m3e);
            uint16x8_t g = vandq_u16(vshrq_n_u16(c, 4), m3e);
            uint16x8_t b = vandq_u16(vshrq_n_u16(c, 9), m3e);
            uint16x8x2_t o;
            o.val[0] = vandq_u16(vsliq_n_u16(r, g, 8), a);
            o.val[1] = vandq_u16(vorrq_u16(b, vflag), a);
            vst2q_u16((u16*)(out + j), o);
            accLo = vorrq_u16(accLo, o.val[0]); accHi = vorrq_u16(accHi, o.val[1]);
            allLo = vandq_u16(allLo, o.val[0]); allHi = vandq_u16(allHi, o.val[1]);
        }
        acc |= HorOr16(accLo, accHi);
        all &= HorAnd16(allLo, allHi);
    }
#endif
    if (n >= 4)
    {
        V4 vacc = VZero(), vall = VDup(~0u);
        V4 vflag = VDup(flag);
        V4 m1f = VDup(0x1F), m3e0 = VDup(0x3E0), m7c00 = VDup(0x7C00), m8000 = VDup(0x8000);
        for (; j + 4 <= n; j += 4)
        {
            V4 c = VLoadU16x4(src + j);
            V4 alpha = VNonZero(VAnd(c, m8000));
            V4 col = VOr(VOr(VShl<1>(VAnd(c, m1f)), VShl<4>(VAnd(c, m3e0))), VOr(VShl<7>(VAnd(c, m7c00)), vflag));
            col = VAnd(col, alpha);
            VStore(out + j, col);
            vacc = VOr(vacc, col);
            vall = VAnd(vall, col);
        }
        acc |= VHorOr(vacc);
        all &= VHorAnd(vall);
    }
    for (; j < n; j++)
    {
        u32 color = src[j];
        u32 c = (color & 0x8000) ? (Conv15(color & 0x7FFF) | flag) : 0;
        out[j] = c;
        acc |= c;
        all &= c;
    }
}


}
using namespace melonDS;
int main(){
  srand(1); int bad=0;
  static u16 vram[2048]; static u32 o1[512], o2[512];
  for(int t=0;t<200000;t++){
    for(int k=0;k<1024;k++){ u16 v=rand()&0xFFFF; int mode=t%4; if(mode==1) v|=0x8000; if(mode==2) v&=0x7FFF; vram[k]=v; }
    int n=rand()%257; int off=rand()%512; u32 flag=0x01000000u<<(rand()%4);
    Banana2DPrefetch16 = t&1;
    u32 acc1=(rand()&1)?0:flag, all1=(rand()&1)?~0u:((rand()&1)?0:(flag|(rand()&0x00FFFFFF))), acc2=acc1, all2=all1;
    memset(o1,0xAB,sizeof o1); memset(o2,0xAB,sizeof o2);
    ConvertRun16(o1, vram+off, n, flag, acc1, all1);
    ConvertRun16Old(o2, vram+off, n, flag, acc2, all2);
    if(memcmp(o1,o2,sizeof o1) || (!acc1)!=(!acc2) || (!all1)!=(!all2)){ if(bad<5) printf("mismatch t=%d n=%d acc %x/%x all %x/%x\n",t,n,acc1,acc2,all1,all2); bad++; }
  }
  printf("ConvertRun16 new vs old: 200000 random runs (n 0-256, opaque/transparent/mixed rows, 4 flags), %d mismatches\n",bad); return bad!=0;
}
