#!/bin/sh
# Copies the AYN Thor's hardware support from ROCKNIX into Banana's Buildroot tree (buildroot/board/thor), at a
# pinned ROCKNIX commit: the kernel patches ROCKNIX applies for SM8550 (in its order), the Thor's device tree and the
# .dtsi it shares with the Odin 2 family, and ROCKNIX's SM8550 kernel config (the base Banana trims with a fragment).
# The RG DS's counterpart is tools/sync-rocknix-hw.sh; the files stay as ROCKNIX wrote them (GPL-2.0), so a later
# sync is a plain diff.
#
#   tools/sync-rocknix-sm8550.sh [ROCKNIX commit]      (default: the one in buildroot/board/thor/ROCKNIX)
#
# Needs git and network access to github.com.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
B=$HERE/buildroot/board/thor
PIN=$B/ROCKNIX
COMMIT=${1:-$(sed -n 's/^commit=//p' "$PIN" 2>/dev/null)}
[ -n "$COMMIT" ] || { echo "usage: sync-rocknix-sm8550.sh <ROCKNIX commit>"; exit 1; }
DTS="qcs8550-ayn-thor.dts qcs8550-ayn-common.dtsi"

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
D=projects/ROCKNIX/devices/SM8550
git -C "$W" init -q r
git -C "$W/r" remote add origin https://github.com/ROCKNIX/distribution
git -C "$W/r" fetch -q --depth 1 --filter=blob:none origin "$COMMIT"
git -C "$W/r" sparse-checkout set --no-cone "/$D/linux/" "/$D/patches/linux/" /projects/ROCKNIX/packages/linux/patches/ \
    /packages/linux/patches/default/ /projects/ROCKNIX/packages/linux/package.mk
git -C "$W/r" checkout -q FETCH_HEAD
R=$W/r

# the kernel version ROCKNIX builds for SM8550 (its tarball from kernel.org; the 7.2 patch directory goes with it)
KVER=$(awk '/SM8550/{f=1} f&&/PKG_VERSION=/{gsub(/.*="|"/,"");print;exit}' $R/projects/ROCKNIX/packages/linux/package.mk)
KSHA=$(awk '/SM8550/{f=1} f&&/PKG_SHA256=/{gsub(/.*="|"/,"");print;exit}' $R/projects/ROCKNIX/packages/linux/package.mk)
[ -n "$KVER" ] || { echo "no SM8550 kernel version in ROCKNIX's linux/package.mk"; exit 1; }

rm -rf "$B/linux-patches" "$B/dts"
mkdir -p "$B/linux-patches" "$B/dts"
# ROCKNIX's order for SM8550 (linux/package.mk's PKG_PATCH_DIRS): mainline, the device's, default, then the patch
# directory named after the kernel version (numbered so Buildroot keeps it)
n=0
for dir in projects/ROCKNIX/packages/linux/patches/mainline $D/patches/linux packages/linux/patches/default \
           projects/ROCKNIX/packages/linux/patches/$KVER; do
    n=$((n + 1))
    for p in $(ls "$R/$dir"/*.patch 2>/dev/null | sort); do
        cp "$p" "$B/linux-patches/$n-$(basename "$dir")-$(basename "$p")"
    done
done
# what the Thor hasn't or Banana doesn't build: Intel's display, the iMON remote, perf's Rust build, the initramfs
# warning (Banana boots without one), the RK3326 joypad's ADC-keys redirect
rm -f "$B"/linux-patches/*i915* "$B"/linux-patches/*imon* "$B"/linux-patches/*rust-build* \
      "$B"/linux-patches/*initramfs* "$B"/linux-patches/*adc-keys-redirect*
for f in $DTS; do cp "$R/$D/linux/dts/qcom/$f" "$B/dts/"; done
cp "$R/$D/linux/linux.aarch64.conf" "$B/linux-rocknix.config"

printf 'commit=%s\nlinux=%s\nlinux_sha256=%s\n' "$COMMIT" "$KVER" "$KSHA" > "$PIN"
# the defconfig's kernel tarball follows ROCKNIX's version
sed -i "s|^BR2_LINUX_KERNEL_CUSTOM_TARBALL_LOCATION=.*|BR2_LINUX_KERNEL_CUSTOM_TARBALL_LOCATION=\"https://cdn.kernel.org/pub/linux/kernel/v${KVER%%.*}.x/linux-$KVER.tar.xz\"|" \
    "$HERE/buildroot/configs/banana_thor_defconfig"
echo "board/thor: ROCKNIX $COMMIT, Linux $KVER, $(ls "$B/linux-patches" | wc -l) patches, $(echo $DTS | wc -w) device-tree files"
