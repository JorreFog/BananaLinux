#!/bin/sh
# kmsmk4.sh <tag> <bin> <secs> [KEY=value env lines...]: Mario Kart DS on the panels, started by the menu's launch hook
# (/run/banana/launch: as A on it in the library), the real launcher (run-bananads, KMS, the card's bananads.conf:
# 2x + ds-grid-2x, cpugov), <bin> bind-mounted over /usr/bin/bananads, a copy of the user's save, cpugov memory empty.
# From a cold boot: A taps every 0.75 s from 5 s to 23 s (title -> Grand Prix -> Figure-8 Circuit), then A held <secs>.
# kms.stats=60 windows, cpugov and audio stats in /data/kmsmk/s3-<tag>/bananads.log. Gates: no game running, SoC < 50 C.
T=$1; B=$2; S=$3; shift 3
D=/data/bananads-dev/s3
[ -s /run/banana/game.pid ] && { echo "$T GAME-RUNNING: abort"; exit 1; }
pidof bananads bananads.real >/dev/null && { echo "$T BANANADS-RUNNING: abort"; exit 1; }
pidof banana-ui >/dev/null || { echo "$T: no menu"; exit 1; }
[ -x "$B" ] || { echo "$T: no binary $B"; exit 1; }
i=0; while [ $(cat /sys/class/thermal/thermal_zone0/temp) -ge ${MAXTEMP:-50000} ] && [ $i -lt 240 ]; do sleep 2; i=$((i+2)); done
T0=$(($(cat /sys/class/thermal/thermal_zone0/temp)/1000))
ROM="/data/roms/nds/Mario Kart DS (USA) (En,Fr,De,Es,It).nds"; G="Mario Kart DS (USA) (En,Fr,De,Es,It)"
W=/data/kmsmk/s3-$T; rm -rf $W; mkdir -p $W/st $W/sv $W/gov
cp "/data/saves/nds/$G.sav" $W/sv/
{ echo "BANANADS_SAVE_DIR=$W/sv"; echo "BANANADS_STATE_DIR=$W/st"; echo "BANANADS_RESUME_ON_START=off"; echo "BANANADS_RESUME_ON_QUIT=off"
  echo "BANANADS_CPUGOV_DIR=$W/gov"; echo "BANANADS_KMS_STATS=60"; echo "BANANADS_CPUGOV_LOG=on"; echo "BANANADS_AUDIO_STATS=on"
  echo "BANANADS_RA_USER="; for kv in "$@"; do echo "$kv"; done; } > /data/banana/bananads.env
cleanup() { umount /usr/bin/bananads 2>/dev/null; rm -f /data/banana/bananads.env /run/banana/launch; }
trap cleanup EXIT; trap 'exit 130' INT TERM
echo "# $(date -u +%H:%M:%S) $T: $B held=${S}s temp=${T0}C env: $*" > $W/run.txt
mount --bind $B /usr/bin/bananads
echo "$ROM" > /run/banana/launch
i=0; until pidof bananads >/dev/null; do sleep 0.2; i=$((i+1)); [ $i -gt 100 ] && { echo "$T: the menu didn't start the game"; exit 1; }; done
K=/data/bananads-dev/padkey; E=/dev/input/event6
sleep 5; i=0; while [ $i -lt 25 ]; do $K $E 305 1; sleep 0.1; $K $E 305 0; sleep 0.65; i=$((i+1)); done
$K $E 305 1; sleep $S; $K $E 305 0
T1=$(($(cat /sys/class/thermal/thermal_zone0/temp)/1000))
kill -TERM $(pidof bananads)
i=0; while { pidof bananads >/dev/null || [ -s /run/banana/game.pid ]; } && [ $i -lt 100 ]; do sleep 0.2; i=$((i+1)); done
sleep 1; cp /data/banana/logs/bananads/bananads.log $W/bananads.log
echo "temp ${T0}-${T1}C" >> $W/run.txt
echo "$T done, temp ${T0}-${T1}C, $(grep -c '^\[kms\] 60 frames' $W/bananads.log) windows"
