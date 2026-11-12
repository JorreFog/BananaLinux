#!/bin/sh
# usr/libexec/banana/tune against a made-up /sys and /proc with the AYN Thor's profile: the sysctls, MGLRU, huge pages,
# the idle-state quirk, the card's scheduler and read-ahead, zram sized by the RAM (the Base's 8 GB and the Max's 16 GB)
# with its second algorithm and limit, the recompression pass, the daemons' OOM protection; and a profile with none of
# the keys (the RG DS's) touching nothing.
#   sh system/tests/tune-test.sh
cd "$(dirname "$0")/../.." || exit 1
TU=$PWD/system/rootfs/usr/libexec/banana/tune
T=$(mktemp -d)
trap '[ -f "$T/run/zram-recompress.pid" ] && kill "$(cat "$T/run/zram-recompress.pid")" 2>/dev/null; rm -rf "$T"' EXIT
passed=0 failed=0
check() { if [ "$1" = "$2" ]; then passed=$((passed + 1)); echo "  ok: $3"; else failed=$((failed + 1)); echo "  FAIL: $3 (got '$1', want '$2')"; fi; }
SH=sh; command -v busybox >/dev/null && SH="busybox ash"   # (rcS's shell where there is one)

# the made-up handheld: <ram kB>
mk() {
    rm -rf "$T/sys" "$T/proc" "$T/run"
    S=$T/sys P=$T/proc
    mkdir -p "$P/sys/vm" "$S/kernel/mm/lru_gen" "$S/kernel/mm/transparent_hugepage" "$S/module/zswap/parameters" \
        "$S/devices/system/cpu/cpu0/cpuidle/state1" "$S/block/mmcblk0/queue" "$S/block/mmcblk0p1" "$S/block/sda/queue" \
        "$S/block/zram0" "$T/run"
    printf 'MemTotal:       %s kB\nMemFree:        100 kB\n' "$1" > "$P/meminfo"
    for f in watermark_scale_factor vfs_cache_pressure dirty_background_bytes dirty_bytes overcommit_memory swappiness page-cluster; do
        echo 0 > "$P/sys/vm/$f"
    done
    echo 0 > "$S/kernel/mm/lru_gen/min_ttl_ms"
    echo 'always defer defer+madvise [madvise] never' > "$S/kernel/mm/transparent_hugepage/defrag"
    echo Y > "$S/module/zswap/parameters/enabled"
    echo 0 > "$S/devices/system/cpu/cpu0/cpuidle/state1/disable"
    for d in mmcblk0 sda; do echo '[none] mq-deadline kyber bfq' > "$S/block/$d/queue/scheduler"; echo 128 > "$S/block/$d/queue/read_ahead_kb"; done
    echo 'lzo-rle [lz4] zstd' > "$S/block/zram0/comp_algorithm"
    for f in recomp_algorithm disksize mem_limit idle recompress; do : > "$S/block/zram0/$f"; done
    printf 'Filename Type Size Used Priority\n' > "$P/swaps"
    # processes: udevd (pid 10), bananad (20), the menu (30)
    for p in 10:udevd 20:bananad 30:banana-ui; do mkdir -p "$P/${p%%:*}"; echo "${p#*:}" > "$P/${p%%:*}/comm"; echo 0 > "$P/${p%%:*}/oom_score_adj"; done
}
run() { env BANANA_SYS="$T/sys" BANANA_PROC="$T/proc" BANANA_RUN="$T/run" BANANA_HW_CONF="$HW" $SH "$TU" "$@"; }
v() { cat "$1"; }
# the Thor's profile, as shipped (the keys tune reads)
HW=$T/thor.conf
grep -E '^HW_(ZRAM|SYSCTL|MGLRU_TTL_MS|THP_DEFRAG|CPUIDLE_OFF|IO|OOM_PROTECT)' system/rootfs/etc/banana/hw/thor.conf > "$HW"

echo "tune boot: the Thor's profile"
mk 7680000
run boot
check "$(v "$P/sys/vm/watermark_scale_factor") $(v "$P/sys/vm/dirty_background_bytes") $(v "$P/sys/vm/dirty_bytes") $(v "$P/sys/vm/overcommit_memory")" \
    "100 33554432 134217728 1" "HW_SYSCTL into /proc/sys"
check "$(v "$S/kernel/mm/lru_gen/min_ttl_ms")" 1000 "MGLRU's min_ttl_ms"
check "$(v "$S/kernel/mm/transparent_hugepage/defrag")" defer "THP defrag"
check "$(v "$S/devices/system/cpu/cpu0/cpuidle/state1/disable")" 1 "cpu0's state1 off"
check "$(v "$S/module/zswap/parameters/enabled")" N "zswap off in front of zram"
check "$(v "$S/block/mmcblk0/queue/scheduler") $(v "$S/block/mmcblk0/queue/read_ahead_kb")" "mq-deadline 1024" "the card's scheduler and read-ahead"
check "$(v "$S/block/sda/queue/scheduler") $(v "$S/block/sda/queue/read_ahead_kb")" "[none] mq-deadline kyber bfq 128" "the UFS (no rule) left alone"

