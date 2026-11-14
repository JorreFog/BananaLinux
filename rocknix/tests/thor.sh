#!/bin/sh
# The AYN Thor's pieces on ROCKNIX, no ROCKNIX needed: rocknix-device (board thor and the Thor's profile lines on a Thor,
# the shared profile alone on another SM8550 handheld); the Thor's tools in their services mode (HW_SERVICES=systemd):
# pc-games' runtime in a plain folder (no ext4 image), run-pc in ROCKNIX's sway (no compositor of its own), usb-debug
# over ROCKNIX's usbgadget, tune's boot step with the Thor's ROCKNIX profile (only what ROCKNIX leaves alone);
# the installer on a made-up ROCKNIX checkout (every tool, the themes, the companions, the frontend's files).
HERE=$(cd "$(dirname "$0")/.." && pwd)
L=$HERE/../system/rootfs/usr/libexec/banana
S=$HERE/packages/ui/banana-frontend/sources
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL rocknix thor: $*"; fail=1; }

# ---- rocknix-device ----
mkdir -p "$T/share" "$T/run"
cp "$S/hw.conf" "$S/hw-thor.conf" "$T/share/"
dev() { env -u QUIRK_DEVICE BANANA_RUN="$T/run" BANANA_ROCKNIX_SHARE="$T/share" sh "$S/rocknix-device" "$@"; }
[ "$(dev "AYN Thor")" = thor ] && [ "$(cat "$T/run/board")" = thor ] || bad "the Thor's board: $(cat "$T/run/board")"
grep -qx 'HW_SERVICES=systemd' "$T/run/hw.conf" && grep -qx 'HW_HOME=yes' "$T/run/hw.conf" && grep -qx "HW_HOME_KEY=316,102,194" "$T/run/hw.conf" ||
    bad "the Thor's profile: $(tr '\n' '|' < "$T/run/hw.conf")"
# (the profile as the scripts read it: sourced, the Thor's name after the shared one)
( . "$T/run/hw.conf"; [ "$HW_NAME" = "AYN Thor" ] && [ "$HW_USB_DEBUG" = yes ] ) || bad "the Thor's lines don't win when sourced"
grep -q '^HW_ZRAM\|^HW_SYSCTL\|^HW_CPUIDLE_OFF\|^HW_OOM_PROTECT' "$T/run/hw.conf" && bad "a tuning ROCKNIX does itself is in the profile"
[ "$(dev "AYN Odin 2")" = rocknix ] || bad "another handheld's board: $(cat "$T/run/board")"
grep -q '^HW_HOME' "$T/run/hw.conf" && bad "the Thor's lines on another handheld"
grep -qx 'HW_SERVICES=systemd' "$T/run/hw.conf" || bad "no services mode on another handheld"
dev "AYN Thor" >/dev/null
cp "$T/run/hw.conf" "$T/hw.conf"

# ---- pc-games: the runtime in a folder of /data (ROCKNIX's /storage is ext4), nothing mounted ----
if command -v zstd >/dev/null && command -v xz >/dev/null; then
    mkdir -p "$T/src/wine-11.0-amd64-wow64/bin" "$T/src/dxvk-3.1.1/x64" "$T/src/dxvk-3.1.1/x32" "$T/src/vkd3d-proton-3.0.1/x64" "$T/src/vkd3d-proton-3.0.1/x86"
    printf '#!/bin/sh\n' > "$T/src/wine-11.0-amd64-wow64/bin/wine"; cp "$T/src/wine-11.0-amd64-wow64/bin/wine" "$T/src/wine-11.0-amd64-wow64/bin/wineserver"
    chmod +x "$T/src/wine-11.0-amd64-wow64/bin/"*
    for d in d3d8 d3d9 d3d10core d3d11 dxgi; do echo $d > "$T/src/dxvk-3.1.1/x64/$d.dll"; echo $d > "$T/src/dxvk-3.1.1/x32/$d.dll"; done
    for d in d3d12 d3d12core; do echo $d > "$T/src/vkd3d-proton-3.0.1/x64/$d.dll"; echo $d > "$T/src/vkd3d-proton-3.0.1/x86/$d.dll"; done
    (cd "$T/src" && tar -cf - wine-11.0-amd64-wow64 | xz > "$T/wine.tar.xz" && tar -czf "$T/dxvk.tar.gz" dxvk-3.1.1 && tar -cf - vkd3d-proton-3.0.1 | zstd -q > "$T/vkd3d.tar.zst")
    sum() { sha256sum "$1" | cut -d' ' -f1; }
    printf '#!/bin/sh\ncase $1 in *wine-*) cp "%s/wine.tar.xz" "$2" ;; *dxvk-*) cp "%s/dxvk.tar.gz" "$2" ;; *vkd3d-*) cp "%s/vkd3d.tar.zst" "$2" ;; *) exit 1 ;; esac\n' "$T" "$T" "$T" > "$T/fetch"
    cat > "$T/box64" <<BOX
