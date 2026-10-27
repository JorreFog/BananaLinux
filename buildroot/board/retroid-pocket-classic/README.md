# Retroid Pocket Classic (SM4450 / Rev Q) — BananaOS board

This board reuses **ROCKNIX's SM4450 hardware layer** (kernel 7.2 + patches, Rev Q
DTB, ADSP/WPSS/ACF/QCA firmware) and **OpenABL** (already flashed) instead of
U-Boot. Banana's userspace, extlinux A/B boot and exFAT data partition stay.

Boot flow:

```
OpenABL (abl_a/abl_b)
  └─ FAT p1 BANANA-BOOT / extlinux/extlinux.conf
       ├─ slot a: /a/Image + /a/sm4450-retroid-pocket-classic-revq.dtb
       └─ slot b: /b/Image + /b/... (after an update)
            ↓
       squashfs p2 banana-root-a (or p3 -b), exFAT p4 BANANA
```

Partitions: p1 boot (FAT), p2 root A, p3 root B, p4 data (created on first boot).

Kernel: `linux-rocknix.config` + 49 patches in `linux-patches/`, both imported
from ROCKNIX `next` at 2026-10-27. The copied common DTSI already has the
AW88166 right channel fix (`audio-channel = <1>`).

Firmware in `firmware/` is installed to `/lib/firmware/` by `post-build.sh`.
