#!/bin/sh
# This route against ROCKNIX's real SM8550 image, not stand-ins: what the package and BananaOS's scripts take from
# ROCKNIX is checked in its released root file system.
#
#   rocknix/tests/real-image.sh <ROCKNIX SM8550 root fs> [aarch64 build: banana-ui (SDL=1 ALSA=1) and its helpers]
#
# The root fs, from a release's update tar (its SYSTEM is a squashfs):
#   curl -LO https://github.com/ROCKNIX/distribution/releases/download/20261101/ROCKNIX-SM8550.aarch64-20261101.tar
#   tar -xf ROCKNIX-SM8550.aarch64-20261101.tar
#   unsquashfs -d rootfs ROCKNIX-SM8550.aarch64-20261101/target/SYSTEM
# Skipped without one (rocknix/tests/run.sh runs it when ROCKNIX_ROOTFS names one).
#
# 1. Anywhere: the files and their shapes. runemu.sh's arguments, es_systems.cfg converted (every system with a start,
#    DOS as [dos], the Windows, Steam and Heroic shelves as ROCKNIX's), every system's game page rows from the real
#    es_features.cfg and system.cfg, the board for every SM8550 handheld ROCKNIX knows (thor on the AYN Thor only), the
#    autostart and UI_SERVICE order, essway's unit, the AYN Thor's sway lines, the tools the scripts call.
# 2. As root on a machine that runs aarch64 programs (qemu-user with binfmt_misc's F flag): the image's own bash,
#    busybox, python3 and ROCKNIX's tool scripts in a chroot over an overlay (the root fs isn't changed), a made-up
#    /sys of an AYN Thor under it: ROCKNIX's quirks for the Thor, 099-banana-frontend and UI_SERVICE, handheld over
#    ROCKNIX's ledcontrol, analog_sticks_ledcontrol, gpu_overclock and fancontrol, backlight against ROCKNIX's
#    brightness, run-rocknix's settings read back by ROCKNIX's get_setting, Wi-Fi (a stand-in nmcli), usb-debug over
#    ROCKNIX's usbgadget, pc-games, update (HW_UPDATE=local), sysstat, the archive tools pc-games and steam use.
# 3. With the build: each binary's libraries and symbol versions in the image (readelf); with 2 as well, banana-ui
#    headless at the Thor's two screens from the installed package's paths, the converted systems and the Thor OLED
#    theme ($OUT/real-*.png), and BANANA_REAL_SWAY=1 adds ROCKNIX's own sway headless (pixman) with the Thor's lines
#    and start_banana.sh as bananasway starts it ($OUT/real-sway-*.png; a minute or more under qemu).
HERE=$(cd "$(dirname "$0")/.." && pwd)
TOP=$HERE/..
R=$1
BUILD=$2
OUT=${OUT:-$HERE/tests/out/real-image}
[ -n "$R" ] && [ -f "$R/usr/bin/runemu.sh" ] || { echo "real-image test skipped (no ROCKNIX root fs given)"; exit 0; }
R=$(cd "$R" && pwd)
L=$TOP/system/rootfs/usr/libexec/banana
S=$HERE/packages/ui/banana-frontend/sources
T=$(mktemp -d)
fail=0
bad() { echo "FAIL real-image: $*"; fail=1; }
have() { [ -e "$R/$1" ] || [ -L "$R/$1" ] || bad "no $1 in the image"; }
M=
# (what still runs in the chroot is stopped before the overlay goes; the overlay unmounted before anything is removed,
# so nothing is removed through it)
in_chroot() { for p in /proc/[0-9]*; do [ "$(readlink "$p/root" 2>/dev/null)" = "$M" ] && echo "${p#/proc/}"; done; }
cleanup() {
    if [ -n "$M" ]; then
        for i in 1 2 3 4 5; do k=$(in_chroot); [ -n "$k" ] || break; kill -9 $k 2>/dev/null; sleep 0.5; done
        umount "$M/proc" 2>/dev/null; umount "$M" 2>/dev/null || umount -l "$M" 2>/dev/null
        mountpoint -q "$M" 2>/dev/null && { echo "real-image: $M is still mounted; $T left as it is"; return; }
    fi
    rm -rf "$T"
}
trap cleanup EXIT
mkdir -p "$OUT"
. "$R/etc/os-release" 2>/dev/null
echo "real-image: ROCKNIX $OS_VERSION ($HW_DEVICE, $BUILD_BRANCH $(echo "$BUILD_ID" | cut -c1-8)); BananaOS's pin $(sed -n 's/^commit=//p' "$HERE/ROCKNIX" | cut -c1-8)"