#!/bin/sh
echo "box64 \$*" >> "$T/pclog"
case "\$*" in *wineboot*) mkdir -p "\$WINEPREFIX/drive_c/windows/system32" "\$WINEPREFIX/drive_c/windows/syswow64" "\$WINEPREFIX/dosdevices"; : > "\$WINEPREFIX/system.reg" ;; esac
exit 0
BOX
    chmod +x "$T/fetch" "$T/box64"
    (
        export BANANA_HW_CONF="$T/hw.conf" BANANA_PC="$T/pc" BANANA_PC_ROMS="$T/roms/windows" BANANA_PC_FETCH="$T/fetch" \
            BANANA_BOX64="$T/box64" BANANA_DATA="$T/data" BANANA_PC_NOART=1 BANANA_PC_NOBINFMT=1 \
            BANANA_PC_SUMS="$(sum "$T/wine.tar.xz") $(sum "$T/dxvk.tar.gz") $(sum "$T/vkd3d.tar.zst")"
        unset BANANA_PC_MNT BANANA_PC_NOMOUNT
        sh "$L/pc-games" setup > "$T/setup.out" 2>&1 || bad "pc-games setup in the folder mode: $(tail -n 2 "$T/setup.out")"
        [ -x "$T/pc/runtime/wine-11.0/bin/wine" ] && [ -f "$T/pc/runtime/prefix/system.reg" ] || bad "the runtime isn't in pc/runtime: $(ls "$T/pc" "$T/pc/runtime" 2>&1 | tr '\n' ' ')"
        [ -e "$T/pc/pc.ext4" ] && bad "an ext4 image was made on ROCKNIX"
        [ "$(sh "$L/pc-games" status | head -n 1)" = ready ] || bad "pc-games status: $(sh "$L/pc-games" status | head -n 1)"
        sh "$L/pc-games" scan; [ -f "$T/roms/windows/Steam.pc" ] || bad "no Steam entry after the setup"
        sh "$L/pc-games" umount || bad "umount failed in the folder mode"
        [ -x "$T/pc/runtime/wine-11.0/bin/wine" ] || bad "umount took the folder away"
    )
else
    echo "rocknix thor: pc-games' folder mode skipped (no zstd or xz)"
fi

# ---- run-pc under ROCKNIX's sway: the game run as it is, no compositor started, its output focused ----
mkdir -p "$T/lib" "$T/bin"
printf '#!/bin/sh\necho "pc-games $*" >> "%s/log"\n[ "$1" = run ] && exit 7\nexit 0\n' "$T" > "$T/lib/pc-games"
printf '#!/bin/sh\necho "run-wayland $*" >> "%s/log"\n' "$T" > "$T/lib/run-wayland"
printf '#!/bin/sh\necho "swaymsg $*" >> "%s/log"\n' "$T" > "$T/bin/swaymsg"
chmod +x "$T/lib/pc-games" "$T/lib/run-wayland" "$T/bin/swaymsg"
echo "exe = Game/game.exe" > "$T/Game.pc"
env PATH="$T/bin:$PATH" BANANA_HW_CONF="$T/hw.conf" BANANA_LIBEXEC="$T/lib" BANANA_TOP=DSI-2 BANANA_NO_SCOPE=1 sh "$L/run-pc" "$T/Game.pc"
rc=$?
[ $rc = 7 ] || bad "run-pc: the game's exit status was $rc"
grep -q "^pc-games run $T/Game.pc$" "$T/log" || bad "run-pc didn't run the game: $(cat "$T/log")"
grep -q "run-wayland" "$T/log" && bad "run-pc started a compositor of its own under ROCKNIX"
grep -q "^swaymsg focus output DSI-2$" "$T/log" || bad "run-pc didn't put the game on the top output"
grep -q "^pc-games umount$" "$T/log" || bad "run-pc didn't let go after the game"
# (BananaOS's own image: run-wayland as before)
: > "$T/log"; printf 'HW_SERVICES=\n' > "$T/hw-own.conf"
env PATH="$T/bin:$PATH" BANANA_HW_CONF="$T/hw-own.conf" BANANA_LIBEXEC="$T/lib" sh "$L/run-pc" "$T/Game.pc"
grep -q "^run-wayland --keep-bottom -- $T/lib/pc-games run $T/Game.pc$" "$T/log" || bad "run-pc on BananaOS's image: $(cat "$T/log")"

