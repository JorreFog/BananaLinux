/* rgds-sim: a Cortex-A55 / RK3568 timing model for BananaDS under qemu-aarch64 (linux-user), as a TCG plugin.
 *
 * Per guest thread (a vCPU in qemu-user) and per code location (an ELF function symbol, or for code without one,
 * such as BananaDS's JIT blocks and shared libraries, the translation block's start address) it counts the events
 * the A55 timing depends on:
 *   - instructions by class (A64 decode: ALU, MUL, DIV, load, store, load/store pair, SIMD/FP load/store,
 *     atomics/exclusives, branch cond / direct / call / indirect / indirect call / ret, FP/NEON, system);
 *   - the base cycles of each block from a static in-order dual-issue schedule (A55-like pairing rules, load-use
 *     and multiply/FP latencies inside the block, L1 hits assumed);
 *   - L1I and L1D (per thread: each busy thread is assumed to own a core) and a shared L3, LRU, write-allocate;
 *     a simple stream prefetcher on L1D misses and a next-line prefetch on L1I misses;
 *   - L1 iTLB / dTLB and a unified L2 TLB (4 KB pages): L2 TLB hits and page walks;
 *   - conditional branches through a gshare predictor, indirect branches through a BTB, returns through a
 *     return stack (the outcome is the next executed block's address).
 * Latencies (L3, DRAM, mispredict, TLB) are NOT applied here: they are linear weights applied by report.py, so the
 * model can be refitted to device numbers without re-running (calibrate.py).
 *
 * Output at exit: <out>.tsv (vcpu, kind sym|addr, key, counters...) and <out>.threads (per thread: tid, name,
 * class counts, counters) and <out>.cfg (the structural parameters used).
 * Args (all optional): out=<path> l1i=32768 l1d=32768 l1ways=4 line=64 l3=524288 l3ways=16 itlb=15 dtlb=16
 *   l2tlb=1024 l2tlbways=4 page=4096 bpbits=12 bphist=10 btb=256 ras=8 ldlat=3 mullat=3 mul64lat=4 divlat=8
 *   fplat=4 pf=1 pfdeg=2 ipf=1 l3shared=1 thp=1 hugepages=LO-HI (hex; also learnt from madvise(MADV_HUGEPAGE))
 *   marks=N1:N2 warm=10 (one run: snapshots after frames N1 and N2 from BananaDS's headless.window=1 log; out= is
 *   then a prefix: <out>N1.tsv, <out>N2.tsv...)
 * Build: gcc -O2 -shared -fPIC -I<qemu>/include/qemu $(pkg-config --cflags glib-2.0) rgdssim.c -o librgdssim.so
 */
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/* ---- instruction classes ---- */
enum { CL_ALU, CL_MUL, CL_DIV, CL_LD, CL_ST, CL_LDP, CL_STP, CL_LDV, CL_STV, CL_ATOM, CL_BCOND, CL_B, CL_BL,
       CL_BR, CL_BLR, CL_RET, CL_FP, CL_SYS, CL_N };
static const char *clname[CL_N] = { "alu", "mul", "div", "ld", "st", "ldp", "stp", "ldv", "stv", "atomic",
    "bcond", "b", "bl", "br", "blr", "ret", "fpneon", "sys" };

/* ---- per-slot event counters ---- */
enum { C_INSN, C_BASE, C_L1I_MISS, C_L1I_L3MISS, C_LD, C_ST, C_LD_MISS, C_LD_L3MISS, C_ST_MISS, C_ST_L3MISS,
       C_ITLB_L2, C_ITLB_WALK, C_DTLB_L2, C_DTLB_WALK, C_BCOND, C_BCOND_MISS, C_BIND, C_BIND_MISS, C_RET_MISS,
       C_PF, C_PF_DRAM, C_N };
static const char *cname[C_N] = { "insn", "base", "l1i_miss", "l1i_l3miss", "ld", "st", "ld_miss", "ld_l3miss",
    "st_miss", "st_l3miss", "itlb_l2", "itlb_walk", "dtlb_l2", "dtlb_walk", "bcond", "bcond_miss", "bind",
    "bind_miss", "ret_miss", "pf", "pf_dram" };

/* ---- parameters ---- */
static const char *outpath = "rgdssim";
static unsigned p_l1i = 32768, p_l1d = 32768, p_l1ways = 4, p_line = 64, p_l3 = 524288, p_l3ways = 16;
static unsigned p_itlb = 15, p_dtlb = 16, p_l2tlb = 1024, p_l2tlbways = 4, p_page = 4096;
static unsigned p_bpbits = 12, p_bphist = 10, p_btb = 256, p_ras = 8;
static unsigned p_ldlat = 3, p_mullat = 3, p_mul64lat = 4, p_divlat = 8, p_fplat = 4;
static unsigned p_pf = 1, p_pfdeg = 2, p_ipf = 1, p_l3shared = 1, p_thp = 1;
static unsigned lshift, pshift;
/* transparent huge pages: ranges the guest madvise(MADV_HUGEPAGE)s (BananaLinux's kernel: THP=madvise) get 2 MB
   TLB entries when thp=1 (qemu-user itself ignores madvise). Also hugepages=LO-HI (hex) by hand. */
