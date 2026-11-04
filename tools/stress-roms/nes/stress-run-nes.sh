#!/bin/bash
# stress-run.sh plus|rgds <rom> <seconds> [tag] [--interval-us N]
# One stress-ROM run with full data collection. Run it under banana-device-test's locked.sh:
#   locked.sh plus -- stress-run.sh plus out/dsstress-realistic.nds 600 baseline
# Pushes the ROM + collector to /run/stress, starts the ROM through the menu (/run/banana/launch, the real launcher),
# collects while it plays, quits it (SIGTERM), and saves everything to ~/stress-runs/<rom>/<date>-<dev>-<tag>/.
set -u
S=$HOME/.claude/skills/stress-test-roms/scripts; H=$S; . "$HOME/.claude/skills/banana-device-test/scripts/common.sh"
# NES variant: the menu launches only games in its library, so the ROM goes to /data/roms/nes for the run and is removed after.
# EMU_ENV (optional): a remote shell snippet run before the launch (e.g. selecting another core in settings.conf)
D=$1; ROM=$2; SEC=$3; TAG=${4:-run}; US=100000; [ "${5:-}" = --interval-us ] && US=$6
dev_ok "$D"; [ -f "$ROM" ] || { echo "no ROM $ROM" >&2; exit 2; }
B=$(basename "$ROM"); OUT=${STRESS_RUNS:-$HOME/stress-runs}/${B%.*}/$(date +%Y%m%d-%H%M%S)-$D-$TAG; mkdir -p "$OUT"
cp "$ROM" "$OUT/"; [ -f "${ROM%.*}.phases.tsv" ] && cp "${ROM%.*}.phases.tsv" "$OUT/phases.tsv"
{ echo "device=$D tag=$TAG rom=$B seconds=$SEC interval_us=$US date=$(date -Is)"
  echo "rom_md5=$(md5sum < "$ROM" | cut -d' ' -f1)"
  for r in ~/BananaLinux ~/superdrastic "$(dirname "$(readlink -f "$ROM")")"; do
    git -C "$r" rev-parse --show-toplevel >/dev/null 2>&1 && echo "git $(git -C "$r" rev-parse --show-toplevel) $(git -C "$r" rev-parse --short HEAD) $(git -C "$r" branch --show-current) dirty=$(git -C "$r" status --porcelain | wc -l)"
  done; } > "$OUT/meta.txt"
dev "$D" "$GATE" >> "$OUT/meta.txt" || { echo "device busy" >&2; exit 75; }
dev "$D" 'cat /etc/os-release; cat /data/banana/*.conf /data/bananads/bananads.conf 2>/dev/null' > "$OUT/device-config.txt"
dev "$D" 'rm -rf /run/stress; mkdir -p /run/stress /data/roms/nes' && dev "$D" 'cat > /run/stress/collect.sh' < "$H/collect.sh" &&
  dev "$D" "cat > '/data/roms/nes/$B'" < "$ROM" || exit 1
dev "$D" "${EMU_ENV:-true}; killall banana-ui; sleep 7"
sleep 3
dev "$D" "true
  for t in \$(seq 40); do echo '/data/roms/nes/$B' > /run/banana/launch; for i in \$(seq 10); do [ -e /run/banana/game.pid ] && break 2; sleep 0.5; done; done
  [ -e /run/banana/game.pid ] || { echo 'game did not start'; exit 1; }
  sleep 2; G=\$(cut -d' ' -f1 /run/banana/game.pid); P=
  for n in bananads drastic retroarch fake08; do P=\$(pidof \$n | cut -d' ' -f1); [ -n \"\$P\" ] && break; done
  if [ -z \"\$P\" ]; then P=\$G; while c=\$(cat /proc/\$P/task/\$P/children 2>/dev/null | cut -d' ' -f1); [ -n \"\$c\" ]; do P=\$c; done; fi   # else the deepest child of the launcher
  echo \"pid=\$P \$(cat /proc/\$P/comm)\"
  sh /run/stress/collect.sh /run/stress/data \$P $SEC $US
  kill -TERM \$P; for i in \$(seq 40); do [ -e /run/banana/game.pid ] || break; sleep 0.5; done
  [ -e /run/banana/game.pid ] && echo 'WARNING: game.pid still there'
  cp /run/banana/*.log /run/stress/data/ 2>/dev/null; mkdir -p /run/stress/data/logs; cp /data/banana/logs/* /run/stress/data/logs/ 2>/dev/null
  for f in /data/bananads/bananads.log /run/banana/retroarch.log; do [ -f \$f ] && cp \$f /run/stress/data/; done
  ls /data/saves/*/'${B%.*}'.* /data/*/saves/'${B%.*}'.* 2>/dev/null | while read s; do cp \"\$s\" /run/stress/data/; done
  rm -f '/data/roms/nes/$B'; rmdir /data/roms/nes 2>/dev/null; true" | tee "$OUT/run.txt"
dev "$D" 'tar -C /run/stress/data -cf - . && rm -rf /run/stress' | tar -C "$OUT" -xf -
python3 -I "$H/summarize.py" "$OUT" > "$OUT/summary.md" && cat "$OUT/summary.md"
echo "data: $OUT"
