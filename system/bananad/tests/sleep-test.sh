#!/bin/sh
# bananad's sleep, tested without an RG DS: bananad runs in its own mount and PID namespaces against a made-up
# /sys (the battery, the LEDs, the backlights, the RTC, /sys/power) and /dev/input (FIFOs that carry real
# struct input_event records). /sys/power/state is a FIFO: bananad's write of "mem" blocks until the test reads it,
# which is the handheld being asleep; the test decides what the world looks like when it "wakes". A stand-in game
# (its process group must be stopped while asleep) and a stand-in banana-ui (it must get SIGUSR1 after a wake).
#
#   sudo system/bananad/tests/sleep-test.sh        (root: it mounts in a private namespace; nothing outside changes)
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
ROOTFS=$(cd "$HERE/../rootfs" && pwd)
if [ "$1" != --inside ]; then
    W=$(mktemp -d)
    cc -O2 -o "$W/bananad" "$HERE/bananad.c"
    cc -O2 -o "$W/banana-ui" -x c - <<'EOF'
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
static void on(int s) { FILE *f = fopen("/run/test/ui-signals", "a"); if (f) { fprintf(f, "SIGUSR1\n"); fclose(f); } }
int main(void) { signal(SIGUSR1, on); for (;;) pause(); }
EOF
    mkdir -p /data 2>/dev/null || true
    exec unshare -m -p -f --mount-proc sh "$0" --inside "$W" "$ROOTFS"
fi

# ---- inside: pid 1 is this shell, so bananad's "shut down" (SIGUSR2 to init) comes here ------------------------
W=$2 ROOTFS=$3
POWEROFF=0
trap 'POWEROFF=1' USR2
mount -t tmpfs none /sys/power
mount -t tmpfs none /sys/class
mount -t tmpfs none /dev/input 2>/dev/null || { mkdir -p /dev/input; mount -t tmpfs none /dev/input; }
mount -t tmpfs none /run
mount -t tmpfs none /data
mkdir -p /etc/banana && mount -t tmpfs none /etc/banana && cp -r "$ROOTFS/etc/banana/." /etc/banana/
mkdir -p /run/banana /run/test /data/banana/logs /sys/class/rtc/rtc0 /sys/class/power_supply/battery
echo Battery > /sys/class/power_supply/battery/type
echo Discharging > /sys/class/power_supply/battery/status
echo 50 > /sys/class/power_supply/battery/capacity
: > /sys/class/rtc/rtc0/wakealarm
echo "s2idle [deep]" > /sys/power/mem_sleep
echo 0 > /sys/power/pm_async
for l in green:power red:status amber:charging; do mkdir -p /sys/class/leds/$l; : > /sys/class/leds/$l/trigger; : > /sys/class/leds/$l/brightness; done
for b in backlight0 backlight1; do mkdir -p /sys/class/backlight/$b; echo 255 > /sys/class/backlight/$b/max_brightness; echo 200 > /sys/class/backlight/$b/brightness; echo 0 > /sys/class/backlight/$b/bl_power; done
mkfifo /sys/power/state /dev/input/event0
exec 3<>/dev/input/event0       # a writer that stays, so bananad's poll doesn't see a hang-up
echo 0 > /run/test/lid