# ---- usb-debug over ROCKNIX's usbgadget ----
printf '#!/bin/sh\nM=%s/usbmode\ncase $1 in "") cat "$M" 2>/dev/null || echo disabled ;; network) echo network > "$M"; echo "usbgadget network" >> %s/log ;; disabled) echo disabled > "$M"; echo "usbgadget disabled" >> %s/log ;; address) echo 169.254.170.2 ;; esac\n' "$T" "$T" "$T" > "$T/bin/usbgadget"
printf '#!/bin/sh\necho "systemctl $*" >> %s/log\n' "$T" > "$T/bin/systemctl"
chmod +x "$T/bin/usbgadget" "$T/bin/systemctl"
mkdir -p "$T/data"
ud() { env PATH="$T/bin:$PATH" BANANA_HW_CONF="$T/hw.conf" BANANA_DATA="$T/data" BANANA_RUN="$T/run" sh "$L/usb-debug" "$@"; }
: > "$T/log"
ud up >/dev/null
grep -q "^usbgadget network$" "$T/log" && grep -q "^systemctl start sshd$" "$T/log" || bad "usb-debug up: $(cat "$T/log")"
[ "$(ud status)" = "up: network 169.254.170.2" ] || bad "usb-debug status: $(ud status)"
[ "$(ud up)" = "already up" ] || bad "a second up"
ud down >/dev/null; [ "$(ud status)" = down ] || bad "usb-debug down"
echo file_transfer > "$T/usbmode"; : > "$T/log"
ud up >/dev/null; grep -q "usbgadget network" "$T/log" && bad "usb-debug took the port from ROCKNIX's file transfer mode"
echo disabled > "$T/usbmode"; echo "usb_debug = off" > "$T/data/settings.conf"; : > "$T/log"
ud up >/dev/null; grep -q "usbgadget" "$T/log" && bad "usb-debug came up with usb_debug = off"
[ -d "$T/run/usb_gadget" ] && bad "usb-debug made a gadget of its own"
rm -f "$T/data/settings.conf"

# ---- tune boot with the Thor's ROCKNIX profile: MGLRU, huge pages, the card; no sysctl, no zram ----
mkdir -p "$T/sys/kernel/mm/lru_gen" "$T/sys/kernel/mm/transparent_hugepage" "$T/sys/block/mmcblk0/queue" "$T/proc/sys/vm" "$T/sys/block/zram0"
echo 0 > "$T/sys/kernel/mm/lru_gen/min_ttl_ms"; echo "always defer [madvise] never" > "$T/sys/kernel/mm/transparent_hugepage/defrag"
echo "[none] mq-deadline kyber" > "$T/sys/block/mmcblk0/queue/scheduler"; echo 128 > "$T/sys/block/mmcblk0/queue/read_ahead_kb"
echo 0 > "$T/proc/sys/vm/overcommit_memory"; : > "$T/sys/block/zram0/disksize"
env BANANA_SYS="$T/sys" BANANA_PROC="$T/proc" BANANA_HW_CONF="$T/hw.conf" BANANA_RUN="$T/run" sh "$L/tune" boot
env BANANA_SYS="$T/sys" BANANA_PROC="$T/proc" BANANA_HW_CONF="$T/hw.conf" BANANA_RUN="$T/run" sh "$L/tune" zram
[ "$(cat "$T/sys/kernel/mm/lru_gen/min_ttl_ms")" = 1000 ] || bad "tune: MGLRU"
[ "$(cat "$T/sys/kernel/mm/transparent_hugepage/defrag")" = defer ] || bad "tune: huge pages"
[ "$(cat "$T/sys/block/mmcblk0/queue/scheduler")" = mq-deadline ] && [ "$(cat "$T/sys/block/mmcblk0/queue/read_ahead_kb")" = 1024 ] || bad "tune: the card"
[ "$(cat "$T/proc/sys/vm/overcommit_memory")" = 0 ] && [ ! -s "$T/sys/block/zram0/disksize" ] || bad "tune touched what ROCKNIX's memory manager owns"

