#!/bin/sh
# banana-ui's readers of what's on the card, fuzzed: libFuzzer with AddressSanitizer and UBSan (needs clang).
#
#   ui/fuzz/run.sh                    every target for FUZZ_SECONDS (default 60) each
#   ui/fuzz/run.sh json zip           only these
#   ui/fuzz/run.sh zip <input>        one input again (a crash's, to debug it)
#
# Targets: json (themes), expr (layout themes' expressions and colours), zip (.zip ROMs), romicon (a DS ROM's banner,
# from a .nds and a .zip), cardfiles (systems.conf, settings.conf, favourites, play stats, bananad.conf), layout (a
# layout theme: the menu drawn from it, used, at both panel sizes), ndsrom (the DS Pokémon companion's ROM reader: a
# made-up game's cartridge image the input changes, its sprites and its cache on the card; or the input as the image),
# gamecomp (a game companion's definition, index.tsv, the game's ROM and its memory: matched, every page read), gbarom
# (a GBA Pokémon ROM as the GBA companion reads it: its profile, its tables, the DS pages' data). The seeds are made
# from the repo's own files; each target's corpus grows in $OUT/corpus-<target> and is kept between runs. A crash
# stops that target, leaves its input at $OUT/crash-<target>-* and fails the run. Before each target is fuzzed,
# ui/fuzz/regressions/<target>/ (inputs that crashed or hung it once, now fixed) are run once each.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$HERE/.." && pwd)
OUT=${OUT:-$HERE/fuzz/out}
SECONDS_EACH=${FUZZ_SECONDS:-60}
CXX=${FUZZ_CXX:-clang++}
TARGETS="json expr zip romicon cardfiles layout ndsrom gamecomp gbarom"
# the targets' assets and systems list (cardfiles, layout), the companions' system maps (gamecomp)
export BANANA_FUZZ_ASSETS="$HERE/assets/rnds" BANANA_FUZZ_SYSTEMS="$ROOT/system/rootfs/etc/banana/systems.conf"
export BANANA_FUZZ_SYSTEMS_DIR="$ROOT/system/rootfs/usr/share/banana/companions/systems"
mkdir -p "$OUT/obj"

