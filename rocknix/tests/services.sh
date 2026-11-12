#!/bin/sh
# BananaOS's system scripts in services mode (hw.conf's HW_SERVICES=systemd: ROCKNIX, where the menu is the frontend),
# against stand-ins for systemctl, bluetoothctl, nmcli and rocknix-update on PATH: what each asks of the system's
# services and settings for the menu's own commands. No ROCKNIX needed.
HERE=$(cd "$(dirname "$0")/.." && pwd)
L=$HERE/../system/rootfs/usr/libexec/banana
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/bin" "$T/run" "$T/data" "$T/home"
cp "$HERE/tests/fake/nmcli" "$T/bin/nmcli"
printf '#!/bin/sh\necho "systemctl $*" >> "$T/log"\ncase "$1" in is-active) [ -f "$T/bt-on" ] ;; start) case "$*" in *bluetooth*) touch "$T/bt-on" ;; esac ;; stop) case "$*" in *bluetooth*) rm -f "$T/bt-on" ;; esac ;; esac\n' > "$T/bin/systemctl"
printf '#!/bin/sh\necho "bluetoothctl $*" >> "$T/log"\ncase "$*" in *show*) echo "Powered: yes" ;; esac\n' > "$T/bin/bluetoothctl"
printf '#!/bin/sh\ncase $1 in check) echo 20261201 ;; update) echo "rocknix-update update" >> "$T/log" ;; esac\n' > "$T/bin/rocknix-update"
printf '#!/bin/sh\nexit 0\n' > "$T/bin/timeout"
sed -i "s|\\\$T|$T|g" "$T/bin/systemctl" "$T/bin/bluetoothctl" "$T/bin/rocknix-update"
chmod +x "$T/bin/"*
printf 'HW_SERVICES=systemd\nHW_SOUND=pipewire\n' > "$T/hw.conf"
printf 'network={\n\tssid="Banana Home"\n\tpsk="password1"\n}\n' > "$T/data/wifi.conf"
export PATH="$T/bin:$PATH" BANANA_HW_CONF="$T/hw.conf" BANANA_LIBEXEC="$L" BANANA_DATA="$T/data" BANANA_RUN="$T/run" \
    ROCKNIX_SETTINGS="$T/system.cfg" BANANA_BOARD=thor FAKE_NM_LOG="$T/log" FAKE_NM_UP="$T/nm-up" HOME="$T/home"
fail=0
ok() { grep -q -- "$2" "$T/$1" || { echo "FAIL rocknix services: no '$2' in $1"; fail=1; }; }

# Wi-Fi through NetworkManager, ROCKNIX's settings kept the same
sh "$L/wifi" scan > "$T/scan"; ok scan "Banana Home"; ok scan "$(printf '\t2437\t-62\t')"
sh "$L/wifi" up; ok log "device wifi connect Banana Home password password1"; ok system.cfg "wifi.ssid=Banana Home"
sh "$L/wifi" status > "$T/status"; ok status "wpa_state=COMPLETED"
# Bluetooth: ROCKNIX's units and setting
sh "$L/bluetooth" up >/dev/null 2>&1; ok log "systemctl start bluetooth bluetooth-agent"
[ "$(sh "$L/bluetooth" status)" = on ] || { echo "FAIL rocknix services: bluetooth status isn't on"; fail=1; }
sh "$L/bluetooth" down; ok log "systemctl stop bluetooth-agent bluetooth"
# SSH and sharing: its sshd and Samba, the card's keys as root's
sh "$L/ssh" up; ok log "systemctl start sshd"; [ -f "$T/home/.ssh/authorized_keys" ] || { echo "FAIL rocknix services: no authorized_keys"; fail=1; }
sh "$L/share" up; ok log "systemctl restart nmbd smbd"
# updates: ROCKNIX's updater behind the menu's check / download / apply
sh "$L/update" check > "$T/check"; ok check "state=newer"; ok check "latest=20261201"
sh "$L/update" download; ok log "rocknix-update update"; ok run/update-status "ready rocknix-20261201"
# the second card: none to manage
sh "$L/sd2" scan; [ ! -e "$T/run/sd2/slot" ] || { echo "FAIL rocknix services: sd2 offers a slot"; fail=1; }
# a game through run-rocknix: runemu.sh with the menu's mode as ROCKNIX's per-game settings, the bottom screen dark for
# a one-screen system and lit again, left alone for the DS
printf '#!/bin/sh\necho "runemu $*" >> "%s/log"\n' "$T" > "$T/bin/runemu.sh"; chmod +x "$T/bin/runemu.sh"
printf '#!/bin/sh\necho "backlight $*" >> "%s/log"\n' "$T" > "$T/backlight"; chmod +x "$T/backlight"
echo "performance performance" > "$T/run/perf"
env BANANA_LIBEXEC="$T" BANANA_RUNEMU="$T/bin/runemu.sh" sh "$L/run-rocknix" psp "/roms/psp/Some Game.iso" --core=ppsspp --emulator=ppsspp-sa
ok log 'runemu /roms/psp/Some Game.iso -Ppsp --core=ppsspp --emulator=ppsspp-sa'
ok system.cfg 'psp\["Some Game.iso"\].cpugovernor=performance'
ok log "backlight off 1"; ok log "backlight on 1"
: > "$T/log"
env BANANA_LIBEXEC="$T" BANANA_RUNEMU="$T/bin/runemu.sh" sh "$L/run-rocknix" nds "/roms/nds/a.nds"
grep -q "backlight" "$T/log" && { echo "FAIL rocknix services: the DS's bottom screen was darkened"; fail=1; }
[ $fail = 0 ] && echo "rocknix services: all passed"
exit $fail