# ---- 1. files and shapes ----
for f in usr/bin/runemu.sh usr/bin/start_es.sh usr/bin/es_settings usr/bin/setsettings.sh usr/bin/autostart \
         usr/config/emulationstation/es_systems.cfg usr/config/emulationstation/es_features.cfg \
         usr/config/system/configs/system.cfg etc/profile.d/001-functions usr/lib/systemd/system/essway.service \
         usr/lib/systemd/system/sway.service usr/lib/systemd/system/fancontrol.service \
         usr/lib/systemd/system/inputplumber.service usr/lib/autostart/common/111-sway-init \
         usr/lib/autostart/quirks/platforms/SM8550/090-ui_service usr/lib/autostart/quirks/platforms/SM8550/bin/gpu_overclock \
         usr/lib/autostart/quirks/platforms/SM8550/bin/fancontrol "usr/lib/autostart/quirks/devices/AYN Thor/bin/ledcontrol" \
         "usr/lib/autostart/quirks/devices/AYN Thor/bin/analog_sticks_ledcontrol" usr/share/inputplumber/devices/02-ayn-thor-controller.yaml \
         usr/bin/ledcontrol usr/bin/analog_sticks_ledcontrol usr/bin/fancontrol usr/bin/brightness usr/bin/usbgadget usr/bin/box64 \
         usr/bin/inputplumber usr/bin/python3 usr/bin/curl usr/bin/unzip usr/bin/sha256sum usr/bin/xzcat usr/bin/zcat usr/bin/tar \
         usr/bin/grim usr/bin/jq usr/bin/swaymsg usr/bin/sway usr/bin/systemd-run usr/bin/foot usr/bin/nmcli usr/bin/wpctl \
         usr/bin/bluetoothctl usr/bin/rocknix-update usr/lib/libSDL2-2.0.so.0 usr/lib/libfreetype.so.6 usr/lib/libdrm.so.2 \
         usr/lib/libasound.so.2 usr/lib/libcurl.so.4 usr/lib/libssl.so.3 usr/lib/libcrypto.so.3 usr/lib/libz.so.1 usr/lib/libc.so.6; do
    have "$f"
done
# (no zstd program: pc-games unpacks VKD3D-Proton's .tar.zst with Python's compression.zstd instead)
ls "$R"/usr/lib/python3*/lib-dynload/_zstd*.so >/dev/null 2>&1 || bad "python3 has no zstd module (pc-games' VKD3D-Proton)"
grep -aq 'wayland' "$R/usr/lib/libSDL2-2.0.so.0" || bad "SDL2 without Wayland"
# runemu.sh: <rom> -P<system> --core= --emulator= --controllers=, each game in games.slice
grep -q 'PLATFORM="${ARGUMENTS##\*-P}"' "$R/usr/bin/runemu.sh" && grep -q 'CORE="${ARGUMENTS##\*--core=}"' "$R/usr/bin/runemu.sh" &&
    grep -q 'EMULATOR="${ARGUMENTS##\*--emulator=}"' "$R/usr/bin/runemu.sh" || bad "runemu.sh reads its arguments another way"
grep -q -- '--slice=games.slice' "$R/usr/bin/runemu.sh" || bad "runemu.sh no longer puts games in games.slice (HW_GAME_CGROUP)"
grep -q 'get_setting "cpugovernor" "${PLATFORM}" "${ROMNAME##\*/}"' "$R/usr/bin/runemu.sh" &&
    grep -q 'get_setting "gpuperf" "${PLATFORM}" "${ROMNAME##\*/}"' "$R/usr/bin/runemu.sh" || bad "runemu.sh's per-game governor/GPU keys changed (run-rocknix)"
grep -q 'J_CONF="${J_DIR}/configs/system.cfg"' "$R/etc/profile.d/001-functions" && grep -q 'J_CONF_LOCK="/tmp/.system.cfg.lock"' "$R/etc/profile.d/001-functions" ||
    bad "ROCKNIX's settings file or its lock moved (rocknix-options)"
grep -q '^ExecStart=/usr/bin/start_es.sh' "$R/usr/lib/systemd/system/essway.service" || bad "essway.service starts something else (its drop-in)"
# autostart: platform quirks (090-ui_service), common scripts (099-banana-frontend), /etc/profile again, UI_SERVICE
grep -q 'UI_SERVICE="sway.service essway.service"' "$R/usr/lib/autostart/quirks/platforms/SM8550/090-ui_service" || bad "SM8550's UI_SERVICE changed"
awk '/autostart\/common/ { c = NR } /^\. \/etc\/profile/ { p = NR } /systemctl start \$\{UI_SERVICE\}/ { u = NR } END { exit !(c && p > c && u > p) }' "$R/usr/bin/autostart" ||
    bad "autostart no longer reads profile.d after the common scripts before starting UI_SERVICE"
grep -q 'for CONFIG in /storage/.config/profile.d/\*' "$R/etc/profile" || bad "/etc/profile doesn't read /storage/.config/profile.d"
# the AYN Thor's sway lines that start_banana.sh answers: the second output off and turned upright by an exec, its
# touchscreen disabled, both touchscreens by their device tree labels
SI=$R/usr/lib/autostart/common/111-sway-init
grep -q 'QUIRK_DEVICE}" = "AYN Thor"' "$SI" || bad "111-sway-init has no AYN Thor block"
grep -qF "swaymsg output \${second_con} transform 90; sleep 1; swaymsg output \${second_con} power off" "$SI" ||
    bad "the Thor's second output is set up another way (start_banana.sh waits for that exec)"
grep -q '"0:0:bottom_touchscreen" events disabled' "$SI" && grep -q '"0:0:top_touchscreen"' "$SI" || bad "the Thor's touchscreens' names changed (BANANA_TOUCH)"
grep -q 'SDL_VIDEO_DISPLAY_PRIORITY=${con},${second_con}' "$SI" || bad "no SDL_VIDEO_DISPLAY_PRIORITY (start_banana.sh's second output)"
grep -q 'export SWAYSOCK=' "$R/etc/profile.d/050-sway.conf" || bad "SWAYSOCK isn't in ROCKNIX's profile"
# InputPlumber merges the Thor's pad and AYN key (BANANA_INPUT_IGNORE)
grep -q 'rsinput-gamepad' "$R/usr/share/inputplumber/devices/02-ayn-thor-controller.yaml" &&
    grep -q 'gpio-keys-ayn' "$R/usr/share/inputplumber/devices/02-ayn-thor-controller.yaml" || bad "InputPlumber's Thor sources changed (BANANA_INPUT_IGNORE)"