# ---- build: the menu's sources but main.cpp, once, then each target against them ----
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
# (-Isrc as the Makefile's UI_FLAGS: the sources include "nds/logic.h" and the like from src/; without it nothing built)
# (-I rcheevos and its rhash objects, as the Makefile's RC_*: achievements.cpp includes rc_hash.h; without them the
# build stopped there)
RC=$HERE/third_party/rcheevos
FLAGS="-std=c++17 -O1 -g $SAN -I$HERE/src -I$RC/include $(pkg-config --cflags freetype2 libdrm zlib)"
LIBS="$(pkg-config --libs freetype2 libdrm zlib) -lpthread -ldl"
OBJS=""
for s in "$RC"/src/rhash/*.c "$RC/src/rc_compat.c"; do	# (upstream C: built with clang, its warnings off)
    o=$OUT/obj/rc_$(basename "$s").o
    [ "$o" -nt "$s" ] || ${FUZZ_CC:-clang} -O1 -g $SAN -fsanitize=fuzzer-no-link -I"$RC/include" -w -c "$s" -o "$o"
    OBJS="$OBJS $o"
done
for s in $(sed -n 's/^SRC +*= //p' "$HERE/Makefile") fuzz/stb.cpp; do	# (stb's code is in main.cpp)
    [ "$s" = src/main.cpp ] && continue
    o=$OUT/obj/$(echo "$s" | tr / _).o
    [ "$o" -nt "$HERE/$s" ] && [ "$o" -nt "$HERE/Makefile" ] || $CXX $FLAGS -fsanitize=fuzzer-no-link -c "$HERE/$s" -o "$o"
    OBJS="$OBJS $o"
done
build() {
    $CXX $FLAGS -fsanitize=fuzzer "$HERE/fuzz/$1.cpp" $OBJS $LIBS -o "$OUT/fuzz-$1"
}

# ---- seeds ----
seeds() {
    d=$OUT/seeds-$1
    rm -rf "$d"; mkdir -p "$d"
    case $1 in
    json|layout) cp "$ROOT"/docs/themes/*.json "$ROOT"/system/rootfs/usr/share/banana/themes/*.json "$ROOT/docs/theme.schema.json" "$d"/ 2>/dev/null || true ;;
    expr)
        # every "=..." and "{...}" string in the example themes, one file each
        python3 -I - "$d" "$ROOT"/docs/themes/*.json "$ROOT"/system/rootfs/usr/share/banana/themes/*.json <<'PY'
import json, re, sys
out, n = sys.argv[1], 0
for p in sys.argv[2:]:
    try: text = open(p, encoding='utf-8').read()
    except OSError: continue
    for s in set(re.findall(r'"((?:[^"\\]|\\.)*)"', text)):
        if s.startswith('=') or '{' in s or s.startswith('#') or s.startswith('rgb'):
            open(f'{out}/s{n}', 'w').write(s); n += 1
PY
        ;;
    zip|romicon)
        # small DS ROMs (a banner at 0x200; versions 1 and 0x103) and zips of them, stored and deflated
        python3 -I - "$d" <<'PY'
import io, struct, sys, zipfile
out = sys.argv[1]
def rom(version):
    r = bytearray(0x200 + 0x240)
    r[0x68:0x6c] = struct.pack('<I', 0x200)
    r[0x200:0x202] = struct.pack('<H', version)
    for i in range(0x20, 0x220): r[0x200 + i] = (i * 7) & 0xff
    for i in range(16): r[0x200 + 0x220 + 2 * i:0x200 + 0x222 + 2 * i] = struct.pack('<H', i * 0x0842)
    return bytes(r)
for v in (1, 0x103):
    open(f'{out}/rom-{v}.nds', 'wb').write(rom(v))
    for m, name in ((zipfile.ZIP_STORED, 'stored'), (zipfile.ZIP_DEFLATED, 'deflated')):
        b = io.BytesIO()
        with zipfile.ZipFile(b, 'w', m) as z:
            z.writestr('readme.txt', 'hi')
            z.writestr('dir/', '')
            z.writestr('game.nds', rom(v))
        open(f'{out}/rom-{v}-{name}.zip', 'wb').write(b.getvalue())
PY
        ;;
    cardfiles)
        # the files joined as FuzzedDataProvider splits them (a backslash and another character ends each one)
        sep='\x'
        for f in "$ROOT/system/rootfs/etc/banana/systems.conf" "$ROOT/system/rootfs/etc/banana/settings.conf"; do
            [ -f "$f" ] || f=/dev/null
            sed 's/\\/\\\\/g' "$f"; printf '%s' "$sep"
        done > "$d/card"
        printf 'nds/Peel Quest.nds\n%sfavorites\n%snds/Peel Quest.nds\t3\t200\t1700000000\n%sbattery_shutdown_percent = 3\n' "$sep" "$sep" "$sep" >> "$d/card"
        ;;
    ndsrom)
        # each game the target makes (the first byte, 0-9; 10: the rest is the image), read once and through its cache
        for g in 0 1 2 3 4 5 6 7 8 9; do printf "\\$(printf %o "$g")\\000" > "$d/game-$g"; printf "\\$(printf %o "$g")\\001" > "$d/game-$g-cache"; done
        ;;
    gbarom)
        # a header for each of the codes the target writes (the first byte), and a pokeemerald-expansion build's
        python3 -I - "$d" <<'PY'
import sys
for i in range(10):
    open('%s/code-%d.gba' % (sys.argv[1], i), 'wb').write(bytes([i]) + bytes(0x3ff))
open('%s/expansion.gba' % sys.argv[1], 'wb').write(bytes(0x10) + bytes(0xf0) + b'RHHEXP' + bytes(0x2fa))
PY
        ;;
    gamecomp)
        # every definition of the image's, with a memory, a ROM whose header is the definition's game, its index line
        python3 -I - "$d" "$ROOT"/system/rootfs/usr/share/banana/companions/*.json "$ROOT"/system/rootfs/usr/share/banana/companions/*/*.json <<'PY'