#define MAXHUGE 16
static uint64_t huge_lo[MAXHUGE], huge_hi[MAXHUGE]; static volatile int nhuge;
static inline uint64_t pagekey(uint64_t addr)
{
    for (int i = 0; i < nhuge; i++)
        if (addr >= huge_lo[i] && addr < huge_hi[i]) return (addr >> 21) | (1ull << 62);
    return addr >> pshift;
}



/* ---- set-associative cache with LRU (age stamps) ---- */
typedef struct {
    unsigned sets, ways;
    uint64_t *tag;     /* sets*ways, line number + 1 (0 = empty) */
    uint64_t *stamp;
    uint64_t clock;
} Cache;
static void cache_init(Cache *c, unsigned bytes, unsigned ways, unsigned line)
{
    c->ways = ways; c->sets = bytes / line / ways; if (!c->sets) c->sets = 1;
    c->tag = g_new0(uint64_t, (size_t)c->sets * ways); c->stamp = g_new0(uint64_t, (size_t)c->sets * ways);
    c->clock = 0;
}
/* returns 1 on hit; on miss inserts the line (LRU victim) */
static inline int cache_access(Cache *c, uint64_t ln)
{
    unsigned s = (unsigned)(ln % c->sets);
    uint64_t *t = c->tag + (size_t)s * c->ways, *st = c->stamp + (size_t)s * c->ways, key = ln + 1;
    unsigned victim = 0; uint64_t old = UINT64_MAX;
    c->clock++;
    for (unsigned w = 0; w < c->ways; w++) {
        if (t[w] == key) { st[w] = c->clock; return 1; }
        if (st[w] < old) { old = st[w]; victim = w; }
    }
    t[victim] = key; st[victim] = c->clock;
    return 0;
}
static inline int cache_probe(Cache *c, uint64_t ln)
{
    unsigned s = (unsigned)(ln % c->sets);
    uint64_t *t = c->tag + (size_t)s * c->ways, key = ln + 1;
    for (unsigned w = 0; w < c->ways; w++) if (t[w] == key) return 1;
    return 0;
}

/* shared L3 */
static Cache l3;
static GMutex l3lock;
static inline int l3_access(Cache *own, uint64_t ln)
{
    int h;
    if (!p_l3shared) return cache_access(own, ln);
    g_mutex_lock(&l3lock); h = cache_access(&l3, ln); g_mutex_unlock(&l3lock);
    return h;
}

/* ---- per-thread state ---- */
#define MAXV 64
#define CHUNK 4096
#define MAXCHUNKS 1024        /* 4M slots */
typedef struct TBInfo TBInfo;
typedef struct {
    uint64_t *chunk[MAXCHUNKS];
    uint64_t cls[CL_N];
    Cache l1i, l1d, l3own, l2tlb;
    uint64_t itlb[64], itlb_st[64], dtlb[64], dtlb_st[64], tlbclock;
    uint64_t last_dline, last_dpage, last_ipage;
    uint8_t *bp; uint64_t ghist;
    uint64_t *btb_pc, *btb_tgt;
    uint64_t *ras; unsigned rasn, rastop;
    uint64_t missring[8]; unsigned missi;
    const TBInfo *prev;
    long tid;
    char name[32];
} VState;
static VState *vs[MAXV];

static VState *vstate(unsigned v)
{
    if (v >= MAXV) v = MAXV - 1;
    VState *s = vs[v];
    if (s) return s;
    s = g_new0(VState, 1);
    cache_init(&s->l1i, p_l1i, p_l1ways, p_line);
    cache_init(&s->l1d, p_l1d, p_l1ways, p_line);
    if (!p_l3shared) cache_init(&s->l3own, p_l3, p_l3ways, p_line);
    cache_init(&s->l2tlb, p_l2tlb * p_page / 1, p_l2tlbways, p_page);   /* entries = sets*ways */
    s->bp = g_malloc(1u << p_bpbits); memset(s->bp, 1, 1u << p_bpbits);   /* weakly not taken */
    s->btb_pc = g_new0(uint64_t, p_btb); s->btb_tgt = g_new0(uint64_t, p_btb);
    s->ras = g_new0(uint64_t, p_ras ? p_ras : 1);
    s->last_dline = s->last_dpage = s->last_ipage = UINT64_MAX;
    s->tid = syscall(SYS_gettid);
    vs[v] = s;
    return s;
}
static inline uint64_t *slotctr(VState *s, unsigned slot)
{
    unsigned c = slot / CHUNK;
    if (c >= MAXCHUNKS) c = MAXCHUNKS - 1, slot = c * CHUNK + CHUNK - 1;
    if (!s->chunk[c]) s->chunk[c] = g_new0(uint64_t, (size_t)CHUNK * C_N);
    return s->chunk[c] + (size_t)(slot % CHUNK) * C_N;
}

/* fully associative small TLB with LRU */
static inline int ftlb_access(uint64_t *e, uint64_t *st, unsigned n, uint64_t *clock, uint64_t pg)
{
    unsigned victim = 0; uint64_t old = UINT64_MAX, key = pg + 1;
    (*clock)++;
    for (unsigned i = 0; i < n; i++) {
        if (e[i] == key) { st[i] = *clock; return 1; }
        if (st[i] < old) { old = st[i]; victim = i; }
    }
    e[victim] = key; st[victim] = *clock;
    return 0;
}