# the Thor's LEDs and fan as handheld drives them
grep -q 'multi-led${1}${n}/leds/rgb:${1}${n}/multi_intensity' "$R/usr/lib/autostart/quirks/devices/AYN Thor/bin/analog_sticks_ledcontrol" ||
    bad "the stick rings' sysfs changed (handheld's rings)"
grep -q 'get_setting "cooling.profile"' "$R/usr/lib/autostart/quirks/platforms/SM8550/bin/fancontrol" || bad "fancontrol's profile setting changed"

# the converter on the real es_systems.cfg
ES=$R/usr/config/emulationstation/es_systems.cfg
python3 -I "$S/es-systems-to-banana.py" "$ES" "$T/systems.conf" || bad "the converter failed on the real es_systems.cfg"
n_es=$(python3 -I -c 'import sys, xml.etree.ElementTree as E; r = E.parse(sys.argv[1]).getroot(); print(sum(1 for s in r.findall("system") if (s.findtext("command") or "").strip() not in ("/usr/bin/true", "")))' "$ES")
n_out=$(grep -c '^run = ' "$T/systems.conf")
# (the menu's own pages after ROCKNIX's systems: PC games, Store, Döda Kvarter, Bshare, Activity, Apps, Files)
[ "$n_out" = $((n_es + 7)) ] || bad "$n_es systems with a start in es_systems.cfg, $n_out shelves out"
python3 -I - "$T/systems.conf" <<'EOF' || fail=1
import sys, re
secs, cur = {}, None
for ln in open(sys.argv[1], encoding='utf-8'):
    ln = ln.rstrip('\n')
    m = re.match(r'^\[(.*)\]$', ln)
    if m:
        cur = m.group(1)
        if cur in secs: print('FAIL real-image: [%s] twice' % cur)
        secs[cur] = {}
    elif cur and ' = ' in ln:
        k, v = ln.split(' = ', 1); secs[cur][k] = v
bad = 0
for sid, kv in secs.items():
    run = kv.get('run', '')
    if not run: print('FAIL real-image: [%s] has no start' % sid); bad = 1
    if 'run-rocknix' in run and (re.search(r'--core=( |$)', run) or re.search(r'--emulator=( |$)', run)):
        print('FAIL real-image: [%s] starts with no core or emulator: %s' % (sid, run)); bad = 1
    if '%' in run.replace('%ROM%', ''): print('FAIL real-image: [%s] left a placeholder: %s' % (sid, run)); bad = 1
    for k, v in kv.items():
        if any(0xE000 <= ord(c) <= 0xF8FF for c in v) or v != v.strip():
            print('FAIL real-image: [%s] %s = %r' % (sid, k, v)); bad = 1
want = {'dos': 'run-rocknix pc %ROM% --core=dosbox_pure --emulator=retroarch',
        'windows': 'run-rocknix windows %ROM% --core=wine --emulator=wine',
        'steam': 'run-rocknix steam %ROM% --core=steam --emulator=steam',
        'heroic': 'run-rocknix heroic %ROM% --core=heroic --emulator=heroic',
        'nds': 'run-rocknix nds %ROM% --core=melonds-sa --emulator=melonds',
        'pc': 'run-pc %ROM%', 'tools': '/usr/bin/foot %ROM%'}
for sid, w in want.items():
    if w not in secs.get(sid, {}).get('run', ''): print('FAIL real-image: [%s] run is %r' % (sid, secs.get(sid, {}).get('run'))); bad = 1
if 'imageviewer' in secs: print('FAIL real-image: EmulationStation\'s own screenshot viewer became a shelf'); bad = 1
if list(secs)[0] != 'nds': print('FAIL real-image: the first shelf is [%s]' % list(secs)[0]); bad = 1
sys.exit(bad)
EOF
# every system's game page rows from the real es_features.cfg and system.cfg
cp "$R/usr/config/system/configs/system.cfg" "$T/system.cfg"
export ROCKNIX_SETTINGS="$T/system.cfg" ROCKNIX_ES_SYSTEMS="$ES" ROCKNIX_ES_FEATURES="$R/usr/config/emulationstation/es_features.cfg" \
    ROCKNIX_SHADERS="$R/usr/share/common-shaders" ROCKNIX_LOCK="$T/lock"
for s in $(sed -n 's/^\[\(.*\)\]$/\1/p' "$T/systems.conf"); do
    grep -A6 "^\[$s\]$" "$T/systems.conf" | grep -q '^run = .*run-rocknix' || continue
    python3 -I "$L/rocknix-options" rows "$s" "A Game.zip" > "$T/rows" 2> "$T/rows.err" || { bad "rocknix-options rows $s: $(tail -n 1 "$T/rows.err")"; continue; }
    head -n 1 "$T/rows" | grep -q '^emulator	' || bad "rocknix-options rows $s: no emulator row first"
done
python3 -I "$L/rocknix-options" set dos "KEEN.EXE" emulator retroarch/dosbox_core && grep -qxF 'pc["KEEN.EXE"].core=dosbox_core' "$T/system.cfg" ||
    bad "rocknix-options set on dos isn't ROCKNIX's pc"