import json, os, sys
out, sep = sys.argv[1], b'\xff\xfe\xfd\xfc'
mem = bytes((i * 37 + (i >> 8)) & 0xff for i in range(4096)).replace(b'\xee', b'\xef')
for p in sys.argv[2:]:
    try:
        text = open(p, 'rb').read()
        m = json.loads(text).get('match', {}) or {}
    except (OSError, ValueError, AttributeError):
        continue
    rom = bytearray(0x400)
    rom[0xa0:0xac] = str(m.get('title12', ''))[:12].encode('latin-1', 'replace').ljust(12, b'\0')
    code = m.get('code', m.get('codes', ['']))
    rom[0xac:0xb0] = str(code[0] if isinstance(code, list) and code else code)[:4].encode('latin-1', 'replace').ljust(4, b'\0')
    index = 'game.json\t%s\t%s\t\t0\t\n' % (rom[0xac:0xb0].rstrip(b'\0').decode('latin-1'), rom[0xa0:0xac].rstrip(b'\0').decode('latin-1'))
    name = os.path.basename(os.path.dirname(p)) + '-' + os.path.basename(p)
    open(os.path.join(out, name), 'wb').write(text + sep + mem + sep + bytes(rom) + sep + index.encode())
PY
        ;;
    esac
}

# the themes' and the companions' own words (keys, element types, hook names) as a libFuzzer dictionary: mutations that
# splice them in keep a theme a theme, where random bytes mostly break the JSON before the engine sees it
dict() {
    python3 -I - "$OUT/dict" "$ROOT"/docs/themes/*.json "$ROOT"/system/rootfs/usr/share/banana/themes/*.json \
        "$ROOT"/system/rootfs/usr/share/banana/companions/*.json <<'PY'
import re, sys
words = set()
for p in sys.argv[2:]:
    try: text = open(p, encoding='utf-8').read()
    except OSError: continue
    words.update(w for w in re.findall(r'"([A-Za-z_][A-Za-z0-9_.]{0,23})"', text))
    words.update(re.findall(r'\b([a-z][A-Za-z0-9]*)\(', text))     # expression functions
with open(sys.argv[1], 'w') as f:
    for w in sorted(words): f.write('"%s"\n' % w)
    for t in ('{', '}', '[', ']', ':', ',', '"=', '{sel}', '?', '||', '&&', '#ff00ff80', '1e9', '-1', 'NaN', 'null', 'true'):
        f.write('"%s"\n' % t.replace('"', '\\"'))
PY
}

if [ $# = 2 ] && [ -f "$2" ]; then
    build "$1"
    exec "$OUT/fuzz-$1" "$2"
fi
[ $# -gt 0 ] && TARGETS="$*"
fail=0
dict
for t in $TARGETS; do
    build "$t"
    if [ -d "$HERE/fuzz/regressions/$t" ]; then
        rlog=$OUT/$t-regressions.log
        "$OUT/fuzz-$t" -timeout=10 -rss_limit_mb=2048 "$HERE"/fuzz/regressions/$t/* > "$rlog" 2>&1 ||
            { echo "FAIL $t: regression $(grep 'Running:' "$rlog" | tail -1 | sed 's|.*/regressions/||'): $(grep -m1 -E 'ERROR|runtime error|deadly signal|timeout' "$rlog")"; fail=1; continue; }
        echo "== $t: $(ls "$HERE/fuzz/regressions/$t" | wc -l | tr -d ' ') regression inputs pass"
    fi
    seeds "$t"
    mkdir -p "$OUT/corpus-$t"
    echo "== $t ($SECONDS_EACH s)"
    case $t in json|expr|layout|gamecomp) D=-dict="$OUT/dict" ;; *) D= ;; esac
    # (ndsrom's seeds are two bytes: without a length to grow into, libFuzzer kept its inputs at a few bytes for minutes)
    [ "$t" = ndsrom ] && D="-max_len=16384 -len_control=0"
    [ "$t" = gbarom ] && D="-max_len=65536"
    "$OUT/fuzz-$t" $D -max_total_time="$SECONDS_EACH" -timeout=${FUZZ_TIMEOUT:-10} -rss_limit_mb=2048 \
        -artifact_prefix="$OUT/crash-$t-" -print_final_stats=1 "$OUT/corpus-$t" "$OUT/seeds-$t" > "$OUT/$t.log" 2>&1 ||
        { echo "FAIL $t: $(grep -m1 -E 'ERROR|runtime error|deadly signal|timeout' "$OUT/$t.log")"; ls "$OUT"/crash-$t-* 2>/dev/null
          sed -n '/ERROR\|runtime error\|ALARM\|deadly signal/,$p' "$OUT/$t.log" | head -40; fail=1; continue; }
    grep -E '^stat::number_of_executed_units|^#[0-9]+.*DONE' "$OUT/$t.log" | tail -1
done
[ $fail = 0 ] && echo "fuzz: no crashes ($OUT)"
exit $fail