# ---- update: ROCKNIX's releases not offered on this image (they'd take the menu away) ----
printf '#!/bin/sh\necho "rocknix-update $*" >> %s/log\n[ "$1" = check ] && echo 20270101\n' "$T" > "$T/bin/rocknix-update"; chmod +x "$T/bin/rocknix-update"
mkdir -p "$T/upd"
env PATH="$T/bin:$PATH" BANANA_HW_CONF="$T/hw.conf" BANANA_DATA="$T/upd" BANANA_RUN="$T/run" sh "$L/update" check > "$T/check" 2>&1
grep -q '^state=current' "$T/check" || bad "update offered ROCKNIX's release: $(tr '\n' ' ' < "$T/check")"
grep -v '^HW_UPDATE' "$T/hw.conf" > "$T/hw-upd.conf"
env PATH="$T/bin:$PATH" BANANA_HW_CONF="$T/hw-upd.conf" BANANA_DATA="$T/upd" BANANA_RUN="$T/run" sh "$L/update" check > "$T/check" 2>&1
grep -q '^state=newer' "$T/check" || bad "update without HW_UPDATE=local: $(tr '\n' ' ' < "$T/check")"

# ---- the installer on a made-up ROCKNIX checkout ----
R=$T/rocknix
B=$R/projects/ROCKNIX
mkdir -p "$B/devices/SM8550" "$B/packages/virtual/emulators" "$B/packages/ui/emulationstation/config/common" \
    "$B/packages/emulators/libretro/retroarch/patches"
printf '  ADDITIONAL_PACKAGES="gamepadcalibration openabl inputplumber"\n' > "$B/devices/SM8550/options"
printf '  add_emu_core nds melonds melonds-sa true\n  add_es_system nds\n' > "$B/packages/virtual/emulators/package.mk"
printf '<?xml version="1.0"?>\n<features>\n</features>\n' > "$B/packages/ui/emulationstation/config/common/es_features.cfg"
sh "$HERE/../tools/rocknix-banana.sh" "$R" SM8550 > "$T/inst.out" 2>&1 || bad "the installer failed: $(tail -n 3 "$T/inst.out")"
P=$B/packages/ui/banana-frontend
for f in sysstat steam pc-games run-pc usb-debug tune run-rocknix backlight handheld; do
    [ -f "$P/sources/libexec/$f" ] || bad "the installer didn't copy $f"
done
for f in bin/banana-perf themes/thor-oled.json themes/thor-white.json themes/thor-purple.json themes/thor-rainbow.json themes/thor-oled companions \
         rocknix-device rocknix-frontend hw.conf hw-thor.conf; do
    [ -e "$P/sources/$f" ] || bad "no sources/$f in the package"
done
[ -f "$P/system.d/essway.service.d/banana.conf" ] || bad "no essway drop-in"
grep -q '^Delegate=yes' "$P/system.d/bananasway.service" || bad "bananasway doesn't delegate its control group"
[ -f "$B/packages/emulators/libretro/retroarch/patches/1001-banana-alsa-config-reread.patch" ] || bad "no RetroArch patch"
grep -q 'banana-frontend' "$B/devices/SM8550/options" || bad "banana-frontend not in ADDITIONAL_PACKAGES"
# (a second run leaves the same)
(cd "$B" && find . -type f | sort | xargs md5sum) > "$T/first.sum"
sh "$HERE/../tools/rocknix-banana.sh" "$R" SM8550 >/dev/null 2>&1
(cd "$B" && find . -type f | sort | xargs md5sum) | diff -q - "$T/first.sum" >/dev/null || bad "a second installer run changed the checkout"
bash -n "$S/rocknix-frontend" || bad "rocknix-frontend doesn't parse"
sh -n "$S/rocknix-device" || bad "rocknix-device doesn't parse"
[ $fail = 0 ] && echo "rocknix thor: all passed"
exit $fail
