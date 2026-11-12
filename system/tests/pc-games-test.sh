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
    BANANA_BOX64="$T/box64" BANANA_DATA="$T/data" BANANA_PC_NOART=1
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
# the PC games page's side: a game's settings set and read, the preset's and the overlay's variables at a run
sh "$P" set "$T/roms/My Game.pc" preset fast; sh "$P" set "$T/roms/My Game.pc" hud full; sh "$P" set "$T/roms/My Game.pc" d3d; sh "$P" set "$T/roms/My Game.pc" env
[ "$(sh "$P" get "$T/roms/My Game.pc" preset)" = fast ] && [ -z "$(sh "$P" get "$T/roms/My Game.pc" d3d)" ] || bad "set/get: $(tr '\n' '|' < "$T/roms/My Game.pc")"
grep -q '^exe = My Game/bin/game.exe$' "$T/roms/My Game.pc" && grep -q '^args = -windowed$' "$T/roms/My Game.pc" || bad "set took other lines: $(tr '\n' '|' < "$T/roms/My Game.pc")"
sh "$P" set "$T/roms/My Game.pc" nonsense x >/dev/null 2>&1 && bad "an unknown setting was taken"
printf '#!/bin/sh\necho "box64 [$DXVK_HUD] [$BOX64_DYNAREC_BIGBLOCK] [$BOX64_DYNAREC_STRONGMEM]" >> "%s/log"\n' "$T" > "$T/box64"; : > "$T/log"
sh "$P" run "$T/roms/My Game.pc" >/dev/null 2>&1
grep -q "\[fps,frametimes,gpuload,memory,version\] \[3\] \[0\]" "$T/log" || bad "the fast preset and the full overlay: $(cat "$T/log")"
sh "$P" set "$T/roms/My Game.pc" preset compatible; : > "$T/log"; sh "$P" run "$T/roms/My Game.pc" >/dev/null 2>&1
grep -q "\] \[0\] \[2\]" "$T/log" || bad "the compatible preset: $(cat "$T/log")"
# the list: a game on the card with its size, a Steam game not downloaded, the shelf's own entry, a cover
echo "steam = 620" > "$T/roms/Portal 2.pc"; mkdir -p "$T/roms/media/box"; : > "$T/roms/media/box/My Game.jpg"
L=$(sh "$P" list)
TAB=$(printf '\t')
echo "$L" | grep -q "^$T/roms/My Game.pc${TAB}My Game${TAB}installed${TAB}${TAB}[0-9]*${TAB}$T/roms/media/box/My Game.jpg$" || bad "list, a game on the card: $(echo "$L" | tr '\t' '~')"
echo "$L" | grep -q "^$T/roms/Portal 2.pc${TAB}Portal 2${TAB}cloud${TAB}620${TAB}0${TAB}$" || bad "list, a Steam game to download: $(echo "$L" | tr '\t' '~')"
echo "$L" | grep -q "${TAB}Steam${TAB}tool${TAB}" || bad "list, the shelf's own entry: $(echo "$L" | tr '\t' '~')"
# the programs to pick from: the guess first, the installer and the rest after
[ "$(sh "$P" exes "$T/roms/My Game.pc" | head -n 1)" = "My Game/bin/game.exe" ] && sh "$P" exes "$T/roms/My Game.pc" | grep -qx "My Game/setup.exe" || bad "exes: $(sh "$P" exes "$T/roms/My Game.pc" | tr '\n' '|')"
# uninstall: a copied game's folder, entry and cover go; a Steam game's entry stays as one to download, its settings with it
mkdir -p "$T/roms/Portal 2/bin"; : > "$T/roms/Portal 2/bin/portal2.exe"; printf 'steam = 620\nexe = Portal 2/bin/portal2.exe\npreset = fast\n' > "$T/roms/Portal 2.pc"
sh "$P" uninstall "$T/roms/Portal 2.pc" >/dev/null
[ ! -e "$T/roms/Portal 2" ] && [ "$(tr '\n' '|' < "$T/roms/Portal 2.pc")" = "steam = 620|preset = fast|" ] || bad "uninstall, a Steam game: $(ls "$T/roms") $(tr '\n' '|' < "$T/roms/Portal 2.pc")"
sh "$P" uninstall "$T/roms/My Game.pc" >/dev/null
[ ! -e "$T/roms/My Game" ] && [ ! -e "$T/roms/My Game.pc" ] && [ ! -e "$T/roms/media/box/My Game.jpg" ] || bad "uninstall, a copied game: $(ls "$T/roms" "$T/roms/media/box")"
sh "$P" uninstall "$T/roms/Steam.pc" >/dev/null 2>&1 && bad "the shelf's own entry was uninstalled"
[ -d "$T/roms/media" ] || bad "uninstall took more than the game"
[ $fail = 0 ] && echo "pc-games: all passed"
exit $fail
