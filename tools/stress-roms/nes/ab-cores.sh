#!/bin/bash
# ab-cores.sh <plus|rgds> <coreA.so> <coreB.so> <rom> <seconds> [rounds] [tag]   (derived from BananaFront tools/ab.sh)
# Same as ab.sh but BananaFront with core A vs BananaFront with core B (nes), unpinned (the governor), full-system sampling.
# (original header:) ab.sh <plus|rgds> <system> <core-name> <rom> <seconds> [rounds] [tag]
# RetroArch vs BananaFront on the same core and ROM, through the real menu launcher, alternating RA/BF `rounds`
# times (default 2). Each run: 8 s settle, then <seconds> measured the same way for both from outside:
#   the frontend process's CPU % (utime+stime of every thread), system busy % (all 4 cores), mean CPU clock, SoC
#   temperature start/end, RSS; BananaFront's own stats lines too. Run under the lock (locked.sh <dev> -w 1800 --).
# Out: ~/bf-runs/ab-<rom>/<date>-<dev>-<tag>/{results.tsv, *.log}
set -u
DEV=$1 SYS=nes COREA=$2 COREB=$3 ROM=$4 SEC=$5 ROUNDS=${6:-2} TAG=${7:-abc}
CORE=x
R=$HOME/banana-emu/BananaFront
BOARD=$([ "$DEV" = plus ] && echo rgds-plus || echo rgds)
BF_BIN=${BF_BIN:-$R/build/$BOARD/bananafront}
BF_LAUNCHER=${BF_LAUNCHER:-$HOME/ovn-front-bl/system/rootfs/usr/libexec/banana/run-bananafront}
BF_UI=${BF_UI:-$HOME/ovn-front-kit/xb/ui/banana-ui}
DS=~/.claude/skills/banana-device-test/scripts
dev() { ~/bananaos-0.4.3/dev.sh "$DEV" "$@"; }
B=$(basename "$ROM")
OUT=$HOME/bf-runs/abc-${B%.*}/$(date +%Y%m%d-%H%M%S)-$DEV-$TAG; mkdir -p "$OUT"
K=$(mktemp -d); trap 'rm -rf "$K"' EXIT
dev "cat /etc/banana/systems.conf" > "$K/systems.orig" || exit 1
mk_conf() {  # mk_conf <run line> <out>
    awk -v s="[$SYS]" -v r="run = $1" '/^\[/ { in_s = ($0 == s) } in_s && /^run *=/ { print r; next } { print }' \
        "$K/systems.orig" > "$2"
}
mk_conf "/run/bdev/bf/run-bananafront /run/bdev/bf/coreA.so %ROM%" "$K/sys-ra.conf"
mk_conf "/run/bdev/bf/run-bananafront /run/bdev/bf/coreB.so %ROM%" "$K/sys-bf.conf"
# a card with its own full systems.conf (the Plus): banana-ui merges it over the image's, so it is switched too
CARD=0; CARDF=
if dev "[ -f /data/banana/systems.conf ]"; then
    CARD=1; CARDF="$K/card-ra.conf:card-ra.conf $K/card-bf.conf:card-bf.conf"
    dev "cat /data/banana/systems.conf" > "$K/card.orig"
    awk -v s="[$SYS]" -v r="run = /run/bdev/bf/run-bananafront /run/bdev/bf/coreA.so %ROM%" '/^\[/ { in_s = ($0 == s) } in_s && /^run *=/ { print r; next } { print }' "$K/card.orig" > "$K/card-ra.conf"
    awk -v s="[$SYS]" -v r="run = /run/bdev/bf/run-bananafront /run/bdev/bf/coreB.so %ROM%" '/^\[/ { in_s = ($0 == s) } in_s && /^run *=/ { print r; next } { print }' "$K/card.orig" > "$K/card-bf.conf"
fi
cat > "$K/wrap" <<W
#!/bin/sh
export BANANAFRONT_BIN=/run/bdev/bf/bananafront BANANAFRONT_STATS=/run/bdev/bf/stats.txt ${BF_ENV:-}
exec /run/bdev/bf/launcher "\$@"
W
dev 'rm -rf /run/bdev/bf; mkdir -p /run/bdev/bf' || exit 1
dev "cat > /run/bdev/bf/coreA.so" < "$COREA" || exit 1
dev "cat > /run/bdev/bf/coreB.so" < "$COREB" || exit 1
for f in "$BF_BIN:bananafront" "$BF_LAUNCHER:launcher" "$K/wrap:run-bananafront" "$K/sys-ra.conf:sys-ra.conf" \
         "$K/sys-bf.conf:sys-bf.conf" $CARDF; do
    dev "cat > /run/bdev/bf/${f##*:} && chmod +x /run/bdev/bf/${f##*:}" < "${f%%:*}" || exit 1
