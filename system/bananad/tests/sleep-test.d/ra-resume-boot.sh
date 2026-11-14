# sleep-test.sh scenario: Power off after sleeping with a RetroArch that never reads its command pipe
# (sourced by sleep-test.sh, after its own: its helpers start, stop, ev, wake, asleep, ok and bad)

echo "ra-resume-boot 1. RetroArch gone before it took the commands (no state saved): shut down, no resume-boot"
# (a stand-in that ends as soon as it runs again, without opening the pipe: what a RetroArch crashing on SIGCONT does)
mkdir -p "$W/crash"
cc -O2 -o "$W/crash/retroarch" -x c - <<'XEOF'
#include <signal.h>
#include <unistd.h>
static void on(int s) { (void)s; _exit(1); }
int main(void) { signal(SIGCONT, on); for (;;) pause(); }
XEOF
mkdir -p /data/retroarch; printf 'state_slot = "0"\n' > /data/retroarch/retroarch.cfg
rm -f /run/banana/retroarch.cmd /data/banana/resume-boot
BANANA_ROM="/data/roms/gba/Orchard Tactics.gba" setsid sh -c '"$0"; :' "$W/crash/retroarch" & RAG=$!
sleep 0.2; echo $RAG > /run/banana/game.pid
start "poweroff_after_sleep_minutes = 0.02"      # 1.2 s
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
asleep && ok "suspended" || bad "no suspend"
sleep 1.3; : > /sys/class/rtc/rtc0/wakealarm; wake
for i in $(seq 50); do [ $POWEROFF = 1 ] && break; sleep 0.1; done
[ $POWEROFF = 1 ] && ok "shut down" || bad "no shutdown"
grep -q "RetroArch's command pipe closed" /data/banana/logs/sleep.log && ok "logged: no state saved" || bad "not logged"
[ ! -e /data/banana/resume-boot ] && ok "no resume-boot (no state was saved)" || bad "resume-boot written: $(tr '\n' ' ' < /data/banana/resume-boot)"
stop; POWEROFF=0; echo 0 > /run/test/lid; echo 0 > /sys/class/backlight/backlight0/bl_power
rm -f /run/banana/game.pid /data/banana/resume-boot