/* ---- static block info ---- */
enum { BK_NONE, BK_COND, BK_B, BK_BL, BK_BR, BK_BLR, BK_RET };
struct TBInfo {
    uint64_t pc, end, lastpc;     /* first insn, address after the last, the last insn */
    unsigned slot, n, base, kind;
    uint16_t cls[CL_N];
};
static GMutex lock;
static GHashTable *symslot, *addrslot;
static char **slotkey; static int *slotkind; static unsigned nslots, maxslots = CHUNK * MAXCHUNKS;

static unsigned slot_for(const char *sym, uint64_t vaddr)
{
    gpointer v;
    if (sym) { v = g_hash_table_lookup(symslot, sym); if (v) return GPOINTER_TO_UINT(v) - 1; }
    else { v = g_hash_table_lookup(addrslot, GSIZE_TO_POINTER(vaddr)); if (v) return GPOINTER_TO_UINT(v) - 1; }
    if (nslots >= maxslots - 1) return maxslots - 1;
    unsigned s = nslots++;
    if (sym) { slotkey[s] = g_strdup(sym); slotkind[s] = 0; g_hash_table_insert(symslot, slotkey[s], GUINT_TO_POINTER(s + 1)); }
    else { slotkey[s] = g_strdup_printf("%" PRIx64, vaddr); slotkind[s] = 1; g_hash_table_insert(addrslot, GSIZE_TO_POINTER(vaddr), GUINT_TO_POINTER(s + 1)); }
    return s;
}

/* ---- A64 decode: class, registers (0-30 X, 31 none, 32 NZCV, 33-64 V), latency ---- */
#define RNONE 255
#define RFLAGS 32
#define RV(x) (33 + (x))
typedef struct { uint8_t cls, nd, ns, lat, d[3], s[4]; } Dec;
static inline uint8_t X(unsigned r) { return r == 31 ? RNONE : (uint8_t)r; }
static void add_d(Dec *o, uint8_t r) { if (r != RNONE && o->nd < 3) o->d[o->nd++] = r; }
static void add_s(Dec *o, uint8_t r) { if (r != RNONE && o->ns < 4) o->s[o->ns++] = r; }

