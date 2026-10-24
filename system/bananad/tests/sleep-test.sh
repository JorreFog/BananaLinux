#!/bin/sh
# bananad's sleep, tested without an RG DS: bananad runs in its own mount and PID namespaces against a made-up
# /sys (the battery, the LEDs, the backlights, the RTC, /sys/power) and /dev/input (FIFOs that carry real
# struct input_event records). /sys/power/state is a FIFO: bananad's write of "mem" blocks until the test reads it,
# which is the handheld being asleep; the test decides what the world looks like when it "wakes". A stand-in game
# (its process group must be stopped while asleep) and a stand-in banana-ui (it must get SIGUSR1 after a wake).
#
#   sudo system/bananad/tests/sleep-test.sh        (root: it mounts in a private namespace; nothing outside changes)
#   sleep-test.sh --build <dir>                      only the programs, into <dir> (for a runner of its own, then --inside)
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
ROOTFS=$(cd "$HERE/../rootfs" && pwd)
if [ "$1" != --inside ]; then
    if [ "$1" = --build ]; then W=$2; mkdir -p "$W"; else W=$(mktemp -d); fi
    cc -O2 -o "$W/bananad" "$HERE/bananad.c"
    cc -O2 -o "$W/banana-ui" -x c - <<'EOF'
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
static void on(int s) { FILE *f = fopen("/run/test/ui-signals", "a"); if (f) { fprintf(f, "SIGUSR1\n"); fclose(f); } }
int main(void) { signal(SIGUSR1, on); for (;;) pause(); }
EOF
    # stand-in emulators for Power off after sleeping, one program under each name bananad looks for (drastic,
    # bananads, retroarch): what reaches it (SIGUSR1, SIGTERM, the lines on its stdin) in /run/test/<name>.log, then
    # "saved" a moment later and it quits, on a signal or on RetroArch's QUIT
    cc -O2 -o "$W/drastic" -x c - <<'EOF'
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static volatile sig_atomic_t got;
static void on(int s) { got = s; }
static const char *name;
static void say(const char *m) { char p[128]; snprintf(p, sizeof p, "/run/test/%s.log", name); FILE *f = fopen(p, "a"); if (f) { fprintf(f, "%s\n", m); fclose(f); } }
int main(int argc, char **argv)
{
	(void)argc; name = basename(argv[0]);
	signal(SIGUSR1, on); signal(SIGTERM, on);
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
	char buf[512]; size_t have = 0;
	for (;;) {
		if (got) { say(got == SIGUSR1 ? "USR1" : "TERM"); usleep(300000); say("saved"); return 0; }
		ssize_t r = read(0, buf + have, sizeof buf - 1 - have);
		if (r > 0) {
			have += r; buf[have] = 0;
			char *nl;
			while ((nl = strchr(buf, '\n'))) {
				*nl = 0; say(buf);
				int quit = !strcmp(buf, "QUIT");
				memmove(buf, nl + 1, strlen(nl + 1) + 1); have = strlen(buf);
				if (quit) { usleep(300000); say("saved"); return 0; }
			}
		}
		usleep(20000);
	}
}
EOF
    cp "$W/drastic" "$W/bananads"; cp "$W/drastic" "$W/retroarch"
    [ "$1" = --build ] && exit 0
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

# Power off after sleeping: a game as banana-ui starts one (sh -c in a session of its own, BANANA_ROM set), the
# emulator a child of the launcher
game() {    # game <emulator> <rom> [stdin]
    rm -f /run/test/$1.log
    BANANA_ROM=$2 setsid sh -c '"$0" < "$1"; :' "$W/$1" "${3:-/dev/null}" & GAME=$!
    sleep 0.2; echo $GAME > /run/banana/game.pid
}
poweroff() { for i in $(seq 50); do [ $POWEROFF = 1 ] && return 0; sleep 0.1; done; return 1; }
echo 60 > /sys/class/power_supply/battery/capacity

echo "9. Power off after sleeping: the deadline's alarm, the lid still shut: SuperDrastic saves and quits, a shutdown"
game drastic "/data/roms/nds/Peel Quest.nds"
: > /run/test/ui-signals; rm -f /data/banana/resume-boot
start "sleep_check_hours = 4" "poweroff_after_sleep_minutes = 0.02"    # 1.2 s
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
asleep && ok "suspended" || bad "no suspend"
[ "$(cat /sys/class/rtc/rtc0/wakealarm)" = +1 ] && ok "the RTC alarm at the deadline, not the 4 h battery check" || bad "wakealarm $(cat /sys/class/rtc/rtc0/wakealarm)"
[ "$(gstate)" = T ] && ok "game stopped while asleep" || bad "game not stopped ($(gstate))"
sleep 1.3; : > /sys/class/rtc/rtc0/wakealarm; wake
poweroff && ok "shut down cleanly (init asked to power off)" || bad "no shutdown at the deadline"
grep -qx USR1 /run/test/drastic.log 2>/dev/null && ok "drastic got SIGUSR1 (save a resume state and quit)" || bad "drastic: $(cat /run/test/drastic.log 2>/dev/null)"
grep -qx saved /run/test/drastic.log 2>/dev/null && ! kill -0 $GAME 2>/dev/null && ok "the shutdown waited for the save" || bad "shut down before the save"
[ "$(cat /data/banana/resume-boot 2>/dev/null)" = "$(printf 'rom=/data/roms/nds/Peel Quest.nds\nemulator=superdrastic')" ] && ok "resume-boot names the game" || bad "resume-boot: $(cat /data/banana/resume-boot 2>/dev/null)"
[ "$(cat /sys/class/backlight/backlight0/bl_power)" = 4 ] && ok "the panels stayed dark" || bad "bl_power $(cat /sys/class/backlight/backlight0/bl_power)"
[ ! -s /run/test/ui-signals ] && ok "the menu wasn't woken" || bad "the menu was told to redraw"
grep -q 'the game saved, then shutting down' /data/banana/logs/sleep.log && ok "logged" || bad "not logged"
stop; POWEROFF=0; echo 0 > /run/test/lid; echo 0 > /sys/class/backlight/backlight0/bl_power

