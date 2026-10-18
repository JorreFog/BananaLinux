#!/bin/sh
# rgds-sim/ab.sh <binA> <binB> <rom> [--label L] [--mode st|dev|raw] [--frames N1:N2]
#                [--a "--set k=v ..."] [--b "--set k=v ..."] [--set k=v ...]
# A/B under rgds-sim: two binaries, or one binary twice with --a/--b setting sets (MELONDS_*/BANANADS_* variables:
# put them in --a/--b as "env:VAR=value"). Runs A then B (each run is two qemu, N1 and N2 frames) and prints, per
# thread, modeled ms/frame at 1104 and 1992 MHz A -> B, and the buckets and events that moved.
# Output: $OUT/<label>-A, $OUT/<label>-B, and $OUT/<label>-ab.txt. Same model version for both (params.json is read
# once and copied to $OUT/<label>-params.json).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
BA=$1; BB=$2; ROM=$3; shift 3
L=ab-$(basename "$ROM" .nds); MODE=st; FR=100:300; SA=""; SB=""; COMMON=""
while [ $# -gt 0 ]; do
  case $1 in
    --label) L=$2; shift 2 ;;
    --mode) MODE=$2; shift 2 ;;
    --frames) FR=$2; shift 2 ;;
    --a) SA=$2; shift 2 ;;
    --b) SB=$2; shift 2 ;;
    *) COMMON="$COMMON $1"; shift ;;
  esac
done
O=${OUT:-$PWD/rgds-runs}; mkdir -p "$O"
cp "${PARAMS:-$HERE/params.json}" "$O/$L-params.json"
run() {  # run <bin> <suffix> <sets>
  envs=""; sets=""
  for x in $3; do case $x in env:*) envs="$envs ${x#env:}" ;; *) sets="$sets $x" ;; esac; done
  env $envs PARAMS="$O/$L-params.json" OUT="$O" sh "$HERE/run.sh" "$1" "$ROM" "$L-$2" --mode $MODE --frames $FR $COMMON $sets > /dev/null
}
run "$BA" A "$SA"
run "$BB" B "$SB"
python3 "$HERE/ab.py" "$O/$L-A/report.json" "$O/$L-B/report.json" "$O/$L-params.json" | tee "$O/$L-ab.txt"