static Dec decode(uint32_t w)
{
    Dec o; memset(&o, 0, sizeof o); o.cls = CL_ALU; o.lat = 1;
    unsigned op0 = (w >> 25) & 0xF, rd = w & 31, rn = (w >> 5) & 31, rm = (w >> 16) & 31, ra = (w >> 10) & 31;
    if ((op0 & 0xE) == 0x8) {                         /* data processing, immediate */
        unsigned op = (w >> 23) & 7;
        if (op <= 1) add_d(&o, X(rd));                             /* adr, adrp */
        else if (op == 2 || op == 3) { add_d(&o, rd == 31 ? RNONE : rd); add_s(&o, X(rn)); if (w & (1u << 29)) add_d(&o, RFLAGS); }
        else if (op == 4) { add_d(&o, X(rd)); add_s(&o, X(rn)); if (((w >> 29) & 3) == 3) add_d(&o, RFLAGS); }
        else if (op == 5) { add_d(&o, X(rd)); if (((w >> 29) & 3) == 3) add_s(&o, X(rd)); }
        else if (op == 6) { add_d(&o, X(rd)); add_s(&o, X(rn)); if (((w >> 29) & 3) == 1) add_s(&o, X(rd)); }
        else { add_d(&o, X(rd)); add_s(&o, X(rn)); add_s(&o, X(rm)); }
        return o;
    }
    if ((op0 & 0xE) == 0xA) {                         /* branches, exceptions, system */
        if ((w & 0xFF000010) == 0x54000000) { o.cls = CL_BCOND; add_s(&o, RFLAGS); return o; }
        if ((w & 0x7E000000) == 0x34000000 || (w & 0x7E000000) == 0x36000000) { o.cls = CL_BCOND; add_s(&o, X(rd)); return o; }
        if ((w & 0x7C000000) == 0x14000000) { if (w >> 31) { o.cls = CL_BL; add_d(&o, 30); } else o.cls = CL_B; return o; }
        if ((w & 0xFE000000) == 0xD6000000) {          /* unconditional branch (register) */
            unsigned opc = (w >> 21) & 0xF;
            add_s(&o, X(rn));
            if (opc == 2 || opc == 0xA) { o.cls = CL_RET; if (rn == 31) add_s(&o, 30); }
            else if (opc == 1 || opc == 9) { o.cls = CL_BLR; add_d(&o, 30); }
            else if (opc == 4 || opc == 5) o.cls = CL_SYS;      /* eret, drps */
            else o.cls = CL_BR;
            return o;
        }
        o.cls = CL_SYS;                                 /* svc/brk, msr/mrs, hints, barriers */
        if ((w & 0xFFF00000) == 0xD5300000) add_d(&o, X(rd));         /* mrs */
        if ((w & 0xFFF00000) == 0xD5100000) add_s(&o, X(rd));         /* msr (register) */
        if ((w & 0xFFFFF01F) == 0xD503201F) o.cls = CL_ALU;            /* hints (nop, yield...) */
        return o;
    }
    if ((op0 & 0x5) == 0x4) {                         /* loads and stores */
        unsigned v = (w >> 26) & 1, b2927 = (w >> 27) & 7;
        if (((w >> 24) & 0x3F) == 0x08) {             /* exclusive / acquire-release / cas */
            o.cls = CL_ATOM; o.lat = p_ldlat; add_s(&o, X(rn)); add_s(&o, X(rd)); add_d(&o, X(rd)); add_d(&o, X(rm)); return o;
        }
        if (b2927 == 3 && !((w >> 24) & 1)) {          /* load literal (or ldapr/stlr family when bit 24 set) */
            o.cls = v ? CL_LDV : CL_LD; o.lat = p_ldlat; add_d(&o, v ? RV(rd) : X(rd)); return o;
        }
        if (b2927 == 5) {                              /* pairs */
            unsigned L = (w >> 22) & 1, idx = (w >> 23) & 3;
            uint8_t t1 = v ? RV(rd) : X(rd), t2 = v ? RV(ra) : X(ra);
            add_s(&o, X(rn));
            if (L) { o.cls = v ? CL_LDV : CL_LDP; o.lat = p_ldlat; add_d(&o, t1); add_d(&o, t2); }
            else { o.cls = v ? CL_STV : CL_STP; add_s(&o, t1); add_s(&o, t2); }
            if (idx == 1 || idx == 3) add_d(&o, X(rn));
            return o;
        }
        if (b2927 == 7) {                              /* load/store register */
            unsigned opc = (w >> 22) & 3, size = w >> 30;
            uint8_t t = v ? RV(rd) : X(rd);
            add_s(&o, X(rn));
            if (!v && !((w >> 24) & 1) && ((w >> 21) & 1) && ((w >> 10) & 3) == 0) {   /* atomic memory ops */
                o.cls = CL_ATOM; o.lat = p_ldlat; add_s(&o, X(rm)); add_d(&o, X(rd)); return o;
            }
            int reg = !((w >> 24) & 1) && ((w >> 21) & 1) && ((w >> 10) & 3) == 2;
            if (reg) add_s(&o, X(rm));
            if (!((w >> 24) & 1) && !((w >> 21) & 1) && (((w >> 10) & 3) == 1 || ((w >> 10) & 3) == 3)) add_d(&o, X(rn));
            int load = v ? (opc & 1) : (opc != 0);
            if (!v && size == 3 && opc == 2) { o.cls = CL_LD; o.lat = p_ldlat; return o; }      /* prfm */
            if (load) { o.cls = v ? CL_LDV : CL_LD; o.lat = p_ldlat; add_d(&o, t); }
            else { o.cls = v ? CL_STV : CL_ST; add_s(&o, t); }
            return o;
        }
        if (((w >> 24) & 0x3E) == 0x0C) {              /* SIMD structures (001100, 001101) */
            unsigned L = (w >> 22) & 1;
            add_s(&o, X(rn));
            if ((w >> 23) & 1) add_d(&o, X(rn));
            if (L) { o.cls = CL_LDV; o.lat = p_ldlat; add_d(&o, RV(rd)); } else { o.cls = CL_STV; add_s(&o, RV(rd)); }
            return o;
        }
        o.cls = CL_LD; o.lat = p_ldlat; add_s(&o, X(rn)); add_d(&o, X(rd));
        return o;
    }
    if ((op0 & 0x7) == 0x5) {                         /* data processing, register */
        unsigned op1 = (w >> 28) & 1, op2 = (w >> 21) & 0xF;
        if (!op1) {
            add_d(&o, X(rd)); add_s(&o, X(rn)); add_s(&o, X(rm));
            if (((w >> 24) & 1) ? ((w >> 29) & 1) : (((w >> 29) & 3) == 3)) add_d(&o, RFLAGS);
            if (((w >> 10) & 0x3F) && !((w >> 24) & 1)) o.lat = 2;   /* shifted logical: assumed 2 */
            return o;
        }
        if (op2 == 0) { add_d(&o, X(rd)); add_s(&o, X(rn)); add_s(&o, X(rm)); add_s(&o, RFLAGS); if ((w >> 29) & 1) add_d(&o, RFLAGS); return o; }
        if (op2 == 2) { add_s(&o, X(rn)); if (!((w >> 11) & 1)) add_s(&o, X(rm)); add_s(&o, RFLAGS); add_d(&o, RFLAGS); return o; }
        if (op2 == 4) { add_d(&o, X(rd)); add_s(&o, X(rn)); add_s(&o, X(rm)); add_s(&o, RFLAGS); return o; }
        if (op2 == 6) {
            add_d(&o, X(rd)); add_s(&o, X(rn));
            if (!((w >> 30) & 1)) {
                add_s(&o, X(rm));
                if ((((w >> 10) & 0x3F) >> 1) == 1) { o.cls = CL_DIV; o.lat = p_divlat; }
            }
            return o;
        }
        if (op2 >= 8) { o.cls = CL_MUL; o.lat = (w >> 31) ? p_mul64lat : p_mullat; add_d(&o, X(rd)); add_s(&o, X(rn)); add_s(&o, X(rm)); add_s(&o, X(ra)); return o; }
        add_d(&o, X(rd)); add_s(&o, X(rn)); add_s(&o, X(rm));
        return o;
    }
    if ((op0 & 0x7) == 0x7) {                         /* SIMD and FP */
        o.cls = CL_FP; o.lat = p_fplat;
        add_d(&o, RV(rd)); add_s(&o, RV(rn)); add_s(&o, RV(rm));
        if ((w & 0xFF20FC07) == 0x1E202000) { o.nd = 0; add_d(&o, RFLAGS); }   /* fcmp */
        return o;
    }
    o.cls = CL_SYS;
    return o;
}

