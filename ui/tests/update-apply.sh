#!/bin/bash
# The update script's apply and switch on a made-up card (OUT/update-apply/<case>: /boot with extlinux.conf and the two
# slots' kernels, the two root partitions as files): what a failed or cut-off write leaves the next boot. A dd put
# first on PATH fails at the second 16 MB piece when DD_FAIL is set (a write error or the power going, as far as the
# card is concerned). Run with bash; the update script itself runs as POSIX sh (bash --posix, or dash if there).
HERE=$(cd "$(dirname "$0")/.." && pwd)
UPD=$HERE/../system/rootfs/usr/libexec/banana/update
OUT=${OUT:-$HERE/tests/out}/update-apply
SH=${UPDATE_SH:-"bash --posix"}; [ -z "$UPDATE_SH" ] && command -v dash >/dev/null && SH=dash
fails=0
bad() { echo "FAIL update-apply: $*"; fails=$((fails + 1)); }
REALDD=$(command -v dd)

# card <case>: a card running slot a (DEFAULT a), both slots with a system on them
card() {
    F=$OUT/$1; rm -rf "${F:?}"; mkdir -p "$F"/{boot/extlinux,boot/a,boot/b,dev,run,data/banana/updates,sys/class/block/mmcblk0p2,sys/class/block/mmcblk0p3,bin}
    sed 's/@DTB@/rk3568-anbernic-rg-ds-plus/g' "$HERE/../buildroot/board/rgds/extlinux.conf.in" > "$F/boot/extlinux/extlinux.conf"
    grep -q '^DEFAULT a$' "$F/boot/extlinux/extlinux.conf" || sed -i 's/^DEFAULT .*/DEFAULT a/' "$F/boot/extlinux/extlinux.conf"
    for s in a b; do echo "kernel-$s-old" > "$F/boot/$s/Image"; echo "dtb-$s-old" > "$F/boot/$s/rk3568-anbernic-rg-ds-plus.dtb"; done
    for p in 2 3; do head -c $((64 * 1048576)) /dev/zero | tr '\0' "$p" > "$F/dev/mmcblk0p$p"; echo $((64 * 2048)) > "$F/sys/class/block/mmcblk0p$p/size"; done
    echo "$F/dev/mmcblk0" > "$F/run/disk"; echo a > "$F/run/slot"
    printf '#!/bin/sh\nexit 0\n' > "$F/bin/mount"
    printf '#!/bin/sh\n# the second piece fails when DD_FAIL is set\nif [ -n "$DD_FAIL" ]; then for a in "$@"; do [ "$a" = seek=1 ] && exit 1; done; fi\nexec %s "$@"\n' "$REALDD" > "$F/bin/dd"
    chmod +x "$F/bin/mount" "$F/bin/dd"
    sed -e "s#/boot/#$F/boot/#g; s#/proc/sys/vm/drop_caches#/dev/null#; s#/sys/class/block/#$F/sys/class/block/#; s#\[ -b \"\$PART\" \]#[ -f \"\$PART\" ]#" \
        "$UPD" > "$F/update"
}
# pkg <case> <version> <fill byte>: a 40 MB package for the Plus in <case>/data/banana/updates
pkg() {
    d=$OUT/$1/pkgsrc; rm -rf "${d:?}"; U=banana-rgds-plus-$2.update; mkdir -p "$d/$U"
    echo "kernel-$2" > "$d/$U/Image"; echo "dtb-$2" > "$d/$U/rk3568-anbernic-rg-ds-plus.dtb"
    head -c $((40 * 1048576)) /dev/zero | tr '\0' "$3" > "$d/$U/rootfs.squashfs"
    echo "$2" > "$d/$U/VERSION"; echo rgds-plus > "$d/$U/board"
    (cd "$d/$U" && sha256sum Image ./*.dtb rootfs.squashfs | sed 's# \./# #' > sha256sums)
    tar -cf "$OUT/$1/data/banana/updates/$U.tar" -C "$d" "$U"; rm -rf "${d:?}"
    echo "$OUT/$1/data/banana/updates/$U.tar"
}
run() { F=$OUT/$1; shift; env PATH="$F/bin:$PATH" BANANA_RUN=$F/run BANANA_DATA=$F/data/banana BANANA_BOARD=rgds-plus BANANA_VERSION=0.5.2.2 $SH "$F/update" "$@" >/dev/null 2>&1; }
dflt() { sed -n 's/^DEFAULT *//p' "$OUT/$1/boot/extlinux/extlinux.conf" | head -n 1; }

# 1. an apply that works: slot b gets it and is the default
card ok; P=$(pkg ok 0.5.3 x)
run ok apply "$P" || bad "a good apply failed"
[ "$(dflt ok)" = b ] || bad "after a good apply the default is $(dflt ok), not b"
[ "$(cat "$OUT/ok/boot/b/Image")" = kernel-0.5.3 ] || bad "after a good apply slot b's kernel isn't the package's"

# 2. a second apply before the restart (Updates opened again lists the package again), cut off while writing slot b,
# which is the default already: the next boot must not be the half-written slot (the running one, a, instead)
card again; P=$(pkg again 0.5.3 x); run again apply "$P" || bad "again: the first apply failed"
P2=$(pkg again 0.5.4 y)
DD_FAIL=1 run again apply "$P2" && bad "again: the cut-off apply reported success"
[ "$(dflt again)" = a ] || bad "a cut-off apply left the default on the half-written slot $(dflt again) (should be a, the running one)"

# 3. an apply cut off while writing slot b (default a), then Previous version (update switch): refused, slot b's root
# is half written
card switch; P=$(pkg switch 0.5.3 x)
DD_FAIL=1 run switch apply "$P" && bad "switch: the cut-off apply reported success"
[ "$(dflt switch)" = a ] || bad "switch: a cut-off apply moved the default to $(dflt switch)"
run switch switch && bad "Previous version after a cut-off apply switched to the half-written slot"
[ "$(dflt switch)" = a ] || bad "after a refused switch the default is $(dflt switch), not a"

# 4. the same card applied again (nothing cut off this time): slot b whole and the default
run switch apply "$P" || bad "switch: the apply after the cut-off one failed"
[ "$(dflt switch)" = b ] || bad "switch: after a good apply the default is $(dflt switch), not b"
[ "$(cat "$OUT/switch/boot/b/Image")" = kernel-0.5.3 ] || bad "switch: slot b's kernel isn't the package's after the good apply"
run switch switch a || bad "switch: back to slot a refused (it holds a system)"
[ "$(dflt switch)" = a ] || bad "switch: back to a didn't happen"

# 5. a package whose sha256sums lists no kernel (a sums file cut short): refused before anything on the card changes,
# so Previous version (slot b's kernel) and slot b's root are as they were; and one without the root's line the same
for miss in Image rootfs.squashfs; do
    c=sums-$miss; card "$c"; P=$(pkg "$c" 0.5.3 x)
    d=$OUT/$c/re; mkdir -p "$d"; tar -xf "$P" -C "$d"; sed -i "/ $miss\$/d" "$d"/*/sha256sums; tar -cf "$P" -C "$d" "$(ls "$d")"; rm -rf "${d:?}"
    before=$(sha256sum < "$OUT/$c/dev/mmcblk0p3")
    run "$c" apply "$P" && bad "$c: a package whose sha256sums doesn't list $miss was installed"
    grep -q "^failed: .*sha256sums doesn't list $miss" "$OUT/$c/run/update-status" || bad "$c: the status doesn't say why: $(cat "$OUT/$c/run/update-status" 2>/dev/null)"
    [ "$(dflt "$c")" = a ] || bad "$c: the default moved to $(dflt "$c")"
    [ "$(cat "$OUT/$c/boot/b/Image" 2>/dev/null)" = kernel-b-old ] || bad "$c: slot b's kernel (Previous version) is gone though nothing was installed"
    [ "$(sha256sum < "$OUT/$c/dev/mmcblk0p3")" = "$before" ] || bad "$c: slot b's root was written though the package was refused"
    [ -e "$OUT/$c/data/banana/updates/.unpack" ] && bad "$c: the unpacked package was left on the card"
done

# 6. an apply (and a switch) while another is installing: refused, and the running one's progress line left alone
card busy; P=$(pkg busy 0.5.3 x)
echo "writing 42%" > "$OUT/busy/run/update-status"; mkdir -p "$OUT/busy/data/banana/updates/.unpack"; echo part > "$OUT/busy/data/banana/updates/.unpack/keep"
( flock 7; sleep 4 ) 7> "$OUT/busy/run/update-apply.lock" &
HOLD=$!; sleep 0.5
run busy apply "$P" && bad "busy: a second apply ran beside the first"
run busy apply /nonexistent && bad "busy: a second apply (no such package) reported success"
run busy switch b && bad "busy: a switch ran beside an apply"
[ "$(cat "$OUT/busy/run/update-status")" = "writing 42%" ] || bad "busy: the running apply's status was overwritten: $(cat "$OUT/busy/run/update-status")"
[ -f "$OUT/busy/data/banana/updates/.unpack/keep" ] || bad "busy: the running apply's unpacked package was deleted under it"
[ "$(dflt busy)" = a ] || bad "busy: the default moved to $(dflt busy)"
wait $HOLD
run busy apply "$P" || bad "busy: the apply after the first one ended failed"
[ "$(dflt busy)" = b ] || bad "busy: after a good apply the default is $(dflt busy), not b"

[ $fails = 0 ] && echo "update-apply: all passed ($SH)"
exit $((fails > 0))
