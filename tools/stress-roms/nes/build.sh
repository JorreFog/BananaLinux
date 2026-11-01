#!/bin/sh
# build.sh [LEVEL] -> out/stress-nes[-L<n>].nes (+ .phases.tsv); needs cc65 (~/.local/opt/cc65/bin) and python3
set -e
cd "$(dirname "$0")"
export PATH="$HOME/.local/opt/cc65/bin:$PATH"
L=${1:-5}
python3 gen.py
mkdir -p out
ca65 -g -DLEVEL=$L -o out/stress.o stress.s
ld65 -C nes.cfg -o out/stress.nes out/stress.o -m out/stress.map -Ln out/stress.lbl 2>&1 | grep -v "Warning: Segment" || true
if [ "$L" = 5 ]; then N=stress-nes; else N=stress-nes-L$L; fi
mv out/stress.nes out/$N.nes
python3 - "$N" <<'PY'
import sys
n = sys.argv[1]
names = ["title", "platformer", "parallax-irq", "shooter", "town-rpg", "fighter-dmc", "loads", "split-2006", "vertical-scroll"]
with open(f"out/{n}.phases.tsv", "w") as f:
    for i, nm in enumerate(names): f.write(f"{round(i * 1500 / 60.0988, 1)}\t{nm}\n")
PY
echo "built out/$N.nes"