/* pairing rules for the second issue slot (ASSUMED, after Arm's A55 optimisation guide: one load/store, one
   multiply/divide, one branch per cycle; FP/NEON pairs only with a non-FP instruction; divide/system issue alone) */
static inline int pipe_of(unsigned c)
{
    switch (c) {
    case CL_LD: case CL_ST: case CL_LDP: case CL_STP: case CL_LDV: case CL_STV: case CL_ATOM: return 1;
    case CL_MUL: return 2;
    case CL_BCOND: case CL_B: case CL_BL: case CL_BR: case CL_BLR: case CL_RET: return 3;
    case CL_FP: return 4;
    case CL_DIV: case CL_SYS: return 5;
    default: return 0;
    }
}
static unsigned schedule(const Dec *d, unsigned n)
{
    uint64_t ready[65]; memset(ready, 0, sizeof ready);
    uint64_t cyc = 0; int slot = 0; int prevpipe = -1;
    for (unsigned i = 0; i < n; i++) {
        uint64_t t = cyc;
        for (unsigned k = 0; k < d[i].ns; k++) if (ready[d[i].s[k]] > t) t = ready[d[i].s[k]];
        int pp = pipe_of(d[i].cls);
        if (t == cyc && slot == 1) {
            if (pp == 5 || prevpipe == 5 || (pp != 0 && pp == prevpipe)) t = cyc + 1;
        } else if (t == cyc && slot == 2) t = cyc + 1;
        if (t > cyc) { cyc = t; slot = 0; }
        slot++; prevpipe = pp;
        if (pp == 5) slot = 2;          /* issues alone */
        for (unsigned k = 0; k < d[i].nd; k++) {
            uint64_t r = t + d[i].lat;
            if (d[i].d[k] == RFLAGS) r = t + 1;
            ready[d[i].d[k]] = r;
        }
        /* load/store pairs of 128 bits and 4-register structures take 2 cycles in the LS pipe (ASSUMED) */
    }
    return (unsigned)(cyc + 1);
}

/* ---- run-time callbacks ---- */
static inline void icache(VState *s, const TBInfo *tb, uint64_t *c)
{
    uint64_t first = tb->pc >> lshift, last = (tb->end - 1) >> lshift;
    for (uint64_t ln = first; ln <= last; ln++) {
        uint64_t pg = pagekey(ln << lshift);
        if (pg != s->last_ipage) {
            s->last_ipage = pg;
            if (!ftlb_access(s->itlb, s->itlb_st, p_itlb, &s->tlbclock, pg)) {
                if (cache_access(&s->l2tlb, pg)) c[C_ITLB_L2]++; else c[C_ITLB_WALK]++;
            }
        }
        if (!cache_access(&s->l1i, ln)) {
            c[C_L1I_MISS]++;
            if (!l3_access(&s->l3own, ln)) c[C_L1I_L3MISS]++;
            if (p_ipf && !cache_probe(&s->l1i, ln + 1)) {   /* next-line prefetch (ASSUMED) */
                cache_access(&s->l1i, ln + 1);
                l3_access(&s->l3own, ln + 1);
            }
        }
    }
}

static void tb_exec(unsigned vcpu, void *ud)
{
    const TBInfo *tb = ud;
    VState *s = vstate(vcpu);
    const TBInfo *p = s->prev;
    uint64_t pc = tb->pc;
    if (p && p->kind != BK_NONE) {
        uint64_t *pc_ = slotctr(s, p->slot);
        uint64_t bpc = p->lastpc;
        switch (p->kind) {
        case BK_COND: {
            int taken = pc != p->end;
            unsigned idx = (unsigned)(((bpc >> 2) ^ s->ghist) & ((1u << p_bpbits) - 1));
            uint8_t ctr = s->bp[idx];
            pc_[C_BCOND]++;
            if ((ctr >= 2) != taken) pc_[C_BCOND_MISS]++;
            if (taken && ctr < 3) s->bp[idx] = ctr + 1;
            if (!taken && ctr > 0) s->bp[idx] = ctr - 1;
            s->ghist = ((s->ghist << 1) | (uint64_t)taken) & ((1ull << p_bphist) - 1);
            break;
        }
        case BK_BL:
            if (p_ras) { s->ras[s->rastop] = p->end; s->rastop = (s->rastop + 1) % p_ras; if (s->rasn < p_ras) s->rasn++; }
            break;
        case BK_BR: case BK_BLR: {
            unsigned i = (unsigned)((bpc >> 2) % p_btb);
            pc_[C_BIND]++;
            if (s->btb_pc[i] != bpc || s->btb_tgt[i] != pc) pc_[C_BIND_MISS]++;
            s->btb_pc[i] = bpc; s->btb_tgt[i] = pc;
            if (p->kind == BK_BLR && p_ras) { s->ras[s->rastop] = p->end; s->rastop = (s->rastop + 1) % p_ras; if (s->rasn < p_ras) s->rasn++; }
            break;
        }
        case BK_RET: {
            uint64_t pred = 0;
            pc_[C_BIND]++;
            if (s->rasn) { s->rastop = (s->rastop + p_ras - 1) % p_ras; pred = s->ras[s->rastop]; s->rasn--; }
            if (pred != pc) { pc_[C_BIND_MISS]++; pc_[C_RET_MISS]++; }
            break;
        }
        default: break;
        }
    }
    uint64_t *c = slotctr(s, tb->slot);
    c[C_INSN] += tb->n;
    c[C_BASE] += tb->base;
    for (unsigned k = 0; k < CL_N; k++) s->cls[k] += tb->cls[k];
    icache(s, tb, c);
    s->prev = tb;
}

