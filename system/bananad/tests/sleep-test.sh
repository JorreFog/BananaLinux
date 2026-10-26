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
mkfifo /sys/power/state /dev/input/event0 /run/test/wake
exec 5<>/sys/power/state        # the "kernel" takes bananad's "mem" at once (never read)...
exec 6<>/run/test/wake          # ...and bananad waits on this until wake() sends a byte (BANANAD_TEST_WAKE)
exec 3<>/dev/input/event0       # a writer that stays, so bananad's poll doesn't see a hang-up
mkfifo /run/test/touch; exec 4<>/run/test/touch     # the stand-in touchscreen bananad reads unpolled (BANANAD_TEST_TOUCH)
echo 0 > /run/test/lid

pass=0 fail=0
ok() { pass=$((pass + 1)); echo "  ok: $*"; }
bad() { fail=$((fail + 1)); echo "  FAIL: $*"; tail -n 4 /data/banana/logs/sleep.log 2>/dev/null | sed 's/^/    sleep.log: /'; }   # (what bananad did)
# an input event: type code value (then a SYN_REPORT)
ev() { python3 -c "
import struct, sys
t, c, v = (int(x) for x in sys.argv[1:4])
sys.stdout.buffer.write(struct.pack('llHHi', 0, 0, t, c, v) + struct.pack('llHHi', 0, 0, 0, 0, 0))" "$@" >&3; }
EV_SW=5 SW_LID=0 EV_KEY=1 KEY_POWER=116
# a touch on the stand-in touchscreen, stamped with the monotonic clock (as bananad asks the real one for)
touch() { python3 -c "
import struct, sys, time
t = time.clock_gettime(time.CLOCK_MONOTONIC); s = int(t); u = int((t - s) * 1e6)
sys.stdout.buffer.write(struct.pack('llHHi', s, u, 3, 0x35, 100) + struct.pack('llHHi', s, u, 0, 0, 0))" >&4; }
# is bananad asleep (its "mem" written, waiting for the wake: it says so in wake.asleep)? wait up to 3 s
asleep() { for i in $(seq 30); do [ -e /run/test/wake.asleep ] && return 0; sleep 0.1; done; return 1; }
# the wake: one byte, and only to a bananad that is asleep (a byte left over would end its next sleep at once)
wake() { if asleep; then printf x >&6; else bad "wake: bananad wasn't asleep"; fi; sleep 0.3; }
gstate() { awk '{print $3}' /proc/$GAME/stat; }
start() {   # start [bananad.conf lines]
    echo "s2idle [deep]" > /sys/power/mem_sleep     # the kernel's file always lists both
    printf '%s\n' "autosleep_minutes = 0" "sleep_check_hours = 0" "$@" > /data/banana/bananad.conf
    BANANAD_TEST_LID=/run/test/lid BANANAD_TEST_TOUCH=/run/test/touch BANANAD_TEST_WAKE=/run/test/wake "$W/bananad" & BD=$!
    sleep 0.5
}
stop() { kill $BD 2>/dev/null; wait $BD 2>/dev/null || true; rm -f /run/test/wake.asleep; }   # (killed asleep: no wake)

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
rmdir /sys/power/state; mkfifo /sys/power/state; exec 5<>/sys/power/state

echo "6. lid sleep off: closing the lid only turns the backlights off"
start "lid_sleep = no"
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1; sleep 0.3
[ "$(cat /sys/class/backlight/backlight1/bl_power)" = 4 ] && ok "backlights off" || bad "backlights on"
asleep 2>/dev/null && bad "it suspended" || ok "no suspend"
echo 0 > /run/test/lid; ev $EV_SW $SW_LID 0; sleep 0.3
[ "$(cat /sys/class/backlight/backlight1/bl_power)" = 0 ] && ok "backlights on again" || bad "still off"
stop

echo "7. idle in the menu: the panels dim, and come back at the first press"
rm -f /run/banana/game.pid
echo 200 > /sys/class/backlight/backlight0/brightness; echo 200 > /sys/class/backlight/backlight1/brightness
start "dim_minutes = 0.02" "dim_percent = 30"      # 1.2 s
sleep 2.5
[ "$(cat /sys/class/backlight/backlight1/brightness)" = 60 ] && ok "dimmed to 30% after the idle time" || bad "not dimmed: brightness $(cat /sys/class/backlight/backlight1/brightness)"
ev $EV_KEY 304 1; ev $EV_KEY 304 0; sleep 0.3         # BTN_SOUTH: any button
[ "$(cat /sys/class/backlight/backlight1/brightness)" = 200 ] && ok "back to 200 at the press" || bad "still dim: $(cat /sys/class/backlight/backlight1/brightness)"
echo $GAME > /run/banana/game.pid; sleep 2
[ "$(cat /sys/class/backlight/backlight1/brightness)" = 200 ] && ok "never dimmed while a game runs" || bad "dimmed during a game"
rm -f /run/banana/game.pid
stop

echo "8. charging: the lid shut keeps it awake and dark (the charging light stays on); unplugged, it sleeps"
mkdir -p /sys/class/power_supply/charger
echo USB > /sys/class/power_supply/charger/type; echo 1 > /sys/class/power_supply/charger/online
echo $GAME > /run/banana/game.pid
start
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1; sleep 1
asleep 2>/dev/null && bad "it suspended while charging" || ok "no suspend while charging"
[ "$(cat /sys/class/backlight/backlight0/bl_power)" = 4 ] && ok "panels dark" || bad "backlight bl_power $(cat /sys/class/backlight/backlight0/bl_power)"
a=$(cat /sys/class/leds/amber:charging/brightness)
[ -n "$a" ] && [ "$a" != 0 ] && ok "charging light on ($a)" || bad "charging light off ('$a')"
[ "$(gstate)" = T ] && ok "game paused" || bad "game not paused ($(gstate))"
echo 0 > /run/test/lid; sleep 1
[ "$(cat /sys/class/backlight/backlight0/bl_power)" = 0 ] && ok "panels back when the lid opens" || bad "panels still dark"
[ "$(gstate)" != T ] && ok "game running again" || bad "game still paused"
grep -q 'dark while charging for' /data/banana/logs/sleep.log && ok "logged" || bad "not logged"
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1; sleep 1
echo 0 > /sys/class/power_supply/charger/online
asleep && ok "asleep once the charger is pulled out" || bad "no sleep after unplugging"
echo 0 > /run/test/lid; wake
rm -rf /sys/class/power_supply/charger
rm -f /run/banana/game.pid
stop

# more scenarios, a file per topic (tests/sleep-test.d), run here in turn with the helpers above
for t in "$HERE"/tests/sleep-test.d/*.sh; do [ -f "$t" ] && . "$t"; done

echo "27. the flight recorder: buttons, the lid, a sleep, touches (no position while typing), a marker (Select held) and the stop, on the card"
# tev <type> <code> <value>...: events on the stand-in touchscreen, with the monotonic time (as the real one stamps them)
tev() { python3 -c "
import struct, sys, time
t = time.clock_gettime(time.CLOCK_MONOTONIC); s = int(t); u = int((t - s) * 1e6)
a = [int(x) for x in sys.argv[1:]]
sys.stdout.buffer.write(b''.join(struct.pack('llHHi', s, u, a[i], a[i + 1], a[i + 2]) for i in range(0, len(a), 3)))" "$@" >&4; }
rm -rf /data/banana/logs/sessions; rm -f /run/banana/keyboard
start "recorder = on"
[ -e /run/banana/recording ] && ok "recording (/run/banana/recording, for the menu's dot)" || bad "no /run/banana/recording"
F=$(cat /run/banana/recording 2>/dev/null)
ev $EV_KEY 304 1; ev $EV_KEY 304 0                          # A
tev 3 57 1  3 53 100  3 54 200  0 0 0                      # a touch: tracking id 1 at 100,200, then the report
tev 3 57 -1  0 0 0                                          # its lift
sleep 0.5
: > /run/banana/keyboard
tev 3 57 2  3 53 300  3 54 50  0 0 0;  tev 3 57 -1  0 0 0  # a touch while typing
sleep 0.5; rm -f /run/banana/keyboard
ev $EV_KEY 314 1; ev $EV_KEY 314 0; sleep 0.3                  # a quick Select: no marker
grep -q "MARK" "$F" 2>/dev/null && bad "a marker from a quick press" || ok "a quick Select: no marker"
ev $EV_KEY 314 1; sleep 2.4; ev $EV_KEY 314 0                  # Select held 2 s: a marker (written at once)
sleep 0.5
grep -q "MARK .*marker 1" "$F" 2>/dev/null && ok "Select held 2 s: the marker on the card at once" || bad "no marker in $F"
[ "$(cat /sys/class/leds/green:power/brightness)" = 128 ] && ok "the power LED back after its blinks" || bad "LED $(cat /sys/class/leds/green:power/brightness)"
# banana-ui's events: a datagram on the recorder's socket, copied in (control characters made spaces)
python3 -c "
import socket; s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
s.sendto(b'setting\tTurn off when idle: 1 hour -> 5 minutes', '/run/banana/recorder.sock')
s.sendto(b'game\tstart: Peel Quest\n(nds, run-nds)', '/run/banana/recorder.sock')"
sleep 0.3
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
asleep && ok "asleep" || bad "no sleep"
grep -q "button .*a down" "$F" && grep -q "lid .*closed" "$F" && ok "flushed before the sleep: the button, the lid" || bad "not flushed before the sleep: $(tail -n 3 "$F" 2>/dev/null | tr '\n' '|')"
grep -q "setting  Turn off when idle: 1 hour -> 5 minutes" "$F" && ok "banana-ui's event copied in (setting)" || bad "no setting line: $(grep -c setting "$F")"
grep -q "game     start: Peel Quest (nds, run-nds)" "$F" && ok "its newline made a space: one line each" || bad "game line: $(grep game "$F")"
grep -q "touch .*down 100,200" "$F" && grep -q "touch .*up 100,200" "$F" && ok "the touch, where (100,200)" || bad "touch lines: $(grep touch "$F" | tr '\n' '|')"
grep -q "keyboard open: where not recorded" "$F" && ! grep -q "300,50" "$F" && ok "the touch while typing, without where" || bad "while typing: $(grep touch "$F" | tr '\n' '|')"
echo 0 > /run/test/lid; wake; sleep 0.3
python3 -c "
import socket; socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM).sendto(b'menu\trestart (Settings)', '/run/banana/recorder.sock')"
stop    # (SIGTERM, as rcK's: before the unmount, the last lines written out at once, not at the next half-minute)
grep -q "menu     restart (Settings)" "$F" && grep -q "bananad  stopped" "$F" && ok "stopped: banana-ui's last event and the stop, on the card" || bad "at the stop: $(tail -n 2 "$F" | tr '\n' '|')"
sed -n '1,2p' "$F" | grep -q "flight recorder" && ok "the session's header" || bad "header: $(head -n1 "$F")"
start
[ ! -e /run/banana/recording ] && ok "recorder off: not recording" || bad "recording with recorder off"
n=$(ls /data/banana/logs/sessions | wc -l)
ev $EV_KEY 304 1; ev $EV_KEY 304 0; sleep 0.3
[ "$(ls /data/banana/logs/sessions | wc -l)" = "$n" ] && ok "recorder off: no new session" || bad "a session with recorder off"
stop

kill $UI $GAME 2>/dev/null || true
echo "sleep test: $pass passed, $fail failed"
[ $fail = 0 ]
