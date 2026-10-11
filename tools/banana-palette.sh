#!/bin/sh
# Banana's palette over ROCKNIXDS Pixel's: the theme's blue-grey neutrals turned warm, and its blue accents (the
# selected cartridge's plate, the shelf's scroll thumb, a held icon, the default system tint) turned banana yellow.
# A colour map, applied to the engine (ui/rnds/RndsUI.cpp, imported unchanged by tools/import-rnds.sh, which runs
# this last), to banana-ui's own copies of the palette (settings.cpp, splash.cpp) and to the splash's still layers
# (tools/make-splash.mjs: run it again afterwards). Every value is a 6-digit hex; the same value means the same
# thing wherever ROCKNIXDS uses it, so one map covers all the files.
#   tools/banana-palette.sh            rewrite the files in place
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
FILES="$HERE/ui/rnds/RndsUI.cpp $HERE/ui/src/settings.cpp $HERE/ui/src/splash.cpp $HERE/tools/make-splash.mjs"
MAP='
# dark theme: blue-grey -> warm charcoal
e7eef6 f1ede2
93a0b0 a39d8e
7d8b9c 8a8374
d5dee8 e8e2d3
e7f4ff fff6dc
070a0e 0b0a0d
121820 17161c
243040 2e2b33
10161e 15141a
2e3a48 3a3540
0e141c 121117
12181f 17161c
1b2330 262219
3a4654 45403d
2c3848 38333b
141b24 191820
0c1218 0f0e13
0c1016 100f14
2a3544 353039
05080b 070609
# light theme: cool paper -> warm paper
1d2733 2a2620
66758a 7a7262
8d9bab a69d8c
2b3644 3a342c
f2faff fffaf0
14233a 3a2e14
e6ebf0 f3efe6
b9c4d0 d4ccbd
fbfcfd fdfcf9
fafbfc fdfbf6
edf1f6 f6f2ea
c5cfda d8d0c0
eef2f7 f6f2ea
dfe5ec ebe5d9
d6dde6 e3dccd
c3ccd6 d6cfc0
f4f7fa faf7f0
cfd8e2 ddd5c6
d9e0e8 e6dfd0
# accents: blue -> banana yellow (the plate, the thumb, a held icon, the default system tint, the light slot ring)
1a6ea3 f0b429
7ec8ee ffe68a
0c3d5c 8a5a00
3d8ec8 f0b429
b7e6ff ffe68a
3d8ec4 d9a21b
# the splash cartridge label: navy and violet -> golden
07091f 1f1405
26307a 7a5a10
4a2c86 b8861e
'
echo "$MAP" | grep -v '^#' | grep . | while read -r from to; do
    for f in $FILES; do
        sed -i "s/0x$from/0x$to/g; s/0xff$from/0xff$to/g; s/0x$(echo $from | tr a-f A-F)/0x$to/g; s/#$from/#$to/g" "$f"
    done
done
echo "banana palette applied"