unset ROCKNIX_SETTINGS ROCKNIX_ES_SYSTEMS ROCKNIX_ES_FEATURES ROCKNIX_SHADERS ROCKNIX_LOCK
# the board for each handheld ROCKNIX has quirks for (their names are the device tree models): thor on the AYN Thor only
mkdir -p "$T/share"; cp "$S/hw.conf" "$S/hw-thor.conf" "$T/share/"
ls "$R/usr/lib/autostart/quirks/devices" | grep -q '^AYN Thor$' || bad "ROCKNIX has no quirks for \"AYN Thor\" (the device tree's model)"
ls "$R/usr/lib/autostart/quirks/devices" | while IFS= read -r m; do
    b=$(env -u QUIRK_DEVICE BANANA_RUN="$T/run" BANANA_ROCKNIX_SHARE="$T/share" sh "$S/rocknix-device" "$m")
    if [ "$m" = "AYN Thor" ]; then [ "$b" = thor ] || echo "FAIL real-image: the AYN Thor's board is $b"
    else [ "$b" = rocknix ] || echo "FAIL real-image: $m's board is $b"; fi
done | grep FAIL && fail=1

# ---- 2. ROCKNIX's own programs in a chroot (an overlay over the root fs) ----
part2=
if [ "$(id -u)" = 0 ] && command -v chroot >/dev/null && chroot "$R" /usr/bin/true 2>/dev/null; then
    mkdir -p "$T/u" "$T/w" "$T/m"
    if mount -t overlay overlay -o "lowerdir=$R,upperdir=$T/u,workdir=$T/w" "$T/m" 2>/dev/null; then
        M=$T/m; part2=1
    fi
fi
if [ -z "$part2" ]; then
    echo "real-image: part 2 skipped (needs root, overlayfs and a way to run aarch64 programs: qemu-user-static with binfmt_misc)"