pass=0 fail=0
ok() { pass=$((pass + 1)); echo "  ok: $*"; }
bad() { fail=$((fail + 1)); echo "  FAIL: $*"; }
# an input event: type code value (then a SYN_REPORT)
ev() { python3 -c "
import struct, sys
t, c, v = (int(x) for x in sys.argv[1:4])
sys.stdout.buffer.write(struct.pack('llHHi', 0, 0, t, c, v) + struct.pack('llHHi', 0, 0, 0, 0, 0))" "$@" >&3; }
EV_SW=5 SW_LID=0 EV_KEY=1 KEY_POWER=116
# is bananad blocked writing "mem" (asleep)? wait up to 3 s
# (blocked opening the FIFO: its wait channel is wait_for_partner)
asleep() { for i in $(seq 30); do [ "$(cat /proc/$BD/wchan 2>/dev/null)" = wait_for_partner ] && return 0; sleep 0.1; done; return 1; }
wake() { head -c 3 /sys/power/state > /dev/null; sleep 0.3; }       # read "mem": the write returns, it is awake
gstate() { awk '{print $3}' /proc/$GAME/stat; }
start() {   # start [bananad.conf lines]
    echo "s2idle [deep]" > /sys/power/mem_sleep     # the kernel's file always lists both
    printf '%s\n' "autosleep_minutes = 0" "sleep_check_hours = 0" "$@" > /data/banana/bananad.conf
    BANANAD_TEST_LID=/run/test/lid "$W/bananad" & BD=$!
    sleep 0.5
}
stop() { kill $BD 2>/dev/null; wait $BD 2>/dev/null || true; }

"$W/banana-ui" & UI=$!
setsid sleep 600 & GAME=$!
sleep 0.2
echo $GAME > /run/banana/game.pid

echo "1. lid closed, then opened"
start
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
if asleep; then ok "suspended on the lid"; else bad "no suspend on the lid"; fi
[ "$(gstate)" = T ] && ok "game stopped while asleep" || bad "game not stopped ($(gstate))"
[ "$(cat /sys/power/pm_async)" = 1 ] && ok "devices resume in parallel (pm_async 1)" || bad "pm_async $(cat /sys/power/pm_async)"
grep -q "\[deep\]" /sys/power/mem_sleep && ok "deep sleep chosen" || bad "mem_sleep $(cat /sys/power/mem_sleep)"
echo 0 > /run/test/lid; wake
[ "$(gstate)" != T ] && ok "game running again after the wake" || bad "game still stopped"
rm -f /run/banana/game.pid; sleep 0.1
grep -q 'awake (lid opened)' /data/banana/logs/sleep.log && ok "sleep.log: $(tail -n1 /data/banana/logs/sleep.log | cut -c21-)" || bad "no wake in sleep.log"
stop

echo "2. menu asleep, woken with the lid still shut, then opened"
start
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
asleep && ok "suspended" || bad "no suspend"
wake      # the power button bumped in a bag: the lid is still shut
if asleep; then ok "straight back to sleep with the lid shut"; else bad "stayed awake with the lid shut"; fi
[ ! -s /run/test/ui-signals ] && ok "the menu wasn't woken for it" || bad "the menu was woken with the lid shut"
echo 0 > /run/test/lid; wake
sleep 0.2
grep -q SIGUSR1 /run/test/ui-signals && ok "menu told to redraw after the real wake" || bad "menu not told to redraw"
grep -q 'slept again with the lid shut' /data/banana/logs/sleep.log && ok "logged the second sleep" || bad "second sleep not logged"
stop

echo "3. the battery guard: an RTC wake with the battery nearly empty shuts down cleanly"
echo $GAME > /run/banana/game.pid
echo 3 > /sys/class/power_supply/battery/capacity
start "sleep_check_hours = 0.002" "sleep_poweroff_percent = 5"
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
asleep && ok "suspended with the RTC alarm set ($(cat /sys/class/rtc/rtc0/wakealarm))" || bad "no suspend"
: > /sys/class/rtc/rtc0/wakealarm; wake      # the alarm fired (the RTC clears it): the RTC woke it
for i in $(seq 30); do [ $POWEROFF = 1 ] && break; sleep 0.1; done
[ $POWEROFF = 1 ] && ok "shut down cleanly (init asked to power off)" || bad "no shutdown at 3%"
kill -0 $GAME 2>/dev/null && bad "the game wasn't told to quit and save" || ok "the game was told to quit and save first"
grep -q 'battery check: 3%' /data/banana/logs/sleep.log && ok "logged the battery check" || bad "battery check not logged"
stop
POWEROFF=0
setsid sleep 600 & GAME=$!; sleep 0.1; echo $GAME > /run/banana/game.pid

echo "4. an RTC wake with enough battery goes back to sleep"
echo 60 > /sys/class/power_supply/battery/capacity
start "sleep_check_hours = 0.002"
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
asleep && ok "suspended" || bad "no suspend"
: > /sys/class/rtc/rtc0/wakealarm; wake
if asleep; then ok "back to sleep after the battery check"; else bad "awake after the battery check"; fi
[ "$(gstate)" = T ] && ok "the game stayed stopped" || bad "the game ran during the check"
echo 0 > /run/test/lid; wake; sleep 0.2
grep -q 'battery checked' /data/banana/logs/sleep.log && ok "logged" || bad "not logged"
stop

echo "5. a suspend that fails: the safer ways, then the panels dark until the lid opens"
rm -f /sys/power/state; : > /sys/power/state; chmod 000 /sys/power/state
# root can write a mode-000 file; a directory can't be written at all
rm -f /sys/power/state; mkdir /sys/power/state
start
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
sleep 1.5
grep -q 'trying the safer way' /data/banana/logs/sleep.log && ok "fell back after the failure" || bad "no fallback"
[ "$(cat /sys/power/mem_sleep)" = s2idle ] && ok "s2idle tried" || bad "mem_sleep $(cat /sys/power/mem_sleep)"
[ "$(cat /sys/class/backlight/backlight0/bl_power)" = 4 ] && ok "panels dark with the lid shut" || bad "backlight bl_power $(cat /sys/class/backlight/backlight0/bl_power)"
echo 0 > /run/test/lid; ev $EV_SW $SW_LID 0; sleep 0.3
[ "$(cat /sys/class/backlight/backlight0/bl_power)" = 0 ] && ok "panels back when the lid opens" || bad "panels still dark"
stop
rmdir /sys/power/state; mkfifo /sys/power/state

echo "6. lid sleep off: closing the lid only turns the backlights off"
start "lid_sleep = no"
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1; sleep 0.3
[ "$(cat /sys/class/backlight/backlight1/bl_power)" = 4 ] && ok "backlights off" || bad "backlights on"
asleep 2>/dev/null && bad "it suspended" || ok "no suspend"
echo 0 > /run/test/lid; ev $EV_SW $SW_LID 0; sleep 0.3
[ "$(cat /sys/class/backlight/backlight1/bl_power)" = 0 ] && ok "backlights on again" || bad "still off"
stop

kill $UI $GAME 2>/dev/null || true
echo "sleep test: $pass passed, $fail failed"
[ $fail = 0 ]
