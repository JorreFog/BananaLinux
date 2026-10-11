#!/bin/sh
# Copies the RG DS's hardware support from ROCKNIX into Banana's Buildroot tree, at a pinned ROCKNIX commit:
# the kernel patches ROCKNIX applies for RK3566 (in its order), the two handhelds' device trees, its kernel config
# (the base Banana trims with a fragment), the generic DSI panel driver, the joypad driver, its U-Boot patches and
# Rockchip's boot firmware for the RK3568.
# Banana builds these into its own kernel; the files stay as ROCKNIX wrote them (GPL-2.0), so a later sync is a
# plain diff.
#
#   tools/sync-rocknix-hw.sh [ROCKNIX commit]        (default: the one in buildroot/board/rgds/ROCKNIX)
#
# Needs git and network access to github.com.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
B=$HERE/buildroot/board/rgds
PIN=$B/ROCKNIX
COMMIT=${1:-$(sed -n 's/^commit=//p' "$PIN" 2>/dev/null)}
[ -n "$COMMIT" ] || { echo "usage: sync-rocknix-hw.sh <ROCKNIX commit>"; exit 1; }
JOYPAD_COMMIT=d02ed13aae08113f6f9e0e9d699cb29bb3450fa2   # hackbar/rocknix-joypad (ROCKNIX's pin was force-pushed away)
# Rockchip's TF-A (BL31) for the RK3568, the one ROCKNIX boots the RG DS with (Buildroot's rkbin lacks it; its DRAM
# init is the same version as ROCKNIX's, so that comes from Buildroot); redistributable under rkbin's LICENSE
RKBIN_COMMIT=74213af1e952c4683d2e35952507133b61394862
RKBIN_FILES="bin/rk35/rk3568_bl31_v1.45.elf LICENSE"

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
D=projects/ROCKNIX/devices/RK3566
git -C "$W" init -q r
git -C "$W/r" remote add origin https://github.com/ROCKNIX/distribution
git -C "$W/r" fetch -q --depth 1 --filter=blob:none origin "$COMMIT"
git -C "$W/r" sparse-checkout set --no-cone "/$D/" /projects/ROCKNIX/packages/linux/patches/ /packages/linux/patches/default/ \
    /projects/ROCKNIX/packages/linux-drivers/generic-dsi/sources/ /projects/ROCKNIX/packages/linux/package.mk
git -C "$W/r" checkout -q FETCH_HEAD
R=$W/r

# the kernel version ROCKNIX builds for RK3566
KVER=$(awk '/RK3576\|RK3566\)/{f=1} f&&/PKG_VERSION=/{gsub(/.*="|"/,"");print;exit}' $R/projects/ROCKNIX/packages/linux/package.mk)

rm -rf "$B/linux-patches" "$B/dts" "$B/kernel" "$B/uboot-patches"
mkdir -p "$B/linux-patches" "$B/dts" "$B/kernel" "$B/uboot-patches"
# ROCKNIX's order for RK3566: mainline, the device's, default, 7.2.6, mainline-rockchip (numbered so Buildroot keeps it)
n=0
for dir in projects/ROCKNIX/packages/linux/patches/mainline $D/patches/linux packages/linux/patches/default \
           projects/ROCKNIX/packages/linux/patches/7.2.6 projects/ROCKNIX/packages/linux/patches/mainline-rockchip; do
    n=$((n + 1))
    for p in $(ls "$R/$dir"/*.patch 2>/dev/null | sort); do
        cp "$p" "$B/linux-patches/$n-$(basename "$dir")-$(basename "$p")"
    done
done
# the joypad driver needs input-polldev, which mainline removed: ROCKNIX's RK3326 patch adds it back
git -C "$R" sparse-checkout add /projects/ROCKNIX/devices/RK3326/patches/linux/003-input-add-input-polldev-driver.patch \
    /projects/ROCKNIX/packages/emulators/standalone/drastic-sa/config/RK3566/drastic.cfg.rgds
cp "$R/projects/ROCKNIX/devices/RK3326/patches/linux/003-input-add-input-polldev-driver.patch" \
    "$B/linux-patches/6-rk3326-003-input-add-input-polldev-driver.patch"
# patches for Mali's kbase driver, which Banana doesn't use (it runs Panfrost), and for Bluetooth, which Banana's
# kernel leaves out (one of them needs fuzz to apply, which Buildroot's patch -F0 refuses)
rm -f "$B"/linux-patches/*mali* "$B"/linux-patches/*i915* "$B"/linux-patches/*Bluetooth*
# patches for hardware the two handhelds haven't (the kernel's "all" builds every Rockchip device tree, so a patch
# for another handheld's tree that assumes ROCKNIX's other patches breaks the build): device-tree patches that
# touch nothing the RG DS's tree includes (rk3568.dtsi, rk356x-base.dtsi), Qualcomm's display, the iMON remote,
# the panels of other handhelds, perf's Rust build, the initramfs warning
rm -f "$B"/linux-patches/*msm* "$B"/linux-patches/*imon* "$B"/linux-patches/*rust-build* "$B"/linux-patches/*initramfs* \
      "$B"/linux-patches/*st7703* "$B"/linux-patches/*st7701* "$B"/linux-patches/*nv3051d*
for p in "$B"/linux-patches/*.patch; do
    files=$(sed -n 's#^+++ b/##p' "$p")
    [ -n "$files" ] || continue
    echo "$files" | grep -qv '^arch/arm64/boot/dts/' && continue
    echo "$files" | grep -q 'rk356x-base\.dtsi\|rk3568' || rm -f "$p"
done

cp $R/$D/linux/dts/rockchip/rk3568-anbernic-rg-ds.dts $R/$D/linux/dts/rockchip/rk3568-anbernic-rg-ds-plus.dts "$B/dts/"
cp $R/$D/linux/linux.aarch64.conf "$B/linux-rocknix.config"
cp $R/projects/ROCKNIX/packages/linux-drivers/generic-dsi/sources/panel-generic-dsi.c "$B/kernel/"
# DraStic's settings and button map for the RG DS (the drastic package turns on 2x resolution and threaded 3D)
cp $R/projects/ROCKNIX/packages/emulators/standalone/drastic-sa/config/RK3566/drastic.cfg.rgds "$B/drastic.cfg"
cp $R/$D/patches/u-boot-Specific/*.patch "$B/uboot-patches/" 2>/dev/null ||
    cp $R/$D/packages/u-boot-Specific/patches/*.patch "$B/uboot-patches/"

git -C "$W" clone -q https://github.com/hackbar/rocknix-joypad j
git -C "$W/j" checkout -q $JOYPAD_COMMIT
cp "$W/j/rocknix-singleadc-joypad.c" "$W/j/rocknix-joypad.h" "$B/kernel/"

rm -rf "$B/rkbin"; mkdir -p "$B/rkbin"
for f in $RKBIN_FILES; do
    curl -fsSL -o "$B/rkbin/$(basename $f)" "https://raw.githubusercontent.com/rockchip-linux/rkbin/$RKBIN_COMMIT/$f"
done

cat > "$PIN" <<EOF
# The ROCKNIX commit Banana's RG DS hardware support comes from (tools/sync-rocknix-hw.sh)
commit=$COMMIT
linux=$KVER
joypad=$JOYPAD_COMMIT
rkbin=$RKBIN_COMMIT
EOF
echo "synced ROCKNIX $COMMIT: Linux $KVER, $(ls "$B/linux-patches" | wc -l) patches"