else
    # (/proc, and /dev's few nodes made in the overlay: a container's /dev may have no /dev/fd, which bash's process
    # substitution in ROCKNIX's scripts needs)
    mount -t proc proc "$M/proc"
    for n in "null 1 3" "zero 1 5" "random 1 8" "urandom 1 9" "tty 5 0"; do
        set -- $n; [ -c "$M/dev/$1" ] || { rm -f "$M/dev/$1"; mknod -m 666 "$M/dev/$1" c "$2" "$3"; }
    done
    ln -sfn /proc/self/fd "$M/dev/fd"; ln -sfn /proc/self/fd/0 "$M/dev/stdin"; ln -sfn /proc/self/fd/1 "$M/dev/stdout"
    ln -sfn /proc/self/fd/2 "$M/dev/stderr"; mkdir -p "$M/dev/shm"
    # the package as its install step puts it (package.mk), from this repository
    mkdir -p "$M/usr/libexec/banana" "$M/usr/share/banana/rocknix" "$M/etc/banana" "$M/usr/lib/autostart/common" "$M/run/banana" "$M/tmp/t"
    cp "$L"/* "$M/usr/libexec/banana/"; cp "$S/rocknix-device" "$S/rocknix-frontend" "$M/usr/libexec/banana/"
    cp "$TOP/system/rootfs/usr/bin/banana-perf" "$S/start_banana.sh" "$M/usr/bin/"
    cp "$S/es-systems-to-banana.py" "$S/hw.conf" "$S/hw-thor.conf" "$M/usr/share/banana/rocknix/"
    cp "$S/099-banana-frontend" "$M/usr/lib/autostart/common/"
    chmod 0755 "$M"/usr/libexec/banana/* "$M/usr/bin/banana-perf" "$M/usr/bin/start_banana.sh" "$M/usr/lib/autostart/common/099-banana-frontend"
    ln -sf /run/banana/hw.conf "$M/etc/banana/hw.conf"; ln -sf /run/banana/board "$M/etc/banana/board"
    ln -sfn /storage/.config/banana-data "$M/data"
    mkdir -p "$M/storage/.config/banana-data/banana" "$M/storage/roms" "$M/storage/.cache/usbgadget"
    # stand-ins for what needs a running system: systemctl (no systemd in a chroot), NetworkManager
    printf '#!/bin/sh\necho "systemctl $*" >> /tmp/t/log\ncase $1 in is-active|is-enabled) exit 1 ;; esac\nexit 0\n' > "$M/usr/bin/systemctl"
    cp "$HERE/tests/fake/nmcli" "$M/usr/bin/nmcli"; chmod +x "$M/usr/bin/systemctl" "$M/usr/bin/nmcli"
    # an AYN Thor's /sys: the model, both panels' backlights under their DSI hosts (ae94000 the bottom, DSI-1;
    # ae96000 the top, DSI-2), the stick rings, the fan, the GPU, the battery, the USB port in network mode
    Y=$M/sys
    mkdir -p "$Y/firmware/devicetree/base" "$Y/class/backlight" "$Y/class/hwmon/hwmon0" "$Y/class/devfreq" "$Y/class/power_supply/battery" "$Y/class/udc/a600000.usb"
    printf 'AYN Thor\0' > "$Y/firmware/devicetree/base/model"
    for h in ae94000 ae96000; do
        d=/sys/devices/platform/soc@0/ae00000.display-subsystem/$h.dsi/$h.dsi.0
        mkdir -p "$M$d/backlight/$h.dsi.0"
        echo 4095 > "$M$d/backlight/$h.dsi.0/max_brightness"; echo 2048 > "$M$d/backlight/$h.dsi.0/brightness"
        ln -s "$d" "$M$d/backlight/$h.dsi.0/device"; ln -s "$d/backlight/$h.dsi.0" "$Y/class/backlight/$h.dsi.0"
    done
    for s in l r; do for n in 1 2 3 4; do
        d=$Y/devices/platform/multi-led$s$n/leds/rgb:$s$n; mkdir -p "$d"; echo 0 > "$d/brightness"; echo "0 0 0" > "$d/multi_intensity"
    done; done
    echo 0 > "$Y/class/hwmon/hwmon0/pwm1"; echo 1 > "$Y/class/hwmon/hwmon0/pwm1_enable"
    G=/sys/devices/platform/soc@0/3d00000.gpu/devfreq/3d00000.gpu; mkdir -p "$M$G"
    echo 680000000 > "$M$G/max_freq"; echo "220000000 680000000 1000000000" > "$M$G/available_frequencies"
    echo simple_ondemand > "$M$G/governor"; echo "simple_ondemand performance powersave" > "$M$G/available_governors"
    ln -s "$G" "$Y/class/devfreq/3d00000.gpu"
    echo 80 > "$Y/class/power_supply/battery/capacity"; echo Discharging > "$Y/class/power_supply/battery/status"
    echo Battery > "$Y/class/power_supply/battery/type"
    echo cdc > "$Y/class/udc/a600000.usb/function"; echo 'IP=169.254.170.2' > "$M/storage/.cache/usbgadget/network.conf"
    MD=$M/storage/.config/banana-data/banana
    c() { timeout "${REAL_TIMEOUT:-300}" chroot "$M" /bin/bash -c "$1" 2>>"$T/chroot.err"; }
    # ROCKNIX's boot for the Thor: its SM8550 and AYN Thor quirks (the profile.d files: GPU_FREQ, the fan, the rings),
    # then 099-banana-frontend among the common scripts, then UI_SERVICE as autostart reads it
    c '. /etc/profile; export QUIRK_DEVICE="AYN Thor"; for s in /usr/lib/autostart/quirks/platforms/SM8550/* "/usr/lib/autostart/quirks/devices/AYN Thor"/*; do [ -f "$s" ] && timeout 20 "$s" >/dev/null 2>&1; done; /usr/lib/autostart/common/099-banana-frontend'
    [ "$(c '. /etc/profile; echo "$UI_SERVICE"')" = "sway.service bananasway.service" ] || bad "UI_SERVICE after autostart: $(c '. /etc/profile; echo "$UI_SERVICE"')"
    [ "$(cat "$M/run/banana/board" 2>/dev/null)" = thor ] || bad "099-banana-frontend's board on the Thor: $(cat "$M/run/banana/board" 2>/dev/null)"
    c '. /etc/banana/hw.conf; [ "$HW_NAME" = "AYN Thor" ] && [ "$HW_SERVICES" = systemd ]' || bad "the Thor's hw.conf"
    c '. /etc/profile.d/001-functions; set_setting banana.frontend 0; /usr/lib/autostart/common/099-banana-frontend; . /etc/profile; [ "$UI_SERVICE" = "sway.service essway.service" ]' ||
        bad "banana.frontend = 0 doesn't give EmulationStation back"
    c '. /etc/profile.d/001-functions; del_setting banana.frontend; /usr/lib/autostart/common/099-banana-frontend'
    # the tools the scripts use, as busybox and the image have them
    mkdir -p "$T/a/w/bin"; echo wine > "$T/a/w/bin/wine"; echo dd > "$T/a/DepotDownloader"
    (cd "$T/a" && tar -cf w.tar w && xz -k w.tar && gzip -k w.tar && (cd "$T/a" && python3 -c 'import zipfile; z = zipfile.ZipFile("dd.zip", "w"); z.write("DepotDownloader"); z.close()'))
    command -v zstd >/dev/null && zstd -q -k "$T/a/w.tar"
    cp "$T"/a/w.tar.* "$T/a/dd.zip" "$M/tmp/t/"
    c 'cd /tmp/t && mkdir -p x1 x2 x3 && xzcat w.tar.xz | tar -xf - -C x1 --strip-components=1 && [ -f x1/bin/wine ] && zcat w.tar.gz | tar -xf - -C x2 && [ -f x2/w/bin/wine ] && unzip -o -q dd.zip DepotDownloader -d x3 && [ -f x3/DepotDownloader ] && sha256sum dd.zip | grep -q dd.zip && flock -n /tmp/t/lk true && date -d @0 +%Y >/dev/null' ||
        bad "the image's tar, xzcat, zcat, unzip, sha256sum, flock or date as pc-games, steam and update use them"
    if [ -f "$M/tmp/t/w.tar.zst" ]; then
        c 'cd /tmp/t && mkdir -p x4 && eval "$(sed -n "/^unzst() {/,/^}/p" /usr/libexec/banana/pc-games)" && unzst w.tar.zst | tar -xf - -C x4 && [ -f x4/w/bin/wine ]' ||
            bad "pc-games' unzst (VKD3D-Proton's .tar.zst) on the image"
    fi
    # handheld over ROCKNIX's tools: the caps, the rings, the GPU boost, the fan
    hh() { c ". /etc/profile; export BANANA_DATA=/data/banana; /usr/libexec/banana/handheld $1"; }
    [ "$(hh caps | tr '\n' ' ')" = "fan leds gpuboost " ] || bad "handheld caps on the Thor: $(hh caps | tr '\n' ' ')"
    hh "leds red high" >/dev/null
    [ "$(cat "$Y/devices/platform/multi-ledl1/leds/rgb:l1/multi_intensity")" = "0 0 255" ] && [ "$(cat "$Y/devices/platform/multi-ledr4/leds/rgb:r4/brightness")" = 255 ] ||
        bad "handheld leds red: the rings have $(cat "$Y/devices/platform/multi-ledl1/leds/rgb:l1/multi_intensity") at $(cat "$Y/devices/platform/multi-ledr4/leds/rgb:r4/brightness")"
    [ "$(c '. /etc/profile.d/001-functions; get_setting led.color')" = rgb ] && [ "$(c '. /etc/profile.d/001-functions; get_setting analogsticks.led')" = "255 255 0 0 255 0 0" ] ||
        bad "handheld leds: ROCKNIX's led.color/analogsticks.led"
    hh "leds off" >/dev/null
    [ "$(cat "$Y/devices/platform/multi-ledl1/leds/rgb:l1/multi_intensity")" = "0 0 0" ] || bad "handheld leds off through ROCKNIX's ledcontrol"
    hh "gpu on" >/dev/null
    [ "$(cat "$M$G/max_freq")" = 1000000000 ] && [ "$(c '. /etc/profile.d/001-functions; get_setting enable.gpu-overclock')" = 1 ] ||
        bad "handheld gpu on didn't go through ROCKNIX's gpu_overclock ($(cat "$M$G/max_freq"))"
    hh "gpu off" >/dev/null; [ "$(cat "$M$G/max_freq")" = 680000000 ] || bad "handheld gpu off"
    : > "$M/tmp/t/log"; hh "fan aggressive" >/dev/null
    [ "$(c '. /etc/profile.d/001-functions; get_setting cooling.profile')" = aggressive ] && grep -q 'systemctl restart fancontrol' "$M/tmp/t/log" ||
        bad "handheld fan: cooling.profile and fancontrol"
    # the backlights: the top by the DSI hosts' order, ROCKNIX's per-screen settings in its brightness script's order
    bl() { c ". /etc/profile; export BANANA_DATA=/data/banana BANANA_TOP=DSI-2; /usr/libexec/banana/backlight $1"; }
    [ "$(bl names)" = "ae96000.dsi.0 ae94000.dsi.0" ] || bad "backlight names: $(bl names)"
    bl "set 0 800"; bl "set 1 300"
    [ "$(c '. /etc/profile.d/001-functions; get_setting display.brightness2')" = 80 ] && [ "$(c '. /etc/profile.d/001-functions; get_setting display.brightness')" = 30 ] ||
        bad "backlight's ROCKNIX settings: $(grep display.brightness "$M/storage/.config/system/configs/system.cfg" | tr '\n' ' ')"
    # (ROCKNIX's brightness hotkey on screen 2 steps the top one from the menu's level)
    c '/usr/bin/brightness up 2' >/dev/null
    [ "$(cat "$M/sys/devices/platform/soc@0/ae00000.display-subsystem/ae96000.dsi/ae96000.dsi.0/backlight/ae96000.dsi.0/brightness")" = 3480 ] ||
        bad "ROCKNIX's brightness up 2 didn't step the top screen from 80 to 85 %"
    # run-rocknix: the performance mode as ROCKNIX's per-game settings, read back by ROCKNIX's get_setting after a change
    printf '#!/bin/sh\necho "runemu $*" >> /tmp/t/log\n' > "$M/tmp/t/runemu"; chmod +x "$M/tmp/t/runemu"
    rr() { c "echo '$1' > /run/banana/perf; . /etc/profile; export BANANA_DATA=/data/banana BANANA_RUNEMU=/tmp/t/runemu; /usr/libexec/banana/run-rocknix psp '/storage/roms/psp/A Game.iso' --core=ppsspp-sa --emulator=ppsspp --controllers="; }
    rr "performance performance"; rr "schedutil auto"
    [ "$(c '. /etc/profile.d/001-functions; get_setting cpugovernor psp "A Game.iso"')" = schedutil ] &&
        [ "$(c '. /etc/profile.d/001-functions; get_setting gpuperf psp "A Game.iso"')" = auto ] ||
        bad "run-rocknix: ROCKNIX reads $(c '. /etc/profile.d/001-functions; get_setting cpugovernor psp "A Game.iso"') for the game's governor after a change"
    grep -q '^runemu /storage/roms/psp/A Game.iso -Ppsp --core=ppsspp-sa --emulator=ppsspp --controllers=$' "$M/tmp/t/log" || bad "run-rocknix's runemu.sh line: $(grep runemu "$M/tmp/t/log" | tail -n 1)"
    # (ROCKNIX's own set_setting on a game's key: when this says replaced, run-rocknix could use it again)
    c '. /etc/profile.d/001-functions; set_setting "nds[\"B.nds\"].cpugovernor" performance; set_setting "nds[\"B.nds\"].cpugovernor" powersave; [ "$(get_setting cpugovernor nds B.nds)" = powersave ]' &&
        echo "real-image: note: ROCKNIX's set_setting now replaces a game's key"
    # Wi-Fi (NetworkManager's stand-in), usb-debug over ROCKNIX's usbgadget, pc-games, update, sysstat
    sv() { c ". /etc/profile; export BANANA_DATA=/data/banana FAKE_NM_LOG=/tmp/t/log FAKE_NM_UP=/tmp/t/nm-up; $1"; }
    sv '/usr/libexec/banana/wifi scan' | grep -q 'Banana Home' || bad "wifi scan"
    echo 'network={
	ssid="Banana Home"
	psk="password1"
}' > "$M/storage/.config/banana-data/banana/wifi.conf"
    sv '/usr/libexec/banana/wifi up' >/dev/null 2>&1
    grep -q 'device wifi connect Banana Home password password1' "$M/tmp/t/log" && [ "$(c '. /etc/profile.d/001-functions; get_setting wifi.ssid')" = "Banana Home" ] ||
        bad "wifi up: NetworkManager and ROCKNIX's wifi.ssid"
    [ "$(sv '/usr/libexec/banana/usb-debug status')" = "up: network 169.254.170.2" ] || bad "usb-debug status over ROCKNIX's usbgadget: $(sv '/usr/libexec/banana/usb-debug status')"
    sv '/usr/libexec/banana/pc-games status' > "$T/pc" 2>&1
    grep -qx 'not set up' "$T/pc" && grep -qx 'box64: yes' "$T/pc" || bad "pc-games status: $(tr '\n' '|' < "$T/pc")"
    [ -e "$M/storage/.config/banana-data/pc/pc.ext4" ] && bad "pc-games made an ext4 image on ROCKNIX"
    sv 'PATH=/tmp/t:$PATH /usr/libexec/banana/update check' > "$T/upd" 2>&1
    grep -q '^state=current' "$T/upd" || bad "update check with HW_UPDATE=local: $(tr '\n' '|' < "$T/upd")"
    sv '/usr/libexec/banana/sysstat' > "$T/stat" 2>&1
    grep -q '^mem_total_mb=' "$T/stat" && grep -q '^battery=80' "$T/stat" || bad "sysstat: $(tr '\n' ' ' < "$T/stat" | cut -c1-200)"
fi

# ---- 3. the build: libraries and symbol versions, then banana-ui on the image ----
if [ -z "$BUILD" ]; then
    echo "real-image: part 3 skipped (no aarch64 build given)"
else
    RE=$(command -v aarch64-linux-gnu-readelf || command -v readelf)
    bins=
    for b in banana-ui banana-scrape banana-sync banana-bshare banana-fetch banana-activity; do
        if [ -f "$BUILD/$b" ]; then bins="$bins $BUILD/$b"; elif [ -f "$BUILD/ui/$b" ]; then bins="$bins $BUILD/ui/$b"; else bad "no $b in $BUILD"; fi
    done
    READELF=$RE python3 -I - "$R" $bins <<'EOF' || fail=1
import os, re, subprocess, sys
root, bins = sys.argv[1], sys.argv[2:]
RE = os.environ['READELF']
def resolve(p):
    for _ in range(16):
        if not os.path.islink(p): return p
        t = os.readlink(p)
        p = os.path.join(root, t.lstrip('/')) if t.startswith('/') else os.path.join(os.path.dirname(p), t)
    return p
def rd(*a): return subprocess.run([RE] + list(a), capture_output=True, text=True).stdout
def syms(p):
    und, defs = set(), set()
    for ln in rd('--dyn-syms', '-W', p).splitlines():
        f = ln.split()
        if len(f) < 8 or not f[0].rstrip(':').isdigit(): continue
        n = f[7]
        if f[6] == 'UND':
            if '@' in n and f[4] != 'WEAK': und.add(n)
        else: defs.add(n.replace('@@', '@'))
    return und, defs
bad = 0
for b in bins:
    libs = {}
    for n in re.findall(r'\(NEEDED\).*\[(.*)\]', rd('-d', '-W', b)):
        p = resolve(os.path.join(root, 'usr/lib', n))
        if not os.path.exists(p): print('FAIL real-image: %s needs %s, not in the image' % (os.path.basename(b), n)); bad = 1
        else: libs[n] = p
    und, _ = syms(b)
    have = set()
    for p in libs.values(): have |= syms(p)[1]
    for s in sorted(und - have): print('FAIL real-image: %s needs %s, not in the image\'s libraries' % (os.path.basename(b), s)); bad = 1
    key = lambda v: [int(x) for x in re.findall(r'\d+', v)]
    g = sorted({s.split('@')[1] for s in und if '@GLIBC_2' in s}, key=key)
    x = sorted({s.split('@')[1] for s in und if '@GLIBCXX_' in s}, key=key)
    print('real-image: %s: %s; up to %s, %s' % (os.path.basename(b), ' '.join(sorted(libs)), g[-1] if g else '-', x[-1] if x else '-'))
sys.exit(bad)
EOF
    if [ -n "$part2" ]; then
        # the package's files where package.mk puts them (the menu's assets, themes, the binaries), then the menu's start
        # as start_banana.sh does it, banana-ui headless at the Thor's screens
        UI=$(dirname "$(echo $bins | cut -d' ' -f1)")
        for b in $bins; do cp "$b" "$M/usr/bin/"; done
        for d in rnds splash banana lang speaker-eq; do mkdir -p "$M/usr/share/banana/$d"; cp -a "$TOP/ui/assets/$d/." "$M/usr/share/banana/$d/"; done
        cp -a "$TOP/system/rootfs/usr/share/banana/themes" "$TOP/system/rootfs/usr/share/banana/companions" "$M/usr/share/banana/"
        for a in files bshare activity; do mkdir -p "$M/usr/share/banana/apps/$a"; done
        mkdir -p "$M/storage/roms/nds" "$M/storage/roms/gba" "$M/storage/roms/pc" "$M/storage/roms/windows" "$M/storage/roms/ports"
        : > "$M/storage/roms/nds/Peel Quest.nds"; : > "$M/storage/roms/gba/Monkey Ball.gba"; : > "$M/storage/roms/pc/KEEN.EXE"
        c 'D=/storage/.config/banana-data; ln -sfn /storage/roms $D/roms; rm -f /data/banana/settings.conf; echo "setup 2" > /data/banana/setup-done
           BANANA_MODEL="AYN Thor" /usr/libexec/banana/rocknix-device >/dev/null
           python3 /usr/share/banana/rocknix/es-systems-to-banana.py /usr/config/emulationstation/es_systems.cfg /run/banana/systems.conf
           mkdir -p /tmp/shots; BANANA_DATA=/data/banana BANANA_RUN=/run/banana BANANA_ROCKNIX=1 /usr/bin/banana-ui --headless /tmp/shots/real- \
             --size 1440x1080 --screens 1920x1080,1240x1080 --systems /run/banana/systems.conf --roms /data/roms --data /data/banana \
             --script "splash boot 1390 1; home 0; wait 2400; shot home; press a; wait 2400; shot lib; press b; press start; shot settings" 2> /tmp/shots/real.log' ||
            bad "banana-ui headless on the image exited $?"
        cp "$M"/tmp/shots/real-* "$OUT/" 2>/dev/null
        grep -qx 'theme = user:thor-oled' "$MD/settings.conf" 2>/dev/null || bad "the Thor OLED theme isn't the default on the image ($(grep '^theme' "$MD/settings.conf" 2>/dev/null))"
        grep -qi 'theme\|error' "$M/tmp/shots/real.log" && bad "banana-ui: $(grep -i -m2 'theme\|error' "$M/tmp/shots/real.log")"
        for v in home lib settings; do
            [ -s "$OUT/real-$v-top.png" ] && [ -s "$OUT/real-$v-bot.png" ] || bad "no $v screenshots"
        done
        if [ "$BANANA_REAL_SWAY" = 1 ]; then
            # ROCKNIX's sway headless with 111-sway-init's AYN Thor lines (HEADLESS-1 for DSI-2 the top; HEADLESS-2 for
            # DSI-1 the bottom, 1080x1240 until the exec turns it upright), then start_banana.sh at once, as autostart
            # starts UI_SERVICE's two units together
            cat > "$M/tmp/t/sway.sh" <<'XEOF'
#!/bin/bash
. /etc/profile
mkdir -p "$XDG_RUNTIME_DIR" /storage/.config/sway /storage/.config/profile.d; chmod 700 "$XDG_RUNTIME_DIR"
C=/storage/.config/sway/config; cp /usr/share/sway/config.kiosk $C
con=HEADLESS-1; second_con=HEADLESS-2
cat >> $C <<EOF
output HEADLESS-1 mode 1920x1080 position 0 0
output HEADLESS-2 mode 1080x1240 position 0 1080
for_window [title=".*(Secondary|\[w2\]|Sub|Bottom|Screen 2|GamePad).*"] move window to output ${second_con}
output ${second_con} power off
exec sh -c 'swaymsg output ${second_con} power on; sleep 1; swaymsg output ${second_con} transform 90; sleep 1; swaymsg output ${second_con} power off'
for_window [title=".*(Secondary|\[w2\]|Sub|Bottom|Screen 2|GamePad).*"] output ${second_con} power on
exec_always swaymsg '[app_id="emulationstation"]' focus output ${con}, output ${second_con} power off
exec_always swaymsg input "0:0:bottom_touchscreen" events disabled
EOF
printf 'WLR_CON=%s\nWLR_CON_TRANSFORM=0\nSDL_VIDEO_DISPLAY_PRIORITY=%s,%s\n' $con $con $second_con > /storage/.config/profile.d/095-sway
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_HEADLESS_OUTPUTS=2 sway -c $C > /tmp/t/sway.log 2>&1 &
SW=$!
for i in $(seq 1 100); do [ -S "$SWAYSOCK" ] && break; sleep 0.1; done
/usr/bin/start_banana.sh &
UI=$!
for i in $(seq 1 ${REAL_SWAY_WAIT:-180}); do grep -q 'the menu at' /data/banana/logs/banana-ui.log 2>/dev/null && break; sleep 1; done
sleep 8
swaymsg -t get_outputs | jq -r '.[] | "\(.name) \(.rect.width)x\(.rect.height) \(.transform) \(.power)"' > /tmp/t/outputs
swaymsg -t get_tree | jq -r '.. | select(.type? == "con" and .app_id? == "banana-ui") | "\(.name) \(.fullscreen_mode) \(.rect.x),\(.rect.y) \(.rect.width)x\(.rect.height)"' > /tmp/t/windows
grim -o HEADLESS-1 /tmp/shots/real-sway-top.png; grim -o HEADLESS-2 /tmp/shots/real-sway-bottom.png
kill $UI; sleep 2; kill -9 $UI 2>/dev/null; kill $SW; wait 2>/dev/null
XEOF
            chmod +x "$M/tmp/t/sway.sh"; rm -rf "$MD/logs"
            REAL_TIMEOUT=600 c '/tmp/t/sway.sh' >/dev/null 2>&1
            cp "$M"/tmp/shots/real-sway-* "$OUT/" 2>/dev/null
            cp "$M/tmp/t/sway.log" "$OUT/real-sway.log" 2>/dev/null; cp "$MD/logs/banana-ui.log" "$OUT/real-sway-banana-ui.log" 2>/dev/null
            grep -qx 'HEADLESS-2 1240x1080 90 true' "$M/tmp/t/outputs" || bad "the bottom output after the menu's start: $(grep HEADLESS-2 "$M/tmp/t/outputs")"
            grep -qx 'banana-ui top 1 0,0 1920x1080' "$M/tmp/t/windows" && grep -qx 'banana-ui bottom 1 0,1080 1240x1080' "$M/tmp/t/windows" ||
                bad "the menu's windows under ROCKNIX's sway: $(tr '\n' '|' < "$M/tmp/t/windows")"
            for f in top bottom; do [ -s "$OUT/real-sway-$f.png" ] || bad "no real-sway-$f.png"; done
        fi
    fi
fi
[ $fail = 0 ] && echo "real-image: all passed ($OUT)"
exit $fail