echo "tune io: a card put in later, a scheduler the kernel hasn't"
mkdir -p "$S/block/mmcblk1/queue"; echo '[none] kyber' > "$S/block/mmcblk1/queue/scheduler"; echo 128 > "$S/block/mmcblk1/queue/read_ahead_kb"
run io mmcblk1
check "$(v "$S/block/mmcblk1/queue/scheduler") $(v "$S/block/mmcblk1/queue/read_ahead_kb")" "[none] kyber 1024" "no mq-deadline: the scheduler kept, the read-ahead set"
run io mmcblk0p1; check $? 0 "a partition (no queue): nothing, no error"

echo "tune zram: sized by the RAM"
run zram
check "$(v "$S/block/zram0/disksize")" 3750M "Base (7.5 GB seen): half"
check "$(v "$S/block/zram0/mem_limit")" 1875M "... a quarter as its limit"
check "$(v "$S/block/zram0/comp_algorithm")" lz4 "LZ4 first"
check "$(v "$S/block/zram0/recomp_algorithm")" "algo=zstd priority=1" "zstd as the second"
check "$(v "$P/sys/vm/swappiness") $(v "$P/sys/vm/page-cluster")" "100 0" "swappiness and page-cluster"
check "$(v "$S/block/zram0/idle")" all "pages marked idle for the first pass"
[ -f "$T/run/zram-recompress.pid" ] && kill -0 "$(cat "$T/run/zram-recompress.pid")" 2>/dev/null; check $? 0 "the recompression loop runs"
kill "$(cat "$T/run/zram-recompress.pid")" 2>/dev/null; rm -f "$T/run/zram-recompress.pid"
mk 15400000; run zram
check "$(v "$S/block/zram0/disksize")" 7519M "Max (15 GB seen): half"
kill "$(cat "$T/run/zram-recompress.pid")" 2>/dev/null; rm -f "$T/run/zram-recompress.pid"
mk 7680000; echo '/dev/zram0 partition 4 0 100' >> "$P/swaps"; run zram
check "$(v "$S/block/zram0/disksize")" "" "already swapping on zram0: left as it is"
mk 7680000; echo 'HW_ZRAM=2048' >> "$HW"; run zram
check "$(v "$S/block/zram0/disksize")" 2048M "a size in MB (the old profile's form)"
kill "$(cat "$T/run/zram-recompress.pid")" 2>/dev/null; rm -f "$T/run/zram-recompress.pid"
grep -v '^HW_ZRAM=2048' "$HW" > "$HW.n" && mv "$HW.n" "$HW"
mk 7680000; echo 'HW_ZRAM=lots' >> "$HW"; run zram
check "$(v "$S/block/zram0/disksize")" "" "a size that is no size: nothing"
grep -v '^HW_ZRAM=lots' "$HW" > "$HW.n" && mv "$HW.n" "$HW"

echo "tune recompress: one pass"
mk 7680000; : > "$S/block/zram0/idle"
run recompress; check "$(v "$S/block/zram0/recompress")" "" "no zram swap yet: nothing"
echo '/dev/zram0 partition 4 0 100' >> "$P/swaps"; run recompress
check "$(v "$S/block/zram0/recompress")" "type=idle threshold=1024 priority=1" "the idle pages over 1 KB recompressed"
check "$(v "$S/block/zram0/idle")" all "every page marked idle again"

echo "tune protect"
run protect
check "$(v "$P/10/oom_score_adj") $(v "$P/20/oom_score_adj") $(v "$P/30/oom_score_adj")" "-900 -900 0" "udevd and bananad protected, the menu not"

echo "a profile without the keys (the RG DS's): nothing touched"
HW=$PWD/system/rootfs/etc/banana/hw/rgds.conf
mk 2000000
before=$(cd "$T" && find sys proc -type f | sort | while read -r f; do printf '%s=%s\n' "$f" "$(cat "$f")"; done)
run boot; run zram; run protect; run recompress
after=$(cd "$T" && find sys proc -type f | sort | while read -r f; do printf '%s=%s\n' "$f" "$(cat "$f")"; done)
check "$([ "$before" = "$after" ] && echo same)" same "boot, zram, protect and recompress change nothing"
[ ! -f "$T/run/zram-recompress.pid" ]; check $? 0 "no recompression loop"
run nonsense 2>/dev/null; check $? 2 "an unknown step is refused"

echo "tune: $passed passed, $failed failed"
[ "$failed" = 0 ]
