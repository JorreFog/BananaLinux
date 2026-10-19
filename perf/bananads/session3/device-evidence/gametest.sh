#!/bin/sh
# gametest.sh <tag> <bin> <rom path> [KEY=value...]: one game on the panels through the menu's launch hook, <bin>
# bind-mounted over /usr/bin/bananads, the card's settings, a copy of the game's save, no resume. Start then A tapped
# every 0.75 s for 30 s (title, menus, dialogue), then A held 15 s; screenshots of both panels at 20, 35 and 50 s
# (grab.py: a ~0.1 s stall each); kms.stats=60, cpugov and audio stats. Everything in /data/kmsmk/g-<tag>/.
T=$1; B=$2; ROM=$3; shift 3
D=/data/bananads-dev/s3
[ -s /run/banana/game.pid ] && { echo "$T GAME-RUNNING: abort"; exit 1; }
pidof bananads bananads.real >/dev/null && { echo "$T BANANADS-RUNNING: abort"; exit 1; }
pidof banana-ui >/dev/null || { echo "$T: no menu"; exit 1; }
[ -x "$B" ] || { echo "$T: no binary $B"; exit 1; }
[ -f "$ROM" ] || { echo "$T: no rom $ROM"; exit 1; }
i=0; while [ $(cat /sys/class/thermal/thermal_zone0/temp) -ge ${MAXTEMP:-52000} ] && [ $i -lt 240 ]; do sleep 2; i=$((i+2)); done
T0=$(($(cat /sys/class/thermal/thermal_zone0/temp)/1000))
G=$(basename "$ROM"); G=${G%.*}
W=/data/kmsmk/g-$T; rm -rf $W; mkdir -p $W/st $W/sv $W/gov
[ -f "/data/saves/nds/$G.sav" ] && cp "/data/saves/nds/$G.sav" $W/sv/
{ echo "BANANADS_SAVE_DIR=$W/sv"; echo "BANANADS_STATE_DIR=$W/st"; echo "BANANADS_RESUME_ON_START=off"; echo "BANANADS_RESUME_ON_QUIT=off"
  echo "BANANADS_CPUGOV_DIR=$W/gov"; echo "BANANADS_KMS_STATS=60"; echo "BANANADS_CPUGOV_LOG=on"; echo "BANANADS_AUDIO_STATS=on"
  echo "BANANADS_RA_USER="; for kv in "$@"; do echo "$kv"; done; } > /data/banana/bananads.env
cleanup() { umount /usr/bin/bananads 2>/dev/null; rm -f /data/banana/bananads.env /run/banana/launch; }
trap cleanup EXIT; trap 'exit 130' INT TERM
echo "# $(date -u +%H:%M:%S) $T: $B rom=$G temp=${T0}C env: $*" > $W/run.txt
mount --bind $B /usr/bin/bananads
echo "$ROM" > /run/banana/launch
i=0; until pidof bananads >/dev/null; do sleep 0.2; i=$((i+1)); [ $i -gt 100 ] && { echo "$T: the menu didn't start the game"; exit 1; }; done
K=/data/bananads-dev/padkey; E=$(awk '/^N: Name="retrogame_joypad"/{f=1} f&&/^H:/{for(i=1;i<=NF;i++) if($i ~ /^event/) {print "/dev/input/"$i; exit}}' /proc/bus/input/devices)
tap() { $K $E $1 1; sleep 0.1; $K $E $1 0; }
shot() { python3 $D/grab.py $W/shot-$1.png all >/dev/null 2>&1; }
sleep 5; tap 315; sleep 0.65
i=0; while [ $i -lt 40 ]; do tap 305; sleep 0.65; i=$((i+1)); [ $i -eq 19 ] && shot 20; done
$K $E 305 1; sleep 1; shot 35; sleep 14; $K $E 305 0; shot 50
T1=$(($(cat /sys/class/thermal/thermal_zone0/temp)/1000))
alive=$(pidof bananads)
kill -TERM $(pidof bananads) 2>/dev/null
i=0; while { pidof bananads >/dev/null || [ -s /run/banana/game.pid ]; } && [ $i -lt 100 ]; do sleep 0.2; i=$((i+1)); done
sleep 1; cp /data/banana/logs/bananads/bananads.log $W/bananads.log
echo "temp ${T0}-${T1}C alive_at_end=${alive:-NO}" >> $W/run.txt
W60=$(grep -c '^\[kms\] 60 frames' $W/bananads.log); BAD=$(awk '/^\[kms\] 60 frames shown/ {v=$8; gsub(/[^0-9.]/,"",v); if (v<59) b++} END{print b+0}' $W/bananads.log)
ERR=$(grep -c -i -E 'segfault|crash|assert|fatal|error' $W/bananads.log)
echo "$T: ${alive:+ran}${alive:-DIED} temp ${T0}-${T1}C, $W60 windows, $BAD under 59, log errors $ERR, shots $(ls $W/*.png 2>/dev/null | wc -l)"
