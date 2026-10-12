#!/bin/sh
# Imports ROCKNIXDS Pixel's engine (RndsRaster, RndsUI) and its theme assets from a ROCKNIXDS checkout, so Banana's
# frontend draws exactly what ROCKNIXDS Pixel draws. The engine lives in ROCKNIXDS as "new file" sections of
# dii-ess-aye/es-rgds-rnds.patch; only the ES-independent files are taken (RndsEs and the ES views stay behind).
#
#   tools/import-rnds.sh <ROCKNIXDS checkout>
#
# Writes ui/rnds/*.{h,cpp} and ui/assets/rnds/ (fonts, icons, sounds, pictures, systems.cfg), puts
# ui/assets/overrides/ (Banana's logo, tools/make-logo.py) over the result, and records the ROCKNIXDS commit in
# ui/rnds/UPSTREAM. Banana's boot splash (ui/assets/splash) is its own: tools/make-splash.mjs.
set -e
SRC=${1:?usage: import-rnds.sh <ROCKNIXDS checkout>}
HERE=$(cd "$(dirname "$0")/.." && pwd)
PATCH=$SRC/dii-ess-aye/es-rgds-rnds.patch
DARK=$SRC/dii-ess-aye/themes/rocknixds-pixel-dark
[ -f "$PATCH" ] || { echo "no $PATCH"; exit 1; }

mkdir -p "$HERE/ui/rnds"
for f in RndsRaster.cpp RndsRaster.h RndsUI.cpp RndsUI.h; do
    awk -v f="+++ b/es-app/src/rnds/$f" '
        $0 == f { on = 1; getline; next }       # the hunk header after +++
        on && /^diff --git/ { on = 0 }
        on && /^\+/ { print substr($0, 2) }' "$PATCH" > "$HERE/ui/rnds/$f"
    [ -s "$HERE/ui/rnds/$f" ] || { echo "$f not found in $PATCH"; exit 1; }
done

rm -rf "$HERE/ui/assets/rnds"
cp -r "$DARK/rnds" "$HERE/ui/assets/rnds"

# Banana's own pictures over ROCKNIXDS's (the logo; see ui/assets/overrides)
cp "$HERE"/ui/assets/overrides/*.png "$HERE/ui/assets/rnds/"
# and system icons Banana redrew (icons/: ports.png, its PC three pixels lower, centred as the others)
cp "$HERE"/ui/assets/overrides/icons/*.png "$HERE/ui/assets/rnds/icons/"

commit=$(git -C "$SRC" rev-parse HEAD 2>/dev/null || echo unknown)
cat > "$HERE/ui/rnds/UPSTREAM" <<EOF
ROCKNIXDS Pixel engine, imported by tools/import-rnds.sh. Don't edit these files here: change them in ROCKNIXDS
(dii-ess-aye/es-rgds-rnds.patch) and import again, so both frontends stay the same.
ROCKNIXDS commit: $commit
EOF
echo "imported ROCKNIXDS Pixel from $commit"

# Banana's palette over the theme's (the engine, banana-ui's copies, the splash's stills: run tools/make-splash.mjs after)
sh "$HERE/tools/banana-palette.sh"
