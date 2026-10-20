/*
 * a55prof: a QEMU TCG plugin (qemu-aarch64 user mode) that counts the AArch64 instructions a program executes,
 * per function, so an emulator's frame can be costed for the RG DS Plus's Cortex-A55 without the handheld.
 *
 * Every translation block's instructions are added to the counter of the function its first instruction is in
 * (qemu_plugin_insn_symbol; the binary must keep its symbols). Code with no symbol (a JIT's generated code, a
 * shared library's) goes to "[nosym <1 MB region>]". Counting is inline (no helper call), so the run is only a little slower.
 *
 * Counts are per function: the main thread's (the first vCPU) as "<count>\t<function>", the other threads' as
 * "<count>\t@<thread> <function>" (threads numbered in the order they started).
 *
 * Markers: the program makes the syscall 0xBA5E (unknown to Linux: it returns -ENOSYS) with a mark id in x0 and a
 * value in x1. At each one the plugin writes, to out=<file>, "MARK <id> <value> <total insns>" and then every
 * function whose count moved since the previous mark as "<delta>\t<symbol>". tools/3ds-perf/a55report.py turns
 * that into per-scene costs.
 *
 *   qemu-aarch64 -plugin ./liba55prof.so,out=prof.txt ./panda3ds-bench ...
 *
 * hot=1 also lists, at the end, the 200 symbol-less translation blocks that cost most ("HOT <vaddr> insns= execs="): with
 * the JIT's code dumped (panda3ds-bench --dump-jit), what the JIT generates for the guest's hot loops.
 */
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define MAX_SYMS 32768
#define MARK_SYSCALL 0xBA5E

static struct qemu_plugin_scoreboard *counts;  /* per vCPU: uint64_t[MAX_SYMS] */
static GHashTable *sym_index;                   /* symbol name -> index + 1 */
static const char *sym_names[MAX_SYMS];
static uint64_t last[MAX_SYMS];
static int nsyms;
static GMutex lock;
static FILE *out;
static int hot;                 /* hot=1: also count each symbol-less translation block on its own (a JIT's hot code) */
static GHashTable *hot_tbs;     /* vaddr -> struct hot_tb */
struct hot_tb { uint64_t vaddr; uint64_t n_insns; uint64_t execs; };

static void hot_exec(unsigned int vcpu, void *udata)
{
    ((struct hot_tb *)udata)->execs++;
}

static int index_of(const char *name)
{
    gpointer v = g_hash_table_lookup(sym_index, name);
    if (v) return GPOINTER_TO_INT(v) - 1;
    if (nsyms == MAX_SYMS) return 0;
    int i = nsyms++;
    sym_names[i] = g_strdup(name);
    g_hash_table_insert(sym_index, (gpointer)sym_names[i], GINT_TO_POINTER(i + 1));
    return i;
}

static void tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    struct qemu_plugin_insn *first = qemu_plugin_tb_get_insn(tb, 0);
    const char *sym = qemu_plugin_insn_symbol(first);
    char region[48];
    if (!sym) {
        /* code with no symbol (a JIT's, a shared library's): by the 1 MB of address space it is in, which the
           program's /proc/self/maps names (panda3ds-bench --maps) */
        snprintf(region, sizeof region, "[nosym 0x%" PRIx64 "]", qemu_plugin_insn_vaddr(first) >> 20 << 20);
        sym = region;
        if (hot) {
            uint64_t va = qemu_plugin_insn_vaddr(first);
            g_mutex_lock(&lock);
            struct hot_tb *h = g_hash_table_lookup(hot_tbs, &va);
            if (!h) {
                h = g_new0(struct hot_tb, 1);
                h->vaddr = va;
                g_hash_table_insert(hot_tbs, &h->vaddr, h);
            }
            h->n_insns = n;
            g_mutex_unlock(&lock);
            qemu_plugin_register_vcpu_tb_exec_cb(tb, hot_exec, QEMU_PLUGIN_CB_NO_REGS, h);
        }
    }
    g_mutex_lock(&lock);
    int i = index_of(sym);
    g_mutex_unlock(&lock);
    qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(tb, QEMU_PLUGIN_INLINE_ADD_U64,
        (qemu_plugin_u64){ counts, i * sizeof(uint64_t) }, n);
}

