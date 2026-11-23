#!/bin/sh
# tests/paths-test's setting: the image's cores installed (as files: only that one is there is looked at), one on the
# card, a zip of two members and a folder; then the test, on a throwaway card in <dir>, with the image's systems.conf.
#   paths-test.sh <dir>
HERE=$(cd "$(dirname "$0")/.." && pwd)
. "$HERE/tests/sync-card.sh"
D=$1
mkdir -p "$D/lib" "$D/card" || exit 1
core_table | while IFS='	' read -r core rest; do fakecore "$D/lib/${core}_libretro.so" || exit 1; done || exit 1
# (a core RetroArch's Core Downloader put on the card)
fakecore "$D/card/retroarch/cores/nestopia_libretro.so" || exit 1
python3 -c '
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("sub/", b"")
    z.writestr("Zip Game (USA).gba", b"gba rom zipped " * 300)
    z.writestr("sub/Zip Game (USA) [b].gba", b"second member" * 10)' "$D/game.zip" || exit 1
BANANA_LIBRETRO=$D/lib BANANA_SYSTEMS=$HERE/../system/rootfs/etc/banana/systems.conf exec "$HERE/tests/paths-test" "$D"
