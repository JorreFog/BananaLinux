#!/bin/sh
# The ROCKNIX frontend layer's checks, no ROCKNIX needed: es-systems-to-banana.py on a sample es_systems.cfg in the
# shape ROCKNIX writes it (each system's default emulator and core into runemu.sh, the extensions, the folder, a
# system with its own command, the menu's own pages after), and the start script and installer parse.
HERE=$(cd "$(dirname "$0")/.." && pwd)
C=$HERE/packages/ui/banana-frontend/sources/es-systems-to-banana.py
fail=0
out=$(python3 -I "$C" "$HERE/tests/es_systems.cfg") || { echo "FAIL rocknix: the converter failed"; exit 1; }
want() { printf '%s\n' "$out" | grep -qxF -- "$1" || { echo "FAIL rocknix: no line '$1'"; fail=1; }; }
want '[nds]'
want 'perf = steady'
want 'run = /usr/libexec/banana/run-rocknix nds %ROM% --core=melonds-sa --emulator=melonds --controllers=""'
want 'ext = nds zip 7z'
want 'folder = /storage/roms/nds'
want 'run = /usr/libexec/banana/run-rocknix gba %ROM% --core=mgba --emulator=retroarch --controllers=""'
want 'run = %ROM%'
want 'run = banana:files'
bash -n "$HERE/packages/ui/banana-frontend/sources/start_banana.sh" || { echo "FAIL rocknix: start_banana.sh"; fail=1; }
sh -n "$HERE/../tools/rocknix-banana.sh" || { echo "FAIL rocknix: rocknix-banana.sh"; fail=1; }
sh "$HERE/tests/services.sh" || fail=1
sh "$HERE/tests/backlight.sh" || fail=1
sh "$HERE/tests/perf.sh" || fail=1
sh "$HERE/tests/handheld.sh" || fail=1
sh "$HERE/tests/options.sh" || fail=1
sh "$HERE/tests/register.sh" || fail=1
for f in "$HERE"/packages/emulators/standalone/*/scripts/*.sh; do bash -n "$f" || { echo "FAIL rocknix: $f"; fail=1; }; done
sh "$HERE/tests/bananads-sway.sh" || fail=1
bash -n "$HERE/packages/ui/banana-frontend/sources/099-banana-frontend" || { echo "FAIL rocknix: 099-banana-frontend"; fail=1; }
[ $fail = 0 ] && echo "rocknix: all passed"
exit $fail