static inline void prefetch_d(VState *s, uint64_t ln, uint64_t *c)
{
    for (unsigned k = 1; k <= p_pfdeg; k++) {
        if (cache_probe(&s->l1d, ln + k)) continue;
        cache_access(&s->l1d, ln + k);
        c[C_PF]++;
        if (!l3_access(&s->l3own, ln + k)) c[C_PF_DRAM]++;
    }
}

static void mem_cb(unsigned vcpu, qemu_plugin_meminfo_t info, uint64_t va, void *ud)
{
    VState *s = vstate(vcpu);
    uint64_t *c = slotctr(s, GPOINTER_TO_UINT(ud));
    int st = qemu_plugin_mem_is_store(info);
    unsigned size = 1u << qemu_plugin_mem_size_shift(info);
    c[st ? C_ST : C_LD]++;
    uint64_t first = va >> lshift, last = (va + size - 1) >> lshift;
    for (uint64_t ln = first; ln <= last; ln++) {
        if (ln == s->last_dline) continue;
        s->last_dline = ln;
        uint64_t pg = pagekey(ln << lshift);
        if (pg != s->last_dpage) {
            s->last_dpage = pg;
            if (!ftlb_access(s->dtlb, s->dtlb_st, p_dtlb, &s->tlbclock, pg)) {
                if (cache_access(&s->l2tlb, pg)) c[C_DTLB_L2]++; else c[C_DTLB_WALK]++;
            }
        }
        if (cache_access(&s->l1d, ln)) continue;
        c[st ? C_ST_MISS : C_LD_MISS]++;
        if (!l3_access(&s->l3own, ln)) c[st ? C_ST_L3MISS : C_LD_L3MISS]++;
        if (p_pf) {   /* stream prefetcher (ASSUMED): a miss next to one of the last 8 misses prefetches ahead */
            int seq = 0;
            for (unsigned k = 0; k < 8; k++) if (s->missring[k] == ln - 1 || s->missring[k] == ln - 2) { seq = 1; break; }
            s->missring[s->missi++ & 7] = ln;
            if (seq) prefetch_d(s, ln, c);
        }
    }
}

static void tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    if (!n) return;
    struct qemu_plugin_insn *first = qemu_plugin_tb_get_insn(tb, 0);
    const char *sym = qemu_plugin_insn_symbol(first);
    uint64_t va = qemu_plugin_tb_vaddr(tb);
    TBInfo *t = g_new0(TBInfo, 1);
    Dec *d = g_new(Dec, n);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *in = qemu_plugin_tb_get_insn(tb, i);
        uint32_t w = 0;
        qemu_plugin_insn_data(in, &w, 4);
        d[i] = decode(w);
        t->cls[d[i].cls]++;
    }
    t->pc = va; t->n = (unsigned)n;
    struct qemu_plugin_insn *lastin = qemu_plugin_tb_get_insn(tb, n - 1);
    t->lastpc = qemu_plugin_insn_vaddr(lastin);
    t->end = t->lastpc + qemu_plugin_insn_size(lastin);
    switch (d[n - 1].cls) {
    case CL_BCOND: t->kind = BK_COND; break;
    case CL_B: t->kind = BK_B; break;
    case CL_BL: t->kind = BK_BL; break;
    case CL_BR: t->kind = BK_BR; break;
    case CL_BLR: t->kind = BK_BLR; break;
    case CL_RET: t->kind = BK_RET; break;
    default: t->kind = BK_NONE;
    }
    t->base = schedule(d, (unsigned)n);
    g_mutex_lock(&lock);
    t->slot = slot_for(sym, va);
    g_mutex_unlock(&lock);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, tb_exec, QEMU_PLUGIN_CB_NO_REGS, t);
    for (size_t i = 0; i < n; i++) {
        unsigned c = d[i].cls;
        if (c == CL_LD || c == CL_ST || c == CL_LDP || c == CL_STP || c == CL_LDV || c == CL_STV || c == CL_ATOM)
            qemu_plugin_register_vcpu_mem_cb(qemu_plugin_tb_get_insn(tb, i), mem_cb, QEMU_PLUGIN_CB_NO_REGS,
                                             QEMU_PLUGIN_MEM_RW, GUINT_TO_POINTER(t->slot));
    }
    g_free(d);
}