/* a function's count on one thread (vCPU: threads in the order they started; 0 is the emulator's main thread) */
static uint64_t count_on(int i, unsigned vcpu)
{
    return qemu_plugin_u64_get((qemu_plugin_u64){ counts, i * sizeof(uint64_t) }, vcpu);
}

#define MAX_THREADS 16
static uint64_t last_other[MAX_THREADS][MAX_SYMS];

static void dump(uint64_t id, uint64_t value)
{
    g_mutex_lock(&lock);
    uint64_t total = 0;
    static uint64_t now[MAX_SYMS];
    for (int i = 0; i < nsyms; i++) { now[i] = count_on(i, 0); total += now[i]; }
    fprintf(out, "MARK %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", id, value, total);
    for (int i = 0; i < nsyms; i++) {
        if (now[i] != last[i]) fprintf(out, "%" PRIu64 "\t%s\n", now[i] - last[i], sym_names[i]);
        last[i] = now[i];
    }
    /* the other threads, function by function: "<count>\t@<thread> <function>" */
    unsigned threads = (unsigned)qemu_plugin_num_vcpus();
    for (unsigned v = 1; v < threads && v < MAX_THREADS; v++) {
        for (int i = 0; i < nsyms; i++) {
            uint64_t c = count_on(i, v);
            if (c != last_other[v][i]) fprintf(out, "%" PRIu64 "\t@%u %s\n", c - last_other[v][i], v, sym_names[i]);
            last_other[v][i] = c;
        }
    }
    fflush(out);
    g_mutex_unlock(&lock);
}

static void on_syscall(qemu_plugin_id_t id, unsigned int vcpu, int64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
{
    if (num == MARK_SYSCALL) dump(a1, a2);
}

static gint by_cost(gconstpointer a, gconstpointer b)
{
    const struct hot_tb *x = *(struct hot_tb *const *)a, *y = *(struct hot_tb *const *)b;
    uint64_t cx = x->execs * x->n_insns, cy = y->execs * y->n_insns;
    return cx < cy ? 1 : cx > cy ? -1 : 0;
}

static void at_exit(qemu_plugin_id_t id, void *p)
{
    dump(UINT64_MAX, 0);
    if (hot) {
        GPtrArray *all = g_ptr_array_new();
        GHashTableIter it; gpointer k, v;
        g_hash_table_iter_init(&it, hot_tbs);
        while (g_hash_table_iter_next(&it, &k, &v)) g_ptr_array_add(all, v);
        g_ptr_array_sort(all, by_cost);
        for (guint i = 0; i < all->len && i < 200; i++) {
            struct hot_tb *h = g_ptr_array_index(all, i);
            fprintf(out, "HOT 0x%" PRIx64 " insns=%" PRIu64 " execs=%" PRIu64 " total=%" PRIu64 "\n", h->vaddr, h->n_insns, h->execs, h->n_insns * h->execs);
        }
    }
    fclose(out);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info, int argc, char **argv)
{
    const char *path = "a55prof.txt";
    for (int i = 0; i < argc; i++) {
        if (strncmp(argv[i], "out=", 4) == 0) path = argv[i] + 4;
        if (strcmp(argv[i], "hot=1") == 0) hot = 1;
    }
    out = fopen(path, "w");
    if (!out) return -1;
    sym_index = g_hash_table_new(g_str_hash, g_str_equal);
    hot_tbs = g_hash_table_new(g_int64_hash, g_int64_equal);
    counts = qemu_plugin_scoreboard_new(MAX_SYMS * sizeof(uint64_t));
    index_of("[nosym]");
    qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans);
    qemu_plugin_register_vcpu_syscall_cb(id, on_syscall);
    qemu_plugin_register_atexit_cb(id, at_exit, NULL);
    return 0;
}
