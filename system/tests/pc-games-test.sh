#!/bin/sh
# usr/libexec/banana/pc-games against stand-ins (no Wine, no Box64, no ext4 image): setup fetches the three archives,
# checks their checksums and unpacks them, makes the prefix and puts DXVK's DLLs in it; a wrong checksum is refused;
# scan writes a .pc for a game's folder with its main .exe (not its installer); run starts that .exe through box64 and
# wine with the game's arguments and environment, and passes its exit status on.
HERE=$(cd "$(dirname "$0")/.." && pwd)
P=$HERE/rootfs/usr/libexec/banana/pc-games
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL pc-games: $*"; fail=1; }
command -v zstd >/dev/null || { echo "pc-games test skipped (no zstd)"; exit 0; }
# the stand-in archives, in the releases' own layouts
mkdir -p "$T/src/wine-11.0-amd64-wow64/bin" "$T/src/dxvk-3.1.1/x64" "$T/src/dxvk-3.1.1/x32" "$T/src/vkd3d-proton-3.0.1/x64" "$T/src/vkd3d-proton-3.0.1/x86"
printf '#!/bin/sh\n' > "$T/src/wine-11.0-amd64-wow64/bin/wine"; cp "$T/src/wine-11.0-amd64-wow64/bin/wine" "$T/src/wine-11.0-amd64-wow64/bin/wineserver"
chmod +x "$T/src/wine-11.0-amd64-wow64/bin/"*
for d in d3d8 d3d9 d3d10core d3d11 dxgi; do echo $d > "$T/src/dxvk-3.1.1/x64/$d.dll"; echo $d > "$T/src/dxvk-3.1.1/x32/$d.dll"; done
for d in d3d12 d3d12core; do echo $d > "$T/src/vkd3d-proton-3.0.1/x64/$d.dll"; echo $d > "$T/src/vkd3d-proton-3.0.1/x86/$d.dll"; done
(cd "$T/src" && tar -cf - wine-11.0-amd64-wow64 | xz > "$T/wine.tar.xz" && tar -czf "$T/dxvk.tar.gz" dxvk-3.1.1 && tar -cf - vkd3d-proton-3.0.1 | zstd -q > "$T/vkd3d.tar.zst")
sum() { sha256sum "$1" | cut -d' ' -f1; }
# the downloader: the archive its URL names
printf '#!/bin/sh\ncase $1 in *wine-*) cp "%s/wine.tar.xz" "$2" ;; *dxvk-*) cp "%s/dxvk.tar.gz" "$2" ;; *vkd3d-*) cp "%s/vkd3d.tar.zst" "$2" ;; *) exit 1 ;; esac\n' "$T" "$T" "$T" > "$T/fetch"
# box64: what it was asked to run, written down; "wine wineboot" makes the prefix; a game's exit status is 5
cat > "$T/box64" <<BOX
#!/bin/sh
echo "box64 \$* [\$DXVK_HUD] [\$WINEDLLOVERRIDES]" >> "$T/log"
case "\$*" in *wineboot*) mkdir -p "\$WINEPREFIX/drive_c/windows/system32" "\$WINEPREFIX/drive_c/windows/syswow64" "\$WINEPREFIX/dosdevices"; : > "\$WINEPREFIX/system.reg" ;; *game.exe*) exit 5 ;; esac
exit 0
BOX
chmod +x "$T/fetch" "$T/box64"
export BANANA_PC="$T/pc" BANANA_PC_MNT="$T/mnt" BANANA_PC_ROMS="$T/roms" BANANA_PC_NOMOUNT=1 BANANA_PC_FETCH="$T/fetch" \
    BANANA_BOX64="$T/box64" BANANA_DATA="$T/data"
[ "$(sh "$P" status | head -n 1)" = "not set up" ] || bad "status before setup"
# a wrong checksum: nothing unpacked
BANANA_PC_SUMS="0000 $(sum "$T/dxvk.tar.gz") $(sum "$T/vkd3d.tar.zst")" sh "$P" setup >/dev/null 2>&1 && bad "a wrong checksum was taken"
[ -e "$T/mnt/wine-11.0" ] && bad "Wine unpacked from a wrong download"
export BANANA_PC_SUMS="$(sum "$T/wine.tar.xz") $(sum "$T/dxvk.tar.gz") $(sum "$T/vkd3d.tar.zst")"
sh "$P" setup > "$T/setup.out" 2>&1 || bad "setup failed: $(tail -n 2 "$T/setup.out")"
[ -x "$T/mnt/wine-11.0/bin/wine" ] || bad "no Wine after setup"
[ -f "$T/mnt/prefix/drive_c/windows/system32/d3d11.dll" ] && [ -f "$T/mnt/prefix/drive_c/windows/syswow64/d3d12.dll" ] || bad "DXVK's or VKD3D's DLLs not in the prefix"
grep -q "reg add HKCU.Software.Wine.Drivers /v Graphics /d wayland,x11" "$T/log" || bad "the Wayland driver not chosen"
[ "$(sh "$P" status | head -n 1)" = ready ] || bad "status after setup: $(sh "$P" status | head -n 1)"
# a game's folder: its main program found, not the installer or the redistributable
mkdir -p "$T/roms/My Game/bin" "$T/roms/My Game/_CommonRedist"
head -c 5000 /dev/zero > "$T/roms/My Game/bin/game.exe"; head -c 90000 /dev/zero > "$T/roms/My Game/setup.exe"
head -c 80000 /dev/zero > "$T/roms/My Game/_CommonRedist/vcredist_x64.exe"; head -c 100 /dev/zero > "$T/roms/My Game/bin/tool.exe"
sh "$P" scan
[ "$(cat "$T/roms/My Game.pc" 2>/dev/null)" = "exe = My Game/bin/game.exe" ] || bad "scan: $(cat "$T/roms/My Game.pc" 2>/dev/null)"
printf 'args = -windowed\nenv = DXVK_HUD=fps\n' >> "$T/roms/My Game.pc"
: > "$T/log"
sh "$P" run "$T/roms/My Game.pc" >/dev/null 2>&1; rc=$?
[ $rc = 5 ] || bad "run: the game's exit status was $rc"
grep -q "wine-11.0/bin/wine $T/roms/My Game/bin/game.exe -windowed \[fps\]" "$T/log" || bad "run: $(cat "$T/log")"
printf 'd3d = wined3d\n' >> "$T/roms/My Game.pc"; : > "$T/log"
sh "$P" run "$T/roms/My Game.pc" >/dev/null 2>&1
grep -q "d3d11,dxgi=b" "$T/log" || bad "wined3d not asked for: $(cat "$T/log")"
[ $fail = 0 ] && echo "pc-games: all passed"
exit $fail