echo "10. a power-button sleep with the lid open: BananaDS saves at the deadline"
game bananads "/data/roms/nds/Banana Kart DS.nds"
start "poweroff_after_sleep_minutes = 0.02"     # (sleep_check_hours 0: the deadline is the only alarm)
ev $EV_KEY $KEY_POWER 1; ev $EV_KEY $KEY_POWER 0
asleep && ok "suspended on the power button" || bad "no suspend"
[ "$(cat /sys/class/rtc/rtc0/wakealarm)" = +1 ] && ok "the alarm set with no battery check" || bad "wakealarm $(cat /sys/class/rtc/rtc0/wakealarm)"
sleep 1.3; : > /sys/class/rtc/rtc0/wakealarm; wake
poweroff && ok "shut down" || bad "no shutdown"
grep -qx USR1 /run/test/bananads.log 2>/dev/null && grep -qx saved /run/test/bananads.log && ok "bananads got SIGUSR1 and saved" || bad "bananads: $(cat /run/test/bananads.log 2>/dev/null)"
grep -qx emulator=bananads /data/banana/resume-boot 2>/dev/null && ok "resume-boot: bananads" || bad "resume-boot: $(cat /data/banana/resume-boot 2>/dev/null)"
stop; POWEROFF=0

echo "11. RetroArch: the slot down to its automatic one, SAVE_STATE, QUIT (through its command pipe)"
mkdir -p /data/retroarch; printf 'state_slot = "2"\n' > /data/retroarch/retroarch.cfg
rm -f /run/banana/retroarch.cmd; mkfifo /run/banana/retroarch.cmd; exec 5<>/run/banana/retroarch.cmd
game retroarch "/data/roms/gba/Orchard Tactics.gba" /run/banana/retroarch.cmd
start "poweroff_after_sleep_minutes = 0.02"
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
asleep && ok "suspended" || bad "no suspend"
sleep 1.3; : > /sys/class/rtc/rtc0/wakealarm; wake
poweroff && ok "shut down" || bad "no shutdown"
ra=$(grep -v AUDIO_REINIT /run/test/retroarch.log 2>/dev/null | tr '\n' ' ')     # (bananad's at its start: the pipe is there)
[ "$ra" = "STATE_SLOT_MINUS STATE_SLOT_MINUS STATE_SLOT_MINUS STATE_SLOT_MINUS SAVE_STATE QUIT saved " ] &&
    ok "RetroArch: slot 2 down past 0, saved, quit" || bad "retroarch: $ra"
grep -qx emulator=retroarch /data/banana/resume-boot 2>/dev/null && ok "resume-boot: retroarch" || bad "resume-boot: $(cat /data/banana/resume-boot 2>/dev/null)"
stop; POWEROFF=0; exec 5>&-; rm -f /run/banana/retroarch.cmd /data/banana/resume-boot; echo 0 > /run/test/lid

echo "12. another program (a port): SIGTERM, no resume-boot"
setsid sleep 600 & GAME=$!; sleep 0.1; echo $GAME > /run/banana/game.pid
start "poweroff_after_sleep_minutes = 0.02"
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
asleep && ok "suspended" || bad "no suspend"
sleep 1.3; : > /sys/class/rtc/rtc0/wakealarm; wake
poweroff && ok "shut down" || bad "no shutdown"
kill -0 $GAME 2>/dev/null && bad "the game wasn't stopped" || ok "the game got SIGTERM"
[ ! -e /data/banana/resume-boot ] && ok "no resume-boot (nothing to resume)" || bad "resume-boot written"
stop; POWEROFF=0; echo 0 > /run/test/lid

echo "13. on the charger the deadline doesn't count; unplugged, it starts at the real sleep; opening the lid first wins"
mkdir -p /sys/class/power_supply/charger
echo USB > /sys/class/power_supply/charger/type; echo 1 > /sys/class/power_supply/charger/online
setsid sleep 600 & GAME=$!; sleep 0.1; echo $GAME > /run/banana/game.pid
start "poweroff_after_sleep_minutes = 0.05"     # 3 s
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1; sleep 4
[ $POWEROFF = 0 ] && kill -0 $GAME 2>/dev/null && ok "dark on the charger past the deadline: no shutdown" || bad "shut down on the charger"
echo 0 > /sys/class/power_supply/charger/online
asleep && ok "asleep once unplugged" || bad "no sleep after unplugging"
[ "$(cat /sys/class/rtc/rtc0/wakealarm)" = +3 ] && ok "the deadline from the real sleep (+3)" || bad "wakealarm $(cat /sys/class/rtc/rtc0/wakealarm)"
echo 0 > /run/test/lid; wake       # the lid opened before the alarm
[ $POWEROFF = 0 ] && [ "$(gstate)" != T ] && ok "awake and playing, no shutdown" || bad "shut down or still paused after the lid opened"
grep -q 'awake (lid opened)' /data/banana/logs/sleep.log && ok "a normal wake" || bad "no wake logged"
rm -rf /sys/class/power_supply/charger
rm -f /run/banana/game.pid
stop

kill $UI $GAME 2>/dev/null || true
echo "sleep test: $pass passed, $fail failed"
[ $fail = 0 ]