static void add_huge(uint64_t lo, uint64_t hi)
{
    g_mutex_lock(&lock);
    if (nhuge < MAXHUGE) { huge_lo[nhuge] = lo & ~0x1FFFFFull; huge_hi[nhuge] = hi; nhuge++; }
    g_mutex_unlock(&lock);
    fprintf(stderr, "[rgdssim] 2 MB pages: 0x%" PRIx64 "-0x%" PRIx64 "\n", lo, hi);
}
static void frame_mark(long done);
static unsigned m_n2;
static void on_syscall(qemu_plugin_id_t id, unsigned v, int64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
{
    if (num == 233 && a3 == 14 && p_thp) add_huge(a1, a1 + a2);   /* aarch64 madvise(MADV_HUGEPAGE) */
    if (num == 64 && m_n2 && (a1 == 1 || a1 == 2) && a3 >= 20) {    /* write(stdout/stderr): a frame mark? */
        GByteArray *b = g_byte_array_new();
        if (qemu_plugin_read_memory_vaddr(a2, b, a3 < 48 ? a3 : 48)) {
            g_byte_array_append(b, (const guint8 *)"", 1);
            const char *t = (const char *)b->data, *m = strstr(t, "[headless] frames ");
            const char *d = m ? strchr(m + 18, '-') : NULL;
            if (d) frame_mark(strtol(d + 1, NULL, 10));
        }
        g_byte_array_free(b, TRUE);
    }
}

static void read_name(VState *s, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    char b[32] = { 0 };
    if (fgets(b, sizeof b, f)) { b[strcspn(b, "\n")] = 0; if (b[0]) g_strlcpy(s->name, b, sizeof s->name); }
    fclose(f);
}
static void vcpu_exit(qemu_plugin_id_t id, unsigned v)
{
    if (v < MAXV && vs[v]) read_name(vs[v], "/proc/thread-self/comm");
}

static void dump(const char *prefix)
{
    char *path = g_strdup_printf("%s.tsv", prefix);
    FILE *f = fopen(path, "w");
    g_free(path);
    fprintf(f, "vcpu\tkind\tkey");
    for (int k = 0; k < C_N; k++) fprintf(f, "\t%s", cname[k]);
    fprintf(f, "\n");
    for (unsigned v = 0; v < MAXV; v++) {
        VState *s = vs[v];
        if (!s) continue;
        for (unsigned sl = 0; sl < nslots; sl++) {
            uint64_t *ch = s->chunk[sl / CHUNK];
            if (!ch) continue;
            uint64_t *c = ch + (size_t)(sl % CHUNK) * C_N;
            int any = 0;
            for (int k = 0; k < C_N; k++) any |= c[k] != 0;
            if (!any) continue;
            fprintf(f, "%u\t%s\t%s", v, slotkind[sl] ? "addr" : "sym", slotkey[sl]);
            for (int k = 0; k < C_N; k++) fprintf(f, "\t%" PRIu64, c[k]);
            fprintf(f, "\n");
        }
    }
    fclose(f);
    path = g_strdup_printf("%s.threads", prefix);
    f = fopen(path, "w");
    g_free(path);
    fprintf(f, "vcpu\ttid\tname");
    for (int k = 0; k < CL_N; k++) fprintf(f, "\t%s", clname[k]);
    fprintf(f, "\n");
    for (unsigned v = 0; v < MAXV; v++) {
        VState *s = vs[v];
        if (!s) continue;
        if (!s->name[0]) { char *pp = g_strdup_printf("/proc/self/task/%ld/comm", s->tid); read_name(s, pp); g_free(pp); }
        fprintf(f, "%u\t%ld\t%s", v, s->tid, s->name[0] ? s->name : "?");
        for (int k = 0; k < CL_N; k++) fprintf(f, "\t%" PRIu64, s->cls[k]);
        fprintf(f, "\n");
    }
    fclose(f);
    path = g_strdup_printf("%s.cfg", prefix);
    f = fopen(path, "w");
    g_free(path);
    fprintf(f, "l1i=%u l1d=%u l1ways=%u line=%u l3=%u l3ways=%u itlb=%u dtlb=%u l2tlb=%u l2tlbways=%u page=%u "
            "bpbits=%u bphist=%u btb=%u ras=%u ldlat=%u mullat=%u mul64lat=%u divlat=%u fplat=%u pf=%u pfdeg=%u "
            "ipf=%u l3shared=%u thp=%u\n", p_l1i, p_l1d, p_l1ways, p_line, p_l3, p_l3ways, p_itlb, p_dtlb, p_l2tlb,
            p_l2tlbways, p_page, p_bpbits, p_bphist, p_btb, p_ras, p_ldlat, p_mullat, p_mul64lat, p_divlat, p_fplat,
            p_pf, p_pfdeg, p_ipf, p_l3shared, p_thp);
    fclose(f);
    /* the address map (qemu-user: guest = host addresses here), to name shared-library code in the report */
    path = g_strdup_printf("%s.maps", prefix);
    FILE *in = fopen("/proc/self/maps", "r"), *o = fopen(path, "w");
    g_free(path);
    if (in && o) { char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, in)) > 0) fwrite(b, 1, n, o); }
    if (in) fclose(in);
    if (o) fclose(o);
}