done
# the sampler: one process, reads /proc once a second (cost ~nil next to a frontend)
cat > "$K/sample.sh" <<'S'
#!/bin/sh
# sample.sh <pid> <secs>: prints "cpu_pct sys_busy_pct mhz temp_start temp_end rss_kb"
P=$1 N=$2
tz=/sys/class/thermal/thermal_zone0/temp
pt() { awk '{print $14+$15}' /proc/$P/stat; }
st() { awk '/^cpu /{b=$2+$3+$4+$7+$8; t=b+$5+$6; print b, t}' /proc/stat; }
t0=$(cat $tz); p0=$(pt); set -- $(st); b0=$1; a0=$2; mhz=0
for i in $(seq $N); do sleep 1; m=0; for c in /sys/devices/system/cpu/cpu[0-3]/cpufreq/scaling_cur_freq; do m=$((m + $(cat $c))); done; mhz=$((mhz + m / 4000)); done
p1=$(pt); set -- $(st); b1=$1; a1=$2; t1=$(cat $tz)
hz=$(getconf CLK_TCK 2>/dev/null || echo 100)
rss=$(awk '/VmRSS/{print $2}' /proc/$P/status)
awk -v p=$((p1-p0)) -v n=$N -v hz=$hz -v b=$((b1-b0)) -v a=$((a1-a0)) -v m=$mhz -v t0=$t0 -v t1=$t1 -v r=$rss \
  'BEGIN { printf "%.1f %.1f %d %.1f %.1f %d\n", p*100/(n*hz), b*100/a, m/n, t0/1000, t1/1000, r }'
S
dev 'cat > /run/bdev/bf/sample.sh && chmod +x /run/bdev/bf/sample.sh' < "$K/sample.sh"
DROM=/data/roms/$SYS/$B
NEWROM=0
dev "[ -e '$DROM' ]" || { dev "mkdir -p /data/roms/$SYS && cat > '$DROM'" < "$ROM" && NEWROM=1; }
UIARG=; [ -f "$BF_UI" ] && UIARG="$BF_UI=/usr/bin/banana-ui"
echo -e "round\tcore(A=A,B=B)\tfrontend\tproc_cpu%\tsys_busy%\tmean_MHz\ttemp0\ttemp1\trss_kB" > "$OUT/results.tsv"
run_one() {  # run_one <ra|bf> <round>
    local fe=$1 rd=$2 name pid
    name=bananafront
    dev "umount -l /etc/banana/systems.conf 2>/dev/null; mount --bind /run/bdev/bf/sys-$fe.conf /etc/banana/systems.conf; if [ $CARD = 1 ]; then umount -l /data/banana/systems.conf 2>/dev/null; mount --bind /run/bdev/bf/card-$fe.conf /data/banana/systems.conf; fi"
    dev 'killall banana-ui; sleep 5' > /dev/null
    # wait until the menu runs again and is cool enough to compare
    dev 'for i in $(seq 60); do t=$(cat /sys/class/thermal/thermal_zone0/temp); [ $t -lt 50000 ] && break; sleep 2; done'
    sleep 4
    dev "rm -f /run/bdev/bf/stats.txt /run/banana/retroarch.log; echo '$DROM' > /run/banana/launch; for i in \$(seq 40); do pidof $name >/dev/null && break; sleep 0.5; done"
    pid=$(dev "pidof $name" | cut -d' ' -f1)
    [ -n "$pid" ] || { echo "$name did not start"; return 1; }
    sleep 8
    local r; r=$(dev "/run/bdev/bf/sample.sh $pid $SEC")
    echo "$rd $fe $r" | tr " " "\t" | tee -a "$OUT/results.tsv"
    dev 'echo QUIT > /run/banana/retroarch.cmd; sleep 3'
    dev "pidof $name >/dev/null && kill $pid; sleep 1"
    dev 'cat /run/bdev/bf/stats.txt' > "$OUT/stats-$fe-$rd.txt" 2>/dev/null
    true
}
# banana-ui with the lease for run-bananafront (bind-mounted for the whole A/B: the RetroArch runs use it too)
[ -n "$UIARG" ] && $DS/deploy.sh "$DEV" --restart ui $UIARG > /dev/null
for rd in $(seq "$ROUNDS"); do run_one ra $rd; run_one bf $rd; done
dev 'umount -l /etc/banana/systems.conf 2>/dev/null; grep -q " /data/banana/systems.conf " /proc/mounts && umount -l /data/banana/systems.conf'
$DS/deploy.sh "$DEV" undo > /dev/null
[ $NEWROM = 1 ] && dev "rm -f '$DROM'"
dev 'rm -rf /run/bdev/bf'
cat "$OUT/results.tsv"; echo "-> $OUT"
