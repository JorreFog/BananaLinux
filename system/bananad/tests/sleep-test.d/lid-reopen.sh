# sleep-test.sh scenario: the lid opened again while the sleep is still being prepared
# (sourced by sleep-test.sh, after its own: its helpers start, stop, ev, wake, asleep, ok and bad)

echo "lid-reopen 1. the lid shut, then opened again while the pre-sleep hooks run: no suspend, awake as after a wake"
# (Wi-Fi's hook takes it down and unloads its driver, a second or more: the lid can open in the meantime, and its
# edge has gone by when the SoC suspends, so only the power button would wake it)
printf '#!/bin/sh\n[ "$1" = pre ] && sleep 1.5\nexit 0\n' > /etc/banana/sleep.d/05-slow; chmod +x /etc/banana/sleep.d/05-slow
setsid sleep 600 & LRG=$!; sleep 0.1; echo $LRG > /run/banana/game.pid
start
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
sleep 0.4
echo 0 > /run/test/lid; ev $EV_SW $SW_LID 0
if asleep; then bad "suspended with the lid open again"; wake; else ok "no suspend once the lid was open again"; fi
sleep 0.3
[ "$(awk '{print $3}' /proc/$LRG/stat)" != T ] && ok "game running again" || bad "game still stopped"
grep -q 'lid opened again before the suspend' /data/banana/logs/sleep.log && ok "logged" || bad "not logged: $(tail -n 2 /data/banana/logs/sleep.log | tr '\n' '|')"
rm -f /etc/banana/sleep.d/05-slow /run/banana/game.pid; kill $LRG 2>/dev/null || true
stop
