#!/bin/sh
# Downloads the emulator APKs that android/system/emulators/Android.bp puts in the image, each from its project's
# own release page, at a pinned version, and checks each against the SHA-256 recorded here (every one was
# downloaded and hashed once to write this list, on 2026-11-14). Run it once before building the ROM:
#   sh vendor/banana/android/system/emulators/fetch.sh            all of them, into apks/ beside this script
#   sh vendor/banana/android/system/emulators/fetch.sh retroarch  one (or several) by name
# A file already there with the right hash is kept; a wrong hash is deleted and the script fails.
# Only open-source apps whose licences allow redistribution are listed (docs/THOR-LINEAGE-PARITY.md has the rest).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${BANANA_APKS:-$HERE/apks}
mkdir -p "$OUT"

# name | file | url | sha256 | package, version, licence
LIST='
retroarch|RetroArch_aarch64.apk|https://buildbot.libretro.com/stable/1.22.2/android/RetroArch_aarch64.apk|7bd5d208dfe93cc8e2ea6c04608948ce1a045980f160a58ca2d0993aa20ad213|com.retroarch.aarch64 1.22.2, GPL-3.0
dolphin|dolphin-2609b.apk|https://dl.dolphin-emu.org/releases/2609b/dolphin-2609b.apk|9171e6d39a8d691e5a1fe1e518c56f909768fa837ee21437a06241f87f4b5130|org.dolphinemu.dolphinemu 2609b, GPL-2.0-or-later
ppsspp|ppsspp-1.20.4.apk|https://www.ppsspp.org/files/1_20_4/ppsspp.apk|dd702a31270ecd68db37afee311d6efe08b1d4da9601d2750ce1a73bbd316c3f|org.ppsspp.ppsspp 1.20.4, GPL-2.0-or-later
scummvm|scummvm-2026.3.0-android-arm64-v8a.apk|https://downloads.scummvm.org/frs/scummvm/2026.3.0/scummvm-2026.3.0-android-arm64-v8a.apk|26e25a59496eb94a4dc76f282bb3234d6d36f7f393c3e699e0a62e1a5c3e412b|org.scummvm.scummvm 2026.3.0, GPL-3.0-or-later (the same hash as the .sha256 published beside it)
flycast|flycast-2.7.apk|https://github.com/flyinghead/flycast/releases/download/v2.7/flycast-2.7.apk|1ac54d77b8283f6d58d077396d0916ec12c15b7e4ada9d957836a64590756c00|com.flycast.emulator 2.7, GPL-2.0
azahar|azahar-android-vanilla-2126.2.apk|https://github.com/azahar-emu/azahar/releases/download/2126.2/azahar-android-vanilla-2126.2.apk|b511d1d38012c5ab0dfdb4479335059bb89ef17acfbf0ba45e79107ed4185ba3|org.azahar_emu.azahar 2126.2, GPL-2.0-or-later
melonds|melonDS-android-1.10.0.apk|https://github.com/rafaelvcaetano/melonDS-android/releases/download/beta-1.10.0/app-gitHub-prod-release.apk|fea43a3727b4de0a384eeb00e77a6cb87ae9aaab9ba28a913e6d73ea997d1d8a|me.magnum.melonds 1.10.0 (beta), GPL-3.0
vita3k|vita3k-4338-android.apk|https://github.com/Vita3K/Vita3K-builds/releases/download/4338/vita3k-4338-2c85515d-android.apk|839c5cc78eddd55cbf3efe0ca3528ab0e4e3e47664f9eebc825287ae4389ae1e|org.vita3k.emulator build 4338 (commit 2c85515d), GPL-2.0
armsx2|ARMSX2-2.9.3-a15-armv8.2-sdk35.apk|https://github.com/ARMSX2/ARMSX2/releases/download/2.9.3/ARMSX2-2.9.3-a15-armv8.2-sdk35.apk|0ac4d6d1e92dade794b632d488e5f468ba1e18a09df751d05bd3e836c31182f7|com.armsx2 2.9.3, GPL-3.0-or-later (the Android 15+ build)
gamenative|gamenative-v1.4.0.apk|https://github.com/utkarshdalal/GameNative/releases/download/v1.4.0/gamenative-v1.4.0.apk|51e7edc667a352c664f7da274d15b973cd054ec04e0b828e2039fb40c334224d|app.gamenative 1.4.0, GPL-3.0
winlator|Winlator_11.2.apk|https://github.com/brunodev85/winlator/releases/download/v11.2.0/Winlator_11.2.apk|ea5c81bf0e5b90a6b9ccb8007121ab6738c8646a5a60b341ce0ac2b33f1d100a|com.winlator 11.2, LGPL-2.1
'

sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

want=" $* "
echo "$LIST" | while IFS='|' read -r name file url sum what; do
    [ -n "$name" ] || continue
    [ $# -eq 0 ] || case $want in *" $name "*) ;; *) continue ;; esac
    f=$OUT/$file
    if [ -f "$f" ] && [ "$(sha "$f")" = "$sum" ]; then echo "ok    $name ($what)"; continue; fi
    echo "fetch $name: $url"
    curl -fL --retry 3 -o "$f.part" "$url"
    got=$(sha "$f.part")
    if [ "$got" != "$sum" ]; then
        rm -f "$f.part"
        echo "WRONG $name: sha256 $got, expected $sum" >&2
        exit 1
    fi
    mv -f "$f.part" "$f"
    echo "ok    $name ($what)"
done