/* frame marks (marks=N1:N2): BananaDS's headless frontend with headless.window=1 logs "[headless] frames A-B: ..."
   after every frame; the plugin counts nothing (plain qemu speed) until frame N1-warm, then instruments everything
   and writes <out>N1.* after frame N1 and <out>N2.* after frame N2: one run instead of two. */
static unsigned m_n1, m_warm = 10, m_on, m_done;
static qemu_plugin_id_t m_id;
static void on_syscall(qemu_plugin_id_t id, unsigned v, int64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8);
static void vcpu_exit(qemu_plugin_id_t id, unsigned v);
static void at_exit(qemu_plugin_id_t id, void *p);
static void register_all(qemu_plugin_id_t id)
{
    qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans);
    qemu_plugin_register_vcpu_exit_cb(id, vcpu_exit);
    qemu_plugin_register_vcpu_syscall_cb(id, on_syscall);
    qemu_plugin_register_atexit_cb(id, at_exit, NULL);
}
static void frame_mark(long done)
{
    if (!m_on && done + m_warm >= m_n1) { m_on = 1; qemu_plugin_reset(m_id, register_all); return; }
    if (m_on && (done == m_n1 || done == m_n2)) {
        char *pre = g_strdup_printf("%s%ld", outpath, done);
        dump(pre);
        g_free(pre);
        if (done == m_n2) m_done = 1;
    }
}

static void at_exit(qemu_plugin_id_t id, void *p)
{
    if (m_n2) {
        if (!m_done) fprintf(stderr, "[rgdssim] frame %u never reached: no report\n", m_n2);
        return;
    }
    dump(outpath);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info, int argc, char **argv)
{
    struct { const char *k; unsigned *v; } P[] = {
        { "l1i", &p_l1i }, { "l1d", &p_l1d }, { "l1ways", &p_l1ways }, { "line", &p_line }, { "l3", &p_l3 },
        { "l3ways", &p_l3ways }, { "itlb", &p_itlb }, { "dtlb", &p_dtlb }, { "l2tlb", &p_l2tlb },
        { "l2tlbways", &p_l2tlbways }, { "page", &p_page }, { "bpbits", &p_bpbits }, { "bphist", &p_bphist },
        { "btb", &p_btb }, { "ras", &p_ras }, { "ldlat", &p_ldlat }, { "mullat", &p_mullat },
        { "mul64lat", &p_mul64lat }, { "divlat", &p_divlat }, { "fplat", &p_fplat }, { "pf", &p_pf },
        { "pfdeg", &p_pfdeg }, { "ipf", &p_ipf }, { "l3shared", &p_l3shared }, { "thp", &p_thp } };
    for (int i = 0; i < argc; i++) {
        if (!strncmp(argv[i], "out=", 4)) { outpath = g_strdup(argv[i] + 4); continue; }
        if (!strncmp(argv[i], "marks=", 6)) { char *e; m_n1 = strtoul(argv[i] + 6, &e, 10); m_n2 = *e == ':' ? strtoul(e + 1, 0, 10) : 0; continue; }
        if (!strncmp(argv[i], "warm=", 5)) { m_warm = strtoul(argv[i] + 5, 0, 10); continue; }
        if (!strncmp(argv[i], "hugepages=", 10)) {
            char *e; uint64_t lo = strtoull(argv[i] + 10, &e, 16), hi = *e == '-' ? strtoull(e + 1, 0, 16) : lo;
            add_huge(lo, hi); continue;
        }
        int ok = 0;
        for (size_t k = 0; k < G_N_ELEMENTS(P); k++) {
            size_t l = strlen(P[k].k);
            if (!strncmp(argv[i], P[k].k, l) && argv[i][l] == '=') { *P[k].v = (unsigned)strtoul(argv[i] + l + 1, 0, 0); ok = 1; }
        }
        if (!ok) { fprintf(stderr, "rgdssim: unknown argument %s\n", argv[i]); return -1; }
    }
    if (p_itlb > 64) p_itlb = 64;
    if (p_dtlb > 64) p_dtlb = 64;
    if (p_btb == 0) p_btb = 1;
    lshift = __builtin_ctz(p_line); pshift = __builtin_ctz(p_page);
    cache_init(&l3, p_l3, p_l3ways, p_line);
    symslot = g_hash_table_new(g_str_hash, g_str_equal);
    addrslot = g_hash_table_new(g_direct_hash, g_direct_equal);
    slotkey = g_new0(char *, maxslots); slotkind = g_new0(int, maxslots);
    m_id = id;
    if (m_n2) {   /* frame marks: plain qemu until frame N1 - warm */
        qemu_plugin_register_vcpu_syscall_cb(id, on_syscall);
        qemu_plugin_register_atexit_cb(id, at_exit, NULL);
        if (m_warm >= m_n1) { m_on = 1; register_all(id); }
        return 0;
    }
    register_all(id);
    return 0;
}
