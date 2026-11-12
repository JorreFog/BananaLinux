---
name: rocknix-sync
description: Pull hardware support (kernel patches, device trees, kernel config, firmware, bootloader pins) from ROCKNIX into BananaOS and prove it still fits before a build. Use when updating board/rgds or board/thor from ROCKNIX, bumping the ROCKNIX pin or the kernel version, adding a ROCKNIX device, or when a Banana kernel build fails on a patch or config.
---

# Syncing hardware support from ROCKNIX

BananaOS doesn't write board support; it takes ROCKNIX's (github.com/ROCKNIX/distribution, GPL-2.0) at a pinned
commit, unchanged, so a re-sync is a diff. Banana's own changes sit beside it: a kernel fragment, `dts-patches/`.

| Board | Script | Pin file | ROCKNIX device dir |
|---|---|---|---|
| RG DS, RG DS Plus | `tools/sync-rocknix-hw.sh [commit]` | `buildroot/board/rgds/ROCKNIX` | `projects/ROCKNIX/devices/RK3566` |
| AYN Thor | `tools/sync-rocknix-sm8550.sh [commit]` | `buildroot/board/thor/ROCKNIX` | `projects/ROCKNIX/devices/SM8550` |

Firmware: `buildroot/package/rocknix-extra-firmware` pins ROCKNIX/extra-firmware (the commit ROCKNIX's
`packages/linux-firmware/extra-firmware/package.mk` uses); linux-firmware files ROCKNIX lists in
`devices/<SoC>/config/kernel-firmware.dat` that Buildroot's options miss go in `external.mk`'s `LINUX_FIRMWARE_FILES`.
OpenABL (SM8550's bootloader): `buildroot/package/openabl`, ROCKNIX's `packages/tools/openabl` pin.

## Reading ROCKNIX without cloning it all

```sh
git init -q r && cd r && git remote add origin https://github.com/ROCKNIX/distribution
git fetch -q --depth 1 --filter=blob:none origin <commit>
git sparse-checkout set --no-cone /projects/ROCKNIX/devices/SM8550/ /projects/ROCKNIX/packages/linux/
git checkout -q FETCH_HEAD
git ls-tree -r --name-only HEAD | grep -i <thing>     # find files anywhere without fetching blobs
```

Patch order is `projects/ROCKNIX/packages/linux/package.mk`'s `PKG_PATCH_DIRS` (`${LINUX} mainline ${DEVICE} default`,
plus the version directory); the kernel version per SoC is its `case ${DEVICE}`. The sync scripts number the copied
patches by directory so Buildroot keeps the order; `external.mk` applies them with `patch -p1 -N` (fuzz allowed, as
ROCKNIX does), not Buildroot's `-F0`.

## Prove it before a 1-hour build

Do all three after every sync (a few minutes; scratch dir anywhere):

```sh
# 1. every patch applies to the pinned kernel, in order
curl -fsSLO https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-<ver>.tar.xz   # sha256: the pin file's linux_sha256
mkdir k && tar -xJf linux-<ver>.tar.xz -C k --strip-components=1 && cd k
for p in $(ls <repo>/buildroot/board/<dir>/linux-patches/*.patch | sort); do
  patch -p1 -N --no-backup-if-mismatch -s < $p >/dev/null || echo "FAILED $p"; done
# 2. the config with Banana's fragments keeps what we asked for
cp <repo>/buildroot/board/<dir>/linux-rocknix.config .config
ARCH=arm64 scripts/kconfig/merge_config.sh -m .config <repo>/buildroot/board/rgds/linux-banana.fragment <repo>/buildroot/board/<dir>/linux-<board>.fragment
make ARCH=arm64 olddefconfig
for l in $(grep -hE '^CONFIG_|^# CONFIG_.* is not set' <fragments> | sed 's/ /~/g'); do
  l=$(echo "$l" | tr '~' ' '); grep -qxF "$l" .config || echo "not kept: $l"; done
# 3. the device tree compiles
cp <repo>/buildroot/board/<dir>/dts/* arch/arm64/boot/dts/<vendor>/
echo 'dtb-$(CONFIG_ARCH_QCOM) += <name>.dtb' >> arch/arm64/boot/dts/qcom/Makefile   # if not listed
make ARCH=arm64 <vendor>/<name>.dtb
```

"not kept" lines are fine only when they are a later fragment's deliberate override or a symbol that doesn't exist
for that SoC (Rockchip options on a Qualcomm config). Anything else: find the dependency (`make menuconfig`, search
the symbol). Known one: disabling `CONFIG_VT` needs `CONFIG_EXPERT=y` (ROCKNIX's RK3566 config has it, SM8550's not).

## Banana's own kernel patches

`board/<dir>/linux-patches-banana/` is applied after the synced `linux-patches/` and is never touched by a sync. Put
patches that aren't ROCKNIX's there, e.g. the Thor's from Banana Pocket, with a README saying where each came from.
After a re-sync, run check 1 again with both directories in order: a ROCKNIX change can make one of them conflict.

## Pruning

Drop what the board can't use or Banana doesn't build (the sync scripts list them): Intel display hacks, iMON,
perf's Rust build, the initramfs warning, other handhelds' panel patches when they break the build. Never edit a
synced patch in place; add a Banana patch after it, or a `dts-patches/` patch for the device tree.
